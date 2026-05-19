#include "ubuilder.h"
#include "core/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PLATFORM_WINDOWS
#  include <direct.h>
#  define getcwd _getcwd
#else
#  include <unistd.h>
#endif

#ifndef PLATFORM_WINDOWS
#include <getopt.h>

/* getopt long-options table.
 * '-1' is used as a short-option marker for long-only options (--config). */
static struct option long_options[] = {
    {"project-dir", required_argument, 0, 'p'},
    {"runtime",     required_argument, 0, 'r'},
    {"output",      required_argument, 0, 'o'},
    {"entry-point", required_argument, 0, 'e'},
    {"config",            required_argument, 0,  1 },
    {"runtime-source",    required_argument, 0,  2 },
    {"use-host-runtime",  no_argument,       0,  3 },
    {"no-install-deps",   no_argument,       0,  4 },
    {"no-auto-vendor",    no_argument,       0,  5 },
    {"exclude",           required_argument, 0,  6 },
    {"gui",               no_argument,       0, 'g'},
    {"verbose",     no_argument,       0, 'v'},
    {"help",        no_argument,       0, 'h'},
    {"version",     no_argument,       0, 'V'},
    {0, 0, 0, 0}
};
#endif

/* Trailing path segment of `p` written to `out`. Treats both / and \ as
 * separators. If `p` ends in a separator or is empty, writes "". */
static void main_path_basename(const char* p, char* out, size_t cap) {
    if (!p || !*p) { if (cap) out[0] = 0; return; }
    const char* slash = NULL;
    for (const char* q = p; *q; q++) {
        if (*q == '/' || *q == '\\') slash = q;
    }
    const char* base = slash ? slash + 1 : p;
    snprintf(out, cap, "%s", base);
}

/* Append `pattern` to `cfg->exclude` if it isn't already present. */
static int append_exclude_pattern(ub_config_t* cfg, const char* pattern) {
    if (!pattern || !*pattern) return 0;
    for (size_t i = 0; i < cfg->exclude_count; i++) {
        if (cfg->exclude[i] && strcmp(cfg->exclude[i], pattern) == 0) return 0;
    }
    size_t n = cfg->exclude_count;
    char** grown = (char**)realloc(cfg->exclude, (n + 1) * sizeof(char*));
    if (!grown) return -1;
    grown[n] = strdup(pattern);
    if (!grown[n]) { cfg->exclude = grown; return -1; }
    cfg->exclude = grown;
    cfg->exclude_count = n + 1;
    return 0;
}

/* Final config-fill step, runs after ub_config_apply and before
 * validate_required. Two responsibilities:
 *
 *   (a) Default `output_path` to `dist/<name>` if still unset. <name> is
 *       derived from basename(project_dir); falls back to "app".
 *
 *   (b) Auto-exclude the output directory from the recursive app embed
 *       so the (growing) output file doesn't get walked into the bundle.
 *       For "dist/app" the exclude pattern is "dist/**"; for a root-level
 *       output like "app" the exclude is just "app". Only fires for
 *       relative output paths (absolute paths typically live outside the
 *       project tree anyway).
 *
 * Both behaviors are silent unless --verbose is set, so the zero-flag DX
 * stays clean. */
static ub_result_t fill_defaults(ub_config_t* config) {
    /* (a) default output */
    if (!config->output_path) {
        char name_buf[256] = {0};
        if (config->project_dir && *config->project_dir) {
            main_path_basename(config->project_dir, name_buf, sizeof(name_buf));
            if (!name_buf[0] || strcmp(name_buf, ".") == 0) {
                /* project_dir was "." (or empty after basename) — resolve to cwd basename. */
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd))) {
                    main_path_basename(cwd, name_buf, sizeof(name_buf));
                }
            }
        }
        if (!name_buf[0]) snprintf(name_buf, sizeof(name_buf), "app");
        char out_buf[512];
        snprintf(out_buf, sizeof(out_buf), "dist/%s", name_buf);
        config->output_path = strdup(out_buf);
        if (!config->output_path) return UB_ERROR_MEMORY_ALLOCATION;
        if (config->verbose) {
            fprintf(stderr, "ubuilder: no output specified — defaulting to %s\n",
                    config->output_path);
        }
    }

    /* (b) auto-exclude the output directory */
    if (config->output_path && config->output_path[0]) {
        const char* op = config->output_path;
        /* Skip absolute paths — they typically live outside the project. */
        int is_abs = (op[0] == '/' || op[0] == '\\' ||
                      (op[0] && op[1] == ':'));
        if (!is_abs) {
            /* Find first path-segment boundary so we exclude the top-level
             * output directory's whole subtree, mirroring how .gitignore's
             * `dist/` works in practice. */
            const char* slash = strchr(op, '/');
            if (!slash) slash = strchr(op, '\\');
            if (slash && slash != op) {
                size_t n = (size_t)(slash - op);
                char pat[260];
                if (n + 4 < sizeof(pat)) {
                    memcpy(pat, op, n);
                    memcpy(pat + n, "/**", 4);
                    if (append_exclude_pattern(config, pat) != 0)
                        return UB_ERROR_MEMORY_ALLOCATION;
                    if (config->verbose) {
                        fprintf(stderr, "ubuilder: auto-excluding output tree '%s'\n", pat);
                    }
                }
            } else {
                /* Root-level output (no slash) → exclude the bare filename. */
                if (append_exclude_pattern(config, op) != 0)
                    return UB_ERROR_MEMORY_ALLOCATION;
                if (config->verbose) {
                    fprintf(stderr, "ubuilder: auto-excluding output file '%s'\n", op);
                }
            }
        }
    }

    return UB_SUCCESS;
}

/* Append one --exclude argument to config->exclude. Bumps presence so the
 * config-file layer knows to merge rather than overwrite. */
static int append_exclude(ub_config_t* config, ub_cli_presence_t* presence,
                          const char* value) {
    if (!value || !*value) {
        fprintf(stderr, "Error: --exclude requires a non-empty pattern\n");
        return -1;
    }
    size_t n = config->exclude_count;
    char** grown = (char**)realloc(config->exclude, (n + 1) * sizeof(char*));
    if (!grown) return -1;
    grown[n] = strdup(value);
    if (!grown[n]) { config->exclude = grown; return -1; }
    config->exclude = grown;
    config->exclude_count = n + 1;
    presence->exclude = 1;
    return 0;
}

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
    printf("      --runtime-source PATH Vendored interpreter directory or binary to embed.\n");
    printf("                            Default: auto-discover from\n");
    printf("                            $XDG_CACHE_HOME/ubuilder/runtimes/<rt>/* if present;\n");
    printf("                            otherwise fall back to host probe (non-portable).\n");
    printf("                            See docs/architecture/M1_HERMETIC_INTERPRETERS.md.\n");
    printf("      --use-host-runtime    Explicit opt-in to use the host's interpreter.\n");
    printf("                            Skips cache auto-discovery. Bundle will NOT be portable.\n");
    printf("                            Useful for fast local dev iteration.\n");
    printf("      --no-install-deps     Skip installing user dependencies into the embedded\n");
    printf("                            runtime. Default: when requirements.txt (Python) is\n");
    printf("                            present in the project, deps are pip-installed into a\n");
    printf("                            staged copy of the vendored runtime before bundling.\n");
    printf("      --no-auto-vendor      Don't auto-spawn scripts/vendor-runtimes.sh when the\n");
    printf("                            cache is empty. Default: vendor automatically on miss.\n");
    printf("      --exclude PATTERN     Drop a file glob or PHP `ext-<name>` from the bundle.\n");
    printf("                            Repeatable. Appends to ubuilder.json's \"exclude\" array.\n");
    printf("                            Globs: * (one segment), ** (cross /), ?, [abc], [a-z].\n");
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
        } else if (strcmp(arg, "--use-host-runtime") == 0) {
            config->use_host_runtime = 1;
            presence->use_host_runtime = 1;
        } else if (strcmp(arg, "--no-install-deps") == 0) {
            config->no_install_deps = 1;
            presence->no_install_deps = 1;
        } else if (strcmp(arg, "--no-auto-vendor") == 0) {
            config->no_auto_vendor = 1;
            presence->no_auto_vendor = 1;
        } else if (strcmp(arg, "--exclude") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: --exclude requires an argument\n"); return UB_ERROR_INVALID_ARGS; }
            if (append_exclude(config, presence, argv[++i]) != 0) return UB_ERROR_MEMORY_ALLOCATION;
        } else if (strncmp(arg, "--exclude=", 10) == 0) {
            if (append_exclude(config, presence, arg + 10) != 0) return UB_ERROR_INVALID_ARGS;
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
            case 3: /* --use-host-runtime */
                config->use_host_runtime = 1;
                presence->use_host_runtime = 1;
                break;
            case 4: /* --no-install-deps */
                config->no_install_deps = 1;
                presence->no_install_deps = 1;
                break;
            case 5: /* --no-auto-vendor */
                config->no_auto_vendor = 1;
                presence->no_auto_vendor = 1;
                break;
            case 6: /* --exclude */
                if (append_exclude(config, presence, optarg) != 0)
                    return UB_ERROR_MEMORY_ALLOCATION;
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
    for (size_t i = 0; i < config->exclude_count; i++) free(config->exclude[i]);
    free(config->exclude);
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

    /* Fill in defaults that aren't part of config_apply (output path,
     * auto-exclude). Runs after config_apply so we don't shadow values
     * the user explicitly set, and before validate_required so the
     * "output is required" check sees the defaulted value. */
    result = fill_defaults(&config);
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
            printf("Runtime source: %s (explicit)\n", config.runtime_source);
        } else if (config.use_host_runtime) {
            printf("Runtime: host (explicit --use-host-runtime; bundle will NOT be portable)\n");
        } else {
            printf("Runtime: auto (will try cache, fall back to host if empty)\n");
        }
    }

    result = ub_build_executable(&config);
    if (result != UB_SUCCESS) {
        /* Skip the generic category roll-up for UB_ERROR_INVALID_ARGS —
         * those cases always print specific guidance downstream (e.g. the
         * "output path is a directory" message in copy_executable_template),
         * and the "Error: Build failed: Invalid arguments" tail just adds
         * noise. Keep the wrapper for other failure modes (memory, I/O,
         * spawn) where the downstream might not always print context. */
        if (result != UB_ERROR_INVALID_ARGS) {
            fprintf(stderr, "Error: Build failed: %s\n", ub_error_string(result));
        }
        ub_config_free(cfg_file);
        free_config(&config);
        free(config_path);
        ub_cleanup();
        return EXIT_FAILURE;
    }

    /* Auto-generate ubuilder.json on first build (no-op if it already
     * exists). Captures the runtime/entry_point/exclude values actually
     * used, so the next build picks them up with no flags. */
    if (!cfg_file) {
        ub_config_write_if_missing(&config);
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
