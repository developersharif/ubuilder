#include "../src/core/platform_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

extern int test_count;
extern int test_passed;

#define EXPECT(name, cond) do { \
    test_count++; \
    if (cond) { test_passed++; printf("✓ %s\n", name); } \
    else      { printf("✗ %s\n", name); } \
} while (0)

static int touch(const char* path, const char* contents) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    if (contents && *contents) fwrite(contents, 1, strlen(contents), f);
    fclose(f);
    return 0;
}

static int path_exists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void test_remove_tree(void) {
    char base[256];
    snprintf(base, sizeof(base), "/tmp/ubuilder-pc-test.%d", (int)getpid());
    /* Ensure clean start */
    pc_remove_tree(base);

    /* Build a nested tree:
     *   base/
     *     a.txt
     *     sub/
     *       b.txt
     *       deep/
     *         c.txt
     */
    char p[512];
    EXPECT("remove_tree: mkdir base",            mkdir(base, 0755) == 0);
    snprintf(p, sizeof(p), "%s/a.txt", base);
    EXPECT("remove_tree: write a.txt",           touch(p, "hello") == 0);
    snprintf(p, sizeof(p), "%s/sub", base);
    EXPECT("remove_tree: mkdir sub",             mkdir(p, 0755) == 0);
    snprintf(p, sizeof(p), "%s/sub/b.txt", base);
    EXPECT("remove_tree: write sub/b.txt",       touch(p, "x") == 0);
    snprintf(p, sizeof(p), "%s/sub/deep", base);
    EXPECT("remove_tree: mkdir sub/deep",        mkdir(p, 0755) == 0);
    snprintf(p, sizeof(p), "%s/sub/deep/c.txt", base);
    EXPECT("remove_tree: write sub/deep/c.txt",  touch(p, "") == 0);

    EXPECT("remove_tree: remove succeeds",       pc_remove_tree(base) == 0);
    EXPECT("remove_tree: base is gone",          !path_exists(base));

    /* Removing a missing path is an error (caller can ignore). */
    EXPECT("remove_tree: missing path fails",    pc_remove_tree(base) != 0);

    /* Single file path also works. */
    snprintf(p, sizeof(p), "/tmp/ubuilder-pc-single.%d", (int)getpid());
    touch(p, "x");
    EXPECT("remove_tree: removes single file",   pc_remove_tree(p) == 0 && !path_exists(p));
}

static void test_spawn_basic(void) {
    /* /bin/true: exit 0. /bin/false: exit 1. */
    char* argv_true[]  = { (char*)"true",  NULL };
    char* argv_false[] = { (char*)"false", NULL };

    int t = pc_spawn_and_wait("/bin/true",  argv_true,  NULL, NULL);
    int f = pc_spawn_and_wait("/bin/false", argv_false, NULL, NULL);

    EXPECT("spawn: /bin/true exits 0",  t == 0);
    EXPECT("spawn: /bin/false exits 1", f == 1);
}

static void test_spawn_missing(void) {
    char* argv_missing[] = { (char*)"nope", NULL };
    int r = pc_spawn_and_wait("/no/such/exe/here", argv_missing, NULL, NULL);
    EXPECT("spawn: missing exe fails (< 0)", r < 0);
}

static void test_env_overlay(void) {
    char* extra[] = { (char*)"UB_TEST_KEY=overlay_value", NULL };
    char** env = pc_env_overlay(extra);
    EXPECT("env_overlay: returns non-null", env != NULL);
    if (!env) return;

    int found = 0;
    for (int i = 0; env[i]; i++) {
        if (strncmp(env[i], "UB_TEST_KEY=", 12) == 0 &&
            strcmp(env[i] + 12, "overlay_value") == 0) { found = 1; break; }
    }
    EXPECT("env_overlay: contains the injected key", found);
    pc_env_free(env);
}

static void test_mkdir_p(void) {
    char base[256];
    snprintf(base, sizeof(base), "/tmp/ubuilder-mkdir-p.%d/a/b/c", (int)getpid());
    pc_remove_tree(base); /* paranoia */

    int rc = pc_mkdir_p(base);
    EXPECT("mkdir_p: creates nested tree", rc == 0 && path_exists(base));

    /* Idempotent. */
    rc = pc_mkdir_p(base);
    EXPECT("mkdir_p: succeeds when path already exists", rc == 0);

    /* Cleanup. */
    char root[256];
    snprintf(root, sizeof(root), "/tmp/ubuilder-mkdir-p.%d", (int)getpid());
    pc_remove_tree(root);
}

static void test_path_lookup(void) {
    /* /bin/sh is essentially universal on Linux/macOS; the test runs there. */
    char* p = pc_path_lookup("sh");
    EXPECT("path_lookup: finds 'sh' on PATH", p != NULL);
    free(p);

    /* Absolute path that exists. */
    p = pc_path_lookup("/bin/true");
    EXPECT("path_lookup: accepts absolute path that exists", p != NULL);
    free(p);

    /* Absolute path that doesn't exist. */
    p = pc_path_lookup("/no/such/binary/here");
    EXPECT("path_lookup: rejects absolute path that doesn't exist", p == NULL);

    /* Name that isn't on PATH. */
    p = pc_path_lookup("ubuilder-no-such-binary-xyz-12345");
    EXPECT("path_lookup: returns NULL for unknown name", p == NULL);
}

static void test_spawn_capture(void) {
    /* echo on POSIX is generally /bin/echo or a shell builtin; use printf
     * through a known path for determinism. /bin/echo prints "hi\n" → we
     * strip the newline and expect "hi". */
    char* out = NULL;
    char* argv[] = { (char*)"echo", (char*)"hi", NULL };
    int rc = pc_spawn_capture("/bin/echo", argv, NULL, NULL, 64, &out);
    EXPECT("spawn_capture: /bin/echo exit 0",      rc == 0);
    EXPECT("spawn_capture: captures stdout 'hi'",  out && strcmp(out, "hi") == 0);
    free(out);

    /* Truncation: ask for max_bytes=2 from echo abcdef → we expect "ab"
     * (and pipe closes; child may report broken-pipe but we don't care
     * about exit code semantics here, just that out is bounded). */
    char* argv2[] = { (char*)"echo", (char*)"abcdef", NULL };
    rc = pc_spawn_capture("/bin/echo", argv2, NULL, NULL, 2, &out);
    EXPECT("spawn_capture: truncates output to max_bytes",
           out && strlen(out) <= 2);
    free(out);
}

void test_platform_compat(void) {
    printf("\nPlatform compatibility shim tests\n");
    printf("---------------------------------\n");
    test_remove_tree();
    test_spawn_basic();
    test_spawn_missing();
    test_env_overlay();
    test_mkdir_p();
    test_path_lookup();
    test_spawn_capture();
}
