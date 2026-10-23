#include "WowRealmSelectWidget.h"
#include "WowWindowStyle.h"
#include "UI/WowTextCentering.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"
#include "InputCoreTypes.h"

namespace
{
// RealmList.lua / GlueXML constants.
constexpr int32 MaxRealmsDisplayed = 18;
constexpr float RowX = 22.f, RowY = 56.f, RowPitch = 20.f, RowW = 512.f, RowH = 16.f;
constexpr float PanelW = 640.f, PanelH = 512.f;

const FLinearColor NormalColor(1.f, .82f, 0.f);       // NORMAL_FONT_COLOR
const FLinearColor GlueGold(1.f, .78f, 0.f);          // GlueFontNormal
const FLinearColor Highlight(1.f, 1.f, 1.f);
const FLinearColor GreenColor(.1f, 1.f, .1f);
const FLinearColor RedColor(1.f, .1f, .1f);
const FLinearColor GrayColor(.5f, .5f, .5f);
const FLinearColor BlueColor(0.f, .75f, .95f);

const TCHAR* const TabActive = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ActiveTab");
const TCHAR* const TabInactive = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-InActiveTab");
const TCHAR* const TabHighlight = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-Tab-Highlight");
const TCHAR* const ColumnTabs = TEXT("Interface\\FriendsFrame\\WhoFrame-ColumnTabs");

FBox2f UV(float L, float R, float T, float B) { return FBox2f(FVector2f(L, T), FVector2f(R, B)); }

bool IsDown(const FWowRealmInfo& Realm) { return (Realm.Flags & 0x02) != 0; }

/** RealmList.lua pvp/rp column. */
void TypeText(const FWowRealmInfo& Realm, FString& OutText, FLinearColor& OutColor)
{
    switch (Realm.Type)
    {
    case 1: OutText = TEXT("(PVP)"); OutColor = RedColor; break;
    case 6: OutText = TEXT("(RP)"); OutColor = GreenColor; break;
    case 8: OutText = TEXT("(RPPVP)"); OutColor = NormalColor; break;
    default: OutText = TEXT("Normal"); OutColor = NormalColor; break;
    }
}

/** RealmList.lua load column (realm flags 0x20 recommended, 0x40 new, 0x80 full). */
void LoadText(const FWowRealmInfo& Realm, FString& OutText, FLinearColor& OutColor, int32& OutRank)
{
    if (IsDown(Realm)) { OutText = TEXT("Offline"); OutColor = GrayColor; OutRank = -10; }
    else if (Realm.bLocked) { OutText = TEXT("Locked"); OutColor = RedColor; OutRank = -9; }
    else if (Realm.Flags & 0x20) { OutText = TEXT("New Players"); OutColor = BlueColor; OutRank = -3; }
    else if (Realm.Flags & 0x40) { OutText = TEXT("New"); OutColor = GreenColor; OutRank = -2; }
    else if ((Realm.Flags & 0x80) || Realm.Population >= 2.f) { OutText = TEXT("Full"); OutColor = RedColor; OutRank = 3; }
    else if (Realm.Population > 1.25f) { OutText = TEXT("High"); OutColor = RedColor; OutRank = 2; }
    else if (Realm.Population < .75f) { OutText = TEXT("Low"); OutColor = GreenColor; OutRank = 0; }
    else { OutText = TEXT("Medium"); OutColor = NormalColor; OutRank = 1; }
}

float MeasureText(const FString& Text, const FSlateFontInfo& Font)
{
    if (!FSlateApplication::IsInitialized()) return Text.Len() * Font.Size;
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
}

/** Full-screen black that swallows clicks, like RealmList's enableMouse. */
class SRealmBlocker : public SBorder
{
public:
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
};
}

// ── Construction ──────────────────────────────────────────────────────────

void SWowRealmSelectWidget::Construct(const FArguments& InArgs)
{
    TextureLoader = InArgs._TextureLoader;
    TSharedRef<SCanvas> Screen = SNew(SCanvas);
    // RealmListBackground: 640x512, CENTER +24 of the 1024x768 glue screen.
    Screen->AddSlot().Position(FVector2D(512.f - PanelW * .5f + 24.f, 384.f - PanelH * .5f)).Size(FVector2D(PanelW, PanelH))
        [SAssignNew(PanelRoot, SBox).WidthOverride(PanelW).HeightOverride(PanelH)];

    ChildSlot
    [SNew(SOverlay)
        + SOverlay::Slot()
        [SNew(SRealmBlocker).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .BorderBackgroundColor(FLinearColor(0, 0, 0, .75f))]
        + SOverlay::Slot()
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
            [SNew(SBox).WidthOverride(1024.f).HeightOverride(768.f)[Screen]]]];
    Rebuild();
}

void SWowRealmSelectWidget::PopulateRealms(const TArray<FWowRealmInfo>& Realms, int32 ActiveIndex)
{
    CachedRealms = Realms;
    CurrentRealmIndex = ActiveIndex;
    Categories.Reset();
    for (const FWowRealmInfo& Realm : Realms) Categories.AddUnique(Realm.Category);
    Categories.Sort();
    SelectedCategory = Realms.IsValidIndex(ActiveIndex) ? Realms[ActiveIndex].Category : (Categories.IsEmpty() ? 0 : Categories[0]);
    // The current realm starts highlighted, as RealmListUpdate does with currentRealm.
    SelectedRealm = IsSelectable(ActiveIndex) ? ActiveIndex : INDEX_NONE;
    Offset = 0;
    const TArray<int32> Visible = VisibleRealms();
    const int32 Row = Visible.Find(SelectedRealm);
    if (Row >= MaxRealmsDisplayed) Offset = Row - MaxRealmsDisplayed + 1;
    Rebuild();
}

void SWowRealmSelectWidget::SetStatusText(const FString& Text)
{
    // The original frame has no status line; keep the controller's progress/error messages
    // but not the old "Select a realm" hint.
    StatusText = Text == TEXT("Select a realm to connect.") ? FString() : Text;
}

// ── State ─────────────────────────────────────────────────────────────────

TArray<int32> SWowRealmSelectWidget::VisibleRealms() const
{
    TArray<int32> Result;
    for (int32 Index = 0; Index < CachedRealms.Num(); ++Index)
        if (CachedRealms[Index].Category == SelectedCategory) Result.Add(Index);
    if (Sort == ESort::None) return Result;
    auto Key = [this](int32 Index, int32& OutRank) -> FString
    {
        const FWowRealmInfo& Realm = CachedRealms[Index];
        FString Text; FLinearColor Color;
        switch (Sort)
        {
        case ESort::Type: TypeText(Realm, Text, Color); OutRank = 0; return Text;
        case ESort::Characters: OutRank = Realm.CharacterCount; return FString();
        case ESort::Load: LoadText(Realm, Text, Color, OutRank); return FString();
        default: OutRank = 0; return Realm.Name;
        }
    };
    const bool bDesc = bSortDescending;
    Result.StableSort([&](int32 A, int32 B)
    {
        int32 RankA = 0, RankB = 0;
        const FString KeyA = Key(A, RankA), KeyB = Key(B, RankB);
        if (RankA != RankB) return bDesc ? RankA > RankB : RankA < RankB;
        const int32 Cmp = KeyA.Compare(KeyB, ESearchCase::IgnoreCase);
        if (Cmp != 0) return bDesc ? Cmp > 0 : Cmp < 0;
        return CachedRealms[A].Name.Compare(CachedRealms[B].Name, ESearchCase::IgnoreCase) < 0;
    });
    return Result;
}

bool SWowRealmSelectWidget::IsSelectable(int32 RealmIndex) const
{
    return CachedRealms.IsValidIndex(RealmIndex) && !IsDown(CachedRealms[RealmIndex]);
}

void SWowRealmSelectWidget::SelectRow(int32 RealmIndex)
{
    if (IsSelectable(RealmIndex)) SelectedRealm = RealmIndex;
}

void SWowRealmSelectWidget::Accept()
{
    if (IsSelectable(SelectedRealm)) OnRealmSelected.ExecuteIfBound(SelectedRealm);
}

void SWowRealmSelectWidget::Cancel()
{
    OnCancel.ExecuteIfBound();
}

void SWowRealmSelectWidget::SetSort(ESort NewSort)
{
    bSortDescending = Sort == NewSort ? !bSortDescending : NewSort == ESort::Characters;
    Sort = NewSort;
    Offset = 0;
    Rebuild();
}

void SWowRealmSelectWidget::Scroll(int32 Delta)
{
    const int32 MaxOffset = FMath::Max(0, VisibleRealms().Num() - MaxRealmsDisplayed);
    const int32 NewOffset = FMath::Clamp(Offset + Delta, 0, MaxOffset);
    if (NewOffset == Offset) return;
    Offset = NewOffset;
    Rebuild();
}

FReply SWowRealmSelectWidget::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    const FKey Key = Event.GetKey();
    if (Key == EKeys::Escape) { Cancel(); return FReply::Handled(); }
    if (Key == EKeys::Enter) { Accept(); return FReply::Handled(); }
    if (Key == EKeys::Up || Key == EKeys::Down)
    {
        const TArray<int32> Visible = VisibleRealms();
        int32 Row = Visible.Find(SelectedRealm);
        const int32 Step = Key == EKeys::Up ? -1 : 1;
        for (int32 Next = Row + Step; Visible.IsValidIndex(Next); Next += Step)
            if (IsSelectable(Visible[Next])) { Row = Next; break; }
        if (Visible.IsValidIndex(Row))
        {
            SelectedRealm = Visible[Row];
            if (Row < Offset) Scroll(Row - Offset);
            else if (Row >= Offset + MaxRealmsDisplayed) Scroll(Row - Offset - MaxRealmsDisplayed + 1);
        }
        return FReply::Handled();
    }
    return SCompoundWidget::OnKeyDown(Geometry, Event);
}

FReply SWowRealmSelectWidget::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    Scroll(Event.GetWheelDelta() > 0 ? -1 : 1);
    return FReply::Handled();
}

// ── Layout ────────────────────────────────────────────────────────────────

void SWowRealmSelectWidget::Rebuild()
{
    if (!PanelRoot.IsValid()) return;
    ButtonStyles.Reset();
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    BuildPanelArt(*Canvas);
    BuildSortButtons(*Canvas);
    BuildRows(*Canvas);
    BuildScrollBar(*Canvas, VisibleRealms().Num());
    BuildTabs(*Canvas);
    BuildButtons(*Canvas);
    PanelRoot->SetContent(Canvas);
}

void SWowRealmSelectWidget::BuildPanelArt(SCanvas& Canvas)
{
    Place(Canvas, 0, 0, 256, 256, Img(TEXT("Interface\\HelpFrame\\HelpFrame-TopLeft")));
    Place(Canvas, 256, 0, 256, 256, Img(TEXT("Interface\\HelpFrame\\HelpFrame-Top")));
    Place(Canvas, 512, 0, 128, 256, Img(TEXT("Interface\\HelpFrame\\HelpFrame-TopRight")));
    Place(Canvas, 0, 256, 256, 256, Img(TEXT("Interface\\HelpFrame\\HelpFrame-BotLeft")));
    Place(Canvas, 256, 256, 256, 256, Img(TEXT("Interface\\HelpFrame\\HelpFrame-Bottom")));
    Place(Canvas, 512, 256, 128, 256, Img(TEXT("Interface\\HelpFrame\\HelpFrame-BotRight")));

    // RealmListHeader (256x64, TOP -12,+12) with SERVER_SELECTION in GlueFontNormalSmall.
    const float HeaderX = (PanelW - 256.f) * .5f - 12.f;
    Place(Canvas, HeaderX, -12, 256, 64, Img(TEXT("Interface\\DialogFrame\\UI-DialogBox-Header")));
    Place(Canvas, HeaderX, 2, 256, 16,
        SNew(STextBlock).Text(FText::FromString(TEXT("Realm Selection"))).Font(GlueFont(12))
            .ColorAndOpacity(GlueGold).ShadowOffset(FVector2D(1, 1)).Justification(ETextJustify::Center));

    // RealmListCloseButton (GlueCloseButton, TOPRIGHT -42,-3).
    auto CloseStyle = MakeShared<FButtonStyle>(FButtonStyle::GetDefault());
    if (const FSlateBrush* Up = Brush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up")))
        CloseStyle->SetNormal(*Up).SetHovered(*Up);
    if (const FSlateBrush* Down = Brush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down")))
        CloseStyle->SetPressed(*Down);
    CloseStyle->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
    ButtonStyles.Add(CloseStyle);
    TSharedPtr<SButton> Close;
    Place(Canvas, PanelW - 42 - 32, 3, 32, 32,
        SNew(SOverlay)
        + SOverlay::Slot()
        [SAssignNew(Close, SButton).ButtonStyle(&CloseStyle.Get()).ContentPadding(0)
            .ClickMethod(EButtonClickMethod::MouseDown)
            .OnClicked_Lambda([this]() { Cancel(); return FReply::Handled(); })]
        + SOverlay::Slot()
        [AddImg(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Highlight"), UV(0, 1, 0, 1),
            TAttribute<FLinearColor>::CreateLambda([WeakClose = TWeakPtr<SButton>(Close)]()
            {
                const TSharedPtr<SButton> Button = WeakClose.Pin();
                return Button.IsValid() && Button->IsHovered() ? FLinearColor::White : FLinearColor::Transparent;
            }))]);
}

void SWowRealmSelectWidget::BuildSortButtons(SCanvas& Canvas)
{
    struct FColumn { const TCHAR* Text; float W; ESort Sort; };
    const FColumn Columns[] = {
        { TEXT("Realm Name"), 223, ESort::Name }, { TEXT("Type"), 80, ESort::Type },
        { TEXT("Characters"), 100, ESort::Characters }, { TEXT("Population"), 138, ESort::Load } };
    float X = 21.f;
    const float Y = 31.f, H = 19.f; // BOTTOMLEFT at TOPLEFT +21,-50
    for (const FColumn& Column : Columns)
    {
        Place(Canvas, X, Y, 5, H, Img(ColumnTabs, UV(0, .078125f, 0, .59375f)));
        Place(Canvas, X + 5, Y, Column.W - 9, H, Img(ColumnTabs, UV(.078125f, .90625f, 0, .59375f)));
        Place(Canvas, X + Column.W - 4, Y, 4, H, Img(ColumnTabs, UV(.90625f, .96875f, 0, .59375f)));
        // UI-SortArrow: NormalTexture, right of the text, flipped for a descending sort.
        const bool bFlip = Sort == Column.Sort && bSortDescending;
        const FBox2f ArrowUV = bFlip ? UV(0, .5625f, 1, 0) : UV(0, .5625f, 0, 1);
        TSharedPtr<SButton> Button;
        Place(Canvas, X, Y, Column.W, H,
            SNew(SOverlay)
            + SOverlay::Slot()
            [SAssignNew(Button, SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
                .ContentPadding(FMargin(8, 0, 0, 0)).VAlign(VAlign_Center).HAlign(HAlign_Left)
                .ClickMethod(EButtonClickMethod::MouseDown)
                .OnClicked_Lambda([this, Sort = Column.Sort]() { SetSort(Sort); return FReply::Handled(); })
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(STextBlock).Text(FText::FromString(Column.Text)).Font(GlueFont(12))
                        .RenderTransform(WowCapCenterTransform(GlueFont(12)))
                        .ColorAndOpacity(Highlight).ShadowOffset(FVector2D(1, 1))]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3, 2, 0, 0)
                    [SNew(SBox).WidthOverride(9).HeightOverride(8)[Img(TEXT("Interface\\Buttons\\UI-SortArrow"), ArrowUV)]]]]
            + SOverlay::Slot().VAlign(VAlign_Center)
            [SNew(SBox).HeightOverride(24).Padding(FMargin(0, 0, -4, 0)).Visibility(EVisibility::HitTestInvisible)
                [AddImg(TabHighlight, UV(0, 1, 0, 1),
                    TAttribute<FLinearColor>::CreateLambda([WeakButton = TWeakPtr<SButton>(Button)]()
                    {
                        const TSharedPtr<SButton> B = WeakButton.Pin();
                        return B.IsValid() && B->IsHovered() ? FLinearColor::White : FLinearColor::Transparent;
                    }))]]);
        X += Column.W;
    }
}

void SWowRealmSelectWidget::BuildRows(SCanvas& Canvas)
{
    const TArray<int32> Visible = VisibleRealms();
    for (int32 Row = 0; Row < MaxRealmsDisplayed; ++Row)
    {
        const int32 ListIndex = Offset + Row;
        if (!Visible.IsValidIndex(ListIndex)) break;
        const int32 RealmIndex = Visible[ListIndex];
        const FWowRealmInfo& Realm = CachedRealms[RealmIndex];
        const float Y = RowY + Row * RowPitch;
        const bool bDown = IsDown(Realm);
        const bool bHasCharacters = Realm.CharacterCount > 0;

        // RealmListHighlight: UI-QuestLogTitleHighlight (ADD), 557 wide, green with characters
        // on the realm, gold without.
        const FLinearColor HighlightTint = bHasCharacters ? FLinearColor(.1f, 1.f, .1f) : FLinearColor(1.f, .78f, 0.f);
        Place(Canvas, RowX, Y, 557, RowH,
            SNew(SBox).Visibility(EVisibility::HitTestInvisible)
            [AddImg(TEXT("Interface\\QuestFrame\\UI-QuestLogTitleHighlight"), UV(0, 1, 0, 1),
                TAttribute<FLinearColor>::CreateLambda([this, RealmIndex, HighlightTint]()
                {
                    return SelectedRealm == RealmIndex ? HighlightTint : FLinearColor::Transparent;
                }))]);

        FString TypeLabel, LoadLabel;
        FLinearColor TypeColor, LoadColor;
        int32 LoadRank = 0;
        TypeText(Realm, TypeLabel, TypeColor);
        LoadText(Realm, LoadLabel, LoadColor, LoadRank);

        TSharedRef<SBorder> RowWidget = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .OnMouseButtonDown_Lambda([this, RealmIndex](const FGeometry&, const FPointerEvent& Event)
            {
                if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
                SelectRow(RealmIndex);
                return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
            })
            .OnMouseDoubleClick_Lambda([this, RealmIndex](const FGeometry&, const FPointerEvent& Event)
            {
                if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !IsSelectable(RealmIndex)) return FReply::Unhandled();
                SelectRow(RealmIndex);
                Accept();
                return FReply::Handled();
            });
        const TWeakPtr<SBorder> WeakRow = RowWidget;
        auto IsHot = [this, WeakRow, RealmIndex]()
        {
            const TSharedPtr<SBorder> Pinned = WeakRow.Pin();
            return SelectedRealm == RealmIndex || (Pinned.IsValid() && Pinned->IsHovered());
        };
        // Name: RealmCharactersNormal (green) / RealmNoCharactersNormal (gold) / RealmDownNormal,
        // GlueFontHighlightLeft while hovered.
        const FLinearColor NameColor = bDown ? GrayColor : bHasCharacters ? GreenColor : GlueGold;
        const FLinearColor NameHot = bDown ? FLinearColor(.8f, .8f, .8f) : Highlight;
        // Cells are centred on the row (a 16px line inside a 20px band, so the 15px realm name
        // and the descenders of the smaller texts have room); capitals centred optically.
        TSharedRef<SCanvas> Cells = SNew(SCanvas);
        const float CellMidY = RowPitch * .5f;
        Cells->AddSlot().Position(FVector2D(5, CellMidY)).Size(FVector2D(220, RowPitch)).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(FText::FromString(Realm.Name)).Font(GlueFont(15)).ShadowOffset(FVector2D(1, 1))
                .RenderTransform(WowCapCenterTransform(GlueFont(15)))
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                .ColorAndOpacity_Lambda([IsHot, NameColor, NameHot]() { return FSlateColor(IsHot() ? NameHot : NameColor); })];
        // The selected realm shows its type and load in white.
        auto SelectedWhite = [this, RealmIndex](FLinearColor Base)
        {
            return [this, RealmIndex, Base]() { return FSlateColor(SelectedRealm == RealmIndex ? Highlight : Base); };
        };
        Cells->AddSlot().Position(FVector2D(235, CellMidY)).Size(FVector2D(60, RowPitch)).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(FText::FromString(TypeLabel)).Font(GlueFont(12)).ShadowOffset(FVector2D(1, 1))
                .RenderTransform(WowCapCenterTransform(GlueFont(12)))
                .Justification(ETextJustify::Center).ColorAndOpacity_Lambda(SelectedWhite(TypeColor))];
        if (bHasCharacters)
            Cells->AddSlot().Position(FVector2D(335, CellMidY)).Size(FVector2D(32, RowPitch)).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("(%d)"), Realm.CharacterCount)))
                    .Font(GlueFont(12)).ShadowOffset(FVector2D(1, 1)).Justification(ETextJustify::Center)
                    .RenderTransform(WowCapCenterTransform(GlueFont(12)))
                    .ColorAndOpacity(Highlight)];
        Cells->AddSlot().Position(FVector2D(412, CellMidY)).Size(FVector2D(110, RowPitch)).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(FText::FromString(LoadLabel)).Font(GlueFont(12)).ShadowOffset(FVector2D(1, 1))
                .RenderTransform(WowCapCenterTransform(GlueFont(12)))
                .Justification(ETextJustify::Center).ColorAndOpacity_Lambda(SelectedWhite(LoadColor))];
        RowWidget->SetContent(Cells);
        Place(Canvas, RowX, Y - (RowPitch - RowH) * .5f, RowW, RowPitch, RowWidget);
    }
}

void SWowRealmSelectWidget::BuildScrollBar(SCanvas& Canvas, int32 NumRealms)
{
    // GlueScrollFrame_Update hides the scroll frame (and its bar art) when everything fits.
    if (NumRealms <= MaxRealmsDisplayed) return;
    const TCHAR* Bar = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar");
    const float FrameX = 49.f, FrameY = 53.f, FrameW = 510.f, FrameH = 410.f;
    const float BarX = FrameX + FrameW - 2.f;
    const float TopY = FrameY - 5.f, BottomY = FrameY + FrameH + 2.f - 106.f;
    Place(Canvas, BarX, TopY, 31, 256, Img(Bar, UV(0, .484375f, 0, 1)));
    Place(Canvas, BarX, BottomY, 31, 106, Img(Bar, UV(.515625f, 1, 0, .4140625f)));
    if (BottomY > TopY + 256.f)
        Place(Canvas, BarX, TopY + 256.f, 31, BottomY - TopY - 256.f, Img(Bar, UV(0, .484375f, .75f, 1)));

    auto ArrowButton = [this](const FString& Base, bool bEnabled, int32 Delta) -> TSharedRef<SWidget>
    {
        auto Style = MakeShared<FButtonStyle>(FButtonStyle::GetDefault());
        const FBox2f Inner = UV(.23f, .77f, .23f, .77f);
        if (const FSlateBrush* Up = Brush(Base + TEXT("-Up"), Inner)) Style->SetNormal(*Up).SetHovered(*Up);
        if (const FSlateBrush* Down = Brush(Base + TEXT("-Down"), Inner)) Style->SetPressed(*Down);
        if (const FSlateBrush* Off = Brush(Base + TEXT("-Disabled"), Inner)) Style->SetDisabled(*Off);
        Style->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        ButtonStyles.Add(Style);
        return SNew(SButton).ButtonStyle(&Style.Get()).ContentPadding(0).IsEnabled(bEnabled)
            .OnClicked_Lambda([this, Delta]() { Scroll(Delta); return FReply::Handled(); });
    };
    const int32 MaxOffset = NumRealms - MaxRealmsDisplayed;
    const float BarLeft = FrameX + FrameW + 6.f, UpY = FrameY, DownY = FrameY + FrameH - 18.f;
    Place(Canvas, BarLeft - 1.f, UpY, 18, 18, ArrowButton(TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton"), Offset > 0, -1));
    Place(Canvas, BarLeft - 1.f, DownY, 18, 18, ArrowButton(TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton"), Offset < MaxOffset, 1));
    const float TrackTop = UpY + 18.f, TrackBottom = DownY - 16.f;
    const float KnobY = TrackTop + (TrackBottom - TrackTop) * (float(Offset) / FMath::Max(1, MaxOffset));
    Place(Canvas, BarLeft, KnobY, 16, 16, Img(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"), UV(.25f, .75f, .25f, .75f)));
}

void SWowRealmSelectWidget::BuildTabs(SCanvas& Canvas)
{
    // RealmList_UpdateTabs: hidden when the server sends one category.
    if (Categories.Num() <= 1) return;
    const FSlateFontInfo Font = GlueFont(12);
    float X = 11.f;
    const float Y = PanelH + 15.f - 32.f;
    for (const uint8 Category : Categories)
    {
        const FString Label = FString::Printf(TEXT("Group %u"), Category);
        const float W = FMath::RoundToFloat(MeasureText(Label, Font)) + 40.f; // GlueTemplates_TabResize
        const bool bSelected = Category == SelectedCategory;
        // Selected = disabled state: UI-Character-ActiveTab drawn 3 px higher.
        const TCHAR* Art = bSelected ? TabActive : TabInactive;
        const float ArtY = bSelected ? Y - 3.f : Y;
        Place(Canvas, X, ArtY, 20, 32, Img(Art, UV(0, .15625f, 0, 1)));
        Place(Canvas, X + 20, ArtY, W - 40, 32, Img(Art, UV(.15625f, .84375f, 0, 1)));
        Place(Canvas, X + W - 20, ArtY, 20, 32, Img(Art, UV(.84375f, 1, 0, 1)));
        TSharedPtr<SButton> Button;
        Place(Canvas, X, Y, W, 32,
            SNew(SOverlay)
            + SOverlay::Slot()
            [SAssignNew(Button, SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
                .ContentPadding(FMargin(0, 0, 0, 4)).HAlign(HAlign_Center).VAlign(VAlign_Center)
                .ClickMethod(EButtonClickMethod::MouseDown).IsEnabled(!bSelected)
                .OnClicked_Lambda([this, Category]()
                {
                    SelectedCategory = Category;
                    Offset = 0;
                    if (!CachedRealms.IsValidIndex(SelectedRealm) || CachedRealms[SelectedRealm].Category != Category)
                        SelectedRealm = INDEX_NONE;
                    Rebuild();
                    return FReply::Handled();
                })
                [SNew(STextBlock).Text(FText::FromString(Label)).Font(Font).ShadowOffset(FVector2D(1, 1))
                    .RenderTransform(WowCapCenterTransform(Font))
                    .ColorAndOpacity(bSelected ? Highlight : GlueGold)]]
            + SOverlay::Slot().Padding(FMargin(10, 0, 10, 4))
            [SNew(SBox).Visibility(EVisibility::HitTestInvisible)
                [AddImg(TabHighlight, UV(0, 1, 0, 1),
                    TAttribute<FLinearColor>::CreateLambda([WeakButton = TWeakPtr<SButton>(Button)]()
                    {
                        const TSharedPtr<SButton> B = WeakButton.Pin();
                        return B.IsValid() && B->IsHovered() && B->IsEnabled() ? FLinearColor::White : FLinearColor::Transparent;
                    }))]]);
        X += W - 15.f;
    }
}

void SWowRealmSelectWidget::BuildButtons(SCanvas& Canvas)
{
    // GlueDialogButtonTemplate: red Glue-Panel-Button art, GlueFontNormal (gold / white / grey).
    auto GlueButton = [this](const TCHAR* Text, TFunction<void()> OnClick, TAttribute<bool> Enabled) -> TSharedRef<SWidget>
    {
        auto Style = MakeShared<FButtonStyle>(FButtonStyle::GetDefault());
        const FBox2f ArtUV = UV(0, .578125f, 0, .75f);
        if (const FSlateBrush* Up = Brush(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Up"), ArtUV)) Style->SetNormal(*Up).SetHovered(*Up);
        if (const FSlateBrush* Down = Brush(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Down"), ArtUV)) Style->SetPressed(*Down);
        if (const FSlateBrush* Off = Brush(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Disabled"), ArtUV)) Style->SetDisabled(*Off);
        // Text centred on the painted art (WowGlueLabelTransform); pressed pushes it 1px right/down.
        Style->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(2, 2, 0, 0));
        ButtonStyles.Add(Style);
        TSharedPtr<SButton> Button;
        SAssignNew(Button, SButton).ButtonStyle(&Style.Get()).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .IsEnabled(Enabled)
            .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); });
        const TWeakPtr<SButton> WeakButton = Button;
        Button->SetContent(SNew(STextBlock).Text(FText::FromString(Text)).Font(GlueFont(15)).ShadowOffset(FVector2D(1, 1))
            .RenderTransform(WowGlueLabelTransform(WeakButton, GlueFont(15)))
            .ColorAndOpacity_Lambda([WeakButton]()
            {
                const TSharedPtr<SButton> B = WeakButton.Pin();
                if (!B.IsValid() || !B->IsEnabled()) return FSlateColor(GrayColor);
                return FSlateColor(B->IsHovered() ? Highlight : GlueGold);
            }));
        return SNew(SOverlay)
            + SOverlay::Slot()[Button.ToSharedRef()]
            + SOverlay::Slot()
            [SNew(SBox).Visibility(EVisibility::HitTestInvisible)
                [AddImg(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Highlight"), UV(0, .625f, 0, .6875f),
                    TAttribute<FLinearColor>::CreateLambda([WeakButton]()
                    {
                        const TSharedPtr<SButton> B = WeakButton.Pin();
                        return B.IsValid() && B->IsHovered() && B->IsEnabled() ? FLinearColor::White : FLinearColor::Transparent;
                    }))]];
    };
    const float CancelX = PanelW - 46.f - 125.f, ButtonY = PanelH - 13.f - 35.f;
    Place(Canvas, CancelX, ButtonY, 125, 35, GlueButton(TEXT("Cancel"), [this]() { Cancel(); }, true));
    Place(Canvas, CancelX + 8.f - 125.f, ButtonY, 125, 35, GlueButton(TEXT("Okay"), [this]() { Accept(); },
        TAttribute<bool>::CreateLambda([this]() { return IsSelectable(SelectedRealm); })));

    // Connection progress and errors from the login controller (not part of the original frame).
    Place(Canvas, 24, ButtonY + 4.f, CancelX - 125.f - 32.f, 30,
        SNew(STextBlock).Font(GlueFont(12)).ShadowOffset(FVector2D(1, 1)).ColorAndOpacity(Highlight).AutoWrapText(true)
            .Text_Lambda([this]() { return FText::FromString(StatusText); }));
}

// ── Helpers ───────────────────────────────────────────────────────────────

void SWowRealmSelectWidget::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y)).Size(FVector2D(W, H))[Widget];
}

FSlateFontInfo SWowRealmSelectWidget::GlueFont(float WowPixels)
{
    // FRIZQT__ with outline="NORMAL"; Slate sizes are points (0.75 px).
    FSlateFontInfo Font = FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * .75f)));
    Font.OutlineSettings.OutlineSize = 1;
    Font.OutlineSettings.OutlineColor = FLinearColor::Black;
    return Font;
}

const FSlateBrush* SWowRealmSelectWidget::Brush(const FString& Path, FBox2f InUV) const
{
    const FString Key = FString::Printf(TEXT("%s|%.4f,%.4f,%.4f,%.4f"), *Path, InUV.Min.X, InUV.Min.Y, InUV.Max.X, InUV.Max.Y);
    if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Key)) return Found->Get();
    UTexture2D* Texture = nullptr;
    if (const TStrongObjectPtr<UTexture2D>* Loaded = Textures.Find(Path)) Texture = Loaded->Get();
    else if (!MissingTextures.Contains(Path) && TextureLoader)
    {
        Texture = TextureLoader(Path);
        if (Texture) Textures.Add(Path, TStrongObjectPtr<UTexture2D>(Texture));
        else MissingTextures.Add(Path);
    }
    if (!Texture) return nullptr;
    auto NewBrush = MakeShared<FSlateBrush>();
    NewBrush->SetResourceObject(Texture);
    NewBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    NewBrush->DrawAs = ESlateBrushDrawType::Image;
    NewBrush->Tiling = ESlateBrushTileType::NoTile;
    NewBrush->SetUVRegion(InUV);
    Brushes.Add(Key, NewBrush);
    return &NewBrush.Get();
}

TSharedRef<SWidget> SWowRealmSelectWidget::Img(const FString& Path, FBox2f InUV) const
{
    const FSlateBrush* Found = Brush(Path, InUV);
    if (!Found) return SNullWidget::NullWidget;
    return SNew(SImage).Image(Found).Visibility(EVisibility::HitTestInvisible);
}

TSharedRef<SWidget> SWowRealmSelectWidget::AddImg(const FString& Path, FBox2f InUV, TAttribute<FLinearColor> Tint) const
{
    const FSlateBrush* Found = Brush(Path, InUV);
    if (!Found) return SNullWidget::NullWidget;
    return SNew(SWowAddImage).Image(Found).ColorAndOpacity(Tint).Visibility(EVisibility::HitTestInvisible);
}
