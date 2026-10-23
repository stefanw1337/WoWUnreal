#include "WowVRComfort.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
constexpr TCHAR Section[] = TEXT("WowUnreal.VRComfort");

struct FValues
{
    int32 Preset = 0; // first VR start: Comfortable
    bool bSnapTurn = true;
    float SnapDegrees = 30.f;
    float Vignette = .8f;
    float LookDownGain = 1.f;
    bool bLeanOver = false;
    float Acceleration = .6f;
    bool bDepthOfField = false;
    float HeadStillness = 0.f;
    int32 GraphicsLevel = 1;
};

FValues& Values()
{
    static FValues V;
    static bool bLoaded = false;
    if (!bLoaded && GConfig)
    {
        bLoaded = true;
        GConfig->GetInt(Section, TEXT("Preset"), V.Preset, GGameUserSettingsIni);
        GConfig->GetBool(Section, TEXT("SnapTurn"), V.bSnapTurn, GGameUserSettingsIni);
        GConfig->GetFloat(Section, TEXT("SnapTurnDegrees"), V.SnapDegrees, GGameUserSettingsIni);
        GConfig->GetFloat(Section, TEXT("Vignette"), V.Vignette, GGameUserSettingsIni);
        GConfig->GetFloat(Section, TEXT("LookDownGain"), V.LookDownGain, GGameUserSettingsIni);
        GConfig->GetBool(Section, TEXT("LeanOver"), V.bLeanOver, GGameUserSettingsIni);
        GConfig->GetFloat(Section, TEXT("Acceleration"), V.Acceleration, GGameUserSettingsIni);
        GConfig->GetBool(Section, TEXT("DepthOfField"), V.bDepthOfField, GGameUserSettingsIni);
        GConfig->GetFloat(Section, TEXT("HeadStillness"), V.HeadStillness, GGameUserSettingsIni);
        GConfig->GetInt(Section, TEXT("GraphicsLevel"), V.GraphicsLevel, GGameUserSettingsIni);
        V.GraphicsLevel = FMath::Clamp(V.GraphicsLevel, 0, 3);
        V.Preset = FMath::Clamp(V.Preset, 0, 3);
        V.SnapDegrees = FMath::Clamp(FMath::IsFinite(V.SnapDegrees) ? V.SnapDegrees : 30.f, 15.f, 90.f);
        V.Vignette = FMath::Clamp(FMath::IsFinite(V.Vignette) ? V.Vignette : .8f, 0.f, 1.f);
        V.LookDownGain = FMath::Clamp(FMath::IsFinite(V.LookDownGain) ? V.LookDownGain : 1.f, 1.f, 3.f);
        V.Acceleration = FMath::Clamp(FMath::IsFinite(V.Acceleration) ? V.Acceleration : .6f, .5f, 1.5f);
        V.HeadStillness = FMath::Clamp(FMath::IsFinite(V.HeadStillness) ? V.HeadStillness : 0.f, 0.f, 1.f);
    }
    return V;
}

void Save()
{
    if (!GConfig) return;
    const FValues& V = Values();
    GConfig->SetInt(Section, TEXT("Preset"), V.Preset, GGameUserSettingsIni);
    GConfig->SetBool(Section, TEXT("SnapTurn"), V.bSnapTurn, GGameUserSettingsIni);
    GConfig->SetFloat(Section, TEXT("SnapTurnDegrees"), V.SnapDegrees, GGameUserSettingsIni);
    GConfig->SetFloat(Section, TEXT("Vignette"), V.Vignette, GGameUserSettingsIni);
    GConfig->SetFloat(Section, TEXT("LookDownGain"), V.LookDownGain, GGameUserSettingsIni);
    GConfig->SetBool(Section, TEXT("LeanOver"), V.bLeanOver, GGameUserSettingsIni);
    GConfig->SetFloat(Section, TEXT("Acceleration"), V.Acceleration, GGameUserSettingsIni);
    GConfig->SetBool(Section, TEXT("DepthOfField"), V.bDepthOfField, GGameUserSettingsIni);
    GConfig->SetFloat(Section, TEXT("HeadStillness"), V.HeadStillness, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("GraphicsLevel"), V.GraphicsLevel, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

// Any single change makes the preset Custom.
void Changed() { Values().Preset = int32(EWowVRComfortPreset::Custom); Save(); }
}

EWowVRComfortPreset FWowVRComfort::GetPreset() { return static_cast<EWowVRComfortPreset>(Values().Preset); }

// Head stillness is 0 in every preset: Stefan found the unfiltered head pose better than the
// filtered one. Holding back small head motion makes the view lag the real head and fight the
// headset's own reprojection; the "heartbeat" he sees is more likely uneven frame pacing.
void FWowVRComfort::ApplyPreset(EWowVRComfortPreset Preset)
{
    FValues& V = Values();
    switch (Preset)
    {
    case EWowVRComfortPreset::Comfortable:
        V.bSnapTurn = true;  V.SnapDegrees = 30.f; V.Vignette = .8f; V.LookDownGain = 1.f;
        V.bLeanOver = false; V.Acceleration = .6f; V.bDepthOfField = false; V.HeadStillness = 0.f; break;
    case EWowVRComfortPreset::Balanced:
        V.bSnapTurn = false; V.SnapDegrees = 30.f; V.Vignette = .35f; V.LookDownGain = 1.3f;
        V.bLeanOver = true;  V.Acceleration = 1.f; V.bDepthOfField = false; V.HeadStillness = 0.f; break;
    case EWowVRComfortPreset::Immersive:
        V.bSnapTurn = false; V.SnapDegrees = 30.f; V.Vignette = 0.f; V.LookDownGain = 1.9f;
        V.bLeanOver = true;  V.Acceleration = 1.f; V.bDepthOfField = true; V.HeadStillness = 0.f; break;
    default: return;
    }
    V.Preset = int32(Preset);
    Save();
}

bool FWowVRComfort::GetSnapTurn() { return Values().bSnapTurn; }
void FWowVRComfort::SetSnapTurn(bool b) { Values().bSnapTurn = b; Changed(); }
float FWowVRComfort::GetSnapTurnDegrees() { return Values().SnapDegrees; }
void FWowVRComfort::SetSnapTurnDegrees(float v) { if (FMath::IsFinite(v)) { Values().SnapDegrees = FMath::Clamp(v, 15.f, 90.f); Changed(); } }
float FWowVRComfort::GetVignette() { return Values().Vignette; }
void FWowVRComfort::SetVignette(float v) { if (FMath::IsFinite(v)) { Values().Vignette = FMath::Clamp(v, 0.f, 1.f); Changed(); } }
float FWowVRComfort::GetLookDownGain() { return Values().LookDownGain; }
void FWowVRComfort::SetLookDownGain(float v) { if (FMath::IsFinite(v)) { Values().LookDownGain = FMath::Clamp(v, 1.f, 3.f); Changed(); } }
bool FWowVRComfort::GetLeanOver() { return Values().bLeanOver; }
void FWowVRComfort::SetLeanOver(bool b) { Values().bLeanOver = b; Changed(); }
float FWowVRComfort::GetAcceleration() { return Values().Acceleration; }
void FWowVRComfort::SetAcceleration(float v) { if (FMath::IsFinite(v)) { Values().Acceleration = FMath::Clamp(v, .5f, 1.5f); Changed(); } }
bool FWowVRComfort::GetDepthOfField() { return Values().bDepthOfField; }
void FWowVRComfort::SetDepthOfField(bool b) { Values().bDepthOfField = b; Changed(); }
float FWowVRComfort::GetHeadStillness() { return Values().HeadStillness; }
void FWowVRComfort::SetHeadStillness(float v) { if (FMath::IsFinite(v)) { Values().HeadStillness = FMath::Clamp(v, 0.f, 1.f); Changed(); } }
int32 FWowVRComfort::GetGraphicsLevel() { return Values().GraphicsLevel; }
// Not a comfort value: the preset stays as it is.
void FWowVRComfort::SetGraphicsLevel(int32 Level) { Values().GraphicsLevel = FMath::Clamp(Level, 0, 3); Save(); }
