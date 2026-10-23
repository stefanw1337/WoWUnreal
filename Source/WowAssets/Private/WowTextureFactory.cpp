#include "WowTextureFactory.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTexture, Log, All);

namespace
{
FName MakeUniqueTransientTextureName(const FString& SourceName)
{
    FString SafeName = SourceName.IsEmpty() ? TEXT("WowTexture") : SourceName;
    SafeName.ReplaceInline(TEXT("\\"), TEXT("_"));
    SafeName.ReplaceInline(TEXT("/"), TEXT("_"));
    SafeName.ReplaceInline(TEXT("."), TEXT("_"));
    SafeName.ReplaceInline(TEXT(":"), TEXT("_"));
    SafeName.ReplaceInline(TEXT(" "), TEXT("_"));

    if (SafeName.Len() > 96)
    {
        SafeName = SafeName.Right(96);
    }

    const FName BaseName(*FString::Printf(TEXT("Wow_%s"), *SafeName));
    return MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), BaseName);
}

int64 GetExpectedMipBytes(EPixelFormat PixelFormat, uint32 Width, uint32 Height)
{
	const uint32 SafeWidth = FMath::Max(Width, 1u);
	const uint32 SafeHeight = FMath::Max(Height, 1u);
	if (PixelFormat == PF_DXT1 || PixelFormat == PF_DXT3 || PixelFormat == PF_DXT5)
	{
		const int64 BlockBytes = PixelFormat == PF_DXT1 ? 8 : 16;
		const int64 BlocksWide = FMath::Max<int64>((SafeWidth + 3u) / 4u, 1);
		const int64 BlocksHigh = FMath::Max<int64>((SafeHeight + 3u) / 4u, 1);
		return BlocksWide * BlocksHigh * BlockBytes;
	}
	return static_cast<int64>(SafeWidth) * static_cast<int64>(SafeHeight) * 4;
}
}

UTexture2D* FWowTextureFactory::CreateTexture(const FBlpTexture& BlpData, const FString& Name)
{
    if (!BlpData.bIsValid || BlpData.MipLevels.Num() == 0) return nullptr;

    EPixelFormat PF;
    const TCHAR* PFName = TEXT("Unknown");
    switch (BlpData.PixelFormat)
    {
        case EBlpPixelFormat::DXT1: PF = PF_DXT1; PFName = TEXT("DXT1"); break;
        case EBlpPixelFormat::DXT3: PF = PF_DXT3; PFName = TEXT("DXT3"); break;
        case EBlpPixelFormat::DXT5: PF = PF_DXT5; PFName = TEXT("DXT5"); break;
        case EBlpPixelFormat::RGBA8: PF = PF_R8G8B8A8; PFName = TEXT("RGBA8"); break;
        default:
            UE_LOG(LogWowTexture, Warning, TEXT("Unsupported BLP pixel format for '%s'"), *Name);
            return nullptr;
    }

    const FName TextureObjectName = MakeUniqueTransientTextureName(Name);
    UTexture2D* Tex = UTexture2D::CreateTransient(BlpData.Width, BlpData.Height, PF, TextureObjectName);
    if (!Tex)
    {
        UE_LOG(LogWowTexture, Error, TEXT("CreateTransient failed for '%s' (%dx%d %s)"),
            *Name, BlpData.Width, BlpData.Height, PFName);
        return nullptr;
    }

	// Preserve the authored mip chain and blend between levels. Sampling only
	// mip zero caused high-frequency world textures to shimmer in rectangular
	// blocks whenever the camera moved over terrain or WMO floors.
    Tex->Filter = TF_Trilinear;
    Tex->SRGB = true;
    Tex->NeverStream = true;

	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	if (!PlatformData)
	{
		UE_LOG(LogWowTexture, Error, TEXT("Texture platform data missing for '%s'"), *Name);
		return nullptr;
	}

	int32 ValidMipCount = 0;
	for (int32 MipIndex = 0; MipIndex < BlpData.MipLevels.Num(); ++MipIndex)
	{
		const FBlpMipLevel& SourceMip = BlpData.MipLevels[MipIndex];
		const uint32 ExpectedWidth = FMath::Max(BlpData.Width >> MipIndex, 1u);
		const uint32 ExpectedHeight = FMath::Max(BlpData.Height >> MipIndex, 1u);
		const int64 ExpectedBytes = GetExpectedMipBytes(PF, ExpectedWidth, ExpectedHeight);
		if (SourceMip.Width != ExpectedWidth || SourceMip.Height != ExpectedHeight ||
			SourceMip.Data.Num() != ExpectedBytes)
		{
			UE_LOG(LogWowTexture, Warning,
				TEXT("Ignoring invalid mip %d for '%s': got %ux%u/%d bytes, expected %ux%u/%lld bytes"),
				MipIndex, *Name, SourceMip.Width, SourceMip.Height, SourceMip.Data.Num(),
				ExpectedWidth, ExpectedHeight, ExpectedBytes);
			break;
		}
		++ValidMipCount;
	}

	if (ValidMipCount == 0)
	{
		UE_LOG(LogWowTexture, Error, TEXT("No valid mip data for '%s'"), *Name);
		return nullptr;
	}

	PlatformData->Mips.Empty();
	for (int32 MipIndex = 0; MipIndex < ValidMipCount; ++MipIndex)
	{
		const FBlpMipLevel& SourceMip = BlpData.MipLevels[MipIndex];
		FTexture2DMipMap* DestinationMip = new FTexture2DMipMap();
		PlatformData->Mips.Add(DestinationMip);
		DestinationMip->SizeX = static_cast<int32>(SourceMip.Width);
		DestinationMip->SizeY = static_cast<int32>(SourceMip.Height);
		void* DestinationData = DestinationMip->BulkData.Lock(LOCK_READ_WRITE);
		DestinationData = DestinationMip->BulkData.Realloc(SourceMip.Data.Num());
		FMemory::Memcpy(DestinationData, SourceMip.Data.GetData(), SourceMip.Data.Num());
		DestinationMip->BulkData.Unlock();
	}
    Tex->UpdateResource();

	UE_LOG(LogWowTexture, Verbose, TEXT("Created texture '%s': %dx%d %s (%d mips)"),
		*Name, BlpData.Width, BlpData.Height, PFName, ValidMipCount);

    return Tex;
}
