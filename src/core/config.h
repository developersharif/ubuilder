#ifndef UBUILDER_CONFIG_H
#define UBUILDER_CONFIG_H

#include "ubuilder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bitfield tracking which CLI flags were explicitly set by the user.
 * The config-file layer only fills fields whose presence bit is 0. */
typedef struct {
    unsigned project_dir    : 1;
    unsigned output         : 1;
    unsigned entry_point    : 1;
    unsigned runtime        : 1;
    unsigned gui            : 1;
    unsigned verbose        : 1;
    unsigned compression    : 1;
    unsigned runtime_source    : 1;
    unsigned use_host_runtime  : 1;
    unsigned no_install_deps   : 1;
    unsigned no_auto_vendor    : 1;
    unsigned exclude           : 1;
    unsigned php_runtime_static : 1;
} ub_cli_presence_t;

typedef struct ub_config_file ub_config_file_t;

/*
 * Resolve and load a config file.
 *
 *   explicit_path     — if non-NULL, the path passed via --config; missing file
 *                       is treated as an error and returned to the caller.
 *   project_dir_hint  — if non-NULL, the value of --project-dir; used to look
 *                       for <hint>/ubuilder.json.
 *
 * Discovery order (first match wins):
 *   1. explicit_path
 *   2. <project_dir_hint>/ubuilder.json
 *   3. ./ubuilder.json
 *
 * On success returns UB_SUCCESS. *out_file is non-NULL if a config was found
 * and loaded; NULL if discovery found nothing (this is not an error).
 *
 * On parse/validation failure returns an error code and writes a diagnostic
 * to stderr; *out_file is NULL.
 */
ub_result_t ub_config_load(const char* explicit_path,
                           const char* project_dir_hint,
                           ub_config_file_t** out_file);

/*
 * Apply config-file values to `cfg` where `presence` says the CLI did not set
 * the field. Also sets `cfg->project_dir` to the directory containing the
 * config file when the CLI didn't supply --project-dir.
 *
 * Returns UB_SUCCESS even if file == NULL (no-op).
 */
ub_result_t ub_config_apply(const ub_config_file_t*    file,
                            const ub_cli_presence_t*   presence,
                            ub_config_t*               cfg);

/* Path the config was loaded from (for verbose / error messages). NULL-safe. */
const char* ub_config_path(const ub_config_file_t* file);

void ub_config_free(ub_config_file_t* file);

/*
 * If no ubuilder.json is present in `cfg->project_dir`, write one capturing
 * the runtime, entry_point, output basename, and any --exclude patterns
 * actually used by this build. Idempotent: a no-op when the file already
 * exists.
 *
 * Returns UB_SUCCESS even on a no-op. Returns an error if writing failed
 * (which the caller may treat as a soft warning — the build has already
 * succeeded by the time we get here).
 */
ub_result_t ub_config_write_if_missing(const ub_config_t* cfg);

#ifdef __cplusplus
}
#endif

#endif
