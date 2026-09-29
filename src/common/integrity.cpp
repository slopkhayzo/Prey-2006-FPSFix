#include "integrity.h"

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
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

std::optional<std::string> Sha256File(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const auto length = file.tellg();
    if (length <= 0 ||
        static_cast<unsigned long long>(length) >
            static_cast<unsigned long long>(MAXDWORD)) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) return std::nullopt;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD returned = 0;
    std::vector<std::uint8_t> object;
    std::array<std::uint8_t, 32> digest{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectLength),
                          sizeof(objectLength), &returned, 0) < 0) {
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }
    object.resize(objectLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
                         nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()),
                         0) < 0) {
        if (hash != nullptr) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        result << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return result.str();
}

} // namespace preyhfr
