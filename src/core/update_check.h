#ifndef UBUILDER_UPDATE_CHECK_H
#define UBUILDER_UPDATE_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Background update-available check.
 *
 * Called once from builder-mode main(), right after config_apply. Looks
 * for a cached "latest known release" entry under
 *   $XDG_CACHE_HOME/ubuilder/last-update-check
 * (or $HOME/.cache/ubuilder/... as fallback). If the cache is fresh
 * (<= UB_UPDATE_CHECK_TTL_SECONDS old) and the cached version is newer
 * than UBUILDER_VERSION_{MAJOR,MINOR,PATCH}, prints a one-line banner
 * to stderr — e.g.:
 *
 *   ubuilder: a newer version is available (v2.3.0 → run `ubuilder --self-update`)
 *
 * If the cache is stale or missing, this function forks a detached
 * child that runs `curl` against the GitHub releases-latest endpoint
 * and writes the cache file. The parent returns immediately — no
 * network latency added to the user's build.
 *
 * Skipped entirely (no banner, no spawn) when:
 *   - UBUILDER_NO_UPDATE_CHECK=1 is set in the environment, or
 *   - stdout/stderr isn't a TTY (CI / piped invocations — banner would
 *     just be noise).
 *
 * Always safe to call: any failure (no curl, offline, network error,
 * cache write failure) is silent and non-fatal.
 */
void ub_update_check_run(void);

/*
 * Foreground self-update. Triggered by `ubuilder --self-update`.
 *
 * Resolves the latest GitHub release tag (fresh fetch, ignores the
 * 24h cache), determines the platform archive name, downloads via
 * curl into a temp dir, extracts the inner ubuilder binary via tar,
 * verifies the new binary runs (`<new> --version` exit 0), and
 * atomically replaces the currently-running binary with it.
 *
 * Returns 0 on success, non-zero on any error (network down, no
 * write permission to the binary's path, hash mismatch, extracted
 * binary doesn't run, …). Always prints a precise reason on failure.
 *
 * If the latest tag is the same as or older than the running version,
 * prints "already on latest" and returns 0.
 */
int ub_self_update_run(void);

#ifdef __cplusplus
}
#endif

#endif /* UBUILDER_UPDATE_CHECK_H */
