#include "WowNpcHeadLook.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowAnimationController.h"
#include "WowClientPreferences.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"

namespace
{
    constexpr double LookRange = 2500.0; // 25 m
    TWeakObjectPtr<UWowFootIKInstance> GLooking;

    UWowFootIKInstance* FindLookInstance(AActor* Actor)
    {
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Actor);
        for (USkeletalMeshComponent* Mesh : Meshes)
            if (UWowFootIKInstance* Look = Mesh ? Cast<UWowFootIKInstance>(Mesh->GetAnimInstance()) : nullptr)
                return Look;
        return nullptr;
    }

    void Release()
    {
        if (UWowFootIKInstance* Previous = GLooking.Get()) Previous->bLookEnabled = false;
        GLooking.Reset();
    }
}

void FWowNpcHeadLook::Update(AWowGameplayController* Controller, APawn* Player, const FVector& CameraLocation)
{
    if (!Controller || !Player || !Controller->ConnectionManager || Controller->TargetGuid == 0
        || !FWowClientPreferences::GetWorldOption(TEXT("headlook")))
    {
        Release();
        return;
    }
    auto& Entities = Controller->ConnectionManager->PacketHandler.EntityManager;
    const FWowEntity* Entity = Entities.Find(Controller->TargetGuid);
    const TObjectPtr<AActor>* Found = Controller->GetSpawnedEntityActors().Find(Controller->TargetGuid);
    AActor* Npc = Found ? Found->Get() : nullptr;
    // NPCs only (other players turn their own heads), alive, and near enough to meet your eyes.
    if (!Entity || !Entity->IsUnit() || Entity->IsPlayer() || !IsValid(Npc) || Npc == Player
        || static_cast<const FWowUnitEntity*>(Entity)->IsDead()
        || FVector::Dist(Npc->GetActorLocation(), Player->GetActorLocation()) > LookRange)
    {
        Release();
        return;
    }
    UWowFootIKInstance* Look = FindLookInstance(Npc);
    if (!Look) { Release(); return; }
    if (GLooking.Get() != Look) { Release(); GLooking = Look; }

    // Your face: the camera when it sits in your head (first person / VR), else your model's head.
    FVector Face = CameraLocation;
    if (FVector::Dist(CameraLocation, Player->GetActorLocation()) > 120.0)
    {
        Face = Player->GetActorLocation() + FVector(0.0, 0.0, 60.0);
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
        for (const USkeletalMeshComponent* Mesh : Meshes)
            if (Mesh && Mesh->DoesSocketExist(TEXT("WowFirstPersonHead"))) { Face = Mesh->GetSocketLocation(TEXT("WowFirstPersonHead")); break; }
    }
    Look->bLookEnabled = true;
    Look->bLookHeadOnly = false;
    Look->LookAtWorld = Face;
}
