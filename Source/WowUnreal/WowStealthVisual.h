#pragma once
#include "CoreMinimal.h"

class AWowGameplayController;
class UMaterialInterface;

/**
 * r605 (Stefan: "Stealth also didn't work. We have the buff, but our character needs to be in the
 * shadows"): how a stealthed unit is drawn. Applies to the local player and to every stealthed unit
 * the server lets us see (party members, a rogue that has been detected); who can see whom is the
 * server's decision, as in the original client.
 *
 * Looks (Options > Experimental, rogues; world options, the first set one wins):
 *  - "stealthpredator": almost invisible, the world bends behind you, thin cool outline.
 *  - "stealthshadowonly": the body disappears, only its shadow on the ground is left.
 *  - "stealthclassic": the original client's see-through dark body.
 *  - none set (default) "shadow cloak": see-through and darkened, plus a soft shimmer and a faint rim.
 * Entering and leaving stealth plays a sound for the local player.
 * "stealthdetectrange" (Options > Experimental, any class that can stealth): while you are stealthed,
 * each nearby enemy shows a ground cone of how close it can come before it notices you (AzerothCore's
 * detection rule; an estimate, the server decides). Blue = safe, amber = it may turn to look, red = seen.
 */
struct FWowStealthVisual
{
    static void Tick(AWowGameplayController* Controller, float DeltaTime);
    /** Cooks M_WowStealthOverlay (WowPrepareStandaloneCommandlet). */
    static void PrepareStandaloneMaterials();
    static UMaterialInterface* GetOverlayMaterial();
};
