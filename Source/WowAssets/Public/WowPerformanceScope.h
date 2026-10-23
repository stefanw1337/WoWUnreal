#pragma once
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

// Emit only slow work, so profiling stays useful without per-frame log spam.
// Measurements are inclusive CPU wall time, not GPU time or additive totals.
class FWowPerformanceScope
{
public:
    FWowPerformanceScope(const TCHAR* InPhase, const FString& InAsset, double InThresholdMs=8.0)
        : Phase(InPhase), Asset(InAsset), ThresholdMs(InThresholdMs), Started(FPlatformTime::Seconds()) {}
    // Hot update scopes use literals and must not allocate a label every frame.
    FWowPerformanceScope(const TCHAR* InPhase, const TCHAR* InAsset, double InThresholdMs=8.0)
        : Phase(InPhase), StaticAsset(InAsset), ThresholdMs(InThresholdMs), Started(FPlatformTime::Seconds()) {}
    ~FWowPerformanceScope()
    {
        const double ElapsedMs=(FPlatformTime::Seconds()-Started)*1000.0;
        if (ElapsedMs>=ThresholdMs)
            UE_LOG(LogTemp, Log, TEXT("Wow slow work: phase=%s asset=%s cpuWallMs=%.2f"),
                Phase,StaticAsset ? StaticAsset : *Asset,ElapsedMs);
    }
private:
    const TCHAR* Phase;
    FString Asset;
    const TCHAR* StaticAsset = nullptr;
    double ThresholdMs;
    double Started;
};
