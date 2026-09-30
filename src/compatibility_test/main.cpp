#include "compatibility.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::cerr << "Usage: PreyHFRCompatibilityTest prey.exe gamex86.dll\n";
        return 2;
    }
    preyhfr::CompatibilityRequirements requirements;
    requirements.displayOverrides = true;
    requirements.viewHook = true;
    requirements.cameraHook = true;
    requirements.entityHooks = true;
    requirements.animationHooks = true;
    requirements.mouseHooks = true;
    requirements.asyncClockHooks = true;

    std::string error;
    if (!preyhfr::ValidateExecutableLayout(fs::path(argv[1]), requirements,
                                           error)) {
        std::cerr << "prey.exe incompatible: " << error << "\n";
        return 1;
    }
    if (!preyhfr::ValidateGameModuleLayout(fs::path(argv[2]), requirements,
                                           error)) {
        std::cerr << "gamex86.dll incompatible: " << error << "\n";
        return 1;
    }
    std::cout << "compatible x86 layouts and configured hook paths validated\n";
    return 0;
}
