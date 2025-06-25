#ifndef RUNTIME_MANAGER_H
#define RUNTIME_MANAGER_H

#include "../core/ubuilder.h"

#ifdef __cplusplus
extern "C" {
#endif

// Runtime configuration
typedef struct {
    ub_runtime_type_t type;
    const char* name;
    const char* executable;
    const char* version;
    const ub_resource_t* runtime_data;
    size_t runtime_data_count;
} ub_runtime_config_t;

// Runtime operations
ub_result_t runtime_manager_init(void);
ub_result_t runtime_manager_cleanup(void);

ub_result_t runtime_get_config(ub_runtime_type_t type, ub_runtime_config_t** config);
ub_result_t runtime_extract(const ub_runtime_config_t* config, const char* temp_dir);
ub_result_t runtime_execute(const ub_runtime_config_t* config, const char* temp_dir, 
                           const char* script_path, int argc, char** argv);

// Runtime-specific implementations
ub_result_t python_runtime_init(ub_runtime_config_t* config);
ub_result_t php_runtime_init(ub_runtime_config_t* config);
ub_result_t nodejs_runtime_init(ub_runtime_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_MANAGER_H
