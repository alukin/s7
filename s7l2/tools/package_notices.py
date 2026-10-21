#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Collect unchanged notices and a dependency inventory for a staged package.

This records provenance; it does not decide redistribution rights or replace
source/offer obligations. Unsupported ownership must be resolved before release.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import os
import sys


def command(*args):
    result = subprocess.run(args, capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else ''


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
    else:
        backend = 'rpm' if shutil.which('rpm') else 'dpkg' if shutil.which('dpkg-query') else ''
        if not backend:
            raise RuntimeError('Linux notice collection requires rpm or dpkg-query')
        # Match deployed ELF basenames to their original RPM-owned files.
        originals = {}
        for root in (Path('/usr/lib64'), Path('/usr/lib')):
            if root.is_dir():
                for path in root.rglob('*.so*'):
                    originals.setdefault(path.name, path)
        families = {}
        for line in (command('rpm', '-qa', '--qf', '%{NAME}\t%{SOURCERPM}\n').splitlines() if backend == 'rpm' else []):
            name, source_rpm = line.split('\t')
            families.setdefault(source_rpm, []).append(name)
        packages = {}
        unresolved = []
        for binary in sorted(output.rglob('*')):
            if not binary.is_file() or binary.is_symlink():
                continue
            with binary.open('rb') as stream:
                if stream.read(4) != b'\x7fELF':
                    continue
            relative = str(binary.relative_to(output))
            if binary.name.startswith(('s7', 'libs7')):
                inventory.append({'binary': relative, 'package': 'S7', 'notices': 'S7/LICENSING.md'})
                continue
            original = originals.get(binary.name)
            package = ''
            if original:
                if backend == 'rpm':
                    package = command('rpm', '-qf', '--qf', '%{NAME}', str(original))
                else:
                    owner = command('dpkg-query', '-S', str(original)).splitlines()
                    if owner: package = owner[0].split(': ', 1)[0]
            if not package:
                unresolved.append(relative)
                continue
            if package not in packages:
                if backend == 'rpm':
                    source_rpm = command('rpm', '-q', '--qf', '%{SOURCERPM}', package)
                    siblings = families.get(source_rpm, [package])
                    files = command('rpm', '-ql', *siblings).splitlines()
                    license_files = sorted({Path(f) for f in files if Path(f).is_file() and
                        ('/licenses/' in f or Path(f).name.lower().startswith(('copying', 'license', 'copyright', 'notice')))})
                    metadata = command('rpm', '-q', '--qf', '%{NAME} %{VERSION}-%{RELEASE}\n%{LICENSE}', package)
                else:
                    copyright_file = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
                    license_files = [copyright_file] if copyright_file.is_file() else []
                    metadata = command('dpkg-query', '-W', '-f', '${binary:Package} ${Version}', package)
                if not license_files:
                    unresolved.append(f'{package}: no installed license files')
                for path in license_files:
                    copy(path, backend + '/' + str(path.relative_to('/')))
                packages[package] = metadata
            inventory.append({'binary': relative, 'package': package})
        (notices / (backend + '-packages.json')).write_text(json.dumps(packages, indent=2) + '\n')
        (notices / 'missing-notices.json').write_text(json.dumps(unresolved, indent=2) + '\n')
        if unresolved:
            (notices / 'inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
            message = 'Unresolved package notices: ' + ', '.join(unresolved)
            if os.environ.get('S7_ALLOW_INCOMPLETE_NOTICES') != '1':
                raise RuntimeError(message + '; local testing only: S7_ALLOW_INCOMPLETE_NOTICES=1')
            print('LOCAL TEST PACKAGE, NOT FOR DISTRIBUTION: ' + message, file=sys.stderr)
    (notices / 'inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--sysroot', type=Path)
    args = parser.parse_args()
    collect(args.source.resolve(), args.build.resolve(), args.output.resolve(), args.sysroot)
