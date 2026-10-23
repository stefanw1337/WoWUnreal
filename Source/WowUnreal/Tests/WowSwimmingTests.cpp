#include "Misc/AutomationTest.h"
#include "WowAnimationController.h"
#include "WowKeyBindings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowSwimmingTest,"WowUnreal.Water.SwimmingDirectionsAndBindings",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowSwimmingTest::RunTest(const FString&)
{
    using namespace WowMovementFlags;
    auto State=[](float Speed,uint32 Flags){return FWowMovementAnimationPolicy::ResolveSwimmingState(Speed,Flags);};
    TestTrue(TEXT("Idle wins over held input against obstruction"),State(0,FORWARD)==EWowAnimState::SwimmingIdle);
    TestTrue(TEXT("Pure vertical motion swims"),State(80,0)==EWowAnimState::Swimming);
    TestTrue(TEXT("Authored left"),State(200,STRAFE_LEFT)==EWowAnimState::SwimmingLeft);
    TestTrue(TEXT("Authored right"),State(200,STRAFE_RIGHT)==EWowAnimState::SwimmingRight);
    TestTrue(TEXT("Forward left diagonal"),State(200,FORWARD|STRAFE_LEFT)==EWowAnimState::SwimmingForwardLeft);
    TestTrue(TEXT("Forward right diagonal"),State(200,FORWARD|STRAFE_RIGHT)==EWowAnimState::SwimmingForwardRight);
    TestTrue(TEXT("Authored backward"),State(200,BACKWARD)==EWowAnimState::SwimmingBackward);
    TestTrue(TEXT("Backward left diagonal"),State(200,BACKWARD|STRAFE_LEFT)==EWowAnimState::SwimmingBackwardLeft);
    TestTrue(TEXT("Opposite strafes cancel"),State(200,FORWARD|STRAFE_LEFT|STRAFE_RIGHT)==EWowAnimState::Swimming);
    const FString OriginalIni=GGameUserSettingsIni;
    const auto Original=FWowKeyBindings::Snapshot();
    const FString TestIni=FConfigCacheIni::NormalizeConfigIniPath(FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir()/TEXT("Automation")/(TEXT("swimming-")+FGuid::NewGuid().ToString()+TEXT(".ini"))));
    FConfigFile Config; Config.bCanSaveAllSections=true; Config.NoSave=false; GConfig->Add(TestIni,Config);
    GGameUserSettingsIni=TestIni;
    FWowKeyBindings::SetCharacter(TEXT("")); FWowKeyBindings::Reload();
    FWowKeyBindings::UsePreset(2); FWowKeyBindings::ResetPreset();
    TestEqual(TEXT("Right Ctrl default"),FWowKeyBindings::Command(TEXT("RCTRL")),FString(TEXT("SWIMDOWN")));
    FWowKeyBindings::Press(TEXT("RCTRL")); FWowKeyBindings::Press(TEXT("LCTRL"));
    FWowKeyBindings::Release(TEXT("RCTRL"));
    TestTrue(TEXT("Other Ctrl keeps descent held"),FWowKeyBindings::Held(TEXT("SWIMDOWN")));
    FWowKeyBindings::Release(TEXT("LCTRL"));
    TestFalse(TEXT("Release stops descent"),FWowKeyBindings::Held(TEXT("SWIMDOWN")));
    FWowKeyBindings::Unbind(TEXT("SWIMDOWN"),0); FWowKeyBindings::Unbind(TEXT("SWIMDOWN"),1);
    FWowKeyBindings::Save(); FWowKeyBindings::Reload();
    TestTrue(TEXT("Deliberate unbinding survives reload"),FWowKeyBindings::Keys(TEXT("SWIMDOWN")).IsEmpty());
    const TCHAR* Section=TEXT("WowUnreal.KeyBindings.Preset2");
    GConfig->SetBool(Section,TEXT("SwimDownDefaultsV1"),false,TestIni);
    FWowKeyBindings::Reload();
    TestEqual(TEXT("Legacy empty binding migrates"),FWowKeyBindings::Command(TEXT("RCTRL")),FString(TEXT("SWIMDOWN")));
    FWowKeyBindings::Unbind(TEXT("SWIMDOWN"),0); FWowKeyBindings::Unbind(TEXT("SWIMDOWN"),1);
    FWowKeyBindings::Bind(TEXT("RCTRL"),TEXT("JUMP"),1); FWowKeyBindings::Save();
    GConfig->SetBool(Section,TEXT("SwimDownDefaultsV1"),false,TestIni); FWowKeyBindings::Reload();
    TestEqual(TEXT("Migration preserves occupied Ctrl"),FWowKeyBindings::Command(TEXT("RCTRL")),FString(TEXT("JUMP")));
    TestEqual(TEXT("Migration uses remaining Ctrl"),FWowKeyBindings::Command(TEXT("LCTRL")),FString(TEXT("SWIMDOWN")));
    GGameUserSettingsIni=OriginalIni; FWowKeyBindings::Restore(Original);
    GConfig->UnloadFile(TestIni); IFileManager::Get().Delete(*TestIni);
    return true;
}
#endif
