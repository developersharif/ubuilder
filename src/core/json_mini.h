#ifndef UBUILDER_JSON_MINI_H
#define UBUILDER_JSON_MINI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value json_value_t;
typedef struct json_pair  json_pair_t;

struct json_value {
    json_type_t type;
    int line;
    int col;
    union {
        int   b;
        long  n;
        struct { char* s; size_t len; } str;
        struct { json_value_t** items; size_t count; } arr;
        struct { json_pair_t*   pairs; size_t count; } obj;
    } v;
};

struct json_pair {
    char*         key;
    size_t        key_len;
    int           key_line;
    int           key_col;
    json_value_t* value;
};

/*
 * Parse a complete JSON document from `input` of byte length `len`.
 *
 *  - On success returns the root value (caller owns; free with json_free).
 *  - On failure returns NULL and writes a one-line message to `err_buf`
 *    of the form "line:col: <reason>" (truncated to err_buf_len-1 bytes).
 *
 *  Subset supported: objects, arrays, strings (with \" \\ \/ \b \f \n \r \t \uXXXX
 *  escapes), integers, booleans (true/false), null. No floats, no comments,
 *  no trailing commas. UTF-8 input is passed through; \uXXXX is encoded to UTF-8.
 */
json_value_t* json_parse(const char* input, size_t len,
                         char* err_buf, size_t err_buf_len);

void json_free(json_value_t* v);

/*
 * Convenience lookup: return the value for `key` in an OBJECT, or NULL if the
 * value is not an object or the key is absent. O(n) over object size.
 */
const json_value_t* json_obj_get(const json_value_t* obj, const char* key);

#ifdef __cplusplus
}
#endif

#endif
