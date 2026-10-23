#include "WowWaterRenderer.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowWaterMaterial.h"
#include "Formats/AdtTypes.h"
#include "Formats/Dbc/DbcStore.h"
#include "Coord/WowCoordinate.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/Material.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogWowWater, Log, All);
static constexpr float WaterOverlapDepthCm = 350.0f;
static constexpr float WaterOverlapSurfacePaddingCm = 18.0f;
static constexpr float WaterOverlapBottomPaddingCm = 120.0f;
static constexpr float WaterVolumeMaxSurfaceDeltaCm = 65.0f;

struct FWaterRenderBatch
{
    TArray<FVector> Vertices;
    TArray<FVector2D> UVs;
    TArray<FVector4f> Colors;
    TArray<FVector2D> UV1s; // ocean: x = water depth in metres
    TArray<FVector2D> UV2s; // ocean: shoreward direction x strength (world XY)
    TArray<int32> Indices;
    int32 QuadCount = 0;
};

// Ocean cells are split 3x3 so swell and the shore depth are smooth (WoW cells are ~3.8 m).
static constexpr int32 OceanSubdivision = 3;

static FVector AdtToTileLocalUE(float X, float Height, float Z, int32 TileX, int32 TileY)
{
    return FWowCoordinate::AdtToUE(X, Height, Z) - FWowCoordinate::TileToWorld(TileX, TileY);
}

static TArray<FVector3f> BuildSmoothVertexNormals(const TArray<FVector>& Vertices, const TArray<int32>& Indices)
{
	TArray<FVector> AccumulatedNormals;
	AccumulatedNormals.SetNumZeroed(Vertices.Num());

	for (int32 TriangleIndex = 0; TriangleIndex + 2 < Indices.Num(); TriangleIndex += 3)
	{
		const int32 A = Indices[TriangleIndex];
		const int32 B = Indices[TriangleIndex + 1];
		const int32 C = Indices[TriangleIndex + 2];
		if (!Vertices.IsValidIndex(A) || !Vertices.IsValidIndex(B) || !Vertices.IsValidIndex(C))
		{
			continue;
		}

		// UE clockwise face indices require the negative cross product.
		const FVector TriangleNormal = -FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]);
		if (!TriangleNormal.IsNearlyZero())
		{
			const FVector SafeNormal = TriangleNormal.GetSafeNormal();
			AccumulatedNormals[A] += SafeNormal;
			AccumulatedNormals[B] += SafeNormal;
			AccumulatedNormals[C] += SafeNormal;
		}
	}

	TArray<FVector3f> Normals;
	Normals.Reserve(AccumulatedNormals.Num());
	for (FVector Normal : AccumulatedNormals)
	{
		if (Normal.IsNearlyZero() || Normal.Z < 0.0f)
		{
			Normal = FVector::UpVector;
		}
		else
		{
			Normal.Normalize();
		}
		Normals.Add(FVector3f(Normal));
	}

	return Normals;
}

// ── Helper: build a UStaticMesh from vertices/indices/UVs/colors ───────────────

// r705: the mesh description is plain data, so it can be built on a worker thread (PrefetchWaterMeshes).
static TSharedPtr<FMeshDescription> BuildLiquidMeshDescription(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Indices,
	const TArray<FVector2D>& UVs,
	const TArray<FVector4f>& Colors,
	const TArray<FVector2D>* UV1s = nullptr,
	const TArray<FVector2D>* UV2s = nullptr)
{
	TSharedPtr<FMeshDescription> Shared = MakeShared<FMeshDescription>();
	FMeshDescription& MeshDesc = *Shared;
	FStaticMeshAttributes Attributes(MeshDesc);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
	const bool bSecondUV = UV1s && UV1s->Num() == Vertices.Num();
	const bool bThirdUV = bSecondUV && UV2s && UV2s->Num() == Vertices.Num();
	VertexInstanceUVs.SetNumChannels(bThirdUV ? 3 : bSecondUV ? 2 : 1);

	FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
	MeshDesc.ReserveNewVertices(Vertices.Num());
	MeshDesc.ReserveNewVertexInstances(Vertices.Num());
	MeshDesc.ReserveNewPolygons(Indices.Num() / 3);
	MeshDesc.ReserveNewTriangles(Indices.Num() / 3);
	MeshDesc.ReserveNewEdges(Indices.Num() / 2);

	TArray<FVertexInstanceID> VertexInstanceIDs;
	VertexInstanceIDs.SetNum(Vertices.Num());
	const TArray<FVector3f> SmoothNormals = BuildSmoothVertexNormals(Vertices, Indices);

	for (int32 v = 0; v < Vertices.Num(); ++v)
	{
		const FVector& P = Vertices[v];
		FVertexID VertID = MeshDesc.CreateVertex();
		VertexPositions[VertID] = FVector3f(P.X, P.Y, P.Z);

		FVertexInstanceID InstID = MeshDesc.CreateVertexInstance(VertID);
		VertexInstanceIDs[v] = InstID;

		VertexInstanceNormals[InstID] = SmoothNormals.IsValidIndex(v) ? SmoothNormals[v] : FVector3f(0, 0, 1);
		VertexInstanceTangents[InstID] = FVector3f(1, 0, 0);
		VertexInstanceBinormalSigns[InstID] = 1.0f;

		FVector2f UV = (v < UVs.Num()) ? FVector2f(UVs[v]) : FVector2f(0, 0);
		VertexInstanceUVs.Set(InstID, 0, UV);
		if (bSecondUV) VertexInstanceUVs.Set(InstID, 1, FVector2f((*UV1s)[v]));
		if (bThirdUV) VertexInstanceUVs.Set(InstID, 2, FVector2f((*UV2s)[v]));

		// Vertex color R carries depth/opacity for water transparency.
		FVector4f VC = (v < Colors.Num()) ? Colors[v] : FVector4f(1, 1, 1, 0.7f);
		VertexInstanceColors[InstID] = VC;
	}

	// r366: no heap array per triangle (ocean bands have ~75k of them).
	for (int32 t = 0; t + 2 < Indices.Num(); t += 3)
	{
		const FVertexInstanceID TriVerts[3] = {
			VertexInstanceIDs[Indices[t]], VertexInstanceIDs[Indices[t + 1]], VertexInstanceIDs[Indices[t + 2]]};
		MeshDesc.CreateTriangle(PolyGroup, MakeArrayView(TriVerts, 3));
	}
	return Shared;
}

// Game thread: the UStaticMesh from a prepared description.
static UStaticMesh* BuildLiquidStaticMeshFromDescription(FMeshDescription& MeshDesc, UMaterialInterface* Material)
{
	UStaticMesh* SM = NewObject<UStaticMesh>();
	FStaticMaterial StaticMaterial(Material);
	StaticMaterial.UVChannelData.bInitialized = true;
	SM->GetStaticMaterials().Add(MoveTemp(StaticMaterial));

	TArray<const FMeshDescription*> MeshDescs;
	MeshDescs.Add(&MeshDesc);
	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bFastBuild = true;
	BuildParams.bCommitMeshDescription = false;
	WowPrepareRuntimeMeshMaterials(SM, const_cast<FMeshDescription&>(*MeshDescs[0]));
	SM->BuildFromMeshDescriptions(MeshDescs, BuildParams);
	SM->NeverStream = true;

	if (Material)
	{
		if (SM->GetStaticMaterials().Num() <= 0)
		{
			SM->GetStaticMaterials().SetNum(1);
		}
		SM->GetStaticMaterials()[0].MaterialInterface = Material;
	}

	return SM;
}

static UStaticMesh* BuildLiquidStaticMesh(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Indices,
	const TArray<FVector2D>& UVs,
	const TArray<FVector4f>& Colors,
	UMaterialInterface* Material,
	const TArray<FVector2D>* UV1s = nullptr,
	const TArray<FVector2D>* UV2s = nullptr)
{
	TSharedPtr<FMeshDescription> Desc = BuildLiquidMeshDescription(Vertices, Indices, UVs, Colors, UV1s, UV2s);
	return BuildLiquidStaticMeshFromDescription(*Desc, Material);
}

// r705 (Stefan: "check the performance as well"; the 18.10 log had 462 "tile-build-water" hitches of ~25 ms
// each, one per sea chunk row, while he travelled along coasts): everything a water band needs except the
// UObjects - vertices, UVs, depth and shore data, normals, the mesh descriptions and the swim-volume boxes -
// is plain data. PrefetchWaterMeshes builds it for a tile's wet bands on a worker thread while the ground is
// still being built; the band steps then only make the meshes and components.
struct FPreparedWaterBand
{
	struct FMesh { int32 Category = 0; TSharedPtr<FMeshDescription> Description; };
	TArray<FMesh> Meshes;
	TArray<TPair<FBox, int32>> Volumes;
	int32 AuthoredCells = 0;
};
struct FPreparedWaterTile
{
	std::atomic<bool> bDone{false};
	TArray<FPreparedWaterBand> Bands; // 16 chunk rows
};
static TMap<TWeakObjectPtr<const AActor>, TSharedPtr<FPreparedWaterTile, ESPMode::ThreadSafe>> GPreparedWater;
static FPreparedWaterBand PrepareWaterBand(const FAdtData& AdtData, int32 TileX, int32 TileY,
	bool bCreatePhysicsVolumes, int32 ChunkRowBegin, int32 ChunkRowEnd);

namespace { TArray<TWeakObjectPtr<UPrimitiveComponent>> MistWaterRegistry; }
void FWowWaterRenderer::GatherMistWater(UWorld* World,const FVector& Position,float Radius,TArray<UPrimitiveComponent*>& Out,bool bIncludeOcean)
{
    MistWaterRegistry.RemoveAll([](const TWeakObjectPtr<UPrimitiveComponent>& C){return !C.IsValid();});
    for(const auto& Entry:MistWaterRegistry)
    {
        auto* C=Entry.Get(); if(!C||C->GetWorld()!=World||!C->IsRegistered()) continue;
        if(!bIncludeOcean&&!C->ComponentHasTag(TEXT("WowMistWater"))) continue;
        const FBox Box=C->Bounds.GetBox();
        const FVector Surface(FMath::Clamp(Position.X,Box.Min.X,Box.Max.X),FMath::Clamp(Position.Y,Box.Min.Y,Box.Max.Y),Box.Max.Z-18.f);
        if(FVector::DistSquared(Surface,Position)<FMath::Square(Radius)) Out.Add(C);
    }
}

void FWowWaterRenderer::CreateWaterOverlapVolume(AActor* Owner, int32& WaterVolumeIndex, const FBox& WaterBounds, int32 Category)
{
	if (!Owner || !WaterBounds.IsValid)
	{
		return;
	}

	const float SurfaceZ = WaterBounds.Max.Z;
	const float VolumeBottomZ = FMath::Min(
		SurfaceZ - WaterOverlapDepthCm,
		WaterBounds.Min.Z - WaterOverlapBottomPaddingCm);
	const float VolumeHeight = SurfaceZ + WaterOverlapSurfacePaddingCm - VolumeBottomZ;
	FVector VolumeCenter = WaterBounds.GetCenter();
	VolumeCenter.Z = VolumeBottomZ + VolumeHeight * 0.5f;
	FVector VolumeExtent = WaterBounds.GetExtent();
	VolumeExtent.X = FMath::Max(VolumeExtent.X, 25.0f);
	VolumeExtent.Y = FMath::Max(VolumeExtent.Y, 25.0f);
	VolumeExtent.Z = VolumeHeight * 0.5f;

	UBoxComponent* WaterVolume = NewObject<UBoxComponent>(Owner, *FString::Printf(TEXT("WaterVolume_%d"), WaterVolumeIndex++));
	Owner->AddInstanceComponent(WaterVolume);
	WaterVolume->SetupAttachment(Owner->GetRootComponent());
	WaterVolume->SetRelativeLocation(VolumeCenter);
	WaterVolume->SetBoxExtent(VolumeExtent);
	WaterVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WaterVolume->SetCollisionObjectType(ECC_WorldDynamic);
	WaterVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	WaterVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	WaterVolume->SetGenerateOverlapEvents(true);
	WaterVolume->ComponentTags.Add(TEXT("WowWaterVolume"));
    if(Category==0) WaterVolume->ComponentTags.Add(TEXT("WowMistWater"));
    if(Category==1) WaterVolume->ComponentTags.Add(TEXT("WowOceanWater"));
	WaterVolume->RegisterComponent();
    if(Category<=1) MistWaterRegistry.Add(WaterVolume);
}

static FPreparedWaterBand PrepareWaterBand(const FAdtData& AdtData, int32 TileX, int32 TileY,
	bool bCreatePhysicsVolumes, int32 ChunkRowBegin, int32 ChunkRowEnd)
{
    FPreparedWaterBand Out;
    // r366: a full ocean tile is ~160k vertices / ~300k triangles (3x3 subdivision) and cost
    // ~450 ms in one tick. The tile builder now calls this once per band of chunk rows, so each
    // band is its own mesh built on its own tick. Names carry the band so they stay unique.
    TMap<int32, FWaterRenderBatch> Batches;
    int32 AuthoredCells = 0;
    constexpr float CellSize = FWowCoordinate::UNIT_SIZE;
    // Sea floor height anywhere in the tile (cells 0..128 along X/Z), from the chunks'
    // outer vertices: smooth and identical for every chunk that samples it.
    auto TileFloorZ = [&AdtData](float GX, float GY)
    {
        GX = FMath::Clamp(GX, 0.0f, 128.0f); GY = FMath::Clamp(GY, 0.0f, 128.0f);
        const int32 CI = FMath::Clamp(FMath::FloorToInt(GX / 8.0f), 0, 15), CJ = FMath::Clamp(FMath::FloorToInt(GY / 8.0f), 0, 15);
        const FAdtChunkData& T = AdtData.Chunks[CJ * 16 + CI];
        const float FX = FMath::Clamp(GX - CI * 8.0f, 0.0f, 8.0f), FY = FMath::Clamp(GY - CJ * 8.0f, 0.0f, 8.0f);
        const int32 X0 = FMath::Min(FMath::FloorToInt(FX), 7), Y0 = FMath::Min(FMath::FloorToInt(FY), 7);
        const float U = FX - X0, V = FY - Y0;
        auto H = [&T](int32 X, int32 Y) { const int32 I = Y * 17 + X; return (I >= 0 && I < int32(UE_ARRAY_COUNT(T.Heights))) ? float(T.Heights[I]) : 0.0f; };
        const float Bilinear = FMath::Lerp(FMath::Lerp(H(X0, Y0), H(X0 + 1, Y0), U), FMath::Lerp(H(X0, Y0 + 1), H(X0 + 1, Y0 + 1), U), V);
        return (T.WorldZ + Bilinear) * FWowCoordinate::SCALE;
    };
    // UE directions of one cell step along the ADT X ("column") and Z ("row") axes.
    const FVector AxisCol = AdtToTileLocalUE(CellSize, 0.0f, 0.0f, TileX, TileY) - AdtToTileLocalUE(0.0f, 0.0f, 0.0f, TileX, TileY);
    const FVector AxisRow = AdtToTileLocalUE(0.0f, 0.0f, CellSize, TileX, TileY) - AdtToTileLocalUE(0.0f, 0.0f, 0.0f, TileX, TileY);

    for (int32 ChunkIndex = ChunkRowBegin * 16; ChunkIndex < ChunkRowEnd * 16; ++ChunkIndex)
    {
        const FAdtChunkData& Terrain = AdtData.Chunks[ChunkIndex];
        const float ChunkX = TileX * FWowCoordinate::TILE_SIZE + (ChunkIndex % 16) * FWowCoordinate::CHUNK_SIZE;
        const float ChunkZ = TileY * FWowCoordinate::TILE_SIZE + (ChunkIndex / 16) * FWowCoordinate::CHUNK_SIZE;
        for (const FMH2OInstance& Layer : AdtData.WaterChunks[ChunkIndex].Layers)
        {
            // Liquid IDs are not category ranges (e.g. 5 is Slow Water).
            const auto* LiquidType = FDbcStore::Get().LiquidTypes().GetById(Layer.LiquidType);
            const int32 Category = LiquidType ? static_cast<int32>(LiquidType->Type) : Layer.GetLiquidCategory();
            const int32 VW = Layer.Width + 1;
            const int32 VertexCount = VW * (Layer.Height + 1);
            if (Category < 0 || !Layer.Width || !Layer.Height ||
                Layer.XOffset + Layer.Width > 8 || Layer.YOffset + Layer.Height > 8 ||
                Layer.Heights.Num() != VertexCount) continue;
            FWaterRenderBatch& Batch = Batches.FindOrAdd(Category);
            const int32 Base = Batch.Vertices.Num();
            const bool bOcean = Category == 1;
            const int32 Sub = bOcean ? OceanSubdivision : 1;
            const int32 SubVW = Layer.Width * Sub + 1;
            auto LayerVertex = [&](int32 Row, int32 Col)
            {
                const float X = ChunkX + (Layer.XOffset + Col) * CellSize;
                const float Z = ChunkZ + (Layer.YOffset + Row) * CellSize;
                return AdtToTileLocalUE(X, Layer.Heights[Row * VW + Col], Z, TileX, TileY);
            };
            // Terrain height under the liquid (UE cm): bilinear over the cell's four outer
            // vertices plus the centre vertex of the MCNK four-triangle fan.
            auto TerrainZ = [&](int32 SubRow, int32 SubCol)
            {
                const float FX = Layer.XOffset + float(SubCol) / Sub;
                const float FY = Layer.YOffset + float(SubRow) / Sub;
                const int32 CX = FMath::Clamp(FMath::FloorToInt(FX), 0, 7);
                const int32 CY = FMath::Clamp(FMath::FloorToInt(FY), 0, 7);
                const float U = FMath::Clamp(FX - CX, 0.0f, 1.0f), V = FMath::Clamp(FY - CY, 0.0f, 1.0f);
                auto Outer = [&](int32 X, int32 Y) { const int32 I = Y * 17 + X; return I >= 0 && I < 145 ? Terrain.Heights[I] : 0.0f; };
                const float H00 = Outer(CX, CY), H10 = Outer(CX + 1, CY), H01 = Outer(CX, CY + 1), H11 = Outer(CX + 1, CY + 1);
                const float Bilinear = FMath::Lerp(FMath::Lerp(H00, H10, U), FMath::Lerp(H01, H11, U), V);
                const int32 Inner = CY * 17 + 9 + CX;
                const float Centre = Inner >= 0 && Inner < 145 ? Terrain.Heights[Inner] : 0.25f * (H00 + H10 + H01 + H11);
                const float CentreWeight = FMath::Clamp(1.0f - 2.0f * FMath::Max(FMath::Abs(U - 0.5f), FMath::Abs(V - 0.5f)), 0.0f, 1.0f);
                const float H = Bilinear + (Centre - 0.25f * (H00 + H10 + H01 + H11)) * CentreWeight;
                return (Terrain.WorldZ + H) * FWowCoordinate::SCALE;
            };
            for (int32 SubRow = 0; SubRow <= Layer.Height * Sub; ++SubRow)
                for (int32 SubCol = 0; SubCol <= Layer.Width * Sub; ++SubCol)
                {
                    // Bilinear between the authored liquid vertices.
                    const int32 R0 = FMath::Min(SubRow / Sub, Layer.Height - 1), C0 = FMath::Min(SubCol / Sub, Layer.Width - 1);
                    const float FR = float(SubRow - R0 * Sub) / Sub, FC = float(SubCol - C0 * Sub) / Sub;
                    const FVector P = FMath::Lerp(FMath::Lerp(LayerVertex(R0, C0), LayerVertex(R0, C0 + 1), FC),
                        FMath::Lerp(LayerVertex(R0 + 1, C0), LayerVertex(R0 + 1, C0 + 1), FC), FR);
                    const float X = ChunkX + (Layer.XOffset + float(SubCol) / Sub) * CellSize;
                    const float Z = ChunkZ + (Layer.YOffset + float(SubRow) / Sub) * CellSize;
                    Batch.Vertices.Add(P);
                    Batch.UVs.Add(FVector2D(X / 16.0f, Z / 16.0f));
                    const int32 Index = FMath::Min(FMath::RoundToInt(float(SubRow) / Sub), int32(Layer.Height)) * VW
                        + FMath::Min(FMath::RoundToInt(float(SubCol) / Sub), int32(Layer.Width));
                    const float Depth = Layer.Depths.IsValidIndex(Index) ? Layer.Depths[Index] / 255.0f : 1.0f;
                    const float Opacity = Category <= 1 ? FMath::Lerp(0.64f, 0.94f, Depth) : 1.0f;
                    Batch.Colors.Add(FVector4f(Opacity, Opacity, Opacity, 1.0f));
                    if (bOcean)
                    {
                        Batch.UV1s.Add(FVector2D(FMath::Clamp((P.Z - TerrainZ(SubRow, SubCol)) / 100.0, 0.0, 60.0), 0.0));
                        // Shoreward direction from the smoothed sea-floor slope over the whole
                        // tile (continuous across chunks); fades out at tile borders so
                        // neighbouring tiles always agree on shared vertices.
                        const float GX = (ChunkIndex % 16) * 8 + Layer.XOffset + float(SubCol) / Sub;
                        const float GY = (ChunkIndex / 16) * 8 + Layer.YOffset + float(SubRow) / Sub;
                        const float DX = (TileFloorZ(GX + 3.0f, GY) - TileFloorZ(GX - 3.0f, GY)) / 6.0f;
                        const float DY = (TileFloorZ(GX, GY + 3.0f) - TileFloorZ(GX, GY - 3.0f)) / 6.0f;
                        const FVector2D Grad = FVector2D(AxisCol) * (DX / AxisCol.SizeSquared2D()) + FVector2D(AxisRow) * (DY / AxisRow.SizeSquared2D());
                        const float Edge = FMath::Min(FMath::Min(GX, 128.0f - GX), FMath::Min(GY, 128.0f - GY));
                        const float Strength = FMath::Clamp(static_cast<float>(Grad.Size()) / 0.03f, 0.0f, 1.0f)
                            * FMath::Clamp((Edge - 4.0f) / 8.0f, 0.0f, 1.0f);
                        Batch.UV2s.Add(Grad.GetSafeNormal() * Strength);
                    }
                }

            // Render every authored wet cell, including narrow reaches and cells
            // partly behind banks/rocks. The depth buffer clips the shoreline.
            // Never snap water vertices to banks, flatten rivers, grow masks or
            // discard small connected components; those hid legitimate water.
            // Row boxes are collected and merged with the rows above them when they
            // line up exactly (same span, similar surface): an open-sea chunk becomes
            // one swim volume instead of eight, so ocean tiles no longer spawn ~2000
            // box components on the game thread (300-500 ms hitches when they stream in).
            TArray<FBox> ChunkVolumes;
            auto EmitVolume = [&](const FBox& Box)
            {
                if (FBox* Last = ChunkVolumes.Num() ? &ChunkVolumes.Last() : nullptr)
                {
                    const FBox Merged = *Last + Box;
                    const double AreaA = (Last->Max.X - Last->Min.X) * (Last->Max.Y - Last->Min.Y);
                    const double AreaB = (Box.Max.X - Box.Min.X) * (Box.Max.Y - Box.Min.Y);
                    const double AreaM = (Merged.Max.X - Merged.Min.X) * (Merged.Max.Y - Merged.Min.Y);
                    if (AreaM <= (AreaA + AreaB) * 1.02 && FMath::Abs(Last->Max.Z - Box.Max.Z) <= WaterVolumeMaxSurfaceDeltaCm)
                    { *Last = Merged; return; }
                }
                ChunkVolumes.Add(Box);
            };
            for (int32 Row = 0; Row < Layer.Height; ++Row)
            {
                FBox RowVolume(ForceInit);
                float RowMinSurface = TNumericLimits<float>::Max();
                float RowMaxSurface = -TNumericLimits<float>::Max();
                auto FlushVolume = [&]()
                {
                    if (bCreatePhysicsVolumes && RowVolume.IsValid)
                        EmitVolume(RowVolume);
                    RowVolume = FBox(ForceInit);
                    RowMinSurface = TNumericLimits<float>::Max();
                    RowMaxSurface = -TNumericLimits<float>::Max();
                };
                for (int32 Col = 0; Col < Layer.Width; ++Col)
                {
                    if (!Layer.HasSubTile(Row, Col, !Layer.bExistsBitmapIsFullChunk))
                    {
                        FlushVolume();
                        continue;
                    }
                    // Match the UE-facing winding used by the native ocean surface.
                    // The opposite winding makes Water_Underside run above the river.
                    for (int32 SR = 0; SR < Sub; ++SR)
                        for (int32 SC = 0; SC < Sub; ++SC)
                        {
                            const int32 A = Base + (Row * Sub + SR) * SubVW + Col * Sub + SC;
                            FWowWaterRenderer::AppendSurfaceCellIndices(Batch.Indices, A, A + 1, A + SubVW, A + SubVW + 1);
                        }
                    ++Batch.QuadCount;
                    ++AuthoredCells;
                    if (!bCreatePhysicsVolumes) continue;
                    FBox CellBounds(ForceInit);
                    for (const FVector& Corner : {LayerVertex(Row, Col), LayerVertex(Row, Col + 1),
                        LayerVertex(Row + 1, Col), LayerVertex(Row + 1, Col + 1)}) CellBounds += Corner;
                    const float Surface = CellBounds.Max.Z;
                    if (RowVolume.IsValid && FMath::Max(RowMaxSurface, Surface) -
                        FMath::Min(RowMinSurface, Surface) > WaterVolumeMaxSurfaceDeltaCm) FlushVolume();
                    const int32 X = Layer.XOffset + Col;
                    const int32 Y = Layer.YOffset + Row;
                    // Include the centre vertex of the terrain's four-triangle
                    // fan, which can be substantially lower than its corners.
                    float Bottom = Surface - WaterOverlapDepthCm;
                    for (int32 Index : {Y * 17 + X, Y * 17 + X + 1,
                        (Y + 1) * 17 + X, (Y + 1) * 17 + X + 1, Y * 17 + 9 + X})
                        Bottom = FMath::Min(Bottom, (Terrain.WorldZ + Terrain.Heights[Index]) * FWowCoordinate::SCALE);
                    CellBounds.Min.Z = FMath::Min(CellBounds.Min.Z, static_cast<double>(Bottom));
                    RowVolume += CellBounds;
                    RowMinSurface = FMath::Min(RowMinSurface, Surface);
                    RowMaxSurface = FMath::Max(RowMaxSurface, Surface);
                }
                FlushVolume();
            }
            for (const FBox& Box : ChunkVolumes) Out.Volumes.Add(TPair<FBox, int32>(Box, Category));
        }
    }
    for (const auto& Pair : Batches)
    {
        const FWaterRenderBatch& Batch = Pair.Value;
        if (Batch.Indices.IsEmpty()) continue;
        FPreparedWaterBand::FMesh& Mesh = Out.Meshes.AddDefaulted_GetRef();
        Mesh.Category = Pair.Key;
        Mesh.Description = BuildLiquidMeshDescription(Batch.Vertices, Batch.Indices, Batch.UVs, Batch.Colors,
            Batch.UV1s.Num() == Batch.Vertices.Num() ? &Batch.UV1s : nullptr,
            Batch.UV2s.Num() == Batch.Vertices.Num() ? &Batch.UV2s : nullptr);
    }
    Out.AuthoredCells = AuthoredCells;
    return Out;
}

void FWowWaterRenderer::PrefetchWaterMeshes(const AActor* Owner, TSharedPtr<FAdtData> AdtData, int32 TileX, int32 TileY,
    bool bCreatePhysicsVolumes)
{
    if (!Owner || !AdtData.IsValid() || !AdtData->bIsValid) return;
    bool bWet = false;
    for (const FMH2OChunkData& Chunk : AdtData->WaterChunks) if (Chunk.Layers.Num() > 0) { bWet = true; break; }
    if (!bWet) return;
    for (auto It = GPreparedWater.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    TSharedPtr<FPreparedWaterTile, ESPMode::ThreadSafe> Tile = MakeShared<FPreparedWaterTile, ESPMode::ThreadSafe>();
    GPreparedWater.Add(Owner, Tile);
    // The task keeps its own references to the parsed tile and to the result.
    Async(EAsyncExecution::ThreadPool, [Tile, AdtData, TileX, TileY, bCreatePhysicsVolumes]()
    {
        TArray<FPreparedWaterBand> Bands;
        Bands.SetNum(16);
        for (int32 Row = 0; Row < 16; ++Row)
        {
            bool bRowWet = false;
            for (int32 C = Row * 16; C < Row * 16 + 16 && !bRowWet; ++C) bRowWet = AdtData->WaterChunks[C].Layers.Num() > 0;
            if (bRowWet) Bands[Row] = PrepareWaterBand(*AdtData, TileX, TileY, bCreatePhysicsVolumes, Row, Row + 1);
        }
        Tile->Bands = MoveTemp(Bands);
        Tile->bDone = true;
    });
}

TArray<UStaticMeshComponent*> FWowWaterRenderer::CreateWaterMeshes(
    AActor* Owner, const FAdtData& AdtData, int32 TileX, int32 TileY,
    bool bCreatePhysicsVolumes, int32 ChunkRowBegin, int32 ChunkRowEnd)
{
    TArray<UStaticMeshComponent*> Result;
    if (!Owner) return Result;
    ChunkRowBegin = FMath::Clamp(ChunkRowBegin, 0, 16);
    ChunkRowEnd = FMath::Clamp(ChunkRowEnd, ChunkRowBegin, 16);
    const double Started = FPlatformTime::Seconds();
    // r705: a band prepared on a worker thread (one chunk row); otherwise prepare it here.
    FPreparedWaterBand Band;
    bool bPrepared = false;
    if (ChunkRowEnd == ChunkRowBegin + 1)
        if (const TSharedPtr<FPreparedWaterTile, ESPMode::ThreadSafe>* Found = GPreparedWater.Find(Owner))
            if ((*Found)->bDone && (*Found)->Bands.IsValidIndex(ChunkRowBegin))
            {
                Band = MoveTemp((*Found)->Bands[ChunkRowBegin]);
                bPrepared = true;
                if (ChunkRowBegin == 15) GPreparedWater.Remove(Owner);
            }
    if (!bPrepared) Band = PrepareWaterBand(AdtData, TileX, TileY, bCreatePhysicsVolumes, ChunkRowBegin, ChunkRowEnd);

    int32 WaterVolumeIndex = ChunkRowBegin * 4096;
    for (const TPair<FBox, int32>& Volume : Band.Volumes)
        CreateWaterOverlapVolume(Owner, WaterVolumeIndex, Volume.Key, Volume.Value);
    for (FPreparedWaterBand::FMesh& Prepared : Band.Meshes)
    {
        if (!Prepared.Description.IsValid()) continue;
        UMaterialInterface* Material = FWowWaterMaterial::GetMaterialForCategory(Prepared.Category);
        UStaticMesh* Mesh = BuildLiquidStaticMeshFromDescription(*Prepared.Description, Material);
        if (Mesh && Prepared.Category == 1)
        {
            // The swell moves the surface by up to ~2 m; keep the flat mesh from being culled.
            Mesh->SetPositiveBoundsExtension(FVector(300.0, 300.0, 300.0));
            Mesh->SetNegativeBoundsExtension(FVector(300.0, 300.0, 300.0));
            Mesh->CalculateExtendedBounds();
        }
        UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner,
            *FString::Printf(TEXT("Water_Category_%d_Row%d"), Prepared.Category, ChunkRowBegin));
        Owner->AddInstanceComponent(Component);
        Component->SetupAttachment(Owner->GetRootComponent());
        Component->SetStaticMesh(Mesh);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCastShadow(false);
        Component->SetMobility(EComponentMobility::Static);
        Component->bNeverDistanceCull = true;
        Component->RegisterComponent();
        Result.Add(Component);
    }
    UE_LOG(LogWowWater, Log, TEXT("Authored MH2O tile %d,%d rows %d-%d: %d wet cells, %d meshes, %d swim volumes, %.1f ms on the game thread (%s)"),
        TileX, TileY, ChunkRowBegin, ChunkRowEnd - 1, Band.AuthoredCells, Result.Num(), WaterVolumeIndex - ChunkRowBegin * 4096,
        (FPlatformTime::Seconds() - Started) * 1000.0, bPrepared ? TEXT("prepared on a worker") : TEXT("built here"));
    return Result;
}

UStaticMeshComponent* FWowWaterRenderer::CreateOceanPlane(
	AActor* Owner, float OceanHeight, int32 TileX, int32 TileY)
{
	if (!Owner) return nullptr;

	// Create a large flat plane centered on this tile, extending well beyond tile edges
	// for a seamless ocean. Each tile is TILE_SIZE in WoW coordinates.
	// The ocean plane extends 2 tiles in each direction for overlap.
	float TileSize = FWowCoordinate::TILE_SIZE;
	float Extent = TileSize * 2.0f; // extend 2 tiles outward

	float CenterNgX = TileX * TileSize + TileSize * 0.5f;
	float CenterNgZ = TileY * TileSize + TileSize * 0.5f;

	// Generate a grid of vertices for the ocean plane (low poly is fine)
	const int32 GridRes = 8; // 8x8 grid
	float Step = (Extent * 2.0f) / GridRes;

	TArray<FVector> Vertices;
	TArray<FVector2D> UVs;
	TArray<FVector4f> Colors;
	Vertices.Reserve((GridRes + 1) * (GridRes + 1));
	UVs.Reserve((GridRes + 1) * (GridRes + 1));
	Colors.Reserve((GridRes + 1) * (GridRes + 1));

	for (int32 Row = 0; Row <= GridRes; Row++)
	{
		for (int32 Col = 0; Col <= GridRes; Col++)
		{
			float NgX = (CenterNgX - Extent) + Col * Step;
			float NgZ = (CenterNgZ - Extent) + Row * Step;

			FVector UEPos = AdtToTileLocalUE(NgX, OceanHeight, NgZ, TileX, TileY);
			Vertices.Add(UEPos);
			UVs.Add(FVector2D((float)Col / GridRes, (float)Row / GridRes));
			// Deep ocean: high opacity
			Colors.Add(FVector4f(0.05f, 0.15f, 0.35f, 0.85f));
		}
	}

	TArray<int32> Indices;
	int32 VW = GridRes + 1;
	for (int32 Row = 0; Row < GridRes; Row++)
	{
		for (int32 Col = 0; Col < GridRes; Col++)
		{
			int32 TL = Row * VW + Col;
			int32 TR = TL + 1;
			int32 BL = TL + VW;
			int32 BR = BL + 1;

			Indices.Add(TL);
			Indices.Add(BL);
			Indices.Add(TR);

			Indices.Add(TR);
			Indices.Add(BL);
			Indices.Add(BR);
		}
	}

	UMaterialInterface* WaterMat = FWowWaterMaterial::GetWaterMaterial();
	UStaticMesh* SM = BuildLiquidStaticMesh(Vertices, Indices, UVs, Colors, WaterMat);

	UStaticMeshComponent* OceanComp = NewObject<UStaticMeshComponent>(Owner, TEXT("OceanPlane"));
	OceanComp->SetupAttachment(Owner->GetRootComponent());
	OceanComp->SetStaticMesh(SM);
	OceanComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OceanComp->SetCastShadow(false);
	OceanComp->SetMobility(EComponentMobility::Static);
	OceanComp->RegisterComponent();
	// Note: uses masked blend (not translucent) to avoid Metal crash

	UE_LOG(LogWowWater, Log, TEXT("Created ocean plane at height %.1f for tile %d,%d"), OceanHeight, TileX, TileY);

	return OceanComp;
}
