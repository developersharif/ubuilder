#include <stdio.h>
#include "ubuilder.h"
#include "runtimes/runtime_manager.h"

// Test framework macros
extern int test_count;
extern int test_passed;

#define TEST(name, condition) do { \
    test_count++; \
    if (condition) { \
        test_passed++; \
        printf("✓ %s\n", name); \
    } else { \
        printf("✗ %s\n", name); \
    } \
} while(0)

void test_runtime_manager(void) {
    printf("Testing Runtime Manager:\n");
    printf("------------------------\n");
    
    // Test runtime manager initialization
    ub_result_t result = runtime_manager_init();
    TEST("runtime_manager_init() succeeds", result == UB_SUCCESS);
    
    // Test getting runtime configurations
    ub_runtime_config_t* config = NULL;
    
    result = runtime_get_config(UB_RUNTIME_PYTHON, &config);
    TEST("runtime_get_config(UB_RUNTIME_PYTHON) succeeds", result == UB_SUCCESS);
    TEST("Python runtime config is valid", config != NULL && config->name != NULL);
    
    result = runtime_get_config(UB_RUNTIME_PHP, &config);
    TEST("runtime_get_config(UB_RUNTIME_PHP) succeeds", result == UB_SUCCESS);
    TEST("PHP runtime config is valid", config != NULL && config->name != NULL);
    
    result = runtime_get_config(UB_RUNTIME_NODEJS, &config);
    TEST("runtime_get_config(UB_RUNTIME_NODEJS) succeeds", result == UB_SUCCESS);
    TEST("Node.js runtime config is valid", config != NULL && config->name != NULL);
    
    result = runtime_get_config(UB_RUNTIME_UNKNOWN, &config);
    TEST("runtime_get_config(UB_RUNTIME_UNKNOWN) fails appropriately", 
         result == UB_ERROR_RUNTIME_NOT_FOUND);
    
    // Test runtime manager cleanup
    result = runtime_manager_cleanup();
    TEST("runtime_manager_cleanup() succeeds", result == UB_SUCCESS);
    
    printf("\n");
}
