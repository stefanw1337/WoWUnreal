#include "Formats/WmoParser.h"
#include "Formats/ChunkId.h"

DEFINE_LOG_CATEGORY_STATIC(LogWmo, Log, All);

// Root chunks — REVERSED order, as stored on disk (little-endian uint32 read)
static constexpr uint32 WMO_MVER = MakeFourCC('R','E','V','M');
static constexpr uint32 WMO_MOHD = MakeFourCC('D','H','O','M');
static constexpr uint32 WMO_MOTX = MakeFourCC('X','T','O','M');
static constexpr uint32 WMO_MOMT = MakeFourCC('T','M','O','M');
static constexpr uint32 WMO_MOGN = MakeFourCC('N','G','O','M');
static constexpr uint32 WMO_MOGI = MakeFourCC('I','G','O','M');
static constexpr uint32 WMO_MOPV = MakeFourCC('V','P','O','M');
static constexpr uint32 WMO_MOPT = MakeFourCC('T','P','O','M');
static constexpr uint32 WMO_MOPR = MakeFourCC('R','P','O','M');
static constexpr uint32 WMO_MOLT = MakeFourCC('T','L','O','M');
static constexpr uint32 WMO_MODS = MakeFourCC('S','D','O','M');
static constexpr uint32 WMO_MODN = MakeFourCC('N','D','O','M');
static constexpr uint32 WMO_MODD = MakeFourCC('D','D','O','M');
static constexpr uint32 WMO_MFOG = MakeFourCC('G','O','F','M'); // r654

// Group chunks — REVERSED order, as stored on disk (little-endian uint32 read)
static constexpr uint32 WMO_MOGP = MakeFourCC('P','G','O','M');
static constexpr uint32 WMO_MOPY = MakeFourCC('Y','P','O','M');
static constexpr uint32 WMO_MOVI = MakeFourCC('I','V','O','M');
static constexpr uint32 WMO_MOVT = MakeFourCC('T','V','O','M');
static constexpr uint32 WMO_MONR = MakeFourCC('R','N','O','M');
static constexpr uint32 WMO_MOTV = MakeFourCC('V','T','O','M');
static constexpr uint32 WMO_MOBA = MakeFourCC('A','B','O','M');
static constexpr uint32 WMO_MOCV = MakeFourCC('V','C','O','M');
static constexpr uint32 WMO_MLIQ = MakeFourCC('Q','I','L','M');
static constexpr uint32 WMO_MODR = MakeFourCC('R','D','O','M');

namespace
{
// Safe read helpers
    template<typename T>
    FORCEINLINE T ReadVal(const uint8* Base, uint32& Offset, uint32 DataSize)
    {
        if (Offset + sizeof(T) > DataSize)
        {
            Offset = DataSize; // prevent further reads
            return T{};
        }
        T Value;
        FMemory::Memcpy(&Value, Base + Offset, sizeof(T));
        Offset += sizeof(T);
        return Value;
    }

    FORCEINLINE float ReadFloat(const uint8* Base, uint32& Offset, uint32 DataSize)
    {
        return ReadVal<float>(Base, Offset, DataSize);
    }

    FORCEINLINE uint32 ReadU32(const uint8* Base, uint32& Offset, uint32 DataSize)
    {
        return ReadVal<uint32>(Base, Offset, DataSize);
    }

    FORCEINLINE uint16 ReadU16(const uint8* Base, uint32& Offset, uint32 DataSize)
    {
        return ReadVal<uint16>(Base, Offset, DataSize);
    }

    FORCEINLINE uint8 ReadU8(const uint8* Base, uint32& Offset, uint32 DataSize)
    {
        return ReadVal<uint8>(Base, Offset, DataSize);
    }

    FORCEINLINE int32 ReadI32(const uint8* Base, uint32& Offset, uint32 DataSize)
    {
        return ReadVal<int32>(Base, Offset, DataSize);
    }

    FORCEINLINE int16 ReadI16(const uint8* Base, uint32& Offset, uint32 DataSize)
    {
        return ReadVal<int16>(Base, Offset, DataSize);
    }

    // Read a null-terminated string from a string table at a given offset
    FString ReadStringFromTable(const uint8* TableBase, uint32 TableSize, uint32 StringOffset)
    {
        if (StringOffset >= TableSize)
        {
            return FString();
        }
        // Find the null terminator
        uint32 End = StringOffset;
        while (End < TableSize && TableBase[End] != 0)
        {
            End++;
        }
        if (End == StringOffset)
        {
            return FString();
        }
        // Convert to FString (ASCII)
        return FString(End - StringOffset, reinterpret_cast<const char*>(TableBase + StringOffset));
    }

    // Collect all null-terminated strings from a string table as (offset, string) pairs
    void CollectAllStrings(const uint8* TableBase, uint32 TableSize, TArray<TPair<uint32, FString>>& OutStrings)
    {
        uint32 Pos = 0;
        while (Pos < TableSize)
        {
            // Skip null padding bytes
            if (TableBase[Pos] == 0)
            {
                Pos++;
                continue;
            }
            uint32 Start = Pos;
            while (Pos < TableSize && TableBase[Pos] != 0)
            {
                Pos++;
            }
            FString Str(Pos - Start, reinterpret_cast<const char*>(TableBase + Start));
            OutStrings.Add(TPair<uint32, FString>(Start, MoveTemp(Str)));
            if (Pos < TableSize) Pos++; // skip the null terminator
        }
    }

    FString ReadFixedCString(const uint8* Base, uint32 Offset, uint32 MaxLen)
    {
        uint32 Len = 0;
        while (Len < MaxLen && Base[Offset + Len] != 0)
        {
            ++Len;
        }

        return Len > 0
            ? FString(Len, reinterpret_cast<const char*>(Base + Offset))
            : FString();
    }
}

// ============================================================================
// ParseRoot
// ============================================================================
FWmoRootData FWmoParser::ParseRoot(const TArray<uint8>& Data)
{
    FWmoRootData Result;
    const uint8* Base = Data.GetData();
    const uint32 DataSize = static_cast<uint32>(Data.Num());

    if (DataSize < 8)
    {
        UE_LOG(LogWmo, Warning, TEXT("WMO root data too small (%d bytes)"), DataSize);
        return Result;
    }

    // Texture name string table (populated from MOTX chunk)
    const uint8* TexNameTable = nullptr;
    uint32 TexNameTableSize = 0;

    // Doodad name string table (populated from MODN chunk)
    const uint8* DoodNameTable = nullptr;
    uint32 DoodNameTableSize = 0;
    uint32 ExpectedLightCount = 0;
    TArray<FVector> PortalVertices; // r539: MOPV

    // Iterate chunks
    uint32 Offset = 0;
    while (Offset + 8 <= DataSize)
    {
        const uint32 ChunkMagic = ReadU32(Base, Offset, DataSize);
        const uint32 ChunkSize = ReadU32(Base, Offset, DataSize);
        const uint32 ChunkStart = Offset;

        if (ChunkStart + ChunkSize > DataSize)
        {
            UE_LOG(LogWmo, Warning, TEXT("WMO root chunk 0x%08X at offset %u overflows (size=%u, remaining=%u)"),
                ChunkMagic, ChunkStart - 8, ChunkSize, DataSize - ChunkStart);
            break;
        }

        if (ChunkMagic == WMO_MOHD)
        {
            // Header: 64 bytes
            if (ChunkSize < 64)
            {
                UE_LOG(LogWmo, Warning, TEXT("MOHD chunk too small: %u"), ChunkSize);
                Offset = ChunkStart + ChunkSize;
                continue;
            }
            uint32 Off = ChunkStart;
            const uint32 nMaterials  = ReadU32(Base, Off, DataSize);
            const uint32 nGroups     = ReadU32(Base, Off, DataSize);
            /*nPortals*/               ReadU32(Base, Off, DataSize);
            ExpectedLightCount       = ReadU32(Base, Off, DataSize);
            /*nModels*/                ReadU32(Base, Off, DataSize);
            /*nDoodads*/               ReadU32(Base, Off, DataSize);
            /*nSets*/                  ReadU32(Base, Off, DataSize);
            const uint32 AmbientBGRA = ReadU32(Base, Off, DataSize);
            /*wmoID*/                  ReadU32(Base, Off, DataSize);

            // Bounding box: min (3 floats), max (3 floats)
            const float MinX = ReadFloat(Base, Off, DataSize);
            const float MinY = ReadFloat(Base, Off, DataSize);
            const float MinZ = ReadFloat(Base, Off, DataSize);
            const float MaxX = ReadFloat(Base, Off, DataSize);
            const float MaxY = ReadFloat(Base, Off, DataSize);
            const float MaxZ = ReadFloat(Base, Off, DataSize);

            // WoW coord system: X right, Y up(?), Z forward  ->  UE: swap Y/Z, negate as needed
            // For WMO we store raw WoW coords; the rendering layer handles conversion.
            Result.BoundingBox = FBox(FVector(MinX, MinY, MinZ), FVector(MaxX, MaxY, MaxZ));

            const uint16 Flags  = ReadU16(Base, Off, DataSize);
            /*numLods*/           ReadU16(Base, Off, DataSize);

            Result.NumGroups = nGroups;
            Result.Flags = Flags;

            (void)nMaterials; // materials count comes from MOMT chunk size
            (void)AmbientBGRA;
        }
        else if (ChunkMagic == WMO_MOTX)
        {
            // Texture name string table - just store pointer, will be used by MOMT
            TexNameTable = Base + ChunkStart;
            TexNameTableSize = ChunkSize;
        }
        else if (ChunkMagic == WMO_MOMT)
        {
            // Materials: 64 bytes each
            const uint32 NumMaterials = ChunkSize / 64;
            Result.Materials.Reserve(NumMaterials);

            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < NumMaterials; i++)
            {
                FWmoMaterial Mat;
                Mat.Flags     = ReadU32(Base, Off, DataSize);
                Mat.Shader    = ReadU32(Base, Off, DataSize);
                Mat.BlendMode = ReadU32(Base, Off, DataSize);

                const uint32 Tex1Ofs = ReadU32(Base, Off, DataSize);

                // Emissive color (BGRA in file)
                const uint8 EmB = ReadU8(Base, Off, DataSize);
                const uint8 EmG = ReadU8(Base, Off, DataSize);
                const uint8 EmR = ReadU8(Base, Off, DataSize);
                const uint8 EmA = ReadU8(Base, Off, DataSize);
                Mat.EmissiveColor = FColor(EmR, EmG, EmB, EmA);

                // sidn_emissive_color / flags2 (4 bytes, skip)
                Off += 4;

                const uint32 Tex2Ofs = ReadU32(Base, Off, DataSize);

                // Diffuse color (ARGB layout per CArgb: R,G,B,A)
                const uint8 DiffR = ReadU8(Base, Off, DataSize);
                const uint8 DiffG = ReadU8(Base, Off, DataSize);
                const uint8 DiffB = ReadU8(Base, Off, DataSize);
                const uint8 DiffA = ReadU8(Base, Off, DataSize);
                Mat.DiffuseColor = FColor(DiffR, DiffG, DiffB, DiffA);

                /*groundType*/  ReadU32(Base, Off, DataSize);
                const uint32 Tex3Ofs = ReadU32(Base, Off, DataSize);
                /*color2*/      ReadU32(Base, Off, DataSize);
                /*flags3*/      ReadU32(Base, Off, DataSize);
                // runtime data: 4 uint32s
                Off += 16;

                // Resolve texture paths from string table
                if (TexNameTable)
                {
                    Mat.TexturePath1 = ReadStringFromTable(TexNameTable, TexNameTableSize, Tex1Ofs);
                    Mat.TexturePath2 = ReadStringFromTable(TexNameTable, TexNameTableSize, Tex2Ofs);
                }

                (void)Tex3Ofs;

                Result.Materials.Add(MoveTemp(Mat));
            }
        }
        else if (ChunkMagic == WMO_MOPV)
        {
            // r539: portal polygons, raw WMO coordinates (comes before MOPT in the file).
            uint32 Off=ChunkStart;
            for (uint32 I=0;I<ChunkSize/12;++I)
            {
                const float X=ReadFloat(Base,Off,DataSize);
                const float Y=ReadFloat(Base,Off,DataSize);
                const float Z=ReadFloat(Base,Off,DataSize);
                PortalVertices.Add(FVector(X,Y,Z));
            }
        }
        else if (ChunkMagic == WMO_MOPT)
        {
            uint32 Off=ChunkStart;
            for (uint32 I=0;I<ChunkSize/20;++I)
            {
                // r539: the vertex range (MOPV) for portal culling.
                const uint16 FirstVertex=ReadU16(Base,Off,DataSize);
                const uint16 VertexCount=ReadU16(Base,Off,DataSize);
                FWmoRootData::FPortal Portal;
                for (uint32 V=FirstVertex;V<uint32(FirstVertex)+VertexCount && V<uint32(PortalVertices.Num());++V)
                    Portal.Vertices.Add(PortalVertices[V]);
                Portal.Normal.X=ReadFloat(Base,Off,DataSize);
                Portal.Normal.Y=ReadFloat(Base,Off,DataSize);
                Portal.Normal.Z=ReadFloat(Base,Off,DataSize);
                Portal.Distance=ReadFloat(Base,Off,DataSize);
                Result.Portals.Add(Portal);
            }
        }
        else if (ChunkMagic == WMO_MOPR)
        {
            uint32 Off=ChunkStart;
            for (uint32 I=0;I<ChunkSize/8;++I)
            {
                FWmoRootData::FPortalReference Ref;
                Ref.Portal=ReadU16(Base,Off,DataSize);
                Ref.Group=ReadU16(Base,Off,DataSize);
                Ref.Side=ReadI16(Base,Off,DataSize);
                Off+=2;
                Result.PortalReferences.Add(Ref);
            }
        }
        else if (ChunkMagic == WMO_MOLT)
        {
            // Lights: fixed 48-byte records. These are WMO-local authored light
            // sources used by buildings and interiors.
            constexpr uint32 LightRecordSize = 48;
            const uint32 NumLights = ChunkSize / LightRecordSize;
            Result.Lights.Reserve(NumLights);

            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < NumLights; ++i)
            {
                FWmoLight Light;
                Light.Type = ReadU8(Base, Off, DataSize);
                Light.bUseAttenuation = ReadU8(Base, Off, DataSize) != 0;
                /*padding*/ ReadU8(Base, Off, DataSize);
                /*padding*/ ReadU8(Base, Off, DataSize);

                const uint8 B = ReadU8(Base, Off, DataSize);
                const uint8 G = ReadU8(Base, Off, DataSize);
                const uint8 R = ReadU8(Base, Off, DataSize);
                const uint8 A = ReadU8(Base, Off, DataSize);
                Light.Color = FColor(R, G, B, A);

                const float PosX = ReadFloat(Base, Off, DataSize);
                const float PosY = ReadFloat(Base, Off, DataSize);
                const float PosZ = ReadFloat(Base, Off, DataSize);
                Light.Position = FVector(PosX, PosY, PosZ);

                Light.Intensity = ReadFloat(Base, Off, DataSize);
                Light.AttenuationStart = ReadFloat(Base, Off, DataSize);
                Light.AttenuationEnd = ReadFloat(Base, Off, DataSize);
                Light.Unknown0 = ReadFloat(Base, Off, DataSize);
                Light.Unknown1 = ReadFloat(Base, Off, DataSize);
                Light.Unknown2 = ReadFloat(Base, Off, DataSize);
                Light.Unknown3 = ReadFloat(Base, Off, DataSize);

                Result.Lights.Add(MoveTemp(Light));
            }

            if (ChunkSize % LightRecordSize != 0)
            {
                UE_LOG(LogWmo, Warning, TEXT("MOLT chunk has %u trailing bytes"), ChunkSize % LightRecordSize);
            }
        }
        else if (ChunkMagic == WMO_MODN)
        {
            // Doodad name string table
            DoodNameTable = Base + ChunkStart;
            DoodNameTableSize = ChunkSize;

            // Collect all doodad paths
            TArray<TPair<uint32, FString>> AllStrings;
            CollectAllStrings(DoodNameTable, DoodNameTableSize, AllStrings);
            Result.DoodadPaths.Reserve(AllStrings.Num());
            for (auto& Pair : AllStrings)
            {
                Result.DoodadPathByOffset.Add(Pair.Key, Pair.Value);
                Result.DoodadPaths.Add(MoveTemp(Pair.Value));
            }
        }
        else if (ChunkMagic == WMO_MODS)
        {
            // Doodad sets: fixed 32-byte records.
            const uint32 NumSets = ChunkSize / 32;
            Result.DoodadSets.Reserve(NumSets);

            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < NumSets; ++i)
            {
                FWmoDoodadSet Set;
                Set.Name = ReadFixedCString(Base, Off, 20);
                Off += 20;
                Set.FirstDoodadIndex = ReadU32(Base, Off, DataSize);
                Set.NumDoodads = ReadU32(Base, Off, DataSize);
                /*padding*/ ReadU32(Base, Off, DataSize);
                Result.DoodadSets.Add(MoveTemp(Set));
            }
        }
        else if (ChunkMagic == WMO_MFOG)
        {
            // r654: fog definitions, 48 bytes each (flags, position, small/large radius, fog end, start scalar,
            // BGRA colour, then the same three for under water).
            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < ChunkSize / 48; ++i)
            {
                FWmoRootData::FFog Fog;
                Fog.Flags = ReadU32(Base, Off, DataSize);
                const float X = ReadFloat(Base, Off, DataSize);
                const float Y = ReadFloat(Base, Off, DataSize);
                const float Z = ReadFloat(Base, Off, DataSize);
                Fog.Position = FVector(X, Y, Z);
                Fog.SmallRadius = ReadFloat(Base, Off, DataSize);
                Fog.LargeRadius = ReadFloat(Base, Off, DataSize);
                Fog.End = ReadFloat(Base, Off, DataSize);
                Fog.StartScalar = ReadFloat(Base, Off, DataSize);
                {
                    const uint8 B = ReadU8(Base, Off, DataSize), G = ReadU8(Base, Off, DataSize), R = ReadU8(Base, Off, DataSize), A = ReadU8(Base, Off, DataSize);
                    Fog.Color = FColor(R, G, B, A);
                }
                Fog.UnderwaterEnd = ReadFloat(Base, Off, DataSize);
                Fog.UnderwaterStartScalar = ReadFloat(Base, Off, DataSize);
                {
                    const uint8 B = ReadU8(Base, Off, DataSize), G = ReadU8(Base, Off, DataSize), R = ReadU8(Base, Off, DataSize), A = ReadU8(Base, Off, DataSize);
                    Fog.UnderwaterColor = FColor(R, G, B, A);
                }
                Result.Fogs.Add(Fog);
            }
        }
        else if (ChunkMagic == WMO_MODD)
        {
            // Doodad definitions: 40-byte records. The high byte of nameIndex
            // stores placement flags; the low 24 bits are an offset into MODN.
            const uint32 NumDefs = ChunkSize / 40;
            Result.DoodadDefs.Reserve(NumDefs);

            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < NumDefs; ++i)
            {
                FWmoDoodadDef Def;
                const uint32 NameAndFlags = ReadU32(Base, Off, DataSize);
                Def.NameOffset = NameAndFlags & 0x00FFFFFFu;
                Def.Flags = static_cast<uint8>((NameAndFlags >> 24) & 0xFFu);

                const float PosX = ReadFloat(Base, Off, DataSize);
                const float PosY = ReadFloat(Base, Off, DataSize);
                const float PosZ = ReadFloat(Base, Off, DataSize);
                Def.Position = FVector(PosX, PosY, PosZ);

                const float RotX = ReadFloat(Base, Off, DataSize);
                const float RotY = ReadFloat(Base, Off, DataSize);
                const float RotZ = ReadFloat(Base, Off, DataSize);
                const float RotW = ReadFloat(Base, Off, DataSize);
                Def.Rotation = FQuat(RotX, RotY, RotZ, RotW);
                if (!Def.Rotation.IsNormalized())
                {
                    Def.Rotation.Normalize();
                }

                Def.Scale = ReadFloat(Base, Off, DataSize);

                const uint8 B = ReadU8(Base, Off, DataSize);
                const uint8 G = ReadU8(Base, Off, DataSize);
                const uint8 R = ReadU8(Base, Off, DataSize);
                const uint8 A = ReadU8(Base, Off, DataSize);
                Def.Color = FColor(R, G, B, A);

                Result.DoodadDefs.Add(MoveTemp(Def));
            }
        }

        // Advance to next chunk
        Offset = ChunkStart + ChunkSize;
    }

    for (FWmoDoodadDef& Def : Result.DoodadDefs)
    {
        if (const FString* Resolved = Result.DoodadPathByOffset.Find(Def.NameOffset))
        {
            Def.Path = *Resolved;
        }
        else if (DoodNameTable && Def.NameOffset < DoodNameTableSize)
        {
            Def.Path = ReadStringFromTable(DoodNameTable, DoodNameTableSize, Def.NameOffset);
            if (!Def.Path.IsEmpty())
            {
                Result.DoodadPathByOffset.Add(Def.NameOffset, Def.Path);
            }
        }
    }

    Result.bIsValid = true;
    if (ExpectedLightCount != 0 && Result.Lights.Num() != static_cast<int32>(ExpectedLightCount))
    {
        UE_LOG(LogWmo, Warning, TEXT("WMO root expected %u lights but parsed %d"),
            ExpectedLightCount, Result.Lights.Num());
    }

    UE_LOG(LogWmo, Log, TEXT("WMO root parsed: %d materials, %d groups, %d doodad paths, %d sets, %d defs, %d lights"),
        Result.Materials.Num(), Result.NumGroups, Result.DoodadPaths.Num(),
        Result.DoodadSets.Num(), Result.DoodadDefs.Num(), Result.Lights.Num());
    return Result;
}

// ============================================================================
// ParseGroup
// ============================================================================
FWmoGroupData FWmoParser::ParseGroup(const TArray<uint8>& Data)
{
    FWmoGroupData Result;
    const uint8* Base = Data.GetData();
    const uint32 DataSize = static_cast<uint32>(Data.Num());

    if (DataSize < 8)
    {
        UE_LOG(LogWmo, Warning, TEXT("WMO group data too small (%d bytes)"), DataSize);
        return Result;
    }

    // The group file starts with an MVER chunk, then the MOGP chunk which wraps all sub-chunks.
    // MOGP's chunk size covers the group header + all sub-chunks inside it.
    // We need to find MOGP first, parse its header, then iterate sub-chunks inside it.

    uint32 Offset = 0;
    uint32 MogpDataStart = 0;
    uint32 MogpDataEnd = 0;

    // Find the MOGP chunk
    while (Offset + 8 <= DataSize)
    {
        const uint32 ChunkMagic = ReadU32(Base, Offset, DataSize);
        const uint32 ChunkSize = ReadU32(Base, Offset, DataSize);
        const uint32 ChunkStart = Offset;

        if (ChunkStart + ChunkSize > DataSize)
        {
            break;
        }

        if (ChunkMagic == WMO_MOGP)
        {
            // Parse the MOGP header (68 bytes)
            if (ChunkSize < 68)
            {
                UE_LOG(LogWmo, Warning, TEXT("MOGP chunk too small: %u"), ChunkSize);
                return Result;
            }

            uint32 Off = ChunkStart;
            /*groupName*/         ReadU32(Base, Off, DataSize);
            /*descriptiveGroup*/  ReadU32(Base, Off, DataSize);
            Result.Flags        = ReadU32(Base, Off, DataSize);

            const float MinX = ReadFloat(Base, Off, DataSize);
            const float MinY = ReadFloat(Base, Off, DataSize);
            const float MinZ = ReadFloat(Base, Off, DataSize);
            const float MaxX = ReadFloat(Base, Off, DataSize);
            const float MaxY = ReadFloat(Base, Off, DataSize);
            const float MaxZ = ReadFloat(Base, Off, DataSize);
            Result.BoundingBox = FBox(FVector(MinX, MinY, MinZ), FVector(MaxX, MaxY, MaxZ));

            /*portalStart*/       ReadU16(Base, Off, DataSize);
            /*portalCount*/       ReadU16(Base, Off, DataSize);
            /*transBatchCount*/   ReadU16(Base, Off, DataSize);
            /*intBatchCount*/     ReadU16(Base, Off, DataSize);
            /*extBatchCount*/     ReadU16(Base, Off, DataSize);
            /*padding*/           ReadU16(Base, Off, DataSize);
            // fogs[4] (r654: kept - indices into the root's MFOG)
            for (int32 FogSlot = 0; FogSlot < 4; ++FogSlot) Result.FogIds[FogSlot] = ReadU8(Base, Off, DataSize);
            Result.LiquidTypeId = ReadU32(Base, Off, DataSize);
            /*groupID*/           ReadU32(Base, Off, DataSize);
            /*unk1*/              ReadU32(Base, Off, DataSize);
            /*unk2*/              ReadU32(Base, Off, DataSize);

            // Sub-chunks start right after the 68-byte MOGP header
            MogpDataStart = Off;
            MogpDataEnd = ChunkStart + ChunkSize;
            break;
        }

        Offset = ChunkStart + ChunkSize;
    }

    if (MogpDataStart == 0 || MogpDataEnd == 0)
    {
        UE_LOG(LogWmo, Warning, TEXT("WMO group: MOGP chunk not found"));
        return Result;
    }

    // Now iterate sub-chunks inside MOGP
    Offset = MogpDataStart;
    while (Offset + 8 <= MogpDataEnd)
    {
        const uint32 ChunkMagic = ReadU32(Base, Offset, DataSize);
        const uint32 ChunkSize = ReadU32(Base, Offset, DataSize);
        const uint32 ChunkStart = Offset;

        if (ChunkStart + ChunkSize > MogpDataEnd)
        {
            UE_LOG(LogWmo, Warning, TEXT("WMO group sub-chunk 0x%08X at %u overflows MOGP (size=%u)"),
                ChunkMagic, ChunkStart - 8, ChunkSize);
            break;
        }

        if (ChunkMagic == WMO_MOPY)
        {
            const uint32 Count = ChunkSize / 2;
            Result.TriangleMaterials.SetNum(Count);
            uint32 Off = ChunkStart;
            for (auto& Triangle : Result.TriangleMaterials)
            {
                Triangle.Flags = ReadU8(Base, Off, DataSize);
                Triangle.MaterialId = ReadU8(Base, Off, DataSize);
            }
        }
        else if (ChunkMagic == WMO_MOVI)
        {
            // Triangle indices: uint16 each
            const uint32 NumIndices = ChunkSize / 2;
            Result.Indices.SetNumUninitialized(NumIndices);
            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < NumIndices; i++)
            {
                Result.Indices[i] = ReadU16(Base, Off, DataSize);
            }
        }
        else if (ChunkMagic == WMO_MOVT)
        {
            // Vertices: 3 floats (12 bytes) each
            const uint32 NumVerts = ChunkSize / 12;
            Result.Vertices.SetNumUninitialized(NumVerts);
            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < NumVerts; i++)
            {
                const float X = ReadFloat(Base, Off, DataSize);
                const float Y = ReadFloat(Base, Off, DataSize);
                const float Z = ReadFloat(Base, Off, DataSize);
                Result.Vertices[i] = FVector(X, Y, Z);
            }
        }
        else if (ChunkMagic == WMO_MONR)
        {
            // Normals: 3 floats (12 bytes) each
            const uint32 NumNormals = ChunkSize / 12;
            Result.Normals.SetNumUninitialized(NumNormals);
            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < NumNormals; i++)
            {
                const float X = ReadFloat(Base, Off, DataSize);
                const float Y = ReadFloat(Base, Off, DataSize);
                const float Z = ReadFloat(Base, Off, DataSize);
                Result.Normals[i] = FVector(X, Y, Z);
            }
        }
        else if (ChunkMagic == WMO_MOTV)
        {
            // Texture coordinates: 2 floats (8 bytes) each
            // Only store the first MOTV set (some groups have two)
            if (Result.TexCoords.Num() == 0)
            {
                const uint32 NumCoords = ChunkSize / 8;
                Result.TexCoords.SetNumUninitialized(NumCoords);
                uint32 Off = ChunkStart;
                for (uint32 i = 0; i < NumCoords; i++)
                {
                    const float U = ReadFloat(Base, Off, DataSize);
                    const float V = ReadFloat(Base, Off, DataSize);
                    Result.TexCoords[i] = FVector2D(U, V);
                }
            }
        }
        else if (ChunkMagic == WMO_MOBA)
        {
            // Render batches: 24 bytes each
            const uint32 NumBatches = ChunkSize / 24;
            Result.Batches.SetNumUninitialized(NumBatches);
            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < NumBatches; i++)
            {
                FWmoGroupData::FBatch& Batch = Result.Batches[i];

                // Skip bounding box: 6 int16 = 12 bytes
                Off += 12;

                Batch.IndexStart    = ReadU32(Base, Off, DataSize);
                Batch.IndexCount    = ReadU16(Base, Off, DataSize);
                Batch.VertexStart   = ReadU16(Base, Off, DataSize);
                Batch.VertexEnd     = ReadU16(Base, Off, DataSize);
                /*flags*/             ReadU8(Base, Off, DataSize);
                Batch.MaterialIndex = ReadU8(Base, Off, DataSize);
            }
        }
        else if (ChunkMagic == WMO_MODR)
        {
            // r539: doodads drawn with this group (portal culling hides them with it).
            uint32 Off = ChunkStart;
            for (uint32 i = 0; i < ChunkSize / 2; i++) Result.DoodadRefs.Add(ReadU16(Base, Off, DataSize));
        }
        else if (ChunkMagic == WMO_MOCV)
        {
            // Vertex colors: BGRA, 4 bytes each
            // Only store the first MOCV set
            if (Result.VertexColors.Num() == 0)
            {
                const uint32 NumColors = ChunkSize / 4;
                Result.VertexColors.SetNumUninitialized(NumColors);
                uint32 Off = ChunkStart;
                for (uint32 i = 0; i < NumColors; i++)
                {
                    const uint8 B = ReadU8(Base, Off, DataSize);
                    const uint8 G = ReadU8(Base, Off, DataSize);
                    const uint8 R = ReadU8(Base, Off, DataSize);
                    const uint8 A = ReadU8(Base, Off, DataSize);
                    Result.VertexColors[i] = FColor(R, G, B, A);
                }
            }
        }

        else if (ChunkMagic == WMO_MLIQ)
        {
            // MLIQ: liquid data — 30-byte header + vertex map + tile flags
            if (ChunkSize >= 30)
            {
                uint32 Off = ChunkStart;
                Result.Liquid.XVerts = ReadU32(Base, Off, DataSize);
                Result.Liquid.YVerts = ReadU32(Base, Off, DataSize);
                uint32 XTiles = ReadU32(Base, Off, DataSize); (void)XTiles;
                uint32 YTiles = ReadU32(Base, Off, DataSize); (void)YTiles;
                float PosX = ReadFloat(Base, Off, DataSize);
                float PosY = ReadFloat(Base, Off, DataSize);
                float PosZ = ReadFloat(Base, Off, DataSize);
                Result.Liquid.Position = FVector(PosX, PosY, PosZ);
                Result.Liquid.MaterialId = ReadU16(Base, Off, DataSize);

                // Read vertex data: XVerts * YVerts vertices, 8 bytes each
                uint32 NumVerts = Result.Liquid.XVerts * Result.Liquid.YVerts;
                Result.Liquid.Vertices.SetNumUninitialized(NumVerts);
                for (uint32 i = 0; i < NumVerts; i++)
                {
                    FWmoLiquidVertex& V = Result.Liquid.Vertices[i];
                    V.Flow1   = ReadU8(Base, Off, DataSize);
                    V.Flow2   = ReadU8(Base, Off, DataSize);
                    V.FlowPct = ReadU8(Base, Off, DataSize);
                    V.Filler  = ReadU8(Base, Off, DataSize);
                    V.Height  = ReadFloat(Base, Off, DataSize);
                }

                // Read tile flags: (XVerts-1) * (YVerts-1) bytes
                uint32 NumTiles = (Result.Liquid.XVerts > 0 ? Result.Liquid.XVerts - 1 : 0)
                                * (Result.Liquid.YVerts > 0 ? Result.Liquid.YVerts - 1 : 0);
                Result.Liquid.TileFlags.SetNumUninitialized(NumTiles);
                for (uint32 i = 0; i < NumTiles; i++)
                {
                    Result.Liquid.TileFlags[i] = ReadU8(Base, Off, DataSize);
                }

                UE_LOG(LogWmo, Log, TEXT("  MLIQ: %ux%u verts, %u tiles, mat=%u, pos=(%.1f,%.1f,%.1f)"),
                    Result.Liquid.XVerts, Result.Liquid.YVerts, NumTiles,
                    Result.Liquid.MaterialId, PosX, PosY, PosZ);
            }
        }

        // Advance to next sub-chunk
        Offset = ChunkStart + ChunkSize;
    }

    Result.bIsValid = Result.Vertices.Num() > 0 && Result.Indices.Num() > 0;
    UE_LOG(LogWmo, Log, TEXT("WMO group parsed: %d verts, %d indices, %d batches, %d colors"),
        Result.Vertices.Num(), Result.Indices.Num(), Result.Batches.Num(), Result.VertexColors.Num());
    return Result;
}
