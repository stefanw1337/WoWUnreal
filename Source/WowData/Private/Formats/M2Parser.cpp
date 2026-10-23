#include "Formats/M2Parser.h"
#include <type_traits>
DEFINE_LOG_CATEGORY_STATIC(LogM2, Log, All);

// ── Raw on-disk structures ──────────────────────────────────────────────────

#pragma pack(push, 1)

struct FM2Header
{
    uint32 Magic;               // 'MD20' = 0x3032444D
    uint32 Version;             // 264 for WotLK
    uint32 NameLength;
    uint32 NameOffset;
    uint32 GlobalFlags;
    uint32 nGlobalSequences;
    uint32 ofsGlobalSequences;
    uint32 nAnimations;
    uint32 ofsAnimations;
    uint32 nAnimationLookup;
    uint32 ofsAnimationLookup;
    uint32 nBones;
    uint32 ofsBones;
    uint32 nKeyBoneLookup;
    uint32 ofsKeyBoneLookup;
    uint32 nVertices;
    uint32 ofsVertices;
    uint32 nViews;              // Always 0 in WotLK; skins are in .skin files
    uint32 nColors;
    uint32 ofsColors;
    uint32 nTextures;
    uint32 ofsTextures;
    uint32 nTransparency;
    uint32 ofsTransparency;
    uint32 nTexAnims;
    uint32 ofsTexAnims;
    uint32 nTexReplace;
    uint32 ofsTexReplace;
    uint32 nRenderFlags;
    uint32 ofsRenderFlags;
    uint32 nBoneLookup;
    uint32 ofsBoneLookup;
    uint32 nTexLookup;
    uint32 ofsTexLookup;
    uint32 nTexUnits;
    uint32 ofsTexUnits;
    uint32 nTransLookup;
    uint32 ofsTransLookup;
    uint32 nTexAnimLookup;
    uint32 ofsTexAnimLookup;
    float  BoundingBox[6];      // min xyz, max xyz
    float  BoundingSphereRadius;
    float  CollisionBox[6];
    float  CollisionSphereRadius;
    uint32 nBoundingTriangles;
    uint32 ofsBoundingTriangles;
    uint32 nBoundingVertices;
    uint32 ofsBoundingVertices;
    uint32 nBoundingNormals;
    uint32 ofsBoundingNormals;
    uint32 nAttachments;
    uint32 ofsAttachments;
    uint32 nAttachmentLookup;
    uint32 ofsAttachmentLookup;
    uint32 nEvents;
    uint32 ofsEvents;
    uint32 nLights;
    uint32 ofsLights;
    uint32 nCameras;
    uint32 ofsCameras;
    uint32 nCameraLookup;
    uint32 ofsCameraLookup;
    uint32 nRibbonEmitters;
    uint32 ofsRibbonEmitters;
    uint32 nParticleEmitters;
    uint32 ofsParticleEmitters;
};

struct FM2VertexRaw
{
    float  Pos[3];
    uint8  BoneWeights[4];
    uint8  BoneIndices[4];
    float  Normal[3];
    float  TexCoords[2];
    float  TexCoords2[2];
};

struct FM2VectorRaw
{
    float Value[3];
};

struct FM2TextureEntry
{
    uint32 Type;
    uint32 Flags;
    uint32 NameLength;
    uint32 NameOffset;
};

struct FM2RenderFlagEntry
{
    uint16 Flags;
    uint16 BlendMode;
};

// ── Skin file structures ────────────────────────────────────────────────────

struct FM2SkinHeader
{
    uint32 Magic;               // 'SKIN' = 0x4E494B53
    uint32 nIndices;
    uint32 ofsIndices;
    uint32 nTriangles;
    uint32 ofsTriangles;
    uint32 nProperties;
    uint32 ofsProperties;
    uint32 nSubmeshes;
    uint32 ofsSubmeshes;
    uint32 nTextureUnits;
    uint32 ofsTextureUnits;
    uint32 LOD;
};

struct FM2SkinSubmesh
{
    uint16 ID;
    uint16 Level;
    uint16 StartVertex;
    uint16 nVertices;
    uint16 StartTriangle;
    uint16 nTriangles;
    uint16 nBones;
    uint16 StartBones;
    uint16 BoneInfluences;
    uint16 RootBone;
    float  CenterMass[3];
    float  CenterBoundingBox[3];
    float  Radius;
};

struct FM2SkinTextureUnit
{
    uint8  Flags;
    int8   PriorityPlane;
    int16  ShaderId;
    uint16 SkinSectionIndex;
    uint16 GeosetIndex;
    int16  ColorIndex;
    uint16 RenderFlagsIndex;
    uint16 TexUnitNumber;
    uint16 OpCount;
    uint16 TextureComboIndex;
    uint16 TexCoordComboIndex;
    uint16 TransparencyComboIndex;
    uint16 TexAnimComboIndex;
};

struct FM2BoneRaw
{
	int32  KeyBoneId;
	uint32 Flags;
	int16  ParentBone;
	uint16 SubmeshId;
	uint16 Unk[2];
	// Animation blocks follow (translation, rotation, scale) — we skip for now
	// Each is 20 bytes (type, globalSeq, nTimestamps, ofsTimestamps, nValues, ofsValues)
	uint8 TransBlock[20];
	uint8 RotBlock[20];
	uint8 ScaleBlock[20];
	float PivotPoint[3];
};

struct FM2AnimSequenceRaw
{
	uint16 AnimationId;
	uint16 SubAnimationId;
	uint32 Length;
	float  MoveSpeed;
	uint32 Flags;
	int16  Probability;
	uint16 Padding;
	uint32 MinRepetitions;
	uint32 MaxRepetitions;
	uint32 BlendTime;
	float  BoundsMin[3];
	float  BoundsMax[3];
	float  BoundRadius;
	int16  NextAnimation;
	uint16 AliasNext;
};

struct FM2AttachmentRaw
{
    uint32 Id;
    uint32 Bone;
    float Position[3];
    uint8 EnabledBlock[20]; // AnimBlock — ignored
};

struct FM2RibbonEmitterRaw
{
    int32 Id;
    uint32 Bone;
    float Position[3];
    uint32 TextureCount;
    uint32 TextureOffset;
    uint32 MaterialCount;
    uint32 MaterialOffset;
    uint8 ColorTrack[20];
    uint8 AlphaTrack[20];
    uint8 HeightAboveTrack[20];
    uint8 HeightBelowTrack[20];
    float EdgesPerSecond;
    float EdgeLifetime;
    float Gravity;
    uint16 TextureRows;
    uint16 TextureColumns;
    uint8 TextureSlotTrack[20];
    uint8 VisibilityTrack[20];
    int16 PriorityPlane;
    uint16 Padding;
};

static_assert(sizeof(FM2RibbonEmitterRaw) == 0xB0,
    "WotLK M2 ribbon emitter records must use the 176-byte on-disk stride");

struct FM2ParticleEmitterRaw
{
    uint32 Id;
    uint32 Flags;
    float Position[3];
    uint16 Bone;
    uint16 Texture;
    uint32 ModelFileNameCount;
    uint32 ModelFileNameOffset;
    uint32 ChildEmitterFileNameCount;
    uint32 ChildEmitterFileNameOffset;
    uint8 BlendingType;
    uint8 EmitterType;        // 1=plane, 2=sphere, 3=spline
    uint16 ParticleColorIndex;
    uint8 ParticleType;       // 0=regular, 1=chunky, 2=both
    uint8 HeadorTail;         // 0=head, 1=tail, 2=both
    int16 TextureTileRotation;
    uint16 TextureRows;
    uint16 TextureColumns;
    uint8 RemainingData[0x1DC - 0x34];
};

static_assert(sizeof(FM2ParticleEmitterRaw) == 0x1DC,
    "WotLK M2 particle emitter records must use the 476-byte on-disk stride");

struct FM2TrackRaw
{
    uint16 InterpolationType;
    int16 GlobalSequence;
    uint32 TimestampArrayCount;
    uint32 TimestampArrayOffset;
    uint32 ValueArrayCount;
    uint32 ValueArrayOffset;
};

struct FM2SubArrayRaw
{
    uint32 Count;
    uint32 Offset;
};

struct FM2PartTrackRaw
{
    uint32 TimestampCount;
    uint32 TimestampOffset;
    uint32 ValueCount;
    uint32 ValueOffset;
};

struct FM2ParticleColorRaw
{
    float R;
    float G;
    float B;
};

struct FM2ParticleScaleRaw
{
    float X;
    float Y;
};

#pragma pack(pop)

// ── Helpers ─────────────────────────────────────────────────────────────────

template<typename T>
static bool SafeRead(const uint8* Base, int32 TotalSize, uint32 Offset, uint32 Count, const T*& OutPtr)
{
    const uint64 End = static_cast<uint64>(Offset) + static_cast<uint64>(Count) * sizeof(T);
    if (End > static_cast<uint64>(TotalSize))
    {
        OutPtr = nullptr;
        return false;
    }
    OutPtr = reinterpret_cast<const T*>(Base + Offset);
    return true;
}

template<typename TValue>
static bool ReadFirstTrackValue(const uint8* Base, int32 TotalSize,
    const uint8* TrackAddress, TValue& OutValue)
{
    FM2TrackRaw Track;
    FMemory::Memcpy(&Track, TrackAddress, sizeof(Track));
    const FM2SubArrayRaw* ValueArrays = nullptr;
    if (Track.ValueArrayCount == 0 ||
        !SafeRead(Base, TotalSize, Track.ValueArrayOffset, Track.ValueArrayCount, ValueArrays))
    {
        return false;
    }

    for (uint32 ArrayIndex = 0; ArrayIndex < Track.ValueArrayCount; ++ArrayIndex)
    {
        const TValue* Values = nullptr;
        if (ValueArrays[ArrayIndex].Count > 0 &&
            SafeRead(Base, TotalSize, ValueArrays[ArrayIndex].Offset,
                ValueArrays[ArrayIndex].Count, Values))
        {
            OutValue = Values[0];
            return true;
        }
    }
    return false;
}

template<typename TValue>
static bool ReadParticleLifetimeTrack(const uint8* Base, int32 TotalSize,
    const uint8* TrackAddress, TValue& OutStart, TValue& OutMiddle,
    TValue& OutEnd, float& OutMiddleTime)
{
    FM2PartTrackRaw Track;
    FMemory::Memcpy(&Track, TrackAddress, sizeof(Track));
    const uint32 Count = FMath::Min(Track.TimestampCount, Track.ValueCount);
    const uint16* Times = nullptr;
    const TValue* Values = nullptr;
    if (Count == 0 ||
        !SafeRead(Base, TotalSize, Track.TimestampOffset, Count, Times) ||
        !SafeRead(Base, TotalSize, Track.ValueOffset, Count, Values))
    {
        return false;
    }

    const uint32 MiddleIndex = Count / 2;
    OutStart = Values[0];
    OutMiddle = Values[MiddleIndex];
    OutEnd = Values[Count - 1];
    OutMiddleTime = FMath::Clamp(
        static_cast<float>(Times[MiddleIndex]) / 32767.0f, 0.0f, 1.0f);
    return true;
}

// ── Implementation ──────────────────────────────────────────────────────────

FM2Data FM2Parser::Parse(const TArray<uint8>& InData, const TArray<uint8>& SkinData)
{
    FM2Data Result;

    // ── Validate M2 header ──────────────────────────────────────────────
    if (InData.Num() < static_cast<int32>(sizeof(FM2Header)))
    {
        UE_LOG(LogM2, Error, TEXT("M2 data too small (%d bytes)"), InData.Num());
        return Result;
    }

    const uint8* M2Base = InData.GetData();
    const int32  M2Size = InData.Num();
    const FM2Header& Header = *reinterpret_cast<const FM2Header*>(M2Base);

    // Magic check: 'MD20' stored little-endian as 0x3032444D
    if (Header.Magic != 0x3032444D)
    {
        UE_LOG(LogM2, Error, TEXT("Bad M2 magic: 0x%08X"), Header.Magic);
        return Result;
    }

    UE_LOG(LogM2, Log, TEXT("M2 version=%u, verts=%u, textures=%u, bones=%u"),
        Header.Version, Header.nVertices, Header.nTextures, Header.nBones);

    // ── Read model name ─────────────────────────────────────────────────
    FString ModelName;
    if (Header.NameLength > 0 && Header.NameOffset + Header.NameLength <= static_cast<uint32>(M2Size))
    {
        ModelName = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(M2Base + Header.NameOffset)));
    }

    // ── Bounding box ────────────────────────────────────────────────────
    // WoW coordinate system: Z-up, Y-north. Convert to UE: X-forward, Z-up.
    // WoW (X,Y,Z) -> UE (Y, X, Z)  — or just store as-is and let the
    // mesh builder handle coordinate conversion.  We store raw here.
    Result.BoundingBox = FBox(
        FVector(Header.BoundingBox[0], Header.BoundingBox[1], Header.BoundingBox[2]),
        FVector(Header.BoundingBox[3], Header.BoundingBox[4], Header.BoundingBox[5])
    );
    Result.BoundingSphereRadius = Header.BoundingSphereRadius;
    Result.CollisionBox = FBox(
        FVector(Header.CollisionBox[0], Header.CollisionBox[1], Header.CollisionBox[2]),
        FVector(Header.CollisionBox[3], Header.CollisionBox[4], Header.CollisionBox[5])
    );
    Result.CollisionSphereRadius = Header.CollisionSphereRadius;
    Result.NumBones = Header.nBones;

    // M2 files carry a purpose-built collision mesh.  It is normally much
    // tighter than the visible tree mesh because it excludes leaf cards and
    // decorative branches.  Preserve only complete, valid triangles so a
    // damaged asset cannot feed invalid indices into Chaos later.
    const FM2VectorRaw* RawCollisionVertices = nullptr;
    const uint16* RawCollisionIndices = nullptr;
    if (Header.nBoundingVertices >= 3 && Header.nBoundingTriangles >= 3 &&
        SafeRead(M2Base, M2Size, Header.ofsBoundingVertices,
            Header.nBoundingVertices, RawCollisionVertices) &&
        SafeRead(M2Base, M2Size, Header.ofsBoundingTriangles,
            Header.nBoundingTriangles, RawCollisionIndices))
    {
        Result.CollisionVertices.Reserve(Header.nBoundingVertices);
        for (uint32 i = 0; i < Header.nBoundingVertices; ++i)
        {
            Result.CollisionVertices.Add(FVector(
                RawCollisionVertices[i].Value[0],
                RawCollisionVertices[i].Value[1],
                RawCollisionVertices[i].Value[2]));
        }

        const uint32 CompleteIndexCount = Header.nBoundingTriangles -
            (Header.nBoundingTriangles % 3u);
        Result.CollisionIndices.Reserve(CompleteIndexCount);
        for (uint32 i = 0; i < CompleteIndexCount; i += 3)
        {
            const uint16 I0 = RawCollisionIndices[i];
            const uint16 I1 = RawCollisionIndices[i + 1];
            const uint16 I2 = RawCollisionIndices[i + 2];
            if (I0 < Header.nBoundingVertices && I1 < Header.nBoundingVertices &&
                I2 < Header.nBoundingVertices && I0 != I1 && I1 != I2 && I0 != I2)
            {
                Result.CollisionIndices.Add(I0);
                Result.CollisionIndices.Add(I1);
                Result.CollisionIndices.Add(I2);
            }
        }

        if (Result.CollisionIndices.Num() < 3)
        {
            Result.CollisionVertices.Reset();
            Result.CollisionIndices.Reset();
        }
    }

    // ── Read vertices ───────────────────────────────────────────────────
    const FM2VertexRaw* RawVerts = nullptr;
    if (!SafeRead(M2Base, M2Size, Header.ofsVertices, Header.nVertices, RawVerts))
    {
        UE_LOG(LogM2, Error, TEXT("Vertex data out of bounds"));
        return Result;
    }

    Result.Vertices.SetNum(Header.nVertices);
    for (uint32 i = 0; i < Header.nVertices; i++)
    {
        const FM2VertexRaw& Src = RawVerts[i];
        FM2Vertex& Dst = Result.Vertices[i];
        Dst.Position = FVector(Src.Pos[0], Src.Pos[1], Src.Pos[2]);
        Dst.Normal   = FVector(Src.Normal[0], Src.Normal[1], Src.Normal[2]);
        Dst.TexCoord = FVector2D(Src.TexCoords[0], Src.TexCoords[1]);
        Dst.TexCoord2 = FVector2D(Src.TexCoords2[0], Src.TexCoords2[1]);
        FMemory::Memcpy(Dst.BoneWeights, Src.BoneWeights, 4);
        FMemory::Memcpy(Dst.BoneIndices, Src.BoneIndices, 4);
    }

    // ── Read bones ───────────────────────────────────────────────────────
    const FM2BoneRaw* RawBones = nullptr;
    if (Header.nBones > 0 && SafeRead(M2Base, M2Size, Header.ofsBones, Header.nBones, RawBones))
    {
        Result.Bones.SetNum(Header.nBones);
        for (uint32 i = 0; i < Header.nBones; i++)
        {
            const FM2BoneRaw& Src = RawBones[i];
            FM2Bone& Dst = Result.Bones[i];
            Dst.KeyBoneId = Src.KeyBoneId;
            Dst.Flags = Src.Flags;
            Dst.ParentBone = Src.ParentBone;
            Dst.PivotPoint = FVector(Src.PivotPoint[0], Src.PivotPoint[1], Src.PivotPoint[2]);
        }
        UE_LOG(LogM2, Log, TEXT("  Parsed %d bones"), Result.Bones.Num());
    }

    // ── Read animation sequences ────────────────────────────────────────
    const FM2AnimSequenceRaw* RawAnims = nullptr;
    if (Header.nAnimations > 0 && SafeRead(M2Base, M2Size, Header.ofsAnimations, Header.nAnimations, RawAnims))
    {
        Result.Animations.SetNum(Header.nAnimations);
        for (uint32 i = 0; i < Header.nAnimations; i++)
        {
            const FM2AnimSequenceRaw& Src = RawAnims[i];
            FM2AnimSequence& Dst = Result.Animations[i];
            Dst.AnimationId = Src.AnimationId;
            Dst.SubAnimationId = Src.SubAnimationId;
            Dst.Length = Src.Length;
            Dst.MoveSpeed = Src.MoveSpeed;
            Dst.Flags = Src.Flags;
            Dst.NextAnimation = Src.NextAnimation;
            Dst.AliasNext = Src.AliasNext;
            Dst.Probability = Src.Probability;
        }
        UE_LOG(LogM2, Log, TEXT("  Parsed %d animation sequences"), Result.Animations.Num());
    }

    LoadAnimationTracks(Result, InData);

    // ── Read textures ───────────────────────────────────────────────────
    const FM2TextureEntry* TexEntries = nullptr;
    if (Header.nTextures > 0 && SafeRead(M2Base, M2Size, Header.ofsTextures, Header.nTextures, TexEntries))
    {
        Result.TexturePaths.SetNum(Header.nTextures);
        Result.TextureTypes.SetNum(Header.nTextures);
        Result.TextureFlags.SetNum(Header.nTextures);
        for (uint32 i = 0; i < Header.nTextures; i++)
        {
            const FM2TextureEntry& Tex = TexEntries[i];
            Result.TextureTypes[i] = Tex.Type;
            Result.TextureFlags[i] = Tex.Flags;
            if (Tex.Type == 0 && Tex.NameLength > 1 &&
                Tex.NameOffset + Tex.NameLength <= static_cast<uint32>(M2Size))
            {
                // Type 0 = filename-based texture
                FString Path = FString(UTF8_TO_TCHAR(
                    reinterpret_cast<const char*>(M2Base + Tex.NameOffset)));
                // Normalise path separators
                Path.ReplaceInline(TEXT("\\"), TEXT("/"));
                Result.TexturePaths[i] = Path;
            }
            else
            {
                // Type != 0 means a replaceable texture (skin, hair, etc.)
                // Store empty string; the renderer resolves these by type at runtime
                Result.TexturePaths[i] = FString();
            }
        }
    }

    // ── Read render flags ───────────────────────────────────────────────
    struct FRenderFlag { uint16 Flags; uint16 BlendMode; };
    TArray<FRenderFlag> RenderFlags;
    const FM2RenderFlagEntry* RFEntries = nullptr;
    if (Header.nRenderFlags > 0 && SafeRead(M2Base, M2Size, Header.ofsRenderFlags, Header.nRenderFlags, RFEntries))
    {
        RenderFlags.SetNum(Header.nRenderFlags);
        for (uint32 i = 0; i < Header.nRenderFlags; i++)
        {
            RenderFlags[i].Flags     = RFEntries[i].Flags;
            RenderFlags[i].BlendMode = RFEntries[i].BlendMode;
        }
    }

    // ── Read texture lookup table ───────────────────────────────────────
    TArray<uint16> TexLookup;
    const uint16* TexLookupPtr = nullptr;
    if (Header.nTexLookup > 0 && SafeRead(M2Base, M2Size, Header.ofsTexLookup, Header.nTexLookup, TexLookupPtr))
    {
        TexLookup.SetNum(Header.nTexLookup);
        FMemory::Memcpy(TexLookup.GetData(), TexLookupPtr, Header.nTexLookup * sizeof(uint16));
    }

    // ── Parse skin file ─────────────────────────────────────────────────
    if (SkinData.Num() < static_cast<int32>(sizeof(FM2SkinHeader)))
    {
        UE_LOG(LogM2, Error, TEXT("Skin data too small (%d bytes)"), SkinData.Num());
        return Result;
    }

    const uint8* SkinBase = SkinData.GetData();
    const int32  SkinSize = SkinData.Num();
    const FM2SkinHeader& Skin = *reinterpret_cast<const FM2SkinHeader*>(SkinBase);

    if (Skin.Magic != 0x4E494B53) // 'SKIN' in LE
    {
        UE_LOG(LogM2, Error, TEXT("Bad skin magic: 0x%08X"), Skin.Magic);
        return Result;
    }

    // ── Skin vertex indices (lookup into M2 vertex array) ───────────────
    const uint16* SkinVertexIndices = nullptr;
    if (!SafeRead(SkinBase, SkinSize, Skin.ofsIndices, Skin.nIndices, SkinVertexIndices))
    {
        UE_LOG(LogM2, Error, TEXT("Skin vertex indices out of bounds"));
        return Result;
    }

    // ── Skin triangle indices (index into SkinVertexIndices) ────────────
    const uint16* SkinTriangles = nullptr;
    if (!SafeRead(SkinBase, SkinSize, Skin.ofsTriangles, Skin.nTriangles, SkinTriangles))
    {
        UE_LOG(LogM2, Error, TEXT("Skin triangles out of bounds"));
        return Result;
    }

    // ── Submeshes ───────────────────────────────────────────────────────
    const FM2SkinSubmesh* SkinSubmeshes = nullptr;
    if (!SafeRead(SkinBase, SkinSize, Skin.ofsSubmeshes, Skin.nSubmeshes, SkinSubmeshes))
    {
        UE_LOG(LogM2, Error, TEXT("Skin submeshes out of bounds"));
        return Result;
    }

    // ── Texture units ───────────────────────────────────────────────────
    const FM2SkinTextureUnit* SkinTexUnits = nullptr;
    if (!SafeRead(SkinBase, SkinSize, Skin.ofsTextureUnits, Skin.nTextureUnits, SkinTexUnits))
    {
        UE_LOG(LogM2, Error, TEXT("Skin texture units out of bounds"));
        return Result;
    }

    // ── Build final index buffer ────────────────────────────────────────
    // The skin's triangle indices are indices into SkinVertexIndices[],
    // which are themselves indices into the M2's vertex array.
    // We resolve the double-indirection and emit direct vertex indices.
    Result.Indices.SetNum(Skin.nTriangles);
    for (uint32 i = 0; i < Skin.nTriangles; i++)
    {
        uint16 LocalIdx = SkinTriangles[i];
        if (LocalIdx < Skin.nIndices)
        {
            Result.Indices[i] = SkinVertexIndices[LocalIdx];
        }
        else
        {
            UE_LOG(LogM2, Warning, TEXT("Triangle index %u out of range (max %u)"), LocalIdx, Skin.nIndices);
            Result.Indices[i] = 0;
        }
    }

    int32 HoldSequence = INDEX_NONE;
    for (int32 I = 0; I < Result.Animations.Num(); ++I) if (Result.Animations[I].AnimationId == 158) { HoldSequence = I; break; }
    auto ReadMaterialTrack = [&](uint32 Offset, FM2MaterialTrack& Out, int32 ColorComponent = -1, int32 ForceSequence = INDEX_NONE)
    {
        const FM2TrackRaw* Track=nullptr;
        if (!SafeRead(M2Base,M2Size,Offset,1,Track)) return;
        const FM2SubArrayRaw *Times=nullptr,*Values=nullptr;
        if (!Track->TimestampArrayCount || !Track->ValueArrayCount
            || !SafeRead(M2Base,M2Size,Track->TimestampArrayOffset,Track->TimestampArrayCount,Times)
            || !SafeRead(M2Base,M2Size,Track->ValueArrayOffset,Track->ValueArrayCount,Values)) return;
        // Per-sequence keys: use the Stand sequence (what an idle/persistent model loops),
        // not blindly sequence 0. Global-sequence tracks only have one key array.
        const int32 Stand=ForceSequence!=INDEX_NONE ? ForceSequence : Result.StandSequence();
        const uint32 Seq=(Track->GlobalSequence<0 && Track->TimestampArrayCount>uint32(Stand)
            && Track->ValueArrayCount>uint32(Stand)) ? uint32(Stand) : 0u;
        const uint32 Count=FMath::Min(Times[Seq].Count,Values[Seq].Count);
        if (ForceSequence!=INDEX_NONE && Seq==uint32(ForceSequence) && Count==0)
        {
            // Keyed in other sequences but not this one: the client reads 0 (hidden).
            Out.Times.Add(0); Out.Values.Add(0.f); Out.bStep=true;
            Out.Duration=Result.Animations.IsValidIndex(Seq)?Result.Animations[Seq].Length:0;
            return;
        }
        const uint32* Keys=nullptr; const int16* Samples=nullptr; const FM2VectorRaw* Colors=nullptr;
        // Glue material tracks are step/linear; spline tracks need tangent decoding.
        if (!Count || Track->InterpolationType>1
            || !SafeRead(M2Base,M2Size,Times[Seq].Offset,Count,Keys)
            || (ColorComponent<0 ? !SafeRead(M2Base,M2Size,Values[Seq].Offset,Count,Samples)
                : !SafeRead(M2Base,M2Size,Values[Seq].Offset,Count,Colors))) return;
        Out.bStep=Track->InterpolationType==0;
        Out.Duration=Result.Animations.IsValidIndex(Seq)?Result.Animations[Seq].Length:0;
        const uint32* Durations=nullptr;
        if (Track->GlobalSequence>=0 && uint32(Track->GlobalSequence)<Header.nGlobalSequences
            && SafeRead(M2Base,M2Size,Header.ofsGlobalSequences,Header.nGlobalSequences,Durations))
            Out.Duration=Durations[Track->GlobalSequence];
        for(uint32 K=0;K<Count;++K)
        {
            if(K && Keys[K]<Keys[K-1]) { Out=FM2MaterialTrack(); return; }
            Out.Times.Add(Keys[K]);
            const float Value=ColorComponent<0?float(Samples[K])/32767.f:Colors[K].Value[ColorComponent];
            Out.Values.Add(FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,ColorComponent<0?1.f:100.f):1.f);
        }
    };

    // ── Build render passes from texture units ──────────────────────────
    Result.RenderPasses.SetNum(Skin.nTextureUnits);
    for (uint32 i = 0; i < Skin.nTextureUnits; i++)
    {
        const FM2SkinTextureUnit& TU = SkinTexUnits[i];
        FM2RenderPass& Pass = Result.RenderPasses[i];

        Pass.SubmeshIndex = TU.SkinSectionIndex;
        Pass.PriorityPlane = TU.PriorityPlane; // r613
        Pass.bHasHoldTracks = HoldSequence!=INDEX_NONE;
        const uint16* WeightLookup=nullptr;
        if (SafeRead(M2Base,M2Size,Header.ofsTransLookup,Header.nTransLookup,WeightLookup)
            && TU.TransparencyComboIndex<Header.nTransLookup)
        {
            const uint32 Index=WeightLookup[TU.TransparencyComboIndex];
            if (Index<Header.nTransparency)
            {
                ReadMaterialTrack(Header.ofsTransparency+Index*20,Pass.TextureWeight);
                if (HoldSequence!=INDEX_NONE) ReadMaterialTrack(Header.ofsTransparency+Index*20,Pass.HoldTextureWeight,-1,HoldSequence);
            }
        }
        if (TU.ColorIndex>=0 && uint32(TU.ColorIndex)<Header.nColors)
        {
            const uint64 Offset=uint64(Header.ofsColors)+uint64(TU.ColorIndex)*40;
            if (Offset+40<=uint64(M2Size))
            {
                ReadMaterialTrack(uint32(Offset)+20,Pass.ColorAlpha);
                if (HoldSequence!=INDEX_NONE) ReadMaterialTrack(uint32(Offset)+20,Pass.HoldColorAlpha,-1,HoldSequence);
                ReadMaterialTrack(uint32(Offset),Pass.ColorRed,0);
                ReadMaterialTrack(uint32(Offset),Pass.ColorGreen,1);
                ReadMaterialTrack(uint32(Offset),Pass.ColorBlue,2);
            }
        }
        // Global two-key linear UV loops (snow/cloud sheets) are authored in M2.
        // Keep unsupported keyed rotation/scale or non-linear tracks static rather than inventing motion.
        // r615: shared by both texture layers.
        auto ReadUVScroll=[&](uint32 ComboIndex,FVector2D& OutSpeed)
        {
        const uint16* UVLookup=nullptr;
        if(SafeRead(M2Base,M2Size,Header.ofsTexAnimLookup,Header.nTexAnimLookup,UVLookup)
            && ComboIndex<Header.nTexAnimLookup)
        {
            const uint32 UVIndex=UVLookup[ComboIndex];
            const uint64 Offset=uint64(Header.ofsTexAnims)+uint64(UVIndex)*60;
            if(UVIndex<Header.nTexAnims && Offset+60<=uint64(M2Size))
            {
                struct FUVBlock { uint16 Interpolation;int16 Global;uint32 NT,OT,NV,OV; };
                struct FUVArray {uint32 Count,Offset;};
                struct FUVValue {float X,Y,Z;};
                FUVBlock Block;FMemory::Memcpy(&Block,M2Base+Offset,sizeof(Block));
                const FUVArray *Times=nullptr,*Values=nullptr;
                const uint32* Durations=nullptr;
                if(Block.Interpolation==1 && Block.Global>=0 && uint32(Block.Global)<Header.nGlobalSequences
                    && Block.NT>0 && Block.NV>0
                    && SafeRead(M2Base,M2Size,Header.ofsGlobalSequences,Header.nGlobalSequences,Durations)
                    && SafeRead(M2Base,M2Size,Block.OT,Block.NT,Times)
                    && SafeRead(M2Base,M2Size,Block.OV,Block.NV,Values))
                {
                    const uint32* Keys=nullptr;const FUVValue* UV=nullptr;
                    if(Times[0].Count==2 && Values[0].Count==2
                        && SafeRead(M2Base,M2Size,Times[0].Offset,2,Keys)
                        && SafeRead(M2Base,M2Size,Values[0].Offset,2,UV)
                        && Keys[0]==0 && Keys[1]>0 && Keys[1]==Durations[Block.Global]
                        // r639: a start offset of whole texture repeats is the same picture (the login sky
                        // clouds start at -0.998 and scroll down one repeat per 100 s; they stood still).
                        && FMath::Abs(UV[0].X - FMath::RoundToFloat(UV[0].X)) < 0.01f
                        && FMath::Abs(UV[0].Y - FMath::RoundToFloat(UV[0].Y)) < 0.01f
                        && FMath::IsFinite(UV[1].X) && FMath::IsFinite(UV[1].Y))
                        OutSpeed=FVector2D(UV[1].X-UV[0].X,UV[1].Y-UV[0].Y)*(1000.0/double(Keys[1]));
                }
            }
        }
        };
        ReadUVScroll(TU.TexAnimComboIndex,Pass.UVScrollSpeed);
        if (TU.OpCount>=2)
        {
            ReadUVScroll(uint32(TU.TexAnimComboIndex)+1,Pass.UVScrollSpeed2);
            const uint32 Combo2=uint32(TU.TextureComboIndex)+1;
            if (Combo2<uint32(TexLookup.Num())) Pass.TextureIndex2=TexLookup[Combo2];
        }


        // r739: each layer's coordinate source from the texture-coordinate lookup (-1 env map, 0 UV0, 1 UV1).
        const int16* CoordLookup = nullptr;
        if (Header.nTexUnits > 0 && SafeRead(M2Base, M2Size, Header.ofsTexUnits, Header.nTexUnits, CoordLookup))
        {
            if (TU.TexCoordComboIndex < Header.nTexUnits)
                Pass.TexCoordSource = static_cast<int8>(FMath::Clamp<int32>(CoordLookup[TU.TexCoordComboIndex], -1, 1));
            if (TU.OpCount >= 2 && uint32(TU.TexCoordComboIndex) + 1 < Header.nTexUnits)
                Pass.TexCoordSource2 = static_cast<int8>(FMath::Clamp<int32>(CoordLookup[TU.TexCoordComboIndex + 1], -1, 1));
        }

        // Resolve texture index through the texture lookup table
        if (TU.TextureComboIndex < static_cast<uint16>(TexLookup.Num()))
        {
            Pass.TextureIndex = TexLookup[TU.TextureComboIndex];
        }
        else
        {
            Pass.TextureIndex = TU.TextureComboIndex;
        }

        // Resolve render flags
        if (TU.RenderFlagsIndex < static_cast<uint16>(RenderFlags.Num()))
        {
            Pass.Flags     = RenderFlags[TU.RenderFlagsIndex].Flags;
            Pass.BlendMode = RenderFlags[TU.RenderFlagsIndex].BlendMode;
        }
    }

    // ── Parse attachments ────────────────────────────────────────────────
    const FM2AttachmentRaw* RawAttachments = nullptr;
    if (Header.nAttachments > 0 && SafeRead(M2Base, M2Size, Header.ofsAttachments, Header.nAttachments, RawAttachments))
    {
        Result.Attachments.SetNum(Header.nAttachments);
        for (uint32 i = 0; i < Header.nAttachments; i++)
        {
            const FM2AttachmentRaw& Src = RawAttachments[i];
            FM2Attachment& Dst = Result.Attachments[i];
            Dst.Id = Src.Id;
            Dst.Bone = static_cast<int32>(Src.Bone);
            Dst.Position = FVector(Src.Position[0], Src.Position[1], Src.Position[2]);
        }
        UE_LOG(LogM2, Log, TEXT("  Parsed %d attachments"), Result.Attachments.Num());
    }

    // ── Parse attachment lookup ─────────────────────────────────────────
    const int16* RawAttachLookup = nullptr;
    if (Header.nAttachmentLookup > 0 && SafeRead(M2Base, M2Size, Header.ofsAttachmentLookup, Header.nAttachmentLookup, RawAttachLookup))
    {
        Result.AttachmentLookup.SetNum(Header.nAttachmentLookup);
        FMemory::Memcpy(Result.AttachmentLookup.GetData(), RawAttachLookup, Header.nAttachmentLookup * sizeof(int16));
    }

    // ── Parse ribbon emitters ───────────────────────────────────────────
    const FM2RibbonEmitterRaw* RawRibbonEmitters = nullptr;
    if (Header.nRibbonEmitters > 0 &&
        SafeRead(M2Base, M2Size, Header.ofsRibbonEmitters,
            Header.nRibbonEmitters, RawRibbonEmitters))
    {
        Result.RibbonEmitters.Reserve(Header.nRibbonEmitters);
        for (uint32 i = 0; i < Header.nRibbonEmitters; ++i)
        {
            const FM2RibbonEmitterRaw& Src = RawRibbonEmitters[i];
            FM2RibbonEmitter Dst;
            Dst.Id = static_cast<uint32>(Src.Id);
            Dst.Bone = static_cast<int32>(Src.Bone);
            Dst.Position = FVector(Src.Position[0], Src.Position[1], Src.Position[2]);
            Dst.EdgesPerSecond = FMath::Clamp(
                FMath::IsFinite(Src.EdgesPerSecond) ? Src.EdgesPerSecond : 20.0f,
                1.0f, 240.0f);
            Dst.EdgeLifetime = FMath::Clamp(
                FMath::IsFinite(Src.EdgeLifetime) ? Src.EdgeLifetime : 0.5f,
                0.05f, 10.0f);
            Dst.Gravity = FMath::IsFinite(Src.Gravity) ? Src.Gravity : 0.0f;
            Dst.TextureRows = FMath::Max<uint16>(Src.TextureRows, 1);
            Dst.TextureColumns = FMath::Max<uint16>(Src.TextureColumns, 1);

            const uint16* Textures = nullptr;
            if (Src.TextureCount > 0 &&
                SafeRead(M2Base, M2Size, Src.TextureOffset, Src.TextureCount, Textures))
            {
                Dst.Texture = Textures[0];
            }
            const uint16* Materials = nullptr;
            if (Src.MaterialCount > 0 &&
                SafeRead(M2Base, M2Size, Src.MaterialOffset, Src.MaterialCount, Materials))
            {
                Dst.Material = Materials[0];
            }

            FM2VectorRaw Color{{1.0f, 1.0f, 1.0f}};
            int16 Alpha = 32767;
            float HeightAbove = 0.15f;
            float HeightBelow = 0.15f;
            uint8 Visible = 1;
            ReadFirstTrackValue(M2Base, M2Size, Src.ColorTrack, Color);
            ReadFirstTrackValue(M2Base, M2Size, Src.AlphaTrack, Alpha);
            ReadFirstTrackValue(M2Base, M2Size, Src.HeightAboveTrack, HeightAbove);
            ReadFirstTrackValue(M2Base, M2Size, Src.HeightBelowTrack, HeightBelow);
            ReadFirstTrackValue(M2Base, M2Size, Src.VisibilityTrack, Visible);
            Dst.Color = FLinearColor(
                FMath::Clamp(Color.Value[0], 0.0f, 8.0f),
                FMath::Clamp(Color.Value[1], 0.0f, 8.0f),
                FMath::Clamp(Color.Value[2], 0.0f, 8.0f),
                FMath::Clamp(static_cast<float>(Alpha) / 32767.0f, 0.0f, 1.0f));
            Dst.HeightAbove = FMath::Clamp(FMath::Abs(HeightAbove), 0.005f, 20.0f);
            Dst.HeightBelow = FMath::Clamp(FMath::Abs(HeightBelow), 0.005f, 20.0f);
            Dst.bVisible = Visible != 0;
            // Full visibility keys for the played (Stand) sequence, not just the first value.
            {
                FM2TrackRaw Track;
                FMemory::Memcpy(&Track, Src.VisibilityTrack, sizeof(Track));
                const int32 Stand = Result.StandSequence();
                const uint32 Seq = (Track.TimestampArrayCount > uint32(Stand) && Track.ValueArrayCount > uint32(Stand)) ? uint32(Stand) : 0u;
                const FM2SubArrayRaw* TimeArrays = nullptr;
                const FM2SubArrayRaw* ValueArrays = nullptr;
                if (Track.GlobalSequence < 0 && Track.TimestampArrayCount > Seq && Track.ValueArrayCount > Seq
                    && SafeRead(M2Base, M2Size, Track.TimestampArrayOffset, Track.TimestampArrayCount, TimeArrays)
                    && SafeRead(M2Base, M2Size, Track.ValueArrayOffset, Track.ValueArrayCount, ValueArrays))
                {
                    const uint32 Count = FMath::Min(TimeArrays[Seq].Count, ValueArrays[Seq].Count);
                    const uint32* Times = nullptr;
                    const uint8* Values = nullptr;
                    if (Count > 0 && Count <= 256
                        && SafeRead(M2Base, M2Size, TimeArrays[Seq].Offset, Count, Times)
                        && SafeRead(M2Base, M2Size, ValueArrays[Seq].Offset, Count, Values))
                    {
                        for (uint32 K = 0; K < Count; ++K)
                        {
                            Dst.VisibilityTimes.Add(Times[K]);
                            Dst.VisibilityValues.Add(Values[K]);
                        }
                        Dst.bVisible = Values[0] != 0;
                    }
                }
            }
            Result.RibbonEmitters.Add(Dst);
        }
        UE_LOG(LogM2, Log, TEXT("  Parsed %d ribbon emitters"), Result.RibbonEmitters.Num());
    }

    // ── Parse particle emitters ─────────────────────────────────────────
    const FM2ParticleEmitterRaw* RawParticleEmitters = nullptr;
    if (Header.nParticleEmitters > 0 && SafeRead(M2Base, M2Size, Header.ofsParticleEmitters, Header.nParticleEmitters, RawParticleEmitters))
    {
        Result.ParticleEmitters.SetNum(Header.nParticleEmitters);
        for (uint32 i = 0; i < Header.nParticleEmitters; i++)
        {
            const FM2ParticleEmitterRaw& Src = RawParticleEmitters[i];
            FM2ParticleEmitter& Dst = Result.ParticleEmitters[i];

            Dst.Id = Src.Id;
            Dst.Flags = Src.Flags;
            Dst.Position = FVector(Src.Position[0], Src.Position[1], Src.Position[2]);
            Dst.Bone = static_cast<int16>(Src.Bone);
            Dst.Texture = Src.Texture;
            Dst.BlendingType = FString::FromInt(Src.BlendingType);
            Dst.BlendMode = Src.BlendingType;
            Dst.EmitterType = Src.EmitterType;
            Dst.ParticleType = Src.ParticleType;
            Dst.HeadorTail = Src.HeadorTail;
            Dst.TextureRows = FMath::Max<uint16>(Src.TextureRows, 1);
            Dst.TextureColumns = FMath::Max<uint16>(Src.TextureColumns, 1);
            const uint8* ParticleBase = reinterpret_cast<const uint8*>(&Src);

            // Enabled for the sequence the model idles in (Stand). Gameobjects such as
            // G_BookTrapFire keep their fire off in Stand and only light it in another
            // sequence; the first non-empty key of any sequence made them burn forever.
            uint8 Enabled = 1;
            {
                FM2TrackRaw Track;
                FMemory::Memcpy(&Track, ParticleBase + 456, sizeof(Track));
                const int32 Stand = Result.StandSequence();
                const FM2SubArrayRaw* ValueArrays = nullptr;
                const FM2SubArrayRaw* TimeArrays = nullptr;
                const uint8* StandValues = nullptr;
                const uint32* StandTimes = nullptr;
                if (Track.GlobalSequence < 0 && Stand >= 0 && Track.ValueArrayCount > uint32(Stand)
                    && SafeRead(M2Base, M2Size, Track.ValueArrayOffset, Track.ValueArrayCount, ValueArrays)
                    && ValueArrays[Stand].Count > 0
                    && SafeRead(M2Base, M2Size, ValueArrays[Stand].Offset, ValueArrays[Stand].Count, StandValues))
                {
                    // r345: the state the sequence spends most of its time in, not its first key. Water
                    // Shield's orbs key 0 at 0 ms and 1 from 200 ms (a birth delay): the first key hid them.
                    Enabled = StandValues[0];
                    const uint32 Keys = ValueArrays[Stand].Count;
                    if (Keys > 1 && Track.TimestampArrayCount > uint32(Stand)
                        && SafeRead(M2Base, M2Size, Track.TimestampArrayOffset, Track.TimestampArrayCount, TimeArrays)
                        && TimeArrays[Stand].Count == Keys
                        && SafeRead(M2Base, M2Size, TimeArrays[Stand].Offset, Keys, StandTimes)
                        && Result.Animations.IsValidIndex(Stand))
                    {
                        const uint32 Length = FMath::Max(Result.Animations[Stand].Length, StandTimes[Keys - 1]);
                        uint64 OnTime = 0, OffTime = 0;
                        for (uint32 K = 0; K < Keys; ++K)
                        {
                            const uint32 End = K + 1 < Keys ? StandTimes[K + 1] : Length;
                            const uint32 Held = End > StandTimes[K] ? End - StandTimes[K] : 0;
                            (StandValues[K] ? OnTime : OffTime) += Held;
                        }
                        if (OnTime + OffTime > 0) Enabled = OnTime > OffTime ? 1 : 0;
                        for (uint32 K = 0; K < Keys; ++K) { Dst.EnabledTimes.Add(StandTimes[K]); Dst.EnabledValues.Add(StandValues[K]); }
                    }
                }
                else
                    ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 456, Enabled);
            }
            Dst.bEnabledInStand = Enabled != 0;
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 52, Dst.EmissionSpeed);
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 72, Dst.SpeedVariation);
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 92, Dst.VerticalRange);
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 112, Dst.HorizontalRange);
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 132, Dst.Gravity);
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 152, Dst.Lifespan);
            FMemory::Memcpy(&Dst.LifespanVariation, ParticleBase + 172, sizeof(float));
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 176, Dst.EmissionRate);
            // r317 (Stefan: an Ethereal's hands were wrapped in big fiery puffs): emission rate is
            // per sequence like the enabled track. Ethereal emitters 12-19 only have a rate in Death
            // and emitter 0 has none at all, but the first non-empty key (or the 10/s default) made
            // them emit forever. Use the Stand sequence's rate; no Stand key (or no keys) = no emission.
            {
                FM2TrackRaw RateTrack;
                FMemory::Memcpy(&RateTrack, ParticleBase + 176, sizeof(RateTrack));
                const FM2SubArrayRaw* RateArrays = nullptr;
                if (RateTrack.ValueArrayCount > 0 &&
                    SafeRead(M2Base, M2Size, RateTrack.ValueArrayOffset, RateTrack.ValueArrayCount, RateArrays))
                {
                    bool bAnyKeys = false;
                    for (uint32 K = 0; K < RateTrack.ValueArrayCount; ++K) bAnyKeys |= RateArrays[K].Count > 0;
                    const int32 Stand = Result.StandSequence();
                    if (!bAnyKeys) Dst.EmissionRate = 0.0f;
                    else if (RateTrack.GlobalSequence < 0 && Stand >= 0 && RateTrack.ValueArrayCount > uint32(Stand))
                    {
                        const float* StandRate = nullptr;
                        if (RateArrays[Stand].Count == 0) Dst.EmissionRate = 0.0f;
                        else if (SafeRead(M2Base, M2Size, RateArrays[Stand].Offset, 1, StandRate)) Dst.EmissionRate = StandRate[0];
                    }
                }
            }
            FMemory::Memcpy(&Dst.EmissionRateVariation, ParticleBase + 196, sizeof(float));
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 200, Dst.EmissionAreaWidth);
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 220, Dst.EmissionAreaLength);
            ReadFirstTrackValue(M2Base, M2Size, ParticleBase + 240, Dst.ZSource);

            FM2ParticleColorRaw StartColor{255.0f, 255.0f, 255.0f};
            FM2ParticleColorRaw MiddleColor = StartColor;
            FM2ParticleColorRaw EndColor = StartColor;
            float MiddleTime = 0.5f;
            if (ReadParticleLifetimeTrack(M2Base, M2Size, ParticleBase + 260,
                StartColor, MiddleColor, EndColor, MiddleTime))
            {
                auto StoreColor = [](const FM2ParticleColorRaw& Source, uint32* Destination)
                {
                    Destination[0] = FMath::Clamp(FMath::RoundToInt(Source.R), 0, 255);
                    Destination[1] = FMath::Clamp(FMath::RoundToInt(Source.G), 0, 255);
                    Destination[2] = FMath::Clamp(FMath::RoundToInt(Source.B), 0, 255);
                };
                StoreColor(StartColor, Dst.ColorStart);
                StoreColor(MiddleColor, Dst.ColorMiddle);
                StoreColor(EndColor, Dst.ColorEnd);
                Dst.MiddleTime = MiddleTime;
            }

            int16 StartAlpha = 32767;
            int16 MiddleAlpha = 32767;
            int16 EndAlpha = 32767;
            if (ReadParticleLifetimeTrack(M2Base, M2Size, ParticleBase + 276,
                StartAlpha, MiddleAlpha, EndAlpha, MiddleTime))
            {
                auto StoreAlpha = [](int16 Value)
                {
                    return static_cast<uint8>(FMath::Clamp(
                        FMath::RoundToInt(static_cast<float>(Value) * 255.0f / 32767.0f),
                        0, 255));
                };
                Dst.AlphaStart = StoreAlpha(StartAlpha);
                Dst.AlphaMiddle = StoreAlpha(MiddleAlpha);
                Dst.AlphaEnd = StoreAlpha(EndAlpha);
                Dst.MiddleTime = MiddleTime;
            }

            FM2ParticleScaleRaw StartScale{1.0f, 1.0f};
            FM2ParticleScaleRaw MiddleScale = StartScale;
            FM2ParticleScaleRaw EndScale = StartScale;
            if (ReadParticleLifetimeTrack(M2Base, M2Size, ParticleBase + 292,
                StartScale, MiddleScale, EndScale, MiddleTime))
            {
                auto AverageScale = [](const FM2ParticleScaleRaw& Scale)
                {
                    return FMath::Max(0.0f, (Scale.X + Scale.Y) * 0.5f);
                };
                Dst.ScaleStart = AverageScale(StartScale);
                Dst.ScaleMiddle = AverageScale(MiddleScale);
                Dst.ScaleEnd = AverageScale(EndScale);
                Dst.MiddleTime = MiddleTime;
            }

            FM2ParticleScaleRaw ScaleVariation{0.0f, 0.0f};
            FMemory::Memcpy(&ScaleVariation, ParticleBase + 308, sizeof(ScaleVariation));
            Dst.ScaleVariation = FMath::Max(
                FMath::Abs(ScaleVariation.X), FMath::Abs(ScaleVariation.Y));

            uint16 StartCell = 0;
            uint16 MiddleCell = 0;
            uint16 EndCell = 0;
            if (ReadParticleLifetimeTrack(M2Base, M2Size, ParticleBase + 316,
                StartCell, MiddleCell, EndCell, MiddleTime))
            {
                const uint16 LastCell = static_cast<uint16>(FMath::Max(
                    static_cast<int32>(Dst.TextureRows) * static_cast<int32>(Dst.TextureColumns) - 1, 0));
                Dst.HeadCellStart = FMath::Min(StartCell, LastCell);
                Dst.HeadCellMiddle = FMath::Min(MiddleCell, LastCell);
                Dst.HeadCellEnd = FMath::Min(EndCell, LastCell);
                Dst.MiddleTime = MiddleTime;
            }

            FMemory::Memcpy(&Dst.TailLength, ParticleBase + 348, sizeof(float));

            // Preserve the old millisecond fields for callers that have not
            // moved to the decoded WotLK lifespan yet.
            Dst.Lifespan = FMath::Clamp(Dst.Lifespan, 0.05f, 30.0f);
            Dst.LifespanVariation = FMath::Clamp(Dst.LifespanVariation, 0.0f, Dst.Lifespan * 0.95f);
            Dst.HeadLifeStart = Dst.Lifespan * 1000.0f;
            Dst.HeadLifeEnd = Dst.HeadLifeStart;
            Dst.HeadLifeRepeat = 0.0f;
            Dst.HeadDecay = 0.1f;

            Dst.TailLifeStart = Dst.HeadLifeStart;
            Dst.TailLifeEnd = Dst.HeadLifeEnd;
            Dst.TailLifeRepeat = 0.0f;
            Dst.TailDecay = 0.2f;

            Dst.EmissionRate = FMath::Clamp(Dst.EmissionRate, 0.0f, 1000.0f);
            Dst.EmissionSpeed = FMath::Clamp(Dst.EmissionSpeed, -1000.0f, 1000.0f);
            Dst.SpeedVariation = FMath::Clamp(Dst.SpeedVariation, 0.0f, 1.0f);
            Dst.EmissionAreaLength = FMath::Clamp(Dst.EmissionAreaLength, 0.0f, 1000.0f);
            Dst.EmissionAreaWidth = FMath::Clamp(Dst.EmissionAreaWidth, 0.0f, 1000.0f);
        }
        UE_LOG(LogM2, Log, TEXT("  Parsed %d particle emitters"), Result.ParticleEmitters.Num());
    }

    // ── Parse submeshes from skin file ──────────────────────────────────
    if (SkinData.Num() >= static_cast<int32>(sizeof(FM2SkinHeader)))
    {
        const FM2SkinHeader* SkinHeader = reinterpret_cast<const FM2SkinHeader*>(SkinData.GetData());
        const FM2SkinSubmesh* RawSubmeshes = nullptr;
        if (SkinHeader->nSubmeshes > 0 && SafeRead(SkinData.GetData(), SkinData.Num(), SkinHeader->ofsSubmeshes, SkinHeader->nSubmeshes, RawSubmeshes))
        {
            Result.Submeshes.SetNum(SkinHeader->nSubmeshes);
            for (uint32 i = 0; i < SkinHeader->nSubmeshes; i++)
            {
                const FM2SkinSubmesh& Src = RawSubmeshes[i];
                FM2Submesh& Dst = Result.Submeshes[i];
                Dst.Id = Src.ID;
                Dst.StartVertex = Src.StartVertex;
                Dst.NumVertices = Src.nVertices;
                // Skins with more than 65535 indices (HD character models) store the
                // high bits of the start offset in Level.
                Dst.StartTriangle = uint32(Src.StartTriangle) | (uint32(Src.Level) << 16);
                Dst.NumTriangles = Src.nTriangles;
                Dst.CenterMass = FVector(Src.CenterMass[0], Src.CenterMass[1], Src.CenterMass[2]);
                Dst.Radius = Src.Radius;
            }

            UE_LOG(LogM2, Log, TEXT("  Parsed %d submeshes from skin"), Result.Submeshes.Num());
        }
    }

    Result.bIsValid = true;
    UE_LOG(LogM2, Log, TEXT("M2 '%s' parsed: %d verts, %d indices, %d collision verts, %d collision indices, %d passes, %d textures, %d attachments, %d submeshes, %d particle emitters, %d ribbons"),
        *ModelName, Result.Vertices.Num(), Result.Indices.Num(),
        Result.CollisionVertices.Num(), Result.CollisionIndices.Num(),
        Result.RenderPasses.Num(), Result.TexturePaths.Num(),
        Result.Attachments.Num(), Result.Submeshes.Num(), Result.ParticleEmitters.Num(),
        Result.RibbonEmitters.Num());
    return Result;
}

void FM2Parser::LoadAnimationTracks(FM2Data& Model, const TArray<uint8>& Bytes,
    const FAnimationReader& ReadAnimation, const TSet<int32>* AnimationIds)
{
    if (Bytes.Num() < sizeof(FM2Header)) return;
    const uint8* Base = Bytes.GetData();
    const int32 Size = Bytes.Num();
    const FM2Header& Header = *reinterpret_cast<const FM2Header*>(Base);
    const FM2AnimSequenceRaw* Sequences = nullptr;
    const FM2BoneRaw* Bones = nullptr;
    if (Header.Magic != 0x3032444D ||
        !SafeRead(Base, Size, Header.ofsAnimations, Header.nAnimations, Sequences) ||
        !SafeRead(Base, Size, Header.ofsBones, Header.nBones, Bones)) return;
    const uint32* Globals = nullptr;
    SafeRead(Base, Size, Header.ofsGlobalSequences, Header.nGlobalSequences, Globals);
    struct FBlock { uint16 Interpolation; int16 Global; uint32 NT, OT, NV, OV; };
    struct FArray { uint32 Count, Offset; };
    struct FVectorKey { float X,Y,Z; };
    struct FQuatKey { int16 X,Y,Z,W; };
    auto Unpack = [](int16 V) { return (V < 0 ? V + 32768 : V - 32767) / 32767.0; };
    for (uint32 Index = 0; Index < Header.nAnimations; ++Index)
    {
        const auto& Requested = Sequences[Index];
        if (AnimationIds && !AnimationIds->Contains(Requested.AnimationId)) continue;
        if (Model.AnimationTracks.ContainsByPredicate([&](const FM2AnimationData& Clip)
            { return Clip.AnimationId == Requested.AnimationId && Clip.SubAnimationId == Requested.SubAnimationId; })) continue;
        uint32 SourceIndex = Index;
        uint32 Hops = 0;
        while (SourceIndex < Header.nAnimations && (Sequences[SourceIndex].Flags & 0x40) && Hops++ < Header.nAnimations)
            SourceIndex = Sequences[SourceIndex].AliasNext;
        if (SourceIndex >= Header.nAnimations || Hops >= Header.nAnimations) continue;
        const auto& Source = Sequences[SourceIndex];
        TArray<uint8> External;
        const bool bExternal = (Source.Flags & 0x130) == 0;
        if (bExternal && (!ReadAnimation || !ReadAnimation(Source.AnimationId, Source.SubAnimationId, External)
            || External.IsEmpty())) continue; // Never reinterpret M2 bytes as missing .anim keys.
        const uint8* Payload = bExternal ? External.GetData() : Base;
        const int32 PayloadSize = bExternal ? External.Num() : Size;
        FM2AnimationData Clip;
        Clip.AnimationId = Requested.AnimationId;
        Clip.SubAnimationId = Requested.SubAnimationId;
        Clip.Duration = Source.Length;
        Clip.MoveSpeed = Requested.MoveSpeed;
        // 0x20 means embedded data, not looping. 0x80 is a non-looping sequence.
        Clip.bIsLooping = Requested.AnimationId != 1 && (Source.Flags & 0x80) == 0;
        Clip.BoneTracks.SetNum(Header.nBones);
        bool bAny = false;
        bool bValid = Clip.Duration > 0;
        for (uint32 BoneIndex = 0; BoneIndex < Header.nBones && bValid; ++BoneIndex)
        {
            auto ReadTrack = [&](const uint8* BlockBytes, auto* KeyType, auto Convert,
                TArray<uint32>& Times, auto& Values, uint32& GlobalDuration)
            {
                using Key = std::remove_pointer_t<decltype(KeyType)>;
                const FBlock& Block = *reinterpret_cast<const FBlock*>(BlockBytes);
                const bool bGlobal = Block.Global >= 0;
                const uint32 Slot = bGlobal ? 0 : SourceIndex;
                if (Slot >= Block.NT || Slot >= Block.NV) return;
                const FArray *TimeArrays = nullptr, *ValueArrays = nullptr;
                if (!SafeRead(Base, Size, Block.OT, Block.NT, TimeArrays) ||
                    !SafeRead(Base, Size, Block.OV, Block.NV, ValueArrays)) { bValid = false; return; }
                const FArray& T = TimeArrays[Slot];
                const FArray& V = ValueArrays[Slot];
                if (T.Count == 0 && V.Count == 0) return;
                const uint8* TrackBase = bGlobal ? Base : Payload;
                const int32 TrackSize = bGlobal ? Size : PayloadSize;
                const uint32* RawTimes = nullptr;
                const Key* RawValues = nullptr;
                if (T.Count != V.Count || !SafeRead(TrackBase, TrackSize, T.Offset, T.Count, RawTimes) ||
                    !SafeRead(TrackBase, TrackSize, V.Offset, V.Count, RawValues)) { bValid = false; return; }
                if (bGlobal)
                {
                    if (!Globals || uint32(Block.Global) >= Header.nGlobalSequences) { bValid = false; return; }
                    GlobalDuration = Globals[Block.Global];
                }
                for (uint32 K = 0; K < T.Count; ++K)
                {
                    auto Value = Convert(RawValues[K]);
                    if (Value.ContainsNaN() || (K && RawTimes[K] < RawTimes[K-1])) { bValid = false; return; }
                    Times.Add(RawTimes[K]);
                    Values.Add(Value);
                }
                bAny |= T.Count > 0;
            };
            auto Vector = [](const FVectorKey& V) { return FVector(V.X,V.Y,V.Z); };
            auto Rotation = [&](const FQuatKey& V) { return FQuat(Unpack(V.X),Unpack(V.Y),Unpack(V.Z),Unpack(V.W)); };
            auto& Track = Clip.BoneTracks[BoneIndex];
            ReadTrack(Bones[BoneIndex].TransBlock, (FVectorKey*)nullptr, Vector,
                Track.TransTimestamps, Track.TransValues, Track.TransGlobalDuration);
            ReadTrack(Bones[BoneIndex].RotBlock, (FQuatKey*)nullptr, Rotation,
                Track.RotTimestamps, Track.RotValues, Track.RotGlobalDuration);
            ReadTrack(Bones[BoneIndex].ScaleBlock, (FVectorKey*)nullptr, Vector,
                Track.ScaleTimestamps, Track.ScaleValues, Track.ScaleGlobalDuration);
        }
        if (bValid && bAny) Model.AnimationTracks.Add(MoveTemp(Clip));
        else if (!bValid) UE_LOG(LogM2, Warning, TEXT("Rejected invalid animation %u-%u"), Requested.AnimationId, Requested.SubAnimationId);
    }
}
