#pragma once
#include "CoreMinimal.h"
#include "WowEntity.h"
class UWowConnectionManager;
class SToolTip;
class SWidget;
class UWowUIManager;
// One tooltip line; Right is the right-aligned half of a double line (slot / armor type, speed).
struct FWowItemTooltipLine { FString Text; FLinearColor Color = FLinearColor::White; FString Right; FLinearColor RightColor = FLinearColor::White; };
struct WOWUI_API FWowItemPresentation
{
    static const FWowItemTemplateInfo* Info(UWowConnectionManager* Connection, uint32 Entry);
    static FString Name(UWowConnectionManager* Connection, uint32 Entry);
    static FString IconPath(uint32 Entry, uint32 DisplayId = 0);
    // Durability/MaxDurability of the item instance when known (-1 = show the template maximum).
    // Instance: the item object when known (random "of the X" suffix name and stats, enchant slots 7-11).
    static TArray<FWowItemTooltipLine> Lines(UWowConnectionManager* Connection, uint32 Entry, int32 Durability = -1, int32 MaxDurability = -1,
        const FWowItemEntity* Instance = nullptr);
    // "Currently Equipped" tooltip for EquippedEntry plus the stat changes NewEntry would bring.
    static TArray<FWowItemTooltipLine> CompareLines(UWowConnectionManager* Connection, uint32 NewEntry, uint32 EquippedEntry);
    // Equipment slots (FWowInventoryManager::EQUIP_SLOT_*) an inventory type is compared against.
    static TArray<uint8> CompareSlots(uint32 InventoryType);
    // bCompare: Shift shows the equipped item(s) beside it (off for items that are equipped).
    // ExtraLines: shown before the sell price (e.g. the enchants and gems of an inspected item).
    static TSharedRef<SToolTip> Tooltip(UWowConnectionManager* Connection, uint32 Entry, bool bCompare = true,
        const TArray<FWowItemTooltipLine>& ExtraLines = TArray<FWowItemTooltipLine>());
    static TSharedRef<SWidget> ItemCard(UWowConnectionManager* Connection, UWowUIManager* UI, uint32 Entry, uint32 Count = 1, uint32 DisplayId = 0);
    static FLinearColor QualityColor(UWowConnectionManager* Connection, uint32 Entry);
    static FString TypeLabel(const FWowItemTemplateInfo& Item);
    static FString Money(uint32 Copper);
};
