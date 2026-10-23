#pragma once
#include "CoreMinimal.h"

struct WOWDATA_API FWmoMaterial
{
    uint32 Flags = 0;
    uint32 Shader = 0;
    uint32 BlendMode = 0;
    FString TexturePath1;
    FString TexturePath2;
    FColor DiffuseColor = FColor::White;
    FColor EmissiveColor = FColor::Black;
};

struct WOWDATA_API FWmoDoodadSet
{
    FString Name;
    uint32 FirstDoodadIndex = 0;
    uint32 NumDoodads = 0;
};

struct WOWDATA_API FWmoDoodadDef
{
    uint32 NameOffset = 0;
    uint8 Flags = 0;
    FString Path;
    FVector Position = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    float Scale = 1.0f;
    FColor Color = FColor::White;
};

/** MOLT light record from a WMO root file. Coordinates stay in raw WMO local space. */
struct WOWDATA_API FWmoLight
{
    uint8 Type = 0;
    bool bUseAttenuation = false;
    FColor Color = FColor::White;
    FVector Position = FVector::ZeroVector;
    float Intensity = 1.0f;
    float AttenuationStart = 0.0f;
    float AttenuationEnd = 0.0f;
    float Unknown0 = 0.0f;
    float Unknown1 = 0.0f;
    float Unknown2 = 0.0f;
    float Unknown3 = 0.0f;
};

/** MLIQ liquid vertex (8 bytes): flow/UV data + height */
struct WOWDATA_API FWmoLiquidVertex
{
    float Height = 0.0f;
    uint8 Flow1 = 0;    // depth/flow for water; UV.x for magma (as int16 overlaid)
    uint8 Flow2 = 0;    // depth/flow for water; UV.y for magma (as int16 overlaid)
    uint8 FlowPct = 0;
    uint8 Filler = 0;
};

/** Parsed MLIQ liquid data from a WMO group */
struct WOWDATA_API FWmoLiquidData
{
    uint32 XVerts = 0;     // vertex grid width
    uint32 YVerts = 0;     // vertex grid height
    FVector Position = FVector::ZeroVector;  // base position offset
    uint16 MaterialId = 0; // MOMT material slot, NOT LiquidType.dbc
    TArray<FWmoLiquidVertex> Vertices; // XVerts * YVerts
    TArray<uint8> TileFlags;           // (XVerts-1) * (YVerts-1)
    bool HasLiquid() const { return XVerts > 1 && YVerts > 1 && Vertices.Num() > 0; }

    /** Get liquid category: 0=water, 1=ocean, 2=magma, 3=slime */
    int32 GetLiquidCategory() const
    {
        if (MaterialId <= 3) return 0;
        if (MaterialId <= 7) return 1;
        if (MaterialId <= 11) return 2;
        return 3;
    }
};

struct WOWDATA_API FWmoGroupData
{
    TArray<FVector> Vertices;
    TArray<FVector> Normals;
    TArray<FVector2D> TexCoords;
    TArray<uint16> Indices;
    struct FTriangleMaterial
    {
        uint8 Flags = 0;
        uint8 MaterialId = 0;
        bool HasCollision() const
        {
            // WotLK MOPY: collision-only faces and solid render faces, excluding
            // decorative detail. Same selection as AzerothCore's WMO extractor.
            return (Flags & 0x08) || MaterialId == 0xff ||
                ((Flags & 0x20) && !(Flags & 0x04));
        }
    };
    TArray<FTriangleMaterial> TriangleMaterials;
    TArray<FColor> VertexColors;
    struct FBatch { uint32 IndexStart=0; uint16 IndexCount=0; uint16 VertexStart=0; uint16 VertexEnd=0; uint8 MaterialIndex=0; };
    TArray<FBatch> Batches;
    FBox BoundingBox = FBox(ForceInit);
    uint32 Flags = 0;
    uint32 LiquidTypeId = 0; // MOGP group liquid field; interpreted with root flags
    FWmoLiquidData Liquid;
    TArray<uint16> DoodadRefs; // r539: MODR, the MODD entries drawn with this group
    uint8 FogIds[4] = { 0, 0, 0, 0 }; // r654: MOGP fogs[4], indices into the root's MFOG
    bool bIsValid = false;
};

struct WOWDATA_API FWmoRootData
{
    TArray<FWmoMaterial> Materials;
    uint32 NumGroups = 0;
    TArray<FString> DoodadPaths;
    TMap<uint32, FString> DoodadPathByOffset;
    TArray<FWmoDoodadSet> DoodadSets;
    TArray<FWmoDoodadDef> DoodadDefs;
    TArray<FWmoLight> Lights;
    struct FPortal { FVector Normal=FVector::ZeroVector; float Distance=0; TArray<FVector> Vertices; }; // r539: MOPV polygon
    struct FPortalReference { uint16 Portal=0; uint16 Group=0; int16 Side=0; };
    TArray<FPortal> Portals;
    TArray<FPortalReference> PortalReferences;
    // r654: MFOG - the fog inside the building or cave (linear from End * StartScalar to End, in yards).
    struct FFog { uint32 Flags = 0; FVector Position = FVector::ZeroVector; float SmallRadius = 0, LargeRadius = 0;
        float End = 0, StartScalar = 0; FColor Color = FColor::White; float UnderwaterEnd = 0, UnderwaterStartScalar = 0; FColor UnderwaterColor = FColor::White; };
    TArray<FFog> Fogs;
    FBox BoundingBox = FBox(ForceInit);
    uint32 Flags = 0;
    bool bIsValid = false;
};
