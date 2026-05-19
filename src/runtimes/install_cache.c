/*
 * M8-fast: content-addressed install cache for user dependencies.
 *
 * Avoids re-running pip/npm install when (runtime identity + manifest +
 * lockfile) is unchanged. See runtime_embedder.h for the public API and
 * docs/architecture/ROADMAP_NEXT.md item #2 for the design rationale.
 *
 * Layout: $XDG_CACHE_HOME/ubuilder/install-cache/<runtime>/<hex_key>/
 * Each cache entry is a directory tree (Python: deps-only site-packages
 * fragment from `pip install --target=...`; Node: a `node_modules/` tree).
 * The consumer hardlink-merges the entry into its staged build dir.
 */

#include "runtime_embedder.h"
#include "../core/platform_compat.h"
#include "../core/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#ifndef PLATFORM_WINDOWS
#include <unistd.h>
#include <dirent.h>
#endif

/* ============================================================
 * Helpers
 * ============================================================ */

static int read_file_full(const char* path, uint8_t** out, size_t* out_len) {
    *out = NULL; *out_len = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    uint8_t* buf = NULL;
    if (n > 0) {
        buf = (uint8_t*)malloc((size_t)n);
        if (!buf) { fclose(f); return -1; }
        if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
            free(buf); fclose(f); return -1;
        }
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)n;
    return 0;
}

static int sha256_file(const char* path, uint8_t out[32]) {
    uint8_t* data = NULL; size_t len = 0;
    if (read_file_full(path, &data, &len) != 0) return -1;
    ub_sha256(data, len, out);
    free(data);
    return 0;
}

static int install_cache_root(const char* runtime, char* out, size_t out_cap) {
    if (!runtime) return -1;
    const char* xdg  = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    int n;
    if (xdg && *xdg) {
        n = snprintf(out, out_cap, "%s/ubuilder/install-cache/%s", xdg, runtime);
    } else if (home && *home) {
        n = snprintf(out, out_cap, "%s/.cache/ubuilder/install-cache/%s", home, runtime);
    } else {
        return -1;
    }
    if (n < 0 || (size_t)n >= out_cap) return -1;
    return 0;
}

/* ============================================================
 * ub_install_cache_key
 * ============================================================ */

int ub_install_cache_key(const char* runtime_id,
                         const char* manifest_path,
                         const char* lockfile_path,
                         char        out[65]) {
    if (!runtime_id || !manifest_path || !out) return -1;

    uint8_t manifest_sha[32];
    if (sha256_file(manifest_path, manifest_sha) != 0) return -1;

    /* Lockfile slot: empty-string hash unless an extant regular file is
     * provided. Keeps the key stable for projects without a lockfile and
     * distinct for projects with one. */
    uint8_t lock_sha[32];
    int have_lock = 0;
    if (lockfile_path) {
        struct stat st;
        if (stat(lockfile_path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (sha256_file(lockfile_path, lock_sha) != 0) return -1;
            have_lock = 1;
        }
    }
    if (!have_lock) ub_sha256(NULL, 0, lock_sha);

    ub_sha256_ctx c;
    ub_sha256_init(&c);
    const char* tag_rt   = "rt=";
    const char  zero     = '\0';
    const char* tag_man  = "manifest=";
    const char* tag_lock = "lock=";
    ub_sha256_update(&c, tag_rt,   strlen(tag_rt));
    ub_sha256_update(&c, runtime_id, strlen(runtime_id));
    ub_sha256_update(&c, &zero, 1);
    ub_sha256_update(&c, tag_man, strlen(tag_man));
    ub_sha256_update(&c, manifest_sha, 32);
    ub_sha256_update(&c, &zero, 1);
    ub_sha256_update(&c, tag_lock, strlen(tag_lock));
    ub_sha256_update(&c, lock_sha, 32);

    uint8_t digest[32];
    ub_sha256_final(&c, digest);
    ub_sha256_hex(digest, out);
    return 0;
}

/* ============================================================
 * ub_install_cache_entry_path
 * ============================================================ */

int ub_install_cache_entry_path(const char* runtime,
                                const char* hex_key,
                                char*       out,
                                size_t      out_cap) {
    if (!runtime || !hex_key || !out) return -1;
    char root[1024];
    if (install_cache_root(runtime, root, sizeof(root)) != 0) return -1;
    int n = snprintf(out, out_cap, "%s/%s", root, hex_key);
    if (n < 0 || (size_t)n >= out_cap) return -1;
    return 0;
}

/* ============================================================
 * Hardlink-merge (additive)
 *
 * Like pc_copy_or_link_tree, but tolerates EEXIST on per-file link():
 * an existing file at the destination wins (first writer keeps its
 * inode). That matches the semantics we want for "merge cache contents
 * into a staged tree that may already have stdlib bits in it".
 * ============================================================ */

#ifndef PLATFORM_WINDOWS
int ub_link_merge_tree(const char* src, const char* dst) {
    struct stat st;
    if (lstat(src, &st) != 0) return -1;

    if (S_ISLNK(st.st_mode)) {
        char target[2048];
        ssize_t n = readlink(src, target, sizeof(target) - 1);
        if (n < 0) return -1;
        target[n] = 0;
        if (symlink(target, dst) != 0 && errno != EEXIST) return -1;
        return 0;
    }
    if (S_ISREG(st.st_mode)) {
        if (link(src, dst) == 0) return 0;
        if (errno == EEXIST) return 0;
        if (errno != EXDEV && errno != EPERM && errno != EACCES) return -1;
        FILE* in = fopen(src, "rb");
        if (!in) return -1;
        FILE* out = fopen(dst, "wb");
        if (!out) { fclose(in); return -1; }
        char buf[65536]; size_t k;
        while ((k = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (fwrite(buf, 1, k, out) != k) { fclose(in); fclose(out); return -1; }
        }
        fclose(in); fclose(out);
        chmod(dst, st.st_mode & 0xFFFu);
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) return 0;  /* skip device/fifo/socket */

    if (pc_mkdir_p(dst) != 0) return -1;
    DIR* d = opendir(src);
    if (!d) return -1;
    struct dirent* de;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char sp[2048], dp[2048];
        int ns = snprintf(sp, sizeof(sp), "%s/%s", src, de->d_name);
        int nd = snprintf(dp, sizeof(dp), "%s/%s", dst, de->d_name);
        if (ns < 0 || (size_t)ns >= sizeof(sp) ||
            nd < 0 || (size_t)nd >= sizeof(dp)) { rc = -1; break; }
        if (ub_link_merge_tree(sp, dp) != 0) { rc = -1; break; }
    }
    closedir(d);
    return rc;
}
#else
int ub_link_merge_tree(const char* src, const char* dst) {
    (void)src; (void)dst;
    return -1;
}
#endif

/* ============================================================
 * ub_install_cache_lookup
 * ============================================================ */

int ub_install_cache_lookup(const char* runtime,
                            const char* hex_key,
                            const char* dest_dir) {
#ifdef PLATFORM_WINDOWS
    (void)runtime; (void)hex_key; (void)dest_dir;
    return -1;  /* Cache support is POSIX-only for now. */
#else
    if (!runtime || !hex_key || !dest_dir) return -1;
    char entry[2048];
    if (ub_install_cache_entry_path(runtime, hex_key, entry, sizeof(entry)) != 0) return -1;
    struct stat st;
    if (stat(entry, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    return ub_link_merge_tree(entry, dest_dir);
#endif
}

/* ============================================================
 * ub_install_cache_store
 * ============================================================ */

int ub_install_cache_store(const char* runtime,
                           const char* hex_key,
                           const char* src_dir) {
#ifdef PLATFORM_WINDOWS
    (void)runtime; (void)hex_key; (void)src_dir;
    return -1;
#else
    if (!runtime || !hex_key || !src_dir) return -1;

    char root[1024];
    if (install_cache_root(runtime, root, sizeof(root)) != 0) return -1;
    if (pc_mkdir_p(root) != 0) return -1;

    char final_path[2048];
    if (ub_install_cache_entry_path(runtime, hex_key, final_path, sizeof(final_path)) != 0) return -1;

    struct stat st;
    if (stat(final_path, &st) == 0) return 0;  /* already cached — peaceful no-op */

    char tmp_path[2048];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s/.tmp-%d-%lld",
                     root, (int)getpid(), (long long)time(NULL));
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) return -1;
    pc_remove_tree(tmp_path);
    if (pc_copy_or_link_tree(src_dir, tmp_path) != 0) {
        pc_remove_tree(tmp_path);
        return -1;
    }
    if (rename(tmp_path, final_path) != 0) {
        int e = errno;
        pc_remove_tree(tmp_path);
        if (e == EEXIST || e == ENOTEMPTY) return 0;  /* race: another writer won */
        return -1;
    }
    return 0;
#endif
}
