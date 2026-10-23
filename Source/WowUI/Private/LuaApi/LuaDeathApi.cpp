// Death, corpse and resurrection API used by the stock FrameXML StaticPopup dialogs
// (DEATH, RESURRECT*, XP_LOSS*, RECOVER_CORPSE, AREA_SPIRIT_HEAL). The dialogs, texts and
// countdowns are the original UI; this file supplies the client functions behind them.
// State comes from FWowPacketHandler::DeathState, kept by the gameplay controller.
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntityManager.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellDbc.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#define HAS_LUA 1
#else
#define HAS_LUA 0
#endif

#if HAS_LUA
namespace
{
// 3.3.5a opcodes.
constexpr uint32 CMSG_REPOP_REQUEST = 0x015A;
constexpr uint32 CMSG_RESURRECT_RESPONSE = 0x015C;
constexpr uint32 CMSG_RECLAIM_CORPSE = 0x01D2;
constexpr uint32 CMSG_SPIRIT_HEALER_ACTIVATE = 0x021C;
constexpr uint32 CMSG_SELF_RES = 0x02B3;
constexpr uint32 CMSG_AREA_SPIRIT_HEALER_QUEUE = 0x02E3;
constexpr uint32 PLAYER_FLAGS_GHOST = 0x10;
constexpr uint16 PLAYER_SELF_RES_SPELL = PlayerField::BASE + 0x041B;
/** Seconds before an unreleased corpse is released automatically (DEATH dialog countdown). */
constexpr double ReleaseTimeSeconds = 360.0;

FWowDeathState* State(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx && Ctx->ConnectionManager ? &Ctx->ConnectionManager->PacketHandler.DeathState : nullptr;
}

UWowConnectionManager* Connection(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx ? Ctx->ConnectionManager : nullptr;
}

FWowEntity* LocalPlayer(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->EntityManager) return nullptr;
    return Ctx->EntityManager->Find(Ctx->EntityManager->LocalPlayerGuid);
}

void SendGuid(UWowConnectionManager* Connection, uint32 Opcode, uint64 Guid, TOptional<uint8> Tail = {})
{
    if (!Connection) return;
    TArray<uint8> Data;
    Data.Append(reinterpret_cast<const uint8*>(&Guid), 8);
    if (Tail.IsSet()) Data.Add(Tail.GetValue());
    Connection->SendRawPacket(Opcode, Data);
}

/** Resolves a unit token through the existing UnitGUID (party1, target, mouseover ...). */
FWowEntity* ResolveUnitByGuid(lua_State* L, int Index)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->EntityManager || !lua_isstring(L, Index)) return nullptr;
    lua_getglobal(L, "UnitGUID");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return nullptr; }
    lua_pushvalue(L, Index);
    if (lua_pcall(L, 1, 1, 0) != 0) { lua_pop(L, 1); return nullptr; }
    FString Text = lua_isstring(L, -1) ? UTF8_TO_TCHAR(lua_tostring(L, -1)) : FString();
    lua_pop(L, 1);
    if (Text.IsEmpty()) return nullptr;
    Text.RemoveFromStart(TEXT("0x"));
    const uint64 Guid = FCString::Strtoui64(*Text, nullptr, 16);
    return Guid ? Ctx->EntityManager->Find(Guid) : nullptr;
}

void RespondToResurrect(lua_State* L, uint8 Status)
{
    FWowDeathState* Death = State(L);
    if (!Death || !Death->ResurrecterGuid) return;
    SendGuid(Connection(L), CMSG_RESURRECT_RESPONSE, Death->ResurrecterGuid, Status);
    Death->ResurrecterGuid = 0;
}

bool IsGhost(const FWowEntity* Entity)
{
    return Entity && Entity->IsPlayer() && (Entity->GetField(PlayerField::FLAGS) & PLAYER_FLAGS_GHOST) != 0;
}

/** Defines Name only when no other part of the client has registered it yet. */
void RegisterIfMissing(lua_State* L, const char* Name, lua_CFunction Function)
{
    lua_getglobal(L, Name);
    const bool bExists = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!bExists) lua_register(L, Name, Function);
}
}
#endif

void WowLuaApi::RegisterDeathApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;

    lua_register(L, "UnitIsGhost", [](lua_State* L2) -> int {
        lua_pushboolean(L2, IsGhost(ResolveUnitByGuid(L2, 1)));
        return 1;
    });
    // A ghost has health, so the stock "dead" test misses it.
    luaL_dostring(L,
        "local isDead = UnitIsDeadOrGhost\n"
        "if isDead then UnitIsDeadOrGhost = function(unit) return isDead(unit) or UnitIsGhost(unit) end end\n");

    // DEATH dialog: "%d minutes until release", then the client releases by itself.
    lua_register(L, "GetReleaseTimeRemaining", [](lua_State* L2) -> int {
        const FWowDeathState* Death = State(L2);
        double Remaining = 0.0;
        if (Death && Death->bDead && !Death->bGhost)
            Remaining = FMath::Max(0.0, ReleaseTimeSeconds - (FPlatformTime::Seconds() - Death->DeathTime));
        // PLAYER_DEAD reaches UIParent before the death state is recorded; UIParent only shows the
        // DEATH dialog (Release Spirit) when this is above 0 (Stefan 10.10: no release button).
        else if (const FWowEntity* Me = LocalPlayer(L2); Me && Me->IsUnit() && Me->GetHealth() <= 0 && !IsGhost(Me))
            Remaining = ReleaseTimeSeconds;
        lua_pushnumber(L2, FMath::CeilToDouble(Remaining));
        return 1;
    });
    lua_register(L, "RepopMe", [](lua_State* L2) -> int {
        FWowDeathState* Death = State(L2);
        if (UWowConnectionManager* Conn = Connection(L2); Conn && Death && Death->bDead && !Death->bGhost)
        {
            Conn->SendRawPacket(CMSG_REPOP_REQUEST, TArray<uint8>{0});
            Death->bReleaseSent = true;
        }
        return 0;
    });
    // Soulstone / Reincarnation: PLAYER_SELF_RES_SPELL holds the spell the corpse may use.
    lua_register(L, "HasSoulstone", [](lua_State* L2) -> int {
        const FWowEntity* Player = LocalPlayer(L2);
        const uint32 SpellId = Player ? Player->GetField(PLAYER_SELF_RES_SPELL) : 0;
        if (!SpellId) { lua_pushnil(L2); return 1; }
        const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(SpellId);
        const FString Name = Spell ? Spell->SpellName : FString();
        const TCHAR* Text = Name.Equals(TEXT("Reincarnation")) ? TEXT("Reincarnate")
            : Name.Equals(TEXT("Twisting Nether")) ? TEXT("Twisting Nether") : TEXT("Use Soulstone");
        lua_pushstring(L2, TCHAR_TO_UTF8(Text));
        return 1;
    });
    lua_register(L, "UseSoulstone", [](lua_State* L2) -> int {
        if (UWowConnectionManager* Conn = Connection(L2)) Conn->SendRawPacket(CMSG_SELF_RES);
        return 0;
    });
    lua_register(L, "CannotBeResurrected", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });

    // Corpse delay (SMSG_CORPSE_RECLAIM_DELAY) for RECOVER_CORPSE and the resurrect timer text.
    lua_register(L, "GetCorpseRecoveryDelay", [](lua_State* L2) -> int {
        const FWowDeathState* Death = State(L2);
        const double Remaining = Death ? FMath::Max(0.0, Death->ReclaimReadyTime - FPlatformTime::Seconds()) : 0.0;
        lua_pushnumber(L2, FMath::CeilToDouble(Remaining));
        return 1;
    });
    lua_register(L, "RetrieveCorpse", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        if (Ctx && Ctx->EntityManager) SendGuid(Connection(L2), CMSG_RECLAIM_CORPSE, Ctx->EntityManager->LocalPlayerGuid);
        return 0;
    });

    // Resurrection offered by another player (SMSG_RESURRECT_REQUEST).
    lua_register(L, "ResurrectGetOfferer", [](lua_State* L2) -> int {
        const FWowDeathState* Death = State(L2);
        if (Death && Death->ResurrecterGuid) lua_pushstring(L2, TCHAR_TO_UTF8(*Death->ResurrecterName));
        else lua_pushnil(L2);
        return 1;
    });
    lua_register(L, "ResurrectHasSickness", [](lua_State* L2) -> int {
        const FWowDeathState* Death = State(L2);
        lua_pushboolean(L2, Death && Death->bResurrectSickness);
        return 1;
    });
    lua_register(L, "ResurrectHasTimer", [](lua_State* L2) -> int {
        const FWowDeathState* Death = State(L2);
        lua_pushboolean(L2, Death && Death->bResurrectTimer);
        return 1;
    });
    lua_register(L, "AcceptResurrect", [](lua_State* L2) -> int { RespondToResurrect(L2, 1); return 0; });
    lua_register(L, "DeclineResurrect", [](lua_State* L2) -> int { RespondToResurrect(L2, 0); return 0; });

    // Spirit healer (XP_LOSS dialogs after SMSG_SPIRIT_HEALER_CONFIRM). The server applies the
    // durability loss and the Resurrection Sickness aura when CMSG_SPIRIT_HEALER_ACTIVATE arrives.
    lua_register(L, "GetResSicknessDuration", [](lua_State* L2) -> int {
        const FWowEntity* Player = LocalPlayer(L2);
        const int32 Level = Player ? static_cast<int32>(Player->GetField(UnitField::LEVEL)) : 0;
        if (Level <= 10) { lua_pushnil(L2); return 1; }
        const int32 Minutes = Level < 20 ? Level - 10 : 10;
        lua_pushstring(L2, TCHAR_TO_UTF8(*FString::Printf(TEXT("%d %s"), Minutes, Minutes == 1 ? TEXT("minute") : TEXT("minutes"))));
        return 1;
    });
    lua_register(L, "AcceptXPLoss", [](lua_State* L2) -> int {
        FWowDeathState* Death = State(L2);
        if (Death && Death->SpiritHealerGuid) SendGuid(Connection(L2), CMSG_SPIRIT_HEALER_ACTIVATE, Death->SpiritHealerGuid);
        return 0;
    });
    lua_register(L, "CheckSpiritHealerDist", [](lua_State* L2) -> int {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L2);
        const FWowDeathState* Death = State(L2);
        const FWowEntity* Player = LocalPlayer(L2);
        const FWowEntity* Healer = Ctx && Ctx->EntityManager && Death ? Ctx->EntityManager->Find(Death->SpiritHealerGuid) : nullptr;
        // Unknown positions never close the dialog; the server checks the distance again.
        const bool bNear = !Player || !Healer
            || FVector::Dist(Player->Movement.Position, Healer->Movement.Position) <= 20.f;
        lua_pushboolean(L2, bNear);
        return 1;
    });

    // Battleground spirit guide (AREA_SPIRIT_HEAL dialog countdown).
    lua_register(L, "GetAreaSpiritHealerTime", [](lua_State* L2) -> int {
        const FWowDeathState* Death = State(L2);
        const double Remaining = Death ? FMath::Max(0.0, Death->AreaSpiritHealerReadyTime - FPlatformTime::Seconds()) : 0.0;
        lua_pushnumber(L2, FMath::CeilToDouble(Remaining));
        return 1;
    });
    lua_register(L, "AcceptAreaSpiritHeal", [](lua_State* L2) -> int {
        const FWowDeathState* Death = State(L2);
        if (Death && Death->AreaSpiritHealerGuid) SendGuid(Connection(L2), CMSG_AREA_SPIRIT_HEALER_QUEUE, Death->AreaSpiritHealerGuid);
        return 0;
    });
    lua_register(L, "CancelAreaSpiritHeal", [](lua_State* L2) -> int { return 0; });

    RegisterIfMissing(L, "IsActiveBattlefieldArena", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    RegisterIfMissing(L, "IsOutOfBounds", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    RegisterIfMissing(L, "UnitIsControlling", [](lua_State* L2) -> int { lua_pushnil(L2); return 1; });
    RegisterIfMissing(L, "CloseGossip", [](lua_State* L2) -> int { return 0; });
#endif
}
