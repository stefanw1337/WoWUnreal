#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FEmotesDbcEntry
{
    uint32 ID = 0, AnimationID = 0, Flags = 0, Type = 0, StandState = 0;
    bool IsLooping() const { return Type == 2; }
};

class WOWDATA_API FEmotesDbc
{
public:
    bool Load(const FDbcParser& Parser)
    {
        Entries.Reset();
        if (!Parser.IsValid() || Parser.GetFieldCount() != 7) return false;
        for (uint32 I = 0; I < Parser.GetRecordCount(); ++I)
        {
            FEmotesDbcEntry E;
            E.ID = Parser.GetUInt(I,0); E.AnimationID = Parser.GetUInt(I,2);
            E.Flags = Parser.GetUInt(I,3); E.Type = Parser.GetUInt(I,4); E.StandState = Parser.GetUInt(I,5);
            Entries.Add(E.ID,E);
        }
        return true;
    }
    const FEmotesDbcEntry* GetById(uint32 Id) const { return Entries.Find(Id); }
private:
    TMap<uint32,FEmotesDbcEntry> Entries;
};
