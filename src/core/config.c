#include "config.h"
#include "json_mini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct ub_config_file {
    char*         path;          /* absolute or resolved path */
    char*         dir;           /* directory containing the config (owned) */
    json_value_t* root;          /* parsed JSON tree (owned) */
};

const char* ub_config_path(const ub_config_file_t* file) {
    return file ? file->path : NULL;
}

void ub_config_free(ub_config_file_t* file) {
    if (!file) return;
    free(file->path);
    free(file->dir);
    json_free(file->root);
    free(file);
}

static int file_exists(const char* path) {
    struct stat st;
    return path && stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

static char* dup_str(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char*  d = (char*)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static char* join_path(const char* a, const char* b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    int need_sep = (la > 0 && a[la - 1] != '/' && a[la - 1] != '\\');
    char* out = (char*)malloc(la + lb + 2);
    if (!out) return NULL;
    memcpy(out, a, la);
    size_t p = la;
    if (need_sep) out[p++] = '/';
    memcpy(out + p, b, lb);
    out[p + lb] = 0;
    return out;
}

static char* dirname_of(const char* path) {
    if (!path) return dup_str(".");
    const char* slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') slash = p;
    }
    if (!slash) return dup_str(".");
    size_t n = (size_t)(slash - path);
    if (n == 0) return dup_str("/");
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, path, n);
    out[n] = 0;
    return out;
}

static char* slurp_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = 0;
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

/* ---------- known-key table (helps detect typos) ---------- */
static const char* KNOWN_ROOT_KEYS[] = {
    "schema_version", "name", "runtime", "entry_point", "output",
    "include", "exclude", "verbose", "gui", "compression",
    "runtime_options", "build",
    NULL
};

static int is_known_root_key(const char* k, size_t klen) {
    for (size_t i = 0; KNOWN_ROOT_KEYS[i]; i++) {
        if (strlen(KNOWN_ROOT_KEYS[i]) == klen &&
            memcmp(KNOWN_ROOT_KEYS[i], k, klen) == 0) return 1;
    }
    return 0;
}

/* ---------- typed getters with validation ---------- */
static int expect_string(const char* path, const char* key,
                         const json_value_t* v, char** out) {
    if (!v) return 0;
    if (v->type != JSON_STRING) {
        fprintf(stderr, "%s:%d:%d: \"%s\" must be a string\n", path, v->line, v->col, key);
        return -1;
    }
    *out = dup_str(v->v.str.s);
    return 1;
}

static int expect_bool(const char* path, const char* key,
                       const json_value_t* v, int* out) {
    if (!v) return 0;
    if (v->type != JSON_BOOL) {
        fprintf(stderr, "%s:%d:%d: \"%s\" must be a boolean (true/false)\n",
                path, v->line, v->col, key);
        return -1;
    }
    *out = v->v.b;
    return 1;
}

/* ---------- public API ---------- */

ub_result_t ub_config_load(const char* explicit_path,
                           const char* project_dir_hint,
                           ub_config_file_t** out_file) {
    *out_file = NULL;
    char*       resolved = NULL;
    int         must_exist = 0;

    if (explicit_path && *explicit_path) {
        resolved   = dup_str(explicit_path);
        must_exist = 1;
    } else if (project_dir_hint && *project_dir_hint) {
        resolved = join_path(project_dir_hint, "ubuilder.json");
    }
    if (!resolved || !file_exists(resolved)) {
        if (must_exist) {
            fprintf(stderr, "Error: --config path not found: %s\n", explicit_path);
            free(resolved);
            return UB_ERROR_FILE_NOT_FOUND;
        }
        free(resolved);
        /* Try CWD */
        if (file_exists("ubuilder.json")) {
            resolved = dup_str("ubuilder.json");
        } else {
            return UB_SUCCESS; /* no config — fine */
        }
    }

    size_t len = 0;
    char*  buf = slurp_file(resolved, &len);
    if (!buf) {
        fprintf(stderr, "Error: cannot read %s\n", resolved);
        free(resolved);
        return UB_ERROR_FILE_NOT_FOUND;
    }

    char err[256] = {0};
    json_value_t* root = json_parse(buf, len, err, sizeof(err));
    free(buf);
    if (!root) {
        fprintf(stderr, "Error: %s:%s\n", resolved, err[0] ? err : "parse failed");
        free(resolved);
        return UB_ERROR_INVALID_ARGS;
    }
    if (root->type != JSON_OBJECT) {
        fprintf(stderr, "Error: %s: root must be a JSON object\n", resolved);
        json_free(root);
        free(resolved);
        return UB_ERROR_INVALID_ARGS;
    }

    /* Warn on unknown root keys (forward-compat). */
    for (size_t i = 0; i < root->v.obj.count; i++) {
        const json_pair_t* pr = &root->v.obj.pairs[i];
        if (!is_known_root_key(pr->key, pr->key_len)) {
            fprintf(stderr, "warning: %s:%d:%d: unknown config key \"%.*s\" (ignored)\n",
                    resolved, pr->key_line, pr->key_col,
                    (int)pr->key_len, pr->key);
        }
    }

    /* Validate schema_version if present. */
    const json_value_t* sv = json_obj_get(root, "schema_version");
    if (sv) {
        if (sv->type != JSON_NUMBER) {
            fprintf(stderr, "Error: %s:%d:%d: \"schema_version\" must be an integer\n",
                    resolved, sv->line, sv->col);
            json_free(root);
            free(resolved);
            return UB_ERROR_INVALID_ARGS;
        }
        if (sv->v.n < 1) {
            fprintf(stderr, "Error: %s: \"schema_version\" must be >= 1 (got %ld)\n",
                    resolved, sv->v.n);
            json_free(root);
            free(resolved);
            return UB_ERROR_INVALID_ARGS;
        }
        if (sv->v.n > 1) {
            fprintf(stderr, "warning: %s: schema_version %ld is newer than supported (1); "
                            "continuing, but new keys may be ignored\n",
                    resolved, sv->v.n);
        }
    }

    ub_config_file_t* f = (ub_config_file_t*)calloc(1, sizeof(*f));
    if (!f) {
        json_free(root);
        free(resolved);
        return UB_ERROR_MEMORY_ALLOCATION;
    }
    f->path = resolved;
    f->dir  = dirname_of(resolved);
    f->root = root;
    *out_file = f;
    return UB_SUCCESS;
}

ub_result_t ub_config_apply(const ub_config_file_t*  file,
                            const ub_cli_presence_t* presence,
                            ub_config_t*             cfg) {
    if (!file || !file->root) return UB_SUCCESS;

    /* --project-dir: if CLI didn't set it, use the directory containing the config. */
    if (!presence->project_dir && !cfg->project_dir) {
        cfg->project_dir = dup_str(file->dir);
    }

    /* runtime */
    if (!presence->runtime) {
        const json_value_t* v = json_obj_get(file->root, "runtime");
        if (v) {
            char* s = NULL;
            int rc = expect_string(file->path, "runtime", v, &s);
            if (rc < 0) return UB_ERROR_INVALID_ARGS;
            if (rc > 0) {
                cfg->runtime = ub_parse_runtime(s);
                if (cfg->runtime == UB_RUNTIME_UNKNOWN) {
                    fprintf(stderr, "Error: %s: unknown runtime \"%s\"\n", file->path, s);
                    free(s);
                    return UB_ERROR_INVALID_ARGS;
                }
                free(s);
            }
        }
    }

    /* entry_point */
    if (!presence->entry_point && !cfg->entry_point) {
        const json_value_t* v = json_obj_get(file->root, "entry_point");
        if (v) {
            char* s = NULL;
            int rc = expect_string(file->path, "entry_point", v, &s);
            if (rc < 0) return UB_ERROR_INVALID_ARGS;
            if (rc > 0) cfg->entry_point = s;
        }
    }

    /* output */
    if (!presence->output && !cfg->output_path) {
        const json_value_t* v = json_obj_get(file->root, "output");
        if (v) {
            char* s = NULL;
            int rc = expect_string(file->path, "output", v, &s);
            if (rc < 0) return UB_ERROR_INVALID_ARGS;
            if (rc > 0) cfg->output_path = s;
        }
    }

    /* output default: derive from "name" if output is still unset */
    if (!presence->output && !cfg->output_path) {
        const json_value_t* v = json_obj_get(file->root, "name");
        if (v) {
            char* s = NULL;
            int rc = expect_string(file->path, "name", v, &s);
            if (rc < 0) return UB_ERROR_INVALID_ARGS;
            if (rc > 0) cfg->output_path = s; /* name == basename of output */
        }
    }

    /* bools */
    if (!presence->verbose) {
        const json_value_t* v = json_obj_get(file->root, "verbose");
        if (v) {
            int b = 0;
            int rc = expect_bool(file->path, "verbose", v, &b);
            if (rc < 0) return UB_ERROR_INVALID_ARGS;
            if (rc > 0) cfg->verbose = b;
        }
    }
    if (!presence->gui) {
        const json_value_t* v = json_obj_get(file->root, "gui");
        if (v) {
            int b = 0;
            int rc = expect_bool(file->path, "gui", v, &b);
            if (rc < 0) return UB_ERROR_INVALID_ARGS;
            if (rc > 0) cfg->enable_gui = b;
        }
    }
    if (!presence->compression) {
        const json_value_t* v = json_obj_get(file->root, "compression");
        if (v) {
            int b = 0;
            int rc = expect_bool(file->path, "compression", v, &b);
            if (rc < 0) return UB_ERROR_INVALID_ARGS;
            if (rc > 0) cfg->enable_compression = b;
        }
    }

    /* M1: runtime_options.<rt>.source — only honor the key for the *selected*
     * runtime to avoid configs that quietly carry stale paths for other rts. */
    if (!presence->runtime_source && !cfg->runtime_source) {
        const json_value_t* rto = json_obj_get(file->root, "runtime_options");
        if (rto && rto->type == JSON_OBJECT) {
            const char* rt_key =
                cfg->runtime == UB_RUNTIME_PYTHON ? "python" :
                cfg->runtime == UB_RUNTIME_PHP    ? "php"    :
                cfg->runtime == UB_RUNTIME_NODEJS ? "node"   : NULL;
            if (rt_key) {
                const json_value_t* per_rt = json_obj_get(rto, rt_key);
                if (per_rt && per_rt->type == JSON_OBJECT) {
                    const json_value_t* src = json_obj_get(per_rt, "source");
                    if (src) {
                        char* s = NULL;
                        int rc = expect_string(file->path, "runtime_options.<rt>.source", src, &s);
                        if (rc < 0) return UB_ERROR_INVALID_ARGS;
                        if (rc > 0) cfg->runtime_source = s;
                    }
                }
            }
        }
    }

    /* Honestly flag keys we parse but don't yet honor in the build pipeline. */
    static const char* ignored[] = { "include", "exclude", "build", NULL };
    for (size_t i = 0; ignored[i]; i++) {
        if (json_obj_get(file->root, ignored[i]) && cfg->verbose) {
            fprintf(stderr, "note: %s: \"%s\" is parsed but not yet honored by the build\n",
                    file->path, ignored[i]);
        }
    }

    return UB_SUCCESS;
}
