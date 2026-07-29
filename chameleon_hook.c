#include <windows.h>
#include <stdint.h>
#include <string.h>

/* RVAs below are for the 2026-07-29 build of PenguinHotel-Win64-Shipping.exe
   (sha256 a816b81498074fb44af96bac79bdad84513ccf896907e6ec76da5c25daf40b63) */
#define WFSTRING_CTOR_RVA 0x11ED530

static void ensure_auth_ini(void) {
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("LOCALAPPDATA", path, sizeof(path))) return;
    const char *parts[] = {"\\Chameleon", "\\Saved", "\\Config", "\\Windows"};
    for (int i = 0; i < 4; i++) { strcat(path, parts[i]); CreateDirectoryA(path, NULL); }
    strcat(path, "\\Engine.ini");

    HANDLE r = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	
    if (r != INVALID_HANDLE_VALUE) {
        static char buf[16384]; DWORD got = 0;
        if (ReadFile(r, buf, sizeof(buf) - 1, &got, NULL)) {
            buf[got] = 0;
            if (strstr(buf, "AuthenticationGraph")) { CloseHandle(r); return; }
        }
        CloseHandle(r);
    }
    HANDLE w = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	
    if (w == INVALID_HANDLE_VALUE) return;
    SetFilePointer(w, 0, NULL, FILE_END);
	
    const char *txt = "\r\n[EpicOnlineServices]\r\nAuthenticationGraph=Anonymous\r\n";
    DWORD wr; WriteFile(w, txt, (DWORD)strlen(txt), &wr, NULL);
    CloseHandle(w);
}

static void ensure_steam_appid(void) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!n || n >= sizeof(path)) return;
    char *slash = strrchr(path, '\\');
    if (!slash) return;
    strcpy(slash + 1, "steam_appid.txt");
    HANDLE w = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (w == INVALID_HANDLE_VALUE) return;
    DWORD wr; WriteFile(w, "480\n", 4, &wr, NULL);
    CloseHandle(w);
}

static uint8_t *g_base;
typedef int64_t (*fn_t)(void *, void *, void *, void *);
typedef void *(*wfstrctor_t)(void *out, const wchar_t *wstr);

#define NCAND 2

static const uintptr_t g_rva[NCAND]   = {0x5343540, 0x53B78D0};
static const int       g_steal[NCAND]  = {18,        15};
static const int       g_ripfix[NCAND] = {14,        -1};
static fn_t            g_tramp[NCAND];

static const uint8_t g_expect0[] = {0x40,0x53,0x55,0x56,0x57,0x41,0x56};
static const uint8_t g_expect1[] = {0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x56};
static const uint8_t *g_expect[NCAND] = {g_expect0, g_expect1};
static const int      g_explen[NCAND] = {7, 10};

static const char *steam_persona(void) {
    HMODULE m = GetModuleHandleA("steam_api64.dll");
    if (!m) return NULL;
    void *(*friends)(void) = (void *(*)(void))GetProcAddress(m, "SteamAPI_SteamFriends_v017");
    const char *(*getname)(void *) = (const char *(*)(void *))GetProcAddress(m, "SteamAPI_ISteamFriends_GetPersonaName");
    if (!friends || !getname) return NULL;
    void *fr = friends();
    return fr ? getname(fr) : NULL;
}

static void put_steam_name(void *fs) {
    const char *name = steam_persona();
    if (!name || !name[0]) return;
    wchar_t w[256];
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 256) <= 0) return;
    ((wfstrctor_t)(g_base + WFSTRING_CTOR_RVA))(fs, w);
}

static int64_t hook_getnick(void *a1, void *a2, void *a3, void *a4) {
    int64_t r = g_tramp[0](a1, a2, a3, a4);
    if (a3) put_steam_name(a3);
    return r;
}

static int64_t hook_nickimpl(void *a1, void *a2, void *a3, void *a4) {
    int64_t r = g_tramp[1](a1, a2, a3, a4);
    if (a2) put_steam_name(a2);
    return r;
}
static void *g_hooks[NCAND] = {(void *)hook_getnick, (void *)hook_nickimpl};

static void write_absjmp(uint8_t *at, void *target) {
    at[0] = 0xFF; at[1] = 0x25; *(int32_t *)(at + 2) = 0; *(void **)(at + 6) = target;
}

static void *alloc_near(uint8_t *anchor, size_t size) {
    uintptr_t base = (uintptr_t)anchor & ~0xFFFFULL;
    for (uintptr_t a = base - 0x10000; a > base - 0x7FF00000ULL; a -= 0x10000) {
        void *p = VirtualAlloc((void *)a, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    for (uintptr_t a = base + 0x10000; a < base + 0x7FF00000ULL; a += 0x10000) {
        void *p = VirtualAlloc((void *)a, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

static void install(int i) {
    uint8_t *addr = g_base + g_rva[i];
    if (memcmp(addr, g_expect[i], g_explen[i]) != 0) return;
    int n = g_steal[i];
    uint8_t *tr = (uint8_t *)alloc_near(addr, n + 14);
    memcpy(tr, addr, n);
    if (g_ripfix[i] >= 0) *(int32_t *)(tr + g_ripfix[i]) += (int32_t)(addr - tr);
    write_absjmp(tr + n, addr + n);
    g_tramp[i] = (fn_t)tr;
    DWORD old;
    VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old);
    write_absjmp(addr, g_hooks[i]);
    for (int k = 14; k < n; k++) addr[k] = 0x90;
    VirtualProtect(addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
}

static DWORD WINAPI init_thread(LPVOID u) {
    (void)u;
    g_base = (uint8_t *)GetModuleHandleA(NULL);
    for (int i = 0; i < NCAND; i++) install(i);
    return 0;
}

__declspec(dllexport) void ChameleonInit(void) {}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        ensure_steam_appid();
        ensure_auth_ini();
        CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
    }
    return TRUE;
}
