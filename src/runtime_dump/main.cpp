#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path gameDirectory = fs::current_path();
    fs::path outputDirectory = fs::current_path() / L"research" / L"runtime";
    DWORD waitMilliseconds = 12'000;
    bool keepRunning = false;
};

struct RemoteModule {
    std::uintptr_t base = 0;
    std::uint32_t size = 0;
    fs::path path;
};

struct PeSection {
    std::string name;
    std::uint32_t virtualAddress = 0;
    std::uint32_t virtualSize = 0;
    std::uint32_t rawOffset = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t characteristics = 0;
};

struct PeImage {
    std::uint32_t imageSize = 0;
    std::uint32_t timestamp = 0;
    std::uint32_t entryPoint = 0;
    std::vector<PeSection> sections;
};

std::wstring Quote(const fs::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}

std::string JsonEscape(std::string_view input) {
    std::ostringstream out;
    for (const unsigned char c : input) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(c) << std::dec;
            } else {
                out << static_cast<char>(c);
            }
        }
    }
    return out.str();
}

std::optional<std::vector<std::uint8_t>> ReadFileBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }
    const auto length = file.tellg();
    if (length <= 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) {
        return std::nullopt;
    }
    return bytes;
}

std::optional<PeImage> ParsePe(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER)) {
        return std::nullopt;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return std::nullopt;
    }
    const auto ntOffset = static_cast<std::size_t>(dos->e_lfanew);
    if (ntOffset + sizeof(IMAGE_NT_HEADERS32) > bytes.size()) {
        return std::nullopt;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + ntOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return std::nullopt;
    }

    PeImage image;
    image.imageSize = nt->OptionalHeader.SizeOfImage;
    image.timestamp = nt->FileHeader.TimeDateStamp;
    image.entryPoint = nt->OptionalHeader.AddressOfEntryPoint;

    const auto sectionOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
                               nt->FileHeader.SizeOfOptionalHeader;
    const auto sectionBytes = static_cast<std::size_t>(nt->FileHeader.NumberOfSections) *
                              sizeof(IMAGE_SECTION_HEADER);
    if (sectionOffset + sectionBytes > bytes.size()) {
        return std::nullopt;
    }
    const auto* section = reinterpret_cast<const IMAGE_SECTION_HEADER*>(bytes.data() + sectionOffset);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        PeSection item;
        const auto nameEnd = std::find(section[index].Name,
                                       section[index].Name + IMAGE_SIZEOF_SHORT_NAME, 0);
        item.name.assign(reinterpret_cast<const char*>(section[index].Name),
                         reinterpret_cast<const char*>(nameEnd));
        item.virtualAddress = section[index].VirtualAddress;
        item.virtualSize = section[index].Misc.VirtualSize;
        item.rawOffset = section[index].PointerToRawData;
        item.rawSize = section[index].SizeOfRawData;
        item.characteristics = section[index].Characteristics;
        image.sections.push_back(item);
    }
    return image;
}

std::optional<std::string> Sha256(const std::uint8_t* data, std::size_t length) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    std::vector<std::uint8_t> object;
    std::array<std::uint8_t, 32> digest{};

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                          &resultLength, 0) < 0) {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }
    object.resize(objectLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
                         nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(data),
                       static_cast<ULONG>(length), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        out << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return out.str();
}

std::optional<RemoteModule> FindMainModule(DWORD processId) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                      processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    const BOOL found = Module32FirstW(snapshot, &module);
    CloseHandle(snapshot);
    if (!found) {
        return std::nullopt;
    }
    return RemoteModule{
        reinterpret_cast<std::uintptr_t>(module.modBaseAddr),
        module.modBaseSize,
        module.szExePath,
    };
}

bool IsReadableProtection(DWORD protection) {
    if ((protection & PAGE_GUARD) != 0 || protection == PAGE_NOACCESS) {
        return false;
    }
    const DWORD baseProtection = protection & 0xff;
    return baseProtection == PAGE_READONLY || baseProtection == PAGE_READWRITE ||
           baseProtection == PAGE_WRITECOPY || baseProtection == PAGE_EXECUTE_READ ||
           baseProtection == PAGE_EXECUTE_READWRITE ||
           baseProtection == PAGE_EXECUTE_WRITECOPY;
}

std::vector<std::uint8_t> ReadRemoteImage(HANDLE process, const RemoteModule& module,
                                          std::size_t imageSize,
                                          std::size_t& unreadableBytes) {
    std::vector<std::uint8_t> image(imageSize, 0);
    unreadableBytes = 0;
    std::size_t offset = 0;

    while (offset < image.size()) {
        MEMORY_BASIC_INFORMATION info{};
        const auto address = reinterpret_cast<const void*>(module.base + offset);
        if (VirtualQueryEx(process, address, &info, sizeof(info)) == 0) {
            unreadableBytes += image.size() - offset;
            break;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const auto regionEnd = regionStart + info.RegionSize;
        const auto readStart = module.base + offset;
        const auto remaining = image.size() - offset;
        const auto regionRemaining = regionEnd > readStart ? regionEnd - readStart : 0;
        const auto chunk = std::min<std::size_t>(remaining, regionRemaining);
        if (chunk == 0) {
            ++offset;
            ++unreadableBytes;
            continue;
        }

        SIZE_T bytesRead = 0;
        if (info.State == MEM_COMMIT && IsReadableProtection(info.Protect) &&
            ReadProcessMemory(process, reinterpret_cast<const void*>(readStart),
                              image.data() + offset, chunk, &bytesRead)) {
            if (bytesRead < chunk) {
                unreadableBytes += chunk - bytesRead;
            }
        } else {
            unreadableBytes += chunk;
        }
        offset += chunk;
    }
    return image;
}

std::string SectionHash(const std::vector<std::uint8_t>& image, const PeSection& section) {
    const auto start = static_cast<std::size_t>(section.virtualAddress);
    const auto length = std::min<std::size_t>(section.virtualSize, image.size() -
                                               std::min(start, image.size()));
    if (start >= image.size() || length == 0) {
        return {};
    }
    return Sha256(image.data() + start, length).value_or("");
}

double ByteDifferenceRatio(const std::vector<std::uint8_t>& fileBytes,
                           const std::vector<std::uint8_t>& memoryImage,
                           const PeSection& section) {
    const auto compareLength = std::min<std::size_t>(
        {section.rawSize, section.virtualSize,
         fileBytes.size() > section.rawOffset ? fileBytes.size() - section.rawOffset : 0,
         memoryImage.size() > section.virtualAddress
             ? memoryImage.size() - section.virtualAddress
             : 0});
    if (compareLength == 0) {
        return 0.0;
    }
    std::size_t differences = 0;
    for (std::size_t index = 0; index < compareLength; ++index) {
        if (fileBytes[section.rawOffset + index] !=
            memoryImage[section.virtualAddress + index]) {
            ++differences;
        }
    }
    return static_cast<double>(differences) / static_cast<double>(compareLength);
}

std::optional<std::vector<std::uint8_t>> BuildAnalysisPe(
    const std::vector<std::uint8_t>& memoryImage) {
    if (memoryImage.size() < sizeof(IMAGE_DOS_HEADER)) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> rebuilt = memoryImage;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(rebuilt.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return std::nullopt;
    }
    const auto ntOffset = static_cast<std::size_t>(dos->e_lfanew);
    if (ntOffset + sizeof(IMAGE_NT_HEADERS32) > rebuilt.size()) {
        return std::nullopt;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(rebuilt.data() + ntOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return std::nullopt;
    }
    const auto sectionOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
                               nt->FileHeader.SizeOfOptionalHeader;
    const auto sectionBytes = static_cast<std::size_t>(nt->FileHeader.NumberOfSections) *
                              sizeof(IMAGE_SECTION_HEADER);
    if (sectionOffset + sectionBytes > rebuilt.size()) {
        return std::nullopt;
    }
    auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(rebuilt.data() + sectionOffset);
    const std::uint32_t alignment = std::max<std::uint32_t>(
        nt->OptionalHeader.FileAlignment, 1);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const std::uint32_t start = sections[index].VirtualAddress;
        const std::uint32_t available = start < rebuilt.size()
            ? static_cast<std::uint32_t>(rebuilt.size() - start)
            : 0;
        const std::uint32_t wanted = (sections[index].Misc.VirtualSize + alignment - 1) /
                                     alignment * alignment;
        sections[index].PointerToRawData = start;
        sections[index].SizeOfRawData = std::min(wanted, available);
    }
    // This image exists only for static analysis. The original bound entry point
    // is deliberately retained, but the file must never be executed.
    return rebuilt;
}

std::optional<Options> ParseOptions(int argc, wchar_t** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--game-dir" && index + 1 < argc) {
            options.gameDirectory = fs::absolute(argv[++index]);
        } else if (argument == L"--output-dir" && index + 1 < argc) {
            options.outputDirectory = fs::absolute(argv[++index]);
        } else if (argument == L"--wait-ms" && index + 1 < argc) {
            options.waitMilliseconds = std::stoul(argv[++index]);
        } else if (argument == L"--keep-running") {
            options.keepRunning = true;
        } else if (argument == L"--help") {
            std::wcout << L"Usage: PreyRuntimeDump [--game-dir PATH] [--output-dir PATH] "
                          L"[--wait-ms N] [--keep-running]\n";
            return std::nullopt;
        } else {
            std::wcerr << L"Unknown or incomplete option: " << argument << L"\n";
            return std::nullopt;
        }
    }
    return options;
}

int Run(const Options& options) {
    const fs::path executable = options.gameDirectory / L"prey.exe";
    const auto fileBytes = ReadFileBytes(executable);
    if (!fileBytes) {
        std::wcerr << L"Could not read " << executable << L"\n";
        return 2;
    }
    const auto pe = ParsePe(*fileBytes);
    if (!pe) {
        std::wcerr << L"The target is not a supported 32-bit PE image.\n";
        return 3;
    }

    fs::create_directories(options.outputDirectory);
    const fs::path isolatedProfile = options.outputDirectory / L"profile";
    fs::create_directories(isolatedProfile);

    std::wstring commandLine = Quote(executable) +
        L" +set fs_savepath " + Quote(isolatedProfile) +
        L" +set r_fullscreen 0 +set r_mode 3 +set r_swapInterval 0";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        0, nullptr, options.gameDirectory.c_str(), &startup, &process)) {
        std::wcerr << L"CreateProcess failed with error " << GetLastError() << L"\n";
        return 4;
    }

    std::wcout << L"Started Prey as PID " << process.dwProcessId << L"; waiting "
               << options.waitMilliseconds << L" ms for startup binding.\n";
    const DWORD waitResult = WaitForSingleObject(process.hProcess, options.waitMilliseconds);
    if (waitResult == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(process.hProcess, &exitCode);
        std::wcerr << L"Prey exited before capture (exit code " << exitCode << L").\n";
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 5;
    }

    const auto module = FindMainModule(process.dwProcessId);
    if (!module) {
        std::wcerr << L"Could not enumerate the target's main module.\n";
        TerminateProcess(process.hProcess, 0xE001);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 6;
    }

    const std::size_t captureSize = std::min<std::size_t>(module->size, pe->imageSize);
    std::size_t unreadableBytes = 0;
    const auto memoryImage = ReadRemoteImage(process.hProcess, *module, captureSize,
                                              unreadableBytes);

    const std::wstring stem = L"prey-runtime-" + std::to_wstring(process.dwProcessId);
    const fs::path dumpPath = options.outputDirectory / (stem + L".bin");
    const fs::path analysisPePath = options.outputDirectory / (stem + L"-analysis.exe");
    const fs::path reportPath = options.outputDirectory / (stem + L".json");

    {
        std::ofstream dump(dumpPath, std::ios::binary);
        dump.write(reinterpret_cast<const char*>(memoryImage.data()),
                   static_cast<std::streamsize>(memoryImage.size()));
        if (!dump) {
            std::wcerr << L"Failed to write " << dumpPath << L"\n";
        }
    }

    if (const auto analysisPe = BuildAnalysisPe(memoryImage)) {
        std::ofstream dump(analysisPePath, std::ios::binary);
        dump.write(reinterpret_cast<const char*>(analysisPe->data()),
                   static_cast<std::streamsize>(analysisPe->size()));
        if (!dump) {
            std::wcerr << L"Failed to write " << analysisPePath << L"\n";
        }
    }

    {
        std::ofstream report(reportPath);
        report << "{\n";
        report << "  \"pid\": " << process.dwProcessId << ",\n";
        report << "  \"module_path\": \""
               << JsonEscape(Narrow(module->path.wstring())) << "\",\n";
        report << "  \"module_base\": \"0x" << std::hex << module->base << std::dec
               << "\",\n";
        report << "  \"module_size\": " << module->size << ",\n";
        report << "  \"pe_image_size\": " << pe->imageSize << ",\n";
        report << "  \"pe_timestamp\": " << pe->timestamp << ",\n";
        report << "  \"entry_point_rva\": " << pe->entryPoint << ",\n";
        report << "  \"unreadable_bytes\": " << unreadableBytes << ",\n";
        report << "  \"image_sha256\": \""
               << Sha256(memoryImage.data(), memoryImage.size()).value_or("") << "\",\n";
        report << "  \"sections\": [\n";
        for (std::size_t index = 0; index < pe->sections.size(); ++index) {
            const auto& section = pe->sections[index];
            report << "    {\"name\": \"" << JsonEscape(section.name)
                   << "\", \"rva\": " << section.virtualAddress
                   << ", \"virtual_size\": " << section.virtualSize
                   << ", \"characteristics\": " << section.characteristics
                   << ", \"memory_sha256\": \"" << SectionHash(memoryImage, section)
                   << "\", \"file_memory_difference_ratio\": " << std::fixed
                   << std::setprecision(6)
                   << ByteDifferenceRatio(*fileBytes, memoryImage, section) << "}";
            if (index + 1 != pe->sections.size()) report << ',';
            report << '\n';
        }
        report << "  ]\n}\n";
    }

    std::wcout << L"Captured " << memoryImage.size() << L" bytes to " << dumpPath
               << L" (" << unreadableBytes << L" unreadable bytes).\n";
    std::wcout << L"Metadata: " << reportPath << L"\n";
    std::wcout << L"Analysis-only reconstructed PE: " << analysisPePath << L"\n";

    if (!options.keepRunning) {
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 5'000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const auto options = ParseOptions(argc, argv);
        if (!options) {
            return argc > 1 && std::wstring_view(argv[1]) == L"--help" ? 0 : 1;
        }
        return Run(*options);
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 10;
    }
}
