#include "json_mini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char* src;
    size_t      len;
    size_t      pos;
    int         line;
    int         col;
    char*       err;
    size_t      err_cap;
} P;

static void set_err(P* p, int line, int col, const char* msg) {
    if (p->err && p->err_cap > 0 && !p->err[0]) {
        snprintf(p->err, p->err_cap, "%d:%d: %s", line, col, msg);
    }
}

static void advance(P* p, size_t n) {
    for (size_t i = 0; i < n && p->pos < p->len; i++) {
        if (p->src[p->pos] == '\n') { p->line++; p->col = 1; }
        else { p->col++; }
        p->pos++;
    }
}

static void skip_ws(P* p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(p, 1);
        } else {
            break;
        }
    }
}

static int peek(const P* p) {
    return (p->pos < p->len) ? (unsigned char)p->src[p->pos] : -1;
}

static json_value_t* val_new(json_type_t t, int line, int col) {
    json_value_t* v = (json_value_t*)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = t;
    v->line = line;
    v->col  = col;
    return v;
}

/* forward */
static json_value_t* parse_value(P* p);

static void encode_utf8(unsigned cp, char* out, size_t* out_n) {
    if (cp < 0x80) {
        out[(*out_n)++] = (char)cp;
    } else if (cp < 0x800) {
        out[(*out_n)++] = (char)(0xC0 | (cp >> 6));
        out[(*out_n)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out[(*out_n)++] = (char)(0xE0 | (cp >> 12));
        out[(*out_n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*out_n)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[(*out_n)++] = (char)(0xF0 | (cp >> 18));
        out[(*out_n)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*out_n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*out_n)++] = (char)(0x80 | (cp & 0x3F));
    }
}

static int hex_nibble(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a JSON string. Returns malloc'd buffer + length via *out_len. */
static char* parse_string(P* p, size_t* out_len) {
    int start_line = p->line, start_col = p->col;
    if (peek(p) != '"') { set_err(p, p->line, p->col, "expected string"); return NULL; }
    advance(p, 1);

    /* worst case: every input byte becomes one output byte */
    size_t cap = 32, n = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) { set_err(p, start_line, start_col, "out of memory"); return NULL; }

    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->src[p->pos];
        if (c == '"') {
            advance(p, 1);
            if (n + 1 > cap) { cap = n + 1; buf = (char*)realloc(buf, cap); }
            buf[n] = 0;        /* NUL-terminate so callers can use strlen safely */
            *out_len = n;
            return buf;
        }
        if (c == '\\') {
            if (p->pos + 1 >= p->len) { set_err(p, p->line, p->col, "unterminated escape"); free(buf); return NULL; }
            char esc = p->src[p->pos + 1];
            advance(p, 2);
            char repl = 0;
            switch (esc) {
                case '"':  repl = '"';  break;
                case '\\': repl = '\\'; break;
                case '/':  repl = '/';  break;
                case 'b':  repl = '\b'; break;
                case 'f':  repl = '\f'; break;
                case 'n':  repl = '\n'; break;
                case 'r':  repl = '\r'; break;
                case 't':  repl = '\t'; break;
                case 'u': {
                    if (p->pos + 4 > p->len) { set_err(p, p->line, p->col, "bad \\u escape"); free(buf); return NULL; }
                    int h0 = hex_nibble((unsigned char)p->src[p->pos + 0]);
                    int h1 = hex_nibble((unsigned char)p->src[p->pos + 1]);
                    int h2 = hex_nibble((unsigned char)p->src[p->pos + 2]);
                    int h3 = hex_nibble((unsigned char)p->src[p->pos + 3]);
                    if ((h0|h1|h2|h3) < 0) { set_err(p, p->line, p->col, "bad \\u hex"); free(buf); return NULL; }
                    advance(p, 4);
                    unsigned cp = (unsigned)((h0<<12)|(h1<<8)|(h2<<4)|h3);
                    if (n + 4 > cap) { cap = (n + 4) * 2; buf = (char*)realloc(buf, cap); }
                    encode_utf8(cp, buf, &n);
                    continue;
                }
                default: set_err(p, p->line, p->col, "unknown escape"); free(buf); return NULL;
            }
            if (n + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
            buf[n++] = repl;
            continue;
        }
        if (c < 0x20) { set_err(p, p->line, p->col, "control char in string"); free(buf); return NULL; }
        if (n + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        buf[n++] = (char)c;
        advance(p, 1);
    }
    set_err(p, start_line, start_col, "unterminated string");
    free(buf);
    return NULL;
}

static json_value_t* parse_number(P* p) {
    int line = p->line, col = p->col;
    size_t start = p->pos;
    if (peek(p) == '-') advance(p, 1);
    while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) advance(p, 1);
    /* reject fractions/exponents — v1 only needs integers */
    if (p->pos < p->len && (p->src[p->pos] == '.' || p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        set_err(p, line, col, "non-integer numbers not supported");
        return NULL;
    }
    if (p->pos == start) { set_err(p, line, col, "expected number"); return NULL; }
    char buf[64];
    size_t blen = p->pos - start;
    if (blen >= sizeof(buf)) { set_err(p, line, col, "number too long"); return NULL; }
    memcpy(buf, p->src + start, blen);
    buf[blen] = 0;
    char* end = NULL;
    long val = strtol(buf, &end, 10);
    if (!end || *end != 0) { set_err(p, line, col, "bad number"); return NULL; }
    json_value_t* v = val_new(JSON_NUMBER, line, col);
    if (!v) { set_err(p, line, col, "out of memory"); return NULL; }
    v->v.n = val;
    return v;
}

static json_value_t* parse_literal(P* p, const char* word, json_type_t t, int boolv) {
    int line = p->line, col = p->col;
    size_t wlen = strlen(word);
    if (p->pos + wlen > p->len || memcmp(p->src + p->pos, word, wlen) != 0) {
        set_err(p, line, col, "unexpected token");
        return NULL;
    }
    advance(p, wlen);
    json_value_t* v = val_new(t, line, col);
    if (!v) { set_err(p, line, col, "out of memory"); return NULL; }
    if (t == JSON_BOOL) v->v.b = boolv;
    return v;
}

static json_value_t* parse_array(P* p) {
    int line = p->line, col = p->col;
    advance(p, 1); /* [ */
    skip_ws(p);
    json_value_t* v = val_new(JSON_ARRAY, line, col);
    if (!v) { set_err(p, line, col, "out of memory"); return NULL; }
    if (peek(p) == ']') { advance(p, 1); return v; }
    size_t cap = 4;
    v->v.arr.items = (json_value_t**)calloc(cap, sizeof(json_value_t*));
    if (!v->v.arr.items) { set_err(p, line, col, "out of memory"); json_free(v); return NULL; }
    while (1) {
        skip_ws(p);
        json_value_t* item = parse_value(p);
        if (!item) { json_free(v); return NULL; }
        if (v->v.arr.count == cap) {
            cap *= 2;
            v->v.arr.items = (json_value_t**)realloc(v->v.arr.items, cap * sizeof(json_value_t*));
        }
        v->v.arr.items[v->v.arr.count++] = item;
        skip_ws(p);
        int c = peek(p);
        if (c == ',') { advance(p, 1); continue; }
        if (c == ']') { advance(p, 1); return v; }
        set_err(p, p->line, p->col, "expected ',' or ']'");
        json_free(v);
        return NULL;
    }
}

static json_value_t* parse_object(P* p) {
    int line = p->line, col = p->col;
    advance(p, 1); /* { */
    skip_ws(p);
    json_value_t* v = val_new(JSON_OBJECT, line, col);
    if (!v) { set_err(p, line, col, "out of memory"); return NULL; }
    if (peek(p) == '}') { advance(p, 1); return v; }
    size_t cap = 4;
    v->v.obj.pairs = (json_pair_t*)calloc(cap, sizeof(json_pair_t));
    if (!v->v.obj.pairs) { set_err(p, line, col, "out of memory"); json_free(v); return NULL; }
    while (1) {
        skip_ws(p);
        if (peek(p) != '"') { set_err(p, p->line, p->col, "expected object key"); json_free(v); return NULL; }
        int kl = p->line, kc = p->col;
        size_t klen = 0;
        char*  key  = parse_string(p, &klen);
        if (!key) { json_free(v); return NULL; }
        skip_ws(p);
        if (peek(p) != ':') { set_err(p, p->line, p->col, "expected ':'"); free(key); json_free(v); return NULL; }
        advance(p, 1);
        skip_ws(p);
        json_value_t* val = parse_value(p);
        if (!val) { free(key); json_free(v); return NULL; }
        if (v->v.obj.count == cap) {
            cap *= 2;
            v->v.obj.pairs = (json_pair_t*)realloc(v->v.obj.pairs, cap * sizeof(json_pair_t));
        }
        json_pair_t* pr = &v->v.obj.pairs[v->v.obj.count++];
        pr->key      = key;
        pr->key_len  = klen;
        pr->key_line = kl;
        pr->key_col  = kc;
        pr->value    = val;
        skip_ws(p);
        int c = peek(p);
        if (c == ',') { advance(p, 1); continue; }
        if (c == '}') { advance(p, 1); return v; }
        set_err(p, p->line, p->col, "expected ',' or '}'");
        json_free(v);
        return NULL;
    }
}

static json_value_t* parse_value(P* p) {
    skip_ws(p);
    int c = peek(p);
    int line = p->line, col = p->col;
    switch (c) {
        case '{': return parse_object(p);
        case '[': return parse_array(p);
        case '"': {
            size_t slen = 0;
            char*  s    = parse_string(p, &slen);
            if (!s) return NULL;
            json_value_t* v = val_new(JSON_STRING, line, col);
            if (!v) { free(s); set_err(p, line, col, "out of memory"); return NULL; }
            v->v.str.s   = s;
            v->v.str.len = slen;
            return v;
        }
        case 't': return parse_literal(p, "true",  JSON_BOOL, 1);
        case 'f': return parse_literal(p, "false", JSON_BOOL, 0);
        case 'n': return parse_literal(p, "null",  JSON_NULL, 0);
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return parse_number(p);
            set_err(p, line, col, "unexpected character");
            return NULL;
    }
}

json_value_t* json_parse(const char* input, size_t len, char* err_buf, size_t err_buf_len) {
    if (err_buf && err_buf_len > 0) err_buf[0] = 0;
    P p = { input, len, 0, 1, 1, err_buf, err_buf_len };
    skip_ws(&p);
    json_value_t* v = parse_value(&p);
    if (!v) return NULL;
    skip_ws(&p);
    if (p.pos != p.len) {
        set_err(&p, p.line, p.col, "trailing data after JSON value");
        json_free(v);
        return NULL;
    }
    return v;
}

void json_free(json_value_t* v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            free(v->v.str.s);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->v.arr.count; i++) json_free(v->v.arr.items[i]);
            free(v->v.arr.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->v.obj.count; i++) {
                free(v->v.obj.pairs[i].key);
                json_free(v->v.obj.pairs[i].value);
            }
            free(v->v.obj.pairs);
            break;
        default: break;
    }
    free(v);
}

const json_value_t* json_obj_get(const json_value_t* obj, const char* key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    size_t klen = strlen(key);
    for (size_t i = 0; i < obj->v.obj.count; i++) {
        const json_pair_t* pr = &obj->v.obj.pairs[i];
        if (pr->key_len == klen && memcmp(pr->key, key, klen) == 0) {
            return pr->value;
        }
    }
    return NULL;
}
