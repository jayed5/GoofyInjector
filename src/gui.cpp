#include "framework.h"
#include "resource_ids.h"
#include "i18n.h"
#include "injector.h"
#include "settings.h"
#include "gui.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <thread>
#include <memory>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")

namespace gi {

namespace {

constexpr int WM_APP_INJECT_DONE = WM_APP + 1;

HWND g_main = nullptr;
HWND g_list = nullptr;
HWND g_log = nullptr;
HWND g_status = nullptr;
HWND g_method = nullptr;
HWND g_exportEdit = nullptr;
HFONT g_font = nullptr;
HBRUSH g_bgBrush = nullptr;
HBRUSH g_groupBrush = nullptr;

std::unique_ptr<std::thread> g_worker;
DWORD g_selectedPid = 0;

void AppendLog(const std::wstring& msg) {
    if (!g_log) return;
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    std::wstring line = (len ? L"\r\n" : L"") + msg;
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

void SetStatus(const std::wstring& msg) {
    if (g_status) SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)msg.c_str());
}

void BuildMenu() {
    HMENU menu = CreateMenu();

    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_FILE_EXIT, Tr("menu.file.exit"));

    HMENU lang = CreatePopupMenu();
    AppendMenuW(lang, MF_STRING, ID_LANG_EN, Tr("menu.lang.en"));
    AppendMenuW(lang, MF_STRING, ID_LANG_PL, Tr("menu.lang.pl"));

    HMENU method = CreatePopupMenu();
    AppendMenuW(method, MF_STRING, ID_METHOD_EXPORT, Tr("method.exportcall"));
    AppendMenuW(method, MF_STRING, ID_METHOD_RT, Tr("method.remotethread"));
    AppendMenuW(method, MF_STRING, ID_METHOD_APC, Tr("method.apc"));
    AppendMenuW(method, MF_STRING, ID_METHOD_HIJACK, Tr("method.threadhijack"));
    AppendMenuW(method, MF_STRING, ID_METHOD_NT, Tr("method.ntcreatethread"));

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, Tr("menu.help.about"));

    AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, Tr("menu.file"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)lang, Tr("menu.lang"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)method, Tr("menu.method"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)help, Tr("menu.help"));

    // Sync checkmarks with the current selection.
    int sel = (int)SendMessageW(g_method, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) sel = 0;
    CheckMenuItem(method, ID_METHOD_EXPORT, sel == 0 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(method, ID_METHOD_RT,     sel == 1 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(method, ID_METHOD_APC,    sel == 2 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(method, ID_METHOD_HIJACK, sel == 3 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(method, ID_METHOD_NT,     sel == 4 ? MF_CHECKED : MF_UNCHECKED);

    SetMenu(g_main, menu);
}

// Fills the method combobox with the (translated) method names.
void FillMethodCombo(int sel) {
    SendMessageW(g_method, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_method, CB_ADDSTRING, 0, (LPARAM)Tr("method.exportcall"));
    SendMessageW(g_method, CB_ADDSTRING, 0, (LPARAM)Tr("method.remotethread"));
    SendMessageW(g_method, CB_ADDSTRING, 0, (LPARAM)Tr("method.apc"));
    SendMessageW(g_method, CB_ADDSTRING, 0, (LPARAM)Tr("method.threadhijack"));
    SendMessageW(g_method, CB_ADDSTRING, 0, (LPARAM)Tr("method.ntcreatethread"));
    SendMessageW(g_method, CB_SETCURSEL, sel < 0 || sel > 4 ? 0 : sel, 0);
}

// Export name only matters for the ExportCall method.
void UpdateMethodUi() {
    int sel = (int)SendMessageW(g_method, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) sel = 0;
    BOOL exportNeeded = (sel == 0);
    EnableWindow(g_exportEdit, exportNeeded);
    EnableWindow(GetDlgItem(g_main, IDC_EXPORTLBL), exportNeeded);
    if (exportNeeded) {
        SendMessageW(g_exportEdit, EM_SETCUEBANNER, TRUE,
                     (LPARAM)L"SendMemoryFileToDiscord");
    }
    BuildMenu();
    SettingsSet(L"method", std::to_wstring(sel));
}

void ApplyLanguage() {
    SetWindowTextW(g_main, Tr("app.title"));
    BuildMenu();
    int curSel = (int)SendMessageW(g_method, CB_GETCURSEL, 0, 0);
    FillMethodCombo(curSel == CB_ERR ? 0 : curSel);
    SetDlgItemTextW(g_main, IDC_FILTERME, Tr("label.filter"));
    SetDlgItemTextW(g_main, IDC_REFRESH, Tr("btn.refresh"));
    SetDlgItemTextW(g_main, IDC_BROWSE, Tr("btn.browse"));
    SetDlgItemTextW(g_main, IDC_INJECT, Tr("btn.inject"));
    SetDlgItemTextW(g_main, IDC_CLEARLOG, Tr("btn.clear"));
    SetDlgItemTextW(g_main, IDC_COPYLOG, Tr("btn.copy"));
    SetDlgItemTextW(g_main, IDC_EXPORTLBL, Tr("msg.exportlbl"));
    SetDlgItemTextW(g_main, IDC_METHODLBL, Tr("label.method"));
    SetStatus(Tr("label.status.ready"));
    InvalidateRect(g_main, nullptr, TRUE);
}

void RefreshProcessList() {
    bool filter = IsDlgButtonChecked(g_main, IDC_FILTERME) == BST_CHECKED;
    wchar_t search[64]{};
    GetDlgItemTextW(g_main, IDC_SEARCH, search, 64);
    std::wstring needle(search);
    std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);

    auto procs = ListProcesses(filter);

    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_selectedPid = 0;

    int shown = 0;
    for (const auto& p : procs) {
        std::wstring lower = p.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        if (!needle.empty() && lower.find(needle) == std::wstring::npos) continue;

        std::wstring item = p.name + L"  (PID: " + std::to_wstring(p.pid) + L")";
        if (!p.currentUser) item += L"  *";
        int idx = (int)SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)item.c_str());
        SendMessageW(g_list, LB_SETITEMDATA, idx, p.pid);
        ++shown;
    }

    if (shown == 0) SetStatus(Tr("msg.noprocesses"));
}

std::wstring BrowseForDll() {
    wchar_t file[MAX_PATH]{};
    // GetOpenFileName needs \0-separated pairs, not "a|b|c" — convert.
    std::wstring filter(Tr("msg.filterprog"));
    std::replace(filter.begin(), filter.end(), L'|', L'\0');
    filter.push_back(L'\0');

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) return file;
    return L"";
}

void RunInjectionAsync() {
    if (g_worker) return; // one injection at a time

    if (g_selectedPid == 0) { AppendLog(Tr("msg.selectproc")); return; }

    wchar_t dll[MAX_PATH]{};
    GetDlgItemTextW(g_main, IDC_DLLPATH, dll, MAX_PATH);
    if (!dll[0]) { AppendLog(Tr("msg.selectdll")); return; }

    wchar_t exp[128]{};
    GetDlgItemTextW(g_main, IDC_EXPORT, exp, 128);
    std::wstring exportName = exp[0] ? exp : L"SendMemoryFileToDiscord";

    EnableWindow(GetDlgItem(g_main, IDC_INJECT), FALSE);
    SetStatus(Tr("msg.injecting"));

    int sel = (int)SendMessageW(g_method, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) sel = 0;
    InjectMethod method = sel == 1 ? InjectMethod::RemoteThread
                        : sel == 2 ? InjectMethod::QueueUserAPC
                        : sel == 3 ? InjectMethod::ThreadHijack
                        : sel == 4 ? InjectMethod::NtCreateThreadEx
                                   : InjectMethod::ExportCall;

    DWORD pid = g_selectedPid;
    std::wstring path(dll);
    g_worker = std::make_unique<std::thread>([pid, method, path, exportName]() {
        std::wstring err = Inject(pid, method, path, exportName);
        if (err.empty()) {
            if (method == InjectMethod::QueueUserAPC) err = Tr("msg.apcnote");
            else if (method == InjectMethod::ThreadHijack) err = Tr("msg.hijacknote");
        }
        PostMessageW(g_main, WM_APP_INJECT_DONE, 0,
                     (LPARAM)new std::wstring(err));
    });
}

void OnInjectDone(std::wstring* errPtr) {
    std::unique_ptr<std::wstring> err(errPtr);
    if (g_worker && g_worker->joinable()) g_worker->join();
    g_worker.reset();
    EnableWindow(GetDlgItem(g_main, IDC_INJECT), TRUE);

    if (err->empty()) {
        AppendLog(Tr("msg.done"));
        SetStatus(Tr("msg.done"));
        MessageBeep(MB_OK);
    } else {
        AppendLog(*err);
        SetStatus(*err);
        MessageBeep(MB_ICONERROR);
    }
}

void CopyLogToClipboard(HWND hwnd) {
    int len = GetWindowTextLengthW(g_log);
    std::wstring buf(len + 1, L'\0');
    GetWindowTextW(g_log, buf.data(), len + 1);
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
    if (hg) {
        memcpy(GlobalLock(hg), buf.c_str(), (len + 1) * sizeof(wchar_t));
        GlobalUnlock(hg);
        SetClipboardData(CF_UNICODETEXT, hg);
    }
    CloseClipboard();
    AppendLog(Tr("msg.clipcopied"));
}

void ShowAbout() {
    MessageBoxW(g_main, Tr("dlg.about.text"), Tr("dlg.about"),
                MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_REFRESH:  RefreshProcessList(); return 0;
        case IDC_FILTERME: RefreshProcessList(); return 0;
        case IDC_BROWSE: {
            std::wstring path = BrowseForDll();
            if (!path.empty()) SetDlgItemTextW(g_main, IDC_DLLPATH, path.c_str());
            return 0;
        }
        case IDC_INJECT:   RunInjectionAsync(); return 0;
        case IDC_CLEARLOG: SetWindowTextW(g_log, L""); return 0;
        case IDC_COPYLOG:  CopyLogToClipboard(hwnd); return 0;
        case ID_FILE_EXIT: DestroyWindow(hwnd); return 0;
        case ID_HELP_ABOUT: ShowAbout(); return 0;
        case ID_LANG_EN:
        case ID_LANG_PL: {
            SetLanguage(LOWORD(wp) == ID_LANG_EN ? Lang::EN : Lang::PL);
            SettingsSet(L"language", LanguageName(GetLanguage()));
            ApplyLanguage();
            return 0;
        }
        case ID_METHOD_EXPORT:
        case ID_METHOD_RT:
        case ID_METHOD_APC:
        case ID_METHOD_HIJACK:
        case ID_METHOD_NT: {
            int m = LOWORD(wp) == ID_METHOD_EXPORT ? 0
                  : LOWORD(wp) == ID_METHOD_RT     ? 1
                  : LOWORD(wp) == ID_METHOD_APC    ? 2
                  : LOWORD(wp) == ID_METHOD_HIJACK ? 3 : 4;
            SendMessageW(g_method, CB_SETCURSEL, m, 0);
            UpdateMethodUi();
            return 0;
        }
        default:
            if (HIWORD(wp) == LBN_SELCHANGE && LOWORD(wp) == IDC_PROCLIST) {
                int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR)
                    g_selectedPid = (DWORD)SendMessageW(g_list, LB_GETITEMDATA, sel, 0);
                return 0;
            }
            if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) == IDC_SEARCH) {
                RefreshProcessList();
                return 0;
            }
            if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == IDC_METHOD) {
                UpdateMethodUi();
                return 0;
            }
            break;
        }
        break;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(20, 20, 20));
        SetBkColor(hdc, RGB(250, 250, 250));
        return (LRESULT)g_bgBrush;
    }

    case WM_APP_INJECT_DONE:
        OnInjectDone((std::wstring*)lp);
        return 0;

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(drop, 0, path, MAX_PATH))
            SetDlgItemTextW(g_main, IDC_DLLPATH, path);
        DragFinish(drop);
        return 0;
    }

    case WM_SIZE:
        SendMessageW(g_status, WM_SIZE, 0, 0);
        return 0;

    case WM_DESTROY:
        if (g_worker && g_worker->joinable()) g_worker->detach();
        // Save settings (DLL path, export name, filter, language).
        {
            wchar_t buf[MAX_PATH]{};
            GetDlgItemTextW(g_main, IDC_DLLPATH, buf, MAX_PATH);
            SettingsSet(L"dllpath", buf);
            wchar_t exp[128]{};
            GetDlgItemTextW(g_main, IDC_EXPORT, exp, 128);
            SettingsSet(L"export", exp);
            SettingsSet(L"onlymy", IsDlgButtonChecked(g_main, IDC_FILTERME) == BST_CHECKED
                                       ? L"1" : L"0");
            int msel = (int)SendMessageW(g_method, CB_GETCURSEL, 0, 0);
            SettingsSet(L"method", std::to_wstring(msel == CB_ERR ? 0 : msel));
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int RunGui(HINSTANCE hInstance) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Load saved language before any window text is created.
    SetLanguage(LanguageFromName(SettingsGet(L"language", L"english")));

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GoofyInjectorMain";
    RegisterClassW(&wc);

    g_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_bgBrush = CreateSolidBrush(RGB(250, 250, 250));
    g_groupBrush = CreateSolidBrush(RGB(240, 240, 245));

    g_main = CreateWindowExW(0, wc.lpszClassName, Tr("app.title"),
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 560, 520,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_main) return 1;

    auto MakeButton = [&](int id, const wchar_t* text, int x, int y, int w, int h,
                          DWORD style = BS_PUSHBUTTON) {
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style,
                        x, y, w, h, g_main, (HMENU)(INT_PTR)id, hInstance, nullptr);
    };
    auto MakeEdit = [&](int id, const wchar_t* text, int x, int y, int w, int h,
                        DWORD style = ES_AUTOHSCROLL) {
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                        WS_CHILD | WS_VISIBLE | style,
                        x, y, w, h, g_main, (HMENU)(INT_PTR)id, hInstance, nullptr);
    };

    MakeButton(IDC_FILTERME, Tr("label.filter"), 10, 10, 160, 20, BS_AUTOCHECKBOX);
    MakeEdit(IDC_SEARCH, L"", 320, 10, 100, 24);
    MakeButton(IDC_REFRESH, Tr("btn.refresh"), 430, 10, 90, 24);

    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                             10, 40, 520, 145, g_main,
                             (HMENU)(INT_PTR)IDC_PROCLIST, hInstance, nullptr);

    MakeEdit(IDC_DLLPATH, SettingsGet(L"dllpath", L"").c_str(), 10, 192, 400, 24);
    MakeButton(IDC_BROWSE, Tr("btn.browse"), 420, 192, 100, 24);

    // Injection method selector.
    CreateWindowExW(0, L"STATIC", Tr("label.method"), WS_CHILD | WS_VISIBLE,
                    10, 224, 250, 16, g_main,
                    (HMENU)(INT_PTR)IDC_METHODLBL, hInstance, nullptr);
    g_method = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", nullptr,
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                               WS_VSCROLL | WS_TABSTOP,
                               10, 242, 250, 120, g_main,
                               (HMENU)(INT_PTR)IDC_METHOD, hInstance, nullptr);
    FillMethodCombo(_wtoi(SettingsGet(L"method", L"0").c_str()));

    CreateWindowExW(0, L"STATIC", Tr("msg.exportlbl"), WS_CHILD | WS_VISIBLE,
                    275, 224, 250, 16, g_main,
                    (HMENU)(INT_PTR)IDC_EXPORTLBL, hInstance, nullptr);
    g_exportEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                    SettingsGet(L"export", L"").c_str(),
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    275, 242, 150, 24, g_main,
                    (HMENU)(INT_PTR)IDC_EXPORT, hInstance, nullptr);
    SendMessageW(g_exportEdit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"SendMemoryFileToDiscord");
    MakeButton(IDC_INJECT, Tr("btn.inject"), 420, 242, 100, 28, BS_DEFPUSHBUTTON);

    g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                            WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                            10, 286, 520, 120, g_main,
                            (HMENU)(INT_PTR)IDC_LOG, hInstance, nullptr);
    MakeButton(IDC_CLEARLOG, Tr("btn.clear"), 10, 412, 80, 24);
    MakeButton(IDC_COPYLOG, Tr("btn.copy"), 100, 412, 80, 24);

    g_status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
                               WS_CHILD | WS_VISIBLE,
                               0, 0, 0, 0, g_main,
                               (HMENU)(INT_PTR)IDC_STATUSBAR, hInstance, nullptr);
    // Size the status bar right away (it only auto-sizes on WM_SIZE).
    SendMessageW(g_status, WM_SIZE, 0, 0);

    CheckDlgButton(g_main, IDC_FILTERME,
                   SettingsGet(L"onlymy", L"1") == L"1" ? BST_CHECKED : BST_UNCHECKED);

    // Sync the export-name field state with the restored method.
    UpdateMethodUi();

    EnumChildWindows(g_main, [](HWND child, LPARAM) -> BOOL {
        SendMessageW(child, WM_SETFONT, (WPARAM)g_font, TRUE);
        return TRUE;
    }, 0);

    ShowWindow(g_main, SW_SHOW);
    UpdateWindow(g_main);
    RefreshProcessList();
    AppendLog(Tr("label.status.ready"));
    if (!IsElevated()) AppendLog(Tr("msg.admin"));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

} // namespace gi
