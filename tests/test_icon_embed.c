/* Cross-platform unit tests for ICO header validation. The actual
 * UpdateResource embedding is exercised only on Windows CI; here we
 * cover what's testable on every host: parsing well-formed and
 * malformed .ico files end-to-end through ub_ico_validate. */

#include "../src/core/icon_embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Portable temp-file I/O: POSIX has unistd write/close/unlink + mkstemp,
 * MSVC doesn't ship <unistd.h>. Use stdio (fopen/fwrite/fclose) and
 * `remove()` everywhere — both are C-standard. */
#ifdef _WIN32
#  include <process.h>   /* _getpid */
#  define getpid _getpid
#else
#  include <unistd.h>
#endif

extern int test_count;
extern int test_passed;

#define TEST(name, condition) do {                       \
    test_count++;                                        \
    if (condition) { test_passed++; printf("✓ %s\n", name); } \
    else           {                printf("✗ %s\n", name); } \
} while (0)

/* Write `bytes` to a temp file and return the path (heap; caller frees).
 * We generate a unique-enough name from pid + a monotonic counter to avoid
 * collisions across calls within a single test run. Uses stdio rather than
 * mkstemp/open so MSVC (no <unistd.h>) builds cleanly. */
static char* write_temp_ico(const uint8_t* bytes, size_t len) {
    static int counter = 0;
    counter++;
#ifdef _WIN32
    const char* dir = getenv("TEMP");
    if (!dir) dir = getenv("TMP");
    if (!dir) dir = ".";
    const char  sep = '\\';
#else
    const char* dir = "/tmp";
    const char  sep = '/';
#endif
    char path[512];
    snprintf(path, sizeof(path), "%s%cub-icon-%d-%d.ico",
             dir, sep, (int)getpid(), counter);
    FILE* f = fopen(path, "wb");
    if (!f) return NULL;
    if (fwrite(bytes, 1, len, f) != len) { fclose(f); remove(path); return NULL; }
    fclose(f);
    char* p = (char*)malloc(strlen(path) + 1);
    if (p) strcpy(p, path);
    return p;
}

/* Build the minimum bytes that ub_ico_validate accepts: ICONDIR (6) +
 * one ICONDIRENTRY (16) + one byte of "image" so bytes_in_res=1 fits
 * inside the file. Total: 23 bytes. */
static void build_valid_ico(uint8_t out[23]) {
    memset(out, 0, 23);
    /* ICONDIR */
    out[0] = 0; out[1] = 0;       /* reserved = 0          */
    out[2] = 1; out[3] = 0;       /* type     = 1 (icon)   */
    out[4] = 1; out[5] = 0;       /* count    = 1          */
    /* ICONDIRENTRY at offset 6 */
    out[6]  = 16;                  /* width                 */
    out[7]  = 16;                  /* height                */
    out[8]  = 0;                   /* color_count           */
    out[9]  = 0;                   /* reserved              */
    out[10] = 1; out[11] = 0;      /* planes = 1            */
    out[12] = 32; out[13] = 0;     /* bit_count = 32        */
    out[14] = 1; out[15] = 0; out[16] = 0; out[17] = 0;   /* bytes_in_res = 1 */
    out[18] = 22; out[19] = 0; out[20] = 0; out[21] = 0;  /* image_offset = 22 */
    /* image data (1 byte) at offset 22 */
    out[22] = 0xff;
}

static void test_ico_validate_well_formed(void) {
    uint8_t bytes[23];
    build_valid_ico(bytes);
    char* path = write_temp_ico(bytes, sizeof(bytes));
    int n = path ? ub_ico_validate(path) : -1;
    TEST("ico_validate accepts well-formed single-image .ico (returns 1)", n == 1);
    if (path) { remove(path); free(path); }
}

static void test_ico_validate_rejects_short_header(void) {
    uint8_t bytes[3] = {0, 0, 0};
    char* path = write_temp_ico(bytes, sizeof(bytes));
    int n = path ? ub_ico_validate(path) : -1;
    TEST("ico_validate rejects file shorter than ICONDIR", n < 0);
    if (path) { remove(path); free(path); }
}

static void test_ico_validate_rejects_wrong_type(void) {
    uint8_t bytes[23];
    build_valid_ico(bytes);
    bytes[2] = 2;  /* type = 2 (cursor) — we only embed icons */
    char* path = write_temp_ico(bytes, sizeof(bytes));
    int n = path ? ub_ico_validate(path) : -1;
    TEST("ico_validate rejects cursor type (type != 1)", n < 0);
    if (path) { remove(path); free(path); }
}

static void test_ico_validate_rejects_zero_count(void) {
    uint8_t bytes[23];
    build_valid_ico(bytes);
    bytes[4] = 0; bytes[5] = 0;  /* count = 0 */
    char* path = write_temp_ico(bytes, sizeof(bytes));
    int n = path ? ub_ico_validate(path) : -1;
    TEST("ico_validate rejects empty .ico (count = 0)", n < 0);
    if (path) { remove(path); free(path); }
}

static void test_ico_validate_rejects_offset_past_eof(void) {
    uint8_t bytes[23];
    build_valid_ico(bytes);
    bytes[18] = 100;  /* image_offset = 100, file is only 23 bytes */
    char* path = write_temp_ico(bytes, sizeof(bytes));
    int n = path ? ub_ico_validate(path) : -1;
    TEST("ico_validate rejects entry whose image extends past EOF", n < 0);
    if (path) { remove(path); free(path); }
}

static void test_ico_validate_rejects_missing_file(void) {
    int n = ub_ico_validate("/tmp/this-file-does-not-exist.ico");
    TEST("ico_validate rejects missing file path", n < 0);
}

void test_icon_embed(void);
void test_icon_embed(void) {
    printf("\n-- icon_embed --\n");
    test_ico_validate_well_formed();
    test_ico_validate_rejects_short_header();
    test_ico_validate_rejects_wrong_type();
    test_ico_validate_rejects_zero_count();
    test_ico_validate_rejects_offset_past_eof();
    test_ico_validate_rejects_missing_file();
}
