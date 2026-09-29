#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace preyhfr {

inline constexpr const char* kSupportedExeSha256 =
    "cea6d424fbb8e2ffbf307a5bee509b45c2d35242f70be31387224db2a0eadd69";
inline constexpr const char* kSupportedGameDllSha256 =
    "74d436d376ba144762a28c940d0243135b4f9db8fdd7ee597b9cb5e4277b43c6";

std::optional<std::filesystem::path> ModulePath(HMODULE module);
std::optional<std::string> Sha256File(const std::filesystem::path& path);

} // namespace preyhfr
