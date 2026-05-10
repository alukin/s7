# RSocket/Folly Dependency Bootstrap

This workspace vendors `rsocket-cpp` as a submodule at `external/rsocket`.

To configure S7 with clean, unguarded RSocket integration, native dependencies for
`folly` and `rsocket-cpp` must exist first.

## One-command helper

Use the bootstrap script:

```bash
cd /home/al/projects/s7/s7pp
./scripts/bootstrap_deps.sh
```

The default mode is **dry-run** (prints commands only).

Apply install:

```bash
cd /home/al/projects/s7/s7pp
./scripts/bootstrap_deps.sh --install
```

Install + configure with rsocket dependency bootstrap:

```bash
cd /home/al/projects/s7/s7pp
./scripts/bootstrap_deps.sh --install --configure
```

During `--configure`, the script also applies tracked rsocket compatibility
patches from `third_party_patches/rsocket/` via:

```bash
./scripts/apply_third_party_patches.sh
```

If you need to skip that step:

```bash
./scripts/bootstrap_deps.sh --install --configure --no-apply-patches
```

## What it installs

### Fedora / RHEL family

- `gcc-c++`, `gcc`, `make`, `cmake`, `ninja-build`, `pkgconf-pkg-config`, `git`
- `openssl-devel`, `gflags-devel`, `glog-devel`, `fmt-devel`, `double-conversion-devel`, `fast_float-devel`
- `boost-devel`, `libevent-devel`, `libunwind-devel`, `zlib-ng-compat-devel`, `zstd-devel`, `libsodium-devel`

### Debian / Ubuntu family

- `build-essential`, `cmake`, `ninja-build`, `pkg-config`, `git`
- `libssl-dev`, `libgflags-dev`, `libgoogle-glog-dev`, `libfmt-dev`, `libdouble-conversion-dev`, `libfast-float-dev`
- `libboost-all-dev`, `libevent-dev`, `libunwind-dev`, `zlib1g-dev`, `libzstd-dev`, `libsodium-dev`

## Configure/build after deps

```bash
cd /home/al/projects/s7/s7pp
cmake -S . -B build_verify -DCMAKE_BUILD_TYPE=Release -DRSOCKET_INSTALL_DEPS=ON
cmake --build build_verify -j4
ctest --test-dir build_verify --output-on-failure
```

## Notes

- The script uses `sudo` for package installation.
- If your distro is unsupported, install equivalent dev packages manually and rerun configure.
- `external/rsocket/scripts/build_folly.sh` may still fetch/build folly sources when `-DRSOCKET_INSTALL_DEPS=ON` is enabled.
- Local rsocket changes are preserved as patch files in `third_party_patches/rsocket/` for environments where you cannot push to upstream submodule repo.

