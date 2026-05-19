#include "update_check.h"
#include "ubuilder.h"
#include "platform_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef PLATFORM_WINDOWS
/* Windows path: skip the background-update path entirely for now. The
 * fork/setsid pattern below is POSIX-only; native Windows would need a
 * CreateProcess + DETACHED_PROCESS variant. Not worth the complexity
 * until we have a Windows ubuilder.exe distribution channel. */
void ub_update_check_run(void) { /* no-op on Windows */ }
int  ub_self_update_run(void)  {
    fprintf(stderr, "ubuilder: --self-update is not yet supported on Windows.\n"
                    "          Download the latest ubuilder-windows-amd64.zip manually from\n"
                    "          https://github.com/developersharif/ubuilder/releases/latest\n");
    return 1;
}
#else

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define UB_UPDATE_CHECK_TTL_SECONDS    (24 * 3600)
#define UB_UPDATE_RELEASE_API \
    "https://api.github.com/repos/developersharif/ubuilder/releases/latest"

/* Build the cache-file path under XDG_CACHE_HOME or $HOME/.cache. Writes
 * the resulting path into `out` (caller-provided, `cap` bytes). Returns
 * 0 on success, -1 if neither env var is set. Also mkdir -p's the parent
 * dir on the way through. */
static int build_cache_path(char* out, size_t cap) {
    const char* xdg  = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    char dir[1024];
    if (xdg && *xdg)        snprintf(dir, sizeof(dir), "%s/ubuilder",         xdg);
    else if (home && *home) snprintf(dir, sizeof(dir), "%s/.cache/ubuilder",  home);
    else                    return -1;
    pc_mkdir_p(dir);
    if (snprintf(out, cap, "%s/last-update-check", dir) >= (int)cap) return -1;
    return 0;
}

/* Parse a "<unix_ts> <tag>\n" line. tag goes into out_tag (capacity
 * out_cap), ts into *out_ts. Returns 0 on success, -1 on malformed. */
static int parse_cache_entry(const char* buf, time_t* out_ts,
                             char* out_tag, size_t out_cap) {
    long long ts = 0;
    char tag[128] = {0};
    if (sscanf(buf, "%lld %127s", &ts, tag) != 2) return -1;
    if (!tag[0]) return -1;
    *out_ts = (time_t)ts;
    if (strlen(tag) >= out_cap) return -1;
    memcpy(out_tag, tag, strlen(tag) + 1);
    return 0;
}

/* Compare "vX.Y.Z" or "X.Y.Z" against the compiled-in version. Returns
 *  1 if `tag` is newer than what we are, 0 if equal-or-older or unparseable. */
static int tag_is_newer(const char* tag) {
    if (!tag || !*tag) return 0;
    const char* p = (tag[0] == 'v') ? tag + 1 : tag;
    int maj = 0, min = 0, pat = 0;
    if (sscanf(p, "%d.%d.%d", &maj, &min, &pat) < 2) return 0;
    if (maj != UBUILDER_VERSION_MAJOR) return maj > UBUILDER_VERSION_MAJOR;
    if (min != UBUILDER_VERSION_MINOR) return min > UBUILDER_VERSION_MINOR;
    return pat > UBUILDER_VERSION_PATCH;
}

/* Spawn a fully detached child that hits the GitHub releases-latest
 * endpoint via curl, parses out the "tag_name", and writes the cache
 * file. Parent doesn't wait. Double-fork() so the orphan is reparented
 * to init, no zombies, no stale FDs. */
static void spawn_background_update_check(const char* cache_path) {
    /* If curl isn't installed, give up silently. */
    char* curl = pc_path_lookup("curl");
    if (!curl) return;

    pid_t pid = fork();
    if (pid < 0) { free(curl); return; }
    if (pid > 0) {
        /* Parent: reap the immediate child to avoid zombies; the
         * grandchild will be orphaned and inherited by init. */
        int status;
        waitpid(pid, &status, 0);
        free(curl);
        return;
    }

    /* Intermediate child: fork again and exit, so the grandchild has
     * no parent to wait for it. */
    pid_t gc = fork();
    if (gc < 0) _exit(0);
    if (gc > 0) _exit(0);

    /* Grandchild: fully detached. Close stdio so we don't write to the
     * user's terminal. setsid so we're independent of the controlling
     * tty (handles the "user closes terminal mid-build" case). */
    setsid();
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        if (devnull > 2) close(devnull);
    }

    /* Run curl, capture stdout. -s silent, -L follow redirects, -f
     * non-zero exit on HTTP errors, --max-time bounds the wait. */
    char* curl_argv[] = {
        curl,
        (char*)"-fsL", (char*)"--max-time", (char*)"5",
        (char*)"-H", (char*)"User-Agent: ubuilder-update-check",
        (char*)UB_UPDATE_RELEASE_API,
        NULL
    };
    char* captured = NULL;
    int rc = pc_spawn_capture(curl, curl_argv, NULL, NULL, 65536, &captured);
    free(curl);
    if (rc != 0 || !captured) { free(captured); _exit(0); }

    /* Extract "tag_name":"vX.Y.Z" without pulling in json_mini — saves
     * a link dep here. Look for the literal `"tag_name"` key. */
    const char* needle = "\"tag_name\"";
    char* hit = strstr(captured, needle);
    if (!hit) { free(captured); _exit(0); }
    char* colon = strchr(hit + strlen(needle), ':');
    if (!colon) { free(captured); _exit(0); }
    char* q1 = strchr(colon, '"');
    if (!q1) { free(captured); _exit(0); }
    char* q2 = strchr(q1 + 1, '"');
    if (!q2) { free(captured); _exit(0); }

    char tag[128];
    size_t taglen = (size_t)(q2 - q1 - 1);
    if (taglen == 0 || taglen >= sizeof(tag)) { free(captured); _exit(0); }
    memcpy(tag, q1 + 1, taglen);
    tag[taglen] = 0;
    free(captured);

    /* Write the cache file atomically: write to a sibling .tmp + rename. */
    char tmp[1280];
    snprintf(tmp, sizeof(tmp), "%s.tmp", cache_path);
    FILE* fo = fopen(tmp, "w");
    if (!fo) _exit(0);
    fprintf(fo, "%lld %s\n", (long long)time(NULL), tag);
    fclose(fo);
    rename(tmp, cache_path);
    _exit(0);
}

void ub_update_check_run(void) {
    /* Operator opt-out. */
    const char* opt_out = getenv("UBUILDER_NO_UPDATE_CHECK");
    if (opt_out && *opt_out && strcmp(opt_out, "0") != 0) return;

    /* Don't pester non-interactive contexts (CI, piped output). */
    if (!isatty(fileno(stderr))) return;

    char cache_path[1280];
    if (build_cache_path(cache_path, sizeof(cache_path)) != 0) return;

    /* Try to read the cache. If it's fresh and the version is newer
     * than ours, print the banner. If it's stale or missing, fire off
     * the background refresh. */
    FILE* fi = fopen(cache_path, "r");
    int cache_was_fresh = 0;
    if (fi) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fi)) {
            time_t ts = 0;
            char tag[128];
            if (parse_cache_entry(buf, &ts, tag, sizeof(tag)) == 0) {
                time_t now = time(NULL);
                if (now - ts <= UB_UPDATE_CHECK_TTL_SECONDS) {
                    cache_was_fresh = 1;
                    if (tag_is_newer(tag)) {
                        fprintf(stderr,
                            "ubuilder: a newer version is available (%s, you have %s)\n"
                            "          run `ubuilder --self-update` to upgrade, or set\n"
                            "          UBUILDER_NO_UPDATE_CHECK=1 to silence this notice.\n",
                            tag, ub_get_version_string());
                    }
                }
            }
        }
        fclose(fi);
    }

    if (!cache_was_fresh) {
        spawn_background_update_check(cache_path);
    }
}

/* ----- foreground --self-update path ------------------------------------ */

/* Resolve the latest release tag synchronously via curl. Writes the tag
 * (e.g. "v2.1.5") into out (capacity out_cap). Returns 0 on success,
 * -1 on any failure (no curl, network error, missing tag_name in JSON). */
static int fetch_latest_tag(char* out, size_t out_cap) {
    char* curl = pc_path_lookup("curl");
    if (!curl) {
        fprintf(stderr, "Error: curl not found on PATH — needed for --self-update\n");
        return -1;
    }
    char* argv[] = {
        curl, (char*)"-fsL", (char*)"--max-time", (char*)"15",
        (char*)"-H", (char*)"User-Agent: ubuilder-self-update",
        (char*)UB_UPDATE_RELEASE_API, NULL
    };
    char* captured = NULL;
    int rc = pc_spawn_capture(curl, argv, NULL, NULL, 65536, &captured);
    free(curl);
    if (rc != 0 || !captured) {
        fprintf(stderr, "Error: failed to query %s (curl exit %d) — check network / repo visibility\n",
                UB_UPDATE_RELEASE_API, rc);
        free(captured);
        return -1;
    }
    const char* needle = "\"tag_name\"";
    char* hit = strstr(captured, needle);
    if (!hit) {
        fprintf(stderr, "Error: GitHub API response missing tag_name field\n");
        free(captured); return -1;
    }
    char* colon = strchr(hit + strlen(needle), ':'); if (!colon) { free(captured); return -1; }
    char* q1 = strchr(colon, '"');                   if (!q1)    { free(captured); return -1; }
    char* q2 = strchr(q1 + 1, '"');                  if (!q2)    { free(captured); return -1; }
    size_t taglen = (size_t)(q2 - q1 - 1);
    if (taglen == 0 || taglen >= out_cap) { free(captured); return -1; }
    memcpy(out, q1 + 1, taglen);
    out[taglen] = 0;
    free(captured);
    return 0;
}

/* Build the asset filename + URL for this platform. tag is like "v2.1.5".
 * Returns 0 on success, -1 if platform isn't recognized. */
static int build_asset_url(const char* tag,
                           char* asset_name, size_t asset_cap,
                           char* asset_url,  size_t url_cap) {
    const char* plat = ub_get_platform_name();
    /* release.yml asset naming: ubuilder-<plat>-amd64.tar.gz (linux/macos),
     *                          ubuilder-windows-amd64.zip (Windows). */
    if (strcmp(plat, "linux") == 0 || strcmp(plat, "macos") == 0) {
        if (snprintf(asset_name, asset_cap, "ubuilder-%s-amd64.tar.gz", plat) >= (int)asset_cap) return -1;
    } else {
        fprintf(stderr, "Error: --self-update on platform '%s' is not supported by this build path\n", plat);
        return -1;
    }
    if (snprintf(asset_url, url_cap,
                 "https://github.com/developersharif/ubuilder/releases/download/%s/%s",
                 tag, asset_name) >= (int)url_cap) return -1;
    return 0;
}

int ub_self_update_run(void) {
    /* 1. Resolve latest version. */
    char tag[64];
    if (fetch_latest_tag(tag, sizeof(tag)) != 0) return 1;
    printf("Latest release: %s\n", tag);

    if (!tag_is_newer(tag)) {
        printf("Already on the latest version (you have %s).\n", ub_get_version_string());
        return 0;
    }

    /* 2. Resolve our own path + writability. */
    char self_path[1024];
    if (pc_executable_path(self_path, sizeof(self_path)) != 0) {
        fprintf(stderr, "Error: could not resolve the running ubuilder binary's own path\n");
        return 1;
    }
    if (access(self_path, W_OK) != 0) {
        fprintf(stderr,
            "Error: cannot write to %s (%s).\n"
            "       Either re-run with sudo (`sudo ubuilder --self-update`), or\n"
            "       install the new binary manually:\n"
            "         curl -L https://github.com/developersharif/ubuilder/releases/download/%s/ubuilder-%s-amd64.tar.gz | tar -xz\n",
            self_path, strerror(errno), tag, ub_get_platform_name());
        return 1;
    }

    /* 3. Resolve archive URL for this platform. */
    char asset_name[128], asset_url[512];
    if (build_asset_url(tag, asset_name, sizeof(asset_name),
                        asset_url,  sizeof(asset_url)) != 0) return 1;

    /* 4. Set up a temp staging dir. */
    char stage_dir[1024];
    snprintf(stage_dir, sizeof(stage_dir), "/tmp/ubuilder-self-update-%d", (int)getpid());
    pc_remove_tree(stage_dir);
    if (pc_mkdir_p(stage_dir) != 0) {
        fprintf(stderr, "Error: cannot create staging dir %s\n", stage_dir);
        return 1;
    }
    char archive_path[1280];
    snprintf(archive_path, sizeof(archive_path), "%s/%s", stage_dir, asset_name);

    /* 5. Download. */
    printf("Downloading %s ...\n", asset_url);
    char* curl = pc_path_lookup("curl");
    if (!curl) { fprintf(stderr, "Error: curl not found on PATH\n"); pc_remove_tree(stage_dir); return 1; }
    char* dl_argv[] = {
        curl, (char*)"-fL", (char*)"--max-time", (char*)"120",
        (char*)"-H", (char*)"User-Agent: ubuilder-self-update",
        (char*)"-o", archive_path, (char*)asset_url, NULL
    };
    int rc = pc_spawn_and_wait(curl, dl_argv, NULL, NULL);
    free(curl);
    if (rc != 0) {
        fprintf(stderr, "Error: curl failed to download %s (exit %d)\n", asset_url, rc);
        pc_remove_tree(stage_dir);
        return 1;
    }

    /* 6. Extract via tar (always present on the platforms we ship). */
    char* tar_exe = pc_path_lookup("tar");
    if (!tar_exe) { fprintf(stderr, "Error: tar not found on PATH\n"); pc_remove_tree(stage_dir); return 1; }
    char* tar_argv[] = {
        tar_exe, (char*)"-xzf", archive_path, (char*)"-C", stage_dir, NULL
    };
    rc = pc_spawn_and_wait(tar_exe, tar_argv, NULL, NULL);
    free(tar_exe);
    if (rc != 0) {
        fprintf(stderr, "Error: tar failed to extract %s (exit %d)\n", archive_path, rc);
        pc_remove_tree(stage_dir);
        return 1;
    }

    /* 7. Locate the extracted binary. The release-package tar has the
     * binary at the archive root (`./ubuilder` after tar's leading-./
     * stripping). */
    char new_bin[1280];
    snprintf(new_bin, sizeof(new_bin), "%s/ubuilder", stage_dir);
    struct stat nst;
    if (stat(new_bin, &nst) != 0 || !S_ISREG(nst.st_mode)) {
        fprintf(stderr, "Error: extracted archive doesn't contain ./ubuilder at %s\n", new_bin);
        pc_remove_tree(stage_dir);
        return 1;
    }
    chmod(new_bin, 0755);

    /* 8. Smoke-test: run `<new> --version`. If it doesn't exit 0 with
     * a sensible version string, refuse to install. */
    char* probe_argv[] = { new_bin, (char*)"--version", NULL };
    char* probe_out = NULL;
    rc = pc_spawn_capture(new_bin, probe_argv, NULL, NULL, 4096, &probe_out);
    if (rc != 0 || !probe_out || !strstr(probe_out, "UBuilder")) {
        fprintf(stderr, "Error: the downloaded binary failed its --version smoke test (rc=%d).\n"
                        "       Output: %s\n"
                        "       Aborting; your installed binary is unchanged.\n",
                rc, probe_out ? probe_out : "(empty)");
        free(probe_out);
        pc_remove_tree(stage_dir);
        return 1;
    }
    free(probe_out);

    /* 9. Atomically replace. rename(2) is atomic within a filesystem;
     * across filesystems it fails with EXDEV and we fall back to a
     * copy+unlink. (Common case: /tmp on tmpfs, /usr/local/bin on
     * the root filesystem.) */
    if (rename(new_bin, self_path) != 0) {
        if (errno != EXDEV) {
            fprintf(stderr, "Error: rename(%s, %s) failed: %s\n", new_bin, self_path, strerror(errno));
            pc_remove_tree(stage_dir);
            return 1;
        }
        /* Cross-fs: copy + replace. */
        FILE* in  = fopen(new_bin, "rb");
        if (!in) { fprintf(stderr, "Error: cannot reopen %s\n", new_bin); pc_remove_tree(stage_dir); return 1; }
        char swap_path[1280];
        snprintf(swap_path, sizeof(swap_path), "%s.swap-%d", self_path, (int)getpid());
        FILE* out = fopen(swap_path, "wb");
        if (!out) {
            fprintf(stderr, "Error: cannot open %s for writing: %s\n", swap_path, strerror(errno));
            fclose(in); pc_remove_tree(stage_dir); return 1;
        }
        char buf[8192]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (fwrite(buf, 1, n, out) != n) {
                fclose(in); fclose(out); unlink(swap_path);
                fprintf(stderr, "Error: write failed on %s\n", swap_path);
                pc_remove_tree(stage_dir); return 1;
            }
        }
        fclose(in); fclose(out);
        chmod(swap_path, 0755);
        if (rename(swap_path, self_path) != 0) {
            fprintf(stderr, "Error: final rename failed: %s\n", strerror(errno));
            unlink(swap_path); pc_remove_tree(stage_dir); return 1;
        }
    }

    pc_remove_tree(stage_dir);

    /* Bust the update-check cache so the freshly-installed binary
     * doesn't immediately complain about itself. */
    char cache_path[1280];
    if (build_cache_path(cache_path, sizeof(cache_path)) == 0) {
        unlink(cache_path);
    }

    printf("ubuilder upgraded to %s. Re-run any in-progress commands.\n", tag);
    return 0;
}

#endif /* !PLATFORM_WINDOWS */
