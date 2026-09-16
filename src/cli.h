#pragma once
#include <windows.h>
#include <string>

namespace gi {

// Accepts a decimal PID ("1234") or a process name ("notepad.exe") and
// returns the matching PID, or 0 when not found.
DWORD parsePidOrName(const std::wstring& pidOrName);

} // namespace gi
