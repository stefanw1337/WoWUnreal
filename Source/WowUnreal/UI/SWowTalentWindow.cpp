#include "SWowTalentWindow.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "WowTextCentering.h"
#include "WowTalentModel.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowRetainedSlateBrush.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/TalentDbc.h"
#include "Formats/Dbc/TalentTabDbc.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Engine/Texture2D.h"

// Layout numbers below are the 3.3.5 originals (Blizzard_TalentUI.xml, TalentFrameTemplates.xml,
// TalentFrameBase.lua, Blizzard_GlyphUI.xml/.lua) in WoW UI units from the frame's top-left.
namespace
{
    constexpr float TalentFrameW = 384.f, TalentFrameH = 512.f;
    constexpr float TalentInitialX = 35.f, TalentInitialY = 20.f, TalentStep = 63.f;
    constexpr float TalentButtonSize = 37.f;      // ItemButtonTemplate
    constexpr float TalentBranchSize = 32.f;      // TALENT_BUTTON_SIZE / branch and arrow pieces
    constexpr int32 TalentTiers = 15, TalentColumns = 4;
    constexpr float TalentScrollX = 23.f, TalentScrollY = 77.f, TalentScrollW = 296.f;

    const FLinearColor TalentNormalColor = WowColor::FromWow(1.f, .82f, 0.f);      // NORMAL_FONT_COLOR
    const FLinearColor TalentGreenColor = WowColor::FromWow(.1f, 1.f, .1f);        // GREEN_FONT_COLOR
    const FLinearColor TalentGreyColor = WowColor::FromWow(.5f, .5f, .5f);         // GRAY_FONT_COLOR
    const FLinearColor TalentHighlightColor(1.f, 1.f, 1.f);    // HIGHLIGHT_FONT_COLOR

    // TALENT_BRANCH_TEXTURECOORDS / TALENT_ARROW_TEXTURECOORDS: {left, right, top, bottom}
    // for [1] (requirements met) and [-1] (not met).
    struct FTalentUV { float L, R, T, B; };
    FTalentUV TalentBranchUV(const TCHAR* Name, int32 State)
    {
        const bool bOn = State > 0;
        const float T = bOn ? 0.f : .5f, B = bOn ? .5f : 1.f;
        if (!FCString::Strcmp(Name, TEXT("up"))) return bOn ? FTalentUV{.12890625f, .25390625f, 0.f, .484375f} : FTalentUV{.12890625f, .25390625f, .515625f, 1.f};
        if (!FCString::Strcmp(Name, TEXT("down"))) return bOn ? FTalentUV{0.f, .125f, 0.f, .484375f} : FTalentUV{0.f, .125f, .515625f, 1.f};
        if (!FCString::Strcmp(Name, TEXT("left")) || !FCString::Strcmp(Name, TEXT("right"))) return {.2578125f, .3828125f, T, B};
        if (!FCString::Strcmp(Name, TEXT("topright"))) return {.515625f, .640625f, T, B};
        if (!FCString::Strcmp(Name, TEXT("topleft"))) return {.640625f, .515625f, T, B};
        if (!FCString::Strcmp(Name, TEXT("bottomright"))) return {.38671875f, .51171875f, T, B};
        if (!FCString::Strcmp(Name, TEXT("bottomleft"))) return {.51171875f, .38671875f, T, B};
        if (!FCString::Strcmp(Name, TEXT("tdown"))) return {.64453125f, .76953125f, T, B};
        return {.7734375f, .8984375f, T, B}; // tup
    }
    FTalentUV TalentArrowUV(const TCHAR* Name, int32 State)
    {
        const float T = State > 0 ? 0.f : .5f, B = State > 0 ? .5f : 1.f;
        if (!FCString::Strcmp(Name, TEXT("right"))) return {1.f, .5f, T, B};
        if (!FCString::Strcmp(Name, TEXT("left"))) return {.5f, 1.f, T, B};
        return {0.f, .5f, T, B}; // top
    }

    // TalentFrame.TALENT_BRANCH_ARRAY node.
    struct FTalentNode { bool bHasTalent = false; int32 Up = 0, Down = 0, Left = 0, Right = 0, LeftArrow = 0, RightArrow = 0, TopArrow = 0; };
    struct FTalentGrid
    {
        FTalentNode Nodes[TalentTiers + 2][TalentColumns + 2];
        FTalentNode& At(int32 Tier, int32 Column) { return Nodes[FMath::Clamp(Tier, 0, TalentTiers + 1)][FMath::Clamp(Column, 0, TalentColumns + 1)]; }
    };

    // TalentFrame_DrawLines (1-based tiers/columns, Met is 1 or -1).
    void TalentDrawLines(FTalentGrid& Grid, int32 ButtonTier, int32 ButtonColumn, int32 Tier, int32 Column, int32 Met)
    {
        if (ButtonColumn == Column)
        {
            for (int32 I = Tier + 1; I <= ButtonTier - 1; ++I) if (Grid.At(I, ButtonColumn).bHasTalent) return;
            for (int32 I = Tier; I <= ButtonTier - 1; ++I)
            {
                Grid.At(I, ButtonColumn).Down = Met;
                if (I + 1 <= ButtonTier - 1) Grid.At(I + 1, ButtonColumn).Up = Met;
            }
            Grid.At(ButtonTier, ButtonColumn).TopArrow = Met;
            return;
        }
        if (ButtonTier == Tier)
        {
            const int32 Left = FMath::Min(ButtonColumn, Column), Right = FMath::Max(ButtonColumn, Column);
            for (int32 I = Left + 1; I <= Right - 1; ++I) if (Grid.At(Tier, I).bHasTalent) return;
            for (int32 I = Left; I <= Right - 1; ++I) { Grid.At(Tier, I).Right = Met; Grid.At(Tier, I + 1).Left = Met; }
            if (ButtonColumn < Column) Grid.At(ButtonTier, ButtonColumn).RightArrow = Met;
            else Grid.At(ButtonTier, ButtonColumn).LeftArrow = Met;
            return;
        }
        int32 Left = FMath::Min(ButtonColumn, Column), Right = FMath::Max(ButtonColumn, Column);
        if (Left == Column) ++Left; else --Right;
        bool bBlocked = false;
        for (int32 I = Left; I <= Right; ++I) if (Grid.At(Tier, I).bHasTalent) bBlocked = true;
        Left = FMath::Min(ButtonColumn, Column); Right = FMath::Max(ButtonColumn, Column);
        if (!bBlocked)
        {
            Grid.At(Tier, ButtonColumn).Down = Met;
            Grid.At(ButtonTier, ButtonColumn).Up = Met;
            for (int32 I = Tier; I <= ButtonTier - 1; ++I) { Grid.At(I, ButtonColumn).Down = Met; Grid.At(I + 1, ButtonColumn).Up = Met; }
            for (int32 I = Left; I <= Right - 1; ++I) { Grid.At(Tier, I).Right = Met; Grid.At(Tier, I + 1).Left = Met; }
            Grid.At(ButtonTier, ButtonColumn).TopArrow = Met;
            return;
        }
        if (Left == ButtonColumn) ++Left; else --Right;
        for (int32 I = Left; I <= Right; ++I) if (Grid.At(ButtonTier, I).bHasTalent) return;
        for (int32 I = Tier; I <= ButtonTier - 1; ++I) { Grid.At(I, Column).Up = Met; Grid.At(I + 1, Column).Down = Met; }
        if (ButtonColumn < Column) Grid.At(ButtonTier, ButtonColumn).RightArrow = Met;
        else Grid.At(ButtonTier, ButtonColumn).LeftArrow = Met;
    }

    // UIPanelScrollBarTemplate thumb: a fixed 18x24 knob that can be dragged along the track.
    class STalentScrollTrack : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(STalentScrollTrack) {}
            SLATE_ARGUMENT(TSharedPtr<SScrollBox>, ScrollBox)
            SLATE_ARGUMENT(const FSlateBrush*, Thumb)
            SLATE_ARGUMENT(float, ThumbHeight)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            ScrollBox = Args._ScrollBox; Thumb = Args._Thumb; ThumbHeight = Args._ThumbHeight;
        }
        virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16.f, 16.f); }
        virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
            int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
        {
            if (!Thumb) return Layer;
            const FVector2D Size = G.GetLocalSize();
            const float Y = Fraction() * FMath::Max(0.f, Size.Y - ThumbHeight);
            const float W = Size.X * 18.f / 16.f;
            FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(W, ThumbHeight),
                FSlateLayoutTransform(FVector2f((Size.X - W) * .5f, Y))), Thumb, ESlateDrawEffect::None, Style.GetColorAndOpacityTint());
            return Layer;
        }
        virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& Event) override
        {
            if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
            bDragging = true;
            SetFromPointer(G, Event);
            return FReply::Handled().CaptureMouse(SharedThis(this));
        }
        virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& Event) override
        {
            if (bDragging) { SetFromPointer(G, Event); return FReply::Handled(); }
            return FReply::Unhandled();
        }
        virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
        {
            bDragging = false;
            return FReply::Handled().ReleaseMouseCapture();
        }
    private:
        TSharedPtr<SScrollBox> ScrollBox;
        const FSlateBrush* Thumb = nullptr;
        float ThumbHeight = 24.f;
        bool bDragging = false;
        float Fraction() const
        {
            const float End = ScrollBox.IsValid() ? ScrollBox->GetScrollOffsetOfEnd() : 0.f;
            return End > 0.f ? FMath::Clamp(ScrollBox->GetScrollOffset() / End, 0.f, 1.f) : 0.f;
        }
        void SetFromPointer(const FGeometry& G, const FPointerEvent& Event)
        {
            if (!ScrollBox.IsValid()) return;
            const float Local = G.AbsoluteToLocal(Event.GetScreenSpacePosition()).Y - ThumbHeight * .5f;
            const float Track = FMath::Max(1.f, G.GetLocalSize().Y - ThumbHeight);
            ScrollBox->SetScrollOffset(FMath::Clamp(Local / Track, 0.f, 1.f) * ScrollBox->GetScrollOffsetOfEnd());
        }
    };
}

void SWowTalentWindow::Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
    UIManager = InArgs._UIManager;
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UIManager->GetFrameManager()->GetUIScale();
    ChildSlot
    [
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return TalentFrameW * Scale; })
        .HeightOverride_Lambda([this]() { return TalentFrameH * Scale; })
    ];
    SetVisibility(EVisibility::Collapsed);
    RefreshTalentData();
}

FVector2D SWowTalentWindow::GetPanelSize() const
{
    return FVector2D(TalentFrameW, TalentFrameH) * Scale;
}

UWowConnectionManager* SWowTalentWindow::Connection() const { return ConnectionManager.Get(); }

int32 SWowTalentWindow::ShownGroup() const
{
    const int32 Groups = FWowTalentModel::NumGroups(Connection());
    return ViewGroup >= 0 && ViewGroup < Groups ? ViewGroup : FWowTalentModel::ActiveGroup(Connection());
}

bool SWowTalentWindow::IsViewingActive() const
{
    return ShownGroup() == FWowTalentModel::ActiveGroup(Connection());
}

TArray<const FTalentTabDbcEntry*> SWowTalentWindow::PlayerTabs() const
{
    return FWowTalentModel::Tabs(FWowTalentModel::PlayerClass(Connection()));
}

bool SWowTalentWindow::ShowGlyphTab() const
{
    // SHOW_INSCRIPTION_LEVEL = 15
    UWowConnectionManager* C = Connection();
    const FWowPlayerEntity* Player = C ? C->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    return Player && Player->GetLevel() >= 15;
}

uint64 SWowTalentWindow::StateStamp() const
{
    UWowConnectionManager* C = Connection();
    const FWowPlayerEntity* Player = C ? C->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    uint64 Stamp = FWowTalentModel::Revision();
    Stamp = Stamp * 1000003ull + (C ? C->PacketHandler.TalentRevision : 0);
    Stamp = Stamp * 131ull + (Player ? Player->GetClassId() : 0);
    Stamp = Stamp * 131ull + (Player ? Player->GetLevel() : 0);
    Stamp = Stamp * 31ull + SelectedTab;
    Stamp = Stamp * 31ull + ShownGroup();
    Stamp = Stamp * 31ull + FWowTalentModel::NumGroups(C);
    Stamp = Stamp * 31ull + (bConfirmLearn ? 1 : 0);
    return Stamp;
}

void SWowTalentWindow::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale)) { Scale = NewScale; BuiltStamp = 0; }
    }
    if (StateStamp() != BuiltStamp) RefreshTalentData();
}

void SWowTalentWindow::ToggleVisibility()
{
    if (IsVisible())
    {
        SetVisibility(EVisibility::Collapsed);
        bConfirmLearn = false;
        return;
    }
    ViewGroup = -1;
    SetVisibility(EVisibility::Visible);
    RefreshTalentData();
}

bool SWowTalentWindow::IsVisible() const
{
    return GetVisibility().IsVisible();
}

void SWowTalentWindow::RefreshTalentData()
{
    if (TreeScroll.IsValid()) SavedScroll = TreeScroll->GetScrollOffset();
    BuiltStamp = StateStamp();
    if (Root.IsValid())
    {
        Root->SetContent(BuildFrame());
        // Measure the new widgets now: rebuilt from Tick, the first paint otherwise used zero
        // desired sizes and tab/tree labels flashed outside their boxes for a frame.
        Root->SlatePrepass(GetCachedGeometry().Scale > 0.f ? GetCachedGeometry().Scale : 1.f);
    }
    if (TreeScroll.IsValid()) TreeScroll->SetScrollOffset(SavedScroll);
}

const FSlateBrush* SWowTalentWindow::Brush(const FString& Path, float U0, float U1, float V0, float V1) const
{
    if (Path.IsEmpty()) return nullptr;
    const FString Key = FString::Printf(TEXT("%s|%.5f,%.5f,%.5f,%.5f"), *Path, U0, U1, V0, V1);
    if (const TSharedPtr<FWowRetainedSlateBrush>* Found = Brushes.Find(Key)) return Found->Get();
    const double Now = FPlatformTime::Seconds();
    if (const double* Failed = FailedBrushes.Find(Key); Failed && Now - *Failed < 5.0) return nullptr;
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager()
        ? UIManager->GetFrameManager()->LoadUITexture(Path) : nullptr;
    if (!Texture) { FailedBrushes.Add(Key, Now); return nullptr; }
    TSharedPtr<FWowRetainedSlateBrush> New = MakeShared<FWowRetainedSlateBrush>();
    New->SetResourceObject(Texture);
    New->DrawAs = ESlateBrushDrawType::Image;
    // TexCoords may be mirrored (left > right); the UV region is used as given.
    FBox2f Region(FVector2f(U0, V0), FVector2f(U1, V1));
    New->SetUVRegion(Region);
    New->ImageSize = FVector2D(Texture->GetSizeX() * FMath::Abs(U1 - U0), Texture->GetSizeY() * FMath::Abs(V1 - V0));
    Brushes.Add(Key, New);
    return New.Get();
}

const FButtonStyle* SWowTalentWindow::TextureButtonStyle(const FString& Up, const FString& Down, const FString& Highlight,
    float U0, float U1, float V0, float V1) const
{
    const FString Key = Up + TEXT("|") + Down + FString::Printf(TEXT("|%.3f%.3f%.3f%.3f"), U0, U1, V0, V1);
    if (const TSharedPtr<FButtonStyle>* Found = ButtonStyles.Find(Key)) return Found->Get();
    const FSlateBrush* Normal = Brush(Up, U0, U1, V0, V1);
    if (!Normal) return nullptr;
    const FSlateBrush* Pressed = Brush(Down, U0, U1, V0, V1);
    TSharedPtr<FButtonStyle> Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
    Style->SetNormal(*Normal);
    FSlateBrush Hovered = *Normal;
    Hovered.TintColor = WowColor::FromWow(1.25f, 1.25f, 1.25f);
    Style->SetHovered(Hovered);
    Style->SetPressed(Pressed ? *Pressed : *Normal);
    Style->SetNormalPadding(FMargin(0));
    Style->SetPressedPadding(FMargin(0));
    ButtonStyles.Add(Key, Style);
    return Style.Get();
}

FSlateFontInfo SWowTalentWindow::Font(float WowPixels) const
{
    // WoW font heights are pixels; Slate sizes are points (x 0.75).
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
}

float SWowTalentWindow::TextWidth(const FString& Text, const FSlateFontInfo& FontInfo) const
{
    if (!FSlateApplication::IsInitialized()) return Text.Len() * FontInfo.Size * .6f;
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, FontInfo).X;
}

void SWowTalentWindow::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y) * Scale).Size(FVector2D(W, H) * Scale)[Widget];
}

void SWowTalentWindow::PlaceImage(SCanvas& Canvas, float X, float Y, float W, float H, const FSlateBrush* Image,
    const FLinearColor& Tint, bool bDesaturate) const
{
    if (!Image) return;
    // Slate's disabled draw effect greys the image like SetDesaturated(1).
    Place(Canvas, X, Y, W, H, SNew(SImage).Image(Image).ColorAndOpacity(Tint)
        .IsEnabled(!bDesaturate).Visibility(EVisibility::HitTestInvisible));
}

void SWowTalentWindow::PlaceText(SCanvas& Canvas, float X, float Y, float W, float H, const FString& Text,
    const FLinearColor& Color, float WowPixels, EHorizontalAlignment HAlign) const
{
    Place(Canvas, X, Y, W, H, SNew(SBox).HAlign(HAlign).VAlign(VAlign_Center).Visibility(EVisibility::HitTestInvisible)
        [ SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(WowPixels)).ColorAndOpacity(Color)
            .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 1)) ]);
}

TSharedRef<SToolTip> SWowTalentWindow::MakeTooltip(const TArray<FWowTalentTooltipLine>& Lines) const
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    for (int32 Index = 0; Index < Lines.Num(); ++Index)
        Box->AddSlot().AutoHeight().Padding(0, Index == 0 ? 0 : 2, 0, 0)
        [ SNew(STextBlock).Text(FText::FromString(Lines[Index].Text)).ColorAndOpacity(Lines[Index].Color)
            .Font(FWowWindowStyle::TooltipFont(Index == 0)).AutoWrapText(Lines[Index].bWrap)
            .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 1)) ];
    static const FSlateNoResource NoBorder;
    return SNew(SToolTip).BorderImage(&NoBorder).TextMargin(FMargin(0))
        [ SNew(SWowTooltipFrame)[ SNew(SBox).MaxDesiredWidth(300)[ Box ] ] ];
}

void SWowTalentWindow::ScrollBy(float Delta)
{
    if (TreeScroll.IsValid())
        TreeScroll->SetScrollOffset(FMath::Clamp(TreeScroll->GetScrollOffset() + Delta, 0.f, TreeScroll->GetScrollOffsetOfEnd()));
}

TSharedRef<SWidget> SWowTalentWindow::BuildFrame()
{
    TreeScroll.Reset();
    UWowConnectionManager* C = Connection();
    const TArray<const FTalentTabDbcEntry*> Tabs = PlayerTabs();
    if (SelectedTab != 3 && !Tabs.IsValidIndex(SelectedTab)) SelectedTab = 0;
    if (SelectedTab == 3 && !ShowGlyphTab()) SelectedTab = 0;
    const bool bGlyphs = SelectedTab == 3;
    const FTalentTabDbcEntry* Tab = !bGlyphs && Tabs.IsValidIndex(SelectedTab) ? Tabs[SelectedTab] : nullptr;
    const int32 Groups = FWowTalentModel::NumGroups(C);
    const int32 Group = ShownGroup();

    TSharedRef<SCanvas> Canvas = SNew(SCanvas);

    // Portrait (BACKGROUND, under the frame art): the class crest stands in for the 3D portrait.
    {
        static const float Crest[12][4] = {{0,0,0,0},{0,.25f,0,.25f},{0,.25f,.5f,.75f},{0,.25f,.25f,.5f},{.49609375f,.7421875f,0,.25f},
            {.49609375f,.7421875f,.25f,.5f},{.25f,.5f,.5f,.75f},{.25f,.49609375f,.25f,.5f},{.25f,.49609375f,0,.25f},
            {.7421875f,.98828125f,.25f,.5f},{0,0,0,0},{.7421875f,.98828125f,0,.25f}};
        const uint8 ClassId = FWowTalentModel::PlayerClass(C);
        if (ClassId > 0 && ClassId < 12 && Crest[ClassId][1] > 0.f)
            PlaceImage(*Canvas, 7, 6, 60, 60, Brush(TEXT("Interface\\TargetingFrame\\UI-Classes-Circles"),
                Crest[ClassId][0], Crest[ClassId][1], Crest[ClassId][2], Crest[ClassId][3]));
    }

    // Frame art: UI-Character-General quadrants. Its centre is the dark stone panel, so the
    // tree, the talent buttons (children of the scroll frame) and the points bar go on top.
    PlaceImage(*Canvas, 2, 1, 256, 256, Brush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopLeft")));
    PlaceImage(*Canvas, 258, 1, 128, 256, Brush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopRight")));
    PlaceImage(*Canvas, 2, 257, 256, 256, Brush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-BottomLeft")));
    PlaceImage(*Canvas, 258, 257, 128, 256, Brush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-BottomRight")));
    // Tree background, scroll frame and talents are drawn over the frame art.
    float PointsBarTop = 405.f;
    if (!bGlyphs)
    {
        TSharedRef<SCanvas> Bars = SNew(SCanvas);
        BuildBars(*Bars, Tab, PointsBarTop);
        const float ScrollH = PointsBarTop - TalentScrollY;
        if (Tab) BuildTree(*Canvas, *Tab, ScrollH);
        Place(*Canvas, 0, 0, TalentFrameW, TalentFrameH, Bars);
    }


    if (bGlyphs)
    {
        BuildGlyphs(*Canvas);
    }
    else
    {
        // Scroll bar art beside the tree (ScrollFrameBackgroundTop/Bottom).
        const float ScrollH = PointsBarTop - TalentScrollY;
        PlaceImage(*Canvas, TalentScrollX + 294, TalentScrollY - 5, 31, 256,
            Brush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar"), 0.f, .484375f, 0.f, 1.f));
        PlaceImage(*Canvas, TalentScrollX + 294, TalentScrollY + ScrollH + 2 - 106, 31, 106,
            Brush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar"), .515625f, 1.f, 0.f, .4140625f));
        // UIPanelScrollBarTemplate: 16 wide, 16 in from both ends, up/down buttons outside.
        const float BarX = TalentScrollX + TalentScrollW + 6;
        const float StepAmount = ScrollH * .5f * Scale;
        if (const FButtonStyle* UpStyle = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Up"),
            TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Down"), FString(), .2f, .8f, .25f, .75f))
            Place(*Canvas, BarX - 1, TalentScrollY, 18, 16, SNew(SButton).ButtonStyle(UpStyle).ClickMethod(EButtonClickMethod::MouseDown)
                .OnClicked_Lambda([this, StepAmount]() { ScrollBy(-StepAmount); return FReply::Handled(); }));
        if (const FButtonStyle* DownStyle = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Up"),
            TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Down"), FString(), .2f, .8f, .25f, .75f))
            Place(*Canvas, BarX - 1, TalentScrollY + ScrollH - 16, 18, 16, SNew(SButton).ButtonStyle(DownStyle).ClickMethod(EButtonClickMethod::MouseDown)
                .OnClicked_Lambda([this, StepAmount]() { ScrollBy(StepAmount); return FReply::Handled(); }));
        if (TreeScroll.IsValid())
            Place(*Canvas, BarX, TalentScrollY + 16, 16, FMath::Max(1.f, ScrollH - 32),
                SNew(STalentScrollTrack).ScrollBox(TreeScroll).ThumbHeight(24.f * Scale)
                .Thumb(Brush(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"), .2f, .8f, .125f, .875f)));

        // Title
        const FString Title = Groups > 1 ? (Group == 0 ? TEXT("Primary Talents") : TEXT("Secondary Talents")) : TEXT("Talents");
        // On the title bar like the other windows (Stefan 10.10: it sat on the bar's top edge).
        Place(*Canvas, 70, 16, 250, 16, WowTitleBox(SNew(STextBlock).Text(FText::FromString(Title)).Font(Font(12)).ColorAndOpacity(TalentNormalColor).Justification(ETextJustify::Center)
            .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 1)), Font(12)));

        // Dual spec: status line on the active spec, Activate button on the other one.
        if (Groups > 1)
        {
            if (IsViewingActive())
            {
                PlaceImage(*Canvas, 75, 46, 8, 20, Brush(TEXT("Interface\\Common\\Common-Input-Border"), 0.f, .0625f, 0.f, .625f));
                PlaceImage(*Canvas, 83, 46, 248, 20, Brush(TEXT("Interface\\Common\\Common-Input-Border"), .0625f, .9375f, 0.f, .625f));
                PlaceImage(*Canvas, 331, 46, 8, 20, Brush(TEXT("Interface\\Common\\Common-Input-Border"), .9375f, 1.f, 0.f, .625f));
                PlaceText(*Canvas, 75, 46, 264, 20, TEXT("These are your active talents"), TalentNormalColor, 12);
            }
            else
            {
                const FString Label = TEXT("Activate These Talents");
                const float W = TextWidth(Label, Font(12)) / Scale + 40.f;
                Place(*Canvas, 202 - W * .5f, 45, W, 22, SNew(SButton).ButtonStyle(FWowWindowStyle::Button())
                    .ClickMethod(EButtonClickMethod::MouseDown).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    .OnClicked_Lambda([this]()
                    {
                        // SetActiveTalentGroup: the "Activate Primary/Secondary Spec" spells.
                        if (UWowConnectionManager* Conn = Connection())
                            Conn->SendCastSpell(ShownGroup() == 0 ? 63645 : 63644, 0);
                        return FReply::Handled();
                    })
                    [ SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(12)).ColorAndOpacity(TalentNormalColor) ]);
            }
        }
    }

    BuildTabs(*Canvas);
    BuildSpecTabs(*Canvas);

    // UIPanelCloseButton, centred on TOPRIGHT (-44, -25).
    if (const FButtonStyle* CloseStyle = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"),
        TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down"), FString()))
        Place(*Canvas, 324, 9, 32, 32, SNew(SButton).ButtonStyle(CloseStyle).ClickMethod(EButtonClickMethod::MouseDown)
            .OnClicked_Lambda([this]() { SetVisibility(EVisibility::Collapsed); bConfirmLearn = false; return FReply::Handled(); }));

    // CONFIRM_LEARN_PREVIEW_TALENTS static popup.
    if (bConfirmLearn)
    {
        Place(*Canvas, 52, 180, 280, 100, SNew(SWowWindowFrame).Padding(FMargin(14))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Are you sure you want to learn these talents?")))
                    .Font(Font(12)).ColorAndOpacity(TalentHighlightColor).AutoWrapText(true).Justification(ETextJustify::Center) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                    [ SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).ClickMethod(EButtonClickMethod::MouseDown)
                        .OnClicked_Lambda([this]() { FWowTalentModel::LearnPreview(Connection()); bConfirmLearn = false; return FReply::Handled(); })
                        [ SNew(STextBlock).Text(FText::FromString(TEXT("Yes"))).Font(Font(12)).ColorAndOpacity(TalentNormalColor) ] ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                    [ SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).ClickMethod(EButtonClickMethod::MouseDown)
                        .OnClicked_Lambda([this]() { bConfirmLearn = false; return FReply::Handled(); })
                        [ SNew(STextBlock).Text(FText::FromString(TEXT("No"))).Font(Font(12)).ColorAndOpacity(TalentNormalColor) ] ]
                ]
            ]);
    }

    // The frame swallows clicks so they do not reach the world behind it.
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
        .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
        [ Canvas ];
}

void SWowTalentWindow::BuildBars(SCanvas& Canvas, const FTalentTabDbcEntry* Tab, float& OutPointsBarTop)
{
    UWowConnectionManager* C = Connection();
    const int32 Group = ShownGroup();
    const bool bActive = IsViewingActive();
    const int32 Unspent = bActive ? FWowTalentModel::Unspent(C) : 0;
    const int32 Preview = bActive ? FWowTalentModel::PreviewSpent() : 0;
    const bool bPreviewBar = bActive && Unspent > 0; // previewTalents is on
    const FString Borders = TEXT("Interface\\Buttons\\UI-Button-Borders2");

    if (bPreviewBar)
    {
        // PlayerTalentFramePreviewBar: LEFT 20, RIGHT -36, BOTTOM 81 (26 high).
        const float X = 20.f, Y = 405.f, W = TalentFrameW - 20.f - 36.f;
        const float ButtonsX = X + W - 164.f;
        PlaceImage(Canvas, X, Y, 8, 26, Brush(Borders, .470703125f, .484375f, 0.f, .203125f));
        PlaceImage(Canvas, X + 8, Y, ButtonsX - X - 16, 26, Brush(Borders, .484375f, .78515625f, 0.f, .203125f));
        PlaceImage(Canvas, ButtonsX - 8, Y, 8, 26, Brush(Borders, .78515625f, .80078125f, 0.f, .203125f));
        PlaceImage(Canvas, ButtonsX, Y, 164, 26, Brush(Borders, .16015625f, .470703125f, 0.f, .203125f));
        auto Button = [this](const TCHAR* Label, bool bEnabled, TFunction<void()> OnClick)
        {
            return SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).IsEnabled(bEnabled)
                .ClickMethod(EButtonClickMethod::MouseDown).HAlign(HAlign_Center).VAlign(VAlign_Center)
                .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
                [ SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(12))
                    .RenderTransform(WowCapCenterTransform(Font(12)))
                    .ColorAndOpacity(bEnabled ? TalentNormalColor : TalentGreyColor) ];
        };
        const float ResetX = X + W - 4.f - 80.f;
        Place(Canvas, ResetX, Y + 2, 80, 22, Button(TEXT("Reset"), Preview > 0, [this]() { FWowTalentModel::ClearPreview(); }));
        Place(Canvas, ResetX - 80.f, Y + 2, 80, 22, Button(TEXT("Learn"), Preview > 0, [this]() { bConfirmLearn = true; }));
    }

    // PlayerTalentFramePointsBar: LEFT 16, RIGHT -36; BOTTOM 81, or 4 into the top of the preview bar.
    const float X = 16.f, W = TalentFrameW - 16.f - 36.f, H = 26.f;
    const float Y = bPreviewBar ? 405.f + 4.f - H : 405.f;
    OutPointsBarTop = Y;
    PlaceImage(Canvas, X, Y, W, H, Brush(Borders, 0.f, .646484375f, .2109375f, .4140625f));
    PlaceImage(Canvas, X + 4, Y + 5, 6, H - 10, Brush(Borders, 0.f, .01171875f, .421875f, .5625f));
    PlaceImage(Canvas, X + W - 10, Y + 5, 6, H - 10, Brush(Borders, .3046875f, .31640625f, .421875f, .5625f));
    PlaceImage(Canvas, X + 10, Y + 5, W - 20, H - 10, Brush(Borders, .01171875f, .3046875f, .421875f, .5625f));

    // 9 px so the line fits inside the bar's inner strip (10 touched its borders).
    auto Pair = [this](const FString& Label, const FString& Value)
    {
        return SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(9)).ColorAndOpacity(TalentNormalColor).ShadowOffset(FVector2D(1, 1)) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(STextBlock).Text(FText::FromString(Value)).Font(Font(9)).ColorAndOpacity(TalentHighlightColor).ShadowOffset(FVector2D(1, 1)) ];
    };
    if (Tab)
    {
        // MASTERY_POINTS_SPENT "%s Talents: %s"
        const int32 Spent = FWowTalentModel::PointsInTab(C, Group, Tab->TalentTabID, bActive);
        Place(Canvas, X + 12, Y, W * .6f, H - 2, SNew(SBox).VAlign(VAlign_Center).HAlign(HAlign_Left)
            [ Pair(Tab->Name + TEXT(" Talents: "), FString::FromInt(Spent)) ]);
    }
    // UNSPENT_TALENT_POINTS "Unspent Talents: %s"
    Place(Canvas, X + W * .4f, Y, W * .6f - 12, H - 2, SNew(SBox).VAlign(VAlign_Center).HAlign(HAlign_Right)
        [ Pair(TEXT("Unspent Talents: "), FString::FromInt(FMath::Max(0, Unspent - Preview))) ]);
}

void SWowTalentWindow::BuildTree(SCanvas& Canvas, const FTalentTabDbcEntry& Tab, float ScrollH)
{
    UWowConnectionManager* C = Connection();
    const int32 Group = ShownGroup();
    const bool bActive = IsViewingActive();
    const bool bPreview = bActive; // previewTalents
    const TArray<const FTalentDbcEntry*> Talents = FWowTalentModel::TalentsInTab(Tab.TalentTabID);
    const FTalentDbc& Dbc = FDbcStore::Get().Talents();

    // Background quads (BackgroundTopLeft 256 wide, 40 px right column, 76 px bottom row);
    // greyed when this is not the active spec (SetDesaturation).
    {
        const FString Base = FWowTalentModel::BackgroundPath(Tab) + TEXT("-");
        const float TopH = FMath::Max(0.f, ScrollH - 76.f);
        const bool bDesat = !bActive;
        static const FSlateColorBrush Fallback(WowColor::FromWow(.03f, .03f, .035f, 1.f));
        PlaceImage(Canvas, TalentScrollX, TalentScrollY, TalentScrollW, ScrollH, &Fallback);
        PlaceImage(Canvas, TalentScrollX, TalentScrollY, 256, TopH, Brush(Base + TEXT("TopLeft"), 0.f, 1.f, 0.f, 1.f), FLinearColor::White, bDesat);
        PlaceImage(Canvas, TalentScrollX + 256, TalentScrollY, 40, TopH, Brush(Base + TEXT("TopRight"), 0.f, .6875f, 0.f, 1.f), FLinearColor::White, bDesat);
        PlaceImage(Canvas, TalentScrollX, TalentScrollY + TopH, 256, 76, Brush(Base + TEXT("BottomLeft"), 0.f, 1.f, 0.f, .5859375f), FLinearColor::White, bDesat);
        PlaceImage(Canvas, TalentScrollX + 256, TalentScrollY + TopH, 40, 76, Brush(Base + TEXT("BottomRight"), 0.f, .6875f, 0.f, .5859375f), FLinearColor::White, bDesat);
    }

    // TalentFrame_Update
    const int32 Unspent = bActive ? FWowTalentModel::Unspent(C) - FWowTalentModel::PreviewSpent() : 0;
    const int32 TabPointsSpent = FWowTalentModel::PointsInTab(C, Group, Tab.TalentTabID, bPreview);
    FTalentGrid Grid;
    int32 LastTier = 1;
    for (const FTalentDbcEntry* Talent : Talents)
    {
        Grid.At(Talent->Row + 1, Talent->Col + 1).bHasTalent = true;
        LastTier = FMath::Max(LastTier, static_cast<int32>(Talent->Row) + 1);
    }

    struct FButtonState { const FTalentDbcEntry* Talent; int32 DisplayRank, Rank, MaxRank; bool bNormal; FLinearColor SlotColor, RankColor; bool bShowRank; };
    TArray<FButtonState> Buttons;
    for (const FTalentDbcEntry* Talent : Talents)
    {
        const int32 Tier = Talent->Row + 1, Column = Talent->Col + 1;
        const int32 Rank = FWowTalentModel::Rank(C, Group, Talent->TalentID, false);
        const int32 DisplayRank = FWowTalentModel::Rank(C, Group, Talent->TalentID, bPreview);
        const int32 MaxRank = FWowTalentModel::MaxRank(*Talent);
        const bool bForceDesaturated = (Unspent <= 0 || !bActive) && DisplayRank == 0;
        const bool bTierUnlocked = (Tier - 1) * FWowTalentModel::PointsPerTier <= TabPointsSpent;
        bool bRequirementsMet = bTierUnlocked && !bForceDesaturated;
        const bool bMeetsPrereq = FWowTalentModel::PrereqMet(C, Group, *Talent, bPreview);
        if (Talent->DependsOn)
            if (const FTalentDbcEntry* Prereq = Dbc.GetById(Talent->DependsOn); Prereq && Prereq->TalentTab == Talent->TalentTab)
            {
                if (bForceDesaturated || !bMeetsPrereq) bRequirementsMet = false;
                TalentDrawLines(Grid, Tier, Column, Prereq->Row + 1, Prereq->Col + 1, bRequirementsMet ? 1 : -1);
            }
        FButtonState State{Talent, DisplayRank, Rank, MaxRank, false, WowColor::FromWow(.5f, .5f, .5f), TalentGreyColor, Rank > 0};
        if (bRequirementsMet && bMeetsPrereq)
        {
            State.bNormal = true;
            State.bShowRank = true;
            const bool bMaxed = DisplayRank >= MaxRank;
            State.SlotColor = bMaxed ? WowColor::FromWow(1.f, .82f, 0.f) : WowColor::FromWow(.1f, 1.f, .1f);
            State.RankColor = bMaxed ? TalentNormalColor : TalentGreenColor;
        }
        Buttons.Add(State);
    }

    // Scroll child: 320 wide, tall enough for the last tier.
    TSharedRef<SCanvas> Child = SNew(SCanvas);
    const float ContentH = FMath::Max(ScrollH, TalentInitialY + (LastTier - 1) * TalentStep + TalentButtonSize + TalentInitialY);
    const FString Branches = TEXT("Interface\\TalentFrame\\UI-TalentBranches");
    const FString Arrows = TEXT("Interface\\TalentFrame\\UI-TalentArrows");
    auto Piece = [&](const FString& Texture, const FTalentUV& UV, float X, float WowY)
    {
        // Lua offsets are WoW coordinates (y up); the child canvas grows downwards.
        PlaceImage(*Child, X, -WowY, TalentBranchSize, TalentBranchSize, Brush(Texture, UV.L, UV.R, UV.T, UV.B));
    };
    TArray<TTuple<FTalentUV, float, float>> ArrowPieces;
    bool bIgnoreUp = false;
    for (int32 I = 1; I <= TalentTiers; ++I)
        for (int32 J = 1; J <= TalentColumns; ++J)
        {
            FTalentNode& Node = Grid.At(I, J);
            const float X = (J - 1) * TalentStep + TalentInitialX + 2.f;
            const float Y = -((I - 1) * TalentStep) - TalentInitialY - 2.f;
            if (Node.bHasTalent)
            {
                if (Node.Up != 0) { if (!bIgnoreUp) Piece(Branches, TalentBranchUV(TEXT("up"), Node.Up), X, Y + TalentBranchSize); else bIgnoreUp = false; }
                if (Node.Down != 0) Piece(Branches, TalentBranchUV(TEXT("down"), Node.Down), X, Y - TalentBranchSize + 1.f);
                if (Node.Left != 0) Piece(Branches, TalentBranchUV(TEXT("left"), Node.Left), X - TalentBranchSize, Y);
                if (Node.Right != 0)
                {
                    const FTalentNode& Next = Grid.At(I, J + 1);
                    if (Next.Left != 0 && Next.Down < 0) Piece(Branches, TalentBranchUV(TEXT("right"), Next.Down), X + TalentBranchSize, Y);
                    else Piece(Branches, TalentBranchUV(TEXT("right"), Node.Right), X + TalentBranchSize + 1.f, Y);
                }
                if (Node.RightArrow != 0) ArrowPieces.Add(MakeTuple(TalentArrowUV(TEXT("right"), Node.RightArrow), X + TalentBranchSize * .5f + 5.f, Y));
                if (Node.LeftArrow != 0) ArrowPieces.Add(MakeTuple(TalentArrowUV(TEXT("left"), Node.LeftArrow), X - TalentBranchSize * .5f - 5.f, Y));
                if (Node.TopArrow != 0) ArrowPieces.Add(MakeTuple(TalentArrowUV(TEXT("top"), Node.TopArrow), X, Y + TalentBranchSize * .5f + 5.f));
            }
            else if (Node.Up != 0 && Node.Left != 0 && Node.Right != 0) Piece(Branches, TalentBranchUV(TEXT("tup"), Node.Up), X, Y);
            else if (Node.Down != 0 && Node.Left != 0 && Node.Right != 0) Piece(Branches, TalentBranchUV(TEXT("tdown"), Node.Down), X, Y);
            else if (Node.Left != 0 && Node.Down != 0)
            {
                Piece(Branches, TalentBranchUV(TEXT("topright"), Node.Left), X, Y);
                Piece(Branches, TalentBranchUV(TEXT("down"), Node.Down), X, Y - 32.f);
            }
            else if (Node.Left != 0 && Node.Up != 0) Piece(Branches, TalentBranchUV(TEXT("bottomright"), Node.Left), X, Y);
            else if (Node.Left != 0 && Node.Right != 0)
            {
                Piece(Branches, TalentBranchUV(TEXT("right"), Node.Right), X + TalentBranchSize, Y);
                Piece(Branches, TalentBranchUV(TEXT("left"), Node.Left), X + 1.f, Y);
            }
            else if (Node.Right != 0 && Node.Down != 0)
            {
                Piece(Branches, TalentBranchUV(TEXT("topleft"), Node.Right), X, Y);
                Piece(Branches, TalentBranchUV(TEXT("down"), Node.Down), X, Y - 32.f);
            }
            else if (Node.Right != 0 && Node.Up != 0) Piece(Branches, TalentBranchUV(TEXT("bottomleft"), Node.Right), X, Y);
            else if (Node.Up != 0 && Node.Down != 0)
            {
                Piece(Branches, TalentBranchUV(TEXT("up"), Node.Up), X, Y);
                Piece(Branches, TalentBranchUV(TEXT("down"), Node.Down), X, Y - 32.f);
                bIgnoreUp = true;
            }
        }

    // Talent buttons (TalentButtonTemplate over ItemButtonTemplate).
    for (const FButtonState& State : Buttons)
    {
        const float X = TalentInitialX + State.Talent->Col * TalentStep;
        const float Y = TalentInitialY + State.Talent->Row * TalentStep;
        const float Center = TalentButtonSize * .5f;
        PlaceImage(*Child, X + Center - 32, Y + Center - 32, 64, 64, Brush(TEXT("Interface\\Buttons\\UI-EmptySlot-White")), State.SlotColor);
        const FSlateBrush* Icon = Brush(FWowTalentModel::IconPath(*State.Talent));
        PlaceImage(*Child, X, Y, TalentButtonSize, TalentButtonSize, Icon,
            State.bNormal ? FLinearColor::White : WowColor::FromWow(.65f, .65f, .65f), !State.bNormal);
        PlaceImage(*Child, X + Center - 32, Y + Center - 32 + 1, 64, 64, Brush(TEXT("Interface\\Buttons\\UI-Quickslot2")));
        if (State.bShowRank)
        {
            PlaceImage(*Child, X + TalentButtonSize - 16, Y + TalentButtonSize - 16, 32, 32,
                Brush(TEXT("Interface\\TalentFrame\\TalentFrame-RankBorder")), State.bNormal ? FLinearColor::White : WowColor::FromWow(.5f, .5f, .5f));
            PlaceText(*Child, X + TalentButtonSize - 16, Y + TalentButtonSize - 16, 32, 32, FString::FromInt(State.DisplayRank), State.RankColor, 10);
        }
        // Click/hover area with the GameTooltip:SetTalent tooltip and ButtonHilight-Square.
        TSharedRef<TWeakPtr<SBorder>> Holder = MakeShared<TWeakPtr<SBorder>>();
        TSharedPtr<SBorder> Hit;
        Place(*Child, X, Y, TalentButtonSize, TalentButtonSize,
            SAssignNew(Hit, SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .ToolTip(MakeTooltip(FWowTalentModel::TooltipLines(C, Group, Tab, *State.Talent, bPreview)))
            .OnMouseButtonDown_Lambda([this, Id = State.Talent->TalentID](const FGeometry&, const FPointerEvent& Event) { return OnTalentMouse(Id, Event); })
            [
                SNew(SWowAddImage).Image(Brush(TEXT("Interface\\Buttons\\ButtonHilight-Square"))).ColorAndOpacity(FLinearColor(1, 1, 1, .5f))
                .Visibility_Lambda([Holder]()
                {
                    const TSharedPtr<SBorder> Border = Holder->Pin();
                    return Border.IsValid() && Border->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
                })
            ]);
        *Holder = Hit;
    }
    for (const auto& Arrow : ArrowPieces)
        Piece(Arrows, Arrow.Get<0>(), Arrow.Get<1>(), Arrow.Get<2>());

    SAssignNew(TreeScroll, SScrollBox)
        .ScrollBarVisibility(EVisibility::Collapsed)
        .ConsumeMouseWheel(EConsumeMouseWheel::Always)
        // ScrollFrameTemplate_OnMouseWheel: half the frame height per notch (Slate's base step is 32).
        .WheelScrollMultiplier(FMath::Max(.1f, ScrollH * .5f * Scale / 32.f))
        + SScrollBox::Slot()[ SNew(SBox).WidthOverride(320.f * Scale).HeightOverride(ContentH * Scale)[ Child ] ];
    Place(Canvas, TalentScrollX, TalentScrollY, TalentScrollW, ScrollH,
        SNew(SBox).Clipping(EWidgetClipping::ClipToBounds)[ TreeScroll.ToSharedRef() ]);
}

void SWowTalentWindow::BuildGlyphs(SCanvas& Canvas)
{
    UWowConnectionManager* C = Connection();
    const int32 Group = ShownGroup();
    const FString Frame = TEXT("Interface\\Spellbook\\UI-GlyphFrame");
    PlaceImage(Canvas, 0, 0, 352, 441, Brush(Frame, 0.f, .6875f, 0.f, .861328125f));
    const int32 Groups = FWowTalentModel::NumGroups(C);
    PlaceText(Canvas, 60, 12, 264, 14, Groups > 1 ? (Group == 0 ? TEXT("Primary Glyphs") : TEXT("Secondary Glyphs")) : TEXT("Glyphs"), TalentNormalColor, 12);

    // GlyphFrameGlyph1..6 centres (GlyphFrame is 384x512).
    static const FVector2D Centers[7] = { {0,0}, {177,116}, {178,359}, {73,178}, {283,299}, {283,178}, {71,299} };
    // GLYPH_SLOTS background texcoords per socket ([0] = empty).
    static const float Slots[7][4] = { {.78125f,.91015625f,.69921875f,.828125f}, {0,.12890625f,.87109375f,1}, {.130859375f,.259765625f,.87109375f,1},
        {.392578125f,.521484375f,.87109375f,1}, {.5234375f,.65234375f,.87109375f,1}, {.26171875f,.390625f,.87109375f,1}, {.654296875f,.783203125f,.87109375f,1} };
    for (int32 Socket = 1; Socket <= 6; ++Socket)
    {
        const FWowGlyphSocket Info = FWowTalentModel::GlyphSocket(C, Socket, Group);
        const bool bMajor = Info.Type == 1;
        const FVector2D P = Centers[Socket];
        const float SettingSize = bMajor ? 108.f : 86.f;
        if (!Info.bEnabled)
        {
            PlaceImage(Canvas, P.X - SettingSize * .5f, P.Y - SettingSize * .5f, SettingSize, SettingSize,
                Brush(TEXT("Interface\\Spellbook\\UI-GlyphFrame-Locked"), .1f, .9f, .1f, .9f));
        }
        else
        {
            const bool bHasGlyph = Info.SpellId != 0;
            if (bMajor) PlaceImage(Canvas, P.X - 54, P.Y - 54, 108, 108, Brush(Frame, .740234375f, .953125f, .484375f, .697265625f), FLinearColor(1, 1, 1, bHasGlyph ? .9f : .6f));
            else PlaceImage(Canvas, P.X - 43, P.Y - 43, 86, 86, Brush(Frame, .765625f, .927734375f, .15625f, .31640625f), FLinearColor(1, 1, 1, bHasGlyph ? .9f : .6f));
            const float BackSize = bMajor ? 70.f : 64.f;
            const float* UV = Slots[bHasGlyph ? Socket : 0];
            PlaceImage(Canvas, P.X - BackSize * .5f, P.Y - BackSize * .5f, BackSize, BackSize, Brush(Frame, UV[0], UV[1], UV[2], UV[3]));
            if (bHasGlyph)
                PlaceImage(Canvas, P.X - 26.5f, P.Y - 26.5f, 53, 53,
                    Brush(Info.IconPath.IsEmpty() ? FString(TEXT("Interface\\Spellbook\\UI-Glyph-Rune1")) : Info.IconPath),
                    bMajor ? WowColor::FromWow(1.f, .25f, 0.f) : WowColor::FromWow(0.f, .25f, 1.f));
            if (bMajor) PlaceImage(Canvas, P.X - 41, P.Y - 41 + 1, 82, 82, Brush(Frame, .767578125f, .92578125f, .32421875f, .482421875f));
            else PlaceImage(Canvas, P.X - 31, P.Y - 31 - 1, 62, 62, Brush(Frame, .787109375f, .908203125f, .033203125f, .154296875f));
            PlaceImage(Canvas, P.X - 9 - 8, P.Y - 12 - 8, 16, 16, bMajor ? Brush(Frame, .9609375f, 1.f, .9609375f, 1.f) : Brush(Frame, .9609375f, 1.f, .921875f, .9609375f));
        }
        Place(Canvas, P.X - 36, P.Y - 36, 72, 72, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .ToolTip(MakeTooltip(FWowTalentModel::GlyphTooltipLines(C, Socket, Group))));
    }
}

void SWowTalentWindow::BuildTabs(SCanvas& Canvas)
{
    const TArray<const FTalentTabDbcEntry*> Tabs = PlayerTabs();
    TArray<TPair<int32, FString>> Entries;
    for (int32 Index = 0; Index < Tabs.Num() && Index < 3; ++Index) Entries.Add({Index, Tabs[Index]->Name});
    if (ShowGlyphTab()) Entries.Add({3, TEXT("Glyphs")});

    // CharacterFrameTabButtonTemplate: 20 px ends + text width, overlapping by 15; first at BOTTOMLEFT (15, 46).
    float X = 15.f;
    const float Y = TalentFrameH - 46.f - 32.f;
    const FSlateFontInfo TabFont = Font(10);
    for (const auto& Entry : Entries)
    {
        const int32 Index = Entry.Key;
        const bool bSelected = Index == SelectedTab;
        const float W = TextWidth(Entry.Value, TabFont) / Scale + 40.f;
        const FString Art = bSelected ? TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ActiveTab") : TEXT("Interface\\PaperDollInfoFrame\\UI-Character-InActiveTab");
        const float ArtY = bSelected ? Y - 5.f : Y;
        PlaceImage(Canvas, X, ArtY, 20, 32, Brush(Art, 0.f, .15625f, 0.f, 1.f));
        PlaceImage(Canvas, X + 20, ArtY, W - 40, 32, Brush(Art, .15625f, .84375f, 0.f, 1.f));
        PlaceImage(Canvas, X + W - 20, ArtY, 20, 32, Brush(Art, .84375f, 1.f, 0.f, 1.f));
        PlaceText(Canvas, X, Y + 8 - 2, W, 16, Entry.Value, bSelected ? TalentHighlightColor : TalentNormalColor, 10);
        TSharedRef<TWeakPtr<SBorder>> Holder = MakeShared<TWeakPtr<SBorder>>();
        TSharedPtr<SBorder> Hit;
        Place(Canvas, X, Y, W, 32, SAssignNew(Hit, SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(FMargin(10 * Scale, 0))
            .OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent& Event)
            {
                if (Event.GetEffectingButton() == EKeys::LeftMouseButton) { SelectedTab = Index; bConfirmLearn = false; }
                return FReply::Handled();
            })
            [
                SNew(SWowAddImage).Image(Brush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-Tab-Highlight"))).ColorAndOpacity(FLinearColor(1, 1, 1, .6f))
                .Visibility_Lambda([Holder, bSelected]()
                {
                    const TSharedPtr<SBorder> Border = Holder->Pin();
                    return !bSelected && Border.IsValid() && Border->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
                })
            ]);
        *Holder = Hit;
        X += W - 15.f;
    }
}

void SWowTalentWindow::BuildSpecTabs(SCanvas& Canvas)
{
    UWowConnectionManager* C = Connection();
    const int32 Groups = FWowTalentModel::NumGroups(C);
    if (Groups < 2) return; // a single spec tab is hidden
    const TArray<const FTalentTabDbcEntry*> Tabs = PlayerTabs();
    for (int32 Group = 0; Group < Groups && Group < 2; ++Group)
    {
        const float X = TalentFrameW - 32.f, Y = 65.f + Group * (32.f + 22.f);
        PlaceImage(Canvas, X - 3, Y - 11, 64, 64, Brush(TEXT("Interface\\SpellBook\\SpellBook-SkillLineTab")));
        // Icon: the spec's primary tree, the hybrid icon, or Ability_Marksmanship when untalented.
        FString Icon = TEXT("Interface\\Icons\\Ability_Marksmanship");
        int32 Total = 0;
        for (const FTalentTabDbcEntry* Tab : Tabs) Total += FWowTalentModel::PointsInTab(C, Group, Tab->TalentTabID, false);
        const int32 Primary = FWowTalentModel::PrimaryTab(C, Group);
        if (Tabs.IsValidIndex(Primary)) Icon = FWowTalentModel::IconPath(*Tabs[Primary]);
        else if (Total > 0) Icon = TEXT("Interface\\Icons\\Ability_DualWieldSpecialization");
        PlaceImage(Canvas, X, Y, 32, 32, Brush(Icon));
        if (Group == ShownGroup()) Place(Canvas, X, Y, 32, 32, SNew(SWowAddImage).Visibility(EVisibility::HitTestInvisible)
            .Image(Brush(TEXT("Interface\\Buttons\\CheckButtonHilight"))).ColorAndOpacity(FLinearColor(1, 1, 1, .8f)));
        TArray<FWowTalentTooltipLine> Lines;
        Lines.Add({Group == 0 ? TEXT("Primary Talents") : TEXT("Secondary Talents"), FLinearColor::White, false});
        for (const FTalentTabDbcEntry* Tab : Tabs)
            Lines.Add({FString::Printf(TEXT("%s: %d points"), *Tab->Name, FWowTalentModel::PointsInTab(C, Group, Tab->TalentTabID, false)), TalentHighlightColor, false});
        if (Group == FWowTalentModel::ActiveGroup(C)) Lines.Add({TEXT("These are your active talents"), TalentGreenColor, false});
        Place(Canvas, X, Y, 32, 32, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .ToolTip(MakeTooltip(Lines))
            .OnMouseButtonDown_Lambda([this, Group](const FGeometry&, const FPointerEvent&) { ViewGroup = Group; bConfirmLearn = false; return FReply::Handled(); }));
    }
}

FReply SWowTalentWindow::OnTalentMouse(uint32 TalentId, const FPointerEvent& Event)
{
    UWowConnectionManager* C = Connection();
    const FTalentDbcEntry* Talent = FDbcStore::Get().Talents().GetById(TalentId);
    // Only the active spec can be changed (PlayerTalentFrameTalent_OnClick).
    if (!C || !Talent || !IsViewingActive()) return FReply::Handled();
    if (Event.GetEffectingButton() == EKeys::LeftMouseButton) FWowTalentModel::AddPreview(C, *Talent);
    else if (Event.GetEffectingButton() == EKeys::RightMouseButton) FWowTalentModel::RemovePreview(C, *Talent);
    return FReply::Handled();
}
