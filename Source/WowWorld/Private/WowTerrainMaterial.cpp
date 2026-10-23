#include "WowTerrainMaterial.h"
#include "WowClientPreferences.h" // r740: the "stonereliefground" option
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "WowCookedMaterial.h"
#include "Formats/AdtTypes.h"
#include "Formats/BlpParser.h"
#include "WowTextureFactory.h"
#include "WowAssetCache.h"
#include "Mpq/MpqManager.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Misc/App.h"
#include "UObject/SavePackage.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "ShaderCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogTerrainMat, Log, All);

namespace
{
TArray<uint8> DownsampleAlphaMip(const TArray<uint8>& Source, int32 SourceWidth, int32 SourceHeight)
{
    const int32 DestinationWidth = FMath::Max(SourceWidth / 2, 1);
    const int32 DestinationHeight = FMath::Max(SourceHeight / 2, 1);
    TArray<uint8> Destination;
    Destination.SetNumUninitialized(DestinationWidth * DestinationHeight);

    for (int32 Y = 0; Y < DestinationHeight; ++Y)
    {
        for (int32 X = 0; X < DestinationWidth; ++X)
        {
            int32 Sum = 0;
            int32 SampleCount = 0;
            for (int32 OffsetY = 0; OffsetY < 2; ++OffsetY)
            {
                const int32 SourceY = FMath::Min(Y * 2 + OffsetY, SourceHeight - 1);
                for (int32 OffsetX = 0; OffsetX < 2; ++OffsetX)
                {
                    const int32 SourceX = FMath::Min(X * 2 + OffsetX, SourceWidth - 1);
                    Sum += Source[SourceY * SourceWidth + SourceX];
                    ++SampleCount;
                }
            }
            Destination[Y * DestinationWidth + X] = static_cast<uint8>(Sum / SampleCount);
        }
    }

    return Destination;
}
}

// r735 (Stefan 23.10, TormentWoW login; the log then showed 2,912 "Chunk has no valid base texture" lines): Blizzard's
// Ulduar tiles, which Torment's "Sanctum of Torment" (map 750) reuses unchanged, have whole chunks with no texture layer
// at all (two edge tiles have none). Those chunks fell back to the engine's plain white shape material. They now get
// Blizzard's own black ground texture (TILESET\Generic\Black.blp, 16x16 black, in every 3.3.5 data set) through the
// normal terrain material, so fog, light and rain treat them like their neighbours.
static UTexture2D* WowDefaultGroundTexture(FMpqManager* Mpq, FWowAssetCache* Cache)
{
    static TStrongObjectPtr<UTexture2D> Ground;
    static bool bUnavailable = false;
    if (!Ground.IsValid() && !bUnavailable)
    {
        Ground.Reset(FWowTerrainMaterial::LoadBlpTexture(TEXT("TILESET\\Generic\\Black.blp"), Mpq, Cache));
        bUnavailable = !Ground.IsValid();
        if (bUnavailable)
            UE_LOG(LogTerrainMat, Warning, TEXT("TILESET\\Generic\\Black.blp is not in the data: untextured terrain chunks keep the plain default material"));
    }
    return Ground.Get();
}

UMaterialInstanceDynamic* FWowTerrainMaterial::CreateChunkMaterial(
    const FAdtChunkData& ChunkData,
    const FAdtData& AdtData,
    FMpqManager* Mpq,
    FWowAssetCache* Cache,
    UObject* Outer)
{
    if (!Mpq || !Outer) return nullptr;

    // Load ground textures referenced by this chunk
    TArray<UTexture2D*> LayerTextures;
    for (int32 i = 0; i < ChunkData.TextureIndices.Num() && i < 4; ++i)
    {
        int32 TexIdx = ChunkData.TextureIndices[i];
        if (AdtData.TexturePaths.IsValidIndex(TexIdx))
        {
            const FString& TexPath = AdtData.TexturePaths[TexIdx];
            UTexture2D* Tex = LoadBlpTexture(TexPath, Mpq, Cache);
            if (Tex)
            {
                UE_LOG(LogTerrainMat, Verbose, TEXT("Loaded layer %d texture: %s (%dx%d)"),
                    i, *TexPath, Tex->GetSizeX(), Tex->GetSizeY());
            }
            LayerTextures.Add(Tex);
        }
        else
        {
            LayerTextures.Add(nullptr);
        }
    }

    // r735: a chunk with no texture layer in the map data (or a layer-0 texture our data lacks) gets the dark default
    // ground instead of the engine's white material. WowTerrainTile logs these once per tile (this was one warning per
    // chunk: 2,912 lines for one Torment login).
    bool bDefaultGround = false;
    if (LayerTextures.Num() == 0 || !LayerTextures[0])
    {
        UTexture2D* DefaultGround = WowDefaultGroundTexture(Mpq, Cache);
        if (!DefaultGround)
        {
            UE_LOG(LogTerrainMat, Verbose, TEXT("Chunk [%d,%d] has no valid base texture"),
                ChunkData.IndexX, ChunkData.IndexY);
            return nullptr;
        }
        if (LayerTextures.Num() == 0) LayerTextures.Add(DefaultGround);
        else LayerTextures[0] = DefaultGround;
        bDefaultGround = true;
    }

    // Debug: log first chunk's texture info
    static bool bLoggedFirst = false;
    if (!bLoggedFirst)
    {
        bLoggedFirst = true;
        UE_LOG(LogTerrainMat, Log, TEXT("First chunk [%d,%d]: %d layer textures, %d alpha maps, base tex=%s (%dx%d)"),
            ChunkData.IndexX, ChunkData.IndexY,
            LayerTextures.Num(), ChunkData.AlphaMaps.Num(),
            LayerTextures[0] ? *LayerTextures[0]->GetName() : TEXT("null"),
            LayerTextures[0] ? LayerTextures[0]->GetSizeX() : 0,
            LayerTextures[0] ? LayerTextures[0]->GetSizeY() : 0);
    }

    // Create alpha map textures from chunk data (layers 1-3 have alpha maps)
    TArray<UTexture2D*> AlphaTextures;
    for (int32 i = 0; i < ChunkData.AlphaMaps.Num() && i < 3; ++i)
    {
        if (ChunkData.AlphaMaps[i].Num() > 0)
        {
            FString AlphaName = FString::Printf(TEXT("Alpha_%d_%d_%d"), ChunkData.IndexX, ChunkData.IndexY, i);
            UTexture2D* Alpha = CreateAlphaTexture(ChunkData.AlphaMaps[i], AlphaName);
            AlphaTextures.Add(Alpha);
        }
        else
        {
            AlphaTextures.Add(nullptr);
        }
    }

    // Get base material and create dynamic instance
    UMaterial* BaseMat = GetBaseMaterial();
    if (!BaseMat)
    {
        UE_LOG(LogTerrainMat, Error, TEXT("Failed to get base terrain material"));
        return nullptr;
    }

    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, Outer);
    if (!MID)
    {
        UE_LOG(LogTerrainMat, Error, TEXT("Failed to create MID"));
        return nullptr;
    }

    // Debug: log material info once
    static bool bLoggedMat = false;
    if (!bLoggedMat)
    {
        bLoggedMat = true;
        UE_LOG(LogTerrainMat, Log, TEXT("MID parent: %s, IsComplete=%d"),
            *BaseMat->GetName(), BaseMat->IsComplete() ? 1 : 0);
    }

    // r310 (Stefan: terrain looks blurry, e.g. the slopes by the keep): the layer textures were
    // trilinear only, which smears at grazing angles. Use the world group's anisotropic filter,
    // like the original client's texture filtering option and our reed cards.
    for (UTexture2D* Layer : LayerTextures)
        if (Layer && Layer->Filter != TF_Default)
        {
            Layer->LODGroup = TEXTUREGROUP_World;
            Layer->Filter = TF_Default;
            Layer->UpdateResource();
        }

    // Set the base layer texture (always present)
    MID->SetTextureParameterValue(FName(TEXT("Layer0Texture")), LayerTextures[0]);
    UE_LOG(LogTerrainMat, Verbose, TEXT("Chunk [%d,%d]: assigned Layer0Texture %s"),
        ChunkData.IndexX, ChunkData.IndexY, *LayerTextures[0]->GetName());

    // Set additional layer textures and their alpha maps
    static const FName LayerParamNames[] = {
        FName(TEXT("Layer1Texture")),
        FName(TEXT("Layer2Texture")),
        FName(TEXT("Layer3Texture"))
    };
    static const FName AlphaParamNames[] = {
        FName(TEXT("AlphaMap1")),
        FName(TEXT("AlphaMap2")),
        FName(TEXT("AlphaMap3"))
    };

    for (int32 i = 0; i < 3; ++i)
    {
        if (i + 1 < LayerTextures.Num() && LayerTextures[i + 1])
        {
            MID->SetTextureParameterValue(LayerParamNames[i], LayerTextures[i + 1]);
        }
        if (i < AlphaTextures.Num() && AlphaTextures[i])
        {
            MID->SetTextureParameterValue(AlphaParamNames[i], AlphaTextures[i]);
        }
    }

    // r732 (Stefan 22.10: "Add it all to the cobblestone and bricks"): ground layers that are cobbles, bricks or
    // flagstones (Elwynn's road outside Stormwind is ElwynnCobbleStoneBase) get the stone relief that
    // WowPrepareTerrainStoneRelief adds to the terrain material; every other layer stays as it was.
    static const FName ReliefParamNames[] = {
        FName(TEXT("Layer0Relief")), FName(TEXT("Layer1Relief")), FName(TEXT("Layer2Relief")), FName(TEXT("Layer3Relief"))
    };
    const bool bStoneReliefOption = FWowClientPreferences::GetWorldOption(TEXT("stonereliefground"));
    for (int32 i = 0; i < 4 && i < ChunkData.TextureIndices.Num(); ++i)
    {
        if (!LayerTextures.IsValidIndex(i) || !LayerTextures[i] || !AdtData.TexturePaths.IsValidIndex(ChunkData.TextureIndices[i]))
            continue;
        if (bDefaultGround && i == 0)
            continue; // r735: the black default ground has no stones
        const FString& LayerPath = AdtData.TexturePaths[ChunkData.TextureIndices[i]];
        const FString Leaf = FPaths::GetBaseFilename(LayerPath);
        bool bStone = false;
        for (const TCHAR* Word : {TEXT("Cobble"), TEXT("Brick"), TEXT("Flagstone"), TEXT("Paving"), TEXT("Paved")})
            bStone |= Leaf.Contains(Word, ESearchCase::IgnoreCase);
        // r740 (Stefan 23.10: revert the stone relief before the release, "ground needs more work"): only with Options >
        // Experimental > World & Sky "Stone relief on streets and ground" on (off by default). Off: Layer<N>Relief stays 0,
        // the ground looks as before r732.
        bStone = bStone && bStoneReliefOption;
        static TSet<FString> LoggedReliefLayers;
        if (bStone && !LoggedReliefLayers.Contains(LayerPath) && LoggedReliefLayers.Num() < 256)
        {
            LoggedReliefLayers.Add(LayerPath);
            UE_LOG(LogTerrainMat, Log, TEXT("Ground layer %s: stone relief yes"), *LayerPath);
        }
        if (bStone)
            MID->SetScalarParameterValue(ReliefParamNames[i], 1.0f);
    }

    return MID;
}

UMaterial* FWowTerrainMaterial::GetBaseMaterial()
{
    static TStrongObjectPtr<UMaterial> RetainedMaterial;
    UMaterial* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
    if (CachedMat) return CachedMat;

    // Try loading a pre-compiled material asset (created by Scripts/CreateTerrainPreviewMaterial.py)
    const TCHAR* MatPaths[] = {
        TEXT("/Game/Wow/Materials/M_WowTerrainPreview"),
        TEXT("/Game/Materials/M_WowTerrain"),
        TEXT("/Game/Materials/M_WowTerrainRuntime")
    };
    for (const TCHAR* Path : MatPaths)
    {
        FSoftObjectPath MatPath(FString(Path) + TEXT(".") + FPaths::GetBaseFilename(Path));
        if (MatPath.ResolveObject() || FPackageName::DoesPackageExist(MatPath.GetLongPackageName()))
        {
            CachedMat = LoadObject<UMaterial>(nullptr, Path);
            if (CachedMat)
            {
                UE_LOG(LogTerrainMat, Log, TEXT("Loaded terrain material from %s"), Path);
#if WITH_EDITOR
                // Ensure shaders are compiled before using — on first run the Metal
                // shaders may not be cached yet, causing grey rendering.
                if (!CachedMat->IsComplete())
                {
                    UE_LOG(LogTerrainMat, Log, TEXT("Material shaders not compiled yet, compiling..."));
                    CachedMat->ForceRecompileForRendering();
                    if (GShaderCompilingManager)
                    {
                        GShaderCompilingManager->FinishAllCompilation();
                    }
                    UE_LOG(LogTerrainMat, Log, TEXT("Material shader compilation done, IsComplete=%d"),
                        CachedMat->IsComplete() ? 1 : 0);
                }
#endif
                return CachedMat;
            }
        }
    }

#if WITH_EDITOR
    // Build the full 4-layer terrain splat material graph programmatically.
    // Layout:
    //   UV0 (0..1 per chunk) → alpha map samplers
    //   UV1 (world-space tiled) → ground texture samplers
    //   Lerp chain: Base → Layer1 (Alpha1) → Layer2 (Alpha2) → Layer3 (Alpha3) → BaseColor
    UE_LOG(LogTerrainMat, Log, TEXT("Creating 4-layer terrain splat material"));

    UPackage* MatPackage = CreatePackage(TEXT("/Game/Materials/M_WowTerrainRuntime"));
    CachedMat = NewObject<UMaterial>(MatPackage, TEXT("M_WowTerrainRuntime"),
        RF_Public | RF_Standalone);
    CachedMat->SetShadingModel(MSM_Unlit);
    CachedMat->TwoSided = false;

    UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    UTexture2D* BlackTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black"));

    auto& Exprs = CachedMat->GetExpressionCollection();

    // --- Texture coordinate nodes ---
    // UV1 = world-space tiled UVs for ground texture sampling
    auto* TexCoordTiled = NewObject<UMaterialExpressionTextureCoordinate>(CachedMat);
    TexCoordTiled->CoordinateIndex = 1;
    TexCoordTiled->MaterialExpressionEditorX = -800;
    TexCoordTiled->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(TexCoordTiled);

    // UV0 = per-chunk [0,1] for alpha/splatmap lookup
    auto* TexCoordAlpha = NewObject<UMaterialExpressionTextureCoordinate>(CachedMat);
    TexCoordAlpha->CoordinateIndex = 0;
    TexCoordAlpha->MaterialExpressionEditorX = -800;
    TexCoordAlpha->MaterialExpressionEditorY = 800;
    Exprs.AddExpression(TexCoordAlpha);

    // --- 4 ground texture samplers (use UV1 for world-space tiling) ---
    const TCHAR* GroundParamNames[] = {
        TEXT("Layer0Texture"), TEXT("Layer1Texture"), TEXT("Layer2Texture"), TEXT("Layer3Texture")
    };
    UMaterialExpressionTextureSampleParameter2D* GroundSamplers[4];

    for (int32 i = 0; i < 4; ++i)
    {
        auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(CachedMat);
        Sampler->ParameterName = FName(GroundParamNames[i]);
        Sampler->SamplerType = SAMPLERTYPE_Color;
        Sampler->Texture = (i == 0) ? WhiteTex : BlackTex;
        Sampler->Coordinates.Connect(0, TexCoordTiled);
        Sampler->MaterialExpressionEditorX = -600;
        Sampler->MaterialExpressionEditorY = i * 200;
        Exprs.AddExpression(Sampler);
        GroundSamplers[i] = Sampler;
    }

    // --- 3 alpha map samplers (use UV0 for per-chunk splatmap) ---
    const TCHAR* AlphaParamNamesArr[] = {
        TEXT("AlphaMap1"), TEXT("AlphaMap2"), TEXT("AlphaMap3")
    };
    UMaterialExpressionTextureSampleParameter2D* AlphaSamplers[3];
    UMaterialExpressionComponentMask* AlphaMasks[3];

    for (int32 i = 0; i < 3; ++i)
    {
        auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(CachedMat);
        Sampler->ParameterName = FName(AlphaParamNamesArr[i]);
        Sampler->SamplerType = SAMPLERTYPE_Color;
        Sampler->Texture = BlackTex;
        Sampler->Coordinates.Connect(0, TexCoordAlpha);
        Sampler->MaterialExpressionEditorX = -600;
        Sampler->MaterialExpressionEditorY = 800 + i * 200;
        Exprs.AddExpression(Sampler);
        AlphaSamplers[i] = Sampler;

        auto* Mask = NewObject<UMaterialExpressionComponentMask>(CachedMat);
        Mask->R = true;
        Mask->G = false;
        Mask->B = false;
        Mask->A = false;
        Mask->MaterialExpressionEditorX = -400;
        Mask->MaterialExpressionEditorY = 800 + i * 200;
        Mask->Input.Connect(0, Sampler);
        Exprs.AddExpression(Mask);
        AlphaMasks[i] = Mask;
    }

    // --- Lerp chain: blend layers using alpha maps ---
    // Lerp1 = lerp(Base, Layer1, Alpha1)
    auto* Lerp1 = NewObject<UMaterialExpressionLinearInterpolate>(CachedMat);
    Lerp1->MaterialExpressionEditorX = -200;
    Lerp1->MaterialExpressionEditorY = 0;
    Lerp1->A.Connect(0, GroundSamplers[0]); // Base RGB
    Lerp1->B.Connect(0, GroundSamplers[1]); // Layer1 RGB
    Lerp1->Alpha.Connect(0, AlphaMasks[0]); // Alpha1 R
    Exprs.AddExpression(Lerp1);

    // Lerp2 = lerp(Lerp1, Layer2, Alpha2)
    auto* Lerp2 = NewObject<UMaterialExpressionLinearInterpolate>(CachedMat);
    Lerp2->MaterialExpressionEditorX = -0;
    Lerp2->MaterialExpressionEditorY = 100;
    Lerp2->A.Connect(0, Lerp1);
    Lerp2->B.Connect(0, GroundSamplers[2]); // Layer2 RGB
    Lerp2->Alpha.Connect(0, AlphaMasks[1]); // Alpha2 R
    Exprs.AddExpression(Lerp2);

    // Lerp3 = lerp(Lerp2, Layer3, Alpha3)
    auto* Lerp3 = NewObject<UMaterialExpressionLinearInterpolate>(CachedMat);
    Lerp3->MaterialExpressionEditorX = 200;
    Lerp3->MaterialExpressionEditorY = 200;
    Lerp3->A.Connect(0, Lerp2);
    Lerp3->B.Connect(0, GroundSamplers[3]); // Layer3 RGB
    Lerp3->Alpha.Connect(0, AlphaMasks[2]); // Alpha3 R
    Exprs.AddExpression(Lerp3);

    // --- Connect final lerp output to BaseColor + roughness/specular ---
    CachedMat->GetEditorOnlyData()->BaseColor.Connect(0, Lerp3);

    auto* RoughnessConst = NewObject<UMaterialExpressionConstant>(CachedMat);
    RoughnessConst->R = 0.85f; // Slightly less than full rough for subtle sheen on terrain
    RoughnessConst->MaterialExpressionEditorX = 200;
    RoughnessConst->MaterialExpressionEditorY = 400;
    Exprs.AddExpression(RoughnessConst);
    CachedMat->GetEditorOnlyData()->Roughness.Connect(0, RoughnessConst);

    auto* SpecularConst = NewObject<UMaterialExpressionConstant>(CachedMat);
    SpecularConst->R = 0.15f; // Small amount of specular for natural terrain
    SpecularConst->MaterialExpressionEditorX = 200;
    SpecularConst->MaterialExpressionEditorY = 500;
    Exprs.AddExpression(SpecularConst);
    CachedMat->GetEditorOnlyData()->Specular.Connect(0, SpecularConst);

    // Compile the material — must be in a real package for Metal shader compilation
    CachedMat->PreEditChange(nullptr);
    CachedMat->PostEditChange();
    CachedMat->ForceRecompileForRendering();

    // Block until shader compilation is done — otherwise MIDs will render grey
    if (GShaderCompilingManager)
    {
        UE_LOG(LogTerrainMat, Log, TEXT("Waiting for terrain material shader compilation..."));
        GShaderCompilingManager->FinishAllCompilation();
        UE_LOG(LogTerrainMat, Log, TEXT("Shader compilation complete, IsComplete=%d"), CachedMat->IsComplete() ? 1 : 0);
    }

    // Save the material to disk so it persists for subsequent launches
    {
        FString PackageFilename = FPackageName::LongPackageNameToFilename(
            TEXT("/Game/Materials/M_WowTerrainRuntime"), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(MatPackage, CachedMat, *PackageFilename, SaveArgs);
        UE_LOG(LogTerrainMat, Log, TEXT("Saved terrain material to: %s"), *PackageFilename);
    }

    UE_LOG(LogTerrainMat, Log, TEXT("4-layer terrain splat material created and compiled"));
#else
    // Non-editor builds: fall back to BasicShapeMaterial
    CachedMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    UE_LOG(LogTerrainMat, Warning, TEXT("No custom terrain material; using BasicShapeMaterial fallback"));
#endif

    return CachedMat;
}

UTexture2D* FWowTerrainMaterial::CreateAlphaTexture(const TArray<uint8>& AlphaData, const FString& Name)
{
    // Alpha maps in WoW ADTs are 64x64 grayscale
    const int32 AlphaSize = 64;
    const int32 ExpectedBytes = AlphaSize * AlphaSize;

    if (AlphaData.Num() < ExpectedBytes)
    {
        UE_LOG(LogTemp, Warning, TEXT("Alpha map '%s' has %d bytes, expected %d"), *Name, AlphaData.Num(), ExpectedBytes);
        return nullptr;
    }

    // Alpha maps are generated per chunk at runtime, so they need unique transient names
    // to avoid replacing textures that are still referenced by already-loaded tiles.
    const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), *Name);
    UTexture2D* Tex = UTexture2D::CreateTransient(AlphaSize, AlphaSize, PF_G8, UniqueName);
    if (!Tex) return nullptr;

    Tex->Filter = TF_Trilinear;
    Tex->SRGB = false; // Alpha data is linear
    Tex->NeverStream = true;
    Tex->LODGroup = TEXTUREGROUP_Terrain_Weightmap;
    Tex->AddressX = TA_Clamp;
    Tex->AddressY = TA_Clamp;

    FTexturePlatformData* PlatformData = Tex->GetPlatformData();
    if (!PlatformData)
    {
        UE_LOG(LogTemp, Warning, TEXT("Alpha map '%s' has no platform data"), *Name);
        return nullptr;
    }

    // Generate the complete weight-map mip chain. A single 64x64 mip made
    // entire ADT chunks switch texture detail in visible rectangular steps.
    TArray<uint8> CurrentMip;
    CurrentMip.Append(AlphaData.GetData(), ExpectedBytes);
    int32 MipWidth = AlphaSize;
    int32 MipHeight = AlphaSize;
    PlatformData->Mips.Empty();

    while (true)
    {
        FTexture2DMipMap* DestinationMip = new FTexture2DMipMap();
        PlatformData->Mips.Add(DestinationMip);
        DestinationMip->SizeX = MipWidth;
        DestinationMip->SizeY = MipHeight;
        void* DestinationData = DestinationMip->BulkData.Lock(LOCK_READ_WRITE);
        DestinationData = DestinationMip->BulkData.Realloc(CurrentMip.Num());
        FMemory::Memcpy(DestinationData, CurrentMip.GetData(), CurrentMip.Num());
        DestinationMip->BulkData.Unlock();

        if (MipWidth == 1 && MipHeight == 1)
        {
            break;
        }

        CurrentMip = DownsampleAlphaMip(CurrentMip, MipWidth, MipHeight);
        MipWidth = FMath::Max(MipWidth / 2, 1);
        MipHeight = FMath::Max(MipHeight / 2, 1);
    }

    Tex->UpdateResource();

    return Tex;
}

UTexture2D* FWowTerrainMaterial::LoadBlpTexture(const FString& Path, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (Path.IsEmpty() || !Mpq) return nullptr;

    // Check cache first
    if (Cache)
    {
        UTexture2D* Cached = Cache->FindTexture(Path);
        if (Cached) return Cached;
    }

    // Read BLP file from MPQ
    TArray<uint8> FileData;
    if (!Mpq->ReadFile(Path, FileData))
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to read texture from MPQ: %s"), *Path);
        return nullptr;
    }

    // Parse BLP
    FBlpTexture BlpData = FBlpParser::Parse(FileData);
    if (!BlpData.bIsValid)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to parse BLP texture: %s"), *Path);
        return nullptr;
    }

    // Create UTexture2D
    FString TexName = FPaths::GetBaseFilename(Path);
    UTexture2D* Tex = FWowTextureFactory::CreateTexture(BlpData, TexName);
    if (!Tex)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to create texture: %s"), *Path);
        return nullptr;
    }

    // Terrain textures should tile
    Tex->AddressX = TA_Wrap;
    Tex->AddressY = TA_Wrap;

    // Cache it
    if (Cache)
    {
        Cache->CacheTexture(Path, Tex);
    }

    return Tex;
}

UMaterial* FWowTerrainMaterial::GetSimpleObjectMaterial()
{
    static TStrongObjectPtr<UMaterial> RetainedMaterial;
    UMaterial* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
    if (CachedMat) return CachedMat;

    // Try loading pre-compiled version first
    const TCHAR* MatPaths[] = {
        // V2: also usable on skeletal meshes (animated doodads such as flying birds drew the
        // grey default material with V1, which lacked the SkeletalMesh usage flag).
        TEXT("/Game/Wow/Materials/M_WowSimpleObjectV2"),
        TEXT("/Game/Materials/M_WowSimpleObjectV2")
    };
    for (const TCHAR* Path : MatPaths)
    {
        FSoftObjectPath MatPath(FString(Path) + TEXT(".") + FPaths::GetBaseFilename(Path));
        if (MatPath.ResolveObject() || FPackageName::DoesPackageExist(MatPath.GetLongPackageName()))
        {
            CachedMat = LoadObject<UMaterial>(nullptr, Path);
            if (CachedMat)
            {
                UE_LOG(LogTerrainMat, Log, TEXT("Loaded simple object material from %s"), Path);
#if WITH_EDITOR
                if (!CachedMat->IsComplete())
                {
                    CachedMat->ForceRecompileForRendering();
                    if (GShaderCompilingManager)
                        GShaderCompilingManager->FinishAllCompilation();
                }
#endif
                return CachedMat;
            }
        }
    }

#if WITH_EDITOR
    // Build a simple single-texture material: samples UV0 with one texture parameter
    UE_LOG(LogTerrainMat, Log, TEXT("Creating simple object material (UV0-based)"));

    UPackage* MatPackage = CreatePackage(TEXT("/Game/Materials/M_WowSimpleObjectV2"));
    CachedMat = NewObject<UMaterial>(MatPackage, TEXT("M_WowSimpleObjectV2"),
        RF_Public | RF_Standalone);
    CachedMat->SetShadingModel(MSM_DefaultLit);
    CachedMat->BlendMode = BLEND_Masked; // Alpha masking for foliage/transparent textures
    CachedMat->TwoSided = true; // Foliage needs two-sided rendering
    CachedMat->bUsedWithInstancedStaticMeshes = true; // Required for HISMC/ISM rendering
    CachedMat->bUsedWithSkeletalMesh = true; // animated doodads (birds, banners)

    UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));

    auto& Exprs = CachedMat->GetExpressionCollection();

    // UV0 for texture sampling (standard object UVs)
    auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(CachedMat);
    TexCoord->CoordinateIndex = 0;
    TexCoord->MaterialExpressionEditorX = -400;
    TexCoord->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(TexCoord);

    // Single texture parameter
    auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(CachedMat);
    Sampler->ParameterName = FName(TEXT("Layer0Texture"));
    Sampler->SamplerType = SAMPLERTYPE_Color;
    Sampler->Texture = WhiteTex;
    Sampler->Coordinates.Connect(0, TexCoord);
    Sampler->MaterialExpressionEditorX = -200;
    Sampler->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(Sampler);

    CachedMat->GetEditorOnlyData()->BaseColor.Connect(0, Sampler);
    CachedMat->GetEditorOnlyData()->OpacityMask.Connect(4, Sampler); // Output 4 = Alpha channel

    auto* RoughnessConst = NewObject<UMaterialExpressionConstant>(CachedMat);
    RoughnessConst->R = 0.8f;
    RoughnessConst->MaterialExpressionEditorX = 0;
    RoughnessConst->MaterialExpressionEditorY = 200;
    Exprs.AddExpression(RoughnessConst);
    CachedMat->GetEditorOnlyData()->Roughness.Connect(0, RoughnessConst);

    CachedMat->PreEditChange(nullptr);
    CachedMat->PostEditChange();
    CachedMat->ForceRecompileForRendering();

    if (GShaderCompilingManager)
    {
        GShaderCompilingManager->FinishAllCompilation();
        UE_LOG(LogTerrainMat, Log, TEXT("Simple object material compiled, IsComplete=%d"),
            CachedMat->IsComplete() ? 1 : 0);
    }

    // Save to disk
    {
        FString PackageFilename = FPackageName::LongPackageNameToFilename(
            TEXT("/Game/Materials/M_WowSimpleObjectV2"), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(MatPackage, CachedMat, *PackageFilename, SaveArgs);
    }
#else
    CachedMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
#endif

    return CachedMat;
}

// r306 (Stefan: the gypsy wagon's ramp showed twice): the plank is a thin box whose underside
// showed through from above because every M2 pass was drawn two-sided. The original client culls
// back faces unless the material has render flag 0x04 (two-sided).
UMaterial* FWowTerrainMaterial::GetSimpleObjectMaterialSingleSided()
{
    static TStrongObjectPtr<UMaterial> RetainedMaterial;
    UMaterial* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
    if (CachedMat) return CachedMat;

    // Try loading pre-compiled version first
    const TCHAR* MatPaths[] = {
        // V2: also usable on skeletal meshes (animated doodads such as flying birds drew the
        // grey default material with V1, which lacked the SkeletalMesh usage flag).
        TEXT("/Game/Wow/Materials/M_WowSimpleObjectSingleSidedV1"),
        TEXT("/Game/Materials/M_WowSimpleObjectSingleSidedV1")
    };
    for (const TCHAR* Path : MatPaths)
    {
        FSoftObjectPath MatPath(FString(Path) + TEXT(".") + FPaths::GetBaseFilename(Path));
        if (MatPath.ResolveObject() || FPackageName::DoesPackageExist(MatPath.GetLongPackageName()))
        {
            CachedMat = LoadObject<UMaterial>(nullptr, Path);
            if (CachedMat)
            {
                UE_LOG(LogTerrainMat, Log, TEXT("Loaded simple object material from %s"), Path);
#if WITH_EDITOR
                if (!CachedMat->IsComplete())
                {
                    CachedMat->ForceRecompileForRendering();
                    if (GShaderCompilingManager)
                        GShaderCompilingManager->FinishAllCompilation();
                }
#endif
                return CachedMat;
            }
        }
    }

#if WITH_EDITOR
    // Build a simple single-texture material: samples UV0 with one texture parameter
    UE_LOG(LogTerrainMat, Log, TEXT("Creating single-sided simple object material (UV0-based)"));

    UPackage* MatPackage = CreatePackage(TEXT("/Game/Materials/M_WowSimpleObjectSingleSidedV1"));
    CachedMat = NewObject<UMaterial>(MatPackage, TEXT("M_WowSimpleObjectSingleSidedV1"),
        RF_Public | RF_Standalone);
    CachedMat->SetShadingModel(MSM_DefaultLit);
    CachedMat->BlendMode = BLEND_Masked; // Alpha masking for foliage/transparent textures
    CachedMat->TwoSided = false; // r306: M2 passes without render flag 0x04 are culled like the original client
    CachedMat->bUsedWithInstancedStaticMeshes = true; // Required for HISMC/ISM rendering
    CachedMat->bUsedWithSkeletalMesh = true; // animated doodads (birds, banners)

    UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));

    auto& Exprs = CachedMat->GetExpressionCollection();

    // UV0 for texture sampling (standard object UVs)
    auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(CachedMat);
    TexCoord->CoordinateIndex = 0;
    TexCoord->MaterialExpressionEditorX = -400;
    TexCoord->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(TexCoord);

    // Single texture parameter
    auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(CachedMat);
    Sampler->ParameterName = FName(TEXT("Layer0Texture"));
    Sampler->SamplerType = SAMPLERTYPE_Color;
    Sampler->Texture = WhiteTex;
    Sampler->Coordinates.Connect(0, TexCoord);
    Sampler->MaterialExpressionEditorX = -200;
    Sampler->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(Sampler);

    CachedMat->GetEditorOnlyData()->BaseColor.Connect(0, Sampler);
    CachedMat->GetEditorOnlyData()->OpacityMask.Connect(4, Sampler); // Output 4 = Alpha channel

    auto* RoughnessConst = NewObject<UMaterialExpressionConstant>(CachedMat);
    RoughnessConst->R = 0.8f;
    RoughnessConst->MaterialExpressionEditorX = 0;
    RoughnessConst->MaterialExpressionEditorY = 200;
    Exprs.AddExpression(RoughnessConst);
    CachedMat->GetEditorOnlyData()->Roughness.Connect(0, RoughnessConst);

    CachedMat->PreEditChange(nullptr);
    CachedMat->PostEditChange();
    CachedMat->ForceRecompileForRendering();

    if (GShaderCompilingManager)
    {
        GShaderCompilingManager->FinishAllCompilation();
        UE_LOG(LogTerrainMat, Log, TEXT("Simple object material compiled, IsComplete=%d"),
            CachedMat->IsComplete() ? 1 : 0);
    }

    // Save to disk
    {
        FString PackageFilename = FPackageName::LongPackageNameToFilename(
            TEXT("/Game/Materials/M_WowSimpleObjectSingleSidedV1"), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(MatPackage, CachedMat, *PackageFilename, SaveArgs);
    }
#else
    CachedMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
#endif

    return CachedMat;
}

UMaterial* FWowTerrainMaterial::GetSimpleAlphaKeyObjectMaterial()
{
    static TStrongObjectPtr<UMaterial> Retained;
    if (Retained.IsValid()) return Retained.Get();
    const TCHAR* Name = TEXT("M_WowSimpleAlphaKeyObjectV1");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->SetShadingModel(MSM_DefaultLit);
        Material->BlendMode = BLEND_Masked;
        // WoW's GxBlend_AlphaKey alpha reference (224/255).
        Material->OpacityMaskClipValue = 0.878f;
        Material->TwoSided = true;
        Material->bUsedWithInstancedStaticMeshes = true;
        Material->bUsedWithSkeletalMesh = true; // animated doodads (birds): default material without it
        auto& Exprs = Material->GetExpressionCollection();
        auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
        TexCoord->CoordinateIndex = 0;
        Exprs.AddExpression(TexCoord);
        auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Sampler->ParameterName = FName(TEXT("Layer0Texture"));
        Sampler->SamplerType = SAMPLERTYPE_Color;
        Sampler->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
        Sampler->Coordinates.Connect(0, TexCoord);
        Exprs.AddExpression(Sampler);
        Material->GetEditorOnlyData()->BaseColor.Connect(0, Sampler);
        Material->GetEditorOnlyData()->OpacityMask.Connect(4, Sampler);
        auto* Roughness = NewObject<UMaterialExpressionConstant>(Material);
        Roughness->R = 0.8f;
        Exprs.AddExpression(Roughness);
        Material->GetEditorOnlyData()->Roughness.Connect(0, Roughness);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (!Material) Material = GetSimpleObjectMaterial();
    Retained.Reset(Material);
    return Material;
}

UMaterial* FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial(bool bTwoSided)
{
    static TStrongObjectPtr<UMaterial> RetainedMaterials[2];
    auto& RetainedMaterial = RetainedMaterials[bTwoSided ? 1 : 0];
    UMaterial* CachedMat = RetainedMaterial.Get();
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
    if (CachedMat) return CachedMat;

    const FString MaterialName = bTwoSided ? TEXT("M_WowSimpleOpaqueObjectV1")
        : TEXT("M_WowSingleSidedOpaqueObjectV1");
    const FString PackagePath = TEXT("/Game/Materials/") + MaterialName;
    const FString MatPaths[] = {TEXT("/Game/Wow/Materials/") + MaterialName, PackagePath};
    for (const FString& MaterialPath : MatPaths)
    {
        const TCHAR* Path = *MaterialPath;
        FSoftObjectPath MatPath(FString(Path) + TEXT(".") + FPaths::GetBaseFilename(Path));
        if (MatPath.ResolveObject() || FPackageName::DoesPackageExist(MatPath.GetLongPackageName()))
        {
            CachedMat = LoadObject<UMaterial>(nullptr, Path);
            if (CachedMat)
            {
#if WITH_EDITOR
                if (!CachedMat->IsComplete())
                {
                    CachedMat->ForceRecompileForRendering();
                    if (GShaderCompilingManager)
                    {
                        GShaderCompilingManager->FinishAllCompilation();
                    }
                }
#endif
                return CachedMat;
            }
        }
    }

#if WITH_EDITOR
    UPackage* MatPackage = CreatePackage(*PackagePath);
    CachedMat = NewObject<UMaterial>(MatPackage, *MaterialName,
        RF_Public | RF_Standalone);
    if (!CachedMat)
    {
        return nullptr;
    }

    CachedMat->SetShadingModel(MSM_DefaultLit);
    CachedMat->BlendMode = BLEND_Opaque;
    CachedMat->TwoSided = bTwoSided;

    UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    auto& Exprs = CachedMat->GetExpressionCollection();

    auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(CachedMat);
    TexCoord->CoordinateIndex = 0;
    TexCoord->MaterialExpressionEditorX = -400;
    TexCoord->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(TexCoord);

    auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(CachedMat);
    Sampler->ParameterName = FName(TEXT("Layer0Texture"));
    Sampler->SamplerType = SAMPLERTYPE_Color;
    Sampler->Texture = WhiteTex;
    Sampler->Coordinates.Connect(0, TexCoord);
    Sampler->MaterialExpressionEditorX = -200;
    Sampler->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(Sampler);
    CachedMat->GetEditorOnlyData()->BaseColor.Connect(0, Sampler);

    auto* RoughnessConst = NewObject<UMaterialExpressionConstant>(CachedMat);
    RoughnessConst->R = 0.8f;
    RoughnessConst->MaterialExpressionEditorX = 0;
    RoughnessConst->MaterialExpressionEditorY = 200;
    Exprs.AddExpression(RoughnessConst);
    CachedMat->GetEditorOnlyData()->Roughness.Connect(0, RoughnessConst);

    CachedMat->PreEditChange(nullptr);
    CachedMat->PostEditChange();
    CachedMat->ForceRecompileForRendering();
    if (GShaderCompilingManager)
    {
        GShaderCompilingManager->FinishAllCompilation();
    }

    FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(MatPackage, CachedMat, *PackageFilename, SaveArgs);
#else
    CachedMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
#endif

    return CachedMat;
}

UMaterial* FWowTerrainMaterial::GetSimpleFoliageObjectMaterial()
{
    static TStrongObjectPtr<UMaterial> RetainedMaterial;
    UMaterial* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
    if (CachedMat) return CachedMat;

    const TCHAR* MatPaths[] = {
        TEXT("/Game/Wow/Materials/M_WowSimpleFoliageObjectV6"),
        TEXT("/Game/Materials/M_WowSimpleFoliageObjectV6")
    };
    for (const TCHAR* Path : MatPaths)
    {
        FSoftObjectPath MatPath(FString(Path) + TEXT(".") + FPaths::GetBaseFilename(Path));
        if (MatPath.ResolveObject() || FPackageName::DoesPackageExist(MatPath.GetLongPackageName()))
        {
            CachedMat = LoadObject<UMaterial>(nullptr, Path);
            if (CachedMat)
            {
#if WITH_EDITOR
                if (!CachedMat->IsComplete())
                {
                    CachedMat->ForceRecompileForRendering();
                    if (GShaderCompilingManager)
                    {
                        GShaderCompilingManager->FinishAllCompilation();
                    }
                }
#endif
                return CachedMat;
            }
        }
    }

#if WITH_EDITOR
    UPackage* MatPackage = CreatePackage(TEXT("/Game/Materials/M_WowSimpleFoliageObjectV6"));
    CachedMat = NewObject<UMaterial>(MatPackage, TEXT("M_WowSimpleFoliageObjectV6"),
        RF_Public | RF_Standalone);
    if (!CachedMat)
    {
        return nullptr;
    }

    CachedMat->SetShadingModel(MSM_TwoSidedFoliage);
    CachedMat->BlendMode = BLEND_Masked;
    CachedMat->OpacityMaskClipValue = 0.24f;
    CachedMat->TwoSided = true;
    CachedMat->bUsedWithInstancedStaticMeshes = true;

    UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));

    auto& Exprs = CachedMat->GetExpressionCollection();

    auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(CachedMat);
    TexCoord->CoordinateIndex = 0;
    TexCoord->MaterialExpressionEditorX = -400;
    TexCoord->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(TexCoord);

    auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(CachedMat);
    Sampler->ParameterName = FName(TEXT("Layer0Texture"));
    Sampler->SamplerType = SAMPLERTYPE_Color;
    Sampler->Texture = WhiteTex;
    Sampler->Coordinates.Connect(0, TexCoord);
    Sampler->MaterialExpressionEditorX = -200;
    Sampler->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(Sampler);

    auto* AddRG = NewObject<UMaterialExpressionAdd>(CachedMat);
    AddRG->A.Connect(1, Sampler);
    AddRG->B.Connect(2, Sampler);
    AddRG->MaterialExpressionEditorX = 0;
    AddRG->MaterialExpressionEditorY = 160;
    Exprs.AddExpression(AddRG);

    auto* AddRGB = NewObject<UMaterialExpressionAdd>(CachedMat);
    AddRGB->A.Connect(0, AddRG);
    AddRGB->B.Connect(3, Sampler);
    AddRGB->MaterialExpressionEditorX = 180;
    AddRGB->MaterialExpressionEditorY = 160;
    Exprs.AddExpression(AddRGB);

    auto* BlackCutoff = NewObject<UMaterialExpressionConstant>(CachedMat);
    BlackCutoff->R = 0.045f;
    BlackCutoff->MaterialExpressionEditorX = 180;
    BlackCutoff->MaterialExpressionEditorY = 310;
    Exprs.AddExpression(BlackCutoff);

    auto* Zero = NewObject<UMaterialExpressionConstant>(CachedMat);
    Zero->R = 0.0f;
    Zero->MaterialExpressionEditorX = 360;
    Zero->MaterialExpressionEditorY = 260;
    Exprs.AddExpression(Zero);

    auto* One = NewObject<UMaterialExpressionConstant>(CachedMat);
    One->R = 1.0f;
    One->MaterialExpressionEditorX = 360;
    One->MaterialExpressionEditorY = 360;
    Exprs.AddExpression(One);

    auto* ChromaMask = NewObject<UMaterialExpressionIf>(CachedMat);
    ChromaMask->A.Connect(0, AddRGB);
    ChromaMask->B.Connect(0, BlackCutoff);
    ChromaMask->AGreaterThanB.Connect(0, One);
    ChromaMask->AEqualsB.Connect(0, Zero);
    ChromaMask->ALessThanB.Connect(0, Zero);
    ChromaMask->MaterialExpressionEditorX = 540;
    ChromaMask->MaterialExpressionEditorY = 210;
    Exprs.AddExpression(ChromaMask);

    auto* FinalMask = NewObject<UMaterialExpressionMultiply>(CachedMat);
    FinalMask->A.Connect(4, Sampler);
    FinalMask->B.Connect(0, ChromaMask);
    FinalMask->MaterialExpressionEditorX = 760;
    FinalMask->MaterialExpressionEditorY = 120;
    Exprs.AddExpression(FinalMask);

    // Two-sided foliage keeps the painted card backs lit without bypassing the
    // sun, cloud shadows, or the day/night cycle. Some classic foliage BLPs
    // carry black RGB padding with little/no alpha, so retain the foliage-only
    // chroma mask instead of treating the complete card as opaque.
    CachedMat->GetEditorOnlyData()->BaseColor.Connect(0, Sampler);
    CachedMat->GetEditorOnlyData()->OpacityMask.Connect(0, FinalMask);

    auto* SubsurfaceTint = NewObject<UMaterialExpressionConstant3Vector>(CachedMat);
    SubsurfaceTint->Constant = FLinearColor(0.32f, 0.40f, 0.24f);
    SubsurfaceTint->MaterialExpressionEditorX = 560;
    SubsurfaceTint->MaterialExpressionEditorY = -100;
    Exprs.AddExpression(SubsurfaceTint);

    auto* SubsurfaceColor = NewObject<UMaterialExpressionMultiply>(CachedMat);
    SubsurfaceColor->A.Connect(0, Sampler);
    SubsurfaceColor->B.Connect(0, SubsurfaceTint);
    SubsurfaceColor->MaterialExpressionEditorX = 760;
    SubsurfaceColor->MaterialExpressionEditorY = -100;
    Exprs.AddExpression(SubsurfaceColor);
    CachedMat->GetEditorOnlyData()->SubsurfaceColor.Connect(0, SubsurfaceColor);

    auto* Roughness = NewObject<UMaterialExpressionConstant>(CachedMat);
    Roughness->R = 0.88f;
    Roughness->MaterialExpressionEditorX = 760;
    Roughness->MaterialExpressionEditorY = -230;
    Exprs.AddExpression(Roughness);
    CachedMat->GetEditorOnlyData()->Roughness.Connect(0, Roughness);

    auto* Specular = NewObject<UMaterialExpressionConstant>(CachedMat);
    Specular->R = 0.18f;
    Specular->MaterialExpressionEditorX = 760;
    Specular->MaterialExpressionEditorY = -330;
    Exprs.AddExpression(Specular);
    CachedMat->GetEditorOnlyData()->Specular.Connect(0, Specular);

    CachedMat->PreEditChange(nullptr);
    CachedMat->PostEditChange();
    CachedMat->ForceRecompileForRendering();

    if (GShaderCompilingManager)
    {
        GShaderCompilingManager->FinishAllCompilation();
    }

    FString PackageFilename = FPackageName::LongPackageNameToFilename(
            TEXT("/Game/Materials/M_WowSimpleFoliageObjectV6"), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(MatPackage, CachedMat, *PackageFilename, SaveArgs);

    UE_LOG(LogTerrainMat, Log, TEXT("Created lit two-sided masked foliage object material V6"));
#else
    CachedMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
#endif

    return CachedMat;
}

static UMaterial* CreateSimpleAlphaObjectMaterial(
    const TCHAR* PackagePath,
    const TCHAR* AssetName,
    EBlendMode BlendMode,
    bool bAdditive)
{
#if WITH_EDITOR
    UPackage* MatPackage = CreatePackage(PackagePath);
    UMaterial* Material = NewObject<UMaterial>(MatPackage, AssetName, RF_Public | RF_Standalone);
    if (!Material)
    {
        return nullptr;
    }

    Material->SetShadingModel(MSM_Unlit);
    Material->BlendMode = BlendMode;
    Material->TwoSided = true;
    Material->bUsedWithInstancedStaticMeshes = true;

    UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));

    auto& Exprs = Material->GetExpressionCollection();

    UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
    TexCoord->CoordinateIndex = 0;
    TexCoord->MaterialExpressionEditorX = -400;
    TexCoord->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(TexCoord);

    UMaterialExpressionTextureSampleParameter2D* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Sampler->ParameterName = FName(TEXT("Layer0Texture"));
    Sampler->SamplerType = SAMPLERTYPE_Color;
    Sampler->Texture = WhiteTex;
    Sampler->Coordinates.Connect(0, TexCoord);
    Sampler->MaterialExpressionEditorX = -200;
    Sampler->MaterialExpressionEditorY = 0;
    Exprs.AddExpression(Sampler);

    // M2 effect doodads (waterfalls, glows, smoke sheets) must use their alpha
    // channel as opacity. Treat them as unlit so black texture backgrounds do
    // not become opaque black planes in the world.
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Sampler);
    Material->GetEditorOnlyData()->Opacity.Connect(4, Sampler);

    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Material->ForceRecompileForRendering();

    if (GShaderCompilingManager)
    {
        GShaderCompilingManager->FinishAllCompilation();
    }

    FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(MatPackage, Material, *PackageFilename, SaveArgs);

    UE_LOG(LogTerrainMat, Log, TEXT("Created %s simple object material: %s"),
        bAdditive ? TEXT("additive") : TEXT("translucent"), PackagePath);
    return Material;
#else
    return LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
#endif
}

UMaterial* FWowTerrainMaterial::GetSimpleTranslucentObjectMaterial()
{
    static TStrongObjectPtr<UMaterial> RetainedMaterial;
    UMaterial* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
    if (CachedMat) return CachedMat;

    const TCHAR* MatPaths[] = {
        TEXT("/Game/Wow/Materials/M_WowSimpleObjectTranslucent"),
        TEXT("/Game/Materials/M_WowSimpleObjectTranslucent")
    };
    for (const TCHAR* Path : MatPaths)
    {
        FSoftObjectPath MatPath(FString(Path) + TEXT(".") + FPaths::GetBaseFilename(Path));
        if (MatPath.ResolveObject() || FPackageName::DoesPackageExist(MatPath.GetLongPackageName()))
        {
            CachedMat = LoadObject<UMaterial>(nullptr, Path);
            if (CachedMat)
            {
#if WITH_EDITOR
                if (!CachedMat->IsComplete())
                {
                    CachedMat->ForceRecompileForRendering();
                    if (GShaderCompilingManager)
                    {
                        GShaderCompilingManager->FinishAllCompilation();
                    }
                }
#endif
                return CachedMat;
            }
        }
    }

    CachedMat = CreateSimpleAlphaObjectMaterial(
        TEXT("/Game/Materials/M_WowSimpleObjectTranslucent"),
        TEXT("M_WowSimpleObjectTranslucent"),
        BLEND_Translucent,
        false);
    return CachedMat;
}

UMaterial* FWowTerrainMaterial::GetSimpleAdditiveObjectMaterial()
{
    static TStrongObjectPtr<UMaterial> RetainedMaterial;
    UMaterial* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
    if (CachedMat) return CachedMat;

    const TCHAR* MatPaths[] = {
        TEXT("/Game/Wow/Materials/M_WowSimpleObjectAdditive"),
        TEXT("/Game/Materials/M_WowSimpleObjectAdditive")
    };
    for (const TCHAR* Path : MatPaths)
    {
        FSoftObjectPath MatPath(FString(Path) + TEXT(".") + FPaths::GetBaseFilename(Path));
        if (MatPath.ResolveObject() || FPackageName::DoesPackageExist(MatPath.GetLongPackageName()))
        {
            CachedMat = LoadObject<UMaterial>(nullptr, Path);
            if (CachedMat)
            {
#if WITH_EDITOR
                if (!CachedMat->IsComplete())
                {
                    CachedMat->ForceRecompileForRendering();
                    if (GShaderCompilingManager)
                    {
                        GShaderCompilingManager->FinishAllCompilation();
                    }
                }
#endif
                return CachedMat;
            }
        }
    }

    CachedMat = CreateSimpleAlphaObjectMaterial(
        TEXT("/Game/Materials/M_WowSimpleObjectAdditive"),
        TEXT("M_WowSimpleObjectAdditive"),
        BLEND_Additive,
        true);
    return CachedMat;
}

UMaterial* FWowTerrainMaterial::GetSimpleWaterfallObjectMaterial()
{
    static TWeakObjectPtr<UMaterial> Cached;
    if (!Cached.IsValid()) Cached = WowCookedMaterial::Load(TEXT("M_WowWaterfallCooked_v2")); // r688
    if (Cached.IsValid()) return Cached.Get();
#if WITH_EDITOR
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_UEWaterfallRuntime"), RF_Transient);
    Material->SetShadingModel(MSM_DefaultLit);
    Material->BlendMode = BLEND_Translucent;
    Material->TwoSided = true;
    Material->bUsedWithInstancedStaticMeshes = true;
    Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
    auto& Expressions = Material->GetExpressionCollection();
    auto Add = [&Expressions](auto* Node) { Expressions.AddExpression(Node); return Node; };
    auto Constant = [&](float Value)
    {
        auto* Node = Add(NewObject<UMaterialExpressionConstant>(Material));
        Node->R = Value; return Node;
    };
    auto Scalar = [&](const TCHAR* Name, float Value)
    {
        auto* Node = Add(NewObject<UMaterialExpressionScalarParameter>(Material));
        Node->ParameterName = Name; Node->DefaultValue = Value; return Node;
    };
    auto Multiply = [&](UMaterialExpression* A, UMaterialExpression* B)
    {
        auto* Node = Add(NewObject<UMaterialExpressionMultiply>(Material));
        Node->A.Connect(0, A); Node->B.Connect(0, B); return Node;
    };
    auto* UV = Add(NewObject<UMaterialExpressionTextureCoordinate>(Material));
    auto* Time = Add(NewObject<UMaterialExpressionTime>(Material));
    auto* Speed = Scalar(TEXT("FlowV"), -0.85f);
    auto* Phase = Add(NewObject<UMaterialExpressionAdd>(Material));
    Phase->A.Connect(0, Multiply(Time, Speed));
    Phase->B.Connect(0, Scalar(TEXT("FlowPhase"), 0.0f));
    auto* Flow = Add(NewObject<UMaterialExpressionAppendVector>(Material));
    Flow->A.Connect(0, Constant(0.0f)); Flow->B.Connect(0, Phase);
    auto* Tiling = Add(NewObject<UMaterialExpressionAppendVector>(Material));
    Tiling->A.Connect(0, Constant(2.0f)); Tiling->B.Connect(0, Constant(6.0f));
    auto* MovingUV = Add(NewObject<UMaterialExpressionAdd>(Material));
    MovingUV->A.Connect(0, Multiply(UV, Tiling)); MovingUV->B.Connect(0, Flow);

    // Exclusively UE Water assets. No WoW water textures, texture transforms,
    // emissive tint or blinking bands. Lighting comes from the actual scene.
    auto* Foam = Add(NewObject<UMaterialExpressionTextureSampleParameter2D>(Material));
    Foam->ParameterName = TEXT("UEFoamTexture");
    Foam->Texture = LoadObject<UTexture2D>(nullptr,
        TEXT("/Water/Textures/Foam/T_WaterFlow_03_Foam_Tiled.T_WaterFlow_03_Foam_Tiled"));
    Foam->SamplerType = Foam->Texture && Foam->Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
    Foam->Coordinates.Connect(0, MovingUV);
    auto* FoamA = Add(NewObject<UMaterialExpressionComponentMask>(Material));
    FoamA->R = true; FoamA->Input.Connect(0, Foam);
    // r688 (Stefan, the Northshire fall at night: "the waterfall is also looking a bit dead" - a thin brown veil
    // over the cliff): a second, finer foam layer falling 1.6x faster over the first, so streaks overtake each
    // other and the white water keeps breaking up.
    auto* Tiling2 = Add(NewObject<UMaterialExpressionAppendVector>(Material));
    Tiling2->A.Connect(0, Constant(3.1f)); Tiling2->B.Connect(0, Constant(9.0f));
    auto* Flow2 = Add(NewObject<UMaterialExpressionAppendVector>(Material));
    Flow2->A.Connect(0, Constant(0.37f)); Flow2->B.Connect(0, Multiply(Phase, Constant(1.6f)));
    auto* MovingUV2 = Add(NewObject<UMaterialExpressionAdd>(Material));
    MovingUV2->A.Connect(0, Multiply(UV, Tiling2)); MovingUV2->B.Connect(0, Flow2);
    auto* Foam2 = Add(NewObject<UMaterialExpressionTextureSampleParameter2D>(Material));
    Foam2->ParameterName = TEXT("UEFoamTexture2");
    Foam2->Texture = Foam->Texture; Foam2->SamplerType = Foam->SamplerType;
    Foam2->Coordinates.Connect(0, MovingUV2);
    auto* FoamB = Add(NewObject<UMaterialExpressionComponentMask>(Material));
    FoamB->R = true; FoamB->Input.Connect(0, Foam2);
    auto* FoamMax = Add(NewObject<UMaterialExpressionMax>(Material));
    FoamMax->A.Connect(0, FoamA); FoamMax->B.Connect(0, FoamB);
    auto* FoamAmount = Add(NewObject<UMaterialExpressionSaturate>(Material));
    FoamAmount->Input.Connect(0, Multiply(FoamMax, Constant(1.25f)));
    auto* Normal = Add(NewObject<UMaterialExpressionTextureSampleParameter2D>(Material));
    Normal->ParameterName = TEXT("UEWaterNormal");
    Normal->Texture = LoadObject<UTexture2D>(nullptr,
        TEXT("/Water/Textures/Foam/T_WaterFlow_03_Foam_Tiled_Normals.T_WaterFlow_03_Foam_Tiled_Normals"));
    Normal->SamplerType = SAMPLERTYPE_Normal;
    Normal->Coordinates.Connect(0, MovingUV);

    auto* WaterColor = Add(NewObject<UMaterialExpressionConstant3Vector>(Material));
    WaterColor->Constant = FLinearColor(0.075f, 0.16f, 0.18f);
    auto* FoamColor = Add(NewObject<UMaterialExpressionConstant3Vector>(Material));
    FoamColor->Constant = FLinearColor(0.68f, 0.76f, 0.78f);
    auto* Color = Add(NewObject<UMaterialExpressionLinearInterpolate>(Material));
    Color->A.Connect(0, WaterColor); Color->B.Connect(0, FoamColor); Color->Alpha.Connect(0, FoamAmount);
    auto* BodyOpacity = Add(NewObject<UMaterialExpressionAdd>(Material));
    // r688: the sheet was a 35-100 % veil times SheetOpacity (0.62 / 0.22): mostly the rock showed through.
    BodyOpacity->A.Connect(0, Multiply(FoamAmount, Constant(0.75f)));
    BodyOpacity->B.Connect(0, Constant(0.5f));

    // Feather each edge without depending on an imported alpha card. Depth fade
    // softens the contact with the original basin rocks instead of hiding them
    // behind a large opaque pool/mist substitute.
    auto Edge = [&](bool bU, float Width)
    {
        auto* Channel = Add(NewObject<UMaterialExpressionComponentMask>(Material));
        Channel->R = bU; Channel->G = !bU; Channel->Input.Connect(0, UV);
        auto* Inverse = Add(NewObject<UMaterialExpressionOneMinus>(Material));
        Inverse->Input.Connect(0, Channel);
        auto* Result = Add(NewObject<UMaterialExpressionSaturate>(Material));
        Result->Input.Connect(0, Multiply(Multiply(Channel, Inverse), Constant(Width)));
        return Result;
    };
    // r688: SheetOpacity 0.62 -> ~0.9, 0.22 -> ~0.4.
    auto* SheetSum = Add(NewObject<UMaterialExpressionAdd>(Material));
    SheetSum->A.Connect(0, Multiply(Scalar(TEXT("SheetOpacity"), 0.62f), Constant(1.25f)));
    SheetSum->B.Connect(0, Constant(0.12f));
    auto* SheetBoost = Add(NewObject<UMaterialExpressionSaturate>(Material));
    SheetBoost->Input.Connect(0, SheetSum);
    auto* Opacity = Multiply(Multiply(BodyOpacity, SheetBoost), Multiply(Edge(true, 20.0f), Edge(false, 60.0f)));
    auto* Contact = Add(NewObject<UMaterialExpressionDepthFade>(Material));
    Contact->InOpacity.Connect(0, Opacity); Contact->FadeDistanceDefault = 80.0f;
    Material->GetEditorOnlyData()->BaseColor.Connect(0, Color);
    Material->GetEditorOnlyData()->Normal.Connect(0, Normal);
    Material->GetEditorOnlyData()->Opacity.Connect(0, Contact);
    // r688: white water catches what light there is - a faint foam glow keeps it alive at night and in shade.
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Multiply(Multiply(FoamColor, FoamAmount), Constant(0.06f)));
    Material->GetEditorOnlyData()->Roughness.Connect(0, Constant(0.22f));
    Material->GetEditorOnlyData()->Specular.Connect(0, Constant(0.5f));
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Cached = WowCookedMaterial::Save(Material, TEXT("M_WowWaterfallCooked_v2"));
    return Cached.Get();
#else
    return UMaterial::GetDefaultMaterial(MD_Surface);
#endif
}
