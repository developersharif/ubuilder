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
static ub_result_t python_embed_runtime(const ub_config_t* config, FILE* output_file) {
    ub_result_t result;

#ifndef PLATFORM_WINDOWS
    /* M1: hermetic mode if --runtime-source points at a directory. */
    if (config && config->runtime_source) {
        struct stat st;
        if (stat(config->runtime_source, &st) != 0) {
            fprintf(stderr, "Error: --runtime-source not found: %s\n", config->runtime_source);
            return UB_ERROR_FILE_NOT_FOUND;
        }
        if (S_ISDIR(st.st_mode)) {
            printf("Embedding hermetic Python tree: %s\n", config->runtime_source);
            return ub_embed_runtime_tree(config->runtime_source, output_file);
        }
        if (S_ISREG(st.st_mode)) {
            printf("Embedding user-chosen Python binary: %s\n", config->runtime_source);
            return ub_embed_runtime_single_as_tree(config->runtime_source, "bin/python3", output_file);
        }
        fprintf(stderr, "Error: --runtime-source must be a file or directory\n");
        return UB_ERROR_INVALID_ARGS;
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
           "      Run `scripts/vendor-runtimes.sh python` + --runtime-source for a hermetic bundle.\n");
    result = ub_embed_runtime_single_as_tree(runtime_info.binary_path, "bin/python3", output_file);
#endif

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
