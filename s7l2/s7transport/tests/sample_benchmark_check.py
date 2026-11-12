#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded integrity/availability checks for the opt-in benchmark; no speed gate."""
import argparse
import json
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', required=True)
    p.add_argument('--cert-dir', required=True)
    p.add_argument('--launcher', nargs='*', default=[])
    a = p.parse_args()
    checks = 0

    def run(args, error=None):
        nonlocal checks
        result = subprocess.run(a.launcher + [a.executable] + args, capture_output=True, text=True, timeout=30)
        if error is not None:
            if result.returncode == 0:
                raise RuntimeError('Invalid/corrupt case was accepted: ' + str(args))
            row = json.loads(result.stderr)
            assert row['status'] == 'failed' and error in row['error'].lower(), (args, result.stderr)
        else:
            if result.returncode:
                raise RuntimeError(str(args) + '\n' + result.stderr)
            row = json.loads(result.stdout)
            assert row['status'] == 'verified'
        checks += 1
        return row

    for mode in ['codec', 'protocol', 'memory', 'ws', 'tls']:
        for kind in ['CI12', 'CI16', 'C32']:
            base = ['--mode', mode, '--type', kind, '--packet-bytes', '384', '--packets', '4', '--channels', '2']
            if mode == 'tls':
                base += ['--cert-dir', a.cert_dir]
            endpoint = mode not in ['codec', 'protocol']
            for execution in (['bulk', 'graph'] if endpoint else ['bulk']):
                for direction in (['forward', 'reverse', 'duplex'] if endpoint else ['forward']):
                    args = base + ['--execution', execution, '--direction', direction]
                    row = run(args)
                    directions = 2 if direction == 'duplex' else 1
                    assert row['payload_bytes'] == 384 * 4 * 2 * directions
                    assert row['graph_execution_measured'] == (execution == 'graph')
                    assert row['primed_frames_per_channel'] == (1 if execution == 'graph' else 0)
                    assert not row['paired_delivery_qualified']
                # Prove verification is active, including ordinary scalar graphs.
                run(base + ['--execution', execution, '--corrupt-frame', '1'], error='mismatch')

    for mode in ['memory', 'ws', 'tls']:
        for kind, width in [('CI12', 3), ('CI16', 4), ('C32', 8)]:
            args = ['--mode', mode, '--type', kind, '--packet-bytes', '384', '--packets', '8', '--channels', '2',
                    '--direction', 'duplex', '--rate-frames', '20000', '--queue-sample-every-blocks', '1']
            if mode == 'tls':
                args += ['--cert-dir', a.cert_dir]
            row = run(args)
            assert row['paced'] and len(row['directions']) == 2
            assert row['offered_payload_bytes_per_second'] == 20000 * width * 2 * 2
            # A block must not be published before its LAST frame is available.
            accumulation_ns = (384 // width - 1) * 1000000000 // 20000
            for direction in row['directions']:
                assert direction['queue_samples'] == 8
                assert direction['observed_send_log_packets_max'] <= 2 * 128
                for histogram in direction['first_frame_delivery_latency']:
                    assert histogram['samples'] == 8 and histogram['min_ns'] >= accumulation_ns

    for args, error in [
        (['--execution', 'graph'], 'endpoint modes'),
        (['--mode', 'memory', '--execution', 'graph', '--rate-frames', '1'], 'bulk-only'),
        (['--mode', 'memory', '--execution', 'graph', '--chunk-elements', '1'], 'bulk-only'),
        (['--mode', 'memory', '--direction', 'invalid'], 'direction'),
        (['--mode', 'memory', '--rate-frames', '1', '--packets', '10000000'], 'one hour'),
        (['--corrupt-frame', '10000000'], 'inside'),
    ]:
        run(args, error=error)
    print(f'{checks} benchmark integrity, direction, graph, pacing and negative controls passed')


if __name__ == '__main__':
    main()
