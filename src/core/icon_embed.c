#include "icon_embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#ifdef _WIN32
#  include <windows.h>
#endif

/* ICO file layout (Win32 official):
 *
 *   ICONDIR (6 bytes):
 *     uint16 idReserved   (must be 0)
 *     uint16 idType       (must be 1 = icon; 2 = cursor)
 *     uint16 idCount      (number of images)
 *
 *   ICONDIRENTRY[idCount] (16 bytes each):
 *     uint8  bWidth       (0 = 256)
 *     uint8  bHeight      (0 = 256)
 *     uint8  bColorCount  (0 if >=8bpp)
 *     uint8  bReserved
 *     uint16 wPlanes      (color planes — 1 for normal icons)
 *     uint16 wBitCount    (bits per pixel)
 *     uint32 dwBytesInRes (image data size)
 *     uint32 dwImageOffset(offset from start of file)
 *
 *   then raw image data for each entry.
 *
 * The RT_GROUP_ICON resource payload is identical in shape EXCEPT each
 * 16-byte entry becomes 14 bytes: the trailing dwImageOffset is replaced
 * by a 2-byte WORD resource ID that points at the matching RT_ICON
 * resource. (Documented in MSDN's "Icons" reference.) */

#pragma pack(push, 1)
typedef struct {
    uint16_t reserved;
    uint16_t type;
    uint16_t count;
} ico_dir_t;

typedef struct {
    uint8_t  width;
    uint8_t  height;
    uint8_t  color_count;
    uint8_t  reserved;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t bytes_in_res;
    uint32_t image_offset;
} ico_dir_entry_t;

typedef struct {
    uint8_t  width;
    uint8_t  height;
    uint8_t  color_count;
    uint8_t  reserved;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t bytes_in_res;
    uint16_t id;
} grp_icon_dir_entry_t;
#pragma pack(pop)

/* Slurp `path` into a heap buffer. Caller frees. Returns 0 on success.
 * Sets *out_buf / *out_len. Caps file size at 16 MiB — a typical .ico
 * is <1 MiB; anything larger is almost certainly a wrong-format input. */
static int slurp_ico(const char* path, uint8_t** out_buf, size_t* out_len) {
    *out_buf = NULL;
    *out_len = 0;
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open icon file %s (%s)\n", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < (long)sizeof(ico_dir_t) || sz > 16 * 1024 * 1024) {
        fprintf(stderr, "Error: icon file %s is %ld bytes — implausible for a .ico\n",
                path, sz);
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "Error: short read on %s\n", path);
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

int ub_ico_validate(const char* ico_path) {
    uint8_t* buf = NULL;
    size_t   len = 0;
    if (slurp_ico(ico_path, &buf, &len) != 0) return -1;

    if (len < sizeof(ico_dir_t)) {
        fprintf(stderr, "Error: %s: too short for ICONDIR header\n", ico_path);
        free(buf);
        return -1;
    }
    ico_dir_t dir;
    memcpy(&dir, buf, sizeof(dir));
    if (dir.reserved != 0 || dir.type != 1 || dir.count == 0) {
        fprintf(stderr,
                "Error: %s: not a valid .ico (reserved=%u type=%u count=%u; "
                "expected reserved=0 type=1 count>=1)\n",
                ico_path,
                (unsigned)dir.reserved, (unsigned)dir.type, (unsigned)dir.count);
        free(buf);
        return -1;
    }

    size_t need = sizeof(ico_dir_t) + (size_t)dir.count * sizeof(ico_dir_entry_t);
    if (len < need) {
        fprintf(stderr, "Error: %s: truncated ICONDIRENTRY table (need %zu, have %zu)\n",
                ico_path, need, len);
        free(buf);
        return -1;
    }

    /* Walk every entry; each image blob's offset+size must lie inside the file. */
    for (uint16_t i = 0; i < dir.count; i++) {
        ico_dir_entry_t e;
        memcpy(&e, buf + sizeof(ico_dir_t) + (size_t)i * sizeof(ico_dir_entry_t),
               sizeof(e));
        if (e.bytes_in_res == 0) {
            fprintf(stderr, "Error: %s: image %u has zero-size payload\n", ico_path, i);
            free(buf);
            return -1;
        }
        if ((uint64_t)e.image_offset + (uint64_t)e.bytes_in_res > (uint64_t)len) {
            fprintf(stderr,
                    "Error: %s: image %u extends past EOF (offset=%u size=%u file=%zu)\n",
                    ico_path, i,
                    (unsigned)e.image_offset, (unsigned)e.bytes_in_res, len);
            free(buf);
            return -1;
        }
    }

    free(buf);
    return (int)dir.count;
}

#ifdef _WIN32

/* Build the RT_GROUP_ICON payload from a parsed ICO header. The layout is:
 *
 *   ico_dir_t      (6 bytes — same as on-disk)
 *   grp_icon_dir_entry_t[count]  (14 bytes each — like ICONDIRENTRY but
 *                                  with a 2-byte WORD resource ID
 *                                  replacing the 4-byte file offset)
 *
 * Resource IDs assigned 1..count, matching the RT_ICON IDs we Update. */
static uint8_t* build_group_icon(const uint8_t* ico_buf, size_t ico_len, size_t* out_len) {
    (void)ico_len;
    ico_dir_t dir;
    memcpy(&dir, ico_buf, sizeof(dir));

    size_t bytes = sizeof(ico_dir_t) + (size_t)dir.count * sizeof(grp_icon_dir_entry_t);
    uint8_t* out = (uint8_t*)malloc(bytes);
    if (!out) return NULL;

    memcpy(out, &dir, sizeof(dir));
    for (uint16_t i = 0; i < dir.count; i++) {
        ico_dir_entry_t e;
        memcpy(&e, ico_buf + sizeof(ico_dir_t) + (size_t)i * sizeof(ico_dir_entry_t),
               sizeof(e));
        grp_icon_dir_entry_t g;
        g.width        = e.width;
        g.height       = e.height;
        g.color_count  = e.color_count;
        g.reserved     = e.reserved;
        g.planes       = e.planes;
        g.bit_count    = e.bit_count;
        g.bytes_in_res = e.bytes_in_res;
        g.id           = (uint16_t)(i + 1);
        memcpy(out + sizeof(ico_dir_t) + (size_t)i * sizeof(grp_icon_dir_entry_t),
               &g, sizeof(g));
    }
    *out_len = bytes;
    return out;
}

int ub_embed_windows_icon(const char* exe_path, const char* ico_path) {
    uint8_t* ico_buf = NULL;
    size_t   ico_len = 0;
    if (slurp_ico(ico_path, &ico_buf, &ico_len) != 0) return -1;
    int n = ub_ico_validate(ico_path);
    if (n <= 0) { free(ico_buf); return -1; }

    ico_dir_t dir;
    memcpy(&dir, ico_buf, sizeof(dir));

    /* MultiByteToWideChar exe_path for BeginUpdateResourceW (the W form
     * is the safe choice — exe_path may contain non-ASCII characters
     * from the user's output= setting). */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, exe_path, -1, NULL, 0);
    if (wlen <= 0) {
        fprintf(stderr, "Error: invalid UTF-8 in exe path %s\n", exe_path);
        free(ico_buf);
        return -1;
    }
    wchar_t* wpath = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wpath) { free(ico_buf); return -1; }
    MultiByteToWideChar(CP_UTF8, 0, exe_path, -1, wpath, wlen);

    HANDLE h = BeginUpdateResourceW(wpath, FALSE /* don't delete existing resources */);
    if (!h) {
        DWORD err = GetLastError();
        fprintf(stderr, "Error: BeginUpdateResource(%s) failed: Win32 error %lu\n",
                exe_path, (unsigned long)err);
        free(wpath);
        free(ico_buf);
        return -1;
    }

    /* Use US English (1033) for the resource language. Matches what RC
     * compilers default to when no LANGUAGE statement is given. */
    const WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    int ok = 1;

    /* Each image becomes an RT_ICON resource with integer ID i+1.
     *
     * NOTE: must use MAKEINTRESOURCEW(3) for RT_ICON rather than the
     * platform RT_ICON macro. The latter expands to MAKEINTRESOURCE
     * which is LPSTR, mismatching the W-form UpdateResourceW signature
     * (MSVC errors C4133, mingw compiles but the resource type is
     * stored wrong). Same applies to RT_GROUP_ICON (14) below. */
    for (uint16_t i = 0; i < dir.count && ok; i++) {
        ico_dir_entry_t e;
        memcpy(&e, ico_buf + sizeof(ico_dir_t) + (size_t)i * sizeof(ico_dir_entry_t),
               sizeof(e));
        uint8_t* img = ico_buf + e.image_offset;
        if (!UpdateResourceW(h, MAKEINTRESOURCEW(3) /* RT_ICON */,
                             MAKEINTRESOURCEW(i + 1),
                             lang, img, e.bytes_in_res)) {
            DWORD err = GetLastError();
            fprintf(stderr, "Error: UpdateResource(RT_ICON, %u) failed: Win32 error %lu\n",
                    (unsigned)(i + 1), (unsigned long)err);
            ok = 0;
        }
    }

    /* RT_GROUP_ICON ties them together. Name "MAINICON" is the historic
     * convention Explorer / Windows shell look for first when picking
     * the icon to display. */
    size_t   grp_len = 0;
    uint8_t* grp     = ok ? build_group_icon(ico_buf, ico_len, &grp_len) : NULL;
    if (ok && grp) {
        if (!UpdateResourceW(h, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */,
                             L"MAINICON", lang, grp, (DWORD)grp_len)) {
            DWORD err = GetLastError();
            fprintf(stderr, "Error: UpdateResource(RT_GROUP_ICON) failed: Win32 error %lu\n",
                    (unsigned long)err);
            ok = 0;
        }
    } else if (ok) {
        fprintf(stderr, "Error: out of memory building RT_GROUP_ICON payload\n");
        ok = 0;
    }
    free(grp);

    /* EndUpdateResource commits (or discards) the staged changes. */
    if (!EndUpdateResourceW(h, ok ? FALSE : TRUE /* fDiscard */)) {
        DWORD err = GetLastError();
        fprintf(stderr, "Error: EndUpdateResource failed: Win32 error %lu\n",
                (unsigned long)err);
        ok = 0;
    }

    free(wpath);
    free(ico_buf);
    return ok ? 0 : -1;
}

#else /* !_WIN32 */

int ub_embed_windows_icon(const char* exe_path, const char* ico_path) {
    /* Validate the .ico header anyway so a malformed file fails the
     * build on every host, not just on Windows. */
    if (ub_ico_validate(ico_path) <= 0) return -1;

    fprintf(stderr,
            "note: --icon=%s recorded but skipped: cross-host PE-resource\n"
            "      editing isn't implemented; build on a Windows host to\n"
            "      embed the icon into %s.\n",
            ico_path, exe_path);
    return 0;
}

#endif /* _WIN32 */
