#pragma once
#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;

/**
 * Creates water, lava, and slime materials for liquid rendering.
 * Water: native UE single-layer water with depth transmission and surface impulses.
 * Lava: opaque with emissive glow and UV distortion.
 * Slime: opaque with slight emissive green.
 */
class WOWWORLD_API FWowWaterMaterial
{
public:
	/** Get or create the native water material shared by river and ocean surfaces */
	static UMaterialInterface* GetWaterMaterial();

	/** Surface impulses are world-space cm; time must come from the same world clock. */
	static void AddSurfaceRipple(const FVector& Position, const FVector& Direction, bool bSplash, float WorldTime);
	static void UpdateSurfaceRipples(float WorldTime);
    static void SetUnderwaterView(float Blend);

	/** Get or create the emissive lava material */
	static UMaterial* GetLavaMaterial();

	/** Get or create the emissive slime material */
	static UMaterial* GetSlimeMaterial();

	/** Get material for a given liquid category (0=water, 1=ocean, 2=magma, 3=slime) */
	static UMaterialInterface* GetMaterialForCategory(int32 Category);

	/** Sea: UE water + wind-driven swell, shoreline surge, beach foam and whitecaps (UV1.x = depth in m). */
	static UMaterialInterface* GetOceanMaterial();
	/** Weather link: wind travel direction (UE XY), wind speed m/s, rain 0..1, thunderstorm. */
	static void SetOceanWeather(const FVector2D& WindTravelDirection, float WindSpeed, float Rain, bool bStorm);
	/** CPU copy of the swell height (cm, relative to the flat surface) at a point; 0 when no ocean is active.
	    WorldTime = UWorld::GetTimeSeconds (the material Time node), depth = water depth in metres. */
	static float SampleOceanHeight(const FVector& WorldPosition, float WorldTime, float DepthMeters);
	/** Horizontal part of the same swell (the to-and-fro of a floating body), world XY in cm. */
	static FVector2D SampleOceanDrift(const FVector& WorldPosition, float WorldTime, float DepthMeters);
private:
	static UMaterialInterface* GetRiverSurfaceParent();
	static UMaterialInterface* GetOceanSurfaceParent();
};
