#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowMovementPacket.h"
#include "WowOpcodes.h"

// Every MovementInfo flag combination we can send must contain exactly the
// optional blocks the server reads for those flags (TormentWoW rejected
// MSG_MOVE_HEARTBEAT packets that claimed SWIMMING/FALLING without the data).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowMovementPacketLayout, "WowUnreal.Network.MovementPacketLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWowMovementPacketLayout::RunTest(const FString&)
{
    using namespace WowMoveFlag;
    const uint32 Combos[] = {
        0u, FORWARD, FORWARD | WALKING, BACKWARD | STRAFE_LEFT, SWIMMING, SWIMMING | FORWARD,
        FALLING, FALLING | FORWARD, FALLING | SWIMMING, ROOT, FLYING | CAN_FLY, SPLINE_ELEVATION,
        FALLING | SPLINE_ELEVATION | SWIMMING | WATERWALKING | HOVER | FALLING_SLOW,
        ONTRANSPORT /* stripped: no transport guid */ };
    for (const uint32 Flags : Combos)
    {
        FWowOutgoingMovementInfo Info;
        Info.Guid = 0x0000000000001234ull;
        Info.Flags = Flags;
        Info.TimeMs = 4242;
        Info.Position = FVector(-8949.95f, -132.49f, 83.53f);
        Info.Orientation = 1.5f;
        Info.Pitch = 0.25f;
        Info.FallTimeMs = 350;
        Info.JumpZSpeed = -7.955547f;
        Info.JumpXYSpeed = 7.0f;
        const TArray<uint8> Payload = WowMovementPacket::BuildMovePayload(Info);
        const uint32 Effective = Flags & ~ONTRANSPORT;
        const int32 Expected = 3 /* packed guid: mask + 2 bytes */ + WowMovementPacket::ExpectedBodySize(Effective, 0);
        TestEqual(*FString::Printf(TEXT("Length for flags 0x%08X"), Flags), Payload.Num(), Expected);

        // Round-trip through the client's own server-side movement parser.
        FWowPacketHandler Handler;
        Handler.EntityManager.GetOrCreate(Info.Guid);
        Handler.HandlePacket(WowOpcode::MSG_MOVE_HEARTBEAT, Payload);
        if (const FWowEntity* Parsed = Handler.EntityManager.Find(Info.Guid))
        {
            TestTrue(*FString::Printf(TEXT("Flags survive 0x%08X"), Flags), Parsed->Movement.MoveFlags == Effective);
            TestTrue(*FString::Printf(TEXT("Fall time aligned 0x%08X"), Flags), Parsed->Movement.FallTime == 350u);
            TestTrue(*FString::Printf(TEXT("Position aligned 0x%08X"), Flags),
                Parsed->Movement.Position.Equals(Info.Position, 0.01f));
        }
    }

    // Force-speed ack: guid, counter, MovementInfo, new speed.
    FWowOutgoingMovementInfo Ack;
    Ack.Guid = 0x55;
    Ack.Flags = FORWARD;
    TArray<uint8> AckPayload = WowMovementPacket::BuildAckPayload(Ack, 7);
    WowMovementPacket::AppendFloat(AckPayload, 9.8f);
    TestEqual(TEXT("Speed ack length"), AckPayload.Num(), 2 + 4 + WowMovementPacket::ExpectedBodySize(FORWARD, 0) + 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowForcedSpeedChange, "WowUnreal.Network.ForcedSpeedChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWowForcedSpeedChange::RunTest(const FString&)
{
    FWowPacketHandler Handler;
    struct { uint64 Guid = 0x77; } Self;
    Handler.EntityManager.GetOrCreate(Self.Guid);
    Handler.EntityManager.LocalPlayerGuid = Self.Guid;
    FWowForcedMovement Received;
    int32 Count = 0;
    Handler.OnForcedMovement.AddLambda([&](const FWowForcedMovement& Change) { Received = Change; ++Count; });

    // SMSG_FORCE_RUN_SPEED_CHANGE: packed guid, counter, u8, float.
    TArray<uint8> P;
    WowMovementPacket::AppendPackedGuid(P, Self.Guid);
    WowMovementPacket::AppendU32(P, 3);
    P.Add(0);
    WowMovementPacket::AppendFloat(P, 9.1f);
    Handler.HandlePacket(0x0E2, P);
    TestEqual(TEXT("One forced change"), Count, 1);
    TestTrue(TEXT("Ack opcode"), Received.AckOpcode == 0x0E3);
    TestTrue(TEXT("Counter echoed"), Received.Counter == 3u);
    TestEqual(TEXT("Run speed stored"), Handler.EntityManager.Find(Self.Guid)->Movement.RunSpeed, 9.1f);

    // SMSG_FORCE_MOVE_ROOT: packed guid, counter.
    P.Reset();
    WowMovementPacket::AppendPackedGuid(P, Self.Guid);
    WowMovementPacket::AppendU32(P, 4);
    Handler.HandlePacket(0x0E8, P);
    TestTrue(TEXT("Root forwarded"), Received.Kind == EWowForcedMoveKind::Root);
    TestTrue(TEXT("Root ack opcode"), Received.AckOpcode == 0x0E9);
    return true;
}
