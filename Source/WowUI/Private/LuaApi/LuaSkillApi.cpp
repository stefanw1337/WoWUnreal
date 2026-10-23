// r341 (Stefan 14.10: "the skills-window needs to be a part of the Tab on character sheet"): the
// 3.3.5 skill API for the stock SkillFrame (CharacterFrame tab "Skills"), over the player's skill
// fields (PLAYER_SKILL_INFO_1_1: 128 skills x 3 fields - id, rank | max << 16, temp | perm bonus)
// and SkillLine.dbc (names, categories, descriptions).
//
// The list is built like the client's: one header per category in the client's order (Class
// Skills, Professions, Secondary Skills, Weapon Skills, Armor Proficiencies, Languages), skills
// by name under each, headers collapsible. SKILL_LINES_CHANGED fires when the fields change.
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntityManager.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "WowEventSystem.h"
#include "WowAddonSettings.h"
#include "WowWorldManager.h"
#include "WowAudioManager.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogWowSkillApi, Log, All);

namespace WowLuaApi { void RegisterSkillApi(lua_State* L); }

#if HAS_LUA
// Named, not anonymous: in a unity build this file shares a translation unit with the other LuaApi files.
namespace WowSkillApiImpl
{
    constexpr uint16 SKILL_FIELD = PlayerField::BASE + 0x1E8;   // PLAYER_SKILL_INFO_1_1 (3.3.5a 12340)
    constexpr uint16 CMSG_UNLEARN_SKILL = 0x202;

    // SkillLine.dbc categories (SkillLineCategory.dbc), in the order the Skills tab shows them.
    struct FCategory { uint32 Id; const char* Name; };
    constexpr FCategory Categories[] = {
        {7, "Class Skills"}, {11, "Professions"}, {9, "Secondary Skills"},
        {6, "Weapon Skills"}, {8, "Armor Proficiencies"}, {10, "Languages"},
    };

    struct FSkillLine { uint32 Category = 0; FString Name, Description; };
    TMap<uint32, FSkillLine> GLines;
    bool bGLoaded = false;

    struct FRow { bool bHeader = false; uint32 Category = 0; uint32 SkillId = 0; uint32 Rank = 0, Max = 0; int32 Temp = 0, Perm = 0; };
    TSet<uint32> GCollapsed;   // collapsed category ids
    int32 GSelected = 0;
    uint32 GSignature = 0;

    FWowPacketHandler* Handler(lua_State* L)
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        return Ctx && Ctx->ConnectionManager ? &Ctx->ConnectionManager->PacketHandler : nullptr;
    }
    void PushString(lua_State* L, const FString& Text) { lua_pushstring(L, TCHAR_TO_UTF8(*Text)); }

    void LoadLines(lua_State* L)
    {
        if (bGLoaded) return;
        FMpqManager* Mpq = FWowAddonSettings::GetMpq();
        if (!Mpq)
        {
            FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
            UWorld* World = Ctx && Ctx->AudioManager ? Ctx->AudioManager->GetWorld() : nullptr;
            const AWowWorldManager* Terrain = World ? Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(World, AWowWorldManager::StaticClass())) : nullptr;
            Mpq = Terrain ? Terrain->GetMpqManager() : nullptr;
        }
        TArray<uint8> Bytes;
        FDbcParser Parser;
        if (!Mpq || !Mpq->ReadFile(TEXT("DBFilesClient\\SkillLine.dbc"), Bytes) || !Parser.Parse(Bytes)) return; // retried next call
        bGLoaded = true;
        if (Parser.GetFieldCount() < 21)
        {
            UE_LOG(LogWowSkillApi, Warning, TEXT("SkillLine.dbc: %u fields, expected 56 (3.3.5a)"), Parser.GetFieldCount());
            return;
        }
        // 3.3.5a SkillLine.dbc: 0 ID, 1 category, 2 SkillCosts id, 3 name (16 locales + mask), 20 description.
        for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
        {
            FSkillLine Line;
            Line.Category = Parser.GetUInt(R, 1);
            Line.Name = Parser.GetLocalizedString(R, 3);
            Line.Description = Parser.GetLocalizedString(R, 20);
            GLines.Add(Parser.GetUInt(R, 0), MoveTemp(Line));
        }
        UE_LOG(LogWowSkillApi, Log, TEXT("SkillLine.dbc: %d skill lines"), GLines.Num());
    }

    const FSkillLine* LineOf(uint32 Id) { return GLines.Find(Id); }

    TArray<FRow> BuildRows(lua_State* L)
    {
        TArray<FRow> Rows;
        FWowPacketHandler* H = Handler(L);
        const FWowPlayerEntity* Me = H ? H->EntityManager.GetLocalPlayer() : nullptr;
        LoadLines(L);
        if (!Me) return Rows;
        TArray<FRow> Skills;
        for (uint32 I = 0; I < 128; ++I)
        {
            const uint16 Base = uint16(SKILL_FIELD + I * 3);
            const uint32 Id = Me->GetField(Base) & 0xffff;
            if (!Id) continue;
            const uint32 Values = Me->GetField(uint16(Base + 1)), Bonus = Me->GetField(uint16(Base + 2));
            FRow Row;
            Row.SkillId = Id;
            const FSkillLine* Line = LineOf(Id);
            Row.Category = Line ? Line->Category : 0;
            Row.Rank = Values & 0xffff;
            Row.Max = Values >> 16;
            Row.Temp = int16(Bonus & 0xffff);
            Row.Perm = int16(Bonus >> 16);
            Skills.Add(Row);
        }
        auto NameOf = [](uint32 Id) { const FSkillLine* Line = LineOf(Id); return Line ? Line->Name : FString(); };
        for (const FCategory& C : Categories)
        {
            TArray<FRow> Members;
            for (const FRow& Row : Skills) if (Row.Category == C.Id && !NameOf(Row.SkillId).IsEmpty()) Members.Add(Row);
            if (!Members.Num()) continue;
            FRow Header; Header.bHeader = true; Header.Category = C.Id;
            Rows.Add(Header);
            if (GCollapsed.Contains(C.Id)) continue;
            Members.Sort([&](const FRow& A, const FRow& B) { return NameOf(A.SkillId) < NameOf(B.SkillId); });
            Rows.Append(Members);
        }
        return Rows;
    }

    const char* CategoryName(uint32 Id)
    {
        for (const FCategory& C : Categories) if (C.Id == Id) return C.Name;
        return "";
    }

    void FireUpdate(lua_State* L)
    {
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem) Ctx->EventSystem->FireEvent(TEXT("SKILL_LINES_CHANGED"));
    }

    // ── API ─────────────────────────────────────────────────────────────────────

    int SK_GetNumSkillLines(lua_State* L) { lua_pushinteger(L, BuildRows(L).Num()); return 1; }

    int SK_GetSkillLineInfo(lua_State* L)
    {
        // skillName, header, isExpanded, skillRank, numTempPoints, skillModifier, skillMaxRank,
        // isAbandonable, stepCost, rankCost, minLevel, skillCostType, skillDescription.
        // SkillFrame adds skillRank + numTempPoints before it checks the name, so an unknown index
        // (GetSelectedSkill() is 0 until a bar is clicked) returns "" and zeros, not nothing.
        const TArray<FRow> Rows = BuildRows(L);
        const int32 Index = int32(luaL_optinteger(L, 1, 0));
        const FRow* Row = Rows.IsValidIndex(Index - 1) ? &Rows[Index - 1] : nullptr;
        if (!Row)
        {
            lua_pushstring(L, ""); lua_pushnil(L); lua_pushnil(L);
            for (int32 I = 0; I < 4; ++I) lua_pushinteger(L, 0);
            lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
            lua_pushinteger(L, 0); lua_pushinteger(L, 0); lua_pushstring(L, "");
            return 13;
        }
        if (Row->bHeader)
        {
            lua_pushstring(L, CategoryName(Row->Category));
            lua_pushinteger(L, 1);
            if (GCollapsed.Contains(Row->Category)) lua_pushnil(L); else lua_pushinteger(L, 1);
            for (int32 I = 0; I < 4; ++I) lua_pushinteger(L, 0);
            lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
            lua_pushinteger(L, 0); lua_pushinteger(L, 0); lua_pushstring(L, "");
            return 13;
        }
        const FSkillLine* Line = LineOf(Row->SkillId);
        PushString(L, Line ? Line->Name : FString());
        lua_pushnil(L);                                  // header
        lua_pushnil(L);                                  // isExpanded
        lua_pushinteger(L, Row->Rank);
        lua_pushinteger(L, 0);                           // numTempPoints (no skill points in 3.3.5)
        lua_pushinteger(L, Row->Temp + Row->Perm);       // skillModifier
        lua_pushinteger(L, Row->Max);
        // Professions and secondary skills can be unlearned (the Unlearn button in the detail pane).
        if (Row->Category == 11 || Row->Category == 9) lua_pushinteger(L, 1); else lua_pushnil(L);
        lua_pushnil(L);                                  // stepCost: not learnable here
        lua_pushnil(L);                                  // rankCost
        lua_pushinteger(L, 0);                           // minLevel
        lua_pushinteger(L, 0);                           // skillCostType
        PushString(L, Line ? Line->Description : FString());
        return 13;
    }

    int SetCollapsed(lua_State* L, bool bCollapse)
    {
        // Index 0 = every header (the "All" button).
        const TArray<FRow> Rows = BuildRows(L);
        const int32 Index = int32(luaL_optinteger(L, 1, 0));
        if (Index == 0)
        {
            if (bCollapse) { for (const FCategory& C : Categories) GCollapsed.Add(C.Id); }
            else { GCollapsed.Reset(); }
        }
        else if (Rows.IsValidIndex(Index - 1) && Rows[Index - 1].bHeader)
        {
            if (bCollapse) GCollapsed.Add(Rows[Index - 1].Category); else GCollapsed.Remove(Rows[Index - 1].Category);
        }
        else
        {
            return 0;
        }
        GSelected = 0;
        FireUpdate(L);
        return 0;
    }
    int SK_ExpandSkillHeader(lua_State* L) { return SetCollapsed(L, false); }
    int SK_CollapseSkillHeader(lua_State* L) { return SetCollapsed(L, true); }

    int SK_SetSelectedSkill(lua_State* L) { GSelected = int32(luaL_optinteger(L, 1, 0)); return 0; }
    int SK_GetSelectedSkill(lua_State* L) { lua_pushinteger(L, GSelected); return 1; }

    int SK_AbandonSkill(lua_State* L)
    {
        const TArray<FRow> Rows = BuildRows(L);
        const int32 Index = int32(luaL_optinteger(L, 1, 0));
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        if (!Ctx || !Ctx->ConnectionManager || !Rows.IsValidIndex(Index - 1) || Rows[Index - 1].bHeader) return 0;
        const uint32 Id = Rows[Index - 1].SkillId;
        TArray<uint8> Data;
        Data.Append(reinterpret_cast<const uint8*>(&Id), 4);
        Ctx->ConnectionManager->SendRawPacket(CMSG_UNLEARN_SKILL, Data);
        UE_LOG(LogWowSkillApi, Log, TEXT("AbandonSkill: unlearn skill %u"), Id);
        GSelected = 0;
        return 0;
    }

    int SK_Tick(lua_State* L)
    {
        // Called a few times a second from the UI tick: SKILL_LINES_CHANGED when the skill fields change.
        FWowPacketHandler* H = Handler(L);
        const FWowPlayerEntity* Me = H ? H->EntityManager.GetLocalPlayer() : nullptr;
        if (!Me) return 0;
        uint32 Hash = 0x5111u;
        for (uint32 I = 0; I < 384; ++I) Hash = HashCombine(Hash, GetTypeHash(Me->GetField(uint16(SKILL_FIELD + I))));
        if (Hash != GSignature)
        {
            GSignature = Hash;
            FireUpdate(L);
        }
        return 0;
    }
}
#endif

void WowLuaApi::RegisterSkillApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    using namespace WowSkillApiImpl;
    lua_register(L, "GetNumSkillLines", SK_GetNumSkillLines);
    lua_register(L, "GetSkillLineInfo", SK_GetSkillLineInfo);
    lua_register(L, "ExpandSkillHeader", SK_ExpandSkillHeader);
    lua_register(L, "CollapseSkillHeader", SK_CollapseSkillHeader);
    lua_register(L, "SetSelectedSkill", SK_SetSelectedSkill);
    lua_register(L, "GetSelectedSkill", SK_GetSelectedSkill);
    lua_register(L, "AbandonSkill", SK_AbandonSkill);
    lua_register(L, "__WowSkillTick", SK_Tick);
    // The UI tick (WowUIManager __WowSanitizeUIPanels) already calls __WowWatchFrameTick a few
    // times a second; chain the skill check onto it rather than adding another C++ call site.
    luaL_dostring(L, R"LUA(
local previous = __WowWatchFrameTick
local registered = false
function __WowWatchFrameTick(...)
    if previous then pcall(previous, ...) end
    -- r346: SkillFrame_OnLoad (function="...") is not run by this client: register its events here.
    if not registered and SkillFrame and SkillFrame.RegisterEvent then
        registered = true
        SkillFrame:RegisterEvent("SKILL_LINES_CHANGED")
        SkillFrame:RegisterEvent("CHARACTER_POINTS_CHANGED")
        SkillFrame.statusBarClickedID = SkillFrame.statusBarClickedID or 0
    end
    pcall(__WowSkillTick)
end
)LUA");
    UE_LOG(LogWowSkillApi, Log, TEXT("Skill Lua API registered"));
#endif
}
