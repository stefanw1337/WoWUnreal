#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowFrameManager.h"
#include "WowCooldownWidget.h"
#include "Components/CanvasPanel.h"
#include "WowClientPreferences.h"
#include "Mpq/MpqManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowCooldownClockTest,"WowUnreal.UI.Cooldown.StableClockAndGlobal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowCooldownClockTest::RunTest(const FString&)
{
    FString Data;FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),Data);
    FMpqManager Mpq;if(!TestTrue(TEXT("Archives mounted"),Mpq.Initialize(Data)))return false;
    FDbcStore::Get().LoadAll(Mpq);FDbcStore::Get().LoadDeferred(Mpq);
    const auto* Might=FDbcStore::Get().Spells().GetById(19740);
    if(!TestNotNull(TEXT("Blessing of Might exists"),Might))return false;
    TestTrue(TEXT("DBC supplies GCD category and duration"),Might->StartRecoveryCategory>0 && Might->StartRecoveryTime>0);
    FWowPacketHandler H;H.EntityManager.LocalPlayerGuid=1;
    H.StartGlobalCooldown(2,19740,1);
    TestEqual(TEXT("Remote casts do not trigger local GCD"),H.GetVisibleSpellCooldown(19740).Duration,0.f);
    H.StartGlobalCooldown(1,19740,1);
    const auto Gcd=H.GetVisibleSpellCooldown(19740);
    TestTrue(TEXT("Other Paladin spell shares GCD"),H.GetVisibleSpellCooldown(21084).Duration>0);
    H.StartGlobalCooldown(1,19740,1);
    TestEqual(TEXT("GO does not restart START cooldown"),H.GetVisibleSpellCooldown(19740).Start,Gcd.Start);
    H.RecordSpellCooldown(19740,30.f);
    const auto Own=H.GetVisibleSpellCooldown(19740);
    TestEqual(TEXT("Long spell cooldown wins over GCD"),Own.Duration,30.f);
    TestEqual(TEXT("Repeated query keeps original duration"),H.GetVisibleSpellCooldown(19740).Duration,30.f);
    H.SpellCooldownWindows[19740].Start-=31.;
    TestEqual(TEXT("Expired spell cooldown reveals remaining GCD"),H.GetVisibleSpellCooldown(19740).Start,Gcd.Start);
    for(auto& P:H.GlobalCooldownWindows) P.Value.Start-=10;
    TestEqual(TEXT("All cooldowns expire"),H.GetVisibleSpellCooldown(19740).Duration,0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowCooldownOverlayTest,"WowUnreal.UI.Cooldown.OverlayLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowCooldownOverlayTest::RunTest(const FString&)
{
    auto* Root=NewObject<UCanvasPanel>();FWowFrameManager Frames;Frames.Initialize(Root);
    FWowFrameDef D;D.Name=TEXT("CooldownFixture");D.Width=36;D.Height=36;
    const int64 Handle=Frames.CreateFrame(D);
    auto* Panel=Cast<UCanvasPanel>(Frames.GetWidgetForHandle(Handle));
    if(!TestNotNull(TEXT("Cooldown canvas"),Panel))return false;
    Frames.SetCooldown(Handle,FPlatformTime::Seconds()-5,10);Frames.TickCooldowns();
    UWowCooldownWidget* Clock=nullptr;
    for(int32 I=0;I<Panel->GetChildrenCount();++I)if(auto* W=Cast<UWowCooldownWidget>(Panel->GetChildAt(I)))Clock=W;
    if(!TestNotNull(TEXT("Clock created above slot art"),Clock))return false;
    TestTrue(TEXT("Clock displays remaining fraction"),Clock->Fraction>.45f && Clock->Fraction<=.5f);
    TestEqual(TEXT("Clock cannot eat clicks"),Clock->GetVisibility(),ESlateVisibility::HitTestInvisible);
    Clock->TakeWidget();
    Frames.SetCooldown(Handle,0,0);
    TestEqual(TEXT("Clearing slot removes clock immediately"),Clock->GetVisibility(),ESlateVisibility::Collapsed);
    Frames.SetCooldown(Handle,FPlatformTime::Seconds()-11,10);Frames.TickCooldowns();
    TestEqual(TEXT("Expired timer stays hidden"),Clock->GetVisibility(),ESlateVisibility::Collapsed);
    return true;
}
#endif
