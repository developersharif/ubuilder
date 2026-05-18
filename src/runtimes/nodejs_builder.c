#include "runtime_builder.h"
#include "runtime_embedder.h"
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

// Embed Node.js runtime
static ub_result_t nodejs_embed_runtime(const ub_config_t* config, FILE* output_file) {
    (void)config;  /* M1-E (Node hermetic) will honor config->runtime_source. */
    ub_runtime_info_t runtime_info;
    ub_result_t result;
    
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
    // On Unix-like systems, embed just the Node.js binary
    printf("Binary size: %.2f MB\n", runtime_info.binary_size / (1024.0 * 1024.0));
    
    result = ub_embed_runtime_binary(runtime_info.binary_path, output_file);
    if (result != UB_SUCCESS) {
        ub_runtime_info_cleanup(&runtime_info);
        return result;
    }
#endif
    
    // Cleanup
    ub_runtime_info_cleanup(&runtime_info);
    
    return result;
}

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
            // Recursively process subdirectory
            nodejs_embed_files_recursive(full_path, base_path, output_file);
        } else if (S_ISREG(st.st_mode)) {
            // Check if it's a Node.js file or other relevant file
            const char* ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".js") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0)) {
                // Calculate relative path from project root
                const char* rel_path = full_path + strlen(base_path);
                if (*rel_path == '/') rel_path++; // Skip leading slash
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

// Embed Node.js application
static ub_result_t nodejs_embed_application(const char* project_dir, FILE* output_file) {
    struct stat st;
    
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
    
    // Embed all Node.js and related files recursively
    ub_result_t result = nodejs_embed_files_recursive(project_dir, project_dir, output_file);
    
    // Write end marker to indicate no more files
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);
    
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
