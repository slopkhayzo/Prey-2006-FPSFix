#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace preyhfr {

struct Resolution {
    unsigned int width = 0;
    unsigned int height = 0;
};

enum class DisplayMode {
    GameDefault,
    Windowed,
    Exclusive,
};

enum class VSyncMode {
    GameDefault,
    Off,
    On,
};

struct Config {
    bool patchEnabled = true;
    unsigned int framesPerSecond = 240;
    bool frameCapFromDesktop = false;
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
    bool bufferedTwoTicInterpolation = false;
    bool interpolationTrace = false;
    unsigned int timelineResetVirtualKey = 0x79; // VK_F10
    bool borderless = false;
    DisplayMode displayMode = DisplayMode::GameDefault;
    VSyncMode vSync = VSyncMode::GameDefault;
    std::optional<Resolution> resolution;
};

bool LoadConfig(const std::filesystem::path& path, Config& config,
                std::string& error);
bool ResolveDesktopConfig(Config& config, std::string& error);
std::string DescribeConfig(const Config& config);
std::string TimelineResetKeyName(unsigned int virtualKey);

} // namespace preyhfr
