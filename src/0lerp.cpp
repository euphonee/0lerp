// 0lerp.dll - clientside interpolation/HUD patches for tf_win64.exe (offline / -insecure).
// Each patch is verified against its expected bytes before applying, and restored on unload.

#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

static const uint32_t UPDATERATE_DISPLAY = 25000; // net_graph top  "%i/s"
static const uint32_t CMDRATE_DISPLAY    = 100;   // net_graph bottom "%i/s"

struct Patch {
    const char* name;
    uintptr_t   rva;
    uint8_t     expected[8];
    uint8_t     patch[8];
    size_t      len;
    uint8_t     saved[8];
    bool        applied;
};

static Patch g_patches[] = {
    { "GetClientInterpAmount",        0x2312a0, {0x40,0x53,0x48,0x83},           {0x0F,0x57,0xC0,0xC3},           4, {}, false }, // -> return 0 (HUD lerp)
    { "C_BaseAnimating::Interpolate", 0x1d1a60, {0x48,0x8B,0xC4},                {0xB0,0x01,0xC3},                3, {}, false }, // -> return true (no interp)
    { "C_BaseEntity::Interpolate",    0x1e4c00, {0x4C,0x8B,0xDC},                {0xB0,0x01,0xC3},                3, {}, false }, // -> return true (no interp)
    { "net_graph updaterate",         0x329163, {0xFF,0x90,0x90,0x00,0x00,0x00}, {0xB8,0x00,0x00,0x00,0x00,0x90}, 6, {}, false }, // mov eax,imm; nop
    { "net_graph cmdrate",            0x329626, {0xFF,0x90,0x90,0x00,0x00,0x00}, {0xB8,0x00,0x00,0x00,0x00,0x90}, 6, {}, false }, // mov eax,imm; nop
};
static const int PATCH_COUNT = sizeof(g_patches) / sizeof(g_patches[0]);

static void logf(const char* fmt, ...) {
    char path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, path)) return;
    strcat_s(path, "0lerp.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") || !f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

static void fillImm(Patch& p, uint32_t v) {
    p.patch[1] = (uint8_t)(v);       p.patch[2] = (uint8_t)(v >> 8);
    p.patch[3] = (uint8_t)(v >> 16); p.patch[4] = (uint8_t)(v >> 24);
}

static bool writeBytes(uintptr_t addr, const uint8_t* bytes, size_t len) {
    DWORD oldProt;
    if (!VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    memcpy((void*)addr, bytes, len);
    VirtualProtect((void*)addr, len, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, len);
    return true;
}

static void applyAll(uintptr_t base) {
    fillImm(g_patches[3], UPDATERATE_DISPLAY);
    fillImm(g_patches[4], CMDRATE_DISPLAY);
    for (int i = 0; i < PATCH_COUNT; ++i) {
        Patch& p = g_patches[i];
        uintptr_t addr = base + p.rva;
        if (memcmp((void*)addr, p.expected, p.len) != 0) {
            logf("[skip] %s: bytes differ (game updated?)", p.name);
            continue;
        }
        memcpy(p.saved, (void*)addr, p.len);
        if (writeBytes(addr, p.patch, p.len)) { p.applied = true; logf("[ok] %s", p.name); }
        else logf("[err] %s: write failed", p.name);
    }
}

static void restoreAll(uintptr_t base) {
    for (int i = 0; i < PATCH_COUNT; ++i)
        if (g_patches[i].applied) { writeBytes(base + g_patches[i].rva, g_patches[i].saved, g_patches[i].len); g_patches[i].applied = false; }
}

static uintptr_t g_base = 0;

static DWORD WINAPI Worker(LPVOID) {
    HMODULE client = nullptr;
    for (int i = 0; i < 600 && !client; ++i) { client = GetModuleHandleA("client.dll"); if (!client) Sleep(100); }
    if (!client) { logf("[!] client.dll never loaded"); return 0; }
    g_base = (uintptr_t)client;
    applyAll(g_base);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(hInst); CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr); }
    else if (reason == DLL_PROCESS_DETACH && g_base) restoreAll(g_base);
    return TRUE;
}
