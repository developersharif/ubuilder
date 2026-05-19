/* White-box unit tests for src/runtimes/php_builder.c (M1-D internals).
 *
 * We pull php_builder.c into this translation unit so the file's static
 * helpers are visible to the tests. Two things to know:
 *
 *   1. The only external symbol in php_builder.c is `php_builder` (the
 *      const vtable). ubuilder_core (linked into test_ubuilder) already
 *      defines it, so we rename it here to avoid the duplicate-symbol
 *      link error. The renamed copy is unused; we only want the statics.
 *
 *   2. The whole synthetic-runtime stack is wrapped in
 *      `#ifndef PLATFORM_WINDOWS`, so this file is a no-op on Windows.
 */
#define php_builder php_builder_TEST_LOCAL_UNUSED
#include "../src/runtimes/php_builder.c"
#undef php_builder

#include "../src/core/platform_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern int test_count;
extern int test_passed;

#define EXPECT(name, cond) do { \
    test_count++; \
    if (cond) { test_passed++; printf("✓ %s\n", name); } \
    else      { printf("✗ %s\n", name); } \
} while (0)

#ifndef PLATFORM_WINDOWS

static void mkfile_with(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    if (content) fputs(content, f);
    fclose(f);
}

static int file_contains(const char* path, const char* needle) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, needle) != NULL;
}

/* ---- php_parse_composer_extensions ---------------------------------- */

static void test_parse_composer_extensions(void) {
    char dir[256];
    snprintf(dir, sizeof(dir), "/tmp/ubuilder-php-parse-test.%d", (int)getpid());
    pc_remove_tree(dir);
    EXPECT("parse: mkdir scratch", mkdir(dir, 0755) == 0);

    char cj[512];
    snprintf(cj, sizeof(cj), "%s/composer.json", dir);

    /* No composer.json: not an error, empty list. */
    {
        php_ext_list_t l = {0};
        int rc = php_parse_composer_extensions(dir, &l);
        EXPECT("parse: no composer.json -> rc==0", rc == 0);
        EXPECT("parse: no composer.json -> count==0", l.count == 0);
        php_ext_list_free(&l);
    }

    /* composer.json without `require`: empty list. */
    mkfile_with(cj, "{\"name\":\"x\"}");
    {
        php_ext_list_t l = {0};
        int rc = php_parse_composer_extensions(dir, &l);
        EXPECT("parse: no require key -> rc==0", rc == 0);
        EXPECT("parse: no require key -> count==0", l.count == 0);
        php_ext_list_free(&l);
    }

    /* `require` present, no ext-* keys: empty list. */
    mkfile_with(cj, "{\"require\":{\"php\":\">=7.4\",\"psr/log\":\"^1.1\"}}");
    {
        php_ext_list_t l = {0};
        int rc = php_parse_composer_extensions(dir, &l);
        EXPECT("parse: require w/o ext-* -> rc==0", rc == 0);
        EXPECT("parse: require w/o ext-* -> count==0", l.count == 0);
        php_ext_list_free(&l);
    }

    /* Mix of ext-* and normal deps: only ext names, with `ext-` stripped. */
    mkfile_with(cj,
        "{\"require\":{"
            "\"php\":\">=7.4\","
            "\"ext-mbstring\":\"*\","
            "\"psr/log\":\"^1.1\","
            "\"ext-curl\":\"*\""
        "}}");
    {
        php_ext_list_t l = {0};
        int rc = php_parse_composer_extensions(dir, &l);
        EXPECT("parse: mix -> rc==0",   rc == 0);
        EXPECT("parse: mix -> count==2", l.count == 2);
        int saw_mb = 0, saw_curl = 0;
        for (size_t i = 0; i < l.count; i++) {
            if (strcmp(l.names[i], "mbstring") == 0) saw_mb = 1;
            if (strcmp(l.names[i], "curl") == 0)     saw_curl = 1;
        }
        EXPECT("parse: ext-mbstring -> 'mbstring'", saw_mb);
        EXPECT("parse: ext-curl     -> 'curl'",     saw_curl);
        php_ext_list_free(&l);
    }

    /* Malformed JSON: rc == -1. (Stderr will print "Error: composer.json
     * parse failed: ..." — that is the SUT, not a test failure.) */
    mkfile_with(cj, "{\"require\": this is not json}");
    {
        php_ext_list_t l = {0};
        int rc = php_parse_composer_extensions(dir, &l);
        EXPECT("parse: malformed JSON -> rc==-1", rc == -1);
        php_ext_list_free(&l);
    }

    pc_remove_tree(dir);
}

/* ---- php_write_default_ini ------------------------------------------ */

static void test_write_default_ini(void) {
    char dir[256];
    snprintf(dir, sizeof(dir), "/tmp/ubuilder-php-ini-test.%d", (int)getpid());
    pc_remove_tree(dir);
    EXPECT("ini: mkdir scratch", mkdir(dir, 0755) == 0);

    char ini[512];
    snprintf(ini, sizeof(ini), "%s/php.ini", dir);

    /* Empty extension list — header lines present, no extension= lines. */
    {
        php_ext_list_t empty = {0};
        EXPECT("ini: write succeeds (empty)", php_write_default_ini(ini, &empty) == 0);
        EXPECT("ini: opcache.enable line",    file_contains(ini, "opcache.enable = 1"));
        EXPECT("ini: opcache.enable_cli line",file_contains(ini, "opcache.enable_cli = 1"));
        EXPECT("ini: phar.readonly = 0 line", file_contains(ini, "phar.readonly = 0"));
        EXPECT("ini: memory_limit line",      file_contains(ini, "memory_limit"));
        EXPECT("ini: no extension= line",     !file_contains(ini, "extension="));
    }

    /* Populated extension list — one extension= line per name. */
    {
        php_ext_list_t exts = {0};
        exts.names = (char**)calloc(2, sizeof(char*));
        exts.names[0] = strdup("mbstring");
        exts.names[1] = strdup("curl");
        exts.count = 2;

        EXPECT("ini: write succeeds (with exts)", php_write_default_ini(ini, &exts) == 0);
        EXPECT("ini: extension=mbstring line",     file_contains(ini, "extension=mbstring"));
        EXPECT("ini: extension=curl line",         file_contains(ini, "extension=curl"));
        php_ext_list_free(&exts);
    }

    pc_remove_tree(dir);
}

/* ---- php_ext_list_free ---------------------------------------------- */

static void test_ext_list_free(void) {
    /* Free of an empty list must be safe and idempotent. */
    php_ext_list_t empty = {0};
    php_ext_list_free(&empty);
    php_ext_list_free(&empty);
    EXPECT("free: empty list resets to {NULL, 0}",
           empty.names == NULL && empty.count == 0);

    /* Free of a populated list must zero the struct. */
    php_ext_list_t populated = {0};
    populated.names = (char**)calloc(2, sizeof(char*));
    populated.names[0] = strdup("a");
    populated.names[1] = strdup("b");
    populated.count = 2;
    php_ext_list_free(&populated);
    EXPECT("free: populated list -> names NULL",  populated.names == NULL);
    EXPECT("free: populated list -> count 0",     populated.count == 0);

    /* Null pointer must be a no-op. */
    php_ext_list_free(NULL);
    EXPECT("free: NULL pointer is a no-op", 1);
}

#endif /* !PLATFORM_WINDOWS */

void test_php_builder(void) {
#ifndef PLATFORM_WINDOWS
    test_parse_composer_extensions();
    test_write_default_ini();
    test_ext_list_free();
#else
    printf("[skipped] test_php_builder: Windows uses the legacy popen path\n");
#endif
}
