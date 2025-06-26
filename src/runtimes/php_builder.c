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
#ifdef PLATFORM_WINDOWS
static ub_result_t php_embed_windows_runtime(const char* php_dir, FILE* output_file);
#endif

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
    
#ifdef PLATFORM_WINDOWS
    // On Windows, we need to embed the entire PHP directory structure
    // Extract the directory from php.exe path
    char php_dir[1024];
    strcpy(php_dir, runtime_info.binary_path);
    char* last_slash = strrchr(php_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        fprintf(stderr, "Error: Invalid PHP binary path format\n");
        ub_runtime_info_cleanup(&runtime_info);
        return UB_ERROR_INVALID_ARGS;
    }
    
    printf("PHP directory: %s\n", php_dir);
    
    // Embed the complete Windows PHP runtime
    result = php_embed_windows_runtime(php_dir, output_file);
    if (result != UB_SUCCESS) {
        ub_runtime_info_cleanup(&runtime_info);
        return result;
    }
#else
    // On Unix-like systems, embed just the PHP binary
    printf("Binary size: %.2f MB\n", runtime_info.binary_size / (1024.0 * 1024.0));
    
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
#endif
    
    // Cleanup
    ub_runtime_info_cleanup(&runtime_info);
    
    return UB_SUCCESS;
}

// Function to embed complete Windows PHP runtime
// Helper function to check if extension is enabled in php.ini
static int is_extension_enabled_in_ini(const char* php_ini_path, const char* ext_name) {
    FILE* ini_file = fopen(php_ini_path, "r");
    if (!ini_file) {
        return 0; // If no php.ini, assume extension could be useful
    }
    
    char line[512];
    char search_pattern[256];
    
    // Create search patterns: extension=ext_name or extension=ext_name.dll
    snprintf(search_pattern, sizeof(search_pattern), "extension=%s", ext_name);
    
    while (fgets(line, sizeof(line), ini_file)) {
        // Skip comments and empty lines
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == ';' || *trimmed == '#' || *trimmed == '\n' || *trimmed == '\0') {
            continue;
        }
        
        // Check if this line enables our extension
        if (strstr(trimmed, search_pattern) != NULL) {
            fclose(ini_file);
            return 1;
        }
    }
    
    fclose(ini_file);
    return 0;
}

// Helper function to check if an extension is built into PHP
static int is_builtin_extension(const char* ext_name) {
    const char* builtin_extensions[] = {
        // Core extensions that are typically built into PHP
        "mbstring.so", "json.so", "ctype.so", "iconv.so", 
        "hash.so", "filter.so", "pcre.so", "spl.so", "date.so",
        "core.so", "standard.so", "phar.so", "reflection.so",
        
        // Extensions commonly built-in on modern PHP installations
        "bcmath.so", "calendar.so", "curl.so", "dom.so", "exif.so",
        "fileinfo.so", "ftp.so", "gd.so", "gettext.so", "libxml.so",
        "openssl.so", "pdo.so", "pdo_sqlite.so", "posix.so", "random.so",
        "session.so", "simplexml.so", "sockets.so", "sqlite3.so",
        "tokenizer.so", "xml.so", "xmlreader.so", "xmlwriter.so",
        "xsl.so", "zip.so", "zlib.so", "bz2.so", "pcntl.so",
        "readline.so", "shmop.so", "sodium.so", "sysvmsg.so",
        "sysvsem.so", "sysvshm.so", "ffi.so", "mysqlnd.so",
        "mcrypt.so", "ssh2.so",
        
        NULL
    };
    
    for (int i = 0; builtin_extensions[i]; i++) {
        if (strcmp(ext_name, builtin_extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

#ifdef PLATFORM_WINDOWS
static ub_result_t php_embed_windows_runtime(const char* php_dir, FILE* output_file) {
    // Essential files needed for PHP to run on Windows
    const char* essential_files[] = {
        "php.exe",
        "php.ini",             // Configuration (if exists)
        NULL
    };
    
    // Optional DLL files that may or may not be present depending on PHP build
    const char* optional_files[] = {
        "php8ts.dll",          // Main PHP engine (Thread Safe)
        "php8.dll",            // Main PHP engine (Non-Thread Safe)
        "libcrypto-3-x64.dll", // OpenSSL crypto
        "libssl-3-x64.dll",    // OpenSSL
        "icudt72.dll",         // ICU data
        "icudt73.dll",         // ICU data (newer version)
        "icuin72.dll",         // ICU internationalization  
        "icuin73.dll",         // ICU internationalization (newer version)
        "icuuc72.dll",         // ICU common
        "icuuc73.dll",         // ICU common (newer version)
        "libsqlite3.dll",      // SQLite
        "ssleay32.dll",        // OpenSSL (older naming)
        "libeay32.dll",        // OpenSSL (older naming)
        "msvcr110.dll",        // Visual C++ runtime
        "msvcr120.dll",        // Visual C++ runtime
        "msvcr140.dll",        // Visual C++ runtime
        NULL
    };
    
    // Core extensions that should always be included (commonly used)
    const char* core_extensions[] = {
        "php_mbstring.dll",    // Multi-byte string support (essential)
        "php_openssl.dll",     // SSL/TLS support (essential for HTTPS)
        "php_curl.dll",        // HTTP client functionality
        "php_fileinfo.dll",    // File type detection
        "php_sqlite3.dll",     // SQLite database support
        "php_pdo_sqlite.dll",  // PDO SQLite driver
        "php_json.dll",        // JSON support (often built-in, but include if available)
        "php_filter.dll",      // Input filtering
        "php_hash.dll",        // Hashing functions
        "php_ctype.dll",       // Character type checking
        NULL
    };
    
    printf("Embedding Windows PHP runtime from: %s\n", php_dir);
    
    size_t total_size = 0;
    int files_embedded = 0;
    
    // Check if php.ini exists for extension detection
    char php_ini_path[1024];
    snprintf(php_ini_path, sizeof(php_ini_path), "%s\\php.ini", php_dir);
    struct stat ini_stat;
    int has_php_ini = (stat(php_ini_path, &ini_stat) == 0);
    
    // First, embed essential core files (required)
    for (int i = 0; essential_files[i]; i++) {
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s\\%s", php_dir, essential_files[i]);
        
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
        } else if (strcmp(essential_files[i], "php.ini") != 0) {
            // php.ini is optional, but php.exe is required
            printf("  Warning: Required file not found: %s\n", file_path);
        }
    }
    
    // Then, embed optional runtime files (best effort)
    for (int i = 0; optional_files[i]; i++) {
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s\\%s", php_dir, optional_files[i]);
        
        struct stat st;
        if (stat(file_path, &st) == 0) {
            FILE* file = fopen(file_path, "rb");
            if (file) {
                printf("  Found optional file: %s (%.2f KB)\n", optional_files[i], st.st_size / 1024.0);
                
                // Write filename length and name
                uint32_t name_len = (uint32_t)strlen(optional_files[i]);
                fwrite(&name_len, sizeof(name_len), 1, output_file);
                fwrite(optional_files[i], 1, name_len, output_file);
                
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
        }
    }
    
    // Then, embed extensions intelligently
    char ext_dir[1024];
    snprintf(ext_dir, sizeof(ext_dir), "%s\\ext", php_dir);
    
    // First embed core extensions
    for (int i = 0; core_extensions[i]; i++) {
        char ext_path[1024];
        snprintf(ext_path, sizeof(ext_path), "%s\\%s", ext_dir, core_extensions[i]);
        
        struct stat st;
        if (stat(ext_path, &st) == 0) {
            FILE* ext_file = fopen(ext_path, "rb");
            if (ext_file) {
                // Write extension filename with ext/ prefix
                char ext_name[256];
                snprintf(ext_name, sizeof(ext_name), "ext\\%s", core_extensions[i]);
                uint32_t name_len = (uint32_t)strlen(ext_name);
                fwrite(&name_len, sizeof(name_len), 1, output_file);
                fwrite(ext_name, 1, name_len, output_file);
                
                // Write file size
                uint32_t file_size = (uint32_t)st.st_size;
                fwrite(&file_size, sizeof(file_size), 1, output_file);
                
                // Write file content
                char buffer[8192];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), ext_file)) > 0) {
                    fwrite(buffer, 1, bytes_read, output_file);
                }
                
                fclose(ext_file);
                total_size += st.st_size;
                files_embedded++;
            }
        }
    }
    
    // Now scan ext/ directory for additional extensions
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char search_path[1024];
    
    snprintf(search_path, sizeof(search_path), "%s\\*.dll", ext_dir);
    find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            // Skip if this is already a core extension
            int is_core = 0;
            for (int i = 0; core_extensions[i]; i++) {
                if (strcmp(find_data.cFileName, core_extensions[i]) == 0) {
                    is_core = 1;
                    break;
                }
            }
            
            if (!is_core) {
                // Check if extension is enabled in php.ini or seems useful
                int should_embed = 0;
                
                if (has_php_ini) {
                    // Remove .dll extension for ini check
                    char ext_name_no_dll[256];
                    strncpy(ext_name_no_dll, find_data.cFileName, sizeof(ext_name_no_dll));
                    char* dll_pos = strstr(ext_name_no_dll, ".dll");
                    if (dll_pos) *dll_pos = '\0';
                    
                    // Remove php_ prefix if present for ini check
                    char* ext_name_for_ini = ext_name_no_dll;
                    if (strncmp(ext_name_no_dll, "php_", 4) == 0) {
                        ext_name_for_ini = ext_name_no_dll + 4;
                    }
                    
                    should_embed = is_extension_enabled_in_ini(php_ini_path, ext_name_for_ini);
                } else {
                    // Without php.ini, embed commonly useful extensions
                    const char* useful_extensions[] = {
                        "php_pdo.dll", "php_pdo_mysql.dll", "php_mysqli.dll", "php_gd.dll",
                        "php_zip.dll", "php_xml.dll", "php_dom.dll", "php_session.dll",
                        "php_pcre.dll", "php_spl.dll", "php_standard.dll", "php_date.dll",
                        "php_exif.dll", "php_intl.dll", "php_xsl.dll", "php_ffi.dll",
                        NULL
                    };
                    
                    for (int i = 0; useful_extensions[i]; i++) {
                        if (strcmp(find_data.cFileName, useful_extensions[i]) == 0) {
                            should_embed = 1;
                            break;
                        }
                    }
                }
                
                if (should_embed) {
                    char ext_path[1024];
                    snprintf(ext_path, sizeof(ext_path), "%s\\%s", ext_dir, find_data.cFileName);
                    
                    struct stat st;
                    if (stat(ext_path, &st) == 0) {
                        FILE* ext_file = fopen(ext_path, "rb");
                        if (ext_file) {
                            char ext_name[256];
                            snprintf(ext_name, sizeof(ext_name), "ext\\%s", find_data.cFileName);
                            uint32_t name_len = (uint32_t)strlen(ext_name);
                            fwrite(&name_len, sizeof(name_len), 1, output_file);
                            fwrite(ext_name, 1, name_len, output_file);
                            
                            uint32_t file_size = (uint32_t)st.st_size;
                            fwrite(&file_size, sizeof(file_size), 1, output_file);
                            
                            char buffer[8192];
                            size_t bytes_read;
                            while ((bytes_read = fread(buffer, 1, sizeof(buffer), ext_file)) > 0) {
                                fwrite(buffer, 1, bytes_read, output_file);
                            }
                            
                            fclose(ext_file);
                            total_size += st.st_size;
                            files_embedded++;
                        }
                    }
                }
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    // Write end marker (empty filename)
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);
    
    printf("Windows PHP runtime embedded: %d files, %.2f MB total\n", 
           files_embedded, total_size / (1024.0 * 1024.0));
    
    return UB_SUCCESS;
}
#endif

// Function to embed essential PHP extensions
static ub_result_t php_embed_extensions(FILE* output_file) {
    // Get PHP extension directory using php-config or default path
    char ext_dir[1024];
    
#ifdef PLATFORM_WINDOWS
    // On Windows, try to get extension directory from registry or use default
    strcpy(ext_dir, "C:\\php\\ext");  // Default Windows PHP extension path
#else
    FILE* php_config = popen("php-config --extension-dir 2>/dev/null", "r");
    if (php_config && fgets(ext_dir, sizeof(ext_dir), php_config)) {
        // Remove newline
        ext_dir[strcspn(ext_dir, "\n")] = 0;
        pclose(php_config);
    } else {
        if (php_config) pclose(php_config);
        // Fallback to common paths
        strcpy(ext_dir, "/usr/lib/php/20240924");
    }
#endif
    
#ifdef PLATFORM_WINDOWS
    printf("Embedding ALL available PHP extensions from: %s (for future use)\n", ext_dir);
#else
    printf("Embedding ALL available PHP extensions from: %s (for future use)\n", ext_dir);
#endif
    
    // Write marker for extension section
    const char* ext_marker = "PHP_EXTENSIONS_START";
    uint32_t marker_len = (uint32_t)strlen(ext_marker);
    fwrite(&marker_len, sizeof(marker_len), 1, output_file);
    fwrite(ext_marker, 1, marker_len, output_file);
    
    int extensions_embedded = 0;
    
    // Scan extension directory and embed ALL extension files
    // They will be available but not automatically loaded to prevent conflicts
#ifdef PLATFORM_WINDOWS
    // On Windows, scan for .dll files in ext directory
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char search_path[1024];
    
    snprintf(search_path, sizeof(search_path), "%s\\*.dll", ext_dir);
    find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            char ext_path[1024];
            snprintf(ext_path, sizeof(ext_path), "%s\\%s", ext_dir, find_data.cFileName);
            
            struct stat st;
            if (stat(ext_path, &st) == 0) {
                FILE* ext_file = fopen(ext_path, "rb");
                if (ext_file) {
                    printf("  Embedding extension: %s (%.2f KB)\n", 
                           find_data.cFileName, st.st_size / 1024.0);
                    
                    // Write extension filename length and name
                    uint32_t name_len = (uint32_t)strlen(find_data.cFileName);
                    fwrite(&name_len, sizeof(name_len), 1, output_file);
                    fwrite(find_data.cFileName, 1, name_len, output_file);
                    
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
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
#else
    // On Unix-like systems, scan for .so files
    DIR* dir = opendir(ext_dir);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            // Only process .so files
            if (strstr(entry->d_name, ".so") && strlen(entry->d_name) > 3) {
                char ext_path[1024];
                snprintf(ext_path, sizeof(ext_path), "%s/%s", ext_dir, entry->d_name);
                
                struct stat st;
                if (stat(ext_path, &st) == 0 && S_ISREG(st.st_mode)) {
                    FILE* ext_file = fopen(ext_path, "rb");
                    if (ext_file) {
                        printf("  Embedding extension: %s (%.2f KB)\n", 
                               entry->d_name, st.st_size / 1024.0);
                        
                        // Write extension filename length and name
                        uint32_t name_len = (uint32_t)strlen(entry->d_name);
                        fwrite(&name_len, sizeof(name_len), 1, output_file);
                        fwrite(entry->d_name, 1, name_len, output_file);
                        
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
        }
        closedir(dir);
    }
#endif
    
    // Write end marker (empty name)
    uint32_t end_marker = 0;
    fwrite(&end_marker, sizeof(end_marker), 1, output_file);
    
    printf("Embedded %d PHP extensions (available but not auto-loaded for compatibility)\n", extensions_embedded);
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
