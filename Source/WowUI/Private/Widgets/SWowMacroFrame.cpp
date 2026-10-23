// Native macro window (MacroFrame + MacroPopupFrame, 3.3.5) - see SWowMacroFrame.h.
// r238 (Stefan 12.10: "match the real macro window"): laid out from Blizzard_MacroUI.xml on its own
// 384 x 512 art - UI-Character-General top, MacroFrame-BotLeft/BotRight, the MacroFrame-Icon
// portrait, HelpFrameTab tabs, PopupButtonTemplate slots, the UI-EmptySlot selected-macro socket,
// the tooltip-backdrop command box and MacroPopupFrame's own art beside it. Positions are the XML's
// (frame units, top-left origin); fonts are GameFont* sizes at the window scale.
#include "Widgets/SWowMacroFrame.h"
#include "WowMacros.h"
#include "WowWindowStyle.h"
#include "WowColor.h"
#include "WowRetainedSlateBrush.h"
#include "WowConnectionManager.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    // MacroFrame 384 x 512, MacroPopupFrame 297 x 298 at its TOPRIGHT -40,-40.
    constexpr float FrameW = 384.f, FrameH = 512.f;
    constexpr float PopupX = FrameW - 40.f, PopupY = 40.f, PopupW = 297.f, PopupH = 298.f;
    // MacroButton1 TOPLEFT 42,-83; 6 per row, 13 apart; rows 10 apart (18 buttons).
    constexpr int32 GridColumns = 6, GridRows = 3;
    constexpr float SlotUnits = 36.f;
    // MacroPopupButton1 TOPLEFT 24,-85; 5 per row 10 apart, 4 rows 8 apart.
    constexpr int32 PopupColumns = 5, PopupRows = 4;

    FLinearColor GoldText() { return WowColor::FromWow(1.f, .82f, 0.f); }
    FLinearColor WhiteText() { return FLinearColor::White; }
}

void SWowMacroFrame::Construct(const FArguments& InArgs)
{
    LoadTextureFn = InArgs._LoadTexture;
    GetScaleFn = InArgs._GetScale;
    GetCharacterNameFn = InArgs._GetCharacterName;
    PlaySoundFn = InArgs._UiSound;
    Connection = InArgs._Connection;
    ChildSlot[ SAssignNew(Root, SBox) ];
    SetVisibility(EVisibility::Collapsed);
}

FVector2D SWowMacroFrame::GetPanelSize() const
{
    return FVector2D(bPopupOpen ? PopupX + PopupW : FrameW, FrameH) * Scale;
}

FSlateFontInfo SWowMacroFrame::Font(int32 Size) const
{
    // GameFont sizes are pixel heights; Slate sizes are points (x .75), like the quest dialog.
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(Size * Scale * .75f)));
}

TSharedRef<SWidget> SWowMacroFrame::Icon(const FString& Path, float Size) const
{
    return SNew(SBox).WidthOverride(Size * Scale).HeightOverride(Size * Scale)
        [ SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, Path]() { return Brush(Path); }) ];
}

const FSlateBrush* SWowMacroFrame::BrushUV(const FString& Path, float L, float R, float T, float B) const
{
    const FString Key = FString::Printf(TEXT("%s|%.4f|%.4f|%.4f|%.4f"), *Path, L, R, T, B);
    if (const TSharedPtr<FWowRetainedSlateBrush>* Found = Brushes.Find(Key)) return Found->Get();
    const FSlateBrush* Base = Brush(Path);
    if (!Base) return nullptr;
    TSharedPtr<FWowRetainedSlateBrush> NewBrush = MakeShared<FWowRetainedSlateBrush>();
    NewBrush->SetResourceObject(Base->GetResourceObject());
    NewBrush->DrawAs = ESlateBrushDrawType::Image;
    NewBrush->ImageSize = Base->ImageSize;
    NewBrush->SetUVRegion(FBox2D(FVector2D(L, T), FVector2D(R, B)));
    Brushes.Add(Key, NewBrush);
    return NewBrush.Get();
}

TSharedRef<SWidget> SWowMacroFrame::Art(const FString& Path) const
{
    return SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, Path]() { return Brush(Path); });
}

TSharedRef<SWidget> SWowMacroFrame::ArtUV(const FString& Path, float L, float R, float T, float B) const
{
    return SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, Path, L, R, T, B]() { return BrushUV(Path, L, R, T, B); });
}

TSharedRef<SWidget> SWowMacroFrame::AddArt(const FString& Path, float Alpha) const
{
    return SNew(SWowAddImage).Visibility(EVisibility::HitTestInvisible).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Alpha))
        .Image_Lambda([this, Path]() { return Brush(Path); });
}

const FTextBlockStyle* SWowMacroFrame::ButtonText() const
{
    // UIPanelButtonTemplate: GameFontNormal in the button's foreground (gold, white on hover, grey disabled).
    const int32 Size = FMath::Max(6, FMath::RoundToInt(12.f * Scale * .75f));
    if (ButtonTextStyle.Font.Size != Size || !ButtonTextStyle.Font.HasValidFont())
    {
        ButtonTextStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));
        ButtonTextStyle.SetFont(FWowWindowStyle::TitleFont(Size));
        ButtonTextStyle.SetColorAndOpacity(FSlateColor::UseForeground());
        ButtonTextStyle.SetShadowOffset(FVector2D(1, 1));
        ButtonTextStyle.SetShadowColorAndOpacity(FLinearColor::Black);
    }
    return &ButtonTextStyle;
}

TSharedRef<SWidget> SWowMacroFrame::Button(const FString& Label, float Width, TFunction<void()> OnClick, TAttribute<bool> Enabled) const
{
    return SNew(SBox).WidthOverride(Width * Scale).HeightOverride(22.f * Scale)
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .ContentPadding(0).IsEnabled(Enabled).TextStyle(ButtonText())
            .Text(FText::FromString(Label))
            .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
        ];
}

TSharedRef<SWidget> SWowMacroFrame::CloseButton(TFunction<void()> OnClick) const
{
    // UIPanelCloseButton (32 x 32): UI-Panel-MinimizeButton Up / Down, Highlight added on hover.
    TSharedPtr<SButton> Btn;
    TSharedRef<SWidget> Result = SNew(SBox).WidthOverride(32.f * Scale).HeightOverride(32.f * Scale)
        [
            SAssignNew(Btn, SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
            .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
        ];
    TWeakPtr<SButton> Weak = Btn;
    Btn->SetContent(
        SNew(SOverlay)
        + SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, Weak]()
            {
                const TSharedPtr<SButton> B = Weak.Pin();
                return Brush(B.IsValid() && B->IsPressed() ? TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down") : TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"));
            }) ]
        + SOverlay::Slot()[ SNew(SBox).Visibility_Lambda([Weak]()
            {
                const TSharedPtr<SButton> B = Weak.Pin();
                return B.IsValid() && B->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            })[ AddArt(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Highlight")) ] ]);
    return Result;
}

TSharedRef<SWidget> SWowMacroFrame::TextLabel(const FString& Text, int32 Size, const FLinearColor& Color) const
{
    return SNew(STextBlock).Visibility(EVisibility::HitTestInvisible).Text(FText::FromString(Text)).Font(Font(Size))
        .ColorAndOpacity(Color).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black);
}

float SWowMacroFrame::TextWidth(const FString& Text, int32 Size) const
{
    if (!FSlateApplication::IsInitialized() || Scale <= 0.f) return Text.Len() * Size * .5f;
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    return Measure->Measure(Text, Font(Size)).X / Scale;
}

const FSlateBrush* SWowMacroFrame::Brush(const FString& Path) const
{
    if (Path.IsEmpty()) return nullptr;
    if (const TSharedPtr<FWowRetainedSlateBrush>* Found = Brushes.Find(Path)) return Found->Get();
    UTexture2D* Texture = LoadTextureFn ? LoadTextureFn(Path) : nullptr;
    if (!Texture) return nullptr;
    TSharedPtr<FWowRetainedSlateBrush> NewBrush = MakeShared<FWowRetainedSlateBrush>();
    NewBrush->SetResourceObject(Texture);
    NewBrush->DrawAs = ESlateBrushDrawType::Image;
    NewBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    Brushes.Add(Path, NewBrush);
    return NewBrush.Get();
}

int32 SWowMacroFrame::FirstIndex() const { return bCharacterTab ? FWowMacros::MaxAccount + 1 : 1; }
int32 SWowMacroFrame::SlotCount() const { return bCharacterTab ? FWowMacros::MaxCharacter : FWowMacros::MaxAccount; }

void SWowMacroFrame::Open()
{
    if (bOpen) return;
    bOpen = true;
    if (!FWowMacros::Get(SelectedIndex))
        SelectedIndex = FWowMacros::Get(FirstIndex()) ? FirstIndex() : 0;
    bPopupOpen = false;
    Rebuild();
    SetVisibility(EVisibility::SelfHitTestInvisible);
    Sound(TEXT("igCharacterInfoOpen"));
}

void SWowMacroFrame::Close()
{
    if (!bOpen) return;
    bOpen = false;
    bPopupOpen = false;
    SetVisibility(EVisibility::Collapsed);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    Sound(TEXT("igCharacterInfoClose"));
}

FReply SWowMacroFrame::OnMouseWheel(const FGeometry&, const FPointerEvent& Event)
{
    // The icon list scrolls a row per notch; elsewhere the wheel stops here (never zooms the camera).
    if (bPopupOpen)
    {
        PopupFirstRow += Event.GetWheelDelta() > 0.f ? -1 : 1;
        Rebuild(); // BuildPopup clamps the row
    }
    else if (FMath::DivideAndRoundUp(SlotCount(), GridColumns) > GridRows)
    {
        // r238: more macros than the 18 MacroButtons - the wheel scrolls the grid a row.
        GridFirstRow += Event.GetWheelDelta() > 0.f ? -1 : 1;
        Rebuild(); // Rebuild clamps the row
    }
    return FReply::Handled();
}

FReply SWowMacroFrame::OnKeyDown(const FGeometry&, const FKeyEvent& Key)
{
    if (Key.GetKey() == EKeys::Escape)
    {
        if (bPopupOpen) ClosePopup(false);
        else Close();
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

void SWowMacroFrame::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (!bOpen) return;
    const float NewScale = GetScaleFn ? FMath::Max(.3f, GetScaleFn()) : 1.f;
    const bool bTyping = (BodyBox.IsValid() && BodyBox->HasKeyboardFocus()) || (PopupNameBox.IsValid() && PopupNameBox->HasKeyboardFocus());
    // Macros changed elsewhere (CreateMacro/DeleteMacro from an addon, another character loaded).
    if ((!FMath::IsNearlyEqual(NewScale, Scale) || FWowMacros::Revision() != SeenRevision) && !bTyping)
    {
        Scale = NewScale;
        Rebuild();
    }
}

void SWowMacroFrame::Select(int32 Index)
{
    if (SelectedIndex == Index) return;
    SelectedIndex = Index;
    Sound(TEXT("igMainMenuOptionCheckBoxOn"));
    Rebuild();
}

void SWowMacroFrame::SaveBody(const FString& Body)
{
    const FWowMacro* Macro = FWowMacros::Get(SelectedIndex);
    if (!Macro) return;
    FWowMacros::Edit(SelectedIndex, Macro->Name, Macro->Icon, Body);
    SeenRevision = FWowMacros::Revision(); // our own edit: keep the text box (and its caret)
}

TSharedRef<SWidget> SWowMacroFrame::BuildSlot(int32 MacroIndex)
{
    // PopupButtonTemplate (36 x 36): UI-EmptySlot-Disabled 45 x 45 behind it, the icon as its
    // normal texture (CENTER 0,-1), the name (GameFontHighlightSmallOutline) along the bottom,
    // ButtonHilight-Square on hover and CheckButtonHilight on the checked (selected) macro.
    const FWowMacro* Macro = FWowMacros::Get(MacroIndex);
    const bool bSelected = Macro && MacroIndex == SelectedIndex;
    const FString IconPath = Macro ? FWowMacros::DisplayIcon(*Macro, Connection.Get()) : FString();
    const FString Name = Macro ? Macro->Name : FString();
    TSharedPtr<SBorder> Hit;
    TSharedRef<SWidget> Result = SNew(SBox).WidthOverride(SlotUnits * Scale).HeightOverride(SlotUnits * Scale)
        [
            SAssignNew(Hit, SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .ToolTipText(Macro ? FText::FromString(Name) : FText::GetEmpty())
            .OnMouseButtonDown_Lambda([this, MacroIndex, bHas = Macro != nullptr](const FGeometry&, const FPointerEvent& Event)
            {
                if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !bHas) return FReply::Handled();
                PressedIndex = MacroIndex;
                PressedAt = Event.GetScreenSpacePosition();
                Select(MacroIndex);
                return FReply::Handled();
            })
            .OnMouseButtonUp_Lambda([this](const FGeometry&, const FPointerEvent&) { PressedIndex = 0; return FReply::Handled(); })
            .OnMouseMove_Lambda([this, MacroIndex](const FGeometry&, const FPointerEvent& Event)
            {
                // Drag a macro onto an action button (PickupMacro).
                if (PressedIndex == MacroIndex && Event.IsMouseButtonDown(EKeys::LeftMouseButton)
                    && FVector2D::Distance(Event.GetScreenSpacePosition(), PressedAt) > 4.f)
                {
                    PressedIndex = 0;
                    if (UWowConnectionManager* Conn = Connection.Get()) Conn->PickupMacroCursor(MacroIndex);
                    Sound(TEXT("igAbilityIconPickup"));
                }
                return FReply::Unhandled();
            })
        ];
    TWeakPtr<SBorder> Weak = Hit;
    Hit->SetContent(
        SNew(SCanvas)
        + SCanvas::Slot().Position(FVector2D(-4.5f, -3.5f) * Scale).Size(FVector2D(45.f, 45.f) * Scale)
        [ Art(TEXT("Interface\\Buttons\\UI-EmptySlot-Disabled")) ]
        + SCanvas::Slot().Position(FVector2D(0.f, 1.f) * Scale).Size(FVector2D(SlotUnits, SlotUnits) * Scale)
        [ SNew(SBox).Visibility(Macro ? EVisibility::HitTestInvisible : EVisibility::Collapsed)[ Art(IconPath) ] ]
        + SCanvas::Slot().Position(FVector2D::ZeroVector).Size(FVector2D(SlotUnits, SlotUnits) * Scale)
        [ SNew(SBox).Visibility(bSelected ? EVisibility::HitTestInvisible : EVisibility::Collapsed)[ AddArt(TEXT("Interface\\Buttons\\CheckButtonHilight")) ] ]
        + SCanvas::Slot().Position(FVector2D::ZeroVector).Size(FVector2D(SlotUnits, SlotUnits) * Scale)
        [ SNew(SBox).Visibility_Lambda([Weak, bHas = Macro != nullptr]()
            {
                const TSharedPtr<SBorder> B = Weak.Pin();
                return bHas && B.IsValid() && B->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            })[ AddArt(TEXT("Interface\\Buttons\\ButtonHilight-Square")) ] ]
        // r377 (Stefan 16.10: "make sure all the text is shown on the macro", "Backstab" showed as
        // "ackstal"): the name may use the gap to the next slot (46 wide, centred) and steps its
        // font down (10 -> 7) until it fits.
        + SCanvas::Slot().Position(FVector2D(-5.f, 24.f) * Scale).Size(FVector2D(SlotUnits + 10.f, 10.f) * Scale)
        [
            SNew(STextBlock).Visibility(EVisibility::HitTestInvisible).Text(FText::FromString(Name))
            .Font([this, &Name]() { int32 Size = 10; while (Size > 7 && TextWidth(Name, Size) > SlotUnits + 9.f) --Size;
                FSlateFontInfo F = Font(Size); F.OutlineSettings.OutlineSize = 1; return F; }())
            .ColorAndOpacity(WhiteText()).Justification(ETextJustify::Center).OverflowPolicy(ETextOverflowPolicy::Clip)
        ]);
    return Result;
}

TSharedRef<SWidget> SWowMacroFrame::BuildTab(const FString& Text, bool bCharacter, float& InOutX)
{
    // TabButtonTemplate: width = text + 32 (PanelTemplates_TabResize(self, 0)), 32 high. The
    // selected tab is disabled and shows HelpFrameTab-Active 3 lower with white text; the others
    // HelpFrameTab-Inactive with gold text and UI-Character-Tab-Highlight (ADD) under the mouse.
    const bool bActive = bCharacterTab == bCharacter;
    const float TextW = FMath::CeilToFloat(TextWidth(Text, 10));
    const float TabW = TextW + 32.f;
    const float X = InOutX;
    InOutX += TabW;
    const FString Tex = bActive ? TEXT("Interface\\HelpFrame\\HelpFrameTab-Active") : TEXT("Interface\\HelpFrame\\HelpFrameTab-Inactive");
    const float ArtY = bActive ? 3.f : 0.f;
    TSharedPtr<SButton> Btn;
    TSharedRef<SWidget> Result = SNew(SBox).WidthOverride(TabW * Scale).HeightOverride(35.f * Scale)
        [
            SAssignNew(Btn, SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
            .OnClicked_Lambda([this, bCharacter]()
            {
                if (bCharacterTab != bCharacter)
                {
                    bCharacterTab = bCharacter;
                    GridFirstRow = 0;
                    SelectedIndex = FWowMacros::Get(FirstIndex()) ? FirstIndex() : 0;
                    Sound(TEXT("igCharacterInfoTab"));
                    Rebuild();
                }
                return FReply::Handled();
            })
        ];
    TWeakPtr<SButton> Weak = Btn;
    Btn->SetContent(
        SNew(SCanvas)
        + SCanvas::Slot().Position(FVector2D(0.f, ArtY) * Scale).Size(FVector2D(16.f, 32.f) * Scale)[ ArtUV(Tex, 0.f, .25f, 0.f, 1.f) ]
        + SCanvas::Slot().Position(FVector2D(16.f, ArtY) * Scale).Size(FVector2D(TextW, 32.f) * Scale)[ ArtUV(Tex, .25f, .75f, 0.f, 1.f) ]
        + SCanvas::Slot().Position(FVector2D(16.f + TextW, ArtY) * Scale).Size(FVector2D(16.f, 32.f) * Scale)[ ArtUV(Tex, .75f, 1.f, 0.f, 1.f) ]
        // HighlightTexture BOTTOM 2,-8, tab width x 32.
        + SCanvas::Slot().Position(FVector2D(2.f, 8.f) * Scale).Size(FVector2D(TabW, 32.f) * Scale)
        [ SNew(SBox).Visibility_Lambda([Weak, bActive]()
            {
                const TSharedPtr<SButton> B = Weak.Pin();
                return !bActive && B.IsValid() && B->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            })[ AddArt(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-Tab-Highlight")) ] ]
        // $parentText: BOTTOM 0,5, 13 high (GameFontNormalSmall / GameFontHighlightSmall).
        + SCanvas::Slot().Position(FVector2D(0.f, 32.f - 5.f - 13.f + ArtY) * Scale).Size(FVector2D(TabW, 13.f) * Scale)
        [
            SNew(STextBlock).Visibility(EVisibility::HitTestInvisible).Text(FText::FromString(Text)).Font(Font(10))
            .Justification(ETextJustify::Center).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
            .ColorAndOpacity_Lambda([Weak, bActive]()
            {
                const TSharedPtr<SButton> B = Weak.Pin();
                return FSlateColor(bActive || (B.IsValid() && B->IsHovered()) ? WhiteText() : GoldText());
            })
        ]);
    (void)X;
    return Result;
}

void SWowMacroFrame::Rebuild()
{
    if (!Root.IsValid()) return;
    Scale = GetScaleFn ? FMath::Max(.3f, GetScaleFn()) : 1.f;
    SeenRevision = FWowMacros::Revision();
    if (!FWowMacros::Get(SelectedIndex)) SelectedIndex = 0;
    const FWowMacro* Selected = FWowMacros::Get(SelectedIndex);
    BodyLength = Selected ? Selected->Body.Len() : 0;
    const int32 Count = bCharacterTab ? FWowMacros::NumCharacter() : FWowMacros::NumAccount();
    const int32 Max = SlotCount();
    // The XML has 18 MacroButtons (6 x 3); the account keeps 36. The wheel over the grid scrolls a
    // row at a time when more are in use, so macros 19-36 stay reachable.
    const int32 TotalRows = FMath::DivideAndRoundUp(Max, GridColumns);
    GridFirstRow = FMath::Clamp(GridFirstRow, 0, FMath::Max(0, TotalRows - GridRows));
    const float Sc = Scale;
    auto At = [Sc](float X, float Y) { return FVector2D(X, Y) * Sc; };
    auto Sz = [Sc](float W, float H) { return FVector2D(W, H) * Sc; };
    const FString CharacterName = GetCharacterNameFn ? GetCharacterNameFn() : FString();

    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    // Art (BACKGROUND portrait, BORDER frame).
    Canvas->AddSlot().Position(At(7, 6)).Size(Sz(60, 60))[ Art(TEXT("Interface\\MacroFrame\\MacroFrame-Icon")) ];
    Canvas->AddSlot().Position(At(0, 0)).Size(Sz(256, 256))[ Art(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopLeft")) ];
    Canvas->AddSlot().Position(At(256, 0)).Size(Sz(128, 256))[ Art(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopRight")) ];
    Canvas->AddSlot().Position(At(0, 256)).Size(Sz(256, 256))[ Art(TEXT("Interface\\MacroFrame\\MacroFrame-BotLeft")) ];
    Canvas->AddSlot().Position(At(256, 256)).Size(Sz(128, 256))[ Art(TEXT("Interface\\MacroFrame\\MacroFrame-BotRight")) ];
    // The frame takes clicks inside its art (HitRectInsets right 34, bottom 75), not the margin.
    Canvas->AddSlot().Position(At(10, 10)).Size(Sz(FrameW - 34.f - 10.f, FrameH - 75.f - 10.f))
    [
        SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
        .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
    ];
    // Title: CREATE_MACROS, GameFontNormal, TOP 0,-17.
    Canvas->AddSlot().Position(At(0, 17)).Size(Sz(FrameW, 14))
    [ SNew(STextBlock).Visibility(EVisibility::HitTestInvisible).Text(FText::FromString(TEXT("Create Macros"))).Font(Font(12))
      .ColorAndOpacity(GoldText()).Justification(ETextJustify::Center).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ];
    // MacroFrameCloseButton: TOPRIGHT -29,-8.
    Canvas->AddSlot().Position(At(FrameW - 29.f - 32.f, 8)).Size(Sz(32, 32))[ CloseButton([this]() { Close(); }) ];
    // MacroFrameTab1 TOPLEFT 65,-39; Tab2 to its right.
    {
        float TabX = 65.f;
        const FString Tab1 = TEXT("General Macros");
        const FString Tab2 = CharacterName.IsEmpty() ? FString(TEXT("Character Specific Macros")) : CharacterName + TEXT(" Specific Macros");
        const float X1 = TabX;
        TSharedRef<SWidget> T1 = BuildTab(Tab1, false, TabX);
        const float X2 = TabX;
        TSharedRef<SWidget> T2 = BuildTab(Tab2, true, TabX);
        Canvas->AddSlot().Position(At(X1, 39)).Size(Sz(X2 - X1, 35))[ T1 ];
        Canvas->AddSlot().Position(At(X2, 39)).Size(Sz(TabX - X2, 35))[ T2 ];
    }
    // MacroButton1..18.
    for (int32 Row = 0; Row < GridRows; ++Row)
        for (int32 Column = 0; Column < GridColumns; ++Column)
        {
            const int32 I = (GridFirstRow + Row) * GridColumns + Column;
            if (I >= Max) continue;
            // r377 (Stefan 16.10: "get the macro slots closer together"): 44 / 43 apart (the XML has
            // 49 / 46), the grid kept centred where it was.
            Canvas->AddSlot().Position(At(59.5f + Column * 44.f, 85.f + Row * 43.f)).Size(Sz(SlotUnits, SlotUnits))[ BuildSlot(FirstIndex() + I) ];
        }
    // MacroHorizontalBarLeft (256 x 16 at 15,-220) and its 75-wide end.
    Canvas->AddSlot().Position(At(15, 220)).Size(Sz(256, 16))[ ArtUV(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar"), 0.f, 1.f, 0.f, .25f) ];
    Canvas->AddSlot().Position(At(271, 220)).Size(Sz(75, 16))[ ArtUV(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar"), 0.f, .29296875f, .25f, .5f) ];
    // Selected macro: UI-EmptySlot (64 x 64 at 16,-228), its button 14,-14 in, the name
    // (GameFontNormalLarge) at its TOPRIGHT -4,-10, Change Name/Icon (170 x 22) at 51,-30.
    Canvas->AddSlot().Position(At(16, 228)).Size(Sz(64, 64))[ Art(TEXT("Interface\\Buttons\\UI-EmptySlot")) ];
    Canvas->AddSlot().Position(At(30, 242)).Size(Sz(SlotUnits, SlotUnits))
    [ SNew(SBox).Visibility(Selected ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
      [ Art(Selected ? FWowMacros::DisplayIcon(*Selected, Connection.Get()) : FString()) ] ];
    Canvas->AddSlot().Position(At(76, 238)).Size(Sz(256, 18))[ TextLabel(Selected ? Selected->Name : FString(), 16, GoldText()) ];
    Canvas->AddSlot().Position(At(67, 258)).Size(Sz(170, 22))
    [ Button(TEXT("Change Name/Icon"), 170.f, [this]() { OpenPopup(false); }, Selected != nullptr && !bPopupOpen) ];
    // ENTER_MACRO_LABEL (GameFontHighlightSmall) under the socket, 8 in.
    Canvas->AddSlot().Position(At(24, 292)).Size(Sz(250, 12))[ TextLabel(TEXT("Enter Macro Commands:"), 10, WhiteText()) ];
    // MacroFrameTextBackground: tooltip backdrop, 322 x 95 at 18,-305; MacroFrameText 286 x 85 at 27,-310.
    Canvas->AddSlot().Position(At(18, 305)).Size(Sz(322, 95))[ SNew(SWowTooltipFrame).Visibility(EVisibility::HitTestInvisible) ];
    Canvas->AddSlot().Position(At(27, 310)).Size(Sz(306, 85))
    [
        SAssignNew(BodyBox, SMultiLineEditableTextBox)
        .IsEnabled(Selected != nullptr)
        .Text(FText::FromString(Selected ? Selected->Body : FString()))
        .Font(Font(10))
        .AutoWrapText(true)
        .BackgroundColor(FLinearColor::Transparent)
        .ForegroundColor(FLinearColor::White)
        .Padding(0)
        .OnTextChanged_Lambda([this](const FText& NewText)
        {
            FString Text = NewText.ToString();
            if (Text.Len() > FWowMacros::MaxBodyLength)
            {
                Text = Text.Left(FWowMacros::MaxBodyLength);
                if (BodyBox.IsValid()) BodyBox->SetText(FText::FromString(Text));
            }
            BodyLength = Text.Len();
            SaveBody(Text);
        })
    ];
    // MacroFrameCharLimitText: BOTTOM -15,105 (GameFontHighlightSmall).
    Canvas->AddSlot().Position(At(0, FrameH - 105.f - 12.f)).Size(Sz(FrameW - 30.f, 12))
    [
        SNew(STextBlock).Visibility(EVisibility::HitTestInvisible).Font(Font(10)).ColorAndOpacity(WhiteText())
        .Justification(ETextJustify::Center).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
        .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d/%d Characters Used"), BodyLength, FWowMacros::MaxBodyLength)); })
    ];
    // Delete BOTTOMLEFT 17,79; New / Exit centred at 222,-422 / 303,-422 (80 x 22 each).
    Canvas->AddSlot().Position(At(17, FrameH - 79.f - 22.f)).Size(Sz(80, 22))
    [ Button(TEXT("Delete"), 80.f, [this]()
        {
            const int32 Deleted = SelectedIndex;
            if (FWowMacros::Delete(Deleted, Connection.Get()))
            {
                Sound(TEXT("igMainMenuOptionCheckBoxOn"));
                // Select the macro that took its place, else the one before it.
                SelectedIndex = FWowMacros::Get(Deleted) ? Deleted : (FWowMacros::Get(Deleted - 1) && Deleted - 1 >= FirstIndex() ? Deleted - 1 : 0);
                Rebuild();
            }
        }, Selected != nullptr && !bPopupOpen) ];
    Canvas->AddSlot().Position(At(222.f - 40.f, 411)).Size(Sz(80, 22))[ Button(TEXT("New"), 80.f, [this]() { OpenPopup(true); }, Count < Max && !bPopupOpen) ];
    Canvas->AddSlot().Position(At(303.f - 40.f, 411)).Size(Sz(80, 22))[ Button(TEXT("Exit"), 80.f, [this]() { Close(); }) ];

    if (bPopupOpen)
    {
        Canvas->AddSlot().Position(At(PopupX, PopupY)).Size(Sz(PopupW, PopupH))[ BuildPopup() ];
    }

    Root->SetWidthOverride(GetPanelSize().X);
    Root->SetHeightOverride(GetPanelSize().Y);
    Root->SetContent(Canvas);
}

TSharedRef<SWidget> SWowMacroFrame::BuildPopup()
{
    const TArray<FString>& Icons = FWowMacros::Icons();
    const int32 TotalRows = FMath::DivideAndRoundUp(Icons.Num(), PopupColumns);
    PopupFirstRow = FMath::Clamp(PopupFirstRow, 0, FMath::Max(0, TotalRows - PopupRows));
    const float Sc = Scale;
    auto At = [Sc](float X, float Y) { return FVector2D(X, Y) * Sc; };
    auto Sz = [Sc](float W, float H) { return FVector2D(W, H) * Sc; };

    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    Canvas->AddSlot().Position(At(0, 0)).Size(Sz(256, 256))[ Art(TEXT("Interface\\MacroFrame\\MacroPopup-TopLeft")) ];
    Canvas->AddSlot().Position(At(256, 0)).Size(Sz(64, 256))[ Art(TEXT("Interface\\MacroFrame\\MacroPopup-TopRight")) ];
    Canvas->AddSlot().Position(At(0, 256)).Size(Sz(256, 64))[ Art(TEXT("Interface\\MacroFrame\\MacroPopup-BotLeft")) ];
    Canvas->AddSlot().Position(At(256, 256)).Size(Sz(64, 64))[ Art(TEXT("Interface\\MacroFrame\\MacroPopup-BotRight")) ];
    Canvas->AddSlot().Position(At(6, 6)).Size(Sz(PopupW - 12.f, PopupH - 12.f))
    [
        SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
        .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
    ];
    // MACRO_POPUP_TEXT at 24,-21 and MACRO_POPUP_CHOOSE_ICON at 24,-69 (GameFontHighlightSmall).
    Canvas->AddSlot().Position(At(24, 21)).Size(Sz(260, 12))[ TextLabel(TEXT("Enter Macro Name (16 Characters Max):"), 10, WhiteText()) ];
    Canvas->AddSlot().Position(At(24, 69)).Size(Sz(260, 12))[ TextLabel(TEXT("Choose an Icon:"), 10, WhiteText()) ];
    // MacroPopupEditBox (200 x 20 at 29,-35) on the UI-ClassTrainer-FilterBorder pieces.
    const FString Filter = TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-FilterBorder");
    Canvas->AddSlot().Position(At(18, 35)).Size(Sz(12, 29))[ ArtUV(Filter, 0.f, .09375f, 0.f, 1.f) ];
    Canvas->AddSlot().Position(At(30, 35)).Size(Sz(175, 29))[ ArtUV(Filter, .09375f, .90625f, 0.f, 1.f) ];
    Canvas->AddSlot().Position(At(205, 35)).Size(Sz(12, 29))[ ArtUV(Filter, .90625f, 1.f, 0.f, 1.f) ];
    Canvas->AddSlot().Position(At(29, 37)).Size(Sz(185, 20))
    [
        SAssignNew(PopupNameBox, SEditableTextBox)
        .Text(FText::FromString(PopupName)).Font(Font(14))
        .BackgroundColor(FLinearColor::Transparent).ForegroundColor(FLinearColor::White).Padding(0)
        .OnTextChanged_Lambda([this](const FText& NewText)
        {
            FString Text = NewText.ToString();
            if (Text.Len() > FWowMacros::MaxNameLength)
            {
                Text = Text.Left(FWowMacros::MaxNameLength);
                if (PopupNameBox.IsValid()) PopupNameBox->SetText(FText::FromString(Text));
            }
            PopupName = Text;
        })
        .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
        {
            if (Commit == ETextCommit::OnEnter) ClosePopup(true);
        })
    ];
    // MacroPopupButton1..20.
    for (int32 Row = 0; Row < PopupRows; ++Row)
        for (int32 Column = 0; Column < PopupColumns; ++Column)
        {
            const int32 IconIndex = (PopupFirstRow + Row) * PopupColumns + Column;
            if (!Icons.IsValidIndex(IconIndex)) continue;
            const bool bChosen = IconIndex == PopupIcon;
            TSharedPtr<SButton> Btn;
            Canvas->AddSlot().Position(At(24.f + Column * 46.f, 85.f + Row * 44.f)).Size(Sz(SlotUnits, SlotUnits))
            [
                SAssignNew(Btn, SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
                .OnClicked_Lambda([this, IconIndex]() { PopupIcon = IconIndex; Sound(TEXT("igMainMenuOptionCheckBoxOn")); Rebuild(); return FReply::Handled(); })
            ];
            TWeakPtr<SButton> Weak = Btn;
            Btn->SetContent(
                SNew(SCanvas)
                + SCanvas::Slot().Position(FVector2D(-4.5f, -3.5f) * Sc).Size(FVector2D(45.f, 45.f) * Sc)[ Art(TEXT("Interface\\Buttons\\UI-EmptySlot-Disabled")) ]
                + SCanvas::Slot().Position(FVector2D(0.f, 1.f) * Sc).Size(FVector2D(SlotUnits, SlotUnits) * Sc)[ Art(Icons[IconIndex]) ]
                + SCanvas::Slot().Position(FVector2D::ZeroVector).Size(FVector2D(SlotUnits, SlotUnits) * Sc)
                [ SNew(SBox).Visibility(bChosen ? EVisibility::HitTestInvisible : EVisibility::Collapsed)[ AddArt(TEXT("Interface\\Buttons\\CheckButtonHilight")) ] ]
                + SCanvas::Slot().Position(FVector2D::ZeroVector).Size(FVector2D(SlotUnits, SlotUnits) * Sc)
                [ SNew(SBox).Visibility_Lambda([Weak]()
                    {
                        const TSharedPtr<SButton> B = Weak.Pin();
                        return B.IsValid() && B->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })[ AddArt(TEXT("Interface\\Buttons\\ButtonHilight-Square")) ] ]);
        }
    // MacroPopupScrollFrame's bar: TOPRIGHT -39,-67 of the popup, up/down buttons 16 x 16 above
    // and below the 163-unit track at x 264.
    auto Scroll = [this, TotalRows](int32 Delta)
    {
        PopupFirstRow = FMath::Clamp(PopupFirstRow + Delta, 0, FMath::Max(0, TotalRows - PopupRows));
        Rebuild();
    };
    auto Arrow = [this](bool bUp, bool bEnabled, TFunction<void()> OnClick)
    {
        const FString Base = bUp ? TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-") : TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-");
        return SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0).IsEnabled(bEnabled)
            .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
            [ ArtUV(Base + (bEnabled ? TEXT("Up") : TEXT("Disabled")), .2f, .8f, .25f, .75f) ];
    };
    Canvas->AddSlot().Position(At(264, 67)).Size(Sz(16, 16))[ Arrow(true, PopupFirstRow > 0, [Scroll]() { Scroll(-1); }) ];
    Canvas->AddSlot().Position(At(264, 246)).Size(Sz(16, 16))[ Arrow(false, PopupFirstRow + PopupRows < TotalRows, [Scroll]() { Scroll(1); }) ];
    // Cancel BOTTOMRIGHT -11,13 (78 x 22); Okay 2 to its left.
    Canvas->AddSlot().Position(At(PopupW - 11.f - 78.f, PopupH - 13.f - 22.f)).Size(Sz(78, 22))[ Button(TEXT("Cancel"), 78.f, [this]() { ClosePopup(false); }) ];
    Canvas->AddSlot().Position(At(PopupW - 11.f - 78.f - 2.f - 78.f, PopupH - 13.f - 22.f)).Size(Sz(78, 22))
    [ Button(TEXT("Okay"), 78.f, [this]() { ClosePopup(true); }, !PopupName.TrimStartAndEnd().IsEmpty()) ];
    return Canvas;
}

void SWowMacroFrame::OpenPopup(bool bNew)
{
    const FWowMacro* Macro = bNew ? nullptr : FWowMacros::Get(SelectedIndex);
    if (!bNew && !Macro) return;
    bPopupOpen = true;
    bPopupNew = bNew;
    PopupName = Macro ? Macro->Name : FString();
    PopupIcon = 0;
    if (Macro) PopupIcon = FMath::Max(0, FWowMacros::Icons().IndexOfByPredicate([Macro](const FString& P) { return P.Equals(Macro->Icon, ESearchCase::IgnoreCase); }));
    PopupFirstRow = FMath::Max(0, PopupIcon / PopupColumns - 1);
    Sound(TEXT("igCharacterInfoOpen"));
    Rebuild();
    if (PopupNameBox.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().SetKeyboardFocus(PopupNameBox, EFocusCause::SetDirectly);
}

void SWowMacroFrame::ClosePopup(bool bAccept)
{
    if (bAccept)
    {
        const TArray<FString>& Icons = FWowMacros::Icons();
        const FString IconPath = Icons.IsValidIndex(PopupIcon) ? Icons[PopupIcon] : FString(FWowMacros::QuestionMarkIcon());
        const FString Name = PopupName.TrimStartAndEnd();
        if (Name.IsEmpty()) return; // MacroPopupOkayButton is disabled without a name
        if (bPopupNew)
        {
            const int32 Index = FWowMacros::Create(Name, IconPath, FString(), bCharacterTab);
            if (Index) SelectedIndex = Index;
        }
        else if (const FWowMacro* Macro = FWowMacros::Get(SelectedIndex))
        {
            FWowMacros::Edit(SelectedIndex, Name, IconPath, Macro->Body);
        }
    }
    bPopupOpen = false;
    Sound(TEXT("igCharacterInfoClose"));
    Rebuild();
    // A new macro: straight into its command text, like the client.
    if (bAccept && bPopupNew && BodyBox.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().SetKeyboardFocus(BodyBox, EFocusCause::SetDirectly);
}
