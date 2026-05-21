#ifndef UBUILDER_ICON_EMBED_H
#define UBUILDER_ICON_EMBED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Embed a Windows .ico file as the RT_GROUP_ICON("MAINICON") + RT_ICON
 * resources of an existing .exe.
 *
 *   exe_path  — output .exe path (must already exist on disk; the
 *               function rewrites its PE resource section in place).
 *   ico_path  — input .ico file.
 *
 * Returns 0 on success, non-zero on any failure (file read, PE update,
 * malformed ICO). The function prints a clear diagnostic to stderr
 * before returning non-zero.
 *
 * Behavior matrix:
 *
 *   Host    | Target    | Effect
 *   --------+-----------+---------------------------------------------
 *   Windows | Windows   | Real PE-resource update via UpdateResourceW
 *   Windows | Other     | (caller is responsible for not calling us)
 *   Other   | Windows   | Logs a "skipping" note and returns 0
 *   Other   | Other     | Same — no-op success
 *
 * The "cross-host" Windows-PE-edit case is technically doable with a
 * hand-rolled PE writer (no kernel32 needed) but it's >500 LOC of
 * fiddly format work for vanishingly little payoff: ubuilder's release
 * pipeline builds each OS's binary on that OS, never cross. Punt
 * unless real demand shows up. */
int ub_embed_windows_icon(const char* exe_path, const char* ico_path);

/* Pure-C ICO header validation, exposed for unit tests. Reads the file
 * at `ico_path` and verifies the ICONDIR + ICONDIRENTRY layout is
 * well-formed. Returns:
 *   >0  number of icon images found (a valid .ico has at least 1)
 *   -1  on read failure / malformed structure (diagnostic printed)
 *   0   never (a 0-image .ico is treated as malformed)
 *
 * Used by both ub_embed_windows_icon (so we fail fast before touching
 * the .exe) and the unit-test suite (so we can verify ICO parsing on
 * non-Windows hosts where the real embed is a no-op). */
int ub_ico_validate(const char* ico_path);

#ifdef __cplusplus
}
#endif

#endif /* UBUILDER_ICON_EMBED_H */
