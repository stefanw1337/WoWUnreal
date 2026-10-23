// r331 (Stefan): Experimental "NPC HD models" / "Player HD models". Other characters spawn with the
// classic model (cheap) and are rebuilt with the HD model one at a time, nearest first, so a city
// fills in over time instead of hitching. Photo mode (guild photos) and loading screens (entering a
// dungeon with the group) swap much faster. An upgraded GUID stays HD when it streams back in.
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowEntityManager.h"
#include "WowUpdateFields.h"
#include "WowPhotoMode.h"
#include "WowCharacterBuilder.h"
#include "WowHDCharacter.h"
#include "WowClientPreferences.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

namespace
{
    TSet<uint64> GHdUpgradedGuids;
    TMap<uint32, bool> GHumanoidDisplayCache;
    double GNextHdUpgradeAt = 0.0;
    FVector GLastPawnLocation = FVector::ZeroVector;
    double GPawnStillSince = 0.0;
    double GLastHdHitchAt = 0.0; // r516: last frame slower than 50 ms

    bool IsHumanoidDisplay(uint32 DisplayId)
    {
        if (const bool* Known = GHumanoidDisplayCache.Find(DisplayId)) return *Known;
        FWowCharacterBuilder::FCharacterParams Unused;
        const bool bHumanoid = FWowCharacterBuilder::BuildNpcCharacterParams(DisplayId, Unused);
        GHumanoidDisplayCache.Add(DisplayId, bHumanoid);
        return bHumanoid;
    }
}

FWowCharacterBuilder::FCharacterParams WowBuildPlayerParamsForHd(const FWowPlayerEntity& Player, uint8 NativeGender); // r520, WowGameplayController.cpp

bool WowIsHdUpgraded(uint64 Guid)
{
    return GHdUpgradedGuids.Contains(Guid);
}

void AWowGameplayController::UpdateHdUpgrades()
{
    const bool bNpcs = FWowClientPreferences::GetWorldOption(TEXT("hdnpcs"));
    const bool bPlayers = FWowClientPreferences::GetWorldOption(TEXT("hdplayers"));
    if (!ConnectionManager) return;

    // Rebuild exactly like an appearance change; the spawn path reads WowIsHdUpgraded.
    auto RebuildModel = [this](uint64 Guid)
    {
        if (TObjectPtr<AActor>* Rider = SpawnedEntityRiderActors.Find(Guid); Rider && *Rider) (*Rider)->Destroy();
        SpawnedEntityRiderActors.Remove(Guid);
        SpawnedEntityRiderAnimationControllers.Remove(Guid);
        if (TObjectPtr<AActor>* Actor = SpawnedEntityActors.Find(Guid); Actor && *Actor) (*Actor)->Destroy();
        SpawnedEntityActors.Remove(Guid);
        SpawnedEntityAnimationControllers.Remove(Guid);
        SpawnedEntityAppearanceSignatures.Remove(Guid);
        EntityNameplates.Remove(Guid); WorldLabelAnchors.Remove(Guid);
        QueueEntityModelSpawn(Guid);
    };

    // r387 (Stefan: turning HD off in the game must bring the old models back): characters that
    // were upgraded go back to the classic model when their option is switched off. Classic
    // models are cheap, so a few per frame; the whole area is back within a second or two.
    if (GHdUpgradedGuids.Num() > 0 && (!bNpcs || !bPlayers))
    {
        FWowEntityManager& Known = ConnectionManager->PacketHandler.EntityManager;
        int32 Budget = 4;
        for (auto It = GHdUpgradedGuids.CreateIterator(); It && Budget > 0; ++It)
        {
            const uint64 Guid = *It;
            const FWowEntity* Entity = Known.Find(Guid);
            if (Entity && (Entity->IsPlayer() ? bPlayers : bNpcs)) continue;
            It.RemoveCurrent();
            if (!Entity || !SpawnedEntityActors.Contains(Guid)) continue; // not in view: comes back classic
            RebuildModel(Guid);
            --Budget;
        }
    }
    if (!bNpcs && !bPlayers) return;
    const APawn* PlayerPawn = GetPawn();
    if (!PlayerPawn) return;

    const double Now = FPlatformTime::Seconds();
    // r516 (Stefan: with NPC + player HD on, 11-12 fps for a second every 4 s): each HD build blocks the
    // game thread for 150-800 ms (median 220 ms in his log), and r508 started one every 1.5 s. Now a swap
    // only happens after 4 s without any frame over 50 ms, at most one per 10 s, while you have stood
    // still for 3 s, within 25 m. The hitch of a swap itself resets the 4 s, so they never chain.
    if (FApp::GetDeltaTime() > 0.05) GLastHdHitchAt = Now;
    const FVector PawnLocation = PlayerPawn->GetActorLocation();
    if (FVector::DistSquared(PawnLocation, GLastPawnLocation) > FMath::Square(20.0)) GPawnStillSince = Now;
    GLastPawnLocation = PawnLocation;

    const bool bFast = (PhotoMode.IsValid() && PhotoMode->IsActive()) || OfflineTransferScreen.IsValid() || bMapTerrainPending;
    if (Now < GNextHdUpgradeAt) return;
    // Each HD build costs a few hundred ms on the game thread: in normal play only while you stand
    // still and nothing else is streaming in.
    // r501 (Stefan: NPC HD models "doesn't seem to change", an orc right in front of him stayed classic):
    // in a busy starting area mobs keep streaming in, so "at most 2 spawns waiting" was rarely true and
    // one swap per 3 s took minutes to reach anyone. Now: standing still 1 s, up to 6 spawns waiting,
    // one swap per 1.5 s, nearest first.
    // r508 (Stefan, HD options all on in Durotar: Kaltunk, Frang and other players all stayed classic;
    // the log never showed one "HD upgrade"): the waiting-spawn queue also holds every entity outside
    // the model streaming range (15-116 in his log), which never leaves the queue while you stand
    // there, so "at most 6 waiting" was never true. Only spawns that can actually be built now count.
    FWowEntityManager& Entities = ConnectionManager->PacketHandler.EntityManager;
    int32 BuildableSpawns = 0;
    for (const uint64 PendingGuid : PendingEntityModelSpawns)
        if (const FWowEntity* Pending = Entities.Find(PendingGuid); Pending && IsEntityModelInStreamingRange(*Pending))
            ++BuildableSpawns;
    // r519: NPC textures are composed in the background first, so their swap is cheap: 2 s without
    // a slow frame and 1 s standing still are enough for them; r520: players the same way.
    if (!bFast && (Now - GPawnStillSince < 1.0 || Now - GLastHdHitchAt < 2.0 || BuildableSpawns > 3))
    {
        static double GNextHdWaitLog = 0.0;
        if (Now >= GNextHdWaitLog)
        {
            GNextHdWaitLog = Now + 20.0;
            UE_LOG(LogTemp, Log, TEXT("HD upgrade waiting: still %.1f s, buildable spawns %d (queue %d), %d upgraded"),
                Now - GPawnStillSince, BuildableSpawns, PendingEntityModelSpawns.Num(), GHdUpgradedGuids.Num());
        }
        GNextHdUpgradeAt = Now + 0.5;
        return;
    }

    const uint64 LocalGuid = Entities.LocalPlayerGuid;
    const double MaxDistSq = FMath::Square(bFast ? 6000.0 : 2500.0);
    uint64 Best = 0;
    double BestDistSq = MaxDistSq;
    for (const TPair<uint64, TObjectPtr<AActor>>& Pair : SpawnedEntityActors)
    {
        if (Pair.Key == LocalGuid || !Pair.Value || GHdUpgradedGuids.Contains(Pair.Key)) continue;
        // r519 (Stefan: "don't swap HD models for the ones you're looking at"): only characters that
        // are not on screen change model; you see them in HD when you turn around. Loading screens and
        // photo mode (bFast) swap everyone.
        if (!bFast && Pair.Value->WasRecentlyRendered(0.25f)) continue;
        const FWowEntity* Entity = Entities.Find(Pair.Key);
        if (!Entity || !Entity->IsUnit()) continue;
        if (Entity->IsPlayer())
        {
            if (!bPlayers) continue;
        }
        else
        {
            if (!bNpcs || (Entity->GetField(UnitField::FLAGS_2) & 0x10u)) continue; // mirror images are dressed separately
            const uint32 DisplayId = Entity->GetDisplayId() ? Entity->GetDisplayId() : Entity->GetNativeDisplayId();
            if (DisplayId == 0 || !IsHumanoidDisplay(DisplayId)) continue;
        }
        const double DistSq = FVector::DistSquared(Pair.Value->GetActorLocation(), PawnLocation);
        if (DistSq < BestDistSq) { BestDistSq = DistSq; Best = Pair.Key; }
    }
    if (Best == 0)
    {
        GNextHdUpgradeAt = Now + 1.0;
        return;
    }

    // r519: an NPC's HD textures are composed on a worker thread first (BLP decode, layers, gear,
    // mips - most of the 150-800 ms a swap used to freeze the game for); the swap waits until they are
    // ready, then only the texture objects and the model are made here.
    // r520: players too (their params come from the update fields, like the spawn path).
    if (const FWowEntity* Entity = Entities.Find(Best); Entity && CachedMpq)
    {
        FWowCharacterBuilder::FCharacterParams Params;
        bool bHaveParams = false;
        if (Entity->IsPlayer())
        {
            if (const uint8* Gender = ConnectionManager->PacketHandler.PlayerGenderCache.Find(Best); Gender && *Gender <= 1)
            {
                Params = WowBuildPlayerParamsForHd(static_cast<const FWowPlayerEntity&>(*Entity), *Gender);
                bHaveParams = true;
            }
        }
        else
        {
            const uint32 DisplayId = Entity->GetDisplayId() ? Entity->GetDisplayId() : Entity->GetNativeDisplayId();
            bHaveParams = FWowCharacterBuilder::BuildNpcCharacterParams(DisplayId, Params);
        }
        if (bHaveParams
            && !FWowHDCharacter::PrefetchTextures(CachedMpq, FWowCharacterBuilder::GetCharacterModelPath(Params.Race, Params.Gender),
                Params.Customization, Params.BodyEquipment))
        {
            GNextHdUpgradeAt = Now + 0.25; // composing; check again shortly
            return;
        }
    }
    GHdUpgradedGuids.Add(Best);
    RebuildModel(Best);
    UE_LOG(LogTemp, Log, TEXT("HD upgrade: %llu at %.0f m (%d upgraded)"), Best, FMath::Sqrt(BestDistSq) / 100.0, GHdUpgradedGuids.Num());
    GNextHdUpgradeAt = Now + (bFast ? 0.15 : 3.0);
}
