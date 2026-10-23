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
import hashlib


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


def windows_notices(output, notices, sysroot):
    """Identify every deployed PE file from the cached, installed MSYS2 packages."""
    installed = (sysroot / '.s7-packages').read_text().splitlines()
    owners, metadata = {}, {}

    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()

    for filename in installed:
        archive = sysroot / '.cache' / filename
        fields = {}
        info = subprocess.check_output(['tar', '--zstd', '-xOf', str(archive), '.PKGINFO'], text=True)
        for line in info.splitlines():
            if ' = ' in line:
                key, value = line.split(' = ', 1)
                fields.setdefault(key, []).append(value)
        name, base = fields['pkgname'][0], fields['pkgbase'][0]
        metadata[name] = {
            'version': fields['pkgver'][0], 'source_package': base,
            'archive': filename, 'archive_sha256': digest(archive),
            'license': fields.get('license', []), 'upstream': fields.get('url', []),
            'build': fields.get('packager', []),
        }
        members = subprocess.check_output(['tar', '--zstd', '-tf', str(archive)], text=True)
        metadata[name]['notice_members'] = [member for member in members.splitlines()
            if member.startswith('ucrt64/share/') and re.fullmatch(r'(copying|copyright|licen[sc]e|notice)([._-].*)?', Path(member).name, re.IGNORECASE)]
        for member in members.splitlines():
            if member.lower().endswith(('.dll', '.exe')):
                owners[member] = name

    inventory, packages = [], {}
    for binary in sorted(output.rglob('*')):
        if not binary.is_file() or binary.suffix.lower() not in ('.exe', '.dll'):
            continue
        relative = binary.relative_to(output).as_posix()
        if binary.name in ('s7studio.exe', 's7runtime.exe', 's7ctl.exe') or binary.name.startswith('libs7'):
            inventory.append({'binary': relative, 'sha256': digest(binary), 'notices': ['S7/LICENSING.md']})
            continue
        if binary.parent == output:
            member = 'ucrt64/bin/' + binary.name
        elif relative.startswith('qml/'):
            member = 'ucrt64/share/qt6/' + relative
        else:
            member = 'ucrt64/share/qt6/plugins/' + relative
        name = owners.get(member)
        original = sysroot / member.removeprefix('ucrt64/')
        if not name or not original.is_file() or digest(binary) != digest(original):
            raise RuntimeError(f'Cannot identify deployed Windows binary: {relative}')
        if name not in packages:
            package = metadata[name].copy()
            candidates = [name.removeprefix('mingw-w64-ucrt-x86_64-'), package['source_package'].removeprefix('mingw-w64-')]
            paths = []
            for candidate in dict.fromkeys(candidates):
                directory = notices / 'MSYS2' / candidate
                if directory.is_dir():
                    paths.extend(p.relative_to(notices).as_posix() for p in sorted(directory.rglob('*')) if p.is_file())
            if not paths:
                # Some MSYS2 packages (notably ICU) keep the notice beside
                # their data rather than in share/licenses. Use only paths
                # owned by this exact package archive.
                for member in package['notice_members']:
                    original_notice = sysroot / member.removeprefix('ucrt64/')
                    destination = notices / 'MSYS2/packages' / name / member.removeprefix('ucrt64/')
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(original_notice, destination)
                    paths.append(destination.relative_to(notices).as_posix())
            if (not paths and name == 'mingw-w64-ucrt-x86_64-fftw' and package['version'] == '3.3.11-2'
                    and package['archive_sha256'] == '8bcffc286600ae011726e9356f19ea8afba0a622427c77e89b5a09ecce252d95'):
                directory = notices / 'MSYS2/fftw'
                directory.mkdir(exist_ok=True)
                fallback = Path(__file__).parent / 'windows/notices'
                for source, filename in [('fftw-3.3.11.COPYING', 'COPYING'), ('fftw-3.3.11.COPYRIGHT', 'COPYRIGHT'), ('README.md', 'README.md')]:
                    destination = directory / filename
                    shutil.copy2(fallback / source, destination)
                    paths.append(destination.relative_to(notices).as_posix())
                package['notice_provenance'] = 'MSYS2/fftw/README.md'
            if not paths:
                raise RuntimeError(f'No MSYS2 notices for {name} ({relative})')
            package['notices'] = paths
            packages[name] = package
        inventory.append({'binary': relative, 'sha256': digest(binary), 'package': name,
                          'notices': packages[name]['notices']})
    (notices / 'inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    (notices / 'source-packages.json').write_text(json.dumps(packages, indent=2) + '\n')
    (notices / 'SOURCES.md').write_text(
        '# Source package identities\n\n'
        'source-packages.json records the exact MSYS2 package versions, source package\n'
        'names, binary archive hashes and upstream/build provenance of deployed DLLs.\n'
        'The source packaging recipes are maintained at https://github.com/msys2/MINGW-packages.\n'
        'Dependency sources are not included in this binary archive; this inventory is\n'
        'not a written source offer. S7 source and its submodules must match the release tag.\n')


def collect(source, build, output, sysroot=None):
    notices = output / 'licenses'
    notices.mkdir(exist_ok=True)

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
    cache = build / 'CMakeCache.txt'
    if cache.is_file():
        match = re.search(r'^FETCHCONTENT_SOURCE_DIR_GPDS:[^=]+=(.+)$', cache.read_text(), re.MULTILINE)
        if match:
            gpds = Path(match[1])
    copy(gpds / 'license.txt', 'GPDS/license.txt')
    for sub, filename in [('miniyaml', 'LICENSE'), ('tinyxml2', 'LICENSE.txt')]:
        copy(gpds / 'gpds/3rdparty' / sub / filename, f'{sub}/{filename}')
    copy(source / 's7studio/resources/icons/tabler/LICENSE.txt', 'Tabler/LICENSE.txt')

    if sysroot:
        # Retain the license tree, then map each deployed binary to its owner.
        license_tree = sysroot / 'share/licenses'
        if not license_tree.is_dir():
            raise RuntimeError(f'Missing MSYS2 license tree: {license_tree}')
        shutil.copytree(license_tree, notices / 'MSYS2', dirs_exist_ok=True)
        windows_notices(output, notices, sysroot)
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
