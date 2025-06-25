#include "ubuilder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PLATFORM_WINDOWS
#include <getopt.h>

// Command line options for Unix/Linux
static struct option long_options[] = {
    {"project-dir", required_argument, 0, 'p'},
    {"runtime", required_argument, 0, 'r'},
    {"output", required_argument, 0, 'o'},
    {"entry-point", required_argument, 0, 'e'},
    {"gui", no_argument, 0, 'g'},
    {"verbose", no_argument, 0, 'v'},
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, 'V'},
    {0, 0, 0, 0}
};
#endif

static void print_usage(const char* program_name) {
    printf("UBuilder - Universal Executable Framework\n");
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("Options:\n");
    printf("  -p, --project-dir PATH    Source project directory\n");
    printf("  -r, --runtime RUNTIME     Runtime type (python|php|node)\n");
    printf("  -o, --output PATH         Output executable path\n");
    printf("  -e, --entry-point FILE    Application entry point\n");
    printf("  -g, --gui                 Enable GUI support\n");
    printf("  -v, --verbose             Enable verbose output\n");
    printf("  -h, --help                Show this help message\n");
    printf("  -V, --version             Show version information\n\n");
    printf("Examples:\n");
    printf("  %s --project-dir=./myapp --runtime=python --output=myapp\n", program_name);
    printf("  %s --project-dir=./webapp --runtime=php --gui --output=webapp\n", program_name);
}

static void print_version(void) {
    printf("UBuilder %s\n", ub_get_version_string());
    printf("Platform: %s\n", ub_get_platform_name());
    printf("Build date: %s %s\n", __DATE__, __TIME__);
}

static ub_result_t parse_arguments(int argc, char* argv[], ub_config_t* config) {
    // Initialize config with defaults
    memset(config, 0, sizeof(ub_config_t));
    config->runtime = UB_RUNTIME_UNKNOWN;
    
#ifdef PLATFORM_WINDOWS
    // Windows-compatible argument parsing
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--project-dir") == 0 || strcmp(argv[i], "-p") == 0) {
            if (i + 1 < argc) {
                config->project_dir = strdup(argv[++i]);
            } else {
                fprintf(stderr, "Error: --project-dir requires an argument\n");
                return UB_ERROR_INVALID_ARGS;
            }
        } else if (strcmp(argv[i], "--runtime") == 0 || strcmp(argv[i], "-r") == 0) {
            if (i + 1 < argc) {
                config->runtime = ub_parse_runtime(argv[++i]);
                if (config->runtime == UB_RUNTIME_UNKNOWN) {
                    fprintf(stderr, "Error: Unknown runtime '%s'\n", argv[i]);
                    return UB_ERROR_INVALID_ARGS;
                }
            } else {
                fprintf(stderr, "Error: --runtime requires an argument\n");
                return UB_ERROR_INVALID_ARGS;
            }
        } else if (strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                config->output_path = strdup(argv[++i]);
            } else {
                fprintf(stderr, "Error: --output requires an argument\n");
                return UB_ERROR_INVALID_ARGS;
            }
        } else if (strcmp(argv[i], "--entry-point") == 0 || strcmp(argv[i], "-e") == 0) {
            if (i + 1 < argc) {
                config->entry_point = strdup(argv[++i]);
            } else {
                fprintf(stderr, "Error: --entry-point requires an argument\n");
                return UB_ERROR_INVALID_ARGS;
            }
        } else if (strcmp(argv[i], "--gui") == 0 || strcmp(argv[i], "-g") == 0) {
            config->enable_gui = 1;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            config->verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return UB_ERROR_INVALID_ARGS; // Exit after help
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            print_version();
            return UB_ERROR_INVALID_ARGS; // Exit after version
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return UB_ERROR_INVALID_ARGS;
        }
    }
#else
    // Unix/Linux getopt-based parsing
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "p:r:o:e:gvhV", long_options, &option_index)) != -1) {
        switch (c) {
            case 'p':
                config->project_dir = strdup(optarg);
                break;
            case 'r':
                config->runtime = ub_parse_runtime(optarg);
                if (config->runtime == UB_RUNTIME_UNKNOWN) {
                    fprintf(stderr, "Error: Unknown runtime '%s'\n", optarg);
                    return UB_ERROR_INVALID_ARGS;
                }
                break;
            case 'o':
                config->output_path = strdup(optarg);
                break;
            case 'e':
                config->entry_point = strdup(optarg);
                break;
            case 'g':
                config->enable_gui = 1;
                break;
            case 'v':
                config->verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return UB_ERROR_INVALID_ARGS; // Exit after help
            case 'V':
                print_version();
                return UB_ERROR_INVALID_ARGS; // Exit after version
            case '?':
                return UB_ERROR_INVALID_ARGS;
            default:
                break;
        }
    }
#endif
    
    // Validate required arguments
    if (!config->project_dir) {
        fprintf(stderr, "Error: --project-dir is required\n");
        return UB_ERROR_INVALID_ARGS;
    }
    
    if (config->runtime == UB_RUNTIME_UNKNOWN) {
        fprintf(stderr, "Error: --runtime is required\n");
        return UB_ERROR_INVALID_ARGS;
    }
    
    if (!config->output_path) {
        fprintf(stderr, "Error: --output is required\n");
        return UB_ERROR_INVALID_ARGS;
    }
    
    return UB_SUCCESS;
}

static void free_config(ub_config_t* config) {
    if (config) {
        free(config->project_dir);
        free(config->output_path);
        free(config->entry_point);
        memset(config, 0, sizeof(ub_config_t));
    }
}

int main(int argc, char* argv[]) {
    ub_config_t config;
    ub_result_t result;
    
    // Initialize UBuilder
    result = ub_init();
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize UBuilder: %s\n", ub_error_string(result));
        return EXIT_FAILURE;
    }
    
    // Check if this is an embedded application first
    result = ub_check_and_run_embedded_app(argc, argv);
    if (result == UB_SUCCESS) {
        // Successfully ran as embedded app
        ub_cleanup();
        return EXIT_SUCCESS;
    } else if (result != UB_ERROR_RUNTIME_NOT_FOUND) {
        // Error running embedded app
        fprintf(stderr, "Error: Failed to run embedded application: %s\n", ub_error_string(result));
        ub_cleanup();
        return EXIT_FAILURE;
    }
    
    // Not an embedded app, continue with normal UBuilder CLI
    
    // Parse command line arguments
    result = parse_arguments(argc, argv, &config);
    if (result != UB_SUCCESS) {
        if (result == UB_ERROR_INVALID_ARGS) {
            // Help or version was shown, or parsing failed
            free_config(&config);
            ub_cleanup();
            return (result == UB_ERROR_INVALID_ARGS && 
                   (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "--version") == 0))) 
                   ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        fprintf(stderr, "Error: %s\n", ub_error_string(result));
        free_config(&config);
        ub_cleanup();
        return EXIT_FAILURE;
    }
    
    if (config.verbose) {
        printf("UBuilder %s starting...\n", ub_get_version_string());
        printf("Platform: %s\n", ub_get_platform_name());
    }
    
    // Build the executable
    result = ub_build_executable(&config);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Build failed: %s\n", ub_error_string(result));
        free_config(&config);
        ub_cleanup();
        return EXIT_FAILURE;
    }
    
    if (config.verbose) {
        printf("Build completed successfully!\n");
    }
    
    // Cleanup
    free_config(&config);
    ub_cleanup();
    
    return EXIT_SUCCESS;
}
