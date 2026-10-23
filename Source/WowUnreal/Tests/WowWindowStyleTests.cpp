#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "WowWindowStyle.h"
#include "WowFrameManager.h"
#include "WowAssetCache.h"
#include "Mpq/MpqManager.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/GarbageCollection.h"
#include "Widgets/SWowVendorWindow.h"
#include "Widgets/SWowSessionCountdown.h"
#include "Widgets/SWowQuestDialog.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWindowChromeTest,"WowUnreal.UI.WindowStyle.LocalAssetsAndLifetime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowWindowChromeTest::RunTest(const FString&)
{
    FString Data;
    if (!FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),Data))
    { AddError(TEXT("Supply local -minimapdata for original UI asset integration")); return false; }
    FMpqManager Mpq;
    if (!TestTrue(TEXT("User data available"),Mpq.Initialize(Data))) return false;
    ON_SCOPE_EXIT { FWowWindowStyle::Reset(); };
    FWowWindowStyle::Reset();
    const auto* StableButton = FWowWindowStyle::Button();
    auto Frame = SNew(SWowWindowFrame)[SNew(STextBlock).Text(FText::FromString(TEXT("Character")))];
    TWeakObjectPtr<UObject> Border,Button;
    {
        FWowAssetCache Cache;
        FWowFrameManager Frames;
        Frames.SetMpqManager(&Mpq); Frames.SetAssetCache(&Cache);
        FWowWindowStyle::Load(Frames);
        TestTrue(TEXT("Original eight-part border loaded"),FWowWindowStyle::HasOriginalBorder());
        Border = FWowWindowStyle::Edge(0)->GetResourceObject();
        Button = FWowWindowStyle::Button()->Normal.GetResourceObject();
        TestTrue(TEXT("Constructed buttons retain the same style address after data load"),StableButton==FWowWindowStyle::Button());
        TestNotNull(TEXT("Original button texture"),Button.Get());
    }
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Border survives GC after cache owner goes away"),Border.IsValid());
    TestTrue(TEXT("Button survives GC after cache owner goes away"),Button.IsValid());
    Frame->SlatePrepass();
    TestTrue(TEXT("Frame reserves room for decorative edges"),Frame->GetDesiredSize().X > 24);

    // Optional offscreen UI-only render: no world, login, network or visible game.
    FString Preview;
    if (FParse::Value(FCommandLine::Get(),TEXT("windowstylepreview="),Preview))
    {
        auto Vendor = SNew(SWowVendorWindow);
        Vendor->UpdateVendor(1,{});
        auto Quest = SNew(SWowQuestDialog);
        FWowQuestDetails Details;
        Details.QuestGiverName=TEXT("Sten Stoutarm"); Details.Title=TEXT("Dwarven Outfitters");
        Details.Details=TEXT("Quest and gossip retain a readable parchment text area inside the common window frame.");
        Details.Objectives=TEXT("Bring back the supplies.");
        Quest->ShowQuestDetails(Details);
        auto Exit = SNew(SWowSessionCountdown).ShowExit(true)
            .Message(SNew(STextBlock).Text(FText::FromString(TEXT("20 seconds until exit game")))
                .Font(FWowWindowStyle::TitleFont(14)));
        auto Logout = SNew(SWowSessionCountdown).ShowExit(false)
            .Message(SNew(STextBlock).Text(FText::FromString(TEXT("20 seconds until logout")))
                .Font(FWowWindowStyle::TitleFont(14)));
        auto PreviewWidget = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(12)
            [ SNew(SBox).WidthOverride(360).HeightOverride(480)[Vendor] ]
            + SHorizontalBox::Slot().AutoWidth().Padding(12)
            [ SNew(SBox).WidthOverride(420).HeightOverride(480)[Quest] ]
            + SHorizontalBox::Slot().AutoWidth().Padding(12).VAlign(VAlign_Top)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Exit]
                + SVerticalBox::Slot().AutoHeight().Padding(0,18,0,0)[Logout] ];
        FWidgetRenderer Renderer(true);
        UTextureRenderTarget2D* Target=Renderer.DrawWidget(PreviewWidget,FVector2D(1212,504));
        if (!TestNotNull(TEXT("Offscreen render target"),Target)) return false;
        TArray<FColor> Pixels;
        FReadSurfaceDataFlags ReadFlags; ReadFlags.SetLinearToGamma(false);
        TestTrue(TEXT("UI pixels read back"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,ReadFlags));
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(1212,504,Pixels,PNG);
        TestTrue(TEXT("UI preview saved"),FFileHelper::SaveArrayToFile(PNG,*Preview));
    }
    FWowWindowStyle::Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Style releases border texture on UI shutdown"),Border.IsValid());
    TestFalse(TEXT("Style releases button texture on UI shutdown"),Button.IsValid());
    return true;
}
#endif
