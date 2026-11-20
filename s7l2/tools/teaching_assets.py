#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy or check the offline teaching trees at a package's share/s7 root."""
import argparse
import json
from pathlib import Path
import re
import shutil
from urllib.parse import unquote, urlsplit


CORE_IDS = {*(f'FN{i:02}' for i in range(1, 7)),
            *(f'CT{i:02}' for i in range(1, 9)),
            *(f'SP{i:02}' for i in (1, 3, 5, 6, 7, 8, 9, 10, 11, 14))}
TREES = ('examples', 'education', 'docs')


def check(root):
    root = Path(root).resolve()
    guides = sorted((root / 'education/labs').glob('*/README.md'))
    ids = [guide.parent.name.split('-', 1)[0] for guide in guides]
    if set(ids) != CORE_IDS or len(ids) != len(CORE_IDS):
        raise ValueError(f'Expected the 24 named core guides; found {ids}')
    diagrams = sorted((root / 'examples/education').glob('*.s7diagram'))
    diagram_ids = [path.name.split('_', 1)[0].upper() for path in diagrams]
    if set(diagram_ids) != CORE_IDS or len(diagram_ids) != len(CORE_IDS):
        raise ValueError(f'Expected the 24 named core diagrams; found {diagram_ids}')
    for tree in TREES:
        if not (root / tree).is_dir():
            raise ValueError(f'Missing teaching tree: {tree}')
        if any(path.name == '__pycache__' or path.suffix in ('.pyc', '.pyo')
               for path in (root / tree).rglob('*')):
            raise ValueError(f'Python cache included in teaching tree: {tree}')
    checked = 0
    development_links = 0
    for page in [root / 'examples/education/README.md', *guides]:
        for href in re.findall(r'\[[^\]]*\]\(([^)]+)\)', page.read_text(encoding='utf-8')):
            url = urlsplit(href)
            if url.scheme == 'qrc' and url.path.startswith('/s7studio/help/'):
                target = root / unquote(url.path.removeprefix('/s7studio/help/'))
            elif url.scheme or url.netloc or not url.path:
                continue
            else:
                target = (page.parent / unquote(url.path)).resolve()
            # References to C++ tests are source-development instructions.
            # They are deliberately not represented as installed SDK assets.
            if target.suffix not in ('.md', '.s7diagram'):
                development_links += 1
                continue
            if not target.is_relative_to(root) or not target.is_file():
                raise ValueError(f'Broken offline teaching link in {page.relative_to(root)}: {href}')
            checked += 1
    return {'core_guides': len(guides), 'core_diagrams': len(diagrams),
            'offline_links_checked': checked, 'development_links_excluded': development_links}


def copy(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    # Never replace a source tree or recurse into one while copying it.
    if source.is_relative_to(destination) or any(
            destination.is_relative_to(source / tree) for tree in TREES):
        raise ValueError('Teaching destination overlaps a source tree')
    for tree in TREES:
        if (destination / tree).exists():
            raise ValueError(f'Teaching destination already exists: {destination / tree}')
    for tree in TREES:
        shutil.copytree(source / tree, destination / tree,
                        ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '*.pyo'))
    return check(destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    copier = commands.add_parser('copy')
    copier.add_argument('source', type=Path)
    copier.add_argument('destination', type=Path)
    checker = commands.add_parser('check')
    checker.add_argument('root', type=Path)
    args = parser.parse_args()
    result = copy(args.source, args.destination) if args.command == 'copy' else check(args.root)
    print(json.dumps(result, indent=2))
