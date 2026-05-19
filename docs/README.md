# UBuilder docs

Two top-level sections — pick the one that matches what you're doing.

## For users — building bundles with ubuilder

Start here if you're packaging a Python / PHP / Node app.

- [Getting started](user/getting-started.md) — install + first bundle.
- [Quick start](user/quick-start.md) — minimal walkthrough.
- [CLI reference](user/cli-reference.md) — every flag, what it does.
- [Examples](user/examples.md) — Laravel, Flask, Express, GUI apps, etc.
- [Architecture overview](user/architecture-overview.md) — how a bundle is laid out and what runs at startup.
- [Troubleshooting](user/troubleshooting.md) — common errors and fixes.
- [Guides](user/guides/) — debug-mode usage, project instructions.
- [Bundle test harness](../tests/bundle/README.md) — end-to-end validation tests you can run.

## For contributors — working on ubuilder itself

Read these if you're modifying ubuilder's source, fixing a bug, or planning a feature.

- [Architecture audit](internals/architecture/ARCHITECTURE_AUDIT.md) — engineering review of the path to true zero-dependency executables.
- [Config file spec](internals/architecture/CONFIG_FILE_SPEC.md) — `ubuilder.json` schema, discovery, CLI/config precedence.
- [Static launcher (S8)](internals/architecture/STATIC_LAUNCHER.md) — building with `-DUBUILDER_STATIC=ON` and the musl toolchain.
- [Hermetic interpreters (M1)](internals/architecture/M1_HERMETIC_INTERPRETERS.md) — vendoring strategy, `--runtime-source`, Tier-3 isolation.
- [User dependency install (M8)](internals/architecture/M8_USER_DEPS.md) — staging the runtime, installing user deps without polluting the shared cache.
- [Roadmap](internals/architecture/ROADMAP_NEXT.md) — what's planned next.
- [Release process](internals/releases.md) — how to cut a release.
- [Historical reports](internals/reports/) — snapshots from earlier milestones (kept for reference; not load-bearing).

## Project links

- [Main README](../README.md)
- [Changelog](../CHANGELOG.md)
- [Contributing](../CONTRIBUTING.md)
- [License](../LICENSE)
