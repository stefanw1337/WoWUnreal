#pragma once
#include "CoreMinimal.h"

namespace WowLunarPhase
{
// Mean synodic cycle anchored to the observed 2000-01-06 18:14 UTC new moon.
// Visual approximation, not a precision ephemeris or location-dependent moonrise prediction.
inline float Cycle(const FDateTime& Utc)
{
    const double Days=(Utc-FDateTime(2000,1,6,18,14)).GetTotalDays();
    double Phase=FMath::Fmod(Days/29.530588853,1.0);
    if (Phase<0) Phase+=1.0;
    return static_cast<float>(Phase);
}
inline float Illumination(float Phase)
{
    return 0.5f*(1.f-FMath::Cos(2.f*PI*Phase));
}
inline float NightVisibility(float Minutes)
{
    const float DistanceFromMidnight=FMath::Min(Minutes,1440.f-Minutes);
    const float X=FMath::Clamp((390.f-DistanceFromMidnight)/90.f,0.f,1.f);
    return X*X*(3.f-2.f*X);
}
}
