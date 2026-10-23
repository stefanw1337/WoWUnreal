// r354 (Stefan 15.10, "start working on the barber UI"): the 3.3.5 barber shop API for the stock
// Blizzard_BarberShopUI (loaded by UIParent on BARBER_SHOP_OPEN):
//   GetHairCustomization, GetFacialHairCustomization, CanAlterSkin, GetBarberShopStyleInfo,
//   SetNextBarberShopStyle, GetBarberShopTotalCost, BarberShopReset, ApplyBarberShopStyle,
//   CancelBarberShop.
// Selector ids (the XML's BarberShopFrameSelector1-4): 1 hair style, 2 hair colour, 3 facial hair
// (or the race's feature: tusks, horns, earrings ...), 4 skin colour.
//
// Data: BarberShopStyle.dbc (the styles a barber offers per race and sex: type 0 hair, 2 facial
// hair, 3 skin; Data = the PLAYER_BYTES value), CharSections.dbc (the hair colours of a style) and
// gtBarberShopCostBase.dbc (the base price per character level). Price: a new hair style costs the
// base price, a new colour of the same style half of it, new facial hair or skin three quarters.
//
// The session state is FWowBarberShop (WowBarberShop.h): the gameplay controller shows the chosen
// look on our model while the window is open. The window closes (BARBER_SHOP_CLOSE) when we leave
// the chair - Cancel stands us up, and the server stands us up after a haircut.
#include "WowBarberShop.h"
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
#include "Formats/Dbc/DbcStore.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogWowBarberApi, Log, All);

namespace WowLuaApi { void RegisterBarberShopApi(lua_State* L); }

#if HAS_LUA // r569 (Claude-Perf, build fix): opens the block that the #endif after the namespace closes
// Named, not anonymous: in a unity build this file shares a translation unit with the other LuaApi files.
namespace WowBarberApiImpl
{
    constexpr uint16 CMSG_ALTER_APPEARANCE = 0x426;

    struct FLook
    {
        uint8 Skin = 0, HairStyle = 0, HairColor = 0, FacialHair = 0;
        bool operator==(const FLook& Other) const
        {
            return Skin == Other.Skin && HairStyle == Other.HairStyle && HairColor == Other.HairColor && FacialHair == Other.FacialHair;
        }
        bool operator!=(const FLook& Other) const { return !(*this == Other); }
    };

    // Session (also read by the gameplay controller through FWowBarberShop).
    bool bGOpenPending = false;     // SMSG_ENABLE_BARBER_SHOP arrived, the look is not taken yet
    bool bGOpen = false;
    bool bGSeated = false;          // the chair stand state was seen during the session
    double GOpenedAt = 0.0;
    FLook GOriginal, GChosen;
    uint8 GRace = 0, GSex = 0;
    uint32 GSerial = 1;
    bool bGAwaiting = false;        // ApplyBarberShopStyle sent, waiting for the new fields
    bool bGResultSeen = false;
    double GAppliedAt = 0.0;
    FLook GRequested;

    struct FStyle { uint32 Id = 0; uint32 Type = 0; FString Name; float CostModifier = 1.f; uint32 Race = 0, Sex = 0, Data = 0; };
    TArray<FStyle> GStyles;
    TArray<float> GCostBase;
    bool bGStylesLoaded = false, bGCostLoaded = false;

    FWowPacketHandler* Handler(lua_State* L)
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        return Ctx && Ctx->ConnectionManager ? &Ctx->ConnectionManager->PacketHandler : nullptr;
    }
    const FWowPlayerEntity* Me(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        return H ? H->EntityManager.GetLocalPlayer() : nullptr;
    }
    FLook LookOf(const FWowPlayerEntity& Player)
    {
        FLook Look;
        Look.Skin = Player.GetFieldByte(PlayerField::BYTES, 0);
        Look.HairStyle = Player.GetFieldByte(PlayerField::BYTES, 2);
        Look.HairColor = Player.GetFieldByte(PlayerField::BYTES, 3);
        Look.FacialHair = Player.GetFieldByte(PlayerField::BYTES_2, 0);
        return Look;
    }
    void PushString(lua_State* L, const FString& Text) { lua_pushstring(L, TCHAR_TO_UTF8(*Text)); }
    void Fire(lua_State* L, const TCHAR* Event, const TArray<FString>& Args = {})
    {
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem) Ctx->EventSystem->FireEvent(Event, Args);
    }

    FMpqManager* FindMpq(lua_State* L)
    {
        if (FMpqManager* Mpq = FWowAddonSettings::GetMpq()) return Mpq;
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        UWorld* World = Ctx && Ctx->AudioManager ? Ctx->AudioManager->GetWorld() : nullptr;
        const AWowWorldManager* Terrain = World ? Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(World, AWowWorldManager::StaticClass())) : nullptr;
        return Terrain ? Terrain->GetMpqManager() : nullptr;
    }

    void LoadData(lua_State* L)
    {
        if (bGStylesLoaded && bGCostLoaded) return;
        FMpqManager* Mpq = FindMpq(L);
        if (!Mpq) return; // retried on the next call
        if (!bGStylesLoaded)
        {
            TArray<uint8> Bytes;
            FDbcParser Parser;
            if (Mpq->ReadFile(TEXT("DBFilesClient\\BarberShopStyle.dbc"), Bytes) && Parser.Parse(Bytes))
            {
                bGStylesLoaded = true;
                // 3.3.5a: 0 ID, 1 type, 2 name (16 locales + mask), 19 additional name, 36 cost modifier,
                // 37 race, 38 sex, 39 data.
                if (Parser.GetFieldCount() >= 40)
                {
                    for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
                    {
                        FStyle Style;
                        Style.Id = Parser.GetUInt(R, 0);
                        Style.Type = Parser.GetUInt(R, 1);
                        Style.Name = Parser.GetLocalizedString(R, 2);
                        Style.CostModifier = Parser.GetFloat(R, 36);
                        Style.Race = Parser.GetUInt(R, 37);
                        Style.Sex = Parser.GetUInt(R, 38);
                        Style.Data = Parser.GetUInt(R, 39);
                        GStyles.Add(MoveTemp(Style));
                    }
                    GStyles.Sort([](const FStyle& A, const FStyle& B) { return A.Id < B.Id; });
                    UE_LOG(LogWowBarberApi, Log, TEXT("BarberShopStyle.dbc: %d styles"), GStyles.Num());
                }
                else UE_LOG(LogWowBarberApi, Warning, TEXT("BarberShopStyle.dbc: %u fields, expected 40 (3.3.5a)"), Parser.GetFieldCount());
            }
        }
        if (!bGCostLoaded)
        {
            TArray<uint8> Bytes;
            FDbcParser Parser;
            if (Mpq->ReadFile(TEXT("DBFilesClient\\gtBarberShopCostBase.dbc"), Bytes) && Parser.Parse(Bytes))
            {
                bGCostLoaded = true;
                for (uint32 R = 0; R < Parser.GetRecordCount(); ++R) GCostBase.Add(Parser.GetFloat(R, 0));
                UE_LOG(LogWowBarberApi, Log, TEXT("gtBarberShopCostBase.dbc: %d levels"), GCostBase.Num());
            }
        }
    }

    /** The barber's choices of one kind for our race and sex, in the dbc's order. */
    TArray<const FStyle*> StylesOf(uint32 Type)
    {
        TArray<const FStyle*> Result;
        for (const FStyle& Style : GStyles)
            if (Style.Type == Type && Style.Race == GRace && Style.Sex == GSex) Result.Add(&Style);
        return Result;
    }
    const FStyle* StyleFor(uint32 Type, uint32 Data)
    {
        for (const FStyle* Style : StylesOf(Type)) if (Style->Data == Data) return Style;
        return nullptr;
    }
    /** The hair colours of a hair style (CharSections hair rows for creation or the barber). */
    TArray<uint8> HairColors(uint8 Style)
    {
        TArray<uint8> Colors;
        for (const FCharSectionsDbcEntry& Entry : FDbcStore::Get().CharSections().GetAll())
            if (Entry.Type == 3 && Entry.RaceID == GRace && Entry.SexID == GSex && Entry.Variation == Style && (Entry.Flags & 3))
                Colors.AddUnique(uint8(Entry.Color));
        Colors.Sort();
        return Colors;
    }

    /** Take the current look when the session starts (the first call after SMSG_ENABLE_BARBER_SHOP). */
    void EnsureSession(lua_State* L)
    {
        const FWowPlayerEntity* Player = Me(L);
        if (!Player) return;
        LoadData(L);
        if (bGOpenPending || !bGOpen)
        {
            const bool bStart = bGOpenPending;
            GRace = Player->GetRaceId();
            GSex = Player->GetGenderId();
            if (!bStart) return; // outside a session: the race is enough (OnLoad labels)
            bGOpenPending = false;
            bGOpen = true;
            bGSeated = false;
            bGAwaiting = false;
            bGResultSeen = false;
            GOpenedAt = FPlatformTime::Seconds();
            GOriginal = GChosen = LookOf(*Player);
            ++GSerial;
            UE_LOG(LogWowBarberApi, Log, TEXT("Barber shop: open (race %d sex %d, skin %d hair %d colour %d facial %d)"),
                int32(GRace), int32(GSex), int32(GOriginal.Skin), int32(GOriginal.HairStyle), int32(GOriginal.HairColor), int32(GOriginal.FacialHair));
        }
    }

    float BaseCost(lua_State* L)
    {
        const FWowPlayerEntity* Player = Me(L);
        if (!Player || GCostBase.IsEmpty()) return 0.f;
        const int32 Level = FMath::Clamp(Player->GetLevel(), 1, GCostBase.Num());
        return GCostBase[Level - 1];
    }
    float CategoryCost(lua_State* L, int32 Category)
    {
        const float Base = BaseCost(L);
        switch (Category)
        {
        case 1: return GChosen.HairStyle != GOriginal.HairStyle ? Base : 0.f;
        case 2: return GChosen.HairColor != GOriginal.HairColor && GChosen.HairStyle == GOriginal.HairStyle ? Base * 0.5f : 0.f;
        case 3: return GChosen.FacialHair != GOriginal.FacialHair ? Base * 0.75f : 0.f;
        case 4: return GChosen.Skin != GOriginal.Skin ? Base * 0.75f : 0.f;
        default: return 0.f;
        }
    }
    uint32 TotalCost(lua_State* L)
    {
        float Cost = 0.f;
        for (int32 Category = 1; Category <= 4; ++Category) Cost += CategoryCost(L, Category);
        return uint32(Cost);
    }

    void CloseSession(lua_State* L, const TCHAR* Why)
    {
        if (!bGOpen && !bGOpenPending) return;
        bGOpen = bGOpenPending = bGAwaiting = false;
        ++GSerial;
        UE_LOG(LogWowBarberApi, Log, TEXT("Barber shop: closed (%s)"), Why);
        Fire(L, TEXT("BARBER_SHOP_CLOSE"));
    }

    void SetChosen(const FLook& Look)
    {
        if (Look == GChosen) return;
        GChosen = Look;
        ++GSerial;
    }

    // ── API ─────────────────────────────────────────────────────────────────────

    int BB_GetHairCustomization(lua_State* L)
    {
        EnsureSession(L);
        const FChrRacesDbcEntry* Race = FDbcStore::Get().ChrRaces().GetById(GRace);
        const FString Kind = Race && !Race->HairCustomization.IsEmpty() ? Race->HairCustomization.ToUpper() : FString(TEXT("NORMAL"));
        PushString(L, Kind);
        return 1;
    }

    int BB_GetFacialHairCustomization(lua_State* L)
    {
        EnsureSession(L);
        const FChrRacesDbcEntry* Race = FDbcStore::Get().ChrRaces().GetById(GRace);
        FString Kind = Race ? (GSex == 1 ? Race->FacialHairCustomization2 : Race->FacialHairCustomization1) : FString();
        PushString(L, Kind.IsEmpty() ? FString(TEXT("NORMAL")) : Kind.ToUpper());
        return 1;
    }

    int BB_CanAlterSkin(lua_State* L)
    {
        EnsureSession(L);
        lua_pushboolean(L, StylesOf(3).Num() > 0 ? 1 : 0);
        return 1;
    }

    int BB_GetBarberShopStyleInfo(lua_State* L)
    {
        // name, (unused), cost, isCurrent
        EnsureSession(L);
        const int32 Category = int32(luaL_optinteger(L, 1, 0));
        const FStyle* Style = nullptr;
        bool bCurrent = true;
        switch (Category)
        {
        case 1: Style = StyleFor(0, GChosen.HairStyle); bCurrent = GChosen.HairStyle == GOriginal.HairStyle; break;
        case 2: bCurrent = GChosen.HairColor == GOriginal.HairColor; break;
        case 3: Style = StyleFor(2, GChosen.FacialHair); bCurrent = GChosen.FacialHair == GOriginal.FacialHair; break;
        case 4: Style = StyleFor(3, GChosen.Skin); bCurrent = GChosen.Skin == GOriginal.Skin; break;
        default: break;
        }
        if (Style && !Style->Name.IsEmpty()) PushString(L, Style->Name); else lua_pushnil(L);
        lua_pushnil(L);
        lua_pushinteger(L, lua_Integer(CategoryCost(L, Category)));
        if (bCurrent) lua_pushboolean(L, 1); else lua_pushnil(L);
        return 4;
    }

    int BB_SetNextBarberShopStyle(lua_State* L)
    {
        EnsureSession(L);
        const int32 Category = int32(luaL_optinteger(L, 1, 0));
        const bool bReverse = lua_toboolean(L, 2) != 0;
        FLook Look = GChosen;
        auto Step = [bReverse](int32 Index, int32 Count) { return Count <= 0 ? -1 : (Index < 0 ? (bReverse ? Count - 1 : 0) : (Index + (bReverse ? Count - 1 : 1)) % Count); };
        if (Category == 2)
        {
            const TArray<uint8> Colors = HairColors(Look.HairStyle);
            const int32 Next = Step(Colors.IndexOfByKey(Look.HairColor), Colors.Num());
            if (Next >= 0) Look.HairColor = Colors[Next];
        }
        else if (Category == 1 || Category == 3 || Category == 4)
        {
            const uint32 Type = Category == 1 ? 0u : Category == 3 ? 2u : 3u;
            const TArray<const FStyle*> Styles = StylesOf(Type);
            uint8& Value = Category == 1 ? Look.HairStyle : Category == 3 ? Look.FacialHair : Look.Skin;
            const int32 Index = Styles.IndexOfByPredicate([&](const FStyle* Style) { return Style->Data == Value; });
            const int32 Next = Step(Index, Styles.Num());
            if (Next >= 0) Value = uint8(Styles[Next]->Data);
            if (Category == 1)
            {
                // A colour the new style does not have becomes its first one (the client keeps it when it exists).
                const TArray<uint8> Colors = HairColors(Look.HairStyle);
                if (Colors.Num() && !Colors.Contains(Look.HairColor)) Look.HairColor = Colors[0];
            }
        }
        SetChosen(Look);
        return 0;
    }

    int BB_GetBarberShopTotalCost(lua_State* L)
    {
        EnsureSession(L);
        lua_pushinteger(L, lua_Integer(TotalCost(L)));
        return 1;
    }

    int BB_BarberShopReset(lua_State* L)
    {
        EnsureSession(L);
        SetChosen(GOriginal);
        return 0;
    }

    int BB_ApplyBarberShopStyle(lua_State* L)
    {
        EnsureSession(L);
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        const FWowPlayerEntity* Player = Me(L);
        if (!bGOpen || !Ctx || !Ctx->ConnectionManager || !Player || GChosen == GOriginal) return 0;
        const uint32 Cost = TotalCost(L);
        if (Cost > Player->GetCoinage())
        {
            lua_getglobal(L, "ERR_NOT_ENOUGH_MONEY");
            const FString Message = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString(TEXT("You don't have enough money."));
            lua_pop(L, 1);
            Fire(L, TEXT("UI_ERROR_MESSAGE"), { Message });
            return 0;
        }
        const FStyle* Hair = StyleFor(0, GChosen.HairStyle);
        const FStyle* Facial = StyleFor(2, GChosen.FacialHair);
        const FStyle* Skin = GChosen.Skin != GOriginal.Skin ? StyleFor(3, GChosen.Skin) : nullptr;
        if (!Hair || !Facial)
        {
            UE_LOG(LogWowBarberApi, Warning, TEXT("Barber shop: no BarberShopStyle row for hair %d / facial %d (race %d sex %d)"),
                int32(GChosen.HairStyle), int32(GChosen.FacialHair), int32(GRace), int32(GSex));
            return 0;
        }
        TArray<uint8> Data;
        auto Put = [&Data](uint32 Value) { Data.Append(reinterpret_cast<const uint8*>(&Value), 4); };
        Put(Hair->Id);
        Put(GChosen.HairColor);
        Put(Facial->Id);
        Put(Skin ? Skin->Id : 0u);
        Ctx->ConnectionManager->SendRawPacket(CMSG_ALTER_APPEARANCE, Data);
        bGAwaiting = true;
        bGResultSeen = false;
        GAppliedAt = FPlatformTime::Seconds();
        GRequested = GChosen;
        UE_LOG(LogWowBarberApi, Log, TEXT("Barber shop: apply hair %u colour %d facial %u skin %u, %u copper"),
            Hair->Id, int32(GChosen.HairColor), Facial->Id, Skin ? Skin->Id : 0u, Cost);
        return 0;
    }

    int BB_CancelBarberShop(lua_State* L)
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        const bool bWasOpen = bGOpen || bGOpenPending;
        CloseSession(L, TEXT("cancelled"));
        // Leave the chair like the client does (the server would otherwise keep us seated).
        if (bWasOpen && Ctx && Ctx->ConnectionManager) Ctx->ConnectionManager->SendStandStateChange(0);
        return 0;
    }

    int BB_Tick(lua_State* L)
    {
        // A few times a second from the UI tick.
        if (bGOpenPending) EnsureSession(L);
        if (!bGOpen) return 0;
        const FWowPlayerEntity* Player = Me(L);
        if (!Player) { CloseSession(L, TEXT("no player")); return 0; }
        const uint8 Stand = Player->GetFieldByte(UnitField::BYTES_1, 0);
        const bool bChair = Stand >= 4 && Stand <= 6; // UNIT_STAND_STATE_SIT_LOW/MEDIUM/HIGH_CHAIR
        const double Now = FPlatformTime::Seconds();
        if (bGAwaiting)
        {
            const FLook Live = LookOf(*Player);
            if (Live == GRequested)
            {
                bGAwaiting = false;
                GOriginal = Live;
                ++GSerial;
                UE_LOG(LogWowBarberApi, Log, TEXT("Barber shop: new look applied"));
                Fire(L, TEXT("BARBER_SHOP_SUCCESS"));
                Fire(L, TEXT("BARBER_SHOP_APPEARANCE_APPLIED"));
            }
            else if (bGResultSeen && Now - GAppliedAt > 3.0)
            {
                bGAwaiting = false;
                UE_LOG(LogWowBarberApi, Warning, TEXT("Barber shop: the server did not change the look"));
                Fire(L, TEXT("BARBER_SHOP_APPEARANCE_APPLIED"));
            }
        }
        if (bChair) bGSeated = true;
        else if (bGSeated && !bGAwaiting) CloseSession(L, TEXT("left the chair"));
        else if (!bGSeated && Now - GOpenedAt > 5.0) CloseSession(L, TEXT("not seated"));
        return 0;
    }
}
#endif

void FWowBarberShop::NotifyOpened()
{
#if HAS_LUA
    using namespace WowBarberApiImpl;
    bGOpenPending = true;
    ++GSerial;
#endif
}

void FWowBarberShop::NotifyResult()
{
#if HAS_LUA
    WowBarberApiImpl::bGResultSeen = true;
#endif
}

bool FWowBarberShop::IsOpen()
{
#if HAS_LUA
    return WowBarberApiImpl::bGOpen || WowBarberApiImpl::bGOpenPending;
#else
    return false;
#endif
}

bool FWowBarberShop::GetPreview(uint8& OutSkin, uint8& OutHairStyle, uint8& OutHairColor, uint8& OutFacialHair)
{
#if HAS_LUA
    using namespace WowBarberApiImpl;
    if (!bGOpen || GChosen == GOriginal) return false;
    OutSkin = GChosen.Skin;
    OutHairStyle = GChosen.HairStyle;
    OutHairColor = GChosen.HairColor;
    OutFacialHair = GChosen.FacialHair;
    return true;
#else
    return false;
#endif
}

uint32 FWowBarberShop::GetPreviewSerial()
{
#if HAS_LUA
    return WowBarberApiImpl::GSerial;
#else
    return 0;
#endif
}

void WowLuaApi::RegisterBarberShopApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    using namespace WowBarberApiImpl;
    lua_register(L, "GetHairCustomization", BB_GetHairCustomization);
    lua_register(L, "GetFacialHairCustomization", BB_GetFacialHairCustomization);
    lua_register(L, "CanAlterSkin", BB_CanAlterSkin);
    lua_register(L, "GetBarberShopStyleInfo", BB_GetBarberShopStyleInfo);
    lua_register(L, "SetNextBarberShopStyle", BB_SetNextBarberShopStyle);
    lua_register(L, "GetBarberShopTotalCost", BB_GetBarberShopTotalCost);
    lua_register(L, "BarberShopReset", BB_BarberShopReset);
    lua_register(L, "ApplyBarberShopStyle", BB_ApplyBarberShopStyle);
    lua_register(L, "CancelBarberShop", BB_CancelBarberShop);
    lua_register(L, "__WowBarberShopTick", BB_Tick);
    // Chained onto the UI tick like the skill check (LuaSkillApi.cpp).
    luaL_dostring(L, R"LUA(
local previous = __WowWatchFrameTick
function __WowWatchFrameTick(...)
    if previous then pcall(previous, ...) end
    pcall(__WowBarberShopTick)
end
)LUA");
    UE_LOG(LogWowBarberApi, Log, TEXT("Barber shop Lua API registered"));
#endif
}
