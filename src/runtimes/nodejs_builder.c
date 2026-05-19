#include "runtime_builder.h"
#include "runtime_embedder.h"
#include "../core/platform_compat.h"
#include "../core/glob_match.h"
#include "../core/json_mini.h"
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
    #define popen _popen
    #define pclose _pclose
#else
    #include <dirent.h>
    #include <unistd.h>
#endif

// Node.js runtime validation
static ub_result_t nodejs_validate_project(const char* project_dir) {
    char main_js_path[1024];
    char package_json_path[1024];
    struct stat st;
    
    // Check for main.js
    snprintf(main_js_path, sizeof(main_js_path), "%s/main.js", project_dir);
    if (stat(main_js_path, &st) == 0) {
        return UB_SUCCESS;
    }
    
    // Check for index.js
    snprintf(main_js_path, sizeof(main_js_path), "%s/index.js", project_dir);
    if (stat(main_js_path, &st) == 0) {
        return UB_SUCCESS;
    }
    
    // Check for package.json
    snprintf(package_json_path, sizeof(package_json_path), "%s/package.json", project_dir);
    if (stat(package_json_path, &st) == 0) {
        return UB_SUCCESS;
    }
    
    return UB_ERROR_FILE_NOT_FOUND;
}

// Forward declarations
#ifdef PLATFORM_WINDOWS
static ub_result_t nodejs_embed_windows_runtime(const char* nodejs_dir, FILE* output_file);
#endif

// Embed Node.js runtime (M1-E: hermetic via vendored tree or user-chosen binary)
static ub_result_t nodejs_embed_runtime(const ub_config_t* config, FILE* output_file) {
    ub_result_t result;

#ifndef PLATFORM_WINDOWS
    if (config && config->runtime_source) {
        struct stat src_st;
        if (stat(config->runtime_source, &src_st) != 0) {
            fprintf(stderr, "Error: --runtime-source not found: %s\n", config->runtime_source);
            return UB_ERROR_FILE_NOT_FOUND;
        }
        if (S_ISDIR(src_st.st_mode)) {
            printf("Embedding hermetic Node.js tree: %s\n", config->runtime_source);
            return ub_embed_runtime_tree(config->runtime_source, output_file);
        }
        if (S_ISREG(src_st.st_mode)) {
            printf("Embedding user-chosen Node.js binary: %s\n", config->runtime_source);
            return ub_embed_runtime_single_as_tree(config->runtime_source, "bin/node", output_file);
        }
        fprintf(stderr, "Error: --runtime-source must be a file or directory\n");
        return UB_ERROR_INVALID_ARGS;
    }

    /* DX: auto-discover a vendored Node in the cache. If missing and the
     * user hasn't opted out, auto-spawn vendor-runtimes.sh to fetch it. */
    if (config && !config->use_host_runtime) {
        char cached[1024];
        int cache_hit = (ub_runtime_cache_lookup("node", "bin/node", cached, sizeof(cached)) == 0);
        if (!cache_hit && !config->no_auto_vendor) {
            if (ub_auto_vendor("node") == UB_SUCCESS) {
                cache_hit = (ub_runtime_cache_lookup("node", "bin/node", cached, sizeof(cached)) == 0);
            }
        }
        if (cache_hit) {
            printf("Embedding hermetic Node.js tree (auto-discovered): %s\n", cached);
            return ub_embed_runtime_tree(cached, output_file);
        }
    }
#endif

    ub_runtime_info_t runtime_info;
    
    // Detect Node.js binary on system
    result = ub_detect_runtime_binary(UB_RUNTIME_NODEJS, &runtime_info);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Node.js runtime not found on system\n");
        return result;
    }
    
    printf("Embedding Node.js runtime: %s\n", runtime_info.binary_path);
    printf("Node.js version: %s\n", runtime_info.version_string ? runtime_info.version_string : "unknown");

#ifdef PLATFORM_WINDOWS
    // On Windows, we need to embed the entire Node.js directory structure
    // Extract the directory from node.exe path
    char nodejs_dir[1024];
    strcpy(nodejs_dir, runtime_info.binary_path);
    char* last_slash = strrchr(nodejs_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        fprintf(stderr, "Error: Invalid Node.js binary path format\n");
        ub_runtime_info_cleanup(&runtime_info);
        return UB_ERROR_INVALID_ARGS;
    }
    
    printf("Node.js directory: %s\n", nodejs_dir);
    
    // Embed the complete Windows Node.js runtime
    result = nodejs_embed_windows_runtime(nodejs_dir, output_file);
    if (result != UB_SUCCESS) {
        ub_runtime_info_cleanup(&runtime_info);
        return result;
    }
#else
    /* POSIX: 1-record tree using the host binary at bin/node. Bundle is
     * V5-format but uses the host's interpreter — non-portable. */
    printf("Binary size: %.2f MB\n", runtime_info.binary_size / (1024.0 * 1024.0));
    printf("note: bundle will use host node (non-portable).\n"
           "      Run `scripts/vendor-runtimes.sh node` for a hermetic bundle\n"
           "      (auto-discovered next build) or drop --use-host-runtime.\n");
    result = ub_embed_runtime_single_as_tree(runtime_info.binary_path, "bin/node", output_file);
    if (result != UB_SUCCESS) {
        ub_runtime_info_cleanup(&runtime_info);
        return result;
    }
#endif
    
    // Cleanup
    ub_runtime_info_cleanup(&runtime_info);
    
    return result;
}

/* TU-local exclude state — published by nodejs_embed_application around
 * the recursion. Single-threaded per build; safe. */
static char* const* g_node_exclude_pats = NULL;
static size_t       g_node_exclude_count = 0;

// Helper function to embed all Node.js files recursively
static ub_result_t nodejs_embed_files_recursive(const char* dir_path, const char* base_path, FILE* output_file) {
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
            nodejs_embed_files_recursive(full_path, base_path, output_file);
        } else {
            // Check if it's a Node.js file or other relevant file
            const char* ext = strrchr(find_data.cFileName, '.');
            if (ext && (strcmp(ext, ".js") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0)) {
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
            {
                const char* rel_dir = full_path + strlen(base_path);
                if (*rel_dir == '/') rel_dir++;
                if (ub_path_excluded((const char* const*)g_node_exclude_pats,
                                     g_node_exclude_count, rel_dir, 1)) continue;
            }
            // Recursively process subdirectory
            nodejs_embed_files_recursive(full_path, base_path, output_file);
        } else if (S_ISREG(st.st_mode)) {
            /* M8-B: drop the .js/.json/.txt extension filter. npm packages
             * ship .cjs/.mjs/.d.ts/LICENSE/README/etc., and the bundle
             * needs all of it to run correctly. Skip dotfiles to keep
             * .git/.npm-cache out of the bundle. */
            if (entry->d_name[0] == '.') { continue; }
            {
                // Calculate relative path from project root
                const char* rel_path = full_path + strlen(base_path);
                if (*rel_path == '/') rel_path++; // Skip leading slash
                if (ub_path_excluded((const char* const*)g_node_exclude_pats,
                                     g_node_exclude_count, rel_path, 0)) continue;
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
/* M8-B: find the vendored Node runtime so we can spawn its `npm` against
 * a staged project. Mirrors the Python staging discovery — prefers
 * config->runtime_source, falls back to cache auto-discovery, else NULL. */
static int nodejs_locate_runtime(const ub_config_t* config, char* out, size_t out_cap) {
    if (config && config->runtime_source) {
        struct stat st;
        if (stat(config->runtime_source, &st) == 0 && S_ISDIR(st.st_mode)) {
            size_t n = strlen(config->runtime_source);
            if (n >= out_cap) return -1;
            memcpy(out, config->runtime_source, n + 1);
            return 0;
        }
    }
    if (config && !config->use_host_runtime) {
        return ub_runtime_cache_lookup("node", "bin/node", out, out_cap);
    }
    return -1;
}

/* M8-B: install user's package.json deps into a staged copy of the
 * project directory so `node_modules/` ships in the bundle without
 * mutating the user's source tree.
 *
 * Returns UB_SUCCESS with *out_staged set to the staged project dir on
 * success. *out_staged is NULL (no staging needed) when:
 *   - --no-install-deps is set
 *   - package.json is absent in project_dir
 *   - no vendored Node runtime is reachable (host mode — caller embeds
 *     the project as-is; the user is responsible for npm install)
 */
/* Return pointer to the trailing path segment of `path` (interior pointer,
 * caller must not free). */
static const char* nodejs_path_basename(const char* path) {
    const char* p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/* Emit a JSON value tree to `fp` with 2-space indent. Internal to the
 * package-json filter; not a general-purpose pretty-printer. */
static void node_json_escape(FILE* fp, const char* s) {
    fputc('"', fp);
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '\n': fputs("\\n", fp);  break;
            case '\r': fputs("\\r", fp);  break;
            case '\t': fputs("\\t", fp);  break;
            case '\b': fputs("\\b", fp);  break;
            case '\f': fputs("\\f", fp);  break;
            default:
                if (*p < 0x20) fprintf(fp, "\\u%04x", *p);
                else fputc(*p, fp);
        }
    }
    fputc('"', fp);
}

static void node_emit_indent(FILE* fp, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", fp);
}

static void node_emit_value(FILE* fp, const json_value_t* v, int depth);

static void node_emit_value(FILE* fp, const json_value_t* v, int depth) {
    if (!v) { fputs("null", fp); return; }
    switch (v->type) {
        case JSON_NULL:   fputs("null", fp); break;
        case JSON_BOOL:   fputs(v->v.b ? "true" : "false", fp); break;
        case JSON_NUMBER: fprintf(fp, "%ld", v->v.n); break;
        case JSON_STRING: node_json_escape(fp, v->v.str.s); break;
        case JSON_ARRAY:
            if (v->v.arr.count == 0) { fputs("[]", fp); break; }
            fputs("[\n", fp);
            for (size_t i = 0; i < v->v.arr.count; i++) {
                node_emit_indent(fp, depth + 1);
                node_emit_value(fp, v->v.arr.items[i], depth + 1);
                fputs(i + 1 < v->v.arr.count ? ",\n" : "\n", fp);
            }
            node_emit_indent(fp, depth);
            fputc(']', fp);
            break;
        case JSON_OBJECT:
            if (v->v.obj.count == 0) { fputs("{}", fp); break; }
            fputs("{\n", fp);
            for (size_t i = 0; i < v->v.obj.count; i++) {
                node_emit_indent(fp, depth + 1);
                node_json_escape(fp, v->v.obj.pairs[i].key);
                fputs(": ", fp);
                node_emit_value(fp, v->v.obj.pairs[i].value, depth + 1);
                fputs(i + 1 < v->v.obj.count ? ",\n" : "\n", fp);
            }
            node_emit_indent(fp, depth);
            fputc('}', fp);
            break;
    }
}

/* Drop keys from `obj` whose names match any exclude pattern. Returns the
 * count of removed pairs. Mutates obj in place. */
static size_t node_filter_obj_pairs(json_value_t* obj,
                                    char* const* patterns, size_t n_patterns,
                                    const char* section_label) {
    if (!obj || obj->type != JSON_OBJECT) return 0;
    size_t write = 0, removed = 0;
    for (size_t i = 0; i < obj->v.obj.count; i++) {
        const char* key = obj->v.obj.pairs[i].key;
        int drop = 0;
        for (size_t p = 0; p < n_patterns; p++) {
            const char* pat = patterns[p];
            if (!pat || !*pat) continue;
            if (strncmp(pat, "ext-", 4) == 0) continue;
            if (strchr(pat, '/') || strchr(pat, '\\')) continue;
            if (strcmp(pat, key) == 0 || ub_glob_match(pat, key, 0)) {
                drop = 1; break;
            }
        }
        if (drop) {
            printf("  - %s.%s (excluded by config; not installed)\n", section_label, key);
            /* Leak the dropped pair's contents intentionally — json_free
             * walks v.obj.count entries up to `write` after we update it,
             * so dropped tails would otherwise be missed. Simpler: free
             * key + value here, then collapse. */
            free(obj->v.obj.pairs[i].key);
            json_free(obj->v.obj.pairs[i].value);
            removed++;
            continue;
        }
        if (write != i) obj->v.obj.pairs[write] = obj->v.obj.pairs[i];
        write++;
    }
    obj->v.obj.count = write;
    return removed;
}

/* Parse `path`, drop matching deps from each dependency-style section,
 * re-emit. Returns total removed; -1 on parse/IO error. */
static int nodejs_filter_package_json(const char* path,
                                      char* const* patterns, size_t n_patterns,
                                      size_t* out_removed) {
    *out_removed = 0;
    FILE* in = fopen(path, "rb");
    if (!in) return -1;
    fseek(in, 0, SEEK_END);
    long sz = ftell(in);
    if (sz < 0 || sz > 16 * 1024 * 1024) { fclose(in); return -1; }
    fseek(in, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(in); return -1; }
    if ((long)fread(buf, 1, (size_t)sz, in) != sz) { free(buf); fclose(in); return -1; }
    buf[sz] = 0;
    fclose(in);

    char err[256];
    json_value_t* root = json_parse(buf, (size_t)sz, err, sizeof(err));
    free(buf);
    if (!root || root->type != JSON_OBJECT) {
        if (root) json_free(root);
        fprintf(stderr, "Error: %s: not a JSON object\n", path);
        return -1;
    }

    /* json_obj_get returns const — but we need to mutate. The struct has
     * the pairs array directly accessible; locate by key and cast away
     * const since we own the tree. */
    const char* sections[] = {
        "dependencies", "devDependencies",
        "optionalDependencies", "peerDependencies"
    };
    size_t total = 0;
    for (size_t s = 0; s < sizeof(sections)/sizeof(sections[0]); s++) {
        for (size_t i = 0; i < root->v.obj.count; i++) {
            if (strcmp(root->v.obj.pairs[i].key, sections[s]) == 0) {
                total += node_filter_obj_pairs(root->v.obj.pairs[i].value,
                                               patterns, n_patterns, sections[s]);
                break;
            }
        }
    }

    if (total > 0) {
        /* `path` is a hardlink-share with the user's source (because
         * pc_copy_or_link_tree hardlinks when the dest is on the same FS).
         * Unlink before rewriting so the staged copy gets a fresh inode
         * and the source file stays pristine — keeping the staging
         * promise the bundle tests assert. */
        if (remove(path) != 0) {
            fprintf(stderr, "Error: could not unlink staged %s before rewrite\n", path);
            json_free(root);
            return -1;
        }
        FILE* out = fopen(path, "wb");
        if (!out) { json_free(root); return -1; }
        node_emit_value(out, root, 0);
        fputc('\n', out);
        fclose(out);
    }
    json_free(root);
    *out_removed = total;
    return 0;
}

static ub_result_t nodejs_maybe_stage_project_with_deps(const ub_config_t* config,
                                                       char** out_staged) {
    *out_staged = NULL;
    if (!config || config->no_install_deps) return UB_SUCCESS;

    char pkg_path[1024];
    char lock_path[1024];
    snprintf(pkg_path,  sizeof(pkg_path),  "%s/package.json",      config->project_dir);
    snprintf(lock_path, sizeof(lock_path), "%s/package-lock.json", config->project_dir);
    struct stat st;
    if (stat(pkg_path, &st) != 0 || !S_ISREG(st.st_mode)) return UB_SUCCESS;
    int have_lock = (stat(lock_path, &st) == 0 && S_ISREG(st.st_mode));

    char node_root[1024];
    if (nodejs_locate_runtime(config, node_root, sizeof(node_root)) != 0) {
        printf("note: package.json found but no vendored Node runtime is reachable;\n"
               "      bundling project as-is. `npm install --omit=dev` yourself, or run\n"
               "      `scripts/vendor-runtimes.sh node` to enable hermetic install.\n");
        return UB_SUCCESS;
    }

    const char* xdg = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    char stage[1024];
    if (xdg && *xdg)        snprintf(stage, sizeof(stage), "%s/ubuilder/stage/node-build-%d",        xdg,  (int)getpid());
    else if (home && *home) snprintf(stage, sizeof(stage), "%s/.cache/ubuilder/stage/node-build-%d", home, (int)getpid());
    else                    snprintf(stage, sizeof(stage), "/tmp/ubuilder-node-stage-%d",                  (int)getpid());

    pc_remove_tree(stage);

    printf("Staging Node.js project for dependency install:\n");
    printf("  source: %s\n", config->project_dir);
    printf("  stage:  %s\n", stage);
    if (pc_copy_or_link_tree(config->project_dir, stage) != 0) {
        fprintf(stderr, "Error: failed to stage project at %s\n", stage);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }

    /* --exclude: rewrite the STAGED package.json (never the user's source)
     * to drop matched deps. If anything changed, drop the staged lockfile
     * too — `npm ci` would refuse on a lockfile/package-json mismatch and
     * we'd rather fall through to `npm install`. */
    char staged_pkg[1280], staged_lock[1280];
    snprintf(staged_pkg,  sizeof(staged_pkg),  "%s/package.json",      stage);
    snprintf(staged_lock, sizeof(staged_lock), "%s/package-lock.json", stage);
    if (config && config->exclude_count > 0) {
        size_t removed = 0;
        if (nodejs_filter_package_json(staged_pkg, config->exclude,
                                       config->exclude_count, &removed) == 0
            && removed > 0) {
            printf("Filtered %zu dep(s) from staged package.json via --exclude\n", removed);
            if (have_lock) {
                if (remove(staged_lock) == 0) {
                    printf("  dropped staged package-lock.json (no longer in sync; npm install path)\n");
                    have_lock = 0;
                }
            }
        }
    }

    /* M8-fast: install-cache lookup before invoking npm. Key inputs:
     *   - runtime id: basename of node_root (e.g. node-v24.15.0-linux-x64)
     *   - manifest:   STAGED package.json (so --exclude rewrites get a
     *                 different cache key from a vanilla build).
     *   - lockfile:   staged package-lock.json when still in sync.
     */
    char hex_key[65];
    int cache_ok = (ub_install_cache_key(nodejs_path_basename(node_root),
                                         staged_pkg,
                                         have_lock ? staged_lock : NULL,
                                         hex_key) == 0);
    char staged_node_modules[1024];
    snprintf(staged_node_modules, sizeof(staged_node_modules), "%s/node_modules", stage);

    if (cache_ok && ub_install_cache_lookup("nodejs", hex_key, staged_node_modules) == 0) {
        printf("Install cache hit (nodejs/%.12s...) — skipped npm install.\n", hex_key);
        *out_staged = strdup(stage);
        return UB_SUCCESS;
    }

    /* Cache miss: run npm. `npm ci --omit=dev` when a lockfile is present
     * (faster, strict — item #3); fall back to `npm install --omit=dev`
     * otherwise. Both leave node_modules at <stage>/node_modules. */
    char node_exe[1024];
    snprintf(node_exe, sizeof(node_exe), "%s/bin/node", node_root);
    char npm_cli[1024];
    snprintf(npm_cli, sizeof(npm_cli), "%s/lib/node_modules/npm/bin/npm-cli.js", node_root);
    if (access(node_exe, X_OK) != 0 || access(npm_cli, R_OK) != 0) {
        fprintf(stderr, "Error: vendored Node missing bin/node or npm-cli.js at %s\n", node_root);
        pc_remove_tree(stage);
        return UB_ERROR_FILE_NOT_FOUND;
    }
    printf("Installing dependencies from %s into staged project%s\n",
           pkg_path, have_lock ? " (npm ci, lockfile mode)" : " (npm install)");
    printf("  node: %s\n  npm:  %s\n", node_exe, npm_cli);

    char* argv[10];
    int i = 0;
    argv[i++] = node_exe;
    argv[i++] = npm_cli;
    argv[i++] = have_lock ? (char*)"ci" : (char*)"install";
    argv[i++] = (char*)"--omit=dev";
    argv[i++] = (char*)"--no-audit";
    argv[i++] = (char*)"--no-fund";
    argv[i++] = (char*)"--no-progress";
    argv[i++] = NULL;

    int rc = pc_spawn_and_wait(node_exe, argv, NULL, stage);
    if (rc != 0) {
        fprintf(stderr, "Error: npm %s failed (exit %d). Bundle build aborted.\n",
                have_lock ? "ci" : "install", rc);
        pc_remove_tree(stage);
        return UB_ERROR_EXECUTION_FAILED;
    }

    /* Best-effort cache store. The freshly-installed node_modules is at
     * <stage>/node_modules; copy (hardlink) into the install cache. */
    if (cache_ok) {
        struct stat nst;
        if (stat(staged_node_modules, &nst) == 0 && S_ISDIR(nst.st_mode)) {
            if (ub_install_cache_store("nodejs", hex_key, staged_node_modules) == 0) {
                printf("Install cache stored (nodejs/%.12s...)\n", hex_key);
            }
        }
    }

    *out_staged = strdup(stage);
    return UB_SUCCESS;
}
#endif

// Embed Node.js application
static ub_result_t nodejs_embed_application(const ub_config_t* config, FILE* output_file) {
    struct stat st;

    g_node_exclude_pats  = config ? config->exclude       : NULL;
    g_node_exclude_count = config ? config->exclude_count : 0;

#ifndef PLATFORM_WINDOWS
    char* staged_project = NULL;
    ub_result_t srcrc = nodejs_maybe_stage_project_with_deps(config, &staged_project);
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

    uint32_t file_count_placeholder = 0;
    fwrite(&file_count_placeholder, sizeof(file_count_placeholder), 1, output_file);

    ub_result_t result = nodejs_embed_files_recursive(project_dir, project_dir, output_file);

    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);

#ifndef PLATFORM_WINDOWS
    if (staged_project) { pc_remove_tree(staged_project); free(staged_project); }
#endif

    g_node_exclude_pats  = NULL;
    g_node_exclude_count = 0;
    return result;
}

// Generate Node.js launcher
static ub_result_t nodejs_generate_launcher(FILE* output_file) {
    const char* launcher_code = 
        "// Node.js UBuilder Launcher\n"
        "const fs = require('fs');\n"
        "const path = require('path');\n"
        "// Launcher code will be here\n";
    
    uint32_t code_size = (uint32_t)strlen(launcher_code);
    fwrite(&code_size, sizeof(code_size), 1, output_file);
    fwrite(launcher_code, 1, code_size, output_file);
    
    return UB_SUCCESS;
}

// Required files for Node.js runtime
static const char* nodejs_required_files[] = {
    "node",
    NULL
};

// Supported file extensions
static const char* nodejs_supported_extensions[] = {
    ".js",
    ".mjs",
    ".json",
    NULL
};

// Node.js builder definition
const ub_runtime_builder_t nodejs_builder = {
    .runtime_type = UB_RUNTIME_NODEJS,
    .name = "Node.js",
    .description = "Node.js runtime builder (embeds full Node.js interpreter)",
    .validate_project = nodejs_validate_project,
    .embed_runtime = nodejs_embed_runtime,
    .embed_application = nodejs_embed_application,
    .generate_launcher = nodejs_generate_launcher,
    .estimated_runtime_size = 60 * 1024 * 1024, // ~60MB for Node.js binary
    .required_files = nodejs_required_files,
    .supported_extensions = nodejs_supported_extensions
};

#ifdef PLATFORM_WINDOWS
// Function to embed Windows Node.js runtime (multiple files format)
static ub_result_t nodejs_embed_windows_runtime(const char* nodejs_dir, FILE* output_file) {
    // Essential files needed for Node.js to run on Windows
    const char* essential_files[] = {
        "node.exe",
        "node.dll",            // Main Node.js engine (if exists)
        "node.lib",            // Node.js library (if exists)
        "npm",                 // NPM executable (if exists)
        "npm.cmd",             // NPM batch file
        NULL
    };
    
    printf("Embedding Windows Node.js runtime from: %s\n", nodejs_dir);
    
    size_t total_size = 0;
    int files_embedded = 0;
    
    // First, embed essential core files
    for (int i = 0; essential_files[i]; i++) {
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s\\%s", nodejs_dir, essential_files[i]);
        
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
        } else if (strcmp(essential_files[i], "node.exe") == 0) {
            // node.exe is required
            printf("  Warning: Required file not found: %s\n", file_path);
        }
    }
    
    // Also try to embed any DLL dependencies in the same directory
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char search_path[1024];
    
    snprintf(search_path, sizeof(search_path), "%s\\*.dll", nodejs_dir);
    find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            // Skip if this is already embedded as an essential file
            int is_essential = 0;
            for (int i = 0; essential_files[i]; i++) {
                if (strcmp(find_data.cFileName, essential_files[i]) == 0) {
                    is_essential = 1;
                    break;
                }
            }
            
            if (!is_essential) {
                char dll_path[1024];
                snprintf(dll_path, sizeof(dll_path), "%s\\%s", nodejs_dir, find_data.cFileName);
                
                struct stat st;
                if (stat(dll_path, &st) == 0) {
                    FILE* dll_file = fopen(dll_path, "rb");
                    if (dll_file) {
                        uint32_t name_len = (uint32_t)strlen(find_data.cFileName);
                        fwrite(&name_len, sizeof(name_len), 1, output_file);
                        fwrite(find_data.cFileName, 1, name_len, output_file);
                        
                        uint32_t file_size = (uint32_t)st.st_size;
                        fwrite(&file_size, sizeof(file_size), 1, output_file);
                        
                        char buffer[8192];
                        size_t bytes_read;
                        while ((bytes_read = fread(buffer, 1, sizeof(buffer), dll_file)) > 0) {
                            fwrite(buffer, 1, bytes_read, output_file);
                        }
                        
                        fclose(dll_file);
                        total_size += st.st_size;
                        files_embedded++;
                    }
                }
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    // Write end marker (empty filename)
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);
    
    printf("Windows Node.js runtime embedded: %d files, %.2f MB total\n", 
           files_embedded, total_size / (1024.0 * 1024.0));
    
    return UB_SUCCESS;
}
#endif
