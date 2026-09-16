#include "framework.h"
#include "i18n.h"
#include "injector.h"
#include "settings.h"
#include "cli.h"
#include "gui.h"

#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <cwctype>
#include <cwchar>

namespace gi {

namespace {

// GUI-subsystem apps have no stdout; attach to the parent console (when
// launched from a terminal) or allocate one, and redirect the streams.
// When stdout is already redirected to a pipe/file, keep it untouched.
void EnsureConsole() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD ft = out != INVALID_HANDLE_VALUE ? GetFileType(out) : FILE_TYPE_UNKNOWN;
    if (out != nullptr && out != INVALID_HANDLE_VALUE &&
        (ft == FILE_TYPE_PIPE || ft == FILE_TYPE_DISK))
        return; // output is redirected; don't hijack it

    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !GetConsoleWindow())
        AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
    std::wcout.clear();
}

void PrintUsage() {
    std::wcout
        << L"Goofy Injector 2.0\n\n"
        << L"Usage:\n"
        << L"  GoofyInjector.exe                     start GUI (default)\n"
        << L"  GoofyInjector.exe --console           start console mode\n"
        << L"  GoofyInjector.exe --lang english|polski\n"
        << L"  GoofyInjector.exe --inject <pid|name> <dll> [export]\n"
        << L"                                        inject without GUI\n\n"
        << L"Examples:\n"
        << L"  GoofyInjector.exe --inject 1234 C:\\tools\\cool.dll\n"
        << L"  GoofyInjector.exe --inject notepad.exe C:\\tools\\cool.dll SendMemoryFileToDiscord\n";
}

int RunConsole(const std::wstring& langOverride) {
    if (!langOverride.empty())
        SetLanguage(LanguageFromName(langOverride));

    auto processes = ListProcesses(false);
    if (processes.empty()) {
        std::wcout << Tr("msg.noprocesses") << std::endl;
        return 1;
    }

    std::wcout << Tr("label.processes") << L":\n";
    for (size_t i = 0; i < processes.size(); ++i) {
        std::wcout << i + 1 << L". " << processes[i].name
                   << L" (PID: " << processes[i].pid << L")\n";
    }

    std::wcout << L"> ";
    std::wstring input;
    std::getline(std::wcin, input);
    if (input.empty()) return 1;

    DWORD pid = 0;
    try {
        size_t pos = 0;
        int idx = std::stoi(input, &pos);
        if (pos == input.size() && idx >= 1 && idx <= (int)processes.size())
            pid = processes[idx - 1].pid;
    } catch (...) {}

    if (pid == 0) {
        std::wstring needle = input;
        std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);
        for (const auto& p : processes) {
            std::wstring name = p.name;
            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
            if (name == needle) { pid = p.pid; break; }
        }
    }

    if (pid == 0) {
        std::wcout << Tr("msg.selectproc") << std::endl;
        return 1;
    }

    std::wcout << Tr("label.dll") << L" ";
    std::wstring dllPath;
    std::getline(std::wcin, dllPath);
    if (dllPath.empty()) return 1;

    std::wstring err = Inject(pid, InjectMethod::ExportCall,
                              dllPath, L"SendMemoryFileToDiscord");
    if (err.empty()) {
        std::wcout << Tr("msg.done") << std::endl;
        return 0;
    }
    std::wcout << err << std::endl;
    return 1;
}

} // namespace

int AppMain(int argc, wchar_t** argv) {
    std::wstring langOverride;
    std::wstring injectTarget, injectDll, injectExport;
    bool console = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--help" || a == L"-h") {
            EnsureConsole();
            PrintUsage();
            LocalFree(argv);
            return 0;
        } else if (a == L"--console") {
            console = true;
        } else if (a == L"--lang" && i + 1 < argc) {
            langOverride = argv[++i];
        } else if (a == L"--inject" && i + 2 < argc) {
            injectTarget = argv[++i];
            injectDll = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != L'-') injectExport = argv[++i];
        }
    }
    LocalFree(argv);

    if (console || !injectTarget.empty()) EnsureConsole();

    if (!langOverride.empty())
        SetLanguage(LanguageFromName(langOverride));

    // FIX: original wmain printed Polish text on a std::cout line ("Zakoczono"
    // typo, mojibake) and mixed narrow/wide output. All output is wide now.
    if (!injectTarget.empty()) {
        DWORD pid = parsePidOrName(injectTarget);
        if (pid == 0) {
            std::wcerr << Tr("msg.selectproc") << std::endl;
            return 1;
        }
        std::wstring err = Inject(
            pid, InjectMethod::ExportCall, injectDll,
            injectExport.empty() ? L"SendMemoryFileToDiscord" : injectExport);
        if (!err.empty()) {
            std::wcerr << err << std::endl;
            return 1;
        }
        std::wcout << Tr("msg.done") << std::endl;
        return 0;
    }

    if (console) return RunConsole(langOverride);
    return RunGui(GetModuleHandleW(nullptr));
}

} // namespace gi

// GUI subsystem entry point (the app also supports CLI flags; in that case
// a console is attached on demand in AppMain).
int wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    return gi::AppMain(argc, argv);
}
