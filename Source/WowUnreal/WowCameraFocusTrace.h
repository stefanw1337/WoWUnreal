#pragma once
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"

namespace WowCameraFocus
{
inline bool ShouldIgnoreComponent(const UPrimitiveComponent* Component)
{
    if (!Component) return false;
    if (Component->ComponentHasTag(TEXT("WowNoCameraFocus")) ||
        Component->ComponentHasTag(TEXT("WowTargetProxy"))) return true;
    const AActor* Owner = Component->GetOwner();
    if (Owner && (Owner->IsHidden() || Owner->ActorHasTag(TEXT("WowSpellVisual")))) return true;
    // Translucent weather, particles, ribbons and spell shells are not solid
    // autofocus subjects, including effects that accidentally acquired collision.
    const int32 Count = Component->GetNumMaterials();
    for (int32 I = 0; I < Count; ++I)
    {
        const UMaterialInterface* Material = Component->GetMaterial(I);
        if (!Material || (Material->GetBlendMode() != BLEND_Translucent &&
            Material->GetBlendMode() != BLEND_Additive && Material->GetBlendMode() != BLEND_Modulate)) return false;
    }
    return Count > 0;
}
inline bool Trace(UWorld* World, FHitResult& Hit, const FVector& Start,
    const FVector& End, ECollisionChannel Channel, FCollisionQueryParams Query)
{
    for (int32 I = 0; I < 32; ++I)
    {
        if (!World->LineTraceSingleByChannel(Hit, Start, End, Channel, Query)) return false;
        // Picking volumes can extend far beyond a corpse's rendered body.
        // Never focus on a proxy, or on a surface containing the camera.
        if (!Hit.bStartPenetrating && Hit.Distance > KINDA_SMALL_NUMBER &&
            !ShouldIgnoreComponent(Hit.GetComponent())) return true;
        Query.AddIgnoredComponent(Hit.GetComponent());
    }
    return false;
}
inline float SceneDistance(bool bHit, float HitDistance, float Previous)
{
    // A clear sightline must release a previous near subject. The camera smooths the transition.
    return bHit ? FMath::Max(10.0f, HitDistance) : 50000.0f;
}
}
