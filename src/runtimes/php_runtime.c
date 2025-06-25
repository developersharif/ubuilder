#include "runtime_manager.h"
#include <string.h>

// Embedded PHP runtime data (placeholder)
static const uint8_t php_runtime_data[] = {
    // Placeholder for embedded PHP runtime
    0x50, 0x48, 0x50  // "PHP"
};

static const ub_resource_t php_resources[] = {
    {
        .name = "php_runtime",
        .data = php_runtime_data,
        .size = sizeof(php_runtime_data),
        .checksum = 0x87654321
    }
};

ub_result_t php_runtime_init(ub_runtime_config_t* config) {
    if (!config) {
        return UB_ERROR_INVALID_ARGS;
    }
    
    config->type = UB_RUNTIME_PHP;
    config->name = "PHP";
    config->version = "8.2.0";
    
#ifdef PLATFORM_WINDOWS
    config->executable = "php.exe";
#else
    config->executable = "php";
#endif
    
    config->runtime_data = php_resources;
    config->runtime_data_count = sizeof(php_resources) / sizeof(php_resources[0]);
    
    return UB_SUCCESS;
}
