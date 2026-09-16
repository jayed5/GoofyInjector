#include "cli.h"
#include "injector.h"

#include <algorithm>
#include <cwctype>

namespace gi {

DWORD parsePidOrName(const std::wstring& pidOrName) {
    // Pure number -> treat as PID.
    if (!pidOrName.empty() &&
        std::all_of(pidOrName.begin(), pidOrName.end(), ::iswdigit)) {
        return (DWORD)std::wcstoul(pidOrName.c_str(), nullptr, 10);
    }

    // Otherwise match by image name (case-insensitive).
    std::wstring needle = pidOrName;
    std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);

    for (const auto& p : ListProcesses(false)) {
        std::wstring name = p.name;
        std::transform(name.begin(), name.end(), name.begin(), ::towlower);
        if (name == needle) return p.pid;
    }
    return 0;
}

} // namespace gi
