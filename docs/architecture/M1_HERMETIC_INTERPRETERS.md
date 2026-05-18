# M1 — Hermetic Interpreters

**Status:** ✅ Python + Node end-to-end (M1-A plumbing + M1-B/E tree embed/extract). Hermetic Python (185 MB) and Node (186 MB) bundles both pass the `PATH=/nonexistent` test in the harness. PHP builder accepts `--runtime-source=<binary>` (M1-D plumbing done) but no upstream pre-built static PHP exists — `static-php-cli` is the documented self-build path. Tier-3 Docker test (M1-C) is the natural next step.
**Companion to:** `ARCHITECTURE_AUDIT.md` (this implements M1).
**Goal:** UBuilder bundles stop depending on the build host's `/usr/bin/python3` (etc.). The bundle's interpreter is a vendored, redistributable build whose ABI is independent of whatever ran the build.

This is the single biggest hermeticity step in the audit. After M1, paired with S8 (static launcher), the bundle can legitimately advertise "zero dependencies" — both the launcher and the embedded interpreter are independent of the target host's libc, OpenSSL, etc.

## Where the interpreter comes from

| Runtime | Source | Artifact | License |
|---|---|---|---|
| Python | [`astral-sh/python-build-standalone`](https://github.com/astral-sh/python-build-standalone) | `cpython-3.12.13+20260510-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz` (~32 MB) | PSF / Apache-2 |
| Node   | [`nodejs.org` official](https://nodejs.org/dist/) | `node-v24.15.0-linux-x64.tar.xz` (~25 MB; glibc 2.28+) | MIT + V8 BSD |
| PHP    | **No upstream pre-built static binary.** Workaround: build with [`crazywhalecc/static-php-cli`](https://github.com/crazywhalecc/static-php-cli) and pass the result via `--runtime-source`. M1-D will automate this. | TBD | PHP License |

We do **not** build interpreters from source ourselves in this repo — that's a multi-day operation per platform-arch and is not where UBuilder's value lives. We consume upstream binary releases with pinned SHA-256s.

## Configuration surface

Three ways to point at a hermetic interpreter, in precedence order:

1. **CLI:** `ubuilder --runtime-source <path>` (overrides everything)
2. **Config file:** `ubuilder.json` key `runtime_options.<runtime>.source: "/path/to/runtime-tree"`
3. **Default cache lookup:** if neither is set, UBuilder looks at `$UBUILDER_RUNTIMES_CACHE/<runtime>/<version>/` (default `~/.cache/ubuilder/runtimes/`). If the cache is empty, falls back to the pre-M1 host-probe behavior with a verbose warning that the bundle is non-hermetic.

The `<path>` may be either a directory (treated as a runtime *tree*) or a single file (treated as a runtime *binary*, like today's Unix path). The builder picks the embed format accordingly.

## Vendoring

`scripts/vendor-runtimes.sh` downloads each interpreter from its upstream release, verifies the SHA-256 against an in-script manifest, and extracts to `$UBUILDER_RUNTIMES_CACHE/<runtime>/<version>/`. The download is idempotent (skipped if `.vendor-ok` marker exists).

Manifest entries are pinned: any update requires editing the script with the new version + hash. This keeps "what we ship" reproducible.

```bash
scripts/vendor-runtimes.sh              # default set
scripts/vendor-runtimes.sh python       # specific runtime
UBUILDER_RUNTIMES_CACHE=/tmp/x scripts/vendor-runtimes.sh python
```

## Runtime payload format (M1-B, shipped)

```
[ u32 magic 'UBRT' (0x54524255) ]
[ records:
   [u16 path_len][path bytes ('/' separators)]
   [u32 mode (low 12 bits = unix perms)]
   [u64 file_size]
   [file bytes]
   ... ]
[ u16 path_len = 0 — end-of-tree sentinel ]
[ application files (existing per-builder format) ]
[ 32 bytes SHA-256(payload) ]
[ uint64_t data_start_offset ]
[ "UBUILDER_MODULAR_V4_SHA256_MARKER" ]
[ uint32_t magic 0xDEADBEEF ]
[ enum runtime ]
```

The runtime-payload prefix `'UBRT'` is the in-band format tag; the V4
trailer is unchanged. The launcher peeks the first 4 bytes of the runtime
payload to decide between tree-format extraction (new) and the legacy
single-binary extractor (used by PHP / Node until M1-D / M1-E). This lets
Python migrate independently of the other runtimes without a forced clean
break on existing PHP / Node bundles.

The sentinel-terminated record list (rather than a leading count) is
deliberate — the bundle writer keeps the output file in append mode
through the whole build, which precludes seek-back patching. Sentinel is
streaming-friendly. (We discovered the count-patching pitfall the hard
way: first M1-B attempt wrote a leading count then `fseek`'d to patch
it, which the OS silently sent to EOF. Test caught it.)

The `mode` field preserves the executable bit on extraction so
`bin/python3` stays runnable without hard-coding which files inside the
tree are binaries.

## Extraction & invocation

The launcher extracts the tree to `<temp>/<bundle-pid>/runtime/<original-tree-layout>`, then invokes `<temp>/<bundle-pid>/runtime/bin/python3 <script>` via `pc_spawn_and_wait` (S1). The hermetic Python knows how to find its own `lib/python3.12/` etc. because we preserved the upstream directory layout.

Cache reuse (M5) is deferred: today's launcher still extracts per-PID. M5 will move this to a content-addressed `~/.cache/ubuilder/runtimes-extracted/<sha256-of-tree>/` so 10 bundles built from the same vendored Python share one extracted copy.

## Tier-3 hermeticity test (M1-C, next commit)

Once the build path is wired, the harness gains a third tier:

```bash
# Tier 3: bundle runs in an Alpine container with NO Python installed.
docker run --rm -v $(pwd):/work alpine /work/dist/my-app
```

If the bundle still runs, it's genuinely 0-dependency. If not, we've learned something concrete about what's still leaking from the build host.

## What this commit does (M1-A + M1-B-Python)

- ✅ `scripts/vendor-runtimes.sh` — Python 3.12.13 pinned, SHA-256 verified.
- ✅ CLI flag `--runtime-source <path>` + config key `runtime_options.<rt>.source`.
- ✅ `ub_config_t.runtime_source` field.
- ✅ Tree embed/extract path in `runtime_embedder.c` (V5 trailer marker).
- ✅ `python_builder.c` uses the tree path when `runtime_source` points at a directory.
- ✅ Bundle harness optionally runs against a vendored Python.

Deferred:

- M1-C: Tier-3 Docker test. Requires this commit's runtime tree extraction to work in a no-Python-on-PATH container.
- M1-D: PHP (`php-static` integration).
- M1-E: Node (musl-static).
- M5: content-addressed cache (multiple bundles share extracted tree).
- arm64 / macOS in the vendor manifest.
