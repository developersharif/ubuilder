#ifndef UBUILDER_PLATFORM_COMPAT_H
#define UBUILDER_PLATFORM_COMPAT_H

/*
 * Platform-compatibility shim. The Apple-sandbox rule (see
 * docs/architecture/ARCHITECTURE_AUDIT.md) is binding: every shell-out and
 * popen() in UBuilder must route through this shim instead. After S1+S4+S5,
 * the runtime *and* build paths are system()/popen()/`/bin/sh`-free.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Recursively remove `path` (file or directory). Symbolic links are not
 * followed (their target is preserved). Returns 0 on success, -1 on failure
 * (errno set on POSIX; GetLastError-equivalent ignored on Windows).
 */
int pc_remove_tree(const char* path);

/*
 * Spawn `exe` and wait for it. Arguments:
 *   exe   — absolute or PATH-resolvable program; passed verbatim to the
 *           OS, NOT through a shell. No quoting/escaping needed.
 *   argv  — NUL-terminated argv array; argv[0] is the conventional program
 *           name and argv[argc] must be NULL. Passed verbatim (no
 *           re-quoting; no length cap).
 *   envp  — NUL-terminated "KEY=VALUE" array, or NULL to inherit the
 *           current process environment.
 *   cwd   — directory to chdir() the child into before exec, or NULL to
 *           use the parent's current working directory.
 *
 * Return value:
 *   >= 0     normal exit: the child's exit status (0..255 on POSIX).
 *   128 + N  killed by signal N (POSIX only).
 *   -1       could not spawn (e.g. exe missing). errno is set.
 */
int pc_spawn_and_wait(const char* exe,
                      char* const argv[],
                      char* const envp[],
                      const char* cwd);

/*
 * Build a new environment array by overlaying `extra` on the current
 * process environment. `extra` is a NUL-terminated array of "KEY=VALUE"
 * strings; keys already present in environ are replaced, new keys are
 * appended. Returns NULL on allocation failure.
 *
 * The returned block must be released with pc_env_free. The strings inside
 * are owned by the returned block.
 */
char** pc_env_overlay(char* const extra[]);
void   pc_env_free(char** env);

/*
 * Recursively create the directory tree `path` (like `mkdir -p`). It is
 * not an error if the path already exists as a directory. Returns 0 on
 * success, -1 on failure (errno set on POSIX). Used to replace
 * `system("mkdir …")` shell-outs and inherit the Apple-sandbox rule.
 */
int pc_mkdir_p(const char* path);

/*
 * Search the current process's PATH for an executable named `exe`. On
 * POSIX this iterates $PATH split on ':' and tests access(p, X_OK). On
 * Windows it iterates %PATH% split on ';' and tries common extensions
 * (.exe / .cmd / .bat).
 *
 * Returns a heap-allocated absolute path (caller frees), or NULL if the
 * binary is not on PATH or not executable. `exe` may also be an absolute
 * path, in which case the function only checks that it exists.
 *
 * This replaces the popen("which X") / popen("where X") idiom.
 */
char* pc_path_lookup(const char* exe);

/*
 * Spawn `exe` with `argv`/`envp`/`cwd` like pc_spawn_and_wait, but capture
 * the child's stdout into a heap-allocated, NUL-terminated buffer up to
 * `max_bytes`. A trailing newline is stripped. Caller frees `*out` with
 * free(). Returns the child's exit status (>= 0), or -1 on spawn / I/O
 * failure with *out set to NULL. stderr is inherited.
 */
int pc_spawn_capture(const char* exe,
                     char* const argv[],
                     char* const envp[],
                     const char* cwd,
                     size_t      max_bytes,
                     char**      out);

/*
 * Return the conventional temporary-directory root for this OS:
 *   POSIX  : $TMPDIR or $TMP if set, otherwise "/tmp".
 *   Windows: %TEMP% or %TMP% if set, otherwise "C:\\Temp".
 * The returned pointer references either an environment string or a
 * static fallback; do not free.
 */
const char* pc_temp_root(void);

/*
 * Write the absolute path to the currently-running executable into `out`
 * (NUL-terminated, truncated to `out_cap-1` if necessary). Returns 0 on
 * success, -1 on failure. Backs `/proc/self/exe` on Linux,
 * `_NSGetExecutablePath` on macOS, `GetModuleFileNameA` on Windows.
 */
int pc_executable_path(char* out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
