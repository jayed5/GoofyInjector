#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <wtsapi32.h>
#include <algorithm>
#pragma comment(lib, "Wtsapi32.lib")

typedef void(__stdcall* SendMemoryFileFunc)(HANDLE);

struct ProcInfo {
    DWORD pid;
    std::wstring name;
};

bool IsProcessOwnedByCurrentUser(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        CloseHandle(hProcess);
        return false;
    }

    DWORD size = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(hToken);
        CloseHandle(hProcess);
        return false;
    }

    std::vector<BYTE> buffer(size);
    if (!GetTokenInformation(hToken, TokenUser, buffer.data(), size, &size)) {
        CloseHandle(hToken);
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hToken);
    CloseHandle(hProcess);

    PSID processSid = ((TOKEN_USER*)buffer.data())->User.Sid;

    HANDLE hCurrentToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hCurrentToken)) return false;

    size = 0;
    GetTokenInformation(hCurrentToken, TokenUser, NULL, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(hCurrentToken);
        return false;
    }

    std::vector<BYTE> bufferCurrent(size);
    if (!GetTokenInformation(hCurrentToken, TokenUser, bufferCurrent.data(), size, &size)) {
        CloseHandle(hCurrentToken);
        return false;
    }

    CloseHandle(hCurrentToken);

    PSID currentSid = ((TOKEN_USER*)bufferCurrent.data())->User.Sid;

    return EqualSid(processSid, currentSid) == TRUE;
}

std::vector<ProcInfo> ListProcesses(bool onlyUserOwned) {
    std::vector<ProcInfo> procs;
    PROCESSENTRY32 procEntry;
    procEntry.dwSize = sizeof(procEntry);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return procs;

    if (Process32First(snapshot, &procEntry)) {
        do {
            if (procEntry.th32ProcessID <= 4) continue;
            if (onlyUserOwned) {
                if (!IsProcessOwnedByCurrentUser(procEntry.th32ProcessID)) continue;
            }
            procs.push_back({ procEntry.th32ProcessID, procEntry.szExeFile });
        } while (Process32Next(snapshot, &procEntry));
    }

    CloseHandle(snapshot);
    return procs;
}

int wmain() {
    std::wcout << L"Wyswietlic tylko procesy uzytkownika? (t/n): ";
    wchar_t c;
    std::wcin >> c;
    std::wcin.ignore();

    bool onlyUser = (c == L't' || c == L'T');

    auto processes = ListProcesses(onlyUser);
    if (processes.empty()) {
        std::wcout << L"Brak procesow do wyswietlenia." << std::endl;
        return 1;
    }

    std::wcout << L"Dostepne procesy:\n";
    for (size_t i = 0; i < processes.size(); i++) {
        std::wcout << std::setw(3) << i + 1 << L". " << processes[i].name << L" (PID: " << processes[i].pid << L")\n";
    }

    std::wcout << L"Podaj numer lub nazwe procesu: ";
    std::wstring input;
    std::getline(std::wcin, input);

    DWORD pid = 0;
   
    try {
        size_t pos = 0;
        int idx = std::stoi(input, &pos);
        if (pos == input.size() && idx >= 1 && idx <= (int)processes.size()) {
            pid = processes[idx - 1].pid;
        }
    }
    catch (...) {}

    if (pid == 0) {
        std::wstring inputLower = input;
        std::transform(inputLower.begin(), inputLower.end(), inputLower.begin(), ::towlower);

        for (const auto& p : processes) {
            std::wstring nameLower = p.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
            if (nameLower == inputLower) {
                pid = p.pid;
                break;
            }
        }
    }

    if (pid == 0) {
        std::wcout << L"Nie znaleziono procesu o podanym numerze lub nazwie." << std::endl;
        return 1;
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::cout << "Nie mozna otworzyc procesu." << std::endl;
        return 1;
    }

    std::wcout << L"Podaj pelna sciezke do DLL: ";
    std::wstring dllPath;
    std::getline(std::wcin, dllPath);

    int len = WideCharToMultiByte(CP_ACP, 0, dllPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string dllPathA(len, 0);
    WideCharToMultiByte(CP_ACP, 0, dllPath.c_str(), -1, &dllPathA[0], len, nullptr, nullptr);

    HMODULE hDll = LoadLibraryA(dllPathA.c_str());
    if (!hDll) {
        std::cout << "Nie mozna zaladowac DLL." << std::endl;
        CloseHandle(hProcess);
        return 1;
    }

    SendMemoryFileFunc sendFunc = (SendMemoryFileFunc)GetProcAddress(hDll, "SendMemoryFileToDiscord");
    if (!sendFunc) {
        std::cout << "Nie znaleziono funkcji w DLL." << std::endl;
        FreeLibrary(hDll);
        CloseHandle(hProcess);
        return 1;
    }

    std::cout << "Wywolywanie funkcji SendMemoryFileToDiscord..." << std::endl;
    sendFunc(hProcess);

    FreeLibrary(hDll);
    CloseHandle(hProcess);
    std::cout << "Zakoczono." << std::endl;
    return 0;
}
