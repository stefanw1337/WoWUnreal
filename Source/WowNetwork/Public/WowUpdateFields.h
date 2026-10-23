#pragma once
#include "CoreMinimal.h"

// WoW 3.3.5a update field indices (build 12340)
// Reference: azerothcore-wotlk UpdateFields.h

// Object types (OBJECT_FIELD_TYPE bitmask)
namespace WowTypeMask
{
    inline constexpr uint32 OBJECT        = 0x0001;
    inline constexpr uint32 ITEM          = 0x0002;
    inline constexpr uint32 CONTAINER     = 0x0004;
    inline constexpr uint32 UNIT          = 0x0008;
    inline constexpr uint32 PLAYER        = 0x0010;
    inline constexpr uint32 GAMEOBJECT    = 0x0020;
    inline constexpr uint32 DYNAMICOBJECT = 0x0040;
    inline constexpr uint32 CORPSE        = 0x0080;
}

// GUID high-type masks for 64-bit GUIDs
namespace WowHighGuid
{
    inline constexpr uint64 PLAYER        = 0x0000000000000000ULL;
    inline constexpr uint64 ITEM          = 0x4000000000000000ULL;
    inline constexpr uint64 GAMEOBJECT    = 0xF110000000000000ULL;
    inline constexpr uint64 TRANSPORT     = 0xF120000000000000ULL;
    inline constexpr uint64 UNIT          = 0xF130000000000000ULL;
    inline constexpr uint64 PET           = 0xF140000000000000ULL;
    inline constexpr uint64 VEHICLE       = 0xF150000000000000ULL;
    inline constexpr uint64 DYNAMICOBJECT = 0xF100000000000000ULL;
    inline constexpr uint64 CORPSE        = 0xF101000000000000ULL;
}

// Object base fields (all objects have these)
namespace ObjectField
{
    inline constexpr uint16 GUID        = 0x0000; // 2 uint32s (64-bit)
    inline constexpr uint16 TYPE        = 0x0002;
    inline constexpr uint16 ENTRY       = 0x0003;
    inline constexpr uint16 SCALE_X     = 0x0004;
    inline constexpr uint16 PADDING     = 0x0005;
    inline constexpr uint16 END         = 0x0006;
}

// Item fields (offset from OBJECT_END)
namespace ItemField
{
    inline constexpr uint16 BASE = ObjectField::END;

    inline constexpr uint16 OWNER                = BASE + 0x0000; // 2
    inline constexpr uint16 CONTAINED            = BASE + 0x0002; // 2
    inline constexpr uint16 CREATOR              = BASE + 0x0004; // 2
    inline constexpr uint16 GIFT_CREATOR         = BASE + 0x0006; // 2
    inline constexpr uint16 STACK_COUNT          = BASE + 0x0008;
    inline constexpr uint16 DURATION             = BASE + 0x0009;
    inline constexpr uint16 SPELL_CHARGES        = BASE + 0x000A; // 5
    inline constexpr uint16 FLAGS                = BASE + 0x000F;
    inline constexpr uint16 ENCHANTMENT_1_1      = BASE + 0x0010;
    inline constexpr uint16 ENCHANTMENT_1_3      = BASE + 0x0012;
    inline constexpr uint16 ENCHANTMENT_2_1      = BASE + 0x0013;
    inline constexpr uint16 ENCHANTMENT_2_3      = BASE + 0x0015;
    inline constexpr uint16 ENCHANTMENT_3_1      = BASE + 0x0016;
    inline constexpr uint16 ENCHANTMENT_3_3      = BASE + 0x0018;
    inline constexpr uint16 ENCHANTMENT_4_1      = BASE + 0x0019;
    inline constexpr uint16 ENCHANTMENT_4_3      = BASE + 0x001B;
    inline constexpr uint16 ENCHANTMENT_5_1      = BASE + 0x001C;
    inline constexpr uint16 ENCHANTMENT_5_3      = BASE + 0x001E;
    inline constexpr uint16 ENCHANTMENT_6_1      = BASE + 0x001F;
    inline constexpr uint16 ENCHANTMENT_6_3      = BASE + 0x0021;
    inline constexpr uint16 ENCHANTMENT_7_1      = BASE + 0x0022;
    inline constexpr uint16 ENCHANTMENT_7_3      = BASE + 0x0024;
    inline constexpr uint16 ENCHANTMENT_8_1      = BASE + 0x0025;
    inline constexpr uint16 ENCHANTMENT_8_3      = BASE + 0x0027;
    inline constexpr uint16 ENCHANTMENT_9_1      = BASE + 0x0028;
    inline constexpr uint16 ENCHANTMENT_9_3      = BASE + 0x002A;
    inline constexpr uint16 ENCHANTMENT_10_1     = BASE + 0x002B;
    inline constexpr uint16 ENCHANTMENT_10_3     = BASE + 0x002D;
    inline constexpr uint16 ENCHANTMENT_11_1     = BASE + 0x002E;
    inline constexpr uint16 ENCHANTMENT_11_3     = BASE + 0x0030;
    inline constexpr uint16 ENCHANTMENT_12_1     = BASE + 0x0031;
    inline constexpr uint16 ENCHANTMENT_12_3     = BASE + 0x0033;
    inline constexpr uint16 PROPERTY_SEED        = BASE + 0x0034;
    inline constexpr uint16 RANDOM_PROPERTIES_ID = BASE + 0x0035;
    inline constexpr uint16 DURABILITY           = BASE + 0x0036;
    inline constexpr uint16 MAX_DURABILITY       = BASE + 0x0037;
    inline constexpr uint16 CREATE_PLAYED_TIME   = BASE + 0x0038;

    inline constexpr uint16 END = BASE + 0x003A;
}

// Container fields (offset from ITEM_END)
namespace ContainerField
{
    inline constexpr uint16 BASE = ItemField::END;

    inline constexpr uint16 NUM_SLOTS = BASE + 0x0000;
    inline constexpr uint16 ALIGN_PAD = BASE + 0x0001;
    inline constexpr uint16 SLOT_1    = BASE + 0x0002; // 72 uint32s / 36 GUIDs

    inline constexpr uint16 END = BASE + 0x004A;
}

// Unit fields (offset from OBJECT_END)
namespace UnitField
{
    inline constexpr uint16 BASE = ObjectField::END;

    inline constexpr uint16 CHARM                = BASE + 0x0000; // 2
    inline constexpr uint16 SUMMON               = BASE + 0x0002; // 2
    inline constexpr uint16 CRITTER              = BASE + 0x0004; // 2
    inline constexpr uint16 CHARMEDBY            = BASE + 0x0006; // 2
    inline constexpr uint16 SUMMONEDBY           = BASE + 0x0008; // 2
    inline constexpr uint16 CREATEDBY            = BASE + 0x000A; // 2
    inline constexpr uint16 TARGET               = BASE + 0x000C; // 2
    inline constexpr uint16 CHANNEL_OBJECT       = BASE + 0x000E; // 2
    inline constexpr uint16 CHANNEL_SPELL        = BASE + 0x0010;
    inline constexpr uint16 BYTES_0              = BASE + 0x0011;
    inline constexpr uint16 HEALTH               = BASE + 0x0012;
    inline constexpr uint16 POWER1               = BASE + 0x0013; // Mana
    inline constexpr uint16 POWER2               = BASE + 0x0014; // Rage
    inline constexpr uint16 POWER3               = BASE + 0x0015; // Focus
    inline constexpr uint16 POWER4               = BASE + 0x0016; // Energy
    inline constexpr uint16 POWER5               = BASE + 0x0017; // Happiness
    inline constexpr uint16 POWER6               = BASE + 0x0018;
    inline constexpr uint16 POWER7               = BASE + 0x0019;
    inline constexpr uint16 MAXHEALTH            = BASE + 0x001A;
    inline constexpr uint16 MAXPOWER1            = BASE + 0x001B;
    inline constexpr uint16 MAXPOWER2            = BASE + 0x001C;
    inline constexpr uint16 MAXPOWER3            = BASE + 0x001D;
    inline constexpr uint16 MAXPOWER4            = BASE + 0x001E;
    inline constexpr uint16 MAXPOWER5            = BASE + 0x001F;
    inline constexpr uint16 MAXPOWER6            = BASE + 0x0020; // Runes
    inline constexpr uint16 MAXPOWER7            = BASE + 0x0021; // Runic Power
    inline constexpr uint16 LEVEL                = BASE + 0x0030;
    inline constexpr uint16 FACTIONTEMPLATE      = BASE + 0x0031;
    inline constexpr uint16 FLAGS                = BASE + 0x0035;
    inline constexpr uint16 FLAGS_2              = BASE + 0x0036;
    inline constexpr uint16 NPC_FLAGS            = BASE + 0x004C; // UNIT_NPC_FLAGS (0x37 is UNIT_FIELD_AURASTATE)
    inline constexpr uint16 DISPLAYID            = BASE + 0x003D;
    inline constexpr uint16 NATIVEDISPLAYID      = BASE + 0x003E;
    inline constexpr uint16 MOUNTDISPLAYID       = BASE + 0x003F;
    inline constexpr uint16 BYTES_1              = BASE + 0x0044; // byte0=StandState, byte1=PetTalents, byte2=StandFlags, byte3=AnimTier
    inline constexpr uint16 DYNAMIC_FLAGS        = BASE + 0x0049;
    inline constexpr uint16 NPC_EMOTESTATE       = BASE + 0x004D;

    // Stats (Strength, Agility, Stamina, Intellect, Spirit)
    inline constexpr uint16 STAT0                = BASE + 0x004E; // Strength
    inline constexpr uint16 STAT1                = BASE + 0x004F; // Agility
    inline constexpr uint16 STAT2                = BASE + 0x0050; // Stamina
    inline constexpr uint16 STAT3                = BASE + 0x0051; // Intellect
    inline constexpr uint16 STAT4                = BASE + 0x0052; // Spirit

    // Resistances (Physical/Armor, Holy, Fire, Nature, Frost, Shadow, Arcane)
    inline constexpr uint16 RESISTANCES          = BASE + 0x005D; // 7 values

    inline constexpr uint16 BYTES_2              = BASE + 0x0074; // byte0=SheathState, byte1=PvpFlags, byte2=PetFlags, byte3=ShapeshiftForm

    inline constexpr uint16 AURA_STATE = BASE + 0x0037;
    inline constexpr uint16 BASE_MANA = BASE + 0x0072;
    // Attack Power
    inline constexpr uint16 ATTACK_POWER         = BASE + 0x0075;
    inline constexpr uint16 RANGED_ATTACK_POWER  = BASE + 0x0078;

    inline constexpr uint16 END = BASE + 0x008E;
}

// Player fields (offset from UNIT_END) — subset
namespace PlayerField
{
    inline constexpr uint16 BASE = UnitField::END;

    inline constexpr uint16 FLAGS         = BASE + 0x0002;
    inline constexpr uint16 BYTES         = BASE + 0x0005;
    inline constexpr uint16 BYTES_2       = BASE + 0x0006;

    // The 3.3.5a quest log has 25 slots. Each slot occupies five uint32 fields:
    // quest ID, state, four packed uint16 objective counters, and timer.
    inline constexpr uint16 QUEST_LOG_START = BASE + 0x000A;
    inline constexpr uint16 QUEST_LOG_FIELDS_PER_SLOT = 5;
    inline constexpr uint16 QUEST_LOG_SLOT_COUNT = 25;
    inline constexpr uint16 QUEST_LOG_END =
        QUEST_LOG_START + QUEST_LOG_FIELDS_PER_SLOT * QUEST_LOG_SLOT_COUNT - 1;

    inline constexpr uint16 XP            = BASE + 0x01E6;
    inline constexpr uint16 NEXT_LEVEL_XP = BASE + 0x01E7;
    inline constexpr uint16 BUYBACK_SLOT_START = BASE + 0x0144; // 12 GUIDs
    inline constexpr uint16 BUYBACK_PRICE_START = BASE + 0x041D; // 12 copper prices
    inline constexpr uint16 BUYBACK_TIME_START = BASE + 0x0429; // 12 timestamps
    inline constexpr uint16 COINAGE       = BASE + 0x03FE;

    // Combat ratings (crit, hit, haste, etc.) - 25 values starting at offset from UNIT_END
    inline constexpr uint16 COMBAT_RATING_1 = BASE + 0x043B;

    // Equipment slots - visible items
    inline constexpr uint16 VISIBLE_ITEM_BASE = BASE + 0x0087; // Entry/enchant pairs for 19 slots

    // Equipment slot indices for VISIBLE_ITEM_BASE
    inline constexpr uint8 EQUIPMENT_SLOT_HEAD = 0;
    inline constexpr uint8 EQUIPMENT_SLOT_NECK = 1;
    inline constexpr uint8 EQUIPMENT_SLOT_SHOULDERS = 2;
    inline constexpr uint8 EQUIPMENT_SLOT_SHIRT = 3;
    inline constexpr uint8 EQUIPMENT_SLOT_CHEST = 4;
    inline constexpr uint8 EQUIPMENT_SLOT_WAIST = 5;
    inline constexpr uint8 EQUIPMENT_SLOT_LEGS = 6;
    inline constexpr uint8 EQUIPMENT_SLOT_FEET = 7;
    inline constexpr uint8 EQUIPMENT_SLOT_WRISTS = 8;
    inline constexpr uint8 EQUIPMENT_SLOT_HANDS = 9;
    inline constexpr uint8 EQUIPMENT_SLOT_FINGER1 = 10;
    inline constexpr uint8 EQUIPMENT_SLOT_FINGER2 = 11;
    inline constexpr uint8 EQUIPMENT_SLOT_TRINKET1 = 12;
    inline constexpr uint8 EQUIPMENT_SLOT_TRINKET2 = 13;
    inline constexpr uint8 EQUIPMENT_SLOT_BACK = 14;
    inline constexpr uint8 EQUIPMENT_SLOT_MAINHAND = 15;
    inline constexpr uint8 EQUIPMENT_SLOT_OFFHAND = 16;
    inline constexpr uint8 EQUIPMENT_SLOT_RANGED = 17;
    inline constexpr uint8 EQUIPMENT_SLOT_TABARD = 18;

    // Inventory slots - item GUIDs. This block covers equipment (0-18) and the 4 worn bag slots.
    inline constexpr uint16 INV_SLOT_HEAD = BASE + 0x00B0; // 23 GUIDs total (46 uint32 fields)
    inline constexpr uint16 INV_SLOT_NECK = BASE + 0x00B2;
    inline constexpr uint16 INV_SLOT_SHOULDERS = BASE + 0x00B4;
    inline constexpr uint16 INV_SLOT_SHIRT = BASE + 0x00B6;
    inline constexpr uint16 INV_SLOT_CHEST = BASE + 0x00B8;
    inline constexpr uint16 INV_SLOT_WAIST = BASE + 0x00BA;
    inline constexpr uint16 INV_SLOT_LEGS = BASE + 0x00BC;
    inline constexpr uint16 INV_SLOT_FEET = BASE + 0x00BE;
    inline constexpr uint16 INV_SLOT_WRISTS = BASE + 0x00C0;
    inline constexpr uint16 INV_SLOT_HANDS = BASE + 0x00C2;
    inline constexpr uint16 INV_SLOT_FINGER1 = BASE + 0x00C4;
    inline constexpr uint16 INV_SLOT_FINGER2 = BASE + 0x00C6;
    inline constexpr uint16 INV_SLOT_TRINKET1 = BASE + 0x00C8;
    inline constexpr uint16 INV_SLOT_TRINKET2 = BASE + 0x00CA;
    inline constexpr uint16 INV_SLOT_BACK = BASE + 0x00CC;
    inline constexpr uint16 INV_SLOT_MAINHAND = BASE + 0x00CE;
    inline constexpr uint16 INV_SLOT_OFFHAND = BASE + 0x00D0;
    inline constexpr uint16 INV_SLOT_RANGED = BASE + 0x00D2;
    inline constexpr uint16 INV_SLOT_TABARD = BASE + 0x00D4;
    inline constexpr uint16 INV_SLOT_BAG_1 = BASE + 0x00D6;
    inline constexpr uint16 INV_SLOT_BAG_2 = BASE + 0x00D8;
    inline constexpr uint16 INV_SLOT_BAG_3 = BASE + 0x00DA;
    inline constexpr uint16 INV_SLOT_BAG_4 = BASE + 0x00DC;
    inline constexpr uint16 INV_SLOT_LAST = INV_SLOT_BAG_4;

    // Backpack slots (item GUIDs in the backpack's 16 positions)
    inline constexpr uint16 PACK_SLOT_START = BASE + 0x00DE; // 32 uint32s (16 GUIDs)
    inline constexpr uint16 PACK_SLOT_END = PACK_SLOT_START + 31;

    // Bank base slots (28 item GUIDs) and bank bag slots (7 bag GUIDs)
    inline constexpr uint16 BANK_SLOT_START = BASE + 0x00FE; // 56 uint32s (28 GUIDs)
    inline constexpr uint16 BANK_SLOT_END = BANK_SLOT_START + 55;
    inline constexpr uint16 BANKBAG_SLOT_1 = BASE + 0x0136; // 14 uint32s (7 GUIDs)
    inline constexpr uint16 BANKBAG_SLOT_END = BANKBAG_SLOT_1 + 13;

    inline constexpr uint16 END = BASE + 0x0474;
}

// GameObject fields (offset from OBJECT_END)
namespace GameObjectField
{
    inline constexpr uint16 BASE = ObjectField::END;

    inline constexpr uint16 CREATED_BY      = BASE + 0x0000; // 2
    inline constexpr uint16 DISPLAY_ID      = BASE + 0x0002;
    inline constexpr uint16 FLAGS           = BASE + 0x0003;
    inline constexpr uint16 PARENT_ROTATION = BASE + 0x0004; // 4 floats
    inline constexpr uint16 DYNAMIC         = BASE + 0x0008;
    inline constexpr uint16 FACTION         = BASE + 0x0009;
    inline constexpr uint16 LEVEL           = BASE + 0x000A;
    inline constexpr uint16 BYTES_1         = BASE + 0x000B;

    inline constexpr uint16 END = BASE + 0x000C;
}

// DynamicObject fields (offset from OBJECT_END)
namespace DynamicObjectField
{
    inline constexpr uint16 BASE = ObjectField::END;

    inline constexpr uint16 CASTER    = BASE + 0x0000; // 2
    inline constexpr uint16 BYTES     = BASE + 0x0002;
    inline constexpr uint16 SPELL_ID  = BASE + 0x0003;
    inline constexpr uint16 RADIUS    = BASE + 0x0004;
    inline constexpr uint16 CAST_TIME = BASE + 0x0005;

    inline constexpr uint16 END = BASE + 0x0006;
}

// Corpse fields (offset from OBJECT_END)
namespace CorpseField
{
    inline constexpr uint16 BASE = ObjectField::END;

    inline constexpr uint16 OWNER = BASE + 0x0000; // 2

    inline constexpr uint16 END = BASE + 0x0012;
}

// UpdateObject block types
namespace UpdateType
{
    inline constexpr uint8 VALUES               = 0;
    inline constexpr uint8 MOVEMENT             = 1;
    inline constexpr uint8 CREATE_OBJECT        = 2;
    inline constexpr uint8 CREATE_OBJECT2       = 3;
    inline constexpr uint8 OUT_OF_RANGE_OBJECTS  = 4;
    inline constexpr uint8 NEAR_OBJECTS          = 5;
}

// Object update flags
namespace UpdateFlag
{
    inline constexpr uint16 NONE                = 0x0000;
    inline constexpr uint16 SELF                = 0x0001;
    inline constexpr uint16 TRANSPORT           = 0x0002;
    inline constexpr uint16 HAS_TARGET          = 0x0004;
    inline constexpr uint16 LOWGUID             = 0x0010;
    inline constexpr uint16 LIVING              = 0x0020;
    inline constexpr uint16 STATIONARY_POSITION = 0x0040;
    inline constexpr uint16 VEHICLE             = 0x0080;
    inline constexpr uint16 POSITION            = 0x0100;
    inline constexpr uint16 ROTATION            = 0x0200;
}
