#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Copy S7, vendor and deployed-library notices into a staged package.

Match deployed build identities before querying RPM ownership: unrelated Qt
plugins can share filenames. Fail closed on missing library notices.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import re


def build_id(path):
    result = subprocess.run(['readelf', '-n', str(path)], capture_output=True, text=True)
    match = re.search(r'Build ID: (\w+)', result.stdout)
    return match[1] if match else None


def linux_notices(output, notices):
    """Inventory the actual deployed ELF files, matching full Qt paths and build IDs.

    Stripping/rpath changes preserve build IDs. A basename alone is insufficient:
    desktop packages can install unrelated Qt plugins with identical names.
    This release supports Fedora RPM builds; other hosts must add a backend.
    """
    if not shutil.which('rpm'):
        raise RuntimeError('Linux release notice collection requires an RPM build host')
    inventory, packages = [], {}
    installed = subprocess.check_output(
        ['rpm', '-qa', '--qf', '%{NAME}\t%{SOURCERPM}\n'], text=True)
    for binary in sorted((output / 'usr').rglob('*')):
        if not binary.is_file() or binary.is_symlink():
            continue
        with binary.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        relative = binary.relative_to(output).as_posix()
        if relative.startswith('usr/bin/s7') or binary.name.startswith('libs7'):
            inventory.append({'binary': relative, 'notices': 'S7/LICENSING.md'})
            continue
        candidates = []
        for prefix, host in [('usr/qml/', '/usr/lib64/qt6/qml/'),
                             ('usr/plugins/', '/usr/lib64/qt6/plugins/'),
                             ('usr/lib/', '/usr/lib64/')]:
            if relative.startswith(prefix):
                candidates.append(Path(host + relative[len(prefix):]))
        identity = build_id(binary)
        original = next((p for p in candidates if p.is_file() and identity
                         and build_id(p) == identity), None)
        if original is None and identity:
            original = next((p for p in Path('/usr/lib64').rglob(binary.name)
                             if p.is_file() and build_id(p) == identity), None)
        if original is None:
            raise RuntimeError(f'Cannot identify deployed binary by build ID: {relative}')
        metadata = subprocess.check_output(
            ['rpm', '-qf', '--qf', '%{NAME}\t%{VERSION}-%{RELEASE}\t%{SOURCERPM}\n',
             str(original)], text=True).strip().split('\t')
        name, version, source_rpm = metadata
        if name not in packages:
            # Fedora often puts a subpackage's notices in its common/base RPM.
            # Only accept siblings built from exactly the same source RPM.
            siblings = [line.split('\t')[0] for line in installed.splitlines()
                        if line.split('\t')[-1] == source_rpm]
            files = subprocess.check_output(['rpm', '-ql', *siblings], text=True).splitlines()
            license_files = [Path(p) for p in files
                             if p.startswith('/usr/share/licenses/') and Path(p).is_file()]
            license_files += [Path(p) for p in files if p.startswith('/usr/share/doc/')
                              and Path(p).name.lower().startswith(('copying', 'license', 'copyright'))
                              and Path(p).is_file()]
            if not license_files and source_rpm == 'xcb-util-keysyms-0.4.1-9.fc44.src.rpm':
                fallback = Path(__file__).parent / 'linux/notices/xcb-util-keysyms-0.4.1.COPYING'
                destination = notices / 'system/xcb-util-keysyms/COPYING'
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(fallback, destination)
                packages[name] = {'version': version, 'source_rpm': source_rpm,
                                  'notices': ['xcb-util-keysyms/COPYING']}
                inventory.append({'binary': relative, 'build_id': identity, 'package': name,
                                  'notices': ['system/xcb-util-keysyms/COPYING']})
                continue
            if not license_files:
                raise RuntimeError(f'No installed license files for {name} ({relative})')
            for path in license_files:
                destination = notices / 'system' / path.relative_to('/usr/share')
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)
            packages[name] = {'version': version, 'source_rpm': source_rpm,
                              'notices': [str(p.relative_to('/usr/share'))
                                          for p in license_files]}
        inventory.append({'binary': relative, 'build_id': identity, 'package': name,
                          'notices': ['system/' + p for p in packages[name]['notices']]})
    (notices / 'inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    (notices / 'source-packages.json').write_text(json.dumps(packages, indent=2) + '\n')
    (notices / 'SOURCES.md').write_text(
        '# Corresponding sources\n\n'
        'This Fedora-built package records exact source RPM names in source-packages.json.\n'
        'Retrieve those source RPMs from https://koji.fedoraproject.org/koji/ using the\n'
        'recorded name/version/release; they contain upstream sources and Fedora patches.\n'
        'Redistributors must provide corresponding sources alongside the binaries;\n'
        'this inventory is not a written source offer. S7 source, its submodules and\n'
        'build scripts must match the release revision.\n')


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
    else:
        linux_notices(output, notices)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--sysroot', type=Path)
    args = parser.parse_args()
    collect(args.source.resolve(), args.build.resolve(), args.output.resolve(), args.sysroot)
