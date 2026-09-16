#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace gi {

enum class InjectMethod {
    ExportCall = 0,    // load DLL locally, call its export with a handle
    RemoteThread = 1,  // classic: CreateRemoteThread + LoadLibraryW
    QueueUserAPC = 2,  // LoadLibraryW queued as APC on an existing thread
    ThreadHijack = 3,  // hijack a target thread's RIP to run LoadLibraryW
    NtCreateThreadEx = 4, // undocumented ntdll thread creation (stealthier)
};

struct ProcInfo {
    DWORD pid = 0;
    std::wstring name;
    bool currentUser = false;
};

// Lists running processes (skips system PIDs <= 4).
// When onlyCurrentUser is true, keeps only processes owned by the current user.
std::vector<ProcInfo> ListProcesses(bool onlyCurrentUser);

// Opens the process with the rights needed by the remote export call.
HANDLE OpenTargetProcess(DWORD pid);

// True when the current process token is elevated (administrator).
bool IsElevated();

// Performs the injection using the chosen method. Returns an error message
// (empty on success). exportName is only used by ExportCall.
std::wstring Inject(DWORD pid, InjectMethod method,
                    const std::wstring& dllPath,
                    const std::wstring& exportName);

} // namespace gi
