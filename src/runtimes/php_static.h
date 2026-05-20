#ifndef UBUILDER_PHP_STATIC_H
#define UBUILDER_PHP_STATIC_H

#include "../core/ubuilder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve a curated static-php-cli binary for the current host
 * platform. Downloads from ubuilder's GitHub releases (tag pattern
 * `static-php-v<minor>.0`) on first use; subsequent calls hit the
 * cache under $XDG_CACHE_HOME/ubuilder/runtimes/php/<minor>-<target>/.
 *
 *   php_minor   — "8.4" (caller normalizes; we don't accept patch versions).
 *   verbose     — 1 to print "Downloading…", "SHA256 OK", etc.
 *   out_php_bin — set to a heap-allocated absolute path on success;
 *                 caller frees with free().
 *
 * Returns UB_SUCCESS on cache hit or successful download+verify.
 * On failure returns UB_ERROR_FILE_NOT_FOUND (download failure),
 * UB_ERROR_RUNTIME_NOT_FOUND (no asset for this OS/arch),
 * UB_ERROR_EXTRACTION_FAILED (tar/checksum mismatch),
 * UB_ERROR_INVALID_ARGS (caller passed nonsense).
 *
 * The function never falls back to host PHP — it's the caller's job
 * to decide what to do on failure (e.g. error out vs. try host).
 */
ub_result_t ub_php_static_resolve(const char* php_minor,
                                  int         verbose,
                                  char**      out_php_bin);

#ifdef __cplusplus
}
#endif

#endif
