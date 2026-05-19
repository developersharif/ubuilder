#!/usr/bin/env bash
# Undo scripts/sandbox/activate.sh — restore the pre-sandbox environment.
#
# Usage:  source scripts/sandbox/deactivate.sh

if [ "${BASH_SOURCE[0]}" = "$0" ] && [ -z "$ZSH_EVAL_CONTEXT" ]; then
	echo "deactivate.sh must be sourced, not executed:  source scripts/sandbox/deactivate.sh" >&2
	exit 1
fi

if [ -z "$UBUILDER_SANDBOX_ACTIVE" ]; then
	echo "ubuilder sandbox is not active; nothing to do."
	return 0 2>/dev/null || true
fi

_ub_restore() {
	local var="$1" old="$2"
	if [ "$old" = "__UNSET__" ]; then
		unset "$var"
	else
		export "$var=$old"
	fi
}

_ub_restore TMPDIR              "$_UB_OLD_TMPDIR"
_ub_restore TMP                 "$_UB_OLD_TMP"
_ub_restore TEMP                "$_UB_OLD_TEMP"
_ub_restore NPM_CONFIG_CACHE    "$_UB_OLD_NPM_CONFIG_CACHE"
_ub_restore PIP_CACHE_DIR       "$_UB_OLD_PIP_CACHE_DIR"
_ub_restore COMPOSER_CACHE_DIR  "$_UB_OLD_COMPOSER_CACHE_DIR"
_ub_restore CARGO_HOME          "$_UB_OLD_CARGO_HOME"
_ub_restore XDG_CACHE_HOME      "$_UB_OLD_XDG_CACHE_HOME"
_ub_restore PS1                 "$_UB_OLD_PS1"

unset _UB_OLD_TMPDIR _UB_OLD_TMP _UB_OLD_TEMP \
	_UB_OLD_NPM_CONFIG_CACHE _UB_OLD_PIP_CACHE_DIR _UB_OLD_COMPOSER_CACHE_DIR \
	_UB_OLD_CARGO_HOME _UB_OLD_XDG_CACHE_HOME _UB_OLD_PS1 \
	UBUILDER_SANDBOX_ROOT UBUILDER_SANDBOX_ACTIVE

unset -f _ub_restore

echo "ubuilder sandbox deactivated."
