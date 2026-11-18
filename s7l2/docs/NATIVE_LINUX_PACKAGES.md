# Native Fedora and Ubuntu packages

S7 can build distribution-native RPMs and DEBs with CPack. These install under
`/usr` and use the distribution's Qt, audio, SDR and other shared libraries.
Build separately on each target distribution; a Fedora binary is not an Ubuntu
binary. The initial qualified targets are Fedora 45 x86-64 and Ubuntu 26.04 LTS
amd64. Ubuntu 26.10 is selectable by the container helper but is not covered by
the initial package verification below.

These are upstream binary packages for local installation. They are unsigned
and are not submissions to the Fedora or Ubuntu archives. Publishing a signed
repository, source RPM/Debian source packages and release automation are separate
work. The private AD936x provider is excluded.

## Packages

| Package | Contents |
| --- | --- |
| `s7-libs` | Versioned core shared library, schema-element library, licensing map and licence texts |
| `s7-runtime` | `s7runtime`, `s7ctl`, `s7analysis_lab`, noise plugin, examples and selected operator documentation |
| `s7-hardware` | `s7hw`, shared hardware/audio/SDR libraries, audio and SDR element plugins; PortAudio, native PipeWire and SoapySDR providers |
| `s7-analysis` | Spectral and wavelet plugins, built out of tree against the installed SDK |
| `s7-studio` | Studio executable, desktop entry, icon and notices for statically incorporated UI dependencies |
| `s7-devel` (Fedora), `s7-dev` (Ubuntu) | Public core/hardware/audio/SDR headers, analysis headers, CMake exports and development library symlinks; compiler/build dependencies |

The runtime and hardware packages do not require Qt. Studio depends on the
runtime; hardware is a recommendation rather than a mandatory Studio dependency.
Internal package dependencies lock the matching S7 version and packaging revision.
The SDK depends on the matching core and hardware libraries.

Normal installation neither starts an S7 service nor opens an audio endpoint or
radio. The explicit `s7hw --run` operation remains the initial hardware execution
entry point; service/Studio activation is not added by packaging. Soapy device
modules are separate distribution/vendor packages: `s7-hardware` provides the
Soapy adapter, not every radio driver. Installing the SDK does not include the
private AD936x provider.

## Build on the target distribution

From `s7l2/`, install build dependencies and run the native helper:

```sh
# Fedora 45
tools/install_deps_fedora.sh --native-packages --no-gui-tests
# Ubuntu 26.04 LTS (or use this installer on 26.10)
tools/install_deps_ubuntu.sh --native-packages --no-gui-tests

tools/linux/build_native_packages.sh
```

Only run the dependency installer for the current distribution. The helper uses
a Release build, `/usr`, required PortAudio/PipeWire/SoapySDR providers and both
frame codecs. Missing native dependencies are errors. It builds the analysis
plugins separately against a staged SDK, then packages them through the main
CPack configuration without adding those plugins to the main build.

`S7_BUILD_JOBS=2` is the default memory limit. Use `--no-studio` for a complete
headless package build and `--test` to build/run the full configured CTest suite.
The native GUI regression suite is disabled by this helper; the package behavior
checks below are separate. Override directories with `--build-dir DIR` and
`--output-dir DIR`. Output defaults to `dist/native/fedora45/`,
`dist/native/ubuntu26.04/` or the corresponding distribution/version.

The `--native-packages` installer option adds `rpm-build`, `file` and Ninja on
Fedora, or `dpkg-dev`, `file` and Ninja on Ubuntu, independently of `--no-extras`.
It does not automatically build or install S7 packages.

## Container builds

With the source and QSchematic submodule present, the helper can install build
dependencies inside a container instead of on the host:

```sh
tools/linux/build_native_container.sh fedora:45
tools/linux/build_native_container.sh ubuntu:26.04
# Podman alternative:
S7_CONTAINER_ENGINE=podman tools/linux/build_native_container.sh fedora:45
```

The default engine is Docker. The container sees the checkout read-only and
writes only its dedicated build and output directories. Network access is needed
for distribution packages and the pinned GPDS FetchContent dependency. The
helper accepts `--no-studio` and `--test`. The container wrapper itself received
shell syntax/argument review; the qualification below used an equivalent
manually prepared Ubuntu build container and a native Fedora build.

## Install and check

Use the package manager so it resolves system dependencies. In the directory
containing the packages:

```sh
# Fedora: all packages
sudo dnf install ./*.rpm
# Headless runtime only
sudo dnf install ./s7-libs-*.rpm ./s7-runtime-*.rpm

# Ubuntu: all packages
sudo apt install ./*.deb
# Headless runtime only
sudo apt install ./s7-libs_*.deb ./s7-runtime_*.deb
```

Run the commands for the target distribution, not both sets. For a headless
Ubuntu install that should omit all optional recommendations, add
`--no-install-recommends`. The SDK package brings development tools; it is not
required to run schemas. Test certificates/private keys from the source's
runtime test directory are excluded from native packages; provision credentials
explicitly before using network examples.

The installed check script runs programs from a temporary working directory,
clears custom plugin/library paths, uses a temporary home and state directories,
and does not discover/open hardware:

```sh
python3 tools/linux/smoke_native_package.py /tmp/s7-native-headless-check
# With hardware, analysis, Studio and SDK packages installed:
python3 tools/linux/smoke_native_package.py /tmp/s7-native-full-check \
  --hardware --analysis --studio --sdk-source s7hardware/tests/sdk
```

For a clean-install check, mount only this script, the small SDK fixture and
the package artifacts into a fresh target-distribution container. Do not mount
the source checkout or build tree. Install the packages through dnf/apt first.
The full check compiles an external provider/consumer against `/usr`, exercises
provider-module lifetime and cancellation, and confirms all three public
backends were compiled in without opening them.

Studio checks default to Qt's offscreen platform and null rendering backend.
They test installed QML imports, local runs, save/reopen and observation behavior;
they do not qualify desktop pixels, GPU rendering or physical devices.

## Layout, notices and provenance

Fedora uses `/usr/lib64`; Ubuntu uses the CMake-selected multiarch directory,
initially `/usr/lib/x86_64-linux-gnu`. Element plugins live under that directory's
`s7/plugins`. Runtime, CLI and Studio now search the configured installed library
directory, retaining the existing AppDir and Windows paths. Installed native
targets have no build-tree RPATH. Headers/CMake exports are separate from
versioned shared libraries.

System shared-library dependencies are calculated by RPM's dependency scanner
or `dpkg-shlibdeps`. QML imports, Qt platform/image plugins, fonts and SDK build
dependencies are declared explicitly. Vendor notices for QSchematic, GPDS,
tinyxml2, miniyaml and Tabler accompany Studio. Zstandard is linked statically,
so its installed distribution copyright/licence notice accompanies S7 as well.
Shared Qt/hardware libraries remain owned by their distribution packages.

The output includes `SHA256SUMS`, `source-files.sha256` and `build-info.json`.
These record artifact hashes, source revision/working-tree inventory, compiler
and installation settings, distribution identity and linked dependency source
package identities, including the static Zstandard archive's owner. Source
inventories include the QSchematic submodule; GPDS's pinned version is recorded
separately. A working-tree build is not represented as an unchanged release tag.
This inventory does not replace corresponding source distribution when packages
are published. No packages are uploaded by these helpers.

## Initial verification, 2026-11-17

| Check | Fedora 45 x86-64 | Ubuntu 26.04 amd64 |
| --- | --- | --- |
| Release build, Studio, three public hardware providers, LZ4/static Zstandard | Passed | Passed |
| Core, hardware adapters, configured plugin discovery and installed-SDK/plugin regressions | 17/17 passed | 17/17 passed |
| Native package set generated | Six RPMs | Six DEBs |
| Fresh dnf/apt installation of runtime/core without Qt | Passed; 5/5 installed checks | Passed; 5/5 installed checks |
| Full installed suite including external SDK build | 23/23 passed | 23/23 passed |
| Package removal with separately stored runtime state preserved | Passed | Passed (including purge) |

Both SDK packages explicitly depend on the compiler, CMake and `make`; the
external consumer succeeds without installing extra build tools by hand.

The user manually checked desktop rendering on 2026-11-16 and reported that it
looked fine. This provides manual rendering evidence alongside the automated
package behavior checks above.

No native Ubuntu 26.10 package run, version-upgrade check or physical device run
is claimed by this package verification pass.
