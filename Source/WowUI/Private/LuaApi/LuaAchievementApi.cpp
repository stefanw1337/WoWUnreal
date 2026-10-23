// r342 (Stefan 14.10: "an overhaul of the Achievement window is needed" - he chose the stock
// Blizzard_AchievementUI): the 3.3.5 achievement API behind the original AchievementFrame, over the
// server's data (FWowPacketHandler::CompletedAchievements / AchievementCriteria, from
// SMSG_ALL_ACHIEVEMENT_DATA, SMSG_ACHIEVEMENT_EARNED, SMSG_CRITERIA_UPDATE) and the client DBCs
// (Achievement, Achievement_Category, Achievement_Criteria, SpellIcon).
//
// Lists are built like the client's: per category the completed achievements first, then the
// incomplete ones, both in the DBC's order; of a chain (Level 10 -> 20 -> ...) only the first
// unfinished step shows (the last one when all are done); hidden achievements and Feats of
// Strength only once earned; achievements of the other faction never. Statistics are the
// achievements flagged as statistic, under the Statistics category's children.
// Events: ACHIEVEMENT_EARNED (id) for new completions, CRITERIA_UPDATE for progress.
// r343: tracked achievements are kept per character (tracked-achievements.txt in the character's WTF
// folder; the WoW client's own trackedAchievements setting is read when that file does not exist yet)
// and are listed at the bottom of the WatchFrame, under the quests (Stefan 14.10).
// Not done yet: comparison with another player (INSPECT_ACHIEVEMENTS).
#include "LuaApiRegistry.h"
#include "WowSavedVariables.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogWowAchievementApi, Log, All);

namespace WowLuaApi { void RegisterAchievementApi(lua_State* L); }

#if HAS_LUA
// Named, not anonymous: in a unity build this file shares a translation unit with the other LuaApi files.
namespace WowAchievementApiImpl
{
    constexpr uint32 FLAG_STATISTIC = 0x01, FLAG_HIDDEN = 0x02, FLAG_STORE_MAX = 0x04, FLAG_MAX_USED = 0x10;
    constexpr uint32 CRITERIA_PROGRESS_BAR = 0x01, CRITERIA_HIDDEN = 0x02, CRITERIA_MONEY = 0x20;
    constexpr uint32 CRITERIA_TYPE_ACHIEVEMENT = 8;
    constexpr uint32 STATISTICS_ROOT = 1, FEATS_OF_STRENGTH = 81;

    struct FAchievement
    {
        uint32 Id = 0; int32 Faction = -1; uint32 Previous = 0, Next = 0, Category = 0, Points = 0, Order = 0, Flags = 0,
            Icon = 0, Count = 0, Ref = 0;
        FString Name, Description, Reward;
    };
    struct FCategory { uint32 Id = 0; int32 Parent = -1; uint32 Order = 0; FString Name; };
    struct FCriteria { uint32 Id = 0, Achievement = 0, Type = 0, Asset = 0, Quantity = 0, Flags = 0, Order = 0; FString Name; };

    TMap<uint32, FAchievement> GAchievements;
    TMap<uint32, FCategory> GCategories;
    TMap<uint32, FCriteria> GCriteria;
    TMap<uint32, TArray<uint32>> GCriteriaOf;      // achievement -> criteria ids, in show order
    TMap<uint32, TArray<uint32>> GByCategory;      // category -> achievement ids, in DBC order
    TMap<uint32, FString> GIcons;
    bool bGLoaded = false;

    TArray<uint32> GTracked;                       // AddTrackedAchievement (the WatchFrame's list)
    FString GTrackedOwner;                         // character the list belongs to ("" = not loaded)
    TArray<uint32> GRecentStats;                   // statistics updated this session, newest first
    TMap<uint32, uint64> GSeenCriteria;
    TSet<uint32> GSeenCompleted;
    uint32 GSeenRevision = MAX_uint32;
    bool bGSeenOnce = false;

    FWowPacketHandler* Handler(lua_State* L)
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        return Ctx && Ctx->ConnectionManager ? &Ctx->ConnectionManager->PacketHandler : nullptr;
    }
    void PushString(lua_State* L, const FString& Text) { lua_pushstring(L, TCHAR_TO_UTF8(*Text)); }

    FMpqManager* FindMpq(lua_State* L)
    {
        FMpqManager* Mpq = FWowAddonSettings::GetMpq();
        if (Mpq) return Mpq;
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        UWorld* World = Ctx && Ctx->AudioManager ? Ctx->AudioManager->GetWorld() : nullptr;
        const AWowWorldManager* Terrain = World ? Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(World, AWowWorldManager::StaticClass())) : nullptr;
        return Terrain ? Terrain->GetMpqManager() : nullptr;
    }

    bool ReadDbc(FMpqManager& Mpq, const TCHAR* Path, FDbcParser& Parser, uint32 MinFields)
    {
        TArray<uint8> Bytes;
        if (!Mpq.ReadFile(Path, Bytes) || !Parser.Parse(Bytes)) return false;
        if (Parser.GetFieldCount() < MinFields)
        {
            UE_LOG(LogWowAchievementApi, Warning, TEXT("%s: %u fields, expected at least %u"), Path, Parser.GetFieldCount(), MinFields);
            return false;
        }
        return true;
    }

    void Load(lua_State* L)
    {
        if (bGLoaded) return;
        FMpqManager* Mpq = FindMpq(L);
        if (!Mpq) return; // retried next call
        bGLoaded = true;
        FDbcParser Parser;
        // 3.3.5a Achievement.dbc: 0 ID, 1 faction (-1 both, 0 Horde, 1 Alliance), 2 map, 3 previous,
        // 4 name, 21 description, 38 category, 39 points, 40 order, 41 flags, 42 icon, 43 reward,
        // 60 criteria count, 61 achievement whose criteria it shares.
        if (ReadDbc(*Mpq, TEXT("DBFilesClient\\Achievement.dbc"), Parser, 62))
        {
            for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
            {
                FAchievement A;
                A.Id = Parser.GetUInt(R, 0);
                A.Faction = static_cast<int32>(Parser.GetUInt(R, 1));
                A.Previous = Parser.GetUInt(R, 3);
                A.Name = Parser.GetLocalizedString(R, 4);
                A.Description = Parser.GetLocalizedString(R, 21);
                A.Category = Parser.GetUInt(R, 38);
                A.Points = Parser.GetUInt(R, 39);
                A.Order = Parser.GetUInt(R, 40);
                A.Flags = Parser.GetUInt(R, 41);
                A.Icon = Parser.GetUInt(R, 42);
                A.Reward = Parser.GetLocalizedString(R, 43);
                A.Count = Parser.GetUInt(R, 60);
                A.Ref = Parser.GetUInt(R, 61);
                GAchievements.Add(A.Id, MoveTemp(A));
            }
        }
        // Achievement_Category.dbc: 0 ID, 1 parent (-1 = top), 2 name, 19 sort order.
        if (ReadDbc(*Mpq, TEXT("DBFilesClient\\Achievement_Category.dbc"), Parser, 20))
        {
            for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
            {
                FCategory C;
                C.Id = Parser.GetUInt(R, 0);
                C.Parent = static_cast<int32>(Parser.GetUInt(R, 1));
                C.Name = Parser.GetLocalizedString(R, 2);
                C.Order = Parser.GetUInt(R, 19);
                GCategories.Add(C.Id, MoveTemp(C));
            }
        }
        // Achievement_Criteria.dbc: 0 ID, 1 achievement, 2 type, 3 asset, 4 quantity, 9 name,
        // 26 flags (progress bar, hidden, money ...), 30 show order.
        if (ReadDbc(*Mpq, TEXT("DBFilesClient\\Achievement_Criteria.dbc"), Parser, 31))
        {
            for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
            {
                FCriteria C;
                C.Id = Parser.GetUInt(R, 0);
                C.Achievement = Parser.GetUInt(R, 1);
                C.Type = Parser.GetUInt(R, 2);
                C.Asset = Parser.GetUInt(R, 3);
                C.Quantity = Parser.GetUInt(R, 4);
                C.Name = Parser.GetLocalizedString(R, 9);
                C.Flags = Parser.GetUInt(R, 26);
                C.Order = Parser.GetUInt(R, 30);
                GCriteriaOf.FindOrAdd(C.Achievement).Add(C.Id);
                GCriteria.Add(C.Id, MoveTemp(C));
            }
        }
        if (ReadDbc(*Mpq, TEXT("DBFilesClient\\SpellIcon.dbc"), Parser, 2))
        {
            for (uint32 R = 0; R < Parser.GetRecordCount(); ++R) GIcons.Add(Parser.GetUInt(R, 0), Parser.GetString(R, 1));
        }
        for (auto& Pair : GCriteriaOf)
        {
            Pair.Value.Sort([](uint32 A, uint32 B)
            {
                const FCriteria& CA = GCriteria[A];
                const FCriteria& CB = GCriteria[B];
                return CA.Order != CB.Order ? CA.Order < CB.Order : A < B;
            });
        }
        for (auto& Pair : GAchievements)
        {
            if (FAchievement* Prev = Pair.Value.Previous ? GAchievements.Find(Pair.Value.Previous) : nullptr) Prev->Next = Pair.Key;
            GByCategory.FindOrAdd(Pair.Value.Category).Add(Pair.Key);
        }
        for (auto& Pair : GByCategory)
        {
            Pair.Value.Sort([](uint32 A, uint32 B)
            {
                const FAchievement& AA = GAchievements[A];
                const FAchievement& AB = GAchievements[B];
                return AA.Order != AB.Order ? AA.Order < AB.Order : A < B;
            });
        }
        UE_LOG(LogWowAchievementApi, Log, TEXT("Achievement DBCs: %d achievements, %d categories, %d criteria, %d icons"),
            GAchievements.Num(), GCategories.Num(), GCriteria.Num(), GIcons.Num());
    }

    // ── State helpers ───────────────────────────────────────────────────────────

    bool IsCompleted(lua_State* L, uint32 Id)
    {
        const FWowPacketHandler* H = Handler(L);
        return H && H->CompletedAchievements.Contains(Id);
    }

    /** 0 Horde, 1 Alliance, -1 unknown. */
    int32 PlayerTeam(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        const FWowPlayerEntity* Me = H ? H->EntityManager.GetLocalPlayer() : nullptr;
        if (!Me) return -1;
        const uint8 Race = Me->GetRaceId();
        return (Race == 2 || Race == 5 || Race == 6 || Race == 8 || Race == 10) ? 0 : 1;
    }

    bool ForMyFaction(lua_State* L, const FAchievement& A)
    {
        const int32 Team = PlayerTeam(L);
        return A.Faction < 0 || Team < 0 || A.Faction == Team;
    }

    bool IsStatisticCategory(uint32 Id)
    {
        for (int32 Guard = 0; Id && Guard < 8; ++Guard)
        {
            if (Id == STATISTICS_ROOT) return true;
            const FCategory* C = GCategories.Find(Id);
            if (!C || C->Parent < 0) return false;
            Id = uint32(C->Parent);
        }
        return false;
    }

    /** Achievements of a category as the list shows them (completed first); bAll = every one, chains included. */
    TArray<uint32> CategoryList(lua_State* L, uint32 Category, bool bAll)
    {
        TArray<uint32> Done, Open;
        const TArray<uint32>* Ids = GByCategory.Find(Category);
        if (!Ids) return Done;
        for (uint32 Id : *Ids)
        {
            const FAchievement& A = GAchievements[Id];
            if (!ForMyFaction(L, A)) continue;
            const bool bDone = IsCompleted(L, Id);
            const bool bStatistic = (A.Flags & FLAG_STATISTIC) != 0;
            if (!bStatistic)
            {
                if (!bDone && ((A.Flags & FLAG_HIDDEN) || Category == FEATS_OF_STRENGTH)) continue;
                if (!bAll)
                {
                    // One step of a chain: shown when the step before it is done, and not when it is
                    // done itself while a further step exists.
                    const FAchievement* Next = A.Next ? GAchievements.Find(A.Next) : nullptr;
                    const bool bNextShown = Next && Next->Category == A.Category && ForMyFaction(L, *Next);
                    if (A.Previous && GAchievements.Contains(A.Previous) && !IsCompleted(L, A.Previous)) continue;
                    if (bDone && bNextShown) continue;
                }
            }
            (bDone && !bStatistic ? Done : Open).Add(Id);
        }
        Done.Append(Open);
        return Done;
    }

    /** Criteria of an achievement as shown (hidden ones left out). */
    TArray<uint32> CriteriaList(uint32 Id)
    {
        TArray<uint32> Out;
        const FAchievement* A = GAchievements.Find(Id);
        const TArray<uint32>* Ids = GCriteriaOf.Find(A && A->Ref ? A->Ref : Id);
        if (!Ids) return Out;
        for (uint32 C : *Ids) if (!(GCriteria[C].Flags & CRITERIA_HIDDEN)) Out.Add(C);
        return Out;
    }

    uint64 Counter(lua_State* L, uint32 Criteria)
    {
        const FWowPacketHandler* H = Handler(L);
        const uint64* Value = H ? H->AchievementCriteria.Find(Criteria) : nullptr;
        return Value ? *Value : 0;
    }

    bool CriteriaDone(lua_State* L, const FCriteria& C, uint32 OwnerAchievement)
    {
        if (C.Type == CRITERIA_TYPE_ACHIEVEMENT) return IsCompleted(L, C.Asset);
        if (IsCompleted(L, OwnerAchievement)) return true;
        return Counter(L, C.Id) >= FMath::Max<uint64>(C.Quantity, 1);
    }

    FString MoneyText(uint64 Copper)
    {
        const uint64 Gold = Copper / 10000, Silver = (Copper / 100) % 100, Cop = Copper % 100;
        FString Text;
        if (Gold) Text += FString::Printf(TEXT("%llu|TInterface\\MoneyFrame\\UI-GoldIcon:0:0:2:0|t "), Gold);
        if (Gold || Silver) Text += FString::Printf(TEXT("%llu|TInterface\\MoneyFrame\\UI-SilverIcon:0:0:2:0|t "), Silver);
        Text += FString::Printf(TEXT("%llu|TInterface\\MoneyFrame\\UI-CopperIcon:0:0:2:0|t"), Cop);
        return Text;
    }

    FString IconPath(uint32 Icon)
    {
        const FString* Path = GIcons.Find(Icon);
        return Path && !Path->IsEmpty() ? *Path : FString(TEXT("Interface\\Icons\\INV_Misc_QuestionMark"));
    }

    /** Packed date (minutes, hours, weekday, day-1, month-1, year-2000) -> month, day, year (2 digits). */
    void UnpackDate(uint32 Packed, int32& Month, int32& Day, int32& Year)
    {
        Day = int32((Packed >> 14) & 0x3F) + 1;
        Month = int32((Packed >> 20) & 0x0F) + 1;
        Year = int32((Packed >> 24) & 0x1F);
    }
    /** Sort key for "latest": year, month, day, hour, minute. */
    uint32 DateKey(uint32 Packed)
    {
        return (((Packed >> 24) & 0x1F) << 24) | (((Packed >> 20) & 0x0F) << 20) | (((Packed >> 14) & 0x3F) << 14)
            | (((Packed >> 6) & 0x1F) << 6) | (Packed & 0x3F);
    }

    int PushAchievement(lua_State* L, uint32 Id)
    {
        // id, name, points, completed, month, day, year, description, flags, icon, rewardText
        const FAchievement* A = GAchievements.Find(Id);
        if (!A) return 0;
        const FWowPacketHandler* H = Handler(L);
        const uint32* Packed = H ? H->CompletedAchievements.Find(Id) : nullptr;
        lua_pushinteger(L, A->Id);
        PushString(L, A->Name);
        lua_pushinteger(L, A->Points);
        lua_pushboolean(L, Packed != nullptr);
        if (Packed)
        {
            int32 Month = 0, Day = 0, Year = 0;
            UnpackDate(*Packed, Month, Day, Year);
            lua_pushinteger(L, Month); lua_pushinteger(L, Day); lua_pushinteger(L, Year);
        }
        else
        {
            lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
        }
        PushString(L, A->Description);
        lua_pushinteger(L, A->Flags);
        PushString(L, IconPath(A->Icon));
        PushString(L, A->Reward);
        return 11;
    }

    FString StatisticText(lua_State* L, uint32 Id)
    {
        const FAchievement* A = GAchievements.Find(Id);
        const TArray<uint32>* Ids = GCriteriaOf.Find(A && A->Ref ? A->Ref : Id);
        if (!A || !Ids) return TEXT("--");
        uint64 Value = 0;
        bool bAny = false, bMoney = false;
        for (uint32 C : *Ids)
        {
            const uint64* Count = nullptr;
            if (const FWowPacketHandler* H = Handler(L)) Count = H->AchievementCriteria.Find(C);
            if (!Count) continue;
            bAny = true;
            bMoney |= (GCriteria[C].Flags & CRITERIA_MONEY) != 0;
            Value = (A->Flags & (FLAG_STORE_MAX | FLAG_MAX_USED)) ? FMath::Max(Value, *Count) : Value + *Count;
        }
        if (!bAny) return TEXT("--");
        return bMoney ? MoneyText(Value) : FString::Printf(TEXT("%llu"), Value);
    }

    // ── Categories ──────────────────────────────────────────────────────────────

    TArray<uint32> Categories(bool bStatistics)
    {
        TArray<uint32> Out;
        for (const auto& Pair : GCategories)
        {
            if (Pair.Key == STATISTICS_ROOT) continue;
            if (IsStatisticCategory(Pair.Key) == bStatistics) Out.Add(Pair.Key);
        }
        Out.Sort([](uint32 A, uint32 B)
        {
            const FCategory& CA = GCategories[A];
            const FCategory& CB = GCategories[B];
            return CA.Order != CB.Order ? CA.Order < CB.Order : A < B;
        });
        return Out;
    }

    int PushIdTable(lua_State* L, const TArray<uint32>& Ids)
    {
        lua_createtable(L, Ids.Num(), 0);
        for (int32 I = 0; I < Ids.Num(); ++I) { lua_pushinteger(L, Ids[I]); lua_rawseti(L, -2, I + 1); }
        return 1;
    }

    int AC_GetCategoryList(lua_State* L) { Load(L); return PushIdTable(L, Categories(false)); }
    int AC_GetStatisticsCategoryList(lua_State* L) { Load(L); return PushIdTable(L, Categories(true)); }

    int AC_GetCategoryInfo(lua_State* L)
    {
        // name, parentID, flags. The Statistics tab shows the Statistics category's children as top level.
        Load(L);
        const FCategory* C = GCategories.Find(uint32(luaL_optinteger(L, 1, 0)));
        if (!C) return 0;
        PushString(L, C->Name);
        lua_pushinteger(L, C->Parent == int32(STATISTICS_ROOT) ? -1 : C->Parent);
        lua_pushinteger(L, 0);
        return 3;
    }

    int AC_GetCategoryNumAchievements(lua_State* L)
    {
        // numItems, numCompleted (includeAll: every step of the chains, not just the shown one)
        Load(L);
        const uint32 Category = uint32(luaL_optinteger(L, 1, 0));
        const bool bAll = lua_toboolean(L, 2) != 0;
        const TArray<uint32> Ids = CategoryList(L, Category, bAll);
        int32 Done = 0;
        for (uint32 Id : Ids) if (IsCompleted(L, Id) && !(GAchievements[Id].Flags & FLAG_STATISTIC)) ++Done;
        lua_pushinteger(L, Ids.Num());
        lua_pushinteger(L, Done);
        return 2;
    }

    int AC_GetNumCompletedAchievements(lua_State* L)
    {
        // total, completed - every achievement of your faction (not statistics, not Feats of Strength).
        Load(L);
        int32 Total = 0, Done = 0;
        for (const auto& Pair : GAchievements)
        {
            const FAchievement& A = Pair.Value;
            if ((A.Flags & FLAG_STATISTIC) || A.Category == FEATS_OF_STRENGTH || !GCategories.Contains(A.Category) || !ForMyFaction(L, A)) continue;
            const bool bDone = IsCompleted(L, A.Id);
            if (!bDone && (A.Flags & FLAG_HIDDEN)) continue;
            ++Total;
            if (bDone) ++Done;
        }
        lua_pushinteger(L, Total);
        lua_pushinteger(L, Done);
        return 2;
    }

    int AC_GetTotalAchievementPoints(lua_State* L)
    {
        Load(L);
        const FWowPacketHandler* H = Handler(L);
        int64 Points = 0;
        if (H) for (const auto& Pair : H->CompletedAchievements) if (const FAchievement* A = GAchievements.Find(Pair.Key)) Points += A->Points;
        lua_pushinteger(L, lua_Integer(Points));
        return 1;
    }

    // ── Achievements ────────────────────────────────────────────────────────────

    int AC_GetAchievementInfo(lua_State* L)
    {
        // GetAchievementInfo(id) or GetAchievementInfo(category, index)
        Load(L);
        if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
        {
            const TArray<uint32> Ids = CategoryList(L, uint32(luaL_optinteger(L, 1, 0)), false);
            const int32 Index = int32(luaL_optinteger(L, 2, 0));
            return Ids.IsValidIndex(Index - 1) ? PushAchievement(L, Ids[Index - 1]) : 0;
        }
        return PushAchievement(L, uint32(luaL_optinteger(L, 1, 0)));
    }

    int AC_GetAchievementCategory(lua_State* L)
    {
        Load(L);
        const FAchievement* A = GAchievements.Find(uint32(luaL_optinteger(L, 1, 0)));
        if (!A) return 0;
        lua_pushinteger(L, A->Category);
        return 1;
    }

    int AC_GetPreviousAchievement(lua_State* L)
    {
        Load(L);
        const FAchievement* A = GAchievements.Find(uint32(luaL_optinteger(L, 1, 0)));
        if (!A || !A->Previous || !GAchievements.Contains(A->Previous)) return 0;
        lua_pushinteger(L, A->Previous);
        return 1;
    }

    int AC_GetNextAchievement(lua_State* L)
    {
        // nextID, completed
        Load(L);
        const FAchievement* A = GAchievements.Find(uint32(luaL_optinteger(L, 1, 0)));
        if (!A || !A->Next) return 0;
        lua_pushinteger(L, A->Next);
        lua_pushboolean(L, IsCompleted(L, A->Next));
        return 2;
    }

    int AC_GetAchievementNumCriteria(lua_State* L)
    {
        Load(L);
        lua_pushinteger(L, CriteriaList(uint32(luaL_optinteger(L, 1, 0))).Num());
        return 1;
    }

    int PushCriteria(lua_State* L, const FCriteria& C, uint32 Owner)
    {
        // criteriaString, criteriaType, completed, quantity, reqQuantity, charName, flags, assetID, quantityString, criteriaID
        const bool bDone = CriteriaDone(L, C, Owner);
        uint64 Quantity = Counter(L, C.Id);
        if (C.Type == CRITERIA_TYPE_ACHIEVEMENT) Quantity = bDone ? 1 : 0;
        else if (bDone && C.Quantity && Quantity < C.Quantity) Quantity = C.Quantity;
        FString Name = C.Name;
        if (Name.IsEmpty() && C.Type == CRITERIA_TYPE_ACHIEVEMENT)
            if (const FAchievement* Asset = GAchievements.Find(C.Asset)) Name = Asset->Name;
        PushString(L, Name);
        lua_pushinteger(L, C.Type);
        lua_pushboolean(L, bDone);
        lua_pushinteger(L, lua_Integer(Quantity));
        lua_pushinteger(L, C.Quantity);
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        PushString(L, Ctx && Ctx->ConnectionManager ? Ctx->ConnectionManager->GetCharacterName() : FString());
        lua_pushinteger(L, C.Flags);
        lua_pushinteger(L, C.Asset);
        if (C.Flags & CRITERIA_MONEY)
            PushString(L, MoneyText(Quantity) + TEXT(" / ") + MoneyText(C.Quantity));
        else
            PushString(L, FString::Printf(TEXT("%llu / %u"), Quantity, C.Quantity));
        lua_pushinteger(L, C.Id);
        return 10;
    }

    int AC_GetAchievementCriteriaInfo(lua_State* L)
    {
        // GetAchievementCriteriaInfo(achievementID, index) or GetAchievementCriteriaInfo(criteriaID)
        Load(L);
        if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
        {
            const uint32 Id = uint32(luaL_optinteger(L, 1, 0));
            const TArray<uint32> Ids = CriteriaList(Id);
            const int32 Index = int32(luaL_optinteger(L, 2, 0));
            return Ids.IsValidIndex(Index - 1) ? PushCriteria(L, GCriteria[Ids[Index - 1]], Id) : 0;
        }
        const FCriteria* C = GCriteria.Find(uint32(luaL_optinteger(L, 1, 0)));
        return C ? PushCriteria(L, *C, C->Achievement) : 0;
    }

    int AC_GetAchievementInfoFromCriteria(lua_State* L)
    {
        Load(L);
        const FCriteria* C = GCriteria.Find(uint32(luaL_optinteger(L, 1, 0)));
        return C ? PushAchievement(L, C->Achievement) : 0;
    }

    int AC_GetStatistic(lua_State* L)
    {
        Load(L);
        PushString(L, StatisticText(L, uint32(luaL_optinteger(L, 1, 0))));
        return 1;
    }

    int AC_GetLatestCompletedAchievements(lua_State* L)
    {
        Load(L);
        const FWowPacketHandler* H = Handler(L);
        TArray<TPair<uint32, uint32>> List;   // date key, id
        if (H)
            for (const auto& Pair : H->CompletedAchievements)
                if (const FAchievement* A = GAchievements.Find(Pair.Key); A && !(A->Flags & FLAG_STATISTIC))
                    List.Add(TPair<uint32, uint32>(DateKey(Pair.Value), Pair.Key));
        List.Sort([](const TPair<uint32, uint32>& A, const TPair<uint32, uint32>& B) { return A.Key != B.Key ? A.Key > B.Key : A.Value > B.Value; });
        int32 Count = 0;
        for (; Count < List.Num() && Count < 5; ++Count) lua_pushinteger(L, List[Count].Value);
        return Count;
    }

    int AC_GetLatestUpdatedStats(lua_State* L)
    {
        int32 Count = 0;
        for (; Count < GRecentStats.Num() && Count < 5; ++Count) lua_pushinteger(L, GRecentStats[Count]);
        return Count;
    }

    int AC_GetAchievementLink(lua_State* L)
    {
        Load(L);
        const uint32 Id = uint32(luaL_optinteger(L, 1, 0));
        const FAchievement* A = GAchievements.Find(Id);
        FWowPacketHandler* H = Handler(L);
        if (!A || !H) return 0;
        const uint32* Packed = H->CompletedAchievements.Find(Id);
        int32 Month = 0, Day = 0, Year = 0;
        if (Packed) UnpackDate(*Packed, Month, Day, Year);
        const FString Link = FString::Printf(TEXT("|cffffff00|Hachievement:%u:%016llX:%d:%d:%d:%d:0:0:0:0|h[%s]|h|r"),
            Id, H->EntityManager.LocalPlayerGuid, Packed ? 1 : 0, Packed ? Month : -1, Packed ? Day : -1, Packed ? Year : -1, *A->Name);
        PushString(L, Link);
        return 1;
    }

    int AC_CanShowAchievementUI(lua_State* L) { lua_pushboolean(L, 1); return 1; }

    // ── Tracking (WatchFrame) ───────────────────────────────────────────────────

    constexpr int32 MAX_TRACKED = 10;              // WATCHFRAME_MAXACHIEVEMENTS
    const TCHAR* TRACKED_FILE = TEXT("tracked-achievements.txt");

    void ParseTracked(const FString& Text)
    {
        TArray<FString> Parts;
        Text.ParseIntoArray(Parts, TEXT(":"), true);
        for (const FString& Part : Parts)
        {
            const uint32 Id = uint32(FCString::Atoi(*Part.TrimStartAndEnd()));
            if (Id && !GTracked.Contains(Id) && GTracked.Num() < MAX_TRACKED) GTracked.Add(Id);
        }
    }

    /** The list for the character now logged in: our file, else the WoW client's config-cache.wtf. */
    bool EnsureTrackedLoaded(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        if (!FWowSavedVariables::HasIdentity() || !H || !H->EntityManager.GetLocalPlayer()) return false;
        const FString Owner = FWowSavedVariables::GetCharacterRelative();
        if (Owner == GTrackedOwner) return true;
        GTrackedOwner = Owner;
        GTracked.Reset();
        FString Text;
        const FString Ours = FWowSavedVariables::ResolveReadPath(FPaths::Combine(Owner, TRACKED_FILE));
        if (!Ours.IsEmpty() && FFileHelper::LoadFileToString(Text, *Ours))
        {
            FWowSavedVariables::NoteRead(Ours);
            ParseTracked(Text);
        }
        else
        {
            // config-cache.wtf: SET trackedAchievements "1234:5678"
            const FString Client = FWowSavedVariables::ResolveReadPath(FPaths::Combine(Owner, TEXT("config-cache.wtf")));
            TArray<FString> Lines;
            if (!Client.IsEmpty() && FFileHelper::LoadFileToStringArray(Lines, *Client))
            {
                for (const FString& Line : Lines)
                {
                    if (!Line.StartsWith(TEXT("SET trackedAchievements "), ESearchCase::IgnoreCase)) continue;
                    ParseTracked(Line.Mid(24).Replace(TEXT("\""), TEXT("")));
                }
            }
        }
        UE_LOG(LogWowAchievementApi, Log, TEXT("Tracked achievements for %s: %d"), *Owner, GTracked.Num());
        return true;
    }

    void SaveTracked()
    {
        if (GTrackedOwner.IsEmpty()) return;
        TArray<FString> Ids;
        for (uint32 Id : GTracked) Ids.Add(FString::Printf(TEXT("%u"), Id));
        const FString Path = FWowSavedVariables::GetWritePath(FPaths::Combine(GTrackedOwner, TRACKED_FILE));
        if (!FWowSavedVariables::SaveWtfFile(Path, FString::Join(Ids, TEXT(":"))))
        {
            UE_LOG(LogWowAchievementApi, Warning, TEXT("Could not save %s"), *Path);
        }
    }

    void FireTracked(lua_State* L)
    {
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem) Ctx->EventSystem->FireEvent(TEXT("TRACKED_ACHIEVEMENT_UPDATE"));
    }
    int AC_AddTrackedAchievement(lua_State* L)
    {
        EnsureTrackedLoaded(L);
        const uint32 Id = uint32(luaL_optinteger(L, 1, 0));
        if (Id && !GTracked.Contains(Id) && GTracked.Num() < MAX_TRACKED) { GTracked.Add(Id); SaveTracked(); FireTracked(L); }
        return 0;
    }
    int AC_RemoveTrackedAchievement(lua_State* L)
    {
        EnsureTrackedLoaded(L);
        if (GTracked.Remove(uint32(luaL_optinteger(L, 1, 0)))) { SaveTracked(); FireTracked(L); }
        return 0;
    }
    int AC_GetTrackedAchievements(lua_State* L)
    {
        EnsureTrackedLoaded(L);
        for (uint32 Id : GTracked) lua_pushinteger(L, Id);
        return GTracked.Num();
    }
    int AC_GetNumTrackedAchievements(lua_State* L) { EnsureTrackedLoaded(L); lua_pushinteger(L, GTracked.Num()); return 1; }
    int AC_IsTrackedAchievement(lua_State* L) { EnsureTrackedLoaded(L); lua_pushboolean(L, GTracked.Contains(uint32(luaL_optinteger(L, 1, 0)))); return 1; }
    // Older (3.0/3.1) single-track names some addons still call.
    int AC_GetTrackedAchievement(lua_State* L) { EnsureTrackedLoaded(L); if (!GTracked.Num()) return 0; lua_pushinteger(L, GTracked[0]); return 1; }
    int AC_SetTrackedAchievement(lua_State* L)
    {
        EnsureTrackedLoaded(L);
        const uint32 Id = uint32(luaL_optinteger(L, 1, 0));
        GTracked.Reset();
        if (Id) GTracked.Add(Id);
        SaveTracked();
        FireTracked(L);
        return 0;
    }

    // ── Comparison (not served yet: needs SMSG_RESPOND_INSPECT_ACHIEVEMENTS) ────

    int AC_NoOp(lua_State*) { return 0; }
    int AC_GetAchievementComparisonInfo(lua_State* L) { lua_pushboolean(L, 0); lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); return 4; }
    int AC_GetComparisonAchievementPoints(lua_State* L) { lua_pushinteger(L, 0); return 1; }
    int AC_GetComparisonCategoryNumAchievements(lua_State* L) { lua_pushinteger(L, 0); lua_pushinteger(L, 0); return 2; }
    int AC_GetComparisonStatistic(lua_State* L) { lua_pushstring(L, "--"); return 1; }

    // ── Events ──────────────────────────────────────────────────────────────────

    int AC_Tick(lua_State* L)
    {
        // A few times a second from the UI tick: ACHIEVEMENT_EARNED for new completions,
        // CRITERIA_UPDATE when progress changed.
        FWowPacketHandler* H = Handler(L);
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        // The tracked list arrives with the character: show it in the WatchFrame as soon as it is known.
        const FString OwnerBefore = GTrackedOwner;
        if (EnsureTrackedLoaded(L) && GTrackedOwner != OwnerBefore && GTracked.Num()) FireTracked(L);
        if (!H || !Ctx || !Ctx->EventSystem || H->AchievementRevision == GSeenRevision) return 0;
        GSeenRevision = H->AchievementRevision;
        Load(L);
        TArray<uint32> Earned;
        for (const auto& Pair : H->CompletedAchievements) if (!GSeenCompleted.Contains(Pair.Key)) Earned.Add(Pair.Key);
        bool bCriteriaChanged = false;
        for (const auto& Pair : H->AchievementCriteria)
        {
            const uint64* Old = GSeenCriteria.Find(Pair.Key);
            if (Old && *Old == Pair.Value) continue;
            bCriteriaChanged = true;
            if (!bGSeenOnce) continue;
            const FCriteria* C = GCriteria.Find(Pair.Key);
            const FAchievement* Owner = C ? GAchievements.Find(C->Achievement) : nullptr;
            if (Owner && (Owner->Flags & FLAG_STATISTIC))
            {
                GRecentStats.Remove(Owner->Id);
                GRecentStats.Insert(Owner->Id, 0);
                if (GRecentStats.Num() > 5) GRecentStats.SetNum(5);
            }
        }
        GSeenCriteria = H->AchievementCriteria;
        for (uint32 Id : Earned) GSeenCompleted.Add(Id);
        // A finished achievement leaves the tracker, like the client.
        int32 Dropped = 0;
        for (int32 I = GTracked.Num() - 1; I >= 0; --I)
        {
            if (H->CompletedAchievements.Contains(GTracked[I])) { GTracked.RemoveAt(I); ++Dropped; }
        }
        if (Dropped) { SaveTracked(); FireTracked(L); }
        // The first full list at login is the starting point, not a row of new achievements.
        if (bGSeenOnce)
            for (uint32 Id : Earned)
            {
                Ctx->EventSystem->FireEvent(TEXT("ACHIEVEMENT_EARNED"), {FString::Printf(TEXT("%u"), Id)});
                UE_LOG(LogWowAchievementApi, Log, TEXT("ACHIEVEMENT_EARNED %u"), Id);
            }
        bGSeenOnce = H->CompletedAchievements.Num() > 0 || H->AchievementCriteria.Num() > 0 || bGSeenOnce;
        if (bCriteriaChanged || Earned.Num()) Ctx->EventSystem->FireEvent(TEXT("CRITERIA_UPDATE"));
        return 0;
    }
}
#endif

void WowLuaApi::RegisterAchievementApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    using namespace WowAchievementApiImpl;
    lua_register(L, "GetCategoryList", AC_GetCategoryList);
    lua_register(L, "GetStatisticsCategoryList", AC_GetStatisticsCategoryList);
    lua_register(L, "GetCategoryInfo", AC_GetCategoryInfo);
    lua_register(L, "GetCategoryNumAchievements", AC_GetCategoryNumAchievements);
    lua_register(L, "GetNumCompletedAchievements", AC_GetNumCompletedAchievements);
    lua_register(L, "GetTotalAchievementPoints", AC_GetTotalAchievementPoints);
    lua_register(L, "GetAchievementInfo", AC_GetAchievementInfo);
    lua_register(L, "GetAchievementCategory", AC_GetAchievementCategory);
    lua_register(L, "GetPreviousAchievement", AC_GetPreviousAchievement);
    lua_register(L, "GetNextAchievement", AC_GetNextAchievement);
    lua_register(L, "GetAchievementNumCriteria", AC_GetAchievementNumCriteria);
    lua_register(L, "GetAchievementCriteriaInfo", AC_GetAchievementCriteriaInfo);
    lua_register(L, "GetAchievementInfoFromCriteria", AC_GetAchievementInfoFromCriteria);
    lua_register(L, "GetStatistic", AC_GetStatistic);
    lua_register(L, "GetLatestCompletedAchievements", AC_GetLatestCompletedAchievements);
    lua_register(L, "GetLatestUpdatedStats", AC_GetLatestUpdatedStats);
    lua_register(L, "GetAchievementLink", AC_GetAchievementLink);
    lua_register(L, "CanShowAchievementUI", AC_CanShowAchievementUI);
    lua_register(L, "AddTrackedAchievement", AC_AddTrackedAchievement);
    lua_register(L, "RemoveTrackedAchievement", AC_RemoveTrackedAchievement);
    lua_register(L, "GetTrackedAchievements", AC_GetTrackedAchievements);
    lua_register(L, "GetNumTrackedAchievements", AC_GetNumTrackedAchievements);
    lua_register(L, "IsTrackedAchievement", AC_IsTrackedAchievement);
    lua_register(L, "GetTrackedAchievement", AC_GetTrackedAchievement);
    lua_register(L, "SetTrackedAchievement", AC_SetTrackedAchievement);
    lua_register(L, "SetAchievementComparisonUnit", AC_NoOp);
    lua_register(L, "ClearAchievementComparisonUnit", AC_NoOp);
    lua_register(L, "GetAchievementComparisonInfo", AC_GetAchievementComparisonInfo);
    lua_register(L, "GetComparisonAchievementPoints", AC_GetComparisonAchievementPoints);
    lua_register(L, "GetComparisonCategoryNumAchievements", AC_GetComparisonCategoryNumAchievements);
    lua_register(L, "GetComparisonStatistic", AC_GetComparisonStatistic);
    lua_register(L, "__WowAchievementTick", AC_Tick);
    // Chained onto the UI tick like the skill check (LuaSkillApi.cpp). r343 (Stefan 14.10): the
    // WatchFrame lists tracked achievements at the bottom, under the quests (the stock order puts
    // them above): its handler moves to the end of WATCHFRAME_OBJECTIVEHANDLERS once.
    luaL_dostring(L, R"LUA(
local previous = __WowWatchFrameTick
local moved = false
function __WowWatchFrameTick(...)
    if previous then pcall(previous, ...) end
    if not moved and WatchFrame and WATCHFRAME_OBJECTIVEHANDLERS and WatchFrame_RemoveObjectiveHandler
        and WatchFrame_AddObjectiveHandler and WatchFrame_HandleDisplayTrackedAchievements then
        moved = true
        WatchFrame_RemoveObjectiveHandler(WatchFrame_HandleDisplayTrackedAchievements)
        WatchFrame_AddObjectiveHandler(WatchFrame_HandleDisplayTrackedAchievements)
        if WatchFrame_Update then pcall(WatchFrame_Update, WatchFrame) end
    end
    pcall(__WowAchievementTick)
end
)LUA");
    UE_LOG(LogWowAchievementApi, Log, TEXT("Achievement Lua API registered"));
#endif
}
