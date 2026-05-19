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

#endif /* !PLATFORM_WINDOWS */
