#include "WowLocale.h"
#include "WowWorldManager.h"
#include "Kismet/GameplayStatics.h"
#include "LuaApiRegistry.h"
#include "Materials/MaterialInterface.h"
#include "WowClientPreferences.h"
#include "WowKeyBindings.h"
#include "WowScreenshotManager.h"
#include "WowSpellPresentation.h"
#include "WowEntityManager.h"
#include "WowEntity.h"
#include "WowConnectionManager.h"
#include "WowDifficultyState.h"
#include "WowPacketHandler.h"
#include "WowUpdateFields.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Formats/Dbc/TalentTabDbc.h"
#include "Formats/DbcParser.h"
#include "WowAddonLoader.h"
#include "Mpq/MpqManager.h"
#include "WowAddonSettings.h"
#include "WowUIManager.h"
#include "Mpq/MpqManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "WowAudioManager.h"
#include "WowFrameManager.h"
#include "WowItemPresentation.h"
#include "WowMacros.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

DEFINE_LOG_CATEGORY_STATIC(LogWowLuaStub, Verbose, All);

// WoW 3.3.5a Combat Rating Types (from PlayerFields.h / azerothcore)
namespace WowCombatRating
{
    inline constexpr int32 WEAPON_SKILL = 0;
    inline constexpr int32 DEFENCE = 1;
    inline constexpr int32 DODGE = 2;
    inline constexpr int32 PARRY = 3;
    inline constexpr int32 BLOCK = 4;
    inline constexpr int32 HIT_MELEE = 5;
    inline constexpr int32 HIT_RANGED = 6;
    inline constexpr int32 HIT_SPELL = 7;
    inline constexpr int32 CRIT_MELEE = 8;
    inline constexpr int32 CRIT_RANGED = 9;
    inline constexpr int32 CRIT_SPELL = 10;
    inline constexpr int32 HIT_TAKEN_MELEE = 11;
    inline constexpr int32 HIT_TAKEN_RANGED = 12;
    inline constexpr int32 HIT_TAKEN_SPELL = 13;
    inline constexpr int32 CRIT_TAKEN_MELEE = 14;
    inline constexpr int32 CRIT_TAKEN_RANGED = 15;
    inline constexpr int32 CRIT_TAKEN_SPELL = 16;
    inline constexpr int32 HASTE_MELEE = 17;
    inline constexpr int32 HASTE_RANGED = 18;
    inline constexpr int32 HASTE_SPELL = 19;
    inline constexpr int32 HIT_TAKEN = 20;
    inline constexpr int32 CRIT_TAKEN = 21;
    inline constexpr int32 RESILIENCE = 22;
    inline constexpr int32 HASTE = 23;
    inline constexpr int32 EXPERTISE = 24;
}

// WoW 3.3.5a Unit Flags
namespace WowUnitFlags
{
    inline constexpr uint32 IN_COMBAT = 0x00080000;
}

// WoW 3.3.5a School mask for spells (for GetSpellCritChance)
namespace WowSpellSchool
{
    inline constexpr int32 PHYSICAL = 0;
    inline constexpr int32 HOLY = 1;
    inline constexpr int32 FIRE = 2;
    inline constexpr int32 NATURE = 3;
    inline constexpr int32 FROST = 4;
    inline constexpr int32 SHADOW = 5;
    inline constexpr int32 ARCANE = 6;
}

// Macro for simple stubs that return a fixed value
#define STUB_RETURN_NIL(name) \
    static int L_##name(lua_State* L) { lua_pushnil(L); return 1; }
#define STUB_RETURN_ZERO(name) \
    static int L_##name(lua_State* L) { lua_pushnumber(L, 0); return 1; }
#define STUB_RETURN_FALSE(name) \
    static int L_##name(lua_State* L) { lua_pushboolean(L, 0); return 1; }
#define STUB_RETURN_TRUE(name) \
    static int L_##name(lua_State* L) { lua_pushboolean(L, 1); return 1; }
#define STUB_RETURN_EMPTY(name) \
    static int L_##name(lua_State* L) { lua_pushstring(L, ""); return 1; }
#define STUB_RETURN_NONE(name) \
    static int L_##name(lua_State* L) { return 0; }

// Helper: resolve common WoW unit strings to entity GUID
static uint64 ResolveGuidByName(const FWowLuaContext* Ctx, const FString& Name)
{
    if (!Ctx || !Ctx->EntityManager || !Ctx->ConnectionManager || Name.IsEmpty())
    {
        return 0;
    }

    const FString CharacterName = Ctx->ConnectionManager->GetCharacterName();
    if (!CharacterName.IsEmpty() && CharacterName.Equals(Name, ESearchCase::IgnoreCase))
    {
        return Ctx->EntityManager->LocalPlayerGuid;
    }

    for (const TPair<uint64, FString>& Pair : Ctx->ConnectionManager->PacketHandler.PlayerNameCache)
    {
        if (Pair.Value.Equals(Name, ESearchCase::IgnoreCase))
        {
            return Pair.Key;
        }
    }

    const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
    for (const FWowGroupMember& Member : GroupInfo.Members)
    {
        if (Member.Guid != 0 && Member.Name.Equals(Name, ESearchCase::IgnoreCase))
        {
            return Member.Guid;
        }
    }

    for (const TPair<uint64, TSharedPtr<FWowEntity>>& Pair : Ctx->EntityManager->GetAll())
    {
        const TSharedPtr<FWowEntity>& EntityPtr = Pair.Value;
        if (!EntityPtr.IsValid() || !EntityPtr->IsUnit() || EntityPtr->IsPlayer())
        {
            continue;
        }

        if (const FString* CreatureName = Ctx->ConnectionManager->PacketHandler.CreatureNameCache.Find(EntityPtr->Entry))
        {
            if (CreatureName->Equals(Name, ESearchCase::IgnoreCase))
            {
                return Pair.Key;
            }
        }
    }

    return 0;
}

static uint64 ResolveDirectUnitGuid(const FWowLuaContext* Ctx, const FString& UnitToken)
{
    if (!Ctx || !Ctx->EntityManager)
    {
        return 0;
    }

    if (UnitToken.Equals(TEXT("player"), ESearchCase::IgnoreCase))
    {
        return Ctx->EntityManager->LocalPlayerGuid;
    }

    if (UnitToken.Equals(TEXT("target"), ESearchCase::IgnoreCase) && Ctx->ConnectionManager)
    {
        return static_cast<uint64>(Ctx->ConnectionManager->GetTargetGuid());
    }

    if (UnitToken.Equals(TEXT("focus"), ESearchCase::IgnoreCase))
    {
        return Ctx->FocusGuid;
    }

    if (UnitToken.Equals(TEXT("mouseover"), ESearchCase::IgnoreCase))
    {
        return Ctx->MouseoverGuid;
    }

    // r282: "npc" - the NPC whose window is open (BankFrame's title and portrait: the banker).
    if (UnitToken.Equals(TEXT("npc"), ESearchCase::IgnoreCase) && Ctx->ConnectionManager)
    {
        if (Ctx->ConnectionManager->ActiveBankerGuid) return Ctx->ConnectionManager->ActiveBankerGuid;
        if (Ctx->ConnectionManager->PacketHandler.ActiveVendorGuid) return Ctx->ConnectionManager->PacketHandler.ActiveVendorGuid;
        return static_cast<uint64>(Ctx->ConnectionManager->GetTargetGuid());
    }

    if (UnitToken.Equals(TEXT("pet"), ESearchCase::IgnoreCase))
    {
        if (FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer())
        {
            return Player->GetField64(UnitField::SUMMON);
        }

        return 0;
    }

    if (UnitToken.StartsWith(TEXT("partypet"), ESearchCase::IgnoreCase))
    {
        if (!Ctx->ConnectionManager)
        {
            return 0;
        }

        const FString IndexString = UnitToken.RightChop(8);
        if (IndexString.Len() != 1 || IndexString[0] < TCHAR('1') || IndexString[0] > TCHAR('4'))
        {
            return 0;
        }

        const int32 PartyIndex = IndexString[0] - TCHAR('1');
        const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
        if (!GroupInfo.Members.IsValidIndex(PartyIndex))
        {
            return 0;
        }

        if (FWowPlayerEntity* PartyMember = Ctx->EntityManager->FindPlayer(GroupInfo.Members[PartyIndex].Guid))
        {
            return PartyMember->GetField64(UnitField::SUMMON);
        }

        return 0;
    }

    if (UnitToken.StartsWith(TEXT("party"), ESearchCase::IgnoreCase))
    {
        if (!Ctx->ConnectionManager)
        {
            return 0;
        }

        const FString IndexString = UnitToken.RightChop(5);
        if (IndexString.Len() != 1 || IndexString[0] < TCHAR('1') || IndexString[0] > TCHAR('4'))
        {
            return 0;
        }

        const int32 PartyIndex = IndexString[0] - TCHAR('1');
        const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
        return GroupInfo.Members.IsValidIndex(PartyIndex) ? GroupInfo.Members[PartyIndex].Guid : 0;
    }

    if (UnitToken.StartsWith(TEXT("raid"), ESearchCase::IgnoreCase))
    {
        if (!Ctx->ConnectionManager)
        {
            return 0;
        }

        const FString IndexString = UnitToken.RightChop(4);
        if (IndexString.IsEmpty())
        {
            return 0;
        }

        for (const TCHAR Digit : IndexString)
        {
            if (!FChar::IsDigit(Digit))
            {
                return 0;
            }
        }

        const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
        if (!GroupInfo.IsRaidGroup())
        {
            return 0;
        }

        const int32 RaidIndex = FCString::Atoi(*IndexString);
        if (RaidIndex <= 0)
        {
            return 0;
        }

        if (RaidIndex == 1)
        {
            return Ctx->EntityManager->LocalPlayerGuid;
        }

        const int32 MemberIndex = RaidIndex - 2;
        return GroupInfo.Members.IsValidIndex(MemberIndex) ? GroupInfo.Members[MemberIndex].Guid : 0;
    }

    return 0;
}

static uint64 ResolveUnitGuidToken(const FWowLuaContext* Ctx, const FString& UnitToken)
{
    if (!Ctx || UnitToken.IsEmpty())
    {
        return 0;
    }

    if (const uint64 DirectGuid = ResolveDirectUnitGuid(Ctx, UnitToken))
    {
        return DirectGuid;
    }

    if (UnitToken.EndsWith(TEXT("target"), ESearchCase::IgnoreCase) && UnitToken.Len() > 6)
    {
        FString BaseToken = UnitToken.LeftChop(6);
        // Stock 3.3.5 FocusFrame uses "focus-target"; addons also use "focustarget".
        BaseToken.RemoveFromEnd(TEXT("-"));
        const uint64 BaseGuid = ResolveUnitGuidToken(Ctx, BaseToken);
        if (BaseGuid == 0)
        {
            return 0;
        }

        if (BaseToken.Equals(TEXT("player"), ESearchCase::IgnoreCase) && Ctx->ConnectionManager)
        {
            const uint64 CurrentTargetGuid = static_cast<uint64>(Ctx->ConnectionManager->GetTargetGuid());
            if (CurrentTargetGuid != 0)
            {
                return CurrentTargetGuid;
            }
        }

        if (FWowEntity* BaseEntity = Ctx->EntityManager->Find(BaseGuid))
        {
            return BaseEntity->GetField64(UnitField::TARGET);
        }

        return 0;
    }

    return ResolveGuidByName(Ctx, UnitToken);
}

static uint64 ResolveUnitGuid(lua_State* L, int ArgIdx = 1)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->EntityManager) return 0;

    const FString UnitToken = UTF8_TO_TCHAR(luaL_optstring(L, ArgIdx, "player"));
    return ResolveUnitGuidToken(Ctx, UnitToken);
}

// Helper: resolve unit string to entity (wraps ResolveUnitGuid for backwards compatibility)
static FWowEntity* ResolveUnit(lua_State* L, int ArgIdx = 1)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->EntityManager) return nullptr;

    uint64 Guid = ResolveUnitGuid(L, ArgIdx);
    return Guid != 0 ? Ctx->EntityManager->Find(Guid) : nullptr;
}

enum class EWowUnitRelationship : uint8
{
    Hostile,
    Neutral,
    Friendly,
};

static EWowUnitRelationship GetUnitRelationship(const FWowLuaContext* Ctx, const FWowEntity* Unit1, const FWowEntity* Unit2)
{
    const int32 Reaction=Ctx && Ctx->ConnectionManager ? Ctx->ConnectionManager->GetUnitReaction(Unit1,Unit2) : 4;
    return Reaction<4 ? EWowUnitRelationship::Hostile : Reaction>4 ? EWowUnitRelationship::Friendly : EWowUnitRelationship::Neutral;
}

static int32 GetUnitReactionRank(const FWowLuaContext* Ctx, const FWowEntity* Unit1, const FWowEntity* Unit2)
{
    switch (GetUnitRelationship(Ctx, Unit1, Unit2))
    {
    case EWowUnitRelationship::Friendly:
        return 8;
    case EWowUnitRelationship::Hostile:
        return 2;
    default:
        return 4;
    }
}

static const FWowPacketHandler::FWowCreatureTemplate* FindCreatureTemplate(const FWowLuaContext* Ctx, const FWowEntity* Entity)
{
    if (!Ctx || !Ctx->ConnectionManager || !Entity || Entity->IsPlayer() || !Entity->IsUnit()) return nullptr;
    return Ctx->ConnectionManager->PacketHandler.CreatureTemplateCache.Find(Entity->Entry);
}

// UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_NOT_SELECTABLE
static bool IsUnitFlaggedUnattackable(const FWowEntity* Entity)
{
    return Entity && Entity->IsUnit() && (Entity->GetField(UnitField::FLAGS) & (0x00000002u | 0x00000100u | 0x02000000u)) != 0;
}

// ─── Instance / Group ───────────────────────────────────────────────────────────
static int L_IsInInstance(lua_State* L)
{
    // Return false, "none" for now (not in instance, no instance type)
    lua_pushboolean(L, false);
    lua_pushstring(L, "none");
    return 2;
}

static int L_GetNumPartyMembers(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
        if (!GroupInfo.IsEmpty() && !GroupInfo.IsRaidGroup())
        {
            lua_pushnumber(L, FMath::Max(0, static_cast<int32>(GroupInfo.MemberCount) - 1));
            return 1;
        }
    }

    lua_pushnumber(L, 0);
    return 1;
}

static int L_GetNumRaidMembers(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
        if (!GroupInfo.IsEmpty() && GroupInfo.IsRaidGroup())
        {
            lua_pushnumber(L, GroupInfo.MemberCount);
            return 1;
        }
    }

    lua_pushnumber(L, 0);
    return 1;
}

static int L_IsInGuild(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const FString& GuildName = Ctx->ConnectionManager->PacketHandler.GuildName;
        lua_pushboolean(L, !GuildName.IsEmpty() ? 1 : 0);
        return 1;
    }

    lua_pushboolean(L, 0);
    return 1;
}
static int L_GetMoney(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            lua_pushnumber(L, Player->GetCoinage());
            return 1;
        }
    }
    lua_pushnumber(L, 0);
    return 1;
}
static int L_InCombatLockdown(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            uint32 UnitFlags = Player->GetUnitFlags();
            // Check UNIT_FLAG_IN_COMBAT (0x00080000)
            bool InCombat = (UnitFlags & 0x80000) != 0;
            lua_pushboolean(L, InCombat ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int L_IsResting(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowEntity* LocalPlayer = Ctx->EntityManager->GetLocalPlayer();
        if (LocalPlayer)
        {
            // Check PLAYER_FLAGS for PLAYER_FLAGS_RESTING (0x0002)
            FWowPlayerEntity PlayerEntity(*LocalPlayer);
            uint32 PlayerFlags = PlayerEntity.GetPlayerFlags();
            bool IsResting = (PlayerFlags & 0x0002) != 0;
            lua_pushboolean(L, IsResting);
            return 1;
        }
    }
    lua_pushboolean(L, false);
    return 1;
}

static int L_IsMounted(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            uint32 MountDisplayId = Player->GetMountDisplayId();
            lua_pushboolean(L, MountDisplayId > 0 ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int L_IsFlying(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            uint32 MoveFlags = Player->Movement.MoveFlags;
            // Check MOVEMENTFLAG_FLYING (0x800000)
            bool IsFlying = (MoveFlags & 0x800000) != 0;
            lua_pushboolean(L, IsFlying ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int L_IsSwimming(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            uint32 MoveFlags = Player->Movement.MoveFlags;
            // Check MOVEMENTFLAG_SWIMMING (0x200000)
            bool IsSwimming = (MoveFlags & 0x200000) != 0;
            lua_pushboolean(L, IsSwimming ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int L_IsFalling(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            uint32 MoveFlags = Player->Movement.MoveFlags;
            // Check MOVEMENTFLAG_FALLING (0x2)
            bool IsFalling = (MoveFlags & 0x2) != 0;
            lua_pushboolean(L, IsFalling ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int L_IsStealthed(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            // Check for stealth auras (basic stealth spell IDs)
            const TArray<uint32> StealthSpellIds = { 1784, 5215, 58984 }; // Stealth, Prowl, Shadow Meld
            for (const FAuraInfo& Aura : Player->Auras)
            {
                if (Aura.bActive && StealthSpellIds.Contains(Aura.SpellId))
                {
                    lua_pushboolean(L, 1);
                    return 1;
                }
            }
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// GetPlayerMapPosition("player") → x, y (map coordinates 0.0-1.0)
static int L_GetPlayerMapPosition(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const char* Unit = luaL_optstring(L, 1, "player");

    if (Ctx && Ctx->EntityManager)
    {
        FWowEntity* Entity = ResolveUnit(L);
        if (Entity)
        {
            // Get world position from movement info
            const FVector& Position = Entity->Movement.Position;

            // TODO: Convert world coordinates to proper map coordinates (0.0-1.0)
            // For now, normalize assuming a standard map size
            // WoW maps are typically ~34000x34000 world units
            float MapX = (Position.X + 17066.0f) / 34132.0f;
            float MapY = (Position.Y + 17066.0f) / 34132.0f;

            // Clamp to valid range
            MapX = FMath::Clamp(MapX, 0.0f, 1.0f);
            MapY = FMath::Clamp(MapY, 0.0f, 1.0f);

            lua_pushnumber(L, MapX);
            lua_pushnumber(L, MapY);
            return 2;
        }
    }

    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
}

// ─── Unit API ─────────────────────────────────────────────────────────────────
static int L_UnitName(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const char* unit = luaL_optstring(L, 1, "player");

    if (strcmp(unit, "player") == 0 && Ctx && Ctx->ConnectionManager)
    {
        FString CharName = Ctx->ConnectionManager->GetCharacterName();
        if (!CharName.IsEmpty())
        {
            lua_pushstring(L, TCHAR_TO_UTF8(*CharName));
            return 1;
        }
    }

    uint64 Guid = ResolveUnitGuid(L);
    if (Guid != 0 && Ctx && Ctx->ConnectionManager)
    {
        // Check player name cache first
        if (const FString* PlayerName = Ctx->ConnectionManager->PacketHandler.PlayerNameCache.Find(Guid))
        {
            lua_pushstring(L, TCHAR_TO_UTF8(**PlayerName));
            return 1;
        }

        // For NPCs, look up by entry ID in creature name cache
        FWowEntity* Entity = Ctx->EntityManager->Find(Guid);
        if (Entity && !Entity->IsPlayer())
        {

            if (const FString* CreatureName = Ctx->ConnectionManager->PacketHandler.CreatureNameCache.Find(Entity->Entry))
            {
                lua_pushstring(L, TCHAR_TO_UTF8(**CreatureName));
                return 1;
            }
            else
            {
                // Name not cached yet - send query request
                UE_LOG(LogWowLuaStub, Warning, TEXT("UnitName: No cached name for Entry=%u, sending creature query"), Entity->Entry);
                Ctx->ConnectionManager->SendCreatureQuery(Entity->Entry, Entity->Guid);

                // Return a temporary name until the query response arrives
                lua_pushstring(L, "Unknown");
                return 1;
            }
        }

        // Fallback for unknown entities
        UE_LOG(LogWowLuaStub, Warning, TEXT("UnitName: No entity found for GUID=%llu"), Guid);
        lua_pushstring(L, "Unknown");
    }
    else
    {
        lua_pushnil(L);
    }
    return 1;
}

static const FWowPacketHandler::FWowCreatureTemplate* FindCreatureTemplate(const FWowLuaContext* Ctx, const FWowEntity* Entity);
static bool IsUnitFlaggedUnattackable(const FWowEntity* Entity);

static int L_UnitLevel(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (!Entity) { lua_pushnumber(L, 0); return 1; }
    int32 Level = Entity->GetLevel();
    // Like the original client: bosses, and enemies ten or more levels above
    // the player, report -1 so FrameXML shows the skull instead of a number.
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (const auto* Template = FindCreatureTemplate(Ctx, Entity); Template && Template->Rank == 3) Level = -1;
    else if (Ctx && Ctx->EntityManager && Ctx->ConnectionManager)
    {
        const FWowEntity* Local = Ctx->EntityManager->GetLocalPlayer();
        if (Local && Local != Entity && Level >= Local->GetLevel() + 10
            && Ctx->ConnectionManager->GetUnitReaction(Entity, Local) < 4)
            Level = -1;
    }
    lua_pushnumber(L, Level);
    return 1;
}

// Units the server presents as corpses while alive (feign death: UNIT_FLAG2_FEIGN_DEATH
// or UNIT_DYNFLAG_DEAD), e.g. quest NPCs lying "dead". Like the original client, other
// players see them as dead with no health; the local player still sees its own health.
static bool ShowsAsFeignDead(lua_State* L, const FWowEntity* Entity)
{
    if (!Entity || !Entity->IsUnit()) return false;
    if ((Entity->GetField(UnitField::FLAGS_2) & 0x1u) == 0 && (Entity->GetField(UnitField::DYNAMIC_FLAGS) & 0x20u) == 0)
        return false;
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const FWowEntity* Local = Ctx && Ctx->EntityManager ? Ctx->EntityManager->GetLocalPlayer() : nullptr;
    return Entity != Local;
}

static int L_UnitHealth(lua_State* L)
{
    const FWowEntity* Entity = ResolveUnit(L);
    lua_pushnumber(L, Entity && Entity->IsUnit() && !ShowsAsFeignDead(L, Entity) ? FMath::Max(0, Entity->GetHealth()) : 0);
    return 1;
}

static int L_UnitHealthMax(lua_State* L)
{
    const FWowEntity* Entity = ResolveUnit(L);
    lua_pushnumber(L, Entity && Entity->IsUnit() ? FMath::Max(0, Entity->GetMaxHealth()) : 0);
    return 1;
}

static int L_UnitPower(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    const FString UnitToken = UTF8_TO_TCHAR(luaL_optstring(L, 1, "player"));
    int32 PowerType = static_cast<int32>(luaL_optnumber(L, 2, 0)); // 0=mana, 1=rage, 2=focus, 3=energy, etc.

    if (Entity && Entity->IsUnit())
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        // No power type given: the unit's own (warriors read rage, rogues energy).
        if (lua_isnoneornil(L, 2)) PowerType = Unit->GetPowerTypeId();
        // Rage and runic power are stored in tenths (1000 = 100 rage), like the real client shows them.
        const int32 PowerScale = (PowerType == 1 || PowerType == 6) ? 10 : 1;
        int32 Power = Unit->GetPower(static_cast<uint8>(PowerType)) / PowerScale;
        const int32 MaxPower = Unit->GetMaxPower(static_cast<uint8>(PowerType)) / PowerScale;
        if (UnitToken.Equals(TEXT("player"), ESearchCase::IgnoreCase) && Power == 0 && MaxPower == 0)
        {
            UE_LOG(LogWowLuaStub, Verbose, TEXT("UnitPower: Player power is uninitialized, returning 1"));
            lua_pushnumber(L, 1);
            return 1;
        }
        UE_LOG(LogWowLuaStub, Verbose, TEXT("UnitPower: Entity found, PowerType=%d, Power=%d"), PowerType, Power);
        lua_pushnumber(L, Power);
    }
    else
    {
        UE_LOG(LogWowLuaStub, Warning, TEXT("UnitPower: No entity found or not a unit, PowerType=%d"), PowerType);
        lua_pushnumber(L, UnitToken.Equals(TEXT("player"), ESearchCase::IgnoreCase) ? 1 : 0);
    }
    return 1;
}

static int L_UnitPowerMax(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    const FString UnitToken = UTF8_TO_TCHAR(luaL_optstring(L, 1, "player"));
    int32 PowerType = static_cast<int32>(luaL_optnumber(L, 2, 0)); // 0=mana, 1=rage, 2=focus, 3=energy, etc.

    if (Entity && Entity->IsUnit())
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        if (lua_isnoneornil(L, 2)) PowerType = Unit->GetPowerTypeId();
        const int32 PowerScale = (PowerType == 1 || PowerType == 6) ? 10 : 1; // rage / runic power in tenths
        int32 MaxPower = Unit->GetMaxPower(static_cast<uint8>(PowerType)) / PowerScale;
        if (UnitToken.Equals(TEXT("player"), ESearchCase::IgnoreCase) && MaxPower == 0)
        {
            UE_LOG(LogWowLuaStub, Verbose, TEXT("UnitPowerMax: Player max power is uninitialized, returning 1"));
            lua_pushnumber(L, 1);
            return 1;
        }
        UE_LOG(LogWowLuaStub, Verbose, TEXT("UnitPowerMax: Entity found, PowerType=%d, MaxPower=%d"), PowerType, MaxPower);
        lua_pushnumber(L, MaxPower);
    }
    else
    {
        UE_LOG(LogWowLuaStub, Warning, TEXT("UnitPowerMax: No entity found or not a unit, PowerType=%d"), PowerType);
        lua_pushnumber(L, UnitToken.Equals(TEXT("player"), ESearchCase::IgnoreCase) ? 1 : 0);
    }
    return 1;
}

// WoW class ID → (display name, token) — 3.3.5a class IDs
static const char* WowClassNames[] = {
    "Unknown", "Warrior", "Paladin", "Hunter", "Rogue",
    "Priest", "Death Knight", "Shaman", "Mage", "Warlock",
    "Unknown", "Druid"
};
static const char* WowClassTokens[] = {
    "UNKNOWN", "WARRIOR", "PALADIN", "HUNTER", "ROGUE",
    "PRIEST", "DEATHKNIGHT", "SHAMAN", "MAGE", "WARLOCK",
    "UNKNOWN", "DRUID"
};

static int L_UnitClass(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity)
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        if (Entity->IsUnit())
        {
            uint8 ClassId = Unit->GetClassId();
            if (ClassId < UE_ARRAY_COUNT(WowClassNames))
            {
                lua_pushstring(L, WowClassNames[ClassId]);
                lua_pushstring(L, WowClassTokens[ClassId]);
                return 2;
            }

			// Preserve custom server class IDs.  A protocol/client-data adapter can
			// later replace this fallback with the server's localized class name,
			// but it must never silently turn an unknown class into Warrior.
			const FString CustomName = FString::Printf(TEXT("Class %u"), ClassId);
			const FString CustomToken = FString::Printf(TEXT("CLASS_%u"), ClassId);
			lua_pushstring(L, TCHAR_TO_UTF8(*CustomName));
			lua_pushstring(L, TCHAR_TO_UTF8(*CustomToken));
			return 2;
        }
    }
	lua_pushstring(L, "Unknown");
	lua_pushstring(L, "UNKNOWN");
    return 2;
}

static int L_UnitRace(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity && Entity->IsUnit())
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        uint8 RaceId = Unit->GetRaceId();

        // Look up race name from ChrRaces.dbc
        const FChrRacesDbcEntry* RaceEntry = FDbcStore::Get().ChrRaces().GetById(RaceId);
        if (RaceEntry && !RaceEntry->Name.IsEmpty())
        {
            // UnitRace returns the name and the race's file token ("Night Elf", "NightElf";
            // "Undead", "Scourge"), which FrameXML builds texture paths from (DressUpBackground-...).
            lua_pushstring(L, TCHAR_TO_UTF8(*RaceEntry->Name));
            lua_pushstring(L, TCHAR_TO_UTF8(RaceEntry->ClientFileString.IsEmpty() ? *RaceEntry->Name : *RaceEntry->ClientFileString));
            return 2;
        }
    }
    lua_pushstring(L, "Human");
    lua_pushstring(L, "Human");
    return 2;
}

static int L_UnitIsDead(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity)
    {
        // Only consider dead if MaxHealth > 0 (field has been set) AND health <= 0,
        // OR if stand state is explicitly DEAD (byte 0 of BYTES_1 == 7).
        // Uninitialized health fields default to 0 which would falsely report
        // live entities as dead.
        int32 MaxHP = Entity->GetMaxHealth();
        int32 HP = Entity->GetHealth();
        uint8 StandState = Entity->GetFieldByte(UnitField::BYTES_1, 0);
        bool bDead = (MaxHP > 0 && HP <= 0) || StandState == WowStandState::DEAD || ShowsAsFeignDead(L, Entity);
        lua_pushboolean(L, bDead ? 1 : 0);
    }
    else
        lua_pushboolean(L, 0);
    return 1;
}

static int L_UnitIsPlayer(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity)
        lua_pushboolean(L, Entity->IsPlayer() ? 1 : 0);
    else
        lua_pushboolean(L, 0);
    return 1;
}

static int L_UnitExists(lua_State* L)
{
    uint64 Guid = ResolveUnitGuid(L);
    lua_pushboolean(L, Guid != 0 ? 1 : 0);
    return 1;
}

static int L_UnitGUID(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(Entity->Guid));
        lua_pushstring(L, buf);
    }
    else
    {
        lua_pushstring(L, "0x0000000000000000");
    }
    return 1;
}

// Additional Unit API functions
static int L_UnitIsDeadOrGhost(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity)
    {
        int32 MaxHP = Entity->GetMaxHealth();
        int32 HP = Entity->GetHealth();
        uint8 StandState = Entity->GetFieldByte(UnitField::BYTES_1, 0);
        bool bDead = (MaxHP > 0 && HP <= 0) || StandState == WowStandState::DEAD || ShowsAsFeignDead(L, Entity);
        lua_pushboolean(L, bDead ? 1 : 0);
    }
    else
        lua_pushboolean(L, 0);
    return 1;
}

static int L_UnitIsFriend(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    FWowEntity* Unit1 = nullptr;
    FWowEntity* Unit2 = nullptr;

    if (lua_gettop(L) >= 2)
    {
        Unit1 = ResolveUnit(L, 1);
        Unit2 = ResolveUnit(L, 2);
    }
    else
    {
        Unit1 = Ctx && Ctx->EntityManager ? Ctx->EntityManager->Find(Ctx->EntityManager->LocalPlayerGuid) : nullptr;
        Unit2 = ResolveUnit(L, 1);
    }

    lua_pushboolean(L, GetUnitRelationship(Ctx, Unit1, Unit2) == EWowUnitRelationship::Friendly ? 1 : 0);
    return 1;
}

static int L_UnitIsEnemy(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    FWowEntity* Unit1 = nullptr;
    FWowEntity* Unit2 = nullptr;

    if (lua_gettop(L) >= 2)
    {
        Unit1 = ResolveUnit(L, 1);
        Unit2 = ResolveUnit(L, 2);
    }
    else
    {
        Unit1 = Ctx && Ctx->EntityManager ? Ctx->EntityManager->Find(Ctx->EntityManager->LocalPlayerGuid) : nullptr;
        Unit2 = ResolveUnit(L, 1);
    }

    lua_pushboolean(L, GetUnitRelationship(Ctx, Unit1, Unit2) == EWowUnitRelationship::Hostile ? 1 : 0);
    return 1;
}

static int L_UnitAffectingCombat(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity && Entity->IsUnit())
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        uint32 UnitFlags = Unit->GetUnitFlags();

        // Check UNIT_FLAG_IN_COMBAT flag
        bool InCombat = (UnitFlags & WowUnitFlags::IN_COMBAT) != 0;
        lua_pushboolean(L, InCombat ? 1 : 0);
        return 1;
    }

    lua_pushboolean(L, 0);
    return 1;
}

static FString GetSpellIconTexturePath(uint32 SpellId);

static int PushUnitAura(lua_State* L, bool bHelpfulDefault)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const FWowEntity* Entity = ResolveUnit(L);
    if (!Ctx || !Entity || !Entity->IsUnit()) { lua_pushnil(L); return 1; }
    const bool bByName = lua_type(L, 2) == LUA_TSTRING;
    int32 Index = bByName ? 1 : static_cast<int32>(luaL_optinteger(L, 2, 1));
    const FString WantedName = bByName ? UTF8_TO_TCHAR(lua_tostring(L, 2)) : TEXT("");
    const FString Filter = UTF8_TO_TCHAR(luaL_optstring(L, bByName ? 4 : 3, ""));
    const bool bHelpful = Filter.Contains(TEXT("HARMFUL")) ? false :
        (Filter.Contains(TEXT("HELPFUL")) ? true : bHelpfulDefault);
    const uint64 PlayerGuid = Ctx->EntityManager->LocalPlayerGuid;
    for (const FAuraInfo& Aura : Entity->Auras)
    {
        if (!Aura.bActive || !Aura.SpellId) continue;
        const bool bPositive = (Aura.Flags & 0x10) != 0 && (Aura.Flags & 0x80) == 0;
        if (bHelpful != bPositive || (Filter.Contains(TEXT("PLAYER")) && Aura.CasterGuid != PlayerGuid)) continue;
        const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(Aura.SpellId);
        // Like the real client: auras the client data does not know (server-side markers such as
        // 61988 Immune Shield Marker) or that are flagged SPELL_ATTR1_NO_AURA_ICON never reach the UI.
        if (!Spell || (Spell->AttributesEx & 0x10000000)) continue;
        if (bByName && !Spell->SpellName.Equals(WantedName)) continue;
        if (!bByName && --Index != 0) continue;
        lua_pushstring(L, TCHAR_TO_UTF8(*(Spell ? Spell->SpellName : FString::Printf(TEXT("Spell %u"),Aura.SpellId))));
        lua_pushstring(L, Spell ? TCHAR_TO_UTF8(*Spell->Rank) : "");
        lua_pushstring(L, TCHAR_TO_UTF8(*GetSpellIconTexturePath(Aura.SpellId)));
        lua_pushinteger(L, Aura.Charges);
        const char* DispelType = nullptr;
        switch (Spell ? Spell->Dispel : 0) {
            case 1: DispelType = "Magic"; break;
            case 2: DispelType = "Curse"; break;
            case 3: DispelType = "Disease"; break;
            case 4: DispelType = "Poison"; break;
        }
        if (DispelType) lua_pushstring(L, DispelType); else lua_pushnil(L);
        lua_pushnumber(L, Aura.DurationMs * 0.001);
        lua_pushnumber(L, Aura.DurationMs > 0 ? Aura.ReceivedAtSeconds + Aura.RemainingMs * 0.001 : 0.0);
        const char* Caster = nullptr;
        for (const char* Token : {"player", "pet", "target", "focus", "party1", "party2", "party3", "party4"})
        {
            if (Aura.CasterGuid && ResolveDirectUnitGuid(Ctx, UTF8_TO_TCHAR(Token)) == Aura.CasterGuid)
            { Caster = Token; break; }
        }
        if (Caster) lua_pushstring(L, Caster); else lua_pushnil(L);
        lua_pushboolean(L, bPositive && Spell && Spell->Dispel == 1 && !(Spell->AttributesEx4 & 0x40));
        lua_pushboolean(L, 0); // consolidation is optional; keep all auras visible
        lua_pushinteger(L, Aura.SpellId);
        return 11;
    }
    lua_pushnil(L);
    return 1;
}

static int L_UnitBuff(lua_State* L) { return PushUnitAura(L, true); }
static int L_UnitDebuff(lua_State* L) { return PushUnitAura(L, false); }
static int L_UnitAura(lua_State* L) { return PushUnitAura(L, true); }

static int L_GetUnitName(lua_State* L)
{
    // Alias for UnitName
    return L_UnitName(L);
}

static int L_UnitPowerType(lua_State* L)
{
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity && Entity->IsUnit())
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        uint8 PowerType = Unit->GetPowerTypeId();

        // Map power type ID to string
        const char* PowerTypeName = "MANA";
        switch (PowerType)
        {
        case 0: PowerTypeName = "MANA"; break;
        case 1: PowerTypeName = "RAGE"; break;
        case 2: PowerTypeName = "FOCUS"; break;
        case 3: PowerTypeName = "ENERGY"; break;
        case 4: PowerTypeName = "HAPPINESS"; break; // Pet happiness
        case 6: PowerTypeName = "RUNIC_POWER"; break; // Death Knight
        default: PowerTypeName = "MANA"; break;
        }

        lua_pushnumber(L, PowerType);
        lua_pushstring(L, PowerTypeName);
        return 2;
    }
    lua_pushnumber(L, 0);
    lua_pushstring(L, "MANA");
    return 2;
}

static int L_UnitStat(lua_State* L)
{
    // UnitStat(unit, statIndex) → base, stat, posBuff, negBuff
    // statIndex: 1=Strength, 2=Agility, 3=Stamina, 4=Intellect, 5=Spirit
    FWowEntity* Entity = ResolveUnit(L);
    int32 StatIndex = static_cast<int32>(luaL_checknumber(L, 2));

    if (Entity && Entity->IsUnit() && StatIndex >= 1 && StatIndex <= 5)
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        uint32 StatValue = 0;

        switch (StatIndex)
        {
        case 1: StatValue = Unit->GetStrength(); break;
        case 2: StatValue = Unit->GetAgility(); break;
        case 3: StatValue = Unit->GetStamina(); break;
        case 4: StatValue = Unit->GetIntellect(); break;
        case 5: StatValue = Unit->GetSpirit(); break;
        }

        // For now, return the total stat as both base and current
        // TODO: Implement stat calculation to split base/buffs/debuffs
        lua_pushnumber(L, StatValue); // base
        lua_pushnumber(L, StatValue); // stat
        lua_pushnumber(L, 0); // posBuff
        lua_pushnumber(L, 0); // negBuff
    }
    else
    {
        lua_pushnumber(L, 10); // base
        lua_pushnumber(L, 10); // stat
        lua_pushnumber(L, 0); // posBuff
        lua_pushnumber(L, 0); // negBuff
    }
    return 4;
}

static int L_UnitAttackPower(lua_State* L)
{
    // UnitAttackPower(unit) → base, posBuff, negBuff
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity && Entity->IsUnit())
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        int32 AttackPower = Unit->GetAttackPower();

        lua_pushnumber(L, AttackPower); // base
        lua_pushnumber(L, 0); // posBuff (TODO: implement buff/debuff calculation)
        lua_pushnumber(L, 0); // negBuff
    }
    else
    {
        lua_pushnumber(L, 100); // base
        lua_pushnumber(L, 0); // posBuff
        lua_pushnumber(L, 0); // negBuff
    }
    return 3;
}

static int L_UnitRangedAttackPower(lua_State* L)
{
    // UnitRangedAttackPower(unit) → base, posBuff, negBuff
    FWowEntity* Entity = ResolveUnit(L);
    if (Entity && Entity->IsUnit())
    {
        FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
        int32 RangedAttackPower = Unit->GetRangedAttackPower();

        lua_pushnumber(L, RangedAttackPower); // base
        lua_pushnumber(L, 0); // posBuff (TODO: implement buff/debuff calculation)
        lua_pushnumber(L, 0); // negBuff
    }
    else
    {
        lua_pushnumber(L, 50); // base
        lua_pushnumber(L, 0); // posBuff
        lua_pushnumber(L, 0); // negBuff
    }
    return 3;
}

// ─── Locale / client ────────────────────────────────────────────────────────────
static int L_GetLocale(lua_State* L)
{
    lua_pushstring(L, TCHAR_TO_UTF8(*WowLocale::Get()));
    return 1;
}

static int L_GetBuildInfo(lua_State* L)
{
    lua_pushstring(L, "3.3.5");     // version
    lua_pushnumber(L, 12340);        // build
    lua_pushstring(L, "Dec 12 2009"); // date
    lua_pushnumber(L, 30300);        // tocversion
    return 4;
}

static int L_GetRealmName(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        // The realm this session entered (addon profiles are keyed "Character - Realm").
        const FString RealmName = Ctx->ConnectionManager->GetSelectedRealmName();
        const TArray<FWowRealmInfo> CachedRealms = Ctx->ConnectionManager->GetCachedRealms();
        if (!RealmName.IsEmpty())
        {
            lua_pushstring(L, TCHAR_TO_UTF8(*RealmName));
        }
        else if (CachedRealms.Num() > 0)
        {
            lua_pushstring(L, TCHAR_TO_UTF8(*CachedRealms[0].Name));
        }
        else
        {
            lua_pushstring(L, "WowUnreal");
        }
    }
    else
    {
        lua_pushstring(L, "WowUnreal");
    }
    return 1;
}

static int L_GetServerTime(lua_State* L)
{
    // LOGIN_SETTIMESPEED carries realm-local calendar time, not a UTC epoch.
    // Use actual UTC as fallback; monotonic process uptime is never a Unix timestamp.
    lua_pushnumber(L, static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()));
    return 1;
}

static int L_GetGameTime(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const FDateTime LocalNow = FDateTime::Now();
    const double Minutes = Ctx && Ctx->ConnectionManager
        ? Ctx->ConnectionManager->PacketHandler.RealmClock.Sample(FPlatformTime::Seconds(), LocalNow)
        : LocalNow.GetTimeOfDay().GetTotalMinutes();
    lua_pushnumber(L, FMath::FloorToInt(Minutes) / 60);
    lua_pushnumber(L, FMath::FloorToInt(Minutes) % 60);
    return 2;
}

// ─── Screen / resolution ────────────────────────────────────────────────────────
// WoW returns screen dimensions in UI coordinate units (height is always 768 base,
// width varies with aspect ratio).  Addons use these to position frames correctly.
static int L_GetScreenWidth(lua_State* L)
{
    FVector2D Size(1920, 1080);
    if (GEngine && GEngine->GameViewport)
    {
        GEngine->GameViewport->GetViewportSize(Size);
    }
    // Convert to WoW UI units: width = viewport_width / (viewport_height / 768)
    float UIScale = FMath::Max(Size.Y, 1.0) / 768.0f * FWowClientPreferences::GetUIScale();
    lua_pushnumber(L, Size.X / UIScale);
    return 1;
}

static int L_GetScreenHeight(lua_State* L)
{
    // WoW's base UI height is always 768 units
    lua_pushnumber(L, 768.0 / FWowClientPreferences::GetUIScale());
    return 1;
}

// ─── Chat ─────────────────────────────────────────────────────────────────────
namespace
{
int32 WowChatTypeFromLua(const char* ChatType)
{
    struct FEntry { const char* Name; int32 Type; };
    static const FEntry Entries[] = {
        {"SAY", 1}, {"PARTY", 2}, {"RAID", 3}, {"GUILD", 4}, {"OFFICER", 5}, {"YELL", 6},
        {"WHISPER", 7}, {"EMOTE", 10}, {"CHANNEL", 17}, {"AFK", 0x17}, {"DND", 0x18},
        {"RAID_WARNING", 0x28}, {"BATTLEGROUND", 0x2C} };
    for (const FEntry& Entry : Entries)
        if (FCStringAnsi::Stricmp(ChatType, Entry.Name) == 0) return Entry.Type;
    return 1;
}
}

static int L_SendChatMessage(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        // SendChatMessage(msg [, chatType [, language [, channel/target]]])
        const FString Message = UTF8_TO_TCHAR(luaL_optstring(L, 1, ""));
        const int32 Type = WowChatTypeFromLua(luaL_optstring(L, 2, "SAY"));
        FString Target;
        if (lua_type(L, 4) == LUA_TNUMBER)
            Target = Ctx->ConnectionManager->PacketHandler.GetChannelNameByNumber(static_cast<int32>(lua_tonumber(L, 4)));
        else Target = UTF8_TO_TCHAR(luaL_optstring(L, 4, ""));
        if (Type == 17 && Target.IsEmpty()) return 0;
        int32 Language = static_cast<int32>(Ctx->ConnectionManager->GetDefaultChatLanguage());
        if (lua_type(L, 3) == LUA_TNUMBER) Language = static_cast<int32>(lua_tonumber(L, 3));
        Ctx->ConnectionManager->SendChatMessage(Message, Type, Target, Language);
    }
    return 0;
}

static int L_UnitCreatureType(lua_State* L2)
{
    // UnitCreatureType(unit) -> creature type from SMSG_CREATURE_QUERY_RESPONSE.
    static const char* Types[] = { nullptr, "Beast", "Dragonkin", "Demon", "Elemental", "Giant", "Undead",
        "Humanoid", "Critter", "Mechanical", "Not specified", "Totem", "Non-combat Pet", "Gas Cloud" };
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
    FWowEntity* Entity = ResolveUnit(L2, 1);
    if (!Entity || !Entity->IsUnit()) { lua_pushnil(L2); return 1; }
    if (Entity->IsPlayer()) { lua_pushstring(L2, "Humanoid"); return 1; }
    const auto* Template = FindCreatureTemplate(Ctx, Entity);
    if (Template && Template->CreatureType > 0 && Template->CreatureType < UE_ARRAY_COUNT(Types))
        lua_pushstring(L2, Types[Template->CreatureType]);
    else
        lua_pushnil(L2);
    return 1;
}

static int L_UnitCreatureFamily(lua_State* L2)
{
    // UnitCreatureFamily(unit) -> beast/demon family name, nil when none.
    struct FFamily { uint32 Id; const char* Name; };
    static const FFamily Families[] = {
        {1,"Wolf"},{2,"Cat"},{3,"Spider"},{4,"Bear"},{5,"Boar"},{6,"Crocolisk"},{7,"Carrion Bird"},{8,"Crab"},
        {9,"Gorilla"},{11,"Raptor"},{12,"Tallstrider"},{15,"Felhunter"},{16,"Voidwalker"},{17,"Succubus"},
        {19,"Doomguard"},{20,"Scorpid"},{21,"Turtle"},{23,"Imp"},{24,"Bat"},{25,"Hyena"},{26,"Bird of Prey"},
        {27,"Wind Serpent"},{28,"Remote Control"},{29,"Felguard"},{30,"Dragonhawk"},{31,"Ravager"},
        {32,"Warp Stalker"},{33,"Sporebat"},{34,"Nether Ray"},{35,"Serpent"},{37,"Moth"},{38,"Chimaera"},
        {39,"Devilsaur"},{40,"Ghoul"},{41,"Silithid"},{42,"Worm"},{43,"Rhino"},{44,"Wasp"},{45,"Core Hound"},
        {46,"Spirit Beast"} };
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
    const auto* Template = FindCreatureTemplate(Ctx, ResolveUnit(L2, 1));
    if (Template)
        for (const FFamily& Family : Families)
            if (Family.Id == Template->Family) { lua_pushstring(L2, Family.Name); return 1; }
    lua_pushnil(L2);
    return 1;
}

static int L_UnitClassification(lua_State* L2)
{
    // Creature rank: 0 normal, 1 elite, 2 rareelite, 3 worldboss, 4 rare.
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
    const FWowEntity* Unit = ResolveUnit(L2, 1);
    const auto* Template = FindCreatureTemplate(Ctx, Unit);
    // r342: a tamed/summoned pet (UNIT_FLAG_PLAYER_CONTROLLED) drops its wild elite/rare rank.
    if (Unit && Unit->IsUnit() && (Unit->GetField(UnitField::FLAGS) & 0x00000008u) != 0) Template = nullptr;
    static const char* Ranks[] = { "normal", "elite", "rareelite", "worldboss", "rare" };
    lua_pushstring(L2, Template && Template->Rank < UE_ARRAY_COUNT(Ranks) ? Ranks[Template->Rank] : "normal");
    return 1;
}

static int L_SendAddonMessage(lua_State* L)
{
    // SendAddonMessage(prefix, text, "PARTY"|"RAID"|"GUILD"|"BATTLEGROUND"|"WHISPER" [, target])
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    // Byte for byte (one TCHAR per byte): addon payloads may be binary.
    auto BytesToString = [](const char* Bytes, size_t Length)
    {
        FString Result;
        Result.Reserve(static_cast<int32>(Length));
        for (size_t Index = 0; Index < Length; ++Index) Result.AppendChar(static_cast<TCHAR>(static_cast<uint8>(Bytes[Index])));
        return Result;
    };
    size_t PrefixLength = 0, TextLength = 0;
    const char* PrefixBytes = luaL_checklstring(L, 1, &PrefixLength);
    const char* TextBytes = luaL_optlstring(L, 2, "", &TextLength);
    const FString Prefix = BytesToString(PrefixBytes, PrefixLength);
    const FString Text = BytesToString(TextBytes, TextLength);
    const char* Distribution = luaL_checkstring(L, 3);
    const FString Target = UTF8_TO_TCHAR(luaL_optstring(L, 4, ""));
    if (Prefix.Len() + Text.Len() > 254) return luaL_error(L, "SendAddonMessage: message too long");
    if (Ctx && Ctx->ConnectionManager)
    {
        const int32 Type = WowChatTypeFromLua(Distribution);
        if (Type == 7 && Target.IsEmpty()) return luaL_error(L, "SendAddonMessage: whisper requires a target");
        Ctx->ConnectionManager->SendAddonMessage(Prefix, Text, Type, Target);
    }
    return 0;
}

static int L_GetNumLanguages(lua_State* L)
{
    lua_pushnumber(L, 1); // Just Common for now
    return 1;
}

static int L_GetDefaultLanguage(lua_State* L)
{
    lua_pushstring(L, "Common");
    return 1;
}

static int L_GetLanguageByIndex(lua_State* L)
{
    // GetLanguageByIndex(index) → languageName, languageId
    lua_pushstring(L, "Common");
    lua_pushnumber(L, 7); // LANG_COMMON = 7 in WoW
    return 2;
}

// ─── Action bar functions ───────────────────────────────────────────────────────
// Helper: safely read action slot from arg 1, returns -1 if nil
static int32 SafeActionSlot(lua_State* L)
{
    if (lua_isnil(L, 1) || lua_isnone(L, 1)) return -1;
    return static_cast<int32>(lua_tonumber(L, 1)) - 1; // Convert 1-based to 0-based
}

static int32 SafePetActionSlot(lua_State* L)
{
    if (lua_isnil(L, 1) || lua_isnone(L, 1)) return -1;
    return static_cast<int32>(lua_tonumber(L, 1)) - 1; // Convert 1-based to 0-based
}

static const FWowPetActionBarState* GetPetActionBarState(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return (Ctx && Ctx->ConnectionManager) ? &Ctx->ConnectionManager->PacketHandler.PetActionBar : nullptr;
}

static FString GetSpellIconTexturePath(uint32 SpellId)
{
    const FSpellDbcEntry* SpellEntry = FDbcStore::Get().Spells().GetById(SpellId);
    if (!SpellEntry)
    {
        return TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
    }

    const FSpellIconDbcEntry* IconEntry = FDbcStore::Get().SpellIcons().GetById(SpellEntry->SpellIconID);
    return (IconEntry && !IconEntry->TexturePath.IsEmpty())
        ? IconEntry->TexturePath
        : TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
}

static FString GetPetActionTexturePath(const FWowPetActionSlot& Slot)
{
    if (Slot.IsSpell() && Slot.ActionId != 0)
    {
        return GetSpellIconTexturePath(Slot.ActionId);
    }

    if (Slot.IsCommand())
    {
        switch (Slot.ActionId)
        {
        case WowPetCommand::ATTACK: return TEXT("Interface\\Icons\\Ability_GhoulFrenzy");
        case WowPetCommand::FOLLOW: return TEXT("Interface\\Icons\\Ability_Tracking");
        case WowPetCommand::STAY: return TEXT("Interface\\Icons\\Spell_Nature_TimeStop");
        case WowPetCommand::ABANDON: return TEXT("Interface\\Icons\\Spell_Shadow_AnimateDead");
        default: break;
        }
    }

    if (Slot.IsReaction())
    {
        switch (Slot.ActionId)
        {
        case WowPetReactState::AGGRESSIVE: return TEXT("Interface\\Icons\\Ability_Racial_BloodRage");
        case WowPetReactState::DEFENSIVE: return TEXT("Interface\\Icons\\Ability_Defend");
        case WowPetReactState::PASSIVE: return TEXT("Interface\\Icons\\Ability_Seal");
        default: break;
        }
    }

    return TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
}

static bool IsPetActionActive(const FWowPetActionBarState& PetActionBar, const FWowPetActionSlot& Slot)
{
    if (Slot.IsReaction())
    {
        return PetActionBar.ReactState == Slot.ActionId;
    }

    if (Slot.IsCommand())
    {
        return PetActionBar.CommandState == Slot.ActionId;
    }

    return false;
}

struct FClientSpellBookTab
{
    FString Name, Texture;
    uint32 Skill=0;
    int32 Offset=0, Count=0, HighestOffset=0, HighestCount=0;
};
struct FClientSpellBook
{
    TArray<FClientSpellBookTab> Tabs;
    TArray<uint32> Spells;
    TArray<int32> HighestSlots;
};
static const FClientSpellBook& GetClientSpellBook(lua_State* L)
{
    static FClientSpellBook Book;
    static TWeakObjectPtr<UWowConnectionManager> Connection;
    static TArray<uint32> Previous;
    static uint32 PreviousIdentity=MAX_uint32, Revision=MAX_uint32;
    auto* Ctx=WowLuaApi::GetContext(L);
    auto* CM=Ctx ? Ctx->ConnectionManager : nullptr;
    auto* Player=CM ? CM->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    const uint32 Class=Player ? Player->GetClassId() : 0, Race=Player ? Player->GetRaceId() : 0;
    const uint32 Identity=(Class<<8)|Race;
    const uint32 ClassMask=Class>0 && Class<=32 ? 1u<<(Class-1) : 0;
    const uint32 RaceMask=Race>0 && Race<=32 ? 1u<<(Race-1) : 0;
    auto& Dbc=FDbcStore::Get(); auto& Skills=Dbc.SkillLines();
    TArray<uint32> Known=CM ? CM->PacketHandler.KnownSpells.Array() : TArray<uint32>();
    Known.Sort();
    if (Connection.Get()==CM && Previous==Known && PreviousIdentity==Identity && Revision==Skills.Revision) return Book;
    Connection=CM; Previous=Known; PreviousIdentity=Identity; Revision=Skills.Revision; Book={};
    Book.Tabs.Add({TEXT("General"),TEXT("Interface\\Icons\\INV_Misc_Book_09")});
    // Category 7 contains class skill trees, plus mounts/companions (separate UI).
    for (const auto& Pair:Skills.GetLines())
    {
        const auto& Line=Pair.Value;
        if (Line.Category!=7 || !(Line.ClassMask&ClassMask) || Line.Id==777 || Line.Id==778 || Line.Name.IsEmpty()) continue;
        FClientSpellBookTab Tab; Tab.Name=Line.Name; Tab.Skill=Line.Id;
        const auto* Icon=Dbc.SpellIcons().GetById(Line.Icon);
        Tab.Texture=Icon ? Icon->TexturePath : TEXT("Interface\\Icons\\INV_Misc_Book_09");
        Book.Tabs.Add(MoveTemp(Tab));
    }
    Book.Tabs.Sort([](const auto& A,const auto& B) { if (!A.Skill || !B.Skill) return !A.Skill && B.Skill; return A.Name==B.Name ? A.Skill<B.Skill : A.Name<B.Name; });
    TArray<TArray<uint32>> Groups; Groups.SetNum(Book.Tabs.Num());
    for (uint32 Id:Known)
    {
        const auto* Spell=Dbc.Spells().GetById(Id);
        if (!Spell || Spell->SpellName.IsEmpty()) continue;
        int32 TabIndex=0; bool bProficiency=false, bCompanion=false, bOnSkillLine=false;
        if (const auto* Rows=Skills.ForSpell(Id)) for (const auto& Row:*Rows)
        {
            const auto* Line=Skills.GetLines().Find(Row.Skill);
            if (!Line) continue;
            bCompanion |= Row.Skill==777 || Row.Skill==778;
            // Armor/weapon proficiencies are useful General entries even when
            // hidden in the original spellbook; do not expose other hidden passives.
            bProficiency |= (Line->Category==6 || Line->Category==8) && (Spell->Effect[0]==60 || Spell->Effect[1]==60 || Spell->Effect[2]==60);
            if ((Row.ClassMask && !(Row.ClassMask&ClassMask)) || (Row.RaceMask && !(Row.RaceMask&RaceMask)) ||
                (Row.ExcludeClass&ClassMask) || (Row.ExcludeRace&RaceMask)) continue;
            // Only the class's own skill lines, racials, professions, languages and proficiencies
            // put a spell in the book; another class's line or a pet family line does not.
            if (Line->Category!=7 || (Line->ClassMask&ClassMask) || !Line->ClassMask) bOnSkillLine=true;
            for (int32 I=1; I<Book.Tabs.Num(); ++I) if (Book.Tabs[I].Skill==Row.Skill) { TabIndex=I; break; }
        }
        // Like the original client: a learned spell is in the book only through a SkillLineAbility
        // row of one of the player's skill lines. The server also teaches internal, quest, aura
        // and item-effect spells with no such row; they all landed in General (Stefan: lots of
        // things that should not be there). Profession recipes (SPELL_ATTR0_TRADESPELL, 0x20)
        // belong to the profession window, hidden spells (0x80) nowhere. Melee Attack, Auto Shot
        // and the wand Shoot are kept, as in the original General tab.
        static const TSet<uint32> AlwaysInGeneral = { 6603u, 75u, 5019u };
        const bool bAlways = AlwaysInGeneral.Contains(Id);
        // Armour and weapon proficiencies (Cloth, Mail, Shield, Staves, One-Handed Maces ...) are
        // not in the original spellbook either - they show in the Skills tab (Stefan 09.10).
        if (bCompanion || bProficiency || ((Spell->Attributes&0x80u) && !bAlways)) continue;
        if (!bAlways && ((Spell->Attributes&0x20u) || !bOnSkillLine))
        {
            static TSet<uint32> LoggedLeftOut;
            if (!LoggedLeftOut.Contains(Id) && LoggedLeftOut.Num() < 400)
            {
                LoggedLeftOut.Add(Id);
                UE_LOG(LogWowLuaStub, Log, TEXT("Spellbook: left out %u %s (%s)"), Id, *Spell->SpellName,
                    (Spell->Attributes&0x20u) ? TEXT("recipe") : TEXT("no skill line of yours"));
            }
            continue;
        }
        Groups[TabIndex].Add(Id);
    }
    // r332 (Stefan 14.10: "a category tab too many - priests only have Holy, Shadow and Discipline"):
    // a class skill line with no spell of yours (Holy 594, the paladin tree that also lists the
    // priest in its class mask) made an empty tab. The client only shows lines you have spells on.
    for (int32 T=Book.Tabs.Num()-1; T>=1; --T)
    {
        if (Groups[T].IsEmpty()) { Book.Tabs.RemoveAt(T); Groups.RemoveAt(T); }
    }
    for (int32 T=0; T<Book.Tabs.Num(); ++T)
    {
        auto& Group=Groups[T];
        Group.Sort([&](uint32 A,uint32 B) {
            const auto* SA=Dbc.Spells().GetById(A); const auto* SB=Dbc.Spells().GetById(B);
            if (SA->SpellName!=SB->SpellName) return SA->SpellName<SB->SpellName;
            if (SA->SpellLevel!=SB->SpellLevel) return SA->SpellLevel<SB->SpellLevel;
            return A<B;
        });
        auto& Tab=Book.Tabs[T]; Tab.Offset=Book.Spells.Num(); Tab.Count=Group.Num(); Tab.HighestOffset=Book.HighestSlots.Num();
        for (uint32 Id:Group)
        {
            Book.Spells.Add(Id); bool bSuperseded=false;
            if (const auto* Rows=Skills.ForSpell(Id)) for (const auto& Row:*Rows)
                if (Row.NextSpell && Group.Contains(Row.NextSpell)) bSuperseded=true;
            const auto* S=Dbc.Spells().GetById(Id);
            if (!S->Rank.IsEmpty()) for (uint32 Other:Group)
            {
                const auto* O=Dbc.Spells().GetById(Other);
                if (Other!=Id && !O->Rank.IsEmpty() && S->SpellName==O->SpellName && S->SpellFamilyName==O->SpellFamilyName && S->SpellIconID==O->SpellIconID &&
                    (O->SpellLevel>S->SpellLevel || (O->SpellLevel==S->SpellLevel && Other>Id))) bSuperseded=true;
            }
            if (!bSuperseded) Book.HighestSlots.Add(Book.Spells.Num());
        }
        Tab.HighestCount=Book.HighestSlots.Num()-Tab.HighestOffset;
    }
    // Diagnostic (Stefan 10.10: spells should go into the tab of their tree): the tabs as built.
    {
        FString Summary;
        for (const auto& Tab:Book.Tabs) Summary+=FString::Printf(TEXT(" [%s skill=%u spells=%d]"), *Tab.Name, Tab.Skill, Tab.Count);
        UE_LOG(LogWowLuaStub, Log, TEXT("Spellbook tabs (class %u):%s"), Class, *Summary);
    }
    return Book;
}

static bool ResolveSpellBookSpellId(lua_State* L, int32 SpellIndexOrId, const char* BookType, uint32& OutSpellId)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager || strcmp(BookType, "spell") != 0)
    {
        return false;
    }

    const auto& Spells=GetClientSpellBook(L).Spells;
    if (!Spells.IsValidIndex(SpellIndexOrId-1)) return false;
    OutSpellId=Spells[SpellIndexOrId-1];
    return true;
}

bool WowLuaApi::ResolveBookSpellSlot(lua_State* L, int32 Slot, const char* BookType, uint32& OutSpellId)
{
    return ResolveSpellBookSpellId(L, Slot, BookType, OutSpellId);
}

static int L_PickupAction(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const int32 Slot = SafeActionSlot(L);
    if (Ctx && Ctx->ConnectionManager && Slot >= 0)
    {
        Ctx->ConnectionManager->PickupActionCursor(Slot);
    }
    return 0;
}

static int L_PlaceAction(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const int32 Slot = SafeActionSlot(L);
    if (Ctx && Ctx->ConnectionManager && Slot >= 0)
    {
        Ctx->ConnectionManager->PlaceCursorIntoActionSlot(Slot);
    }
    return 0;
}

static int L_PickupSpell(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const int32 SpellIndexOrId = static_cast<int32>(luaL_checknumber(L, 1));
    const char* BookType = luaL_optstring(L, 2, "spell");
    uint32 SpellId = 0;

    const bool bResolved = lua_isnoneornil(L, 2)
        ? (Ctx && Ctx->ConnectionManager && Ctx->ConnectionManager->PacketHandler.KnownSpells.Contains(
            SpellId = static_cast<uint32>(SpellIndexOrId)))
        : ResolveSpellBookSpellId(L, SpellIndexOrId, BookType, SpellId);
    if (Ctx && Ctx->ConnectionManager && bResolved)
    {
        Ctx->ConnectionManager->PickupSpellCursor(static_cast<int32>(SpellId), UTF8_TO_TCHAR(BookType));
    }

    return 0;
}

static int L_GetCursorInfo(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    FString Type;
    FString Detail;
    int32 Id = 0;

    if (Ctx && Ctx->ConnectionManager && Ctx->ConnectionManager->GetCursorInfo(Type, Id, Detail))
    {
        lua_pushstring(L, TCHAR_TO_UTF8(*Type));
        lua_pushnumber(L, Id);
        if (!Detail.IsEmpty())
        {
            lua_pushstring(L, TCHAR_TO_UTF8(*Detail));
            return 3;
        }
        return 2;
    }

    return 0;
}

static int L_ClearCursor(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        Ctx->ConnectionManager->ClearCursorPayload();
    }
    return 0;
}

static int L_CursorHasSpell(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const bool bHasSpell = Ctx && Ctx->ConnectionManager && Ctx->ConnectionManager->HasCursorSpellPayload();
    lua_pushboolean(L, bHasSpell);
    return 1;
}

static FString GetModifiedClickBindingName(const char* Action)
{
    if (strcmp(Action, "CHATLINK") == 0) return TEXT("SHIFT");
    if (strcmp(Action, "SELFCAST") == 0) return TEXT("ALT");
    if (strcmp(Action, "FOCUSCAST") == 0) return TEXT("CTRL");
    if (strcmp(Action, "AUTOLOOTTOGGLE") == 0)
    {
        // Options > Interface > Combat > Auto Loot Key.
        static const TCHAR* AutoLootKeys[] = { TEXT("SHIFT"), TEXT("CTRL"), TEXT("ALT"), TEXT("NONE") };
        return AutoLootKeys[FMath::Clamp(FWowClientPreferences::GetAutoLootKey(), 0, 3)];
    }
    // r424 (Stefan 19.10): hold Shift over an item to compare it with what is equipped, as in the
    // client - Ctrl is the dressing room (DRESSUP). It was Ctrl before.
    if (strcmp(Action, "COMPAREITEMS") == 0) return TEXT("SHIFT");
    if (strcmp(Action, "OPENALLBAGS") == 0) return TEXT("SHIFT");
    if (strcmp(Action, "QUESTWATCHTOGGLE") == 0) return TEXT("SHIFT");
    if (strcmp(Action, "DRESSUP") == 0) return TEXT("CTRL");
    if (strcmp(Action, "PICKUPACTION") == 0) return TEXT("SHIFT");
    return TEXT("NONE");
}

static bool IsModifierBindingPressed(const FString& BindingName)
{
    if (!FSlateApplication::IsInitialized())
    {
        return false;
    }

    const FModifierKeysState Modifiers = FSlateApplication::Get().GetModifierKeys();
    if (BindingName == TEXT("SHIFT")) return Modifiers.IsShiftDown();
    if (BindingName == TEXT("CTRL")) return Modifiers.IsControlDown();
    if (BindingName == TEXT("ALT")) return Modifiers.IsAltDown();
    return false;
}

static int L_GetModifiedClick(lua_State* L)
{
    const char* Action = luaL_optstring(L, 1, "");
    const FString BindingName = GetModifiedClickBindingName(Action);
    lua_pushstring(L, TCHAR_TO_UTF8(*BindingName));
    return 1;
}

static int L_IsModifiedClick(lua_State* L)
{
    if (lua_isnoneornil(L, 1))
    {
        bool bAnyModifier = false;
        if (FSlateApplication::IsInitialized())
        {
            const FModifierKeysState Modifiers = FSlateApplication::Get().GetModifierKeys();
            bAnyModifier = Modifiers.IsShiftDown() || Modifiers.IsControlDown() || Modifiers.IsAltDown();
        }
        lua_pushboolean(L, bAnyModifier);
        return 1;
    }

    const char* Action = luaL_optstring(L, 1, "");
    const FString BindingName = GetModifiedClickBindingName(Action);
    lua_pushboolean(L, IsModifierBindingPressed(BindingName));
    return 1;
}

static int L_HasAction(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 Slot = SafeActionSlot(L);

    if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144)
    {
        const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Slot < ActionButtons.Num() && ActionButtons[Slot] != 0)
        {
            lua_pushboolean(L, 1);
            return 1;
        }
    }

    lua_pushboolean(L, 0);
    return 1;
}

static int L_GetActionInfo(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 Slot = SafeActionSlot(L);

    if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144)
    {
        const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Slot < ActionButtons.Num() && ActionButtons[Slot] != 0)
        {
            uint32 ActionData = ActionButtons[Slot];
            uint8 ActionType = (ActionData >> 24) & 0xFF;
            uint32 ActionId = ActionData & 0x00FFFFFF;

            switch (ActionType)
            {
            case 0: // Spell
                lua_pushstring(L, "spell");
                lua_pushnumber(L, ActionId);
                return 2;
            case 64: // Macro
                lua_pushstring(L, "macro");
                lua_pushnumber(L, ActionId);
                return 2;
            case 128: // Item
                lua_pushstring(L, "item");
                lua_pushnumber(L, ActionId);
                return 2;
            default:
                lua_pushstring(L, "unknown");
                lua_pushnumber(L, ActionId);
                return 2;
            }
        }
    }

    lua_pushnil(L);
    return 1;
}

static int L_GetActionTexture(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (lua_isnil(L, 1) || lua_isnone(L, 1)) { lua_pushnil(L); return 1; }
    int32 Slot = SafeActionSlot(L);

    if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144)
    {
        const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Slot < ActionButtons.Num() && ActionButtons[Slot] != 0)
        {
            uint32 ActionData = ActionButtons[Slot];
            uint8 ActionType = (ActionData >> 24) & 0xFF;
            uint32 ActionId = ActionData & 0x00FFFFFF;

            if (ActionType == 0) // Spell
            {
                // Look up spell icon from SpellIcon.dbc
                const FSpellDbcEntry* SpellEntry = FDbcStore::Get().Spells().GetById(ActionId);
                if (SpellEntry)
                {
                    const FSpellIconDbcEntry* IconEntry = FDbcStore::Get().SpellIcons().GetById(SpellEntry->SpellIconID);
                    if (IconEntry && !IconEntry->TexturePath.IsEmpty())
                    {
                        UE_LOG(LogWowLuaStub, Verbose, TEXT("GetActionTexture: slot %d spell %d icon %d -> %s"),
                            Slot+1, ActionId, SpellEntry->SpellIconID, *IconEntry->TexturePath);
                        lua_pushstring(L, TCHAR_TO_UTF8(*IconEntry->TexturePath));
                        return 1;
                    }
                    UE_LOG(LogWowLuaStub, Warning, TEXT("GetActionTexture: spell %d iconID %d not found in SpellIcon.dbc"),
                        ActionId, SpellEntry->SpellIconID);
                }
                else
                {
                    UE_LOG(LogWowLuaStub, Warning, TEXT("GetActionTexture: spell %d not found in Spell.dbc"), ActionId);
                }
                lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark");
                return 1;
            }
            else if (ActionType == 128) // Item
            {
                lua_pushstring(L, TCHAR_TO_UTF8(*FWowItemPresentation::IconPath(ActionId)));
                return 1;
            }
            else if (ActionType == 64) // Macro: its icon (a "?" macro shows its /cast spell's icon)
            {
                if (const FWowMacro* Macro = FWowMacros::Get(int32(ActionId)))
                {
                    lua_pushstring(L, TCHAR_TO_UTF8(*FWowMacros::DisplayIcon(*Macro, Ctx->ConnectionManager)));
                    return 1;
                }
            }
        }
    }

    lua_pushnil(L);
    return 1;
}

static int L_UseAction(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 Slot = SafeActionSlot(L);

    if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144)
    {
        // A macro button runs its macro (RunMacro, LuaMacroApi.cpp).
        const TArray<uint32>& Buttons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Buttons.IsValidIndex(Slot) && (Buttons[Slot] >> 24) == 64)
        {
            lua_getglobal(L, "RunMacro");
            if (lua_isfunction(L, -1))
            {
                lua_pushinteger(L, lua_Integer(Buttons[Slot] & 0x00FFFFFFu));
                if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1);
            }
            else lua_pop(L, 1);
            return 0;
        }
        Ctx->ConnectionManager->UseActionSlot(Slot);
    }

    return 0;
}

static int L_GetActionCount(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 Slot = SafeActionSlot(L);

    if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144)
    {
        const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Slot < ActionButtons.Num() && ActionButtons[Slot] != 0)
        {
            uint32 ActionData = ActionButtons[Slot];
            uint8 ActionType = (ActionData >> 24) & 0xFF;
            uint32 ActionId = ActionData & 0x00FFFFFF;

            if (ActionType == 128) // Item: total carried in backpack and bags
            {
                lua_pushnumber(L, Ctx->ConnectionManager->CountPlayerItems(ActionId));
                return 1;
            }
        }
    }

    lua_pushnumber(L, 0);
    return 1;
}

static int PushSpellCooldown(lua_State* L, uint32 Spell)
{
    auto* Ctx=WowLuaApi::GetContext(L);
    FWowPacketHandler::FCooldownWindow Window;
    if (Ctx && Ctx->ConnectionManager) Window=Ctx->ConnectionManager->PacketHandler.GetVisibleSpellCooldown(Spell);
    lua_pushnumber(L,Window.Start); lua_pushnumber(L,Window.Duration); lua_pushnumber(L,1);
    return 3;
}
static int L_GetActionCooldown(lua_State* L)
{
    auto* Ctx=WowLuaApi::GetContext(L);
    const int32 Slot=SafeActionSlot(L);
    uint32 Spell=0;
    if (Ctx && Ctx->ConnectionManager)
    {
        const auto& Actions=Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Actions.IsValidIndex(Slot) && (Actions[Slot]>>24)==0) Spell=Actions[Slot]&0x00ffffff;
        // Item actions show the cooldown of the item's use spell (potions, hearthstone, trinkets).
        else if (Actions.IsValidIndex(Slot) && (Actions[Slot]>>24)==128) Spell=Ctx->ConnectionManager->GetItemUseSpell(Actions[Slot]&0x00ffffff);
    }
    return PushSpellCooldown(L,Spell);
}

static int L_PetHasActionBar(lua_State* L)
{
    const FWowPetActionBarState* PetActionBar = GetPetActionBarState(L);
    lua_pushboolean(L, PetActionBar && PetActionBar->HasActionBar());
    return 1;
}

static int L_GetPetActionsUsable(lua_State* L)
{
    const FWowPetActionBarState* PetActionBar = GetPetActionBarState(L);
    lua_pushboolean(L, PetActionBar && PetActionBar->HasActionBar());
    return 1;
}

static int L_GetPetActionInfo(lua_State* L)
{
    const FWowPetActionBarState* PetActionBar = GetPetActionBarState(L);
    const int32 SlotIndex = SafePetActionSlot(L);
    const FWowPetActionSlot* Slot = (PetActionBar && SlotIndex >= 0) ? PetActionBar->GetSlot(SlotIndex) : nullptr;
    if (!Slot || !Slot->IsUsable())
    {
        lua_pushnil(L);
        return 1;
    }

    const FString Name = Slot->GetDisplayName();
    const FString TexturePath = GetPetActionTexturePath(*Slot);
    lua_pushstring(L, TCHAR_TO_UTF8(*Name));
    lua_pushstring(L, "");
    lua_pushstring(L, TCHAR_TO_UTF8(*TexturePath));
    lua_pushboolean(L, 0); // isToken
    lua_pushboolean(L, IsPetActionActive(*PetActionBar, *Slot));
    lua_pushboolean(L, Slot->IsAutocastCapable());
    lua_pushboolean(L, Slot->IsAutocastEnabled());
    return 7;
}

static int L_GetPetActionTexture(lua_State* L)
{
    const FWowPetActionBarState* PetActionBar = GetPetActionBarState(L);
    const int32 SlotIndex = SafePetActionSlot(L);
    const FWowPetActionSlot* Slot = (PetActionBar && SlotIndex >= 0) ? PetActionBar->GetSlot(SlotIndex) : nullptr;
    if (!Slot || !Slot->IsUsable())
    {
        lua_pushnil(L);
        return 1;
    }

    const FString TexturePath = GetPetActionTexturePath(*Slot);
    lua_pushstring(L, TCHAR_TO_UTF8(*TexturePath));
    return 1;
}

static int L_GetPetActionCooldown(lua_State* L)
{
    const FWowPetActionBarState* PetActionBar = GetPetActionBarState(L);
    const int32 SlotIndex = SafePetActionSlot(L);
    const FWowPetActionSlot* Slot = (PetActionBar && SlotIndex >= 0) ? PetActionBar->GetSlot(SlotIndex) : nullptr;
    if (PetActionBar && Slot && Slot->IsUsable())
    {
        const float RemainingTime = PetActionBar->GetCooldownRemaining(SlotIndex);
        if (RemainingTime > 0.0f)
        {
            const double CurrentTime = FPlatformTime::Seconds();
            lua_pushnumber(L, CurrentTime - RemainingTime);
            lua_pushnumber(L, RemainingTime);
            lua_pushnumber(L, 1);
            return 3;
        }
    }

    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1);
    return 3;
}

static int L_GetPetActionSlotUsable(lua_State* L)
{
    const FWowPetActionBarState* PetActionBar = GetPetActionBarState(L);
    const int32 SlotIndex = SafePetActionSlot(L);
    const FWowPetActionSlot* Slot = (PetActionBar && SlotIndex >= 0) ? PetActionBar->GetSlot(SlotIndex) : nullptr;
    lua_pushboolean(L, Slot && Slot->IsUsable());
    return 1;
}

static int L_IsPetAttackAction(lua_State* L)
{
    const FWowPetActionBarState* PetActionBar = GetPetActionBarState(L);
    const int32 SlotIndex = SafePetActionSlot(L);
    const FWowPetActionSlot* Slot = (PetActionBar && SlotIndex >= 0) ? PetActionBar->GetSlot(SlotIndex) : nullptr;
    lua_pushboolean(L, Slot && Slot->IsCommand() && Slot->ActionId == WowPetCommand::ATTACK);
    return 1;
}

static int L_CastPetAction(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const int32 SlotIndex = SafePetActionSlot(L);
    if (Ctx && Ctx->ConnectionManager && SlotIndex >= 0)
    {
        Ctx->ConnectionManager->SendPetActionBarSlot(SlotIndex);
    }
    return 0;
}

static int L_TogglePetAutocast(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const FWowPetActionBarState* PetActionBar = GetPetActionBarState(L);
    const int32 SlotIndex = SafePetActionSlot(L);
    const FWowPetActionSlot* Slot = (PetActionBar && SlotIndex >= 0) ? PetActionBar->GetSlot(SlotIndex) : nullptr;
    if (Ctx && Ctx->ConnectionManager && Slot && Slot->IsAutocastCapable())
    {
        Ctx->ConnectionManager->SendPetSpellAutocast(SlotIndex, !Slot->IsAutocastEnabled());
    }
    return 0;
}

static int L_IsCurrentAction(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 Slot = SafeActionSlot(L);

    if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144)
    {
        const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Slot < ActionButtons.Num() && ActionButtons[Slot] != 0)
        {
            uint32 ActionData = ActionButtons[Slot];
            uint8 ActionType = (ActionData >> 24) & 0xFF;
            uint32 ActionId = ActionData & 0x00FFFFFF;

            // Check if this is the auto-attack spell
            if (ActionType == 0 && ActionId == 6603) // Auto Attack spell ID
            {
                // For now, return false for auto-attack detection
                // This would need access to the gameplay controller's IsAutoAttacking state
                lua_pushboolean(L, 0);
                return 1;
            }

            // For other spells, check if they're currently being channeled
            // This would require tracking channeling state in the packet handler
            lua_pushboolean(L, 0);
            return 1;
        }
    }

    lua_pushboolean(L, 0);
    return 1;
}

static int L_IsUsableAction(lua_State* L)
{
    auto* Ctx=WowLuaApi::GetContext(L); const int32 Slot=SafeActionSlot(L);
    FWowSpellUsability State;
    if(Ctx && Ctx->ConnectionManager)
    {
        const auto& Actions=Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if(Actions.IsValidIndex(Slot) && Actions[Slot])
        {
            if((Actions[Slot]>>24)==0) State=FWowSpellPresentation::Usability(Ctx->ConnectionManager,Actions[Slot]&0xffffff);
            else if((Actions[Slot]>>24)==128)
            {
                const uint32 Entry=Actions[Slot]&0xffffff;
                State.Usable=Ctx->ConnectionManager->CountPlayerItems(Entry)>0||Ctx->ConnectionManager->IsItemEntryEquipped(Entry);
            }
            else State.Usable=true;
        }
    }
    lua_pushboolean(L,State.Usable); lua_pushboolean(L,State.NoResource); return 2;
}

static int L_IsAttackAction(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 Slot = SafeActionSlot(L);

    if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144)
    {
        const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Slot < ActionButtons.Num() && ActionButtons[Slot] != 0)
        {
            uint32 ActionData = ActionButtons[Slot];
            uint8 ActionType = (ActionData >> 24) & 0xFF;
            uint32 ActionId = ActionData & 0x00FFFFFF;

            if (ActionType == 0 && ActionId == 6603) // Attack spell ID in WoW
            {
                lua_pushboolean(L, 1);
                return 1;
            }
        }
    }

    lua_pushboolean(L, 0);
    return 1;
}

static int L_GetBonusBarOffset(lua_State* L)
{
    lua_pushnumber(L, FWowKeyBindings::BonusBarOffset());
    return 1;
}

static int L_ChangeActionBarPage(lua_State* L)
{
    const int32 Page=static_cast<int32>(luaL_checkinteger(L,1));
    auto* Ctx=WowLuaApi::GetContext(L);
    if (Ctx && Ctx->FrameManager) Ctx->FrameManager->SetActionBarPage(Page);
    else FWowKeyBindings::SetActionPage(Page);
    return 0;
}

// Global variable for action bar page
static int L_GetCurrentActionBarPage(lua_State* L)
{
    lua_pushnumber(L, FWowKeyBindings::ManualActionPage());
    return 1;
}

// ─── Spell functions ────────────────────────────────────────────────────────────
// A book type explicitly selects a slot; otherwise numeric APIs take a spell ID.
static uint32 ResolveSpellArgument(lua_State* L)
{
    uint32 Id = static_cast<uint32>(luaL_checkinteger(L, 1));
    if (lua_type(L, 2) == LUA_TSTRING &&
        !ResolveSpellBookSpellId(L, static_cast<int32>(Id), lua_tostring(L, 2), Id)) return 0;
    return Id;
}

// IsHelpfulSpell / IsHarmfulSpell(id or spellbook index, "spell"|"pet"): from the
// implicit targets (6 = enemy, 21 = friendly, 25 = any unit).
static int SpellTargetKind(lua_State* L2, bool bHarmful)
{
    uint32 Id = 0;
    if (lua_isnumber(L2, 1)) Id = ResolveSpellArgument(L2);
    bool bEnemy = false, bAlly = false;
    if (const auto* Spell = Id ? FDbcStore::Get().Spells().GetById(Id) : nullptr)
        for (int32 I = 0; I < 3; ++I)
            for (uint32 Type : { Spell->EffectImplicitTargetA[I], Spell->EffectImplicitTargetB[I] })
            { bEnemy |= Type == 6; bAlly |= Type == 21 || Type == 25; }
    if (bHarmful ? bEnemy : bAlly) lua_pushboolean(L2, 1); else lua_pushnil(L2);
    return 1;
}

static int L_GetSpellName(lua_State* L)
{
    uint32 Id = 0;
    if (!ResolveSpellBookSpellId(L, static_cast<int32>(luaL_checkinteger(L, 1)),
        luaL_optstring(L, 2, "spell"), Id)) return 0;
    const auto* Spell = FDbcStore::Get().Spells().GetById(Id);
    if (!Spell) return 0;
    lua_pushstring(L, TCHAR_TO_UTF8(*Spell->SpellName));
    lua_pushstring(L, TCHAR_TO_UTF8(*Spell->Rank));
    return 2;
}

static int L_CastSpell(lua_State* L)
{
    auto* Ctx = WowLuaApi::GetContext(L);
    uint32 Id = 0;
    if (!Ctx || !Ctx->ConnectionManager ||
        !ResolveSpellBookSpellId(L, static_cast<int32>(luaL_checkinteger(L, 1)),
            luaL_optstring(L, 2, "spell"), Id)) return 0;
    const auto* Spell = FDbcStore::Get().Spells().GetById(Id);
    if (!Spell || (Spell->Attributes & 0x40u)) return 0;
    const int64 Target = lua_toboolean(L, 3)
        ? static_cast<int64>(Ctx->ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
        : Ctx->ConnectionManager->GetTargetGuid();
    Ctx->ConnectionManager->SendCastSpell(Id, Target);
    return 0;
}

static int L_GetSpellInfo(lua_State* L)
{
    // GetSpellInfo(spellId) → name, rank, icon, castTime, minRange, maxRange
    const uint32 SpellId = ResolveSpellArgument(L);
    const FSpellDbcEntry* SpellEntry = FDbcStore::Get().Spells().GetById(SpellId);
    if (SpellEntry)
    {
        lua_pushstring(L, TCHAR_TO_UTF8(*SpellEntry->SpellName));  // name
        lua_pushstring(L, TCHAR_TO_UTF8(*SpellEntry->Rank));        // rank

        // Look up icon from SpellIcon.dbc
        const FSpellIconDbcEntry* IconEntry = FDbcStore::Get().SpellIcons().GetById(SpellEntry->SpellIconID);
        if (IconEntry)
        {
            lua_pushstring(L, TCHAR_TO_UTF8(*IconEntry->TexturePath)); // icon
        }
        else
        {
            lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark"); // fallback icon
        }

        lua_pushnumber(L, 0);  // castTime (would need CastingTime DBC lookup)
        lua_pushnumber(L, 0);  // minRange (would need SpellRange DBC lookup)
        lua_pushnumber(L, 0);  // maxRange
        return 6;
    }
    lua_pushnil(L);
    return 1;
}

static int L_GetSpellCooldown(lua_State* L)
{
    return PushSpellCooldown(L,ResolveSpellArgument(L));
}

static int L_CastSpellByID(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager) return 0;

    int32 SpellId = static_cast<int32>(luaL_checknumber(L, 1));
    int64 TargetGuid = Ctx->ConnectionManager->GetTargetGuid();
    Ctx->ConnectionManager->SendCastSpell(SpellId, TargetGuid);
    return 0;
}

static int L_CastSpellByName(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager) return 0;

    const char* SpellName = luaL_checkstring(L, 1);
    FString NameStr = UTF8_TO_TCHAR(SpellName);

    // Strip rank suffix like "Fireball(Rank 1)" — WoW API convention
    int32 ParenIdx = INDEX_NONE;
    if (NameStr.FindChar(TEXT('('), ParenIdx))
    {
        NameStr.LeftInline(ParenIdx);
        NameStr.TrimEndInline();
    }

    // Look up spell ID from DBC by name, matching against known spells
    const FSpellDbc& Spells = FDbcStore::Get().Spells();
    const FWowPacketHandler& Handler = Ctx->ConnectionManager->PacketHandler;
    uint32 FoundSpellId = 0;

    for (const FSpellDbcEntry& Entry : Spells.GetAll())
    {
        if (Entry.SpellName.Equals(NameStr, ESearchCase::IgnoreCase) && Handler.KnownSpells.Contains(Entry.ID))
        {
            // Pick the highest-rank known spell with this name
            FoundSpellId = Entry.ID;
        }
    }

    if (FoundSpellId != 0)
    {
        int64 TargetGuid = Ctx->ConnectionManager->GetTargetGuid();
        Ctx->ConnectionManager->SendCastSpell(FoundSpellId, TargetGuid);
    }

    return 0;
}

static int L_GetSpellTexture(lua_State* L)
{
    // GetSpellTexture(spellId) → texture
    const uint32 SpellId = ResolveSpellArgument(L);
    const FSpellDbcEntry* SpellEntry = FDbcStore::Get().Spells().GetById(SpellId);
    if (SpellEntry)
    {
        const FSpellIconDbcEntry* IconEntry = FDbcStore::Get().SpellIcons().GetById(SpellEntry->SpellIconID);
        if (IconEntry)
        {
            lua_pushstring(L, TCHAR_TO_UTF8(*IconEntry->TexturePath));
            return 1;
        }
    }
    lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark");
    return 1;
}

static int L_GetNumSpellTabs(lua_State* L)
{
    lua_pushinteger(L, GetClientSpellBook(L).Tabs.Num()); return 1;
}
static int L_GetSpellTabInfo(lua_State* L)
{
    const int32 Index=static_cast<int32>(luaL_checkinteger(L,1))-1;
    const auto& Tabs=GetClientSpellBook(L).Tabs;
    if (!Tabs.IsValidIndex(Index))
    {
        // Unused tab: offset and count are still numbers (LibSpellRange compares them).
        lua_pushnil(L); lua_pushnil(L); lua_pushinteger(L,0); lua_pushinteger(L,0); return 4;
    }
    const auto& Tab=Tabs[Index];
    lua_pushstring(L,TCHAR_TO_UTF8(*Tab.Name)); lua_pushstring(L,TCHAR_TO_UTF8(*Tab.Texture));
    lua_pushinteger(L,Tab.Offset); lua_pushinteger(L,Tab.Count);
    lua_pushinteger(L,Tab.HighestOffset); lua_pushinteger(L,Tab.HighestCount); return 6;
}

static int L_IsUsableSpell(lua_State* L)
{
    auto* Ctx=WowLuaApi::GetContext(L); uint32 Id=0;
    if(lua_isnumber(L,1))
    {
        if(lua_isstring(L,2)) ResolveSpellBookSpellId(L,int32(lua_tointeger(L,1)),lua_tostring(L,2),Id);
        else Id=uint32(lua_tointeger(L,1));
    }
    else if(Ctx && Ctx->ConnectionManager && lua_isstring(L,1))
    {
        const FString Name=UTF8_TO_TCHAR(lua_tostring(L,1));
        for(uint32 Known:Ctx->ConnectionManager->PacketHandler.KnownSpells)
            if(const auto* Spell=FDbcStore::Get().Spells().GetById(Known); Spell && Spell->SpellName==Name) {Id=Known; break;}
    }
    const auto State=FWowSpellPresentation::Usability(Ctx?Ctx->ConnectionManager:nullptr,Id);
    lua_pushboolean(L,State.Usable); lua_pushboolean(L,State.NoResource); return 2;
}

static int L_IsSpellInRange(lua_State* L)
{
    // IsSpellInRange(spell, unit) → inRange
    lua_pushboolean(L, 1);
    return 1;
}

static int L_GetSpellBookItemInfo(lua_State* L)
{
    uint32 Id = 0;
    if (!ResolveSpellBookSpellId(L, static_cast<int32>(luaL_checkinteger(L, 1)),
        luaL_optstring(L, 2, "spell"), Id)) return 0;
    lua_pushstring(L, "SPELL"); lua_pushinteger(L, Id);
    return 2;
}

// ─── Items ──────────────────────────────────────────────────────────────────────
// r426 (TormentWoW ElvUI): GetItemInfo was a placeholder ("Unknown Item" for every id, numbers only),
// so addon bags errored on item links and showed no names, qualities or icons. It now reads the
// queried item templates like the native bags and tooltips, and takes ids, "item:N" strings, links
// and names the way the 3.3.5 client does; nil while the template is still unknown (it is requested).
static FString ResolveItemIconPath(uint32 ItemEntry);

static UWowConnectionManager* WowItemApiConnection(lua_State* L)
{
    if (L)
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->ConnectionManager)
            return Ctx->ConnectionManager;
    if (GEngine && GEngine->GameViewport)
        if (UGameInstance* GameInstance = GEngine->GameViewport->GetGameInstance())
            if (UWowUIManager* UI = GameInstance->GetSubsystem<UWowUIManager>())
                return UI->GetConnectionManager();
    return nullptr;
}

static const TCHAR* WowItemQualityHex(uint32 Quality)
{
    static const TCHAR* Hex[] = { TEXT("9d9d9d"), TEXT("ffffff"), TEXT("1eff00"), TEXT("0070dd"),
        TEXT("a335ee"), TEXT("ff8000"), TEXT("e6cc80"), TEXT("e6cc80") };
    return Quality < UE_ARRAY_COUNT(Hex) ? Hex[Quality] : Hex[1];
}

static FString BuildItemLink(uint32 ItemEntry)
{
    if (UWowConnectionManager* Connection = WowItemApiConnection(nullptr))
        if (const FWowItemTemplateInfo* Template = FWowItemPresentation::Info(Connection, ItemEntry); Template && !Template->Name.IsEmpty())
            return FString::Printf(TEXT("|cff%s|Hitem:%u:0:0:0:0:0:0:0:0:0|h[%s]|h|r"),
                WowItemQualityHex(Template->Quality), ItemEntry, *Template->Name);
    return FString::Printf(TEXT("|cffffffff|Hitem:%u:0:0:0:0:0:0:0:0:0|h[Item %u]|h|r"), ItemEntry, ItemEntry);
}

static const TCHAR* WowItemClassName(uint32 Class)
{
    static const TCHAR* Names[] = { TEXT("Consumable"), TEXT("Container"), TEXT("Weapon"), TEXT("Gem"), TEXT("Armor"),
        TEXT("Reagent"), TEXT("Projectile"), TEXT("Trade Goods"), TEXT("Generic"), TEXT("Recipe"), TEXT("Money"),
        TEXT("Quiver"), TEXT("Quest"), TEXT("Key"), TEXT("Permanent"), TEXT("Miscellaneous"), TEXT("Glyph") };
    return Class < UE_ARRAY_COUNT(Names) ? Names[Class] : TEXT("Miscellaneous");
}

static FString WowItemSubClassName(uint32 Class, uint32 SubClass)
{
    static const TCHAR* Consumable[] = { TEXT("Consumable"), TEXT("Potion"), TEXT("Elixir"), TEXT("Flask"), TEXT("Scroll"),
        TEXT("Food & Drink"), TEXT("Item Enhancement"), TEXT("Bandage"), TEXT("Other") };
    static const TCHAR* Container[] = { TEXT("Bag"), TEXT("Soul Bag"), TEXT("Herb Bag"), TEXT("Enchanting Bag"),
        TEXT("Engineering Bag"), TEXT("Gem Bag"), TEXT("Mining Bag"), TEXT("Leatherworking Bag"), TEXT("Inscription Bag") };
    static const TCHAR* Weapon[] = { TEXT("One-Handed Axes"), TEXT("Two-Handed Axes"), TEXT("Bows"), TEXT("Guns"),
        TEXT("One-Handed Maces"), TEXT("Two-Handed Maces"), TEXT("Polearms"), TEXT("One-Handed Swords"),
        TEXT("Two-Handed Swords"), TEXT("Obsolete"), TEXT("Staves"), TEXT("One-Handed Exotics"), TEXT("Two-Handed Exotics"),
        TEXT("Fist Weapons"), TEXT("Miscellaneous"), TEXT("Daggers"), TEXT("Thrown"), TEXT("Spears"), TEXT("Crossbows"),
        TEXT("Wands"), TEXT("Fishing Poles") };
    static const TCHAR* Gem[] = { TEXT("Red"), TEXT("Blue"), TEXT("Yellow"), TEXT("Purple"), TEXT("Green"), TEXT("Orange"),
        TEXT("Meta"), TEXT("Simple"), TEXT("Prismatic") };
    static const TCHAR* Armor[] = { TEXT("Miscellaneous"), TEXT("Cloth"), TEXT("Leather"), TEXT("Mail"), TEXT("Plate"),
        TEXT("Bucklers"), TEXT("Shields"), TEXT("Librams"), TEXT("Idols"), TEXT("Totems"), TEXT("Sigils") };
    static const TCHAR* Projectile[] = { TEXT("Wand"), TEXT("Bolt"), TEXT("Arrow"), TEXT("Bullet"), TEXT("Thrown") };
    static const TCHAR* TradeGoods[] = { TEXT("Trade Goods"), TEXT("Parts"), TEXT("Explosives"), TEXT("Devices"),
        TEXT("Jewelcrafting"), TEXT("Cloth"), TEXT("Leather"), TEXT("Metal & Stone"), TEXT("Meat"), TEXT("Herb"),
        TEXT("Elemental"), TEXT("Other"), TEXT("Enchanting"), TEXT("Materials"), TEXT("Armor Enchantment"),
        TEXT("Weapon Enchantment") };
    static const TCHAR* Recipe[] = { TEXT("Book"), TEXT("Leatherworking"), TEXT("Tailoring"), TEXT("Engineering"),
        TEXT("Blacksmithing"), TEXT("Cooking"), TEXT("Alchemy"), TEXT("First Aid"), TEXT("Enchanting"), TEXT("Fishing"),
        TEXT("Jewelcrafting"), TEXT("Inscription") };
    static const TCHAR* Quiver[] = { TEXT("Quiver"), TEXT("Quiver"), TEXT("Quiver"), TEXT("Ammo Pouch") };
    static const TCHAR* Key[] = { TEXT("Key"), TEXT("Lockpick") };
    static const TCHAR* Misc[] = { TEXT("Junk"), TEXT("Reagent"), TEXT("Pet"), TEXT("Holiday"), TEXT("Other"), TEXT("Mount") };
    auto Pick = [SubClass](const TCHAR* const* Table, uint32 Count, const TCHAR* Fallback) -> FString
    { return SubClass < Count ? FString(Table[SubClass]) : FString(Fallback); };
    switch (Class)
    {
    case 0: return Pick(Consumable, UE_ARRAY_COUNT(Consumable), TEXT("Consumable"));
    case 1: return Pick(Container, UE_ARRAY_COUNT(Container), TEXT("Bag"));
    case 2: return Pick(Weapon, UE_ARRAY_COUNT(Weapon), TEXT("Miscellaneous"));
    case 3: return Pick(Gem, UE_ARRAY_COUNT(Gem), TEXT("Simple"));
    case 4: return Pick(Armor, UE_ARRAY_COUNT(Armor), TEXT("Miscellaneous"));
    case 6: return Pick(Projectile, UE_ARRAY_COUNT(Projectile), TEXT("Arrow"));
    case 7: return Pick(TradeGoods, UE_ARRAY_COUNT(TradeGoods), TEXT("Trade Goods"));
    case 9: return Pick(Recipe, UE_ARRAY_COUNT(Recipe), TEXT("Book"));
    case 11: return Pick(Quiver, UE_ARRAY_COUNT(Quiver), TEXT("Quiver"));
    case 13: return Pick(Key, UE_ARRAY_COUNT(Key), TEXT("Key"));
    case 15: return Pick(Misc, UE_ARRAY_COUNT(Misc), TEXT("Other"));
    default: return WowItemClassName(Class);
    }
}

static const char* WowItemEquipLoc(uint32 InventoryType)
{
    static const char* Locs[] = { "", "INVTYPE_HEAD", "INVTYPE_NECK", "INVTYPE_SHOULDER", "INVTYPE_BODY", "INVTYPE_CHEST",
        "INVTYPE_WAIST", "INVTYPE_LEGS", "INVTYPE_FEET", "INVTYPE_WRIST", "INVTYPE_HAND", "INVTYPE_FINGER",
        "INVTYPE_TRINKET", "INVTYPE_WEAPON", "INVTYPE_SHIELD", "INVTYPE_RANGED", "INVTYPE_CLOAK", "INVTYPE_2HWEAPON",
        "INVTYPE_BAG", "INVTYPE_TABARD", "INVTYPE_ROBE", "INVTYPE_WEAPONMAINHAND", "INVTYPE_WEAPONOFFHAND",
        "INVTYPE_HOLDABLE", "INVTYPE_AMMO", "INVTYPE_THROWN", "INVTYPE_RANGEDRIGHT", "INVTYPE_QUIVER", "INVTYPE_RELIC" };
    return InventoryType < UE_ARRAY_COUNT(Locs) ? Locs[InventoryType] : "";
}

/** The item id GetItemInfo-style APIs accept: a number, "item:N", an item link or a cached item's name. */
static uint32 WowLuaItemArgToId(lua_State* L, int Index)
{
    if (lua_type(L, Index) == LUA_TNUMBER) return static_cast<uint32>(lua_tonumber(L, Index));
    const char* Raw = lua_tostring(L, Index);
    if (!Raw || !*Raw) return 0;
    const FString Text = UTF8_TO_TCHAR(Raw);
    int32 At = Text.Find(TEXT("item:"), ESearchCase::IgnoreCase);
    if (At != INDEX_NONE)
    {
        At += 5;
        uint32 Parsed = 0;
        while (At < Text.Len() && FChar::IsDigit(Text[At])) Parsed = Parsed * 10 + uint32(Text[At++] - TEXT('0'));
        return Parsed;
    }
    if (Text.IsNumeric()) return static_cast<uint32>(FCString::Atoi64(*Text));
    if (UWowConnectionManager* Connection = WowItemApiConnection(L))
        for (const TPair<uint32, FWowItemTemplateInfo>& Pair : Connection->PacketHandler.ItemTemplates)
            if (Pair.Value.Name.Equals(Text, ESearchCase::IgnoreCase)) return Pair.Key;
    return 0;
}

static int L_GetItemInfo(lua_State* L)
{
    // GetItemInfo(item) -> name, link, quality, iLevel, reqLevel, class, subclass, maxStack, equipSlot, texture, vendorPrice
    const uint32 ItemId = WowLuaItemArgToId(L, 1);
    UWowConnectionManager* Connection = WowItemApiConnection(L);
    const FWowItemTemplateInfo* Template = (ItemId && Connection) ? FWowItemPresentation::Info(Connection, ItemId) : nullptr;
    if (!Template || Template->Name.IsEmpty()) return 0;
    lua_pushstring(L, TCHAR_TO_UTF8(*Template->Name));
    lua_pushstring(L, TCHAR_TO_UTF8(*BuildItemLink(ItemId)));
    lua_pushinteger(L, lua_Integer(Template->Quality));
    lua_pushinteger(L, lua_Integer(Template->ItemLevel));
    lua_pushinteger(L, lua_Integer(Template->RequiredLevel));
    lua_pushstring(L, TCHAR_TO_UTF8(WowItemClassName(Template->Class)));
    lua_pushstring(L, TCHAR_TO_UTF8(*WowItemSubClassName(Template->Class, Template->SubClass)));
    lua_pushinteger(L, lua_Integer(FMath::Max<uint32>(1, Template->MaxStack)));
    lua_pushstring(L, WowItemEquipLoc(Template->InventoryType));
    lua_pushstring(L, TCHAR_TO_UTF8(*ResolveItemIconPath(ItemId)));
    lua_pushinteger(L, lua_Integer(Template->SellPrice));
    return 11;
}

static FString ResolveItemIconPath(uint32 ItemEntry)
{
    if (const FItemDbcEntry* ItemRecord = FDbcStore::Get().Items().GetById(ItemEntry))
    {
        if (const FItemDisplayInfoDbcEntry* DisplayRecord =
            FDbcStore::Get().ItemDisplayInfo().GetById(ItemRecord->DisplayInfoID))
        {
            FString IconName = DisplayRecord->Icon1;
            IconName.TrimStartAndEndInline();
            if (!IconName.IsEmpty())
            {
                if (!IconName.StartsWith(TEXT("Interface\\Icons\\"), ESearchCase::IgnoreCase))
                {
                    IconName = TEXT("Interface\\Icons\\") + IconName;
                }
                return IconName;
            }
        }
    }

    // Server-only items (custom servers, items newer than the client's Item.dbc): the queried
    // item template's display id has the icon (bags showed red question marks for them).
    if (GEngine && GEngine->GameViewport)
        if (UGameInstance* GameInstance = GEngine->GameViewport->GetGameInstance())
            if (UWowUIManager* UI = GameInstance->GetSubsystem<UWowUIManager>())
                if (UWowConnectionManager* Connection = UI->GetConnectionManager())
                    if (const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(Connection, ItemEntry); Info && Info->DisplayId)
                        return FWowItemPresentation::IconPath(ItemEntry, Info->DisplayId);

    return TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
}

static int32 NormalizeEquipmentInventorySlot(int32 SlotId)
{
    return (SlotId >= 1 && SlotId <= 19) ? (SlotId - 1) : INDEX_NONE;
}

// r282 (Stefan 13.10: the bank needs the WotLK window): bag ids 1-4 are the player's bags, 5-11 the
// bank bags (player inventory slots 67-73); -1 (BANK_CONTAINER) is the bank's 28 slots (39-66).
static uint64 LuaContainerBagGuid(const FWowPlayerEntity* Player, int32 BagId)
{
    if (!Player) return 0;
    if (BagId >= 1 && BagId <= 4) return Player->GetBagGuid(static_cast<uint8>(BagId));
    if (BagId >= 5 && BagId <= 11) return Player->GetField64(static_cast<uint16>(PlayerField::INV_SLOT_HEAD + (62 + BagId) * 2));
    return 0;
}

// Lua bag id -> the cursor code's container id (UWowConnectionManager::NetworkBag): 0 -> 255, -1 -> 254.
static uint8 LuaBagToCursorBag(int32 BagId)
{
    return static_cast<uint8>(BagId == 0 ? 255 : (BagId == -1 ? 254 : BagId));
}

static bool ResolveContainerItem(FWowLuaContext* Ctx, int32 BagId, int32 SlotId, uint64& OutItemGuid, FWowItemEntity*& OutItem)
{
    OutItemGuid = 0;
    OutItem = nullptr;

    if (!Ctx || !Ctx->EntityManager)
    {
        return false;
    }

    FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
    if (!Player || SlotId <= 0)
    {
        return false;
    }

    if (BagId == 0)
    {
        OutItemGuid = Player->GetBackpackItemGuid(static_cast<uint8>(SlotId - 1));
    }
    else if (BagId == -1)
    {
        if (SlotId <= 28) OutItemGuid = Player->GetField64(static_cast<uint16>(PlayerField::INV_SLOT_HEAD + (38 + SlotId) * 2));
    }
    else if (BagId >= 1 && BagId <= 11)
    {
        const uint64 BagGuid = LuaContainerBagGuid(Player, BagId);
        if (BagGuid != 0)
        {
            if (FWowContainerEntity* Container = Ctx->EntityManager->FindContainer(BagGuid))
            {
                OutItemGuid = Container->GetItemGuidAtSlot(SlotId - 1);
            }
        }
    }

    if (OutItemGuid == 0)
    {
        return false;
    }

    OutItem = Ctx->EntityManager->FindItem(OutItemGuid);
    return OutItem != nullptr;
}

static bool ResolveInventoryItem(FWowLuaContext* Ctx, int32 SlotId, uint64& OutItemGuid, FWowItemEntity*& OutItem)
{
    OutItemGuid = 0;
    OutItem = nullptr;

    if (!Ctx || !Ctx->EntityManager)
    {
        return false;
    }

    const int32 EquipmentSlot = NormalizeEquipmentInventorySlot(SlotId);
    // Inventory slots 20-23 are the four bag slots (CharacterBag0-3Slot): the INV_SLOT fields
    // right after the 19 equipment slots. Without them the bar showed empty bag outlines.
    // r282: and 24-74 the same way (backpack 24-39, bank 40-67, bank bags 68-74: BankFrame's buttons).
    const bool bBagSlot = SlotId >= 20 && SlotId <= 74;
    if (EquipmentSlot == INDEX_NONE && !bBagSlot)
    {
        return false;
    }

    FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
    if (!Player)
    {
        return false;
    }

    OutItemGuid = bBagSlot ? Player->GetField64(static_cast<uint16>(PlayerField::INV_SLOT_HEAD + (SlotId - 1) * 2))
        : Player->GetEquipmentItemGuid(static_cast<uint8>(EquipmentSlot));
    if (OutItemGuid == 0)
    {
        return false;
    }

    OutItem = Ctx->EntityManager->FindItem(OutItemGuid);
    return OutItem != nullptr;
}

static int L_GetContainerItemInfo(lua_State* L)
{
    // GetContainerItemInfo(bag, slot) → texture, count, locked, quality, readable, lootable, link
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 BagId = static_cast<int32>(luaL_checknumber(L, 1));
    int32 SlotId = static_cast<int32>(luaL_checknumber(L, 2));

    uint64 ItemGuid = 0;
    FWowItemEntity* Item = nullptr;
    if (ResolveContainerItem(Ctx, BagId, SlotId, ItemGuid, Item))
    {
        const FString IconPath = ResolveItemIconPath(Item->Entry);
        const FString ItemLink = BuildItemLink(Item->Entry);
        lua_pushstring(L, TCHAR_TO_UTF8(*IconPath)); // texture
        lua_pushnumber(L, Item->GetStackCount()); // count
        lua_pushboolean(L, 0); // locked
        lua_pushnumber(L, 1); // quality (would need item DBC lookup)
        lua_pushboolean(L, 0); // readable
        lua_pushboolean(L, 0); // lootable
        lua_pushstring(L, TCHAR_TO_UTF8(*ItemLink)); // link
        return 7;
    }

    lua_pushnil(L); // texture
    lua_pushnumber(L, 0); // count
    lua_pushboolean(L, 0); // locked
    lua_pushnumber(L, 1); // quality
    lua_pushboolean(L, 0); // readable
    lua_pushboolean(L, 0); // lootable
    lua_pushnil(L); // link
    return 7;
}

static int L_GetContainerItemQuestInfo(lua_State* L)
{
    // GetContainerItemQuestInfo(bag, slot) -> isQuestItem, questId, isActive.
    // Quest-item metadata is not cached yet, but FrameXML requires the complete
    // return shape while refreshing every visible container slot.
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    return 3;
}

static int L_GetContainerNumSlots(lua_State* L)
{
    int32 BagId = static_cast<int32>(luaL_checknumber(L, 1));

    if (BagId == 0) // Backpack
    {
        lua_pushnumber(L, 16); // Standard backpack size
        return 1;
    }
    if (BagId == -1) // r282: BANK_CONTAINER
    {
        lua_pushnumber(L, 28);
        return 1;
    }

    // For other bags, check if they exist and get their slot count
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player && BagId >= 1 && BagId <= 11)
        {
            uint64 BagGuid = LuaContainerBagGuid(Player, BagId); // r282: bank bags 5-11
            if (BagGuid != 0)
            {
                FWowContainerEntity* Container = Ctx->EntityManager->FindContainer(BagGuid);
                if (Container)
                {
                    lua_pushnumber(L, Container->GetNumSlots());
                    return 1;
                }
            }
        }
    }

    lua_pushnumber(L, 0);
    return 1;
}

static int L_GetBagName(lua_State* L)
{
    int32 BagId = static_cast<int32>(luaL_checknumber(L, 1));
    if (BagId == 0)
    {
        lua_pushstring(L, "Backpack");
        return 1;
    }

    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player && BagId >= 1 && BagId <= 11)
        {
            const uint64 BagGuid = LuaContainerBagGuid(Player, BagId); // r282: bank bags 5-11
            if (BagGuid != 0)
            {
                // The bag's own item name ("Netherweave Bag", "Small Brown Pouch", ...).
                const FWowEntity* Bag = Ctx->EntityManager->Find(BagGuid);
                if (Bag && Bag->Entry && Ctx->ConnectionManager)
                {
                    if (const FWowItemTemplateInfo* Info = Ctx->ConnectionManager->PacketHandler.ItemTemplates.Find(Bag->Entry);
                        Info && !Info->Name.IsEmpty())
                    {
                        lua_pushstring(L, TCHAR_TO_UTF8(*Info->Name));
                        return 1;
                    }
                    Ctx->ConnectionManager->PacketHandler.RequestItemTemplate(Bag->Entry);
                }
                lua_pushstring(L, "Bag");
                return 1;
            }
        }
    }

    lua_pushnil(L);
    return 1;
}

static int L_GetContainerItemLink(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 BagId = static_cast<int32>(luaL_checknumber(L, 1));
    int32 SlotId = static_cast<int32>(luaL_checknumber(L, 2));

    uint64 ItemGuid = 0;
    FWowItemEntity* Item = nullptr;
    if (ResolveContainerItem(Ctx, BagId, SlotId, ItemGuid, Item))
    {
        const FString ItemLink = BuildItemLink(Item->Entry);
        lua_pushstring(L, TCHAR_TO_UTF8(*ItemLink));
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

static int L_GetContainerFreeSlots(lua_State* L)
{
    // GetContainerFreeSlots(bag) → freeSlots, bagFamily
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
}

static int L_GetInventoryItemLink(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 ArgIndex = 1;
    if (lua_gettop(L) >= 2 && lua_isstring(L, 1))
    {
        const FString UnitId = UTF8_TO_TCHAR(lua_tostring(L, 1));
        if (UnitId != TEXT("player"))
        {
            lua_pushnil(L);
            return 1;
        }
        ArgIndex = 2;
    }

    int32 SlotId = static_cast<int32>(luaL_checknumber(L, ArgIndex));

    uint64 ItemGuid = 0;
    FWowItemEntity* Item = nullptr;
    if (ResolveInventoryItem(Ctx, SlotId, ItemGuid, Item))
    {
        const FString ItemLink = BuildItemLink(Item->Entry);
        lua_pushstring(L, TCHAR_TO_UTF8(*ItemLink));
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

// ItemTextFrame API (readable plaques, books and notes; FWowPacketHandler::ItemText).
static FWowPacketHandler* ItemTextHandler(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx && Ctx->ConnectionManager ? &Ctx->ConnectionManager->PacketHandler : nullptr;
}
static int L_ItemTextGetItem(lua_State* L)
{
    const FWowPacketHandler* H = ItemTextHandler(L);
    if (H && !H->ItemText.Title.IsEmpty()) lua_pushstring(L, TCHAR_TO_UTF8(*H->ItemText.Title)); else lua_pushnil(L);
    return 1;
}
static int L_ItemTextGetText(lua_State* L)
{
    const FWowPacketHandler* H = ItemTextHandler(L);
    const FWowPacketHandler::FWowPageText* Page = H ? H->PageTexts.Find(H->ItemText.PageId) : nullptr;
    lua_pushstring(L, Page ? TCHAR_TO_UTF8(*Page->Text) : "");
    return 1;
}
static int L_ItemTextGetCreator(lua_State* L) { lua_pushnil(L); return 1; }
static int L_ItemTextGetMaterial(lua_State* L)
{
    const FWowPacketHandler* H = ItemTextHandler(L);
    if (H && !H->ItemText.Material.IsEmpty()) lua_pushstring(L, TCHAR_TO_UTF8(*H->ItemText.Material)); else lua_pushnil(L);
    return 1;
}
static int L_ItemTextGetPage(lua_State* L)
{
    const FWowPacketHandler* H = ItemTextHandler(L);
    lua_pushnumber(L, H ? FMath::Max(1, H->ItemText.History.Num()) : 1);
    return 1;
}
static int L_ItemTextHasNextPage(lua_State* L)
{
    const FWowPacketHandler* H = ItemTextHandler(L);
    const FWowPacketHandler::FWowPageText* Page = H ? H->PageTexts.Find(H->ItemText.PageId) : nullptr;
    lua_pushboolean(L, Page && Page->NextPage != 0);
    return 1;
}
static int L_ItemTextNextPage(lua_State* L) { if (FWowPacketHandler* H = ItemTextHandler(L)) H->ItemTextTurnPage(1); return 0; }
static int L_ItemTextPrevPage(lua_State* L) { if (FWowPacketHandler* H = ItemTextHandler(L)) H->ItemTextTurnPage(-1); return 0; }
static int L_CloseItemText(lua_State* L) { if (FWowPacketHandler* H = ItemTextHandler(L)) H->CloseItemText(); return 0; }

static int L_GetInventoryItemTexture(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 ArgIndex = 1;
    if (lua_gettop(L) >= 2 && lua_isstring(L, 1))
    {
        const FString UnitId = UTF8_TO_TCHAR(lua_tostring(L, 1));
        if (UnitId != TEXT("player"))
        {
            lua_pushnil(L);
            return 1;
        }
        ArgIndex = 2;
    }

    int32 SlotId = static_cast<int32>(luaL_checknumber(L, ArgIndex));

    uint64 ItemGuid = 0;
    FWowItemEntity* Item = nullptr;
    if (ResolveInventoryItem(Ctx, SlotId, ItemGuid, Item))
    {
        // Real icon of the item in that slot (Item.dbc -> ItemDisplayInfo).
        if (Item && Item->Entry)
        {
            const FString Icon = ResolveItemIconPath(Item->Entry);
            if (!Icon.EndsWith(TEXT("INV_Misc_QuestionMark")))
            {
                lua_pushstring(L, TCHAR_TO_UTF8(*Icon));
                return 1;
            }
        }
        // Fallback: a generic texture for the WoW slot id (1-19).
        // For now, return a generic texture based on the WoW slot id (1-19).
        const char* SlotTextures[] = {
            "Interface\\Icons\\INV_Helmet_02", // Head
            "Interface\\Icons\\INV_Jewelry_Necklace_03", // Neck
            "Interface\\Icons\\INV_Shoulder_05", // Shoulder
            "Interface\\Icons\\INV_Shirt_02", // Shirt
            "Interface\\Icons\\INV_Chest_Leather_01", // Chest
            "Interface\\Icons\\INV_Belt_05", // Waist
            "Interface\\Icons\\INV_Pants_03", // Legs
            "Interface\\Icons\\INV_Boots_08", // Feet
            "Interface\\Icons\\INV_Bracer_03", // Wrist
            "Interface\\Icons\\INV_Gauntlets_05", // Hands
            "Interface\\Icons\\INV_Jewelry_Ring_03", // Finger1
            "Interface\\Icons\\INV_Jewelry_Ring_02", // Finger2
            "Interface\\Icons\\INV_Jewelry_Talisman_01", // Trinket1
            "Interface\\Icons\\INV_Jewelry_Talisman_02", // Trinket2
            "Interface\\Icons\\INV_Misc_Cape_01", // Back
            "Interface\\Icons\\INV_Weapon_Rifle_01", // Main Hand
            "Interface\\Icons\\INV_Shield_05", // Off Hand
            "Interface\\Icons\\INV_Weapon_Bow_01", // Ranged
            "Interface\\Icons\\INV_Misc_Tabard_01" // Tabard
        };

        const int32 EquipmentSlot = NormalizeEquipmentInventorySlot(SlotId);
        if (EquipmentSlot != INDEX_NONE && EquipmentSlot < UE_ARRAY_COUNT(SlotTextures))
        {
            lua_pushstring(L, SlotTextures[EquipmentSlot]);
            return 1;
        }
    }

    lua_pushnil(L);
    return 1;
}

static uint8 NormalizeDestinationBagId(int32 BagIdOrInventorySlot)
{
    // r282: the bank (-1), bank bags 5-11 and their inventory slots 68-74.
    if (BagIdOrInventorySlot == -1) return 254;
    if (BagIdOrInventorySlot >= 5 && BagIdOrInventorySlot <= 11) return static_cast<uint8>(BagIdOrInventorySlot);
    if (BagIdOrInventorySlot >= 68 && BagIdOrInventorySlot <= 74) return static_cast<uint8>(BagIdOrInventorySlot - 63);
    if (BagIdOrInventorySlot <= 0)
    {
        return 255;
    }

    if (BagIdOrInventorySlot >= 1 && BagIdOrInventorySlot <= 4)
    {
        return static_cast<uint8>(BagIdOrInventorySlot);
    }

    if (BagIdOrInventorySlot >= 20 && BagIdOrInventorySlot <= 23)
    {
        return static_cast<uint8>(BagIdOrInventorySlot - 19);
    }

    return 255;
}

static int L_PickupContainerItem(lua_State* L)
{
    const int32 Bag = static_cast<int32>(luaL_checknumber(L, 1));
    const int32 Slot = static_cast<int32>(luaL_checknumber(L, 2));
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager)
    {
        return 0;
    }

    if (Ctx->ConnectionManager->HasCursorItemPayload())
    {
        Ctx->ConnectionManager->PlaceCursorIntoContainerSlot(
            NormalizeDestinationBagId(Bag),
            static_cast<uint8>(FMath::Max(Slot - 1, 0)));
        return 0;
    }

    uint64 ItemGuid = 0;
    FWowItemEntity* Item = nullptr;
    if (ResolveContainerItem(Ctx, Bag, Slot, ItemGuid, Item))
    {
        Ctx->ConnectionManager->PickupBagItemCursor(
            LuaBagToCursorBag(Bag), // r282: -1 = bank
            static_cast<uint8>(Slot - 1),
            static_cast<int32>(Item->Entry),
            BuildItemLink(Item->Entry));
    }

    return 0;
}

static int L_UseContainerItem(lua_State* L)
{
    const int32 Bag = static_cast<int32>(luaL_checknumber(L, 1));
    const int32 Slot = static_cast<int32>(luaL_checknumber(L, 2));
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    uint64 ItemGuid=0; FWowItemEntity* Item=nullptr;
    if (Ctx && Ctx->ConnectionManager && ResolveContainerItem(Ctx,Bag,Slot,ItemGuid,Item))
    {
        if (Ctx->ConnectionManager->PacketHandler.ActiveVendorGuid != 0)
        {
            Ctx->ConnectionManager->SendSellItem(Ctx->ConnectionManager->PacketHandler.ActiveVendorGuid, ItemGuid, 0);
            return 0;
        }
        // Send Mail tab open: right-click attaches the item (SendMailFrame behaviour).
        if (Ctx->ConnectionManager->ContainerItemUseOverride && Ctx->ConnectionManager->ContainerItemUseOverride(ItemGuid))
            return 0;
        // Network positions use the player inventory slots, not Lua bag indices (r282: bank too).
        const uint8 NetBag=UWowConnectionManager::NetworkBag(LuaBagToCursorBag(Bag));
        const uint8 NetSlot=UWowConnectionManager::NetworkSlot(LuaBagToCursorBag(Bag),static_cast<uint8>(Slot-1));
        // r285: at the guild vault a right-click deposits into the shown tab (the server picks the slot).
        if (Ctx->ConnectionManager->ActiveGuildBankGuid && Ctx->ConnectionManager->GuildBankCurrentTab >= 1
            && Ctx->ConnectionManager->GuildBankCurrentTab <= 6)
        {
            Ctx->ConnectionManager->SendGuildBankSwapWithInventory(false,
                static_cast<uint8>(Ctx->ConnectionManager->GuildBankCurrentTab - 1), 0xFF, 0, NetBag, NetSlot, 0, false);
            return 0;
        }
        // r282: at the bank a right-click moves the item between the bank and the bags, like the client.
        if (Ctx->ConnectionManager->ActiveBankerGuid)
        {
            const bool bBankItem = Bag == -1 || (Bag >= 5 && Bag <= 11);
            if (bBankItem) Ctx->ConnectionManager->SendAutoStoreBankItem(NetBag, NetSlot);
            else Ctx->ConnectionManager->SendAutoBankItem(NetBag, NetSlot);
            return 0;
        }
        // Right-click: vendor open -> sell (above); character sheet open -> equip;
        // otherwise use the item (on-use spell / open container), equip as fallback.
        const int64 CharacterFrame=Ctx->FrameManager?Ctx->FrameManager->FindFrame(TEXT("CharacterFrame")):-1;
        const bool bCharacterSheetOpen=CharacterFrame>=0&&Ctx->FrameManager->IsFrameVisible(CharacterFrame);
        const bool bEquippable=Ctx->ConnectionManager->IsBagItemEquippable(NetBag,NetSlot);
        if(bCharacterSheetOpen&&bEquippable){Ctx->ConnectionManager->RequestEquipItem(NetBag,NetSlot);return 0;}
        if(Ctx->ConnectionManager->UseWeaponEnchantItem(NetBag,NetSlot))return 0;
        if(Ctx->ConnectionManager->UseBagItem(NetBag,NetSlot))return 0;
        if(bEquippable)Ctx->ConnectionManager->RequestEquipItem(NetBag,NetSlot);
    }
    return 0;
}

static int L_PickupInventoryItem(lua_State* L)
{
    const int32 InventorySlot = static_cast<int32>(luaL_checknumber(L, 1));
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager)
    {
        return 0;
    }

    if (Ctx->ConnectionManager->HasCursorItemPayload())
    {
        if (InventorySlot >= 1 && InventorySlot <= 19)
            Ctx->ConnectionManager->EquipCursorIntoSlot(static_cast<uint8>(InventorySlot - 1));
        return 0;
    }

    uint64 ItemGuid = 0;
    FWowItemEntity* Item = nullptr;
    if (ResolveInventoryItem(Ctx, InventorySlot, ItemGuid, Item))
    {
        if(Ctx->ConnectionManager->PendingTargetSpell){Ctx->ConnectionManager->CompleteSpellTarget(ItemGuid);return 0;}
        const int32 EquipmentSlot = NormalizeEquipmentInventorySlot(InventorySlot);
        if (EquipmentSlot != INDEX_NONE)
        {
            Ctx->ConnectionManager->PickupInventoryItemCursor(
                static_cast<uint8>(EquipmentSlot),
                static_cast<int32>(Item->Entry),
                BuildItemLink(Item->Entry));
        }
    }

    return 0;
}

static int L_UseInventoryItem(lua_State* L)
{
    return L_PickupInventoryItem(L);
}

static int L_AutoEquipCursorItem(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        Ctx->ConnectionManager->AutoEquipCursorItem();
    }
    return 0;
}

static int L_PutItemInBackpack(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        Ctx->ConnectionManager->PlaceCursorIntoFirstFreeContainerSlot(255);
    }
    return 0;
}

static int L_PutItemInBag(lua_State* L)
{
    const int32 BagInventorySlot = static_cast<int32>(luaL_checknumber(L, 1));
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const uint8 Destination = NormalizeDestinationBagId(BagInventorySlot);
        // r282: an empty bank bag slot (BankFrameBag1-7) takes the bag on the cursor itself.
        if (Destination >= 5 && Destination <= 11 && Ctx->ConnectionManager->HasCursorItemPayload()
            && Ctx->ConnectionManager->GetItemGuidAtPosition(255, static_cast<uint8>(62 + Destination)) == 0)
        {
            Ctx->ConnectionManager->PlaceCursorIntoContainerSlot(254, static_cast<uint8>(23 + Destination));
            return 0;
        }
        Ctx->ConnectionManager->PlaceCursorIntoFirstFreeContainerSlot(Destination);
    }
    return 0;
}

// r282: the stock BankFrame's API.
static int L_BankButtonIDToInvSlotID(lua_State* L)
{
    const int32 Id = static_cast<int32>(luaL_checknumber(L, 1));
    const bool bBag = lua_toboolean(L, 2) != 0;
    lua_pushinteger(L, bBag ? 63 + Id : 39 + Id); // bag buttons 5-11 -> 68-74, slots 1-28 -> 40-67
    return 1;
}

static int L_GetBankSlotCost(lua_State* L)
{
    // BankBagSlotPrices.dbc: ID (= slot number), price in copper.
    static TMap<int32, int32> Prices;
    static bool bLoaded = false;
    if (!bLoaded)
    {
        if (FMpqManager* Mpq = FWowAddonSettings::GetMpq())
        {
            bLoaded = true;
            TArray<uint8> Bytes;
            FDbcParser Parser;
            if (Mpq->ReadFile(TEXT("DBFilesClient\\BankBagSlotPrices.dbc"), Bytes) && Parser.Parse(Bytes) && Parser.GetFieldCount() >= 2)
                for (uint32 Row = 0; Row < Parser.GetRecordCount(); ++Row)
                    Prices.Add(static_cast<int32>(Parser.GetUInt(Row, 0)), static_cast<int32>(Parser.GetUInt(Row, 1)));
        }
    }
    const int32 Owned = static_cast<int32>(luaL_optnumber(L, 1, 0));
    const int32* Price = Prices.Find(Owned + 1);
    lua_pushnumber(L, Price ? *Price : 999999999);
    return 1;
}

static int L_PurchaseSlot(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendBuyBankSlot();
    return 0;
}

static int L_CloseBankFrame(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager || !Ctx->ConnectionManager->ActiveBankerGuid) return 0;
    Ctx->ConnectionManager->ActiveBankerGuid = 0;
    if (Ctx->EventSystem) Ctx->EventSystem->FireEvent(TEXT("BANKFRAME_CLOSED"));
    return 0;
}

static int L_SplitContainerItem(lua_State* L)
{
    const int32 Bag = static_cast<int32>(luaL_checknumber(L, 1));
    const int32 Slot = static_cast<int32>(luaL_checknumber(L, 2));
    const int32 Count = static_cast<int32>(luaL_checknumber(L, 3));
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager || Count <= 0)
    {
        return 0;
    }

    uint64 ItemGuid = 0;
    FWowItemEntity* Item = nullptr;
    if (ResolveContainerItem(Ctx, Bag, Slot, ItemGuid, Item) && Item && Item->GetStackCount() > Count)
    {
        Ctx->ConnectionManager->PickupBagItemCursor(
            LuaBagToCursorBag(Bag), // r282: -1 = bank
            static_cast<uint8>(Slot - 1),
            static_cast<int32>(Item->Entry),
            BuildItemLink(Item->Entry),
            static_cast<uint32>(Count));
    }

    return 0;
}

static int L_DeleteCursorItem(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        Ctx->ConnectionManager->DeleteCursorItem();
    }
    return 0;
}

// L_GetInventorySlotInfo defined before RegisterStubs

static int L_GetItemCount(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 ItemId = static_cast<int32>(luaL_checknumber(L, 1));

    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            int32 TotalCount = 0;

            // Check backpack
            for (uint8 i = 0; i < 16; ++i)
            {
                uint64 ItemGuid = Player->GetBackpackItemGuid(i);
                if (ItemGuid != 0)
                {
                    FWowItemEntity* Item = Ctx->EntityManager->FindItem(ItemGuid);
                    if (Item && Item->Entry == static_cast<uint32>(ItemId))
                    {
                        TotalCount += Item->GetStackCount();
                    }
                }
            }

            // Check additional bags
            for (uint8 BagIndex = 1; BagIndex <= 4; ++BagIndex)
            {
                uint64 BagGuid = Player->GetBagGuid(BagIndex);
                if (BagGuid != 0)
                {
                    FWowContainerEntity* Container = Ctx->EntityManager->FindContainer(BagGuid);
                    if (Container)
                    {
                        int32 NumSlots = Container->GetNumSlots();
                        for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
                        {
                            uint64 ItemGuid = Container->GetItemGuidAtSlot(SlotIndex);
                            if (ItemGuid != 0)
                            {
                                FWowItemEntity* Item = Ctx->EntityManager->FindItem(ItemGuid);
                                if (Item && Item->Entry == static_cast<uint32>(ItemId))
                                {
                                    TotalCount += Item->GetStackCount();
                                }
                            }
                        }
                    }
                }
            }

            lua_pushnumber(L, TotalCount);
            return 1;
        }
    }

    lua_pushnumber(L, 0);
    return 1;
}

static int L_GetItemIcon(lua_State* L)
{
    uint32 ItemEntry = 0;
    if (lua_isnumber(L, 1))
    {
        ItemEntry = static_cast<uint32>(lua_tonumber(L, 1));
    }
    else if (lua_isstring(L, 1))
    {
        FString ItemReference = UTF8_TO_TCHAR(lua_tostring(L, 1));
        int32 ItemMarker = ItemReference.Find(TEXT("item:"), ESearchCase::IgnoreCase);
        if (ItemMarker != INDEX_NONE)
        {
            ItemReference.RightChopInline(ItemMarker + 5);
        }
        ItemEntry = static_cast<uint32>(FCString::Atoi(*ItemReference));
    }

    const FString IconPath = ResolveItemIconPath(ItemEntry);
    lua_pushstring(L, TCHAR_TO_UTF8(*IconPath));
    return 1;
}

// ─── Target ─────────────────────────────────────────────────────────────────────
static int L_TargetUnit(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager && Ctx->EntityManager)
    {
        const uint64 TargetGuid = ResolveUnitGuid(L, 1);
        if (TargetGuid != 0)
        {
            Ctx->ConnectionManager->SendSetSelection(static_cast<int64>(TargetGuid));
			if (Ctx->FrameManager)
			{
				const int64 TargetFrameHandle = Ctx->FrameManager->FindFrame(TEXT("TargetFrame"));
				if (TargetFrameHandle >= 0)
				{
					Ctx->FrameManager->SetFrameVisible(TargetFrameHandle, true);
				}
			}

            if (Ctx->EventSystem)
            {
                Ctx->EventSystem->FireEvent(TEXT("PLAYER_TARGET_CHANGED"));
                Ctx->EventSystem->FireEvent(TEXT("UNIT_HEALTH"), {TEXT("target")});
                Ctx->EventSystem->FireEvent(TEXT("UNIT_POWER"), {TEXT("target")});
                Ctx->EventSystem->FireEvent(TEXT("UNIT_MANA"), {TEXT("target")});
            }
        }
    }

    return 0;
}

static int L_ClearTarget(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        Ctx->ConnectionManager->SendSetSelection(0);
		if (Ctx->FrameManager)
		{
			const int64 TargetFrameHandle = Ctx->FrameManager->FindFrame(TEXT("TargetFrame"));
			if (TargetFrameHandle >= 0)
			{
				Ctx->FrameManager->SetFrameVisible(TargetFrameHandle, false);
			}
		}
    }
    return 0;
}

static int L_FocusUnit(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx)
    {
        return 0;
    }

    const uint64 NewFocusGuid = ResolveUnitGuid(L, 1);
    if (Ctx->FocusGuid == NewFocusGuid)
    {
        return 0;
    }

    Ctx->FocusGuid = NewFocusGuid;
    if (Ctx->FrameManager)
    {
        const int64 FocusFrameHandle = Ctx->FrameManager->FindFrame(TEXT("FocusFrame"));
        if (FocusFrameHandle >= 0)
        {
            Ctx->FrameManager->SetFrameVisible(FocusFrameHandle, NewFocusGuid != 0);
        }
    }
    if (Ctx->EventSystem)
    {
        Ctx->EventSystem->FireEvent(TEXT("PLAYER_FOCUS_CHANGED"));
        if (NewFocusGuid != 0)
        {
			Ctx->EventSystem->FireEvent(TEXT("UNIT_PORTRAIT_UPDATE"), {TEXT("focus")});
            Ctx->EventSystem->FireEvent(TEXT("UNIT_HEALTH"), {TEXT("focus")});
			Ctx->EventSystem->FireEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("focus")});
            Ctx->EventSystem->FireEvent(TEXT("UNIT_POWER"), {TEXT("focus")});
			Ctx->EventSystem->FireEvent(TEXT("UNIT_MANA"), {TEXT("focus")});
			Ctx->EventSystem->FireEvent(TEXT("UNIT_MAXMANA"), {TEXT("focus")});
        }
    }
    return 0;
}

static int L_ClearFocusUnit(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || Ctx->FocusGuid == 0)
    {
        return 0;
    }

    Ctx->FocusGuid = 0;
    if (Ctx->FrameManager)
    {
        const int64 FocusFrameHandle = Ctx->FrameManager->FindFrame(TEXT("FocusFrame"));
        if (FocusFrameHandle >= 0)
        {
            Ctx->FrameManager->SetFrameVisible(FocusFrameHandle, false);
        }
    }
    if (Ctx->EventSystem)
    {
        Ctx->EventSystem->FireEvent(TEXT("PLAYER_FOCUS_CHANGED"));
    }
    return 0;
}

// ─── Combat ─────────────────────────────────────────────────────────────────────
static int L_AttackTarget(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        int64 TargetGuid = Ctx->ConnectionManager->GetTargetGuid();
        if (TargetGuid != 0)
        {
            Ctx->ConnectionManager->SendAttackSwing(TargetGuid);
        }
    }
    return 0;
}

static int L_StopAttack(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        Ctx->ConnectionManager->SendAttackStop();
    }
    return 0;
}

// ─── Social ─────────────────────────────────────────────────────────────────────
static int L_GetNumFriends(lua_State* L)
{
    // WoW 3.3.5: GetNumFriends() → numTotal, numOnline
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const TArray<FWowFriendInfo>& FriendsList = Ctx->ConnectionManager->PacketHandler.FriendsList;
        int32 Total = 0;
        int32 Online = 0;
        for (const FWowFriendInfo& F : FriendsList)
        {
            if (!(F.Flags & 0x1)) continue;
            ++Total;
            if (F.Status > 0) Online++;
        }
        lua_pushnumber(L, Total);
        lua_pushnumber(L, Online);
        return 2;
    }

    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
}

static int L_GetFriendInfo(lua_State* L)
{
    // GetFriendInfo(index) → name, level, class, area, online, status, note
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 Index = static_cast<int32>(luaL_checknumber(L, 1)) - 1; // Convert 1-based to 0-based

    if (Ctx && Ctx->ConnectionManager && Index >= 0)
    {
        const TArray<FWowFriendInfo>& FriendsList = Ctx->ConnectionManager->PacketHandler.FriendsList;
        const FWowFriendInfo* Selected = nullptr;
        for (const FWowFriendInfo& Contact : FriendsList)
        {
            if ((Contact.Flags & 0x1) && Index-- == 0) { Selected = &Contact; break; }
        }
        if (Selected)
        {
            const FWowFriendInfo& Friend = *Selected;

            lua_pushstring(L, TCHAR_TO_UTF8(*Friend.Name)); // name
            lua_pushnumber(L, Friend.Level); // level

            // Convert class ID to class name
            const char* ClassName = "Unknown";
            if (Friend.Class >= 1 && Friend.Class <= 11)
            {
                ClassName = WowClassNames[Friend.Class];
            }
            lua_pushstring(L, ClassName); // class

            // TODO: Look up area name from AreaTable DBC
            lua_pushstring(L, "Unknown Area"); // area

            lua_pushboolean(L, Friend.Status > 0 ? 1 : 0); // online

            // Convert status to string
            const char* StatusStr = (Friend.Status & 0x4) ? "Do Not Disturb"
                : (Friend.Status & 0x2) ? "Away" : Friend.Status ? "Online" : "Offline";
            lua_pushstring(L, StatusStr); // status

            lua_pushstring(L, TCHAR_TO_UTF8(*Friend.Note)); // note
            return 7;
        }
    }

    lua_pushstring(L, ""); // name
    lua_pushnumber(L, 0); // level
    lua_pushstring(L, ""); // class
    lua_pushstring(L, ""); // area
    lua_pushboolean(L, 0); // online
    lua_pushstring(L, ""); // status
    lua_pushstring(L, ""); // note
    return 7;
}

static int L_GetNumGuildMembers(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const TArray<FWowGuildMember>& GuildRoster = Ctx->ConnectionManager->PacketHandler.GuildRoster;
        lua_pushnumber(L, GuildRoster.Num());
        return 1;
    }

    lua_pushnumber(L, 0);
    return 1;
}

static int L_GetGuildRosterInfo(lua_State* L)
{
    // GetGuildRosterInfo(index) → name, rank, rankIndex, level, class, zone, note, officernote, online, status, classFileName
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 Index = static_cast<int32>(luaL_checknumber(L, 1)) - 1; // Convert 1-based to 0-based

    if (Ctx && Ctx->ConnectionManager && Index >= 0)
    {
        const TArray<FWowGuildMember>& GuildRoster = Ctx->ConnectionManager->PacketHandler.GuildRoster;
        if (Index < GuildRoster.Num())
        {
            const FWowGuildMember& Member = GuildRoster[Index];

            lua_pushstring(L, TCHAR_TO_UTF8(*Member.Name)); // name
            lua_pushstring(L, TCHAR_TO_UTF8(*Member.RankName)); // rank
            lua_pushnumber(L, Member.RankId); // rankIndex
            lua_pushnumber(L, Member.Level); // level

            // Convert class ID to class name
            const char* ClassName = "Unknown";
            const char* ClassFileName = "UNKNOWN";
            if (Member.Class >= 1 && Member.Class <= 11)
            {
                ClassName = WowClassNames[Member.Class];
                ClassFileName = WowClassTokens[Member.Class];
            }
            lua_pushstring(L, ClassName); // class

            // TODO: Look up zone name from AreaTable DBC
            lua_pushstring(L, "Unknown Zone"); // zone

            lua_pushstring(L, TCHAR_TO_UTF8(*Member.PublicNote)); // note
            lua_pushstring(L, TCHAR_TO_UTF8(*Member.OfficerNote)); // officernote
            lua_pushboolean(L, Member.Status > 0 ? 1 : 0); // online

            // Convert status to string
            const char* StatusStr = "Offline";
            switch (Member.Status)
            {
            case 1: StatusStr = "Online"; break;
            case 2: StatusStr = "Away"; break;
            case 3: StatusStr = "Do Not Disturb"; break;
            }
            lua_pushstring(L, StatusStr); // status

            lua_pushstring(L, ClassFileName); // classFileName
            return 11;
        }
    }

    lua_pushstring(L, ""); // name
    lua_pushstring(L, ""); // rank
    lua_pushnumber(L, 0); // rankIndex
    lua_pushnumber(L, 0); // level
    lua_pushstring(L, ""); // class
    lua_pushstring(L, ""); // zone
    lua_pushstring(L, ""); // note
    lua_pushstring(L, ""); // officernote
    lua_pushboolean(L, 0); // online
    lua_pushstring(L, ""); // status
    lua_pushstring(L, ""); // classFileName
    return 11;
}

static int L_GetGuildInfo(lua_State* L)
{
    // GetGuildInfo(unit) → guildName, guildRankName, guildRankIndex
    lua_pushnil(L); // guildName
    lua_pushnil(L); // guildRankName
    lua_pushnumber(L, 0); // guildRankIndex
    return 3;
}

// ─── Quest ──────────────────────────────────────────────────────────────────────
static int L_GetNumQuestLogEntries(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const TArray<FWowQuestLogEntry>& QuestLog = Ctx->ConnectionManager->PacketHandler.QuestLog;
        lua_pushnumber(L, QuestLog.Num()); // numEntries
        lua_pushnumber(L, 25); // numQuests (max quest log size)
        return 2;
    }

    lua_pushnumber(L, 0); // numEntries
    lua_pushnumber(L, 25); // numQuests (max quest log size)
    return 2;
}

static int L_GetQuestLogTitle(lua_State* L)
{
    // GetQuestLogTitle(questIndex) → questTitle, level, questTag, suggestedGroup, isHeader, isCollapsed, isComplete
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 QuestIndex = static_cast<int32>(luaL_checknumber(L, 1)) - 1; // Convert 1-based to 0-based

    if (Ctx && Ctx->ConnectionManager && QuestIndex >= 0)
    {
        const TArray<FWowQuestLogEntry>& QuestLog = Ctx->ConnectionManager->PacketHandler.QuestLog;
        if (QuestIndex < QuestLog.Num())
        {
            const FWowQuestLogEntry& Entry = QuestLog[QuestIndex];

            // TODO: Look up quest details from Quest.dbc
            FString QuestTitle = FString::Printf(TEXT("Quest %d"), Entry.QuestId);
            lua_pushstring(L, TCHAR_TO_UTF8(*QuestTitle)); // questTitle
            lua_pushnumber(L, 1); // level (TODO: get from DBC)
            lua_pushstring(L, ""); // questTag
            lua_pushnumber(L, 0); // suggestedGroup
            lua_pushboolean(L, 0); // isHeader
            lua_pushboolean(L, 0); // isCollapsed
            lua_pushboolean(L, Entry.State == 1 ? 1 : 0); // isComplete
            return 7;
        }
    }

    lua_pushstring(L, ""); // questTitle
    lua_pushnumber(L, 0); // level
    lua_pushstring(L, ""); // questTag
    lua_pushnumber(L, 0); // suggestedGroup
    lua_pushboolean(L, 0); // isHeader
    lua_pushboolean(L, 0); // isCollapsed
    lua_pushboolean(L, 0); // isComplete
    return 7;
}

static int L_GetQuestLogQuestText(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    // TODO: Get selected quest log entry and return quest text
    lua_pushstring(L, ""); // questText
    return 1;
}

static int L_SelectQuestLogEntry(lua_State* L)
{
    // TODO: Store selected quest log entry for GetQuestLogQuestText
    return 0;
}

static int L_IsQuestComplete(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 QuestId = static_cast<int32>(luaL_checknumber(L, 1));

    if (Ctx && Ctx->ConnectionManager)
    {
        const TArray<FWowQuestLogEntry>& QuestLog = Ctx->ConnectionManager->PacketHandler.QuestLog;
        for (const FWowQuestLogEntry& Entry : QuestLog)
        {
            if (Entry.QuestId == static_cast<uint32>(QuestId))
            {
                lua_pushboolean(L, Entry.State == 1 ? 1 : 0);
                return 1;
            }
        }
    }

    lua_pushboolean(L, 0);
    return 1;
}

// ─── Misc ─────────────────────────────────────────────────────────────────────
static int L_IsLoggedIn(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        lua_pushboolean(L, Ctx->ConnectionManager->GetState() == EWowSessionState::WorldInGame ? 1 : 0);
    }
    else
    {
        lua_pushboolean(L, 0);
    }
    // r360 (Stefan 15.10, offline world: "my bars are gone, no action bars shown"): the client is
    // logged in from the moment PLAYER_LOGIN fires (the UI manager sets __WowLoggedIn just before
    // it). The offline world has no world session, so IsLoggedIn() stayed false, AceAddon never
    // enabled its addons and Bartender4 hid the stock bars without creating its own.
    if (!lua_toboolean(L, -1))
    {
        lua_getglobal(L, "__WowLoggedIn");
        const bool bLoginFired = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (bLoginFired) { lua_pop(L, 1); lua_pushboolean(L, 1); }
    }
    return 1;
}

static int L_GetFramerate(lua_State* L)
{
    float DeltaTime = FApp::GetDeltaTime();
    lua_pushnumber(L, DeltaTime > 0.0f ? 1.0f / DeltaTime : 0.0f);
    return 1;
}

// ─── CVar system ────────────────────────────────────────────────────────────────
static TMap<FString, FString> CVarStorage;

static FString NormalizeCVarName(const FString& Name)
{
    return Name.ToLower();
}

static FString GetDefaultCVarValue(const FString& Name)
{
    static const TMap<FString, FString> ExplicitDefaults = {
        { TEXT("chatstyle"), TEXT("classic") },
        { TEXT("previewtalents"), TEXT("1") },
        { TEXT("conversationmode"), TEXT("inline") },
        { TEXT("showtimestamps"), TEXT("none") },
        { TEXT("locale"), WowLocale::Get() },
        { TEXT("targetoftargetmode"), TEXT("5") },
        { TEXT("showtargetoftarget"), TEXT("1") },
        { TEXT("playerstatustext"), TEXT("1") },
        { TEXT("displayworldpvpobjectives"), TEXT("2") },
        { TEXT("threatwarning"), TEXT("0") },
        { TEXT("combattextfloatmode"), TEXT("1") },
        { TEXT("camerasmoothstyle"), TEXT("0") },
        { TEXT("camerasmoothtrackingstyle"), TEXT("0") },
        { TEXT("enablevoicechat"), TEXT("0") },
        { TEXT("enablemicrophone"), TEXT("0") },
        { TEXT("voicechatmode"), TEXT("0") },
        { TEXT("pushtotalkbutton"), TEXT("") },
        { TEXT("pushtotalksound"), TEXT("1") },
        { TEXT("voiceactivationsensitivity"), TEXT("0.5") },
        { TEXT("sound_outputdriverindex"), TEXT("0") },
        { TEXT("sound_voicechatinputdriverindex"), TEXT("0") },
        { TEXT("sound_voicechatoutputdriverindex"), TEXT("0") },
        { TEXT("sound_numchannels"), TEXT("32") },
        { TEXT("sound_outputquality"), TEXT("1") },
        { TEXT("sound_mastervolume"), TEXT("1") },
        { TEXT("sound_musicvolume"), TEXT("1") },
        { TEXT("sound_ambiencevolume"), TEXT("1") },
        { TEXT("sound_sfxvolume"), TEXT("1") },
        { TEXT("chatsoundvolume"), TEXT("1") },
        { TEXT("chatmusicvolume"), TEXT("1") },
        { TEXT("chatambiencevolume"), TEXT("1") },
        { TEXT("inboundchatvolume"), TEXT("1") },
        { TEXT("outboundchatvolume"), TEXT("1") },
        { TEXT("sound_enableallsound"), TEXT("1") },
        { TEXT("sound_enablemusic"), TEXT("1") },
        { TEXT("sound_enableambience"), TEXT("1") },
        { TEXT("sound_enablesfx"), TEXT("1") },
        { TEXT("sound_enableerrorspeech"), TEXT("1") },
        { TEXT("sound_enableemotesounds"), TEXT("1") },
        { TEXT("sound_enablepetsounds"), TEXT("1") },
        { TEXT("sound_enablehardware"), TEXT("1") },
        { TEXT("sound_enablereverb"), TEXT("1") },
        { TEXT("sound_enabledspeffects"), TEXT("1") },
        { TEXT("sound_enablesoftwarehrtf"), TEXT("0") },
        { TEXT("sound_enablesoundwhengameisinbg"), TEXT("0") },
        { TEXT("sound_listeneratcharacter"), TEXT("0") },
        { TEXT("sound_zonemusicnodelay"), TEXT("0") },
		{ TEXT("ubertooltips"), TEXT("1") },
		// r339: the stock WatchFrame reads these on VARIABLES_LOADED (tonumber(nil) broke its filter):
		// narrow width, manual order, and show achievements, finished quests and other zones' quests.
		{ TEXT("watchframewidth"), TEXT("0") },
		{ TEXT("trackersorting"), TEXT("0") },
		{ TEXT("trackerfilter"), TEXT("7") },
    };

    const FString Normalized = NormalizeCVarName(Name);
    if (const FString* Explicit = ExplicitDefaults.Find(Normalized))
    {
        return *Explicit;
    }

    if (Normalized.Contains(TEXT("driverindex")))
    {
        return TEXT("0");
    }

    if (Normalized.Contains(TEXT("volume")))
    {
        return TEXT("1");
    }

    if (Normalized.Contains(TEXT("style")) || Normalized.Contains(TEXT("mode")))
    {
        return TEXT("0");
    }

    if (Normalized.StartsWith(TEXT("sound_enable")))
    {
        return TEXT("1");
    }

    if (Normalized.StartsWith(TEXT("show"))
        || Normalized.StartsWith(TEXT("display"))
        || Normalized.StartsWith(TEXT("auto"))
        || Normalized.StartsWith(TEXT("enable"))
        || Normalized.StartsWith(TEXT("nameplate"))
        || Normalized.StartsWith(TEXT("unitname"))
        || Normalized.StartsWith(TEXT("fct"))
        || Normalized.StartsWith(TEXT("camera"))
        || Normalized.StartsWith(TEXT("chat"))
        || Normalized.StartsWith(TEXT("threat"))
        || Normalized.StartsWith(TEXT("combat"))
        || Normalized.StartsWith(TEXT("mouse"))
        || Normalized.StartsWith(TEXT("movie"))
        || Normalized.StartsWith(TEXT("loot"))
        || Normalized.StartsWith(TEXT("guild"))
        || Normalized.StartsWith(TEXT("showtoast")))
    {
        return TEXT("0");
    }

    return TEXT("0");
}

static FIntPoint GetLiveViewportSize()
{
    FIntPoint Size(1920, 1080);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        const FIntPoint Live = GEngine->GameViewport->Viewport->GetSizeXY();
        if (Live.X > 0 && Live.Y > 0) Size = Live;
    }
    return Size;
}

static FString GetCurrentOrDefaultCVarValue(const FString& Name)
{
    if(FWowClientPreferences::HasWorldOption(Name)) return FWowClientPreferences::GetWorldOption(Name) ? TEXT("1") : TEXT("0");
    // Display cvars come from the live window (ElvUI/oUF pixel scaling read gxResolution).
    if (Name.Equals(TEXT("gxResolution"), ESearchCase::IgnoreCase))
    {
        const FIntPoint Size = GetLiveViewportSize();
        return FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);
    }
    if (Name.Equals(TEXT("gxWindow"), ESearchCase::IgnoreCase))
    {
        return (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid() &&
            GEngine->GameViewport->GetWindow()->GetWindowMode() == EWindowMode::Windowed) ? TEXT("1") : TEXT("0");
    }
    const FString Normalized = NormalizeCVarName(Name);
    if (const FString* Value = CVarStorage.Find(Normalized))
    {
        return *Value;
    }

    return GetDefaultCVarValue(Name);
}

static void SyncActionBarGlobals(lua_State* L)
{
    for (int32 I = 1; I <= 4; ++I)
    {
        const bool bEnabled = FWowClientPreferences::GetWorldOption(FString::Printf(TEXT("actionbar%d"), I + 2));
        if (bEnabled) lua_pushboolean(L, 1); else lua_pushnil(L);
        const FString Global = FString::Printf(TEXT("SHOW_MULTI_ACTIONBAR_%d"), I);
        lua_setglobal(L, TCHAR_TO_UTF8(*Global));
    }
    lua_pushstring(L, FWowClientPreferences::GetWorldOption(TEXT("alwaysshowactionbars")) ? "1" : "0");
    lua_setglobal(L, "ALWAYS_SHOW_MULTIBARS");
}

static bool ActionBarToggleEnabled(lua_State* L, int Index)
{
    if (lua_isnoneornil(L, Index)) return false;
    if (lua_isnumber(L, Index)) return lua_tonumber(L, Index) != 0;
    return lua_toboolean(L, Index) != 0;
}

// r404: Options > Interface > Tooltip Fade Time (seconds) for the GameTooltip fade (WowUIManager.cpp).
static int L_WowTooltipFadeTime(lua_State* L)
{
    lua_pushnumber(L, FWowClientPreferences::GetTooltipFadeTime());
    return 1;
}

static int L_SetCVar(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    // r398 (Stefan 16.10: the spellbook's "Show all ranks" tick would not stay): the client takes
    // nil/false as "0" and true as "1" - ShowAllSpellRanksCheckBox calls
    // SetCVar("ShowAllSpellRanks", self:GetChecked()), which is nil when unticked, and that was a
    // Lua error here.
    const char* value = lua_isboolean(L, 2) ? (lua_toboolean(L, 2) ? "1" : "0")
        : lua_isnoneornil(L, 2) ? "0" : luaL_checkstring(L, 2);
    const FString Key = NormalizeCVarName(UTF8_TO_TCHAR(name));
    const FString NewValue = UTF8_TO_TCHAR(value);
    CVarStorage.Add(Key, NewValue);
    if(FWowClientPreferences::HasWorldOption(Key))
        FWowClientPreferences::SetWorldOption(Key,NewValue==TEXT("1") || NewValue.ToBool());
    if (Key == TEXT("actionbar3") || Key == TEXT("actionbar4")
        || Key == TEXT("actionbar5") || Key == TEXT("actionbar6") || Key == TEXT("alwaysshowactionbars"))
        SyncActionBarGlobals(L);
    // These stock globals are also read by the ToT OnUpdate path. Keep them
    // synchronized when settings change, even before the options panel loads.
    if (Key == TEXT("showtargetoftarget") || Key == TEXT("targetoftargetmode"))
    {
        lua_pushvalue(L, 2);
        lua_setglobal(L, Key == TEXT("showtargetoftarget")
            ? "SHOW_TARGET_OF_TARGET" : "SHOW_TARGET_OF_TARGET_STATE");
    }
    if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem)
    {
        FString EventLabel = lua_isstring(L, 3) ? UTF8_TO_TCHAR(lua_tostring(L, 3)) : UTF8_TO_TCHAR(name);
        if (lua_isnoneornil(L, 3))
        {
            if (Key == TEXT("playerstatustext")) EventLabel = TEXT("STATUS_TEXT_PLAYER");
            else if (Key == TEXT("statustextpercentage")) EventLabel = TEXT("STATUS_TEXT_PERCENT");
        }
        Ctx->EventSystem->FireEvent(TEXT("CVAR_UPDATE"), {EventLabel, NewValue});
    }
    return 0;
}

static int L_GetCVar(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    const FString Value = GetCurrentOrDefaultCVarValue(UTF8_TO_TCHAR(name));
    FTCHARToUTF8 Conv(*Value);
    lua_pushstring(L, Conv.Get());
    return 1;
}

static int L_GetCVarBool(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    const FString Value = GetCurrentOrDefaultCVarValue(UTF8_TO_TCHAR(name));
    lua_pushboolean(L, Value.Equals(TEXT("1")) || Value.ToBool() ? 1 : 0);
    return 1;
}

static int L_GetCVarDefault(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    const FString Value = GetDefaultCVarValue(UTF8_TO_TCHAR(name));
    FTCHARToUTF8 Conv(*Value);
    lua_pushstring(L, Conv.Get());
    return 1;
}

static int L_RegisterForSaveVariables(lua_State* L)
{
    // For addon saved variables
    return 0;
}

// ─── Sound and misc functions ───────────────────────────────────────────────────
static int L_PlaySound(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    // Diagnostic for silent UI sounds (Stefan 10.10: the spellbook page arrows made no sound).
    UE_LOG(LogTemp, Log, TEXT("Lua PlaySound(%s) audio=%d"),
        lua_isstring(L, 1) ? UTF8_TO_TCHAR(lua_tostring(L, 1)) : TEXT("?"), Ctx && Ctx->AudioManager ? 1 : 0);
    if (!Ctx || !Ctx->AudioManager) return 0;
    if (lua_isnumber(L, 1))
    {
        Ctx->AudioManager->PlaySoundEffect(static_cast<uint32>(lua_tonumber(L, 1)));
    }
    else if (lua_isstring(L, 1))
    {
        Ctx->AudioManager->PlayUiSound(FName(UTF8_TO_TCHAR(lua_tostring(L, 1))));
    }
    return 0;
}

static int L_PlaySoundFile(lua_State* L)
{
    const char* SoundPath = luaL_checkstring(L, 1);

    if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->AudioManager)
    {
        Ctx->AudioManager->PlaySoundFile(UTF8_TO_TCHAR(SoundPath));
    }
    return 0;
}

static int L_StopMusic(lua_State* L)
{
    if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->AudioManager)
    {
        Ctx->AudioManager->StopMusic();
    }
    return 0;
}

static int L_SetPortraitTexture(lua_State* L)
{
    // SetPortraitTexture(texture, unit)
    luaL_checktype(L, 1, LUA_TTABLE); // texture widget
    const char* Unit = luaL_checkstring(L, 2);

    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->FrameManager) return 0;

    lua_getfield(L, 1, "__name");
    const FString RegionName = lua_isstring(L, -1) ? UTF8_TO_TCHAR(lua_tostring(L, -1)) : TEXT("");
    lua_pop(L, 1);
    UImage* Image = Ctx->FrameManager->GetNamedTexture(RegionName);
    FWowEntity* Entity = ResolveUnit(L, 2);
    if (!Entity || !Entity->IsUnit())
    {
        if (Image) Image->SetVisibility(ESlateVisibility::Hidden);
        return 0;
    }
    if (Image && Ctx->FrameManager->UnitPortraitProvider)
    {
        if (UMaterialInterface* Portrait = Ctx->FrameManager->UnitPortraitProvider(Entity->Guid))
        {
            FSlateBrush Brush = Image->GetBrush();
            Brush.SetResourceObject(Portrait);
            // The micro-menu portrait keeps the crop its button set with SetTexCoord (normal and
            // pushed state); resetting it to the full face made it look stretched after the
            // character sheet closed (Stefan 10.10).
            if (RegionName != TEXT("MicroButtonPortrait"))
                Brush.SetUVRegion(FBox2f(FVector2f::ZeroVector, FVector2f(1.0f, 1.0f)));
            Brush.DrawAs = ESlateBrushDrawType::Image;
            Image->SetBrush(Brush);
            Image->SetColorAndOpacity(FLinearColor::White);
            Image->SetRenderOpacity(1.0f);
            Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            return 0;
        }
    }

    // Players get the live 3D portrait as soon as their model exists. Until then show
    // nothing (the frame's dark backdrop) rather than the generic TEMPORARYPORTRAIT art,
    // which flashed e.g. a red-haired dwarf before the player's own face appeared.
    if (Entity->IsPlayer() && Ctx->FrameManager->UnitPortraitProvider)
    {
        if (Image) Image->SetRenderOpacity(0.0f);
        return 0;
    }

    FWowUnitEntity* UnitEntity = static_cast<FWowUnitEntity*>(Entity);

    // Get race and gender for portrait texture
    uint8 RaceId = UnitEntity->GetRaceId();
    uint8 Gender = UnitEntity->GetGenderId(); // 0 = male, 1 = female

    // Map race IDs to portrait names (WoW 3.3.5a races)
    const char* RaceNames[] = {
        "", "Human", "Orc", "Dwarf", "NightElf", "Undead", "Tauren", "Gnome", "Troll", "", "BloodElf", "Draenei"
    };

    const char* GenderNames[] = { "Male", "Female" };

    FString PortraitPath;
    if (Entity->IsPlayer() && Gender < UE_ARRAY_COUNT(GenderNames) && RaceId > 0 && RaceId < UE_ARRAY_COUNT(RaceNames) && RaceNames[RaceId] && *RaceNames[RaceId])
    {
        // Use temporary portraits: Interface\CharacterFrame\TEMPORARYPORTRAIT-Female-BloodElf.tga
        PortraitPath = FString::Printf(TEXT("Interface\\CharacterFrame\\TEMPORARYPORTRAIT-%s-%s"),
            UTF8_TO_TCHAR(GenderNames[Gender]), UTF8_TO_TCHAR(RaceNames[RaceId]));
    }
    else
    {
        // Creatures do not carry player race/gender bytes. Never represent an
        // arbitrary NPC as a human; use the data client's neutral unknown icon
        // until the model-rendered portrait path is available.
        PortraitPath = TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
        if (const auto* Display = FDbcStore::Get().CreatureDisplayInfo().GetById(Entity->GetDisplayId());
            Display && !Display->PortraitTextureName.IsEmpty()) PortraitPath = Display->PortraitTextureName;
    }

    // Get the texture name from the widget
    lua_getfield(L, 1, "__name");
    const char* TextureName = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
    lua_pop(L, 1);

    if (TextureName)
    {
        UImage* NamedImage = Ctx->FrameManager->GetNamedTexture(UTF8_TO_TCHAR(TextureName));
        UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(*PortraitPath);

        if (Texture && NamedImage)
        {
            FSlateBrush Brush = NamedImage->GetBrush();
            Brush.SetResourceObject(Texture);
            Brush.DrawAs = ESlateBrushDrawType::Image;
            Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
            Brush.Tiling = ESlateBrushTileType::NoTile;
            NamedImage->SetBrush(Brush);
			NamedImage->SetColorAndOpacity(FLinearColor::White);
			NamedImage->SetRenderOpacity(1.0f);
			NamedImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        }
    }

    // Store the texture path in the widget table
    lua_pushstring(L, TCHAR_TO_UTF8(*PortraitPath));
    lua_setfield(L, 1, "__texture");

    return 0;
}

static int L_SetPortraitToTexture(lua_State* L)
{
    // SetPortraitToTexture(texture, texturePath)
    luaL_checktype(L, 1, LUA_TTABLE); // texture widget
    const char* TexturePath = luaL_checkstring(L, 2);

    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->FrameManager) return 0;

    // Get the texture name from the widget
    lua_getfield(L, 1, "__name");
    const char* TextureName = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
    lua_pop(L, 1);

    if (TextureName)
    {
        UImage* NamedImage = Ctx->FrameManager->GetNamedTexture(UTF8_TO_TCHAR(TextureName));
        UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));

        if (Texture && NamedImage)
        {
            FSlateBrush Brush;
            Brush.SetResourceObject(Texture);
            Brush.DrawAs = ESlateBrushDrawType::Image;
            Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
            Brush.Tiling = ESlateBrushTileType::NoTile;
            NamedImage->SetBrush(Brush);
			NamedImage->SetColorAndOpacity(FLinearColor::White);
			NamedImage->SetRenderOpacity(1.0f);
			NamedImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        }
    }

    // Store the texture path in the widget table
    lua_pushstring(L, TexturePath);
    lua_setfield(L, 1, "__texture");

    return 0;
}

// Called by the stock container generator before it reveals the frame.
static int L_SetBagPortraitTexture(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    const int32 BagId = static_cast<int32>(luaL_checknumber(L,2));
    FString Path = BagId == 0 ? TEXT("Interface\\Buttons\\Button-Backpack-Up") : TEXT("Interface\\Icons\\INV_Misc_Bag_08");
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager && BagId >= 1 && BagId <= 4)
        if (const auto* Player = Ctx->EntityManager->GetLocalPlayer())
            if (const auto* Item = Ctx->EntityManager->FindItem(Player->GetBagGuid(static_cast<uint8>(BagId))))
            {
                const FString Icon = ResolveItemIconPath(Item->Entry);
                if (!Icon.IsEmpty()) Path = Icon;
            }
    lua_settop(L,1);
    lua_pushstring(L,TCHAR_TO_UTF8(*Path));
    return L_SetPortraitToTexture(L);
}

static int L_GetMoneyString(lua_State* L)
{
    int32 copper = static_cast<int32>(luaL_checknumber(L, 1));
    int32 gold = copper / 10000;
    int32 silver = (copper % 10000) / 100;
    copper = copper % 100;

    FString Result;
    if (gold > 0)
        Result += FString::Printf(TEXT("%dg"), gold);
    if (silver > 0)
        Result += FString::Printf(TEXT("%ds"), silver);
    if (copper > 0)
        Result += FString::Printf(TEXT("%dc"), copper);
    if (Result.IsEmpty())
        Result = TEXT("0c");

    FTCHARToUTF8 Conv(*Result);
    lua_pushstring(L, Conv.Get());
    return 1;
}

static int L_GetCoinTextureString(lua_State* L)
{
    int32 amount = static_cast<int32>(luaL_checknumber(L, 1));
    const char* coinType = luaL_optstring(L, 2, "gold");

    FString texture;
    if (strcmp(coinType, "gold") == 0)
        texture = TEXT("Interface\\MoneyFrame\\UI-GoldIcon");
    else if (strcmp(coinType, "silver") == 0)
        texture = TEXT("Interface\\MoneyFrame\\UI-SilverIcon");
    else
        texture = TEXT("Interface\\MoneyFrame\\UI-CopperIcon");

    FString Result = FString::Printf(TEXT("|T%s:0|t%d"), *texture, amount);
    FTCHARToUTF8 Conv(*Result);
    lua_pushstring(L, Conv.Get());
    return 1;
}

static int L_BreakUpLargeNumbers(lua_State* L)
{
    int32 number = static_cast<int32>(luaL_checknumber(L, 1));
    FString Result = FString::Printf(TEXT("%d"), number);

    // Add commas for thousands separators
    int32 Len = Result.Len();
    if (Len > 3)
    {
        FString Formatted;
        for (int32 i = 0; i < Len; i++)
        {
            if (i > 0 && (Len - i) % 3 == 0)
                Formatted += TEXT(",");
            Formatted += Result[i];
        }
        Result = Formatted;
    }

    FTCHARToUTF8 Conv(*Result);
    lua_pushstring(L, Conv.Get());
    return 1;
}
static TArray<FWowTocData> LoadedAddons;
static TArray<FString> LoadedAddonNamesByIndex;
static TSet<FString> RuntimeLoadedAddons;
static FMpqManager* AddonLoaderMpq = nullptr;
static FWowLuaVM* AddonLoaderLuaVM = nullptr;
static FWowFrameManager* AddonLoaderFrameManager = nullptr;
static FWowEventSystem* AddonLoaderEventSystem = nullptr;

// Helper function to update the loaded addons list
namespace WowLuaApi
{
    void SetAddonLoaderContext(FMpqManager* Mpq, FWowLuaVM* LuaVM, FWowFrameManager* FrameManager, FWowEventSystem* EventSystem)
    {
        AddonLoaderMpq = Mpq;
        AddonLoaderLuaVM = LuaVM;
        AddonLoaderFrameManager = FrameManager;
        AddonLoaderEventSystem = EventSystem;
        RuntimeLoadedAddons.Empty();
        UpdateLoadedAddons(Mpq);
    }

    void MarkAddonLoaded(const FString& AddonName)
    {
        RuntimeLoadedAddons.Add(AddonName.ToLower());
    }

    void UpdateLoadedAddons(FMpqManager* Mpq)
    {
        if (!Mpq) return;

        LoadedAddons.Empty();
        LoadedAddonNamesByIndex.Empty();
        TArray<FString> AddonNames = FWowAddonLoader::DiscoverAddons(Mpq);

        for (const FString& AddonName : AddonNames)
        {
            TArray<uint8> TocData;
            if (FWowAddonLoader::ReadTocCached(Mpq, AddonName, TocData)) // r364/r365: read once per session
            {
                FWowTocData TocInfo = FWowAddonLoader::ParseToc(TocData);
                if (TocInfo.Title.IsEmpty())
                    TocInfo.Title = AddonName;
                LoadedAddons.Add(TocInfo);
                LoadedAddonNamesByIndex.Add(AddonName);
            }
        }
    }
}

static int L_GetNumAddOns(lua_State* L)
{
    // For now, return the number of loaded addons
    // In a full implementation, this would scan all available addons
    lua_pushnumber(L, LoadedAddons.Num());
    return 1;
}

// Resolve an addon argument (index or name) to its folder name; empty if unknown.
static FString ResolveAddonArg(lua_State* L, int Arg)
{
    if (lua_type(L, Arg) == LUA_TNUMBER)
    {
        const int32 Index = static_cast<int32>(lua_tonumber(L, Arg)) - 1;
        return LoadedAddonNamesByIndex.IsValidIndex(Index) ? LoadedAddonNamesByIndex[Index] : FString();
    }
    if (const char* Name = lua_tostring(L, Arg))
    {
        const FString Wanted = UTF8_TO_TCHAR(Name);
        for (const FString& Known : LoadedAddonNamesByIndex)
            if (Known.Equals(Wanted, ESearchCase::IgnoreCase)) return Known;
    }
    return FString();
}

static int L_GetAddOnInfo(lua_State* L)
{
    // 3.3.5: name, title, notes, enabled, loadable, reason, security = GetAddOnInfo(index|name)
    const FString AddonName = ResolveAddonArg(L, 1);
    const int32 Index = LoadedAddonNamesByIndex.IndexOfByKey(AddonName);
    if (AddonName.IsEmpty() || !LoadedAddons.IsValidIndex(Index))
    {
        lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
        lua_pushnil(L); lua_pushstring(L, "MISSING"); lua_pushstring(L, "INSECURE");
        return 7;
    }
    const FWowTocData& Addon = LoadedAddons[Index];
    const bool bBlizzard = AddonName.StartsWith(TEXT("Blizzard_"), ESearchCase::IgnoreCase);
    const bool bEnabled = !Addon.bDisabled && (bBlizzard || FWowAddonSettings::IsEnabled(AddonName));
    bool bDepDisabled = false;
    for (const FString& Dep : Addon.RequiredDeps)
        if (!Dep.StartsWith(TEXT("Blizzard_"), ESearchCase::IgnoreCase) && !FWowAddonSettings::IsEnabled(Dep)) bDepDisabled = true;
    const bool bLoadable = bEnabled && !bDepDisabled;
    lua_pushstring(L, TCHAR_TO_UTF8(*AddonName));
    lua_pushstring(L, TCHAR_TO_UTF8(*Addon.Title));
    lua_pushstring(L, TCHAR_TO_UTF8(*Addon.Notes));
    if (bEnabled) lua_pushnumber(L, 1); else lua_pushnil(L);
    if (bLoadable) lua_pushnumber(L, 1); else lua_pushnil(L);
    if (bLoadable) lua_pushnil(L); else lua_pushstring(L, bEnabled ? "DEP_DISABLED" : "DISABLED");
    lua_pushstring(L, bBlizzard ? "SECURE" : "INSECURE");
    return 7;
}

static int L_EnableAddOn(lua_State* L)
{
    const FString Name = ResolveAddonArg(L, 1);
    if (!Name.IsEmpty()) { FWowAddonSettings::SetEnabled(Name, true); FWowAddonSettings::Save(); }
    return 0;
}

static int L_DisableAddOn(lua_State* L)
{
    const FString Name = ResolveAddonArg(L, 1);
    if (!Name.IsEmpty()) { FWowAddonSettings::SetEnabled(Name, false); FWowAddonSettings::Save(); }
    return 0;
}

static int L_SetAllAddOns(lua_State* L, bool bEnabled)
{
    for (const FString& Name : LoadedAddonNamesByIndex)
        if (!Name.StartsWith(TEXT("Blizzard_"), ESearchCase::IgnoreCase)) FWowAddonSettings::SetEnabled(Name, bEnabled);
    FWowAddonSettings::Save();
    return 0;
}

static int L_IsAddOnLoadOnDemand(lua_State* L)
{
    const int32 Index = LoadedAddonNamesByIndex.IndexOfByKey(ResolveAddonArg(L, 1));
    lua_pushboolean(L, LoadedAddons.IsValidIndex(Index) && LoadedAddons[Index].bLoadOnDemand);
    return 1;
}

static int L_GetAddOnDependencies(lua_State* L)
{
    const int32 Index = LoadedAddonNamesByIndex.IndexOfByKey(ResolveAddonArg(L, 1));
    if (!LoadedAddons.IsValidIndex(Index)) return 0;
    for (const FString& Dep : LoadedAddons[Index].RequiredDeps) lua_pushstring(L, TCHAR_TO_UTF8(*Dep));
    return LoadedAddons[Index].RequiredDeps.Num();
}


// ── Auction item classes (enUS 3.3.5). Item search libraries (LibItemSearch /
// Unfit-1.0 used by ElvUI bags) build their tables from these at load. ──
static const char* const GAuctionClasses[] = { "Weapon", "Armor", "Container", "Consumable", "Glyph",
    "Trade Goods", "Projectile", "Quiver", "Recipe", "Gem", "Miscellaneous", "Quest" };
static const TArray<TArray<const char*>>& GetAuctionSubClassTable()
{
    static const TArray<TArray<const char*>> Table = {
        { "One-Handed Axes", "Two-Handed Axes", "Bows", "Guns", "One-Handed Maces", "Two-Handed Maces", "Polearms",
          "One-Handed Swords", "Two-Handed Swords", "Staves", "Fist Weapons", "Miscellaneous", "Daggers", "Thrown",
          "Crossbows", "Wands", "Fishing Poles" },
        { "Miscellaneous", "Cloth", "Leather", "Mail", "Plate", "Shields", "Librams", "Idols", "Totems", "Sigils" },
        { "Bag", "Soul Bag", "Herb Bag", "Enchanting Bag", "Engineering Bag", "Gem Bag", "Mining Bag",
          "Leatherworking Bag", "Inscription Bag" },
        { "Food & Drink", "Potion", "Elixir", "Flask", "Bandage", "Item Enhancement", "Scroll", "Other" },
        { "Warrior", "Paladin", "Hunter", "Rogue", "Priest", "Death Knight", "Shaman", "Mage", "Warlock", "Druid" },
        { "Elemental", "Cloth", "Leather", "Metal & Stone", "Cooking", "Herb", "Enchanting", "Jewelcrafting", "Parts",
          "Devices", "Explosives", "Materials", "Other", "Armor Enchantment", "Weapon Enchantment" },
        { "Arrow", "Bullet" },
        { "Quiver", "Ammo Pouch" },
        { "Book", "Leatherworking", "Tailoring", "Engineering", "Blacksmithing", "Cooking", "Alchemy", "First Aid",
          "Enchanting", "Fishing", "Jewelcrafting", "Inscription" },
        { "Red", "Blue", "Yellow", "Purple", "Green", "Orange", "Meta", "Simple", "Prismatic" },
        { "Junk", "Reagent", "Pet", "Holiday", "Other", "Mount" },
        { } };
    return Table;
}

static int L_GetAuctionItemClasses(lua_State* L)
{
    for (const char* Name : GAuctionClasses) lua_pushstring(L, Name);
    return UE_ARRAY_COUNT(GAuctionClasses);
}

static int L_GetAuctionItemSubClasses(lua_State* L)
{
    const int32 Index = static_cast<int32>(luaL_optnumber(L, 1, 0)) - 1;
    const auto& Table = GetAuctionSubClassTable();
    if (!Table.IsValidIndex(Index)) return 0;
    for (const char* Name : Table[Index]) lua_pushstring(L, Name);
    return Table[Index].Num();
}

// GetRepairAllCost() -> cost, canRepair (no merchant repair window here).
static int L_GetRepairAllCost(lua_State* L)
{
    lua_pushnumber(L, 0);
    lua_pushboolean(L, 0);
    return 2;
}

// IsInventoryItemLocked(slot): an equipped item is locked only while it is on the cursor.
static int L_IsInventoryItemLocked(lua_State* L)
{
    lua_pushboolean(L, 0);
    return 1;
}

// GetAddOnMetadata(name|index, field) -> value or nil. Field is case-insensitive
// ("Version", "Title", "X-oUF", ...). ElvUI, oUF and most Ace addons call this at load.
static int L_GetAddOnMetadata(lua_State* L)
{
    FString AddonName;
    if (lua_type(L, 1) == LUA_TNUMBER)
    {
        const int32 Index = static_cast<int32>(lua_tonumber(L, 1)) - 1;
        if (LoadedAddonNamesByIndex.IsValidIndex(Index)) AddonName = LoadedAddonNamesByIndex[Index];
    }
    else if (const char* Name = lua_tostring(L, 1))
    {
        AddonName = UTF8_TO_TCHAR(Name);
    }
    const char* Field = lua_tostring(L, 2);
    if (AddonName.IsEmpty() || !Field || !AddonLoaderMpq)
    {
        lua_pushnil(L);
        return 1;
    }
    static TMap<FString, TMap<FString, FString>> MetadataCache;
    const FString CacheKey = AddonName.ToLower();
    TMap<FString, FString>* Meta = MetadataCache.Find(CacheKey);
    if (!Meta)
    {
        TArray<uint8> TocData;
        const FString TocPath = FString::Printf(TEXT("Interface\\AddOns\\%s\\%s.toc"), *AddonName, *AddonName);
        TMap<FString, FString> Parsed;
        if (AddonLoaderMpq->ReadFile(TocPath, TocData)) Parsed = FWowAddonLoader::ParseToc(TocData).Metadata;
        Meta = &MetadataCache.Add(CacheKey, MoveTemp(Parsed));
    }
    if (const FString* Value = Meta->Find(FString(UTF8_TO_TCHAR(Field)).ToLower()))
    {
        lua_pushstring(L, TCHAR_TO_UTF8(**Value));
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int L_IsAddOnLoaded(lua_State* L)
{
    const char* AddonName = luaL_checkstring(L, 1);
    FString AddonNameStr = UTF8_TO_TCHAR(AddonName);

    lua_pushboolean(L, RuntimeLoadedAddons.Contains(AddonNameStr.ToLower()) ? 1 : 0);
    return 1;
}
static int L_LoadAddOn(lua_State* L)
{
    FString AddonName;
    if (lua_isnumber(L, 1))
    {
        int32 Index = static_cast<int32>(lua_tonumber(L, 1)) - 1;
        if (Index >= 0 && Index < LoadedAddonNamesByIndex.Num())
        {
            AddonName = LoadedAddonNamesByIndex[Index];
        }
    }
    else
    {
        AddonName = UTF8_TO_TCHAR(luaL_checkstring(L, 1));
    }

    if (AddonName.IsEmpty())
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "NOT_FOUND");
        return 2;
    }

    if (RuntimeLoadedAddons.Contains(AddonName.ToLower()))
    {
        lua_pushboolean(L, 1);
        lua_pushnil(L);
        return 2;
    }

    if (!AddonLoaderMpq || !AddonLoaderLuaVM)
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "MISSING_CONTEXT");
        return 2;
    }

    const bool bLoaded = FWowAddonLoader::LoadAddon(
        AddonName,
        AddonLoaderMpq,
        AddonLoaderLuaVM,
        AddonLoaderFrameManager,
        AddonLoaderEventSystem);

    lua_pushboolean(L, bLoaded ? 1 : 0);
    if (bLoaded)
    {
        lua_pushnil(L);
    }
    else
    {
        lua_pushstring(L, "FAILED");
    }
    return 2;
}
static int L_GetNumBindings(lua_State* L)
{ lua_pushinteger(L,FWowKeyBindings::Actions().Num()); return 1; }
static int L_GetBinding(lua_State* L)
{
    const int32 Index=int32(luaL_checknumber(L,1))-1;
    const auto& Actions=FWowKeyBindings::Actions(); if(!Actions.IsValidIndex(Index)) return 0;
    lua_pushstring(L,TCHAR_TO_UTF8(*Actions[Index].Command));
    auto Keys=FWowKeyBindings::Keys(Actions[Index].Command);
    for(int32 I=0;I<2;++I) {if(Keys.IsValidIndex(I)) {if(Keys[I].IsEmpty())lua_pushnil(L);else lua_pushstring(L,TCHAR_TO_UTF8(*Keys[I]));} else lua_pushnil(L);}
    return 3;
}
static int L_GetBindingKey(lua_State* L)
{
    const auto Keys=FWowKeyBindings::Keys(UTF8_TO_TCHAR(luaL_checkstring(L,1)));
    for(const auto& Key:Keys){if(Key.IsEmpty())lua_pushnil(L);else lua_pushstring(L,TCHAR_TO_UTF8(*Key));} return Keys.Num();
}
static int L_GetBindingAction(lua_State* L)
{ const FString Value=FWowKeyBindings::Command(UTF8_TO_TCHAR(luaL_checkstring(L,1))); lua_pushstring(L,TCHAR_TO_UTF8(*Value));return 1; }
static int L_RunBinding(lua_State* L)
{
    const FString Command=UTF8_TO_TCHAR(luaL_checkstring(L,1));
    auto* Ctx=WowLuaApi::GetContext(L);
    const bool Down=lua_gettop(L)<2 || lua_toboolean(L,2);
    FWowKeyBindings::SetCommandHeld(Command,Down);
    lua_pushboolean(L,Ctx && Ctx->RunBinding && Ctx->RunBinding(Command,Down));return 1;
}
static int L_LoadBindings(lua_State* L)
{
    const int32 Set=int32(luaL_checkinteger(L,1));
    if(Set==0)FWowKeyBindings::ResetPreset();
    else {FWowKeyBindings::Reload();FWowKeyBindings::SetCharacterSpecific(Set==2);}
    if(auto* Ctx=WowLuaApi::GetContext(L);Ctx && Ctx->EventSystem)Ctx->EventSystem->FireEvent(TEXT("UPDATE_BINDINGS"));
    lua_pushboolean(L,1);return 1;
}
static int L_GetCurrentBindingSet(lua_State* L)
{lua_pushinteger(L,FWowKeyBindings::IsCharacterSpecific()?2:1);return 1;}
static int L_SetBinding(lua_State* L)
{ lua_pushboolean(L,FWowKeyBindings::Bind(UTF8_TO_TCHAR(luaL_checkstring(L,1)),UTF8_TO_TCHAR(luaL_optstring(L,2,"")))); return 1; }
static int L_SaveBindings(lua_State* L)
{
    // Saving does not fire UPDATE_BINDINGS: Bartender4 calls SaveBindings from its own
    // UPDATE_BINDINGS handler (ReassignBindings), which recursed into a C stack overflow.
    if(lua_isnumber(L,1))FWowKeyBindings::SetCharacterSpecific(lua_tointeger(L,1)==2); FWowKeyBindings::Save(); return 0;
}

// ─── Shapeshift Forms ──────────────────────────────────────────────────────────
static int L_GetNumShapeshiftForms(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            uint8 ClassId = Player->GetClassId();
            // Druid (class 11) has forms, others typically don't
            if (ClassId == 11) // Druid
            {
                lua_pushnumber(L, 4); // Bear, Aquatic, Cat, Travel forms
                return 1;
            }
        }
    }
    lua_pushnumber(L, 0);
    return 1;
}

static int L_GetShapeshiftFormInfo(lua_State* L)
{
    // GetShapeshiftFormInfo(index) → texture, name, isActive, isCastable
    int32 Index = static_cast<int32>(luaL_checknumber(L, 1));

    // Hardcoded druid forms for now
    const char* FormTextures[] = { "", "Interface\\Icons\\Ability_Racial_BearForm",
                                   "Interface\\Icons\\Ability_Druid_AquaticForm",
                                   "Interface\\Icons\\Ability_Druid_CatForm",
                                   "Interface\\Icons\\Ability_Druid_TravelForm" };
    const char* FormNames[] = { "", "Bear Form", "Aquatic Form", "Cat Form", "Travel Form" };

    if (Index >= 1 && Index <= 4)
    {
        lua_pushstring(L, FormTextures[Index]); // texture
        lua_pushstring(L, FormNames[Index]); // name
        lua_pushboolean(L, 0); // isActive (TODO: check current form)
        lua_pushboolean(L, 1); // isCastable
        return 4;
    }

    lua_pushnil(L); // texture
    lua_pushstring(L, ""); // name
    lua_pushboolean(L, 0); // isActive
    lua_pushboolean(L, 0); // isCastable
    return 4;
}

// ─── Map Functions ──────────────────────────────────────────────────────────────
static AWowWorldManager* CurrentZoneWorld(lua_State* L)
{
    auto* Ctx=WowLuaApi::GetContext(L);
    UWorld* World=Ctx && Ctx->AudioManager ? Ctx->AudioManager->GetWorld() :
        (GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr);
    return World?Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(World,AWowWorldManager::StaticClass())):nullptr;
}
static int L_GetRealZoneText(lua_State* L)
{
    auto* World=CurrentZoneWorld(L);
    lua_pushstring(L,TCHAR_TO_UTF8(*(World?World->GetCurrentZoneName():FString())));return 1;
}
static int L_GetSubZoneText(lua_State* L)
{
    auto* World=CurrentZoneWorld(L);
    const FString Name=World && World->GetCurrentAreaId()!=World->GetCurrentZoneId()?World->GetCurrentAreaName():FString();
    lua_pushstring(L,TCHAR_TO_UTF8(*Name));return 1;
}
static int L_GetMinimapZoneText(lua_State* L)
{
    auto* World=CurrentZoneWorld(L);
    FString Name=World?World->GetCurrentAreaName():FString();
    if(Name.IsEmpty() && World) Name=World->GetCurrentZoneName();
    lua_pushstring(L,TCHAR_TO_UTF8(*Name));return 1;
}

static int L_GetZonePVPInfo(lua_State* L)
{
    // GetZonePVPInfo() → pvpType, isFFA, faction
    lua_pushstring(L, "friendly"); // pvpType
    lua_pushboolean(L, 0); // isFFA
    lua_pushstring(L, "Alliance"); // faction
    return 3;
}

// ─── Talent Functions ───────────────────────────────────────────────────────────
static int L_GetNumTalentTabs(lua_State* L)
{
    // Most classes have 3 talent trees
    lua_pushnumber(L, 3);
    return 1;
}

static int L_GetTalentTabInfo(lua_State* L)
{
    // GetTalentTabInfo(tab) → name, icon, pointsSpent, fileName
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 TabIndex = static_cast<int32>(luaL_checknumber(L, 1));

    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            uint8 ClassId = Player->GetClassId();

            // Get talent tabs for this class from TalentTab DBC
            const FTalentTabDbc& TalentTabs = FDbcStore::Get().TalentTabs();
            int32 TabCount = 0;
            for (const FTalentTabDbcEntry& TabEntry : TalentTabs.GetAll())
            {
                uint32 ClassMask = 1u << (ClassId - 1);
                if ((TabEntry.ClassMask & ClassMask) != 0)
                {
                    if (TabCount == TabIndex - 1) // Convert 1-based to 0-based
                    {
                        lua_pushstring(L, TCHAR_TO_UTF8(*TabEntry.Name)); // name
                        FString IconPath = FString::Printf(TEXT("Interface\\Icons\\Spell_Icon_%d"), TabEntry.SpellIcon);
                        lua_pushstring(L, TCHAR_TO_UTF8(*IconPath)); // icon
                        lua_pushnumber(L, 0); // pointsSpent (TODO: calculate from talents)
                        lua_pushstring(L, TCHAR_TO_UTF8(*TabEntry.InternalName)); // fileName
                        return 4;
                    }
                    TabCount++;
                }
            }
        }
    }

    // Fallback
    const char* TabNames[] = { "Tree 1", "Tree 2", "Tree 3" };
    if (TabIndex >= 1 && TabIndex <= 3)
    {
        lua_pushstring(L, TabNames[TabIndex - 1]); // name
        lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark"); // icon
        lua_pushnumber(L, 0); // pointsSpent
        lua_pushstring(L, TabNames[TabIndex - 1]); // fileName
        return 4;
    }

    lua_pushstring(L, ""); // name
    lua_pushstring(L, ""); // icon
    lua_pushnumber(L, 0); // pointsSpent
    lua_pushstring(L, ""); // fileName
    return 4;
}

static int L_GetNumTalents(lua_State* L)
{
    // GetNumTalents(tab) → numTalents
    // Each talent tree typically has around 30-35 talents
    lua_pushnumber(L, 30);
    return 1;
}

static int L_GetTalentInfo(lua_State* L)
{
    // GetTalentInfo(tab, index) → name, icon, tier, column, rank, maxRank, isExceptional, meetsPrereq
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    int32 TabIndex = static_cast<int32>(luaL_checknumber(L, 1));
    int32 TalentIndex = static_cast<int32>(luaL_checknumber(L, 2));

    if (Ctx && Ctx->ConnectionManager)
    {
        // TODO: Look up talent from Talent DBC and player's talent data
        const TArray<FWowTalentInfo>& PlayerTalents = Ctx->ConnectionManager->PacketHandler.Talents;

        // For now, return placeholder data
        FString TalentName = FString::Printf(TEXT("Talent %d-%d"), TabIndex, TalentIndex);
        lua_pushstring(L, TCHAR_TO_UTF8(*TalentName)); // name
        lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark"); // icon
        lua_pushnumber(L, (TalentIndex - 1) / 4 + 1); // tier (row)
        lua_pushnumber(L, (TalentIndex - 1) % 4 + 1); // column
        lua_pushnumber(L, 0); // rank
        lua_pushnumber(L, 5); // maxRank
        lua_pushboolean(L, 0); // isExceptional
        lua_pushboolean(L, 1); // meetsPrereq
        return 8;
    }

    lua_pushstring(L, ""); // name
    lua_pushstring(L, ""); // icon
    lua_pushnumber(L, 1); // tier
    lua_pushnumber(L, 1); // column
    lua_pushnumber(L, 0); // rank
    lua_pushnumber(L, 1); // maxRank
    lua_pushboolean(L, 0); // isExceptional
    lua_pushboolean(L, 0); // meetsPrereq
    return 8;
}

// ─── Combat Log ─────────────────────────────────────────────────────────────────
namespace
{
    int32 GCombatLogNumEntries = 0;
    int32 GCombatLogCurrentEntry = 0;
    constexpr uint32 CombatLogObjectAffiliationMask = 0x0000000F;
    constexpr uint32 CombatLogObjectReactionMask = 0x000000F0;
    constexpr uint32 CombatLogObjectControlMask = 0x00000300;
    constexpr uint32 CombatLogObjectTypeMask = 0x0000FC00;
    constexpr uint32 CombatLogObjectNone = 0x80000000;
    constexpr uint32 CombatLogObjectSpecialMask = 0xFFFF0000;
    constexpr uint32 CombatLogObjectOptionalSpecialMask = CombatLogObjectSpecialMask & ~CombatLogObjectNone;

    int32 ClampCombatLogEntry(int32 Entry)
    {
        if (GCombatLogNumEntries <= 0)
        {
            return 0;
        }

        return FMath::Clamp(Entry, 1, GCombatLogNumEntries);
    }

    bool IsValidCombatLogFilterMask(uint32 Value)
    {
        if ((Value & CombatLogObjectNone) != 0)
        {
            return true;
        }

        return (Value & CombatLogObjectAffiliationMask) != 0
            && (Value & CombatLogObjectReactionMask) != 0
            && (Value & CombatLogObjectControlMask) != 0
            && (Value & CombatLogObjectTypeMask) != 0;
    }

    bool CombatLogObjectMatchesMask(uint32 ObjectFlags, uint32 Mask)
    {
        if (Mask == 0)
        {
            return false;
        }

        if ((ObjectFlags & CombatLogObjectNone) != 0)
        {
            return (Mask & CombatLogObjectNone) != 0;
        }

        if (Mask == CombatLogObjectNone)
        {
            return false;
        }

        if (!IsValidCombatLogFilterMask(ObjectFlags) || !IsValidCombatLogFilterMask(Mask))
        {
            return false;
        }

        if ((ObjectFlags & (Mask & CombatLogObjectAffiliationMask)) == 0
            || (ObjectFlags & (Mask & CombatLogObjectReactionMask)) == 0
            || (ObjectFlags & (Mask & CombatLogObjectControlMask)) == 0
            || (ObjectFlags & (Mask & CombatLogObjectTypeMask)) == 0)
        {
            return false;
        }

        const uint32 RequestedSpecialFlags = Mask & CombatLogObjectOptionalSpecialMask;
        if (RequestedSpecialFlags != 0 && RequestedSpecialFlags != CombatLogObjectOptionalSpecialMask)
        {
            return (ObjectFlags & RequestedSpecialFlags) != 0;
        }

        return true;
    }

    int PushPlaceholderCombatLogEntry(lua_State* L, const char* EventName, const char* SourceName, const char* DestName)
    {
        lua_pushnumber(L, FPlatformTime::Seconds()); // timestamp
        lua_pushstring(L, EventName); // event
        lua_pushstring(L, "0x0000000000000000"); // sourceGUID
        lua_pushstring(L, SourceName); // sourceName
        lua_pushnumber(L, CombatLogObjectNone); // sourceFlags
        lua_pushstring(L, "0x0000000000000000"); // destGUID
        lua_pushstring(L, DestName); // destName
        lua_pushnumber(L, CombatLogObjectNone); // destFlags
        return 8;
    }
}

static int L_CombatLogGetCurrentEventInfo(lua_State* L)
{
    // CombatLogGetCurrentEventInfo() → timestamp, subevent, hideCaster, sourceGUID, sourceName, sourceFlags, sourceRaidFlags, destGUID, destName, destFlags, destRaidFlags, ...
    // Return basic placeholder data using the WotLK combat-log field order.
    return PushPlaceholderCombatLogEntry(L, "SPELL_DAMAGE", "Unknown", "Unknown");
}

static int L_CombatLogGetNumEntries(lua_State* L)
{
    lua_pushinteger(L, GCombatLogNumEntries);
    return 1;
}

static int L_CombatLogGetCurrentEntry(lua_State* L)
{
    GCombatLogCurrentEntry = ClampCombatLogEntry(GCombatLogCurrentEntry);
    return PushPlaceholderCombatLogEntry(L, "", "", "");
}

static int L_CombatLogSetCurrentEntry(lua_State* L)
{
    const int32 RequestedEntry = static_cast<int32>(luaL_optinteger(L, 1, GCombatLogCurrentEntry));
    GCombatLogCurrentEntry = ClampCombatLogEntry(RequestedEntry);
    return 0;
}

static int L_CombatLogAdvanceEntry(lua_State* L)
{
    const int32 Delta = static_cast<int32>(luaL_optinteger(L, 1, 1));
    GCombatLogCurrentEntry = ClampCombatLogEntry(GCombatLogCurrentEntry + Delta);
    return PushPlaceholderCombatLogEntry(L, "", "", "");
}

static int L_CombatLog_Object_IsA(lua_State* L)
{
    const uint32 ObjectFlags = static_cast<uint32>(luaL_checkinteger(L, 1));
    const uint32 Mask = static_cast<uint32>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, CombatLogObjectMatchesMask(ObjectFlags, Mask));
    return 1;
}

// GetInventorySlotInfo - maps slot names to IDs and textures
static int L_GetInventorySlotInfo(lua_State* L)
{
    const char* slotName = luaL_optstring(L, 1, "");
    // Quick lookup using first char + length for common slots
    int slotId = 0;
    const char* tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Chest";

    if (strcmp(slotName, "HeadSlot") == 0) { slotId = 1; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Head"; }
    else if (strcmp(slotName, "NeckSlot") == 0) { slotId = 2; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Neck"; }
    else if (strcmp(slotName, "ShoulderSlot") == 0) { slotId = 3; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Shoulder"; }
    else if (strcmp(slotName, "ShirtSlot") == 0) { slotId = 4; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Shirt"; }
    else if (strcmp(slotName, "ChestSlot") == 0) { slotId = 5; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Chest"; }
    else if (strcmp(slotName, "WaistSlot") == 0) { slotId = 6; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Waist"; }
    else if (strcmp(slotName, "LegsSlot") == 0) { slotId = 7; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Legs"; }
    else if (strcmp(slotName, "FeetSlot") == 0) { slotId = 8; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Feet"; }
    else if (strcmp(slotName, "WristSlot") == 0) { slotId = 9; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Wrists"; }
    else if (strcmp(slotName, "HandsSlot") == 0) { slotId = 10; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Hands"; }
    else if (strcmp(slotName, "Finger0Slot") == 0) { slotId = 11; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Finger"; }
    else if (strcmp(slotName, "Finger1Slot") == 0) { slotId = 12; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Finger"; }
    else if (strcmp(slotName, "Trinket0Slot") == 0) { slotId = 13; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Trinket"; }
    else if (strcmp(slotName, "Trinket1Slot") == 0) { slotId = 14; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Trinket"; }
    else if (strcmp(slotName, "BackSlot") == 0) { slotId = 15; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Chest"; }
    else if (strcmp(slotName, "MainHandSlot") == 0) { slotId = 16; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-MainHand"; }
    else if (strcmp(slotName, "SecondaryHandSlot") == 0) { slotId = 17; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-SecondaryHand"; }
    else if (strcmp(slotName, "RangedSlot") == 0) { slotId = 18; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Ranged"; }
    else if (strcmp(slotName, "TabardSlot") == 0) { slotId = 19; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Tabard"; }
    else if (strcmp(slotName, "AmmoSlot") == 0) { slotId = 0; tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Ammo"; }
    else if (strncmp(slotName, "Bag", 3) == 0) { slotId = 20 + (slotName[3] - '0'); tex = "Interface\\PaperDoll\\UI-PaperDoll-Slot-Bag"; }

    lua_pushnumber(L, slotId);
    lua_pushstring(L, tex);
    return 2;
}

// Also fix GetClassInfo — can't use struct inside lambda due to macro expansion
static int L_GetClassInfo(lua_State* L)
{
    int classIndex = static_cast<int>(luaL_optnumber(L, 1, 1));
    static const char* classNames[] = {"Warrior","Paladin","Hunter","Rogue","Priest","Death Knight","Shaman","Mage","Warlock","Druid"};
    static const char* classFiles[] = {"WARRIOR","PALADIN","HUNTER","ROGUE","PRIEST","DEATHKNIGHT","SHAMAN","MAGE","WARLOCK","DRUID"};
    int idx = FMath::Clamp(classIndex - 1, 0, 9);
    lua_pushstring(L, classNames[idx]);
    lua_pushstring(L, classFiles[idx]);
    lua_pushnumber(L, classIndex);
    return 3;
}

static int L_UnitXP(lua_State* L)
{
    const char* UnitId = luaL_checkstring(L, 1);
    if (!UnitId || FString(UnitId) != TEXT("player"))
    {
        lua_pushnumber(L, 0);
        return 1;
    }

    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            lua_pushnumber(L, static_cast<lua_Number>(Player->GetXp()));
            return 1;
        }
    }

    lua_pushnumber(L, 0);
    return 1;
}

static int L_UnitXPMax(lua_State* L)
{
    const char* UnitId = luaL_checkstring(L, 1);
    if (!UnitId || FString(UnitId) != TEXT("player"))
    {
        lua_pushnumber(L, 1000);
        return 1;
    }

    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            lua_pushnumber(L, static_cast<lua_Number>(Player->GetNextLevelXp()));
            return 1;
        }
    }

    lua_pushnumber(L, 1000);
    return 1;
}

static int L_GetXPExhaustion(lua_State* L)
{
    // TODO: Implement rested XP when the update field is available
    // For now, return 0 (no rested XP)
    lua_pushnumber(L, 0);
    return 1;
}

static int L_BNGetNumFriends(lua_State* L)
{
    // BNGetNumFriends() → numBNetTotal, numBNetOnline, numWoWTotal, numWoWOnline
    // Return 4 values to ensure numWoWOnline is not nil
    lua_pushnumber(L, 0); // numBNetTotal
    lua_pushnumber(L, 0); // numBNetOnline
    lua_pushnumber(L, 0); // numWoWTotal
    lua_pushnumber(L, 0); // numWoWOnline
    return 4;
}

static int L_FillLocalizedClassList(lua_State* L)
{
    if (!lua_istable(L, 1)) return 0;
    const char* keys[] = {"WARRIOR","PALADIN","HUNTER","ROGUE","PRIEST","DEATHKNIGHT","SHAMAN","MAGE","WARLOCK","DRUID"};
    const char* vals[] = {"Warrior","Paladin","Hunter","Rogue","Priest","Death Knight","Shaman","Mage","Warlock","Druid"};
    for (int i = 0; i < 10; i++)
    {
        lua_pushstring(L, vals[i]);
        lua_setfield(L, 1, keys[i]);
    }
    return 0;
}

// ─── Combat Stats Functions ─────────────────────────────────────────────────────

static int L_GetCritChance(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            // Try to get from combat rating first
            int32 CritRating = Player->GetCombatRating(WowCombatRating::CRIT_MELEE);
            if (CritRating > 0)
            {
                // Convert rating to percentage (approximate conversion for level 80)
                float CritPercent = CritRating / 45.91f; // 45.91 rating = 1% crit at level 80
                lua_pushnumber(L, CritPercent);
            }
            else
            {
                // Fallback: calculate from agility (approximate)
                int32 Agility = Player->GetAgility();
                float CritPercent = FMath::Max(0.0f, Agility / 52.0f + 5.0f); // Base 5% + agility bonus
                lua_pushnumber(L, CritPercent);
            }
            return 1;
        }
    }
    lua_pushnumber(L, 5.0); // Default 5% crit
    return 1;
}

static int L_GetRangedCritChance(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            // Try to get from combat rating first
            int32 CritRating = Player->GetCombatRating(WowCombatRating::CRIT_RANGED);
            if (CritRating > 0)
            {
                float CritPercent = CritRating / 45.91f;
                lua_pushnumber(L, CritPercent);
            }
            else
            {
                // Fallback: calculate from agility
                int32 Agility = Player->GetAgility();
                float CritPercent = FMath::Max(0.0f, Agility / 52.0f + 5.0f);
                lua_pushnumber(L, CritPercent);
            }
            return 1;
        }
    }
    lua_pushnumber(L, 5.0); // Default 5% crit
    return 1;
}

static int L_GetSpellCritChance(lua_State* L)
{
    int32 School = static_cast<int32>(luaL_optnumber(L, 1, WowSpellSchool::PHYSICAL));

    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            // Try to get from combat rating first
            int32 CritRating = Player->GetCombatRating(WowCombatRating::CRIT_SPELL);
            if (CritRating > 0)
            {
                float CritPercent = CritRating / 45.91f;
                lua_pushnumber(L, CritPercent);
            }
            else
            {
                // Fallback: calculate from intellect
                int32 Intellect = Player->GetIntellect();
                float CritPercent = FMath::Max(0.0f, Intellect / 166.67f + 1.0f); // Base 1% + intellect bonus
                lua_pushnumber(L, CritPercent);
            }
            return 1;
        }
    }
    lua_pushnumber(L, 1.0); // Default 1% spell crit
    return 1;
}

static int L_GetDodgeChance(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            int32 DodgeRating = Player->GetCombatRating(WowCombatRating::DODGE);
            if (DodgeRating > 0)
            {
                float DodgePercent = DodgeRating / 45.25f; // 45.25 rating = 1% dodge at level 80
                lua_pushnumber(L, DodgePercent);
            }
            else
            {
                // Fallback: calculate from agility
                int32 Agility = Player->GetAgility();
                float DodgePercent = FMath::Max(0.0f, Agility / 52.0f + 5.0f); // Base 5% + agility bonus
                lua_pushnumber(L, DodgePercent);
            }
            return 1;
        }
    }
    lua_pushnumber(L, 5.0); // Default 5% dodge
    return 1;
}

static int L_GetParryChance(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            int32 ParryRating = Player->GetCombatRating(WowCombatRating::PARRY);
            if (ParryRating > 0)
            {
                float ParryPercent = ParryRating / 45.25f;
                lua_pushnumber(L, ParryPercent);
            }
            else
            {
                // Base parry chance varies by class, default to 5%
                lua_pushnumber(L, 5.0);
            }
            return 1;
        }
    }
    lua_pushnumber(L, 5.0); // Default 5% parry
    return 1;
}

static int L_GetBlockChance(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            // Check if player has a shield equipped in off-hand slot
            uint64 OffhandGuid = Player->GetEquipmentItemGuid(PlayerField::EQUIPMENT_SLOT_OFFHAND);
            bool HasShield = (OffhandGuid != 0); // If there's an item in off-hand, assume it could be a shield

            if (HasShield)
            {
                int32 BlockRating = Player->GetCombatRating(WowCombatRating::BLOCK);
                if (BlockRating > 0)
                {
                    float BlockPercent = BlockRating / 20.0f + 5.0f; // Base 5% + rating
                    lua_pushnumber(L, BlockPercent);
                }
                else
                {
                    lua_pushnumber(L, 5.0); // Base 5% for shield users
                }
            }
            else
            {
                lua_pushnumber(L, 0.0); // No shield = no block
            }
            return 1;
        }
    }
    lua_pushnumber(L, 0.0); // Default 0% block
    return 1;
}

static int L_GetSpellBonusDamage(lua_State* L)
{
    int32 School = static_cast<int32>(luaL_optnumber(L, 1, WowSpellSchool::PHYSICAL));

    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            // TODO: Get spell power from player fields when available
            // For now, calculate from intellect
            int32 Intellect = Player->GetIntellect();
            int32 SpellPower = Intellect; // Simplified: 1 int = 1 spell power
            lua_pushnumber(L, SpellPower);
            return 1;
        }
    }
    lua_pushnumber(L, 0); // Default 0 spell power
    return 1;
}

static int L_GetSpellBonusHealing(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            // TODO: Get healing power from player fields when available
            // For now, calculate from intellect and spirit
            int32 Intellect = Player->GetIntellect();
            int32 Spirit = Player->GetSpirit();
            int32 HealingPower = Intellect + Spirit / 2; // Simplified calculation
            lua_pushnumber(L, HealingPower);
            return 1;
        }
    }
    lua_pushnumber(L, 0); // Default 0 healing power
    return 1;
}

static int L_GetManaRegen(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            // Calculate mana regen from spirit (simplified)
            int32 Spirit = Player->GetSpirit();
            int32 Level = Player->GetLevel();

            // Base mana regen calculation (approximate)
            float BaseRegen = Spirit / 4.0f + Level / 2.0f;
            float CastingRegen = BaseRegen * 0.3f; // 30% while casting

            lua_pushnumber(L, BaseRegen);   // base regen
            lua_pushnumber(L, CastingRegen); // casting regen
            return 2;
        }
    }
    lua_pushnumber(L, 0); // base regen
    lua_pushnumber(L, 0); // casting regen
    return 2;
}

static int L_GetExpertise(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            int32 ExpertiseRating = Player->GetCombatRating(WowCombatRating::EXPERTISE);
            float Expertise = ExpertiseRating / 8.2f; // 8.2 rating = 1 expertise at level 80

            lua_pushnumber(L, Expertise); // main hand
            lua_pushnumber(L, Expertise); // off hand (same for now)
            return 2;
        }
    }
    lua_pushnumber(L, 0); // main hand
    lua_pushnumber(L, 0); // off hand
    return 2;
}

static int L_GetHaste(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->EntityManager)
    {
        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (Player)
        {
            int32 HasteRating = Player->GetCombatRating(WowCombatRating::HASTE);
            float HastePercent = HasteRating / 32.78f; // 32.78 rating = 1% haste at level 80
            lua_pushnumber(L, HastePercent);
            return 1;
        }
    }
    lua_pushnumber(L, 0.0); // Default 0% haste
    return 1;
}

static int L_ToggleSpellBook(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->FrameManager) return 0;

    // Find the SpellBookFrame
    int64 SpellBookHandle = Ctx->FrameManager->FindFrame(TEXT("SpellBookFrame"));
    if (SpellBookHandle < 0)
    {
        UE_LOG(LogWowLuaStub, Warning, TEXT("ToggleSpellBook: SpellBookFrame not found"));
        return 0;
    }

    // Toggle visibility: if shown, hide it; if hidden, show it
    bool bIsVisible = Ctx->FrameManager->IsFrameVisible(SpellBookHandle);
    Ctx->FrameManager->SetFrameVisible(SpellBookHandle, !bIsVisible);

    UE_LOG(LogWowLuaStub, Log, TEXT("ToggleSpellBook: %s SpellBookFrame"),
        bIsVisible ? TEXT("Hiding") : TEXT("Showing"));

    return 0;
}

// --- Wrath 3.3.5 LFG/LFR bridge -------------------------------------------------

static FWowLfgState* GetLfgState(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx && Ctx->ConnectionManager
        ? &Ctx->ConnectionManager->PacketHandler.LfgState
        : nullptr;
}

static const FLfgDungeonDbcEntry* FindLfgDungeon(int32 LuaId)
{
    if (LuaId <= 0)
    {
        return nullptr;
    }
    return FDbcStore::Get().LfgDungeons().GetById(static_cast<uint32>(LuaId) & 0x00FFFFFFu);
}

static int32 GetLfgMaxPlayers(const FLfgDungeonDbcEntry& Entry)
{
    if (!Entry.IsRaidBrowserEntry())
    {
        return 5;
    }
    return Entry.Difficulty == 3 || Entry.Difficulty == 5 ? 25 : 10;
}

static int32 GetLocalLfgLevel(const FWowLuaContext* Ctx)
{
    if (Ctx && Ctx->EntityManager)
    {
        if (const FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer())
        {
            return FMath::Max(1, Player->GetLevel());
        }
    }
    return 1;
}

static bool IsLfgEntryAvailable(const FWowLuaContext* Ctx, const FLfgDungeonDbcEntry& Entry)
{
    const int32 Level = GetLocalLfgLevel(Ctx);
    return (Entry.MinLevel == 0 || Level >= static_cast<int32>(Entry.MinLevel))
        && (Entry.MaxLevel == 0 || Level <= static_cast<int32>(Entry.MaxLevel));
}

static const char* GetLfgRoleToken(uint32 Roles)
{
    if ((Roles & WowLfgRole::Tank) != 0) return "TANK";
    if ((Roles & WowLfgRole::Healer) != 0) return "HEALER";
    return "DAMAGER";
}

static int32 CountLfgEncounterBits(uint32 Mask)
{
    int32 Count = 0;
    while (Mask != 0)
    {
        Count += static_cast<int32>(Mask & 1u);
        Mask >>= 1u;
    }
    return Count;
}

static int PushLfgDungeonInfoValues(lua_State* L, const FLfgDungeonDbcEntry& Entry, int32 GroupIdOverride = MAX_int32)
{
    const int32 GroupId = GroupIdOverride == MAX_int32
        ? (Entry.GroupID > 0 ? -static_cast<int32>(Entry.GroupID) : 0)
        : GroupIdOverride;
    lua_pushstring(L, TCHAR_TO_UTF8(*Entry.Name));
    lua_pushnumber(L, Entry.TypeID);
    lua_pushnumber(L, Entry.MinLevel);
    lua_pushnumber(L, Entry.MaxLevel);
    lua_pushnumber(L, Entry.TargetLevel);
    lua_pushnumber(L, Entry.TargetLevelMin);
    lua_pushnumber(L, Entry.TargetLevelMax);
    lua_pushnumber(L, Entry.ExpansionLevel);
    lua_pushnumber(L, GroupId);
    lua_pushstring(L, TCHAR_TO_UTF8(*Entry.TextureFilename));
    lua_pushnumber(L, Entry.Difficulty);
    lua_pushnumber(L, GetLfgMaxPlayers(Entry));
    return 12;
}

static void PushLfgDungeonInfoTable(lua_State* L, const FLfgDungeonDbcEntry& Entry, int32 GroupIdOverride = MAX_int32)
{
    const int32 GroupId = GroupIdOverride == MAX_int32
        ? (Entry.GroupID > 0 ? -static_cast<int32>(Entry.GroupID) : 0)
        : GroupIdOverride;
    lua_createtable(L, 12, 0);
    lua_pushstring(L, TCHAR_TO_UTF8(*Entry.Name)); lua_rawseti(L, -2, 1);
    lua_pushnumber(L, Entry.TypeID); lua_rawseti(L, -2, 2);
    lua_pushnumber(L, Entry.MinLevel); lua_rawseti(L, -2, 3);
    lua_pushnumber(L, Entry.MaxLevel); lua_rawseti(L, -2, 4);
    lua_pushnumber(L, Entry.TargetLevel); lua_rawseti(L, -2, 5);
    lua_pushnumber(L, Entry.TargetLevelMin); lua_rawseti(L, -2, 6);
    lua_pushnumber(L, Entry.TargetLevelMax); lua_rawseti(L, -2, 7);
    lua_pushnumber(L, Entry.ExpansionLevel); lua_rawseti(L, -2, 8);
    lua_pushnumber(L, GroupId); lua_rawseti(L, -2, 9);
    lua_pushstring(L, TCHAR_TO_UTF8(*Entry.TextureFilename)); lua_rawseti(L, -2, 10);
    lua_pushnumber(L, Entry.Difficulty); lua_rawseti(L, -2, 11);
    lua_pushnumber(L, GetLfgMaxPlayers(Entry)); lua_rawseti(L, -2, 12);
}

static int L_GetLFGMode(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    const char* Mode = nullptr;
    if (State)
    {
        switch (State->Mode)
        {
        case EWowLfgMode::RoleCheck: Mode = "rolecheck"; break;
        case EWowLfgMode::Queued: Mode = "queued"; break;
        case EWowLfgMode::Proposal: Mode = "proposal"; break;
        case EWowLfgMode::Dungeon: Mode = "lfgparty"; break;
        case EWowLfgMode::RaidListing: Mode = "listed"; break;
        default: break;
        }
    }
    if (Mode) lua_pushstring(L, Mode); else lua_pushnil(L);
    lua_pushnil(L); // Wrath submode; unused by the stock LFD/LFR frames.
    return 2;
}

static int L_GetLFGRoles(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    const uint8 Roles = State ? State->Roles : WowLfgRole::Damage;
    lua_pushboolean(L, (Roles & WowLfgRole::Leader) != 0);
    lua_pushboolean(L, (Roles & WowLfgRole::Tank) != 0);
    lua_pushboolean(L, (Roles & WowLfgRole::Healer) != 0);
    lua_pushboolean(L, (Roles & WowLfgRole::Damage) != 0);
    return 4;
}

static int L_SetLFGRoles(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager) return 0;
    uint8 Roles = 0;
    if (lua_toboolean(L, 1)) Roles |= WowLfgRole::Leader;
    if (lua_toboolean(L, 2)) Roles |= WowLfgRole::Tank;
    if (lua_toboolean(L, 3)) Roles |= WowLfgRole::Healer;
    if (lua_toboolean(L, 4)) Roles |= WowLfgRole::Damage;
    Ctx->ConnectionManager->PacketHandler.LfgState.Roles = Roles;
    Ctx->ConnectionManager->SendLfgSetRoles(Roles);
    return 0;
}

static int L_GetLFGQueueStats(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    const bool bHasData = State && State->bQueued && State->QueueStatus.DungeonEntry != 0;
    lua_pushboolean(L, bHasData);
    if (!bHasData)
    {
        return 1;
    }

    const FWowLfgQueueStatus& Queue = State->QueueStatus;
    const FLfgDungeonDbcEntry* Entry = FindLfgDungeon(static_cast<int32>(Queue.DungeonEntry & 0x00FFFFFFu));
    lua_pushnumber(L, 0); // The 3.3.5 status frame does not render a leader slot.
    lua_pushnumber(L, Queue.TanksNeeded);
    lua_pushnumber(L, Queue.HealersNeeded);
    lua_pushnumber(L, Queue.DamageNeeded);
    lua_pushnumber(L, Entry ? Entry->TypeID : ((Queue.DungeonEntry >> 24) & 0xFFu));
    lua_pushstring(L, Entry ? TCHAR_TO_UTF8(*Entry->Name) : "");
    lua_pushnumber(L, Queue.AverageWaitSeconds);
    lua_pushnumber(L, Queue.TankWaitSeconds);
    lua_pushnumber(L, Queue.HealerWaitSeconds);
    lua_pushnumber(L, Queue.DamageWaitSeconds);
    lua_pushnumber(L, Queue.PlayerWaitSeconds);
    lua_pushnumber(L, FPlatformTime::Seconds() - static_cast<double>(Queue.QueuedSeconds));
    return 13;
}

static int L_GetLFGProposal(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    const bool bExists = State && State->Mode == EWowLfgMode::Proposal && State->Proposal.ProposalId != 0;
    lua_pushboolean(L, bExists);
    if (!bExists)
    {
        return 1;
    }

    const FWowLfgProposal& Proposal = State->Proposal;
    const uint32 DungeonId = Proposal.DungeonEntry & 0x00FFFFFFu;
    const FLfgDungeonDbcEntry* Entry = FindLfgDungeon(static_cast<int32>(DungeonId));
    const FWowLfgProposalMember* PlayerMember = Proposal.Members.FindByPredicate(
        [](const FWowLfgProposalMember& Member) { return Member.bIsPlayer; });
    const uint32 PlayerRoles = PlayerMember ? PlayerMember->Roles : State->Roles;
    const int32 CompletedEncounters = CountLfgEncounterBits(Proposal.EncountersMask);

    lua_pushnumber(L, Entry ? Entry->TypeID : ((Proposal.DungeonEntry >> 24) & 0xFFu));
    lua_pushnumber(L, DungeonId);
    lua_pushstring(L, Entry ? TCHAR_TO_UTF8(*Entry->Name) : "");
    lua_pushstring(L, Entry ? TCHAR_TO_UTF8(*Entry->TextureFilename) : "");
    lua_pushstring(L, GetLfgRoleToken(PlayerRoles));
    lua_pushboolean(L, PlayerMember && PlayerMember->bAnswered);
    lua_pushnumber(L, CompletedEncounters); // Encounter names are not part of the queue packet.
    lua_pushnumber(L, CompletedEncounters);
    lua_pushnumber(L, Proposal.Members.Num());
    lua_pushboolean(L, (PlayerRoles & WowLfgRole::Leader) != 0);
    lua_pushboolean(L, false); // isHoliday
    return 12;
}

static int L_GetLFGProposalMember(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const FWowLfgState* State = GetLfgState(L);
    const int32 MemberIndex = static_cast<int32>(luaL_checknumber(L, 1)) - 1;
    if (!State || !State->Proposal.Members.IsValidIndex(MemberIndex))
    {
        lua_pushnil(L);
        return 1;
    }

    const FWowLfgProposalMember& Member = State->Proposal.Members[MemberIndex];
    int32 Level = 0;
    FString Name = FString::Printf(TEXT("Player %d"), MemberIndex + 1);
    const char* ClassToken = "WARRIOR";
    if (Member.bIsPlayer && Ctx)
    {
        Level = GetLocalLfgLevel(Ctx);
        if (Ctx->ConnectionManager)
        {
            const FString CharacterName = Ctx->ConnectionManager->GetCharacterName();
            if (!CharacterName.IsEmpty()) Name = CharacterName;
        }
        if (Ctx->EntityManager)
        {
            if (const FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer())
            {
                const uint8 ClassId = Player->GetClassId();
                if (ClassId < UE_ARRAY_COUNT(WowClassTokens)) ClassToken = WowClassTokens[ClassId];
            }
        }
    }

    lua_pushboolean(L, (Member.Roles & WowLfgRole::Leader) != 0);
    lua_pushstring(L, GetLfgRoleToken(Member.Roles));
    lua_pushnumber(L, Level);
    lua_pushboolean(L, Member.bAnswered);
    lua_pushboolean(L, Member.bAccepted);
    lua_pushstring(L, TCHAR_TO_UTF8(*Name));
    lua_pushstring(L, ClassToken);
    return 7;
}

static int L_GetLFGDungeonInfo(lua_State* L)
{
    const FLfgDungeonDbcEntry* Entry = FindLfgDungeon(static_cast<int32>(luaL_checknumber(L, 1)));
    if (!Entry)
    {
        lua_pushnil(L);
        return 1;
    }
    return PushLfgDungeonInfoValues(L, *Entry);
}

static int L_LFGGetDungeonInfoByID(lua_State* L)
{
    const FLfgDungeonDbcEntry* Entry = FindLfgDungeon(static_cast<int32>(luaL_checknumber(L, 1)));
    if (!Entry)
    {
        lua_pushnil(L);
        return 1;
    }
    PushLfgDungeonInfoTable(L, *Entry);
    return 1;
}

static int L_GetLFDChoiceInfo(lua_State* L)
{
    lua_settop(L, 0);
    lua_newtable(L);
    TSet<uint32> AddedHeaders;
    const FLfgDungeonsDbc& Dungeons = FDbcStore::Get().LfgDungeons();
    for (const FLfgDungeonDbcEntry& Entry : Dungeons.GetAll())
    {
        if (!Entry.IsDungeonFinderEntry() && !Entry.IsRaidBrowserEntry()) continue;
        lua_pushnumber(L, Entry.ID);
        PushLfgDungeonInfoTable(L, Entry);
        lua_settable(L, 1);

        if (Entry.GroupID > 0 && !AddedHeaders.Contains(Entry.GroupID))
        {
            AddedHeaders.Add(Entry.GroupID);
            const FLfgDungeonDbcEntry* HeaderSource = Dungeons.GetById(Entry.GroupID);
            lua_pushnumber(L, -static_cast<int32>(Entry.GroupID));
            PushLfgDungeonInfoTable(L, HeaderSource ? *HeaderSource : Entry, 0);
            lua_settable(L, 1);
        }
    }
    return 1;
}

static int PushLfgChoiceOrder(lua_State* L, bool bRaid)
{
    lua_settop(L, 0);
    lua_newtable(L);
    TSet<uint32> AddedHeaders;
    int32 LuaIndex = 1;
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
    {
        const bool bMatches = bRaid ? Entry.IsRaidBrowserEntry() : (Entry.TypeID == 1 || Entry.TypeID == 5);
        if (!bMatches) continue;
        if (Entry.GroupID > 0 && !AddedHeaders.Contains(Entry.GroupID))
        {
            AddedHeaders.Add(Entry.GroupID);
            lua_pushnumber(L, -static_cast<int32>(Entry.GroupID));
            lua_rawseti(L, 1, LuaIndex++);
        }
        lua_pushnumber(L, Entry.ID);
        lua_rawseti(L, 1, LuaIndex++);
    }
    return 1;
}

static int L_GetLFDChoiceOrder(lua_State* L) { return PushLfgChoiceOrder(L, false); }
static int L_GetLFRChoiceOrder(lua_State* L) { return PushLfgChoiceOrder(L, true); }

static int L_GetLFDChoiceCollapseState(lua_State* L)
{
    lua_settop(L, 0);
    lua_newtable(L);
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
    {
        if (Entry.GroupID == 0) continue;
        lua_pushnumber(L, -static_cast<int32>(Entry.GroupID));
        lua_pushboolean(L, false);
        lua_settable(L, 1);
    }
    return 1;
}

static int L_GetLFDChoiceEnabledState(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    lua_settop(L, 0);
    lua_newtable(L);
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
    {
        if (!Entry.IsDungeonFinderEntry() && !Entry.IsRaidBrowserEntry()) continue;
        lua_pushnumber(L, Entry.ID);
        lua_pushboolean(L, IsLfgEntryAvailable(Ctx, Entry));
        lua_settable(L, 1);
    }
    return 1;
}

static int L_GetLFDChoiceLockedState(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    lua_settop(L, 0);
    lua_newtable(L);
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
    {
        if (!Entry.IsDungeonFinderEntry() && !Entry.IsRaidBrowserEntry()) continue;
        lua_pushnumber(L, Entry.ID);
        lua_pushboolean(L, !IsLfgEntryAvailable(Ctx, Entry));
        lua_settable(L, 1);
    }
    return 1;
}

static int L_GetLFGQueuedList(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    lua_settop(L, 0);
    lua_newtable(L);
    if (State)
    {
        for (uint32 PackedEntry : State->SelectedDungeonEntries)
        {
            lua_pushnumber(L, PackedEntry & 0x00FFFFFFu);
            lua_pushboolean(L, true);
            lua_settable(L, 1);
        }
    }
    return 1;
}

static int L_ClearAllLFGDungeons(lua_State* L)
{
    if (FWowLfgState* State = GetLfgState(L)) State->SelectedDungeonEntries.Reset();
    return 0;
}

static int L_SetLFGDungeon(lua_State* L)
{
    FWowLfgState* State = GetLfgState(L);
    const FLfgDungeonDbcEntry* Entry = FindLfgDungeon(static_cast<int32>(luaL_checknumber(L, 1)));
    if (State && Entry) State->SelectedDungeonEntries.AddUnique(Entry->GetPackedEntry());
    return 0;
}

static int L_JoinLFG(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const FWowLfgState& State = Ctx->ConnectionManager->PacketHandler.LfgState;
        Ctx->ConnectionManager->SendLfgJoin(State.Roles, State.SelectedDungeonEntries, State.Comment);
    }
    return 0;
}

static int L_LeaveLFG(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendLfgLeave();
    return 0;
}

static int L_SetLFGComment(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager) return 0;
    const FString Comment = UTF8_TO_TCHAR(luaL_optstring(L, 1, ""));
    Ctx->ConnectionManager->PacketHandler.LfgState.Comment = Comment.Left(255);
    Ctx->ConnectionManager->SendLfgSetComment(Comment);
    return 0;
}

static int L_GetLFGComment(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    lua_pushstring(L, State ? TCHAR_TO_UTF8(*State->Comment) : "");
    return 1;
}

static int L_AnswerLFGProposal(lua_State* L, bool bAccept)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager)
    {
        const uint32 ProposalId = Ctx->ConnectionManager->PacketHandler.LfgState.Proposal.ProposalId;
        Ctx->ConnectionManager->SendLfgProposalResult(ProposalId, bAccept);
    }
    return 0;
}

static int L_AcceptProposal(lua_State* L) { return L_AnswerLFGProposal(L, true); }
static int L_RejectProposal(lua_State* L) { return L_AnswerLFGProposal(L, false); }

static int L_IsInLFGDungeon(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    lua_pushboolean(L, State && State->Mode == EWowLfgMode::Dungeon);
    return 1;
}

static int L_GetNumRandomDungeons(lua_State* L)
{
    int32 Count = 0;
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
    {
        if (Entry.TypeID == 6) ++Count;
    }
    lua_pushnumber(L, Count);
    return 1;
}

static int L_GetLFGRandomDungeonInfo(lua_State* L)
{
    const int32 RequestedIndex = static_cast<int32>(luaL_checknumber(L, 1));
    int32 CurrentIndex = 0;
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
    {
        if (Entry.TypeID != 6) continue;
        if (++CurrentIndex == RequestedIndex)
        {
            lua_pushnumber(L, Entry.ID);
            lua_pushstring(L, TCHAR_TO_UTF8(*Entry.Name));
            return 2;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int L_IsLFGDungeonJoinable(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const FLfgDungeonDbcEntry* Entry = FindLfgDungeon(static_cast<int32>(luaL_checknumber(L, 1)));
    lua_pushboolean(L, Entry && IsLfgEntryAvailable(Ctx, *Entry));
    return 1;
}

static int L_GetRandomDungeonBestChoice(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const FLfgDungeonDbcEntry* Best = nullptr;
    for (const FLfgDungeonDbcEntry& Entry : FDbcStore::Get().LfgDungeons().GetAll())
    {
        if (Entry.TypeID != 6 || !IsLfgEntryAvailable(Ctx, Entry)) continue;
        if (!Best || Entry.TargetLevel > Best->TargetLevel) Best = &Entry;
    }
    if (Best) lua_pushnumber(L, Best->ID); else lua_pushnil(L);
    return 1;
}

static int L_GetLFGRoleUpdate(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    const bool bInProgress = State && State->Mode == EWowLfgMode::RoleCheck;
    lua_pushboolean(L, bInProgress);
    lua_pushnumber(L, bInProgress ? State->RoleCheck.DungeonEntries.Num() : 0);
    lua_pushnumber(L, bInProgress ? State->RoleCheck.Members.Num() : 0);
    return 3;
}

static int L_GetLFGRoleUpdateSlot(lua_State* L)
{
    const FWowLfgState* State = GetLfgState(L);
    const int32 Index = static_cast<int32>(luaL_checknumber(L, 1)) - 1;
    if (!State || !State->RoleCheck.DungeonEntries.IsValidIndex(Index))
    {
        lua_pushnil(L);
        return 1;
    }
    const uint32 PackedEntry = State->RoleCheck.DungeonEntries[Index];
    const FLfgDungeonDbcEntry* Entry = FindLfgDungeon(static_cast<int32>(PackedEntry & 0x00FFFFFFu));
    lua_pushnumber(L, Entry ? Entry->TypeID : ((PackedEntry >> 24) & 0xFFu));
    lua_pushnumber(L, PackedEntry & 0x00FFFFFFu);
    return 2;
}

static int L_CompleteLFGRoleCheck(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    const bool bAccept = lua_toboolean(L, 1) != 0;
    if (Ctx && Ctx->ConnectionManager)
    {
        if (bAccept)
        {
            Ctx->ConnectionManager->SendLfgSetRoles(Ctx->ConnectionManager->PacketHandler.LfgState.Roles);
        }
        else
        {
            Ctx->ConnectionManager->SendLfgLeave();
        }
    }
    lua_pushboolean(L, true);
    return 1;
}

static int L_RequestLFDPlayerLockInfo(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendLfgPlayerLockInfoRequest();
    return 0;
}

static int L_RequestLFDPartyLockInfo(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendLfgPartyLockInfoRequest();
    return 0;
}

// ─── Registration ───────────────────────────────────────────────────────────────

// ── Calendar (Blizzard_Calendar) ────────────────────────────────────────────
// The month grid works on the client clock; there are no server calendar events yet, so every
// event/invite query is empty and creating events is not offered (CalendarCanAddEvent false).
// Weekdays are 1 = Sunday .. 7 = Saturday, months 1..12 (Stefan 10.10: the window opened empty).
static int32 GWowCalendarMonth = 0; // shown month, 0 = not set (today's month)
static int32 GWowCalendarYear = 0;

static int32 WowCalendarWeekday(const FDateTime& Date)
{
	return (static_cast<int32>(Date.GetDayOfWeek()) + 1) % 7 + 1; // UE: Monday = 0
}

static void WowCalendarNormalize(int32& Month, int32& Year)
{
	const int32 MonthIndex = Year * 12 + (Month - 1);
	Year = FMath::FloorToInt(MonthIndex / 12.0);
	Month = MonthIndex - Year * 12 + 1;
}

static void WowCalendarShown(int32& Month, int32& Year)
{
	if (GWowCalendarMonth == 0)
	{
		const FDateTime Today = FDateTime::Now();
		GWowCalendarMonth = Today.GetMonth();
		GWowCalendarYear = Today.GetYear();
	}
	Month = GWowCalendarMonth;
	Year = GWowCalendarYear;
}

static int WowCalendarPushMonth(lua_State* L, int32 Month, int32 Year)
{
	WowCalendarNormalize(Month, Year);
	Year = FMath::Clamp(Year, 1970, 2100);
	lua_pushnumber(L, Month);
	lua_pushnumber(L, Year);
	lua_pushnumber(L, FDateTime::DaysInMonth(Year, Month));
	lua_pushnumber(L, WowCalendarWeekday(FDateTime(Year, Month, 1)));
	return 4;
}

static int WowCalendarPushDate(lua_State* L, const FDateTime& Date)
{
	lua_pushnumber(L, WowCalendarWeekday(Date));
	lua_pushnumber(L, Date.GetMonth());
	lua_pushnumber(L, Date.GetDay());
	lua_pushnumber(L, Date.GetYear());
	return 4;
}

void WowRegisterCalendarServerApi(lua_State* L); // LuaCalendarApi.cpp (r240)

static void WowRegisterCalendarApi(lua_State* L)
{
	lua_register(L, "CalendarGetDate", [](lua_State* L2) -> int { return WowCalendarPushDate(L2, FDateTime::Now()); });
	lua_register(L, "CalendarGetMonth", [](lua_State* L2) -> int {
		int32 Month; int32 Year; WowCalendarShown(Month, Year);
		return WowCalendarPushMonth(L2, Month + static_cast<int32>(luaL_optnumber(L2, 1, 0)), Year);
	});
	lua_register(L, "CalendarGetAbsMonth", [](lua_State* L2) -> int {
		int32 Month; int32 Year; WowCalendarShown(Month, Year);
		return WowCalendarPushMonth(L2, static_cast<int32>(luaL_optnumber(L2, 1, Month)), static_cast<int32>(luaL_optnumber(L2, 2, Year)));
	});
	lua_register(L, "CalendarSetMonth", [](lua_State* L2) -> int {
		int32 Month; int32 Year; WowCalendarShown(Month, Year);
		Month += static_cast<int32>(luaL_optnumber(L2, 1, 0));
		WowCalendarNormalize(Month, Year);
		GWowCalendarMonth = Month; GWowCalendarYear = FMath::Clamp(Year, 1970, 2100);
		return 0;
	});
	lua_register(L, "CalendarSetAbsMonth", [](lua_State* L2) -> int {
		int32 Month; int32 Year; WowCalendarShown(Month, Year);
		Month = static_cast<int32>(luaL_optnumber(L2, 1, Month));
		Year = static_cast<int32>(luaL_optnumber(L2, 2, Year));
		WowCalendarNormalize(Month, Year);
		GWowCalendarMonth = Month; GWowCalendarYear = FMath::Clamp(Year, 1970, 2100);
		return 0;
	});
	// The client's range: back to the game's launch month, a year ahead.
	lua_register(L, "CalendarGetMinDate", [](lua_State* L2) -> int { return WowCalendarPushDate(L2, FDateTime(2004, 11, 1)); });
	lua_register(L, "CalendarGetMinHistoryDate", [](lua_State* L2) -> int { return WowCalendarPushDate(L2, FDateTime(2004, 11, 1)); });
	lua_register(L, "CalendarGetMaxDate", [](lua_State* L2) -> int {
		const FDateTime Today = FDateTime::Now();
		return WowCalendarPushDate(L2, FDateTime(Today.GetYear() + 1, 12, 31));
	});
	lua_register(L, "CalendarGetMaxCreateDate", [](lua_State* L2) -> int {
		const FDateTime Today = FDateTime::Now();
		return WowCalendarPushDate(L2, FDateTime(Today.GetYear() + 1, 12, 31));
	});
	lua_register(L, "CalendarGetWeekdayNames", [](lua_State* L2) -> int {
		for (const char* Day : {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}) lua_pushstring(L2, Day);
		return 7;
	});
	lua_register(L, "CalendarGetMonthNames", [](lua_State* L2) -> int {
		for (const char* MonthName : {"January", "February", "March", "April", "May", "June", "July", "August", "September",
			"October", "November", "December"}) lua_pushstring(L2, MonthName);
		return 12;
	});
	lua_register(L, "CalendarEventGetTypes", [](lua_State* L2) -> int {
		for (const char* EventType : {"Raid", "Dungeon", "PvP", "Meeting", "Other"}) lua_pushstring(L2, EventType);
		return 5;
	});
	lua_register(L, "CalendarEventGetRepeatOptions", [](lua_State* L2) -> int {
		for (const char* RepeatName : {"Never", "Weekly", "Biweekly", "Monthly"}) lua_pushstring(L2, RepeatName);
		return 4;
	});
	lua_register(L, "CalendarDefaultGuildFilter", [](lua_State* L2) -> int {
		lua_pushnumber(L2, 1); lua_pushnumber(L2, 80); lua_pushnumber(L2, 1);
		return 3;
	});

	static const char* const ZeroQueries[] = { "CalendarGetNumDayEvents", "CalendarGetNumPendingInvites",
		"CalendarEventGetNumInvites", "GetNumPendingCalendarInvites" };
	for (const char* Name : ZeroQueries)
		lua_register(L, Name, [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });

	static const char* const FalseQueries[] = { "CalendarIsActionPending", "CalendarCanAddEvent", "CalendarCanSendInvite",
		"CalendarEventCanEdit", "CalendarEventCanModerate", "CalendarEventHaveSettingsChanged", "CalendarEventIsModerator",
		"CalendarEventHasPendingInvite", "CalendarContextEventCanEdit", "CalendarContextEventCanComplain",
		"CalendarContextInviteIsPending", "CalendarContextEventClipboard", "CanEditGuildEvent" };
	for (const char* Name : FalseQueries)
		lua_register(L, Name, [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });

	// No events: lookups return nothing, actions do nothing.
	static const char* const EmptyCalls[] = { "OpenCalendar", "CalendarCloseEvent", "CalendarGetEventIndex",
		"CalendarGetDayEvent", "CalendarGetDayEventSequenceInfo", "CalendarGetFirstPendingInvite", "CalendarGetHolidayInfo",
		"CalendarGetRaidInfo", "CalendarGetEventInfo", "CalendarContextGetEventIndex", "CalendarEventGetInvite",
		"CalendarEventGetSelectedInvite", "CalendarEventGetInviteResponseTime", "CalendarEventGetInviteSortCriterion",
		"CalendarEventGetCalendarType", "CalendarEventGetStatusOptions", "CalendarEventGetTextures",
		"CalendarContextInviteStatus", "CalendarContextInviteType", "CalendarContextInviteModeratorStatus",
		"CalendarContextEventGetCalendarType", "CalendarAddEvent", "CalendarUpdateEvent", "CalendarRemoveEvent",
		"CalendarOpenEvent", "CalendarNewEvent", "CalendarNewGuildEvent", "CalendarNewGuildAnnouncement",
		"CalendarNewArenaTeamEvent", "CalendarEventSetDate", "CalendarEventSetDescription", "CalendarEventSetLocked",
		"CalendarEventSetLockoutDate", "CalendarEventSetLockoutTime", "CalendarEventSetModerator", "CalendarEventSetRepeatOption",
		"CalendarEventSetSize", "CalendarEventSetStatus", "CalendarEventSetTextureID", "CalendarEventSetTime",
		"CalendarEventSetTitle", "CalendarEventSetType", "CalendarEventSetAutoApprove", "CalendarEventClearAutoApprove",
		"CalendarEventClearLocked", "CalendarEventClearModerator", "CalendarEventInvite", "CalendarEventRemoveInvite",
		"CalendarEventSelectInvite", "CalendarEventSortInvites", "CalendarEventAvailable", "CalendarEventDecline",
		"CalendarEventTentative", "CalendarEventSignUp", "CalendarContextSelectEvent", "CalendarContextDeselectEvent",
		"CalendarContextEventCopy", "CalendarContextEventPaste", "CalendarContextEventRemove", "CalendarContextEventComplain",
		"CalendarContextEventSignUp", "CalendarContextInviteAvailable", "CalendarContextInviteDecline",
		"CalendarContextInviteTentative", "CalendarContextInviteRemove", "CalendarMassInviteGuild", "CalendarMassInviteArenaTeam" };
	for (const char* Name : EmptyCalls)
		lua_register(L, Name, [](lua_State*) -> int { return 0; });
}

void WowLuaApi::RegisterStubs(lua_State* L)
{
    // Instance / group
    lua_register(L, "IsInInstance", L_IsInInstance);
    lua_register(L, "GetNumPartyMembers", L_GetNumPartyMembers);
    lua_register(L, "GetNumRaidMembers", L_GetNumRaidMembers);
    lua_register(L, "IsInGuild", L_IsInGuild);
    lua_register(L, "GetMoney", L_GetMoney);
    lua_register(L, "InCombatLockdown", L_InCombatLockdown);
    lua_register(L, "IsResting", L_IsResting);
    lua_register(L, "IsMounted", L_IsMounted);
    lua_register(L, "IsFlying", L_IsFlying);
    lua_register(L, "IsSwimming", L_IsSwimming);
    lua_register(L, "IsFalling", L_IsFalling);
    lua_register(L, "IsStealthed", L_IsStealthed);
    lua_register(L, "GetPlayerMapPosition", L_GetPlayerMapPosition);

    // Unit API
    lua_register(L, "UnitName", L_UnitName);
    lua_register(L, "GetUnitName", L_GetUnitName);
    lua_register(L, "UnitLevel", L_UnitLevel);
    lua_register(L, "UnitHealth", L_UnitHealth);
    lua_register(L, "UnitHealthMax", L_UnitHealthMax);
    lua_register(L, "UnitPower", L_UnitPower);
    lua_register(L, "UnitPowerMax", L_UnitPowerMax);
    lua_register(L, "UnitMana", L_UnitPower);        // WoW 3.3.5 alias
    lua_register(L, "UnitManaMax", L_UnitPowerMax);   // WoW 3.3.5 alias
    lua_register(L, "UnitPowerType", L_UnitPowerType);
    lua_register(L, "UnitClass", L_UnitClass);
    lua_register(L, "UnitRace", L_UnitRace);
    lua_register(L, "UnitIsDead", L_UnitIsDead);
    lua_register(L, "UnitIsDeadOrGhost", L_UnitIsDeadOrGhost);
    lua_register(L, "UnitIsPlayer", L_UnitIsPlayer);
    lua_register(L, "UnitIsFriend", L_UnitIsFriend);
    lua_register(L, "UnitIsEnemy", L_UnitIsEnemy);
    lua_register(L, "UnitAffectingCombat", L_UnitAffectingCombat);

    // Tapped/tagged unit checks (for grey health bars on tapped mobs)
    lua_register(L, "UnitIsTapped", [](lua_State* L2) -> int {
        FWowEntity* Entity = ResolveUnit(L2);
        if (Entity)
        {
            uint32 DynFlags = Entity->GetField(UnitField::DYNAMIC_FLAGS);
            lua_pushboolean(L2, (DynFlags & 0x0004) != 0 ? 1 : 0); // UNIT_DYNFLAG_TAPPED
        }
        else
            lua_pushboolean(L2, 0);
        return 1;
    });
    lua_register(L, "UnitIsTappedByPlayer", [](lua_State* L2) -> int {
        FWowEntity* Entity = ResolveUnit(L2);
        if (Entity)
        {
            uint32 DynFlags = Entity->GetField(UnitField::DYNAMIC_FLAGS);
            lua_pushboolean(L2, (DynFlags & 0x0008) != 0 ? 1 : 0); // UNIT_DYNFLAG_TAPPED_BY_PLAYER
        }
        else
            lua_pushboolean(L2, 0);
        return 1;
    });
    lua_register(L, "UnitIsTappedByAllThreatList", [](lua_State* L2) -> int {
        lua_pushboolean(L2, 0); return 1;
    });

    // UnitSelectionColor — returns r,g,b,a for unit frame name/health bar coloring
    lua_register(L, "UnitSelectionColor", [](lua_State* L2) -> int {
        FWowEntity* Entity = ResolveUnit(L2);
        if (Entity)
        {
            FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
            const FWowEntity* Local = Ctx && Ctx->EntityManager ? Ctx->EntityManager->GetLocalPlayer() : nullptr;
            const int32 Reaction = GetUnitReactionRank(Ctx, Entity, Local);
            const bool bBluePlayer = Entity->IsPlayer() && Reaction > 4
                && (Entity->GetFieldByte(UnitField::BYTES_2, 1) & 0x05) == 0;
            lua_pushnumber(L2, bBluePlayer ? 0.0 : (Reaction <= 4 ? 1.0 : 0.0));
            lua_pushnumber(L2, bBluePlayer ? 0.5 : (Reaction >= 4 ? 1.0 : 0.0));
            lua_pushnumber(L2, bBluePlayer ? 1.0 : 0.0);
            lua_pushnumber(L2, 1.0);
        }
        else
        {
            lua_pushnumber(L2, 1.0); lua_pushnumber(L2, 1.0);
            lua_pushnumber(L2, 1.0); lua_pushnumber(L2, 1.0);
        }
        return 4;
    });

    // GetTimeToWellRested — returns seconds until fully rested (0 = already rested)
    lua_register(L, "GetTimeToWellRested", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0); return 1;
    });

    lua_register(L, "UnitBuff", L_UnitBuff);
    // Unit tooltip: unfinished objectives of your quests that this creature/object counts for,
    // as (quest title, "- Burly Rockjaw Trogg slain: 0/6") pairs.
    lua_register(L, "__WowUnitQuestLines", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const FWowEntity* Entity = ResolveUnit(L2, 1);
        if (!Ctx || !Ctx->ConnectionManager || !Entity || !Entity->Entry) return 0;
        if (Entity->IsUnit() && Entity->GetMaxHealth() > 0 && Entity->GetHealth() <= 0) return 0;
        const auto& Handler = Ctx->ConnectionManager->PacketHandler;
        const bool bGameObject = Entity->IsGameObject();
        int32 Pushed = 0;
        for (const FWowQuestLogEntry& Quest : Handler.QuestLog)
        {
            if (Quest.State == 1) continue;
            for (const FWowQuestObjective& Objective : Quest.Objectives)
            {
                if (!Objective.CreatureOrGOId || !Objective.Required || Objective.Count >= Objective.Required) continue;
                const bool bObjectiveGo = (Objective.CreatureOrGOId & 0x80000000u) != 0;
                const uint32 Id = bObjectiveGo ? uint32(-int32(Objective.CreatureOrGOId)) : Objective.CreatureOrGOId;
                if (bObjectiveGo != bGameObject || Id != Entity->Entry || Pushed >= 16) continue;
                FString Name = Objective.Text;
                if (Name.IsEmpty())
                    if (const FString* Cached = Handler.CreatureNameCache.Find(Id)) Name = *Cached + TEXT(" slain");
                lua_pushstring(L2, TCHAR_TO_UTF8(*(Quest.Title.IsEmpty() ? FString::Printf(TEXT("Quest %u"), Quest.QuestId) : Quest.Title)));
                lua_pushstring(L2, TCHAR_TO_UTF8(*FString::Printf(TEXT("- %s: %u/%u"), *Name, Objective.Count, Objective.Required)));
                Pushed += 2;
            }
        }
        // r372 (Stefan 16.10: a Duskbat showed no "The Damned - Duskbat Wing: 0/6"): quest items this
        // creature drops (the creature query's quest item list) count too, like the client's tooltip.
        if (!bGameObject)
            if (const auto* Template = Handler.CreatureTemplateCache.Find(Entity->Entry))
                for (const FWowQuestLogEntry& Quest : Handler.QuestLog)
                {
                    if (Quest.State == 1) continue;
                    for (const FWowQuestObjective& Objective : Quest.Objectives)
                    {
                        if (!Objective.ItemId || !Objective.Required || Objective.Count >= Objective.Required || Pushed >= 16) continue;
                        bool bDrops = false;
                        for (const uint32 DropId : Template->QuestItems) bDrops |= DropId == Objective.ItemId;
                        if (!bDrops) continue;
                        FString ItemName = Objective.Text;
                        if (ItemName.IsEmpty())
                            if (const FWowItemTemplateInfo* Item = Handler.ItemTemplates.Find(Objective.ItemId)) ItemName = Item->Name;
                        if (ItemName.IsEmpty()) ItemName = FString::Printf(TEXT("Item %u"), Objective.ItemId);
                        lua_pushstring(L2, TCHAR_TO_UTF8(*(Quest.Title.IsEmpty() ? FString::Printf(TEXT("Quest %u"), Quest.QuestId) : Quest.Title)));
                        lua_pushstring(L2, TCHAR_TO_UTF8(*FString::Printf(TEXT("- %s: %u/%u"), *ItemName, Objective.Count, Objective.Required)));
                        Pushed += 2;
                    }
                }
        return Pushed;
    });
    lua_register(L, "UnitDebuff", L_UnitDebuff);
    lua_register(L, "UnitAura", L_UnitAura);
    lua_register(L, "UnitExists", L_UnitExists);
    lua_register(L, "UnitGUID", L_UnitGUID);
    lua_register(L, "UnitStat", L_UnitStat);
    lua_register(L, "UnitAttackPower", L_UnitAttackPower);
    lua_register(L, "UnitRangedAttackPower", L_UnitRangedAttackPower);
    lua_register(L, "UnitXP", L_UnitXP);
    lua_register(L, "UnitXPMax", L_UnitXPMax);
    lua_register(L, "GetXPExhaustion", L_GetXPExhaustion);

    // Locale / client
    lua_register(L, "GetLocale", L_GetLocale);
    lua_register(L, "GetBuildInfo", L_GetBuildInfo);
    lua_register(L, "GetRealmName", L_GetRealmName);
    lua_register(L, "GetServerTime", L_GetServerTime);
    lua_register(L, "GetGameTime", L_GetGameTime);

    // Screen
    lua_register(L, "GetScreenWidth", L_GetScreenWidth);
    lua_register(L, "GetScreenHeight", L_GetScreenHeight);

    // Chat
    lua_register(L, "DoEmote", [](lua_State* State) -> int
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(State);
        if (!Ctx || !Ctx->ConnectionManager) return 0;
        const FString Command = UTF8_TO_TCHAR(luaL_checkstring(State, 1));
        if (Command.Equals(TEXT("stand"), ESearchCase::IgnoreCase))
            Ctx->ConnectionManager->SendStandStateChange(0);
        else if (const auto* Emote = FDbcStore::Get().EmotesText().GetByCommand(Command))
            Ctx->ConnectionManager->SendTextEmote(Emote->ID, Ctx->ConnectionManager->GetTargetGuid());
        return 0;
    });
    lua_register(L, "SendChatMessage", L_SendChatMessage);
    lua_register(L, "SendAddonMessage", L_SendAddonMessage);
    lua_register(L, "GetNumLanguages", L_GetNumLanguages);
    lua_register(L, "GetDefaultLanguage", L_GetDefaultLanguage);
    lua_register(L, "GetLanguageByIndex", L_GetLanguageByIndex);

    // Action bar
    lua_register(L, "HasAction", L_HasAction);
    lua_register(L, "GetActionInfo", L_GetActionInfo);
    lua_register(L, "GetActionTexture", L_GetActionTexture);
    lua_register(L, "GetActionCount", L_GetActionCount);
    lua_register(L, "GetActionCooldown", L_GetActionCooldown);
    lua_register(L, "PickupAction", L_PickupAction);
    lua_register(L, "PlaceAction", L_PlaceAction);
    lua_register(L, "IsCurrentAction", L_IsCurrentAction);
    lua_register(L, "IsUsableAction", L_IsUsableAction);
    lua_register(L, "IsAttackAction", L_IsAttackAction);
    lua_register(L, "IsActionInRange", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        int32 Slot = SafeActionSlot(L2);

        if (!Ctx || !Ctx->ConnectionManager || Slot < 0 || Slot >= 144)
        {
            lua_pushnil(L2); // nil = no action
            return 1;
        }

        const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
        if (Slot >= ActionButtons.Num() || ActionButtons[Slot] == 0)
        {
            lua_pushnil(L2); // nil = no action
            return 1;
        }

        uint32 ActionData = ActionButtons[Slot];
        uint8 ActionType = (ActionData >> 24) & 0xFF;
        uint32 ActionId = ActionData & 0x00FFFFFF;

        if (ActionType == 0)
        {
            const int32 Range=FWowSpellPresentation::InRange(Ctx->ConnectionManager,ActionId);
            if (Range<0) lua_pushnil(L2); else lua_pushinteger(L2,Range);
            return 1;
        }

        // r347 (Stefan 15.10: a dot blinked on item buttons): items, macros and equipment sets have
        // no range here, and the client answers nil for them. "In range" (1) made the action bars
        // show the range dot (RANGE_INDICATOR in the hotkey spot) on every item button, and
        // Bartender's hotkey update hid it again, so it blinked.
        lua_pushnil(L2);
        return 1;
    });
    // ActionHasRange(slot): the action has a range to check (spells aimed at a unit).
    lua_register(L, "ActionHasRange", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const int32 Slot = SafeActionSlot(L2);
        bool bHasRange = false;
        if (Ctx && Ctx->ConnectionManager && Slot >= 0)
        {
            const TArray<uint32>& Actions = Ctx->ConnectionManager->PacketHandler.ActionButtons;
            const uint32 Data = Actions.IsValidIndex(Slot) ? Actions[Slot] : 0;
            if (Data && ((Data >> 24) & 0xFF) == 0)
            {
                const auto& Dbc = FDbcStore::Get().Spells();
                if (const auto* Spell = Dbc.GetById(Data & 0x00FFFFFF))
                    bHasRange = Dbc.MaxRange(Spell->RangeIndex, false) > 0.f || Dbc.MaxRange(Spell->RangeIndex, true) > 0.f;
            }
        }
        lua_pushboolean(L2, bHasRange ? 1 : 0);
        return 1;
    });
    lua_register(L, "IsHelpfulSpell", [](lua_State* L2) -> int { return SpellTargetKind(L2, false); });
    lua_register(L, "IsHarmfulSpell", [](lua_State* L2) -> int { return SpellTargetKind(L2, true); });
    // No LFG roles / vehicle aiming in this client yet.
    lua_register(L, "UnitGroupRolesAssigned", [](lua_State* L2) -> int {
        lua_pushboolean(L2, 0); lua_pushboolean(L2, 0); lua_pushboolean(L2, 0); return 3; });
    lua_register(L, "IsVehicleAimAngleAdjustable", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "IsVehicleAimPowerAdjustable", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "GetBonusBarOffset", L_GetBonusBarOffset);
    lua_register(L, "ChangeActionBarPage", L_ChangeActionBarPage);
    lua_register(L, "GetCurrentActionBarPage", L_GetCurrentActionBarPage);
    lua_register(L, "GetActionBarPage", L_GetCurrentActionBarPage); // WoW 3.3.5 alias
    lua_register(L, "UseAction", L_UseAction);
    lua_register(L, "PetHasActionBar", L_PetHasActionBar);
    lua_register(L, "GetPetActionsUsable", L_GetPetActionsUsable);
    lua_register(L, "GetPetActionInfo", L_GetPetActionInfo);
    lua_register(L, "GetPetActionTexture", L_GetPetActionTexture);
    lua_register(L, "GetPetActionCooldown", L_GetPetActionCooldown);
    lua_register(L, "GetPetActionSlotUsable", L_GetPetActionSlotUsable);
    lua_register(L, "IsPetAttackAction", L_IsPetAttackAction);
    lua_register(L, "CastPetAction", L_CastPetAction);
    lua_register(L, "TogglePetAutocast", L_TogglePetAutocast);

    // Spell
    lua_register(L, "PickupSpell", L_PickupSpell);
    lua_register(L, "GetSpellInfo", L_GetSpellInfo);
    lua_register(L, "GetSpellCooldown", L_GetSpellCooldown);
    lua_register(L, "GetSpellTexture", L_GetSpellTexture);
    lua_register(L, "GetNumSpellTabs", L_GetNumSpellTabs);
    lua_register(L, "GetSpellTabInfo", L_GetSpellTabInfo);
    lua_register(L, "IsUsableSpell", L_IsUsableSpell);
    lua_register(L, "IsSpellInRange", L_IsSpellInRange);
    lua_register(L, "GetSpellBookItemInfo", L_GetSpellBookItemInfo);
    lua_register(L, "GetSpellName", L_GetSpellName);
    lua_register(L, "CastSpell", L_CastSpell);
    lua_register(L, "IsPassiveSpell", [](lua_State* L2) -> int {
        const auto* Spell = FDbcStore::Get().Spells().GetById(ResolveSpellArgument(L2));
        lua_pushboolean(L2, Spell && (Spell->Attributes & 0x40u)); return 1;
    });
    lua_register(L, "GetKnownSlotFromHighestRankSlot", [](lua_State* L2) -> int {
        const int32 Slot = static_cast<int32>(luaL_checkinteger(L2, 1));
        const auto& Slots=GetClientSpellBook(L2).HighestSlots;
        if (!Slots.IsValidIndex(Slot-1)) return 0;
        lua_pushinteger(L2, Slots[Slot-1]); return 1;
    });
    lua_register(L, "GetSpellAutocast", [](lua_State* L2) -> int {
        // Player-book spells do not expose pet autocast controls.
        lua_pushnil(L2); lua_pushnil(L2); return 2;
    });
    lua_register(L, "IsSelectedSpell", [](lua_State* L2) -> int {
        auto* Ctx = WowLuaApi::GetContext(L2); const uint32 Id = ResolveSpellArgument(L2);
        const auto* Progress = Ctx && Ctx->ConnectionManager
            ? Ctx->UnitSpellProgress.Find(Ctx->ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid) : nullptr;
        lua_pushboolean(L2, Id && Progress && Progress->CastId == Id); return 1;
    });
    lua_register(L, "CastSpellByName", L_CastSpellByName);
    lua_register(L, "CastSpellByID", L_CastSpellByID);

    // Item/Inventory
    lua_register(L, "GetItemInfo", L_GetItemInfo);
    lua_register(L, "GetItemCount", L_GetItemCount);
    lua_register(L, "GetItemIcon", L_GetItemIcon);
    lua_register(L, "GetContainerItemInfo", L_GetContainerItemInfo);
    lua_register(L, "GetContainerItemQuestInfo", L_GetContainerItemQuestInfo);
    lua_register(L, "GetContainerItemLink", L_GetContainerItemLink);
    lua_register(L, "GetContainerNumSlots", L_GetContainerNumSlots);
    lua_register(L, "GetBagName", L_GetBagName);
    lua_register(L, "GetContainerFreeSlots", L_GetContainerFreeSlots);
    lua_register(L, "GetInventoryItemLink", L_GetInventoryItemLink);
    lua_register(L, "GetInventoryItemTexture", L_GetInventoryItemTexture);
    lua_register(L, "ItemTextGetItem", L_ItemTextGetItem);
    lua_register(L, "ItemTextGetText", L_ItemTextGetText);
    lua_register(L, "ItemTextGetCreator", L_ItemTextGetCreator);
    lua_register(L, "ItemTextGetMaterial", L_ItemTextGetMaterial);
    lua_register(L, "ItemTextGetPage", L_ItemTextGetPage);
    lua_register(L, "ItemTextHasNextPage", L_ItemTextHasNextPage);
    lua_register(L, "ItemTextNextPage", L_ItemTextNextPage);
    lua_register(L, "ItemTextPrevPage", L_ItemTextPrevPage);
    lua_register(L, "CloseItemText", L_CloseItemText);
    lua_register(L, "GetInventorySlotInfo", L_GetInventorySlotInfo);

    // Target
    lua_register(L, "TargetUnit", L_TargetUnit);
    lua_register(L, "ClearTarget", L_ClearTarget);
    lua_register(L, "FocusUnit", L_FocusUnit);
    lua_register(L, "ClearFocus", L_ClearFocusUnit);

    // Combat
    lua_register(L, "AttackTarget", L_AttackTarget);
    lua_register(L, "StopAttack", L_StopAttack);

    // Social
    lua_register(L, "GetNumFriends", L_GetNumFriends);
    lua_register(L, "GetFriendInfo", L_GetFriendInfo);
    lua_register(L, "GetNumGuildMembers", L_GetNumGuildMembers);
    lua_register(L, "GetGuildRosterInfo", L_GetGuildRosterInfo);
    lua_register(L, "GetGuildInfo", L_GetGuildInfo);

    // Quest
    lua_register(L, "GetNumQuestLogEntries", L_GetNumQuestLogEntries);
    lua_register(L, "GetQuestLogTitle", L_GetQuestLogTitle);
    lua_register(L, "GetQuestLogQuestText", L_GetQuestLogQuestText);
    lua_register(L, "SelectQuestLogEntry", L_SelectQuestLogEntry);
    lua_register(L, "IsQuestComplete", L_IsQuestComplete);

    // CVar
    lua_register(L, "SetCVar", L_SetCVar);
    lua_register(L, "GetCVar", L_GetCVar);
    lua_register(L, "GetCVarBool", L_GetCVarBool);
    lua_register(L, "__WowTooltipFadeTime", L_WowTooltipFadeTime); // r404
    lua_register(L, "RegisterForSaveVariables", L_RegisterForSaveVariables);

    // Sound/UI
    lua_register(L, "PlaySound", L_PlaySound);
    lua_register(L, "PlaySoundFile", L_PlaySoundFile);
    lua_register(L, "StopMusic", L_StopMusic);
    lua_register(L, "SetPortraitTexture", L_SetPortraitTexture);
    lua_register(L, "SetPortraitToTexture", L_SetPortraitToTexture);
    lua_register(L, "SetBagPortraitTexture", L_SetBagPortraitTexture);
    lua_register(L, "GetMoneyString", L_GetMoneyString);
    lua_register(L, "GetCoinTextureString", L_GetCoinTextureString);
    lua_register(L, "BreakUpLargeNumbers", L_BreakUpLargeNumbers);

    // Misc
    lua_register(L, "IsLoggedIn", L_IsLoggedIn);
    lua_register(L, "GetFramerate", L_GetFramerate);
    // Addon/Binding stubs (kept for compatibility)
    lua_register(L, "GetAddOnInfo", L_GetAddOnInfo);
    lua_register(L, "GetAddOnMetadata", L_GetAddOnMetadata);
    lua_register(L, "GetAuctionItemClasses", L_GetAuctionItemClasses);
    lua_register(L, "GetAuctionItemSubClasses", L_GetAuctionItemSubClasses);
    lua_register(L, "GetRepairAllCost", L_GetRepairAllCost);
    lua_register(L, "IsInventoryItemLocked", L_IsInventoryItemLocked);
    lua_register(L, "GetNumAddOns", L_GetNumAddOns);
    lua_register(L, "IsAddOnLoaded", L_IsAddOnLoaded);
    lua_register(L, "EnableAddOn", L_EnableAddOn);
    lua_register(L, "DisableAddOn", L_DisableAddOn);
    lua_register(L, "EnableAllAddOns", [](lua_State* L2) -> int { return L_SetAllAddOns(L2, true); });
    lua_register(L, "DisableAllAddOns", [](lua_State* L2) -> int { return L_SetAllAddOns(L2, false); });
    lua_register(L, "IsAddOnLoadOnDemand", L_IsAddOnLoadOnDemand);
    lua_register(L, "GetAddOnDependencies", L_GetAddOnDependencies);
    lua_register(L, "ShowAddonManager", [](lua_State* L2) -> int { UWowUIManager::RequestAddonManager(); return 0; });
    lua_register(L, "LoadAddOn", L_LoadAddOn);
    lua_register(L, "GetNumBindings", L_GetNumBindings);
    lua_register(L, "GetBinding", L_GetBinding);
    lua_register(L, "GetBindingKey", L_GetBindingKey);
    lua_register(L, "GetBindingAction", L_GetBindingAction);
    lua_register(L, "RunBinding", L_RunBinding);
    lua_register(L, "LoadBindings", L_LoadBindings);
    lua_register(L, "GetCurrentBindingSet", L_GetCurrentBindingSet);
    lua_register(L, "SetBinding", L_SetBinding);
    lua_register(L, "SaveBindings", L_SaveBindings);

    // Shapeshift forms - temporarily disabled due to compilation issues
    // lua_register(L, "GetNumShapeshiftForms", L_GetNumShapeshiftForms);
    // lua_register(L, "GetShapeshiftFormInfo", L_GetShapeshiftFormInfo);

    // Map functions
    lua_register(L, "GetZoneText", L_GetRealZoneText);       // WoW 3.3.5: GetZoneText used by MainMenuBar
    lua_register(L, "GetRealZoneText", L_GetRealZoneText);
    lua_register(L, "GetSubZoneText", L_GetSubZoneText);
    lua_register(L, "GetMinimapZoneText", L_GetMinimapZoneText);
    lua_register(L, "GetZonePVPInfo", L_GetZonePVPInfo);

    // Talent functions - temporarily disabled due to compilation issues
    // lua_register(L, "GetNumTalentTabs", L_GetNumTalentTabs);
    lua_register(L, "GetTalentTabInfo", L_GetTalentTabInfo);
    lua_register(L, "GetNumTalents", L_GetNumTalents);
    lua_register(L, "GetTalentInfo", L_GetTalentInfo);

    // Combat log
    lua_register(L, "CombatLogGetCurrentEventInfo", L_CombatLogGetCurrentEventInfo);
    lua_register(L, "CombatLogGetNumEntries", L_CombatLogGetNumEntries);
    lua_register(L, "CombatLogGetCurrentEntry", L_CombatLogGetCurrentEntry);
    lua_register(L, "CombatLogSetCurrentEntry", L_CombatLogSetCurrentEntry);
    lua_register(L, "CombatLogAdvanceEntry", L_CombatLogAdvanceEntry);
    lua_register(L, "CombatLog_Object_IsA", L_CombatLog_Object_IsA);
    lua_register(L, "CombatLogAddFilter", [](lua_State* L2) -> int {
        return 0;
    });
    lua_register(L, "CombatLogResetFilter", [](lua_State* L2) -> int {
        return 0;
    });

    // Set CURRENT_ACTIONBAR_PAGE global
    lua_pushnumber(L, 1);
    lua_setglobal(L, "CURRENT_ACTIONBAR_PAGE");

    // ── Critical missing stubs (from FrameXML error log) ──

    // Platform detection
#if PLATFORM_MAC
    lua_register(L, "IsMacClient", [](lua_State* L2) -> int {
        lua_pushboolean(L2, true);
        return 1;
    });
#else
    lua_register(L, "IsMacClient", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
#endif
    lua_register(L, "IsWindowsClient", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "IsLinuxClient", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });

    // CVar extended
    lua_register(L, "GetCVarDefault", L_GetCVarDefault);

    // Video/Audio options stubs
    lua_register(L, "GetCurrentMultisampleFormat", [](lua_State* L2) -> int { lua_pushnumber(L2, 1); return 1; });
    lua_register(L, "SetMultisampleFormat", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "IsStereoVideoAvailable", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "Sound_GameSystem_GetOutputDriverNameByIndex", [](lua_State* L2) -> int { lua_pushstring(L2, "Default"); return 1; });
    lua_register(L, "Sound_GameSystem_GetNumOutputDrivers", [](lua_State* L2) -> int { lua_pushnumber(L2, 1); return 1; });
    lua_register(L, "Sound_ChatSystem_GetNumOutputDrivers", [](lua_State* L2) -> int { lua_pushnumber(L2, 1); return 1; });
    lua_register(L, "Sound_ChatSystem_GetOutputDriverNameByIndex", [](lua_State* L2) -> int { lua_pushstring(L2, "Default"); return 1; });
    lua_register(L, "GetScreenResolutions", [](lua_State* L2) -> int { lua_pushstring(L2, "1920x1080"); return 1; });
    lua_register(L, "GetCurrentResolution", [](lua_State* L2) -> int { lua_pushnumber(L2, 1); return 1; });
    lua_register(L, "SetScreenResolution", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetVideoCaps", [](lua_State* L2) -> int { lua_pushstring(L2, ""); return 1; });

    // FrameXML utility functions (called from OnLoad scripts)
    lua_register(L, "ScrollFrame_OnLoad", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "SmallMoneyFrame_OnLoad", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "MoneyFrame_OnLoad", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ChatFrame_OnUpdate", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ChatEdit_OnUpdate", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "MessageFrameScrollButton_OnUpdate", [](lua_State* L2) -> int { return 0; });

    // Mirror timer (breath, fatigue, environmental fire)
    lua_register(L, "GetMirrorTimerProgress", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const char* TimerName = lua_tostring(L2, 1);
        const float Progress = Ctx && Ctx->FrameManager && TimerName
            ? Ctx->FrameManager->GetMirrorTimerProgress(UTF8_TO_TCHAR(TimerName))
            : 0.0f;
        lua_pushnumber(L2, Progress);
        return 1;
    });
    lua_register(L, "GetMirrorTimerInfo", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const int32 OneBasedIndex = static_cast<int32>(luaL_optinteger(L2, 1, 1));
        FWowMirrorTimerState Timer;
        const bool bFound = Ctx && Ctx->FrameManager &&
            Ctx->FrameManager->GetMirrorTimerInfo(OneBasedIndex, Timer);

        const FString TimerName = bFound ? Timer.Name : TEXT("UNKNOWN");
        const FString Label = bFound ? Timer.Label : FString();
        lua_pushstring(L2, TCHAR_TO_UTF8(*TimerName));
        lua_pushnumber(L2, bFound ? Timer.ValueMs : 0.0f);
        lua_pushnumber(L2, bFound ? Timer.MaxValueMs : 0.0f);
        lua_pushnumber(L2, bFound ? Timer.Scale : 0.0f);
        // The 3.3.5 client API returns paused as numeric 0/1. MirrorTimer.lua
        // compares this value numerically during PLAYER_ENTERING_WORLD.
        lua_pushnumber(L2, bFound && Timer.bPaused ? 1.0 : 0.0);
        lua_pushstring(L2, TCHAR_TO_UTF8(*Label));
        return 6;
    });

    // Input state
    lua_register(L, "IsModifierKeyDown", [](lua_State* L2) -> int {
        if (FSlateApplication::IsInitialized())
        {
            FModifierKeysState ModKeys = FSlateApplication::Get().GetModifierKeys();
            bool AnyModDown = ModKeys.IsShiftDown() || ModKeys.IsControlDown() || ModKeys.IsAltDown();
            lua_pushboolean(L2, AnyModDown);
        }
        else
        {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "IsShiftKeyDown", [](lua_State* L2) -> int {
        if (FSlateApplication::IsInitialized())
        {
            lua_pushboolean(L2, FSlateApplication::Get().GetModifierKeys().IsShiftDown());
        }
        else
        {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "IsControlKeyDown", [](lua_State* L2) -> int {
        if (FSlateApplication::IsInitialized())
        {
            lua_pushboolean(L2, FSlateApplication::Get().GetModifierKeys().IsControlDown());
        }
        else
        {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "IsAltKeyDown", [](lua_State* L2) -> int {
        if (FSlateApplication::IsInitialized())
        {
            lua_pushboolean(L2, FSlateApplication::Get().GetModifierKeys().IsAltDown());
        }
        else
        {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "IsMouseButtonDown", [](lua_State* L2) -> int {
        if (FSlateApplication::IsInitialized())
        {
            // 1/"LeftButton", 2/"RightButton", 3/"MiddleButton" (FCFTab_OnUpdate passes self.dragButton).
            int32 Button = 1;
            if (lua_type(L2, 1) == LUA_TSTRING)
            {
                const FString Name = UTF8_TO_TCHAR(lua_tostring(L2, 1));
                Button = Name == TEXT("RightButton") ? 2 : Name == TEXT("MiddleButton") ? 3 : 1;
            }
            else
                Button = static_cast<int32>(luaL_optnumber(L2, 1, 1));
            TSet<FKey> PressedButtons = FSlateApplication::Get().GetPressedMouseButtons();
            bool IsPressed = false;
            if (Button == 1) IsPressed = PressedButtons.Contains(EKeys::LeftMouseButton);
            else if (Button == 2) IsPressed = PressedButtons.Contains(EKeys::RightMouseButton);
            else if (Button == 3) IsPressed = PressedButtons.Contains(EKeys::MiddleMouseButton);
            lua_pushboolean(L2, IsPressed);
        }
        else
        {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    // MainMenuMicroButton's OnUpdate refreshes addon memory for its tooltip.
    lua_register(L, "UpdateAddOnMemoryUsage", [](lua_State*) -> int { return 0; });
    lua_getglobal(L, "GetAddOnMemoryUsage");
    const bool bNoMemoryUsage = lua_isnil(L, -1);
    lua_pop(L, 1);
    if (bNoMemoryUsage)
        lua_register(L, "GetAddOnMemoryUsage", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetMouseFocus", [](lua_State* L2) -> int {
        if(auto* Ctx=WowLuaApi::GetContext(L2);Ctx && Ctx->FrameManager)
        {
            const FString Name=Ctx->FrameManager->GetFrameName(Ctx->FrameManager->GetHoveredFrame());
            if(!Name.IsEmpty()){lua_getglobal(L2,TCHAR_TO_UTF8(*Name));return 1;}
        }
        lua_pushnil(L2);return 1;
    });
    lua_register(L,"FrameStackTooltip_Toggle",[](lua_State* L2)->int {
        if(auto* Ctx=WowLuaApi::GetContext(L2);Ctx && Ctx->FrameManager)Ctx->FrameManager->ToggleFrameStack();
        return 0;
    });
    lua_register(L, "GetMousePosition", [](lua_State* L2) -> int {
        if (FSlateApplication::IsInitialized())
        {
            FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
            // Convert to WoW UI coordinates by dividing by UIScale (viewport_height / 768.0)
            if (GEngine && GEngine->GameViewport)
            {
                FViewport* Viewport = GEngine->GameViewport->Viewport;
                if (Viewport)
                {
                    FIntPoint ViewportSize = Viewport->GetSizeXY();
                    float UIScale = FMath::Max(ViewportSize.Y, 1) / 768.0f * FWowClientPreferences::GetUIScale();
                    float WowX = Viewport->GetMouseX() / UIScale;
                    float WowY = Viewport->GetMouseY() / UIScale;
                    lua_pushnumber(L2, WowX);
                    lua_pushnumber(L2, WowY);
                    return 2;
                }
            }
            lua_pushnumber(L2, CursorPos.X);
            lua_pushnumber(L2, CursorPos.Y);
            return 2;
        }
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        return 2;
    });

    // Zone/Map functions — real implementations registered earlier (L_GetRealZoneText etc.)

    // Shapeshift/stance
    lua_register(L, "GetNumShapeshiftForms", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (!Player)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        uint8 ClassId = Player->GetClassId();
        int32 Level = Player->GetLevel();

        switch (ClassId)
        {
        case 1: // Warrior - Battle/Defensive/Berserker stance
            if (Level >= 10) lua_pushnumber(L2, Level >= 30 ? 3 : (Level >= 20 ? 2 : 1));
            else lua_pushnumber(L2, 0);
            break;
        case 11: // Druid - Bear/Aquatic/Cat/Travel/Moonkin/Tree/Flight
            if (Level >= 15)
            {
                int32 Forms = 1; // Bear at 15
                if (Level >= 16) Forms++; // Aquatic at 16
                if (Level >= 20) Forms++; // Cat at 20
                if (Level >= 30) Forms++; // Travel at 30
                if (Level >= 40) Forms++; // Moonkin/Tree available
                if (Level >= 60) Forms++; // Flight form
                lua_pushnumber(L2, Forms);
            }
            else lua_pushnumber(L2, 0);
            break;
        case 9: // Warlock - might have metamorphosis
            lua_pushnumber(L2, Level >= 60 ? 1 : 0);
            break;
        case 6: // Death Knight - might have presence forms
            lua_pushnumber(L2, Level >= 55 ? 3 : 0);
            break;
        default:
            lua_pushnumber(L2, 0);
            break;
        }
        return 1;
    });
    lua_register(L, "GetShapeshiftFormInfo", [](lua_State* L2) -> int {
        lua_pushstring(L2, ""); lua_pushstring(L2, ""); lua_pushboolean(L2, 0); lua_pushboolean(L2, 0);
        return 4;
    });

    // Talent extended
    lua_register(L, "GetNumTalentTabs", [](lua_State* L2) -> int { lua_pushnumber(L2, 3); return 1; });
    lua_register(L, "GetActiveTalentGroup", [](lua_State* L2) -> int { lua_pushnumber(L2, 1); return 1; });
    lua_register(L, "GetNumTalentGroups", [](lua_State* L2) -> int { lua_pushnumber(L2, 1); return 1; });
    lua_register(L, "GetTalentPreviewCount", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });

    // Minimap/tracking
    lua_register(L, "GetNumTrackingTypes", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetTrackingInfo", [](lua_State* L2) -> int {
        lua_pushstring(L2, ""); lua_pushstring(L2, ""); lua_pushboolean(L2, 0); lua_pushstring(L2, "");
        return 4;
    });
    lua_register(L, "ToggleMinimap", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetPlayerFacing", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        // Get player entity
        FWowEntity* Player = Ctx->EntityManager->Find(Ctx->EntityManager->LocalPlayerGuid);
        if (!Player)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        // Return player's facing angle from movement info
        lua_pushnumber(L2, Player->Movement.Orientation);
        return 1;
    });

    // Misc commonly needed
    lua_register(L, "ToggleGameMenu", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "Logout", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "Quit", [](lua_State* L2) -> int { return 0; });
    // Deferred: the Lua VM is rebuilt on the next game tick (cannot be torn down from inside Lua).
    lua_register(L, "ReloadUI", [](lua_State* L2) -> int { UWowUIManager::RequestReloadUI(); return 0; });
    lua_register(L, "Screenshot", [](lua_State* L2) -> int { UWowScreenshotManager::RequestActiveViewport(); return 0; });
    lua_register(L, "SetUIVisibility", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "OpeningCinematic", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "SetCursor", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ResetCursor", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ShowCursor", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "HideCursor", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetCursorInfo", L_GetCursorInfo);
    lua_register(L, "ClearCursor", L_ClearCursor);
    lua_register(L, "GetItemQualityColor", [](lua_State* L2) -> int {
        // Returns r,g,b,hex for item quality. Simplified: return white for all.
        lua_pushnumber(L2, 1); lua_pushnumber(L2, 1); lua_pushnumber(L2, 1);
        lua_pushstring(L2, "|cffffffff");
        return 4;
    });
    lua_register(L, "GetNumEquipmentSets", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "UnitCharacterPoints", [](lua_State* L2) -> int {
        // UnitCharacterPoints(unit) → unspentTalentPoints
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Entity && Entity->IsUnit()) {
            FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
            // TODO: Get from entity data when talent system is implemented
            lua_pushnumber(L2, 0);
        } else {
            lua_pushnumber(L2, 0);
        }
        return 1;
    });

    // Chat system functions
    lua_register(L, "ChatFrame_OnLoad", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ChatEdit_OnLoad", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ChatEdit_OnUpdate", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "MessageFrameScrollButton_OnLoad", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "HybridScrollFrame_OnLoad", [](lua_State* L2) -> int { return 0; });

    // UI utility functions
    lua_register(L, "PanelTemplates_SetNumTabs", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "PanelTemplates_TabResize", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "PanelTemplates_DisableTab", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "StaticPopup_OnUpdate", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "MoneyInputFrame_SetMode", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "MoneyInputFrame_SetOnValueChangedFunc", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "MultiCastActionButton_OnLoad", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "SetTextStatusBarText", [](lua_State* L2) -> int {
        if (lua_istable(L2, 1) && lua_istable(L2, 2)) {
            lua_pushvalue(L2, 2);
            lua_setfield(L2, 1, "TextString");
        }
        return 0;
    });
    lua_register(L, "MoneyFrame_SetType", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ScrollFrame_OnScrollRangeChanged", [](lua_State* L2) -> int { return 0; });

    // Unit functions
    // r406 (Stefan 16.10: an Undead player's tooltip said "Alliance"): a player's faction from its race
    // (Human, Dwarf, Night Elf, Gnome, Draenei = Alliance; Orc, Undead, Tauren, Troll, Blood Elf = Horde);
    // other units have no faction group here (nil), so no wrong faction line.
    lua_register(L, "UnitFactionGroup", [](lua_State* L2) -> int {
        FWowEntity* Entity = ResolveUnit(L2);
        uint8 RaceId = Entity && Entity->IsPlayer() ? static_cast<FWowUnitEntity*>(Entity)->GetRaceId() : 0;
        // r408 (Stefan 17.10: the PvP micro button lost its faction emblem): FrameXML's OnLoads run before
        // the player object exists; for "player" the race comes from the chosen character then.
        if (RaceId == 0 && lua_isstring(L2, 1) && FCStringAnsi::Stricmp(lua_tostring(L2, 1), "player") == 0)
        {
            FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
            FWowCharacterInfo Character;
            if (Ctx && Ctx->ConnectionManager && Ctx->ConnectionManager->GetSelectedCharacterInfo(Character)) RaceId = Character.Race;
        }
        const bool bAlliance = RaceId == 1 || RaceId == 3 || RaceId == 4 || RaceId == 7 || RaceId == 11;
        const bool bHorde = RaceId == 2 || RaceId == 5 || RaceId == 6 || RaceId == 8 || RaceId == 10;
        if (!bAlliance && !bHorde) return 0;
        lua_pushstring(L2, bAlliance ? "Alliance" : "Horde");
        lua_pushstring(L2, bAlliance ? "Alliance" : "Horde");
        return 2;
    });
    lua_register(L, "UnitIsConnected", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushboolean(L2, 0);
            return 1;
        }

        uint64 UnitGuid = ResolveUnitGuid(L2, 1);
        if (UnitGuid == 0)
        {
            lua_pushboolean(L2, 0);
            return 1;
        }

        // Return true if entity exists (means it's connected/online)
        FWowEntity* Entity = Ctx->EntityManager->Find(UnitGuid);
        lua_pushboolean(L2, Entity != nullptr);
        return 1;
    });
    lua_register(L, "UnitChannelInfo", [](lua_State* L2) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
		const uint64 UnitGuid = ResolveUnitGuid(L2, 1);
		const FWowUnitSpellProgress* Progress = Ctx
			? Ctx->UnitSpellProgress.Find(UnitGuid) : nullptr;
		if (!Progress || !Progress->bChanneling)
		{
			return 0;
		}
		lua_pushstring(L2, TCHAR_TO_UTF8(*Progress->SpellName));
		lua_pushstring(L2, "");
		lua_pushstring(L2, TCHAR_TO_UTF8(*Progress->SpellName));
		lua_pushstring(L2, TCHAR_TO_UTF8(*Progress->SpellIcon));
		lua_pushnumber(L2, Progress->StartMs);
		lua_pushnumber(L2, Progress->EndMs);
		lua_pushboolean(L2, 0);
		lua_pushboolean(L2, 0);
		return 8;
    });
    lua_register(L, "UnitCreatureType", L_UnitCreatureType);
    lua_register(L, "UnitCreatureFamily", L_UnitCreatureFamily);
    lua_register(L, "UnitClassification", L_UnitClassification);
    lua_register(L, "UnitIsPVP", [](lua_State* L2) -> int {
        // UnitIsPVP(unit) → boolean (true if flagged for PVP)
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Entity && Entity->IsUnit()) {
            lua_pushboolean(L2, (Entity->GetFieldByte(UnitField::BYTES_2, 1) & 0x01) != 0);
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "UnitIsPVPFreeForAll", [](lua_State* L2) -> int {
        const FWowEntity* Entity = ResolveUnit(L2, 1);
        lua_pushboolean(L2, Entity && Entity->IsUnit() && (Entity->GetFieldByte(UnitField::BYTES_2, 1) & 0x04) != 0);
        return 1;
    });
    lua_register(L, "UnitPlayerControlled", [](lua_State* L2) -> int {
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (!Entity)
        {
            lua_pushboolean(L2, 0);
            return 1;
        }

        // Players are player-controlled, pets/minions controlled by players also count
        if (Entity->IsPlayer())
        {
            lua_pushboolean(L2, 1);
            return 1;
        }

        // Check if unit has a player owner/creator (for pets/minions)
        if (Entity->IsUnit())
        {
            FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
            // Check summoned by field or created by field for player control
            uint64 SummonedBy = Unit->GetField64(UnitField::SUMMONEDBY);
            uint64 CreatedBy = Unit->GetField64(UnitField::CREATEDBY);
            uint64 CharmedBy = Unit->GetField64(UnitField::CHARMEDBY);

            if (SummonedBy != 0 || CreatedBy != 0 || CharmedBy != 0)
            {
                lua_pushboolean(L2, 1);
                return 1;
            }
        }

        lua_pushboolean(L2, 0);
        return 1;
    });
    lua_register(L, "UnitIsCharmed", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "UnitIsPossessed", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "UnitCanAssist", [](lua_State* L2) -> int {
        // UnitCanAssist(unit1, unit2) → boolean (can unit1 assist unit2?)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        FWowEntity* Entity1 = ResolveUnit(L2, 1);
        FWowEntity* Entity2 = ResolveUnit(L2, 2);
        const bool bCanAssist = Entity1 && Entity2 &&
            GetUnitRelationship(Ctx, Entity1, Entity2) == EWowUnitRelationship::Friendly;
        lua_pushboolean(L2, bCanAssist ? 1 : 0);
        return 1;
    });
    lua_register(L, "UnitCanCooperate", [](lua_State* L2) -> int {
        // UnitCanCooperate(unit1, unit2) → boolean (can unit1 cooperate with unit2?)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        FWowEntity* Entity1 = ResolveUnit(L2, 1);
        FWowEntity* Entity2 = ResolveUnit(L2, 2);
        const bool bCanCooperate = Entity1 && Entity2 &&
            GetUnitRelationship(Ctx, Entity1, Entity2) == EWowUnitRelationship::Friendly;
        lua_pushboolean(L2, bCanCooperate ? 1 : 0);
        return 1;
    });
    lua_register(L, "UnitCanAttack", [](lua_State* L2) -> int {
        // UnitCanAttack(unit1, unit2) → boolean (can unit1 attack unit2?)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        FWowEntity* Entity1 = ResolveUnit(L2, 1);
        FWowEntity* Entity2 = ResolveUnit(L2, 2);
        // Neutral (yellow) creatures are attackable too; only friendly or
        // server-protected units are not.
        const bool bCanAttack = Entity1 && Entity2 && Entity1 != Entity2 &&
            GetUnitRelationship(Ctx, Entity1, Entity2) != EWowUnitRelationship::Friendly &&
            !IsUnitFlaggedUnattackable(Entity2) && !IsUnitFlaggedUnattackable(Entity1);
        lua_pushboolean(L2, bCanAttack ? 1 : 0);
        return 1;
    });
    lua_register(L, "UnitInRaid", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->ConnectionManager)
        {
            lua_pushnil(L2);
            return 1;
        }

        const uint64 UnitGuid = ResolveUnitGuid(L2, 1);
        if (UnitGuid == 0)
        {
            lua_pushnil(L2);
            return 1;
        }

        const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
        if (GroupInfo.IsEmpty() || !GroupInfo.IsRaidGroup())
        {
            lua_pushnil(L2);
            return 1;
        }

        if (UnitGuid == Ctx->EntityManager->LocalPlayerGuid)
        {
            lua_pushnumber(L2, 1);
            return 1;
        }

        for (int32 MemberIndex = 0; MemberIndex < GroupInfo.Members.Num(); ++MemberIndex)
        {
            if (GroupInfo.Members[MemberIndex].Guid == UnitGuid)
            {
                lua_pushnumber(L2, MemberIndex + 2);
                return 1;
            }
        }

        lua_pushnil(L2);
        return 1;
    });
    lua_register(L, "UnitInParty", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->ConnectionManager)
        {
            lua_pushnil(L2);
            return 1;
        }

        const uint64 UnitGuid = ResolveUnitGuid(L2, 1);
        if (UnitGuid == 0)
        {
            lua_pushnil(L2);
            return 1;
        }

        const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
        if (GroupInfo.IsEmpty() || GroupInfo.IsRaidGroup())
        {
            lua_pushnil(L2);
            return 1;
        }

        if (UnitGuid == Ctx->EntityManager->LocalPlayerGuid)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        for (int32 MemberIndex = 0; MemberIndex < GroupInfo.Members.Num(); ++MemberIndex)
        {
            if (GroupInfo.Members[MemberIndex].Guid == UnitGuid)
            {
                lua_pushnumber(L2, MemberIndex + 1);
                return 1;
            }
        }

        lua_pushnil(L2);
        return 1;
    });
    lua_register(L, "UnitInVehicle", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "UnitOnTaxi", [](lua_State* L2) -> int {
        // UnitOnTaxi(unit) → boolean (true if on taxi/flight)
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Entity && Entity->IsUnit()) {
            FWowUnitEntity* Unit = static_cast<FWowUnitEntity*>(Entity);
            uint32 UnitFlags = Unit->GetUnitFlags();
            // UNIT_FLAG_TAXI_FLIGHT = 0x100000
            lua_pushboolean(L2, (UnitFlags & 0x100000) != 0);
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "UnitIsAFK", [](lua_State* L2) -> int {
        // UnitIsAFK(unit) → boolean (true if AFK)
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Entity && Entity->IsPlayer()) {
            FWowPlayerEntity* Player = static_cast<FWowPlayerEntity*>(Entity);
            uint32 PlayerFlags = Player->GetPlayerFlags();
            // PLAYER_FLAG_AFK = 0x01
            lua_pushboolean(L2, (PlayerFlags & 0x01) != 0);
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "UnitIsDND", [](lua_State* L2) -> int {
        // UnitIsDND(unit) → boolean (true if Do Not Disturb)
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Entity && Entity->IsPlayer()) {
            FWowPlayerEntity* Player = static_cast<FWowPlayerEntity*>(Entity);
            uint32 PlayerFlags = Player->GetPlayerFlags();
            // PLAYER_FLAG_DND = 0x02
            lua_pushboolean(L2, (PlayerFlags & 0x02) != 0);
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "UnitXP", L_UnitXP);
    lua_register(L, "UnitXPMax", L_UnitXPMax);
    lua_register(L, "GetXPExhaustion", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushnil(L2);
            return 1;
        }

        // Get player entity
        FWowEntity* Player = Ctx->EntityManager->Find(Ctx->EntityManager->LocalPlayerGuid);
        if (!Player || !Player->IsPlayer())
        {
            lua_pushnil(L2);
            return 1;
        }

        // Rested XP is typically stored in player fields but exact field is TBD
        // For now return 0 but maintain proper return type (number)
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetRestState", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 1);
        lua_pushstring(L2, "Normal");
        lua_pushnumber(L2, 1);
        return 3;
    });

    // Security/Addon
    lua_register(L, "issecure", [](lua_State* L2) -> int { lua_pushboolean(L2, 1); return 1; });
    lua_register(L, "issecurevariable", [](lua_State* L2) -> int {
        lua_pushboolean(L2, 1);
        lua_pushstring(L2, "");
        return 2;
    });
    lua_register(L, "BNFeaturesEnabled", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "BNConnected", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "BNFeaturesEnabledAndConnected", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });

    // Inventory/Equipment
    lua_register(L, "GetInventoryAlertStatus", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    // Bag id -> its inventory slot: bags 1-4 sit in slots 20-23, bank bags 5-11 in slots 68-74
    // (the bag button and bag portrait tooltips call it on mouse-over).
    lua_register(L, "ContainerIDToInventoryID", [](lua_State* L2) -> int {
        const int32 Bag = static_cast<int32>(luaL_checkinteger(L2, 1));
        if (Bag >= 1 && Bag <= 4) { lua_pushinteger(L2, 19 + Bag); return 1; }
        if (Bag >= 5 && Bag <= 11) { lua_pushinteger(L2, 63 + Bag); return 1; }
        return 0;
    });
    // Duel and trade (StaticPopup DUEL_REQUESTED, TradeFrame.lua, addons): the same packets as the
    // native duel invite and trade window send. Missing before (Blizzard Lua scan 11.10).
    lua_register(L, "AcceptDuel", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Ctx->ConnectionManager->PacketHandler.CurrentDuel.ArbiterGuid)
            Ctx->ConnectionManager->SendAcceptDuel(static_cast<int64>(Ctx->ConnectionManager->PacketHandler.CurrentDuel.ArbiterGuid));
        return 0;
    });
    lua_register(L, "CancelDuel", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Ctx->ConnectionManager->PacketHandler.CurrentDuel.ArbiterGuid)
            Ctx->ConnectionManager->SendCancelDuel(static_cast<int64>(Ctx->ConnectionManager->PacketHandler.CurrentDuel.ArbiterGuid));
        return 0;
    });
    lua_register(L, "InitiateTrade", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const uint64 TradeGuid = ResolveUnitGuid(L2, 1);
        if (Ctx && Ctx->ConnectionManager && TradeGuid) Ctx->ConnectionManager->SendInitiateTrade(static_cast<int64>(TradeGuid));
        return 0;
    });
    lua_register(L, "BeginTrade", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendBeginTrade();
        return 0;
    });
    lua_register(L, "AcceptTrade", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendAcceptTrade();
        return 0;
    });
    lua_register(L, "CancelTradeAccept", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendUnacceptTrade();
        return 0;
    });
    lua_register(L, "CancelTrade", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendCancelTrade();
        return 0;
    });
    lua_register(L, "CancelLogout", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->CancelLogout();
        return 0;
    });
    // PaperDollFrame.lua (BAG_UPDATE) calls it for every slot; nothing reports broken gear yet.
    lua_register(L, "GetInventoryItemBroken", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetInventoryItemDurability", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 100);
        lua_pushnumber(L2, 100);
        return 2;
    });
    lua_register(L, "InRepairMode", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "GetContainerItemCooldown", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        return 3;
    });
    lua_register(L, "CursorHasItem", [](lua_State* L2) -> int {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        lua_pushboolean(L2, C && C->ConnectionManager && C->ConnectionManager->HasCursorItemPayload());
        return 1;
    });
    lua_register(L, "CursorHasSpell", L_CursorHasSpell);
    lua_register(L, "IsEquippedAction", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });

    // Loot
    lua_register(L, "GetLootRollTimeLeft", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetLootRollItemInfo", [](lua_State* L2) -> int {
        lua_pushstring(L2, "");
        lua_pushstring(L2, "");
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 1);
        lua_pushboolean(L2, 0);
        lua_pushboolean(L2, 0);
        return 6;
    });
    lua_register(L, "GetLootRollItemLink", [](lua_State* L2) -> int { lua_pushstring(L2, ""); return 1; });

    // Misc
    lua_register(L, "SetWhoToUI", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetNumVoiceSessions", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetTrackingTexture", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetExistingLocales", [](lua_State* L2) -> int { lua_pushstring(L2, TCHAR_TO_UTF8(*WowLocale::Get())); return 1; });
    lua_register(L, "GetCursorPosition", [](lua_State* L2) -> int {
        FWowLuaContext* CursorCtx = WowLuaApi::GetContext(L2);
        float CursorUiX = 0.f; float CursorUiY = 0.f;
        if (CursorCtx && CursorCtx->FrameManager && CursorCtx->FrameManager->GetLuaCursorPosition(CursorUiX, CursorUiY))
        {
            lua_pushnumber(L2, CursorUiX);
            lua_pushnumber(L2, CursorUiY);
            return 2;
        }
        if (FSlateApplication::IsInitialized())
        {
            FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
            // Convert to WoW UI coordinates by dividing by UIScale (viewport_height / 768.0)
            if (GEngine && GEngine->GameViewport)
            {
                FViewport* Viewport = GEngine->GameViewport->Viewport;
                if (Viewport)
                {
                    FIntPoint ViewportSize = Viewport->GetSizeXY();
                    float UIScale = FMath::Max(ViewportSize.Y, 1) / 768.0f * FWowClientPreferences::GetUIScale();
                    lua_pushnumber(L2, Viewport->GetMouseX() / UIScale);
                    lua_pushnumber(L2, (ViewportSize.Y - Viewport->GetMouseY()) / UIScale);
                    return 2;
                }
            }
            lua_pushnumber(L2, CursorPos.X);
            lua_pushnumber(L2, CursorPos.Y);
        }
        else
        {
            lua_pushnumber(L2, 0);
            lua_pushnumber(L2, 0);
        }
        return 2;
    });
    lua_register(L, "GetNumWorldStateUI", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    // No watched faction: nil, like the client. An empty name ("" is true in Lua) made
    // ReputationWatchBar_Update show an empty reputation bar and fail on FACTION_BAR_COLORS[0],
    // before it showed the XP bar again.
    lua_register(L, "GetWatchedFactionInfo", [](lua_State* L2) -> int {
        lua_pushnil(L2);
        return 1;
    });
    lua_register(L, "GetNumFactions", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetText", [](lua_State* L2) -> int {
        const char* key = luaL_optstring(L2, 1, "");
        lua_pushstring(L2, key);
        return 1;
    });
    lua_register(L, "HasPetUI", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushboolean(L2, 0);
            lua_pushboolean(L2, 0);
            return 2;
        }

        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (!Player)
        {
            lua_pushboolean(L2, 0);
            lua_pushboolean(L2, 0);
            return 2;
        }

        // Check if player has a pet - look for UNIT_FIELD_SUMMON
        uint64 PetGuid = Player->GetField64(UnitField::SUMMON);
        bool HasPet = (PetGuid != 0);

        // Check if it's a hunter pet (class 3 = Hunter)
        bool IsHunterPet = HasPet && (Player->GetClassId() == 3);

        lua_pushboolean(L2, HasPet);
        lua_pushboolean(L2, IsHunterPet);
        return 2;
    });
    lua_register(L, "HasPetSpells", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushnil(L2);
            return 1;
        }

        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (!Player)
        {
            lua_pushnil(L2);
            return 1;
        }

        const uint64 PetGuid = Player->GetField64(UnitField::SUMMON);
        if (PetGuid == 0)
        {
            lua_pushnil(L2);
            return 1;
        }

        lua_pushnumber(L2, 1);
        lua_pushstring(L2, Player->GetClassId() == 9 ? "DEMON" : "PET");
        return 2;
    });
    lua_register(L, "PetHasActionBar", L_PetHasActionBar);
    lua_register(L, "HasFullControl", [](lua_State* L2) -> int { lua_pushboolean(L2, 1); return 1; });
    lua_register(L, "GetComboPoints", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
        if (!Player)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        // Get target (combo points are typically on target)
        uint64 TargetGuid = ResolveUnitGuid(L2, 2); // second parameter is target
        if (TargetGuid == 0 && Ctx->ConnectionManager)
        {
            TargetGuid = static_cast<uint64>(Ctx->ConnectionManager->GetTargetGuid());
        }

        // r317: the points the server last sent (SMSG_UPDATE_COMBO_POINTS), when they are on that unit.
        if (!Ctx->ConnectionManager) { lua_pushnumber(L2, 0); return 1; }
        const FWowPacketHandler& Packets = Ctx->ConnectionManager->PacketHandler;
        lua_pushnumber(L2, TargetGuid && TargetGuid == Packets.ComboTargetGuid ? Packets.ComboPoints : 0);
        return 1;
    });
    // PvP numbers from the player's update fields (3.3.5: PLAYER_FIELD_KILLS = today | yesterday << 16,
    // TODAY/YESTERDAY_CONTRIBUTION, LIFETIME_HONORABLE_KILLS, HONOR/ARENA_CURRENCY). The PvP frame
    // showed its XML placeholder numbers because GetHonorCurrency did not exist (Stefan 10.10).
    static auto PvpField = [](lua_State* L2, uint16 Offset) -> uint32
    {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        FWowPlayerEntity* Player = C && C->EntityManager ? C->EntityManager->GetLocalPlayer() : nullptr;
        return Player ? Player->GetField(static_cast<uint16>(PlayerField::BASE + Offset)) : 0u;
    };
    lua_register(L, "GetHonorCurrency", [](lua_State* L2) -> int { lua_pushnumber(L2, PvpField(L2, 0x0469)); return 1; });
    lua_register(L, "GetArenaCurrency", [](lua_State* L2) -> int { lua_pushnumber(L2, PvpField(L2, 0x046A)); return 1; });
    lua_register(L, "CloseArenaTeamRoster", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetPVPLifetimeStats", [](lua_State* L2) -> int {
        lua_pushnumber(L2, PvpField(L2, 0x0438)); // honorable kills
        lua_pushnumber(L2, 0);                     // dishonorable kills (gone in 3.x)
        lua_pushnumber(L2, 0);                     // highest rank (old system)
        return 3;
    });
    lua_register(L, "GetPVPSessionStats", [](lua_State* L2) -> int {
        lua_pushnumber(L2, PvpField(L2, 0x0435) & 0xFFFF); // today's honorable kills
        lua_pushnumber(L2, PvpField(L2, 0x0436));          // today's honor
        return 2;
    });
    lua_register(L, "GetPVPYesterdayStats", [](lua_State* L2) -> int {
        lua_pushnumber(L2, PvpField(L2, 0x0435) >> 16);    // yesterday's honorable kills
        lua_pushnumber(L2, PvpField(L2, 0x0437));          // yesterday's honor
        lua_pushnumber(L2, 0);
        return 3;
    });
    lua_register(L, "GetPVPThisWeekStats", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        return 3;
    });
    lua_register(L, "GetLifetimeHonorableKills", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->EntityManager)
        {
            FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
            if (Player)
            {
                // Try to read from player honor fields if available
                // Honor kills field isn't defined in current PlayerField enum, return 0 for now
                uint32 HonorKills = 0; // Player->GetField(PlayerField::LIFETIME_HONORABLE_KILLS);
                lua_pushnumber(L2, HonorKills);
                return 1;
            }
        }
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetArenaTeam", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetNumArenaTeams", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetNumArenaOpponents", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "IsInArenaTeam", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "GetDodgeChance", L_GetDodgeChance);
    lua_register(L, "GetParryChance", L_GetParryChance);
    lua_register(L, "GetBlockChance", L_GetBlockChance);
    lua_register(L, "GetShieldBlock", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        // Get player entity
        FWowEntity* Player = Ctx->EntityManager->Find(Ctx->EntityManager->LocalPlayerGuid);
        if (!Player || !Player->IsPlayer())
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        FWowPlayerEntity* PlayerEntity = static_cast<FWowPlayerEntity*>(Player);

        // Calculate shield block from block rating
        int32 BlockRating = PlayerEntity->GetCombatRating(WowCombatRating::BLOCK);

        // Simple block value calculation - this would need proper rating conversion in real game
        int32 BlockValue = BlockRating; // Simplified, real calculation involves rating-to-percentage conversion

        lua_pushnumber(L2, BlockValue);
        return 1;
    });
    lua_register(L, "GetCritChance", L_GetCritChance);
    lua_register(L, "GetRangedCritChance", L_GetRangedCritChance);
    lua_register(L, "GetSpellCritChance", L_GetSpellCritChance);
    lua_register(L, "GetCombatRating", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        int32 RatingIndex = static_cast<int32>(luaL_checknumber(L2, 1));

        if (Ctx && Ctx->EntityManager)
        {
            FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
            if (Player)
            {
                int32 Rating = Player->GetCombatRating(RatingIndex);
                lua_pushnumber(L2, Rating);
                return 1;
            }
        }
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetCombatRatingBonus", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        int32 RatingIndex = static_cast<int32>(luaL_checknumber(L2, 1));

        if (Ctx && Ctx->EntityManager)
        {
            FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
            if (Player)
            {
                int32 Rating = Player->GetCombatRating(RatingIndex);
                // Convert rating to percentage bonus using level 80 conversion rates
                float BonusPercent = 0.0f;
                switch (RatingIndex)
                {
                case WowCombatRating::CRIT_MELEE:
                case WowCombatRating::CRIT_RANGED:
                case WowCombatRating::CRIT_SPELL:
                    BonusPercent = Rating / 45.91f;
                    break;
                case WowCombatRating::HASTE:
                case WowCombatRating::HASTE_MELEE:
                case WowCombatRating::HASTE_RANGED:
                case WowCombatRating::HASTE_SPELL:
                    BonusPercent = Rating / 32.78f;
                    break;
                case WowCombatRating::HIT_MELEE:
                case WowCombatRating::HIT_RANGED:
                case WowCombatRating::HIT_SPELL:
                    BonusPercent = Rating / 32.78f;
                    break;
                case WowCombatRating::DODGE:
                case WowCombatRating::PARRY:
                    BonusPercent = Rating / 45.25f;
                    break;
                case WowCombatRating::EXPERTISE:
                    BonusPercent = Rating / 8.2f;
                    break;
                default:
                    BonusPercent = Rating / 32.78f; // Default conversion
                    break;
                }
                lua_pushnumber(L2, BonusPercent);
                return 1;
            }
        }
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetManaRegen", L_GetManaRegen);
    lua_register(L, "GetExpertise", L_GetExpertise);
    lua_register(L, "GetSpellBonusDamage", L_GetSpellBonusDamage);
    lua_register(L, "GetSpellBonusHealing", L_GetSpellBonusHealing);
    lua_register(L, "GetSpellPenetration", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->EntityManager)
        {
            FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
            if (Player)
            {
                // Spell penetration isn't a combat rating in WotLK, but could be in stats
                // For now return 0 as it's not commonly tracked
                lua_pushnumber(L2, 0);
                return 1;
            }
        }
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetMastery", [](lua_State* L2) -> int {
        // Mastery doesn't exist in WotLK (3.3.5), added in Cataclysm
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetHaste", L_GetHaste);
    lua_register(L, "GetVersatility", [](lua_State* L2) -> int {
        // Versatility doesn't exist in WotLK (3.3.5), added in Warlords of Draenor
        lua_pushnumber(L2, 0);
        return 1;
    });

    // ── Final batch: remaining missing functions from error log ──
    lua_register(L, "FauxScrollFrame_GetOffset", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "FauxScrollFrame_OnVerticalScroll", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "FauxScrollFrame_Update", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetChatWindowInfo", [](lua_State* L2) -> int {
        // FrameXML expects name, size, RGB, numeric alpha, shown, locked,
        // docked and uninteractable. A boolean in the alpha position breaks
        // FloatingChatFrame's fading math and disables UIParent's OnUpdate.
        const int32 Index = static_cast<int32>(luaL_optinteger(L2, 1, 1));
        lua_pushstring(L2, Index == 2 ? "Combat Log" : "General");
        lua_pushnumber(L2, 14);
        lua_pushnumber(L2, 0); lua_pushnumber(L2, 0); lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0.25);
        lua_pushboolean(L2, Index == 1 || Index == 2);
        lua_pushboolean(L2, 1);
        if (Index == 1 || Index == 2) lua_pushnumber(L2, Index);
        else lua_pushnil(L2); // Lua treats numeric zero as true.
        lua_pushboolean(L2, 0);
        return 10;
    });
    lua_register(L, "SetChatWindowName", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetCVarMin", [](lua_State* L2) -> int { lua_pushstring(L2, "0"); return 1; });
    lua_register(L, "GetCVarMax", [](lua_State* L2) -> int { lua_pushstring(L2, "1"); return 1; });
    lua_register(L, "GetNetStats", [](lua_State* L2) -> int {
        const FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const int32 Latency = Ctx && Ctx->ConnectionManager ? Ctx->ConnectionManager->GetWorldLatencyMs() : 0;
        lua_pushnumber(L2, 0); lua_pushnumber(L2, 0);
        lua_pushnumber(L2, Latency); lua_pushnumber(L2, Latency);
        return 4;
    });
    lua_register(L, "GetSendMailPrice", [](lua_State* L2) -> int { lua_pushnumber(L2, 30); return 1; });
    lua_register(L, "GetVoiceSessionInfo", [](lua_State* L2) -> int {
        lua_pushstring(L2, ""); lua_pushboolean(L2, 0);
        return 2;
    });
    lua_register(L, "IsVoiceChatEnabled", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "PanelTemplates_SetTab", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "RequestBattlefieldPositions", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetNumBattlefieldScores", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "RequestBattlefieldScoreData", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "UnitCastingInfo", [](lua_State* L2) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
		const uint64 UnitGuid = ResolveUnitGuid(L2, 1);
		const FWowUnitSpellProgress* Progress = Ctx
			? Ctx->UnitSpellProgress.Find(UnitGuid) : nullptr;
		if (!Progress || Progress->bChanneling)
		{
			return 0;
		}
		lua_pushstring(L2, TCHAR_TO_UTF8(*Progress->SpellName));
		lua_pushstring(L2, "");
		lua_pushstring(L2, TCHAR_TO_UTF8(*Progress->SpellName));
		lua_pushstring(L2, TCHAR_TO_UTF8(*Progress->SpellIcon));
		lua_pushnumber(L2, Progress->StartMs);
		lua_pushnumber(L2, Progress->EndMs);
		lua_pushboolean(L2, 0);
		lua_pushnumber(L2, Progress->CastId);
		lua_pushboolean(L2, 0);
		return 9;
    });
    lua_register(L, "MoneyFrame_SetType", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "IsEquippedAction", [](lua_State* L2) -> int {
        // IsEquippedAction(slot) → boolean (true if action corresponds to an equipped item)
        int32 Slot = static_cast<int32>(luaL_checknumber(L2, 1)) - 1; // Convert to 0-based
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144) {
            const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
            if (Slot < ActionButtons.Num()) {
                uint32 Data = ActionButtons[Slot];
                uint8 Type = (Data >> 24) & 0xFF;
                uint32 ItemId = Data & 0x00FFFFFF;
                lua_pushboolean(L2, Type == 128 && Ctx->ConnectionManager->IsItemEntryEquipped(ItemId));
            } else {
                lua_pushboolean(L2, false);
            }
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "ScrollFrame_OnScrollRangeChanged", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "IsConsumableAction", [](lua_State* L2) -> int {
        // IsConsumableAction(slot) → boolean (true if action is a consumable)
        int32 Slot = static_cast<int32>(luaL_checknumber(L2, 1)) - 1; // Convert to 0-based
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144) {
            const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
            if (Slot < ActionButtons.Num()) {
                uint32 Data = ActionButtons[Slot];
                uint8 Type = (Data >> 24) & 0xFF;
                // Item actions (type 0x80): consumables show their carried count.
                const FWowItemTemplateInfo* Info = Type == 128 ? Ctx->ConnectionManager->PacketHandler.ItemTemplates.Find(Data & 0x00FFFFFF) : nullptr;
                lua_pushboolean(L2, Info && (Info->Class == 0 || Info->MaxStack > 1));
            } else {
                lua_pushboolean(L2, false);
            }
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "IsStackableAction", [](lua_State* L2) -> int {
        // IsStackableAction(slot) → boolean (true if action is stackable)
        int32 Slot = static_cast<int32>(luaL_checknumber(L2, 1)) - 1; // Convert to 0-based
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144) {
            const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
            if (Slot < ActionButtons.Num()) {
                uint32 Data = ActionButtons[Slot];
                uint8 Type = (Data >> 24) & 0xFF;
                const FWowItemTemplateInfo* Info = Type == 128 ? Ctx->ConnectionManager->PacketHandler.ItemTemplates.Find(Data & 0x00FFFFFF) : nullptr;
                lua_pushboolean(L2, Info && Info->MaxStack > 1);
            } else {
                lua_pushboolean(L2, false);
            }
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "IsItemAction", [](lua_State* L2) -> int {
        // IsItemAction(slot) → boolean (true if action is an item)
        int32 Slot = static_cast<int32>(luaL_checknumber(L2, 1)) - 1; // Convert to 0-based
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144) {
            const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
            if (Slot < ActionButtons.Num()) {
                uint32 Data = ActionButtons[Slot];
                uint8 Type = (Data >> 24) & 0xFF;
                lua_pushboolean(L2, Type == 128); // ACTION_BUTTON_TYPE_ITEM
            } else {
                lua_pushboolean(L2, false);
            }
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "GetActionBarToggles", [](lua_State* L2) -> int {
        SyncActionBarGlobals(L2);
        for(int32 I=3;I<=6;++I)
        {
            if(FWowClientPreferences::GetWorldOption(FString::Printf(TEXT("actionbar%d"),I))) lua_pushboolean(L2,1);
            else lua_pushnil(L2);
        }
        return 4;
    });
    lua_register(L, "SetActionBarToggles", [](lua_State* L2) -> int {
        for (int32 I = 1; I <= 4; ++I)
            FWowClientPreferences::SetWorldOption(FString::Printf(TEXT("actionbar%d"), I + 2), ActionBarToggleEnabled(L2, I));
        // The stock options panel passes Always Show as a fifth argument.
        if (!lua_isnone(L2, 5))
            FWowClientPreferences::SetWorldOption(TEXT("alwaysshowactionbars"), ActionBarToggleEnabled(L2, 5));
        SyncActionBarGlobals(L2);
        return 0;
    });

    lua_register(L, "MoneyFrame_Update", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "IsAutoRepeatAction", [](lua_State* L2) -> int {
        // IsAutoRepeatAction(slot) → true while an auto-repeat spell (Auto Shot, Shoot, Throw)
        // in that slot is repeating. Not tracked yet: false. (It used to answer true for slot 1,
        // which kept the yellow "checked" border on action button 1 all the time.)
        luaL_checknumber(L2, 1);
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "IsAutoRepeatSpell", [](lua_State* L2) -> int {
        // IsAutoRepeatSpell(slot) → boolean (true if spell is auto-repeat like Shoot)
        int32 Slot = static_cast<int32>(luaL_checknumber(L2, 1)) - 1; // Convert to 0-based
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144) {
            const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
            if (Slot < ActionButtons.Num()) {
                uint32 Data = ActionButtons[Slot];
                uint8 Type = (Data >> 24) & 0xFF;
                uint32 SpellId = Data & 0x00FFFFFF;
                // TODO: Check if spell is auto-repeat (like hunter Auto Shot)
                lua_pushboolean(L2, false);
            } else {
                lua_pushboolean(L2, false);
            }
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "OffhandHasWeapon", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->EntityManager)
        {
            FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
            if (Player)
            {
                // Check offhand equipment slot (slot 16 in WoW)
                uint64 OffhandGuid = Player->GetEquipmentItemGuid(PlayerField::EQUIPMENT_SLOT_OFFHAND);
                if (OffhandGuid != 0)
                {
                    // Find the item entity and check if it's a weapon
                    FWowEntity* ItemEntity = Ctx->EntityManager->Find(OffhandGuid);
                    if (ItemEntity && ItemEntity->IsItem())
                    {
                        // Would need item template to determine weapon type
                        // For now, assume any offhand item could be a weapon
                        lua_pushboolean(L2, 1);
                        return 1;
                    }
                }
            }
        }
        lua_pushboolean(L2, 0);
        return 1;
    });
    lua_register(L, "GetActionText", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        int32 Slot = static_cast<int32>(luaL_checknumber(L2, 1)) - 1; // Convert to 0-based

        if (Ctx && Ctx->ConnectionManager && Slot >= 0 && Slot < 144)
        {
            const TArray<uint32>& ActionButtons = Ctx->ConnectionManager->PacketHandler.ActionButtons;
            if (Slot < ActionButtons.Num() && ActionButtons[Slot] != 0)
            {
                uint32 ActionData = ActionButtons[Slot];
                uint8 ActionType = (ActionData >> 24) & 0xFF;
                uint32 ActionId = ActionData & 0x00FFFFFF;

                if (ActionType == 0) // Spell action
                {
                    // For spells, return empty string (spells don't have text)
                    lua_pushstring(L2, "");
                    return 1;
                }
                else if (ActionType == 64) // Macro action: its name under the button, like the client
                {
                    const FWowMacro* Macro = FWowMacros::Get(int32(ActionId));
                    lua_pushstring(L2, Macro ? TCHAR_TO_UTF8(*Macro->Name) : "");
                    return 1;
                }
            }
        }
        lua_pushstring(L2, "");
        return 1;
    });
    lua_register(L, "GetNumVoiceSessionMembersBySessionID", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });

    // Priority 4 - Additional missing API functions that cause Lua errors
    lua_register(L, "GetSpellLink", [](lua_State* L2) -> int {
        const uint32 SpellId = ResolveSpellArgument(L2);
        
        // Look up spell name from SpellDbc
        const FSpellDbcEntry* SpellEntry = FDbcStore::Get().Spells().GetById(SpellId);
        if (SpellEntry)
        {
            // Construct WoW spell link: |cff71d5ff|Hspell:ID|h[SpellName]|h|r
            FString SpellLink = FString::Printf(TEXT("|cff71d5ff|Hspell:%d|h[%s]|h|r"), 
                SpellId, *SpellEntry->SpellName);
            lua_pushstring(L2, TCHAR_TO_UTF8(*SpellLink));
            return 1;
        }
        lua_pushnil(L2);
        return 1;
    });
    lua_register(L, "GetTradeSkillLine", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetAuctionItemInfo", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "CanInspect", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "InspectUnit", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "CheckInteractDistance", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "GetGuildBankMoney", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "IsSpellKnown", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        int32 SpellId = static_cast<int32>(luaL_checknumber(L2, 1));

        if (Ctx && Ctx->ConnectionManager && SpellId > 0)
        {
            const TSet<uint32>& KnownSpells = Ctx->ConnectionManager->PacketHandler.KnownSpells;
            bool bKnown = KnownSpells.Contains(static_cast<uint32>(SpellId));
            lua_pushboolean(L2, bKnown ? 1 : 0);
            return 1;
        }

        lua_pushboolean(L2, 0);
        return 1;
    });
    lua_register(L, "GetSpellCharges", [](lua_State* L2) -> int {
        // Spell charges system doesn't exist in WotLK (3.3.5)
        lua_pushnil(L2);
        return 1;
    });
    lua_register(L, "GetItemCooldown", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0); lua_pushnumber(L2, 0); lua_pushnumber(L2, 0);
        return 3;
    });
    lua_register(L, "GetInventoryItemCount", [](lua_State* L2) -> int {
        FWowEntity* Entity = ResolveUnit(L2, 1);
        int32 SlotIndex = static_cast<int32>(luaL_optinteger(L2, 2, 0));

        if (!Entity || !Entity->IsPlayer())
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        FWowPlayerEntity* Player = static_cast<FWowPlayerEntity*>(Entity);

        // WoW's public inventory API uses paper-doll slot IDs 1-19.
        const int32 EquipmentSlot = NormalizeEquipmentInventorySlot(SlotIndex);
        if (EquipmentSlot != INDEX_NONE)
        {
            uint64 ItemGuid = Player->GetEquipmentItemGuid(EquipmentSlot);
            if (ItemGuid != 0)
            {
                lua_pushnumber(L2, 1); // Equipped items count as 1
                return 1;
            }
        }
        // r282: bag, backpack, bank and bank bag slots (20-74) give the stack size (BankFrame counts).
        if (SlotIndex >= 20 && SlotIndex <= 74)
        {
            uint64 ItemGuid = 0;
            FWowItemEntity* Item = nullptr;
            if (ResolveInventoryItem(WowLuaApi::GetContext(L2), SlotIndex, ItemGuid, Item) && Item)
            {
                lua_pushnumber(L2, FMath::Max<int32>(1, static_cast<int32>(Item->GetStackCount())));
                return 1;
            }
        }

        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetInventoryItemQuality", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        FWowEntity* Entity = ResolveUnit(L2, 1);
        int32 SlotIndex = static_cast<int32>(luaL_optinteger(L2, 2, 0));

        if (!Ctx || !Ctx->EntityManager || !Entity || !Entity->IsPlayer())
        {
            lua_pushnil(L2);
            return 1;
        }

        FWowPlayerEntity* Player = static_cast<FWowPlayerEntity*>(Entity);

        const int32 EquipmentSlot = NormalizeEquipmentInventorySlot(SlotIndex);
        if (EquipmentSlot != INDEX_NONE)
        {
            uint64 ItemGuid = Player->GetEquipmentItemGuid(EquipmentSlot);
            if (ItemGuid != 0)
            {
                // Try to find the item entity to get its quality
                FWowEntity* ItemEntity = Ctx->EntityManager->Find(ItemGuid);
                if (ItemEntity && ItemEntity->IsItem())
                {
                    // Quality is typically stored in item fields or we default to common
                    lua_pushnumber(L2, 1); // 1 = common (white)
                    return 1;
                }
                else
                {
                    // No item entity found but slot has item, default quality
                    lua_pushnumber(L2, 1); // 1 = common (white)
                    return 1;
                }
            }
        }

        lua_pushnil(L2);
        return 1;
    });
    lua_register(L, "IsEquippedItemType", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "GetShapeshiftFormCooldown", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0); lua_pushnumber(L2, 0); lua_pushnumber(L2, 1);
        return 3;
    });
    lua_register(L, "UnitThreatSituation", [](lua_State* L2) -> int {
        // UnitThreatSituation(unit, otherUnit) → threatStatus (0=none, 1=low, 2=medium, 3=high)
        // For now, return 0 (no threat)
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetThreatStatusColor", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0); lua_pushnumber(L2, 1); lua_pushnumber(L2, 0);
        return 3;
    });
    lua_register(L, "UnitDetailedThreatSituation", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false); lua_pushnumber(L2, 0); lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0); lua_pushnumber(L2, 0);
        return 5;
    });
    lua_register(L, "GetPetActionInfo", L_GetPetActionInfo);
    lua_register(L, "GetPetActionsUsable", L_GetPetActionsUsable);
    lua_register(L, "UnitInRange", [](lua_State* L2) -> int {
        // UnitInRange(unit) → boolean (true if unit is in range)
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Entity) {
            // TODO: Calculate actual distance based on spell ranges, interaction distance, etc.
            // For now, return true (assume in range)
            lua_pushboolean(L2, true);
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "SpellIsTargeting", [](lua_State* L2) -> int {
        // SpellIsTargeting() → boolean (true if in targeting mode)
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "SpellStopCasting", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager) {
            const uint64 LocalGuid = Ctx->ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
            const FWowUnitSpellProgress* Progress = Ctx->UnitSpellProgress.Find(LocalGuid);
            // CastId currently stores the spell ID, not the wire cast counter.
            // ID 0 also cancels a pending cast before SPELL_START arrives.
            Ctx->ConnectionManager->SendCancelCast(Progress ? Progress->CastId : 0,
                Progress && Progress->bChanneling);
        }
        return 0;
    });
    // BuffButton_OnClick (right-click a buff): CancelUnitBuff(unit, index|name[, rank|filter]).
    // Only your own helpful auras can be cancelled; CMSG_CANCEL_AURA 0x136 carries the spell id.
    {
        auto CancelBuff = [](lua_State* L2) -> int {
            FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
            if (!Ctx || !Ctx->ConnectionManager || !Ctx->EntityManager) return 0;
            // CancelUnitBuff(unit, ...) vs CancelPlayerBuff(index|name, ...).
            const bool bHasUnit = lua_type(L2, 1) == LUA_TSTRING && lua_gettop(L2) >= 2
                && (lua_type(L2, 2) == LUA_TNUMBER || lua_type(L2, 2) == LUA_TSTRING);
            const int32 Base = bHasUnit ? 1 : 0;
            if (bHasUnit)
            {
                const FString Unit = UTF8_TO_TCHAR(lua_tostring(L2, 1));
                if (!Unit.Equals(TEXT("player"), ESearchCase::IgnoreCase) && !Unit.Equals(TEXT("vehicle"), ESearchCase::IgnoreCase))
                    return 0;
            }
            const FWowEntity* Player = Ctx->EntityManager->Find(Ctx->EntityManager->LocalPlayerGuid);
            if (!Player || lua_gettop(L2) < Base + 1) return 0;
            const bool bByName = lua_type(L2, Base + 1) == LUA_TSTRING && !lua_isnumber(L2, Base + 1);
            int32 Index = bByName ? 1 : static_cast<int32>(lua_tointeger(L2, Base + 1));
            const FString Name = bByName ? UTF8_TO_TCHAR(lua_tostring(L2, Base + 1)) : FString();
            const FString Filter = !bByName && lua_type(L2, Base + 2) == LUA_TSTRING ? UTF8_TO_TCHAR(lua_tostring(L2, Base + 2)) : FString();
            if (Filter.Contains(TEXT("HARMFUL"))) return 0;
            for (const FAuraInfo& Aura : Player->Auras)
            {
                if (!Aura.bActive || !Aura.SpellId) continue;
                const bool bPositive = (Aura.Flags & 0x10) != 0 && (Aura.Flags & 0x80) == 0;
                if (!bPositive) continue;
                const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(Aura.SpellId);
                if (!Spell || (Spell->AttributesEx & 0x10000000)) continue; // same list UnitBuff shows
                if (bByName ? !Spell->SpellName.Equals(Name) : --Index != 0) continue;
                TArray<uint8> Data;
                Data.Append(reinterpret_cast<const uint8*>(&Aura.SpellId), 4);
                Ctx->ConnectionManager->SendRawPacket(0x0136, Data);
                break;
            }
            return 0;
        };
        lua_register(L, "CancelUnitBuff", CancelBuff);
        lua_register(L, "CancelPlayerBuff", CancelBuff);
    }
    // r375 (Stefan 16.10: right-click should remove weapon enchants such as poisons, if the server
    // allows): CancelItemTempEnchantment(1 = main hand, 2 = off hand) sends
    // CMSG_CANCEL_TEMP_ENCHANTMENT (0x379) with the equipment slot (15 / 16); the server removes it.
    lua_register(L, "CancelItemTempEnchantment", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const int32 Hand = static_cast<int32>(luaL_optinteger(L2, 1, 1));
        if (!Ctx || !Ctx->ConnectionManager || (Hand != 1 && Hand != 2)) return 0;
        const uint32 EquipmentSlot = Hand == 1 ? 15u : 16u;
        TArray<uint8> Data;
        Data.Append(reinterpret_cast<const uint8*>(&EquipmentSlot), 4);
        Ctx->ConnectionManager->SendRawPacket(0x0379, Data);
        return 0;
    });
    lua_register(L, "SpellStopTargeting", [](lua_State* L2) -> int {
        // Cancel targeting mode - no-op for now but accept silently
        UE_LOG(LogWowLuaStub, Verbose, TEXT("SpellStopTargeting: stopping targeting mode"));
        return 0;
    });
    lua_register(L, "GetTotemInfo", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false); lua_pushstring(L2, ""); lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0); lua_pushnil(L2);
        return 5;
    });
    lua_register(L, "GetRuneCooldown", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0); lua_pushnumber(L2, 0); lua_pushboolean(L2, true);
        return 3;
    });
    lua_register(L, "GetRuneType", [](lua_State* L2) -> int { lua_pushnumber(L2, 1); return 1; });
    // GetMinimapZoneText — real implementation registered earlier (L_GetMinimapZoneText)
    lua_register(L, "SetMapToCurrentZone", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetPlayerMapPosition2", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0); lua_pushnumber(L2, 0);
        return 2;
    });
    // UI panel toggle functions — toggle FrameXML frames by name
    auto ToggleFrameByName = [](lua_State* L2, const char* FrameName) {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        if (!C || !C->FrameManager) return;
        int64 H = C->FrameManager->FindFrame(FString(FrameName));
        if (H >= 0)
        {
            bool bVis = C->FrameManager->IsFrameVisible(H);
            C->FrameManager->SetFrameVisible(H, !bVis);
            UE_LOG(LogWowLuaStub, Log, TEXT("Toggle %s: %s"), UTF8_TO_TCHAR(FrameName), bVis ? TEXT("Hide") : TEXT("Show"));
        }
    };
    lua_register(L, "ToggleCharacter", [](lua_State* L2) -> int {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        if (C && C->FrameManager) {
            int64 H = C->FrameManager->FindFrame(TEXT("CharacterFrame"));
            if (H >= 0) { bool v = C->FrameManager->IsFrameVisible(H); C->FrameManager->SetFrameVisible(H, !v); }
        }
        return 0;
    });
    lua_register(L, "ToggleSpellBook", L_ToggleSpellBook);
    lua_register(L, "ToggleTalentFrame", [](lua_State* L2) -> int {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        // TalentMicroButton / addons: same path as the N binding (native talent window,
        // locked below SHOW_TALENT_LEVEL 10 like UIParent.lua's ToggleTalentFrame).
        if (C && C->RunBinding) { C->RunBinding(TEXT("TOGGLETALENTS"), true); return 0; }
        if (C && C->FrameManager) {
            int64 H = C->FrameManager->FindFrame(TEXT("PlayerTalentFrame"));
            if (H >= 0) { bool v = C->FrameManager->IsFrameVisible(H); C->FrameManager->SetFrameVisible(H, !v); }
        }
        return 0;
    });
    lua_register(L, "ToggleFriendsFrame", [](lua_State* L2) -> int {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        if (C && C->FrameManager) {
            int64 H = C->FrameManager->FindFrame(TEXT("FriendsFrame"));
            if (H >= 0) { bool v = C->FrameManager->IsFrameVisible(H); C->FrameManager->SetFrameVisible(H, !v); }
        }
        return 0;
    });
    lua_register(L, "ToggleQuestLog", [](lua_State* L2) -> int {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        if(C && C->RunBinding){C->RunBinding(TEXT("TOGGLEQUESTLOG"),true);return 0;}
        if (C && C->FrameManager) {
            int64 H = C->FrameManager->FindFrame(TEXT("QuestLogFrame"));
            if (H >= 0) { bool v = C->FrameManager->IsFrameVisible(H); C->FrameManager->SetFrameVisible(H, !v); }
        }
        return 0;
    });
    lua_register(L, "ToggleWorldMap", [](lua_State* L2) -> int { auto* C=WowLuaApi::GetContext(L2); if(C && C->RunBinding) C->RunBinding(TEXT("TOGGLEWORLDMAP"),true); return 0; });
    lua_register(L, "OpenWorldMap", [](lua_State* L2) -> int { auto* C=WowLuaApi::GetContext(L2); if(C && C->RunBinding) C->RunBinding(TEXT("OPENWORLDMAP"),true); return 0; });
    lua_register(L, "CloseWorldMap", [](lua_State* L2) -> int { auto* C=WowLuaApi::GetContext(L2); if(C && C->RunBinding) C->RunBinding(TEXT("CLOSEWORLDMAP"),true); return 0; });
    lua_register(L, "GetCoinText", [](lua_State* L2) -> int { lua_pushstring(L2, "0g"); return 1; });
    // ShowUIPanel(frame) / HideUIPanel(frame) — THE core panel management functions
    // Every WoW UI panel (spellbook, character, quest log etc.) uses these
    lua_register(L, "ShowUIPanel", [](lua_State* L2) -> int {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        if (!C || !C->FrameManager || !lua_istable(L2, 1)) return 0;
        lua_getfield(L2, 1, "__handle");
        if (lua_isnumber(L2, -1)) {
            int64 H = static_cast<int64>(lua_tointeger(L2, -1));
            C->FrameManager->SetFrameVisible(H, true);
        }
        lua_pop(L2, 1);
        // Also call frame:Show() via Lua so scripts fire
        lua_getfield(L2, 1, "Show");
        if (lua_isfunction(L2, -1)) {
            lua_pushvalue(L2, 1);
            lua_pcall(L2, 1, 0, 0);
        } else {
            lua_pop(L2, 1);
        }
        return 0;
    });
    lua_register(L, "HideUIPanel", [](lua_State* L2) -> int {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        if (!C || !C->FrameManager || !lua_istable(L2, 1)) return 0;
        lua_getfield(L2, 1, "__handle");
        if (lua_isnumber(L2, -1)) {
            int64 H = static_cast<int64>(lua_tointeger(L2, -1));
            C->FrameManager->SetFrameVisible(H, false);
        }
        lua_pop(L2, 1);
        // Also call frame:Hide() via Lua so scripts fire
        lua_getfield(L2, 1, "Hide");
        if (lua_isfunction(L2, -1)) {
            lua_pushvalue(L2, 1);
            lua_pcall(L2, 1, 0, 0);
        } else {
            lua_pop(L2, 1);
        }
        return 0;
    });
    lua_register(L, "GetUIPanel", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "CloseMenus", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "AcceptGroup", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "DeclineGroup", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "UnitIsUnit", [](lua_State* L2) -> int {
        const char* u1 = luaL_optstring(L2, 1, "");
        const char* u2 = luaL_optstring(L2, 2, "");
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const uint64 Guid1 = ResolveUnitGuidToken(Ctx, UTF8_TO_TCHAR(u1));
        const uint64 Guid2 = ResolveUnitGuidToken(Ctx, UTF8_TO_TCHAR(u2));
        lua_pushboolean(L2, Guid1 != 0 && Guid1 == Guid2);
        return 1;
    });
    lua_register(L, "UnitIsGroupLeader", [](lua_State* L2) -> int {
        // UnitIsGroupLeader(unit) → boolean (true if unit is group leader)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Ctx && Ctx->ConnectionManager && Entity) {
            const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
            lua_pushboolean(L2, Entity->Guid == GroupInfo.LeaderGuid);
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "UnitIsPartyLeader", [](lua_State* L2) -> int {
        // UnitIsPartyLeader(unit) → boolean (WoW 3.3.5 compatibility)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Ctx && Ctx->ConnectionManager && Entity) {
            const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
            lua_pushboolean(L2, GroupInfo.IsPartyGroup() && Entity->Guid == GroupInfo.LeaderGuid);
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "UnitIsGroupAssistant", [](lua_State* L2) -> int {
        // UnitIsGroupAssistant(unit) → boolean (true if unit is group assistant)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        FWowEntity* Entity = ResolveUnit(L2, 1);
        if (Ctx && Ctx->ConnectionManager && Entity) {
            const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
            // TODO: Check if unit is an assistant (when that data is available)
            lua_pushboolean(L2, false);
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "UnitReaction", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->EntityManager)
        {
            lua_pushnumber(L2, 4); // neutral
            return 1;
        }

        FWowEntity* Unit1 = ResolveUnit(L2, 1);
        FWowEntity* Unit2 = ResolveUnit(L2, 2);

        if (!Unit1 || !Unit2)
        {
            lua_pushnumber(L2, 4); // neutral
            return 1;
        }

        lua_pushnumber(L2, GetUnitReactionRank(Ctx, Unit1, Unit2));
        return 1;
    });

    // Party/raid leadership
    lua_register(L, "IsPartyLeader", [](lua_State* L2) -> int {
        // IsPartyLeader() → boolean (true if player is party leader)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Ctx->EntityManager) {
            uint64 PlayerGuid = Ctx->EntityManager->LocalPlayerGuid;
            const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
            lua_pushboolean(L2, GroupInfo.IsPartyGroup() && GroupInfo.LocalPlayerIsLeader(PlayerGuid));
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "IsRaidLeader", [](lua_State* L2) -> int {
        // IsRaidLeader() → boolean (true if player is raid leader)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Ctx->EntityManager) {
            uint64 PlayerGuid = Ctx->EntityManager->LocalPlayerGuid;
            const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
            lua_pushboolean(L2, GroupInfo.IsRaidGroup() && GroupInfo.LocalPlayerIsLeader(PlayerGuid));
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "IsRaidOfficer", [](lua_State* L2) -> int {
        // IsRaidOfficer() → boolean (true if player is raid officer)
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Ctx->EntityManager) {
            const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
            lua_pushboolean(L2, GroupInfo.IsRaidGroup() && GroupInfo.LocalPlayerIsAssistant());
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "PromoteToLeader", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "DemoteAssistant", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "UninviteUnit", [](lua_State* L2) -> int { return 0; });
    // Player-frame menu (UnitPopup "SELF"): leave group, reset instances, loot method, difficulty.
    // 3.3.5 opcodes: CMSG_GROUP_DISBAND 0x07B, CMSG_LOOT_METHOD 0x07A, CMSG_RESET_INSTANCES 0x31D,
    // MSG_SET_DUNGEON_DIFFICULTY 0x329, MSG_SET_RAID_DIFFICULTY 0x4EB.
    lua_register(L, "LeaveParty", [](lua_State* L2) -> int {
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L2); Ctx && Ctx->ConnectionManager)
            Ctx->ConnectionManager->SendRawPacket(0x007B);
        return 0;
    });
    lua_register(L, "ResetInstances", [](lua_State* L2) -> int {
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L2); Ctx && Ctx->ConnectionManager)
            Ctx->ConnectionManager->SendRawPacket(0x031D);
        return 0;
    });
    // r356 (Claude-Perf r563): the difficulty lives in FWowDifficultyState (0-based, set by the server's
    // MSG_SET_DUNGEON/RAID_DIFFICULTY too), so the menu, the server and the instance portals agree.
    lua_register(L, "GetDungeonDifficulty", [](lua_State* L2) -> int { lua_pushnumber(L2, FWowDifficultyState::GetDungeon() + 1); return 1; });
    lua_register(L, "GetRaidDifficulty", [](lua_State* L2) -> int { lua_pushnumber(L2, FWowDifficultyState::GetRaid() + 1); return 1; });
    lua_register(L, "SetDungeonDifficulty", [](lua_State* L2) -> int {
        const int32 Value = FMath::Clamp(static_cast<int32>(luaL_checknumber(L2, 1)), 1, 2);
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L2); Ctx && Ctx->ConnectionManager)
        {
            TArray<uint8> Data; const uint32 Difficulty = uint32(Value - 1);
            Data.Append(reinterpret_cast<const uint8*>(&Difficulty), 4);
            Ctx->ConnectionManager->SendRawPacket(0x0329, Data);
        }
        FWowDifficultyState::SetDungeon(Value - 1);
        return 0;
    });
    lua_register(L, "SetRaidDifficulty", [](lua_State* L2) -> int {
        const int32 Value = FMath::Clamp(static_cast<int32>(luaL_checknumber(L2, 1)), 1, 4);
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L2); Ctx && Ctx->ConnectionManager)
        {
            TArray<uint8> Data; const uint32 Difficulty = uint32(Value - 1);
            Data.Append(reinterpret_cast<const uint8*>(&Difficulty), 4);
            Ctx->ConnectionManager->SendRawPacket(0x04EB, Data);
        }
        FWowDifficultyState::SetRaid(Value - 1);
        return 0;
    });
    lua_register(L, "ConvertToRaid", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ConvertToParty", [](lua_State* L2) -> int { return 0; });
#define WOW_LOOT_METHOD_NAMES {TEXT("freeforall"), TEXT("roundrobin"), TEXT("master"), TEXT("group"), TEXT("needbeforegreed")}
    lua_register(L, "SetLootMethod", [](lua_State* L2) -> int {
        // SetLootMethod("freeforall"|"roundrobin"|"master"|"group"|"needbeforegreed"[, masterName, threshold])
        const FString Method = UTF8_TO_TCHAR(luaL_optstring(L2, 1, "group"));
        static const TCHAR* const Methods[] = WOW_LOOT_METHOD_NAMES; // macro: braces with commas break lua_register
        uint32 MethodId = 3;
        for (uint32 I = 0; I < UE_ARRAY_COUNT(Methods); ++I) if (Method.Equals(Methods[I], ESearchCase::IgnoreCase)) MethodId = I;
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->ConnectionManager) return 0;
        uint64 MasterGuid = 0;
        if (MethodId == 2)
            if (const FWowPlayerEntity* Me = Ctx->ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer()) MasterGuid = Me->Guid;
        const uint32 Threshold = 2; // uncommon
        TArray<uint8> Data;
        Data.Append(reinterpret_cast<const uint8*>(&MethodId), 4);
        Data.Append(reinterpret_cast<const uint8*>(&MasterGuid), 8);
        Data.Append(reinterpret_cast<const uint8*>(&Threshold), 4);
        Ctx->ConnectionManager->SendRawPacket(0x007A, Data);
        return 0;
    });
    lua_register(L, "GetLootMethod", [](lua_State* L2) -> int { lua_pushstring(L2, "freeforall"); lua_pushnil(L2); lua_pushnil(L2); return 3; });
    lua_register(L, "GetLootThreshold", [](lua_State* L2) -> int { lua_pushnumber(L2, 2); return 1; });
    lua_register(L, "SetLootThreshold", [](lua_State* L2) -> int { return 0; });

    // Missing global functions from remaining errors
    lua_register(L, "GetGlobalString", [](lua_State* L2) -> int {
        const char* key = luaL_optstring(L2, 1, "");
        lua_getglobal(L2, key);
        if (lua_isnil(L2, -1)) { lua_pop(L2, 1); lua_pushstring(L2, key); }
        return 1;
    });
    lua_register(L, "IsReferAFriendLinked", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "GetOptOutOfLoot", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "HasLFGRestrictions", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "CanChangePlayerDifficulty", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "GetInstanceInfo", [](lua_State* L2) -> int {
        lua_pushnil(L2);             // name
        lua_pushstring(L2, "none");  // instanceType
        lua_pushnumber(L2, 1);       // difficulty
        lua_pushstring(L2, "");      // difficultyName
        lua_pushnumber(L2, 0);       // maxPlayers
        lua_pushnumber(L2, 0);       // playerDifficulty
        lua_pushboolean(L2, false);  // isDynamicInstance
        return 7;
    });
    lua_register(L, "GetSummonFriendCooldown", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        return 2;
    });
    lua_register(L, "CanSummonFriend", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "CanGrantLevel", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "SummonFriend", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GrantLevel", [](lua_State* L2) -> int { return 0; });

    // Missing functions from error logs
    lua_register(L, "UnitInBattleground", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "Sound_ChatSystem_GetNumInputDrivers", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "Sound_ChatSystem_GetInputDriverNameByIndex", [](lua_State* L2) -> int { lua_pushstring(L2, "Default"); return 1; });
    lua_register(L, "GetRefreshRates", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 60);
        return 1;
    });
    lua_register(L, "GetMultisampleFormats", [](lua_State* L2) -> int {
        // Blizzard expects colorBits, depthBits, sampleCount triples.
        lua_pushnumber(L2, 24);
        lua_pushnumber(L2, 24);
        lua_pushnumber(L2, 1);
        return 3;
    });
    lua_register(L, "GetLFGProposal", L_GetLFGProposal);
    lua_register(L, "GetVoiceCurrentSessionID", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetVoiceSessionMemberInfoBySessionID", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "VoiceChat_Toggle", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "IsVoiceChatAllowedByServer", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });

    // LFG/LFR functions
    lua_register(L, "GetLFGMode", L_GetLFGMode);
    lua_register(L, "GetLFGRoles", L_GetLFGRoles);
    lua_register(L, "SetLFGRoles", L_SetLFGRoles);
    lua_register(L, "GetLFGQueueStats", L_GetLFGQueueStats);
    lua_register(L, "GetLFGProposalMember", L_GetLFGProposalMember);
    lua_register(L, "AcceptProposal", L_AcceptProposal);
    lua_register(L, "RejectProposal", L_RejectProposal);
    lua_register(L, "GetLFGDungeonInfo", L_GetLFGDungeonInfo);
    lua_register(L, "LFGGetDungeonInfoByID", L_LFGGetDungeonInfoByID);
    lua_register(L, "GetLFDChoiceInfo", L_GetLFDChoiceInfo);
    lua_register(L, "GetLFDChoiceOrder", L_GetLFDChoiceOrder);
    lua_register(L, "GetLFRChoiceOrder", L_GetLFRChoiceOrder);
    lua_register(L, "GetLFDChoiceCollapseState", L_GetLFDChoiceCollapseState);
    lua_register(L, "GetLFDChoiceEnabledState", L_GetLFDChoiceEnabledState);
    lua_register(L, "GetLFDChoiceLockedState", L_GetLFDChoiceLockedState);
    lua_register(L, "GetLFGQueuedList", L_GetLFGQueuedList);
    lua_register(L, "ClearAllLFGDungeons", L_ClearAllLFGDungeons);
    lua_register(L, "SetLFGDungeon", L_SetLFGDungeon);
    lua_register(L, "SetLFGDungeonEnabled", [](lua_State*) -> int { return 0; });
    lua_register(L, "SetLFGHeaderCollapsed", [](lua_State*) -> int { return 0; });
    lua_register(L, "JoinLFG", L_JoinLFG);
    lua_register(L, "LeaveLFG", L_LeaveLFG);
    lua_register(L, "SetLFGComment", L_SetLFGComment);
    lua_register(L, "GetLFGComment", L_GetLFGComment);
    lua_register(L, "IsInLFGDungeon", L_IsInLFGDungeon);
    lua_register(L, "GetNumRandomDungeons", L_GetNumRandomDungeons);
    lua_register(L, "GetLFGRandomDungeonInfo", L_GetLFGRandomDungeonInfo);
    lua_register(L, "IsLFGDungeonJoinable", L_IsLFGDungeonJoinable);
    lua_register(L, "GetRandomDungeonBestChoice", L_GetRandomDungeonBestChoice);
    lua_register(L, "GetLFGRoleUpdate", L_GetLFGRoleUpdate);
    lua_register(L, "GetLFGRoleUpdateSlot", L_GetLFGRoleUpdateSlot);
    lua_register(L, "CompleteLFGRoleCheck", L_CompleteLFGRoleCheck);
    lua_register(L, "RequestLFDPlayerLockInfo", L_RequestLFDPlayerLockInfo);
    lua_register(L, "RequestLFDPartyLockInfo", L_RequestLFDPartyLockInfo);
    lua_register(L, "GetAvailableRoles", [](lua_State* L2) -> int {
        lua_pushboolean(L2, true);
        lua_pushboolean(L2, true);
        lua_pushboolean(L2, true);
        return 3;
    });
    lua_register(L, "CanPartyLFGBackfill", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "GetBattlefieldStatus", [](lua_State* L2) -> int { lua_pushstring(L2, "none"); return 1; });

    // Tutorial
    lua_register(L, "GetTutorialsEnabled", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "SetTutorialsEnabled", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ClearTutorials", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "TriggerTutorial", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "IsTutorialFlagged", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "FlagTutorial", [](lua_State* L2) -> int { return 0; });

    // Missing video/display
    lua_register(L, "GetMaxMultisampleFormat", [](lua_State* L2) -> int { lua_pushnumber(L2, 1); return 1; });
    lua_register(L, "RestartGx", [](lua_State* L2) -> int { return 0; });

    // BattleNet
    lua_register(L, "BNGetNumFriendInvites", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "BNGetNumFriends", L_BNGetNumFriends);
    lua_register(L, "BNGetFriendInfo", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "BNGetInfo", [](lua_State* L2) -> int { lua_pushnil(L2); lua_pushnil(L2); lua_pushnil(L2); lua_pushnil(L2); lua_pushnumber(L2, 0); lua_pushnil(L2); lua_pushnil(L2); lua_pushboolean(L2, false); lua_pushnil(L2); return 9; });
    lua_register(L, "BNSendWhisper", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "BNInviteFriend", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "BNSetCustomMessage", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "BNGetFriendInviteInfo", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });

    // Calendar
    lua_register(L, "GetNumPendingCalendarInvites", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });

    // Missing functions that cause OnLoad errors
    lua_register(L, "GetModifiedClick", L_GetModifiedClick);
    lua_register(L, "IsModifiedClick", L_IsModifiedClick);
    lua_register(L, "SetModifiedClick", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GetModifiedClickAction", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetLFGInfoServer", [](lua_State* L2) -> int {
        // Returns: isQueued, bgName, bgID, isRegistered, suspend, queueType
        lua_pushboolean(L2, false);
        lua_pushstring(L2, "");
        lua_pushnumber(L2, 0);
        lua_pushboolean(L2, false);
        lua_pushboolean(L2, false);
        lua_pushstring(L2, "none");
        return 6;
    });
    lua_register(L, "GetVoiceChatMode", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "BNGetNumFriendInvites", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetTrackingTexture", [](lua_State* L2) -> int { lua_pushstring(L2, "Interface\\Minimap\\Tracking\\None"); return 1; });
    lua_register(L, "GetInventorySlotInfo", L_GetInventorySlotInfo);

    // Rune frame (Death Knight)
    lua_register(L, "GetRuneCount", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });

    // Class color sort order table
    lua_register(L, "GetNumClasses", [](lua_State* L2) -> int { lua_pushnumber(L2, 10); return 1; });
    lua_register(L, "GetClassInfo", L_GetClassInfo);

    // Remaining missing functions from OnLoad errors
    lua_register(L, "IsPartyLFG", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager)
        {
            lua_pushboolean(L2, Ctx->ConnectionManager->PacketHandler.GroupInfo.IsLfgGroup());
        }
        else
        {
            lua_pushboolean(L2, false);
        }
        return 1;
    });
    lua_register(L, "BNGetFriendInfo", [](lua_State* L2) -> int {
        // Returns multiple values: presenceID, presenceName, battleTag, isBattleTagPresence, toonName, toonID, client, isOnline, lastOnline, isAFK, isDND, messageText, noteText, isRIDFriend, broadcastTime, canSoR
        for (int i = 0; i < 16; i++) lua_pushnil(L2);
        return 16;
    });
    lua_register(L, "BNGetNumFOF", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetNumDisplayChannels", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetChannelDisplayInfo", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetPendingInviteConfirmations", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });

    // Functions that fix remaining OnLoad errors
    lua_register(L, "IsThreatWarningEnabled", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "GetThreatWarningEnabled", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });

    // UI toggle functions (called from keybinds — FrameXML should define these,
    // but we register fallback stubs in case the FrameXML versions failed to load)
    lua_register(L, "ToggleBackpack", [](lua_State* L2) -> int {
        FWowLuaContext* C = WowLuaApi::GetContext(L2);
        if (C && C->FrameManager) {
            int64 BackpackFrame = C->FrameManager->FindFrame(TEXT("ContainerFrame1"));
            if (BackpackFrame >= 0) {
                const bool bShowBackpack = !C->FrameManager->IsFrameVisible(BackpackFrame);
                for (int32 ContainerIndex = 1; ContainerIndex <= 13; ++ContainerIndex) {
                    int64 ContainerFrame = C->FrameManager->FindFrame(
                        FString::Printf(TEXT("ContainerFrame%d"), ContainerIndex));
                    if (ContainerFrame >= 0) {
                        C->FrameManager->SetFrameVisible(ContainerFrame, false);
                    }
                }
                if (bShowBackpack) {
                    C->FrameManager->SetFrameVisible(BackpackFrame, true);
                }
                C->FrameManager->SanitizeBackpackOnlyContainers();
                UE_LOG(LogWowLuaStub, Log, TEXT("ToggleBackpack: %s"), bShowBackpack ? TEXT("Show only backpack") : TEXT("Hide all containers"));
            }
        }
        return 0;
    });
    lua_register(L, "ToggleSpellBook", L_ToggleSpellBook);
    // NOTE: ToggleCharacter is registered earlier with a full implementation that
    // toggles CharacterFrame visibility. Do NOT re-register here.

    // Chat frame utility functions (normally defined in ChatFrame.lua)
    lua_register(L, "ChatEdit_SetLastActiveWindow", [](lua_State* L2) -> int {
        // ChatEdit_SetLastActiveWindow(editBox) - store active chat edit box reference
        if (!lua_isnil(L2, 1)) {
            lua_pushvalue(L2, 1);
            lua_setglobal(L2, "_LastActiveChatEditBox");
            UE_LOG(LogWowLuaStub, Verbose, TEXT("ChatEdit_SetLastActiveWindow: stored active edit box"));
        }
        return 0;
    });
    lua_register(L, "ChatEdit_GetActiveWindow", [](lua_State* L2) -> int {
        // ChatEdit_GetActiveWindow() → editBox or nil
        lua_getglobal(L2, "_LastActiveChatEditBox");
        if (lua_isnil(L2, -1)) {
            // Return ChatFrame1EditBox as fallback
            lua_getglobal(L2, "ChatFrame1EditBox");
        }
        return 1;
    });
    lua_register(L, "ChatEdit_SetLastTellTarget", [](lua_State* L2) -> int {
        // ChatEdit_SetLastTellTarget(name) - store last whisper target
        const char* Name = luaL_checkstring(L2, 1);
        lua_pushstring(L2, Name);
        lua_setglobal(L2, "_LastTellTarget");
        UE_LOG(LogWowLuaStub, Verbose, TEXT("ChatEdit_SetLastTellTarget: %s"), UTF8_TO_TCHAR(Name));
        return 0;
    });
	    lua_register(L, "FCF_GetCurrentChatFrame", [](lua_State* L2) -> int {
	        lua_getglobal(L2, "ChatFrame1");
	        return 1;
	    });
	    lua_register(L, "SetChatWindowShown", [](lua_State* L2) -> int {
	        luaL_optinteger(L2, 1, 1);
	        lua_toboolean(L2, 2);
	        return 0;
	    });
	    lua_register(L, "FCFDock_GetChatFrames", [](lua_State* L2) -> int {
	        lua_newtable(L2);
	        return 1;
	    });
    lua_register(L, "GetVoiceChatMode", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });

    // Critical functions needed by FrameXML Lua files during load
    lua_register(L, "FillLocalizedClassList", L_FillLocalizedClassList);
    lua_register(L, "RegisterStaticConstants", [](lua_State* L2) -> int { return 0; });

    // Trade system
    lua_register(L, "GetCursorMoney", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetPlayerTradeMoney", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetTargetTradeMoney", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetTradePlayerItemLink", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetTradeTargetItemLink", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetTradePlayerItemInfo", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetTradeTargetItemInfo", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetNumTradeItems", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });

    // Container/item operations
    lua_register(L, "PickupContainerItem", L_PickupContainerItem);
    lua_register(L, "UseContainerItem", L_UseContainerItem);
    lua_register(L, "SplitContainerItem", L_SplitContainerItem);
    lua_register(L, "PickupInventoryItem", L_PickupInventoryItem);
    lua_register(L, "UseInventoryItem", L_UseInventoryItem);
    lua_register(L, "AutoEquipCursorItem", L_AutoEquipCursorItem);
    lua_register(L, "PutItemInBackpack", L_PutItemInBackpack);
    lua_register(L, "PutItemInBag", L_PutItemInBag);
    lua_register(L, "BankButtonIDToInvSlotID", L_BankButtonIDToInvSlotID); // r282
    lua_register(L, "GetBankSlotCost", L_GetBankSlotCost);
    lua_register(L, "PurchaseSlot", L_PurchaseSlot);
    lua_register(L, "CloseBankFrame", L_CloseBankFrame);
    lua_register(L, "DeleteCursorItem", L_DeleteCursorItem);
    lua_register(L, "EquipPendingItem", [](lua_State* L2) -> int {
        auto* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->ConfirmPendingEquip(static_cast<uint32>(luaL_checknumber(L2, 1)));
        return 0;
    });
    lua_register(L, "CancelPendingEquip", [](lua_State* L2) -> int {
        auto* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->CancelPendingEquip(static_cast<uint32>(lua_tonumber(L2, 1)));
        return 0;
    });

    // Shaman multi-cast bar
    lua_register(L, "GetMultiCastBarOffset", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    // Totem bar (MultiCastActionBarFrame): it only exists once Call of the Elements (level 30)
    // is learned. Without this function MultiCastActionBarFrame_Update stopped with an error
    // before hiding the frame, so its slot buttons showed the whole UI-TotemBar atlas
    // (also inside Bartender4's totem bar). No totem spells per slot yet = no totem bar.
    lua_register(L, "GetMultiCastTotemSpells", [](lua_State*) -> int { return 0; });
    lua_register(L, "SetMultiCastSpell", [](lua_State*) -> int { return 0; });

    // Chat window saved state (persistence)
    lua_register(L, "GetChatWindowSavedDimensions", [](lua_State* L2) -> int {
        // Returns: width, height (WoW units)
        lua_pushnumber(L2, 430); // default chat width
        lua_pushnumber(L2, 120); // default chat height
        return 2;
    });
    lua_register(L, "GetChatWindowSavedPosition", [](lua_State* L2) -> int {
        // WoW 3.3.5: GetChatWindowSavedPosition(index) -> point, xOffset, yOffset, shown
        int32 Index = static_cast<int32>(luaL_optnumber(L2, 1, 1));
        lua_pushstring(L2, "BOTTOMLEFT"); // point
        lua_pushnumber(L2, 32);           // xOffset
        lua_pushnumber(L2, 215);          // yOffset
        lua_pushboolean(L2, Index == 1 ? 1 : 0); // shown (only ChatFrame1 visible by default)
        return 4;
    });
    lua_register(L, "SetChatWindowSavedDimensions", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "SetChatWindowSavedPosition", [](lua_State* L2) -> int { return 0; });

    // Party member lookup
    lua_register(L, "GetPartyMember", [](lua_State* L2) -> int {
        int32 Index = static_cast<int32>(luaL_optnumber(L2, 1, 0));
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager && Index >= 1 && Index <= 4)
        {
            const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
            int32 PartyIdx = Index - 1;
            if (PartyIdx < GroupInfo.Members.Num())
            {
                lua_pushnumber(L2, PartyIdx + 1);
                return 1;
            }
        }
        lua_pushnil(L2);
        return 1;
    });
    lua_register(L, "GetChatTypeIndex", [](lua_State* L2) -> int {
        // GetChatTypeIndex(chatType) -> index
        const char* chatType = luaL_optstring(L2, 1, "");
        // Return a valid index for common chat types
        lua_pushnumber(L2, 1);
        return 1;
    });
    lua_register(L, "GetNumEquipmentSets", [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; });
    lua_register(L, "GetEquipmentSetInfo", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });

    // Ready check
    lua_register(L, "GetReadyCheckStatus", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->ConnectionManager || !Ctx->EntityManager)
        {
            lua_pushnil(L2);
            return 1;
        }

        const uint64 UnitGuid = ResolveUnitGuid(L2, 1);
        if (UnitGuid == 0)
        {
            lua_pushnil(L2);
            return 1;
        }

        const FWowPacketHandler& PacketHandler = Ctx->ConnectionManager->PacketHandler;
        if (!PacketHandler.ReadyCheck.bActive)
        {
            lua_pushnil(L2);
            return 1;
        }

        const bool bIsLocalPlayer = UnitGuid == Ctx->EntityManager->LocalPlayerGuid;
        const FWowGroupMember* Member = bIsLocalPlayer ? nullptr : PacketHandler.GroupInfo.FindMember(UnitGuid);
        const bool bOnline = bIsLocalPlayer
            || (Member && (Member->Status & WowGroupMemberStatus::ONLINE) != 0);

        const uint8* ResponseState = PacketHandler.ReadyCheck.FindResponse(UnitGuid);
        if (!bOnline && ResponseState == nullptr)
        {
            lua_pushstring(L2, "notready");
            return 1;
        }

        if (ResponseState == nullptr)
        {
            lua_pushstring(L2, "waiting");
            return 1;
        }

        lua_pushstring(L2, (*ResponseState == WowReadyCheckResponse::READY) ? "ready" : "notready");
        return 1;
    });
    lua_register(L, "GetReadyCheckTimeLeft", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->ConnectionManager)
        {
            lua_pushnumber(L2, 0);
            return 1;
        }

        lua_pushnumber(L2, Ctx->ConnectionManager->PacketHandler.ReadyCheck.GetTimeLeftSeconds());
        return 1;
    });
    lua_register(L, "GetRaidTargetIndex", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (!Ctx || !Ctx->ConnectionManager)
        {
            lua_pushnil(L2);
            return 1;
        }

        const uint64 UnitGuid = ResolveUnitGuid(L2, 1);
        if (UnitGuid == 0)
        {
            lua_pushnil(L2);
            return 1;
        }

        const int32 IconIndex = Ctx->ConnectionManager->PacketHandler.RaidTargets.GetIconIndex(UnitGuid);
        if (IconIndex == INDEX_NONE)
        {
            lua_pushnil(L2);
            return 1;
        }

        lua_pushnumber(L2, IconIndex + 1);
        return 1;
    });
    lua_register(L, "DoReadyCheck", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager)
        {
            Ctx->ConnectionManager->SendStartReadyCheck();
        }
        return 0;
    });
    lua_register(L, "ConfirmReadyCheck", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager)
        {
            Ctx->ConnectionManager->SendReadyCheckConfirm(lua_toboolean(L2, 1) != 0);
        }
        return 0;
    });

    // Vehicle UI
    lua_register(L, "UnitHasVehicleUI", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "UnitControllingVehicle", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "UnitUsingVehicle", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "CanExitVehicle", [](lua_State* L2) -> int { lua_pushboolean(L2, false); return 1; });
    lua_register(L, "GetVehicleUIIndicator", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    lua_register(L, "GetVehicleUIIndicatorSeat", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });

    // Party leader functions
    lua_register(L, "GetPartyLeaderIndex", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager)
        {
            const FWowGroupInfo& GroupInfo = Ctx->ConnectionManager->PacketHandler.GroupInfo;
            if (!GroupInfo.IsEmpty() && GroupInfo.LeaderGuid != 0)
            {
                // Find the leader in the members array (1-based indexing for Lua)
                for (int32 i = 0; i < GroupInfo.Members.Num(); ++i)
                {
                    if (GroupInfo.Members[i].Guid == GroupInfo.LeaderGuid)
                    {
                        lua_pushnumber(L2, i + 1); // Convert to 1-based
                        return 1;
                    }
                }
            }
        }
        lua_pushnumber(L2, 0); // Not in party or no leader
        return 1;
    });

    // Bank slot functions
    lua_register(L, "GetNumBankSlots", [](lua_State* L2) -> int {
        int32 PurchasedSlots = 0;
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->ConnectionManager)
        {
            if (const FWowPlayerEntity* Player = Ctx->ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
            {
                PurchasedSlots = Player->GetBankBagSlotCount();
            }
        }

        lua_pushnumber(L2, PurchasedSlots);
        lua_pushboolean(L2, PurchasedSlots >= 7);
        return 2;
    });

    // CreateFont function - creates a font object with frame metatable
    lua_register(L, "CreateFont", [](lua_State* L2) -> int {
        const char* fontName = luaL_optstring(L2, 1, "DefaultFont");

        // Create a font table with basic properties
        lua_newtable(L2);

        // Set font name
        lua_pushstring(L2, fontName);
        lua_setfield(L2, -2, "fontName");

        // Add basic font methods (stubs for now)
        lua_pushcfunction(L2, [](lua_State* L3) -> int {
            // SetFont(fontPath, size, flags)
            return 0;
        });
        lua_setfield(L2, -2, "SetFont");

        lua_pushcfunction(L2, [](lua_State* L3) -> int {
            // GetFont() -> fontPath, size, flags
            lua_pushstring(L3, "Fonts\\FRIZQT__.TTF");
            lua_pushnumber(L3, 12);
            lua_pushstring(L3, "");
            return 3;
        });
        lua_setfield(L2, -2, "GetFont");

        return 1;
    });

    // ── Functions needed by OnUpdate/OnEvent handlers (function="..." attribute) ──

    // Achievement system
    lua_register(L, "HasCompletedAnyAchievement", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "GetAchievementInfo", [](lua_State* L2) -> int {
        // Returns: id, name, points, completed, month, day, year, description, flags, icon, rewardText, isGuild, wasEarnedByMe, earnedBy
        lua_pushnumber(L2, luaL_optnumber(L2, 1, 0));
        lua_pushstring(L2, "");
        lua_pushnumber(L2, 0);
        lua_pushboolean(L2, false);
        lua_pushnil(L2); lua_pushnil(L2); lua_pushnil(L2);
        lua_pushstring(L2, "");
        lua_pushnumber(L2, 0);
        lua_pushstring(L2, "Interface\\Icons\\INV_Misc_QuestionMark");
        lua_pushstring(L2, "");
        lua_pushboolean(L2, false);
        lua_pushboolean(L2, false);
        lua_pushnil(L2);
        return 14;
    });

    // Voice chat
    lua_register(L, "VoiceChat_IsRecordingLoopbackSound", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "VoiceChat_IsPlayingLoopbackSound", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "VoiceChat_GetCurrentMicrophoneSignalLevel", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        return 1;
    });

    // Calendar (CalendarGetDate's weekday was Monday = 1; WoW counts from Sunday)
    WowRegisterCalendarApi(L);
    // r240: the server's events, invites, lockouts and raid resets (LuaCalendarApi.cpp).
    WowRegisterCalendarServerApi(L);

    // IsDressableItem(link): wearable gear, or a mount when the Experimental mount preview is on
    // (DressUpItemLink, Ctrl-click). Answered by the dressing room (gameplay controller).
    lua_register(L, "IsDressableItem", [](lua_State* L2) -> int {
        FWowLuaContext* DressCtx = WowLuaApi::GetContext(L2);
        uint32 DressEntry = 0;
        if (lua_type(L2, 1) == LUA_TNUMBER) DressEntry = uint32(lua_tonumber(L2, 1));
        else if (lua_isstring(L2, 1))
        {
            const FString DressLink = UTF8_TO_TCHAR(lua_tostring(L2, 1));
            const int32 DressAt = DressLink.Find(TEXT("item:"));
            DressEntry = uint32(FCString::Atoi(*DressLink.Mid(DressAt >= 0 ? DressAt + 5 : 0)));
        }
        const bool bDressable = DressEntry && DressCtx && DressCtx->FrameManager && DressCtx->FrameManager->DressUpHandler
            && DressCtx->FrameManager->DressUpHandler(TEXT("dressable"), DressEntry, 0.f);
        lua_pushboolean(L2, bDressable ? 1 : 0);
        return 1;
    });

    // Guild
    lua_register(L, "GuildControlGetNumRanks", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GuildControlGetRankName", [](lua_State* L2) -> int {
        lua_pushstring(L2, "");
        return 1;
    });

    // World map
    lua_register(L, "UpdateWorldMapArrowFrames", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "PositionWorldMapArrowFrame", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "ShowWorldMapArrowFrame", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "HideWorldMapArrowFrame", [](lua_State* L2) -> int { return 0; });

    // PVP / Wintergrasp
    lua_register(L, "GetWintergraspWaitTime", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "CanQueueForWintergrasp", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });

    // PVP battlefield
    lua_register(L, "PlayerIsPVPInactive", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "GetNumBattlefieldPositions", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetBattlefieldPosition", [](lua_State* L2) -> int {
        // Returns: x, y
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        return 2;
    });
    lua_register(L, "GetNumBattlefieldFlagPositions", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetBattlefieldFlagPosition", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        lua_pushstring(L2, "");
        return 3;
    });
    lua_register(L, "GetCorpseMapPosition", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        return 2;
    });
    lua_register(L, "GetDeathReleasePosition", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        return 2;
    });
    lua_register(L, "GetCurrentMapContinent", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        return 1;
    });
    lua_register(L, "GetCurrentMapZone", [](lua_State* L2) -> int {
        lua_pushnumber(L2, 0);
        return 1;
    });

    // Container/bag
    lua_register(L, "GetContainerNumFreeSlots", [](lua_State* L2) -> int {
        // Returns: numFreeSlots, bagType
        lua_pushnumber(L2, 0);
        lua_pushnumber(L2, 0);
        return 2;
    });

    // GM ticket system
    lua_register(L, "GetGMTicket", [](lua_State* L2) -> int { return 0; });
    lua_register(L, "GMEuropaBugsEnabled", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });

    // Voice system
    lua_register(L, "GetVoiceStatus", [](lua_State* L2) -> int {
        // Returns: isEnabled, isTalking
        lua_pushboolean(L2, false);
        lua_pushboolean(L2, false);
        return 2;
    });
    lua_register(L, "GetMuteStatus", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "UnitIsTalking", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "UnitIsMuted", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });

    // Spell targeting
    lua_register(L, "SpellCanTargetItem", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "SpellCanTargetUnit", [](lua_State* L2) -> int {
        const auto* Ctx=WowLuaApi::GetContext(L2);
        auto* C=Ctx ? Ctx->ConnectionManager : nullptr;
        const uint64 Guid=ResolveUnitGuid(L2);
        lua_pushboolean(L2,C && C->PendingTargetSpell && Guid && C->ResolveHelpfulSpellTarget(C->PendingTargetSpell,Guid)==int64(Guid));
        return 1;
    });
    lua_register(L, "SpellIsTargeting", [](lua_State* L2) -> int {
        const auto* Ctx=WowLuaApi::GetContext(L2);
        lua_pushboolean(L2,Ctx && Ctx->ConnectionManager && Ctx->ConnectionManager->PendingTargetSpell!=0);
        return 1;
    });
    lua_register(L, "SpellTargetUnit", [](lua_State* L2) -> int {
        const auto* Ctx=WowLuaApi::GetContext(L2);
        if(Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->CompleteSpellTarget(ResolveUnitGuid(L2));
        return 0;
    });
    lua_register(L, "SpellStopTargeting", [](lua_State* L2) -> int {
        const auto* Ctx=WowLuaApi::GetContext(L2);
        if(Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->CancelSpellTarget();
        return 0;
    });

    // Unit state queries
    lua_register(L, "UnitIsCorpse", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "UnitIsGhost", [](lua_State* L2) -> int {
        lua_pushboolean(L2, false);
        return 1;
    });
    lua_register(L, "UnitIsDeadOrGhost", [](lua_State* L2) -> int {
        FWowEntity* Entity = ResolveUnit(L2);
        if (Entity && Entity->IsUnit()) {
            const FWowUnitEntity* Unit = static_cast<const FWowUnitEntity*>(Entity);
            lua_pushboolean(L2, Unit->IsDead() || ShowsAsFeignDead(L2, Entity));
        } else {
            lua_pushboolean(L2, false);
        }
        return 1;
    });

    lua_register(L, "GetWeaponEnchantInfo", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const auto* Player = Ctx && Ctx->EntityManager ? Ctx->EntityManager->GetLocalPlayer() : nullptr;
        for (uint8 Slot : {uint8(15), uint8(16)})
        {
            const auto* Item = Player ? Ctx->EntityManager->FindItem(Player->GetEquipmentItemGuid(Slot)) : nullptr;
            const double Remaining = Item ? Item->GetTempEnchantRemainingMs() : 0;
            const bool Active = Item && Item->GetEnchantmentId(1) && (!Item->bTempEnchantTimed || Remaining > 0);
            lua_pushboolean(L2, Active);
            lua_pushnumber(L2, Active ? Remaining : 0);
            lua_pushinteger(L2, Active ? Item->GetEnchantmentCharges(1) : 0);
        }
        return 6;
    });

    UE_LOG(LogWowLuaStub, Log, TEXT("Registered WoW Lua API (~400+ functions, entity-backed unit API, FrameXML-ready)"));
}

#else
void WowLuaApi::RegisterStubs(lua_State*) {}
#endif
