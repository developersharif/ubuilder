<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logowhite.png">
    <img src="docs/assets/logo-transparent.png" alt="UBuilder" width="180">
  </picture>
</p>

<h1 align="center">UBuilder</h1>

<p align="center">
  Ship your Python, Node.js, or PHP app as <b>one file</b>. No installers. No runtimes. No surprises.
</p>

<p align="center">
  <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="MIT License"></a>
  <a href="https://github.com/developersharif/ubuilder/releases/latest"><img src="https://img.shields.io/github/v/release/developersharif/ubuilder" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Linux-supported-success.svg" alt="Linux">
  <img src="https://img.shields.io/badge/macOS-supported-success.svg" alt="macOS">
  <img src="https://img.shields.io/badge/Windows-supported-success.svg" alt="Windows">
</p>

UBuilder takes your scripting-language project and produces a single
self-contained executable. Hand that file to anyone — your customer,
your CI runner, a fresh VM — and it just runs. They don't need Python.
They don't need Node. They don't need PHP, `pip`, `npm`, or
`composer`. They don't even need to know what language you wrote it in.

```bash
cd my-app/        # your project, just main.py and (optionally) requirements.txt
ubuilder          # one command. no flags needed.
./dist/my-app     # ships anywhere on the same OS — fully standalone
```

That's the whole pitch.

---

## Why people use it

- **Zero-friction distribution.** Email it, drop it on a USB stick, put it in a Docker `FROM scratch` — the bundle has no external dependencies.
- **Hermetic by design.** The build pulls pinned interpreter tarballs into a local cache once, then embeds them into every bundle. Your users get the exact runtime you tested against.
- **Tiny mental model.** A `ubuilder.json` is two lines. The first build writes it for you. Re-running needs zero flags.
- **Honest cross-platform.** Linux and macOS bundles are fully hermetic, including PHP — the macOS PHP builder walks `otool -L` and re-points every non-system dylib to live inside the bundle, so it runs on any Mac with nothing pre-installed.
- **Fast.** Pure C11/C++17. The launcher is a few hundred KB; the bundle layout is `[launcher][runtime][app][SHA-256 trailer]` and extraction is a single sequential read.

---

## Install

### Linux (x86_64)

```bash
curl -L https://github.com/developersharif/ubuilder/releases/latest/download/ubuilder-linux-amd64.tar.gz | tar -xz
sudo mv ubuilder /usr/local/bin/
ubuilder --version
```

### macOS (Apple Silicon)

```bash
curl -L https://github.com/developersharif/ubuilder/releases/latest/download/ubuilder-macos-amd64.tar.gz | tar -xz
sudo mv ubuilder /usr/local/bin/
xattr -d com.apple.quarantine /usr/local/bin/ubuilder 2>/dev/null || true
ubuilder --version
```

### Windows (x86_64)

```powershell
$url = "https://github.com/developersharif/ubuilder/releases/latest/download/ubuilder-windows-amd64.zip"
Invoke-WebRequest -Uri $url -OutFile "$env:TEMP\ubuilder.zip"
Expand-Archive -Path "$env:TEMP\ubuilder.zip" -DestinationPath "$env:USERPROFILE\ubuilder" -Force
$env:Path += ";$env:USERPROFILE\ubuilder"
ubuilder --version
```

### From source

Needs CMake ≥ 3.16, a C11/C++17 compiler, and `zlib` headers.

```bash
git clone https://github.com/developersharif/ubuilder.git
cd ubuilder && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j
```

---

## Your first bundle

Pick a runtime. Each example is a complete, runnable session.

### Python

```bash
mkdir hello && cd hello
echo 'print("Hello from a single-file Python app")' > main.py

ubuilder          # auto-writes ubuilder.json on the first run
./dist/hello
```

With dependencies, just add a `requirements.txt` — ubuilder will `pip install` them into the bundle.

### Node.js

```bash
mkdir hello && cd hello
echo 'console.log("Hello from a single-file Node app")' > main.js
echo '{"runtime":"node","entry_point":"main.js"}' > ubuilder.json

ubuilder
./dist/hello
```

Add a `package.json` and ubuilder will `npm install` for you.

### PHP

```bash
mkdir hello && cd hello
echo '<?php echo "Hello from a single-file PHP app\n";' > main.php
echo '{"runtime":"php","entry_point":"main.php"}' > ubuilder.json

ubuilder
./dist/hello
```

For minimal PHP bundles (~50–80 MB instead of 280–400 MB), use the curated static build:

```bash
ubuilder --runtime=php --php-runtime=static
```

---

## Everyday commands

```bash
# Trim files out of the bundle
ubuilder --exclude='tests/**' --exclude='*.md'

# Drop a dependency from the install
ubuilder --exclude=six           # Python
ubuilder --exclude=is-number     # Node
ubuilder --exclude=ext-curl      # PHP

# Pick where the output goes
ubuilder --output=/opt/builds/myapp

# See what it's doing
ubuilder --verbose

# Upgrade yourself
ubuilder --self-update
```

The full flag list is in `ubuilder --help`.

---

## Documentation

Everything else — full CLI reference, the `ubuilder.json` schema, guides
per runtime, architecture overview, troubleshooting — lives in
**[the docs](docs/README.md)**.

---

## Platform support

| Runtime | Linux | macOS | Windows |
|---|---|---|---|
| **Python** | Hermetic | Hermetic | Host |
| **Node.js** | Hermetic | Hermetic | Host |
| **PHP** | Host-bits hermetic | Fresh-Mac portable | Host |

*Hermetic* means the bundle ships its own interpreter and runs on a
clean machine with nothing pre-installed. *Host* means the bundle uses
the target machine's interpreter — works fine for dev, not yet for
distribution. Hermetic Windows is on the roadmap and a great place to
contribute.

---

## How it works

UBuilder is one C binary that operates in two modes, decided at
startup:

1. **Build mode** (no payload attached) parses your project, picks the
   right runtime builder, installs your dependencies into a staged
   copy, and writes a new executable laid out as
   `[ubuilder launcher][runtime tree][app tree][V4 trailer with SHA-256]`.
2. **Launcher mode** (payload present) detects the trailer, verifies
   the SHA-256, extracts everything to a temp directory, `exec`s the
   embedded interpreter against the embedded entry point, and cleans
   up on exit.

Successful dependency installs are content-addressed by SHA-256 of
your manifest + lockfile and replayed from cache on the next build —
so re-running is fast and reproducible.

---

## Contributing

UBuilder is built by one developer and a small group of contributors
who care a lot about making distribution boring. If that sounds fun,
jump in.

A typical loop:

```bash
git clone https://github.com/developersharif/ubuilder.git
cd ubuilder && mkdir build && cd build
cmake .. && cmake --build . -j
./tests/test_ubuilder     # all green before you change anything
```

Good first issues live on the [issue tracker](https://github.com/developersharif/ubuilder/issues).
High-impact areas right now:

- **Hermetic Windows runtimes** — vendor a portable Python / Node tree the way Linux and macOS already do.
- **Fully hermetic PHP on Linux** — port the macOS dylib-rewiring trick to ELF (`ldd` walk + `DT_RUNPATH=$ORIGIN/../lib`).
- **Lockfile reproducibility** for Python and PHP.
- **More example apps** — Flask, Express, Laravel, Discord bots, anything that proves the "one file, ships anywhere" idea.

Setup details, code conventions, and PR workflow are in
[CONTRIBUTING.md](CONTRIBUTING.md).

---

## License

MIT — see [LICENSE](LICENSE). Use it, fork it, ship it.
