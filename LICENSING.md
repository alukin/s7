# Licensing

S7 is free software. Different parts of the repository use different licences,
chosen for how each part is meant to be used. Every source file states its
licence in an SPDX header; [`REUSE.toml`](REUSE.toml) assigns licences to files
that cannot carry one (diagrams, JSON, certificates, icons, documentation). The
full licence texts are in [`LICENSES/`](LICENSES/); [`LICENSE`](LICENSE) is a
copy of the GNU GPL version 3.

| Part | Licence | Why |
|---|---|---|
| Runtime and plugin SDK: `s7l2/s7core`, `s7io`, `s7observe`, `s7net`, `s7transport`, `s7runtime`, `s7noise`, and the build files `s7l2/CMakeLists.txt`, `s7l2/cmake/` | [LGPL-3.0-or-later](LICENSES/LGPL-3.0-or-later.txt) | Element plugins under any licence can link `s7core`, and applications can embed the runtime. Changes to these libraries themselves stay free. |
| S7 Studio (`s7l2/s7studio`) and `s7ctl` (`s7l2/s7ctl`) | [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt) with the [S7 plugin permission](LICENSES/LicenseRef-S7-plugin-permission.txt) | Studio is a complete application; the permission allows it to load element plugins under any licence. |
| Everything else that is code: tests, tools and scripts, CI, the Python node `s7py` | [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt) | |
| Documentation: `doc/`, `s7l2/docs/`, READMEs and other Markdown | [CC-BY-4.0](LICENSES/CC-BY-4.0.txt) | Anyone may reuse and adapt it, with attribution. The protocol specifications may be implemented by anyone. |
| Example models and their notes and scripts (`s7l2/examples`, `s7l2/s7runtime/examples`, `s7py/examples`) and the protocol conformance fixtures (`s7l2/s7runtime/tests/fixtures`, `s7l2/s7transport/tests/fixtures`) | [CC0-1.0](LICENSES/CC0-1.0.txt) | Free to copy into your own models or another S7 implementation, without conditions. |
| `s7l2/s7spectral`, the reference third-party plugin | [MIT-0](LICENSES/MIT-0.txt) | A template for new plugins, usable without conditions. A distributed binary of it also links FFTW, which is GPL-2.0-or-later. |

## Your own work

Diagrams, schemas, element libraries and results that you create with S7 are
yours: they are not covered by S7's licences merely because S7 Studio or the
runtime produced or ran them. Material you copy from S7 into them, such as an
example model, carries its own licence (for examples, CC0-1.0, which imposes no
conditions).

## Element plugins

An element plugin links the `s7core` shared library through the plugin SDK
([`s7l2/docs/PLUGIN_INTERFACE.md`](s7l2/docs/PLUGIN_INTERFACE.md)). Because
`s7core` and `s7runtime` are LGPL and Studio and `s7ctl` have the plugin
permission, a separately distributed plugin may use any licence. If you
distribute `s7core` itself, including a modified copy, with your plugin, the
LGPL applies to `s7core`.

## Third-party components

Licences of third-party code stay with that code:

| Component | Where | Licence |
|---|---|---|
| QSchematic | `s7l2/external/QSchematic` (git submodule), linked into Studio | MIT, `s7l2/external/QSchematic/license.txt` |
| GPDS | fetched by QSchematic's build, linked into Studio | MIT |
| Tabler Icons | `s7l2/s7studio/resources/icons/tabler` | MIT, `LICENSE.txt` in that directory |

S7 also uses Qt 6 (LGPL-3.0 or GPL-3.0; Qt Graphs and Qt Quick 3D are GPL-3.0
only, which Studio's licence satisfies), Boost (BSL-1.0), OpenSSL 3 (Apache-2.0),
zlib (Zlib), tinyxml2 (Zlib) and, for `s7spectral` only, FFTW (GPL-2.0-or-later).
Release packages carry the notices of the libraries they bundle; see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Contributions

Contributions are accepted under the licence of the files they change. New
files carry the SPDX header of their part of the repository, for example:

```cpp
// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
```

`reuse lint` ([REUSE](https://reuse.software/)) checks that every file has
licence information.

## History

Until 2026-10-21 S7 was distributed under the GNU GPL version 2. Its copyright
holders relicensed it as described above; earlier releases keep their original
licence.
