#include "runtime_embedder.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #include <process.h>  // For _access constants
    #define popen _popen
    #define pclose _pclose
    #define access _access
    #define mkdir(path, mode) _mkdir(path)
    #define readlink(path, buf, size) (-1)  // Not available on Windows
    #define lstat stat  // Use stat instead of lstat on Windows
    #define S_ISLNK(mode) (0)  // No symbolic links concept on Windows like Unix
    #define unlink _unlink
    #define chmod _chmod
    #ifdef _MSC_VER
        typedef ptrdiff_t ssize_t;  // Define ssize_t for MSVC
        #define strdup _strdup
    #endif
#else
    #include <unistd.h>
#endif

// Function to execute a command and capture output
static char* execute_command(const char* command) {
    FILE* pipe = popen(command, "r");
    if (!pipe) return NULL;
    
    char buffer[1024];
    char* result = NULL;
    size_t total_size = 0;
    
    while (fgets(buffer, sizeof(buffer), pipe)) {
        size_t len = strlen(buffer);
        char* new_result = realloc(result, total_size + len + 1);
        if (!new_result) {
            free(result);
            pclose(pipe);
            return NULL;
        }
        result = new_result;
        strcpy(result + total_size, buffer);
        total_size += len;
    }
    
    pclose(pipe);
    
    // Remove trailing newline
    if (result && total_size > 0 && result[total_size - 1] == '\n') {
        result[total_size - 1] = '\0';
    }
    
    return result;
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

// Function to detect and get runtime information
ub_result_t ub_detect_runtime_binary(ub_runtime_type_t runtime, ub_runtime_info_t* info) {
    if (!info) return UB_ERROR_INVALID_ARGS;
    
    memset(info, 0, sizeof(ub_runtime_info_t));
    
    const char* which_cmd;
    const char* version_cmd;
    const char* binary_name;
    
    switch (runtime) {
        case UB_RUNTIME_PHP:
#ifdef PLATFORM_WINDOWS
            // On Windows, PHP requires multiple files: php.exe + php8ts.dll + dependencies
            // First get the PHP directory from php.exe path
            which_cmd = "php -r \"echo PHP_BINARY;\"";
            version_cmd = "php --version";
#else
            which_cmd = "which php";
            version_cmd = "php --version | head -1";
#endif
            binary_name = "php";
            break;
        case UB_RUNTIME_PYTHON:
#ifdef PLATFORM_WINDOWS
            // On Windows, try multiple approaches to find Python
            which_cmd = "python -c \"import sys; print(sys.executable)\"";
            version_cmd = "python --version";
#else
            which_cmd = "which python3";
            version_cmd = "python3 --version";
#endif
            binary_name = "python";
            break;
        case UB_RUNTIME_NODEJS:
#ifdef PLATFORM_WINDOWS
            // On Windows, get the actual Node.js executable path
            which_cmd = "node -p \"process.execPath\"";
            version_cmd = "node --version";
#else
            which_cmd = "which node";
            version_cmd = "node --version";
#endif
            binary_name = "node";
            break;
        default:
            return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    // Find binary path
    char* binary_path = execute_command(which_cmd);
    if (!binary_path) {
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    // Resolve symlinks to get actual binary
    char* resolved_path = resolve_binary_path(binary_path);
    free(binary_path);
    
    if (!resolved_path) {
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    // Check if binary exists and is executable
#ifdef PLATFORM_WINDOWS
    if (access(resolved_path, 0) != 0) {  // Check if file exists on Windows
#else
    if (access(resolved_path, X_OK) != 0) {
#endif
        free(resolved_path);
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    // Get version info
    char* version = execute_command(version_cmd);
    
    // Get binary size
    size_t size = get_file_size(resolved_path);
    if (size == 0) {
        free(resolved_path);
        free(version);
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    // Fill info structure
    info->binary_path = resolved_path;
    info->binary_name = strdup(binary_name);
    info->binary_size = size;
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
    strcat(php_ini_content, "\n; Error handling for maximum compatibility\n");
    strcat(php_ini_content, "error_reporting = E_ALL & ~E_WARNING & ~E_NOTICE\n");
    strcat(php_ini_content, "display_errors = On\n");
    strcat(php_ini_content, "log_errors = Off\n");
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
        char ext_path[2048];
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
            // Create directory recursively 
            char mkdir_cmd[1024];
            snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir \"%s\" 2>nul", dir_path);
            system(mkdir_cmd);
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
    int files_skipped = 0;
    
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
    
    // Create cache directories
    char mkdir_cmd[1024];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir \"%s\" 2>nul", cache_root);
    system(mkdir_cmd);
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir \"%s\" 2>nul", cache_dir);
    system(mkdir_cmd);
    
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
            // Create directory recursively 
            char mkdir_cmd[1024];
            snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir \"%s\" 2>nul", dir_path);
            system(mkdir_cmd);
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
