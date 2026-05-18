# tests/bundle — end-to-end bundle harness

This directory contains the **only test in the repo that actually validates
UBuilder's core claim** — that the CLI can take a project directory and emit
a working bundled executable.

## What it does

For each runtime (Python, PHP, Node.js):

1. Build `ubuilder` if not already built.
2. Run `ubuilder --project-dir=fixtures/<rt> --runtime=<rt> --output=<tmp>/app`.
3. Copy the produced bundle into a clean temp directory (so the bundle cannot
   accidentally read files from the source fixture dir at run time).
4. Run the bundle with two fixed argv values.
5. Assert exit code `0` and that stdout exactly matches `fixtures/<rt>/expected.txt`.

The fixture programs are intentionally trivial (`print hello`, echo argv,
`sum(1..10)`) so the assertions are deterministic and don't depend on stdlib
extras like `json` or `requests`.

## Running

```bash
# All runtimes:
tests/bundle/run-bundle-tests.sh

# A subset:
tests/bundle/run-bundle-tests.sh python php

# Keep the work directory after the run (for debugging):
UBUILDER_KEEP_ARTIFACTS=1 tests/bundle/run-bundle-tests.sh

# Trace every command:
UBUILDER_VERBOSE=1 tests/bundle/run-bundle-tests.sh
```

Exit code is the number of failed cases (0 on full success).

## What it does **not** prove (today)

The harness asserts the bundle runs and produces correct output. It does
**not** yet prove that the bundle is hermetic (i.e. would still work on a
host with no Python/PHP/Node installed). The architecture audit
(`docs/architecture/ARCHITECTURE_AUDIT.md`, §3 G1–G2) explains why: today's
Unix builders embed only the interpreter binary, not the stdlib, and the
launcher silently falls back to the host interpreter if extraction fails.

A Tier-3 "clean room" Docker-based harness is planned alongside the M1
hermetic-interpreter work. Until then, this harness skips a runtime when the
host doesn't have it installed (it can't possibly pass — and we want the
skip to be loud, not a silent green).

## Adding a new fixture

1. Create `tests/bundle/fixtures/<runtime>/main.<ext>` with deterministic output.
2. Create `tests/bundle/fixtures/<runtime>/expected.txt` (the exact stdout).
3. Create `tests/bundle/fixtures/<runtime>/ubuilder.json` (consistency with `examples/`).
4. If it's a new runtime, add it to `ALL_RUNTIMES` and the three case-statements
   (`fixture_dir_for`, `runtime_flag_for`, `host_runtime_for`) at the top of
   `run-bundle-tests.sh`.

The harness passes `harness-arg-1 harness-arg-2` as argv; fixtures should
echo those back so the harness simultaneously validates argv pass-through.
