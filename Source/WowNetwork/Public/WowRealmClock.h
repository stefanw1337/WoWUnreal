#pragma once
#include "CoreMinimal.h"

/** Server wall clock, independent of frame rate, pause and client timezone. */
struct FWowRealmClock
{
    bool bSynchronized = false;
    double MinutesAtSync = 0.0;
    double MonotonicAtSync = 0.0;
    double MinutesPerSecond = 1.0 / 60.0;

    bool Synchronize(uint32 PackedTime, float Speed, double Now)
    {
        const uint32 Minute = PackedTime & 63;
        const uint32 Hour = (PackedTime >> 6) & 31;
        if (Minute > 59 || Hour > 23 || !FMath::IsFinite(Speed) || Speed < 0 || Speed > 60 || !FMath::IsFinite(Now)) return false;
        MinutesAtSync = Hour * 60.0 + Minute;
        MonotonicAtSync = Now;
        MinutesPerSecond = Speed;
        bSynchronized = true;
        return true;
    }
    double Sample(double Now, const FDateTime& LocalNow) const
    {
        if (!bSynchronized) return LocalNow.GetTimeOfDay().GetTotalMinutes();
        const double Minutes = MinutesAtSync + FMath::Max(0.0, Now - MonotonicAtSync) * MinutesPerSecond;
        return FMath::Fmod(Minutes, 1440.0);
    }
};
