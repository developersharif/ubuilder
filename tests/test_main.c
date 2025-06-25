#include <stdio.h>
#include <stdlib.h>
#include "ubuilder.h"

// Simple test framework
int test_count = 0;
int test_passed = 0;

#define TEST(name, condition) do { \
    test_count++; \
    if (condition) { \
        test_passed++; \
        printf("✓ %s\n", name); \
    } else { \
        printf("✗ %s\n", name); \
    } \
} while(0)

// Test declarations
extern void test_core_functions(void);
extern void test_runtime_manager(void);

int main(void) {
    printf("UBuilder Test Suite\n");
    printf("==================\n\n");
    
    // Initialize UBuilder
    ub_result_t result = ub_init();
    if (result != UB_SUCCESS) {
        printf("Failed to initialize UBuilder: %s\n", ub_error_string(result));
        return EXIT_FAILURE;
    }
    
    // Run tests
    test_core_functions();
    test_runtime_manager();
    
    // Cleanup
    ub_cleanup();
    
    // Print results
    printf("\nTest Results:\n");
    printf("=============\n");
    printf("Total tests: %d\n", test_count);
    printf("Passed: %d\n", test_passed);
    printf("Failed: %d\n", test_count - test_passed);
    
    if (test_passed == test_count) {
        printf("\n✓ All tests passed!\n");
        return EXIT_SUCCESS;
    } else {
        printf("\n✗ Some tests failed!\n");
        return EXIT_FAILURE;
    }
}
