#include "injector.h"
#include "i18n.h"

#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <format>

#pragma comment(lib, "Advapi32.lib")

namespace gi {

namespace {

template <typename... Args>
std::wstring Fmt(const char* key, Args&&... args) {
    return std::vformat(Tr(key), std::make_wformat_args(args...));
}

bool SameUserSid(HANDLE hTokenA, HANDLE hTokenB) {
    DWORD sizeA = 0, sizeB = 0;
    GetTokenInformation(hTokenA, TokenUser, nullptr, 0, &sizeA);
    GetTokenInformation(hTokenB, TokenUser, nullptr, 0, &sizeB);
    if (sizeA == 0 || sizeB == 0) return false;

    std::vector<BYTE> bufA(sizeA), bufB(sizeB);
    if (!GetTokenInformation(hTokenA, TokenUser, bufA.data(), sizeA, &sizeA)) return false;
    if (!GetTokenInformation(hTokenB, TokenUser, bufB.data(), sizeB, &sizeB)) return false;

    PSID sidA = ((TOKEN_USER*)bufA.data())->User.Sid;
    PSID sidB = ((TOKEN_USER*)bufB.data())->User.Sid;
    return EqualSid(sidA, sidB) == TRUE;
}

bool IsProcessOwnedByCurrentUser(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    HANDLE hToken = nullptr;
    bool result = false;
    if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        HANDLE hCurrent = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hCurrent)) {
            result = SameUserSid(hToken, hCurrent);
            CloseHandle(hCurrent);
        }
        CloseHandle(hToken);
    }
    CloseHandle(hProcess);
    return result;
}

// --- Method 1: load DLL locally and call its export with a process handle ---

std::wstring InjectViaExport(DWORD pid, const std::wstring& dllPath,
                             const std::wstring& exportName) {
    // FIX (1.0): the original converted the path with CP_ACP, breaking
    // non-ASCII paths. The DLL is loaded with the wide-char API now.
    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        return Fmt("msg.loadfail", (unsigned long long)GetLastError());
    }

    std::string exportA(exportName.begin(), exportName.end()); // exports are ASCII
    FARPROC fn = GetProcAddress(hDll, exportA.c_str());
    if (!fn) {
        std::wstring msg = Fmt("msg.notfound", exportName);
        FreeLibrary(hDll);
        return msg;
    }

    HANDLE hProcess = OpenTargetProcess(pid);
    if (!hProcess) {
        unsigned long long err = GetLastError();
        std::wstring msg = Fmt("msg.openfail", err);
        if (err == ERROR_ACCESS_DENIED) msg += L" " + std::wstring(Tr("msg.admin"));
        FreeLibrary(hDll);
        return msg;
    }

    using ExportFn = void(__stdcall*)(HANDLE);
    reinterpret_cast<ExportFn>(fn)(hProcess);

    CloseHandle(hProcess);
    FreeLibrary(hDll);
    return L"";
}

// --- Shared helpers for the remote methods ---

HANDLE OpenForInjection(DWORD pid) {
    return OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                           PROCESS_VM_READ,
                       FALSE, pid);
}

// Writes the DLL path into the target's memory; returns the remote address
// or nullptr on failure (error message in out).
LPVOID WritePathToTarget(HANDLE hProcess, const std::wstring& dllPath,
                         std::wstring& out) {
    SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProcess, nullptr, bytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        out = Fmt("msg.vmallocfail", (unsigned long long)GetLastError());
        return nullptr;
    }
    if (!WriteProcessMemory(hProcess, remote, dllPath.c_str(), bytes, nullptr)) {
        out = Fmt("msg.vmwritefail", (unsigned long long)GetLastError());
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return nullptr;
    }
    return remote;
}

// --- Method 2: classic CreateRemoteThread + LoadLibraryW ---

std::wstring InjectViaRemoteThread(HANDLE hProcess, const std::wstring& dllPath) {
    std::wstring err;
    LPVOID remote = WritePathToTarget(hProcess, dllPath, err);
    if (!remote) return err;

    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                     "LoadLibraryW");
    if (!loadLib) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Fmt("msg.threadfail", (unsigned long long)GetLastError());
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
                                        (LPTHREAD_START_ROUTINE)loadLib,
                                        remote, 0, nullptr);
    if (!hThread) {
        unsigned long long e = GetLastError();
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Fmt("msg.threadfail", e);
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
    return L"";
}

// --- Method 4: thread hijack — suspend a thread, set RIP to LoadLibraryW ---

std::wstring InjectViaHijack(HANDLE hProcess, DWORD pid, const std::wstring& dllPath) {
    std::wstring err;
    LPVOID remote = WritePathToTarget(hProcess, dllPath, err);
    if (!remote) return err;

    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                     "LoadLibraryW");
    if (!loadLib) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Fmt("msg.hijackfail", (unsigned long long)GetLastError());
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Tr("msg.notid");
    }

    bool ok = false;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE hThread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
            if (!hThread) continue;

            if (SuspendThread(hThread) != (DWORD)-1) {
                CONTEXT ctx{};
                ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                if (GetThreadContext(hThread, &ctx)) {
#ifdef _WIN64
                    // Trampoline shellcode: LoadLibraryW(path); restores RAX.
                    // Simplest robust approach: run LoadLibraryW directly as
                    // the thread's new RIP, then resume. The thread will not
                    // return to its original code (killed), so we hijack only
                    // as a last resort and warn the user.
                    ctx.Rip = (DWORD64)loadLib;
                    ctx.Rcx = (DWORD64)remote; // x64 calling convention arg 1
                    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                    ok = SetThreadContext(hThread, &ctx) != 0;
#else
                    ctx.Eip = (DWORD)loadLib;
                    ctx.Eax = (DWORD)remote;
                    ok = SetThreadContext(hThread, &ctx) != 0;
#endif
                }
                ResumeThread(hThread);
            }
            CloseHandle(hThread);
            if (ok) break;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    if (!ok) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Fmt("msg.hijackfail", (unsigned long long)GetLastError());
    }
    return L"";
}

// --- Method 5: NtCreateThreadEx (undocumented, stealthier) ---

using NtCreateThreadExFn = NTSTATUS(NTAPI*)(
    PHANDLE threadHandle, ACCESS_MASK desiredAccess, PVOID objectAttributes,
    HANDLE processHandle, LPTHREAD_START_ROUTINE startRoutine, PVOID argument,
    ULONG createFlags, SIZE_T zeroBits, SIZE_T stackCommit,
    SIZE_T stackReserve, PVOID attributeList);

std::wstring InjectViaNtCreateThreadEx(HANDLE hProcess, const std::wstring& dllPath) {
    std::wstring err;
    LPVOID remote = WritePathToTarget(hProcess, dllPath, err);
    if (!remote) return err;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto ntCreate = (NtCreateThreadExFn)(void*)GetProcAddress(ntdll,
                                                             "NtCreateThreadEx");
    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                     "LoadLibraryW");
    if (!ntCreate || !loadLib) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Tr("msg.notid");
    }

    HANDLE hThread = nullptr;
    NTSTATUS status = ntCreate(&hThread, THREAD_ALL_ACCESS, nullptr, hProcess,
                               (LPTHREAD_START_ROUTINE)loadLib, remote,
                               0 /* not suspended */, 0, 0, 0, nullptr);
    if (status != 0 || !hThread) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Fmt("msg.ntfail", (unsigned long long)status);
    }
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
    return L"";
}

// --- Method 3: QueueUserAPC + LoadLibraryW on an existing thread ---

std::wstring InjectViaAPC(HANDLE hProcess, DWORD pid, const std::wstring& dllPath) {
    std::wstring err;
    LPVOID remote = WritePathToTarget(hProcess, dllPath, err);
    if (!remote) return err;

    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                     "LoadLibraryW");
    if (!loadLib) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Fmt("msg.apcfail", (unsigned long long)GetLastError());
    }

    // Pick any (preferably alertable-friendly) thread of the target.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Tr("msg.notid");
    }

    bool queued = false;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE hThread = OpenThread(THREAD_SET_CONTEXT, FALSE,
                                        te.th32ThreadID);
            if (!hThread) continue;
            DWORD e = 0;
            if (QueueUserAPC((PAPCFUNC)loadLib, hThread, (ULONG_PTR)remote))
                queued = true;
            else
                e = GetLastError();
            CloseHandle(hThread);
            if (queued) break;
            (void)e;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    if (!queued) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return Fmt("msg.apcfail", (unsigned long long)GetLastError());
    }
    return L"";
}

} // namespace

std::vector<ProcInfo> ListProcesses(bool onlyCurrentUser) {
    std::vector<ProcInfo> procs;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return procs;

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (pe.th32ProcessID <= 4) continue;
            bool owned = IsProcessOwnedByCurrentUser(pe.th32ProcessID);
            if (onlyCurrentUser && !owned) continue;
            procs.push_back({ pe.th32ProcessID, pe.szExeFile, owned });
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    std::sort(procs.begin(), procs.end(),
              [](const ProcInfo& a, const ProcInfo& b) {
                  return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
              });
    return procs;
}

HANDLE OpenTargetProcess(DWORD pid) {
    return OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                       FALSE, pid);
}

bool IsElevated() {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elev{};
        DWORD size = sizeof(elev);
        if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size)) {
            elevated = elev.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return elevated != FALSE;
}

std::wstring Inject(DWORD pid, InjectMethod method,
                    const std::wstring& dllPath,
                    const std::wstring& exportName) {
    if (method == InjectMethod::ExportCall)
        return InjectViaExport(pid, dllPath, exportName);

    HANDLE hProcess = OpenForInjection(pid);
    if (!hProcess) {
        unsigned long long err = GetLastError();
        std::wstring msg = Fmt("msg.openfail", err);
        if (err == ERROR_ACCESS_DENIED) msg += L" " + std::wstring(Tr("msg.admin"));
        return msg;
    }

    std::wstring result;
    switch (method) {
    case InjectMethod::RemoteThread:   result = InjectViaRemoteThread(hProcess, dllPath); break;
    case InjectMethod::QueueUserAPC:   result = InjectViaAPC(hProcess, pid, dllPath); break;
    case InjectMethod::ThreadHijack:   result = InjectViaHijack(hProcess, pid, dllPath); break;
    case InjectMethod::NtCreateThreadEx: result = InjectViaNtCreateThreadEx(hProcess, dllPath); break;
    default: break;
    }

    CloseHandle(hProcess);
    return result;
}

} // namespace gi
