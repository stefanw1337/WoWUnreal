#include "WowClientPreferences.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLightingPreferencesTest,
    "WowUnreal.Video.LightingSwitches", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowLightingPreferencesTest::RunTest(const FString&)
{
    FString Fixture;
    if (!FParse::Value(FCommandLine::Get(), TEXT("wowsettingsfixture="), Fixture))
    {
        AddError(TEXT("Lighting persistence test requires a disposable -wowsettingsfixture INI."));
        return false;
    }
    const FConfigBranch* Branch = GConfig->FindBranch(FName(TEXT("GameUserSettings")), GGameUserSettingsIni);
    if (!TestTrue(TEXT("Writes only to the explicitly supplied test INI"),
        Branch && FPaths::IsSamePath(Branch->IniPath, Fixture))) return false;
    TGuardValue<bool> RuntimeMode(GIsEditor, false);
    auto Value = [](const TCHAR* Name)
    {
        const auto* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
        return CVar ? CVar->GetInt() : -1;
    };
    TestFalse(TEXT("Fog defaults off"), FWowClientPreferences::IsVolumetricFogEnabled());
    TestFalse(TEXT("Lumen defaults off"), FWowClientPreferences::IsLumenEnabled());
    TestFalse(TEXT("Hardware RT defaults off"), FWowClientPreferences::IsHardwareRayTracingEnabled());
    TestEqual(TEXT("Gamma defaults to standard 2.2"),FWowClientPreferences::GetGamma(),2.2f);
    FWowClientPreferences::SetGamma(2.6f);
    TestEqual(TEXT("Gamma applies without changing frame generation"),GEngine->DisplayGamma,2.6f);
    FConfigFile GammaDisk;GammaDisk.Read(Fixture);float SavedGamma=0;
    TestTrue(TEXT("Gamma persists to disk"),GammaDisk.GetFloat(TEXT("WowUnreal.ClientPreferences"),TEXT("Gamma"),SavedGamma));
    TestEqual(TEXT("Saved gamma is retained"),SavedGamma,2.6f);
    FWowClientPreferences::SetGamma(99.0f);
    TestEqual(TEXT("Gamma stays in safe display range"),FWowClientPreferences::GetGamma(),3.0f);
    FWowClientPreferences::SetGamma(2.2f);
    FWowClientPreferences::SetVolumetricFogEnabled(true);
    FWowClientPreferences::SetLumenEnabled(true);
    FWowClientPreferences::SetHardwareRayTracingEnabled(true);
    FWowClientPreferences::ApplyAdvancedVideoPreferences();
    TestEqual(TEXT("Fog applies despite previous renderer defaults"), Value(TEXT("r.VolumetricFog")), 1);
    TestEqual(TEXT("Lumen only enables when the renderer supports it"), Value(TEXT("r.DynamicGlobalIlluminationMethod")), FWowClientPreferences::IsLumenAvailable() ? 1 : 0);
    TestEqual(TEXT("Ray tracing only enables on supported hardware"), Value(TEXT("r.RayTracing.Enable")), FWowClientPreferences::IsHardwareRayTracingAvailable() ? 1 : 0);
    TestEqual(TEXT("Runtime HWRT GI evaluates skylight at surface hits"),
        Value(TEXT("r.Lumen.HardwareRayTracing.HitLighting.Skylight")),
        FWowClientPreferences::IsLumenAvailable() && FWowClientPreferences::IsHardwareRayTracingAvailable() ? 1 : 2);
    FConfigFile Disk;
    Disk.Read(Fixture);
    for (const TCHAR* Key : {TEXT("VolumetricFog"), TEXT("Lumen"), TEXT("HardwareRayTracing")})
    {
        bool Enabled = false;
        TestTrue(FString::Printf(TEXT("Preference %s survives a disk read"), Key),
            Disk.GetBool(TEXT("WowUnreal.ClientPreferences"), Key, Enabled) && Enabled);
    }
    FWowClientPreferences::SetVolumetricFogEnabled(false);
    FWowClientPreferences::SetLumenEnabled(false);
    FWowClientPreferences::SetHardwareRayTracingEnabled(false);
    FWowClientPreferences::ApplyAdvancedVideoPreferences();
    TestEqual(TEXT("Fog can be turned back off"), Value(TEXT("r.VolumetricFog")), 0);
    TestEqual(TEXT("Lumen GI can be turned back off"), Value(TEXT("r.DynamicGlobalIlluminationMethod")), 0);
    TestEqual(TEXT("Lumen reflections can be turned back off"), Value(TEXT("r.ReflectionMethod")), 0);
    TestEqual(TEXT("Hardware tracing can be turned back off"), Value(TEXT("r.RayTracing.Enable")), 0);
    TestEqual(TEXT("Ray traced shadows can be turned back off"), Value(TEXT("r.RayTracing.Shadows")), 0);
    TestEqual(TEXT("Disabling HWRT restores default skylight policy"),
        Value(TEXT("r.Lumen.HardwareRayTracing.HitLighting.Skylight")),2);
    return true;
}
#endif
