#include <windows.h>
#include <mmsystem.h>

#include "config.h"
#include "integrity.h"
#include "preyhfr_version.h"
#include "wait_gate_patch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <sstream>
#include <intrin.h>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

namespace {

using SwapBuffersFn = BOOL(WINAPI*)(HDC);
using WglGetCurrentContextFn = void*(WINAPI*)();
using WglGetProcAddressFn = PROC(WINAPI*)(LPCSTR);
using WglGetSwapIntervalExtFn = int(WINAPI*)();
using SingleViewFn = void(__thiscall*)(void*, void*, const void*);
using CalculateRenderViewFn = void(__thiscall*)(void*);
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
using SetCVarStringFn = void(__thiscall*)(void*, const char*, const char*, int);
using SetCVarBoolFn = void(__thiscall*)(void*, const char*, bool, int);
using SetCVarIntegerFn = void(__thiscall*)(void*, const char*, int, int);
using CreateWaitableTimerAFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL,
                                               LPCSTR);
using SetWaitableTimerFn = BOOL(WINAPI*)(HANDLE, const LARGE_INTEGER*, LONG,
                                         PTIMERAPCROUTINE, LPVOID, BOOL);
using WaitForSingleObjectFn = DWORD(WINAPI*)(HANDLE, DWORD);
using TimeBeginPeriodFn = MMRESULT(WINAPI*)(UINT);
using TimeGetTimeFn = DWORD(WINAPI*)();
using NtQueryTimerResolutionFn = LONG(NTAPI*)(PULONG, PULONG, PULONG);

constexpr std::uintptr_t kSingleViewRva = 0x001a8de0;
constexpr std::uintptr_t kCalculateRenderViewRva = 0x00087600;
constexpr std::uintptr_t kDetermineViewAnglesRva = 0x00195730;
constexpr std::uintptr_t kGameRenderWorldPointerRva = 0x0038ff60;
constexpr std::uintptr_t kMouseMoveRva = 0x00069000;
constexpr std::uintptr_t kUsercmdTicCmdRva = 0x00068ba0;
constexpr std::uintptr_t kUsercmdInterruptRva = 0x00069880;
constexpr std::uintptr_t kGetDirectUsercmdRva = 0x00069970;
constexpr std::uintptr_t kCvarSystemVtableRva = 0x003af26c;
constexpr std::uintptr_t kSetCVarStringRva = 0x0002db00;
constexpr std::uintptr_t kSetCVarBoolRva = 0x0002db10;
constexpr std::uintptr_t kSetCVarIntegerRva = 0x0002db90;
constexpr std::uintptr_t kRunGameTicRvaBegin = 0x0005c670;
constexpr std::uintptr_t kRunGameTicRvaEnd = 0x0005cf00;
constexpr std::uintptr_t kSensitivityCvarPointerRva = 0x0044298c;
constexpr std::uintptr_t kPitchCvarPointerRva = 0x004429c0;
constexpr std::uintptr_t kYawCvarPointerRva = 0x004429f4;
constexpr std::uintptr_t kSmoothCvarPointerRva = 0x00442a5c;
constexpr std::uintptr_t kClearEntityDefDynamicModelRva = 0x000df3e0;
constexpr std::size_t kSingleViewStolenBytes = 6;
constexpr std::size_t kCalculateRenderViewStolenBytes = 6;
constexpr std::size_t kDetermineViewAnglesStolenBytes = 6;
constexpr std::size_t kMouseMoveStolenBytes = 9;
constexpr std::size_t kRenderViewSize = 140;
constexpr std::size_t kRenderViewTimeOffset = 80;
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
constexpr std::int32_t kCameraPhaseRecoveryEnterMilliseconds = 8;
constexpr std::int32_t kCameraPhaseRecoveryExitMilliseconds = 2;
constexpr std::int32_t kCameraPhaseRecoveryDebounceMilliseconds = 96;
constexpr double kCameraPhaseRecoverySlewFraction = 0.25;
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
constexpr std::size_t kProducedCameraSnapshotQueueSize = 8;
constexpr std::size_t kProducedEntitySnapshotQueueSize = 4;
constexpr std::size_t kAsyncTimerTraceCapacity = 4096;
constexpr std::array<std::uint8_t, kSingleViewStolenBytes> kSingleViewPrologue{
    0x64, 0xa1, 0x00, 0x00, 0x00, 0x00 // mov eax, fs:[0]
};
constexpr std::array<std::uint8_t, kCalculateRenderViewStolenBytes>
    kCalculateRenderViewPrologue{
        0x83, 0xec, 0x10, // sub esp, 10h
        0x56,             // push esi
        0x8b, 0xf1        // mov esi, ecx
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
std::atomic<int> g_swapIntervalProbeState{0};
HMODULE g_pluginModule = nullptr;
bool g_probeMode = false;
std::atomic<bool> g_patchActive{false};
std::atomic<bool> g_initializationComplete{false};
preyhfr::WaitGatePatch g_waitGatePatch;
void** g_setCVarStringSlot = nullptr;
void** g_setCVarBoolSlot = nullptr;
void** g_setCVarIntegerSlot = nullptr;
SetCVarStringFn g_originalSetCVarString = nullptr;
SetCVarBoolFn g_originalSetCVarBool = nullptr;
SetCVarIntegerFn g_originalSetCVarInteger = nullptr;
// 0 = waiting, 1 = applying, 2 = applied, 3 = first presentation was too early.
std::atomic<int> g_displayOverrideStartupState{0};
// 0 = pending, 1 = every supported retail file validated, 2 = rejected.
std::atomic<int> g_retailValidationState{0};
HANDLE g_retailValidationEvent = nullptr;
std::atomic<std::uintptr_t> g_comTicNumberAddress{0};
bool g_displayCvarHooksInstalled = false;
std::optional<int> g_requestedMode;
std::optional<int> g_requestedFullscreen;
std::optional<int> g_requestedSwapInterval;
std::optional<int> g_requestedCustomWidth;
std::optional<int> g_requestedCustomHeight;
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
SRWLOCK g_logLock = SRWLOCK_INIT;
bool g_timerResolutionRaised = false;
CreateWaitableTimerAFn g_originalCreateWaitableTimerA = nullptr;
SetWaitableTimerFn g_originalSetWaitableTimer = nullptr;
WaitForSingleObjectFn g_originalWaitForSingleObject = nullptr;
TimeBeginPeriodFn g_originalTimeBeginPeriod = nullptr;
TimeGetTimeFn g_originalTimeGetTime = nullptr;
IMAGE_THUNK_DATA32* g_createWaitableTimerThunk = nullptr;
IMAGE_THUNK_DATA32* g_setWaitableTimerThunk = nullptr;
IMAGE_THUNK_DATA32* g_waitForSingleObjectThunk = nullptr;
IMAGE_THUNK_DATA32* g_timeBeginPeriodThunk = nullptr;
IMAGE_THUNK_DATA32* g_timeGetTimeThunk = nullptr;
NtQueryTimerResolutionFn g_ntQueryTimerResolution = nullptr;
std::atomic<HANDLE> g_asyncTimerHandle{nullptr};
std::atomic<bool> g_asyncTimerConfirmed{false};
std::atomic<bool> g_asyncTimeGetHookInstalled{false};
std::atomic<bool> g_asyncClockStabilized{false};
std::atomic<DWORD> g_asyncTimerThreadId{0};
std::atomic<DWORD> g_asyncSyntheticMilliseconds{0};
std::int64_t g_asyncClockPreviousWakeQpc = 0;

struct TimerResolutionSnapshot {
    ULONG maximum100ns = 0;
    ULONG minimum100ns = 0;
    ULONG current100ns = 0;
    bool valid = false;
};

struct AsyncTimerTraceRecord {
    std::uint64_t sequence = 0;
    std::int64_t waitBeginQpc = 0;
    std::int64_t wakeQpc = 0;
    std::int64_t previousWakeQpc = 0;
    DWORD wakeMilliseconds = 0;
    DWORD previousWakeMilliseconds = 0;
    DWORD waitTimeoutMilliseconds = 0;
    DWORD waitResult = WAIT_FAILED;
    DWORD threadId = 0;
    DWORD processorNumber = 0;
    int threadPriority = THREAD_PRIORITY_ERROR_RETURN;
    std::int32_t comTicAtWaitEntry = -1;
    std::int32_t comTicAtWake = -1;
    std::int32_t previousWakeComTic = -1;
    ULONG timerResolution100ns = 0;
    DWORD syntheticMilliseconds = 0;
    std::uint32_t stabilizedAdvanceTics = 0;
    bool timerResolutionValid = false;
    bool discoveredByWait = false;
    bool clockStabilized = false;
};

std::array<AsyncTimerTraceRecord, kAsyncTimerTraceCapacity>
    g_asyncTimerTraceRecords{};
std::atomic<std::uint64_t> g_asyncTimerTracePublished{0};
std::uint64_t g_asyncTimerTraceConsumed = 0;
std::uint64_t g_asyncTimerTraceDropped = 0;
std::int64_t g_asyncTimerPreviousWakeQpc = 0;
DWORD g_asyncTimerPreviousWakeMilliseconds = 0;
std::int32_t g_asyncTimerPreviousWakeComTic = -1;
bool g_viewLoggingRequested = false;
bool g_cameraInterpolationRequested = false;
bool g_viewModelInterpolationRequested = false;
bool g_viewModelAnimationInterpolationRequested = false;
bool g_worldInterpolationRequested = false;
bool g_worldAnimationInterpolationRequested = false;
bool g_effectInterpolationRequested = false;
bool g_mouseInterpolationRequested = false;
bool g_continuousSnapshotTimingRequested = false;
bool g_multiTicEntityAlignmentRequested = false;
bool g_overdueSnapshotFallbackRequested = false;
bool g_bufferedTwoTicInterpolationRequested = false;
bool g_interpolationTraceRequested = false;
unsigned int g_timelineResetVirtualKey = 0;
bool g_timelineResetKeyDown = false;
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
int g_calculateRenderViewHookState = 0;
std::uint8_t* g_calculateRenderViewTarget = nullptr;
void* g_calculateRenderViewTrampoline = nullptr;
CalculateRenderViewFn g_originalCalculateRenderView = nullptr;
std::array<std::uint8_t, kCalculateRenderViewStolenBytes>
    g_calculateRenderViewOriginal{};
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

static_assert(sizeof(void*) == 4, "PreyHFR must be built for x86");
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

struct MouseFrameProbe {
    double trackedYaw = 0.0;
    double trackedPitch = 0.0;
    double totalYaw = 0.0;
    double totalPitch = 0.0;
    double transitionYaw = 0.0;
    double transitionPitch = 0.0;
    std::uint64_t latestSerial = 0;
    std::uint64_t includedSerialBefore = 0;
    std::uint64_t includedSerialAfter = 0;
    std::uint64_t selectedSerial = 0;
    std::uint64_t bufferedPreviousSerial = 0;
    std::uint64_t bufferedCurrentSerial = 0;
    int peekDeltaX = 0;
    int peekDeltaY = 0;
    bool tracked = false;
    bool peeked = false;
    bool buffered = false;
};

MouseFrameProbe g_mouseFrameProbe{};

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

constexpr std::uint32_t kCameraProbeInitial = 1u << 0;
constexpr std::uint32_t kCameraProbeSameTimeStall = 1u << 1;
constexpr std::uint32_t kCameraProbeTimeDelta = 1u << 2;
constexpr std::uint32_t kCameraProbeViewId = 1u << 3;
constexpr std::uint32_t kCameraProbeFov = 1u << 4;
constexpr std::uint32_t kCameraProbeStall = 1u << 5;
constexpr std::uint32_t kCameraProbeOrigin = 1u << 6;
constexpr std::uint32_t kCameraProbeAxis = 1u << 7;
constexpr std::uint32_t kCameraProbeClockDebt = 1u << 8;

struct CameraFrameProbe {
    std::int64_t qpc = 0;
    double callGapMilliseconds = 0.0;
    std::int32_t viewTime = 0;
    std::int64_t viewDelta = 0;
    std::uint32_t resetMask = 0;
    std::uint32_t producerBridgeSamples = 0;
    std::int64_t producerEarlyClampQpc = 0;
    std::uint32_t producerBufferDepth = 0;
    std::uint32_t producerBufferAdvances = 0;
    std::uint32_t producerSettledAdvances = 0;
    bool fovInterpolated = false;
};

CameraFrameProbe g_cameraFrameProbe{};
std::string g_interpolationTraceBuffer;
std::atomic<std::uint64_t> g_selectedGameTicSerial{0};
std::atomic<std::int64_t> g_selectedGameTicQpc{0};
std::atomic<std::int32_t> g_selectedGameTicArgument{-1};
std::atomic<std::int32_t> g_selectedGameTicComTic{-1};

struct CameraProductionProbe {
    std::uint64_t calls = 0;
    std::uint64_t selectedGameTicSerial = 0;
    std::int64_t qpc = 0;
    std::int32_t comTic = -1;
    std::int32_t viewTime = 0;
    std::int32_t previousViewTime = 0;
    bool havePreviousViewTime = false;
    bool readable = false;
};

CameraProductionProbe g_cameraProductionProbe{};

struct ProducedCameraSnapshot {
    alignas(16) std::array<std::uint8_t, kRenderViewSize> view{};
    std::uint64_t sequence = 0;
    std::int64_t qpc = 0;
    std::uint64_t mouseSerial = 0;
    double pitch = 0.0;
    bool pitchValid = false;
};

std::array<ProducedCameraSnapshot, kProducedCameraSnapshotQueueSize>
    g_producedCameraSnapshots{};
std::uint64_t g_latestProducedCameraSequence = 0;
std::uint64_t g_consumedCameraProductionSequence = 0;
std::uint64_t g_cameraPreviousProductionSequence = 0;
std::uint64_t g_cameraCurrentProductionSequence = 0;
std::uint64_t g_cameraPreviousMouseSerial = 0;
std::uint64_t g_cameraCurrentMouseSerial = 0;
bool g_authoritativeCameraBufferActive = false;
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
bool g_cameraPhaseRecoveryActive = false;
std::int64_t g_cameraPhaseRecoveryCandidateQpc = 0;
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

struct ProducedEntityPoseSnapshot {
    ViewEntityPose pose{};
    std::uint64_t sequence = 0;
    bool valid = false;
    bool canInterpolateFromPrevious = false;
};

struct ProducedEntityAnimationSnapshot {
    std::vector<JointMatrix> joints;
    std::uint64_t sequence = 0;
    bool valid = false;
    bool canInterpolateFromPrevious = false;
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
    std::array<ProducedEntityPoseSnapshot,
               kProducedEntitySnapshotQueueSize> producedPoses{};
    std::array<ProducedEntityAnimationSnapshot,
               kProducedEntitySnapshotQueueSize> producedAnimations{};
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
    AcquireSRWLockExclusive(&g_logLock);
    DWORD written = 0;
    WriteFile(g_log, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    ReleaseSRWLockExclusive(&g_logLock);
}

void LogPresentationEvent(const std::string& message) {
    if (!g_interpolationTraceRequested) {
        Log(message);
        return;
    }
    g_interpolationTraceBuffer.append(message);
    if (g_interpolationTraceBuffer.size() >= 512 * 1024) {
        Log(g_interpolationTraceBuffer);
        g_interpolationTraceBuffer.clear();
    }
}

std::int32_t ReadComTicNumber() {
    const std::uintptr_t address =
        g_comTicNumberAddress.load(std::memory_order_acquire);
    return address != 0
        ? *reinterpret_cast<volatile const std::int32_t*>(address)
        : -1;
}

TimerResolutionSnapshot QueryTimerResolution() {
    TimerResolutionSnapshot snapshot;
    if (g_ntQueryTimerResolution == nullptr) return snapshot;
    snapshot.valid = g_ntQueryTimerResolution(
                         &snapshot.maximum100ns, &snapshot.minimum100ns,
                         &snapshot.current100ns) >= 0;
    return snapshot;
}

std::uint32_t StabilizedTicsForWakeInterval(std::int64_t qpcDelta) {
    if (qpcDelta <= 0 || g_frequency.QuadPart <= 0) return 1;
    const std::int64_t nativePeriodCounts = std::max<std::int64_t>(
        1, (g_frequency.QuadPart * kNativeTicMilliseconds) / 1000);
    const std::int64_t elapsedPeriods = qpcDelta / nativePeriodCounts;
    return static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(elapsedPeriods, 1, 10));
}

std::uint32_t UpdateStabilizedAsyncClock(std::int64_t wakeQpc,
                                         DWORD realMilliseconds,
                                         DWORD threadId) {
    std::uint32_t advanceTics = 1;
    bool rebaseToRealTime = false;
    const std::int64_t nativePeriodCounts = g_frequency.QuadPart > 0
        ? std::max<std::int64_t>(
              1, (g_frequency.QuadPart * kNativeTicMilliseconds) / 1000)
        : 0;
    if (g_asyncClockPreviousWakeQpc > 0) {
        const std::int64_t qpcDelta =
            wakeQpc - g_asyncClockPreviousWakeQpc;
        advanceTics = StabilizedTicsForWakeInterval(qpcDelta);
        if (nativePeriodCounts > 0 &&
            qpcDelta / nativePeriodCounts > 10) {
            // Match the retail ten-tic backlog limit and discard older debt.
            g_asyncClockPreviousWakeQpc = wakeQpc;
            rebaseToRealTime = true;
        } else if (nativePeriodCounts > 0) {
            // Preserve the timer's ideal 16 ms phase rather than accumulating
            // ordinary sub-millisecond wake jitter.
            g_asyncClockPreviousWakeQpc +=
                static_cast<std::int64_t>(advanceTics) * nativePeriodCounts;
        } else {
            g_asyncClockPreviousWakeQpc = wakeQpc;
        }
    } else {
        g_asyncClockPreviousWakeQpc = wakeQpc;
    }
    DWORD syntheticMilliseconds = realMilliseconds;
    if (g_asyncClockStabilized.load(std::memory_order_acquire) &&
        !rebaseToRealTime) {
        syntheticMilliseconds =
            g_asyncSyntheticMilliseconds.load(std::memory_order_relaxed) +
            advanceTics * static_cast<DWORD>(kNativeTicMilliseconds);
    }
    g_asyncTimerThreadId.store(threadId, std::memory_order_relaxed);
    g_asyncSyntheticMilliseconds.store(syntheticMilliseconds,
                                       std::memory_order_relaxed);
    g_asyncClockStabilized.store(true, std::memory_order_release);
    return advanceTics;
}

void LogTimerResolutionEvent(const char* source, MMRESULT result,
                             const TimerResolutionSnapshot& before,
                             const TimerResolutionSnapshot& after) {
    char line[384]{};
    const int length = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "timer_resolution: source=%s request_ms=1 result=%u "
        "before_valid=%d before_current_100ns=%lu "
        "after_valid=%d after_maximum_100ns=%lu after_minimum_100ns=%lu "
        "after_current_100ns=%lu\r\n",
        source, static_cast<unsigned int>(result), before.valid ? 1 : 0,
        before.current100ns, after.valid ? 1 : 0, after.maximum100ns,
        after.minimum100ns, after.current100ns);
    if (length > 0) {
        Log(std::string(line, static_cast<std::size_t>(length)));
    }
}

void PublishAsyncTimerTrace(const AsyncTimerTraceRecord& record) {
    g_asyncTimerTraceRecords[static_cast<std::size_t>(
        record.sequence % kAsyncTimerTraceCapacity)] = record;
    g_asyncTimerTracePublished.store(record.sequence,
                                     std::memory_order_release);
}

void FlushAsyncTimerTrace() {
    if (!g_interpolationTraceRequested) return;
    const std::uint64_t published =
        g_asyncTimerTracePublished.load(std::memory_order_acquire);
    if (published <= g_asyncTimerTraceConsumed) return;
    if (published - g_asyncTimerTraceConsumed > kAsyncTimerTraceCapacity) {
        const std::uint64_t dropped = published - g_asyncTimerTraceConsumed -
            kAsyncTimerTraceCapacity;
        g_asyncTimerTraceDropped += dropped;
        g_asyncTimerTraceConsumed = published - kAsyncTimerTraceCapacity;
    }
    while (g_asyncTimerTraceConsumed < published) {
        const std::uint64_t sequence = g_asyncTimerTraceConsumed + 1;
        const AsyncTimerTraceRecord record =
            g_asyncTimerTraceRecords[static_cast<std::size_t>(
                sequence % kAsyncTimerTraceCapacity)];
        if (record.sequence != sequence) break;
        const double waitMicroseconds = g_frequency.QuadPart > 0
            ? 1'000'000.0 * static_cast<double>(
                  record.wakeQpc - record.waitBeginQpc) /
                  static_cast<double>(g_frequency.QuadPart)
            : -1.0;
        const double wakeIntervalMilliseconds =
            record.previousWakeQpc > 0 && g_frequency.QuadPart > 0
            ? 1000.0 * static_cast<double>(
                  record.wakeQpc - record.previousWakeQpc) /
                  static_cast<double>(g_frequency.QuadPart)
            : -1.0;
        const double previousCallbackMicroseconds =
            record.previousWakeQpc > 0 && g_frequency.QuadPart > 0
            ? 1'000'000.0 * static_cast<double>(
                  record.waitBeginQpc - record.previousWakeQpc) /
                  static_cast<double>(g_frequency.QuadPart)
            : -1.0;
        const long millisecondsDelta = record.previousWakeQpc > 0
            ? static_cast<long>(record.wakeMilliseconds -
                                record.previousWakeMilliseconds)
            : -1;
        const int emittedTics = record.previousWakeComTic >= 0 &&
                record.comTicAtWaitEntry >= 0
            ? record.comTicAtWaitEntry - record.previousWakeComTic
            : -1;
        char line[768]{};
        const int length = _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "async_timer: sequence=%llu wait_begin_qpc=%lld wake_qpc=%lld "
            "wait_us=%.3f wake_interval_ms=%.4f previous_callback_us=%.3f "
            "timegettime=%lu timegettime_delta_ms=%ld timeout_ms=%lu "
            "wait_result=0x%08lx thread_id=%lu processor=%lu priority=%d "
            "previous_wake_com_tic=%d com_tic_at_wait_entry=%d "
            "com_tic_at_wake=%d previous_emitted_tics=%d "
            "timer_resolution_valid=%d timer_resolution_100ns=%lu "
            "clock_stabilized=%d stabilized_advance_tics=%u "
            "synthetic_timegettime=%lu discovered_by_wait=%d "
            "dropped_total=%llu\r\n",
            static_cast<unsigned long long>(record.sequence),
            static_cast<long long>(record.waitBeginQpc),
            static_cast<long long>(record.wakeQpc), waitMicroseconds,
            wakeIntervalMilliseconds, previousCallbackMicroseconds,
            record.wakeMilliseconds, millisecondsDelta,
            record.waitTimeoutMilliseconds, record.waitResult,
            record.threadId, record.processorNumber, record.threadPriority,
            record.previousWakeComTic, record.comTicAtWaitEntry,
            record.comTicAtWake, emittedTics,
            record.timerResolutionValid ? 1 : 0,
            record.timerResolution100ns,
            record.clockStabilized ? 1 : 0,
            record.stabilizedAdvanceTics,
            record.syntheticMilliseconds,
            record.discoveredByWait ? 1 : 0,
            static_cast<unsigned long long>(g_asyncTimerTraceDropped));
        if (length > 0) {
            LogPresentationEvent(
                std::string(line, static_cast<std::size_t>(length)));
        }
        g_asyncTimerTraceConsumed = sequence;
    }
}

struct SelectedGameTicSnapshot {
    std::uint64_t serial = 0;
    std::int64_t qpc = 0;
    std::int32_t argument = -1;
    std::int32_t comTic = -1;
};

SelectedGameTicSnapshot ReadSelectedGameTic() {
    SelectedGameTicSnapshot selected;
    selected.serial =
        g_selectedGameTicSerial.load(std::memory_order_acquire);
    selected.qpc = g_selectedGameTicQpc.load(std::memory_order_relaxed);
    selected.argument =
        g_selectedGameTicArgument.load(std::memory_order_relaxed);
    selected.comTic =
        g_selectedGameTicComTic.load(std::memory_order_relaxed);
    return selected;
}

void RecordSelectedGameTic(std::int32_t argument) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const std::uint64_t serial =
        g_selectedGameTicSerial.load(std::memory_order_relaxed) + 1;
    g_selectedGameTicQpc.store(now.QuadPart, std::memory_order_relaxed);
    g_selectedGameTicArgument.store(argument, std::memory_order_relaxed);
    g_selectedGameTicComTic.store(ReadComTicNumber(),
                                  std::memory_order_relaxed);
    g_selectedGameTicSerial.store(serial, std::memory_order_release);
}

const ProducedCameraSnapshot* FindProducedCameraSnapshot(
    std::uint64_t sequence) {
    if (sequence == 0) return nullptr;
    const auto& snapshot = g_producedCameraSnapshots[
        static_cast<std::size_t>(
            sequence % kProducedCameraSnapshotQueueSize)];
    return snapshot.sequence == sequence ? &snapshot : nullptr;
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

std::array<double, 2> ComposeBufferedMouseOverlay(
    double transitionYaw, double transitionPitch,
    double liveYaw, double livePitch, double interpolationAlpha) {
    const double remaining =
        std::clamp(1.0 - interpolationAlpha, 0.0, 1.0);
    return {liveYaw + transitionYaw * remaining,
            livePitch + transitionPitch * remaining};
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
    g_mouseFrameProbe.latestSerial = latest;
    g_mouseFrameProbe.includedSerialBefore = g_includedMouseSerial;
    bool valid = true;
    if (!g_haveMouseViewTime || viewTime != g_mouseViewTime) {
        std::uint64_t selected =
            g_selectedMouseSerial.load(std::memory_order_acquire);
        selected = std::min(selected, latest);
        selected = std::max(selected, g_includedMouseSerial);
        g_mouseFrameProbe.selectedSerial = selected;
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

    // The authoritative camera path deliberately presents the previous
    // producer interval.  The normal retirement cursor follows the newest
    // simulation view, so using it as the overlay cutoff would retire mouse
    // input one tic before the delayed camera displays it.  Reconstruct the
    // overlay relative to the actual buffered camera pair instead: finish the
    // not-yet-presented fraction of previous->current, then apply everything
    // newer than current at full strength.
    const bool buffered = cameraInterpolated &&
        g_authoritativeCameraBufferActive;
    if (valid && buffered) {
        const std::uint64_t current =
            std::min(g_cameraCurrentMouseSerial, latest);
        const std::uint64_t previous =
            std::min(g_cameraPreviousMouseSerial, current);
        double transitionYaw = 0.0;
        double transitionPitch = 0.0;
        double liveYaw = 0.0;
        double livePitch = 0.0;
        if (current > previous) {
            valid = SumMouseDeltasLocked(previous + 1, current,
                                         transitionYaw, transitionPitch);
        }
        if (valid && latest > current) {
            valid = SumMouseDeltasLocked(current + 1, latest,
                                         liveYaw, livePitch);
        }
        if (valid) {
            const auto composed = ComposeBufferedMouseOverlay(
                transitionYaw, transitionPitch, liveYaw, livePitch,
                interpolationAlpha);
            yaw = composed[0];
            pitch = composed[1];
        }
        g_mouseFrameProbe.buffered = true;
        g_mouseFrameProbe.bufferedPreviousSerial = previous;
        g_mouseFrameProbe.bufferedCurrentSerial = current;
        g_mouseFrameProbe.transitionYaw = transitionYaw;
        g_mouseFrameProbe.transitionPitch = transitionPitch;
    } else if (valid && latest > g_includedMouseSerial) {
        valid = SumMouseDeltasLocked(g_includedMouseSerial + 1, latest,
                                     yaw, pitch);
    }

    if (valid && cameraInterpolated && !buffered) {
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
    g_mouseFrameProbe.includedSerialAfter = g_includedMouseSerial;
    if (!buffered) {
        g_mouseFrameProbe.transitionYaw = g_mouseTransitionYaw;
        g_mouseFrameProbe.transitionPitch = g_mouseTransitionPitch;
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
    g_mouseFrameProbe = {};
    if (!g_mouseInterpolationRequested || baseView == nullptr) return false;

    double trackedYaw = 0.0;
    double trackedPitch = 0.0;
    const bool tracked = GetTrackedMouseOverlay(
        authoritativeView, interpolationAlpha, temporaryInitialized, trackedYaw,
        trackedPitch);
    g_mouseFrameProbe.tracked = tracked;
    g_mouseFrameProbe.trackedYaw = trackedYaw;
    g_mouseFrameProbe.trackedPitch = trackedPitch;
    double yaw = trackedYaw;
    double pitch = trackedPitch;

    int deltaX = 0;
    int deltaY = 0;
    const bool peeked = PeekPendingMouse(deltaX, deltaY);
    g_mouseFrameProbe.peeked = peeked;
    g_mouseFrameProbe.peekDeltaX = deltaX;
    g_mouseFrameProbe.peekDeltaY = deltaY;

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
    g_mouseFrameProbe.totalYaw = yaw;
    g_mouseFrameProbe.totalPitch = pitch;
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

void ClearProducedRenderEntityHistories() {
    for (std::size_t index = 0; index < g_activeRenderEntityCount; ++index) {
        auto* entity = FindTrackedRenderEntity(
            g_activeRenderEntityHandles[index]);
        if (entity == nullptr) continue;
        for (auto& snapshot : entity->producedPoses) snapshot = {};
        for (auto& snapshot : entity->producedAnimations) {
            snapshot.sequence = 0;
            snapshot.valid = false;
            snapshot.canInterpolateFromPrevious = false;
            snapshot.joints.clear();
        }
    }
}

void ResetPresentationTimelineManually() {
    std::uint64_t latestMouseSerial = 0;
    std::uint64_t includedMouseSerial = 0;
    const std::uint64_t selectedMouseSerial =
        g_selectedMouseSerial.load(std::memory_order_acquire);
    AcquireSRWLockShared(&g_mouseLedgerLock);
    latestMouseSerial = g_latestMouseSerial;
    includedMouseSerial = g_includedMouseSerial;
    ReleaseSRWLockShared(&g_mouseLedgerLock);
    const std::int32_t comTicNumber = ReadComTicNumber();
    const std::int32_t previousViewTime =
        g_havePreviousView ? g_previousViewTime : -1;
    g_haveCameraCurrent = false;
    g_canInterpolateCamera = false;
    g_cameraPreviousPitchValid = false;
    g_cameraCurrentPitchValid = false;
    g_cameraCurrentQpc = 0;
    g_lastCameraCallQpc = 0;
    g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
    g_cameraTimelineSynchronized = false;
    g_cameraPhaseRecoveryActive = false;
    g_cameraPhaseRecoveryCandidateQpc = 0;
    g_authoritativeCameraBufferActive = false;
    g_cameraPreviousProductionSequence = 0;
    g_cameraCurrentProductionSequence = 0;
    g_cameraPreviousMouseSerial = 0;
    g_cameraCurrentMouseSerial = 0;
    g_consumedCameraProductionSequence = 0;
    g_havePreviousView = false;
    g_haveMouseViewTime = false;
    g_mouseTransitionYaw = 0.0;
    g_mouseTransitionPitch = 0.0;
    ClearProducedRenderEntityHistories();
    ResetTrackedEntityInterpolation();

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    char buffer[320]{};
    const int length = _snprintf_s(
        buffer, sizeof(buffer), _TRUNCATE,
        "camera: presentation timeline manually reset; qpc=%lld key_vk=0x%02x "
        "com_tic=%d view_time=%d mouse_serial_latest=%llu "
        "mouse_serial_included=%llu mouse_serial_selected=%llu\r\n",
        static_cast<long long>(now.QuadPart), g_timelineResetVirtualKey,
        comTicNumber, previousViewTime,
        static_cast<unsigned long long>(latestMouseSerial),
        static_cast<unsigned long long>(includedMouseSerial),
        static_cast<unsigned long long>(selectedMouseSerial));
    if (length > 0) {
        LogPresentationEvent(
            std::string(buffer, static_cast<std::size_t>(length)));
    }
}

void PollTimelineResetKey() {
    if (!g_cameraInterpolationRequested || g_timelineResetVirtualKey == 0) {
        return;
    }
    const bool down =
        (GetAsyncKeyState(static_cast<int>(g_timelineResetVirtualKey)) &
         0x8000) != 0;
    DWORD foregroundProcess = 0;
    const HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow != nullptr) {
        GetWindowThreadProcessId(foregroundWindow, &foregroundProcess);
    }
    if (foregroundProcess != GetCurrentProcessId()) {
        g_timelineResetKeyDown = down;
        return;
    }
    if (down && !g_timelineResetKeyDown) {
        ResetPresentationTimelineManually();
    }
    g_timelineResetKeyDown = down;
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

void AppendViewModelProductionTrace(int handle, int allowViewId,
                                    bool created,
                                    const ViewEntityPose& pose,
                                    const void* renderEntity) {
    if (!g_interpolationTraceRequested || allowViewId == 0 ||
        g_frequency.QuadPart <= 0) {
        return;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const SelectedGameTicSnapshot selected = ReadSelectedGameTic();
    const double selectedAgeMilliseconds = selected.qpc > 0
        ? 1000.0 * static_cast<double>(now.QuadPart - selected.qpc) /
              static_cast<double>(g_frequency.QuadPart)
        : -1.0;
    const int jointCount = IsReadableRange(
        static_cast<const std::uint8_t*>(renderEntity) +
            kRenderEntityNumJointsOffset,
        sizeof(int))
        ? ReadUnaligned<int>(renderEntity, kRenderEntityNumJointsOffset)
        : -1;
    char line[640]{};
    const int length = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "viewmodel_source: qpc=%lld selected_tic_serial=%llu "
        "selected_tic_qpc=%lld selected_tic_age_ms=%.4f "
        "selected_tic_argument=%d selected_tic_com_tic=%d com_tic=%d "
        "handle=%d created=%d allow_view_id=%d joints=%d "
        "origin=%.4f,%.4f,%.4f\r\n",
        static_cast<long long>(now.QuadPart),
        static_cast<unsigned long long>(selected.serial),
        static_cast<long long>(selected.qpc), selectedAgeMilliseconds,
        selected.argument, selected.comTic, ReadComTicNumber(), handle,
        created ? 1 : 0, allowViewId, jointCount, pose.origin[0],
        pose.origin[1], pose.origin[2]);
    if (length > 0) {
        LogPresentationEvent(
            std::string(line, static_cast<std::size_t>(length)));
    }
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
    AppendViewModelProductionTrace(handle, allowViewId, created, pose,
                                   renderEntity);
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

void CaptureProducedRenderEntitySnapshot(TrackedRenderEntity& tracked,
                                         std::uint64_t sequence) {
    if (sequence == 0) return;
        auto& pose = tracked.producedPoses[static_cast<std::size_t>(
            sequence % kProducedEntitySnapshotQueueSize)];
        const auto& previousPose = tracked.producedPoses[
            static_cast<std::size_t>(
                (sequence - 1) % kProducedEntitySnapshotQueueSize)];
        pose = {};
        pose.sequence = sequence;
        pose.valid = tracked.hasCurrent;
        if (pose.valid) {
            pose.pose = tracked.current;
            const bool worldEntity = tracked.allowViewId == 0;
            const double maximumStep = worldEntity
                ? g_maximumWorldEntityStep
                : kMaximumViewModelStep;
            const double maximumAngle = worldEntity
                ? g_maximumWorldEntityAngleDegrees
                : kMaximumViewModelAngleDegrees;
            pose.canInterpolateFromPrevious = previousPose.valid &&
                previousPose.sequence + 1 == sequence &&
                PoseOriginDistance(previousPose.pose, pose.pose) <=
                    maximumStep &&
                PoseAxisAngleDegrees(previousPose.pose, pose.pose) <=
                    maximumAngle;
        }

        auto& animation = tracked.producedAnimations[
            static_cast<std::size_t>(
                sequence % kProducedEntitySnapshotQueueSize)];
        const auto& previousAnimation = tracked.producedAnimations[
            static_cast<std::size_t>(
                (sequence - 1) % kProducedEntitySnapshotQueueSize)];
        animation.sequence = sequence;
        animation.valid = false;
        animation.canInterpolateFromPrevious = false;
        animation.joints.clear();
        if (!tracked.animation.hasCurrent) return;
        try {
            animation.joints = tracked.animation.current;
            animation.valid = true;
            animation.canInterpolateFromPrevious =
                previousAnimation.valid &&
                previousAnimation.sequence + 1 == sequence &&
                previousAnimation.joints.size() == animation.joints.size() &&
                (tracked.allowViewId == 0 ||
                 !ViewModelJointPoseDiscontinuous(
                     previousAnimation.joints, animation.joints));
        } catch (...) {
            animation.joints.clear();
            CountAnimationSkip(tracked);
        }
}

const ProducedEntityPoseSnapshot* FindProducedEntityPose(
    const TrackedRenderEntity& tracked, std::uint64_t sequence) {
    if (sequence == 0) return nullptr;
    const ProducedEntityPoseSnapshot* match = nullptr;
    for (const auto& snapshot : tracked.producedPoses) {
        if (snapshot.valid && snapshot.sequence <= sequence &&
            (match == nullptr || snapshot.sequence > match->sequence)) {
            match = &snapshot;
        }
    }
    return match;
}

const ProducedEntityAnimationSnapshot* FindProducedEntityAnimation(
    const TrackedRenderEntity& tracked, std::uint64_t sequence) {
    if (sequence == 0) return nullptr;
    const ProducedEntityAnimationSnapshot* match = nullptr;
    for (const auto& snapshot : tracked.producedAnimations) {
        if (snapshot.valid && snapshot.sequence <= sequence &&
            (match == nullptr || snapshot.sequence > match->sequence)) {
            match = &snapshot;
        }
    }
    return match;
}

const ProducedEntityPoseSnapshot* FindLatestProducedEntityPose(
    const TrackedRenderEntity& tracked) {
    const ProducedEntityPoseSnapshot* latest = nullptr;
    for (const auto& snapshot : tracked.producedPoses) {
        if (snapshot.valid &&
            (latest == nullptr || snapshot.sequence > latest->sequence)) {
            latest = &snapshot;
        }
    }
    return latest;
}

const ProducedEntityAnimationSnapshot* FindLatestProducedEntityAnimation(
    const TrackedRenderEntity& tracked) {
    const ProducedEntityAnimationSnapshot* latest = nullptr;
    for (const auto& snapshot : tracked.producedAnimations) {
        if (snapshot.valid &&
            (latest == nullptr || snapshot.sequence > latest->sequence)) {
            latest = &snapshot;
        }
    }
    return latest;
}

bool JointMatricesEqual(const std::vector<JointMatrix>& left,
                        const std::vector<JointMatrix>& right) {
    return left.size() == right.size() &&
        (left.empty() ||
         std::memcmp(left.data(), right.data(),
                     left.size() * sizeof(left[0])) == 0);
}

bool BufferedEntityHistoryNeedsPresentation(bool samplesDiffer,
                                            bool canInterpolate,
                                            std::uint64_t latestSequence,
                                            std::uint64_t displayedSequence) {
    // A carried-forward sample is only a description of state, not evidence
    // that the live render entity must be rewritten.  Present it when there
    // is an actual interpolation span, or when a newer captured state proves
    // the live entity is ahead of the delayed camera playhead.
    return (samplesDiffer && canInterpolate) ||
           latestSequence > displayedSequence;
}

int __fastcall HookedAddEntityDef(void* self, void*, const void* renderEntity) {
    const int handle = g_originalAddEntityDef(self, renderEntity);
    if (self == g_renderWorld) {
        ObserveRenderEntity(handle, renderEntity, true);
        if (auto* tracked = FindTrackedRenderEntity(handle)) {
            CaptureProducedRenderEntitySnapshot(
                *tracked, g_latestProducedCameraSequence);
        }
    }
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
        if (auto* tracked = FindTrackedRenderEntity(handle)) {
            CaptureProducedRenderEntitySnapshot(
                *tracked, g_latestProducedCameraSequence);
        }
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
std::uint64_t g_lastAnimatedWorldTraceSequence = 0;

std::array<float, 16> PoseToModelMatrix(const ViewEntityPose& pose) {
    return {
        pose.axis[0], pose.axis[1], pose.axis[2], 0.0f,
        pose.axis[3], pose.axis[4], pose.axis[5], 0.0f,
        pose.axis[6], pose.axis[7], pose.axis[8], 0.0f,
        pose.origin[0], pose.origin[1], pose.origin[2], 1.0f,
    };
}

void InterpolateJointMatrices(const std::vector<JointMatrix>& previous,
                              const std::vector<JointMatrix>& current,
                              std::vector<JointMatrix>& interpolated,
                              double alpha) {
    interpolated.resize(current.size());
    for (std::size_t index = 0; index < current.size(); ++index) {
        const auto& from = previous[index].values;
        const auto& to = current[index].values;
        auto& result = interpolated[index].values;
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

void InterpolateJointMatrices(TrackedJointAnimation& animation,
                              double alpha) {
    InterpolateJointMatrices(animation.previous, animation.current,
                             animation.interpolated, alpha);
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
double AnimationInterpolationAlpha(bool pending,
                                   std::uint32_t transitionSamples,
                                   double cameraAlpha,
                                   double latestTicAlpha,
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
        const auto* bufferedPreviousPose =
            g_authoritativeCameraBufferActive
            ? FindProducedEntityPose(
                  *tracked, g_cameraPreviousProductionSequence)
            : nullptr;
        const auto* bufferedCurrentPose =
            g_authoritativeCameraBufferActive
            ? FindProducedEntityPose(
                  *tracked, g_cameraCurrentProductionSequence)
            : nullptr;
        const bool bufferedPoseAvailable = bufferedPreviousPose != nullptr &&
            bufferedCurrentPose != nullptr;
        const auto* latestBufferedPose = bufferedPoseAvailable
            ? FindLatestProducedEntityPose(*tracked)
            : nullptr;
        const bool bufferedPoseChanged = bufferedPoseAvailable &&
            std::memcmp(&bufferedPreviousPose->pose,
                        &bufferedCurrentPose->pose,
                        sizeof(bufferedPreviousPose->pose)) != 0;
        const double bufferedMaximumStep = viewModel
            ? kMaximumViewModelStep
            : g_maximumWorldEntityStep;
        const double bufferedMaximumAngle = viewModel
            ? kMaximumViewModelAngleDegrees
            : g_maximumWorldEntityAngleDegrees;
        const bool bufferedPoseInterpolatable = bufferedPoseChanged &&
            PoseOriginDistance(bufferedPreviousPose->pose,
                               bufferedCurrentPose->pose) <=
                bufferedMaximumStep &&
            PoseAxisAngleDegrees(bufferedPreviousPose->pose,
                                 bufferedCurrentPose->pose) <=
                bufferedMaximumAngle;
        const bool bufferedPoseNeedsPresentation =
            bufferedPoseAvailable &&
            BufferedEntityHistoryNeedsPresentation(
                bufferedPoseChanged, bufferedPoseInterpolatable,
                latestBufferedPose != nullptr
                    ? latestBufferedPose->sequence
                    : 0,
                g_cameraCurrentProductionSequence);
        const bool useBufferedPose = g_authoritativeCameraBufferActive &&
            bufferedPoseNeedsPresentation;
        const bool currentPose = !g_authoritativeCameraBufferActive &&
            tracked->canInterpolate &&
            tracked->transitionGeneration == g_cameraSnapshotGeneration;
        const bool pendingPose = !g_authoritativeCameraBufferActive &&
            g_overdueSnapshotFallbackRequested &&
            tracked->canInterpolate &&
            tracked->transitionGeneration == g_cameraSnapshotGeneration + 1;
        const bool interpolatePose = g_authoritativeCameraBufferActive
            ? bufferedPoseInterpolatable
            : currentPose || pendingPose;
        const bool applyRoot = viewModel
            ? g_viewModelInterpolationRequested &&
                  tracked->allowViewId == viewId &&
                  ((cameraInterpolated &&
                    (g_authoritativeCameraBufferActive
                         ? bufferedPoseNeedsPresentation
                         : interpolatePose)) ||
                   mouseOverlay.applied)
            : g_worldInterpolationRequested && cameraInterpolated &&
                  (g_authoritativeCameraBufferActive
                       ? bufferedPoseNeedsPresentation
                       : interpolatePose);
        const bool animationRequested = viewModel
            ? g_viewModelAnimationInterpolationRequested
            : g_worldAnimationInterpolationRequested;
        const bool animationMatchesView = !viewModel ||
            tracked->allowViewId == viewId;
        const auto* bufferedPreviousAnimation =
            g_authoritativeCameraBufferActive
            ? FindProducedEntityAnimation(
                  *tracked, g_cameraPreviousProductionSequence)
            : nullptr;
        const auto* bufferedCurrentAnimation =
            g_authoritativeCameraBufferActive
            ? FindProducedEntityAnimation(
                  *tracked, g_cameraCurrentProductionSequence)
            : nullptr;
        const bool bufferedAnimationAvailable =
            bufferedPreviousAnimation != nullptr &&
            bufferedCurrentAnimation != nullptr &&
            bufferedPreviousAnimation->joints.size() ==
                bufferedCurrentAnimation->joints.size();
        const auto* latestBufferedAnimation = bufferedAnimationAvailable
            ? FindLatestProducedEntityAnimation(*tracked)
            : nullptr;
        const bool bufferedAnimationChanged =
            bufferedAnimationAvailable &&
            !JointMatricesEqual(bufferedPreviousAnimation->joints,
                                bufferedCurrentAnimation->joints);
        const bool bufferedAnimationInterpolatable =
            bufferedAnimationChanged &&
            (tracked->allowViewId == 0 ||
             !ViewModelJointPoseDiscontinuous(
                 bufferedPreviousAnimation->joints,
                 bufferedCurrentAnimation->joints));
        const bool bufferedAnimationNeedsPresentation =
            bufferedAnimationAvailable &&
            BufferedEntityHistoryNeedsPresentation(
                bufferedAnimationChanged,
                bufferedAnimationInterpolatable,
                latestBufferedAnimation != nullptr
                    ? latestBufferedAnimation->sequence
                    : 0,
                g_cameraCurrentProductionSequence);
        const bool currentAnimation = !g_authoritativeCameraBufferActive &&
            tracked->animation.canInterpolate &&
            tracked->animation.transitionGeneration ==
                g_cameraSnapshotGeneration;
        const bool pendingAnimation = !g_authoritativeCameraBufferActive &&
            g_overdueSnapshotFallbackRequested &&
            tracked->animation.canInterpolate &&
            tracked->animation.transitionGeneration ==
                g_cameraSnapshotGeneration + 1;
        const bool applyAnimation = animationRequested &&
            animationMatchesView && cameraInterpolated &&
            tracked->animation.hasCurrent &&
            (g_authoritativeCameraBufferActive
                 ? bufferedAnimationNeedsPresentation
                 : currentAnimation || pendingAnimation);
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
                !g_authoritativeCameraBufferActive &&
                g_multiTicEntityAlignmentRequested &&
                tracked->transitionSamples > 1;
            const double rootAlpha = g_authoritativeCameraBufferActive
                ? (bufferedPoseInterpolatable ? cameraAlpha : 1.0)
                : EntityInterpolationAlpha(
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
            const ViewEntityPose& previousPose = useBufferedPose
                ? bufferedPreviousPose->pose
                : g_authoritativeCameraBufferActive
                      ? livePose
                      : tracked->previous;
            const ViewEntityPose& currentPoseValue = useBufferedPose
                ? bufferedCurrentPose->pose
                : g_authoritativeCameraBufferActive
                      ? livePose
                      : tracked->current;
            const bool interpolateBufferedOrCurrent = useBufferedPose
                ? bufferedPoseInterpolatable
                : !g_authoritativeCameraBufferActive && interpolatePose;
            for (std::size_t index = 0;
                 index < currentPoseValue.origin.size(); ++index) {
                interpolatedPose.origin[index] = interpolatePose
                    ? static_cast<float>(
                          previousPose.origin[index] +
                          (currentPoseValue.origin[index] -
                           previousPose.origin[index]) * rootAlpha)
                    : currentPoseValue.origin[index];
            }
            interpolatedPose.axis = interpolateBufferedOrCurrent
                ? QuaternionToMatrix(
                      Slerp(MatrixToQuaternion(previousPose.axis),
                            MatrixToQuaternion(currentPoseValue.axis),
                            rootAlpha))
                : currentPoseValue.axis;
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
                !g_authoritativeCameraBufferActive &&
                g_multiTicEntityAlignmentRequested &&
                tracked->animation.transitionSamples > 1;
            const double animationAlpha = g_authoritativeCameraBufferActive
                ? (bufferedAnimationInterpolatable ? cameraAlpha : 1.0)
                : AnimationInterpolationAlpha(
                      pendingAnimation,
                      tracked->animation.transitionSamples,
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
            if (bufferedAnimationAvailable) {
                InterpolateJointMatrices(
                    bufferedPreviousAnimation->joints,
                    bufferedCurrentAnimation->joints,
                    tracked->animation.interpolated, animationAlpha);
            } else {
                InterpolateJointMatrices(tracked->animation, animationAlpha);
            }
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

void AppendAnimatedWorldEntityTrace(
    const std::array<AppliedViewEntityPose, kMaximumAppliedEntityPoses>& applied,
    std::size_t appliedCount) {
    if (!g_interpolationTraceRequested ||
        !g_authoritativeCameraBufferActive ||
        g_cameraCurrentProductionSequence == 0 ||
        g_cameraCurrentProductionSequence ==
            g_lastAnimatedWorldTraceSequence) {
        return;
    }
    g_lastAnimatedWorldTraceSequence = g_cameraCurrentProductionSequence;

    constexpr std::size_t maximumEntries = 24;
    std::string entries;
    std::size_t total = 0;
    std::size_t emitted = 0;
    for (std::size_t activeIndex = 0;
         activeIndex < g_activeRenderEntityCount; ++activeIndex) {
        auto* tracked = FindTrackedRenderEntity(
            g_activeRenderEntityHandles[activeIndex]);
        if (tracked == nullptr || tracked->allowViewId != 0 ||
            !tracked->animation.hasCurrent) {
            continue;
        }
        ++total;

        const auto* previousAnimation = FindProducedEntityAnimation(
            *tracked, g_cameraPreviousProductionSequence);
        const auto* currentAnimation = FindProducedEntityAnimation(
            *tracked, g_cameraCurrentProductionSequence);
        const bool animationPair = previousAnimation != nullptr &&
            currentAnimation != nullptr &&
            previousAnimation->joints.size() ==
                currentAnimation->joints.size();
        const bool animationChanged = animationPair &&
            !JointMatricesEqual(previousAnimation->joints,
                                currentAnimation->joints);
        const bool animationInterpolatable = animationChanged;

        const auto* previousPose = FindProducedEntityPose(
            *tracked, g_cameraPreviousProductionSequence);
        const auto* currentPose = FindProducedEntityPose(
            *tracked, g_cameraCurrentProductionSequence);
        const bool rootPair = previousPose != nullptr &&
            currentPose != nullptr;
        const bool rootChanged = rootPair &&
            std::memcmp(&previousPose->pose, &currentPose->pose,
                        sizeof(previousPose->pose)) != 0;
        const bool rootInterpolatable = rootChanged &&
            PoseOriginDistance(previousPose->pose, currentPose->pose) <=
                g_maximumWorldEntityStep &&
            PoseAxisAngleDegrees(previousPose->pose, currentPose->pose) <=
                g_maximumWorldEntityAngleDegrees;

        bool animationApplied = false;
        bool rootApplied = false;
        for (std::size_t index = 0; index < appliedCount; ++index) {
            if (applied[index].handle != tracked->handle) continue;
            animationApplied = applied[index].jointsApplied;
            rootApplied = applied[index].poseApplied;
            break;
        }

        if (emitted >= maximumEntries) continue;
        char entry[160]{};
        const int length = _snprintf_s(
            entry, sizeof(entry), _TRUNCATE,
            "%sh=%d,e=%d,j=%zu,a=%d%d%d%d,r=%d%d%d%d",
            emitted == 0 ? "" : ";", tracked->handle,
            tracked->identity.entityNumber,
            tracked->animation.current.size(), animationPair ? 1 : 0,
            animationChanged ? 1 : 0,
            animationInterpolatable ? 1 : 0,
            animationApplied ? 1 : 0, rootPair ? 1 : 0,
            rootChanged ? 1 : 0, rootInterpolatable ? 1 : 0,
            rootApplied ? 1 : 0);
        if (length > 0) {
            entries.append(entry, static_cast<std::size_t>(length));
            ++emitted;
        }
    }

    char prefix[256]{};
    const int prefixLength = _snprintf_s(
        prefix, sizeof(prefix), _TRUNCATE,
        "entity_interp_trace: sequence=%llu previous_sequence=%llu "
        "world_animated=%zu emitted=%zu entries=",
        static_cast<unsigned long long>(g_cameraCurrentProductionSequence),
        static_cast<unsigned long long>(g_cameraPreviousProductionSequence),
        total, emitted);
    if (prefixLength <= 0) return;
    std::string line(prefix, static_cast<std::size_t>(prefixLength));
    line += entries;
    line += "\r\n";
    LogPresentationEvent(line);
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

bool IsConsecutiveProducedCameraBridge(std::int32_t consumedTime,
                                       std::int32_t producedPreviousTime,
                                       std::int32_t producedCurrentTime) {
    return static_cast<std::int64_t>(producedPreviousTime) - consumedTime ==
               kNativeTicMilliseconds &&
           static_cast<std::int64_t>(producedCurrentTime) -
                   producedPreviousTime ==
               kNativeTicMilliseconds;
}

std::int64_t CameraIntervalQpc(std::int32_t milliseconds,
                               std::int64_t frequency) {
    return static_cast<std::int64_t>(std::llround(
        static_cast<double>(milliseconds) *
        static_cast<double>(frequency) / 1000.0));
}

bool ProducedCameraSampleReadyForAdvance(
    std::int64_t sampleQpc, bool hasLookahead, std::int64_t nowQpc,
    std::int64_t nativeIntervalQpc) {
    return hasLookahead ||
           (sampleQpc > 0 && nowQpc >= sampleQpc &&
            nowQpc - sampleQpc >= nativeIntervalQpc);
}

std::int64_t AdvanceProducedCameraSnapshotClock(
    std::int64_t currentSnapshotQpc, std::int32_t intervalMilliseconds,
    std::int64_t producedSnapshotQpc, std::int64_t frequency,
    std::int64_t* earlyClampQpc = nullptr) {
    const std::int64_t predictedSnapshotQpc =
        currentSnapshotQpc +
        CameraIntervalQpc(intervalMilliseconds, frequency);
    const bool producedEarly = producedSnapshotQpc > 0 &&
                               predictedSnapshotQpc > producedSnapshotQpc;
    if (earlyClampQpc != nullptr) {
        *earlyClampQpc = producedEarly
            ? predictedSnapshotQpc - producedSnapshotQpc
            : 0;
    }
    return producedEarly ? producedSnapshotQpc : predictedSnapshotQpc;
}

struct CameraSnapshotClockUpdate {
    std::int64_t qpc = 0;
    std::int64_t phaseLagQpc = 0;
    std::int64_t correctionQpc = 0;
    bool recoveryStarted = false;
    bool recoveryFinished = false;
};

CameraSnapshotClockUpdate UpdateCameraSnapshotClock(
    std::int64_t currentSnapshotQpc, std::int32_t intervalMilliseconds,
    std::int64_t previousCallQpc, std::int64_t nowQpc,
    std::int64_t frequency, bool& recoveryActive,
    std::int64_t& recoveryCandidateQpc) {
    CameraSnapshotClockUpdate update;
    const std::int64_t predictedSnapshotQpc =
        currentSnapshotQpc +
        CameraIntervalQpc(intervalMilliseconds, frequency);
    update.qpc = predictedSnapshotQpc;
    if (frequency <= 0 || previousCallQpc <= 0 ||
        previousCallQpc > nowQpc) {
        return update;
    }

    // A changed render view was produced between the previous and current
    // presentation calls. Its midpoint is a bounded phase observation; unlike
    // the current call time, it does not include the whole sampling delay.
    const std::int64_t observedSnapshotQpc =
        previousCallQpc + (nowQpc - previousCallQpc) / 2;
    update.phaseLagQpc = observedSnapshotQpc - predictedSnapshotQpc;

    // A prediction later than the observation cannot represent a snapshot
    // already in hand. Moving this boundary earlier only advances the
    // presentation timeline, so it is safe and also removes initial sampling
    // error at commensurate rates such as 60 and 120 Hz.
    if (predictedSnapshotQpc > nowQpc) {
        update.qpc = nowQpc;
        update.correctionQpc = nowQpc - predictedSnapshotQpc;
        update.recoveryFinished = recoveryActive;
        recoveryActive = false;
        recoveryCandidateQpc = 0;
        return update;
    }

    const std::int64_t enterQpc = CameraIntervalQpc(
        kCameraPhaseRecoveryEnterMilliseconds, frequency);
    const std::int64_t exitQpc = CameraIntervalQpc(
        kCameraPhaseRecoveryExitMilliseconds, frequency);
    if (!recoveryActive) {
        if (update.phaseLagQpc <= enterQpc) {
            recoveryCandidateQpc = 0;
            return update;
        }
        if (recoveryCandidateQpc == 0) {
            recoveryCandidateQpc = nowQpc;
            return update;
        }
        const std::int64_t debounceQpc = CameraIntervalQpc(
            kCameraPhaseRecoveryDebounceMilliseconds, frequency);
        if (nowQpc - recoveryCandidateQpc < debounceQpc) return update;
        recoveryActive = true;
        recoveryCandidateQpc = 0;
        update.recoveryStarted = true;
    }
    if (update.phaseLagQpc <= exitQpc) {
        recoveryActive = false;
        recoveryCandidateQpc = 0;
        update.recoveryFinished = true;
        return update;
    }

    // Correct only a fraction of one presentation interval at a time. The
    // presented game-time advance across this snapshot remains positive, so
    // camera and viewmodel motion cannot perform the captured alpha-to-zero
    // rollback. Repeated snapshots still converge the clock back into phase.
    const std::int64_t callGapQpc = nowQpc - previousCallQpc;
    const std::int64_t maximumCorrectionQpc = (std::max)(
        std::int64_t{1}, static_cast<std::int64_t>(std::llround(
            static_cast<double>(callGapQpc) *
            kCameraPhaseRecoverySlewFraction)));
    update.correctionQpc = (std::min)(
        update.phaseLagQpc - exitQpc, maximumCorrectionQpc);
    update.qpc += update.correctionQpc;
    return update;
}

bool CameraSnapshotClockDebtExceeded(std::int64_t currentSnapshotQpc,
                                     std::int32_t nextIntervalMilliseconds,
                                     std::int32_t maximumDebtMilliseconds,
                                     std::int64_t nowQpc,
                                     std::int64_t frequency) {
    if (nextIntervalMilliseconds <= 0 || maximumDebtMilliseconds <= 0 ||
        frequency <= 0) return false;
    const std::int64_t predictedSnapshotQpc =
        currentSnapshotQpc +
        CameraIntervalQpc(nextIntervalMilliseconds, frequency);
    const std::int64_t maximumSupportedDebtQpc = CameraIntervalQpc(
        maximumDebtMilliseconds, frequency);
    return nowQpc - predictedSnapshotQpc > maximumSupportedDebtQpc;
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

double CameraRotationInterpolationAlpha(double interpolationAlpha) {
    // Positional extrapolation keeps continuous movement useful while an
    // async tic is late.  Reusing the previous angular velocity is unsafe,
    // however: live mouse deltas already describe rotation after the newest
    // snapshot, so angular extrapolation counts a fast turn twice and snaps
    // back when the next authoritative view arrives.
    return std::clamp(interpolationAlpha, 0.0, 1.0);
}

std::int32_t InterpolatedPresentationTime(std::int32_t previousTime,
                                          std::int32_t currentTime,
                                          double interpolationAlpha) {
    // Renderer particles and material expressions are pure functions of the
    // render-view clock. Keep that clock on the same authoritative interval
    // as the camera, but never predict effects beyond the newest game tic.
    const double alpha = std::clamp(interpolationAlpha, 0.0, 1.0);
    const double time = static_cast<double>(previousTime) +
        static_cast<double>(static_cast<std::int64_t>(currentTime) -
                            static_cast<std::int64_t>(previousTime)) * alpha;
    return static_cast<std::int32_t>(std::llround(time));
}

bool ApplyInterpolatedEffectTime(
    std::array<std::uint8_t, kRenderViewSize>& view,
    double interpolationAlpha) {
    if (!g_effectInterpolationRequested || !g_canInterpolateCamera) {
        return false;
    }
    const auto previousTime = ReadUnaligned<std::int32_t>(
        g_cameraPrevious.data(), kRenderViewTimeOffset);
    const auto currentTime = ReadUnaligned<std::int32_t>(
        g_cameraCurrent.data(), kRenderViewTimeOffset);
    WriteUnaligned(view.data(), kRenderViewTimeOffset,
                   InterpolatedPresentationTime(previousTime, currentTime,
                                                interpolationAlpha));
    return true;
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

double AnimationInterpolationAlpha(bool pending,
                                   std::uint32_t transitionSamples,
                                   double cameraAlpha,
                                   double latestTicAlpha,
                                   double pendingTicAlpha,
                                   bool multiTicEntityAlignment,
                                   bool overdueSnapshotFallback) {
    // One native tic of prediction is required to bridge the engine's regular
    // paired-tic catch-ups.  The upstream cadence functions already bound the
    // value to that horizon; keep a final defensive bound here as well.
    return std::clamp(
        EntityInterpolationAlpha(
            pending, transitionSamples, cameraAlpha, latestTicAlpha,
            pendingTicAlpha, multiTicEntityAlignment,
            overdueSnapshotFallback),
        0.0, 2.0);
}

std::uint32_t CameraSnapshotDiscontinuityMask(
    const std::array<std::uint8_t, kRenderViewSize>& previous,
    const std::array<std::uint8_t, kRenderViewSize>& current) {
    std::uint32_t mask = 0;
    const auto previousTime = ReadUnaligned<std::int32_t>(previous.data(), 80);
    const auto currentTime = ReadUnaligned<std::int32_t>(current.data(), 80);
    if (static_cast<std::int64_t>(currentTime) - previousTime !=
        kNativeTicMilliseconds) {
        mask |= kCameraProbeTimeDelta;
    }
    if (ReadUnaligned<std::int32_t>(previous.data(), 0) !=
        ReadUnaligned<std::int32_t>(current.data(), 0)) {
        mask |= kCameraProbeViewId;
    }
    if (std::abs(ReadUnaligned<float>(previous.data(), 20) -
                 ReadUnaligned<float>(current.data(), 20)) > 0.01f ||
        std::abs(ReadUnaligned<float>(previous.data(), 24) -
                 ReadUnaligned<float>(current.data(), 24)) > 0.01f) {
        mask |= kCameraProbeFov;
    }
    if (OriginDistance(previous.data(), current.data()) >
        kMaximumCameraStep) {
        mask |= kCameraProbeOrigin;
    }
    if (AxisAngleDegrees(previous.data(), current.data()) >
        kMaximumCameraAngleDegrees) {
        mask |= kCameraProbeAxis;
    }
    return mask;
}

bool CameraSnapshotRequiresReset(std::uint32_t changeMask) {
    constexpr std::uint32_t resetBits =
        kCameraProbeTimeDelta | kCameraProbeViewId |
        kCameraProbeOrigin | kCameraProbeAxis;
    return (changeMask & resetBits) != 0;
}

void InterpolateCameraFov(
    const std::array<std::uint8_t, kRenderViewSize>& previous,
    const std::array<std::uint8_t, kRenderViewSize>& current,
    double interpolationAlpha, void* output) {
    const double alpha = std::clamp(interpolationAlpha, 0.0, 1.0);
    for (const std::size_t offset : {std::size_t{20}, std::size_t{24}}) {
        const float previousFov = ReadUnaligned<float>(previous.data(), offset);
        const float currentFov = ReadUnaligned<float>(current.data(), offset);
        WriteUnaligned(
            output, offset,
            static_cast<float>(previousFov +
                (currentFov - previousFov) * alpha));
    }
}

std::optional<bool> BuildAuthoritativeBufferedView(
    const void* presentationView,
    std::array<std::uint8_t, kRenderViewSize>& temporary,
    double& interpolationAlpha, double& latestTicAlpha,
    double& pendingTicAlpha, double& basePitch, bool& basePitchValid,
    std::int64_t nowQpc) {
    if (!g_bufferedTwoTicInterpolationRequested ||
        !g_continuousSnapshotTimingRequested ||
        g_latestProducedCameraSequence < 2) {
        g_authoritativeCameraBufferActive = false;
        return std::nullopt;
    }
    const auto* latest = FindProducedCameraSnapshot(
        g_latestProducedCameraSequence);
    if (latest == nullptr ||
        ReadUnaligned<std::int32_t>(latest->view.data(), 80) !=
            ReadUnaligned<std::int32_t>(presentationView, 80) ||
        ReadUnaligned<std::int32_t>(latest->view.data(), 0) !=
            ReadUnaligned<std::int32_t>(presentationView, 0)) {
        g_authoritativeCameraBufferActive = false;
        g_cameraPreviousProductionSequence = 0;
        g_cameraCurrentProductionSequence = 0;
        g_cameraPreviousMouseSerial = 0;
        g_cameraCurrentMouseSerial = 0;
        return std::nullopt;
    }

    g_cameraFrameProbe = {};
    g_cameraFrameProbe.qpc = nowQpc;
    const std::int64_t nativeIntervalQpc = CameraIntervalQpc(
        kNativeTicMilliseconds, g_frequency.QuadPart);
    const auto initialize = [&]() -> bool {
        const std::uint64_t currentSequence = latest->sequence - 1;
        const auto* current = FindProducedCameraSnapshot(currentSequence);
        const auto* previous = currentSequence > 1
            ? FindProducedCameraSnapshot(currentSequence - 1)
            : nullptr;
        if (current == nullptr) return false;

        g_cameraCurrent = current->view;
        g_cameraPrevious = previous != nullptr
            ? previous->view
            : current->view;
        g_cameraCurrentPitch = current->pitch;
        g_cameraCurrentPitchValid = current->pitchValid;
        g_cameraPreviousPitch = previous != nullptr
            ? previous->pitch
            : current->pitch;
        g_cameraPreviousPitchValid = previous != nullptr
            ? previous->pitchValid
            : current->pitchValid;
        g_cameraCurrentProductionSequence = currentSequence;
        g_cameraPreviousProductionSequence = previous != nullptr
            ? previous->sequence
            : currentSequence;
        g_cameraCurrentMouseSerial = current->mouseSerial;
        g_cameraPreviousMouseSerial = previous != nullptr
            ? previous->mouseSerial
            : current->mouseSerial;
        g_consumedCameraProductionSequence = currentSequence;
        g_cameraCurrentQpc = latest->qpc;
        g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
        g_haveCameraCurrent = true;
        const std::uint32_t initialDiscontinuity = previous != nullptr
            ? CameraSnapshotDiscontinuityMask(previous->view, current->view)
            : 0;
        const bool initialReset =
            CameraSnapshotRequiresReset(initialDiscontinuity);
        g_canInterpolateCamera = previous != nullptr && !initialReset;
        if (initialReset) {
            ClearProducedRenderEntityHistories();
        }
        g_cameraFrameProbe.fovInterpolated = previous != nullptr &&
            !initialReset &&
            (initialDiscontinuity & kCameraProbeFov) != 0;
        g_cameraTimelineSynchronized = true;
        g_cameraPhaseRecoveryActive = false;
        g_cameraPhaseRecoveryCandidateQpc = 0;
        g_authoritativeCameraBufferActive = true;
        ++g_cameraSnapshotGeneration;
        g_lastCameraCallQpc = nowQpc;
        g_cameraFrameProbe.resetMask = kCameraProbeInitial;
        g_cameraFrameProbe.viewTime = ReadUnaligned<std::int32_t>(
            g_cameraCurrent.data(), 80);
        g_cameraFrameProbe.producerBufferDepth = static_cast<std::uint32_t>(
            latest->sequence - currentSequence);
        ResetTrackedEntityInterpolation();
        return true;
    };

    if (!g_authoritativeCameraBufferActive || !g_haveCameraCurrent) {
        if (!initialize()) return std::nullopt;
        basePitch = g_cameraCurrentPitch;
        basePitchValid = g_cameraCurrentPitchValid;
        ++g_viewCounters.snappedViews;
        return false;
    }

    const std::int64_t previousCallQpc = g_lastCameraCallQpc;
    const double callGapMilliseconds = previousCallQpc > 0
        ? 1000.0 * static_cast<double>(nowQpc - previousCallQpc) /
              static_cast<double>(g_frequency.QuadPart)
        : 0.0;
    g_lastCameraCallQpc = nowQpc;
    g_cameraFrameProbe.callGapMilliseconds = callGapMilliseconds;
    if (callGapMilliseconds > kMaximumCameraStallSeconds * 1000.0 ||
        nowQpc - g_cameraCurrentQpc > 3 * nativeIntervalQpc) {
        if (!initialize()) return std::nullopt;
        g_cameraFrameProbe.resetMask |= kCameraProbeStall;
        basePitch = g_cameraCurrentPitch;
        basePitchValid = g_cameraCurrentPitchValid;
        ++g_viewCounters.interpolationResets;
        ++g_viewCounters.resetStall;
        ++g_viewCounters.snappedViews;
        return false;
    }

    const std::int64_t nextBoundaryQpc =
        g_cameraCurrentQpc + nativeIntervalQpc;
    const std::uint64_t nextSequence =
        g_cameraCurrentProductionSequence + 1;
    const auto* next = FindProducedCameraSnapshot(nextSequence);
    if (nowQpc >= nextBoundaryQpc && next != nullptr) {
        const std::uint32_t discontinuity =
            CameraSnapshotDiscontinuityMask(g_cameraCurrent, next->view);
        const auto* lookahead = FindProducedCameraSnapshot(
            nextSequence + 1);
        const bool nextReady = ProducedCameraSampleReadyForAdvance(
            next->qpc, lookahead != nullptr, nowQpc, nativeIntervalQpc);
        if (CameraSnapshotRequiresReset(discontinuity)) {
            g_cameraPrevious = next->view;
            g_cameraCurrent = next->view;
            g_cameraPreviousPitch = next->pitch;
            g_cameraCurrentPitch = next->pitch;
            g_cameraPreviousPitchValid = next->pitchValid;
            g_cameraCurrentPitchValid = next->pitchValid;
            g_cameraPreviousProductionSequence = nextSequence;
            g_cameraCurrentProductionSequence = nextSequence;
            g_cameraPreviousMouseSerial = next->mouseSerial;
            g_cameraCurrentMouseSerial = next->mouseSerial;
            g_consumedCameraProductionSequence = nextSequence;
            g_cameraCurrentQpc = lookahead != nullptr
                ? lookahead->qpc
                : nowQpc;
            g_canInterpolateCamera = false;
            g_cameraFrameProbe.resetMask = discontinuity;
            ClearProducedRenderEntityHistories();
            ResetTrackedEntityInterpolation();
            ++g_viewCounters.interpolationResets;
        } else if (nextReady) {
            g_cameraPrevious = g_cameraCurrent;
            g_cameraCurrent = next->view;
            g_cameraPreviousPitch = g_cameraCurrentPitch;
            g_cameraPreviousPitchValid = g_cameraCurrentPitchValid;
            g_cameraCurrentPitch = next->pitch;
            g_cameraCurrentPitchValid = next->pitchValid;
            g_cameraPreviousProductionSequence =
                g_cameraCurrentProductionSequence;
            g_cameraCurrentProductionSequence = nextSequence;
            g_cameraPreviousMouseSerial = g_cameraCurrentMouseSerial;
            g_cameraCurrentMouseSerial = next->mouseSerial;
            g_consumedCameraProductionSequence = nextSequence;
            g_cameraCurrentQpc = nextBoundaryQpc;
            if (lookahead != nullptr &&
                g_cameraCurrentQpc > lookahead->qpc) {
                g_cameraFrameProbe.producerEarlyClampQpc =
                    g_cameraCurrentQpc - lookahead->qpc;
                g_cameraCurrentQpc = lookahead->qpc;
            }
            g_canInterpolateCamera = true;
            ++g_cameraSnapshotGeneration;
            g_cameraFrameProbe.viewDelta = kNativeTicMilliseconds;
            g_cameraFrameProbe.producerBufferAdvances = 1;
            g_cameraFrameProbe.producerSettledAdvances =
                lookahead == nullptr ? 1u : 0u;
            g_cameraFrameProbe.fovInterpolated =
                (discontinuity & kCameraProbeFov) != 0;
        }
    }

    g_cameraFrameProbe.viewTime = ReadUnaligned<std::int32_t>(
        g_cameraCurrent.data(), 80);
    g_cameraFrameProbe.producerBufferDepth =
        latest->sequence >= g_cameraCurrentProductionSequence
            ? static_cast<std::uint32_t>(
                  latest->sequence - g_cameraCurrentProductionSequence)
            : 0;
    if (!g_canInterpolateCamera) {
        basePitch = g_cameraCurrentPitch;
        basePitchValid = g_cameraCurrentPitchValid;
        ++g_viewCounters.snappedViews;
        return false;
    }

    const double alpha = CameraInterpolationAlphaForMode(
        kNativeTicMilliseconds, nowQpc, g_cameraCurrentQpc,
        g_frequency.QuadPart, false);
    interpolationAlpha = alpha;
    latestTicAlpha = alpha;
    pendingTicAlpha = 0.0;
    basePitchValid = g_cameraPreviousPitchValid &&
                     g_cameraCurrentPitchValid;
    if (basePitchValid) {
        basePitch = g_cameraPreviousPitch +
                    (g_cameraCurrentPitch - g_cameraPreviousPitch) * alpha;
    }
    std::memcpy(temporary.data(), presentationView, temporary.size());
    InterpolateCameraFov(g_cameraPrevious, g_cameraCurrent, alpha,
                         temporary.data());
    const auto previousOrigin = ReadOrigin(g_cameraPrevious.data());
    const auto currentOrigin = ReadOrigin(g_cameraCurrent.data());
    for (std::size_t index = 0; index < previousOrigin.size(); ++index) {
        WriteUnaligned(
            temporary.data(), 28 + index * sizeof(float),
            static_cast<float>(previousOrigin[index] +
                (currentOrigin[index] - previousOrigin[index]) * alpha));
    }
    const auto matrix = QuaternionToMatrix(Slerp(
        MatrixToQuaternion(ReadAxis(g_cameraPrevious.data())),
        MatrixToQuaternion(ReadAxis(g_cameraCurrent.data())), alpha));
    std::memcpy(temporary.data() + 40, matrix.data(), sizeof(matrix));
    ++g_viewCounters.interpolatedViews;
    return true;
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
    if (const auto buffered = BuildAuthoritativeBufferedView(
            view, temporary, interpolationAlpha, latestTicAlpha,
            pendingTicAlpha, basePitch, basePitchValid, now.QuadPart);
        buffered.has_value()) {
        return *buffered;
    }
    const void* presentationView = view;
    const ProducedCameraSnapshot* producedCurrent = nullptr;
    const ProducedCameraSnapshot* producedPrevious = nullptr;
    const std::uint64_t consumedProductionSequenceBefore =
        g_consumedCameraProductionSequence;
    if (g_latestProducedCameraSequence != 0 &&
        g_latestProducedCameraSequence !=
            g_consumedCameraProductionSequence) {
        const auto* candidate = FindProducedCameraSnapshot(
            g_latestProducedCameraSequence);
        if (candidate != nullptr &&
            ReadUnaligned<std::int32_t>(candidate->view.data(), 80) ==
                ReadUnaligned<std::int32_t>(presentationView, 80) &&
            ReadUnaligned<std::int32_t>(candidate->view.data(), 0) ==
                ReadUnaligned<std::int32_t>(presentationView, 0)) {
            producedCurrent = candidate;
            producedPrevious = FindProducedCameraSnapshot(
                candidate->sequence - 1);
            view = candidate->view.data();
            basePitch = candidate->pitch;
            basePitchValid = candidate->pitchValid;
            g_consumedCameraProductionSequence = candidate->sequence;
        }
    }
    const std::int64_t observedSnapshotQpc =
        producedCurrent != nullptr ? producedCurrent->qpc : now.QuadPart;
    const auto viewTime = ReadUnaligned<std::int32_t>(view, 80);
    g_cameraFrameProbe = {};
    g_cameraFrameProbe.qpc = now.QuadPart;
    g_cameraFrameProbe.viewTime = viewTime;
    if (!g_haveCameraCurrent) {
        g_cameraFrameProbe.resetMask = kCameraProbeInitial;
        ++g_cameraSnapshotGeneration;
        std::memcpy(g_cameraCurrent.data(), view, g_cameraCurrent.size());
        g_cameraPrevious = g_cameraCurrent;
        g_cameraCurrentPitch = basePitch;
        g_cameraPreviousPitch = basePitch;
        g_cameraCurrentPitchValid = basePitchValid;
        g_cameraPreviousPitchValid = basePitchValid;
        g_haveCameraCurrent = true;
        g_canInterpolateCamera = false;
        g_cameraCurrentQpc = observedSnapshotQpc;
        g_lastCameraCallQpc = now.QuadPart;
        g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
        g_cameraTimelineSynchronized = false;
        g_cameraPhaseRecoveryActive = false;
        g_cameraPhaseRecoveryCandidateQpc = 0;
        ResetTrackedEntityInterpolation();
        ++g_viewCounters.snappedViews;
        return false;
    }

    const std::int64_t previousCallQpc = g_lastCameraCallQpc;
    const double callGap = static_cast<double>(now.QuadPart - previousCallQpc) /
                           static_cast<double>(g_frequency.QuadPart);
    g_cameraFrameProbe.callGapMilliseconds = callGap * 1000.0;
    g_lastCameraCallQpc = now.QuadPart;
    auto currentTime =
        ReadUnaligned<std::int32_t>(g_cameraCurrent.data(), 80);
    const std::int64_t presentationObservedViewDelta =
        static_cast<std::int64_t>(viewTime) - currentTime;
    bool producerPreAdvanceDiscontinuity = false;
    if (producedPrevious != nullptr &&
        producedPrevious->sequence > consumedProductionSequenceBefore) {
        const auto previousProducedTime = ReadUnaligned<std::int32_t>(
            producedPrevious->view.data(), 80);
        const auto currentViewId =
            ReadUnaligned<std::int32_t>(g_cameraCurrent.data(), 0);
        const auto previousProducedViewId = ReadUnaligned<std::int32_t>(
            producedPrevious->view.data(), 0);
        producerPreAdvanceDiscontinuity =
            !IsConsecutiveProducedCameraBridge(
                currentTime, previousProducedTime, viewTime) ||
            currentViewId != previousProducedViewId ||
            OriginDistance(g_cameraCurrent.data(),
                           producedPrevious->view.data()) >
                kMaximumCameraStep ||
            AxisAngleDegrees(g_cameraCurrent.data(),
                             producedPrevious->view.data()) >
                kMaximumCameraAngleDegrees;
        if (!producerPreAdvanceDiscontinuity) {
            std::memcpy(g_cameraCurrent.data(),
                        producedPrevious->view.data(),
                        g_cameraCurrent.size());
            g_cameraCurrentPitch = producedPrevious->pitch;
            g_cameraCurrentPitchValid = producedPrevious->pitchValid;
            if (g_cameraTimelineSynchronized) {
                std::int64_t earlyClampQpc = 0;
                g_cameraCurrentQpc = AdvanceProducedCameraSnapshotClock(
                    g_cameraCurrentQpc, kNativeTicMilliseconds,
                    producedPrevious->qpc, g_frequency.QuadPart,
                    &earlyClampQpc);
                g_cameraFrameProbe.producerEarlyClampQpc += earlyClampQpc;
            } else {
                g_cameraCurrentQpc = producedPrevious->qpc;
            }
            currentTime = previousProducedTime;
            g_cameraFrameProbe.producerBridgeSamples = 1;
        }
    }
    if (viewTime == currentTime && callGap > kMaximumCameraStallSeconds) {
        g_cameraFrameProbe.resetMask = kCameraProbeSameTimeStall;
        std::memcpy(g_cameraCurrent.data(), view, g_cameraCurrent.size());
        g_cameraPrevious = g_cameraCurrent;
        g_cameraPreviousPitch = g_cameraCurrentPitch;
        g_cameraPreviousPitchValid = g_cameraCurrentPitchValid;
        g_canInterpolateCamera = false;
        g_cameraCurrentQpc = now.QuadPart;
        g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
        g_cameraTimelineSynchronized = false;
        g_cameraPhaseRecoveryActive = false;
        g_cameraPhaseRecoveryCandidateQpc = 0;
        ResetTrackedEntityInterpolation();
        ++g_viewCounters.interpolationResets;
        ++g_viewCounters.resetStall;
    } else if (viewTime != currentTime) {
        ++g_cameraSnapshotGeneration;
        const auto currentViewId =
            ReadUnaligned<std::int32_t>(g_cameraCurrent.data(), 0);
        const auto nextViewId = ReadUnaligned<std::int32_t>(view, 0);
        const auto timeDelta = static_cast<std::int64_t>(viewTime) - currentTime;
        g_cameraFrameProbe.viewDelta = presentationObservedViewDelta;
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
        const bool clockDebtExceeded =
            g_continuousSnapshotTimingRequested &&
            g_cameraTimelineSynchronized && !timeDeltaChanged &&
            CameraSnapshotClockDebtExceeded(
                g_cameraCurrentQpc, static_cast<std::int32_t>(timeDelta),
                g_overdueSnapshotFallbackRequested
                    ? kMaximumInterpolatedCameraIntervalMilliseconds
                    : kNativeTicMilliseconds,
                now.QuadPart, g_frequency.QuadPart);
        const std::int32_t maximumClockDebtMilliseconds =
            g_overdueSnapshotFallbackRequested
                ? kMaximumInterpolatedCameraIntervalMilliseconds
                : kNativeTicMilliseconds;
        const double clockDebtMilliseconds = clockDebtExceeded
            ? 1000.0 * static_cast<double>(
                  now.QuadPart -
                  (g_cameraCurrentQpc + CameraIntervalQpc(
                      static_cast<std::int32_t>(timeDelta),
                      g_frequency.QuadPart))) /
                  static_cast<double>(g_frequency.QuadPart)
            : 0.0;
        const bool reset = timeDeltaChanged || viewIdChanged || stalled ||
                           originExceeded || axisExceeded ||
                           clockDebtExceeded ||
                           producerPreAdvanceDiscontinuity;
        g_cameraFrameProbe.fovInterpolated = fovChanged && !reset;
        if (reset) {
            if (timeDeltaChanged || producerPreAdvanceDiscontinuity) {
                g_cameraFrameProbe.resetMask |= kCameraProbeTimeDelta;
            }
            if (viewIdChanged) {
                g_cameraFrameProbe.resetMask |= kCameraProbeViewId;
            }
            if (fovChanged) g_cameraFrameProbe.resetMask |= kCameraProbeFov;
            if (stalled) g_cameraFrameProbe.resetMask |= kCameraProbeStall;
            if (originExceeded) {
                g_cameraFrameProbe.resetMask |= kCameraProbeOrigin;
            }
            if (axisExceeded) g_cameraFrameProbe.resetMask |= kCameraProbeAxis;
            if (clockDebtExceeded) {
                g_cameraFrameProbe.resetMask |= kCameraProbeClockDebt;
            }
            std::memcpy(g_cameraCurrent.data(), view, g_cameraCurrent.size());
            g_cameraPrevious = g_cameraCurrent;
            g_cameraCurrentPitch = basePitch;
            g_cameraPreviousPitch = basePitch;
            g_cameraCurrentPitchValid = basePitchValid;
            g_cameraPreviousPitchValid = basePitchValid;
            g_canInterpolateCamera = false;
            g_cameraCurrentQpc = observedSnapshotQpc;
            g_cameraCurrentIntervalMilliseconds = kNativeTicMilliseconds;
            g_cameraTimelineSynchronized = false;
            g_cameraPhaseRecoveryActive = false;
            g_cameraPhaseRecoveryCandidateQpc = 0;
            ResetTrackedEntityInterpolation();
            ++g_viewCounters.interpolationResets;
            if (timeDeltaChanged || producerPreAdvanceDiscontinuity) {
                ++g_viewCounters.resetTimeDelta;
            }
            if (viewIdChanged) ++g_viewCounters.resetViewId;
            if (fovChanged) ++g_viewCounters.resetFov;
            if (stalled || clockDebtExceeded) ++g_viewCounters.resetStall;
            if (originExceeded) ++g_viewCounters.resetOrigin;
            if (axisExceeded) ++g_viewCounters.resetAxis;
            if (clockDebtExceeded) {
                char buffer[256]{};
                const int length = _snprintf_s(
                    buffer, sizeof(buffer), _TRUNCATE,
                    "camera: presentation timeline automatically rebased; "
                    "reason=clock_debt qpc=%lld view_time=%d view_delta=%lld "
                    "debt_ms=%.4f limit_ms=%d\r\n",
                    static_cast<long long>(now.QuadPart), viewTime,
                    static_cast<long long>(timeDelta), clockDebtMilliseconds,
                    maximumClockDebtMilliseconds);
                if (length > 0) {
                    LogPresentationEvent(
                        std::string(buffer, static_cast<std::size_t>(length)));
                }
            }
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
                if (producedCurrent != nullptr &&
                    g_cameraTimelineSynchronized) {
                    // Every producer sample represents one completed native
                    // tic, including the intermediate sample in a paired-tic
                    // catch-up. Preserve that logical cadence when production
                    // is late, but never place an already-produced snapshot in
                    // the future. The one-sided clamp can only advance the
                    // presentation phase and cannot cause an alpha rollback.
                    std::int64_t earlyClampQpc = 0;
                    g_cameraCurrentQpc = AdvanceProducedCameraSnapshotClock(
                        g_cameraCurrentQpc,
                        g_cameraCurrentIntervalMilliseconds,
                        observedSnapshotQpc, g_frequency.QuadPart,
                        &earlyClampQpc);
                    g_cameraFrameProbe.producerEarlyClampQpc += earlyClampQpc;
                    g_cameraPhaseRecoveryActive = false;
                    g_cameraPhaseRecoveryCandidateQpc = 0;
                } else if (g_cameraTimelineSynchronized) {
                    const CameraSnapshotClockUpdate clockUpdate =
                        UpdateCameraSnapshotClock(
                            g_cameraCurrentQpc,
                            g_cameraCurrentIntervalMilliseconds,
                            previousCallQpc, now.QuadPart,
                            g_frequency.QuadPart,
                            g_cameraPhaseRecoveryActive,
                            g_cameraPhaseRecoveryCandidateQpc);
                    g_cameraCurrentQpc = clockUpdate.qpc;
                    if (clockUpdate.recoveryStarted ||
                        clockUpdate.recoveryFinished) {
                        const double phaseLagMilliseconds =
                            1000.0 * static_cast<double>(
                                clockUpdate.phaseLagQpc) /
                            static_cast<double>(g_frequency.QuadPart);
                        const double correctionMilliseconds =
                            1000.0 * static_cast<double>(
                                clockUpdate.correctionQpc) /
                            static_cast<double>(g_frequency.QuadPart);
                        char buffer[288]{};
                        const int length = _snprintf_s(
                            buffer, sizeof(buffer), _TRUNCATE,
                            "camera: presentation timeline phase recovery %s; "
                            "qpc=%lld view_time=%d view_delta=%lld "
                            "lag_ms=%.4f correction_ms=%.4f\r\n",
                            clockUpdate.recoveryStarted ? "started" : "finished",
                            static_cast<long long>(now.QuadPart), viewTime,
                            static_cast<long long>(timeDelta),
                            phaseLagMilliseconds, correctionMilliseconds);
                        if (length > 0) {
                            LogPresentationEvent(std::string(
                                buffer, static_cast<std::size_t>(length)));
                        }
                    }
                } else {
                    // Establish a stable phase, then advance it from the
                    // authoritative simulation timestamps.
                    g_cameraCurrentQpc = observedSnapshotQpc;
                    g_cameraTimelineSynchronized = true;
                    g_cameraPhaseRecoveryActive = false;
                    g_cameraPhaseRecoveryCandidateQpc = 0;
                }
            } else {
                // RC1 compatibility: restart interpolation when this
                // presentation call observes each new snapshot.
                g_cameraCurrentQpc = observedSnapshotQpc;
                g_cameraTimelineSynchronized = false;
                g_cameraPhaseRecoveryActive = false;
                g_cameraPhaseRecoveryCandidateQpc = 0;
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
    const double rotationAlpha = CameraRotationInterpolationAlpha(alpha);
    interpolationAlpha = alpha;
    latestTicAlpha = LatestNativeTicInterpolationAlpha(
        now.QuadPart, g_cameraCurrentQpc, g_frequency.QuadPart);
    pendingTicAlpha = PendingNativeTicInterpolationAlpha(
        now.QuadPart, g_cameraCurrentQpc, g_frequency.QuadPart);
    if (alpha > 1.0) ++g_viewCounters.extrapolatedViews;
    basePitchValid = g_cameraPreviousPitchValid && g_cameraCurrentPitchValid;
    if (basePitchValid) {
        basePitch = g_cameraPreviousPitch +
                    (g_cameraCurrentPitch - g_cameraPreviousPitch) *
                        rotationAlpha;
    }
    // Preserve the exact final SingleView flags/template. The producer
    // observer supplies the completed-tic camera transform and FOV history.
    std::memcpy(temporary.data(), presentationView, temporary.size());
    InterpolateCameraFov(g_cameraPrevious, g_cameraCurrent, rotationAlpha,
                         temporary.data());
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
              MatrixToQuaternion(ReadAxis(g_cameraCurrent.data())),
              rotationAlpha);
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

void AppendInterpolationTrace(
    const void* sourceView, const void* presentedView,
    bool cameraInterpolated, bool effectsTimeApplied, bool mouseOverlaid,
    double cameraAlpha, double latestTicAlpha, double pendingTicAlpha,
    double basePitch,
    std::size_t appliedEntityCount,
    const std::array<AppliedViewEntityPose, kMaximumAppliedEntityPoses>& applied,
    const ViewCounters& beforeEntities) {
    if (!g_interpolationTraceRequested || sourceView == nullptr ||
        presentedView == nullptr || g_frequency.QuadPart <= 0) {
        return;
    }

    std::size_t rootCurrent = 0;
    std::size_t rootPending = 0;
    std::size_t rootOther = 0;
    std::size_t animationCurrent = 0;
    std::size_t animationPending = 0;
    std::size_t animationOther = 0;
    for (std::size_t index = 0; index < g_activeRenderEntityCount; ++index) {
        const auto* tracked =
            FindTrackedRenderEntity(g_activeRenderEntityHandles[index]);
        if (tracked == nullptr) continue;
        if (g_authoritativeCameraBufferActive) {
            const auto* previousPose = FindProducedEntityPose(
                *tracked, g_cameraPreviousProductionSequence);
            const auto* currentPose = FindProducedEntityPose(
                *tracked, g_cameraCurrentProductionSequence);
            if (previousPose != nullptr && currentPose != nullptr) {
                ++rootCurrent;
            } else {
                ++rootOther;
            }
            const auto* previousAnimation = FindProducedEntityAnimation(
                *tracked, g_cameraPreviousProductionSequence);
            const auto* currentAnimation = FindProducedEntityAnimation(
                *tracked, g_cameraCurrentProductionSequence);
            if (previousAnimation != nullptr && currentAnimation != nullptr) {
                ++animationCurrent;
            } else if (tracked->animation.hasCurrent) {
                ++animationOther;
            }
            continue;
        }
        if (tracked->hasCurrent && tracked->canInterpolate) {
            if (tracked->transitionGeneration == g_cameraSnapshotGeneration) {
                ++rootCurrent;
            } else if (tracked->transitionGeneration ==
                       g_cameraSnapshotGeneration + 1) {
                ++rootPending;
            } else {
                ++rootOther;
            }
        }
        if (tracked->animation.hasCurrent &&
            tracked->animation.canInterpolate) {
            if (tracked->animation.transitionGeneration ==
                g_cameraSnapshotGeneration) {
                ++animationCurrent;
            } else if (tracked->animation.transitionGeneration ==
                       g_cameraSnapshotGeneration + 1) {
                ++animationPending;
            } else {
                ++animationOther;
            }
        }
    }

    const auto sourceOrigin = ReadOrigin(sourceView);
    const auto presentedOrigin = ReadOrigin(presentedView);
    const auto sourceTime = ReadUnaligned<std::int32_t>(
        sourceView, kRenderViewTimeOffset);
    const auto presentedTime = ReadUnaligned<std::int32_t>(
        presentedView, kRenderViewTimeOffset);
    const auto sourceAxis = ReadAxis(sourceView);
    const auto presentedAxis = ReadAxis(presentedView);
    const std::int32_t comTicNumber = ReadComTicNumber();
    const SelectedGameTicSnapshot selectedGameTic = ReadSelectedGameTic();
    const double cameraSourceAgeMilliseconds =
        g_cameraProductionProbe.qpc > 0
            ? 1000.0 * static_cast<double>(
                  g_cameraFrameProbe.qpc - g_cameraProductionProbe.qpc) /
                  static_cast<double>(g_frequency.QuadPart)
            : -1.0;
    const std::uint64_t selectedTicsSinceCameraSource =
        selectedGameTic.serial >=
                g_cameraProductionProbe.selectedGameTicSerial
            ? selectedGameTic.serial -
                  g_cameraProductionProbe.selectedGameTicSerial
            : 0;
    int viewModelHandle = -1;
    std::array<float, 3> viewModelSourceOrigin{};
    std::array<float, 3> viewModelPresentedOrigin{};
    for (std::size_t index = 0; index < appliedEntityCount; ++index) {
        const auto* tracked = FindTrackedRenderEntity(applied[index].handle);
        if (!applied[index].poseApplied || tracked == nullptr ||
            tracked->allowViewId == 0) {
            continue;
        }
        viewModelHandle = applied[index].handle;
        for (std::size_t component = 0; component < 3; ++component) {
            viewModelSourceOrigin[component] = ReadUnaligned<float>(
                applied[index].original.data(), component * sizeof(float));
            viewModelPresentedOrigin[component] = ReadUnaligned<float>(
                applied[index].renderEntity, kRenderEntityOriginOffset +
                    component * sizeof(float));
        }
        break;
    }
    const auto previousTime = ReadUnaligned<std::int32_t>(
        g_cameraPrevious.data(), 80);
    const auto currentTime = ReadUnaligned<std::int32_t>(
        g_cameraCurrent.data(), 80);
    const double snapshotAgeMilliseconds =
        1000.0 * static_cast<double>(g_cameraFrameProbe.qpc -
                                     g_cameraCurrentQpc) /
        static_cast<double>(g_frequency.QuadPart);
    const double producerEarlyClampMilliseconds =
        1000.0 * static_cast<double>(
            g_cameraFrameProbe.producerEarlyClampQpc) /
        static_cast<double>(g_frequency.QuadPart);
    const auto delta = [](std::uint64_t after, std::uint64_t before) {
        return after - before;
    };

    char line[3072]{};
    const int length = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "interp_trace: qpc=%lld com_tic=%d selected_tic_serial=%llu "
        "selected_tic_qpc=%lld selected_tic_argument=%d "
        "selected_tic_com_tic=%d camera_source_calls=%llu "
        "producer_latest_sequence=%llu producer_consumed_sequence=%llu "
        "camera_source_tic_serial=%llu camera_source_qpc=%lld "
        "camera_source_age_ms=%.4f camera_source_view_time=%d "
        "selected_tics_since_camera_source=%llu "
        "view_time=%d camera_prev_time=%d "
        "camera_current_time=%d view_delta=%lld call_gap_ms=%.4f "
        "snapshot_qpc=%lld snapshot_age_ms=%.4f interval_ms=%d "
        "generation=%llu reset_mask=0x%02x timeline_sync=%d "
        "producer_bridge_samples=%u producer_early_clamp_ms=%.4f "
        "producer_buffer_depth=%u producer_buffer_advances=%u "
        "producer_settled_advances=%u "
        "fov_interpolated=%d can_interpolate=%d "
        "camera_applied=%d effects_time_applied=%d mouse_applied=%d "
        "source_time=%d presented_time=%d "
        "alpha=%.6f latest_alpha=%.6f pending_alpha=%.6f "
        "source_origin=%.4f,%.4f,%.4f presented_origin=%.4f,%.4f,%.4f "
        "source_forward=%.6f,%.6f,%.6f "
        "presented_forward=%.6f,%.6f,%.6f base_pitch=%.6f "
        "mouse_yaw=%.6f mouse_pitch=%.6f "
        "mouse_tracked_yaw=%.6f mouse_tracked_pitch=%.6f "
        "mouse_transition_yaw=%.6f mouse_transition_pitch=%.6f "
        "mouse_peek=%d mouse_dx=%d mouse_dy=%d "
        "mouse_serial_latest=%llu mouse_serial_included_before=%llu "
        "mouse_serial_included_after=%llu mouse_serial_selected=%llu "
        "mouse_buffered=%d mouse_buffer_previous_serial=%llu "
        "mouse_buffer_current_serial=%llu "
        "viewmodel_handle=%d viewmodel_source_origin=%.4f,%.4f,%.4f "
        "viewmodel_presented_origin=%.4f,%.4f,%.4f "
        "applied_entities=%zu active_entities=%zu "
        "roots_current=%zu roots_pending=%zu roots_other=%zu "
        "anims_current=%zu anims_pending=%zu anims_other=%zu "
        "frame_viewmodel_roots=%llu frame_world_roots=%llu "
        "frame_viewmodel_anims=%llu frame_world_anims=%llu "
        "frame_viewmodel_latest_roots=%llu frame_world_latest_roots=%llu "
        "frame_viewmodel_pending_roots=%llu frame_world_pending_roots=%llu "
        "frame_viewmodel_latest_anims=%llu frame_world_latest_anims=%llu "
        "frame_viewmodel_pending_anims=%llu frame_world_pending_anims=%llu\r\n",
        static_cast<long long>(g_cameraFrameProbe.qpc),
        comTicNumber,
        static_cast<unsigned long long>(selectedGameTic.serial),
        static_cast<long long>(selectedGameTic.qpc), selectedGameTic.argument,
        selectedGameTic.comTic,
        static_cast<unsigned long long>(g_cameraProductionProbe.calls),
        static_cast<unsigned long long>(g_latestProducedCameraSequence),
        static_cast<unsigned long long>(
            g_consumedCameraProductionSequence),
        static_cast<unsigned long long>(
            g_cameraProductionProbe.selectedGameTicSerial),
        static_cast<long long>(g_cameraProductionProbe.qpc),
        cameraSourceAgeMilliseconds, g_cameraProductionProbe.viewTime,
        static_cast<unsigned long long>(selectedTicsSinceCameraSource),
        g_cameraFrameProbe.viewTime, previousTime, currentTime,
        static_cast<long long>(g_cameraFrameProbe.viewDelta),
        g_cameraFrameProbe.callGapMilliseconds,
        static_cast<long long>(g_cameraCurrentQpc), snapshotAgeMilliseconds,
        g_cameraCurrentIntervalMilliseconds,
        static_cast<unsigned long long>(g_cameraSnapshotGeneration),
        g_cameraFrameProbe.resetMask,
        g_cameraTimelineSynchronized ? 1 : 0,
        g_cameraFrameProbe.producerBridgeSamples,
        producerEarlyClampMilliseconds,
        g_cameraFrameProbe.producerBufferDepth,
        g_cameraFrameProbe.producerBufferAdvances,
        g_cameraFrameProbe.producerSettledAdvances,
        g_cameraFrameProbe.fovInterpolated ? 1 : 0,
        g_canInterpolateCamera ? 1 : 0, cameraInterpolated ? 1 : 0,
        effectsTimeApplied ? 1 : 0, mouseOverlaid ? 1 : 0,
        sourceTime, presentedTime, cameraAlpha, latestTicAlpha,
        pendingTicAlpha, sourceOrigin[0], sourceOrigin[1], sourceOrigin[2],
        presentedOrigin[0], presentedOrigin[1], presentedOrigin[2],
        sourceAxis[0], sourceAxis[1], sourceAxis[2], presentedAxis[0],
        presentedAxis[1], presentedAxis[2], basePitch,
        g_mouseFrameProbe.totalYaw, g_mouseFrameProbe.totalPitch,
        g_mouseFrameProbe.trackedYaw, g_mouseFrameProbe.trackedPitch,
        g_mouseFrameProbe.transitionYaw, g_mouseFrameProbe.transitionPitch,
        g_mouseFrameProbe.peeked ? 1 : 0, g_mouseFrameProbe.peekDeltaX,
        g_mouseFrameProbe.peekDeltaY,
        static_cast<unsigned long long>(g_mouseFrameProbe.latestSerial),
        static_cast<unsigned long long>(
            g_mouseFrameProbe.includedSerialBefore),
        static_cast<unsigned long long>(
            g_mouseFrameProbe.includedSerialAfter),
        static_cast<unsigned long long>(g_mouseFrameProbe.selectedSerial),
        g_mouseFrameProbe.buffered ? 1 : 0,
        static_cast<unsigned long long>(
            g_mouseFrameProbe.bufferedPreviousSerial),
        static_cast<unsigned long long>(
            g_mouseFrameProbe.bufferedCurrentSerial),
        viewModelHandle, viewModelSourceOrigin[0], viewModelSourceOrigin[1],
        viewModelSourceOrigin[2], viewModelPresentedOrigin[0],
        viewModelPresentedOrigin[1], viewModelPresentedOrigin[2],
        appliedEntityCount, g_activeRenderEntityCount, rootCurrent,
        rootPending, rootOther, animationCurrent, animationPending,
        animationOther,
        static_cast<unsigned long long>(delta(
            g_viewCounters.adjustedViewModels,
            beforeEntities.adjustedViewModels)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.adjustedWorldEntities,
            beforeEntities.adjustedWorldEntities)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.adjustedViewModelAnimations,
            beforeEntities.adjustedViewModelAnimations)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.adjustedWorldAnimations,
            beforeEntities.adjustedWorldAnimations)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.viewModelLatestTicRoots,
            beforeEntities.viewModelLatestTicRoots)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.worldLatestTicRoots,
            beforeEntities.worldLatestTicRoots)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.viewModelPendingRoots,
            beforeEntities.viewModelPendingRoots)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.worldPendingRoots,
            beforeEntities.worldPendingRoots)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.viewModelLatestTicAnimations,
            beforeEntities.viewModelLatestTicAnimations)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.worldLatestTicAnimations,
            beforeEntities.worldLatestTicAnimations)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.viewModelPendingAnimations,
            beforeEntities.viewModelPendingAnimations)),
        static_cast<unsigned long long>(delta(
            g_viewCounters.worldPendingAnimations,
            beforeEntities.worldPendingAnimations)));
    if (length <= 0) return;
    LogPresentationEvent(
        std::string(line, static_cast<std::size_t>(length)));
}

void AppendCameraProductionTrace(const void* renderView,
                                 std::int64_t beginQpc,
                                 std::int64_t endQpc) {
    ++g_cameraProductionProbe.calls;
    const SelectedGameTicSnapshot selected = ReadSelectedGameTic();
    const bool hadReadableView = g_cameraProductionProbe.readable;
    const std::int32_t previousViewTime =
        g_cameraProductionProbe.viewTime;
    g_cameraProductionProbe.selectedGameTicSerial = selected.serial;
    g_cameraProductionProbe.qpc = endQpc;
    g_cameraProductionProbe.comTic = ReadComTicNumber();
    g_cameraProductionProbe.readable =
        IsReadableRange(renderView, kRenderViewSize);

    std::int32_t viewId = 0;
    std::array<float, 3> origin{};
    std::uint64_t producedMouseSerial = 0;
    if (g_cameraProductionProbe.readable) {
        const std::uint64_t producedSequence =
            g_latestProducedCameraSequence + 1;
        auto& produced = g_producedCameraSnapshots[
            static_cast<std::size_t>(
                producedSequence % kProducedCameraSnapshotQueueSize)];
        std::memcpy(produced.view.data(), renderView, produced.view.size());
        produced.sequence = producedSequence;
        produced.qpc = endQpc;
        produced.mouseSerial =
            g_selectedMouseSerial.load(std::memory_order_acquire);
        producedMouseSerial = produced.mouseSerial;
        produced.pitchValid = ReadEffectivePitch(produced.pitch);
        g_latestProducedCameraSequence = producedSequence;

        const std::int32_t viewTime =
            ReadUnaligned<std::int32_t>(renderView, 80);
        g_cameraProductionProbe.previousViewTime = previousViewTime;
        g_cameraProductionProbe.havePreviousViewTime = hadReadableView;
        g_cameraProductionProbe.viewTime = viewTime;
        viewId = ReadUnaligned<std::int32_t>(renderView, 0);
        origin = ReadOrigin(renderView);
    }

    if (!g_interpolationTraceRequested || g_frequency.QuadPart <= 0) return;
    const double durationMicroseconds = 1'000'000.0 *
        static_cast<double>(endQpc - beginQpc) /
        static_cast<double>(g_frequency.QuadPart);
    const double selectedAgeMilliseconds = selected.qpc > 0
        ? 1000.0 * static_cast<double>(endQpc - selected.qpc) /
              static_cast<double>(g_frequency.QuadPart)
        : -1.0;
    const std::int32_t viewDelta =
        g_cameraProductionProbe.readable &&
                g_cameraProductionProbe.havePreviousViewTime
            ? g_cameraProductionProbe.viewTime -
                  g_cameraProductionProbe.previousViewTime
            : 0;
    char line[768]{};
    const int length = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "camera_source: qpc=%lld begin_qpc=%lld duration_us=%.3f "
        "producer_sequence=%llu mouse_serial=%llu "
        "selected_tic_serial=%llu selected_tic_qpc=%lld "
        "selected_tic_age_ms=%.4f selected_tic_argument=%d "
        "selected_tic_com_tic=%d com_tic=%d readable=%d view_id=%d "
        "view_time=%d view_delta=%d origin=%.4f,%.4f,%.4f\r\n",
        static_cast<long long>(endQpc), static_cast<long long>(beginQpc),
        durationMicroseconds,
        static_cast<unsigned long long>(g_latestProducedCameraSequence),
        static_cast<unsigned long long>(producedMouseSerial),
        static_cast<unsigned long long>(selected.serial),
        static_cast<long long>(selected.qpc), selectedAgeMilliseconds,
        selected.argument, selected.comTic, g_cameraProductionProbe.comTic,
        g_cameraProductionProbe.readable ? 1 : 0, viewId,
        g_cameraProductionProbe.viewTime, viewDelta, origin[0], origin[1],
        origin[2]);
    if (length > 0) {
        LogPresentationEvent(
            std::string(line, static_cast<std::size_t>(length)));
    }
}

void __fastcall HookedCalculateRenderView(void* self, void*) {
    LARGE_INTEGER begin{};
    QueryPerformanceCounter(&begin);
    g_originalCalculateRenderView(self);
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&end);

    const void* renderView = nullptr;
    if (IsReadableRange(self, 0xa8)) {
        renderView = ReadUnaligned<const void*>(self, 0xa4);
    }
    AppendCameraProductionTrace(renderView, begin.QuadPart, end.QuadPart);
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
    const bool effectsTimeApplied = cameraInterpolated &&
        ApplyInterpolatedEffectTime(temporary, interpolationAlpha);
    const void* baseView = cameraInterpolated ? temporary.data() : view;
    MouseOverlay mouseOverlay;
    const bool mouseOverlaid = ApplyPendingMouseOverlay(
        baseView, temporary, cameraInterpolated, view, interpolationAlpha,
        basePitch, basePitchValid, mouseOverlay);
    const void* presentedView =
        (cameraInterpolated || mouseOverlaid) ? temporary.data() : view;
    const int viewId = view != nullptr ? ReadUnaligned<int>(view, 0) : 0;
    const ViewCounters beforeEntities = g_viewCounters;
    const std::size_t appliedCount =
        ApplyInterpolatedRenderEntities(viewId, interpolationAlpha,
                                        latestTicAlpha, pendingTicAlpha,
                                        cameraInterpolated, mouseOverlay,
                                        g_appliedEntityPoses);
    AppendAnimatedWorldEntityTrace(g_appliedEntityPoses, appliedCount);
    AppendInterpolationTrace(
        view, presentedView, cameraInterpolated, effectsTimeApplied,
        mouseOverlaid,
        interpolationAlpha, latestTicAlpha, pendingTicAlpha, basePitch,
        appliedCount,
        g_appliedEntityPoses, beforeEntities);
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

std::optional<int> RequestedCvarInteger(const char* name) {
    if (name == nullptr) return std::nullopt;
    if (_stricmp(name, "r_mode") == 0) return g_requestedMode;
    if (_stricmp(name, "r_fullscreen") == 0) return g_requestedFullscreen;
    if (_stricmp(name, "r_swapInterval") == 0) {
        return g_requestedSwapInterval;
    }
    if (_stricmp(name, "r_customWidth") == 0) {
        return g_requestedCustomWidth;
    }
    if (_stricmp(name, "r_customHeight") == 0) {
        return g_requestedCustomHeight;
    }
    return std::nullopt;
}

void ApplyDisplayOverrides(void* cvarSystem) {
    int validationState =
        g_retailValidationState.load(std::memory_order_acquire);
    if (validationState == 0 && g_retailValidationEvent != nullptr) {
        WaitForSingleObject(g_retailValidationEvent, 30'000);
        validationState =
            g_retailValidationState.load(std::memory_order_acquire);
    }
    if (validationState != 1) return;

    int expected = 0;
    if (!g_displayOverrideStartupState.compare_exchange_strong(
            expected, 1, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return;
    }
    const auto set = [cvarSystem](const char* name,
                                  const std::optional<int>& value) {
        if (value) g_originalSetCVarInteger(cvarSystem, name, *value, 0);
    };
    set("r_mode", g_requestedMode);
    set("r_customWidth", g_requestedCustomWidth);
    set("r_customHeight", g_requestedCustomHeight);
    set("r_fullscreen", g_requestedFullscreen);
    set("r_swapInterval", g_requestedSwapInterval);
    g_displayOverrideStartupState.store(2, std::memory_order_release);
    Log("display: renderer cvar overrides applied on the engine thread\r\n");
}

void __fastcall HookedSetCVarString(void* self, void*, const char* name,
                                    const char* value, int flags) {
    ApplyDisplayOverrides(self);
    char replacement[24]{};
    if (g_retailValidationState.load(std::memory_order_acquire) == 1 &&
        g_displayOverrideStartupState.load(std::memory_order_acquire) != 3) {
        if (const auto requested = RequestedCvarInteger(name)) {
            _snprintf_s(replacement, sizeof(replacement), _TRUNCATE, "%d",
                        *requested);
            value = replacement;
        }
    }
    g_originalSetCVarString(self, name, value, flags);
}

void __fastcall HookedSetCVarBool(void* self, void*, const char* name,
                                  bool value, int flags) {
    ApplyDisplayOverrides(self);
    if (g_retailValidationState.load(std::memory_order_acquire) == 1 &&
        g_displayOverrideStartupState.load(std::memory_order_acquire) != 3) {
        if (const auto requested = RequestedCvarInteger(name)) {
            value = *requested != 0;
        }
    }
    g_originalSetCVarBool(self, name, value, flags);
}

void __fastcall HookedSetCVarInteger(void* self, void*, const char* name,
                                     int value, int flags) {
    ApplyDisplayOverrides(self);
    if (g_retailValidationState.load(std::memory_order_acquire) == 1 &&
        g_displayOverrideStartupState.load(std::memory_order_acquire) != 3) {
        if (const auto requested = RequestedCvarInteger(name)) value = *requested;
    }
    g_originalSetCVarInteger(self, name, value, flags);
}

bool InstallDisplayCvarHooks(DWORD timeoutMilliseconds, std::string& error) {
    if (!g_requestedMode && !g_requestedFullscreen &&
        !g_requestedSwapInterval && !g_requestedCustomWidth &&
        !g_requestedCustomHeight) {
        return true;
    }

    auto* executable =
        reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (executable == nullptr) {
        error = "the main executable module is unavailable";
        return false;
    }
    auto** vtable =
        reinterpret_cast<void**>(executable + kCvarSystemVtableRva);
    const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
    while (!IsReadableRange(vtable, 9 * sizeof(void*)) ||
           vtable[6] != executable + kSetCVarStringRva ||
           vtable[7] != executable + kSetCVarBoolRva ||
           vtable[8] != executable + kSetCVarIntegerRva) {
        if (GetTickCount64() >= deadline) {
            error = "the validated static idCVarSystem vtable did not appear before timeout";
            return false;
        }
        Sleep(1);
    }
    g_setCVarStringSlot = &vtable[6];
    g_setCVarBoolSlot = &vtable[7];
    g_setCVarIntegerSlot = &vtable[8];
    g_originalSetCVarString =
        reinterpret_cast<SetCVarStringFn>(*g_setCVarStringSlot);
    g_originalSetCVarBool =
        reinterpret_cast<SetCVarBoolFn>(*g_setCVarBoolSlot);
    g_originalSetCVarInteger =
        reinterpret_cast<SetCVarIntegerFn>(*g_setCVarIntegerSlot);
    if (g_originalSetCVarString == nullptr || g_originalSetCVarBool == nullptr ||
        g_originalSetCVarInteger == nullptr) {
        error = "the validated idCVarSystem vtable has null setter slots";
        return false;
    }
    const bool stringInstalled = WriteVtableSlot(
        g_setCVarStringSlot, reinterpret_cast<void*>(&HookedSetCVarString));
    const bool boolInstalled = stringInstalled &&
        WriteVtableSlot(g_setCVarBoolSlot,
                        reinterpret_cast<void*>(&HookedSetCVarBool));
    const bool integerInstalled = boolInstalled &&
        WriteVtableSlot(g_setCVarIntegerSlot,
                        reinterpret_cast<void*>(&HookedSetCVarInteger));
    if (!integerInstalled) {
        if (boolInstalled) {
            WriteVtableSlot(g_setCVarBoolSlot,
                            reinterpret_cast<void*>(g_originalSetCVarBool));
        }
        if (stringInstalled) {
            WriteVtableSlot(g_setCVarStringSlot,
                            reinterpret_cast<void*>(g_originalSetCVarString));
        }
        error = "one or more idCVarSystem vtable slots could not be patched";
        return false;
    }
    g_displayCvarHooksInstalled = true;
    Log("display: validated static idCVarSystem hooks installed\r\n");
    return true;
}

bool WaitForDisplayOverrides(DWORD timeoutMilliseconds, std::string& error) {
    if (!g_displayCvarHooksInstalled) return true;
    const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
    do {
        const int state =
            g_displayOverrideStartupState.load(std::memory_order_acquire);
        if (state == 2) return true;
        if (state == 3) {
            error = "the first presentation occurred before renderer cvar overrides began; the ASI loader invoked PreyHFR too late";
            return false;
        }
        Sleep(1);
    } while (GetTickCount64() < deadline);
    error = "the engine did not consume renderer cvars before timeout";
    return false;
}

bool RestoreDisplayCvarHooks() {
    if (!g_displayCvarHooksInstalled) return true;
    const auto restore = [](void** slot, void* hook, void* original) {
        if (slot == nullptr) return false;
        if (*slot == original) return true;
        return *slot == hook && WriteVtableSlot(slot, original);
    };
    const bool integerRestored = restore(
        g_setCVarIntegerSlot, reinterpret_cast<void*>(&HookedSetCVarInteger),
        reinterpret_cast<void*>(g_originalSetCVarInteger));
    const bool boolRestored = restore(
        g_setCVarBoolSlot, reinterpret_cast<void*>(&HookedSetCVarBool),
        reinterpret_cast<void*>(g_originalSetCVarBool));
    const bool stringRestored = restore(
        g_setCVarStringSlot, reinterpret_cast<void*>(&HookedSetCVarString),
        reinterpret_cast<void*>(g_originalSetCVarString));
    return integerRestored && boolRestored && stringRestored;
}

void DisableConfiguredRendererCvarOverrides() {
    // Borderless window styling is applied from SwapBuffers and does not
    // depend on the renderer cvar hooks. Keep the requested dimensions as a
    // safety check so a late-loaded ASI may still remove the window chrome
    // when the game is already rendering at the desktop resolution.
    if (!g_borderlessRequested) {
        g_requestedRenderWidth = 0;
        g_requestedRenderHeight = 0;
    }
    g_requestedMode.reset();
    g_requestedFullscreen.reset();
    g_requestedSwapInterval.reset();
    g_requestedCustomWidth.reset();
    g_requestedCustomHeight.reset();
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
        RecordSelectedGameTic(ticNumber);
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
        RecordSelectedGameTic(-1);
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

void TryInstallCalculateRenderViewHook() {
    if ((!g_cameraInterpolationRequested && !g_interpolationTraceRequested) ||
        g_calculateRenderViewHookState != 0) {
        return;
    }
    auto* gameModule =
        reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"gamex86.dll"));
    if (gameModule == nullptr) return;

    auto* target = gameModule + kCalculateRenderViewRva;
    if (std::memcmp(target, kCalculateRenderViewPrologue.data(),
                    kCalculateRenderViewPrologue.size()) != 0) {
        Log("error: idPlayer::CalculateRenderView prologue mismatch; "
            "upstream camera snapshots unavailable; using SingleView "
            "fallback\r\n");
        g_calculateRenderViewHookState = 2;
        return;
    }

    constexpr std::size_t trampolineSize =
        kCalculateRenderViewStolenBytes + 5;
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        Log("error: could not allocate CalculateRenderView trampoline; "
            "upstream camera snapshots unavailable; using SingleView "
            "fallback\r\n");
        g_calculateRenderViewHookState = 2;
        return;
    }
    std::memcpy(g_calculateRenderViewOriginal.data(), target,
                g_calculateRenderViewOriginal.size());
    std::memcpy(trampoline, target, kCalculateRenderViewStolenBytes);
    EncodeRelativeJump(trampoline + kCalculateRenderViewStolenBytes,
                       trampoline + kCalculateRenderViewStolenBytes,
                       target + kCalculateRenderViewStolenBytes);

    std::array<std::uint8_t, kCalculateRenderViewStolenBytes> detour{};
    detour.fill(0x90);
    EncodeRelativeJump(detour.data(), target,
                       reinterpret_cast<const void*>(
                           &HookedCalculateRenderView));
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, detour.size(), PAGE_EXECUTE_READWRITE,
                        &oldProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        Log("error: could not make CalculateRenderView writable; "
            "upstream camera snapshots unavailable; using SingleView "
            "fallback\r\n");
        g_calculateRenderViewHookState = 2;
        return;
    }

    g_calculateRenderViewTarget = target;
    g_calculateRenderViewTrampoline = trampoline;
    g_originalCalculateRenderView =
        reinterpret_cast<CalculateRenderViewFn>(trampoline);
    std::memcpy(target, detour.data(), detour.size());
    FlushInstructionCache(GetCurrentProcess(), target, detour.size());
    DWORD ignored = 0;
    VirtualProtect(target, detour.size(), oldProtection, &ignored);
    if (std::memcmp(target, detour.data(), detour.size()) != 0) {
        DWORD restoreProtection = 0;
        if (VirtualProtect(target, g_calculateRenderViewOriginal.size(),
                           PAGE_EXECUTE_READWRITE, &restoreProtection)) {
            std::memcpy(target, g_calculateRenderViewOriginal.data(),
                        g_calculateRenderViewOriginal.size());
            FlushInstructionCache(GetCurrentProcess(), target,
                                  g_calculateRenderViewOriginal.size());
            VirtualProtect(target, g_calculateRenderViewOriginal.size(),
                           restoreProtection, &ignored);
        }
        g_calculateRenderViewTarget = nullptr;
        g_calculateRenderViewTrampoline = nullptr;
        g_originalCalculateRenderView = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_calculateRenderViewHookState = 2;
        Log("error: CalculateRenderView detour verification failed; "
            "upstream camera snapshots unavailable; using SingleView "
            "fallback\r\n");
        return;
    }
    g_calculateRenderViewHookState = 1;
    Log("camera: idPlayer::CalculateRenderView producer queue installed at "
        "RVA 0x00087600\r\n");
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

bool RestoreMainImport(IMAGE_THUNK_DATA32* thunk, const void* replacement,
                       const void* original) {
    if (thunk == nullptr || replacement == nullptr || original == nullptr) {
        return false;
    }
    if (thunk->u1.Function == reinterpret_cast<std::uintptr_t>(original)) {
        return true;
    }
    if (thunk->u1.Function != reinterpret_cast<std::uintptr_t>(replacement)) {
        return false;
    }
    DWORD oldProtection = 0;
    if (!VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function),
                        PAGE_READWRITE, &oldProtection)) {
        return false;
    }
    thunk->u1.Function = reinterpret_cast<std::uintptr_t>(original);
    DWORD ignored = 0;
    VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function),
                   oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &thunk->u1.Function,
                          sizeof(thunk->u1.Function));
    return thunk->u1.Function == reinterpret_cast<std::uintptr_t>(original);
}

HANDLE WINAPI HookedCreateWaitableTimerA(
    LPSECURITY_ATTRIBUTES timerAttributes, BOOL manualReset,
    LPCSTR timerName) {
    const HANDLE timer = g_originalCreateWaitableTimerA(
        timerAttributes, manualReset, timerName);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    char line[320]{};
    const int length = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "async_timer_setup: operation=create qpc=%lld handle=0x%08lx "
        "manual_reset=%d named=%d error=%lu\r\n",
        static_cast<long long>(now.QuadPart),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(timer)),
        manualReset ? 1 : 0, timerName != nullptr ? 1 : 0,
        timer == nullptr ? GetLastError() : ERROR_SUCCESS);
    if (length > 0) Log(std::string(line, static_cast<std::size_t>(length)));
    return timer;
}

BOOL WINAPI HookedSetWaitableTimer(HANDLE timer,
                                   const LARGE_INTEGER* dueTime,
                                   LONG periodMilliseconds,
                                   PTIMERAPCROUTINE completionRoutine,
                                   LPVOID completionArgument,
                                   BOOL resume) {
    const BOOL result = g_originalSetWaitableTimer(
        timer, dueTime, periodMilliseconds, completionRoutine,
        completionArgument, resume);
    const LONGLONG dueTime100ns = dueTime != nullptr ? dueTime->QuadPart : 0;
    if (result && periodMilliseconds == kNativeTicMilliseconds &&
        dueTime != nullptr && dueTime100ns == 0) {
        g_asyncTimerHandle.store(timer, std::memory_order_release);
        g_asyncClockPreviousWakeQpc = 0;
        g_asyncTimerThreadId.store(0, std::memory_order_relaxed);
        g_asyncClockStabilized.store(false, std::memory_order_release);
        g_asyncTimerConfirmed.store(true, std::memory_order_release);
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    char line[448]{};
    const int length = _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "async_timer_setup: operation=set qpc=%lld handle=0x%08lx "
        "due_100ns=%lld period_ms=%ld completion=%d resume=%d result=%d "
        "error=%lu selected=%d\r\n",
        static_cast<long long>(now.QuadPart),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(timer)),
        static_cast<long long>(dueTime100ns), periodMilliseconds,
        completionRoutine != nullptr ? 1 : 0, resume ? 1 : 0,
        result ? 1 : 0, result ? ERROR_SUCCESS : GetLastError(),
        result && periodMilliseconds == kNativeTicMilliseconds &&
                dueTime != nullptr && dueTime100ns == 0
            ? 1
            : 0);
    if (length > 0) Log(std::string(line, static_cast<std::size_t>(length)));
    return result;
}

DWORD WINAPI HookedWaitForSingleObject(HANDLE object,
                                       DWORD timeoutMilliseconds) {
    HANDLE selected = g_asyncTimerHandle.load(std::memory_order_acquire);
    const bool discoveryCandidate = selected == nullptr &&
        timeoutMilliseconds == 100;
    const bool traceCandidate = object == selected || discoveryCandidate;
    if (!traceCandidate) {
        return g_originalWaitForSingleObject(object, timeoutMilliseconds);
    }

    LARGE_INTEGER begin{};
    QueryPerformanceCounter(&begin);
    const std::int32_t comTicAtWaitEntry = ReadComTicNumber();
    const DWORD result =
        g_originalWaitForSingleObject(object, timeoutMilliseconds);
    LARGE_INTEGER wake{};
    QueryPerformanceCounter(&wake);
    const DWORD wakeMilliseconds = timeGetTime();

    bool discoveredByWait = false;
    if (discoveryCandidate && result == WAIT_OBJECT_0) {
        HANDLE expected = nullptr;
        discoveredByWait = g_asyncTimerHandle.compare_exchange_strong(
            expected, object, std::memory_order_acq_rel,
            std::memory_order_acquire);
        selected = g_asyncTimerHandle.load(std::memory_order_acquire);
    }
    if (object != selected) return result;

    const DWORD threadId = GetCurrentThreadId();
    std::uint32_t stabilizedAdvanceTics = 0;
    if (result == WAIT_OBJECT_0 &&
        g_asyncTimerConfirmed.load(std::memory_order_acquire) &&
        g_asyncTimeGetHookInstalled.load(std::memory_order_acquire)) {
        stabilizedAdvanceTics = UpdateStabilizedAsyncClock(
            wake.QuadPart, wakeMilliseconds, threadId);
    }

    const std::uint64_t sequence =
        g_asyncTimerTracePublished.load(std::memory_order_relaxed) + 1;
    const TimerResolutionSnapshot resolution =
        sequence == 1 || (sequence & 0xffu) == 0
        ? QueryTimerResolution()
        : TimerResolutionSnapshot{};
    AsyncTimerTraceRecord record;
    record.sequence = sequence;
    record.waitBeginQpc = begin.QuadPart;
    record.wakeQpc = wake.QuadPart;
    record.previousWakeQpc = g_asyncTimerPreviousWakeQpc;
    record.wakeMilliseconds = wakeMilliseconds;
    record.previousWakeMilliseconds = g_asyncTimerPreviousWakeMilliseconds;
    record.waitTimeoutMilliseconds = timeoutMilliseconds;
    record.waitResult = result;
    record.threadId = threadId;
    record.processorNumber = GetCurrentProcessorNumber();
    record.threadPriority = GetThreadPriority(GetCurrentThread());
    record.comTicAtWaitEntry = comTicAtWaitEntry;
    record.comTicAtWake = ReadComTicNumber();
    record.previousWakeComTic = g_asyncTimerPreviousWakeComTic;
    record.timerResolution100ns = resolution.current100ns;
    record.syntheticMilliseconds =
        g_asyncSyntheticMilliseconds.load(std::memory_order_relaxed);
    record.stabilizedAdvanceTics = stabilizedAdvanceTics;
    record.timerResolutionValid = resolution.valid;
    record.discoveredByWait = discoveredByWait;
    record.clockStabilized =
        g_asyncClockStabilized.load(std::memory_order_acquire);
    PublishAsyncTimerTrace(record);

    g_asyncTimerPreviousWakeQpc = wake.QuadPart;
    g_asyncTimerPreviousWakeMilliseconds = wakeMilliseconds;
    g_asyncTimerPreviousWakeComTic = record.comTicAtWake;
    return result;
}

DWORD WINAPI HookedTimeGetTime() {
    const DWORD realMilliseconds = g_originalTimeGetTime();
    if (!g_asyncClockStabilized.load(std::memory_order_acquire) ||
        GetCurrentThreadId() !=
            g_asyncTimerThreadId.load(std::memory_order_relaxed)) {
        return realMilliseconds;
    }
    return g_asyncSyntheticMilliseconds.load(std::memory_order_relaxed);
}

MMRESULT WINAPI HookedTimeBeginPeriod(UINT periodMilliseconds) {
    const TimerResolutionSnapshot before = QueryTimerResolution();
    const MMRESULT result = g_originalTimeBeginPeriod(periodMilliseconds);
    const TimerResolutionSnapshot after = QueryTimerResolution();
    if (periodMilliseconds == 1) {
        LogTimerResolutionEvent("engine", result, before, after);
    }
    return result;
}

bool InstallAsyncTimerInstrumentation() {
    if (!g_cameraInterpolationRequested && !g_interpolationTraceRequested) {
        return true;
    }
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr) {
        g_ntQueryTimerResolution =
            reinterpret_cast<NtQueryTimerResolutionFn>(
                GetProcAddress(ntdll, "NtQueryTimerResolution"));
    }

    bool complete = true;
    const auto install = [&](const char* moduleName, const char* functionName,
                             void* replacement, auto& original,
                             IMAGE_THUNK_DATA32*& thunk) {
        void* originalAddress = nullptr;
        IMAGE_THUNK_DATA32* installedThunk = nullptr;
        if (!HookMainImport(moduleName, functionName, replacement,
                            &originalAddress, &installedThunk)) {
            complete = false;
            return;
        }
        original = reinterpret_cast<std::decay_t<decltype(original)>>(
            originalAddress);
        thunk = installedThunk;
    };
    install("KERNEL32.dll", "CreateWaitableTimerA",
            reinterpret_cast<void*>(&HookedCreateWaitableTimerA),
            g_originalCreateWaitableTimerA, g_createWaitableTimerThunk);
    install("KERNEL32.dll", "SetWaitableTimer",
            reinterpret_cast<void*>(&HookedSetWaitableTimer),
            g_originalSetWaitableTimer, g_setWaitableTimerThunk);
    install("KERNEL32.dll", "WaitForSingleObject",
            reinterpret_cast<void*>(&HookedWaitForSingleObject),
            g_originalWaitForSingleObject, g_waitForSingleObjectThunk);
    install("WINMM.dll", "timeBeginPeriod",
            reinterpret_cast<void*>(&HookedTimeBeginPeriod),
            g_originalTimeBeginPeriod, g_timeBeginPeriodThunk);
    install("WINMM.dll", "timeGetTime",
            reinterpret_cast<void*>(&HookedTimeGetTime),
            g_originalTimeGetTime, g_timeGetTimeThunk);
    g_asyncTimeGetHookInstalled.store(
        g_timeGetTimeThunk != nullptr && g_originalTimeGetTime != nullptr,
        std::memory_order_release);
    Log(complete
            ? "async_timer: upstream scheduler stabilization and instrumentation installed\r\n"
            : "warning: async timer stabilization/instrumentation is partial; one or more imports were unavailable\r\n");
    return complete;
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
    FlushAsyncTimerTrace();
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
    if (!g_interpolationTraceBuffer.empty()) {
        Log(g_interpolationTraceBuffer);
        g_interpolationTraceBuffer.clear();
    }
    if (length > 0) Log(std::string(buffer, static_cast<std::size_t>(length)));
    g_reportStart = now.QuadPart;
    g_reportFrames = 0;
    g_intervalSampleCount = 0;
    g_worstIntervalMilliseconds = 0.0;
    g_viewCounters = {};
}

void LogActiveSwapIntervalOnce() {
    if (g_swapIntervalProbeState.load(std::memory_order_acquire) != 0) return;

    HMODULE openGl = GetModuleHandleW(L"opengl32.dll");
    if (openGl == nullptr) return;
    const auto getCurrentContext = reinterpret_cast<WglGetCurrentContextFn>(
        GetProcAddress(openGl, "wglGetCurrentContext"));
    const auto getProcAddress = reinterpret_cast<WglGetProcAddressFn>(
        GetProcAddress(openGl, "wglGetProcAddress"));
    if (getCurrentContext == nullptr || getProcAddress == nullptr ||
        getCurrentContext() == nullptr) {
        return;
    }

    const PROC raw = getProcAddress("wglGetSwapIntervalEXT");
    const auto rawValue = reinterpret_cast<std::uintptr_t>(raw);
    if (raw == nullptr || rawValue <= 3 || rawValue == UINTPTR_MAX) {
        int expected = 0;
        if (g_swapIntervalProbeState.compare_exchange_strong(
                expected, 2, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            Log("display: WGL_EXT_swap_control query unavailable; VRR state is driver-controlled\r\n");
        }
        return;
    }

    const auto getSwapInterval =
        reinterpret_cast<WglGetSwapIntervalExtFn>(raw);
    const int interval = getSwapInterval();
    int expected = 0;
    if (g_swapIntervalProbeState.compare_exchange_strong(
            expected, 1, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        char buffer[192]{};
        const int length = _snprintf_s(
            buffer, sizeof(buffer), _TRUNCATE,
            "display: active WGL swap interval=%d; VRR engagement remains driver-controlled\r\n",
            interval);
        if (length > 0) {
            Log(std::string(buffer, static_cast<std::size_t>(length)));
        }
    }
}

BOOL WINAPI HookedSwapBuffers(HDC deviceContext) {
    int waitingForDisplayOverrides = 0;
    g_displayOverrideStartupState.compare_exchange_strong(
        waitingForDisplayOverrides, 3, std::memory_order_acq_rel,
        std::memory_order_acquire);
    if (!g_patchActive.load(std::memory_order_acquire)) {
        return g_originalSwapBuffers(deviceContext);
    }
    DWORD expectedThread = 0;
    g_presentationThreadId.compare_exchange_strong(
        expectedThread, GetCurrentThreadId(), std::memory_order_release,
        std::memory_order_relaxed);
    TryInstallDetermineViewAnglesHook();
    TryInstallCalculateRenderViewHook();
    TryInstallViewHook();
    TryInstallRenderEntityHooks();
    TryInstallMouseMoveHook();
    TryInstallUsercmdHooks();
    TryApplyBorderlessWindow(deviceContext);
    LogActiveSwapIntervalOnce();
    PollTimelineResetKey();
    WaitForDeadline();
    const BOOL result = g_originalSwapBuffers(deviceContext);
    ReportFrame();
    return result;
}

bool OpenLog(const fs::path& path) {
    g_log = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_log != INVALID_HANDLE_VALUE;
}

void ConfigureFromEnvironment() {
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
    g_effectInterpolationRequested =
        ReadEnvironmentFlag(L"PREYHFR_EFFECT_INTERP") &&
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
    g_bufferedTwoTicInterpolationRequested =
        ReadEnvironmentFlag(L"PREYHFR_BUFFERED_TWO_TIC_INTERPOLATION") &&
        g_continuousSnapshotTimingRequested;
    g_interpolationTraceRequested =
        ReadEnvironmentFlag(L"PREYHFR_INTERPOLATION_TRACE") &&
        g_cameraInterpolationRequested;
    if (g_interpolationTraceRequested) {
        g_interpolationTraceBuffer.reserve(512 * 1024);
    }
    g_timelineResetVirtualKey =
        ReadEnvironmentUnsigned(L"PREYHFR_TIMELINE_RESET_KEY", 255);
    g_borderlessRequested = ReadEnvironmentFlag(L"PREYHFR_BORDERLESS");
    g_requestedRenderWidth =
        ReadEnvironmentUnsigned(L"PREYHFR_RENDER_WIDTH", 16384);
    g_requestedRenderHeight =
        ReadEnvironmentUnsigned(L"PREYHFR_RENDER_HEIGHT", 16384);
}

void ConfigureFromConfig(const preyhfr::Config& config) {
    QueryPerformanceFrequency(&g_frequency);
    g_viewLoggingRequested = config.viewLog;
    g_cameraInterpolationRequested = config.cameraInterpolation;
    g_viewModelInterpolationRequested = config.viewModelInterpolation;
    g_viewModelAnimationInterpolationRequested =
        config.viewModelAnimationInterpolation && config.cameraInterpolation;
    g_worldInterpolationRequested =
        config.worldInterpolation && config.cameraInterpolation;
    g_worldAnimationInterpolationRequested =
        config.worldAnimationInterpolation && config.cameraInterpolation;
    g_effectInterpolationRequested =
        config.effectInterpolation && config.cameraInterpolation;
    g_maximumWorldEntityStep = config.maximumWorldEntityDistance;
    g_maximumWorldEntityAngleDegrees = config.maximumWorldEntityAngle;
    g_mouseInterpolationRequested =
        config.mouseInterpolation && config.cameraInterpolation;
    g_continuousSnapshotTimingRequested = config.continuousSnapshotTiming;
    g_multiTicEntityAlignmentRequested =
        config.multiTicEntityAlignment && config.continuousSnapshotTiming;
    g_overdueSnapshotFallbackRequested =
        config.overdueSnapshotFallback && g_multiTicEntityAlignmentRequested;
    g_bufferedTwoTicInterpolationRequested =
        config.bufferedTwoTicInterpolation &&
        g_continuousSnapshotTimingRequested;
    g_interpolationTraceRequested =
        config.interpolationTrace && config.cameraInterpolation;
    if (g_interpolationTraceRequested) {
        g_interpolationTraceBuffer.reserve(512 * 1024);
    }
    g_timelineResetVirtualKey = config.timelineResetVirtualKey;
    g_borderlessRequested = config.borderless;
    g_requestedRenderWidth = config.resolution ? config.resolution->width : 0;
    g_requestedRenderHeight = config.resolution ? config.resolution->height : 0;
    g_requestedMode.reset();
    g_requestedFullscreen.reset();
    g_requestedSwapInterval.reset();
    g_requestedCustomWidth.reset();
    g_requestedCustomHeight.reset();
    g_displayOverrideStartupState.store(0, std::memory_order_release);
    if (config.resolution) {
        g_requestedMode = -1;
        g_requestedCustomWidth = static_cast<int>(config.resolution->width);
        g_requestedCustomHeight = static_cast<int>(config.resolution->height);
    }
    if (config.borderless ||
        config.displayMode == preyhfr::DisplayMode::Windowed) {
        g_requestedFullscreen = 0;
    } else if (config.displayMode == preyhfr::DisplayMode::Exclusive) {
        g_requestedFullscreen = 1;
    }
    if (config.vSync == preyhfr::VSyncMode::Off) {
        g_requestedSwapInterval = 0;
    } else if (config.vSync == preyhfr::VSyncMode::On) {
        g_requestedSwapInterval = 1;
    }
}

bool InstallConfiguredHooks(unsigned int cap) {
    if (g_borderlessRequested) {
        // Establish physical-pixel coordinates before the retail engine creates
        // its OpenGL window. This keeps custom renderer dimensions aligned with
        // the monitor rectangle on scaled Windows desktops.
        SetProcessDPIAware();
    }
    InstallAsyncTimerInstrumentation();
    if (cap > 0 && g_frequency.QuadPart > 0) {
        g_periodCounts = std::max<std::int64_t>(1, g_frequency.QuadPart / cap);
        const TimerResolutionSnapshot before = QueryTimerResolution();
        const MMRESULT result = timeBeginPeriod(1);
        const TimerResolutionSnapshot after = QueryTimerResolution();
        g_timerResolutionRaised = result == TIMERR_NOERROR;
        if (g_interpolationTraceRequested) {
            LogTimerResolutionEvent("preyhfr", result, before, after);
        }
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
    char buffer[896]{};
    const int length = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                                   "hook: GDI32!SwapBuffers installed; cap=%u Hz; "
                                   "view_log=%s; camera_interp=%s; "
                                   "viewmodel_interp=%s; viewmodel_anim_interp=%s; "
                                   "world_interp=%s; world_anim_interp=%s; "
                                   "effect_interp=%s; "
                                   "world_max_distance=%.3f; world_max_angle=%.3f; "
                                   "mouse_interp=%s; continuous_snapshot_timing=%s; "
                                   "multi_tic_entity_alignment=%s; "
                                   "overdue_snapshot_fallback=%s; "
                                   "buffered_two_tic_interpolation=%s; "
                                   "interpolation_trace=%s; "
                                   "timeline_reset_vk=0x%02x; "
                                   "borderless=%s; "
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
                                   g_effectInterpolationRequested
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
                                   g_bufferedTwoTicInterpolationRequested
                                       ? "enabled"
                                       : "disabled",
                                   g_interpolationTraceRequested
                                       ? "enabled"
                                       : "disabled",
                                   g_timelineResetVirtualKey,
                                   g_borderlessRequested ? "enabled" : "disabled",
                                   g_requestedRenderWidth,
                                   g_requestedRenderHeight);
    if (length > 0) Log(std::string(buffer, static_cast<std::size_t>(length)));
    if (g_interpolationTraceRequested) {
        Log("interp_trace_schema: reset_mask_bits=initial:0x01,"
            "same_time_stall:0x02,time_delta:0x04,view_id:0x08,"
            "fov:0x10,stall:0x20,origin:0x40,axis:0x80,"
            "clock_debt:0x100; "
            "one interp_trace record is emitted per gameplay presentation\r\n");
        Log("entity_interp_trace_schema: one record per producer interval; "
            "entry fields h=render handle,e=entity number,j=joint count,"
            "a=animation pair/changed/interpolatable/applied,"
            "r=root pair/changed/interpolatable/applied; at most 24 entries\r\n");
        Log("async_timer_trace_schema: one record per 16 ms async timer wake; "
            "previous_callback_us covers engine Async work and lock delay; "
            "previous_emitted_tics is observed at the following wait entry\r\n");
    }
    return true;
}

bool InitializeFromEnvironment() {
    const std::wstring logPath = ReadEnvironment(L"PREYHFR_LOG");
    if (!logPath.empty()) OpenLog(logPath);
    ConfigureFromEnvironment();
    return InstallConfiguredHooks(ReadFrameCap());
}

void Shutdown();

void LogBootstrapFailure(const char* stage, const std::string& detail) {
    std::string message = "bootstrap: ";
    message += stage;
    message += " failed: ";
    message += detail;
    message += "\r\n";
    Log(message);
}

DWORD WINAPI BootstrapAsi(LPVOID) {
    HMODULE pinnedModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(g_pluginModule), &pinnedModule);

    const auto pluginPath = preyhfr::ModulePath(g_pluginModule);
    if (!pluginPath) {
        g_initializationComplete.store(true, std::memory_order_release);
        return 1;
    }
    const fs::path pluginDirectory = pluginPath->parent_path();
    OpenLog(pluginDirectory / L"PreyHFR.log");
    Log("bootstrap: PreyHFR " PREYHFR_VERSION " ASI discovered\r\n");

    preyhfr::Config config;
    std::string error;
    const fs::path configPath = pluginDirectory / L"PreyHFR.ini";
    if (!preyhfr::LoadConfig(configPath, config, error) ||
        !preyhfr::ResolveDesktopConfig(config, error)) {
        LogBootstrapFailure("configuration", error);
        g_initializationComplete.store(true, std::memory_order_release);
        return 1;
    }
    Log("bootstrap: configuration valid: " + preyhfr::DescribeConfig(config) +
        "\r\n");
    if (!config.patchEnabled) {
        Log("bootstrap: patch disabled by configuration; no hooks or patches installed\r\n");
        g_initializationComplete.store(true, std::memory_order_release);
        return 0;
    }

    const auto executablePath = preyhfr::ModulePath(nullptr);
    if (!executablePath ||
        _wcsicmp(executablePath->filename().c_str(), L"prey.exe") != 0) {
        LogBootstrapFailure("host validation",
                            "the host executable is not prey.exe");
        g_initializationComplete.store(true, std::memory_order_release);
        return 1;
    }
    const auto executableHash = preyhfr::Sha256File(*executablePath);
    if (!executableHash || *executableHash != preyhfr::kSupportedExeSha256) {
        LogBootstrapFailure(
            "host validation",
            "unsupported prey.exe SHA-256 " +
                executableHash.value_or("unavailable"));
        g_initializationComplete.store(true, std::memory_order_release);
        return 1;
    }
    ConfigureFromConfig(config);
    g_retailValidationState.store(0, std::memory_order_release);
    g_retailValidationEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_retailValidationEvent == nullptr) {
        LogBootstrapFailure("validation synchronization",
                            "could not create the retail-validation event");
        g_initializationComplete.store(true, std::memory_order_release);
        return 1;
    }

    bool displayHooksAvailable = InstallDisplayCvarHooks(30'000, error);
    if (!displayHooksAvailable) {
        Log("warning: display cvar overrides unavailable: " + error +
            "; continuing with game-configured display settings\r\n");
        DisableConfiguredRendererCvarOverrides();
    }
    if (!InstallConfiguredHooks(config.framesPerSecond)) {
        g_retailValidationState.store(2, std::memory_order_release);
        SetEvent(g_retailValidationEvent);
        LogBootstrapFailure("early hook installation",
                            "required main-module import hook was unavailable");
        Shutdown();
        g_initializationComplete.store(true, std::memory_order_release);
        return 1;
    }
    Log("bootstrap: early hooks installed in inactive state\r\n");

    // A hooked engine cvar write waits on this validation while the bootstrap
    // hashes the game module. This prevents renderer startup from outrunning
    // validation without applying settings to an unsupported game DLL.
    const fs::path gameDllPath =
        executablePath->parent_path() / L"base" / L"gamex86.dll";
    const auto gameDllHash = preyhfr::Sha256File(gameDllPath);
    if (!gameDllHash || *gameDllHash != preyhfr::kSupportedGameDllSha256) {
        g_retailValidationState.store(2, std::memory_order_release);
        SetEvent(g_retailValidationEvent);
        LogBootstrapFailure(
            "game-module validation",
            "unsupported base/gamex86.dll SHA-256 " +
                gameDllHash.value_or("unavailable"));
        Shutdown();
        g_initializationComplete.store(true, std::memory_order_release);
        return 1;
    }
    g_retailValidationState.store(1, std::memory_order_release);
    SetEvent(g_retailValidationEvent);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    Log("bootstrap: supported retail executable and game DLL validated\r\n");

    if (displayHooksAvailable && !WaitForDisplayOverrides(30'000, error)) {
        const bool restored = RestoreDisplayCvarHooks();
        Log("warning: display cvar override timing unavailable: " + error +
            (restored ? "; cvar hooks restored" :
                        "; one or more cvar hooks could not be restored") +
            (g_borderlessRequested
                 ? "; continuing with game-configured renderer settings and runtime borderless styling\r\n"
                 : "; continuing with game-configured display settings\r\n"));
        DisableConfiguredRendererCvarOverrides();
    }

    preyhfr::WaitGateResult waitGate;
    if (!g_waitGatePatch.WaitAndApply(30'000, waitGate, error)) {
        LogBootstrapFailure("render-wait patch", error);
        Shutdown();
        g_initializationComplete.store(true, std::memory_order_release);
        return 1;
    }
    g_comTicNumberAddress.store(waitGate.comTicAddress,
                                std::memory_order_release);
    char waitGateMessage[320]{};
    const int waitGateLength = _snprintf_s(
        waitGateMessage, sizeof(waitGateMessage), _TRUNCATE,
        "bootstrap: timing gate %s at VA 0x%08llx; com_ticNumber=0x%08llx; "
        "com_fixedTic=0x%08llx\r\n",
        waitGate.alreadyPatched ? "was already patched" : "patched",
        static_cast<unsigned long long>(waitGate.address),
        static_cast<unsigned long long>(waitGate.comTicAddress),
        static_cast<unsigned long long>(waitGate.fixedTicObjectPointerAddress));
    if (waitGateLength > 0) {
        Log(std::string(waitGateMessage,
                        static_cast<std::size_t>(waitGateLength)));
    }

    g_patchActive.store(true, std::memory_order_release);
    Log("bootstrap: ASI activation complete\r\n");
    g_initializationComplete.store(true, std::memory_order_release);
    return 0;
}

void Shutdown() {
    g_patchActive.store(false, std::memory_order_release);
    g_asyncClockStabilized.store(false, std::memory_order_release);
    const bool waitGateWasOwned = g_waitGatePatch.ownsPatch();
    FlushAsyncTimerTrace();
    if (!g_interpolationTraceBuffer.empty()) {
        Log(g_interpolationTraceBuffer);
        g_interpolationTraceBuffer.clear();
    }
    const auto restoreSlot = [](void** slot, void* hook, void* original) {
        if (slot == nullptr) return false;
        if (*slot == original) return true;
        return *slot == hook && WriteVtableSlot(slot, original);
    };
    bool displayCvarHooksRestored = false;
    if (g_displayCvarHooksInstalled) {
        displayCvarHooksRestored = RestoreDisplayCvarHooks();
    }
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
    bool calculateRenderViewHookRestored = false;
    bool determineViewAnglesHookRestored = false;
    bool viewHookRestored = false;
    bool gameModuleUnloadedFirst = false;
    auto* loadedGameModule =
        reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"gamex86.dll"));
    if (g_calculateRenderViewHookState == 1 &&
        g_calculateRenderViewTarget != nullptr &&
        g_calculateRenderViewTrampoline != nullptr &&
        loadedGameModule != nullptr &&
        g_calculateRenderViewTarget ==
            loadedGameModule + kCalculateRenderViewRva) {
        std::array<std::uint8_t, kCalculateRenderViewStolenBytes> detour{};
        detour.fill(0x90);
        EncodeRelativeJump(detour.data(), g_calculateRenderViewTarget,
                           reinterpret_cast<const void*>(
                               &HookedCalculateRenderView));
        if (std::memcmp(g_calculateRenderViewTarget, detour.data(),
                        detour.size()) == 0) {
            DWORD oldProtection = 0;
            if (VirtualProtect(g_calculateRenderViewTarget,
                               g_calculateRenderViewOriginal.size(),
                               PAGE_EXECUTE_READWRITE, &oldProtection)) {
                std::memcpy(g_calculateRenderViewTarget,
                            g_calculateRenderViewOriginal.data(),
                            g_calculateRenderViewOriginal.size());
                FlushInstructionCache(
                    GetCurrentProcess(), g_calculateRenderViewTarget,
                    g_calculateRenderViewOriginal.size());
                DWORD ignored = 0;
                VirtualProtect(g_calculateRenderViewTarget,
                               g_calculateRenderViewOriginal.size(),
                               oldProtection, &ignored);
                calculateRenderViewHookRestored =
                    std::memcmp(g_calculateRenderViewTarget,
                                g_calculateRenderViewOriginal.data(),
                                g_calculateRenderViewOriginal.size()) == 0;
            }
        }
        if (calculateRenderViewHookRestored) {
            VirtualFree(g_calculateRenderViewTrampoline, 0, MEM_RELEASE);
            g_calculateRenderViewTrampoline = nullptr;
            g_originalCalculateRenderView = nullptr;
            g_calculateRenderViewTarget = nullptr;
        }
    } else if (g_calculateRenderViewHookState == 1 &&
               loadedGameModule == nullptr) {
        gameModuleUnloadedFirst = true;
        if (g_calculateRenderViewTrampoline != nullptr) {
            VirtualFree(g_calculateRenderViewTrampoline, 0, MEM_RELEASE);
            g_calculateRenderViewTrampoline = nullptr;
        }
        g_originalCalculateRenderView = nullptr;
        g_calculateRenderViewTarget = nullptr;
    }
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
    const auto restoreOptionalImport = [](IMAGE_THUNK_DATA32* thunk,
                                          const void* replacement,
                                          const void* original) {
        return thunk == nullptr ||
            RestoreMainImport(thunk, replacement, original);
    };
    const bool asyncTimerWaitRestored = restoreOptionalImport(
        g_waitForSingleObjectThunk,
        reinterpret_cast<const void*>(&HookedWaitForSingleObject),
        reinterpret_cast<const void*>(g_originalWaitForSingleObject));
    const bool asyncTimerSetRestored = restoreOptionalImport(
        g_setWaitableTimerThunk,
        reinterpret_cast<const void*>(&HookedSetWaitableTimer),
        reinterpret_cast<const void*>(g_originalSetWaitableTimer));
    const bool asyncTimerCreateRestored = restoreOptionalImport(
        g_createWaitableTimerThunk,
        reinterpret_cast<const void*>(&HookedCreateWaitableTimerA),
        reinterpret_cast<const void*>(g_originalCreateWaitableTimerA));
    const bool engineTimeBeginPeriodRestored = restoreOptionalImport(
        g_timeBeginPeriodThunk,
        reinterpret_cast<const void*>(&HookedTimeBeginPeriod),
        reinterpret_cast<const void*>(g_originalTimeBeginPeriod));
    const bool engineTimeGetTimeRestored = restoreOptionalImport(
        g_timeGetTimeThunk,
        reinterpret_cast<const void*>(&HookedTimeGetTime),
        reinterpret_cast<const void*>(g_originalTimeGetTime));
    const bool asyncTimerImportsRestored = asyncTimerWaitRestored &&
        asyncTimerSetRestored && asyncTimerCreateRestored &&
        engineTimeBeginPeriodRestored && engineTimeGetTimeRestored;
    g_asyncTimeGetHookInstalled.store(false, std::memory_order_release);

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
    const bool waitGateRestored = g_waitGatePatch.Restore();
    if (g_timerResolutionRaised) timeEndPeriod(1);
    if (g_log != INVALID_HANDLE_VALUE) {
        FlushAsyncTimerTrace();
        if (!g_interpolationTraceBuffer.empty()) {
            Log(g_interpolationTraceBuffer);
            g_interpolationTraceBuffer.clear();
        }
        if (g_displayCvarHooksInstalled) {
            Log(displayCvarHooksRestored
                    ? "display: idCVarSystem override hooks restored\r\n"
                    : "warning: display cvar hooks were not restored\r\n");
        }
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
        if (g_cameraInterpolationRequested || g_interpolationTraceRequested) {
            Log(calculateRenderViewHookRestored
                    ? "camera: CalculateRenderView producer queue restored\r\n"
                    : gameModuleUnloadedFirst
                          ? "camera: game module unloaded before producer "
                            "observer; detour no longer live\r\n"
                          : g_calculateRenderViewHookState == 0
                                ? "camera: producer queue was never installed\r\n"
                                : g_calculateRenderViewHookState == 2
                                      ? "camera: producer queue remained "
                                        "disabled after error\r\n"
                                      : "warning: CalculateRenderView producer "
                                        "queue was not restored\r\n");
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
        if (g_interpolationTraceRequested) {
            Log(asyncTimerImportsRestored
                    ? "async_timer: scheduler stabilization/instrumentation restored\r\n"
                    : "warning: one or more async timer stabilization/instrumentation imports were not restored\r\n");
        }
        Log(importRestored ? "hook: GDI32!SwapBuffers import restored\r\n"
                           : "warning: GDI32!SwapBuffers import was not restored\r\n");
        if (waitGateWasOwned) {
            Log(waitGateRestored
                    ? "patch: render-wait gate restored\r\n"
                    : "warning: render-wait gate was not restored\r\n");
        }
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
                kMaximumInterpolatedCameraIntervalMilliseconds, true) ||
            !IsConsecutiveProducedCameraBridge(1000, 1016, 1032) ||
            IsConsecutiveProducedCameraBridge(1000, 1032, 1048) ||
            IsConsecutiveProducedCameraBridge(1000, 1016, 1048)) {
            return false;
        }
        std::array<std::uint8_t, kRenderViewSize> previousView{};
        std::array<std::uint8_t, kRenderViewSize> currentView{};
        std::array<std::uint8_t, kRenderViewSize> interpolatedView{};
        WriteUnaligned(previousView.data(), 80, std::int32_t{1000});
        WriteUnaligned(currentView.data(), 80, std::int32_t{1016});
        WriteUnaligned(previousView.data(), 20, 90.0f);
        WriteUnaligned(previousView.data(), 24, 60.0f);
        WriteUnaligned(currentView.data(), 20, 110.0f);
        WriteUnaligned(currentView.data(), 24, 80.0f);
        const std::uint32_t fovOnlyMask =
            CameraSnapshotDiscontinuityMask(previousView, currentView);
        InterpolateCameraFov(previousView, currentView, 0.25,
                             interpolatedView.data());
        if (fovOnlyMask != kCameraProbeFov ||
            CameraSnapshotRequiresReset(fovOnlyMask) ||
            !close(ReadUnaligned<float>(interpolatedView.data(), 20), 95.0) ||
            !close(ReadUnaligned<float>(interpolatedView.data(), 24), 65.0)) {
            return false;
        }
        WriteUnaligned(currentView.data(), 28, 10000.0f);
        const std::uint32_t teleportMask =
            CameraSnapshotDiscontinuityMask(previousView, currentView);
        if ((teleportMask & kCameraProbeFov) == 0 ||
            (teleportMask & kCameraProbeOrigin) == 0 ||
            !CameraSnapshotRequiresReset(teleportMask)) {
            return false;
        }
        const std::int64_t twoTics = CameraIntervalQpc(
            2 * kNativeTicMilliseconds, frequency);
        const std::int64_t threeTics = CameraIntervalQpc(
            3 * kNativeTicMilliseconds, frequency);
        const std::int64_t capturedCheckpointRecovery =
            CameraIntervalQpc(58, frequency);
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
                   0.5) ||
             !close(CameraRotationInterpolationAlpha(0.75), 0.75) ||
             !close(CameraRotationInterpolationAlpha(1.75), 1.0) ||
            InterpolatedPresentationTime(1000, 1016, -0.5) != 1000 ||
            InterpolatedPresentationTime(1000, 1016, 0.25) != 1004 ||
            InterpolatedPresentationTime(1000, 1016, 1.0) != 1016 ||
            InterpolatedPresentationTime(1000, 1016, 1.75) != 1016 ||
            CameraSnapshotClockDebtExceeded(
                0, kNativeTicMilliseconds,
                kMaximumInterpolatedCameraIntervalMilliseconds,
                threeTics, frequency) ||
            !CameraSnapshotClockDebtExceeded(
                0, kNativeTicMilliseconds,
                kMaximumInterpolatedCameraIntervalMilliseconds,
                threeTics + 1, frequency) ||
            !CameraSnapshotClockDebtExceeded(
                0, kNativeTicMilliseconds,
                kMaximumInterpolatedCameraIntervalMilliseconds,
                capturedCheckpointRecovery, frequency) ||
            CameraSnapshotClockDebtExceeded(
                0, kMaximumInterpolatedCameraIntervalMilliseconds,
                kMaximumInterpolatedCameraIntervalMilliseconds,
                threeTics, frequency) ||
            !CameraSnapshotClockDebtExceeded(
                0, kNativeTicMilliseconds, kNativeTicMilliseconds,
                threeTics, frequency)) {
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
                     pendingAlpha) &&
               close(AnimationInterpolationAlpha(
                         false, 1, 1.75, latestAlpha, pendingAlpha,
                         true, true),
                     1.75) &&
               close(AnimationInterpolationAlpha(
                         false, 2, cameraAlpha, 1.5, pendingAlpha,
                         true, true),
                     1.5);
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
        std::int64_t lastCallQpc = 0;
        bool phaseRecoveryActive = false;
        std::int64_t phaseRecoveryCandidateQpc = 0;
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
                    currentQpc = UpdateCameraSnapshotClock(
                        currentQpc, delta, lastCallQpc, now, frequency,
                        phaseRecoveryActive,
                        phaseRecoveryCandidateQpc).qpc;
                } else {
                    currentQpc = now;
                    synchronized = true;
                    phaseRecoveryActive = false;
                    phaseRecoveryCandidateQpc = 0;
                }
            }
            changedOnPreviousFrame = changed;
            lastCallQpc = now;
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
            // Allow the first snapshot transition to establish the stable
            // presentation phase before checking its increments.
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
        std::int64_t lastCallQpc = 0;
        bool phaseRecoveryActive = false;
        std::int64_t phaseRecoveryCandidateQpc = 0;
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
                    currentQpc = UpdateCameraSnapshotClock(
                        currentQpc, delta, lastCallQpc, now, frequency,
                        phaseRecoveryActive,
                        phaseRecoveryCandidateQpc).qpc;
                } else {
                    currentQpc = now;
                    synchronized = true;
                    phaseRecoveryActive = false;
                    phaseRecoveryCandidateQpc = 0;
                }
            }
            lastCallQpc = now;
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

    const auto testBatchedPhaseExcursion = [] {
        // The retail trace regularly accumulates about one tick of apparent
        // lag across 16 ms samples, then cancels it with a 32 ms camera bridge.
        // That short batching cycle must never start phase recovery.
        struct Sample {
            std::int32_t intervalMilliseconds;
            double observedMidpointMilliseconds;
        };
        constexpr std::array<Sample, 3> samples{{
            {16, 30.6868},
            {16, 46.4000},
            {32, 65.0000},
        }};
        constexpr double halfCallGapMilliseconds = 1.5999;
        std::int64_t currentSnapshotQpc = 0;
        bool phaseRecoveryActive = false;
        std::int64_t phaseRecoveryCandidateQpc = 0;
        bool sawCandidate = false;
        for (const Sample& sample : samples) {
            const std::int64_t midpointQpc = static_cast<std::int64_t>(
                std::llround(sample.observedMidpointMilliseconds *
                             static_cast<double>(frequency) / 1000.0));
            const std::int64_t halfGapQpc = static_cast<std::int64_t>(
                std::llround(halfCallGapMilliseconds *
                             static_cast<double>(frequency) / 1000.0));
            const CameraSnapshotClockUpdate update =
                UpdateCameraSnapshotClock(
                    currentSnapshotQpc, sample.intervalMilliseconds,
                    midpointQpc - halfGapQpc, midpointQpc + halfGapQpc,
                    frequency, phaseRecoveryActive,
                    phaseRecoveryCandidateQpc);
            currentSnapshotQpc = update.qpc;
            sawCandidate = sawCandidate || phaseRecoveryCandidateQpc != 0;
            if (update.recoveryStarted || update.correctionQpc != 0 ||
                phaseRecoveryActive) {
                return false;
            }
        }
        return sawCandidate && phaseRecoveryCandidateQpc == 0;
    };

    const auto testEarlyProducerClamp = [=] {
        // Reproduce the cold-boot trace: after synchronization, the next
        // completed 16 ms sample arrived only 8.5 ms later. A snapshot already
        // in hand cannot have a future presentation boundary.
        const std::int64_t synchronizedQpc = CameraIntervalQpc(10, frequency);
        const std::int64_t earlyProducerQpc = 18'500'000;
        std::int64_t earlyClampQpc = 0;
        const std::int64_t clampedQpc = AdvanceProducedCameraSnapshotClock(
            synchronizedQpc, kNativeTicMilliseconds, earlyProducerQpc,
            frequency, &earlyClampQpc);
        if (clampedQpc != earlyProducerQpc ||
            earlyClampQpc != 7'500'000) {
            return false;
        }

        // Delayed producer delivery is still advanced from game time rather
        // than chasing wall-clock jitter.
        earlyClampQpc = -1;
        const std::int64_t delayedQpc = AdvanceProducedCameraSnapshotClock(
            synchronizedQpc, kNativeTicMilliseconds, 30'000'000,
            frequency, &earlyClampQpc);
        if (delayedQpc != 26'000'000 || earlyClampQpc != 0) return false;

        // For a paired catch-up, retain the first logical tick and cap only
        // the final boundary if the burst completed slightly before it.
        std::int64_t bridgeQpc = 0;
        bridgeQpc = AdvanceProducedCameraSnapshotClock(
            bridgeQpc, kNativeTicMilliseconds, 30'000'000,
            frequency, &earlyClampQpc);
        if (bridgeQpc != 16'000'000 || earlyClampQpc != 0) return false;
        bridgeQpc = AdvanceProducedCameraSnapshotClock(
            bridgeQpc, kNativeTicMilliseconds, 30'500'000,
            frequency, &earlyClampQpc);
        return bridgeQpc == 30'500'000 && earlyClampQpc == 1'500'000;
    };

    const auto testAuthoritativeTwoTicBuffer = [] {
        struct Arrival {
            std::int64_t milliseconds;
            std::uint64_t sequence;
        };
        // Sample 5 is one native tick late and arrives in a pair with sample 6.
        // The delayed playhead must consume only sample 5 at 80 ms, retain
        // sample 6, and never enter extrapolation or jump presentation time.
        constexpr std::array<Arrival, 7> arrivals{{
            {0, 1}, {16, 2}, {32, 3}, {48, 4},
            {80, 5}, {80, 6}, {112, 7},
        }};
        constexpr std::int64_t testFrequency = 1000;
        constexpr std::int64_t nativeIntervalQpc =
            kNativeTicMilliseconds;
        if (ProducedCameraSampleReadyForAdvance(
                80, false, 80, nativeIntervalQpc) ||
            ProducedCameraSampleReadyForAdvance(
                80, false, 95, nativeIntervalQpc) ||
            !ProducedCameraSampleReadyForAdvance(
                80, true, 80, nativeIntervalQpc) ||
            !ProducedCameraSampleReadyForAdvance(
                80, false, 96, nativeIntervalQpc)) {
            return false;
        }
        std::size_t arrivalIndex = 0;
        std::uint64_t latestSequence = 0;
        std::uint64_t previousSequence = 0;
        std::uint64_t currentSequence = 0;
        std::int64_t currentBoundary = 0;
        bool initialized = false;
        bool verifiedPairDepth = false;
        double lastPresented = 0.0;
        for (std::int64_t now = 0; now <= 112; ++now) {
            while (arrivalIndex < arrivals.size() &&
                   arrivals[arrivalIndex].milliseconds <= now) {
                latestSequence = arrivals[arrivalIndex].sequence;
                ++arrivalIndex;
            }
            if (!initialized && latestSequence >= 2) {
                currentSequence = latestSequence - 1;
                previousSequence = currentSequence;
                currentBoundary = now;
                initialized = true;
            } else if (initialized &&
                       now >= currentBoundary + kNativeTicMilliseconds &&
                       currentSequence < latestSequence) {
                const std::uint64_t nextSequence = currentSequence + 1;
                std::int64_t nextArrival = -1;
                for (const auto& arrival : arrivals) {
                    if (arrival.sequence == nextSequence) {
                        nextArrival = arrival.milliseconds;
                        break;
                    }
                }
                const bool hasLookahead =
                    latestSequence > nextSequence;
                if (ProducedCameraSampleReadyForAdvance(
                        nextArrival, hasLookahead, now,
                        nativeIntervalQpc)) {
                    previousSequence = currentSequence;
                    ++currentSequence;
                    currentBoundary += kNativeTicMilliseconds;
                }
            }
            if (!initialized) continue;
            const double alpha = CameraInterpolationAlphaForMode(
                kNativeTicMilliseconds, now, currentBoundary,
                testFrequency, false);
            if (alpha < 0.0 || alpha > 1.0) return false;
            const double presented =
                static_cast<double>(previousSequence) *
                    kNativeTicMilliseconds +
                static_cast<double>(currentSequence - previousSequence) *
                    kNativeTicMilliseconds * alpha;
            if (now > 32 &&
                (presented < lastPresented ||
                 presented - lastPresented > 1.000001)) {
                return false;
            }
            if (now == 80) {
                verifiedPairDepth = latestSequence == 6 &&
                    currentSequence == 5 &&
                    latestSequence - currentSequence == 1 &&
                    std::abs(presented - 64.0) < 0.000001;
            }
            lastPresented = presented;
        }
        return verifiedPairDepth;
    };

    const auto testBufferedMouseLateLatch = [] {
        const auto close = [](double left, double right) {
            return std::abs(left - right) < 0.000001;
        };
        // The camera interpolation has already presented alpha of the input
        // between its two snapshots.  Only the remainder of that interval is
        // replayed, while input newer than the current snapshot is always
        // applied in full.
        const auto quarter = ComposeBufferedMouseOverlay(
            8.0, -4.0, 3.0, 2.0, 0.25);
        const auto start = ComposeBufferedMouseOverlay(
            8.0, -4.0, 3.0, 2.0, 0.0);
        const auto end = ComposeBufferedMouseOverlay(
            8.0, -4.0, 3.0, 2.0, 1.0);
        const auto clampedLow = ComposeBufferedMouseOverlay(
            8.0, -4.0, 3.0, 2.0, -1.0);
        const auto clampedHigh = ComposeBufferedMouseOverlay(
            8.0, -4.0, 3.0, 2.0, 2.0);
        return close(quarter[0], 9.0) && close(quarter[1], -1.0) &&
               close(start[0], 11.0) && close(start[1], -2.0) &&
               close(end[0], 3.0) && close(end[1], 2.0) &&
               close(clampedLow[0], start[0]) &&
               close(clampedLow[1], start[1]) &&
               close(clampedHigh[0], end[0]) &&
               close(clampedHigh[1], end[1]);
    };

    const auto testBufferedEntityEligibility = [] {
        // A static or repeatedly submitted unchanged state must not make an
        // entity eligible forever merely because it is the nearest older
        // sample.  A changed pair is interpolated, while an unchanged older
        // state is still held when a newer future sample proves the live
        // entity is ahead of the delayed playhead.
        const bool staticSample = BufferedEntityHistoryNeedsPresentation(
            false, false, 10, 100);
        const bool unchangedCurrent =
            BufferedEntityHistoryNeedsPresentation(false, false, 100, 100);
        const bool changedPair = BufferedEntityHistoryNeedsPresentation(
            true, true, 100, 100);
        const bool delayedFuture = BufferedEntityHistoryNeedsPresentation(
            false, false, 101, 100);
        const bool discontinuityAtLive =
            BufferedEntityHistoryNeedsPresentation(true, false, 100, 100);
        const bool discontinuityAhead =
            BufferedEntityHistoryNeedsPresentation(true, false, 101, 100);
        return !staticSample && !unchangedCurrent && changedPair &&
               delayedFuture && !discontinuityAtLive &&
               discontinuityAhead;
    };

    const auto testSustainedPhaseRecovery = [] {
        // Reproduce the captured rollback geometry, then keep the observed
        // phase persistently late. Debounce must preserve the first frames,
        // bounded recovery must eventually start, and every presented time
        // must remain forward-moving until synchronization is restored.
        constexpr double callGapMilliseconds = 3.1998;
        constexpr double firstMidpointMilliseconds = 30.6868;
        const std::int64_t callGapQpc = static_cast<std::int64_t>(
            std::llround(callGapMilliseconds *
                         static_cast<double>(frequency) / 1000.0));
        const std::int64_t halfGapQpc = callGapQpc / 2;
        const std::int64_t nativeTicQpc = CameraIntervalQpc(
            kNativeTicMilliseconds, frequency);
        std::int64_t midpointQpc = static_cast<std::int64_t>(
            std::llround(firstMidpointMilliseconds *
                         static_cast<double>(frequency) / 1000.0));
        std::int64_t currentSnapshotQpc = 0;
        bool phaseRecoveryActive = false;
        std::int64_t phaseRecoveryCandidateQpc = 0;
        double previousViewTime = kNativeTicMilliseconds;
        const double previousPresented = 29.0869;
        double lastPresented = previousPresented;
        bool sawCandidate = false;
        bool sawRecoveryStart = false;
        bool sawRecoveryFinish = false;
        for (int sample = 0; sample < 64; ++sample) {
            const std::int64_t nowQpc = midpointQpc + halfGapQpc;
            const CameraSnapshotClockUpdate update =
                UpdateCameraSnapshotClock(
                    currentSnapshotQpc, kNativeTicMilliseconds,
                    midpointQpc - halfGapQpc, nowQpc, frequency,
                    phaseRecoveryActive, phaseRecoveryCandidateQpc);
            currentSnapshotQpc = update.qpc;
            sawCandidate = sawCandidate || phaseRecoveryCandidateQpc != 0;
            sawRecoveryStart = sawRecoveryStart || update.recoveryStarted;
            const double alpha = CameraInterpolationAlphaForMode(
                kNativeTicMilliseconds, nowQpc, currentSnapshotQpc,
                frequency, true);
            const double presented = previousViewTime +
                static_cast<double>(kNativeTicMilliseconds) * alpha;
            if (presented <= lastPresented) return false;
            if (!sawRecoveryStart && update.correctionQpc != 0) return false;
            lastPresented = presented;
            if (update.recoveryFinished) {
                sawRecoveryFinish = true;
                break;
            }
            midpointQpc += nativeTicQpc;
            previousViewTime += kNativeTicMilliseconds;
        }

        const double rebasedPresented =
            static_cast<double>(kNativeTicMilliseconds);
        return sawCandidate && sawRecoveryStart && sawRecoveryFinish &&
               !phaseRecoveryActive && phaseRecoveryCandidateQpc == 0 &&
               rebasedPresented < previousPresented;
    };

    const bool featureGatesPassed = testFeatureGates();
    const bool rate120Passed = testRate(120.0, false, true);
    const bool rate60Passed = testRate(60.0, true, false);
    const bool overdue360Passed = testOverdue360();
    const bool batchedPhasePassed = testBatchedPhaseExcursion();
    const bool earlyProducerPassed = testEarlyProducerClamp();
    const bool authoritativeBufferPassed = testAuthoritativeTwoTicBuffer();
    const bool bufferedMousePassed = testBufferedMouseLateLatch();
    const bool bufferedEntityPassed = testBufferedEntityEligibility();
    const bool sustainedPhasePassed = testSustainedPhaseRecovery();
    if (!featureGatesPassed || !rate120Passed || !rate60Passed ||
        !overdue360Passed || !batchedPhasePassed ||
        !earlyProducerPassed || !authoritativeBufferPassed ||
        !bufferedMousePassed || !bufferedEntityPassed ||
        !sustainedPhasePassed) {
        char line[288]{};
        const int length = _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "test: interpolation cadence failed; features=%d rate120=%d "
            "rate60=%d overdue360=%d batched_phase=%d early_producer=%d "
            "authoritative_buffer=%d buffered_mouse=%d "
            "buffered_entity=%d sustained_phase=%d\r\n",
            featureGatesPassed ? 1 : 0, rate120Passed ? 1 : 0,
            rate60Passed ? 1 : 0, overdue360Passed ? 1 : 0,
            batchedPhasePassed ? 1 : 0, earlyProducerPassed ? 1 : 0,
            authoritativeBufferPassed ? 1 : 0,
            bufferedMousePassed ? 1 : 0,
            bufferedEntityPassed ? 1 : 0,
            sustainedPhasePassed ? 1 : 0);
        if (length > 0) Log(std::string(line, static_cast<std::size_t>(length)));
        return FALSE;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI PreyHFRTestBorderless(HDC deviceContext) {
    TryApplyBorderlessWindow(deviceContext, false, false);
    const HWND window = WindowFromDC(deviceContext);
    return window != nullptr && g_borderlessWindow.window == window &&
           g_borderlessWindow.applied ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) DWORD WINAPI PreyHFRGetInitializationState() {
    if (!g_initializationComplete.load(std::memory_order_acquire)) return 0;
    return g_patchActive.load(std::memory_order_acquire) ? 1 : 2;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        g_pluginModule = instance;
        wchar_t probeMode[2]{};
        g_probeMode = GetEnvironmentVariableW(
                          L"PREYHFR_PROBE", probeMode,
                          static_cast<DWORD>(std::size(probeMode))) != 0 &&
                      probeMode[0] == L'1';
        if (g_probeMode) {
            const bool initialized = InitializeFromEnvironment();
            g_patchActive.store(initialized, std::memory_order_release);
            g_initializationComplete.store(true, std::memory_order_release);
            return initialized ? TRUE : FALSE;
        }
        const HANDLE bootstrap =
            CreateThread(nullptr, 0, &BootstrapAsi, nullptr, 0, nullptr);
        if (bootstrap == nullptr) return FALSE;
        SetThreadPriority(bootstrap, THREAD_PRIORITY_HIGHEST);
        CloseHandle(bootstrap);
        return TRUE;
    }
    if (reason == DLL_PROCESS_DETACH) {
        g_patchActive.store(false, std::memory_order_release);
        if (g_probeMode && reserved == nullptr) Shutdown();
    }
    return TRUE;
}
