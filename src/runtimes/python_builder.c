#include "runtime_builder.h"
#include "runtime_embedder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

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
static ub_result_t python_embed_runtime(FILE* output_file) {
    ub_runtime_info_t runtime_info;
    ub_result_t result;
    
    // Detect Python binary on system
    result = ub_detect_runtime_binary(UB_RUNTIME_PYTHON, &runtime_info);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Python runtime not found on system\n");
        return result;
    }
    
    printf("Embedding Python runtime: %s\n", runtime_info.binary_path);
    printf("Python version: %s\n", runtime_info.version_string ? runtime_info.version_string : "unknown");
    printf("Binary size: %.2f MB\n", runtime_info.binary_size / (1024.0 * 1024.0));
    
    // Embed the actual Python binary
    result = ub_embed_runtime_binary(runtime_info.binary_path, output_file);
    
    // Cleanup
    ub_runtime_info_cleanup(&runtime_info);
    
    return result;
}

// Helper function to embed all Python files recursively
static ub_result_t python_embed_files_recursive(const char* dir_path, const char* base_path, FILE* output_file) {
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
            python_embed_files_recursive(full_path, base_path, output_file);
        } else if (S_ISREG(st.st_mode)) {
            // Check if it's a Python file or other relevant file
            const char* ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".py") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0)) {
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

// Embed Python application
static ub_result_t python_embed_application(const char* project_dir, FILE* output_file) {
    struct stat st;
    
    // Verify project directory exists
    if (stat(project_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return UB_ERROR_FILE_NOT_FOUND;
    }
    
    // Write number of files marker (we'll update this later if needed)
    uint32_t file_count_placeholder = 0;
    long file_count_pos = ftell(output_file);
    fwrite(&file_count_placeholder, sizeof(file_count_placeholder), 1, output_file);
    
    // Embed all Python and related files recursively
    ub_result_t result = python_embed_files_recursive(project_dir, project_dir, output_file);
    
    // Write end marker to indicate no more files
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);
    
    return result;
}

// Generate Python launcher
static ub_result_t python_generate_launcher(FILE* output_file) {
    // Write Python-specific launcher code
    const char* launcher_code = 
        "# Python UBuilder Launcher\n"
        "import sys, os, tempfile\n"
        "# Launcher code will be here\n";
    
    uint32_t code_size = strlen(launcher_code);
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
