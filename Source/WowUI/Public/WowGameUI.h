#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WowEntity.h"
#include "WowGameUI.generated.h"

class SWidget;
class SWowLootWindow;
class SWowVendorWindow;
class SWowQuestDialog;
class SWowTradeWindow;
class UWowConnectionManager;

/**
 * Game UI manager that handles WoW-style UI windows
 * Integrates with the connection manager for packet sending
 */
UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class WOWUI_API UWowGameUI : public UActorComponent
{
    GENERATED_BODY()

public:
    UWowGameUI();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    bool CloseNpcWindows();
    /** Close: how to close the window when a newer one needs its place (null: never closed). */
    void AddManagedPanel(FName Name,TSharedRef<SWidget> Widget,FVector2D Size,TFunction<void()> Close=nullptr);
    /** Transparent margin of a panel's art that must not take the mouse, as fractions of its size
     *  (FrameXML hitRectInsets): the 384x512 frames have empty space right and below the art. */
    void SetPanelHitInsets(FName Name,const FMargin& FractionOfSize);
    TFunction<bool()> CanMoveFrames;
    /** Ctrl-drag works from the start (the input hook was only installed with the first loot/quest/vendor window). */
    void EnableFrameMoving() { RegisterFrameMoveInput(); }
    /** A native (non-FrameXML) element Ctrl-drag can move, e.g. the objectives tracker. Its offset
     *  is saved with the frame layout and applied as a render translation. */
    void AddMovableWidget(FName Name, TSharedRef<SWidget> Widget);
    void ApplyMovableWidgets();
    bool IsMovingFrame() const { return bFrameMoveOwned; }
    bool BeginFrameMove(const FVector2D& ScreenPosition);
    void UpdateFrameMove(const FVector2D& ScreenPosition);
    void EndFrameMove();

    /** Set the connection manager for sending packets */
    UFUNCTION(BlueprintCallable)
    void SetConnectionManager(UWowConnectionManager* InConnectionManager);

    /** Show the loot window with the given data */
    void ShowLootWindow(uint64 LootGuid, uint8 LootType, uint32 Gold, const TArray<FWowLootItem>& Items);
    void PrepareLootRequest(uint64 LootGuid);

    /** Hide the loot window */
    void HideLootWindow();

    void RemoveLootSlot(uint8 Slot);
    void ClearLootMoney();

    /** Show the vendor window with the given data */
    void ShowVendorWindow(uint64 VendorGuid, const TArray<FWowVendorItem>& Items);

    /** Update vendor window with player inventory for sell functionality */
    void UpdateVendorPlayerInventory(const TArray<FWowItem>& Items);

    /** Hide the vendor window */
    void HideVendorWindow();

    /** Show quest details dialog */
    void ShowQuestDialog(const FWowQuestDetails& QuestDetails);

    /** Show the quest choices returned by an NPC gossip/quest-list packet */
    void ShowQuestList(const FWowGossipMenu& Menu);

    void UpdateGossipGreeting(uint32 TextId, const FString& Greeting);

    /** Show quest reward dialog */
    void ShowQuestRewardDialog(const FWowQuestDetails& QuestDetails);

    /** Show quest requirements and a Continue action when ready. */
    void ShowQuestProgressDialog(const FWowQuestDetails& QuestDetails);

    /** Hide quest dialog */
    void HideQuestDialog();

    /** Whether the gossip/quest dialog is open. It does not block world input. */
    bool IsQuestDialogVisible() const;
    bool IsNpcInteractionInRange(uint64 Guid) const;

    /** Hit-test native NPC windows in Slate desktop coordinates, including viewport DPI. */
    bool IsPointerOverWindow(const FVector2D& ScreenPosition) const;

    /** Show or refresh the trade window */
    void ShowTradeWindow(const FWowTradeState& TradeState);

    /** Hide the trade window */
    void HideTradeWindow();

protected:
    UPROPERTY()
    TObjectPtr<UWowConnectionManager> ConnectionManager;

private:
    TSharedPtr<class IInputProcessor> FrameMoveInput;
    bool bFrameMoveOwned=false;
    FString MovingFrameName;
    FVector2D MoveStartCursor=FVector2D::ZeroVector, MoveStartPosition=FVector2D::ZeroVector, MoveSize=FVector2D::ZeroVector;
    class FWowFrameManager* GetFrameManager() const;
    void RegisterFrameMoveInput();
    void UnregisterFrameMoveInput();
    uint64 CurrentVendorGuid = 0;
    uint64 CurrentQuestNpcGuid = 0;
    struct FManagedPanel
    {
        TWeakPtr<SWidget> Widget;
        FVector2D Size;
        FVector2D Position=FVector2D(16,100);
        float Scale=1;
        TFunction<void()> Close;
        FMargin HitInsets=FMargin(0);
    };
    TMap<FName,FManagedPanel> ManagedPanels;
    TArray<FName> PanelOrder;
    TArray<TSharedPtr<SWidget>> ViewportContainers;
    void ReflowPanels();
    TMap<FName,TWeakPtr<SWidget>> MovableWidgets;
    bool bMovingNativeWidget=false;
    FVector2D MoveStartTranslation=FVector2D::ZeroVector;
    uint64 CurrentLootGuid = 0;
    uint64 PreparedLootGuid = 0;
    bool bPreparedAutoLoot = false;
    bool bLootAtMouse = false;
    FVector2D PreparedLootCursor = FVector2D::ZeroVector;
    FVector2D LootWindowPosition = FVector2D::ZeroVector;
    /** Cursor at open (absolute): the arranged first slot is moved exactly under it next tick. */
    FVector2D LootOpenCursor = FVector2D::ZeroVector;
    int32 LootAnchorFixFrames = 0;
    bool CloseLootIfEmpty();
    uint8 CurrentLootType = 0;
    uint32 CurrentLootMoney = 0;
    TArray<FWowLootItem> CurrentLootItems;

    // Widget instances
    TSharedPtr<SWowLootWindow> LootWindow;
    TSharedPtr<SWowVendorWindow> VendorWindow;
    TSharedPtr<SWowQuestDialog> QuestDialog;
    TSharedPtr<SWowTradeWindow> TradeWindow;

    // Widget callbacks
    void OnLootItem(int32 LootSlot);
    void OnLootAll();
    void OnCloseLoot();
    void OnBuyItem(uint64 VendorGuid, uint32 ItemId, int32 Count, int32 VendorSlot);
    void OnSellItem(uint64 VendorGuid, uint64 ItemGuid, uint8 Count);
    void OnCloseVendor();
    bool OnQuestAccept(uint64 QuestGiverGuid, uint32 QuestId);
    void OnQuestSelect(uint64 QuestGiverGuid, uint32 QuestId);
    void OnQuestProgress(uint64 QuestGiverGuid, uint32 QuestId);
    void OnQuestContinue(uint64 QuestGiverGuid, uint32 QuestId);
    void OnQuestComplete(uint64 QuestGiverGuid, uint32 QuestId, int32 RewardChoice);
    void OnGossipSelect(uint64 NpcGuid, uint32 MenuId, uint32 OptionId, const FString& Code);
    void OnQuestDecline();
    /** The conversation the quest dialog shows, for the Lua API (FWowNpcDialogState), and its event
        (GOSSIP_SHOW, QUEST_GREETING, QUEST_DETAIL, QUEST_PROGRESS, QUEST_COMPLETE; GOSSIP_CLOSED /
        QUEST_FINISHED for Mode 0). Mode is FWowNpcDialogState::EMode. */
    void PublishNpcDialog(uint8 Mode, const FWowGossipMenu* Menu, const FWowQuestDetails* Quest);
    void OnAcceptTrade(bool bBeginTrade);
    void OnCancelTrade();

    // UI integration
    void CreateWidgets();
    void AddWidgetsToViewport();
    void RemoveWidgetsFromViewport();

    bool bWidgetsCreated = false;
};
