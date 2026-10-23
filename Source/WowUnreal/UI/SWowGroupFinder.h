#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/SlateTypes.h"

class UWowConnectionManager;
class UWowUIManager;
class SCanvas;
class SBox;
class SEditableTextBox;
class SScrollBar;
class SScrollBox;
class STextBlock;
class SVerticalBox;
class FWowRetainedSlateBrush;

/**
 * Native Wrath 3.3.5 Dungeon Finder and Looking For Raid browser, drawn in the quest log's
 * dual-pane frame (UI-QuestLogDualPane, 682x445) with the LFG/LFR portrait in the ring:
 * role icons on top, the full dungeon/raid list on the left page, details/results on the
 * right page, UIPanelButtons along the bottom and Dungeon/Raid tabs under the frame.
 */
class WOWUNREAL_API SWowGroupFinder : public SCompoundWidget
{
public:
    static constexpr float FrameW = 682.f;
    static constexpr float FrameH = 445.f;

    SLATE_BEGIN_ARGS(SWowGroupFinder) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager);
    void ToggleVisibility();
    void SetFinderVisibility(bool bVisible);
    bool IsVisible() const;
    void RefreshFromState();
    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
    enum class EPage : uint8 { DungeonFinder, RaidBrowser };
    enum class ERaidPage : uint8 { List, Browse };

    void Rebuild();
    void BuildRoles(SCanvas& Canvas);
    void BuildDungeonPage(SCanvas& Canvas);
    void BuildRaidPage(SCanvas& Canvas);
    void BuildTabs(SCanvas& Canvas);
    void RebuildDungeonList();
    void RebuildRaidList();
    void RebuildRaidResults();
    void RefreshPageVisibility();

    // Helpers (WoW units x UI scale)
    void Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    FSlateFontInfo Fnt(float WowPixels) const;
    const FSlateBrush* LoadBrush(const FString& TexturePath, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;
    TSharedRef<SWidget> Img(const FString& Path, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;
    TSharedRef<SWidget> PanelButton(const FText& Label, float W, float H, FOnClicked OnClicked, TAttribute<bool> Enabled = true) const;
    TSharedRef<SWidget> CheckButton(float Size, TFunction<bool()> IsChecked, TFunction<void()> Toggle, TFunction<bool()> Enabled) const;
    TSharedRef<SWidget> InputBox(TSharedPtr<SEditableTextBox>& OutBox, const FText& Hint) const;
    TSharedRef<SScrollBar> MakeScrollBar() const;
    void PlaceScrollBar(SCanvas& Canvas, float X, float Top, float Bottom, TSharedRef<SScrollBar> Bar, TSharedPtr<SScrollBox> Box) const;
    TSharedRef<SWidget> SelectRow(TFunction<bool()> IsSelected, FOnClicked OnClicked, TSharedRef<SWidget> Content) const;

    FReply OnClose();
    FReply OnDungeonTab();
    FReply OnRaidTab();
    FReply OnRaidListTab();
    FReply OnRaidBrowseTab();
    FReply OnFindGroup();
    FReply OnLeaveQueue();
    FReply OnProposalAnswer(bool bAccept);
    FReply OnTeleport(bool bOut);
    FReply OnListRaid();
    FReply OnLeaveRaidListing();
    FReply OnSelectRaid(uint32 PackedEntry);
    FReply OnRefreshRaidSearch();
    FReply OnSelectRaidResult(uint64 PlayerGuid);
    FReply OnInviteRaidResult();
    void OnRoleChanged(ECheckBoxState State, uint8 RoleMask);
    void OnDungeonChanged(ECheckBoxState State, uint32 PackedEntry, bool bRandom);

    uint8 GetSelectedRoles() const;
    /** Dungeons pinned to the top of the list (right-click), saved in the user's settings ini. */
    void LoadPins();
    void SavePins() const;
    FText GetQueueStatusText() const;
    FText GetPrimaryActionText() const;
    FText GetRaidListingActionText() const;
    FText GetSelectedDescription() const;
    FText GetConnectionHint() const;
    EVisibility GetProposalVisibility() const;
    EVisibility GetDungeonStatusVisibility() const;
    EVisibility GetFindGroupVisibility() const;
    EVisibility GetLeaveQueueVisibility() const;
    EVisibility GetTeleportVisibility() const;
    EVisibility GetRaidListVisibility() const;
    EVisibility GetRaidUnlistVisibility() const;

    UWowConnectionManager* ConnectionManager = nullptr;
    TWeakObjectPtr<UWowUIManager> UIManager;
    float Scale = 1.f;
    EPage ActivePage = EPage::DungeonFinder;
    ERaidPage ActiveRaidPage = ERaidPage::List;
    EVisibility CurrentVisibility = EVisibility::Hidden;
    bool bTank = false;
    bool bHealer = false;
    bool bDamage = true;
    bool bLeader = false;
    uint32 SelectedRandomEntry = 0;
    TSet<uint32> SelectedSpecificEntries;
    TSet<uint32> PinnedDungeons;
    uint32 SelectedRaidEntry = 0;
    uint64 SelectedRaidPlayerGuid = 0;

    TSharedPtr<SBox> Root;
    TSharedPtr<SWidget> DungeonPageWidget;
    TSharedPtr<SWidget> RaidPageWidget;
    TSharedPtr<SWidget> RaidListPageWidget;
    TSharedPtr<SWidget> RaidBrowsePageWidget;
    TSharedPtr<SScrollBox> DungeonListBox;
    TSharedPtr<SScrollBox> RaidListingBox;
    TSharedPtr<SScrollBox> RaidResultBox;
    TSharedPtr<SEditableTextBox> DungeonCommentBox;
    TSharedPtr<SEditableTextBox> RaidCommentBox;
    TSharedPtr<FScrollBarStyle> ScrollStyle;
    TArray<TSharedPtr<FButtonStyle>> ButtonStyles;
    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> TextureBrushes;
    mutable TMap<FString, double> FailedTextureTimes;
};
