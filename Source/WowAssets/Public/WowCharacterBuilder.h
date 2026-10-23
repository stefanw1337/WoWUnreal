#pragma once
#include "CoreMinimal.h"
#include "WowCharacterTexture.h"
#include "WowEquipmentManager.h"
#include "WowSkeletalMeshBuilder.h"

class FMpqManager;
class FWowAssetCache;
class USkeletalMeshComponent;
class AActor;
class UWorld;

/**
 * Builds and spawns WoW character models from race/gender/customization data.
 * Uses ChrRaces.dbc to map race+gender to M2 model paths, then loads the skeletal mesh.
 * Supports composite character textures (skin+face+underwear), equipment attachment,
 * and geoset (submesh) visibility for hair, facial hair, gloves, boots, etc.
 */
class WOWASSETS_API FWowCharacterBuilder
{
public:
    static void PrepareGlueMaterials();
    /** Forget cached character/creature models and textures (after switching the game-data folder). */
    static void ClearCaches();
    /** Race IDs from ChrRaces.dbc */
    enum class ERace : uint8
    {
        Human = 1, Orc = 2, Dwarf = 3, NightElf = 4, Undead = 5,
        Tauren = 6, Gnome = 7, Troll = 8, BloodElf = 10, Draenei = 11
    };

    enum class EGender : uint8 { Male = 0, Female = 1 };

    /** Equipment slots for character gear */
    struct FEquipmentSlot
    {
        uint32 ItemDisplayId = 0;
        uint32 EnchantmentVisualId = 0;
        uint32 PackedEnchantments = 0;
        FWowEquipmentManager::EAttachmentPoint AttachPoint = FWowEquipmentManager::EAttachmentPoint::RightHand;
        uint8 InventoryType = 0;
        uint8 SheathType = 0;
        bool bRangedWeapon = false;
    };

    /** Body equipment that affects texture overlays and geosets (defined in WowCharacterTexture.h) */
    using FBodyEquipment = FWowCharacterTexture::FBodyEquipment;

    /** Full character spawn parameters */
    struct FCharacterParams
    {
        ERace Race = ERace::Human;
        EGender Gender = EGender::Male;
        FWowCharacterTexture::FCustomization Customization;
        TArray<FEquipmentSlot> Equipment;
        FBodyEquipment BodyEquipment;
        bool bLoadAnimationsOnDemand = false;
        /** Use the HD model (HDX\\ data, see FWowHDCharacter) when it is installed. */
        bool bUseHDModel = false;
        /** r311: other players - build the gear atlas on a worker thread. The model appears with
         *  the plain skin atlas (or a neutral placeholder) and swaps to the dressed one when ready. */
        bool bAsyncEquipmentAtlas = false;
    };

    /** Get the M2 model path for a race/gender combo using ChrRaces.dbc */
    static FString GetCharacterModelPath(ERace Race, EGender Gender);

    /** r566: other players - read and parse the race/gender model on a worker thread before it is
     *  spawned (the first player of a race cost 40-60 ms of parsing on the game thread). True when
     *  the model is parsed (or cannot be); false while the worker is still busy (try again soon). */
    static bool PrewarmCharacterModel(FMpqManager* Mpq, ERace Race, EGender Gender);

    /**
     * Spawn a character actor with skeletal mesh + idle animation.
     * Simple version without customization or equipment.
     */
    static AActor* SpawnCharacter(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
        ERace Race, EGender Gender, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);

    /**
     * Spawn a fully customized character with composite texture and equipment.
     * Returns the spawned actor, or nullptr on failure.
     */
    static AActor* SpawnCharacterWithEquipment(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
        const FCharacterParams& Params, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);

    /** r331: a humanoid NPC display (CreatureDisplayInfoExtra with a playable race) as character
     *  params - race, gender, customization, body gear, helmet and shoulders - so it can be built
     *  like a player (e.g. with the HD model). False for every other display. */
    static bool BuildNpcCharacterParams(uint32 DisplayId, FCharacterParams& OutParams);

    /** Spawn a creature/NPC by display ID from CreatureDisplayInfo.dbc */
    static AActor* SpawnCreatureByDisplayId(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
        uint32 DisplayId, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator,
        bool bLoadAnimationsOnDemand = false);

    /** Spawn a generic M2 model by path. Used for glue screens, doodad-like previews, and diagnostics. */
    static AActor* SpawnM2Model(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
        const FString& ModelPath, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator,
        float Scale = 1.0f);

    /**
     * Compute the default visible geoset set for a character based on customization.
     * Returns a map from geoset group -> visible variant.
     * Groups not in the map default to variant 0 (show default/no equipment).
     */
    static TMap<uint16, uint16> ComputeDefaultGeosets(const FWowCharacterTexture::FCustomization& Customization);

    /**
     * Compute geoset visibility with equipment modifications.
     * Applies equipment-based geoset changes on top of the default customization geosets.
     */
    static TMap<uint16, uint16> ComputeGeosetWithEquipment(
        const FWowCharacterTexture::FCustomization& Customization,
        const FBodyEquipment& BodyEquipment);

    /**
     * Apply geoset visibility to a skeletal mesh component.
     * Shows sections whose geoset matches the desired variant for their group;
     * hides all others. VisibleGeosets maps group -> variant to show.
     */
    static void ApplyGeosetVisibility(USkeletalMeshComponent* MeshComp,
        const TArray<FGeosetSectionInfo>& GeosetInfo,
        const TMap<uint16, uint16>& VisibleGeosets);

    /**
     * Get the WoW animation controller from a spawned character actor.
     * Returns nullptr if the actor doesn't have an animation controller.
     */
    static class UWowAnimationController* GetAnimationController(AActor* CharacterActor);

private:
    /** Internal: load M2, build skeleton + mesh + anims, spawn actor */
    static AActor* SpawnM2Actor(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
        const FString& ModelPath, const FVector& Location, const FRotator& Rotation, float Scale = 1.0f,
        UTexture2D* OverrideTexture = nullptr,
        const FWowCharacterTexture::FCustomization* Customization = nullptr,
        const FBodyEquipment* BodyEquipment = nullptr,
        const TMap<uint32, UTexture2D*>* CreatureTextures = nullptr, bool bLoadAnimationsOnDemand = false,
        uint32 CreatureGeosetData = 0, const TMap<uint16, uint16>* GeosetOverride = nullptr);
};
