#include "php_static.h"
#include "../core/platform_compat.h"
#include "../core/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <process.h>   /* _getpid */
#  include <io.h>        /* _chmod-ish */
#  define getpid _getpid
#else
#  include <unistd.h>
#endif

/* Hardcoded — same approach as update_check.c. Switching repos would
 * need a recompile (intentional: we don't want a runtime-tweakable
 * download source). */
#define UB_STATIC_PHP_OWNER  "developersharif"
#define UB_STATIC_PHP_REPO   "ubuilder"

/* Map the build host to the target triple the workflow publishes. */
static const char* host_target(void) {
#if defined(__APPLE__)
#  if defined(__aarch64__) || defined(__arm64__)
    return "macos-arm64";
#  else
    return "macos-x86_64";
#  endif
#elif defined(__linux__)
#  if defined(__aarch64__) || defined(__arm64__)
    return "linux-arm64";   /* not yet built — caller hits "no asset" path */
#  else
    return "linux-x86_64";
#  endif
#else
    return NULL;            /* Windows: handled by caller (no static path yet) */
#endif
}

/* `<base>/<rest>` with overflow check; returns -1 if it wouldn't fit. */
static int joinp(char* dst, size_t cap, const char* base, const char* rest) {
    int n = snprintf(dst, cap, "%s/%s", base, rest);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

/* Cache root: $XDG_CACHE_HOME/ubuilder/runtimes/php
 *  or $HOME/.cache/ubuilder/runtimes/php
 *  or /tmp/ubuilder-runtimes/php   (last-resort) */
static int cache_root(char* out, size_t cap) {
    const char* xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) {
        int n = snprintf(out, cap, "%s/ubuilder/runtimes/php", xdg);
        return (n < 0 || (size_t)n >= cap) ? -1 : 0;
    }
    const char* home = getenv("HOME");
    if (home && *home) {
        int n = snprintf(out, cap, "%s/.cache/ubuilder/runtimes/php", home);
        return (n < 0 || (size_t)n >= cap) ? -1 : 0;
    }
    int n = snprintf(out, cap, "/tmp/ubuilder-runtimes/php");
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

/* Compute sha256 of `path` in-process. Writes the 64-char lowercase
 * hex digest into `out` (must be at least 65 bytes). Returns 0 on
 * success. */
static int file_sha256(const char* path, char* out) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    ub_sha256_ctx ctx;
    ub_sha256_init(&ctx);
    uint8_t buf[65536];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        ub_sha256_update(&ctx, buf, r);
    }
    int err = ferror(f);
    fclose(f);
    if (err) return -1;
    uint8_t digest[UB_SHA256_DIGEST_SIZE];
    ub_sha256_final(&ctx, digest);
    ub_sha256_hex(digest, out);
    return 0;
}

/* Download `url` to `dst_path` via curl. Returns 0 on success. */
static int curl_download(const char* url, const char* dst_path, int verbose) {
    char* curl = pc_path_lookup("curl");
    if (!curl) {
        fprintf(stderr, "Error: curl not found on PATH — needed for --php-runtime=static\n");
        return -1;
    }
    if (verbose) printf("  GET %s\n", url);
    char* argv[] = {
        curl, (char*)"-fL", (char*)"--max-time", (char*)"600",
        (char*)"-H", (char*)"User-Agent: ubuilder-php-static",
        (char*)"-o", (char*)dst_path, (char*)url, NULL
    };
    int rc = pc_spawn_and_wait(curl, argv, NULL, NULL);
    free(curl);
    return rc == 0 ? 0 : -1;
}

ub_result_t ub_php_static_resolve(const char* php_minor,
                                  int         verbose,
                                  char**      out_php_bin) {
    if (!php_minor || !*php_minor || !out_php_bin) return UB_ERROR_INVALID_ARGS;
    *out_php_bin = NULL;

    const char* target = host_target();
    if (!target) {
        fprintf(stderr, "Error: --php-runtime=static is not supported on this OS\n"
                        "       (currently macOS arm64/x86_64 and Linux x86_64 only).\n");
        return UB_ERROR_RUNTIME_NOT_FOUND;
    }

    /* 1. Cache check. */
    char root[1024];
    if (cache_root(root, sizeof(root)) != 0) return UB_ERROR_INVALID_ARGS;

    char subdir[1280];
    int n = snprintf(subdir, sizeof(subdir), "%s/%s-%s", root, php_minor, target);
    if (n < 0 || (size_t)n >= sizeof(subdir)) return UB_ERROR_INVALID_ARGS;

    char php_path[1400];
    if (joinp(php_path, sizeof(php_path), subdir, "php") != 0) return UB_ERROR_INVALID_ARGS;

    struct stat st;
    if (stat(php_path, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111)) {
        if (verbose) printf("Static PHP cache hit: %s\n", php_path);
        *out_php_bin = strdup(php_path);
        return *out_php_bin ? UB_SUCCESS : UB_ERROR_MEMORY_ALLOCATION;
    }

    /* 2. Cache miss — download + verify + install. */
    char asset[256];
    snprintf(asset, sizeof(asset),
             "ubuilder-static-php-%s-%s.tar.gz", php_minor, target);
    char url[1024];
    snprintf(url, sizeof(url),
             "https://github.com/%s/%s/releases/download/static-php-v%s.0/%s",
             UB_STATIC_PHP_OWNER, UB_STATIC_PHP_REPO, php_minor, asset);
    char sha_url[1100];
    snprintf(sha_url, sizeof(sha_url), "%s.sha256", url);

    /* Stage in a temp dir under the cache root so a partial download
     * never half-poisons the cache. mv-into-place at the end. */
    if (pc_mkdir_p(root) != 0) {
        fprintf(stderr, "Error: cannot create %s (%s)\n", root, strerror(errno));
        return UB_ERROR_EXTRACTION_FAILED;
    }
    char stage[1300];
    snprintf(stage, sizeof(stage), "%s/.dl-%s-%s-%d", root, php_minor, target, (int)getpid());
    pc_remove_tree(stage);
    if (pc_mkdir_p(stage) != 0) {
        fprintf(stderr, "Error: cannot create staging dir %s (%s)\n", stage, strerror(errno));
        return UB_ERROR_EXTRACTION_FAILED;
    }

    char tarball[1400];
    char shafile[1400];
    if (joinp(tarball, sizeof(tarball), stage, asset) != 0 ||
        joinp(shafile, sizeof(shafile), stage, "sha256.txt") != 0) {
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }

    printf("Downloading static PHP %s for %s ...\n", php_minor, target);
    if (curl_download(url, tarball, verbose) != 0) {
        fprintf(stderr, "Error: failed to download %s\n"
                        "       (check network, or that release static-php-v%s.0 exists\n"
                        "        at https://github.com/%s/%s/releases)\n",
                url, php_minor, UB_STATIC_PHP_OWNER, UB_STATIC_PHP_REPO);
        pc_remove_tree(stage);
        return UB_ERROR_FILE_NOT_FOUND;
    }
    if (curl_download(sha_url, shafile, verbose) != 0) {
        fprintf(stderr, "Error: failed to download checksum from %s\n", sha_url);
        pc_remove_tree(stage);
        return UB_ERROR_FILE_NOT_FOUND;
    }

    /* Parse expected sha from shafile (first 64 hex chars). */
    FILE* shf = fopen(shafile, "r");
    char expected[80] = {0};
    if (shf) {
        if (fread(expected, 1, 64, shf) != 64) expected[0] = 0;
        fclose(shf);
        expected[64] = 0;
    }
    if (!expected[0]) {
        fprintf(stderr, "Error: could not read checksum from %s\n", shafile);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    /* Lowercase normalize. */
    for (char* p = expected; *p; p++) {
        if (*p >= 'A' && *p <= 'F') *p = (char)(*p - 'A' + 'a');
    }

    char actual[80] = {0};
    if (file_sha256(tarball, actual) != 0) {
        fprintf(stderr, "Error: failed to compute sha256 of %s\n", tarball);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "Error: sha256 mismatch for %s\n"
                        "       expected %s\n"
                        "       got      %s\n"
                        "       (corrupted download or wrong release asset)\n",
                asset, expected, actual);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    if (verbose) printf("  sha256 OK (%s)\n", actual);

    /* Extract tarball into stage/. The workflow tars `php` directly
     * (no leading directory), so it ends up at <stage>/php. */
    char* tar_exe = pc_path_lookup("tar");
    if (!tar_exe) {
        fprintf(stderr, "Error: tar not found on PATH\n");
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    char* tar_argv[] = {
        tar_exe, (char*)"-xzf", tarball, (char*)"-C", stage, NULL
    };
    int rc = pc_spawn_and_wait(tar_exe, tar_argv, NULL, NULL);
    free(tar_exe);
    if (rc != 0) {
        fprintf(stderr, "Error: tar failed to extract %s (exit %d)\n", tarball, rc);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }

    char extracted_php[1400];
    if (joinp(extracted_php, sizeof(extracted_php), stage, "php") != 0) {
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    if (stat(extracted_php, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "Error: tarball %s did not contain ./php\n", asset);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    chmod(extracted_php, 0755);

    /* Smoke-test before moving into cache. */
    char* probe_argv[] = { extracted_php, (char*)"-v", NULL };
    char* probe_out = NULL;
    rc = pc_spawn_capture(extracted_php, probe_argv, NULL, NULL, 4096, &probe_out);
    if (rc != 0 || !probe_out || !strstr(probe_out, "PHP")) {
        fprintf(stderr, "Error: downloaded php failed its --version smoke test (rc=%d)\n"
                        "       Output: %s\n",
                rc, probe_out ? probe_out : "(empty)");
        free(probe_out);
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    free(probe_out);

    /* mv staged binary into final cache subdir. */
    pc_remove_tree(subdir);   /* harmless if absent */
    if (pc_mkdir_p(subdir) != 0) {
        fprintf(stderr, "Error: cannot create %s (%s)\n", subdir, strerror(errno));
        pc_remove_tree(stage);
        return UB_ERROR_EXTRACTION_FAILED;
    }
    if (rename(extracted_php, php_path) != 0) {
        /* Cross-fs (XDG cache on a different mount than /tmp): fall
         * back to copy via pc_copy_or_link_tree-equivalent (cat). */
        FILE* fi = fopen(extracted_php, "rb");
        FILE* fo = fi ? fopen(php_path, "wb") : NULL;
        if (!fi || !fo) {
            if (fi) fclose(fi);
            if (fo) fclose(fo);
            fprintf(stderr, "Error: cannot install php into %s (%s)\n", php_path, strerror(errno));
            pc_remove_tree(stage);
            return UB_ERROR_EXTRACTION_FAILED;
        }
        char buf[65536];
        size_t r;
        while ((r = fread(buf, 1, sizeof(buf), fi)) > 0) {
            if (fwrite(buf, 1, r, fo) != r) { fclose(fi); fclose(fo); pc_remove_tree(stage); return UB_ERROR_EXTRACTION_FAILED; }
        }
        fclose(fi);
        fclose(fo);
        chmod(php_path, 0755);
    }
    pc_remove_tree(stage);

    printf("Static PHP installed: %s\n", php_path);
    *out_php_bin = strdup(php_path);
    return *out_php_bin ? UB_SUCCESS : UB_ERROR_MEMORY_ALLOCATION;
}
