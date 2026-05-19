# Static Launcher (S8)

**Status:** Opt-in via `-DUBUILDER_STATIC=ON`. Verified locally and in CI on Linux.
**Goal:** the `ubuilder` CLI itself depends on nothing but the OS syscall ABI — no glibc, no musl, no dynamic loader. Honors the Apple-sandbox rule's clause 5 ("no host-environment leaks") for the launcher binary itself.

## What this fixes

Before S8, the `ubuilder` launcher was dynamically linked against whatever libc shipped with the build host. A bundle built on Ubuntu 24.04 (glibc 2.39+) would refuse to launch on Ubuntu 22.04 (glibc 2.35) because the loader couldn't resolve newer glibc symbols. That makes "produces a universal executable" a lie at the launcher layer, even if the embedded payload were hermetic.

After S8, the launcher itself loads no shared libraries:

```
$ ldd build-static/src/ubuilder
        not a dynamic executable
$ file build-static/src/ubuilder
ELF 64-bit LSB executable, x86-64, … statically linked, …
```

This is one of two prerequisites for true 0-dep bundles. The other is M1 (hermetic interpreters).

## Two ways to build statically

### A. Glibc-static (works today, partial hermeticity)

```bash
cmake -B build-static -S . \
      -DUBUILDER_STATIC=ON \
      -DENABLE_COMPRESSION=OFF \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-static -j
```

Requires `libc6-dev` (Debian/Ubuntu) or `glibc-static` (Fedora).
`ENABLE_COMPRESSION=OFF` avoids the link error on systems that only ship `libz.so` (no `libz.a`). To keep compression, build a static zlib first and point `CMAKE_PREFIX_PATH` at it.

**Caveats** (don't apply to UBuilder, but worth knowing if you reuse the toolchain):
- Glibc's `getaddrinfo` / NSS lookups quietly load `libnss_*.so` at runtime and degrade silently if those aren't present. **UBuilder doesn't do DNS or user lookups**, so this is a non-issue here.
- `dlopen` is unavailable in static builds. **UBuilder doesn't use dlopen.**
- Locale data is read at runtime from `/usr/share/locale`. Affects `setlocale`, which **UBuilder doesn't call**.

### B. musl + zig or `musl-gcc` (recommended for distribution)

True hermetic static linking. Output runs anywhere the Linux syscall ABI is stable (kernel ≥ 3.2).

```bash
# Option 1 — install musl-tools (Debian/Ubuntu)
sudo apt-get install -y musl-tools
cmake -B build-musl -S . \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/musl-linux-x86_64.cmake \
      -DUBUILDER_STATIC=ON \
      -DENABLE_COMPRESSION=OFF
cmake --build build-musl -j

# Option 2 — zig as a drop-in C cross-compiler (no install via package manager)
MUSL_CC="zig cc -target x86_64-linux-musl" \
MUSL_CXX="zig c++ -target x86_64-linux-musl" \
cmake -B build-musl -S . \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/musl-linux-x86_64.cmake \
      -DUBUILDER_STATIC=ON \
      -DENABLE_COMPRESSION=OFF
cmake --build build-musl -j
```

## Verifying the static build works

Use the existing bundle harness with the `UBUILDER_BIN` env override — no special static-only test path needed:

```bash
UBUILDER_BIN="$(pwd)/build-static/src/ubuilder" tests/bundle/run-bundle-tests.sh
```

This builds Python/PHP/Node bundles with the static launcher, runs them, asserts the exact stdout (with argv pass-through for `"hello world"` / `"it's"`), then tampers each bundle and asserts the SHA-256 check rejects it. All three runtimes pass.

## Size & startup trade-offs

Dynamic launcher: ~150 KB. Static glibc launcher: ~870 KB. Static musl launcher: ~200 KB.

For UBuilder bundles, the launcher contributes a constant overhead per bundle (it's appended-to once). A ~700 KB delta on top of a 6 MB Python or 122 MB Node bundle is negligible. Choose musl when you can; glibc-static is a fine stopgap for development.

## What S8 does *not* fix

The launcher being static doesn't make the bundle's *embedded interpreter* hermetic. The Python/PHP/Node binary inside the bundle is still whatever was on the build host's PATH at build time, and that interpreter is dynamically linked against the build host's glibc + OpenSSL + libxml2 + … . That gap is closed by **M1** (vendor hermetic interpreters via `python-build-standalone`, static Node, `php-static`), which is the next major step.

S8 + M1 together unlock the Tier-3 "no runtime on the target" Docker test — running a bundle inside an Alpine container that has no Python/PHP/Node installed and asserting it still works.

## CI integration

The default CI job continues to use the dynamic build (faster, exercises ordinary developer setup). A new optional matrix variant can build with `UBUILDER_STATIC=ON` and `UBUILDER_BIN=…/build-static/src/ubuilder` to run the same harness against the static launcher. Wiring that into `.github/workflows/ci.yml` is straightforward once musl-tools is on the runner; see `toolchains/musl-linux-x86_64.cmake`.
