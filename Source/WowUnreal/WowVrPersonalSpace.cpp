#include "WowVrPersonalSpace.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowClientPreferences.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowVrSpace, Log, All);

namespace
{
    // Free space kept between the player and the front of a unit's body (cm), and how far a body
    // may be slid back at most.
    constexpr float PersonalSpace = 130.f;
    constexpr float MaxPush = 160.f;
    constexpr float CheckRange = 700.f;

    struct FPush
    {
        TWeakObjectPtr<USceneComponent> Mesh;
        FVector Applied = FVector::ZeroVector;   // our share of the mesh's relative location
        FVector LastSet = FVector::ZeroVector;   // the relative location we last wrote
        FVector Direction = FVector::ForwardVector;
        float Amount = 0.f;
        bool bSeen = false;
    };
    TMap<uint64, FPush> GPushes;
    int32 GLoggedPushes = 0;

    void Apply(FPush& Push, USceneComponent* Mesh)
    {
        if (!Mesh) return;
        if (Push.Mesh.Get() != Mesh)
        {
            Push.Mesh = Mesh;
            Push.Applied = FVector::ZeroVector;
            Push.LastSet = Mesh->GetRelativeLocation();
        }
        // Another system (ground tilt, swimming) may have written the relative location since our
        // last frame; then it holds no offset of ours any more.
        FVector Base = Mesh->GetRelativeLocation();
        if (Base.Equals(Push.LastSet, 0.01)) Base -= Push.Applied;
        const FVector World = Push.Direction * Push.Amount;
        const USceneComponent* Parent = Mesh->GetAttachParent();
        const FVector Relative = Parent ? Parent->GetComponentTransform().InverseTransformVector(World) : World;
        const FVector Wanted = Base + Relative;
        if (!Wanted.Equals(Mesh->GetRelativeLocation(), 0.01)) Mesh->SetRelativeLocation(Wanted);
        Push.Applied = Relative;
        Push.LastSet = Wanted;
    }
}

void FWowVrPersonalSpace::Update(AWowGameplayController* Controller, APawn* Player, float DeltaTime, bool bActive)
{
    const bool bOn = bActive && Controller && Player && Controller->ConnectionManager
        && FWowClientPreferences::GetWorldOption(TEXT("vrfirstperson"));
    if (!bOn && GPushes.IsEmpty()) return;
    for (auto& Pair : GPushes) Pair.Value.bSeen = false;

    if (bOn)
    {
        auto& Entities = Controller->ConnectionManager->PacketHandler.EntityManager;
        const uint64 Self = Entities.LocalPlayerGuid;
        const FVector Eye = Player->GetActorLocation();
        for (const auto& Pair : Controller->GetSpawnedEntityActors())
        {
            AActor* Actor = Pair.Value.Get();
            if (Pair.Key == Self || !IsValid(Actor) || Actor == Player) continue;
            const FWowEntity* Entity = Entities.Find(Pair.Key);
            if (!Entity || !Entity->IsUnit()) continue;
            FVector To = Actor->GetActorLocation() - Eye;
            To.Z = 0.0;
            const float Distance = float(To.Size());
            FPush* Existing = GPushes.Find(Pair.Key);
            if (Distance > CheckRange && !Existing) continue;
            USkeletalMeshComponent* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>();
            if (!Mesh) continue;
            // Front of the body: half the mesh's horizontal size (big mobs reach further).
            const FVector Extent = Mesh->Bounds.BoxExtent;
            const float Radius = FMath::Clamp(float(FMath::Max(Extent.X, Extent.Y)) * 0.6f, 20.f, 250.f);
            const float Want = Distance > CheckRange ? 0.f : FMath::Clamp(PersonalSpace - (Distance - Radius), 0.f, MaxPush);
            FPush& Push = Existing ? *Existing : GPushes.Add(Pair.Key);
            Push.bSeen = true;
            if (Distance > 1.f) Push.Direction = To / Distance;
            Push.Amount = FMath::FInterpTo(Push.Amount, Want, DeltaTime, 6.f);
            if (Push.Amount < 0.5f && Want <= 0.f) Push.Amount = 0.f;
            Apply(Push, Mesh);
            if (Want > 30.f && GLoggedPushes < 20)
            {
                ++GLoggedPushes;
                UE_LOG(LogWowVrSpace, Log, TEXT("VR personal space: %s at %.0f cm (body radius %.0f) drawn %.0f cm further back"),
                    *Actor->GetName(), Distance, Radius, Want);
            }
        }
    }

    // Units that left (or VR off): put their bodies back where the server has them.
    for (auto It = GPushes.CreateIterator(); It; ++It)
    {
        FPush& Push = It.Value();
        if (Push.bSeen) { if (Push.Amount <= 0.f) It.RemoveCurrent(); continue; }
        if (USceneComponent* Mesh = Push.Mesh.Get())
        {
            Push.Amount = 0.f;
            Apply(Push, Mesh);
        }
        It.RemoveCurrent();
    }
}
