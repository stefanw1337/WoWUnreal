// Visual ground conform for NPCs. The server's Z comes from its own map extraction; where our
// terrain differs slightly, creatures float above a dip or sink into a slope. Only the body
// mesh is moved (never the actor, whose position stays authoritative), within a small window,
// and never for flying, swimming, hovering or falling units.
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace
{
constexpr uint32 NoGroundFlags = 0x00000200u /*DISABLE_GRAVITY*/ | 0x00001000u /*FALLING*/ | 0x00002000u /*FALLING_FAR*/ | 0x00200000u /*SWIMMING*/
    | 0x01000000u /*CAN_FLY*/ | 0x02000000u /*FLYING*/ | 0x40000000u /*HOVER*/;
constexpr float MaxDrop = 150.0f;   // cm the body may be lowered onto lower ground
constexpr float MaxLift = 40.0f;    // cm it may be raised out of a slope
constexpr int32 ProbesPerFrame = 24;
}

void AWowGameplayController::ConformEntitiesToGround(float DeltaTime)
{
    if (!ConnectionManager || !GetWorld()) return;
    auto& Entities = ConnectionManager->PacketHandler.EntityManager;
    const double Now = FPlatformTime::Seconds();
    int32 Budget = ProbesPerFrame;
    FCollisionObjectQueryParams Static(ECC_WorldStatic);

    for (auto It = GroundConform.CreateIterator(); It; ++It)
        if (!SpawnedEntityActors.Contains(It.Key())) It.RemoveCurrent();

    for (const auto& Pair : SpawnedEntityActors)
    {
        AActor* Actor = Pair.Value;
        if (!Actor || !IsValid(Actor) || Pair.Key == Entities.LocalPlayerGuid) continue;
        const FWowEntity* Entity = Entities.Find(Pair.Key);
        if (!Entity || !Entity->IsUnit()) continue;
        // Other players too (r273, Stefan: a bot paladin stood in the air above a floor). Bots
        // walk server paths whose Z comes from the navmesh, which can sit well above stairs and
        // floors. Not on transports, and only when every other skeletal mesh (hair, mount,
        // equipment) hangs under the body, so moving the body cannot split the model.
        if (Entity->IsPlayer() && Entity->Movement.TransportGuid != 0) continue;

        FGroundConform& G = GroundConform.FindOrAdd(Pair.Key);
        USceneComponent* Mesh = G.Mesh.Get();
        if (!Mesh)
        {
            USkeletalMeshComponent* Body = Actor->FindComponentByClass<USkeletalMeshComponent>();
            if (!Body) continue;
            if (Entity->IsPlayer())
            {
                TArray<USkeletalMeshComponent*> Skins;
                Actor->GetComponents<USkeletalMeshComponent>(Skins);
                bool bAllUnderBody = true;
                for (USkeletalMeshComponent* Other : Skins)
                    if (Other && Other != Body && !Other->IsAttachedTo(Body)) { bAllUnderBody = false; break; }
                if (!bAllUnderBody) continue;
            }
            Mesh = Body;
            G.Mesh = Mesh; G.BaseZ = static_cast<float>(Mesh->GetRelativeLocation().Z); G.bHasBase = true; G.Offset = G.Target = 0;
        }
        const bool bGrounded = (Entity->Movement.MoveFlags & NoGroundFlags) == 0 && !DeadEntityGuids.Contains(Pair.Key);
        // Moving units follow server paths that cut straight through the air over stairs and
        // kerbs (r284, Stefan: an NPC walked in the air, then dropped): probe often and settle
        // fast while they move, slowly while they stand.
        const bool bMoving = Entity->Movement.bHasActiveSpline || VisuallyMovingEntityGuids.Contains(Pair.Key);
        if (!bGrounded) G.Target = 0;
        else if (Now >= G.NextProbe && Budget > 0)
        {
            --Budget;
            G.NextProbe = Now + (bMoving ? 0.06 + FMath::FRand() * 0.04 : 0.3 + FMath::FRand() * 0.15);
            const FVector Feet = Actor->GetActorLocation();
            FHitResult Hit;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(WowNpcGroundConform), false, Actor);
            if (GetWorld()->LineTraceSingleByObjectType(Hit, Feet + FVector(0, 0, MaxLift + 60.0f), Feet - FVector(0, 0, MaxDrop + 30.0f), Static, Params)
                && Hit.ImpactNormal.Z > 0.5f)
            {
                const float Gap = static_cast<float>(Hit.ImpactPoint.Z - Feet.Z);
                G.Target = (Gap < -MaxDrop || Gap > MaxLift) ? 0.0f : Gap;
            }
            else G.Target = 0;
        }
        G.Offset = FMath::FInterpTo(G.Offset, G.Target, DeltaTime, bMoving ? 16.0f : 6.0f);
        FVector Relative = Mesh->GetRelativeLocation();
        // Relative offsets are scaled by the actor scale (creature display scale): convert from world cm.
        const float Scale = FMath::Max(0.05f, static_cast<float>(Actor->GetActorScale3D().Z));
        const float Desired = G.BaseZ + (FMath::Abs(G.Offset) < 0.3f ? 0.0f : G.Offset / Scale);
        if (!FMath::IsNearlyEqual(static_cast<float>(Relative.Z), Desired, 0.05f))
        {
            Relative.Z = Desired;
            Mesh->SetRelativeLocation(Relative);
        }
    }
}
