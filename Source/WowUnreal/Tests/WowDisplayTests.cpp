#include "Misc/AutomationTest.h"
#include "WowViewportCoordinates.h"
#include "../WowMouseCapturePolicy.h"
#include "WowClientPreferences.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowViewportCoordinatesTest, "WowUnreal.Display.ViewportCoordinates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowViewportCoordinatesTest::RunTest(const FString& Parameters)
{
    for (FIntPoint Pixels : {FIntPoint(1920,1080), FIntPoint(3840,2160)})
    for (float DesktopDPI : {1.f, 1.25f, 1.5f, 2.f})
    for (float GameDPI : {1.f, 2.f})
    for (float UserScale : {0.75f, 1.f, 1.5f})
    {
        const FVector2D Origin(413, 197);
        const FVector2D Extent(Pixels.X, Pixels.Y);
        const FGeometry Viewport = FGeometry::MakeRoot(Extent / DesktopDPI, FSlateLayoutTransform(DesktopDPI, Origin));
        const float DrawScale = GameDPI * UserScale;
        const FGeometry Canvas = FGeometry::MakeRoot(Extent / DrawScale, FSlateLayoutTransform(DrawScale, Origin));
        for (const FVector2D Pixel : {FVector2D(0,0), Extent * 0.5f, Extent - FVector2D(1,1)})
        {
            const FVector2D Local = WowViewportCoordinates::PixelToWidget(Pixel, Pixels, Viewport, Canvas);
            TestTrue(TEXT("Hit test returns the actual painted pixel after all transforms"),
                Canvas.LocalToAbsolute(Local).Equals(Origin + Pixel, 0.01));
            // The software pointer uses its own parent's geometry too.
            const FVector2D Hotspot(10,5);
            const FVector2D Draw = Canvas.AbsoluteToLocal(Origin + Pixel) - Hotspot;
            TestTrue(TEXT("Pointer hotspot remains under the screen cursor"),
                Canvas.LocalToAbsolute(Draw + Hotspot).Equals(Origin + Pixel, 0.01));
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowSharedSettingsTest, "WowUnreal.Display.SharedSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowSharedSettingsTest::RunTest(const FString& Parameters)
{
    FString Fixture;
    if (!FParse::Value(FCommandLine::Get(), TEXT("wowsettingsfixture="), Fixture))
    {
        AddInfo(TEXT("Provide -wowsettingsfixture with a disposable GameUserSettingsINI for persistence testing."));
        return true;
    }
    // UE5.7 uses a short known-config key in GGameUserSettingsIni. Compare
    // the loaded branch's physical file, not that key, before any test writes.
    UGameUserSettings* Settings = GEngine->GetGameUserSettings();
    Settings->LoadSettings(true);
    const FConfigBranch* Branch = GConfig->FindBranch(FName(TEXT("GameUserSettings")), GGameUserSettingsIni);
    const FString ActualPath = Branch ? Branch->IniPath : FString();
    AddInfo(FString::Printf(TEXT("Settings file: %s"), *ActualPath));
    if (!TestTrue(TEXT("Engine uses the launcher's explicit settings file"), FPaths::IsSamePath(ActualPath, Fixture))) return false;
    TGuardValue<bool> RuntimeMode(GIsEditor, false);
    TestEqual(TEXT("Initial window width read from shared INI"), Settings->GetScreenResolution().X, 1920);
    TestTrue(TEXT("Initial mode is windowed"), Settings->GetFullscreenMode() == EWindowMode::Windowed);
    const FString Data = FWowClientPreferences::GetDataSource();
    TestTrue(TEXT("DataSource read from same INI"), FPaths::FileExists(Data / TEXT("common.MPQ")));
    Settings->SetScreenResolution(FIntPoint(1600,900));
    Settings->SetFullscreenMode(EWindowMode::Windowed);
    Settings->ConfirmVideoMode();
    Settings->SaveSettings();
    Settings->SetScreenResolution(FIntPoint(3840,2160));
    Settings->LoadSettings(true);
    TestEqual(TEXT("Saved selection survives a settings reload"), Settings->GetScreenResolution().X, 1600);
    FConfigFile Disk;
    Disk.Read(Fixture);
    int32 Width = 0;
    Disk.GetInt(TEXT("/Script/Engine.GameUserSettings"), TEXT("ResolutionSizeX"), Width);
    TestEqual(TEXT("New width is present on disk, not only in memory"), Width, 1600);
    FString SavedData;
    Disk.GetString(TEXT("WowUnreal.ClientPreferences"), TEXT("DataSource"), SavedData);
    TestEqual(TEXT("Data link survives engine SaveSettings"), SavedData, Data);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowFirstMousePressTest, "WowUnreal.Display.FirstMousePress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowFirstMousePressTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Visible gameplay cursor delivers an uncaptured first press"),
        WowMouseCapturePolicy::Resolve(false,false),EMouseCaptureMode::CaptureDuringMouseDown);
    TestEqual(TEXT("Camera drag retains continuous capture"),
        WowMouseCapturePolicy::Resolve(true,false),EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
    TestEqual(TEXT("Loading/modal suppression does not capture clicks"),
        WowMouseCapturePolicy::Resolve(false,true),EMouseCaptureMode::NoCapture);
    return true;
}
#endif
