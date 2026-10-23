#include "WowUnreal.h"
#include "Modules/ModuleManager.h"
#include "WowClientPreferences.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformMisc.h"

void FWowUnrealModule::StartupModule()
{
    if (IsRunningCommandlet()) return;
#if !WITH_EDITOR
    // r493 (Stefan: "a huuuge FPS freeze"): an engine self-check (GPUScene.cpp "GPU Scene Lights is
    // stale", a development-build validation) fired and its ensure report walked the call stack for
    // 5.6 s with the game frozen. The packaged game does not report ensures (the check itself costs
    // nothing then); start it with -ensures to get the reports back for debugging.
    if (GConfig)
        GConfig->SetFloat(TEXT("Core.System"), TEXT("HandleEnsurePercent"),
            FParse::Param(FCommandLine::Get(), TEXT("ensures")) ? 100.f : 0.f, GEngineIni);
    FPlatformMisc::UpdateHotfixableEnsureSettings();
#endif
    if (FParse::Param(FCommandLine::Get(), TEXT("offline")))
    {
        FCommandLine::Append(TEXT(" -offlineflow"));
        FString Scene;
        if (!FParse::Value(FCommandLine::Get(), TEXT("testscene="), Scene))
            FCommandLine::Append(TEXT(" -testscene=login"));
    }
    FString DataPath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("wowdata="), DataPath))
    {
        DataPath = FWowClientPreferences::GetDataSource();
        if (!DataPath.IsEmpty() && !DataPath.Contains(TEXT("\"")))
            FCommandLine::Append(*FString::Printf(TEXT(" -wowdata=\"%s\""), *DataPath));
    }
}
void FWowUnrealModule::ShutdownModule() {}

IMPLEMENT_PRIMARY_GAME_MODULE(FWowUnrealModule, WowUnreal, "WowUnreal");
