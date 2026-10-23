#include "WowCinematicManager.h"
#include "WowLocale.h"
#include "Mpq/MpqManager.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowCinematic, Log, All);

FWowCinematicManager::FWowCinematicManager()
{
    // Create media player
    MediaPlayer = NewObject<UMediaPlayer>(GetTransientPackage(), NAME_None, RF_Transient);
    if (MediaPlayer)
    {
        MediaPlayer->AddToRoot();
        MediaPlayer->SetLooping(true);
        MediaPlayer->PlayOnOpen = true;
    }

    // Create media texture bound to the player
    MediaTexture = NewObject<UMediaTexture>(GetTransientPackage(), NAME_None, RF_Transient);
    if (MediaTexture)
    {
        MediaTexture->AddToRoot();
        MediaTexture->AutoClear = true;
        MediaTexture->SetMediaPlayer(MediaPlayer);
        MediaTexture->UpdateResource();
    }

    UE_LOG(LogWowCinematic, Log, TEXT("CinematicManager created (MediaPlayer=%s MediaTexture=%s)"),
        MediaPlayer ? TEXT("OK") : TEXT("FAIL"),
        MediaTexture ? TEXT("OK") : TEXT("FAIL"));
}

FWowCinematicManager::~FWowCinematicManager()
{
    StopCinematic();

    if (MediaTexture)
    {
        MediaTexture->RemoveFromRoot();
        MediaTexture = nullptr;
    }
    if (MediaPlayer)
    {
        MediaPlayer->RemoveFromRoot();
        MediaPlayer = nullptr;
    }
}

bool FWowCinematicManager::PrepareCinematic(FMpqManager* Mpq, EWowExpansion Expansion)
{
    if (ExtractedExpansions.Contains(Expansion))
    {
        return HasCinematic(Expansion);
    }

    // Cinematics are loose files on disk (not in MPQ), in the locale subfolder
    // e.g. Data/enUS/Interface/Cinematics/WOW_Intro_800.avi
    FString DataPath = Mpq ? Mpq->GetDataPath() : TEXT("");
    if (DataPath.IsEmpty())
    {
        UE_LOG(LogWowCinematic, Warning, TEXT("No WoW data path available"));
        return false;
    }

    // Try multiple paths: selected locale and generic
    TArray<FString> SearchPaths;
    FString Filename = GetCinematicFilename(Expansion);

    // Locale paths first (most common location)
    SearchPaths.Add(DataPath / WowLocale::Get() / TEXT("Interface") / TEXT("Cinematics") / Filename);
    SearchPaths.Add(DataPath / TEXT("Interface") / TEXT("Cinematics") / Filename);
    // Also try 1024 variant
    FString Filename1024 = Filename.Replace(TEXT("_800"), TEXT("_1024"));
    SearchPaths.Add(DataPath / WowLocale::Get() / TEXT("Interface") / TEXT("Cinematics") / Filename1024);
    SearchPaths.Add(DataPath / TEXT("Interface") / TEXT("Cinematics") / Filename1024);

    for (const FString& Path : SearchPaths)
    {
        if (FPaths::FileExists(Path))
        {
            CinematicPaths.Add(Expansion, FPaths::ConvertRelativePathToFull(Path));
            ExtractedExpansions.Add(Expansion);
            UE_LOG(LogWowCinematic, Log, TEXT("Found cinematic for expansion %d: %s"),
                static_cast<int32>(Expansion), *Path);
            return true;
        }
    }

    UE_LOG(LogWowCinematic, Warning, TEXT("Cinematic not found for expansion %d (tried %d paths)"),
        static_cast<int32>(Expansion), SearchPaths.Num());
    for (const FString& Path : SearchPaths)
    {
        UE_LOG(LogWowCinematic, Warning, TEXT("  Tried: %s"), *Path);
    }
    return false;
}

FString FWowCinematicManager::GetCinematicFilename(EWowExpansion Expansion)
{
    switch (Expansion)
    {
    case EWowExpansion::Classic:
        return TEXT("WOW_Intro_800.avi");
    case EWowExpansion::BurningCrusade:
        return TEXT("WOW_Intro_BC_800.avi");
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return TEXT("WOW_FotLK_800.avi");
    }
}

FString FWowCinematicManager::GetCinematicPath(EWowExpansion Expansion) const
{
    const FString* Path = CinematicPaths.Find(Expansion);
    return Path ? *Path : FString();
}

void FWowCinematicManager::PlayCinematic(EWowExpansion Expansion)
{
    if (!MediaPlayer) return;

    FString Path = GetCinematicPath(Expansion);
    if (Path.IsEmpty())
    {
        UE_LOG(LogWowCinematic, Warning, TEXT("No cinematic path for expansion %d"), static_cast<int32>(Expansion));
        return;
    }

    UE_LOG(LogWowCinematic, Log, TEXT("Playing cinematic: %s"), *Path);

    if (!MediaPlayer->OpenUrl(Path))
    {
        UE_LOG(LogWowCinematic, Error, TEXT("Failed to open cinematic: %s"), *Path);
    }
}

void FWowCinematicManager::StopCinematic()
{
    if (MediaPlayer)
    {
        MediaPlayer->Close();
    }
}

bool FWowCinematicManager::IsPlaying() const
{
    return MediaPlayer && MediaPlayer->IsPlaying();
}

bool FWowCinematicManager::HasCinematic(EWowExpansion Expansion) const
{
    return CinematicPaths.Contains(Expansion);
}
