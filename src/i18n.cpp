#include "i18n.h"

#include <cstring>

namespace gi {

namespace {

Lang g_lang = Lang::EN; // default language: English

struct Entry {
    const char* key;
    const wchar_t* en;
    const wchar_t* pl;
};

// Single table keeps translations next to each other so adding a language
// or a string is one row.
const Entry kTable[] = {
    { "app.title",        L"Goofy Injector",                     L"Goofy Injector" },
    { "menu.file",        L"&File",                              L"&Plik" },
    { "menu.file.exit",   L"E&xit",                              L"&Zako\u0144cz" },
    { "menu.lang",        L"&Language",                          L"&J\u0119zyk" },
    { "menu.lang.en",     L"&English",                           L"&English" },
    { "menu.lang.pl",     L"&Polski",                            L"&Polski" },
    { "menu.help",        L"&Help",                              L"&Pomoc" },
    { "menu.help.about",  L"&About",                             L"&O programie" },
    { "menu.settings",    L"&Settings",                          L"&Ustawienia" },

    { "btn.refresh",      L"Refresh",                            L"Od\u015Bwie\u017C" },
    { "btn.inject",       L"Inject",                             L"Wstrzyknij" },
    { "btn.browse",       L"Browse...",                          L"Przegl\u0105daj..." },
    { "btn.clear",        L"Clear log",                          L"Wyczy\u015B\u0107 log" },
    { "btn.copy",         L"Copy",                               L"Kopiuj" },
    { "btn.retry",        L"Retry",                              L"Pon\u00F3w" },

    { "label.processes",  L"Processes",                          L"Procesy" },
    { "label.search",     L"Search:",                            L"Szukaj:" },
    { "label.filter",     L"Only my processes",                  L"Tylko moje procesy" },
    { "label.dll",        L"DLL path:",                          L"\u015Acie\u017Cka DLL:" },
    { "label.log",        L"Log",                                L"Dziennik" },
    { "label.status.ready", L"Ready.",                           L"Gotowy." },

    { "msg.selectproc",   L"Select a process first.",            L"Najpierw wybierz proces." },
    { "msg.selectdll",    L"Choose a DLL file first.",           L"Najpierw wybierz plik DLL." },
    { "msg.notfound",     L"Export not found in DLL: {}",        L"Nie znaleziono eksportu w DLL: {}" },
    { "msg.loadfail",     L"Failed to load DLL (error {})",      L"Nie uda\u0142o si\u0119 za\u0142adowa\u0107 DLL (b\u0142\u0105d {})" },
    { "msg.openfail",     L"Failed to open process (error {})",  L"Nie uda\u0142o si\u0119 otworzy\u0107 procesu (b\u0142\u0105d {})" },
    { "msg.admin",        L"Run as administrator may be required.", L"Mo\u017Cna by\u0107 wymagane uruchomienie jako administrator." },
    { "msg.injecting",    L"Calling export in target process...", L"Wywo\u0142uj\u0119 eksport w procesie docelowym..." },
    { "msg.done",         L"Done.",                              L"Zako\u0144czono." },
    { "msg.noprocesses",  L"No processes to show.",              L"Brak proces\u00F3w do wy\u015Bwietlenia." },
    { "msg.filterprog",   L"DLL files (*.dll)|*.dll|All files (*.*)|*.*", L"Pliki DLL (*.dll)|*.dll|Wszystkie pliki (*.*)|*.*" },
    { "msg.pathtoolarge", L"Path too long.",                     L"\u015Acie\u017Cka jest za d\u0142uga." },
    { "msg.clipcopied",   L"Copied to clipboard.",               L"Skopiowano do schowka." },
    { "msg.exportlbl",    L"Export name:",                       L"Nazwa eksportu:" },
    { "menu.method",      L"&Method",                            L"&Metoda" },
    { "label.method",     L"Injection method:",                  L"Metoda wstrzykiwania:" },
    { "method.exportcall", L"Export call (handle)",              L"Wywołanie eksportu (uchwyt)" },
    { "method.remotethread", L"Classic (CreateRemoteThread)",    L"Klasyczna (CreateRemoteThread)" },
    { "method.apc",       L"QueueUserAPC",                       L"QueueUserAPC" },
    { "method.threadhijack", L"Thread hijack (risky)",            L"Hijack w\u0105tku (ryzykowne)" },
    { "method.ntcreatethread", L"NtCreateThreadEx (stealth)",     L"NtCreateThreadEx (stealth)" },
    { "msg.hijacknote",   L"Thread hijacked; original RIP restored via shellcode. May crash the target.", L"W\u0105tek przej\u0119ty; oryginalny RIP przywr\u00F3cony shellcodem. Mo\u017Ce crashowa\u0107 proces." },
    { "msg.hijackfail",   L"Thread hijack failed (error {})",    L"Hijack w\u0105tku nie powi\u00F3d\u0142 si\u0119 (b\u0142\u0105d {})" },
    { "msg.ntfail",       L"NtCreateThreadEx failed (NTSTATUS 0x{:X}", L"NtCreateThreadEx nie powi\u00F3d\u0142 si\u0119 (NTSTATUS 0x{:X}" },
    { "msg.vmallocfail",  L"VirtualAllocEx failed (error {})",   L"VirtualAllocEx nie powiod\u0142o si\u0119 (b\u0142\u0105d {})" },
    { "msg.vmwritefail",  L"WriteProcessMemory failed (error {})", L"WriteProcessMemory nie powiod\u0142o si\u0119 (b\u0142\u0105d {})" },
    { "msg.threadfail",   L"CreateRemoteThread failed (error {})", L"CreateRemoteThread nie powiod\u0142o si\u0119 (b\u0142\u0105d {})" },
    { "msg.apcfail",      L"QueueUserAPC failed (error {})",     L"QueueUserAPC nie powiod\u0142o si\u0119 (b\u0142\u0105d {})" },
    { "msg.apcnote",      L"APC fires when a target thread enters alertable wait.", L"APC wykona si\u0119, gdy w\u0105tek ofiary wejdzie w alertable wait." },
    { "msg.notid",        L"No thread found for the target process.", L"Nie znaleziono w\u0105tku procesu docelowego." },

    { "dlg.about",        L"About Goofy Injector",               L"O programie Goofy Injector" },
    { "dlg.about.text",   L"Goofy Injector\nSimple DLL export caller.\nVersion 2.0", L"Goofy Injector\nProste wywo\u0142ywanie eksport\u00F3w DLL.\nWersja 2.0" },
    { "dlg.settings",     L"Settings",                           L"Ustawienia" },
    { "dlg.langchange",   L"Language changed.",                  L"Zmieniono j\u0119zyk." },
};

} // namespace

const wchar_t* Tr(const char* key) {
    for (const auto& e : kTable) {
        if (std::string(e.key) == key) {
            return g_lang == Lang::PL ? e.pl : e.en;
        }
    }
    static std::wstring fallback;
    fallback.assign(key, key + strlen(key));
    return fallback.c_str();
}

void SetLanguage(Lang lang) { g_lang = lang; }
Lang GetLanguage() { return g_lang; }

Lang LanguageFromName(const std::wstring& name) {
    return name == L"polski" ? Lang::PL : Lang::EN;
}

const wchar_t* LanguageName(Lang lang) {
    return lang == Lang::PL ? L"polski" : L"english";
}

} // namespace gi
