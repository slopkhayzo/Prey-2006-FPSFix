#include "config.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: PreyHFRConfigTest <PreyHFR.ini>\n";
        return 2;
    }

    preyhfr::Config config;
    std::string error;
    if (!preyhfr::LoadConfig(argv[1], config, error) ||
        !preyhfr::ResolveDesktopConfig(config, error)) {
        std::cerr << "Configuration error: " << error << '\n';
        return 1;
    }
    std::cout << "Configuration is valid: "
              << preyhfr::DescribeConfig(config) << '\n';
    return 0;
}
