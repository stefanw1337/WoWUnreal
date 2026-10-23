#pragma once
#include "CoreMinimal.h"

struct FM2Data;
class USkeleton;
class USkeletalMesh;
class UAnimSequence;
class FMpqManager;
class FWowAssetCache;

/**
 * Per-section geoset info returned by CreateSkeletalMesh.
 * SectionIndex maps to the USkeletalMesh material section index.
 */
struct WOWASSETS_API FGeosetSectionInfo
{
    int32 RenderPassIndex = INDEX_NONE;
    FVector2D UVScrollSpeed = FVector2D::ZeroVector;
	int32 SectionIndex = 0;   // Mesh section index (polygon group)
	uint16 GeosetId = 0;     // Raw geoset ID from M2 submesh (e.g. 0, 1, 101, 201, 401, 801)
	uint16 GeosetGroup = 0;  // GeosetId / 100 (0=body, 1=hair, 2=facial, 4=gloves, 8=tabard, etc.)
	uint16 GeosetVariant = 0; // GeosetId % 100 (0=default, 1+=specific customization/equipment)
	uint16 TextureIndex = 0; // Index into M2Data.TexturePaths for this section's render pass
	uint16 BlendMode = 0;    // M2 render-flag blend mode for this section
	uint16 RenderFlags = 0;  // M2 render flags (two-sided/unlit/etc.)
};

/**
 * Builds USkeleton, USkeletalMesh, and UAnimSequence assets from parsed M2 data.
 */
class WOWASSETS_API FWowSkeletalMeshBuilder
{
public:
	/** Build a USkeleton from M2 bone hierarchy. Returns nullptr on failure. */
	static USkeleton* CreateSkeleton(const FM2Data& Data, const FString& ModelName);

	/** Build a USkeletalMesh from M2 vertices with bone weights.
	 *  One mesh section per render pass, enabling per-geoset visibility control.
	 *  OutGeosetInfo receives the geoset ID mapping for each section.
	 *  VisibleGeosets: if non-null, only include render passes whose geoset matches. */
	static USkeletalMesh* CreateSkeletalMesh(const FM2Data& Data, USkeleton* Skeleton,
		const FString& ModelName, FMpqManager* Mpq, FWowAssetCache* Cache,
		TArray<FGeosetSectionInfo>* OutGeosetInfo = nullptr,
		const TMap<uint16, uint16>* VisibleGeosets = nullptr);

	/** Build a skeletal mesh containing ONLY render passes that reference a specific texture type.
	 *  Used to split hair (type 6) into a separate mesh from body (type 1). */
	static USkeletalMesh* CreateSkeletalMeshByTextureType(const FM2Data& Data, USkeleton* Skeleton,
		const FString& ModelName, FMpqManager* Mpq, FWowAssetCache* Cache,
		uint32 TextureType, const TMap<uint16, uint16>* VisibleGeosets = nullptr);

	/** Build UAnimSequence assets for all parsed animation tracks. */
	static TArray<UAnimSequence*> CreateAnimations(const FM2Data& Data, USkeleton* Skeleton,
		const FString& ModelName, const TSet<int32>* AnimationIds = nullptr,
		TArray<int32>* OutAnimationIds = nullptr, bool bFirstVariantOnly = false);

	/** Get a bone name for a given bone index (deterministic naming). */
	static FName GetBoneName(int32 BoneIndex);
};
