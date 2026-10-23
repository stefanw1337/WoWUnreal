#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FEmotesTextDbcEntry
{
	uint32 ID;
	FString Name;
	uint32 EmoteID;
};

class WOWDATA_API FEmotesTextDbc
{
public:
	bool Load(const FDbcParser& Parser);
	const FEmotesTextDbcEntry* GetByCommand(const FString& Command) const;
	const FEmotesTextDbcEntry* GetById(uint32 ID) const;
	const TArray<FEmotesTextDbcEntry>& GetAll() const { return Entries; }
	int32 Num() const { return Entries.Num(); }

private:
	TArray<FEmotesTextDbcEntry> Entries;
	TMap<uint32, int32> IdIndex;
};