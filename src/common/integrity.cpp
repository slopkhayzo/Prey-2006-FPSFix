#include "integrity.h"

#include <algorithm>
#include <vector>

namespace preyhfr {

std::optional<std::filesystem::path> ModulePath(HMODULE module) {
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return std::nullopt;
        if (length < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), length));
        }
        if (buffer.size() >= 32'768) return std::nullopt;
        buffer.resize((std::min)(static_cast<std::size_t>(32'768),
                                 buffer.size() * 2));
    }
}

} // namespace preyhfr
