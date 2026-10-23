#pragma once
#include "CoreMinimal.h"

enum class EWowWeatherKind : uint8 { Clear, Fog, Rain, Snow, Dust, Storm, BlackRain, BlackSnow };
// r723: Mountain = zones where WoW has both rain and snow (Alterac); appended so the logged numbers stay the same.
enum class EWowClimate : uint8 { Temperate, Cold, Dry, Tropical, Ash, Mountain };

struct FWowWeatherSample
{
    EWowWeatherKind Kind = EWowWeatherKind::Clear;
    float Intensity = 0, Cloud = 0.2f, Wind = 2, Direction = 0;
    // r534 (Stefan: mix weathers - fog with rain, fog with overcast): fog on top of the main kind
    // (0..1, same scale as Fog intensity). Cloud already varies freely, so overcast or a clear sky
    // combine with any kind.
    float Mist = 0;
    // r723 (Stefan: "I want my weather to be the in-game weather ... in Alterac Mountains there would be snow,
    // or a blizzard instead of rain, but it could also rain"): air temperature in C - at his real location
    // (Open-Meteo temperature_2m) or a seasonal guess for the zone fronts. Decides rain or snow in mountain
    // zones. Realm weather has none (bHasTemperature false), so the realm's own rain/snow is kept.
    float TemperatureC = 0;
    bool bHasTemperature = false;
};

namespace WowWeather
{
// Deterministic smooth fronts for zones without a supplied meteorological heading.
// Absolute time keeps direction stable across reconnects; interpolation handles borders.
inline float ZoneWindDirection(uint32 Zone,double Seconds)
{
    const double Phase=double(Zone%997)*.37;
    return FMath::UnwindDegrees(float(Phase*57.2957795
        +65.0*FMath::Sin(Seconds/317.0+Phase)
        +35.0*FMath::Sin(Seconds/733.0+Phase*1.7)));
}
// A small dry atmospheric layer, independent of precipitation type/intensity.
inline float AmbientStrength(uint32 Zone, double Seconds, float Wind)
{
    if (!Zone || !FMath::IsFinite(Seconds) || !FMath::IsFinite(Wind)) return 0.f;
    const double Phase = double(Zone % 997) * 0.37;
    const float Variation = float(0.5 + 0.25 * FMath::Sin(Seconds / 137.0 + Phase)
        + 0.25 * FMath::Sin(Seconds / 223.0 + Phase * 1.7));
    return 0.10f + 0.12f * FMath::Clamp(Variation,0.f,1.f)
        * FMath::Clamp(1.f-FMath::Max(0.f,Wind)/40.f,0.15f,1.f);
}
inline EWowWeatherKind DecodeState(uint32 State)
{
    switch (State)
    {
    case 1: return EWowWeatherKind::Fog;
    case 3: case 4: case 5: return EWowWeatherKind::Rain;
    case 6: case 7: case 8: return EWowWeatherKind::Snow;
    case 22: case 41: case 42: return EWowWeatherKind::Dust;
    case 86: return EWowWeatherKind::Storm;
    case 90: return EWowWeatherKind::BlackRain;
    case 106: return EWowWeatherKind::BlackSnow;
    default: return EWowWeatherKind::Clear;
    }
}
inline uint32 EncodeState(EWowWeatherKind Kind)
{
    switch(Kind) {
    case EWowWeatherKind::Fog: return 1;
    case EWowWeatherKind::Rain: return 4;
    case EWowWeatherKind::Snow: return 7;
    case EWowWeatherKind::Dust: return 41;
    case EWowWeatherKind::Storm: return 86;
    case EWowWeatherKind::BlackRain: return 90;
    case EWowWeatherKind::BlackSnow: return 106;
    default: return 0;
    }
}
inline EWowClimate Climate(const FString& Zone, uint32 ZoneId = 0)
{
    // Zone IDs survive localized AreaTable names; callers pass the parent zone.
    switch (ZoneId) {
    case 1: case 618: case 210: case 67: case 65: case 4197: case 1537: case 721: return EWowClimate::Cold;
    case 440: case 1377: case 3: case 405: case 14: case 17: case 400: case 1637: return EWowClimate::Dry;
    case 33: case 490: case 3711: case 8: case 3521: case 357: return EWowClimate::Tropical;
    case 51: case 46: case 3483: case 3520: return EWowClimate::Ash;
    // r723: Alterac Mountains, Alterac Valley, Grizzly Hills, Howling Fjord - rain or snow by the temperature.
    case 36: case 2597: case 394: case 495: return EWowClimate::Mountain;
    default: break;
    }
    for (const TCHAR* Name : {TEXT("Dun Morogh"),TEXT("Winterspring"),TEXT("Icecrown"),TEXT("The Storm Peaks"),TEXT("Dragonblight"),TEXT("Wintergrasp"),TEXT("Ironforge"),TEXT("Gnomeregan")})
        if (Zone.Equals(Name,ESearchCase::IgnoreCase)) return EWowClimate::Cold;
    for (const TCHAR* Name : {TEXT("Tanaris"),TEXT("Silithus"),TEXT("Badlands"),TEXT("Desolace"),TEXT("Durotar"),TEXT("The Barrens"),TEXT("Thousand Needles"),TEXT("Orgrimmar")})
        if (Zone.Equals(Name,ESearchCase::IgnoreCase)) return EWowClimate::Dry;
    for (const TCHAR* Name : {TEXT("Stranglethorn Vale"),TEXT("Un'Goro Crater"),TEXT("Sholazar Basin"),TEXT("Swamp of Sorrows"),TEXT("Zangarmarsh"),TEXT("Feralas")})
        if (Zone.Equals(Name,ESearchCase::IgnoreCase)) return EWowClimate::Tropical;
    for (const TCHAR* Name : {TEXT("Searing Gorge"),TEXT("Burning Steppes"),TEXT("Hellfire Peninsula"),TEXT("Shadowmoon Valley")})
        if (Zone.Equals(Name,ESearchCase::IgnoreCase)) return EWowClimate::Ash;
    for (const TCHAR* Name : {TEXT("Alterac Mountains"),TEXT("Alterac Valley"),TEXT("Grizzly Hills"),TEXT("Howling Fjord")})
        if (Zone.Equals(Name,ESearchCase::IgnoreCase)) return EWowClimate::Mountain;
    return EWowClimate::Temperate;
}
inline bool IsPrecipitation(EWowWeatherKind Kind)
{
    return Kind==EWowWeatherKind::Rain || Kind==EWowWeatherKind::Snow || Kind==EWowWeatherKind::Storm
        || Kind==EWowWeatherKind::BlackRain || Kind==EWowWeatherKind::BlackSnow;
}
// r723: a mountain zone is taken as about 1 km above the player's own place (~6 C colder); its precipitation
// falls as snow from +1 C there, i.e. at +7 C or less at home.
constexpr float WowMountainCoolingC = 6.f;
constexpr float WowMountainSnowLineC = 1.f;
inline bool FallsAsSnow(const FWowWeatherSample& S, EWowClimate Zone)
{
    if (Zone == EWowClimate::Cold) return true;
    if (Zone != EWowClimate::Mountain) return false;
    if (S.Kind == EWowWeatherKind::Snow || S.Kind == EWowWeatherKind::BlackSnow) return true;
    return S.bHasTemperature && S.TemperatureC - WowMountainCoolingC <= WowMountainSnowLineC;
}
inline FWowWeatherSample Adapt(FWowWeatherSample S, EWowClimate Zone)
{
    if (Zone == EWowClimate::Dry || Zone == EWowClimate::Ash)
    {
        // r723: only rain, snow and storms turn into a sandstorm; fog at home was carried over as a 0.7 dust storm.
        // Fog or a clear sky leave just the wind-raised dust.
        const float Precipitation = IsPrecipitation(S.Kind) ? S.Intensity : 0.f;
        S.Intensity = FMath::Max(Precipitation,FMath::Clamp((S.Wind-6)/14,0.f,1.f));
        // Volcanic zones have a light ash fall even between stronger fronts.
        if (Zone == EWowClimate::Ash) S.Intensity = FMath::Max(0.12f,S.Intensity);
        S.Kind = S.Intensity > 0.08f ? (Zone==EWowClimate::Ash ? EWowWeatherKind::BlackSnow : EWowWeatherKind::Dust) : EWowWeatherKind::Clear;
        S.Cloud *= 0.6f;
    }
    else if (S.Kind != EWowWeatherKind::Clear && S.Kind != EWowWeatherKind::Fog)
    {
        if (FallsAsSnow(S, Zone))
        {
            // r723: a thunderstorm, or snow in a strong wind (8 m/s and up at home), is a blizzard here: heavy
            // snow and a gale (the blizzard look starts at 18 m/s and is full at 56, GetBlizzardStrength).
            if (S.Kind == EWowWeatherKind::Storm || S.Wind >= 8.f)
            {
                S.Intensity = FMath::Max(S.Intensity, 0.85f);
                S.Wind = FMath::Max(FMath::Lerp(28.f, 56.f, FMath::Clamp((S.Wind - 8.f) / 12.f, 0.f, 1.f)),
                    S.Kind == EWowWeatherKind::Storm ? 44.f : 0.f);
            }
            S.Kind = EWowWeatherKind::Snow;
        }
        else if (S.Kind == EWowWeatherKind::Snow || S.Kind == EWowWeatherKind::BlackSnow) S.Kind = EWowWeatherKind::Rain;
        if (Zone == EWowClimate::Tropical) S.Intensity = FMath::Clamp(S.Intensity*1.2f,0.f,1.f);
    }
    return S;
}
inline bool CoordinatesValid(double Latitude, double Longitude)
{
    return FMath::IsFinite(Latitude) && FMath::IsFinite(Longitude) && FMath::Abs(Latitude)<=90 && FMath::Abs(Longitude)<=180;
}
// Reject missing, malformed, future or stale provider data. Keep the previous valid sample on failure.
WOWWORLD_API bool ParseCurrent(const FString& Json, int64 NowUtc, FWowWeatherSample& Out, int64& SampleUtc);
}
