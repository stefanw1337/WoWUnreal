#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "../WowCursorManager.h"
#include "WowClientPreferences.h"
#include "Mpq/MpqManager.h"
#include "UObject/StrongObjectPtr.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowCursorArtworkTest, "WowUnreal.Cursor.LocalArtworkAndHardware",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowCursorArtworkTest::RunTest(const FString& Parameters)
{
    FString Data=FWowClientPreferences::GetDataSource();
    FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),Data);
    FMpqManager Mpq;
    if (!TestTrue(TEXT("External user data opens"), Mpq.Initialize(Data))) return false;
    TStrongObjectPtr<UWowCursorManager> Manager(NewObject<UWowCursorManager>());
    Manager->Initialize(&Mpq, nullptr);
    Manager->LoadCursors();
    TestTrue(TEXT("Original metal glove is available before world login"), Manager->HasOriginalCursors());
    for (int32 Value = 0; Value <= int32(EWowCursorType::Interact); ++Value)
    {
        const EWowCursorType Type = static_cast<EWowCursorType>(Value);
        const auto* Pixels = Manager->GetPixels(Type);
        if (!TestNotNull(TEXT("Every interaction icon has original CPU artwork"), Pixels)) continue;
        TestNotNull(TEXT("Same icon is available for software rendering"), Manager->GetCursorTexture(Type));
        Manager->SetCursorType(Type);
        if (!TestTrue(TEXT("Native icon is created"), Manager->ApplyHardwareCursor(38))) continue;
        ICONINFO Info = {};
        if (!TestTrue(TEXT("Native handle is a valid Windows cursor"), !!::GetIconInfo(static_cast<HCURSOR>(Manager->GetHardwareHandle(Type)), &Info))) continue;
        TestFalse(TEXT("Created as a cursor, not a window icon"), !!Info.fIcon);
        TestEqual(TEXT("Native hotspot X matches software"), int32(Info.xHotspot), 10);
        TestEqual(TEXT("Native hotspot Y matches software"), int32(Info.yHotspot), 5);
        BITMAPINFO Bitmap = {};
        Bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        Bitmap.bmiHeader.biWidth = 38;
        Bitmap.bmiHeader.biHeight = -38;
        Bitmap.bmiHeader.biPlanes = 1;
        Bitmap.bmiHeader.biBitCount = 32;
        Bitmap.bmiHeader.biCompression = BI_RGB;
        TArray<uint8> BGRA; BGRA.SetNumZeroed(38 * 38 * 4);
        HDC DC = ::CreateCompatibleDC(nullptr);
        const int32 Rows = ::GetDIBits(DC, Info.hbmColor, 0, 38, BGRA.GetData(), &Bitmap, DIB_RGB_COLORS);
        ::DeleteDC(DC);
        TestEqual(TEXT("OS cursor bitmap can be read"), Rows, 38);
        bool bColoursMatch = Rows == 38;
        int32 VisiblePixels = 0;
        for (int32 Y = 0; Y < 38 && bColoursMatch; ++Y)
        for (int32 X = 0; X < 38 && bColoursMatch; ++X)
        {
            const uint8* Src = Pixels->RGBA.GetData() + ((Y * Pixels->Height / 38) * Pixels->Width + X * Pixels->Width / 38) * 4;
            const uint8* Dst = BGRA.GetData() + (Y * 38 + X) * 4;
            bColoursMatch = Dst[0] == Src[2] && Dst[1] == Src[1] && Dst[2] == Src[0] && Dst[3] == Src[3];
            if (Src[3] > 0) ++VisiblePixels;
        }
        TestTrue(TEXT("Hardware pixels preserve original colour and alpha"), bColoursMatch);
        TestTrue(TEXT("Native cursor is not empty"), VisiblePixels > 20);
        ::DeleteObject(Info.hbmColor); ::DeleteObject(Info.hbmMask);
    }
    Manager->ReleaseHardwareCursors();
    TestNull(TEXT("Releasing removes native handles"), Manager->GetHardwareHandle(EWowCursorType::Point));
    Manager->SetCursorType(EWowCursorType::Point);
    TestTrue(TEXT("DPI-sized cursor rebuild succeeds after release"), Manager->ApplyHardwareCursor(76));
    Manager->ReleaseHardwareCursors();
    return true;
}
#endif
