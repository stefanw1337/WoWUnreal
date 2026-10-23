#pragma once
#include "CoreMinimal.h"
#include "WowCharacterTexture.h"

class FMpqManager;
class FWowAssetCache;
class UTexture2D;

/**
 * HD player models (retail-format character M2 + skin, converted to the 3.3.5 layout,
 * shipped as patch-H.MPQ with loose HD_Data textures beside it).
 *
 * Everything is read through FMpqManager's HDX\ prefix, so stock models are never
 * replaced and the classic path keeps working when the option is off or data is missing.
 * The look comes from <Race><Gender>.customization.json (ChrCustomization export):
 * the five classic customization bytes pick choices of Skin Color, Face, Hair Style,
 * Hair Color and the race's facial option; the chosen elements give geosets and the
 * texture layers that are composited per texture type (1 body, 6 hair, 19 eyes ...).
 */
class WOWASSETS_API FWowHDCharacter
{
public:
    struct FAppearance
    {
        FString ModelPath;                       // HDX\Character\Orc\Male\OrcMale.m2
        TMap<uint16, uint16> Geosets;            // group -> variant (0 = hidden)
        TMap<uint32, UTexture2D*> Textures;      // M2 texture type -> composited texture
    };

    /** True when HD data exists for this classic model path (Character\Race\Sex\RaceSex.m2). */
    static bool IsAvailable(FMpqManager* Mpq, const FString& ClassicModelPath);

    /**
     * Resolve geosets and build textures (equipment overlays included) for an HD model.
     * ClassicEquipmentGeosets is FWowCharacterBuilder::ComputeGeosetWithEquipment's result
     * and ClassicDefaultGeosets its ComputeDefaultGeosets result: groups the gear changes
     * are carried over when the HD mesh has that variant.
     */
    static bool Build(FMpqManager* Mpq, FWowAssetCache* Cache, const FString& ClassicModelPath,
        const FWowCharacterTexture::FCustomization& Customization,
        const FWowCharacterTexture::FBodyEquipment& Equipment,
        const TMap<uint16, uint16>& ClassicDefaultGeosets,
        const TMap<uint16, uint16>& ClassicEquipmentGeosets,
        FAppearance& Out);

    /**
     * r519 (Stefan: "build it in the background"): start composing this appearance's HD textures on a
     * worker thread (BLP decode, layer compositing, gear overlays, mip chain). Returns true once Build
     * can run without that work (textures cached or composed and waiting), false while it is still
     * being composed. Unavailable or failed appearances return true (Build falls back by itself).
     */
    static bool PrefetchTextures(FMpqManager* Mpq, const FString& ClassicModelPath,
        const FWowCharacterTexture::FCustomization& Customization,
        const FWowCharacterTexture::FBodyEquipment& Equipment);

    static void ClearCaches();
};
