#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace preyhfr {

// These RVAs describe the retail 1.4 layout used by the reversible hooks. A
// compatible executable may have a different file identity, but every RVA that
// a configured feature depends on must still have the expected role.
inline constexpr std::uintptr_t kSingleViewRva = 0x001a8de0;
inline constexpr std::uintptr_t kCalculateRenderViewRva = 0x00087600;
inline constexpr std::uintptr_t kDetermineViewAnglesRva = 0x00195730;
inline constexpr std::uintptr_t kGameRenderWorldPointerRva = 0x0038ff60;
inline constexpr std::uintptr_t kMouseMoveRva = 0x00069000;
inline constexpr std::uintptr_t kUsercmdTicCmdRva = 0x00068ba0;
inline constexpr std::uintptr_t kUsercmdInterruptRva = 0x00069880;
inline constexpr std::uintptr_t kGetDirectUsercmdRva = 0x00069970;
inline constexpr std::uintptr_t kCvarSystemVtableRva = 0x003af26c;
inline constexpr std::uintptr_t kSetCVarStringRva = 0x0002db00;
inline constexpr std::uintptr_t kSetCVarBoolRva = 0x0002db10;
inline constexpr std::uintptr_t kSetCVarIntegerRva = 0x0002db90;
inline constexpr std::uintptr_t kRunGameTicRvaBegin = 0x0005c670;
inline constexpr std::uintptr_t kRunGameTicRvaEnd = 0x0005cf00;
inline constexpr std::uintptr_t kRunGameTicUsercmdSelectionRva = 0x0005c74f;
inline constexpr std::uintptr_t kSensitivityCvarPointerRva = 0x0044298c;
inline constexpr std::uintptr_t kPitchCvarPointerRva = 0x004429c0;
inline constexpr std::uintptr_t kYawCvarPointerRva = 0x004429f4;
inline constexpr std::uintptr_t kSmoothCvarPointerRva = 0x00442a5c;
inline constexpr std::uintptr_t kClearEntityDefDynamicModelRva = 0x000df3e0;

inline constexpr std::array<std::uint8_t, 6> kSingleViewPrologue{
    0x64, 0xa1, 0x00, 0x00, 0x00, 0x00};
inline constexpr std::array<std::uint8_t, 6> kCalculateRenderViewPrologue{
    0x83, 0xec, 0x10, 0x56, 0x8b, 0xf1};
inline constexpr std::array<std::uint8_t, 6> kDetermineViewAnglesPrologue{
    0x55, 0x8b, 0xec, 0x83, 0xe4, 0xc0};
inline constexpr std::array<std::uint8_t, 5> kMouseMoveOpcodePrefix{
    0x83, 0xec, 0x1c, 0x8b, 0x15};
inline constexpr std::size_t kMouseMoveInstructionSize = 9;
inline constexpr std::array<std::uint8_t, 64> kRunGameTicUsercmdPattern{
    0x8b, 0x0d, 0, 0, 0, 0,       // mov ecx, [com_fixedTic]
    0x83, 0x79, 0x24, 0x00,       // cmp [ecx+24h], 0
    0x8b, 0x0d, 0, 0, 0, 0,       // mov ecx, [usercmdGen]
    0x74, 0x15,                   // je direct-command path
    0x8b, 0x85, 0, 0, 0, 0,       // mov eax, [session+lastGameTic]
    0x8b, 0x11,
    0x8b, 0x52, 0x1c,             // vtable slot 7: TicCmd
    0x50,
    0x8d, 0x44, 0x24, 0x34,
    0x50,
    0xff, 0xd2,
    0xeb, 0x0c,
    0x8b, 0x01,
    0x8b, 0x40, 0x3c,             // vtable slot 15: GetDirectUsercmd
    0x8d, 0x54, 0x24, 0x30,
    0x52,
    0xff, 0xd0,
    0xb9, 0x08, 0x00, 0x00, 0x00, // copy the returned 32-byte usercmd
    0x8d, 0x7c, 0x24, 0x10,
    0x8b, 0xf0,
    0xf3, 0xa5,
};
inline constexpr std::array<bool, kRunGameTicUsercmdPattern.size()>
    kRunGameTicUsercmdMask{
        true, true, false, false, false, false,
        true, true, true, true,
        true, true, false, false, false, false,
        true, true,
        true, true, false, false, false, false,
        true, true,
        true, true, true,
        true,
        true, true, true, true,
        true,
        true, true,
        true, true,
        true, true,
        true, true, true,
        true, true, true, true,
        true,
        true, true,
        true, true, true, true, true,
        true, true, true, true,
        true, true,
        true, true,
    };
inline constexpr std::array<std::uint8_t, 16>
    kClearEntityDefDynamicModelPrologue{
        0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x8b, 0xb7,
        0x78, 0x01, 0x00, 0x00, 0x85, 0xf6, 0x74, 0x13};

struct CompatibilityRequirements {
    bool displayOverrides = false;
    bool viewHook = false;
    bool cameraHook = false;
    bool entityHooks = false;
    bool animationHooks = false;
    bool mouseHooks = false;
    bool asyncClockHooks = false;
};

bool ValidateExecutableLayout(const std::filesystem::path& path,
                              const CompatibilityRequirements& requirements,
                              std::string& error);
bool ValidateGameModuleLayout(const std::filesystem::path& path,
                              const CompatibilityRequirements& requirements,
                              std::string& error);

} // namespace preyhfr
