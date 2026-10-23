#include "Formats/BlpParser.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlp, Log, All);

#pragma pack(push, 1)
struct FBlpHeader
{
    uint32 Magic;
    uint32 Type;
    uint8 Encoding;
    uint8 AlphaDepth;
    uint8 AlphaEncoding;
    uint8 HasMips;
    uint32 Width;
    uint32 Height;
    uint32 MipOffsets[16];
    uint32 MipSizes[16];
};
#pragma pack(pop)

FBlpTexture FBlpParser::Parse(const TArray<uint8>& Data)
{
    FBlpTexture Result;
    if (Data.Num() < (int32)sizeof(FBlpHeader)) return Result;
    const FBlpHeader* H = reinterpret_cast<const FBlpHeader*>(Data.GetData());
    if (H->Magic != 0x32504C42) { UE_LOG(LogBlp, Warning, TEXT("Invalid BLP magic: 0x%08X"), H->Magic); return Result; }
    if (H->Width == 0 || H->Height == 0 || H->Width > 16384 || H->Height > 16384) return Result;

    if (H->Encoding == 2)
        Result = ParseDXT(Data.GetData(), Data.Num(), H->Width, H->Height, H->AlphaDepth, H->AlphaEncoding, H->MipOffsets, H->MipSizes);
    else if (H->Encoding == 1)
    {
        Result = ParsePaletted(Data.GetData(), Data.Num(), H->Width, H->Height, H->AlphaDepth, H->MipOffsets, H->MipSizes);
        Result.PixelFormat = EBlpPixelFormat::RGBA8;
    }
    Result.Width = H->Width;
    Result.Height = H->Height;
    return Result;
}

FBlpTexture FBlpParser::ParseDXT(const uint8* Data, uint32 DataSize, uint32 Width, uint32 Height, uint8 AlphaDepth, uint8 AlphaEncoding, const uint32* MipOffsets, const uint32* MipSizes)
{
    FBlpTexture Result;
    Result.bIsValid = true;
    if (AlphaDepth == 0 || AlphaEncoding == 0) Result.PixelFormat = EBlpPixelFormat::DXT1;
    else if (AlphaEncoding == 1) Result.PixelFormat = EBlpPixelFormat::DXT3;
    else Result.PixelFormat = EBlpPixelFormat::DXT5;

    uint32 W = Width, H = Height;
    for (int32 i = 0; i < 16; i++)
    {
        if (MipOffsets[i] == 0 || MipSizes[i] == 0) break;
        if (static_cast<uint64>(MipOffsets[i]) + MipSizes[i] > DataSize) break;
        FBlpMipLevel& Mip = Result.MipLevels.AddDefaulted_GetRef();
        Mip.Width = FMath::Max(W, 1u); Mip.Height = FMath::Max(H, 1u);
        Mip.Data.SetNumUninitialized(MipSizes[i]);
        FMemory::Memcpy(Mip.Data.GetData(), Data + MipOffsets[i], MipSizes[i]);
        W >>= 1; H >>= 1;
    }
    if (Result.MipLevels.Num() == 0) Result.bIsValid = false;
    return Result;
}

FBlpTexture FBlpParser::ParsePaletted(const uint8* Data, uint32 DataSize, uint32 Width, uint32 Height, uint8 AlphaDepth, const uint32* MipOffsets, const uint32* MipSizes)
{
    FBlpTexture Result;
    const uint32 PalOff = sizeof(FBlpHeader);
    if (PalOff + 1024 > DataSize) return Result;
    const uint8* Pal = Data + PalOff;
    uint32 W = Width, H = Height;
    for (int32 i = 0; i < 16; i++)
    {
        if (MipOffsets[i] == 0 || MipSizes[i] == 0) break;
        if (static_cast<uint64>(MipOffsets[i]) + MipSizes[i] > DataSize) break;
        const uint64 PixelCount = static_cast<uint64>(FMath::Max(W,1u)) * FMath::Max(H,1u);
        if (PixelCount > MAX_int32 / 4) break;
        const uint32 Pixels = static_cast<uint32>(PixelCount);
        FBlpMipLevel& Mip = Result.MipLevels.AddDefaulted_GetRef();
        Mip.Width = FMath::Max(W,1u); Mip.Height = FMath::Max(H,1u);
        Mip.Data.SetNumUninitialized(Pixels * 4);
        const uint8* Idx = Data + MipOffsets[i];
        const uint32 IndexBytes = FMath::Min(Pixels, MipSizes[i]);
        const uint32 AlphaOffset = Pixels;
        const uint32 AlphaBytesAvailable = (MipSizes[i] > AlphaOffset) ? (MipSizes[i] - AlphaOffset) : 0;

        for (uint32 p = 0; p < Pixels && p < IndexBytes; p++)
        {
            const uint8* C = Pal + Idx[p] * 4;
            Mip.Data[p*4+0] = C[2]; // R (palette is BGRA, output RGBA)
            Mip.Data[p*4+1] = C[1]; // G
            Mip.Data[p*4+2] = C[0]; // B
            if (AlphaDepth == 8 && p < AlphaBytesAvailable)
            {
                Mip.Data[p*4+3] = Idx[AlphaOffset + p];
            }
            else if (AlphaDepth == 4 && (p / 2) < AlphaBytesAvailable)
            {
                uint8 a = Idx[AlphaOffset + p / 2];
                Mip.Data[p*4+3] = (p%2==0) ? ((a&0x0F)*17) : ((a>>4)*17);
            }
            else if (AlphaDepth == 1 && (p / 8) < AlphaBytesAvailable)
            {
                const uint8 a = Idx[AlphaOffset + p / 8];
                Mip.Data[p*4+3] = (a & (1u << (p % 8))) ? 255 : 0;
            }
            else
            {
                Mip.Data[p*4+3] = 255;
            }
        }

        for (uint32 p = IndexBytes; p < Pixels; p++)
        {
            Mip.Data[p*4+0] = 255;
            Mip.Data[p*4+1] = 255;
            Mip.Data[p*4+2] = 255;
            Mip.Data[p*4+3] = 0;
        }
        W >>= 1; H >>= 1;
    }
    Result.bIsValid = Result.MipLevels.Num() > 0;
    return Result;
}
