#include "platform_compat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <dirent.h>
  #include <fcntl.h>
  #include <signal.h>
  #include <spawn.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
  extern char** environ;
#endif

/* ============================================================
 * pc_remove_tree
 * ============================================================ */

#ifdef _WIN32

static int wpath_from_utf8(const char* in, wchar_t* out, int out_cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, in, -1, out, out_cap);
    return (n > 0) ? 0 : -1;
}

static int remove_tree_w(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return -1;

    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return DeleteFileW(path) ? 0 : -1;
    }

    wchar_t pattern[MAX_PATH];
    if ((wcslen(path) + 3) >= MAX_PATH) return -1;
    wcscpy(pattern, path);
    wcscat(pattern, L"\\*");

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;

    int rc = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        wchar_t child[MAX_PATH];
        if ((wcslen(path) + wcslen(fd.cFileName) + 2) >= MAX_PATH) { rc = -1; break; }
        wcscpy(child, path);
        wcscat(child, L"\\");
        wcscat(child, fd.cFileName);
        if (remove_tree_w(child) != 0) rc = -1;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (!RemoveDirectoryW(path)) rc = -1;
    return rc;
}

int pc_remove_tree(const char* path) {
    if (!path) return -1;
    wchar_t wpath[MAX_PATH];
    if (wpath_from_utf8(path, wpath, MAX_PATH) != 0) return -1;
    return remove_tree_w(wpath);
}

#else /* POSIX */

static int remove_tree_posix(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }
    DIR* d = opendir(path);
    if (!d) return -1;
    int rc = 0;
    struct dirent* de;
    size_t plen = strlen(path);
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        size_t clen = plen + 1 + strlen(de->d_name) + 1;
        char* child = (char*)malloc(clen);
        if (!child) { rc = -1; break; }
        snprintf(child, clen, "%s/%s", path, de->d_name);
        if (remove_tree_posix(child) != 0) rc = -1;
        free(child);
    }
    closedir(d);
    if (rmdir(path) != 0) rc = -1;
    return rc;
}

int pc_remove_tree(const char* path) {
    if (!path) { errno = EINVAL; return -1; }
    return remove_tree_posix(path);
}

#endif

/* ============================================================
 * pc_spawn_and_wait
 * ============================================================ */

#ifdef _WIN32

/* Quote a single argv element per the CommandLineToArgvW rules. */
static int append_quoted(char* dst, size_t* used, size_t cap, const char* s) {
    size_t need = 2; /* surrounding quotes */
    for (const char* p = s; *p; p++) need += (*p == '"' || *p == '\\') ? 2 : 1;
    if (*used + need + 1 > cap) return -1;
    dst[(*used)++] = '"';
    for (const char* p = s; *p; p++) {
        if (*p == '"' || *p == '\\') dst[(*used)++] = '\\';
        dst[(*used)++] = *p;
    }
    dst[(*used)++] = '"';
    dst[*used] = 0;
    return 0;
}

int pc_spawn_and_wait(const char* exe,
                      char* const argv[],
                      char* const envp[],
                      const char* cwd) {
    if (!exe || !argv) { errno = EINVAL; return -1; }

    /* Build a single command line buffer from argv. */
    size_t cap = 4096;
    char*  cmd = (char*)malloc(cap);
    if (!cmd) return -1;
    size_t used = 0;
    cmd[0] = 0;
    if (append_quoted(cmd, &used, cap, exe) != 0) { free(cmd); return -1; }
    for (int i = 1; argv[i] != NULL; i++) {
        if (used + 1 + strlen(argv[i]) * 2 + 4 > cap) {
            cap *= 2;
            char* nb = (char*)realloc(cmd, cap);
            if (!nb) { free(cmd); return -1; }
            cmd = nb;
        }
        cmd[used++] = ' ';
        if (append_quoted(cmd, &used, cap, argv[i]) != 0) { free(cmd); return -1; }
    }

    /* Build env block: "K1=V1\0K2=V2\0...\0" */
    char* env_block = NULL;
    if (envp) {
        size_t total = 1;
        for (int i = 0; envp[i]; i++) total += strlen(envp[i]) + 1;
        env_block = (char*)malloc(total);
        if (!env_block) { free(cmd); return -1; }
        size_t p = 0;
        for (int i = 0; envp[i]; i++) {
            size_t n = strlen(envp[i]) + 1;
            memcpy(env_block + p, envp[i], n);
            p += n;
        }
        env_block[p] = 0;
    }

    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessA(
        exe,                /* lpApplicationName */
        cmd,                /* lpCommandLine */
        NULL, NULL, FALSE,
        0,
        env_block,
        cwd,
        &si, &pi);

    free(cmd);
    free(env_block);

    if (!ok) { errno = EIO; return -1; }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}

#else /* POSIX */

int pc_spawn_and_wait(const char* exe,
                      char* const argv[],
                      char* const envp[],
                      const char* cwd) {
    if (!exe || !argv) { errno = EINVAL; return -1; }

    posix_spawn_file_actions_t actions;
    int have_actions = 0;
    if (cwd) {
        if (posix_spawn_file_actions_init(&actions) != 0) return -1;
        have_actions = 1;
#ifdef __GLIBC__
        if (posix_spawn_file_actions_addchdir_np(&actions, cwd) != 0) {
            posix_spawn_file_actions_destroy(&actions);
            return -1;
        }
#else
        /* No portable posix_spawn chdir helper on older libcs.
         * Fork+exec is acceptable here; see commented impl below. */
        posix_spawn_file_actions_destroy(&actions);
        have_actions = 0;
#endif
    }

    pid_t pid = 0;
    char* const* env = envp ? envp : environ;
    int rc;
#ifdef __GLIBC__
    rc = posix_spawnp(&pid, exe, have_actions ? &actions : NULL, NULL, argv, env);
#else
    /* Fallback: fork + chdir + execvp[e] so cwd works on every libc. */
    rc = 0;
    pid = fork();
    if (pid < 0) rc = errno;
    else if (pid == 0) {
        if (cwd && chdir(cwd) != 0) _exit(127);
        if (envp) execve(exe, argv, envp);
        else      execvp(exe, argv);
        _exit(127);
    }
#endif
    if (have_actions) posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) { errno = rc; return -1; }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

#endif

/* ============================================================
 * pc_env_overlay / pc_env_free
 * ============================================================ */

static size_t env_key_len(const char* kv) {
    const char* eq = strchr(kv, '=');
    return eq ? (size_t)(eq - kv) : strlen(kv);
}

static int env_key_eq(const char* a, const char* b) {
    size_t la = env_key_len(a);
    size_t lb = env_key_len(b);
    return la == lb && memcmp(a, b, la) == 0;
}

#ifdef _WIN32
/* Windows: read the process environment via GetEnvironmentStringsA. */
static char** snapshot_environ(void) {
    LPCH block = GetEnvironmentStringsA();
    if (!block) return NULL;
    size_t n = 0;
    for (const char* p = block; *p; p += strlen(p) + 1) n++;
    char** out = (char**)calloc(n + 1, sizeof(char*));
    if (!out) { FreeEnvironmentStringsA(block); return NULL; }
    size_t i = 0;
    for (const char* p = block; *p; p += strlen(p) + 1) {
        out[i++] = strdup(p);
    }
    out[i] = NULL;
    FreeEnvironmentStringsA(block);
    return out;
}
#else
static char** snapshot_environ(void) {
    size_t n = 0;
    for (char** e = environ; *e; e++) n++;
    char** out = (char**)calloc(n + 1, sizeof(char*));
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        out[i] = strdup(environ[i]);
        if (!out[i]) {
            for (size_t j = 0; j < i; j++) free(out[j]);
            free(out);
            return NULL;
        }
    }
    out[n] = NULL;
    return out;
}
#endif

char** pc_env_overlay(char* const extra[]) {
    char** base = snapshot_environ();
    if (!base) return NULL;

    if (!extra || !extra[0]) return base;

    /* Replace any matching keys in base; collect new keys for append. */
    for (int i = 0; extra[i]; i++) {
        int replaced = 0;
        for (int j = 0; base[j]; j++) {
            if (env_key_eq(base[j], extra[i])) {
                free(base[j]);
                base[j] = strdup(extra[i]);
                replaced = 1;
                break;
            }
        }
        if (!replaced) {
            size_t bn = 0;
            while (base[bn]) bn++;
            char** grown = (char**)realloc(base, (bn + 2) * sizeof(char*));
            if (!grown) { pc_env_free(base); return NULL; }
            base = grown;
            base[bn]     = strdup(extra[i]);
            base[bn + 1] = NULL;
        }
    }
    return base;
}

void pc_env_free(char** env) {
    if (!env) return;
    for (size_t i = 0; env[i]; i++) free(env[i]);
    free(env);
}

/* ============================================================
 * pc_mkdir_p
 * ============================================================ */

#ifdef _WIN32
static int mkdir_single(const char* path) {
    wchar_t w[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH) <= 0) return -1;
    if (CreateDirectoryW(w, NULL)) return 0;
    return (GetLastError() == ERROR_ALREADY_EXISTS) ? 0 : -1;
}
#else
static int mkdir_single(const char* path) {
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    }
    return -1;
}
#endif

static int is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

int pc_mkdir_p(const char* path) {
    if (!path || !*path) return -1;
    size_t n = strlen(path);
    char* buf = (char*)malloc(n + 1);
    if (!buf) return -1;
    memcpy(buf, path, n + 1);

    /* Walk components left-to-right; mkdir each prefix. Skip leading
     * separators (and drive letters on Windows). */
    size_t i = 0;
#ifdef _WIN32
    /* Skip "C:" drive prefix. */
    if (n >= 2 && ((buf[1] == ':') &&
                   ((buf[0] >= 'A' && buf[0] <= 'Z') ||
                    (buf[0] >= 'a' && buf[0] <= 'z')))) {
        i = 2;
    }
#endif
    while (i < n && is_sep(buf[i])) i++;  /* skip leading separators */

    int rc = 0;
    while (i <= n) {
        if (i == n || is_sep(buf[i])) {
            char saved = buf[i];
            buf[i] = 0;
            if (buf[0] && mkdir_single(buf) != 0) { rc = -1; break; }
            buf[i] = saved;
            if (i == n) break;                  /* whole path done */
            /* coalesce consecutive separators */
            while (i < n && is_sep(buf[i])) i++;
        } else {
            i++;
        }
    }
    free(buf);
    return rc;
}

/* ============================================================
 * pc_path_lookup
 * ============================================================ */

#ifdef _WIN32
#define PATH_SEP_CHAR ';'
static const char* WIN_EXTS[] = { "", ".exe", ".cmd", ".bat", NULL };
#else
#define PATH_SEP_CHAR ':'
#endif

static int is_executable(const char* path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    return access(path, X_OK) == 0;
#endif
}

static int has_separator(const char* s) {
    for (; *s; s++) if (is_sep(*s)) return 1;
    return 0;
}

char* pc_path_lookup(const char* exe) {
    if (!exe || !*exe) return NULL;

    /* If exe contains a separator, treat it as a direct path. */
    if (has_separator(exe)) {
        if (is_executable(exe)) {
            return strdup(exe);
        }
        return NULL;
    }

    const char* path = getenv("PATH");
    if (!path || !*path) return NULL;

    size_t exelen = strlen(exe);
    const char* p = path;
    while (*p) {
        const char* sep = strchr(p, PATH_SEP_CHAR);
        size_t dlen = sep ? (size_t)(sep - p) : strlen(p);
        if (dlen > 0) {
            /* Build "<dir>/<exe>[<ext>]" */
#ifdef _WIN32
            for (int e = 0; WIN_EXTS[e]; e++) {
                size_t extlen = strlen(WIN_EXTS[e]);
                char* cand = (char*)malloc(dlen + 1 + exelen + extlen + 1);
                if (!cand) return NULL;
                memcpy(cand, p, dlen);
                cand[dlen] = '\\';
                memcpy(cand + dlen + 1, exe, exelen);
                memcpy(cand + dlen + 1 + exelen, WIN_EXTS[e], extlen + 1);
                if (is_executable(cand)) return cand;
                free(cand);
            }
#else
            char* cand = (char*)malloc(dlen + 1 + exelen + 1);
            if (!cand) return NULL;
            memcpy(cand, p, dlen);
            cand[dlen] = '/';
            memcpy(cand + dlen + 1, exe, exelen + 1);
            if (is_executable(cand)) return cand;
            free(cand);
#endif
        }
        if (!sep) break;
        p = sep + 1;
    }
    return NULL;
}

/* ============================================================
 * pc_spawn_capture
 * ============================================================ */

#ifdef _WIN32

int pc_spawn_capture(const char* exe,
                     char* const argv[],
                     char* const envp[],
                     const char* cwd,
                     size_t      max_bytes,
                     char**      out) {
    if (out) *out = NULL;
    if (!exe || !argv || !out) { errno = EINVAL; return -1; }

    HANDLE r = NULL, w = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&r, &w, &sa, 0)) return -1;
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    /* Build command line (mirrors pc_spawn_and_wait quoting rules). */
    size_t cap = 4096;
    char*  cmd = (char*)malloc(cap);
    if (!cmd) { CloseHandle(r); CloseHandle(w); return -1; }
    size_t used = 0;
    cmd[0] = 0;
    {
        size_t need = 2;
        for (const char* x = exe; *x; x++) need += (*x == '"' || *x == '\\') ? 2 : 1;
        if (used + need + 1 > cap) { free(cmd); CloseHandle(r); CloseHandle(w); return -1; }
        cmd[used++] = '"';
        for (const char* x = exe; *x; x++) {
            if (*x == '"' || *x == '\\') cmd[used++] = '\\';
            cmd[used++] = *x;
        }
        cmd[used++] = '"';
        cmd[used] = 0;
    }
    for (int i = 1; argv[i] != NULL; i++) {
        if (used + 1 + strlen(argv[i]) * 2 + 4 > cap) {
            cap *= 2;
            char* nb = (char*)realloc(cmd, cap);
            if (!nb) { free(cmd); CloseHandle(r); CloseHandle(w); return -1; }
            cmd = nb;
        }
        cmd[used++] = ' ';
        cmd[used++] = '"';
        for (const char* x = argv[i]; *x; x++) {
            if (*x == '"' || *x == '\\') cmd[used++] = '\\';
            cmd[used++] = *x;
        }
        cmd[used++] = '"';
        cmd[used] = 0;
    }

    /* Build env block. */
    char* env_block = NULL;
    if (envp) {
        size_t total = 1;
        for (int i = 0; envp[i]; i++) total += strlen(envp[i]) + 1;
        env_block = (char*)malloc(total);
        if (env_block) {
            size_t pp = 0;
            for (int i = 0; envp[i]; i++) {
                size_t nb = strlen(envp[i]) + 1;
                memcpy(env_block + pp, envp[i], nb);
                pp += nb;
            }
            env_block[pp] = 0;
        }
    }

    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = w;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    BOOL ok = CreateProcessA(exe, cmd, NULL, NULL, TRUE, 0, env_block, cwd, &si, &pi);
    free(cmd);
    free(env_block);
    CloseHandle(w);
    if (!ok) { CloseHandle(r); errno = EIO; return -1; }

    size_t cap_out = 1024;
    if (cap_out > max_bytes) cap_out = max_bytes;
    char*  buf = (char*)malloc(cap_out + 1);
    if (!buf) { CloseHandle(r); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); return -1; }
    size_t got = 0;
    while (got < max_bytes) {
        if (got == cap_out) {
            size_t nc = cap_out * 2;
            if (nc > max_bytes) nc = max_bytes;
            char* nb = (char*)realloc(buf, nc + 1);
            if (!nb) { free(buf); CloseHandle(r); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); return -1; }
            buf = nb;
            cap_out = nc;
        }
        DWORD n = 0;
        if (!ReadFile(r, buf + got, (DWORD)(cap_out - got), &n, NULL) || n == 0) break;
        got += n;
    }
    CloseHandle(r);
    buf[got] = 0;
    if (got > 0 && buf[got - 1] == '\n') buf[--got] = 0;
    if (got > 0 && buf[got - 1] == '\r') buf[--got] = 0;
    *out = buf;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)ec;
}

#else  /* POSIX */

int pc_spawn_capture(const char* exe,
                     char* const argv[],
                     char* const envp[],
                     const char* cwd,
                     size_t      max_bytes,
                     char**      out) {
    if (out) *out = NULL;
    if (!exe || !argv || !out) { errno = EINVAL; return -1; }

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc != 0) { close(pipefd[0]); close(pipefd[1]); errno = rc; return -1; }
    /* Child: close read end, dup write end to stdout, close write end. */
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2 (&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
#ifdef __GLIBC__
    if (cwd) posix_spawn_file_actions_addchdir_np(&actions, cwd);
#endif

    pid_t pid = 0;
    char* const* env = envp ? envp : environ;
    rc = posix_spawnp(&pid, exe, &actions, NULL, argv, env);
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);    /* parent doesn't write */
    if (rc != 0) { close(pipefd[0]); errno = rc; return -1; }

    size_t cap = 1024;
    if (cap > max_bytes) cap = max_bytes;
    char*  buf = (char*)malloc(cap + 1);
    if (!buf) { close(pipefd[0]); waitpid(pid, NULL, 0); return -1; }
    size_t got = 0;
    while (got < max_bytes) {
        if (got == cap) {
            size_t nc = cap * 2;
            if (nc > max_bytes) nc = max_bytes;
            char* nb = (char*)realloc(buf, nc + 1);
            if (!nb) { free(buf); close(pipefd[0]); waitpid(pid, NULL, 0); return -1; }
            buf = nb;
            cap = nc;
        }
        ssize_t n = read(pipefd[0], buf + got, cap - got);
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        got += (size_t)n;
    }
    close(pipefd[0]);
    buf[got] = 0;
    if (got > 0 && buf[got - 1] == '\n') buf[--got] = 0;
    *out = buf;

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

#endif
