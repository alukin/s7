#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check shared frame fixtures, or all directed cross-language codec pairs."""
import argparse
import json
from pathlib import Path
import subprocess


def request(command, requests):
    result = subprocess.run(command, input=''.join(json.dumps(r) + '\n' for r in requests), text=True, capture_output=True, timeout=60)
    if result.returncode:
        raise RuntimeError(str(command) + '\n' + result.stderr)
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if len(rows) != len(requests):
        raise RuntimeError(f'{command}: expected {len(requests)} responses, got {len(rows)}\n{result.stderr}')
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixtures', default=str(Path(__file__).resolve().parents[1] / 's7transport/tests/fixtures/frames_v1.json'))
    p.add_argument('--require-codecs', action='store_true')
    p.add_argument('--cpp')
    p.add_argument('--python')
    p.add_argument('--java-classpath')
    p.add_argument('--java', default='java')
    p.add_argument('--bridge', nargs=argparse.REMAINDER)
    a = p.parse_args()
    if a.bridge is None and not all((a.cpp, a.python, a.java_classpath)):
        p.error('provide --bridge or all of --cpp, --python and --java-classpath')
    if a.bridge == []:
        p.error('--bridge requires a command')
    data = json.loads(Path(a.fixtures).read_text())
    root = Path(__file__).resolve().parents[2]
    bridges = {'bridge': a.bridge} if a.bridge else {
        'C++': [a.cpp], 'Python': [a.python, str(root / 's7py/tools/frame_interop.py')],
        'Java': [a.java, '-cp', a.java_classpath, 's7.frames.FrameInterop']}
    total = skipped = 0
    for language, command in bridges.items():
        cases = data['valid'] + data['invalid']
        rows = request(command, [{'op': 'decode', 'case': c} for c in cases])
        for index, (c, r) in enumerate(zip(cases, rows)):
            expected = 'ok' if index < len(data['valid']) else 'rejected'
            if r['status'] == 'unavailable' and not a.require_codecs:
                skipped += 1
            elif r['status'] != expected:
                raise RuntimeError(f'{language} {c["name"]}: expected {expected}, got {r}')
            else:
                total += 1
    if not a.bridge:
        for producer, command in bridges.items():
            rows = request(command, [{'op': 'encode', 'case': c} for c in data['valid']])
            encoded = []
            for c, r in zip(data['valid'], rows):
                if r['status'] == 'unavailable' and not a.require_codecs:
                    skipped += 1
                    continue
                if r['status'] != 'ok':
                    raise RuntimeError(f'{producer} encode {c["name"]}: {r}')
                encoded.append((c, r))
                if (c['encoding'] == 0 or c['info']['fault_only']) and c['name'] != 'unknown_optional_header':
                    if r['message_hex'] != c['message_hex']:
                        raise RuntimeError(f'{producer} {c["name"]}: canonical raw/fault encoding differs')
                    total += 1
            for consumer, target in bridges.items():
                inputs = []
                for c, r in encoded:
                    inputs.append({'op': 'decode', 'case': dict(c, message_hex=r['message_hex'])})
                outputs = request(target, inputs)
                verified = 0
                for (c, _), r in zip(encoded, outputs):
                    if r['status'] == 'unavailable' and not a.require_codecs:
                        skipped += 1
                        continue
                    if r['status'] != 'ok':
                        raise RuntimeError(f'{producer} -> {consumer} {c["name"]}: {r}')
                    total += 1
                    verified += 1
                print(f'{producer} -> {consumer}: {verified} verified frames')
    print(f'{total} frame fixture/interoperability checks passed; {skipped} unavailable-codec cases skipped')


if __name__ == '__main__':
    main()
