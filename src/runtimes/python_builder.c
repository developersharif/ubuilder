#include "runtime_builder.h"
#include "runtime_embedder.h"
#include "../core/platform_compat.h"
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
    #define popen _popen
    #define pclose _pclose
#else
    #include <dirent.h>
    #include <unistd.h>
#endif

// Python runtime validation
static ub_result_t python_validate_project(const char* project_dir) {
    char main_py_path[1024];
    struct stat st;
    
    // Check for main.py or other Python files
    snprintf(main_py_path, sizeof(main_py_path), "%s/main.py", project_dir);
    
    if (stat(main_py_path, &st) == 0) {
        return UB_SUCCESS;
    }
    
    // Look for any .py files
    // TODO: Implement directory scanning for .py files
    
    return UB_ERROR_FILE_NOT_FOUND;
}

// Embed Python runtime (minimal embedded Python)
// Forward declarations
#ifdef PLATFORM_WINDOWS
static ub_result_t python_embed_windows_runtime(const char* python_dir, FILE* output_file);
static void python_embed_directory_recursive(const char* dir_path, const char* base_python_dir, FILE* output_file, size_t* total_size, int* files_embedded);
#endif

/* M1: Embed Python runtime.
 *
 * Three modes, in precedence:
 *   1. config->runtime_source is a directory  → tree embed (hermetic).
 *   2. config->runtime_source is a file       → single-binary tree (one
 *      record at bin/python3), with explicit user-chosen interpreter.
 *   3. config->runtime_source is NULL         → fall back to host probe
 *      and embed the host's /usr/bin/python3 as a 1-record tree. Print a
 *      "non-hermetic" hint pointing at scripts/vendor-runtimes.sh so the
 *      next build can be portable.
 *
 * On POSIX every path produces a V5 tree-format payload (uniform launcher
 * extraction). On Windows we still defer to the legacy multi-file embed.
 */
#ifndef PLATFORM_WINDOWS
/* M8: locate <runtime_dir>/lib/python3.X/site-packages by globbing the
 * versioned lib dir. python-build-standalone ships exactly one. Returns
 * 0 on success and writes the path into `out`, -1 if not found. */
static int python_find_site_packages(const char* runtime_dir, char* out, size_t out_cap) {
    char lib_dir[1024];
    snprintf(lib_dir, sizeof(lib_dir), "%s/lib", runtime_dir);
    DIR* d = opendir(lib_dir);
    if (!d) return -1;
    struct dirent* de;
    int rc = -1;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "python3.", 8) != 0) continue;
        int n = snprintf(out, out_cap, "%s/%s/site-packages", lib_dir, de->d_name);
        if (n < 0 || (size_t)n >= out_cap) continue;
        struct stat st;
        if (stat(out, &st) == 0 && S_ISDIR(st.st_mode)) { rc = 0; break; }
    }
    closedir(d);
    return rc;
}

/* M8: pip-install user deps into `target_dir` using the staged interpreter.
 *
 * Two manifest modes (M8 + item #3 lockfile reproducibility):
 *   - lockfile_path != NULL  → `pip install --no-deps -r <lockfile>`. The
 *     lockfile is expected to enumerate the resolved transitive graph
 *     (one pinned version per package), so transitive resolution is OFF.
 *   - lockfile_path == NULL  → `pip install -r <requirements_path>`. Pip
 *     resolves transitive deps each run; two builds a month apart may
 *     differ.
 *
 * `target_dir` is the pip --target= sink, an absolute path; the directory
 * is created by pip if missing. Callers use a scratch dir on the cache
 * filesystem so the result can be atomically moved into the install
 * cache afterwards.
 *
 * Returns UB_SUCCESS / UB_ERROR_EXECUTION_FAILED. */
static ub_result_t python_pip_install(const char* staged_runtime,
                                      const char* requirements_path,
                                      const char* lockfile_path,
                                      const char* target_dir) {
    char interpreter[1024];
    snprintf(interpreter, sizeof(interpreter), "%s/bin/python3", staged_runtime);
    struct stat st;
    if (stat(interpreter, &st) != 0) {
        DIR* d = opendir(staged_runtime);
        if (d) closedir(d);
        char bin_dir[1024];
        snprintf(bin_dir, sizeof(bin_dir), "%s/bin", staged_runtime);
        DIR* bd = opendir(bin_dir);
        if (bd) {
            struct dirent* de;
            while ((de = readdir(bd)) != NULL) {
                if (strncmp(de->d_name, "python3.", 8) == 0) {
                    snprintf(interpreter, sizeof(interpreter), "%s/%s", bin_dir, de->d_name);
                    break;
                }
            }
            closedir(bd);
        }
    }

    const char* manifest = lockfile_path ? lockfile_path : requirements_path;
    printf("Installing Python dependencies%s\n",
           lockfile_path ? " (lockfile mode, --no-deps)" : "");
    printf("  interpreter: %s\n  manifest:    %s\n  target:      %s\n",
           interpreter, manifest, target_dir);

    /* Build argv with optional --no-deps for lockfile mode. */
    char* argv[16];
    int i = 0;
    argv[i++] = interpreter;
    argv[i++] = (char*)"-m";
    argv[i++] = (char*)"pip";
    argv[i++] = (char*)"install";
    argv[i++] = (char*)"--disable-pip-version-check";
    argv[i++] = (char*)"--no-warn-script-location";
    if (lockfile_path) argv[i++] = (char*)"--no-deps";
    argv[i++] = (char*)"--target";
    argv[i++] = (char*)target_dir;
    argv[i++] = (char*)"-r";
    argv[i++] = (char*)manifest;
    argv[i++] = NULL;

    int rc = pc_spawn_and_wait(interpreter, argv, NULL, NULL);
    if (rc != 0) {
        fprintf(stderr, "Error: pip install failed (exit %d). Bundle build aborted.\n", rc);
        return UB_ERROR_EXECUTION_FAILED;
    }
    return UB_SUCCESS;
}

/* Return the trailing path segment of `path`. Returns a pointer into the
 * input string (caller must not free). For "/a/b/c" → "c"; for "c" → "c". */
static const char* path_basename_ptr(const char* path) {
    const char* p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/* M8 + M8-fast: stage the cached runtime, materialize user deps into its
 * site-packages, return the staged path.
 *
 * Dep materialization, in precedence:
 *   1. Install-cache hit → hardlink-merge cached deps into site-packages.
 *      No pip invocation. (Item #2: content-addressed install cache.)
 *   2. Install-cache miss → pip install --target=<scratch>; hardlink-merge
 *      scratch into site-packages; store scratch as the cache entry.
 *
 * If requirements.lock is present (item #3), it's preferred over
 * requirements.txt and pip is invoked with --no-deps for reproducibility.
 * Either way the lockfile (if present) participates in the cache key, so
 * `requirements.txt` and `requirements.lock` invalidate the cache
 * independently.
 *
 * Returns UB_SUCCESS with *out_staged NULL when there's no dep work to
 * do (caller embeds the cache tree directly). */
static ub_result_t python_maybe_stage_with_deps(const ub_config_t* config,
                                                const char* runtime_dir,
                                                char**      out_staged) {
    *out_staged = NULL;
    if (!config || config->no_install_deps) return UB_SUCCESS;

    char req_path[1024];
    char lock_path[1024];
    snprintf(req_path,  sizeof(req_path),  "%s/requirements.txt",  config->project_dir);
    snprintf(lock_path, sizeof(lock_path), "%s/requirements.lock", config->project_dir);
    struct stat st;
    int have_req  = (stat(req_path,  &st) == 0 && S_ISREG(st.st_mode));
    int have_lock = (stat(lock_path, &st) == 0 && S_ISREG(st.st_mode));
    if (!have_req && !have_lock) return UB_SUCCESS;

    /* Lockfile-only mode: still supported (no requirements.txt). The lockfile
     * doubles as the manifest passed to pip and the cache-key input. */
    const char* manifest_for_pip  = have_lock ? lock_path : req_path;
    const char* manifest_for_key  = have_req  ? req_path  : lock_path;
    const char* lockfile_for_pip  = have_lock ? lock_path : NULL;
    const char* lockfile_for_key  = have_lock ? lock_path : NULL;

    /* Build a staging path under the cache's parent so hardlinks work
     * (same filesystem). $XDG_CACHE_HOME/ubuilder/stage/build-<pid>. */
    const char* xdg = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    char stage[1024];
    if (xdg && *xdg)        snprintf(stage, sizeof(stage), "%s/ubuilder/stage/build-%d",        xdg,  (int)getpid());
    else if (home && *home) snprintf(stage, sizeof(stage), "%s/.cache/ubuilder/stage/build-%d", home, (int)getpid());
    else                    snprintf(stage, sizeof(stage), "/tmp/ubuilder-stage-%d",                  (int)getpid());

    pc_remove_tree(stage);

    printf("Staging hermetic Python tree for dependency install:\n");
    printf("  source: %s\n", runtime_dir);
    printf("  stage:  %s\n", stage);
    if (pc_copy_or_link_tree(runtime_dir, stage) != 0) {
        fprintf(stderr, "Error: failed to stage runtime at %s\n", stage);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }

    char site_packages[1024];
    if (python_find_site_packages(stage, site_packages, sizeof(site_packages)) != 0) {
        fprintf(stderr, "Error: could not locate site-packages in %s\n", stage);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }

    /* Cache key: runtime identity is the basename of the source runtime
     * dir (it's pinned by SHA in scripts/vendor-runtimes.sh, so the
     * basename uniquely identifies the build of Python). Manifest +
     * optional lockfile both feed the SHA. */
    char hex_key[65];
    int cache_ok = (ub_install_cache_key(path_basename_ptr(runtime_dir),
                                         manifest_for_key,
                                         lockfile_for_key,
                                         hex_key) == 0);
    if (cache_ok && ub_install_cache_lookup("python", hex_key, site_packages) == 0) {
        printf("Install cache hit (python/%.12s...) — skipped pip install.\n", hex_key);
        *out_staged = strdup(stage);
        return UB_SUCCESS;
    }

    /* Cache miss: pip-install into a scratch dir on the cache filesystem
     * so it can be moved into the cache by rename(2) on success. */
    char scratch[1024] = {0};
    int have_scratch = 0;
    if (cache_ok) {
        char cache_root[1024];
        if (xdg && *xdg)        snprintf(cache_root, sizeof(cache_root), "%s/ubuilder/install-cache/python",       xdg);
        else if (home && *home) snprintf(cache_root, sizeof(cache_root), "%s/.cache/ubuilder/install-cache/python", home);
        else                    cache_root[0] = 0;
        if (cache_root[0]) {
            pc_mkdir_p(cache_root);
            snprintf(scratch, sizeof(scratch), "%s/.scratch-%d", cache_root, (int)getpid());
            pc_remove_tree(scratch);
            have_scratch = 1;
        }
    }
    /* If we couldn't pick a scratch dir, fall back to installing directly
     * into the staged site-packages (legacy path). */
    const char* pip_target = have_scratch ? scratch : site_packages;

    ub_result_t rc = python_pip_install(stage, manifest_for_pip, lockfile_for_pip, pip_target);
    if (rc != UB_SUCCESS) {
        if (have_scratch) pc_remove_tree(scratch);
        pc_remove_tree(stage);
        return rc;
    }

    if (have_scratch) {
        if (ub_link_merge_tree(scratch, site_packages) != 0) {
            fprintf(stderr, "Error: failed to merge installed deps into site-packages\n");
            pc_remove_tree(scratch);
            pc_remove_tree(stage);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        /* Best-effort: persist the scratch dir as a cache entry. Failure
         * is non-fatal — the build already has its deps; next run will
         * just re-install instead of cache-hit. */
        if (ub_install_cache_store("python", hex_key, scratch) == 0) {
            printf("Install cache stored (python/%.12s...)\n", hex_key);
        }
        pc_remove_tree(scratch);
    }

    *out_staged = strdup(stage);
    return UB_SUCCESS;
}
#endif

static ub_result_t python_embed_runtime(const ub_config_t* config, FILE* output_file) {
    ub_result_t result;

#ifndef PLATFORM_WINDOWS
    /* M1 + M8: hermetic mode if --runtime-source points at a directory,
     * with optional dep-staging when requirements.txt is present. */
    if (config && config->runtime_source) {
        struct stat st;
        if (stat(config->runtime_source, &st) != 0) {
            fprintf(stderr, "Error: --runtime-source not found: %s\n", config->runtime_source);
            return UB_ERROR_FILE_NOT_FOUND;
        }
        if (S_ISDIR(st.st_mode)) {
            printf("Embedding hermetic Python tree: %s\n", config->runtime_source);
            char* staged = NULL;
            ub_result_t rc = python_maybe_stage_with_deps(config, config->runtime_source, &staged);
            if (rc != UB_SUCCESS) return rc;
            const char* src = staged ? staged : config->runtime_source;
            rc = ub_embed_runtime_tree(src, output_file);
            if (staged) { pc_remove_tree(staged); free(staged); }
            return rc;
        }
        if (S_ISREG(st.st_mode)) {
            printf("Embedding user-chosen Python binary: %s\n", config->runtime_source);
            return ub_embed_runtime_single_as_tree(config->runtime_source, "bin/python3", output_file);
        }
        fprintf(stderr, "Error: --runtime-source must be a file or directory\n");
        return UB_ERROR_INVALID_ARGS;
    }

    /* DX: auto-discover a vendored Python in the cache. If missing and the
     * user hasn't opted out (--no-auto-vendor / --use-host-runtime),
     * auto-spawn scripts/vendor-runtimes.sh to fetch it on first use. */
    if (config && !config->use_host_runtime) {
        char cached[1024];
        int cache_hit = (ub_runtime_cache_lookup("python", "bin/python3", cached, sizeof(cached)) == 0);
        if (!cache_hit && !config->no_auto_vendor) {
            if (ub_auto_vendor("python") == UB_SUCCESS) {
                cache_hit = (ub_runtime_cache_lookup("python", "bin/python3", cached, sizeof(cached)) == 0);
            }
        }
        if (cache_hit) {
            printf("Embedding hermetic Python tree (auto-discovered): %s\n", cached);
            char* staged = NULL;
            ub_result_t rc = python_maybe_stage_with_deps(config, cached, &staged);
            if (rc != UB_SUCCESS) return rc;
            const char* src = staged ? staged : cached;
            rc = ub_embed_runtime_tree(src, output_file);
            if (staged) { pc_remove_tree(staged); free(staged); }
            return rc;
        }
    }
#endif

    /* Fall back to host probe. */
    ub_runtime_info_t runtime_info;
    result = ub_detect_runtime_binary(UB_RUNTIME_PYTHON, &runtime_info);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Python runtime not found on system.\n"
                        "  Hint: run `scripts/vendor-runtimes.sh python` to vendor a\n"
                        "        hermetic Python, then pass --runtime-source=<cache-dir>.\n");
        return result;
    }

    printf("Embedding Python runtime: %s\n", runtime_info.binary_path);
    printf("Python version: %s\n", runtime_info.version_string ? runtime_info.version_string : "unknown");

#ifdef PLATFORM_WINDOWS
    /* Windows path still uses the legacy multi-file embed (V4 marker). */
    char python_dir[1024];
    strcpy(python_dir, runtime_info.binary_path);
    char* last_slash = strrchr(python_dir, '\\');
    if (last_slash) { *last_slash = '\0'; }
    else {
        fprintf(stderr, "Error: Invalid Python binary path format\n");
        ub_runtime_info_cleanup(&runtime_info);
        return UB_ERROR_INVALID_ARGS;
    }
    printf("Python directory: %s\n", python_dir);
    result = python_embed_windows_runtime(python_dir, output_file);
#else
    /* POSIX: emit a 1-record tree using the host binary at bin/python3.
     * Bundle is V5-format but uses the host's interpreter — NOT portable.
     * The hint above tells users how to fix it. */
    printf("Binary size: %.2f MB\n", runtime_info.binary_size / (1024.0 * 1024.0));
    printf("note: bundle will use host /usr/bin/python3 (non-portable).\n"
           "      Run `scripts/vendor-runtimes.sh python` for a hermetic bundle\n"
           "      (auto-discovered next build) or drop --use-host-runtime.\n");
    result = ub_embed_runtime_single_as_tree(runtime_info.binary_path, "bin/python3", output_file);
#endif

    ub_runtime_info_cleanup(&runtime_info);
    return result;
}

/* TU-local exclude state — published by python_embed_application around
 * the recursion. Single-threaded per build; safe. */
static char* const* g_py_exclude_pats = NULL;
static size_t       g_py_exclude_count = 0;

// Helper function to embed all Python files recursively
static ub_result_t python_embed_files_recursive(const char* dir_path, const char* base_path, FILE* output_file) {
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
            python_embed_files_recursive(full_path, base_path, output_file);
        } else {
            // Check if it's a Python file or other relevant file
            const char* ext = strrchr(find_data.cFileName, '.');
            if (ext && (strcmp(ext, ".py") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0)) {
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
                if (ub_path_excluded((const char* const*)g_py_exclude_pats,
                                     g_py_exclude_count, rel_dir, 1)) continue;
            }
            // Recursively process subdirectory
            python_embed_files_recursive(full_path, base_path, output_file);
        } else if (S_ISREG(st.st_mode)) {
            // Check if it's a Python file or other relevant file
            const char* ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".py") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0)) {
                // Calculate relative path from project root
                const char* rel_path = full_path + strlen(base_path);
                if (*rel_path == '/') rel_path++; // Skip leading slash
                if (ub_path_excluded((const char* const*)g_py_exclude_pats,
                                     g_py_exclude_count, rel_path, 0)) continue;
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

// Embed Python application
static ub_result_t python_embed_application(const ub_config_t* config, FILE* output_file) {
    const char* project_dir = config->project_dir;
    struct stat st;

    g_py_exclude_pats  = config ? config->exclude       : NULL;
    g_py_exclude_count = config ? config->exclude_count : 0;

    // Verify project directory exists
    if (stat(project_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    // Write number of files marker (we'll update this later if needed)
    uint32_t file_count_placeholder = 0;
    (void)file_count_placeholder; // Suppress unused warning
    long file_count_pos = ftell(output_file);
    (void)file_count_pos; // Suppress unused warning
    fwrite(&file_count_placeholder, sizeof(file_count_placeholder), 1, output_file);
    
    // Embed all Python and related files recursively
    ub_result_t result = python_embed_files_recursive(project_dir, project_dir, output_file);

    // Write end marker to indicate no more files
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);

    g_py_exclude_pats  = NULL;
    g_py_exclude_count = 0;
    return result;
}

// Generate Python launcher
static ub_result_t python_generate_launcher(FILE* output_file) {
    // Write Python-specific launcher code
    const char* launcher_code = 
        "# Python UBuilder Launcher\n"
        "import sys, os, tempfile\n"
        "# Launcher code will be here\n";
    
    uint32_t code_size = (uint32_t)strlen(launcher_code);
    fwrite(&code_size, sizeof(code_size), 1, output_file);
    fwrite(launcher_code, 1, code_size, output_file);
    
    return UB_SUCCESS;
}

// Required files for Python runtime
static const char* python_required_files[] = {
    "python3",
    "libpython3.so",
    NULL
};

// Supported file extensions
static const char* python_supported_extensions[] = {
    ".py",
    ".pyw",
    NULL
};

// Python builder definition
const ub_runtime_builder_t python_builder = {
    .runtime_type = UB_RUNTIME_PYTHON,
    .name = "Python",
    .description = "Python 3.x runtime builder (embeds full Python interpreter)",
    .validate_project = python_validate_project,
    .embed_runtime = python_embed_runtime,
    .embed_application = python_embed_application,
    .generate_launcher = python_generate_launcher,
    .estimated_runtime_size = 25 * 1024 * 1024, // ~25MB for Python binary
    .required_files = python_required_files,
    .supported_extensions = python_supported_extensions
};

#ifdef PLATFORM_WINDOWS
// Function to embed Windows Python runtime (multiple files format)
static ub_result_t python_embed_windows_runtime(const char* python_dir, FILE* output_file) {
    // Essential files needed for Python to run on Windows
    const char* essential_files[] = {
        "python.exe",
        "pythonw.exe",         // Windows version (if exists)
        "python39.dll",        // Python 3.9 DLL (adjust version as needed)
        "python310.dll",       // Python 3.10 DLL
        "python311.dll",       // Python 3.11 DLL
        "python312.dll",       // Python 3.12 DLL
        "python313.dll",       // Python 3.13 DLL
        "vcruntime140.dll",    // Visual C++ runtime
        "vcruntime140_1.dll",  // Additional VC runtime
        "msvcp140.dll",        // Microsoft Visual C++ runtime
        NULL
    };
    
    printf("Embedding Windows Python runtime from: %s\n", python_dir);
    
    size_t total_size = 0;
    int files_embedded = 0;
    
    // First, embed essential core files
    for (int i = 0; essential_files[i]; i++) {
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s\\%s", python_dir, essential_files[i]);
        
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
        } else if (strcmp(essential_files[i], "python.exe") == 0) {
            // python.exe is required
            printf("  Warning: Required file not found: %s\n", file_path);
        }
    }
    
    // Embed complete Python standard library (Lib directory) recursively
    char lib_dir[1024];
    snprintf(lib_dir, sizeof(lib_dir), "%s\\Lib", python_dir);
    
    struct stat lib_stat;
    if (stat(lib_dir, &lib_stat) == 0) {
        printf("  Embedding complete Python standard library...\n");
        
        // Use recursive function to embed ALL Lib directory contents
        python_embed_directory_recursive(lib_dir, python_dir, output_file, &total_size, &files_embedded);
    }
    
    // Embed complete site-packages (pip-installed packages) recursively  
    char site_packages_dir[1024];
    snprintf(site_packages_dir, sizeof(site_packages_dir), "%s\\Lib\\site-packages", python_dir);
    
    struct stat site_stat;
    if (stat(site_packages_dir, &site_stat) == 0) {
        printf("  Embedding all pip-installed packages...\n");
        
        // Use recursive function to embed ALL site-packages contents
        python_embed_directory_recursive(site_packages_dir, python_dir, output_file, &total_size, &files_embedded);
    }
    
    // Embed tcl directory for tkinter support
    char tcl_dir[1024];
    snprintf(tcl_dir, sizeof(tcl_dir), "%s\\tcl", python_dir);
    
    struct stat tcl_stat;
    if (stat(tcl_dir, &tcl_stat) == 0) {
        printf("  Embedding TCL/TK for tkinter support...\n");
        
        // Use recursive function to embed ALL tcl directory contents
        python_embed_directory_recursive(tcl_dir, python_dir, output_file, &total_size, &files_embedded);
    }
    
    // Embed Python extension modules and DLLs from DLLs directory (critical for stdlib and tkinter)
    char dlls_dir[1024];
    snprintf(dlls_dir, sizeof(dlls_dir), "%s\\DLLs", python_dir);
    
    struct stat dlls_stat;
    if (stat(dlls_dir, &dlls_stat) == 0) {
        printf("  Embedding Python extension modules and DLLs...\n");
        
        // Embed all *.pyd files (Python extensions)
        WIN32_FIND_DATAA dlls_find_data;
        HANDLE dlls_find_handle;
        char dlls_search_path[1024];
        
        snprintf(dlls_search_path, sizeof(dlls_search_path), "%s\\*.pyd", dlls_dir);
        dlls_find_handle = FindFirstFileA(dlls_search_path, &dlls_find_data);
        
        if (dlls_find_handle != INVALID_HANDLE_VALUE) {
            do {
                char pyd_path[1024];
                snprintf(pyd_path, sizeof(pyd_path), "%s\\%s", dlls_dir, dlls_find_data.cFileName);
                
                struct stat pyd_stat;
                if (stat(pyd_path, &pyd_stat) == 0) {
                    FILE* pyd_file = fopen(pyd_path, "rb");
                    if (pyd_file) {
                        // Store with DLLs\ prefix to maintain directory structure
                        char pyd_name[256];
                        snprintf(pyd_name, sizeof(pyd_name), "DLLs\\%s", dlls_find_data.cFileName);
                        
                        uint32_t name_len = (uint32_t)strlen(pyd_name);
                        fwrite(&name_len, sizeof(name_len), 1, output_file);
                        fwrite(pyd_name, 1, name_len, output_file);
                        
                        uint32_t file_size = (uint32_t)pyd_stat.st_size;
                        fwrite(&file_size, sizeof(file_size), 1, output_file);
                        
                        char buffer[8192];
                        size_t bytes_read;
                        while ((bytes_read = fread(buffer, 1, sizeof(buffer), pyd_file)) > 0) {
                            fwrite(buffer, 1, bytes_read, output_file);
                        }
                        
                        fclose(pyd_file);
                        total_size += pyd_stat.st_size;
                        files_embedded++;
                    }
                }
            } while (FindNextFileA(dlls_find_handle, &dlls_find_data));
            FindClose(dlls_find_handle);
        }
        
        // Also embed all *.dll files from DLLs directory (needed for tkinter: tcl86t.dll, tk86t.dll)
        snprintf(dlls_search_path, sizeof(dlls_search_path), "%s\\*.dll", dlls_dir);
        dlls_find_handle = FindFirstFileA(dlls_search_path, &dlls_find_data);
        
        if (dlls_find_handle != INVALID_HANDLE_VALUE) {
            do {
                char dll_path[1024];
                snprintf(dll_path, sizeof(dll_path), "%s\\%s", dlls_dir, dlls_find_data.cFileName);
                
                struct stat dll_stat;
                if (stat(dll_path, &dll_stat) == 0) {
                    FILE* dll_file = fopen(dll_path, "rb");
                    if (dll_file) {
                        // Store with DLLs\ prefix to maintain directory structure
                        char dll_name[256];
                        snprintf(dll_name, sizeof(dll_name), "DLLs\\%s", dlls_find_data.cFileName);
                        
                        uint32_t name_len = (uint32_t)strlen(dll_name);
                        fwrite(&name_len, sizeof(name_len), 1, output_file);
                        fwrite(dll_name, 1, name_len, output_file);
                        
                        uint32_t file_size = (uint32_t)dll_stat.st_size;
                        fwrite(&file_size, sizeof(file_size), 1, output_file);
                        
                        char buffer[8192];
                        size_t bytes_read;
                        while ((bytes_read = fread(buffer, 1, sizeof(buffer), dll_file)) > 0) {
                            fwrite(buffer, 1, bytes_read, output_file);
                        }
                        
                        fclose(dll_file);
                        total_size += dll_stat.st_size;
                        files_embedded++;
                    }
                }
            } while (FindNextFileA(dlls_find_handle, &dlls_find_data));
            FindClose(dlls_find_handle);
        }
    }
    
    // Also embed any additional DLL dependencies
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char search_path[1024];
    
    snprintf(search_path, sizeof(search_path), "%s\\*.dll", python_dir);
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
                snprintf(dll_path, sizeof(dll_path), "%s\\%s", python_dir, find_data.cFileName);
                
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
    
    printf("Windows Python runtime embedded: %d files, %.2f MB total\n", 
           files_embedded, total_size / (1024.0 * 1024.0));
    
    return UB_SUCCESS;
}

// Recursive function to embed entire directory trees (for complete Python stdlib and site-packages)
static void python_embed_directory_recursive(const char* dir_path, const char* base_python_dir, FILE* output_file, size_t* total_size, int* files_embedded) {
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char search_path[2048];
    
    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);
    find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        return;
    }
    
    do {
        // Skip . and .. directories
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, find_data.cFileName);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // Skip __pycache__ directories and other cache directories
                if (strstr(find_data.cFileName, "__pycache__") || 
                    strstr(find_data.cFileName, ".egg-info") ||
                    strstr(find_data.cFileName, ".dist-info")) {
                    continue;
                }
                
                // Recursively embed subdirectory
                python_embed_directory_recursive(full_path, base_python_dir, output_file, total_size, files_embedded);
            } else {
                // Skip certain file types that aren't needed at runtime
                const char* filename_lower = find_data.cFileName;
                if (strstr(filename_lower, ".pyc") ||      // Compiled bytecode (will be regenerated)
                    strstr(filename_lower, ".pyo") ||      // Optimized bytecode
                    strstr(filename_lower, ".exe") ||      // Skip executables inside packages
                    strstr(filename_lower, ".pdb") ||      // Debug symbols
                    strstr(filename_lower, ".lib") ||      // Static libraries
                    strstr(filename_lower, ".exp")) {      // Export files
                    continue;
                }
                
                // Calculate relative path from Python base directory
                const char* relative_path = full_path;
                if (strncmp(full_path, base_python_dir, strlen(base_python_dir)) == 0) {
                    relative_path = full_path + strlen(base_python_dir);
                    if (relative_path[0] == '\\') relative_path++; // Skip leading backslash
                }
                
                // Embed the file
                FILE* file = fopen(full_path, "rb");
                if (file) {
                    uint32_t name_len = (uint32_t)strlen(relative_path);
                    fwrite(&name_len, sizeof(name_len), 1, output_file);
                    fwrite(relative_path, 1, name_len, output_file);
                    
                    uint32_t file_size = (uint32_t)st.st_size;
                    fwrite(&file_size, sizeof(file_size), 1, output_file);
                    
                    char buffer[8192];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                        fwrite(buffer, 1, bytes_read, output_file);
                    }
                    
                    fclose(file);
                    *total_size += st.st_size;
                    (*files_embedded)++;
                }
            }
        }
    } while (FindNextFileA(find_handle, &find_data));
    
    FindClose(find_handle);
}
#endif
