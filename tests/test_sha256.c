#include "../src/core/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <io.h>
#  include <process.h>
#  include <direct.h>
#  define getpid    _getpid
#  define unlink    _unlink
#  define access    _access
#  define F_OK      0
#  define mkdir(p, m) _mkdir(p)
#  ifndef S_ISDIR
#    define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#  endif
#  ifndef S_ISREG
#    define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#  endif
#else
#  include <unistd.h>
#endif

extern int test_count;
extern int test_passed;

#define EXPECT(name, cond) do { \
    test_count++; \
    if (cond) { test_passed++; printf("✓ %s\n", name); } \
    else      { printf("✗ %s\n", name); } \
} while (0)

static int hex_eq(const uint8_t digest[32], const char* expected_hex) {
    char hex[65];
    ub_sha256_hex(digest, hex);
    return strcmp(hex, expected_hex) == 0;
}

static void test_known_vectors(void) {
    uint8_t d[32];

    /* FIPS 180-2 test vectors. */
    ub_sha256("", 0, d);
    EXPECT("sha256: empty string",
           hex_eq(d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    ub_sha256("abc", 3, d);
    EXPECT("sha256: \"abc\"",
           hex_eq(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    const char* s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    ub_sha256(s, strlen(s), d);
    EXPECT("sha256: 448-bit message",
           hex_eq(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    /* Million 'a' — exercises incremental update across many blocks.
     * 1,000,000 = 976 full 1024-byte blocks + 576 trailing 'a's. */
    ub_sha256_ctx ctx;
    ub_sha256_init(&ctx);
    uint8_t a_block[1024];
    memset(a_block, 'a', sizeof(a_block));
    for (int i = 0; i < 976; i++) ub_sha256_update(&ctx, a_block, sizeof(a_block));
    ub_sha256_update(&ctx, a_block, 576);          /* 976*1024 + 576 = 1,000,000 */
    ub_sha256_final(&ctx, d);
    EXPECT("sha256: one million 'a' (streaming)",
           hex_eq(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

static void test_incremental_matches_oneshot(void) {
    const char* msg = "the quick brown fox jumps over the lazy dog";
    size_t      n   = strlen(msg);
    uint8_t a[32], b[32];

    ub_sha256(msg, n, a);

    ub_sha256_ctx c;
    ub_sha256_init(&c);
    for (size_t i = 0; i < n; i++) ub_sha256_update(&c, msg + i, 1);
    ub_sha256_final(&c, b);

    EXPECT("sha256: byte-by-byte update == one-shot",
           memcmp(a, b, 32) == 0);
}

static void test_file_range(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/ubuilder-sha256.%d", (int)getpid());
    FILE* f = fopen(path, "wb");
    if (!f) { EXPECT("sha256: tmp file open", 0); return; }
    /* File contents: [16 bytes of 0xAA padding][message "abc"][16 bytes 0xBB] */
    uint8_t pad[16];
    memset(pad, 0xAA, sizeof(pad));
    fwrite(pad, 1, sizeof(pad), f);
    fwrite("abc", 1, 3, f);
    memset(pad, 0xBB, sizeof(pad));
    fwrite(pad, 1, sizeof(pad), f);
    fclose(f);

    f = fopen(path, "rb");
    EXPECT("sha256: tmp file reopen", f != NULL);
    if (!f) return;

    uint8_t d[32];
    int rc = ub_sha256_file_range(f, 16, 3, d);
    EXPECT("sha256: file_range returns 0", rc == 0);
    EXPECT("sha256: file_range over slice matches \"abc\" digest",
           hex_eq(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    /* Verify the file position is preserved across the call. */
    fseek(f, 7, SEEK_SET);
    ub_sha256_file_range(f, 16, 3, d);
    long pos = ftell(f);
    EXPECT("sha256: file_range preserves position", pos == 7);

    fclose(f);
    unlink(path);
}

void test_sha256(void) {
    printf("\nSHA-256 tests\n");
    printf("-------------\n");
    test_known_vectors();
    test_incremental_matches_oneshot();
    test_file_range();
}
