#include "wait_gate_patch.h"

#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

namespace preyhfr {
namespace {

// idSessionLocal::Frame: test com_fixedTic, issue the GUI/event-loop command,
// then compare com_ticNumber with the selected minimum tic. Immediates and the
// object-relative store displacement are validated as image relationships.
constexpr std::array<std::uint8_t, 47> kWaitGatePattern{
    0xa1, 0, 0, 0, 0,
    0x39, 0x58, 0x24,
    0x74, 0x02,
    0x8b, 0xf9,
    0x8b, 0x0d, 0, 0, 0, 0,
    0x8b, 0x11,
    0x8b, 0x42, 0x20,
    0x6a, 0x01,
    0x68, 0, 0, 0, 0,
    0xff, 0xd0,
    0xa1, 0, 0, 0, 0,
    0x3b, 0xc7,
    0x89, 0x86, 0, 0, 0, 0,
    0x7d, 0x1a,
};
constexpr std::array<bool, kWaitGatePattern.size()> kWaitGateMask{
    true, false, false, false, false,
    true, true, true,
    true, true,
    true, true,
    true, true, false, false, false, false,
    true, true,
    true, true, true,
    true, true,
    true, false, false, false, false,
    true, true,
    true, false, false, false, false,
    true, true,
    true, true, false, false, false, false,
    true, true,
};
constexpr std::size_t kPatchOffset = 8;
constexpr std::size_t kEventLoopImmediateOffset = 14;
constexpr std::size_t kWaitLabelImmediateOffset = 26;
constexpr std::size_t kComTicImmediateOffset = 33;
constexpr std::array<std::uint8_t, 2> kOriginalWaitBytes{0x74, 0x02};
constexpr std::array<std::uint8_t, 2> kPatchedWaitBytes{0x90, 0x90};

struct MainImage {
    std::uint8_t* base = nullptr;
    std::uint32_t imageSize = 0;
    std::uint32_t textRva = 0;
    std::uint32_t textSize = 0;
};

std::optional<MainImage> GetMainImage() {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (base == nullptr) return std::nullopt;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return std::nullopt;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return std::nullopt;
    }
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const auto& section = sections[index];
        if (std::memcmp(section.Name, ".text", 5) == 0) {
            const auto textSize = section.Misc.VirtualSize != 0
                ? section.Misc.VirtualSize
                : section.SizeOfRawData;
            if (textSize == 0 ||
                section.VirtualAddress >= nt->OptionalHeader.SizeOfImage ||
                textSize > nt->OptionalHeader.SizeOfImage -
                    section.VirtualAddress) {
                return std::nullopt;
            }
            return MainImage{base, nt->OptionalHeader.SizeOfImage,
                             section.VirtualAddress, textSize};
        }
    }
    return std::nullopt;
}

bool MatchesAt(const std::vector<std::uint8_t>& bytes, std::size_t offset,
               bool patched) {
    for (std::size_t index = 0; index < kWaitGatePattern.size(); ++index) {
        std::uint8_t expected = kWaitGatePattern[index];
        if (patched && index == kPatchOffset) expected = kPatchedWaitBytes[0];
        if (patched && index == kPatchOffset + 1) expected = kPatchedWaitBytes[1];
        if (kWaitGateMask[index] && bytes[offset + index] != expected) {
            return false;
        }
    }
    return true;
}

std::vector<std::size_t> FindMatches(const std::vector<std::uint8_t>& bytes,
                                     bool patched) {
    std::vector<std::size_t> matches;
    if (bytes.size() < kWaitGatePattern.size()) return matches;
    for (std::size_t offset = 0;
         offset <= bytes.size() - kWaitGatePattern.size(); ++offset) {
        if (MatchesAt(bytes, offset, patched)) matches.push_back(offset);
    }
    return matches;
}

bool WritePatch(std::uint8_t* address,
                const std::array<std::uint8_t, 2>& bytes) {
    DWORD oldProtection = 0;
    if (!VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE,
                        &oldProtection)) {
        return false;
    }
    std::memcpy(address, bytes.data(), bytes.size());
    FlushInstructionCache(GetCurrentProcess(), address, bytes.size());
    DWORD ignored = 0;
    const bool protectionRestored =
        VirtualProtect(address, bytes.size(), oldProtection, &ignored) != FALSE;
    return protectionRestored &&
           std::memcmp(address, bytes.data(), bytes.size()) == 0;
}

} // namespace

bool WaitGatePatch::WaitAndApply(DWORD timeoutMilliseconds,
                                 WaitGateResult& result,
                                 std::string& error) {
    const auto image = GetMainImage();
    if (!image) {
        error = "could not parse the running executable image";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeoutMilliseconds);
    do {
        std::vector<std::uint8_t> text(image->textSize);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               image->base + image->textRva,
                               text.data(), text.size(), &bytesRead) ||
            bytesRead != text.size()) {
            error = "could not read the running executable text section";
            return false;
        }

        auto matches = FindMatches(text, false);
        bool alreadyPatched = false;
        if (matches.empty()) {
            matches = FindMatches(text, true);
            alreadyPatched = matches.size() == 1;
        }
        if (matches.size() == 1) {
            const std::size_t offset = matches.front();
            if (offset + kComTicImmediateOffset + sizeof(std::uint32_t) >
                    text.size() ||
                text[offset + kComTicImmediateOffset - 1] != 0xa1) {
                error = "timing signature failed structural validation";
                return false;
            }
            std::uint32_t fixedTicAddress = 0;
            std::uint32_t eventLoopAddress = 0;
            std::uint32_t waitLabelAddress = 0;
            std::uint32_t comTicAddress = 0;
            std::memcpy(&fixedTicAddress, text.data() + offset + 1,
                        sizeof(fixedTicAddress));
            std::memcpy(&eventLoopAddress,
                        text.data() + offset + kEventLoopImmediateOffset,
                        sizeof(eventLoopAddress));
            std::memcpy(&waitLabelAddress,
                        text.data() + offset + kWaitLabelImmediateOffset,
                        sizeof(waitLabelAddress));
            std::memcpy(&comTicAddress,
                        text.data() + offset + kComTicImmediateOffset,
                        sizeof(comTicAddress));
            const auto imageBegin = reinterpret_cast<std::uintptr_t>(image->base);
            const auto imageEnd = imageBegin + image->imageSize;
            if (fixedTicAddress < imageBegin || fixedTicAddress >= imageEnd ||
                eventLoopAddress < imageBegin || eventLoopAddress >= imageEnd ||
                waitLabelAddress < imageBegin || waitLabelAddress >= imageEnd ||
                comTicAddress < imageBegin || comTicAddress >= imageEnd) {
                error = "timing signature referenced data outside the executable";
                return false;
            }

            auto* waitGate = image->base + image->textRva + offset;
            auto* patchAddress = waitGate + kPatchOffset;
            if (!alreadyPatched && !WritePatch(patchAddress, kPatchedWaitBytes)) {
                error = "could not write or verify the render-wait patch";
                return false;
            }
            patchAddress_ = patchAddress;
            ownsPatch_ = !alreadyPatched;
            result = WaitGateResult{
                reinterpret_cast<std::uintptr_t>(waitGate),
                comTicAddress,
                fixedTicAddress,
                eventLoopAddress,
                waitLabelAddress,
                alreadyPatched,
            };
            return true;
        }
        if (matches.size() > 1) {
            error = "timing signature was ambiguous";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    } while (std::chrono::steady_clock::now() < deadline);

    error = "timing signature did not appear before the initialization timeout";
    return false;
}

bool WaitGatePatch::Restore() {
    if (!ownsPatch_) return true;
    if (patchAddress_ == nullptr ||
        std::memcmp(patchAddress_, kPatchedWaitBytes.data(),
                    kPatchedWaitBytes.size()) != 0) {
        return false;
    }
    if (!WritePatch(patchAddress_, kOriginalWaitBytes)) return false;
    ownsPatch_ = false;
    patchAddress_ = nullptr;
    return true;
}

} // namespace preyhfr
