#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FItemDisplayInfoDbcEntry
{
    uint32 ID = 0;                  // 0
    FString ModelNames[2];          // 1-2
    FString ModelTextures[2];       // 3-4
    FString Icon1;                  // 5
    FString Icon2;                  // 6
    uint32 GeosetGroups[6]{};       // 7-9 in Wrath; remaining entries reserved/zero
    uint32 Flags = 0;               // 10
    uint32 SpellVisualID = 0;       // 11
    uint32 GroupSoundIndex = 0;     // 12
    uint32 HelmetGeosetVis[2]{};    // 13-14
    FString TextureOverlays[8];     // 15-22
    uint32 ItemVisual = 0;          // 23
    uint32 ParticleColorID = 0;     // 24
};

class WOWDATA_API FItemDisplayInfoDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FItemDisplayInfoDbcEntry* GetById(uint32 ID) const;
    const TArray<FItemDisplayInfoDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FItemDisplayInfoDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};
