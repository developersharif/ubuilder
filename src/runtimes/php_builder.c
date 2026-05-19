#include "runtime_builder.h"
#include "runtime_embedder.h"
#include "../core/platform_compat.h"
#include "../core/json_mini.h"
#include "../core/glob_match.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    char* argv[] = { (char*)php_bin, (char*)"--ini", NULL };
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

/* Probe `<php_bin> -r 'echo ini_get("extension_dir");'`. Returns 0 / -1. */
static int php_probe_extension_dir(const char* php_bin, char* out, size_t out_cap) {
    char* argv[] = {
        (char*)php_bin,
        (char*)"-d", (char*)"extension_dir=",  /* Block auto-load — just want the default. */
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

    /* bin/php — hardlink-or-copy from the host. */
    snprintf(p, sizeof(p), "%s/bin/php", stage_dir);
    if (pc_copy_or_link_tree(php_bin, p) != 0) {
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
    if (host_main_ini[0]) {
        FILE* fi = fopen(host_main_ini, "rb");
        FILE* fo = fopen(p, "wb");
        if (!fi || !fo) {
            if (fi) fclose(fi);
            if (fo) fclose(fo);
            fprintf(stderr, "Error: failed to copy host php.ini (%s -> %s)\n",
                    host_main_ini, p);
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
        /* Host didn't have a php.ini at all — write an empty placeholder so
         * the launcher's `-c <ini>` argument resolves. */
        FILE* fo = fopen(p, "w");
        if (fo) { fprintf(fo, "; (host had no php.ini; see conf.d/ for ubuilder overrides)\n"); fclose(fo); }
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
    char* php_bin = pc_path_lookup("php");
    if (!php_bin) {
        fprintf(stderr,
                "Error: PHP not on PATH. Install php-cli, e.g.:\n"
                "  sudo apt install php-cli\n");
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    char ext_dir_host[1024];
    if (php_probe_extension_dir(php_bin, ext_dir_host, sizeof(ext_dir_host)) != 0) {
        fprintf(stderr, "Error: could not probe host PHP for extension_dir\n");
        free(php_bin);
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    printf("Host PHP: %s\n  extension_dir: %s\n", php_bin, ext_dir_host);

    /* DEFAULT BUNDLING POLICY: copy ALL .so files from the host's
     * extension_dir, then drop anything the user listed in `exclude`.
     * Rationale: an FFI / GUI app needs ext-ffi at runtime even when
     * composer.json doesn't mention it, and asking every user to list
     * every transitively-needed extension in composer.json is bad DX.
     * The user can `--exclude=ext-<name>` to drop noisy or oversized
     * extensions they don't want shipping. */
    php_ext_list_t exts = {0};
    if (php_list_host_extensions(ext_dir_host, &exts) != 0) {
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
     * apt/dnf install hint and abort. */
    int missing = 0;
    for (size_t i = 0; i < declared.count; i++) {
        const char* dname = declared.names[i];
        if (config && ub_ext_excluded((const char* const*)config->exclude,
                                      config->exclude_count, dname)) continue;
        int found = 0;
        for (size_t j = 0; j < exts.count; j++) {
            if (strcmp(exts.names[j], dname) == 0) { found = 1; break; }
        }
        if (!found) {
            fprintf(stderr,
                    "Error: composer.json declares ext-%s but no %s/%s.so found in host extension_dir.\n"
                    "       On Debian/Ubuntu: sudo apt install php-%s\n"
                    "       On RHEL/Fedora:   sudo dnf install php-%s\n",
                    dname, ext_dir_host, dname, dname, dname);
            missing++;
        }
    }
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

    printf("Bundling %zu host PHP extension%s from %s\n",
           exts.count, exts.count == 1 ? "" : "s", ext_dir_host);
    if (config && config->verbose) {
        for (size_t i = 0; i < exts.count; i++) printf("  + %s\n", exts.names[i]);
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

    printf("Embedding PHP synthetic runtime tree: %s\n", stage);
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
            // Recursively process subdirectory
            php_embed_files_recursive(full_path, base_path, output_file);
        } else {
            // Check if it's a PHP file or other relevant file
            const char* ext = strrchr(find_data.cFileName, '.');
            if (ext && (strcmp(ext, ".php") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0 || 
                       strcmp(ext, ".dll") == 0 || strcmp(ext, ".exe") == 0 || strcmp(ext, ".pdb") == 0 ||
                       strcmp(ext, ".lib") == 0 || strcmp(ext, ".xml") == 0 || strcmp(ext, ".ini") == 0 ||
                       strcmp(ext, ".so") == 0 || strcmp(ext, ".dylib") == 0)) {
                // Calculate relative path from project root
                const char* rel_path = full_path + strlen(base_path);
                if (*rel_path == '\\') rel_path++; // Skip leading backslash
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
    printf("Staging PHP project for composer install:\n");
    printf("  source: %s\n  stage:  %s\n", config->project_dir, stage);
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

    /* Cache miss: composer install in the stage. */
    printf("Running composer install in staged project%s\n",
           have_lock ? " (lockfile present)" : "");
    printf("  composer: %s\n", composer);

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
            printf("  passing %s\n", flag);
        }
    }
    argv[a] = NULL;

    int rc = pc_spawn_and_wait(composer, argv, NULL, stage);
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
