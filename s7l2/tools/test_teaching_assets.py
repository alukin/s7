#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise installed teaching links and packaging failure cases in temp dirs."""
from pathlib import Path
import tempfile
import unittest

from teaching_assets import check, copy


class TeachingAssetsTest(unittest.TestCase):
    def test_relocated_copy_and_missing_assets(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='s7-teaching-package-') as temporary:
            root = Path(temporary)
            stage = root / 'stage/share/s7'
            report = copy(source, stage)
            self.assertEqual(report['core_guides'], 24)
            self.assertEqual(report['core_diagrams'], 24)
            self.assertGreater(report['offline_links_checked'], 100)
            self.assertGreater(report['development_links_excluded'], 0)
            for directory in ('examples/education', 'education/labs'):
                for asset in (source / directory).rglob('*'):
                    if asset.suffix in ('.md', '.s7diagram'):
                        self.assertEqual(asset.read_bytes(), (stage / asset.relative_to(source)).read_bytes())
            moved = root / 'relocated/share/s7'
            moved.parent.mkdir(parents=True)
            stage.rename(moved)
            self.assertEqual(check(moved), report)
            guide = moved / 'education/labs/SP14-coherent-am/README.md'
            text = guide.read_text()
            guide.write_text(text + '\n[Missing reading](missing.md)\n')
            with self.assertRaisesRegex(ValueError, 'Broken offline teaching link'):
                check(moved)
            guide.write_text(text)
            guide.unlink()
            with self.assertRaisesRegex(ValueError, '24 named core guides'):
                check(moved)
            guide.write_text(text)
            diagram = moved / 'examples/education/sp14_coherent_am.s7diagram'
            diagram.unlink()
            with self.assertRaisesRegex(ValueError, '24 named core diagrams'):
                check(moved)

    def test_copy_excludes_caches_and_rejects_overlap(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='s7-teaching-cache-') as temporary:
            root = Path(temporary)
            staging_source = root / 'source'
            copy(source, staging_source)
            cache = staging_source / 'examples/education/__pycache__'
            cache.mkdir()
            (cache / 'cached.pyc').write_bytes(b'cache')
            (staging_source / 'education/cached.pyo').write_bytes(b'cache')
            package = root / 'package'
            copy(staging_source, package)
            self.assertFalse((package / 'examples/education/__pycache__').exists())
            self.assertFalse((package / 'education/cached.pyo').exists())
            leaked = package / 'education/leaked.pyc'
            leaked.write_bytes(b'cache')
            with self.assertRaisesRegex(ValueError, 'Python cache included'):
                check(package)
            leaked.unlink()
            with self.assertRaisesRegex(ValueError, 'already exists'):
                copy(staging_source, package)
            with self.assertRaisesRegex(ValueError, 'overlaps'):
                copy(staging_source, staging_source / 'examples/package')


if __name__ == '__main__':
    unittest.main()
