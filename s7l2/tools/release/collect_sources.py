#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Collect exact Fedora source RPMs identified by a staged Linux package.

Usage: collect_sources.py PACKAGE OUTPUT
Publish OUTPUT alongside the binary archive. S7's own source is archived
separately from the tagged repository, including initialized submodules.
"""
import concurrent.futures
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request


def collect(package, output):
    packages = json.loads((package / 'licenses/source-packages.json').read_text())
    output.mkdir(parents=True, exist_ok=True)
    sources = {p['source_rpm']: p['version'] for p in packages.values()}

    def fetch(item):
        filename, version = item
        suffix = '-' + version + '.src.rpm'
        if not filename.endswith(suffix):
            raise ValueError(f'Inconsistent source identity: {filename}, {version}')
        name = filename[:-len(suffix)]
        upstream_version, release = version.rsplit('-', 1)
        url = (f'https://kojipkgs.fedoraproject.org/packages/{name}/'
               f'{upstream_version}/{release}/src/{filename}')
        destination = output / filename
        if not destination.exists():
            temporary = destination.with_suffix('.part')
            with urllib.request.urlopen(url, timeout=120) as response, temporary.open('wb') as stream:
                while chunk := response.read(1024 * 1024):
                    stream.write(chunk)
            temporary.replace(destination)
        with destination.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        print(filename, flush=True)
        return {'file': filename, 'url': url, 'sha256': digest}

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        manifest = list(executor.map(fetch, sorted(sources.items())))
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(f"{p['sha256']}  {p['file']}\n" for p in manifest))


def archive_repository(repository, build, output, version):
    if subprocess.check_output(['git', '-C', str(repository), 'status', '--porcelain']):
        raise RuntimeError('Commit the verified release changes before archiving source')
    revision = subprocess.check_output(
        ['git', '-C', str(repository), 'rev-parse', 'HEAD'], text=True).strip()
    prefix = f's7-{version}-source'
    with tarfile.open(output / f'{prefix}.tar.gz', 'w:gz') as archive:
        for root, subdir in [(repository, ''), (build / '_deps/gpds-src', 'dependencies/gpds/')]:
            files = subprocess.check_output(
                ['git', '-C', str(root), 'ls-files', '--recurse-submodules', '-z'])
            for name in files.decode().split('\0'):
                if name:
                    archive.add(root / name, arcname=prefix + '/' + subdir + name, recursive=False)
    (output / 'S7-REVISION').write_text(revision + '\n')
    (output / 'BUILD-SOURCES.md').write_text(
        f'S7 revision: {revision}\n\n'
        'The source archive includes initialized QSchematic sources and the exact\n'
        'GPDS sources from the release build under dependencies/gpds. Build with:\n\n'
        '    cmake -S s7l2 -B build -DCMAKE_BUILD_TYPE=Release '
        '-DFETCHCONTENT_SOURCE_DIR_GPDS="$PWD/dependencies/gpds"\n'
        '    cmake --build build --parallel 2\n\n'
        'Compiler and dependency requirements: s7l2/README.md. Dependency source\n'
        'RPMs include Fedora build specifications and patches. Publish these\n'
        'sources alongside the preview binary archive.\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--repository', type=Path)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--version', default='0.1.0')
    args = parser.parse_args()
    if args.repository and not args.build:
        parser.error('--repository requires --build')
    collect(args.package.resolve(), args.output.resolve())
    if args.repository:
        archive_repository(args.repository.resolve(), args.build.resolve(),
                           args.output.resolve(), args.version)
