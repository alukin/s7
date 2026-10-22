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
| [FFTW 3](https://www.fftw.org/) | Studio spectrum (double precision); separate `s7spectral` plugin (single precision) | GPL-2.0-or-later | installed licence files and exact source-package inventory for the bundled Studio library |

On Fedora Linux the collector matches deployed ELF build IDs and full Qt
paths, then looks up the RPM that owns each bundled library and copies its
installed licence files. Packaging fails when a notice cannot be found.
The Linux release backend is RPM-only; dpkg hosts are not qualified.
On Windows it copies the MSYS2 sysroot's
`share/licenses/` tree.

## Linux preview distribution

The package includes `licenses/inventory.json` and `licenses/source-packages.json`:
these identify the deployed binaries, notices and exact Fedora source RPMs.
Notices in common/base subpackages are accepted only from the same source RPM.
The xcb-util-keysyms 0.4.1 notice, omitted by Fedora's binary RPM, is copied
verbatim from its source RPM; provenance is in `s7l2/tools/linux/notices/README.md`.

Qt/dependency source archives are not included in the binary bundle.
Corresponding-source distribution is separate from binary packaging; the source
inventory identifies exact versions and is not a written source offer. Windows redistribution remains deferred pending
review of the per-DLL inventory against the MSYS2 licence tree.
