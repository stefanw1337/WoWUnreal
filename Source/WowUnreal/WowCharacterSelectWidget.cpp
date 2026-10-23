#include "WowCharacterSelectWidget.h"
#include "WowCookedMaterial.h"
#include "WowNativeWidgetStyle.h"
#include "WowWindowStyle.h"
#include "SWowAddonList.h"
#include "UI/WowWotlkStyle.h"
#include "UI/WowTextCentering.h"
#include "Formats/Dbc/DbcStore.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Input/SEditableText.h"
#include "Brushes/SlateColorBrush.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "Mpq/MpqManager.h"
#include "Formats/BlpParser.h"
#include "WowTextureFactory.h"
#include "WowAddonSettings.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#endif

namespace
{
FSlateFontInfo CharacterListFont(int32 Size)
{
    const FString Path = FPaths::ProjectSavedDir() / TEXT("Fonts/FRIZQT__.TTF");
    return IFileManager::Get().FileExists(*Path)
        ? FSlateFontInfo(Path, Size) : FCoreStyle::GetDefaultFontStyle("Regular", Size);
}

UMaterialInterface* MakeCharacterHighlightMaterial(UTexture2D* Texture)
{
    if (Texture)
    {
        // GlueXML uses ADD: black texels contribute no light and must not obscure the panel.
        static TWeakObjectPtr<UMaterial> SharedBase;
        UMaterial* Base = SharedBase.Get();
        if (!Base) Base = WowCookedMaterial::Load(TEXT("M_WowCharacterHighlight"));
#if WITH_EDITOR
        if (!Base)
        {
            Base = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
            Base->MaterialDomain = MD_UI;
            Base->BlendMode = BLEND_Additive;
            auto* Sample = NewObject<UMaterialExpressionTextureSampleParameter2D>(Base);
            Sample->ParameterName = TEXT("HighlightTexture");
            Sample->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
            Base->GetExpressionCollection().AddExpression(Sample);
            Base->GetEditorOnlyData()->EmissiveColor.Connect(0, Sample);
            Base->PostEditChange();
            Base = WowCookedMaterial::Save(Base, TEXT("M_WowCharacterHighlight"));
            SharedBase = Base;
        }
#endif
        if (!Base) return nullptr;
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
        Material->SetTextureParameterValue(TEXT("HighlightTexture"), Texture);
        return Material;
    }
    return nullptr;
}
}


// r423 (Stefan 18.10: bigger character rows "with the shield for alliance or horde"): the faction
// emblem of the character list, from the client's PvP icon (UI-PVP-Alliance/Horde, emblem in the
// upper-left 40 of its 64 px). Loaded once per faction; nullptr when the data has no such file.
static const FSlateBrush* WowCharacterFactionEmblem(uint8 Race)
{
    int32 Faction = -1;
    switch (Race) { case 1: case 3: case 4: case 7: case 11: Faction = 0; break; case 2: case 5: case 6: case 8: case 10: Faction = 1; break; default: break; }
    if (Faction < 0) return nullptr;
    static TSharedPtr<FSlateBrush> Emblems[2];
    static bool bTried[2] = { false, false };
    if (!bTried[Faction])
    {
        bTried[Faction] = true;
        FMpqManager* Mpq = FWowAddonSettings::GetMpq();
        const FString Path = Faction == 0 ? TEXT("Interface\\TargetingFrame\\UI-PVP-Alliance.blp") : TEXT("Interface\\TargetingFrame\\UI-PVP-Horde.blp");
        TArray<uint8> Data;
        if (Mpq && Mpq->IsInitialized() && Mpq->ReadFile(Path, Data))
        {
            const auto Blp = FBlpParser::Parse(Data);
            if (Blp.bIsValid)
            {
                if (UTexture2D* Texture = FWowTextureFactory::CreateTexture(Blp, Path))
                {
                    Texture->AddToRoot();
                    TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
                    Brush->SetResourceObject(Texture);
                    Brush->ImageSize = FVector2D(46.0f, 46.0f);
                    Brush->DrawAs = ESlateBrushDrawType::Image;
                    Brush->SetUVRegion(FBox2f(FVector2f(0.0f, 0.0f), FVector2f(0.625f, 0.625f)));
                    Emblems[Faction] = Brush;
                }
            }
        }
    }
    return Emblems[Faction].Get();
}

static FString GetRaceName(uint8 Race)
{
    switch (Race)
    {
    case 1: return TEXT("Human");
    case 2: return TEXT("Orc");
    case 3: return TEXT("Dwarf");
    case 4: return TEXT("Night Elf");
    case 5: return TEXT("Undead");
    case 6: return TEXT("Tauren");
    case 7: return TEXT("Gnome");
    case 8: return TEXT("Troll");
    case 10: return TEXT("Blood Elf");
    case 11: return TEXT("Draenei");
    default: return FString::Printf(TEXT("Race %d"), Race);
    }
}

static FString GetClassName(uint8 Class)
{
    switch (Class)
    {
    case 1: return TEXT("Warrior");
    case 2: return TEXT("Paladin");
    case 3: return TEXT("Hunter");
    case 4: return TEXT("Rogue");
    case 5: return TEXT("Priest");
    case 6: return TEXT("Death Knight");
    case 7: return TEXT("Shaman");
    case 8: return TEXT("Mage");
    case 9: return TEXT("Warlock");
    case 11: return TEXT("Druid");
    default: return FString::Printf(TEXT("Class %d"), Class);
    }
}

static FString GetLocationText(const FWowCharacterInfo& Character)
{
    if (Character.ZoneId != 0)
    {
        if (const FAreaTableDbcEntry* Area = FDbcStore::Get().AreaTable().GetById(Character.ZoneId))
        {
            if (!Area->Name.IsEmpty())
            {
                return Area->Name;
            }
        }
    }

    if (const FMapDbcEntry* Map = FDbcStore::Get().Maps().GetById(Character.MapId))
    {
        if (!Map->Name.IsEmpty())
        {
            return Map->Name;
        }
    }

    if (Character.MapId == 0)
    {
        return TEXT("Eastern Kingdoms");
    }

    return FString::Printf(TEXT("Map %u"), Character.MapId);
}



namespace WowCharacterSelectUI
{
    static TSharedRef<SWidget> MakeFramedPanel(const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(14.0f))
    {
        return SNew(SWowWindowFrame).Padding(Padding)[Content];
    }

    static TSharedRef<SWidget> MakeHeader(const FString& Text)
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Text))
            .Justification(ETextJustify::Center)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
            .ColorAndOpacity(FWowWotlkStyle::Gold());
    }

    static TSharedRef<SWidget> MakeLogo()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("WORLD")))
                .Justification(ETextJustify::Center)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 40))
                .ColorAndOpacity(FLinearColor(0.96f, 0.74f, 0.20f, 1.0f))
                .ShadowOffset(FVector2D(2.0f, 2.0f))
                .ShadowColorAndOpacity(FLinearColor::Black)
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("of WARCRAFT")))
                .Justification(ETextJustify::Center)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
                .ColorAndOpacity(FLinearColor(0.96f, 0.74f, 0.20f, 1.0f))
                .ShadowOffset(FVector2D(2.0f, 2.0f))
                .ShadowColorAndOpacity(FLinearColor::Black)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, -1.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Wrath of the Lich King")))
                .Justification(ETextJustify::Center)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                .ColorAndOpacity(FLinearColor(0.70f, 0.90f, 1.0f, 1.0f))
                .ShadowOffset(FVector2D(1.0f, 1.0f))
                .ShadowColorAndOpacity(FLinearColor::Black)
            ];
    }

    static TSharedRef<SWidget> MakeRedButton(const FString& Label, const FOnClicked& OnClicked, float Width = 210.0f, TAttribute<bool> Enabled = true)
    {
        // r288 (Stefan 13.10, Diablo 3 glue patch: "CREATE NEW CHARAC", "DELETE CHARACT"): a wide glue
        // font (Exocet, all capitals) overflowed the button; the caption shrinks until it fits inside the
        // art (like the client's button text), never below 9 pt.
        FSlateFontInfo Font = FWowWindowStyle::TitleFont(14);
        if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
        {
            const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
            for (int32 Size = 14; Size > 9 && Measure->Measure(Label, Font).X > Width - 28.0f; --Size)
                Font = FWowWindowStyle::TitleFont(Size - 1);
        }
        return SNew(SBox)
            .WidthOverride(Width)
            .MinDesiredHeight(30.0f)
            [
                SNew(SButton)
                .ButtonStyle(FWowWindowStyle::Button())
                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                .IsEnabled(Enabled)
                .OnClicked(OnClicked)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .Font(Font)
                    .RenderTransform(WowCapCenterTransform(Font))
                    .ColorAndOpacity(FSlateColor::UseForeground())
                    .ShadowOffset(FVector2D(1,1))
                    .ShadowColorAndOpacity(FLinearColor::Black)
                ]
            ];
    }
}

namespace
{
// Enter must work wherever focus is on the glue screen (the list rows are buttons, the
// viewport may hold focus), but not while typing into a dialog's edit box.
class FWowCharacterSelectEnterKey : public IInputProcessor
{
public:
    explicit FWowCharacterSelectEnterKey(TWeakPtr<SWowCharacterSelectWidget> InOwner) : Owner(InOwner) {}
    virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
    {
        if (InKeyEvent.IsRepeat() || (InKeyEvent.GetKey() != EKeys::Enter && InKeyEvent.GetKey() != EKeys::Virtual_Accept))
            return false;
        const TSharedPtr<SWidget> Focused = SlateApp.GetKeyboardFocusedWidget();
        if (Focused.IsValid() && Focused->GetType() == FName(TEXT("SEditableText"))) return false;
        const TSharedPtr<SWowCharacterSelectWidget> Widget = Owner.Pin();
        return Widget.IsValid() && Widget->HandleEnterKey();
    }
    virtual const TCHAR* GetDebugName() const override { return TEXT("WowCharacterSelectEnterKey"); }
private:
    TWeakPtr<SWowCharacterSelectWidget> Owner;
};
}

SWowCharacterSelectWidget::~SWowCharacterSelectWidget()
{
    if (EnterKeyProcessor.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(EnterKeyProcessor);
}

bool SWowCharacterSelectWidget::HandleEnterKey()
{
    // Only while this screen is actually shown (it stays alive behind dialogs/loading).
    if (!GetVisibility().IsVisible() || GetCachedGeometry().GetLocalSize().X <= 0.f || !GetParentWidget().IsValid())
        return false;
    if (SWowAddonList::IsOpen() || CachedCharacters.IsEmpty() || PendingDeleteGuid != 0) return false;
    EnterSelectedCharacter();
    return true;
}

void SWowCharacterSelectWidget::Construct(const FArguments& InArgs)
{
    if (FSlateApplication::IsInitialized())
    {
        EnterKeyProcessor = MakeShared<FWowCharacterSelectEnterKey>(SharedThis(this));
        FSlateApplication::Get().RegisterInputPreProcessor(EnterKeyProcessor);
    }
    ChildSlot
    [
        SNew(SOverlay)

        // In the 3.3.5 client the selected race environment is the entire
        // glue screen, not a portrait rendered into a black centre panel.
        + SOverlay::Slot()
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Fill)
        [
            SNew(SScaleBox)
            .Stretch(EStretch::ScaleToFill)
            .StretchDirection(EStretchDirection::Both)
            [
                SAssignNew(PreviewImage, SImage)
                .Visibility(EVisibility::Collapsed)
            ]
        ]

        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(FWowWotlkStyle::SolidBrush())
            .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.14f))
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(24.0f, 12.0f, 0.0f, 0.0f)
        [
            SNew(SBox)
            .WidthOverride(330.0f)
            .HeightOverride(180.0f)
            [
                SNew(SOverlay)
                + SOverlay::Slot()
                .HAlign(HAlign_Fill)
                .VAlign(VAlign_Fill)
                [
                    SAssignNew(LogoFallback, SBox)
                    [
                        WowCharacterSelectUI::MakeLogo()
                    ]
                ]
                + SOverlay::Slot()
                .HAlign(HAlign_Fill)
                .VAlign(VAlign_Fill)
                [
                    SAssignNew(LogoImage, SImage)
                    .Visibility(EVisibility::Collapsed)
                ]
            ]
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        [
            SAssignNew(PreviewPlaceholder, STextBlock)
            .Text(FText::FromString(TEXT("Select a character")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
            .ColorAndOpacity(FWowWotlkStyle::GoldDim())
            .ShadowOffset(FVector2D(1.0f, 1.0f))
            .ShadowColorAndOpacity(FLinearColor::Black)
        ]

        // r232 (Stefan 12.10: "more like WotLK"): CharacterSelectCharacterFrame - 260 x 642 at TOPRIGHT
        // -5,-15 in the 768-high glue space (x1.40625 here), the Glue-Tooltip backdrop, the realm's name
        // on top with Change Realm under it, and Create New Character at the bottom of the list.
        + SOverlay::Slot()
        .HAlign(HAlign_Right)
        .VAlign(VAlign_Top)
        .Padding(0.0f, 21.0f, 7.0f, 0.0f)
        [
            SNew(SBox)
            .WidthOverride(430.0f) // r423: room for the bigger character rows (was 366)
            .HeightOverride(903.0f)
            [
                SNew(SWowTooltipFrame)
                .Padding(FMargin(14.0f, 12.0f, 10.0f, 16.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
                    [
                        SAssignNew(RealmNameLabel, STextBlock)
                        .Text(FText::GetEmpty())
                        .Justification(ETextJustify::Center)
                        .Font(FWowNativeWidgetStyle::Heading(22))
                        .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f)) // GlueFontDisableLarge
                        .ShadowOffset(FVector2D(1.0f, 1.0f))
                        .ShadowColorAndOpacity(FLinearColor::Black)
                    ]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 22.0f)
                    [
                        WowCharacterSelectUI::MakeRedButton(
                            TEXT("Change Realm"),
                            FOnClicked::CreateLambda([this]() -> FReply
                            {
                                OnChangeRealmRequest.ExecuteIfBound();
                                return FReply::Handled();
                            }),
                            170.0f, TAttribute<bool>::CreateLambda([this]() { return OnChangeRealmRequest.IsBound(); }))
                    ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SAssignNew(CharListBox, SVerticalBox)
                        ]
                    ]
                    // CharSelectCreateCharacterButton: 200 x 45 at the bottom of the list.
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 8.0f, 0.0f, 0.0f)
                    [
                        WowCharacterSelectUI::MakeRedButton(
                            TEXT("Create New Character"),
                            FOnClicked::CreateLambda([this]() -> FReply
                            {
                                OnCreateCharacterRequest.ExecuteIfBound();
                                return FReply::Handled();
                            }),
                            250.0f)
                    ]
                ]
            ]
        ]

        // r232: a server's own news (Interface\Glues\ServerNews.txt), left, under the logo.
        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(20.0f, 190.0f, 0.0f, 0.0f)
        [
            SAssignNew(ServerNewsPanel, SBox)
            .WidthOverride(480.0f)
            .HeightOverride(550.0f)
            .Visibility(EVisibility::Collapsed)
            [
                SNew(SWowTooltipFrame)
                .Padding(FMargin(16.0f, 10.0f, 10.0f, 12.0f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 8.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Server News")))
                        .Font(FWowNativeWidgetStyle::Heading(22))
                        .ColorAndOpacity(FLinearColor(1.0f, 0.82f, 0.0f))
                        .ShadowOffset(FVector2D(1.0f, 1.0f))
                        .ShadowColorAndOpacity(FLinearColor::Black)
                    ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SAssignNew(ServerNewsLines, SVerticalBox)
                        ]
                    ]
                ]
            ]
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Bottom)
        .Padding(0.0f, 0.0f, 0.0f, 50.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight()
            [
                SAssignNew(SelectedNameLabel, STextBlock)
                .Text(FText::FromString(TEXT("No Character Selected")))
                .Justification(ETextJustify::Center)
                .Font(FWowNativeWidgetStyle::Heading(26))
                .ColorAndOpacity(FWowWotlkStyle::Gold())
                .ShadowOffset(FVector2D(2.0f, 2.0f))
                .ShadowColorAndOpacity(FLinearColor::Black)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 8.0f)
            [
                SAssignNew(SelectedDetailLabel, STextBlock)
                .Text(FText::FromString(TEXT("Choose a character from the list")))
                .Justification(ETextJustify::Center)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                .ColorAndOpacity(FWowWotlkStyle::TextLight())
                .ShadowOffset(FVector2D(1.0f, 1.0f))
                .ShadowColorAndOpacity(FLinearColor::Black)
            ]

            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [
                WowCharacterSelectUI::MakeRedButton(
                    TEXT("Enter World"),
                    FOnClicked::CreateLambda([this]() -> FReply
                    {
                        EnterSelectedCharacter();
                        return FReply::Handled();
                    }),
                    240.0f, TAttribute<bool>::CreateLambda([this](){return SelectedGuid!=0;}))
            ]
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Right)
        .VAlign(VAlign_Bottom)
        .Padding(0.0f, 0.0f, 24.0f, 20.0f)
        [
            SNew(SHorizontalBox)

            // CharacterSelectDeleteButton (165 x 35) left of CharacterSelectBackButton (100 x 35).
            + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
            [
                WowCharacterSelectUI::MakeRedButton(
                    TEXT("Delete Character"),
                    FOnClicked::CreateLambda([this]() -> FReply
                    {
                        DeleteSelectedCharacter();
                        return FReply::Handled();
                    }),
                    210.0f, TAttribute<bool>::CreateLambda([this](){return SelectedGuid!=0;}))
            ]

            + SHorizontalBox::Slot().AutoWidth()
            [
                WowCharacterSelectUI::MakeRedButton(
                    TEXT("Back"),
                    FOnClicked::CreateLambda([this]() -> FReply
                    {
                        OnBackRequest.ExecuteIfBound();
                        return FReply::Handled();
                    }),
                    130.0f)
            ]
        ]

        // "AddOns" (bottom left, as on the original character select screen).
        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Bottom)
        .Padding(24.0f, 0.0f, 0.0f, 52.0f)
        [
            WowCharacterSelectUI::MakeRedButton(
                TEXT("AddOns"),
                FOnClicked::CreateLambda([]() -> FReply
                {
                    SWowAddonList::OpenInViewport(false);
                    return FReply::Handled();
                }),
                150.0f)
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Bottom)
        .Padding(24.0f, 0.0f, 0.0f, 26.0f)
        [
            SAssignNew(StatusLabel, STextBlock)
            .Text(FText::GetEmpty())
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
            .ColorAndOpacity(FWowWotlkStyle::GoldDim())
            .ShadowOffset(FVector2D(1.0f, 1.0f))
            .ShadowColorAndOpacity(FLinearColor::Black)
        ]

        // Delete confirmation.
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, .45f))
            // At the top like the client's dialogs (StaticPopup / CharacterDeleteDialog, TOP 0,-135),
            // with the name typed to confirm (Stefan 10.10).
            .HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.0f, 135.0f, 0.0f, 0.0f))
            .Visibility_Lambda([this]() { return PendingDeleteGuid != 0 ? EVisibility::Visible : EVisibility::Collapsed; })
            [
                WowCharacterSelectUI::MakeFramedPanel(
                    SNew(SBox).WidthOverride(380.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 14.0f)
                        [
                            SNew(STextBlock)
                            .Text_Lambda([this]()
                            {
                                return FText::FromString(FString::Printf(TEXT("Do you want to permanently delete %s?\nType \"DELETE\" into the field to confirm."),
                                    PendingDeleteName.IsEmpty() ? TEXT("this character") : *PendingDeleteName));
                            })
                            .Justification(ETextJustify::Center)
                            .AutoWrapText(true)
                            .Font(FWowWindowStyle::TitleFont(14))
                            .ColorAndOpacity(FLinearColor::White)
                            .ShadowOffset(FVector2D(1.0f, 1.0f))
                            .ShadowColorAndOpacity(FLinearColor::Black)
                        ]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 14.0f)
                        [
                            SNew(SBox).WidthOverride(170.0f).HeightOverride(26.0f)
                            [
                                SNew(SBorder)
                                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f))
                                .Padding(FMargin(6.0f, 2.0f)).VAlign(VAlign_Center)
                                [
                                    SAssignNew(DeleteTextBox, SEditableText)
                                    .Text_Lambda([this]() { return FText::FromString(DeleteTypedText); })
                                    .Font(FWowWindowStyle::TitleFont(14))
                                    .ColorAndOpacity(FLinearColor::White)
                                    .Justification(ETextJustify::Center)
                                    .OnTextChanged_Lambda([this](const FText& Text) { DeleteTypedText = Text.ToString().Left(16); })
                                    .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
                                    {
                                        DeleteTypedText = Text.ToString();
                                        if (Commit == ETextCommit::OnEnter && PendingDeleteGuid != 0 && DeleteTypedText.TrimStartAndEnd().Equals(TEXT("DELETE"), ESearchCase::IgnoreCase))
                                        {
                                            const int64 Guid = PendingDeleteGuid;
                                            PendingDeleteGuid = 0;
                                            DeleteTypedText.Reset();
                                            SetStatusText(TEXT("Deleting character..."));
                                            OnDeleteCharacterRequest.ExecuteIfBound(Guid);
                                        }
                                    })
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
                            [
                                SNew(SBox).IsEnabled_Lambda([this]() { return DeleteTypedText.TrimStartAndEnd().Equals(TEXT("DELETE"), ESearchCase::IgnoreCase); })
                                [
                                WowCharacterSelectUI::MakeRedButton(TEXT("Delete"), FOnClicked::CreateLambda([this]() -> FReply
                                {
                                    if (!DeleteTypedText.TrimStartAndEnd().Equals(TEXT("DELETE"), ESearchCase::IgnoreCase)) return FReply::Handled();
                                    const int64 Guid = PendingDeleteGuid;
                                    PendingDeleteGuid = 0;
                                    DeleteTypedText.Reset();
                                    if (Guid != 0)
                                    {
                                        SetStatusText(TEXT("Deleting character..."));
                                        OnDeleteCharacterRequest.ExecuteIfBound(Guid);
                                    }
                                    return FReply::Handled();
                                }), 130.0f)
                                ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
                            [
                                WowCharacterSelectUI::MakeRedButton(TEXT("Cancel"), FOnClicked::CreateLambda([this]() -> FReply
                                {
                                    PendingDeleteGuid = 0;
                                    DeleteTypedText.Reset();
                                    return FReply::Handled();
                                }), 130.0f)
                            ]
                        ]
                    ], FMargin(20.0f, 16.0f))
            ]
        ]
    ];
}

void SWowCharacterSelectWidget::PopulateCharacters(const TArray<FWowCharacterInfo>& Characters)
{
    CachedCharacters = Characters;
    SelectedIndex = INDEX_NONE;
    HoveredIndex = INDEX_NONE;
    SelectedGuid = 0;

    if (!CharListBox.IsValid())
    {
        return;
    }

    CharListBox->ClearChildren();

    if (Characters.Num() == 0)
    {
        if (SelectedNameLabel.IsValid())
        {
            SelectedNameLabel->SetText(FText::FromString(TEXT("No Characters")));
        }
        if (SelectedDetailLabel.IsValid())
        {
            SelectedDetailLabel->SetVisibility(EVisibility::Visible);
            SelectedDetailLabel->SetText(FText::FromString(TEXT("Create a new character to enter the world.")));
        }
        SetStatusText(TEXT("No characters found."));
        return;
    }

    for (int32 Index = 0; Index < Characters.Num(); ++Index)
    {
        const FWowCharacterInfo Character = Characters[Index];
        const int32 RowIndex = Index;
        const FString NameText = Character.Name.IsEmpty() ? TEXT("Unnamed") : Character.Name;
        // r253 (Stefan 12.10: a dead character should show it in the list): CHARACTER_SELECT_INFO_GHOST,
        // "Level %d %s (Ghost)", and the name in grey.
        // r424: Printf needs a literal format string (UE 5.8 checks it at compile time).
        const FString DetailText = (Character.CharacterFlags & 0x2000)
            ? FString::Printf(TEXT("Level %d %s (Ghost)"), int32(Character.Level), *GetClassName(Character.Class))
            : FString::Printf(TEXT("Level %d %s"), int32(Character.Level), *GetClassName(Character.Class));
        const FString LocationText = GetLocationText(Character);

        // Original WotLK rows are transparent; only hover/selection draws the gold glow.
        // r232: CharSelectCharacterButton rows are 57 glue units apart (80 px here).
        CharListBox->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 2.0f)
        [
            // r234: the row grows with its three text lines (the scroll font is taller than the
            // 78 px the rows had), so the gold selection covers the whole character block (Stefan 12.10).
            // r423 (Stefan 18.10: "Make the selector bigger"): 396 x 104 rows, the selection glow round
            // the whole block with room inside, and the faction emblem on the right.
            SNew(SBox).WidthOverride(396.0f).MinDesiredHeight(104.0f)
            [
                SNew(SButton)
                .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                .ContentPadding(0.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                // Act on the press: after Alt-Tab the input mode is re-applied when the activation
                // click is released, which dropped the button's press, so the first click of a
                // double-click was lost (Stefan 10.10). The double-click time is measured press to press.
                .ClickMethod(EButtonClickMethod::MouseDown)
                .OnHovered_Lambda([this, RowIndex]() { HoveredIndex = RowIndex; })
                .OnUnhovered_Lambda([this, RowIndex]()
                {
                    if (HoveredIndex == RowIndex) HoveredIndex = INDEX_NONE;
                })
                .OnClicked_Lambda([this, RowIndex]() -> FReply
                {
                    // Second click on the same row within the double-click time enters the world.
                    // The time is taken after SelectCharacter: selecting a new row builds its model and
                    // can stall a second, and the second click (queued during the stall) then looked
                    // too late, so it took several double-clicks to enter the world (Stefan 10.10).
                    const double Now = FPlatformTime::Seconds();
                    const bool bDoubleClick = LastClickedIndex == RowIndex && Now - LastClickTime < 0.5;
                    LastClickedIndex = bDoubleClick ? INDEX_NONE : RowIndex;
                    if (!bDoubleClick) SelectCharacter(RowIndex);
                    LastClickTime = FPlatformTime::Seconds();
                    if (bDoubleClick) EnterSelectedCharacter();
                    return FReply::Handled();
                })
                [
                    SNew(SOverlay)
                    + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                    [
                        SNew(SBox)
                        [
                            SNew(SImage)
                            .Image_Lambda([this]() -> const FSlateBrush*
                            {
                                static const FSlateColorBrush Fallback(FLinearColor(0.65f, 0.44f, 0.04f, 0.18f));
                                return CharacterHighlightBrush.IsValid() ? CharacterHighlightBrush.Get() : &Fallback;
                            })
                            .Visibility_Lambda([this, RowIndex]()
                            {
                                return RowIndex == SelectedIndex || RowIndex == HoveredIndex
                                    ? EVisibility::HitTestInvisible : EVisibility::Hidden;
                            })
                        ]
                    ]
                    // Include the texture glow inset around all three text lines.
                    // Content, selection and button now share the same row geometry.
                    + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 26.0f, 0.0f)
                    [
                        SNew(SBox).WidthOverride(46.0f).HeightOverride(46.0f)
                        .Visibility(WowCharacterFactionEmblem(Character.Race) ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
                        [
                            SNew(SImage).Image(WowCharacterFactionEmblem(Character.Race))
                        ]
                    ]
                    + SOverlay::Slot().Padding(30.0f, 16.0f, 84.0f, 16.0f).VAlign(VAlign_Center)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(NameText))
                            .Font(FWowNativeWidgetStyle::Heading(16))
                            .ColorAndOpacity((Character.CharacterFlags & 0x2000) ? FLinearColor(0.5f, 0.5f, 0.5f) : FLinearColor(1.0f, 0.82f, 0.0f))
                            .ShadowOffset(FVector2D(1.0f, 1.0f))
                            .ShadowColorAndOpacity(FLinearColor::Black)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(DetailText))
                            .Font(CharacterListFont(12))
                            .ColorAndOpacity(FLinearColor::White)
                            .ShadowOffset(FVector2D(1.0f, 1.0f))
                            .ShadowColorAndOpacity(FLinearColor::Black)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(LocationText))
                            .Font(CharacterListFont(12))
                            .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
                            .ShadowOffset(FVector2D(1.0f, 1.0f))
                            .ShadowColorAndOpacity(FLinearColor::Black)
                        ]
                    ]
                ]
            ]
        ];
    }

    SelectCharacter(0);
    SetStatusText(FString::Printf(TEXT("%d character(s) available."), Characters.Num()));
}

void SWowCharacterSelectWidget::SetRealmName(const FString& RealmName)
{
    if (RealmNameLabel.IsValid()) RealmNameLabel->SetText(FText::FromString(RealmName));
}

/** One news line: |cAARRGGBB colour runs (until |r) become separate coloured text pieces. */
static TSharedRef<SWidget> MakeServerNewsLine(const FString& Line)
{
    TSharedRef<SWrapBox> Box = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(0.0f, 0.0f));
    const FLinearColor Default = FLinearColor::White;
    FLinearColor Color = Default;
    FString Run;
    auto Flush = [&Box, &Run, &Color]()
    {
        // Word by word so the wrap box can break long runs.
        TArray<FString> Words;
        Run.ParseIntoArray(Words, TEXT(" "), false);
        for (int32 I = 0; I < Words.Num(); ++I)
        {
            const FString Word = Words[I] + (I + 1 < Words.Num() ? TEXT(" ") : TEXT(""));
            if (Word.IsEmpty()) continue;
            Box->AddSlot()
            [
                SNew(STextBlock)
                .Text(FText::FromString(Word))
                .Font(FWowNativeWidgetStyle::Heading(15))
                .ColorAndOpacity(Color)
                .ShadowOffset(FVector2D(1.0f, 1.0f))
                .ShadowColorAndOpacity(FLinearColor::Black)
            ];
        }
        Run.Reset();
    };
    for (int32 I = 0; I < Line.Len(); ++I)
    {
        if (Line[I] == TEXT('|') && I + 1 < Line.Len())
        {
            const TCHAR Code = FChar::ToLower(Line[I + 1]);
            if (Code == TEXT('c') && I + 9 < Line.Len())
            {
                Flush();
                const FString Hex = Line.Mid(I + 4, 6); // |cAARRGGBB: skip the alpha pair
                Color = FLinearColor(FColor::FromHex(Hex));
                I += 9;
                continue;
            }
            if (Code == TEXT('r')) { Flush(); Color = Default; ++I; continue; }
        }
        Run.AppendChar(Line[I]);
    }
    Flush();
    return Box;
}

void SWowCharacterSelectWidget::SetServerNews(const FString& NewsText)
{
    if (!ServerNewsLines.IsValid() || !ServerNewsPanel.IsValid()) return;
    ServerNewsLines->ClearChildren();
    FString Text = NewsText.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("|n"), TEXT("\n"));
    TArray<FString> Lines;
    Text.ParseIntoArray(Lines, TEXT("\n"), false);
    bool bAny = false;
    for (const FString& Line : Lines)
    {
        const bool bBlank = Line.TrimStartAndEnd().IsEmpty();
        bAny |= !bBlank;
        TSharedRef<SWidget> Content = SNullWidget::NullWidget;
        if (bBlank) { TSharedRef<SBox> Spacer = SNew(SBox).HeightOverride(12.0f); Content = Spacer; }
        else Content = MakeServerNewsLine(Line);
        ServerNewsLines->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
        [
            Content
        ];
    }
    ServerNewsPanel->SetVisibility(bAny ? EVisibility::Visible : EVisibility::Collapsed);
}

void SWowCharacterSelectWidget::SelectCharacter(int32 Index)
{
    if (!CachedCharacters.IsValidIndex(Index))
    {
        return;
    }

    SelectedIndex = Index;
    const FWowCharacterInfo& Character = CachedCharacters[Index];
    SelectedGuid = Character.Guid;

    const FString CharacterName = Character.Name.IsEmpty() ? TEXT("Unnamed") : Character.Name;

    if (SelectedNameLabel.IsValid())
    {
        SelectedNameLabel->SetText(FText::FromString(CharacterName));
    }

    if (SelectedDetailLabel.IsValid())
    {
        // The realm list already carries level/race/class/location. The stock
        // Wrath stage keeps the selected name clean above Enter World instead
        // of repeating the same information across the bright backdrop.
        SelectedDetailLabel->SetText(FText::GetEmpty());
        SelectedDetailLabel->SetVisibility(EVisibility::Collapsed);
    }

    OnCharacterHighlighted.ExecuteIfBound(Character);
}

FReply SWowCharacterSelectWidget::OnMouseButtonDown(
    const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && SelectedIndex != INDEX_NONE)
    {
        const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        const float PreviewRightEdge = FMath::Max(0.0f, MyGeometry.GetLocalSize().X - 370.0f);
        if (LocalPosition.X < PreviewRightEdge)
        {
            bRotatingCharacter = true;
            return FReply::Handled().CaptureMouse(AsShared());
        }
    }

    return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SWowCharacterSelectWidget::OnMouseButtonUp(
    const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (bRotatingCharacter && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        bRotatingCharacter = false;
        return FReply::Handled().ReleaseMouseCapture();
    }

    return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
}

FReply SWowCharacterSelectWidget::OnMouseMove(
    const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (bRotatingCharacter && HasMouseCapture())
    {
        const float DeltaX = MouseEvent.GetCursorDelta().X;
        if (!FMath::IsNearlyZero(DeltaX))
        {
            OnCharacterRotated.ExecuteIfBound(-DeltaX * 0.32f);
        }
        return FReply::Handled();
    }

    return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
}

void SWowCharacterSelectWidget::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
    bRotatingCharacter = false;
    SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

void SWowCharacterSelectWidget::SelectCharacterForTesting(int32 Index)
{
    SelectCharacter(Index);
}

void SWowCharacterSelectWidget::EnterSelectedCharacter()
{
    if (SelectedGuid == 0 && CachedCharacters.Num() > 0)
    {
        SelectCharacter(0);
    }

    if (SelectedGuid == 0)
    {
        SetStatusText(TEXT("Select a character first."));
        return;
    }

    OnCharacterSelected.ExecuteIfBound(SelectedGuid);
}

void SWowCharacterSelectWidget::DeleteSelectedCharacter()
{
    if (SelectedGuid == 0)
    {
        SetStatusText(TEXT("Select a character first."));
        return;
    }

    // Ask first, like the client does.
    PendingDeleteGuid = SelectedGuid;
    PendingDeleteName = CachedCharacters.IsValidIndex(SelectedIndex) ? CachedCharacters[SelectedIndex].Name : FString();
    DeleteTypedText.Reset();
    if (DeleteTextBox.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().SetAllUserFocus(DeleteTextBox.ToSharedRef(), EFocusCause::SetDirectly);
}

void SWowCharacterSelectWidget::SetPreviewRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
    if (!PreviewImage.IsValid())
    {
        return;
    }

    if (InRenderTarget)
    {
        PreviewBrush = MakeShared<FSlateBrush>();
        PreviewBrush->SetResourceObject(InRenderTarget);
        PreviewBrush->ImageSize = FVector2D(1920.0f, 1080.0f);
        PreviewBrush->DrawAs = ESlateBrushDrawType::Image;
        PreviewBrush->Tiling = ESlateBrushTileType::NoTile;

        PreviewImage->SetImage(PreviewBrush.Get());
        PreviewImage->SetVisibility(EVisibility::SelfHitTestInvisible);

        if (PreviewPlaceholder.IsValid())
        {
            PreviewPlaceholder->SetVisibility(EVisibility::Collapsed);
        }
    }
}

void SWowCharacterSelectWidget::SetGlueTextures(
    UTexture2D* InLogo, UTexture2D* InCharacterHighlight)
{
    LogoTexture.Reset(InLogo);
    CharacterHighlightTexture.Reset(InCharacterHighlight);

    if (LogoTexture.IsValid() && LogoImage.IsValid())
    {
        LogoBrush = MakeShared<FSlateBrush>();
        LogoBrush->SetResourceObject(LogoTexture.Get());
        LogoBrush->ImageSize = FVector2D(330.0f, 180.0f);
        LogoBrush->DrawAs = ESlateBrushDrawType::Image;
        LogoBrush->Tiling = ESlateBrushTileType::NoTile;
        LogoImage->SetImage(LogoBrush.Get());
        LogoImage->SetVisibility(EVisibility::SelfHitTestInvisible);
        if (LogoFallback.IsValid())
        {
            LogoFallback->SetVisibility(EVisibility::Collapsed);
        }
    }

    CharacterHighlightMaterial.Reset(MakeCharacterHighlightMaterial(InCharacterHighlight));
    CharacterHighlightBrush.Reset();
    if (CharacterHighlightMaterial.IsValid())
    {
        CharacterHighlightBrush = MakeShared<FSlateBrush>();
        CharacterHighlightBrush->SetResourceObject(CharacterHighlightMaterial.Get());
        CharacterHighlightBrush->ImageSize = FVector2D(256.0f, 74.0f);
        CharacterHighlightBrush->DrawAs = ESlateBrushDrawType::Image;
        CharacterHighlightBrush->Tiling = ESlateBrushTileType::NoTile;
    }
}

void SWowCharacterSelectWidget::SetStatusText(const FString& Text)
{
    if (StatusLabel.IsValid())
    {
        StatusLabel->SetText(FText::FromString(Text));
    }
}

void WowPrepareCharacterHighlightMaterial()
{
    MakeCharacterHighlightMaterial(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture")));
}
