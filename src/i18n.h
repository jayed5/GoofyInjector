#pragma once
#include <string>

namespace gi {

enum class Lang { EN, PL };

// Returns the translated string for the given key in the active language.
// Unknown keys return the key itself.
const wchar_t* Tr(const char* key);
void SetLanguage(Lang lang);
Lang GetLanguage();
Lang LanguageFromName(const std::wstring& name); // "english" / "polski"
const wchar_t* LanguageName(Lang lang);

} // namespace gi
