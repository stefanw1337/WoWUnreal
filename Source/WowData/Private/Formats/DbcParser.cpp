#include "Formats/DbcParser.h"
#include "WowLocale.h"
DEFINE_LOG_CATEGORY_STATIC(LogDbc, Log, All);

#pragma pack(push,1)
struct FDbcHeader { uint32 Magic, RecordCount, FieldCount, RecordSize, StringBlockSize; };
#pragma pack(pop)

bool FDbcParser::Parse(const TArray<uint8>& Data)
{
    bIsValid = false;
    if (Data.Num() < (int32)sizeof(FDbcHeader)) return false;
    const FDbcHeader* H = reinterpret_cast<const FDbcHeader*>(Data.GetData());
    if (H->Magic != 0x43424457) return false;
    RecordCount = H->RecordCount; FieldCount = H->FieldCount;
    RecordSize = H->RecordSize; StringBlockSize = H->StringBlockSize;
    uint32 Expected = sizeof(FDbcHeader) + RecordCount * RecordSize + StringBlockSize;
    if ((uint32)Data.Num() < Expected) return false;
    RawData = Data;
    RecordData = RawData.GetData() + sizeof(FDbcHeader);
    StringBlock = RecordData + RecordCount * RecordSize;
    bIsValid = true;
    return true;
}

uint32 FDbcParser::GetUInt(uint32 Rec, uint32 Fld) const
{
    if (!bIsValid || Rec >= RecordCount || Fld >= FieldCount) return 0;
    return *reinterpret_cast<const uint32*>(RecordData + Rec * RecordSize + Fld * 4);
}

float FDbcParser::GetFloat(uint32 Rec, uint32 Fld) const
{
    if (!bIsValid || Rec >= RecordCount || Fld >= FieldCount) return 0.f;
    return *reinterpret_cast<const float*>(RecordData + Rec * RecordSize + Fld * 4);
}

FString FDbcParser::GetString(uint32 Rec, uint32 Fld) const
{
    if (!bIsValid) return FString();
    uint32 Off = GetUInt(Rec, Fld);
    if (Off >= StringBlockSize) return FString();
    return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(StringBlock + Off)));
}

FString FDbcParser::GetLocalizedString(uint32 Record, uint32 FirstField) const
{
    const uint32 Index = WowLocale::DbcIndex();
    FString Result = GetString(Record, FirstField + Index);
    if (Result.IsEmpty()) Result = GetString(Record, FirstField);
    return Result;
}
