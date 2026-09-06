// 0lerp.exe - self-contained loader. Embeds 0lerp.dll (resource 101),
// waits for tf_win64.exe, and injects it via LoadLibraryW.
//   --wait N   look for the process for N seconds (default 120)
//   --loop     stay resident and re-inject on every launch

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <string>

static const wchar_t* kProcName = L"tf_win64.exe";

static void log(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n'); fflush(stdout); }

static bool extractDll(std::wstring& outPath) {
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(nullptr, hRes);
    DWORD size = SizeofResource(nullptr, hRes);
    void* data = LockResource(hData);
    if (!data || !size) return false;
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    outPath = std::wstring(tmp) + L"0lerp.dll";
    HANDLE h = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0; WriteFile(h, data, size, &written, nullptr); CloseHandle(h);
    return written == size;
}

static DWORD findProcess() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe); DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) do {
        if (_wcsicmp(pe.szExeFile, kProcName) == 0) { pid = pe.th32ProcessID; break; }
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return pid;
}

static bool alreadyInjected(HANDLE proc) {
    HMODULE mods[1024]; DWORD needed = 0;
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) return false;
    for (unsigned i = 0; i < needed / sizeof(HMODULE); ++i) {
        wchar_t name[MAX_PATH];
        if (GetModuleBaseNameW(proc, mods[i], name, MAX_PATH) && _wcsicmp(name, L"0lerp.dll") == 0) return true;
    }
    return false;
}

static bool inject(DWORD pid, const std::wstring& dllPath) {
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                              PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { log("[!] OpenProcess failed (%lu). Run as admin?", GetLastError()); return false; }
    if (alreadyInjected(proc)) { log("[=] already loaded; skipping"); CloseHandle(proc); return true; }

    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { log("[!] VirtualAllocEx failed"); CloseHandle(proc); return false; }
    WriteProcessMemory(proc, remote, dllPath.c_str(), bytes, nullptr);

    auto pLoadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, pLoadLib, remote, 0, nullptr);
    if (!thread) { log("[!] CreateRemoteThread failed"); VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc); return false; }
    WaitForSingleObject(thread, 5000);
    DWORD exitCode = 0; GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread); VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc);

    if (!exitCode) { log("[!] LoadLibraryW returned 0"); return false; }
    log("[+] injected into pid %lu", pid);
    return true;
}

int wmain(int argc, wchar_t** argv) {
    int waitSecs = 120; bool loop = false;
    for (int i = 1; i < argc; ++i) {
        if (!_wcsicmp(argv[i], L"--wait") && i + 1 < argc) waitSecs = _wtoi(argv[++i]);
        else if (!_wcsicmp(argv[i], L"--loop")) loop = true;
    }
    std::wstring dllPath;
    if (!extractDll(dllPath)) { log("[!] failed to stage payload"); return 1; }

    do {
        log("[*] waiting for tf_win64.exe (up to %ds)...", waitSecs);
        DWORD pid = 0;
        for (int t = 0; t < waitSecs * 2 && !(pid = findProcess()); ++t) Sleep(500);
        if (!pid) { log("[!] process not found."); if (!loop) return 1; else continue; }
        inject(pid, dllPath);
        if (loop) {
            HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (p) { WaitForSingleObject(p, INFINITE); CloseHandle(p); }
            log("[*] game closed; watching for next launch...");
        }
    } while (loop);

    if (!loop) { log("[*] done. Press Enter to exit."); getchar(); }
    return 0;
}
