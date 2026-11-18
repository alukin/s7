# Windows hardware build research

Research date: 2026-11-17. Public source reviewed: `c88b1fd`.
This is a feasibility assessment and implementation proposal, not a record of
a successful Windows hardware build or device qualification.

Release decision, 2026-11-17: retain the portable Windows folder/ZIP with
PortAudio sound, excluding SDR libraries, elements and providers. Linux retains
SDR. The implementation and current verification are tracked in
[Windows builds](WINDOWS_BUILD.md); the broader possibilities below remain
research rather than the selected Windows release scope.

## Finding

The new hardware elements do not require a different Windows architecture.
Retain the existing Linux-to-Windows x86-64 cross build: Clang 22, MinGW-w64
UCRT, libstdc++, and dependencies from MSYS2 UCRT64. Build the portable hardware
contract and elements on Windows; select providers by platform and installed
dependencies. The main identified work is dependency acquisition, DLL packaging,
cross-platform SDK tests and native device qualification. Additional compiler or
backend defects may emerge when those builds run.

| Layer/provider | Proposed Windows treatment | Evidence and remaining work |
| --- | --- | --- |
| `s7hardware`, scalar boundaries, device-paced owner | Build | Public code largely uses standard C++; library loading already uses `s7::platform`. Linux SIGPIPE handling is guarded and becomes a no-op elsewhere. Windows build/execution still needs verification. |
| `s7audio` + PortAudio | Enable | MSYS2 supplies UCRT64 PortAudio. Start qualification with an explicitly selected WASAPI endpoint. |
| `s7audio` + PipeWire | Disable | Already restricted to Linux in CMake. Explicit `S7_PIPEWIRE=ON` fails on Windows. |
| `s7sdr` + SoapySDR | Enable | MSYS2 supplies UCRT64 SoapySDR. Real radios additionally require matching device modules, their dependencies and OS drivers. |
| Private AD936x provider | Separate optional build/package | Keep outside public builds. Audit/build its core dependencies for Windows and qualify each advertised device profile. |
| Hardware execution | Use `s7hw` initially | Runtime service and Studio do not yet activate hardware runs; compiling their element plugins does not add that integration. |

MSYS2's [PortAudio recipe](https://github.com/msys2/MINGW-packages/blob/master/mingw-w64-portaudio/PKGBUILD)
builds UCRT64 packages, including a separate ASIO variant. Its
[SoapySDR package](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-soapysdr)
currently supplies 0.8.1-4, headers, an import library, `SoapySDR.pc`,
`libSoapySDR.dll` and `SoapySDRUtil.exe`. These establish dependency availability,
not successful compilation of the current S7 code.

## Gaps in the current repository

1. **Windows dependencies do not include hardware backends.**
   `tools/windows/fetch_msys2_sysroot.py` requests compiler support, core,
   Studio and spectral dependencies, but neither `portaudio` nor `soapysdr`.
   Because backend selection defaults to `AUTO`, a clean sysroot can produce
   a build with no actual Windows hardware providers while retaining elements
   and offline checks. The toolchain already isolates target pkg-config paths;
   preserve that isolation rather than discovering Linux headers/libraries.
2. **The wrapper has no explicit hardware profile.**
   `build_for_windows` does not set the backend switches. It fetches only when
   the compiler headers are absent, so adding packages to the fetch script alone
   will not upgrade existing sysroots through this wrapper. It needs a way to
   ensure requested dependencies are present as well as explicit build flags.
3. **The bundle omits the new executable and DLLs.**
   `tools/windows/package.sh` explicitly selects executables, `libs7core.dll`,
   noise and analysis plugins. It does not select `s7hw.exe`, the three hardware
   libraries or the two element plugins. Its import walker copies dependencies
   only from the sysroot, so it cannot repair missing project DLLs from their
   build directories. Explicitly stage those project outputs before dependency
   closure is calculated.
4. **Wine cannot find all new project dependencies by default.**
   The generated `S7_WINE_DLL_PATH` contains only the core and noise build
   directories and the sysroot's `bin`. Add the hardware, audio and SDR DLL
   directories before expecting their tests or `s7hw` to load reliably.
5. **The external hardware SDK test excludes Windows.**
   `s7hardware_sdk_smoke` is registered only for native non-Apple Unix builds.
   Its script does not forward a cross toolchain or emulator. Extend it to test
   a Windows SDK consumer and external fake provider, including module loading
   and lifetime, rather than counting existing Linux SDK coverage as Windows
   evidence.
6. **Packaging needs explicit plugin discovery and notice coverage.**
   The PE import walker cannot discover dynamically loaded Soapy device modules
   or an optional private provider. Stage selected modules explicitly and walk
   their DLL dependencies too. `tools/package_notices.py` also needs to recognize
   `s7hw.exe` as a project binary; its current project-executable list omits it.
   Inventory additional public dependencies and keep private artifacts separate.
7. **The supplied audio examples select PipeWire.**
   `examples/hardware/audio_capture.json` and `audio_playback.json` explicitly
   request `backend=pipewire`. Windows examples need PortAudio and deliberate
   device selection. An offline `--check` validates graph/profile structure
   without resolving the backend; success does not prove hardware availability.

Relevant code: [backend detection](../s7hardware/cmake/OptionalBackend.cmake),
[audio build](../s7audio/CMakeLists.txt), [SDR build](../s7sdr/CMakeLists.txt),
[top-level targets and Wine setup](../CMakeLists.txt),
[Windows fetcher](../tools/windows/fetch_msys2_sysroot.py),
[packager](../tools/windows/package.sh),
[SDK test](../s7hardware/tests/sdk_smoke.cmake).

## Audio: PortAudio first, with explicit limits

PortAudio supports Windows host APIs including WASAPI, MME, DirectSound,
WDM/KS and ASIO; its v19.7 CMake configuration enables WASAPI by default.
See the [Windows build documentation](https://files.portaudio.com/docs/v19-doxydocs/compile_windows.html)
and [versioned build configuration](https://github.com/PortAudio/portaudio/blob/v19.7.0/CMakeLists.txt).
Start with the ordinary MSYS2 PortAudio package and select a device whose
enumerated host API is WASAPI. Treat ASIO as a later, separately qualified
profile if a concrete device/latency requirement justifies it.

The existing S7 adapter already reports the host API and selected device,
validates nominal rate, uses bounded callback queues, and keeps graph execution
outside the callback. Those semantics can remain unchanged. However, it accepts
no backend options and supplies no host-specific stream information. Do not
claim WASAPI exclusive mode, explicit loopback capture, bit-exact physical PCM,
or controlled OS resampling from this implementation. Verify actual available
formats/rates and record OS/device conversion as unknown where appropriate.

PortAudio device indices are unstable. Enumerate and explicitly select an
endpoint for each qualified run; `default` does not guarantee WASAPI or the
same physical device between runs. Native tests should cover playback, capture,
variable callback sizes, underflow/overflow, cancellation, endpoint removal and
bounded shutdown. The current Linux PipeWire sink-monitor experiment is not a
Windows loopback test. Finite playback still aborts pending output; Windows
support must not accidentally imply graceful DAC drain.

## SDR: library, module, driver and device are separate requirements

The dependency chain is:

```text
S7 SdrSource/SdrSink -> S7 Soapy provider -> libSoapySDR.dll
                                          -> device support module
                                             -> vendor/USB library
                                                -> Windows driver -> radio
```

SoapySDR uses runtime-loadable device modules, as described in its
[upstream overview](https://github.com/pothosware/SoapySDR/wiki).
Shipping only `libSoapySDR.dll` does not supply radio support. Select a small
supported device set and ship/build its modules against the same Soapy ABI and
compatible MinGW/libstdc++ runtime. Avoid mixing arbitrary MSVC C++ module
binaries with our MinGW Soapy installation.

Use a separate directory for Soapy modules, distinct from S7 element plugins.
For a flat application bundle, set an application-local
`SOAPY_SDR_PLUGIN_PATH` or provide an installation layout/root that Soapy can
resolve. Soapy 0.8.1 derives its Windows installation root by walking up from
its DLL and searches `<root>/lib/SoapySDR/modules<ABI>`; simply moving its DLL
beside our EXEs changes that calculation. Its
[versioned module loader](https://github.com/pothosware/SoapySDR/blob/soapy-sdr-0.8.1/lib/Modules.in.cpp)
also confirms that custom module paths use semicolon separators on Windows.
Include relocated-package discovery checks.

For USB radios, a user-space DLL is only part of the installation. Qualify the
appropriate driver for the particular device/interface. The
[libusb Windows guide](https://github.com/libusb/libusb/wiki/Windows#driver-installation)
explains supported drivers and WinUSB installation. Network-connected radios
can avoid a Windows USB driver dependency, but still need their provider,
protocol libraries, firmware compatibility and native throughput tests.

S7's existing explicit selector, format/full-scale checks and fault semantics
remain applicable. A Windows build does not establish CI12/CI16 support for a
new module/device, lossless nominal-rate capture, or multi-channel RF coherence.

## Private AD936x route

The public contract already permits a separately loaded private provider using
the installed S7 SDK. Keep that boundary: no private source or dependency
fetching belongs in the public Windows build.

Review of the separate private build found an explicit libiio 0.x requirement
and rejection of 1.x. MSYS2 currently supplies
[libiio 0.26-4](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-libiio)
and [libad9361-iio 0.3-1](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-libad9361-iio),
which are plausible dependency candidates. Pin and test the actual pair rather
than substituting current libiio main/1.x documentation or binaries. Upstream
[libiio](https://github.com/analogdevicesinc/libiio) explicitly supports Windows
clients communicating with Linux IIO targets over USB or Ethernet; Windows
does not need a local Linux IIO kernel subsystem for that use.

The next private feasibility build should target the core radio library and
provider, with Linux audio dependencies disabled and unrelated applications,
DSP helpers and embedded diagnostics excluded as necessary. The private
top-level build currently includes extra applications/audio targets, so it is
not yet a verified minimal Windows build entry point. Build the provider with
the same S7 toolchain, verify its exported factory and dependency loading, then
test an explicit network IIO URI before adding Windows USB qualification.
Keep Soapy and private AD936x qualification independent; one does not replace
the other or establish its channel/profile support.

## Proposed delivery sequence

1. Restore a reproducible Clang 22 cross toolchain and record exact sysroot
   package versions. Run a backend-disabled build first to verify the portable
   contract, element plugins, offline checks and installed SDK.
2. Add `portaudio` and `soapysdr` to Windows dependency acquisition. Make a
   hardware build explicitly require `S7_PORTAUDIO=ON`, `S7_SOAPYSDR=ON`,
   `S7_PIPEWIRE=OFF`. Provide an explicit backend-disabled option as well.
   Fail required profiles when packages are missing; do not silently rely on
   `AUTO` for release artifacts.
3. Correct project DLL staging, Wine paths, Soapy module deployment, notices
   and Windows audio examples. Validate a relocated installed bundle with no
   build-tree/sysroot DLL paths available.
4. Run fake-provider/adapter and installed-SDK tests under Wine. Then use a
   native Windows machine for endpoint/driver discovery and physical-device
   qualification. Separate ordinary CI from explicitly enabled live tests.
5. Build/package the private provider separately and qualify its network and
   USB profiles. Record capture continuity, format, channels, cancellation,
   disconnect behavior and measured sustainable rate per actual device.

The intended public hardware configure profile, **after dependencies and
toolchain are present**, is:

```sh
cmake -S . -B build-win-hardware -G Ninja \
  --toolchain cmake/toolchains/x86_64-w64-mingw32-clang.cmake \
  -DS7_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release \
  -DS7_PORTAUDIO=ON -DS7_PIPEWIRE=OFF -DS7_SOAPYSDR=ON
cmake --build build-win-hardware --target build-tests --parallel 2
ctest --test-dir build-win-hardware \
  -R 's7(hardware|audio|sdr)' --output-on-failure
```

This is a proposed verification command, not a currently verified recipe:
the DLL-path and SDK-test gaps above must be addressed. For the portable
backend-disabled profile set all three backend options to `OFF`.

Keeping hardware execution on Linux is also a valid initial deployment choice,
with Windows used as a client. Preserve explicit network boundaries and the
device-paced graph owner. This does not make Studio/service hardware activation
available today; remote launch/control and graph/stream integration still need
their own implementation and qualification.

## Evidence limits at the initial research stage

This research inspected public build/provider/packaging code, the installed
Windows sysroot inventory, the separate private build's dependency constraints,
and the primary sources linked above. No application code was changed, no
devices were opened and no native Windows execution was performed.

No fresh Windows build was completed during the initial research pass. At that
time the host exposed Clang 23 and the Clang 22 executable referenced by an
older Windows build cache was absent. Clang 22 has since been restored and the
sound-only build/verification is recorded in [Windows builds](WINDOWS_BUILD.md).
`s7core/CMakeLists.txt` deliberately rejects Clang 23 on Windows because the
current F128 ABI adapter requires Clang 22. The ABI check remains enabled.
Old Windows artifacts and Linux live tests
cannot establish the new modules' Windows support. See also
[Windows build details](WINDOWS_BUILD.md), [hardware implementation limits](HARDWARE_IO.md)
and [existing Linux live evidence](HARDWARE_IO_LIVE_TESTS.md).
