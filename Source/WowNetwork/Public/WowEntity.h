#pragma once
#include "CoreMinimal.h"
#include "WowUpdateFields.h"

// Spline movement waypoint for SMSG_MONSTER_MOVE
struct FWowSplineWaypoint
{
    FVector Position = FVector::ZeroVector;

    FWowSplineWaypoint() = default;
    FWowSplineWaypoint(const FVector& InPosition) : Position(InPosition) {}
};

// Movement info for a living entity
struct FWowMovementInfo
{
    FVector Position = FVector::ZeroVector;
    float Orientation = 0.0f;
    /** Game objects: full world rotation from UPDATEFLAG_ROTATION (WoW axes), r285. */
    FQuat PackedRotation = FQuat::Identity;
    bool bHasPackedRotation = false;
    uint32 MoveFlags = 0;
    uint16 MoveFlags2 = 0;
    uint32 FallTime = 0;
    /** Jump block of a FALLING mover: initial vertical speed, WoW convention (negative = rising). */
    float JumpZSpeed = 0.0f;
    // Monotonic local receipt time; remote clocks cannot be compared with ours.
    double MovementReceivedAtSeconds = 0.0;
    uint32 MovementTimestamp = 0;
    float RunSpeed = 7.0f;
    float RunBackSpeed = 4.5f;
    float SwimSpeed = 4.722f;
    float SwimBackSpeed = 2.5f;
    float FlightSpeed = 7.0f;
    float FlightBackSpeed = 4.5f;
    float TurnRate = 3.1415f;
    float PitchRate = 0.0f;
    float WalkSpeed = 2.5f;
    uint64 TransportGuid = 0;
    FVector TransportOffset = FVector::ZeroVector;
    float TransportOrientation = 0.0f;
    // r598: UPDATEFLAG_TRANSPORT path timer of a transport game object (boat, zeppelin, elevator), in ms,
    // and the local time (FPlatformTime::Seconds) it arrived; the client runs the path from it.
    bool bHasTransportTimer = false;
    uint32 TransportTimer = 0;
    double TransportTimerAt = 0.0;

    // Spline movement data from SMSG_MONSTER_MOVE
    bool bHasActiveSpline = false;
    uint32 SplineId = 0;
    uint32 SplineFlags = 0;
    float SplineSpeed = 0.0f; // Authored path units per second; zero when unknown.
    uint32 SplineDuration = 0;  // Movement duration in milliseconds
    float SplineElapsed = 0.0f; // Time elapsed since spline started
    TArray<FWowSplineWaypoint> SplineWaypoints;
    FVector SplineStartPosition = FVector::ZeroVector;
    uint8 SplineMoveType = 0; // 0=normal, 1=stop, 2=facing point, 3=facing target, 4=facing angle
    FVector SplineFacingPoint = FVector::ZeroVector;
    uint64 SplineFacingTargetGuid = 0;
    float SplineFacingAngle = 0.0f;

    void UpdateSplineSpeed()
    {
        double Length = 0.0;
        FVector Previous = SplineStartPosition;
        for (const FWowSplineWaypoint& Point : SplineWaypoints)
        {
            Length += FVector::Distance(Previous, Point.Position);
            Previous = Point.Position;
        }
        SplineSpeed = SplineDuration > 0 && FMath::IsFinite(Length)
            ? static_cast<float>(Length * 1000.0 / SplineDuration) : 0.0f;
    }

};

// Aura information for a single aura slot
struct FAuraInfo
{
    uint32 SpellId = 0;
    uint8 Flags = 0;
    uint8 Level = 0;
    uint8 Charges = 0;
    uint64 CasterGuid = 0;
    uint32 DurationMs = 0;
    uint32 RemainingMs = 0;
    double ReceivedAtSeconds = 0.0;
    bool bActive = false;
};

// WoW item information
struct FWowItem
{
    uint64 Guid = 0;
    uint32 Entry = 0;   // Item template ID
    uint32 Count = 0;
    uint8 Bag = 0;       // 255 = backpack
    uint8 Slot = 0;
    uint8 Quality = 0;   // 0=grey, 1=white, 2=green, 3=blue, 4=purple, 5=orange
};

// Loot item information
struct FWowLootItem
{
    uint32 ItemId = 0;
    uint32 Count = 0;
    uint32 DisplayId = 0;
    uint32 RandomSuffix = 0;
    uint32 RandomProperty = 0;
    uint8 Quality = 0;
    uint8 Index = 0;
    uint8 SlotType = 0;
    bool bLooted = false;
};

// Server-authored template data shared by merchant, bags and equipment tooltips.
struct FWowItemTemplateInfo
{
    uint32 Entry = 0, Class = 0, SubClass = 0, DisplayId = 0, Quality = 0;
    uint32 Flags = 0, BuyPrice = 0, SellPrice = 0, InventoryType = 0;
    uint32 ItemLevel = 0, RequiredLevel = 0, MaxStack = 1, ContainerSlots = 0;
    uint32 Armor = 0, DelayMs = 0, Bonding = 0, Block = 0, MaxDurability = 0;
    FString Name, Description;
    TArray<TPair<uint32, int32>> Stats;
    float DamageMin = 0, DamageMax = 0;
    uint32 Resistances[6] = {};
    uint32 SpellIds[5] = {}, SpellTriggers[5] = {};
    uint32 SocketColors[3] = {}, SocketBonus = 0;
    /** Heirlooms: ScalingStatDistribution.dbc id and the ScalingStatValues column mask. */
    uint32 ScalingStatDistribution = 0, ScalingStatValue = 0;
};

// Vendor item information
struct FWowVendorItem
{
    uint32 Slot = 0;
    uint32 ItemId = 0;
    uint32 DisplayId = 0;
    uint32 MaxCount = 0;
    uint32 Price = 0;
    uint32 MaxDurability = 0;
    uint32 BuyCount = 0;
    uint32 ExtendedCost = 0;
};

// Mail attachment entry
struct FWowMailAttachment
{
    uint8 AttachmentIndex = 0;
    uint32 ItemGuidLow = 0;
    uint32 ItemEntry = 0;
    int32 RandomPropertyId = 0;
    uint32 SuffixFactor = 0;
    uint32 Count = 0;
    uint32 Charges = 0;
    uint32 MaxDurability = 0;
    uint32 Durability = 0;
};

// Mail header/body plus attachment data
struct FWowMailMessage
{
    uint32 MessageId = 0;
    uint8 MessageType = 0;
    uint64 SenderGuid = 0;
    uint32 SenderEntry = 0;
    uint32 COD = 0;
    uint32 Stationery = 0;
    uint32 Money = 0;
    uint32 Checked = 0;
    float DaysLeft = 0.0f;
    uint32 MailTemplateId = 0;
    FString Subject;
    FString Body;
    TArray<FWowMailAttachment> Attachments;
};

// Trade slot summary from SMSG_TRADE_STATUS_EXTENDED
struct FWowTradeItem
{
    uint8 Slot = 0;
    uint32 ItemId = 0;
    uint32 DisplayId = 0;
    uint32 Count = 0;
    bool bWrapped = false;
    uint64 GiftCreatorGuid = 0;
    uint32 PermanentEnchantId = 0;
    uint32 GemEnchantId1 = 0;
    uint32 GemEnchantId2 = 0;
    uint32 GemEnchantId3 = 0;
    uint64 CreatorGuid = 0;
    uint32 Charges = 0;
    uint32 SuffixFactor = 0;
    int32 RandomPropertyId = 0;
    uint32 LockId = 0;
    uint32 MaxDurability = 0;
    uint32 Durability = 0;

    bool HasItem() const { return ItemId != 0; }
};

// Trade window state
struct FWowTradeState
{
    uint64 TraderGuid = 0;
    uint32 Status = 0;
    bool bTradeOpen = false;
    bool bLocalAccepted = false;
    bool bTargetAccepted = false;
    uint32 PlayerMoney = 0;
    uint32 TargetMoney = 0;
    uint32 PlayerSpell = 0;
    uint32 TargetSpell = 0;
    TArray<FWowTradeItem> PlayerItems;
    TArray<FWowTradeItem> TargetItems;
};

// Quest reward item
struct FWowQuestRewardItem
{
    uint32 ItemId = 0;
    uint32 Count = 0;
    uint32 DisplayId = 0;
};

namespace WowQuestGiverStatus
{
    inline constexpr uint8 NONE = 0;
    inline constexpr uint8 UNAVAILABLE = 1;
    inline constexpr uint8 LOW_LEVEL_AVAILABLE = 2;
    inline constexpr uint8 LOW_LEVEL_REWARD_REP = 3;
    inline constexpr uint8 LOW_LEVEL_AVAILABLE_REP = 4;
    inline constexpr uint8 INCOMPLETE = 5;
    inline constexpr uint8 REWARD_REP = 6;
    inline constexpr uint8 AVAILABLE_REP = 7;
    inline constexpr uint8 AVAILABLE = 8;
    inline constexpr uint8 REWARD_NO_MINIMAP = 9;
    inline constexpr uint8 REWARD = 10;

    inline bool IsAvailable(uint8 Status)
    {
        return Status == AVAILABLE || Status == AVAILABLE_REP;
    }

    inline bool IsReadyForTurnIn(uint8 Status)
    {
        return Status == REWARD || Status == REWARD_REP || Status == REWARD_NO_MINIMAP;
    }
}

// Quest details for dialog
struct FWowQuestDetails
{
    uint64 QuestGiverGuid = 0;
    uint32 QuestId = 0;
    FString QuestGiverName;
    FString Title;
    FString Details;
    FString Objectives;
    TArray<FWowQuestRewardItem> RewardItems;
    TArray<FWowQuestRewardItem> ChoiceRewards;
    TArray<FWowQuestRewardItem> RequiredItems;
    uint32 RewardMoney = 0;
    uint32 RewardXP = 0;
    uint32 RequiredMoney = 0;
    bool bCanComplete = false;
};

struct FWowQuestMenuItem
{
    uint32 QuestId = 0;
    uint32 Icon = 0;
    int32 Level = 0;
    uint32 Flags = 0;
    bool bRepeatable = false;
    FString Title;
};

struct FWowGossipOption
{
    uint32 OptionId = 0;
    uint8 Icon = 0;
    bool bCoded = false;
    uint32 BoxMoney = 0;
    FString Text;
    FString BoxText;
};

struct FWowGossipMenu
{
    uint64 NpcGuid = 0;
    FString NpcName;
    uint32 MenuId = 0;
    uint32 TextId = 0;
    FString Greeting;
    TArray<FWowGossipOption> Options;
    TArray<FWowQuestMenuItem> Quests;
};

// WoW 3.3.5a NPC flags — determine what interactions are available
namespace WowNpcFlags
{
    inline constexpr uint32 NONE            = 0x00000000;
    inline constexpr uint32 GOSSIP          = 0x00000001;
    inline constexpr uint32 QUESTGIVER      = 0x00000002;
    inline constexpr uint32 VENDOR          = 0x00000080; // Generic vendor (sells items)
    inline constexpr uint32 VENDOR_AMMO     = 0x00000100;
    inline constexpr uint32 VENDOR_FOOD     = 0x00000200;
    inline constexpr uint32 VENDOR_POISON   = 0x00000400;
    inline constexpr uint32 VENDOR_REAGENT  = 0x00000800;
    inline constexpr uint32 REPAIR          = 0x00001000;
    inline constexpr uint32 FLIGHTMASTER    = 0x00002000;
    inline constexpr uint32 TRAINER         = 0x00000010;
    inline constexpr uint32 TRAINER_CLASS   = 0x00000020;
    inline constexpr uint32 TRAINER_PROF    = 0x00000040;
    inline constexpr uint32 INNKEEPER       = 0x00010000;
    inline constexpr uint32 BANKER          = 0x00020000;
    inline constexpr uint32 PETITIONER      = 0x00040000;
    inline constexpr uint32 AUCTIONEER      = 0x00200000;
    inline constexpr uint32 STABLEMASTER    = 0x00400000;
    inline constexpr uint32 MAILBOX         = 0x04000000;

    // Combined mask: any flag that means "talk to me" instead of "attack me"
    inline constexpr uint32 INTERACTABLE = GOSSIP | QUESTGIVER | VENDOR | REPAIR
        | FLIGHTMASTER | TRAINER | TRAINER_CLASS | TRAINER_PROF
        | INNKEEPER | BANKER | PETITIONER | AUCTIONEER | STABLEMASTER;
}

namespace WowGameObjectType
{
    inline constexpr uint8 MAILBOX = 19;
}

namespace WowMailMessageType
{
    inline constexpr uint8 NORMAL     = 0;
    inline constexpr uint8 AUCTION    = 2;
    inline constexpr uint8 CREATURE   = 3;
    inline constexpr uint8 GAMEOBJECT = 4;
    inline constexpr uint8 CALENDAR   = 5;
}

namespace WowMailCheckMask
{
    inline constexpr uint32 READ        = 0x01;
    inline constexpr uint32 RETURNED    = 0x02;
    inline constexpr uint32 COPIED      = 0x04;
    inline constexpr uint32 COD_PAYMENT = 0x08;
    inline constexpr uint32 HAS_BODY    = 0x10;
}

namespace WowTradeStatus
{
    inline constexpr uint32 BUSY           = 0;
    inline constexpr uint32 BEGIN_TRADE    = 1;
    inline constexpr uint32 OPEN_WINDOW    = 2;
    inline constexpr uint32 TRADE_CANCELED = 3;
    inline constexpr uint32 TRADE_ACCEPT   = 4;
    inline constexpr uint32 BUSY_2         = 5;
    inline constexpr uint32 NO_TARGET      = 6;
    inline constexpr uint32 BACK_TO_TRADE  = 7;
    inline constexpr uint32 TRADE_COMPLETE = 8;
    inline constexpr uint32 TARGET_TO_FAR  = 10;
    inline constexpr uint32 WRONG_FACTION  = 11;
    inline constexpr uint32 CLOSE_WINDOW   = 12;
    inline constexpr uint32 IGNORE_YOU     = 14;
    inline constexpr uint32 YOU_STUNNED    = 15;
    inline constexpr uint32 TARGET_STUNNED = 16;
    inline constexpr uint32 YOU_DEAD       = 17;
    inline constexpr uint32 TARGET_DEAD    = 18;
    inline constexpr uint32 YOU_LOGOUT     = 19;
    inline constexpr uint32 TARGET_LOGOUT  = 20;
    inline constexpr uint32 TRIAL_ACCOUNT  = 21;
    inline constexpr uint32 ONLY_CONJURED  = 22;
    inline constexpr uint32 NOT_ELIGIBLE   = 23;
}

namespace WowDuelResultReason
{
    inline constexpr uint8 WON  = 0;
    inline constexpr uint8 FLED = 1;
}

namespace WowPetActionType
{
    inline constexpr uint8 PASSIVE  = 0x01;
    inline constexpr uint8 DISABLED = 0x81;
    inline constexpr uint8 ENABLED  = 0xC1;
    inline constexpr uint8 COMMAND  = 0x07;
    inline constexpr uint8 REACTION = 0x06;
}

namespace WowPetCommand
{
    inline constexpr uint32 STAY    = 0;
    inline constexpr uint32 FOLLOW  = 1;
    inline constexpr uint32 ATTACK  = 2;
    inline constexpr uint32 ABANDON = 3;
}

namespace WowPetReactState
{
    inline constexpr uint32 PASSIVE    = 0;
    inline constexpr uint32 DEFENSIVE  = 1;
    inline constexpr uint32 AGGRESSIVE = 2;
}

inline constexpr int32 WOW_PET_ACTION_SLOT_COUNT = 10;

enum class EWowDuelPhase : uint8
{
    None,
    Requested,
    Countdown,
    InProgress,
    Completed
};

struct FWowDuelState
{
    EWowDuelPhase Phase = EWowDuelPhase::None;
    uint64 ArbiterGuid = 0;
    uint64 InitiatorGuid = 0;
    bool bInBounds = true;
    bool bInterrupted = false;
    bool bHasWinner = false;
    uint8 ResultReason = WowDuelResultReason::WON;
    FString WinnerName;
    FString LoserName;
    double CountdownEndTimeSeconds = 0.0;
    double StatusEndTimeSeconds = 0.0;

    void Clear()
    {
        Phase = EWowDuelPhase::None;
        ArbiterGuid = 0;
        InitiatorGuid = 0;
        bInBounds = true;
        bInterrupted = false;
        bHasWinner = false;
        ResultReason = WowDuelResultReason::WON;
        WinnerName.Reset();
        LoserName.Reset();
        CountdownEndTimeSeconds = 0.0;
        StatusEndTimeSeconds = 0.0;
    }

    void BeginRequest(uint64 InArbiterGuid, uint64 InInitiatorGuid)
    {
        Clear();
        Phase = EWowDuelPhase::Requested;
        ArbiterGuid = InArbiterGuid;
        InitiatorGuid = InInitiatorGuid;
    }

    void BeginCountdown(double DurationSeconds)
    {
        Phase = EWowDuelPhase::Countdown;
        bInterrupted = false;
        bHasWinner = false;
        bInBounds = true;
        WinnerName.Reset();
        LoserName.Reset();
        CountdownEndTimeSeconds = FPlatformTime::Seconds() + FMath::Max(0.0, DurationSeconds);
        StatusEndTimeSeconds = 0.0;
    }

    void MarkActive()
    {
        Phase = EWowDuelPhase::InProgress;
        CountdownEndTimeSeconds = 0.0;
    }

    void SetInBounds(bool bIsInBounds)
    {
        bInBounds = bIsInBounds;
        if (Phase == EWowDuelPhase::Requested)
        {
            return;
        }

        if (Phase == EWowDuelPhase::Completed && !bInterrupted && !bHasWinner)
        {
            return;
        }

        if (GetCountdownRemainingSeconds() <= KINDA_SMALL_NUMBER)
        {
            MarkActive();
        }
    }

    void MarkCompleted()
    {
        Phase = EWowDuelPhase::Completed;
        CountdownEndTimeSeconds = 0.0;
        StatusEndTimeSeconds = FPlatformTime::Seconds() + 6.0;
    }

    void MarkInterrupted()
    {
        MarkCompleted();
        bInterrupted = true;
        bHasWinner = false;
        WinnerName.Reset();
        LoserName.Reset();
    }

    void SetWinner(uint8 InResultReason, const FString& InWinnerName, const FString& InLoserName)
    {
        MarkCompleted();
        bInterrupted = false;
        bHasWinner = true;
        ResultReason = InResultReason;
        WinnerName = InWinnerName;
        LoserName = InLoserName;
    }

    float GetCountdownRemainingSeconds() const
    {
        if (Phase != EWowDuelPhase::Countdown)
        {
            return 0.0f;
        }

        return static_cast<float>(FMath::Max(0.0, CountdownEndTimeSeconds - FPlatformTime::Seconds()));
    }

    bool IsRequestPending() const
    {
        return Phase == EWowDuelPhase::Requested;
    }

    bool IsCountdownActive() const
    {
        return Phase == EWowDuelPhase::Countdown && GetCountdownRemainingSeconds() > KINDA_SMALL_NUMBER;
    }

    bool IsInProgress() const
    {
        return Phase == EWowDuelPhase::InProgress
            || (Phase == EWowDuelPhase::Countdown && GetCountdownRemainingSeconds() <= KINDA_SMALL_NUMBER);
    }

    bool IsComplete() const
    {
        return Phase == EWowDuelPhase::Completed;
    }

    bool ShouldShowCompletionText() const
    {
        return IsComplete() && FPlatformTime::Seconds() < StatusEndTimeSeconds;
    }
};

struct FWowPetActionSlot
{
    uint32 PackedData = 0;
    uint32 ActionId = 0;
    uint8 ActionType = WowPetActionType::PASSIVE;

    void SetPackedData(uint32 InPackedData)
    {
        PackedData = InPackedData;
        ActionId = PackedData & 0x00FFFFFF;
        ActionType = static_cast<uint8>((PackedData >> 24) & 0xFF);
    }

    bool IsSpell() const
    {
        return ActionType == WowPetActionType::PASSIVE
            || ActionType == WowPetActionType::DISABLED
            || ActionType == WowPetActionType::ENABLED;
    }

    bool IsCommand() const
    {
        return ActionType == WowPetActionType::COMMAND;
    }

    bool IsReaction() const
    {
        return ActionType == WowPetActionType::REACTION;
    }

    bool IsAutocastCapable() const
    {
        return ActionType == WowPetActionType::DISABLED || ActionType == WowPetActionType::ENABLED;
    }

    bool IsAutocastEnabled() const
    {
        return ActionType == WowPetActionType::ENABLED;
    }

    bool IsEmpty() const
    {
        return IsSpell() && ActionId == 0;
    }

    bool IsUsable() const
    {
        return IsCommand() || IsReaction() || (IsSpell() && ActionId != 0);
    }

    FString GetDisplayName() const
    {
        if (IsCommand())
        {
            switch (ActionId)
            {
            case WowPetCommand::ATTACK: return TEXT("Attack");
            case WowPetCommand::FOLLOW: return TEXT("Follow");
            case WowPetCommand::STAY: return TEXT("Stay");
            case WowPetCommand::ABANDON: return TEXT("Dismiss");
            default: return TEXT("Command");
            }
        }

        if (IsReaction())
        {
            switch (ActionId)
            {
            case WowPetReactState::AGGRESSIVE: return TEXT("Aggressive");
            case WowPetReactState::DEFENSIVE: return TEXT("Defensive");
            case WowPetReactState::PASSIVE: return TEXT("Passive");
            default: return TEXT("Reaction");
            }
        }

        return ActionId != 0
            ? FString::Printf(TEXT("Spell %u"), ActionId)
            : TEXT("");
    }
};

struct FWowPetActionBarState
{
    uint64 PetGuid = 0;
    uint16 Family = 0;
    uint32 DurationMs = 0;
    uint8 ReactState = 0;
    uint8 CommandState = 0;
    uint16 Flags = 0;
    TArray<FWowPetActionSlot> ActionSlots;
    TArray<uint32> KnownSpells;
    TMap<uint32, double> SpellCooldownExpirySeconds;

    FWowPetActionBarState()
    {
        Clear();
    }

    void Clear()
    {
        PetGuid = 0;
        Family = 0;
        DurationMs = 0;
        ReactState = 0;
        CommandState = 0;
        Flags = 0;
        ActionSlots.Init(FWowPetActionSlot{}, WOW_PET_ACTION_SLOT_COUNT);
        KnownSpells.Empty();
        SpellCooldownExpirySeconds.Empty();
    }

    bool HasPet() const
    {
        return PetGuid != 0;
    }

    bool HasActionBar() const
    {
        return HasPet() && ActionSlots.Num() == WOW_PET_ACTION_SLOT_COUNT;
    }

    const FWowPetActionSlot* GetSlot(int32 SlotIndex) const
    {
        return ActionSlots.IsValidIndex(SlotIndex) ? &ActionSlots[SlotIndex] : nullptr;
    }

    FWowPetActionSlot* GetMutableSlot(int32 SlotIndex)
    {
        return ActionSlots.IsValidIndex(SlotIndex) ? &ActionSlots[SlotIndex] : nullptr;
    }

    float GetCooldownRemaining(int32 SlotIndex) const
    {
        const FWowPetActionSlot* Slot = GetSlot(SlotIndex);
        if (!Slot || !Slot->IsSpell() || Slot->ActionId == 0)
        {
            return 0.0f;
        }

        if (const double* ExpirySeconds = SpellCooldownExpirySeconds.Find(Slot->ActionId))
        {
            return static_cast<float>(FMath::Max(0.0, *ExpirySeconds - FPlatformTime::Seconds()));
        }

        return 0.0f;
    }
};

// WoW 3.3.5a unit stand states (UNIT_FIELD_BYTES_1 byte 0)
namespace WowStandState
{
    inline constexpr uint8 STAND     = 0;
    inline constexpr uint8 SIT       = 1;
    inline constexpr uint8 SIT_CHAIR = 2;
    inline constexpr uint8 SLEEP     = 3;
    inline constexpr uint8 SIT_LOW   = 4;
    inline constexpr uint8 SIT_MED   = 5;
    inline constexpr uint8 SIT_HIGH  = 6;
    inline constexpr uint8 DEAD      = 7;
    inline constexpr uint8 KNEEL     = 8;
    inline constexpr uint8 SUBMERGED = 9;
}

// WoW 3.3.5a unit dynamic flags
namespace WowDynFlags
{
    inline constexpr uint32 NONE         = 0x0000;
    inline constexpr uint32 LOOTABLE     = 0x0001;
    inline constexpr uint32 TRACK_UNIT   = 0x0002;
    inline constexpr uint32 TAPPED       = 0x0004;
    inline constexpr uint32 TAPPED_BY_PLAYER = 0x0008;
    inline constexpr uint32 DEAD         = 0x0020;
}

enum class EWowEntityKind : uint8
{
    Object,
    Item,
    Container,
    Unit,
    Player,
    GameObject,
    DynamicObject,
    Corpse
};

namespace WowGroupType
{
    inline constexpr uint8 NORMAL         = 0x00;
    inline constexpr uint8 BG             = 0x01;
    inline constexpr uint8 RAID           = 0x02;
    inline constexpr uint8 LFG_RESTRICTED = 0x04;
    inline constexpr uint8 LFG            = 0x08;
}

namespace WowGroupMemberFlags
{
    inline constexpr uint8 ASSISTANT  = 0x01;
    inline constexpr uint8 MAINTANK   = 0x02;
    inline constexpr uint8 MAINASSIST = 0x04;
}

namespace WowGroupMemberStatus
{
    inline constexpr uint8 ONLINE  = 0x01;
    inline constexpr uint8 PVP     = 0x02;
    inline constexpr uint8 DEAD    = 0x04;
    inline constexpr uint8 GHOST   = 0x08;
    inline constexpr uint8 AFK     = 0x40;
    inline constexpr uint8 DND     = 0x80;
}

namespace WowReadyCheckResponse
{
    inline constexpr uint8 NOT_READY = 0;
    inline constexpr uint8 READY     = 1;
}

namespace WowLfgRole
{
    inline constexpr uint8 Leader = 0x01;
    inline constexpr uint8 Tank = 0x02;
    inline constexpr uint8 Healer = 0x04;
    inline constexpr uint8 Damage = 0x08;
}

enum class EWowLfgMode : uint8
{
    None,
    RoleCheck,
    Queued,
    Proposal,
    Dungeon,
    RaidListing,
    RaidSearch,
    Disabled,
};

struct FWowLfgQueueStatus
{
    uint32 DungeonEntry = 0;
    int32 AverageWaitSeconds = 0;
    int32 PlayerWaitSeconds = 0;
    int32 TankWaitSeconds = 0;
    int32 HealerWaitSeconds = 0;
    int32 DamageWaitSeconds = 0;
    uint8 TanksNeeded = 0;
    uint8 HealersNeeded = 0;
    uint8 DamageNeeded = 0;
    uint32 QueuedSeconds = 0;
};

struct FWowLfgProposalMember
{
    uint32 Roles = 0;
    bool bIsPlayer = false;
    bool bInDungeon = false;
    bool bSameGroup = false;
    bool bAnswered = false;
    bool bAccepted = false;
};

struct FWowLfgProposal
{
    uint32 DungeonEntry = 0;
    uint8 State = 0;
    uint32 ProposalId = 0;
    uint32 EncountersMask = 0;
    bool bSilent = false;
    TArray<FWowLfgProposalMember> Members;
};

struct FWowLfgRoleCheckMember
{
    uint64 Guid = 0;
    bool bReady = false;
    uint32 Roles = 0;
    uint8 Level = 0;
};

struct FWowLfgRoleCheck
{
    uint32 State = 0;
    bool bInitializing = false;
    TArray<uint32> DungeonEntries;
    TArray<FWowLfgRoleCheckMember> Members;
};

struct FWowLfgRaidBrowserEntry
{
    uint64 PlayerGuid = 0;
    uint64 GroupGuid = 0;
    FString PlayerName;
    FString Comment;
    uint8 Level = 0;
    uint8 ClassId = 0;
    uint8 Roles = 0;
    uint32 AreaId = 0;
    uint8 Status = 0;
    bool bGroupLeader = false;
    float AverageItemLevel = 0.0f;
};

struct FWowLfgState
{
    EWowLfgMode Mode = EWowLfgMode::None;
    uint8 Roles = WowLfgRole::Damage;
    TArray<uint32> SelectedDungeonEntries;
    FString Comment;
    bool bQueued = false;
    bool bListedForRaid = false;
    uint32 RaidSearchEntry = 0;
    uint32 LastJoinResult = 0;
    uint32 LastJoinState = 0;
    FWowLfgQueueStatus QueueStatus;
    FWowLfgProposal Proposal;
    FWowLfgRoleCheck RoleCheck;
    TArray<FWowLfgRaidBrowserEntry> RaidResults;

    void ClearQueue()
    {
        bQueued = false;
        SelectedDungeonEntries.Reset();
        QueueStatus = FWowLfgQueueStatus();
        Proposal = FWowLfgProposal();
        RoleCheck = FWowLfgRoleCheck();
        Mode = bListedForRaid ? EWowLfgMode::RaidListing : EWowLfgMode::None;
    }
};

// Base entity — represents any WoW object tracked by the client
struct WOWNETWORK_API FWowEntity
{
    uint64 Guid = 0;
    uint8 ObjectTypeId = 0; // Server object type id from CREATE_OBJECT
    uint32 TypeMask = 0;  // WowTypeMask bitmask
    uint32 Entry = 0;
    float Scale = 1.0f;

    FWowMovementInfo Movement;

    // Aura slots (max 64 aura slots in 3.3.5a)
    TArray<FAuraInfo> Auras;

    // Raw update field values (indexed by field offset)
    TMap<uint16, uint32> Fields;

    FWowEntity()
    {
        // Initialize 64 aura slots
        Auras.SetNum(64);
    }

    FWowEntity(const FWowEntity&) = default;
    FWowEntity(FWowEntity&&) = default;
    FWowEntity& operator=(const FWowEntity&) = default;
    FWowEntity& operator=(FWowEntity&&) = default;
    virtual ~FWowEntity() = default;

    virtual EWowEntityKind GetEntityKind() const { return EWowEntityKind::Object; }
    virtual const TCHAR* GetEntityKindName() const { return TEXT("Object"); }

    // Convenience accessors
    bool IsPlayer() const { return GetEntityKind() == EWowEntityKind::Player || (TypeMask & WowTypeMask::PLAYER) != 0; }
    bool IsUnit() const
    {
        const EWowEntityKind Kind = GetEntityKind();
        return Kind == EWowEntityKind::Unit || Kind == EWowEntityKind::Player
            || (TypeMask & (WowTypeMask::UNIT | WowTypeMask::PLAYER)) != 0;
    }
    bool IsGameObject() const { return GetEntityKind() == EWowEntityKind::GameObject || (TypeMask & WowTypeMask::GAMEOBJECT) != 0; }
    bool IsItem() const
    {
        const EWowEntityKind Kind = GetEntityKind();
        return Kind == EWowEntityKind::Item || Kind == EWowEntityKind::Container
            || (TypeMask & (WowTypeMask::ITEM | WowTypeMask::CONTAINER)) != 0;
    }
    bool IsContainer() const { return GetEntityKind() == EWowEntityKind::Container || (TypeMask & WowTypeMask::CONTAINER) != 0; }
    bool IsDynamicObject() const { return GetEntityKind() == EWowEntityKind::DynamicObject || (TypeMask & WowTypeMask::DYNAMICOBJECT) != 0; }
    bool IsCorpse() const { return GetEntityKind() == EWowEntityKind::Corpse || (TypeMask & WowTypeMask::CORPSE) != 0; }

    uint32 GetField(uint16 Index) const
    {
        const uint32* Val = Fields.Find(Index);
        return Val ? *Val : 0;
    }

    uint64 GetField64(uint16 Index) const
    {
        return static_cast<uint64>(GetField(Index)) | (static_cast<uint64>(GetField(Index + 1)) << 32);
    }

    float GetFieldFloat(uint16 Index) const
    {
        uint32 Raw = GetField(Index);
        float Value = 0.0f;
        FMemory::Memcpy(&Value, &Raw, sizeof(float));
        return Value;
    }

    uint8 GetFieldByte(uint16 Index, uint8 ByteOffset) const
    {
        return static_cast<uint8>((GetField(Index) >> (ByteOffset * 8)) & 0xFF);
    }

    double TempEnchantReceivedAt = 0.0;
    double TempEnchantDeadline = 0.0;
    bool bTempEnchantTimed = false;
    void SetField(uint16 Index, uint32 Value)
    {
        Fields.Add(Index, Value);
        if (IsItem() && Index == ItemField::ENCHANTMENT_1_1 + 4)
        {
            TempEnchantReceivedAt = FPlatformTime::Seconds();
            TempEnchantDeadline = TempEnchantReceivedAt + Value * .001;
            bTempEnchantTimed = Value > 0;
        }
        if (IsItem() && Index == ItemField::ENCHANTMENT_1_1 + 3 && Value == 0)
        { TempEnchantDeadline = 0; bTempEnchantTimed = false; }
    }
    double GetTempEnchantRemainingMs(double Now = FPlatformTime::Seconds()) const
    { return bTempEnchantTimed ? FMath::Max(0.0, (TempEnchantDeadline - Now) * 1000.0) : 0.0; }


    // Common field shortcuts
    int32 GetHealth() const { return static_cast<int32>(GetField(UnitField::HEALTH)); }
    int32 GetMaxHealth() const { return static_cast<int32>(GetField(UnitField::MAXHEALTH)); }
    int32 GetLevel() const { return static_cast<int32>(GetField(UnitField::LEVEL)); }
    uint32 GetDisplayId() const { return GetField(UnitField::DISPLAYID); }
    uint32 GetNativeDisplayId() const { return GetField(UnitField::NATIVEDISPLAYID); }
};

struct WOWNETWORK_API FWowItemEntity : public FWowEntity
{
    FWowItemEntity() = default;
    explicit FWowItemEntity(const FWowEntity& Other) : FWowEntity(Other) {}

    EWowEntityKind GetEntityKind() const override { return EWowEntityKind::Item; }
    const TCHAR* GetEntityKindName() const override { return TEXT("Item"); }

    uint64 GetOwnerGuid() const { return GetField64(ItemField::OWNER); }
    uint64 GetContainedGuid() const { return GetField64(ItemField::CONTAINED); }
    uint64 GetCreatorGuid() const { return GetField64(ItemField::CREATOR); }
    int32 GetStackCount() const { return static_cast<int32>(GetField(ItemField::STACK_COUNT)); }
    int32 GetDuration() const { return static_cast<int32>(GetField(ItemField::DURATION)); }
    uint32 GetItemFlags() const { return GetField(ItemField::FLAGS); }
    int32 GetDurability() const { return static_cast<int32>(GetField(ItemField::DURABILITY)); }
    int32 GetMaxDurability() const { return static_cast<int32>(GetField(ItemField::MAX_DURABILITY)); }

    uint32 GetSpellCharges(int32 Slot) const
    {
        return (Slot >= 0 && Slot < 5) ? GetField(ItemField::SPELL_CHARGES + Slot) : 0;
    }

    uint32 GetEnchantmentId(int32 Slot) const
    {
        return (Slot >= 0 && Slot < 12) ? GetField(ItemField::ENCHANTMENT_1_1 + Slot * 3) : 0;
    }

    uint32 GetEnchantmentDuration(int32 Slot) const
    {
        return (Slot >= 0 && Slot < 12) ? GetField(ItemField::ENCHANTMENT_1_1 + Slot * 3 + 1) : 0;
    }

    uint32 GetEnchantmentCharges(int32 Slot) const
    {
        return (Slot >= 0 && Slot < 12) ? GetField(ItemField::ENCHANTMENT_1_1 + Slot * 3 + 2) : 0;
    }
};

struct WOWNETWORK_API FWowContainerEntity : public FWowItemEntity
{
    FWowContainerEntity() = default;
    explicit FWowContainerEntity(const FWowEntity& Other) : FWowItemEntity(Other) {}

    EWowEntityKind GetEntityKind() const override { return EWowEntityKind::Container; }
    const TCHAR* GetEntityKindName() const override { return TEXT("Container"); }

    int32 GetNumSlots() const { return static_cast<int32>(GetField(ContainerField::NUM_SLOTS)); }

    uint64 GetItemGuidAtSlot(int32 SlotIndex) const
    {
        return (SlotIndex >= 0 && SlotIndex < 36) ? GetField64(ContainerField::SLOT_1 + SlotIndex * 2) : 0;
    }
};

struct WOWNETWORK_API FWowUnitEntity : public FWowEntity
{
    FWowUnitEntity() = default;
    explicit FWowUnitEntity(const FWowEntity& Other) : FWowEntity(Other) {}

    EWowEntityKind GetEntityKind() const override { return EWowEntityKind::Unit; }
    const TCHAR* GetEntityKindName() const override { return TEXT("Unit"); }

    int32 GetPower(uint8 PowerType) const
    {
        return (PowerType < 7) ? static_cast<int32>(GetField(UnitField::POWER1 + PowerType)) : 0;
    }

    int32 GetMaxPower(uint8 PowerType) const
    {
        return (PowerType < 7) ? static_cast<int32>(GetField(UnitField::MAXPOWER1 + PowerType)) : 0;
    }

    uint32 GetFactionTemplate() const { return GetField(UnitField::FACTIONTEMPLATE); }
    uint32 GetUnitFlags() const { return GetField(UnitField::FLAGS); }
    uint32 GetUnitFlags2() const { return GetField(UnitField::FLAGS_2); }
    uint32 GetNpcFlags() const { return GetField(UnitField::NPC_FLAGS); }
    uint32 GetMountDisplayId() const { return GetField(UnitField::MOUNTDISPLAYID); }

    /** Check if this NPC has any interactable flags (gossip, questgiver, vendor, etc.) */
    bool IsInteractable() const { return (GetNpcFlags() & WowNpcFlags::INTERACTABLE) != 0; }
    bool IsQuestGiver() const { return (GetNpcFlags() & WowNpcFlags::QUESTGIVER) != 0; }
    bool IsVendor() const { return (GetNpcFlags() & WowNpcFlags::VENDOR) != 0; }
    bool IsFlightMaster() const { return (GetNpcFlags() & WowNpcFlags::FLIGHTMASTER) != 0; }

    // Stand state (from BYTES_1, byte 0) — controls idle pose: stand, sit, sleep, kneel, dead
    uint8 GetStandState() const { return GetFieldByte(UnitField::BYTES_1, 0); }
    bool IsDead() const { return GetStandState() == WowStandState::DEAD || GetHealth() <= 0; }
    bool IsSitting() const { const uint8 State = GetStandState(); return State == WowStandState::SIT || State == WowStandState::SIT_CHAIR || State == WowStandState::SIT_LOW || State == WowStandState::SIT_MED || State == WowStandState::SIT_HIGH; }
    bool IsSleeping() const { return GetStandState() == WowStandState::SLEEP; }
    bool IsKneeling() const { return GetStandState() == WowStandState::KNEEL; }

    // Dynamic flags — lootable, tapped, etc.
    uint32 GetDynamicFlags() const { return GetField(UnitField::DYNAMIC_FLAGS); }
    bool IsLootable() const { return (GetDynamicFlags() & WowDynFlags::LOOTABLE) != 0; }
    bool IsTapped() const { return (GetDynamicFlags() & WowDynFlags::TAPPED) != 0; }

    // NPC emote state (looping emote animation)
    uint32 GetEmoteState() const { return GetField(UnitField::NPC_EMOTESTATE); }

    // Stats (Strength, Agility, Stamina, Intellect, Spirit)
    int32 GetStrength() const { return static_cast<int32>(GetField(UnitField::STAT0)); }
    int32 GetAgility() const { return static_cast<int32>(GetField(UnitField::STAT1)); }
    int32 GetStamina() const { return static_cast<int32>(GetField(UnitField::STAT2)); }
    int32 GetIntellect() const { return static_cast<int32>(GetField(UnitField::STAT3)); }
    int32 GetSpirit() const { return static_cast<int32>(GetField(UnitField::STAT4)); }

    // Resistances (Physical=armor at index 0, then Holy, Fire, Nature, Frost, Shadow, Arcane)
    int32 GetArmor() const { return static_cast<int32>(GetField(UnitField::RESISTANCES)); }
    int32 GetResistance(int32 School) const
    {
        return (School >= 0 && School < 7) ? static_cast<int32>(GetField(UnitField::RESISTANCES + School)) : 0;
    }

    // Attack Power
    int32 GetAttackPower() const { return static_cast<int32>(GetField(UnitField::ATTACK_POWER)); }
    int32 GetRangedAttackPower() const { return static_cast<int32>(GetField(UnitField::RANGED_ATTACK_POWER)); }

    // Sheath state (from BYTES_2, byte 0) — weapon visibility
    uint8 GetSheathState() const { return GetFieldByte(UnitField::BYTES_2, 0); }
    uint8 GetRaceId() const { return GetFieldByte(UnitField::BYTES_0, 0); }
    uint8 GetClassId() const { return GetFieldByte(UnitField::BYTES_0, 1); }
    uint8 GetGenderId() const { return GetFieldByte(UnitField::BYTES_0, 2); }
    uint8 GetPowerTypeId() const { return GetFieldByte(UnitField::BYTES_0, 3); }
};

struct WOWNETWORK_API FWowPlayerEntity : public FWowUnitEntity
{
    FWowPlayerEntity() = default;
    explicit FWowPlayerEntity(const FWowEntity& Other) : FWowUnitEntity(Other) {}

    EWowEntityKind GetEntityKind() const override { return EWowEntityKind::Player; }
    const TCHAR* GetEntityKindName() const override { return TEXT("Player"); }

    uint32 GetPlayerFlags() const { return GetField(PlayerField::FLAGS); }
    uint32 GetPlayerBytes() const { return GetField(PlayerField::BYTES); }
    uint32 GetPlayerBytes2() const { return GetField(PlayerField::BYTES_2); }
    uint32 GetXp() const { return GetField(PlayerField::XP); }
    uint32 GetNextLevelXp() const { return GetField(PlayerField::NEXT_LEVEL_XP); }
    uint32 GetCoinage() const { return GetField(PlayerField::COINAGE); }
    uint8 GetBankBagSlotCount() const { return GetFieldByte(PlayerField::BYTES_2, 2); }

    // Equipment and inventory accessors
    uint64 GetEquipmentItemGuid(uint8 SlotIndex) const
    {
        return (SlotIndex < 19) ? GetField64(PlayerField::INV_SLOT_HEAD + SlotIndex * 2) : 0;
    }

    uint64 GetBackpackItemGuid(uint8 SlotIndex) const
    {
        return (SlotIndex < 16) ? GetField64(PlayerField::PACK_SLOT_START + SlotIndex * 2) : 0;
    }

    uint64 GetBagGuid(uint8 BagIndex) const
    {
        if (BagIndex >= 1 && BagIndex <= 4)
            return GetField64(PlayerField::INV_SLOT_BAG_1 + (BagIndex - 1) * 2);
        return 0;
    }

    uint64 GetBankItemGuid(uint8 SlotIndex) const
    {
        return (SlotIndex < 28) ? GetField64(PlayerField::BANK_SLOT_START + SlotIndex * 2) : 0;
    }

    uint64 GetBankBagGuid(uint8 BagIndex) const
    {
        if (BagIndex < 7)
            return GetField64(PlayerField::BANKBAG_SLOT_1 + BagIndex * 2);
        return 0;
    }

    // Combat ratings (for players only)
    int32 GetCombatRating(int32 RatingType) const
    {
        return (RatingType >= 0 && RatingType < 25) ? static_cast<int32>(GetField(PlayerField::COMBAT_RATING_1 + RatingType)) : 0;
    }
};

struct WOWNETWORK_API FWowGameObjectEntity : public FWowEntity
{
    FWowGameObjectEntity() = default;
    explicit FWowGameObjectEntity(const FWowEntity& Other) : FWowEntity(Other) {}

    EWowEntityKind GetEntityKind() const override { return EWowEntityKind::GameObject; }
    const TCHAR* GetEntityKindName() const override { return TEXT("GameObject"); }

    uint64 GetCreatorGuid() const { return GetField64(GameObjectField::CREATED_BY); }
    uint32 GetGameObjectDisplayId() const { return GetField(GameObjectField::DISPLAY_ID); }
    uint32 GetGameObjectFlags() const { return GetField(GameObjectField::FLAGS); }
    uint32 GetFaction() const { return GetField(GameObjectField::FACTION); }
    int32 GetGameObjectLevel() const { return static_cast<int32>(GetField(GameObjectField::LEVEL)); }
    uint8 GetGameObjectType() const { return GetFieldByte(GameObjectField::BYTES_1, 1); }
    /** Like the original client: GO_FLAG_NOT_SELECTABLE (0x10) never, and GO_FLAG_INTERACT_COND (0x04),
     *  set on quest objects, only while the server marks it usable for us (GO_DYNFLAG_LO_ACTIVATE 0x01). */
    bool CanInteractNow() const
    {
        const uint32 Flags = GetGameObjectFlags();
        if (Flags & 0x10u) return false;
        // Types the client never uses on click: generic decor, traps, spell foci (braziers,
        // anvils, forges for crafting), transports, area damage, map objects, capture points,
        // aura generators and destructible buildings.
        switch (GetGameObjectType())
        {
        case 5: case 6: case 8: case 11: case 12: case 14: case 15: case 29: case 30: case 33:
            return false;
        default: break;
        }
        return (Flags & 0x04u) == 0 || (GetField(GameObjectField::DYNAMIC) & 0x01u) != 0;
    }
    float GetParentRotationComponent(int32 ComponentIndex) const
    {
        return (ComponentIndex >= 0 && ComponentIndex < 4) ? GetFieldFloat(GameObjectField::PARENT_ROTATION + ComponentIndex) : 0.0f;
    }
};

struct WOWNETWORK_API FWowDynamicObjectEntity : public FWowEntity
{
    FWowDynamicObjectEntity() = default;
    explicit FWowDynamicObjectEntity(const FWowEntity& Other) : FWowEntity(Other) {}

    EWowEntityKind GetEntityKind() const override { return EWowEntityKind::DynamicObject; }
    const TCHAR* GetEntityKindName() const override { return TEXT("DynamicObject"); }

    uint64 GetCasterGuid() const { return GetField64(DynamicObjectField::CASTER); }
    uint32 GetSpellId() const { return GetField(DynamicObjectField::SPELL_ID); }
    float GetRadius() const { return GetFieldFloat(DynamicObjectField::RADIUS); }
    uint32 GetCastTime() const { return GetField(DynamicObjectField::CAST_TIME); }
};

struct WOWNETWORK_API FWowCorpseEntity : public FWowEntity
{
    FWowCorpseEntity() = default;
    explicit FWowCorpseEntity(const FWowEntity& Other) : FWowEntity(Other) {}

    EWowEntityKind GetEntityKind() const override { return EWowEntityKind::Corpse; }
    const TCHAR* GetEntityKindName() const override { return TEXT("Corpse"); }

    uint64 GetOwnerGuid() const { return GetField64(CorpseField::OWNER); }
};

// Quest objective tracking
struct FWowQuestObjective
{
    uint32 CreatureOrGOId = 0;
    uint32 ItemId = 0;
    uint32 Count = 0;
    uint32 Required = 0;
    FString Text;
};

// Quest log entry
struct FWowQuestLogEntry
{
    uint32 QuestId = 0;
    FString Title;
    FString Details;
    FString ObjectiveText;
    FString ZoneName;
    int32 Level = 0;
    int32 ZoneOrSort = 0;
    uint32 State = 0; // 0=not complete, 1=complete, 2=failed
    bool bIsTracked = false;
    TArray<uint32> ObjectiveCounts;
    TArray<FWowQuestObjective> Objectives;
    // Rewards from SMSG_QUEST_QUERY_RESPONSE (item entry, count).
    TArray<TPair<uint32, uint32>> RewardItems;
    TArray<TPair<uint32, uint32>> RewardChoiceItems;
    int32 RewardMoney = 0;   // negative = money required
    uint32 RewardSpell = 0;
    /** r320: the quest's source item (quest query field SrcItemId), shown as a usable item in the tracker. */
    uint32 SourceItemId = 0;
    /** r339: the quest query's completed-objectives line ("Return to ..."), GetQuestLogCompletionText. */
    FString CompletionText;
};

// Talent information
struct FWowTalentInfo
{
    uint32 TalentId = 0;
    uint8 Rank = 0; // 0-5
};

// Friend information for the social system
struct FWowFriendInfo
{
    uint64 Guid = 0;
    uint32 Flags = 0;     // 0x1=friend, 0x2=ignored, 0x4=muted
    FString Name;
    uint8 Status = 0;    // 0=offline, 1=online, 2=AFK, 3=DND
    uint32 AreaId = 0;
    uint8 Level = 0;
    uint8 Class = 0;
    FString Note;
};

// Guild member information
struct FWowGuildMember
{
    uint64 Guid = 0;
    FString Name;
    uint8 Status = 0;
    uint8 Level = 0;
    uint8 Class = 0;
    uint32 ZoneId = 0;
    uint8 RankId = 0;
    FString RankName;
    FString PublicNote;
    FString OfficerNote;
};

// Group/Party member information
struct FWowGroupMember
{
    uint64 Guid = 0;
    FString Name;
    uint8 Group = 0;        // Subgroup index (0-7)
    uint8 Flags = 0;        // Member flags
    uint8 Roles = 0;        // Tank/Healer/DPS roles
    uint8 Status = 0;       // Online status
    uint8 Level = 0;        // Member level
    uint8 Class = 0;        // Member class
};

// Raid target icon state for the current group
struct FWowRaidTargetState
{
    TArray<uint64> IconTargets;
    TMap<uint64, uint8> GuidToIcon;

    FWowRaidTargetState()
    {
        Clear();
    }

    void Clear()
    {
        IconTargets.Init(0, 8);
        GuidToIcon.Empty();
    }

    bool HasAnyIcons() const
    {
        for (const uint64 Guid : IconTargets)
        {
            if (Guid != 0)
            {
                return true;
            }
        }

        return false;
    }

    void SetTarget(uint8 IconIndex, uint64 TargetGuid)
    {
        if (!IconTargets.IsValidIndex(IconIndex))
        {
            return;
        }

        const uint64 PreviousTarget = IconTargets[IconIndex];
        if (PreviousTarget != 0)
        {
            GuidToIcon.Remove(PreviousTarget);
        }

        if (TargetGuid != 0)
        {
            for (int32 ExistingIndex = 0; ExistingIndex < IconTargets.Num(); ++ExistingIndex)
            {
                if (ExistingIndex != IconIndex && IconTargets[ExistingIndex] == TargetGuid)
                {
                    IconTargets[ExistingIndex] = 0;
                }
            }
        }

        IconTargets[IconIndex] = TargetGuid;
        if (TargetGuid != 0)
        {
            GuidToIcon.Add(TargetGuid, IconIndex);
        }
    }

    int32 GetIconIndex(uint64 Guid) const
    {
        const uint8* FoundIcon = GuidToIcon.Find(Guid);
        return FoundIcon ? static_cast<int32>(*FoundIcon) : INDEX_NONE;
    }
};

// Active ready-check state for the current group
struct FWowReadyCheckState
{
    bool bActive = false;
    uint64 InitiatorGuid = 0;
    double EndTimeSeconds = 0.0;
    TMap<uint64, uint8> Responses;

    void Begin(uint64 InInitiatorGuid, double DurationSeconds = 35.0)
    {
        bActive = true;
        InitiatorGuid = InInitiatorGuid;
        EndTimeSeconds = FPlatformTime::Seconds() + DurationSeconds;
        Responses.Empty();
    }

    void Clear()
    {
        bActive = false;
        InitiatorGuid = 0;
        EndTimeSeconds = 0.0;
        Responses.Empty();
    }

    void SetResponse(uint64 Guid, uint8 ResponseState)
    {
        Responses.Add(Guid, ResponseState);
    }

    float GetTimeLeftSeconds() const
    {
        if (!bActive)
        {
            return 0.0f;
        }

        return static_cast<float>(FMath::Max(0.0, EndTimeSeconds - FPlatformTime::Seconds()));
    }

    const uint8* FindResponse(uint64 Guid) const
    {
        return Responses.Find(Guid);
    }
};

// Group/Party information
struct FWowGroupInfo
{
    uint8 MemberCount = 0;  // Total count including the local player
    uint8 GroupType = 0;    // 0=party, 1=raid
    uint8 RawGroupType = 0;
    uint64 GroupGuid = 0;
    uint64 LeaderGuid = 0;
    uint8 SelfSubgroup = 0;
    uint8 SelfFlags = 0;
    uint8 SelfRoles = 0;
    TArray<FWowGroupMember> Members;

    void Clear()
    {
        MemberCount = 0;
        GroupType = 0;
        RawGroupType = 0;
        GroupGuid = 0;
        LeaderGuid = 0;
        SelfSubgroup = 0;
        SelfFlags = 0;
        SelfRoles = 0;
        Members.Empty();
    }

    bool IsEmpty() const
    {
        return MemberCount == 0;
    }

    bool IsRaidGroup() const
    {
        return GroupType == 1;
    }

    bool IsPartyGroup() const
    {
        return !IsEmpty() && GroupType == 0;
    }

    bool IsLfgGroup() const
    {
        return (RawGroupType & WowGroupType::LFG) != 0;
    }

    bool LocalPlayerIsLeader(uint64 LocalPlayerGuid) const
    {
        return LocalPlayerGuid != 0 && LocalPlayerGuid == LeaderGuid;
    }

    bool LocalPlayerIsAssistant() const
    {
        return (SelfFlags & WowGroupMemberFlags::ASSISTANT) != 0;
    }

    const FWowGroupMember* FindMember(uint64 Guid) const
    {
        return Members.FindByPredicate([Guid](const FWowGroupMember& Member)
        {
            return Member.Guid == Guid;
        });
    }
};

// Taxi node information
struct FWowTaxiNode
{
    uint32 Id = 0;
    uint32 MapId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    FString Name;

    FWowTaxiNode() = default;
    FWowTaxiNode(uint32 InId, uint32 InMapId, float InX, float InY, float InZ, const FString& InName)
        : Id(InId), MapId(InMapId), X(InX), Y(InY), Z(InZ), Name(InName) {}
};

// Taxi system information
struct FWowTaxiData
{
    uint64 NpcGuid = 0;
    uint32 CurrentNodeId = 0;
    TArray<bool> KnownNodes;        // Known taxi nodes (bitmask converted to array)
    TArray<FWowTaxiNode> AllNodes;  // All taxi nodes from DBC
    bool bTaxiWindowOpen = false;

    void Clear()
    {
        NpcGuid = 0;
        CurrentNodeId = 0;
        KnownNodes.Empty();
        bTaxiWindowOpen = false;
    }

    bool IsNodeKnown(uint32 NodeId) const
    {
        return NodeId < static_cast<uint32>(KnownNodes.Num()) && KnownNodes[NodeId];
    }

    const FWowTaxiNode* FindNode(uint32 NodeId) const
    {
        return AllNodes.FindByPredicate([NodeId](const FWowTaxiNode& Node)
        {
            return Node.Id == NodeId;
        });
    }
};
