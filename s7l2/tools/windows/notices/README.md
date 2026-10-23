# FFTW 3.3.11 notices

The MSYS2 UCRT64 FFTW 3.3.11-2 binary package omits installed licence files.
Its unmodified upstream `COPYING` and `COPYRIGHT` are retained here for binary
packaging; the collector limits this fallback to that exact package hash.

- Binary package: `mingw-w64-ucrt-x86_64-fftw-3.3.11-2-any.pkg.tar.zst`
- Binary SHA256: `8bcffc286600ae011726e9356f19ea8afba0a622427c77e89b5a09ecce252d95`
- [MSYS2 source package](https://mirror.msys2.org/mingw/sources/mingw-w64-fftw-3.3.11-2.src.tar.zst)
- Source package SHA256: `a2ff0d734d5a699e8d1beb5c1cd2740197dd254c5666704177cfb9b48017dfc4`
- Contained upstream `fftw-3.3.11.tar.gz` SHA256:
  `5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1`,
  verified against the included MSYS2 `PKGBUILD`.

Only the notice files enter the binary bundle. Dependency source archives are
not bundled. The `.license` sidecars provide REUSE metadata for these verbatim
third-party texts.
