#include "Formats/AdtParser.h"
#include "Formats/ChunkId.h"
#include "Coord/WowCoordinate.h"

DEFINE_LOG_CATEGORY_STATIC(LogAdt, Log, All);

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace
{
    // Chunk magics as they appear on disk (little-endian reversed)
    const uint32 MAGIC_MVER = MakeFourCC('R','E','V','M');
    const uint32 MAGIC_MHDR = MakeFourCC('R','D','H','M');
    const uint32 MAGIC_MCIN = MakeFourCC('N','I','C','M');
    const uint32 MAGIC_MTEX = MakeFourCC('X','E','T','M');
    const uint32 MAGIC_MMDX = MakeFourCC('X','D','M','M');
    const uint32 MAGIC_MMID = MakeFourCC('D','I','M','M');
    const uint32 MAGIC_MWMO = MakeFourCC('O','M','W','M');
    const uint32 MAGIC_MWID = MakeFourCC('D','I','W','M');
    const uint32 MAGIC_MDDF = MakeFourCC('F','D','D','M');
    const uint32 MAGIC_MODF = MakeFourCC('F','D','O','M');
    const uint32 MAGIC_MH2O = MakeFourCC('O','2','H','M');
    const uint32 MAGIC_MCNK = MakeFourCC('K','N','C','M');
    const uint32 MAGIC_MCVT = MakeFourCC('T','V','C','M');
    const uint32 MAGIC_MCNR = MakeFourCC('R','N','C','M');
    const uint32 MAGIC_MCLY = MakeFourCC('Y','L','C','M');
    const uint32 MAGIC_MCAL = MakeFourCC('L','A','C','M');
    const uint32 MAGIC_MCSH = MakeFourCC('H','S','C','M');
    const uint32 MAGIC_MCCV = MakeFourCC('V','C','C','M');

    // MCNK header flags
    const uint32 MCNK_FLAG_HAS_MCSH = 0x01;
    const uint32 MCNK_FLAG_HAS_MCCV = 0x40;
    const uint32 MCNK_FLAG_DO_NOT_FIX_ALPHA = 0x8000;

    // MCLY layer flags
    const uint32 MCLY_FLAG_USE_ALPHA       = 0x100;
    const uint32 MCLY_FLAG_ALPHA_COMPRESSED = 0x200;

    // Safe reader that wraps the raw byte array
    struct FBinaryReader
    {
        const uint8* Base;
        int32 Size;
        int32 Pos;

        FBinaryReader(const TArray<uint8>& Data)
            : Base(Data.GetData()), Size(Data.Num()), Pos(0) {}

        bool CanRead(int32 Bytes) const { return Pos + Bytes <= Size; }
        void Seek(int32 NewPos) { Pos = NewPos; }
        void Skip(int32 Bytes) { Pos += Bytes; }

        uint8  ReadU8()  { uint8  V = 0; if (CanRead(1)) { V = Base[Pos]; Pos += 1; } return V; }
        int8   ReadI8()  { return static_cast<int8>(ReadU8()); }
        uint16 ReadU16() { uint16 V = 0; if (CanRead(2)) { FMemory::Memcpy(&V, Base + Pos, 2); Pos += 2; } return V; }
        uint32 ReadU32() { uint32 V = 0; if (CanRead(4)) { FMemory::Memcpy(&V, Base + Pos, 4); Pos += 4; } return V; }
        float  ReadF32() { float  V = 0; if (CanRead(4)) { FMemory::Memcpy(&V, Base + Pos, 4); Pos += 4; } return V; }

        void ReadBytes(void* Dst, int32 Count)
        {
            if (CanRead(Count)) { FMemory::Memcpy(Dst, Base + Pos, Count); Pos += Count; }
        }
    };

    // Read a chunk header: 4-byte magic + 4-byte size.  Returns false on EOF.
    bool ReadChunkHeader(FBinaryReader& R, uint32& OutMagic, uint32& OutSize)
    {
        if (!R.CanRead(8)) return false;
        OutMagic = R.ReadU32();
        OutSize  = R.ReadU32();
        return true;
    }

    // Parse null-separated string block into an array of FStrings
    TArray<FString> ParseStringBlock(FBinaryReader& R, uint32 BlockSize)
    {
        TArray<FString> Result;
        if (BlockSize == 0) return Result;

        const int32 BlockStart = R.Pos;
        const int32 BlockEnd   = BlockStart + static_cast<int32>(BlockSize);

        while (R.Pos < BlockEnd)
        {
            const char* Start = reinterpret_cast<const char*>(R.Base + R.Pos);
            int32 Len = 0;
            while (R.Pos + Len < BlockEnd && Start[Len] != '\0') { ++Len; }
            if (Len > 0)
            {
                Result.Add(FString(Len, ANSI_TO_TCHAR(Start)));
            }
            R.Pos += Len + 1; // skip past the null
        }
        R.Seek(BlockEnd);
        return Result;
    }

    // Resolve an offset table (MMID/MWID) into string indices by matching offsets
    // into the corresponding string block.
    // OffsetTable: array of uint32 offsets into the raw string block
    // StringBlock: the raw bytes of the string block
    // Returns: for each entry in OffsetTable, the index into Paths that starts at that offset
    TArray<int32> BuildOffsetToIndexMap(const TArray<uint32>& OffsetTable,
                                        const uint8* StringBlockBase, uint32 StringBlockSize)
    {
        // Build a map from byte-offset → string index
        TMap<uint32, int32> OffsetToIndex;
        int32 Idx = 0;
        uint32 Off = 0;
        while (Off < StringBlockSize)
        {
            OffsetToIndex.Add(Off, Idx);
            // skip to next null
            while (Off < StringBlockSize && StringBlockBase[Off] != 0) ++Off;
            ++Off; // skip null
            ++Idx;
        }

        TArray<int32> Result;
        Result.SetNum(OffsetTable.Num());
        for (int32 i = 0; i < OffsetTable.Num(); ++i)
        {
            const int32* Found = OffsetToIndex.Find(OffsetTable[i]);
            Result[i] = Found ? *Found : 0;
        }
        return Result;
    }

    // ── MCNK sub-chunk parsing ──────────────────────────────────────────────

    void ParseMCVT(FBinaryReader& R, FAdtChunkData& Chunk)
    {
        for (int32 i = 0; i < 145; ++i)
        {
            float H = R.ReadF32();
            // MCVT heights are small deltas relative to chunk base, typically -50 to +50
            if (!FMath::IsFinite(H) || H > 500.0f || H < -500.0f)
            {
                H = 0.0f;
            }
            Chunk.Heights[i] = H;
        }
    }

    void ParseMCNR(FBinaryReader& R, FAdtChunkData& Chunk)
    {
        // 145 normals, each 3 × int8 in X, Z, Y order
        for (int32 i = 0; i < 145; ++i)
        {
            int8 Nx = R.ReadI8();
            int8 Nz = R.ReadI8();
            int8 Ny = R.ReadI8();
            // Normalize from [-127,127] to [-1,1]
            Chunk.Normals[i] = FVector(
                static_cast<float>(Nx) / 127.0f,
                static_cast<float>(Ny) / 127.0f,
                static_cast<float>(Nz) / 127.0f
            );
        }
        // 13 bytes padding — skip
        R.Skip(13);
    }

    struct FMclyEntry
    {
        uint32 TextureId;
        uint32 Flags;
        uint32 OfsAlpha;
        uint32 EffectId;
    };

    TArray<FMclyEntry> ParseMCLY(FBinaryReader& R, uint32 ChunkSize)
    {
        TArray<FMclyEntry> Layers;
        const int32 Count = static_cast<int32>(ChunkSize) / 16;
        Layers.Reserve(Count);
        for (int32 i = 0; i < Count; ++i)
        {
            FMclyEntry E;
            E.TextureId = R.ReadU32();
            E.Flags     = R.ReadU32();
            E.OfsAlpha  = R.ReadU32();
            E.EffectId  = R.ReadU32();
            Layers.Add(E);
        }
        return Layers;
    }

    void ParseMCAL(FBinaryReader& R, uint32 McalTotalSize, const TArray<FMclyEntry>& Layers,
                   bool bBigAlpha, bool bDoNotFixAlpha, FAdtChunkData& Chunk)
    {
        // The start of the MCAL data block (after its 8-byte chunk header)
        const uint32 McalDataStart = static_cast<uint32>(R.Pos);

        for (int32 LayerIdx = 0; LayerIdx < Layers.Num(); ++LayerIdx)
        {
            // Layer 0 has no alpha map
            if (LayerIdx == 0)
                continue;

            const FMclyEntry& Layer = Layers[LayerIdx];

            // Seek to the layer's alpha data (offset relative to MCAL data start)
            if (static_cast<uint64>(McalDataStart) + Layer.OfsAlpha >= R.Size) continue;
            R.Seek(McalDataStart + Layer.OfsAlpha);

            TArray<uint8> AlphaMap;
            AlphaMap.SetNum(4096); // 64×64

            const bool bCompressed = (Layer.Flags & MCLY_FLAG_ALPHA_COMPRESSED) != 0;

            if (bCompressed)
            {
                // RLE compressed — always produces 4096 output values
                int32 OutPos = 0;
                while (OutPos < 4096)
                {
                    uint8 Control = R.ReadU8();
                    bool bFill   = (Control & 0x80) != 0;
                    int32 Count  = Control & 0x7F;

                    if (bFill)
                    {
                        uint8 Value = R.ReadU8();
                        for (int32 j = 0; j < Count && OutPos < 4096; ++j)
                        {
                            AlphaMap[OutPos++] = Value;
                        }
                    }
                    else
                    {
                        for (int32 j = 0; j < Count && OutPos < 4096; ++j)
                        {
                            AlphaMap[OutPos++] = R.ReadU8();
                        }
                    }
                }
            }
            else if (bBigAlpha)
            {
                // Uncompressed 64×64, 1 byte per pixel = 4096 bytes
                for (int32 j = 0; j < 4096; ++j)
                {
                    AlphaMap[j] = R.ReadU8();
                }
            }
            else
            {
                // Uncompressed 32×64 packed: 4 bits per pixel, 2 pixels per byte = 2048 bytes
                // Produces a 64×64 alpha map
                int32 OutPos = 0;
                for (int32 j = 0; j < 2048 && OutPos < 4096; ++j)
                {
                    uint8 Byte = R.ReadU8();
                    uint8 Lo = (Byte & 0x0F);
                    uint8 Hi = (Byte & 0xF0) >> 4;
                    // Scale from [0,15] to [0,255]
                    AlphaMap[OutPos++] = static_cast<uint8>((Lo * 255) / 15);
                    if (OutPos < 4096)
                        AlphaMap[OutPos++] = static_cast<uint8>((Hi * 255) / 15);
                }

                // Fix broken alpha: copy second-to-last column to last column,
                // and second-to-last row to last row
                if (!bDoNotFixAlpha)
                {
                    for (int32 Row = 0; Row < 64; ++Row)
                    {
                        AlphaMap[Row * 64 + 63] = AlphaMap[Row * 64 + 62];
                    }
                    for (int32 Col = 0; Col < 64; ++Col)
                    {
                        AlphaMap[63 * 64 + Col] = AlphaMap[62 * 64 + Col];
                    }
                }
            }

            Chunk.AlphaMaps.Add(MoveTemp(AlphaMap));
        }
    }

    void ParseMCCV(FBinaryReader& R, FAdtChunkData& Chunk)
    {
        Chunk.bHasVertexColors = true;
        // 145 × 4 bytes BGRA
        for (int32 i = 0; i < 145; ++i)
        {
            uint8 B = R.ReadU8();
            uint8 G = R.ReadU8();
            uint8 Rv = R.ReadU8();
            uint8 A = R.ReadU8();
            Chunk.VertexColors[i] = FColor(Rv, G, B, A);
        }
    }

    // ── Parse a single MCNK chunk ───────────────────────────────────────────

    void ParseMCNK(FBinaryReader& R, uint32 McnkSize, bool bBigAlpha, FAdtChunkData& Chunk)
    {
        // MCNK sub-chunk offsets are relative to the MCNK chunk START (including magic+size).
        // R.Pos is right after reading the MCNK magic+size (8 bytes).
        // Subtract 8 to get the start of the MCNK chunk.
        const int32 McnkDataStart = R.Pos - 8;
        const int32 McnkDataEnd   = McnkDataStart + static_cast<int32>(McnkSize);

        // ── Read the 128-byte MCNK header ───────────────────────────────────
        uint32 Flags        = R.ReadU32();
        uint32 Ix           = R.ReadU32();
        uint32 Iy           = R.ReadU32();
        uint32 NLayers      = R.ReadU32();
        uint32 NDoodadRefs  = R.ReadU32(); // not used here
        uint32 OfsHeight    = R.ReadU32();
        uint32 OfsNormal    = R.ReadU32();
        uint32 OfsLayer     = R.ReadU32();
        uint32 OfsRefs      = R.ReadU32(); // MCRF, skip
        uint32 OfsAlpha     = R.ReadU32();
        uint32 SizeAlpha    = R.ReadU32();
        uint32 OfsShadow    = R.ReadU32();
        uint32 SizeShadow   = R.ReadU32();
        uint32 AreaId       = R.ReadU32();
        uint32 NMapObjRefs  = R.ReadU32(); // not used here
        uint16 HolesLow     = R.ReadU16();
        uint16 Unknown      = R.ReadU16(); // unknown_but_used

        // The client uses these compact 8x8 maps to select which of the four
        // texture layers supplies ground clutter and where clutter is disabled.
        for (int32 Row = 0; Row < 8; ++Row)
        {
            uint16 LayerRow = R.ReadU16();
            for (int32 Col = 0; Col < 8; ++Col)
            {
                Chunk.GroundEffectLayerMap[Row * 8 + Col] = static_cast<uint8>((LayerRow >> (Col * 2)) & 0x3u);
            }
        }
        for (int32 Row = 0; Row < 8; ++Row)
        {
            const uint8 DisabledRow = R.ReadU8();
            for (int32 Col = 0; Col < 8; ++Col)
            {
                Chunk.GroundEffectDisabled[Row * 8 + Col] = (DisabledRow & (1u << Col)) != 0;
            }
        }

        uint32 OfsSndEmitters = R.ReadU32();
        uint32 NSndEmitters   = R.ReadU32();
        uint32 OfsLiquid      = R.ReadU32();
        uint32 SizeLiquid     = R.ReadU32();

        float ZPos = R.ReadF32(); // height base
        float XPos = R.ReadF32(); // north position
        float YPos = R.ReadF32(); // west position

        uint32 OfsMCCV = R.ReadU32();
        R.Skip(4); // unused1
        R.Skip(4); // unused2

        // ── Fill output chunk ───────────────────────────────────────────────
        Chunk.IndexX = static_cast<int32>(Ix);
        Chunk.IndexY = static_cast<int32>(Iy);
        Chunk.AreaId = AreaId;
        Chunk.Holes  = HolesLow;
        // Position: WoW ADT calculates from tile/chunk indices, NOT from header floats.
        // header.ypos (stored 3rd) is the only value used directly (height base).
        // header.zpos and header.xpos are derived values = ZEROPOINT - actual_pos.
        // We store the height and let the mesh builder compute XY from indices.
        Chunk.WorldX = 0.0f;   // Will be computed from tile/chunk indices in mesh builder
        Chunk.WorldY = 0.0f;   // Will be computed from tile/chunk indices in mesh builder
        Chunk.WorldZ = YPos;   // Height base (ypos from header, stored as 3rd float)

        const bool bDoNotFixAlpha = (Flags & MCNK_FLAG_DO_NOT_FIX_ALPHA) != 0;

        // ── MCVT (heights) ──────────────────────────────────────────────────
        if (OfsHeight > 0 && McnkDataStart + OfsHeight + 8 + 145 * 4 <= static_cast<int64>(R.Size))
        {
            R.Seek(McnkDataStart + OfsHeight);
            uint32 SubMagic, SubSize;
            ReadChunkHeader(R, SubMagic, SubSize);
            if (SubMagic == MAGIC_MCVT && SubSize >= 145 * 4)
            {
                ParseMCVT(R, Chunk);
            }
            else
            {
                UE_LOG(LogAdt, Warning, TEXT("MCVT magic mismatch or bad size for chunk %d,%d (magic=0x%08X, size=%u)"),
                    Ix, Iy, SubMagic, SubSize);
            }
        }

        // ── MCNR (normals) ─────────────────────────────────────────────────
        if (OfsNormal > 0 && McnkDataStart + OfsNormal + 8 < static_cast<int64>(R.Size))
        {
            R.Seek(McnkDataStart + OfsNormal);
            uint32 SubMagic, SubSize;
            ReadChunkHeader(R, SubMagic, SubSize);
            ParseMCNR(R, Chunk);
        }

        // ── MCLY (texture layers) ──────────────────────────────────────────
        TArray<FMclyEntry> Layers;
        if (OfsLayer > 0 && McnkDataStart + OfsLayer + 8 < static_cast<int64>(R.Size))
        {
            R.Seek(McnkDataStart + OfsLayer);
            uint32 SubMagic, SubSize;
            ReadChunkHeader(R, SubMagic, SubSize);
            Layers = ParseMCLY(R, SubSize);

            Chunk.TextureIndices.Reserve(Layers.Num());
            Chunk.GroundEffectIds.Reserve(Layers.Num());
            for (const FMclyEntry& L : Layers)
            {
                Chunk.TextureIndices.Add(static_cast<int32>(L.TextureId));
                Chunk.GroundEffectIds.Add(L.EffectId);
            }
        }

        // ── MCAL (alpha maps) ──────────────────────────────────────────────
        if (OfsAlpha > 0 && Layers.Num() > 1 && McnkDataStart + OfsAlpha + 8 < static_cast<int64>(R.Size))
        {
            R.Seek(McnkDataStart + OfsAlpha);
            uint32 SubMagic, SubSize;
            ReadChunkHeader(R, SubMagic, SubSize);
            ParseMCAL(R, SubSize, Layers, bBigAlpha, bDoNotFixAlpha, Chunk);
        }

        // ── MCCV (vertex colors, optional) ─────────────────────────────────
        if ((Flags & MCNK_FLAG_HAS_MCCV) && OfsMCCV > 0 && McnkDataStart + OfsMCCV + 8 < static_cast<int64>(R.Size))
        {
            R.Seek(McnkDataStart + OfsMCCV);
            uint32 SubMagic, SubSize;
            ReadChunkHeader(R, SubMagic, SubSize);
            ParseMCCV(R, Chunk);
        }

        // Seek past this entire MCNK chunk
        R.Seek(McnkDataEnd);
    }

} // anonymous namespace

// ── Public API ───────────────────────────────────────────────────────────────

FAdtData FAdtParser::Parse(const TArray<uint8>& Data, bool bBigAlpha)
{
    FAdtData Result;
    Result.bBigAlpha = bBigAlpha;

    if (Data.Num() < 8)
    {
        UE_LOG(LogAdt, Error, TEXT("ADT data too small (%d bytes)"), Data.Num());
        return Result;
    }

    FBinaryReader R(Data);

    // ── MVER ─────────────────────────────────────────────────────────────
    {
        uint32 Magic, Size;
        if (!ReadChunkHeader(R, Magic, Size) || Magic != MAGIC_MVER)
        {
            UE_LOG(LogAdt, Error, TEXT("Missing MVER chunk"));
            return Result;
        }
        uint32 Version = R.ReadU32();
        if (Version != 18)
        {
            UE_LOG(LogAdt, Warning, TEXT("Unexpected ADT version %u (expected 18)"), Version);
        }
    }

    // ── MHDR ─────────────────────────────────────────────────────────────
    uint32 MhdrFlags = 0;
    int32 MhdrDataStart = 0; // offsets in MHDR are relative to MHDR data start
    uint32 OfsMcin = 0, OfsMtex = 0, OfsMmdx = 0, OfsMmid = 0;
    uint32 OfsMwmo = 0, OfsMwid = 0, OfsMddf = 0, OfsModf = 0;
    uint32 OfsMfbo = 0, OfsMh2o = 0, OfsMtxf = 0;
    {
        uint32 Magic, Size;
        if (!ReadChunkHeader(R, Magic, Size) || Magic != MAGIC_MHDR)
        {
            UE_LOG(LogAdt, Error, TEXT("Missing MHDR chunk"));
            return Result;
        }
        MhdrDataStart = R.Pos;
        MhdrFlags = R.ReadU32();
        OfsMcin   = R.ReadU32();
        OfsMtex   = R.ReadU32();
        OfsMmdx   = R.ReadU32();
        OfsMmid   = R.ReadU32();
        OfsMwmo   = R.ReadU32();
        OfsMwid   = R.ReadU32();
        OfsMddf   = R.ReadU32();
        OfsModf   = R.ReadU32();
        OfsMfbo   = R.ReadU32();
        OfsMh2o   = R.ReadU32();
        OfsMtxf   = R.ReadU32();
        // Skip remaining padding (header is 64 bytes total: 4 + 11*4 + 4*4 padding = 64)
        R.Seek(MhdrDataStart + static_cast<int32>(Size));
    }

    // All MHDR offsets are relative to MHDR data start
    auto AbsOfs = [MhdrDataStart](uint32 Rel) -> int32 { return MhdrDataStart + static_cast<int32>(Rel); };

    // ── Helper: remember the raw string block positions for MMDX/MWMO ───
    int32 MmdxBlockStart = 0;
    uint32 MmdxBlockSize = 0;
    int32 MwmoBlockStart = 0;
    uint32 MwmoBlockSize = 0;

    // ── MCIN (256 entries with offsets to MCNKs — we skip this since we
    //    parse MCNKs sequentially, but read past it) ─────────────────────
    // MCIN offsets are absolute file offsets, so we could use them, but
    // sequential parsing is simpler and equally correct.

    // ── MTEX ─────────────────────────────────────────────────────────────
    if (OfsMtex != 0)
    {
        R.Seek(AbsOfs(OfsMtex));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MTEX)
        {
            Result.TexturePaths = ParseStringBlock(R, Size);
        }
    }

    // ── MMDX ─────────────────────────────────────────────────────────────
    if (OfsMmdx != 0)
    {
        R.Seek(AbsOfs(OfsMmdx));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MMDX)
        {
            MmdxBlockStart = R.Pos;
            MmdxBlockSize  = Size;
            Result.DoodadPaths = ParseStringBlock(R, Size);
        }
    }

    // ── MMID (offsets into MMDX) — build name-index lookup ──────────────
    TArray<int32> MmdxIndexMap; // MMID entry index → DoodadPaths index
    if (OfsMmid != 0 && MmdxBlockSize > 0)
    {
        R.Seek(AbsOfs(OfsMmid));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MMID)
        {
            int32 Count = static_cast<int32>(Size) / 4;
            TArray<uint32> Offsets;
            Offsets.SetNum(Count);
            for (int32 i = 0; i < Count; ++i)
            {
                Offsets[i] = R.ReadU32();
            }
            MmdxIndexMap = BuildOffsetToIndexMap(Offsets, R.Base + MmdxBlockStart, MmdxBlockSize);
        }
    }

    // ── MWMO ─────────────────────────────────────────────────────────────
    if (OfsMwmo != 0)
    {
        R.Seek(AbsOfs(OfsMwmo));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MWMO)
        {
            MwmoBlockStart = R.Pos;
            MwmoBlockSize  = Size;
            Result.WmoPaths = ParseStringBlock(R, Size);
        }
    }

    // ── MWID (offsets into MWMO) — build name-index lookup ──────────────
    TArray<int32> MwmoIndexMap;
    if (OfsMwid != 0 && MwmoBlockSize > 0)
    {
        R.Seek(AbsOfs(OfsMwid));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MWID)
        {
            int32 Count = static_cast<int32>(Size) / 4;
            TArray<uint32> Offsets;
            Offsets.SetNum(Count);
            for (int32 i = 0; i < Count; ++i)
            {
                Offsets[i] = R.ReadU32();
            }
            MwmoIndexMap = BuildOffsetToIndexMap(Offsets, R.Base + MwmoBlockStart, MwmoBlockSize);
        }
    }

    // ── MDDF (doodad placements) ─────────────────────────────────────────
    if (OfsMddf != 0)
    {
        R.Seek(AbsOfs(OfsMddf));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MDDF)
        {
            int32 Count = static_cast<int32>(Size) / 36;
            Result.DoodadPlacements.Reserve(Count);
            for (int32 i = 0; i < Count; ++i)
            {
                FAdtDoodadPlacement P;
                uint32 RawNameIdx = R.ReadU32();
                P.UniqueId  = R.ReadU32();
                float Px    = R.ReadF32();
                float Py    = R.ReadF32();
                float Pz    = R.ReadF32();
                float Rx    = R.ReadF32();
                float Ry    = R.ReadF32();
                float Rz    = R.ReadF32();
                P.Scale     = R.ReadU16();
                P.Flags     = R.ReadU16();

                // Resolve MMID name index → DoodadPaths index
                P.NameIndex = (MmdxIndexMap.IsValidIndex(RawNameIdx)) ? MmdxIndexMap[RawNameIdx] : static_cast<int32>(RawNameIdx);

                P.Position = FVector(Px, Py, Pz);
                P.Rotation = FVector(Rx, Ry, Rz);

                Result.DoodadPlacements.Add(P);
            }
        }
    }

    // ── MODF (WMO placements) ────────────────────────────────────────────
    if (OfsModf != 0)
    {
        R.Seek(AbsOfs(OfsModf));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MODF)
        {
            int32 Count = static_cast<int32>(Size) / 64;
            Result.WmoPlacements.Reserve(Count);
            for (int32 i = 0; i < Count; ++i)
            {
                FAdtWmoPlacement P;
                uint32 RawNameIdx = R.ReadU32();
                P.UniqueId  = R.ReadU32();

                float Px = R.ReadF32();
                float Py = R.ReadF32();
                float Pz = R.ReadF32();
                float Rx = R.ReadF32();
                float Ry = R.ReadF32();
                float Rz = R.ReadF32();

                // Extents: 2 × 3 floats (min, max)
                float Ex0 = R.ReadF32();
                float Ey0 = R.ReadF32();
                float Ez0 = R.ReadF32();
                float Ex1 = R.ReadF32();
                float Ey1 = R.ReadF32();
                float Ez1 = R.ReadF32();

                P.Flags      = R.ReadU16();
                P.DoodadSet  = R.ReadU16();
                P.NameSet    = R.ReadU16();
                P.Scale      = R.ReadU16();

                P.NameIndex = (MwmoIndexMap.IsValidIndex(RawNameIdx)) ? MwmoIndexMap[RawNameIdx] : static_cast<int32>(RawNameIdx);

                P.Position    = FVector(Px, Py, Pz);
                P.Rotation    = FVector(Rx, Ry, Rz);
                P.BoundingBox = FBox(FVector(Ex0, Ey0, Ez0), FVector(Ex1, Ey1, Ez1));

                Result.WmoPlacements.Add(P);
            }
        }
    }

    // ── MH2O (water data) ────────────────────────────────────────────────
    if (OfsMh2o != 0)
    {
        R.Seek(AbsOfs(OfsMh2o));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MH2O && Size >= 256 * 12 && static_cast<int64>(R.Pos) + Size <= R.Size)
        {
            int32 Mh2oStart = R.Pos; // start of MH2O data (offsets are relative to this)

            // 256 chunk headers (12 bytes each)
            struct FMH2OHeader { uint32 OfsInstances; uint32 LayerCount; uint32 OfsAttributes; };
            FMH2OHeader Headers[256];
            for (int32 i = 0; i < 256; i++)
            {
                Headers[i].OfsInstances  = R.ReadU32();
                Headers[i].LayerCount    = R.ReadU32();
                Headers[i].OfsAttributes = R.ReadU32();
            }

            int32 WaterChunkCount = 0;
            for (int32 i = 0; i < 256; i++)
            {
                if (Headers[i].LayerCount == 0 || Headers[i].OfsInstances == 0 ||
                    Headers[i].OfsInstances > Size || Size - Headers[i].OfsInstances < 24) continue;

                R.Seek(Mh2oStart + Headers[i].OfsInstances);

                for (uint32 Layer = 0; Layer < Headers[i].LayerCount; Layer++)
                {
                    if (R.Pos < Mh2oStart || static_cast<int64>(R.Pos) + 24 > static_cast<int64>(Mh2oStart) + Size) break;

                    FMH2OInstance Inst;
                    Inst.LiquidType    = R.ReadU16();
                    Inst.VertexFormat  = R.ReadU16();
                    Inst.MinHeight     = R.ReadF32();
                    Inst.MaxHeight     = R.ReadF32();
                    Inst.XOffset       = R.ReadU8();
                    Inst.YOffset       = R.ReadU8();
                    Inst.Width         = R.ReadU8();
                    Inst.Height        = R.ReadU8();
                    uint32 OfsMask     = R.ReadU32();
                    uint32 OfsHeightmap = R.ReadU32();

                    int32 NextLayerPos = R.Pos; // save position for next layer

                    const bool bValidLayerRect =
                        Inst.Width > 0 && Inst.Height > 0 &&
                        Inst.XOffset < 8 && Inst.YOffset < 8 &&
                        Inst.Width <= 8 && Inst.Height <= 8 &&
                        Inst.XOffset + Inst.Width <= 8 &&
                        Inst.YOffset + Inst.Height <= 8;
                    const bool bValidHeightRange =
                        FMath::IsFinite(Inst.MinHeight) &&
                        FMath::IsFinite(Inst.MaxHeight) &&
                        Inst.MinHeight > -2000.0f &&
                        Inst.MaxHeight < 2000.0f &&
                        Inst.MaxHeight >= Inst.MinHeight - 0.1f;

                    if (!bValidLayerRect || !bValidHeightRange)
                    {
                        R.Seek(NextLayerPos);
                        continue;
                    }

                    // Read the liquid existence bitmap. In WotLK MH2O this mask
                    // is local to the layer rectangle, not to the whole 8x8
                    // chunk. Reading/treating it as a full chunk mask shifts
                    // offset layers and creates broken, patchy rivers.
                    if (OfsMask != 0)
                    {
                        Inst.bHasExplicitExistsBitmap = true;
                        const uint32 RequiredMaskBytes = (Inst.Width * Inst.Height + 7) / 8;
                        if (OfsMask > Size || RequiredMaskBytes > Size - OfsMask)
                        {
                            R.Seek(NextLayerPos);
                            continue;
                        }
                        R.Seek(Mh2oStart + OfsMask);
                        Inst.ExistsBitmap = 0;
                        const int32 LocalMaskBytes = FMath::Clamp((Inst.Width * Inst.Height + 7) / 8, 0, 8);
                        const int32 MaskBytes = LocalMaskBytes;
                        Inst.bExistsBitmapIsFullChunk = false;
                        for (int32 ByteIndex = 0; ByteIndex < MaskBytes && R.CanRead(1); ByteIndex++)
                        {
                            Inst.ExistsBitmap |= static_cast<uint64>(R.ReadU8()) << (ByteIndex * 8);
                        }
                    }
                    else
                    {
                        // No mask = every sub-tile in the layer rectangle exists.
                        Inst.bHasExplicitExistsBitmap = false;
                        Inst.bExistsBitmapIsFullChunk = false;
                        Inst.ExistsBitmap = 0;
                        for (int32 Row = 0; Row < Inst.Height; ++Row)
                        {
                            for (int32 Col = 0; Col < Inst.Width; ++Col)
                            {
                                Inst.ExistsBitmap |= 1ULL << (Row * Inst.Width + Col);
                            }
                        }
                    }

                    // MH2O is planar: ALL heights, then ALL UV pairs (if any),
                    // then ALL depths. Interleaving these per vertex consumed depth
                    // bytes as floats, corrupting four out of five river heights.
                    const int32 VertCount = (Inst.Width + 1) * (Inst.Height + 1);
                    Inst.Heights.Init(Inst.MinHeight, VertCount);
                    Inst.Depths.Init(255, VertCount);
                    if (OfsHeightmap != 0)
                    {
                        const int32 Format = Inst.GetLiquidVertexFormat();
                        const bool HasHeights = Format != 2;
                        const bool HasUV = Format == 1 || Format == 3;
                        const bool HasDepth = Format != 1;
                        const uint32 RequiredBytes = VertCount * ((HasHeights ? 4 : 0) +
                            (HasUV ? 4 : 0) + (HasDepth ? 1 : 0));
                        if (OfsHeightmap > Size || RequiredBytes > Size - OfsHeightmap)
                        {
                            R.Seek(NextLayerPos);
                            continue;
                        }
                        R.Seek(Mh2oStart + OfsHeightmap);
                        if (HasHeights)
                            for (float& Height : Inst.Heights)
                            {
                                Height = R.ReadF32();
                                if (!FMath::IsFinite(Height) || Height < -2000.0f || Height > 2000.0f)
                                    Height = Inst.MinHeight;
                            }
                        if (HasUV) R.Skip(VertCount * 4);
                        if (HasDepth)
                            for (uint8& Depth : Inst.Depths) Depth = R.ReadU8();
                    }

                    Result.WaterChunks[i].Layers.Add(MoveTemp(Inst));
                    R.Seek(NextLayerPos); // restore for next layer
                }
                WaterChunkCount++;
            }
            UE_LOG(LogAdt, Log, TEXT("MH2O: %d chunks with water data"), WaterChunkCount);
        }
    }

    // ── MCIN — read MCNK offsets so we can seek directly ────────────────
    struct FMcinEntry { uint32 Offset; uint32 Size; };
    TArray<FMcinEntry> McinEntries;
    if (OfsMcin != 0)
    {
        R.Seek(AbsOfs(OfsMcin));
        uint32 Magic, Size;
        if (ReadChunkHeader(R, Magic, Size) && Magic == MAGIC_MCIN)
        {
            McinEntries.SetNum(256);
            for (int32 i = 0; i < 256; ++i)
            {
                McinEntries[i].Offset = R.ReadU32();
                McinEntries[i].Size   = R.ReadU32();
                R.Skip(8); // flags + asyncID
            }
        }
    }

    // ── 256 × MCNK chunks ───────────────────────────────────────────────
    int32 McnksParsed = 0;
    if (McinEntries.Num() == 256)
    {
        // Use MCIN offsets for direct seeking (offsets are absolute file positions)
        for (int32 i = 0; i < 256; ++i)
        {
            if (McinEntries[i].Offset == 0)
                continue;

            R.Seek(static_cast<int32>(McinEntries[i].Offset));
            uint32 Magic, Size;
            if (!ReadChunkHeader(R, Magic, Size) || Magic != MAGIC_MCNK)
            {
                UE_LOG(LogAdt, Warning, TEXT("MCNK #%d: bad magic at offset %u"), i, McinEntries[i].Offset);
                continue;
            }

            ParseMCNK(R, Size, bBigAlpha, Result.Chunks[i]);
            ++McnksParsed;
        }
    }
    else
    {
        // Fallback: scan the file for MCNK chunks sequentially
        // Start after the top-level chunks; find the first MCNK
        // We'll scan from the beginning looking for all MCNK chunks
        R.Seek(0);
        while (McnksParsed < 256 && R.CanRead(8))
        {
            int32 ChunkStart = R.Pos;
            uint32 Magic, Size;
            if (!ReadChunkHeader(R, Magic, Size))
                break;

            if (Magic == MAGIC_MCNK)
            {
                ParseMCNK(R, Size, bBigAlpha, Result.Chunks[McnksParsed]);
                ++McnksParsed;
            }
            else
            {
                R.Skip(static_cast<int32>(Size));
            }
        }
    }

    Result.bIsValid = (McnksParsed == 256);

    if (McnksParsed != 256)
    {
        UE_LOG(LogAdt, Warning, TEXT("Only parsed %d/256 MCNK chunks"), McnksParsed);
    }
    else
    {
        UE_LOG(LogAdt, Log, TEXT("ADT parsed successfully: %d textures, %d doodads, %d WMOs, 256 chunks"),
            Result.TexturePaths.Num(), Result.DoodadPlacements.Num(), Result.WmoPlacements.Num());
    }

    return Result;
}
