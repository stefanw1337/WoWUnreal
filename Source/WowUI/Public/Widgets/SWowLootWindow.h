#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowEntity.h"

class SButton;
class SVerticalBox;

DECLARE_DELEGATE_OneParam(FOnLootItem, int32 /*LootSlot*/);
DECLARE_DELEGATE(FOnLootAll);
DECLARE_DELEGATE(FOnCloseLoot);

/**
 * WoW-style loot window widget
 * Compact item list; click an item to loot, shift-click to loot all
 */
class WOWUI_API SWowLootWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowLootWindow) {}
        SLATE_EVENT(FOnLootItem, OnLootItem)
        SLATE_EVENT(FOnLootAll, OnLootAll)
        SLATE_EVENT(FOnCloseLoot, OnCloseLoot)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetContext(class UWowConnectionManager* InConnection, class UWowUIManager* UI);
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override;

    /** Update the loot window with new data */
    void UpdateLoot(uint64 LootGuid, uint8 LootType, uint32 Gold, const TArray<FWowLootItem>& Items);

    /** Hide the loot window */
    void CloseLoot();
    /** Local point inside the first lootable item (or money when there are no items). */
    FVector2D GetFirstLootAnchor() const;
    /** Laid-out position of the first slot (absolute), once the window has been arranged. */
    bool GetFirstLootAbsoluteAnchor(FVector2D& OutAbsolute) const;

private:
    FOnLootItem OnLootItem;
    FOnLootAll OnLootAll;
    FOnCloseLoot OnCloseLoot;

    TSharedPtr<SVerticalBox> ItemList;
    TSharedPtr<SButton> FirstLootButton;
    TSharedPtr<SButton> GoldButton;
    TSharedPtr<class SScrollBox> ItemScrollBox;
    TWeakObjectPtr<class UWowConnectionManager> Connection;
    TWeakObjectPtr<class UWowUIManager> UIManager;
    TMap<FString, TSharedPtr<FSlateBrush>> IconBrushes;
    uint32 LastRevision = 0;
    const FSlateBrush* Icon(uint32 Entry, uint32 DisplayId);
    const FSlateBrush* IconByPath(const FString& TexturePath);
    TSharedRef<SWidget> MakeSlot(const FSlateBrush* IconBrush, const FText& Name, const FLinearColor& NameColor,
        uint32 Count, bool bEnabled, TSharedPtr<SButton>& OutButton, FOnClicked OnClicked, TSharedPtr<class IToolTip> ToolTip);
    void RefreshItems();

    uint64 CurrentLootGuid = 0;
    uint32 CurrentGold = 0;
    TArray<FWowLootItem> CurrentItems;

    FReply OnLootAllClicked();
    FReply OnCloseClicked();
    FReply OnLootItemClicked(int32 LootSlot);

    /** Generate a WoW-style gold display text (123g 45s 67c) */
    FText GetGoldText(uint32 Gold) const;
};