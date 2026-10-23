#include "Formats/WdtParser.h"
#include "Formats/ChunkId.h"
DEFINE_LOG_CATEGORY_STATIC(LogWdt, Log, All);

// These match what's ON DISK (reversed)
static constexpr uint32 WDT_MVER = MakeFourCC('R', 'E', 'V', 'M');
static constexpr uint32 WDT_MPHD = MakeFourCC('D', 'H', 'P', 'M');
static constexpr uint32 WDT_MAIN = MakeFourCC('N', 'I', 'A', 'M');
static constexpr uint32 WDT_MWMO = MakeFourCC('O', 'M', 'W', 'M');
static constexpr uint32 WDT_MODF = MakeFourCC('F', 'D', 'O', 'M');

FWdtData FWdtParser::Parse(const TArray<uint8>& Data)
{
    FWdtData Result;
    if (Data.Num() < 8) return Result;

    const uint8* Ptr = Data.GetData();
    const uint8* End = Ptr + Data.Num();

    while (Ptr + 8 <= End)
    {
        uint32 Magic = *reinterpret_cast<const uint32*>(Ptr);
        uint32 Size  = *reinterpret_cast<const uint32*>(Ptr + 4);
        const uint8* ChunkData = Ptr + 8;

        if (ChunkData + Size > End) break;

        if (Magic == WDT_MPHD)
        {
            if (Size >= 4)
            {
                Result.Flags = *reinterpret_cast<const uint32*>(ChunkData);
                Result.bUseBigAlpha = (Result.Flags & 0x04) != 0;
            }
        }
        else if (Magic == WDT_MAIN)
        {
            if (Size >= 64 * 64 * 8)
            {
                int32 TileCount = 0;
                for (int32 y = 0; y < 64; y++)
                {
                    for (int32 x = 0; x < 64; x++)
                    {
                        uint32 Flags = *reinterpret_cast<const uint32*>(ChunkData + (y * 64 + x) * 8);
                        Result.TileExists[x][y] = (Flags & 0x01) != 0;
                        if (Flags & 0x01) TileCount++;
                    }
                }
                UE_LOG(LogWdt, Log, TEXT("MAIN chunk: found %d existing tiles"), TileCount);
            }
        }
        else if (Magic == WDT_MWMO)
        {
            if (Size > 0)
            {
                Result.GlobalWmoPath = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(ChunkData)));
            }
        }
        else if (Magic == WDT_MODF && Size >= 64)
        {
            // r556: the placement of the global WMO (64 bytes, as in an ADT MODF).
            auto F = [ChunkData](int32 Offset) { return *reinterpret_cast<const float*>(ChunkData + Offset); };
            auto U16 = [ChunkData](int32 Offset) { return *reinterpret_cast<const uint16*>(ChunkData + Offset); };
            FAdtWmoPlacement& P = Result.GlobalWmoPlacement;
            P.NameIndex = 0;
            P.UniqueId = *reinterpret_cast<const uint32*>(ChunkData + 4);
            P.Position = FVector(F(8), F(12), F(16));
            P.Rotation = FVector(F(20), F(24), F(28));
            P.BoundingBox = FBox(FVector(F(32), F(36), F(40)), FVector(F(44), F(48), F(52)));
            P.Flags = U16(56);
            P.DoodadSet = U16(58);
            P.NameSet = U16(60);
            P.Scale = U16(62);
            Result.bHasGlobalWmoPlacement = true;
        }

        Ptr = ChunkData + Size;
    }

    Result.bIsValid = true;
    UE_LOG(LogWdt, Log, TEXT("WDT parsed: flags=0x%X, bigAlpha=%d, globalWmo='%s'"),
        Result.Flags, Result.bUseBigAlpha ? 1 : 0, *Result.GlobalWmoPath);
    return Result;
}
