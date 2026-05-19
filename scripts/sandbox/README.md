# Project-local sandbox

Redirects `TMPDIR` and the common package-manager caches into `./.sandbox/`
inside the repo, so a full system `/tmp` (or noisy global caches) can't
break the shell or the build.

The scripts only mutate the **current shell's** environment — they touch
no global config, no dotfiles, no system files. Close the shell or
`deactivate` to undo.

## Usage

```sh
source scripts/sandbox/activate.sh
# ... build, test, run ubuilder ...
source scripts/sandbox/deactivate.sh
```

Optional shortcut — add to your shell rc (not required):

```sh
alias ub-on='source ~/Documents/Codes/c\ lang/ubuilder/scripts/sandbox/activate.sh'
alias ub-off='source ~/Documents/Codes/c\ lang/ubuilder/scripts/sandbox/deactivate.sh'
```

## What gets redirected

| Variable             | Points to                        |
|----------------------|----------------------------------|
| `TMPDIR` / `TMP` / `TEMP` | `.sandbox/tmp/`             |
| `NPM_CONFIG_CACHE`   | `.sandbox/npm/`                  |
| `PIP_CACHE_DIR`      | `.sandbox/pip/`                  |
| `COMPOSER_CACHE_DIR` | `.sandbox/composer/`             |
| `CARGO_HOME`         | `.sandbox/cargo/`                |
| `XDG_CACHE_HOME`     | `.sandbox/xdg-cache/`            |

`PS1` is also prefixed with `(ubuilder-sandbox)` so you can see when
you're inside the sandbox.

The previous values are saved in `_UB_OLD_*` variables and restored by
`deactivate.sh`. Variables that were unset before activation are unset
again on deactivation (not left as empty strings).

## What's NOT touched

- The Claude Code permission sandbox (`.claude/settings*.json`).
- The ubuilder build's hermeticity guarantees (per the Apple-sandbox
  rule, this only adds isolation; it never decreases it).
- Anything outside this shell session.

## Cleanup

The `.sandbox/` directory is gitignored. To reclaim space:

```sh
rm -rf .sandbox
```
