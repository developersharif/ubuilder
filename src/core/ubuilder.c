#include "ubuilder.h"
#include "platform_compat.h"
#include "sha256.h"
#include "runtimes/runtime_builder.h"
#include "runtimes/runtime_embedder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
    #include <direct.h>
    #include <process.h>
    #include <io.h>
    #define mkdir(path, mode) _mkdir(path)
    #define getpid _getpid
    #define chdir _chdir
    #define getcwd _getcwd
    #define unlink _unlink
    #define rmdir _rmdir
    #define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <dirent.h>
#endif

#ifdef PLATFORM_MACOS
    #include <mach-o/dyld.h>
#endif

// Forward declarations
static ub_result_t copy_executable_template(const char* output_path);
static ub_result_t create_modular_executable(const ub_config_t* config);
static ub_result_t ub_run_modular_embedded_app(ub_runtime_type_t runtime, FILE* data_file, int argc, char* argv[]);

// Global state
static int g_initialized = 0;

// Version string
static const char* VERSION_STRING = "2.0.1";

// Platform names
static const char* PLATFORM_NAMES[] = {
    #ifdef PLATFORM_WINDOWS
    "windows"
    #elif defined(PLATFORM_MACOS)
    "macos"
    #else
    "linux"
    #endif
};

// Error messages
static const char* ERROR_MESSAGES[] = {
    [UB_SUCCESS] = "Success",
    [-UB_ERROR_INVALID_ARGS] = "Invalid arguments",
    [-UB_ERROR_FILE_NOT_FOUND] = "File not found",
    [-UB_ERROR_RUNTIME_NOT_FOUND] = "Runtime not found",
    [-UB_ERROR_EXTRACTION_FAILED] = "Resource extraction failed",
    [-UB_ERROR_EXECUTION_FAILED] = "Application execution failed",
    [-UB_ERROR_MEMORY_ALLOCATION] = "Memory allocation failed",
    [-UB_ERROR_UNKNOWN] = "Unknown error"
};

ub_result_t ub_init(void) {
    if (g_initialized) {
        return UB_SUCCESS;
    }
    
    // Initialize runtime builder system
    ub_result_t result = runtime_builder_init();
    if (result != UB_SUCCESS) {
        return result;
    }
    
    g_initialized = 1;
    return UB_SUCCESS;
}

ub_result_t ub_cleanup(void) {
    if (!g_initialized) {
        return UB_SUCCESS;
    }
    
    // Cleanup runtime builder system
    runtime_builder_cleanup();
    
    g_initialized = 0;
    return UB_SUCCESS;
}

const char* ub_get_version_string(void) {
    return VERSION_STRING;
}

const char* ub_get_platform_name(void) {
    return PLATFORM_NAMES[0];
}

const char* ub_error_string(ub_result_t error) {
    if (error >= 0) {
        return ERROR_MESSAGES[0];
    }
    
    int index = -error;
    size_t array_size = sizeof(ERROR_MESSAGES) / sizeof(ERROR_MESSAGES[0]);
    if ((size_t)index < array_size) {
        return ERROR_MESSAGES[index];
    }
    
    return "Unknown error";
}

ub_runtime_type_t ub_parse_runtime(const char* runtime_str) {
    if (!runtime_str) {
        return UB_RUNTIME_UNKNOWN;
    }
    
    if (strcmp(runtime_str, "python") == 0) {
        return UB_RUNTIME_PYTHON;
    } else if (strcmp(runtime_str, "php") == 0) {
        return UB_RUNTIME_PHP;
    } else if (strcmp(runtime_str, "node") == 0 || strcmp(runtime_str, "nodejs") == 0) {
        return UB_RUNTIME_NODEJS;
    }
    
    return UB_RUNTIME_UNKNOWN;
}

static ub_result_t create_directory_recursive(const char* path) {
    char* path_copy = strdup(path);
    if (!path_copy) {
        return UB_ERROR_MEMORY_ALLOCATION;
    }
    
    char* p = path_copy;
    
#ifdef PLATFORM_WINDOWS
    // Skip drive letter on Windows (e.g., "C:")
    if (strlen(p) > 2 && p[1] == ':') {
        p += 3; // Skip "C:\"
    }
    
    while (*p) {
        char* next_sep = strchr(p, '\\');
        if (next_sep) {
            *next_sep = '\0';
        }
        
        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            free(path_copy);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        if (next_sep) {
            *next_sep = '\\';
            p = next_sep + 1;
        } else {
            break;
        }
    }
#else
    // Skip leading slash on Unix systems
    if (*p == '/') {
        p++;
    }
    
    while (*p) {
        char* next_sep = strchr(p, '/');
        if (next_sep) {
            *next_sep = '\0';
        }
        
        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            free(path_copy);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        if (next_sep) {
            *next_sep = '/';
            p = next_sep + 1;
        } else {
            break;
        }
    }
#endif
    
    free(path_copy);
    return UB_SUCCESS;
}

ub_result_t ub_build_executable(const ub_config_t* config) {
    if (!config || !config->project_dir || !config->output_path) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    printf("Building executable for runtime: %d\n", config->runtime);
    printf("Project directory: %s\n", config->project_dir);
    printf("Output path: %s\n", config->output_path);
    
    // Use Phase 3 modular runtime system
    ub_result_t result = create_modular_executable(config);
    if (result != UB_SUCCESS) {
        return result;
    }
    
    printf("Successfully created executable: %s\n", config->output_path);
    return UB_SUCCESS;
}

ub_result_t ub_extract_runtime(ub_runtime_type_t runtime, const char* temp_dir) {
    if (!temp_dir) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    // Create temporary directory
    ub_result_t result = create_directory_recursive(temp_dir);
    if (result != UB_SUCCESS) {
        return result;
    }
    
    printf("Extracting runtime %d to: %s\n", runtime, temp_dir);
    
    // TODO: Implement runtime extraction
    // 1. Find embedded runtime resources
    // 2. Extract to temporary directory
    // 3. Set appropriate permissions
    
    return UB_SUCCESS;
}

ub_result_t ub_execute_application(const char* temp_dir, const char* entry_point, int argc, char** argv) {
    if (!temp_dir || !entry_point) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    // Suppress unused parameter warnings for future implementation
    (void)argc;
    (void)argv;
    
    printf("Executing application from: %s\n", temp_dir);
    printf("Entry point: %s\n", entry_point);
    
    // TODO: Implement application execution
    // 1. Construct runtime command
    // 2. Execute with proper environment
    // 3. Handle exit codes and cleanup
    
    return UB_SUCCESS;
}

// Function to check if current executable is a UBuilder app and run it
ub_result_t ub_check_and_run_embedded_app(int argc, char* argv[]) {
    // Quick check: if we have obvious CLI arguments, skip embedded app detection
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (argv[i] && (
                strstr(argv[i], "--version") || 
                strstr(argv[i], "--help") || 
                strstr(argv[i], "--project-dir") ||
                strstr(argv[i], "--runtime") ||
                strstr(argv[i], "--output") ||
                argv[i][0] == '-')) {
                // This looks like a CLI invocation, not an embedded app
                return UB_ERROR_RUNTIME_NOT_FOUND;
            }
        }
    }
    
    FILE* self_file;
    /* V4 trailer (audit S3): adds a 32-byte SHA-256 of the payload sitting
     * just before the offset+marker block. V3 bundles (which carried no
     * hash) are intentionally rejected — see the Apple-sandbox principle
     * in docs/architecture/ARCHITECTURE_AUDIT.md. */
    char modular_marker[] = "UBUILDER_MODULAR_V4_SHA256_MARKER";
    ub_runtime_type_t runtime;
    ub_result_t result = UB_ERROR_RUNTIME_NOT_FOUND;
    
    /* Get path to current executable (S4: via pc_executable_path). */
    char exe_path[1024];
    if (pc_executable_path(exe_path, sizeof(exe_path)) != 0) {
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    self_file = fopen(exe_path, "rb");
    if (!self_file) {
        // Unable to open executable - not an embedded app
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    // Get file size to avoid reading beyond bounds
    fseek(self_file, 0, SEEK_END);
    long file_size = ftell(self_file);
    
    // Check if file is too small to contain any markers
    if (file_size < (long)strlen(modular_marker) + 100) {  // Need at least marker + some data
        fclose(self_file);
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    // First try to find the new modular marker 
    // For true runtime embedding, search a larger area to ensure we find the marker
    long search_size = (file_size > 10240) ? 10240 : file_size; // Search last 10KB or entire file if smaller
    long search_start = file_size - search_size;
    if (search_start < 0) search_start = 0;
    
    // Allocate buffer for search
    char* search_buffer = malloc(search_size);
    if (!search_buffer) {
        fclose(self_file);
        return UB_ERROR_MEMORY_ALLOCATION;
    }
    
    fseek(self_file, search_start, SEEK_SET);
    size_t bytes_read = fread(search_buffer, 1, file_size - search_start, self_file);
    
    /* Look for the V4 modular marker.
     * Trailer layout (relative to marker start at search_buffer + i):
     *   [ ... payload ...                           ]                            ends at sha_pos
     *   [ 32 bytes SHA-256(payload) ]               at i - sizeof(uint64_t) - 32
     *   [ uint64_t data_start_offset ]              at i - sizeof(uint64_t)
     *   [ marker (35 bytes) ]                       at i
     *   [ uint32_t magic ]                          at i + len(marker)
     *   [ enum runtime ]                            at i + len(marker) + 4
     */
    const size_t mlen = strlen(modular_marker);
    for (size_t i = 0; i + mlen <= bytes_read; i++) {
        if (memcmp(search_buffer + i, modular_marker, mlen) != 0) continue;

        size_t magic_pos       = i + mlen;
        size_t runtime_pos     = magic_pos + sizeof(uint32_t);
        if (runtime_pos + sizeof(runtime) > bytes_read) continue;
        if (i < sizeof(uint64_t) + UB_SHA256_DIGEST_SIZE) continue;
        size_t data_offset_pos = i - sizeof(uint64_t);
        size_t sha_pos         = data_offset_pos - UB_SHA256_DIGEST_SIZE;

        /* Quick sanity gate. */
        uint32_t magic_number;
        memcpy(&magic_number, search_buffer + magic_pos, sizeof(magic_number));
        if (magic_number != 0xDEADBEEF) continue;

        memcpy(&runtime, search_buffer + runtime_pos, sizeof(runtime));
        if (runtime < UB_RUNTIME_PYTHON || runtime > UB_RUNTIME_NODEJS) continue;

        uint64_t data_start_offset_64;
        memcpy(&data_start_offset_64, search_buffer + data_offset_pos, sizeof(data_start_offset_64));
        long data_start_offset = (long)data_start_offset_64;

        /* Absolute file offset of the stored hash. */
        long sha_abs = search_start + (long)sha_pos;
        if (data_start_offset < 0 || sha_abs <= data_start_offset) continue;

        uint8_t stored_hash[UB_SHA256_DIGEST_SIZE];
        memcpy(stored_hash, search_buffer + sha_pos, UB_SHA256_DIGEST_SIZE);

        /* Integrity check (S3): SHA-256 over [data_start_offset, sha_abs).
         * On mismatch we refuse to run — no fallback, no warning-then-continue. */
        uint8_t actual_hash[UB_SHA256_DIGEST_SIZE];
        long    payload_len = sha_abs - data_start_offset;
        if (ub_sha256_file_range(self_file, data_start_offset, payload_len, actual_hash) != 0) {
            free(search_buffer);
            fclose(self_file);
            fprintf(stderr, "Error: Failed to compute payload hash for integrity check\n");
            return UB_ERROR_EXTRACTION_FAILED;
        }
        if (memcmp(stored_hash, actual_hash, UB_SHA256_DIGEST_SIZE) != 0) {
            char want_hex[65], got_hex[65];
            ub_sha256_hex(stored_hash, want_hex);
            ub_sha256_hex(actual_hash, got_hex);
            fprintf(stderr,
                "Error: Bundle integrity check FAILED — refusing to run.\n"
                "  Expected SHA-256: %s\n"
                "  Computed SHA-256: %s\n"
                "  The bundle has been modified, truncated, or corrupted since it was built.\n",
                want_hex, got_hex);
            free(search_buffer);
            fclose(self_file);
            return UB_ERROR_EXTRACTION_FAILED;
        }

        fseek(self_file, data_start_offset, SEEK_SET);
        result = ub_run_modular_embedded_app(runtime, self_file, argc, argv);
        free(search_buffer);
        fclose(self_file);
        return result;
    }
    
    /* Legacy v2 (UBUILDER_DATA_MARKER) format intentionally not supported here.
     * It used a host-runtime fallback (system("python3 ..."), etc.) which
     * silently masked extraction failures. Modular V3 is the only honest path. */
    free(search_buffer);
    
    fclose(self_file);
    // No embedded data found - this is normal for the CLI executable
    return UB_ERROR_RUNTIME_NOT_FOUND;
}

/* S4: get_temp_dir() collapsed into pc_temp_root() in platform_compat. */

/* S2 (audit): removed `ub_execute_embedded_app` and `ub_execute_script`.
 * Both were unconditional fallbacks to host runtimes via
 *   system("python3 ..."), system("php ..."), system("node ...")
 * which silently masked extraction failures on hosts that happened to have
 * the interpreter installed. The modular bundle path
 * (ub_run_modular_embedded_app -> ub_execute_script_with_embedded_runtime)
 * is the only execution path now; missing or broken embedded runtimes fail
 * with UB_ERROR_EXTRACTION_FAILED / UB_ERROR_EXECUTION_FAILED instead.
 * See docs/architecture/ARCHITECTURE_AUDIT.md §3 G3 and §4.1 S2. */

// Function to execute script with embedded runtime binary
/*
 * Execute the embedded interpreter against `script_path` with the user's
 * argv. Spawns directly via pc_spawn_and_wait (audit S1) — no shell, no
 * argv re-quoting, no 1024-byte cap.
 *
 * Runtime-specific argv/env construction:
 *   PHP    : argv = [exe, "-c", "<runtime_dir>/php.ini", script_name, user_args...]
 *            env  += PHP_INI_SCAN_DIR=""        (block host extension scan)
 *   Python : argv = [exe, script_name, user_args...]
 *            env  += PYTHONPATH=<runtime_dir>/Lib:<runtime_dir>/Lib/site-packages:$PYTHONPATH
 *   Node   : argv = [exe, script_name, user_args...]   (no env injection)
 *
 * The child is chdir'd into the directory containing the script so relative
 * imports/requires resolve against the project layout.
 */
static int ub_execute_script_with_embedded_runtime(ub_runtime_type_t runtime,
                                                   const char*       runtime_binary_path,
                                                   const char*       script_path,
                                                   int argc, char* argv[]) {
    /* Split script_path into <dir>/<name>. */
    char script_dir[1024];
    char script_name[256];
    if (strlen(script_path) >= sizeof(script_dir)) return -1;
    strcpy(script_dir, script_path);
#ifdef PLATFORM_WINDOWS
    char* last_slash = strrchr(script_dir, '\\');
#else
    char* last_slash = strrchr(script_dir, '/');
#endif
    const char* cwd_for_child = NULL;
    if (last_slash) {
        *last_slash = '\0';
        if (strlen(last_slash + 1) >= sizeof(script_name)) return -1;
        strcpy(script_name, last_slash + 1);
        cwd_for_child = script_dir;
    } else {
        if (strlen(script_path) >= sizeof(script_name)) return -1;
        strcpy(script_name, script_path);
    }

    /* Build the runtime directory (parent of the runtime binary) for
     * PYTHONPATH / php.ini resolution. */
    char runtime_dir[1024];
    if (strlen(runtime_binary_path) >= sizeof(runtime_dir)) return -1;
    strcpy(runtime_dir, runtime_binary_path);
#ifdef PLATFORM_WINDOWS
    char* rt_slash = strrchr(runtime_dir, '\\');
#else
    char* rt_slash = strrchr(runtime_dir, '/');
#endif
    if (rt_slash) *rt_slash = '\0'; else strcpy(runtime_dir, ".");

    /* Build argv array. Owned heap-side so the loop bound (argc) doesn't
     * leak into a fixed buffer. */
    int    extra = (runtime == UB_RUNTIME_PHP) ? 4 : 2;  /* exe + maybe ["-c","ini"] + script + NULL */
    int    user  = (argc > 1) ? (argc - 1) : 0;
    char** spawn_argv = (char**)calloc((size_t)(extra + user + 1), sizeof(char*));
    if (!spawn_argv) return -1;

    char php_ini_path[1024];
    int  ai = 0;
    spawn_argv[ai++] = (char*)runtime_binary_path;
    if (runtime == UB_RUNTIME_PHP) {
#ifdef PLATFORM_WINDOWS
        snprintf(php_ini_path, sizeof(php_ini_path), "%s\\php.ini", runtime_dir);
#else
        snprintf(php_ini_path, sizeof(php_ini_path), "%s/php.ini", runtime_dir);
#endif
        spawn_argv[ai++] = (char*)"-c";
        spawn_argv[ai++] = php_ini_path;
    }
    spawn_argv[ai++] = script_name;
    for (int i = 1; i < argc; i++) spawn_argv[ai++] = argv[i];
    spawn_argv[ai] = NULL;

    /* Build environment overlay. */
    char  pythonpath[2200];
    char* extra_env[3] = {NULL, NULL, NULL};
    int   ei = 0;
    if (runtime == UB_RUNTIME_PYTHON) {
        const char* host_pp = getenv("PYTHONPATH");
#ifdef PLATFORM_WINDOWS
        snprintf(pythonpath, sizeof(pythonpath),
                 "PYTHONPATH=%s\\Lib;%s\\Lib\\site-packages%s%s",
                 runtime_dir, runtime_dir,
                 host_pp ? ";" : "", host_pp ? host_pp : "");
#else
        snprintf(pythonpath, sizeof(pythonpath),
                 "PYTHONPATH=%s/Lib:%s/Lib/site-packages%s%s",
                 runtime_dir, runtime_dir,
                 host_pp ? ":" : "", host_pp ? host_pp : "");
#endif
        extra_env[ei++] = pythonpath;
    } else if (runtime == UB_RUNTIME_PHP) {
        /* Empty PHP_INI_SCAN_DIR isolates from host extension scan. */
        extra_env[ei++] = (char*)"PHP_INI_SCAN_DIR=";
    }
    extra_env[ei] = NULL;

    char** env = pc_env_overlay(extra_env);
    int result = pc_spawn_and_wait(runtime_binary_path, spawn_argv, env, cwd_for_child);
    pc_env_free(env);
    free(spawn_argv);

    if (result < 0) {
        fprintf(stderr, "Error: failed to spawn embedded %s runtime at %s\n",
                runtime == UB_RUNTIME_PYTHON ? "python" :
                runtime == UB_RUNTIME_PHP    ? "php"    :
                runtime == UB_RUNTIME_NODEJS ? "node"   : "unknown",
                runtime_binary_path);
    }
    return result;
}

/* S6 (audit): `validate_project_directory` was a dead static — the per-
 * builder `validate_project()` (e.g., python_builder.validate_project)
 * supersedes it. Deleted with its forward decl. */

// Helper function to copy the current executable as a template
static ub_result_t copy_executable_template(const char* output_path) {
    FILE *src = NULL, *dst = NULL;
    char buffer[8192];
    size_t bytes_read;
    ub_result_t result = UB_SUCCESS;
    
    /* S4: collapsed via pc_executable_path. */
    char exe_path[1024];
    if (pc_executable_path(exe_path, sizeof(exe_path)) != 0) {
        return UB_ERROR_FILE_NOT_FOUND;
    }

    src = fopen(exe_path, "rb");
    if (!src) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    dst = fopen(output_path, "wb");
    if (!dst) {
        fclose(src);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Copy the executable
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
            result = UB_ERROR_EXTRACTION_FAILED;
            break;
        }
    }
    
    fclose(src);
    fclose(dst);
    
    if (result == UB_SUCCESS) {
        // Make the output executable
#ifndef PLATFORM_WINDOWS
        chmod(output_path, 0755);
#endif
    }
    
    return result;
}

/* S6 (audit): `embed_project_files` + `embed_directory_recursive` deleted.
 * They wrote the legacy v2 `UBUILDER_DATA_MARKER` format, which S2 already
 * removed the *reader* for. They were never wired to a caller in modular
 * V3/V4 builds (the per-builder `embed_application` does this work today). */

// Main function to create modular runtime-specific executable (Phase 3)
static ub_result_t create_modular_executable(const ub_config_t* config) {
    ub_runtime_builder_t* builder;
    ub_result_t result;
    
    // Get runtime-specific builder
    result = runtime_builder_get(config->runtime, &builder);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Runtime builder not found for runtime %d\n", config->runtime);
        return result;
    }
    
    // Validate project using runtime-specific validation
    result = builder->validate_project(config->project_dir);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Project validation failed for %s runtime\n", builder->name);
        return result;
    }
    
    printf("Using %s runtime builder (%s)\n", builder->name, builder->description);
    printf("Estimated runtime size: %.1f MB\n", builder->estimated_runtime_size / (1024.0 * 1024.0));
    
    // 1. Copy current executable as template
    result = copy_executable_template(config->output_path);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Failed to copy executable template\n");
        return result;
    }
    
    // 2. Open output file for appending runtime-specific data
    FILE* output_file = fopen(config->output_path, "ab");
    if (!output_file) {
        fprintf(stderr, "Error: Failed to open output file for writing\n");
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Get the current file size to know where embedded data starts
    fseek(output_file, 0, SEEK_END);
    long data_start_offset = ftell(output_file);
    
    // 3. Embed runtime-specific runtime
    result = builder->embed_runtime(config, output_file);
    if (result != UB_SUCCESS) {
        fclose(output_file);
        fprintf(stderr, "Error: Failed to embed %s runtime\n", builder->name);
        return result;
    }
    
    // 4. Embed application using runtime-specific method
    result = builder->embed_application(config, output_file);
    if (result != UB_SUCCESS) {
        fclose(output_file);
        fprintf(stderr, "Error: Failed to embed application for %s runtime\n", builder->name);
        return result;
    }
    
    // 5. Generate runtime-specific launcher
    result = builder->generate_launcher(output_file);
    if (result != UB_SUCCESS) {
        fclose(output_file);
        fprintf(stderr, "Error: Failed to generate launcher for %s runtime\n", builder->name);
        return result;
    }
    
    /* 6. Compute SHA-256 over the payload [data_start_offset, payload_end).
     * Per the Apple-sandbox principle: integrity at every boundary. The
     * hash is verified before extraction at launch time; any tamper or
     * truncation refuses to run instead of best-efforting through it. */
    long payload_end = ftell(output_file);
    if (payload_end < data_start_offset) {
        fclose(output_file);
        fprintf(stderr, "Error: Inconsistent payload bounds\n");
        return UB_ERROR_UNKNOWN;
    }
    fclose(output_file);

    FILE* hash_in = fopen(config->output_path, "rb");
    if (!hash_in) {
        fprintf(stderr, "Error: Failed to reopen output file for hashing\n");
        return UB_ERROR_EXTRACTION_FAILED;
    }
    uint8_t payload_hash[UB_SHA256_DIGEST_SIZE];
    long    payload_len = payload_end - data_start_offset;
    if (ub_sha256_file_range(hash_in, data_start_offset, payload_len, payload_hash) != 0) {
        fclose(hash_in);
        fprintf(stderr, "Error: Failed to compute payload SHA-256\n");
        return UB_ERROR_EXTRACTION_FAILED;
    }
    fclose(hash_in);

    /* 7. Append the trailer. New V4 layout (S3):
     *
     *   [ payload ]
     *   [ 32 bytes SHA-256(payload) ]
     *   [ uint64_t data_start_offset ]
     *   [ "UBUILDER_MODULAR_V4_SHA256_MARKER" ]
     *   [ uint32_t magic 0xDEADBEEF ]
     *   [ enum runtime ]
     */
    output_file = fopen(config->output_path, "ab");
    if (!output_file) {
        fprintf(stderr, "Error: Failed to reopen output file to write trailer\n");
        return UB_ERROR_EXTRACTION_FAILED;
    }
    fwrite(payload_hash, 1, UB_SHA256_DIGEST_SIZE, output_file);

    uint64_t data_start_offset_64 = (uint64_t)data_start_offset;
    fwrite(&data_start_offset_64, sizeof(data_start_offset_64), 1, output_file);

    const char* modular_marker = "UBUILDER_MODULAR_V4_SHA256_MARKER";
    fwrite(modular_marker, 1, strlen(modular_marker), output_file);

    uint32_t magic_number = 0xDEADBEEF;
    fwrite(&magic_number, sizeof(magic_number), 1, output_file);

    fwrite(&config->runtime, sizeof(config->runtime), 1, output_file);

    fclose(output_file);

    if (config->verbose) {
        char hex[65];
        ub_sha256_hex(payload_hash, hex);
        printf("Payload SHA-256: %s\n", hex);
    }
    printf("Successfully created modular %s executable: %s\n", builder->name, config->output_path);
    return UB_SUCCESS;
}

// Function to run Phase 3 modular embedded application
static ub_result_t ub_run_modular_embedded_app(ub_runtime_type_t runtime, FILE* data_file, int argc, char* argv[]) {
    char temp_dir[512];
    char main_script_path[1024];
    char runtime_binary_path[1024];
    ub_result_t result;
    
    // Create temporary directory
    const char* base_temp_dir = pc_temp_root();
#ifdef PLATFORM_WINDOWS
    snprintf(temp_dir, sizeof(temp_dir), "%s\\ubuilder-%d", base_temp_dir, getpid());
#else
    snprintf(temp_dir, sizeof(temp_dir), "%s/ubuilder-%d", base_temp_dir, getpid());
#endif
    if (mkdir(temp_dir, 0755) != 0 && errno != EEXIST) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Extract embedded runtime binary
#ifdef PLATFORM_WINDOWS
    if (runtime == UB_RUNTIME_PHP) {
        result = ub_extract_windows_php_runtime(data_file, temp_dir, runtime_binary_path);
    } else if (runtime == UB_RUNTIME_NODEJS) {
        result = ub_extract_windows_nodejs_runtime(data_file, temp_dir, runtime_binary_path);
    } else if (runtime == UB_RUNTIME_PYTHON) {
        result = ub_extract_windows_python_runtime(data_file, temp_dir, runtime_binary_path);
    } else {
        result = ub_extract_runtime_binary(data_file, temp_dir, runtime_binary_path);
    }
#else
    /* M1: POSIX Python bundles use the V5 tree format. Detect the magic by
     * peeking the first 4 bytes; fall back to the legacy single-binary
     * extractor for runtimes that haven't migrated yet (PHP, Node). */
    long peek_pos = ftell(data_file);
    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, data_file) != 1) return UB_ERROR_EXTRACTION_FAILED;
    if (fseek(data_file, peek_pos, SEEK_SET) != 0)       return UB_ERROR_EXTRACTION_FAILED;
    if (magic == UB_RUNTIME_TREE_MAGIC) {
        /* Tree-format runtime. Extract to <temp_dir>/runtime/ and point
         * runtime_binary_path at <temp_dir>/runtime/bin/<rt>. */
        char tree_root[1024];
        snprintf(tree_root, sizeof(tree_root), "%s/runtime", temp_dir);
        result = ub_extract_runtime_tree(data_file, tree_root);
        if (result == UB_SUCCESS) {
            const char* bin_name =
                runtime == UB_RUNTIME_PYTHON ? "bin/python3" :
                runtime == UB_RUNTIME_PHP    ? "bin/php"     :
                runtime == UB_RUNTIME_NODEJS ? "bin/node"    : NULL;
            if (!bin_name) return UB_ERROR_RUNTIME_NOT_FOUND;
            snprintf(runtime_binary_path, 1024, "%s/%s", tree_root, bin_name);
            /* python-build-standalone ships `bin/python3` as a symlink to a
             * versioned binary which we couldn't replicate during extract
             * (symlinks are followed at embed time). If python3 is missing
             * but `bin/python3.X` exists, fall through to that. */
            struct stat st;
            if (stat(runtime_binary_path, &st) != 0) {
                DIR* d = opendir(tree_root);
                if (d) {
                    /* No-op: tree_root/bin contains versioned names — try one. */
                    closedir(d);
                }
                char alt[1024];
                snprintf(alt, sizeof(alt), "%s/bin", tree_root);
                DIR* bd = opendir(alt);
                if (bd) {
                    struct dirent* de;
                    while ((de = readdir(bd)) != NULL) {
                        if (strncmp(de->d_name, "python3.", 8) == 0) {
                            snprintf(runtime_binary_path, 1024, "%s/%s", alt, de->d_name);
                            break;
                        }
                    }
                    closedir(bd);
                }
            }
        }
    } else {
        result = ub_extract_runtime_binary(data_file, temp_dir, runtime_binary_path);
    }
#endif
    if (result != UB_SUCCESS) {
        return result;
    }
    
    // For PHP runtime, extract extensions and create custom php.ini
    if (runtime == UB_RUNTIME_PHP) {
#ifdef PLATFORM_WINDOWS
        // Windows PHP runtime already includes extensions, skip separate extraction
#else
        result = ub_extract_php_extensions(data_file, temp_dir);
        if (result != UB_SUCCESS) {
            printf("Warning: Failed to extract PHP extensions, continuing...\n");
            // Continue anyway - the app might work without extensions
        }
#endif
    }
    
    // Read file count placeholder (skip it for now)
    uint32_t file_count_placeholder;
    if (fread(&file_count_placeholder, sizeof(file_count_placeholder), 1, data_file) != 1) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Extract all embedded files
    int files_extracted = 0;
    char first_php_file[1024] = "";
    
    while (1) {
        uint32_t path_len;
        
        // Read path length (0 means end of files)
        if (fread(&path_len, sizeof(path_len), 1, data_file) != 1) {
            break;
        }
        
        if (path_len == 0) {
            // End marker reached
            break;
        }
        
        // Read relative file path
        char* rel_path = malloc(path_len + 1);
        if (!rel_path) {
            return UB_ERROR_MEMORY_ALLOCATION;
        }
        
        if (fread(rel_path, 1, path_len, data_file) != path_len) {
            free(rel_path);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        rel_path[path_len] = '\0';
        
        // Read file size
        uint32_t file_size;
        if (fread(&file_size, sizeof(file_size), 1, data_file) != 1) {
            free(rel_path);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        // Create full path in temp directory
        char full_path[1024];
#ifdef PLATFORM_WINDOWS
        snprintf(full_path, sizeof(full_path), "%s\\%s", temp_dir, rel_path);
        
        // Convert forward slashes to backslashes in rel_path for Windows
        for (char* p = full_path; *p; p++) {
            if (*p == '/') *p = '\\';
        }
#else
        snprintf(full_path, sizeof(full_path), "%s/%s", temp_dir, rel_path);
#endif
        
        // Create directory structure if needed
        char* dir_path = strdup(full_path);
#ifdef PLATFORM_WINDOWS
        char* last_slash = strrchr(dir_path, '\\');
#else
        char* last_slash = strrchr(dir_path, '/');
#endif
        if (last_slash && last_slash != dir_path) {
            *last_slash = '\0';
            create_directory_recursive(dir_path);
        }
        free(dir_path);
        
        // Extract file content
        FILE* output_file = fopen(full_path, "wb");
        if (!output_file) {
            free(rel_path);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        char buffer[8192];
        uint32_t remaining = file_size;
        while (remaining > 0) {
            size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
            size_t bytes_read = fread(buffer, 1, to_read, data_file);
            if (bytes_read == 0) break;
            fwrite(buffer, 1, bytes_read, output_file);
            remaining -= (uint32_t)bytes_read;
        }
        fclose(output_file);
        
        // Remember main.php specifically, or the first PHP file as fallback
        if (runtime == UB_RUNTIME_PHP) {
            const char* ext = strrchr(rel_path, '.');
            if (ext && strcmp(ext, ".php") == 0) {
                // Prefer main.php over other files
                if (strstr(rel_path, "main.php") != NULL || strlen(first_php_file) == 0) {
                    strcpy(first_php_file, full_path);
                }
            }
        }
        
        free(rel_path);
        files_extracted++;
    }
    
    if (files_extracted == 0) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    // Determine main script path
    switch (runtime) {
        case UB_RUNTIME_PYTHON:
#ifdef PLATFORM_WINDOWS
            snprintf(main_script_path, sizeof(main_script_path), "%s\\main.py", temp_dir);
#else
            snprintf(main_script_path, sizeof(main_script_path), "%s/main.py", temp_dir);
#endif
            break;
        case UB_RUNTIME_PHP:
            if (strlen(first_php_file) > 0) {
                strcpy(main_script_path, first_php_file);
            } else {
#ifdef PLATFORM_WINDOWS
                snprintf(main_script_path, sizeof(main_script_path), "%s\\main.php", temp_dir);
#else
                snprintf(main_script_path, sizeof(main_script_path), "%s/main.php", temp_dir);
#endif
            }
            break;
        case UB_RUNTIME_NODEJS:
#ifdef PLATFORM_WINDOWS
            snprintf(main_script_path, sizeof(main_script_path), "%s\\main.js", temp_dir);
#else
            snprintf(main_script_path, sizeof(main_script_path), "%s/main.js", temp_dir);
#endif
            break;
        default:
            return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    // Execute the script using embedded runtime
    int exit_code = ub_execute_script_with_embedded_runtime(runtime, runtime_binary_path, main_script_path, argc, argv);
    
    /* S1: structured recursive remove instead of system("rm -rf …") /
     * system("rmdir /s /q …"). Drops the /bin/sh & external-tool dependency
     * and is safe with paths containing spaces, quotes, or shell metachars. */
    if (pc_remove_tree(temp_dir) != 0) {
        fprintf(stderr, "Warning: Failed to clean up temporary directory: %s\n", temp_dir);
    }
    
    return (exit_code == 0) ? UB_SUCCESS : UB_ERROR_EXECUTION_FAILED;
}

/* S2 (audit): removed `ub_run_legacy_embedded_app`. It was the only reader
 * of the v2 (UBUILDER_DATA_MARKER) format, which embedded just the user's
 * script bytes and relied on a host interpreter via system(). Modern bundles
 * use the modular V3 format with embedded interpreter binaries. */
