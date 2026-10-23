#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowConnectionManager.h"
#include "WowLunarPhase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealmClockTest,"WowUnreal.Sky.Clock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRealmClockTest::RunTest(const FString& Parameters)
{
    FWowRealmClock Clock;
    const FDateTime Local(2026,9,23,15,27,30);
    TestEqual(TEXT("Offline uses local hour, minute and second"),Clock.Sample(42,Local),927.5);
    TestTrue(TEXT("Packed realm time accepted"),Clock.Synchronize((23u<<6)|59u,1.f/60.f,100));
    TestTrue(TEXT("60 real seconds advance one game minute and cross midnight"),FMath::IsNearlyEqual(Clock.Sample(160,Local),0.0,0.0001));
    TestTrue(TEXT("Alt-tab elapsed wall time is not capped by frame delta"),FMath::IsNearlyEqual(Clock.Sample(3700,Local),59.0,0.0001));
    TestTrue(TEXT("Server can explicitly freeze time"),Clock.Synchronize((4u<<6)|15u,0,100));
    TestEqual(TEXT("Frozen realm time"),Clock.Sample(3700,Local),255.0);
    TestFalse(TEXT("Reject impossible hour"),Clock.Synchronize(31u<<6,1.f/60.f,0));
    TestFalse(TEXT("Reject impossible minute"),Clock.Synchronize(63u,1.f/60.f,0));
    TestFalse(TEXT("Reject invalid speed"),Clock.Synchronize(0,-1,0));
    TestEqual(TEXT("Bad packet does not replace clock"),Clock.Sample(3700,Local),255.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealmClockPacketTest,"WowUnreal.Sky.ClockPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRealmClockPacketTest::RunTest(const FString& Parameters)
{
    UWowConnectionManager* Connection=NewObject<UWowConnectionManager>();
    TArray<uint8> Packet;
    const uint32 Packed=(26u<<24)|(8u<<20)|(22u<<14)|(21u<<6)|37u;
    const float Speed=0.01666667f;
    Packet.Append(reinterpret_cast<const uint8*>(&Packed),4);
    Packet.Append(reinterpret_cast<const uint8*>(&Speed),4);
    Packet.AddZeroed(4);
    Connection->PacketHandler.HandlePacket(WowOpcode::SMSG_LOGIN_SETTIMESPEED,Packet);
    TestTrue(TEXT("Login opcode drives realm clock"),Connection->PacketHandler.RealmClock.bSynchronized);
    TestEqual(TEXT("Packed date bits do not alter wall time"),Connection->PacketHandler.RealmClock.MinutesAtSync,1297.0);
    Connection->PacketHandler.HandlePacket(WowOpcode::SMSG_LOGIN_SETTIMESPEED,{0,1,2});
    TestEqual(TEXT("Truncated packet leaves valid sync intact"),Connection->PacketHandler.RealmClock.MinutesAtSync,1297.0);
    Connection->ConfigureOfflineSession({},0);
    TestFalse(TEXT("Switch to offline clears old server time"),Connection->PacketHandler.RealmClock.bSynchronized);
    Connection->PacketHandler.HandlePacket(WowOpcode::SMSG_LOGIN_SETTIMESPEED,Packet);
    Connection->Disconnect();
    TestFalse(TEXT("Disconnect clears old server time"),Connection->PacketHandler.RealmClock.bSynchronized);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLunarPhaseTest,"WowUnreal.Sky.LunarPhase",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLunarPhaseTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Reference new moon"),WowLunarPhase::Cycle(FDateTime(2000,1,6,18,14)),0.f);
    TestTrue(TEXT("Known January 2000 full moon is bright"),WowLunarPhase::Illumination(WowLunarPhase::Cycle(FDateTime(2000,1,21,4,40)))>0.99f);
    TestTrue(TEXT("Earlier dates wrap correctly"),WowLunarPhase::Cycle(FDateTime(1999,12,1))>=0.f);
    TestEqual(TEXT("New moon illumination"),WowLunarPhase::Illumination(0),0.f);
    TestEqual(TEXT("Quarter moon illumination"),WowLunarPhase::Illumination(0.25f),0.5f);
    TestEqual(TEXT("Full moon illumination"),WowLunarPhase::Illumination(0.5f),1.f);
    TestEqual(TEXT("Stars hidden at noon"),WowLunarPhase::NightVisibility(720),0.f);
    TestEqual(TEXT("Stars visible at midnight"),WowLunarPhase::NightVisibility(0),1.f);
    TestTrue(TEXT("Dawn fades smoothly"),WowLunarPhase::NightVisibility(345)>0.f&&WowLunarPhase::NightVisibility(345)<1.f);
    return true;
}
