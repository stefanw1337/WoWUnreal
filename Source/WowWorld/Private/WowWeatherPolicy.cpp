#include "WowWeatherPolicy.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool WowWeather::ParseCurrent(const FString& Json, int64 NowUtc, FWowWeatherSample& Out, int64& SampleUtc)
{
    if (Json.Len()>65536) return false;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root) || !Root.IsValid()) return false;
    const TSharedPtr<FJsonObject>* Data = nullptr;
    if (!Root->TryGetObjectField(TEXT("current"),Data) || !Data || !Data->IsValid()) return false;
    double Code,Cloud,Wind,Direction,Time;
    auto Get=[&](const TCHAR* Key,double& Value) { return (*Data)->TryGetNumberField(Key,Value)&&FMath::IsFinite(Value); };
    if (!Get(TEXT("weather_code"),Code)||!Get(TEXT("cloud_cover"),Cloud)||!Get(TEXT("wind_speed_10m"),Wind)||!Get(TEXT("wind_direction_10m"),Direction)||!Get(TEXT("time"),Time)) return false;
    if (Time>NowUtc+900 || Time<NowUtc-7200 || Wind<0 || Wind>150 || Cloud<0 || Cloud>100 || Direction<0 || Direction>360 || Code!=FMath::FloorToDouble(Code)) return false;
    FWowWeatherSample S;
    S.Cloud=Cloud/100; S.Wind=FMath::Min(float(Wind),30.f); S.Direction=Direction;
    switch (int32(Code)) {
    case 0: case 1: case 2: case 3: break;
    case 45: case 48: S.Kind=EWowWeatherKind::Fog; S.Intensity=0.7f; break;
    case 51: case 56: S.Kind=EWowWeatherKind::Rain; S.Intensity=0.2f; break;
    case 53: case 55: case 57: case 61: case 66: case 80: S.Kind=EWowWeatherKind::Rain; S.Intensity=0.4f; break;
    case 63: case 81: S.Kind=EWowWeatherKind::Rain; S.Intensity=0.65f; break;
    case 65: case 67: case 82: S.Kind=EWowWeatherKind::Rain; S.Intensity=0.95f; break;
    case 71: case 77: case 85: S.Kind=EWowWeatherKind::Snow; S.Intensity=0.4f; break;
    case 73: S.Kind=EWowWeatherKind::Snow; S.Intensity=0.65f; break;
    case 75: case 86: S.Kind=EWowWeatherKind::Snow; S.Intensity=0.95f; break;
    case 95: case 96: case 99: S.Kind=EWowWeatherKind::Storm; S.Intensity=0.95f; break;
    default: return false;
    }
    // r723: air temperature (optional, so older cached answers without it still load).
    double Temperature;
    if (Get(TEXT("temperature_2m"),Temperature) && Temperature>-90 && Temperature<60)
    {
        S.TemperatureC=float(Temperature); S.bHasTemperature=true;
    }
    Out=S; SampleUtc=int64(Time); return true;
}
