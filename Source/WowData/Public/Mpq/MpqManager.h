#pragma once
#include "CoreMinimal.h"

typedef void* HANDLE;

enum class EWowAssetProfile : uint8 { OriginalWotlk335, WithOverrides };

class WOWDATA_API FMpqManager
{
public:
    FMpqManager();
    ~FMpqManager();
    // Explicit roots have highest priority, then the WoW installation root and Data.
    bool Initialize(const FString& DataPath, const TArray<FString>& OverrideRoots = {}, EWowAssetProfile Profile = EWowAssetProfile::WithOverrides);
    // Stock-only is for original login artwork; gameplay keeps all mod/loose overrides.
    EWowAssetProfile GetAssetProfile() const { return AssetProfile; }
    TArray<FString> GetLooseFileRoots() const;
    static FString FindClientRoot(const FString& StartDirectory);
    /** <client root>\\User: everything that is the player's (settings, addons, data overlay, sky,
     *  sounds, screenshots). Falls back to the client root for the old flat layout. */
    static FString FindUserRoot(const FString& StartDirectory);
    void Shutdown();
    bool ReadFile(const FString& FilePath, TArray<uint8>& OutData) const;
    bool FileExists(const FString& FilePath) const;
    const FString& GetDataPath() const { return DataPath; }
    /**
     * HD character data (patch-H*.MPQ and the loose HD_Data textures) is never mixed into
     * the normal lookup: stock paths stay stock. It is read only through the "HDX\\" prefix,
     * e.g. ReadFile("HDX\\Character\\Orc\\Male\\OrcMale.m2").
     */
    static const TCHAR* HDPrefix() { return TEXT("HDX/"); }
    bool HasHDData() const;
    /** r362: true once any MPQ manager found HD data (Data\HD folder or patch-H*.MPQ); greys out the HD options. */
    static bool AnyHDDataAvailable();
    /** <client root>/Data: generic overlay above every server's data, also used offline. */
    const FString& GetOverlayDataPath() const { return OverlayDataPath; }
    bool IsInitialized() const { return bInitialized; }

private:
    HANDLE OpenArchive(const FString& ArchivePath) const;
    TArray<HANDLE> Archives;
    TArray<FString> LooseFileRoots;
    TArray<HANDLE> HDArchives;
    TArray<FString> HDLooseRoots;
    FString DataPath;
    FString OverlayDataPath;
    bool ReadFromSet(const FString& Normalized, const TArray<FString>& Roots, const TArray<HANDLE>& Set, TArray<uint8>& OutData) const;
    bool ExistsInSet(const FString& Normalized, const TArray<FString>& Roots, const TArray<HANDLE>& Set) const;
    bool bInitialized = false;
    EWowAssetProfile AssetProfile = EWowAssetProfile::WithOverrides;
    mutable FCriticalSection ArchiveLock;
};
