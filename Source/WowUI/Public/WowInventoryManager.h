#pragma once
#include "CoreMinimal.h"
#include "WowEntity.h"

class UWowConnectionManager;

DECLARE_MULTICAST_DELEGATE(FOnInventoryChanged);

// Item slot information
struct FWowItemSlot
{
    uint64 ItemGuid = 0;
    uint32 ItemEntry = 0;
    uint32 StackCount = 0;
    uint8 Quality = 0; // 0=gray, 1=white, 2=green, 3=blue, 4=purple, 5=orange
    uint8 Bag = 255;   // 255 = backpack
    uint8 Slot = 0;

    bool IsEmpty() const { return ItemGuid == 0; }
};

// WoW inventory manager for tracking player items
class WOWUI_API FWowInventoryManager
{
public:
    FWowInventoryManager();

    /** Update inventory from player entity fields */
    void UpdateFromPlayerEntity(const FWowPlayerEntity& Player, class FWowEntityManager& EntityManager);

    UWowConnectionManager* GetConnectionManager() const { return ConnectionManager.Get(); }

    /** Get item in backpack slot (0-15) */
    const FWowItemSlot* GetBackpackItem(uint8 Slot) const;

    /** Get equipped item by slot */
    const FWowItemSlot* GetEquippedItem(uint8 Slot) const;

    /** Get all backpack items */
    const TArray<FWowItemSlot>& GetBackpackItems() const { return BackpackItems; }

    /** Get bank base slot item by slot (0-27) */
    const FWowItemSlot* GetBankItem(uint8 Slot) const;

    /** Get bank bag slot item by slot (0-6) */
    const FWowItemSlot* GetBankBagSlot(uint8 Slot) const;

    /** Get all bank base slot items */
    const TArray<FWowItemSlot>& GetBankItems() const { return BankItems; }

    /** Get all bank bag slot items */
    const TArray<FWowItemSlot>& GetBankBagSlots() const { return BankBagSlots; }

    /** Get all equipped items */
    const TArray<FWowItemSlot>& GetEquippedItems() const { return EquippedItems; }

    /** Number of purchased bank bag slots */
    int32 GetPurchasedBankBagSlots() const { return PurchasedBankBagSlots; }

    /** Provide the live connection manager so inventory actions can send packets. */
    void SetConnectionManager(UWowConnectionManager* InConnectionManager);

    /** Send autoequip item packet */
    void AutoEquipItem(uint8 SrcBag, uint8 SrcSlot);

    /** Send auto-store item packet (used for unequipping into a bag). */
    void AutoStoreItem(uint8 SrcBag, uint8 SrcSlot, uint8 DestBag = 255);

    /** Equipment slot constants */
    static constexpr uint8 EQUIP_SLOT_HEAD = 0;
    static constexpr uint8 EQUIP_SLOT_NECK = 1;
    static constexpr uint8 EQUIP_SLOT_SHOULDERS = 2;
    static constexpr uint8 EQUIP_SLOT_SHIRT = 3;
    static constexpr uint8 EQUIP_SLOT_CHEST = 4;
    static constexpr uint8 EQUIP_SLOT_WAIST = 5;
    static constexpr uint8 EQUIP_SLOT_LEGS = 6;
    static constexpr uint8 EQUIP_SLOT_FEET = 7;
    static constexpr uint8 EQUIP_SLOT_WRISTS = 8;
    static constexpr uint8 EQUIP_SLOT_HANDS = 9;
    static constexpr uint8 EQUIP_SLOT_FINGER1 = 10;
    static constexpr uint8 EQUIP_SLOT_FINGER2 = 11;
    static constexpr uint8 EQUIP_SLOT_TRINKET1 = 12;
    static constexpr uint8 EQUIP_SLOT_TRINKET2 = 13;
    static constexpr uint8 EQUIP_SLOT_BACK = 14;
    static constexpr uint8 EQUIP_SLOT_MAINHAND = 15;
    static constexpr uint8 EQUIP_SLOT_OFFHAND = 16;
    static constexpr uint8 EQUIP_SLOT_RANGED = 17;
    static constexpr uint8 EQUIP_SLOT_TABARD = 18;
    static constexpr uint8 EQUIP_SLOT_COUNT = 19;

    /** Get slot name for display */
    static const TCHAR* GetSlotName(uint8 SlotIndex);

    /** Get item quality color */
    static FLinearColor GetQualityColor(uint8 Quality);

    /** Event fired when inventory changes */
    FOnInventoryChanged OnInventoryChanged;

private:
    TArray<FWowItemSlot> BackpackItems; // 16 slots
    TArray<FWowItemSlot> BankItems; // 28 base bank slots
    TArray<FWowItemSlot> BankBagSlots; // 7 purchasable bank bag slots
    TArray<FWowItemSlot> EquippedItems; // 19 equipment slots
    int32 PurchasedBankBagSlots = 0;
    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;

    void UpdateItemSlot(FWowItemSlot& Slot, uint64 ItemGuid, class FWowEntityManager& EntityManager, uint8 Bag, uint8 SlotIndex);
};
