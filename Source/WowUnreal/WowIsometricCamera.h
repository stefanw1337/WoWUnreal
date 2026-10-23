#pragma once
#include "CoreMinimal.h"

class APawn;
class UMaterialInterface;
struct FMinimalViewInfo;

/**
 * r610 (Stefan: "isometric camera for WoW, more Diablo-style ... locking the camera up high, bird's
 * view, middle mouse turns the camera; roofs and cave tops are the biggest problem"): an
 * experimental camera mode, world option "isometriccamera" (Options > Experimental > Camera & Lens).
 *
 *  - The view looks down at a fixed 55 degrees from far away through a narrow lens (30 degrees),
 *    which reads as isometric while fog, depth of field and translucency keep working.
 *  - Middle mouse drag turns the camera around the player; the mouse wheel zooms (12-45 m).
 *  - W/A/S/D (and the turn keys) move relative to the screen; the character turns to face the
 *    way it walks, so the server sees normal forward movement.
 *  - Cut-away: when something is overhead (a roof, a cave ceiling, an upper floor), every
 *    building/cave/prop surface above head height within ~18 m of the player is cut away, so the
 *    room you stand in is seen from above. Terrain and instanced foliage are not cut (yet).
 *    Cut surfaces are drawn with a simplified material while they are cut.
 */
struct FWowIsometricCamera
{
    static bool IsActive();
    /** Camera placement for this frame (also runs the middle-mouse turn and the cut-away). */
    static void ApplyView(APawn* Pawn, FMinimalViewInfo& View, float DeltaTime);
    /** Screen-relative move direction for WASD input (X = right, Y = up the screen). */
    static FVector ScreenDirection(const FVector2D& Input);
    static void Zoom(float Steps);
    /** Restores every cut surface (mode switched off, pawn gone). */
    static void ClearCutaway();
    /** r618: cut-away surfaces do not block clicks and hover (they are not drawn there). */
    static void IgnoreCutSurfaces(struct FCollisionQueryParams& Query);
    /** r619: true when this world point is inside the cut-away (not drawn): clicks pass through it. */
    static bool IsCutAway(const FVector& Point);
    /** Cooks M_WowIsoCutaway_v1 (WowPrepareStandaloneCommandlet). */
    static void PrepareStandaloneMaterials();
    static UMaterialInterface* GetCutawayMaterial();
};
