#include "WowTerrainTile.h"
#include "WowPerformanceScope.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowRuntimeLumen.h"
#include "WowRuntimeStaticMesh.h"
#include "WowTerrainMeshBuilder.h"
#include "WowTerrainMaterial.h"
#include "Formats/AdtTypes.h"
#include "Formats/Dbc/DbcStore.h"
#include "Coord/WowCoordinate.h"
#include "Misc/Paths.h"
#include "WowDoodadManager.h"
#include "WowWmoRenderer.h"
#include "WowWaterRenderer.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

DEFINE_LOG_CATEGORY_STATIC(LogTerrainTile, Log, All);

// r684: puddle-friendly ground per 8x8 cell of each loaded tile (255 = dirt, road, stone; 0 = grass), by tile.
static TMap<FIntPoint, TArray<uint8>> GWowTerrainPuddleGround;

// r684: read by the rain puddles (WowRainGroundWater.cpp, same module): 0 grass .. 1 bare ground; -1 when unknown.
float WowTerrainPuddleGroundAt(const FVector& Location)
{
    const FIntPoint Tile = FWowCoordinate::WorldToTile(Location);
    const TArray<uint8>* Cells = GWowTerrainPuddleGround.Find(Tile);
    if (!Cells || Cells->Num() != 256 * 64) return -1.f;
    // Same maths as AWowWorldManager::GetTerrainIceAt.
    const double NgX = Location.Y / FWowCoordinate::SCALE + FWowCoordinate::ADT_CENTER_X;
    const double NgZ = -Location.X / FWowCoordinate::SCALE + FWowCoordinate::ADT_CENTER_Z;
    const double LocalX = NgX - Tile.X * FWowCoordinate::TILE_SIZE;
    const double LocalZ = NgZ - Tile.Y * FWowCoordinate::TILE_SIZE;
    const int32 CellX = FMath::Clamp(FMath::FloorToInt32(LocalX / (FWowCoordinate::CHUNK_SIZE / 8.0)), 0, 127);
    const int32 CellY = FMath::Clamp(FMath::FloorToInt32(LocalZ / (FWowCoordinate::CHUNK_SIZE / 8.0)), 0, 127);
    return (*Cells)[((CellY / 8) * 16 + CellX / 8) * 64 + (CellY % 8) * 8 + CellX % 8] / 255.f;
}

static int32 TerrainOuterIndex(int32 X, int32 Y)
{
    return Y * 17 + X;
}

static bool SampleAdtTerrainHeight(const FAdtData& Data, int32 TileX, int32 TileY, float AdtX, float AdtZ, float& OutHeight)
{
    const float TileMinX = TileX * FWowCoordinate::TILE_SIZE;
    const float TileMinZ = TileY * FWowCoordinate::TILE_SIZE;
    const float LocalX = AdtX - TileMinX;
    const float LocalZ = AdtZ - TileMinZ;

    if (LocalX < 0.0f || LocalZ < 0.0f ||
        LocalX > FWowCoordinate::TILE_SIZE || LocalZ > FWowCoordinate::TILE_SIZE)
    {
        return false;
    }

    const int32 ChunkX = FMath::Clamp(FMath::FloorToInt(LocalX / FWowCoordinate::CHUNK_SIZE), 0, 15);
    const int32 ChunkY = FMath::Clamp(FMath::FloorToInt(LocalZ / FWowCoordinate::CHUNK_SIZE), 0, 15);
    const FAdtChunkData& Chunk = Data.Chunks[ChunkY * 16 + ChunkX];

    const float ChunkLocalX = LocalX - ChunkX * FWowCoordinate::CHUNK_SIZE;
    const float ChunkLocalZ = LocalZ - ChunkY * FWowCoordinate::CHUNK_SIZE;
    const float GridX = FMath::Clamp(ChunkLocalX / FWowCoordinate::UNIT_SIZE, 0.0f, 8.0f);
    const float GridZ = FMath::Clamp(ChunkLocalZ / FWowCoordinate::UNIT_SIZE, 0.0f, 8.0f);

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, 8);
    const int32 Z0 = FMath::Clamp(FMath::FloorToInt(GridZ), 0, 8);
    const int32 X1 = FMath::Min(X0 + 1, 8);
    const int32 Z1 = FMath::Min(Z0 + 1, 8);
    const float AlphaX = GridX - static_cast<float>(X0);
    const float AlphaZ = GridZ - static_cast<float>(Z0);

    const float H00 = Chunk.WorldZ + Chunk.Heights[TerrainOuterIndex(X0, Z0)];
    const float H10 = Chunk.WorldZ + Chunk.Heights[TerrainOuterIndex(X1, Z0)];
    const float H01 = Chunk.WorldZ + Chunk.Heights[TerrainOuterIndex(X0, Z1)];
    const float H11 = Chunk.WorldZ + Chunk.Heights[TerrainOuterIndex(X1, Z1)];

    const float HX0 = FMath::Lerp(H00, H10, AlphaX);
    const float HX1 = FMath::Lerp(H01, H11, AlphaX);
    OutHeight = FMath::Lerp(HX0, HX1, AlphaZ);
    return true;
}

static bool ShouldSnapDoodadToTerrain(const FString& DoodadPath)
{
    FString Path = DoodadPath.ToLower();
    Path.ReplaceInline(TEXT("/"), TEXT("\\"));

    return Path.Contains(TEXT("fence")) ||
        Path.Contains(TEXT("stonewall")) ||
        Path.Contains(TEXT("stone_wall")) ||
        Path.Contains(TEXT("woodwall")) ||
        Path.Contains(TEXT("grave")) ||
        Path.Contains(TEXT("tomb")) ||
        Path.Contains(TEXT("cart")) ||
        Path.Contains(TEXT("wagon")) ||
        Path.Contains(TEXT("barrel")) ||
        Path.Contains(TEXT("crate")) ||
        Path.Contains(TEXT("post"));
}

static bool ShouldAlignDoodadToTerrainNormal(const FString& DoodadPath)
{
    FString Path = DoodadPath.ToLower();
    Path.ReplaceInline(TEXT("/"), TEXT("\\"));

    return Path.Contains(TEXT("fence")) ||
        Path.Contains(TEXT("stonewall")) ||
        Path.Contains(TEXT("stone_wall")) ||
        Path.Contains(TEXT("woodwall")) ||
        Path.Contains(TEXT("wood_wall"));
}

static bool ComputeAdtTerrainNormalUE(const FAdtData& Data, int32 TileX, int32 TileY, float AdtX, float AdtZ, FVector& OutNormal)
{
    const float SampleStep = FWowCoordinate::UNIT_SIZE * 0.5f;
    float EastHeight = 0.0f;
    float WestHeight = 0.0f;
    float SouthHeight = 0.0f;
    float NorthHeight = 0.0f;

    if (!SampleAdtTerrainHeight(Data, TileX, TileY, AdtX + SampleStep, AdtZ, EastHeight) ||
        !SampleAdtTerrainHeight(Data, TileX, TileY, AdtX - SampleStep, AdtZ, WestHeight) ||
        !SampleAdtTerrainHeight(Data, TileX, TileY, AdtX, AdtZ + SampleStep, SouthHeight) ||
        !SampleAdtTerrainHeight(Data, TileX, TileY, AdtX, AdtZ - SampleStep, NorthHeight))
    {
        return false;
    }

    const FVector East = FWowCoordinate::AdtToUE(AdtX + SampleStep, EastHeight, AdtZ);
    const FVector West = FWowCoordinate::AdtToUE(AdtX - SampleStep, WestHeight, AdtZ);
    const FVector South = FWowCoordinate::AdtToUE(AdtX, SouthHeight, AdtZ + SampleStep);
    const FVector North = FWowCoordinate::AdtToUE(AdtX, NorthHeight, AdtZ - SampleStep);

    const FVector TangentEast = (East - West).GetSafeNormal();
    const FVector TangentSouth = (South - North).GetSafeNormal();
    FVector Normal = FVector::CrossProduct(TangentEast, TangentSouth).GetSafeNormal();
    if (Normal.Z < 0.0f)
    {
        Normal *= -1.0f;
    }

    if (Normal.IsNearlyZero())
    {
        return false;
    }

    OutNormal = Normal;
    return true;
}

static uint32 GetGroundEffectIdAt(const FAdtChunkData& Chunk, float U, float V)
{
    if (Chunk.GroundEffectIds.Num() == 0)
    {
        return 0;
    }

    const int32 MapX = FMath::Clamp(FMath::FloorToInt(U * 8.0f), 0, 7);
    const int32 MapY = FMath::Clamp(FMath::FloorToInt(V * 8.0f), 0, 7);
    const int32 MapIndex = MapY * 8 + MapX;
    if (Chunk.GroundEffectDisabled[MapIndex])
    {
        return 0;
    }

    const int32 SelectedLayer = FMath::Clamp(
        static_cast<int32>(Chunk.GroundEffectLayerMap[MapIndex]),
        0,
        Chunk.GroundEffectIds.Num() - 1);

    const uint32 EffectId = Chunk.GroundEffectIds[SelectedLayer];
    return EffectId == MAX_uint32 ? 0 : EffectId;
}

static uint32 ChooseGroundDoodadId(const FGroundEffectTextureDbcEntry& Effect, FRandomStream& Random)
{
    int32 TotalWeight = 0;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (Effect.DoodadID[Index] != 0)
        {
            TotalWeight += FMath::Max(0, Effect.DoodadWeight[Index]);
        }
    }

    if (TotalWeight <= 0)
    {
        for (uint32 DoodadId : Effect.DoodadID)
        {
            if (DoodadId != 0)
            {
                return DoodadId;
            }
        }
        return 0;
    }

    int32 Roll = Random.RandRange(1, TotalWeight);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (Effect.DoodadID[Index] == 0)
        {
            continue;
        }

        Roll -= FMath::Max(0, Effect.DoodadWeight[Index]);
        if (Roll <= 0)
        {
            return Effect.DoodadID[Index];
        }
    }

    return 0;
}

static int32 AppendGroundEffectDoodads(const FAdtData& Data, int32 TileX, int32 TileY,
    TArray<FAdtDoodadPlacement>& InOutPlacements, TArray<FString>& InOutPaths)
{
    const FGroundEffectTextureDbc& EffectTable = FDbcStore::Get().GroundEffectTextures();
    const FGroundEffectDoodadDbc& DoodadTable = FDbcStore::Get().GroundEffectDoodads();
    if (EffectTable.Num() == 0 || DoodadTable.Num() == 0)
    {
        return 0;
    }

    TMap<uint32, int32> PathIndexByDoodadId;
    int32 Added = 0;
    constexpr int32 SamplesPerAxis = 4;

    for (int32 ChunkIndex = 0; ChunkIndex < 256; ++ChunkIndex)
    {
        const FAdtChunkData& Chunk = Data.Chunks[ChunkIndex];
        if (Chunk.GroundEffectIds.Num() == 0)
        {
            continue;
        }

        FRandomStream Random(TileX * 73856093 ^ TileY * 19349663 ^ ChunkIndex * 83492791);
        for (int32 SampleY = 0; SampleY < SamplesPerAxis; ++SampleY)
        {
            for (int32 SampleX = 0; SampleX < SamplesPerAxis; ++SampleX)
            {
                const int32 HoleBit = SampleY * SamplesPerAxis + SampleX;
                if ((Chunk.Holes & (1u << HoleBit)) != 0)
                {
                    continue;
                }

                const float U = (static_cast<float>(SampleX) + Random.FRandRange(0.18f, 0.82f)) /
                    static_cast<float>(SamplesPerAxis);
                const float V = (static_cast<float>(SampleY) + Random.FRandRange(0.18f, 0.82f)) /
                    static_cast<float>(SamplesPerAxis);
                const uint32 EffectId = GetGroundEffectIdAt(Chunk, U, V);
                const FGroundEffectTextureDbcEntry* Effect = EffectTable.GetById(EffectId);
                if (!Effect || Effect->Density == 0)
                {
                    continue;
                }

                const float SpawnProbability = FMath::Clamp(static_cast<float>(Effect->Density) / 16.0f, 0.20f, 1.0f);
                if (Random.FRand() > SpawnProbability)
                {
                    continue;
                }

                const uint32 DoodadId = ChooseGroundDoodadId(*Effect, Random);
                const FGroundEffectDoodadDbcEntry* Doodad = DoodadTable.GetById(DoodadId);
                if (!Doodad || Doodad->ModelPath.IsEmpty())
                {
                    continue;
                }

                int32 PathIndex = INDEX_NONE;
                if (const int32* ExistingIndex = PathIndexByDoodadId.Find(DoodadId))
                {
                    PathIndex = *ExistingIndex;
                }
                else
                {
                    PathIndex = InOutPaths.Add(Doodad->ModelPath);
                    PathIndexByDoodadId.Add(DoodadId, PathIndex);
                }

                const float AdtX = TileX * FWowCoordinate::TILE_SIZE +
                    (static_cast<float>(Chunk.IndexX) + U) * FWowCoordinate::CHUNK_SIZE;
                const float AdtZ = TileY * FWowCoordinate::TILE_SIZE +
                    (static_cast<float>(Chunk.IndexY) + V) * FWowCoordinate::CHUNK_SIZE;
                float AdtHeight = 0.0f;
                if (!SampleAdtTerrainHeight(Data, TileX, TileY, AdtX, AdtZ, AdtHeight))
                {
                    continue;
                }

                FAdtDoodadPlacement Placement;
                Placement.NameIndex = PathIndex;
                Placement.UniqueId = 0xE0000000u ^
                    (static_cast<uint32>(TileX & 63) << 20) ^
                    (static_cast<uint32>(TileY & 63) << 14) ^
                    (static_cast<uint32>(ChunkIndex) << 6) ^
                    static_cast<uint32>(SampleY * SamplesPerAxis + SampleX);
                Placement.Position = FVector(AdtX, AdtHeight, AdtZ);
                Placement.Rotation = FVector(0.0f, Random.FRandRange(0.0f, 360.0f), 0.0f);
                Placement.Scale = static_cast<uint16>(FMath::RoundToInt(
                    Random.FRandRange(0.88f, 1.12f) * 1024.0f));
                InOutPlacements.Add(Placement);
                ++Added;
            }
        }
    }

    return Added;
}

AWowTerrainTile::AWowTerrainTile()
{
    PrimaryActorTick.bCanEverTick = false;
    RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(RootScene);
}

UMaterialInterface* AWowTerrainTile::GetDefaultTerrainMaterial() const
{
    static UMaterial* DefaultMat = nullptr;
    if (!DefaultMat)
    {
        DefaultMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
        if (DefaultMat) DefaultMat->AddToRoot(); // r274: keep the cached pointer valid across GC
    }
    return DefaultMat;
}

void AWowTerrainTile::BuildFromAdtData(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache, TSet<uint32>* SpawnedWmoIds)
{
    for (int32 Phase = 0; Phase < NumBuildPhases; ++Phase)
    {
        BuildPhase(Phase, Data, TX, TY, Mpq, Cache);
    }
}

void AWowTerrainTile::BuildPhase(int32 Phase, const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    TileCoord = FIntPoint(TX, TY);
    if (!Data.bIsValid) return;
    if (Phase >= 1 && Phase <= NumWaterBands)
    {
        FWowPerformanceScope Timing(TEXT("tile-build-water"), TEXT("world"), 16.0);
        const int32 RowsPerBand = 16 / NumWaterBands;
        const int32 FirstRow = (Phase - 1) * RowsPerBand;
        TArray<UStaticMeshComponent*> WaterComps = FWowWaterRenderer::CreateWaterMeshes(this, Data, TX, TY, true,
            FirstRow, FirstRow + RowsPerBand);
        for (UStaticMeshComponent* WC : WaterComps)
        {
            WaterMeshes.Add(WC);
        }
        return;
    }
    if (Phase == NumWaterBands + 1)
    {
        // r567: all doodad steps at once (the world manager calls BuildDoodadStep itself).
        while (!BuildDoodadStep(Data, TX, TY, Mpq, Cache, true)) {}
        return;
    }
    // r565: the ground is built in steps (BuildTerrainStep); here all of them at once.
    while (!BuildTerrainStep(Data, TX, TY, Mpq, Cache, true)) {}
    bTerrainReady = true;
}

// r565 (Stefan: "when stopping W he kept on going for a second"): building a tile's ground cost
// 150-250 ms in one frame on the game thread (25 tiles after login, "tile-build-terrain" in the log),
// so key releases arrived a quarter second late. Now it is split into steps of a few milliseconds,
// one per world tick: 0 geometry and placement data, 1-4 the chunk materials (64 chunks each),
// 5 the static mesh and its component, 6 waits for the collision cooked on a worker thread.
// The world manager counts the tile as ground only once IsTerrainReady().
bool AWowTerrainTile::BuildTerrainStep(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache, bool bSynchronous)
{
    TileCoord = FIntPoint(TX, TY);
    if (bTerrainReady || !Data.bIsValid) { bTerrainReady = true; return true; }
    const int32 Step = TerrainStep;
    if (Step == 0)
    {
        FWowPerformanceScope Timing(TEXT("tile-build-terrain-geometry"), TEXT("world"), 8.0);
        // Doodads arrive in the last phase as HISMCs; until then keep the legacy per-instance
        // streamer away from this tile (the doodad phase sets the real value).
        bUsesInstancedDoodads = true;

        FVector TileCenter = FWowCoordinate::TileToWorld(TX, TY);
        SetActorLocation(TileCenter);
        RootScene->SetMobility(EComponentMobility::Static);

        UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d at %s: %d textures, %d doodads, %d WMOs"),
            TX, TY, *TileCenter.ToString(), Data.TexturePaths.Num(), Data.DoodadPlacements.Num(), Data.WmoPlacements.Num());

        // Build a single UStaticMesh with one polygon group per chunk (up to 256 material slots)
        PendingTerrainMesh = MakeUnique<FMeshDescription>();
        FMeshDescription& MeshDesc = *PendingTerrainMesh;
        PendingTerrainChunks.Reset();
        PendingTerrainMaterials.Reset();
        // Must set UV channels before Register() so attributes are allocated correctly
        MeshDesc.SetNumUVChannels(2); // UV0 = alpha/splatmap [0,1], UV1 = world-space tiled
        FStaticMeshAttributes Attributes(MeshDesc);
        Attributes.Register();

        TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
        TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
        TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
        TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
        TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

        int32 ChunksWithHeights = 0;
        int32 ChunksSkipped = 0;

        for (int32 i = 0; i < 256; ++i)
        {
            const FAdtChunkData& Chunk = Data.Chunks[i];
            FTerrainChunkMeshData MeshData = FTerrainMeshBuilder::BuildChunkMesh(Chunk, TX, TY);

            if (MeshData.Vertices.Num() == 0 || MeshData.Indices.Num() == 0)
            {
                ChunksSkipped++;
                continue;
            }

            // Check if this chunk has valid height variation
            bool bHasHeights = false;
            for (int32 h = 0; h < 145; h++)
            {
                if (Chunk.Heights[h] != 0.0f) { bHasHeights = true; break; }
            }
            if (bHasHeights) ChunksWithHeights++;

            // Create a polygon group for this chunk (maps to a material slot)
            FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();

            // Add vertices and vertex instances for this chunk
            int32 BaseVertIndex = MeshDesc.Vertices().Num();
            TArray<FVertexInstanceID> ChunkVertInstIDs;
            ChunkVertInstIDs.SetNum(MeshData.Vertices.Num());

            for (int32 v = 0; v < MeshData.Vertices.Num(); ++v)
            {
                const FVector& P = MeshData.Vertices[v];
                FVertexID VertID = MeshDesc.CreateVertex();
                VertexPositions[VertID] = FVector3f(P.X, P.Y, P.Z);

                FVertexInstanceID InstID = MeshDesc.CreateVertexInstance(VertID);
                ChunkVertInstIDs[v] = InstID;

                FVector3f N = (v < MeshData.Normals.Num())
                    ? FVector3f(MeshData.Normals[v]) : FVector3f(0, 0, 1);
                N.Normalize();
                VertexInstanceNormals[InstID] = N;

                FVector3f T = FVector3f::CrossProduct(N, FVector3f(0, 1, 0));
                if (T.SizeSquared() < 0.001f)
                    T = FVector3f::CrossProduct(N, FVector3f(1, 0, 0));
                T.Normalize();
                VertexInstanceTangents[InstID] = T;
                VertexInstanceBinormalSigns[InstID] = 1.0f;

                FVector2f UV0 = (v < MeshData.UVs.Num())
                    ? FVector2f(MeshData.UVs[v]) : FVector2f(0, 0);
                VertexInstanceUVs.Set(InstID, 0, UV0);

                FVector2f UV1 = (v < MeshData.TiledUVs.Num())
                    ? FVector2f(MeshData.TiledUVs[v]) : FVector2f(0, 0);
                VertexInstanceUVs.Set(InstID, 1, UV1);
            }

            // Create triangles for this chunk
            for (int32 t = 0; t < MeshData.Indices.Num(); t += 3)
            {
                TArray<FVertexInstanceID> TriVerts;
                TriVerts.Add(ChunkVertInstIDs[MeshData.Indices[t]]);
                TriVerts.Add(ChunkVertInstIDs[MeshData.Indices[t + 1]]);
                TriVerts.Add(ChunkVertInstIDs[MeshData.Indices[t + 2]]);
                MeshDesc.CreatePolygon(PolyGroup, TriVerts);
            }

            PendingTerrainChunks.Add(i);
        }

        // Store WMO placement data for distance-based streaming (doodads are now instanced)
        DoodadPlacements = Data.DoodadPlacements;
        DoodadPaths = Data.DoodadPaths;
        WmoPlacements = Data.WmoPlacements;
        WmoPaths = Data.WmoPaths;
        CachedMpq = Mpq;
        CachedCache = Cache;

        // Ice under foot (r230): which parts of the ground are painted with an ice texture.
        {
            auto IsIceTexture = [](const FString& Path)
            {
                const FString File = FPaths::GetBaseFilename(Path).ToLower();
                if (File.Contains(TEXT("glacier")) || File.Contains(TEXT("frozen"))) return true;
                int32 At = File.Find(TEXT("ice"));
                while (At != INDEX_NONE)
                {
                    // "ice" as its own part of the name: not Icecrown (a zone, rock and snow too),
                    // rice, slice, dice, spice, price, lice, vice, juice, office, police...
                    const bool bWordStart = At == 0 || !FChar::IsAlpha(File[At - 1]);
                    const bool bCrown = File.Mid(At, 8) == TEXT("icecrown");
                    if (bWordStart && !bCrown) return true;
                    At = File.Find(TEXT("ice"), ESearchCase::IgnoreCase, ESearchDir::FromStart, At + 3);
                }
                return false;
            };
            TArray<bool> IceTexture;
            bool bAnyIce = false;
            for (const FString& Path : Data.TexturePaths) { IceTexture.Add(IsIceTexture(Path)); bAnyIce |= IceTexture.Last(); }
            IceCells.Reset();
            if (bAnyIce)
            {
                IceCells.Init(0, 256 * 64);
                for (const auto& Chunk : Data.Chunks)
                {
                    if (Chunk.IndexX < 0 || Chunk.IndexX >= 16 || Chunk.IndexY < 0 || Chunk.IndexY >= 16) continue;
                    const int32 Base = (Chunk.IndexY * 16 + Chunk.IndexX) * 64;
                    for (int32 Cell = 0; Cell < 64; ++Cell)
                    {
                        const int32 Pixel = ((Cell / 8) * 8 + 4) * 64 + (Cell % 8) * 8 + 4; // centre of the cell in the 64x64 alpha map
                        // Layers blend in order, each over the ones below: weight = own alpha x what the later layers leave.
                        float Ice = 0.f;
                        for (int32 L = Chunk.TextureIndices.Num() - 1, Remaining = 255; L >= 0; --L)
                        {
                            const float Alpha = L == 0 ? 1.f : (Chunk.AlphaMaps.IsValidIndex(L - 1) && Chunk.AlphaMaps[L - 1].Num() > Pixel
                                ? Chunk.AlphaMaps[L - 1][Pixel] / 255.f : 0.f);
                            if (IceTexture.IsValidIndex(Chunk.TextureIndices[L]) && IceTexture[Chunk.TextureIndices[L]]) Ice += Alpha * Remaining;
                            Remaining = FMath::RoundToInt(Remaining * (1.f - Alpha));
                        }
                        IceCells[Base + Cell] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Ice), 0, 255));
                    }
                }
                int32 IceTextures = 0; for (bool b : IceTexture) IceTextures += b;
                UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: %d ice ground textures (slippery)"), TX, TY, IceTextures);
            }
        }

        // r684 (Stefan: "in grass it's also less likely to actually see the puddles - like in real life - so brown
        // dirt and roads is better, like Stormwind's stones as well"): how much of each 8x8 cell is painted with a
        // grassy texture (grass, flowers, scrub, crops, moss...). Rain puddles read it (WowTerrainPuddleGroundAt):
        // few on grass, most on dirt, roads, stone and rock.
        {
            auto IsGrassTexture = [](const FString& Path)
            {
                const FString File = FPaths::GetBaseFilename(Path).ToLower();
                for (const TCHAR* Word : {TEXT("grass"), TEXT("flower"), TEXT("scrub"), TEXT("brush"), TEXT("crop"), TEXT("moss"),
                    TEXT("meadow"), TEXT("leaf"), TEXT("leaves"), TEXT("fern"), TEXT("weed"), TEXT("clover"), TEXT("bush"), TEXT("needle")})
                    if (File.Contains(Word)) return true;
                return false;
            };
            TArray<bool> GrassTexture;
            for (const FString& Path : Data.TexturePaths) GrassTexture.Add(IsGrassTexture(Path));
            TArray<uint8> Cells; Cells.Init(255, 256 * 64);
            for (const auto& Chunk : Data.Chunks)
            {
                if (Chunk.IndexX < 0 || Chunk.IndexX >= 16 || Chunk.IndexY < 0 || Chunk.IndexY >= 16) continue;
                const int32 Base = (Chunk.IndexY * 16 + Chunk.IndexX) * 64;
                for (int32 Cell = 0; Cell < 64; ++Cell)
                {
                    const int32 Pixel = ((Cell / 8) * 8 + 4) * 64 + (Cell % 8) * 8 + 4;
                    float Grass = 0.f;
                    for (int32 L = Chunk.TextureIndices.Num() - 1, Remaining = 255; L >= 0; --L)
                    {
                        const float Alpha = L == 0 ? 1.f : (Chunk.AlphaMaps.IsValidIndex(L - 1) && Chunk.AlphaMaps[L - 1].Num() > Pixel
                            ? Chunk.AlphaMaps[L - 1][Pixel] / 255.f : 0.f);
                        if (GrassTexture.IsValidIndex(Chunk.TextureIndices[L]) && GrassTexture[Chunk.TextureIndices[L]]) Grass += Alpha * Remaining;
                        Remaining = FMath::RoundToInt(Remaining * (1.f - Alpha));
                    }
                    Cells[Base + Cell] = static_cast<uint8>(255 - FMath::Clamp(FMath::RoundToInt(Grass), 0, 255));
                }
            }
            GWowTerrainPuddleGround.Add(FIntPoint(TX, TY), MoveTemp(Cells));
        }

        // Store area IDs per chunk for zone detection
        ChunkAreaIds.Init(0, 256);
        for (const auto& Chunk : Data.Chunks)
        {
            if (Chunk.IndexX >= 0 && Chunk.IndexX < 16 && Chunk.IndexY >= 0 && Chunk.IndexY < 16)
                ChunkAreaIds[Chunk.IndexY * 16 + Chunk.IndexX] = Chunk.AreaId;
        }

        UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: %d sections, %d/%d have heights, %d skipped, %d WMO placements for streaming"),
            TX, TY, PendingTerrainChunks.Num(), ChunksWithHeights, 256 - ChunksSkipped, ChunksSkipped, WmoPlacements.Num());

        TerrainStep = PendingTerrainChunks.Num() > 0 ? 1 : 6;
        if (TerrainStep == 6) { PendingTerrainMesh.Reset(); bTerrainReady = true; return true; }
        return false;
    }
    if (Step >= 1 && Step <= 4)
    {
        FWowPerformanceScope Timing(TEXT("tile-build-terrain-materials"), TEXT("world"), 8.0);
        UMaterialInterface* DefaultMat = GetDefaultTerrainMaterial();
        const int32 First = PendingTerrainMaterials.Num();
        const int32 Last = FMath::Min(PendingTerrainChunks.Num(), (bSynchronous ? 256 : First + 64));
        for (int32 Section = First; Section < Last; ++Section)
        {
            UMaterialInstanceDynamic* ChunkMat = FWowTerrainMaterial::CreateChunkMaterial(
                Data.Chunks[PendingTerrainChunks[Section]], Data, Mpq, Cache, this);
            PendingTerrainMaterials.Add(ChunkMat ? static_cast<UMaterialInterface*>(ChunkMat) : DefaultMat);
        }
        // r735 (Stefan 23.10, TormentWoW map 750 = Blizzard's Ulduar terrain): one line per tile instead of one warning
        // per chunk. Chunks with no texture layer are normal map data (Ulduar has whole edge tiles of them); a layer-0
        // texture that our data lacks is a real gap, so that case is a warning naming the first path.
        if (PendingTerrainMaterials.Num() >= PendingTerrainChunks.Num())
        {
            int32 NoLayers = 0, Missing = 0;
            FString FirstMissing;
            TMap<int32, bool> BaseLoads;
            for (const int32 ChunkIndex : PendingTerrainChunks)
            {
                const FAdtChunkData& Chunk = Data.Chunks[ChunkIndex];
                if (Chunk.TextureIndices.Num() == 0) { ++NoLayers; continue; }
                const int32 BaseTexture = Chunk.TextureIndices[0];
                if (!BaseLoads.Contains(BaseTexture))
                    BaseLoads.Add(BaseTexture, Data.TexturePaths.IsValidIndex(BaseTexture)
                        && FWowTerrainMaterial::LoadBlpTexture(Data.TexturePaths[BaseTexture], Mpq, Cache) != nullptr);
                if (!BaseLoads[BaseTexture])
                {
                    ++Missing;
                    if (FirstMissing.IsEmpty())
                        FirstMissing = Data.TexturePaths.IsValidIndex(BaseTexture) ? Data.TexturePaths[BaseTexture]
                            : FString::Printf(TEXT("texture index %d of %d"), BaseTexture, Data.TexturePaths.Num());
                }
            }
            if (Missing > 0)
            {
                UE_LOG(LogTerrainTile, Warning, TEXT("Tile %d,%d: %d chunks use a ground texture our data does not have (first: %s), %d have no ground texture in the map; both drawn with the dark default ground"),
                    TX, TY, Missing, *FirstMissing, NoLayers);
            }
            else if (NoLayers > 0)
            {
                UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: %d of %d chunks have no ground texture in the map data (normal at map edges and under buildings); drawn with the dark default ground"),
                    TX, TY, NoLayers, PendingTerrainChunks.Num());
            }
        }
        TerrainStep = PendingTerrainMaterials.Num() >= PendingTerrainChunks.Num() ? 5 : Step + 1;
        return false;
    }
    if (Step == 5)
    {
        FWowPerformanceScope Timing(TEXT("tile-build-terrain-mesh"), TEXT("world"), 8.0);
        FMeshDescription& MeshDesc = *PendingTerrainMesh;
        const TArray<TObjectPtr<UMaterialInterface>>& SectionMaterials = PendingTerrainMaterials;
        const int32 SectionIndex = SectionMaterials.Num();
    if (SectionIndex > 0)
    {
        // Build UStaticMesh from the mesh description
        UStaticMesh* SM = NewObject<UWowRuntimeStaticMesh>(this);

        // Add materials BEFORE building so sections are initialized with correct materials
        for (UMaterialInterface* Mat : SectionMaterials)
        {
            FStaticMaterial StaticMaterial(Mat);
            StaticMaterial.UVChannelData.bInitialized = true;
            SM->GetStaticMaterials().Add(MoveTemp(StaticMaterial));
        }

        SM->bAllowCPUAccess = true;

        TArray<const FMeshDescription*> MeshDescs;
        MeshDescs.Add(&MeshDesc);
        UStaticMesh::FBuildMeshDescriptionsParams Params;
        Params.bBuildSimpleCollision = false;
        Params.bFastBuild = true;
        Params.bCommitMeshDescription = false;
        WowPrepareRuntimeMeshMaterials(SM, const_cast<FMeshDescription&>(*MeshDescs[0]));
        SM->BuildFromMeshDescriptions(MeshDescs, Params);
        WowBuildRuntimeLumenCards(SM, MeshDesc, true);
        SM->NeverStream = true;

        // Enable complex collision (use render mesh as collision)
        if (SM->GetBodySetup() == nullptr)
        {
            SM->CreateBodySetup();
        }
        UBodySetup* BodySetup = SM->GetBodySetup();
        if (BodySetup) BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;

        // Create UStaticMeshComponent and set materials on the component
        UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this, TEXT("TerrainMesh"));
        MeshComp->SetStaticMesh(SM);
        MeshComp->SetupAttachment(RootScene);
        MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
        // Terrain slopes are independent of tree/prop collision restrictions.
        MeshComp->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Increase, 55.0f));
        for (int32 s = 0; s < SectionMaterials.Num(); ++s)
        {
            MeshComp->SetMaterial(s, SectionMaterials[s]);
        }
        MeshComp->SetCastShadow(false);
        MeshComp->SetCollisionObjectType(ECC_WorldStatic);
        ApplyRvtTo(MeshComp);
        // Registering with collision on would cook the collision right here, on the game thread.
        if (!bSynchronous) MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComp->RegisterComponent();
        ChunkMeshes.Add(MeshComp);
        PendingTerrainComponent = MeshComp;
        // Runtime-built terrain needs cooked physics data for line traces and walking. Cook it on a
        // worker thread (r565); the component gets its physics state when the cook is done.
        TerrainStep = 6;
        if (BodySetup && !bSynchronous)
        {
            TWeakObjectPtr<AWowTerrainTile> WeakTile(this);
            BodySetup->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateLambda([WeakTile](bool bSuccess)
            {
                AWowTerrainTile* Tile = WeakTile.Get();
                if (!Tile) return;
                if (UStaticMeshComponent* Ground = Tile->PendingTerrainComponent.Get())
                    Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); // creates the physics state
                Tile->PendingTerrainComponent = nullptr;
                Tile->bTerrainReady = true;
                UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: ground collision cooked%s"), Tile->TileCoord.X, Tile->TileCoord.Y,
                    bSuccess ? TEXT("") : TEXT(" (FAILED)"));
            }));
        }
        else
        {
            if (BodySetup) BodySetup->CreatePhysicsMeshes();
            MeshComp->RecreatePhysicsState();
            PendingTerrainComponent = nullptr;
            bTerrainReady = true;
        }
    }
    else bTerrainReady = true;
    PendingTerrainMesh.Reset();
    PendingTerrainMaterials.Reset();
    TerrainStep = 6;
    return true;
    }
    // Step 6: the mesh is built; the collision may still be cooking (IsTerrainReady).
    return true;
}


void AWowTerrainTile::BuildDoodadPhase(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    // Spawn authored and texture-driven ground doodads using instancing.
    {
        TArray<FAdtDoodadPlacement> GroundAdjustedDoodads = Data.DoodadPlacements;
        TArray<FString> SpawnDoodadPaths = Data.DoodadPaths;
        const int32 GroundEffectDoodads = AppendGroundEffectDoodads(
            Data, TX, TY, GroundAdjustedDoodads, SpawnDoodadPaths);
        int32 GroundSnappedDoodads = 0;
        for (FAdtDoodadPlacement& Placement : GroundAdjustedDoodads)
        {
            if (!SpawnDoodadPaths.IsValidIndex(Placement.NameIndex))
            {
                continue;
            }

            if (!ShouldSnapDoodadToTerrain(SpawnDoodadPaths[Placement.NameIndex]))
            {
                continue;
            }

            float TerrainHeight = 0.0f;
            if (SampleAdtTerrainHeight(Data, TX, TY, Placement.Position.X, Placement.Position.Z, TerrainHeight))
            {
                Placement.Position.Y = TerrainHeight;
                GroundSnappedDoodads++;

                if (ShouldAlignDoodadToTerrainNormal(SpawnDoodadPaths[Placement.NameIndex]))
                {
                    FVector TerrainNormalUE = FVector::UpVector;
                    if (ComputeAdtTerrainNormalUE(Data, TX, TY, Placement.Position.X, Placement.Position.Z, TerrainNormalUE))
                    {
                        Placement.TerrainNormalUE = TerrainNormalUE;
                        Placement.bAlignToTerrainNormal = true;
                    }
                }
            }
        }

        if (GroundEffectDoodads > 0)
        {
            UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: generated %d instanced ground-effect doodads"),
                TX, TY, GroundEffectDoodads);
        }

        if (GroundSnappedDoodads > 0)
        {
            UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: snapped %d ground doodads to terrain height"),
                TX, TY, GroundSnappedDoodads);
        }

        // r567: placed in steps (BuildDoodadStep), a few models at a time.
        PendingDoodads = MoveTemp(GroundAdjustedDoodads);
        PendingDoodadPaths = MoveTemp(SpawnDoodadPaths);
    }
}

// r567 (performance list, after r565): the doodad phase placed every tree, rock and ground-effect
// model of a tile in one frame, up to 90 ms when new models had to be built. It now prepares the
// placements once, then places whole models in batches (at most 6 models or 1500 instances per step,
// one model per batch at least), one step per call.
bool AWowTerrainTile::BuildDoodadStep(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache, bool bSynchronous)
{
    if (!Data.bIsValid) return true;
    if (!bDoodadsPrepared)
    {
        FWowPerformanceScope Timing(TEXT("tile-build-doodads-prepare"), TEXT("world"), 8.0);
        bDoodadsPrepared = true;
        BuildDoodadPhase(Data, TX, TY, Mpq, Cache);
        TMap<int32, TArray<int32>> ByModel;
        for (int32 Index = 0; Index < PendingDoodads.Num(); ++Index)
            ByModel.FindOrAdd(PendingDoodads[Index].NameIndex).Add(Index);
        TArray<int32> Batch;
        int32 Models = 0;
        for (auto& Pair : ByModel)
        {
            // r648 (Stefan: "huge drops in FPS down to 8", the log had tile-build-doodads steps up to 233 ms):
            // at most 2 models or 600 instances per step.
            if (Models > 0 && (Models >= 2 || Batch.Num() + Pair.Value.Num() > 600))
            {
                PendingDoodadBatches.Add(MoveTemp(Batch));
                Batch.Reset();
                Models = 0;
            }
            Batch.Append(Pair.Value);
            ++Models;
        }
        if (Batch.Num() > 0) PendingDoodadBatches.Add(MoveTemp(Batch));
        NextDoodadBatch = 0;
        if (!bSynchronous) return PendingDoodadBatches.Num() == 0;
    }
    FWowPerformanceScope Timing(TEXT("tile-build-doodads"), TEXT("world"), 8.0);
    while (NextDoodadBatch < PendingDoodadBatches.Num())
    {
        TArray<FAdtDoodadPlacement> Subset;
        for (int32 Index : PendingDoodadBatches[NextDoodadBatch]) Subset.Add(PendingDoodads[Index]);
        ++NextDoodadBatch;
        InstancedDoodads.Append(FWowDoodadManager::SpawnDoodadsInstanced(this, Subset, PendingDoodadPaths, Mpq, Cache));
        if (!bSynchronous) break;
    }
    if (NextDoodadBatch < PendingDoodadBatches.Num()) return false;
    bUsesInstancedDoodads = InstancedDoodads.Num() > 0;
    PendingDoodads.Empty();
    PendingDoodadPaths.Empty();
    PendingDoodadBatches.Empty();
    UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: doodads placed, %d instanced models"), TX, TY, InstancedDoodads.Num());
    return true;
}

void AWowTerrainTile::ApplyRvtTo(UStaticMeshComponent* Mesh) const
{
    URuntimeVirtualTexture* RVT = TerrainRVT.Get();
    if (!RVT || !Mesh) return;
    Mesh->RuntimeVirtualTextures.AddUnique(RVT);
    Mesh->VirtualTextureLodBias = 0;
    Mesh->VirtualTextureCullMips = 0;
    Mesh->VirtualTextureMinCoverage = 1;
    Mesh->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Always;
}

void AWowTerrainTile::ApplyRuntimeVirtualTexture(URuntimeVirtualTexture* RVT)
{
    if (!RVT) return;
    TerrainRVT = RVT; // r565: the ground mesh may be built after this call

    // Configure all terrain chunk meshes to render into the RVT
    for (UStaticMeshComponent* Mesh : ChunkMeshes)
    {
        if (Mesh)
        {
            Mesh->RuntimeVirtualTextures.AddUnique(RVT);
            Mesh->VirtualTextureLodBias = 0;
            Mesh->VirtualTextureCullMips = 0;
            Mesh->VirtualTextureMinCoverage = 1;
            Mesh->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Always;
            Mesh->MarkRenderStateDirty();
        }
    }

    UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: Applied RVT to %d chunk meshes"),
        TileCoord.X, TileCoord.Y, ChunkMeshes.Num());
}
