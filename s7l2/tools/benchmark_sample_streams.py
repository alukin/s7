#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run verified sample capacity cases sequentially; preserve commands and host metadata.

Each channel is an independent stream. Graph mode uses the ordinary scalar
runtime. Output is exclusive JSONL, flushed after every case.
"""
import argparse
import json
import platform
import subprocess
import time
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--modes', nargs='+', choices=['codec', 'protocol', 'memory', 'ws', 'tls'],
                   default=['codec', 'protocol', 'memory', 'ws'])
    p.add_argument('--types', nargs='+', choices=['CI12', 'CI16', 'C32'], default=['CI12', 'CI16', 'C32'])
    p.add_argument('--payloads', type=int, nargs='+', default=[16384, 65536, 262144])
    p.add_argument('--channels', type=int, default=2)
    p.add_argument('--packets', type=int, default=512)
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--timeout', type=float, default=60)
    p.add_argument('--cert-dir', type=Path)
    p.add_argument('--execution', choices=['bulk', 'graph'], default='bulk')
    p.add_argument('--directions', nargs='+', choices=['forward', 'reverse', 'duplex'], default=['forward'])
    p.add_argument('--rate-frames', type=int, default=0)
    p.add_argument('--queue-sample-every-blocks', type=int, default=0)
    a = p.parse_args()
    if (not 1 <= a.channels <= 16 or not 1 <= a.packets <= 10000000 or not 1 <= a.repeats <= 100
            or not 0 < a.timeout <= 3600 or not 0 <= a.rate_frames <= 1000000000
            or not 0 <= a.queue_sample_every_blocks <= 10000000
            or any(not 8 <= n <= 262144 for n in a.payloads)):
        p.error('Invalid benchmark bounds')
    if 'tls' in a.modes and a.cert_dir is None:
        p.error('TLS mode requires --cert-dir')
    if (a.execution == 'graph' or a.rate_frames or a.queue_sample_every_blocks or a.directions != ['forward']) and any(m in a.modes for m in ['codec', 'protocol']):
        p.error('Graph, pacing and direction selection require endpoint modes')
    if a.execution == 'graph' and (a.rate_frames or a.queue_sample_every_blocks):
        p.error('Graph path is unpaced; pacing and queue sampling are bulk-only options')
    executable = str(a.executable.resolve(strict=True))
    with a.output.open('x') as out:
        out.write(json.dumps(dict(kind='host',platform=platform.platform(),machine=platform.machine(),
                                  processor=platform.processor(),utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())))+'\n')
        out.flush()
        for mode in a.modes:
            for kind in a.types:
                width = {'CI12': 3, 'CI16': 4, 'C32': 8}[kind]
                for requested in a.payloads:
                    # Round down to whole complex elements, particularly CI12.
                    payload = requested - requested % width
                    for direction in a.directions:
                        for repeat in range(a.repeats):
                            command = [executable, '--mode', mode, '--type', kind, '--packet-bytes', str(payload),
                                       '--packets', str(a.packets), '--channels', str(a.channels),
                                       '--execution', a.execution, '--direction', direction]
                            if a.rate_frames:
                                command += ['--rate-frames', str(a.rate_frames)]
                            if a.queue_sample_every_blocks:
                                command += ['--queue-sample-every-blocks', str(a.queue_sample_every_blocks)]
                            if mode == 'tls':
                                command += ['--cert-dir', str(a.cert_dir.resolve())]
                            try:
                                run = subprocess.run(command, capture_output=True, text=True, timeout=a.timeout)
                                result = json.loads(run.stdout) if run.returncode == 0 else {'status': 'failed', 'stderr': run.stderr}
                            except (subprocess.TimeoutExpired, ValueError) as error:
                                result = {'status': 'failed', 'error': str(error)}
                            out.write(json.dumps(dict(kind='case',command=command,repeat=repeat,
                                                      requested_payload_bytes=requested,result=result))+'\n')
                            out.flush()
                            if result.get('status') != 'verified':
                                raise RuntimeError(f'Benchmark case failed; see {a.output}')
    print(a.output)


if __name__ == '__main__':
    main()
