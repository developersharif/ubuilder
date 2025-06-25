#include "runtime_manager.h"
#include <string.h>

// Embedded Node.js runtime data (placeholder)
static const uint8_t nodejs_runtime_data[] = {
    // Placeholder for embedded Node.js runtime
    0x4E, 0x4F, 0x44, 0x45  // "NODE"
};

static const ub_resource_t nodejs_resources[] = {
    {
        .name = "nodejs_runtime",
        .data = nodejs_runtime_data,
        .size = sizeof(nodejs_runtime_data),
        .checksum = 0xABCDEF00
    }
};

ub_result_t nodejs_runtime_init(ub_runtime_config_t* config) {
    if (!config) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    config->type = UB_RUNTIME_NODEJS;
    config->name = "Node.js";
    config->version = "18.17.0";
    
#ifdef PLATFORM_WINDOWS
    config->executable = "node.exe";
#else
    config->executable = "node";
#endif
    
    config->runtime_data = nodejs_resources;
    config->runtime_data_count = sizeof(nodejs_resources) / sizeof(nodejs_resources[0]);
    
    return UB_SUCCESS;
}
