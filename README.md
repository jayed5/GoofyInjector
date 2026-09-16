# 🃏 Goofy Injector 2.0

**Goofy Injector** is a Windows utility for injecting DLLs into running
processes — with a modern GUI, **5 injection methods**, built-in English/Polish
language switching and persistent settings. It is a complete rewrite of the
original console-only tool.

> ⚠️ **Disclaimer:** DLL injection is a powerful low-level operation. Use it
> only with software you own or are explicitly allowed to modify (your own
> plugins, debugging, reverse-engineering practice). The authors are not
> responsible for misuse.

---

## ✨ Features

- 🖥️ **Native Win32 GUI** — process list with live search, "only my processes"
  filter, injection log with copy/clear, status bar, drag & drop for DLL files.
- 💉 **5 injection methods** (see below).
- 🌍 **Language settings** — English (default) and Polski, switchable from the
  menu, persisted in `GoofyInjector.ini`.
- ⚙️ **Persistent settings** — DLL path, export name, injection method and
  filter are restored on launch.
- 🧵 **Non-blocking injection** — the UI stays responsive; results are
  reported in the log and status bar with sound feedback.
- ⌨️ **CLI mode** for scripting and headless use.
- 🛡️ **Clear error reporting** — Windows error codes, NTSTATUS codes and an
  administrator-privilege hint when `OpenProcess` fails with `ACCESS_DENIED`.

## 💉 Injection methods

| # | Method | How it works | Notes |
|---|--------|--------------|-------|
| 1 | **Export call (handle)** | The DLL is loaded into the *injector* itself; the chosen export (`void f(HANDLE)`) is called with a handle to the target process | Your DLL manages target memory itself (e.g. via `ReadProcessMemory`). Requires an exported function. |
| 2 | **Classic (CreateRemoteThread)** | The DLL path is written into the target's memory (`VirtualAllocEx` + `WriteProcessMemory`), then `CreateRemoteThread` runs `LoadLibraryW` inside the target — your `DllMain` executes there | Easiest to detect (anti-cheats watch for it) |
| 3 | **QueueUserAPC** | `LoadLibraryW` is queued as an APC on an existing thread of the target; it fires when that thread enters an alertable wait | Stealthier; needs a waiting thread |
| 4 | **Thread hijack (risky)** | A target thread is suspended, its instruction pointer is redirected to `LoadLibraryW`, then resumed | ⚠️ The thread does **not** return to its original code — may crash the target |
| 5 | **NtCreateThreadEx (stealth)** | Undocumented `ntdll` API creates the remote thread directly, bypassing some user-mode wrappers | Stealthier variant of #2; NTSTATUS codes shown on failure |

Methods 2–5 need the DLL to have a standard `DllMain`. Method 1 needs an
exported function (default name `SendMemoryFileToDiscord`, configurable in the
GUI). Methods 2–5 usually require **administrator rights** for protected or
elevated processes.

## 📦 Download / Build

Grab a ready `GoofyInjector.exe` from
[Releases](../../releases) — it is statically linked, no runtime DLLs needed.

### Build from source

**Visual Studio 2022:** open `GoofyInjector.sln`, pick *Debug/Release × x64*, press **F7**.

**CMake (MSVC or MinGW):**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Plain MinGW (single command):**

```bash
windres src/version.rc -O coff -o build/version.res
g++ -std=c++20 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ \
    -o bin/GoofyInjector.exe src/*.cpp build/version.res \
    -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -lshlwapi -lwtsapi32 -ladvapi32 -lntdll
```

Requirements: Windows 10+, C++20 compiler.

## 🚀 Usage

### GUI

1. Run `GoofyInjector.exe` (right-click → *Run as administrator* for protected processes).
2. Find the target process — type in the **Search** box to filter, uncheck
   **Only my processes** to see all users' processes.
3. Choose the DLL — browse or drag & drop the file onto the window.
4. Pick the **Injection method** (combobox or *Method* menu).
5. For method 1 set the **Export name** (the field is disabled for other methods).
6. Click **Inject**. Results appear in the log at the bottom.

### Command line

The exe is a GUI-subsystem app, but CLI flags work — when run from a terminal
it attaches to the console automatically.

```text
GoofyInjector.exe                                        # GUI
GoofyInjector.exe --lang polski                          # GUI in Polish
GoofyInjector.exe --inject 1234 C:\tools\cool.dll        # by PID, export call
GoofyInjector.exe --inject notepad.exe C:\tools\cool.dll # by name
GoofyInjector.exe --inject 1234 C:\tools\cool.dll MyExport
GoofyInjector.exe --console                              # classic console flow
```

### Settings file

`GoofyInjector.ini` (created next to the exe):

```ini
[settings]
language=english
dllpath=C:\tools\cool.dll
export=SendMemoryFileToDiscord
method=1
onlymy=1
```

## 🗂️ Project layout

```
src/
├── main.cpp         entry point (wWinMain), CLI parsing, console mode
├── gui.cpp/.h       Win32 GUI (process list, search, methods, log, menus)
├── injector.cpp/.h  process listing + 5 injection methods
├── i18n.cpp/.h      EN/PL translation tables
├── settings.cpp/.h  INI-backed persistent settings
├── cli.cpp/.h       pid/name parsing for the CLI
├── framework.h      common includes
├── resource_ids.h   control / menu IDs
└── version.rc       version info resource
```

## 📄 License

MIT — see [LICENSE](LICENSE).
