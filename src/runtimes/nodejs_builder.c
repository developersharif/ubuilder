#include "runtime_builder.h"
#include "runtime_embedder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

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

// Embed Node.js runtime
static ub_result_t nodejs_embed_runtime(FILE* output_file) {
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
    printf("Binary size: %.2f MB\n", runtime_info.binary_size / (1024.0 * 1024.0));
    
    // Embed the actual Node.js binary
    result = ub_embed_runtime_binary(runtime_info.binary_path, output_file);
    
    // Cleanup
    ub_runtime_info_cleanup(&runtime_info);
    
    return result;
}

// Helper function to embed all Node.js files recursively
static ub_result_t nodejs_embed_files_recursive(const char* dir_path, const char* base_path, FILE* output_file) {
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
                
                // Write file metadata: relative path length and content
                uint32_t path_len = strlen(rel_path);
                fwrite(&path_len, sizeof(path_len), 1, output_file);
                fwrite(rel_path, 1, path_len, output_file);
                
                // Write file content
                FILE* file = fopen(full_path, "rb");
                if (file) {
                    uint32_t file_size = st.st_size;
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
    }
    
    closedir(dir);
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
    long file_count_pos = ftell(output_file);
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
    
    uint32_t code_size = strlen(launcher_code);
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
