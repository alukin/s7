# Third-party notices

S7's own licensing is described in [`LICENSING.md`](LICENSING.md). This file
lists the third-party components S7 contains or links. In a binary package,
`s7l2/tools/package_notices.py` collects the notices into `licenses/`: S7's
licences in `licenses/S7/`, vendored components under their own names, the
installed licence files of every bundled library, and an inventory
(`licenses/inventory.json`) of which notice covers which binary.

## Contained in or linked into S7 programs

| Component | Used by | Licence | Notice in packages |
|---|---|---|---|
| [QSchematic](https://github.com/simulton/QSchematic) | Studio (static) | MIT | `licenses/QSchematic/license.txt` |
| [GPDS](https://github.com/simulton/gpds) | Studio (static, via QSchematic) | MIT | `licenses/GPDS/license.txt` |
| tinyxml2, miniyaml (bundled in GPDS) | Studio (static) | Zlib, MIT | `licenses/tinyxml2/`, `licenses/miniyaml/` |
| [Tabler Icons](https://tabler.io/icons) | Studio (embedded resources) | MIT | `licenses/Tabler/LICENSE.txt` |
| [Qt 6](https://www.qt.io/) | Studio | LGPL-3.0 or GPL-3.0; Qt Graphs and Qt Quick 3D GPL-3.0 | installed licence files of the bundled Qt libraries |
| [Boost](https://www.boost.org/) (JSON, System) | all programs | BSL-1.0 | installed licence files |
| [OpenSSL 3](https://www.openssl.org/) | runtime, transport, Studio, s7ctl | Apache-2.0 | installed licence files |
| [zlib](https://zlib.net/) | s7core | Zlib | installed licence files |
| [FFTW 3](https://www.fftw.org/) | `s7spectral` only (not packaged) | GPL-2.0-or-later | n/a |

On Linux the collector looks up the distribution package that owns each
bundled library (rpm or dpkg) and copies its installed licence files; packaging
fails when a notice cannot be found. On Windows it copies the MSYS2 sysroot's
`share/licenses/` tree.

## Open packaging work

Before binary packages are published (release item P1 in
`s7l2/docs/DEVELOPMENT_PLAN.md`):

- **Linux:** resolve the libraries whose installed notices are missing on the
  build host (`licenses/missing-notices.json`; see `s7l2/tools/linux/NOTES.md`).
  The dpkg backend has not yet been exercised.
- **Windows:** review the per-DLL inventory against the MSYS2 licence tree.
- **Qt and other LGPL libraries:** state in the package where the
  corresponding source can be obtained (the Qt version and the distribution or
  MSYS2 source package it was built from).
