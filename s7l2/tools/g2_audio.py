# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for the G2 two-node audio tools (standard library only).

Used by tools/measure_g2_audio_rate.py and tools/run_g2_audio_demo.py:
node configuration, compact-binary recordings, float WAV files, the
AudioEqualizer reference and process metrics. Not a command-line tool.
"""
import cmath
import hashlib
import math
import os
from pathlib import Path
import platform
import random
import socket
import ssl
import struct
import time

NODE_A = "11111111-2222-3333-4444-555555555555"   # the test credentials' node identities
NODE_B = "33333333-4444-5555-6666-777777777777"
OID = "2.25.4266950079767543379770189370854073200"
DESIGN_RATE = 48000.0
# AudioEqualizer bands (docs/CONTROL_ELEMENTS.md): kind, frequency, bandwidth.
EQ_BANDS = (("low", 100.0, 0.0), ("peak", 400.0, 400.0), ("peak", 1600.0, 1600.0), ("peak", 6000.0, 6000.0), ("high", 12000.0, 0.0))


def node_config(certs, cert_name, port, studio_fp, security="shared", bind="127.0.0.1"):
    """An idle node that serves the mTLS control plane (and data, unless split)."""
    config = {
        "name": "idle",
        "elements": [{"id": "idle", "type": "Constant", "params": {"value": 0}}],
        "connections": [],
        "dt": 1.0,  # the idle startup graph is not stepped (no execution.step_rate_hz)
        "websocket": {"server": {"enabled": True, "port": port, "bind_address": bind, "tls": {
            "enabled": True, "cert_file": str(Path(certs) / f"{cert_name}.cert.pem"),
            "key_file": str(Path(certs) / f"{cert_name}.key.pem"),
            "ca_file": str(Path(certs) / "s7_root_ca.cert.pem"), "require_client_cert": True}}},
        "control": {"studio_key_purpose_oid": OID, "studio_certificate_sha256": studio_fp},
        "clock": {"assume_synchronized": True},
    }
    if security != "shared":
        config["websocket"]["data_server"] = {"enabled": True, "port": port + 2, "bind_address": bind, "security": security}
    return config


def studio_fingerprint(certs):
    der = ssl.PEM_cert_to_DER_cert((Path(certs) / "s7_studio.cert.pem").read_text())
    return hashlib.sha256(der).hexdigest()


def tls_args(certs):
    certs = Path(certs)
    return ["--cert", str(certs / "s7_studio.cert.pem"), "--key", str(certs / "s7_studio.key.pem"),
            "--ca", str(certs / "s7_root_ca.cert.pem")]


def read_recording(path):
    """Samples of an F64 compact-binary v3 recording (docs/RECORDING_FORMAT.md) and its rate."""
    data = Path(path).read_bytes()
    if len(data) < 40 or data[:4] != b"S7B1" or data[4] != 3 or data[5] != 0 or struct.unpack_from("<H", data, 6)[0] != 0x000F:
        raise ValueError(f"{Path(path).name}: not an F64 compact binary v3 recording")
    header, numerator, denominator = struct.unpack_from("<QQQ", data, 8)
    payload = memoryview(data)[header:]
    if len(payload) % 8:
        raise ValueError(f"{Path(path).name}: truncated sample")
    return struct.unpack(f"<{len(payload) // 8}d", payload), numerator / denominator


def write_wav_float(path, samples, rate):
    """Mono 32-bit IEEE float WAV: no requantization, no dither."""
    data = struct.pack(f"<{len(samples)}f", *samples)
    fmt = struct.pack("<HHIIHHH", 3, 1, int(rate), int(rate) * 4, 4, 32, 0)
    fact = struct.pack("<I", len(samples))
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"fact" + struct.pack("<I", 4) + fact
    body += b"data" + struct.pack("<I", len(data)) + data
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def _allpass(kind, frequency, bandwidth, design=DESIGN_RATE):
    if kind == "peak":
        t = math.tan(math.pi * bandwidth / design)
        return (t - 1) / (t + 1), -math.cos(2 * math.pi * frequency / design)
    t = math.tan(math.pi * frequency / design)
    return (t - 1) / (t + 1), None


def equalizer_reference(x, gains_db):
    """The bundled AudioEqualizer, sample by sample from its documented design:
    per band a fixed allpass A(z) mixed with K = 10^(dB/20)."""
    signal_in = list(x)
    for (kind, frequency, bandwidth), gain_db in zip(EQ_BANDS, gains_db):
        k = 10 ** (gain_db / 20)
        c, d = _allpass(kind, frequency, bandwidth)
        out, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
        for v in signal_in:
            if kind == "peak":   # A(z) = (-c + d(1-c) z^-1 + z^-2) / (1 + d(1-c) z^-1 - c z^-2)
                ap = -c * v + d * (1 - c) * x1 + x2 - d * (1 - c) * y1 + c * y2
                x2, y2 = x1, y1
            else:                # A(z) = (a + z^-1) / (1 + a z^-1), a = c
                ap = c * v + x1 - c * y1
            x1, y1 = v, ap
            out.append((1 + k) / 2 * v + ((k - 1) / 2 if kind == "low" else (1 - k) / 2) * ap)
        signal_in = out
    return signal_in


def equalizer_response(frequency, gains_db, rate=DESIGN_RATE):
    """|H(e^jw)| of the documented design, evaluated in the frequency domain."""
    z1 = cmath.exp(-2j * math.pi * frequency / rate)
    h = 1
    for (kind, f0, bandwidth), gain_db in zip(EQ_BANDS, gains_db):
        k = 10 ** (gain_db / 20)
        c, d = _allpass(kind, f0, bandwidth)
        if kind == "peak":
            a = (-c + d * (1 - c) * z1 + z1 * z1) / (1 + d * (1 - c) * z1 - c * z1 * z1)
        else:
            a = (c + z1) / (1 + c * z1)
        h *= (1 + k) / 2 + ((k - 1) / 2 if kind == "low" else (1 - k) / 2) * a
    return abs(h)


def cpu_seconds(pid):
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def memory_kib(pid):
    values = {}
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        key, _, rest = line.partition(":")
        if key in ("VmRSS", "VmHWM"):
            values[key] = int(rest.split()[0])
    return values


def host_profile():
    def os_name():
        try:
            for line in Path("/etc/os-release").read_text().splitlines():
                if line.startswith("PRETTY_NAME="):
                    return line.split("=", 1)[1].strip('"')
        except OSError:
            pass
        return platform.system()
    cpu = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                if line.startswith("model name")), platform.processor())
    memory = next((int(line.split()[1]) for line in Path("/proc/meminfo").read_text().splitlines()
                   if line.startswith("MemTotal:")), 0)
    # No host name: reports are meant to be shared; roles identify the nodes.
    return {"os": os_name(), "kernel": platform.release(), "cpu": cpu,
            "logical_cpus": os.cpu_count(), "memory_mib": memory // 1024, "load_average": os.getloadavg()}


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def free_port_pair():
    """Two consecutive free local ports (plus two for split data listeners)."""
    for _ in range(100):
        base = random.randrange(20000, 24990, 4)
        try:
            for port in range(base, base + 4):
                with socket.socket() as s:
                    s.bind(("127.0.0.1", port))
            return base, base + 1
        except OSError:
            continue
    raise RuntimeError("no free local ports")


def wait_port(host, port, process=None, timeout=15.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise RuntimeError(f"node on port {port} exited with {process.returncode}")
        try:
            with socket.create_connection((host, port), timeout=0.3):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError(f"nothing listens on {host}:{port}")
