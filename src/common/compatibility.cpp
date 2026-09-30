#include "compatibility.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace preyhfr {
namespace {

std::optional<std::vector<std::uint8_t>> ReadFileBytes(
    const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const auto length = file.tellg();
    if (length <= 0 || static_cast<unsigned long long>(length) > MAXDWORD) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) return std::nullopt;
    return bytes;
}

bool EqualAsciiInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

class PeFile {
public:
    explicit PeFile(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    bool Parse(std::string& error) {
        if (bytes_.size() < sizeof(IMAGE_DOS_HEADER)) {
            error = "file is too small for a DOS header";
            return false;
        }
        dos_ = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes_.data());
        if (dos_->e_magic != IMAGE_DOS_SIGNATURE || dos_->e_lfanew <= 0) {
            error = "DOS header is invalid";
            return false;
        }
        const auto ntOffset = static_cast<std::size_t>(dos_->e_lfanew);
        if (ntOffset > bytes_.size() ||
            sizeof(IMAGE_NT_HEADERS32) > bytes_.size() - ntOffset) {
            error = "PE header lies outside the file";
            return false;
        }
        nt_ = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes_.data() + ntOffset);
        if (nt_->Signature != IMAGE_NT_SIGNATURE ||
            nt_->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt_->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            error = "image is not an x86 PE32 file";
            return false;
        }
        if (nt_->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
            nt_->OptionalHeader.SizeOfImage == 0) {
            error = "PE32 optional header is incomplete";
            return false;
        }
        const auto sectionOffset = ntOffset + sizeof(DWORD) +
            sizeof(IMAGE_FILE_HEADER) + nt_->FileHeader.SizeOfOptionalHeader;
        const auto sectionBytes =
            static_cast<std::size_t>(nt_->FileHeader.NumberOfSections) *
            sizeof(IMAGE_SECTION_HEADER);
        if (sectionOffset > bytes_.size() ||
            sectionBytes > bytes_.size() - sectionOffset) {
            error = "PE section table lies outside the file";
            return false;
        }
        sections_ = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
            bytes_.data() + sectionOffset);
        for (WORD index = 0; index < nt_->FileHeader.NumberOfSections; ++index) {
            const auto& section = sections_[index];
            const std::uint64_t virtualEnd =
                static_cast<std::uint64_t>(section.VirtualAddress) +
                (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
            const std::uint64_t rawEnd =
                static_cast<std::uint64_t>(section.PointerToRawData) +
                section.SizeOfRawData;
            if (virtualEnd > nt_->OptionalHeader.SizeOfImage ||
                rawEnd > bytes_.size()) {
                error = "PE section range is invalid";
                return false;
            }
        }
        return true;
    }

    bool isDll() const {
        return (nt_->FileHeader.Characteristics & IMAGE_FILE_DLL) != 0;
    }

    const IMAGE_NT_HEADERS32& nt() const { return *nt_; }

    const IMAGE_SECTION_HEADER* SectionFor(std::uint32_t rva,
                                           std::size_t size) const {
        const std::uint64_t end = static_cast<std::uint64_t>(rva) + size;
        if (end < rva || end > nt_->OptionalHeader.SizeOfImage) return nullptr;
        for (WORD index = 0; index < nt_->FileHeader.NumberOfSections; ++index) {
            const auto& section = sections_[index];
            const std::uint64_t begin = section.VirtualAddress;
            const std::uint64_t sectionEnd = begin +
                (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
            if (rva >= begin && end <= sectionEnd) return &section;
        }
        return nullptr;
    }

    const std::uint8_t* BytesAt(std::uint32_t rva, std::size_t size) const {
        const auto* section = SectionFor(rva, size);
        if (section == nullptr || rva < section->VirtualAddress) return nullptr;
        const auto delta = rva - section->VirtualAddress;
        if (delta > section->SizeOfRawData ||
            size > section->SizeOfRawData - delta) {
            return nullptr;
        }
        return bytes_.data() + section->PointerToRawData + delta;
    }

    std::optional<std::string_view> StringAt(std::uint32_t rva) const {
        const auto* section = SectionFor(rva, 1);
        if (section == nullptr || rva < section->VirtualAddress) {
            return std::nullopt;
        }
        const auto delta = rva - section->VirtualAddress;
        if (delta >= section->SizeOfRawData) return std::nullopt;
        const auto available = section->SizeOfRawData - delta;
        const auto* text = reinterpret_cast<const char*>(
            bytes_.data() + section->PointerToRawData + delta);
        const void* terminator = std::memchr(text, '\0', available);
        if (terminator == nullptr) return std::nullopt;
        return std::string_view(
            text, static_cast<const char*>(terminator) - text);
    }

    bool HasImport(std::string_view moduleName,
                   std::string_view functionName) const {
        const auto& directory = nt_->OptionalHeader.DataDirectory[
            IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0 ||
            directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            return false;
        }
        const std::size_t maximumDescriptors =
            directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        for (std::size_t descriptorIndex = 0;
             descriptorIndex < maximumDescriptors; ++descriptorIndex) {
            const auto descriptorRva = directory.VirtualAddress +
                static_cast<std::uint32_t>(descriptorIndex *
                                            sizeof(IMAGE_IMPORT_DESCRIPTOR));
            const auto* descriptor = reinterpret_cast<
                const IMAGE_IMPORT_DESCRIPTOR*>(
                    BytesAt(descriptorRva, sizeof(IMAGE_IMPORT_DESCRIPTOR)));
            if (descriptor == nullptr || descriptor->Name == 0) break;
            const auto observedModule = StringAt(descriptor->Name);
            if (!observedModule ||
                !EqualAsciiInsensitive(*observedModule, moduleName)) {
                continue;
            }
            const std::uint32_t thunkRva = descriptor->OriginalFirstThunk != 0
                ? descriptor->OriginalFirstThunk
                : descriptor->FirstThunk;
            for (std::size_t thunkIndex = 0; thunkIndex < 4096; ++thunkIndex) {
                const auto entryRva = thunkRva + static_cast<std::uint32_t>(
                    thunkIndex * sizeof(IMAGE_THUNK_DATA32));
                const auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
                    BytesAt(entryRva, sizeof(IMAGE_THUNK_DATA32)));
                if (thunk == nullptr || thunk->u1.AddressOfData == 0) break;
                if (IMAGE_SNAP_BY_ORDINAL32(thunk->u1.Ordinal)) continue;
                const auto importedName = StringAt(
                    thunk->u1.AddressOfData +
                    static_cast<std::uint32_t>(offsetof(IMAGE_IMPORT_BY_NAME,
                                                        Name)));
                if (importedName && *importedName == functionName) return true;
            }
            return false;
        }
        return false;
    }

    bool HasExport(std::string_view functionName) const {
        const auto& directory = nt_->OptionalHeader.DataDirectory[
            IMAGE_DIRECTORY_ENTRY_EXPORT];
        const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
            BytesAt(directory.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY)));
        if (exports == nullptr || exports->AddressOfNames == 0) return false;
        for (DWORD index = 0; index < exports->NumberOfNames; ++index) {
            const auto* nameRva = reinterpret_cast<const std::uint32_t*>(BytesAt(
                exports->AddressOfNames + index * sizeof(std::uint32_t),
                sizeof(std::uint32_t)));
            if (nameRva == nullptr) return false;
            const auto name = StringAt(*nameRva);
            if (name && *name == functionName) return true;
        }
        return false;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    const IMAGE_DOS_HEADER* dos_ = nullptr;
    const IMAGE_NT_HEADERS32* nt_ = nullptr;
    const IMAGE_SECTION_HEADER* sections_ = nullptr;
};

bool RequireRange(const PeFile& pe, std::uint32_t rva, std::size_t size,
                  DWORD requiredCharacteristics, const char* description,
                  std::string& error) {
    const auto* section = pe.SectionFor(rva, size);
    if (section == nullptr ||
        (section->Characteristics & requiredCharacteristics) !=
            requiredCharacteristics) {
        error = std::string(description) +
            " is absent from the expected PE section type";
        return false;
    }
    return true;
}

template <std::size_t Size>
bool RequireBytes(const PeFile& pe, std::uint32_t rva,
                  const std::array<std::uint8_t, Size>& expected,
                  const char* description, std::string& error) {
    const auto* observed = pe.BytesAt(rva, expected.size());
    if (observed == nullptr ||
        std::memcmp(observed, expected.data(), expected.size()) != 0) {
        error = std::string(description) + " signature does not match";
        return false;
    }
    return true;
}

bool RequireImport(const PeFile& pe, const char* moduleName,
                   const char* functionName, std::string& error) {
    if (pe.HasImport(moduleName, functionName)) return true;
    error = std::string("required import ") + moduleName + "!" +
        functionName + " is absent";
    return false;
}

bool ValidateCommonImage(const PeFile& pe, bool expectDll,
                         std::string& error) {
    if (pe.isDll() != expectDll) {
        error = expectDll ? "image is not a DLL" : "image is not an executable";
        return false;
    }
    const WORD characteristics = pe.nt().FileHeader.Characteristics;
    if ((characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) == 0 ||
        (characteristics & IMAGE_FILE_32BIT_MACHINE) == 0) {
        error = "image does not have the required executable x86 flags";
        return false;
    }
    return true;
}

} // namespace

bool ValidateExecutableLayout(const std::filesystem::path& path,
                              const CompatibilityRequirements& requirements,
                              std::string& error) {
    const auto bytes = ReadFileBytes(path);
    if (!bytes) {
        error = "could not read prey.exe";
        return false;
    }
    PeFile pe(*bytes);
    if (!pe.Parse(error) || !ValidateCommonImage(pe, false, error)) return false;

    constexpr DWORD executableCode =
        IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    constexpr DWORD readableData = IMAGE_SCN_MEM_READ;
    constexpr DWORD writableData = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    if (!RequireRange(pe, static_cast<std::uint32_t>(kRunGameTicRvaBegin),
                      kRunGameTicRvaEnd - kRunGameTicRvaBegin, executableCode,
                      "RunGameTic caller range", error) ||
        !RequireImport(pe, "GDI32.dll", "SwapBuffers", error)) {
        return false;
    }
    if (requirements.displayOverrides &&
        (!RequireRange(pe, static_cast<std::uint32_t>(kCvarSystemVtableRva),
                       9 * sizeof(std::uint32_t), readableData,
                       "idCVarSystem vtable", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kSetCVarStringRva), 1,
                       executableCode, "SetCVarString", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kSetCVarBoolRva), 1,
                       executableCode, "SetCVarBool", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kSetCVarIntegerRva), 1,
                       executableCode, "SetCVarInteger", error))) {
        return false;
    }
    if (requirements.mouseHooks &&
        (!RequireRange(pe, static_cast<std::uint32_t>(kMouseMoveRva),
                       kMouseMoveInstructionSize, executableCode,
                       "MouseMove hook", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kUsercmdTicCmdRva), 1,
                       executableCode, "TicCmd hook", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kUsercmdInterruptRva), 1,
                       executableCode, "UsercmdInterrupt hook", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kGetDirectUsercmdRva), 1,
                       executableCode, "GetDirectUsercmd hook", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kSensitivityCvarPointerRva),
                       sizeof(std::uint32_t), writableData,
                       "sensitivity cvar pointer", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kPitchCvarPointerRva),
                       sizeof(std::uint32_t), writableData,
                       "pitch cvar pointer", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kYawCvarPointerRva),
                       sizeof(std::uint32_t), writableData,
                       "yaw cvar pointer", error) ||
         !RequireRange(pe, static_cast<std::uint32_t>(kSmoothCvarPointerRva),
                       sizeof(std::uint32_t), writableData,
                       "smoothing cvar pointer", error) ||
         !RequireImport(pe, "DINPUT.dll", "DirectInputCreateA", error))) {
        return false;
    }
    if (requirements.animationHooks &&
        !RequireRange(pe,
                      static_cast<std::uint32_t>(kClearEntityDefDynamicModelRva),
                      kClearEntityDefDynamicModelPrologue.size(), executableCode,
                      "R_ClearEntityDefDynamicModel", error)) {
        return false;
    }
    if (requirements.asyncClockHooks &&
        (!RequireImport(pe, "KERNEL32.dll", "CreateWaitableTimerA", error) ||
         !RequireImport(pe, "KERNEL32.dll", "SetWaitableTimer", error) ||
         !RequireImport(pe, "KERNEL32.dll", "WaitForSingleObject", error) ||
         !RequireImport(pe, "WINMM.dll", "timeBeginPeriod", error) ||
         !RequireImport(pe, "WINMM.dll", "timeGetTime", error))) {
        return false;
    }
    return true;
}

bool ValidateGameModuleLayout(const std::filesystem::path& path,
                              const CompatibilityRequirements& requirements,
                              std::string& error) {
    const auto bytes = ReadFileBytes(path);
    if (!bytes) {
        error = "could not read base/gamex86.dll";
        return false;
    }
    PeFile pe(*bytes);
    if (!pe.Parse(error) || !ValidateCommonImage(pe, true, error)) return false;
    if (!pe.HasExport("GetGameAPI")) {
        error = "required game module export GetGameAPI is absent";
        return false;
    }

    constexpr DWORD executableCode =
        IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    constexpr DWORD writableData = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    if (requirements.viewHook &&
        (!RequireRange(pe, static_cast<std::uint32_t>(kSingleViewRva),
                       kSingleViewPrologue.size(), executableCode,
                       "SingleView hook", error) ||
         !RequireBytes(pe, static_cast<std::uint32_t>(kSingleViewRva),
                       kSingleViewPrologue, "SingleView hook", error))) {
        return false;
    }
    if (requirements.cameraHook &&
        (!RequireRange(pe, static_cast<std::uint32_t>(kCalculateRenderViewRva),
                       kCalculateRenderViewPrologue.size(), executableCode,
                       "CalculateRenderView hook", error) ||
         !RequireBytes(pe, static_cast<std::uint32_t>(kCalculateRenderViewRva),
                       kCalculateRenderViewPrologue,
                       "CalculateRenderView hook", error))) {
        return false;
    }
    if (requirements.mouseHooks &&
        (!RequireRange(pe, static_cast<std::uint32_t>(kDetermineViewAnglesRva),
                       kDetermineViewAnglesPrologue.size(), executableCode,
                       "DetermineViewAngles hook", error) ||
         !RequireBytes(pe, static_cast<std::uint32_t>(kDetermineViewAnglesRva),
                       kDetermineViewAnglesPrologue,
                       "DetermineViewAngles hook", error))) {
        return false;
    }
    if (requirements.entityHooks &&
        !RequireRange(pe,
                      static_cast<std::uint32_t>(kGameRenderWorldPointerRva),
                      sizeof(std::uint32_t), writableData,
                      "game render-world pointer", error)) {
        return false;
    }
    return true;
}

} // namespace preyhfr
