#include "WowInventoryManager.h"
#include "WowColor.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"

FWowInventoryManager::FWowInventoryManager()
{
    BackpackItems.SetNum(16); // Backpack has 16 slots
    BankItems.SetNum(28); // Bank has 28 base slots
    BankBagSlots.SetNum(7); // Up to 7 purchased bank bag slots
    EquippedItems.SetNum(EQUIP_SLOT_COUNT); // 19 equipment slots
}

void FWowInventoryManager::UpdateFromPlayerEntity(const FWowPlayerEntity& Player, FWowEntityManager& EntityManager)
{
    // Equipment and worn bags live in the player's inventory block.
    for (uint8 SlotIndex = 0; SlotIndex < EQUIP_SLOT_COUNT; ++SlotIndex)
    {
        UpdateItemSlot(
            EquippedItems[SlotIndex],
            Player.GetEquipmentItemGuid(SlotIndex),
            EntityManager,
            255,
            SlotIndex);
    }

    for (uint8 SlotIndex = 0; SlotIndex < 16; ++SlotIndex)
    {
        // AzerothCore uses bag 255 and slots 23-38 for the backpack.
        UpdateItemSlot(
            BackpackItems[SlotIndex],
            Player.GetBackpackItemGuid(SlotIndex),
            EntityManager,
            255,
            static_cast<uint8>(23 + SlotIndex));
    }

    for (uint8 SlotIndex = 0; SlotIndex < 28; ++SlotIndex)
    {
        // Bank base slots are bag 255, slots 39-66.
        UpdateItemSlot(
            BankItems[SlotIndex],
            Player.GetBankItemGuid(SlotIndex),
            EntityManager,
            255,
            static_cast<uint8>(39 + SlotIndex));
    }

    for (uint8 SlotIndex = 0; SlotIndex < 7; ++SlotIndex)
    {
        // Bank bag slots are bag 255, slots 67-73.
        UpdateItemSlot(
            BankBagSlots[SlotIndex],
            Player.GetBankBagGuid(SlotIndex),
            EntityManager,
            255,
            static_cast<uint8>(67 + SlotIndex));
    }

    PurchasedBankBagSlots = Player.GetBankBagSlotCount();
    OnInventoryChanged.Broadcast();
}

const FWowItemSlot* FWowInventoryManager::GetBackpackItem(uint8 Slot) const
{
    return (Slot < 16) ? &BackpackItems[Slot] : nullptr;
}

const FWowItemSlot* FWowInventoryManager::GetBankItem(uint8 Slot) const
{
    return (Slot < 28) ? &BankItems[Slot] : nullptr;
}

const FWowItemSlot* FWowInventoryManager::GetBankBagSlot(uint8 Slot) const
{
    return (Slot < 7) ? &BankBagSlots[Slot] : nullptr;
}

const FWowItemSlot* FWowInventoryManager::GetEquippedItem(uint8 Slot) const
{
    return (Slot < EQUIP_SLOT_COUNT) ? &EquippedItems[Slot] : nullptr;
}

void FWowInventoryManager::SetConnectionManager(UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
}

void FWowInventoryManager::AutoEquipItem(uint8 SrcBag, uint8 SrcSlot)
{
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
    {
        Connection->SendAutoEquipItem(SrcBag, SrcSlot);
    }
}

void FWowInventoryManager::AutoStoreItem(uint8 SrcBag, uint8 SrcSlot, uint8 DestBag)
{
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
    {
        Connection->SendAutoStoreBagItem(SrcBag, SrcSlot, DestBag);
    }
}

const TCHAR* FWowInventoryManager::GetSlotName(uint8 SlotIndex)
{
    switch (SlotIndex)
    {
        case EQUIP_SLOT_HEAD: return TEXT("Head");
        case EQUIP_SLOT_NECK: return TEXT("Neck");
        case EQUIP_SLOT_SHOULDERS: return TEXT("Shoulders");
        case EQUIP_SLOT_SHIRT: return TEXT("Shirt");
        case EQUIP_SLOT_CHEST: return TEXT("Chest");
        case EQUIP_SLOT_WAIST: return TEXT("Waist");
        case EQUIP_SLOT_LEGS: return TEXT("Legs");
        case EQUIP_SLOT_FEET: return TEXT("Feet");
        case EQUIP_SLOT_WRISTS: return TEXT("Wrists");
        case EQUIP_SLOT_HANDS: return TEXT("Hands");
        case EQUIP_SLOT_FINGER1: return TEXT("Finger 1");
        case EQUIP_SLOT_FINGER2: return TEXT("Finger 2");
        case EQUIP_SLOT_TRINKET1: return TEXT("Trinket 1");
        case EQUIP_SLOT_TRINKET2: return TEXT("Trinket 2");
        case EQUIP_SLOT_BACK: return TEXT("Back");
        case EQUIP_SLOT_MAINHAND: return TEXT("Main Hand");
        case EQUIP_SLOT_OFFHAND: return TEXT("Off Hand");
        case EQUIP_SLOT_RANGED: return TEXT("Ranged");
        case EQUIP_SLOT_TABARD: return TEXT("Tabard");
        default: return TEXT("Unknown");
    }
}

FLinearColor FWowInventoryManager::GetQualityColor(uint8 Quality)
{
    switch (Quality)
    {
        // ITEM_QUALITY_COLORS from 3.3.5 (9d9d9d, ffffff, 1eff00, 0070dd, a335ee, ff8000, e6cc80).
        case 0: return WowColor::FromWow(0.62f, 0.62f, 0.62f); // Poor
        case 1: return FLinearColor::White;               // Common
        case 2: return WowColor::FromWow(0.12f, 1.0f, 0.0f);   // Uncommon
        case 3: return WowColor::FromWow(0.0f, 0.44f, 0.87f);  // Rare
        case 4: return WowColor::FromWow(0.64f, 0.21f, 0.93f); // Epic
        case 5: return WowColor::FromWow(1.0f, 0.5f, 0.0f);    // Legendary
        case 6: // Artifact
        case 7: return WowColor::FromWow(0.90f, 0.80f, 0.50f); // Heirloom
        default: return WowColor::FromWow(0.62f, 0.62f, 0.62f);
    }
}

void FWowInventoryManager::UpdateItemSlot(FWowItemSlot& Slot, uint64 ItemGuid, FWowEntityManager& EntityManager, uint8 Bag, uint8 SlotIndex)
{
    Slot.ItemGuid = ItemGuid;
    Slot.Bag = Bag;
    Slot.Slot = SlotIndex;

    if (ItemGuid == 0)
    {
        // Empty slot
        Slot.ItemEntry = 0;
        Slot.StackCount = 0;
        Slot.Quality = 0;
    }
    else
    {
        FWowItemEntity* Item = EntityManager.FindItem(ItemGuid);
        Slot.ItemEntry = Item ? Item->GetField(ObjectField::ENTRY) : 0;
        Slot.StackCount = Item ? FMath::Max(Item->GetStackCount(), 1) : 1;

        // Quality is not part of update fields in this client yet, so keep a stable visible default.
        Slot.Quality = Slot.ItemEntry != 0 ? 1 : 0;

        if (Slot.ItemEntry == 0)
        {
            // Fall back to a deterministic placeholder so the UI still surfaces the slot content.
            Slot.ItemEntry = static_cast<uint32>(ItemGuid & 0xFFFFFFFFu);
        }
    }
}
