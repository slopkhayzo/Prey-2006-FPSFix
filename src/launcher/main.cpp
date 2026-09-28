#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>

#include "preyhfr_version.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSupportedExeSha256 =
    "cea6d424fbb8e2ffbf307a5bee509b45c2d35242f70be31387224db2a0eadd69";
constexpr std::string_view kSupportedGameDllSha256 =
    "74d436d376ba144762a28c940d0243135b4f9db8fdd7ee597b9cb5e4277b43c6";

constexpr std::array<std::uint8_t, 12> kWaitGatePattern{
    0xA1, 0, 0, 0, 0,       // mov eax, [com_fixedTic]
    0x39, 0x58, 0x24,       // cmp [eax+24h], ebx
    0x74, 0x02,             // je +2 (the two bytes replaced by NOPs)
    0x8B, 0xF9,             // mov edi, ecx
};

constexpr std::array<bool, kWaitGatePattern.size()> kWaitGateMask{
    true, false, false, false, false,
    true, true, true,
    true, true,
    true, true,
};

constexpr std::size_t kPatchOffset = 8;
constexpr std::size_t kComTicImmediateOffset = 33;
constexpr std::array<std::uint8_t, 2> kOriginalWaitBytes{0x74, 0x02};
constexpr std::array<std::uint8_t, 2> kPatchedWaitBytes{0x90, 0x90};

struct Resolution {
    unsigned int width = 0;
    unsigned int height = 0;
};

struct DesktopMode {
    Resolution resolution;
    unsigned int refreshRate = 0;
};

enum class DisplayMode {
    Unspecified,
    Windowed,
    Exclusive,
};

struct Options {
    fs::path gameDirectory = fs::current_path();
    DWORD scanTimeoutMilliseconds = 30'000;
    DWORD testSeconds = 0;
    unsigned int framesPerSecond = 240;
    bool frameCapFromDesktop = false;
    bool dryRun = false;
    bool viewLog = false;
    bool cameraInterpolation = false;
    bool viewModelInterpolation = false;
    bool viewModelAnimationInterpolation = false;
    bool worldInterpolation = false;
    bool worldAnimationInterpolation = false;
    double maximumWorldEntityDistance = 128.0;
    double maximumWorldEntityAngle = 90.0;
    bool mouseInterpolation = false;
    bool continuousSnapshotTiming = true;
    bool multiTicEntityAlignment = true;
    bool overdueSnapshotFallback = true;
    bool interpolationTrace = false;
    unsigned int timelineResetVirtualKey = VK_F10;
    bool borderless = false;
    DisplayMode displayMode = DisplayMode::Unspecified;
    std::optional<bool> vSync;
    std::optional<Resolution> resolution;
    std::vector<std::wstring> gameArguments;
    bool patchEnabled = true;
    bool validateConfiguration = false;
};

struct LaunchConfiguration {
    std::wstring commandLine;
    fs::path hookLog;
};

struct PeInfo {
    std::uint32_t imageSize = 0;
    std::uint32_t textRva = 0;
    std::uint32_t textSize = 0;
};

struct RemoteModule {
    std::uintptr_t base = 0;
    std::uint32_t size = 0;
};

struct Match {
    std::uintptr_t address = 0;
    std::uintptr_t comTicAddress = 0;
    std::uintptr_t fixedTicObjectPointerAddress = 0;
};

struct ClockSample {
    std::uint32_t tic = 0;
    std::chrono::steady_clock::time_point sampledAt{};
};

struct PresentationWindow {
    std::int64_t qpcStart = 0;
    std::int64_t qpcEnd = 0;
    std::uint64_t swaps = 0;
    double seconds = 0.0;
    double rate = 0.0;
    std::size_t samples = 0;
    double medianMilliseconds = 0.0;
    double p95Milliseconds = 0.0;
    double p99Milliseconds = 0.0;
    double worstMilliseconds = 0.0;
    int viewHook = 0;
    std::uint64_t views = 0;
    std::uint64_t nullViews = 0;
    std::uint64_t timeChanges = 0;
    std::uint64_t sameTime = 0;
    std::uint64_t discontinuities = 0;
    std::uint64_t viewIdChanges = 0;
    double maximumOriginStep = 0.0;
    int cameraEnabled = 0;
    std::uint64_t interpolatedViews = 0;
    std::uint64_t snappedViews = 0;
    std::uint64_t cameraResets = 0;
    std::uint64_t cameraResetTime = 0;
    std::uint64_t cameraResetViewId = 0;
    std::uint64_t cameraResetFov = 0;
    std::uint64_t cameraResetStall = 0;
    std::uint64_t cameraResetOrigin = 0;
    std::uint64_t cameraResetAxis = 0;
    std::uint64_t viewDeltaNonpositive = 0;
    std::uint64_t viewDelta32 = 0;
    std::uint64_t viewDelta48 = 0;
    std::uint64_t viewDelta64 = 0;
    std::uint64_t viewDeltaOther = 0;
    std::int64_t minimumViewDelta = 0;
    std::int64_t maximumViewDelta = 0;
    int viewModelEnabled = 0;
    std::uint64_t adjustedViewModels = 0;
    std::size_t trackedViewModels = 0;
    int viewModelAnimationEnabled = 0;
    std::uint64_t adjustedViewModelAnimations = 0;
    std::uint64_t interpolatedViewModelJoints = 0;
    std::size_t trackedViewModelAnimations = 0;
    std::uint64_t viewModelAnimationSkips = 0;
    int worldEnabled = 0;
    std::uint64_t adjustedWorldEntities = 0;
    std::size_t trackedWorldEntities = 0;
    std::uint64_t worldEntityResets = 0;
    std::uint64_t worldEntityCapacitySkips = 0;
    int worldAnimationEnabled = 0;
    std::uint64_t adjustedWorldAnimations = 0;
    std::uint64_t interpolatedWorldJoints = 0;
    std::size_t trackedWorldAnimations = 0;
    std::uint64_t worldAnimationSkips = 0;
    int mouseEnabled = 0;
    int mouseHook = 0;
    std::uint64_t mouseGetDataCalls = 0;
    std::uint64_t mouseGetDataEvents = 0;
    std::uint64_t mousePeekCalls = 0;
    std::uint64_t mousePeekEvents = 0;
    std::uint64_t mouseOverlaidViews = 0;
    std::uint64_t mouseSkippedState = 0;
    std::uint64_t mouseSkippedSmoothing = 0;
    std::uint64_t mousePeekFailures = 0;
    std::uint64_t mouseMoveCalls = 0;
    std::uint64_t mouseMoveChanges = 0;
    std::uint64_t mouseTrackedOverlays = 0;
    std::uint64_t mouseRetiredChanges = 0;
    std::uint64_t mouseAsyncCommands = 0;
    std::uint64_t mouseTicSelections = 0;
    std::uint64_t mouseDirectSelections = 0;
    std::uint64_t mouseCommandMisses = 0;
    std::uint64_t mouseLedgerOverflows = 0;
};

std::wstring Quote(const fs::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

std::optional<fs::path> CurrentExecutablePath() {
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) return std::nullopt;
        if (length < buffer.size() - 1) {
            return fs::path(std::wstring(buffer.data(), length));
        }
        if (buffer.size() >= 32'768) return std::nullopt;
        buffer.resize(std::min<std::size_t>(32'768, buffer.size() * 2));
    }
}

std::optional<DesktopMode> CurrentPrimaryDesktopMode() {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) ||
        mode.dmPelsWidth == 0 || mode.dmPelsHeight == 0) {
        return std::nullopt;
    }
    return DesktopMode{
        Resolution{mode.dmPelsWidth, mode.dmPelsHeight},
        mode.dmDisplayFrequency,
    };
}

std::optional<Resolution> ParseResolution(std::wstring_view value) {
    const auto separator = value.find_first_of(L"xX");
    if (separator == std::wstring_view::npos || separator == 0 ||
        separator + 1 >= value.size() ||
        value.find_first_of(L"xX", separator + 1) != std::wstring_view::npos) {
        return std::nullopt;
    }
    const std::wstring widthText(value.substr(0, separator));
    const std::wstring heightText(value.substr(separator + 1));
    const auto decimalDigitsOnly = [](const std::wstring& text) {
        return !text.empty() &&
            std::all_of(text.begin(), text.end(), [](wchar_t character) {
                return character >= L'0' && character <= L'9';
            });
    };
    if (!decimalDigitsOnly(widthText) || !decimalDigitsOnly(heightText) ||
        widthText.size() > 5 || heightText.size() > 5) {
        return std::nullopt;
    }
    std::size_t widthParsed = 0;
    std::size_t heightParsed = 0;
    const unsigned long width = std::stoul(widthText, &widthParsed);
    const unsigned long height = std::stoul(heightText, &heightParsed);
    if (widthParsed != widthText.size() || heightParsed != heightText.size() ||
        width < 320 || width > 16384 || height < 240 || height > 16384) {
        return std::nullopt;
    }
    return Resolution{static_cast<unsigned int>(width),
                      static_cast<unsigned int>(height)};
}

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char character) {
        return character == ' ' || character == '\t' || character == '\r' ||
               character == '\n';
    };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return static_cast<char>(character);
    });
    return value;
}

std::optional<bool> ParseConfigBoolean(const std::string& value) {
    const std::string normalized = LowerAscii(Trim(value));
    if (normalized == "true" || normalized == "yes" || normalized == "1" ||
        normalized == "on") {
        return true;
    }
    if (normalized == "false" || normalized == "no" || normalized == "0" ||
        normalized == "off") {
        return false;
    }
    return std::nullopt;
}

bool IsValidFrameCap(unsigned long value) {
    return value == 0 || (value >= 30 && value <= 1000);
}

std::optional<unsigned int> ParseTimelineResetKey(std::string value) {
    const std::string normalized = LowerAscii(Trim(std::move(value)));
    if (normalized == "none" || normalized == "off" ||
        normalized == "disabled") {
        return 0;
    }
    if (normalized.size() < 2 || normalized.front() != 'f') {
        return std::nullopt;
    }
    try {
        std::size_t parsed = 0;
        const unsigned long functionKey =
            std::stoul(normalized.substr(1), &parsed);
        if (parsed != normalized.size() - 1 || functionKey < 1 ||
            functionKey > 24) {
            return std::nullopt;
        }
        return VK_F1 + static_cast<unsigned int>(functionKey - 1);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string TimelineResetKeyName(unsigned int virtualKey) {
    if (virtualKey == 0) return "none";
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return "F" + std::to_string(virtualKey - VK_F1 + 1);
    }
    return "unknown";
}

std::optional<fs::path> SelectConfigPath(int argc, wchar_t** argv,
                                         const fs::path& launcherDirectory,
                                         bool& explicitlyRequested) {
    explicitlyRequested = false;
    std::optional<fs::path> selected = launcherDirectory / L"PreyHFR.ini";
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--") break;
        if (argument == L"--no-config") {
            if (explicitlyRequested) {
                throw std::runtime_error("--config and --no-config cannot be combined");
            }
            selected.reset();
            explicitlyRequested = true;
        } else if (argument == L"--config") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--config requires a file path");
            }
            if (explicitlyRequested) {
                throw std::runtime_error("only one --config or --no-config option is allowed");
            }
            selected = fs::absolute(argv[++index]);
            explicitlyRequested = true;
        }
    }
    return selected;
}

bool LoadConfiguration(const fs::path& path, Options& options, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "could not open configuration file: " + path.string();
        return false;
    }

    std::map<std::string, std::pair<std::string, std::size_t>> values;
    std::string section;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        line = Trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = LowerAscii(Trim(line.substr(1, line.size() - 2)));
            if (section != "patch" && section != "interpolation" &&
                section != "compatibility" && section != "diagnostics" &&
                section != "display") {
                error = "unknown section [" + section + "] at line " +
                        std::to_string(lineNumber);
                return false;
            }
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos || section.empty()) {
            error = "expected key=value inside a section at line " +
                    std::to_string(lineNumber);
            return false;
        }
        const std::string key = section + "." +
                                LowerAscii(Trim(line.substr(0, separator)));
        const std::string value = Trim(line.substr(separator + 1));
        if (key.back() == '.') {
            error = "empty key at line " + std::to_string(lineNumber);
            return false;
        }
        if (!values.emplace(key, std::make_pair(value, lineNumber)).second) {
            error = "duplicate key " + key + " at line " +
                    std::to_string(lineNumber);
            return false;
        }
    }
    if (!file.eof()) {
        error = "failed while reading configuration file: " + path.string();
        return false;
    }

    const std::array<std::string_view, 18> knownKeys{
        "patch.enabled", "patch.presentation_fps", "patch.simulation_hz",
        "interpolation.enabled", "interpolation.camera",
        "interpolation.viewmodel", "interpolation.viewmodel_animation",
        "interpolation.world", "interpolation.world_animation",
        "interpolation.mouse", "interpolation.world_max_distance",
        "interpolation.world_max_angle",
        "compatibility.continuous_snapshot_timing",
        "compatibility.multi_tic_entity_alignment",
        "compatibility.overdue_snapshot_fallback",
        "diagnostics.interpolation_trace", "diagnostics.timeline_reset_key",
        "display.borderless",
    };
    for (const auto& [key, entry] : values) {
        const bool known = std::find(knownKeys.begin(), knownKeys.end(), key) !=
                           knownKeys.end();
        if (!known && key != "display.resolution") {
            error = "unknown key " + key + " at line " +
                    std::to_string(entry.second);
            return false;
        }
    }

    const auto readBoolean = [&](const char* key, bool& destination) {
        const auto found = values.find(key);
        if (found == values.end()) return true;
        const auto parsed = ParseConfigBoolean(found->second.first);
        if (!parsed) {
            error = std::string("invalid boolean for ") + key + " at line " +
                    std::to_string(found->second.second);
            return false;
        }
        destination = *parsed;
        return true;
    };

    if (!readBoolean("patch.enabled", options.patchEnabled)) return false;
    const auto simulation = values.find("patch.simulation_hz");
    if (simulation != values.end() &&
        LowerAscii(Trim(simulation->second.first)) != "native") {
        error = "patch.simulation_hz must be native at line " +
                std::to_string(simulation->second.second);
        return false;
    }
    const auto cap = values.find("patch.presentation_fps");
    if (cap != values.end()) {
        const std::string normalized = LowerAscii(Trim(cap->second.first));
        if (normalized == "desktop" || normalized == "refresh") {
            options.frameCapFromDesktop = true;
        } else {
            try {
                std::size_t parsed = 0;
                const unsigned long value = std::stoul(cap->second.first, &parsed);
                if (parsed != cap->second.first.size() || !IsValidFrameCap(value)) {
                    throw std::invalid_argument("range");
                }
                options.framesPerSecond = static_cast<unsigned int>(value);
                options.frameCapFromDesktop = false;
            } catch (const std::exception&) {
                error = "patch.presentation_fps must be desktop, 0, or 30..1000 at line " +
                        std::to_string(cap->second.second);
                return false;
            }
        }
    }

    bool interpolationEnabled = false;
    if (!readBoolean("interpolation.enabled", interpolationEnabled)) return false;
    if (values.contains("interpolation.enabled")) {
        options.cameraInterpolation = interpolationEnabled;
        options.viewModelInterpolation = interpolationEnabled;
        options.viewModelAnimationInterpolation = interpolationEnabled;
        options.worldInterpolation = interpolationEnabled;
        options.worldAnimationInterpolation = interpolationEnabled;
        options.mouseInterpolation = interpolationEnabled;
    }
    if (!readBoolean("interpolation.camera", options.cameraInterpolation) ||
        !readBoolean("interpolation.viewmodel", options.viewModelInterpolation) ||
        !readBoolean("interpolation.viewmodel_animation",
                     options.viewModelAnimationInterpolation) ||
        !readBoolean("interpolation.world", options.worldInterpolation) ||
        !readBoolean("interpolation.world_animation",
                     options.worldAnimationInterpolation) ||
        !readBoolean("interpolation.mouse", options.mouseInterpolation) ||
        !readBoolean("compatibility.continuous_snapshot_timing",
                     options.continuousSnapshotTiming) ||
        !readBoolean("compatibility.multi_tic_entity_alignment",
                     options.multiTicEntityAlignment) ||
        !readBoolean("compatibility.overdue_snapshot_fallback",
                     options.overdueSnapshotFallback) ||
        !readBoolean("diagnostics.interpolation_trace",
                     options.interpolationTrace) ||
        !readBoolean("display.borderless", options.borderless)) {
        return false;
    }

    const auto resetKey = values.find("diagnostics.timeline_reset_key");
    if (resetKey != values.end()) {
        const auto parsed = ParseTimelineResetKey(resetKey->second.first);
        if (!parsed) {
            error = "diagnostics.timeline_reset_key must be F1..F24 or none at line " +
                    std::to_string(resetKey->second.second);
            return false;
        }
        options.timelineResetVirtualKey = *parsed;
    }

    const auto readDouble = [&](const char* key, double minimum, double maximum,
                                double& destination) {
        const auto found = values.find(key);
        if (found == values.end()) return true;
        try {
            std::size_t parsed = 0;
            const double value = std::stod(found->second.first, &parsed);
            if (parsed != found->second.first.size() || !std::isfinite(value) ||
                value < minimum || value > maximum) {
                throw std::invalid_argument("range");
            }
            destination = value;
            return true;
        } catch (const std::exception&) {
            error = std::string(key) + " is outside its supported range at line " +
                    std::to_string(found->second.second);
            return false;
        }
    };
    if (!readDouble("interpolation.world_max_distance", 1.0, 4096.0,
                    options.maximumWorldEntityDistance) ||
        !readDouble("interpolation.world_max_angle", 1.0, 180.0,
                    options.maximumWorldEntityAngle)) {
        return false;
    }

    const auto resolution = values.find("display.resolution");
    if (resolution != values.end() && !resolution->second.first.empty() &&
        LowerAscii(resolution->second.first) != "desktop") {
        std::wstring wideValue(resolution->second.first.begin(),
                               resolution->second.first.end());
        options.resolution = ParseResolution(wideValue);
        if (!options.resolution) {
            error = "display.resolution must be blank, desktop, or WIDTHxHEIGHT at line " +
                    std::to_string(resolution->second.second);
            return false;
        }
    }
    if (resolution != values.end() &&
        LowerAscii(resolution->second.first) == "desktop") {
        options.borderless = true;
    }
    return true;
}

bool EnvironmentEntryHasName(const std::wstring& entry, const wchar_t* name) {
    const auto separator = entry.find(L'=');
    if (separator == std::wstring::npos || separator == 0) return false;
    const std::size_t nameLength = std::wcslen(name);
    return separator == nameLength &&
           _wcsnicmp(entry.c_str(), name, nameLength) == 0;
}

std::optional<std::vector<wchar_t>> BuildChildEnvironment(unsigned int cap,
                                                          const fs::path& logPath,
                                                          bool viewLog,
                                                          bool cameraInterpolation,
                                                          bool viewModelInterpolation,
                                                          bool viewModelAnimationInterpolation,
                                                          bool worldInterpolation,
                                                          bool worldAnimationInterpolation,
                                                          double maximumWorldEntityDistance,
                                                          double maximumWorldEntityAngle,
                                                          bool mouseInterpolation,
                                                          bool continuousSnapshotTiming,
                                                          bool multiTicEntityAlignment,
                                                          bool overdueSnapshotFallback,
                                                          bool interpolationTrace,
                                                          unsigned int timelineResetVirtualKey,
                                                          bool borderless,
                                                          const std::optional<Resolution>& resolution) {
    LPWCH environment = GetEnvironmentStringsW();
    if (environment == nullptr) return std::nullopt;

    std::vector<std::wstring> entries;
    for (const wchar_t* current = environment; *current != L'\0';) {
        std::wstring entry(current);
        current += entry.size() + 1;
        if (!EnvironmentEntryHasName(entry, L"PREYHFR_CAP") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_LOG") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_VIEW_LOG") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_CAMERA_INTERP") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_VIEWMODEL_INTERP") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_VIEWMODEL_ANIM_INTERP") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_WORLD_INTERP") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_WORLD_ANIM_INTERP") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_WORLD_MAX_DISTANCE") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_WORLD_MAX_ANGLE") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_MOUSE_INTERP") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_CONTINUOUS_SNAPSHOT_TIMING") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_MULTI_TIC_ENTITY_ALIGNMENT") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_OVERDUE_SNAPSHOT_FALLBACK") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_INTERPOLATION_TRACE") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_TIMELINE_RESET_KEY") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_BORDERLESS") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_RENDER_WIDTH") &&
            !EnvironmentEntryHasName(entry, L"PREYHFR_RENDER_HEIGHT")) {
            entries.push_back(std::move(entry));
        }
    }
    FreeEnvironmentStringsW(environment);

    entries.push_back(L"PREYHFR_CAP=" + std::to_wstring(cap));
    entries.push_back(L"PREYHFR_LOG=" + logPath.wstring());
    entries.push_back(std::wstring(L"PREYHFR_VIEW_LOG=") + (viewLog ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_CAMERA_INTERP=") +
                      (cameraInterpolation ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_VIEWMODEL_INTERP=") +
                      (viewModelInterpolation ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_VIEWMODEL_ANIM_INTERP=") +
                      (viewModelAnimationInterpolation ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_WORLD_INTERP=") +
                      (worldInterpolation ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_WORLD_ANIM_INTERP=") +
                      (worldAnimationInterpolation ? L"1" : L"0"));
    entries.push_back(L"PREYHFR_WORLD_MAX_DISTANCE=" +
                      std::to_wstring(maximumWorldEntityDistance));
    entries.push_back(L"PREYHFR_WORLD_MAX_ANGLE=" +
                      std::to_wstring(maximumWorldEntityAngle));
    entries.push_back(std::wstring(L"PREYHFR_MOUSE_INTERP=") +
                      (mouseInterpolation ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_CONTINUOUS_SNAPSHOT_TIMING=") +
                      (continuousSnapshotTiming ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_MULTI_TIC_ENTITY_ALIGNMENT=") +
                      (multiTicEntityAlignment ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_OVERDUE_SNAPSHOT_FALLBACK=") +
                      (overdueSnapshotFallback ? L"1" : L"0"));
    entries.push_back(std::wstring(L"PREYHFR_INTERPOLATION_TRACE=") +
                      (interpolationTrace ? L"1" : L"0"));
    entries.push_back(L"PREYHFR_TIMELINE_RESET_KEY=" +
                      std::to_wstring(timelineResetVirtualKey));
    entries.push_back(std::wstring(L"PREYHFR_BORDERLESS=") +
                      (borderless ? L"1" : L"0"));
    entries.push_back(L"PREYHFR_RENDER_WIDTH=" + std::to_wstring(
        resolution ? resolution->width : 0));
    entries.push_back(L"PREYHFR_RENDER_HEIGHT=" + std::to_wstring(
        resolution ? resolution->height : 0));

    std::vector<wchar_t> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

std::optional<std::vector<std::uint8_t>> ReadFileBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const auto length = file.tellg();
    if (length <= 0) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) return std::nullopt;
    return bytes;
}

std::optional<std::string> Sha256(const std::vector<std::uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD returned = 0;
    std::vector<std::uint8_t> object;
    std::array<std::uint8_t, 32> digest{};

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                          &returned, 0) < 0) {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }
    object.resize(objectLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
                         nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
                       static_cast<ULONG>(bytes.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        if (hash) BCryptDestroyHash(hash);
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

std::optional<PeInfo> ParsePe(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER)) return std::nullopt;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return std::nullopt;
    const auto ntOffset = static_cast<std::size_t>(dos->e_lfanew);
    if (ntOffset + sizeof(IMAGE_NT_HEADERS32) > bytes.size()) return std::nullopt;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + ntOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return std::nullopt;

    PeInfo result;
    result.imageSize = nt->OptionalHeader.SizeOfImage;
    const auto sectionOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
                               nt->FileHeader.SizeOfOptionalHeader;
    if (sectionOffset + static_cast<std::size_t>(nt->FileHeader.NumberOfSections) *
                            sizeof(IMAGE_SECTION_HEADER) > bytes.size()) return std::nullopt;
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(bytes.data() +
                                                                         sectionOffset);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const std::string name(reinterpret_cast<const char*>(sections[index].Name),
                               strnlen(reinterpret_cast<const char*>(sections[index].Name),
                                       IMAGE_SIZEOF_SHORT_NAME));
        if (name == ".text") {
            result.textRva = sections[index].VirtualAddress;
            result.textSize = sections[index].Misc.VirtualSize;
            break;
        }
    }
    if (result.textRva == 0 || result.textSize == 0) return std::nullopt;
    return result;
}

std::optional<RemoteModule> FindMainModule(DWORD processId) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                      processId);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    const BOOL found = Module32FirstW(snapshot, &module);
    CloseHandle(snapshot);
    if (!found) return std::nullopt;
    return RemoteModule{reinterpret_cast<std::uintptr_t>(module.modBaseAddr),
                        module.modBaseSize};
}

std::optional<std::uintptr_t> FindRemoteModuleByPath(DWORD processId,
                                                     const fs::path& expectedPath,
                                                     std::uintptr_t expectedBase) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                      processId);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    BOOL found = Module32FirstW(snapshot, &module);
    std::optional<std::uintptr_t> result;
    while (found) {
        std::error_code error;
        const fs::path observedPath(module.szExePath);
        bool equivalent = fs::equivalent(expectedPath, observedPath, error);
        if (!equivalent) {
            error.clear();
            const fs::path expectedNormalized =
                fs::weakly_canonical(expectedPath, error);
            if (!error) {
                error.clear();
                const fs::path observedNormalized =
                    fs::weakly_canonical(observedPath, error);
                equivalent = !error && _wcsicmp(
                    expectedNormalized.c_str(), observedNormalized.c_str()) == 0;
            }
        }
        const auto observedBase =
            reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
        if (equivalent && observedBase == expectedBase) {
            result = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
            break;
        }
        found = Module32NextW(snapshot, &module);
    }
    CloseHandle(snapshot);
    return result;
}

bool InjectLibrary(HANDLE process, DWORD processId, const fs::path& libraryPath,
                   DWORD timeoutMilliseconds) {
    const std::wstring path = fs::absolute(libraryPath).wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    if (remotePath == nullptr) return false;

    SIZE_T written = 0;
    const bool wrote = WriteProcessMemory(process, remotePath, path.c_str(), bytes,
                                          &written) != FALSE && written == bytes;
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    const FARPROC loadLibrary = kernel32 != nullptr
        ? GetProcAddress(kernel32, "LoadLibraryW")
        : nullptr;
    if (!wrote || loadLibrary == nullptr) {
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    const HANDLE thread = CreateRemoteThread(
        process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary),
        remotePath, 0, nullptr);
    if (thread == nullptr) {
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, timeoutMilliseconds);
    DWORD remoteModuleResult = 0;
    const bool readExitCode = wait == WAIT_OBJECT_0 &&
        GetExitCodeThread(thread, &remoteModuleResult) != FALSE;
    const bool completed = readExitCode && remoteModuleResult != 0;
    CloseHandle(thread);
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    if (!completed) {
        std::cerr << "Remote LoadLibraryW did not complete successfully "
                  << "(wait=0x" << std::hex << wait << ", result=0x"
                  << remoteModuleResult << std::dec << ").\n";
        return false;
    }

    const auto verificationDeadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(
            (std::min)(timeoutMilliseconds, static_cast<DWORD>(2'000)));
    do {
        const auto loadedModule = FindRemoteModuleByPath(
            processId, libraryPath,
            static_cast<std::uintptr_t>(remoteModuleResult));
        if (loadedModule) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < verificationDeadline);

    std::cerr << "LoadLibraryW returned module base 0x" << std::hex
              << remoteModuleResult << std::dec
              << ", but the module could not be matched by base and normalized "
                 "path in the target process.\n";
    return false;
}

bool PatternMatches(const std::uint8_t* bytes) {
    for (std::size_t index = 0; index < kWaitGatePattern.size(); ++index) {
        if (kWaitGateMask[index] && bytes[index] != kWaitGatePattern[index]) {
            return false;
        }
    }
    return true;
}

std::vector<std::size_t> FindPattern(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::size_t> matches;
    if (bytes.size() < kWaitGatePattern.size()) return matches;
    for (std::size_t offset = 0;
         offset <= bytes.size() - kWaitGatePattern.size(); ++offset) {
        if (PatternMatches(bytes.data() + offset)) matches.push_back(offset);
    }
    return matches;
}

template <typename T>
std::optional<T> ReadRemote(HANDLE process, std::uintptr_t address) {
    T value{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(address), &value,
                           sizeof(value), &read) || read != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<Match> FindWaitGate(HANDLE process, const RemoteModule& module,
                                  const PeInfo& pe, bool& alreadyPatched) {
    std::vector<std::uint8_t> text(pe.textSize);
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(process,
                           reinterpret_cast<const void*>(module.base + pe.textRva),
                           text.data(), text.size(), &bytesRead) || bytesRead != text.size()) {
        return std::nullopt;
    }

    auto matches = FindPattern(text);
    alreadyPatched = false;
    if (matches.empty()) {
        auto patchedPattern = kWaitGatePattern;
        patchedPattern[kPatchOffset] = kPatchedWaitBytes[0];
        patchedPattern[kPatchOffset + 1] = kPatchedWaitBytes[1];
        for (std::size_t offset = 0;
             offset <= text.size() - patchedPattern.size(); ++offset) {
            bool matched = true;
            for (std::size_t index = 0; index < patchedPattern.size(); ++index) {
                if (kWaitGateMask[index] && text[offset + index] != patchedPattern[index]) {
                    matched = false;
                    break;
                }
            }
            if (matched) matches.push_back(offset);
        }
        alreadyPatched = matches.size() == 1;
    }
    if (matches.size() != 1) return std::nullopt;

    const auto offset = matches.front();
    if (offset + kComTicImmediateOffset + sizeof(std::uint32_t) > text.size()) {
        return std::nullopt;
    }
    // The validated retail sequence immediately following the wait comparison is:
    //   A1 <com_ticNumber> 3B C7 89 ...
    if (text[offset + kComTicImmediateOffset - 1] != 0xA1) return std::nullopt;
    std::uint32_t comTicAddress = 0;
    std::uint32_t fixedTicAddress = 0;
    std::memcpy(&fixedTicAddress, text.data() + offset + 1, sizeof(fixedTicAddress));
    std::memcpy(&comTicAddress, text.data() + offset + kComTicImmediateOffset,
                sizeof(comTicAddress));
    if (comTicAddress < module.base || comTicAddress >= module.base + module.size ||
        fixedTicAddress < module.base || fixedTicAddress >= module.base + module.size) {
        return std::nullopt;
    }
    return Match{module.base + pe.textRva + offset,
                 comTicAddress, fixedTicAddress};
}

std::optional<ClockSample> WaitForStableClock(HANDLE process,
                                              std::uintptr_t comTicAddress,
                                              DWORD timeoutMilliseconds) {
    using namespace std::chrono;
    constexpr auto sampleWindow = milliseconds(1000);
    constexpr std::uint32_t minimumTicsPerWindow = 50;

    const auto deadline = steady_clock::now() + milliseconds(timeoutMilliseconds);
    auto windowStart = steady_clock::now();
    auto startTic = ReadRemote<std::uint32_t>(process, comTicAddress);
    if (!startTic) return std::nullopt;

    while (steady_clock::now() < deadline) {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return std::nullopt;
        std::this_thread::sleep_for(milliseconds(100));
        const auto now = steady_clock::now();
        const auto currentTic = ReadRemote<std::uint32_t>(process, comTicAddress);
        if (!currentTic) return std::nullopt;
        if (now - windowStart >= sampleWindow) {
            const auto elapsed = duration<double>(now - windowStart).count();
            const auto delta = *currentTic - *startTic;
            const auto rate = delta / elapsed;
            std::cout << "Clock probe: " << delta << " async tics in " << std::fixed
                      << std::setprecision(3) << elapsed << " seconds = " << rate
                      << " Hz.\n";
            if (delta >= minimumTicsPerWindow) {
                return ClockSample{*currentTic, now};
            }
            windowStart = now;
            startTic = currentTic;
        }
    }
    return std::nullopt;
}

bool ApplyWaitPatch(HANDLE process, const Match& match) {
    const auto patchAddress = match.address + kPatchOffset;
    const auto current = ReadRemote<std::array<std::uint8_t, 2>>(process, patchAddress);
    if (!current || *current != kOriginalWaitBytes) return false;

    DWORD oldProtection = 0;
    if (!VirtualProtectEx(process, reinterpret_cast<void*>(patchAddress),
                          kPatchedWaitBytes.size(), PAGE_EXECUTE_READWRITE,
                          &oldProtection)) return false;
    SIZE_T written = 0;
    const BOOL wrote = WriteProcessMemory(process, reinterpret_cast<void*>(patchAddress),
                                          kPatchedWaitBytes.data(),
                                          kPatchedWaitBytes.size(), &written);
    FlushInstructionCache(process, reinterpret_cast<const void*>(patchAddress),
                          kPatchedWaitBytes.size());
    DWORD ignored = 0;
    VirtualProtectEx(process, reinterpret_cast<void*>(patchAddress),
                     kPatchedWaitBytes.size(), oldProtection, &ignored);
    if (!wrote || written != kPatchedWaitBytes.size()) return false;
    const auto verified = ReadRemote<std::array<std::uint8_t, 2>>(process, patchAddress);
    return verified && *verified == kPatchedWaitBytes;
}

std::optional<Options> ParseOptions(int argc, wchar_t** argv, Options options) {
    bool gameArguments = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (gameArguments) {
            options.gameArguments.emplace_back(argument);
        } else if (argument == L"--") {
            gameArguments = true;
        } else if (argument == L"--config" && index + 1 < argc) {
            ++index; // Configuration was selected and loaded before this pass.
        } else if (argument == L"--no-config") {
            // Configuration selection was handled before this pass.
        } else if (argument == L"--validate-config") {
            options.validateConfiguration = true;
        } else if (argument == L"--version") {
            std::cout << "PreyHFR " << PREYHFR_VERSION << '\n';
            return std::nullopt;
        } else if (argument == L"--game-dir" && index + 1 < argc) {
            options.gameDirectory = fs::absolute(argv[++index]);
        } else if (argument == L"--scan-timeout-ms" && index + 1 < argc) {
            options.scanTimeoutMilliseconds = std::stoul(argv[++index]);
        } else if (argument == L"--test-seconds" && index + 1 < argc) {
            options.testSeconds = std::stoul(argv[++index]);
        } else if (argument == L"--fps" && index + 1 < argc) {
            const std::wstring_view value(argv[++index]);
            if (value == L"desktop" || value == L"refresh") {
                options.frameCapFromDesktop = true;
            } else {
                try {
                    std::size_t parsed = 0;
                    const unsigned long cap = std::stoul(std::wstring(value), &parsed);
                    if (parsed != value.size() || !IsValidFrameCap(cap)) {
                        throw std::invalid_argument("range");
                    }
                    options.framesPerSecond = static_cast<unsigned int>(cap);
                    options.frameCapFromDesktop = false;
                } catch (const std::exception&) {
                    std::wcerr << L"--fps must be desktop, 0, or between 30 and 1000.\n";
                    return std::nullopt;
                }
            }
        } else if (argument == L"--enabled") {
            options.patchEnabled = true;
        } else if (argument == L"--disabled") {
            options.patchEnabled = false;
        } else if (argument == L"--dry-run") {
            options.dryRun = true;
        } else if (argument == L"--view-log") {
            options.viewLog = true;
        } else if (argument == L"--no-view-log") {
            options.viewLog = false;
        } else if (argument == L"--camera-interp") {
            options.cameraInterpolation = true;
        } else if (argument == L"--no-camera-interp") {
            options.cameraInterpolation = false;
        } else if (argument == L"--viewmodel-interp") {
            options.viewModelInterpolation = true;
        } else if (argument == L"--no-viewmodel-interp") {
            options.viewModelInterpolation = false;
        } else if (argument == L"--viewmodel-anim-interp") {
            options.viewModelAnimationInterpolation = true;
        } else if (argument == L"--no-viewmodel-anim-interp") {
            options.viewModelAnimationInterpolation = false;
        } else if (argument == L"--world-interp") {
            options.worldInterpolation = true;
        } else if (argument == L"--no-world-interp") {
            options.worldInterpolation = false;
        } else if (argument == L"--world-anim-interp") {
            options.worldAnimationInterpolation = true;
        } else if (argument == L"--no-world-anim-interp") {
            options.worldAnimationInterpolation = false;
        } else if (argument == L"--world-max-distance" && index + 1 < argc) {
            std::size_t parsed = 0;
            options.maximumWorldEntityDistance = std::stod(argv[++index], &parsed);
            if (parsed != std::wcslen(argv[index]) ||
                !std::isfinite(options.maximumWorldEntityDistance) ||
                options.maximumWorldEntityDistance < 1.0 ||
                options.maximumWorldEntityDistance > 4096.0) {
                std::wcerr << L"--world-max-distance must be between 1 and 4096.\n";
                return std::nullopt;
            }
        } else if (argument == L"--world-max-angle" && index + 1 < argc) {
            std::size_t parsed = 0;
            options.maximumWorldEntityAngle = std::stod(argv[++index], &parsed);
            if (parsed != std::wcslen(argv[index]) ||
                !std::isfinite(options.maximumWorldEntityAngle) ||
                options.maximumWorldEntityAngle < 1.0 ||
                options.maximumWorldEntityAngle > 180.0) {
                std::wcerr << L"--world-max-angle must be between 1 and 180.\n";
                return std::nullopt;
            }
        } else if (argument == L"--mouse-interp") {
            options.mouseInterpolation = true;
        } else if (argument == L"--no-mouse-interp") {
            options.mouseInterpolation = false;
        } else if (argument == L"--continuous-snapshot-timing") {
            options.continuousSnapshotTiming = true;
        } else if (argument == L"--no-continuous-snapshot-timing") {
            options.continuousSnapshotTiming = false;
        } else if (argument == L"--multi-tic-entity-alignment") {
            options.multiTicEntityAlignment = true;
        } else if (argument == L"--no-multi-tic-entity-alignment") {
            options.multiTicEntityAlignment = false;
        } else if (argument == L"--overdue-snapshot-fallback") {
            options.overdueSnapshotFallback = true;
        } else if (argument == L"--no-overdue-snapshot-fallback") {
            options.overdueSnapshotFallback = false;
        } else if (argument == L"--interpolation-trace") {
            options.interpolationTrace = true;
        } else if (argument == L"--no-interpolation-trace") {
            options.interpolationTrace = false;
        } else if (argument == L"--timeline-reset-key" && index + 1 < argc) {
            const std::wstring wideValue(argv[++index]);
            if (!std::all_of(wideValue.begin(), wideValue.end(),
                             [](wchar_t character) {
                                 return character >= 0 && character <= 0x7f;
                             })) {
                std::wcerr << L"--timeline-reset-key must be F1..F24 or none.\n";
                return std::nullopt;
            }
            std::string asciiValue;
            asciiValue.reserve(wideValue.size());
            for (const wchar_t character : wideValue) {
                asciiValue.push_back(static_cast<char>(character));
            }
            const auto parsed = ParseTimelineResetKey(asciiValue);
            if (!parsed) {
                std::wcerr << L"--timeline-reset-key must be F1..F24 or none.\n";
                return std::nullopt;
            }
            options.timelineResetVirtualKey = *parsed;
        } else if (argument == L"--borderless") {
            options.borderless = true;
        } else if (argument == L"--no-borderless") {
            options.borderless = false;
        } else if (argument == L"--windowed") {
            options.displayMode = DisplayMode::Windowed;
        } else if (argument == L"--exclusive") {
            options.displayMode = DisplayMode::Exclusive;
        } else if (argument == L"--vsync" && index + 1 < argc) {
            const std::wstring_view value(argv[++index]);
            if (value == L"on" || value == L"1") {
                options.vSync = true;
            } else if (value == L"off" || value == L"0") {
                options.vSync = false;
            } else {
                std::wcerr << L"--vsync must be on or off.\n";
                return std::nullopt;
            }
        } else if (argument == L"--resolution" && index + 1 < argc) {
            options.resolution = ParseResolution(argv[++index]);
            if (!options.resolution) {
                std::wcerr << L"--resolution must use WIDTHxHEIGHT with a width "
                              L"from 320 to 16384 and a height from 240 to 16384.\n";
                return std::nullopt;
            }
        } else if (argument == L"--help") {
            std::wcout << L"Usage: PreyHFRLauncher [--version] "
                           L"[--config FILE | --no-config] "
                           L"[--validate-config] "
                           L"[--game-dir PATH] [--enabled|--disabled] [--dry-run] "
                           L"[--fps desktop|0|30..1000] [--view-log|--no-view-log] "
                           L"[--camera-interp|--no-camera-interp] "
                           L"[--viewmodel-interp] [--world-interp] "
                           L"[--viewmodel-anim-interp] "
                           L"[--world-anim-interp] "
                           L"[--world-max-distance N] [--world-max-angle N] "
                           L"[--mouse-interp] "
                           L"[--continuous-snapshot-timing] "
                           L"[--multi-tic-entity-alignment] "
                           L"[--overdue-snapshot-fallback] "
                           L"[--interpolation-trace|--no-interpolation-trace] "
                           L"[--timeline-reset-key F1..F24|none] "
                           L"[--windowed|--exclusive|--borderless] "
                           L"[--vsync on|off] [--resolution WIDTHxHEIGHT] "
                           L"[--test-seconds N] "
                          L"[-- game arguments]\n";
            return std::nullopt;
        } else {
            std::wcerr << L"Unknown or incomplete option: " << argument << L"\n";
            return std::nullopt;
        }
    }
    if (options.viewModelInterpolation && !options.cameraInterpolation) {
        std::wcerr << L"--viewmodel-interp requires --camera-interp.\n";
        return std::nullopt;
    }
    if (options.viewModelAnimationInterpolation &&
        !options.cameraInterpolation) {
        std::wcerr << L"--viewmodel-anim-interp requires --camera-interp.\n";
        return std::nullopt;
    }
    if (options.worldInterpolation && !options.cameraInterpolation) {
        std::wcerr << L"--world-interp requires --camera-interp.\n";
        return std::nullopt;
    }
    if (options.worldAnimationInterpolation &&
        !options.cameraInterpolation) {
        std::wcerr << L"--world-anim-interp requires --camera-interp.\n";
        return std::nullopt;
    }
    if (options.mouseInterpolation && !options.cameraInterpolation) {
        std::wcerr << L"--mouse-interp requires --camera-interp.\n";
        return std::nullopt;
    }
    if (options.interpolationTrace && !options.cameraInterpolation) {
        std::wcerr << L"--interpolation-trace requires --camera-interp.\n";
        return std::nullopt;
    }
    if (options.multiTicEntityAlignment &&
        !options.continuousSnapshotTiming) {
        std::wcerr << L"Multi-tic entity alignment requires continuous snapshot "
                      L"timing. Disable both to select RC1 behavior.\n";
        return std::nullopt;
    }
    if (options.overdueSnapshotFallback &&
        !options.multiTicEntityAlignment) {
        std::wcerr << L"The overdue snapshot fallback requires multi-tic entity "
                      L"alignment. Disable both to select RC2 behavior.\n";
        return std::nullopt;
    }
    if (options.borderless && options.displayMode == DisplayMode::Exclusive) {
        std::wcerr << L"--borderless cannot be combined with --exclusive.\n";
        return std::nullopt;
    }
    if (options.borderless && options.dryRun) {
        std::wcerr << L"--borderless requires hook injection and cannot be used "
                      L"with --dry-run.\n";
        return std::nullopt;
    }
    std::optional<DesktopMode> desktopMode;
    if (options.frameCapFromDesktop || options.borderless) {
        desktopMode = CurrentPrimaryDesktopMode();
        if (!desktopMode) {
            std::wcerr << L"Could not determine the current primary-desktop "
                          L"display mode.\n";
            return std::nullopt;
        }
    }
    if (options.frameCapFromDesktop) {
        if (!IsValidFrameCap(desktopMode->refreshRate)) {
            std::wcerr << L"The current primary-desktop refresh rate ("
                       << desktopMode->refreshRate
                       << L" Hz) is outside the supported range of 30..1000 Hz.\n";
            return std::nullopt;
        }
        options.framesPerSecond = desktopMode->refreshRate;
    }
    if (options.borderless) {
        options.displayMode = DisplayMode::Windowed;
        if (options.resolution &&
            (options.resolution->width != desktopMode->resolution.width ||
             options.resolution->height != desktopMode->resolution.height)) {
            std::wcerr << L"--borderless requires the renderer resolution to match "
                          L"the primary desktop ("
                       << desktopMode->resolution.width << L"x"
                       << desktopMode->resolution.height
                       << L"). Omit --resolution to select it automatically.\n";
            return std::nullopt;
        }
        options.resolution = desktopMode->resolution;
    }
    return options;
}

LaunchConfiguration BuildLaunchConfiguration(const fs::path& executable,
                                               const Options& options,
                                               const fs::path& launcherDirectory) {
    LaunchConfiguration result;
    result.commandLine = Quote(executable);
    if (options.testSeconds > 0) {
        std::array<wchar_t, MAX_PATH> temp{};
        if (GetTempPathW(static_cast<DWORD>(temp.size()), temp.data()) == 0) {
            throw std::runtime_error("GetTempPathW failed");
        }
        const std::wstring uniqueName =
            L"PreyHFR-test-profile-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64());
        const fs::path profile = fs::path(temp.data()) / uniqueName;
        const fs::path profileBase = profile / L"base";
        fs::create_directories(profileBase);
        // Keep the user's real profile untouched while allowing the retail build
        // to pass its local key/config startup checks in the isolated profile.
        fs::copy_file(options.gameDirectory / L"base" / L"preykey",
                      profileBase / L"preykey", fs::copy_options::overwrite_existing);
        fs::copy_file(options.gameDirectory / L"base" / L"preyconfig.cfg",
                      profileBase / L"preyconfig.cfg",
                      fs::copy_options::overwrite_existing);
        result.commandLine += L" +set fs_savepath " + Quote(profile) +
                              L" +set r_fullscreen 0 +set r_mode 3"
                              L" +set r_swapInterval 0 +set logfile 2"
                              L" +set logfileName PreyHFR-test.log";
        result.hookLog = profile / L"PreyHFR-hook.log";
    } else {
        result.hookLog = launcherDirectory / L"PreyHFR.log";
    }
    for (const auto& argument : options.gameArguments) {
        result.commandLine += L" \"" + argument + L"\"";
    }
    if (options.resolution) {
        result.commandLine +=
            L" +set r_mode -1 +set r_customWidth " +
            std::to_wstring(options.resolution->width) +
            L" +set r_customHeight " +
            std::to_wstring(options.resolution->height);
    }
    if (options.borderless) {
        // The retail renderer exposes only decorated-windowed and exclusive
        // fullscreen modes. Keep it windowed so the injected hook can remove
        // the frame without invoking ChangeDisplaySettings.
        result.commandLine += L" +set r_fullscreen 0";
    } else if (options.displayMode != DisplayMode::Unspecified) {
        result.commandLine += options.displayMode == DisplayMode::Exclusive
                                  ? L" +set r_fullscreen 1"
                                  : L" +set r_fullscreen 0";
    }
    if (options.vSync) {
        result.commandLine += *options.vSync ? L" +set r_swapInterval 1"
                                             : L" +set r_swapInterval 0";
    }
    return result;
}

std::vector<PresentationWindow> ReadPresentationWindows(const fs::path& logPath,
                                                        std::int64_t minimumQpc,
                                                        std::int64_t maximumQpc) {
    std::ifstream log(logPath);
    std::vector<PresentationWindow> windows;
    std::string line;
    while (std::getline(log, line)) {
        PresentationWindow window;
        unsigned long long swaps = 0;
        unsigned long long views = 0;
        unsigned long long nullViews = 0;
        unsigned long long timeChanges = 0;
        unsigned long long sameTime = 0;
        unsigned long long discontinuities = 0;
        unsigned long long viewIdChanges = 0;
        unsigned long long interpolatedViews = 0;
        unsigned long long snappedViews = 0;
        unsigned long long cameraResets = 0;
        unsigned long long adjustedViewModels = 0;
        unsigned long long mouseGetDataCalls = 0;
        unsigned long long mouseGetDataEvents = 0;
        unsigned long long mousePeekCalls = 0;
        unsigned long long mousePeekEvents = 0;
        unsigned long long mouseOverlaidViews = 0;
        unsigned long long mouseSkippedState = 0;
        unsigned long long mouseSkippedSmoothing = 0;
        unsigned long long mousePeekFailures = 0;
        unsigned long long adjustedWorldEntities = 0;
        unsigned long long worldEntityResets = 0;
        unsigned long long worldEntityCapacitySkips = 0;
        unsigned long long cameraResetTime = 0;
        unsigned long long cameraResetViewId = 0;
        unsigned long long cameraResetFov = 0;
        unsigned long long cameraResetStall = 0;
        unsigned long long cameraResetOrigin = 0;
        unsigned long long cameraResetAxis = 0;
        unsigned long long viewDeltaNonpositive = 0;
        unsigned long long viewDelta32 = 0;
        unsigned long long viewDelta48 = 0;
        unsigned long long viewDelta64 = 0;
        unsigned long long viewDeltaOther = 0;
        long long minimumViewDelta = 0;
        long long maximumViewDelta = 0;
        unsigned long long adjustedViewModelAnimations = 0;
        unsigned long long interpolatedViewModelJoints = 0;
        unsigned long long viewModelAnimationSkips = 0;
        unsigned long long adjustedWorldAnimations = 0;
        unsigned long long interpolatedWorldJoints = 0;
        unsigned long long worldAnimationSkips = 0;
        unsigned long long mouseMoveCalls = 0;
        unsigned long long mouseMoveChanges = 0;
        unsigned long long mouseTrackedOverlays = 0;
        unsigned long long mouseRetiredChanges = 0;
        unsigned long long mouseAsyncCommands = 0;
        unsigned long long mouseTicSelections = 0;
        unsigned long long mouseDirectSelections = 0;
        unsigned long long mouseCommandMisses = 0;
        unsigned long long mouseLedgerOverflows = 0;
        const int fields = sscanf_s(
            line.c_str(),
            "presentation: qpc_start=%lld qpc_end=%lld swaps=%llu seconds=%lf "
            "rate=%lf samples=%zu median_ms=%lf p95_ms=%lf p99_ms=%lf worst_ms=%lf "
            "view_hook=%d views=%llu null_views=%llu time_changes=%llu same_time=%llu "
            "discontinuities=%llu view_id_changes=%llu max_origin_step=%lf "
            "camera_enabled=%d interpolated=%llu snapped=%llu camera_resets=%llu "
            "viewmodel_enabled=%d viewmodel_adjusted=%llu viewmodel_tracked=%zu "
            "mouse_enabled=%d mouse_hook=%d mouse_getdata_calls=%llu "
            "mouse_getdata_events=%llu mouse_peek_calls=%llu "
            "mouse_peek_events=%llu mouse_overlaid=%llu "
            "mouse_skipped_state=%llu mouse_skipped_smoothing=%llu "
            "mouse_peek_failures=%llu world_enabled=%d "
            "world_adjusted=%llu world_tracked=%zu world_resets=%llu "
            "world_capacity_skips=%llu camera_reset_time=%llu "
            "camera_reset_viewid=%llu camera_reset_fov=%llu "
            "camera_reset_stall=%llu camera_reset_origin=%llu "
            "camera_reset_axis=%llu view_delta_nonpositive=%llu "
            "view_delta_32=%llu view_delta_48=%llu view_delta_64=%llu "
            "view_delta_other=%llu view_delta_min=%lld view_delta_max=%lld "
            "viewmodel_anim_enabled=%d viewmodel_anim_adjusted=%llu "
            "viewmodel_anim_joints=%llu viewmodel_anim_tracked=%zu "
            "viewmodel_anim_skips=%llu world_anim_enabled=%d "
            "world_anim_adjusted=%llu world_anim_joints=%llu "
            "world_anim_tracked=%zu world_anim_skips=%llu "
            "mouse_move_calls=%llu mouse_move_changes=%llu "
            "mouse_tracked_overlays=%llu mouse_retired_changes=%llu "
            "mouse_async_commands=%llu mouse_tic_selections=%llu "
            "mouse_direct_selections=%llu mouse_command_misses=%llu "
            "mouse_ledger_overflows=%llu",
            &window.qpcStart, &window.qpcEnd, &swaps, &window.seconds, &window.rate,
            &window.samples, &window.medianMilliseconds, &window.p95Milliseconds,
            &window.p99Milliseconds, &window.worstMilliseconds, &window.viewHook,
            &views, &nullViews, &timeChanges, &sameTime, &discontinuities,
            &viewIdChanges, &window.maximumOriginStep, &window.cameraEnabled,
            &interpolatedViews, &snappedViews, &cameraResets,
            &window.viewModelEnabled, &adjustedViewModels,
            &window.trackedViewModels, &window.mouseEnabled, &window.mouseHook,
            &mouseGetDataCalls, &mouseGetDataEvents, &mousePeekCalls,
            &mousePeekEvents, &mouseOverlaidViews, &mouseSkippedState,
            &mouseSkippedSmoothing, &mousePeekFailures, &window.worldEnabled,
            &adjustedWorldEntities, &window.trackedWorldEntities,
            &worldEntityResets, &worldEntityCapacitySkips, &cameraResetTime,
            &cameraResetViewId, &cameraResetFov, &cameraResetStall,
            &cameraResetOrigin, &cameraResetAxis, &viewDeltaNonpositive,
            &viewDelta32, &viewDelta48, &viewDelta64, &viewDeltaOther,
            &minimumViewDelta, &maximumViewDelta,
            &window.viewModelAnimationEnabled, &adjustedViewModelAnimations,
            &interpolatedViewModelJoints,
            &window.trackedViewModelAnimations, &viewModelAnimationSkips,
            &window.worldAnimationEnabled, &adjustedWorldAnimations,
            &interpolatedWorldJoints, &window.trackedWorldAnimations,
            &worldAnimationSkips, &mouseMoveCalls, &mouseMoveChanges,
            &mouseTrackedOverlays, &mouseRetiredChanges, &mouseAsyncCommands,
            &mouseTicSelections, &mouseDirectSelections, &mouseCommandMisses,
            &mouseLedgerOverflows);
        if ((fields == 25 || fields == 35 || fields == 40 || fields == 53 ||
             fields == 58 || fields == 63 || fields == 72) &&
            window.qpcStart >= minimumQpc &&
            window.qpcEnd <= maximumQpc) {
            window.swaps = static_cast<std::uint64_t>(swaps);
            window.views = static_cast<std::uint64_t>(views);
            window.nullViews = static_cast<std::uint64_t>(nullViews);
            window.timeChanges = static_cast<std::uint64_t>(timeChanges);
            window.sameTime = static_cast<std::uint64_t>(sameTime);
            window.discontinuities = static_cast<std::uint64_t>(discontinuities);
            window.viewIdChanges = static_cast<std::uint64_t>(viewIdChanges);
            window.interpolatedViews =
                static_cast<std::uint64_t>(interpolatedViews);
            window.snappedViews = static_cast<std::uint64_t>(snappedViews);
            window.cameraResets = static_cast<std::uint64_t>(cameraResets);
            window.adjustedViewModels =
                static_cast<std::uint64_t>(adjustedViewModels);
            window.mouseGetDataCalls =
                static_cast<std::uint64_t>(mouseGetDataCalls);
            window.mouseGetDataEvents =
                static_cast<std::uint64_t>(mouseGetDataEvents);
            window.mousePeekCalls = static_cast<std::uint64_t>(mousePeekCalls);
            window.mousePeekEvents = static_cast<std::uint64_t>(mousePeekEvents);
            window.mouseOverlaidViews =
                static_cast<std::uint64_t>(mouseOverlaidViews);
            window.mouseSkippedState =
                static_cast<std::uint64_t>(mouseSkippedState);
            window.mouseSkippedSmoothing =
                static_cast<std::uint64_t>(mouseSkippedSmoothing);
            window.mousePeekFailures =
                static_cast<std::uint64_t>(mousePeekFailures);
            window.mouseMoveCalls =
                static_cast<std::uint64_t>(mouseMoveCalls);
            window.mouseMoveChanges =
                static_cast<std::uint64_t>(mouseMoveChanges);
            window.mouseTrackedOverlays =
                static_cast<std::uint64_t>(mouseTrackedOverlays);
            window.mouseRetiredChanges =
                static_cast<std::uint64_t>(mouseRetiredChanges);
            window.mouseAsyncCommands =
                static_cast<std::uint64_t>(mouseAsyncCommands);
            window.mouseTicSelections =
                static_cast<std::uint64_t>(mouseTicSelections);
            window.mouseDirectSelections =
                static_cast<std::uint64_t>(mouseDirectSelections);
            window.mouseCommandMisses =
                static_cast<std::uint64_t>(mouseCommandMisses);
            window.mouseLedgerOverflows =
                static_cast<std::uint64_t>(mouseLedgerOverflows);
            window.adjustedWorldEntities =
                static_cast<std::uint64_t>(adjustedWorldEntities);
            window.worldEntityResets =
                static_cast<std::uint64_t>(worldEntityResets);
            window.worldEntityCapacitySkips =
                static_cast<std::uint64_t>(worldEntityCapacitySkips);
            window.cameraResetTime =
                static_cast<std::uint64_t>(cameraResetTime);
            window.cameraResetViewId =
                static_cast<std::uint64_t>(cameraResetViewId);
            window.cameraResetFov =
                static_cast<std::uint64_t>(cameraResetFov);
            window.cameraResetStall =
                static_cast<std::uint64_t>(cameraResetStall);
            window.cameraResetOrigin =
                static_cast<std::uint64_t>(cameraResetOrigin);
            window.cameraResetAxis =
                static_cast<std::uint64_t>(cameraResetAxis);
            window.viewDeltaNonpositive =
                static_cast<std::uint64_t>(viewDeltaNonpositive);
            window.viewDelta32 = static_cast<std::uint64_t>(viewDelta32);
            window.viewDelta48 = static_cast<std::uint64_t>(viewDelta48);
            window.viewDelta64 = static_cast<std::uint64_t>(viewDelta64);
            window.viewDeltaOther =
                static_cast<std::uint64_t>(viewDeltaOther);
            window.minimumViewDelta =
                static_cast<std::int64_t>(minimumViewDelta);
            window.maximumViewDelta =
                static_cast<std::int64_t>(maximumViewDelta);
            window.adjustedViewModelAnimations =
                static_cast<std::uint64_t>(adjustedViewModelAnimations);
            window.interpolatedViewModelJoints =
                static_cast<std::uint64_t>(interpolatedViewModelJoints);
            window.viewModelAnimationSkips =
                static_cast<std::uint64_t>(viewModelAnimationSkips);
            window.adjustedWorldAnimations =
                static_cast<std::uint64_t>(adjustedWorldAnimations);
            window.interpolatedWorldJoints =
                static_cast<std::uint64_t>(interpolatedWorldJoints);
            window.worldAnimationSkips =
                static_cast<std::uint64_t>(worldAnimationSkips);
            windows.push_back(window);
        }
    }
    return windows;
}

bool ReportPresentationTest(const fs::path& logPath, std::int64_t minimumQpc,
                            std::int64_t maximumQpc, unsigned int expectedCap,
                            bool expectViewLog, bool expectCameraInterpolation,
                            bool expectViewModelInterpolation,
                            bool expectViewModelAnimationInterpolation,
                            bool expectWorldInterpolation,
                            bool expectWorldAnimationInterpolation,
                            bool expectMouseInterpolation,
                            bool expectBorderless) {
    const auto windows = ReadPresentationWindows(logPath, minimumQpc, maximumQpc);
    if (windows.empty()) {
        std::cerr << "No complete presentation-rate window was recorded during the "
                     "self-test. Hook log: "
                  << logPath.string() << "\n";
        return false;
    }

    if (expectBorderless) {
        std::ifstream log(logPath);
        std::string line;
        bool applied = false;
        bool failed = false;
        while (std::getline(log, line)) {
            applied = applied ||
                line.find("window: borderless applied") != std::string::npos;
            failed = failed ||
                line.find("error: borderless") != std::string::npos;
        }
        if (!applied || failed) {
            std::cerr << "Borderless fullscreen was requested but a validated "
                         "desktop-sized window was not applied. Hook log: "
                      << logPath.string() << "\n";
            return false;
        }
        std::cout << "Borderless fullscreen self-test: desktop-sized window "
                     "style applied.\n";
    }

    std::uint64_t swaps = 0;
    double seconds = 0.0;
    double minimumRate = windows.front().rate;
    double maximumRate = windows.front().rate;
    double worstInterval = 0.0;
    double worstP99 = 0.0;
    std::uint64_t views = 0;
    std::uint64_t nullViews = 0;
    std::uint64_t timeChanges = 0;
    std::uint64_t sameTime = 0;
    std::uint64_t discontinuities = 0;
    std::uint64_t viewIdChanges = 0;
    double maximumOriginStep = 0.0;
    bool viewHookObserved = false;
    bool cameraInterpolationObserved = false;
    std::uint64_t interpolatedViews = 0;
    std::uint64_t snappedViews = 0;
    std::uint64_t cameraResets = 0;
    std::uint64_t cameraResetTime = 0;
    std::uint64_t cameraResetViewId = 0;
    std::uint64_t cameraResetFov = 0;
    std::uint64_t cameraResetStall = 0;
    std::uint64_t cameraResetOrigin = 0;
    std::uint64_t cameraResetAxis = 0;
    std::uint64_t viewDeltaNonpositive = 0;
    std::uint64_t viewDelta32 = 0;
    std::uint64_t viewDelta48 = 0;
    std::uint64_t viewDelta64 = 0;
    std::uint64_t viewDeltaOther = 0;
    std::int64_t minimumViewDelta = 0;
    std::int64_t maximumViewDelta = 0;
    bool haveViewDeltaRange = false;
    std::uint64_t gameplaySwaps = 0;
    double gameplaySeconds = 0.0;
    double gameplayMinimumRate = 0.0;
    double gameplayMaximumRate = 0.0;
    double gameplayWorstP99 = 0.0;
    double gameplayWorstInterval = 0.0;
    std::size_t gameplayWindows = 0;
    bool viewModelInterpolationObserved = false;
    std::uint64_t adjustedViewModels = 0;
    std::size_t maximumTrackedViewModels = 0;
    bool viewModelAnimationInterpolationObserved = false;
    std::uint64_t adjustedViewModelAnimations = 0;
    std::uint64_t interpolatedViewModelJoints = 0;
    std::size_t maximumTrackedViewModelAnimations = 0;
    std::uint64_t viewModelAnimationSkips = 0;
    bool worldInterpolationObserved = false;
    std::uint64_t adjustedWorldEntities = 0;
    std::size_t maximumTrackedWorldEntities = 0;
    std::uint64_t worldEntityResets = 0;
    std::uint64_t worldEntityCapacitySkips = 0;
    bool worldAnimationInterpolationObserved = false;
    std::uint64_t adjustedWorldAnimations = 0;
    std::uint64_t interpolatedWorldJoints = 0;
    std::size_t maximumTrackedWorldAnimations = 0;
    std::uint64_t worldAnimationSkips = 0;
    bool mouseInterpolationObserved = false;
    bool mouseHooksObserved = false;
    std::uint64_t mouseGetDataCalls = 0;
    std::uint64_t mouseGetDataEvents = 0;
    std::uint64_t mousePeekCalls = 0;
    std::uint64_t mousePeekEvents = 0;
    std::uint64_t mouseOverlaidViews = 0;
    std::uint64_t mouseSkippedState = 0;
    std::uint64_t mouseSkippedSmoothing = 0;
    std::uint64_t mousePeekFailures = 0;
    std::uint64_t mouseMoveCalls = 0;
    std::uint64_t mouseMoveChanges = 0;
    std::uint64_t mouseTrackedOverlays = 0;
    std::uint64_t mouseRetiredChanges = 0;
    std::uint64_t mouseAsyncCommands = 0;
    std::uint64_t mouseTicSelections = 0;
    std::uint64_t mouseDirectSelections = 0;
    std::uint64_t mouseCommandMisses = 0;
    std::uint64_t mouseLedgerOverflows = 0;
    for (const auto& window : windows) {
        swaps += window.swaps;
        seconds += window.seconds;
        minimumRate = std::min(minimumRate, window.rate);
        maximumRate = std::max(maximumRate, window.rate);
        worstInterval = std::max(worstInterval, window.worstMilliseconds);
        worstP99 = std::max(worstP99, window.p99Milliseconds);
        viewHookObserved = viewHookObserved || window.viewHook != 0;
        views += window.views;
        nullViews += window.nullViews;
        timeChanges += window.timeChanges;
        sameTime += window.sameTime;
        discontinuities += window.discontinuities;
        viewIdChanges += window.viewIdChanges;
        maximumOriginStep = std::max(maximumOriginStep, window.maximumOriginStep);
        cameraInterpolationObserved =
            cameraInterpolationObserved || window.cameraEnabled != 0;
        interpolatedViews += window.interpolatedViews;
        snappedViews += window.snappedViews;
        cameraResets += window.cameraResets;
        cameraResetTime += window.cameraResetTime;
        cameraResetViewId += window.cameraResetViewId;
        cameraResetFov += window.cameraResetFov;
        cameraResetStall += window.cameraResetStall;
        cameraResetOrigin += window.cameraResetOrigin;
        cameraResetAxis += window.cameraResetAxis;
        viewDeltaNonpositive += window.viewDeltaNonpositive;
        viewDelta32 += window.viewDelta32;
        viewDelta48 += window.viewDelta48;
        viewDelta64 += window.viewDelta64;
        viewDeltaOther += window.viewDeltaOther;
        const auto categorizedViewDeltas = window.viewDeltaNonpositive +
            window.viewDelta32 + window.viewDelta48 + window.viewDelta64 +
            window.viewDeltaOther;
        if (categorizedViewDeltas > 0) {
            if (!haveViewDeltaRange) {
                minimumViewDelta = window.minimumViewDelta;
                maximumViewDelta = window.maximumViewDelta;
                haveViewDeltaRange = true;
            } else {
                minimumViewDelta =
                    std::min(minimumViewDelta, window.minimumViewDelta);
                maximumViewDelta =
                    std::max(maximumViewDelta, window.maximumViewDelta);
            }
        }
        viewModelInterpolationObserved =
            viewModelInterpolationObserved || window.viewModelEnabled != 0;
        adjustedViewModels += window.adjustedViewModels;
        maximumTrackedViewModels =
            std::max(maximumTrackedViewModels, window.trackedViewModels);
        viewModelAnimationInterpolationObserved =
            viewModelAnimationInterpolationObserved ||
            window.viewModelAnimationEnabled != 0;
        adjustedViewModelAnimations += window.adjustedViewModelAnimations;
        interpolatedViewModelJoints += window.interpolatedViewModelJoints;
        maximumTrackedViewModelAnimations = std::max(
            maximumTrackedViewModelAnimations,
            window.trackedViewModelAnimations);
        viewModelAnimationSkips += window.viewModelAnimationSkips;
        worldInterpolationObserved =
            worldInterpolationObserved || window.worldEnabled != 0;
        adjustedWorldEntities += window.adjustedWorldEntities;
        maximumTrackedWorldEntities = std::max(
            maximumTrackedWorldEntities, window.trackedWorldEntities);
        worldEntityResets += window.worldEntityResets;
        worldEntityCapacitySkips += window.worldEntityCapacitySkips;
        worldAnimationInterpolationObserved =
            worldAnimationInterpolationObserved ||
            window.worldAnimationEnabled != 0;
        adjustedWorldAnimations += window.adjustedWorldAnimations;
        interpolatedWorldJoints += window.interpolatedWorldJoints;
        maximumTrackedWorldAnimations = std::max(
            maximumTrackedWorldAnimations, window.trackedWorldAnimations);
        worldAnimationSkips += window.worldAnimationSkips;
        mouseInterpolationObserved =
            mouseInterpolationObserved || window.mouseEnabled != 0;
        mouseHooksObserved = mouseHooksObserved || window.mouseHook != 0;
        if (window.views > 0) {
            mouseGetDataCalls += window.mouseGetDataCalls;
            mouseGetDataEvents += window.mouseGetDataEvents;
            mousePeekCalls += window.mousePeekCalls;
            mousePeekEvents += window.mousePeekEvents;
            mouseOverlaidViews += window.mouseOverlaidViews;
            mouseSkippedState += window.mouseSkippedState;
            mouseSkippedSmoothing += window.mouseSkippedSmoothing;
            mousePeekFailures += window.mousePeekFailures;
            mouseMoveCalls += window.mouseMoveCalls;
            mouseMoveChanges += window.mouseMoveChanges;
            mouseTrackedOverlays += window.mouseTrackedOverlays;
            mouseRetiredChanges += window.mouseRetiredChanges;
            mouseAsyncCommands += window.mouseAsyncCommands;
            mouseTicSelections += window.mouseTicSelections;
            mouseDirectSelections += window.mouseDirectSelections;
            mouseCommandMisses += window.mouseCommandMisses;
            mouseLedgerOverflows += window.mouseLedgerOverflows;
            gameplaySwaps += window.swaps;
            gameplaySeconds += window.seconds;
            gameplayWorstP99 = std::max(gameplayWorstP99, window.p99Milliseconds);
            gameplayWorstInterval =
                std::max(gameplayWorstInterval, window.worstMilliseconds);
            if (gameplayWindows == 0) {
                gameplayMinimumRate = window.rate;
                gameplayMaximumRate = window.rate;
            } else {
                gameplayMinimumRate = std::min(gameplayMinimumRate, window.rate);
                gameplayMaximumRate = std::max(gameplayMaximumRate, window.rate);
            }
            ++gameplayWindows;
        }
    }

    if (expectCameraInterpolation) {
        std::cout << "Camera interpolation self-test: " << interpolatedViews
                  << " interpolated views, " << snappedViews << " snapped views, "
                  << cameraResets << " reset events.\n";
        if (!cameraInterpolationObserved || interpolatedViews == 0) {
            std::cerr << "Camera interpolation was requested but no interpolated "
                         "gameplay view was observed.\n";
            return false;
        }
        const auto categorizedViewDeltas = viewDeltaNonpositive + viewDelta32 +
            viewDelta48 + viewDelta64 + viewDeltaOther;
        const auto resetReasonMatches = cameraResetTime + cameraResetViewId +
            cameraResetFov + cameraResetStall + cameraResetOrigin +
            cameraResetAxis;
        if (categorizedViewDeltas > 0 || resetReasonMatches > 0) {
            std::cout << "Camera reset diagnostics: time=" << cameraResetTime
                      << ", view-ID=" << cameraResetViewId
                      << ", FOV=" << cameraResetFov
                      << ", stall=" << cameraResetStall
                      << ", origin=" << cameraResetOrigin
                      << ", axis=" << cameraResetAxis
                      << " reason matches (overlap allowed). Non-16 ms view "
                         "deltas: <=0=" << viewDeltaNonpositive
                      << ", 32=" << viewDelta32
                      << ", 48=" << viewDelta48
                      << ", 64=" << viewDelta64
                      << ", other=" << viewDeltaOther;
            if (haveViewDeltaRange) {
                std::cout << ", range " << minimumViewDelta << " to "
                          << maximumViewDelta << " ms";
            }
            std::cout << ".\n";
        }
    }
    if (expectViewModelInterpolation) {
        std::cout << "Viewmodel interpolation self-test: " << adjustedViewModels
                  << " entity poses adjusted; maximum "
                  << maximumTrackedViewModels << " tracked view-only entities.\n";
        if (!viewModelInterpolationObserved || adjustedViewModels == 0) {
            std::cerr << "Viewmodel interpolation was requested but no view-only "
                         "entity pose was adjusted.\n";
            return false;
        }
    }
    if (expectViewModelAnimationInterpolation) {
        std::cout << "Viewmodel animation interpolation self-test: "
                  << adjustedViewModelAnimations
                  << " skeletal poses adjusted across "
                  << interpolatedViewModelJoints
                  << " joint matrices; maximum "
                  << maximumTrackedViewModelAnimations
                  << " tracked skeletal viewmodels; "
                  << viewModelAnimationSkips << " fail-closed skips.\n";
        if (!viewModelAnimationInterpolationObserved ||
            adjustedViewModelAnimations == 0 ||
            interpolatedViewModelJoints == 0) {
            std::cerr << "Viewmodel animation interpolation was requested but no "
                         "skeletal view-only pose was adjusted. Enter gameplay "
                         "with a visible weapon to validate this prototype.\n";
            return false;
        }
    }
    if (expectWorldInterpolation) {
        std::cout << "World interpolation self-test: "
                  << adjustedWorldEntities << " entity poses adjusted; maximum "
                  << maximumTrackedWorldEntities << " tracked world entities; "
                  << worldEntityResets << " threshold resets; "
                  << worldEntityCapacitySkips << " capacity skips.\n";
        if (!worldInterpolationObserved || adjustedWorldEntities == 0) {
            std::cerr << "World interpolation was requested but no ordinary world "
                         "entity pose was adjusted. Enter gameplay and move or "
                         "launch an entity to validate this prototype.\n";
            return false;
        }
        if (worldEntityCapacitySkips != 0) {
            std::cerr << "World interpolation exceeded a fail-closed tracking or "
                         "temporary-application capacity.\n";
            return false;
        }
    }
    if (expectWorldAnimationInterpolation) {
        std::cout << "World animation interpolation self-test: "
                  << adjustedWorldAnimations
                  << " skeletal poses adjusted across "
                  << interpolatedWorldJoints
                  << " joint matrices; maximum "
                  << maximumTrackedWorldAnimations
                  << " tracked skeletal world entities; "
                  << worldAnimationSkips << " fail-closed skips.\n";
        if (!worldAnimationInterpolationObserved ||
            adjustedWorldAnimations == 0 || interpolatedWorldJoints == 0) {
            std::cerr << "World animation interpolation was requested but no "
                         "ordinary skeletal entity pose was adjusted. Enter "
                         "gameplay near an animated character to validate this "
                         "prototype.\n";
            return false;
        }
    }
    if (expectMouseInterpolation) {
        const double overlayPercent = views > 0
            ? 100.0 * static_cast<double>(mouseOverlaidViews) /
                  static_cast<double>(views)
            : 0.0;
        std::cout << "Mouse interpolation self-test: " << mouseGetDataCalls
                  << " authoritative GetDeviceData calls returned "
                  << mouseGetDataEvents << " events; " << mousePeekCalls
                  << " presentation peeks saw " << mousePeekEvents << " events; "
                  << mouseOverlaidViews << " views received an overlay ("
                  << overlayPercent << "%); " << mouseSkippedState
                  << " state skips, " << mouseSkippedSmoothing
                  << " smoothing skips, " << mousePeekFailures
                  << " peek failures; " << mouseMoveCalls
                  << " MouseMove calls produced " << mouseMoveChanges
                  << " angular changes; " << mouseTrackedOverlays
                  << " views used consumed-delta tracking; "
                  << mouseRetiredChanges << " changes retired through "
                  << mouseTicSelections << " async-buffer selections and "
                  << mouseDirectSelections << " direct selections; "
                  << mouseAsyncCommands << " async commands tagged, "
                  << mouseCommandMisses << " command-map misses, "
                  << mouseLedgerOverflows << " ledger overflows.\n";
        if (!mouseInterpolationObserved || !mouseHooksObserved ||
            (views > 0 && mousePeekCalls == 0)) {
            std::cerr << "Mouse interpolation was requested but its observer chain "
                         "was not active during gameplay.\n";
            return false;
        }
        if (mouseLedgerOverflows > 0 || mouseCommandMisses > 0) {
            std::cerr << "Mouse interpolation lost exact command-to-camera "
                         "accounting; inspect the hook log before accepting this "
                         "run.\n";
            return false;
        }
        if (mouseMoveChanges > 0 && mouseTrackedOverlays == 0) {
            std::cerr << "Mouse movement was observed, but no presentation used "
                         "the consumed-delta overlay.\n";
            return false;
        }
    }
    const double aggregateRate = seconds > 0.0 ? swaps / seconds : 0.0;
    std::cout << "Presentation self-test: " << swaps << " swaps in " << std::fixed
              << std::setprecision(3) << seconds << " logged seconds = "
              << aggregateRate << " Hz across " << windows.size()
              << " complete windows; window range " << minimumRate << "-"
              << maximumRate << " Hz, worst p99 " << worstP99
              << " ms, worst interval " << worstInterval << " ms.\n";

    const double gameplayRate = gameplaySeconds > 0.0
        ? static_cast<double>(gameplaySwaps) / gameplaySeconds
        : 0.0;
    if (gameplayWindows > 0) {
        std::cout << "Gameplay presentation: " << gameplaySwaps << " swaps in "
                  << gameplaySeconds << " logged seconds = " << gameplayRate
                  << " Hz across " << gameplayWindows << " windows; window range "
                  << gameplayMinimumRate << "-" << gameplayMaximumRate
                  << " Hz, worst p99 " << gameplayWorstP99
                  << " ms, worst interval " << gameplayWorstInterval << " ms.\n";
    }

    if (expectViewLog) {
        std::cout << "View self-test: " << views << " SingleView calls for " << swaps
                  << " swaps; " << timeChanges << " time changes, " << sameTime
                  << " repeated-time views, " << discontinuities
                  << " time discontinuities, " << viewIdChanges
                  << " view-ID changes, " << nullViews << " null views, maximum "
                  << "origin step " << maximumOriginStep << ".\n";
        if (!viewHookObserved) {
            std::cerr << "The requested read-only SingleView hook was not installed "
                         "during the self-test.\n";
            return false;
        }
        if (views == 0) {
            std::cerr << "The SingleView hook was installed, but no gameplay view was "
                         "rendered. A menu-only run is inconclusive; enter a level to "
                         "validate the view boundary.\n";
            return false;
        }
    }

    if (expectedCap == 0) return true;
    const double validationRate = gameplayWindows > 0 ? gameplayRate : aggregateRate;
    const double tolerance = static_cast<double>(expectedCap) * 0.10;
    if (validationRate < expectedCap - tolerance ||
        validationRate > expectedCap + tolerance) {
        std::cerr << "Presentation rate is outside 10% of the requested "
                  << expectedCap << " Hz cap.\n";
        return false;
    }
    return true;
}

BOOL CALLBACK RequestCloseForProcess(HWND window, LPARAM parameter) {
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(window, &windowProcessId);
    if (windowProcessId == static_cast<DWORD>(parameter)) {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

bool StopTestProcess(PROCESS_INFORMATION& process) {
    if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) return true;
    EnumWindows(&RequestCloseForProcess, static_cast<LPARAM>(process.dwProcessId));
    if (WaitForSingleObject(process.hProcess, 5'000) == WAIT_OBJECT_0) {
        std::cout << "Self-test process shut down normally after WM_CLOSE.\n";
        return true;
    }
    std::cerr << "Self-test process did not exit after WM_CLOSE; terminating only "
                 "the process created by this launcher.\n";
    if (!TerminateProcess(process.hProcess, 0xE104)) return false;
    return WaitForSingleObject(process.hProcess, 5'000) == WAIT_OBJECT_0;
}

int Run(const Options& options) {
    const fs::path executable = options.gameDirectory / L"prey.exe";
    const fs::path gameDll = options.gameDirectory / L"base" / L"gamex86.dll";
    const auto fileBytes = ReadFileBytes(executable);
    if (!fileBytes) {
        std::wcerr << L"Could not read " << executable << L"\n";
        return 2;
    }
    const auto hash = Sha256(*fileBytes);
    if (!hash || *hash != kSupportedExeSha256) {
        std::cerr << "Unsupported prey.exe SHA-256: " << hash.value_or("unavailable")
                  << "\nNo process was started and no patch was applied.\n";
        return 3;
    }
    const auto gameDllBytes = ReadFileBytes(gameDll);
    const auto gameDllHash = gameDllBytes ? Sha256(*gameDllBytes) : std::nullopt;
    if (!gameDllHash || *gameDllHash != kSupportedGameDllSha256) {
        std::cerr << "Unsupported base/gamex86.dll SHA-256: "
                  << gameDllHash.value_or("unavailable")
                  << "\nNo process was started and no patch was applied.\n";
        return 3;
    }
    const auto pe = ParsePe(*fileBytes);
    if (!pe) {
        std::cerr << "Could not parse the supported executable's PE metadata.\n";
        return 4;
    }

    const auto launcherPath = CurrentExecutablePath();
    if (!launcherPath) {
        std::cerr << "Could not resolve the launcher's own path.\n";
        return 5;
    }
    const fs::path launcherDirectory = launcherPath->parent_path();
    const fs::path hookPath = launcherDirectory / L"PreyHFRHook.dll";
    if (options.patchEnabled && !options.dryRun && !fs::is_regular_file(hookPath)) {
        std::wcerr << L"The required colocated hook DLL was not found: " << hookPath
                   << L"\nNo process was started.\n";
        return 5;
    }

    Options launchOptions = options;
    if (!options.patchEnabled) {
        launchOptions.borderless = false;
        launchOptions.displayMode = DisplayMode::Unspecified;
        launchOptions.vSync.reset();
        launchOptions.resolution.reset();
    }
    const LaunchConfiguration launch =
        BuildLaunchConfiguration(executable, launchOptions, launcherDirectory);
    std::optional<std::vector<wchar_t>> childEnvironment;
    if (options.patchEnabled && !options.dryRun) {
        childEnvironment = BuildChildEnvironment(options.framesPerSecond, launch.hookLog,
                                                 options.viewLog,
                                                 options.cameraInterpolation,
                                                 options.viewModelInterpolation,
                                                 options.viewModelAnimationInterpolation,
                                                 options.worldInterpolation,
                                                 options.worldAnimationInterpolation,
                                                 options.maximumWorldEntityDistance,
                                                 options.maximumWorldEntityAngle,
                                                 options.mouseInterpolation,
                                                 options.continuousSnapshotTiming,
                                                 options.multiTicEntityAlignment,
                                                 options.overdueSnapshotFallback,
                                                 options.interpolationTrace,
                                                 options.timelineResetVirtualKey,
                                                 options.borderless,
                                                 options.resolution);
        if (!childEnvironment) {
            std::cerr << "Could not build the temporary child environment.\n";
            return 5;
        }
    }
    std::vector<wchar_t> mutableCommand(launch.commandLine.begin(),
                                        launch.commandLine.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT |
        (!options.patchEnabled || options.dryRun ? 0 : CREATE_SUSPENDED);
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr,
                        FALSE, creationFlags,
                        childEnvironment ? childEnvironment->data() : nullptr,
                        options.gameDirectory.c_str(), &startup, &process)) {
        std::wcerr << L"CreateProcess failed with error " << GetLastError() << L"\n";
        return 5;
    }

    std::wcout << L"Started supported Prey 1.4 process " << process.dwProcessId;
    if (options.patchEnabled && !options.dryRun) {
        std::wcout << L" with its primary thread suspended";
    }
    std::wcout << L".\n";
    if (!options.patchEnabled) {
        std::cout << "Patch disabled by configuration; launcher is detaching while "
                     "the unmodified game continues.\n";
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    if (!options.dryRun) {
        if (!InjectLibrary(process.hProcess, process.dwProcessId, hookPath,
                           options.scanTimeoutMilliseconds)) {
            std::cerr << "Fail-closed: PreyHFRHook.dll could not be injected and "
                         "verified. The suspended child will be terminated.\n"
                      << "Hook log: " << launch.hookLog.string() << "\n";
            TerminateProcess(process.hProcess, 0xE105);
            WaitForSingleObject(process.hProcess, 5'000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 6;
        }
        std::cout << "Injected and verified PreyHFRHook.dll; requested presentation "
                  << "cap is " << options.framesPerSecond << " Hz; read-only view "
                  << "logging is " << (options.viewLog ? "enabled" : "disabled")
                  << "; camera interpolation is "
                  << (options.cameraInterpolation ? "enabled" : "disabled")
                  << "; viewmodel interpolation is "
                  << (options.viewModelInterpolation ? "enabled" : "disabled")
                  << "; viewmodel animation interpolation is "
                  << (options.viewModelAnimationInterpolation
                          ? "enabled"
                          : "disabled")
                  << "; world interpolation is "
                  << (options.worldInterpolation ? "enabled" : "disabled")
                  << "; world animation interpolation is "
                  << (options.worldAnimationInterpolation
                          ? "enabled"
                          : "disabled")
                  << "; mouse interpolation is "
                  << (options.mouseInterpolation ? "enabled" : "disabled")
                  << "; continuous snapshot timing is "
                  << (options.continuousSnapshotTiming ? "enabled" : "disabled")
                  << "; multi-tic entity alignment is "
                  << (options.multiTicEntityAlignment ? "enabled" : "disabled")
                  << "; overdue snapshot fallback is "
                  << (options.overdueSnapshotFallback ? "enabled" : "disabled")
                  << "; interpolation trace is "
                  << (options.interpolationTrace ? "enabled" : "disabled")
                  << "; timeline reset key is "
                  << TimelineResetKeyName(options.timelineResetVirtualKey)
                  << "; borderless fullscreen is "
                  << (options.borderless ? "enabled" : "disabled")
                  << "; display mode is "
                  << (options.borderless
                          ? "borderless"
                          : options.displayMode == DisplayMode::Windowed
                                ? "windowed"
                                : options.displayMode == DisplayMode::Exclusive
                                      ? "exclusive"
                                      : "game default")
                  << "; V-Sync override is "
                  << (options.vSync ? (*options.vSync ? "on" : "off")
                                    : "disabled")
                  << "; resolution override is "
                  << (options.resolution
                          ? std::to_string(options.resolution->width) + "x" +
                                std::to_string(options.resolution->height)
                          : "disabled")
                  << ".\n";
    } else {
        std::cout << "Dry run: hook injection and suspended startup are intentionally "
                     "skipped.\n";
    }
    if (!options.dryRun &&
        ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        std::cerr << "Could not resume the primary thread; terminating the child.\n";
        TerminateProcess(process.hProcess, 0xE106);
        WaitForSingleObject(process.hProcess, 5'000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 6;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(options.scanTimeoutMilliseconds);
    std::optional<Match> match;
    bool alreadyPatched = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
        if (const auto module = FindMainModule(process.dwProcessId)) {
            match = FindWaitGate(process.hProcess, *module, *pe, alreadyPatched);
            if (match) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!match) {
        std::cerr << "The validated timing signature did not resolve uniquely; no patch "
                     "was applied.\n";
        if (!options.dryRun || options.testSeconds > 0) {
            TerminateProcess(process.hProcess, 0xE101);
            WaitForSingleObject(process.hProcess, 5'000);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 6;
    }

    std::cout << "Timing gate resolved at VA 0x" << std::hex << match->address
              << "; com_ticNumber at 0x" << match->comTicAddress
              << "; com_fixedTic object pointer at 0x"
              << match->fixedTicObjectPointerAddress << std::dec << ".\n";

    std::optional<ClockSample> stableClock;
    if (options.testSeconds > 0) {
        std::cout << "Waiting for the native asynchronous clock to stabilize.\n";
        stableClock = WaitForStableClock(process.hProcess, match->comTicAddress,
                                         options.scanTimeoutMilliseconds);
        if (!stableClock) {
            std::cerr << "The asynchronous clock did not reach 50 Hz in a one-second "
                         "window; no timing conclusion is valid.\n";
            TerminateProcess(process.hProcess, 0xE103);
            WaitForSingleObject(process.hProcess, 5'000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 8;
        }
    }

    if (options.dryRun) {
        std::cout << "Dry run requested; signature validated and no bytes changed.\n";
    } else if (alreadyPatched) {
        std::cout << "The render-wait gate was already patched.\n";
    } else if (!ApplyWaitPatch(process.hProcess, *match)) {
        std::cerr << "Patch write or verification failed; terminating the child to "
                     "avoid a partial hook-only state.\n";
        TerminateProcess(process.hProcess, 0xE102);
        WaitForSingleObject(process.hProcess, 5'000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 7;
    } else {
        std::cout << "Removed the render wait; native simulation forcing remains disabled.\n";
    }

    if (options.testSeconds > 0) {
        const auto measurementTic = ReadRemote<std::uint32_t>(
            process.hProcess, match->comTicAddress);
        if (!measurementTic) {
            std::cerr << "Could not read the simulation clock at measurement start.\n";
            TerminateProcess(process.hProcess, 0xE107);
            WaitForSingleObject(process.hProcess, 5'000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 8;
        }
        const auto startTic = *measurementTic;
        const auto start = std::chrono::steady_clock::now();
        LARGE_INTEGER presentationStart{};
        QueryPerformanceCounter(&presentationStart);
        const DWORD testMilliseconds = options.testSeconds * 1000;
        const DWORD waitResult = WaitForSingleObject(process.hProcess, testMilliseconds);
        const auto stop = std::chrono::steady_clock::now();
        LARGE_INTEGER presentationStop{};
        QueryPerformanceCounter(&presentationStop);
        const auto stopTic = ReadRemote<std::uint32_t>(process.hProcess,
                                                       match->comTicAddress);
        bool selfTestSucceeded = false;
        if (waitResult == WAIT_TIMEOUT && stopTic) {
            const double elapsed = std::chrono::duration<double>(stop - start).count();
            const auto tickDelta = *stopTic - startTic;
            std::cout << "Self-test: " << tickDelta << " async tics in " << std::fixed
                      << std::setprecision(3) << elapsed << " seconds = "
                      << tickDelta / elapsed << " Hz.\n";
            selfTestSucceeded = true;
        } else {
            std::cerr << "The process exited or tic counters became unreadable during "
                         "the self-test.\n";
        }
        if (!StopTestProcess(process)) {
            std::cerr << "The self-test process could not be stopped.\n";
            selfTestSucceeded = false;
        }
        if (!options.dryRun) {
            selfTestSucceeded =
                ReportPresentationTest(launch.hookLog, presentationStart.QuadPart,
                                       presentationStop.QuadPart,
                                       options.framesPerSecond,
                                       options.viewLog || options.cameraInterpolation,
                                       options.cameraInterpolation,
                                       options.viewModelInterpolation,
                                       options.viewModelAnimationInterpolation,
                                       options.worldInterpolation,
                                       options.worldAnimationInterpolation,
                                       options.mouseInterpolation,
                                       options.borderless) &&
                selfTestSucceeded;
            std::wcout << L"Hook log: " << launch.hookLog << L"\n";
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return selfTestSucceeded ? 0 : 9;
    } else {
        if (options.dryRun) {
            std::cout << "Dry-run validation complete; launcher is detaching while "
                         "the unmodified game continues.\n";
        } else {
            std::cout << "Patch installed; launcher is detaching while the game "
                         "continues.\n";
        }
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        Options defaults;
        bool helpRequested = false;
        bool versionRequested = false;
        for (int index = 1; index < argc; ++index) {
            if (std::wstring_view(argv[index]) == L"--help") {
                helpRequested = true;
                break;
            }
            if (std::wstring_view(argv[index]) == L"--version") {
                versionRequested = true;
                break;
            }
            if (std::wstring_view(argv[index]) == L"--") break;
        }
        if (!helpRequested && !versionRequested) {
            const auto launcherPath = CurrentExecutablePath();
            if (!launcherPath) {
                std::cerr << "Could not resolve the launcher's own path.\n";
                return 5;
            }
            defaults.gameDirectory = launcherPath->parent_path();
            bool configExplicitlyRequested = false;
            const auto configPath = SelectConfigPath(
                argc, argv, launcherPath->parent_path(), configExplicitlyRequested);
            if (configPath && (configExplicitlyRequested || fs::exists(*configPath))) {
                std::string error;
                if (!LoadConfiguration(*configPath, defaults, error)) {
                    std::cerr << "Configuration error: " << error
                              << "\nNo process was started.\n";
                    return 1;
                }
                std::wcout << L"Loaded configuration: " << *configPath << L"\n";
            }
        }
        const auto options = ParseOptions(argc, argv, defaults);
        if (!options) {
            return (helpRequested || versionRequested) ? 0 : 1;
        }
        if (options->validateConfiguration) {
            std::cout << "Configuration is valid: patch="
                      << (options->patchEnabled ? "enabled" : "disabled")
                      << "; presentation_fps=" << options->framesPerSecond
                      << "; simulation_hz=native; camera="
                      << (options->cameraInterpolation ? "on" : "off")
                      << "; viewmodel="
                      << (options->viewModelInterpolation ? "on" : "off")
                      << "; viewmodel_animation="
                      << (options->viewModelAnimationInterpolation ? "on" : "off")
                      << "; world="
                      << (options->worldInterpolation ? "on" : "off")
                      << "; world_animation="
                      << (options->worldAnimationInterpolation ? "on" : "off")
                      << "; mouse="
                      << (options->mouseInterpolation ? "on" : "off")
                      << "; continuous_snapshot_timing="
                      << (options->continuousSnapshotTiming ? "on" : "off")
                      << "; multi_tic_entity_alignment="
                      << (options->multiTicEntityAlignment ? "on" : "off")
                      << "; overdue_snapshot_fallback="
                      << (options->overdueSnapshotFallback ? "on" : "off")
                      << "; interpolation_trace="
                      << (options->interpolationTrace ? "on" : "off")
                      << "; timeline_reset_key="
                      << TimelineResetKeyName(options->timelineResetVirtualKey)
                      << "; borderless=" << (options->borderless ? "on" : "off")
                      << "; display_mode="
                      << (options->borderless
                              ? "borderless"
                              : options->displayMode == DisplayMode::Windowed
                                    ? "windowed"
                                    : options->displayMode == DisplayMode::Exclusive
                                          ? "exclusive"
                                          : "game_default")
                      << "; vsync="
                      << (options->vSync ? (*options->vSync ? "on" : "off")
                                        : "game_default")
                      << ".\n";
            return 0;
        }
        return Run(*options);
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 10;
    }
}
