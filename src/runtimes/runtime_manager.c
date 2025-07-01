#include "runtime_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global runtime configurations
static ub_runtime_config_t* g_runtime_configs[4] = {0}; // Support for 4 runtimes
static int g_runtime_manager_initialized = 0;

ub_result_t runtime_manager_init(void) {
    if (g_runtime_manager_initialized) {
        return UB_SUCCESS;
    }
    
    // Initialize Python runtime
    g_runtime_configs[UB_RUNTIME_PYTHON] = malloc(sizeof(ub_runtime_config_t));
    if (g_runtime_configs[UB_RUNTIME_PYTHON]) {
        python_runtime_init(g_runtime_configs[UB_RUNTIME_PYTHON]);
    }
    
    // Initialize PHP runtime
    g_runtime_configs[UB_RUNTIME_PHP] = malloc(sizeof(ub_runtime_config_t));
    if (g_runtime_configs[UB_RUNTIME_PHP]) {
        php_runtime_init(g_runtime_configs[UB_RUNTIME_PHP]);
    }
    
    // Initialize Node.js runtime
    g_runtime_configs[UB_RUNTIME_NODEJS] = malloc(sizeof(ub_runtime_config_t));
    if (g_runtime_configs[UB_RUNTIME_NODEJS]) {
        nodejs_runtime_init(g_runtime_configs[UB_RUNTIME_NODEJS]);
    }
    
    g_runtime_manager_initialized = 1;
    return UB_SUCCESS;
}

ub_result_t runtime_manager_cleanup(void) {
    if (!g_runtime_manager_initialized) {
        return UB_SUCCESS;
    }
    
    for (int i = 0; i < 4; i++) {
        if (g_runtime_configs[i]) {
            free(g_runtime_configs[i]);
            g_runtime_configs[i] = NULL;
        }
    }
    
    g_runtime_manager_initialized = 0;
    return UB_SUCCESS;
}

ub_result_t runtime_get_config(ub_runtime_type_t type, ub_runtime_config_t** config) {
    if (!config) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    if (!g_runtime_manager_initialized) {
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }
    
    if (type >= 0 && type < 4 && g_runtime_configs[type]) {
        *config = g_runtime_configs[type];
        return UB_SUCCESS;
    }
    
    return UB_ERROR_RUNTIME_NOT_FOUND;
}

ub_result_t runtime_extract(const ub_runtime_config_t* config, const char* temp_dir) {
    if (!config || !temp_dir) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    printf("Extracting %s runtime to %s\n", config->name, temp_dir);
    
    // TODO: Extract embedded runtime files
    // For now, just create the directory structure
    
    return UB_SUCCESS;
}

ub_result_t runtime_execute(const ub_runtime_config_t* config, const char* temp_dir,
                           const char* script_path, int argc, char** argv) {
    if (!config || !temp_dir || !script_path) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    // Suppress unused parameter warnings for future implementation
    (void)argc;
    (void)argv;
    
    printf("Executing %s script: %s\n", config->name, script_path);
    
    // TODO: Build and execute runtime command
    // This will be runtime-specific implementation
    
    return UB_SUCCESS;
}
