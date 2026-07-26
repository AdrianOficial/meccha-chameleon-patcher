#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

static int inject_into(HANDLE proc, const char *dll_abspath) {
    size_t len = strlen(dll_abspath) + 1;
    void *remote = VirtualAllocEx(proc, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { printf("VirtualAllocEx failed: %lu\n", GetLastError()); return 1; }
    if (!WriteProcessMemory(proc, remote, dll_abspath, len, NULL)) {
        printf("WriteProcessMemory failed: %lu\n", GetLastError()); return 1;
    }
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    FARPROC load = GetProcAddress(k32, "LoadLibraryA");
    HANDLE th = CreateRemoteThread(proc, NULL, 0, (LPTHREAD_START_ROUTINE)load, remote, 0, NULL);
    if (!th) { printf("CreateRemoteThread failed: %lu\n", GetLastError()); return 1; }
    WaitForSingleObject(th, INFINITE);
    DWORD code = 0; GetExitCodeThread(th, &code);
    printf("LoadLibraryA remote returned module=0x%lX\n", code);
    CloseHandle(th);
    return code ? 0 : 2;
}

static DWORD find_pid(const char *exename) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { .dwSize = sizeof(pe) };
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, exename) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage:\n  inject.exe launch \"<game exe>\" \"<dll>\"\n"
               "  inject.exe attach \"<exe name>\" \"<dll>\"\n");
        return 1;
    }
    char dll[MAX_PATH];
    GetFullPathNameA(argv[3] ? argv[3] : "chameleon_hook.dll", MAX_PATH, dll, NULL);
    printf("DLL: %s\n", dll);

    if (strcmp(argv[1], "launch") == 0) {
        char exepath[MAX_PATH];
        GetFullPathNameA(argv[2], MAX_PATH, exepath, NULL);

        char cwd[MAX_PATH]; strcpy(cwd, exepath);
        char *slash = strrchr(cwd, '\\'); if (slash) *slash = 0;

        STARTUPINFOA si = { .cb = sizeof(si) };
        PROCESS_INFORMATION pi = {0};
        if (!CreateProcessA(exepath, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, cwd, &si, &pi)) {
            printf("CreateProcess failed: %lu\n", GetLastError()); return 1;
        }
        printf("launched suspended pid=%lu, injecting...\n", pi.dwProcessId);
        int rc = inject_into(pi.hProcess, dll);
        ResumeThread(pi.hThread);
        printf("resumed. rc=%d\n", rc);
        return rc;
    } else if (strcmp(argv[1], "attach") == 0) {
        DWORD pid = find_pid(argv[2]);
        if (!pid) { printf("process '%s' not found\n", argv[2]); return 1; }
        printf("found pid=%lu, injecting...\n", pid);
        HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!proc) { printf("OpenProcess failed: %lu\n", GetLastError()); return 1; }
        int rc = inject_into(proc, dll);
        CloseHandle(proc);
        return rc;
    }
    printf("unknown mode '%s'\n", argv[1]);
    return 1;
}
