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
#endif

// Embed Python runtime
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

#ifdef PLATFORM_WINDOWS
    // On Windows, we need to embed the entire Python directory structure
    // Extract the directory from python.exe path
    char python_dir[1024];
    strcpy(python_dir, runtime_info.binary_path);
    char* last_slash = strrchr(python_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        fprintf(stderr, "Error: Invalid Python binary path format\n");
        ub_runtime_info_cleanup(&runtime_info);
        return UB_ERROR_INVALID_ARGS;
    }
    
    printf("Python directory: %s\n", python_dir);
    
    // Embed the complete Windows Python runtime
    result = python_embed_windows_runtime(python_dir, output_file);
    if (result != UB_SUCCESS) {
        ub_runtime_info_cleanup(&runtime_info);
        return result;
    }
#else
    // On Unix-like systems, embed just the Python binary
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
            // Recursively process subdirectory
            python_embed_files_recursive(full_path, base_path, output_file);
        } else if (S_ISREG(st.st_mode)) {
            // Check if it's a Python file or other relevant file
            const char* ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".py") == 0 || strcmp(ext, ".json") == 0 || strcmp(ext, ".txt") == 0)) {
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

// Embed Python application
static ub_result_t python_embed_application(const char* project_dir, FILE* output_file) {
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
    
    // Try to embed Python standard library (Lib directory) - more comprehensive
    char lib_dir[1024];
    snprintf(lib_dir, sizeof(lib_dir), "%s\\Lib", python_dir);
    
    struct stat lib_stat;
    if (stat(lib_dir, &lib_stat) == 0) {
        // Essential standard library modules needed for basic Python operation
        const char* essential_lib_files[] = {
            "Lib\\encodings\\__init__.py",
            "Lib\\encodings\\utf_8.py",
            "Lib\\encodings\\cp1252.py",
            "Lib\\encodings\\latin_1.py",
            "Lib\\encodings\\ascii.py",
            "Lib\\codecs.py",
            "Lib\\os.py",
            "Lib\\sys.py",
            "Lib\\io.py", 
            "Lib\\_collections_abc.py",
            "Lib\\abc.py",
            "Lib\\types.py",
            "Lib\\functools.py",
            "Lib\\operator.py",
            "Lib\\keyword.py",
            "Lib\\heapq.py",
            "Lib\\reprlib.py",
            "Lib\\collections\\__init__.py",
            "Lib\\json\\__init__.py",
            "Lib\\json\\decoder.py",
            "Lib\\json\\encoder.py",
            NULL
        };
        
        for (int i = 0; essential_lib_files[i]; i++) {
            char lib_file_path[1024];
            snprintf(lib_file_path, sizeof(lib_file_path), "%s\\%s", python_dir, essential_lib_files[i]);
            
            struct stat st;
            if (stat(lib_file_path, &st) == 0) {
                FILE* lib_file = fopen(lib_file_path, "rb");
                if (lib_file) {
                    uint32_t name_len = (uint32_t)strlen(essential_lib_files[i]);
                    fwrite(&name_len, sizeof(name_len), 1, output_file);
                    fwrite(essential_lib_files[i], 1, name_len, output_file);
                    
                    uint32_t file_size = (uint32_t)st.st_size;
                    fwrite(&file_size, sizeof(file_size), 1, output_file);
                    
                    char buffer[8192];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), lib_file)) > 0) {
                        fwrite(buffer, 1, bytes_read, output_file);
                    }
                    
                    fclose(lib_file);
                    total_size += st.st_size;
                    files_embedded++;
                }
            }
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
#endif
