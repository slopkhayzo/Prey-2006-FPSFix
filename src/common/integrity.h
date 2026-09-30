#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>

namespace preyhfr {

std::optional<std::filesystem::path> ModulePath(HMODULE module);

} // namespace preyhfr
