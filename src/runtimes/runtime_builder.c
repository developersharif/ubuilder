#include "runtime_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global builder registry
static const ub_runtime_builder_t* g_builders[] = {
    &python_builder,
    &php_builder,
    &nodejs_builder,
    NULL
};

ub_result_t runtime_builder_init(void) {
    // Initialize any global state for builders
    return UB_SUCCESS;
}

ub_result_t runtime_builder_cleanup(void) {
    // Cleanup global state
    return UB_SUCCESS;
}

ub_result_t runtime_builder_get(ub_runtime_type_t type, ub_runtime_builder_t** builder) {
    if (!builder) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    for (int i = 0; g_builders[i]; i++) {
        if (g_builders[i]->runtime_type == type) {
            *builder = (ub_runtime_builder_t*)g_builders[i];
            return UB_SUCCESS;
        }
    }
    
    return UB_ERROR_RUNTIME_NOT_FOUND;
}

ub_result_t runtime_builder_list_available(ub_runtime_type_t** types, size_t* count) {
    if (!types || !count) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    // Count available builders
    size_t builder_count = 0;
    while (g_builders[builder_count]) {
        builder_count++;
    }
    
    // Allocate array for runtime types
    ub_runtime_type_t* runtime_types = malloc(builder_count * sizeof(ub_runtime_type_t));
    if (!runtime_types) {
        return UB_ERROR_MEMORY_ALLOCATION;
    }
    
    // Fill array
    for (size_t i = 0; i < builder_count; i++) {
        runtime_types[i] = g_builders[i]->runtime_type;
    }
    
    *types = runtime_types;
    *count = builder_count;
    
    return UB_SUCCESS;
}
