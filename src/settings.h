#pragma once
#include <string>

namespace gi {

// Reads/writes simple key=value strings from an INI next to the exe.
std::wstring SettingsGet(const std::wstring& key, const std::wstring& fallback);
void SettingsSet(const std::wstring& key, const std::wstring& value);

} // namespace gi
