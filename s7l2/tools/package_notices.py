#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Copy S7 and source-vendor notices into a staged package.

Host RPM/dpkg ownership is not a deployment manifest: unrelated Qt plugins can
share filenames. Keep machine-local dependency audits out of package creation.
"""
import argparse
import json
from pathlib import Path
import shutil


def collect(source, build, output, sysroot=None):
    notices = output / 'licenses'
    notices.mkdir(exist_ok=True)
    inventory = []

    def copy(path, name):
        if not path.is_file():
            raise RuntimeError(f'Missing required notice: {path}')
        destination = notices / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)

    # S7's licences: LICENSING.md maps each part to its licence text.
    for name in ('LICENSE', 'LICENSING.md', 'THIRD_PARTY_NOTICES.md'):
        copy(source.parent / name, 'S7/' + name)
    for text in sorted((source.parent / 'LICENSES').glob('*.txt')):
        copy(text, 'S7/LICENSES/' + text.name)
    copy(source / 'external/QSchematic/license.txt', 'QSchematic/license.txt')
    gpds = build / '_deps/gpds-src'
    copy(gpds / 'license.txt', 'GPDS/license.txt')
    for sub, filename in [('miniyaml', 'LICENSE'), ('tinyxml2', 'LICENSE.txt')]:
        copy(gpds / 'gpds/3rdparty' / sub / filename, f'{sub}/{filename}')
    copy(source / 's7studio/resources/icons/tabler/LICENSE.txt', 'Tabler/LICENSE.txt')

    if sysroot:
        # MSYS2 installs package licenses here. Retain this tree unchanged;
        # native-Windows redistribution still needs its separate release audit.
        license_tree = sysroot / 'share/licenses'
        if not license_tree.is_dir():
            raise RuntimeError(f'Missing MSYS2 license tree: {license_tree}')
        shutil.copytree(license_tree, notices / 'MSYS2', dirs_exist_ok=True)
        for binary in sorted(output.rglob('*.dll')):
            inventory.append({'binary': str(binary.relative_to(output)),
                              'notices': 'S7/LICENSING.md' if binary.name.startswith('libs7') else 'MSYS2',
                              'ownership': 'requires Windows package inventory review'})

        (notices / 'inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--sysroot', type=Path)
    args = parser.parse_args()
    collect(args.source.resolve(), args.build.resolve(), args.output.resolve(), args.sysroot)
