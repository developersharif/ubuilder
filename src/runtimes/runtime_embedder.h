#ifndef RUNTIME_EMBEDDER_H
#define RUNTIME_EMBEDDER_H

#include "../core/ubuilder.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runtime binary embedding utilities
typedef struct {
    char* binary_path;
    char* binary_name;
    size_t binary_size;
    char* version_string;
} ub_runtime_info_t;

// Function to detect and get runtime information
ub_result_t ub_detect_runtime_binary(ub_runtime_type_t runtime, ub_runtime_info_t* info);

// Function to embed runtime binary into output file
ub_result_t ub_embed_runtime_binary(const char* binary_path, FILE* output_file);

// Function to extract embedded runtime binary
ub_result_t ub_extract_runtime_binary(FILE* input_file, const char* temp_dir, char* extracted_path);

// Function to extract PHP extensions and create custom php.ini
ub_result_t ub_extract_php_extensions(FILE* input_file, const char* temp_dir);

// Cleanup function
void ub_runtime_info_cleanup(ub_runtime_info_t* info);

#ifdef PLATFORM_WINDOWS
// Windows-specific functions
ub_result_t ub_extract_windows_php_runtime(FILE* input_file, const char* temp_dir, char* extracted_path);
ub_result_t ub_extract_windows_nodejs_runtime(FILE* input_file, const char* temp_dir, char* extracted_path);
ub_result_t ub_extract_windows_python_runtime(FILE* input_file, const char* temp_dir, char* extracted_path);
#endif

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_EMBEDDER_H
