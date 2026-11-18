#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in, sequential two-host video capacity/camera/stability spike matrix."""
import argparse
import json
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True, type=Path)
    p.add_argument('--host', required=True)
    p.add_argument('--key-alias', default='win')
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--camera', action='store_true', help='Explicitly activate the webcam in three live cases')
    p.add_argument('--camera-target', default='v4l2_input.pci-0000_04_00.4-usb-0_3_1.0')
    p.add_argument('--virtual-display', action='store_true')
    p.add_argument('--images', type=int, default=60)
    p.add_argument('--stability-images', type=int, default=1800)
    a = p.parse_args()
    a.output = a.output.resolve()
    if a.output.exists():
        p.error('output must be new')
    a.output.mkdir(parents=True, mode=0o700)
    binary = a.binary.resolve()
    codecs = json.loads(subprocess.check_output([str(binary), '--capabilities'], text=True))['codecs']
    report = {'status': 'running', 'local_platform': platform.platform(), 'local_host': platform.node(),
              'remote_host': a.host, 'ordering': 'sequential, one repetition per case',
              'scope': 'V0; SSH-tunneled mTLS, GStreamer PipeWire adapters; no physical display or device continuity qualification',
              'cases': []}
    inventory = 'import json,platform;print(json.dumps(dict(host=platform.node(),platform=platform.platform(),machine=platform.machine())))'
    report['remote_platform'] = json.loads(subprocess.check_output(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
        '-o', 'HostKeyAlias='+a.key_alias, a.host, shlex.join(['python3', '-c', inventory])], text=True))
    cases = [(f'capacity-{mode}-{codec}', 'synthetic', mode, codec, a.images, True, False)
             for mode in ('scalar', 'staged', 'bulk') for codec in codecs]
    if a.camera:
        cases += [(f'camera-staged-{codec}', 'camera', 'staged', codec, 90, False, True) for codec in codecs]
    if a.stability_images:
        cases += [('stability-staged-raw', 'synthetic', 'staged', 'raw', a.stability_images, False, False)]
    began = time.monotonic()
    try:
        for index, (name, source, mode, codec, images, unpaced, viewing) in enumerate(cases):
            cmd = [sys.executable, '-B', str(Path(__file__).with_name('run_video_stream_spike.py')),
                   '--binary', str(binary), '--host', a.host, '--key-alias', a.key_alias, '--output', str(a.output/name),
                   '--source', source, '--mode', mode, '--encoding', codec, '--images', str(images),
                   '--camera-target', a.camera_target]
            if unpaced:
                cmd += ['--unpaced']
            if viewing:
                cmd += ['--pipewire-output']
                if a.virtual_display and codec == 'raw':
                    cmd += ['--virtual-display']
            print(f'{index+1}/{len(cases)} {name}', flush=True)
            subprocess.run(cmd, check=True)
            r = json.loads((a.output/name/'result.json').read_text())
            report['cases'].append(dict(name=name, result=r))
            (a.output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
        hashes = {r['result']['sender']['sha256'] for r in report['cases'] if r['name'].startswith('capacity-')}
        if len(hashes) != 1:
            raise RuntimeError('capacity cases did not use byte-identical synthetic images')
        report['status'] = 'passed'
    except Exception as failure:
        report['status'] = 'failed'
        report['error'] = str(failure)
        raise
    finally:
        report['matrix_wall_seconds'] = time.monotonic()-began
        (a.output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
