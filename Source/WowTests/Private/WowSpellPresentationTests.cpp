#include "Misc/AutomationTest.h"
#include "WowAnimationController.h"
#include "WowOpcodes.h"
#include "WowPacketHandler.h"

namespace
{
void AppendU32(TArray<uint8>& Bytes, uint32 Value)
{
	Bytes.Add(static_cast<uint8>(Value));
	Bytes.Add(static_cast<uint8>(Value >> 8));
	Bytes.Add(static_cast<uint8>(Value >> 16));
	Bytes.Add(static_cast<uint8>(Value >> 24));
}

void AppendSmallPackedGuid(TArray<uint8>& Bytes, uint8 LowByte)
{
	Bytes.Add(0x01);
	Bytes.Add(LowByte);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellAnimationPresentationContract,
	"WowUnreal.Combat.Spells.AnimationPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSpellAnimationPresentationContract::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A timed directed cast holds its ready pose"),
		FWowSpellAnimationPolicy::ResolveReadyAnimation(0, false),
		EWowAnimId::ReadySpellDirected);
	TestEqual(TEXT("An instant spell uses the omni release fallback"),
		FWowSpellAnimationPolicy::ResolveReleaseAnimation(0, true),
		EWowAnimId::SpellCastOmni);
	TestEqual(TEXT("A channel has a dedicated authored loop"),
		FWowSpellAnimationPolicy::ResolveChannelAnimation(0, false),
		EWowAnimId::ChannelCast);
	TestEqual(TEXT("A DBC-authored animation overrides the fallback"),
		FWowSpellAnimationPolicy::ResolveReleaseAnimation(32, true),
		EWowAnimId::SpellCast);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellChannelPacketContract,
	"WowUnreal.Combat.Spells.ChannelAndPushbackPackets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSpellChannelPacketContract::RunTest(const FString& Parameters)
{
	FWowPacketHandler Handler;
	uint64 StartedCaster = 0;
	uint32 StartedSpell = 0;
	uint32 StartedDuration = 0;
	uint32 UpdatedRemaining = MAX_uint32;
	uint32 DelayedBy = 0;
	Handler.OnChannelStart.AddLambda([&](uint64 Caster, uint32 Spell, uint32 Duration)
	{
		StartedCaster = Caster;
		StartedSpell = Spell;
		StartedDuration = Duration;
	});
	Handler.OnChannelUpdate.AddLambda([&](uint64, uint32 Remaining)
	{
		UpdatedRemaining = Remaining;
	});
	Handler.OnSpellDelayed.AddLambda([&](uint64, uint32 Delay)
	{
		DelayedBy = Delay;
	});

	TArray<uint8> Start;
	AppendSmallPackedGuid(Start, 0x43);
	AppendU32(Start, 635);
	AppendU32(Start, 8000);
	Handler.HandlePacket(WowOpcode::MSG_CHANNEL_START, Start);
	TestEqual(TEXT("Channel start preserves the packed caster GUID"), StartedCaster, uint64(0x43));
	TestEqual(TEXT("Channel start preserves the spell id"), StartedSpell, uint32(635));
	TestEqual(TEXT("Channel start is measured in milliseconds"), StartedDuration, uint32(8000));

	TArray<uint8> Update;
	AppendSmallPackedGuid(Update, 0x43);
	AppendU32(Update, 5750);
	Handler.HandlePacket(WowOpcode::MSG_CHANNEL_UPDATE, Update);
	TestEqual(TEXT("Channel pushback updates remaining time"), UpdatedRemaining, uint32(5750));

	TArray<uint8> Delayed;
	AppendSmallPackedGuid(Delayed, 0x43);
	AppendU32(Delayed, 500);
	Handler.HandlePacket(WowOpcode::SMSG_SPELL_DELAYED, Delayed);
	TestEqual(TEXT("Normal cast pushback preserves the added delay"), DelayedBy, uint32(500));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellGoTargetPacketContract,
	"WowUnreal.Combat.Spells.ImpactTargetPacket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSpellGoTargetPacketContract::RunTest(const FString& Parameters)
{
	FWowPacketHandler Handler;
	uint64 ReceivedCaster = 0;
	uint64 ReceivedTarget = 0;
	uint32 ReceivedSpell = 0;
	Handler.OnSpellGo.AddLambda([&](uint64 Caster, uint64 Target, uint32 Spell, uint32)
	{
		ReceivedCaster = Caster;
		ReceivedTarget = Target;
		ReceivedSpell = Spell;
	});

	TArray<uint8> Packet;
	AppendSmallPackedGuid(Packet, 0x42); // item/cast source
	AppendSmallPackedGuid(Packet, 0x43); // actual caster
	Packet.Add(1);                      // cast counter
	AppendU32(Packet, 635);             // Holy Light
	AppendU32(Packet, 0);               // cast flags
	AppendU32(Packet, 1234);            // server timestamp
	Packet.Add(1);                      // one hit target
	AppendU32(Packet, 0x44); // hit lists use full GUIDs
	AppendU32(Packet, 0xF1300001);
	Packet.Add(0);                      // no misses
	Handler.HandlePacket(WowOpcode::SMSG_SPELL_GO, Packet);

	TestEqual(TEXT("Spell go exposes its actual caster"), ReceivedCaster, uint64(0x43));
	TestEqual(TEXT("Spell go exposes the first impact target"), ReceivedTarget, uint64(0xF130000100000044ULL));
	TestEqual(TEXT("Spell go exposes the spell id"), ReceivedSpell, uint32(635));
	return true;
}


#include "Formats/Dbc/SpellVisualKitDbc.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellVisualKit335Layout,
    "WowUnreal.Combat.Spells.VisualKit335Layout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSpellVisualKit335Layout::RunTest(const FString& Parameters)
{
    // WDBC 12340: two animations then eight attachment effects, three special
    // effects, world, sound, shake, character procedures/parameters, flags.
    TArray<uint8> Data = {'W', 'D', 'B', 'C'};
    AppendU32(Data, 1); AppendU32(Data, 38); AppendU32(Data, 152); AppendU32(Data, 1);
    const uint32 Prefix[] = {77, 51, 53, 101, 102, 103, 104, 105, 106, 107, 108,
        201, 202, 203, 301, 401, 501};
    for (uint32 Value : Prefix) AppendU32(Data, Value);
    for (int32 I = UE_ARRAY_COUNT(Prefix); I < 38; ++I) AppendU32(Data, 0);
    Data.Add(0);
    FDbcParser Parser;
    TestTrue(TEXT("Valid build 12340 DBC"), Parser.Parse(Data));
    FSpellVisualKitDbc Table;
    TestTrue(TEXT("Visual kits load"), Table.Load(Parser));
    const auto* Kit = Table.GetById(77);
    if (!TestNotNull(TEXT("Visual kit found"), Kit)) return false;
    TestEqual(TEXT("Head begins immediately after the animations"), Kit->HeadEffectID, 101u);
    TestEqual(TEXT("Left hand stays attached to the left-hand effect"), Kit->LeftHandEffectID, 104u);
    TestEqual(TEXT("Right hand stays attached to the right-hand effect"), Kit->RightHandEffectID, 105u);
    TestEqual(TEXT("World effect follows all three special slots"), Kit->WorldEffectID, 301u);
    TestEqual(TEXT("Sound is not confused with a special effect"), Kit->SoundID, 401u);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellMultipleHitsAndMisses335,
    "WowUnreal.Combat.Spells.MultipleHitsAndMisses335",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSpellMultipleHitsAndMisses335::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    TArray<uint64> Hits;
    int32 Releases = 0;
    Handler.OnSpellGo.AddLambda([&](uint64, uint64, uint32, uint32) { ++Releases; });
    Handler.OnSpellHitTargets.AddLambda([&](uint64, uint32, const TArray<uint64>& Targets) { Hits = Targets; });
    TArray<uint8> Header = {1, 0x42, 1, 0x43, 1};
    AppendU32(Header, 133); AppendU32(Header, 0); AppendU32(Header, 1234);
    auto Guid = [](TArray<uint8>& Bytes, uint64 Value)
    {
        AppendU32(Bytes, uint32(Value)); AppendU32(Bytes, uint32(Value >> 32));
    };
    TArray<uint8> Packet = Header;
    Packet.Add(2);
    Guid(Packet, 0xF1300102000044ULL); Guid(Packet, 0xF1300102000099ULL);
    Packet.Add(1); Guid(Packet, 0xF1300102000077ULL); Packet.Add(11); Packet.Add(1);
    Handler.HandlePacket(WowOpcode::SMSG_SPELL_GO, Packet);
    TestEqual(TEXT("Both hit targets survive the full-GUID lists"), Hits.Num(), 2);
    if (Hits.Num() == 2) TestEqual(TEXT("Second target is intact"), Hits[1], uint64(0xF1300102000099ULL));
    TestEqual(TEXT("One release regardless of number of targets"), Releases, 1);
    TArray<uint8> AllMiss = Header;
    AllMiss.Add(0); AllMiss.Add(1); Guid(AllMiss, 0xF1300102000077ULL); AllMiss.Add(2);
    Handler.HandlePacket(WowOpcode::SMSG_SPELL_GO, AllMiss);
    TestTrue(TEXT("All-miss cast has no fabricated impact target"), Hits.IsEmpty());
    TestEqual(TEXT("An all-miss cast still releases"), Releases, 2);
    Packet.Pop(); // truncated reflected result
    Handler.HandlePacket(WowOpcode::SMSG_SPELL_GO, Packet);
    TestEqual(TEXT("Truncated reflect does not publish a partial release"), Releases, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMelee335OptionalDamageBlocks,
    "WowUnreal.Combat.Spells.Melee335OptionalDamageBlocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMelee335OptionalDamageBlocks::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    uint32 Damage = 0, Flags = 0;
    int32 Events = 0;
    Handler.OnAttackerStateUpdate.AddLambda([&](uint64, uint64, uint32 HitInfo, uint32 Value)
    { Flags = HitInfo; Damage = Value; ++Events; });
    TArray<uint8> Packet;
    AppendU32(Packet, 0x2260); // critical, block, absorb
    AppendSmallPackedGuid(Packet, 0x42); AppendSmallPackedGuid(Packet, 0x43);
    AppendU32(Packet, 73); AppendU32(Packet, 19); // total and overkill
    Packet.Add(1); AppendU32(Packet, 1); AppendU32(Packet, 0x42920000); AppendU32(Packet, 73);
    AppendU32(Packet, 7); // absorb for the one school
    Packet.Add(1); AppendU32(Packet, 0); AppendU32(Packet, 0); AppendU32(Packet, 12);
    Handler.HandlePacket(WowOpcode::SMSG_ATTACKERSTATEUPDATE, Packet);
    TestEqual(TEXT("Damage is independent of overkill"), Damage, 73u);
    TestEqual(TEXT("Critical uses HITINFO bit 0x200"), Flags & 0x200u, 0x200u);
    TestEqual(TEXT("Valid melee payload broadcasts once"), Events, 1);
    Packet.Pop();
    Handler.HandlePacket(WowOpcode::SMSG_ATTACKERSTATEUPDATE, Packet);
    TestEqual(TEXT("Truncated block data does not publish a hit"), Events, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcGreeting335,
    "WowUnreal.Combat.Spells.QuestNpcGreeting335",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNpcGreeting335::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    FString Greeting;
    Handler.OnNpcText.AddLambda([&](uint32, const FString& Text) { Greeting = Text; });
    TArray<uint8> Packet;
    AppendU32(Packet, 42);
    auto String = [](TArray<uint8>& Bytes, const ANSICHAR* Text)
    { Bytes.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text) + 1); };
    for (int32 I = 0; I < 8; ++I)
    {
        AppendU32(Packet, I == 1 ? 0x3F800000 : 0);
        String(Packet, I == 1 ? "Greetings $N!$BDefend Northshire." : "");
        String(Packet, "");
        for (int32 J = 0; J < 7; ++J) AppendU32(Packet, 0);
    }
    Handler.HandlePacket(WowOpcode::SMSG_NPC_TEXT_UPDATE, Packet);
    TestEqual(TEXT("Real server greeting is retained"), Greeting, FString(TEXT("Greetings $N!$BDefend Northshire.")));
    TestEqual(TEXT("Greeting is cached by text ID"), Handler.NpcTextCache.FindRef(42), Greeting);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuraSnapshot335,
    "WowUnreal.Combat.Spells.AuraSnapshot335",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAuraSnapshot335::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    FWowEntity& Entity = Handler.EntityManager.GetOrCreate(0x43);
    Entity.Auras[7].bActive = true;
    Entity.Auras[7].SpellId = 999;
    TArray<uint8> Packet = {1, 0x43, 1};
    AppendU32(Packet, 1459);
    Packet.Append({0x39, 10, 1}); // positive self-cast with duration, no caster GUID
    AppendU32(Packet, 1800000); AppendU32(Packet, 900000);
    Packet.Add(2); AppendU32(Packet, 589);
    Packet.Append({0x21, 12, 1, 1, 0x55}); // external caster, packed GUID
    AppendU32(Packet, 18000); AppendU32(Packet, 17000);
    Handler.HandlePacket(0x495, Packet); // actual server full-aura opcode
    TestFalse(TEXT("Full snapshot clears missing old auras"), Entity.Auras[7].bActive);
    TestEqual(TEXT("Self-cast GUID does not consume duration bytes"), Entity.Auras[1].CasterGuid, uint64(0x43));
    TestEqual(TEXT("Self-cast duration is aligned"), Entity.Auras[1].RemainingMs, 900000u);
    TestEqual(TEXT("Second aura retains external caster"), Entity.Auras[2].CasterGuid, uint64(0x55));
    Packet.Pop();
    Entity.Auras[7].bActive = true;
    Handler.HandlePacket(0x495, Packet);
    TestTrue(TEXT("Truncated snapshot leaves existing state intact"), Entity.Auras[7].bActive);
    TArray<uint8> Remove = {1, 0x43, 1}; AppendU32(Remove, 0);
    Handler.HandlePacket(0x496, Remove);
    TestFalse(TEXT("Single-slot removal uses its own opcode"), Entity.Auras[1].bActive);
    TestTrue(TEXT("Single-slot removal leaves other slots intact"), Entity.Auras[2].bActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLootAcknowledgements335,
    "WowUnreal.Combat.Spells.LootAcknowledgements335",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLootAcknowledgements335::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    uint64 OpenedGuid = 0;
    uint32 Gold = 0;
    TArray<FWowLootItem> Loot;
    int32 OpenEvents = 0, CloseEvents = 0, MoneyEvents = 0;
    uint8 RemovedSlot = 255;
    Handler.OnLootOpened.AddLambda([&](uint64 Guid, uint8, uint32 Money, const TArray<FWowLootItem>& Items)
    { OpenedGuid = Guid; Gold = Money; Loot = Items; ++OpenEvents; });
    Handler.OnLootClosed.AddLambda([&]() { ++CloseEvents; });
    Handler.OnLootMoneyCleared.AddLambda([&]() { ++MoneyEvents; });
    Handler.OnLootSlotRemoved.AddLambda([&](uint8 Slot) { RemovedSlot = Slot; });
    TArray<uint8> Packet;
    AppendU32(Packet, 0x44); AppendU32(Packet, 0xF1300012);
    Packet.Add(1); AppendU32(Packet, 12345); Packet.Add(2);
    for (uint8 Slot : {uint8(3), uint8(7)})
    {
        Packet.Add(Slot);
        AppendU32(Packet, 117); AppendU32(Packet, 2); AppendU32(Packet, 100);
        AppendU32(Packet, 0); AppendU32(Packet, 0); Packet.Add(0);
    }
    Handler.HandlePacket(0x160, Packet);
    TestEqual(TEXT("Corpse full GUID is not parsed as packed"), OpenedGuid, uint64(0xF130001200000044ULL));
    TestEqual(TEXT("Money survives GUID alignment"), Gold, 12345u);
    TestEqual(TEXT("All loot slots received"), Loot.Num(), 2);
    if (Loot.Num() == 2) TestEqual(TEXT("Non-consecutive slot retained"), Loot[1].Index, uint8(7));
    Packet.Pop(); Handler.HandlePacket(0x160, Packet);
    TestEqual(TEXT("Truncated item does not replace current loot"), OpenEvents, 1);
    Handler.HandlePacket(0x162, TArray<uint8>{7});
    Handler.HandlePacket(0x165, TArray<uint8>{});
    TestEqual(TEXT("Server removes the acknowledged slot"), RemovedSlot, uint8(7));
    TestEqual(TEXT("Money acknowledgement dispatched"), MoneyEvents, 1);
    TArray<uint8> Close;
    AppendU32(Close, 0x99); AppendU32(Close, 0xF1300012); Close.Add(1);
    Handler.HandlePacket(0x161, Close);
    TestEqual(TEXT("Old corpse release cannot close current loot"), CloseEvents, 0);
    Close[0] = 0x44; Handler.HandlePacket(0x161, Close);
    TestEqual(TEXT("Matching corpse release closes once"), CloseEvents, 1);
    TestEqual(TEXT("Release and collect-money are distinct opcodes"), WowOpcode::CMSG_LOOT_RELEASE, uint16(0x15F));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementAndTeleport335,
    "WowUnreal.Combat.Spells.MovementAndTeleport335",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMovementAndTeleport335::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    Handler.EntityManager.LocalPlayerGuid = 0x43;
    FWowEntity& Entity = Handler.EntityManager.GetOrCreate(0x43);
    Entity.Movement.RunSpeed = 14.0f;
    int32 Teleports = 0;
    uint32 Counter = 0;
    FVector Destination = FVector::ZeroVector;
    Handler.OnTeleportRequest.AddLambda([&](uint64, uint32 Order, uint32, FVector Pos, float)
    { ++Teleports; Counter = Order; Destination = Pos; });
    auto Float = [](TArray<uint8>& Bytes, float Value)
    { Bytes.Append(reinterpret_cast<const uint8*>(&Value), 4); };
    TArray<uint8> Movement;
    AppendU32(Movement, 0x200); // on transport
    Movement.Append({0x20, 0x04}); // always pitch + interpolated transport time
    AppendU32(Movement, 1234);
    Float(Movement, 10); Float(Movement, 20); Float(Movement, 30); Float(Movement, 1);
    AppendSmallPackedGuid(Movement, 0x55);
    Float(Movement, 1); Float(Movement, 2); Float(Movement, 3); Float(Movement, 0);
    AppendU32(Movement, 50); Movement.Add(0); AppendU32(Movement, 51);
    Float(Movement, 0.5f); AppendU32(Movement, 52);
    TArray<uint8> Ack = {1, 0x43}; AppendU32(Ack, 77); Ack.Append(Movement);
    Handler.HandlePacket(0xC7, Ack);
    TestEqual(TEXT("Directed teleport is dispatched"), Teleports, 1);
    TestEqual(TEXT("Movement order counter is retained for ACK"), Counter, 77u);
    TestEqual(TEXT("Destination follows 16-bit movement flags"), Destination, FVector(10, 20, 30));
    TestEqual(TEXT("Transport interpolation and pitch retain fall time alignment"), Entity.Movement.FallTime, 52u);
    TestEqual(TEXT("Teleport preserves movement speed"), Entity.Movement.RunSpeed, 14.0f);
    Ack.Pop(); Handler.HandlePacket(0xC7, Ack);
    TestEqual(TEXT("Truncated teleport is not acknowledged or applied"), Teleports, 1);
    TArray<uint8> Heartbeat = {1, 0x43}; Heartbeat.Append(Movement);
    Handler.HandlePacket(0xEE, Heartbeat);
    TestEqual(TEXT("Heartbeat preserves server speed"), Entity.Movement.RunSpeed, 14.0f);
    TestEqual(TEXT("Heartbeat does not generate teleport acknowledgement"), Teleports, 1);
    return true;
}
