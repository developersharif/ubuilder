#include "ubuilder.h"
#include "core/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PLATFORM_WINDOWS
#include <getopt.h>

/* getopt long-options table.
 * '-1' is used as a short-option marker for long-only options (--config). */
static struct option long_options[] = {
    {"project-dir", required_argument, 0, 'p'},
    {"runtime",     required_argument, 0, 'r'},
    {"output",      required_argument, 0, 'o'},
    {"entry-point", required_argument, 0, 'e'},
    {"config",         required_argument, 0,  1 },
    {"runtime-source", required_argument, 0,  2 },
    {"gui",            no_argument,       0, 'g'},
    {"verbose",     no_argument,       0, 'v'},
    {"help",        no_argument,       0, 'h'},
    {"version",     no_argument,       0, 'V'},
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
    printf("      --config PATH         Explicit config file (default: ./ubuilder.json,\n");
    printf("                            or <project-dir>/ubuilder.json)\n");
    printf("      --runtime-source PATH Vendored interpreter directory or binary to embed\n");
    printf("                            (M1; default: probe host PATH). See\n");
    printf("                            docs/architecture/M1_HERMETIC_INTERPRETERS.md.\n");
    printf("  -g, --gui                 Enable GUI support\n");
    printf("  -v, --verbose             Enable verbose output\n");
    printf("  -h, --help                Show this help message\n");
    printf("  -V, --version             Show version information\n\n");
    printf("Config file (optional):\n");
    printf("  If ./ubuilder.json (or <project-dir>/ubuilder.json) exists, its values\n");
    printf("  are used as defaults. CLI flags always override the config file.\n");
    printf("  See docs/architecture/CONFIG_FILE_SPEC.md for the full schema.\n\n");
    printf("Examples:\n");
    printf("  %s --project-dir=./myapp --runtime=python --output=myapp\n", program_name);
    printf("  cd myapp && %s                # uses ./ubuilder.json\n", program_name);
}

static void print_version(void) {
    printf("UBuilder %s\n", ub_get_version_string());
    printf("Platform: %s\n", ub_get_platform_name());
    printf("Build date: %s %s\n", __DATE__, __TIME__);
}

static ub_result_t parse_arguments(int argc, char* argv[],
                                   ub_config_t*       config,
                                   ub_cli_presence_t* presence,
                                   char**             config_path_out) {
    memset(config,   0, sizeof(*config));
    memset(presence, 0, sizeof(*presence));
    *config_path_out = NULL;
    config->runtime = UB_RUNTIME_UNKNOWN;

#ifdef PLATFORM_WINDOWS
    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];

        if (strcmp(arg, "--project-dir") == 0 || strcmp(arg, "-p") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: --project-dir requires an argument\n"); return UB_ERROR_INVALID_ARGS; }
            config->project_dir = strdup(argv[++i]);
            presence->project_dir = 1;
        } else if (strncmp(arg, "--project-dir=", 14) == 0) {
            config->project_dir = strdup(arg + 14);
            presence->project_dir = 1;
        } else if (strcmp(arg, "--runtime") == 0 || strcmp(arg, "-r") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: --runtime requires an argument\n"); return UB_ERROR_INVALID_ARGS; }
            config->runtime = ub_parse_runtime(argv[++i]);
            if (config->runtime == UB_RUNTIME_UNKNOWN) { fprintf(stderr, "Error: Unknown runtime '%s'\n", argv[i]); return UB_ERROR_INVALID_ARGS; }
            presence->runtime = 1;
        } else if (strncmp(arg, "--runtime=", 10) == 0) {
            config->runtime = ub_parse_runtime(arg + 10);
            if (config->runtime == UB_RUNTIME_UNKNOWN) { fprintf(stderr, "Error: Unknown runtime '%s'\n", arg + 10); return UB_ERROR_INVALID_ARGS; }
            presence->runtime = 1;
        } else if (strcmp(arg, "--output") == 0 || strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: --output requires an argument\n"); return UB_ERROR_INVALID_ARGS; }
            config->output_path = strdup(argv[++i]);
            presence->output = 1;
        } else if (strncmp(arg, "--output=", 9) == 0) {
            config->output_path = strdup(arg + 9);
            presence->output = 1;
        } else if (strcmp(arg, "--entry-point") == 0 || strcmp(arg, "-e") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: --entry-point requires an argument\n"); return UB_ERROR_INVALID_ARGS; }
            config->entry_point = strdup(argv[++i]);
            presence->entry_point = 1;
        } else if (strncmp(arg, "--entry-point=", 14) == 0) {
            config->entry_point = strdup(arg + 14);
            presence->entry_point = 1;
        } else if (strcmp(arg, "--config") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: --config requires an argument\n"); return UB_ERROR_INVALID_ARGS; }
            *config_path_out = strdup(argv[++i]);
        } else if (strncmp(arg, "--config=", 9) == 0) {
            *config_path_out = strdup(arg + 9);
        } else if (strcmp(arg, "--runtime-source") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: --runtime-source requires an argument\n"); return UB_ERROR_INVALID_ARGS; }
            config->runtime_source = strdup(argv[++i]);
            presence->runtime_source = 1;
        } else if (strncmp(arg, "--runtime-source=", 17) == 0) {
            config->runtime_source = strdup(arg + 17);
            presence->runtime_source = 1;
        } else if (strcmp(arg, "--gui") == 0 || strcmp(arg, "-g") == 0) {
            config->enable_gui = 1;
            presence->gui = 1;
        } else if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
            config->verbose = 1;
            presence->verbose = 1;
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            return UB_ERROR_INVALID_ARGS;
        } else if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0) {
            print_version();
            return UB_ERROR_INVALID_ARGS;
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", arg);
            return UB_ERROR_INVALID_ARGS;
        }
    }
#else
    int option_index = 0;
    int c;
    while ((c = getopt_long(argc, argv, "p:r:o:e:gvhV", long_options, &option_index)) != -1) {
        switch (c) {
            case 'p':
                config->project_dir = strdup(optarg);
                presence->project_dir = 1;
                break;
            case 'r':
                config->runtime = ub_parse_runtime(optarg);
                if (config->runtime == UB_RUNTIME_UNKNOWN) {
                    fprintf(stderr, "Error: Unknown runtime '%s'\n", optarg);
                    return UB_ERROR_INVALID_ARGS;
                }
                presence->runtime = 1;
                break;
            case 'o':
                config->output_path = strdup(optarg);
                presence->output = 1;
                break;
            case 'e':
                config->entry_point = strdup(optarg);
                presence->entry_point = 1;
                break;
            case 1: /* --config */
                *config_path_out = strdup(optarg);
                break;
            case 2: /* --runtime-source */
                config->runtime_source = strdup(optarg);
                presence->runtime_source = 1;
                break;
            case 'g':
                config->enable_gui = 1;
                presence->gui = 1;
                break;
            case 'v':
                config->verbose = 1;
                presence->verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return UB_ERROR_INVALID_ARGS;
            case 'V':
                print_version();
                return UB_ERROR_INVALID_ARGS;
            case '?':
                return UB_ERROR_INVALID_ARGS;
            default:
                break;
        }
    }
#endif

    return UB_SUCCESS;
}

static ub_result_t validate_required(const ub_config_t* config) {
    if (!config->project_dir) {
        fprintf(stderr, "Error: project directory is required (pass --project-dir, "
                        "or run from a directory containing ubuilder.json)\n");
        return UB_ERROR_INVALID_ARGS;
    }
    if (config->runtime == UB_RUNTIME_UNKNOWN) {
        fprintf(stderr, "Error: runtime is required (pass --runtime, or set \"runtime\" in ubuilder.json)\n");
        return UB_ERROR_INVALID_ARGS;
    }
    if (!config->output_path) {
        fprintf(stderr, "Error: output is required (pass --output, or set \"output\" in ubuilder.json)\n");
        return UB_ERROR_INVALID_ARGS;
    }
    return UB_SUCCESS;
}

static void free_config(ub_config_t* config) {
    if (!config) return;
    free(config->project_dir);
    free(config->output_path);
    free(config->entry_point);
    free(config->runtime_source);
    memset(config, 0, sizeof(*config));
}

int main(int argc, char* argv[]) {
    ub_config_t       config;
    ub_cli_presence_t presence;
    char*             config_path = NULL;
    ub_config_file_t* cfg_file    = NULL;
    ub_result_t       result;

    result = ub_init();
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize UBuilder: %s\n", ub_error_string(result));
        return EXIT_FAILURE;
    }

    /* Embedded-app fast path. */
    result = ub_check_and_run_embedded_app(argc, argv);
    if (result == UB_SUCCESS) {
        ub_cleanup();
        return EXIT_SUCCESS;
    } else if (result != UB_ERROR_RUNTIME_NOT_FOUND) {
        fprintf(stderr, "Error: Failed to run embedded application: %s\n", ub_error_string(result));
        ub_cleanup();
        return EXIT_FAILURE;
    }

    /* CLI mode. */
    result = parse_arguments(argc, argv, &config, &presence, &config_path);
    if (result != UB_SUCCESS) {
        free_config(&config);
        free(config_path);
        ub_cleanup();
        int help_or_version =
            (argc > 1 && (strcmp(argv[1], "--help") == 0 ||
                          strcmp(argv[1], "-h") == 0 ||
                          strcmp(argv[1], "--version") == 0 ||
                          strcmp(argv[1], "-V") == 0));
        return help_or_version ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Load + apply config file (no-op if none is present). */
    result = ub_config_load(config_path, config.project_dir, &cfg_file);
    if (result != UB_SUCCESS) {
        free_config(&config);
        free(config_path);
        ub_cleanup();
        return EXIT_FAILURE;
    }
    if (cfg_file && config.verbose) {
        fprintf(stderr, "ubuilder: using config %s\n", ub_config_path(cfg_file));
    }
    result = ub_config_apply(cfg_file, &presence, &config);
    if (result != UB_SUCCESS) {
        ub_config_free(cfg_file);
        free_config(&config);
        free(config_path);
        ub_cleanup();
        return EXIT_FAILURE;
    }

    result = validate_required(&config);
    if (result != UB_SUCCESS) {
        ub_config_free(cfg_file);
        free_config(&config);
        free(config_path);
        ub_cleanup();
        return EXIT_FAILURE;
    }

    if (config.verbose) {
        printf("UBuilder %s starting...\n", ub_get_version_string());
        printf("Platform: %s\n", ub_get_platform_name());
        if (config.runtime_source) {
            printf("Runtime source: %s\n", config.runtime_source);
            printf("note: --runtime-source / runtime_options.<rt>.source is plumbed (M1-A)\n"
                   "      but the builder still uses the host runtime in this build.\n"
                   "      M1-B wires the vendored tree into the embed path.\n");
        }
    }

    result = ub_build_executable(&config);
    if (result != UB_SUCCESS) {
        fprintf(stderr, "Error: Build failed: %s\n", ub_error_string(result));
        ub_config_free(cfg_file);
        free_config(&config);
        free(config_path);
        ub_cleanup();
        return EXIT_FAILURE;
    }

    if (config.verbose) {
        printf("Build completed successfully!\n");
    }

    ub_config_free(cfg_file);
    free_config(&config);
    free(config_path);
    ub_cleanup();
    return EXIT_SUCCESS;
}
