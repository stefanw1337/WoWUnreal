#pragma once
#include "CoreMinimal.h"
class UTexture2D;
class UStaticMesh;
class USkeletalMesh;

struct WOWASSETS_API FWowCacheStats
{
    int32 TextureCount = 0;
    int32 MeshCount = 0;
    int32 SkelMeshCount = 0;
    int64 EstimatedTextureMemory = 0;
    int64 EstimatedMeshMemory = 0;
    int64 EstimatedSkelMeshMemory = 0;
    int64 TotalEstimatedMemory() const { return EstimatedTextureMemory + EstimatedMeshMemory + EstimatedSkelMeshMemory; }
};

class WOWASSETS_API FWowAssetCache
{
public:
    void SetRuntimeContext(UObject* Context) { RuntimeContext = Context; }
    UObject* GetRuntimeContext() const { return RuntimeContext.IsValid() ? RuntimeContext.Get() : GetTransientPackage(); }
    void CacheTexture(const FString& Path, UTexture2D* Tex);
    UTexture2D* FindTexture(const FString& Path) const;
    void CacheMesh(const FString& Path, UStaticMesh* Mesh);
    UStaticMesh* FindMesh(const FString& Path) const;
    void CacheSkelMesh(const FString& Path, USkeletalMesh* Mesh);
    USkeletalMesh* FindSkelMesh(const FString& Path) const;
    void Clear();

    /** Remove entries whose weak pointers are no longer valid */
    void PurgeStaleEntries();

    /** Get current cache statistics */
    FWowCacheStats GetStats() const;

    /** Memory budget in bytes (default 512MB). 0 = unlimited. */
    int64 MemoryBudget = 512 * 1024 * 1024;

    /** Check if over budget and log warning */
    bool IsOverBudget() const;

private:
    TWeakObjectPtr<UObject> RuntimeContext;
    mutable FCriticalSection Lock;
    TMap<FString, TWeakObjectPtr<UTexture2D>> Textures;
    TMap<FString, TWeakObjectPtr<UStaticMesh>> Meshes;
    TMap<FString, TWeakObjectPtr<USkeletalMesh>> SkelMeshes;

    static int64 EstimateTextureMemory(UTexture2D* Tex);
    static int64 EstimateMeshMemory(UStaticMesh* Mesh);
    static int64 EstimateSkelMeshMemory(USkeletalMesh* Mesh);
};
