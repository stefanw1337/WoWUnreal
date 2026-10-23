#include "WowCharacterCreateWidget.h"
#include "WowCharacterAppearance.h"
#include "WowNativeWidgetStyle.h"
#include "UI/WowWotlkStyle.h"
#include "UI/WowTextCentering.h"
#include "Formats/Dbc/DbcStore.h"
#include "InputCoreTypes.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "WowWindowStyle.h"
#include "Formats/Dbc/ChrRacesDbc.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

struct FRaceEntry
{
    const TCHAR* Name;
    uint8 Id;
    bool bAlliance;
    const TCHAR* Summary;
};

struct FClassEntry
{
    const TCHAR* Name;
    uint8 Id;
    const TCHAR* Summary;
};

static const FRaceEntry GRaces[] =
{
    { TEXT("Human"), 1, true, TEXT("- Increased spirit.\n- Bonus reputation gains.\n- Skilled with swords and maces.\n\nAdaptable, resilient, and deeply tied to the Alliance kingdoms.") },
    { TEXT("Dwarf"), 3, true, TEXT("- Stoneform removes harmful effects.\n- Bonus critical strike with guns.\n- Frost resistance.\n\nHardy mountain folk with a long martial tradition.") },
    { TEXT("Night Elf"), 4, true, TEXT("- Shadowmeld.\n- Increased dodge chance.\n- Nature resistance.\n\nAncient guardians of forest and moonlit wilds.") },
    { TEXT("Gnome"), 7, true, TEXT("- Escape Artist.\n- Increased intellect.\n- Arcane resistance.\n\nBrilliant inventors with improbable courage.") },
    { TEXT("Draenei"), 11, true, TEXT("- Gift of the Naaru.\n- Party hit aura.\n- Shadow resistance.\n\nExiles guided by faith, crystals, and stubborn hope.") },
    { TEXT("Orc"), 2, false, TEXT("- Blood Fury.\n- Stun resistance.\n- Command improves pet damage.\n\nFierce survivors with a proud warrior culture.") },
    { TEXT("Undead"), 5, false, TEXT("- Will of the Forsaken.\n- Cannibalize.\n- Shadow resistance.\n\nThe Forsaken endure between death, vengeance, and free will.") },
    { TEXT("Tauren"), 6, false, TEXT("- War Stomp.\n- Increased health.\n- Nature resistance.\n\nNoble plainsfolk with a deep bond to the earth.") },
    { TEXT("Troll"), 8, false, TEXT("- Berserking.\n- Regeneration.\n- Beast damage bonus.\n\nCunning and relentless hunters from ancient tribes.") },
    { TEXT("Blood Elf"), 10, false, TEXT("- Arcane Torrent.\n- Enchanting skill bonus.\n- Magic resistance.\n\nA proud people reshaped by loss, magic, and survival.") }
};

static const FClassEntry GClasses[] =
{
    { TEXT("Warrior"), 1, TEXT("- Role: Tank or Damage.\n- Uses rage.\n- Heavy armor and martial weapons.\n\nWarriors rely on strength, weapons, and battlefield control.") },
    { TEXT("Paladin"), 2, TEXT("- Role: Tank, Healer, or Damage.\n- Uses mana.\n- Plate armor and holy magic.\n\nPaladins blend faith, protection, and righteous force.") },
    { TEXT("Hunter"), 3, TEXT("- Role: Damage.\n- Uses ranged weapons and pets.\n- Strong solo capability.\n\nHunters fight from range and command beasts.") },
    { TEXT("Rogue"), 4, TEXT("- Role: Damage.\n- Uses energy and stealth.\n- Poisons, daggers, and burst damage.\n\nRogues strike from shadows and vanish before payback.") },
    { TEXT("Priest"), 5, TEXT("- Role: Healer or Damage.\n- Uses mana.\n- Holy and shadow magic.\n\nPriests mend allies or unravel enemies with faith and shadow.") },
    { TEXT("Death Knight"), 6, TEXT("- Role: Tank or Damage.\n- Uses runes and runic power.\n- Hero class.\n\nDeath Knights are fallen champions wielding dark martial magic.") },
    { TEXT("Shaman"), 7, TEXT("- Role: Healer or Damage.\n- Uses mana and totems.\n- Elemental, enhancement, and restoration paths.\n\nShamans channel the raw force of the elements.") },
    { TEXT("Mage"), 8, TEXT("- Role: Damage.\n- Uses mana.\n- Fire, frost, and arcane magic.\n\nMages bend reality through disciplined spellcraft.") },
    { TEXT("Warlock"), 9, TEXT("- Role: Damage.\n- Uses mana and demons.\n- Curses, drains, and shadow fire.\n\nWarlocks bargain with dangerous powers and make them useful.") },
    { TEXT("Druid"), 11, TEXT("- Role: Tank, Healer, or Damage.\n- Uses shapeshifting.\n- Nature magic and forms.\n\nDruids adapt through forms, healing, and wild magic.") }
};

static FLinearColor GetFactionColor(bool bAlliance)
{
    return bAlliance ? FLinearColor(0.18f, 0.30f, 0.92f, 0.92f) : FLinearColor(0.70f, 0.02f, 0.02f, 0.92f);
}

static FLinearColor GetClassColor(uint8 Class)
{
    switch (Class)
    {
    case 1: return FLinearColor(0.78f, 0.61f, 0.43f);
    case 2: return FLinearColor(0.96f, 0.55f, 0.73f);
    case 3: return FLinearColor(0.67f, 0.83f, 0.45f);
    case 4: return FLinearColor(1.0f, 0.96f, 0.41f);
    case 5: return FLinearColor(1.0f, 1.0f, 1.0f);
    case 6: return FLinearColor(0.77f, 0.12f, 0.23f);
    case 7: return FLinearColor(0.0f, 0.44f, 0.87f);
    case 8: return FLinearColor(0.41f, 0.80f, 0.94f);
    case 9: return FLinearColor(0.58f, 0.51f, 0.79f);
    case 11: return FLinearColor(1.0f, 0.49f, 0.04f);
    default: return FWowWotlkStyle::TextLight();
    }
}

// Strings tokens for the user's GlueStrings.lua (ABILITY_INFO_<RACE>n, RACE_INFO_<RACE>,
// CLASS_INFO_<CLASS>n, CLASS_<CLASS>), in GRaces / GClasses order.
static const TCHAR* const GRaceTokens[] = { TEXT("HUMAN"), TEXT("DWARF"), TEXT("NIGHTELF"), TEXT("GNOME"), TEXT("DRAENEI"),
    TEXT("ORC"), TEXT("SCOURGE"), TEXT("TAUREN"), TEXT("TROLL"), TEXT("BLOODELF") };
static const TCHAR* const GClassTokens[] = { TEXT("WARRIOR"), TEXT("PALADIN"), TEXT("HUNTER"), TEXT("ROGUE"), TEXT("PRIEST"),
    TEXT("DEATHKNIGHT"), TEXT("SHAMAN"), TEXT("MAGE"), TEXT("WARLOCK"), TEXT("DRUID") };

namespace
{
const FLinearColor CcGold(1.f, .78f, 0.f);
FBox2f CcUV(float L, float R, float T, float B) { return FBox2f(FVector2f(L, T), FVector2f(R, B)); }
const FBox2f CcFull = CcUV(0, 1, 0, 1);

/** FRIZQT__ (GlueFont*) with outline; Slate sizes are points (0.75 of a glue pixel). */
FSlateFontInfo CcFriz(float GluePixels)
{
    FSlateFontInfo Font = FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(GluePixels * .75f)));
    Font.OutlineSettings.OutlineSize = 1;
    Font.OutlineSettings.OutlineColor = FLinearColor::Black;
    return Font;
}
/** The create-screen error line: shown 5 s, faded over the last second. */
float CcErrorAlpha(double ShownAt)
{
    const double Age = FPlatformTime::Seconds() - ShownAt;
    return Age < 4.0 ? 1.f : float(FMath::Clamp(5.0 - Age, 0.0, 1.0));
}
/** MORPHEUS (FactionName_Shadow_Huge, 18). */
FSlateFontInfo CcMorpheus(float GluePixels)
{
    FSlateFontInfo Font = FWowNativeWidgetStyle::Heading(FMath::Max(6, FMath::RoundToInt(GluePixels * .75f)));
    Font.OutlineSettings.OutlineSize = 1;
    Font.OutlineSettings.OutlineColor = FLinearColor::Black;
    return Font;
}
void CcPlace(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget)
{
    Canvas.AddSlot().Position(FVector2D(X, Y)).Size(FVector2D(W, H))[Widget];
}
}

const FSlateBrush* SWowCharacterCreateWidget::TexBrush(const FString& Path, FBox2f UV) const
{
    const FString Key = FString::Printf(TEXT("%s|%.4f,%.4f,%.4f,%.4f"), *Path, UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
    if (const TSharedPtr<FSlateBrush>* Found = GlueBrushes.Find(Key)) return Found->Get();
    UTexture2D* Texture = nullptr;
    if (const TStrongObjectPtr<UTexture2D>* Loaded = PathTextures.Find(Path)) Texture = Loaded->Get();
    else if (!MissingPaths.Contains(Path) && TextureLoader)
    {
        Texture = TextureLoader(Path);
        if (Texture) PathTextures.Add(Path, TStrongObjectPtr<UTexture2D>(Texture));
        else MissingPaths.Add(Path);
    }
    if (!Texture) return nullptr;
    auto Brush = MakeShared<FSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    Brush->SetUVRegion(UV);
    GlueBrushes.Add(Key, Brush);
    return &Brush.Get(); // TSharedRef::Get() is a reference
}

TSharedRef<SWidget> SWowCharacterCreateWidget::TexImage(const FString& Path, FBox2f UV, TAttribute<FSlateColor> Tint) const
{
    return SNew(SImage).Visibility(EVisibility::HitTestInvisible).ColorAndOpacity(Tint)
        .Image_Lambda([this, Path, UV]() { return TexBrush(Path, UV); });
}

TSharedRef<SWidget> SWowCharacterCreateWidget::TexAdd(const FString& Path, FBox2f UV, TAttribute<FLinearColor> Tint) const
{
    return SNew(SWowAddImage).Visibility(EVisibility::HitTestInvisible).ColorAndOpacity(Tint)
        .Image_Lambda([this, Path, UV]() { return TexBrush(Path, UV); });
}

FString SWowCharacterCreateWidget::Glue(const FString& Token, const TCHAR* Fallback) const
{
    if (const FString* Found = GlueStrings.Find(Token)) return *Found;
    return Fallback ? FString(Fallback) : FString();
}

/** GetFlavorText: the _FEMALE variant for female characters when the locale has one. */
FString SWowCharacterCreateWidget::Flavor(const FString& Token) const
{
    if (SelectedGender == 1)
        if (const FString* Female = GlueStrings.Find(Token + TEXT("_FEMALE"))) return *Female;
    return Glue(Token);
}

FString SWowCharacterCreateWidget::AbilityText(bool bRace) const
{
    // SetCharacterRace starts at ABILITY_INFO_<RACE>1, SetCharacterClass at CLASS_INFO_<CLASS>0.
    const FString Prefix = bRace ? FString::Printf(TEXT("ABILITY_INFO_%s"), GRaceTokens[SelectedRaceIndex])
        : FString::Printf(TEXT("CLASS_INFO_%s"), GClassTokens[SelectedClassIndex]);
    FString Result;
    for (int32 Index = bRace ? 1 : 0; ; ++Index)
    {
        const FString* Line = GlueStrings.Find(Prefix + FString::FromInt(Index));
        if (!Line) break;
        Result += *Line + TEXT("\n\n");
    }
    if (Result.IsEmpty()) Result = bRace ? GRaces[SelectedRaceIndex].Summary : GClasses[SelectedClassIndex].Summary;
    return Result;
}

FString SWowCharacterCreateWidget::CustomizationLabel(int32 Field) const
{
    // CharacterCreate_UpdateHairCustomization: HAIR_<x>_STYLE / HAIR_<x>_COLOR / FACIAL_HAIR_<x> from ChrRaces.
    const FChrRacesDbcEntry* Race = FDbcStore::Get().ChrRaces().GetById(GRaces[SelectedRaceIndex].Id);
    const FString Hair = Race && !Race->HairCustomization.IsEmpty() ? Race->HairCustomization.ToUpper() : FString(TEXT("NORMAL"));
    const FString Facial = Race ? (SelectedGender == 1 ? Race->FacialHairCustomization2 : Race->FacialHairCustomization1).ToUpper() : FString();
    switch (Field)
    {
    case 0: return Glue(TEXT("CHAR_CUSTOMIZATION1_DESC"), TEXT("Skin Color"));
    case 1: return Glue(TEXT("CHAR_CUSTOMIZATION2_DESC"), TEXT("Face"));
    case 2: return Glue(FString::Printf(TEXT("HAIR_%s_STYLE"), *Hair), TEXT("Hair Style"));
    case 3: return Glue(FString::Printf(TEXT("HAIR_%s_COLOR"), *Hair), TEXT("Hair Color"));
    default: return Glue(FString::Printf(TEXT("FACIAL_HAIR_%s"), *(Facial.IsEmpty() ? FString(TEXT("NORMAL")) : Facial)), TEXT("Facial Hair"));
    }
}

FLinearColor SWowCharacterCreateWidget::BackdropColor() const
{
    // FACTION_BACKDROP_COLOR_TABLE
    return GRaces[SelectedRaceIndex].bAlliance ? FLinearColor(.09f, .09f, .19f) : FLinearColor(.19f, .05f, .05f);
}

/** CharacterCreateIconButtonTemplate: 38x38 icon, 58x58 shadow, bevel, ADD hover/checked, dark when disabled. */
TSharedRef<SWidget> SWowCharacterCreateWidget::IconButton(const FString& Tooltip, TFunction<const FSlateBrush*()> Image,
    TFunction<bool()> Checked, TFunction<bool()> Enabled, TFunction<void()> Click)
{
    const FString Art = TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Highlights");
    TSharedPtr<SButton> Button;
    TSharedRef<SOverlay> Overlay = SNew(SOverlay);
    Overlay->AddSlot()
    [SAssignNew(Button, SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
        .ToolTipText(FText::FromString(Tooltip))
        .OnClicked_Lambda([Click, Enabled]() { if (Enabled()) Click(); return FReply::Handled(); })
        [SNew(SImage).Image_Lambda([Image]() { return Image(); })]];
    const TWeakPtr<SButton> Weak = Button;
    Overlay->AddSlot()[TexImage(Art, CcUV(0, .5f, .5f, 1))];
    Overlay->AddSlot()
    [SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .ColorAndOpacity_Lambda([Enabled]() { return FSlateColor(FLinearColor(0, 0, 0, Enabled() ? 0.f : .75f)); })];
    Overlay->AddSlot()
    [TexAdd(Art, CcUV(0, .5f, 0, .5f), TAttribute<FLinearColor>::CreateLambda([Weak]()
        { const auto B = Weak.Pin(); return B.IsValid() && B->IsHovered() ? FLinearColor::White : FLinearColor::Transparent; }))];
    Overlay->AddSlot()
    [TexAdd(Art, CcUV(.5f, 1, 0, .5f), TAttribute<FLinearColor>::CreateLambda([Checked]()
        { return Checked() ? FLinearColor::White : FLinearColor::Transparent; }))];
    return SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(-10)
        [SNew(SBox).WidthOverride(58).HeightOverride(58)
            [TexImage(TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-IconShadow"), CcFull, FSlateColor(FLinearColor(1, 1, 1, .85f)))]]
        + SOverlay::Slot()[Overlay];
}

/** GlueButtonTemplate / GlueButtonSmallTemplate: red Glue-Panel-Button art. */
TSharedRef<SWidget> SWowCharacterCreateWidget::GlueButton(const FString& Text, float FontPixels, TFunction<void()> Click)
{
    const FString Base = TEXT("Interface\\Glues\\Common\\Glue-Panel-Button");
    auto Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
    // Text centred on the painted art (WowGlueLabelTransform); pressed pushes it 1px right/down.
    Style->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(2, 2, 0, 0));
    ButtonStyles.Add(Style);
    TSharedPtr<SButton> Button;
    SAssignNew(Button, SButton).ButtonStyle(&Style.Get()).HAlign(HAlign_Center).VAlign(VAlign_Center)
        .OnClicked_Lambda([Click]() { Click(); return FReply::Handled(); });
    const TWeakPtr<SButton> Weak = Button;
    Button->SetContent(SNew(STextBlock).Text(FText::FromString(Text)).Font(CcFriz(FontPixels)).ShadowOffset(FVector2D(1, 1))
        .RenderTransform(WowGlueLabelTransform(Weak, CcFriz(FontPixels)))
        .ColorAndOpacity_Lambda([Weak]() { const auto B = Weak.Pin(); return FSlateColor(B.IsValid() && B->IsHovered() ? FLinearColor::White : CcGold); }));
    return SNew(SOverlay)
        + SOverlay::Slot()
        [SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, Base, Weak]()
            { const auto B = Weak.Pin(); return TexBrush(Base + (B.IsValid() && B->IsPressed() ? TEXT("-Down") : TEXT("-Up")), CcUV(0, .578125f, 0, .75f)); })]
        + SOverlay::Slot()[Button.ToSharedRef()]
        + SOverlay::Slot()
        [TexAdd(Base + TEXT("-Highlight"), CcUV(0, .625f, 0, .6875f), TAttribute<FLinearColor>::CreateLambda([Weak]()
            { const auto B = Weak.Pin(); return B.IsValid() && B->IsHovered() ? FLinearColor::White : FLinearColor::Transparent; }))];
}

/** Glue-LeftArrow / Glue-RightArrow 32x32 buttons. */
TSharedRef<SWidget> SWowCharacterCreateWidget::ArrowButton(bool bRight, TFunction<void()> Click)
{
    const FString Base = bRight ? TEXT("Interface\\Glues\\Common\\Glue-RightArrow-Button") : TEXT("Interface\\Glues\\Common\\Glue-LeftArrow-Button");
    TSharedPtr<SButton> Button;
    SAssignNew(Button, SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
        .OnClicked_Lambda([Click]() { Click(); return FReply::Handled(); });
    const TWeakPtr<SButton> Weak = Button;
    Button->SetContent(SNew(SImage).Image_Lambda([this, Base, Weak]()
        { const auto B = Weak.Pin(); return TexBrush(Base + (B.IsValid() && B->IsPressed() ? TEXT("-Down") : TEXT("-Up")), CcFull); }));
    return SNew(SOverlay)
        + SOverlay::Slot()[Button.ToSharedRef()]
        + SOverlay::Slot()
        [TexAdd(Base + TEXT("-Highlight"), CcFull, TAttribute<FLinearColor>::CreateLambda([Weak]()
            { const auto B = Weak.Pin(); return B.IsValid() && B->IsHovered() ? FLinearColor::White : FLinearColor::Transparent; }))];
}

/** CharacterCreateCharacterRace / CharacterCreateCharacterClass info box (267x287). */
TSharedRef<SWidget> SWowCharacterCreateWidget::InfoBox(bool bRace)
{
    TSharedRef<SCanvas> Box = SNew(SCanvas);
    // Backdrop: UI-Tooltip-Background, insets l12 r3 t20 b6, tinted with the faction backdrop color.
    CcPlace(*Box, 12, 20, 267 - 15, 287 - 26,
        TexImage(TEXT("Interface\\Tooltips\\UI-Tooltip-Background"), CcFull,
            TAttribute<FSlateColor>::CreateLambda([this]() { return FSlateColor(BackdropColor()); })));
    // Round race / class icon under the frame art.
    CcPlace(*Box, 4, 4, 48, 48,
        SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, bRace]()
        {
            if (bRace)
            {
                const int32 Columns[] = {0, 1, 3, 2, 4, 3, 1, 0, 2, 4};
                const float X = Columns[SelectedRaceIndex] * .125f;
                const float Y = (GRaces[SelectedRaceIndex].bAlliance ? 0.f : .25f) + SelectedGender * .5f;
                return TexBrush(TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-RacesRound"), CcUV(X, X + .125f, Y, Y + .25f));
            }
            const int32 Columns[] = {0, 0, 0, 2, 2, 1, 1, 1, 3, 3};
            const int32 Rows[] = {0, 2, 1, 0, 1, 2, 1, 0, 1, 0};
            const float Left[] = {0, .25f, .49609375f, .7421875f};
            const float Right[] = {.25f, .49609375f, .7421875f, .98828125f};
            const int32 C = Columns[SelectedClassIndex];
            return TexBrush(TEXT("Interface\\TargetingFrame\\UI-Classes-Circles"), CcUV(Left[C], Right[C], Rows[SelectedClassIndex] * .25f, Rows[SelectedClassIndex] * .25f + .25f));
        }));
    CcPlace(*Box, 0, 0, 267, 287, TexImage(TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-InfoBox"),
        CcUV(.005859375f, .52734375f, .005859375f, .56640625f)));
    // CharacterCreateRaceLabel / ClassLabel: FactionName_Shadow_Huge, TOP +40,-10, 220 wide, left.
    CcPlace(*Box, 267 * .5f + 40 - 110, 10, 220, 24,
        SNew(STextBlock).Font(CcMorpheus(18)).ShadowOffset(FVector2D(1, 1)).ColorAndOpacity(FLinearColor::White)
            .Text_Lambda([this, bRace]() { return FText::FromString(bRace ? FString(GRaces[SelectedRaceIndex].Name) : FString(GClasses[SelectedClassIndex].Name)); }));
    // Scroll frame (20,55) 220x220: abilities in GlueFontNormalSmall, then the flavor text in GlueFontCharacterCreate.
    TSharedPtr<SScrollBox>& Scroll = bRace ? RaceScroll : ClassScroll;
    CcPlace(*Box, 20, 55, 220, 220,
        SAssignNew(Scroll, SScrollBox).ScrollBarVisibility(EVisibility::Collapsed).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        + SScrollBox::Slot()
        [SNew(SBox).WidthOverride(220)
            [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Font(CcFriz(12)).ColorAndOpacity(CcGold).ShadowOffset(FVector2D(1, 1)).AutoWrapText(true)
                .Text_Lambda([this, bRace]() { return FText::FromString(AbilityText(bRace)); })]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
            [SNew(STextBlock).Font(CcFriz(12)).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1, 1)).AutoWrapText(true)
                .LineHeightPercentage(1.1f)
                .Text_Lambda([this, bRace]() { return FText::FromString(bRace
                    ? Flavor(FString(TEXT("RACE_INFO_")) + GRaceTokens[SelectedRaceIndex])
                    : Flavor(FString(TEXT("CLASS_")) + GClassTokens[SelectedClassIndex])); })]]]);
    // GlueScrollFrameTemplate bar: TOPLEFT at the frame's TOPRIGHT +7,+4, BOTTOMLEFT +7,-12; arrows outside it.
    const TWeakPtr<SScrollBox> WeakScroll = Scroll;
    auto Range = [WeakScroll]() { const auto S = WeakScroll.Pin(); return S.IsValid() ? FMath::Max(0.f, S->GetScrollOffsetOfEnd()) : 0.f; };
    auto Offset = [WeakScroll]() { const auto S = WeakScroll.Pin(); return S.IsValid() ? S->GetScrollOffset() : 0.f; };
    auto ScrollArrow = [this, WeakScroll, Range, Offset](bool bDown) -> TSharedRef<SWidget>
    {
        const FString Base = bDown ? TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton") : TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton");
        const FBox2f Inner = CcUV(.23f, .77f, .23f, .77f);
        auto Enabled = [bDown, Range, Offset]() { return bDown ? Offset() < Range() - .5f : Offset() > .5f; };
        TSharedPtr<SButton> Button;
        SAssignNew(Button, SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
            .IsEnabled_Lambda(Enabled)
            .OnClicked_Lambda([WeakScroll, bDown]()
            {
                if (const auto S = WeakScroll.Pin()) S->SetScrollOffset(FMath::Max(0.f, S->GetScrollOffset() + (bDown ? 24.f : -24.f)));
                return FReply::Handled();
            });
        const TWeakPtr<SButton> Weak = Button;
        Button->SetContent(SNew(SImage).Image_Lambda([this, Base, Inner, Weak, Enabled]()
        {
            const auto B = Weak.Pin();
            const TCHAR* State = !Enabled() ? TEXT("-Disabled") : B.IsValid() && B->IsPressed() ? TEXT("-Down") : TEXT("-Up");
            return TexBrush(Base + State, Inner);
        }));
        return SNew(SOverlay)
            + SOverlay::Slot()[Button.ToSharedRef()]
            + SOverlay::Slot()
            [TexAdd(Base + TEXT("-Highlight"), Inner, TAttribute<FLinearColor>::CreateLambda([Weak, Enabled]()
                { const auto B = Weak.Pin(); return B.IsValid() && B->IsHovered() && Enabled() ? FLinearColor::White : FLinearColor::Transparent; }))];
    };
    const float BarX = 20 + 220 + 7, BarTop = 55 - 4 + 16, BarBottom = 55 + 220 - 12 - 16;
    CcPlace(*Box, BarX, BarTop - 16, 16, 16, ScrollArrow(false));
    CcPlace(*Box, BarX, BarBottom, 16, 16, ScrollArrow(true));
    TSharedRef<SCanvas> Track = SNew(SCanvas);
    Track->AddSlot()
        .Position_Lambda([Range, Offset, BarTop, BarBottom]()
        {
            const float Travel = BarBottom - BarTop - 16.f;
            const float R = Range();
            return FVector2D(0, R > .5f ? Travel * FMath::Clamp(Offset() / R, 0.f, 1.f) : 0.f);
        })
        .Size(FVector2D(16, 16))
        [SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this]()
            { return TexBrush(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"), CcUV(.25f, .75f, .25f, .75f)); })];
    CcPlace(*Box, BarX, BarTop, 16, BarBottom - BarTop, Track);
    return SNew(SBox).WidthOverride(267).HeightOverride(287)[Box];
}

namespace
{
// The last unfinished character (kept for this game session): Back, then Create New
// Character again, brings it back instead of starting over.
struct FCcDraft
{
    bool bValid = false;
    int32 Race = 0, Class = 0, Gender = 0, Skin = 0, Face = 0, HairStyle = 0, HairColor = 0, FacialHair = 0;
    FString Name;
};
FCcDraft GCcDraft;
// Looks per race and gender (key RaceIndex * 2 + Gender), kept for the game session.
struct FCcLooks { int32 Skin = 0, Face = 0, HairStyle = 0, HairColor = 0, FacialHair = 0; };
TMap<int32, FCcLooks> GCcLooks;
}

void SWowCharacterCreateWidget::StoreLooks() const
{
    FCcLooks& Looks = GCcLooks.FindOrAdd(SelectedRaceIndex * 2 + SelectedGender);
    Looks.Skin = SelectedSkin; Looks.Face = SelectedFace; Looks.HairStyle = SelectedHairStyle;
    Looks.HairColor = SelectedHairColor; Looks.FacialHair = SelectedFacialHair;
}

void SWowCharacterCreateWidget::LoadLooks()
{
    const FCcLooks Looks = GCcLooks.FindRef(SelectedRaceIndex * 2 + SelectedGender);
    SelectedSkin = Looks.Skin; SelectedFace = Looks.Face; SelectedHairStyle = Looks.HairStyle;
    SelectedHairColor = Looks.HairColor; SelectedFacialHair = Looks.FacialHair;
}

void SWowCharacterCreateWidget::SaveDraft() const
{
    GCcDraft.bValid = true;
    GCcDraft.Race = SelectedRaceIndex; GCcDraft.Class = SelectedClassIndex; GCcDraft.Gender = SelectedGender;
    GCcDraft.Skin = SelectedSkin; GCcDraft.Face = SelectedFace; GCcDraft.HairStyle = SelectedHairStyle;
    GCcDraft.HairColor = SelectedHairColor; GCcDraft.FacialHair = SelectedFacialHair;
    GCcDraft.Name = NameBox.IsValid() ? NameBox->GetText().ToString() : FString();
    StoreLooks();
}

void SWowCharacterCreateWidget::RestoreDraft()
{
    if (!GCcDraft.bValid) return;
    SelectedRaceIndex = GCcDraft.Race; SelectedClassIndex = GCcDraft.Class; SelectedGender = GCcDraft.Gender;
    SelectedSkin = GCcDraft.Skin; SelectedFace = GCcDraft.Face; SelectedHairStyle = GCcDraft.HairStyle;
    SelectedHairColor = GCcDraft.HairColor; SelectedFacialHair = GCcDraft.FacialHair;
    if (NameBox.IsValid()) NameBox->SetText(FText::FromString(GCcDraft.Name));
}

void SWowCharacterCreateWidget::Construct(const FArguments& InArgs)
{
    // ── CharacterCreateConfigurationFrame: 256x758 at TOPLEFT +9,-5. Everything fits; no scrolling.
    TSharedRef<SCanvas> Config = SNew(SCanvas);
    const FString Border = TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-OuterBorder");
    CcPlace(*Config, 10, 8, 236, 742, TexImage(TEXT("Interface\\Tooltips\\UI-Tooltip-Background"), CcFull,
        TAttribute<FSlateColor>::CreateLambda([this]() { return FSlateColor(BackdropColor()); })));
    CcPlace(*Config, 0, 5, 256, 512, TexImage(TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Banners"), CcFull));
    CcPlace(*Config, 0, 0, 256, 256, TexImage(Border, CcUV(0, 1, 1, 0)));
    CcPlace(*Config, 0, 256, 256, 246, TexImage(Border, CcUV(0, 1, 0, .9609375f)));
    CcPlace(*Config, 0, 502, 256, 256, TexImage(Border, CcFull));

    auto Centered = [](const FString& Text, FSlateFontInfo Font, FLinearColor Color) -> TSharedRef<SWidget>
    {
        return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font).ColorAndOpacity(Color)
            .ShadowOffset(FVector2D(1, 1)).Justification(ETextJustify::Center);
    };
    CcPlace(*Config, 128 - 50 - 60, 20, 120, 24, Centered(Glue(TEXT("ALLIANCE"), TEXT("Alliance")), CcMorpheus(18), FLinearColor::White));
    CcPlace(*Config, 128 + 50 - 60, 20, 120, 24, Centered(Glue(TEXT("HORDE"), TEXT("Horde")), CcMorpheus(18), FLinearColor::White));

    // Race buttons: 38x38, columns centred at ±50, first at y 61, then 21 apart.
    int32 AllianceRow = 0, HordeRow = 0;
    for (int32 I = 0; I < UE_ARRAY_COUNT(GRaces); ++I)
    {
        const bool bAlliance = GRaces[I].bAlliance;
        const int32 Row = bAlliance ? AllianceRow++ : HordeRow++;
        const float X = 128.f + (bAlliance ? -50.f : 50.f) - 19.f;
        const float Y = 61.f + Row * 59.f;
        CcPlace(*Config, X, Y, 38, 38, IconButton(GRaces[I].Name,
            [this, I]() { return GlueBrush(FString::Printf(TEXT("Race%d_%d"), I, SelectedGender)); },
            [this, I]() { return SelectedRaceIndex == I; }, []() { return true; },
            [this, I]() { SetSelectedRaceIndex(I); }));
        // Only the selected race shows its name, above the icon (GlueFontNormalSmall).
        CcPlace(*Config, X - 41, Y - 16, 120, 14,
            SNew(STextBlock).Font(CcFriz(12)).ColorAndOpacity(CcGold).ShadowOffset(FVector2D(1, 1)).Justification(ETextJustify::Center)
                .Text_Lambda([this, I]() { return FText::FromString(SelectedRaceIndex == I ? FString(GRaces[I].Name) : FString()); }));
    }

    // Gender: label bottom at 388, buttons at 392 (centres ±20).
    CcPlace(*Config, 0, 370, 256, 18,
        SNew(STextBlock).Font(CcFriz(15)).ColorAndOpacity(CcGold).ShadowOffset(FVector2D(1, 1)).Justification(ETextJustify::Center)
            .Text_Lambda([this]() { return FText::FromString(SelectedGender == 0 ? Glue(TEXT("MALE"), TEXT("Male")) : Glue(TEXT("FEMALE"), TEXT("Female"))); }));
    for (int32 Gender = 0; Gender < 2; ++Gender)
        CcPlace(*Config, 128 + (Gender ? 20 : -20) - 19, 392, 38, 38, IconButton(Gender ? TEXT("Female") : TEXT("Male"),
            [this, Gender]() { return GlueBrush(FString::Printf(TEXT("Gender%d"), Gender)); },
            [this, Gender]() { return SelectedGender == Gender; }, []() { return true; },
            [this, Gender]() { if (SelectedGender != Gender) ToggleGender(); }));

    // Class: name bottom at 456, buttons from y 463, 5 per row, 44 apart.
    CcPlace(*Config, 0, 438, 256, 18,
        SNew(STextBlock).Font(CcFriz(15)).ColorAndOpacity(CcGold).ShadowOffset(FVector2D(1, 1)).Justification(ETextJustify::Center)
            .Text_Lambda([this]() { return FText::FromString(GClasses[SelectedClassIndex].Name); }));
    for (int32 I = 0; I < UE_ARRAY_COUNT(GClasses); ++I)
        CcPlace(*Config, 128 - 90 - 19 + (I % 5) * 44, 463 + (I / 5) * 44, 38, 38, IconButton(GClasses[I].Name,
            [this, I]() { return GlueBrush(FString::Printf(TEXT("Class%d"), I)); },
            [this, I]() { return SelectedClassIndex == I; },
            [this, I]() { return IsClassAllowedForRace(GRaces[SelectedRaceIndex].Id, GClasses[I].Id); },
            [this, I]() { SetSelectedClassIndex(I); }));

    // CharacterCustomizationButtonFrame1-5 (230x32) above the Randomize button.
    const FString Label = TEXT("Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame");
    for (int32 Field = 0; Field < 5; ++Field)
    {
        TSharedRef<SCanvas> Row = SNew(SCanvas);
        CcPlace(*Row, -5, -16, 25, 64, TexImage(Label, CcUV(0, .1953125f, 0, 1)));
        CcPlace(*Row, 20, -16, 141, 64, TexImage(Label, CcUV(.1953125f, .8046875f, 0, 1)));
        CcPlace(*Row, 161, -16, 25, 64, TexImage(Label, CcUV(.8046875f, 1, 0, 1)));
        CcPlace(*Row, 20, 7, 141, 16,
            SNew(STextBlock).Font(CcFriz(12)).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1, 1)).Justification(ETextJustify::Center)
                .Text_Lambda([this, Field]() { return FText::FromString(CustomizationLabel(Field)); }));
        CcPlace(*Row, 169, 0, 32, 32, ArrowButton(false, [this, Field]() { CycleAppearance(Field, -1); }));
        CcPlace(*Row, 198, 0, 32, 32, ArrowButton(true, [this, Field]() { CycleAppearance(Field, 1); }));
        CcPlace(*Config, 7, 555 + Field * 32, 230, 32, Row);
    }
    const FString RandomizeText = Glue(TEXT("RANDOMIZE"), TEXT("Randomize"));
    const float RandomizeW = FMath::Max(146.f, FSlateApplication::IsInitialized()
        ? FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(RandomizeText, CcFriz(12)).X + 50.f : 146.f);
    CcPlace(*Config, 128 - RandomizeW * .5f, 718, RandomizeW, 30, GlueButton(RandomizeText, 12, [this]() { RandomizeAppearance(); }));

    // ── Right column: WotLK logo (TOPRIGHT -9), race and class info boxes, Accept and Back.
    TSharedRef<SCanvas> Right = SNew(SCanvas);
    CcPlace(*Right, 19, 0, 256, 128, SNew(SImage).Visibility(EVisibility::HitTestInvisible)
        .Image_Lambda([this]() { return GlueBrush(TEXT("Logo")); }));
    CcPlace(*Right, 8.5f, 123, 267, 287, InfoBox(true));
    CcPlace(*Right, 3.5f, 415, 267, 287, InfoBox(false));
    CcPlace(*Right, 56, 705, 160, 35, GlueButton(Glue(TEXT("CHARACTER_CREATE_ACCEPT"), TEXT("Accept")), 15, [this]() { SubmitCharacter(); }));
    CcPlace(*Right, 80, 737, 120, 30, GlueButton(Glue(TEXT("BACK"), TEXT("Back")), 12, [this]() { bConfirmBack = true; }));

    // ── Bottom centre: Name box (156x40, bottom +55) and the rotate buttons (bottom -5).
    TSharedRef<SCanvas> Bottom = SNew(SCanvas);
    CcPlace(*Bottom, 0, 26, 256, 20, Centered(Glue(TEXT("NAME"), TEXT("Name")), CcFriz(16), CcGold));
    // The tooltip-border frame is the whole look (like the original EditBox): no Slate box art,
    // no blue focus outline, no inner padding, so the 16px name fits the 40px box uncut.
    static FEditableTextBoxStyle NameBoxStyle = []()
    {
        FEditableTextBoxStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
        const FSlateBrush* None = FCoreStyle::Get().GetBrush("NoBrush");
        Style.SetBackgroundImageNormal(*None).SetBackgroundImageHovered(*None)
            .SetBackgroundImageFocused(*None).SetBackgroundImageReadOnly(*None)
            .SetPadding(FMargin(0)).SetForegroundColor(FLinearColor::White)
            .SetFocusedForegroundColor(FLinearColor::White);
        return Style;
    }();
    CcPlace(*Bottom, 50, 45, 156, 40,
        SNew(SWowTooltipFrame).Padding(FMargin(8, 0, 8, 0))
        [SNew(SBox).VAlign(VAlign_Center)
        [SAssignNew(NameBox, SEditableTextBox).Style(&NameBoxStyle)
            .BackgroundColor(FLinearColor::Transparent).ForegroundColor(FLinearColor::White)
            .Padding(FMargin(0)).Justification(ETextJustify::Center)
            .Font(CcFriz(16))
            .OnTextChanged_Lambda([this](const FText& Text)
            {
                // letters="12"
                if (Text.ToString().Len() > 12 && NameBox.IsValid()) NameBox->SetText(FText::FromString(Text.ToString().Left(12)));
            })
            .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Type) { if (Type == ETextCommit::OnEnter) SubmitCharacter(); })]]);
    auto RotateButton = [this](bool bRightTurn) -> TSharedRef<SWidget>
    {
        const FString Base = TEXT("Interface\\Glues\\CharacterCreate\\UI-RotationRight-Big");
        const FBox2f UV = bRightTurn ? CcFull : CcUV(1, 0, 0, 1);
        TSharedPtr<SButton> Button;
        SAssignNew(Button, SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
            .OnPressed_Lambda([this, bRightTurn]() { RotateDirection = bRightTurn ? -1 : 1; })
            .OnReleased_Lambda([this]() { RotateDirection = 0; });
        const TWeakPtr<SButton> Weak = Button;
        Button->SetContent(SNew(SImage).Image_Lambda([this, Base, UV, Weak]()
            { const auto B = Weak.Pin(); return TexBrush(Base + (B.IsValid() && B->IsPressed() ? TEXT("-Down") : TEXT("-Up")), UV); }));
        return SNew(SOverlay)
            + SOverlay::Slot()[Button.ToSharedRef()]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(SBox).WidthOverride(30).HeightOverride(30)
                [TexAdd(TEXT("Interface\\Buttons\\UI-Common-MouseHilight"), CcFull, TAttribute<FLinearColor>::CreateLambda([Weak]()
                    { const auto B = Weak.Pin(); return B.IsValid() && B->IsHovered() ? FLinearColor::White : FLinearColor::Transparent; }))]];
    };
    CcPlace(*Bottom, 128 - 16 - 25, 95, 50, 50, RotateButton(false));
    CcPlace(*Bottom, 128 + 16 - 25, 95, 50, 50, RotateButton(true));

    ChildSlot
    [SNew(SOverlay)
        + SOverlay::Slot()[SAssignNew(PreviewImage, SImage).Visibility(EVisibility::HitTestInvisible)]
        + SOverlay::Slot()
        [SNew(SDPIScaler).DPIScale_Lambda([this]()
            {
                // Glue screens are 768 units high; the width follows the window aspect.
                const float Height = GetCachedGeometry().GetLocalSize().Y;
                return Height > 1.f ? Height / 768.f : 1.f;
            })
            [SNew(SOverlay)
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(9, 5, 0, 0)
                [SNew(SBox).WidthOverride(256).HeightOverride(758)[Config]]
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 0, 0, 0)
                [SNew(SBox).WidthOverride(284).HeightOverride(768)[Right]]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)
                [SNew(SBox).WidthOverride(256).HeightOverride(140)[Bottom]]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(280, 12, 300, 0)
                [SAssignNew(StatusLabel, STextBlock).Font(CcFriz(15)).AutoWrapText(true).Justification(ETextJustify::Center)
                    .ShadowOffset(FVector2D(1, 1)).ColorAndOpacity(CcGold)]
            // Errors (name rejected, creation failed): red, centred near the top like UIErrorsFrame
            // (TOP 0,-122, 512 wide, ErrorFont), shown 5 s and faded over the last second
            // (Stefan 11.10: "like the in-game errors, red and across the screen").
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 122, 0, 0)
                [SNew(SBox).WidthOverride(512).Visibility(EVisibility::HitTestInvisible)
                    // r313 (Stefan 14.10: the red text faded but its black outline stayed): the outline and
                    // shadow fade with the text, and the line is gone once faded out.
                    [SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center)
                        .ShadowOffset(FVector2D(1, 1))
                        .Font_Lambda([this]()
                        {
                            FSlateFontInfo Font = CcFriz(18);
                            Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, CcErrorAlpha(ErrorShownAt));
                            return Font;
                        })
                        .ShadowColorAndOpacity_Lambda([this]() { return FLinearColor(0.f, 0.f, 0.f, CcErrorAlpha(ErrorShownAt)); })
                        .Text_Lambda([this]() { return FText::FromString(CcErrorAlpha(ErrorShownAt) > 0.f ? ErrorText : FString()); })
                        .ColorAndOpacity_Lambda([this]() { return FSlateColor(FLinearColor(1.0f, 0.1f, 0.1f, CcErrorAlpha(ErrorShownAt))); })]]
            // Back confirmation (a misclick on Back lost the whole character before).
            + SOverlay::Slot()
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, .45f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    .Visibility_Lambda([this]() { return bConfirmBack ? EVisibility::Visible : EVisibility::Collapsed; })
                    [SNew(SBox).WidthOverride(330).HeightOverride(120)
                        [SNew(SWowTooltipFrame).Padding(FMargin(16, 14, 16, 12))
                            [SNew(SVerticalBox)
                            + SVerticalBox::Slot().FillHeight(1).VAlign(VAlign_Center)
                                [SNew(STextBlock).Font(CcFriz(15)).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1, 1))
                                    .Justification(ETextJustify::Center).AutoWrapText(true)
                                    .Text(FText::FromString(TEXT("Go back to character selection?\nYour choices are kept for next time.")))]
                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                                [SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                                    [SNew(SBox).WidthOverride(120).HeightOverride(30)
                                        [GlueButton(Glue(TEXT("YES"), TEXT("Yes")), 12, [this]()
                                        {
                                            bConfirmBack = false;
                                            SaveDraft();
                                            OnBackToCharSelect.ExecuteIfBound();
                                        })]]
                                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                                    [SNew(SBox).WidthOverride(120).HeightOverride(30)
                                        [GlueButton(Glue(TEXT("NO"), TEXT("No")), 12, [this]() { bConfirmBack = false; })]]]]]]]]]];
    RestoreDraft();
    EnsureValidClassForRace();
    NormalizeAppearance();
    RefreshSelectionText();
}

void SWowCharacterCreateWidget::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    // CharacterCreateRotateLeft/Right_OnUpdate: turn while the button is held.
    if (RotateDirection != 0) OnPreviewRotated.ExecuteIfBound(RotateDirection * 180.f * DeltaTime);
}

void SWowCharacterCreateWidget::SetTextureLoader(TFunction<UTexture2D*(const FString&)> InLoader)
{
    TextureLoader = MoveTemp(InLoader);
    MissingPaths.Reset();
}

void SWowCharacterCreateWidget::SetGlueStrings(TMap<FString, FString> InStrings)
{
    GlueStrings = MoveTemp(InStrings);
}


void SWowCharacterCreateWidget::SetSelectedRaceIndex(int32 NewIndex)
{
    if (NewIndex < 0 || NewIndex >= UE_ARRAY_COUNT(GRaces))
    {
        return;
    }

    OnPreviewFaceFocus.ExecuteIfBound(false);
    StoreLooks();
    SelectedRaceIndex = NewIndex;
    LoadLooks();
    NormalizeAppearance();
    EnsureValidClassForRace();
    RefreshSelectionText();
    RefreshDescriptionText();
    NotifyAppearanceChanged();
}

void SWowCharacterCreateWidget::SetSelectedClassIndex(int32 NewIndex)
{
    if (NewIndex < 0 || NewIndex >= UE_ARRAY_COUNT(GClasses))
    {
        return;
    }

    const uint8 RaceId = GRaces[SelectedRaceIndex].Id;
    const uint8 ClassId = GClasses[NewIndex].Id;
    if (!IsClassAllowedForRace(RaceId, ClassId))
    {
        SetStatusText(FString::Printf(TEXT("%s cannot be %s."), GRaces[SelectedRaceIndex].Name, GClasses[NewIndex].Name));
        return;
    }

    SelectedClassIndex = NewIndex;
    OnPreviewFaceFocus.ExecuteIfBound(false);
    RefreshSelectionText();
    RefreshDescriptionText();
    NotifyAppearanceChanged();
}

void SWowCharacterCreateWidget::CycleRace(int32 Direction)
{
    const int32 Count = UE_ARRAY_COUNT(GRaces);
    SetSelectedRaceIndex((SelectedRaceIndex + Count + Direction) % Count);
}

void SWowCharacterCreateWidget::CycleClass(int32 Direction)
{
    const int32 Count = UE_ARRAY_COUNT(GClasses);
    for (int32 Step = 1; Step <= Count; ++Step)
    {
        const int32 CandidateIndex = (SelectedClassIndex + Count + (Direction * Step)) % Count;
        if (IsClassAllowedForRace(GRaces[SelectedRaceIndex].Id, GClasses[CandidateIndex].Id))
        {
            SetSelectedClassIndex(CandidateIndex);
            return;
        }
    }
}

void SWowCharacterCreateWidget::ToggleGender()
{
    OnPreviewFaceFocus.ExecuteIfBound(false);
    StoreLooks();
    SelectedGender = SelectedGender == 0 ? 1 : 0;
    LoadLooks();
    NormalizeAppearance();
    RefreshSelectionText();
    NotifyAppearanceChanged();
}

void SWowCharacterCreateWidget::RandomizeAppearance()
{
    for (int32 Field : {0, 1, 2, 3, 4})
    {
        const auto Options = AppearanceOptions(Field);
        AppearanceValue(Field) = Options[FMath::RandHelper(Options.Num())];
    }
    NormalizeAppearance();
    RefreshSelectionText();
    NotifyAppearanceChanged();
}

void SWowCharacterCreateWidget::RefreshSelectionText()
{
    if (FactionText.IsValid())
    {
        FactionText->SetText(FText::FromString(GRaces[SelectedRaceIndex].bAlliance ? TEXT("Alliance") : TEXT("Horde")));
        FactionText->SetColorAndOpacity(FSlateColor(GetFactionColor(GRaces[SelectedRaceIndex].bAlliance)));
    }

    if (RaceText.IsValid())
    {
        RaceText->SetText(FText::FromString(FString(GRaces[SelectedRaceIndex].Name)));
    }

    if (ClassText.IsValid())
    {
        ClassText->SetText(FText::FromString(FString(GClasses[SelectedClassIndex].Name)));
    }

    if (GenderText.IsValid())
    {
        GenderText->SetText(FText::FromString(SelectedGender == 0 ? TEXT("Male") : TEXT("Female")));
    }

    if (SkinText.IsValid())
    {
        SkinText->SetText(FText::AsNumber(SelectedSkin + 1));
    }
    if (FaceText.IsValid())
    {
        FaceText->SetText(FText::AsNumber(SelectedFace + 1));
    }
    if (HairStyleText.IsValid())
    {
        HairStyleText->SetText(FText::AsNumber(SelectedHairStyle + 1));
    }
    if (HairColorText.IsValid())
    {
        HairColorText->SetText(FText::AsNumber(SelectedHairColor + 1));
    }
    if (FacialHairText.IsValid())
    {
        FacialHairText->SetText(FText::AsNumber(SelectedFacialHair + 1));
    }
}

void SWowCharacterCreateWidget::RefreshDescriptionText()
{
    // A new race/class starts its info box at the top, like SetCharacterRace/Class.
    if (RaceScroll.IsValid()) RaceScroll->SetScrollOffset(0);
    if (ClassScroll.IsValid()) ClassScroll->SetScrollOffset(0);
    if (RaceTitleText.IsValid())
    {
        RaceTitleText->SetText(FText::FromString(FString(GRaces[SelectedRaceIndex].Name)));
        RaceTitleText->SetColorAndOpacity(FSlateColor(GRaces[SelectedRaceIndex].bAlliance ? FLinearColor(0.70f, 0.82f, 1.0f, 1.0f) : FLinearColor(1.0f, 0.45f, 0.35f, 1.0f)));
    }

    if (RaceBodyText.IsValid())
    {
        RaceBodyText->SetText(FText::FromString(FString(GRaces[SelectedRaceIndex].Summary)));
    }

    if (ClassTitleText.IsValid())
    {
        ClassTitleText->SetText(FText::FromString(FString(GClasses[SelectedClassIndex].Name)));
        ClassTitleText->SetColorAndOpacity(FSlateColor(GetClassColor(GClasses[SelectedClassIndex].Id)));
    }

    if (ClassBodyText.IsValid())
    {
        ClassBodyText->SetText(FText::FromString(FString(GClasses[SelectedClassIndex].Summary)));
    }
}

void SWowCharacterCreateWidget::NotifyAppearanceChanged()
{
    OnAppearanceChanged.ExecuteIfBound(GRaces[SelectedRaceIndex].Id, static_cast<uint8>(SelectedGender));
}

bool SWowCharacterCreateWidget::IsClassAllowedForRace(uint8 RaceId, uint8 ClassId) const
{
    switch (RaceId)
    {
    case 1: return ClassId == 1 || ClassId == 2 || ClassId == 4 || ClassId == 5 || ClassId == 6 || ClassId == 8 || ClassId == 9;
    case 2: return ClassId == 1 || ClassId == 3 || ClassId == 4 || ClassId == 6 || ClassId == 7 || ClassId == 9;
    case 3: return ClassId == 1 || ClassId == 2 || ClassId == 3 || ClassId == 4 || ClassId == 5 || ClassId == 6;
    case 4: return ClassId == 1 || ClassId == 3 || ClassId == 4 || ClassId == 5 || ClassId == 6 || ClassId == 11;
    case 5: return ClassId == 1 || ClassId == 4 || ClassId == 5 || ClassId == 6 || ClassId == 8 || ClassId == 9;
    case 6: return ClassId == 1 || ClassId == 3 || ClassId == 6 || ClassId == 7 || ClassId == 11;
    case 7: return ClassId == 1 || ClassId == 4 || ClassId == 6 || ClassId == 8 || ClassId == 9;
    case 8: return ClassId == 1 || ClassId == 3 || ClassId == 4 || ClassId == 5 || ClassId == 6 || ClassId == 7 || ClassId == 8;
    case 10: return ClassId == 2 || ClassId == 3 || ClassId == 4 || ClassId == 5 || ClassId == 6 || ClassId == 8 || ClassId == 9;
    case 11: return ClassId == 1 || ClassId == 2 || ClassId == 3 || ClassId == 5 || ClassId == 6 || ClassId == 7 || ClassId == 8;
    default: return false;
    }
}

void SWowCharacterCreateWidget::EnsureValidClassForRace()
{
    if (IsClassAllowedForRace(GRaces[SelectedRaceIndex].Id, GClasses[SelectedClassIndex].Id))
    {
        return;
    }

    for (int32 ClassIndex = 0; ClassIndex < UE_ARRAY_COUNT(GClasses); ++ClassIndex)
    {
        if (IsClassAllowedForRace(GRaces[SelectedRaceIndex].Id, GClasses[ClassIndex].Id))
        {
            SelectedClassIndex = ClassIndex;
            return;
        }
    }

    SelectedClassIndex = 0;
}

void SWowCharacterCreateWidget::ShowError(const FString& Text)
{
    ErrorText = Text;
    ErrorShownAt = FPlatformTime::Seconds();
    SetStatusText(FString());
}

void SWowCharacterCreateWidget::SetStatusText(const FString& Text)
{
    if (StatusLabel.IsValid())
    {
        StatusLabel->SetText(FText::FromString(Text));
    }
}

void SWowCharacterCreateWidget::SetPreviewSelection(uint8 RaceId, uint8 GenderId)
{
    for (int32 RaceIndex = 0; RaceIndex < UE_ARRAY_COUNT(GRaces); ++RaceIndex)
    {
        if (GRaces[RaceIndex].Id == RaceId)
        {
            SelectedRaceIndex = RaceIndex;
            break;
        }
    }

    SelectedGender = GenderId == 0 ? 0 : 1;
    NormalizeAppearance();
    EnsureValidClassForRace();
    RefreshSelectionText();
    RefreshDescriptionText();
    NotifyAppearanceChanged();
}

void SWowCharacterCreateWidget::SetPreviewRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
    if (!PreviewImage.IsValid() || !InRenderTarget)
    {
        return;
    }

    PreviewBrush = MakeShared<FSlateBrush>();
    PreviewBrush->SetResourceObject(InRenderTarget);
    PreviewBrush->ImageSize = FVector2D(InRenderTarget->SizeX, InRenderTarget->SizeY);
    PreviewBrush->DrawAs = ESlateBrushDrawType::Image;
    PreviewBrush->Tiling = ESlateBrushTileType::NoTile;

    PreviewImage->SetImage(PreviewBrush.Get());
    PreviewImage->SetVisibility(EVisibility::SelfHitTestInvisible);

    if (PreviewPlaceholder.IsValid())
    {
        PreviewPlaceholder->SetVisibility(EVisibility::Collapsed);
    }
}

const FSlateBrush* SWowCharacterCreateWidget::GlueBrush(const FString& Key) const
{
    const auto* Brush = GlueBrushes.Find(Key);
    return Brush ? Brush->Get() : FCoreStyle::Get().GetBrush("NoBrush");
}

void SWowCharacterCreateWidget::SetGlueTexture(const FString& Key, UTexture2D* Texture)
{
    if (!Texture) return;
    GlueTextures.Emplace(Texture);
    auto Add = [&](const FString& Name, float Left, float Top, float Right, float Bottom)
    {
        auto Brush = MakeShared<FSlateBrush>();
        Brush->SetResourceObject(Texture);
        Brush->DrawAs = ESlateBrushDrawType::Image;
        Brush->ImageSize = FVector2D(Texture->GetSizeX()*(Right-Left),Texture->GetSizeY()*(Bottom-Top));
        Brush->SetUVRegion(FBox2f(FVector2f(Left,Top),FVector2f(Right,Bottom)));
        GlueBrushes.Add(Name,Brush);
    };
    Add(Key,0,0,1,1);
    if (Key == TEXT("Banners"))
    {
        // Visible cloth plus a small transparent gutter, from the 256x512 atlas.
        // Keep the original painted edges and pointed/tasselled lower ends.
        Add(TEXT("AllianceBanner"),32.f/256.f,0,124.f/256.f,360.f/512.f);
        Add(TEXT("HordeBanner"),132.f/256.f,0,224.f/256.f,370.f/512.f);
    }
    // Atlas coordinates from the user's 3.3.5 GlueXML CharacterCreate.lua.
    if (Key == TEXT("Races"))
    {
        const int32 Columns[] = {0,1,3,2,4,3,1,0,2,4};
        for (int32 I=0; I<UE_ARRAY_COUNT(GRaces); ++I)
            for (int32 Gender=0; Gender<2; ++Gender)
            {
                const float X=Columns[I]*0.125f;
                const float Y=(GRaces[I].bAlliance ? 0.0f : 0.25f)+Gender*0.5f;
                Add(FString::Printf(TEXT("Race%d_%d"),I,Gender),X,Y,X+0.125f,Y+0.25f);
            }
    }
    else if (Key == TEXT("Classes"))
    {
        const int32 Columns[] = {0,0,0,2,2,1,1,1,3,3};
        const int32 Rows[] = {0,2,1,0,1,2,1,0,1,0};
        const float Left[] = {0,0.25f,0.49609375f,0.7421875f};
        const float Right[] = {0.25f,0.49609375f,0.7421875f,0.98828125f};
        for (int32 I=0; I<UE_ARRAY_COUNT(GClasses); ++I)
            Add(FString::Printf(TEXT("Class%d"),I),Left[Columns[I]],Rows[I]*0.25f,Right[Columns[I]],Rows[I]*0.25f+0.25f);
    }
    else if (Key == TEXT("Gender"))
    {
        Add(TEXT("Gender0"),0,0,0.5f,1);
        Add(TEXT("Gender1"),0.5f,0,1,1);
    }
}

int32& SWowCharacterCreateWidget::AppearanceValue(int32 Field)
{
    switch (Field) {
    case 0: return SelectedSkin;
    case 1: return SelectedFace;
    case 2: return SelectedHairStyle;
    case 3: return SelectedHairColor;
    default: return SelectedFacialHair;
    }
}

TArray<int32> SWowCharacterCreateWidget::AppearanceOptions(int32 Field) const
{
    const int32 Race=GRaces[SelectedRaceIndex].Id;
    TArray<int32> Options;
    for (const auto& Entry : FDbcStore::Get().CharSections().GetAll())
    {
        if (Entry.RaceID != Race || Entry.SexID != SelectedGender || !(Entry.Flags & 1)) continue;
        if (Field == 0 && Entry.Type == 0) Options.AddUnique(Entry.Color);
        if (Field == 1 && Entry.Type == 1 && Entry.Color == SelectedSkin) Options.AddUnique(Entry.Variation);

    }
    if (Field == 2 || Field == 3)
        Options = WowCharacterAppearance::HairChoices(FDbcStore::Get().CharSections().GetAll(),
            FDbcStore::Get().CharHairGeosets().GetAll(), Race, SelectedGender,
            Field == 2 ? INDEX_NONE : SelectedHairStyle);
    if (Field == 4)
        for (const auto& Entry : FDbcStore::Get().CharacterFacialHairStyles().GetAll())
            if (Entry.RaceID == Race && Entry.SexID == SelectedGender) Options.AddUnique(Entry.VariationID);
    if (Options.IsEmpty()) Options.Add(0);
    Options.Sort();
    return Options;
}

void SWowCharacterCreateWidget::NormalizeAppearance()
{
    for (int32 Field=0; Field<5; ++Field)
    {
        const auto Options=AppearanceOptions(Field);
        if (!Options.Contains(AppearanceValue(Field))) AppearanceValue(Field)=Options[0];
    }
}

void SWowCharacterCreateWidget::CycleAppearance(int32 Field, int32 Direction)
{
    if (Field<0 || Field>4) return;
    const auto Options=AppearanceOptions(Field);
    const int32 Index=FMath::Max(0,Options.IndexOfByKey(AppearanceValue(Field)));
    AppearanceValue(Field)=Options[(Index+Options.Num()+(Direction<0 ? -1 : 1))%Options.Num()];
    NormalizeAppearance();
    RefreshSelectionText();
    NotifyAppearanceChanged();
    // Skin colour is a whole-body choice; the other four rows are about the head.
    OnPreviewFaceFocus.ExecuteIfBound(Field != 0);
}

FWowCharacterInfo SWowCharacterCreateWidget::GetPreviewCharacter() const
{
    FWowCharacterInfo Info;
    Info.Race=GRaces[SelectedRaceIndex].Id;
    Info.Class=GClasses[SelectedClassIndex].Id;
    Info.Gender=SelectedGender;
    Info.Skin=SelectedSkin;
    Info.Face=SelectedFace;
    Info.HairStyle=SelectedHairStyle;
    Info.HairColor=SelectedHairColor;
    Info.FacialHair=SelectedFacialHair;
    return Info;
}

void SWowCharacterCreateWidget::SubmitCharacter()
{
    const FString Name=NameBox.IsValid() ? NameBox->GetText().ToString().TrimStartAndEnd() : FString();
    if (Name.Len()<2 || Name.Len()>12)
    {
        SetStatusText(TEXT("Choose a character name between 2 and 12 letters."));
        return;
    }
    const auto Info=GetPreviewCharacter();
    GCcDraft = FCcDraft(); // created (or failed: Back saves the current choices again)
    GCcLooks.Reset();
    SetStatusText(TEXT("Creating character..."));
    OnCharacterCreated.ExecuteIfBound(Name,Info.Race,Info.Class,Info.Gender,
        Info.Skin,Info.Face,Info.HairStyle,Info.HairColor,Info.FacialHair);
}

FReply SWowCharacterCreateWidget::OnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    const auto Position=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
    // Drag-rotate between the configuration frame (left 265 glue units) and the info boxes (right 284).
    const float Scale=Geometry.GetLocalSize().Y>1?Geometry.GetLocalSize().Y/768.f:1.f;
    if (Event.GetEffectingButton()==EKeys::LeftMouseButton && Position.X>270*Scale
        && Position.X<Geometry.GetLocalSize().X-290*Scale)
    {
        bRotatingPreview=true;
        return FReply::Handled().CaptureMouse(AsShared());
    }
    return SCompoundWidget::OnMouseButtonDown(Geometry,Event);
}
FReply SWowCharacterCreateWidget::OnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if (bRotatingPreview && Event.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        bRotatingPreview=false;
        return FReply::Handled().ReleaseMouseCapture();
    }
    return SCompoundWidget::OnMouseButtonUp(Geometry,Event);
}
FReply SWowCharacterCreateWidget::OnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if (bRotatingPreview && HasMouseCapture())
    {
        OnPreviewRotated.ExecuteIfBound(-Event.GetCursorDelta().X*0.32f);
        return FReply::Handled();
    }
    return SCompoundWidget::OnMouseMove(Geometry,Event);
}
void SWowCharacterCreateWidget::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
    bRotatingPreview=false;
    SCompoundWidget::OnMouseCaptureLost(Event);
}
