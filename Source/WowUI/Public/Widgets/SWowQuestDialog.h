#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowEntity.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"

class SScrollBox;
class SButton;
class SHorizontalBox;

DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnQuestDialogAccept, uint64 /*QuestGiverGuid*/, uint32 /*QuestId*/);
DECLARE_DELEGATE_TwoParams(FOnQuestDialogSelect, uint64 /*QuestGiverGuid*/, uint32 /*QuestId*/);
DECLARE_DELEGATE_TwoParams(FOnQuestDialogProgress, uint64 /*QuestGiverGuid*/, uint32 /*QuestId*/);
DECLARE_DELEGATE_TwoParams(FOnQuestDialogContinue, uint64 /*QuestGiverGuid*/, uint32 /*QuestId*/);
DECLARE_DELEGATE_ThreeParams(FOnQuestDialogComplete, uint64 /*QuestGiverGuid*/, uint32 /*QuestId*/, int32 /*RewardChoice*/);
DECLARE_DELEGATE_FourParams(FOnGossipDialogSelect, uint64 /*NpcGuid*/, uint32 /*MenuId*/, uint32 /*OptionId*/, const FString& /*Code*/);
DECLARE_DELEGATE(FOnQuestDialogDecline);

/**
 * WoW-style quest dialog widget
 * Shows quest details with title, description, objectives, and rewards
 * Has Accept/Decline buttons or Complete Quest button depending on mode
 */
class WOWUI_API SWowQuestDialog : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowQuestDialog) {}
        SLATE_EVENT(FOnQuestDialogAccept, OnQuestAccept)
        SLATE_EVENT(FOnQuestDialogSelect, OnQuestSelect)
        SLATE_EVENT(FOnQuestDialogProgress, OnQuestProgress)
        SLATE_EVENT(FOnQuestDialogContinue, OnQuestContinue)
        SLATE_EVENT(FOnQuestDialogComplete, OnQuestComplete)
        SLATE_EVENT(FOnGossipDialogSelect, OnGossipSelect)
        SLATE_EVENT(FOnQuestDialogDecline, OnQuestDecline)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetUIManager(class UWowUIManager* UI);
    void SetConnectionManager(class UWowConnectionManager* InConnection);

    /** Show quest details dialog (for new quest) */
    void ShowQuestDetails(const FWowQuestDetails& QuestDetails);

    /** Show the quests offered/involved by an NPC */
    void ShowQuestList(const FWowGossipMenu& Menu);

    /** Show quest reward dialog (for quest completion) */
    void ShowQuestReward(const FWowQuestDetails& QuestDetails);

    /** Show required items/progress before advancing to rewards. */
    void ShowQuestProgress(const FWowQuestDetails& QuestDetails);

    void UpdateGreeting(uint32 TextId, const FString& Greeting);
    void SetPlayerName(const FString& Name) { PlayerName = Name; }

    /** Hide the quest dialog */
    void CloseDialog();

private:
    FOnQuestDialogAccept OnQuestAccept;
    FOnQuestDialogSelect OnQuestSelect;
    FOnQuestDialogProgress OnQuestProgress;
    FOnQuestDialogContinue OnQuestContinue;
    FOnQuestDialogComplete OnQuestComplete;
    FOnGossipDialogSelect OnGossipSelect;
    FOnQuestDialogDecline OnQuestDecline;

    TSharedPtr<SScrollBox> ContentArea;
    TSharedPtr<SHorizontalBox> ButtonArea;
    TSharedPtr<SHorizontalBox> RewardArea;

    TWeakObjectPtr<class UWowUIManager> UIManager;
    TWeakObjectPtr<class UWowConnectionManager> Connection;
    FString PlayerName;
    FWowQuestDetails CurrentQuest;
    FWowGossipMenu CurrentMenu;
    bool bIsQuestList = false;
    bool bIsProgressDialog = false;
    bool bIsRewardDialog = false;
    int32 SelectedRewardChoice = INDEX_NONE;
    TSharedPtr<FSlateStyleSet> RichTextStyles;
    TSet<FName> RegisteredTextStyles;
    TSharedPtr<class SEditableTextBox> GossipCode;
    TOptional<FWowGossipOption> PendingGossipOption;
    bool bActionPending = false;
    TSharedRef<SWidget> MakeRichText(const FString& Source, const FLinearColor& Color, bool bAnimate = false);
    double NextTextRevealDelay = 0;
    FReply OnConfirmGossip();
    FReply OnCancelGossip();

    FReply OnAcceptClicked();
    FReply OnDeclineClicked();
    FReply OnCompleteClicked();
    FReply OnContinueClicked();
    FReply OnRewardItemClicked(int32 RewardIndex);
    FReply OnQuestListItemClicked(uint32 QuestId, uint32 QuestIcon);
    FReply OnGossipOptionClicked(uint32 OptionId);

    /** Generate quest text content */
    void RefreshContent();
    /** Corner placement like QuestFrame: Accept/Continue left, Decline/Goodbye right. */
    void ArrangeButtons(bool bSingleOnRight);
    // QuestFrame / GossipFrame art (UI-QuestGreeting / UI-QuestDetails pages) and the NPC portrait.
    const FSlateBrush* ArtBrush(const FString& Path) const;
    /** ArtBrush with a texture-coordinate crop (the 32x32 scroll button/knob textures). */
    const FSlateBrush* ArtBrushUV(const FString& Path, const FBox2f& UV) const;
    /** UIPanelScrollBarTemplate look for the text page: knob thumb, no track (the frame art has it). */
    FScrollBarStyle PageScrollStyle;
    TSharedPtr<class SScrollBar> PageScrollBar;
    void ScrollPage(float Direction);
    void EnsurePageScrollKnob();
    const FSlateBrush* PortraitImage() const;
    float GetScale() const;
    FString TitleName() const;
    FSlateFontInfo TitleFontFor(const FString& Text) const;
    const FTextBlockStyle* PanelButtonTextStyle() const;
    mutable FTextBlockStyle ButtonTextStyle;
    mutable TMap<FString, TSharedPtr<class FWowRetainedSlateBrush>> ArtBrushes;
    mutable TSharedPtr<class FWowRetainedSlateBrush> PortraitBrush;
};
