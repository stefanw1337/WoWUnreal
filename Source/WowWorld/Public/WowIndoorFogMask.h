#pragma once
#include "CoreMinimal.h"

class AActor;
class UStaticMeshComponent;
class UMaterialInterface;
class UExponentialHeightFogComponent;

/**
 * r412 (Stefan, r20t223: the weather fog filled rooms seen through a door). The weather fog is one
 * height fog for the whole world, so a room seen from outside got the fog of its full distance. A
 * post-process pass (Scene Color Before DOF) takes back the part of the fog that lies
 * inside the indoor groups of nearby buildings (WMO groups with the indoor flag 0x2000): for a pixel
 * in such a room, or with the camera in one, the fog is recomputed for the outdoor stretch of the
 * view ray only (the same exponential height fog formula UE uses, with our fog's own values).
 * The rooms are the groups' boxes (oriented with the building's yaw), at most 16 at a time,
 * nearest first. Only while weather fog is on and volumetric fog is off (heavy weather): the
 * volumetric fog grid is not this analytic fog.
 */
class WOWWORLD_API FWowIndoorFogMask
{
public:
    /** WMO loading: an indoor group mesh (its local bounds become a room box). */
    static void RegisterIndoorGroup(UStaticMeshComponent* Group);
    /** Cooked post-process material M_WowIndoorFogMask_v1 (made in the editor / standalone prepare). */
    static UMaterialInterface* PrepareMaterial();
    /**
     * After the sky manager set the height fog: places the pass on Owner and feeds it the fog's
     * values and the rooms nearest to Camera. bWeatherFog: weather fog is part of the fog now.
     */
    static void Update(AActor* Owner, const UExponentialHeightFogComponent* Fog, const FVector& Camera, bool bWeatherFog);
    /** r506: Point lies inside the box of a loaded indoor WMO group (MOGP flag 0x2000), within Margin cm. */
    static bool IsInsideIndoorGroup(const FVector& Point, double Margin = 50.0);
    /**
     * r655 (Stefan in the Burning Blade cave: "we need a haze in all caves and mines, this looks too clean"): the
     * group's own fog from the WMO (MFOG via the group's fog id): sRGB colour, linear from StartCm to EndCm as
     * in the original client. Drawn while the view point (the camera; in the isometric camera the character)
     * is inside that group.
     */
    static void SetGroupFog(UStaticMeshComponent* Group, const FColor& Color, float StartCm, float EndCm);
};
