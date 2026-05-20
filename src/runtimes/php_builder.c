#include "runtime_builder.h"
#include "runtime_embedder.h"
#include "php_static.h"
#include "../core/platform_compat.h"
#include "../core/json_mini.h"
#include "../core/glob_match.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
    #include <io.h>
    #define PATH_MAX MAX_PATH
    #define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
    #define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#else
    #include <dirent.h>
    #include <unistd.h>
#endif

// Forward declarations
#ifdef PLATFORM_WINDOWS
static ub_result_t php_embed_windows_runtime(const char* php_dir, FILE* output_file);
#endif

#ifndef PLATFORM_WINDOWS
/* ============================================================
 * M1-D (PHP hermetic-with-host-bits)
 *
 * On POSIX the PHP runtime is too sprawling to vendor as a prebuilt
 * (no upstream pre-built static PHP exists; static-php-cli is a self-
 * build path). Instead we bundle:
 *   - host /usr/bin/php   → bin/php
 *   - extensions enumerated in composer.json's `require.ext-*`, copied
 *     from the host's extension_dir → ext/<name>.so
 *   - a generated php.ini with sensible defaults + extension lines
 *
 * Caveat (documented in M1_PHP.md): bundle still depends on system
 * shared libs (libgd, libxml2, libcurl, ...) being present on the
 * target. Truly hermetic ldd-bundling is future work. For CLI / web /
 * GUI-via-FFI use cases on Debian-family targets this is shippable
 * today.
 *
 * Composer integration (mirrors M8 / M8-B): if composer.json is
 * present, run `composer install --no-dev` against a staged copy of
 * the project; vendor/ ships in the bundle. Honors the install-cache
 * with composer.lock as a key input.
 * ============================================================ */

/* List of extension names (without `.so`) parsed from composer.json. */
typedef struct {
    char** names;
    size_t count;
} php_ext_list_t;

static void php_ext_list_free(php_ext_list_t* l) {
    if (!l) return;
    for (size_t i = 0; i < l->count; i++) free(l->names[i]);
    free(l->names);
    l->names = NULL;
    l->count = 0;
}

/* Parse `<project>/composer.json` for `require` keys starting with
 * "ext-". Caller frees with php_ext_list_free. Returns 0 on success
 * (including "file absent" → empty list); -1 only on malformed JSON. */
static int php_parse_composer_extensions(const char* project_dir, php_ext_list_t* out) {
    out->names = NULL;
    out->count = 0;

    char path[1024];
    snprintf(path, sizeof(path), "%s/composer.json", project_dir);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;  /* No composer.json → no extensions, not an error. */
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n < 0 || n > 16 * 1024 * 1024) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    if ((long)fread(buf, 1, (size_t)n, f) != n) { free(buf); fclose(f); return -1; }
    buf[n] = 0;
    fclose(f);

    char err[256];
    json_value_t* root = json_parse(buf, (size_t)n, err, sizeof(err));
    free(buf);
    if (!root) {
        fprintf(stderr, "Error: composer.json parse failed: %s\n", err);
        return -1;
    }

    const json_value_t* req = json_obj_get(root, "require");
    if (!req || req->type != JSON_OBJECT) { json_free(root); return 0; }

    /* First pass: count. */
    size_t ext_count = 0;
    for (size_t i = 0; i < req->v.obj.count; i++) {
        const char* k = req->v.obj.pairs[i].key;
        if (strncmp(k, "ext-", 4) == 0) ext_count++;
    }
    if (ext_count == 0) { json_free(root); return 0; }

    out->names = (char**)calloc(ext_count, sizeof(char*));
    if (!out->names) { json_free(root); return -1; }

    for (size_t i = 0; i < req->v.obj.count; i++) {
        const char* k = req->v.obj.pairs[i].key;
        if (strncmp(k, "ext-", 4) != 0) continue;
        const char* name = k + 4;
        char* dup = strdup(name);
        if (!dup) { php_ext_list_free(out); json_free(root); return -1; }
        out->names[out->count++] = dup;
    }
    json_free(root);
    return 0;
}

/* Load-order priority for `name` — lower numbers load first. PHP requires
 * certain extensions to be loaded BEFORE the ones that depend on them
 * (pdo before pdo_mysql, mysqlnd before mysqli, etc.) and emits opaque
 * `undefined symbol` startup warnings otherwise. Debian's conf.d uses a
 * filename-prefix priority scheme (10-mysqlnd.ini before 20-mysqli.ini);
 * we replicate the same ordering inside our single generated ini. The
 * default priority (50) covers everything not explicitly ranked. */
static int php_extension_load_priority(const char* name) {
    /* Tier 10 — foundational, must come before their dependents. */
    if (strcmp(name, "pdo") == 0)        return 10;
    if (strcmp(name, "mysqlnd") == 0)    return 10;
    if (strcmp(name, "xml") == 0)        return 10;
    if (strcmp(name, "json") == 0)       return 10;
    if (strcmp(name, "ctype") == 0)      return 10;
    if (strcmp(name, "iconv") == 0)      return 10;
    if (strcmp(name, "phar") == 0)       return 10;
    if (strcmp(name, "tokenizer") == 0)  return 10;
    if (strcmp(name, "session") == 0)    return 10;

    /* Tier 20 — first-degree dependents (pdo_*, mysqli, dom-family). */
    if (strcmp(name, "mysqli") == 0)     return 20;
    if (strcmp(name, "pdo_mysql") == 0)  return 20;
    if (strcmp(name, "pdo_pgsql") == 0)  return 20;
    if (strcmp(name, "pdo_sqlite") == 0) return 20;
    if (strcmp(name, "pdo_odbc") == 0)   return 20;
    if (strcmp(name, "pdo_dblib") == 0)  return 20;
    if (strcmp(name, "pdo_firebird") == 0) return 20;
    if (strcmp(name, "dom") == 0)        return 20;
    if (strcmp(name, "simplexml") == 0)  return 20;
    if (strcmp(name, "xmlreader") == 0)  return 20;
    if (strcmp(name, "xmlwriter") == 0)  return 20;
    if (strcmp(name, "wddx") == 0)       return 20;

    /* Tier 30 — second-degree (depends on tier-20 like dom). */
    if (strcmp(name, "xsl") == 0)        return 30;
    if (strcmp(name, "soap") == 0)       return 30;

    return 50;
}

/* Return non-zero if `name` is a Zend extension (loaded via zend_extension=)
 * rather than a regular extension (extension=). Zend extensions hook into
 * the engine differently and PHP throws a warning if you load them with
 * the wrong directive. The list below covers everything we've seen on
 * Debian/RHEL/Arch in 2025-2026. */
static int php_is_zend_extension(const char* name) {
    static const char* zend_exts[] = {
        "opcache", "xdebug", "tideways", "tideways_xhprof",
        "ioncube_loader", "ioncube_loader_lin", "blackfire",
        "datadog-profiling", "ddtrace", "newrelic", "snuffleupagus",
        NULL
    };
    for (size_t i = 0; zend_exts[i]; i++) {
        if (strcmp(name, zend_exts[i]) == 0) return 1;
    }
    return 0;
}

/* Scan the host's extension_dir and return every <name>.so found, with the
 * `.so` suffix stripped. Output is alphabetized for deterministic builds.
 * Caller frees with php_ext_list_free. Returns 0 on success, -1 on I/O. */
static int php_list_host_extensions(const char* ext_dir, php_ext_list_t* out) {
    out->names = NULL;
    out->count = 0;

    DIR* dir = opendir(ext_dir);
    if (!dir) return -1;

    /* Two-pass: count first to size the allocation, then fill. */
    size_t cap = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        const char* dot = strrchr(name, '.');
        if (dot && strcmp(dot, ".so") == 0 && dot != name) cap++;
    }
    rewinddir(dir);

    if (cap == 0) { closedir(dir); return 0; }
    out->names = (char**)calloc(cap, sizeof(char*));
    if (!out->names) { closedir(dir); return -1; }

    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        const char* dot = strrchr(name, '.');
        if (!dot || strcmp(dot, ".so") != 0 || dot == name) continue;
        size_t n = (size_t)(dot - name);
        char* dup = (char*)malloc(n + 1);
        if (!dup) { php_ext_list_free(out); closedir(dir); return -1; }
        memcpy(dup, name, n);
        dup[n] = 0;
        out->names[out->count++] = dup;
    }
    closedir(dir);

    /* Sort alphabetically so the generated ini and embedded files have a
     * stable order across machines/builds (helps with content-addressed
     * caching downstream). qsort with a pointer-to-string comparator. */
    if (out->count > 1) {
        /* Simple insertion sort — count is small (~50 typical). */
        for (size_t i = 1; i < out->count; i++) {
            char* key = out->names[i];
            size_t j = i;
            while (j > 0 && strcmp(out->names[j - 1], key) > 0) {
                out->names[j] = out->names[j - 1];
                j--;
            }
            out->names[j] = key;
        }
    }
    return 0;
}

/* Probe `<php_bin> --ini` and extract the two paths we need from its output:
 *   Loaded Configuration File:         /etc/php/8.4/cli/php.ini
 *   Scan for additional .ini files in: /etc/php/8.4/cli/conf.d
 * Either field may be "(none)" — we leave the corresponding out-buffer
 * empty in that case. Returns 0 always (best-effort; missing values are
 * a soft condition, handled by the caller). */
static int php_probe_ini_paths(const char* php_bin,
                               char* main_ini, size_t main_cap,
                               char* scan_dir, size_t scan_cap) {
    if (main_cap) main_ini[0] = 0;
    if (scan_cap) scan_dir[0] = 0;

    /* PHP 8.5 rejects ANY other args alongside `--ini` with
     * "Unknown argument for --ini" — so we cannot pre-set `-d
     * display_errors=stderr` here to silence startup noise. The labels
     * we scan for ("Loaded Configuration File:", "Scan for additional
     * .ini files in:") are distinctive enough that interleaved PHP
     * warnings on stdout still get ignored by the parser; the 16 KiB
     * capture comfortably accommodates a few KB of warning text. */
    char* argv[] = {
        (char*)php_bin,
        (char*)"--ini",
        NULL
    };
    char* captured = NULL;
    if (pc_spawn_capture(php_bin, argv, NULL, NULL, 16384, &captured) != 0 || !captured) {
        free(captured);
        return 0;
    }

    /* Tokenize on newlines and look for the two leading labels. */
    char* save = NULL;
    for (char* line = strtok_r(captured, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        const char* labels[2] = {
            "Loaded Configuration File:",
            "Scan for additional .ini files in:"
        };
        char* outs[2] = { main_ini, scan_dir };
        size_t caps[2] = { main_cap, scan_cap };

        for (int i = 0; i < 2; i++) {
            char* hit = strstr(line, labels[i]);
            if (!hit || outs[i][0]) continue;   /* only first hit wins */
            char* p = hit + strlen(labels[i]);
            while (*p == ' ' || *p == '\t') p++;
            char* end = p + strlen(p);
            while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
            *end = 0;
            /* PHP 8.5 wraps these paths in double quotes (e.g.
             * `"/opt/homebrew/etc/php/8.5/php.ini"`). Strip a surrounding
             * pair so the path round-trips into fopen() / opendir(). */
            if (end > p + 1 && *p == '"' && end[-1] == '"') {
                end[-1] = 0;
                p++;
            }
            if (strcmp(p, "(none)") != 0 && strlen(p) < caps[i] && caps[i] > 0) {
                memcpy(outs[i], p, strlen(p) + 1);
            }
        }
    }
    free(captured);
    return 0;
}

/* Copy every <name>.ini from src_dir into dst_dir, FOLLOWING symlinks
 * (Debian's conf.d entries are symlinks into mods-available/). Skip files
 * whose stripped extension name matches the user's exclude list — keeps
 * us from loading an extension the user just dropped. Returns the count
 * of files copied; -1 on a hard error. */
static int php_copy_host_confd(const char* src_dir, const char* dst_dir,
                               char* const* exclude_pats, size_t exclude_count) {
    if (pc_mkdir_p(dst_dir) != 0) return -1;
    DIR* d = opendir(src_dir);
    if (!d) return 0;   /* host has no conf.d — fine, nothing to copy. */

    int copied = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        const char* name = e->d_name;
        size_t name_len = strlen(name);
        if (name_len < 4 || strcmp(name + name_len - 4, ".ini") != 0) continue;
        if (name[0] == '.') continue;

        /* Derive the bare extension name by stripping a leading "<digits>-"
         * priority prefix and the trailing ".ini". Use it to honor the
         * --exclude list (so we don't keep a conf.d that loads an ext we
         * just dropped). E.g. "20-mbstring.ini" → "mbstring". */
        const char* ext_start = name;
        while (*ext_start >= '0' && *ext_start <= '9') ext_start++;
        if (*ext_start == '-') ext_start++;
        size_t ext_len = (name_len - 4) - (size_t)(ext_start - name);
        if (ext_len > 0 && ext_len < 128 && exclude_pats && exclude_count > 0) {
            char ext_name[128];
            memcpy(ext_name, ext_start, ext_len);
            ext_name[ext_len] = 0;
            if (ub_ext_excluded((const char* const*)exclude_pats, exclude_count, ext_name)) {
                continue;
            }
        }

        char src[1280], dst[1280];
        snprintf(src, sizeof(src), "%s/%s", src_dir, name);
        snprintf(dst, sizeof(dst), "%s/%s", dst_dir, name);

        FILE* fi = fopen(src, "rb");   /* fopen follows symlinks */
        if (!fi) continue;
        FILE* fo = fopen(dst, "wb");
        if (!fo) { fclose(fi); continue; }
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) {
            if (fwrite(buf, 1, n, fo) != n) break;
        }
        fclose(fi);
        fclose(fo);
        copied++;
    }
    closedir(d);
    return copied;
}

/* Probe `<php_bin> -m` and return the list of loaded modules (lowercased,
 * one per non-empty non-`[`-prefixed line). Used when host PHP is
 * statically linked: there is no `extension_dir` to scan, but `php -m`
 * still reports every built-in module so we can cross-check composer's
 * `require.ext-*` against what the binary actually provides. Caller frees
 * with php_ext_list_free. Returns 0 on success, -1 on probe failure. */
static int php_list_loaded_modules(const char* php_bin, php_ext_list_t* out) {
    out->names = NULL;
    out->count = 0;

    /* Silence display_errors → stdout duplication so any stale
     * extension load failure (Herd uninstalled, etc.) doesn't pollute
     * the captured module list with warning text. */
    char* argv[] = {
        (char*)php_bin,
        (char*)"-d", (char*)"display_errors=stderr",
        (char*)"-d", (char*)"display_startup_errors=Off",
        (char*)"-m",
        NULL
    };
    char* captured = NULL;
    if (pc_spawn_capture(php_bin, argv, NULL, NULL, 16384, &captured) != 0 || !captured) {
        free(captured);
        return -1;
    }

    /* First pass to count, second to fill. `php -m` emits sections like
     * `[PHP Modules]` and `[Zend Modules]` plus module names — we keep
     * non-bracketed non-empty lines and lowercase them so the
     * cross-check against composer's lowercase `ext-<name>` works. */
    size_t cap = 0;
    for (char* p = captured; *p; ) {
        char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > 0 && p[0] != '[') cap++;
        if (!nl) break;
        p = nl + 1;
    }
    if (cap == 0) { free(captured); return 0; }
    out->names = (char**)calloc(cap, sizeof(char*));
    if (!out->names) { free(captured); return -1; }

    for (char* p = captured; *p; ) {
        char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        while (len > 0 && (p[len-1] == '\r' || p[len-1] == ' ' || p[len-1] == '\t')) len--;
        if (len > 0 && p[0] != '[') {
            char* dup = (char*)malloc(len + 1);
            if (!dup) { php_ext_list_free(out); free(captured); return -1; }
            for (size_t i = 0; i < len; i++) {
                char c = p[i];
                dup[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
            }
            dup[len] = 0;
            out->names[out->count++] = dup;
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(captured);
    return 0;
}

/* Probe `<php_bin> -r 'echo ini_get("extension_dir");'`. Returns 0 / -1.
 *
 * NOTE 1: do NOT pass `-d extension_dir=` to clear the value first —
 * that would return PHP's compile-time default, which on Homebrew points
 * at `/opt/homebrew/Cellar/php/<ver>/lib/php/<api>/` (a path that doesn't
 * exist on disk; Homebrew's pecl extensions actually live at
 * `/opt/homebrew/lib/php/pecl/<api>/`, configured via the host php.ini).
 * Asking PHP without the override returns the runtime-effective value
 * the host's php.ini actually loads from.
 *
 * NOTE 2: `-d display_errors=stderr -d display_startup_errors=Off`
 * silences PHP's "PHP Warning: Unable to load dynamic library ..."
 * duplicate that otherwise lands on STDOUT (CLI default duplicates
 * startup errors to display_errors target). Without this, a host with
 * a stale extension reference (e.g. Laravel Herd uninstalled but
 * Homebrew PHP's conf.d still loading `/Applications/Herd.app/...`)
 * floods stdout with kilobytes of warning text, blowing past our
 * fixed-size capture buffer and making the probe return -1. */
static int php_probe_extension_dir(const char* php_bin, char* out, size_t out_cap) {
    char* argv[] = {
        (char*)php_bin,
        (char*)"-d", (char*)"display_errors=stderr",
        (char*)"-d", (char*)"display_startup_errors=Off",
        (char*)"-r", (char*)"echo ini_get(\"extension_dir\");",
        NULL
    };
    char* captured = NULL;
    int rc = pc_spawn_capture(php_bin, argv, NULL, NULL, 4096, &captured);
    if (rc != 0 || !captured) { free(captured); return -1; }
    size_t len = strlen(captured);
    if (len == 0 || len >= out_cap) { free(captured); return -1; }
    memcpy(out, captured, len + 1);
    free(captured);
    return 0;
}

/* Write a tiny ubuilder-overrides snippet into conf.d/. This file is
 * loaded LAST by PHP (high-priority suffix) so its values take precedence
 * over whatever the host's main php.ini and conf.d/* set. Keep it small:
 * only the settings we need to FORCE for bundled-CLI semantics. Settings
 * the host already had (memory_limit, error_reporting, timezone, etc.)
 * stay as the user configured them in the host php.ini we copied. */
static int php_write_default_ini(const char* ini_path,
                                 const php_ext_list_t* exts) {
    (void)exts;   /* The extension list is now driven by conf.d/<prio>-<ext>.ini
                     files copied from the host; we no longer enumerate it here. */
    FILE* f = fopen(ini_path, "w");
    if (!f) return -1;
    fprintf(f,
        "; ubuilder runtime overrides (loaded last so the values below win)\n"
        "; Edit/remove individual lines or pass --exclude=ext-<name> to drop\n"
        "; specific extensions; the rest of the host's php.ini is preserved\n"
        "; verbatim in the sibling conf.d/* files we copied.\n"
        "\n"
        "; FFI: PHP 8+ defaults to ffi.enable=preload, which disables FFI\n"
        "; in CLI mode unless opcache.preload is also configured. Bundles\n"
        "; run as standalone CLI processes (no preload step), so we force\n"
        "; ffi.enable=true here. Harmless when ffi.so isn't loaded.\n"
        "ffi.enable = true\n"
        "\n"
        "; OPcache: bundled files are immutable, so we skip the per-file\n"
        "; stat() on every request. Saves a few ms on startup.\n"
        "opcache.enable_cli = 1\n"
        "opcache.validate_timestamps = 0\n"
        "\n"
        "; Phar: bundles may legitimately want to load .phar archives.\n"
        "phar.readonly = 0\n");
    fclose(f);
    return 0;
}

#ifdef __APPLE__
/* ============================================================
 * macOS dyld rewiring (cross-Mac portability)
 *
 * On macOS, Mach-O binaries reference their dependencies by absolute
 * path baked into LC_LOAD_DYLIB load commands. A bundle that includes
 * Homebrew's /opt/homebrew/bin/php has refs like
 *   /opt/homebrew/opt/libxml2/lib/libxml2.2.dylib
 * which won't resolve on a clean target Mac. To make bundles portable
 * across Macs we:
 *   1. otool -L every bundled Mach-O, gather non-system dep paths
 *   2. recursively copy each into <bundle>/lib/<basename>
 *   3. install_name_tool -change every absolute non-system ref to
 *      @executable_path/../lib/<basename> (resolves at runtime against
 *      the dir of the exec'd binary, which is always <tmp>/runtime/bin/)
 *   4. install_name_tool -id on each bundled dylib so it advertises
 *      itself with the same @executable_path-relative install name
 *   5. codesign --force --sign - to ad-hoc re-sign each modified file
 *      (Apple Silicon requires a valid signature even for unsigned
 *      ad-hoc binaries; install_name_tool invalidates the existing one)
 *
 * "System" dylibs (/usr/lib/* and /System/*) are NOT bundled — they're
 * guaranteed present on every macOS install. Statically linked PHP
 * (Herd / static-php-cli) only references those system paths, so this
 * whole pass is a no-op on that path.
 * @rpath references are deliberately out of scope for v1 (would need
 * LC_RPATH parsing); they're warned and skipped — vanishingly rare in
 * the PHP/ext-* set we care about.
 * ============================================================ */

#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
extern char** environ;

/* Spawn install_name_tool (or any tool) with stderr redirected to
 * /dev/null. install_name_tool emits "changes being made to the file
 * will invalidate the code signature in: …" for every Mach-O it
 * touches — but we always `codesign --force` immediately afterwards,
 * which makes the warning misleading noise. Suppressing stderr keeps
 * the build log focused on what actually matters. Returns exit status
 * (>= 0) on success, -1 on spawn failure. */
static int mac_spawn_quiet_stderr(char* const argv[]) {
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return -1;
    if (posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                         "/dev/null", O_WRONLY, 0) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) { errno = rc; return -1; }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

/* Minimal growable string list, used as both a queue and a "seen" set. */
typedef struct {
    char** items;
    size_t count;
    size_t cap;
} mac_strs_t;

static void mac_strs_free(mac_strs_t* l) {
    if (!l) return;
    for (size_t i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = l->cap = 0;
}

static int mac_strs_push(mac_strs_t* l, const char* s) {
    if (l->count == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 16;
        char** ni = (char**)realloc(l->items, nc * sizeof(char*));
        if (!ni) return -1;
        l->items = ni;
        l->cap = nc;
    }
    char* dup = strdup(s);
    if (!dup) return -1;
    l->items[l->count++] = dup;
    return 0;
}

static int mac_strs_contains(const mac_strs_t* l, const char* s) {
    for (size_t i = 0; i < l->count; i++) {
        if (strcmp(l->items[i], s) == 0) return 1;
    }
    return 0;
}

/* Dylibs under /usr/lib/ and frameworks under /System/ are part of the
 * macOS base install — every Mac has them. Bundling them would just
 * inflate the binary; on Apple Silicon they're cached in the dyld
 * shared cache and aren't even on-disk anymore. */
static int mac_is_system_dylib(const char* path) {
    return strncmp(path, "/usr/lib/", 9) == 0
        || strncmp(path, "/System/", 8) == 0;
}

/* Parse `otool -L <mach_o>` output. Each non-header line is a dep of the
 * form "\t<path> (compatibility version X, current version Y)". Push the
 * <path> portion into `out`. For dylibs the first dep line is the file's
 * own LC_ID_DYLIB — caller is expected to deal with that (skip when
 * recursing, but include when rewriting since we want to update the
 * install name too). */
static int mac_otool_deps(const char* mach_o_path, mac_strs_t* out) {
    char* otool = pc_path_lookup("otool");
    if (!otool) {
        fprintf(stderr, "Error: otool not found on PATH (install Xcode Command Line Tools)\n");
        return -1;
    }
    char* argv[] = { otool, (char*)"-L", (char*)mach_o_path, NULL };
    char* captured = NULL;
    int rc = pc_spawn_capture(otool, argv, NULL, NULL, 65536, &captured);
    free(otool);
    if (rc != 0 || !captured) { free(captured); return -1; }

    char* save = NULL;
    int header_skipped = 0;
    for (char* line = strtok_r(captured, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        /* The first non-blank line ends with ':' and names the inspected
         * file — drop it. Subsequent dep lines begin with whitespace. */
        if (!header_skipped) {
            header_skipped = 1;
            char* end = line + strlen(line);
            while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
            if (end > line && end[-1] == ':') continue;
        }
        while (*line == '\t' || *line == ' ') line++;
        if (!*line) continue;
        char* paren = strstr(line, " (compatibility");
        if (paren) *paren = 0;
        char* end = line + strlen(line);
        while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
        *end = 0;
        if (!*line) continue;
        if (mac_strs_push(out, line) != 0) {
            free(captured);
            return -1;
        }
    }
    free(captured);
    return 0;
}

/* Parse LC_RPATH load commands from `otool -l <mach_o>` output and
 * return each rpath in load order. Caller frees with mac_strs_free.
 * Returns 0 on success, -1 on probe failure.
 *
 * Sample output we parse:
 *     Load command 18
 *           cmd LC_RPATH
 *       cmdsize 56
 *          path /opt/homebrew/opt/openssl 3/lib (offset 12)
 */
static int mac_parse_rpaths(const char* mach_o, mac_strs_t* out) {
    char* otool = pc_path_lookup("otool");
    if (!otool) return -1;
    char* argv[] = { otool, (char*)"-l", (char*)mach_o, NULL };
    char* captured = NULL;
    int rc = pc_spawn_capture(otool, argv, NULL, NULL, 1024 * 1024, &captured);
    free(otool);
    if (rc != 0 || !captured) { free(captured); return -1; }

    int in_rpath = 0;
    char* save = NULL;
    for (char* line = strtok_r(captured, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (strstr(line, "cmd LC_RPATH")) { in_rpath = 1; continue; }
        if (!in_rpath) continue;
        char* hit = strstr(line, "path ");
        if (!hit) continue;
        char* p = hit + 5;
        while (*p == ' ' || *p == '\t') p++;
        char* paren = strstr(p, " (offset");
        if (paren) *paren = 0;
        char* end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
        *end = 0;
        if (*p) mac_strs_push(out, p);
        in_rpath = 0;
    }
    free(captured);
    return 0;
}

/* Resolve @loader_path / @executable_path / @rpath references. For
 * @rpath, walks the rpaths list and returns the first substitution
 * that exists on disk. Caller frees *out. Returns 0 / -1. */
static int mac_resolve_ref(const char* referrer, const char* ref,
                           const mac_strs_t* rpaths, char** out) {
    *out = NULL;
    if (ref[0] != '@') {
        *out = strdup(ref);
        return *out ? 0 : -1;
    }

    if (strncmp(ref, "@loader_path/", 13) == 0 ||
        strncmp(ref, "@executable_path/", 17) == 0) {
        const char* tail = strchr(ref + 1, '/');
        if (!tail) return -1;
        tail++;
        char dir[1024];
        size_t rlen = strlen(referrer);
        if (rlen >= sizeof(dir)) return -1;
        memcpy(dir, referrer, rlen + 1);
        char* slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        size_t need = strlen(dir) + 1 + strlen(tail) + 1;
        *out = (char*)malloc(need);
        if (!*out) return -1;
        snprintf(*out, need, "%s/%s", dir, tail);
        return 0;
    }

    if (strncmp(ref, "@rpath/", 7) == 0) {
        if (!rpaths || rpaths->count == 0) return -1;
        const char* tail = ref + 7;
        for (size_t i = 0; i < rpaths->count; i++) {
            const char* rp = rpaths->items[i];
            /* @loader_path / @executable_path are allowed *inside*
             * LC_RPATH entries — resolve those against the referrer. */
            char resolved_rp[1024];
            if (strncmp(rp, "@loader_path/", 13) == 0 ||
                strncmp(rp, "@executable_path/", 17) == 0) {
                const char* rp_tail = strchr(rp + 1, '/');
                if (!rp_tail) continue;
                rp_tail++;
                char dir[1024];
                size_t rlen = strlen(referrer);
                if (rlen >= sizeof(dir)) continue;
                memcpy(dir, referrer, rlen + 1);
                char* slash = strrchr(dir, '/');
                if (slash) *slash = 0;
                snprintf(resolved_rp, sizeof(resolved_rp), "%s/%s", dir, rp_tail);
                rp = resolved_rp;
            }
            char cand[1280];
            snprintf(cand, sizeof(cand), "%s/%s", rp, tail);
            struct stat st;
            if (stat(cand, &st) == 0 && S_ISREG(st.st_mode)) {
                *out = strdup(cand);
                return *out ? 0 : -1;
            }
        }
        return -1;
    }

    return -1;  /* unknown @-prefix */
}
/* BFS over `mach_o`'s transitive non-system dep graph. Each newly-seen
 * dep is hardlink-or-copied into `bundle_lib_dir/<basename>`, then its
 * own deps are queued.
 *
 * IMPORTANT: the queue holds *original* (host) paths, not staged ones.
 * We otool the original because that's where `@loader_path` references
 * resolve correctly — `@loader_path/libicudata.78.dylib` in libicuuc
 * means "sibling of where libicuuc actually lives on the host", which
 * is `/opt/homebrew/Cellar/icu4c@78/<ver>/lib/`. If we otool'd the
 * staged copy instead, @loader_path would resolve to our stage/lib/
 * dir where the sibling hasn't been copied yet, so we'd miss it.
 *
 * `seen` is keyed by realpath (canonical source) so a host-side dylib
 * pointed at by both `/opt/homebrew/lib/libfoo.X.dylib` (symlink) and
 * `/opt/homebrew/Cellar/.../libfoo.X.dylib` (target) only gets bundled
 * once. Best-effort: an unresolvable @rpath ref logs a warning and
 * keeps going rather than aborting the whole build. */
static int mac_bundle_deps_recursive(const char* start_orig_path,
                                     const char* bundle_lib_dir,
                                     mac_strs_t* seen,
                                     int verbose) {
    mac_strs_t queue = {0};
    if (mac_strs_push(&queue, start_orig_path) != 0) { mac_strs_free(&queue); return -1; }

    int rc = 0;
    while (queue.count > 0) {
        char* cur_orig = queue.items[queue.count - 1];
        queue.items[queue.count - 1] = NULL;
        queue.count--;

        mac_strs_t deps = {0};
        if (mac_otool_deps(cur_orig, &deps) != 0) {
            if (verbose) fprintf(stderr, "  warn: otool -L %s failed\n", cur_orig);
            free(cur_orig);
            continue;
        }
        /* Parse LC_RPATH so @rpath/ deps resolve against the referrer's
         * own rpath list. Homebrew dylibs commonly include rpaths into
         * /opt/homebrew/opt/<formula>/lib — without this we'd lose
         * transitive deps like libbrotlicommon (referenced via @rpath
         * from libbrotlidec). */
        mac_strs_t rpaths = {0};
        mac_parse_rpaths(cur_orig, &rpaths);

        for (size_t i = 0; i < deps.count; i++) {
            const char* dep = deps.items[i];
            char* resolved = NULL;
            if (mac_resolve_ref(cur_orig, dep, &rpaths, &resolved) != 0) {
                if (verbose) fprintf(stderr, "  warn: cannot resolve %s (referrer %s) — skipping\n", dep, cur_orig);
                continue;
            }
            /* Skip self-reference (dylibs' LC_ID_DYLIB shows up here). */
            if (strcmp(resolved, cur_orig) == 0) { free(resolved); continue; }
            if (mac_is_system_dylib(resolved)) { free(resolved); continue; }

            /* Canonicalize via realpath: Homebrew exposes most dylibs at
             * `/opt/homebrew/lib/` as relative symlinks into the Cellar.
             * Dedup, otool, and the hardlink source must all use the
             * realpath; otherwise pc_copy_or_link_tree preserves the
             * symlink and the bundled `../Cellar/...` ref dangles. */
            char real_path[1024];
            const char* canonical = resolved;
            if (realpath(resolved, real_path)) {
                canonical = real_path;
            }

            if (mac_strs_contains(seen, canonical)) { free(resolved); continue; }
            if (mac_strs_contains(seen, resolved))  { free(resolved); continue; }

            const char* base = strrchr(resolved, '/');
            base = base ? base + 1 : resolved;
            char dst[1280];
            snprintf(dst, sizeof(dst), "%s/%s", bundle_lib_dir, base);

            /* Basename collision: a previously-seen dylib already lives at
             * this dst path. Keep the first; log so the user knows. */
            if (mac_strs_contains(seen, dst)) {
                if (verbose) fprintf(stderr, "  warn: basename collision for %s — keeping first\n", base);
                free(resolved);
                continue;
            }

            if (pc_copy_or_link_tree(canonical, dst) != 0) {
                fprintf(stderr, "  warn: failed to bundle %s -> %s\n", canonical, dst);
                free(resolved);
                continue;
            }
            chmod(dst, 0644);
            if (verbose) printf("  bundled dylib: %s\n", base);

            /* Track original, realpath, and dst so dedup hits whichever
             * key the next iteration brings in. Push the ORIGINAL host
             * path into the queue so its @loader_path refs resolve in
             * the source location, not in our stage. */
            mac_strs_push(seen, resolved);
            if (canonical != resolved) mac_strs_push(seen, canonical);
            mac_strs_push(seen, dst);
            mac_strs_push(&queue, resolved);
            free(resolved);
        }
        mac_strs_free(&deps);
        mac_strs_free(&rpaths);
        free(cur_orig);
    }
    mac_strs_free(&queue);
    return rc;
}

/* Rewrite every absolute non-system LC_LOAD_DYLIB ref in `mach_o` to
 * `@executable_path/../lib/<basename>`. If `set_id_to_self` is non-zero,
 * also update LC_ID_DYLIB so the dylib advertises itself with the same
 * @executable_path-relative name (needed for bundled dylibs so other
 * Mach-Os can reference them consistently). */
static int mac_rewrite_to_bundle(const char* mach_o, int set_id_to_self, int verbose) {
    char* int_tool = pc_path_lookup("install_name_tool");
    if (!int_tool) {
        fprintf(stderr, "Error: install_name_tool not found (install Xcode Command Line Tools)\n");
        return -1;
    }

    mac_strs_t deps = {0};
    if (mac_otool_deps(mach_o, &deps) != 0) { free(int_tool); return -1; }

    /* Make the file writable in case it came from a read-only hardlink source. */
    chmod(mach_o, 0755);

    const char* mach_o_base = strrchr(mach_o, '/');
    mach_o_base = mach_o_base ? mach_o_base + 1 : mach_o;

    /* Batch every -change pair (and the optional -id) into a single
     * install_name_tool invocation per file. Without this, each pair
     * is a separate process and each emits its own "will invalidate
     * the code signature" warning — for ~50 dylibs with ~10 deps each,
     * that's 500+ warning lines. Batching cuts it to one call per file,
     * and `mac_spawn_quiet_stderr` discards the (still-emitted) single
     * warning since we always codesign --force right after. */
    char new_id[1024];
    new_id[0] = 0;
    if (set_id_to_self) {
        snprintf(new_id, sizeof(new_id), "@executable_path/../lib/%s", mach_o_base);
    }

    /* First pass: count eligible -change pairs so we can size argv. */
    size_t n_changes = 0;
    for (size_t i = 0; i < deps.count; i++) {
        const char* dep = deps.items[i];
        if (dep[0] == '@' && strncmp(dep, "@rpath/", 7) != 0) continue;
        if (dep[0] != '@' && mac_is_system_dylib(dep)) continue;
        const char* base = strrchr(dep, '/');
        base = base ? base + 1 : dep;
        if (set_id_to_self && strcmp(base, mach_o_base) == 0) continue;
        n_changes++;
    }

    if (n_changes == 0 && !set_id_to_self) {
        mac_strs_free(&deps);
        free(int_tool);
        return 0;
    }

    /* Build argv:
     *   [int_tool, "-id", new_id,            // when set_id_to_self
     *    "-change", OLD1, NEW1,
     *    "-change", OLD2, NEW2, ...,
     *    mach_o, NULL] */
    size_t argc_total = 1                                /* int_tool */
                      + (set_id_to_self ? 2 : 0)         /* -id NEW_ID */
                      + n_changes * 3                    /* -change OLD NEW */
                      + 1                                /* mach_o */
                      + 1;                               /* NULL */
    char** argv      = (char**)calloc(argc_total, sizeof(char*));
    /* `new_ref` strings are heap-allocated and outlive the loop. */
    char** to_free   = (char**)calloc(n_changes + 1, sizeof(char*));
    if (!argv || !to_free) {
        free(argv); free(to_free);
        mac_strs_free(&deps); free(int_tool);
        return -1;
    }

    size_t a = 0, tf = 0;
    argv[a++] = int_tool;
    if (set_id_to_self) {
        argv[a++] = (char*)"-id";
        argv[a++] = new_id;
    }
    for (size_t i = 0; i < deps.count; i++) {
        const char* dep = deps.items[i];
        if (dep[0] == '@' && strncmp(dep, "@rpath/", 7) != 0) continue;
        if (dep[0] != '@' && mac_is_system_dylib(dep)) continue;
        const char* base = strrchr(dep, '/');
        base = base ? base + 1 : dep;
        if (set_id_to_self && strcmp(base, mach_o_base) == 0) continue;

        char new_ref[1024];
        snprintf(new_ref, sizeof(new_ref), "@executable_path/../lib/%s", base);
        char* nr_dup = strdup(new_ref);
        if (!nr_dup) continue;
        to_free[tf++] = nr_dup;

        argv[a++] = (char*)"-change";
        argv[a++] = (char*)dep;
        argv[a++] = nr_dup;
    }
    argv[a++] = (char*)mach_o;
    argv[a]   = NULL;

    int rc = mac_spawn_quiet_stderr(argv);
    if (rc != 0 && verbose) {
        fprintf(stderr, "  warn: install_name_tool failed on %s (rc=%d)\n", mach_o, rc);
        /* Non-fatal — codesign will fail later if the binary is broken. */
    }

    for (size_t i = 0; i < tf; i++) free(to_free[i]);
    free(to_free);
    free(argv);
    mac_strs_free(&deps);
    free(int_tool);
    return rc == 0 ? 0 : -1;
}

/* Ad-hoc re-sign a Mach-O after install_name_tool modified it. Required
 * on Apple Silicon: the kernel refuses to exec unsigned/broken-signature
 * Mach-Os from the dyld path. `codesign --sign -` is an ad-hoc identity
 * (no developer cert needed); --force replaces the now-invalid sig. */
static int mac_codesign_adhoc(const char* mach_o, int verbose) {
    char* cs = pc_path_lookup("codesign");
    if (!cs) {
        if (verbose) fprintf(stderr, "  warn: codesign not found — bundle may be rejected on Apple Silicon\n");
        return 0;  /* Best-effort: don't fail the build on x86_64 hosts that may not have it. */
    }
    char* argv[] = { cs, (char*)"--force", (char*)"--sign", (char*)"-", (char*)mach_o, NULL };
    /* codesign prints "<path>: replacing existing signature" to stderr
     * for every file — informative but spammy across ~50 dylibs. Discard
     * stderr; a non-zero rc still surfaces a clear warning below. */
    int rc = mac_spawn_quiet_stderr(argv);
    if (rc != 0 && verbose) fprintf(stderr, "  warn: codesign --sign - failed on %s (rc=%d)\n", mach_o, rc);
    free(cs);
    return 0;  /* Always best-effort — we don't want to fail builds on a signing edge case. */
}

/* Top-level macOS portability pass over a fully staged bin/php +
 * ext/*.so tree. Walks deps, bundles them into <stage>/lib/, rewires
 * every Mach-O in the tree, ad-hoc-signs everything modified.
 *
 * `original_php_bin` is the host path the bundled bin/php was
 * hardlinked from (post-realpath); ditto `original_ext_dir` for the
 * .so files in <stage>/ext/. We need the original locations to walk
 * deps — `@loader_path` refs in a bundled dylib resolve against where
 * it lived on the host, not where it lives in the bundle. */
static void mac_make_bundle_portable(const char* stage_dir,
                                     const char* original_php_bin,
                                     const char* original_ext_dir,
                                     int verbose) {
    char lib_dir[1024];
    snprintf(lib_dir, sizeof(lib_dir), "%s/lib", stage_dir);
    if (pc_mkdir_p(lib_dir) != 0) {
        fprintf(stderr, "  warn: could not create %s — skipping dyld rewiring\n", lib_dir);
        return;
    }

    /* Two parallel lists: `roots_staged` is what we rewrite/codesign at
     * the end; `roots_orig` is what we feed into the dep-graph walker
     * so @loader_path resolves to the host filesystem (where siblings
     * actually exist). For each ext/*.so we discover, the corresponding
     * host location is <original_ext_dir>/<same-basename>. */
    mac_strs_t roots_staged = {0};
    mac_strs_t roots_orig   = {0};
    char p[1024];
    snprintf(p, sizeof(p), "%s/bin/php", stage_dir);
    struct stat st;
    if (stat(p, &st) == 0 && S_ISREG(st.st_mode) && original_php_bin) {
        mac_strs_push(&roots_staged, p);
        mac_strs_push(&roots_orig,   original_php_bin);
    }
    snprintf(p, sizeof(p), "%s/ext", stage_dir);
    DIR* d = opendir(p);
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            const char* nm = e->d_name;
            const char* dot = strrchr(nm, '.');
            if (!dot || strcmp(dot, ".so") != 0) continue;
            char full_staged[1280];
            snprintf(full_staged, sizeof(full_staged), "%s/%s", p, nm);
            mac_strs_push(&roots_staged, full_staged);
            if (original_ext_dir && *original_ext_dir) {
                char full_orig[1280];
                snprintf(full_orig, sizeof(full_orig), "%s/%s", original_ext_dir, nm);
                mac_strs_push(&roots_orig, full_orig);
            } else {
                /* No original ext dir (static PHP) — fall back to the
                 * staged path; will mostly resolve since the bundle has
                 * no @loader_path refs in this case. */
                mac_strs_push(&roots_orig, full_staged);
            }
        }
        closedir(d);
    }

    /* Bundle the transitive closure of non-system deps. */
    mac_strs_t seen = {0};
    for (size_t i = 0; i < roots_orig.count; i++) {
        mac_bundle_deps_recursive(roots_orig.items[i], lib_dir, &seen, verbose);
    }

    /* Now rewrite every Mach-O in the tree (roots + bundled libs). The
     * roots are NOT dylibs in the LC_ID_DYLIB sense (bin/php is an
     * executable; ext/*.so are bundles with no install name worth
     * advertising) so don't touch their id. Bundled lib/*.dylib DO get
     * an -id rewrite so they declare themselves @executable_path-relative. */
    for (size_t i = 0; i < roots_staged.count; i++) {
        mac_rewrite_to_bundle(roots_staged.items[i], 0, verbose);
        mac_codesign_adhoc(roots_staged.items[i], verbose);
    }

    DIR* dl = opendir(lib_dir);
    if (dl) {
        struct dirent* e;
        while ((e = readdir(dl)) != NULL) {
            const char* nm = e->d_name;
            if (nm[0] == '.') continue;
            char full[1280];
            snprintf(full, sizeof(full), "%s/%s", lib_dir, nm);
            struct stat lst;
            if (stat(full, &lst) != 0 || !S_ISREG(lst.st_mode)) continue;
            mac_rewrite_to_bundle(full, 1, verbose);
            mac_codesign_adhoc(full, verbose);
        }
        closedir(dl);
    }

    if (verbose) {
        size_t bundled = 0;
        DIR* dlv = opendir(lib_dir);
        if (dlv) {
            struct dirent* e;
            while ((e = readdir(dlv)) != NULL) {
                if (e->d_name[0] == '.') continue;
                bundled++;
            }
            closedir(dlv);
        }
        printf("macOS dyld rewiring: %zu dylib%s bundled in %s/lib\n",
               bundled, bundled == 1 ? "" : "s", stage_dir);
    }

    mac_strs_free(&seen);
    mac_strs_free(&roots_staged);
    mac_strs_free(&roots_orig);
}
#endif  /* __APPLE__ */

/* Compose a synthetic runtime tree at `stage_dir` containing:
 *   bin/php                                (hardlink/copy of host php)
 *   bin/php.ini                            (copy of host's loaded php.ini)
 *   bin/conf.d/<host-conf.d-files>.ini     (copied from host, ext exclude applied)
 *   bin/conf.d/99-ubuilder-overrides.ini   (ffi.enable=true + opcache tweaks)
 *   ext/<name>.so                          (hardlink/copy from host extension_dir)
 *
 * Rationale: rather than guess at extension load order or hand-curate a
 * synthetic ini, we copy the host's actual config verbatim. The host's
 * conf.d/<NN>-<ext>.ini fragments encode the right load order via their
 * numeric prefixes (10-mysqlnd before 20-mysqli, etc.). Our 99- override
 * file loads dead last, so its values win for the few things we need to
 * force (ffi.enable, opcache.validate_timestamps, phar.readonly).
 *
 * Returns UB_SUCCESS on success; the caller embeds via
 * ub_embed_runtime_tree(stage_dir) and removes `stage_dir` afterwards.
 * Missing extensions abort with a clear "install php-<name>" hint. */
static ub_result_t php_stage_synthetic_runtime(const char* php_bin,
                                               const char* ext_dir_host,
                                               const php_ext_list_t* exts,
                                               const ub_config_t* config,
                                               const char* stage_dir) {
    char p[1024];

    pc_remove_tree(stage_dir);
    snprintf(p, sizeof(p), "%s/bin", stage_dir);        if (pc_mkdir_p(p) != 0) return UB_ERROR_EXTRACTION_FAILED;
    snprintf(p, sizeof(p), "%s/bin/conf.d", stage_dir); if (pc_mkdir_p(p) != 0) return UB_ERROR_EXTRACTION_FAILED;
    snprintf(p, sizeof(p), "%s/ext", stage_dir);        if (pc_mkdir_p(p) != 0) return UB_ERROR_EXTRACTION_FAILED;

    /* bin/php — hardlink-or-copy from the host. Resolve through symlinks
     * first: Homebrew exposes `php` as `/opt/homebrew/bin/php` →
     * `../Cellar/php/<ver>/bin/php` (a RELATIVE symlink). `pc_copy_or_link_tree`
     * faithfully preserves symlinks, but a relative link to `../Cellar/...`
     * becomes dangling once the bundle is extracted on a target that has no
     * Cellar tree. realpath() collapses the chain to the real binary so we
     * hardlink/copy the actual file. */
#ifndef PLATFORM_WINDOWS
    char php_real_path[1024];
    const char* php_to_stage = php_bin;
    if (realpath(php_bin, php_real_path)) {
        php_to_stage = php_real_path;
    }
#else
    const char* php_to_stage = php_bin;
#endif
    snprintf(p, sizeof(p), "%s/bin/php", stage_dir);
    if (pc_copy_or_link_tree(php_to_stage, p) != 0) {
        fprintf(stderr, "Error: failed to stage host PHP binary at %s\n", p);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    chmod(p, 0755);

    /* bin/php.ini + bin/conf.d/* — copy the host's actual config so we
     * inherit memory_limit / error_reporting / timezone / extension load
     * order without having to guess any of it. */
    char host_main_ini[1024] = {0};
    char host_scan_dir[1024] = {0};
    php_probe_ini_paths(php_bin, host_main_ini, sizeof(host_main_ini),
                        host_scan_dir, sizeof(host_scan_dir));

    snprintf(p, sizeof(p), "%s/bin/php.ini", stage_dir);
    /* `php --ini` reports the path PHP _would_ load if present, but on
     * fresh Homebrew installs (and Herd/static-php-cli) the file often
     * doesn't actually exist on disk yet — only `php.ini-development` /
     * `-production` ship. Treat an unreadable source the same as the
     * "(none)" case rather than failing the entire build: write an empty
     * placeholder so `-c <ini>` resolves and let conf.d/ + the
     * 99-ubuilder-overrides.ini we drop below carry the actual config. */
    FILE* fi = host_main_ini[0] ? fopen(host_main_ini, "rb") : NULL;
    if (fi) {
        FILE* fo = fopen(p, "wb");
        if (!fo) {
            fclose(fi);
            fprintf(stderr, "Error: failed to open stage php.ini for write: %s\n", p);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) {
            if (fwrite(buf, 1, n, fo) != n) {
                fclose(fi); fclose(fo);
                fprintf(stderr, "Error: short write copying host php.ini\n");
                return UB_ERROR_EXTRACTION_FAILED;
            }
        }
        fclose(fi); fclose(fo);
    } else {
        if (host_main_ini[0] && config && config->verbose) {
            printf("note: host php.ini reported at %s but not readable — using placeholder\n",
                   host_main_ini);
        }
        FILE* fo = fopen(p, "w");
        if (fo) {
            if (host_main_ini[0]) {
                fprintf(fo, "; (host reported %s but it wasn't readable at build time; conf.d/ holds the active config)\n",
                        host_main_ini);
            } else {
                fprintf(fo, "; (host had no php.ini; see conf.d/ for ubuilder overrides)\n");
            }
            fclose(fo);
        }
    }

    /* Copy host conf.d → bin/conf.d, dropping fragments for --excluded exts. */
    char confd_dir[1024];
    snprintf(confd_dir, sizeof(confd_dir), "%s/bin/conf.d", stage_dir);
    if (host_scan_dir[0]) {
        php_copy_host_confd(host_scan_dir, confd_dir,
                            config ? config->exclude       : NULL,
                            config ? config->exclude_count : 0);
    }

    /* High-priority ubuilder override file (loaded last). */
    snprintf(p, sizeof(p), "%s/99-ubuilder-overrides.ini", confd_dir);
    if (php_write_default_ini(p, exts) != 0) {
        fprintf(stderr, "Error: failed to write %s\n", p);
        return UB_ERROR_EXTRACTION_FAILED;
    }

    /* ext/<name>.so — one per declared extension. */
    int missing = 0;
    for (size_t i = 0; i < exts->count; i++) {
        char src[1024], dst[1024];
        snprintf(src, sizeof(src), "%s/%s.so", ext_dir_host, exts->names[i]);
        snprintf(dst, sizeof(dst), "%s/ext/%s.so", stage_dir, exts->names[i]);
        struct stat st;
        if (stat(src, &st) != 0) {
            fprintf(stderr,
                    "Error: composer.json declares ext-%s but no %s found in host extension_dir.\n"
                    "       On Debian/Ubuntu: sudo apt install php-%s\n"
                    "       On RHEL/Fedora:   sudo dnf install php-%s\n",
                    exts->names[i], src, exts->names[i], exts->names[i]);
            missing++;
            continue;
        }
        if (pc_copy_or_link_tree(src, dst) != 0) {
            fprintf(stderr, "Error: failed to stage extension %s\n", src);
            return UB_ERROR_EXTRACTION_FAILED;
        }
    }
    if (missing > 0) return UB_ERROR_RUNTIME_NOT_FOUND;

#ifdef __APPLE__
    /* macOS portability pass: bundle non-system dylibs, rewrite Mach-O
     * load commands to @executable_path-relative paths, ad-hoc re-sign.
     * For statically linked host PHP (Herd) every dep is /usr/lib or
     * /System and this completes as a no-op (no dylibs bundled, no refs
     * to rewrite). For dynamic Homebrew PHP it pulls in the libxml2 /
     * libicu / libsodium etc. chain so the bundle runs on a clean Mac.
     *
     * Pass the ORIGINAL host paths (post-realpath for php_bin; the host
     * ext_dir for .so files) so the BFS walker resolves @loader_path
     * refs in the location they were authored against. */
    mac_make_bundle_portable(stage_dir, php_to_stage, ext_dir_host,
                             config ? config->verbose : 0);
#endif

    return UB_SUCCESS;
}

/* Locate `composer` on the host PATH. Returns heap path or NULL. */
static char* php_find_composer(void) {
    char* p = pc_path_lookup("composer");
    if (p) return p;
    /* Fall back to /usr/local/bin/composer.phar style installs. */
    return pc_path_lookup("composer.phar");
}

#endif  /* !PLATFORM_WINDOWS */

// PHP runtime validation
//
// Accept any of: a configured entry_point that resolves to an existing
// regular file (Laravel `artisan`, Symfony `bin/console`, generic CLI
// scripts), or one of the conventional defaults (main.php, index.php).
// Extensionless entry files are allowed — PHP doesn't care about the
// extension; the shebang/argv tells it what to run.
static ub_result_t php_validate_project(const char* project_dir) {
    char path[1024];
    struct stat st;
    /* The validate hook doesn't have access to the config yet (it's called
     * before embed). We accept any of the conventional names OR any of the
     * common CLI entry-point names projects use. The configured entry is
     * cross-checked at embed time, where we DO have the config — and the
     * embed step writes a .ubuilder.entry marker so the launcher honors it. */
    const char* candidates[] = {
        "main.php", "index.php", "artisan", "bin/console", "console", "app", NULL
    };
    for (size_t i = 0; candidates[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", project_dir, candidates[i]);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) return UB_SUCCESS;
    }
    return UB_ERROR_FILE_NOT_FOUND;
}

// Embed PHP runtime (M1-D: synthetic tree from host php + composer ext-*)
static ub_result_t php_embed_runtime(const ub_config_t* config, FILE* output_file) {
    ub_result_t result;

#ifndef PLATFORM_WINDOWS
    /* Mode 1: explicit --runtime-source. Same shape as Python/Node. */
    if (config && config->runtime_source) {
        struct stat src_st;
        if (stat(config->runtime_source, &src_st) != 0) {
            fprintf(stderr, "Error: --runtime-source not found: %s\n", config->runtime_source);
            return UB_ERROR_FILE_NOT_FOUND;
        }
        if (S_ISDIR(src_st.st_mode)) {
            printf("Embedding hermetic PHP tree: %s\n", config->runtime_source);
            return ub_embed_runtime_tree(config->runtime_source, output_file);
        }
        if (S_ISREG(src_st.st_mode)) {
            printf("Embedding user-chosen PHP binary: %s\n", config->runtime_source);
            return ub_embed_runtime_single_as_tree(config->runtime_source, "bin/php", output_file);
        }
        fprintf(stderr, "Error: --runtime-source must be a file or directory\n");
        return UB_ERROR_INVALID_ARGS;
    }

    /* Mode 2: synthesize a runtime tree from host PHP + composer ext-* deps.
     * This is the default path for `ubuilder` on PHP projects. */
    char* php_bin = NULL;
    if (config && config->php_runtime_static) {
        /* v2.5.0: opt-in static-php-cli runtime. Downloads a curated
         * static PHP from ubuilder's GitHub releases on first use,
         * then reuses the cached binary on subsequent builds. */
        ub_result_t sr = ub_php_static_resolve("8.4",
                                               config->verbose,
                                               &php_bin);
        if (sr != UB_SUCCESS) {
            /* ub_php_static_resolve printed a specific error already. */
            return sr;
        }
    } else {
        php_bin = pc_path_lookup("php");
        if (!php_bin) {
            fprintf(stderr,
                    "Error: PHP not on PATH. Install php-cli, e.g.:\n"
                    "  sudo apt install php-cli\n"
                    "  brew install php\n"
                    "Or pass --php-runtime=static to download a curated\n"
                    "static PHP from ubuilder's GitHub releases.\n");
            return UB_ERROR_RUNTIME_NOT_FOUND;
        }
    }

    char ext_dir_host[1024];
    if (php_probe_extension_dir(php_bin, ext_dir_host, sizeof(ext_dir_host)) != 0) {
        fprintf(stderr, "Error: could not probe host PHP for extension_dir\n");
        free(php_bin);
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    /* Static-PHP detection. Two shapes of "compiled-in extensions, nothing
     * to bundle" we collapse together:
     *   1. Reported `extension_dir` doesn't exist on disk
     *      (static-php-cli output: Herd reports `/lib/php/extensions/...`
     *      from `./configure --prefix=`, a path that's never resolved)
     *   2. Reported `extension_dir` exists but contains no `.so` files
     *      (Homebrew base install: 71 modules compiled into the binary,
     *      empty `/opt/homebrew/lib/php/pecl/<api>/` waits for future
     *      `pecl install <ext>` to drop files in)
     * In both cases `php -m` surfaces every built-in module; we ship the
     * binary + ini as-is, ext/ stays empty. Avoids inventing a fake
     * extension_dir layout the host doesn't actually use. */
    int static_php = 0;
    {
        struct stat ext_st;
        if (stat(ext_dir_host, &ext_st) != 0 || !S_ISDIR(ext_st.st_mode)) {
            static_php = 1;
        } else {
            DIR* d = opendir(ext_dir_host);
            int has_so = 0;
            if (d) {
                struct dirent* e;
                while ((e = readdir(d)) != NULL) {
                    const char* nm = e->d_name;
                    const char* dot = strrchr(nm, '.');
                    if (dot && strcmp(dot, ".so") == 0 && dot != nm) {
                        has_so = 1;
                        break;
                    }
                }
                closedir(d);
            }
            if (!has_so) static_php = 1;
        }
    }

    if (config && config->verbose) {
        const char* static_note = "";
        if (static_php) {
            struct stat ext_st;
            static_note = (stat(ext_dir_host, &ext_st) != 0)
                ? "  (not on disk — treating PHP as statically linked)"
                : "  (empty — every extension is compiled into the binary)";
        }
        printf("Host PHP: %s\n  extension_dir: %s%s\n",
               php_bin, ext_dir_host, static_note);
    }

    /* DEFAULT BUNDLING POLICY: copy ALL .so files from the host's
     * extension_dir, then drop anything the user listed in `exclude`.
     * Rationale: an FFI / GUI app needs ext-ffi at runtime even when
     * composer.json doesn't mention it, and asking every user to list
     * every transitively-needed extension in composer.json is bad DX.
     * The user can `--exclude=ext-<name>` to drop noisy or oversized
     * extensions they don't want shipping. */
    php_ext_list_t exts = {0};
    if (!static_php && php_list_host_extensions(ext_dir_host, &exts) != 0) {
        fprintf(stderr, "Error: could not enumerate host extension_dir %s\n", ext_dir_host);
        free(php_bin);
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    /* Composer.json's `require.ext-*` is still meaningful for two things:
     *   - emitting a clear "install php-X" hint when the user declares an
     *     extension that the host doesn't ship
     *   - feeding --ignore-platform-req= flags to `composer install` for
     *     extensions the user explicitly --excluded
     * Parse it once here so both can use it. */
    php_ext_list_t declared = {0};
    if (php_parse_composer_extensions(config ? config->project_dir : ".", &declared) != 0) {
        free(php_bin);
        php_ext_list_free(&exts);
        return UB_ERROR_INVALID_ARGS;
    }

    /* Cross-check declared vs host: any declared ext that's NOT on the
     * host AND not in the exclude list is a real build-blocker — emit the
     * apt/dnf install hint and abort. For statically linked PHP (Herd,
     * static-php-cli output) the host extension list is `php -m` rather
     * than a directory scan, since the binary ships its modules baked
     * in. */
    php_ext_list_t loaded_mods = {0};
    const php_ext_list_t* host_provided = &exts;
    if (static_php) {
        if (php_list_loaded_modules(php_bin, &loaded_mods) == 0) {
            host_provided = &loaded_mods;
        }
    }
    int missing = 0;
    for (size_t i = 0; i < declared.count; i++) {
        const char* dname = declared.names[i];
        if (config && ub_ext_excluded((const char* const*)config->exclude,
                                      config->exclude_count, dname)) continue;
        int found = 0;
        for (size_t j = 0; j < host_provided->count; j++) {
            if (strcmp(host_provided->names[j], dname) == 0) { found = 1; break; }
        }
        if (!found) {
            if (config && config->php_runtime_static) {
                /* User opted in to ubuilder's curated static PHP; we
                 * (the ubuilder maintainers) own the extension set. */
                fprintf(stderr,
                        "Error: composer.json declares ext-%s but the curated static PHP\n"
                        "       (--php-runtime=static) doesn't include it.\n"
                        "       Either:\n"
                        "         - drop ext-%s from composer.json's `require`, or\n"
                        "         - use --php-runtime=host instead (uses the host's PHP), or\n"
                        "         - open an issue requesting ext-%s in the default static build:\n"
                        "           https://github.com/developersharif/ubuilder/issues\n",
                        dname, dname, dname);
            } else if (static_php) {
                fprintf(stderr,
                        "Error: composer.json declares ext-%s but `php -m` doesn't list it.\n"
                        "       Host PHP appears statically linked (e.g. Laravel Herd, static-php-cli).\n"
                        "       Use a PHP build that includes %s, or rebuild static-php-cli with `--with-%s`.\n",
                        dname, dname, dname);
            } else {
                fprintf(stderr,
                        "Error: composer.json declares ext-%s but no %s/%s.so found in host extension_dir.\n"
                        "       On Debian/Ubuntu: sudo apt install php-%s\n"
                        "       On RHEL/Fedora:   sudo dnf install php-%s\n",
                        dname, ext_dir_host, dname, dname, dname);
            }
            missing++;
        }
    }
    php_ext_list_free(&loaded_mods);
    php_ext_list_free(&declared);
    if (missing > 0) {
        free(php_bin);
        php_ext_list_free(&exts);
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    /* Apply --exclude / exclude:[...] to the host-scanned list. Per-name
     * lines are always printed (the user explicitly asked for these to be
     * dropped — they want confirmation it took effect). The bulk "+ <ext>"
     * list of bundled extensions is only printed under --verbose. */
    if (config && config->exclude_count > 0 && exts.count > 0) {
        size_t write = 0;
        for (size_t i = 0; i < exts.count; i++) {
            if (ub_ext_excluded((const char* const*)config->exclude,
                                config->exclude_count, exts.names[i])) {
                printf("  - %s (excluded by config; not staged)\n", exts.names[i]);
                free(exts.names[i]);
                exts.names[i] = NULL;
                continue;
            }
            exts.names[write++] = exts.names[i];
        }
        exts.count = write;
    }

    if (static_php) {
        printf("Bundling statically linked PHP (no loadable extensions to copy)\n");
    } else {
        printf("Bundling %zu host PHP extension%s from %s\n",
               exts.count, exts.count == 1 ? "" : "s", ext_dir_host);
        if (config && config->verbose) {
            for (size_t i = 0; i < exts.count; i++) printf("  + %s\n", exts.names[i]);
        }
    }

    /* Stage under XDG cache so hardlinks work. */
    const char* xdg  = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    char stage[1024];
    if (xdg && *xdg)        snprintf(stage, sizeof(stage), "%s/ubuilder/stage/php-rt-%d",        xdg,  (int)getpid());
    else if (home && *home) snprintf(stage, sizeof(stage), "%s/.cache/ubuilder/stage/php-rt-%d", home, (int)getpid());
    else                    snprintf(stage, sizeof(stage), "/tmp/ubuilder-php-rt-%d",                  (int)getpid());

    result = php_stage_synthetic_runtime(php_bin, ext_dir_host, &exts, config, stage);
    free(php_bin);
    php_ext_list_free(&exts);
    if (result != UB_SUCCESS) {
        pc_remove_tree(stage);
        return result;
    }

    if (config && config->verbose) {
        printf("Embedding PHP synthetic runtime tree: %s\n", stage);
    }
    result = ub_embed_runtime_tree(stage, output_file);
    pc_remove_tree(stage);
    return result;
#else
    ub_runtime_info_t runtime_info;

    // Detect PHP binary on system
    result = ub_detect_runtime_binary(UB_RUNTIME_PHP, &runtime_info);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: PHP runtime not found on system\n");
        return result;
    }

    printf("Embedding PHP runtime: %s\n", runtime_info.binary_path);
    printf("PHP version: %s\n", runtime_info.version_string ? runtime_info.version_string : "unknown");

    // On Windows, we need to embed the entire PHP directory structure
    char php_dir[1024];
    strcpy(php_dir, runtime_info.binary_path);
    char* last_slash = strrchr(php_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        fprintf(stderr, "Error: Invalid PHP binary path format\n");
        ub_runtime_info_cleanup(&runtime_info);
        return UB_ERROR_INVALID_ARGS;
    }

    printf("PHP directory: %s\n", php_dir);

    result = php_embed_windows_runtime(php_dir, output_file);
    ub_runtime_info_cleanup(&runtime_info);
    return result;
#endif
}

#ifdef PLATFORM_WINDOWS
// Function to embed complete Windows PHP runtime
// Helper function to check if extension is enabled in php.ini
static int is_extension_enabled_in_ini(const char* php_ini_path, const char* ext_name) {
    FILE* ini_file = fopen(php_ini_path, "r");
    if (!ini_file) {
        return 0; // If no php.ini, assume extension could be useful
    }
    
    char line[512];
    char search_pattern[256];
    
    // Create search patterns: extension=ext_name or extension=ext_name.dll
    snprintf(search_pattern, sizeof(search_pattern), "extension=%s", ext_name);
    
    while (fgets(line, sizeof(line), ini_file)) {
        // Skip comments and empty lines
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == ';' || *trimmed == '#' || *trimmed == '\n' || *trimmed == '\0') {
            continue;
        }
        
        // Check if this line enables our extension
        if (strstr(trimmed, search_pattern) != NULL) {
            fclose(ini_file);
            return 1;
        }
    }
    
    fclose(ini_file);
    return 0;
}

// Helper function to check if an extension is built into PHP
static int is_builtin_extension(const char* ext_name) {
    const char* builtin_extensions[] = {
        // Core extensions that are typically built into PHP
        "mbstring.so", "json.so", "ctype.so", "iconv.so", 
        "hash.so", "filter.so", "pcre.so", "spl.so", "date.so",
        "core.so", "standard.so", "phar.so", "reflection.so",
        
        // Extensions commonly built-in on modern PHP installations
        "bcmath.so", "calendar.so", "curl.so", "dom.so", "exif.so",
        "fileinfo.so", "ftp.so", "gd.so", "gettext.so", "libxml.so",
        "openssl.so", "pdo.so", "pdo_sqlite.so", "posix.so", "random.so",
        "session.so", "simplexml.so", "sockets.so", "sqlite3.so",
        "tokenizer.so", "xml.so", "xmlreader.so", "xmlwriter.so",
        "xsl.so", "zip.so", "zlib.so", "bz2.so", "pcntl.so",
        "readline.so", "shmop.so", "sodium.so", "sysvmsg.so",
        "sysvsem.so", "sysvshm.so", "ffi.so", "mysqlnd.so",
        "mcrypt.so", "ssh2.so",
        
        NULL
    };
    
    for (int i = 0; builtin_extensions[i]; i++) {
        if (strcmp(ext_name, builtin_extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static ub_result_t php_embed_windows_runtime(const char* php_dir, FILE* output_file) {
    // Essential files needed for PHP to run on Windows
    const char* essential_files[] = {
        "php.exe",
        "php.ini",             // Configuration (if exists)
        NULL
    };
    
    // Optional DLL files that may or may not be present depending on PHP build
    const char* optional_files[] = {
        "php8ts.dll",          // Main PHP engine (Thread Safe)
        "php8.dll",            // Main PHP engine (Non-Thread Safe)
        "libcrypto-3-x64.dll", // OpenSSL crypto
        "libssl-3-x64.dll",    // OpenSSL
        "icudt72.dll",         // ICU data
        "icudt73.dll",         // ICU data (newer version)
        "icuin72.dll",         // ICU internationalization  
        "icuin73.dll",         // ICU internationalization (newer version)
        "icuuc72.dll",         // ICU common
        "icuuc73.dll",         // ICU common (newer version)
        "libsqlite3.dll",      // SQLite
        "ssleay32.dll",        // OpenSSL (older naming)
        "libeay32.dll",        // OpenSSL (older naming)
        "msvcr110.dll",        // Visual C++ runtime
        "msvcr120.dll",        // Visual C++ runtime
        "msvcr140.dll",        // Visual C++ runtime
        NULL
    };
    
    // Core extensions that should always be included (commonly used)
    const char* core_extensions[] = {
        "php_mbstring.dll",    // Multi-byte string support (essential)
        "php_openssl.dll",     // SSL/TLS support (essential for HTTPS)
        "php_curl.dll",        // HTTP client functionality
        "php_fileinfo.dll",    // File type detection
        "php_sqlite3.dll",     // SQLite database support
        "php_pdo_sqlite.dll",  // PDO SQLite driver
        "php_json.dll",        // JSON support (often built-in, but include if available)
        "php_filter.dll",      // Input filtering
        "php_hash.dll",        // Hashing functions
        "php_ctype.dll",       // Character type checking
        "php_ffi.dll",         // FFI — required by php-gui and native library integrations
        "php_sockets.dll",     // Sockets — needed by IPC / GUI event bridges
        NULL
    };
    
    printf("Embedding Windows PHP runtime from: %s\n", php_dir);
    
    size_t total_size = 0;
    int files_embedded = 0;
    
    // Check if php.ini exists for extension detection
    char php_ini_path[1024];
    snprintf(php_ini_path, sizeof(php_ini_path), "%s\\php.ini", php_dir);
    struct stat ini_stat;
    int has_php_ini = (stat(php_ini_path, &ini_stat) == 0);
    
    // First, embed essential core files (required)
    for (int i = 0; essential_files[i]; i++) {
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s\\%s", php_dir, essential_files[i]);
        
        struct stat st;
        if (stat(file_path, &st) == 0) {
            FILE* file = fopen(file_path, "rb");
            if (file) {
                // Write filename length and name
                uint32_t name_len = (uint32_t)strlen(essential_files[i]);
                fwrite(&name_len, sizeof(name_len), 1, output_file);
                fwrite(essential_files[i], 1, name_len, output_file);
                
                // Write file size
                uint32_t file_size = (uint32_t)st.st_size;
                fwrite(&file_size, sizeof(file_size), 1, output_file);
                
                // Write file content
                char buffer[8192];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                    fwrite(buffer, 1, bytes_read, output_file);
                }
                
                fclose(file);
                total_size += st.st_size;
                files_embedded++;
            }
        } else if (strcmp(essential_files[i], "php.ini") != 0) {
            // php.ini is optional, but php.exe is required
            printf("  Warning: Required file not found: %s\n", file_path);
        }
    }
    
    // Then, embed optional runtime files (best effort)
    for (int i = 0; optional_files[i]; i++) {
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s\\%s", php_dir, optional_files[i]);
        
        struct stat st;
        if (stat(file_path, &st) == 0) {
            FILE* file = fopen(file_path, "rb");
            if (file) {
                printf("  Found optional file: %s (%.2f KB)\n", optional_files[i], st.st_size / 1024.0);
                
                // Write filename length and name
                uint32_t name_len = (uint32_t)strlen(optional_files[i]);
                fwrite(&name_len, sizeof(name_len), 1, output_file);
                fwrite(optional_files[i], 1, name_len, output_file);
                
                // Write file size
                uint32_t file_size = (uint32_t)st.st_size;
                fwrite(&file_size, sizeof(file_size), 1, output_file);
                
                // Write file content
                char buffer[8192];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                    fwrite(buffer, 1, bytes_read, output_file);
                }
                
                fclose(file);
                total_size += st.st_size;
                files_embedded++;
            }
        }
    }
    
    // Then, embed extensions intelligently
    char ext_dir[1024];
    snprintf(ext_dir, sizeof(ext_dir), "%s\\ext", php_dir);
    
    // First embed core extensions
    for (int i = 0; core_extensions[i]; i++) {
        char ext_path[1024];
        snprintf(ext_path, sizeof(ext_path), "%s\\%s", ext_dir, core_extensions[i]);
        
        struct stat st;
        if (stat(ext_path, &st) == 0) {
            FILE* ext_file = fopen(ext_path, "rb");
            if (ext_file) {
                // Write extension filename with ext/ prefix
                char ext_name[256];
                snprintf(ext_name, sizeof(ext_name), "ext\\%s", core_extensions[i]);
                uint32_t name_len = (uint32_t)strlen(ext_name);
                fwrite(&name_len, sizeof(name_len), 1, output_file);
                fwrite(ext_name, 1, name_len, output_file);
                
                // Write file size
                uint32_t file_size = (uint32_t)st.st_size;
                fwrite(&file_size, sizeof(file_size), 1, output_file);
                
                // Write file content
                char buffer[8192];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), ext_file)) > 0) {
                    fwrite(buffer, 1, bytes_read, output_file);
                }
                
                fclose(ext_file);
                total_size += st.st_size;
                files_embedded++;
            }
        }
    }
    
    // Now scan ext/ directory for additional extensions
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char search_path[1024];
    
    snprintf(search_path, sizeof(search_path), "%s\\*.dll", ext_dir);
    find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            // Skip if this is already a core extension
            int is_core = 0;
            for (int i = 0; core_extensions[i]; i++) {
                if (strcmp(find_data.cFileName, core_extensions[i]) == 0) {
                    is_core = 1;
                    break;
                }
            }
            
            if (!is_core) {
                // Check if extension is enabled in php.ini or seems useful
                int should_embed = 0;
                
                if (has_php_ini) {
                    // Remove .dll extension for ini check
                    char ext_name_no_dll[256];
                    strncpy(ext_name_no_dll, find_data.cFileName, sizeof(ext_name_no_dll));
                    char* dll_pos = strstr(ext_name_no_dll, ".dll");
                    if (dll_pos) *dll_pos = '\0';
                    
                    // Remove php_ prefix if present for ini check
                    char* ext_name_for_ini = ext_name_no_dll;
                    if (strncmp(ext_name_no_dll, "php_", 4) == 0) {
                        ext_name_for_ini = ext_name_no_dll + 4;
                    }
                    
                    should_embed = is_extension_enabled_in_ini(php_ini_path, ext_name_for_ini);
                } else {
                    // Without php.ini, embed commonly useful extensions
                    const char* useful_extensions[] = {
                        "php_pdo.dll", "php_pdo_mysql.dll", "php_mysqli.dll", "php_gd.dll",
                        "php_zip.dll", "php_xml.dll", "php_dom.dll", "php_session.dll",
                        "php_pcre.dll", "php_spl.dll", "php_standard.dll", "php_date.dll",
                        "php_exif.dll", "php_intl.dll", "php_xsl.dll", "php_ffi.dll",
                        NULL
                    };
                    
                    for (int i = 0; useful_extensions[i]; i++) {
                        if (strcmp(find_data.cFileName, useful_extensions[i]) == 0) {
                            should_embed = 1;
                            break;
                        }
                    }
                }
                
                if (should_embed) {
                    char ext_path[1024];
                    snprintf(ext_path, sizeof(ext_path), "%s\\%s", ext_dir, find_data.cFileName);
                    
                    struct stat st;
                    if (stat(ext_path, &st) == 0) {
                        FILE* ext_file = fopen(ext_path, "rb");
                        if (ext_file) {
                            char ext_name[256];
                            snprintf(ext_name, sizeof(ext_name), "ext\\%s", find_data.cFileName);
                            uint32_t name_len = (uint32_t)strlen(ext_name);
                            fwrite(&name_len, sizeof(name_len), 1, output_file);
                            fwrite(ext_name, 1, name_len, output_file);
                            
                            uint32_t file_size = (uint32_t)st.st_size;
                            fwrite(&file_size, sizeof(file_size), 1, output_file);
                            
                            char buffer[8192];
                            size_t bytes_read;
                            while ((bytes_read = fread(buffer, 1, sizeof(buffer), ext_file)) > 0) {
                                fwrite(buffer, 1, bytes_read, output_file);
                            }
                            
                            fclose(ext_file);
                            total_size += st.st_size;
                            files_embedded++;
                        }
                    }
                }
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    // Write end marker (empty filename)
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);
    
    printf("Windows PHP runtime embedded: %d files, %.2f MB total\n", 
           files_embedded, total_size / (1024.0 * 1024.0));
    
    return UB_SUCCESS;
}
#endif



/* TU-local handle to the active exclude list. Set by php_embed_application
 * before the recursion starts, cleared on return. Single-threaded per build
 * (ub_build_executable is not re-entrant), so a static is safe here. */
static char* const* g_php_exclude_pats = NULL;
static size_t       g_php_exclude_count = 0;

// Helper function to embed all PHP files recursively
static ub_result_t php_embed_files_recursive(const char* dir_path, const char* base_path, FILE* output_file) {
#ifdef PLATFORM_WINDOWS
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char search_path[1024];
    
    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);
    find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* Skip dot-dirs (.git, .composer, .idea, etc.) */
            if (find_data.cFileName[0] == '.') continue;
            {
                const char* rel_dir = full_path + strlen(base_path);
                if (*rel_dir == '\\') rel_dir++;
                if (ub_path_excluded((const char* const*)g_php_exclude_pats,
                                     g_php_exclude_count, rel_dir, 1)) continue;
            }
            // Recursively process subdirectory
            php_embed_files_recursive(full_path, base_path, output_file);
        } else {
            /* Skip dotfiles (.gitignore, .env, etc.) */
            if (find_data.cFileName[0] == '.') continue;
            /* No extension whitelist — bundle ALL files like the POSIX branch
             * does.  Tcl/Tk script libraries (.tcl), timezone data, and other
             * resource files without recognised extensions must be included so
             * Tcl_Init() can find init.tcl.  Rely on ub_path_excluded() to
             * drop anything the user excluded (e.g. dist/**). */
            {
                // Calculate relative path from project root
                const char* rel_path = full_path + strlen(base_path);
                if (*rel_path == '\\') rel_path++; // Skip leading backslash
                if (ub_path_excluded((const char* const*)g_php_exclude_pats,
                                     g_php_exclude_count, rel_path, 0)) continue;
#else
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            /* Skip dot-dirs so .git / .composer / .idea stay out. */
            if (entry->d_name[0] == '.') continue;
            {
                const char* rel_dir = full_path + strlen(base_path);
                if (*rel_dir == '/') rel_dir++;
                if (ub_path_excluded((const char* const*)g_php_exclude_pats,
                                     g_php_exclude_count, rel_dir, 1)) continue;
            }
            // Recursively process subdirectory
            php_embed_files_recursive(full_path, base_path, output_file);
        } else if (S_ISREG(st.st_mode)) {
            /* M1-D: drop the extension whitelist. composer vendor/ ships
             * .php, .json, LICENSE, README, etc.; the bundle needs all
             * of it. Skip dotfiles to keep .gitignore / .env out. */
            if (entry->d_name[0] == '.') continue;
            {
                // Calculate relative path from project root
                const char* rel_path = full_path + strlen(base_path);
                if (*rel_path == '/') rel_path++; // Skip leading slash
                if (ub_path_excluded((const char* const*)g_php_exclude_pats,
                                     g_php_exclude_count, rel_path, 0)) continue;
#endif
                
                // Write file metadata: relative path length and content
                uint32_t path_len = (uint32_t)strlen(rel_path);
                fwrite(&path_len, sizeof(path_len), 1, output_file);
                fwrite(rel_path, 1, path_len, output_file);
                
                // Write file content
                FILE* file = fopen(full_path, "rb");
                if (file) {
#ifdef PLATFORM_WINDOWS
                    // Get file size on Windows
                    fseek(file, 0, SEEK_END);
                    uint32_t file_size = ftell(file);
                    fseek(file, 0, SEEK_SET);
#else
                    uint32_t file_size = st.st_size;
#endif
                    fwrite(&file_size, sizeof(file_size), 1, output_file);
                    
                    char buffer[8192];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                        fwrite(buffer, 1, bytes_read, output_file);
                    }
                    fclose(file);
                }
            }
        }
#ifdef PLATFORM_WINDOWS
    } while (FindNextFileA(find_handle, &find_data));
    
    FindClose(find_handle);
#else
    }
    
    closedir(dir);
#endif
    return UB_SUCCESS;
}

#ifndef PLATFORM_WINDOWS
/* M1-D composer integration: stage the project, run `composer install
 * --no-dev`, hardlink-cache the resulting vendor/. Mirrors the Node
 * M8-B pattern.
 *
 * Returns UB_SUCCESS with *out_staged set on success. *out_staged is
 * NULL when no staging is needed (no composer.json, --no-install-deps,
 * or composer not on PATH — non-fatal warn). */
static ub_result_t php_maybe_stage_project_with_deps(const ub_config_t* config,
                                                     char** out_staged) {
    *out_staged = NULL;
    if (!config || config->no_install_deps) return UB_SUCCESS;

    char cj_path[1024];
    char cl_path[1024];
    snprintf(cj_path, sizeof(cj_path), "%s/composer.json", config->project_dir);
    snprintf(cl_path, sizeof(cl_path), "%s/composer.lock", config->project_dir);
    struct stat st;
    if (stat(cj_path, &st) != 0 || !S_ISREG(st.st_mode)) return UB_SUCCESS;
    int have_lock = (stat(cl_path, &st) == 0 && S_ISREG(st.st_mode));

    char* composer = php_find_composer();
    if (!composer) {
        printf("note: composer.json found but `composer` is not on PATH;\n"
               "      bundling project as-is. Install composer (https://getcomposer.org/)\n"
               "      or vendor your deps manually before building.\n");
        return UB_SUCCESS;
    }

    char* php_bin = pc_path_lookup("php");
    if (!php_bin) { free(composer); return UB_SUCCESS; }

    const char* xdg  = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    char stage[1024];
    if (xdg && *xdg)        snprintf(stage, sizeof(stage), "%s/ubuilder/stage/php-app-%d",        xdg,  (int)getpid());
    else if (home && *home) snprintf(stage, sizeof(stage), "%s/.cache/ubuilder/stage/php-app-%d", home, (int)getpid());
    else                    snprintf(stage, sizeof(stage), "/tmp/ubuilder-php-app-%d",                  (int)getpid());

    pc_remove_tree(stage);
    if (config->verbose) {
        printf("Staging PHP project for composer install:\n");
        printf("  source: %s\n  stage:  %s\n", config->project_dir, stage);
    }
    if (pc_copy_or_link_tree(config->project_dir, stage) != 0) {
        fprintf(stderr, "Error: failed to stage project at %s\n", stage);
        pc_remove_tree(stage);
        free(composer); free(php_bin);
        return UB_ERROR_EXTRACTION_FAILED;
    }

    /* Install cache key: host PHP version is the runtime identity. */
    char php_ver[64] = {0};
    char* ver_argv[] = {php_bin, (char*)"-r", (char*)"echo PHP_VERSION;", NULL};
    char* ver_captured = NULL;
    if (pc_spawn_capture(php_bin, ver_argv, NULL, NULL, 128, &ver_captured) == 0 && ver_captured) {
        snprintf(php_ver, sizeof(php_ver), "php-%s", ver_captured);
    } else {
        snprintf(php_ver, sizeof(php_ver), "php-unknown");
    }
    free(ver_captured);

    char hex_key[65];
    int cache_ok = (ub_install_cache_key(php_ver, cj_path,
                                         have_lock ? cl_path : NULL,
                                         hex_key) == 0);
    char staged_vendor[1024];
    snprintf(staged_vendor, sizeof(staged_vendor), "%s/vendor", stage);

    if (cache_ok && ub_install_cache_lookup("php", hex_key, staged_vendor) == 0) {
        printf("Install cache hit (php/%.12s...) — skipped composer install.\n", hex_key);
        *out_staged = strdup(stage);
        free(composer); free(php_bin);
        return UB_SUCCESS;
    }

    /* Cache miss: composer install in the stage. The headline summary
     * stays on by default (composer install can take a while; users
     * want to know what's happening), but the detail lines (composer
     * binary path, --ignore-platform-req flags we built up) are gated
     * on --verbose. */
    printf("Running composer install in staged project%s\n",
           have_lock ? " (lockfile present)" : "");
    if (config->verbose) {
        printf("  composer: %s\n", composer);
    }

    /* Neutralize host PHP directives that commonly break composer when
     * the host config references defunct paths (e.g., stale
     * `auto_prepend_file` pointing at an uninstalled Herd/Valet helper,
     * or `extension=` lines for since-deleted .so files). We can't pass
     * `-d` flags to composer's PHP child (the binary may be a shell
     * wrapper), but PHP_INI_SCAN_DIR is honored regardless: PHP loads
     * every `*.ini` in those dirs after the main php.ini, so a file
     * with the same key wins.
     *
     * Setting PHP_INI_SCAN_DIR REPLACES the compile-time scan dir, so
     * we must include the host's own scan dir first (otherwise its
     * conf.d extensions stop loading). Our dir is appended last so its
     * `zz-*.ini` file is processed after host conf.d and overrides. */
    char host_scan_dir[1024] = {0};
    char host_main_ini[1024] = {0};
    php_probe_ini_paths(php_bin, host_main_ini, sizeof(host_main_ini),
                        host_scan_dir, sizeof(host_scan_dir));

    char ini_override_dir[1280];
    snprintf(ini_override_dir, sizeof(ini_override_dir),
             "%s/.ubuilder-ini-overrides", stage);
    pc_mkdir_p(ini_override_dir);

    char ini_override_file[1400];
    snprintf(ini_override_file, sizeof(ini_override_file),
             "%s/zz-ubuilder-neutralize.ini", ini_override_dir);
    FILE* ovf = fopen(ini_override_file, "w");
    if (ovf) {
        fputs("; ubuilder: neutralize host PHP directives that can break\n", ovf);
        fputs("; composer install (stale auto_prepend_file pointing at\n", ovf);
        fputs("; uninstalled Herd/Valet helpers, noisy startup errors).\n", ovf);
        fputs("auto_prepend_file =\n", ovf);
        fputs("auto_append_file =\n", ovf);
        fputs("display_errors = stderr\n", ovf);
        fputs("display_startup_errors = Off\n", ovf);
        fclose(ovf);
    }

    char scan_env_val[2600];
    if (host_scan_dir[0]) {
#ifdef _WIN32
        snprintf(scan_env_val, sizeof(scan_env_val),
                 "PHP_INI_SCAN_DIR=%s;%s", host_scan_dir, ini_override_dir);
#else
        snprintf(scan_env_val, sizeof(scan_env_val),
                 "PHP_INI_SCAN_DIR=%s:%s", host_scan_dir, ini_override_dir);
#endif
    } else {
        snprintf(scan_env_val, sizeof(scan_env_val),
                 "PHP_INI_SCAN_DIR=%s", ini_override_dir);
    }
    if (config->verbose) {
        printf("  %s\n", scan_env_val);
    }

    /* Build argv dynamically — for each --excluded `ext-<name>` we ALSO
     * need to pass --ignore-platform-req=ext-<name> so composer doesn't
     * abort over the missing platform requirement. */
    static const char* base_args[] = {
        "install", "--no-dev", "--no-interaction", "--no-progress",
        "--prefer-dist", "--optimize-autoloader"
    };
    const size_t base_n = sizeof(base_args) / sizeof(base_args[0]);

    /* Count platform-req flags we'll need. */
    size_t ipr_count = 0;
    if (config && config->exclude_count > 0) {
        for (size_t i = 0; i < config->exclude_count; i++) {
            const char* p = config->exclude[i];
            if (p && strncmp(p, "ext-", 4) == 0) ipr_count++;
        }
    }

    size_t argc_total = 1 /*composer*/ + base_n + ipr_count;
    char**  argv     = (char**)calloc(argc_total + 1, sizeof(char*));
    char**  to_free  = (char**)calloc(ipr_count + 1, sizeof(char*));
    if (!argv || !to_free) {
        free(argv); free(to_free); free(composer); free(php_bin);
        return UB_ERROR_MEMORY_ALLOCATION;
    }
    size_t a = 0;
    argv[a++] = composer;
    for (size_t i = 0; i < base_n; i++) argv[a++] = (char*)base_args[i];
    size_t tf = 0;
    if (config) {
        for (size_t i = 0; i < config->exclude_count; i++) {
            const char* p = config->exclude[i];
            if (!p || strncmp(p, "ext-", 4) != 0) continue;
            char* flag = NULL;
            int len = snprintf(NULL, 0, "--ignore-platform-req=%s", p);
            if (len <= 0) continue;
            flag = (char*)malloc((size_t)len + 1);
            if (!flag) { free(argv); for (size_t k = 0; k < tf; k++) free(to_free[k]); free(to_free); free(composer); free(php_bin); return UB_ERROR_MEMORY_ALLOCATION; }
            snprintf(flag, (size_t)len + 1, "--ignore-platform-req=%s", p);
            argv[a++]    = flag;
            to_free[tf++] = flag;
            /* User opted to exclude this ext — confirm the platform-req
             * pass took effect (parallels the "(excluded by config; not
             * staged)" lines we always print). */
            printf("  passing %s\n", flag);
        }
    }
    argv[a] = NULL;

    char* env_extras[] = { scan_env_val, NULL };
    char** spawn_env = pc_env_overlay(env_extras);
    int rc = pc_spawn_and_wait(composer, argv,
                               spawn_env ? spawn_env : NULL, stage);
    if (spawn_env) pc_env_free(spawn_env);
    for (size_t i = 0; i < tf; i++) free(to_free[i]);
    free(to_free);
    free(argv);
    free(composer); free(php_bin);
    if (rc != 0) {
        fprintf(stderr, "Error: composer install failed (exit %d). Bundle build aborted.\n", rc);
        pc_remove_tree(stage);
        return UB_ERROR_EXECUTION_FAILED;
    }

    if (cache_ok) {
        struct stat vs;
        if (stat(staged_vendor, &vs) == 0 && S_ISDIR(vs.st_mode)) {
            if (ub_install_cache_store("php", hex_key, staged_vendor) == 0) {
                printf("Install cache stored (php/%.12s...)\n", hex_key);
            }
        }
    }

    *out_staged = strdup(stage);
    return UB_SUCCESS;
}
#endif

// Embed PHP application
static ub_result_t php_embed_application(const ub_config_t* config, FILE* output_file) {
    struct stat st;

    /* Publish the exclude list so php_embed_files_recursive sees it. */
    g_php_exclude_pats  = config ? config->exclude       : NULL;
    g_php_exclude_count = config ? config->exclude_count : 0;

#ifndef PLATFORM_WINDOWS
    char* staged_project = NULL;
    ub_result_t srcrc = php_maybe_stage_project_with_deps(config, &staged_project);
    if (srcrc != UB_SUCCESS) return srcrc;
    const char* project_dir = staged_project ? staged_project : config->project_dir;
#else
    const char* project_dir = config->project_dir;
#endif

    if (stat(project_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
#ifndef PLATFORM_WINDOWS
        if (staged_project) { pc_remove_tree(staged_project); free(staged_project); }
#endif
        return UB_ERROR_FILE_NOT_FOUND;
    }

    // Write number of files marker (we'll update this later if needed)
    uint32_t file_count_placeholder = 0;
    fwrite(&file_count_placeholder, sizeof(file_count_placeholder), 1, output_file);

    /* Emit a `.ubuilder.entry` marker file as the FIRST embedded record. It
     * carries the configured entry_point's relative path so the launcher
     * doesn't have to guess (today's main.php→index.php→first.php heuristic
     * breaks for Laravel's `artisan`, Symfony's `bin/console`, etc.).
     * Skipped when no entry_point was configured — launcher heuristic still
     * works for the simple cases. */
    if (config && config->entry_point && *config->entry_point) {
        const char* marker = ".ubuilder.entry";
        uint32_t   plen   = (uint32_t)strlen(marker);
        fwrite(&plen,  sizeof(plen),  1, output_file);
        fwrite(marker, 1, plen, output_file);

        uint32_t clen = (uint32_t)strlen(config->entry_point);
        fwrite(&clen, sizeof(clen), 1, output_file);
        fwrite(config->entry_point, 1, clen, output_file);
    }

    ub_result_t result = php_embed_files_recursive(project_dir, project_dir, output_file);

    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);

#ifndef PLATFORM_WINDOWS
    if (staged_project) { pc_remove_tree(staged_project); free(staged_project); }
#endif

    g_php_exclude_pats  = NULL;
    g_php_exclude_count = 0;
    return result;
}

// Generate PHP launcher
static ub_result_t php_generate_launcher(FILE* output_file) {
    const char* launcher_code = 
        "<?php\n"
        "// PHP UBuilder Launcher\n"
        "// Launcher code will be here\n"
        "?>\n";
    
    uint32_t code_size = (uint32_t)strlen(launcher_code);
    fwrite(&code_size, sizeof(code_size), 1, output_file);
    fwrite(launcher_code, 1, code_size, output_file);
    
    return UB_SUCCESS;
}

// Required files for PHP runtime
static const char* php_required_files[] = {
    "php",
    "php.ini",
    NULL
};

// Supported file extensions
static const char* php_supported_extensions[] = {
    ".php",
    ".phtml",
    NULL
};

// PHP builder definition
const ub_runtime_builder_t php_builder = {
    .runtime_type = UB_RUNTIME_PHP,
    .name = "PHP",
    .description = "PHP CLI runtime builder (embeds full PHP interpreter)",
    .validate_project = php_validate_project,
    .embed_runtime = php_embed_runtime,
    .embed_application = php_embed_application,
    .generate_launcher = php_generate_launcher,
    .estimated_runtime_size = 6 * 1024 * 1024, // ~6MB for PHP binary
    .required_files = php_required_files,
    .supported_extensions = php_supported_extensions
};
