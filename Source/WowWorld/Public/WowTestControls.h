#pragma once
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

// One gate for all session-only development overrides. Shipping builds always ignore them.
namespace WowTestControls {
inline bool Enabled() {
#if UE_BUILD_SHIPPING
    return false;
#else
    const IConsoleVariable* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestMenuEnabled"));
    return C && C->GetInt()!=0;
#endif
}
inline float ClockMinutes() {
    const IConsoleVariable* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime"));
    return Enabled()&&C?C->GetFloat():-1.f;
}
}
