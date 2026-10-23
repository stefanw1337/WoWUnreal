#include "WowClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"
#include "WowClientPreferences.h"
namespace { FDelegateHandle StopFrameGenerationOnExit; FTSTicker::FDelegateHandle AdaptiveSyncTicker; }
void FWowClientModule::StartupModule()
{
    StopFrameGenerationOnExit=FCoreDelegates::OnEnginePreExit.AddLambda([]() {
        for (const TCHAR* Name : {TEXT("r.Streamline.DLSSG.Enable"),TEXT("r.FidelityFX.FI.Enabled")})
            if (auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name)) Variable->Set(0,ECVF_SetByCode);
    });
    AdaptiveSyncTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float DeltaTime) {
        FWowClientPreferences::TickAdaptiveSync(DeltaTime);
        return true;
    }));
}
void FWowClientModule::ShutdownModule()
{
    FTSTicker::GetCoreTicker().RemoveTicker(AdaptiveSyncTicker);
    FCoreDelegates::OnEnginePreExit.Remove(StopFrameGenerationOnExit);
}
IMPLEMENT_MODULE(FWowClientModule, WowClient);
