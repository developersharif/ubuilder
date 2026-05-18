#ifndef RUNTIME_EMBEDDER_H
#define RUNTIME_EMBEDDER_H

#include "../core/ubuilder.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runtime binary embedding utilities
typedef struct {
    char* binary_path;
    char* binary_name;
    size_t binary_size;
    char* version_string;
} ub_runtime_info_t;

// Function to detect and get runtime information
ub_result_t ub_detect_runtime_binary(ub_runtime_type_t runtime, ub_runtime_info_t* info);

// Function to embed runtime binary into output file
ub_result_t ub_embed_runtime_binary(const char* binary_path, FILE* output_file);

// Function to extract embedded runtime binary
ub_result_t ub_extract_runtime_binary(FILE* input_file, const char* temp_dir, char* extracted_path);

/*
 * M1: tree-format embed/extract. Header is [u32 magic 'UBRT']. Records
 * follow as [u16 path_len][path][u32 mode][u64 size][bytes], terminated
 * by a single [u16 path_len=0] sentinel. Paths use '/' separators.
 *
 * The sentinel design (rather than a leading count) lets the writer
 * stream records into an append-mode output file without seeking back
 * to patch a count — important since the bundle writer keeps the output
 * in append mode for the whole build.
 *
 * Used by the hermetic Python path (and PHP/Node when M1-D/E land).
 * Honors the Apple-sandbox rule: extraction creates files with `mode`
 * preserved (executables stay executable), not 0644 + a chmod afterwards.
 */
#define UB_RUNTIME_TREE_MAGIC 0x54524255u  /* 'UBRT' little-endian */
ub_result_t ub_embed_runtime_tree(const char* source_dir, FILE* output_file);
ub_result_t ub_extract_runtime_tree(FILE* input_file, const char* dest_dir);

/*
 * M1: shortcut for the "host runtime fallback" path — write a one-record
 * tree-format payload containing the single binary at the supplied
 * `dest_rel_path` (e.g. "bin/python3", "bin/node", "bin/php"). Used by
 * every *_builder.c when --runtime-source is unset or points at a file.
 *
 * Honors the same tree format as ub_embed_runtime_tree so the launcher
 * has one extraction path for all bundles.
 */
ub_result_t ub_embed_runtime_single_as_tree(const char* binary_path,
                                            const char* dest_rel_path,
                                            FILE*       output_file);

/*
 * DX (post-M1): auto-discover a vendored runtime in the local cache.
 * Looks at  $UBUILDER_RUNTIMES_CACHE / $XDG_CACHE_HOME/ubuilder/runtimes /
 * ~/.cache/ubuilder/runtimes  in that order, then under
 * <cache>/<subdir>/ for any subdirectory containing the named relative
 * executable (e.g. python/<ver>/bin/python3). Returns 0 and writes the
 * absolute path into `out` (size `out_cap`) on success, -1 if no usable
 * cache entry is found.
 *
 * Lex sort of the version directories gives "newest first" since both
 * python-build-standalone and nodejs.org use sortable version strings.
 */
int ub_runtime_cache_lookup(const char* cache_subdir,
                            const char* rel_exe,
                            char*       out,
                            size_t      out_cap);

/*
 * DX (post-M8): locate `scripts/vendor-runtimes.sh` relative to the current
 * executable. Probe order:
 *   1. $UBUILDER_VENDOR_SCRIPT  (explicit override)
 *   2. <exe-dir>/../../scripts/vendor-runtimes.sh  (dev: build/src/ubuilder)
 *   3. <exe-dir>/../scripts/vendor-runtimes.sh     (dev variant)
 *   4. <exe-dir>/scripts/vendor-runtimes.sh        (binary alongside scripts/)
 *   5. <exe-dir>/../share/ubuilder/vendor-runtimes.sh  (system install)
 *
 * Returns 0 and writes the absolute path on success, -1 if not found.
 */
int ub_find_vendor_script(char* out, size_t out_cap);

/*
 * Auto-vendor a missing runtime. Spawns `bash <script> <runtime_key>`.
 * `runtime_key` is "python" / "node" matching the script's manifest.
 * Returns UB_SUCCESS on success (cache should now contain the runtime),
 * UB_ERROR_RUNTIME_NOT_FOUND if the script wasn't found or its dependencies
 * (bash / curl / tar) are missing, UB_ERROR_EXECUTION_FAILED if it ran
 * but exited non-zero.
 */
ub_result_t ub_auto_vendor(const char* runtime_key);

// Function to extract PHP extensions and create custom php.ini
ub_result_t ub_extract_php_extensions(FILE* input_file, const char* temp_dir);

// Cleanup function
void ub_runtime_info_cleanup(ub_runtime_info_t* info);

#ifdef PLATFORM_WINDOWS
// Windows-specific functions
ub_result_t ub_extract_windows_php_runtime(FILE* input_file, const char* temp_dir, char* extracted_path);
ub_result_t ub_extract_windows_nodejs_runtime(FILE* input_file, const char* temp_dir, char* extracted_path);
ub_result_t ub_extract_windows_python_runtime(FILE* input_file, const char* temp_dir, char* extracted_path);
#endif

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_EMBEDDER_H
