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

// Forward declarations
static ub_result_t php_embed_extensions(FILE* output_file);

// PHP runtime validation
static ub_result_t php_validate_project(const char* project_dir) {
    char main_php_path[1024];
    struct stat st;
    
    // Check for main.php or other PHP files
    snprintf(main_php_path, sizeof(main_php_path), "%s/main.php", project_dir);
    
    if (stat(main_php_path, &st) == 0) {
        return UB_SUCCESS;
    }
    
    // Look for index.php
    snprintf(main_php_path, sizeof(main_php_path), "%s/index.php", project_dir);
    if (stat(main_php_path, &st) == 0) {
        return UB_SUCCESS;
    }
    
    return UB_ERROR_FILE_NOT_FOUND;
}

// Embed PHP runtime
static ub_result_t php_embed_runtime(FILE* output_file) {
    ub_runtime_info_t runtime_info;
    ub_result_t result;
    
    // Detect PHP binary on system
    result = ub_detect_runtime_binary(UB_RUNTIME_PHP, &runtime_info);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: PHP runtime not found on system\n");
        return result;
    }
    
    printf("Embedding PHP runtime: %s\n", runtime_info.binary_path);
    printf("PHP version: %s\n", runtime_info.version_string ? runtime_info.version_string : "unknown");
    printf("Binary size: %.2f MB\n", runtime_info.binary_size / (1024.0 * 1024.0));
    
    // Embed the actual PHP binary
    result = ub_embed_runtime_binary(runtime_info.binary_path, output_file);
    if (result != UB_SUCCESS) {
        ub_runtime_info_cleanup(&runtime_info);
        return result;
    }
    
    // Embed essential PHP extensions
    result = php_embed_extensions(output_file);
    if (result != UB_SUCCESS) {
        printf("Warning: Failed to embed some PHP extensions, continuing...\n");
        // Don't fail the build, but warn about potential issues
    }
    
    // Cleanup
    ub_runtime_info_cleanup(&runtime_info);
    
    return UB_SUCCESS;
}

// Function to embed essential PHP extensions
static ub_result_t php_embed_extensions(FILE* output_file) {
    // Essential PHP extensions that are commonly needed
    const char* essential_extensions[] = {
        "mbstring.so",
        "json.so", 
        "ctype.so",
        "iconv.so",
        "fileinfo.so",
        "curl.so",
        "openssl.so",
        "pcre.so",
        "hash.so",
        "filter.so",
        "xml.so",
        "dom.so",
        NULL
    };
    
    // Get PHP extension directory using php-config or default path
    char ext_dir[1024];
    FILE* php_config = popen("php-config --extension-dir 2>/dev/null", "r");
    if (php_config && fgets(ext_dir, sizeof(ext_dir), php_config)) {
        // Remove newline
        ext_dir[strcspn(ext_dir, "\n")] = 0;
        pclose(php_config);
    } else {
        if (php_config) pclose(php_config);
        // Fallback to common paths
#ifdef PLATFORM_WINDOWS
        #ifdef _MSC_VER
            strcpy_s(ext_dir, sizeof(ext_dir), "C:\\php\\ext");
        #else
            strncpy(ext_dir, "C:\\php\\ext", sizeof(ext_dir) - 1);
            ext_dir[sizeof(ext_dir) - 1] = '\0';
        #endif
#else
        strcpy(ext_dir, "/usr/lib/php/20240924");
#endif
    }
    
    printf("Embedding PHP extensions from: %s\n", ext_dir);
    
    // Write marker for extension section
    const char* ext_marker = "PHP_EXTENSIONS_START";
    uint32_t marker_len = (uint32_t)strlen(ext_marker);
    fwrite(&marker_len, sizeof(marker_len), 1, output_file);
    fwrite(ext_marker, 1, marker_len, output_file);
    
    int extensions_embedded = 0;
    
    // Embed each essential extension if it exists
    for (int i = 0; essential_extensions[i]; i++) {
        char ext_path[1024];
        snprintf(ext_path, sizeof(ext_path), "%s/%s", ext_dir, essential_extensions[i]);
        
        struct stat st;
        if (stat(ext_path, &st) == 0) {
            FILE* ext_file = fopen(ext_path, "rb");
            if (ext_file) {
                // Write extension filename length and name
                uint32_t name_len = (uint32_t)strlen(essential_extensions[i]);
                fwrite(&name_len, sizeof(name_len), 1, output_file);
                fwrite(essential_extensions[i], 1, name_len, output_file);
                
                // Write extension file size
                uint32_t ext_size = st.st_size;
                fwrite(&ext_size, sizeof(ext_size), 1, output_file);
                
                // Write extension content
                char buffer[8192];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), ext_file)) > 0) {
                    fwrite(buffer, 1, bytes_read, output_file);
                }
                
                fclose(ext_file);
                extensions_embedded++;
            }
        }
    }
    
    // Write end marker (empty name)
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);
    
    if (extensions_embedded > 0) {
        printf("Embedded %d PHP extensions\n", extensions_embedded);
    }
    return UB_SUCCESS;
}

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
            if (ext && (strcmp(ext, ".php") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0)) {
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
            php_embed_files_recursive(full_path, base_path, output_file);
        } else if (S_ISREG(st.st_mode)) {
            // Check if it's a PHP file or other relevant file
            const char* ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".php") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0)) {
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

// Embed PHP application
static ub_result_t php_embed_application(const char* project_dir, FILE* output_file) {
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
    
    // Embed all PHP and related files recursively
    ub_result_t result = php_embed_files_recursive(project_dir, project_dir, output_file);
    
    // Write end marker to indicate no more files
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);
    
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
