#include "settings.h"

#include <windows.h>
#include <shlwapi.h>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")

namespace gi {

namespace {

std::wstring IniPath() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    PathRemoveExtensionW(exe);
    return std::wstring(exe) + L".ini";
}

} // namespace

std::wstring SettingsGet(const std::wstring& key, const std::wstring& fallback) {
    wchar_t buf[512]{};
    GetPrivateProfileStringW(L"settings", key.c_str(), fallback.c_str(),
                             buf, (DWORD)(sizeof(buf) / sizeof(wchar_t)),
                             IniPath().c_str());
    return buf;
}

void SettingsSet(const std::wstring& key, const std::wstring& value) {
    WritePrivateProfileStringW(L"settings", key.c_str(), value.c_str(),
                               IniPath().c_str());
}

} // namespace gi
