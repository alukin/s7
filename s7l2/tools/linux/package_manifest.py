#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record native binary package hashes and build provenance beside the artifacts."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

source, build, output = (Path(arg).resolve() for arg in sys.argv[1:])
packages = sorted([*output.glob('*.rpm'), *output.glob('*.deb')])
if not packages:
    raise SystemExit('No native packages to inventory')
cache = {}
for line in (build / 'CMakeCache.txt').read_text().splitlines():
    if line and not line.startswith(('#', '//')) and '=' in line and ':' in line.split('=', 1)[0]:
        key, value = line.split('=', 1)
        cache[key.split(':', 1)[0]] = value
revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
status = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'], text=True)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


hashes = {path.name: digest(path) for path in packages}
source_paths = subprocess.check_output([
    'git', '-C', str(source), 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', '.'
]).decode().split('\0')
source_files = {name: digest(source / name) for name in sorted(set(source_paths))
                if name and (source / name).is_file()}
# A submodule appears as a directory in the parent's index; inventory its files too.
vendor = source / 'external/QSchematic'
if vendor.is_dir():
    for name in subprocess.check_output(['git', '-C', str(vendor), 'ls-files', '-z']).decode().split('\0'):
        if name and (vendor / name).is_file():
            source_files['external/QSchematic/' + name] = digest(vendor / name)
(output / 'source-files.sha256').write_text(''.join(f'{value}  {name}\n' for name, value in source_files.items()))
(output / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in hashes.items()))
dependency_files = {Path(cache['S7_ZSTD_STATIC_LIBRARY']).resolve()}
for relative in ('s7runtime/s7runtime', 's7ctl/s7ctl', 's7hw', 's7studio/s7studio',
                 's7audio/libs7audio.so', 's7sdr/libs7sdr.so'):
    binary = build / relative
    if binary.exists():
        linked = subprocess.check_output(['ldd', str(binary)], text=True)
        dependency_files.update(Path(path).resolve() for path in re.findall(r'=> (/\S+)', linked))
dependency_packages = set()
for path in sorted(dependency_files):
    if not str(path).startswith(('/usr/', '/lib')):
        continue
    if shutil.which('rpm'):
        identity = subprocess.check_output(['rpm', '-qf', '--qf',
            '%{NAME}\t%{VERSION}-%{RELEASE}\t%{SOURCERPM}\n', str(path)], text=True)
    else:
        owner = subprocess.check_output(['dpkg-query', '-S', str(path)], text=True).split(': ', 1)[0]
        identity = subprocess.check_output(['dpkg-query', '-W',
            '-f=${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n', owner], text=True)
    dependency_packages.update(identity.strip().splitlines())
(output / 'build-info.json').write_text(json.dumps({
    'source_revision': revision, 'tracked_changes': status.splitlines(),
    'distribution': Path('/etc/os-release').read_text(),
    'cmake_cache': {key: value for key, value in cache.items() if key.startswith(('S7_', 'CMAKE_CXX_', 'CMAKE_INSTALL_'))},
    'packages': hashes,
    'source_inventory_sha256': digest(output / 'source-files.sha256'),
    'static_dependencies': {'gpds': '1.10.0 (QSchematic FetchContent)',
        'zstandard_archive': cache.get('S7_ZSTD_STATIC_LIBRARY')},
    'dependency_source_packages': sorted(dependency_packages),
}, indent=2) + '\n')
