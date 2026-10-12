#pragma once
#include <MinHook.h>

namespace tweaks {

// Every tweak that hooks calls this from its install; only the first call initializes MinHook.
inline bool startMinHook() {
    const MH_STATUS status = MH_Initialize();
    return status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED;
}

}  // namespace tweaks
