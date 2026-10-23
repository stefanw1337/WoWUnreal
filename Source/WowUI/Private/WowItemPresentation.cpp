#include "WowItemPresentation.h"
#include "WowColor.h"
#include "WowClientPreferences.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowRetainedSlateBrush.h"
#include "Engine/Texture2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SCompoundWidget.h"
#include "WowSpellPresentation.h"
#include "WowConnectionManager.h"
#include "WowInventoryManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"
#include "WowWindowStyle.h"
#include "Brushes/SlateNoResource.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"

const FWowItemTemplateInfo* FWowItemPresentation::Info(UWowConnectionManager* Connection, uint32 Entry)
{
    if (!Connection) return nullptr;
    Connection->RequestItemInfo(Entry);
    return Connection->PacketHandler.ItemTemplates.Find(Entry);
}
FString FWowItemPresentation::Name(UWowConnectionManager* Connection, uint32 Entry)
{
    const FWowItemTemplateInfo* Item = Info(Connection, Entry);
    return Item ? Item->Name : TEXT("Loading item...");
}
FString FWowItemPresentation::IconPath(uint32 Entry, uint32 DisplayId)
{
    if (!DisplayId)
        if (const auto* Record = FDbcStore::Get().Items().GetById(Entry)) DisplayId = Record->DisplayInfoID;
    if (const auto* Display = FDbcStore::Get().ItemDisplayInfo().GetById(DisplayId))
    {
        if (!Display->Icon1.IsEmpty()) return TEXT("Interface\\Icons\\") + Display->Icon1;
    }
    return TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
}
FString FWowItemPresentation::Money(uint32 Copper)
{
    return FString::Printf(TEXT("%ug %us %uc"), Copper / 10000, Copper / 100 % 100, Copper % 100);
}
namespace
{
// 3.3.5 GlobalStrings for item stat types (ITEM_MOD_*); primary stats are "+N Name" lines,
// the rest are green "Equip: ..." lines.
struct FItemStatText { const TCHAR* Line; const TCHAR* Short; bool bPrimary; };
const FItemStatText* ItemStatText(uint32 Type)
{
    static const TMap<uint32, FItemStatText> Texts = {
        {0,{TEXT("Mana"),TEXT("Mana"),true}},{1,{TEXT("Health"),TEXT("Health"),true}},
        {3,{TEXT("Agility"),TEXT("Agility"),true}},{4,{TEXT("Strength"),TEXT("Strength"),true}},
        {5,{TEXT("Intellect"),TEXT("Intellect"),true}},{6,{TEXT("Spirit"),TEXT("Spirit"),true}},
        {7,{TEXT("Stamina"),TEXT("Stamina"),true}},
        {12,{TEXT("Increases defense rating by %d."),TEXT("Defense Rating"),false}},
        {13,{TEXT("Increases your dodge rating by %d."),TEXT("Dodge Rating"),false}},
        {14,{TEXT("Increases your parry rating by %d."),TEXT("Parry Rating"),false}},
        {15,{TEXT("Increases your shield block rating by %d."),TEXT("Block Rating"),false}},
        {16,{TEXT("Improves melee hit rating by %d."),TEXT("Hit Rating (Melee)"),false}},
        {17,{TEXT("Improves ranged hit rating by %d."),TEXT("Hit Rating (Ranged)"),false}},
        {18,{TEXT("Improves spell hit rating by %d."),TEXT("Hit Rating (Spell)"),false}},
        {19,{TEXT("Improves melee critical strike rating by %d."),TEXT("Critical Strike Rating (Melee)"),false}},
        {20,{TEXT("Improves ranged critical strike rating by %d."),TEXT("Critical Strike Rating (Ranged)"),false}},
        {21,{TEXT("Improves spell critical strike rating by %d."),TEXT("Critical Strike Rating (Spell)"),false}},
        {22,{TEXT("Improves melee hit avoidance rating by %d."),TEXT("Hit Avoidance Rating (Melee)"),false}},
        {23,{TEXT("Improves ranged hit avoidance rating by %d."),TEXT("Hit Avoidance Rating (Ranged)"),false}},
        {24,{TEXT("Improves spell hit avoidance rating by %d."),TEXT("Hit Avoidance Rating (Spell)"),false}},
        {25,{TEXT("Improves melee critical avoidance rating by %d."),TEXT("Critical Strike Avoidance Rating (Melee)"),false}},
        {26,{TEXT("Improves ranged critical avoidance rating by %d."),TEXT("Critical Strike Avoidance Rating (Ranged)"),false}},
        {27,{TEXT("Improves spell critical avoidance rating by %d."),TEXT("Critical Strike Avoidance Rating (Spell)"),false}},
        {28,{TEXT("Improves melee haste rating by %d."),TEXT("Haste Rating (Melee)"),false}},
        {29,{TEXT("Improves ranged haste rating by %d."),TEXT("Haste Rating (Ranged)"),false}},
        {30,{TEXT("Improves spell haste rating by %d."),TEXT("Haste Rating (Spell)"),false}},
        {31,{TEXT("Improves hit rating by %d."),TEXT("Hit Rating"),false}},
        {32,{TEXT("Improves critical strike rating by %d."),TEXT("Critical Strike Rating"),false}},
        {33,{TEXT("Improves hit avoidance rating by %d."),TEXT("Hit Avoidance Rating"),false}},
        {34,{TEXT("Improves critical avoidance rating by %d."),TEXT("Critical Strike Avoidance Rating"),false}},
        {35,{TEXT("Improves your resilience rating by %d."),TEXT("Resilience Rating"),false}},
        {36,{TEXT("Improves haste rating by %d."),TEXT("Haste Rating"),false}},
        {37,{TEXT("Increases your expertise rating by %d."),TEXT("Expertise Rating"),false}},
        {38,{TEXT("Increases attack power by %d."),TEXT("Attack Power"),false}},
        {39,{TEXT("Increases ranged attack power by %d."),TEXT("Ranged Attack Power"),false}},
        {40,{TEXT("Increases attack power by %d in Cat, Bear, Dire Bear, and Moonkin forms only."),TEXT("Attack Power In Forms"),false}},
        {41,{TEXT("Increases healing done by magical spells and effects by up to %d."),TEXT("Bonus Healing"),false}},
        {42,{TEXT("Increases damage done by magical spells and effects by up to %d."),TEXT("Bonus Damage"),false}},
        {43,{TEXT("Restores %d mana per 5 sec."),TEXT("Mana Regeneration"),false}},
        {44,{TEXT("Increases your armor penetration rating by %d."),TEXT("Armor Penetration Rating"),false}},
        {45,{TEXT("Increases spell power by %d."),TEXT("Spell Power"),false}},
        {46,{TEXT("Restores %d health per 5 sec."),TEXT("Health Regeneration"),false}},
        {47,{TEXT("Increases spell penetration by %d."),TEXT("Spell Penetration"),false}},
        {48,{TEXT("Increases the block value of your shield by %d."),TEXT("Block Value"),false}}};
    return Texts.Find(Type);
}
const TCHAR* ItemSchoolName(int32 Index)
{
    static const TCHAR* Schools[] = {TEXT("Holy"),TEXT("Fire"),TEXT("Nature"),TEXT("Frost"),TEXT("Shadow"),TEXT("Arcane")};
    return Index >= 0 && Index < 6 ? Schools[Index] : TEXT("");
}
const FLinearColor ItemTipGreen = WowColor::FromWow(.12f, 1.f, 0.f);
const FLinearColor ItemTipGold = WowColor::FromWow(1.f, .82f, 0.f);
const FLinearColor ItemTipRed = WowColor::FromWow(1.f, .125f, .125f);
const FLinearColor ItemTipGrey = WowColor::FromWow(.5f, .5f, .5f);

// Left (slot) and right (armor/weapon type) texts of the 3.3.5 type line.
void ItemTypeLine(const FWowItemTemplateInfo& Item, FString& Left, FString& Right)
{
    static const TCHAR* Slots[]={TEXT(""),TEXT("Head"),TEXT("Neck"),TEXT("Shoulder"),TEXT("Shirt"),TEXT("Chest"),TEXT("Waist"),TEXT("Legs"),TEXT("Feet"),TEXT("Wrist"),TEXT("Hands"),TEXT("Finger"),TEXT("Trinket"),TEXT("One-Hand"),TEXT("Off Hand"),TEXT("Ranged"),TEXT("Back"),TEXT("Two-Hand"),TEXT("Bag"),TEXT("Tabard"),TEXT("Chest"),TEXT("Main Hand"),TEXT("Off Hand"),TEXT("Held In Off-hand"),TEXT("Projectile"),TEXT("Thrown"),TEXT("Ranged"),TEXT("Quiver"),TEXT("Relic")};
    static const TCHAR* Armor[]={TEXT(""),TEXT("Cloth"),TEXT("Leather"),TEXT("Mail"),TEXT("Plate"),TEXT(""),TEXT("Shield"),TEXT("Libram"),TEXT("Idol"),TEXT("Totem"),TEXT("Sigil")};
    static const TCHAR* Weapons[]={TEXT("Axe"),TEXT("Axe"),TEXT("Bow"),TEXT("Gun"),TEXT("Mace"),TEXT("Mace"),TEXT("Polearm"),TEXT("Sword"),TEXT("Sword"),TEXT(""),TEXT("Staff"),TEXT(""),TEXT(""),TEXT("Fist Weapon"),TEXT("Miscellaneous"),TEXT("Dagger"),TEXT("Thrown"),TEXT("Spear"),TEXT("Crossbow"),TEXT("Wand"),TEXT("Fishing Pole")};
    Left = Item.InventoryType < UE_ARRAY_COUNT(Slots) ? Slots[Item.InventoryType] : TEXT("");
    Right.Reset();
    if (Item.InventoryType == 18 || Item.InventoryType == 0) { Left.Reset(); return; } // bags: "N Slot Bag" line instead
    if (Item.Class == 4 && Item.SubClass < UE_ARRAY_COUNT(Armor) && Item.InventoryType != 16) Right = Armor[Item.SubClass];
    else if (Item.Class == 2 && Item.SubClass < UE_ARRAY_COUNT(Weapons)) Right = Weapons[Item.SubClass];
    else if (Item.Class == 6) Right = Item.SubClass == 2 ? TEXT("Arrow") : Item.SubClass == 3 ? TEXT("Bullet") : TEXT("");
}

// r378 (Stefan 16.10: "Mail needs to be red because we lack proficiency"): the client colours an
// armor/weapon type red when you cannot use it. Proficiency is the passive skill spell the class
// learns (Mail 8737, Two-Handed Axes 197 ...); unknown or always-allowed types count as usable, and
// nothing is red before the spell list has arrived.
bool ItemTypeUsable(UWowConnectionManager* Connection, const FWowItemTemplateInfo& Item)
{
    if (!Connection || Connection->PacketHandler.KnownSpells.IsEmpty()) return true;
    static const uint32 ArmorSpells[]  = {0, 9078, 9077, 8737, 750, 0, 9116, 27762, 27764, 27763, 52665};
    static const uint32 WeaponSpells[] = {196, 197, 264, 266, 198, 199, 200, 201, 202, 0, 227, 0, 0, 15590, 0, 1180, 2567, 0, 5011, 5009, 0};
    uint32 Spell = 0;
    if (Item.Class == 4 && Item.SubClass < UE_ARRAY_COUNT(ArmorSpells)) Spell = ArmorSpells[Item.SubClass];
    else if (Item.Class == 2 && Item.SubClass < UE_ARRAY_COUNT(WeaponSpells)) Spell = WeaponSpells[Item.SubClass];
    return !Spell || Connection->PacketHandler.KnownSpells.Contains(Spell);
}

float ItemDps(const FWowItemTemplateInfo& Item)
{
    return Item.DelayMs > 0 ? (Item.DamageMin + Item.DamageMax) * 500.f / Item.DelayMs : 0.f;
}
}

namespace
{
// Heirlooms (ItemPrototype::ScalingStatDistribution): stats, armor, weapon damage and spell power
// come from ScalingStatDistribution x ScalingStatValues at min(player level, max level), like the
// 3.3.5 client (Player::_ApplyItemBonuses / ScalingStatValuesEntry getters).
bool ApplyHeirloomScaling(FWowItemTemplateInfo& Item, int32 PlayerLevel)
{
    const FDbcStore::FScalingStatDistribution* Ssd = FDbcStore::Get().ScalingDistribution(Item.ScalingStatDistribution);
    if (!Ssd) return false;
    const uint32 Level = uint32(FMath::Clamp(PlayerLevel, 1, int32(Ssd->MaxLevel ? Ssd->MaxLevel : 80)));
    const FDbcStore::FScalingStatValues* Ssv = FDbcStore::Get().ScalingValues(Level);
    if (!Ssv) return false;
    const uint32 Mask = Item.ScalingStatValue;
    uint32 Multiplier = 0;
    if (Mask & 0x00000001) Multiplier = Ssv->SsdMultiplier[0];
    else if (Mask & 0x00000002) Multiplier = Ssv->SsdMultiplier[1];
    else if (Mask & 0x00000004) Multiplier = Ssv->SsdMultiplier[2];
    else if (Mask & 0x00000008) Multiplier = Ssv->SsdMultiplier2;
    else if (Mask & 0x00000010) Multiplier = Ssv->SsdMultiplier[3];
    else if (Mask & 0x00040000) Multiplier = Ssv->SsdMultiplier3;
    Item.Stats.Reset();
    for (int32 I = 0; I < 10; ++I)
        if (Ssd->StatId[I] >= 0 && Ssd->Bonus[I])
            Item.Stats.Emplace(uint32(Ssd->StatId[I]), int32(uint64(Multiplier) * Ssd->Bonus[I] / 10000));
    uint32 Armor = 0;
    if (Mask & 0x00000020) Armor = Ssv->ArmorMod[0];
    else if (Mask & 0x00000040) Armor = Ssv->ArmorMod[1];
    else if (Mask & 0x00000080) Armor = Ssv->ArmorMod[2];
    else if (Mask & 0x00000100) Armor = Ssv->ArmorMod[3];
    else if (Mask & 0x00100000) Armor = Ssv->ArmorMod2[0];
    else if (Mask & 0x00200000) Armor = Ssv->ArmorMod2[1];
    else if (Mask & 0x00400000) Armor = Ssv->ArmorMod2[2];
    else if (Mask & 0x00800000) Armor = Ssv->ArmorMod2[3];
    if (Armor) Item.Armor = Armor;
    uint32 Dps = 0; bool bTwoHand = false;
    for (int32 I = 0; I < 6; ++I)
        if (Mask & (0x00000200u << I)) { Dps = Ssv->DpsMod[I]; bTwoHand = I == 1 || I == 3; break; }
    if (Dps && Item.DelayMs)
    {
        const float Average = float(Dps) * Item.DelayMs / 1000.f;
        const float Spread = bTwoHand ? .2f : .3f;
        Item.DamageMin = (1.f - Spread) * Average;
        Item.DamageMax = (1.f + Spread) * Average;
    }
    if ((Mask & 0x00008000) && Ssv->SpellPower) Item.Stats.Emplace(45u, int32(Ssv->SpellPower)); // ITEM_MOD_SPELL_POWER
    return true;
}
}

TArray<FWowItemTooltipLine> FWowItemPresentation::Lines(UWowConnectionManager* Connection, uint32 Entry, int32 Durability, int32 MaxDurability,
    const FWowItemEntity* Instance)
{
    TArray<FWowItemTooltipLine> Result;
    const FWowItemTemplateInfo* Item = Info(Connection, Entry);
    if (!Item) { Result.Add({TEXT("Retrieving item information"), ItemTipRed}); return Result; }
    FWowItemTemplateInfo ScaledItem;
    if (Item->ScalingStatDistribution)
    {
        const FWowPlayerEntity* Me = Connection ? Connection->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
        ScaledItem = *Item;
        if (ApplyHeirloomScaling(ScaledItem, Me ? Me->GetLevel() : 1)) Item = &ScaledItem;
    }
    auto Add = [&Result](const FString& Text, FLinearColor Color = FLinearColor::White, const FString& Right = FString(), FLinearColor RightColor = FLinearColor::White)
    { if (!Text.IsEmpty() || !Right.IsEmpty()) Result.Add({Text, Color, Right, RightColor}); };

    // Random property / suffix of this item instance (ITEM_FIELD_RANDOM_PROPERTIES_ID: > 0 ItemRandomProperties,
    // < 0 ItemRandomSuffix with ITEM_FIELD_PROPERTY_SEED as the suffix factor). The server puts their
    // enchantments in the PROP_ENCHANTMENT slots 7-11; a suffix scales them by AllocationPct.
    const int32 RandomId = Instance ? static_cast<int32>(Instance->GetField(ItemField::RANDOM_PROPERTIES_ID)) : 0;
    const uint32 SuffixFactor = Instance ? Instance->GetField(ItemField::PROPERTY_SEED) : 0;
    const FDbcStore::FItemRandomEnchants* Random = RandomId > 0 ? FDbcStore::Get().RandomProperty(uint32(RandomId))
        : RandomId < 0 ? FDbcStore::Get().RandomSuffix(uint32(-RandomId)) : nullptr;
    if (Instance)
    {
        static TSet<uint64> Logged;
        if (!Logged.Contains(Instance->Guid))
        {
            Logged.Add(Instance->Guid);
            UE_LOG(LogTemp, Log, TEXT("Item tooltip %u '%s': class %u/%u inv %u armor %u stats %d dur %u scaling %u/0x%x random %d seed %u (%s) enchants %u %u %u %u %u"),
                Entry, *Item->Name, Item->Class, Item->SubClass, Item->InventoryType, Item->Armor, Item->Stats.Num(), Item->MaxDurability,
                Item->ScalingStatDistribution, Item->ScalingStatValue,
                RandomId, SuffixFactor, Random ? *Random->Name : TEXT("no dbc row"), Instance->GetEnchantmentId(7), Instance->GetEnchantmentId(8),
                Instance->GetEnchantmentId(9), Instance->GetEnchantmentId(10), Instance->GetEnchantmentId(11));
        }
    }

    // Order and wording follow the 3.3.5 GameTooltip item layout (GlobalStrings ITEM_*).
    FString DisplayName = Item->Name;
    if (Random && !Random->Name.IsEmpty() && !DisplayName.EndsWith(Random->Name)) DisplayName += TEXT(" ") + Random->Name;
    Add(DisplayName, FWowInventoryManager::GetQualityColor(Item->Quality));
    if (Item->Flags & 0x8) Add(TEXT("Heroic"), ItemTipGreen);
    if (Item->Flags & 0x2) Add(TEXT("Conjured Item"));
    // An owned item that is already bound says so (ITEM_FIELD_FLAGS 0x1 = ITEM_SOULBOUND; Stefan 10.10:
    // an equipped item still read "Binds when equipped").
    if (Instance && (Instance->GetItemFlags() & 0x1))
        Add(Item->Flags & 0x08000000 ? TEXT("Account Bound") : TEXT("Soulbound"));
    else if (Item->Flags & 0x08000000) Add(TEXT("Binds to account")); // ITEM_FLAG_BIND_TO_ACCOUNT (heirlooms)
    else if (Item->Bonding == 1) Add(TEXT("Binds when picked up"));
    else if (Item->Bonding == 2) Add(TEXT("Binds when equipped"));
    else if (Item->Bonding == 3) Add(TEXT("Binds when used"));
    else if (Item->Bonding == 4 || Item->Bonding == 5) Add(TEXT("Quest Item"));
    FString SlotText, TypeText;
    ItemTypeLine(*Item, SlotText, TypeText);
    Add(SlotText, FLinearColor::White, TypeText, ItemTypeUsable(Connection, *Item) ? FLinearColor::White : ItemTipRed); // r378
    if (Item->DamageMax > 0)
    {
        Add(FString::Printf(TEXT("%d - %d Damage"), FMath::RoundToInt(Item->DamageMin), FMath::RoundToInt(Item->DamageMax)), FLinearColor::White,
            Item->DelayMs ? FString::Printf(TEXT("Speed %.2f"), Item->DelayMs / 1000.f) : FString());
        if (Item->DelayMs) Add(FString::Printf(TEXT("(%.1f damage per second)"), ItemDps(*Item)));
    }
    if (Item->Armor) Add(FString::Printf(TEXT("%u Armor"), Item->Armor));
    if (Item->Block) Add(FString::Printf(TEXT("%u Block"), Item->Block));
    for (const auto& Stat : Item->Stats)
        if (const FItemStatText* Text = ItemStatText(Stat.Key); Text && Text->bPrimary)
            Add(FString::Printf(TEXT("%c%d %s"), Stat.Value < 0 ? TEXT('-') : TEXT('+'), FMath::Abs(Stat.Value), Text->Line));
    for (int32 Index = 0; Index < 6; ++Index)
        if (Item->Resistances[Index]) Add(FString::Printf(TEXT("+%u %s Resistance"), Item->Resistances[Index], ItemSchoolName(Index)));
    // Random enchantments: primary stats here, rating stats with the other green "Equip:" lines below.
    TArray<FString> RandomEquipLines;
    if (Instance)
        for (int32 Slot = 7; Slot <= 11; ++Slot)
        {
            const uint32 EnchantId = Instance->GetEnchantmentId(Slot);
            const FDbcStore::FEnchantmentEffects* Effects = EnchantId ? FDbcStore::Get().EnchantmentEffects(EnchantId) : nullptr;
            if (!Effects) continue;
            for (int32 E = 0; E < 3; ++E)
            {
                int32 Amount = Effects->Amount[E];
                if (RandomId < 0 && Random && SuffixFactor)
                    for (int32 K = 0; K < 5; ++K)
                        if (Random->Enchant[K] == EnchantId) { Amount = int32(uint64(SuffixFactor) * Random->AllocationPct[K] / 10000); break; }
                if (Effects->Type[E] == 5) // ITEM_ENCHANTMENT_TYPE_STAT
                {
                    if (const FItemStatText* Text = ItemStatText(Effects->Arg[E]))
                    {
                        if (Text->bPrimary) Add(FString::Printf(TEXT("%c%d %s"), Amount < 0 ? TEXT('-') : TEXT('+'), FMath::Abs(Amount), Text->Line));
                        else RandomEquipLines.Add(TEXT("Equip: ") + FString(Text->Line).Replace(TEXT("%d"), *FString::FromInt(Amount)));
                    }
                }
                else if (Effects->Type[E] == 4) // ITEM_ENCHANTMENT_TYPE_RESISTANCE (arg = school, 0 = armor)
                {
                    if (Effects->Arg[E] == 0) Add(FString::Printf(TEXT("+%d Armor"), Amount));
                    else Add(FString::Printf(TEXT("+%d %s Resistance"), Amount, ItemSchoolName(int32(Effects->Arg[E]) - 1)));
                }
                else if (Effects->Type[E] != 0 && E == 0)
                {
                    const FString Name = FDbcStore::Get().EnchantmentName(EnchantId);
                    if (!Name.IsEmpty()) RandomEquipLines.Add(Name.Replace(TEXT("$i"), *FString::FromInt(Amount)));
                }
            }
        }
    for (uint32 Color : Item->SocketColors)
        if (Color) Add(Color == 1 ? TEXT("Meta Socket") : Color == 2 ? TEXT("Red Socket")
            : Color == 4 ? TEXT("Yellow Socket") : Color == 8 ? TEXT("Blue Socket") : TEXT("Prismatic Socket"));
    if (Item->ContainerSlots) Add(FString::Printf(TEXT("%u Slot Bag"), Item->ContainerSlots));
    if (MaxDurability > 0) Add(FString::Printf(TEXT("Durability %d / %d"), FMath::Max(0, Durability), MaxDurability));
    else if (Item->MaxDurability) Add(FString::Printf(TEXT("Durability %u / %u"), Item->MaxDurability, Item->MaxDurability));
    if (Item->RequiredLevel > 1)
    {
        const FWowPlayerEntity* Player = Connection ? Connection->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
        const bool bTooLow = Player && Player->GetLevel() < static_cast<int32>(Item->RequiredLevel);
        Add(FString::Printf(TEXT("Requires Level %u"), Item->RequiredLevel), bTooLow ? ItemTipRed : FLinearColor::White);
    }
    for (const auto& Stat : Item->Stats)
        if (const FItemStatText* Text = ItemStatText(Stat.Key); Text && !Text->bPrimary)
            Add(TEXT("Equip: ") + FString(Text->Line).Replace(TEXT("%d"), *FString::FromInt(Stat.Value)), ItemTipGreen);
    for (const FString& Line : RandomEquipLines) Add(Line, ItemTipGreen);
    for (int32 Index = 0; Index < 5; ++Index)
        if (Item->SpellIds[Index])
            if (const auto* Spell = FDbcStore::Get().Spells().GetById(Item->SpellIds[Index]))
            {
                const uint32 Trigger = Item->SpellTriggers[Index];
                const TCHAR* Prefix = Trigger == 0 ? TEXT("Use: ") : Trigger == 1 ? TEXT("Equip: ") : Trigger == 2 ? TEXT("Chance on hit: ") : TEXT("Use: ");
                const FString Text = Spell->Description.IsEmpty() ? Spell->SpellName : FWowSpellPresentation::Description(Spell->ID);
                if (!Text.IsEmpty()) Add(Prefix + Text, ItemTipGreen);
            }
    if (!Item->Description.IsEmpty()) Add(TEXT("\"") + Item->Description + TEXT("\""), ItemTipGold);
    if (Item->SellPrice) Add(TEXT("Sell Price: ") + Money(Item->SellPrice));
    return Result;
}

TArray<FWowItemTooltipLine> FWowItemPresentation::CompareLines(UWowConnectionManager* Connection, uint32 NewEntry, uint32 EquippedEntry)
{
    // ShoppingTooltip for the equipped item: "Currently Equipped", the item, then the
    // GetItemStatDelta summary of what changes when the hovered item replaces it.
    TArray<FWowItemTooltipLine> Result;
    Result.Add({TEXT("Currently Equipped"), ItemTipGrey});
    Result.Append(Lines(Connection, EquippedEntry));
    const FWowItemTemplateInfo* New = Info(Connection, NewEntry);
    const FWowItemTemplateInfo* Old = Info(Connection, EquippedEntry);
    if (!New || !Old) return Result;
    TArray<TPair<FString, float>> Deltas;
    TMap<uint32, int32> Stats;
    for (const auto& Stat : New->Stats) Stats.FindOrAdd(Stat.Key) += Stat.Value;
    for (const auto& Stat : Old->Stats) Stats.FindOrAdd(Stat.Key) -= Stat.Value;
    for (const auto& Pair : Stats)
        if (Pair.Value != 0)
            if (const FItemStatText* Text = ItemStatText(Pair.Key)) Deltas.Add({Text->Short, static_cast<float>(Pair.Value)});
    const float Dps = ItemDps(*New) - ItemDps(*Old);
    if (FMath::Abs(Dps) >= .05f) Deltas.Add({TEXT("Damage Per Second"), Dps});
    const int32 Armor = static_cast<int32>(New->Armor) - static_cast<int32>(Old->Armor);
    if (Armor) Deltas.Add({TEXT("Armor"), static_cast<float>(Armor)});
    for (int32 Index = 0; Index < 6; ++Index)
        if (const int32 Resist = static_cast<int32>(New->Resistances[Index]) - static_cast<int32>(Old->Resistances[Index]))
            Deltas.Add({FString(ItemSchoolName(Index)) + TEXT(" Resistance"), static_cast<float>(Resist)});
    if (Deltas.IsEmpty()) return Result;
    Result.Add({TEXT(" "), FLinearColor::White});
    Result.Add({TEXT("If you replace this item, the following stat changes will occur:"), FLinearColor::White});
    for (const auto& Delta : Deltas)
    {
        const bool bFraction = !FMath::IsNearlyEqual(Delta.Value, FMath::RoundToFloat(Delta.Value));
        const FString Number = bFraction ? FString::Printf(TEXT("%+.1f"), Delta.Value) : FString::Printf(TEXT("%+d"), FMath::RoundToInt(Delta.Value));
        Result.Add({Number + TEXT(" ") + Delta.Key, Delta.Value > 0 ? ItemTipGreen : ItemTipRed});
    }
    return Result;
}

TArray<uint8> FWowItemPresentation::CompareSlots(uint32 InventoryType)
{
    switch (InventoryType)
    {
    case 1: return {0}; case 2: return {1}; case 3: return {2}; case 4: return {3};
    case 5: case 20: return {4}; case 6: return {5}; case 7: return {6}; case 8: return {7};
    case 9: return {8}; case 10: return {9}; case 11: return {10, 11}; case 12: return {12, 13};
    case 16: return {14}; case 13: return {15, 16}; case 17: case 21: return {15};
    case 14: case 22: case 23: return {16}; case 15: case 25: case 26: case 28: return {17};
    case 19: return {18};
    default: return {};
    }
}

namespace
{
// Native item tooltip in the GameTooltip style; holding Shift (COMPAREITEMS, r424 - was Ctrl) adds "Currently Equipped"
// tooltips beside it, like ShoppingTooltip1/2 (IsModifiedClick("COMPAREITEMS")).
class SWowLiveItemTooltip : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowLiveItemTooltip) {} SLATE_END_ARGS()
    void Construct(const FArguments&, UWowConnectionManager* InConnection, uint32 InEntry, bool bInCompare,
        const TArray<FWowItemTooltipLine>& InExtra = TArray<FWowItemTooltipLine>())
    {
        Connection=InConnection; Entry=InEntry; bCompare=bInCompare; Extra=InExtra;
        ChildSlot
        [
            SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
            [SNew(SWowTooltipFrame)[SNew(SBox).MaxDesiredWidth(320)[SAssignNew(Rows,SVerticalBox)]]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(2,0,0,0)
            [SAssignNew(CompareBox,SHorizontalBox)]
        ];
        Refresh();
    }
    void Tick(const FGeometry& G,double T,float D) override
    {
        SCompoundWidget::Tick(G,T,D);
        const bool bShift=bCompare&&((FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetModifierKeys().IsShiftDown())
            ||FWowClientPreferences::GetWorldOption(TEXT("alwayscompareitems")));
        if(T>NextRefresh||bShift!=bShowingCompare){NextRefresh=T+.2;bShowingCompare=bShift;Refresh();}
    }
private:
    TWeakObjectPtr<UWowConnectionManager> Connection;
    uint32 Entry=0; double NextRefresh=0;
    bool bCompare=true, bShowingCompare=false;
    TArray<FWowItemTooltipLine> Extra;
    FString Signature;
    TSharedPtr<SVerticalBox> Rows;
    TSharedPtr<SHorizontalBox> CompareBox;

    static void Fill(SVerticalBox& Box,const TArray<FWowItemTooltipLine>& Lines,int32 HeaderLine)
    {
        Box.ClearChildren();
        for(int32 I=0;I<Lines.Num();++I)
        {
            const bool bHeader=I==HeaderLine;
            const FWowItemTooltipLine& Line=Lines[I];
            auto Text=[&](const FString& Value,const FLinearColor& Color,bool bWrap)
            {
                return SNew(STextBlock).Text(FText::FromString(Value)).ColorAndOpacity(Color)
                    .Font(FWowWindowStyle::TooltipFont(bHeader)).ShadowOffset(FVector2D(1,1))
                    .ShadowColorAndOpacity(FLinearColor(0,0,0,1)).AutoWrapText(bWrap);
            };
            if(Line.Right.IsEmpty())
                Box.AddSlot().AutoHeight().Padding(0,I==0?0:2,0,0)[Text(Line.Text,Line.Color,true)];
            else
                Box.AddSlot().AutoHeight().Padding(0,I==0?0:2,0,0)
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1)[Text(Line.Text,Line.Color,false)]
                    +SHorizontalBox::Slot().AutoWidth().Padding(20,0,0,0).HAlign(HAlign_Right)[Text(Line.Right,Line.RightColor,false)]];
        }
    }
    TArray<uint32> EquippedForCompare() const
    {
        TArray<uint32> Result;
        const FWowItemTemplateInfo* Item=FWowItemPresentation::Info(Connection.Get(),Entry);
        UWowUIManager* UI=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->GetGameInstance()
            ?GEngine->GameViewport->GetGameInstance()->GetSubsystem<UWowUIManager>():nullptr;
        TSharedPtr<FWowInventoryManager> Inventory=UI?UI->GetInventoryManager():nullptr;
        if(!Item||!Inventory.IsValid())return Result;
        for(uint8 Slot:FWowItemPresentation::CompareSlots(Item->InventoryType))
            if(const FWowItemSlot* Equipped=Inventory->GetEquippedItem(Slot))
                if(Equipped->ItemEntry&&Equipped->ItemEntry!=Entry)Result.Add(Equipped->ItemEntry);
        return Result;
    }
    void Refresh()
    {
        auto Lines=FWowItemPresentation::Lines(Connection.Get(),Entry);
        if(Extra.Num())
        {
            int32 At=Lines.Num();
            for(int32 I=0;I<Lines.Num();++I)if(Lines[I].Text.StartsWith(TEXT("Sell Price")))At=I;
            for(int32 I=0;I<Extra.Num();++I)Lines.Insert(Extra[I],At+I);
        }
        const TArray<uint32> Equipped=bShowingCompare?EquippedForCompare():TArray<uint32>();
        TArray<TArray<FWowItemTooltipLine>> Compare;
        for(uint32 Other:Equipped)Compare.Add(FWowItemPresentation::CompareLines(Connection.Get(),Entry,Other));
        FString NewSignature;
        for(const auto& Line:Lines) NewSignature+=Line.Text+Line.Right+Line.Color.ToString();
        for(const auto& List:Compare){NewSignature+=TEXT("|");for(const auto& Line:List)NewSignature+=Line.Text+Line.Color.ToString();}
        if(Signature==NewSignature) return;
        Signature=NewSignature;
        Fill(*Rows,Lines,0);
        CompareBox->ClearChildren();
        for(const auto& List:Compare)
        {
            TSharedPtr<SVerticalBox> Box;
            CompareBox->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0,0,2,0)
            [SNew(SWowTooltipFrame)[SNew(SBox).MaxDesiredWidth(320)[SAssignNew(Box,SVerticalBox)]]];
            Fill(*Box,List,1);
        }
    }
};

class SWowLiveItemCard : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowLiveItemCard) {} SLATE_END_ARGS()
    void Construct(const FArguments&,UWowConnectionManager* InConnection,UWowUIManager* InUI,uint32 InEntry,uint32 Count,uint32 Display)
    {
        Connection=InConnection;UI=InUI;Entry=InEntry;DisplayId=Display;
        Brush=MakeShared<FWowRetainedSlateBrush>();Brush->DrawAs=ESlateBrushDrawType::Image;Brush->ImageSize=FVector2D(40);
        ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(WowColor::FromWow(.045f, .035f, .025f, 1)).Padding(6)
            .ToolTip(FWowItemPresentation::Tooltip(InConnection,Entry))
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(1)
                    .BorderBackgroundColor_Lambda([this](){return FWowItemPresentation::QualityColor(Connection.Get(),Entry);})
                    [SNew(SBox).WidthOverride(40).HeightOverride(40)[SNew(SImage).Image(Brush.Get())]]]
                +SHorizontalBox::Slot().FillWidth(1).Padding(8,0).VAlign(VAlign_Center)
                [SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))
                        .ColorAndOpacity_Lambda([this](){return FWowItemPresentation::QualityColor(Connection.Get(),Entry);})
                        .Text_Lambda([this,Count](){return FText::FromString(FWowItemPresentation::Name(Connection.Get(),Entry)
                            +(Count>1?FString::Printf(TEXT(" (x%u)"),Count):FString()));})]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)
                    [SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",10))
                        .ColorAndOpacity(WowColor::FromWow(.85f, .80f, .66f))
                        .Text_Lambda([this](){const auto* Item=FWowItemPresentation::Info(Connection.Get(),Entry);
                            return FText::FromString(Item?FWowItemPresentation::TypeLabel(*Item):TEXT("Loading details..."));})]]]];
        UpdateIcon();
    }
    void Tick(const FGeometry& G,double T,float D) override
    {SCompoundWidget::Tick(G,T,D);if(T>NextRefresh){NextRefresh=T+.25;UpdateIcon();}}
private:
    TWeakObjectPtr<UWowConnectionManager> Connection;
    TWeakObjectPtr<UWowUIManager> UI;
    uint32 Entry=0,DisplayId=0;double NextRefresh=0;
    FString LoadedIcon;
    TSharedPtr<FWowRetainedSlateBrush> Brush;
    void UpdateIcon()
    {
        const auto* Item=FWowItemPresentation::Info(Connection.Get(),Entry);
        const FString Path=FWowItemPresentation::IconPath(Entry,Item&&Item->DisplayId?Item->DisplayId:DisplayId);
        if(Path==LoadedIcon||!UI.IsValid()||!UI->GetFrameManager())return;
        if(UTexture2D* Texture=UI->GetFrameManager()->LoadUITexture(Path))
        {Brush->SetResourceObject(Texture);LoadedIcon=Path;}
    }
};
}
FLinearColor FWowItemPresentation::QualityColor(UWowConnectionManager* Connection,uint32 Entry)
{
    const auto* Item=Info(Connection,Entry);
    return Item?FWowInventoryManager::GetQualityColor(Item->Quality):FLinearColor::White;
}
FString FWowItemPresentation::TypeLabel(const FWowItemTemplateInfo& Item)
{
    static const TCHAR* Armor[]={TEXT("Miscellaneous"),TEXT("Cloth"),TEXT("Leather"),TEXT("Mail"),TEXT("Plate"),TEXT("Cosmetic"),TEXT("Shield"),TEXT("Libram"),TEXT("Idol"),TEXT("Totem"),TEXT("Sigil")};
    static const TCHAR* Weapons[]={TEXT("Axe"),TEXT("Two-Handed Axe"),TEXT("Bow"),TEXT("Gun"),TEXT("Mace"),TEXT("Two-Handed Mace"),TEXT("Polearm"),TEXT("Sword"),TEXT("Two-Handed Sword"),TEXT("Weapon"),TEXT("Staff"),TEXT("Weapon"),TEXT("Weapon"),TEXT("Fist Weapon"),TEXT("Miscellaneous"),TEXT("Dagger"),TEXT("Thrown"),TEXT("Weapon"),TEXT("Crossbow"),TEXT("Wand"),TEXT("Fishing Pole")};
    static const TCHAR* Slots[]={TEXT(""),TEXT("Head"),TEXT("Neck"),TEXT("Shoulder"),TEXT("Shirt"),TEXT("Chest"),TEXT("Waist"),TEXT("Legs"),TEXT("Feet"),TEXT("Wrist"),TEXT("Hands"),TEXT("Finger"),TEXT("Trinket"),TEXT("One-Hand"),TEXT("Off Hand"),TEXT("Ranged"),TEXT("Back"),TEXT("Two-Hand"),TEXT("Bag"),TEXT("Tabard"),TEXT("Chest"),TEXT("Main Hand"),TEXT("Off Hand"),TEXT("Held In Off-hand"),TEXT("Ammo"),TEXT("Thrown"),TEXT("Ranged"),TEXT(""),TEXT("Relic")};
    FString Type;
    if(Item.Class==4&&Item.SubClass<UE_ARRAY_COUNT(Armor))Type=Armor[Item.SubClass];
    else if(Item.Class==2&&Item.SubClass<UE_ARRAY_COUNT(Weapons))Type=Weapons[Item.SubClass];
    else if(Item.Class==0)Type=TEXT("Consumable");
    else if(Item.Class==7)Type=TEXT("Trade Goods");
    else if(Item.Class==12)Type=TEXT("Quest Item");
    const FString Slot=Item.InventoryType<UE_ARRAY_COUNT(Slots)?Slots[Item.InventoryType]:TEXT("");
    return Slot.IsEmpty()?Type:Type.IsEmpty()?Slot:Slot+TEXT("  -  ")+Type;
}
TSharedRef<SWidget> FWowItemPresentation::ItemCard(UWowConnectionManager* Connection,UWowUIManager* UI,uint32 Entry,uint32 Count,uint32 DisplayId)
{return SNew(SWowLiveItemCard,Connection,UI,Entry,Count,DisplayId);}
TSharedRef<SToolTip> FWowItemPresentation::Tooltip(UWowConnectionManager* Connection,uint32 Entry,bool bCompare,
    const TArray<FWowItemTooltipLine>& ExtraLines)
{
    // The tooltip frame paints its own GameTooltip backdrop; the SToolTip shell stays invisible.
    static const FSlateNoResource NoBorder;
    return SNew(SToolTip).BorderImage(&NoBorder).TextMargin(FMargin(0))[SNew(SWowLiveItemTooltip,Connection,Entry,bCompare,ExtraLines)];
}
