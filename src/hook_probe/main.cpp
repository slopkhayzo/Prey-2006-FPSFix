#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

using TestPresentationFn = void(WINAPI*)();
using TestMouseAxisFn = BOOL(WINAPI*)();
using TestInterpolationCadenceFn = BOOL(WINAPI*)();
using TestBorderlessFn = BOOL(WINAPI*)(HDC);

// Keep a real GDI32!SwapBuffers import in the probe so the hook exercises the
// same main-module IAT lookup used by Prey.
BOOL(WINAPI* volatile g_importedSwapBuffers)(HDC) = &SwapBuffers;

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
        buffer.resize((std::min)(static_cast<std::size_t>(32'768), buffer.size() * 2));
    }
}

bool RectanglesEqual(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto printUsage = [] {
        std::wcout << L"Usage: PreyHFRHookProbe [0|30..1000] [seconds]\n";
    };
    if (argc > 1 && std::wstring_view(argv[1]) == L"--help") {
        printUsage();
        return 0;
    }
    if (argc > 3) {
        printUsage();
        return 1;
    }
    unsigned int cap = 240;
    unsigned int seconds = 4;
    try {
        std::size_t parsed = 0;
        if (argc > 1) {
            cap = std::stoul(argv[1], &parsed);
            if (parsed != std::wcslen(argv[1])) throw std::invalid_argument("cap");
        }
        if (argc > 2) {
            seconds = std::stoul(argv[2], &parsed);
            if (parsed != std::wcslen(argv[2]) || seconds == 0 || seconds > 3600) {
                throw std::invalid_argument("seconds");
            }
        }
    } catch (const std::exception&) {
        printUsage();
        return 1;
    }
    if (cap != 0 && (cap < 30 || cap > 1000)) {
        printUsage();
        return 1;
    }

    const auto executable = CurrentExecutablePath();
    if (!executable) {
        std::cerr << "Could not resolve the probe path.\n";
        return 2;
    }
    const fs::path directory = executable->parent_path();
    const fs::path hook = directory / L"PreyHFR.asi";
    const fs::path log = directory / (L"PreyHFRHookProbe-" + std::to_wstring(cap) +
                                      L".log");
    DeleteFileW(log.c_str());
    SetEnvironmentVariableW(L"PREYHFR_PROBE", L"1");
    SetEnvironmentVariableW(L"PREYHFR_CAP", std::to_wstring(cap).c_str());
    SetEnvironmentVariableW(L"PREYHFR_LOG", log.c_str());
    SetEnvironmentVariableW(L"PREYHFR_CONTINUOUS_SNAPSHOT_TIMING", L"1");
    SetEnvironmentVariableW(L"PREYHFR_MULTI_TIC_ENTITY_ALIGNMENT", L"1");
    SetEnvironmentVariableW(L"PREYHFR_OVERDUE_SNAPSHOT_FALLBACK", L"1");
    SetEnvironmentVariableW(L"PREYHFR_BORDERLESS", L"1");
    SetProcessDPIAware();
    SetEnvironmentVariableW(
        L"PREYHFR_RENDER_WIDTH",
        std::to_wstring(GetSystemMetrics(SM_CXSCREEN)).c_str());
    SetEnvironmentVariableW(
        L"PREYHFR_RENDER_HEIGHT",
        std::to_wstring(GetSystemMetrics(SM_CYSCREEN)).c_str());

    const HMODULE module = LoadLibraryW(hook.c_str());
    if (module == nullptr) {
        std::cerr << "LoadLibraryW failed with error " << GetLastError() << ".\n";
        return 3;
    }

    FARPROC testExport = GetProcAddress(module, "PreyHFRTestPresentation");
    if (testExport == nullptr) {
        testExport = GetProcAddress(module, "_PreyHFRTestPresentation@0");
    }
    const auto testPresentation = reinterpret_cast<TestPresentationFn>(testExport);
    FARPROC mouseAxisExport =
        GetProcAddress(module, "PreyHFRTestGravityMouseAxis");
    if (mouseAxisExport == nullptr) {
        mouseAxisExport =
            GetProcAddress(module, "_PreyHFRTestGravityMouseAxis@0");
    }
    const auto testMouseAxis = reinterpret_cast<TestMouseAxisFn>(mouseAxisExport);
    FARPROC interpolationCadenceExport =
        GetProcAddress(module, "PreyHFRTestInterpolationCadence");
    if (interpolationCadenceExport == nullptr) {
        interpolationCadenceExport =
            GetProcAddress(module, "_PreyHFRTestInterpolationCadence@0");
    }
    const auto testInterpolationCadence =
        reinterpret_cast<TestInterpolationCadenceFn>(
            interpolationCadenceExport);
    FARPROC borderlessExport =
        GetProcAddress(module, "PreyHFRTestBorderless");
    if (borderlessExport == nullptr) {
        borderlessExport =
            GetProcAddress(module, "_PreyHFRTestBorderless@4");
    }
    const auto testBorderless =
        reinterpret_cast<TestBorderlessFn>(borderlessExport);
    if (testPresentation == nullptr || testMouseAxis == nullptr ||
        testInterpolationCadence == nullptr ||
        testBorderless == nullptr ||
        g_importedSwapBuffers == nullptr) {
        std::cerr << "The probe export or required SwapBuffers import is missing.\n";
        FreeLibrary(module);
        return 4;
    }
    if (!testMouseAxis()) {
        std::cerr << "The gravity-relative mouse-axis test failed.\n";
        FreeLibrary(module);
        return 5;
    }
    if (!testInterpolationCadence()) {
        std::cerr << "The 60/120 Hz interpolation-cadence test failed.\n";
        FreeLibrary(module);
        return 5;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* windowClass = L"PreyHFRBorderlessProbeWindow";
    WNDCLASSW windowClassDefinition{};
    windowClassDefinition.lpfnWndProc = DefWindowProcW;
    windowClassDefinition.hInstance = instance;
    windowClassDefinition.lpszClassName = windowClass;
    if (!RegisterClassW(&windowClassDefinition) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::cerr << "Could not register the borderless probe window.\n";
        FreeLibrary(module);
        return 6;
    }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY),
                         &monitor)) {
        std::cerr << "Could not query the primary monitor.\n";
        FreeLibrary(module);
        UnregisterClassW(windowClass, instance);
        return 6;
    }
    RECT outer{0, 0, monitor.rcMonitor.right - monitor.rcMonitor.left,
               monitor.rcMonitor.bottom - monitor.rcMonitor.top};
    AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const HWND window = CreateWindowExW(
        0, windowClass, L"PreyHFR borderless probe", WS_OVERLAPPEDWINDOW,
        40, 40, outer.right - outer.left, outer.bottom - outer.top,
        nullptr, nullptr, instance, nullptr);
    const HDC deviceContext = window != nullptr ? GetDC(window) : nullptr;
    RECT originalRect{};
    const LONG_PTR originalStyle =
        window != nullptr ? GetWindowLongPtrW(window, GWL_STYLE) : 0;
    if (window == nullptr || deviceContext == nullptr ||
        !GetWindowRect(window, &originalRect) ||
        !testBorderless(deviceContext)) {
        std::cerr << "The borderless application test failed.\n";
        if (deviceContext != nullptr) ReleaseDC(window, deviceContext);
        if (window != nullptr) DestroyWindow(window);
        FreeLibrary(module);
        UnregisterClassW(windowClass, instance);
        return 6;
    }
    RECT borderlessRect{};
    const LONG_PTR borderlessStyle = GetWindowLongPtrW(window, GWL_STYLE);
    if (!GetWindowRect(window, &borderlessRect) ||
        !RectanglesEqual(borderlessRect, monitor.rcMonitor) ||
        (borderlessStyle & static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) != 0 ||
        IsWindowVisible(window)) {
        std::cerr << "The borderless style, bounds, or hidden state is invalid.\n";
        ReleaseDC(window, deviceContext);
        DestroyWindow(window);
        FreeLibrary(module);
        UnregisterClassW(windowClass, instance);
        return 6;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(seconds);
    std::uint64_t calls = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        testPresentation();
        ++calls;
    }
    if (!FreeLibrary(module)) {
        std::cerr << "FreeLibrary failed with error " << GetLastError() << ".\n";
        ReleaseDC(window, deviceContext);
        DestroyWindow(window);
        UnregisterClassW(windowClass, instance);
        return 6;
    }
    RECT restoredRect{};
    const bool borderlessRestored =
        GetWindowLongPtrW(window, GWL_STYLE) == originalStyle &&
        GetWindowRect(window, &restoredRect) &&
        RectanglesEqual(restoredRect, originalRect) && !IsWindowVisible(window);
    ReleaseDC(window, deviceContext);
    DestroyWindow(window);
    UnregisterClassW(windowClass, instance);
    if (!borderlessRestored) {
        std::cerr << "The borderless restoration test failed.\n";
        return 6;
    }

    std::wcout << L"Gravity-relative mouse-axis, 60/120 Hz interpolation "
                  L"cadence, and borderless apply/restore tests passed; probe completed "
               << calls << L" presentation calls at cap " << cap
               << L"; log: " << Quote(log) << L"\n";
    return 0;
}
