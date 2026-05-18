#include "runtime_embedder.h"
#include "../core/platform_compat.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #include <process.h>  // For _access constants
    #define access _access
    #define mkdir(path, mode) _mkdir(path)
    #define readlink(path, buf, size) (-1)  // Not available on Windows
    #define lstat stat  // Use stat instead of lstat on Windows
    #define S_ISLNK(mode) (0)  // No symbolic links concept on Windows like Unix
    #define unlink _unlink
    #define chmod _chmod
    // Define access mode constants for Windows
    #ifndef F_OK
        #define F_OK 0  // Test for existence
    #endif
    #ifndef R_OK
        #define R_OK 4  // Test for read permission
    #endif
    #ifndef W_OK
        #define W_OK 2  // Test for write permission
    #endif
    #ifndef X_OK
        #define X_OK 1  // Test for execute permission
    #endif
    #ifdef _MSC_VER
        typedef ptrdiff_t ssize_t;  // Define ssize_t for MSVC
        #define strdup _strdup
    #endif
#else
    #include <unistd.h>
    #include <dirent.h>
#endif

/* S4/S5 (audit): the old `execute_command` was a popen() shell-out used to
 * discover host runtimes (e.g. `which python3`, `python3 --version`). It
 * violated the Apple-sandbox rule on two counts: shell invocation and
 * host-tool probing.
 *
 * Replaced with structured calls:
 *   - pc_path_lookup(exe)  — searches $PATH directly, no shell.
 *   - pc_spawn_capture(...) — runs the binary via posix_spawn / CreateProcess
 *                             with stdout piped, no shell quoting.
 *
 * Note: build-time host probing is still non-hermetic by nature; M1
 * (vendored hermetic interpreters) is the real fix. These helpers are
 * a stopgap that at least removes the /bin/sh dependency. */

static char* capture_version(const char* exe) {
    char* out = NULL;
    char* argv[] = { (char*)exe, (char*)"--version", NULL };
    int rc = pc_spawn_capture(exe, argv, NULL, NULL, 1024, &out);
    if (rc != 0 || !out) {
        free(out);
        return NULL;
    }
    /* Trim to the first line so we don't carry a multi-line PHP banner. */
    char* nl = strchr(out, '\n');
    if (nl) *nl = 0;
    return out;
}

// Function to get file size
static size_t get_file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return st.st_size;
}

// Function to resolve symbolic links to actual binary
static char* resolve_binary_path(const char* path) {
    char* resolved_path = malloc(1024);
    if (!resolved_path) return NULL;
    
    ssize_t len = readlink(path, resolved_path, 1023);
    if (len == -1) {
        // Not a symlink, return original path
        strcpy(resolved_path, path);
        return resolved_path;
    }
    
    resolved_path[len] = '\0';
    
    // If it's a relative path, we need to resolve it relative to the original path
#ifdef PLATFORM_WINDOWS
    if (resolved_path[0] != '/' && !(strlen(resolved_path) > 1 && resolved_path[1] == ':')) {
        // Relative path on Windows (not absolute like C:\path or /path)
        char* dir = strdup(path);
        char* last_slash = strrchr(dir, '\\');
        if (!last_slash) last_slash = strrchr(dir, '/'); // Handle mixed separators
        if (last_slash) {
            *last_slash = '\0';
            char* full_path = malloc(1024);
            snprintf(full_path, 1024, "%s\\%s", dir, resolved_path);
            free(dir);
            free(resolved_path);
            
            // Recursively resolve in case of chained symlinks
            return resolve_binary_path(full_path);
        }
        free(dir);
    }
#else
    if (resolved_path[0] != '/') {
        char* dir = strdup(path);
        char* last_slash = strrchr(dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            char* full_path = malloc(1024);
            snprintf(full_path, 1024, "%s/%s", dir, resolved_path);
            free(dir);
            free(resolved_path);
            
            // Recursively resolve in case of chained symlinks
            return resolve_binary_path(full_path);
        }
        free(dir);
    }
#endif
    
    // For absolute paths, recursively resolve in case of chained symlinks
    struct stat st;
    if (lstat(resolved_path, &st) == 0 && S_ISLNK(st.st_mode)) {
        char* final_path = resolve_binary_path(resolved_path);
        free(resolved_path);
        return final_path;
    }
    
    return resolved_path;
}

/* Detect a host runtime binary by name. Build-time only (build is non-
 * hermetic until M1 lands); the runtime path never reaches this. */
ub_result_t ub_detect_runtime_binary(ub_runtime_type_t runtime, ub_runtime_info_t* info) {
    if (!info) return UB_ERROR_INVALID_ARGS;
    memset(info, 0, sizeof(ub_runtime_info_t));

    const char* binary_name;
    /* Candidates to probe on PATH, in priority order. */
    const char* candidates[3];
    int         ncand = 0;

    switch (runtime) {
        case UB_RUNTIME_PHP:
            binary_name   = "php";
            candidates[ncand++] = "php";
            break;
        case UB_RUNTIME_PYTHON:
            binary_name   = "python";
            candidates[ncand++] = "python3";
            candidates[ncand++] = "python";
            break;
        case UB_RUNTIME_NODEJS:
            binary_name   = "node";
            candidates[ncand++] = "node";
            break;
        default:
            return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    char* binary_path = NULL;
    for (int i = 0; i < ncand; i++) {
        binary_path = pc_path_lookup(candidates[i]);
        if (binary_path) break;
    }
    if (!binary_path) return UB_ERROR_RUNTIME_NOT_FOUND;

    /* Resolve symlinks so we embed the actual binary, not the link. */
    char* resolved_path = resolve_binary_path(binary_path);
    free(binary_path);
    if (!resolved_path) return UB_ERROR_RUNTIME_NOT_FOUND;

#ifdef PLATFORM_WINDOWS
    if (access(resolved_path, 0) != 0) {
#else
    if (access(resolved_path, X_OK) != 0) {
#endif
        free(resolved_path);
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    /* Best-effort version banner — not load-bearing, used only for logging. */
    char* version = capture_version(resolved_path);

    size_t size = get_file_size(resolved_path);
    if (size == 0) {
        free(resolved_path);
        free(version);
        return UB_ERROR_FILE_NOT_FOUND;
    }

    info->binary_path    = resolved_path;
    info->binary_name    = strdup(binary_name);
    info->binary_size    = size;
    info->version_string = version;
    return UB_SUCCESS;
}

// Function to embed runtime binary into output file
ub_result_t ub_embed_runtime_binary(const char* binary_path, FILE* output_file) {
    if (!binary_path || !output_file) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    FILE* binary_file = fopen(binary_path, "rb");
    if (!binary_file) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    // Get file size
    fseek(binary_file, 0, SEEK_END);
    size_t file_size = ftell(binary_file);
    fseek(binary_file, 0, SEEK_SET);
    
    // Write size header
    fwrite(&file_size, sizeof(file_size), 1, output_file);
    
    // Copy binary data
    char buffer[8192];
    size_t remaining = file_size;
    
    while (remaining > 0) {
        size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        size_t bytes_read = fread(buffer, 1, to_read, binary_file);
        
        if (bytes_read == 0) {
            fclose(binary_file);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        fwrite(buffer, 1, bytes_read, output_file);
        remaining -= bytes_read;
    }
    
    fclose(binary_file);
    return UB_SUCCESS;
}

// Function to extract embedded runtime binary
ub_result_t ub_extract_runtime_binary(FILE* input_file, const char* temp_dir, char* extracted_path) {
    if (!input_file || !temp_dir || !extracted_path) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    // Read size header
    size_t file_size;
    if (fread(&file_size, sizeof(file_size), 1, input_file) != 1) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Create output path
#ifdef PLATFORM_WINDOWS
    snprintf(extracted_path, 1024, "%s\\runtime_binary.exe", temp_dir);
#else
    snprintf(extracted_path, 1024, "%s/runtime_binary", temp_dir);
#endif
    
    FILE* output_file = fopen(extracted_path, "wb");
    if (!output_file) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Extract binary data
    char buffer[8192];
    size_t remaining = file_size;
    
    while (remaining > 0) {
        size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        size_t bytes_read = fread(buffer, 1, to_read, input_file);
        
        if (bytes_read == 0) {
            fclose(output_file);
            unlink(extracted_path);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        fwrite(buffer, 1, bytes_read, output_file);
        remaining -= bytes_read;
    }
    
    fclose(output_file);
    
    // Make extracted binary executable
    chmod(extracted_path, 0755);
    
    return UB_SUCCESS;
}

// Function to extract PHP extensions and create custom php.ini
ub_result_t ub_extract_php_extensions(FILE* input_file, const char* temp_dir) {
    if (!input_file || !temp_dir) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    // Look for PHP extensions marker
    uint32_t marker_len;
    if (fread(&marker_len, sizeof(marker_len), 1, input_file) != 1) {
        // No extensions section found, skip
        return UB_SUCCESS;
    }
    
    char* marker = malloc(marker_len + 1);
    if (!marker) {
        return UB_ERROR_MEMORY_ALLOCATION;
    }
    
    if (fread(marker, 1, marker_len, input_file) != marker_len) {
        free(marker);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    marker[marker_len] = '\0';
    
    // Check if this is the PHP extensions section
    if (strcmp(marker, "PHP_EXTENSIONS_START") != 0) {
        // Not PHP extensions, rewind and return
        fseek(input_file, -(long)(sizeof(marker_len) + marker_len), SEEK_CUR);
        free(marker);
        return UB_SUCCESS;
    }
    free(marker);
    
    // Create extensions directory
    char ext_dir[2048];
#ifdef PLATFORM_WINDOWS
    snprintf(ext_dir, sizeof(ext_dir), "%s\\extensions", temp_dir);
#else
    snprintf(ext_dir, sizeof(ext_dir), "%s/extensions", temp_dir);
#endif
    if (mkdir(ext_dir, 0755) != 0 && errno != EEXIST) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Extract extensions and create completely isolated php.ini
    int extensions_extracted = 0;
    char php_ini_content[8192] = "";  // Increased size for more extensions
    strcat(php_ini_content, "; UBuilder generated PHP configuration for complete isolation\n");
    strcat(php_ini_content, "; This configuration completely isolates embedded PHP from host system\n");
    strcat(php_ini_content, "\n; CRITICAL: Prevent scanning system configuration directories\n");
    strcat(php_ini_content, "; This prevents loading of host system extension configurations\n");
    strcat(php_ini_content, "cfg_file_path = \"\"\n");  // Disable additional config file scanning
    strcat(php_ini_content, "\n; Core PHP settings for isolated execution\n");
    strcat(php_ini_content, "enable_dl = Off\n");
    strcat(php_ini_content, "auto_globals_jit = On\n");
    strcat(php_ini_content, "default_charset = \"UTF-8\"\n");
    strcat(php_ini_content, "extension_dir = \"\"\n");  // Completely disable system extension loading
    strcat(php_ini_content, "auto_prepend_file = \"\"\n");
    strcat(php_ini_content, "auto_append_file = \"\"\n");
    strcat(php_ini_content, "\n; Error handling for maximum compatibility and clean output\n");
    strcat(php_ini_content, "error_reporting = E_ERROR | E_PARSE\n");  // Only show critical errors
    strcat(php_ini_content, "display_errors = On\n");
    strcat(php_ini_content, "display_startup_errors = Off\n");  // Hide startup warnings
    strcat(php_ini_content, "log_errors = Off\n");
    strcat(php_ini_content, "\n; EXTENSIONS COMPLETELY DISABLED FOR PORTABILITY\n");
    strcat(php_ini_content, "; Extensions are embedded but NOT automatically loaded to ensure maximum portability\n");
    strcat(php_ini_content, "; This prevents compatibility issues when running on different systems\n");
    strcat(php_ini_content, "; Advanced users can extract and configure extensions manually if needed\n");
    strcat(php_ini_content, "\n; EXTENSIONS EMBEDDED BUT NOT LOADED\n");
    strcat(php_ini_content, "; All available extensions from build machine are embedded in this executable\n");
    strcat(php_ini_content, "; They are not automatically loaded to prevent compatibility issues\n");
    strcat(php_ini_content, "; Advanced users can manually enable specific extensions by:\n");
    strcat(php_ini_content, "; 1. Extracting extensions to a directory\n");
    strcat(php_ini_content, "; 2. Setting extension_dir and adding extension= lines\n");
    strcat(php_ini_content, "; 3. Using dl() function to load them dynamically (if enable_dl=On)\n");
    
    while (1) {
        uint32_t name_len;
        if (fread(&name_len, sizeof(name_len), 1, input_file) != 1) {
            break;
        }
        
        if (name_len == 0) {
            // End marker
            break;
        }
        
        // Read extension name
        char* ext_name = malloc(name_len + 1);
        if (!ext_name) {
            return UB_ERROR_MEMORY_ALLOCATION;
        }
        
        if (fread(ext_name, 1, name_len, input_file) != name_len) {
            free(ext_name);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        ext_name[name_len] = '\0';
        
        // Read extension size
        uint32_t ext_size;
        if (fread(&ext_size, sizeof(ext_size), 1, input_file) != 1) {
            free(ext_name);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        // Create extension file path
        char ext_path[4096];  // Increased buffer size to avoid truncation warnings
#ifdef PLATFORM_WINDOWS
        snprintf(ext_path, sizeof(ext_path), "%s\\%s", ext_dir, ext_name);
#else
        snprintf(ext_path, sizeof(ext_path), "%s/%s", ext_dir, ext_name);
#endif
        
        // Extract extension file
        FILE* ext_file = fopen(ext_path, "wb");
        if (ext_file) {
            char buffer[8192];
            uint32_t remaining = ext_size;
            
            while (remaining > 0) {
                size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
                size_t bytes_read = fread(buffer, 1, to_read, input_file);
                if (bytes_read == 0) break;
                fwrite(buffer, 1, bytes_read, ext_file);
                remaining -= (uint32_t)bytes_read;
            }
            
            fclose(ext_file);
            
            // Complete isolation strategy: Don't load ANY external extensions
            // Extensions are embedded for potential future use, but not automatically loaded
            // This prevents ALL compatibility issues across different systems
            (void)ext_name;  // Suppress unused variable warning
            
            extensions_extracted++;
        }
        
        free(ext_name);
    }
    
    // Create custom php.ini
    char php_ini_path[2048];
#ifdef PLATFORM_WINDOWS
    snprintf(php_ini_path, sizeof(php_ini_path), "%s\\php.ini", temp_dir);
#else
    snprintf(php_ini_path, sizeof(php_ini_path), "%s/php.ini", temp_dir);
#endif
    FILE* ini_file = fopen(php_ini_path, "w");
    if (ini_file) {
        fwrite(php_ini_content, 1, strlen(php_ini_content), ini_file);
        fclose(ini_file);
    }
    
    return UB_SUCCESS;
}

// Cleanup function
void ub_runtime_info_cleanup(ub_runtime_info_t* info) {
    if (!info) return;
    
    free(info->binary_path);
    free(info->binary_name);
    free(info->version_string);
    
    memset(info, 0, sizeof(ub_runtime_info_t));
}

#ifdef PLATFORM_WINDOWS
// Function to extract Windows PHP runtime (multiple files format)
ub_result_t ub_extract_windows_php_runtime(FILE* input_file, const char* temp_dir, char* extracted_path) {
    if (!input_file || !temp_dir || !extracted_path) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    int files_extracted = 0;
    
    while (1) {
        uint32_t name_len;
        
        // Read filename length (0 means end of files)
        if (fread(&name_len, sizeof(name_len), 1, input_file) != 1) {
            break;
        }
        
        if (name_len == 0) {
            // End marker reached
            break;
        }
        
        // Read filename
        char* filename = malloc(name_len + 1);
        if (!filename) {
            return UB_ERROR_MEMORY_ALLOCATION;
        }
        
        if (fread(filename, 1, name_len, input_file) != name_len) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        filename[name_len] = '\0';
        
        // Read file size
        uint32_t file_size;
        if (fread(&file_size, sizeof(file_size), 1, input_file) != 1) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        // Create full path
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s\\%s", temp_dir, filename);
        
        // Create directory structure if needed (for ext\*.dll files)
        char* dir_path = strdup(full_path);
        char* last_slash = strrchr(dir_path, '\\');
        if (last_slash && last_slash != dir_path) {
            *last_slash = '\0';
            (void)pc_mkdir_p(dir_path);   /* best-effort; fopen below will fail loudly if not creatable */
        }
        free(dir_path);

        // Extract file
        FILE* output_file = fopen(full_path, "wb");
        if (!output_file) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }


        char buffer[8192];
        uint32_t remaining = file_size;
        while (remaining > 0) {
            size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
            size_t bytes_read = fread(buffer, 1, to_read, input_file);
            if (bytes_read == 0) break;
            fwrite(buffer, 1, bytes_read, output_file);
            remaining -= (uint32_t)bytes_read;
        }
        fclose(output_file);
        
        // Remember php.exe path for execution
        if (strcmp(filename, "php.exe") == 0) {
            strcpy(extracted_path, full_path);
        }
        
        free(filename);
        files_extracted++;
    }
    
    
    if (files_extracted == 0) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    return UB_SUCCESS;
}

// Function to extract Windows Node.js runtime (multiple files format)
ub_result_t ub_extract_windows_nodejs_runtime(FILE* input_file, const char* temp_dir, char* extracted_path) {
    if (!input_file || !temp_dir || !extracted_path) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    int files_extracted = 0;
    
    while (1) {
        uint32_t name_len;
        
        // Read filename length (0 means end of files)
        if (fread(&name_len, sizeof(name_len), 1, input_file) != 1) {
            break;
        }
        
        if (name_len == 0) {
            // End marker reached
            break;
        }
        
        // Read filename
        char* filename = malloc(name_len + 1);
        if (!filename) {
            return UB_ERROR_MEMORY_ALLOCATION;
        }
        
        if (fread(filename, 1, name_len, input_file) != name_len) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        filename[name_len] = '\0';
        
        // Read file size
        uint32_t file_size;
        if (fread(&file_size, sizeof(file_size), 1, input_file) != 1) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        // Create full path
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s\\%s", temp_dir, filename);
        
        // Extract file
        FILE* output_file = fopen(full_path, "wb");
        if (!output_file) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        char buffer[8192];
        uint32_t remaining = file_size;
        while (remaining > 0) {
            size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
            size_t bytes_read = fread(buffer, 1, to_read, input_file);
            if (bytes_read == 0) break;
            fwrite(buffer, 1, bytes_read, output_file);
            remaining -= (uint32_t)bytes_read;
        }
        fclose(output_file);
        
        // Remember node.exe path for execution
        if (strcmp(filename, "node.exe") == 0) {
            strcpy(extracted_path, full_path);
        }
        
        free(filename);
        files_extracted++;
    }
    
    if (files_extracted == 0) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    return UB_SUCCESS;
}

// Function to extract Windows Python runtime (multiple files format) - OPTIMIZED
ub_result_t ub_extract_windows_python_runtime(FILE* input_file, const char* temp_dir, char* extracted_path) {
    if (!input_file || !temp_dir || !extracted_path) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    int files_extracted = 0;
    
    // Create a persistent cache directory based on UBuilder version/hash
    char cache_dir[1024];
    char cache_root[1024];
    
    // Use a fixed cache location that persists across runs
#ifdef PLATFORM_WINDOWS
    snprintf(cache_root, sizeof(cache_root), "%s\\UBuilder", getenv("LOCALAPPDATA") ? getenv("LOCALAPPDATA") : getenv("TEMP"));
#else
    snprintf(cache_root, sizeof(cache_root), "%s/.ubuilder", getenv("HOME"));
#endif
    
    snprintf(cache_dir, sizeof(cache_dir), "%s\\PythonRuntime", cache_root);
    
    /* Create cache directories (structured — no shell). */
    (void)pc_mkdir_p(cache_root);
    (void)pc_mkdir_p(cache_dir);
    
    // Check if cache is already populated (quick check for python.exe)
    char python_exe_check[1024];
    snprintf(python_exe_check, sizeof(python_exe_check), "%s\\python.exe", cache_dir);
    
    int cache_exists = (access(python_exe_check, F_OK) == 0);
    
    if (cache_exists) {
        printf("Using cached Python runtime (skipping extraction)...\n");
        strcpy(extracted_path, python_exe_check);
        
        // Still need to read through the embedded data to advance file pointer
        while (1) {
            uint32_t name_len;
            
            if (fread(&name_len, sizeof(name_len), 1, input_file) != 1) {
                break;
            }
            
            if (name_len == 0) {
                break;
            }
            
            // Skip filename
            fseek(input_file, name_len, SEEK_CUR);
            
            // Read and skip file size
            uint32_t file_size;
            if (fread(&file_size, sizeof(file_size), 1, input_file) != 1) {
                break;
            }
            
            // Skip file content
            fseek(input_file, file_size, SEEK_CUR);
        }
        
        return UB_SUCCESS;
    }
    
    printf("Extracting Python runtime to persistent cache (one-time setup)...\n");
    
    while (1) {
        uint32_t name_len;
        
        // Read filename length (0 means end of files)
        if (fread(&name_len, sizeof(name_len), 1, input_file) != 1) {
            break;
        }
        
        if (name_len == 0) {
            // End marker reached
            break;
        }
        
        // Read filename
        char* filename = malloc(name_len + 1);
        if (!filename) {
            return UB_ERROR_MEMORY_ALLOCATION;
        }
        
        if (fread(filename, 1, name_len, input_file) != name_len) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        filename[name_len] = '\0';
        
        // Read file size
        uint32_t file_size;
        if (fread(&file_size, sizeof(file_size), 1, input_file) != 1) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        // Create full path in cache directory
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s\\%s", cache_dir, filename);
        
        // Create directory structure if needed
        char* dir_path = strdup(full_path);
        char* last_slash = strrchr(dir_path, '\\');
        if (last_slash && last_slash != dir_path) {
            *last_slash = '\0';
            (void)pc_mkdir_p(dir_path);
        }
        free(dir_path);
        
        // Extract file
        FILE* output_file = fopen(full_path, "wb");
        if (!output_file) {
            free(filename);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        
        char buffer[8192];
        uint32_t remaining = file_size;
        while (remaining > 0) {
            size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
            size_t bytes_read = fread(buffer, 1, to_read, input_file);
            if (bytes_read == 0) break;
            fwrite(buffer, 1, bytes_read, output_file);
            remaining -= (uint32_t)bytes_read;
        }
        fclose(output_file);
        files_extracted++;
        
        // Remember python.exe path for execution
        if (strcmp(filename, "python.exe") == 0) {
            strcpy(extracted_path, full_path);
        }
        
        free(filename);
        
        // Progress indicator for large extractions
        if (files_extracted % 1000 == 0) {
            printf("  Extracted %d files...\n", files_extracted);
        }
    }
    
    printf("Python runtime cached: %d files extracted to %s\n", files_extracted, cache_dir);
    
    if (files_extracted == 0) {
        return UB_ERROR_EXTRACTION_FAILED;
    }

    return UB_SUCCESS;
}
#endif

/* ================================================================
 * M1: Tree-format embed / extract
 * ================================================================
 *
 * Header (8 bytes):   [u32 magic 'UBRT'][u32 file_count]
 * Per record:         [u16 path_len][path bytes][u32 mode][u64 size][bytes]
 *
 * Paths use '/' separators on all platforms. Directories are implied by
 * the paths of their contained files; empty directories are not preserved.
 * Mode preserves the executable bit so vendored interpreter binaries stay
 * runnable after extraction. See runtime_embedder.h.
 */

static int has_path_sep(const char* s) {
    for (; *s; s++) if (*s == '/' || *s == '\\') return 1;
    return 0;
}

/* Recursive tree walker for embed. `prefix_len` is the byte offset into
 * the absolute path where the relative path starts. */
static ub_result_t embed_tree_walk(const char* abs_path,
                                   size_t prefix_len,
                                   FILE*  out,
                                   uint32_t* count) {
    struct stat st;
    if (lstat(abs_path, &st) != 0) return UB_ERROR_FILE_NOT_FOUND;

    if (S_ISDIR(st.st_mode)) {
#ifdef PLATFORM_WINDOWS
        WIN32_FIND_DATAA fd;
        char pattern[1024];
        snprintf(pattern, sizeof(pattern), "%s\\*", abs_path);
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return UB_SUCCESS;
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char child[1024];
            snprintf(child, sizeof(child), "%s\\%s", abs_path, fd.cFileName);
            ub_result_t rc = embed_tree_walk(child, prefix_len, out, count);
            if (rc != UB_SUCCESS) { FindClose(h); return rc; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        DIR* d = opendir(abs_path);
        if (!d) return UB_SUCCESS;  /* unreadable dir — skip */
        struct dirent* de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            size_t plen = strlen(abs_path);
            size_t nlen = strlen(de->d_name);
            char* child = (char*)malloc(plen + 1 + nlen + 1);
            if (!child) { closedir(d); return UB_ERROR_MEMORY_ALLOCATION; }
            snprintf(child, plen + 1 + nlen + 1, "%s/%s", abs_path, de->d_name);
            ub_result_t rc = embed_tree_walk(child, prefix_len, out, count);
            free(child);
            if (rc != UB_SUCCESS) { closedir(d); return rc; }
        }
        closedir(d);
#endif
        return UB_SUCCESS;
    }

    if (!S_ISREG(st.st_mode)) {
        /* Symlinks: follow and embed the target's content with the link's
         * own path. Other special files (devices, sockets) are skipped. */
        if (!S_ISLNK(st.st_mode)) return UB_SUCCESS;
    }

    /* Build the relative path with '/' separators. */
    const char* rel = abs_path + prefix_len;
    while (*rel == '/' || *rel == '\\') rel++;

    size_t rlen = strlen(rel);
    if (rlen > 0xFFFF) return UB_ERROR_INVALID_ARGS;

    /* Normalize backslashes to forward slashes. */
    char* nrel = (char*)malloc(rlen + 1);
    if (!nrel) return UB_ERROR_MEMORY_ALLOCATION;
    for (size_t i = 0; i < rlen; i++) nrel[i] = (rel[i] == '\\') ? '/' : rel[i];
    nrel[rlen] = 0;

    /* Open and re-stat through the symlink to get the target size/content. */
    FILE* in = fopen(abs_path, "rb");
    if (!in) { free(nrel); return UB_ERROR_FILE_NOT_FOUND; }
    if (fseek(in, 0, SEEK_END) != 0) { fclose(in); free(nrel); return UB_ERROR_FILE_NOT_FOUND; }
    long sz = ftell(in);
    if (sz < 0) { fclose(in); free(nrel); return UB_ERROR_FILE_NOT_FOUND; }
    fseek(in, 0, SEEK_SET);

    uint16_t path_len = (uint16_t)rlen;
    uint32_t mode     = (uint32_t)(st.st_mode & 0xFFFu);
    uint64_t size64   = (uint64_t)sz;

    if (fwrite(&path_len, sizeof(path_len), 1, out) != 1 ||
        fwrite(nrel,      1, rlen, out)             != rlen ||
        fwrite(&mode,     sizeof(mode), 1, out)     != 1 ||
        fwrite(&size64,   sizeof(size64), 1, out)   != 1) {
        fclose(in); free(nrel); return UB_ERROR_EXTRACTION_FAILED;
    }

    char buf[65536];
    long remaining = sz;
    while (remaining > 0) {
        size_t want = (remaining > (long)sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
        size_t got  = fread(buf, 1, want, in);
        if (got == 0) { fclose(in); free(nrel); return UB_ERROR_EXTRACTION_FAILED; }
        if (fwrite(buf, 1, got, out) != got) { fclose(in); free(nrel); return UB_ERROR_EXTRACTION_FAILED; }
        remaining -= (long)got;
    }
    fclose(in);
    free(nrel);
    (*count)++;
    return UB_SUCCESS;
}

#ifndef PLATFORM_WINDOWS
static int probe_script(const char* path, char* out, size_t out_cap) {
    if (access(path, R_OK) != 0) return -1;
    size_t n = strlen(path);
    if (n >= out_cap) return -1;
    memcpy(out, path, n + 1);
    return 0;
}

int ub_find_vendor_script(char* out, size_t out_cap) {
    if (!out || out_cap < 64) return -1;

    /* 1. explicit env override */
    const char* env = getenv("UBUILDER_VENDOR_SCRIPT");
    if (env && *env) {
        if (probe_script(env, out, out_cap) == 0) return 0;
        /* explicit override that's invalid is a hard error to the caller */
        return -1;
    }

    /* Resolve <exe-dir> from /proc/self/exe-style lookup. */
    char exe[1024];
    if (pc_executable_path(exe, sizeof(exe)) != 0) return -1;
    char* last_slash = strrchr(exe, '/');
    if (!last_slash) return -1;
    *last_slash = 0;  /* exe is now the directory */

    /* Probe well-known relative paths. */
    const char* rels[] = {
        "/../../scripts/vendor-runtimes.sh",
        "/../scripts/vendor-runtimes.sh",
        "/scripts/vendor-runtimes.sh",
        "/../share/ubuilder/vendor-runtimes.sh",
        NULL
    };
    char cand[2048];
    for (int i = 0; rels[i]; i++) {
        snprintf(cand, sizeof(cand), "%s%s", exe, rels[i]);
        if (probe_script(cand, out, out_cap) == 0) return 0;
    }
    return -1;
}

ub_result_t ub_auto_vendor(const char* runtime_key) {
    if (!runtime_key || !*runtime_key) return UB_ERROR_INVALID_ARGS;

    char script[1024];
    if (ub_find_vendor_script(script, sizeof(script)) != 0) {
        fprintf(stderr,
                "note: cannot auto-vendor — vendor-runtimes.sh not found.\n"
                "      Set UBUILDER_VENDOR_SCRIPT=<path> or run the script manually.\n");
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    char* bash = pc_path_lookup("bash");
    if (!bash) {
        fprintf(stderr,
                "note: cannot auto-vendor — bash not on PATH.\n"
                "      Run %s manually, or install bash + curl + tar.\n", script);
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    printf("Auto-vendoring %s (one-time setup) ...\n", runtime_key);
    printf("  script: %s\n", script);
    fflush(stdout);   /* surface our header before the script's curl output. */
    char* argv[] = { bash, script, (char*)runtime_key, NULL };
    int rc = pc_spawn_and_wait(bash, argv, NULL, NULL);
    free(bash);
    if (rc != 0) {
        fprintf(stderr,
                "note: auto-vendor failed (exit %d). Check network access\n"
                "      or run the script manually for a clearer error.\n", rc);
        return UB_ERROR_EXECUTION_FAILED;
    }
    return UB_SUCCESS;
}
#else
int ub_find_vendor_script(char* out, size_t out_cap) {
    (void)out; (void)out_cap;
    return -1;  /* Windows auto-vendor support deferred. */
}
ub_result_t ub_auto_vendor(const char* runtime_key) {
    (void)runtime_key;
    return UB_ERROR_RUNTIME_NOT_FOUND;
}
#endif

#ifndef PLATFORM_WINDOWS
int ub_runtime_cache_lookup(const char* cache_subdir,
                            const char* rel_exe,
                            char*       out,
                            size_t      out_cap) {
    if (!cache_subdir || !rel_exe || !out || out_cap < 16) return -1;

    /* Resolve cache root: $UBUILDER_RUNTIMES_CACHE > $XDG_CACHE_HOME > $HOME. */
    char root[1024];
    const char* override = getenv("UBUILDER_RUNTIMES_CACHE");
    if (override && *override) {
        snprintf(root, sizeof(root), "%s/%s", override, cache_subdir);
    } else {
        const char* xdg = getenv("XDG_CACHE_HOME");
        if (xdg && *xdg) {
            snprintf(root, sizeof(root), "%s/ubuilder/runtimes/%s", xdg, cache_subdir);
        } else {
            const char* home = getenv("HOME");
            if (!home || !*home) return -1;
            snprintf(root, sizeof(root), "%s/.cache/ubuilder/runtimes/%s", home, cache_subdir);
        }
    }

    DIR* d = opendir(root);
    if (!d) return -1;

    /* Collect subdir names, pick the lexicographically-greatest one with
     * the named executable inside. */
    char  best[256] = {0};
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char probe[2048];
        snprintf(probe, sizeof(probe), "%s/%s/%s", root, de->d_name, rel_exe);
        if (access(probe, X_OK) != 0) continue;
        if (best[0] == 0 || strcmp(de->d_name, best) > 0) {
            size_t n = strlen(de->d_name);
            if (n >= sizeof(best)) continue;
            memcpy(best, de->d_name, n + 1);
        }
    }
    closedir(d);

    if (best[0] == 0) return -1;
    int n = snprintf(out, out_cap, "%s/%s", root, best);
    if (n < 0 || (size_t)n >= out_cap) return -1;
    return 0;
}
#else
int ub_runtime_cache_lookup(const char* cache_subdir,
                            const char* rel_exe,
                            char*       out,
                            size_t      out_cap) {
    (void)cache_subdir; (void)rel_exe; (void)out; (void)out_cap;
    return -1;  /* Cache auto-discovery is POSIX-only for now. */
}
#endif

ub_result_t ub_embed_runtime_single_as_tree(const char* binary_path,
                                            const char* dest_rel_path,
                                            FILE*       output_file) {
    if (!binary_path || !dest_rel_path || !output_file) return UB_ERROR_INVALID_ARGS;

    uint32_t magic = UB_RUNTIME_TREE_MAGIC;
    if (fwrite(&magic, sizeof(magic), 1, output_file) != 1) return UB_ERROR_EXTRACTION_FAILED;

    struct stat st;
    if (stat(binary_path, &st) != 0 || !S_ISREG(st.st_mode)) return UB_ERROR_FILE_NOT_FOUND;

    size_t   rlen = strlen(dest_rel_path);
    if (rlen == 0 || rlen > 0xFFFF) return UB_ERROR_INVALID_ARGS;
    uint16_t plen   = (uint16_t)rlen;
    uint32_t mode   = 0755;   /* exec for the embedded interpreter */
    uint64_t size64 = (uint64_t)st.st_size;

    if (fwrite(&plen,        sizeof(plen),   1, output_file) != 1)    return UB_ERROR_EXTRACTION_FAILED;
    if (fwrite(dest_rel_path, 1, rlen, output_file)          != rlen) return UB_ERROR_EXTRACTION_FAILED;
    if (fwrite(&mode,        sizeof(mode),   1, output_file) != 1)    return UB_ERROR_EXTRACTION_FAILED;
    if (fwrite(&size64,      sizeof(size64), 1, output_file) != 1)    return UB_ERROR_EXTRACTION_FAILED;

    FILE* in = fopen(binary_path, "rb");
    if (!in) return UB_ERROR_FILE_NOT_FOUND;
    char buf[65536];
    uint64_t remaining = size64;
    while (remaining > 0) {
        size_t want = (remaining > sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
        size_t got  = fread(buf, 1, want, in);
        if (got == 0)                              { fclose(in); return UB_ERROR_EXTRACTION_FAILED; }
        if (fwrite(buf, 1, got, output_file) != got){ fclose(in); return UB_ERROR_EXTRACTION_FAILED; }
        remaining -= got;
    }
    fclose(in);

    uint16_t sentinel = 0;
    if (fwrite(&sentinel, sizeof(sentinel), 1, output_file) != 1) return UB_ERROR_EXTRACTION_FAILED;
    return UB_SUCCESS;
}

ub_result_t ub_embed_runtime_tree(const char* source_dir, FILE* output_file) {
    if (!source_dir || !output_file) return UB_ERROR_INVALID_ARGS;
    struct stat st;
    if (stat(source_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return UB_ERROR_FILE_NOT_FOUND;
    }

    /* Write magic, stream records, then a u16=0 sentinel. The append-mode
     * output file precludes seek-back patching, hence the sentinel rather
     * than a leading count. */
    uint32_t magic = UB_RUNTIME_TREE_MAGIC;
    uint32_t count = 0;
    if (fwrite(&magic, sizeof(magic), 1, output_file) != 1) return UB_ERROR_EXTRACTION_FAILED;

    size_t prefix_len = strlen(source_dir);
    ub_result_t rc = embed_tree_walk(source_dir, prefix_len, output_file, &count);
    if (rc != UB_SUCCESS) return rc;

    uint16_t sentinel = 0;
    if (fwrite(&sentinel, sizeof(sentinel), 1, output_file) != 1)
        return UB_ERROR_EXTRACTION_FAILED;

    printf("Embedded %u files from %s\n", (unsigned)count, source_dir);
    return UB_SUCCESS;
}

ub_result_t ub_extract_runtime_tree(FILE* input_file, const char* dest_dir) {
    if (!input_file || !dest_dir) return UB_ERROR_INVALID_ARGS;

    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, input_file) != 1)  return UB_ERROR_EXTRACTION_FAILED;
    if (magic != UB_RUNTIME_TREE_MAGIC)                    return UB_ERROR_EXTRACTION_FAILED;

    if (pc_mkdir_p(dest_dir) != 0) return UB_ERROR_EXTRACTION_FAILED;

    char path[2048];
    char target[2048];
    char buf[65536];
    uint32_t extracted = 0;
    for (;;) {
        uint16_t path_len = 0;
        if (fread(&path_len, sizeof(path_len), 1, input_file) != 1) return UB_ERROR_EXTRACTION_FAILED;
        if (path_len == 0) break;                /* sentinel — end of tree */
        if (path_len >= sizeof(path))             return UB_ERROR_EXTRACTION_FAILED;

        uint32_t mode     = 0;
        uint64_t size64   = 0;
        if (fread(path, 1, path_len, input_file) != path_len)       return UB_ERROR_EXTRACTION_FAILED;
        path[path_len] = 0;
        if (fread(&mode,   sizeof(mode),   1, input_file) != 1)     return UB_ERROR_EXTRACTION_FAILED;
        if (fread(&size64, sizeof(size64), 1, input_file) != 1)     return UB_ERROR_EXTRACTION_FAILED;

        /* Reject path-traversal attempts. */
        if (path[0] == '/' || strstr(path, "..") != NULL) return UB_ERROR_EXTRACTION_FAILED;

        /* Materialize: <dest_dir>/<path>. */
        int n = snprintf(target, sizeof(target), "%s/%s", dest_dir, path);
        if (n < 0 || (size_t)n >= sizeof(target)) return UB_ERROR_EXTRACTION_FAILED;
#ifdef PLATFORM_WINDOWS
        for (char* p = target; *p; p++) if (*p == '/') *p = '\\';
#endif
        /* mkdir -p for parent dir. */
        char parent[2048];
        strcpy(parent, target);
        char* last_sep = NULL;
        for (char* p = parent; *p; p++) {
#ifdef PLATFORM_WINDOWS
            if (*p == '\\' || *p == '/') last_sep = p;
#else
            if (*p == '/') last_sep = p;
#endif
        }
        if (last_sep) {
            *last_sep = 0;
            if (pc_mkdir_p(parent) != 0) return UB_ERROR_EXTRACTION_FAILED;
        }

        FILE* out = fopen(target, "wb");
        if (!out) return UB_ERROR_EXTRACTION_FAILED;
        uint64_t remaining = size64;
        while (remaining > 0) {
            size_t want = (remaining > sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
            size_t got  = fread(buf, 1, want, input_file);
            if (got == 0) { fclose(out); return UB_ERROR_EXTRACTION_FAILED; }
            if (fwrite(buf, 1, got, out) != got) { fclose(out); return UB_ERROR_EXTRACTION_FAILED; }
            remaining -= got;
        }
        fclose(out);

#ifndef PLATFORM_WINDOWS
        chmod(target, (mode_t)(mode & 0xFFFu));
#else
        (void)mode;
#endif
        extracted++;
    }
    (void)extracted;
    return UB_SUCCESS;
}
