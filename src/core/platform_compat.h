#ifndef UBUILDER_PLATFORM_COMPAT_H
#define UBUILDER_PLATFORM_COMPAT_H

/*
 * Platform-compatibility shim. Hand-rolled today; the full S4 surface
 * (pc_temp_root, pc_mkdir_p, pc_executable_path, etc.) is planned. This
 * header only declares the pieces needed by S1 (audit §4.1): replacing
 * system() with structured spawn + a recursive remove that doesn't need
 * /bin/sh or `rm`/`rmdir.exe`.
 */

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

#ifdef __cplusplus
}
#endif

#endif
