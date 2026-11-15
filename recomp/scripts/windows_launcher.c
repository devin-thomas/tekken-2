/* Tekken 2 Recompiled: Windows launcher (the exe the desktop shortcut points at).
 *
 * The psxrecomp runtime keeps every file it reads or writes (game.toml, bios\, mods\,
 * settings.toml, disc.cfg, saves\, run reports) beside its own executable, found with
 * GetModuleFileNameW. So the install folder would collect per-user state. This launcher
 * mirrors the install's runtime\ folder into a per-user home, then runs the runtime from
 * there, so every write lands in that home:
 *     %LOCALAPPDATA%\Tekken 2 Recompiled\      (T2_RECOMP_HOME overrides it)
 * Files are copied when missing or when size or write time differ; files under mods\ are
 * copied only when missing, because the runtime writes mods\state.toml. The disc is never
 * bundled: the runtime shows its own Open dialog when no disc is remembered and stores the
 * choice in disc.cfg in the home. Arguments pass through; the exit code is the runtime's.
 * The launcher's STARTUPINFO show flag is passed on, so a no-activate launch stays one.
 * Build: clang -O2 -municode -mwindows windows_launcher.c -o "Tekken 2 Recompiled.exe"
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#define RUNTIME_EXE L"Tekken_2_Recompiled.exe"
#define TITLE L"Tekken 2 Recompiled"
#define PATHMAX 4096

static void fail(const wchar_t *what, const wchar_t *path)
{
    wchar_t msg[PATHMAX + 512];
    DWORD err = GetLastError();
    _snwprintf(msg, PATHMAX + 512, L"%ls\n%ls\n(Windows error %lu)", what, path ? path : L"", err);
    msg[PATHMAX + 511] = 0;
    MessageBoxW(NULL, msg, TITLE, MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

static void join(wchar_t *out, const wchar_t *a, const wchar_t *b)
{
    if (_snwprintf(out, PATHMAX, L"%ls\\%ls", a, b) < 0) fail(L"Path too long:", a);
    out[PATHMAX - 1] = 0;
}

static void ensure_dir(const wchar_t *dir)
{
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        fail(L"Could not create folder:", dir);
}

static int same_file(const wchar_t *src, const wchar_t *dst)
{
    WIN32_FILE_ATTRIBUTE_DATA a, b;
    if (!GetFileAttributesExW(src, GetFileExInfoStandard, &a)) return 0;
    if (!GetFileAttributesExW(dst, GetFileExInfoStandard, &b)) return 0;
    return a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
           CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
}

/* Mirror src into dst. only_missing: never overwrite (user-writable content). */
static void mirror(const wchar_t *src, const wchar_t *dst, int only_missing)
{
    wchar_t pattern[PATHMAX], s[PATHMAX], d[PATHMAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    ensure_dir(dst);
    join(pattern, src, L"*");
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) fail(L"Could not read the install folder:", src);
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        join(s, src, fd.cFileName);
        join(d, dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            mirror(s, d, only_missing || !_wcsicmp(fd.cFileName, L"mods"));
        } else if (GetFileAttributesW(d) == INVALID_FILE_ATTRIBUTES) {
            if (!CopyFileW(s, d, TRUE)) fail(L"Could not copy:", s);
        } else if (!only_missing && !same_file(s, d)) {
            if (!CopyFileW(s, d, FALSE)) fail(L"Could not update (is the game already running?):", d);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* Everything after argv[0] in the raw command line, so quoting passes through untouched. */
static const wchar_t *args_after_program(const wchar_t *cmd)
{
    int quoted = 0;
    while (*cmd && (quoted || (*cmd != L' ' && *cmd != L'\t'))) {
        if (*cmd == L'"') quoted = !quoted;
        cmd++;
    }
    while (*cmd == L' ' || *cmd == L'\t') cmd++;
    return cmd;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    wchar_t self[PATHMAX], install[PATHMAX], runtime[PATHMAX], home[PATHMAX], exe[PATHMAX];
    static wchar_t cmd[32768];
    wchar_t *slash;
    DWORD n, code = 1;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    (void)inst; (void)prev; (void)cmdline; (void)show;

    n = GetModuleFileNameW(NULL, self, PATHMAX);
    if (!n || n >= PATHMAX) fail(L"Could not find the launcher's own path.", NULL);
    wcscpy(install, self);
    slash = wcsrchr(install, L'\\');
    if (slash) *slash = 0;
    join(runtime, install, L"runtime");

    n = GetEnvironmentVariableW(L"T2_RECOMP_HOME", home, PATHMAX);
    if (!n || n >= PATHMAX) {
        n = GetEnvironmentVariableW(L"LOCALAPPDATA", home, PATHMAX);
        if (!n || n >= PATHMAX) fail(L"LOCALAPPDATA is not set.", NULL);
        wcscat(home, L"\\" TITLE);
    }
    mirror(runtime, home, 0);
    join(exe, home, RUNTIME_EXE);

    _snwprintf(cmd, 32768, L"\"%ls\" %ls", exe, args_after_program(GetCommandLineW()));
    cmd[32767] = 0;
    GetStartupInfoW(&si);
    si.cb = sizeof(si);
    si.lpReserved = NULL; si.lpDesktop = NULL; si.lpTitle = NULL;
    si.cbReserved2 = 0; si.lpReserved2 = NULL;
    si.dwFlags &= STARTF_USESHOWWINDOW;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, home, &si, &pi))
        fail(L"Could not start the game:", exe);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}
