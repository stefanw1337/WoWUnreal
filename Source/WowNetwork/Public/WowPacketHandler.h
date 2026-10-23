#pragma once
#include "CoreMinimal.h"
#include "WowOpcodes.h"
#include "WowNpcServices.h"
#include "WowRealmClock.h"
#include "WowEntityManager.h"
#include "WowWardenHandler.h"

DECLARE_MULTICAST_DELEGATE_FiveParams(FOnLoginVerifyWorld, uint32 /*MapId*/, float /*X*/, float /*Y*/, float /*Z*/, float /*Orientation*/);
DECLARE_MULTICAST_DELEGATE_SixParams(FOnChatMessage, uint8 /*Type*/, uint32 /*Language*/, uint64 /*SenderGuid*/, const FString& /*SenderName*/, const FString& /*Message*/, const FString& /*Channel*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FOnSpellStart, uint64 /*CasterGuid*/, uint32 /*SpellId*/, uint32 /*CastFlags*/, int32 /*CastTime*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FOnSpellGo, uint64 /*CasterGuid*/, uint64 /*FirstHitTargetGuid*/, uint32 /*SpellId*/, uint32 /*CastFlags*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnSpellHitTargets, uint64, uint32, const TArray<uint64>&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnSpellFailure, uint64 /*CasterGuid*/, uint32 /*SpellId*/, uint8 /*FailureReason*/);
/** SMSG_CAST_FAILED: our cast was refused before it started (out of range, not fishable water, ...). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCastFailed, uint32 /*SpellId*/, uint8 /*SpellCastResult*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnChannelStart, uint64 /*CasterGuid*/, uint32 /*SpellId*/, uint32 /*DurationMs*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChannelUpdate, uint64 /*CasterGuid*/, uint32 /*RemainingMs*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSpellDelayed, uint64 /*CasterGuid*/, uint32 /*DelayMs*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSpellCooldown, uint32 /*SpellId*/, float /*Duration*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FOnAttackerStateUpdate, uint64 /*AttackerGuid*/, uint64 /*TargetGuid*/, uint32 /*HitInfo*/, uint32 /*Damage*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAttackStart, uint64 /*AttackerGuid*/, uint64 /*VictimGuid*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAttackStop, uint64 /*AttackerGuid*/, uint64 /*VictimGuid*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FOnLootOpened, uint64 /*LootGuid*/, uint8 /*LootType*/, uint32 /*Gold*/, const TArray<FWowLootItem>& /*Items*/);
DECLARE_MULTICAST_DELEGATE(FOnLootClosed);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLootSlotRemoved, uint8);
DECLARE_MULTICAST_DELEGATE(FOnLootMoneyCleared);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVendorOpened, uint64 /*VendorGuid*/, const TArray<FWowVendorItem>& /*Items*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMailListReceived, const TArray<FWowMailMessage>& /*Mail*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMailboxShown, uint64 /*MailboxGuid*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FOnMailResult, uint32 /*MailId*/, uint32 /*Action*/, uint32 /*Error*/, uint32 /*EquipError*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnTradeUpdated, const FWowTradeState& /*TradeState*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDuelUpdated, const FWowDuelState& /*DuelState*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPetBarUpdated, const FWowPetActionBarState& /*PetActionBar*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBankOpened, uint64 /*BankerGuid*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnQuestDialog, const FWowQuestDetails& /*QuestDetails*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnQuestProgressDialog, const FWowQuestDetails& /*QuestDetails*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnQuestRewardDialog, const FWowQuestDetails& /*QuestDetails*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnQuestGiverStatus, uint64 /*QuestGiverGuid*/, uint8 /*Status*/);
DECLARE_DELEGATE_TwoParams(FOnSendPacket, uint32 /*Opcode*/, const TArray<uint8>& /*Data*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpcodeReceived, uint16 /*Opcode*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnQuestAccepted, uint32 /*QuestId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnQuestComplete, uint32 /*QuestId*/);
DECLARE_MULTICAST_DELEGATE(FOnQuestLogUpdated);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnUiMessage,const FString&);
DECLARE_MULTICAST_DELEGATE(FOnQuestWatchChanged);
DECLARE_MULTICAST_DELEGATE(FOnTalentsUpdated);
DECLARE_MULTICAST_DELEGATE(FOnFriendListUpdated);
DECLARE_MULTICAST_DELEGATE(FOnWhoListUpdated);
DECLARE_MULTICAST_DELEGATE(FOnGuildRosterUpdated);
/** r285: guild bank changes, as the Lua event to fire (GUILDBANKBAGSLOTS_CHANGED ...) and its argument (-1 = none). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGuildBankEvent, const FString& /*LuaEvent*/, int32 /*Argument*/);

// r285 (Stefan 13.10: guild vaults need the WotLK window): what SMSG_GUILD_BANK_LIST, MSG_GUILD_PERMISSIONS,
// MSG_GUILD_BANK_MONEY_WITHDRAWN, MSG_GUILD_BANK_LOG_QUERY and MSG_QUERY_GUILD_BANK_TEXT told us.
struct FWowGuildBankItem
{
    uint32 Entry = 0;
    int32 Count = 0;
    int32 RandomPropertyId = 0;
    int32 EnchantId = 0;
};
struct FWowGuildBankTab
{
    FString Name;
    FString Icon;
    TMap<uint8, FWowGuildBankItem> Items; // by slot 0-97
    int32 WithdrawalsRemaining = 0;       // -1 = unlimited
    FString Text;
};
struct FWowGuildBankLogEntry
{
    int8 Type = 0; // 1 deposit, 2 withdraw, 3/7 move item, 4 deposit money, 5 withdraw money, 6 repair, 8 withdraw for tab, 9 buy tab
    uint64 Player = 0;
    uint32 Entry = 0;
    uint32 Count = 0;
    uint8 OtherTab = 0;
    uint32 Money = 0;
    uint32 SecondsAgo = 0;
};
struct FWowGuildBankState
{
    uint64 Money = 0;
    TArray<FWowGuildBankTab> Tabs;           // purchased tabs, server order
    bool bPermissionsKnown = false;
    uint32 RankId = 0;
    uint32 RankRights = 0;                   // GR_RIGHT_*
    int32 WithdrawGoldPerDay = 0;
    int32 PurchasedTabs = 0;
    uint32 TabRights[6] = {0, 0, 0, 0, 0, 0}; // GUILD_BANK_RIGHT_*: 1 view, 2 deposit, 4 edit text
    int32 TabWithdrawLimit[6] = {0, 0, 0, 0, 0, 0};
    int32 RemainingWithdrawMoney = 0;        // -1 = unlimited
    TMap<uint8, TArray<FWowGuildBankLogEntry>> Logs; // server tab 0-5, 6 = money log
};
DECLARE_MULTICAST_DELEGATE(FOnGroupUpdated);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnRaidTargetsUpdated, const FWowRaidTargetState& /*RaidTargets*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnReadyCheckUpdated, const FWowReadyCheckState& /*ReadyCheck*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGroupInviteReceived, const FString& /*InviterName*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPartyCommandResult, uint8 /*Command*/, const FString& /*PlayerName*/, uint8 /*Result*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTaxiNodesShown, uint64 /*NpcGuid*/, uint32 /*CurrentNodeId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnTaxiActivateReply, uint8 /*Result*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(FOnTeleportRequest, uint64 /*Guid*/, uint32 /*Flags*/, uint32 /*Time*/, FVector /*Position*/, float /*Orientation*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(FOnMapTransfer, uint32 /*MapId*/, float /*X*/, float /*Y*/, float /*Z*/, float /*Orientation*/);
DECLARE_MULTICAST_DELEGATE(FOnPlayerDeath);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntityDeath, uint64 /*Guid*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCorpseReclaimDelay, float /*DelayInSeconds*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnResurrectRequest, const FString& /*RequesterName*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSpiritHealerConfirm, uint64 /*HealerGuid*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FOnPlayerTeleport, uint32 /*MapId*/, float /*X*/, float /*Y*/, float /*Z*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAurasChanged, uint64 /*Guid*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPlayerNameReceived, uint64 /*Guid*/, const FString& /*Name*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnCreatureNameReceived, uint32 /*Entry*/, const FString& /*Name*/, const FString& /*Title*/);
DECLARE_MULTICAST_DELEGATE(FOnPlayerInventoryUpdate);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnUnitHealthUpdate, uint64 /*EntityGuid*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnUnitPowerUpdate, uint64 /*EntityGuid*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEmote, uint64 /*EntityGuid*/, uint32 /*EmoteId*/);
// Server-scripted sounds (boss/NPC voice lines, events): SoundEntries id, source object (0 = 2D/global).
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPlayServerSound, uint32 /*SoundEntryId*/, uint64 /*SourceGuid*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameObjectCustomAnim, uint64 /*GameObjectGuid*/, uint32 /*AnimId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlayServerMusic, uint32 /*SoundEntryId*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAIReaction, uint64 /*UnitGuid*/, uint32 /*Reaction: 2 = hostile (aggro)*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FOnTextEmote, uint64 /*SenderGuid*/, uint32 /*TextEmoteId (EmotesText.dbc)*/, uint32 /*EmoteNum*/, const FString& /*TargetName*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelUp, uint32 /*NewLevel*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnXPGain, uint32 /*Amount*/, uint8 /*Type*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnKillXPGain, uint32 /*Amount*/, uint32 /*RestedBonus*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(FOnBindPointUpdate, float /*X*/, float /*Y*/, float /*Z*/, uint32 /*MapId*/, uint32 /*AreaId*/);
// r527: SMSG_BINDER_CONFIRM (innkeeper asks "make this inn your home?") and SMSG_PLAYERBOUND.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBinderConfirm, uint64 /*InnkeeperGuid*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPlayerBound, uint64 /*InnkeeperGuid*/, uint32 /*AreaId*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnWeatherUpdate, uint32 /*WeatherState*/, float /*Grade*/, uint8 /*Abrupt*/);
DECLARE_MULTICAST_DELEGATE_SixParams(FOnMirrorTimerStarted, uint32 /*TimerId*/, int32 /*ValueMs*/,
    int32 /*MaxValueMs*/, int32 /*Scale*/, bool /*Paused*/, uint32 /*SpellId*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMirrorTimerPaused, uint32 /*TimerId*/, bool /*Paused*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMirrorTimerStopped, uint32 /*TimerId*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnWorldStatesInit, uint32 /*MapId*/, uint32 /*AreaId*/, uint32 /*SubAreaId*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnWorldStateUpdate, uint32 /*Field*/, uint32 /*Value*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnProficiencySet, uint8 /*ItemClass*/, uint32 /*ItemSubclassMask*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGossipMessage, const FWowGossipMenu& /*Menu*/);
DECLARE_MULTICAST_DELEGATE(FOnGossipComplete);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNpcText, uint32, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnInitialSpells, const TArray<uint32>& /*SpellIds*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnKnownSpellsChanged, bool /*bLearned*/);
DECLARE_MULTICAST_DELEGATE(FOnActionButtonsUpdated);
DECLARE_MULTICAST_DELEGATE(FOnLfgUpdated);

/** Server-forced change to the local mover that the client must apply and acknowledge. */
enum class EWowForcedMoveKind : uint8
{
    Speed,
    Root,
    Unroot,
    KnockBack,
    WaterWalk,
    LandWalk,
    FeatherFall,
    NormalFall,
    Hover,
    Unhover,
    SetCanFly,
    UnsetCanFly,
};

enum class EWowSpeedType : uint8
{
    Walk, Run, RunBack, Swim, SwimBack, TurnRate, Flight, FlightBack, PitchRate,
};

struct FWowForcedMovement
{
    EWowForcedMoveKind Kind = EWowForcedMoveKind::Speed;
    EWowSpeedType SpeedType = EWowSpeedType::Run;
    uint16 AckOpcode = 0;
    uint32 Counter = 0;
    float Speed = 0.0f;
    // Knockback: direction in WoW world space, horizontal speed and vertical speed (yards/s, + = up).
    float KnockCos = 1.0f;
    float KnockSin = 0.0f;
    float KnockSpeedXY = 0.0f;
    float KnockSpeedZ = 0.0f;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FOnForcedMovement, const FWowForcedMovement&);

/** One server combat event, already mapped to its 3.3.5 COMBAT_LOG_EVENT sub-event name. */
/** SMSG_MIRRORIMAGE_DATA: a unit drawn as a dressed player character (UNIT_FLAG2_MIRROR_IMAGE).
 *  Custom NPC outfits (CreatureOutfits on AzerothCore/Torment) and Mirror Image use it. */
struct FWowMirrorImageData
{
    uint64 Guid = 0;
    uint32 DisplayId = 0;
    bool bHasAppearance = false;
    uint8 Race = 0, Gender = 0, Class = 0, Skin = 0, Face = 0, Hair = 0, HairColor = 0, FacialHair = 0;
    /** Head, shoulders, shirt, chest, waist, legs, feet, wrists, hands, back, tabard (display ids). */
    uint32 ItemDisplay[11] = {};
};
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMirrorImageData, const FWowMirrorImageData&);

struct FWowCombatLogEntry
{
    FString Event;              // SWING_DAMAGE, SPELL_DAMAGE, SPELL_PERIODIC_HEAL, SPELL_AURA_APPLIED, ...
    uint64 SourceGuid = 0;
    uint64 DestGuid = 0;
    uint32 SpellId = 0;         // 0 for swing / environmental / kill events
    uint8 School = 1;           // spell school mask
    int32 Amount = 0;
    int32 Overkill = 0;         // damage: overkill, heal: overhealing
    int32 Resisted = 0;
    int32 Blocked = 0;
    int32 Absorbed = 0;
    int32 ExtraAmount = 0;      // leech: amount gained
    int32 PowerType = 0;        // energize / leech
    bool bCritical = false;
    bool bGlancing = false;
    bool bCrushing = false;
    FString MissType;           // *_MISSED: MISS, DODGE, PARRY, BLOCK, RESIST, ABSORB, IMMUNE, EVADE, DEFLECT, REFLECT
    FString EnvironmentalType;  // ENVIRONMENTAL_DAMAGE: FALLING, DROWNING, FATIGUE, FIRE, LAVA, SLIME
    FString AuraType;           // SPELL_AURA_*: BUFF or DEBUFF
};
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCombatLog, const FWowCombatLogEntry&);

// Simple byte-stream reader for packet payloads
struct FPacketReader
{
    const uint8* Data = nullptr;
    int32 Size = 0;
    int32 Pos = 0;
    bool bFailed = false;
    FString FailureReason;

    FPacketReader(const TArray<uint8>& InData) : Data(InData.GetData()), Size(InData.Num()) {}
    FPacketReader(const uint8* InData, int32 InSize)
        : Data(InData), Size(FMath::Max(0, InSize))
    {
        if (InSize < 0 || (InSize > 0 && InData == nullptr))
        {
            Fail(TEXT("Invalid packet buffer"));
        }
    }

    bool CanRead(int32 Bytes) const
    {
        return !bFailed && Bytes >= 0 && Pos >= 0 && Pos <= Size && Bytes <= Size - Pos;
    }
    int32 Remaining() const { return FMath::Max(0, Size - Pos); }
    bool HasFailed() const { return bFailed; }
    const FString& GetFailureReason() const { return FailureReason; }

    void Fail(const FString& Reason)
    {
        if (!bFailed)
        {
            bFailed = true;
            FailureReason = Reason;
        }
    }

    bool Require(int32 Bytes, const TCHAR* FieldName)
    {
        if (CanRead(Bytes))
        {
            return true;
        }

        Fail(FString::Printf(TEXT("Required field '%s' needs %d bytes at offset %d, only %d remain"),
            FieldName ? FieldName : TEXT("packet field"), Bytes, Pos, Remaining()));
        return false;
    }

    uint8 ReadU8()
    {
        if (!Require(1, TEXT("uint8"))) return 0;
        return Data[Pos++];
    }
    uint16 ReadU16()
    {
        if (!Require(2, TEXT("uint16"))) return 0;
        uint16 V = Data[Pos] | (uint16(Data[Pos + 1]) << 8);
        Pos += 2;
        return V;
    }
    uint32 ReadU32()
    {
        if (!Require(4, TEXT("uint32"))) return 0;
        uint32 V;
        FMemory::Memcpy(&V, Data + Pos, 4);
        Pos += 4;
        return V;
    }
    uint64 ReadU64()
    {
        if (!Require(8, TEXT("uint64"))) return 0;
        uint64 V;
        FMemory::Memcpy(&V, Data + Pos, 8);
        Pos += 8;
        return V;
    }
    float ReadFloat()
    {
        if (!Require(4, TEXT("float"))) return 0.0f;
        float V;
        FMemory::Memcpy(&V, Data + Pos, 4);
        Pos += 4;
        return V;
    }
    FString ReadCString()
    {
        if (bFailed)
        {
            return FString();
        }

        const int32 Start = Pos;
        while (Pos < Size && Data[Pos] != 0) ++Pos;
        if (Pos >= Size)
        {
            Pos = Start;
            Fail(FString::Printf(TEXT("Unterminated UTF-8 string at offset %d"), Start));
            return FString();
        }

        const int32 ByteLength = Pos - Start;
        FString Result;
        if (ByteLength > 0)
        {
            const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data + Start), ByteLength);
            Result = FString(Converted.Length(), Converted.Get());
        }
        ++Pos; // skip null terminator validated above
        return Result;
    }
    /** Null-terminated string kept byte for byte (each byte -> one TCHAR 0..255).
     *  Addon messages carry binary payloads (AIO's compressed code) that a UTF-8
     *  decode would corrupt. */
    FString ReadCStringBytes()
    {
        if (bFailed)
        {
            return FString();
        }
        const int32 Start = Pos;
        while (Pos < Size && Data[Pos] != 0) ++Pos;
        if (Pos >= Size)
        {
            Pos = Start;
            Fail(FString::Printf(TEXT("Unterminated string at offset %d"), Start));
            return FString();
        }
        FString Result;
        Result.Reserve(Pos - Start);
        for (int32 Index = Start; Index < Pos; ++Index)
        {
            Result.AppendChar(static_cast<TCHAR>(Data[Index]));
        }
        ++Pos;
        return Result;
    }
    void Skip(int32 Bytes)
    {
        if (!Require(Bytes, TEXT("skipped bytes"))) return;
        Pos += Bytes;
    }

    // Read packed GUID (variable-length encoding)
    uint64 ReadPackedGuid()
    {
        uint8 Mask = ReadU8();
        if (bFailed) return 0;
        uint64 Guid = 0;
        for (int32 i = 0; i < 8; ++i)
        {
            if (Mask & (1 << i))
            {
                const uint8 Byte = ReadU8();
                if (bFailed) return 0;
                Guid |= static_cast<uint64>(Byte) << (i * 8);
            }
        }
        return Guid;
    }
};

/** One SMSG_WHO result row. */
struct FWowWhoEntry
{
    FString Name;
    FString Guild;
    uint32 Level = 0;
    uint32 Class = 0;
    uint32 Race = 0;
    uint32 Zone = 0;
};

/** One SMSG_QUEST_POI_QUERY_RESPONSE region: objective area polygon on a world map area/floor. */
struct FWowQuestPOI
{
    uint32 Id = 0;
    int32 ObjectiveIndex = -1; // -1 = turn-in / quest giver
    uint32 MapId = 0;
    uint32 WorldMapAreaId = 0;
    uint32 Floor = 0;
    TArray<FVector2D> Points; // world X/Y
};

/** Death / ghost / resurrection state shared by the gameplay controller and the Lua API
 *  (RepopMe, GetReleaseTimeRemaining, GetCorpseRecoveryDelay, AcceptResurrect, AcceptXPLoss ...). */
struct FWowDeathState
{
    bool bDead = false;            // corpse, not released
    bool bGhost = false;           // PLAYER_FLAGS_GHOST
    double DeathTime = 0.0;        // FPlatformTime::Seconds() at death
    bool bReleaseSent = false;
    double ReclaimReadyTime = 0.0; // SMSG_CORPSE_RECLAIM_DELAY
    uint32 CurrentMap = 0;
    bool bHasCorpse = false;
    uint32 CorpseMap = 0;
    FVector CorpsePos = FVector::ZeroVector; // WoW world coordinates (yards)
    bool bCorpseInRange = false;
    uint64 ResurrecterGuid = 0;    // SMSG_RESURRECT_REQUEST
    FString ResurrecterName;
    bool bResurrectSickness = false;
    bool bResurrectTimer = true;
    uint64 SpiritHealerGuid = 0;   // SMSG_SPIRIT_HEALER_CONFIRM
    uint64 AreaSpiritHealerGuid = 0;
    double AreaSpiritHealerReadyTime = 0.0; // SMSG_AREA_SPIRIT_HEALER_TIME
    bool bAreaSpiritHealerInRange = false;
};

/** r240: SMSG_CALENDAR_SEND_CALENDAR (3.3.5) - the player's invites and events, bound instances and the
 *  raid reset schedule. Times are unix seconds; packed event times are kept as sent. */
struct FWowCalendarInvite
{
    uint64 EventId = 0;
    uint64 InviteId = 0;
    uint8 Status = 0;     // 0 invited, 1 accepted, 2 declined, 3 confirmed, 4 out, 5 standby, 6 signed up, 7 not signed up, 8 tentative
    uint8 Rank = 0;       // 0 player, 1 moderator, 2 owner
    bool bGuildEvent = false;
    uint64 CreatorGuid = 0;
};
struct FWowCalendarEvent
{
    uint64 EventId = 0;
    FString Title;
    uint32 Type = 0;      // 0 raid, 1 dungeon, 2 PvP, 3 meeting, 4 other
    uint32 PackedTime = 0;
    uint32 Flags = 0;
    int32 DungeonId = -1;
    uint64 CreatorGuid = 0;
};
struct FWowCalendarLockout
{
    uint32 MapId = 0;
    uint32 Difficulty = 0;
    int64 ResetUnix = 0;  // received time + seconds left
    uint64 InstanceId = 0;
};
struct FWowCalendarRaidReset
{
    uint32 MapId = 0;
    int32 Period = 0;     // seconds between resets
    int32 Offset = 0;     // seconds after BaseUnix
};
struct FWowCalendarState
{
    bool bReceived = false;
    uint32 Revision = 0;
    TArray<FWowCalendarInvite> Invites;
    TArray<FWowCalendarEvent> Events;
    int64 ServerUnix = 0;
    uint32 ServerPackedTime = 0;  // the realm's local time, packed
    int64 BaseUnix = 0;           // reset schedule origin (the client's constant 2005-12-28)
    TArray<FWowCalendarLockout> Lockouts;
    TArray<FWowCalendarRaidReset> RaidResets;
    int32 NumPendingInvites = 0;
};

class WOWNETWORK_API FWowPacketHandler
{
public:
    /** SMSG_INSPECT_TALENT (answer to CMSG_INSPECT): talents of the active spec, glyphs, equipped items with enchants. */
    struct FInspectData
    {
        uint64 Guid = 0;
        uint32 FreeTalentPoints = 0;
        TMap<uint32, uint8> TalentRanks;    // talent id -> points (rank + 1)
        TArray<uint16> Glyphs;
        uint32 ItemEntries[19] = {};
        TArray<uint16> ItemEnchants[19];    // index = enchantment slot (0 permanent, 1 temporary, 2-4 sockets, 5 bonus, 6 prismatic, 7-11 random)
        uint32 Revision = 0;
    };
    FInspectData InspectData;
    void HandleInspectTalent(FPacketReader&);
    /** Your achievements: id -> packed completion date (SMSG_ALL_ACHIEVEMENT_DATA, SMSG_ACHIEVEMENT_EARNED)
     *  and criteria progress counters (SMSG_CRITERIA_UPDATE). */
    TMap<uint32, uint32> CompletedAchievements;
    TMap<uint32, uint64> AchievementCriteria;
    uint32 AchievementRevision = 0;
    void HandleAllAchievementData(FPacketReader&);
    void HandleAchievementEarned(FPacketReader&);
    void HandleCriteriaUpdate(FPacketReader&);
public:
    FWowPacketHandler();
    FSimpleMulticastDelegate OnNpcServiceOpened;
    uint64 TrainerGuid=0, AuctionGuid=0;
    uint32 ServiceRevision=0, AuctionTotal=0;
    bool AuctionOpen=false;
    FString ServiceMessage;
    TArray<FWowTrainerOffer> TrainerOffers;
    TArray<FWowAuctionOffer> AuctionOffers;
    /** Your own auctions (Auctions tab) and the auctions you bid on (Bids tab). */
    TArray<FWowAuctionOffer> AuctionOwnOffers, AuctionBidOffers;
    uint32 AuctionOwnTotal=0, AuctionBidTotal=0;
    uint32 LastAuctionAction=0, LastAuctionError=0, AuctionResultCount=0;
    /** Bumped when each list arrives (AUCTION_ITEM_LIST_UPDATE / _BIDDER_ / _OWNED_LIST_UPDATE). */
    uint32 AuctionListRevision=0, AuctionBidRevision=0, AuctionOwnRevision=0;
    void HandleAuctionOwnerList(FPacketReader&);
    void HandleAuctionBidderList(FPacketReader&);
    void HandleTrainerList(FPacketReader&);
    void HandleTrainerSuccess(FPacketReader&);
    void HandleTrainerFailure(FPacketReader&);
    void HandleAuctionHello(FPacketReader&);
    void HandleAuctionList(FPacketReader&);
    void HandleAuctionResult(FPacketReader&);

    /** Handle an incoming server packet (called from game thread) */
    void HandlePacket(uint16 Opcode, const TArray<uint8>& Data);

    /** Entity manager — tracks all objects in the world */
    FWowEntityManager EntityManager;

    /** Known spell IDs (populated from SMSG_INITIAL_SPELLS) */
    TSet<uint32> KnownSpells;

    /** Quest log entries */
    TArray<FWowQuestLogEntry> QuestLog;

    /** Latest server-authoritative marker state for visible quest givers. */
    TMap<uint64, uint8> QuestGiverStatuses;
    /** Quest titles learned from gossip/list/detail packets during this session. */
    TMap<uint32, FString> QuestTitleCache;
    TMap<uint32, FString> QuestObjectiveTextCache;
    TMap<uint32, FWowQuestLogEntry> QuestTemplateCache;
    TSet<uint32> PendingQuestTemplateQueries;

    /** Quest POI regions per quest (CMSG_QUEST_POI_QUERY), shown on the world map and minimap. */
    TMap<uint32, TArray<FWowQuestPOI>> QuestPOIs;
    TSet<uint32> QuestPOIRequested;
    /** Quest selected on the world map (WORLDMAP_SETTINGS.selectedQuestId); its area is drawn and tracked on the minimap. */
    uint32 SuperTrackedQuestId = 0;
    /** Query POIs for quest log entries that have none yet (max 25 per packet, like the client). */
    void RequestQuestPOIs();

    /** Known talents of the active spec (Rank = number of learned ranks). */
    TArray<FWowTalentInfo> Talents;
    /** Every spec from SMSG_TALENTS_INFO (dual spec); Talents mirrors ActiveTalentGroup. */
    TArray<TArray<FWowTalentInfo>> TalentGroups;
    /** Glyph ids per spec (6 slots each). */
    TArray<TArray<uint16>> TalentGlyphs;
    uint8 ActiveTalentGroup = 0;
    uint32 UnspentTalentPoints = 0;
    bool bTalentInfoReceived = false;
    /** Incremented on every SMSG_TALENTS_INFO. */
    uint32 TalentRevision = 0;
    /** Hunter pet talents (same packet with the pet flag set). */
    TArray<FWowTalentInfo> PetTalents;
    uint32 PetUnspentTalentPoints = 0;

    /** Warden anti-cheat handler */
    FWowWardenHandler WardenHandler;

    /** Action bar data (144 slots, first 12 are main action bar) */
    TArray<uint32> ActionButtons;

    /** r240: calendar data (OpenCalendar / RequestCalendar) */
    FWowCalendarState Calendar;
    void RequestCalendar();

    /** Spell cooldowns (SpellId -> expiry time as FPlatformTime::Seconds()) */
    TMap<uint32, double> SpellCooldowns;
    struct FCooldownWindow { double Start=0; float Duration=0; };
    TMap<uint32, FCooldownWindow> SpellCooldownWindows;
    TMap<uint32, FCooldownWindow> GlobalCooldownWindows;
    uint32 LastGlobalSpell=0;
    uint8 LastGlobalCounter=0;
    double LastGlobalCast=-100;
    void StartGlobalCooldown(uint64 Caster, uint32 Spell, uint8 Counter);
    void RecordSpellCooldown(uint32 Spell, float Duration);
    FCooldownWindow GetVisibleSpellCooldown(uint32 Spell) const;

    /** Player name cache */
    TMap<uint64, FString> PlayerNameCache;
    TMap<uint64, uint8> PlayerGenderCache;
    TMap<uint64, uint8> PlayerClassCache;

    /** Creature name cache */
    TMap<uint32, uint32> GameObjectLocks;
    /** Game object names from SMSG_GAMEOBJECT_QUERY_RESPONSE (world mouseover tooltip). */
    TMap<uint32, FString> GameObjectNames;
    /** GAMEOBJECT_TYPE_TEXT (9): first page (data0) and page material (data2, PageTextMaterial.dbc). */
    TMap<uint32, uint32> GameObjectPageTexts;
    TMap<uint32, uint32> GameObjectPageMaterials;
    /** Page texts from SMSG_PAGE_TEXT_QUERY_RESPONSE. */
    struct FWowPageText { FString Text; uint32 NextPage = 0; };
    TMap<uint32, FWowPageText> PageTexts;
    /** The ItemTextFrame being read (ItemTextGet* Lua API); the controller fires the pending events. */
    struct FWowItemTextState
    {
        bool bOpen = false;
        uint64 SourceGuid = 0;
        FString Title;
        FString Material;
        uint32 PageId = 0;
        TArray<uint32> History;
        bool bBeginPending = false;
        bool bReadyPending = false;
        bool bClosedPending = false;
    };
    FWowItemTextState ItemText;
    void RequestPageText(uint32 PageId);
    void ItemTextTurnPage(int32 Delta);
    void CloseItemText();
    TFunction<void(uint32)> OnGameObjectInfo;
    TMap<uint32, FString> CreatureNameCache;
    TMap<uint32, FString> CreatureTitleCache;
    /** SMSG_CREATURE_QUERY_RESPONSE metadata (type, family, rank, cursor icon, type flags). */
    struct FWowCreatureTemplate
    {
        FString IconName;
        uint32 TypeFlags = 0;
        uint32 CreatureType = 0;   // 1 Beast, 2 Dragonkin, 3 Demon, 4 Elemental, 5 Giant, 6 Undead, 7 Humanoid, 8 Critter, 9 Mechanical, 10 Not specified, 11 Totem, 12 Non-combat Pet, 13 Gas Cloud
        uint32 Family = 0;         // CreatureFamily.dbc id
        uint32 Rank = 0;           // 0 normal, 1 elite, 2 rare elite, 3 boss, 4 rare
        uint32 QuestItems[6] = {0, 0, 0, 0, 0, 0};
        uint32 DisplayIds[4] = {0, 0, 0, 0}; // r300: CreatureDisplayInfo ids (mount preview)
    };
    TMap<uint32, FWowCreatureTemplate> CreatureTemplateCache;

    /** World states (field -> value) */
    TMap<uint32, uint32> WorldStates;

    /** ── Social System ──────────────────────────────────────────────────── */
    /** Friends list */
    TArray<FWowFriendInfo> FriendsList;

    /** Last SMSG_WHO result (WhoFrame list) and the server's total match count. */
    TArray<FWowWhoEntry> WhoResults;
    uint32 WhoTotalCount = 0;
    /** Set when the Who window sent the query: results go to its list instead of chat. */
    bool bWhoForFrame = false;

    /** Guild roster */
    TArray<FWowGuildMember> GuildRoster;
    /** r285: rank rights from SMSG_GUILD_ROSTER (index = rank id). */
    struct FWowGuildRankRights { uint32 Rights = 0; uint32 GoldPerDay = 0; uint32 TabRights[6] = {0, 0, 0, 0, 0, 0}; uint32 TabSlots[6] = {0, 0, 0, 0, 0, 0}; };
    TArray<FWowGuildRankRights> GuildRanks;
    /** r285: the guild bank. */
    FWowGuildBankState GuildBank;
    FOnGuildBankEvent OnGuildBankEvent;
    /** r317: rogue / cat combo points (SMSG_UPDATE_COMBO_POINTS) and the unit they are on. */
    uint64 ComboTargetGuid = 0;
    uint8 ComboPoints = 0;

    /** Guild information */
    FString GuildName;
    FString ResolveGuildName(uint32 GuildId);
    TMap<uint32,FString> GuildNames;
    /** r343: guild tabard designs (FWowCharacterTexture::MakeGuildTabard packing) by guild id;
     *  static so character builders and the pawn can read them; revision bumps on each change. */
    static uint32 GetGuildTabardDesign(uint32 GuildId);
    static uint32 GetGuildTabardRevision();
    TMap<uint32,double> GuildQueryTimes;
    double NextGuildNameQuery=0;
    FString GuildMotd;

    /** Group/Party information */
    FWowGroupInfo GroupInfo;

    /** Active raid target icon state */
    FWowRaidTargetState RaidTargets;

    /** Active ready check state */
    FWowReadyCheckState ReadyCheck;

    /** Taxi system data */
    FWowTaxiData TaxiData;

    /** Current mailbox contents */
    uint64 CurrentMailboxGuid = 0;
    uint32 MailInboxTotalCount = 0;
    TArray<FWowMailMessage> MailInbox;

    /** Current trade window state */
    FWowTradeState CurrentTrade;

    /** Current duel state */
    FWowDuelState CurrentDuel;

    /** Current pet action bar state */
    FWowPetActionBarState PetActionBar;

    /** Wrath Dungeon Finder and Looking For Raid browser state. */
    FWowLfgState LfgState;

    // Events
    FOnLoginVerifyWorld OnLoginVerifyWorld;
    FOnChatMessage OnChatMessage;
    FOnSpellStart OnSpellStart;
    FOnSpellGo OnSpellGo;
    FOnSpellHitTargets OnSpellHitTargets;
    FOnSpellFailure OnSpellFailure;
    FOnCastFailed OnCastFailed;
    FOnChannelStart OnChannelStart;
    FOnChannelUpdate OnChannelUpdate;
    FOnSpellDelayed OnSpellDelayed;
    FOnSpellCooldown OnSpellCooldown;
    FOnAttackerStateUpdate OnAttackerStateUpdate;
    FOnCombatLog OnCombatLog;
    FOnAttackStart OnAttackStart;
    FOnAttackStop OnAttackStop;
    FOnLootOpened OnLootOpened;
    FOnLootClosed OnLootClosed;
    FOnLootSlotRemoved OnLootSlotRemoved;
    FOnLootMoneyCleared OnLootMoneyCleared;
    uint64 ActiveLootGuid = 0;
    FOnVendorOpened OnVendorOpened;
    TMap<uint32, FWowItemTemplateInfo> ItemTemplates;
    TMap<uint32,double> PendingItemQueries;
    TSet<uint32> UnavailableItemQueries;
    void RequestItemTemplate(uint32 Entry);
    TArray<FWowVendorItem> VendorItems;
    uint64 ActiveVendorGuid = 0;
    uint32 VendorRevision = 0;
    FString VendorMessage;
    FOnMailListReceived OnMailListReceived;
    FOnMailboxShown OnMailboxShown;
    /** SMSG_SEND_MAIL_RESULT (action: 0 send, 1 money taken, 2 item taken, 3 returned, 4 deleted, 5 copied). */
    FOnMailResult OnMailResult;
    /** SMSG_RECEIVED_MAIL: new mail arrived while online. */
    FSimpleMulticastDelegate OnNewMailReceived;
    /** Client text for a WotLK InventoryResult (GlobalStrings ERR_*); empty for 0. */
    static FString InventoryErrorText(uint8 ErrorCode, uint32 RequiredLevel = 0);
    FOnTradeUpdated OnTradeUpdated;
    FOnDuelUpdated OnDuelUpdated;
    FOnPetBarUpdated OnPetBarUpdated;
    FOnBankOpened OnBankOpened;
    FOnQuestDialog OnQuestDialog;
    FOnQuestProgressDialog OnQuestProgressDialog;
    FOnQuestRewardDialog OnQuestRewardDialog;
    FOnQuestGiverStatus OnQuestGiverStatus;
    FOnQuestAccepted OnQuestAccepted;
    FOnQuestComplete OnQuestComplete;
    FOnQuestLogUpdated OnQuestLogUpdated;
    FOnUiMessage OnUiErrorMessage;
    FOnUiMessage OnUiInfoMessage;
    FOnQuestWatchChanged OnQuestWatchChanged;
    FOnTalentsUpdated OnTalentsUpdated;
    FOnFriendListUpdated OnFriendListUpdated;
    FOnWhoListUpdated OnWhoListUpdated;
    FOnGuildRosterUpdated OnGuildRosterUpdated;
    FOnGroupUpdated OnGroupUpdated;
    FOnRaidTargetsUpdated OnRaidTargetsUpdated;
    FOnReadyCheckUpdated OnReadyCheckUpdated;
    FOnGroupInviteReceived OnGroupInviteReceived;
    FOnPartyCommandResult OnPartyCommandResult;
    FOnTaxiNodesShown OnTaxiNodesShown;
    FOnTaxiActivateReply OnTaxiActivateReply;
    FOnTeleportRequest OnTeleportRequest;
    FOnForcedMovement OnForcedMovement;
    /** Chat tag of the message being broadcast through OnChatMessage (1 AFK, 2 DND, 4 GM). */
    uint8 LastChatTag = 0;
    /** Joined chat channels; index + 1 is the /number shown by the original client. */
    TArray<FString> JoinedChannels;
    /** ChatChannels.dbc id per slot (0 for custom channels); a zone change reuses the slot. */
    TArray<uint32> JoinedChannelIds;
    // Hearthstone home (SMSG_BINDPOINTUPDATE), AreaTable id; tooltips show it for $z.
    uint32 BindAreaId = 0;
    // r527: innkeeper waiting for our answer to SMSG_BINDER_CONFIRM (0 = none). Accepting sends
    // CMSG_BINDER_ACTIVATE (0x01B5) with this GUID; the server then has the innkeeper cast Bind (3286)
    // on us and sends SMSG_PLAYERBOUND + SMSG_BINDPOINTUPDATE.
    uint64 PendingBinderGuid = 0;
    // r538: one constant per declaration (MSVC C2487 on the 2nd+ declarator of an exported class).
    static constexpr uint16 SMSG_BINDER_CONFIRM = 0x02EB;
    static constexpr uint16 CMSG_BINDER_ACTIVATE = 0x01B5;
    static constexpr uint16 SMSG_PLAYERBOUND = 0x0158;
    // r340: reputation. Index = Faction.dbc ReputationListID; Standing is the server's value on top of
    // the faction's race/class base (Faction.dbc ReputationBase), as AzerothCore's ReputationMgr sends it.
    struct FFactionState { uint8 Flags = 0; int32 Standing = 0; };
    TArray<FFactionState> FactionStates;
    uint32 FactionRevision = 0;
    static constexpr uint16 SMSG_INITIALIZE_FACTIONS = 0x0122;
    static constexpr uint16 SMSG_SET_FACTION_VISIBLE = 0x0123;
    static constexpr uint16 SMSG_SET_FACTION_STANDING = 0x0124;
    static constexpr uint16 CMSG_SET_FACTION_ATWAR = 0x0125;
    static constexpr uint16 CMSG_SET_FACTION_INACTIVE = 0x0317;
    static constexpr uint16 CMSG_SET_WATCHED_FACTION = 0x0318;
    int32 GetChannelNumber(const FString& ChannelName) const;
    FString GetChannelNameByNumber(int32 Number) const;
    FOnMapTransfer OnMapTransfer;
    FOnPlayerDeath OnPlayerDeath;
    FOnEntityDeath OnEntityDeath;
    FOnCorpseReclaimDelay OnCorpseReclaimDelay;
    FOnResurrectRequest OnResurrectRequest;
    FOnSpiritHealerConfirm OnSpiritHealerConfirm;
    FWowDeathState DeathState;
    FOnPlayerTeleport OnPlayerTeleport;
    FOnAurasChanged OnAurasChanged;
    FOnPlayerNameReceived OnPlayerNameReceived;
    FOnCreatureNameReceived OnCreatureNameReceived;
    FOnPlayerInventoryUpdate OnPlayerInventoryUpdate;
    FOnUnitHealthUpdate OnUnitHealthUpdate;
    FOnUnitPowerUpdate OnUnitPowerUpdate;
    FOnEmote OnEmote;
    FOnPlayServerSound OnPlayServerSound;
    /** SMSG_GAMEOBJECT_CUSTOM_ANIM: a fish bites (bobber splash), chests, doors' custom animations. */
    FOnGameObjectCustomAnim OnGameObjectCustomAnim;
    FOnMirrorImageData OnMirrorImageData;
    FOnPlayServerMusic OnPlayServerMusic;
    FOnAIReaction OnAIReaction;
    FOnTextEmote OnTextEmote;
    FOnLevelUp OnLevelUp;
    /** Last SMSG_LEVELUP_INFO gains: health, mana, strength, agility, stamina, intellect, spirit. */
    TArray<int32> LastLevelUpGains;
    FOnXPGain OnXPGain;
    FOnKillXPGain OnKillXPGain;
    FOnBindPointUpdate OnBindPointUpdate;
    FOnBinderConfirm OnBinderConfirm;
    FOnPlayerBound OnPlayerBound;
    FOnWeatherUpdate OnWeatherUpdate;
    FOnMirrorTimerStarted OnMirrorTimerStarted;
    FOnMirrorTimerPaused OnMirrorTimerPaused;
    FOnMirrorTimerStopped OnMirrorTimerStopped;
    FOnWorldStatesInit OnWorldStatesInit;
    FOnWorldStateUpdate OnWorldStateUpdate;
    FOnProficiencySet OnProficiencySet;
    FOnGossipMessage OnGossipMessage;
    FOnGossipComplete OnGossipComplete;
    FOnNpcText OnNpcText;
    TMap<uint32, FString> NpcTextCache;
    FOnInitialSpells OnInitialSpells;
    FOnKnownSpellsChanged OnKnownSpellsChanged;
    FOnActionButtonsUpdated OnActionButtonsUpdated;
    FOnLfgUpdated OnLfgUpdated;

    /** Bind this to send packets back to the server (e.g. TIME_SYNC_RESP) */
    FOnSendPacket OnSendPacket;
    FWowRealmClock RealmClock;
    void ResetInventoryTracking() { bInventoryUpdatePending = false; InitializedItemQuests.Reset(); }

    /** Initialize Warden encryption with session key */
    void InitializeWarden(const TArray<uint8>& SessionKey);

    /** Check if spell is on cooldown */
    bool IsSpellOnCooldown(uint32 SpellId) const;

    /** Get remaining cooldown time in seconds */
    float GetSpellCooldownRemaining(uint32 SpellId) const;

    /** Fired for every processed SMSG opcode — wire to EventSystem for UI events */
    FOnOpcodeReceived OnOpcodeReceived;

private:
    bool bInventoryUpdatePending = false;
    TSet<uint32> InitializedItemQuests;
    void RefreshQuestItemCounts(bool bNotifyProgress);

    // Handler function pointer type
    using HandlerFunc = void (FWowPacketHandler::*)(FPacketReader&);

    // Dispatch table
    TMap<uint16, HandlerFunc> Handlers;

    // Individual handlers
    void HandleLoginVerifyWorld(FPacketReader& R);
    void HandleUpdateObject(FPacketReader& R);
    void HandleCompressedUpdateObject(FPacketReader& R);
    void HandleDestroyObject(FPacketReader& R);
    void HandleMovement(FPacketReader& R);
    void HandleForcedMovement(FPacketReader& R);
    void HandleMoveSetSpeed(FPacketReader& R);
    void HandleSplineSetSpeed(FPacketReader& R);
    /** Opcode currently being dispatched (for handlers shared by several opcodes). */
    uint16 CurrentOpcode = 0;
    void HandleMessageChat(FPacketReader& R);
	void HandleMotd(FPacketReader& R);
    void HandleInitialSpells(FPacketReader& R);
    void HandleActionButtons(FPacketReader& R);
    void HandleTimeSyncReq(FPacketReader& R);
    void HandleLoginTimeSpeed(FPacketReader& R);
    void HandleSpellStart(FPacketReader& R);
    void HandleSpellGo(FPacketReader& R);
    void HandleSpellFailure(FPacketReader& R);
    void HandleCastFailed(FPacketReader& R);
    void HandleSpellCooldown(FPacketReader& R);
    void HandleChannelStart(FPacketReader& R);
    void HandleChannelUpdate(FPacketReader& R);
    void HandleSpellDelayed(FPacketReader& R);
    void HandleAttackerStateUpdate(FPacketReader& R);
    void HandleSpellNonMeleeDamageLog(FPacketReader& R);
    void HandleSpellHealLog(FPacketReader& R);
    void HandleSpellEnergizeLog(FPacketReader& R);
    void HandlePeriodicAuraLog(FPacketReader& R);
    void HandleSpellLogMiss(FPacketReader& R);
    void HandleSpellDamageShield(FPacketReader& R);
    void HandlePartyKillLog(FPacketReader& R);
    void HandleAttackStart(FPacketReader& R);
    void HandleAttackStop(FPacketReader& R);
    void HandleItemEnchantTimeUpdate(FPacketReader& R);
    void HandleAuraUpdate(FPacketReader& R);
    void HandleAuraUpdateAll(FPacketReader& R);
    void ParseAuraUpdate(FPacketReader& R, bool bReplaceAll);
    void HandlePowerUpdate(FPacketReader& R);
    void HandleMonsterMove(FPacketReader& R);
    void HandleInventoryChangeFailure(FPacketReader& R);
    void HandleLootResponse(FPacketReader& R);
    void HandleLootReleaseResponse(FPacketReader& R);
    void HandleLootRemoved(FPacketReader& R);
    void HandleLootClearMoney(FPacketReader& R);
    void HandleItemPushResult(FPacketReader& R);
    void HandleListInventory(FPacketReader& R);
    void HandleItemQueryResponse(FPacketReader& R);
    void HandleBuyItemResult(FPacketReader& R);
    void HandleBuyItemFailure(FPacketReader& R);
    void HandleSellItemResult(FPacketReader& R);

    // Quest handlers
    void HandleQuestgiverStatus(FPacketReader& R);
    void HandleQuestgiverStatusMultiple(FPacketReader& R);
    void HandleQuestgiverQuestList(FPacketReader& R);
    void HandleQuestgiverQuestDetails(FPacketReader& R);
    void HandleQuestgiverRequestItems(FPacketReader& R);
    void HandleQuestgiverOfferReward(FPacketReader& R);
    void HandleQuestgiverQuestComplete(FPacketReader& R);
    void HandleQuestUpdateAddKill(FPacketReader& R);
    void HandleQuestUpdateComplete(FPacketReader& R);
    void HandleQuestQueryResponse(FPacketReader& R);

    // Talent handlers
    void HandleTalentsInfo(FPacketReader& R);
    void HandleLearnedSpell(FPacketReader& R);
    void HandleRemovedSpell(FPacketReader& R);

    // ── Social / Guild / Friends handlers ──────────────────────────────────
    void HandleFriendList(FPacketReader& R);
    void HandleFriendStatus(FPacketReader& R);
    void HandleGuildRoster(FPacketReader& R);
    void HandleGuildName(FPacketReader& R);
    void HandleGuildEvent(FPacketReader& R);
    void HandleGuildBankList(FPacketReader& R);          // r285: SMSG_GUILD_BANK_LIST
    void HandleGuildPermissions(FPacketReader& R);       // r285: MSG_GUILD_PERMISSIONS
    void HandleGuildBankMoneyWithdrawn(FPacketReader& R);// r285: MSG_GUILD_BANK_MONEY_WITHDRAWN
    void HandleUpdateComboPoints(FPacketReader& R);     // r317: SMSG_UPDATE_COMBO_POINTS (0x39D)
    void HandleGuildBankLog(FPacketReader& R);           // r285: MSG_GUILD_BANK_LOG_QUERY
    void HandleGuildBankText(FPacketReader& R);          // r285: MSG_QUERY_GUILD_BANK_TEXT
    void HandleChannelNotify(FPacketReader& R);
    void HandleGroupList(FPacketReader& R);
    void HandleRaidTargetUpdate(FPacketReader& R);
    void HandleRaidReadyCheck(FPacketReader& R);
    void HandleRaidReadyCheckConfirm(FPacketReader& R);
    void HandleRaidReadyCheckFinished(FPacketReader& R);
    void HandlePartyCommandResult(FPacketReader& R);
    void HandleGroupInvite(FPacketReader& R);
    void HandleWho(FPacketReader& R);
    void HandleRandomRoll(FPacketReader& R);

    // Wrath Dungeon Finder and Looking For Raid browser handlers
    void HandleLfgQueueStatus(FPacketReader& R);
    void HandleLfgProposalUpdate(FPacketReader& R);
    void HandleLfgRoleCheckUpdate(FPacketReader& R);
    void HandleLfgJoinResult(FPacketReader& R);
    void HandleLfgUpdatePlayer(FPacketReader& R);
    void HandleLfgUpdateParty(FPacketReader& R);
    void HandleLfgUpdateSearch(FPacketReader& R);
    void HandleLfgList(FPacketReader& R);
    void HandleLfgDisabled(FPacketReader& R);

    // ── Taxi / Flight Path handlers ─────────────────────────────────────────
    void HandleShowTaxiNodes(FPacketReader& R);
    void HandleActivateTaxiReply(FPacketReader& R);
    void HandleNewTaxiPath(FPacketReader& R);
    void HandleNameQueryResponse(FPacketReader& R);
    void HandleGameObjectQueryResponse(FPacketReader& R);
    /** r563: MSG_SET_DUNGEON_DIFFICULTY / MSG_SET_RAID_DIFFICULTY from the server (FWowDifficultyState). */
    void HandleSetDungeonDifficulty(FPacketReader& R);
    void HandleSetRaidDifficulty(FPacketReader& R);
    void HandleGameObjectPageText(FPacketReader& R);
    void HandlePageTextQueryResponse(FPacketReader& R);
    void HandleCreatureQueryResponse(FPacketReader& R);

    // ── Emote handlers ──────────────────────────────────────────────────────
    void HandleEmote(FPacketReader& R);
    void HandlePlaySound(FPacketReader& R);
    void HandlePlayObjectSound(FPacketReader& R);
    void HandleGameObjectCustomAnim(FPacketReader& R);
    void HandleMirrorImageData(FPacketReader& R);
    void HandlePlayMusic(FPacketReader& R);
    void HandleAIReaction(FPacketReader& R);
    void HandleTextEmote(FPacketReader& R);

    // ── Warden / Teleport handlers ──────────────────────────────────────────
    void HandleWardenData(FPacketReader& R);
    void HandleCalendarSendCalendar(FPacketReader& R);
    void HandleCalendarSendNumPending(FPacketReader& R);
    void HandleMoveTeleport(FPacketReader& R);
    void HandleMoveTeleportAck(FPacketReader& R);
    void HandleTransferPending(FPacketReader& R);
    void HandleNewWorld(FPacketReader& R);

    // ── Death / Corpse / Resurrection handlers ──────────────────────────────
    void HandleCorpseReclaimDelay(FPacketReader& R);
    void HandleResurrectRequest(FPacketReader& R);
    void HandleSpiritHealerConfirm(FPacketReader& R);
    void HandleCorpseQuery(FPacketReader& R);
    void HandleAreaSpiritHealerTime(FPacketReader& R);

    // ── Player progression handlers ─────────────────────────────────────────
    void HandleLevelUpInfo(FPacketReader& R);
    void HandleLogXPGain(FPacketReader& R);
    void HandleExplorationExperience(FPacketReader& R);
    void HandleEnvironmentalDamageLog(FPacketReader& R);
    void HandleStartMirrorTimer(FPacketReader& R);
    void HandlePauseMirrorTimer(FPacketReader& R);
    void HandleStopMirrorTimer(FPacketReader& R);
    void HandleBindPointUpdate(FPacketReader& R);
    void HandleBinderConfirm(FPacketReader& R);
    void HandlePlayerBound(FPacketReader& R);
    void HandleInitializeFactions(FPacketReader& R);
    void HandleSetFactionVisible(FPacketReader& R);
    void HandleSetFactionStanding(FPacketReader& R);
    void HandlePlayedTime(FPacketReader& R);

    // ── World state handlers ────────────────────────────────────────────────
    void HandleWeather(FPacketReader& R);
    void HandleInitWorldStates(FPacketReader& R);
    void HandleUpdateWorldState(FPacketReader& R);

    // ── Proficiency handlers ────────────────────────────────────────────────
    void HandleSetProficiency(FPacketReader& R);
    void HandleAccountDataTimes(FPacketReader& R);
    void HandlePartyMemberStats(FPacketReader& R);

    // ── NPC interaction handlers ────────────────────────────────────────────
    void HandleGossipMessage(FPacketReader& R);
    void HandleGossipComplete(FPacketReader& R);
    void HandleNpcText(FPacketReader& R);

    // ── Mail system handlers ────────────────────────────────────────────────
    void HandleShowMailbox(FPacketReader& R);
    void HandleMailListResult(FPacketReader& R);
    void HandleSendMailResult(FPacketReader& R);
    void HandleReceivedMail(FPacketReader& R);
    void HandleQuestPOIQueryResponse(FPacketReader& R);

    // ── Pet system handlers ─────────────────────────────────────────────────
public:
		void HandlePetSpells(FPacketReader& R);
private:

    // ── Duel system handlers ────────────────────────────────────────────────
    void HandleDuelRequested(FPacketReader& R);
    void HandleDuelOutOfBounds(FPacketReader& R);
    void HandleDuelInBounds(FPacketReader& R);
    void HandleDuelComplete(FPacketReader& R);
    void HandleDuelWinner(FPacketReader& R);
    void HandleDuelCountdown(FPacketReader& R);

    // ── Trade system handlers ───────────────────────────────────────────────
    void HandleTradeStatus(FPacketReader& R);
    void HandleTradeStatusExtended(FPacketReader& R);

    // ── Bank system handlers ────────────────────────────────────────────────
    void HandleShowBank(FPacketReader& R);

    // Internal parsing
    void ParseUpdateBlock(FPacketReader& R);
    void ParseMovementInfo(FPacketReader& R, FWowMovementInfo& Out);
    void ParseObjectMovement(FPacketReader& R, FWowMovementInfo& Out);
    void ParseUpdateFields(FPacketReader& R, FWowEntity& Entity);
    void RebuildQuestLogFromPlayerFields(const FWowEntity& PlayerEntity);
    FString ResolveQuestCategoryName(int32 ZoneOrSort) const;

    // Internal Warden response sender (bound to WardenHandler)
    void SendWardenResponse(uint32 Opcode, const TArray<uint8>& Data);

    // Stats
    int32 EntitiesCreated = 0;
    int32 EntitiesUpdated = 0;
    int32 EntitiesDestroyed = 0;
};
