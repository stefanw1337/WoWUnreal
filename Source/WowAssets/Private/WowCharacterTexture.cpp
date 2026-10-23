#include "WowCharacterTexture.h"
#include "WowPerformanceScope.h"
#include "WowCharacterBuilder.h"
#include "WowTextureFactory.h"
#include "WowAssetCache.h"
#include "Mpq/MpqManager.h"
#include "Formats/BlpParser.h"
#include "Formats/BlpTypes.h"
#include "Formats/Dbc/DbcStore.h"
#include "Engine/Texture2D.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/PlatformProcess.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowCharTex, Log, All);

FString FWowCharacterTexture::GetSectionTexture(uint32 RaceId, uint32 Gender, ESectionType Type,
    int32 Variation, int32 Color)
{
    const FCharSectionsDbc& CharSections = FDbcStore::Get().CharSections();

    FString BestMatch;
    int32 BestScore = -1;
    for (const FCharSectionsDbcEntry& Entry : CharSections.GetAll())
    {
        if (Entry.RaceID != RaceId ||
            Entry.SexID != Gender ||
            Entry.Type != static_cast<uint32>(Type))
        {
            continue;
        }

        int32 Score = 0;
        if (Variation != INDEX_NONE)
        {
            if (Entry.Variation != static_cast<uint32>(Variation))
            {
                continue;
            }
            Score += 2;
        }

        if (Color != INDEX_NONE)
        {
            if (Entry.Color != static_cast<uint32>(Color))
            {
                continue;
            }
            Score += 4;
        }

        // When variation is unspecified, prefer variation 0 (the default)
        if (Variation == INDEX_NONE && Entry.Variation == 0)
        {
            Score += 1;
        }

        for (int32 i = 0; i < 3; ++i)
        {
            if (!Entry.Textures[i].IsEmpty() && Score > BestScore)
            {
                BestMatch = Entry.Textures[i];
                BestScore = Score;
                break;
            }
        }
    }

    return BestMatch;
}

/** Decode a DXT1 4x4 block into 16 RGBA pixels */
static void DecodeDXT1Block(const uint8* Block, uint8 OutPixels[64])
{
    uint16 C0 = Block[0] | (Block[1] << 8);
    uint16 C1 = Block[2] | (Block[3] << 8);

    uint8 Colors[4][4]; // [index][RGBA]
    auto Decode565 = [](uint16 C, uint8* Out) {
        Out[0] = ((C >> 11) & 0x1F) * 255 / 31;
        Out[1] = ((C >> 5) & 0x3F) * 255 / 63;
        Out[2] = (C & 0x1F) * 255 / 31;
        Out[3] = 255;
    };

    Decode565(C0, Colors[0]);
    Decode565(C1, Colors[1]);

    if (C0 > C1)
    {
        for (int i = 0; i < 3; i++)
        {
            Colors[2][i] = (2 * Colors[0][i] + Colors[1][i] + 1) / 3;
            Colors[3][i] = (Colors[0][i] + 2 * Colors[1][i] + 1) / 3;
        }
        Colors[2][3] = 255;
        Colors[3][3] = 255;
    }
    else
    {
        for (int i = 0; i < 3; i++)
        {
            Colors[2][i] = (Colors[0][i] + Colors[1][i]) / 2;
            Colors[3][i] = 0;
        }
        Colors[2][3] = 255;
        Colors[3][3] = 0; // Transparent
    }

    uint32 Bits = Block[4] | (Block[5] << 8) | (Block[6] << 16) | (Block[7] << 24);
    for (int p = 0; p < 16; p++)
    {
        uint32 Idx = (Bits >> (p * 2)) & 0x3;
        FMemory::Memcpy(&OutPixels[p * 4], Colors[Idx], 4);
    }
}

/** Decompress a DXT BLP to RGBA8 pixels. Returns empty array for unsupported formats. */
static TArray<uint8> DecompressToRGBA(const FBlpTexture& Blp)
{
    TArray<uint8> Pixels;
    if (!Blp.bIsValid || Blp.MipLevels.Num() == 0) return Pixels;

    uint32 W = Blp.Width;
    uint32 H = Blp.Height;

    if (Blp.PixelFormat == EBlpPixelFormat::RGBA8)
    {
        // Already RGBA — just copy
        Pixels = Blp.MipLevels[0].Data;
        return Pixels;
    }

    if (Blp.PixelFormat == EBlpPixelFormat::DXT1)
    {
        Pixels.SetNumZeroed(W * H * 4);
        const uint8* Src = Blp.MipLevels[0].Data.GetData();
        uint32 BlocksX = FMath::Max(1u, W / 4);
        uint32 BlocksY = FMath::Max(1u, H / 4);

        for (uint32 by = 0; by < BlocksY; by++)
        {
            for (uint32 bx = 0; bx < BlocksX; bx++)
            {
                uint8 BlockPixels[64];
                DecodeDXT1Block(Src, BlockPixels);
                Src += 8;

                for (uint32 py = 0; py < 4 && (by * 4 + py) < H; py++)
                {
                    for (uint32 px = 0; px < 4 && (bx * 4 + px) < W; px++)
                    {
                        uint32 DstIdx = ((by * 4 + py) * W + (bx * 4 + px)) * 4;
                        FMemory::Memcpy(&Pixels[DstIdx], &BlockPixels[(py * 4 + px) * 4], 4);
                    }
                }
            }
        }
        return Pixels;
    }

    if (Blp.PixelFormat == EBlpPixelFormat::DXT3)
    {
        Pixels.SetNumZeroed(W * H * 4);
        const uint8* Src = Blp.MipLevels[0].Data.GetData();
        uint32 BlocksX = FMath::Max(1u, W / 4);
        uint32 BlocksY = FMath::Max(1u, H / 4);

        for (uint32 by = 0; by < BlocksY; by++)
        {
            for (uint32 bx = 0; bx < BlocksX; bx++)
            {
                // DXT3: 8 bytes explicit alpha + 8 bytes DXT1 color block
                const uint8* AlphaBlock = Src;
                const uint8* ColorBlock = Src + 8;

                uint8 BlockPixels[64];
                DecodeDXT1Block(ColorBlock, BlockPixels);

                // Apply explicit 4-bit alpha per pixel
                for (uint32 py = 0; py < 4; py++)
                {
                    uint16 AlphaRow = AlphaBlock[py * 2] | (AlphaBlock[py * 2 + 1] << 8);
                    for (uint32 px = 0; px < 4; px++)
                    {
                        uint8 Alpha4 = (AlphaRow >> (px * 4)) & 0xF;
                        BlockPixels[(py * 4 + px) * 4 + 3] = Alpha4 * 17; // 0-15 → 0-255
                    }
                }

                Src += 16;

                for (uint32 py = 0; py < 4 && (by * 4 + py) < H; py++)
                {
                    for (uint32 px = 0; px < 4 && (bx * 4 + px) < W; px++)
                    {
                        uint32 DstIdx = ((by * 4 + py) * W + (bx * 4 + px)) * 4;
                        FMemory::Memcpy(&Pixels[DstIdx], &BlockPixels[(py * 4 + px) * 4], 4);
                    }
                }
            }
        }
        return Pixels;
    }

    if (Blp.PixelFormat == EBlpPixelFormat::DXT5)
    {
        Pixels.SetNumZeroed(W * H * 4);
        const uint8* Src = Blp.MipLevels[0].Data.GetData();
        uint32 BlocksX = FMath::Max(1u, W / 4);
        uint32 BlocksY = FMath::Max(1u, H / 4);

        for (uint32 by = 0; by < BlocksY; by++)
        {
            for (uint32 bx = 0; bx < BlocksX; bx++)
            {
                // DXT5: 8 bytes interpolated alpha + 8 bytes DXT1 color block
                uint8 Alpha0 = Src[0];
                uint8 Alpha1 = Src[1];
                uint8 AlphaLookup[8];
                AlphaLookup[0] = Alpha0;
                AlphaLookup[1] = Alpha1;
                if (Alpha0 > Alpha1)
                {
                    for (int i = 0; i < 6; i++)
                        AlphaLookup[2 + i] = (uint8)(((6 - i) * Alpha0 + (1 + i) * Alpha1 + 3) / 7);
                }
                else
                {
                    for (int i = 0; i < 4; i++)
                        AlphaLookup[2 + i] = (uint8)(((4 - i) * Alpha0 + (1 + i) * Alpha1 + 2) / 5);
                    AlphaLookup[6] = 0;
                    AlphaLookup[7] = 255;
                }

                // 6 bytes of 3-bit alpha indices
                uint64 AlphaBits = 0;
                for (int b = 0; b < 6; b++)
                    AlphaBits |= (uint64)Src[2 + b] << (b * 8);

                const uint8* ColorBlock = Src + 8;
                uint8 BlockPixels[64];
                DecodeDXT1Block(ColorBlock, BlockPixels);

                for (int p = 0; p < 16; p++)
                {
                    uint8 AlphaIdx = (AlphaBits >> (p * 3)) & 0x7;
                    BlockPixels[p * 4 + 3] = AlphaLookup[AlphaIdx];
                }

                Src += 16;

                for (uint32 py = 0; py < 4 && (by * 4 + py) < H; py++)
                {
                    for (uint32 px = 0; px < 4 && (bx * 4 + px) < W; px++)
                    {
                        uint32 DstIdx = ((by * 4 + py) * W + (bx * 4 + px)) * 4;
                        FMemory::Memcpy(&Pixels[DstIdx], &BlockPixels[(py * 4 + px) * 4], 4);
                    }
                }
            }
        }
        return Pixels;
    }

    // Unsupported format
    return Pixels;
}

/** Alpha-blend overlay pixels onto base pixels. Both arrays must be W*H*4 RGBA. */
static void AlphaBlendLayer(TArray<uint8>& Base, const TArray<uint8>& Overlay, uint32 W, uint32 H)
{
    uint32 Count = FMath::Min((uint32)Base.Num(), (uint32)Overlay.Num()) / 4;
    for (uint32 i = 0; i < Count; i++)
    {
        uint32 Off = i * 4;
        uint8 Alpha = Overlay[Off + 3];
        if (Alpha == 0) continue;
        if (Alpha == 255)
        {
            Base[Off + 0] = Overlay[Off + 0];
            Base[Off + 1] = Overlay[Off + 1];
            Base[Off + 2] = Overlay[Off + 2];
            Base[Off + 3] = 255;
        }
        else
        {
            float A = Alpha / 255.0f;
            float InvA = 1.0f - A;
            Base[Off + 0] = FMath::Clamp<int32>(FMath::RoundToInt32(Overlay[Off + 0] * A + Base[Off + 0] * InvA), 0, 255);
            Base[Off + 1] = FMath::Clamp<int32>(FMath::RoundToInt32(Overlay[Off + 1] * A + Base[Off + 1] * InvA), 0, 255);
            Base[Off + 2] = FMath::Clamp<int32>(FMath::RoundToInt32(Overlay[Off + 2] * A + Base[Off + 2] * InvA), 0, 255);
            Base[Off + 3] = 255;
        }
    }
}

/** Load a BLP from MPQ and decompress to RGBA8. Returns empty on failure. */
static TArray<uint8> LoadBlpAsRGBA(FMpqManager* Mpq, const FString& Path, uint32& OutW, uint32& OutH)
{
    OutW = OutH = 0;
    TArray<uint8> BlpRaw;
    if (!Mpq->ReadFile(Path, BlpRaw)) return TArray<uint8>();

    FBlpTexture Blp = FBlpParser::Parse(BlpRaw);
    if (!Blp.bIsValid) return TArray<uint8>();

    OutW = Blp.Width;
    OutH = Blp.Height;
    return DecompressToRGBA(Blp);
}

/** Scale RGBA pixels to target dimensions using nearest-neighbor sampling */
static TArray<uint8> ScalePixels(const TArray<uint8>& Src, uint32 SrcW, uint32 SrcH, uint32 DstW, uint32 DstH)
{
    if (SrcW == DstW && SrcH == DstH) return Src;

    TArray<uint8> Dst;
    Dst.SetNumZeroed(DstW * DstH * 4);
    for (uint32 y = 0; y < DstH; y++)
    {
        uint32 sy = y * SrcH / DstH;
        for (uint32 x = 0; x < DstW; x++)
        {
            uint32 sx = x * SrcW / DstW;
            FMemory::Memcpy(&Dst[(y * DstW + x) * 4], &Src[(sy * SrcW + sx) * 4], 4);
        }
    }
    return Dst;
}

/** Paste region-sized texture onto composite at target coordinates with alpha blending */
static void PasteRegionWithAlpha(TArray<uint8>& Composite, const TArray<uint8>& Region,
    uint32 CompositeW, uint32 CompositeH, uint32 RegionW, uint32 RegionH,
    uint32 TargetX, uint32 TargetY, uint32 TargetW, uint32 TargetH)
{
    if (Region.Num() == 0) return;

    // Scale region to target dimensions if necessary
    TArray<uint8> ScaledRegion = (RegionW == TargetW && RegionH == TargetH)
        ? Region
        : ScalePixels(Region, RegionW, RegionH, TargetW, TargetH);

    // Alpha blend onto composite at target position
    for (uint32 y = 0; y < TargetH && (TargetY + y) < CompositeH; y++)
    {
        for (uint32 x = 0; x < TargetW && (TargetX + x) < CompositeW; x++)
        {
            uint32 SrcIdx = (y * TargetW + x) * 4;
            uint32 DstIdx = ((TargetY + y) * CompositeW + (TargetX + x)) * 4;

            if (SrcIdx + 3 >= static_cast<uint32>(ScaledRegion.Num()) ||
                DstIdx + 3 >= static_cast<uint32>(Composite.Num())) continue;

            uint8 Alpha = ScaledRegion[SrcIdx + 3];
            if (Alpha == 0) continue;

            if (Alpha == 255)
            {
                Composite[DstIdx + 0] = ScaledRegion[SrcIdx + 0];
                Composite[DstIdx + 1] = ScaledRegion[SrcIdx + 1];
                Composite[DstIdx + 2] = ScaledRegion[SrcIdx + 2];
                Composite[DstIdx + 3] = 255;
            }
            else
            {
                float A = Alpha / 255.0f;
                float InvA = 1.0f - A;
                Composite[DstIdx + 0] = FMath::Clamp<int32>(FMath::RoundToInt32(ScaledRegion[SrcIdx + 0] * A + Composite[DstIdx + 0] * InvA), 0, 255);
                Composite[DstIdx + 1] = FMath::Clamp<int32>(FMath::RoundToInt32(ScaledRegion[SrcIdx + 1] * A + Composite[DstIdx + 1] * InvA), 0, 255);
                Composite[DstIdx + 2] = FMath::Clamp<int32>(FMath::RoundToInt32(ScaledRegion[SrcIdx + 2] * A + Composite[DstIdx + 2] * InvA), 0, 255);
                Composite[DstIdx + 3] = 255;
            }
        }
    }
}

/** Get fallback region coordinates for layout 1 if DBC doesn't load */
struct FRegionCoords
{
    uint32 X, Y, W, H;
};

/** Get fallback region coordinates for WoW 3.3.5 (WotLK).
 *  From WMVx: canvas is 512x512, coordinates at 2x scale of base 256x256 layout. */
static TMap<uint32, FRegionCoords> GetFallbackRegions()
{
    TMap<uint32, FRegionCoords> Regions;
    // Direct from WMVx LegacyCharacterComponentTextureAdaptor (512x512 canvas)
    Regions.Add(0,  {0,   0,   256, 128});  // ARM_UPPER
    Regions.Add(1,  {0,   128, 256, 128});  // ARM_LOWER
    Regions.Add(2,  {0,   256, 256, 64});   // HAND
    Regions.Add(9,  {0,   320, 256, 64});   // FACE_UPPER
    Regions.Add(10, {0,   384, 256, 128});  // FACE_LOWER
    Regions.Add(3,  {256, 0,   256, 128});  // TORSO_UPPER
    Regions.Add(4,  {256, 128, 256, 64});   // TORSO_LOWER
    Regions.Add(5,  {256, 192, 256, 128});  // LEG_UPPER
    Regions.Add(6,  {256, 320, 256, 128});  // LEG_LOWER
    Regions.Add(7,  {256, 448, 256, 64});   // FOOT
    return Regions;
}

bool FWowCharacterTexture::BuildCompositePixels(FMpqManager* Mpq, const FCustomization& Customization,
    TArray<uint8>& OutPixels, uint32& OutWidth, uint32& OutHeight)
{
    FWowPerformanceScope Timing(TEXT("character-atlas-pixels"), FString::Printf(TEXT("race=%u sex=%u"),Customization.RaceId,Customization.Gender));
    if (!Mpq) return false;
    // 1. Get layout dimensions from DBC or use fallback
    uint32 LayoutId = 1; // Default layout for all character textures in 3.3.5
    uint32 CompositeW = 512, CompositeH = 512; // WMVx: 256*2 scale factor for WotLK

    const FDbcStore& DbcStore = FDbcStore::Get();
    const FCharComponentTextureLayoutsDbcEntry* Layout = DbcStore.CharComponentTextureLayouts().GetById(LayoutId);
    if (Layout)
    {
        CompositeW = Layout->Width;
        CompositeH = Layout->Height;
        UE_LOG(LogWowCharTex, Log, TEXT("Using DBC layout %d: %dx%d"), LayoutId, CompositeW, CompositeH);
    }
    else
    {
        UE_LOG(LogWowCharTex, Log, TEXT("Using fallback layout: %dx%d"), CompositeW, CompositeH);
    }

    // 2. Load region coordinates from DBC or use hardcoded fallbacks
    TMap<uint32, FRegionCoords> RegionCoords;
    TArray<const FCharComponentTextureSectionsDbcEntry*> Sections = DbcStore.CharComponentTextureSections().GetByLayoutId(LayoutId);
    if (Sections.Num() > 0)
    {
        for (const FCharComponentTextureSectionsDbcEntry* Section : Sections)
        {
            RegionCoords.Add(Section->SectionType, {Section->X, Section->Y, Section->Width, Section->Height});
        }
        UE_LOG(LogWowCharTex, Log, TEXT("Loaded %d region coordinates from DBC"), Sections.Num());
    }
    else
    {
        RegionCoords = GetFallbackRegions();
        UE_LOG(LogWowCharTex, Log, TEXT("Using fallback region coordinates"));
    }

    // 3. Create composite buffer filled with transparent black
    TArray<uint8> Composite;
    Composite.SetNumZeroed(CompositeW * CompositeH * 4);

    // 4. Load and place skin texture (fills entire atlas as base layer)
    FString SkinPath = GetSectionTexture(Customization.RaceId, Customization.Gender,
        ESectionType::Skin, INDEX_NONE, static_cast<int32>(Customization.SkinColor));
    if (SkinPath.IsEmpty())
    {
        UE_LOG(LogWowCharTex, Warning, TEXT("No skin texture for Race=%d Gender=%d SkinColor=%d"),
            Customization.RaceId, Customization.Gender, Customization.SkinColor);
        return false;
    }

    uint32 SkinW, SkinH;
    TArray<uint8> SkinPixels = LoadBlpAsRGBA(Mpq, SkinPath, SkinW, SkinH);
    if (SkinPixels.Num() == 0)
    {
        UE_LOG(LogWowCharTex, Warning, TEXT("Failed to load/decompress skin: %s"), *SkinPath);
        return false; // BuildCompositeTexture falls back to the raw skin BLP
    }

    // Skin texture fills the entire atlas (it IS full-body)
    if (SkinW != CompositeW || SkinH != CompositeH)
    {
        SkinPixels = ScalePixels(SkinPixels, SkinW, SkinH, CompositeW, CompositeH);
    }
    FMemory::Memcpy(Composite.GetData(), SkinPixels.GetData(), FMath::Min(Composite.Num(), SkinPixels.Num()));
    UE_LOG(LogWowCharTex, Log, TEXT("Placed skin as base layer: %s (%dx%d)"), *SkinPath, SkinW, SkinH);

    int32 LayersComposited = 1; // Skin counts as layer 1

    // 5. Face textures: Textures[0] → FACE_LOWER, Textures[1] → FACE_UPPER (from WMVx)
    for (const FCharSectionsDbcEntry& Entry : FDbcStore::Get().CharSections().GetAll())
    {
        if (Entry.RaceID != Customization.RaceId || Entry.SexID != Customization.Gender) continue;
        if (Entry.Type != static_cast<uint32>(ESectionType::Face)) continue;
        if (Entry.Variation != Customization.FaceVariation || Entry.Color != Customization.SkinColor) continue;

        for (int32 TexIdx = 0; TexIdx < 2; ++TexIdx)
        {
            if (Entry.Textures[TexIdx].IsEmpty()) continue;
            uint32 RegionId = (TexIdx == 0) ? 10 : 9; // [0]→FACE_LOWER, [1]→FACE_UPPER
            const FRegionCoords* Region = RegionCoords.Find(RegionId);
            if (!Region) continue;

            uint32 TexW, TexH;
            TArray<uint8> Pixels = LoadBlpAsRGBA(Mpq, Entry.Textures[TexIdx], TexW, TexH);
            if (Pixels.Num() > 0)
            {
                PasteRegionWithAlpha(Composite, Pixels, CompositeW, CompositeH,
                    TexW, TexH, Region->X, Region->Y, Region->W, Region->H);
                LayersComposited++;
                UE_LOG(LogWowCharTex, Log, TEXT("Placed face[%d] at region %d: %s (%dx%d)"),
                    TexIdx, RegionId, *Entry.Textures[TexIdx], TexW, TexH);
            }
        }
        break;
    }

    // 6. Hair color textures: [0]→replaceable hair tex, [1]→FACE_LOWER, [2]→FACE_UPPER (from WMVx)
    for (const FCharSectionsDbcEntry& Entry : FDbcStore::Get().CharSections().GetAll())
    {
        if (Entry.RaceID != Customization.RaceId || Entry.SexID != Customization.Gender) continue;
        if (Entry.Type != static_cast<uint32>(ESectionType::Hair)) continue;
        if (Entry.Variation != Customization.HairStyle || Entry.Color != Customization.HairColor) continue;

        // Textures[1] → FACE_LOWER (layer 3), Textures[2] → FACE_UPPER (layer 3)
        for (int32 TexIdx = 1; TexIdx < 3; ++TexIdx)
        {
            if (Entry.Textures[TexIdx].IsEmpty()) continue;
            uint32 RegionId = (TexIdx == 1) ? 10 : 9; // [1]→FACE_LOWER, [2]→FACE_UPPER
            const FRegionCoords* Region = RegionCoords.Find(RegionId);
            if (!Region) continue;

            uint32 TexW, TexH;
            TArray<uint8> Pixels = LoadBlpAsRGBA(Mpq, Entry.Textures[TexIdx], TexW, TexH);
            if (Pixels.Num() > 0)
            {
                PasteRegionWithAlpha(Composite, Pixels, CompositeW, CompositeH,
                    TexW, TexH, Region->X, Region->Y, Region->W, Region->H);
                LayersComposited++;
                UE_LOG(LogWowCharTex, Log, TEXT("Placed hair[%d] at region %d: %s (%dx%d)"),
                    TexIdx, RegionId, *Entry.Textures[TexIdx], TexW, TexH);
            }
        }
        break;
    }

    // 7. Facial hair textures: [0]→FACE_LOWER, [1]→FACE_UPPER (from WMVx)
    for (const FCharSectionsDbcEntry& Entry : FDbcStore::Get().CharSections().GetAll())
    {
        if (Entry.RaceID != Customization.RaceId || Entry.SexID != Customization.Gender) continue;
        if (Entry.Type != static_cast<uint32>(ESectionType::FacialHair)) continue;
        if (Entry.Variation != Customization.FacialHairStyle || Entry.Color != Customization.HairColor) continue;

        for (int32 TexIdx = 0; TexIdx < 2; ++TexIdx)
        {
            if (Entry.Textures[TexIdx].IsEmpty()) continue;
            uint32 RegionId = (TexIdx == 0) ? 10 : 9; // [0]→FACE_LOWER, [1]→FACE_UPPER
            const FRegionCoords* Region = RegionCoords.Find(RegionId);
            if (!Region) continue;

            uint32 TexW, TexH;
            TArray<uint8> Pixels = LoadBlpAsRGBA(Mpq, Entry.Textures[TexIdx], TexW, TexH);
            if (Pixels.Num() > 0)
            {
                PasteRegionWithAlpha(Composite, Pixels, CompositeW, CompositeH,
                    TexW, TexH, Region->X, Region->Y, Region->W, Region->H);
                LayersComposited++;
                UE_LOG(LogWowCharTex, Log, TEXT("Placed facial hair[%d] at region %d: %s"),
                    TexIdx, RegionId, *Entry.Textures[TexIdx]);
            }
        }
        break;
    }

    // 8. Load and place underwear textures at TORSO_UPPER and LEG_UPPER regions
    // Need to find CharSections entry to get both Textures[0] and Textures[1]
    const FCharSectionsDbc& CharSections = FDbcStore::Get().CharSections();
    for (const FCharSectionsDbcEntry& Entry : CharSections.GetAll())
    {
        if (Entry.RaceID == Customization.RaceId &&
            Entry.SexID == Customization.Gender &&
            Entry.Type == static_cast<uint32>(ESectionType::Underwear) &&
            Entry.Color == Customization.SkinColor)
        {
            // In 3.3.5: Textures[0] is pelvis/shorts → LEG_UPPER, Textures[1] is chest/bra → TORSO_UPPER
            if (!Entry.Textures[0].IsEmpty())
            {
                uint32 UnderwearW, UnderwearH;
                TArray<uint8> UnderwearPixels = LoadBlpAsRGBA(Mpq, Entry.Textures[0], UnderwearW, UnderwearH);
                if (UnderwearPixels.Num() > 0)
                {
                    const FRegionCoords* Region = RegionCoords.Find(5); // LEG_UPPER (pelvis/shorts)
                    if (Region)
                    {
                        PasteRegionWithAlpha(Composite, UnderwearPixels, CompositeW, CompositeH,
                            UnderwearW, UnderwearH, Region->X, Region->Y, Region->W, Region->H);
                        LayersComposited++;
                        UE_LOG(LogWowCharTex, Log, TEXT("Placed underwear[0] at LEG_UPPER: %s (%dx%d)"), *Entry.Textures[0], UnderwearW, UnderwearH);
                    }
                }
            }

            if (!Entry.Textures[1].IsEmpty())
            {
                uint32 UnderwearW, UnderwearH;
                TArray<uint8> UnderwearPixels = LoadBlpAsRGBA(Mpq, Entry.Textures[1], UnderwearW, UnderwearH);
                if (UnderwearPixels.Num() > 0)
                {
                    const FRegionCoords* Region = RegionCoords.Find(3); // TORSO_UPPER (chest/bra)
                    if (Region)
                    {
                        PasteRegionWithAlpha(Composite, UnderwearPixels, CompositeW, CompositeH,
                            UnderwearW, UnderwearH, Region->X, Region->Y, Region->W, Region->H);
                        LayersComposited++;
                        UE_LOG(LogWowCharTex, Log, TEXT("Placed underwear[1] at TORSO_UPPER: %s (%dx%d)"), *Entry.Textures[1], UnderwearW, UnderwearH);
                    }
                }
            }
            break; // Only need first matching underwear entry
        }
    }

    UE_LOG(LogWowCharTex, Log, TEXT("Composited character atlas: %dx%d, %d layers"), CompositeW, CompositeH, LayersComposited);
    OutPixels = MoveTemp(Composite);
    OutWidth = CompositeW;
    OutHeight = CompositeH;
    return true;
}

FString FWowCharacterTexture::MakeCompositeCacheKey(const FCustomization& Customization)
{
    return FString::Printf(TEXT("CharTex_%d_%d_%d_%d_%d_%d_%d"),
        Customization.RaceId, Customization.Gender, Customization.SkinColor,
        Customization.FaceVariation, Customization.HairStyle, Customization.HairColor,
        Customization.FacialHairStyle);
}

namespace { FThreadSafeCounter GWowCharacterAtlasJobs; }
void FWowCharacterTexture::BeginAsyncWork() { GWowCharacterAtlasJobs.Increment(); }
void FWowCharacterTexture::EndAsyncWork() { GWowCharacterAtlasJobs.Decrement(); }
void FWowCharacterTexture::WaitForAsyncWork()
{
    const double Until = FPlatformTime::Seconds() + 10.0;
    while (GWowCharacterAtlasJobs.GetValue() > 0 && FPlatformTime::Seconds() < Until) FPlatformProcess::Sleep(0.002f);
}

UTexture2D* FWowCharacterTexture::BuildCompositeTexture(FMpqManager* Mpq, FWowAssetCache* Cache,
    const FCustomization& Customization)
{
    FWowPerformanceScope Timing(TEXT("character-atlas"), FString::Printf(TEXT("race=%u sex=%u"),Customization.RaceId,Customization.Gender));
    if (!Mpq) return nullptr;

    // Cache key includes all customization parameters
    FString CacheKey = FString::Printf(TEXT("CharTex_%d_%d_%d_%d_%d_%d_%d"),
        Customization.RaceId, Customization.Gender, Customization.SkinColor,
        Customization.FaceVariation, Customization.HairStyle, Customization.HairColor,
        Customization.FacialHairStyle);

    UTexture2D* CachedTex = Cache ? Cache->FindTexture(CacheKey) : nullptr;
    if (CachedTex) return CachedTex;

    TArray<uint8> Composite;
    uint32 CompositeW = 0, CompositeH = 0;
    if (!BuildCompositePixels(Mpq, Customization, Composite, CompositeW, CompositeH))
    {
        // The skin could not be decoded: hand back the raw skin BLP (old behaviour).
        const FString SkinPath = GetSectionTexture(Customization.RaceId, Customization.Gender,
            ESectionType::Skin, INDEX_NONE, static_cast<int32>(Customization.SkinColor));
        TArray<uint8> BlpRaw;
        if (!SkinPath.IsEmpty() && Mpq->ReadFile(SkinPath, BlpRaw))
        {
            FBlpTexture Blp = FBlpParser::Parse(BlpRaw);
            if (Blp.bIsValid)
            {
                UTexture2D* Tex = FWowTextureFactory::CreateTexture(Blp, CacheKey);
                if (Tex && Cache) Cache->CacheTexture(CacheKey, Tex);
                return Tex;
            }
        }
        return nullptr;
    }

    // 9. Create UTexture2D from composited RGBA pixels
    UTexture2D* Tex = UTexture2D::CreateTransient(CompositeW, CompositeH, PF_R8G8B8A8, *CacheKey);
    if (!Tex) return nullptr;

    Tex->Filter = TF_Bilinear;
    Tex->SRGB = true;
    Tex->NeverStream = true;

    void* TexData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    if (TexData)
    {
        FMemory::Memcpy(TexData, Composite.GetData(), Composite.Num());
        Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
    }
    Tex->UpdateResource();

    if (Cache) Cache->CacheTexture(CacheKey, Tex);

    UE_LOG(LogWowCharTex, Log, TEXT("Built composite character texture: %dx%d"), CompositeW, CompositeH);

    return Tex;
}

void FWowCharacterTexture::ApplyEquipmentOverlays(TArray<uint8>& Composite, uint32 CompositeW, uint32 CompositeH,
    const FWowCharacterBuilder::FBodyEquipment& Equipment, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    FWowPerformanceScope Timing(TEXT("equipment-atlas"), FString::Printf(TEXT("race=%u sex=%u"),Equipment.RaceId,Equipment.Gender));
    if (!Mpq || Composite.Num() == 0) return;

    const FDbcStore& Dbc = FDbcStore::Get();

    // Get region coordinates (use same logic as BuildCompositeTexture)
    TMap<uint32, FRegionCoords> RegionCoords;
    uint32 LayoutId = 1; // Default layout for all character textures in 3.3.5
    TArray<const FCharComponentTextureSectionsDbcEntry*> Sections = Dbc.CharComponentTextureSections().GetByLayoutId(LayoutId);
    if (Sections.Num() > 0)
    {
        for (const FCharComponentTextureSectionsDbcEntry* Section : Sections)
        {
            RegionCoords.Add(Section->SectionType, {Section->X, Section->Y, Section->Width, Section->Height});
        }
    }
    else
    {
        RegionCoords = GetFallbackRegions();
    }
    // HD bodies use the same layout at a larger scale (2048x1024 = 4x the 256-high
    // classic layout, with the face area added on the right): scale every region.
    {
        uint32 LayoutH = 0;
        for (const auto& Pair : RegionCoords) LayoutH = FMath::Max(LayoutH, Pair.Value.Y + Pair.Value.H);
        if (LayoutH > 0 && CompositeH > LayoutH)
        {
            const uint32 Scale = CompositeH / LayoutH;
            for (auto& Pair : RegionCoords)
            { Pair.Value.X *= Scale; Pair.Value.Y *= Scale; Pair.Value.W *= Scale; Pair.Value.H *= Scale; }
        }
    }

    // Texture overlay index → Region mapping:
    // TextureOverlays[0] → ARM_UPPER (region 0)
    // TextureOverlays[1] → ARM_LOWER (region 1)
    // TextureOverlays[2] → HAND (region 2)
    // TextureOverlays[3] → TORSO_UPPER (region 3)
    // TextureOverlays[4] → TORSO_LOWER (region 4)
    // TextureOverlays[5] → LEG_UPPER (region 5)
    // TextureOverlays[6] → LEG_LOWER (region 6)
    // TextureOverlays[7] → FOOT (region 7)

    // Race+Gender suffix for equipment texture paths
    // Format: 2-letter race code + gender char (lowercase), e.g. "hum" = Human Male, "bef" = Blood Elf Female
    FString RaceGenderSuffix;
    {
        const FChrRacesDbcEntry* Race = Dbc.ChrRaces().GetById(Equipment.RaceId);
        if (Race && !Race->ClientPrefix.IsEmpty())
        {
            FString RaceCode = Race->ClientPrefix.Left(2).ToLower();
            TCHAR GenderChar = (Equipment.Gender == 1) ? TEXT('f') : TEXT('m');
            RaceGenderSuffix = RaceCode + GenderChar;
        }
        else
        {
            RaceGenderSuffix = (Equipment.Gender == 1) ? TEXT("F") : TEXT("M");
        }
        UE_LOG(LogWowCharTex, Log, TEXT("Equipment texture suffix: %s (Race=%d Gender=%d)"),
            *RaceGenderSuffix, Equipment.RaceId, Equipment.Gender);
    }

    // Map name suffix to region index and subdirectory
    auto DetectRegionFromName = [](const FString& Name, int32& OutRegion) -> const TCHAR* {
        static const struct { const TCHAR* Suffix; int32 Region; const TCHAR* Subdir; } SuffixMap[] = {
            { TEXT("_AU"), 0, TEXT("ArmUpperTexture") },
            { TEXT("_AL"), 1, TEXT("ArmLowerTexture") },
            { TEXT("_HA"), 2, TEXT("HandTexture") },
            { TEXT("_TU"), 3, TEXT("TorsoUpperTexture") },
            { TEXT("_TL"), 4, TEXT("TorsoLowerTexture") },
            { TEXT("_LU"), 5, TEXT("LegUpperTexture") },
            { TEXT("_LL"), 6, TEXT("LegLowerTexture") },
            { TEXT("_FO"), 7, TEXT("FootTexture") },
            // Also check full suffixes
            { TEXT("_Sleeve_AU"), 0, TEXT("ArmUpperTexture") },
            { TEXT("_Sleeve_AL"), 1, TEXT("ArmLowerTexture") },
            { TEXT("_Chest_TU"), 3, TEXT("TorsoUpperTexture") },
            { TEXT("_Chest_TL"), 4, TEXT("TorsoLowerTexture") },
            { TEXT("_Pant_LU"), 5, TEXT("LegUpperTexture") },
            { TEXT("_Pant_LL"), 6, TEXT("LegLowerTexture") },
        };
        for (const auto& S : SuffixMap)
        {
            if (Name.EndsWith(S.Suffix))
            {
                OutRegion = S.Region;
                return S.Subdir;
            }
        }
        OutRegion = -1;
        return nullptr;
    };

    auto ApplyItemOverlays = [&](uint32 DisplayId, const TArray<uint32>& RegionIndices) {
        if (DisplayId == 0) return;

        const FItemDisplayInfoDbcEntry* Item = Dbc.ItemDisplayInfo().GetById(DisplayId);
        if (!Item) return;

        for (int32 OverlayIdx = 0; OverlayIdx < 8; ++OverlayIdx)
        {
            if (Item->TextureOverlays[OverlayIdx].IsEmpty()) continue;

            FString Name = Item->TextureOverlays[OverlayIdx];

            // Detect the region from the texture name suffix
            int32 DetectedRegion = -1;
            const TCHAR* Subdir = DetectRegionFromName(Name, DetectedRegion);
            if (!Subdir || DetectedRegion < 0) continue;

            // Check if this region is in the list of regions we should apply
            bool bShouldApply = false;
            for (uint32 RegionIdx : RegionIndices)
            {
                if (static_cast<int32>(RegionIdx) == DetectedRegion)
                {
                    bShouldApply = true;
                    break;
                }
            }
            if (!bShouldApply) continue;

            const FRegionCoords* Region = RegionCoords.Find(DetectedRegion);
            if (!Region) continue;

            // Try paths: with gender suffix, then bare name
            TArray<FString> Paths = {
                FString::Printf(TEXT("Item\\TextureComponents\\%s\\%s_%s.blp"), Subdir, *Name, *RaceGenderSuffix),
                FString::Printf(TEXT("Item\\TextureComponents\\%s\\%s_%c.blp"), Subdir, *Name, (Equipment.Gender == 1) ? TEXT('F') : TEXT('M')),
                FString::Printf(TEXT("Item\\TextureComponents\\%s\\%s_U.blp"), Subdir, *Name),
                FString::Printf(TEXT("Item\\TextureComponents\\%s\\%s.blp"), Subdir, *Name),
            };

            uint32 OverlayW, OverlayH;
            TArray<uint8> OverlayPixels;
            for (const FString& TryPath : Paths)
            {
                OverlayPixels = LoadBlpAsRGBA(Mpq, TryPath, OverlayW, OverlayH);
                if (OverlayPixels.Num() > 0) break;
            }
            if (OverlayPixels.Num() == 0)
            {
                // Try to verify the MPQ can read ANYTHING from Item\TextureComponents
                TArray<uint8> TestData;
                bool bMpqTest = Mpq->ReadFile(Paths[0], TestData);
                UE_LOG(LogWowCharTex, Warning, TEXT("Equipment overlay not found for [%d]: %s (tried %d paths, MpqRead=%d, DataSize=%d)"),
                    OverlayIdx, *Name, Paths.Num(), bMpqTest ? 1 : 0, TestData.Num());
            }
            if (OverlayPixels.Num() > 0)
            {
                PasteRegionWithAlpha(Composite, OverlayPixels, CompositeW, CompositeH,
                    OverlayW, OverlayH, Region->X, Region->Y, Region->W, Region->H);

                UE_LOG(LogWowCharTex, Log, TEXT("Applied equipment overlay[%d] for DisplayId %d: %s"),
                    OverlayIdx, DisplayId, *Name);
            }
        }
    };

    // WoW texture layering order (bottom to top):
    // 1. Skin (already applied as base)
    // 2. Underwear (already applied)
    // 3. Shirt
    // 4. Pants (skip if wearing robe)
    // 5. Boots
    // 6. Bracers
    // 7. Gloves
    // 8. Chest/Robe (on TOP — covers everything underneath)

    bool bIsWearingRobe = Equipment.bIsRobeEquipped;
    if (!bIsWearingRobe && Equipment.ChestDisplayId > 0)
    {
        const FItemDisplayInfoDbcEntry* ChestItem = Dbc.ItemDisplayInfo().GetById(Equipment.ChestDisplayId);
        if (ChestItem && ChestItem->GeosetGroups[2] > 0)
            bIsWearingRobe = true;
    }

    // SHIRT: arms only when wearing robe (robe covers torso), full when no robe
    if (Equipment.ShirtDisplayId > 0)
    {
        if (bIsWearingRobe)
        {
            // Only shirt sleeves show under a sleeveless robe
            TArray<uint32> ShirtRegions = {0, 1}; // ARM_UPPER, ARM_LOWER only
            ApplyItemOverlays(Equipment.ShirtDisplayId, ShirtRegions);
        }
        else
        {
            TArray<uint32> ShirtRegions = {0, 1, 3, 4};
            ApplyItemOverlays(Equipment.ShirtDisplayId, ShirtRegions);
        }
    }

    // PANTS: skip entirely when wearing robe (robe covers legs)
    if (!bIsWearingRobe)
    {
        TArray<uint32> PantsRegions = {5, 6};
        ApplyItemOverlays(Equipment.PantsDisplayId, PantsRegions);
    }

    // BOOTS
    TArray<uint32> BootsRegions = {6, 7};
    ApplyItemOverlays(Equipment.BootsDisplayId, BootsRegions);

    // BRACERS
    TArray<uint32> BracersRegions = {1};
    ApplyItemOverlays(Equipment.BracersDisplayId, BracersRegions);

    // GLOVES
    TArray<uint32> GlovesRegions = {1, 2};
    ApplyItemOverlays(Equipment.GlovesDisplayId, GlovesRegions);

    // CHEST/ROBE: applied LAST so it covers everything underneath
    if (Equipment.ChestDisplayId > 0)
    {
        if (bIsWearingRobe)
        {
            // Robe covers torso + legs (arms only if robe has sleeve textures)
            TArray<uint32> RobeRegions = {0, 1, 3, 4, 5, 6};
            ApplyItemOverlays(Equipment.ChestDisplayId, RobeRegions);
        }
        else
        {
            TArray<uint32> ChestRegions = {0, 1, 3, 4};
            ApplyItemOverlays(Equipment.ChestDisplayId, ChestRegions);
        }
    }

    // BELT: overlays [4]=TORSO_LOWER, [5]=LEG_UPPER
    TArray<uint32> BeltRegions = {4, 5};
    ApplyItemOverlays(Equipment.BeltDisplayId, BeltRegions);

    // TABARD: overlays [3]=TORSO_UPPER, [4]=TORSO_LOWER
    TArray<uint32> TabardRegions = {3, 4};
    ApplyItemOverlays(Equipment.TabardDisplayId, TabardRegions);

    // r343: the guild design on the blank Guild Tabard (Stefan: guild members wore plain white).
    // Background colour, border (style+colour) and emblem (style+colour) layers per torso half,
    // Textures\GuildEmblems\*_TU_U.blp (128x64) and *_TL_U.blp (128x32).
    const uint32 Guild = Equipment.GuildTabard;
    if ((Guild & 0x80000000u) && Equipment.TabardDisplayId)
    {
        // r551: layout with a 7-bit background (colours 0-50), see MakeGuildTabard.
        const uint32 EmblemStyle = (Guild >> 21) & 0xffu, EmblemColor = (Guild >> 16) & 0x1fu;
        const uint32 BorderStyle = (Guild >> 12) & 0xfu, BorderColor = (Guild >> 7) & 0x1fu, Background = Guild & 0x7fu;
        const TCHAR* Halves[2] = { TEXT("TU"), TEXT("TL") };
        const int32 HalfRegions[2] = { 3, 4 };
        for (int32 Half = 0; Half < 2; ++Half)
        {
            const FRegionCoords* Region = RegionCoords.Find(HalfRegions[Half]);
            if (!Region) continue;
            const FString Layers[3] = {
                FString::Printf(TEXT("Textures\\GuildEmblems\\Background_%02u_%s_U.blp"), Background, Halves[Half]),
                FString::Printf(TEXT("Textures\\GuildEmblems\\Border_%02u_%02u_%s_U.blp"), BorderStyle, BorderColor, Halves[Half]),
                FString::Printf(TEXT("Textures\\GuildEmblems\\Emblem_%02u_%02u_%s_U.blp"), EmblemStyle, EmblemColor, Halves[Half]) };
            for (const FString& Layer : Layers)
            {
                uint32 LayerW = 0, LayerH = 0;
                const TArray<uint8> Pixels = LoadBlpAsRGBA(Mpq, Layer, LayerW, LayerH);
                if (Pixels.Num() == 0) { UE_LOG(LogWowCharTex, Warning, TEXT("Guild tabard layer not found: %s"), *Layer); continue; }
                PasteRegionWithAlpha(Composite, Pixels, CompositeW, CompositeH, LayerW, LayerH, Region->X, Region->Y, Region->W, Region->H);
            }
        }
    }
}

TArray<uint8> FWowCharacterTexture::LoadBlpRGBA(FMpqManager* Mpq, const FString& Path, uint32& OutW, uint32& OutH)
{
    if (!Mpq) { OutW = OutH = 0; return TArray<uint8>(); }
    return LoadBlpAsRGBA(Mpq, Path, OutW, OutH);
}
