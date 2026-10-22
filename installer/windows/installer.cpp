// DumbDock Amp - Windows installer / uninstaller (no dependencies, plain Win32).
//   Install:    run Install-DumbDockAmp.exe (asks for administrator rights itself)
//   Uninstall:  run Uninstall-DumbDockAmp.exe  (same program, built with -DUNINSTALL)
//   Silent:     add /S    (no dialogs; exit code 0 = OK)
// The plugin DLL is embedded as resource 101.  Only one folder is ever written:
//   %CommonProgramFiles%/VST3/DumbDock Amp.vst3  (one folder only)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <cstdio>

static bool gSilent = false;

static void say(const std::wstring& text, UINT icon = MB_ICONINFORMATION)
{
    if (!gSilent) MessageBoxW(nullptr, text.c_str(), L"DumbDock Amp", MB_OK | icon | MB_SETFOREGROUND);
}

static bool ask(const std::wstring& text)
{
    if (gSilent) return true;
    return MessageBoxW(nullptr, text.c_str(), L"DumbDock Amp", MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES;
}

static std::wstring vst3Folder()
{
    wchar_t buf[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES_COMMON, nullptr, SHGFP_TYPE_CURRENT, buf)))
        wcscpy_s(buf, L"C:\\Program Files\\Common Files");
    return std::wstring(buf) + L"\\VST3";
}

static bool makeDirs(const std::wstring& p)
{
    if (CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    const size_t s = p.find_last_of(L'\\');
    if (s == std::wstring::npos) return false;
    if (!makeDirs(p.substr(0, s))) return false;
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::wstring lastError()
{
    const DWORD e = GetLastError();
    wchar_t* m = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPWSTR) &m, 0, nullptr);
    std::wstring r = m ? m : L"unknown error";
    if (m) LocalFree(m);
    return r + L" (code " + std::to_wstring(e) + L")";
}

[[maybe_unused]] static bool removeTree(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    bool ok = true;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            const std::wstring full = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok &= removeTree(full);
            else { SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL); ok &= DeleteFileW(full.c_str()) != 0; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(dir.c_str()) && ok;
}

[[maybe_unused]] static bool dirExists(const std::wstring& p)
{
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

#ifdef UNINSTALL
static int doUninstall()
{
    const std::wstring bundle = vst3Folder() + L"\\DumbDock Amp.vst3";
    if (!dirExists(bundle)) { say(L"DumbDock Amp is not installed - nothing to remove."); return 0; }
    if (!ask(L"Remove DumbDock Amp from:\n" + bundle + L" ?")) return 1;
    if (!removeTree(bundle)) {
        say(L"Could not remove the plugin. Close Reaper (it may be using the file) and run this again.\n\n" + lastError(), MB_ICONERROR);
        return 2;
    }
    say(L"DumbDock Amp has been removed. Your Reaper projects are not touched; they will just show the plugin as missing.");
    return 0;
}
#else
static int doInstall()
{
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    HGLOBAL hg = res ? LoadResource(nullptr, res) : nullptr;
    const void* data = hg ? LockResource(hg) : nullptr;
    const DWORD size = res ? SizeofResource(nullptr, res) : 0;
    if (!data || size < 100000) { say(L"This installer file is damaged (plugin missing). Please download it again.", MB_ICONERROR); return 3; }
    // sanity: must be a Windows DLL (MZ header)
    if (((const unsigned char*) data)[0] != 'M' || ((const unsigned char*) data)[1] != 'Z') { say(L"This installer file is damaged. Please download it again.", MB_ICONERROR); return 3; }

    const std::wstring root   = vst3Folder();
    const std::wstring bundle = root + L"\\DumbDock Amp.vst3";
    const std::wstring dir    = bundle + L"\\Contents\\x86_64-win";
    const std::wstring dll    = dir + L"\\DumbDock Amp.vst3";
    const std::wstring tmp    = dir + L"\\DumbDock Amp.vst3.new";

    if (!ask(L"Install DumbDock Amp (VST3, 64-bit) for Reaper and other DAWs?\n\nIt will be copied to:\n" + bundle +
             L"\n\nNothing else on your computer is changed."))
        return 1;

    if (!makeDirs(dir)) { say(L"Could not create the VST3 folder.\n\n" + lastError(), MB_ICONERROR); return 4; }

    // write to a temp name first, verify, then swap in - an interrupted install never leaves a half-written plugin
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { say(L"Could not write the plugin file.\n\n" + lastError(), MB_ICONERROR); return 4; }
    DWORD written = 0;
    const BOOL wr = WriteFile(f, data, size, &written, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!wr || written != size) { DeleteFileW(tmp.c_str()); say(L"Writing the plugin failed (disk full?).\n\n" + lastError(), MB_ICONERROR); return 4; }

    WIN32_FILE_ATTRIBUTE_DATA ad;
    if (!GetFileAttributesExW(tmp.c_str(), GetFileExInfoStandard, &ad) || ad.nFileSizeLow != size) {
        DeleteFileW(tmp.c_str()); say(L"Verification of the written file failed.", MB_ICONERROR); return 4;
    }

    if (!MoveFileExW(tmp.c_str(), dll.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const std::wstring e = lastError();
        DeleteFileW(tmp.c_str());
        say(L"Could not replace the existing plugin. Close Reaper (and any other DAW) and run the installer again.\n\n" + e, MB_ICONERROR);
        return 5;
    }

    say(L"DumbDock Amp is installed.\n\nIn Reaper:\n"
        L"1. Options > Preferences > Plug-ins > VST\n"
        L"2. Click 'Re-scan' (or 'Clear cache / re-scan')\n"
        L"3. On a track press FX, search 'DumbDock', add 'VST3: DumbDock Amp'.\n\n"
        L"A short welcome overview of the amps and options shows when the plugin window opens.");
    return 0;
}
#endif

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmd, int)
{
    gSilent = cmd && (wcsstr(cmd, L"/S") || wcsstr(cmd, L"/s") || wcsstr(cmd, L"/silent"));
#ifdef UNINSTALL
    return doUninstall();
#else
    if (cmd && wcsstr(cmd, L"/uninstall")) { say(L"Use Uninstall-DumbDockAmp.exe to remove the plugin."); return 0; }
    return doInstall();
#endif
}
