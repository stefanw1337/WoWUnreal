#include "WowWorldManager.h"
#include "WowPerformanceScope.h"
#include "GameFramework/Pawn.h"
#include "WowStartupProgress.h"
#include "WowSkyManager.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowTerrainTile.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "Formats/AdtParser.h"
#include "Formats/WdtParser.h"
#include "Formats/WdtTypes.h"
#include "Formats/WdlParser.h"
#include "Formats/WdlTypes.h"
// ProceduralMeshComponent removed — all world rendering uses UStaticMesh
#include "Coord/WowCoordinate.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "WowDoodadManager.h"
#include "WowCharacterBuilder.h"
#include "WowCharacterTexture.h"
#include "WowEquipmentManager.h"
#include "EngineUtils.h"
#include "WowTerrainMaterial.h"
#include "WowWaterRenderer.h"
#include "UnrealClient.h"
#include "WowWmoRenderer.h"
#include "Formats/Dbc/DbcStore.h"
#include "Async/Async.h"

// r649 (Stefan: "huge drops in FPS down to 8 ... very jumpy at the start"): the distant (LOD 1) tiles were read
// from the archives and parsed on the game thread, one per streaming tick (about 50 ADT parses in the first
// seconds). They are now read and parsed on a worker like the near tiles; the game thread only builds the
// coarse mesh from the parsed data.
namespace WowLod1Async
{
    struct FParse { uint64 Generation = 0; TFuture<TSharedPtr<FAdtData>> Future; };
    static TMap<int64, FParse> InFlight;
    static TMap<int64, TSharedPtr<FAdtData>> Ready; // parsed, waiting for SpawnLod1Tile
    static constexpr int32 MaxInFlight = 4;
    static void WaitAll()
    {
        for (auto& Pair : InFlight) if (Pair.Value.Future.IsValid()) Pair.Value.Future.Wait();
        InFlight.Empty();
        Ready.Empty();
    }
}
#include "VT/RuntimeVirtualTexture.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "WowTerrainMeshBuilder.h"
#include "WowAudioManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowWorld, Log, All);

namespace
{
constexpr uint8 WdlTransition_North = 1 << 0;
constexpr uint8 WdlTransition_South = 1 << 1;
constexpr uint8 WdlTransition_West = 1 << 2;
constexpr uint8 WdlTransition_East = 1 << 3;
constexpr int32 WdlTransitionSamples = 16 * 8 + 1;
constexpr float WdlTransitionDepthOffset = 15.0f;

int32 OuterVertexIndex(int32 X, int32 Y)
{
    return Y * 17 + X;
}

bool SaveViewportPng(const FString& OutputPath)
{
    FString AbsPath = FPaths::ConvertRelativePathToFull(OutputPath);

    // Use UE's built-in screenshot API — no platform-specific tools needed
    FScreenshotRequest::RequestScreenshot(AbsPath, true, false);

    UE_LOG(LogWowWorld, Log, TEXT("Screenshot requested via FScreenshotRequest: %s"), *AbsPath);
    return true; // Screenshot is async; file appears after next frame render
}

void SpawnNativeFallbackScene(UWorld* World)
{
    if (!World) return;

    AActor* Scene = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector,
        FRotator::ZeroRotator);
    if (!Scene) return;
#if WITH_EDITOR
    Scene->SetActorLabel(TEXT("NativeNoExternalDataFallback"));
#endif

    USceneComponent* Root = NewObject<USceneComponent>(Scene, TEXT("FallbackRoot"));
    Scene->AddInstanceComponent(Root);
    Root->RegisterComponent();
    Scene->SetRootComponent(Root);

    UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    auto AddShape = [Scene, Root, ShapeMaterial](const TCHAR* Name, const TCHAR* MeshPath,
        const FVector& Location, const FVector& Scale, const FRotator& Rotation,
        const FLinearColor& Color) -> UStaticMeshComponent*
    {
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
        if (!Mesh) return nullptr;

        UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(Scene, Name);
        Scene->AddInstanceComponent(Shape);
        Shape->SetupAttachment(Root);
        Shape->SetStaticMesh(Mesh);
        Shape->SetRelativeLocation(Location);
        Shape->SetRelativeRotation(Rotation);
        Shape->SetRelativeScale3D(Scale);
        Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Shape->RegisterComponent();
        if (ShapeMaterial)
        {
            UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ShapeMaterial, Shape);
            Material->SetVectorParameterValue(TEXT("Color"), Color);
            Shape->SetMaterial(0, Material);
        }
        return Shape;
    };

    constexpr const TCHAR* CubeMesh = TEXT("/Engine/BasicShapes/Cube.Cube");
    constexpr const TCHAR* SphereMesh = TEXT("/Engine/BasicShapes/Sphere.Sphere");
    constexpr const TCHAR* CylinderMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
    constexpr const TCHAR* ConeMesh = TEXT("/Engine/BasicShapes/Cone.Cone");

    // A deliberately sparse native floor: this uses only Engine primitives and
    // remains available when no external archive or interface data can load.
    UStaticMeshComponent* Floor = AddShape(TEXT("EmptyFallbackPlane"), CubeMesh,
        FVector(1050.0f, 0.0f, -55.0f),
        FVector(22.0f, 15.0f, 1.0f), FRotator::ZeroRotator,
        FLinearColor(0.14f, 0.16f, 0.18f, 1.0f));
    if (Floor)
    {
        Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Floor->SetCollisionObjectType(ECC_WorldStatic);
        Floor->SetCollisionResponseToAllChannels(ECR_Block);
    }

    // A generic, original fantasy-orc gag. The head and boots protrude from a
    // fallen rock; no game-specific model, texture, insignia, or named character.
    const FLinearColor OrcGreen(0.16f, 0.42f, 0.08f, 1.0f);
    AddShape(TEXT("UnluckyOrcHead"), SphereMesh, FVector(930.0f, 5.0f, 66.0f),
        FVector(0.42f, 0.48f, 0.38f), FRotator::ZeroRotator, OrcGreen);
    AddShape(TEXT("UnluckyOrcEarLeft"), ConeMesh, FVector(930.0f, 52.0f, 69.0f),
        FVector(0.16f, 0.16f, 0.34f), FRotator(0.0f, 0.0f, -90.0f), OrcGreen);
    AddShape(TEXT("UnluckyOrcEarRight"), ConeMesh, FVector(930.0f, -42.0f, 69.0f),
        FVector(0.16f, 0.16f, 0.34f), FRotator(0.0f, 0.0f, 90.0f), OrcGreen);
    AddShape(TEXT("UnluckyOrcBrow"), CubeMesh, FVector(892.0f, 5.0f, 79.0f),
        FVector(0.10f, 0.30f, 0.07f), FRotator::ZeroRotator,
        FLinearColor(0.08f, 0.20f, 0.025f, 1.0f));
    AddShape(TEXT("UnluckyOrcEyeLeft"), SphereMesh, FVector(887.0f, -11.0f, 72.0f),
        FVector(0.055f), FRotator::ZeroRotator, FLinearColor(0.9f, 0.72f, 0.12f, 1.0f));
    AddShape(TEXT("UnluckyOrcEyeRight"), SphereMesh, FVector(887.0f, 21.0f, 72.0f),
        FVector(0.055f), FRotator::ZeroRotator, FLinearColor(0.9f, 0.72f, 0.12f, 1.0f));
    AddShape(TEXT("UnluckyOrcNose"), SphereMesh, FVector(884.0f, 5.0f, 58.0f),
        FVector(0.12f, 0.15f, 0.12f), FRotator::ZeroRotator, OrcGreen);
    AddShape(TEXT("UnluckyOrcTuskLeft"), ConeMesh, FVector(882.0f, -11.0f, 47.0f),
        FVector(0.07f, 0.07f, 0.16f), FRotator(0.0f, -90.0f, 0.0f),
        FLinearColor(0.82f, 0.76f, 0.58f, 1.0f));
    AddShape(TEXT("UnluckyOrcTuskRight"), ConeMesh, FVector(882.0f, 21.0f, 47.0f),
        FVector(0.07f, 0.07f, 0.16f), FRotator(0.0f, -90.0f, 0.0f),
        FLinearColor(0.82f, 0.76f, 0.58f, 1.0f));
    AddShape(TEXT("UnluckyOrcBootLeft"), CubeMesh, FVector(955.0f, -230.0f, 16.0f),
        FVector(0.42f, 0.18f, 0.16f), FRotator(0.0f, -14.0f, 0.0f),
        FLinearColor(0.10f, 0.045f, 0.02f, 1.0f));
    AddShape(TEXT("UnluckyOrcBootRight"), CubeMesh, FVector(955.0f, -165.0f, 16.0f),
        FVector(0.42f, 0.18f, 0.16f), FRotator(0.0f, 12.0f, 0.0f),
        FLinearColor(0.10f, 0.045f, 0.02f, 1.0f));
    AddShape(TEXT("FallenRock"), SphereMesh, FVector(980.0f, -95.0f, 43.0f),
        FVector(1.45f, 1.15f, 0.82f), FRotator(12.0f, 18.0f, -8.0f),
        FLinearColor(0.19f, 0.20f, 0.22f, 1.0f));

    UTextRenderComponent* Caption = NewObject<UTextRenderComponent>(Scene, TEXT("FallbackCaption"));
    Scene->AddInstanceComponent(Caption);
    Caption->SetupAttachment(Root);
    Caption->SetRelativeLocation(FVector(650.0f, -420.0f, 1.0f));
    Caption->SetRelativeRotation(FRotator(-90.0f, 180.0f, 0.0f));
    Caption->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    Caption->SetText(FText::FromString(TEXT("EXTERNAL DATA CAN BE LINKED\nLink a supported data folder to load the world")));
    Caption->SetTextRenderColor(FColor(150, 155, 165));
    Caption->SetWorldSize(24.0f);
    Caption->RegisterComponent();

    UPointLightComponent* Light = NewObject<UPointLightComponent>(Scene, TEXT("FallbackLight"));
    Scene->AddInstanceComponent(Light);
    Light->SetupAttachment(Root);
    Light->SetRelativeLocation(FVector(500.0f, 0.0f, 900.0f));
    Light->SetIntensity(18000.0f);
    Light->SetAttenuationRadius(3500.0f);
    Light->RegisterComponent();

    UE_LOG(LogWowWorld, Warning,
        TEXT("Spawned native no-data fallback: walkable plane and unlucky generic orc"));
}

int32 GetTileDistance(const FIntPoint& CameraTile, int32 TX, int32 TY)
{
    return FMath::Max(FMath::Abs(TX - CameraTile.X), FMath::Abs(TY - CameraTile.Y));
}

float GetChunkOuterHeight(const FAdtChunkData& Chunk, int32 OuterX, int32 OuterY)
{
    return Chunk.WorldZ + Chunk.Heights[OuterVertexIndex(OuterX, OuterY)];
}

bool IsNeighborInLod1Range(const FWdtData* WdtData, const FIntPoint& CameraTile, int32 LoadRadius, int32 Lod1Radius, int32 TX, int32 TY)
{
    if (!WdtData || TX < 0 || TX >= 64 || TY < 0 || TY >= 64 || !WdtData->TileExists[TX][TY])
    {
        return false;
    }

    const int32 Distance = GetTileDistance(CameraTile, TX, TY);
    return Distance > LoadRadius && Distance <= Lod1Radius;
}

uint8 DetermineWdlTransitionEdges(const FWdtData* WdtData, const FIntPoint& CameraTile, int32 LoadRadius, int32 Lod1Radius, int32 TX, int32 TY)
{
    uint8 EdgeMask = 0;
    if (IsNeighborInLod1Range(WdtData, CameraTile, LoadRadius, Lod1Radius, TX, TY - 1))
    {
        EdgeMask |= WdlTransition_North;
    }
    if (IsNeighborInLod1Range(WdtData, CameraTile, LoadRadius, Lod1Radius, TX, TY + 1))
    {
        EdgeMask |= WdlTransition_South;
    }
    if (IsNeighborInLod1Range(WdtData, CameraTile, LoadRadius, Lod1Radius, TX - 1, TY))
    {
        EdgeMask |= WdlTransition_West;
    }
    if (IsNeighborInLod1Range(WdtData, CameraTile, LoadRadius, Lod1Radius, TX + 1, TY))
    {
        EdgeMask |= WdlTransition_East;
    }
    return EdgeMask;
}

bool LoadTileAdtForTransition(FMpqManager* Mpq, const FString& MapName, bool bBigAlpha, int32 TX, int32 TY, FAdtData& OutAdtData)
{
    if (!Mpq)
    {
        return false;
    }

    const FString AdtPath = FString::Printf(TEXT("World\\Maps\\%s\\%s_%d_%d.adt"), *MapName, *MapName, TX, TY);
    TArray<uint8> AdtRaw;
    if (!Mpq->ReadFile(AdtPath, AdtRaw))
    {
        return false;
    }

    OutAdtData = FAdtParser::Parse(AdtRaw, bBigAlpha);
    return OutAdtData.bIsValid;
}

void BuildNorthEdgeHeights(const FAdtData& AdtData, TArray<float>& OutHeights)
{
    OutHeights.Reset();
    OutHeights.Reserve(WdlTransitionSamples);

    for (int32 ChunkX = 0; ChunkX < 16; ++ChunkX)
    {
        const FAdtChunkData& Chunk = AdtData.Chunks[ChunkX];
        for (int32 SampleX = 0; SampleX < 8; ++SampleX)
        {
            OutHeights.Add(GetChunkOuterHeight(Chunk, SampleX, 0));
        }
    }

    OutHeights.Add(GetChunkOuterHeight(AdtData.Chunks[15], 8, 0));
}

void BuildSouthEdgeHeights(const FAdtData& AdtData, TArray<float>& OutHeights)
{
    OutHeights.Reset();
    OutHeights.Reserve(WdlTransitionSamples);

    for (int32 ChunkX = 0; ChunkX < 16; ++ChunkX)
    {
        const FAdtChunkData& Chunk = AdtData.Chunks[15 * 16 + ChunkX];
        for (int32 SampleX = 0; SampleX < 8; ++SampleX)
        {
            OutHeights.Add(GetChunkOuterHeight(Chunk, SampleX, 8));
        }
    }

    OutHeights.Add(GetChunkOuterHeight(AdtData.Chunks[15 * 16 + 15], 8, 8));
}

void BuildWestEdgeHeights(const FAdtData& AdtData, TArray<float>& OutHeights)
{
    OutHeights.Reset();
    OutHeights.Reserve(WdlTransitionSamples);

    for (int32 ChunkY = 0; ChunkY < 16; ++ChunkY)
    {
        const FAdtChunkData& Chunk = AdtData.Chunks[ChunkY * 16];
        for (int32 SampleY = 0; SampleY < 8; ++SampleY)
        {
            OutHeights.Add(GetChunkOuterHeight(Chunk, 0, SampleY));
        }
    }

    OutHeights.Add(GetChunkOuterHeight(AdtData.Chunks[15 * 16], 0, 8));
}

void BuildEastEdgeHeights(const FAdtData& AdtData, TArray<float>& OutHeights)
{
    OutHeights.Reset();
    OutHeights.Reserve(WdlTransitionSamples);

    for (int32 ChunkY = 0; ChunkY < 16; ++ChunkY)
    {
        const FAdtChunkData& Chunk = AdtData.Chunks[ChunkY * 16 + 15];
        for (int32 SampleY = 0; SampleY < 8; ++SampleY)
        {
            OutHeights.Add(GetChunkOuterHeight(Chunk, 8, SampleY));
        }
    }

    OutHeights.Add(GetChunkOuterHeight(AdtData.Chunks[15 * 16 + 15], 8, 8));
}

float SampleWdlRowHeight(const FWdlTileData& TileData, int32 Row, int32 SampleIndex)
{
    if (SampleIndex >= WdlTransitionSamples - 1)
    {
        return static_cast<float>(TileData.Height17[Row][16]);
    }

    const int32 Segment = FMath::Clamp(SampleIndex / 8, 0, 15);
    const float Alpha = static_cast<float>(SampleIndex % 8) / 8.0f;
    return FMath::Lerp(
        static_cast<float>(TileData.Height17[Row][Segment]),
        static_cast<float>(TileData.Height17[Row][Segment + 1]),
        Alpha);
}

float SampleWdlColumnHeight(const FWdlTileData& TileData, int32 Column, int32 SampleIndex)
{
    if (SampleIndex >= WdlTransitionSamples - 1)
    {
        return static_cast<float>(TileData.Height17[16][Column]);
    }

    const int32 Segment = FMath::Clamp(SampleIndex / 8, 0, 15);
    const float Alpha = static_cast<float>(SampleIndex % 8) / 8.0f;
    return FMath::Lerp(
        static_cast<float>(TileData.Height17[Segment][Column]),
        static_cast<float>(TileData.Height17[Segment + 1][Column]),
        Alpha);
}

void ComputeSectionNormals(const TArray<FVector>& Vertices, const TArray<int32>& Indices, TArray<FVector>& OutNormals)
{
    OutNormals.Init(FVector::ZeroVector, Vertices.Num());

    for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
    {
        const FVector& A = Vertices[Indices[Index]];
        const FVector& B = Vertices[Indices[Index + 1]];
        const FVector& C = Vertices[Indices[Index + 2]];
        const FVector FaceNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();

        OutNormals[Indices[Index]] += FaceNormal;
        OutNormals[Indices[Index + 1]] += FaceNormal;
        OutNormals[Indices[Index + 2]] += FaceNormal;
    }

    for (FVector& Normal : OutNormals)
    {
        Normal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    }
}

void BuildWdlTransitionGeometry(
    const FWdlTileData& TileData,
    int32 TX,
    int32 TY,
    uint8 Edge,
    const TArray<float>& OuterHeights,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutIndices,
    TArray<FVector2D>& OutUVs)
{
    if (OuterHeights.Num() != WdlTransitionSamples) return;

    const int32 BaseVertex = OutVertices.Num();

    const float TileNgX = TX * FWowCoordinate::TILE_SIZE;
    const float TileNgZ = TY * FWowCoordinate::TILE_SIZE;
    const float FineStep = FWowCoordinate::UNIT_SIZE;
    const float CoarseStep = FWowCoordinate::CHUNK_SIZE;

    // Outer row
    for (int32 SampleIndex = 0; SampleIndex < WdlTransitionSamples; ++SampleIndex)
    {
        const float Offset = SampleIndex * FineStep;
        float OuterX = TileNgX, OuterZ = TileNgZ;

        switch (Edge)
        {
        case WdlTransition_North: OuterX += Offset; break;
        case WdlTransition_South: OuterX += Offset; OuterZ += FWowCoordinate::TILE_SIZE; break;
        case WdlTransition_West:  OuterZ += Offset; break;
        case WdlTransition_East:  OuterX += FWowCoordinate::TILE_SIZE; OuterZ += Offset; break;
        default: return;
        }

        FVector V = FWowCoordinate::AdtToUE(OuterX, OuterHeights[SampleIndex], OuterZ);
        V.Z -= WdlTransitionDepthOffset;
        OutVertices.Add(V);
        OutUVs.Add(FVector2D(static_cast<float>(SampleIndex) / (WdlTransitionSamples - 1), 0.0f));
    }

    // Inner row
    for (int32 SampleIndex = 0; SampleIndex < WdlTransitionSamples; ++SampleIndex)
    {
        const float Offset = SampleIndex * FineStep;
        float InnerX = TileNgX, InnerZ = TileNgZ;
        float InnerHeight = 0.0f;

        switch (Edge)
        {
        case WdlTransition_North:
            InnerX += Offset; InnerZ += CoarseStep;
            InnerHeight = SampleWdlRowHeight(TileData, 1, SampleIndex); break;
        case WdlTransition_South:
            InnerX += Offset; InnerZ += FWowCoordinate::TILE_SIZE - CoarseStep;
            InnerHeight = SampleWdlRowHeight(TileData, 15, SampleIndex); break;
        case WdlTransition_West:
            InnerX += CoarseStep; InnerZ += Offset;
            InnerHeight = SampleWdlColumnHeight(TileData, 1, SampleIndex); break;
        case WdlTransition_East:
            InnerX += FWowCoordinate::TILE_SIZE - CoarseStep; InnerZ += Offset;
            InnerHeight = SampleWdlColumnHeight(TileData, 15, SampleIndex); break;
        default: return;
        }

        FVector V = FWowCoordinate::AdtToUE(InnerX, InnerHeight, InnerZ);
        V.Z -= WdlTransitionDepthOffset;
        OutVertices.Add(V);
        OutUVs.Add(FVector2D(static_cast<float>(SampleIndex) / (WdlTransitionSamples - 1), 1.0f));
    }

    // Triangles
    for (int32 SampleIndex = 0; SampleIndex < WdlTransitionSamples - 1; ++SampleIndex)
    {
        const int32 TL = BaseVertex + SampleIndex;
        const int32 TR = BaseVertex + SampleIndex + 1;
        const int32 BL = BaseVertex + WdlTransitionSamples + SampleIndex;
        const int32 BR = BL + 1;

        OutIndices.Add(TL); OutIndices.Add(BL); OutIndices.Add(TR);
        OutIndices.Add(TR); OutIndices.Add(BL); OutIndices.Add(BR);
    }
}
}

AWowWorldManager::AWowWorldManager() { PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = 0.1f; }

bool AWowWorldManager::IsPositionInsidePlayableWorld(const FVector& WorldPosition) const
{
	// Treat the world as valid until its WDT is ready. This avoids a false fatigue
	// bar during initial streaming or in native fallback test scenes.
	if (!WdtData || !WdtData->bIsValid)
	{
		return true;
	}
	// r556: a dungeon (WMO-only map) has no terrain tiles at all; all of it is playable.
	if (IsWmoOnlyMap())
	{
		return true;
	}

	const FIntPoint Tile = FWowCoordinate::WorldToTile(WorldPosition);
	return Tile.X >= 0 && Tile.X < 64 && Tile.Y >= 0 && Tile.Y < 64 &&
		WdtData->TileExists[Tile.X][Tile.Y];
}

bool AWowWorldManager::IsWmoOnlyMap() const
{
    return WdtData && WdtData->bIsValid && !WdtData->GlobalWmoPath.IsEmpty();
}

bool AWowWorldManager::IsGlobalWmoReady() const
{
    // A WMO that could not be loaded must not hold the loading screen forever.
    return bGlobalWmoFailed || (GlobalWmoActor && !FWowWmoRenderer::HasPendingBuilds());
}

bool AWowWorldManager::IsTerrainReadyAtPosition(const FVector& WorldPosition) const
{
    if (IsWmoOnlyMap()) return IsGlobalWmoReady();
	const FIntPoint Tile = FWowCoordinate::WorldToTile(WorldPosition);
	return IsTileTerrainReady(Tile.X, Tile.Y);
}

// r565: a tile is in LoadedTiles from its first build step; its ground is there (mesh and cooked
// collision) only once the tile says so.
bool AWowWorldManager::IsTileTerrainReady(int32 TX, int32 TY) const
{
	const TObjectPtr<AWowTerrainTile>* Tile = LoadedTiles.Find(TileKey(TX, TY));
	return Tile && *Tile && (*Tile)->IsTerrainReady();
}

bool AWowWorldManager::IsTileMeshBuilt(int32 TX, int32 TY) const
{
	const TObjectPtr<AWowTerrainTile>* Tile = LoadedTiles.Find(TileKey(TX, TY));
	return Tile && *Tile && (*Tile)->IsTerrainMeshBuilt();
}

bool AWowWorldManager::IsInitialLoadComplete() const
{
	if (IsWmoOnlyMap())
	{
		return IsGlobalWmoReady();
	}
	if (InitialTilesQueued <= 0 || InitialTilesLoaded != InitialTilesQueued)
	{
		return false;
	}

	for (const int64 Key : InitialTileKeys)
	{
		const TObjectPtr<AWowTerrainTile>* Tile = LoadedTiles.Find(Key);
		if (!Tile || !*Tile || !(*Tile)->IsTerrainReady())
		{
			return false;
		}
	}
	return true;
}

void AWowWorldManager::RefreshInitialTileProgress()
{
	InitialTilesLoaded = 0;
	for (const int64 Key : InitialTileKeys)
	{
		const TObjectPtr<AWowTerrainTile>* Tile = LoadedTiles.Find(Key);
		if (Tile && *Tile && (*Tile)->IsTerrainReady()) // r565: ground built and walkable
		{
			++InitialTilesLoaded;
		}
	}
}

void AWowWorldManager::BeginPlay()
{
    Super::BeginPlay();
    const TCHAR* CmdLine = FCommandLine::Get();
    MpqManager = MakeUnique<FMpqManager>();
    AssetCache = MakeUnique<FWowAssetCache>();
    AssetCache->SetRuntimeContext(GetWorld());
    FString DataPath;
    FParse::Value(CmdLine, TEXT("-wowdata="), DataPath);
    if (DataPath.IsEmpty()) DataPath = TEXT("/Users/clancey/Downloads/World of Warcraft 3.3.5a/Data");
    int32 StartupTileX = DebugTileX;
    int32 StartupTileY = DebugTileY;
    float StartupCameraHeight = 40000.0f;
    float StartupCameraPitch = -20.0f;
    float StartupCameraYaw = 0.0f;
    FParse::Value(CmdLine, TEXT("-tilex="), StartupTileX);
    FParse::Value(CmdLine, TEXT("-tiley="), StartupTileY);
    FParse::Value(CmdLine, TEXT("-cameraheight="), StartupCameraHeight);
    FParse::Value(CmdLine, TEXT("-camerapitch="), StartupCameraPitch);
    FParse::Value(CmdLine, TEXT("-camerayaw="), StartupCameraYaw);
    FParse::Value(CmdLine, TEXT("-autoscreenshotdelay="), AutoScreenshotDelaySeconds);
    FParse::Value(CmdLine, TEXT("-autoquitdelay="), AutoQuitDelaySeconds);
    FParse::Value(CmdLine, TEXT("-autoscreenshot="), AutoScreenshotPath);
    bAutoWaitForInitialLoad = (FParse::Param(CmdLine, TEXT("autologin"))
        || FParse::Param(CmdLine, TEXT("offlineloading"))
        || FParse::Param(CmdLine, TEXT("autowaitforinitialload")))
        && (AutoScreenshotDelaySeconds >= 0.0f || AutoQuitDelaySeconds >= 0.0f);
    bAutoTimerArmed = !bAutoWaitForInitialLoad;
    if (AutoScreenshotDelaySeconds >= 0.0f)
    {
        if (AutoScreenshotPath.IsEmpty())
        {
            AutoScreenshotPath = FString::Printf(TEXT("auto_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
        }
        if (FPaths::IsRelative(AutoScreenshotPath))
        {
            AutoScreenshotPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), AutoScreenshotPath);
        }

        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.CreateDirectoryTree(*FPaths::GetPath(AutoScreenshotPath));
        UE_LOG(LogWowWorld, Log, TEXT("Auto screenshot scheduled at %.1fs%s -> %s"),
            AutoScreenshotDelaySeconds,
            bAutoWaitForInitialLoad ? TEXT(" after initial terrain load") : TEXT(""),
            *AutoScreenshotPath);
    }
    if (AutoQuitDelaySeconds >= 0.0f)
    {
        UE_LOG(LogWowWorld, Log, TEXT("Auto quit scheduled at %.1fs%s"),
            AutoQuitDelaySeconds,
            bAutoWaitForInitialLoad ? TEXT(" after initial terrain load") : TEXT(""));
    }
    AutoStartWallClockSeconds = FPlatformTime::Seconds();
    if (!MpqManager->Initialize(DataPath, { FMpqManager::FindUserRoot(FPaths::ProjectDir()), FMpqManager::FindClientRoot(FPaths::ProjectDir()) }))
    {
        UE_LOG(LogWowWorld, Error, TEXT("Failed to init MPQ from: %s"), *DataPath);
        SpawnNativeFallbackScene(GetWorld());
        return;
    }
    UE_LOG(LogWowWorld, Log, TEXT("World Manager ready: %s"), *MapName);

    WowReportStartupProgress(2);

    // Load essential DBC tables (Tier 1) synchronously
    FDbcStore::Get().LoadAll(*MpqManager);
    WowReportStartupProgress(3);

    // Load deferred DBC tables (Tier 2) on background thread
    DeferredDbcLoad = Async(EAsyncExecution::Thread, [this]()
    {
        FDbcStore::Get().LoadDeferred(*MpqManager);
    });

    // Check if a test scene mode wants MPQ-only (no terrain loading)
    FString TestScene;
    FParse::Value(FCommandLine::Get(), TEXT("-testscene="), TestScene);
    FString CurrentMapName;
    if (const UWorld* World = GetWorld())
    {
        CurrentMapName = World->GetMapName();
        CurrentMapName.RemoveFromStart(World->StreamingLevelsPrefix);
    }

    if (TestScene.Equals(TEXT("character"), ESearchCase::IgnoreCase) ||
        TestScene.Equals(TEXT("ui"), ESearchCase::IgnoreCase) ||
        CurrentMapName.Equals(TEXT("CharacterTest"), ESearchCase::IgnoreCase) ||
        CurrentMapName.Equals(TEXT("ModelViewer"), ESearchCase::IgnoreCase))
    {
        UE_LOG(LogWowWorld, Log, TEXT("Terrain loading skipped for test scene '%s' on map '%s' (MPQ-only mode)"),
            *TestScene, *CurrentMapName);
        return;
    }

    // Direct world tests may explicitly enable terrain without logging in. A glue
    // flow must remain lightweight even if a stale launcher argument requests
    // terrain; the login controller enables it only after Enter World.
    const bool bGlueFlow = FParse::Param(CmdLine, TEXT("offlineflow")) ||
        (TestScene.Equals(TEXT("login"), ESearchCase::IgnoreCase) &&
            !FParse::Param(CmdLine, TEXT("offlineloading")));
    if (FParse::Param(CmdLine, TEXT("forceterrain")) && !bGlueFlow)
    {
        bStreamingEnabled = true;
        UE_LOG(LogWowWorld, Log, TEXT("Terrain streaming forced by -forceterrain"));
    }
    else if (FParse::Param(CmdLine, TEXT("forceterrain")) && bGlueFlow)
    {
        UE_LOG(LogWowWorld, Warning,
            TEXT("Ignoring -forceterrain during glue flow; world streaming remains deferred until Enter World"));
    }

// If streaming is disabled at startup (e.g. login scene), skip all terrain
// loading. The LoginController will enable streaming and call LoadInitialTerrain()
// when the player enters the world.
    if (!bStreamingEnabled)
    {
        UE_LOG(LogWowWorld, Log, TEXT("Terrain loading deferred (bStreamingEnabled=false, login scene)"));
        return;
    }

    // Load WDT for the map
    FString WdtPath = FString::Printf(TEXT("World\\Maps\\%s\\%s.wdt"), *MapName, *MapName);
    TArray<uint8> WdtRaw;
    if (MpqManager->ReadFile(WdtPath, WdtRaw))
    {
        WdtData = MakeUnique<FWdtData>(FWdtParser::Parse(WdtRaw));
        UE_LOG(LogWowWorld, Log, TEXT("Loaded WDT for %s (BigAlpha=%d)"), *MapName, WdtData->bUseBigAlpha ? 1 : 0);

        // Log existing tiles for debugging
        int32 TileCount = 0;
        FString FirstTiles;
        for (int32 y = 0; y < 64; y++)
        {
            for (int32 x = 0; x < 64; x++)
            {
                if (WdtData->TileExists[x][y])
                {
                    TileCount++;
                    if (TileCount <= 10)
                    {
                        FirstTiles += FString::Printf(TEXT("(%d,%d) "), x, y);
                    }
                }
            }
        }
        UE_LOG(LogWowWorld, Log, TEXT("WDT has %d tiles. First: %s"), TileCount, *FirstTiles);

        // Check if debug tile exists
        if (StartupTileX >= 0 && StartupTileX < 64 && StartupTileY >= 0 && StartupTileY < 64)
        {
            UE_LOG(LogWowWorld, Log, TEXT("Startup tile %d,%d exists: %s"), StartupTileX, StartupTileY,
                WdtData->TileExists[StartupTileX][StartupTileY] ? TEXT("YES") : TEXT("NO"));
        }
    }
    else
    {
        UE_LOG(LogWowWorld, Warning, TEXT("Failed to load WDT: %s"), *WdtPath);
    }

    // Load WDL for distant terrain
    FString WdlPath = FString::Printf(TEXT("World\\Maps\\%s\\%s.wdl"), *MapName, *MapName);
    TArray<uint8> WdlRaw;
    if (MpqManager->ReadFile(WdlPath, WdlRaw))
    {
        WdlData = MakeUnique<FWdlData>(FWdlParser::Parse(WdlRaw));
        UE_LOG(LogWowWorld, Log, TEXT("Loaded WDL for %s (valid=%d)"), *MapName, WdlData->bIsValid ? 1 : 0);
    }
    else
    {
        UE_LOG(LogWowWorld, Warning, TEXT("No WDL file found: %s"), *WdlPath);
    }

    // Set up Runtime Virtual Texture for terrain
    SetupRuntimeVirtualTexture();

    // Load a 3x3 grid of tiles around the debug tile (async so the renderer
    // is not starved — each tile builds 256 mesh chunks on the game thread).
    for (int32 DX = -1; DX <= 1; ++DX)
    {
        for (int32 DY = -1; DY <= 1; ++DY)
        {
            LoadTileAsync(StartupTileX + DX, StartupTileY + DY);
        }
    }

    // Enable streaming so more tiles load as camera moves
    // Test scenes with limited terrain disable streaming after initial load
    if (TestScene.Equals(TEXT("terrain"), ESearchCase::IgnoreCase) ||
        TestScene.Equals(TEXT("wmo"), ESearchCase::IgnoreCase))
    {
        bStreamingEnabled = false;
        UE_LOG(LogWowWorld, Log, TEXT("Streaming disabled for test scene '%s'"), *TestScene);
    }
    else
    {
        bStreamingEnabled = true;
    }

    // Teleport player above the loaded terrain
    // The tile actor is at TileToWorld position, vertices are local to that
    // First chunk (0,0) starts at local offset ~(-26667, -26667, height*100)
    // The height for Elwynn is ~236 WoW units = 23600 UE cm
    FVector TileCenter = FWowCoordinate::TileToWorld(StartupTileX, StartupTileY);
    // Start at a reasonable flying height (above terrain which is ~100-300 WoW units = 10000-30000 cm)
    TileCenter.Z = StartupCameraHeight;

    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (PC && PC->GetPawn())
    {
        PC->GetPawn()->SetActorLocation(TileCenter);
        // Allow verification runs to change the framing without touching defaults.
        PC->SetControlRotation(FRotator(StartupCameraPitch, StartupCameraYaw, 0.0f));
        UE_LOG(LogWowWorld, Log, TEXT("Teleported player to: %s"), *TileCenter.ToString());
    }
}

int32 AWowWorldManager::LoadTilesAroundPosition(const FVector& UEPosition, int32 Radius)
{
    FIntPoint CenterTile = FWowCoordinate::WorldToTile(UEPosition);
    int32 Queued = 0;

    UE_LOG(LogWowWorld, Log, TEXT("Loading initial tiles around UE pos (%.0f, %.0f, %.0f) = tile (%d, %d), radius %d"),
        UEPosition.X, UEPosition.Y, UEPosition.Z, CenterTile.X, CenterTile.Y, Radius);

    InitialTilesQueued = 0;
    InitialTilesLoaded = 0;
    InitialTileKeys.Empty();

    for (int32 DX = -Radius; DX <= Radius; ++DX)
    {
        for (int32 DY = -Radius; DY <= Radius; ++DY)
        {
            int32 TX = CenterTile.X + DX;
            int32 TY = CenterTile.Y + DY;
            if (TX >= 0 && TX < 64 && TY >= 0 && TY < 64)
            {
                if (WdtData && !WdtData->TileExists[TX][TY]) continue;
                int64 Key = TileKey(TX, TY);
                if (!IsTileLoaded(TX, TY) && !IsTilePending(TX, TY))
                {
                    LoadTileAsync(TX, TY);
                    ++Queued;
                }
                InitialTileKeys.Add(Key);
            }
        }
    }

    InitialTilesQueued = InitialTileKeys.Num();
    RefreshInitialTileProgress();

    UE_LOG(LogWowWorld, Log, TEXT("Initial load: %d tiles queued, %d already loaded, %d total tracked"),
        Queued, InitialTilesLoaded, InitialTilesQueued);
    return Queued;
}

void AWowWorldManager::BeginWorldEntryWarmup(const FVector& UEPosition)
{
    WorldEntryStreamingFocus = UEPosition;
    bHasWorldEntryStreamingFocus = true;
    LastCameraTile = FIntPoint(-9999, -9999);
    ObjectStreamingPassCount = 0;
    ObjectStreamingRevision = 0;
    FWowWmoRenderer::SetFastBuild(true); // loading screen: large WMOs build at full speed (r246)

    UE_LOG(LogWowWorld, Log, TEXT("World-entry streaming focus set to %s"), *UEPosition.ToString());
}

void AWowWorldManager::EndWorldEntryWarmup()
{
    if (!bHasWorldEntryStreamingFocus)
    {
        return;
    }

    bHasWorldEntryStreamingFocus = false;
    FWowWmoRenderer::SetFastBuild(false);
    LastCameraTile = FIntPoint(-9999, -9999);
    UE_LOG(LogWowWorld, Log, TEXT("World-entry streaming focus returned to the gameplay camera"));
}

bool AWowWorldManager::SelectMap(uint32 MapId)
{
    const auto* Map=FDbcStore::Get().Maps().GetById(MapId);
    if(!Map || Map->InternalName.IsEmpty() || !HasExternalData())
    {
        UE_LOG(LogWowWorld,Error,TEXT("Cannot select map %u: map metadata/data unavailable"),MapId);
        return false;
    }
    if(MapName==Map->InternalName && WdtData && WdtData->bIsValid) return true;
    TArray<uint8> Raw;
    const FString Path=FString::Printf(TEXT("World\\Maps\\%s\\%s.wdt"),*Map->InternalName,*Map->InternalName);
    if(!MpqManager->ReadFile(Path,Raw)) return false;
    auto NewWdt=MakeUnique<FWdtData>(FWdtParser::Parse(Raw));
    if(!NewWdt->bIsValid) return false;
    // Retire in-flight work without blocking the game thread or closing MPQs.
    ++MapGeneration;
    WowLod1Async::Ready.Empty(); // r649: parsed tiles of the old map (in-flight ones are dropped by generation)
    DestroyGlobalWmo();
    PendingTileKeys.Empty();
    for(auto& Pair:LoadedTiles) if(Pair.Value)
    {
        for(auto& Wmo:Pair.Value->SpawnedWmos) if(Wmo.Value) Wmo.Value->Destroy();
        Pair.Value->Destroy();
    }
    LoadedTiles.Empty();
    for(auto& Pair:WdlTiles) if(Pair.Value) Pair.Value->Destroy();
    WdlTiles.Empty();
    for(auto& Pair:Lod1Tiles) if(Pair.Value) Pair.Value->Destroy();
    Lod1Tiles.Empty();
    SpawnedWmoIds.Empty(); SpawnedDoodadIds.Empty();
    InitialTileKeys.Empty(); InitialTilesQueued=0; InitialTilesLoaded=0;
    ActiveDoodadCount=0; ActiveWmoGroupCount=0;
    CurrentAudioAreaId=0; bHasWorldEntryStreamingFocus=false;
    LastCameraTile=FIntPoint(-9999,-9999);
    ObjectStreamingPassCount=0; ++ObjectStreamingRevision;
    MapName=Map->InternalName; WdtData=MoveTemp(NewWdt); WdlData.Reset();
    Raw.Reset();
    const FString WdlPath=FString::Printf(TEXT("World\\Maps\\%s\\%s.wdl"),*MapName,*MapName);
    if(MpqManager->ReadFile(WdlPath,Raw)) WdlData=MakeUnique<FWdlData>(FWdlParser::Parse(Raw));
    if(!TerrainRVT || !RVTVolumeComponent) SetupRuntimeVirtualTexture();
    bStreamingEnabled=true;
    UE_LOG(LogWowWorld,Log,TEXT("Selected terrain map %u: %s generation=%llu"),MapId,*MapName,MapGeneration);
    return true;
}

void AWowWorldManager::EnableTerrainStreaming()
{
    if (bStreamingEnabled) return; // Already enabled
    if (!MpqManager || !MpqManager->IsInitialized())
    {
        UE_LOG(LogWowWorld, Error, TEXT("Cannot enable terrain streaming — MPQ not initialized"));
        return;
    }

    UE_LOG(LogWowWorld, Log, TEXT("Enabling deferred terrain streaming for map: %s"), *MapName);

    // Load WDT
    FString WdtPath = FString::Printf(TEXT("World\\Maps\\%s\\%s.wdt"), *MapName, *MapName);
    TArray<uint8> WdtRaw;
    if (MpqManager->ReadFile(WdtPath, WdtRaw))
    {
        WdtData = MakeUnique<FWdtData>(FWdtParser::Parse(WdtRaw));
        UE_LOG(LogWowWorld, Log, TEXT("Loaded WDT for %s (BigAlpha=%d)"), *MapName, WdtData->bUseBigAlpha ? 1 : 0);
    }

    // Load WDL
    FString WdlPath = FString::Printf(TEXT("World\\Maps\\%s\\%s.wdl"), *MapName, *MapName);
    TArray<uint8> WdlRaw;
    if (MpqManager->ReadFile(WdlPath, WdlRaw))
    {
        WdlData = MakeUnique<FWdlData>(FWdlParser::Parse(WdlRaw));
        UE_LOG(LogWowWorld, Log, TEXT("Loaded WDL for %s"), *MapName);
    }

    SetupRuntimeVirtualTexture();

    bStreamingEnabled = true;
    UE_LOG(LogWowWorld, Log, TEXT("Terrain streaming enabled — tiles will load around camera position"));
}

void AWowWorldManager::WaitForDeferredDbcLoad()
{
    if (DeferredDbcLoad.IsValid()) DeferredDbcLoad.Wait();
}

bool AWowWorldManager::SwitchDataFolder(const FString& NewDataPath, FString& OutError)
{
    if (!MpqManager || !AssetCache)
    {
        OutError = TEXT("Game data is not initialized.");
        UE_LOG(LogWowWorld, Error, TEXT("Game data NOT switched to %s: %s"), *NewDataPath, *OutError);
        return false;
    }
    if (bStreamingEnabled || PendingLoads.Num() > 0 || LoadedTiles.Num() > 0)
    {
        // r231 (Stefan 12.10: logged out of TormentWoW, logged in to TheraWoW and still had
        // TormentWoW's data and addons): this used to refuse with "Cannot change game data while
        // the world is loaded", silently. Switching only happens at the login screen, so the old
        // world is no longer in use: wait for in-flight tile loads (they read the archives), then
        // drop it like SelectMap does; the next world entry loads the map from the new data.
        for (FPendingTileLoad& Pending : PendingLoads)
            if (Pending.Future.IsValid()) Pending.Future.Wait();
        PendingLoads.Empty();
        WowLod1Async::WaitAll(); // r649: they read the archives too
        TilesBuilding.Empty();
        PendingTileKeys.Empty();
        ++MapGeneration;
        for (auto& Pair : LoadedTiles) if (Pair.Value)
        {
            for (auto& Wmo : Pair.Value->SpawnedWmos) if (Wmo.Value) Wmo.Value->Destroy();
            Pair.Value->Destroy();
        }
        LoadedTiles.Empty();
        for (auto& Pair : WdlTiles) if (Pair.Value) Pair.Value->Destroy();
        WdlTiles.Empty();
        for (auto& Pair : Lod1Tiles) if (Pair.Value) Pair.Value->Destroy();
        Lod1Tiles.Empty();
        SpawnedWmoIds.Empty(); SpawnedDoodadIds.Empty();
        InitialTileKeys.Empty(); InitialTilesQueued = 0; InitialTilesLoaded = 0;
        ActiveDoodadCount = 0; ActiveWmoGroupCount = 0;
        CurrentAudioAreaId = 0; bHasWorldEntryStreamingFocus = false;
        LastCameraTile = FIntPoint(-9999, -9999);
        ObjectStreamingPassCount = 0; ++ObjectStreamingRevision;
        WdtData.Reset(); WdlData.Reset();
        DestroyGlobalWmo();
        bStreamingEnabled = false;
        UE_LOG(LogWowWorld, Log, TEXT("Unloaded the world of %s before switching game data"), *MpqManager->GetDataPath());
    }
    // Other players' gear atlases are composited on worker threads that read the archives.
    FWowCharacterTexture::WaitForAsyncWork();
    const FString OldPath = MpqManager->GetDataPath();
    const double Started = FPlatformTime::Seconds();
    // Nothing may read the archives or write the tables while they are swapped.
    WaitForDeferredDbcLoad();
    const TArray<FString> Overrides = { FMpqManager::FindUserRoot(FPaths::ProjectDir()), FMpqManager::FindClientRoot(FPaths::ProjectDir()) };
    const auto Reload = [this, &Overrides](const FString& Path)
    {
        if (!MpqManager->Initialize(Path, Overrides)) return false;
        FDbcStore::Get().ResetForReload();
        FDbcStore::Get().LoadAll(*MpqManager);
        FDbcStore::Get().LoadDeferred(*MpqManager);
        return FDbcStore::Get().IsLoaded();
    };
    // Data-derived caches: models, skeletons, textures, sounds, item visual tables.
    AssetCache->Clear();
    FWowDoodadManager::ClearCaches();
    FWowCharacterBuilder::ClearCaches();
    FWowEquipmentManager::ClearCaches();
    for (TActorIterator<AWowAudioManager> It(GetWorld()); It; ++It) It->ClearSoundCache();
    bool bOk = Reload(NewDataPath);
    if (!bOk)
    {
        OutError = FString::Printf(TEXT("Could not load game data from %s"), *NewDataPath);
        UE_LOG(LogWowWorld, Error, TEXT("%s - restoring %s"), *OutError, *OldPath);
        if (!Reload(OldPath))
            UE_LOG(LogWowWorld, Error, TEXT("Restoring game data from %s failed too"), *OldPath);
    }
    // Light.dbc entries were reallocated: re-bind the sky's zone pointers.
    for (TActorIterator<AWowSkyManager> It(GetWorld()); It; ++It) It->LoadLightZones(It->CurrentMapId);
    UE_LOG(LogWowWorld, Log, TEXT("Game data %s: %s -> %s (%.2f s)"), bOk ? TEXT("switched") : TEXT("NOT switched"),
        *OldPath, *MpqManager->GetDataPath(), FPlatformTime::Seconds() - Started);
    return bOk;
}

void AWowWorldManager::EndPlay(const EEndPlayReason::Type R)
{
    // Wait for any pending async loads to complete before cleanup
    for (FPendingTileLoad& Pending : PendingLoads)
    {
        if (Pending.Future.IsValid())
        {
            Pending.Future.Wait();
        }
    }
    PendingLoads.Empty();
    WowLod1Async::WaitAll(); // r649
    TilesBuilding.Empty();
    PendingTileKeys.Empty();

    // Destroy all loaded tiles
    for (auto& Pair : LoadedTiles)
    {
        if (Pair.Value)
        {
            Pair.Value->Destroy();
        }
    }
    LoadedTiles.Empty();

    // Destroy WDL tiles and release their static meshes
    for (auto& Pair : WdlTiles)
    {
        if (Pair.Value)
        {
            Pair.Value->Destroy();
        }
    }
    WdlTiles.Empty();

    // Destroy LOD 1 tiles
    for (auto& Pair : Lod1Tiles)
    {
        if (Pair.Value) Pair.Value->Destroy();
    }
    Lod1Tiles.Empty();

    WdtData.Reset();
    WdlData.Reset();

    // r311: other players' gear atlases are composited on worker threads that read the MPQs.
    FWowCharacterTexture::WaitForAsyncWork();
    if (AssetCache) AssetCache->Clear();
    if (MpqManager) MpqManager->Shutdown();
    Super::EndPlay(R);
}

void AWowWorldManager::Tick(float DT)
{
    FWowPerformanceScope Timing(TEXT("world-tick"), TEXT("world"), 16.0);
    Super::Tick(DT);

    // Always process completed async loads, even before streaming is enabled
    ProcessPendingLoads();

    if (bAutoWaitForInitialLoad && !bAutoTimerArmed)
    {
        if (InitialTilesQueued <= 0)
        {
            // The world manager can tick once before the login/gameplay controller
            // supplies the entry position and queues its initial terrain set. Do not
            // let automated captures inherit time spent loading the map and fire on
            // the loading screen during that short hand-off.
            return;
        }

        if (IsInitialLoadComplete())
        {
            bAutoTimerArmed = true;
            AutoStartWallClockSeconds = FPlatformTime::Seconds();
            AutoLogBucket = -1;
            UE_LOG(LogWowWorld, Log, TEXT("Auto timers armed after initial terrain load (%d/%d tiles)"),
                InitialTilesLoaded, InitialTilesQueued);
        }
        else
        {
            return;
        }
    }

    const double AutoElapsedSeconds = FPlatformTime::Seconds() - AutoStartWallClockSeconds;

    if ((AutoScreenshotDelaySeconds >= 0.0f || AutoQuitDelaySeconds >= 0.0f) && AutoElapsedSeconds >= 0.0)
    {
        const int32 CurrentBucket = FMath::FloorToInt32(AutoElapsedSeconds / 5.0);
        if (CurrentBucket != AutoLogBucket)
        {
            AutoLogBucket = CurrentBucket;
            UE_LOG(LogWowWorld, Log, TEXT("Auto timer: %.1fs elapsed (shot=%.1fs quit=%.1fs)"),
                AutoElapsedSeconds, AutoScreenshotDelaySeconds, AutoQuitDelaySeconds);
        }
    }

    if (!bAutoScreenshotRequested && AutoScreenshotDelaySeconds >= 0.0f && AutoElapsedSeconds >= AutoScreenshotDelaySeconds)
    {
        bAutoScreenshotRequested = true;
        const bool bSaved = SaveViewportPng(AutoScreenshotPath);
        UE_LOG(LogWowWorld, Log, TEXT("%s auto screenshot capture: %s"),
            bSaved ? TEXT("Saved") : TEXT("Failed to save"),
            *AutoScreenshotPath);
    }

    if (!bAutoQuitRequested && AutoQuitDelaySeconds >= 0.0f && AutoElapsedSeconds >= AutoQuitDelaySeconds)
    {
        bAutoQuitRequested = true;
        UE_LOG(LogWowWorld, Log, TEXT("Requesting auto quit at %.1fs"), AutoElapsedSeconds);
        FPlatformMisc::RequestExit(false);
        return;
    }

    if (!bStreamingEnabled || !MpqManager || !MpqManager->IsInitialized())
    {
        return;
    }

    UpdateStreaming();
    UpdateObjectStreaming();
    UpdateZoneAudio();

    // Periodically purge stale cache entries and check budget
    static int32 CachePurgeCounter = 0;
    if (AssetCache && (++CachePurgeCounter % 100 == 0)) // every ~10 seconds at 0.1s tick
    {
        AssetCache->PurgeStaleEntries();
        if (AssetCache->IsOverBudget())
        {
            FWowCacheStats Stats = AssetCache->GetStats();
            UE_LOG(LogWowWorld, Warning, TEXT("Asset cache over budget: %lld MB (budget: %lld MB, %d textures, %d meshes)"),
                Stats.TotalEstimatedMemory() / (1024 * 1024),
                AssetCache->MemoryBudget / (1024 * 1024),
                Stats.TextureCount, Stats.MeshCount);
        }
    }
}

void AWowWorldManager::LoadTile(int32 TX, int32 TY)
{
    if (IsTileLoaded(TX, TY) || IsTilePending(TX, TY))
    {
        return;
    }

    // Check WDT if available
    if (WdtData && WdtData->bIsValid)
    {
        if (TX < 0 || TX >= 64 || TY < 0 || TY >= 64 || !WdtData->TileExists[TX][TY])
        {
            UE_LOG(LogWowWorld, Warning, TEXT("Tile %d,%d does not exist in WDT"), TX, TY);
            return;
        }
    }
    else
    {
        UE_LOG(LogWowWorld, Warning, TEXT("No valid WDT data, attempting tile load anyway"));
    }

    // Read ADT file from MPQ
    FString AdtPath = FString::Printf(TEXT("World\\Maps\\%s\\%s_%d_%d.adt"), *MapName, *MapName, TX, TY);
    TArray<uint8> AdtRaw;
    if (!MpqManager->ReadFile(AdtPath, AdtRaw))
    {
        UE_LOG(LogWowWorld, Warning, TEXT("Failed to read ADT: %s"), *AdtPath);
        return;
    }

    bool bBigAlpha = WdtData ? WdtData->bUseBigAlpha : false;
    FAdtData AdtData = FAdtParser::Parse(AdtRaw, bBigAlpha);
    if (!AdtData.bIsValid)
    {
        UE_LOG(LogWowWorld, Warning, TEXT("Failed to parse ADT: %s"), *AdtPath);
        return;
    }

    UE_LOG(LogWowWorld, Log, TEXT("Loading tile %d,%d (%s)"), TX, TY, *AdtPath);

    // Spawn terrain tile actor
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *FString::Printf(TEXT("TerrainTile_%d_%d"), TX, TY);
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    AWowTerrainTile* Tile = GetWorld()->SpawnActor<AWowTerrainTile>(AWowTerrainTile::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (Tile)
    {
        Tile->BuildFromAdtData(AdtData, TX, TY, MpqManager.Get(), AssetCache.Get(), &SpawnedWmoIds);
        if (TerrainRVT) Tile->ApplyRuntimeVirtualTexture(TerrainRVT);
        LoadedTiles.Add(TileKey(TX, TY), Tile);
    }
}

void AWowWorldManager::LoadTileAsync(int32 TX, int32 TY)
{
    if (IsTileLoaded(TX, TY) || IsTilePending(TX, TY))
    {
        return;
    }

    // Check WDT if available
    if (WdtData && WdtData->bIsValid)
    {
        if (TX < 0 || TX >= 64 || TY < 0 || TY >= 64 || !WdtData->TileExists[TX][TY])
        {
            return;
        }
    }

    int64 Key = TileKey(TX, TY);
    PendingTileKeys.Add(Key);

    UE_LOG(LogWowWorld, Log, TEXT("Async loading tile %d,%d"), TX, TY);

    // Capture what we need for the background thread
    FMpqManager* Mpq = MpqManager.Get();
    FString Map = MapName;
    bool bBigAlpha = WdtData ? WdtData->bUseBigAlpha : false;

    FPendingTileLoad Pending;
    Pending.Generation = MapGeneration;
    Pending.TX = TX;
    Pending.TY = TY;
    Pending.Future = Async(EAsyncExecution::ThreadPool, [Mpq, Map, TX, TY, bBigAlpha]() -> TSharedPtr<FAdtData>
    {
        FString AdtPath = FString::Printf(TEXT("World\\Maps\\%s\\%s_%d_%d.adt"), *Map, *Map, TX, TY);
        TArray<uint8> AdtRaw;
        if (!Mpq->ReadFile(AdtPath, AdtRaw))
        {
            UE_LOG(LogWowWorld, Warning, TEXT("Async: Failed to read ADT: %s"), *AdtPath);
            return nullptr;
        }

        TSharedPtr<FAdtData> Result = MakeShared<FAdtData>(FAdtParser::Parse(AdtRaw, bBigAlpha));
        if (!Result->bIsValid)
        {
            UE_LOG(LogWowWorld, Warning, TEXT("Async: Failed to parse ADT: %s"), *AdtPath);
            return nullptr;
        }

        return Result;
    });

    PendingLoads.Add(MoveTemp(Pending));
}

void AWowWorldManager::FinalizeTileLoad(int32 TX, int32 TY, TSharedPtr<FAdtData> AdtData)
{
    if (!AdtData || IsTileLoaded(TX, TY))
    {
        return;
    }

    UE_LOG(LogWowWorld, Log, TEXT("Finalizing async tile %d,%d on game thread"), TX, TY);

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *FString::Printf(TEXT("TerrainTile_%d_%d"), TX, TY);
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    AWowTerrainTile* Tile = GetWorld()->SpawnActor<AWowTerrainTile>(AWowTerrainTile::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (Tile)
    {
        // Terrain (ground, collision, placement data) now; water and doodads follow
        // on later ticks (AdvanceTileBuild) so one tile never costs a whole hitch.
        // r565: the ground itself is built in steps too (AWowTerrainTile::BuildTerrainStep).
        if (TerrainRVT) Tile->ApplyRuntimeVirtualTexture(TerrainRVT);
        const bool bGroundBuilt = Tile->BuildTerrainStep(*AdtData, TX, TY, MpqManager.Get(), AssetCache.Get());
        FTileBuildInProgress& Building = TilesBuilding.AddDefaulted_GetRef();
        Building.Tile = Tile; Building.Data = AdtData; Building.TX = TX; Building.TY = TY;
        Building.Generation = MapGeneration;
        Building.NextPhase = bGroundBuilt ? 1 : 0;
        // r705/r706: the tile's water geometry is prepared on a worker while its ground is built.
        FWowWaterRenderer::PrefetchWaterMeshes(Tile, AdtData, TX, TY, true);
        if (TerrainRVT) Tile->ApplyRuntimeVirtualTexture(TerrainRVT);
        int64 Key = TileKey(TX, TY);
        LoadedTiles.Add(Key, Tile);

        // Track currently resident entry tiles. A tile can be reloaded later, so
        // this must never be a cumulative counter (25/25 must not become 26/25).
        if (InitialTileKeys.Contains(Key))
        {
            RefreshInitialTileProgress();
            UE_LOG(LogWowWorld, Log, TEXT("Initial tile %d,%d loaded (%d/%d)"),
                TX, TY, InitialTilesLoaded, InitialTilesQueued);
        }
    }
}

bool AWowWorldManager::AdvanceTileBuild()
{
    while (TilesBuilding.Num() > 0)
    {
        FTileBuildInProgress& Building = TilesBuilding[0];
        AWowTerrainTile* Tile = Building.Tile.Get();
        if (!Tile || !Building.Data || Building.Generation != MapGeneration ||
            Building.NextPhase >= AWowTerrainTile::NumBuildPhases)
        {
            TilesBuilding.RemoveAt(0);
            continue;
        }
        if (Building.NextPhase == 0)
        {
            // r565: one ground step per call; water and doodads follow once the ground mesh is built.
            if (Tile->BuildTerrainStep(*Building.Data, Building.TX, Building.TY, MpqManager.Get(), AssetCache.Get()))
                Building.NextPhase = 1;
            return true;
        }
        if (Building.NextPhase == AWowTerrainTile::NumWaterBands + 1)
        {
            // r567: the doodads in steps of a few models.
            if (Tile->BuildDoodadStep(*Building.Data, Building.TX, Building.TY, MpqManager.Get(), AssetCache.Get()))
                TilesBuilding.RemoveAt(0);
            return true;
        }
        Tile->BuildPhase(Building.NextPhase++, *Building.Data, Building.TX, Building.TY, MpqManager.Get(), AssetCache.Get());
        // r498: water bands (one chunk row each) without any liquid cost nothing; skip them in this tick.
        while (Building.NextPhase >= 1 && Building.NextPhase <= AWowTerrainTile::NumWaterBands)
        {
            const int32 RowsPerBand = 16 / AWowTerrainTile::NumWaterBands;
            const int32 FirstChunk = (Building.NextPhase - 1) * RowsPerBand * 16;
            bool bWet = false;
            for (int32 Chunk = FirstChunk; Chunk < FirstChunk + RowsPerBand * 16 && !bWet; ++Chunk)
                bWet = Building.Data->WaterChunks[Chunk].Layers.Num() > 0;
            if (bWet) break;
            ++Building.NextPhase;
        }
        if (Building.NextPhase >= AWowTerrainTile::NumBuildPhases)
        {
            TilesBuilding.RemoveAt(0);
        }
        return true;
    }
    return false;
}

void AWowWorldManager::ProcessPendingLoads()
{
    FWowPerformanceScope Timing(TEXT("terrain-finalization"), TEXT("world"), 16.0);
    RefreshInitialTileProgress(); // r565: tiles turn ready when their collision cook finishes
    // r565: small build steps until a time budget is used: 6 ms while playing, 40 ms behind the
    // loading screen. Finish started tiles first.
    {
        const double Budget = bHasWorldEntryStreamingFocus ? 0.040 : 0.006;
        const double Start = FPlatformTime::Seconds();
        bool bWorked = false;
        while (FPlatformTime::Seconds() - Start < Budget && AdvanceTileBuild()) bWorked = true;
        if (bWorked) return;
    }
    // Unreal object and render-resource creation happens on the game thread. Finalizing
    // more than one parsed tile per tick causes visible multi-frame stalls, even when
    // the archive reads themselves completed asynchronously.
    constexpr int32 MaxPerTick = 1;

    int32 Finalized = 0;
    for (int32 i = PendingLoads.Num() - 1; i >= 0; --i)
    {
        FPendingTileLoad& Pending = PendingLoads[i];
        if (Pending.Future.IsValid() && Pending.Future.IsReady())
        {
            if(Pending.Generation!=MapGeneration)
            {
                Pending.Future.Get(); PendingLoads.RemoveAt(i); continue;
            }
            // Do not consume a future until its result can be finalized.
            if(Finalized>=MaxPerTick) continue;
            TSharedPtr<FAdtData> Result = Pending.Future.Get();
            int64 Key = TileKey(Pending.TX, Pending.TY);
            PendingTileKeys.Remove(Key);

            if (Result && Finalized < MaxPerTick)
            {
                FinalizeTileLoad(Pending.TX, Pending.TY, Result);
                ++Finalized;
            }
            else if (Result)
            {
                // Re-queue: data is ready but we hit the per-tick limit.
                // Leave it in the array for next tick.
                continue;
            }

            PendingLoads.RemoveAt(i);
        }
    }
}

bool AWowWorldManager::IsTilePending(int32 TX, int32 TY) const
{
    return PendingTileKeys.Contains(TileKey(TX, TY));
}

void AWowWorldManager::UnloadTile(int32 TX, int32 TY)
{
    int64 Key = TileKey(TX, TY);
    TObjectPtr<AWowTerrainTile>* Found = LoadedTiles.Find(Key);
    if (Found && *Found)
    {
        (*Found)->Destroy();
        LoadedTiles.Remove(Key);
		if (InitialTileKeys.Contains(Key))
		{
			RefreshInitialTileProgress();
		}
        UE_LOG(LogWowWorld, Log, TEXT("Unloaded tile %d,%d"), TX, TY);
    }
}

bool AWowWorldManager::IsTileLoaded(int32 TX, int32 TY) const
{
    return LoadedTiles.Contains(TileKey(TX, TY));
}

void AWowWorldManager::UpdateStreaming()
{
    FWowPerformanceScope Timing(TEXT("world-streaming"), TEXT("world"), 16.0);
    FVector CamLoc;
    if (!ResolveStreamingViewLocation(CamLoc)) return;

    // Figure out which tile the camera is over
    FIntPoint CameraTile = FWowCoordinate::WorldToTile(CamLoc);

    const bool bTileChanged = (CameraTile != LastCameraTile);

    // Always run throttled LOD1/WDL spawning (they limit per-tick work internally)
    UpdateWdlStreaming(CameraTile);
    UpdateLod1Streaming(CameraTile);

    if (!bTileChanged) return;
    LastCameraTile = CameraTile;

    // Keep RVT volume centered on camera
    UpdateRVTBounds(CameraTile);

    UE_LOG(LogWowWorld, Verbose, TEXT("Camera tile: %d,%d"), CameraTile.X, CameraTile.Y);

    // Load tiles within radius (async to avoid main-thread stalls)
    for (int32 DX = -LoadRadius; DX <= LoadRadius; ++DX)
    {
        for (int32 DY = -LoadRadius; DY <= LoadRadius; ++DY)
        {
            int32 TX = CameraTile.X + DX;
            int32 TY = CameraTile.Y + DY;
            if (TX >= 0 && TX < 64 && TY >= 0 && TY < 64)
            {
                LoadTileAsync(TX, TY);
            }
        }
    }

    // Never remove the physical ground beneath the possessed pawn. Camera/view
    // targets may change during glue-screen handoff, but pawn safety is invariant.
    FIntPoint PawnTile = CameraTile;
    bool bHasPawnTile = false;
    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (const APawn* Pawn = PC->GetPawn())
            {
                PawnTile = FWowCoordinate::WorldToTile(Pawn->GetActorLocation());
                bHasPawnTile = true;
            }
        }
    }

    // Unload tiles beyond unload radius
    TArray<int64> TilesToUnload;
    for (auto& Pair : LoadedTiles)
    {
        if (Pair.Value)
        {
            FIntPoint Coord = Pair.Value->GetTileCoord();
            int32 Dist = FMath::Max(FMath::Abs(Coord.X - CameraTile.X), FMath::Abs(Coord.Y - CameraTile.Y));
            const int32 PawnDist = FMath::Max(FMath::Abs(Coord.X - PawnTile.X), FMath::Abs(Coord.Y - PawnTile.Y));
            if (Dist > UnloadRadius && (!bHasPawnTile || PawnDist > 1))
            {
                TilesToUnload.Add(Pair.Key);
            }
        }
    }
    bool bEntryTileUnloaded = false;
    for (int64 Key : TilesToUnload)
    {
        TObjectPtr<AWowTerrainTile>* Found = LoadedTiles.Find(Key);
        if (Found && *Found)
        {
            AWowTerrainTile* Tile = *Found;
            UE_LOG(LogWowWorld, Log, TEXT("Unloading tile %d,%d (too far)"), Tile->GetTileCoord().X, Tile->GetTileCoord().Y);

            // Clean up spawned objects tracked by this tile
            // HISMC doodads are destroyed automatically with the tile actor
            if (!Tile->bUsesInstancedDoodads)
            {
                for (auto& DoodadPair : Tile->SpawnedDoodads)
                {
                    if (DoodadPair.Value)
                    {
                        DoodadPair.Value->DestroyComponent();
                        --ActiveDoodadCount;
                        ++ObjectStreamingRevision;
                    }
                    SpawnedDoodadIds.Remove(DoodadPair.Key);
                }
            }
            for (auto& WmoPair : Tile->SpawnedWmos)
            {
                if (WmoPair.Value)
                {
                    WmoPair.Value->Destroy();
                    --ActiveWmoGroupCount; // approximate; exact tracking not critical here
                    ++ObjectStreamingRevision;
                }
                SpawnedWmoIds.Remove(WmoPair.Key);
            }

            Tile->Destroy();
        }
        LoadedTiles.Remove(Key);
		bEntryTileUnloaded |= InitialTileKeys.Contains(Key);
    }
    if (bEntryTileUnloaded)
    {
		RefreshInitialTileProgress();
    }
}

bool AWowWorldManager::ResolveStreamingViewLocation(FVector& OutLocation) const
{
    if (bHasCinematicStreamingFocus)
    {
        OutLocation = CinematicStreamingFocus;
        return true;
    }
    if (bHasWorldEntryStreamingFocus)
    {
        OutLocation = WorldEntryStreamingFocus;
        return true;
    }

    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    if (!PC)
    {
        return false;
    }

    // Terrain must follow the possessed pawn. A stale or destroyed login camera
    // is allowed to affect presentation, but must never unload gameplay collision.
    if (const APawn* Pawn = PC->GetPawn())
    {
        OutLocation = Pawn->GetActorLocation();
        return true;
    }

    FRotator ViewRotation;
    PC->GetPlayerViewPoint(OutLocation, ViewRotation);
    return true;
}

void AWowWorldManager::UpdateWdlStreaming(const FIntPoint& CameraTile)
{
    if (!WdlData || !WdlData->bIsValid) return;

    // Load WDL only beyond the complete LOD1 ring. Previously WDL filled the
    // still-pending mid range first, only to be overlapped/replaced by LOD1 a
    // few frames later. Keep the batch deliberately small so runtime mesh
    // creation cannot monopolize the 100 ms streaming tick.
    int32 WdlSpawned = 0;
    constexpr int32 MaxWdlTilesPerTick = 2;
    for (int32 DX = -WdlRadius; DX <= WdlRadius && WdlSpawned < MaxWdlTilesPerTick; ++DX)
    {
        for (int32 DY = -WdlRadius; DY <= WdlRadius && WdlSpawned < MaxWdlTilesPerTick; ++DY)
        {
            const int32 Dist = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
            if (Dist <= Lod1Radius) continue;

            int32 TX = CameraTile.X + DX;
            int32 TY = CameraTile.Y + DY;
            if (TX < 0 || TX >= 64 || TY < 0 || TY >= 64) continue;

            // Skip tiles that have full-detail ADT loaded, pending, or LOD 1
            if (IsTileLoaded(TX, TY) || IsTilePending(TX, TY)) continue;
            if (Lod1Tiles.Contains(TileKey(TX, TY))) continue;

            // Skip tiles already loaded as WDL
            int64 Key = TileKey(TX, TY);
            if (WdlTiles.Contains(Key)) continue;

            // Must have WDL data for this tile
            if (!WdlData->HasTile(TX, TY)) continue;

            SpawnWdlTile(TX, TY);
            ++WdlSpawned;
        }
    }

    // Unload WDL tiles beyond unload radius or that now have full ADT loaded
    TArray<int64> ToUnload;
    for (auto& Pair : WdlTiles)
    {
        int32 TX = (int32)(Pair.Key >> 32);
        int32 TY = (int32)(Pair.Key & 0xFFFFFFFF);
        int32 Dist = FMath::Max(FMath::Abs(TX - CameraTile.X), FMath::Abs(TY - CameraTile.Y));

        // Unload if too far or if full-detail tile is now loaded
        if (Dist > WdlUnloadRadius || IsTileMeshBuilt(TX, TY) || Lod1Tiles.Contains(Pair.Key))
        {
            ToUnload.Add(Pair.Key);
        }
    }
    for (int64 Key : ToUnload)
    {
        TObjectPtr<AActor>* Found = WdlTiles.Find(Key);
        if (Found && *Found)
        {
            // Release the UStaticMesh from root before destroying actor
            (*Found)->Destroy();
        }
        WdlTiles.Remove(Key);
    }
}

void AWowWorldManager::SpawnWdlTile(int32 TX, int32 TY)
{
    if (!WdlData || !WdlData->HasTile(TX, TY)) return;

    const FWdlTileData& TileData = *WdlData->Tiles[TY][TX];

    // Build a 17x17 mesh from WDL heights
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector2D> UVs;

    Vertices.Reserve(17 * 17);
    UVs.Reserve(17 * 17);

    float StepSize = FWowCoordinate::TILE_SIZE / 16.0f;
    float TileNgX = TX * FWowCoordinate::TILE_SIZE;
    float TileNgZ = TY * FWowCoordinate::TILE_SIZE;

    for (int32 Row = 0; Row < 17; Row++)
    {
        for (int32 Col = 0; Col < 17; Col++)
        {
            float NgX = TileNgX + Col * StepSize;
            float NgZ = TileNgZ + Row * StepSize;
            float NgY = (float)TileData.Height17[Row][Col];
            Vertices.Add(FWowCoordinate::AdtToUE(NgX, NgY, NgZ));
            UVs.Add(FVector2D((float)Col / 16.0f, (float)Row / 16.0f));
        }
    }

    Indices.Reserve(16 * 16 * 6);
    for (int32 Row = 0; Row < 16; Row++)
    {
        for (int32 Col = 0; Col < 16; Col++)
        {
            if (TileData.IsHole(Col, Row)) continue;
            int32 TL = Row * 17 + Col;
            int32 TR = TL + 1;
            int32 BL = TL + 17;
            int32 BR = BL + 1;
            Indices.Add(TL); Indices.Add(BL); Indices.Add(TR);
            Indices.Add(TR); Indices.Add(BL); Indices.Add(BR);
        }
    }

    // Append transition strip geometry into the same vertex/index arrays
    int32 TransitionSectionCount = 0;
    const uint8 TransitionEdges = DetermineWdlTransitionEdges(WdtData.Get(), LastCameraTile, LoadRadius, Lod1Radius, TX, TY);
    if (TransitionEdges != 0)
    {
        FAdtData TransitionAdtData;
        if (LoadTileAdtForTransition(MpqManager.Get(), MapName, WdtData ? WdtData->bUseBigAlpha : false, TX, TY, TransitionAdtData))
        {
            TArray<float> EdgeHeights;
            if ((TransitionEdges & WdlTransition_North) != 0)
            {
                BuildNorthEdgeHeights(TransitionAdtData, EdgeHeights);
                BuildWdlTransitionGeometry(TileData, TX, TY, WdlTransition_North, EdgeHeights, Vertices, Indices, UVs);
                TransitionSectionCount++;
            }
            if ((TransitionEdges & WdlTransition_South) != 0)
            {
                BuildSouthEdgeHeights(TransitionAdtData, EdgeHeights);
                BuildWdlTransitionGeometry(TileData, TX, TY, WdlTransition_South, EdgeHeights, Vertices, Indices, UVs);
                TransitionSectionCount++;
            }
            if ((TransitionEdges & WdlTransition_West) != 0)
            {
                BuildWestEdgeHeights(TransitionAdtData, EdgeHeights);
                BuildWdlTransitionGeometry(TileData, TX, TY, WdlTransition_West, EdgeHeights, Vertices, Indices, UVs);
                TransitionSectionCount++;
            }
            if ((TransitionEdges & WdlTransition_East) != 0)
            {
                BuildEastEdgeHeights(TransitionAdtData, EdgeHeights);
                BuildWdlTransitionGeometry(TileData, TX, TY, WdlTransition_East, EdgeHeights, Vertices, Indices, UVs);
                TransitionSectionCount++;
            }
        }
    }

    if (Indices.Num() == 0) return;

    // Compute normals
    TArray<FVector> Normals;
    Normals.SetNumZeroed(Vertices.Num());
    for (int32 i = 0; i < Indices.Num(); i += 3)
    {
        const FVector& V0 = Vertices[Indices[i]];
        const FVector& V1 = Vertices[Indices[i + 1]];
        const FVector& V2 = Vertices[Indices[i + 2]];
        FVector FaceNormal = FVector::CrossProduct(V1 - V0, V2 - V0).GetSafeNormal();
        Normals[Indices[i]] += FaceNormal;
        Normals[Indices[i + 1]] += FaceNormal;
        Normals[Indices[i + 2]] += FaceNormal;
    }
    for (FVector& N : Normals) N = N.GetSafeNormal();

    // Build UStaticMesh from FMeshDescription
    UStaticMesh* SM = NewObject<UStaticMesh>();

    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attributes(MeshDesc);
    Attributes.Register();

    TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
    TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
    TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

    FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
    MeshDesc.ReserveNewVertices(Vertices.Num());
    MeshDesc.ReserveNewVertexInstances(Vertices.Num());
    MeshDesc.ReserveNewPolygons(Indices.Num() / 3);

    TArray<FVertexInstanceID> VertexInstanceIDs;
    VertexInstanceIDs.SetNum(Vertices.Num());

    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        const FVector& P = Vertices[i];
        FVertexID VertID = MeshDesc.CreateVertex();
        VertexPositions[VertID] = FVector3f(P.X, P.Y, P.Z);

        FVertexInstanceID InstID = MeshDesc.CreateVertexInstance(VertID);
        VertexInstanceIDs[i] = InstID;

        FVector3f N = (i < Normals.Num()) ? FVector3f(Normals[i]) : FVector3f(0, 0, 1);
        N.Normalize();
        VertexInstanceNormals[InstID] = N;

        FVector3f T = FVector3f::CrossProduct(N, FVector3f(0, 1, 0));
        if (T.SizeSquared() < 0.001f) T = FVector3f::CrossProduct(N, FVector3f(1, 0, 0));
        T.Normalize();
        VertexInstanceTangents[InstID] = T;
        VertexInstanceBinormalSigns[InstID] = 1.0f;

        FVector2f UV = (i < UVs.Num()) ? FVector2f(UVs[i]) : FVector2f(0, 0);
        VertexInstanceUVs.Set(InstID, 0, UV);
    }

    for (int32 i = 0; i < Indices.Num(); i += 3)
    {
        TArray<FVertexInstanceID> TriVerts;
        TriVerts.Add(VertexInstanceIDs[Indices[i]]);
        TriVerts.Add(VertexInstanceIDs[Indices[i + 1]]);
        TriVerts.Add(VertexInstanceIDs[Indices[i + 2]]);
        MeshDesc.CreatePolygon(PolyGroup, TriVerts);
    }

    TArray<const FMeshDescription*> MeshDescs;
    MeshDescs.Add(&MeshDesc);
    FStaticMaterial StaticMaterial;
    StaticMaterial.UVChannelData.bInitialized = true;
    SM->GetStaticMaterials().Add(MoveTemp(StaticMaterial));
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bBuildSimpleCollision = false;
    Params.bFastBuild = true;
    WowPrepareRuntimeMeshMaterials(SM, const_cast<FMeshDescription&>(*MeshDescs[0]));
    SM->BuildFromMeshDescriptions(MeshDescs, Params);
    SM->NeverStream = true;

    // Distant terrain uses the tile's minimap artwork; the engine default
    // material previously ignored the colour parameter entirely.
    if (UMaterialInterface* Mat = CreateDistantTerrainMaterial(TX, TY, SM, FLinearColor(0.3f, 0.4f, 0.2f, 1.0f)))
    {
        if (SM->GetStaticMaterials().Num() <= 0)
        {
            SM->GetStaticMaterials().SetNum(1);
        }
        SM->GetStaticMaterials()[0].MaterialInterface = Mat;
    }

    // Spawn actor with UStaticMeshComponent
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *FString::Printf(TEXT("WdlTile_%d_%d"), TX, TY);
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    AActor* WdlActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!WdlActor) { return; }

    UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(WdlActor, TEXT("WdlMesh"));
    MeshComp->SetStaticMesh(SM);
    MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComp->SetCastShadow(false);
    MeshComp->RegisterComponent();
    WdlActor->SetRootComponent(MeshComp);

    WdlTiles.Add(TileKey(TX, TY), WdlActor);

    if (TransitionSectionCount > 0)
    {
        UE_LOG(LogWowWorld, Log, TEXT("Spawned WDL tile %d,%d with %d transition strips (%d verts, UStaticMesh)"), TX, TY, TransitionSectionCount, Vertices.Num());
    }
    else
    {
        UE_LOG(LogWowWorld, Verbose, TEXT("Spawned WDL tile %d,%d (%d verts, UStaticMesh)"), TX, TY, Vertices.Num());
    }
}

UTexture2D* AWowWorldManager::LoadDistantTerrainTexture(int32 TX, int32 TY)
{
    if (!MpqManager) return nullptr;
    if (!MinimapTranslateMap.Equals(MapName, ESearchCase::IgnoreCase))
    {
        MinimapTranslate.Reset();
        MinimapTranslateMap = MapName;
        TArray<uint8> Bytes;
        if (MpqManager->ReadFile(TEXT("Textures\\Minimap\\md5translate.trs"), Bytes))
        {
            FString Text;
            FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num());
            TArray<FString> Lines;
            Text.ParseIntoArrayLines(Lines);
            const FString Prefix = MapName.ToLower() + TEXT("\\");
            for (const FString& Line : Lines)
            {
                FString Key, Value;
                if (!Line.Split(TEXT("\t"), &Key, &Value)) continue;
                Key.TrimStartAndEndInline();
                Value.TrimStartAndEndInline();
                Key = Key.ToLower();
                if (Key.StartsWith(Prefix)) MinimapTranslate.Add(Key, Value);
            }
        }
    }
    const FString Key = FString::Printf(TEXT("%s\\map%d_%d.blp"), *MapName, TX, TY).ToLower();
    const FString* Hashed = MinimapTranslate.Find(Key);
    if (!Hashed) return nullptr;
    UTexture2D* Texture = FWowTerrainMaterial::LoadBlpTexture(TEXT("Textures\\Minimap\\") + *Hashed, MpqManager.Get(), AssetCache.Get());
    if (Texture)
    {
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
    }
    return Texture;
}

UMaterialInterface* AWowWorldManager::CreateDistantTerrainMaterial(int32 TX, int32 TY, UObject* Outer, const FLinearColor& Fallback)
{
    if (UTexture2D* Texture = LoadDistantTerrainTexture(TX, TY))
    {
        if (UMaterial* Base = FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial(false))
        {
            if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer))
            {
                MID->SetTextureParameterValue(TEXT("Layer0Texture"), Texture);
                return MID;
            }
        }
    }
    UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    UMaterialInstanceDynamic* Solid = BaseMaterial ? UMaterialInstanceDynamic::Create(BaseMaterial, Outer) : nullptr;
    if (Solid) Solid->SetVectorParameterValue(TEXT("Color"), Fallback);
    return Solid ? static_cast<UMaterialInterface*>(Solid) : BaseMaterial;
}

void AWowWorldManager::UpdateLod1Streaming(const FIntPoint& CameraTile)
{
    if (!WdtData || !WdtData->bIsValid || !MpqManager) return;

    // Load LOD 1 tiles in mid-range (beyond full-detail, within Lod1Radius).
    // Water reconstruction and runtime mesh creation are game-thread work, so
    // finalize no more than one mid-distance tile per tick to avoid visible stalls.
    // r649: collect the worker results (results of an older map are dropped).
    for (auto It = WowLod1Async::InFlight.CreateIterator(); It; ++It)
    {
        if (!It.Value().Future.IsReady()) continue;
        TSharedPtr<FAdtData> Parsed = It.Value().Future.Get();
        if (Parsed && It.Value().Generation == MapGeneration) WowLod1Async::Ready.Add(It.Key(), Parsed);
        It.RemoveCurrent();
    }
    int32 Lod1Spawned = 0;
    for (int32 DX = -Lod1Radius; DX <= Lod1Radius; ++DX)
    {
        for (int32 DY = -Lod1Radius; DY <= Lod1Radius; ++DY)
        {
            int32 Dist = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
            // Only LOD 1 range: beyond LoadRadius, within Lod1Radius
            if (Dist <= LoadRadius) continue;

            int32 TX = CameraTile.X + DX;
            int32 TY = CameraTile.Y + DY;
            if (TX < 0 || TX >= 64 || TY < 0 || TY >= 64) continue;
            if (!WdtData->TileExists[TX][TY]) continue;

            // Skip if full ADT loaded or pending
            if (IsTileLoaded(TX, TY) || IsTilePending(TX, TY)) continue;

            int64 Key = TileKey(TX, TY);
            if (Lod1Tiles.Contains(Key)) continue;

            // r649: parsed on a worker first; then one tile mesh per tick as before.
            if (WowLod1Async::Ready.Contains(Key))
            {
                if (Lod1Spawned >= 1) continue;
                SpawnLod1Tile(TX, TY);
                ++Lod1Spawned;
                continue;
            }
            if (WowLod1Async::InFlight.Contains(Key) || WowLod1Async::InFlight.Num() >= WowLod1Async::MaxInFlight) continue;
            {
                FMpqManager* Mpq = MpqManager.Get();
                const FString AdtPath = FString::Printf(TEXT("World\\Maps\\%s\\%s_%d_%d.adt"), *MapName, *MapName, TX, TY);
                const bool bBigAlpha = WdtData->bUseBigAlpha;
                WowLod1Async::FParse& Parse = WowLod1Async::InFlight.Add(Key);
                Parse.Generation = MapGeneration;
                Parse.Future = Async(EAsyncExecution::ThreadPool, [Mpq, AdtPath, bBigAlpha]() -> TSharedPtr<FAdtData>
                {
                    FWowPerformanceScope Timing(TEXT("lod1-read-parse-async"), AdtPath, 1000.0);
                    TArray<uint8> AdtRaw;
                    if (!Mpq || !Mpq->ReadFile(AdtPath, AdtRaw)) return nullptr;
                    TSharedPtr<FAdtData> Result = MakeShared<FAdtData>(FAdtParser::Parse(AdtRaw, bBigAlpha));
                    return Result->bIsValid ? Result : nullptr;
                });
            }
        }
    }

    // Unload LOD 1 tiles that are too far or superseded by full ADT
    TArray<int64> ToUnload;
    for (auto& Pair : Lod1Tiles)
    {
        int32 TX = (int32)(Pair.Key >> 32);
        int32 TY = (int32)(Pair.Key & 0xFFFFFFFF);
        int32 Dist = FMath::Max(FMath::Abs(TX - CameraTile.X), FMath::Abs(TY - CameraTile.Y));

        if (Dist > Lod1Radius + 1 || IsTileMeshBuilt(TX, TY))
        {
            ToUnload.Add(Pair.Key);
        }
    }
    // r649: parsed tiles that are no longer wanted.
    for (auto It = WowLod1Async::Ready.CreateIterator(); It; ++It)
    {
        const int32 TX = (int32)(It.Key() >> 32), TY = (int32)(It.Key() & 0xFFFFFFFF);
        if (FMath::Max(FMath::Abs(TX - CameraTile.X), FMath::Abs(TY - CameraTile.Y)) > Lod1Radius + 1
            || IsTileLoaded(TX, TY) || IsTilePending(TX, TY) || Lod1Tiles.Contains(It.Key()))
            It.RemoveCurrent();
    }
    for (int64 Key : ToUnload)
    {
        TObjectPtr<AActor>* Found = Lod1Tiles.Find(Key);
        if (Found && *Found) (*Found)->Destroy();
        Lod1Tiles.Remove(Key);
    }
}

void AWowWorldManager::SpawnLod1Tile(int32 TX, int32 TY)
{
    // r649: parsed on a worker (UpdateLod1Streaming); read here only if it was not.
    TSharedPtr<FAdtData> Parsed;
    WowLod1Async::Ready.RemoveAndCopyValue(TileKey(TX, TY), Parsed);
    if (!Parsed)
    {
        FString AdtPath = FString::Printf(TEXT("World\\Maps\\%s\\%s_%d_%d.adt"), *MapName, *MapName, TX, TY);
        TArray<uint8> AdtRaw;
        if (!MpqManager->ReadFile(AdtPath, AdtRaw))
        {
            return;
        }
        bool bBigAlpha = WdtData ? WdtData->bUseBigAlpha : false;
        Parsed = MakeShared<FAdtData>(FAdtParser::Parse(AdtRaw, bBigAlpha));
    }
    if (!Parsed || !Parsed->bIsValid) return;
    FAdtData& AdtData = *Parsed;

    FVector TileCenter = FWowCoordinate::TileToWorld(TX, TY);

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *FString::Printf(TEXT("Lod1Tile_%d_%d"), TX, TY);
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    AActor* Lod1Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), TileCenter, FRotator::ZeroRotator, SpawnParams);
    if (!Lod1Actor) return;

    USceneComponent* Root = NewObject<USceneComponent>(Lod1Actor, TEXT("Root"));
    Lod1Actor->AddInstanceComponent(Root);
    Lod1Actor->SetRootComponent(Root);
    // Plain AActor has no native root during SpawnActor, so its requested
    // spawn transform cannot position this component created afterwards.
    // Vertices below are tile-local: restore the tile center before attaching
    // the mesh, otherwise every distant tile overlaps at the map origin.
    Root->SetWorldLocation(TileCenter);
    Root->RegisterComponent();

    // LOD1 is several kilometres from the camera. Building 256 individual
    // chunk meshes, loading their BLP textures and reconstructing full MH2O
    // water made a single streaming tick take 60-100 ms. Sample the authored
    // ADT height field into one 33x33 grid instead: it preserves the terrain
    // silhouette at this distance with roughly 95% fewer vertices and one
    // shared solid material. Full terrain and water remain in the near ring.
    constexpr int32 SamplesPerChunk = 2;
    constexpr int32 GridSize = 16 * SamplesPerChunk + 1;
    constexpr float GridDenominator = static_cast<float>(GridSize - 1);

    FMeshDescription MeshDesc;
    MeshDesc.SetNumUVChannels(1);
    FStaticMeshAttributes SMAttributes(MeshDesc);
    SMAttributes.Register();

    TVertexAttributesRef<FVector3f> VertexPositions = SMAttributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = SMAttributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = SMAttributes.GetVertexInstanceTangents();
    TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = SMAttributes.GetVertexInstanceBinormalSigns();
    TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = SMAttributes.GetVertexInstanceUVs();

    const FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
    TArray<FVertexInstanceID> VertexInstances;
    VertexInstances.SetNum(GridSize * GridSize);
    const float TileBaseX = TX * FWowCoordinate::TILE_SIZE;
    const float TileBaseZ = TY * FWowCoordinate::TILE_SIZE;
    const float TileCenterX = TileBaseX + FWowCoordinate::TILE_SIZE * 0.5f;
    const float TileCenterZ = TileBaseZ + FWowCoordinate::TILE_SIZE * 0.5f;

    for (int32 GridY = 0; GridY < GridSize; ++GridY)
    {
        const int32 ChunkY = FMath::Min(GridY / SamplesPerChunk, 15);
        const int32 LocalOuterY = GridY == GridSize - 1
            ? 8 : (GridY % SamplesPerChunk) * 4;
        for (int32 GridX = 0; GridX < GridSize; ++GridX)
        {
            const int32 ChunkX = FMath::Min(GridX / SamplesPerChunk, 15);
            const int32 LocalOuterX = GridX == GridSize - 1
                ? 8 : (GridX % SamplesPerChunk) * 4;
            const FAdtChunkData& Chunk = AdtData.Chunks[ChunkY * 16 + ChunkX];
            const int32 HeightIndex = LocalOuterY * 17 + LocalOuterX;
            const float AdtX = TileBaseX + (GridX / GridDenominator) * FWowCoordinate::TILE_SIZE;
            const float AdtZ = TileBaseZ + (GridY / GridDenominator) * FWowCoordinate::TILE_SIZE;
            const float AdtY = Chunk.WorldZ + Chunk.Heights[HeightIndex];

            const FVertexID VertexId = MeshDesc.CreateVertex();
            VertexPositions[VertexId] = FVector3f(
                -(AdtZ - TileCenterZ) * FWowCoordinate::SCALE,
                (AdtX - TileCenterX) * FWowCoordinate::SCALE,
                AdtY * FWowCoordinate::SCALE);

            const FVertexInstanceID InstanceId = MeshDesc.CreateVertexInstance(VertexId);
            VertexInstances[GridY * GridSize + GridX] = InstanceId;
            const FVector& WowNormal = Chunk.Normals[HeightIndex];
            FVector3f Normal(-WowNormal.Z, WowNormal.X, WowNormal.Y);
            Normal.Normalize();
            VertexInstanceNormals[InstanceId] = Normal;
            FVector3f Tangent = FVector3f::CrossProduct(Normal, FVector3f(0, 1, 0));
            if (Tangent.SizeSquared() < 0.001f)
            {
                Tangent = FVector3f::CrossProduct(Normal, FVector3f(1, 0, 0));
            }
            Tangent.Normalize();
            VertexInstanceTangents[InstanceId] = Tangent;
            VertexInstanceBinormalSigns[InstanceId] = 1.0f;
            VertexInstanceUVs.Set(InstanceId, 0,
                FVector2f(GridX / GridDenominator, GridY / GridDenominator));
        }
    }

    for (int32 GridY = 0; GridY < GridSize - 1; ++GridY)
    {
        for (int32 GridX = 0; GridX < GridSize - 1; ++GridX)
        {
            // r417 (Stefan: "Orgrimmar is fubar" - huge flat dark walls and a lid over the city):
            // the city sits in terrain holes (MCNK holes, filled by the Orgrimmar WMO), but this
            // lightweight tile closed every hole with flat planes at whatever height the hole's
            // vertices have. A cell covers 4x4 outer quads = 2x2 of the chunk's 4x4 hole bits;
            // leave it out when any of them is a hole.
            {
                const FAdtChunkData& CellChunk = AdtData.Chunks[FMath::Min(GridY / SamplesPerChunk, 15) * 16 + FMath::Min(GridX / SamplesPerChunk, 15)];
                const int32 HoleX = (GridX % SamplesPerChunk) * 2, HoleY = (GridY % SamplesPerChunk) * 2;
                bool bHole = false;
                for (int32 DY = 0; DY < 2 && !bHole; ++DY)
                    for (int32 DX = 0; DX < 2 && !bHole; ++DX)
                        bHole = (CellChunk.Holes & (1u << ((HoleY + DY) * 4 + HoleX + DX))) != 0;
                if (bHole) continue;
            }
            const int32 TL = GridY * GridSize + GridX;
            const int32 TR = TL + 1;
            const int32 BL = TL + GridSize;
            const int32 BR = BL + 1;
            const TArray<FVertexInstanceID> FirstTriangle = {
                VertexInstances[TL], VertexInstances[BL], VertexInstances[TR]};
            const TArray<FVertexInstanceID> SecondTriangle = {
                VertexInstances[TR], VertexInstances[BL], VertexInstances[BR]};
            MeshDesc.CreatePolygon(PolyGroup, FirstTriangle);
            MeshDesc.CreatePolygon(PolyGroup, SecondTriangle);
        }
    }

    // Colour the distant silhouette with the tile's own minimap artwork (one
    // small texture per tile) instead of a uniform green.
    UMaterialInterface* DistantMaterial = CreateDistantTerrainMaterial(TX, TY, Lod1Actor,
        FLinearColor(0.24f, 0.32f, 0.14f, 1.0f));

    UStaticMesh* SM = NewObject<UStaticMesh>();
    FStaticMaterial StaticMaterial(DistantMaterial);
    StaticMaterial.UVChannelData.bInitialized = true;
    SM->GetStaticMaterials().Add(MoveTemp(StaticMaterial));
    TArray<const FMeshDescription*> MeshDescs;
    MeshDescs.Add(&MeshDesc);
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bBuildSimpleCollision = false;
    Params.bFastBuild = true;
    Params.bCommitMeshDescription = false;
    WowPrepareRuntimeMeshMaterials(SM, const_cast<FMeshDescription&>(*MeshDescs[0]));
    SM->BuildFromMeshDescriptions(MeshDescs, Params);
    SM->NeverStream = true;

    UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(Lod1Actor, TEXT("Lod1Mesh"));
    MeshComp->SetStaticMesh(SM);
    MeshComp->SetupAttachment(Root);
    MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComp->SetCastShadow(false);
    if (DistantMaterial)
    {
        MeshComp->SetMaterial(0, DistantMaterial);
    }
    MeshComp->RegisterComponent();

    Lod1Tiles.Add(TileKey(TX, TY), Lod1Actor);
    UE_LOG(LogWowWorld, Verbose,
        TEXT("Spawned lightweight LOD 1 tile %d,%d (%d vertices, one solid material)"),
        TX, TY, GridSize * GridSize);
}

float AWowWorldManager::GetTerrainIceAt(const FVector& Location) const
{
    const FIntPoint Tile = FWowCoordinate::WorldToTile(Location);
    const TObjectPtr<AWowTerrainTile>* TilePtr = LoadedTiles.Find(TileKey(Tile.X, Tile.Y));
    if (!TilePtr || !*TilePtr || (*TilePtr)->IceCells.Num() != 256 * 64) return 0.f;
    // Same chunk maths as UpdateZoneAudio, one level finer (8x8 cells per chunk).
    const double NgX = Location.Y / FWowCoordinate::SCALE + FWowCoordinate::ADT_CENTER_X;
    const double NgZ = -Location.X / FWowCoordinate::SCALE + FWowCoordinate::ADT_CENTER_Z;
    const double LocalX = NgX - Tile.X * FWowCoordinate::TILE_SIZE;
    const double LocalZ = NgZ - Tile.Y * FWowCoordinate::TILE_SIZE;
    const int32 CellX = FMath::Clamp(FMath::FloorToInt32(LocalX / (FWowCoordinate::CHUNK_SIZE / 8.0)), 0, 127);
    const int32 CellY = FMath::Clamp(FMath::FloorToInt32(LocalZ / (FWowCoordinate::CHUNK_SIZE / 8.0)), 0, 127);
    const int32 Index = ((CellY / 8) * 16 + CellX / 8) * 64 + (CellY % 8) * 8 + CellX % 8;
    return (*TilePtr)->IceCells[Index] / 255.f;
}

void AWowWorldManager::UpdateZoneAudio()
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC) return;

    FVector CamLoc;
    if (const APawn* Pawn = PC->GetPawn()) CamLoc = Pawn->GetActorLocation();
    else
    {
        FRotator CamRot;
        PC->GetPlayerViewPoint(CamLoc, CamRot);
    }

    // Area follows the player, not the orbit camera across a zone boundary.
    FIntPoint CameraTile = FWowCoordinate::WorldToTile(CamLoc);
    int64 Key = TileKey(CameraTile.X, CameraTile.Y);

    TObjectPtr<AWowTerrainTile>* TilePtr = LoadedTiles.Find(Key);
    if (!TilePtr || !*TilePtr) return;

    AWowTerrainTile* Tile = *TilePtr;
    if (Tile->ChunkAreaIds.Num() != 256) return;

    // Calculate which chunk within the tile (0-15 in each axis)
    double NgX = CamLoc.Y / FWowCoordinate::SCALE + FWowCoordinate::ADT_CENTER_X;
    double NgZ = -CamLoc.X / FWowCoordinate::SCALE + FWowCoordinate::ADT_CENTER_Z;
    double LocalX = NgX - CameraTile.X * FWowCoordinate::TILE_SIZE;
    double LocalZ = NgZ - CameraTile.Y * FWowCoordinate::TILE_SIZE;

    int32 ChunkX = FMath::Clamp(FMath::FloorToInt32(LocalX / FWowCoordinate::CHUNK_SIZE), 0, 15);
    int32 ChunkY = FMath::Clamp(FMath::FloorToInt32(LocalZ / FWowCoordinate::CHUNK_SIZE), 0, 15);

    uint32 AreaId = Tile->ChunkAreaIds[ChunkY * 16 + ChunkX];
    if (AreaId == 0 || AreaId == CurrentAudioAreaId) return;

    CurrentAudioAreaId = AreaId;

    // Look up the zone (parent area) from AreaTable.dbc
    const FAreaTableDbcEntry* AreaEntry = FDbcStore::Get().AreaTable().GetById(AreaId);
    uint32 ZoneId = AreaEntry ? AreaEntry->ParentAreaID : 0;
    if (ZoneId == 0) ZoneId = AreaId; // Top-level zone

    if (AWowSkyManager* Sky=Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(this,AWowSkyManager::StaticClass())))
    {
        const FAreaTableDbcEntry* ZoneEntry=FDbcStore::Get().AreaTable().GetById(ZoneId);
        Sky->SetWeatherZone(ZoneId,ZoneEntry?ZoneEntry->Name:FString());
    }

    // Find audio manager and notify zone change
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWowAudioManager::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        AWowAudioManager* AudioMgr = Cast<AWowAudioManager>(Found[0]);
        if (AudioMgr)
        {
            AudioMgr->SetCurrentZone(ZoneId, AreaId);
        }
    }
}

void AWowWorldManager::DestroyGlobalWmo()
{
    if (GlobalWmoActor) GlobalWmoActor->Destroy();
    GlobalWmoActor = nullptr;
    bGlobalWmoTried = false;
    bGlobalWmoFailed = false;
}

// r556 (Stefan: dungeon portals should take us into the dungeon): most dungeons are WMO-only maps -
// the WDT has no terrain tiles, only one global WMO (MWMO + MODF). It was never spawned, so a
// transfer into the Stockade loaded nothing and the loading screen waited for terrain forever.
// The global WMO is spawned whole, once per map selection, without the distance and group limits.
void AWowWorldManager::UpdateGlobalWmo()
{
    if (bGlobalWmoTried || !IsWmoOnlyMap() || !MpqManager || !AssetCache) return;
    bGlobalWmoTried = true;
    FAdtWmoPlacement Placement = WdtData->GlobalWmoPlacement;
    // A zero position means "at the map origin" (as the server's vmap extractor reads it): in
    // placement space that is the map centre, so the WMO's own coordinates are world coordinates.
    if (!WdtData->bHasGlobalWmoPlacement || Placement.Position.IsNearlyZero())
        Placement.Position = FVector(FWowCoordinate::MAP_ORIGIN, Placement.Position.Y, FWowCoordinate::MAP_ORIGIN);
    if (Placement.UniqueId == 0 || Placement.UniqueId == MAX_uint32) Placement.UniqueId = 0x7FFFFFF0u;
    GlobalWmoActor = FWowWmoRenderer::SpawnWmo(GetWorld(), WdtData->GlobalWmoPath, Placement, MpqManager.Get(), AssetCache.Get());
    bGlobalWmoFailed = !GlobalWmoActor;
    ++ObjectStreamingRevision;
    UE_LOG(LogWowWorld, Log, TEXT("Global WMO %s for map %s: %s (placement %s rot %s set %u)"),
        GlobalWmoActor ? TEXT("spawned") : TEXT("FAILED"), *MapName, *WdtData->GlobalWmoPath,
        *Placement.Position.ToString(), *Placement.Rotation.ToString(), Placement.DoodadSet);
}

void AWowWorldManager::UpdateObjectStreaming()
{
    UpdateGlobalWmo();
    FVector CamLoc;
    if (!ResolveStreamingViewLocation(CamLoc)) return;

    // Runtime mesh/material creation is still game-thread work. Keep it bounded so
    // the loading screen remains responsive and background streaming stays smooth.
    constexpr int32 MaxDoodadSpawnsPerTick = 8;
    constexpr int32 MaxWmoSpawnsPerTick = 1;
    int32 DoodadSpawnsThisTick = 0;
    int32 WmoSpawnsThisTick = 0;

    const float DoodadRadiusSq = DoodadRadius * DoodadRadius;
    const float DoodadDespawnRadiusSq = (DoodadRadius * 1.2f) * (DoodadRadius * 1.2f);
    const float WmoRadiusSq = WmoRadius * WmoRadius;
    const float WmoDespawnRadiusSq = (WmoRadius * 1.2f) * (WmoRadius * 1.2f);
    // WMO range is measured to the nearest edge of its placement bounds (r245, Stefan): measured
    // to the origin, Stormwind's walls and gate (hundreds of metres from the city's origin) were
    // missing from Elwynn and popped in close to the gate, leaving the sky in the gap.
    auto WmoDistanceSq = [&CamLoc](const FAdtWmoPlacement& Placement)
    {
        if (Placement.BoundingBox.IsValid)
        {
            FBox Box(ForceInit);
            Box += FWowCoordinate::AdtToUE(Placement.BoundingBox.Min.X, Placement.BoundingBox.Min.Y, Placement.BoundingBox.Min.Z);
            Box += FWowCoordinate::AdtToUE(Placement.BoundingBox.Max.X, Placement.BoundingBox.Max.Y, Placement.BoundingBox.Max.Z);
            // Ignore broken bounds (more than 4 km across): fall back to the origin.
            if (Box.GetSize().GetMax() < 400000.0)
                return float(Box.ComputeSquaredDistanceToPoint(CamLoc));
        }
        return float(FVector::DistSquared(CamLoc, FWowCoordinate::AdtToUE(
            Placement.Position.X, Placement.Position.Y, Placement.Position.Z)));
    };

    // Keep diagnostics useful without flooding the log during initial streaming.
    static double LastObjectStreamingLogSeconds = -5.0;
    const double NowSeconds = FPlatformTime::Seconds();
    const bool bDebugLog = NowSeconds - LastObjectStreamingLogSeconds >= 5.0;
    if (bDebugLog)
    {
        LastObjectStreamingLogSeconds = NowSeconds;
        int32 TotalDoodads = 0, TotalWmos = 0;
        for (auto& TP : LoadedTiles)
        {
            if (TP.Value) { TotalDoodads += TP.Value->DoodadPlacements.Num(); TotalWmos += TP.Value->WmoPlacements.Num(); }
        }
        UE_LOG(LogWowWorld, Log, TEXT("ObjectStreaming: cam=%s, %d tiles, %d total doodads, %d total wmos, active: %d doodads, %d wmo groups"),
            *CamLoc.ToString(), LoadedTiles.Num(), TotalDoodads, TotalWmos, ActiveDoodadCount, ActiveWmoGroupCount);
    }
    // r441 (Stefan: Orgrimmar still has huge flat dark planes, the sky faintly showing through
    // them): name every large visible primitive near the camera once per 60 m moved (at most every
    // 20 s), so the next log says which actor/material draws those planes.
    {
        static FVector LastScan(1e12);
        static double LastScanSeconds = -100.0;
        if (bDebugLog && LoadedTiles.Num() > 0 && NowSeconds - LastScanSeconds >= 15.0 && (FVector::Dist(LastScan, CamLoc) > 1000.0 || NowSeconds - LastScanSeconds >= 60.0)) // r443: the first scan ran on an empty world and short walks never re-ran it
        {
            LastScan = CamLoc; LastScanSeconds = NowSeconds;
            int32 Logged = 0;
            for (TActorIterator<AActor> It(GetWorld()); It && Logged < 60; ++It)
            {
                AActor* A = *It;
                if (!A || A->IsHidden()) continue;
                TInlineComponentArray<UPrimitiveComponent*> Prims(A);
                for (UPrimitiveComponent* P : Prims)
                {
                    if (!P || !P->IsRegistered() || !P->ShouldRender()) continue;
                    const FBoxSphereBounds& B = P->Bounds;
                    if (B.SphereRadius < 2500.0 || B.SphereRadius > 400000.0) continue;
                    if (FVector::Dist(B.Origin, CamLoc) - B.SphereRadius > 20000.0) continue;
                    UMaterialInterface* M = P->GetNumMaterials() > 0 ? P->GetMaterial(0) : nullptr;
                    UE_LOG(LogWowWorld, Log, TEXT("NearPrimitive: %s.%s (%s) mat=%s base=%s blend=%d sections=%d origin=%s extent=%s"),
                        *A->GetName(), *P->GetName(), *P->GetClass()->GetName(), *GetNameSafe(M),
                        M && M->GetMaterial() ? *M->GetMaterial()->GetName() : TEXT("-"), M ? int32(M->GetBlendMode()) : -1,
                        P->GetNumMaterials(), *B.Origin.ToCompactString(), *B.BoxExtent.ToCompactString());
                    if (++Logged >= 60) break;
                }
            }
        }
    }

    // Iterate all loaded tiles
    for (auto& TilePair : LoadedTiles)
    {
        AWowTerrainTile* Tile = TilePair.Value;
        if (!Tile || !Tile->CachedMpq) continue;

        // --- DOODADS: Skip per-instance streaming if tile uses HISMC instancing ---
        // HISMC handles culling automatically via built-in LOD and cull distances
        if (!Tile->bUsesInstancedDoodads)
        {
            // Legacy per-instance doodad streaming (fallback)
            // --- DOODADS: despawn out-of-range ---
            {
                TArray<uint32> ToDespawn;
                for (auto& Pair : Tile->SpawnedDoodads)
                {
                    if (!Pair.Value) { ToDespawn.Add(Pair.Key); continue; }
                    float DistSq = FVector::DistSquared(CamLoc, Pair.Value->GetComponentLocation());
                    if (DistSq > DoodadDespawnRadiusSq)
                    {
                        ToDespawn.Add(Pair.Key);
                    }
                }
                for (uint32 Id : ToDespawn)
                {
                    TObjectPtr<UStaticMeshComponent>* Comp = Tile->SpawnedDoodads.Find(Id);
                    if (Comp && *Comp)
                    {
                        (*Comp)->DestroyComponent();
                        --ActiveDoodadCount;
                        ++ObjectStreamingRevision;
                    }
                    Tile->SpawnedDoodads.Remove(Id);
                    SpawnedDoodadIds.Remove(Id);
                }
            }

            // --- DOODADS: spawn in-range ---
            for (const FAdtDoodadPlacement& Placement : Tile->DoodadPlacements)
            {
                if (ActiveDoodadCount >= MaxActiveDoodads) break;
                if (DoodadSpawnsThisTick >= MaxDoodadSpawnsPerTick) break;

                // Already spawned globally (could be on this tile or another)
                if (SpawnedDoodadIds.Contains(Placement.UniqueId)) continue;

                // Resolve path
                if (Placement.NameIndex < 0 || Placement.NameIndex >= Tile->DoodadPaths.Num()) continue;
                const FString& M2Path = Tile->DoodadPaths[Placement.NameIndex];
                if (M2Path.IsEmpty()) continue;

                // Distance check: convert MDDF to ADT space then UE (matches terrain)
                FVector UEPos = FWowCoordinate::AdtToUE(
                    Placement.Position.X,
                    Placement.Position.Y,
                    Placement.Position.Z);
                float DistSq = FVector::DistSquared(CamLoc, UEPos);
                if (DistSq > DoodadRadiusSq) continue;

                // Spawn it
                UStaticMeshComponent* Comp = FWowDoodadManager::SpawnSingleDoodad(
                    Tile, Placement, M2Path, Tile->CachedMpq, Tile->CachedCache);
                if (Comp)
                {
                    Tile->SpawnedDoodads.Add(Placement.UniqueId, Comp);
                    SpawnedDoodadIds.Add(Placement.UniqueId);
                    ++ActiveDoodadCount;
                    ++DoodadSpawnsThisTick;
                    ++ObjectStreamingRevision;
                }
            }
        }

        // --- WMOs: despawn out-of-range ---
        {
            TArray<uint32> ToDespawn;
            for (auto& Pair : Tile->SpawnedWmos)
            {
                if (!Pair.Value) { ToDespawn.Add(Pair.Key); continue; }
                float DistSq = FVector::DistSquared(CamLoc, Pair.Value->GetActorLocation());
                for (const FAdtWmoPlacement& Placed : Tile->WmoPlacements)
                    if (Placed.UniqueId == Pair.Key) { DistSq = WmoDistanceSq(Placed); break; }
                if (DistSq > WmoDespawnRadiusSq)
                {
                    ToDespawn.Add(Pair.Key);
                }
            }
            for (uint32 Id : ToDespawn)
            {
                TObjectPtr<AActor>* Act = Tile->SpawnedWmos.Find(Id);
                if (Act && *Act)
                {
                    (*Act)->Destroy();
                    --ActiveWmoGroupCount; // approximate
                    ++ObjectStreamingRevision;
                }
                Tile->SpawnedWmos.Remove(Id);
                SpawnedWmoIds.Remove(Id);
            }
        }

        // --- WMOs: spawn in-range ---
        for (const FAdtWmoPlacement& Placement : Tile->WmoPlacements)
        {
            if (ActiveWmoGroupCount >= MaxActiveWmoGroups) break;
            if (WmoSpawnsThisTick >= MaxWmoSpawnsPerTick) break;

            // Already spawned globally
            if (SpawnedWmoIds.Contains(Placement.UniqueId)) continue;

            // Resolve path
            if (Placement.NameIndex < 0 || Placement.NameIndex >= Tile->WmoPaths.Num()) continue;
            const FString& WmoPath = Tile->WmoPaths[Placement.NameIndex];
            if (WmoPath.IsEmpty()) continue;

            // Distance check: convert MODF to ADT space then UE (matches terrain)
            float DistSq = WmoDistanceSq(Placement);
            if (DistSq > WmoRadiusSq) continue;

            // Check group count - skip huge WMOs (e.g. Stormwind)
            uint32 GroupCount = FWowWmoRenderer::GetWmoGroupCount(WmoPath, Tile->CachedMpq);
            if (GroupCount == 0) continue;
            if ((int32)GroupCount > MaxWmoGroupsPerObject)
            {
                UE_LOG(LogWowWorld, Verbose, TEXT("Skipping large WMO %s (%d groups > max %d)"),
                    *WmoPath, GroupCount, MaxWmoGroupsPerObject);
                continue;
            }

            // Check if adding this WMO would exceed the group limit
            if (ActiveWmoGroupCount + (int32)GroupCount > MaxActiveWmoGroups) continue;

            // Spawn it
            AActor* WmoActor = FWowWmoRenderer::SpawnWmo(
                GetWorld(), WmoPath, Placement, Tile->CachedMpq, Tile->CachedCache);
            if (WmoActor)
            {
                WmoActor->AttachToActor(Tile, FAttachmentTransformRules::KeepWorldTransform);
                Tile->SpawnedWmos.Add(Placement.UniqueId, WmoActor);
                SpawnedWmoIds.Add(Placement.UniqueId);
                ActiveWmoGroupCount += (int32)GroupCount;
                ++WmoSpawnsThisTick;
                ++ObjectStreamingRevision;
            }
        }
    }

    // A large WMO still being built over several frames counts as streaming activity, so the
    // loading screen waits for it (r246).
    if (FWowWmoRenderer::HasPendingBuilds()) ++ObjectStreamingRevision;
    ++ObjectStreamingPassCount;
}

void AWowWorldManager::SetupRuntimeVirtualTexture()
{
    // Create the Runtime Virtual Texture object
    // Uses defaults: BaseColor_Normal_Specular, TileCount=8 (256), TileSize=2 (256px)
    TerrainRVT = NewObject<URuntimeVirtualTexture>(this, TEXT("TerrainRVT"));
    if (!TerrainRVT)
    {
        UE_LOG(LogWowWorld, Warning, TEXT("Failed to create Runtime Virtual Texture"));
        return;
    }

    // Create the RVT volume component that defines world bounds
    RVTVolumeComponent = NewObject<URuntimeVirtualTextureComponent>(this, TEXT("RVTVolume"));
    if (RVTVolumeComponent)
    {
        RVTVolumeComponent->SetupAttachment(GetRootComponent());
        RVTVolumeComponent->RegisterComponent();
        RVTVolumeComponent->SetVirtualTexture(TerrainRVT);

        // Set initial bounds covering loaded terrain area
        // Scale defines the volume extents - RVT bounds come from CalcBounds using transform
        float TileSizeUE = FWowCoordinate::TILE_SIZE * 100.0f; // ADT units to UE cm
        float CoverageHalfSize = (LoadRadius + 1) * TileSizeUE;
        FVector TileCenter = FWowCoordinate::TileToWorld(DebugTileX, DebugTileY);

        RVTVolumeComponent->SetWorldLocation(TileCenter);
        RVTVolumeComponent->SetWorldScale3D(FVector(CoverageHalfSize * 2.0f, CoverageHalfSize * 2.0f, 100000.0f));

        UE_LOG(LogWowWorld, Log, TEXT("RVT volume at %s, coverage %.0f cm"), *TileCenter.ToString(), CoverageHalfSize);
    }

    UE_LOG(LogWowWorld, Log, TEXT("Runtime Virtual Texture created for terrain"));
}

void AWowWorldManager::UpdateRVTBounds(const FIntPoint& CameraTile)
{
    if (!RVTVolumeComponent) return;

    // Recenter RVT volume on the camera tile
    FVector TileCenter = FWowCoordinate::TileToWorld(CameraTile.X, CameraTile.Y);
    RVTVolumeComponent->SetWorldLocation(FVector(TileCenter.X, TileCenter.Y, 0.0f));
}

uint32 AWowWorldManager::GetCurrentZoneId() const
{
    const auto* Area=FDbcStore::Get().AreaTable().GetById(CurrentAudioAreaId);
    return Area && Area->ParentAreaID ? Area->ParentAreaID : CurrentAudioAreaId;
}
FString AWowWorldManager::GetCurrentAreaName() const
{
    const auto* Area=FDbcStore::Get().AreaTable().GetById(CurrentAudioAreaId);
    return Area?Area->Name:FString();
}
FString AWowWorldManager::GetCurrentZoneName() const
{
    const auto* Area=FDbcStore::Get().AreaTable().GetById(GetCurrentZoneId());
    return Area?Area->Name:FString();
}
