#pragma once
#include "CoreMinimal.h"

class WOWDATA_API FDbcParser
{
public:
    bool Parse(const TArray<uint8>& Data);
    uint32 GetRecordCount() const { return RecordCount; }
    uint32 GetFieldCount() const { return FieldCount; }
    uint32 GetUInt(uint32 Record, uint32 Field) const;
    float GetFloat(uint32 Record, uint32 Field) const;
    FString GetString(uint32 Record, uint32 Field) const;
    FString GetLocalizedString(uint32 Record, uint32 FirstField) const;
    bool IsValid() const { return bIsValid; }
private:
    uint32 RecordCount=0, FieldCount=0, RecordSize=0, StringBlockSize=0;
    const uint8* RecordData=nullptr;
    const uint8* StringBlock=nullptr;
    TArray<uint8> RawData;
    bool bIsValid = false;
};
