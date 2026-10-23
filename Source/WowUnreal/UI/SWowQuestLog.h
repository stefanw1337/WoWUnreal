#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FWowQuestLogEntry;
class FWowPacketHandler;
class FWowRetainedSlateBrush;

/**
 * Quest log window, laid out like the 3.3.5 QuestLogFrame: book icon and
 * "Quests: n/25" top left, Show Map top right, quest list on the left, a
 * parchment page with title, objectives, description and rewards on the right,
 * Abandon / Share / Track bottom left and Close bottom right.
 */
class WOWUNREAL_API SWowQuestLog : public SCompoundWidget
{
public:
    /** QuestLogFrame size in WoW units (UI-QuestLogDualPane art); Slate size = this x UI scale. */
    static constexpr float FrameW = 682.f;
    static constexpr float FrameH = 447.f;

    SLATE_BEGIN_ARGS(SWowQuestLog)
        : _OnTrackedQuestsChanged()
    {}
        SLATE_EVENT(FSimpleDelegate, OnTrackedQuestsChanged)
        SLATE_EVENT(FSimpleDelegate, OnShowMap)
        SLATE_ARGUMENT(TWeakObjectPtr<class UWowUIManager>, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, FWowPacketHandler* InPacketHandler);

    /** Update the quest list from the packet handler */
    void UpdateQuestList();

    /** Toggle visibility of the quest log window */
    void ToggleVisibility();

    /** Check if quest log is visible */
    bool IsVisible() const;

    /** Open the log with this quest selected (left-click in the objectives tracker). */
    void ShowQuest(uint32 QuestId);

    /** Quest selected in the log (0 = none); "Show Map" opens the map on it. */
    uint32 GetSelectedQuestId() const;

    /** The "Abandon <quest>?" popup's frame while it is shown (the controller treats clicks there as UI). */
    TSharedPtr<SWidget> GetAbandonPopupFrame() const { return AbandonPopup.IsValid() ? AbandonPopupFrame : nullptr; }
    /** Esc: closes the abandon popup; true when it was open. */
    bool CancelAbandonPopup();

    /** Frame size in Slate units at the current UI scale. */
    FVector2D GetPanelSize() const { return FVector2D(FrameW, FrameH) * Scale; }

    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

    /** GetQuestDifficultyColor() of the 3.3.5 UI (quest log titles and the objectives tracker). */
    static FLinearColor DifficultyColor(int32 QuestLevel, int32 PlayerLevel);
    /** "[12] Title" when Interface > Quests > Show Quest Level is on. */
    static FString TitleWithLevel(const FString& Title, int32 QuestLevel);

    /** Replace $N/$C/$R/$G/$B tokens the way the original client does. */
    static FString ExpandQuestText(const FString& Text, const FString& PlayerName, const FString& RaceName,
        const FString& ClassName, bool bFemale);

private:
    TSharedRef<SWidget> CreateQuestDetailPanel();
    TSharedRef<SWidget> CreateQuestEntry(const FWowQuestLogEntry& QuestEntry, int32 QuestIndex) const;
    TSharedRef<SWidget> CreateZoneHeader(const FString& ZoneName, int32 QuestCount) const;
    TSharedRef<SWidget> CreateObjectivesWidget(const FWowQuestLogEntry& QuestEntry) const;
    TSharedRef<SWidget> CreateRewardsWidget(const FWowQuestLogEntry& QuestEntry);
    TSharedRef<SWidget> CreateBottomBar();
    /** Lay the frame out on a canvas in WoW units (positions from QuestLogFrame.xml / .lua). */
    void Rebuild();
    FSlateFontInfo Fnt(float WowPixels) const;
    void Place(class SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    TSharedRef<SWidget> PanelButton(const FText& Label, float W, float H, FOnClicked OnClicked, TAttribute<bool> Enabled = true) const;
    /** HybridScrollBar / UIPanelScrollBar look: up/down arrow buttons and the knob, track left to the art. */
    void PlaceScrollBar(class SCanvas& Canvas, float X, float Top, float Bottom, TSharedRef<class SScrollBar> Bar,
        TSharedPtr<class SScrollBox> Box) const;

    FString GetQuestName(const FWowQuestLogEntry& QuestEntry) const;
    FString GetQuestZoneName(const FWowQuestLogEntry& QuestEntry) const;
    FString ExpandForPlayer(const FString& Text) const;
    FLinearColor GetQuestDifficultyColor(int32 QuestLevel) const;
    int32 GetPlayerLevel() const;
    const FWowQuestLogEntry* GetSelectedQuest() const;
    int32 FindQuestLogSlot(uint32 QuestId) const;
    /** Original UI texture from the user's data (cached; failures retried every 2 s). */
    const struct FSlateBrush* LoadBrush(const FString& TexturePath, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1)),
        FSlateColor Tint = FSlateColor(FLinearColor::White)) const;
    TSharedRef<SWidget> CreateRewardCard(uint32 ItemEntry, uint32 Count) const;
    TSharedRef<SWidget> CreateMoneyWidget(int32 Copper) const;

    FReply OnCloseButtonClicked();
    FReply OnQuestSelected(int32 QuestIndex);
    FReply OnZoneHeaderClicked(FString ZoneName);
    void ToggleQuestWatch(int32 QuestIndex);
    FReply OnAbandonClicked();
    FReply OnAbandonConfirmed();
    void ShowAbandonPopup(uint32 QuestId);
    void HideAbandonPopup();
    FReply OnShareClicked();
    FReply OnTrackClicked();

    TWeakObjectPtr<class UWowUIManager> UIManager;
    mutable FWowPacketHandler* PacketHandler = nullptr;
    TSharedPtr<class SScrollBox> QuestListScrollBox;
    TSharedPtr<class SScrollBox> QuestDetailScrollBox;
    TSharedPtr<class SBox> Root;
    TSharedPtr<class SBox> CountBox;
    TSharedPtr<struct FScrollBarStyle> ScrollStyle;
    float Scale = 1.25f;
    TSharedPtr<class SVerticalBox> QuestDetailBox;
    TSharedPtr<class SBox> BottomBarBox;
    TSharedPtr<class STextBlock> QuestCountText;
    TSet<FString> CollapsedZones;
    FSimpleDelegate OnTrackedQuestsChanged;
    FSimpleDelegate OnShowMap;
    int32 SelectedQuestIndex = INDEX_NONE;
    uint32 PendingAbandonQuestId = 0;
    TSharedPtr<SWidget> AbandonPopup;
    TSharedPtr<SWidget> AbandonPopupFrame;
    EVisibility CurrentVisibility = EVisibility::Hidden;
    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> TextureBrushes;
    mutable TMap<FString, double> FailedTextureTimes;
};
