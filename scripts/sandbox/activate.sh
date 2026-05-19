#!/usr/bin/env bash
# Project-local sandbox: redirect TMPDIR and package-manager caches into
# ./.sandbox/ so a full system /tmp can't break the shell or the build.
#
# Usage:  source scripts/sandbox/activate.sh      (NOT ./activate.sh)
# Undo:   source scripts/sandbox/deactivate.sh

if [ "${BASH_SOURCE[0]}" = "$0" ] && [ -z "$ZSH_EVAL_CONTEXT" ]; then
	echo "activate.sh must be sourced, not executed:  source scripts/sandbox/activate.sh" >&2
	exit 1
fi

if [ -n "$UBUILDER_SANDBOX_ACTIVE" ]; then
	echo "ubuilder sandbox already active at: $UBUILDER_SANDBOX_ROOT"
	return 0 2>/dev/null || true
fi

_ub_repo_root=$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}" )/../.." && pwd )

export UBUILDER_SANDBOX_ROOT="$_ub_repo_root/.sandbox"
export UBUILDER_SANDBOX_ACTIVE=1

mkdir -p \
	"$UBUILDER_SANDBOX_ROOT/tmp" \
	"$UBUILDER_SANDBOX_ROOT/npm" \
	"$UBUILDER_SANDBOX_ROOT/pip" \
	"$UBUILDER_SANDBOX_ROOT/composer" \
	"$UBUILDER_SANDBOX_ROOT/cargo" \
	"$UBUILDER_SANDBOX_ROOT/xdg-cache"

export _UB_OLD_TMPDIR="${TMPDIR-__UNSET__}"
export _UB_OLD_TMP="${TMP-__UNSET__}"
export _UB_OLD_TEMP="${TEMP-__UNSET__}"
export _UB_OLD_NPM_CONFIG_CACHE="${NPM_CONFIG_CACHE-__UNSET__}"
export _UB_OLD_PIP_CACHE_DIR="${PIP_CACHE_DIR-__UNSET__}"
export _UB_OLD_COMPOSER_CACHE_DIR="${COMPOSER_CACHE_DIR-__UNSET__}"
export _UB_OLD_CARGO_HOME="${CARGO_HOME-__UNSET__}"
export _UB_OLD_XDG_CACHE_HOME="${XDG_CACHE_HOME-__UNSET__}"
export _UB_OLD_PS1="${PS1-__UNSET__}"

export TMPDIR="$UBUILDER_SANDBOX_ROOT/tmp"
export TMP="$TMPDIR"
export TEMP="$TMPDIR"
export NPM_CONFIG_CACHE="$UBUILDER_SANDBOX_ROOT/npm"
export PIP_CACHE_DIR="$UBUILDER_SANDBOX_ROOT/pip"
export COMPOSER_CACHE_DIR="$UBUILDER_SANDBOX_ROOT/composer"
export CARGO_HOME="$UBUILDER_SANDBOX_ROOT/cargo"
export XDG_CACHE_HOME="$UBUILDER_SANDBOX_ROOT/xdg-cache"

if [ -n "$PS1" ]; then
	PS1="(ubuilder-sandbox) $PS1"
fi

unset _ub_repo_root

echo "ubuilder sandbox active. TMPDIR=$TMPDIR"
echo "deactivate with:  source scripts/sandbox/deactivate.sh"
