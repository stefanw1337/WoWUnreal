#pragma once
#include "Formats/Dbc/CharSectionsDbc.h"
#include "Formats/Dbc/CharHairGeosetsDbc.h"

namespace WowCharacterAppearance
{
// Keep the extended player hairstyles already offered by character creation.
// Their color rows can use the barber flag (2) instead of the creation flag (1).
inline TArray<int32> HairChoices(const TArray<FCharSectionsDbcEntry>& Sections,
    const TArray<FCharHairGeosetsDbcEntry>& Geosets, uint32 Race, uint32 Gender,
    int32 Style = INDEX_NONE)
{
    TArray<int32> Result;
    for (const auto& Entry : Sections)
    {
        if (Entry.RaceID != Race || Entry.SexID != Gender || Entry.Type != 3
            || !(Entry.Flags & 3)) continue;
        if (!Geosets.ContainsByPredicate([&](const auto& Hair)
            { return Hair.RaceID == Race && Hair.SexID == Gender
                && Hair.VariationID == Entry.Variation; })) continue;
        if (Style == INDEX_NONE) Result.AddUnique(Entry.Variation);
        else if (Entry.Variation == static_cast<uint32>(Style)) Result.AddUnique(Entry.Color);
    }
    Result.Sort();
    return Result;
}
}
