#pragma once
#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

/**
 * VR world cards: UI drawn on a world-space widget card that faces the head, so both eyes
 * see it at its real depth (screen-projected Slate overlays render into one eye only and
 * are not shown in the headset at all). Used for nameplates, quest diamonds, chat bubbles,
 * combat text and the XP gain text while VR is on.
 */
namespace WowVrCards
{
/** World scale of a card at this distance: about 0.1 cm per pixel up close, growing with
 *  distance so the text keeps a readable angular size (capped at 1 cm per pixel). */
inline float DistanceScale(float DistanceCm)
{
    return FMath::Clamp(.1f + DistanceCm * .00022f, .12f, 1.f);
}

/** Turn the card to the eye and size it. The card's front is its local +X. */
inline void Face(UWidgetComponent* Card, const FVector& Eye, float Scale)
{
    if (!Card) return;
    Card->SetWorldScale3D(FVector(Scale));
    Card->SetWorldRotation((Eye - Card->GetComponentLocation()).Rotation());
}

/**
 * Re-registering the owner's components (the player character re-registers them when its model
 * is rebuilt) runs UWidgetComponent::OnRegister again, which builds a new dynamic material from
 * slot 0 - by then our own dynamic material - and fails ("... is not a valid parent"): the card then
 * draws the default checkerboard (Stefan's black square pointer, nameplates). Rebuild it from the
 * widget base material when that happened.
 */
inline void RepairMaterial(UWidgetComponent* Card)
{
    if (!Card) return;
    const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Card->GetMaterial(0));
    if (!Mid || (Mid->Parent && !Mid->Parent->IsA<UMaterialInstanceDynamic>())) return;
    const EWidgetBlendMode Mode = Card->GetBlendMode();
    // SetBlendMode rebuilds the material from slot 0: clear the override first each time so it
    // starts from the base widget material, and switch away and back to force the rebuild.
    Card->SetMaterial(0, nullptr);
    Card->SetBlendMode(Mode == EWidgetBlendMode::Masked ? EWidgetBlendMode::Transparent : EWidgetBlendMode::Masked);
    Card->SetMaterial(0, nullptr);
    Card->SetBlendMode(Mode);
}

/** A new world-space card on the actor (absolute transform). */
inline UWidgetComponent* Make(AActor* Owner, const TCHAR* Name, const FVector2D& DrawSize, const FVector2D& Pivot, bool bDesiredSize)
{
    if (!Owner) return nullptr;
    UWidgetComponent* Card = NewObject<UWidgetComponent>(Owner, MakeUniqueObjectName(Owner, UWidgetComponent::StaticClass(), FName(Name)));
    if (!Card) return nullptr;
    if (USceneComponent* Root = Owner->GetRootComponent()) Card->SetupAttachment(Root);
    Card->SetUsingAbsoluteLocation(true);
    Card->SetUsingAbsoluteRotation(true);
    Card->SetUsingAbsoluteScale(true);
    Card->SetWidgetSpace(EWidgetSpace::World);
    // Default masked one-sided widget material (it faces the eye every frame); changing blend
    // mode or sidedness rebuilt the material instance from itself and the card drew nothing.
    Card->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Card->SetCastShadow(false);
    Card->SetDrawSize(DrawSize);
    Card->SetDrawAtDesiredSize(bDesiredSize);
    Card->SetPivot(Pivot);
    Card->ComponentTags.Add(TEXT("WowNoCameraFocus"));
    Card->RegisterComponent();
    return Card;
}
}
