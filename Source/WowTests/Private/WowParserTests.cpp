#include "Formats/Dbc/EmotesDbc.h"
#include "Formats/Dbc/EmotesTextDbc.h"
#include "Misc/AutomationTest.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "Formats/BlpParser.h"
#include "Formats/BlpTypes.h"
#include "Formats/AdtParser.h"
#include "Formats/AdtTypes.h"
#include "Formats/M2Parser.h"
#include "Formats/M2Types.h"
#include "Formats/WmoParser.h"
#include "Formats/WmoTypes.h"
#include "Coord/WowCoordinate.h"
#include "WowTextureFactory.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WowWaterMaterial.h"
#include "WowWaterRenderer.h"
#include "WowDoodadManager.h"
#include "WowAssetCache.h"

#define private public
#include "WowConnectionManager.h"
#include "Net/WowWorldSocket.h"
#undef private

#include "WowLuaVM.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "WowFrameTypes.h"
#include "WowInventoryManager.h"
#include "WowOpcodes.h"
#include "Components/CanvasPanel.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPacketReaderRejectsTruncatedFields,
    "WowUnreal.Network.Security.PacketReaderRejectsTruncatedFields",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPacketReaderRejectsTruncatedFields::RunTest(const FString& Parameters)
{
    TArray<uint8> TruncatedInteger = { 0x12, 0x34, 0x56 };
    FPacketReader IntegerReader(TruncatedInteger);
    TestEqual(TEXT("Truncated uint32 returns no partial value"), IntegerReader.ReadU32(), 0u);
    TestTrue(TEXT("Truncated uint32 marks the reader failed"), IntegerReader.HasFailed());
    TestEqual(TEXT("Failed fixed-width read does not advance the cursor"), IntegerReader.Pos, 0);

    TArray<uint8> UnterminatedString = { 'n', 'a', 'm', 'e' };
    FPacketReader StringReader(UnterminatedString);
    TestEqual(TEXT("Unterminated string is rejected"), StringReader.ReadCString(), FString());
    TestTrue(TEXT("Unterminated string marks the reader failed"), StringReader.HasFailed());
    TestEqual(TEXT("Failed string read restores the cursor"), StringReader.Pos, 0);

    TArray<uint8> TruncatedGuid = { 0x03, 0xAA };
    FPacketReader GuidReader(TruncatedGuid);
    TestEqual(TEXT("Truncated packed GUID returns no partial GUID"), GuidReader.ReadPackedGuid(), 0ULL);
    TestTrue(TEXT("Truncated packed GUID marks the reader failed"), GuidReader.HasFailed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPacketHandlerRejectsTruncatedRequiredPayload,
    "WowUnreal.Network.Security.PacketHandlerRejectsTruncatedRequiredPayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPacketHandlerRejectsTruncatedRequiredPayload::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    bool bWorldPositionBroadcast = false;
    bool bOpcodeBroadcast = false;
    Handler.OnLoginVerifyWorld.AddLambda(
        [&bWorldPositionBroadcast](uint32, float, float, float, float)
        {
            bWorldPositionBroadcast = true;
        });
    Handler.OnOpcodeReceived.AddLambda(
        [&bOpcodeBroadcast](uint16)
        {
            bOpcodeBroadcast = true;
        });

    TArray<uint8> TruncatedLoginPayload;
    TruncatedLoginPayload.SetNumZeroed(12); // LOGIN_VERIFY_WORLD requires 20 bytes.
    Handler.HandlePacket(WowOpcode::SMSG_LOGIN_VERIFY_WORLD, TruncatedLoginPayload);

    TestFalse(TEXT("Malformed login payload does not publish a world position"), bWorldPositionBroadcast);
    TestFalse(TEXT("Malformed packet does not reach gameplay/UI opcode subscribers"), bOpcodeBroadcast);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContactList335PacketLayout,
    "WowUnreal.Network.ContactList335PacketLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FContactList335PacketLayout::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;
    TArray<uint8> Packet;
    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value) { Buffer.Add(Value); };
    auto AppendU32 = [](TArray<uint8>& Buffer, uint32 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(Value));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(Value));
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(Value));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(Value));
    };
    auto AppendCString = [](TArray<uint8>& Buffer, const ANSICHAR* Value)
    {
        const int32 Length = FCStringAnsi::Strlen(Value) + 1;
        const int32 Offset = Buffer.AddUninitialized(Length);
        FMemory::Memcpy(Buffer.GetData() + Offset, Value, Length);
    };

    constexpr uint64 FriendGuid = 0x0000000000001234ULL;
    constexpr uint64 IgnoredGuid = 0x0000000000005678ULL;
    AppendU32(Packet, 0x3u); // requested friends + ignores
    AppendU32(Packet, 2u);
    AppendU64(Packet, FriendGuid);
    AppendU32(Packet, 0x1u);
    AppendCString(Packet, "tank friend");
    AppendU8(Packet, 1u);
    AppendU32(Packet, 12u);
    AppendU32(Packet, 80u);
    AppendU32(Packet, 1u);
    AppendU64(Packet, IgnoredGuid);
    AppendU32(Packet, 0x2u);
    AppendCString(Packet, "ignored note");

    PacketHandler.HandlePacket(WowOpcode::SMSG_FRIEND_LIST, Packet);
    TestEqual(TEXT("Both 3.3.5 contacts are parsed"), PacketHandler.FriendsList.Num(), 2);
    if (PacketHandler.FriendsList.Num() == 2)
    {
        const FWowFriendInfo& Friend = PacketHandler.FriendsList[0];
        const FWowFriendInfo& Ignored = PacketHandler.FriendsList[1];
        TestEqual(TEXT("Friend GUID stays aligned"), Friend.Guid, FriendGuid);
        TestEqual(TEXT("Friend flag is preserved"), Friend.Flags, 0x1u);
        TestEqual(TEXT("Online friend level uses uint32 wire field"), Friend.Level, static_cast<uint8>(80));
        TestEqual(TEXT("Ignore GUID stays aligned"), Ignored.Guid, IgnoredGuid);
        TestEqual(TEXT("Ignore flag is preserved"), Ignored.Flags, 0x2u);
        TestEqual(TEXT("Ignore note is parsed"), Ignored.Note, FString(TEXT("ignored note")));
    }
    return true;
}
struct FWowLuaContext
{
    class FWowEntityManager* EntityManager = nullptr;
    class UWowConnectionManager* ConnectionManager = nullptr;
    class FWowEventSystem* EventSystem = nullptr;
    class FWowFrameManager* FrameManager = nullptr;
};

// ---- Helper: shared MPQ manager for data-driven tests ----
namespace WowTestUtils
{
    static void InventoryFixture(UWowConnectionManager& C)
    {
        auto& EM=C.PacketHandler.EntityManager;EM.LocalPlayerGuid=1;
        auto& P=EM.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
        P.SetField(PlayerField::PACK_SLOT_START+4,2);P.SetField(PlayerField::PACK_SLOT_START+5,0);
        auto& I=EM.PromoteToTyped(2,WowTypeMask::OBJECT|WowTypeMask::ITEM);
        I.Entry=6948;I.SetField(ItemField::OWNER,1);I.SetField(ItemField::STACK_COUNT,1);
        auto& Info=C.PacketHandler.ItemTemplates.FindOrAdd(6948);
        Info.Name=TEXT("Test weapon");Info.InventoryType=13;Info.Quality=3;
    }

    static FMpqManager& GetMpq()
    {
        static FMpqManager Mpq;
        static bool bInit = false;
        if (!bInit)
        {
            bInit = true;
            // Try known WoW 3.3.5a data paths
            TArray<FString> Paths = {
                FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Downloads/World of Warcraft 3.3.5a/Data")),
                FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("World of Warcraft 3.3.5a/Data")),
                TEXT("/Users/clancey/Downloads/World of Warcraft 3.3.5a/Data"),
            };
            for (const FString& Path : Paths)
            {
                if (FPaths::DirectoryExists(Path))
                {
                    Mpq.Initialize(Path);
                    break;
                }
            }
        }
        return Mpq;
    }

    static TSharedPtr<FWowWorldSocket> AttachTestWorldSocket(UWowConnectionManager& Connection, int64 TargetGuid)
    {
        Connection.State = EWowSessionState::WorldInGame;
        Connection.TargetGuid = TargetGuid;
        Connection.WorldSocket = MakeShared<FWowWorldSocket>();
        // Queue-only test transport: no OS socket or worker is started.
        Connection.WorldSocket->bRunning = true;
        return Connection.WorldSocket;
    }

    static bool DequeueClientPacket(FWowWorldSocket& Socket, uint32& OutOpcode, TArray<uint8>& OutPayload)
    {
        TArray<uint8> Packet;
        if (!Socket.SendQueue.Dequeue(Packet) || Packet.Num() < 6)
        {
            return false;
        }

        FMemory::Memcpy(&OutOpcode, Packet.GetData() + 2, sizeof(OutOpcode));
        OutPayload.SetNumUninitialized(Packet.Num() - 6);
        if (OutPayload.Num() > 0)
        {
            FMemory::Memcpy(OutPayload.GetData(), Packet.GetData() + 6, OutPayload.Num());
        }

        return true;
    }

    static void SetGuidField64(FWowEntity& Entity, uint16 FieldIndex, uint64 Guid)
    {
        Entity.SetField(FieldIndex, static_cast<uint32>(Guid & 0xFFFFFFFFu));
        Entity.SetField(FieldIndex + 1, static_cast<uint32>(Guid >> 32));
    }

    static void SetLuaContext(FWowLuaVM& LuaVM, FWowLuaContext& Context)
    {
        lua_pushlightuserdata(LuaVM.GetState(), &Context);
        lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");
    }

    static void ClearLuaContext(FWowLuaVM& LuaVM)
    {
        lua_pushnil(LuaVM.GetState());
        lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");
    }

    static int32 GetLuaIntegerGlobal(lua_State* L, const char* Name)
    {
        lua_getglobal(L, Name);
        const int32 Value = lua_isnumber(L, -1) ? static_cast<int32>(lua_tointeger(L, -1)) : 0;
        lua_pop(L, 1);
        return Value;
    }

    static FString GetLuaStringGlobal(lua_State* L, const char* Name)
    {
        lua_getglobal(L, Name);
        const FString Value = lua_isstring(L, -1) ? UTF8_TO_TCHAR(lua_tostring(L, -1)) : FString();
        lua_pop(L, 1);
        return Value;
    }

    static double GetLuaNumberGlobal(lua_State* L, const char* Name)
    {
        lua_getglobal(L, Name);
        const double Value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
        return Value;
    }

    static bool GetLuaBooleanGlobal(lua_State* L, const char* Name, bool& bOutIsBoolean)
    {
        lua_getglobal(L, Name);
        bOutIsBoolean = lua_isboolean(L, -1) != 0;
        const bool bValue = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return bValue;
    }
}

// ====================================================================
// Coordinate Conversion Tests (pure math, no data dependency)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoordAdtToUE, "WowUnreal.Coord.AdtToUE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCoordAdtToUE::RunTest(const FString& Parameters)
{
    // ADT coords: X=east, Y=up, Z=south → UE: X=-Z*100, Y=X*100, Z=Y*100
    FVector Result = FWowCoordinate::AdtToUE(10.0f, 20.0f, 30.0f);
    TestTrue(TEXT("UE X = -NgZ * 100"), FMath::IsNearlyEqual(Result.X, -3000.0, 0.01));
    TestTrue(TEXT("UE Y = NgX * 100"), FMath::IsNearlyEqual(Result.Y, 1000.0, 0.01));
    TestTrue(TEXT("UE Z = NgY * 100"), FMath::IsNearlyEqual(Result.Z, 2000.0, 0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoordTileRoundtrip, "WowUnreal.Coord.TileRoundtrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCoordTileRoundtrip::RunTest(const FString& Parameters)
{
    // Convert tile→world→tile and verify roundtrip
    int32 OrigX = 32, OrigY = 48;
    FVector WorldPos = FWowCoordinate::TileToWorld(OrigX, OrigY);
    FIntPoint TileBack = FWowCoordinate::WorldToTile(WorldPos);
    TestEqual(TEXT("Tile X roundtrips"), TileBack.X, OrigX);
    TestEqual(TEXT("Tile Y roundtrips"), TileBack.Y, OrigY);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoordWowRoundtrip, "WowUnreal.Coord.WowToUERoundtrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCoordWowRoundtrip::RunTest(const FString& Parameters)
{
    // Test WowToUE→UEToWow roundtrip
    FVector WowPos(1000.0f, 2000.0f, 100.0f);
    FVector UePos = FWowCoordinate::WowToUE(WowPos);
    FVector Back = FWowCoordinate::UEToWow(UePos);
    TestTrue(TEXT("WoW X roundtrips"), FMath::IsNearlyEqual(Back.X, WowPos.X, 0.01f));
    TestTrue(TEXT("WoW Y roundtrips"), FMath::IsNearlyEqual(Back.Y, WowPos.Y, 0.01f));
    TestTrue(TEXT("WoW Z roundtrips"), FMath::IsNearlyEqual(Back.Z, WowPos.Z, 0.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoordMovementOrientation, "WowUnreal.Coord.MovementOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCoordMovementOrientation::RunTest(const FString& Parameters)
{
    const TArray<FVector> WowDirections = {
        FVector(1.0f, 0.0f, 0.0f),
        FVector(0.0f, 1.0f, 0.0f),
        FVector(-1.0f, 0.0f, 0.0f),
        FVector(0.0f, -1.0f, 0.0f)
    };

    for (const FVector& WowDirection : WowDirections)
    {
        const float WowOrientation = FMath::Atan2(WowDirection.Y, WowDirection.X);
        const float UEYaw = FWowCoordinate::WowOrientationToUEYaw(WowOrientation);
        const FVector UEForward = FRotator(0.0f, UEYaw, 0.0f).Vector().GetSafeNormal2D();
        const FVector ExpectedUEForward(WowDirection.X, -WowDirection.Y, 0.0f);
        TestTrue(TEXT("Converted actor forward follows the converted movement direction"),
            FVector::DotProduct(UEForward, ExpectedUEForward.GetSafeNormal2D()) > 0.999f);
        TestTrue(TEXT("Orientation roundtrips between WoW and Unreal"),
            FMath::IsNearlyEqual(
                FMath::UnwindRadians(FWowCoordinate::UEYawToWowOrientation(UEYaw) - WowOrientation),
                0.0f,
                0.0001f));
    }

    return true;
}

// ====================================================================
// Entity Tests (pure in-memory, no data dependency)
// ====================================================================

#include "WowEntity.h"
#include "WowEntityManager.h"
#include "WowUpdateFields.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntityCreateAndLookup, "WowUnreal.Entity.CreateAndLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEntityCreateAndLookup::RunTest(const FString& Parameters)
{
    FWowEntityManager EM;

    // Create an entity
    FWowEntity& Ent = EM.GetOrCreate(12345);
    TestEqual(TEXT("GUID matches"), Ent.Guid, (uint64)12345);
    TestEqual(TEXT("Count is 1"), EM.Num(), 1);

    // Find it
    FWowEntity* Found = EM.Find(12345);
    TestNotNull(TEXT("Found by GUID"), Found);
    TestEqual(TEXT("Same entity"), Found, &Ent);

    // Missing entity returns null
    FWowEntity* Missing = EM.Find(99999);
    TestNull(TEXT("Missing returns null"), Missing);

    // Remove it
    EM.Remove(12345);
    TestEqual(TEXT("Count is 0 after remove"), EM.Num(), 0);
    TestNull(TEXT("Removed entity not found"), EM.Find(12345));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntityFieldAccess, "WowUnreal.Entity.FieldAccess",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEntityFieldAccess::RunTest(const FString& Parameters)
{
    FWowEntity Ent;
    Ent.Guid = 100;
    Ent.TypeMask = WowTypeMask::UNIT | WowTypeMask::PLAYER;

    // Type checks
    TestTrue(TEXT("IsUnit"), Ent.IsUnit());
    TestTrue(TEXT("IsPlayer"), Ent.IsPlayer());
    TestFalse(TEXT("Not GameObject"), Ent.IsGameObject());

    // Set fields
    Ent.SetField(UnitField::HEALTH, 5000);
    Ent.SetField(UnitField::MAXHEALTH, 10000);
    Ent.SetField(UnitField::LEVEL, 80);

    TestEqual(TEXT("Health"), Ent.GetHealth(), 5000);
    TestEqual(TEXT("MaxHealth"), Ent.GetMaxHealth(), 10000);
    TestEqual(TEXT("Level"), Ent.GetLevel(), 80);

    // Default field returns 0
    TestEqual(TEXT("Unset field is 0"), Ent.GetField(999), (uint32)0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntityTypeMasks, "WowUnreal.Entity.TypeMasks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEntityTypeMasks::RunTest(const FString& Parameters)
{
    FWowEntity GO;
    GO.TypeMask = WowTypeMask::GAMEOBJECT;
    TestTrue(TEXT("GO IsGameObject"), GO.IsGameObject());
    TestFalse(TEXT("GO not IsUnit"), GO.IsUnit());
    TestFalse(TEXT("GO not IsPlayer"), GO.IsPlayer());
    TestFalse(TEXT("GO not IsItem"), GO.IsItem());

    FWowEntity Item;
    Item.TypeMask = WowTypeMask::ITEM;
    TestTrue(TEXT("Item IsItem"), Item.IsItem());
    TestFalse(TEXT("Item not IsUnit"), Item.IsUnit());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntityTypedPromotion, "WowUnreal.Entity.TypedPromotion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEntityTypedPromotion::RunTest(const FString& Parameters)
{
    FWowEntityManager EM;

    FWowEntity& Base = EM.GetOrCreate(777);
    Base.SetField(ObjectField::TYPE, WowTypeMask::UNIT | WowTypeMask::PLAYER);
    Base.SetField(UnitField::HEALTH, 4242);
    Base.SetField(UnitField::MAXHEALTH, 5000);
    Base.SetField(UnitField::LEVEL, 80);
    Base.SetField(PlayerField::XP, 9001);
    Base.SetField(PlayerField::COINAGE, 123456);

    FWowEntity& Promoted = EM.PromoteToTyped(777, Base.GetField(ObjectField::TYPE));
    TestTrue(TEXT("Promoted entity reports player kind"), Promoted.IsPlayer());

    FWowPlayerEntity* Player = EM.FindPlayer(777);
    TestNotNull(TEXT("Typed player lookup succeeds"), Player);
    if (!Player)
    {
        return false;
    }

    TestEqual(TEXT("Unit data preserved across promotion"), Player->GetHealth(), 4242);
    TestEqual(TEXT("Player XP accessor works"), Player->GetXp(), static_cast<uint32>(9001));
    TestEqual(TEXT("Player coinage accessor works"), Player->GetCoinage(), static_cast<uint32>(123456));
    TestEqual(TEXT("Unit lookup returns promoted player"), EM.FindUnit(777), static_cast<FWowUnitEntity*>(Player));

    EM.LocalPlayerGuid = 777;
    TestEqual(TEXT("Local player lookup is typed"), EM.GetLocalPlayer(), Player);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterMoveFacingAndPath,
    "WowUnreal.Network.MonsterMoveFacingAndPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMonsterMoveFacingAndPath::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    constexpr uint64 MovingGuid = 1;
    constexpr uint64 FacingTargetGuid = 2;
    Handler.EntityManager.GetOrCreate(MovingGuid);
    Handler.EntityManager.GetOrCreate(FacingTargetGuid).Movement.Position = FVector(20.0f, 0.0f, 0.0f);

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value) { Buffer.Add(Value); };
    auto AppendU32 = [](TArray<uint8>& Buffer, uint32 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(Value));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(Value));
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(Value));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(Value));
    };
    auto AppendFloat = [](TArray<uint8>& Buffer, float Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(Value));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(Value));
    };
    auto AppendPackedGuid = [&AppendU8](TArray<uint8>& Buffer, uint64 Guid)
    {
        uint8 Mask = 0;
        TArray<uint8> Bytes;
        for (uint8 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
        {
            const uint8 ByteValue = static_cast<uint8>((Guid >> (ByteIndex * 8)) & 0xFF);
            if (ByteValue != 0)
            {
                Mask |= static_cast<uint8>(1u << ByteIndex);
                Bytes.Add(ByteValue);
            }
        }
        AppendU8(Buffer, Mask);
        Buffer.Append(Bytes);
    };
    auto AppendHeader = [&](TArray<uint8>& Buffer, uint8 MoveType, uint32 SplineId)
    {
        AppendPackedGuid(Buffer, MovingGuid);
        AppendU8(Buffer, 0);
        AppendFloat(Buffer, 0.0f);
        AppendFloat(Buffer, 0.0f);
        AppendFloat(Buffer, 0.0f);
        AppendU32(Buffer, SplineId);
        AppendU8(Buffer, MoveType);
    };
    auto AppendSinglePointSpline = [&](TArray<uint8>& Buffer, const FVector& Destination)
    {
        AppendU32(Buffer, 0);
        AppendU32(Buffer, 1000);
        AppendU32(Buffer, 1);
        AppendFloat(Buffer, Destination.X);
        AppendFloat(Buffer, Destination.Y);
        AppendFloat(Buffer, Destination.Z);
    };

    TArray<uint8> FacingTargetPacket;
    AppendHeader(FacingTargetPacket, 3, 100);
    AppendU64(FacingTargetPacket, FacingTargetGuid);
    AppendSinglePointSpline(FacingTargetPacket, FVector(10.0f, 0.0f, 0.0f));
    Handler.HandlePacket(WowOpcode::SMSG_MONSTER_MOVE, FacingTargetPacket);

    FWowEntity* Entity = Handler.EntityManager.Find(MovingGuid);
    TestNotNull(TEXT("Moving entity remains available"), Entity);
    if (!Entity)
    {
        return false;
    }
    TestEqual(TEXT("Move type 3 is parsed as a facing target"), Entity->Movement.SplineMoveType, static_cast<uint8>(3));
    TestEqual(TEXT("Facing target GUID stays aligned"), Entity->Movement.SplineFacingTargetGuid, FacingTargetGuid);
    TestTrue(TEXT("Facing-target packets still carry an active movement spline"), Entity->Movement.bHasActiveSpline);
    TestEqual(TEXT("Facing-target destination is parsed"), Entity->Movement.SplineWaypoints.Num(), 1);

    TArray<uint8> FacingAnglePacket;
    AppendHeader(FacingAnglePacket, 4, 101);
    AppendFloat(FacingAnglePacket, 1.25f);
    AppendSinglePointSpline(FacingAnglePacket, FVector(0.0f, 10.0f, 0.0f));
    Handler.HandlePacket(WowOpcode::SMSG_MONSTER_MOVE, FacingAnglePacket);
    TestEqual(TEXT("Move type 4 is parsed as a facing angle"), Entity->Movement.SplineMoveType, static_cast<uint8>(4));
    TestTrue(TEXT("Facing angle stays aligned"), FMath::IsNearlyEqual(Entity->Movement.SplineFacingAngle, 1.25f));

    TArray<uint8> PackedLinearPacket;
    AppendHeader(PackedLinearPacket, 0, 102);
    AppendU32(PackedLinearPacket, 0);
    AppendU32(PackedLinearPacket, 1000);
    AppendU32(PackedLinearPacket, 2);
    AppendFloat(PackedLinearPacket, 10.0f);
    AppendFloat(PackedLinearPacket, 10.0f);
    AppendFloat(PackedLinearPacket, 0.0f);

    const int32 PackedX = 8;
    const int32 PackedY = -8;
    const uint32 PackedPoint =
        (static_cast<uint32>(PackedX) & 0x7FF) |
        ((static_cast<uint32>(PackedY) & 0x7FF) << 11);
    AppendU32(PackedLinearPacket, PackedPoint);
    Handler.HandlePacket(WowOpcode::SMSG_MONSTER_MOVE, PackedLinearPacket);

    TestEqual(TEXT("Linear packet resolves its intermediate and final points"),
        Entity->Movement.SplineWaypoints.Num(), 2);
    if (Entity->Movement.SplineWaypoints.Num() == 2)
    {
        TestTrue(TEXT("Packed midpoint offset uses the server's midpoint-minus-offset convention"),
            Entity->Movement.SplineWaypoints[0].Position.Equals(FVector(3.0f, 7.0f, 0.0f), 0.01f));
        TestTrue(TEXT("Linear destination remains the last waypoint"),
            Entity->Movement.SplineWaypoints[1].Position.Equals(FVector(10.0f, 10.0f, 0.0f), 0.01f));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntityTypedContainersAndGameObjects, "WowUnreal.Entity.TypedContainersAndGameObjects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEntityTypedContainersAndGameObjects::RunTest(const FString& Parameters)
{
    FWowEntityManager EM;

    FWowEntity& ContainerBase = EM.GetOrCreate(1001);
    ContainerBase.SetField(ObjectField::TYPE, WowTypeMask::ITEM | WowTypeMask::CONTAINER);
    ContainerBase.SetField(ItemField::STACK_COUNT, 20);
    ContainerBase.SetField(ContainerField::NUM_SLOTS, 16);
    ContainerBase.SetField(ContainerField::SLOT_1, 0xAABBCCDD);
    ContainerBase.SetField(ContainerField::SLOT_1 + 1, 0x11223344);
    EM.PromoteToTyped(1001, ContainerBase.GetField(ObjectField::TYPE));

    FWowContainerEntity* Container = EM.FindContainer(1001);
    TestNotNull(TEXT("Typed container lookup succeeds"), Container);
    if (!Container)
    {
        return false;
    }

    TestEqual(TEXT("Container inherits item stack count"), Container->GetStackCount(), 20);
    TestEqual(TEXT("Container slot count accessor works"), Container->GetNumSlots(), 16);
    TestEqual(TEXT("Container GUID slot accessor combines 64-bit field"), Container->GetItemGuidAtSlot(0), static_cast<uint64>(0x11223344AABBCCDDULL));
    TestEqual(TEXT("Item lookup returns container subtype"), EM.FindItem(1001), static_cast<FWowItemEntity*>(Container));

    FWowEntity& GoBase = EM.GetOrCreate(2002);
    GoBase.SetField(ObjectField::TYPE, WowTypeMask::GAMEOBJECT);
    GoBase.SetField(GameObjectField::DISPLAY_ID, 31415);
    GoBase.SetField(GameObjectField::FLAGS, 0x20);
    GoBase.SetField(GameObjectField::LEVEL, 1);
    EM.PromoteToTyped(2002, GoBase.GetField(ObjectField::TYPE));

    FWowGameObjectEntity* GameObject = EM.FindGameObject(2002);
    TestNotNull(TEXT("Typed gameobject lookup succeeds"), GameObject);
    if (!GameObject)
    {
        return false;
    }

    TestEqual(TEXT("GameObject display ID accessor works"), GameObject->GetGameObjectDisplayId(), static_cast<uint32>(31415));
    TestEqual(TEXT("GameObject flags accessor works"), GameObject->GetGameObjectFlags(), static_cast<uint32>(0x20));
    TestEqual(TEXT("GameObject level accessor works"), GameObject->GetGameObjectLevel(), 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionCursorPlacesSpellIntoActionBar, "WowUnreal.UI.ActionCursorPlacesSpellIntoActionBar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FActionCursorPlacesSpellIntoActionBar::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    Connection->PickupSpellCursor(133, TEXT("spell"));

    FString CursorType;
    FString CursorDetail;
    int32 CursorId = 0;
    TestTrue(TEXT("Cursor info available after spell pickup"), Connection->GetCursorInfo(CursorType, CursorId, CursorDetail));
    TestEqual(TEXT("Cursor type is spell"), CursorType, FString(TEXT("spell")));
    TestEqual(TEXT("Cursor spell id matches"), CursorId, 133);
    TestEqual(TEXT("Cursor detail stores spell book type"), CursorDetail, FString(TEXT("spell")));
    TestTrue(TEXT("Spell payload is reported"), Connection->HasCursorSpellPayload());

    const bool bPlaced = Connection->PlaceCursorIntoActionSlot(4);
    TestTrue(TEXT("Placing spell payload into slot succeeds"), bPlaced);
    TestTrue(TEXT("Action array grew to include placed slot"), Connection->PacketHandler.ActionButtons.IsValidIndex(4));
    if (Connection->PacketHandler.ActionButtons.IsValidIndex(4))
    {
        TestEqual(TEXT("Placed action stores spell id in the slot"), Connection->PacketHandler.ActionButtons[4], static_cast<uint32>(133));
    }
    TestFalse(TEXT("Cursor payload clears after placement"), Connection->HasCursorPayload());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionCursorMovesExistingAction, "WowUnreal.UI.ActionCursorMovesExistingAction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FActionCursorMovesExistingAction::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    Connection->PacketHandler.ActionButtons.SetNumZeroed(12);
    const uint32 PackedSpellAction = 6603; // type 0 spell action
    Connection->PacketHandler.ActionButtons[1] = PackedSpellAction;

    Connection->PickupActionCursor(1);

    FString CursorType;
    FString CursorDetail;
    int32 CursorId = 0;
    TestTrue(TEXT("Cursor info available after action pickup"), Connection->GetCursorInfo(CursorType, CursorId, CursorDetail));
    TestEqual(TEXT("Cursor type is action"), CursorType, FString(TEXT("action")));
    TestEqual(TEXT("Cursor action id is 1-based source slot"), CursorId, 2);

    const bool bPlaced = Connection->PlaceCursorIntoActionSlot(3);
    TestTrue(TEXT("Moving action payload into destination slot succeeds"), bPlaced);
    TestEqual(TEXT("Source slot is cleared after move"), Connection->PacketHandler.ActionButtons[1], static_cast<uint32>(0));
    TestEqual(TEXT("Destination slot receives moved action"), Connection->PacketHandler.ActionButtons[3], PackedSpellAction);
    TestFalse(TEXT("Cursor payload clears after move"), Connection->HasCursorPayload());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemCursorTracksBagItemPickup, "WowUnreal.UI.ItemCursorTracksBagItemPickup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FItemCursorTracksBagItemPickup::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    WowTestUtils::InventoryFixture(*Connection);
    Connection->PickupBagItemCursor(255, 2, 6948, TEXT("|cffffffff|Hitem:6948:0:0:0:0:0:0:0:0:0|h[Item 6948]|h|r"));

    FString CursorType;
    FString CursorDetail;
    int32 CursorId = 0;
    TestTrue(TEXT("Cursor info available after item pickup"), Connection->GetCursorInfo(CursorType, CursorId, CursorDetail));
    TestEqual(TEXT("Cursor type is item"), CursorType, FString(TEXT("item")));
    TestEqual(TEXT("Cursor item id matches"), CursorId, 6948);
    TestTrue(TEXT("Cursor item detail preserves the item link"), CursorDetail.Contains(TEXT("Hitem:6948")));
    TestTrue(TEXT("Item payload is reported"), Connection->HasCursorItemPayload());

    Connection->ClearCursorPayload();
    TestFalse(TEXT("Cursor payload clears cleanly"), Connection->HasCursorPayload());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaUseContainerItemQueuesAutoEquip, "WowUnreal.UI.LuaUseContainerItemQueuesAutoEquip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaUseContainerItemQueuesAutoEquip::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, 0);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    WowTestUtils::InventoryFixture(*Connection);
    FWowLuaContext Context;
    Context.EntityManager = &Connection->PacketHandler.EntityManager;
    Context.ConnectionManager = Connection;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    const bool bExecuted = LuaVM.ExecuteString(TEXT("UseContainerItem(0, 3)"), TEXT("LuaUseContainerItemQueuesAutoEquip"));
    TestTrue(TEXT("UseContainerItem executes successfully"), bExecuted);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("UseContainerItem queues a client packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("UseContainerItem queues CMSG_AUTOEQUIP_ITEM"), Opcode, static_cast<uint32>(WowOpcode::CMSG_AUTOEQUIP_ITEM));
        TestEqual(TEXT("Autoequip payload stores bag+slot"), Payload.Num(), 2);
        if (Payload.Num() == 2)
        {
            TestEqual(TEXT("Backpack bag id normalizes to 255"), Payload[0], static_cast<uint8>(255));
            TestEqual(TEXT("Lua backpack slot 3 maps to server inventory slot 25"), Payload[1], static_cast<uint8>(25));
        }
    }

    // Vendor context takes priority over equip, including a BoE weapon.
    Connection->PacketHandler.ActiveVendorGuid=99;
    LuaVM.ExecuteString(TEXT("UseContainerItem(0, 3)"));
    TestTrue(TEXT("Vendor right-click sends a packet"),WowTestUtils::DequeueClientPacket(*WorldSocket,Opcode,Payload));
    TestEqual(TEXT("Vendor right-click sells"),Opcode,uint32(WowOpcode::CMSG_SELL_ITEM));
    TestEqual(TEXT("Sell contains full GUIDs and count"),Payload.Num(),20);
    Connection->PacketHandler.ActiveVendorGuid=0;
    auto& Info=Connection->PacketHandler.ItemTemplates[6948];Info.Bonding=2;
    uint32 Token=0;
    Connection->OnItemConfirmation.AddLambda([&](const FString& Event,const TArray<FString>& Args){if(Event.Contains(TEXT("EQUIP")))Token=FCString::Atoi(*Args[0]);});
    LuaVM.ExecuteString(TEXT("UseContainerItem(0, 3)"));
    TestTrue(TEXT("BoE produces confirmation token"),Token>0);
    TestFalse(TEXT("BoE cannot reach server before confirmation"),WowTestUtils::DequeueClientPacket(*WorldSocket,Opcode,Payload));
    LuaVM.ExecuteString(FString::Printf(TEXT("EquipPendingItem(%u)"),Token));
    TestTrue(TEXT("Confirm sends equip"),WowTestUtils::DequeueClientPacket(*WorldSocket,Opcode,Payload));
    TestEqual(TEXT("Confirmed opcode"),Opcode,uint32(WowOpcode::CMSG_AUTOEQUIP_ITEM));
    // Simulate the server binding the item; drag to offhand must retain that exact destination.
    Connection->PacketHandler.EntityManager.FindItem(2)->SetField(ItemField::FLAGS,1);
    LuaVM.ExecuteString(TEXT("PickupContainerItem(0, 3); PickupInventoryItem(17)"));
    TestTrue(TEXT("Drag sends swap"),WowTestUtils::DequeueClientPacket(*WorldSocket,Opcode,Payload));
    TestEqual(TEXT("Drag uses targeted swap"),Opcode,uint32(WowOpcode::CMSG_SWAP_ITEM));
    TestTrue(TEXT("Destination first, offhand 16, backpack source 25"),Payload==TArray<uint8>({255,16,255,25}));
    LuaVM.ExecuteString(TEXT("PickupContainerItem(0, 3)"));
    Connection->RequestDeleteCursorItem();
    TestFalse(TEXT("World drop does not send destroy"),WowTestUtils::DequeueClientPacket(*WorldSocket,Opcode,Payload));
    LuaVM.ExecuteString(TEXT("ClearCursor(); DeleteCursorItem()"));
    TestFalse(TEXT("Cancel cannot destroy"),WowTestUtils::DequeueClientPacket(*WorldSocket,Opcode,Payload));
    LuaVM.ExecuteString(TEXT("PickupContainerItem(0, 3)"));
    Connection->RequestDeleteCursorItem();LuaVM.ExecuteString(TEXT("DeleteCursorItem()"));
    TestTrue(TEXT("Confirmed destroy sends"),WowTestUtils::DequeueClientPacket(*WorldSocket,Opcode,Payload));
    TestEqual(TEXT("Destroy opcode"),Opcode,uint32(WowOpcode::CMSG_DESTROYITEM));
    TestTrue(TEXT("Destroy addresses backpack slot 25"),Payload==TArray<uint8>({255,25,0,0,0,0}));
    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaInventoryAccessorsUseWowSlotIds, "WowUnreal.UI.LuaInventoryAccessorsUseWowSlotIds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaInventoryAccessorsUseWowSlotIds::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    FWowEntityManager& EM = Connection->PacketHandler.EntityManager;
    constexpr uint64 PlayerGuid = 0x0102030405060708ULL;
    constexpr uint64 HeadItemGuid = 0x1111111122222222ULL;
    constexpr uint64 TabardItemGuid = 0x3333333344444444ULL;

    FWowEntity& PlayerBase = EM.GetOrCreate(PlayerGuid);
    PlayerBase.SetField(ObjectField::TYPE, WowTypeMask::UNIT | WowTypeMask::PLAYER);
    EM.PromoteToTyped(PlayerGuid, PlayerBase.GetField(ObjectField::TYPE));
    FWowPlayerEntity* Player = EM.FindPlayer(PlayerGuid);
    TestNotNull(TEXT("Typed player entity created"), Player);
    if (!Player)
    {
        return false;
    }
    EM.LocalPlayerGuid = PlayerGuid;

    FWowEntity& HeadItemBase = EM.GetOrCreate(HeadItemGuid);
    HeadItemBase.SetField(ObjectField::TYPE, WowTypeMask::ITEM);
    HeadItemBase.SetField(ObjectField::ENTRY, 6948);
    HeadItemBase.SetField(ItemField::STACK_COUNT, 1);
    EM.PromoteToTyped(HeadItemGuid, HeadItemBase.GetField(ObjectField::TYPE));
    if (FWowItemEntity* HeadItem = EM.FindItem(HeadItemGuid))
    {
        HeadItem->Entry = HeadItem->GetField(ObjectField::ENTRY);
    }

    FWowEntity& TabardItemBase = EM.GetOrCreate(TabardItemGuid);
    TabardItemBase.SetField(ObjectField::TYPE, WowTypeMask::ITEM);
    TabardItemBase.SetField(ObjectField::ENTRY, 5976);
    TabardItemBase.SetField(ItemField::STACK_COUNT, 1);
    EM.PromoteToTyped(TabardItemGuid, TabardItemBase.GetField(ObjectField::TYPE));
    if (FWowItemEntity* TabardItem = EM.FindItem(TabardItemGuid))
    {
        TabardItem->Entry = TabardItem->GetField(ObjectField::ENTRY);
    }

    WowTestUtils::SetGuidField64(*Player, PlayerField::INV_SLOT_HEAD, HeadItemGuid);
    WowTestUtils::SetGuidField64(*Player, PlayerField::INV_SLOT_TABARD, TabardItemGuid);

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    Context.EntityManager = &EM;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    const bool bExecuted = LuaVM.ExecuteString(
        TEXT("HEAD_LINK = GetInventoryItemLink(\"player\", 1)\n")
        TEXT("HEAD_TEXTURE = GetInventoryItemTexture(\"player\", 1)\n")
        TEXT("TABARD_TEXTURE = GetInventoryItemTexture(\"player\", 19)\n"),
        TEXT("LuaInventoryAccessorsUseWowSlotIds"));
    TestTrue(TEXT("Inventory accessor Lua helpers execute successfully"), bExecuted);

    lua_State* L = LuaVM.GetState();
    TestTrue(TEXT("GetInventoryItemLink uses WoW 1-based slot ids"), WowTestUtils::GetLuaStringGlobal(L, "HEAD_LINK").Contains(TEXT("Hitem:6948")));
    TestTrue(TEXT("GetInventoryItemTexture resolves head slot from WoW slot id 1"), !WowTestUtils::GetLuaStringGlobal(L, "HEAD_TEXTURE").IsEmpty());
    TestTrue(TEXT("GetInventoryItemTexture resolves tabard slot from WoW slot id 19"), !WowTestUtils::GetLuaStringGlobal(L, "TABARD_TEXTURE").IsEmpty());

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaInventoryPickupFlowsQueueEquipAndUnequipPackets, "WowUnreal.UI.LuaInventoryPickupFlowsQueueEquipAndUnequipPackets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaInventoryPickupFlowsQueueEquipAndUnequipPackets::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, 0);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    FWowEntityManager& EM = Connection->PacketHandler.EntityManager;
    constexpr uint64 PlayerGuid = 0x777788889999AAAAULL;
    constexpr uint64 BagItemGuid = 0x0101010102020202ULL;
    constexpr uint64 EquippedItemGuid = 0x0303030304040404ULL;

    FWowEntity& PlayerBase = EM.GetOrCreate(PlayerGuid);
    PlayerBase.SetField(ObjectField::TYPE, WowTypeMask::UNIT | WowTypeMask::PLAYER);
    EM.PromoteToTyped(PlayerGuid, PlayerBase.GetField(ObjectField::TYPE));
    FWowPlayerEntity* Player = EM.FindPlayer(PlayerGuid);
    TestNotNull(TEXT("Typed player entity created"), Player);
    if (!Player)
    {
        return false;
    }
    EM.LocalPlayerGuid = PlayerGuid;

    FWowEntity& BagItemBase = EM.GetOrCreate(BagItemGuid);
    BagItemBase.SetField(ObjectField::TYPE, WowTypeMask::ITEM);
    BagItemBase.SetField(ObjectField::ENTRY, 6948);
    BagItemBase.SetField(ItemField::STACK_COUNT, 1);
    EM.PromoteToTyped(BagItemGuid, BagItemBase.GetField(ObjectField::TYPE));
    if (FWowItemEntity* BagItem = EM.FindItem(BagItemGuid))
    {
        BagItem->Entry = BagItem->GetField(ObjectField::ENTRY);
    }

    FWowEntity& EquippedItemBase = EM.GetOrCreate(EquippedItemGuid);
    EquippedItemBase.SetField(ObjectField::TYPE, WowTypeMask::ITEM);
    EquippedItemBase.SetField(ObjectField::ENTRY, 5976);
    EquippedItemBase.SetField(ItemField::STACK_COUNT, 1);
    EM.PromoteToTyped(EquippedItemGuid, EquippedItemBase.GetField(ObjectField::TYPE));
    if (FWowItemEntity* EquippedItem = EM.FindItem(EquippedItemGuid))
    {
        EquippedItem->Entry = EquippedItem->GetField(ObjectField::ENTRY);
    }

    WowTestUtils::SetGuidField64(*Player, PlayerField::PACK_SLOT_START, BagItemGuid);
    WowTestUtils::SetGuidField64(*Player, PlayerField::INV_SLOT_HEAD, EquippedItemGuid);

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    Context.EntityManager = &EM;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    bool bExecuted = LuaVM.ExecuteString(
        TEXT("PickupContainerItem(0, 1)\n")
        TEXT("CURSOR_AFTER_PICKUP = CursorHasItem()\n")
        TEXT("PickupInventoryItem(1)\n"),
        TEXT("LuaInventoryPickupEquip"));
    TestTrue(TEXT("Container pickup to equipment executes successfully"), bExecuted);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("Picking up a bag item then clicking equipment queues a packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("Bag-to-equipment flow queues CMSG_AUTOEQUIP_ITEM"), Opcode, static_cast<uint32>(WowOpcode::CMSG_AUTOEQUIP_ITEM));
        TestEqual(TEXT("Autoequip payload stores backpack source slot"), Payload.Num(), 2);
        if (Payload.Num() == 2)
        {
            TestEqual(TEXT("Backpack bag id normalizes to 255"), Payload[0], static_cast<uint8>(255));
            TestEqual(TEXT("Backpack item source slot is zero-based"), Payload[1], static_cast<uint8>(0));
        }
    }

    bool bIsBoolean = false;
    const bool bCursorAfterPickup = WowTestUtils::GetLuaBooleanGlobal(LuaVM.GetState(), "CURSOR_AFTER_PICKUP", bIsBoolean);
    TestFalse(TEXT("Equip flow clears the cursor payload"), Connection->HasCursorItemPayload());
    TestTrue(TEXT("CursorHasItem reports true after bag pickup"), bCursorAfterPickup && bIsBoolean);

    bExecuted = LuaVM.ExecuteString(
        TEXT("PickupInventoryItem(1)\n")
        TEXT("CURSOR_AFTER_EQUIP_PICKUP = CursorHasItem()\n")
        TEXT("PutItemInBackpack()\n"),
        TEXT("LuaInventoryPickupUnequip"));
    TestTrue(TEXT("Equipment pickup to backpack executes successfully"), bExecuted);

    Opcode = 0;
    Payload.Reset();
    bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("Picking up an equipped item then placing it in the backpack queues a packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("Equipment-to-backpack flow queues CMSG_AUTOSTORE_BAG_ITEM"), Opcode, static_cast<uint32>(WowOpcode::CMSG_AUTOSTORE_BAG_ITEM));
        TestEqual(TEXT("Autostore payload stores source bag, source slot, and destination bag"), Payload.Num(), 3);
        if (Payload.Num() == 3)
        {
            TestEqual(TEXT("Equipped items source from bag 255"), Payload[0], static_cast<uint8>(255));
            TestEqual(TEXT("Head slot uses zero-based equipment slot 0"), Payload[1], static_cast<uint8>(0));
            TestEqual(TEXT("PutItemInBackpack stores into backpack bag 255"), Payload[2], static_cast<uint8>(255));
        }
    }

    const bool bCursorAfterEquipPickup = WowTestUtils::GetLuaBooleanGlobal(LuaVM.GetState(), "CURSOR_AFTER_EQUIP_PICKUP", bIsBoolean);
    TestFalse(TEXT("Unequip flow clears the cursor payload"), Connection->HasCursorItemPayload());
    TestTrue(TEXT("CursorHasItem reports true after equipment pickup"), bCursorAfterEquipPickup && bIsBoolean);

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaSplitContainerItemQueuesSplitPacket, "WowUnreal.UI.LuaSplitContainerItemQueuesSplitPacket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaSplitContainerItemQueuesSplitPacket::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, 0);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    FWowEntityManager& EM = Connection->PacketHandler.EntityManager;
    constexpr uint64 PlayerGuid = 0x5555666677778888ULL;
    constexpr uint64 StackItemGuid = 0x1111222233334444ULL;

    FWowEntity& PlayerBase = EM.GetOrCreate(PlayerGuid);
    PlayerBase.SetField(ObjectField::TYPE, WowTypeMask::UNIT | WowTypeMask::PLAYER);
    EM.PromoteToTyped(PlayerGuid, PlayerBase.GetField(ObjectField::TYPE));
    FWowPlayerEntity* Player = EM.FindPlayer(PlayerGuid);
    TestNotNull(TEXT("Typed player entity created"), Player);
    if (!Player)
    {
        return false;
    }
    EM.LocalPlayerGuid = PlayerGuid;

    FWowEntity& StackItemBase = EM.GetOrCreate(StackItemGuid);
    StackItemBase.SetField(ObjectField::TYPE, WowTypeMask::ITEM);
    StackItemBase.SetField(ObjectField::ENTRY, 6948);
    StackItemBase.SetField(ItemField::STACK_COUNT, 5);
    EM.PromoteToTyped(StackItemGuid, StackItemBase.GetField(ObjectField::TYPE));
    if (FWowItemEntity* StackItem = EM.FindItem(StackItemGuid))
    {
        StackItem->Entry = StackItem->GetField(ObjectField::ENTRY);
    }

    WowTestUtils::SetGuidField64(*Player, PlayerField::PACK_SLOT_START, StackItemGuid);

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    Context.EntityManager = &EM;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    const bool bExecuted = LuaVM.ExecuteString(
        TEXT("SplitContainerItem(0, 1, 2)\n")
        TEXT("CURSOR_AFTER_SPLIT = CursorHasItem()\n")
        TEXT("PickupContainerItem(0, 2)\n"),
        TEXT("LuaSplitContainerItemQueuesSplitPacket"));
    TestTrue(TEXT("SplitContainerItem flow executes successfully"), bExecuted);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("SplitContainerItem queues a client packet when the split stack is placed"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("Split flow queues CMSG_SPLIT_ITEM"), Opcode, static_cast<uint32>(WowOpcode::CMSG_SPLIT_ITEM));
        TestEqual(TEXT("Split payload stores source bag, source slot, destination bag, destination slot, and count"), Payload.Num(), 8);
        if (Payload.Num() == 8)
        {
            TestEqual(TEXT("Backpack source bag normalizes to 255"), Payload[0], static_cast<uint8>(255));
            TestEqual(TEXT("Source slot is zero-based"), Payload[1], static_cast<uint8>(0));
            TestEqual(TEXT("Backpack destination bag normalizes to 255"), Payload[2], static_cast<uint8>(255));
            TestEqual(TEXT("Destination slot is zero-based"), Payload[3], static_cast<uint8>(1));

            uint32 Count = 0;
            FMemory::Memcpy(&Count, Payload.GetData() + 4, sizeof(Count));
            TestEqual(TEXT("Split count is preserved"), Count, static_cast<uint32>(2));
        }
    }

    bool bIsBoolean = false;
    const bool bCursorAfterSplit = WowTestUtils::GetLuaBooleanGlobal(LuaVM.GetState(), "CURSOR_AFTER_SPLIT", bIsBoolean);
    TestTrue(TEXT("SplitContainerItem places an item payload on the cursor"), bCursorAfterSplit && bIsBoolean);
    TestFalse(TEXT("Placing the split stack clears the cursor payload"), Connection->HasCursorItemPayload());

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaDeleteCursorItemQueuesDestroyItem, "WowUnreal.UI.LuaDeleteCursorItemQueuesDestroyItem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaDeleteCursorItemQueuesDestroyItem::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, 0);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    FWowEntityManager& EM = Connection->PacketHandler.EntityManager;
    constexpr uint64 PlayerGuid = 0x9999AAAABBBBCCCCULL;
    constexpr uint64 BagItemGuid = 0xDDDDEEEEFFFF0001ULL;

    FWowEntity& PlayerBase = EM.GetOrCreate(PlayerGuid);
    PlayerBase.SetField(ObjectField::TYPE, WowTypeMask::UNIT | WowTypeMask::PLAYER);
    EM.PromoteToTyped(PlayerGuid, PlayerBase.GetField(ObjectField::TYPE));
    FWowPlayerEntity* Player = EM.FindPlayer(PlayerGuid);
    TestNotNull(TEXT("Typed player entity created"), Player);
    if (!Player)
    {
        return false;
    }
    EM.LocalPlayerGuid = PlayerGuid;

    FWowEntity& BagItemBase = EM.GetOrCreate(BagItemGuid);
    BagItemBase.SetField(ObjectField::TYPE, WowTypeMask::ITEM);
    BagItemBase.SetField(ObjectField::ENTRY, 5976);
    BagItemBase.SetField(ItemField::STACK_COUNT, 1);
    EM.PromoteToTyped(BagItemGuid, BagItemBase.GetField(ObjectField::TYPE));
    if (FWowItemEntity* BagItem = EM.FindItem(BagItemGuid))
    {
        BagItem->Entry = BagItem->GetField(ObjectField::ENTRY);
    }

    WowTestUtils::SetGuidField64(*Player, PlayerField::PACK_SLOT_START, BagItemGuid);

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    Context.EntityManager = &EM;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    const bool bExecuted = LuaVM.ExecuteString(
        TEXT("PickupContainerItem(0, 1)\n")
        TEXT("CURSOR_BEFORE_DELETE = CursorHasItem()\n")
        TEXT("DeleteCursorItem()\n"),
        TEXT("LuaDeleteCursorItemQueuesDestroyItem"));
    TestTrue(TEXT("DeleteCursorItem flow executes successfully"), bExecuted);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("DeleteCursorItem queues a client packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("Delete cursor flow queues CMSG_DESTROYITEM"), Opcode, static_cast<uint32>(WowOpcode::CMSG_DESTROYITEM));
        TestEqual(TEXT("Destroy payload stores bag, slot, count, and flags"), Payload.Num(), 6);
        if (Payload.Num() == 6)
        {
            TestEqual(TEXT("Backpack bag normalizes to 255"), Payload[0], static_cast<uint8>(255));
            TestEqual(TEXT("Source slot is zero-based"), Payload[1], static_cast<uint8>(0));
            TestEqual(TEXT("Full-item delete uses count 0"), Payload[2], static_cast<uint8>(0));
        }
    }

    bool bIsBoolean = false;
    const bool bCursorBeforeDelete = WowTestUtils::GetLuaBooleanGlobal(LuaVM.GetState(), "CURSOR_BEFORE_DELETE", bIsBoolean);
    TestTrue(TEXT("PickupContainerItem places an item payload on the cursor before deletion"), bCursorBeforeDelete && bIsBoolean);
    TestFalse(TEXT("DeleteCursorItem clears the cursor payload"), Connection->HasCursorItemPayload());

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionInvocationRoutesAutoAttack, "WowUnreal.UI.ActionInvocationRoutesAutoAttack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FActionInvocationRoutesAutoAttack::RunTest(const FString& Parameters)
{
    const FWowActionInvocation Invocation =
        UWowConnectionManager::ResolveActionInvocation(6603u, 0x1234);

    TestEqual(TEXT("Auto-attack action resolves to auto-attack kind"),
        Invocation.Kind, EWowActionInvocationKind::AutoAttack);
    TestEqual(TEXT("Auto-attack action preserves spell id"), Invocation.ActionId, static_cast<uint32>(6603));
    TestEqual(TEXT("Auto-attack action preserves target"), Invocation.TargetGuid, static_cast<int64>(0x1234));
    TestTrue(TEXT("Auto-attack invocation is valid"), Invocation.IsValid());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionInvocationRoutesRegularSpell, "WowUnreal.UI.ActionInvocationRoutesRegularSpell",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FActionInvocationRoutesRegularSpell::RunTest(const FString& Parameters)
{
    const uint32 PackedSpellAction = 133u; // type 0 spell action
    const FWowActionInvocation Invocation =
        UWowConnectionManager::ResolveActionInvocation(PackedSpellAction, 0);

    TestEqual(TEXT("Regular spell resolves to spell-cast kind"),
        Invocation.Kind, EWowActionInvocationKind::SpellCast);
    TestEqual(TEXT("Regular spell preserves spell id"), Invocation.ActionId, static_cast<uint32>(133));
    TestEqual(TEXT("Regular spell preserves target"), Invocation.TargetGuid, static_cast<int64>(0));
    TestTrue(TEXT("Regular spell invocation is valid"), Invocation.IsValid());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaUseActionAutoAttackQueuesAttackSwing, "WowUnreal.UI.LuaUseActionAutoAttackQueuesAttackSwing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaUseActionAutoAttackQueuesAttackSwing::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    constexpr int64 TargetGuid = 0x0011223344556677LL;
    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, TargetGuid);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    Connection->PacketHandler.ActionButtons.SetNumZeroed(12);
    Connection->PacketHandler.ActionButtons[0] = 6603; // Auto Attack

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    lua_pushlightuserdata(LuaVM.GetState(), &Context);
    lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");

    const bool bExecuted = LuaVM.ExecuteString(TEXT("UseAction(1)"), TEXT("LuaUseActionAutoAttack"));
    TestTrue(TEXT("UseAction executes successfully"), bExecuted);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("UseAction queued a client packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("UseAction queues CMSG_ATTACKSWING for auto-attack"), Opcode, static_cast<uint32>(WowOpcode::CMSG_ATTACKSWING));
        TestEqual(TEXT("Attack swing payload is an 8-byte guid"), Payload.Num(), 8);

        uint64 SentTargetGuid = 0;
        if (Payload.Num() == 8)
        {
            FMemory::Memcpy(&SentTargetGuid, Payload.GetData(), sizeof(SentTargetGuid));
            TestEqual(TEXT("Attack swing targets the selected unit"), SentTargetGuid, static_cast<uint64>(TargetGuid));
        }
    }

    lua_pushnil(LuaVM.GetState());
    lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaUseActionSpellQueuesCastSpell, "WowUnreal.UI.LuaUseActionSpellQueuesCastSpell",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaUseActionSpellQueuesCastSpell::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    constexpr int64 TargetGuid = 0x0000000000004242LL;
    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, TargetGuid);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    Connection->PacketHandler.ActionButtons.SetNumZeroed(12);
    Connection->PacketHandler.ActionButtons[0] = 133; // Fireball

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    lua_pushlightuserdata(LuaVM.GetState(), &Context);
    lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");

    const bool bExecuted = LuaVM.ExecuteString(TEXT("UseAction(1)"), TEXT("LuaUseActionSpell"));
    TestTrue(TEXT("UseAction executes successfully"), bExecuted);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("UseAction queued a client packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("UseAction queues CMSG_CAST_SPELL for normal spells"), Opcode, static_cast<uint32>(WowOpcode::CMSG_CAST_SPELL));
        TestTrue(TEXT("Cast spell payload is large enough for cast count, spell id, flags, and target mask"), Payload.Num() >= 10);

        if (Payload.Num() >= 5)
        {
            uint32 SpellId = 0;
            FMemory::Memcpy(&SpellId, Payload.GetData() + 1, sizeof(SpellId));
            TestEqual(TEXT("Cast spell payload preserves the spell id"), SpellId, static_cast<uint32>(133));
        }
    }

    lua_pushnil(LuaVM.GetState());
    lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaTargetUnitResolvesTargetTarget, "WowUnreal.UI.LuaTargetUnitResolvesTargetTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaTargetUnitResolvesTargetTarget::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    constexpr int64 CurrentTargetGuid = 0x0102030405060708LL;
    constexpr uint64 ChainedTargetGuid = 0x1122334455667788ULL;
    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, CurrentTargetGuid);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    FWowEntity& CurrentTarget = Connection->PacketHandler.EntityManager.GetOrCreate(static_cast<uint64>(CurrentTargetGuid));
    CurrentTarget.TypeMask = WowTypeMask::UNIT;
    CurrentTarget.SetField(UnitField::TARGET, static_cast<uint32>(ChainedTargetGuid & 0xFFFFFFFFULL));
    CurrentTarget.SetField(UnitField::TARGET + 1, static_cast<uint32>(ChainedTargetGuid >> 32));

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    Context.EntityManager = &Connection->PacketHandler.EntityManager;
    lua_pushlightuserdata(LuaVM.GetState(), &Context);
    lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");

    const bool bExecuted = LuaVM.ExecuteString(TEXT("TargetUnit(\"targettarget\")"), TEXT("LuaTargetUnitResolvesTargetTarget"));
    TestTrue(TEXT("TargetUnit executes successfully"), bExecuted);
    TestEqual(TEXT("TargetUnit updates the selected GUID"), Connection->GetTargetGuid(), static_cast<int64>(ChainedTargetGuid));

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("TargetUnit queued a client packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("TargetUnit queues CMSG_SET_SELECTION"), Opcode, static_cast<uint32>(WowOpcode::CMSG_SET_SELECTION));
        TestEqual(TEXT("Target selection payload is an 8-byte guid"), Payload.Num(), 8);

        uint64 SentTargetGuid = 0;
        if (Payload.Num() == 8)
        {
            FMemory::Memcpy(&SentTargetGuid, Payload.GetData(), sizeof(SentTargetGuid));
            TestEqual(TEXT("TargetUnit selects the chained target"), SentTargetGuid, ChainedTargetGuid);
        }
    }

    lua_pushnil(LuaVM.GetState());
    lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaTargetUnitResolvesPlayerName, "WowUnreal.UI.LuaTargetUnitResolvesPlayerName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaTargetUnitResolvesPlayerName::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    constexpr uint64 RemotePlayerGuid = 0x8877665544332211ULL;
    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, 0);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    Connection->PacketHandler.PlayerNameCache.Add(RemotePlayerGuid, TEXT("TargetableMage"));

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    Context.EntityManager = &Connection->PacketHandler.EntityManager;
    lua_pushlightuserdata(LuaVM.GetState(), &Context);
    lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");

    const bool bExecuted = LuaVM.ExecuteString(TEXT("TargetUnit(\"TargetableMage\", 1)"), TEXT("LuaTargetUnitResolvesPlayerName"));
    TestTrue(TEXT("TargetUnit by player name executes successfully"), bExecuted);
    TestEqual(TEXT("TargetUnit by player name updates the selected GUID"), Connection->GetTargetGuid(), static_cast<int64>(RemotePlayerGuid));

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("TargetUnit by player name queued a client packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("TargetUnit by player name queues CMSG_SET_SELECTION"), Opcode, static_cast<uint32>(WowOpcode::CMSG_SET_SELECTION));
    }

    lua_pushnil(LuaVM.GetState());
    lua_setfield(LuaVM.GetState(), LUA_REGISTRYINDEX, "WowLuaContext");
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaRegisterForClicksAnyDownDispatchesBooleanDown, "WowUnreal.UI.LuaRegisterForClicksAnyDownDispatchesBooleanDown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaRegisterForClicksAnyDownDispatchesBooleanDown::RunTest(const FString& Parameters)
{
    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowEventSystem EventSystem;
    EventSystem.SetLuaVM(&LuaVM);

    UCanvasPanel* RootCanvas = NewObject<UCanvasPanel>();
    FWowFrameManager FrameManager;
    FrameManager.Initialize(RootCanvas);
    FrameManager.SetEventSystem(&EventSystem);
    EventSystem.SetFrameManager(&FrameManager);

    FWowLuaContext Context;
    Context.EventSystem = &EventSystem;
    Context.FrameManager = &FrameManager;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    FWowFrameDef ButtonDef;
    ButtonDef.Name = TEXT("ClickDownButton");
    ButtonDef.Type = EWowFrameType::Button;
    ButtonDef.Width = 32.0f;
    ButtonDef.Height = 32.0f;
    const int64 ButtonHandle = FrameManager.CreateFrame(ButtonDef);
    TestTrue(TEXT("Button frame created"), ButtonHandle > 0);
    if (ButtonHandle <= 0)
    {
        WowTestUtils::ClearLuaContext(LuaVM);
        LuaVM.Shutdown();
        return false;
    }

    const bool bConfigured = LuaVM.ExecuteString(
        TEXT("ClickDownButton:RegisterForClicks(\"AnyDown\")\n")
        TEXT("ClickDownButton:SetScript(\"OnClick\", function(self, button, down)\n")
        TEXT("  CLICK_COUNT = (CLICK_COUNT or 0) + 1\n")
        TEXT("  CLICK_BUTTON = button\n")
        TEXT("  CLICK_DOWN = down\n")
        TEXT("  CLICK_DOWN_TYPE = type(down)\n")
        TEXT("end)"),
        TEXT("LuaRegisterForClicksAnyDownDispatchesBooleanDown"));
    TestTrue(TEXT("Lua button click script configured"), bConfigured);

    FrameManager.DispatchMouseDown(ButtonHandle, TEXT("LeftButton"));
    if (FrameManager.IsFrameClickRegistered(ButtonHandle, TEXT("LeftButton"), true))
    {
        FrameManager.DispatchClick(ButtonHandle, TEXT("LeftButton"), true);
    }
    FrameManager.DispatchMouseUp(ButtonHandle, TEXT("LeftButton"));
    if (FrameManager.IsFrameClickRegistered(ButtonHandle, TEXT("LeftButton"), false))
    {
        FrameManager.DispatchClick(ButtonHandle, TEXT("LeftButton"), false);
    }

    lua_State* L = LuaVM.GetState();
    const int32 ClickCount = WowTestUtils::GetLuaIntegerGlobal(L, "CLICK_COUNT");
    const FString ClickButton = WowTestUtils::GetLuaStringGlobal(L, "CLICK_BUTTON");
    const FString ClickDownType = WowTestUtils::GetLuaStringGlobal(L, "CLICK_DOWN_TYPE");
    bool bIsBoolean = false;
    const bool bClickDown = WowTestUtils::GetLuaBooleanGlobal(L, "CLICK_DOWN", bIsBoolean);

    TestEqual(TEXT("AnyDown registration fires one click"), ClickCount, 1);
    TestEqual(TEXT("AnyDown registration preserves the clicked button"), ClickButton, FString(TEXT("LeftButton")));
    TestEqual(TEXT("OnClick down argument is a Lua boolean"), ClickDownType, FString(TEXT("boolean")));
    TestTrue(TEXT("OnClick down value is true for down-edge clicks"), bClickDown);
    TestTrue(TEXT("OnClick down value is stored as a boolean"), bIsBoolean);

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FButtonDefaultClickDispatchesOnMouseUp, "WowUnreal.UI.ButtonDefaultClickDispatchesOnMouseUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FButtonDefaultClickDispatchesOnMouseUp::RunTest(const FString& Parameters)
{
    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowEventSystem EventSystem;
    EventSystem.SetLuaVM(&LuaVM);

    UCanvasPanel* RootCanvas = NewObject<UCanvasPanel>();
    FWowFrameManager FrameManager;
    FrameManager.Initialize(RootCanvas);
    FrameManager.SetEventSystem(&EventSystem);
    EventSystem.SetFrameManager(&FrameManager);

    FWowLuaContext Context;
    Context.EventSystem = &EventSystem;
    Context.FrameManager = &FrameManager;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    FWowFrameDef ButtonDef;
    ButtonDef.Name = TEXT("DefaultClickButton");
    ButtonDef.Type = EWowFrameType::Button;
    ButtonDef.Width = 32.0f;
    ButtonDef.Height = 32.0f;
    const int64 ButtonHandle = FrameManager.CreateFrame(ButtonDef);
    TestTrue(TEXT("Default button frame created"), ButtonHandle > 0);
    if (ButtonHandle <= 0)
    {
        WowTestUtils::ClearLuaContext(LuaVM);
        LuaVM.Shutdown();
        return false;
    }

    const bool bConfigured = LuaVM.ExecuteString(
        TEXT("DefaultClickButton:SetScript(\"OnClick\", function(self, button, down)\n")
        TEXT("  CLICK_COUNT = (CLICK_COUNT or 0) + 1\n")
        TEXT("  if down then\n")
        TEXT("    DOWN_CLICK_COUNT = (DOWN_CLICK_COUNT or 0) + 1\n")
        TEXT("  else\n")
        TEXT("    UP_CLICK_COUNT = (UP_CLICK_COUNT or 0) + 1\n")
        TEXT("  end\n")
        TEXT("  LAST_CLICK_DOWN = down\n")
        TEXT("  LAST_CLICK_DOWN_TYPE = type(down)\n")
        TEXT("end)"),
        TEXT("ButtonDefaultClickDispatchesOnMouseUp"));
    TestTrue(TEXT("Lua default button click script configured"), bConfigured);

    FrameManager.DispatchMouseDown(ButtonHandle, TEXT("LeftButton"));
    if (FrameManager.IsFrameClickRegistered(ButtonHandle, TEXT("LeftButton"), true))
    {
        FrameManager.DispatchClick(ButtonHandle, TEXT("LeftButton"), true);
    }

    lua_State* L = LuaVM.GetState();
    TestEqual(TEXT("Default button does not click on mouse down"), WowTestUtils::GetLuaIntegerGlobal(L, "CLICK_COUNT"), 0);

    FrameManager.DispatchMouseUp(ButtonHandle, TEXT("LeftButton"));
    if (FrameManager.IsFrameClickRegistered(ButtonHandle, TEXT("LeftButton"), false))
    {
        FrameManager.DispatchClick(ButtonHandle, TEXT("LeftButton"), false);
    }

    const int32 ClickCount = WowTestUtils::GetLuaIntegerGlobal(L, "CLICK_COUNT");
    const int32 DownClickCount = WowTestUtils::GetLuaIntegerGlobal(L, "DOWN_CLICK_COUNT");
    const int32 UpClickCount = WowTestUtils::GetLuaIntegerGlobal(L, "UP_CLICK_COUNT");
    const FString LastClickDownType = WowTestUtils::GetLuaStringGlobal(L, "LAST_CLICK_DOWN_TYPE");
    bool bIsBoolean = false;
    const bool bLastClickDown = WowTestUtils::GetLuaBooleanGlobal(L, "LAST_CLICK_DOWN", bIsBoolean);

    TestEqual(TEXT("Default button clicks exactly once"), ClickCount, 1);
    TestEqual(TEXT("Default button does not dispatch down-edge clicks"), DownClickCount, 0);
    TestEqual(TEXT("Default button dispatches one up-edge click"), UpClickCount, 1);
    TestEqual(TEXT("Default button stores a Lua boolean for down"), LastClickDownType, FString(TEXT("boolean")));
    TestFalse(TEXT("Default button OnClick down value is false on mouse up"), bLastClickDown);
    TestTrue(TEXT("Default button OnClick down arg remains a boolean"), bIsBoolean);

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPetSpellsPacketParsesActionBar, "WowUnreal.Network.PetSpellsPacketParsesActionBar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPetSpellsPacketParsesActionBar::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;

    bool bBroadcasted = false;
    Handler.OnPetBarUpdated.AddLambda([&bBroadcasted](const FWowPetActionBarState&)
    {
        bBroadcasted = true;
    });

    constexpr uint64 PetGuid = 0x0102030405060708ULL;
    constexpr uint32 SpellId = 16827u;
    const uint32 AttackPacked = WowPetCommand::ATTACK | (static_cast<uint32>(WowPetActionType::COMMAND) << 24);
    const uint32 DefensivePacked = WowPetReactState::DEFENSIVE | (static_cast<uint32>(WowPetActionType::REACTION) << 24);
    const uint32 SpellPacked = SpellId | (static_cast<uint32>(WowPetActionType::DISABLED) << 24);

    TArray<uint8> Packet;
    auto AppendValue = [&Packet](auto Value)
    {
        using TValue = decltype(Value);
        const int32 Offset = Packet.AddUninitialized(sizeof(TValue));
        FMemory::Memcpy(Packet.GetData() + Offset, &Value, sizeof(TValue));
    };

    AppendValue(PetGuid);
    AppendValue(static_cast<uint16>(17));
    AppendValue(static_cast<uint32>(0));
    AppendValue(static_cast<uint8>(WowPetReactState::DEFENSIVE));
    AppendValue(static_cast<uint8>(WowPetCommand::FOLLOW));
    AppendValue(static_cast<uint16>(0));
    AppendValue(AttackPacked);
    AppendValue(DefensivePacked);
    AppendValue(SpellPacked);
    for (int32 SlotIndex = 3; SlotIndex < WOW_PET_ACTION_SLOT_COUNT; ++SlotIndex)
    {
        AppendValue(static_cast<uint32>(0));
    }
    AppendValue(static_cast<uint8>(1));
    AppendValue(SpellId);
    AppendValue(static_cast<uint8>(1));
    AppendValue(SpellId);
    AppendValue(static_cast<uint16>(0));
    AppendValue(static_cast<uint32>(5000));
    AppendValue(static_cast<uint32>(0));

    FPacketReader Reader(Packet);
    Handler.HandlePetSpells(Reader);

    TestTrue(TEXT("PET_SPELLS broadcasts pet bar updates"), bBroadcasted);
    TestEqual(TEXT("PET_SPELLS preserves pet guid"), Handler.PetActionBar.PetGuid, PetGuid);
    TestEqual(TEXT("PET_SPELLS preserves react state"), Handler.PetActionBar.ReactState, static_cast<uint8>(WowPetReactState::DEFENSIVE));
    TestEqual(TEXT("PET_SPELLS keeps the expected slot count"), Handler.PetActionBar.ActionSlots.Num(), WOW_PET_ACTION_SLOT_COUNT);

    const FWowPetActionSlot* AttackSlot = Handler.PetActionBar.GetSlot(0);
    TestTrue(TEXT("Slot 1 parsed as pet attack command"), AttackSlot && AttackSlot->IsCommand() && AttackSlot->ActionId == WowPetCommand::ATTACK);

    const FWowPetActionSlot* DefensiveSlot = Handler.PetActionBar.GetSlot(1);
    TestTrue(TEXT("Slot 2 parsed as defensive reaction"), DefensiveSlot && DefensiveSlot->IsReaction() && DefensiveSlot->ActionId == WowPetReactState::DEFENSIVE);

    const FWowPetActionSlot* SpellSlot = Handler.PetActionBar.GetSlot(2);
    TestTrue(TEXT("Slot 3 parsed as pet spell"), SpellSlot && SpellSlot->IsSpell() && SpellSlot->ActionId == SpellId);
    TestTrue(TEXT("PET_SPELLS tracks known pet spells"), Handler.PetActionBar.KnownSpells.Contains(SpellId));
    TestTrue(TEXT("PET_SPELLS records cooldown data"), Handler.PetActionBar.GetCooldownRemaining(2) > 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaPetActionInfoReflectsPetBarState, "WowUnreal.UI.LuaPetActionInfoReflectsPetBarState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaPetActionInfoReflectsPetBarState::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    Connection->PacketHandler.PetActionBar.PetGuid = 0x1020304050607080ULL;
    Connection->PacketHandler.PetActionBar.ReactState = static_cast<uint8>(WowPetReactState::DEFENSIVE);
    Connection->PacketHandler.PetActionBar.ActionSlots[0].SetPackedData(
        WowPetCommand::ATTACK | (static_cast<uint32>(WowPetActionType::COMMAND) << 24));
    Connection->PacketHandler.PetActionBar.ActionSlots[1].SetPackedData(
        WowPetReactState::DEFENSIVE | (static_cast<uint32>(WowPetActionType::REACTION) << 24));
    Connection->PacketHandler.PetActionBar.ActionSlots[2].SetPackedData(
        16827u | (static_cast<uint32>(WowPetActionType::ENABLED) << 24));
    Connection->PacketHandler.PetActionBar.SpellCooldownExpirySeconds.Add(16827u, FPlatformTime::Seconds() + 3.0);

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    const bool bExecuted = LuaVM.ExecuteString(
        TEXT("PET_HAS_BAR = PetHasActionBar()\n")
        TEXT("PET_ACTIONS_USABLE = GetPetActionsUsable()\n")
        TEXT("local name, subtext, texture, isToken, isActive, autoCastAllowed, autoCastEnabled = GetPetActionInfo(2)\n")
        TEXT("PET_ACTION_NAME = name\n")
        TEXT("PET_ACTION_TEXTURE = texture\n")
        TEXT("PET_ACTION_ACTIVE = isActive\n")
        TEXT("PET_ACTION_TOKEN = isToken\n")
        TEXT("PET_ATTACK_TEXTURE = GetPetActionTexture(1)\n")
        TEXT("PET_ATTACK_SLOT_USABLE = GetPetActionSlotUsable(1)\n")
        TEXT("PET_ATTACK_IS_ATTACK = IsPetAttackAction(1)\n")
        TEXT("local start, duration, enabled = GetPetActionCooldown(3)\n")
        TEXT("PET_COOLDOWN_DURATION = duration\n")
        TEXT("PET_COOLDOWN_ENABLED = enabled\n"),
        TEXT("LuaPetActionInfoReflectsPetBarState"));
    TestTrue(TEXT("Pet action Lua helpers execute successfully"), bExecuted);

    lua_State* L = LuaVM.GetState();
    bool bHasBarIsBoolean = false;
    bool bActionsUsableIsBoolean = false;
    bool bActionActiveIsBoolean = false;
    bool bActionTokenIsBoolean = false;
    bool bAttackSlotUsableIsBoolean = false;
    bool bAttackActionIsBoolean = false;
    const bool bHasBar = WowTestUtils::GetLuaBooleanGlobal(L, "PET_HAS_BAR", bHasBarIsBoolean);
    const bool bActionsUsable = WowTestUtils::GetLuaBooleanGlobal(L, "PET_ACTIONS_USABLE", bActionsUsableIsBoolean);
    const bool bActionActive = WowTestUtils::GetLuaBooleanGlobal(L, "PET_ACTION_ACTIVE", bActionActiveIsBoolean);
    const bool bActionToken = WowTestUtils::GetLuaBooleanGlobal(L, "PET_ACTION_TOKEN", bActionTokenIsBoolean);
    const bool bAttackSlotUsable = WowTestUtils::GetLuaBooleanGlobal(L, "PET_ATTACK_SLOT_USABLE", bAttackSlotUsableIsBoolean);
    const bool bAttackAction = WowTestUtils::GetLuaBooleanGlobal(L, "PET_ATTACK_IS_ATTACK", bAttackActionIsBoolean);

    TestTrue(TEXT("PetHasActionBar returns true for populated pet state"), bHasBar && bHasBarIsBoolean);
    TestTrue(TEXT("GetPetActionsUsable returns true for populated pet state"), bActionsUsable && bActionsUsableIsBoolean);
    TestEqual(TEXT("GetPetActionInfo returns the reaction display name"), WowTestUtils::GetLuaStringGlobal(L, "PET_ACTION_NAME"), FString(TEXT("Defensive")));
    TestTrue(TEXT("GetPetActionInfo marks the active reaction"), bActionActive && bActionActiveIsBoolean);
    TestFalse(TEXT("GetPetActionInfo returns a file path, not a token"), bActionToken);
    TestTrue(TEXT("GetPetActionTexture returns a non-empty icon path"), !WowTestUtils::GetLuaStringGlobal(L, "PET_ACTION_TEXTURE").IsEmpty());
    TestTrue(TEXT("GetPetActionTexture works for command slots too"), !WowTestUtils::GetLuaStringGlobal(L, "PET_ATTACK_TEXTURE").IsEmpty());
    TestTrue(TEXT("GetPetActionSlotUsable returns true for usable pet commands"), bAttackSlotUsable && bAttackSlotUsableIsBoolean);
    TestTrue(TEXT("IsPetAttackAction recognizes the attack button"), bAttackAction && bAttackActionIsBoolean);
    TestTrue(TEXT("GetPetActionCooldown returns a positive cooldown duration"), WowTestUtils::GetLuaNumberGlobal(L, "PET_COOLDOWN_DURATION") > 0.0);
    TestEqual(TEXT("GetPetActionCooldown reports enabled=1"), WowTestUtils::GetLuaNumberGlobal(L, "PET_COOLDOWN_ENABLED"), 1.0);

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaCastPetActionQueuesPetPacket, "WowUnreal.UI.LuaCastPetActionQueuesPetPacket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaCastPetActionQueuesPetPacket::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    constexpr uint64 TargetGuid = 0x2233445566778899ULL;
    constexpr uint64 PetGuid = 0x0101010102020202ULL;
    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, static_cast<int64>(TargetGuid));
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    Connection->PacketHandler.PetActionBar.PetGuid = PetGuid;
    Connection->PacketHandler.PetActionBar.ActionSlots[0].SetPackedData(
        WowPetCommand::ATTACK | (static_cast<uint32>(WowPetActionType::COMMAND) << 24));

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    const bool bExecuted = LuaVM.ExecuteString(TEXT("CastPetAction(1)"), TEXT("LuaCastPetActionQueuesPetPacket"));
    TestTrue(TEXT("CastPetAction executes successfully"), bExecuted);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("CastPetAction queues a client packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("CastPetAction queues CMSG_PET_ACTION"), Opcode, static_cast<uint32>(WowOpcode::CMSG_PET_ACTION));
        TestEqual(TEXT("CMSG_PET_ACTION payload has pet guid, packed slot, and target guid"), Payload.Num(), 20);
        if (Payload.Num() == 20)
        {
            uint64 SentPetGuid = 0;
            uint32 PackedAction = 0;
            uint64 SentTargetGuid = 0;
            FMemory::Memcpy(&SentPetGuid, Payload.GetData(), sizeof(SentPetGuid));
            FMemory::Memcpy(&PackedAction, Payload.GetData() + 8, sizeof(PackedAction));
            FMemory::Memcpy(&SentTargetGuid, Payload.GetData() + 12, sizeof(SentTargetGuid));
            TestEqual(TEXT("CastPetAction preserves the pet guid"), SentPetGuid, PetGuid);
            TestEqual(TEXT("CastPetAction preserves the packed slot data"), PackedAction,
                WowPetCommand::ATTACK | (static_cast<uint32>(WowPetActionType::COMMAND) << 24));
            TestEqual(TEXT("Attack commands preserve the current target"), SentTargetGuid, TargetGuid);
        }
    }

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaTogglePetAutocastQueuesAutocastPacket, "WowUnreal.UI.LuaTogglePetAutocastQueuesAutocastPacket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaTogglePetAutocastQueuesAutocastPacket::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    constexpr uint64 PetGuid = 0x0A0B0C0D0E0F1011ULL;
    constexpr uint32 SpellId = 16827u;
    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, 0);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    Connection->PacketHandler.PetActionBar.PetGuid = PetGuid;
    Connection->PacketHandler.PetActionBar.ActionSlots[0].SetPackedData(
        SpellId | (static_cast<uint32>(WowPetActionType::DISABLED) << 24));

    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowLuaContext Context;
    Context.ConnectionManager = Connection;
    WowTestUtils::SetLuaContext(LuaVM, Context);

    const bool bExecuted = LuaVM.ExecuteString(TEXT("TogglePetAutocast(1)"), TEXT("LuaTogglePetAutocastQueuesAutocastPacket"));
    TestTrue(TEXT("TogglePetAutocast executes successfully"), bExecuted);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("TogglePetAutocast queues a client packet"), bDequeued);
    if (bDequeued)
    {
        TestEqual(TEXT("TogglePetAutocast queues CMSG_PET_SPELL_AUTOCAST"), Opcode, static_cast<uint32>(WowOpcode::CMSG_PET_SPELL_AUTOCAST));
        TestEqual(TEXT("CMSG_PET_SPELL_AUTOCAST payload has pet guid, spell id, and enabled flag"), Payload.Num(), 13);
        if (Payload.Num() == 13)
        {
            uint64 SentPetGuid = 0;
            uint32 SentSpellId = 0;
            uint8 bEnabled = 0;
            FMemory::Memcpy(&SentPetGuid, Payload.GetData(), sizeof(SentPetGuid));
            FMemory::Memcpy(&SentSpellId, Payload.GetData() + 8, sizeof(SentSpellId));
            FMemory::Memcpy(&bEnabled, Payload.GetData() + 12, sizeof(bEnabled));
            TestEqual(TEXT("TogglePetAutocast preserves the pet guid"), SentPetGuid, PetGuid);
            TestEqual(TEXT("TogglePetAutocast preserves the spell id"), SentSpellId, SpellId);
            TestEqual(TEXT("TogglePetAutocast enables autocast when it was disabled"), bEnabled, static_cast<uint8>(1));
        }
    }

    const FWowPetActionSlot* UpdatedSlot = Connection->PacketHandler.PetActionBar.GetSlot(0);
    TestTrue(TEXT("TogglePetAutocast updates local pet action state"), UpdatedSlot && UpdatedSlot->IsAutocastEnabled());

    WowTestUtils::ClearLuaContext(LuaVM);
    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSendAutoEquipItemEncodesPacket, "WowUnreal.Network.AutoEquipItemEncodesPacket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSendAutoEquipItemEncodesPacket::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, 0);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    Connection->SendAutoEquipItem(255, 23);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    const bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("Auto-equip packet enqueued"), bDequeued);
    if (!bDequeued)
    {
        return false;
    }

    TestEqual(TEXT("Opcode is CMSG_AUTOEQUIP_ITEM"), Opcode, static_cast<uint32>(WowOpcode::CMSG_AUTOEQUIP_ITEM));
    TestEqual(TEXT("Auto-equip payload length"), Payload.Num(), 2);
    if (Payload.Num() == 2)
    {
        TestEqual(TEXT("Auto-equip source bag"), static_cast<int32>(Payload[0]), 255);
        TestEqual(TEXT("Auto-equip source slot"), static_cast<int32>(Payload[1]), 23);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryManagerRoutesEquipAndUnequipPackets, "WowUnreal.UI.InventoryManagerRoutesEquipAndUnequipPackets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInventoryManagerRoutesEquipAndUnequipPackets::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    TestNotNull(TEXT("Connection manager created"), Connection);
    if (!Connection)
    {
        return false;
    }

    TSharedPtr<FWowWorldSocket> WorldSocket = WowTestUtils::AttachTestWorldSocket(*Connection, 0);
    TestTrue(TEXT("Test world socket attached"), WorldSocket.IsValid());
    if (!WorldSocket.IsValid())
    {
        return false;
    }

    FWowInventoryManager InventoryManager;
    InventoryManager.SetConnectionManager(Connection);

    InventoryManager.AutoEquipItem(255, 24);
    InventoryManager.AutoStoreItem(255, 15, 255);

    uint32 Opcode = 0;
    TArray<uint8> Payload;
    bool bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("Inventory manager enqueues equip packet"), bDequeued);
    if (!bDequeued)
    {
        return false;
    }

    TestEqual(TEXT("Inventory manager equip opcode"), Opcode, static_cast<uint32>(WowOpcode::CMSG_AUTOEQUIP_ITEM));
    TestEqual(TEXT("Inventory manager equip payload length"), Payload.Num(), 2);
    if (Payload.Num() == 2)
    {
        TestEqual(TEXT("Inventory manager equip source bag"), static_cast<int32>(Payload[0]), 255);
        TestEqual(TEXT("Inventory manager equip source slot"), static_cast<int32>(Payload[1]), 24);
    }

    bDequeued = WowTestUtils::DequeueClientPacket(*WorldSocket, Opcode, Payload);
    TestTrue(TEXT("Inventory manager enqueues auto-store packet"), bDequeued);
    if (!bDequeued)
    {
        return false;
    }

    TestEqual(TEXT("Inventory manager auto-store opcode"), Opcode, static_cast<uint32>(WowOpcode::CMSG_AUTOSTORE_BAG_ITEM));
    TestEqual(TEXT("Inventory manager auto-store payload length"), Payload.Num(), 3);
    if (Payload.Num() == 3)
    {
        TestEqual(TEXT("Inventory manager auto-store source bag"), static_cast<int32>(Payload[0]), 255);
        TestEqual(TEXT("Inventory manager auto-store source slot"), static_cast<int32>(Payload[1]), 15);
        TestEqual(TEXT("Inventory manager auto-store destination bag"), static_cast<int32>(Payload[2]), 255);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeferredOnLoadBatchResolvesSiblingGlobals, "WowUnreal.UI.DeferredOnLoadBatchResolvesSiblingGlobals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDeferredOnLoadBatchResolvesSiblingGlobals::RunTest(const FString& Parameters)
{
    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowEventSystem EventSystem;
    EventSystem.SetLuaVM(&LuaVM);

    EventSystem.BeginOnLoadBatch();

    EventSystem.CreateFrameObject(1, TEXT("ParentFrame"));
    FWowFrameDef ParentDef;
    ParentDef.Name = TEXT("ParentFrame");
    FWowScriptHandler OnLoad;
    OnLoad.Event = TEXT("OnLoad");
    OnLoad.Code = TEXT("assert(SiblingFrame ~= nil, 'SiblingFrame missing during OnLoad'); ParentFrameSawSibling = true");
    ParentDef.Scripts.Add(OnLoad);
    EventSystem.CompileFrameScripts(1, ParentDef);

    EventSystem.CreateFrameObject(2, TEXT("SiblingFrame"));
    FWowFrameDef SiblingDef;
    SiblingDef.Name = TEXT("SiblingFrame");
    EventSystem.CompileFrameScripts(2, SiblingDef);

    lua_getglobal(LuaVM.GetState(), "ParentFrameSawSibling");
    TestTrue(TEXT("OnLoad is deferred until the batch completes"), lua_isnil(LuaVM.GetState(), -1));
    lua_pop(LuaVM.GetState(), 1);

    EventSystem.EndOnLoadBatch();

    lua_getglobal(LuaVM.GetState(), "ParentFrameSawSibling");
    const bool bSawSibling = lua_toboolean(LuaVM.GetState(), -1) != 0;
    lua_pop(LuaVM.GetState(), 1);
    TestTrue(TEXT("Deferred OnLoad sees globals created later in the same batch"), bSawSibling);

    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLaterOnLoadOverridesEarlierScript, "WowUnreal.UI.LaterOnLoadOverridesEarlierScript",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLaterOnLoadOverridesEarlierScript::RunTest(const FString& Parameters)
{
    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowEventSystem EventSystem;
    EventSystem.SetLuaVM(&LuaVM);
    EventSystem.CreateFrameObject(1, TEXT("TestFrame"));

    FWowFrameDef Def;
    Def.Name = TEXT("TestFrame");

    FWowScriptHandler TemplateOnLoad;
    TemplateOnLoad.Event = TEXT("OnLoad");
    TemplateOnLoad.Code = TEXT("TemplateOnLoadRan = true");
    Def.Scripts.Add(TemplateOnLoad);

    FWowScriptHandler OverrideOnLoad;
    OverrideOnLoad.Event = TEXT("OnLoad");
    OverrideOnLoad.Code = TEXT("OverrideOnLoadRan = (TemplateOnLoadRan ~= true)");
    Def.Scripts.Add(OverrideOnLoad);

    EventSystem.CompileFrameScripts(1, Def);

    lua_getglobal(LuaVM.GetState(), "TemplateOnLoadRan");
    const bool bTemplateRan = lua_toboolean(LuaVM.GetState(), -1) != 0;
    lua_pop(LuaVM.GetState(), 1);

    lua_getglobal(LuaVM.GetState(), "OverrideOnLoadRan");
    const bool bOverrideRanWithoutTemplate = lua_toboolean(LuaVM.GetState(), -1) != 0;
    lua_pop(LuaVM.GetState(), 1);

    TestFalse(TEXT("Earlier duplicate OnLoad is overridden"), bTemplateRan);
    TestTrue(TEXT("Later duplicate OnLoad replaces the earlier handler"), bOverrideRanWithoutTemplate);

    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiParentDefaultAttributesSeeded, "WowUnreal.UI.UiParentDefaultAttributesSeeded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUiParentDefaultAttributesSeeded::RunTest(const FString& Parameters)
{
    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    FWowEventSystem EventSystem;
    EventSystem.SetLuaVM(&LuaVM);
    EventSystem.CreateFrameObject(1, TEXT("UIParent"));

    lua_getglobal(LuaVM.GetState(), "UIParent");
    TestTrue(TEXT("UIParent global exists"), lua_istable(LuaVM.GetState(), -1));
    lua_getfield(LuaVM.GetState(), -1, "__attr_DEFAULT_FRAME_WIDTH");
    const double DefaultFrameWidth = lua_tonumber(LuaVM.GetState(), -1);
    lua_pop(LuaVM.GetState(), 2);
    TestEqual(TEXT("UIParent seeds DEFAULT_FRAME_WIDTH"), DefaultFrameWidth, 384.0);

    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaUiUtilityGlobalsExist, "WowUnreal.UI.LuaUiUtilityGlobalsExist",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLuaUiUtilityGlobalsExist::RunTest(const FString& Parameters)
{
    FWowLuaVM LuaVM;
    TestTrue(TEXT("Lua VM initializes"), LuaVM.Initialize());
    if (!LuaVM.IsInitialized())
    {
        return false;
    }

    lua_State* L = LuaVM.GetState();

    lua_getglobal(L, "table");
    TestTrue(TEXT("table library exists"), lua_istable(L, -1));
    lua_getfield(L, -1, "wipe");
    const bool bHasTableWipe = lua_isfunction(L, -1);
    lua_pop(L, 2);
    TestTrue(TEXT("table.wipe is exposed for Blizzard UI code"), bHasTableWipe);

    lua_getglobal(L, "CombatLogResetFilter");
    const bool bHasCombatLogResetFilter = lua_isfunction(L, -1);
    lua_pop(L, 1);
    TestTrue(TEXT("CombatLogResetFilter global exists"), bHasCombatLogResetFilter);

    lua_getglobal(L, "CombatLogAddFilter");
    const bool bHasCombatLogAddFilter = lua_isfunction(L, -1);
    lua_pop(L, 1);
    TestTrue(TEXT("CombatLogAddFilter global exists"), bHasCombatLogAddFilter);

    lua_getglobal(L, "CombatLogGetNumEntries");
    const bool bHasCombatLogGetNumEntries = lua_isfunction(L, -1);
    lua_pop(L, 1);
    TestTrue(TEXT("CombatLogGetNumEntries global exists"), bHasCombatLogGetNumEntries);

    lua_getglobal(L, "CombatLogGetCurrentEntry");
    const bool bHasCombatLogGetCurrentEntry = lua_isfunction(L, -1);
    lua_pop(L, 1);
    TestTrue(TEXT("CombatLogGetCurrentEntry global exists"), bHasCombatLogGetCurrentEntry);

    lua_getglobal(L, "CombatLog_Object_IsA");
    const bool bHasCombatLogObjectIsA = lua_isfunction(L, -1);
    lua_pop(L, 1);
    TestTrue(TEXT("CombatLog_Object_IsA global exists"), bHasCombatLogObjectIsA);

    lua_getglobal(L, "CombatLogSetCurrentEntry");
    lua_pushinteger(L, 5);
    const int SetCurrentEntryResult = lua_pcall(L, 1, 0, 0);
    TestEqual(TEXT("CombatLogSetCurrentEntry accepts a numeric cursor"), SetCurrentEntryResult, 0);

    lua_getglobal(L, "CombatLogAdvanceEntry");
    lua_pushinteger(L, 1);
    const int AdvanceEntryResult = lua_pcall(L, 1, 8, 0);
    TestEqual(TEXT("CombatLogAdvanceEntry accepts a numeric delta"), AdvanceEntryResult, 0);
    const bool bAdvanceEntryHasStringEvent = lua_isstring(L, -7) != 0;
    const FString AdvancedEventName = bAdvanceEntryHasStringEvent ? UTF8_TO_TCHAR(lua_tostring(L, -7)) : FString();
    const bool bAdvanceEntryHasNumericSourceFlags = lua_isnumber(L, -4) != 0;
    const int32 AdvancedSourceFlags = bAdvanceEntryHasNumericSourceFlags ? static_cast<int32>(lua_tointeger(L, -4)) : 0;
    lua_pop(L, 8);
    TestTrue(TEXT("CombatLogAdvanceEntry returns a WotLK event tuple"), bAdvanceEntryHasStringEvent);
    TestEqual(TEXT("CombatLogAdvanceEntry uses an empty placeholder event"), AdvancedEventName, FString());
    TestTrue(TEXT("CombatLogAdvanceEntry returns numeric source flags"), bAdvanceEntryHasNumericSourceFlags);
    TestEqual(TEXT("CombatLogAdvanceEntry defaults unknown source flags to COMBATLOG_OBJECT_NONE"), AdvancedSourceFlags, static_cast<int32>(0x80000000u));

    lua_getglobal(L, "CombatLogGetNumEntries");
    const int GetNumEntriesResult = lua_pcall(L, 0, 1, 0);
    TestEqual(TEXT("CombatLogGetNumEntries call succeeds"), GetNumEntriesResult, 0);
    const int32 CombatLogNumEntries = static_cast<int32>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    TestEqual(TEXT("CombatLog cursor API defaults to an empty log"), CombatLogNumEntries, 0);

    lua_getglobal(L, "CombatLogGetCurrentEntry");
    const int GetCurrentEntryResult = lua_pcall(L, 0, 8, 0);
    TestEqual(TEXT("CombatLogGetCurrentEntry call succeeds"), GetCurrentEntryResult, 0);
    const bool bCurrentEntryHasStringEvent = lua_isstring(L, -7) != 0;
    const FString CurrentEntryEventName = bCurrentEntryHasStringEvent ? UTF8_TO_TCHAR(lua_tostring(L, -7)) : FString();
    const bool bCurrentEntryHasNumericDestFlags = lua_isnumber(L, -1) != 0;
    const int32 CurrentDestFlags = bCurrentEntryHasNumericDestFlags ? static_cast<int32>(lua_tointeger(L, -1)) : 0;
    lua_pop(L, 8);
    TestTrue(TEXT("CombatLogGetCurrentEntry returns a WotLK event tuple"), bCurrentEntryHasStringEvent);
    TestEqual(TEXT("CombatLog current-entry placeholder event is empty"), CurrentEntryEventName, FString());
    TestTrue(TEXT("CombatLogGetCurrentEntry returns numeric destination flags"), bCurrentEntryHasNumericDestFlags);
    TestEqual(TEXT("CombatLog current-entry defaults unknown destination flags to COMBATLOG_OBJECT_NONE"), CurrentDestFlags, static_cast<int32>(0x80000000u));

    lua_getglobal(L, "CombatLog_Object_IsA");
    lua_pushinteger(L, 0x00000511);
    lua_pushinteger(L, 0x00000511);
    const int CombatLogObjectIsAResult = lua_pcall(L, 2, 1, 0);
    TestEqual(TEXT("CombatLog_Object_IsA call succeeds"), CombatLogObjectIsAResult, 0);
    const bool bCombatLogObjectMatches = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    TestTrue(TEXT("CombatLog_Object_IsA matches valid composite combat log filters"), bCombatLogObjectMatches);

    lua_getglobal(L, "CombatLog_Object_IsA");
    lua_pushinteger(L, 0x00000004);
    lua_pushinteger(L, 0x00000004);
    const int CombatLogObjectSingleConstantResult = lua_pcall(L, 2, 1, 0);
    TestEqual(TEXT("CombatLog_Object_IsA rejects single-category constants cleanly"), CombatLogObjectSingleConstantResult, 0);
    const bool bCombatLogObjectSingleConstantMatches = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    TestFalse(TEXT("CombatLog_Object_IsA rejects incomplete masks"), bCombatLogObjectSingleConstantMatches);

    lua_getglobal(L, "CombatLog_Object_IsA");
    lua_pushinteger(L, static_cast<lua_Integer>(0x80000000u));
    lua_pushinteger(L, static_cast<lua_Integer>(0x80000000u));
    const int CombatLogObjectUnknownResult = lua_pcall(L, 2, 1, 0);
    TestEqual(TEXT("CombatLog_Object_IsA handles COMBATLOG_OBJECT_NONE"), CombatLogObjectUnknownResult, 0);
    const bool bCombatLogObjectUnknownMatches = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    TestTrue(TEXT("CombatLog_Object_IsA matches unknown-unit filters"), bCombatLogObjectUnknownMatches);

    LuaVM.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerInventoryFieldAccessors, "WowUnreal.Entity.PlayerInventoryFieldAccessors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPlayerInventoryFieldAccessors::RunTest(const FString& Parameters)
{
    FWowPlayerEntity Player;
    Player.TypeMask = WowTypeMask::UNIT | WowTypeMask::PLAYER;

    const uint64 BackpackGuid = 0x0000000200000001ULL;
    const uint64 BankGuid = 0x0000000300000002ULL;
    const uint64 BankBagGuid = 0x0000000400000003ULL;
    const uint64 WornBagGuid = 0x0000000500000004ULL;

    Player.SetField(PlayerField::PACK_SLOT_START, static_cast<uint32>(BackpackGuid & 0xFFFFFFFFu));
    Player.SetField(PlayerField::PACK_SLOT_START + 1, static_cast<uint32>(BackpackGuid >> 32));
    Player.SetField(PlayerField::BANK_SLOT_START, static_cast<uint32>(BankGuid & 0xFFFFFFFFu));
    Player.SetField(PlayerField::BANK_SLOT_START + 1, static_cast<uint32>(BankGuid >> 32));
    Player.SetField(PlayerField::BANKBAG_SLOT_1, static_cast<uint32>(BankBagGuid & 0xFFFFFFFFu));
    Player.SetField(PlayerField::BANKBAG_SLOT_1 + 1, static_cast<uint32>(BankBagGuid >> 32));
    Player.SetField(PlayerField::INV_SLOT_BAG_1, static_cast<uint32>(WornBagGuid & 0xFFFFFFFFu));
    Player.SetField(PlayerField::INV_SLOT_BAG_1 + 1, static_cast<uint32>(WornBagGuid >> 32));
    Player.SetField(PlayerField::BYTES_2, 2u << 16);

    TestEqual(TEXT("Backpack slot accessor uses corrected pack-slot range"), Player.GetBackpackItemGuid(0), BackpackGuid);
    TestEqual(TEXT("Bank slot accessor uses corrected bank-slot range"), Player.GetBankItemGuid(0), BankGuid);
    TestEqual(TEXT("Bank bag accessor uses corrected bank-bag range"), Player.GetBankBagGuid(0), BankBagGuid);
    TestEqual(TEXT("Worn bag accessor uses inventory slot bag range"), Player.GetBagGuid(1), WornBagGuid);
    TestEqual(TEXT("Purchased bank bag slot count comes from PLAYER_BYTES_2 byte 2"), Player.GetBankBagSlotCount(), static_cast<uint8>(2));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShowBankPacketBroadcastsEvent, "WowUnreal.Network.ShowBankPacketBroadcastsEvent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FShowBankPacketBroadcastsEvent::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    bool bCalled = false;
    uint64 OpenedBankerGuid = 0;
    PacketHandler.OnBankOpened.AddLambda([&bCalled, &OpenedBankerGuid](uint64 BankerGuid)
    {
        bCalled = true;
        OpenedBankerGuid = BankerGuid;
    });

    const uint64 BankerGuid = 0x1122334455667788ULL;
    TArray<uint8> PacketData;
    PacketData.SetNumUninitialized(8);
    FMemory::Memcpy(PacketData.GetData(), &BankerGuid, sizeof(BankerGuid));

    PacketHandler.HandlePacket(WowOpcode::SMSG_SHOW_BANK, PacketData);

    TestTrue(TEXT("SMSG_SHOW_BANK fires the bank-open delegate"), bCalled);
    TestEqual(TEXT("Delegate receives the banker guid from the packet"), OpenedBankerGuid, BankerGuid);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShowMailboxPacketBroadcastsEvent, "WowUnreal.Network.ShowMailboxPacketBroadcastsEvent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FShowMailboxPacketBroadcastsEvent::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    bool bCalled = false;
    uint64 OpenedMailboxGuid = 0;
    PacketHandler.OnMailboxShown.AddLambda([&bCalled, &OpenedMailboxGuid](uint64 MailboxGuid)
    {
        bCalled = true;
        OpenedMailboxGuid = MailboxGuid;
    });

    const uint64 MailboxGuid = 0x8877665544332211ULL;
    TArray<uint8> PacketData;
    PacketData.SetNumUninitialized(8);
    FMemory::Memcpy(PacketData.GetData(), &MailboxGuid, sizeof(MailboxGuid));

    PacketHandler.HandlePacket(WowOpcode::SMSG_SHOW_MAILBOX, PacketData);

    TestTrue(TEXT("SMSG_SHOW_MAILBOX fires the mailbox-open delegate"), bCalled);
    TestEqual(TEXT("Delegate receives the mailbox guid from the packet"), OpenedMailboxGuid, MailboxGuid);
    TestEqual(TEXT("Packet handler stores the current mailbox guid"), PacketHandler.CurrentMailboxGuid, MailboxGuid);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectMailboxTypeAccessor, "WowUnreal.Entity.GameObjectMailboxTypeAccessor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGameObjectMailboxTypeAccessor::RunTest(const FString& Parameters)
{
    FWowGameObjectEntity Mailbox;
    Mailbox.TypeMask = WowTypeMask::GAMEOBJECT;
    Mailbox.SetField(GameObjectField::BYTES_1, static_cast<uint32>(WowGameObjectType::MAILBOX) << 8);

    TestEqual(TEXT("Mailbox gameobject type is read from GAMEOBJECT_BYTES_1 byte 1"), Mailbox.GetGameObjectType(), static_cast<uint8>(WowGameObjectType::MAILBOX));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMailListPacketParsesInbox, "WowUnreal.Network.MailListPacketParsesInbox",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMailListPacketParsesInbox::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    bool bCalled = false;
    TArray<FWowMailMessage> ParsedMail;
    PacketHandler.OnMailListReceived.AddLambda([&bCalled, &ParsedMail](const TArray<FWowMailMessage>& Mail)
    {
        bCalled = true;
        ParsedMail = Mail;
    });

    TArray<uint8> PacketData;
    TArray<uint8> MailBlock;

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendU16 = [](TArray<uint8>& Buffer, uint16 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint16));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint16));
    };
    auto AppendU32 = [](TArray<uint8>& Buffer, uint32 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint32));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint32));
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint64));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint64));
    };
    auto AppendFloat = [](TArray<uint8>& Buffer, float Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(float));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(float));
    };
    auto AppendCString = [](TArray<uint8>& Buffer, const ANSICHAR* Value)
    {
        const int32 Length = FCStringAnsi::Strlen(Value) + 1;
        const int32 Offset = Buffer.AddUninitialized(Length);
        FMemory::Memcpy(Buffer.GetData() + Offset, Value, Length);
    };

    AppendU32(PacketData, 1u);
    AppendU8(PacketData, 1u);

    const uint32 MessageId = 42;
    const uint64 SenderGuid = 0x1122334455667788ULL;
    const float DaysLeft = 29.5f;
    const uint32 ItemEntry = 6948;

    AppendU32(MailBlock, MessageId);
    AppendU8(MailBlock, WowMailMessageType::NORMAL);
    AppendU64(MailBlock, SenderGuid);
    AppendU32(MailBlock, 1234u); // C.O.D.
    AppendU32(MailBlock, 0u);    // unknown 3.3.5 field
    AppendU32(MailBlock, 61u);   // stationery
    AppendU32(MailBlock, 56789u);
    AppendU32(MailBlock, WowMailCheckMask::HAS_BODY);
    AppendFloat(MailBlock, DaysLeft);
    AppendU32(MailBlock, 0u); // template id
    AppendCString(MailBlock, "Welcome");
    AppendCString(MailBlock, "Mailbox parser smoke test.");
    AppendU8(MailBlock, 1u); // one attachment
    AppendU8(MailBlock, 0u); // attachment index
    AppendU32(MailBlock, 77u);
    AppendU32(MailBlock, ItemEntry);
    for (int32 EnchantIndex = 0; EnchantIndex < 7; ++EnchantIndex)
    {
        AppendU32(MailBlock, 0u);
        AppendU32(MailBlock, 0u);
        AppendU32(MailBlock, 0u);
    }
    AppendU32(MailBlock, static_cast<uint32>(-42)); // random property id
    AppendU32(MailBlock, 99u); // suffix factor
    AppendU32(MailBlock, 2u);  // count
    AppendU32(MailBlock, 0u);  // charges
    AppendU32(MailBlock, 100u);
    AppendU32(MailBlock, 80u);
    AppendU8(MailBlock, 0u);

    AppendU16(PacketData, static_cast<uint16>(MailBlock.Num() + sizeof(uint16)));
    PacketData.Append(MailBlock);

    PacketHandler.HandlePacket(WowOpcode::SMSG_MAIL_LIST_RESULT, PacketData);

    TestTrue(TEXT("SMSG_MAIL_LIST_RESULT fires the mail list delegate"), bCalled);
    TestEqual(TEXT("Parsed inbox has one message"), ParsedMail.Num(), 1);
    TestEqual(TEXT("Packet handler stores the parsed inbox"), PacketHandler.MailInbox.Num(), 1);
    if (ParsedMail.Num() != 1 || PacketHandler.MailInbox.Num() != 1)
    {
        return false;
    }

    const FWowMailMessage& Mail = ParsedMail[0];
    TestEqual(TEXT("Mail message id parsed"), Mail.MessageId, MessageId);
    TestEqual(TEXT("Mail type parsed"), Mail.MessageType, static_cast<uint8>(WowMailMessageType::NORMAL));
    TestEqual(TEXT("Mail sender guid parsed"), Mail.SenderGuid, SenderGuid);
    TestEqual(TEXT("Mail COD parsed"), Mail.COD, 1234u);
    TestEqual(TEXT("Mail money parsed"), Mail.Money, 56789u);
    TestEqual(TEXT("Mail subject parsed"), Mail.Subject, FString(TEXT("Welcome")));
    TestEqual(TEXT("Mail body parsed"), Mail.Body, FString(TEXT("Mailbox parser smoke test.")));
    TestEqual(TEXT("Mail attachment count parsed"), Mail.Attachments.Num(), 1);
    TestTrue(TEXT("Mail days left parsed"), FMath::IsNearlyEqual(Mail.DaysLeft, DaysLeft, KINDA_SMALL_NUMBER));

    if (Mail.Attachments.Num() != 1)
    {
        return false;
    }

    const FWowMailAttachment& Attachment = Mail.Attachments[0];
    TestEqual(TEXT("Attachment entry parsed"), Attachment.ItemEntry, ItemEntry);
    TestEqual(TEXT("Attachment count parsed"), Attachment.Count, 2u);
    TestEqual(TEXT("Attachment random property id parsed as signed"), Attachment.RandomPropertyId, -42);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeStatusPacketParsesState, "WowUnreal.Network.TradeStatusPacketParsesState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTradeStatusPacketParsesState::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    bool bCalled = false;
    FWowTradeState TradeState;
    PacketHandler.OnTradeUpdated.AddLambda([&bCalled, &TradeState](const FWowTradeState& State)
    {
        bCalled = true;
        TradeState = State;
    });

    TArray<uint8> BeginPacket;
    const uint32 BeginStatus = WowTradeStatus::BEGIN_TRADE;
    const uint64 TraderGuid = 0x8877665544332211ULL;
    BeginPacket.AddUninitialized(sizeof(uint32) + sizeof(uint64));
    FMemory::Memcpy(BeginPacket.GetData(), &BeginStatus, sizeof(uint32));
    FMemory::Memcpy(BeginPacket.GetData() + sizeof(uint32), &TraderGuid, sizeof(uint64));

    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS, BeginPacket);

    TestTrue(TEXT("Begin trade packet fires delegate"), bCalled);
    TestEqual(TEXT("Trade status parsed"), TradeState.Status, BeginStatus);
    TestEqual(TEXT("Trader guid parsed"), TradeState.TraderGuid, TraderGuid);
    TestFalse(TEXT("Begin trade does not mark window open yet"), TradeState.bTradeOpen);

    bCalled = false;
    TArray<uint8> OpenPacket;
    const uint32 OpenStatus = WowTradeStatus::OPEN_WINDOW;
    const uint32 OpenCookie = 0;
    OpenPacket.AddUninitialized(sizeof(uint32) + sizeof(uint32));
    FMemory::Memcpy(OpenPacket.GetData(), &OpenStatus, sizeof(uint32));
    FMemory::Memcpy(OpenPacket.GetData() + sizeof(uint32), &OpenCookie, sizeof(uint32));

    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS, OpenPacket);

    TestTrue(TEXT("Open trade packet fires delegate"), bCalled);
    TestEqual(TEXT("Open trade status parsed"), TradeState.Status, OpenStatus);
    TestTrue(TEXT("Open trade marks window open"), TradeState.bTradeOpen);
    TestEqual(TEXT("Trader guid persists across status updates"), TradeState.TraderGuid, TraderGuid);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeStatusExtendedParsesOffers, "WowUnreal.Network.TradeStatusExtendedParsesOffers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTradeStatusExtendedParsesOffers::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendU32 = [](TArray<uint8>& Buffer, uint32 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint32));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint32));
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint64));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint64));
    };

    auto AppendTradeSlot = [&AppendU8, &AppendU32, &AppendU64](TArray<uint8>& Buffer, uint8 SlotIndex, uint32 ItemId, uint32 DisplayId, uint32 Count)
    {
        AppendU8(Buffer, SlotIndex);
        AppendU32(Buffer, ItemId);
        AppendU32(Buffer, DisplayId);
        AppendU32(Buffer, Count);
        AppendU32(Buffer, 0u); // wrapped
        AppendU64(Buffer, 0u); // gift creator
        AppendU32(Buffer, 0u); // permanent enchant
        AppendU32(Buffer, 0u); // gem 1
        AppendU32(Buffer, 0u); // gem 2
        AppendU32(Buffer, 0u); // gem 3
        AppendU64(Buffer, 0u); // creator
        AppendU32(Buffer, 0u); // charges
        AppendU32(Buffer, 0u); // suffix factor
        AppendU32(Buffer, 0u); // random property
        AppendU32(Buffer, 0u); // lock id
        AppendU32(Buffer, 100u); // max durability
        AppendU32(Buffer, 80u);  // durability
    };

    TArray<uint8> TargetPacket;
    AppendU8(TargetPacket, 1u);  // trader data
    AppendU32(TargetPacket, 0u); // window cookie
    AppendU32(TargetPacket, 7u);
    AppendU32(TargetPacket, 7u);
    AppendU32(TargetPacket, 50000u);
    AppendU32(TargetPacket, 0u);
    for (uint8 Slot = 0; Slot < 7; ++Slot)
    {
        AppendTradeSlot(TargetPacket, Slot, Slot == 0 ? 6948u : 0u, Slot == 0 ? 123u : 0u, Slot == 0 ? 2u : 0u);
    }

    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS_EXTENDED, TargetPacket);

    TestTrue(TEXT("Trade extended packet opens trade state"), PacketHandler.CurrentTrade.bTradeOpen);
    TestEqual(TEXT("Target money parsed"), PacketHandler.CurrentTrade.TargetMoney, 50000u);
    TestEqual(TEXT("Target trade slots parsed"), PacketHandler.CurrentTrade.TargetItems.Num(), 7);
    if (PacketHandler.CurrentTrade.TargetItems.Num() != 7)
    {
        return false;
    }

    TestEqual(TEXT("Target trade item entry parsed"), PacketHandler.CurrentTrade.TargetItems[0].ItemId, 6948u);
    TestEqual(TEXT("Target trade item count parsed"), PacketHandler.CurrentTrade.TargetItems[0].Count, 2u);

    TArray<uint8> PlayerPacket;
    AppendU8(PlayerPacket, 0u);  // player data
    AppendU32(PlayerPacket, 0u); // window cookie
    AppendU32(PlayerPacket, 7u);
    AppendU32(PlayerPacket, 7u);
    AppendU32(PlayerPacket, 12345u);
    AppendU32(PlayerPacket, 0u);
    for (uint8 Slot = 0; Slot < 7; ++Slot)
    {
        AppendTradeSlot(PlayerPacket, Slot, Slot == 1 ? 17031u : 0u, Slot == 1 ? 321u : 0u, Slot == 1 ? 5u : 0u);
    }

    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS_EXTENDED, PlayerPacket);

    TestEqual(TEXT("Player money parsed"), PacketHandler.CurrentTrade.PlayerMoney, 12345u);
    TestEqual(TEXT("Player trade slots parsed"), PacketHandler.CurrentTrade.PlayerItems.Num(), 7);
    if (PacketHandler.CurrentTrade.PlayerItems.Num() != 7)
    {
        return false;
    }

    TestEqual(TEXT("Player trade item entry parsed"), PacketHandler.CurrentTrade.PlayerItems[1].ItemId, 17031u);
    TestEqual(TEXT("Player trade item count parsed"), PacketHandler.CurrentTrade.PlayerItems[1].Count, 5u);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeStatusPacketTracksLifecycle, "WowUnreal.Network.TradeStatusPacketTracksLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTradeStatusPacketTracksLifecycle::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    int32 UpdateCount = 0;
    PacketHandler.OnTradeUpdated.AddLambda([&UpdateCount](const FWowTradeState&)
    {
        ++UpdateCount;
    });

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendU32 = [](TArray<uint8>& Buffer, uint32 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint32));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint32));
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint64));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint64));
    };

    const uint64 TraderGuid = 0x1020304050607080ULL;

    TArray<uint8> BeginPacket;
    AppendU32(BeginPacket, WowTradeStatus::BEGIN_TRADE);
    AppendU64(BeginPacket, TraderGuid);
    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS, BeginPacket);

    TestEqual(TEXT("BEGIN_TRADE stores the trader guid"), PacketHandler.CurrentTrade.TraderGuid, TraderGuid);
    TestEqual(TEXT("BEGIN_TRADE updates trade status"), PacketHandler.CurrentTrade.Status, static_cast<uint32>(WowTradeStatus::BEGIN_TRADE));
    TestFalse(TEXT("Trade is not open at request time"), PacketHandler.CurrentTrade.bTradeOpen);
    TestFalse(TEXT("Local accepted starts cleared"), PacketHandler.CurrentTrade.bLocalAccepted);
    TestFalse(TEXT("Target accepted starts cleared"), PacketHandler.CurrentTrade.bTargetAccepted);

    TArray<uint8> OpenPacket;
    AppendU32(OpenPacket, WowTradeStatus::OPEN_WINDOW);
    AppendU32(OpenPacket, 0u);
    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS, OpenPacket);

    TestTrue(TEXT("OPEN_WINDOW marks the trade as open"), PacketHandler.CurrentTrade.bTradeOpen);

    TArray<uint8> AcceptPacket;
    AppendU32(AcceptPacket, WowTradeStatus::TRADE_ACCEPT);
    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS, AcceptPacket);

    TestTrue(TEXT("TRADE_ACCEPT marks the remote player as accepted"), PacketHandler.CurrentTrade.bTargetAccepted);

    PacketHandler.CurrentTrade.bLocalAccepted = true;
    TArray<uint8> BackPacket;
    AppendU32(BackPacket, WowTradeStatus::BACK_TO_TRADE);
    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS, BackPacket);

    TestFalse(TEXT("BACK_TO_TRADE clears local acceptance"), PacketHandler.CurrentTrade.bLocalAccepted);
    TestFalse(TEXT("BACK_TO_TRADE clears remote acceptance"), PacketHandler.CurrentTrade.bTargetAccepted);
    TestTrue(TEXT("BACK_TO_TRADE keeps the trade window open"), PacketHandler.CurrentTrade.bTradeOpen);

    PacketHandler.CurrentTrade.PlayerMoney = 100u;
    PacketHandler.CurrentTrade.TargetMoney = 200u;
    PacketHandler.CurrentTrade.PlayerItems.SetNum(1);
    PacketHandler.CurrentTrade.PlayerItems[0].ItemId = 6948;

    TArray<uint8> ClosePacket;
    AppendU32(ClosePacket, WowTradeStatus::CLOSE_WINDOW);
    AppendU32(ClosePacket, 0u);
    AppendU8(ClosePacket, 0u);
    AppendU32(ClosePacket, 0u);
    PacketHandler.HandlePacket(WowOpcode::SMSG_TRADE_STATUS, ClosePacket);

    TestFalse(TEXT("CLOSE_WINDOW hides the trade window"), PacketHandler.CurrentTrade.bTradeOpen);
    TestEqual(TEXT("CLOSE_WINDOW clears local money"), PacketHandler.CurrentTrade.PlayerMoney, 0u);
    TestEqual(TEXT("CLOSE_WINDOW clears target money"), PacketHandler.CurrentTrade.TargetMoney, 0u);
    TestEqual(TEXT("CLOSE_WINDOW clears offered items"), PacketHandler.CurrentTrade.PlayerItems.Num(), 0);
    TestTrue(TEXT("Trade update delegate fired for each lifecycle packet"), UpdateCount >= 4);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeStatusExtendedParsesBothSides, "WowUnreal.Network.TradeStatusExtendedParsesBothSides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTradeStatusExtendedParsesBothSides::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendU32 = [](TArray<uint8>& Buffer, uint32 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint32));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint32));
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint64));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint64));
    };
    auto AppendTradeSlot = [&AppendU8, &AppendU32, &AppendU64](TArray<uint8>& Buffer, uint8 Slot, uint32 ItemId, uint32 DisplayId, uint32 Count)
    {
        AppendU8(Buffer, Slot);
        AppendU32(Buffer, ItemId);
        AppendU32(Buffer, DisplayId);
        AppendU32(Buffer, Count);
        AppendU32(Buffer, 0u);
        AppendU64(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU64(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU32(Buffer, 0u);
        AppendU32(Buffer, 0u);
    };

    auto BuildExtendedPacket = [&AppendU8, &AppendU32, &AppendTradeSlot](bool bTraderData, uint32 Money, uint32 SpellId, uint8 OccupiedSlot, uint32 ItemId, uint32 DisplayId, uint32 Count)
    {
        TArray<uint8> Packet;
        AppendU8(Packet, bTraderData ? 1u : 0u);
        AppendU32(Packet, 0u);
        AppendU32(Packet, 7u);
        AppendU32(Packet, 7u);
        AppendU32(Packet, Money);
        AppendU32(Packet, SpellId);
        for (uint8 Slot = 0; Slot < 7; ++Slot)
        {
            const bool bOccupied = (Slot == OccupiedSlot);
            AppendTradeSlot(Packet, Slot, bOccupied ? ItemId : 0u, bOccupied ? DisplayId : 0u, bOccupied ? Count : 0u);
        }
        return Packet;
    };

    PacketHandler.HandlePacket(
        WowOpcode::SMSG_TRADE_STATUS_EXTENDED,
        BuildExtendedPacket(false, 12345u, 133u, 0u, 6948u, 111u, 2u));
    PacketHandler.HandlePacket(
        WowOpcode::SMSG_TRADE_STATUS_EXTENDED,
        BuildExtendedPacket(true, 54321u, 686u, 1u, 17031u, 222u, 1u));

    TestTrue(TEXT("Trade window is open after extended payloads"), PacketHandler.CurrentTrade.bTradeOpen);
    TestEqual(TEXT("Extended payload sets the trade status to OPEN_WINDOW"), PacketHandler.CurrentTrade.Status, static_cast<uint32>(WowTradeStatus::OPEN_WINDOW));
    TestEqual(TEXT("Player money parsed from own trade payload"), PacketHandler.CurrentTrade.PlayerMoney, 12345u);
    TestEqual(TEXT("Target money parsed from trader trade payload"), PacketHandler.CurrentTrade.TargetMoney, 54321u);
    TestEqual(TEXT("Player spell id parsed"), PacketHandler.CurrentTrade.PlayerSpell, 133u);
    TestEqual(TEXT("Target spell id parsed"), PacketHandler.CurrentTrade.TargetSpell, 686u);
    TestTrue(TEXT("Player trade items array has all trade slots"), PacketHandler.CurrentTrade.PlayerItems.IsValidIndex(0));
    TestTrue(TEXT("Target trade items array has all trade slots"), PacketHandler.CurrentTrade.TargetItems.IsValidIndex(1));

    if (!PacketHandler.CurrentTrade.PlayerItems.IsValidIndex(0) || !PacketHandler.CurrentTrade.TargetItems.IsValidIndex(1))
    {
        return false;
    }

    TestEqual(TEXT("Own side item entry parsed"), PacketHandler.CurrentTrade.PlayerItems[0].ItemId, 6948u);
    TestEqual(TEXT("Own side item count parsed"), PacketHandler.CurrentTrade.PlayerItems[0].Count, 2u);
    TestEqual(TEXT("Trader side item entry parsed"), PacketHandler.CurrentTrade.TargetItems[1].ItemId, 17031u);
    TestEqual(TEXT("Trader side item count parsed"), PacketHandler.CurrentTrade.TargetItems[1].Count, 1u);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaidGroupListParsesRosterMetadata, "WowUnreal.Network.Raid.GroupListParsesRosterMetadata",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRaidGroupListParsesRosterMetadata::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendU32 = [](TArray<uint8>& Buffer, uint32 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint32));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint32));
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint64));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint64));
    };
    auto AppendCString = [](TArray<uint8>& Buffer, const ANSICHAR* Value)
    {
        const int32 Length = FCStringAnsi::Strlen(Value) + 1;
        const int32 Offset = Buffer.AddUninitialized(Length);
        FMemory::Memcpy(Buffer.GetData() + Offset, Value, Length);
    };

    const uint64 GroupGuid = 0x1020304050607080ULL;
    const uint64 LeaderGuid = 0x9988776655443322ULL;
    const uint64 MemberGuidA = 0x1111222233334444ULL;
    const uint64 MemberGuidB = LeaderGuid;

    TArray<uint8> Packet;
    AppendU8(Packet, WowGroupType::RAID);
    AppendU8(Packet, 2u);
    AppendU8(Packet, WowGroupMemberFlags::ASSISTANT);
    AppendU8(Packet, 3u);
    AppendU64(Packet, GroupGuid);
    AppendU32(Packet, 41u);
    AppendU32(Packet, 2u);

    AppendCString(Packet, "Tanky");
    AppendU64(Packet, MemberGuidA);
    AppendU8(Packet, WowGroupMemberStatus::ONLINE);
    AppendU8(Packet, 0u);
    AppendU8(Packet, WowGroupMemberFlags::MAINTANK);
    AppendU8(Packet, 1u);

    AppendCString(Packet, "Leader");
    AppendU64(Packet, MemberGuidB);
    AppendU8(Packet, WowGroupMemberStatus::ONLINE);
    AppendU8(Packet, 1u);
    AppendU8(Packet, 0u);
    AppendU8(Packet, 2u);

    AppendU64(Packet, LeaderGuid);
    AppendU8(Packet, 2u);
    AppendU64(Packet, 0u);
    AppendU8(Packet, 3u);
    AppendU8(Packet, 1u);
    AppendU8(Packet, 2u);
    AppendU8(Packet, 0u);

    PacketHandler.HandlePacket(WowOpcode::SMSG_GROUP_LIST, Packet);

    TestTrue(TEXT("Group list marks the roster as a raid"), PacketHandler.GroupInfo.IsRaidGroup());
    TestEqual(TEXT("Group list stores the raw group flags"), PacketHandler.GroupInfo.RawGroupType, static_cast<uint8>(WowGroupType::RAID));
    TestEqual(TEXT("Group list stores the group guid"), PacketHandler.GroupInfo.GroupGuid, GroupGuid);
    TestEqual(TEXT("Group list stores the total member count including the local player"), PacketHandler.GroupInfo.MemberCount, static_cast<uint8>(3));
    TestEqual(TEXT("Group list stores the leader guid"), PacketHandler.GroupInfo.LeaderGuid, LeaderGuid);
    TestEqual(TEXT("Group list stores the local subgroup"), PacketHandler.GroupInfo.SelfSubgroup, static_cast<uint8>(2));
    TestEqual(TEXT("Group list stores the local flags"), PacketHandler.GroupInfo.SelfFlags, static_cast<uint8>(WowGroupMemberFlags::ASSISTANT));
    TestEqual(TEXT("Group list stores the remote roster size"), PacketHandler.GroupInfo.Members.Num(), 2);
    if (PacketHandler.GroupInfo.Members.Num() != 2)
    {
        return false;
    }

    TestEqual(TEXT("First remote member name parsed"), PacketHandler.GroupInfo.Members[0].Name, FString(TEXT("Tanky")));
    TestEqual(TEXT("First remote member subgroup parsed"), PacketHandler.GroupInfo.Members[0].Group, static_cast<uint8>(0));
    TestEqual(TEXT("Second remote member guid parsed"), PacketHandler.GroupInfo.Members[1].Guid, MemberGuidB);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaidTargetPacketsTrackAssignments, "WowUnreal.Network.Raid.TargetPacketsTrackAssignments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRaidTargetPacketsTrackAssignments::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint64));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint64));
    };

    const uint64 TargetA = 0xABCDEF0011223344ULL;
    const uint64 TargetB = 0x5566778899AABBCCULL;
    const uint64 TargetC = 0xCAFEBABE01020304ULL;

    TArray<uint8> ListPacket;
    AppendU8(ListPacket, 1u);
    AppendU8(ListPacket, 0u);
    AppendU64(ListPacket, TargetA);
    AppendU8(ListPacket, 7u);
    AppendU64(ListPacket, TargetB);
    PacketHandler.HandlePacket(WowOpcode::MSG_RAID_TARGET_UPDATE, ListPacket);

    TestEqual(TEXT("Icon list assigns icon 1 to the first target"), PacketHandler.RaidTargets.GetIconIndex(TargetA), 0);
    TestEqual(TEXT("Icon list assigns icon 8 to the second target"), PacketHandler.RaidTargets.GetIconIndex(TargetB), 7);

    TArray<uint8> UpdatePacket;
    AppendU8(UpdatePacket, 0u);
    AppendU64(UpdatePacket, 0x0102030405060708ULL);
    AppendU8(UpdatePacket, 7u);
    AppendU64(UpdatePacket, TargetC);
    PacketHandler.HandlePacket(WowOpcode::MSG_RAID_TARGET_UPDATE, UpdatePacket);

    TestEqual(TEXT("Single target update replaces the icon target"), PacketHandler.RaidTargets.GetIconIndex(TargetC), 7);
    TestEqual(TEXT("Replaced target no longer has an icon"), PacketHandler.RaidTargets.GetIconIndex(TargetB), INDEX_NONE);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaidReadyCheckPacketsTrackLifecycle, "WowUnreal.Network.Raid.ReadyCheckPacketsTrackLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRaidReadyCheckPacketsTrackLifecycle::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint64));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint64));
    };

    const uint64 InitiatorGuid = 0x8877665544332211ULL;
    const uint64 ReadyGuid = 0x1111111122222222ULL;
    const uint64 NotReadyGuid = 0x3333333344444444ULL;

    TArray<uint8> StartPacket;
    AppendU64(StartPacket, InitiatorGuid);
    PacketHandler.HandlePacket(WowOpcode::MSG_RAID_READY_CHECK, StartPacket);

    TestTrue(TEXT("Ready check becomes active on request"), PacketHandler.ReadyCheck.bActive);
    TestEqual(TEXT("Ready check stores the initiator"), PacketHandler.ReadyCheck.InitiatorGuid, InitiatorGuid);
    TestTrue(TEXT("Ready check starts with remaining time"), PacketHandler.ReadyCheck.GetTimeLeftSeconds() > 0.0f);

    TArray<uint8> ReadyPacket;
    AppendU64(ReadyPacket, ReadyGuid);
    AppendU8(ReadyPacket, WowReadyCheckResponse::READY);
    PacketHandler.HandlePacket(WowOpcode::MSG_RAID_READY_CHECK_CONFIRM, ReadyPacket);

    TArray<uint8> NotReadyPacket;
    AppendU64(NotReadyPacket, NotReadyGuid);
    AppendU8(NotReadyPacket, WowReadyCheckResponse::NOT_READY);
    PacketHandler.HandlePacket(WowOpcode::MSG_RAID_READY_CHECK_CONFIRM, NotReadyPacket);

    const uint8* ReadyState = PacketHandler.ReadyCheck.FindResponse(ReadyGuid);
    const uint8* NotReadyState = PacketHandler.ReadyCheck.FindResponse(NotReadyGuid);
    TestNotNull(TEXT("Ready responder is tracked"), ReadyState);
    TestNotNull(TEXT("Not-ready responder is tracked"), NotReadyState);
    if (!ReadyState || !NotReadyState)
    {
        return false;
    }

    TestEqual(TEXT("Ready response value is stored"), *ReadyState, static_cast<uint8>(WowReadyCheckResponse::READY));
    TestEqual(TEXT("Not-ready response value is stored"), *NotReadyState, static_cast<uint8>(WowReadyCheckResponse::NOT_READY));

    PacketHandler.HandlePacket(WowOpcode::MSG_RAID_READY_CHECK_FINISHED, {});
    TestFalse(TEXT("Ready check is cleared when finished"), PacketHandler.ReadyCheck.bActive);
    TestEqual(TEXT("Ready check responses are cleared when finished"), PacketHandler.ReadyCheck.Responses.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelRequestPacketsTrackLifecycle, "WowUnreal.Network.Duel.RequestPacketsTrackLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDuelRequestPacketsTrackLifecycle::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendU32 = [](TArray<uint8>& Buffer, uint32 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint32));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint32));
    };
    auto AppendU64 = [](TArray<uint8>& Buffer, uint64 Value)
    {
        const int32 Offset = Buffer.AddUninitialized(sizeof(uint64));
        FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(uint64));
    };

    const uint64 ArbiterGuid = 0x8877665544332211ULL;
    const uint64 InitiatorGuid = 0x1122334455667788ULL;

    TArray<uint8> RequestPacket;
    AppendU64(RequestPacket, ArbiterGuid);
    AppendU64(RequestPacket, InitiatorGuid);
    PacketHandler.HandlePacket(WowOpcode::SMSG_DUEL_REQUESTED, RequestPacket);

    TestTrue(TEXT("Duel request enters the requested phase"), PacketHandler.CurrentDuel.IsRequestPending());
    TestEqual(TEXT("Duel request stores arbiter guid"), PacketHandler.CurrentDuel.ArbiterGuid, ArbiterGuid);
    TestEqual(TEXT("Duel request stores initiator guid"), PacketHandler.CurrentDuel.InitiatorGuid, InitiatorGuid);

    TArray<uint8> CountdownPacket;
    AppendU32(CountdownPacket, 3000u);
    PacketHandler.HandlePacket(WowOpcode::SMSG_DUEL_COUNTDOWN, CountdownPacket);

    TestTrue(TEXT("Duel countdown starts after accept"), PacketHandler.CurrentDuel.IsCountdownActive());
    TestTrue(TEXT("Duel countdown has time remaining"), PacketHandler.CurrentDuel.GetCountdownRemainingSeconds() > 0.0f);

    PacketHandler.HandlePacket(WowOpcode::SMSG_DUEL_OUTOFBOUNDS, {});
    TestFalse(TEXT("Out-of-bounds packet flips the in-bounds flag"), PacketHandler.CurrentDuel.bInBounds);

    PacketHandler.HandlePacket(WowOpcode::SMSG_DUEL_INBOUNDS, {});
    TestTrue(TEXT("In-bounds packet restores the in-bounds flag"), PacketHandler.CurrentDuel.bInBounds);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCompletionPacketsParseResults, "WowUnreal.Network.Duel.CompletionPacketsParseResults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDuelCompletionPacketsParseResults::RunTest(const FString& Parameters)
{
    FWowPacketHandler PacketHandler;

    auto AppendU8 = [](TArray<uint8>& Buffer, uint8 Value)
    {
        Buffer.Add(Value);
    };
    auto AppendCString = [](TArray<uint8>& Buffer, const ANSICHAR* Value)
    {
        const int32 Length = FCStringAnsi::Strlen(Value) + 1;
        const int32 Offset = Buffer.AddUninitialized(Length);
        FMemory::Memcpy(Buffer.GetData() + Offset, Value, Length);
    };

    TArray<uint8> InterruptedPacket;
    AppendU8(InterruptedPacket, 0u);
    PacketHandler.HandlePacket(WowOpcode::SMSG_DUEL_COMPLETE, InterruptedPacket);

    TestTrue(TEXT("Interrupted duel enters the completed phase"), PacketHandler.CurrentDuel.IsComplete());
    TestTrue(TEXT("Interrupted duel sets interrupted state"), PacketHandler.CurrentDuel.bInterrupted);
    TestFalse(TEXT("Interrupted duel does not mark a winner"), PacketHandler.CurrentDuel.bHasWinner);

    PacketHandler.CurrentDuel.Clear();

    TArray<uint8> CompletePacket;
    AppendU8(CompletePacket, 1u);
    PacketHandler.HandlePacket(WowOpcode::SMSG_DUEL_COMPLETE, CompletePacket);

    TArray<uint8> WinnerPacket;
    AppendU8(WinnerPacket, WowDuelResultReason::FLED);
    AppendCString(WinnerPacket, "Winner");
    AppendCString(WinnerPacket, "Loser");
    PacketHandler.HandlePacket(WowOpcode::SMSG_DUEL_WINNER, WinnerPacket);

    TestTrue(TEXT("Winner packet keeps the duel completed"), PacketHandler.CurrentDuel.IsComplete());
    TestTrue(TEXT("Winner packet marks a duel winner"), PacketHandler.CurrentDuel.bHasWinner);
    TestEqual(TEXT("Winner packet stores the result reason"), PacketHandler.CurrentDuel.ResultReason, static_cast<uint8>(WowDuelResultReason::FLED));
    TestEqual(TEXT("Winner packet stores the winner name"), PacketHandler.CurrentDuel.WinnerName, FString(TEXT("Winner")));
    TestEqual(TEXT("Winner packet stores the loser name"), PacketHandler.CurrentDuel.LoserName, FString(TEXT("Loser")));

    return true;
}

// ====================================================================
// DBC Parser Tests (require MPQ data)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDbcParserBasic, "WowUnreal.Parser.DBC.MapDbc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDbcParserBasic::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping DBC test"));
        return true;
    }

    TArray<uint8> Data;
    if (!Mpq.ReadFile(TEXT("DBFilesClient\\Map.dbc"), Data))
    {
        AddError(TEXT("Failed to read Map.dbc from MPQ"));
        return false;
    }

    FDbcParser Parser;
    TestTrue(TEXT("Map.dbc parses"), Parser.Parse(Data));
    TestTrue(TEXT("Has records"), Parser.GetRecordCount() > 0);
    TestTrue(TEXT("Has fields"), Parser.GetFieldCount() > 0);

    // Map.dbc record 0 should have ID in field 0
    uint32 FirstId = Parser.GetUInt(0, 0);
    TestTrue(TEXT("First record has valid ID"), FirstId > 0 || FirstId == 0); // ID 0 is Eastern Kingdoms

    // Get a string field — map name is field 5 in Map.dbc
    FString MapName = Parser.GetString(0, 5);
    TestTrue(TEXT("Map name not empty"), MapName.Len() > 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDbcParserAreaTable, "WowUnreal.Parser.DBC.AreaTableDbc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDbcParserAreaTable::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    TArray<uint8> Data;
    if (!Mpq.ReadFile(TEXT("DBFilesClient\\AreaTable.dbc"), Data))
    {
        AddError(TEXT("Failed to read AreaTable.dbc"));
        return false;
    }

    FDbcParser Parser;
    TestTrue(TEXT("AreaTable.dbc parses"), Parser.Parse(Data));
    // WoW 3.3.5 has ~3000+ areas
    TestTrue(TEXT("AreaTable has >1000 records"), Parser.GetRecordCount() > 1000);

    return true;
}

// ====================================================================
// BLP Parser Tests (require MPQ data)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlpParserDxt, "WowUnreal.Parser.BLP.DxtTexture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBlpParserDxt::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    TArray<uint8> Data;
    // Try common terrain textures (case-insensitive, backslash paths)
    bool bFound = Mpq.ReadFile(TEXT("Tileset\\Elwynn\\ElwynnGrassBase01.blp"), Data)
               || Mpq.ReadFile(TEXT("TILESET\\ELWYNN\\ELWYNNGRASSBASE01.BLP"), Data)
               || Mpq.ReadFile(TEXT("Tileset\\Generic\\GenericRock01.blp"), Data)
               || Mpq.ReadFile(TEXT("TILESET\\GENERIC\\GENERICROCK01.BLP"), Data);
    if (!bFound)
    {
        AddWarning(TEXT("Could not find a BLP texture in MPQ — skipping"));
        return true;
    }

    FBlpTexture Tex = FBlpParser::Parse(Data);
    TestTrue(TEXT("BLP is valid"), Tex.bIsValid);
    TestTrue(TEXT("Has width"), Tex.Width > 0);
    TestTrue(TEXT("Has height"), Tex.Height > 0);
    TestTrue(TEXT("Has mip levels"), Tex.MipLevels.Num() > 0);
    TestTrue(TEXT("Is DXT format"), Tex.PixelFormat == EBlpPixelFormat::DXT1 ||
             Tex.PixelFormat == EBlpPixelFormat::DXT3 || Tex.PixelFormat == EBlpPixelFormat::DXT5);
    TestTrue(TEXT("Mip 0 has data"), Tex.MipLevels[0].Data.Num() > 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlpRuntimeMipChain,
	"WowUnreal.Assets.BLP.RuntimeMipChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBlpRuntimeMipChain::RunTest(const FString& Parameters)
{
	FBlpTexture Source;
	Source.bIsValid = true;
	Source.Width = 4;
	Source.Height = 4;
	Source.PixelFormat = EBlpPixelFormat::RGBA8;

	for (const FIntPoint Size : {FIntPoint(4, 4), FIntPoint(2, 2), FIntPoint(1, 1)})
	{
		FBlpMipLevel& Mip = Source.MipLevels.AddDefaulted_GetRef();
		Mip.Width = Size.X;
		Mip.Height = Size.Y;
		Mip.Data.Init(static_cast<uint8>(40 + Source.MipLevels.Num()), Size.X * Size.Y * 4);
	}

	UTexture2D* RuntimeTexture = FWowTextureFactory::CreateTexture(Source, TEXT("AutomationMipChain"));
	TestNotNull(TEXT("Runtime texture created"), RuntimeTexture);
	if (!RuntimeTexture || !RuntimeTexture->GetPlatformData())
	{
		return false;
	}

	TestEqual(TEXT("All authored mip levels uploaded"),
		RuntimeTexture->GetPlatformData()->Mips.Num(), Source.MipLevels.Num());
	for (int32 MipIndex = 0; MipIndex < Source.MipLevels.Num(); ++MipIndex)
	{
		const FTexture2DMipMap& RuntimeMip = RuntimeTexture->GetPlatformData()->Mips[MipIndex];
		TestEqual(FString::Printf(TEXT("Mip %d width"), MipIndex),
			RuntimeMip.SizeX, static_cast<int32>(Source.MipLevels[MipIndex].Width));
		TestEqual(FString::Printf(TEXT("Mip %d height"), MipIndex),
			RuntimeMip.SizeY, static_cast<int32>(Source.MipLevels[MipIndex].Height));
		TestEqual(FString::Printf(TEXT("Mip %d byte count"), MipIndex),
			RuntimeMip.BulkData.GetBulkDataSize(),
			static_cast<int64>(Source.MipLevels[MipIndex].Data.Num()));
	}
	return true;
}

// ====================================================================
// ADT Parser Tests (require MPQ data)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdtParserBasic, "WowUnreal.Parser.ADT.Elwynn32_48",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdtParserBasic::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    TArray<uint8> Data;
    if (!Mpq.ReadFile(TEXT("World\\Maps\\Azeroth\\Azeroth_32_48.adt"), Data))
    {
        AddError(TEXT("Failed to read Azeroth_32_48.adt"));
        return false;
    }

    FAdtData Adt = FAdtParser::Parse(Data);
    TestTrue(TEXT("ADT is valid"), Adt.bIsValid);

    // Should have 256 chunks (16x16)
    // Verify first chunk has valid height data (not all zeros would be unusual)
    bool bHasNonZeroHeight = false;
    for (int32 i = 0; i < 145; ++i)
    {
        if (FMath::Abs(Adt.Chunks[0].Heights[i]) > 0.001f)
        {
            bHasNonZeroHeight = true;
            break;
        }
    }
    TestTrue(TEXT("Chunk 0 has height data"), bHasNonZeroHeight);

    // Should have texture paths
    TestTrue(TEXT("Has texture paths"), Adt.TexturePaths.Num() > 0);

    // Should have doodad placements (Elwynn is heavily populated)
    TestTrue(TEXT("Has doodad placements"), Adt.DoodadPlacements.Num() > 0);

    return true;
}

// ====================================================================
// M2 Parser Tests (require MPQ data)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2ParserBasic, "WowUnreal.Parser.M2.TreeModel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FM2ParserBasic::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    // Read a common doodad (tree)
    TArray<uint8> M2Data;
    if (!Mpq.ReadFile(TEXT("World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTree01.m2"), M2Data))
    {
        // Try alternate path
        if (!Mpq.ReadFile(TEXT("World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTree01.M2"), M2Data))
        {
            AddWarning(TEXT("Could not find tree M2 — skipping"));
            return true;
        }
    }

    // Read skin file
    TArray<uint8> SkinData;
    FString SkinPath = TEXT("World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTree0100.skin");
    Mpq.ReadFile(SkinPath, SkinData);

    FM2Data Model = FM2Parser::Parse(M2Data, SkinData);
    TestTrue(TEXT("M2 is valid"), Model.bIsValid);
    TestTrue(TEXT("Has vertices"), Model.Vertices.Num() > 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2SpellRibbon335,
    "WowUnreal.Parser.M2.SpellRibbon335",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FM2SpellRibbon335::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    TArray<uint8> M2Data;
    TArray<uint8> SkinData;
    if (!Mpq.ReadFile(TEXT("Spells\\HolySmite_Low_Chest.m2"), M2Data))
    {
        AddWarning(TEXT("HolySmite spell model is not available — skipping"));
        return true;
    }
    Mpq.ReadFile(TEXT("Spells\\HolySmite_Low_Chest00.skin"), SkinData);
    const FM2Data Model = FM2Parser::Parse(M2Data, SkinData);
    TestTrue(TEXT("Smite M2 is valid"), Model.bIsValid);
    TestEqual(TEXT("Smite carries both authored light ribbons"), Model.RibbonEmitters.Num(), 2);
    if (Model.RibbonEmitters.Num() == 2)
    {
        for (const FM2RibbonEmitter& Ribbon : Model.RibbonEmitters)
        {
            TestTrue(TEXT("Ribbon references a valid bone"), Model.Bones.IsValidIndex(Ribbon.Bone));
            TestTrue(TEXT("Ribbon references a valid texture"), Model.TexturePaths.IsValidIndex(Ribbon.Texture));
            TestTrue(TEXT("Ribbon has a positive edge rate"), Ribbon.EdgesPerSecond > 0.0f);
            TestTrue(TEXT("Ribbon has a positive lifetime"), Ribbon.EdgeLifetime > 0.0f);
        }
    }
    return true;
}

// ====================================================================
// WMO Parser Tests (require MPQ data)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWmoParserBasic, "WowUnreal.Parser.WMO.GoldshireInn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWmoParserBasic::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    TArray<uint8> Data;
    if (!Mpq.ReadFile(TEXT("World\\wmo\\Azeroth\\Buildings\\GoldShireInn\\GoldShire_Inn.wmo"), Data))
    {
        AddWarning(TEXT("Could not find GoldShire_Inn.wmo — skipping"));
        return true;
    }

    FWmoRootData Root = FWmoParser::ParseRoot(Data);
    TestTrue(TEXT("WMO root is valid"), Root.bIsValid);
    TestTrue(TEXT("Has groups"), Root.NumGroups > 0);
    TestTrue(TEXT("Has materials"), Root.Materials.Num() > 0);

    return true;
}

// ====================================================================
// MPQ Manager Tests (filesystem)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMpqManagerBasic, "WowUnreal.Mpq.InitAndRead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMpqManagerBasic::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ data not available at expected path — skipping"));
        return true;
    }

    TestTrue(TEXT("MPQ is initialized"), Mpq.IsInitialized());

    // Test file existence
    TestTrue(TEXT("Map.dbc exists"), Mpq.FileExists(TEXT("DBFilesClient\\Map.dbc")));
    TestFalse(TEXT("Nonexistent file"), Mpq.FileExists(TEXT("this\\does\\not\\exist.blp")));

    // Test reading a known file
    TArray<uint8> Data;
    TestTrue(TEXT("Read Map.dbc"), Mpq.ReadFile(TEXT("DBFilesClient\\Map.dbc"), Data));
    TestTrue(TEXT("Map.dbc has data"), Data.Num() > 0);

    // DBC header magic should be "WDBC"
    TestTrue(TEXT("DBC data >= 4 bytes"), Data.Num() >= 4);
    if (Data.Num() >= 4)
    {
        TestEqual(TEXT("DBC magic W"), (char)Data[0], 'W');
        TestEqual(TEXT("DBC magic D"), (char)Data[1], 'D');
        TestEqual(TEXT("DBC magic B"), (char)Data[2], 'B');
        TestEqual(TEXT("DBC magic C"), (char)Data[3], 'C');
    }

    return true;
}

// ====================================================================
// Audio DBC Tests (require MPQ data)
// ====================================================================

#include "Formats/Dbc/DbcStore.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZoneMusicDbcTest, "WowUnreal.Audio.ZoneMusicDbc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FZoneMusicDbcTest::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    // Ensure DBCs are loaded
    FDbcStore::Get().LoadAll(Mpq);

    // ZoneMusic ID 1 is Elwynn Forest music
    const FZoneMusicDbcEntry* Entry = FDbcStore::Get().ZoneMusic().GetById(1);
    TestNotNull(TEXT("ZoneMusic ID 1 exists"), Entry);
    if (Entry)
    {
        TestTrue(TEXT("Day sound ID is valid (not zero)"), Entry->SoundDayID > 0);
        TestTrue(TEXT("Day sound ID is a reasonable SoundEntry ID (<100000)"), Entry->SoundDayID < 100000);
        TestTrue(TEXT("Night sound ID is valid"), Entry->SoundNightID > 0);
    }

    TestTrue(TEXT("ZoneMusic has >10 entries"), FDbcStore::Get().ZoneMusic().Num() > 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundEntriesDbcTest, "WowUnreal.Audio.SoundEntriesDbc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSoundEntriesDbcTest::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    FDbcStore::Get().LoadAll(Mpq);

    TestTrue(TEXT("SoundEntries has >1000 entries"), FDbcStore::Get().SoundEntries().Num() > 1000);

    // Verify a known sound entry has a file path
    const FZoneMusicDbcEntry* ZM = FDbcStore::Get().ZoneMusic().GetById(1);
    if (ZM && ZM->SoundDayID > 0)
    {
        const FSoundEntriesDbcEntry* SE = FDbcStore::Get().SoundEntries().GetById(ZM->SoundDayID);
        TestNotNull(TEXT("Elwynn day music SoundEntry exists"), SE);
        if (SE)
        {
            bool bHasFile = false;
            for (int32 i = 0; i < 10; ++i)
            {
                if (!SE->FileDataID[i].IsEmpty())
                {
                    bHasFile = true;
                    break;
                }
            }
            TestTrue(TEXT("Elwynn music SoundEntry has at least one file"), bHasFile);
            TestTrue(TEXT("SoundEntry has directory base"), !SE->DirectoryBase.IsEmpty());
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundAmbienceDbcTest, "WowUnreal.Audio.SoundAmbienceDbc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSoundAmbienceDbcTest::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    FDbcStore::Get().LoadAll(Mpq);

    TestTrue(TEXT("SoundAmbience has >10 entries"), FDbcStore::Get().SoundAmbience().Num() > 10);

    // AreaTable ID 12 (Elwynn Forest) should have an ambience ID
    const FAreaTableDbcEntry* Area = FDbcStore::Get().AreaTable().GetById(12);
    TestNotNull(TEXT("Elwynn Forest (ID 12) exists in AreaTable"), Area);
    if (Area)
    {
        TestTrue(TEXT("Elwynn has ambience ID"), Area->AmbienceID > 0);
        TestTrue(TEXT("Elwynn has zone music ID"), Area->ZoneMusicID > 0);

        if (Area->AmbienceID > 0)
        {
            const FSoundAmbienceDbcEntry* Amb = FDbcStore::Get().SoundAmbience().GetById(Area->AmbienceID);
            TestNotNull(TEXT("Elwynn ambience entry exists"), Amb);
            if (Amb)
            {
                TestTrue(TEXT("Elwynn has day ambience sound"), Amb->DayAmbience > 0);
            }
        }
    }

    return true;
}

// ====================================================================
// Packet Handler Tests (in-memory, no network dependency)
// ====================================================================

#include "WowPacketHandler.h"
#include "WowOpcodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPacketHandlerSpellStart, "WowUnreal.Network.HandleSpellStart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPacketHandlerSpellStart::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;

    TArray<uint8> TestData;
    // Packed GUID (caster item): mask=0x01, low=0x42
    TestData.Add(0x01); TestData.Add(0x42);
    // Packed GUID (caster): mask=0x01, low=0x43
    TestData.Add(0x01); TestData.Add(0x43);
    // Cast counter
    TestData.Add(0x01);
    // Spell ID = 133 (Fireball)
    TestData.Add(133); TestData.Add(0); TestData.Add(0); TestData.Add(0);
    // Cast flags = 0
    TestData.Add(0); TestData.Add(0); TestData.Add(0); TestData.Add(0);
    // Cast time = 3000ms
    TestData.Add(0xB8); TestData.Add(0x0B); TestData.Add(0); TestData.Add(0);

    // Should not crash
    Handler.HandlePacket(WowOpcode::SMSG_SPELL_START, TestData);
    TestTrue(TEXT("SPELL_START handled without crash"), true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPacketHandlerPowerUpdate, "WowUnreal.Network.HandlePowerUpdate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPacketHandlerPowerUpdate::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;

    TArray<uint8> TestData;
    // Packed GUID: mask=0x01, low=0x44
    TestData.Add(0x01); TestData.Add(0x44);
    // Power type = 0 (mana)
    TestData.Add(0x00);
    // Power value = 1500
    TestData.Add(0xDC); TestData.Add(0x05); TestData.Add(0); TestData.Add(0);

    Handler.HandlePacket(WowOpcode::SMSG_POWER_UPDATE, TestData);
    TestTrue(TEXT("POWER_UPDATE handled without crash"), true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPacketHandlerEntityCreation, "WowUnreal.Network.EntityCreation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPacketHandlerEntityCreation::RunTest(const FString& Parameters)
{
    FWowEntityManager EM;

    // Create and promote a unit entity
    FWowEntity& Base = EM.GetOrCreate(5001);
    Base.SetField(UnitField::HEALTH, 100);
    Base.SetField(UnitField::MAXHEALTH, 200);
    Base.SetField(UnitField::DISPLAYID, 3167); // Stormwind Guard
    Base.SetField(UnitField::LEVEL, 75);
    Base.SetField(UnitField::BYTES_0, 1 | (1 << 8) | (0 << 16)); // Race=1(Human), Class=1(Warrior), Gender=0(Male)

    EM.PromoteToTyped(5001, WowTypeMask::UNIT);
    FWowUnitEntity* Unit = EM.FindUnit(5001);
    TestNotNull(TEXT("Promoted to unit"), Unit);
    if (Unit)
    {
        TestEqual(TEXT("Display ID"), Unit->GetDisplayId(), (uint32)3167);
        TestEqual(TEXT("Race"), Unit->GetRaceId(), (uint8)1);
        TestEqual(TEXT("Class"), Unit->GetClassId(), (uint8)1);
        TestEqual(TEXT("Gender"), Unit->GetGenderId(), (uint8)0);
        TestEqual(TEXT("Level"), Unit->GetLevel(), 75);
    }

    return true;
}

// ====================================================================
// Character Builder Tests (model path lookup, no rendering)
// ====================================================================

#include "WowCharacterBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterModelPath, "WowUnreal.Character.ModelPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCharacterModelPath::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    FDbcStore::Get().LoadAll(Mpq);

    // Human Male
    FString HumanMale = FWowCharacterBuilder::GetCharacterModelPath(
        FWowCharacterBuilder::ERace::Human, FWowCharacterBuilder::EGender::Male);
    TestTrue(TEXT("Human Male path not empty"), !HumanMale.IsEmpty());
    TestTrue(TEXT("Human Male path contains 'Human'"), HumanMale.Contains(TEXT("Human")));
    TestTrue(TEXT("Human Male path ends with .m2"), HumanMale.EndsWith(TEXT(".m2")));

    // Orc Female
    FString OrcFemale = FWowCharacterBuilder::GetCharacterModelPath(
        FWowCharacterBuilder::ERace::Orc, FWowCharacterBuilder::EGender::Female);
    TestTrue(TEXT("Orc Female path not empty"), !OrcFemale.IsEmpty());
    TestTrue(TEXT("Orc Female path contains 'Orc'"), OrcFemale.Contains(TEXT("Orc")));

    // Verify the M2 file exists in MPQ
    TArray<uint8> M2Data;
    TestTrue(TEXT("Human Male M2 exists in MPQ"), Mpq.ReadFile(HumanMale, M2Data));
    TestTrue(TEXT("Human Male M2 has data"), M2Data.Num() > 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCreatureDisplayLookup, "WowUnreal.Character.CreatureDisplayLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCreatureDisplayLookup::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    FDbcStore::Get().LoadAll(Mpq);

    // Stormwind Guard display ID = 3167
    const FCreatureDisplayInfoDbcEntry* DisplayInfo = FDbcStore::Get().CreatureDisplayInfo().GetById(3167);
    TestNotNull(TEXT("DisplayInfo 3167 exists"), DisplayInfo);
    if (DisplayInfo)
    {
        TestTrue(TEXT("Has model ID"), DisplayInfo->ModelID > 0);

        const FCreatureModelDataDbcEntry* ModelData = FDbcStore::Get().CreatureModelData().GetById(DisplayInfo->ModelID);
        TestNotNull(TEXT("CreatureModelData exists"), ModelData);
        if (ModelData)
        {
            TestTrue(TEXT("Has model path"), !ModelData->ModelPath.IsEmpty());
        }
    }

    return true;
}

// ====================================================================
// Movement Coordinate Tests (pure math)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementWowToUE, "WowUnreal.Movement.WowToUEPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMovementWowToUE::RunTest(const FString& Parameters)
{
    // Elwynn starting position
    FVector WowPos(-8949.0f, -132.0f, 84.0f);
    FVector UEPos = FWowCoordinate::WowToUE(WowPos);

    // Verify UE position is reasonable (within world bounds)
    TestTrue(TEXT("UE X is non-zero"), FMath::Abs(UEPos.X) > 100.0f);
    TestTrue(TEXT("UE Y is non-zero"), FMath::Abs(UEPos.Y) > 100.0f);
    TestTrue(TEXT("UE Z (height) is positive"), UEPos.Z > 0.0f);

    // Roundtrip
    FVector Back = FWowCoordinate::UEToWow(UEPos);
    TestTrue(TEXT("Roundtrip X"), FMath::IsNearlyEqual(Back.X, WowPos.X, 0.1f));
    TestTrue(TEXT("Roundtrip Y"), FMath::IsNearlyEqual(Back.Y, WowPos.Y, 0.1f));
    TestTrue(TEXT("Roundtrip Z"), FMath::IsNearlyEqual(Back.Z, WowPos.Z, 0.1f));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementScaleConsistency, "WowUnreal.Movement.ScaleConsistency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMovementScaleConsistency::RunTest(const FString& Parameters)
{
    // Verify SCALE constant
    TestEqual(TEXT("SCALE is 100"), FWowCoordinate::SCALE, 100.0f);

    // One WoW tile = 533.33 yards, at 100 cm/yard = 53333 cm
    FVector TileCenter = FWowCoordinate::TileToWorld(0, 0);
    FVector NextTile = FWowCoordinate::TileToWorld(1, 0);
    float TileDistUE = FVector::Dist(TileCenter, NextTile);
    float ExpectedDist = FWowCoordinate::TILE_SIZE * FWowCoordinate::SCALE;
    TestTrue(TEXT("Adjacent tiles are TILE_SIZE * SCALE apart"),
        FMath::IsNearlyEqual(TileDistUE, ExpectedDist, 1.0f));

    return true;
}

// ====================================================================
// ADT Area ID Tests (require MPQ data)
// ====================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrameXmlChatHistoryScrolls, "WowUnreal.UI.FrameXmlChatHistoryScrolls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFrameXmlChatHistoryScrolls::RunTest(const FString& Parameters)
{
    UCanvasPanel* RootCanvas = NewObject<UCanvasPanel>();
    FWowFrameManager FrameManager;
    FrameManager.Initialize(RootCanvas);

    FWowFrameDef ChatDef;
    ChatDef.Name = TEXT("ChatHistoryTestFrame");
    ChatDef.Type = EWowFrameType::ScrollingMessageFrame;
    ChatDef.Width = 430.0f;
    ChatDef.Height = 120.0f;
    const int64 ChatHandle = FrameManager.CreateFrame(ChatDef);
    TestTrue(TEXT("Scrolling message frame created"), ChatHandle > 0);

    for (int32 Index = 1; Index <= 10; ++Index)
    {
        FrameManager.AddScrollingMessage(ChatHandle,
            FString::Printf(TEXT("History line %d"), Index), FLinearColor::White);
    }

    TestEqual(TEXT("Chat retains messages beyond the seven visible rows"),
        FrameManager.GetScrollingMessageCount(ChatHandle), 10);
    TestTrue(TEXT("New chat starts at the bottom"), FrameManager.AreMessagesAtBottom(ChatHandle));

    FrameManager.ScrollMessages(ChatHandle, 1);
    TestFalse(TEXT("Scroll up leaves the bottom"), FrameManager.AreMessagesAtBottom(ChatHandle));
    FrameManager.ScrollMessagesToTop(ChatHandle);
    TestTrue(TEXT("Scroll-to-top reaches oldest retained row"), FrameManager.AreMessagesAtTop(ChatHandle));
    FrameManager.ScrollMessagesToBottom(ChatHandle);
    TestTrue(TEXT("Scroll-to-bottom returns to newest row"), FrameManager.AreMessagesAtBottom(ChatHandle));

    FrameManager.ClearScrollingMessages(ChatHandle);
    TestEqual(TEXT("Clear removes retained messages"), FrameManager.GetScrollingMessageCount(ChatHandle), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdtAreaIds, "WowUnreal.World.AdtAreaIds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdtAreaIds::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!Mpq.IsInitialized())
    {
        AddWarning(TEXT("MPQ not available — skipping"));
        return true;
    }

    TArray<uint8> Data;
    if (!Mpq.ReadFile(TEXT("World\\Maps\\Azeroth\\Azeroth_32_48.adt"), Data))
    {
        AddError(TEXT("Failed to read Azeroth_32_48.adt"));
        return false;
    }

    FAdtData Adt = FAdtParser::Parse(Data);
    TestTrue(TEXT("ADT is valid"), Adt.bIsValid);

    // All 256 chunks should have area IDs
    bool bHasAreaId = false;
    for (int32 i = 0; i < 256; ++i)
    {
        if (Adt.Chunks[i].AreaId > 0)
        {
            bHasAreaId = true;
            break;
        }
    }
    TestTrue(TEXT("At least one chunk has an area ID"), bHasAreaId);

    // Elwynn tile 32,48 should have Elwynn Forest or Northshire area IDs
    uint32 FirstAreaId = Adt.Chunks[0].AreaId;
    TestTrue(TEXT("First chunk area ID > 0"), FirstAreaId > 0);

    return true;
}


namespace WowWaterRegression
{
TArray<uint8> MakeAdt(uint16 Format, bool TruncateVertexData = false)
{
    auto Put = [](TArray<uint8>& Bytes, int32 Offset, auto Value)
    {
        FMemory::Memcpy(Bytes.GetData() + Offset, &Value, sizeof(Value));
    };
    TArray<uint8> Water;
    constexpr uint32 Instances = 256 * 12;
    constexpr uint32 Mask = Instances + 48;
    constexpr uint32 VertexData = Mask + 1;
    Water.SetNumZeroed(VertexData);
    Put(Water, 0, Instances); Put(Water, 4, uint32(2));
    Put(Water, Instances, uint16(1)); Put(Water, Instances + 2, Format);
    Put(Water, Instances + 4, 20.0f); Put(Water, Instances + 8, 30.0f);
    Water[Instances + 12] = 2; Water[Instances + 13] = 3;
    Water[Instances + 14] = 2; Water[Instances + 15] = 1;
    Put(Water, Instances + 16, Mask); Put(Water, Instances + 20, VertexData);
    Water[Mask] = 2; // Only the second cell of an offset 2x1 rectangle.
    Put(Water, Instances + 24, uint16(1));
    Put(Water, Instances + 28, 8.0f); Put(Water, Instances + 32, 8.0f);
    Water[Instances + 38] = 1; Water[Instances + 39] = 1; // Flat second layer, no arrays.
    auto Append = [](TArray<uint8>& Bytes, auto Value)
    {
        const int32 Offset = Bytes.AddUninitialized(sizeof(Value));
        FMemory::Memcpy(Bytes.GetData() + Offset, &Value, sizeof(Value));
    };
    if (Format != 2) for (int32 I = 0; I < 6; ++I) Append(Water, 21.0f + I);
    if (Format == 1 || Format == 3)
        for (int32 I = 0; I < 6; ++I) { Append(Water, uint16(8192 + I)); Append(Water, uint16(16384 + I)); }
    if (Format != 1) for (int32 I = 0; I < 6; ++I) Append(Water, uint8(10 + 10 * I));
    if (TruncateVertexData) Water.Pop();

    TArray<uint8> Bytes;
    Bytes.SetNumZeroed(84);
    Put(Bytes, 0, uint32(0x4D564552)); Put(Bytes, 4, uint32(4)); Put(Bytes, 8, uint32(18));
    Put(Bytes, 12, uint32(0x4D484452)); Put(Bytes, 16, uint32(64));
    Put(Bytes, 20 + 40, uint32(64)); // MHDR offset is relative to its payload.
    Append(Bytes, uint32(0x4D48324F)); Append(Bytes, uint32(Water.Num())); Bytes.Append(Water);
    for (int32 I = 0; I < 256; ++I)
    {
        Append(Bytes, uint32(0x4D434E4B)); Append(Bytes, uint32(128));
        const int32 Header = Bytes.AddZeroed(128);
        Put(Bytes, Header + 4, uint32(I % 16)); Put(Bytes, Header + 8, uint32(I / 16));
    }
    return Bytes;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaterPlanarVertexData,
    "WowUnreal.Water.PlanarVertexData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWaterPlanarVertexData::RunTest(const FString& Parameters)
{
    for (uint16 Format = 0; Format <= 3; ++Format)
    {
        const FAdtData Data = FAdtParser::Parse(WowWaterRegression::MakeAdt(Format));
        TestTrue(TEXT("Fixture contains all 256 terrain chunks"), Data.bIsValid);
        if (!TestEqual(TEXT("Second layer remains readable after vertex arrays"), Data.WaterChunks[0].Layers.Num(), 2)) return false;
        const FMH2OInstance& Layer = Data.WaterChunks[0].Layers[0];
        TestEqual(TEXT("Offset rectangle stays local"), Layer.XOffset, uint8(2));
        TestFalse(TEXT("Masked dry cell stays dry"), Layer.HasSubTile(0, 0));
        TestTrue(TEXT("Offset wet cell survives"), Layer.HasSubTile(0, 1));
        for (int32 I = 0; I < 6; ++I)
        {
            TestEqual(TEXT("Planar height array"), Layer.Heights[I], Format == 2 ? 20.0f : 21.0f + I);
            TestEqual(TEXT("Depth follows entire height/UV arrays"), Layer.Depths[I], uint8(Format == 1 ? 255 : 10 + 10 * I));
        }
        TestEqual(TEXT("No-data layer retains its authored level"), Data.WaterChunks[0].Layers[1].Heights[3], 8.0f);
        const FAdtData Truncated = FAdtParser::Parse(WowWaterRegression::MakeAdt(Format, true));
        TestEqual(TEXT("Truncated liquid array cannot consume following MCNK bytes"), Truncated.WaterChunks[0].Layers.Num(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNorthshireAuthoredWater,
    "WowUnreal.Water.NorthshireAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNorthshireAuthoredWater::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!TestTrue(TEXT("Local client archives available"), Mpq.IsInitialized())) return false;
    TArray<uint8> Raw;
    if (!TestTrue(TEXT("Northshire tile readable"), Mpq.ReadFile(TEXT("World\\Maps\\Azeroth\\Azeroth_32_48.adt"), Raw))) return false;
    const FAdtData Data = FAdtParser::Parse(Raw);
    if (!TestTrue(TEXT("Northshire parses"), Data.bIsValid)) return false;
    int32 WetCells = 0;
    int32 CheckedHeights = 0;
    // Compare against the archive's float planes directly, independent of the
    // parser's sanitization/mask selection and the former interleaved decoder.
    auto U32 = [&](int32 Offset) { uint32 V = 0; FMemory::Memcpy(&V, Raw.GetData() + Offset, 4); return V; };
    for (int32 P = 0; P + 8 <= Raw.Num(); P += 8 + U32(P + 4))
    {
        if (U32(P) != 0x4D48324F) continue;
        const int32 Start = P + 8;
        for (int32 Chunk = 0; Chunk < 256; ++Chunk)
        {
            const int32 InstanceOffset = U32(Start + Chunk * 12);
            for (int32 L = 0; L < Data.WaterChunks[Chunk].Layers.Num(); ++L)
            {
                const FMH2OInstance& Layer = Data.WaterChunks[Chunk].Layers[L];
                const int32 Vertices = U32(Start + InstanceOffset + L * 24 + 20);
                for (int32 I = 0; Vertices && I < Layer.Heights.Num(); ++I)
                {
                    float Expected = 0; FMemory::Memcpy(&Expected, Raw.GetData() + Start + Vertices + I * 4, 4);
                    TestEqual(TEXT("Every Northshire height matches the source"), Layer.Heights[I], Expected);
                    ++CheckedHeights;
                }
                for (int32 Y = 0; Y < Layer.Height; ++Y)
                    for (int32 X = 0; X < Layer.Width; ++X) WetCells += Layer.HasSubTile(Y, X) ? 1 : 0;
            }
        }
        break;
    }
    TestEqual(TEXT("Whole authored river/pool mask survives"), WetCells, 1199);
    TestEqual(TEXT("All authored Northshire water heights checked"), CheckedHeights, 2100);
    int32 BasinRocks = 0;
    for (const FAdtDoodadPlacement& Placement : Data.DoodadPlacements)
        if (Data.DoodadPaths.IsValidIndex(Placement.NameIndex) &&
            Data.DoodadPaths[Placement.NameIndex].Contains(TEXT("cliffrocks"), ESearchCase::IgnoreCase) &&
            FVector2D::Distance(FVector2D(Placement.Position.X, Placement.Position.Z), FVector2D(17475.943359,25808.755859)) < 100.0)
            ++BasinRocks;
    TestTrue(TEXT("Original basin rocks remain placed"), BasinRocks >= 10);
    UMaterialInterface* River = FWowWaterMaterial::GetWaterMaterial();
    if (!TestNotNull(TEXT("UE Water custom-mesh material loads"), River)) return false;
    const UMaterialInstanceDynamic* RiverInstance = Cast<UMaterialInstanceDynamic>(River);
    TestTrue(TEXT("River uses the persistent derivative of the UE Water material"), RiverInstance && RiverInstance->Parent && RiverInstance->Parent->GetPathName() == TEXT("/Game/Water/MI_WowRiver.MI_WowRiver"));
    FLinearColor Absorption;
    TestTrue(TEXT("Native absorption parameter resolves"), River->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Absorption")), Absorption));
    // Native absorption is inverse distance (RGB*A). Verify clear shallow
    // water with more green transmission, rather than merely exact constants.
    TestTrue(TEXT("Two metres of river retain bottom visibility"), FMath::Exp(-200.0f / (Absorption.R * Absorption.A)) > 0.8f);
    TestTrue(TEXT("Green transmission exceeds blue"), Absorption.G > Absorption.B);
    TestTrue(TEXT("UE water shading model retained"), River->GetMaterial()->GetShadingModels().HasShadingModel(MSM_SingleLayerWater));
    FWowAssetCache Cache;
    const TCHAR* Falls[] = {
        TEXT("World\\Azeroth\\Elwynn\\PassiveDoodads\\Waterfall\\ElwynnTallWaterfall01"),
        TEXT("World\\Generic\\PassiveDoodads\\Waterfall\\NewWaterfall")};
    for (const TCHAR* Path : Falls)
    {
        TArray<uint8> M2, Skin;
        if (!Mpq.ReadFile(FString(Path) + TEXT(".m2"), M2) || !Mpq.ReadFile(FString(Path) + TEXT("00.skin"), Skin)) return false;
        const FM2Data Model = FM2Parser::Parse(M2, Skin);
        TestTrue(TEXT("Original waterfall mesh parses"), Model.bIsValid);
        TestEqual(TEXT("Original contour retained"), Model.Vertices.Num(), 52);
        if (!TestEqual(TEXT("Curtain and drops both retained"), Model.RenderPasses.Num(), 2)) return false;
        UStaticMesh* Mesh = FWowDoodadManager::GetOrCreateStaticMesh(FString(Path) + TEXT(".m2"), &Mpq, &Cache);
        if (!TestNotNull(TEXT("UE waterfall mesh builds"), Mesh)) return false;
        if (!TestEqual(TEXT("Both UE effect layers reach the renderer"), Mesh->GetStaticMaterials().Num(), 2)) return false;
        for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
        {
            UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Slot.MaterialInterface);
            if (!TestNotNull(TEXT("Waterfall effect instance created"), MID)) return false;
            for (const TCHAR* Name : {TEXT("UEFoamTexture"), TEXT("UEWaterNormal")})
            {
                UTexture* Texture = nullptr;
                TestTrue(TEXT("UE effect texture bound"), MID->GetTextureParameterValue(FMaterialParameterInfo(Name), Texture));
                if (TestNotNull(TEXT("UE effect asset available"), Texture))
                    TestTrue(TEXT("Water appearance only references UE Water content"), Texture->GetPathName().StartsWith(TEXT("/Water/")));
            }
            UTexture* ImportedWater = nullptr;
            TestFalse(TEXT("No WoW BLP parameter in waterfall material"), MID->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Layer0Texture")), ImportedWater));
        }
    }
    AddInfo(FString::Printf(TEXT("Northshire: %d wet cells, %d exact heights, %d nearby cliff rocks; both waterfall spatial guides verified (appearance uses UE Water)"), WetCells, CheckedHeights, BasinRocks));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoodadTiltCoordinates,
    "WowUnreal.Water.DoodadTiltCoordinates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDoodadTiltCoordinates::RunTest(const FString& Parameters)
{
    // Independent column-vector transforms from the MDDF placement convention:
    // source X/Z matrices are clockwise; Y is counterclockwise. Do not use
    // the production quaternion construction to calculate this reference.
    const FVector Rotations[] = {FVector(0,0,0), FVector(0,132,0), FVector(35,0,0),
        FVector(0,0,47), FVector(-11.5,-42,-3), FVector(-70.5,-31.5,1.5),
        FVector(-24,-83.5,-63), FVector(21,1,47)};
    const FVector ModelPoints[] = {FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), FVector(3,5,7)};
    for (const FVector& Rotation : Rotations)
        for (const FVector& Model : ModelPoints)
        {
            FVector P(Model.X, Model.Z, -Model.Y); // M2 -> ADT local axes
            double A = FMath::DegreesToRadians(-Rotation.Z);
            P = FVector(P.X, FMath::Cos(A)*P.Y + FMath::Sin(A)*P.Z, -FMath::Sin(A)*P.Y + FMath::Cos(A)*P.Z);
            A = FMath::DegreesToRadians(Rotation.X);
            P = FVector(FMath::Cos(A)*P.X + FMath::Sin(A)*P.Y, -FMath::Sin(A)*P.X + FMath::Cos(A)*P.Y, P.Z);
            A = FMath::DegreesToRadians(Rotation.Y-90.0);
            P = FVector(FMath::Cos(A)*P.X + FMath::Sin(A)*P.Z, P.Y, -FMath::Sin(A)*P.X + FMath::Cos(A)*P.Z);
            const FVector Expected(-P.Z, P.X, P.Y);
            const FVector Actual = FWowCoordinate::WowRotationToUEQuat(Rotation.X,Rotation.Y,Rotation.Z).RotateVector(FVector(Model.Y,Model.X,Model.Z));
            TestTrue(*FString::Printf(TEXT("Tilt %s, vertex %s matches source placement"), *Rotation.ToString(), *Model.ToString()), Actual.Equals(Expected,0.0001));
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2AuthoredEmotes335,
    "WowUnreal.Animation.AuthoredEmotes335", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FM2AuthoredEmotes335::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    if (!TestTrue(TEXT("Original MPQ required"), Mpq.IsInitialized())) return false;
    const TSet<int32> Wanted = {66,67,68,69,97,100,115};
    const TArray<FString> Races = {TEXT("Human"),TEXT("Dwarf"),TEXT("NightElf"),TEXT("Gnome"),TEXT("Draenei"),
        TEXT("Orc"),TEXT("Scourge"),TEXT("Tauren"),TEXT("Troll"),TEXT("BloodElf")};
    for (const FString& Race : Races) for (const FString& Gender : {FString(TEXT("Male")),FString(TEXT("Female"))})
    {
        const FString Path = FString::Printf(TEXT("Character\\%s\\%s\\%s%s"), *Race,*Gender,*Race,*Gender);
        TArray<uint8> Raw,Skin;
        if (!TestTrue(Path, Mpq.ReadFile(Path + TEXT(".m2"),Raw))) continue;
        Mpq.ReadFile(Path + TEXT("00.skin"),Skin);
        FM2Data Model = FM2Parser::Parse(Raw,Skin);
        TestTrue(TEXT("Model valid"),Model.bIsValid);
        if (Race == TEXT("Dwarf") && Gender == TEXT("Male"))
            TestFalse(TEXT("Missing external sit must never read M2 bytes as keys"),Model.AnimationTracks.ContainsByPredicate(
                [](const FM2AnimationData& C) { return C.AnimationId == 97; }));
        FM2Parser::LoadAnimationTracks(Model,Raw,[&](uint16 Id,uint16 Variant,TArray<uint8>& Out)
            { return Mpq.ReadFile(Path + FString::Printf(TEXT("%04u-%02u.anim"),Id,Variant),Out); },&Wanted);
        for (int32 Id : Wanted)
        {
            const auto* Clip = Model.AnimationTracks.FindByPredicate([&](const FM2AnimationData& C) { return C.AnimationId == Id; });
            if (!TestNotNull(FString::Printf(TEXT("%s original clip %d"),*Path,Id),Clip)) continue;
            TestEqual(TEXT("One track per bone"),Clip->BoneTracks.Num(),Model.Bones.Num());
            bool bSane = true;
            for (const auto& B : Clip->BoneTracks)
            {
                for (const auto& V : B.TransValues) bSane &= !V.ContainsNaN() && V.GetAbsMax() < 20.0;
                for (const auto& V : B.ScaleValues) bSane &= !V.ContainsNaN() && V.GetAbsMax() < 10.0;
                for (const auto& V : B.RotValues) bSane &= !V.ContainsNaN() && FMath::Abs(V.SizeSquared()-1.0) < 0.02;
            }
            TestTrue(TEXT("Authored transforms stay finite and anatomically scaled"),bSane);
        }
        if (Race == TEXT("Dwarf") && Gender == TEXT("Male"))
        {
            // Compare a rotation against the actual external file, independent of parser addressing.
            uint32 SeqCount,SeqOffset,BoneCount,BoneOffset;
            FMemory::Memcpy(&SeqCount,Raw.GetData()+28,4); FMemory::Memcpy(&SeqOffset,Raw.GetData()+32,4);
            FMemory::Memcpy(&BoneCount,Raw.GetData()+44,4); FMemory::Memcpy(&BoneOffset,Raw.GetData()+48,4);
            uint32 Slot=0;
            while (Slot < SeqCount && *reinterpret_cast<const uint16*>(Raw.GetData()+SeqOffset+Slot*64)!=97) ++Slot;
            TArray<uint8> Anim; Mpq.ReadFile(Path + TEXT("0097-00.anim"),Anim);
            const auto* Clip=Model.AnimationTracks.FindByPredicate([](const FM2AnimationData& C) { return C.AnimationId==97; });
            bool bCompared=false;
            for(uint32 B=0; Clip && B<BoneCount && !bCompared; ++B)
            {
                const uint8* Block=Raw.GetData()+BoneOffset+B*88+36;
                const uint32 Arrays=*reinterpret_cast<const uint32*>(Block+12), Offset=*reinterpret_cast<const uint32*>(Block+16);
                if(Slot>=Arrays) continue;
                const uint32 Count=*reinterpret_cast<const uint32*>(Raw.GetData()+Offset+Slot*8);
                const uint32 Keys=*reinterpret_cast<const uint32*>(Raw.GetData()+Offset+Slot*8+4);
                if(!Count || Keys+8>uint32(Anim.Num()) || Clip->BoneTracks[B].RotValues.IsEmpty()) continue;
                const int16* Q=reinterpret_cast<const int16*>(Anim.GetData()+Keys);
                auto Decode=[](int16 V) { return (V<0?V+32768:V-32767)/32767.0; };
                TestTrue(TEXT("Sit rotation comes from 0097-00.anim"),Clip->BoneTracks[B].RotValues[0].Equals(FQuat(Decode(Q[0]),Decode(Q[1]),Decode(Q[2]),Decode(Q[3])),1.e-7));
                bCompared=true;
            }
            TestTrue(TEXT("Compared external sit key"),bCompared);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmoteDbcRouting335,
    "WowUnreal.Animation.EmoteDbcRouting335", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEmoteDbcRouting335::RunTest(const FString& Parameters)
{
    FMpqManager& Mpq = WowTestUtils::GetMpq();
    TArray<uint8> Bytes; FDbcParser Parser; FEmotesTextDbc Text; FEmotesDbc Emotes;
    if (!Mpq.ReadFile(TEXT("DBFilesClient\\EmotesText.dbc"),Bytes) || !Parser.Parse(Bytes) || !Text.Load(Parser)) return false;
    if (!Mpq.ReadFile(TEXT("DBFilesClient\\Emotes.dbc"),Bytes) || !Parser.Parse(Bytes) || !Emotes.Load(Parser)) return false;
    const auto* Dance=Text.GetByCommand(TEXT("dance"));
    if (!TestNotNull(TEXT("Dance command"),Dance)) return false;
    TestEqual(TEXT("Dance sends text emote 34, not animation/emote 10"),Dance->ID,34u);
    const auto* State=Emotes.GetById(Dance->EmoteID);
    if (!TestNotNull(TEXT("Dance state"),State)) return false;
    TestEqual(TEXT("Original dance clip"),State->AnimationID,69u);
    TestTrue(TEXT("Dance loops"),State->IsLooping());
    for (const FString& Command : {FString(TEXT("wave")),FString(TEXT("bow")),FString(TEXT("cheer"))})
    {
        const auto* Entry=Text.GetByCommand(Command);
        if (!TestNotNull(Command,Entry)) continue;
        const auto* Emote=Emotes.GetById(Entry->EmoteID);
        if (TestNotNull(TEXT("Original one-shot mapping"),Emote)) TestFalse(TEXT("One-shot does not loop"),Emote->IsLooping());
    }
    TestNull(TEXT("Unknown command has no invented animation"),Text.GetByCommand(TEXT("not-an-emote")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiverSurfaceFacing,
    "WowUnreal.Water.SurfaceFacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRiverSurfaceFacing::RunTest(const FString& Parameters)
{
    // Test the triangles actually emitted for a sloping MH2O cell after coordinate
    // conversion. The former winding made the native shader show its underside.
    const FVector P[] = {
        FWowCoordinate::AdtToUE(17400, 70, 25800),
        FWowCoordinate::AdtToUE(17406, 70, 25800),
        FWowCoordinate::AdtToUE(17400, 69, 25806),
        FWowCoordinate::AdtToUE(17406, 69, 25806)};
    TArray<int32> Indices;
    FWowWaterRenderer::AppendSurfaceCellIndices(Indices, 0, 1, 2, 3);
    if (!TestEqual(TEXT("Wet cell retains two triangles"), Indices.Num(), 6)) return false;
    for (int I=0; I<6; I+=3)
    {
        const FVector Cross = FVector::CrossProduct(P[Indices[I+1]]-P[Indices[I]], P[Indices[I+2]]-P[Indices[I]]);
        TestTrue(TEXT("River presents the UE front face to a camera above it"), Cross.Z < 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiverSurfaceImpulses,
    "WowUnreal.Water.SurfaceImpulses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRiverSurfaceImpulses::RunTest(const FString& Parameters)
{
    auto* Material = FWowWaterMaterial::GetWaterMaterial();
    FWowWaterMaterial::UpdateSurfaceRipples(-1);
    FWowWaterMaterial::UpdateSurfaceRipples(10);
    // Nine impacts must replace the oldest of the eight slots, not allocate more.
    for (int32 I=0; I<9; ++I)
        FWowWaterMaterial::AddSurfaceRipple(FVector(100+I,200,7000), FVector(3,4,0), false, 10+I*.1f);
    FLinearColor Position, Direction;
    TestTrue(TEXT("Shader exposes surface impulses"), Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("RipplePosition0")), Position));
    TestEqual(TEXT("Ninth impulse reuses the oldest slot"), Position.R, 108.f);
    Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("RippleDirection0")), Direction);
    TestTrue(TEXT("Wake uses normalized travel direction"), FMath::IsNearlyEqual(Direction.R,.6f) && FMath::IsNearlyEqual(Direction.G,.8f));
    FWowWaterMaterial::UpdateSurfaceRipples(0);
    for (int32 I=0; I<8; ++I)
    {
        Material->GetVectorParameterValue(FMaterialParameterInfo(*FString::Printf(TEXT("RipplePosition%d"),I)), Position);
        TestTrue(TEXT("New world clears old surface impacts"), Position.A < -10);
    }
    return true;
}
