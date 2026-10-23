#pragma once
#include "CoreMinimal.h"

class FMpqManager;
class FWowAssetCache;
class USkeletalMeshComponent;
class AActor;
class USceneComponent;
struct FM2Data;

/**
 * Loads and attaches equipment (weapons, shoulders, helmets) to character models.
 * Uses ItemDisplayInfo.dbc to find model paths, then loads M2 models and
 * parents them to attachment points on the character skeleton.
 */
class WOWASSETS_API FWowEquipmentManager
{
public:
    static void PrepareStandaloneMaterials();
    /** Reload ItemVisuals/ItemVisualEffects/enchantment rows on next use (after switching the game-data folder). */
    static void ClearCaches();
    /** Well-known Wrath character M2 attachment point IDs. */
    enum class EAttachmentPoint : uint32
    {
        ShieldHand = 0,
        RightHand = 1,       // RIGHT_PALM — main-hand weapon grip
        LeftHand = 2,        // LEFT_PALM — off-hand weapon grip
        RightShoulder = 5,
        LeftShoulder = 6,
        Helmet = 11,
        Back = 12,           // BACK — cape/cloak attachment
        MainHandSheath = 26,
        OffHandSheath = 27,
        ShieldSheath = 28,
        LargeWeaponLeft = 30,
        LargeWeaponRight = 31,
        HipWeaponLeft = 32,
        HipWeaponRight = 33,
        Shield = ShieldSheath // Backward-compatible model-viewer alias.
    };

    /**
     * Attach a weapon or equipment model to a character's attachment point.
     * Loads the item's M2 model and parents it to the specified bone.
     * @param ItemDisplayId  ItemDisplayInfo.dbc ID
     * @param AttachPoint    Which attachment point to use
     * @param CharacterMesh  The character's skeletal mesh component
     * @param CharacterM2    The character's parsed M2 data (for attachment bone lookup)
     * @return The spawned static/skeletal mesh component, or nullptr
     */
    static USceneComponent* AttachEquipment(FMpqManager* Mpq, FWowAssetCache* Cache,
        uint32 ItemDisplayId, EAttachmentPoint AttachPoint,
        USkeletalMeshComponent* CharacterMesh, const FM2Data& CharacterM2, uint32 EnchantmentVisualId = 0);

    /** r398: attach an item model (ItemDisplayInfo id) to an existing socket of the built character
        mesh, e.g. "WowSheathHipWeaponLeft". Same model, textures and particles as AttachEquipment. */
    static USceneComponent* AttachItemToSocket(FMpqManager* Mpq, FWowAssetCache* Cache,
        uint32 ItemDisplayId, USkeletalMeshComponent* CharacterMesh, FName Socket);

    static void ApplyDrawnTransform(class UStaticMeshComponent* Equipment);
    static void ApplySheathedTransform(class UStaticMeshComponent* Equipment, USkeletalMeshComponent* CharacterMesh);
    static void UpdateEquipmentEnchantment(FMpqManager* Mpq, FWowAssetCache* Cache, USceneComponent* Equipment, uint32 VisualId);
    static uint32 ResolveEnchantmentVisual(FMpqManager* Mpq, uint32 PackedEnchantments);
    static void RestoreEquipmentEffects(FMpqManager* Mpq, FWowAssetCache* Cache, USceneComponent* Equipment);

private:
    static USceneComponent* AttachEquipmentImpl(FMpqManager* Mpq, FWowAssetCache* Cache,
        uint32 ItemDisplayId, EAttachmentPoint AttachPoint, USkeletalMeshComponent* CharacterMesh,
        const FM2Data* CharacterM2, FName ForcedSocket, uint32 EnchantmentVisualId);
public:
    /** Get the bone name for a given attachment point from M2 data */
    static FName GetAttachmentBoneName(const FM2Data& M2Data, EAttachmentPoint AttachPoint);

    /** Resolve a display ID into a concrete M2/BLP pair for a specific attachment point. */
    static bool ResolveEquipmentPaths(FMpqManager* Mpq, uint32 ItemDisplayId, EAttachmentPoint AttachPoint,
        FString& OutModelPath, FString* OutTexturePath = nullptr);
};
