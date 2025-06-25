#include <stdio.h>
#include <string.h>
#include "ubuilder.h"

// Test framework macros (same as in test_main.c)
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

void test_core_functions(void) {
    printf("Testing Core Functions:\n");
    printf("-----------------------\n");
    
    // Test version string
    const char* version = ub_get_version_string();
    TEST("ub_get_version_string returns non-null", version != NULL);
    TEST("ub_get_version_string returns valid version", strlen(version) > 0);
    
    // Test platform name
    const char* platform = ub_get_platform_name();
    TEST("ub_get_platform_name returns non-null", platform != NULL);
    TEST("ub_get_platform_name returns valid platform", strlen(platform) > 0);
    
    // Test error string function
    const char* error_str = ub_error_string(UB_SUCCESS);
    TEST("ub_error_string(UB_SUCCESS) returns success message", 
         error_str != NULL && strstr(error_str, "Success") != NULL);
    
    error_str = ub_error_string(UB_ERROR_INVALID_ARGS);
    TEST("ub_error_string(UB_ERROR_INVALID_ARGS) returns error message",
         error_str != NULL && strlen(error_str) > 0);
    
    // Test runtime parsing
    TEST("ub_parse_runtime('python') returns UB_RUNTIME_PYTHON",
         ub_parse_runtime("python") == UB_RUNTIME_PYTHON);
    
    TEST("ub_parse_runtime('php') returns UB_RUNTIME_PHP",
         ub_parse_runtime("php") == UB_RUNTIME_PHP);
    
    TEST("ub_parse_runtime('node') returns UB_RUNTIME_NODEJS",
         ub_parse_runtime("node") == UB_RUNTIME_NODEJS);
    
    TEST("ub_parse_runtime('nodejs') returns UB_RUNTIME_NODEJS",
         ub_parse_runtime("nodejs") == UB_RUNTIME_NODEJS);
    
    TEST("ub_parse_runtime('unknown') returns UB_RUNTIME_UNKNOWN",
         ub_parse_runtime("unknown") == UB_RUNTIME_UNKNOWN);
    
    TEST("ub_parse_runtime(NULL) returns UB_RUNTIME_UNKNOWN",
         ub_parse_runtime(NULL) == UB_RUNTIME_UNKNOWN);
    
    printf("\n");
}
