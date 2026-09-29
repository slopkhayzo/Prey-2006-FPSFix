#include "config.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace preyhfr {
namespace {

struct ConfigEntry {
    std::string value;
    std::size_t line = 0;
};

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
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return static_cast<char>(character);
    });
    return value;
}

std::optional<bool> ParseBoolean(const std::string& value) {
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

std::optional<Resolution> ParseResolution(std::string value) {
    value = LowerAscii(Trim(std::move(value)));
    const auto separator = value.find('x');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= value.size() ||
        value.find('x', separator + 1) != std::string::npos) {
        return std::nullopt;
    }
    const std::string widthText = value.substr(0, separator);
    const std::string heightText = value.substr(separator + 1);
    const auto decimalDigitsOnly = [](const std::string& text) {
        return !text.empty() &&
            std::all_of(text.begin(), text.end(), [](unsigned char character) {
                return character >= '0' && character <= '9';
            });
    };
    if (!decimalDigitsOnly(widthText) || !decimalDigitsOnly(heightText) ||
        widthText.size() > 5 || heightText.size() > 5) {
        return std::nullopt;
    }
    try {
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
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const char* DisplayModeName(DisplayMode mode) {
    switch (mode) {
    case DisplayMode::GameDefault: return "game_default";
    case DisplayMode::Windowed: return "windowed";
    case DisplayMode::Exclusive: return "exclusive";
    }
    return "unknown";
}

const char* VSyncModeName(VSyncMode mode) {
    switch (mode) {
    case VSyncMode::GameDefault: return "game_default";
    case VSyncMode::Off: return "off";
    case VSyncMode::On: return "on";
    }
    return "unknown";
}

} // namespace

bool LoadConfig(const std::filesystem::path& path, Config& config,
                std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "could not open configuration file: " + path.string();
        return false;
    }

    std::map<std::string, ConfigEntry> values;
    std::string section;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xef &&
            static_cast<unsigned char>(line[1]) == 0xbb &&
            static_cast<unsigned char>(line[2]) == 0xbf) {
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
        if (!values.emplace(key, ConfigEntry{value, lineNumber}).second) {
            error = "duplicate key " + key + " at line " +
                    std::to_string(lineNumber);
            return false;
        }
    }
    if (!file.eof()) {
        error = "failed while reading configuration file: " + path.string();
        return false;
    }

    constexpr std::array<std::string_view, 22> knownKeys{
        "patch.enabled", "patch.presentation_fps", "patch.simulation_hz",
        "interpolation.enabled", "interpolation.camera",
        "interpolation.viewmodel", "interpolation.viewmodel_animation",
        "interpolation.world", "interpolation.world_animation",
        "interpolation.mouse", "interpolation.world_max_distance",
        "interpolation.world_max_angle",
        "compatibility.continuous_snapshot_timing",
        "compatibility.multi_tic_entity_alignment",
        "compatibility.overdue_snapshot_fallback",
        "diagnostics.view_log", "diagnostics.interpolation_trace",
        "diagnostics.timeline_reset_key", "display.borderless",
        "display.mode", "display.vsync", "display.resolution",
    };
    for (const auto& [key, entry] : values) {
        if (std::find(knownKeys.begin(), knownKeys.end(), key) == knownKeys.end()) {
            error = "unknown key " + key + " at line " +
                    std::to_string(entry.line);
            return false;
        }
    }

    const auto readBoolean = [&](const char* key, bool& destination) {
        const auto found = values.find(key);
        if (found == values.end()) return true;
        const auto parsed = ParseBoolean(found->second.value);
        if (!parsed) {
            error = std::string("invalid boolean for ") + key + " at line " +
                    std::to_string(found->second.line);
            return false;
        }
        destination = *parsed;
        return true;
    };

    if (!readBoolean("patch.enabled", config.patchEnabled)) return false;
    const auto simulation = values.find("patch.simulation_hz");
    if (simulation != values.end() &&
        LowerAscii(Trim(simulation->second.value)) != "native") {
        error = "patch.simulation_hz must be native at line " +
                std::to_string(simulation->second.line);
        return false;
    }

    const auto cap = values.find("patch.presentation_fps");
    if (cap != values.end()) {
        const std::string normalized = LowerAscii(Trim(cap->second.value));
        if (normalized == "desktop" || normalized == "refresh") {
            config.frameCapFromDesktop = true;
        } else {
            try {
                std::size_t parsed = 0;
                const unsigned long value = std::stoul(cap->second.value, &parsed);
                if (parsed != cap->second.value.size() || !IsValidFrameCap(value)) {
                    throw std::invalid_argument("range");
                }
                config.framesPerSecond = static_cast<unsigned int>(value);
                config.frameCapFromDesktop = false;
            } catch (const std::exception&) {
                error = "patch.presentation_fps must be desktop, 0, or 30..1000 at line " +
                        std::to_string(cap->second.line);
                return false;
            }
        }
    }

    bool interpolationEnabled = false;
    if (!readBoolean("interpolation.enabled", interpolationEnabled)) return false;
    if (values.contains("interpolation.enabled")) {
        config.cameraInterpolation = interpolationEnabled;
        config.viewModelInterpolation = interpolationEnabled;
        config.viewModelAnimationInterpolation = interpolationEnabled;
        config.worldInterpolation = interpolationEnabled;
        config.worldAnimationInterpolation = interpolationEnabled;
        config.mouseInterpolation = interpolationEnabled;
    }
    if (!readBoolean("interpolation.camera", config.cameraInterpolation) ||
        !readBoolean("interpolation.viewmodel", config.viewModelInterpolation) ||
        !readBoolean("interpolation.viewmodel_animation",
                     config.viewModelAnimationInterpolation) ||
        !readBoolean("interpolation.world", config.worldInterpolation) ||
        !readBoolean("interpolation.world_animation",
                     config.worldAnimationInterpolation) ||
        !readBoolean("interpolation.mouse", config.mouseInterpolation) ||
        !readBoolean("compatibility.continuous_snapshot_timing",
                     config.continuousSnapshotTiming) ||
        !readBoolean("compatibility.multi_tic_entity_alignment",
                     config.multiTicEntityAlignment) ||
        !readBoolean("compatibility.overdue_snapshot_fallback",
                     config.overdueSnapshotFallback) ||
        !readBoolean("diagnostics.view_log", config.viewLog) ||
        !readBoolean("diagnostics.interpolation_trace",
                     config.interpolationTrace) ||
        !readBoolean("display.borderless", config.borderless)) {
        return false;
    }

    const auto resetKey = values.find("diagnostics.timeline_reset_key");
    if (resetKey != values.end()) {
        const auto parsed = ParseTimelineResetKey(resetKey->second.value);
        if (!parsed) {
            error = "diagnostics.timeline_reset_key must be F1..F24 or none at line " +
                    std::to_string(resetKey->second.line);
            return false;
        }
        config.timelineResetVirtualKey = *parsed;
    }

    const auto readDouble = [&](const char* key, double minimum, double maximum,
                                double& destination) {
        const auto found = values.find(key);
        if (found == values.end()) return true;
        try {
            std::size_t parsed = 0;
            const double value = std::stod(found->second.value, &parsed);
            if (parsed != found->second.value.size() || !std::isfinite(value) ||
                value < minimum || value > maximum) {
                throw std::invalid_argument("range");
            }
            destination = value;
            return true;
        } catch (const std::exception&) {
            error = std::string(key) + " is outside its supported range at line " +
                    std::to_string(found->second.line);
            return false;
        }
    };
    if (!readDouble("interpolation.world_max_distance", 1.0, 4096.0,
                    config.maximumWorldEntityDistance) ||
        !readDouble("interpolation.world_max_angle", 1.0, 180.0,
                    config.maximumWorldEntityAngle)) {
        return false;
    }

    const auto displayMode = values.find("display.mode");
    if (displayMode != values.end()) {
        const std::string normalized = LowerAscii(Trim(displayMode->second.value));
        if (normalized == "game" || normalized == "default" ||
            normalized == "game_default") {
            config.displayMode = DisplayMode::GameDefault;
        } else if (normalized == "windowed" || normalized == "window") {
            config.displayMode = DisplayMode::Windowed;
        } else if (normalized == "exclusive" || normalized == "fullscreen") {
            config.displayMode = DisplayMode::Exclusive;
        } else {
            error = "display.mode must be game, windowed, or exclusive at line " +
                    std::to_string(displayMode->second.line);
            return false;
        }
    }

    const auto vSync = values.find("display.vsync");
    if (vSync != values.end()) {
        const std::string normalized = LowerAscii(Trim(vSync->second.value));
        if (normalized.empty() || normalized == "game" ||
            normalized == "default" || normalized == "game_default") {
            config.vSync = VSyncMode::GameDefault;
        } else if (normalized == "on" || normalized == "true" ||
                   normalized == "1") {
            config.vSync = VSyncMode::On;
        } else if (normalized == "off" || normalized == "false" ||
                   normalized == "0") {
            config.vSync = VSyncMode::Off;
        } else {
            error = "display.vsync must be game, on, or off at line " +
                    std::to_string(vSync->second.line);
            return false;
        }
    }

    const auto resolution = values.find("display.resolution");
    if (resolution != values.end()) {
        const std::string normalized = LowerAscii(Trim(resolution->second.value));
        if (normalized == "desktop") {
            config.borderless = true;
        } else if (!normalized.empty() && normalized != "game" &&
                   normalized != "default" && normalized != "game_default") {
            config.resolution = ParseResolution(normalized);
            if (!config.resolution) {
                error = "display.resolution must be blank, game, desktop, or WIDTHxHEIGHT at line " +
                        std::to_string(resolution->second.line);
                return false;
            }
        }
    }

    const auto requireCamera = [&](bool enabled, const char* key) {
        if (enabled && !config.cameraInterpolation) {
            error = std::string(key) + " requires interpolation.camera=true";
            return false;
        }
        return true;
    };
    if (!requireCamera(config.viewModelInterpolation, "interpolation.viewmodel") ||
        !requireCamera(config.viewModelAnimationInterpolation,
                       "interpolation.viewmodel_animation") ||
        !requireCamera(config.worldInterpolation, "interpolation.world") ||
        !requireCamera(config.worldAnimationInterpolation,
                       "interpolation.world_animation") ||
        !requireCamera(config.mouseInterpolation, "interpolation.mouse") ||
        !requireCamera(config.interpolationTrace,
                       "diagnostics.interpolation_trace")) {
        return false;
    }
    if (config.multiTicEntityAlignment && !config.continuousSnapshotTiming) {
        error = "compatibility.multi_tic_entity_alignment requires continuous_snapshot_timing";
        return false;
    }
    if (config.overdueSnapshotFallback && !config.multiTicEntityAlignment) {
        error = "compatibility.overdue_snapshot_fallback requires multi_tic_entity_alignment";
        return false;
    }
    if (config.borderless && config.displayMode == DisplayMode::Exclusive) {
        error = "display.borderless cannot be combined with display.mode=exclusive";
        return false;
    }
    return true;
}

bool ResolveDesktopConfig(Config& config, std::string& error) {
    if (!config.frameCapFromDesktop && !config.borderless) return true;

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) ||
        mode.dmPelsWidth == 0 || mode.dmPelsHeight == 0) {
        error = "could not determine the current primary-desktop display mode";
        return false;
    }
    if (config.frameCapFromDesktop) {
        if (!IsValidFrameCap(mode.dmDisplayFrequency)) {
            error = "primary-desktop refresh rate is outside 30..1000 Hz";
            return false;
        }
        config.framesPerSecond = mode.dmDisplayFrequency;
    }
    if (config.borderless) {
        const Resolution desktop{mode.dmPelsWidth, mode.dmPelsHeight};
        if (config.resolution &&
            (config.resolution->width != desktop.width ||
             config.resolution->height != desktop.height)) {
            error = "borderless renderer resolution must match the primary desktop";
            return false;
        }
        config.resolution = desktop;
        config.displayMode = DisplayMode::Windowed;
    }
    return true;
}

std::string TimelineResetKeyName(unsigned int virtualKey) {
    if (virtualKey == 0) return "none";
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return "F" + std::to_string(virtualKey - VK_F1 + 1);
    }
    return "unknown";
}

std::string DescribeConfig(const Config& config) {
    std::ostringstream result;
    result << "patch=" << (config.patchEnabled ? "enabled" : "disabled")
           << "; presentation_fps=" << config.framesPerSecond
           << "; simulation_hz=native"
           << "; camera=" << (config.cameraInterpolation ? "on" : "off")
           << "; viewmodel=" << (config.viewModelInterpolation ? "on" : "off")
           << "; viewmodel_animation="
           << (config.viewModelAnimationInterpolation ? "on" : "off")
           << "; world=" << (config.worldInterpolation ? "on" : "off")
           << "; world_animation="
           << (config.worldAnimationInterpolation ? "on" : "off")
           << "; mouse=" << (config.mouseInterpolation ? "on" : "off")
           << "; continuous_snapshot_timing="
           << (config.continuousSnapshotTiming ? "on" : "off")
           << "; multi_tic_entity_alignment="
           << (config.multiTicEntityAlignment ? "on" : "off")
           << "; overdue_snapshot_fallback="
           << (config.overdueSnapshotFallback ? "on" : "off")
           << "; interpolation_trace="
           << (config.interpolationTrace ? "on" : "off")
           << "; timeline_reset_key="
           << TimelineResetKeyName(config.timelineResetVirtualKey)
           << "; display_mode="
           << (config.borderless ? "borderless" : DisplayModeName(config.displayMode))
           << "; vsync=" << VSyncModeName(config.vSync)
           << "; resolution=";
    if (config.resolution) {
        result << config.resolution->width << 'x' << config.resolution->height;
    } else {
        result << "game_default";
    }
    return result.str();
}

} // namespace preyhfr
