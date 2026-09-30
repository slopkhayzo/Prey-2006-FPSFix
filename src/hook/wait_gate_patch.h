#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace preyhfr {

struct WaitGateResult {
    std::uintptr_t address = 0;
    std::uintptr_t comTicAddress = 0;
    std::uintptr_t fixedTicObjectPointerAddress = 0;
    std::uintptr_t eventLoopObjectPointerAddress = 0;
    std::uintptr_t waitLabelAddress = 0;
    bool alreadyPatched = false;
};

class WaitGatePatch {
public:
    bool WaitAndApply(DWORD timeoutMilliseconds, WaitGateResult& result,
                      std::string& error);
    bool Restore();
    bool ownsPatch() const { return ownsPatch_; }

private:
    std::uint8_t* patchAddress_ = nullptr;
    bool ownsPatch_ = false;
};

} // namespace preyhfr
