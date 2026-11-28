#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""G2 two-node audio equalizer: deploy, measure and check (docs/G2_AUDIO_CHAIN.md).

Deploys examples/audio/two_node_equalizer.s7diagram: node A generates a
deterministic 48 kHz programme (log sweep, then five tones); node B runs the
bundled AudioEqualizer over a buffered stream and records its input and
output. The check compares the recordings with
  * the programme computed here (every sample: none lost, repeated or
    reordered),
  * an independent sample-level implementation of the equalizer,
  * the same graph run locally in one process (s7runtime simulation mode),
  * the equalizer's analytic response (tone gains and the sweep curve),
and writes report.json, report.md, response.svg and float WAV files.

Subcommands:
  local     both nodes on this host (the default for tests)
  two-host  node A here, node B on another host over ssh
  node      run one node (used by local/two-host, or by hand on each host)
  deploy    deploy to running nodes and record control-plane metrics
  check     check recordings and write the report

Examples:
  python3 tools/run_g2_audio_demo.py local --output /tmp/g2-local
  python3 tools/run_g2_audio_demo.py two-host --host-b al@192.168.0.249 \\
      --address-a 192.168.0.18 --address-b 192.168.0.249 \\
      --remote-src /var/tmp/s7/src --remote-runtime /var/tmp/s7/build/s7runtime/s7runtime \\
      --certs /tmp/g2-certs --output /tmp/g2-two-host

Nothing outside the output directory (and, for two-host, the given remote
directory) is written. No audio device, firewall or system setting is
touched; opening the node ports between hosts is the operator's step.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from g2_audio import (NODE_A, NODE_B, cpu_seconds, equalizer_reference, equalizer_response, free_port_pair, host_profile,
                      memory_kib, node_config, read_recording, sha256_file, studio_fingerprint, tls_args, wait_port,
                      write_wav_float)

ROOT = Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "examples" / "audio" / "two_node_equalizer.s7diagram"
RATE = 48000
PROGRAMME_SECONDS = 15.0   # the example's sweep (0-10 s) and tones (from 10 s)
REQUIRED_SECONDS = 14.5    # recorded length the check needs
TONE_WINDOW = (10.5, 14.0)  # whole periods of every tone; the switch transient has decayed
CERT_NAMES = {"a": "s7_runtime_node", "b": "s7_runtime_node_b"}
# Tolerances (docs/G2_AUDIO_CHAIN.md): binary64 rounding for sample checks,
# the projection's exactness for whole-period tone gains, the finite window
# of a sweep for the response curve.
TOL_INPUT = 1e-9
TOL_OUTPUT = 1e-9
TOL_TONE_DB = 0.01
TOL_SWEEP_DB = 0.5


def default_build():
    for name in ("build-release", "build"):
        if (ROOT / name / "s7runtime" / "s7runtime").is_file():
            return ROOT / name
    return ROOT / "build-release"


# ---------------------------------------------------------------- node

def cmd_node(args):
    """Run one s7runtime node until --seconds pass, SIGTERM, or <dir>/stop appears."""
    work = Path(args.dir)
    work.mkdir(parents=True, exist_ok=True)
    (work / "stop").unlink(missing_ok=True)
    config = node_config(args.certs, CERT_NAMES[args.role], args.port, studio_fingerprint(args.certs), args.data_security, args.bind)
    (work / f"node-{args.role}.json").write_text(json.dumps(config, indent=1))
    log = open(work / f"node-{args.role}.log", "w")
    process = subprocess.Popen([str(args.runtime), "--schema", str(work / f"node-{args.role}.json"),
                                "--state-dir", str(work / f"state-{args.role}"), "--serve-seconds", str(int(args.seconds) + 30)],
                               cwd=work, stdout=log, stderr=subprocess.STDOUT)
    stopping = {"signal": False}
    signal.signal(signal.SIGTERM, lambda *_: stopping.update(signal=True))
    signal.signal(signal.SIGINT, lambda *_: stopping.update(signal=True))
    samples = []
    started = time.monotonic()
    try:
        wait_port("127.0.0.1" if args.bind in ("0.0.0.0", "127.0.0.1") else args.bind, args.port, process)
        print("ready", flush=True)
        while process.poll() is None and not stopping["signal"] and not (work / "stop").exists() \
                and time.monotonic() - started < args.seconds:
            samples.append({"t": time.time(), "cpu_s": cpu_seconds(process.pid), **memory_kib(process.pid)})
            time.sleep(0.5)
        if process.poll() is None:
            samples.append({"t": time.time(), "cpu_s": cpu_seconds(process.pid), **memory_kib(process.pid)})
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        log.close()
    metrics = {"role": args.role, "host": host_profile(), "runtime": str(args.runtime), "runtime_sha256": sha256_file(args.runtime),
               "exit_code": process.returncode, "samples": samples}
    (work / f"node-{args.role}-metrics.json").write_text(json.dumps(metrics, indent=1))
    print("stopped", flush=True)
    return 0


# ---------------------------------------------------------------- deploy

def ctl(args, *command, cwd=None):
    result = subprocess.run([str(args.ctl), *command, *tls_args(args.certs)], cwd=cwd, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise RuntimeError(f"s7ctl {command[0]} failed: {result.stdout}{result.stderr}")
    return result.stdout


def plan(args, out):
    """The example with these endpoints and buffered stream settings, planned by s7ctl check."""
    diagram = json.loads(EXAMPLE.read_text())
    for node, endpoint in zip(diagram["runtime_nodes"], (args.endpoint_a, args.endpoint_b)):
        node["endpoint"] = endpoint
        if args.data_security != "shared":
            host = endpoint.split("://", 1)[1].rsplit(":", 1)
            node["data_endpoint"] = ("wss" if args.data_security == "mtls" else "ws") + f"://{host[0]}:{int(host[1]) + 2}"
            node["data_security"] = args.data_security
    (out / "diagram.json").write_text(json.dumps(diagram))
    result = subprocess.run([str(args.ctl), "check", str(out / "diagram.json"), "--write", str(out / "planned.json")],
                            capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise RuntimeError(f"s7ctl check failed: {result.stdout}{result.stderr}")
    planned = json.loads((out / "planned.json").read_text())
    for stream in planned["streams"]:
        stream["max_elements_per_packet"] = args.packet
        stream["max_batch_latency_ms"] = args.batch_ms
        stream["max_wait_ms"] = args.max_wait_ms
    (out / "planned.json").write_text(json.dumps(planned, indent=1))
    return planned


def cmd_deploy(args, out=None):
    out = Path(out or args.output)
    out.mkdir(parents=True, exist_ok=True)
    planned = plan(args, out)
    deployed = ctl(args, "deploy", str(out / "planned.json"))
    activate = next(int(line.split()[1]) for line in deployed.splitlines() if line.startswith("activate_at_ns "))
    end = activate / 1e9 + args.seconds
    polls = []
    while time.time() < end:
        poll = {"t": time.time()}
        for name, endpoint in (("a", args.endpoint_a), ("b", args.endpoint_b)):
            try:
                poll[name] = {"timing": json.loads(ctl(args, "info", endpoint))["state"]["timing"],
                              "streams": json.loads(ctl(args, "streams", endpoint))["streams"]}
            except (RuntimeError, ValueError, KeyError) as error:
                poll[name] = {"error": str(error)}
        polls.append(poll)
        time.sleep(max(0.0, min(1.0, end - time.time())))
    metrics = {"activate_at_ns": activate, "seconds": args.seconds, "endpoints": [args.endpoint_a, args.endpoint_b],
               "data_security": args.data_security, "streams": planned["streams"], "polls": polls}
    (out / "deploy-metrics.json").write_text(json.dumps(metrics, indent=1))
    print(f"deployed; activation {activate}; {len(polls)} polls", flush=True)
    return 0


# ---------------------------------------------------------------- check

def programme(count):
    """The example's programme, from the documented source algorithms."""
    doc = json.loads(EXAMPLE.read_text())
    params = {e["id"]: e["params"] for e in doc["schema"]["elements"]}
    sweep = params["sweep"]
    f0, f1, amplitude = float(sweep["start_hz"]), float(sweep["end_hz"]), float(sweep["amplitude"])
    sweep_count = round(float(sweep["sweep_time"]) * RATE)
    switch = round(float(params["tone_gate"]["start_time"]) * RATE)
    tones = [(float(params[k]["amplitude"]), float(params[k]["frequency_hz"])) for k in sorted(params) if k.startswith("tone_")
             and "frequency_hz" in params[k]]
    log_ratio = math.log(f1) - math.log(f0)
    phase, values = 0.0, []
    for n in range(count):
        # ChirpGenerator: output, then advance the phase (in cycles) by the
        # frequency of this sample; logarithmic interpolation, then hold.
        frequency = f1 if n >= sweep_count else math.exp(math.log(f0) + (n / sweep_count) * log_ratio)
        swept = amplitude * math.sin(2 * math.pi * phase)
        phase = math.fmod(phase + math.fmod(frequency / RATE, 1.0), 1.0)
        if n < switch:
            values.append(swept)
        else:   # AnalogOscillator tones: A sin(2 pi f n / rate)
            values.append(sum(a * math.sin(2 * math.pi * f * n / RATE) for a, f in tones))
    return values, tones, (f0, f1, float(sweep["sweep_time"]))


def gains_db():
    doc = json.loads(EXAMPLE.read_text())
    params = {e["id"]: e["params"] for e in doc["schema"]["elements"]}
    return [float(params[name]["value"]) for name in ("low_db", "low_mid_db", "mid_db", "high_mid_db", "high_db")]


def local_run(args, out, seconds):
    """The same graph in one process: s7runtime simulation mode, no network."""
    work = out / "local-run"
    work.mkdir(exist_ok=True)
    schema = copy.deepcopy(json.loads(EXAMPLE.read_text())["schema"])
    for element in schema["elements"]:
        element.pop("node", None)
    schema["simulation"] = {"periods": int(math.ceil(seconds)), "sample_rate_hz": RATE, "frequency_hz": 1}
    (work / "local.json").write_text(json.dumps(schema))
    started = time.monotonic()
    result = subprocess.run([str(args.runtime), "--schema", "local.json", "--state-dir", "state"], cwd=work,
                            capture_output=True, text=True, timeout=600)
    (work / "local.log").write_text(result.stdout + result.stderr)
    if result.returncode != 0:
        raise RuntimeError(f"local run failed: {result.stderr[-2000:]}")
    return work, time.monotonic() - started


def projection(signal, frequency, start, stop):
    s = c = 0.0
    w = 2 * math.pi * frequency / RATE
    for n in range(start, stop):
        s += signal[n] * math.sin(w * n)
        c += signal[n] * math.cos(w * n)
    return math.hypot(s, c) * 2 / (stop - start)


def rms(signal, start, stop):
    return math.sqrt(sum(v * v for v in signal[start:stop]) / (stop - start))


def summarize_metrics(directory):
    """Throughput, lag, latency, queues, CPU and memory from the metric files."""
    summary = {}
    deploy_path = directory / "deploy-metrics.json"
    if deploy_path.exists():
        deploy = json.loads(deploy_path.read_text())
        polls = [p for p in deploy["polls"] if "timing" in p.get("a", {}) and "timing" in p.get("b", {})]
        summary["data_security"] = deploy["data_security"]
        summary["stream_settings"] = {k: deploy["streams"][0][k] for k in ("max_elements_per_packet", "max_batch_latency_ms", "max_wait_ms")}
        if len(polls) >= 3:
            first, last = polls[1], polls[-1]   # skip the activation poll
            wall = last["t"] - first["t"]
            for name in ("a", "b"):
                t0, t1 = first[name]["timing"], last[name]["timing"]
                lags = [p[name]["timing"]["lag_ns"] / 1e6 for p in polls[1:]]
                summary[f"node_{name}"] = {"step_rate_hz": (t1["steps"] - t0["steps"]) / wall, "lag_ms_last": lags[-1],
                                           "lag_ms_max_polled": max(lags), "max_lag_ms_since_activation": t1["max_lag_ns"] / 1e6,
                                           "rebases": t1.get("rebases", 0)}
            consumer = [s for p in polls for s in p["b"]["streams"] if s.get("role") == "consumer"]
            producer = [s for p in polls for s in p["a"]["streams"] if s.get("role") == "producer"]
            if consumer and producer:
                last_consumer, last_producer = consumer[-1], producer[-1]
                summary["stream"] = {
                    "elements_received": last_consumer["elements"], "packets": last_consumer["packets"],
                    "consumer_buffered_elements_max": max(s["buffered_elements"] for s in consumer),
                    "consumer_buffered_elements_mean": sum(s["buffered_elements"] for s in consumer) / len(consumer),
                    "producer_send_log_depth_max": max(s["send_log_depth"] for s in producer),
                    # Packet send-to-receive latency (synchronized UTC clocks across hosts).
                    # p50/p99 are power-of-two microsecond bucket bounds; min/mean/max are exact.
                    "latency_ms": {k: last_consumer["latency"][k + "_ns"] / 1e6 for k in ("min", "mean", "max", "p50", "p99")},
                    "late": last_consumer["latency"].get("late", 0),
                    "faults": sorted({s["fault"] for s in consumer + producer}),
                    "replayed": last_consumer["replayed"], "duplicates_discarded": last_consumer["duplicates_discarded"],
                    "malformed": last_consumer["malformed"], "lost_resumes": last_producer["lost_resumes"],
                }
    for name in ("a", "b"):
        path = directory / f"node-{name}-metrics.json"
        if path.exists():
            node = json.loads(path.read_text())
            samples = node["samples"]
            entry = summary.setdefault(f"node_{name}", {})
            entry["host"] = node["host"]
            entry["runtime_sha256"] = node["runtime_sha256"]
            entry["peak_rss_mib"] = max((s.get("VmHWM", 0) for s in samples), default=0) / 1024
            # CPU over the steady part: from 3 s after the first sample to the last.
            steady = [s for s in samples if s["t"] >= samples[0]["t"] + 3] if samples else []
            if len(steady) >= 2:
                entry["cpu_percent"] = 100 * (steady[-1]["cpu_s"] - steady[0]["cpu_s"]) / (steady[-1]["t"] - steady[0]["t"])
    return summary


def response_svg(path, sweep_points, tone_points, gains):
    """The equalizer curve: analytic response, sweep measurement and tone gains."""
    width, height, left, top, right, bottom = 820, 420, 70, 30, 20, 50
    lo, hi, db_lo, db_hi = math.log10(20), math.log10(20000), -15, 10
    x = lambda f: left + (math.log10(f) - lo) / (hi - lo) * (width - left - right)
    y = lambda db: top + (db_hi - db) / (db_hi - db_lo) * (height - top - bottom)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" font-family="sans-serif" font-size="12">',
             '<rect width="100%" height="100%" fill="white"/>']
    for db in range(db_lo, db_hi + 1, 5):
        parts.append(f'<line x1="{left}" x2="{width - right}" y1="{y(db):.1f}" y2="{y(db):.1f}" stroke="#ddd"/>'
                     f'<text x="{left - 8}" y="{y(db) + 4:.1f}" text-anchor="end">{db}</text>')
    for f in (20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000):
        label = f"{f // 1000}k" if f >= 1000 else str(f)
        parts.append(f'<line x1="{x(f):.1f}" x2="{x(f):.1f}" y1="{top}" y2="{height - bottom}" stroke="#ddd"/>'
                     f'<text x="{x(f):.1f}" y="{height - bottom + 16}" text-anchor="middle">{label}</text>')
    curve = " ".join(f"{x(f):.1f},{y(20 * math.log10(equalizer_response(f, gains))):.1f}"
                     for f in (10 ** (lo + i * (hi - lo) / 400) for i in range(401)))
    parts.append(f'<polyline points="{curve}" fill="none" stroke="#1f77b4" stroke-width="2"/>')
    for f, db in sweep_points:
        parts.append(f'<circle cx="{x(f):.1f}" cy="{y(db):.1f}" r="3" fill="none" stroke="#d62728"/>')
    for f, db in tone_points:
        parts.append(f'<rect x="{x(f) - 4:.1f}" y="{y(db) - 4:.1f}" width="8" height="8" fill="#2ca02c"/>')
    parts += [f'<text x="{width / 2}" y="{height - 12}" text-anchor="middle">frequency (Hz)</text>',
              f'<text x="16" y="{height / 2}" transform="rotate(-90 16 {height / 2})" text-anchor="middle">gain (dB)</text>',
              f'<text x="{left + 10}" y="{top + 14}" fill="#1f77b4">analytic response</text>',
              f'<text x="{left + 10}" y="{top + 30}" fill="#d62728">measured from the sweep (node B recordings)</text>',
              f'<text x="{left + 10}" y="{top + 46}" fill="#2ca02c">measured tone gains</text>', '</svg>']
    Path(path).write_text("\n".join(parts) + "\n")


def cmd_check(args, directory=None, out=None):
    directory = Path(directory or args.recordings)
    for name in ("record_in.s7b", "record_out.s7b"):
        if not (directory / name).is_file():
            raise RuntimeError(f"{directory / name} not found")
    out = Path(out or args.output or directory)
    out.mkdir(parents=True, exist_ok=True)
    problems, results = [], {}
    x, rate_in = read_recording(directory / "record_in.s7b")
    y, rate_out = read_recording(directory / "record_out.s7b")
    count = min(len(x), len(y))
    results["recorded_samples"] = {"input": len(x), "output": len(y), "rate_hz": rate_in}
    if rate_in != RATE or rate_out != RATE:
        problems.append(f"recording rates {rate_in}/{rate_out} Hz, expected {RATE}")
    if len(x) != len(y):
        problems.append(f"input and output recordings differ in length: {len(x)} / {len(y)}")
    if count < REQUIRED_SECONDS * RATE:
        problems.append(f"only {count / RATE:.2f} s recorded, need {REQUIRED_SECONDS} s")
        count = min(count, len(x))
    gains = gains_db()

    # 1. Every received sample is the programme's sample with the same index.
    expected, tones, (f0, f1, sweep_time) = programme(count)
    errors = [abs(a - b) for a, b in zip(x[:count], expected)]
    worst = max(errors, default=math.inf)
    results["input_vs_programme"] = {"max_abs_error": worst, "tolerance": TOL_INPUT,
                                     "first_bad_sample": next((n for n, e in enumerate(errors) if e > TOL_INPUT), None)}
    if worst > TOL_INPUT:
        problems.append(f"node B's input differs from the programme by {worst:.3g} (first at sample "
                        f"{results['input_vs_programme']['first_bad_sample']})")

    # 2. The output is the documented equalizer applied to that input.
    reference = equalizer_reference(x[:count], gains)
    worst = max((abs(a - b) for a, b in zip(y[:count], reference)), default=math.inf)
    results["output_vs_reference_equalizer"] = {"max_abs_error": worst, "tolerance": TOL_OUTPUT}
    if worst > TOL_OUTPUT:
        problems.append(f"node B's output differs from the reference equalizer by {worst:.3g}")

    # 3. The same graph in one process gives the same samples.
    work, local_seconds = local_run(args, out, count / RATE + 0.5)
    lx = read_recording(work / "record_in.s7b")[0]
    ly = read_recording(work / "record_out.s7b")[0]
    common = min(count, len(lx), len(ly))
    differ_in = sum(1 for a, b in zip(x[:common], lx) if a != b)
    differ_out = sum(1 for a, b in zip(y[:common], ly) if a != b)
    results["distributed_vs_local"] = {"compared_samples": common, "input_samples_differing": differ_in,
                                       "output_samples_differing": differ_out, "local_run_seconds": local_seconds,
                                       "max_abs_output_difference": max((abs(a - b) for a, b in zip(y[:common], ly)), default=0.0)}
    if differ_in or differ_out or common < REQUIRED_SECONDS * RATE:
        problems.append(f"distributed and local runs differ: {differ_in} input / {differ_out} output samples of {common}")
    if not args.keep_local:
        for name in ("record_in.s7b", "record_out.s7b"):
            (work / name).unlink(missing_ok=True)

    # 4. Tone gains against the analytic response.
    start, stop = (int(t * RATE) for t in TONE_WINDOW)
    tone_rows = []
    if count >= stop:
        for amplitude, frequency in tones:
            measured = 20 * math.log10(projection(y, frequency, start, stop) / projection(x, frequency, start, stop))
            analytic = 20 * math.log10(equalizer_response(frequency, gains))
            tone_rows.append({"frequency_hz": frequency, "measured_db": measured, "analytic_db": analytic, "error_db": measured - analytic})
            if abs(measured - analytic) > TOL_TONE_DB:
                problems.append(f"tone {frequency:g} Hz: {measured:.3f} dB, expected {analytic:.3f} dB")
    results["tone_gains"] = {"window_s": TONE_WINDOW, "tolerance_db": TOL_TONE_DB, "tones": tone_rows}

    # 5. The response curve measured from the sweep.
    sweep_rows = []
    third_octaves = [31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500,
                     3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]
    for frequency in third_octaves:
        centre = sweep_time * math.log(frequency / f0) / math.log(f1 / f0)
        half = max(0.02, 3 / frequency)
        a, b = int((centre - half) * RATE), int((centre + half) * RATE)
        if a < int(0.05 * RATE) or b > int(sweep_time * RATE) or b > count:
            continue
        measured = 20 * math.log10(rms(y, a, b) / rms(x, a, b))
        analytic = 20 * math.log10(equalizer_response(frequency, gains))
        sweep_rows.append({"frequency_hz": frequency, "measured_db": measured, "analytic_db": analytic, "error_db": measured - analytic})
        if abs(measured - analytic) > TOL_SWEEP_DB:
            problems.append(f"sweep at {frequency:g} Hz: {measured:.2f} dB, expected {analytic:.2f} dB")
    results["sweep_response"] = {"tolerance_db": TOL_SWEEP_DB, "points": sweep_rows}
    results["levels"] = {"input_peak": max(abs(v) for v in x[:count]), "output_peak": max(abs(v) for v in y[:count])}

    # Listening copies and the curve.
    write_wav_float(out / "input.wav", x[:count], RATE)
    write_wav_float(out / "output.wav", y[:count], RATE)
    response_svg(out / "response.svg", [(r["frequency_hz"], r["measured_db"]) for r in sweep_rows],
                 [(r["frequency_hz"], r["measured_db"]) for r in tone_rows], gains)

    metrics = summarize_metrics(directory)
    stream = metrics.get("stream", {})
    if stream and (stream["faults"] != ["NONE"] or stream["malformed"] or stream["lost_resumes"]):
        problems.append(f"stream faults {stream['faults']}, malformed {stream['malformed']}, lost resumes {stream['lost_resumes']}")
    revision = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--", "."], capture_output=True, text=True).stdout.strip())
    report = {"passed": not problems, "problems": problems, "revision": revision, "worktree_modified": dirty,
              "diagram": str(EXAMPLE.relative_to(ROOT)), "diagram_sha256": sha256_file(EXAMPLE),
              "script_sha256": sha256_file(__file__), "equalizer_gains_db": gains, "checker_host": host_profile(),
              "results": results, "metrics": metrics}
    (out / "report.json").write_text(json.dumps(report, indent=1))
    (out / "report.md").write_text(markdown(report))
    print(("PASSED" if not problems else "FAILED") + f": report in {out}")
    for problem in problems:
        print(f"  - {problem}")
    return 0 if not problems else 1


def markdown(report):
    r, m = report["results"], report["metrics"]
    lines = [f"# G2 two-node audio equalizer: {'passed' if report['passed'] else 'FAILED'}", "",
             f"Revision `{report['revision'][:10]}`{' (modified worktree)' if report['worktree_modified'] else ''}, "
             f"diagram `{report['diagram']}` (sha256 {report['diagram_sha256'][:12]}), gains {report['equalizer_gains_db']} dB.", ""]
    if report["problems"]:
        lines += ["## Problems", ""] + [f"- {p}" for p in report["problems"]] + [""]
    rec = r["recorded_samples"]
    lines += ["## Correctness", "",
              f"- Recorded on node B: {rec['input']} input and {rec['output']} output samples at {rec['rate_hz']:g} Hz.",
              f"- Input vs programme: max error {r['input_vs_programme']['max_abs_error']:.3g} (tolerance {TOL_INPUT:g}).",
              f"- Output vs independent equalizer: max error {r['output_vs_reference_equalizer']['max_abs_error']:.3g} (tolerance {TOL_OUTPUT:g}).",
              f"- Distributed vs local one-process run: {r['distributed_vs_local']['output_samples_differing']} of "
              f"{r['distributed_vs_local']['compared_samples']} output samples differ "
              f"(max difference {r['distributed_vs_local']['max_abs_output_difference']:.3g}).",
              f"- Peak level: input {r['levels']['input_peak']:.3f}, output {r['levels']['output_peak']:.3f}.", "",
              "| Tone (Hz) | Measured (dB) | Analytic (dB) | Error (dB) |", "| ---: | ---: | ---: | ---: |"]
    lines += [f"| {t['frequency_hz']:g} | {t['measured_db']:.4f} | {t['analytic_db']:.4f} | {t['error_db']:+.5f} |" for t in r["tone_gains"]["tones"]]
    sweep = r["sweep_response"]["points"]
    if sweep:
        worst = max(sweep, key=lambda p: abs(p["error_db"]))
        lines += ["", f"Sweep response at {len(sweep)} third-octave points: largest deviation {worst['error_db']:+.3f} dB at "
                  f"{worst['frequency_hz']:g} Hz (tolerance {TOL_SWEEP_DB} dB). See response.svg.", ""]
    lines += ["## Resources and timing", ""]
    for name in ("a", "b"):
        node = m.get(f"node_{name}", {})
        host = node.get("host", {})
        lines.append(f"- Node {name.upper()}: {host.get('os', '?')}, kernel {host.get('kernel', '?')}, {host.get('cpu', '?')} "
                     f"({host.get('logical_cpus', '?')} CPUs, {host.get('memory_mib', '?')} MiB); "
                     f"step rate {node.get('step_rate_hz', float('nan')):.1f} Hz, lag last/max {node.get('lag_ms_last', float('nan')):.1f}/"
                     f"{node.get('max_lag_ms_since_activation', float('nan')):.1f} ms, CPU {node.get('cpu_percent', float('nan')):.1f} %, "
                     f"peak RSS {node.get('peak_rss_mib', float('nan')):.1f} MiB.")
    stream = m.get("stream")
    if stream:
        lines += [f"- Stream ({m.get('data_security')}, {m.get('stream_settings')}): {stream['elements_received']} samples in "
                  f"{stream['packets']} packets; packet latency min/mean/max {stream['latency_ms']['min']:.2f}/"
                  f"{stream['latency_ms']['mean']:.2f}/{stream['latency_ms']['max']:.2f} ms (p99 bucket bound "
                  f"{stream['latency_ms']['p99']:.2f} ms); node B buffer max/mean "
                  f"{stream['consumer_buffered_elements_max']}/{stream['consumer_buffered_elements_mean']:.0f} samples; "
                  f"node A send log max {stream['producer_send_log_depth_max']} packets; faults {stream['faults']}."]
    lines += ["", "Model-time results (the samples) are checked exactly; timing and resources are wall-clock measurements "
              "of this run, not guarantees.", ""]
    return "\n".join(lines)


# ---------------------------------------------------------------- orchestration

def node_command(args, role, port, directory, bind, runtime, certs, script=__file__, python="python3"):
    return [python, str(script), "node", "--role", role, "--port", str(port), "--bind", bind, "--dir", str(directory),
            "--certs", str(certs), "--runtime", str(runtime), "--seconds", str(args.seconds + 60),
            "--data-security", args.data_security]


def start_node(command, label):
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    line = process.stdout.readline().strip()
    if line != "ready":
        rest = process.stdout.read() if process.poll() is not None else ""
        process.kill()
        raise RuntimeError(f"node {label} did not start: {line} {rest}")
    return process


def finish_node(process, stop, label):
    stop()
    try:
        output = process.communicate(timeout=60)[0]
    except subprocess.TimeoutExpired:
        process.kill()
        output = process.communicate()[0]
    if "stopped" not in output:
        raise RuntimeError(f"node {label} did not stop cleanly: {output}")


def prepare_output(path):
    out = Path(path)
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f"{out} exists and is not empty")
    out.mkdir(parents=True, exist_ok=True)
    return out


def cmd_local(args):
    if args.output is None:
        # A temporary directory, kept only when the check fails (tests).
        out = Path(tempfile.mkdtemp(prefix="s7-g2-audio-"))
        try:
            status = run_local(args, out)
        except BaseException:
            print(f"kept {out}", file=sys.stderr)
            raise
        if status == 0:
            shutil.rmtree(out)
        else:
            print(f"kept {out}", file=sys.stderr)
        return status
    return run_local(args, prepare_output(args.output))


def run_local(args, out):
    ports = free_port_pair()
    args.endpoint_a, args.endpoint_b = (f"wss://127.0.0.1:{p}" for p in ports)
    nodes = {}
    try:
        for role, port in zip("ab", ports):
            nodes[role] = start_node(node_command(args, role, port, out / f"node-{role}", "127.0.0.1", args.runtime, args.certs), role)
        cmd_deploy(args, out)
    finally:
        # Consumer first: stopping the producer first would underrun node B.
        for role in "ba":
            if role in nodes:
                finish_node(nodes[role], lambda r=role: (out / f"node-{r}" / "stop").touch(), role)
    for role in "ab":
        shutil.copy(out / f"node-{role}" / f"node-{role}-metrics.json", out)
    for name in ("record_in.s7b", "record_out.s7b"):
        shutil.move(str(out / "node-b" / name), out / name)
    return cmd_check(args, out, out)


def cmd_two_host(args):
    out = prepare_output(args.output)
    port_a, port_b = args.port_a, args.port_b
    args.endpoint_a, args.endpoint_b = f"wss://{args.address_a}:{port_a}", f"wss://{args.address_b}:{port_b}"
    ssh = ["ssh", "-o", "BatchMode=yes", args.host_b]
    remote_dir, remote_certs = f"{args.remote_dir}/node-b", f"{args.remote_dir}/certs"
    subprocess.run(ssh + [f"rm -rf {shlex.quote(remote_dir)} && mkdir -p {shlex.quote(remote_dir)} {shlex.quote(remote_certs)}"], check=True)
    certs = Path(args.certs)
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", str(certs / "s7_root_ca.cert.pem"), str(certs / "s7_studio.cert.pem"),
                    str(certs / "s7_runtime_node_b.cert.pem"), str(certs / "s7_runtime_node_b.key.pem"),
                    f"{args.host_b}:{remote_certs}/"], check=True)
    nodes = {}
    try:
        nodes["a"] = start_node(node_command(args, "a", port_a, out / "node-a", args.address_a, args.runtime, certs), "a")
        remote = node_command(args, "b", port_b, remote_dir, args.address_b, args.remote_runtime, remote_certs,
                              script=f"{args.remote_src}/tools/run_g2_audio_demo.py")
        nodes["b"] = start_node(ssh + [" ".join(shlex.quote(part) for part in remote)], "b")
        cmd_deploy(args, out)
    finally:
        if "b" in nodes:
            finish_node(nodes["b"], lambda: subprocess.run(ssh + [f"touch {shlex.quote(remote_dir)}/stop"], check=False), "b")
        if "a" in nodes:
            finish_node(nodes["a"], lambda: (out / "node-a" / "stop").touch(), "a")
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", f"{args.host_b}:{remote_dir}/record_in.s7b",
                    f"{args.host_b}:{remote_dir}/record_out.s7b", f"{args.host_b}:{remote_dir}/node-b-metrics.json",
                    f"{args.host_b}:{remote_dir}/node-b.log", str(out)], check=True)
    shutil.copy(out / "node-a" / "node-a-metrics.json", out)
    return cmd_check(args, out, out)


def main():
    build = default_build()
    certs = ROOT / "s7runtime" / "examples" / "certs"
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)

    def common(q, runtime=True, ctl=False):
        q.add_argument("--certs", type=Path, default=certs, help="credential directory (tools/generate_test_runtime_certs.sh layout)")
        if runtime:
            q.add_argument("--runtime", type=Path, default=build / "s7runtime" / "s7runtime", help="s7runtime executable")
        if ctl:
            q.add_argument("--ctl", type=Path, default=build / "s7ctl" / "s7ctl", help="s7ctl executable")

    def deploying(q):
        q.add_argument("--seconds", type=float, default=PROGRAMME_SECONDS + 1, help="seconds to run after activation")
        q.add_argument("--data-security", choices=("shared", "mtls", "trusted_plaintext"), default="shared")
        q.add_argument("--packet", type=int, default=4800, help="stream max_elements_per_packet")
        q.add_argument("--batch-ms", type=int, default=100, help="stream max_batch_latency_ms")
        q.add_argument("--max-wait-ms", type=int, default=5000, help="stream max_wait_ms")

    q = sub.add_parser("local", help="both nodes on this host, then check")
    common(q, ctl=True); deploying(q)
    q.add_argument("--output", type=Path, help="report directory (default: a temporary one, removed when the check passes)")
    q.add_argument("--keep-local", action="store_true", help="keep the local run's recordings")

    q = sub.add_parser("two-host", help="node A here, node B on another host over ssh, then check")
    common(q, ctl=True); deploying(q)
    q.add_argument("--host-b", required=True, help="ssh destination of node B (user@host)")
    q.add_argument("--address-a", required=True, help="address node B reaches node A at")
    q.add_argument("--address-b", required=True, help="address of node B")
    q.add_argument("--port-a", type=int, default=9201)
    q.add_argument("--port-b", type=int, default=9202)
    q.add_argument("--remote-src", required=True, help="S7 source tree on host B (for this script)")
    q.add_argument("--remote-runtime", required=True, help="s7runtime on host B")
    q.add_argument("--remote-dir", default="/var/tmp/s7-g2", help="working directory on host B (replaced)")
    q.add_argument("--output", type=Path, required=True)
    q.add_argument("--keep-local", action="store_true")

    q = sub.add_parser("node", help="run one node (stops on SIGTERM, after --seconds, or when <dir>/stop appears)")
    common(q)
    q.add_argument("--role", choices=("a", "b"), required=True)
    q.add_argument("--port", type=int, required=True)
    q.add_argument("--bind", default="127.0.0.1")
    q.add_argument("--dir", type=Path, required=True)
    q.add_argument("--seconds", type=float, default=600)
    q.add_argument("--data-security", choices=("shared", "mtls", "trusted_plaintext"), default="shared")

    q = sub.add_parser("deploy", help="deploy to running nodes and record metrics")
    common(q, runtime=False, ctl=True); deploying(q)
    q.add_argument("--endpoint-a", required=True)
    q.add_argument("--endpoint-b", required=True)
    q.add_argument("--output", type=Path, required=True)

    q = sub.add_parser("check", help="check recordings (record_in.s7b, record_out.s7b) and write the report")
    common(q)
    q.add_argument("--recordings", type=Path, required=True, help="directory with the recordings and any metric files")
    q.add_argument("--output", type=Path, help="report directory (default: the recordings directory)")
    q.add_argument("--keep-local", action="store_true")

    args = p.parse_args()
    for attr in ("runtime", "ctl", "certs", "output"):
        if getattr(args, attr, None) is not None:
            setattr(args, attr, Path(getattr(args, attr)).resolve())   # nodes run in their own directories
    for attr in ("runtime", "ctl"):
        path = getattr(args, attr, None)
        if path is not None and not Path(path).is_file():
            p.error(f"{path} not found (build s7runtime and s7ctl, or pass --{attr})")
    if hasattr(args, "seconds") and args.command in ("local", "two-host", "deploy") and args.seconds < REQUIRED_SECONDS + 0.5:
        p.error(f"--seconds must be at least {REQUIRED_SECONDS + 0.5}")
    try:
        return {"local": cmd_local, "two-host": cmd_two_host, "node": cmd_node, "deploy": cmd_deploy, "check": cmd_check}[args.command](args)
    except (RuntimeError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
