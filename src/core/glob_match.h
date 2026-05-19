#ifndef UBUILDER_GLOB_MATCH_H
#define UBUILDER_GLOB_MATCH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lightweight path-glob matcher used by the ubuilder.json/--exclude feature.
 *
 * Semantics (intentionally a small subset of git's wildmatch):
 *   *      — matches any run of characters within a single path segment;
 *            does NOT cross a path separator ('/').
 *   **     — matches any run of characters, including separators. Useful
 *            as a recursive segment (e.g. tests followed by slash-star-star)
 *            or in patterns that match a file at any depth.
 *   ?      — matches exactly one character (not a separator).
 *   [...]  — bracket expression: matches one character. Supports ranges
 *            (`[a-z]`) and a leading `!` for negation (`[!abc]`).
 *   /…     — leading slash means "match from the project root". Without
 *            a leading slash, the pattern matches at any depth (i.e.
 *            equivalent to a leading `** /`).
 *   trailing `/` — the pattern matches only directory paths. Caller should
 *            pass `is_dir=1` for directory candidates.
 *
 * Both pattern and path use '/' as the separator. Backslashes in `path`
 * are normalized to '/' inside the matcher so Windows callers work too.
 *
 * Return value:
 *   1 — pattern matches path
 *   0 — does not match
 */
int ub_glob_match(const char* pattern, const char* path, int is_dir);

/* Return non-zero if any pattern in `patterns[0..n)` matches `path`.
 * Empty list (`patterns==NULL` or `n==0`) returns 0. */
int ub_path_excluded(const char* const* patterns, size_t n,
                     const char* path, int is_dir);

/* Return non-zero if any pattern in `patterns` excludes the PHP extension
 * `ext_name`. Three forms match:
 *   - "ext-<name>"   (composer-style)
 *   - "<name>"       (bare)
 *   - any glob pattern matching the bare name (e.g. "*"). */
int ub_ext_excluded(const char* const* patterns, size_t n, const char* ext_name);

#ifdef __cplusplus
}
#endif

#endif /* UBUILDER_GLOB_MATCH_H */
