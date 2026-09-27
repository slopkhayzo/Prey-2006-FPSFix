#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <string>
#include <vector>

namespace {

using SwapBuffersFn = BOOL(WINAPI*)(HDC);
using SingleViewFn = void(__thiscall*)(void*, void*, const void*);
using AddEntityDefFn = int(__thiscall*)(void*, const void*);
using UpdateEntityDefFn = void(__thiscall*)(void*, int, const void*);
using FreeEntityDefFn = void(__thiscall*)(void*, int);
using GetRenderEntityFn = const void*(__thiscall*)(void*, int);
using ClearEntityDefDynamicModelFn = void(__cdecl*)(void*);
using MouseMoveFn = void(__thiscall*)(void*);
using DetermineViewAnglesFn = void*(__thiscall*)(void*, void*, const void*, void*);
using UsercmdTicCmdFn = void*(__thiscall*)(void*, void*, int);
using UsercmdInterruptFn = void(__thiscall*)(void*);
using GetDirectUsercmdFn = void*(__thiscall*)(void*, void*);
using DirectInputCreateAFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, void**, void*);
using DirectInputCreateDeviceFn = HRESULT(WINAPI*)(void*, REFGUID, void**, void*);
using DirectInputGetDeviceDataFn = HRESULT(WINAPI*)(void*, DWORD, void*, DWORD*, DWORD);

constexpr std::uintptr_t kSingleViewRva = 0x001a8de0;
constexpr std::uintptr_t kDetermineViewAnglesRva = 0x00195730;
constexpr std::uintptr_t kGameRenderWorldPointerRva = 0x0038ff60;
constexpr std::uintptr_t kMouseMoveRva = 0x00069000;
constexpr std::uintptr_t kUsercmdTicCmdRva = 0x00068ba0;
constexpr std::uintptr_t kUsercmdInterruptRva = 0x00069880;
constexpr std::uintptr_t kGetDirectUsercmdRva = 0x00069970;
constexpr std::uintptr_t kRunGameTicRvaBegin = 0x0005c670;
constexpr std::uintptr_t kRunGameTicRvaEnd = 0x0005cf00;
constexpr std::uintptr_t kSensitivityCvarPointerRva = 0x0044298c;
constexpr std::uintptr_t kPitchCvarPointerRva = 0x004429c0;
constexpr std::uintptr_t kYawCvarPointerRva = 0x004429f4;
constexpr std::uintptr_t kSmoothCvarPointerRva = 0x00442a5c;
constexpr std::uintptr_t kClearEntityDefDynamicModelRva = 0x000df3e0;
constexpr std::size_t kSingleViewStolenBytes = 6;
constexpr std::size_t kDetermineViewAnglesStolenBytes = 6;
constexpr std::size_t kMouseMoveStolenBytes = 9;
constexpr std::size_t kRenderViewSize = 140;
constexpr std::size_t kRenderEntityAllowViewIdOffset = 56;
constexpr std::size_t kRenderEntityOriginOffset = 60;
constexpr std::size_t kRenderEntityAxisOffset = 72;
// Prey increased MAX_ENTITY_SHADER_PARMS from Doom 3's 12 to 13. After the
// three GUI pointers and remoteRenderView, the retail skeletal fields are
// therefore four bytes later than the Doom 3 layout.
constexpr std::size_t kRenderEntityNumJointsOffset = 192;
constexpr std::size_t kRenderEntityJointsOffset = 196;
constexpr std::size_t kRenderEntityPoseSize = 48;
constexpr std::size_t kRenderEntityModelMatrixSize = 16 * sizeof(float);
constexpr std::size_t kRenderEntityModelMatrixSearchBegin = 200;
constexpr std::size_t kRenderEntityModelMatrixSearchEnd = 320;
constexpr std::size_t kMaximumRenderEntityHandles = 8192;
constexpr std::size_t kMaximumAppliedEntityPoses = 2048;
constexpr std::size_t kMaximumEntityJoints = 512;
constexpr std::size_t kMaximumTrackedWorldJointMatrices = 65536;
constexpr std::size_t kRetailRenderEntityModelMatrixOffset = 0xe8;
constexpr std::size_t kRenderEntityLocalPrefixSize = sizeof(void*);
constexpr std::size_t kRenderEntityLocalValidationSize = 0x17c;
constexpr std::int32_t kNativeTicMilliseconds = 16;
constexpr std::int32_t kMaximumInterpolatedCameraIntervalMilliseconds = 32;
constexpr double kNativeTicSeconds = 0.016;
constexpr double kMaximumCameraStep = 32.0;
constexpr double kMaximumCameraAngleDegrees = 45.0;
constexpr double kMaximumViewModelStep = 32.0;
constexpr double kMaximumViewModelAngleDegrees = 45.0;
constexpr double kMaximumViewModelJointStep = 24.0;
constexpr double kMaximumViewModelJointAngleDegrees = 90.0;
constexpr double kDefaultMaximumWorldEntityStep = 128.0;
constexpr double kDefaultMaximumWorldEntityAngleDegrees = 90.0;
constexpr double kMaximumCameraStallSeconds = 0.100;
constexpr double kMaximumMouseOverlayDegrees = 45.0;
constexpr DWORD kDirectInputPeek = 1;
constexpr DWORD kMouseOffsetX = 0;
constexpr DWORD kMouseOffsetY = 4;
constexpr std::size_t kMaximumPeekedMouseEvents = 256;
constexpr std::size_t kMaximumTrackedMouseDeltas = 4096;
constexpr std::size_t kTrackedUsercmdCount = 128;
constexpr std::array<std::uint8_t, kSingleViewStolenBytes> kSingleViewPrologue{
    0x64, 0xa1, 0x00, 0x00, 0x00, 0x00 // mov eax, fs:[0]
};
constexpr std::array<std::uint8_t, kDetermineViewAnglesStolenBytes>
    kDetermineViewAnglesPrologue{
        0x55,                         // push ebp
        0x8b, 0xec,                   // mov ebp, esp
        0x83, 0xe4, 0xc0              // and esp, -40h
    };
constexpr std::array<std::uint8_t, kMouseMoveStolenBytes> kMouseMovePrologue{
    0x83, 0xec, 0x1c,                   // sub esp, 1ch
    0x8b, 0x15, 0xf0, 0xa0, 0x19, 0x01 // mov edx, [0119a0f0h]
};
constexpr std::array<std::uint8_t, 16> kClearEntityDefDynamicModelPrologue{
    0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x8b, 0xb7,
    0x78, 0x01, 0x00, 0x00, 0x85, 0xf6, 0x74, 0x13
};
constexpr GUID kSystemMouseGuid{
    0x6f1d2b60, 0xd5a0, 0x11cf,
    {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

SwapBuffersFn g_originalSwapBuffers = nullptr;
IMAGE_THUNK_DATA32* g_swapBuffersThunk = nullptr;
LARGE_INTEGER g_frequency{};
std::int64_t g_periodCounts = 0;
std::int64_t g_nextDeadline = 0;
std::int64_t g_reportStart = 0;
std::int64_t g_previousSwap = 0;
std::uint64_t g_reportFrames = 0;
constexpr std::size_t kMaximumIntervalSamples = 4096;
std::array<double, kMaximumIntervalSamples> g_intervalMilliseconds{};
std::size_t g_intervalSampleCount = 0;
double g_worstIntervalMilliseconds = 0.0;
HANDLE g_log = INVALID_HANDLE_VALUE;
bool g_timerResolutionRaised = false;
bool g_viewLoggingRequested = false;
bool g_cameraInterpolationRequested = false;
bool g_viewModelInterpolationRequested = false;
bool g_viewModelAnimationInterpolationRequested = false;
bool g_worldInterpolationRequested = false;
bool g_worldAnimationInterpolationRequested = false;
bool g_mouseInterpolationRequested = false;
bool g_continuousSnapshotTimingRequested = false;
bool g_multiTicEntityAlignmentRequested = false;
bool g_overdueSnapshotFallbackRequested = false;
bool g_borderlessRequested = false;
unsigned int g_requestedRenderWidth = 0;
unsigned int g_requestedRenderHeight = 0;
double g_maximumWorldEntityStep = kDefaultMaximumWorldEntityStep;
double g_maximumWorldEntityAngleDegrees =
    kDefaultMaximumWorldEntityAngleDegrees;
int g_viewHookState = 0; // 0 = waiting for game DLL, 1 = installed, 2 = failed
std::uint8_t* g_singleViewTarget = nullptr;
void* g_singleViewTrampoline = nullptr;
SingleViewFn g_originalSingleView = nullptr;
std::array<std::uint8_t, kSingleViewStolenBytes> g_singleViewOriginal{};
std::uint8_t* g_mouseMoveTarget = nullptr;
void* g_mouseMoveTrampoline = nullptr;
MouseMoveFn g_originalMouseMove = nullptr;
std::array<std::uint8_t, kMouseMoveStolenBytes> g_mouseMoveOriginal{};
int g_mouseMoveHookState = 0;
std::uint8_t* g_determineViewAnglesTarget = nullptr;
void* g_determineViewAnglesTrampoline = nullptr;
DetermineViewAnglesFn g_originalDetermineViewAngles = nullptr;
std::array<std::uint8_t, kDetermineViewAnglesStolenBytes>
    g_determineViewAnglesOriginal{};
int g_determineViewAnglesHookState = 0;
std::atomic<std::uint32_t> g_effectivePitchBits{0};
std::atomic<bool> g_haveEffectivePitch{false};
std::atomic<void*> g_usercmdGenerator{nullptr};
std::atomic<DWORD> g_presentationThreadId{0};
void** g_usercmdTicCmdSlot = nullptr;
void** g_usercmdInterruptSlot = nullptr;
void** g_getDirectUsercmdSlot = nullptr;
UsercmdTicCmdFn g_originalUsercmdTicCmd = nullptr;
UsercmdInterruptFn g_originalUsercmdInterrupt = nullptr;
GetDirectUsercmdFn g_originalGetDirectUsercmd = nullptr;
int g_usercmdHookState = 0;

struct BorderlessWindowState {
    HWND window = nullptr;
    LONG_PTR originalStyle = 0;
    LONG_PTR originalExtendedStyle = 0;
    LONG_PTR appliedStyle = 0;
    LONG_PTR appliedExtendedStyle = 0;
    RECT originalRect{};
    RECT monitorRect{};
    bool originalVisible = false;
    bool captured = false;
    bool applied = false;
    bool failureLogged = false;
};

BorderlessWindowState g_borderlessWindow{};

DirectInputCreateAFn g_originalDirectInputCreate = nullptr;
IMAGE_THUNK_DATA32* g_directInputCreateThunk = nullptr;
void** g_directInputCreateDeviceSlot = nullptr;
DirectInputCreateDeviceFn g_originalDirectInputCreateDevice = nullptr;
void** g_directInputGetDeviceDataSlot = nullptr;
DirectInputGetDeviceDataFn g_originalDirectInputGetDeviceData = nullptr;
void* g_mouseDevice = nullptr;
DWORD g_mouseObjectDataSize = 0;
SRWLOCK g_directInputLock = SRWLOCK_INIT;
int g_directInputHookState = 0;

struct MouseObjectData {
    DWORD offset = 0;
    DWORD data = 0;
    DWORD timestamp = 0;
    DWORD sequence = 0;
};

static_assert(sizeof(void*) == 4, "PreyHFRHook must be built for x86");
static_assert(sizeof(MouseObjectData) == 16,
              "Retail DirectInput object data is expected to be 16 bytes");

struct MouseCounters {
    std::atomic<std::uint64_t> authoritativeCalls{0};
    std::atomic<std::uint64_t> authoritativeEvents{0};
    std::atomic<std::uint64_t> peekCalls{0};
    std::atomic<std::uint64_t> peekEvents{0};
    std::atomic<std::uint64_t> overlayViews{0};
    std::atomic<std::uint64_t> skippedState{0};
    std::atomic<std::uint64_t> skippedSmoothing{0};
    std::atomic<std::uint64_t> peekFailures{0};
    std::atomic<std::uint64_t> moveCalls{0};
    std::atomic<std::uint64_t> moveChanges{0};
    std::atomic<std::uint64_t> trackedOverlayViews{0};
    std::atomic<std::uint64_t> retiredChanges{0};
    std::atomic<std::uint64_t> asyncCommands{0};
    std::atomic<std::uint64_t> ticSelections{0};
    std::atomic<std::uint64_t> directSelections{0};
    std::atomic<std::uint64_t> commandMisses{0};
    std::atomic<std::uint64_t> ledgerOverflows{0};
};

MouseCounters g_mouseCounters{};

struct MouseDeltaRecord {
    std::uint64_t serial = 0;
    double yaw = 0.0;
    double pitch = 0.0;
};

struct UsercmdMouseRecord {
    std::int32_t sequence = 0;
    std::uint64_t serial = 0;
    bool valid = false;
};

SRWLOCK g_mouseLedgerLock = SRWLOCK_INIT;
std::array<MouseDeltaRecord, kMaximumTrackedMouseDeltas> g_mouseDeltaLedger{};
std::array<UsercmdMouseRecord, kTrackedUsercmdCount> g_usercmdMouseLedger{};
std::uint64_t g_latestMouseSerial = 0;
std::uint64_t g_includedMouseSerial = 0;
bool g_haveTaggedAsyncCommand = false;
std::int32_t g_firstTaggedAsyncSequence = 0;
std::atomic<std::uint64_t> g_selectedMouseSerial{0};
bool g_haveMouseViewTime = false;
std::int32_t g_mouseViewTime = 0;
double g_mouseTransitionYaw = 0.0;
double g_mouseTransitionPitch = 0.0;
thread_local bool g_capturingUsercmdMouseSerial = false;
thread_local std::uint64_t g_capturedUsercmdMouseSerial = 0;

struct ViewCounters {
    std::uint64_t calls = 0;
    std::uint64_t nullViews = 0;
    std::uint64_t timeChanges = 0;
    std::uint64_t sameTime = 0;
    std::uint64_t discontinuities = 0;
    std::uint64_t viewIdChanges = 0;
    std::uint64_t interpolatedViews = 0;
    std::uint64_t extrapolatedViews = 0;
    std::uint64_t snappedViews = 0;
    std::uint64_t interpolationResets = 0;
    std::uint64_t resetTimeDelta = 0;
    std::uint64_t resetViewId = 0;
    std::uint64_t resetFov = 0;
    std::uint64_t resetStall = 0;
    std::uint64_t resetOrigin = 0;
    std::uint64_t resetAxis = 0;
    std::uint64_t viewDeltaNonpositive = 0;
    std::uint64_t viewDelta32 = 0;
    std::uint64_t viewDelta48 = 0;
    std::uint64_t viewDelta64 = 0;
    std::uint64_t viewDeltaOther = 0;
    std::int64_t minimumViewDelta = 0;
    std::int64_t maximumViewDelta = 0;
    std::uint64_t adjustedViewModels = 0;
    std::uint64_t adjustedViewModelAnimations = 0;
    std::uint64_t interpolatedViewModelJoints = 0;
    std::uint64_t viewModelAnimationSkips = 0;
    std::uint64_t adjustedWorldEntities = 0;
    std::uint64_t adjustedWorldAnimations = 0;
    std::uint64_t interpolatedWorldJoints = 0;
    std::uint64_t worldAnimationSkips = 0;
    std::uint64_t worldEntityResets = 0;
    std::uint64_t worldEntityCapacitySkips = 0;
    std::uint64_t viewModelLatestTicRoots = 0;
    std::uint64_t viewModelLatestTicAnimations = 0;
    std::uint64_t worldLatestTicRoots = 0;
    std::uint64_t worldLatestTicAnimations = 0;
    std::uint64_t viewModelPendingRoots = 0;
    std::uint64_t viewModelPendingAnimations = 0;
    std::uint64_t worldPendingRoots = 0;
    std::uint64_t worldPendingAnimations = 0;
    double maximumOriginStep = 0.0;
};

ViewCounters g_viewCounters{};
bool g_havePreviousView = false;
std::int32_t g_previousViewTime = 0;
std::int32_t g_previousViewId = 0;
std::array<float, 3> g_previousViewOrigin{};
alignas(16) std::array<std::uint8_t, kRenderViewSize> g_cameraPrevious{};
alignas(16) std::array<std::uint8_t, kRenderViewSize> g_cameraCurrent{};
bool g_haveCameraCurrent = false;
bool g_canInterpolateCamera = false;
double g_cameraPreviousPitch = 0.0;
double g_cameraCurrentPitch = 0.0;
bool g_cameraPreviousPitchValid = false;
bool g_cameraCurrentPitchValid = false;
std::int64_t g_cameraCurrentQpc = 0;
std::int64_t g_lastCameraCallQpc = 0;
std::int32_t g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
bool g_cameraTimelineSynchronized = false;
std::uint64_t g_cameraSnapshotGeneration = 0;

struct ViewEntityPose {
    std::array<float, 3> origin{};
    std::array<float, 9> axis{};
};

struct alignas(16) JointMatrix {
    std::array<float, 12> values{};
};

static_assert(sizeof(JointMatrix) == 48,
              "Retail idJointMat is expected to contain twelve floats");

struct TrackedJointAnimation {
    bool hasCurrent = false;
    bool canInterpolate = false;
    bool cacheWasInterpolated = false;
    std::uint64_t transitionGeneration = 0;
    std::uint32_t transitionSamples = 0;
    const JointMatrix* sourceJoints = nullptr;
    std::vector<JointMatrix> previous;
    std::vector<JointMatrix> current;
    std::vector<JointMatrix> interpolated;
};

struct RenderEntityIdentity {
    std::uintptr_t model = 0;
    int entityNumber = 0;
    int bodyId = 0;
    std::uintptr_t callback = 0;
    std::uintptr_t callbackData = 0;
};

bool operator==(const RenderEntityIdentity& left,
                const RenderEntityIdentity& right) {
    return left.model == right.model &&
           left.entityNumber == right.entityNumber &&
           left.bodyId == right.bodyId &&
           left.callback == right.callback &&
           left.callbackData == right.callbackData;
}

struct TrackedRenderEntity {
    int handle = -1;
    int allowViewId = 0;
    std::uint32_t generation = 0;
    std::uint64_t revision = 0;
    std::size_t activeIndex = 0;
    RenderEntityIdentity identity{};
    bool hasCurrent = false;
    bool canInterpolate = false;
    std::uint64_t transitionGeneration = 0;
    std::uint32_t transitionSamples = 0;
    std::uint64_t firstInterpolatableGeneration = 0;
    ViewEntityPose previous{};
    ViewEntityPose current{};
    TrackedJointAnimation animation{};
};

std::array<TrackedRenderEntity, kMaximumRenderEntityHandles> g_renderEntities{};
std::array<int, kMaximumRenderEntityHandles> g_activeRenderEntityHandles{};
std::size_t g_activeRenderEntityCount = 0;
std::uint32_t g_nextRenderEntityGeneration = 1;
void* g_renderWorld = nullptr;
void** g_renderWorldVtable = nullptr;
void** g_addEntityDefSlot = nullptr;
void** g_updateEntityDefSlot = nullptr;
void** g_freeEntityDefSlot = nullptr;
AddEntityDefFn g_originalAddEntityDef = nullptr;
UpdateEntityDefFn g_originalUpdateEntityDef = nullptr;
FreeEntityDefFn g_originalFreeEntityDef = nullptr;
GetRenderEntityFn g_getRenderEntity = nullptr;
int g_renderEntityHookState = 0;
std::size_t g_renderEntityModelMatrixOffset = 0;
ClearEntityDefDynamicModelFn g_clearEntityDefDynamicModel = nullptr;
int g_animationRendererState = 0;
std::size_t g_trackedWorldJointMatrices = 0;

void Log(const std::string& message) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(g_log, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
}

std::wstring ReadEnvironment(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return {};
    std::wstring value(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0 || copied >= required) return {};
    value.resize(copied);
    return value;
}

unsigned int ReadFrameCap() {
    const std::wstring value = ReadEnvironment(L"PREYHFR_CAP");
    if (value.empty()) return 240;
    unsigned int result = 0;
    for (const wchar_t character : value) {
        if (character < L'0' || character > L'9') return 240;
        result = std::min(1000u, result * 10u + static_cast<unsigned int>(character - L'0'));
    }
    return result;
}

bool ReadEnvironmentFlag(const wchar_t* name) {
    const std::wstring value = ReadEnvironment(name);
    return value == L"1" || value == L"true" || value == L"TRUE" ||
           value == L"yes" || value == L"YES";
}

unsigned int ReadEnvironmentUnsigned(const wchar_t* name,
                                     unsigned int maximum) {
    const std::wstring value = ReadEnvironment(name);
    if (value.empty()) return 0;
    unsigned int result = 0;
    for (const wchar_t character : value) {
        if (character < L'0' || character > L'9') return 0;
        const unsigned int digit =
            static_cast<unsigned int>(character - L'0');
        if (result > (maximum - digit) / 10) return 0;
        result = result * 10 + digit;
    }
    return result;
}

bool SetWindowAttribute(HWND window, int index, LONG_PTR value) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, index, value);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

bool RectanglesEqual(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

bool RestoreBorderlessWindow() {
    if (!g_borderlessWindow.captured || !g_borderlessWindow.applied ||
        !IsWindow(g_borderlessWindow.window)) {
        g_borderlessWindow = {};
        return true;
    }

    const LONG_PTR style = GetWindowLongPtrW(g_borderlessWindow.window, GWL_STYLE);
    const LONG_PTR extendedStyle =
        GetWindowLongPtrW(g_borderlessWindow.window, GWL_EXSTYLE);
    if (style != g_borderlessWindow.appliedStyle ||
        extendedStyle != g_borderlessWindow.appliedExtendedStyle) {
        g_borderlessWindow = {};
        return false;
    }

    const bool styleRestored = SetWindowAttribute(
        g_borderlessWindow.window, GWL_STYLE,
        g_borderlessWindow.originalStyle);
    const bool extendedStyleRestored = SetWindowAttribute(
        g_borderlessWindow.window, GWL_EXSTYLE,
        g_borderlessWindow.originalExtendedStyle);
    const RECT rect = g_borderlessWindow.originalRect;
    const UINT positionFlags = SWP_FRAMECHANGED | SWP_NOACTIVATE |
        (g_borderlessWindow.originalVisible ? SWP_SHOWWINDOW : 0);
    const bool positionRestored = SetWindowPos(
        g_borderlessWindow.window, HWND_NOTOPMOST, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top,
        positionFlags) != FALSE;
    g_borderlessWindow = {};
    return styleRestored && extendedStyleRestored && positionRestored;
}

void TryApplyBorderlessWindow(HDC deviceContext, bool requireVisible = true,
                              bool showWindow = true) {
    if (!g_borderlessRequested || deviceContext == nullptr) return;
    const HWND window = WindowFromDC(deviceContext);
    if (window == nullptr || (requireVisible && !IsWindowVisible(window))) return;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId()) return;

    if (g_borderlessWindow.window != window) {
        if (g_borderlessWindow.window != nullptr) RestoreBorderlessWindow();
        g_borderlessWindow.window = window;
        g_borderlessWindow.originalStyle = GetWindowLongPtrW(window, GWL_STYLE);
        g_borderlessWindow.originalExtendedStyle =
            GetWindowLongPtrW(window, GWL_EXSTYLE);
        g_borderlessWindow.originalVisible = IsWindowVisible(window) != FALSE;
        g_borderlessWindow.captured =
            GetWindowRect(window, &g_borderlessWindow.originalRect) != FALSE;
        if (!g_borderlessWindow.captured) {
            g_borderlessWindow.failureLogged = true;
            Log("error: borderless window bounds could not be captured\r\n");
            return;
        }
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor =
        MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    RECT client{};
    if (monitor == nullptr ||
        !GetMonitorInfoW(monitor, &monitorInfo) ||
        !GetClientRect(window, &client)) {
        if (!g_borderlessWindow.failureLogged) {
            Log("error: borderless monitor or client bounds are unavailable\r\n");
            g_borderlessWindow.failureLogged = true;
        }
        return;
    }

    const int monitorWidth =
        monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    const int monitorHeight =
        monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    const bool requestedDesktopSize =
        g_requestedRenderWidth == static_cast<unsigned int>(monitorWidth) &&
        g_requestedRenderHeight == static_cast<unsigned int>(monitorHeight);
    constexpr int kMaximumDecoratedClamp = 256;
    const bool plausibleDecoratedClient =
        clientWidth <= monitorWidth && clientHeight <= monitorHeight &&
        monitorWidth - clientWidth <= kMaximumDecoratedClamp &&
        monitorHeight - clientHeight <= kMaximumDecoratedClamp;
    if (!requestedDesktopSize || !plausibleDecoratedClient) {
        if (!g_borderlessWindow.failureLogged) {
            char buffer[320]{};
            const int length = _snprintf_s(
                buffer, sizeof(buffer), _TRUNCATE,
                "error: borderless requires a desktop-sized renderer; requested=%ux%u client=%dx%d monitor=%dx%d\r\n",
                g_requestedRenderWidth, g_requestedRenderHeight,
                clientWidth, clientHeight, monitorWidth, monitorHeight);
            if (length > 0) {
                Log(std::string(buffer, static_cast<std::size_t>(length)));
            }
            g_borderlessWindow.failureLogged = true;
        }
        return;
    }

    const LONG_PTR currentStyle = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR currentExtendedStyle =
        GetWindowLongPtrW(window, GWL_EXSTYLE);
    const LONG_PTR desiredStyle =
        (currentStyle & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) |
        static_cast<LONG_PTR>(WS_POPUP);
    const LONG_PTR undesiredExtendedStyle =
        static_cast<LONG_PTR>(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                              WS_EX_CLIENTEDGE | WS_EX_STATICEDGE |
                              WS_EX_TOPMOST);
    const LONG_PTR desiredExtendedStyle =
        currentExtendedStyle & ~undesiredExtendedStyle;

    RECT currentRect{};
    if (g_borderlessWindow.applied &&
        currentStyle == g_borderlessWindow.appliedStyle &&
        currentExtendedStyle == g_borderlessWindow.appliedExtendedStyle &&
        GetWindowRect(window, &currentRect) &&
        RectanglesEqual(currentRect, monitorInfo.rcMonitor)) {
        return;
    }

    if ((currentExtendedStyle & static_cast<LONG_PTR>(WS_EX_TOPMOST)) != 0) {
        // The retail exclusive path marks its window topmost after changing the
        // primary display mode. Return to the desktop mode before restyling.
        ChangeDisplaySettingsW(nullptr, 0);
    }
    const UINT positionFlags = SWP_FRAMECHANGED | SWP_NOACTIVATE |
        (showWindow ? SWP_SHOWWINDOW : 0);
    const bool styleApplied =
        SetWindowAttribute(window, GWL_STYLE, desiredStyle);
    const bool extendedStyleApplied = styleApplied &&
        SetWindowAttribute(window, GWL_EXSTYLE, desiredExtendedStyle);
    const bool positionApplied = extendedStyleApplied &&
        SetWindowPos(window, HWND_NOTOPMOST, monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.top, monitorWidth, monitorHeight,
                     positionFlags) != FALSE;
    if (!styleApplied || !extendedStyleApplied || !positionApplied) {
        SetWindowAttribute(window, GWL_STYLE,
                           g_borderlessWindow.originalStyle);
        SetWindowAttribute(window, GWL_EXSTYLE,
                           g_borderlessWindow.originalExtendedStyle);
        const RECT original = g_borderlessWindow.originalRect;
        SetWindowPos(window, HWND_NOTOPMOST, original.left, original.top,
                     original.right - original.left,
                     original.bottom - original.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE |
                         (g_borderlessWindow.originalVisible
                              ? SWP_SHOWWINDOW
                              : 0));
        g_borderlessWindow.applied = false;
        if (!g_borderlessWindow.failureLogged) {
            Log("error: borderless window style or placement failed\r\n");
            g_borderlessWindow.failureLogged = true;
        }
        return;
    }

    g_borderlessWindow.appliedStyle = desiredStyle;
    g_borderlessWindow.appliedExtendedStyle = desiredExtendedStyle;
    g_borderlessWindow.monitorRect = monitorInfo.rcMonitor;
    const bool firstApplication = !g_borderlessWindow.applied;
    g_borderlessWindow.applied = true;
    g_borderlessWindow.failureLogged = false;
    if (firstApplication) {
        char buffer[256]{};
        const int length = _snprintf_s(
            buffer, sizeof(buffer), _TRUNCATE,
            "window: borderless applied at %ld,%ld with %dx%d client\r\n",
            monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
            monitorWidth, monitorHeight);
        if (length > 0) {
            Log(std::string(buffer, static_cast<std::size_t>(length)));
        }
    }
}

double ReadEnvironmentDouble(const wchar_t* name, double fallback,
                             double minimum, double maximum) {
    const std::wstring value = ReadEnvironment(name);
    if (value.empty()) return fallback;
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(value.c_str(), &end);
    if (end == value.c_str() || *end != L'\0' || !std::isfinite(parsed) ||
        parsed < minimum || parsed > maximum) {
        return fallback;
    }
    return parsed;
}

template <typename T>
T ReadUnaligned(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset,
                sizeof(value));
    return value;
}

template <typename T>
void WriteUnaligned(void* base, std::size_t offset, const T& value) {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(value));
}

bool IsReadableRange(const void* address, std::size_t size) {
    if (address == nullptr || size == 0) return false;
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(address, &information, sizeof(information)) !=
        sizeof(information)) return false;
    if (information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto regionStart = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    return start >= regionStart && start + size >= start &&
           start + size <= regionStart + information.RegionSize;
}

bool ReadEngineCvarFloat(std::uintptr_t pointerRva, float& value) {
    const auto* module = reinterpret_cast<const std::uint8_t*>(
        GetModuleHandleW(nullptr));
    if (module == nullptr) return false;
    void* object = ReadUnaligned<void*>(module, pointerRva);
    if (!IsReadableRange(object, 0x2c)) return false;
    value = ReadUnaligned<float>(object, 0x28);
    return std::isfinite(value);
}

bool ReadEngineCvarInteger(std::uintptr_t pointerRva, int& value) {
    const auto* module = reinterpret_cast<const std::uint8_t*>(
        GetModuleHandleW(nullptr));
    if (module == nullptr) return false;
    void* object = ReadUnaligned<void*>(module, pointerRva);
    if (!IsReadableRange(object, 0x28)) return false;
    value = ReadUnaligned<int>(object, 0x24);
    return true;
}

bool PeekPendingMouse(int& deltaX, int& deltaY) {
    deltaX = 0;
    deltaY = 0;
    if (!g_mouseInterpolationRequested) return false;

    std::array<MouseObjectData, kMaximumPeekedMouseEvents> events{};
    DWORD count = static_cast<DWORD>(events.size());
    HRESULT result = E_FAIL;
    AcquireSRWLockExclusive(&g_directInputLock);
    if (g_mouseDevice != nullptr && g_originalDirectInputGetDeviceData != nullptr &&
        g_mouseObjectDataSize == sizeof(MouseObjectData)) {
        result = g_originalDirectInputGetDeviceData(
            g_mouseDevice, sizeof(MouseObjectData), events.data(), &count,
            kDirectInputPeek);
    }
    ReleaseSRWLockExclusive(&g_directInputLock);

    if (FAILED(result) || count >= events.size()) {
        ++g_mouseCounters.peekFailures;
        return false;
    }
    ++g_mouseCounters.peekCalls;
    g_mouseCounters.peekEvents += count;
    for (DWORD index = 0; index < count; ++index) {
        const int value = static_cast<int>(events[index].data);
        if (events[index].offset == kMouseOffsetX) {
            deltaX += value;
        } else if (events[index].offset == kMouseOffsetY) {
            deltaY += value;
        }
    }
    return deltaX != 0 || deltaY != 0;
}

struct Quaternion {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Quaternion Normalize(Quaternion value) {
    const double length = std::sqrt(value.w * value.w + value.x * value.x +
                                    value.y * value.y + value.z * value.z);
    if (!(length > 0.000001) || !std::isfinite(length)) return {};
    value.w /= length;
    value.x /= length;
    value.y /= length;
    value.z /= length;
    return value;
}

double QuaternionDot(const Quaternion& left, const Quaternion& right) {
    return left.w * right.w + left.x * right.x + left.y * right.y +
           left.z * right.z;
}

Quaternion MatrixToQuaternion(const std::array<float, 9>& matrix) {
    Quaternion result;
    const double trace = static_cast<double>(matrix[0]) + matrix[4] + matrix[8];
    if (trace > 0.0) {
        const double scale = std::sqrt(trace + 1.0) * 2.0;
        result.w = 0.25 * scale;
        result.x = (matrix[7] - matrix[5]) / scale;
        result.y = (matrix[2] - matrix[6]) / scale;
        result.z = (matrix[3] - matrix[1]) / scale;
    } else if (matrix[0] > matrix[4] && matrix[0] > matrix[8]) {
        const double scale = std::sqrt(1.0 + matrix[0] - matrix[4] - matrix[8]) * 2.0;
        result.w = (matrix[7] - matrix[5]) / scale;
        result.x = 0.25 * scale;
        result.y = (matrix[1] + matrix[3]) / scale;
        result.z = (matrix[2] + matrix[6]) / scale;
    } else if (matrix[4] > matrix[8]) {
        const double scale = std::sqrt(1.0 + matrix[4] - matrix[0] - matrix[8]) * 2.0;
        result.w = (matrix[2] - matrix[6]) / scale;
        result.x = (matrix[1] + matrix[3]) / scale;
        result.y = 0.25 * scale;
        result.z = (matrix[5] + matrix[7]) / scale;
    } else {
        const double scale = std::sqrt(1.0 + matrix[8] - matrix[0] - matrix[4]) * 2.0;
        result.w = (matrix[3] - matrix[1]) / scale;
        result.x = (matrix[2] + matrix[6]) / scale;
        result.y = (matrix[5] + matrix[7]) / scale;
        result.z = 0.25 * scale;
    }
    return Normalize(result);
}

Quaternion Slerp(Quaternion from, Quaternion to, double alpha) {
    double dot = QuaternionDot(from, to);
    if (dot < 0.0) {
        to.w = -to.w;
        to.x = -to.x;
        to.y = -to.y;
        to.z = -to.z;
        dot = -dot;
    }
    dot = std::clamp(dot, 0.0, 1.0);
    if (dot > 0.9995) {
        return Normalize({from.w + (to.w - from.w) * alpha,
                          from.x + (to.x - from.x) * alpha,
                          from.y + (to.y - from.y) * alpha,
                          from.z + (to.z - from.z) * alpha});
    }
    const double theta = std::acos(dot);
    const double denominator = std::sin(theta);
    const double fromScale = std::sin((1.0 - alpha) * theta) / denominator;
    const double toScale = std::sin(alpha * theta) / denominator;
    return Normalize({from.w * fromScale + to.w * toScale,
                      from.x * fromScale + to.x * toScale,
                      from.y * fromScale + to.y * toScale,
                      from.z * fromScale + to.z * toScale});
}

std::array<float, 9> QuaternionToMatrix(const Quaternion& value) {
    const double xx = value.x * value.x;
    const double yy = value.y * value.y;
    const double zz = value.z * value.z;
    const double xy = value.x * value.y;
    const double xz = value.x * value.z;
    const double yz = value.y * value.z;
    const double wx = value.w * value.x;
    const double wy = value.w * value.y;
    const double wz = value.w * value.z;
    return {
        static_cast<float>(1.0 - 2.0 * (yy + zz)),
        static_cast<float>(2.0 * (xy - wz)),
        static_cast<float>(2.0 * (xz + wy)),
        static_cast<float>(2.0 * (xy + wz)),
        static_cast<float>(1.0 - 2.0 * (xx + zz)),
        static_cast<float>(2.0 * (yz - wx)),
        static_cast<float>(2.0 * (xz - wy)),
        static_cast<float>(2.0 * (yz + wx)),
        static_cast<float>(1.0 - 2.0 * (xx + yy)),
    };
}

std::array<float, 3> ReadOrigin(const void* view) {
    return {ReadUnaligned<float>(view, 28), ReadUnaligned<float>(view, 32),
            ReadUnaligned<float>(view, 36)};
}

std::array<float, 9> ReadAxis(const void* view) {
    std::array<float, 9> result{};
    std::memcpy(result.data(), static_cast<const std::uint8_t*>(view) + 40,
                sizeof(result));
    return result;
}

struct MouseOverlay {
    bool applied = false;
    std::array<float, 3> cameraOrigin{};
    std::array<float, 9> baseAxis{};
    std::array<float, 9> overlaidAxis{};
};

bool SumMouseDeltasLocked(std::uint64_t firstSerial,
                          std::uint64_t lastSerial,
                          double& yaw, double& pitch) {
    if (lastSerial < firstSerial) return true;
    if (lastSerial - firstSerial + 1 > kMaximumTrackedMouseDeltas) return false;
    for (std::uint64_t serial = firstSerial; serial <= lastSerial; ++serial) {
        const auto& record = g_mouseDeltaLedger[
            static_cast<std::size_t>(serial % kMaximumTrackedMouseDeltas)];
        if (record.serial != serial) return false;
        yaw += record.yaw;
        pitch += record.pitch;
    }
    return true;
}

bool GetTrackedMouseOverlay(const void* view, double interpolationAlpha,
                            bool cameraInterpolated, double& yaw,
                            double& pitch) {
    yaw = 0.0;
    pitch = 0.0;
    if (view == nullptr || g_usercmdHookState != 1) return false;

    const auto viewTime = ReadUnaligned<std::int32_t>(view, 80);
    AcquireSRWLockExclusive(&g_mouseLedgerLock);
    const std::uint64_t latest = g_latestMouseSerial;
    bool valid = true;
    if (!g_haveMouseViewTime || viewTime != g_mouseViewTime) {
        std::uint64_t selected =
            g_selectedMouseSerial.load(std::memory_order_acquire);
        selected = std::min(selected, latest);
        selected = std::max(selected, g_includedMouseSerial);
        double incorporatedYaw = 0.0;
        double incorporatedPitch = 0.0;
        if (selected > g_includedMouseSerial) {
            valid = SumMouseDeltasLocked(g_includedMouseSerial + 1, selected,
                                         incorporatedYaw, incorporatedPitch);
            if (valid) {
                g_mouseCounters.retiredChanges +=
                    selected - g_includedMouseSerial;
                g_includedMouseSerial = selected;
            }
        }
        if (!g_haveMouseViewTime || !cameraInterpolated || !valid) {
            g_mouseTransitionYaw = 0.0;
            g_mouseTransitionPitch = 0.0;
        } else {
            g_mouseTransitionYaw = incorporatedYaw;
            g_mouseTransitionPitch = incorporatedPitch;
        }
        g_mouseViewTime = viewTime;
        g_haveMouseViewTime = true;
    } else if (!cameraInterpolated) {
        g_mouseTransitionYaw = 0.0;
        g_mouseTransitionPitch = 0.0;
    }

    if (valid && latest > g_includedMouseSerial) {
        valid = SumMouseDeltasLocked(g_includedMouseSerial + 1, latest,
                                     yaw, pitch);
    }
    if (valid && cameraInterpolated) {
        const double remaining =
            std::clamp(1.0 - interpolationAlpha, 0.0, 1.0);
        yaw += g_mouseTransitionYaw * remaining;
        pitch += g_mouseTransitionPitch * remaining;
    }
    if (!valid) {
        g_includedMouseSerial = latest;
        g_mouseTransitionYaw = 0.0;
        g_mouseTransitionPitch = 0.0;
        yaw = 0.0;
        pitch = 0.0;
        ++g_mouseCounters.ledgerOverflows;
    }
    ReleaseSRWLockExclusive(&g_mouseLedgerLock);
    return valid && (yaw != 0.0 || pitch != 0.0);
}

void PublishEffectivePitch(float pitch) {
    if (!std::isfinite(pitch) || std::abs(static_cast<double>(pitch)) > 180.0) {
        return;
    }
    std::uint32_t bits = 0;
    std::memcpy(&bits, &pitch, sizeof(bits));
    g_effectivePitchBits.store(bits, std::memory_order_relaxed);
    g_haveEffectivePitch.store(true, std::memory_order_release);
}

bool ReadEffectivePitch(double& pitch) {
    if (!g_haveEffectivePitch.load(std::memory_order_acquire)) return false;
    const std::uint32_t bits =
        g_effectivePitchBits.load(std::memory_order_relaxed);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value) || std::abs(static_cast<double>(value)) > 180.0) {
        return false;
    }
    pitch = value;
    return true;
}

bool NormalizeVector(std::array<double, 3>& vector) {
    const double lengthSquared = vector[0] * vector[0] +
                                 vector[1] * vector[1] +
                                 vector[2] * vector[2];
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12) return false;
    const double inverseLength = 1.0 / std::sqrt(lengthSquared);
    for (double& component : vector) component *= inverseLength;
    return true;
}

std::array<double, 3> RotateAroundAxis(const std::array<double, 3>& vector,
                                       const std::array<double, 3>& axis,
                                       double radians) {
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double oneMinusCosine = 1.0 - cosine;
    const double dot = axis[0] * vector[0] + axis[1] * vector[1] +
                       axis[2] * vector[2];
    const std::array<double, 3> cross{
        axis[1] * vector[2] - axis[2] * vector[1],
        axis[2] * vector[0] - axis[0] * vector[2],
        axis[0] * vector[1] - axis[1] * vector[0]};
    std::array<double, 3> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = cosine * vector[index] + sine * cross[index] +
                        oneMinusCosine * dot * axis[index];
    }
    return result;
}

bool ApplyGravityRelativeViewAngles(const std::array<float, 9>& axis,
                                    double basePitchDegrees,
                                    double yawDegrees,
                                    double pitchDegrees,
                                    std::array<float, 9>& result) {
    const double basePitch = basePitchDegrees * 0.017453292519943295;
    const double yaw = yawDegrees * 0.017453292519943295;
    const double pitch = pitchDegrees * 0.017453292519943295;
    if (!std::isfinite(basePitch) || !std::isfinite(yaw) ||
        !std::isfinite(pitch)) {
        return false;
    }

    std::array<double, 3> forward{
        axis[0], axis[1], axis[2]};
    std::array<double, 3> left{
        axis[3], axis[4], axis[5]};
    std::array<double, 3> up{
        axis[6], axis[7], axis[8]};

    // Prey composes viewAngles.ToMat3() with the player's gravity axis.  Undo
    // the displayed base pitch to recover gravity-up, then yaw around that
    // vector.  Camera-local up only coincides with gravity-up at zero pitch.
    const double basePitchCosine = std::cos(basePitch);
    const double basePitchSine = std::sin(basePitch);
    std::array<double, 3> gravityUp{};
    for (std::size_t index = 0; index < 3; ++index) {
        gravityUp[index] = -basePitchSine * forward[index] +
                           basePitchCosine * up[index];
    }
    if (!NormalizeVector(gravityUp)) return false;

    forward = RotateAroundAxis(forward, gravityUp, yaw);
    left = RotateAroundAxis(left, gravityUp, yaw);
    up = RotateAroundAxis(up, gravityUp, yaw);
    if (!NormalizeVector(left)) return false;

    forward = RotateAroundAxis(forward, left, pitch);
    up = RotateAroundAxis(up, left, pitch);

    for (std::size_t index = 0; index < 3; ++index) {
        result[index] = static_cast<float>(forward[index]);
        result[3 + index] = static_cast<float>(left[index]);
        result[6 + index] = static_cast<float>(up[index]);
        if (!std::isfinite(result[index]) ||
            !std::isfinite(result[3 + index]) ||
            !std::isfinite(result[6 + index])) {
            return false;
        }
    }
    return true;
}

std::array<float, 9> MakeGravityAlignedTestAxis(double pitchDegrees,
                                                double yawDegrees) {
    const double pitch = pitchDegrees * 0.017453292519943295;
    const double yaw = yawDegrees * 0.017453292519943295;
    const double pitchCosine = std::cos(pitch);
    const double pitchSine = std::sin(pitch);
    const double yawCosine = std::cos(yaw);
    const double yawSine = std::sin(yaw);
    return {static_cast<float>(pitchCosine * yawCosine),
            static_cast<float>(pitchCosine * yawSine),
            static_cast<float>(-pitchSine),
            static_cast<float>(-yawSine),
            static_cast<float>(yawCosine),
            0.0f,
            static_cast<float>(pitchSine * yawCosine),
            static_cast<float>(pitchSine * yawSine),
            static_cast<float>(pitchCosine)};
}

bool TestGravityRelativeMouseAxis() {
    constexpr std::array<double, 5> pitches{0.0, 45.0, -45.0, 89.0, -89.0};
    constexpr double initialYaw = 37.0;
    constexpr double yawDelta = -13.0;
    constexpr double pitchDelta = 1.0;
    for (const double pitch : pitches) {
        const auto base = MakeGravityAlignedTestAxis(pitch, initialYaw);
        const auto expected = MakeGravityAlignedTestAxis(
            pitch + pitchDelta, initialYaw + yawDelta);
        std::array<float, 9> actual{};
        if (!ApplyGravityRelativeViewAngles(base, pitch, yawDelta, pitchDelta,
                                            actual)) {
            return false;
        }
        for (std::size_t index = 0; index < actual.size(); ++index) {
            if (std::abs(static_cast<double>(actual[index] - expected[index])) >
                2.0e-5) {
                return false;
            }
        }
    }
    return true;
}

bool ApplyPendingMouseOverlay(const void* baseView,
                              std::array<std::uint8_t, kRenderViewSize>& temporary,
                              bool temporaryInitialized,
                              const void* authoritativeView,
                              double interpolationAlpha,
                              double basePitch,
                              bool basePitchValid,
                              MouseOverlay& overlay) {
    if (!g_mouseInterpolationRequested || baseView == nullptr) return false;

    double trackedYaw = 0.0;
    double trackedPitch = 0.0;
    const bool tracked = GetTrackedMouseOverlay(
        authoritativeView, interpolationAlpha, temporaryInitialized, trackedYaw,
        trackedPitch);
    double yaw = trackedYaw;
    double pitch = trackedPitch;

    int deltaX = 0;
    int deltaY = 0;
    const bool peeked = PeekPendingMouse(deltaX, deltaY);

    void* usercmd = g_usercmdGenerator.load(std::memory_order_acquire);
    if (peeked) {
        if (!IsReadableRange(usercmd, 0xaf4) ||
            ReadUnaligned<int>(usercmd, 0x294) != 0 ||
            ReadUnaligned<int>(usercmd, 0x54) > 0 ||
            std::abs(static_cast<double>(deltaX)) > 1000.0 ||
            std::abs(static_cast<double>(deltaY)) > 1000.0) {
            ++g_mouseCounters.skippedState;
        } else {
            int smoothing = 0;
            if (!ReadEngineCvarInteger(kSmoothCvarPointerRva, smoothing) ||
                smoothing != 1) {
                ++g_mouseCounters.skippedSmoothing;
            } else {
                float sensitivity = 0.0f;
                float yawScale = 0.0f;
                float pitchScale = 0.0f;
                const float gameSensitivity =
                    ReadUnaligned<float>(usercmd, 0xaf0);
                if (!std::isfinite(gameSensitivity) ||
                    !ReadEngineCvarFloat(kSensitivityCvarPointerRva,
                                         sensitivity) ||
                    !ReadEngineCvarFloat(kYawCvarPointerRva, yawScale) ||
                    !ReadEngineCvarFloat(kPitchCvarPointerRva, pitchScale)) {
                    ++g_mouseCounters.skippedState;
                } else {
                    yaw += -static_cast<double>(yawScale) * sensitivity *
                           gameSensitivity * deltaX;
                    const bool mouseLook =
                        (ReadUnaligned<std::uint8_t>(usercmd, 0x2ac) & 0x20) != 0;
                    if (mouseLook) {
                        pitch += static_cast<double>(pitchScale) * sensitivity *
                                 gameSensitivity * deltaY;
                    }
                }
            }
        }
    }
    if ((!std::isfinite(yaw) || !std::isfinite(pitch)) ||
        std::abs(yaw) > kMaximumMouseOverlayDegrees ||
        std::abs(pitch) > kMaximumMouseOverlayDegrees) {
        ++g_mouseCounters.skippedState;
        return false;
    }
    if (yaw == 0.0 && pitch == 0.0) return false;

    if (!basePitchValid) {
        ++g_mouseCounters.skippedState;
        return false;
    }
    // DetermineViewAngles clamps the player's effective pitch independently of
    // the raw generator angle.  Clamp the presentation-only prediction too so
    // input beyond the pole cannot be displayed and then pulled back next tic.
    pitch = std::clamp(basePitch + pitch, -89.0, 89.0) - basePitch;
    if (!std::isfinite(basePitch) || !std::isfinite(pitch)) {
        ++g_mouseCounters.skippedState;
        return false;
    }

    if (!temporaryInitialized) {
        std::memcpy(temporary.data(), baseView, temporary.size());
    }
    overlay.cameraOrigin = ReadOrigin(temporary.data());
    overlay.baseAxis = ReadAxis(temporary.data());
    if (!ApplyGravityRelativeViewAngles(overlay.baseAxis, basePitch, yaw, pitch,
                                        overlay.overlaidAxis)) {
        ++g_mouseCounters.skippedState;
        return false;
    }
    overlay.applied = true;
    std::memcpy(temporary.data() + 40, overlay.overlaidAxis.data(),
                sizeof(overlay.overlaidAxis));
    ++g_mouseCounters.overlayViews;
    if (tracked) ++g_mouseCounters.trackedOverlayViews;
    return true;
}

double OriginDistance(const void* left, const void* right) {
    const auto a = ReadOrigin(left);
    const auto b = ReadOrigin(right);
    const double x = static_cast<double>(a[0] - b[0]);
    const double y = static_cast<double>(a[1] - b[1]);
    const double z = static_cast<double>(a[2] - b[2]);
    return std::sqrt(x * x + y * y + z * z);
}

double AxisAngleDegrees(const void* left, const void* right) {
    const Quaternion a = MatrixToQuaternion(ReadAxis(left));
    const Quaternion b = MatrixToQuaternion(ReadAxis(right));
    const double dot = std::clamp(std::abs(QuaternionDot(a, b)), 0.0, 1.0);
    return 2.0 * std::acos(dot) * 57.29577951308232;
}

ViewEntityPose ReadEntityPose(const void* entity) {
    ViewEntityPose result;
    std::memcpy(result.origin.data(),
                static_cast<const std::uint8_t*>(entity) +
                    kRenderEntityOriginOffset,
                sizeof(result.origin));
    std::memcpy(result.axis.data(),
                static_cast<const std::uint8_t*>(entity) +
                    kRenderEntityAxisOffset,
                sizeof(result.axis));
    return result;
}

RenderEntityIdentity ReadEntityIdentity(const void* entity) {
    return {
        ReadUnaligned<std::uintptr_t>(entity, 0),
        ReadUnaligned<int>(entity, 4),
        ReadUnaligned<int>(entity, 8),
        ReadUnaligned<std::uintptr_t>(entity, 36),
        ReadUnaligned<std::uintptr_t>(entity, 40),
    };
}

double PoseOriginDistance(const ViewEntityPose& left,
                          const ViewEntityPose& right) {
    const double x = static_cast<double>(left.origin[0] - right.origin[0]);
    const double y = static_cast<double>(left.origin[1] - right.origin[1]);
    const double z = static_cast<double>(left.origin[2] - right.origin[2]);
    return std::sqrt(x * x + y * y + z * z);
}

double PoseAxisAngleDegrees(const ViewEntityPose& left,
                            const ViewEntityPose& right) {
    const Quaternion a = MatrixToQuaternion(left.axis);
    const Quaternion b = MatrixToQuaternion(right.axis);
    const double dot = std::clamp(std::abs(QuaternionDot(a, b)), 0.0, 1.0);
    return 2.0 * std::acos(dot) * 57.29577951308232;
}

TrackedRenderEntity* FindTrackedRenderEntity(int handle) {
    if (handle < 0 ||
        static_cast<std::size_t>(handle) >= g_renderEntities.size()) {
        return nullptr;
    }
    auto& entity = g_renderEntities[static_cast<std::size_t>(handle)];
    return entity.handle == handle ? &entity : nullptr;
}

void ForgetRenderEntity(int handle) {
    auto* entity = FindTrackedRenderEntity(handle);
    if (entity == nullptr) return;
    const std::size_t removedIndex = entity->activeIndex;
    const std::size_t lastIndex = --g_activeRenderEntityCount;
    if (removedIndex != lastIndex) {
        const int movedHandle = g_activeRenderEntityHandles[lastIndex];
        g_activeRenderEntityHandles[removedIndex] = movedHandle;
        if (auto* moved = FindTrackedRenderEntity(movedHandle)) {
            moved->activeIndex = removedIndex;
        }
    }
    if (entity->allowViewId == 0 && entity->animation.hasCurrent) {
        const std::size_t joints = entity->animation.current.size();
        g_trackedWorldJointMatrices = joints <= g_trackedWorldJointMatrices
            ? g_trackedWorldJointMatrices - joints
            : 0;
    }
    *entity = {};
}

void ResetTrackedEntityInterpolation() {
    for (std::size_t index = 0; index < g_activeRenderEntityCount; ++index) {
        auto* entity = FindTrackedRenderEntity(g_activeRenderEntityHandles[index]);
        if (entity == nullptr || !entity->hasCurrent) continue;
        entity->previous = entity->current;
        entity->canInterpolate = false;
        entity->transitionSamples = 0;
        if (entity->animation.hasCurrent) {
            entity->animation.previous = entity->animation.current;
            entity->animation.canInterpolate = false;
            entity->animation.transitionSamples = 0;
        }
    }
}

void ClearTrackedJointAnimation(TrackedRenderEntity& tracked) {
    if (tracked.allowViewId == 0 && tracked.animation.hasCurrent) {
        const std::size_t joints = tracked.animation.current.size();
        g_trackedWorldJointMatrices = joints <= g_trackedWorldJointMatrices
            ? g_trackedWorldJointMatrices - joints
            : 0;
    }
    const bool cacheWasInterpolated = tracked.animation.cacheWasInterpolated;
    tracked.animation = {};
    tracked.animation.cacheWasInterpolated = cacheWasInterpolated;
}

void CountAnimationSkip(const TrackedRenderEntity& tracked) {
    if (tracked.allowViewId == 0) {
        ++g_viewCounters.worldAnimationSkips;
    } else {
        ++g_viewCounters.viewModelAnimationSkips;
    }
}

void RecalculateTrackedWorldJointMatrices() {
    std::size_t total = 0;
    for (std::size_t index = 0; index < g_activeRenderEntityCount; ++index) {
        const auto* entity =
            FindTrackedRenderEntity(g_activeRenderEntityHandles[index]);
        if (entity != nullptr && entity->allowViewId == 0 &&
            entity->animation.hasCurrent) {
            total += entity->animation.current.size();
        }
    }
    g_trackedWorldJointMatrices = total;
}

bool ViewModelJointPoseDiscontinuous(
    const std::vector<JointMatrix>& previous,
    const std::vector<JointMatrix>& current) {
    if (previous.size() != current.size()) return true;
    for (std::size_t index = 0; index < current.size(); ++index) {
        const auto& from = previous[index].values;
        const auto& to = current[index].values;
        const double x = static_cast<double>(to[3] - from[3]);
        const double y = static_cast<double>(to[7] - from[7]);
        const double z = static_cast<double>(to[11] - from[11]);
        if (std::sqrt(x * x + y * y + z * z) >
            kMaximumViewModelJointStep) {
            return true;
        }
        const std::array<float, 9> fromAxis{
            from[0], from[4], from[8],
            from[1], from[5], from[9],
            from[2], from[6], from[10]};
        const std::array<float, 9> toAxis{
            to[0], to[4], to[8],
            to[1], to[5], to[9],
            to[2], to[6], to[10]};
        const double dot = std::clamp(
            std::abs(QuaternionDot(MatrixToQuaternion(fromAxis),
                                   MatrixToQuaternion(toAxis))),
            0.0, 1.0);
        if (2.0 * std::acos(dot) * 57.29577951308232 >
            kMaximumViewModelJointAngleDegrees) {
            return true;
        }
    }
    return false;
}

void ObserveRenderEntityJoints(TrackedRenderEntity& tracked,
                               const void* renderEntity) {
    const bool animationRequested = tracked.allowViewId == 0
        ? g_worldAnimationInterpolationRequested
        : g_viewModelAnimationInterpolationRequested;
    if (!animationRequested) return;

    const int count = ReadUnaligned<int>(renderEntity,
                                         kRenderEntityNumJointsOffset);
    const auto* joints = ReadUnaligned<const JointMatrix*>(
        renderEntity, kRenderEntityJointsOffset);
    if (count <= 0 || joints == nullptr) {
        ClearTrackedJointAnimation(tracked);
        return;
    }
    if (count > static_cast<int>(kMaximumEntityJoints) ||
        !IsReadableRange(joints,
                         static_cast<std::size_t>(count) * sizeof(*joints))) {
        ClearTrackedJointAnimation(tracked);
        CountAnimationSkip(tracked);
        return;
    }

    try {
        std::vector<JointMatrix> sample(static_cast<std::size_t>(count));
        std::memcpy(sample.data(), joints, sample.size() * sizeof(sample[0]));
        for (const auto& joint : sample) {
            for (const float value : joint.values) {
                if (!std::isfinite(value)) {
                    ClearTrackedJointAnimation(tracked);
                    CountAnimationSkip(tracked);
                    return;
                }
            }
        }

        auto& animation = tracked.animation;
        if (!animation.hasCurrent ||
            animation.current.size() != sample.size()) {
            std::size_t retainedWorldCount = 0;
            if (tracked.allowViewId == 0) {
                const std::size_t oldCount = animation.hasCurrent
                    ? animation.current.size()
                    : 0;
                retainedWorldCount =
                    oldCount <= g_trackedWorldJointMatrices
                        ? g_trackedWorldJointMatrices - oldCount
                        : 0;
                if (sample.size() >
                    kMaximumTrackedWorldJointMatrices -
                        (std::min)(retainedWorldCount,
                                   kMaximumTrackedWorldJointMatrices)) {
                    ClearTrackedJointAnimation(tracked);
                    CountAnimationSkip(tracked);
                    return;
                }
            }
            animation.previous = sample;
            animation.current = std::move(sample);
            animation.interpolated.resize(animation.current.size());
            animation.hasCurrent = true;
            animation.canInterpolate = false;
            animation.transitionGeneration = g_cameraSnapshotGeneration + 1;
            animation.transitionSamples = 0;
            animation.sourceJoints = joints;
            if (tracked.allowViewId == 0) {
                g_trackedWorldJointMatrices =
                    retainedWorldCount + animation.current.size();
            }
            return;
        }
        if (std::memcmp(sample.data(), animation.current.data(),
                        sample.size() * sizeof(sample[0])) == 0) {
            animation.sourceJoints = joints;
            return;
        }

        const std::uint64_t transitionGeneration =
            g_cameraSnapshotGeneration + 1;
        const bool viewModelDiscontinuity = tracked.allowViewId != 0 &&
            (animation.sourceJoints != joints ||
             ViewModelJointPoseDiscontinuous(animation.current, sample));
        if (viewModelDiscontinuity) {
            animation.previous = sample;
            animation.current = std::move(sample);
            animation.interpolated.resize(animation.current.size());
            animation.transitionGeneration = transitionGeneration;
            animation.transitionSamples = 0;
            animation.sourceJoints = joints;
            animation.canInterpolate = false;
            return;
        }
        const std::uint32_t transitionSamples =
            animation.transitionGeneration == transitionGeneration
                ? (std::min)(animation.transitionSamples + 1u, 3u)
                : 1u;
        animation.previous = std::move(animation.current);
        animation.current = std::move(sample);
        animation.interpolated.resize(animation.current.size());
        animation.transitionGeneration = transitionGeneration;
        animation.transitionSamples = transitionSamples;
        animation.sourceJoints = joints;
        animation.canInterpolate =
            transitionGeneration >= tracked.firstInterpolatableGeneration;
    } catch (...) {
        ClearTrackedJointAnimation(tracked);
        RecalculateTrackedWorldJointMatrices();
        CountAnimationSkip(tracked);
    }
}

void ClearTrackedRenderEntities() {
    for (std::size_t index = 0; index < g_activeRenderEntityCount; ++index) {
        const int handle = g_activeRenderEntityHandles[index];
        if (handle >= 0 &&
            static_cast<std::size_t>(handle) < g_renderEntities.size()) {
            auto& tracked = g_renderEntities[static_cast<std::size_t>(handle)];
            if (tracked.allowViewId == 0 && tracked.animation.hasCurrent) {
                const std::size_t joints = tracked.animation.current.size();
                g_trackedWorldJointMatrices =
                    joints <= g_trackedWorldJointMatrices
                        ? g_trackedWorldJointMatrices - joints
                        : 0;
            }
            tracked = {};
        }
    }
    g_activeRenderEntityCount = 0;
    g_trackedWorldJointMatrices = 0;
}

void ObserveRenderEntity(int handle, const void* renderEntity, bool created) {
    if (handle < 0 || renderEntity == nullptr) return;
    const int allowViewId = ReadUnaligned<int>(
        renderEntity, kRenderEntityAllowViewIdOffset);
    const bool shouldTrack = allowViewId == 0
        ? (g_worldInterpolationRequested ||
           g_worldAnimationInterpolationRequested)
        : (g_viewModelInterpolationRequested ||
           g_viewModelAnimationInterpolationRequested);
    if (!shouldTrack) {
        ForgetRenderEntity(handle);
        return;
    }

    if (static_cast<std::size_t>(handle) >= g_renderEntities.size()) {
        ++g_viewCounters.worldEntityCapacitySkips;
        return;
    }

    const RenderEntityIdentity identity = ReadEntityIdentity(renderEntity);
    TrackedRenderEntity* tracked = FindTrackedRenderEntity(handle);
    if (created ||
        (tracked != nullptr && (tracked->allowViewId != allowViewId ||
                                !(tracked->identity == identity)))) {
        ForgetRenderEntity(handle);
        tracked = nullptr;
    }
    if (tracked == nullptr) {
        if (g_activeRenderEntityCount >= g_activeRenderEntityHandles.size()) {
            ++g_viewCounters.worldEntityCapacitySkips;
            return;
        }
        tracked = &g_renderEntities[static_cast<std::size_t>(handle)];
        *tracked = {};
        tracked->handle = handle;
        tracked->allowViewId = allowViewId;
        tracked->identity = identity;
        tracked->generation = g_nextRenderEntityGeneration++;
        if (g_nextRenderEntityGeneration == 0) g_nextRenderEntityGeneration = 1;
        tracked->revision = 1;
        tracked->firstInterpolatableGeneration =
            g_cameraSnapshotGeneration + 2;
        tracked->activeIndex = g_activeRenderEntityCount;
        g_activeRenderEntityHandles[g_activeRenderEntityCount++] = handle;
    } else {
        ++tracked->revision;
        if (tracked->revision == 0) tracked->revision = 1;
    }

    const ViewEntityPose pose = ReadEntityPose(renderEntity);
    tracked->allowViewId = allowViewId;
    ObserveRenderEntityJoints(*tracked, renderEntity);
    if (!tracked->hasCurrent) {
        tracked->previous = pose;
        tracked->current = pose;
        tracked->hasCurrent = true;
        tracked->canInterpolate = false;
        tracked->transitionSamples = 0;
        return;
    }
    if (std::memcmp(&pose, &tracked->current, sizeof(pose)) == 0) return;

    const bool worldEntity = allowViewId == 0;
    const double maximumStep = worldEntity
        ? g_maximumWorldEntityStep
        : kMaximumViewModelStep;
    const double maximumAngle = worldEntity
        ? g_maximumWorldEntityAngleDegrees
        : kMaximumViewModelAngleDegrees;
    const std::uint64_t transitionGeneration =
        g_cameraSnapshotGeneration + 1;
    const bool thresholdReset =
        PoseOriginDistance(tracked->current, pose) > maximumStep ||
        PoseAxisAngleDegrees(tracked->current, pose) > maximumAngle;
    const bool reset = thresholdReset ||
        transitionGeneration < tracked->firstInterpolatableGeneration;
    if (reset) {
        tracked->previous = pose;
        tracked->current = pose;
        tracked->canInterpolate = false;
        tracked->transitionGeneration = transitionGeneration;
        tracked->transitionSamples = 0;
        if (worldEntity && thresholdReset) {
            ++g_viewCounters.worldEntityResets;
        }
    } else {
        const std::uint32_t transitionSamples =
            tracked->transitionGeneration == transitionGeneration
                ? (std::min)(tracked->transitionSamples + 1u, 3u)
                : 1u;
        tracked->previous = tracked->current;
        tracked->current = pose;
        tracked->canInterpolate = true;
        tracked->transitionGeneration = transitionGeneration;
        tracked->transitionSamples = transitionSamples;
    }
}

int __fastcall HookedAddEntityDef(void* self, void*, const void* renderEntity) {
    const int handle = g_originalAddEntityDef(self, renderEntity);
    if (self == g_renderWorld) ObserveRenderEntity(handle, renderEntity, true);
    return handle;
}

void __fastcall HookedUpdateEntityDef(void* self, void*, int handle,
                                      const void* renderEntity) {
    g_originalUpdateEntityDef(self, handle, renderEntity);
    if (self == g_renderWorld) {
        if (auto* tracked = FindTrackedRenderEntity(handle)) {
            tracked->animation.cacheWasInterpolated = false;
        }
        ObserveRenderEntity(handle, renderEntity, false);
    }
}

void __fastcall HookedFreeEntityDef(void* self, void*, int handle) {
    if (self == g_renderWorld) ForgetRenderEntity(handle);
    g_originalFreeEntityDef(self, handle);
}

struct AppliedViewEntityPose {
    int handle = -1;
    std::uint32_t generation = 0;
    std::uint64_t revision = 0;
    void* renderEntity = nullptr;
    void* modelMatrix = nullptr;
    void* originalJoints = nullptr;
    bool poseApplied = false;
    bool jointsApplied = false;
    std::array<std::uint8_t, kRenderEntityPoseSize> original{};
    std::array<std::uint8_t, kRenderEntityModelMatrixSize> originalModelMatrix{};
};

std::array<AppliedViewEntityPose, kMaximumAppliedEntityPoses>
    g_appliedEntityPoses{};

std::array<float, 16> PoseToModelMatrix(const ViewEntityPose& pose) {
    return {
        pose.axis[0], pose.axis[1], pose.axis[2], 0.0f,
        pose.axis[3], pose.axis[4], pose.axis[5], 0.0f,
        pose.axis[6], pose.axis[7], pose.axis[8], 0.0f,
        pose.origin[0], pose.origin[1], pose.origin[2], 1.0f,
    };
}

void InterpolateJointMatrices(TrackedJointAnimation& animation,
                              double alpha) {
    for (std::size_t index = 0; index < animation.current.size(); ++index) {
        const auto& from = animation.previous[index].values;
        const auto& to = animation.current[index].values;
        auto& result = animation.interpolated[index].values;
        const std::array<float, 9> fromAxis{
            from[0], from[4], from[8],
            from[1], from[5], from[9],
            from[2], from[6], from[10],
        };
        const std::array<float, 9> toAxis{
            to[0], to[4], to[8],
            to[1], to[5], to[9],
            to[2], to[6], to[10],
        };
        const auto axis = QuaternionToMatrix(
            Slerp(MatrixToQuaternion(fromAxis), MatrixToQuaternion(toAxis),
                  alpha));
        result = {
            axis[0], axis[3], axis[6],
            static_cast<float>(from[3] + (to[3] - from[3]) * alpha),
            axis[1], axis[4], axis[7],
            static_cast<float>(from[7] + (to[7] - from[7]) * alpha),
            axis[2], axis[5], axis[8],
            static_cast<float>(from[11] + (to[11] - from[11]) * alpha),
        };
    }
}

bool ClearRenderEntityDynamicModel(const void* renderEntity) {
    if (g_animationRendererState != 1 ||
        g_clearEntityDefDynamicModel == nullptr ||
        g_renderEntityModelMatrixOffset !=
            kRetailRenderEntityModelMatrixOffset) {
        return false;
    }
    auto* local = const_cast<std::uint8_t*>(
        static_cast<const std::uint8_t*>(renderEntity)) -
        kRenderEntityLocalPrefixSize;
    if (!IsReadableRange(local, kRenderEntityLocalValidationSize)) return false;
    g_clearEntityDefDynamicModel(local);
    return true;
}

std::array<float, 3> RotateWithMouseOverlay(
    const std::array<float, 3>& vector, const MouseOverlay& overlay) {
    std::array<double, 3> local{};
    for (std::size_t basis = 0; basis < 3; ++basis) {
        for (std::size_t component = 0; component < 3; ++component) {
            local[basis] += static_cast<double>(vector[component]) *
                            overlay.baseAxis[basis * 3 + component];
        }
    }
    std::array<float, 3> result{};
    for (std::size_t component = 0; component < 3; ++component) {
        double value = 0.0;
        for (std::size_t basis = 0; basis < 3; ++basis) {
            value += local[basis] *
                     overlay.overlaidAxis[basis * 3 + component];
        }
        result[component] = static_cast<float>(value);
    }
    return result;
}

void ApplyMouseOverlayToPose(ViewEntityPose& pose,
                             const MouseOverlay& overlay) {
    if (!overlay.applied) return;
    std::array<float, 3> relative{};
    for (std::size_t index = 0; index < relative.size(); ++index) {
        relative[index] = pose.origin[index] - overlay.cameraOrigin[index];
    }
    const auto rotatedOrigin = RotateWithMouseOverlay(relative, overlay);
    for (std::size_t index = 0; index < relative.size(); ++index) {
        pose.origin[index] = overlay.cameraOrigin[index] + rotatedOrigin[index];
    }
    for (std::size_t basis = 0; basis < 3; ++basis) {
        std::array<float, 3> vector{
            pose.axis[basis * 3], pose.axis[basis * 3 + 1],
            pose.axis[basis * 3 + 2]};
        const auto rotated = RotateWithMouseOverlay(vector, overlay);
        for (std::size_t component = 0; component < 3; ++component) {
            pose.axis[basis * 3 + component] = rotated[component];
        }
    }
}

bool ResolveRenderEntityModelMatrixOffset(
    const void* renderEntity, const std::array<float, 16>& expectedMatrix) {
    if (g_renderEntityModelMatrixOffset != 0) {
        return IsReadableRange(
            static_cast<const std::uint8_t*>(renderEntity) +
                g_renderEntityModelMatrixOffset,
            kRenderEntityModelMatrixSize);
    }

    if (!IsReadableRange(
            static_cast<const std::uint8_t*>(renderEntity) +
                kRenderEntityModelMatrixSearchBegin,
            kRenderEntityModelMatrixSearchEnd -
                kRenderEntityModelMatrixSearchBegin +
                kRenderEntityModelMatrixSize)) {
        return false;
    }

    std::size_t match = 0;
    std::size_t matches = 0;
    const auto* bytes = static_cast<const std::uint8_t*>(renderEntity);
    for (std::size_t offset = kRenderEntityModelMatrixSearchBegin;
         offset <= kRenderEntityModelMatrixSearchEnd; offset += sizeof(float)) {
        if (std::memcmp(bytes + offset, expectedMatrix.data(),
                        sizeof(expectedMatrix)) == 0) {
            match = offset;
            ++matches;
        }
    }
    if (matches != 1) return false;

    g_renderEntityModelMatrixOffset = match;
    char buffer[128]{};
    const int length = _snprintf_s(
        buffer, sizeof(buffer), _TRUNCATE,
        "entity: validated cached model matrix at renderEntity+0x%zx\r\n",
        match);
    if (length > 0) Log(std::string(buffer, static_cast<std::size_t>(length)));
    return true;
}

double EntityInterpolationAlpha(bool pending, std::uint32_t transitionSamples,
                                double cameraAlpha, double latestTicAlpha,
                                double pendingTicAlpha,
                                bool multiTicEntityAlignment,
                                bool overdueSnapshotFallback);

std::size_t ApplyInterpolatedRenderEntities(
    int viewId, double cameraAlpha, double latestTicAlpha,
    double pendingTicAlpha,
    bool cameraInterpolated,
    const MouseOverlay& mouseOverlay,
    std::array<AppliedViewEntityPose, kMaximumAppliedEntityPoses>& applied) {
    if ((!g_viewModelInterpolationRequested && !g_worldInterpolationRequested &&
         !g_viewModelAnimationInterpolationRequested &&
         !g_worldAnimationInterpolationRequested) ||
        g_renderWorld == nullptr ||
        g_getRenderEntity == nullptr || g_renderEntityHookState != 1 ||
        (viewId == 0 && !g_worldInterpolationRequested &&
         !g_worldAnimationInterpolationRequested)) return 0;

    std::size_t count = 0;
    std::uint64_t adjustedViewModels = 0;
    std::uint64_t adjustedWorldEntities = 0;
    for (std::size_t activeIndex = 0;
         activeIndex < g_activeRenderEntityCount; ++activeIndex) {
        const int handle = g_activeRenderEntityHandles[activeIndex];
        auto* tracked = FindTrackedRenderEntity(handle);
        if (tracked == nullptr || !tracked->hasCurrent) continue;
        const bool viewModel = tracked->allowViewId != 0;
        const bool currentPose = tracked->canInterpolate &&
            tracked->transitionGeneration == g_cameraSnapshotGeneration;
        const bool pendingPose = g_overdueSnapshotFallbackRequested &&
            tracked->canInterpolate &&
            tracked->transitionGeneration == g_cameraSnapshotGeneration + 1;
        const bool interpolatePose = currentPose || pendingPose;
        const bool applyRoot = viewModel
            ? g_viewModelInterpolationRequested &&
                  tracked->allowViewId == viewId &&
                  ((cameraInterpolated && interpolatePose) ||
                   mouseOverlay.applied)
            : g_worldInterpolationRequested && cameraInterpolated &&
                  interpolatePose;
        const bool animationRequested = viewModel
            ? g_viewModelAnimationInterpolationRequested
            : g_worldAnimationInterpolationRequested;
        const bool animationMatchesView = !viewModel ||
            tracked->allowViewId == viewId;
        const bool currentAnimation = tracked->animation.canInterpolate &&
            tracked->animation.transitionGeneration ==
                g_cameraSnapshotGeneration;
        const bool pendingAnimation = g_overdueSnapshotFallbackRequested &&
            tracked->animation.canInterpolate &&
            tracked->animation.transitionGeneration ==
                g_cameraSnapshotGeneration + 1;
        const bool applyAnimation = animationRequested &&
            animationMatchesView && cameraInterpolated &&
            tracked->animation.hasCurrent &&
            (currentAnimation || pendingAnimation);
        const bool restoreAnimationCache = animationRequested &&
            animationMatchesView &&
            tracked->animation.cacheWasInterpolated && !applyAnimation;
        if (viewModel) {
            if (!applyRoot && !applyAnimation && !restoreAnimationCache) {
                continue;
            }
        } else if (!applyRoot && !applyAnimation &&
                   !restoreAnimationCache) {
            continue;
        }
        const void* liveConst = g_getRenderEntity(g_renderWorld, tracked->handle);
        if (liveConst == nullptr ||
            ReadUnaligned<int>(liveConst, kRenderEntityAllowViewIdOffset) !=
                tracked->allowViewId) {
            continue;
        }
        const ViewEntityPose livePose = ReadEntityPose(liveConst);
        const auto expectedLiveMatrix = PoseToModelMatrix(livePose);
        if (!ResolveRenderEntityModelMatrixOffset(liveConst,
                                                  expectedLiveMatrix)) {
            continue;
        }
        const auto* liveMatrix = static_cast<const std::uint8_t*>(liveConst) +
            g_renderEntityModelMatrixOffset;
        if (applyRoot &&
            std::memcmp(liveMatrix, expectedLiveMatrix.data(),
                        sizeof(expectedLiveMatrix)) != 0) {
            continue;
        }
        if (restoreAnimationCache) {
            if (ClearRenderEntityDynamicModel(liveConst)) {
                tracked->animation.cacheWasInterpolated = false;
            } else {
                CountAnimationSkip(*tracked);
            }
        }
        if (!applyRoot && !applyAnimation) continue;
        if (count >= applied.size()) {
            if (viewModel) {
                if (applyAnimation) {
                    ++g_viewCounters.viewModelAnimationSkips;
                }
            } else {
                if (applyRoot) {
                    ++g_viewCounters.worldEntityCapacitySkips;
                }
                if (applyAnimation) {
                    ++g_viewCounters.worldAnimationSkips;
                }
            }
            continue;
        }
        auto* live = const_cast<void*>(liveConst);
        auto& saved = applied[count++];
        saved = {};
        saved.handle = tracked->handle;
        saved.generation = tracked->generation;
        saved.revision = tracked->revision;
        saved.renderEntity = live;
        if (applyRoot) {
            saved.poseApplied = true;
            saved.modelMatrix = const_cast<std::uint8_t*>(liveMatrix);
            std::memcpy(saved.original.data(),
                        static_cast<std::uint8_t*>(live) +
                            kRenderEntityOriginOffset,
                        saved.original.size());
            std::memcpy(saved.originalModelMatrix.data(), liveMatrix,
                        saved.originalModelMatrix.size());
        }

        if (applyRoot) {
            const bool useLatestTicAlpha =
                g_multiTicEntityAlignmentRequested &&
                tracked->transitionSamples > 1;
            const double rootAlpha = EntityInterpolationAlpha(
                pendingPose, tracked->transitionSamples, cameraAlpha,
                latestTicAlpha, pendingTicAlpha,
                g_multiTicEntityAlignmentRequested,
                g_overdueSnapshotFallbackRequested);
            if (pendingPose) {
                if (viewModel) {
                    ++g_viewCounters.viewModelPendingRoots;
                } else {
                    ++g_viewCounters.worldPendingRoots;
                }
            } else if (useLatestTicAlpha) {
                if (viewModel) {
                    ++g_viewCounters.viewModelLatestTicRoots;
                } else {
                    ++g_viewCounters.worldLatestTicRoots;
                }
            }
            ViewEntityPose interpolatedPose;
            for (std::size_t index = 0;
                 index < tracked->current.origin.size(); ++index) {
                interpolatedPose.origin[index] = interpolatePose
                    ? static_cast<float>(
                          tracked->previous.origin[index] +
                          (tracked->current.origin[index] -
                           tracked->previous.origin[index]) * rootAlpha)
                    : tracked->current.origin[index];
            }
            interpolatedPose.axis = interpolatePose
                ? QuaternionToMatrix(
                      Slerp(MatrixToQuaternion(tracked->previous.axis),
                            MatrixToQuaternion(tracked->current.axis), rootAlpha))
                : tracked->current.axis;
            if (viewModel) ApplyMouseOverlayToPose(interpolatedPose, mouseOverlay);
            std::memcpy(
                static_cast<std::uint8_t*>(live) + kRenderEntityOriginOffset,
                interpolatedPose.origin.data(), sizeof(interpolatedPose.origin));
            std::memcpy(
                static_cast<std::uint8_t*>(live) + kRenderEntityAxisOffset,
                interpolatedPose.axis.data(), sizeof(interpolatedPose.axis));
            const auto modelMatrix = PoseToModelMatrix(interpolatedPose);
            std::memcpy(saved.modelMatrix, modelMatrix.data(),
                        sizeof(modelMatrix));
            if (viewModel) {
                ++adjustedViewModels;
            } else {
                ++adjustedWorldEntities;
            }
        }
        if (applyAnimation) {
            const bool useLatestTicAlpha =
                g_multiTicEntityAlignmentRequested &&
                tracked->animation.transitionSamples > 1;
            const double animationAlpha = EntityInterpolationAlpha(
                pendingAnimation, tracked->animation.transitionSamples,
                cameraAlpha, latestTicAlpha, pendingTicAlpha,
                g_multiTicEntityAlignmentRequested,
                g_overdueSnapshotFallbackRequested);
            if (pendingAnimation) {
                if (viewModel) {
                    ++g_viewCounters.viewModelPendingAnimations;
                } else {
                    ++g_viewCounters.worldPendingAnimations;
                }
            } else if (useLatestTicAlpha) {
                if (viewModel) {
                    ++g_viewCounters.viewModelLatestTicAnimations;
                } else {
                    ++g_viewCounters.worldLatestTicAnimations;
                }
            }
            InterpolateJointMatrices(tracked->animation, animationAlpha);
            saved.originalJoints = ReadUnaligned<void*>(
                live, kRenderEntityJointsOffset);
            WriteUnaligned(live, kRenderEntityJointsOffset,
                           tracked->animation.interpolated.data());
            if (ClearRenderEntityDynamicModel(live)) {
                saved.jointsApplied = true;
                tracked->animation.cacheWasInterpolated = true;
                if (viewModel) {
                    ++g_viewCounters.adjustedViewModelAnimations;
                    g_viewCounters.interpolatedViewModelJoints +=
                        tracked->animation.interpolated.size();
                } else {
                    ++g_viewCounters.adjustedWorldAnimations;
                    g_viewCounters.interpolatedWorldJoints +=
                        tracked->animation.interpolated.size();
                }
            } else {
                WriteUnaligned(live, kRenderEntityJointsOffset,
                               saved.originalJoints);
                CountAnimationSkip(*tracked);
            }
        }
    }
    g_viewCounters.adjustedViewModels += adjustedViewModels;
    g_viewCounters.adjustedWorldEntities += adjustedWorldEntities;
    return count;
}

void RestoreRenderEntities(
    std::array<AppliedViewEntityPose, kMaximumAppliedEntityPoses>& applied,
    std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        const auto* tracked = FindTrackedRenderEntity(applied[index].handle);
        if (tracked == nullptr ||
            tracked->generation != applied[index].generation ||
            tracked->revision != applied[index].revision ||
            g_getRenderEntity(g_renderWorld, applied[index].handle) !=
                applied[index].renderEntity) {
            continue;
        }
        if (applied[index].poseApplied) {
            std::memcpy(static_cast<std::uint8_t*>(applied[index].renderEntity) +
                            kRenderEntityOriginOffset,
                        applied[index].original.data(),
                        applied[index].original.size());
            std::memcpy(applied[index].modelMatrix,
                        applied[index].originalModelMatrix.data(),
                        applied[index].originalModelMatrix.size());
        }
        if (applied[index].jointsApplied) {
            WriteUnaligned(applied[index].renderEntity,
                           kRenderEntityJointsOffset,
                           applied[index].originalJoints);
        }
    }
}

bool IsInterpolatableCameraIntervalForMode(std::int64_t milliseconds,
                                           bool continuousSnapshotTiming) {
    return milliseconds == kNativeTicMilliseconds ||
           (continuousSnapshotTiming &&
            milliseconds == kMaximumInterpolatedCameraIntervalMilliseconds);
}

bool IsInterpolatableCameraInterval(std::int64_t milliseconds) {
    return IsInterpolatableCameraIntervalForMode(
        milliseconds, g_continuousSnapshotTimingRequested);
}

std::int64_t CameraIntervalQpc(std::int32_t milliseconds,
                               std::int64_t frequency) {
    return static_cast<std::int64_t>(std::llround(
        static_cast<double>(milliseconds) *
        static_cast<double>(frequency) / 1000.0));
}

double CameraInterpolationAlphaForMode(std::int32_t intervalMilliseconds,
                                       std::int64_t nowQpc,
                                       std::int64_t currentSnapshotQpc,
                                       std::int64_t frequency,
                                       bool overdueSnapshotFallback) {
    if (intervalMilliseconds <= 0 || frequency <= 0) return 1.0;
    const double intervalSeconds =
        static_cast<double>(intervalMilliseconds) / 1000.0;
    const double elapsedSeconds =
        static_cast<double>(nowQpc - currentSnapshotQpc) /
        static_cast<double>(frequency);

    // Present one native tic behind the authoritative state.  For an ordinary
    // 16 ms transition this reduces to elapsed / 16 ms.  If a presentation
    // samples two simulation tics at once (common at 60 Hz), begin halfway
    // through the observed 32 ms span instead of snapping across it.
    const double maximumAlpha = overdueSnapshotFallback
        ? 1.0 + kNativeTicSeconds / intervalSeconds
        : 1.0;
    return std::clamp(
        (intervalSeconds + elapsedSeconds - kNativeTicSeconds) /
            intervalSeconds,
        0.0, maximumAlpha);
}

double CameraInterpolationAlpha(std::int32_t intervalMilliseconds,
                                std::int64_t nowQpc,
                                std::int64_t currentSnapshotQpc,
                                std::int64_t frequency) {
    return CameraInterpolationAlphaForMode(
        intervalMilliseconds, nowQpc, currentSnapshotQpc, frequency,
        g_overdueSnapshotFallbackRequested);
}

double LatestNativeTicInterpolationAlphaForMode(
    std::int64_t nowQpc, std::int64_t currentSnapshotQpc,
    std::int64_t frequency, bool overdueSnapshotFallback) {
    if (frequency <= 0) return 1.0;
    const double elapsedSeconds =
        static_cast<double>(nowQpc - currentSnapshotQpc) /
        static_cast<double>(frequency);
    return std::clamp(elapsedSeconds / kNativeTicSeconds, 0.0,
                      overdueSnapshotFallback ? 2.0 : 1.0);
}

double LatestNativeTicInterpolationAlpha(std::int64_t nowQpc,
                                         std::int64_t currentSnapshotQpc,
                                         std::int64_t frequency) {
    return LatestNativeTicInterpolationAlphaForMode(
        nowQpc, currentSnapshotQpc, frequency,
        g_overdueSnapshotFallbackRequested);
}

double PendingNativeTicInterpolationAlpha(std::int64_t nowQpc,
                                          std::int64_t currentSnapshotQpc,
                                          std::int64_t frequency) {
    if (frequency <= 0) return 1.0;
    const double elapsedSeconds =
        static_cast<double>(nowQpc - currentSnapshotQpc) /
        static_cast<double>(frequency);
    return std::clamp(
        (elapsedSeconds - kNativeTicSeconds) / kNativeTicSeconds,
        0.0, 1.0);
}

double EntityInterpolationAlpha(bool pending, std::uint32_t transitionSamples,
                                double cameraAlpha, double latestTicAlpha,
                                double pendingTicAlpha,
                                bool multiTicEntityAlignment,
                                bool overdueSnapshotFallback) {
    if (pending && overdueSnapshotFallback) return pendingTicAlpha;
    if (transitionSamples > 1 && multiTicEntityAlignment) {
        return latestTicAlpha;
    }
    return cameraAlpha;
}

bool BuildInterpolatedView(const void* view,
                           std::array<std::uint8_t, kRenderViewSize>& temporary,
                           double& interpolationAlpha,
                           double& latestTicAlpha,
                           double& pendingTicAlpha,
                           double& basePitch,
                           bool& basePitchValid) {
    interpolationAlpha = 1.0;
    latestTicAlpha = 1.0;
    pendingTicAlpha = 1.0;
    basePitchValid = ReadEffectivePitch(basePitch);
    if (!g_cameraInterpolationRequested || view == nullptr ||
        g_frequency.QuadPart <= 0) return false;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const auto viewTime = ReadUnaligned<std::int32_t>(view, 80);
    if (!g_haveCameraCurrent) {
        ++g_cameraSnapshotGeneration;
        std::memcpy(g_cameraCurrent.data(), view, g_cameraCurrent.size());
        g_cameraPrevious = g_cameraCurrent;
        g_cameraCurrentPitch = basePitch;
        g_cameraPreviousPitch = basePitch;
        g_cameraCurrentPitchValid = basePitchValid;
        g_cameraPreviousPitchValid = basePitchValid;
        g_haveCameraCurrent = true;
        g_canInterpolateCamera = false;
        g_cameraCurrentQpc = now.QuadPart;
        g_lastCameraCallQpc = now.QuadPart;
        g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
        g_cameraTimelineSynchronized = false;
        ResetTrackedEntityInterpolation();
        ++g_viewCounters.snappedViews;
        return false;
    }

    const double callGap = static_cast<double>(now.QuadPart - g_lastCameraCallQpc) /
                           static_cast<double>(g_frequency.QuadPart);
    g_lastCameraCallQpc = now.QuadPart;
    const auto currentTime = ReadUnaligned<std::int32_t>(g_cameraCurrent.data(), 80);
    if (viewTime == currentTime && callGap > kMaximumCameraStallSeconds) {
        std::memcpy(g_cameraCurrent.data(), view, g_cameraCurrent.size());
        g_cameraPrevious = g_cameraCurrent;
        g_cameraPreviousPitch = g_cameraCurrentPitch;
        g_cameraPreviousPitchValid = g_cameraCurrentPitchValid;
        g_canInterpolateCamera = false;
        g_cameraCurrentQpc = now.QuadPart;
        g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
        g_cameraTimelineSynchronized = false;
        ResetTrackedEntityInterpolation();
        ++g_viewCounters.interpolationResets;
        ++g_viewCounters.resetStall;
    } else if (viewTime != currentTime) {
        ++g_cameraSnapshotGeneration;
        const auto currentViewId =
            ReadUnaligned<std::int32_t>(g_cameraCurrent.data(), 0);
        const auto nextViewId = ReadUnaligned<std::int32_t>(view, 0);
        const auto timeDelta = static_cast<std::int64_t>(viewTime) - currentTime;
        const bool fovChanged =
            std::abs(ReadUnaligned<float>(g_cameraCurrent.data(), 20) -
                     ReadUnaligned<float>(view, 20)) > 0.01f ||
            std::abs(ReadUnaligned<float>(g_cameraCurrent.data(), 24) -
                     ReadUnaligned<float>(view, 24)) > 0.01f;
        const bool timeDeltaChanged = !IsInterpolatableCameraInterval(timeDelta);
        const bool viewIdChanged = currentViewId != nextViewId;
        const bool stalled = callGap > kMaximumCameraStallSeconds;
        const bool originExceeded =
            OriginDistance(g_cameraCurrent.data(), view) > kMaximumCameraStep;
        const bool axisExceeded =
            AxisAngleDegrees(g_cameraCurrent.data(), view) >
            kMaximumCameraAngleDegrees;
        const bool reset = timeDeltaChanged || viewIdChanged || fovChanged ||
                           stalled || originExceeded || axisExceeded;
        if (reset) {
            std::memcpy(g_cameraCurrent.data(), view, g_cameraCurrent.size());
            g_cameraPrevious = g_cameraCurrent;
            g_cameraCurrentPitch = basePitch;
            g_cameraPreviousPitch = basePitch;
            g_cameraCurrentPitchValid = basePitchValid;
            g_cameraPreviousPitchValid = basePitchValid;
            g_canInterpolateCamera = false;
            g_cameraCurrentQpc = now.QuadPart;
            g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
            g_cameraTimelineSynchronized = false;
            ResetTrackedEntityInterpolation();
            ++g_viewCounters.interpolationResets;
            if (timeDeltaChanged) ++g_viewCounters.resetTimeDelta;
            if (viewIdChanged) ++g_viewCounters.resetViewId;
            if (fovChanged) ++g_viewCounters.resetFov;
            if (stalled) ++g_viewCounters.resetStall;
            if (originExceeded) ++g_viewCounters.resetOrigin;
            if (axisExceeded) ++g_viewCounters.resetAxis;
        } else {
            g_cameraPrevious = g_cameraCurrent;
            std::memcpy(g_cameraCurrent.data(), view, g_cameraCurrent.size());
            g_cameraPreviousPitch = g_cameraCurrentPitch;
            g_cameraPreviousPitchValid = g_cameraCurrentPitchValid;
            g_cameraCurrentPitch = basePitch;
            g_cameraCurrentPitchValid = basePitchValid;
            g_canInterpolateCamera = true;
            g_cameraCurrentIntervalMilliseconds =
                static_cast<std::int32_t>(timeDelta);
            if (g_continuousSnapshotTimingRequested) {
                if (g_cameraTimelineSynchronized) {
                    const std::int64_t predictedSnapshotQpc =
                        g_cameraCurrentQpc + CameraIntervalQpc(
                        g_cameraCurrentIntervalMilliseconds,
                        g_frequency.QuadPart);
                    // The first transition is observed up to one presentation
                    // late.  Converge toward an earlier observed boundary,
                    // but never move the established clock forward.
                    g_cameraCurrentQpc =
                        (std::min)(predictedSnapshotQpc, now.QuadPart);
                } else {
                    // Establish a stable phase, then advance it from the
                    // authoritative simulation timestamps.
                    g_cameraCurrentQpc = now.QuadPart;
                    g_cameraTimelineSynchronized = true;
                }
            } else {
                // RC1 compatibility: restart interpolation when this
                // presentation call observes each new snapshot.
                g_cameraCurrentQpc = now.QuadPart;
                g_cameraTimelineSynchronized = false;
            }
        }
    }

    if (!g_canInterpolateCamera) {
        basePitch = g_cameraCurrentPitch;
        basePitchValid = g_cameraCurrentPitchValid;
        ++g_viewCounters.snappedViews;
        return false;
    }

    const double alpha = CameraInterpolationAlpha(
        g_cameraCurrentIntervalMilliseconds, now.QuadPart,
        g_cameraCurrentQpc, g_frequency.QuadPart);
    interpolationAlpha = alpha;
    latestTicAlpha = LatestNativeTicInterpolationAlpha(
        now.QuadPart, g_cameraCurrentQpc, g_frequency.QuadPart);
    pendingTicAlpha = PendingNativeTicInterpolationAlpha(
        now.QuadPart, g_cameraCurrentQpc, g_frequency.QuadPart);
    if (alpha > 1.0) ++g_viewCounters.extrapolatedViews;
    basePitchValid = g_cameraPreviousPitchValid && g_cameraCurrentPitchValid;
    if (basePitchValid) {
        basePitch = g_cameraPreviousPitch +
                    (g_cameraCurrentPitch - g_cameraPreviousPitch) * alpha;
    }
    std::memcpy(temporary.data(), view, temporary.size());
    const auto previousOrigin = ReadOrigin(g_cameraPrevious.data());
    const auto currentOrigin = ReadOrigin(g_cameraCurrent.data());
    for (std::size_t index = 0; index < previousOrigin.size(); ++index) {
        const float value = static_cast<float>(
            previousOrigin[index] +
            (currentOrigin[index] - previousOrigin[index]) * alpha);
        WriteUnaligned(temporary.data(), 28 + index * sizeof(float), value);
    }
    const Quaternion interpolatedAxis =
        Slerp(MatrixToQuaternion(ReadAxis(g_cameraPrevious.data())),
              MatrixToQuaternion(ReadAxis(g_cameraCurrent.data())), alpha);
    const auto matrix = QuaternionToMatrix(interpolatedAxis);
    std::memcpy(temporary.data() + 40, matrix.data(), sizeof(matrix));
    ++g_viewCounters.interpolatedViews;
    return true;
}

void ObserveView(const void* view) {
    ++g_viewCounters.calls;
    if (view == nullptr) {
        ++g_viewCounters.nullViews;
        return;
    }

    const auto viewId = ReadUnaligned<std::int32_t>(view, 0);
    const auto viewTime = ReadUnaligned<std::int32_t>(view, 80);
    std::array<float, 3> origin{
        ReadUnaligned<float>(view, 28),
        ReadUnaligned<float>(view, 32),
        ReadUnaligned<float>(view, 36),
    };
    if (g_havePreviousView) {
        if (viewTime == g_previousViewTime) {
            ++g_viewCounters.sameTime;
        } else {
            ++g_viewCounters.timeChanges;
            const std::int64_t delta = static_cast<std::int64_t>(viewTime) -
                                       static_cast<std::int64_t>(g_previousViewTime);
            if (delta != 16) {
                if (g_viewCounters.discontinuities == 0) {
                    g_viewCounters.minimumViewDelta = delta;
                    g_viewCounters.maximumViewDelta = delta;
                } else {
                    g_viewCounters.minimumViewDelta =
                        std::min(g_viewCounters.minimumViewDelta, delta);
                    g_viewCounters.maximumViewDelta =
                        std::max(g_viewCounters.maximumViewDelta, delta);
                }
                ++g_viewCounters.discontinuities;
                if (delta <= 0) {
                    ++g_viewCounters.viewDeltaNonpositive;
                } else if (delta == 32) {
                    ++g_viewCounters.viewDelta32;
                } else if (delta == 48) {
                    ++g_viewCounters.viewDelta48;
                } else if (delta == 64) {
                    ++g_viewCounters.viewDelta64;
                } else {
                    ++g_viewCounters.viewDeltaOther;
                }
            }
            const double x = static_cast<double>(origin[0] - g_previousViewOrigin[0]);
            const double y = static_cast<double>(origin[1] - g_previousViewOrigin[1]);
            const double z = static_cast<double>(origin[2] - g_previousViewOrigin[2]);
            g_viewCounters.maximumOriginStep = std::max(
                g_viewCounters.maximumOriginStep, std::sqrt(x * x + y * y + z * z));
        }
        if (viewId != g_previousViewId) ++g_viewCounters.viewIdChanges;
    }
    g_previousViewTime = viewTime;
    g_previousViewId = viewId;
    g_previousViewOrigin = origin;
    g_havePreviousView = true;
}

void __fastcall HookedSingleView(void* self, void*, void* hud, const void* view) {
    DWORD expectedThread = 0;
    g_presentationThreadId.compare_exchange_strong(
        expectedThread, GetCurrentThreadId(), std::memory_order_release,
        std::memory_order_relaxed);
    ObserveView(view);
    alignas(16) std::array<std::uint8_t, kRenderViewSize> temporary{};
    double interpolationAlpha = 1.0;
    double latestTicAlpha = 1.0;
    double pendingTicAlpha = 1.0;
    double basePitch = 0.0;
    bool basePitchValid = false;
    const bool cameraInterpolated =
        BuildInterpolatedView(view, temporary, interpolationAlpha,
                              latestTicAlpha, pendingTicAlpha, basePitch,
                              basePitchValid);
    const void* baseView = cameraInterpolated ? temporary.data() : view;
    MouseOverlay mouseOverlay;
    const bool mouseOverlaid = ApplyPendingMouseOverlay(
        baseView, temporary, cameraInterpolated, view, interpolationAlpha,
        basePitch, basePitchValid, mouseOverlay);
    const void* presentedView =
        (cameraInterpolated || mouseOverlaid) ? temporary.data() : view;
    const int viewId = view != nullptr ? ReadUnaligned<int>(view, 0) : 0;
    const std::size_t appliedCount =
        ApplyInterpolatedRenderEntities(viewId, interpolationAlpha,
                                        latestTicAlpha, pendingTicAlpha,
                                        cameraInterpolated, mouseOverlay,
                                        g_appliedEntityPoses);
    g_originalSingleView(self, hud, presentedView);
    RestoreRenderEntities(g_appliedEntityPoses, appliedCount);
}

void EncodeRelativeJump(std::uint8_t* output, const void* instructionAddress,
                        const void* destination) {
    output[0] = 0xe9;
    const auto next = reinterpret_cast<std::uintptr_t>(instructionAddress) + 5;
    const auto target = reinterpret_cast<std::uintptr_t>(destination);
    const auto displacement = static_cast<std::uint32_t>(target - next);
    std::memcpy(output + 1, &displacement, sizeof(displacement));
}

void* __fastcall HookedDetermineViewAngles(void* self, void*, void* output,
                                           const void* command,
                                           void* commandAngles) {
    void* result = g_originalDetermineViewAngles(self, output, command,
                                                  commandAngles);
    const void* angles = result != nullptr ? result : output;
    if (IsReadableRange(angles, 3 * sizeof(float))) {
        PublishEffectivePitch(ReadUnaligned<float>(angles, 0));
    }
    return result;
}

bool WriteVtableSlot(void** slot, void* value) {
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) {
        return false;
    }
    *slot = value;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    return *slot == value;
}

bool IsRunGameTicCaller(const void* returnAddress) {
    const auto* module = reinterpret_cast<const std::uint8_t*>(
        GetModuleHandleW(nullptr));
    if (module == nullptr) return false;
    const auto* caller = static_cast<const std::uint8_t*>(returnAddress);
    return caller >= module + kRunGameTicRvaBegin &&
           caller < module + kRunGameTicRvaEnd &&
           GetCurrentThreadId() ==
               g_presentationThreadId.load(std::memory_order_acquire);
}

void RecordSelectedUsercmd(const void* command) {
    if (!IsReadableRange(command, 32)) return;
    const auto sequence = ReadUnaligned<std::int32_t>(command, 28);
    bool found = false;
    bool shouldCountMiss = false;
    std::uint64_t serial = 0;
    AcquireSRWLockShared(&g_mouseLedgerLock);
    const auto& record = g_usercmdMouseLedger[
        static_cast<std::size_t>(static_cast<std::uint32_t>(sequence)) %
        kTrackedUsercmdCount];
    if (record.valid && record.sequence == sequence) {
        serial = record.serial;
        found = true;
    }
    shouldCountMiss = g_haveTaggedAsyncCommand &&
                      sequence >= g_firstTaggedAsyncSequence;
    ReleaseSRWLockShared(&g_mouseLedgerLock);
    if (found) {
        g_selectedMouseSerial.store(serial, std::memory_order_release);
    } else if (shouldCountMiss) {
        ++g_mouseCounters.commandMisses;
    }
}

void* __fastcall HookedUsercmdTicCmd(void* self, void*, void* output,
                                     int ticNumber) {
    void* result = g_originalUsercmdTicCmd(self, output, ticNumber);
    if (IsRunGameTicCaller(_ReturnAddress())) {
        RecordSelectedUsercmd(output);
        ++g_mouseCounters.ticSelections;
    }
    return result;
}

void __fastcall HookedUsercmdInterrupt(void* self, void*) {
    AcquireSRWLockShared(&g_mouseLedgerLock);
    g_capturedUsercmdMouseSerial = g_latestMouseSerial;
    ReleaseSRWLockShared(&g_mouseLedgerLock);
    g_capturingUsercmdMouseSerial = true;
    g_originalUsercmdInterrupt(self);
    g_capturingUsercmdMouseSerial = false;
    if (!IsReadableRange(self, 0x2c0)) return;
    const auto sequence = ReadUnaligned<std::int32_t>(self, 0x2bc);
    AcquireSRWLockExclusive(&g_mouseLedgerLock);
    if (!g_haveTaggedAsyncCommand) {
        g_haveTaggedAsyncCommand = true;
        g_firstTaggedAsyncSequence = sequence;
    }
    auto& record = g_usercmdMouseLedger[
        static_cast<std::size_t>(static_cast<std::uint32_t>(sequence)) %
        kTrackedUsercmdCount];
    record.sequence = sequence;
    record.serial = g_capturedUsercmdMouseSerial;
    record.valid = true;
    ReleaseSRWLockExclusive(&g_mouseLedgerLock);
    ++g_mouseCounters.asyncCommands;
}

void* __fastcall HookedGetDirectUsercmd(void* self, void*, void* output) {
    AcquireSRWLockShared(&g_mouseLedgerLock);
    g_capturedUsercmdMouseSerial = g_latestMouseSerial;
    ReleaseSRWLockShared(&g_mouseLedgerLock);
    g_capturingUsercmdMouseSerial = true;
    void* result = g_originalGetDirectUsercmd(self, output);
    g_capturingUsercmdMouseSerial = false;
    if (IsRunGameTicCaller(_ReturnAddress())) {
        g_selectedMouseSerial.store(g_capturedUsercmdMouseSerial,
                                    std::memory_order_release);
        ++g_mouseCounters.directSelections;
    }
    return result;
}

void TryInstallUsercmdHooks() {
    if (!g_mouseInterpolationRequested || g_usercmdHookState != 0) return;
    auto* module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    void* usercmd = g_usercmdGenerator.load(std::memory_order_acquire);
    if (module == nullptr || !IsReadableRange(usercmd, sizeof(void*))) return;
    auto** vtable = *reinterpret_cast<void***>(usercmd);
    if (!IsReadableRange(vtable, 16 * sizeof(void*)) ||
        vtable[7] != module + kUsercmdTicCmdRva ||
        vtable[8] != module + kUsercmdInterruptRva ||
        vtable[15] != module + kGetDirectUsercmdRva) {
        Log("error: user-command vtable validation failed; consumed mouse overlay disabled\r\n");
        g_usercmdHookState = 2;
        return;
    }

    g_usercmdTicCmdSlot = &vtable[7];
    g_usercmdInterruptSlot = &vtable[8];
    g_getDirectUsercmdSlot = &vtable[15];
    g_originalUsercmdTicCmd =
        reinterpret_cast<UsercmdTicCmdFn>(vtable[7]);
    g_originalUsercmdInterrupt =
        reinterpret_cast<UsercmdInterruptFn>(vtable[8]);
    g_originalGetDirectUsercmd =
        reinterpret_cast<GetDirectUsercmdFn>(vtable[15]);

    const bool ticInstalled = WriteVtableSlot(
        g_usercmdTicCmdSlot, reinterpret_cast<void*>(&HookedUsercmdTicCmd));
    const bool interruptInstalled = ticInstalled && WriteVtableSlot(
        g_usercmdInterruptSlot,
        reinterpret_cast<void*>(&HookedUsercmdInterrupt));
    const bool directInstalled = interruptInstalled && WriteVtableSlot(
        g_getDirectUsercmdSlot,
        reinterpret_cast<void*>(&HookedGetDirectUsercmd));
    if (!directInstalled) {
        if (g_usercmdInterruptSlot != nullptr &&
            *g_usercmdInterruptSlot ==
                reinterpret_cast<void*>(&HookedUsercmdInterrupt)) {
            WriteVtableSlot(g_usercmdInterruptSlot,
                            reinterpret_cast<void*>(g_originalUsercmdInterrupt));
        }
        if (g_usercmdTicCmdSlot != nullptr &&
            *g_usercmdTicCmdSlot == reinterpret_cast<void*>(&HookedUsercmdTicCmd)) {
            WriteVtableSlot(g_usercmdTicCmdSlot,
                            reinterpret_cast<void*>(g_originalUsercmdTicCmd));
        }
        g_usercmdHookState = 2;
        Log("error: user-command observer installation failed; consumed mouse overlay disabled\r\n");
        return;
    }
    g_usercmdHookState = 1;
    Log("mouse: user-command generation/selection observers installed\r\n");
}

void __fastcall HookedMouseMove(void* self, void*) {
    g_usercmdGenerator.store(self, std::memory_order_release);
    float previousPitch = 0.0f;
    float previousYaw = 0.0f;
    const bool readable = IsReadableRange(self, 12);
    if (readable) {
        previousPitch = ReadUnaligned<float>(self, 4);
        previousYaw = ReadUnaligned<float>(self, 8);
    }
    g_originalMouseMove(self);
    ++g_mouseCounters.moveCalls;
    if (readable) {
        const float currentPitch = ReadUnaligned<float>(self, 4);
        const float currentYaw = ReadUnaligned<float>(self, 8);
        const double pitch = static_cast<double>(currentPitch) - previousPitch;
        const double yaw = static_cast<double>(currentYaw) - previousYaw;
        if (std::isfinite(yaw) && std::isfinite(pitch) &&
            (yaw != 0.0 || pitch != 0.0)) {
            AcquireSRWLockExclusive(&g_mouseLedgerLock);
            const std::uint64_t serial = ++g_latestMouseSerial;
            if (serial - g_includedMouseSerial > kMaximumTrackedMouseDeltas) {
                g_includedMouseSerial = serial;
                g_mouseTransitionYaw = 0.0;
                g_mouseTransitionPitch = 0.0;
                ++g_mouseCounters.ledgerOverflows;
            }
            g_mouseDeltaLedger[
                static_cast<std::size_t>(serial % kMaximumTrackedMouseDeltas)] =
                {serial, yaw, pitch};
            ReleaseSRWLockExclusive(&g_mouseLedgerLock);
            ++g_mouseCounters.moveChanges;
        }
    }
    if (g_capturingUsercmdMouseSerial) {
        AcquireSRWLockShared(&g_mouseLedgerLock);
        g_capturedUsercmdMouseSerial = g_latestMouseSerial;
        ReleaseSRWLockShared(&g_mouseLedgerLock);
    }
}

bool EqualGuid(REFGUID left, const GUID& right) {
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

HRESULT WINAPI HookedDirectInputGetDeviceData(void* self, DWORD objectDataSize,
                                               void* objectData, DWORD* elementCount,
                                               DWORD flags) {
    AcquireSRWLockExclusive(&g_directInputLock);
    const HRESULT result = g_originalDirectInputGetDeviceData(
        self, objectDataSize, objectData, elementCount, flags);
    if (self == g_mouseDevice && (flags & kDirectInputPeek) == 0) {
        g_mouseObjectDataSize = objectDataSize;
        ++g_mouseCounters.authoritativeCalls;
        if (SUCCEEDED(result) && elementCount != nullptr) {
            g_mouseCounters.authoritativeEvents += *elementCount;
        }
    }
    ReleaseSRWLockExclusive(&g_directInputLock);
    return result;
}

HRESULT WINAPI HookedDirectInputCreateDevice(void* self, REFGUID deviceGuid,
                                              void** device, void* outer) {
    const HRESULT result = g_originalDirectInputCreateDevice(
        self, deviceGuid, device, outer);
    if (FAILED(result) || device == nullptr || *device == nullptr ||
        !EqualGuid(deviceGuid, kSystemMouseGuid)) return result;

    auto** vtable = *reinterpret_cast<void***>(*device);
    if (vtable == nullptr || vtable[10] == nullptr) {
        Log("error: DirectInput mouse vtable validation failed; mouse interpolation disabled\r\n");
        g_directInputHookState = 2;
        return result;
    }
    AcquireSRWLockExclusive(&g_directInputLock);
    g_mouseDevice = *device;
    g_mouseObjectDataSize = 0;
    if (g_directInputGetDeviceDataSlot == nullptr) {
        g_directInputGetDeviceDataSlot = &vtable[10];
        g_originalDirectInputGetDeviceData =
            reinterpret_cast<DirectInputGetDeviceDataFn>(vtable[10]);
        if (!WriteVtableSlot(g_directInputGetDeviceDataSlot,
                             reinterpret_cast<void*>(
                                 &HookedDirectInputGetDeviceData))) {
            g_directInputGetDeviceDataSlot = nullptr;
            g_originalDirectInputGetDeviceData = nullptr;
            g_mouseDevice = nullptr;
            g_directInputHookState = 2;
        } else {
            g_directInputHookState = 1;
        }
    } else if (*g_directInputGetDeviceDataSlot ==
               reinterpret_cast<void*>(&HookedDirectInputGetDeviceData)) {
        g_directInputHookState = 1;
    }
    ReleaseSRWLockExclusive(&g_directInputLock);
    Log(g_directInputHookState == 1
            ? "mouse: DirectInput mouse GetDeviceData observer installed\r\n"
            : "error: DirectInput GetDeviceData hook failed; mouse interpolation disabled\r\n");
    return result;
}

HRESULT WINAPI HookedDirectInputCreate(HINSTANCE instance, DWORD version,
                                        void** directInput, void* outer) {
    const HRESULT result =
        g_originalDirectInputCreate(instance, version, directInput, outer);
    if (FAILED(result) || directInput == nullptr || *directInput == nullptr) {
        return result;
    }
    auto** vtable = *reinterpret_cast<void***>(*directInput);
    if (vtable == nullptr || vtable[3] == nullptr) {
        Log("error: DirectInput interface vtable validation failed; mouse interpolation disabled\r\n");
        g_directInputHookState = 2;
        return result;
    }
    if (g_directInputCreateDeviceSlot == nullptr) {
        g_directInputCreateDeviceSlot = &vtable[3];
        g_originalDirectInputCreateDevice =
            reinterpret_cast<DirectInputCreateDeviceFn>(vtable[3]);
        if (!WriteVtableSlot(g_directInputCreateDeviceSlot,
                             reinterpret_cast<void*>(
                                 &HookedDirectInputCreateDevice))) {
            g_directInputCreateDeviceSlot = nullptr;
            g_originalDirectInputCreateDevice = nullptr;
            g_directInputHookState = 2;
            Log("error: DirectInput CreateDevice hook failed; mouse interpolation disabled\r\n");
        } else {
            Log("mouse: DirectInput CreateDevice observer installed\r\n");
        }
    }
    return result;
}

void TryInstallMouseMoveHook() {
    if (!g_mouseInterpolationRequested || g_mouseMoveHookState != 0) return;
    auto* module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) return;
    auto* target = module + kMouseMoveRva;
    if (std::memcmp(target, kMouseMovePrologue.data(),
                    kMouseMovePrologue.size()) != 0) {
        Log("error: idUsercmdGenLocal::MouseMove prologue mismatch; mouse interpolation disabled\r\n");
        g_mouseMoveHookState = 2;
        return;
    }

    constexpr std::size_t trampolineSize = kMouseMoveStolenBytes + 5;
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        Log("error: could not allocate MouseMove trampoline; mouse interpolation disabled\r\n");
        g_mouseMoveHookState = 2;
        return;
    }
    std::memcpy(g_mouseMoveOriginal.data(), target, kMouseMoveStolenBytes);
    std::memcpy(trampoline, target, kMouseMoveStolenBytes);
    EncodeRelativeJump(trampoline + kMouseMoveStolenBytes,
                       trampoline + kMouseMoveStolenBytes,
                       target + kMouseMoveStolenBytes);
    std::array<std::uint8_t, kMouseMoveStolenBytes> detour{};
    detour.fill(0x90);
    EncodeRelativeJump(detour.data(), target,
                       reinterpret_cast<const void*>(&HookedMouseMove));

    DWORD oldProtection = 0;
    if (!VirtualProtect(target, detour.size(), PAGE_EXECUTE_READWRITE,
                        &oldProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        Log("error: could not make MouseMove writable; mouse interpolation disabled\r\n");
        g_mouseMoveHookState = 2;
        return;
    }
    g_mouseMoveTarget = target;
    g_mouseMoveTrampoline = trampoline;
    g_originalMouseMove = reinterpret_cast<MouseMoveFn>(trampoline);
    std::memcpy(target, detour.data(), detour.size());
    FlushInstructionCache(GetCurrentProcess(), target, detour.size());
    DWORD ignored = 0;
    VirtualProtect(target, detour.size(), oldProtection, &ignored);
    if (std::memcmp(target, detour.data(), detour.size()) != 0) {
        DWORD restoreProtection = 0;
        if (VirtualProtect(target, g_mouseMoveOriginal.size(),
                           PAGE_EXECUTE_READWRITE, &restoreProtection)) {
            std::memcpy(target, g_mouseMoveOriginal.data(),
                        g_mouseMoveOriginal.size());
            FlushInstructionCache(GetCurrentProcess(), target,
                                  g_mouseMoveOriginal.size());
            VirtualProtect(target, g_mouseMoveOriginal.size(),
                           restoreProtection, &ignored);
        }
        g_mouseMoveTarget = nullptr;
        g_mouseMoveTrampoline = nullptr;
        g_originalMouseMove = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_mouseMoveHookState = 2;
        Log("error: MouseMove detour verification failed; mouse interpolation disabled\r\n");
        return;
    }
    g_mouseMoveHookState = 1;
    Log("mouse: idUsercmdGenLocal::MouseMove observer installed at RVA 0x00069000\r\n");
}

void TryInstallRenderEntityHooks() {
    if (!g_viewModelInterpolationRequested && !g_worldInterpolationRequested &&
        !g_viewModelAnimationInterpolationRequested &&
        !g_worldAnimationInterpolationRequested) return;
    if ((g_viewModelAnimationInterpolationRequested ||
         g_worldAnimationInterpolationRequested) &&
        g_animationRendererState == 0) {
        auto* engineModule = reinterpret_cast<std::uint8_t*>(
            GetModuleHandleW(nullptr));
        auto* clearDynamicModel = engineModule == nullptr
            ? nullptr
            : engineModule + kClearEntityDefDynamicModelRva;
        if (clearDynamicModel == nullptr ||
            std::memcmp(clearDynamicModel,
                        kClearEntityDefDynamicModelPrologue.data(),
                        kClearEntityDefDynamicModelPrologue.size()) != 0) {
            g_animationRendererState = 2;
            Log("error: R_ClearEntityDefDynamicModel signature mismatch; "
                "skeletal animation interpolation disabled\r\n");
        } else {
            g_clearEntityDefDynamicModel =
                reinterpret_cast<ClearEntityDefDynamicModelFn>(clearDynamicModel);
            g_animationRendererState = 1;
            Log("animation: validated R_ClearEntityDefDynamicModel at "
                "executable RVA 0x000df3e0\r\n");
        }
    }
    auto* gameModule = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"gamex86.dll"));
    if (gameModule == nullptr) return;
    void* renderWorld = *reinterpret_cast<void**>(
        gameModule + kGameRenderWorldPointerRva);
    if (g_renderEntityHookState == 1) {
        if (renderWorld != nullptr && renderWorld != g_renderWorld) {
            g_renderWorld = renderWorld;
            ClearTrackedRenderEntities();
            Log("entity: render world changed; tracked entities reset\r\n");
        }
        return;
    }
    if (g_renderEntityHookState != 0) return;
    if (renderWorld == nullptr) return;
    auto** vtable = *reinterpret_cast<void***>(renderWorld);
    if (vtable == nullptr || vtable[2] == nullptr || vtable[3] == nullptr ||
        vtable[4] == nullptr || vtable[5] == nullptr) {
        Log("error: idRenderWorld vtable validation failed; entity interpolation disabled\r\n");
        g_renderEntityHookState = 2;
        return;
    }

    g_renderWorld = renderWorld;
    g_renderWorldVtable = vtable;
    g_addEntityDefSlot = &vtable[2];
    g_updateEntityDefSlot = &vtable[3];
    g_freeEntityDefSlot = &vtable[4];
    g_originalAddEntityDef = reinterpret_cast<AddEntityDefFn>(vtable[2]);
    g_originalUpdateEntityDef = reinterpret_cast<UpdateEntityDefFn>(vtable[3]);
    g_originalFreeEntityDef = reinterpret_cast<FreeEntityDefFn>(vtable[4]);
    g_getRenderEntity = reinterpret_cast<GetRenderEntityFn>(vtable[5]);

    const bool addHooked = WriteVtableSlot(
        g_addEntityDefSlot, reinterpret_cast<void*>(&HookedAddEntityDef));
    const bool updateHooked = addHooked && WriteVtableSlot(
        g_updateEntityDefSlot, reinterpret_cast<void*>(&HookedUpdateEntityDef));
    const bool freeHooked = updateHooked && WriteVtableSlot(
        g_freeEntityDefSlot, reinterpret_cast<void*>(&HookedFreeEntityDef));
    if (!freeHooked) {
        if (g_addEntityDefSlot != nullptr &&
            *g_addEntityDefSlot == reinterpret_cast<void*>(&HookedAddEntityDef)) {
            WriteVtableSlot(g_addEntityDefSlot,
                            reinterpret_cast<void*>(g_originalAddEntityDef));
        }
        if (g_updateEntityDefSlot != nullptr &&
            *g_updateEntityDefSlot == reinterpret_cast<void*>(&HookedUpdateEntityDef)) {
            WriteVtableSlot(g_updateEntityDefSlot,
                            reinterpret_cast<void*>(g_originalUpdateEntityDef));
        }
        g_renderEntityHookState = 2;
        Log("error: idRenderWorld entity hooks could not be installed; "
            "entity interpolation disabled\r\n");
        return;
    }
    g_renderEntityHookState = 1;
    Log("entity: idRenderWorld entity hooks installed\r\n");
}

void TryInstallDetermineViewAnglesHook() {
    if (!g_mouseInterpolationRequested ||
        g_determineViewAnglesHookState != 0) {
        return;
    }
    auto* gameModule =
        reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"gamex86.dll"));
    if (gameModule == nullptr) return;

    auto* target = gameModule + kDetermineViewAnglesRva;
    if (std::memcmp(target, kDetermineViewAnglesPrologue.data(),
                    kDetermineViewAnglesPrologue.size()) != 0) {
        Log("error: hhPlayer::DetermineViewAngles prologue mismatch; "
            "gravity-relative mouse overlay disabled\r\n");
        g_determineViewAnglesHookState = 2;
        return;
    }

    constexpr std::size_t trampolineSize =
        kDetermineViewAnglesStolenBytes + 5;
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        Log("error: could not allocate DetermineViewAngles trampoline; "
            "gravity-relative mouse overlay disabled\r\n");
        g_determineViewAnglesHookState = 2;
        return;
    }
    std::memcpy(g_determineViewAnglesOriginal.data(), target,
                g_determineViewAnglesOriginal.size());
    std::memcpy(trampoline, target, kDetermineViewAnglesStolenBytes);
    EncodeRelativeJump(trampoline + kDetermineViewAnglesStolenBytes,
                       trampoline + kDetermineViewAnglesStolenBytes,
                       target + kDetermineViewAnglesStolenBytes);

    std::array<std::uint8_t, kDetermineViewAnglesStolenBytes> detour{};
    detour.fill(0x90);
    EncodeRelativeJump(detour.data(), target,
                       reinterpret_cast<const void*>(
                           &HookedDetermineViewAngles));
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, detour.size(), PAGE_EXECUTE_READWRITE,
                        &oldProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        Log("error: could not make DetermineViewAngles writable; "
            "gravity-relative mouse overlay disabled\r\n");
        g_determineViewAnglesHookState = 2;
        return;
    }

    g_determineViewAnglesTarget = target;
    g_determineViewAnglesTrampoline = trampoline;
    g_originalDetermineViewAngles =
        reinterpret_cast<DetermineViewAnglesFn>(trampoline);
    std::memcpy(target, detour.data(), detour.size());
    FlushInstructionCache(GetCurrentProcess(), target, detour.size());
    DWORD ignored = 0;
    VirtualProtect(target, detour.size(), oldProtection, &ignored);
    if (std::memcmp(target, detour.data(), detour.size()) != 0) {
        DWORD restoreProtection = 0;
        if (VirtualProtect(target, g_determineViewAnglesOriginal.size(),
                           PAGE_EXECUTE_READWRITE, &restoreProtection)) {
            std::memcpy(target, g_determineViewAnglesOriginal.data(),
                        g_determineViewAnglesOriginal.size());
            FlushInstructionCache(GetCurrentProcess(), target,
                                  g_determineViewAnglesOriginal.size());
            VirtualProtect(target, g_determineViewAnglesOriginal.size(),
                           restoreProtection, &ignored);
        }
        g_determineViewAnglesTarget = nullptr;
        g_determineViewAnglesTrampoline = nullptr;
        g_originalDetermineViewAngles = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_determineViewAnglesHookState = 2;
        Log("error: DetermineViewAngles detour verification failed; "
            "gravity-relative mouse overlay disabled\r\n");
        return;
    }
    g_determineViewAnglesHookState = 1;
    Log("mouse: hhPlayer::DetermineViewAngles observer installed at "
        "RVA 0x00195730\r\n");
}

void TryInstallViewHook() {
    if ((!g_viewLoggingRequested && !g_cameraInterpolationRequested &&
         !g_mouseInterpolationRequested) ||
        g_viewHookState != 0) return;
    auto* gameModule = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"gamex86.dll"));
    if (gameModule == nullptr) return;

    auto* target = gameModule + kSingleViewRva;
    if (std::memcmp(target, kSingleViewPrologue.data(),
                    kSingleViewPrologue.size()) != 0) {
        Log("error: hhPlayerView::SingleView prologue mismatch; view hook disabled\r\n");
        g_viewHookState = 2;
        return;
    }

    constexpr std::size_t trampolineSize = kSingleViewStolenBytes + 5;
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        Log("error: could not allocate SingleView trampoline; view hook disabled\r\n");
        g_viewHookState = 2;
        return;
    }
    std::memcpy(g_singleViewOriginal.data(), target, kSingleViewStolenBytes);
    std::memcpy(trampoline, target, kSingleViewStolenBytes);
    EncodeRelativeJump(trampoline + kSingleViewStolenBytes,
                       trampoline + kSingleViewStolenBytes,
                       target + kSingleViewStolenBytes);

    std::array<std::uint8_t, kSingleViewStolenBytes> detour{};
    EncodeRelativeJump(detour.data(), target,
                       reinterpret_cast<const void*>(&HookedSingleView));
    detour[5] = 0x90;
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, detour.size(), PAGE_EXECUTE_READWRITE,
                        &oldProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        Log("error: could not make SingleView writable; view hook disabled\r\n");
        g_viewHookState = 2;
        return;
    }

    g_singleViewTarget = target;
    g_singleViewTrampoline = trampoline;
    g_originalSingleView = reinterpret_cast<SingleViewFn>(trampoline);
    std::memcpy(target, detour.data(), detour.size());
    FlushInstructionCache(GetCurrentProcess(), target, detour.size());
    DWORD ignored = 0;
    VirtualProtect(target, detour.size(), oldProtection, &ignored);
    if (std::memcmp(target, detour.data(), detour.size()) != 0) {
        DWORD restoreProtection = 0;
        if (VirtualProtect(target, g_singleViewOriginal.size(), PAGE_EXECUTE_READWRITE,
                           &restoreProtection)) {
            std::memcpy(target, g_singleViewOriginal.data(), g_singleViewOriginal.size());
            FlushInstructionCache(GetCurrentProcess(), target,
                                  g_singleViewOriginal.size());
            VirtualProtect(target, g_singleViewOriginal.size(), restoreProtection,
                           &ignored);
        }
        g_singleViewTarget = nullptr;
        g_originalSingleView = nullptr;
        g_singleViewTrampoline = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        Log("error: SingleView detour verification failed; view hook disabled\r\n");
        g_viewHookState = 2;
        return;
    }
    g_viewHookState = 1;
    Log(g_cameraInterpolationRequested
            ? "view: hhPlayerView::SingleView camera hook installed at RVA 0x001a8de0\r\n"
            : "view: hhPlayerView::SingleView read-only hook installed at RVA 0x001a8de0\r\n");
}

bool EqualAsciiInsensitive(const char* left, const char* right) {
    while (*left != '\0' && *right != '\0') {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == *right;
}

bool HookMainImport(const char* moduleName, const char* functionName, void* replacement,
                    void** original, IMAGE_THUNK_DATA32** patchedThunk) {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) return false;

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        const auto* importedModule = reinterpret_cast<const char*>(base + descriptor->Name);
        if (!EqualAsciiInsensitive(importedModule, moduleName)) continue;

        auto* addressThunk = reinterpret_cast<IMAGE_THUNK_DATA32*>(
            base + descriptor->FirstThunk);
        auto* nameThunk = descriptor->OriginalFirstThunk != 0
            ? reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk)
            : addressThunk;
        for (; nameThunk->u1.AddressOfData != 0; ++nameThunk, ++addressThunk) {
            if (IMAGE_SNAP_BY_ORDINAL32(nameThunk->u1.Ordinal)) continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                base + nameThunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), functionName) != 0) {
                continue;
            }

            DWORD oldProtection = 0;
            if (!VirtualProtect(&addressThunk->u1.Function, sizeof(addressThunk->u1.Function),
                                PAGE_READWRITE, &oldProtection)) return false;
            *original = reinterpret_cast<void*>(addressThunk->u1.Function);
            *patchedThunk = addressThunk;
            addressThunk->u1.Function = reinterpret_cast<std::uintptr_t>(replacement);
            DWORD ignored = 0;
            VirtualProtect(&addressThunk->u1.Function, sizeof(addressThunk->u1.Function),
                           oldProtection, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &addressThunk->u1.Function,
                                  sizeof(addressThunk->u1.Function));
            return true;
        }
    }
    return false;
}

void WaitForDeadline() {
    if (g_periodCounts <= 0) return;
    LARGE_INTEGER current{};
    QueryPerformanceCounter(&current);
    if (g_nextDeadline == 0 || current.QuadPart > g_nextDeadline + g_periodCounts * 4) {
        g_nextDeadline = current.QuadPart;
    }
    g_nextDeadline += g_periodCounts;

    for (;;) {
        QueryPerformanceCounter(&current);
        const std::int64_t remaining = g_nextDeadline - current.QuadPart;
        if (remaining <= 0) break;
        const double remainingMilliseconds =
            1000.0 * static_cast<double>(remaining) /
            static_cast<double>(g_frequency.QuadPart);
        if (remainingMilliseconds > 1.5) {
            Sleep(static_cast<DWORD>(remainingMilliseconds - 0.75));
        } else if (remainingMilliseconds > 0.25) {
            SwitchToThread();
        } else {
            YieldProcessor();
        }
    }
}

void ReportFrame() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (g_reportStart == 0) {
        g_reportStart = now.QuadPart;
        g_previousSwap = now.QuadPart;
    } else {
        const double intervalMilliseconds =
            1000.0 * static_cast<double>(now.QuadPart - g_previousSwap) /
            static_cast<double>(g_frequency.QuadPart);
        if (g_intervalSampleCount < g_intervalMilliseconds.size()) {
            g_intervalMilliseconds[g_intervalSampleCount++] = intervalMilliseconds;
        }
        g_worstIntervalMilliseconds =
            std::max(g_worstIntervalMilliseconds, intervalMilliseconds);
        g_previousSwap = now.QuadPart;
    }
    ++g_reportFrames;
    const auto elapsed = now.QuadPart - g_reportStart;
    if (elapsed < g_frequency.QuadPart) return;

    const double seconds = static_cast<double>(elapsed) /
                           static_cast<double>(g_frequency.QuadPart);
    const double rate = static_cast<double>(g_reportFrames) / seconds;
    std::sort(g_intervalMilliseconds.begin(),
              g_intervalMilliseconds.begin() + g_intervalSampleCount);
    const auto percentile = [](double fraction) {
        if (g_intervalSampleCount == 0) return 0.0;
        const auto index = static_cast<std::size_t>(
            fraction * static_cast<double>(g_intervalSampleCount - 1));
        return g_intervalMilliseconds[index];
    };
    std::size_t trackedViewEntities = 0;
    std::size_t trackedViewModelAnimations = 0;
    std::size_t trackedWorldEntities = 0;
    std::size_t trackedWorldAnimations = 0;
    for (std::size_t index = 0; index < g_activeRenderEntityCount; ++index) {
        const auto* entity =
            FindTrackedRenderEntity(g_activeRenderEntityHandles[index]);
        if (entity == nullptr) continue;
        if (entity->allowViewId == 0) {
            ++trackedWorldEntities;
            if (entity->animation.hasCurrent) {
                ++trackedWorldAnimations;
            }
        } else {
            ++trackedViewEntities;
            if (entity->animation.hasCurrent) {
                ++trackedViewModelAnimations;
            }
        }
    }
    const auto authoritativeMouseCalls =
        g_mouseCounters.authoritativeCalls.exchange(0);
    const auto authoritativeMouseEvents =
        g_mouseCounters.authoritativeEvents.exchange(0);
    const auto mousePeekCalls = g_mouseCounters.peekCalls.exchange(0);
    const auto mousePeekEvents = g_mouseCounters.peekEvents.exchange(0);
    const auto mouseOverlayViews = g_mouseCounters.overlayViews.exchange(0);
    const auto mouseSkippedState = g_mouseCounters.skippedState.exchange(0);
    const auto mouseSkippedSmoothing =
        g_mouseCounters.skippedSmoothing.exchange(0);
    const auto mousePeekFailures = g_mouseCounters.peekFailures.exchange(0);
    const auto mouseMoveCalls = g_mouseCounters.moveCalls.exchange(0);
    const auto mouseMoveChanges = g_mouseCounters.moveChanges.exchange(0);
    const auto mouseTrackedOverlayViews =
        g_mouseCounters.trackedOverlayViews.exchange(0);
    const auto mouseRetiredChanges =
        g_mouseCounters.retiredChanges.exchange(0);
    const auto mouseAsyncCommands =
        g_mouseCounters.asyncCommands.exchange(0);
    const auto mouseTicSelections =
        g_mouseCounters.ticSelections.exchange(0);
    const auto mouseDirectSelections =
        g_mouseCounters.directSelections.exchange(0);
    const auto mouseCommandMisses =
        g_mouseCounters.commandMisses.exchange(0);
    const auto mouseLedgerOverflows =
        g_mouseCounters.ledgerOverflows.exchange(0);
    char buffer[3200]{};
    const int length = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                                   "presentation: qpc_start=%lld qpc_end=%lld swaps=%llu "
                                   "seconds=%.6f rate=%.3f samples=%zu median_ms=%.4f "
                                   "p95_ms=%.4f p99_ms=%.4f worst_ms=%.4f "
                                   "view_hook=%d views=%llu null_views=%llu "
                                   "time_changes=%llu same_time=%llu discontinuities=%llu "
                                   "view_id_changes=%llu max_origin_step=%.3f "
                                   "camera_enabled=%d interpolated=%llu snapped=%llu "
                                   "camera_resets=%llu viewmodel_enabled=%d "
                                   "viewmodel_adjusted=%llu viewmodel_tracked=%zu "
                                   "mouse_enabled=%d mouse_hook=%d mouse_getdata_calls=%llu "
                                   "mouse_getdata_events=%llu mouse_peek_calls=%llu "
                                   "mouse_peek_events=%llu mouse_overlaid=%llu "
                                   "mouse_skipped_state=%llu mouse_skipped_smoothing=%llu "
                                   "mouse_peek_failures=%llu world_enabled=%d "
                                   "world_adjusted=%llu world_tracked=%zu "
                                   "world_resets=%llu world_capacity_skips=%llu "
                                   "camera_reset_time=%llu camera_reset_viewid=%llu "
                                   "camera_reset_fov=%llu camera_reset_stall=%llu "
                                   "camera_reset_origin=%llu camera_reset_axis=%llu "
                                   "view_delta_nonpositive=%llu view_delta_32=%llu "
                                   "view_delta_48=%llu view_delta_64=%llu "
                                   "view_delta_other=%llu view_delta_min=%lld "
                                   "view_delta_max=%lld "
                                   "viewmodel_anim_enabled=%d "
                                   "viewmodel_anim_adjusted=%llu "
                                   "viewmodel_anim_joints=%llu "
                                   "viewmodel_anim_tracked=%zu "
                                   "viewmodel_anim_skips=%llu "
                                   "world_anim_enabled=%d "
                                   "world_anim_adjusted=%llu "
                                   "world_anim_joints=%llu "
                                   "world_anim_tracked=%zu "
                                   "world_anim_skips=%llu "
                                   "mouse_move_calls=%llu "
                                   "mouse_move_changes=%llu "
                                   "mouse_tracked_overlays=%llu "
                                   "mouse_retired_changes=%llu "
                                   "mouse_async_commands=%llu "
                                   "mouse_tic_selections=%llu "
                                   "mouse_direct_selections=%llu "
                                   "mouse_command_misses=%llu "
                                   "mouse_ledger_overflows=%llu "
                                   "viewmodel_latest_tic_roots=%llu "
                                   "viewmodel_latest_tic_anims=%llu "
                                   "world_latest_tic_roots=%llu "
                                   "world_latest_tic_anims=%llu "
                                   "camera_extrapolated=%llu "
                                   "viewmodel_pending_roots=%llu "
                                   "viewmodel_pending_anims=%llu "
                                   "world_pending_roots=%llu "
                                   "world_pending_anims=%llu\r\n",
                                   static_cast<long long>(g_reportStart),
                                   static_cast<long long>(now.QuadPart),
                                   static_cast<unsigned long long>(g_reportFrames), seconds,
                                   rate, g_intervalSampleCount, percentile(0.50),
                                   percentile(0.95), percentile(0.99),
                                   g_worstIntervalMilliseconds, g_viewHookState == 1 ? 1 : 0,
                                   static_cast<unsigned long long>(g_viewCounters.calls),
                                   static_cast<unsigned long long>(g_viewCounters.nullViews),
                                   static_cast<unsigned long long>(g_viewCounters.timeChanges),
                                   static_cast<unsigned long long>(g_viewCounters.sameTime),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.discontinuities),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewIdChanges),
                                   g_viewCounters.maximumOriginStep,
                                   g_cameraInterpolationRequested ? 1 : 0,
                                   static_cast<unsigned long long>(
                                       g_viewCounters.interpolatedViews),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.snappedViews),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.interpolationResets),
                                   g_viewModelInterpolationRequested ? 1 : 0,
                                   static_cast<unsigned long long>(
                                       g_viewCounters.adjustedViewModels),
                                   trackedViewEntities,
                                   g_mouseInterpolationRequested ? 1 : 0,
                                   g_directInputHookState == 1 &&
                                           g_mouseMoveHookState == 1 &&
                                           g_usercmdHookState == 1
                                       ? 1
                                       : 0,
                                   static_cast<unsigned long long>(
                                       authoritativeMouseCalls),
                                   static_cast<unsigned long long>(
                                       authoritativeMouseEvents),
                                   static_cast<unsigned long long>(mousePeekCalls),
                                   static_cast<unsigned long long>(mousePeekEvents),
                                   static_cast<unsigned long long>(mouseOverlayViews),
                                   static_cast<unsigned long long>(mouseSkippedState),
                                   static_cast<unsigned long long>(
                                       mouseSkippedSmoothing),
                                   static_cast<unsigned long long>(mousePeekFailures),
                                   g_worldInterpolationRequested ? 1 : 0,
                                   static_cast<unsigned long long>(
                                       g_viewCounters.adjustedWorldEntities),
                                   trackedWorldEntities,
                                   static_cast<unsigned long long>(
                                       g_viewCounters.worldEntityResets),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.worldEntityCapacitySkips),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.resetTimeDelta),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.resetViewId),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.resetFov),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.resetStall),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.resetOrigin),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.resetAxis),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewDeltaNonpositive),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewDelta32),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewDelta48),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewDelta64),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewDeltaOther),
                                   static_cast<long long>(
                                       g_viewCounters.minimumViewDelta),
                                   static_cast<long long>(
                                       g_viewCounters.maximumViewDelta),
                                   g_viewModelAnimationInterpolationRequested
                                       ? 1
                                       : 0,
                                   static_cast<unsigned long long>(
                                       g_viewCounters.adjustedViewModelAnimations),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.interpolatedViewModelJoints),
                                   trackedViewModelAnimations,
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewModelAnimationSkips),
                                   g_worldAnimationInterpolationRequested
                                       ? 1
                                       : 0,
                                   static_cast<unsigned long long>(
                                       g_viewCounters.adjustedWorldAnimations),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.interpolatedWorldJoints),
                                   trackedWorldAnimations,
                                   static_cast<unsigned long long>(
                                       g_viewCounters.worldAnimationSkips),
                                   static_cast<unsigned long long>(mouseMoveCalls),
                                   static_cast<unsigned long long>(mouseMoveChanges),
                                   static_cast<unsigned long long>(
                                       mouseTrackedOverlayViews),
                                   static_cast<unsigned long long>(
                                       mouseRetiredChanges),
                                   static_cast<unsigned long long>(
                                       mouseAsyncCommands),
                                   static_cast<unsigned long long>(
                                       mouseTicSelections),
                                   static_cast<unsigned long long>(
                                       mouseDirectSelections),
                                   static_cast<unsigned long long>(
                                       mouseCommandMisses),
                                   static_cast<unsigned long long>(
                                       mouseLedgerOverflows),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewModelLatestTicRoots),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewModelLatestTicAnimations),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.worldLatestTicRoots),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.worldLatestTicAnimations),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.extrapolatedViews),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewModelPendingRoots),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.viewModelPendingAnimations),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.worldPendingRoots),
                                   static_cast<unsigned long long>(
                                       g_viewCounters.worldPendingAnimations));
    if (length > 0) Log(std::string(buffer, static_cast<std::size_t>(length)));
    g_reportStart = now.QuadPart;
    g_reportFrames = 0;
    g_intervalSampleCount = 0;
    g_worstIntervalMilliseconds = 0.0;
    g_viewCounters = {};
}

BOOL WINAPI HookedSwapBuffers(HDC deviceContext) {
    DWORD expectedThread = 0;
    g_presentationThreadId.compare_exchange_strong(
        expectedThread, GetCurrentThreadId(), std::memory_order_release,
        std::memory_order_relaxed);
    TryInstallDetermineViewAnglesHook();
    TryInstallViewHook();
    TryInstallRenderEntityHooks();
    TryInstallMouseMoveHook();
    TryInstallUsercmdHooks();
    TryApplyBorderlessWindow(deviceContext);
    WaitForDeadline();
    const BOOL result = g_originalSwapBuffers(deviceContext);
    ReportFrame();
    return result;
}

bool Initialize() {
    const std::wstring logPath = ReadEnvironment(L"PREYHFR_LOG");
    if (!logPath.empty()) {
        g_log = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    QueryPerformanceFrequency(&g_frequency);
    g_viewLoggingRequested = ReadEnvironmentFlag(L"PREYHFR_VIEW_LOG");
    g_cameraInterpolationRequested =
        ReadEnvironmentFlag(L"PREYHFR_CAMERA_INTERP");
    g_viewModelInterpolationRequested =
        ReadEnvironmentFlag(L"PREYHFR_VIEWMODEL_INTERP");
    g_viewModelAnimationInterpolationRequested =
        ReadEnvironmentFlag(L"PREYHFR_VIEWMODEL_ANIM_INTERP") &&
        g_cameraInterpolationRequested;
    g_worldInterpolationRequested =
        ReadEnvironmentFlag(L"PREYHFR_WORLD_INTERP") &&
        g_cameraInterpolationRequested;
    g_worldAnimationInterpolationRequested =
        ReadEnvironmentFlag(L"PREYHFR_WORLD_ANIM_INTERP") &&
        g_cameraInterpolationRequested;
    g_maximumWorldEntityStep = ReadEnvironmentDouble(
        L"PREYHFR_WORLD_MAX_DISTANCE", kDefaultMaximumWorldEntityStep,
        1.0, 4096.0);
    g_maximumWorldEntityAngleDegrees = ReadEnvironmentDouble(
        L"PREYHFR_WORLD_MAX_ANGLE",
        kDefaultMaximumWorldEntityAngleDegrees, 1.0, 180.0);
    g_mouseInterpolationRequested =
        ReadEnvironmentFlag(L"PREYHFR_MOUSE_INTERP") &&
        g_cameraInterpolationRequested;
    g_continuousSnapshotTimingRequested =
        ReadEnvironmentFlag(L"PREYHFR_CONTINUOUS_SNAPSHOT_TIMING");
    g_multiTicEntityAlignmentRequested =
        ReadEnvironmentFlag(L"PREYHFR_MULTI_TIC_ENTITY_ALIGNMENT") &&
        g_continuousSnapshotTimingRequested;
    g_overdueSnapshotFallbackRequested =
        ReadEnvironmentFlag(L"PREYHFR_OVERDUE_SNAPSHOT_FALLBACK") &&
        g_multiTicEntityAlignmentRequested;
    g_borderlessRequested = ReadEnvironmentFlag(L"PREYHFR_BORDERLESS");
    g_requestedRenderWidth =
        ReadEnvironmentUnsigned(L"PREYHFR_RENDER_WIDTH", 16384);
    g_requestedRenderHeight =
        ReadEnvironmentUnsigned(L"PREYHFR_RENDER_HEIGHT", 16384);
    if (g_borderlessRequested) {
        // Establish physical-pixel coordinates before the retail engine creates
        // its OpenGL window. This keeps custom renderer dimensions aligned with
        // the monitor rectangle on scaled Windows desktops.
        SetProcessDPIAware();
    }
    const unsigned int cap = ReadFrameCap();
    if (cap > 0 && g_frequency.QuadPart > 0) {
        g_periodCounts = std::max<std::int64_t>(1, g_frequency.QuadPart / cap);
        g_timerResolutionRaised = timeBeginPeriod(1) == TIMERR_NOERROR;
    }

    void* original = nullptr;
    IMAGE_THUNK_DATA32* patchedThunk = nullptr;
    if (!HookMainImport("GDI32.dll", "SwapBuffers",
                        reinterpret_cast<void*>(&HookedSwapBuffers), &original,
                        &patchedThunk)) {
        Log("error: GDI32!SwapBuffers import was not found\r\n");
        return false;
    }
    g_originalSwapBuffers = reinterpret_cast<SwapBuffersFn>(original);
    g_swapBuffersThunk = patchedThunk;
    if (g_mouseInterpolationRequested) {
        original = nullptr;
        patchedThunk = nullptr;
        if (HookMainImport("DINPUT.dll", "DirectInputCreateA",
                           reinterpret_cast<void*>(&HookedDirectInputCreate),
                           &original, &patchedThunk)) {
            g_originalDirectInputCreate =
                reinterpret_cast<DirectInputCreateAFn>(original);
            g_directInputCreateThunk = patchedThunk;
            Log("mouse: DINPUT!DirectInputCreateA observer installed\r\n");
        } else {
            g_directInputHookState = 2;
            Log("error: DINPUT!DirectInputCreateA import was not found; mouse interpolation disabled\r\n");
        }
    }
    char buffer[768]{};
    const int length = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                                   "hook: GDI32!SwapBuffers installed; cap=%u Hz; "
                                   "view_log=%s; camera_interp=%s; "
                                   "viewmodel_interp=%s; viewmodel_anim_interp=%s; "
                                   "world_interp=%s; world_anim_interp=%s; "
                                   "world_max_distance=%.3f; world_max_angle=%.3f; "
                                   "mouse_interp=%s; continuous_snapshot_timing=%s; "
                                   "multi_tic_entity_alignment=%s; "
                                   "overdue_snapshot_fallback=%s; borderless=%s; "
                                   "render_size=%ux%u\r\n",
                                   cap, g_viewLoggingRequested ? "enabled" : "disabled",
                                   g_cameraInterpolationRequested ? "enabled" : "disabled",
                                   g_viewModelInterpolationRequested ? "enabled" : "disabled",
                                   g_viewModelAnimationInterpolationRequested
                                       ? "enabled"
                                       : "disabled",
                                   g_worldInterpolationRequested ? "enabled" : "disabled",
                                   g_worldAnimationInterpolationRequested
                                       ? "enabled"
                                       : "disabled",
                                   g_maximumWorldEntityStep,
                                   g_maximumWorldEntityAngleDegrees,
                                   g_mouseInterpolationRequested ? "enabled" : "disabled",
                                   g_continuousSnapshotTimingRequested
                                       ? "enabled"
                                       : "disabled",
                                   g_multiTicEntityAlignmentRequested
                                       ? "enabled"
                                       : "disabled",
                                   g_overdueSnapshotFallbackRequested
                                       ? "enabled"
                                       : "disabled",
                                   g_borderlessRequested ? "enabled" : "disabled",
                                   g_requestedRenderWidth,
                                   g_requestedRenderHeight);
    if (length > 0) Log(std::string(buffer, static_cast<std::size_t>(length)));
    return true;
}

void Shutdown() {
    const auto restoreSlot = [](void** slot, void* hook, void* original) {
        if (slot == nullptr) return false;
        if (*slot == original) return true;
        return *slot == hook && WriteVtableSlot(slot, original);
    };
    bool renderEntityHooksRestored = false;
    if (g_renderEntityHookState == 1) {
        const bool freeRestored = restoreSlot(
            g_freeEntityDefSlot, reinterpret_cast<void*>(&HookedFreeEntityDef),
            reinterpret_cast<void*>(g_originalFreeEntityDef));
        const bool updateRestored = restoreSlot(
            g_updateEntityDefSlot, reinterpret_cast<void*>(&HookedUpdateEntityDef),
            reinterpret_cast<void*>(g_originalUpdateEntityDef));
        const bool addRestored = restoreSlot(
            g_addEntityDefSlot, reinterpret_cast<void*>(&HookedAddEntityDef),
            reinterpret_cast<void*>(g_originalAddEntityDef));
        renderEntityHooksRestored = freeRestored && updateRestored && addRestored;
    }
    bool determineViewAnglesHookRestored = false;
    bool viewHookRestored = false;
    bool gameModuleUnloadedFirst = false;
    auto* loadedGameModule =
        reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"gamex86.dll"));
    if (g_determineViewAnglesHookState == 1 &&
        g_determineViewAnglesTarget != nullptr &&
        g_determineViewAnglesTrampoline != nullptr &&
        loadedGameModule != nullptr &&
        g_determineViewAnglesTarget ==
            loadedGameModule + kDetermineViewAnglesRva) {
        std::array<std::uint8_t, kDetermineViewAnglesStolenBytes> detour{};
        detour.fill(0x90);
        EncodeRelativeJump(detour.data(), g_determineViewAnglesTarget,
                           reinterpret_cast<const void*>(
                               &HookedDetermineViewAngles));
        if (std::memcmp(g_determineViewAnglesTarget, detour.data(),
                        detour.size()) == 0) {
            DWORD oldProtection = 0;
            if (VirtualProtect(g_determineViewAnglesTarget,
                               g_determineViewAnglesOriginal.size(),
                               PAGE_EXECUTE_READWRITE, &oldProtection)) {
                std::memcpy(g_determineViewAnglesTarget,
                            g_determineViewAnglesOriginal.data(),
                            g_determineViewAnglesOriginal.size());
                FlushInstructionCache(
                    GetCurrentProcess(), g_determineViewAnglesTarget,
                    g_determineViewAnglesOriginal.size());
                DWORD ignored = 0;
                VirtualProtect(g_determineViewAnglesTarget,
                               g_determineViewAnglesOriginal.size(),
                               oldProtection, &ignored);
                determineViewAnglesHookRestored =
                    std::memcmp(g_determineViewAnglesTarget,
                                g_determineViewAnglesOriginal.data(),
                                g_determineViewAnglesOriginal.size()) == 0;
            }
        }
        if (determineViewAnglesHookRestored) {
            VirtualFree(g_determineViewAnglesTrampoline, 0, MEM_RELEASE);
            g_determineViewAnglesTrampoline = nullptr;
            g_originalDetermineViewAngles = nullptr;
            g_determineViewAnglesTarget = nullptr;
        }
    } else if (g_determineViewAnglesHookState == 1 &&
               loadedGameModule == nullptr) {
        gameModuleUnloadedFirst = true;
        if (g_determineViewAnglesTrampoline != nullptr) {
            VirtualFree(g_determineViewAnglesTrampoline, 0, MEM_RELEASE);
            g_determineViewAnglesTrampoline = nullptr;
        }
        g_originalDetermineViewAngles = nullptr;
        g_determineViewAnglesTarget = nullptr;
    }
    if (g_viewHookState == 1 && g_singleViewTarget != nullptr &&
        g_singleViewTrampoline != nullptr && loadedGameModule != nullptr &&
        g_singleViewTarget == loadedGameModule + kSingleViewRva) {
        std::array<std::uint8_t, kSingleViewStolenBytes> detour{};
        EncodeRelativeJump(detour.data(), g_singleViewTarget,
                           reinterpret_cast<const void*>(&HookedSingleView));
        detour[5] = 0x90;
        if (std::memcmp(g_singleViewTarget, detour.data(), detour.size()) == 0) {
            DWORD oldProtection = 0;
            if (VirtualProtect(g_singleViewTarget, g_singleViewOriginal.size(),
                               PAGE_EXECUTE_READWRITE, &oldProtection)) {
                std::memcpy(g_singleViewTarget, g_singleViewOriginal.data(),
                            g_singleViewOriginal.size());
                FlushInstructionCache(GetCurrentProcess(), g_singleViewTarget,
                                      g_singleViewOriginal.size());
                DWORD ignored = 0;
                VirtualProtect(g_singleViewTarget, g_singleViewOriginal.size(),
                               oldProtection, &ignored);
                viewHookRestored =
                    std::memcmp(g_singleViewTarget, g_singleViewOriginal.data(),
                                g_singleViewOriginal.size()) == 0;
            }
        }
        if (viewHookRestored) {
            VirtualFree(g_singleViewTrampoline, 0, MEM_RELEASE);
            g_singleViewTrampoline = nullptr;
            g_originalSingleView = nullptr;
            g_singleViewTarget = nullptr;
        }
    } else if (g_viewHookState == 1 && loadedGameModule == nullptr) {
        gameModuleUnloadedFirst = true;
        if (g_singleViewTrampoline != nullptr) {
            VirtualFree(g_singleViewTrampoline, 0, MEM_RELEASE);
            g_singleViewTrampoline = nullptr;
        }
        g_originalSingleView = nullptr;
        g_singleViewTarget = nullptr;
    }
    bool usercmdHooksRestored = false;
    if (g_usercmdHookState == 1) {
        const bool directRestored = restoreSlot(
            g_getDirectUsercmdSlot,
            reinterpret_cast<void*>(&HookedGetDirectUsercmd),
            reinterpret_cast<void*>(g_originalGetDirectUsercmd));
        const bool interruptRestored = restoreSlot(
            g_usercmdInterruptSlot,
            reinterpret_cast<void*>(&HookedUsercmdInterrupt),
            reinterpret_cast<void*>(g_originalUsercmdInterrupt));
        const bool ticRestored = restoreSlot(
            g_usercmdTicCmdSlot,
            reinterpret_cast<void*>(&HookedUsercmdTicCmd),
            reinterpret_cast<void*>(g_originalUsercmdTicCmd));
        usercmdHooksRestored = directRestored && interruptRestored && ticRestored;
    }

    bool mouseMoveHookRestored = false;
    if (g_mouseMoveHookState == 1 && g_mouseMoveTarget != nullptr &&
        g_mouseMoveTrampoline != nullptr) {
        std::array<std::uint8_t, kMouseMoveStolenBytes> detour{};
        detour.fill(0x90);
        EncodeRelativeJump(detour.data(), g_mouseMoveTarget,
                           reinterpret_cast<const void*>(&HookedMouseMove));
        if (std::memcmp(g_mouseMoveTarget, detour.data(), detour.size()) == 0) {
            DWORD oldProtection = 0;
            if (VirtualProtect(g_mouseMoveTarget, g_mouseMoveOriginal.size(),
                               PAGE_EXECUTE_READWRITE, &oldProtection)) {
                std::memcpy(g_mouseMoveTarget, g_mouseMoveOriginal.data(),
                            g_mouseMoveOriginal.size());
                FlushInstructionCache(GetCurrentProcess(), g_mouseMoveTarget,
                                      g_mouseMoveOriginal.size());
                DWORD ignored = 0;
                VirtualProtect(g_mouseMoveTarget, g_mouseMoveOriginal.size(),
                               oldProtection, &ignored);
                mouseMoveHookRestored =
                    std::memcmp(g_mouseMoveTarget, g_mouseMoveOriginal.data(),
                                g_mouseMoveOriginal.size()) == 0;
            }
        }
        if (mouseMoveHookRestored) {
            VirtualFree(g_mouseMoveTrampoline, 0, MEM_RELEASE);
            g_mouseMoveTrampoline = nullptr;
            g_originalMouseMove = nullptr;
            g_mouseMoveTarget = nullptr;
        }
    }

    bool directInputDeviceHookRestored = false;
    bool directInputCreateDeviceHookRestored = false;
    bool directInputImportRestored = false;
    if (g_mouseInterpolationRequested) {
        AcquireSRWLockExclusive(&g_directInputLock);
        if (g_directInputGetDeviceDataSlot == nullptr) {
            directInputDeviceHookRestored = true;
        } else {
            directInputDeviceHookRestored = restoreSlot(
                g_directInputGetDeviceDataSlot,
                reinterpret_cast<void*>(&HookedDirectInputGetDeviceData),
                reinterpret_cast<void*>(g_originalDirectInputGetDeviceData));
        }
        g_mouseDevice = nullptr;
        ReleaseSRWLockExclusive(&g_directInputLock);
        if (g_directInputCreateDeviceSlot == nullptr) {
            directInputCreateDeviceHookRestored = true;
        } else {
            directInputCreateDeviceHookRestored = restoreSlot(
                g_directInputCreateDeviceSlot,
                reinterpret_cast<void*>(&HookedDirectInputCreateDevice),
                reinterpret_cast<void*>(g_originalDirectInputCreateDevice));
        }
        if (g_directInputCreateThunk != nullptr &&
            g_directInputCreateThunk->u1.Function ==
                reinterpret_cast<std::uintptr_t>(&HookedDirectInputCreate)) {
            DWORD oldProtection = 0;
            if (VirtualProtect(&g_directInputCreateThunk->u1.Function,
                               sizeof(g_directInputCreateThunk->u1.Function),
                               PAGE_READWRITE, &oldProtection)) {
                g_directInputCreateThunk->u1.Function =
                    reinterpret_cast<std::uintptr_t>(g_originalDirectInputCreate);
                DWORD ignored = 0;
                VirtualProtect(&g_directInputCreateThunk->u1.Function,
                               sizeof(g_directInputCreateThunk->u1.Function),
                               oldProtection, &ignored);
                directInputImportRestored =
                    g_directInputCreateThunk->u1.Function ==
                    reinterpret_cast<std::uintptr_t>(g_originalDirectInputCreate);
            }
        }
    }
    bool importRestored = false;
    if (g_swapBuffersThunk != nullptr &&
        g_swapBuffersThunk->u1.Function ==
            reinterpret_cast<std::uintptr_t>(&HookedSwapBuffers)) {
        DWORD oldProtection = 0;
        if (VirtualProtect(&g_swapBuffersThunk->u1.Function,
                           sizeof(g_swapBuffersThunk->u1.Function), PAGE_READWRITE,
                           &oldProtection)) {
            g_swapBuffersThunk->u1.Function =
                reinterpret_cast<std::uintptr_t>(g_originalSwapBuffers);
            DWORD ignored = 0;
            VirtualProtect(&g_swapBuffersThunk->u1.Function,
                           sizeof(g_swapBuffersThunk->u1.Function), oldProtection,
                           &ignored);
            importRestored =
                g_swapBuffersThunk->u1.Function ==
                reinterpret_cast<std::uintptr_t>(g_originalSwapBuffers);
        }
    }
    const bool borderlessRestored =
        !g_borderlessRequested || RestoreBorderlessWindow();
    if (g_timerResolutionRaised) timeEndPeriod(1);
    if (g_log != INVALID_HANDLE_VALUE) {
        if (g_viewModelInterpolationRequested || g_worldInterpolationRequested ||
            g_viewModelAnimationInterpolationRequested ||
            g_worldAnimationInterpolationRequested) {
            Log(renderEntityHooksRestored
                    ? "entity: idRenderWorld entity hooks restored\r\n"
                    : g_renderEntityHookState == 0
                          ? "entity: render world never became available\r\n"
                          : g_renderEntityHookState == 2
                                ? "entity: hooks remained disabled after error\r\n"
                                : "warning: idRenderWorld entity hooks were not restored\r\n");
        }
        if (g_viewLoggingRequested || g_cameraInterpolationRequested ||
            g_mouseInterpolationRequested) {
            Log(viewHookRestored ? "view: SingleView hook restored\r\n"
                                 : gameModuleUnloadedFirst
                                       ? "view: game module unloaded before hook DLL; "
                                         "detour no longer live\r\n"
                                 : g_viewHookState == 0
                                       ? "view: game module never became available\r\n"
                                       : g_viewHookState == 2
                                             ? "view: hook remained disabled after error\r\n"
                                             : "warning: SingleView hook was not restored\r\n");
        }
        if (g_mouseInterpolationRequested) {
            Log(determineViewAnglesHookRestored
                    ? "mouse: DetermineViewAngles observer restored\r\n"
                    : gameModuleUnloadedFirst
                          ? "mouse: game module unloaded before effective-pitch "
                            "observer; detour no longer live\r\n"
                          : g_determineViewAnglesHookState == 0
                                ? "mouse: effective-pitch observer was never installed\r\n"
                                : g_determineViewAnglesHookState == 2
                                      ? "mouse: effective-pitch observer remained "
                                        "disabled after error\r\n"
                                      : "warning: effective-pitch observer was not "
                                        "restored\r\n");
            Log(usercmdHooksRestored
                    ? "mouse: user-command observers restored\r\n"
                    : g_usercmdHookState == 0
                          ? "mouse: user-command observers were never installed\r\n"
                          : g_usercmdHookState == 2
                                ? "mouse: user-command observers remained disabled after error\r\n"
                                : "warning: user-command observers were not restored\r\n");
            Log(mouseMoveHookRestored
                    ? "mouse: MouseMove observer restored\r\n"
                    : g_mouseMoveHookState == 0
                          ? "mouse: MouseMove observer was never installed\r\n"
                          : g_mouseMoveHookState == 2
                                ? "mouse: MouseMove observer remained disabled after error\r\n"
                                : "warning: MouseMove observer was not restored\r\n");
            Log(directInputDeviceHookRestored &&
                        directInputCreateDeviceHookRestored &&
                        directInputImportRestored
                    ? "mouse: DirectInput observers restored\r\n"
                    : "warning: one or more DirectInput observers were not restored\r\n");
        }
        if (g_borderlessRequested) {
            Log(borderlessRestored
                    ? "window: borderless style restored\r\n"
                    : "warning: borderless style was not restored\r\n");
        }
        Log(importRestored ? "hook: GDI32!SwapBuffers import restored\r\n"
                           : "warning: GDI32!SwapBuffers import was not restored\r\n");
        Log("hook: process shutdown\r\n");
        FlushFileBuffers(g_log);
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}

} // namespace

// Used only by the local research probe so limiter/pacing behavior can be
// validated without requiring a functioning OpenGL drawable.
extern "C" __declspec(dllexport) void WINAPI PreyHFRTestPresentation() {
    WaitForDeadline();
    ReportFrame();
}

extern "C" __declspec(dllexport) BOOL WINAPI PreyHFRTestGravityMouseAxis() {
    return TestGravityRelativeMouseAxis() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI PreyHFRTestInterpolationCadence() {
    constexpr std::int64_t frequency = 1'000'000'000;
    const auto testFeatureGates = [=] {
        const auto close = [](double left, double right) {
            return std::abs(left - right) < 0.000001;
        };
        if (!IsInterpolatableCameraIntervalForMode(
                kNativeTicMilliseconds, false) ||
            IsInterpolatableCameraIntervalForMode(
                kMaximumInterpolatedCameraIntervalMilliseconds, false) ||
            !IsInterpolatableCameraIntervalForMode(
                kMaximumInterpolatedCameraIntervalMilliseconds, true)) {
            return false;
        }
        const std::int64_t twoTics = CameraIntervalQpc(
            2 * kNativeTicMilliseconds, frequency);
        if (!close(CameraInterpolationAlphaForMode(
                       kNativeTicMilliseconds, twoTics, 0, frequency, false),
                   1.0) ||
            !close(CameraInterpolationAlphaForMode(
                       kNativeTicMilliseconds, twoTics, 0, frequency, true),
                   2.0) ||
            !close(LatestNativeTicInterpolationAlphaForMode(
                       twoTics, 0, frequency, false),
                   1.0) ||
            !close(LatestNativeTicInterpolationAlphaForMode(
                       twoTics, 0, frequency, true),
                   2.0) ||
            !close(CameraInterpolationAlphaForMode(
                       kMaximumInterpolatedCameraIntervalMilliseconds,
                       0, 0, frequency, false),
                   0.5)) {
            return false;
        }
        constexpr double cameraAlpha = 0.75;
        constexpr double latestAlpha = 0.5;
        constexpr double pendingAlpha = 0.25;
        return close(EntityInterpolationAlpha(
                         false, 2, cameraAlpha, latestAlpha, pendingAlpha,
                         false, false),
                     cameraAlpha) &&
               close(EntityInterpolationAlpha(
                         false, 2, cameraAlpha, latestAlpha, pendingAlpha,
                         true, false),
                     latestAlpha) &&
               close(EntityInterpolationAlpha(
                         true, 1, cameraAlpha, latestAlpha, pendingAlpha,
                         true, false),
                     cameraAlpha) &&
               close(EntityInterpolationAlpha(
                         true, 1, cameraAlpha, latestAlpha, pendingAlpha,
                         true, true),
                     pendingAlpha);
    };
    const auto testRate = [](double rate, bool requireGap,
                             bool requireAdjacentChanges) {
        const double frameNanoseconds =
            static_cast<double>(frequency) / rate;
        bool haveCurrent = false;
        bool synchronized = false;
        bool havePresented = false;
        std::int32_t previousTime = 0;
        std::int32_t currentTime = 0;
        std::int32_t currentInterval = kNativeTicMilliseconds;
        std::int64_t currentQpc = 0;
        std::int32_t entityPreviousTime = 0;
        std::int32_t entityCurrentTime = 0;
        std::uint32_t entityTransitionSamples = 0;
        double previousPresented = 0.0;
        bool sawGap = false;
        bool sawAdjacentChanges = false;
        bool changedOnPreviousFrame = false;

        for (std::int64_t frame = 0; frame < 1200; ++frame) {
            const auto now = static_cast<std::int64_t>(
                std::llround(static_cast<double>(frame) * frameNanoseconds));
            const double nowMilliseconds =
                static_cast<double>(now) * 1000.0 /
                static_cast<double>(frequency);
            const auto viewTime = static_cast<std::int32_t>(
                std::floor(nowMilliseconds /
                           static_cast<double>(kNativeTicMilliseconds))) *
                kNativeTicMilliseconds;
            bool changed = false;
            if (!haveCurrent) {
                previousTime = viewTime;
                currentTime = viewTime;
                entityPreviousTime = viewTime;
                entityCurrentTime = viewTime;
                currentQpc = now;
                haveCurrent = true;
            } else if (viewTime != currentTime) {
                changed = true;
                sawAdjacentChanges = sawAdjacentChanges ||
                                     changedOnPreviousFrame;
                previousTime = currentTime;
                const std::int32_t delta = viewTime - currentTime;
                if (!IsInterpolatableCameraInterval(delta)) return false;
                sawGap = sawGap || delta ==
                    kMaximumInterpolatedCameraIntervalMilliseconds;
                currentTime = viewTime;
                currentInterval = delta;
                entityTransitionSamples = 0;
                while (entityCurrentTime < viewTime) {
                    entityPreviousTime = entityCurrentTime;
                    entityCurrentTime += kNativeTicMilliseconds;
                    ++entityTransitionSamples;
                }
                if (synchronized) {
                    currentQpc = (std::min)(
                        currentQpc + CameraIntervalQpc(delta, frequency), now);
                } else {
                    currentQpc = now;
                    synchronized = true;
                }
            }
            changedOnPreviousFrame = changed;
            if (!synchronized) continue;

            const double alpha = CameraInterpolationAlpha(
                currentInterval, now, currentQpc, frequency);
            const double latestAlpha = LatestNativeTicInterpolationAlpha(
                now, currentQpc, frequency);
            const double presented = static_cast<double>(previousTime) +
                static_cast<double>(currentTime - previousTime) * alpha;
            const double entityAlpha = entityTransitionSamples > 1
                ? latestAlpha
                : alpha;
            const double entityPresented =
                static_cast<double>(entityPreviousTime) +
                static_cast<double>(entityCurrentTime - entityPreviousTime) *
                    entityAlpha;
            if (std::abs(entityPresented - presented) > 0.002) return false;
            // Allow the phase estimator one 400 ms beat period to encounter
            // an exact 60/120-to-62.5 Hz boundary and finish locking on.
            if (havePresented && frame >= 64) {
                const double expected = 1000.0 / rate;
                if (std::abs((presented - previousPresented) - expected) >
                    0.002) {
                    return false;
                }
            }
            previousPresented = presented;
            havePresented = true;
        }
        return (!requireGap || sawGap) &&
               (!requireAdjacentChanges || sawAdjacentChanges);
    };

    const auto testOverdue360 = [] {
        constexpr double rate = 360.0;
        const double frameNanoseconds =
            static_cast<double>(frequency) / rate;
        bool haveCurrent = false;
        bool synchronized = false;
        bool havePresented = false;
        bool sawGap = false;
        bool sawPending = false;
        std::int32_t previousTime = 0;
        std::int32_t currentTime = 0;
        std::int32_t currentInterval = kNativeTicMilliseconds;
        std::int32_t entityPreviousTime = 0;
        std::int32_t entityCurrentTime = 0;
        std::int64_t currentQpc = 0;
        double previousPresented = 0.0;

        for (std::int64_t frame = 0; frame < 1800; ++frame) {
            const auto now = static_cast<std::int64_t>(
                std::llround(static_cast<double>(frame) * frameNanoseconds));
            const double nowMilliseconds =
                static_cast<double>(now) * 1000.0 /
                static_cast<double>(frequency);
            const auto simulationTime = static_cast<std::int32_t>(
                std::floor(nowMilliseconds /
                           static_cast<double>(kNativeTicMilliseconds))) *
                kNativeTicMilliseconds;
            while (entityCurrentTime < simulationTime) {
                entityPreviousTime = entityCurrentTime;
                entityCurrentTime += kNativeTicMilliseconds;
            }

            const std::int32_t tick =
                simulationTime / kNativeTicMilliseconds;
            const std::int32_t viewTime = tick > 0 && tick % 8 == 1
                ? simulationTime - kNativeTicMilliseconds
                : simulationTime;
            if (!haveCurrent) {
                previousTime = viewTime;
                currentTime = viewTime;
                currentQpc = now;
                haveCurrent = true;
            } else if (viewTime != currentTime) {
                previousTime = currentTime;
                const std::int32_t delta = viewTime - currentTime;
                if (!IsInterpolatableCameraInterval(delta)) return false;
                sawGap = sawGap || delta ==
                    kMaximumInterpolatedCameraIntervalMilliseconds;
                currentTime = viewTime;
                currentInterval = delta;
                if (synchronized) {
                    currentQpc = (std::min)(
                        currentQpc + CameraIntervalQpc(delta, frequency), now);
                } else {
                    currentQpc = now;
                    synchronized = true;
                }
            }
            if (!synchronized) continue;

            const double cameraAlpha = CameraInterpolationAlpha(
                currentInterval, now, currentQpc, frequency);
            const double latestAlpha = LatestNativeTicInterpolationAlpha(
                now, currentQpc, frequency);
            const double pendingAlpha = PendingNativeTicInterpolationAlpha(
                now, currentQpc, frequency);
            const double cameraPresented =
                static_cast<double>(previousTime) +
                static_cast<double>(currentTime - previousTime) * cameraAlpha;
            const bool pending = entityCurrentTime > currentTime;
            sawPending = sawPending || pending;
            const double entityAlpha = pending
                ? pendingAlpha
                : currentInterval > kNativeTicMilliseconds
                    ? latestAlpha
                    : cameraAlpha;
            const double entityPresented =
                static_cast<double>(entityPreviousTime) +
                static_cast<double>(entityCurrentTime - entityPreviousTime) *
                    entityAlpha;
            if (frame >= 400 &&
                std::abs(entityPresented - cameraPresented) > 0.002) {
                return false;
            }
            if (havePresented && frame >= 400) {
                const double expected = 1000.0 / rate;
                if (std::abs((cameraPresented - previousPresented) - expected) >
                    0.002) {
                    return false;
                }
            }
            previousPresented = cameraPresented;
            havePresented = true;
        }
        return sawGap && sawPending;
    };

    return testFeatureGates() && testRate(120.0, false, true) &&
            testRate(60.0, true, false) && testOverdue360()
        ? TRUE
        : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI PreyHFRTestBorderless(HDC deviceContext) {
    TryApplyBorderlessWindow(deviceContext, false, false);
    const HWND window = WindowFromDC(deviceContext);
    return window != nullptr && g_borderlessWindow.window == window &&
           g_borderlessWindow.applied ? TRUE : FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        return Initialize() ? TRUE : FALSE;
    }
    if (reason == DLL_PROCESS_DETACH) Shutdown();
    return TRUE;
}
