#pragma once
#include "CoreMinimal.h"

struct WOWDATA_API FAdtChunkData
{
    float Heights[145] = {};
    FVector Normals[145];
    FColor VertexColors[145];
    TArray<int32> TextureIndices;
    /** GroundEffectTexture.dbc ID for each MCLY texture layer. */
    TArray<uint32> GroundEffectIds;
    /** Authored 8x8 two-bit layer selection map from the MCNK header. */
    uint8 GroundEffectLayerMap[64] = {};
    /** Authored 8x8 suppression map for areas that must not receive clutter. */
    bool GroundEffectDisabled[64] = {};
    TArray<TArray<uint8>> AlphaMaps;
    int32 IndexX = 0;
    int32 IndexY = 0;
    uint32 AreaId = 0;
    uint16 Holes = 0;
    bool bHasVertexColors = false;
    /** MCNK header world position (WoW coordinates) */
    float WorldX = 0.0f;
    float WorldY = 0.0f;
    float WorldZ = 0.0f;
};

struct WOWDATA_API FAdtDoodadPlacement
{
    int32 NameIndex = 0;
    uint32 UniqueId = 0;
    FVector Position = FVector::ZeroVector;
    FVector Rotation = FVector::ZeroVector;
    /** Optional UE-space terrain normal used to pitch/roll long ground props. */
    FVector TerrainNormalUE = FVector::UpVector;
    bool bAlignToTerrainNormal = false;
    uint16 Scale = 1024;
    uint16 Flags = 0;
    float GetScaleFloat() const { return Scale == 0 ? 1.0f : Scale / 1024.0f; }
};

struct WOWDATA_API FAdtWmoPlacement
{
    int32 NameIndex = 0;
    uint32 UniqueId = 0;
    FVector Position = FVector::ZeroVector;
    FVector Rotation = FVector::ZeroVector;
    FBox BoundingBox = FBox(ForceInit);
    uint16 Flags = 0;
    uint16 DoodadSet = 0;
    uint16 NameSet = 0;
    uint16 Scale = 1024;
};

/** A single liquid layer within a chunk */
struct WOWDATA_API FMH2OInstance
{
	uint16 LiquidType = 0;
	uint16 VertexFormat = 0;
	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	uint8 XOffset = 0;
	uint8 YOffset = 0;
	uint8 Width = 8;
	uint8 Height = 8;
	/**
	 * Liquid existence bitmap. WotLK MH2O masks are interpreted in layer-local
	 * row-major order. bExistsBitmapIsFullChunk is kept for imported/test data
	 * that already expands the mask into full 8x8 chunk coordinates.
	 */
	uint64 ExistsBitmap = 0;
	/** True when the ADT supplied an explicit MH2O existence bitmap. */
	bool bHasExplicitExistsBitmap = false;
	/** True when ExistsBitmap uses full 8x8 chunk coordinates instead of rectangle-local coordinates. */
	bool bExistsBitmapIsFullChunk = false;
	/** Height values: (Width+1)*(Height+1) floats */
	TArray<float> Heights;
	/** Depth values: (Width+1)*(Height+1) uint8 */
	TArray<uint8> Depths;

	/** Best-effort MH2O liquid vertex format for the current renderer. */
	int32 GetLiquidVertexFormat() const
	{
		// WotLK ADTs normally store the LVF directly as 0..3. Some data can
		// contain higher values used as liquid object/material references; until
		// those are resolved through LiquidType/LiquidObject data, depth-only is
		// the safest fallback because it does not reinterpret arbitrary bytes as
		// floating-point heights.
		return VertexFormat <= 3 ? static_cast<int32>(VertexFormat) : 2;
	}

	bool HasSubTile(int32 Row, int32 Col, bool bTreatBitmapAsRectLocal = true) const
	{
		if (Row < 0 || Col < 0 || Row >= Height || Col >= Width)
		{
			return false;
		}

		const bool bUseFullChunkMask = bExistsBitmapIsFullChunk && !bTreatBitmapAsRectLocal;
		const int32 BitIndex = bUseFullChunkMask
			? (static_cast<int32>(YOffset) + Row) * 8 + (static_cast<int32>(XOffset) + Col)
			: Row * Width + Col;
		return BitIndex < 64 && ((ExistsBitmap & (1ULL << BitIndex)) != 0);
	}

	/** 0=water, 1=ocean, 2=magma, 3=slime based on LiquidType ranges */
	int32 GetLiquidCategory() const
	{
		if (LiquidType == 0) return -1; // placeholder / no liquid
		if (LiquidType <= 3) return 0; // water
		if (LiquidType <= 7) return 1; // ocean (or more water types)
		if (LiquidType <= 11) return 2; // magma
		return 3; // slime
	}
};

/** Water data for one chunk (up to multiple layers, usually 0-1) */
struct WOWDATA_API FMH2OChunkData
{
	TArray<FMH2OInstance> Layers;
	bool HasWater() const { return Layers.Num() > 0; }
};

struct WOWDATA_API FAdtData
{
    FAdtChunkData Chunks[256];
    TArray<FString> TexturePaths;
    TArray<FString> DoodadPaths;
    TArray<FString> WmoPaths;
    TArray<FAdtDoodadPlacement> DoodadPlacements;
    TArray<FAdtWmoPlacement> WmoPlacements;
    FMH2OChunkData WaterChunks[256];
    bool bBigAlpha = false;
    bool bIsValid = false;
};
