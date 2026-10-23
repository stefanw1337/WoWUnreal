#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowRemoteMovement.h"
#include "WowPacketHandler.h"
#include "WowOpcodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowRemoteMovementContinuityTest,
    "WowUnreal.CombatMotion.RemotePlayerContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowRemoteMovementContinuityTest::RunTest(const FString&)
{
    FWowMovementInfo Move;
    Move.Position = FVector(100,200,3); Move.MoveFlags=1;
    Move.MovementReceivedAtSeconds=100;
    TestTrue(TEXT("Run continues between half-second heartbeats"),
        PredictWowRemoteGroundPosition(Move,100.4).Equals(FVector(102.8,200,3),.001));
    const FVector Before=PredictWowRemoteGroundPosition(Move,100.5);
    Move.Position.X+=3.5; Move.MovementReceivedAtSeconds=100.5;
    TestTrue(TEXT("Next consistent heartbeat is continuous"),
        PredictWowRemoteGroundPosition(Move,100.5).Equals(Before,.001));
    Move.MoveFlags=0;
    TestTrue(TEXT("Stop packet halts prediction immediately"),
        PredictWowRemoteGroundPosition(Move,100.8).Equals(Move.Position));
    Move.MoveFlags=1;
    TestTrue(TEXT("Missing packets cannot cause unlimited movement"),
        PredictWowRemoteGroundPosition(Move,200).Equals(Move.Position+FVector(7,0,0),.001));
    Move.MoveFlags=2;
    TestTrue(TEXT("Backpedal uses backward speed"),
        PredictWowRemoteGroundPosition(Move,101).Equals(Move.Position-FVector(2.25,0,0),.001));
    Move.MoveFlags=5;
    TestTrue(TEXT("Diagonal is normalized"), FMath::IsNearlyEqual(
        FVector::Dist(PredictWowRemoteGroundPosition(Move,101),Move.Position),3.5,.001));
    Move.MoveFlags=1|0x100;
    TestTrue(TEXT("Walking uses walk speed"),
        PredictWowRemoteGroundPosition(Move,101).Equals(Move.Position+FVector(1.25,0,0),.001));
    for (uint32 Flag : {0x200u,0x400u,0x1000u,0x2000u,0x200000u,0x2000000u})
    {
        Move.MoveFlags=1|Flag;
        TestTrue(TEXT("Special motion is not projected as ground walking"),
            PredictWowRemoteGroundPosition(Move,101).Equals(Move.Position));
    }
    Move.MoveFlags=1; Move.bHasActiveSpline=true;
    TestTrue(TEXT("Server spline retains ownership"),PredictWowRemoteGroundPosition(Move,101).Equals(Move.Position));
    Move.bHasActiveSpline=false; Move.Orientation=UE_PI/2;
    TestTrue(TEXT("Facing rotates velocity in WoW coordinates"),
        PredictWowRemoteGroundPosition(Move,101).Equals(Move.Position+FVector(0,3.5,0),.001));
    FWowPacketHandler Handler;
    auto& Player=Handler.EntityManager.PromoteToTyped(7,WowTypeMask::PLAYER|WowTypeMask::UNIT);
    Player.Movement.RunSpeed=9.f;
    int32 FieldUpdates=0;
    Handler.EntityManager.OnEntityUpdated.AddLambda([&](const FWowEntity&){++FieldUpdates;});
    TArray<uint8> Packet;
    auto Append=[&](const auto& Value)
    {
        const auto* Bytes=reinterpret_cast<const uint8*>(&Value);
        Packet.Append(Bytes,sizeof(Value));
    };
    Append(uint8(1)); Append(uint8(7)); // Packed GUID.
    Append(uint32(1)); Append(uint16(0)); Append(uint32(1234));
    Append(100.f); Append(200.f); Append(3.f); Append(0.f); Append(uint32(0));
    Handler.HandlePacket(WowOpcode::MSG_MOVE_HEARTBEAT,Packet);
    TestEqual(TEXT("Packet records sender timestamp"),Player.Movement.MovementTimestamp,uint32(1234));
    TestTrue(TEXT("Packet records local receipt clock"),Player.Movement.MovementReceivedAtSeconds>0.0);
    TestEqual(TEXT("Heartbeat retains authoritative speed"),Player.Movement.RunSpeed,9.f);
    TestEqual(TEXT("Remote movement does not refresh unchanged health and appearance"),FieldUpdates,0);
    return true;
}
#endif
