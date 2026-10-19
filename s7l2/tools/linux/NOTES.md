# Linux packaging notes

`package.sh` bundles Qt (and the Qt plugins s7studio needs: `xcb` platform,
`svg` icon engine, ...) and their shared-library dependencies via
[linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) +
[linuxdeploy-plugin-qt](https://github.com/linuxdeploy/linuxdeploy-plugin-qt).
It deliberately does not bundle base libraries every desktop Linux install
already provides: glibc, X11/xcb, GL/EGL, D-Bus, fontconfig, HarfBuzz, ...
linuxdeploy's own curated list decides that split; `package.sh` only adds a
narrow safety-net check (`libc`, `libGL`, `libEGL`, ...) on top of it.

## What has been verified (2026-10-16)

A package built on this machine (Ubuntu 26.04, glibc 2.43) was extracted and
run, with no system Qt installed, in a vanilla `ubuntu:26.04` container with
only these packages added (the baseline any real desktop Linux install
already has, just not a bare server container):
`libegl1 libgl1 libglx-mesa0 libopengl0 libx11-6 libxcb1 libdbus-1-3
libfontconfig1 libxkbcommon0 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4
libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0
libxcb-xinput0 libglib2.0-0 libglvnd0 libharfbuzz0b libsm6 libice6`.

With those present, `s7studio` started under `xvfb-run` and stayed up;
`ldd` found no unresolved libraries anywhere in the bundle. The `libs7noise`
plugin, copied into `./plugins` (a real file, not a symlink -- the plugin
loader skips symlinks), resolves its `libs7core.so.0` dependency from the
copy already loaded into the process by the main executable, not from its
own rpath -- confirmed directly with a `ctypes` dlopen test, not inferred.

## What has NOT been verified: "any distro"

The build this was tested against requires **GLIBC_2.43 / GLIBCXX_3.4.35**
(this script prints the exact figures for your build). That is this year's
Ubuntu; it will not run on current-generation distros (24.04, Debian 12,
etc.), let alone older ones, regardless of how packaging bundles libraries --
glibc and libstdc++ are forward-compatible, not backward. No AppImage-style
bundling can lower a glibc symbol version requirement baked in at compile
time.

To actually support "any distro," the build itself -- not this packaging
step -- needs to happen on a container with an old-enough glibc/libstdc++
baseline (the way Python wheels use `manylinux` containers: a newer compiler
targeting an older glibc). That is a separate, larger piece of work: this
project also requires C++26 and Boost 1.75+ (`CMakeLists.txt`), so the
container needs a recent-enough compiler even while keeping an old glibc --
verify an old distro's Boost is new enough before picking one, Debian/Ubuntu
LTS repos often are not.

Until that exists, use `S7_MAX_GLIBC` to make `package.sh` fail loudly
instead of silently producing a package that only runs on machines at least
as new as the build host.
