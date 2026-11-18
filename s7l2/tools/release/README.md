# Release artifacts

The next release is planned to include three distribution forms:

1. The existing Linux portable tarball, built with `tools/linux/package.sh`.
2. Native Fedora RPMs and Ubuntu DEBs, built and verified separately on each
   target distribution using the [native packaging workflow](../../docs/NATIVE_LINUX_PACKAGES.md).
3. The existing Windows portable ZIP, cross-built with Clang 22/MinGW and the
   MSYS2 UCRT64 sysroot. Windows includes PortAudio sound elements and excludes
   SDR; Linux retains sound and SDR. Numerical spectral/wavelet plugins remain
   available on both platforms.

Build and qualify the artifacts from the intended release source/version first.
The collection command accepts both portable builds and repeated native package
directories. For example, replacing `VERSION` with the intended release version:

```sh
tools/release/make_release.sh VERSION dist/release-VERSION \
  --linux-build build-release --windows-build build-win-release \
  --native-packages dist/native/fedora45 \
  --native-packages dist/native/ubuntu26.04
```

The resulting directory contains `s7studio-VERSION-linux-x86_64.tar.gz`,
`s7studio-VERSION-windows-x86_64.zip` and `native/<distribution>/` with RPM/DEB
files, existing provenance/checksums and verification records. Its top-level
`SHA256SUMS` covers both archives and all native binary packages. Relative output
directories are supported. An existing native output directory is rejected to
avoid mixing stale packages into a new collection.

The script only collects local files; it neither signs nor uploads a release.
Current development artifacts still use the project's current version;
collecting them does not create or publish the next release.
