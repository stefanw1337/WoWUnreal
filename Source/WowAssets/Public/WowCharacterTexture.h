#pragma once
#include "CoreMinimal.h"

class UTexture2D;
class FMpqManager;
class FWowAssetCache;

/**
 * Composites character textures from CharSections.dbc layers.
 * Bakes skin + face + facial hair + underwear into a single body texture.
 */
class WOWASSETS_API FWowCharacterTexture
{
public:
    /** Section types from CharSections.dbc */
    enum class ESectionType : uint32
    {
        Skin = 0,
        Face = 1,
        FacialHair = 2,
        Hair = 3,
        Underwear = 4
    };

    /** Character texture region types for layout positioning */
    enum class ECharRegion : uint32
    {
        ARM_UPPER = 0,
        ARM_LOWER = 1,
        HAND = 2,
        TORSO_UPPER = 3,
        TORSO_LOWER = 4,
        LEG_UPPER = 5,
        LEG_LOWER = 6,
        FOOT = 7,
        ACCESSORY = 8,
        FACE_UPPER = 9,
        FACE_LOWER = 10
    };

    struct FCustomization
    {
        uint32 RaceId = 1;
        uint32 Gender = 0; // 0=male, 1=female
        uint32 SkinColor = 0;
        uint32 FaceVariation = 0;
        uint32 HairStyle = 0;
        uint32 HairColor = 0;
        uint32 FacialHairStyle = 0;
    };

    /**
     * Build a composite character texture by layering CharSections textures.
     * Returns the base skin texture if compositing fails.
     */
    static UTexture2D* BuildCompositeTexture(FMpqManager* Mpq, FWowAssetCache* Cache,
        const FCustomization& Customization);

    /** r311: the same atlas as RGBA pixels without creating a texture. Safe on a worker thread
     *  (MPQ reads are locked, DBC data is read-only). False when the skin cannot be decoded. */
    static bool BuildCompositePixels(FMpqManager* Mpq, const FCustomization& Customization,
        TArray<uint8>& OutPixels, uint32& OutWidth, uint32& OutHeight);
    /** Asset-cache key of BuildCompositeTexture's result. */
    static FString MakeCompositeCacheKey(const FCustomization& Customization);
    /** r311: worker jobs that read the MPQs. The world manager waits for them before it closes
     *  the data (EndPlay). */
    static void BeginAsyncWork();
    static void EndAsyncWork();
    static void WaitForAsyncWork();

    /** Look up a CharSections texture path for the given parameters */
    static FString GetSectionTexture(uint32 RaceId, uint32 Gender, ESectionType Type,
        int32 Variation = INDEX_NONE, int32 Color = INDEX_NONE);

    /** Body equipment IDs that affect texture overlays */
    struct FBodyEquipment
    {
        uint32 RaceId = 1;  // Race ID for texture path suffix
        uint32 Gender = 0;  // 0=male, 1=female
        uint32 ChestDisplayId = 0;
        uint32 PantsDisplayId = 0;
        uint32 BootsDisplayId = 0;
        uint32 GlovesDisplayId = 0;
        uint32 BracersDisplayId = 0;
        uint32 ShirtDisplayId = 0;
        uint32 TabardDisplayId = 0;
        uint32 BeltDisplayId = 0;
        uint32 CapeDisplayId = 0;
        bool bIsRobeEquipped = false;  // True if chest item is a robe (InventoryType 20)
        /** r343: guild tabard design, packed by MakeGuildTabard (0 = none / unknown). */
        uint32 GuildTabard = 0;
    };

    /** r343: pack SMSG_GUILD_QUERY_RESPONSE emblem fields; bit 31 marks a valid design. */
    static uint32 MakeGuildTabard(uint32 EmblemStyle, uint32 EmblemColor, uint32 BorderStyle, uint32 BorderColor, uint32 BackgroundColor)
    {
        // r551: background needs 0-50 (7 bits); same layout as FWowPacketHandler::HandleGuildName.
        return 0x80000000u | ((EmblemStyle & 0xffu) << 21) | ((EmblemColor & 0x1fu) << 16)
            | ((BorderStyle & 0xfu) << 12) | ((BorderColor & 0x1fu) << 7) | (BackgroundColor & 0x7fu);
    }
    /** ItemDisplayInfo of the Guild Tabard (item 5976): the blank cloth the design is painted on. */
    static constexpr uint32 GuildTabardDisplayId = 20621;

    /**
     * Apply equipment texture overlays to an existing composite texture.
     * This should be called after BuildCompositeTexture to add equipment textures.
     */
    /** Decode a BLP (MPQ or loose override, including HDX\ paths) to RGBA8. Empty on failure. */
    static TArray<uint8> LoadBlpRGBA(FMpqManager* Mpq, const FString& Path, uint32& OutW, uint32& OutH);

    static void ApplyEquipmentOverlays(TArray<uint8>& Composite, uint32 CompositeW, uint32 CompositeH,
        const FBodyEquipment& Equipment, FMpqManager* Mpq, FWowAssetCache* Cache);
};
