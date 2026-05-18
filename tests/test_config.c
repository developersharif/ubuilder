#include "../src/core/json_mini.h"
#include "../src/core/config.h"
#include "ubuilder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* Mirror the printf-based "✓/✗" style of the other test suites. */
extern int test_count;
extern int test_passed;

#define EXPECT(name, cond) do { \
    test_count++; \
    if (cond) { test_passed++; printf("✓ %s\n", name); } \
    else      { printf("✗ %s\n", name); } \
} while (0)

static char* write_tmp(const char* contents) {
    static int  n = 0;
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/ubuilder-test-config.%d.%d.json", getpid(), n++);
    FILE* f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite(contents, 1, strlen(contents), f);
    fclose(f);
    return path;
}

static void test_json_basic(void) {
    char err[128];
    const char* src = "{\"a\": 1, \"b\": \"two\", \"c\": true, \"d\": [1,2,3]}";
    json_value_t* v = json_parse(src, strlen(src), err, sizeof(err));
    EXPECT("json: parses well-formed object", v && v->type == JSON_OBJECT);
    if (!v) return;

    const json_value_t* a = json_obj_get(v, "a");
    EXPECT("json: integer value", a && a->type == JSON_NUMBER && a->v.n == 1);

    const json_value_t* b = json_obj_get(v, "b");
    EXPECT("json: string value", b && b->type == JSON_STRING &&
                                  b->v.str.len == 3 && memcmp(b->v.str.s, "two", 3) == 0);

    const json_value_t* c = json_obj_get(v, "c");
    EXPECT("json: bool true", c && c->type == JSON_BOOL && c->v.b == 1);

    const json_value_t* d = json_obj_get(v, "d");
    EXPECT("json: array of 3", d && d->type == JSON_ARRAY && d->v.arr.count == 3);

    json_free(v);
}

static void test_json_errors(void) {
    char err[128];
    #define BAD(s) json_parse((s), strlen(s), err, sizeof(err))
    EXPECT("json: rejects trailing comma in object", BAD("{\"a\":1,}") == NULL);
    EXPECT("json: rejects unquoted key",             BAD("{a:1}")     == NULL);
    EXPECT("json: rejects floats (v1 ints only)",    BAD("{\"a\":1.5}") == NULL);
    EXPECT("json: rejects trailing junk",            BAD("{} extra")  == NULL);
    #undef BAD
}

static void test_config_minimum(void) {
    const char* json =
        "{\n"
        "  \"runtime\": \"python\",\n"
        "  \"entry_point\": \"main.py\"\n"
        "}\n";
    char* path = write_tmp(json);
    EXPECT("config: tmp file written", path != NULL);
    if (!path) return;

    ub_config_file_t* file = NULL;
    ub_result_t r = ub_config_load(path, NULL, &file);
    EXPECT("config: loads minimum file", r == UB_SUCCESS && file != NULL);

    ub_config_t       cfg = {0};
    ub_cli_presence_t pre = {0};
    cfg.runtime = UB_RUNTIME_UNKNOWN;

    r = ub_config_apply(file, &pre, &cfg);
    EXPECT("config: apply succeeds", r == UB_SUCCESS);
    EXPECT("config: runtime applied", cfg.runtime == UB_RUNTIME_PYTHON);
    EXPECT("config: entry_point applied",
           cfg.entry_point && strcmp(cfg.entry_point, "main.py") == 0);
    EXPECT("config: project_dir defaults to config dir",
           cfg.project_dir && strstr(cfg.project_dir, "/tmp") != NULL);

    ub_config_free(file);
    free(cfg.project_dir);
    free(cfg.entry_point);
    free(cfg.output_path);
    unlink(path);
}

static void test_config_cli_wins(void) {
    const char* json =
        "{\n"
        "  \"runtime\": \"python\",\n"
        "  \"entry_point\": \"main.py\"\n"
        "}\n";
    char* path = write_tmp(json);
    if (!path) return;

    ub_config_file_t* file = NULL;
    EXPECT("precedence: file loaded",
           ub_config_load(path, NULL, &file) == UB_SUCCESS && file != NULL);

    /* Simulate: user passed --runtime=php on the CLI. */
    ub_config_t       cfg = {0};
    ub_cli_presence_t pre = {0};
    cfg.runtime    = UB_RUNTIME_PHP;
    pre.runtime    = 1;

    EXPECT("precedence: apply ok", ub_config_apply(file, &pre, &cfg) == UB_SUCCESS);
    EXPECT("precedence: CLI runtime preserved (PHP, not Python from config)",
           cfg.runtime == UB_RUNTIME_PHP);
    EXPECT("precedence: entry_point still filled from config (CLI didn't set it)",
           cfg.entry_point && strcmp(cfg.entry_point, "main.py") == 0);

    ub_config_free(file);
    free(cfg.project_dir);
    free(cfg.entry_point);
    free(cfg.output_path);
    unlink(path);
}

static void test_config_type_error(void) {
    /* runtime must be a string */
    const char* json = "{\"runtime\": 7}";
    char* path = write_tmp(json);
    if (!path) return;

    ub_config_file_t* file = NULL;
    ub_result_t r = ub_config_load(path, NULL, &file);
    /* Load succeeds (schema validation is per-key at apply time); apply must reject. */
    EXPECT("type-error: file parses", r == UB_SUCCESS && file != NULL);

    ub_config_t       cfg = {0};
    ub_cli_presence_t pre = {0};
    cfg.runtime = UB_RUNTIME_UNKNOWN;
    r = ub_config_apply(file, &pre, &cfg);
    EXPECT("type-error: apply rejects non-string runtime",
           r == UB_ERROR_INVALID_ARGS);

    ub_config_free(file);
    free(cfg.project_dir);
    free(cfg.entry_point);
    free(cfg.output_path);
    unlink(path);
}

static void test_config_explicit_missing(void) {
    ub_config_file_t* file = NULL;
    ub_result_t r = ub_config_load("/tmp/ubuilder-does-not-exist-XYZ.json", NULL, &file);
    EXPECT("explicit-missing: --config path missing is a hard error",
           r != UB_SUCCESS && file == NULL);
}

static void test_config_no_discovery(void) {
    /* No explicit path, no project dir hint, no ./ubuilder.json in CWD —
     * load succeeds with NULL out (the no-config case). */
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) return;
    if (chdir("/tmp") != 0) return;
    unlink("/tmp/ubuilder.json");  /* paranoia */

    ub_config_file_t* file = NULL;
    ub_result_t r = ub_config_load(NULL, NULL, &file);
    EXPECT("no-discovery: load succeeds with NULL out when no file present",
           r == UB_SUCCESS && file == NULL);

    if (chdir(cwd) != 0) { /* best effort */ }
}

static void test_config_unknown_key_warns(void) {
    /* Unknown keys produce a warning to stderr but don't fail. */
    const char* json = "{\"runtime\":\"python\", \"entry_point\":\"x.py\", \"made_up_key\": 1}";
    char* path = write_tmp(json);
    if (!path) return;

    ub_config_file_t* file = NULL;
    ub_result_t r = ub_config_load(path, NULL, &file);
    EXPECT("forward-compat: unknown key warns but does not fail",
           r == UB_SUCCESS && file != NULL);

    ub_config_free(file);
    unlink(path);
}

void test_config(void) {
    printf("\nJSON parser & config-file tests\n");
    printf("-------------------------------\n");
    test_json_basic();
    test_json_errors();
    test_config_minimum();
    test_config_cli_wins();
    test_config_type_error();
    test_config_explicit_missing();
    test_config_no_discovery();
    test_config_unknown_key_warns();
}
