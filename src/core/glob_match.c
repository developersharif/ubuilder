#include "glob_match.h"

#include <stdlib.h>
#include <string.h>

/* ----- bracket-expression matching ---------------------------------- */
/* Match `c` against a bracket expression starting at `bra[0]=='['`.
 * Sets `*pat_advance` to the number of pattern characters consumed
 * (including the closing ']'). Returns 1 on match, 0 otherwise.
 * If the bracket expression is malformed (no closing ']'), returns -1.
 */
static int match_bracket(char c, const char* bra, size_t* pat_advance) {
    /* bra[0] == '[' */
    const char* p = bra + 1;
    int negate = 0;
    if (*p == '!' || *p == '^') { negate = 1; p++; }

    int found = 0;
    /* Allow a literal ']' as the first char (POSIX bracket-expression rule). */
    int first = 1;
    while (*p && (*p != ']' || first)) {
        char lo = *p;
        if (p[1] == '-' && p[2] && p[2] != ']') {
            char hi = p[2];
            if (c >= lo && c <= hi) found = 1;
            p += 3;
        } else {
            if (c == lo) found = 1;
            p++;
        }
        first = 0;
    }
    if (*p != ']') return -1;          /* unterminated */
    *pat_advance = (size_t)(p - bra) + 1;
    return negate ? !found : found;
}

/* ----- core matcher ------------------------------------------------- */
/* Returns 1 if pattern matches the whole path, 0 otherwise.
 * Path separators in the path are normalized to '/'. */
static int match_here(const char* pat, const char* path) {
    while (*pat) {
        if (*pat == '*') {
            int double_star = (pat[1] == '*');
            const char* rest = pat + (double_star ? 2 : 1);

            /* Optional trailing slash after `**` (i.e. `**` followed by `/`)
             * collapses to plain `**` — it just means "match across
             * separators", and the slash should not have to appear in the
             * path for the match to proceed. */
            if (double_star && *rest == '/') rest++;

            /* `*` (single): match anything except '/'.
             * `**` (double): match anything including '/'.
             * Try every possible split point. */
            for (const char* p = path; ; p++) {
                if (match_here(rest, p)) return 1;
                if (!*p) return 0;
                if (!double_star && *p == '/') return 0;
            }
        }

        if (*pat == '?') {
            if (!*path || *path == '/') return 0;
            pat++; path++;
            continue;
        }

        if (*pat == '[') {
            size_t adv = 0;
            int r = match_bracket(*path, pat, &adv);
            if (r != 1) return 0;
            pat += adv; path++;
            continue;
        }

        /* Escape: backslash quotes the next pattern char as literal. */
        if (*pat == '\\' && pat[1]) {
            if (pat[1] != *path) return 0;
            pat += 2; path++;
            continue;
        }

        /* Literal */
        if (*pat != *path) return 0;
        pat++; path++;
    }
    return *path == 0;
}

/* Skip a leading "./" or "/" in the normalized path. Returns the new start. */
static const char* skip_leading_slash(const char* p) {
    if (p[0] == '.' && p[1] == '/') return p + 2;
    if (p[0] == '/') return p + 1;
    return p;
}

int ub_glob_match(const char* pattern, const char* path, int is_dir) {
    if (!pattern || !path) return 0;

    /* Normalize backslashes to forward slashes in a local copy of path. */
    char buf[2048];
    size_t plen = strlen(path);
    if (plen >= sizeof(buf)) return 0;
    for (size_t i = 0; i <= plen; i++) {
        char c = path[i];
        buf[i] = (c == '\\') ? '/' : c;
    }
    const char* p = skip_leading_slash(buf);

    /* Trailing slash on pattern: directory-only. */
    size_t patlen = strlen(pattern);
    int dir_only = (patlen > 0 && pattern[patlen - 1] == '/');
    char patbuf[2048];
    if (patlen >= sizeof(patbuf)) return 0;
    memcpy(patbuf, pattern, patlen + 1);
    if (dir_only) {
        if (!is_dir) return 0;
        patbuf[patlen - 1] = 0;
    }

    /* Anchored vs floating: leading '/' anchors to project root; otherwise
     * the pattern is allowed to match at any depth. */
    const char* pat = patbuf;
    int anchored = (*pat == '/');
    if (anchored) pat++;

    if (anchored) return match_here(pat, p);

    /* Floating: try matching at each segment boundary. */
    const char* cursor = p;
    while (1) {
        if (match_here(pat, cursor)) return 1;
        /* Advance to the next segment. */
        while (*cursor && *cursor != '/') cursor++;
        if (!*cursor) return 0;
        cursor++;                          /* skip the '/' */
        if (!*cursor) return 0;
    }
}

int ub_path_excluded(const char* const* patterns, size_t n,
                     const char* path, int is_dir) {
    if (!patterns || n == 0 || !path) return 0;
    for (size_t i = 0; i < n; i++) {
        if (patterns[i] && ub_glob_match(patterns[i], path, is_dir)) return 1;
    }
    return 0;
}

int ub_ext_excluded(const char* const* patterns, size_t n, const char* ext_name) {
    if (!patterns || n == 0 || !ext_name) return 0;
    for (size_t i = 0; i < n; i++) {
        const char* p = patterns[i];
        if (!p) continue;
        /* "ext-<X>": strip the prefix, then either exact-match X against
         * ext_name or treat X as a glob. This lets users write either
         * "ext-curl" or "ext-mysql*". */
        if (strncmp(p, "ext-", 4) == 0) {
            const char* suffix = p + 4;
            if (strcmp(suffix, ext_name) == 0) return 1;
            if (ub_glob_match(suffix, ext_name, 0)) return 1;
            continue;
        }
        /* Bare "<name>" — exact match, then glob fallback. */
        if (strcmp(p, ext_name) == 0) return 1;
        if (ub_glob_match(p, ext_name, 0)) return 1;
    }
    return 0;
}
