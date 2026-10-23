#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "WowScreenshotManager.h"
#include "../WowScreenshotInput.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowScreenshotInputTest,"WowUnreal.Screenshot.Input",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowScreenshotInputTest::RunTest(const FString&)
{
    if(!TestTrue(TEXT("Slate available"),FSlateApplication::IsInitialized()))return false;
    const auto Saved=FWowKeyBindings::Snapshot();
    ON_SCOPE_EXIT { FWowKeyBindings::Restore(Saved); };
    FWowKeyBindings::Bind(TEXT("ALT-F12"),TEXT(""));
    FWowKeyBindings::Bind(TEXT("SHIFT-F12"),TEXT(""));
    FWowKeyBindings::Bind(TEXT("F12"),TEXT("SCREENSHOT"),0);
    int32 Captures=0, Taps=0, Holds=0;
    auto Press=[&](FWowScreenshotInput& Input,FKey Key,bool Alt=false,bool Shift=false,bool Repeat=false)
    {
        FModifierKeysState Mods(Shift,false,false,false,Alt,false,false,false,false);
        return Input.HandleKeyDownEvent(FSlateApplication::Get(),FKeyEvent(Key,Mods,0,Repeat,0,0));
    };
    auto Release=[&](FWowScreenshotInput& Input,FKey Key)
    {
        FModifierKeysState Mods(false,false,false,false,false,false,false,false,false);
        return Input.HandleKeyUpEvent(FSlateApplication::Get(),FKeyEvent(Key,Mods,0,false,0,0));
    };
    // Without photo mode: capture on press, repeats consumed.
    FWowScreenshotInput Plain([&]{++Captures;return true;});
    TestTrue(TEXT("F12 screenshot handled"),Press(Plain,EKeys::F12));
    TestTrue(TEXT("Held key consumed"),Press(Plain,EKeys::F12,false,false,true));
    TestEqual(TEXT("No duplicate capture from repeat"),Captures,1);
    TestFalse(TEXT("Greenshot Alt+F12 passes through"),Press(Plain,EKeys::F12,true));
    TestFalse(TEXT("Shift+F12 passes through"),Press(Plain,EKeys::F12,false,true));
    // With photo mode: the bound key taps on release; hold is time-based (not ticked here).
    FWowScreenshotInput Photo([&]{++Captures;return true;},[&]{++Taps;},[&]{++Holds;});
    TestTrue(TEXT("Bound key down consumed"),Press(Photo,EKeys::F12));
    TestTrue(TEXT("Bound key repeat consumed"),Press(Photo,EKeys::F12,false,false,true));
    TestTrue(TEXT("Bound key up consumed"),Release(Photo,EKeys::F12));
    TestEqual(TEXT("Tap fires once"),Taps,1);
    TestEqual(TEXT("Tap is not a hold"),Holds,0);
    FWowKeyBindings::Bind(TEXT("F11"),TEXT("SCREENSHOT"),0);
    TestFalse(TEXT("Old binding no longer captures"),Press(Plain,EKeys::F12));
    TestTrue(TEXT("Rebound key captures"),Press(Plain,EKeys::F11));
    TestEqual(TEXT("Exactly two captures"),Captures,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowScreenshotLifecycleTest,"WowUnreal.Screenshot.Lifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowScreenshotLifecycleTest::RunTest(const FString&)
{
    if(!TestFalse(TEXT("No external capture pending"),FScreenshotRequest::IsScreenshotRequested()))return false;
    auto* Instance=NewObject<UGameInstance>();
    auto* Service=NewObject<UWowScreenshotManager>(Instance);
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/Screenshot")/FGuid::NewGuid().ToString());
    TArray<FString> Files;
    ON_SCOPE_EXIT
    {
        Service->Deinitialize();
        FScreenshotRequest::Reset();
        for(const auto& File:Files)IFileManager::Get().Delete(*File);
        IFileManager::Get().DeleteDirectory(*Directory,false,false);
    };
    Service->SetNotice(TEXT("Screenshot saved"));
    FString First=Service->BeginRequest(Directory,false);Files.Add(First);
    TestTrue(TEXT("Requested image in chosen directory"),First.StartsWith(Directory));
    TestTrue(TEXT("SDR uses PNG"),First.EndsWith(TEXT(".png")));
    TestTrue(TEXT("Engine request includes UI"),FScreenshotRequest::ShouldShowUI());
    TestTrue(TEXT("Previous toast hidden before capture"),Service->NoticeText.IsEmpty());
    TestEqual(TEXT("In-flight requests coalesced"),Service->BeginRequest(Directory,false),First);
    // Simulate the engine's write -> reset -> processed sequence, without launching a rendered game.
    TArray<uint8> Png;
    FBase64::Decode(TEXT("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aPVkAAAAASUVORK5CYII="),Png);
    TestTrue(TEXT("Fixture file saved"),FFileHelper::SaveArrayToFile(Png,*First));
    FScreenshotRequest::Reset();FScreenshotRequest::OnScreenshotRequestProcessed().Broadcast();
    TestEqual(TEXT("Success shown only after completion"),Service->NoticeText,FString(TEXT("Screenshot saved")));
    const FString Second=Service->BeginRequest(Directory,false);Files.Add(Second);
    TestNotEqual(TEXT("Never overwrite earlier image"),Second,First);
    TestTrue(TEXT("Toast hidden for next screenshot"),Service->NoticeText.IsEmpty());
    AddExpectedError(TEXT("Screenshot capture did not produce a file"),EAutomationExpectedErrorFlags::Contains,1);
    FScreenshotRequest::Reset();FScreenshotRequest::OnScreenshotRequestProcessed().Broadcast();
    TestEqual(TEXT("Failed write does not claim success"),Service->NoticeText,FString(TEXT("Screenshot failed")));
    Service->NoticeExpires=0;Service->TickService(0);
    TestTrue(TEXT("Notice expires"),Service->NoticeText.IsEmpty());
    const FString Hdr=Service->BeginRequest(Directory,true);Files.Add(Hdr);
    TestTrue(TEXT("HDR uses EXR"),Hdr.EndsWith(TEXT(".exr")));
    Service->Deinitialize();
    TestFalse(TEXT("Shutdown cancels owned request"),FScreenshotRequest::IsScreenshotRequested());
    FScreenshotRequest::RequestScreenshot(Directory/TEXT("external.png"),true,false);
    const FString External=FScreenshotRequest::GetFilename();
    TestTrue(TEXT("Foreign capture is not replaced"),Service->BeginRequest(Directory,false).IsEmpty());
    Service->Deinitialize();
    TestEqual(TEXT("Shutdown preserves foreign capture"),FScreenshotRequest::GetFilename(),External);
    return true;
}
#endif
