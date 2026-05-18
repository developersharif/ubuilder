#ifndef UBUILDER_CORE_H
#define UBUILDER_CORE_H

// Suppress MSVC security warnings for standard C functions
#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
    #pragma warning(disable: 4100)  // Unreferenced formal parameter
    #pragma warning(disable: 4267)  // Conversion from size_t to smaller type
    #pragma warning(disable: 4996)  // POSIX function name warnings
#endif

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Version information
#define UBUILDER_VERSION_MAJOR 2
#define UBUILDER_VERSION_MINOR 0
#define UBUILDER_VERSION_PATCH 1

// Platform definitions
#ifdef _WIN32
    #ifndef PLATFORM_WINDOWS
        #define PLATFORM_WINDOWS
    #endif
    #define PATH_SEPARATOR "\\"
#elif defined(__APPLE__)
    #ifndef PLATFORM_MACOS
        #define PLATFORM_MACOS
    #endif
    #define PATH_SEPARATOR "/"
#else
    #ifndef PLATFORM_LINUX
        #define PLATFORM_LINUX
    #endif
    #define PATH_SEPARATOR "/"
#endif

// Error codes
typedef enum {
    UB_SUCCESS = 0,
    UB_ERROR_INVALID_ARGS = -1,
    UB_ERROR_FILE_NOT_FOUND = -2,
    UB_ERROR_RUNTIME_NOT_FOUND = -3,
    UB_ERROR_EXTRACTION_FAILED = -4,
    UB_ERROR_EXECUTION_FAILED = -5,
    UB_ERROR_MEMORY_ALLOCATION = -6,
    UB_ERROR_UNKNOWN = -999
} ub_result_t;

// Runtime types
typedef enum {
    UB_RUNTIME_PYTHON,
    UB_RUNTIME_PHP,
    UB_RUNTIME_NODEJS,
    UB_RUNTIME_UNKNOWN
} ub_runtime_type_t;

// Configuration structure
typedef struct {
    char* project_dir;
    char* output_path;
    char* entry_point;
    ub_runtime_type_t runtime;
    int enable_gui;
    int enable_compression;
    int verbose;
    /* M1 (hermetic interpreters): explicit path to a vendored runtime tree
     * or binary. Set by --runtime-source or runtime_options.<rt>.source in
     * ubuilder.json. If NULL and `use_host_runtime` is 0, UBuilder
     * auto-discovers a vendored runtime in $XDG_CACHE_HOME/ubuilder/runtimes/.
     * See docs/architecture/M1_HERMETIC_INTERPRETERS.md. */
    char* runtime_source;
    /* DX (post-M1): explicit opt-in to use the host's installed interpreter
     * (today's pre-M1 behavior, NOT portable). When set, suppresses cache
     * auto-discovery and falls straight to host probe. */
    int   use_host_runtime;
    /* M8: when set, skip installing user dependencies (requirements.txt,
     * etc.) into the embedded runtime. Default is auto-install when a
     * dependency manifest is present in the project directory. */
    int   no_install_deps;
    /* DX (post-M8): when set, skip the "vendored runtime missing → auto-
     * spawn vendor-runtimes.sh" path. Useful in CI where the cache is
     * pre-populated and we want to fail loudly on a miss. */
    int   no_auto_vendor;
} ub_config_t;

// Embedded resource structure
typedef struct {
    const char* name;
    const uint8_t* data;
    size_t size;
    uint32_t checksum;
} ub_resource_t;

// Core functions
ub_result_t ub_init(void);
ub_result_t ub_cleanup(void);
ub_result_t ub_build_executable(const ub_config_t* config);
ub_result_t ub_extract_runtime(ub_runtime_type_t runtime, const char* temp_dir);
ub_result_t ub_execute_application(const char* temp_dir, const char* entry_point, int argc, char** argv);
ub_result_t ub_check_and_run_embedded_app(int argc, char* argv[]);

// Utility functions
const char* ub_get_version_string(void);
const char* ub_get_platform_name(void);
const char* ub_error_string(ub_result_t error);
ub_runtime_type_t ub_parse_runtime(const char* runtime_str);

// Resource management
ub_result_t ub_embed_resources(const char* source_dir, ub_resource_t** resources, size_t* count);
ub_result_t ub_extract_resource(const ub_resource_t* resource, const char* output_path);
void ub_free_resources(ub_resource_t* resources, size_t count);

#ifdef __cplusplus
}
#endif

#endif // UBUILDER_CORE_H
