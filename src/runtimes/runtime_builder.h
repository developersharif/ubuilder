#ifndef RUNTIME_BUILDER_H
#define RUNTIME_BUILDER_H

#include "../core/ubuilder.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runtime-specific builder interface
typedef struct {
    ub_runtime_type_t runtime_type;
    const char* name;
    const char* description;
    
    // Builder functions
    ub_result_t (*validate_project)(const char* project_dir);
    ub_result_t (*embed_runtime)(FILE* output_file);
    ub_result_t (*embed_application)(const char* project_dir, FILE* output_file);
    ub_result_t (*generate_launcher)(FILE* output_file);
    
    // Runtime information
    size_t estimated_runtime_size;
    const char** required_files;
    const char** supported_extensions;
} ub_runtime_builder_t;

// Builder registry functions
ub_result_t runtime_builder_init(void);
ub_result_t runtime_builder_cleanup(void);
ub_result_t runtime_builder_get(ub_runtime_type_t type, ub_runtime_builder_t** builder);
ub_result_t runtime_builder_list_available(ub_runtime_type_t** types, size_t* count);

// Runtime-specific builders
extern const ub_runtime_builder_t python_builder;
extern const ub_runtime_builder_t php_builder;
extern const ub_runtime_builder_t nodejs_builder;

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_BUILDER_H
