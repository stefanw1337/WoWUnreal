#include "SWowGroupFinder.h"
#include "WowColor.h"
#include "WowTextCentering.h"
#include "WowWindowStyle.h"
#include "WowNativeWidgetStyle.h"
#include "WowRetainedSlateBrush.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"

#include "WowConnectionManager.h"
#include "WowEntity.h"
#include "WowSessionState.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/LfgDungeonsDbc.h"
#include "WowWotlkStyle.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/ConfigCacheIni.h"
#include "CoreGlobals.h"

#define LOCTEXT_NAMESPACE "WowGroupFinder"

namespace
{
    FString FormatWaitTime(int32 Seconds)
    {
        if (Seconds < 0) return TEXT("Unavailable");
        if (Seconds < 60) return FString::Printf(TEXT("%d sec"), Seconds);
        return FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
    }

    FString RoleText(uint8 Roles)
    {
        TArray<FString> Parts;
        if (Roles & WowLfgRole::Tank) Parts.Add(TEXT("Tank"));
        if (Roles & WowLfgRole::Healer) Parts.Add(TEXT("Healer"));
        if (Roles & WowLfgRole::Damage) Parts.Add(TEXT("Damage"));
        return Parts.IsEmpty() ? TEXT("No role") : FString::Join(Parts, TEXT(", "));
    }

    // 3.3.5 FontStyles: GameFontNormal gold, GameFontHighlight white, disabled grey; parchment text.
    FLinearColor Srgb(uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); }
    const FLinearColor LfgGold = WowColor::FromWow(1.f, .82f, 0.f);
    const FLinearColor LfgGrey = WowColor::FromWow(.5f, .5f, .5f);
    FLinearColor Parchment() { return Srgb(46, 31, 15); }
    FLinearColor ParchmentTitle() { return Srgb(0, 0, 0); }

    // GetTexCoordsForRole (LFGFrame.lua), UI-LFG-ICON-ROLES 64x64.
    FBox2f RoleUV(uint8 Role)
    {
        // GetTexCoordsForRole (LFDFrame.lua): 67x67 cells on the 256x256 UI-LFG-ICON-ROLES sheet.
        auto Cell = [](int32 X, int32 Y)
        {
            const float W = 67.f / 256.f;
            return FBox2f(FVector2f((X - 1) * W, (Y - 1) * W), FVector2f(X * W, Y * W));
        };
        switch (Role)
        {
        case WowLfgRole::Tank: return Cell(1, 2);
        case WowLfgRole::Healer: return Cell(2, 1);
        case WowLfgRole::Damage: return Cell(2, 2);
        default: return Cell(1, 1); // leader (GUIDE)
        }
    }
}

// ── Construction / layout ─────────────────────────────────────────────────

void SWowGroupFinder::Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
    UIManager = InArgs._UIManager;
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UIManager->GetFrameManager()->GetUIScale();
    ChildSlot
    [
        // The tabs hang below the frame.
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return FrameW * Scale; })
        .HeightOverride_Lambda([this]() { return (FrameH + 26.f) * Scale; })
    ];
    Rebuild();
    SetVisibility(CurrentVisibility);
    RefreshFromState();
}

void SWowGroupFinder::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale))
        {
            Scale = NewScale;
            Rebuild();
            RefreshFromState();
        }
    }
}

void SWowGroupFinder::Rebuild()
{
    if (!Root.IsValid()) return;
    ButtonStyles.Reset();
    ScrollStyle = MakeShared<FScrollBarStyle>(FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar"));
    static const FSlateNoResource NoTrack;
    ScrollStyle->SetVerticalBackgroundImage(NoTrack).SetVerticalTopSlotImage(NoTrack).SetVerticalBottomSlotImage(NoTrack)
        .SetHorizontalBackgroundImage(NoTrack).SetHorizontalTopSlotImage(NoTrack).SetHorizontalBottomSlotImage(NoTrack);
    if (const FSlateBrush* Knob = LoadBrush(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"), FBox2f(FVector2f(.20f, .125f), FVector2f(.80f, .875f))))
        ScrollStyle->SetNormalThumbImage(*Knob).SetHoveredThumbImage(*Knob).SetDraggedThumbImage(*Knob);

    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    // Ring portrait (LFG eye for dungeons, LFR for raids) under the quest log's dual-pane art.
    Place(*Canvas, 3, 4, 64, 64, SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this]()
    {
        return LoadBrush(ActivePage == EPage::DungeonFinder ? TEXT("Interface\\LFGFrame\\UI-LFG-PORTRAIT") : TEXT("Interface\\LFGFrame\\UI-LFR-PORTRAIT"));
    }));
    Place(*Canvas, 0, 0, 512, 445, Img(TEXT("Interface\\QuestFrame\\UI-QuestLogDualPane-Left"), FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, .86914f))));
    Place(*Canvas, 512, 0, 170, 445, Img(TEXT("Interface\\QuestFrame\\UI-QuestLogDualPane-RIGHT"), FBox2f(FVector2f(0.f, 0.f), FVector2f(.6640625f, .86914f))));
    Place(*Canvas, 191, 15, 300, 14, WowTitleBox(SNew(STextBlock).Font(Fnt(12)).ColorAndOpacity(LfgGold).Justification(ETextJustify::Center)
        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
        .Text_Lambda([this]() { return ActivePage == EPage::DungeonFinder ? LOCTEXT("LfdTitle", "Dungeon Finder") : LOCTEXT("LfrTitle", "Looking For Raid"); }), Fnt(12)));
    {
        auto Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
        if (const FSlateBrush* Up = LoadBrush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"))) { Style->SetNormal(*Up); Style->SetHovered(*Up); }
        if (const FSlateBrush* Down = LoadBrush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down"))) Style->SetPressed(*Down);
        Style->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        ButtonStyles.Add(Style);
        Place(*Canvas, 652, 8, 32, 32, SNew(SButton).ButtonStyle(&Style.Get()).ContentPadding(0)
            .OnClicked(this, &SWowGroupFinder::OnClose));
    }
    BuildRoles(*Canvas);
    BuildDungeonPage(*Canvas);
    BuildRaidPage(*Canvas);
    BuildTabs(*Canvas);
    // Close (QuestLogFrameCancelButton position).
    Place(*Canvas, 595, 411, 80, 22, PanelButton(LOCTEXT("Close", "Close"), 80, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnClose)));
    Root->SetContent(Canvas);
    RebuildDungeonList();
    RebuildRaidList();
    RebuildRaidResults();
    RefreshPageVisibility();
}

void SWowGroupFinder::BuildRoles(SCanvas& Canvas)
{
    // Role buttons (LFDQueueFrameRoleButton*): role icon with its check box in the corner.
    Place(Canvas, 78, 46, 60, 14, SNew(STextBlock).Text(LOCTEXT("Roles", "Roles:")).Font(Fnt(12)).ColorAndOpacity(LfgGold)
        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black));
    struct FRole { uint8 Mask; bool* Value; FText Name; };
    const FRole Roles[] = {
        { WowLfgRole::Tank, &bTank, LOCTEXT("TankRole", "Tank") },
        { WowLfgRole::Healer, &bHealer, LOCTEXT("HealerRole", "Healer") },
        { WowLfgRole::Damage, &bDamage, LOCTEXT("DamageRole", "Damage") },
        { WowLfgRole::Leader, &bLeader, LOCTEXT("LeaderRole", "Leader") } };
    float X = 128.f;
    for (const FRole& Role : Roles)
    {
        bool* Value = Role.Value;
        const uint8 Mask = Role.Mask;
        Place(Canvas, X, 34, 34, 34, SNew(SBox).ToolTipText(Role.Name)
            [SNew(SImage).Image_Lambda([this, Mask]() { return LoadBrush(TEXT("Interface\\LFGFrame\\UI-LFG-ICON-ROLES"), RoleUV(Mask)); })
                .ColorAndOpacity_Lambda([Value]() { return FSlateColor(*Value ? FLinearColor::White : WowColor::FromWow(.55f, .55f, .55f)); })]);
        Place(Canvas, X - 4, 52, 20, 20, CheckButton(20, [Value]() { return *Value; },
            [this, Value, Mask]() { OnRoleChanged(*Value ? ECheckBoxState::Unchecked : ECheckBoxState::Checked, Mask); },
            []() { return true; }));
        X += 46.f;
    }
}

void SWowGroupFinder::BuildDungeonPage(SCanvas& Canvas)
{
    TSharedRef<SCanvas> Page = SNew(SCanvas);
    // Left page: every Dungeon Finder entry with its check box (QuestLogScrollFrame area).
    TSharedRef<SScrollBar> ListBar = MakeScrollBar();
    SAssignNew(DungeonListBox, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        .ExternalScrollbar(ListBar).ScrollBarVisibility(EVisibility::Collapsed);
    Place(*Page, 19, 75, 305, 335, DungeonListBox.ToSharedRef());
    PlaceScrollBar(*Page, 324, 88, 396, ListBar, DungeonListBox);

    // Right page: details on the parchment.
    TSharedRef<SScrollBar> DetailBar = MakeScrollBar();
    TSharedPtr<SScrollBox> Details;
    SAssignNew(Details, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        .ExternalScrollbar(DetailBar).ScrollBarVisibility(EVisibility::Collapsed)
        + SScrollBox::Slot().Padding(FMargin(6 * Scale, 4 * Scale, 10 * Scale, 8 * Scale))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("DungeonDetails", "Dungeon Details")).Font(FWowNativeWidgetStyle::Heading(FMath::RoundToInt(18 * Scale))).ColorAndOpacity(ParchmentTitle()) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6 * Scale, 0, 0)
            [ SNew(STextBlock).Text(this, &SWowGroupFinder::GetSelectedDescription).Font(Fnt(13)).ColorAndOpacity(Parchment()).AutoWrapText(true) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 12 * Scale, 0, 2 * Scale)
            [ SNew(STextBlock).Text(LOCTEXT("Comment", "Comment")).Font(Fnt(13)).ColorAndOpacity(ParchmentTitle()) ]
            + SVerticalBox::Slot().AutoHeight()
            [ InputBox(DungeonCommentBox, LOCTEXT("CommentHint", "Optional party note")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10 * Scale, 0, 0)
            [ SNew(STextBlock).Text(this, &SWowGroupFinder::GetQueueStatusText).Visibility(this, &SWowGroupFinder::GetDungeonStatusVisibility)
                .Font(Fnt(13)).ColorAndOpacity(Parchment()).AutoWrapText(true) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10 * Scale, 0, 0)
            [
                SNew(SVerticalBox).Visibility(this, &SWowGroupFinder::GetProposalVisibility)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Text(LOCTEXT("GroupReady", "Your group is ready!")).Font(FWowNativeWidgetStyle::Heading(FMath::RoundToInt(16 * Scale))).ColorAndOpacity(ParchmentTitle()) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6 * Scale, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()[ PanelButton(LOCTEXT("EnterDungeon", "Enter Dungeon"), 120, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnProposalAnswer, true)) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(6 * Scale, 0, 0, 0)[ PanelButton(LOCTEXT("Decline", "Decline"), 90, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnProposalAnswer, false)) ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 14 * Scale, 0, 0)
            [ SNew(STextBlock).Text(this, &SWowGroupFinder::GetConnectionHint).Font(Fnt(11)).ColorAndOpacity(Srgb(98, 82, 62)).AutoWrapText(true) ]
        ];
    Place(*Page, 352, 77, 298, 334, Details.ToSharedRef());
    PlaceScrollBar(*Page, 656, 90, 395, DetailBar, Details);

    // QuestLogControlPanel area: Find Group / Leave Queue, Teleport.
    Place(*Page, 18, 411, 120, 22, SNew(SBox).Visibility(this, &SWowGroupFinder::GetFindGroupVisibility)
        [ PanelButton(GetPrimaryActionText(), 120, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnFindGroup),
            TAttribute<bool>::CreateLambda([this]()
            {
                // Grey until a dungeon (random or specific) and at least one role are picked.
                return (SelectedRandomEntry != 0 || !SelectedSpecificEntries.IsEmpty())
                    && (GetSelectedRoles() & (WowLfgRole::Tank | WowLfgRole::Healer | WowLfgRole::Damage)) != 0;
            })) ]);
    Place(*Page, 18, 411, 120, 22, SNew(SBox).Visibility(this, &SWowGroupFinder::GetLeaveQueueVisibility)
        [ PanelButton(LOCTEXT("LeaveQueue", "Leave Queue"), 120, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnLeaveQueue)) ]);
    Place(*Page, 142, 411, 150, 22, SNew(SBox).Visibility(this, &SWowGroupFinder::GetTeleportVisibility)
        [ PanelButton(LOCTEXT("TeleportIn", "Teleport to Dungeon"), 150, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnTeleport, false)) ]);
    DungeonPageWidget = Page;
    Place(Canvas, 0, 0, FrameW, FrameH, Page);
}

void SWowGroupFinder::BuildRaidPage(SCanvas& Canvas)
{
    TSharedRef<SCanvas> Page = SNew(SCanvas);
    // Left page: raids.
    TSharedRef<SScrollBar> ListBar = MakeScrollBar();
    SAssignNew(RaidListingBox, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        .ExternalScrollbar(ListBar).ScrollBarVisibility(EVisibility::Collapsed);
    Place(*Page, 19, 75, 305, 335, RaidListingBox.ToSharedRef());
    PlaceScrollBar(*Page, 324, 88, 396, ListBar, RaidListingBox);

    // List Me / Browse switch in the top strip (the active one is pressed in).
    Place(*Page, 480, 40, 85, 22, PanelButton(LOCTEXT("ListMeTab", "List Me"), 85, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnRaidListTab),
        TAttribute<bool>::CreateLambda([this]() { return ActiveRaidPage != ERaidPage::List; })));
    Place(*Page, 568, 40, 85, 22, PanelButton(LOCTEXT("BrowseTab", "Browse"), 85, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnRaidBrowseTab),
        TAttribute<bool>::CreateLambda([this]() { return ActiveRaidPage != ERaidPage::Browse; })));

    // Right page, "List Me": comment and listing.
    TSharedRef<SCanvas> ListPage = SNew(SCanvas);
    Place(*ListPage, 352, 77, 298, 334, SNew(SBox).Padding(FMargin(6 * Scale, 4 * Scale, 10 * Scale, 0))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("RaidBrowser", "Raid Browser")).Font(FWowNativeWidgetStyle::Heading(FMath::RoundToInt(18 * Scale))).ColorAndOpacity(ParchmentTitle()) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6 * Scale, 0, 0)
            [ SNew(STextBlock).Text(LOCTEXT("RaidListHelp", "Choose a raid on the left, pick your roles and list yourself or your group so raid leaders can find you.")).Font(Fnt(13)).ColorAndOpacity(Parchment()).AutoWrapText(true) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 12 * Scale, 0, 2 * Scale)
            [ SNew(STextBlock).Text(LOCTEXT("RaidComment", "Comment")).Font(Fnt(13)).ColorAndOpacity(ParchmentTitle()) ]
            + SVerticalBox::Slot().AutoHeight()
            [ InputBox(RaidCommentBox, LOCTEXT("RaidCommentHint", "What kind of raid group are you looking for?")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 14 * Scale, 0, 0)
            [ SNew(STextBlock).Text(this, &SWowGroupFinder::GetConnectionHint).Font(Fnt(11)).ColorAndOpacity(Srgb(98, 82, 62)).AutoWrapText(true) ]
        ]);
    Place(*ListPage, 18, 411, 150, 22, SNew(SBox).Visibility(this, &SWowGroupFinder::GetRaidListVisibility)
        [ PanelButton(GetRaidListingActionText(), 150, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnListRaid)) ]);
    Place(*ListPage, 18, 411, 150, 22, SNew(SBox).Visibility(this, &SWowGroupFinder::GetRaidUnlistVisibility)
        [ PanelButton(LOCTEXT("Unlist", "Unlist"), 150, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnLeaveRaidListing)) ]);
    RaidListPageWidget = ListPage;
    Place(*Page, 0, 0, FrameW, FrameH, ListPage);

    // Right page, "Browse": players and groups listed for the selected raid.
    TSharedRef<SCanvas> BrowsePage = SNew(SCanvas);
    TSharedRef<SScrollBar> ResultBar = MakeScrollBar();
    SAssignNew(RaidResultBox, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        .ExternalScrollbar(ResultBar).ScrollBarVisibility(EVisibility::Collapsed);
    Place(*BrowsePage, 352, 77, 298, 334, RaidResultBox.ToSharedRef());
    PlaceScrollBar(*BrowsePage, 656, 90, 395, ResultBar, RaidResultBox);
    Place(*BrowsePage, 18, 411, 100, 22, PanelButton(LOCTEXT("Refresh", "Refresh"), 100, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnRefreshRaidSearch)));
    Place(*BrowsePage, 122, 411, 100, 22, PanelButton(LOCTEXT("Invite", "Invite"), 100, 22, FOnClicked::CreateSP(this, &SWowGroupFinder::OnInviteRaidResult),
        TAttribute<bool>::CreateLambda([this]() { return SelectedRaidPlayerGuid != 0; })));
    RaidBrowsePageWidget = BrowsePage;
    Place(*Page, 0, 0, FrameW, FrameH, BrowsePage);

    RaidPageWidget = Page;
    Place(Canvas, 0, 0, FrameW, FrameH, Page);
}

void SWowGroupFinder::BuildTabs(SCanvas& Canvas)
{
    // CharacterFrameTabButtonTemplate under the frame: Dungeons / Raids.
    const FText Labels[] = { LOCTEXT("DungeonsTab", "Dungeon Finder"), LOCTEXT("RaidsTab", "Looking For Raid") };
    float X = 15.f;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const EPage Page = Index == 0 ? EPage::DungeonFinder : EPage::RaidBrowser;
        const float TextW = FSlateApplication::IsInitialized()
            ? FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Labels[Index], Fnt(10)).X / FMath::Max(Scale, .01f) : 80.f;
        const float W = FMath::Max(80.f, TextW + 40.f);
        auto Piece = [this, Page](float L, float R)
        {
            return SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, Page, L, R]()
            {
                return LoadBrush(ActivePage == Page ? TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ActiveTab") : TEXT("Interface\\PaperDollInfoFrame\\UI-Character-InActiveTab"),
                    FBox2f(FVector2f(L, 0), FVector2f(R, 1)));
            });
        };
        const float Y = FrameH - 12.f;
        Place(Canvas, X, Y, 20, 32, Piece(0, .15625f));
        Place(Canvas, X + 20, Y, W - 40, 32, Piece(.15625f, .84375f));
        Place(Canvas, X + W - 20, Y, 20, 32, Piece(.84375f, 1));
        Place(Canvas, X, Y, W, 32, SNew(SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(FMargin(0, 0, 0, 6 * Scale))
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            .OnClicked(this, Index == 0 ? &SWowGroupFinder::OnDungeonTab : &SWowGroupFinder::OnRaidTab)
            [ SNew(STextBlock).Text(Labels[Index]).Font(Fnt(10)).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
                .ColorAndOpacity_Lambda([this, Page]() { return FSlateColor(ActivePage == Page ? FLinearColor::White : LfgGold); }) ]);
        X += W - 15.f;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────

void SWowGroupFinder::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y) * Scale).Size(FVector2D(W, H) * Scale)[Widget];
}

FSlateFontInfo SWowGroupFinder::Fnt(float WowPixels) const
{
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
}

const FSlateBrush* SWowGroupFinder::LoadBrush(const FString& TexturePath, FBox2f UV) const
{
    const FString Key = FString::Printf(TEXT("%s|%.4f,%.4f,%.4f,%.4f"), *TexturePath, UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
    if (const TSharedPtr<FWowRetainedSlateBrush>* Existing = TextureBrushes.Find(Key)) return Existing->Get();
    const double Now = FPlatformTime::Seconds();
    if (const double* Failed = FailedTextureTimes.Find(Key); Failed && Now - *Failed < 2.0) return nullptr;
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager() ? UIManager->GetFrameManager()->LoadUITexture(TexturePath) : nullptr;
    if (!Texture) { FailedTextureTimes.Add(Key, Now); return nullptr; }
    FailedTextureTimes.Remove(Key);
    TSharedPtr<FWowRetainedSlateBrush> Brush = MakeShared<FWowRetainedSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->SetUVRegion(UV);
    Brush->ImageSize = FVector2D(Texture->GetSizeX() * (UV.Max.X - UV.Min.X), Texture->GetSizeY() * (UV.Max.Y - UV.Min.Y));
    TextureBrushes.Add(Key, Brush);
    return Brush.Get();
}

TSharedRef<SWidget> SWowGroupFinder::Img(const FString& Path, FBox2f UV) const
{
    return SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, Path, UV]() { return LoadBrush(Path, UV); });
}

TSharedRef<SWidget> SWowGroupFinder::PanelButton(const FText& Label, float W, float H, FOnClicked OnClicked, TAttribute<bool> Enabled) const
{
    // UIPanelButtonTemplate: red art, GameFontNormal.
    return SNew(SBox).WidthOverride(W * Scale).HeightOverride(H * Scale)
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .ContentPadding(FMargin(2 * Scale, 0)).IsEnabled(Enabled).OnClicked(OnClicked)
            [ SNew(STextBlock).Text(Label).Font(Fnt(12)).OverflowPolicy(ETextOverflowPolicy::Clip).RenderTransform(WowCapCenterTransform(Fnt(12)))
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
        ];
}

TSharedRef<SWidget> SWowGroupFinder::CheckButton(float Size, TFunction<bool()> IsChecked, TFunction<void()> Toggle, TFunction<bool()> Enabled) const
{
    // UICheckButtonTemplate: UI-CheckBox-Up/Down, Check (Check-Disabled when disabled), Highlight (ADD).
    TSharedPtr<SButton> Button;
    SAssignNew(Button, SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
        .IsEnabled_Lambda([Enabled]() { return Enabled(); })
        .OnClicked_Lambda([Toggle]() { Toggle(); return FReply::Handled(); });
    const TWeakPtr<SButton> Weak = Button;
    Button->SetContent(
        SNew(SOverlay)
        + SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, Weak]()
            { const auto B = Weak.Pin(); return LoadBrush(B.IsValid() && B->IsPressed() ? TEXT("Interface\\Buttons\\UI-CheckBox-Down") : TEXT("Interface\\Buttons\\UI-CheckBox-Up")); }) ]
        + SOverlay::Slot()[ SNew(SWowAddImage).Image_Lambda([this]() { return LoadBrush(TEXT("Interface\\Buttons\\UI-CheckBox-Highlight")); })
            .ColorAndOpacity_Lambda([Weak]() { const auto B = Weak.Pin(); return B.IsValid() && B->IsHovered() && B->IsEnabled() ? FLinearColor::White : FLinearColor::Transparent; }) ]
        + SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, Enabled]()
            { return LoadBrush(Enabled() ? TEXT("Interface\\Buttons\\UI-CheckBox-Check") : TEXT("Interface\\Buttons\\UI-CheckBox-Check-Disabled")); })
            .Visibility_Lambda([IsChecked]() { return IsChecked() ? EVisibility::HitTestInvisible : EVisibility::Hidden; }) ]);
    return SNew(SBox).WidthOverride(Size * Scale).HeightOverride(Size * Scale)[ Button.ToSharedRef() ];
}

TSharedRef<SWidget> SWowGroupFinder::InputBox(TSharedPtr<SEditableTextBox>& OutBox, const FText& Hint) const
{
    // InputBoxTemplate: Common-Input-Border, ChatFontNormal.
    static FSlateBrush InputBrush;
    if (const FSlateBrush* Border = LoadBrush(TEXT("Interface\\Common\\Common-Input-Border"), FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, .625f))))
    { InputBrush = *Border; InputBrush.DrawAs = ESlateBrushDrawType::Box; InputBrush.Margin = FMargin(.0625f, .4f, .0625f, .4f); }
    return SNew(SBox).HeightOverride(22 * Scale)
        [
            SNew(SBorder).BorderImage(&InputBrush).Padding(FMargin(6 * Scale, 0)).VAlign(VAlign_Center)
            [
                SAssignNew(OutBox, SEditableTextBox).Style(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
                .BackgroundColor(FLinearColor::Transparent).ForegroundColor(FLinearColor::White).Font(Fnt(13)).HintText(Hint)
            ]
        ];
}

TSharedRef<SScrollBar> SWowGroupFinder::MakeScrollBar() const
{
    return SNew(SScrollBar).Orientation(Orient_Vertical).Style(ScrollStyle.Get())
        .Thickness(FVector2D(16 * Scale, 16 * Scale)).Padding(0).AlwaysShowScrollbar(true);
}

void SWowGroupFinder::PlaceScrollBar(SCanvas& Canvas, float X, float Top, float Bottom, TSharedRef<SScrollBar> Bar, TSharedPtr<SScrollBox> Box) const
{
    // UIPanelScrollUp/DownButtonTemplate outside the slider; the trough is part of the frame art.
    auto Arrow = [this, Box](const TCHAR* Name, float Step)
    {
        const FString Base = FString::Printf(TEXT("Interface\\Buttons\\UI-ScrollBar-Scroll%sButton-"), Name);
        const FBox2f UV(FVector2f(.20f, .25f), FVector2f(.80f, .75f));
        TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
        if (const FSlateBrush* Up = LoadBrush(Base + TEXT("Up"), UV)) { Style->SetNormal(*Up); Style->SetHovered(*Up); }
        if (const FSlateBrush* Down = LoadBrush(Base + TEXT("Down"), UV)) Style->SetPressed(*Down);
        if (const FSlateBrush* Off = LoadBrush(Base + TEXT("Disabled"), UV)) Style->SetDisabled(*Off);
        Style->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        const TWeakPtr<SScrollBox> WeakBox = Box;
        return SNew(SButton).ButtonStyle(&Style.Get()).ContentPadding(0).ClickMethod(EButtonClickMethod::MouseDown)
            .IsEnabled_Lambda([WeakBox, Step]()
            {
                const TSharedPtr<SScrollBox> B = WeakBox.Pin();
                if (!B.IsValid()) return false;
                return Step < 0 ? B->GetScrollOffset() > 0.5f : B->GetScrollOffset() < B->GetScrollOffsetOfEnd() - 0.5f;
            })
            .OnClicked_Lambda([WeakBox, Step, Style]()
            {
                if (const TSharedPtr<SScrollBox> B = WeakBox.Pin())
                    B->SetScrollOffset(FMath::Clamp(B->GetScrollOffset() + Step, 0.f, B->GetScrollOffsetOfEnd()));
                return FReply::Handled();
            });
    };
    Place(Canvas, X, Top - 16, 16, 16, Arrow(TEXT("Up"), -16.f * Scale));
    Place(Canvas, X, Top, 16, Bottom - Top, Bar);
    Place(Canvas, X, Bottom, 16, 16, Arrow(TEXT("Down"), 16.f * Scale));
}

TSharedRef<SWidget> SWowGroupFinder::SelectRow(TFunction<bool()> IsSelected, FOnClicked OnClicked, TSharedRef<SWidget> Content) const
{
    // Quest log row: UI-QuestLogTitleHighlight under the selected / hovered entry.
    TSharedPtr<SButton> Button;
    SAssignNew(Button, SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0).OnClicked(OnClicked)[Content];
    const TWeakPtr<SButton> Weak = Button;
    return SNew(SOverlay)
        + SOverlay::Slot()
        [ SNew(SWowAddImage).Image_Lambda([this]() { return LoadBrush(TEXT("Interface\\QuestFrame\\UI-QuestLogTitleHighlight")); })
            .ColorAndOpacity_Lambda([Weak, IsSelected]()
            {
                const auto B = Weak.Pin();
                if (IsSelected()) return WowColor::FromWow(1.f, .82f, 0.f, 1.f);
                return B.IsValid() && B->IsHovered() ? WowColor::FromWow(1.f, .82f, 0.f, .5f) : FLinearColor::Transparent;
            }) ]
        + SOverlay::Slot()[ Button.ToSharedRef() ];
}

// ── Lists ─────────────────────────────────────────────────────────────────

void SWowGroupFinder::RebuildDungeonList()
{
    if (!DungeonListBox.IsValid()) return;
    DungeonListBox->ClearChildren();

    uint32 PlayerLevel = 1;
    if (ConnectionManager)
        if (const FWowPlayerEntity* Player = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
            PlayerLevel = static_cast<uint32>(FMath::Max(1, Player->GetLevel()));

    // Random dungeons always first, then pinned favourites (right-click a dungeon), then the rest.
    LoadPins();
    TArray<const FLfgDungeonDbcEntry*> Ordered;
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
        if (Entry.IsDungeonFinderEntry()) Ordered.Add(&Entry);
    Ordered.StableSort([this](const FLfgDungeonDbcEntry& A, const FLfgDungeonDbcEntry& B)
    {
        auto Rank = [this](const FLfgDungeonDbcEntry& E) { return E.TypeID == 6 ? 0 : PinnedDungeons.Contains(E.ID) ? 1 : 2; };
        return Rank(A) < Rank(B);
    });
    for (const FLfgDungeonDbcEntry* EntryPtr : Ordered)
    {
        const FLfgDungeonDbcEntry& Entry = *EntryPtr;
        const bool bAvailable = (Entry.MinLevel == 0 || PlayerLevel >= Entry.MinLevel)
            && (Entry.MaxLevel == 0 || PlayerLevel <= Entry.MaxLevel);
        const bool bRandom = Entry.TypeID == 6;
        const uint32 Packed = Entry.GetPackedEntry();
        const FString Name = Entry.Name.IsEmpty() ? FString::Printf(TEXT("Dungeon %u"), Entry.ID) : Entry.Name;
        const FString Levels = Entry.MinLevel > 0 ? FString::Printf(TEXT("%u-%u"), Entry.MinLevel, Entry.MaxLevel) : FString();
        auto IsChecked = [this, Packed, bRandom]() { return bRandom ? SelectedRandomEntry == Packed : SelectedSpecificEntries.Contains(Packed); };
        auto Toggle = [this, Packed, bRandom, IsChecked]() { OnDungeonChanged(IsChecked() ? ECheckBoxState::Unchecked : ECheckBoxState::Checked, Packed, bRandom); };
        const FLinearColor Color = !bAvailable ? LfgGrey : bRandom ? FLinearColor::White : LfgGold;
        const uint32 DungeonId = Entry.ID;
        const bool bPinned = !bRandom && PinnedDungeons.Contains(DungeonId);
        DungeonListBox->AddSlot().Padding(0, 0, 0, 1 * Scale)
        [
            SNew(SBox).HeightOverride(20 * Scale)
            .ToolTipText(bRandom ? FText::GetEmpty() : (bPinned ? LOCTEXT("UnpinHint", "Right-click to unpin") : LOCTEXT("PinHint", "Right-click to pin to the top")))
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
                .OnMouseButtonDown_Lambda([this, DungeonId, bRandom](const FGeometry&, const FPointerEvent& Event)
                {
                    if (bRandom || Event.GetEffectingButton() != EKeys::RightMouseButton) return FReply::Unhandled();
                    if (PinnedDungeons.Remove(DungeonId) == 0) PinnedDungeons.Add(DungeonId);
                    SavePins();
                    RebuildDungeonList();
                    return FReply::Handled();
                })
                [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2 * Scale, 0, 4 * Scale, 0)
                [ CheckButton(20, IsChecked, Toggle, [bAvailable]() { return bAvailable; }) ]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(Name)).Font(Fnt(12)).ColorAndOpacity(Color).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4 * Scale, 0, 4 * Scale, 0)
                [ SNew(STextBlock).Text(FText::FromString(bPinned ? FString(TEXT("Pinned  ")) + Levels : Levels)).Font(Fnt(10))
                    .ColorAndOpacity(bAvailable ? FLinearColor::White : LfgGrey)
                    .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
                ]
            ]
        ];
    }
}

void SWowGroupFinder::LoadPins()
{
    // Personal favourites, kept in the user's own settings ini (never shipped).
    PinnedDungeons.Reset();
    FString Saved;
    if (GConfig) GConfig->GetString(TEXT("WowUnreal.GroupFinder"), TEXT("PinnedDungeons"), Saved, GGameUserSettingsIni);
    TArray<FString> Ids;
    Saved.ParseIntoArray(Ids, TEXT(","));
    for (const FString& Id : Ids) if (const int32 Value = FCString::Atoi(*Id); Value > 0) PinnedDungeons.Add(uint32(Value));
}

void SWowGroupFinder::SavePins() const
{
    TArray<FString> Ids;
    for (uint32 Id : PinnedDungeons) Ids.Add(FString::FromInt(int32(Id)));
    if (GConfig)
    {
        GConfig->SetString(TEXT("WowUnreal.GroupFinder"), TEXT("PinnedDungeons"), *FString::Join(Ids, TEXT(",")), GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
}

void SWowGroupFinder::RebuildRaidList()
{
    if (!RaidListingBox.IsValid()) return;
    RaidListingBox->ClearChildren();
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
    {
        if (!Entry.IsRaidBrowserEntry()) continue;
        const uint32 Packed = Entry.GetPackedEntry();
        RaidListingBox->AddSlot().Padding(0, 0, 0, 1 * Scale)
        [
            SNew(SBox).HeightOverride(18 * Scale)
            [
                SelectRow([this, Packed]() { return SelectedRaidEntry == Packed; },
                    FOnClicked::CreateSP(this, &SWowGroupFinder::OnSelectRaid, Packed),
                    SNew(SBox).Padding(FMargin(8 * Scale, 0, 0, 0)).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(FText::FromString(Entry.Name)).Font(Fnt(12)).ColorAndOpacity(LfgGold)
                        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ])
            ]
        ];
    }
}

void SWowGroupFinder::RebuildRaidResults()
{
    if (!RaidResultBox.IsValid()) return;
    RaidResultBox->ClearChildren();
    if (!ConnectionManager || ConnectionManager->PacketHandler.LfgState.RaidResults.IsEmpty())
    {
        RaidResultBox->AddSlot().Padding(8 * Scale)
        [ SNew(STextBlock).Text(LOCTEXT("NoRaidResults", "No players or groups found.")).Font(Fnt(13)).ColorAndOpacity(Parchment()) ];
        return;
    }
    for (const FWowLfgRaidBrowserEntry& Entry : ConnectionManager->PacketHandler.LfgState.RaidResults)
    {
        const FString Line = FString::Printf(TEXT("%s  -  Level %u  -  %s"), *Entry.PlayerName, Entry.Level, *RoleText(Entry.Roles));
        const uint64 Guid = Entry.PlayerGuid;
        RaidResultBox->AddSlot().Padding(0, 0, 0, 2 * Scale)
        [
            SelectRow([this, Guid]() { return SelectedRaidPlayerGuid == Guid; },
                FOnClicked::CreateSP(this, &SWowGroupFinder::OnSelectRaidResult, Guid),
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(6 * Scale, 2 * Scale, 0, 0)
                [ SNew(STextBlock).Text(FText::FromString(Line)).Font(Fnt(12)).ColorAndOpacity(ParchmentTitle()) ]
                + SVerticalBox::Slot().AutoHeight().Padding(6 * Scale, 0, 0, 2 * Scale)
                [ SNew(STextBlock).Text(FText::FromString(Entry.Comment)).Font(Fnt(11)).ColorAndOpacity(Parchment()).AutoWrapText(true) ])
        ];
    }
}

void SWowGroupFinder::RefreshFromState()
{
    if (ConnectionManager)
    {
        const uint8 Roles = ConnectionManager->PacketHandler.LfgState.Roles;
        bLeader = (Roles & WowLfgRole::Leader) != 0;
        bTank = (Roles & WowLfgRole::Tank) != 0;
        bHealer = (Roles & WowLfgRole::Healer) != 0;
        bDamage = (Roles & WowLfgRole::Damage) != 0;
        RebuildRaidResults();
    }
    RefreshPageVisibility();
}

void SWowGroupFinder::RefreshPageVisibility()
{
    if (DungeonPageWidget.IsValid()) DungeonPageWidget->SetVisibility(ActivePage == EPage::DungeonFinder ? EVisibility::Visible : EVisibility::Collapsed);
    if (RaidPageWidget.IsValid()) RaidPageWidget->SetVisibility(ActivePage == EPage::RaidBrowser ? EVisibility::Visible : EVisibility::Collapsed);
    if (RaidListPageWidget.IsValid()) RaidListPageWidget->SetVisibility(ActiveRaidPage == ERaidPage::List ? EVisibility::Visible : EVisibility::Collapsed);
    if (RaidBrowsePageWidget.IsValid()) RaidBrowsePageWidget->SetVisibility(ActiveRaidPage == ERaidPage::Browse ? EVisibility::Visible : EVisibility::Collapsed);
}

void SWowGroupFinder::ToggleVisibility() { SetFinderVisibility(!IsVisible()); }
void SWowGroupFinder::SetFinderVisibility(bool bVisible)
{
    CurrentVisibility = bVisible ? EVisibility::Visible : EVisibility::Hidden;
    SetVisibility(CurrentVisibility);
    if (bVisible)
    {
        RebuildDungeonList();
        RebuildRaidList();
        RefreshFromState();
        if (ConnectionManager)
        {
            ConnectionManager->SendLfgStatusRequest();
            ConnectionManager->SendLfgPlayerLockInfoRequest();
            if (!ConnectionManager->PacketHandler.GroupInfo.Members.IsEmpty()) ConnectionManager->SendLfgPartyLockInfoRequest();
        }
    }
}
bool SWowGroupFinder::IsVisible() const { return CurrentVisibility == EVisibility::Visible; }

FReply SWowGroupFinder::OnClose() { SetFinderVisibility(false); return FReply::Handled(); }
FReply SWowGroupFinder::OnDungeonTab() { ActivePage = EPage::DungeonFinder; RefreshPageVisibility(); return FReply::Handled(); }
FReply SWowGroupFinder::OnRaidTab() { ActivePage = EPage::RaidBrowser; RefreshPageVisibility(); return FReply::Handled(); }
FReply SWowGroupFinder::OnRaidListTab() { ActiveRaidPage = ERaidPage::List; RefreshPageVisibility(); return FReply::Handled(); }
FReply SWowGroupFinder::OnRaidBrowseTab() { ActiveRaidPage = ERaidPage::Browse; RefreshPageVisibility(); return FReply::Handled(); }

void SWowGroupFinder::OnRoleChanged(ECheckBoxState State, uint8 RoleMask)
{
    const bool bChecked = State == ECheckBoxState::Checked;
    if (RoleMask == WowLfgRole::Leader) bLeader = bChecked;
    else if (RoleMask == WowLfgRole::Tank) bTank = bChecked;
    else if (RoleMask == WowLfgRole::Healer) bHealer = bChecked;
    else if (RoleMask == WowLfgRole::Damage) bDamage = bChecked;
    if (ConnectionManager && ConnectionManager->PacketHandler.LfgState.Mode == EWowLfgMode::RoleCheck)
    {
        ConnectionManager->SendLfgSetRoles(GetSelectedRoles());
    }
}

void SWowGroupFinder::OnDungeonChanged(ECheckBoxState State, uint32 PackedEntry, bool bRandom)
{
    if (bRandom)
    {
        SelectedRandomEntry = State == ECheckBoxState::Checked ? PackedEntry : 0;
        if (SelectedRandomEntry != 0) SelectedSpecificEntries.Reset();
    }
    else
    {
        SelectedRandomEntry = 0;
        if (State == ECheckBoxState::Checked) SelectedSpecificEntries.Add(PackedEntry);
        else SelectedSpecificEntries.Remove(PackedEntry);
    }
}

uint8 SWowGroupFinder::GetSelectedRoles() const
{
    uint8 Roles = 0;
    if (bLeader) Roles |= WowLfgRole::Leader;
    if (bTank) Roles |= WowLfgRole::Tank;
    if (bHealer) Roles |= WowLfgRole::Healer;
    if (bDamage) Roles |= WowLfgRole::Damage;
    return Roles;
}

FReply SWowGroupFinder::OnFindGroup()
{
    if (!ConnectionManager || ConnectionManager->GetState() != EWowSessionState::WorldInGame) return FReply::Handled();
    if ((GetSelectedRoles() & (WowLfgRole::Tank | WowLfgRole::Healer | WowLfgRole::Damage)) == 0) return FReply::Handled();
    TArray<uint32> Entries;
    if (SelectedRandomEntry != 0) Entries.Add(SelectedRandomEntry);
    else
    {
        for (uint32 Entry : SelectedSpecificEntries) Entries.Add(Entry);
    }
    if (Entries.IsEmpty()) return FReply::Handled();
    ConnectionManager->SendLfgJoin(GetSelectedRoles(), Entries, DungeonCommentBox.IsValid() ? DungeonCommentBox->GetText().ToString() : FString());
    return FReply::Handled();
}

FReply SWowGroupFinder::OnLeaveQueue() { if (ConnectionManager) ConnectionManager->SendLfgLeave(); return FReply::Handled(); }
FReply SWowGroupFinder::OnProposalAnswer(bool bAccept)
{
    if (ConnectionManager) ConnectionManager->SendLfgProposalResult(ConnectionManager->PacketHandler.LfgState.Proposal.ProposalId, bAccept);
    return FReply::Handled();
}
FReply SWowGroupFinder::OnTeleport(bool bOut) { if (ConnectionManager) ConnectionManager->SendLfgTeleport(bOut); return FReply::Handled(); }

FReply SWowGroupFinder::OnListRaid()
{
    if (ConnectionManager && SelectedRaidEntry != 0)
    {
        TArray<uint32> Entries;
        Entries.Add(SelectedRaidEntry);
        ConnectionManager->SendLfgJoin(GetSelectedRoles(), Entries, RaidCommentBox.IsValid() ? RaidCommentBox->GetText().ToString() : FString());
    }
    return FReply::Handled();
}
FReply SWowGroupFinder::OnLeaveRaidListing() { if (ConnectionManager) ConnectionManager->SendLfgLeave(); return FReply::Handled(); }
FReply SWowGroupFinder::OnSelectRaid(uint32 PackedEntry)
{
    SelectedRaidEntry = PackedEntry;
    if (ActiveRaidPage == ERaidPage::Browse && ConnectionManager)
    {
        const uint32 Previous = ConnectionManager->PacketHandler.LfgState.RaidSearchEntry;
        if (Previous != 0 && Previous != PackedEntry) ConnectionManager->LeaveRaidBrowserSearch(Previous);
        ConnectionManager->SendRaidBrowserSearch(PackedEntry);
    }
    return FReply::Handled();
}
FReply SWowGroupFinder::OnRefreshRaidSearch()
{
    if (ConnectionManager && SelectedRaidEntry != 0)
    {
        const uint32 Current = ConnectionManager->PacketHandler.LfgState.RaidSearchEntry;
        if (Current != 0) ConnectionManager->LeaveRaidBrowserSearch(Current);
        ConnectionManager->SendRaidBrowserSearch(SelectedRaidEntry);
    }
    return FReply::Handled();
}
FReply SWowGroupFinder::OnSelectRaidResult(uint64 PlayerGuid) { SelectedRaidPlayerGuid = PlayerGuid; return FReply::Handled(); }
FReply SWowGroupFinder::OnInviteRaidResult()
{
    if (!ConnectionManager || SelectedRaidPlayerGuid == 0) return FReply::Handled();
    const FWowLfgRaidBrowserEntry* Entry = ConnectionManager->PacketHandler.LfgState.RaidResults.FindByPredicate(
        [this](const FWowLfgRaidBrowserEntry& Candidate) { return Candidate.PlayerGuid == SelectedRaidPlayerGuid; });
    if (Entry && !Entry->PlayerName.StartsWith(TEXT("Player "))) ConnectionManager->SendGroupInviteByName(Entry->PlayerName);
    return FReply::Handled();
}

FText SWowGroupFinder::GetQueueStatusText() const
{
    if (!ConnectionManager) return FText::GetEmpty();
    const FWowLfgState& State = ConnectionManager->PacketHandler.LfgState;
    if (State.Mode == EWowLfgMode::RoleCheck) return LOCTEXT("RoleCheckStatus", "Waiting for party members to choose roles.");
    if (State.Mode == EWowLfgMode::Proposal) return LOCTEXT("ProposalStatus", "A group has been found. Choose Enter Dungeon or Decline.");
    if (!State.bQueued) return FText::GetEmpty();
    return FText::FromString(FString::Printf(TEXT("Time in queue: %s\nAverage wait: %s\nStill needed: %u tank, %u healer, %u damage"),
        *FormatWaitTime(static_cast<int32>(State.QueueStatus.QueuedSeconds)),
        *FormatWaitTime(State.QueueStatus.AverageWaitSeconds),
        State.QueueStatus.TanksNeeded, State.QueueStatus.HealersNeeded, State.QueueStatus.DamageNeeded));
}

FText SWowGroupFinder::GetPrimaryActionText() const { return LOCTEXT("FindGroup", "Find Group"); }
FText SWowGroupFinder::GetRaidListingActionText() const { return LOCTEXT("ListGroup", "List Me / My Group"); }
FText SWowGroupFinder::GetSelectedDescription() const
{
    if (SelectedRandomEntry == 0 && SelectedSpecificEntries.IsEmpty()) return LOCTEXT("SelectDungeon", "Select a random dungeon category or one or more specific dungeons.");
    const uint32 Packed = SelectedRandomEntry != 0 ? SelectedRandomEntry : *SelectedSpecificEntries.CreateConstIterator();
    if (const FLfgDungeonDbcEntry* Entry = FDbcStore::Get().LfgDungeons().GetById(Packed & 0x00FFFFFFu))
    {
        const FString Description = Entry->Description.IsEmpty() ? Entry->Name : Entry->Description;
        return FText::FromString(Description);
    }
    return FText::GetEmpty();
}
FText SWowGroupFinder::GetConnectionHint() const
{
    if (!ConnectionManager || ConnectionManager->GetState() != EWowSessionState::WorldInGame)
    {
        return LOCTEXT("OfflineHint", "Offline preview: queue and raid-browser actions become active after connecting to a realm.");
    }
    return LOCTEXT("WrathHint", "Wrath uses Dungeon Finder for five-player groups and a separate manual raid browser. PvP queues remain in the PvP window.");
}
EVisibility SWowGroupFinder::GetProposalVisibility() const
{
    return ConnectionManager && ConnectionManager->PacketHandler.LfgState.Mode == EWowLfgMode::Proposal ? EVisibility::Visible : EVisibility::Collapsed;
}
EVisibility SWowGroupFinder::GetDungeonStatusVisibility() const
{
    if (!ConnectionManager) return EVisibility::Collapsed;
    const FWowLfgState& State = ConnectionManager->PacketHandler.LfgState;
    return (State.bQueued || State.Mode == EWowLfgMode::RoleCheck || State.Mode == EWowLfgMode::Proposal) ? EVisibility::Visible : EVisibility::Collapsed;
}
EVisibility SWowGroupFinder::GetFindGroupVisibility() const
{
    if (!ConnectionManager) return EVisibility::Visible;
    const FWowLfgState& State = ConnectionManager->PacketHandler.LfgState;
    return (!State.bQueued && State.Mode != EWowLfgMode::RoleCheck && State.Mode != EWowLfgMode::Proposal)
        ? EVisibility::Visible : EVisibility::Collapsed;
}
EVisibility SWowGroupFinder::GetLeaveQueueVisibility() const
{
    if (!ConnectionManager) return EVisibility::Collapsed;
    const FWowLfgState& State = ConnectionManager->PacketHandler.LfgState;
    return (State.bQueued || State.Mode == EWowLfgMode::RoleCheck || State.Mode == EWowLfgMode::Proposal)
        ? EVisibility::Visible : EVisibility::Collapsed;
}
EVisibility SWowGroupFinder::GetTeleportVisibility() const
{
    return ConnectionManager && ConnectionManager->PacketHandler.LfgState.Mode == EWowLfgMode::Dungeon
        ? EVisibility::Visible : EVisibility::Collapsed;
}
EVisibility SWowGroupFinder::GetRaidListVisibility() const
{
    return ConnectionManager && ConnectionManager->PacketHandler.LfgState.bListedForRaid
        ? EVisibility::Collapsed : EVisibility::Visible;
}
EVisibility SWowGroupFinder::GetRaidUnlistVisibility() const
{
    return ConnectionManager && ConnectionManager->PacketHandler.LfgState.bListedForRaid
        ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
