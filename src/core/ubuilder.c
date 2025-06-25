#include "ubuilder.h"
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
#endif

#ifdef PLATFORM_MACOS
    #include <mach-o/dyld.h>
#endif

// Forward declarations
static ub_result_t validate_project_directory(const ub_config_t* config);
static ub_result_t copy_executable_template(const char* output_path);
static ub_result_t embed_project_files(const ub_config_t* config);
static ub_result_t embed_directory_recursive(const char* dir_path, FILE* output_file);
static ub_result_t create_modular_executable(const ub_config_t* config);
static ub_result_t ub_execute_embedded_app(ub_runtime_type_t runtime, const char* script_content, 
                                          const char* entry_point, int argc, char* argv[]);
static int ub_execute_script(ub_runtime_type_t runtime, const char* script_path, int argc, char* argv[]);
static ub_result_t ub_run_modular_embedded_app(ub_runtime_type_t runtime, FILE* data_file, int argc, char* argv[]);
static ub_result_t ub_run_legacy_embedded_app(FILE* data_file, int argc, char* argv[]);
static ub_result_t ub_execute_embedded_app(ub_runtime_type_t runtime, const char* script_content, 
                                          const char* entry_point, int argc, char* argv[]);
static int ub_execute_script(ub_runtime_type_t runtime, const char* script_path, int argc, char* argv[]);

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
    if (index < sizeof(ERROR_MESSAGES) / sizeof(ERROR_MESSAGES[0])) {
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
    char old_marker[] = "UBUILDER_DATA_MARKER";
    char modular_marker[] = "UBUILDER_MODULAR_V3_B64F7E2A_MARKER";  // More unique marker
    char buffer[32];
    ub_runtime_type_t runtime;
    ub_result_t result = UB_ERROR_RUNTIME_NOT_FOUND;
    
    // Get path to current executable
    char exe_path[1024];
#ifdef PLATFORM_LINUX
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        // Unable to get executable path - not an embedded app
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    exe_path[len] = '\0';
#elif defined(PLATFORM_WINDOWS)
    if (GetModuleFileName(NULL, exe_path, sizeof(exe_path)) == 0) {
        // Unable to get executable path - not an embedded app
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
#elif defined(PLATFORM_MACOS)
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        // Unable to get executable path - not an embedded app
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
#endif
    
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
    // For true runtime embedding, search only the last 1KB to avoid false positives
    long search_size = 1024; // Search last 1KB only
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
    
    // Look for modular marker
    for (size_t i = 0; i <= bytes_read - strlen(modular_marker); i++) {
        if (memcmp(search_buffer + i, modular_marker, strlen(modular_marker)) == 0) {
            // Found modular marker, try to read magic number and runtime type
            size_t magic_pos = i + strlen(modular_marker);
            size_t runtime_pos = magic_pos + sizeof(uint32_t);
            
            if (runtime_pos + sizeof(runtime) <= bytes_read) {
                // Check magic number
                uint32_t magic_number;
                memcpy(&magic_number, search_buffer + magic_pos, sizeof(magic_number));
                if (magic_number != 0xDEADBEEF) {
                    continue; // Not a valid embedded app
                }
                
                memcpy(&runtime, search_buffer + runtime_pos, sizeof(runtime));
                
                // Verify this is a valid runtime type
                if (runtime >= UB_RUNTIME_PYTHON && runtime <= UB_RUNTIME_NODEJS) {
                    // Set file position to start of embedded data
                    fseek(self_file, search_start + runtime_pos + sizeof(runtime), SEEK_SET);
                    result = ub_run_modular_embedded_app(runtime, self_file, argc, argv);
                    free(search_buffer);
                    fclose(self_file);
                    return result;
                }
            }
        }
    }
    
    // If modular marker not found, try old format in the search buffer
    for (size_t i = 0; i <= bytes_read - strlen(old_marker); i++) {
        if (memcmp(search_buffer + i, old_marker, strlen(old_marker)) == 0) {
            // Found old marker, set file position after marker
            fseek(self_file, search_start + i + strlen(old_marker), SEEK_SET);
            result = ub_run_legacy_embedded_app(self_file, argc, argv);
            free(search_buffer);
            fclose(self_file);
            return result;
        }
    }
    
    free(search_buffer);
    
    fclose(self_file);
    // No embedded data found - this is normal for the CLI executable
    return UB_ERROR_RUNTIME_NOT_FOUND;
}

// Function to get cross-platform temporary directory
static const char* get_temp_dir(void) {
    const char* temp_dir = NULL;
    
#ifdef PLATFORM_WINDOWS
    temp_dir = getenv("TEMP");
    if (!temp_dir) temp_dir = getenv("TMP");
    if (!temp_dir) temp_dir = "C:\\temp";
#else
    temp_dir = getenv("TMPDIR");
    if (!temp_dir) temp_dir = getenv("TMP");
    if (!temp_dir) temp_dir = "/tmp";
#endif
    
    return temp_dir;
}

static ub_result_t ub_execute_embedded_app(ub_runtime_type_t runtime, const char* script_content, 
                                          const char* entry_point, int argc, char* argv[]) {
    char temp_dir[512];
    char script_path[1024];
    const char* extension;
    FILE* script_file;
    int exit_code;
    
    // Create temporary directory
    const char* base_temp_dir = get_temp_dir();
#ifdef PLATFORM_WINDOWS
    snprintf(temp_dir, sizeof(temp_dir), "%s\\ubuilder-%d", base_temp_dir, getpid());
#else
    snprintf(temp_dir, sizeof(temp_dir), "%s/ubuilder-%d", base_temp_dir, getpid());
#endif
    if (mkdir(temp_dir, 0755) != 0 && errno != EEXIST) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Determine file extension based on runtime
    switch (runtime) {
        case UB_RUNTIME_PYTHON:
            extension = ".py";
            break;
        case UB_RUNTIME_PHP:
            extension = ".php";
            break;
        case UB_RUNTIME_NODEJS:
            extension = ".js";
            break;
        default:
            return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    // Write script to temporary file
    if (entry_point) {
        snprintf(script_path, sizeof(script_path), "%s/%s", temp_dir, entry_point);
    } else {
        snprintf(script_path, sizeof(script_path), "%s/main%s", temp_dir, extension);
    }
    
    script_file = fopen(script_path, "w");
    if (!script_file) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    fwrite(script_content, 1, strlen(script_content), script_file);
    fclose(script_file);
    
    // Execute the script
    exit_code = ub_execute_script(runtime, script_path, argc, argv);
    
    // Cleanup
    unlink(script_path);
    rmdir(temp_dir);
    
    return (exit_code == 0) ? UB_SUCCESS : UB_ERROR_EXECUTION_FAILED;
}

// Function to execute script with appropriate runtime
static int ub_execute_script(ub_runtime_type_t runtime, const char* script_path, int argc, char* argv[]) {
    char command[2048];
    char args_str[1024] = "";
    char script_dir[1024];
    char script_name[256];
    char original_dir[1024];
    
    // Save current working directory
    if (getcwd(original_dir, sizeof(original_dir)) == NULL) {
        return -1;
    }
    
    // Extract directory and filename from script path
    strcpy(script_dir, script_path);
    char* last_slash = strrchr(script_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        strcpy(script_name, last_slash + 1);
        
        // Change to script directory
        if (chdir(script_dir) != 0) {
            return -1;
        }
    } else {
        // Script is in current directory, no need to change directory
        strcpy(script_name, script_path);
        // Don't change directory in this case
    }
    
    // Build argument string
    for (int i = 1; i < argc; i++) {
        strncat(args_str, " ", sizeof(args_str) - strlen(args_str) - 1);
        strncat(args_str, argv[i], sizeof(args_str) - strlen(args_str) - 1);
    }
    
    // Build execution command based on runtime
    switch (runtime) {
        case UB_RUNTIME_PYTHON:
            snprintf(command, sizeof(command), "python3 \"%s\"%s", script_name, args_str);
            break;
        case UB_RUNTIME_PHP:
            snprintf(command, sizeof(command), "php \"%s\"%s", script_name, args_str);
            break;
        case UB_RUNTIME_NODEJS:
            snprintf(command, sizeof(command), "node \"%s\"%s", script_name, args_str);
            break;
        default:
            // Restore original directory before returning
            chdir(original_dir);
            return -1;
    }
    
    int result = system(command);
    
    // Restore original working directory
    chdir(original_dir);
    
    return result;
}

// Function to execute script with embedded runtime binary
static int ub_execute_script_with_embedded_runtime(ub_runtime_type_t runtime, const char* runtime_binary_path, 
                                                   const char* script_path, int argc, char* argv[]) {
    char command[2048];
    char args_str[1024] = "";
    char script_dir[1024];
    char script_name[256];
    char original_dir[1024];
    
    // Save current working directory
    if (getcwd(original_dir, sizeof(original_dir)) == NULL) {
        return -1;
    }
    
    // Extract directory and filename from script path
    strcpy(script_dir, script_path);
#ifdef PLATFORM_WINDOWS
    char* last_slash = strrchr(script_dir, '\\');
#else
    char* last_slash = strrchr(script_dir, '/');
#endif
    if (last_slash) {
        *last_slash = '\0';
        strcpy(script_name, last_slash + 1);
        
        // Change to script directory
        if (chdir(script_dir) != 0) {
            return -1;
        }
    } else {
        // Script is in current directory, no need to change directory
        strcpy(script_name, script_path);
        // Don't change directory in this case
    }
    
    // Build argument string
    for (int i = 1; i < argc; i++) {
        strncat(args_str, " ", sizeof(args_str) - strlen(args_str) - 1);
        strncat(args_str, argv[i], sizeof(args_str) - strlen(args_str) - 1);
    }
    
    // Build execution command using embedded runtime binary
    if (runtime == UB_RUNTIME_PHP) {
        // For PHP, use custom configuration that points to embedded extensions
        char php_ini_path[1024];
        char extensions_dir[1024];
        
        // Construct paths relative to the temp directory containing the runtime
        char* runtime_dir = strdup(runtime_binary_path);
#ifdef PLATFORM_WINDOWS
        char* last_slash = strrchr(runtime_dir, '\\');
#else
        char* last_slash = strrchr(runtime_dir, '/');
#endif
        if (last_slash) {
            *last_slash = '\0';
#ifdef PLATFORM_WINDOWS
            snprintf(extensions_dir, sizeof(extensions_dir), "%s\\extensions", runtime_dir);
            snprintf(php_ini_path, sizeof(php_ini_path), "%s\\php.ini", runtime_dir);
#else
            snprintf(extensions_dir, sizeof(extensions_dir), "%s/extensions", runtime_dir);
            snprintf(php_ini_path, sizeof(php_ini_path), "%s/php.ini", runtime_dir);
#endif
        } else {
            strcpy(extensions_dir, "./extensions");
            strcpy(php_ini_path, "./php.ini");
        }
        
        // Use custom PHP configuration that points to our embedded extensions
        snprintf(command, sizeof(command), "\"%s\" -n -d extension_dir=\"%s\" -c \"%s\" \"%s\"%s", 
                runtime_binary_path, extensions_dir, php_ini_path, script_name, args_str);
        
        free(runtime_dir);
    } else {
        // For other runtimes, use standard execution
        snprintf(command, sizeof(command), "\"%s\" \"%s\"%s", runtime_binary_path, script_name, args_str);
    }
    
#ifdef PLATFORM_WINDOWS
    // On Windows, use cmd /c to ensure proper execution
    char cmd_command[2048 + 20];
    snprintf(cmd_command, sizeof(cmd_command), "cmd /c \"%s\"", command);
    int result = system(cmd_command);
#else
    int result = system(command);
#endif
    
    // Restore original working directory
    chdir(original_dir);
    
    return result;
}

// Helper function to validate project directory
static ub_result_t validate_project_directory(const ub_config_t* config) {
    struct stat st;
    
    // Check if project directory exists
    if (stat(config->project_dir, &st) != 0) {
        fprintf(stderr, "Error: Project directory does not exist: %s\n", config->project_dir);
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: Project path is not a directory: %s\n", config->project_dir);
        return UB_ERROR_INVALID_ARGS;
    }
    
    // Check if entry point exists (if specified)
    if (config->entry_point) {
        char entry_path[1024];
        snprintf(entry_path, sizeof(entry_path), "%s%s%s", 
                config->project_dir, PATH_SEPARATOR, config->entry_point);
        
        if (stat(entry_path, &st) != 0) {
            fprintf(stderr, "Error: Entry point file not found: %s\n", entry_path);
            return UB_ERROR_FILE_NOT_FOUND;
        }
    }
    
    return UB_SUCCESS;
}

// Helper function to copy the current executable as a template
static ub_result_t copy_executable_template(const char* output_path) {
    FILE *src = NULL, *dst = NULL;
    char buffer[8192];
    size_t bytes_read;
    ub_result_t result = UB_SUCCESS;
    
    // Get the path of the current executable
    char exe_path[1024];
#ifdef PLATFORM_LINUX
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
    exe_path[len] = '\0';
#elif defined(PLATFORM_WINDOWS)
    if (GetModuleFileName(NULL, exe_path, sizeof(exe_path)) == 0) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
#elif defined(PLATFORM_MACOS)
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
#endif
    
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

// Helper function to embed project files into the executable
static ub_result_t embed_project_files(const ub_config_t* config) {
    FILE *output_file;
    char marker[] = "UBUILDER_DATA_MARKER";
    
    // Open the output file in append mode
    output_file = fopen(config->output_path, "ab");
    if (!output_file) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Write data marker
    fwrite(marker, 1, strlen(marker), output_file);
    
    // Write runtime type
    fwrite(&config->runtime, sizeof(config->runtime), 1, output_file);
    
    // Write entry point
    if (config->entry_point) {
        uint32_t entry_len = (uint32_t)strlen(config->entry_point);
        fwrite(&entry_len, sizeof(entry_len), 1, output_file);
        fwrite(config->entry_point, 1, entry_len, output_file);
    } else {
        uint32_t entry_len = 0;
        fwrite(&entry_len, sizeof(entry_len), 1, output_file);
    }
    
    // Embed project files
    ub_result_t result = embed_directory_recursive(config->project_dir, output_file);
    
    fclose(output_file);
    return result;
}

// Helper function to recursively embed directory contents
static ub_result_t embed_directory_recursive(const char* dir_path, FILE* output_file) {
    // For Phase 2, we'll implement a simple file embedding
    // In Phase 3, this will be more sophisticated
    
    char script_path[1024];
    struct stat st;
    
    // For now, just embed the main script file if it exists
    const char* extensions[] = {".py", ".php", ".js", NULL};
    
    for (int i = 0; extensions[i]; i++) {
        snprintf(script_path, sizeof(script_path), "%s/main%s", dir_path, extensions[i]);
        
        if (stat(script_path, &st) == 0) {
            FILE* script_file = fopen(script_path, "rb");
            if (script_file) {
                // Write file size
                uint32_t file_size = st.st_size;
                fwrite(&file_size, sizeof(file_size), 1, output_file);
                
                // Write file content
                char buffer[8192];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), script_file)) > 0) {
                    fwrite(buffer, 1, bytes_read, output_file);
                }
                
                fclose(script_file);
                return UB_SUCCESS;
            }
        }
    }
    
    // No main script found, write empty file
    uint32_t file_size = 0;
    fwrite(&file_size, sizeof(file_size), 1, output_file);
    
    return UB_SUCCESS;
}

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
    
    // Write modular runtime marker
    const char* modular_marker = "UBUILDER_MODULAR_V3_B64F7E2A_MARKER";
    fwrite(modular_marker, 1, strlen(modular_marker), output_file);
    
    // Write magic number for validation
    uint32_t magic_number = 0xDEADBEEF;
    fwrite(&magic_number, sizeof(magic_number), 1, output_file);
    
    // Write runtime type
    fwrite(&config->runtime, sizeof(config->runtime), 1, output_file);
    
    // 3. Embed runtime-specific runtime
    result = builder->embed_runtime(output_file);
    if (result != UB_SUCCESS) {
        fclose(output_file);
        fprintf(stderr, "Error: Failed to embed %s runtime\n", builder->name);
        return result;
    }
    
    // 4. Embed application using runtime-specific method
    result = builder->embed_application(config->project_dir, output_file);
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
    
    fclose(output_file);
    
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
    const char* base_temp_dir = get_temp_dir();
#ifdef PLATFORM_WINDOWS
    snprintf(temp_dir, sizeof(temp_dir), "%s\\ubuilder-%d", base_temp_dir, getpid());
#else
    snprintf(temp_dir, sizeof(temp_dir), "%s/ubuilder-%d", base_temp_dir, getpid());
#endif
    if (mkdir(temp_dir, 0755) != 0 && errno != EEXIST) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Extract embedded runtime binary
    result = ub_extract_runtime_binary(data_file, temp_dir, runtime_binary_path);
    if (result != UB_SUCCESS) {
        return result;
    }
    
    // For PHP runtime, extract extensions and create custom php.ini
    if (runtime == UB_RUNTIME_PHP) {
        result = ub_extract_php_extensions(data_file, temp_dir);
        if (result != UB_SUCCESS) {
            printf("Warning: Failed to extract PHP extensions, continuing...\n");
            // Continue anyway - the app might work without extensions
        }
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
    
    // Cleanup temporary directory
    char cleanup_cmd[1024];
#ifdef PLATFORM_WINDOWS
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rmdir /s /q \"%s\"", temp_dir);
#else
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf %s", temp_dir);
#endif
    system(cleanup_cmd);
    
    return (exit_code == 0) ? UB_SUCCESS : UB_ERROR_EXECUTION_FAILED;
}

// Function to run legacy embedded application (Phase 2 format)
static ub_result_t ub_run_legacy_embedded_app(FILE* data_file, int argc, char* argv[]) {
    ub_runtime_type_t runtime;
    uint32_t entry_len;
    char* entry_point = NULL;
    uint32_t file_size;
    char* script_content = NULL;
    ub_result_t result;
    
    // Read runtime type
    if (fread(&runtime, sizeof(runtime), 1, data_file) != 1) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    // Read entry point
    if (fread(&entry_len, sizeof(entry_len), 1, data_file) != 1) {
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    if (entry_len > 0) {
        entry_point = malloc(entry_len + 1);
        if (!entry_point) {
            return UB_ERROR_MEMORY_ALLOCATION;
        }
        
        if (fread(entry_point, 1, entry_len, data_file) != entry_len) {
            free(entry_point);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        entry_point[entry_len] = '\0';
    }
    
    // Read script content
    if (fread(&file_size, sizeof(file_size), 1, data_file) != 1) {
        free(entry_point);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    
    if (file_size > 0) {
        script_content = malloc(file_size + 1);
        if (!script_content) {
            free(entry_point);
            return UB_ERROR_MEMORY_ALLOCATION;
        }
        
        if (fread(script_content, 1, file_size, data_file) != file_size) {
            free(script_content);
            free(entry_point);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        script_content[file_size] = '\0';
        
        // Execute the embedded application
        result = ub_execute_embedded_app(runtime, script_content, entry_point, argc, argv);
        
        free(script_content);
    } else {
        result = UB_ERROR_FILE_NOT_FOUND;
    }
    
    free(entry_point);
    return result;
}
