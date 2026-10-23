#pragma once
#include "CoreMinimal.h"

class FMpqManager;
class UWorld;
class FWowAssetCache;
struct FAdtChunkData;
struct FAdtData;

/**
 * Creates terrain materials with multi-layer texture splatting.
 * Each chunk gets a UMaterialInstanceDynamic with up to 4 ground textures
 * and 3 alpha blend maps.
 */
class WOWWORLD_API FWowTerrainMaterial
{
public:
    static void PrepareRainMaterials();
    static UMaterial* GetLampGlassMaterial();
    static UMaterial* GetOpaqueWindowLightMaterial();
    static void SetRainWetness(UWorld* World, float Amount);
    /**
     * Create a material instance for a terrain chunk.
     * Loads BLP textures from MPQ, creates alpha map textures,
     * and sets them as material parameters.
     */
    static UMaterialInstanceDynamic* CreateChunkMaterial(
        const FAdtChunkData& ChunkData,
        const FAdtData& AdtData,
        FMpqManager* Mpq,
        FWowAssetCache* Cache,
        UObject* Outer);

    /** Get or create the base terrain material (4-layer splat, uses UV0=alpha, UV1=texture) */
    static UMaterial* GetBaseMaterial();

    /** Get or create a simple single-texture material for WMOs/doodads (uses UV0 for texture) */
    static UMaterial* GetSimpleObjectMaterial();
    /** r306: GetSimpleObjectMaterial without two-sided (M2 passes lacking render flag 0x04). */
    static UMaterial* GetSimpleObjectMaterialSingleSided();

    /** M2 "alpha key" (blend mode 1) passes that are not foliage: cut at WoW's alpha
     *  reference (~0.88) instead of 0.33, so soft-edged base skirts and bark cards keep
     *  their authored outline instead of a light fringe ring. */
    static UMaterial* GetSimpleAlphaKeyObjectMaterial();

    /** Fully opaque object material for solid WMO walls, roofs, floors and bridges. */
    static UMaterial* GetSimpleOpaqueObjectMaterial(bool bTwoSided = true);

    /** Get or create an unlit masked material for foliage/canopy M2 cards */
    static UMaterial* GetSimpleFoliageObjectMaterial();

    /** Get or create a translucent single-texture material for alpha-blended M2 doodad passes */
    static UMaterial* GetSimpleTranslucentObjectMaterial();

    /** Get or create an additive single-texture material for glow/waterfall M2 doodad passes */
    static UMaterial* GetSimpleAdditiveObjectMaterial();

    /** Get or create a translucent scrolling material for M2 waterfall sheet doodads */
    static UMaterial* GetSimpleWaterfallObjectMaterial();

    /** Load a BLP texture from MPQ, with caching */
    static UTexture2D* LoadBlpTexture(const FString& Path, FMpqManager* Mpq, FWowAssetCache* Cache);

private:
    /** Create a UTexture2D from alpha map data (64x64 grayscale) */
    static UTexture2D* CreateAlphaTexture(const TArray<uint8>& AlphaData, const FString& Name);
};
