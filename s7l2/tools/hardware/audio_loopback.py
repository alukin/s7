#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in two-host digital loopback through production S7 audio providers.

Uses two SSH raw-PCM pipes, not the S7 network protocol. The remote host must
already have the probe/libraries and a named virtual sink. No system defaults
are changed. NumPy/SciPy are used only for signal generation/offline analysis.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

import numpy as np
from scipy.signal import correlate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', required=True)
    parser.add_argument('--host', required=True)
    parser.add_argument('--remote-probe', required=True)
    parser.add_argument('--remote-lib', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--remote-work', required=True)
    parser.add_argument('--send-sink', default='s7_test_loop')
    parser.add_argument('--return-sink', default='s7_return_loop')
    parser.add_argument('--remote-sink', default='s7_test_loop')
    parser.add_argument('--seconds', type=int, default=28)
    args = parser.parse_args()
    if args.seconds < 8 or args.seconds > 300:
        parser.error('--seconds must be 8..300')
    if args.work.exists() and any(args.work.iterdir()):
        parser.error('--work must be empty to avoid replacing prior measurements')
    args.work.mkdir(parents=True, exist_ok=True)
    rate = 48000
    frames = args.seconds * rate
    bridge_frames = (args.seconds + 8) * rate
    record_frames = (args.seconds + 10) * rate
    marker_frames = rate * 2
    marker = np.random.default_rng(731207).integers(-4096, 4096, (marker_frames, 2), dtype=np.int32).astype(np.float32) / 65536
    source = np.zeros((frames, 2), dtype=np.float32)
    starts = [rate, (args.seconds - 4) * rate]
    for start in starts:
        source[start:start + marker_frames] = marker
    source.tofile(args.work / 'source.f32')
    marker.tofile(args.work / 'marker.f32')

    def ssh(command):
        return ['ssh', '-T', '-o', 'BatchMode=yes', '--', args.host, shlex.join(command)]

    def request(direction, sink):
        return dict(backend='pipewire', device=sink, direction=direction, type='F32',
                    channels=['0', '1'], rate=[rate, 1], block_frames=256, queue_frames=8192,
                    options={'capture_sink': 'true'} if direction == 'capture' else {})

    local_paths = {}
    for name, direction, sink in [('send-capture', 'capture', args.send_sink),
                                  ('send-play', 'playback', args.send_sink),
                                  ('return-capture', 'capture', args.return_sink),
                                  ('return-play', 'playback', args.return_sink)]:
        path = args.work / (name + '.request.json')
        path.write_text(json.dumps(request(direction, sink)))
        local_paths[name] = path
    subprocess.run(ssh(['mkdir', '-p', args.remote_work]), check=True)
    for direction in ['capture', 'playback']:
        path = args.remote_work + '/' + direction + '.request.json'
        writer = 'import pathlib,sys; p=pathlib.Path(sys.argv[1]); p.write_bytes(sys.stdin.buffer.read())'
        subprocess.run(ssh(['python3', '-c', writer, path]),
                       input=json.dumps(request(direction, args.remote_sink)).encode(), check=True)

    processes, logs = [], []

    def launch(command, name, **kwargs):
        log = (args.work / (name + '.log')).open('wb')
        logs.append(log)
        process = subprocess.Popen(command, stderr=log, **kwargs)
        processes.append((name, process))
        return process

    def local(name, count, summary):
        return [args.probe, '--request', str(local_paths[name]), '--frames', str(count),
                '--summary', str(args.work / (summary + '.json'))]

    def remote(direction, summary):
        return ssh(['env', 'LD_LIBRARY_PATH=' + args.remote_lib, args.remote_probe,
                    '--request', args.remote_work + '/' + direction + '.request.json',
                    '--frames', str(bridge_frames), '--summary', args.remote_work + '/' + summary + '.json'])

    started = time.monotonic()
    try:
        launch(local('return-capture', record_frames, 'return-record') + ['--output', str(args.work / 'return.f32')], 'return-record')
        remote_capture = launch(remote('capture', 'remote-capture') + ['--output', '-'], 'remote-capture', stdout=subprocess.PIPE)
        launch(local('return-play', bridge_frames, 'return-play') + ['--input', '-'], 'return-play', stdin=remote_capture.stdout)
        remote_capture.stdout.close()
        send_capture = launch(local('send-capture', bridge_frames, 'send-capture') + ['--output', '-'], 'send-capture', stdout=subprocess.PIPE)
        tee = launch(['tee', str(args.work / 'reference.f32')], 'reference-copy', stdin=send_capture.stdout, stdout=subprocess.PIPE)
        send_capture.stdout.close()
        launch(remote('playback', 'remote-play') + ['--input', '-'], 'remote-play', stdin=tee.stdout)
        tee.stdout.close()
        time.sleep(1)
        launch(local('send-play', frames, 'source-play') + ['--input', str(args.work / 'source.f32')], 'source-play')
        for name, process in processes:
            process.wait(timeout=max(1, args.seconds + 20 - (time.monotonic() - started)))
    finally:
        for _, process in processes:
            if process.poll() is None:
                process.terminate()
        for _, process in processes:
            if process.poll() is None:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for log in logs:
            log.close()

    for name in ['remote-capture', 'remote-play']:
        data = subprocess.run(ssh(['cat', args.remote_work + '/' + name + '.json']), check=True, capture_output=True).stdout
        (args.work / (name + '.json')).write_bytes(data)
    received = np.fromfile(args.work / 'return.f32', np.float32).reshape(-1, 2)
    reference = np.fromfile(args.work / 'reference.f32', np.float32).reshape(-1, 2)

    def find_markers(data):
        correlation = correlate(data[:, 0], marker[:2048, 0], mode='valid', method='fft')
        positions = []
        for _ in starts:
            index = int(correlation.argmax())
            positions.append(index)
            correlation[max(0, index - marker_frames):index + marker_frames] = -np.inf
        return sorted(positions)

    ref_positions, rec_positions = find_markers(reference), find_markers(received)
    ref_stats = json.loads((args.work / 'send-capture.json').read_text())
    rec_stats = json.loads((args.work / 'return-record.json').read_text())

    def completion_origin(stats):
        # Estimate only: delivery completion includes queue/callback scheduling.
        return stats['first_transfer_steady_ns'] / 1e9 - (stats['first_transfer_frames'] - 1) / rate

    measurements = []
    for ref_pos, rec_pos in zip(ref_positions, rec_positions):
        segment = received[rec_pos:rec_pos + marker_frames]
        exact = segment.shape == marker.shape and np.array_equal(segment, marker)
        measurements.append(dict(reference_frame=ref_pos, return_frame=rec_pos, bit_exact=bool(exact),
                                 max_absolute_error=float(np.max(abs(segment - marker))) if segment.shape == marker.shape else None,
                                 estimated_round_trip_ms=1000 * ((rec_pos - ref_pos) / rate + completion_origin(rec_stats) - completion_origin(ref_stats))))
    result = dict(transport='SSH raw interleaved F32 PCM; not S7 wire protocol',
                  marker_frames=marker_frames, markers=measurements,
                  return_frames=len(received), reference_frames=len(reference),
                  latency_evidence='host read-completion anchors, nominal rate; software estimate, not hardware acquisition/presentation time',
                  exit_codes={name: process.returncode for name, process in processes})
    (args.work / 'analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if all(m['bit_exact'] for m in measurements) and all(p.returncode == 0 for _, p in processes) else 1


if __name__ == '__main__':
    raise SystemExit(main())
