#include "WowOfflineCharacterStore.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString GetOfflineCharacterFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OfflineCharacters.json"));
}

uint8 ReadByte(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, uint8 DefaultValue)
{
	double Value = DefaultValue;
	Object->TryGetNumberField(FieldName, Value);
	return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value), 0, 255));
}

int32 ReadInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, int32 DefaultValue)
{
	double Value = DefaultValue;
	Object->TryGetNumberField(FieldName, Value);
	return FMath::RoundToInt(Value);
}
}

FWowCharacterInfo FWowOfflineCharacterRecord::ToCharacterInfo() const
{
	FWowCharacterInfo Result;
	Result.Guid = Guid;
	Result.Name = Name;
	Result.Race = Race;
	Result.Class = Class;
	Result.Gender = Gender;
	Result.Skin = Skin;
	Result.Face = Face;
	Result.HairStyle = HairStyle;
	Result.HairColor = HairColor;
	Result.FacialHair = FacialHair;
	Result.Level = Level;
	Result.MapId = MapId;
	Result.ZoneId = ZoneId;
	Result.Position = Position;
	return Result;
}

bool FWowOfflineCharacterStore::Load(TArray<FWowOfflineCharacterRecord>& OutCharacters)
{
	OutCharacters.Reset();

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *GetOfflineCharacterFilePath()))
	{
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> Values;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Values))
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			continue;
		}

		FWowOfflineCharacterRecord Character;
		double GuidValue = 0.0;
		Object->TryGetNumberField(TEXT("guid"), GuidValue);
		Character.Guid = static_cast<int64>(GuidValue);
		Object->TryGetStringField(TEXT("name"), Character.Name);
		Character.Race = ReadByte(Object, TEXT("race"), 1);
		Character.Class = ReadByte(Object, TEXT("class"), 1);
		Character.Gender = ReadByte(Object, TEXT("gender"), 0);
		Character.Skin = ReadByte(Object, TEXT("skin"), 0);
		Character.Face = ReadByte(Object, TEXT("face"), 0);
		Character.HairStyle = ReadByte(Object, TEXT("hairStyle"), 0);
		Character.HairColor = ReadByte(Object, TEXT("hairColor"), 0);
		Character.FacialHair = ReadByte(Object, TEXT("facialHair"), 0);
		Character.Level = ReadByte(Object, TEXT("level"), 1);
		Character.MapId = ReadInt(Object, TEXT("map"), 0);
		Character.ZoneId = ReadInt(Object, TEXT("zone"), 12);
		double PositionX = Character.Position.X;
		double PositionY = Character.Position.Y;
		double PositionZ = Character.Position.Z;
		Object->TryGetNumberField(TEXT("x"), PositionX);
		Object->TryGetNumberField(TEXT("y"), PositionY);
		Object->TryGetNumberField(TEXT("z"), PositionZ);
		Character.Position = FVector(PositionX, PositionY, PositionZ);
        double Orientation=0;Object->TryGetNumberField(TEXT("orientation"),Orientation);
        Character.Orientation=FMath::IsFinite(Orientation)?Orientation:0;
		// Characters created before start locations existed were all placed in Northshire.
		if (Character.MapId == 0 && Character.Position.Equals(FVector(-8949.0f, -132.0f, 84.0f), 1.0f)
			&& Character.Race != 1 && Character.Level <= 1)
		{
			ApplyStartLocation(Character);
		}

		if (Character.Guid > 0 && !Character.Name.IsEmpty())
		{
			OutCharacters.Add(Character);
		}
	}

	return true;
}

bool FWowOfflineCharacterStore::Save(const TArray<FWowOfflineCharacterRecord>& Characters)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FWowOfflineCharacterRecord& Character : Characters)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("guid"), static_cast<double>(Character.Guid));
		Object->SetStringField(TEXT("name"), Character.Name);
		Object->SetNumberField(TEXT("race"), Character.Race);
		Object->SetNumberField(TEXT("class"), Character.Class);
		Object->SetNumberField(TEXT("gender"), Character.Gender);
		Object->SetNumberField(TEXT("skin"), Character.Skin);
		Object->SetNumberField(TEXT("face"), Character.Face);
		Object->SetNumberField(TEXT("hairStyle"), Character.HairStyle);
		Object->SetNumberField(TEXT("hairColor"), Character.HairColor);
		Object->SetNumberField(TEXT("facialHair"), Character.FacialHair);
		Object->SetNumberField(TEXT("level"), Character.Level);
		Object->SetNumberField(TEXT("map"), Character.MapId);
		Object->SetNumberField(TEXT("zone"), Character.ZoneId);
		Object->SetNumberField(TEXT("x"), Character.Position.X);
		Object->SetNumberField(TEXT("y"), Character.Position.Y);
		Object->SetNumberField(TEXT("z"), Character.Position.Z);
        Object->SetNumberField(TEXT("orientation"),Character.Orientation);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Values, Writer))
	{
		return false;
	}

	return FFileHelper::SaveStringToFile(Json, *GetOfflineCharacterFilePath());
}

int64 FWowOfflineCharacterStore::AllocateGuid(const TArray<FWowOfflineCharacterRecord>& Characters)
{
	int64 HighestGuid = 0;
	for (const FWowOfflineCharacterRecord& Character : Characters)
	{
		HighestGuid = FMath::Max(HighestGuid, Character.Guid);
	}
	return HighestGuid + 1;
}

void FWowOfflineCharacterStore::ApplyStartLocation(FWowOfflineCharacterRecord& Character)
{
	struct FStart { int32 Map; int32 Zone; float X, Y, Z, O; };
	FStart Start{0, 12, -8949.95f, -132.493f, 83.5312f, 0.f};          // Human: Northshire
	if (Character.Class == 6) Start = {609, 4298, 2355.84f, -5664.77f, 426.028f, 3.65997f}; // Death knight: Acherus
	else switch (Character.Race)
	{
	case 2: case 8: Start = {1, 14, -618.518f, -4251.67f, 38.718f, 0.f}; break;          // Orc, Troll: Valley of Trials
	case 3: case 7: Start = {0, 1, -6240.32f, 331.033f, 382.758f, 6.17716f}; break;      // Dwarf, Gnome: Coldridge Valley
	case 4: Start = {1, 141, 10311.3f, 832.463f, 1326.41f, 5.69632f}; break;           // Night elf: Shadowglen
	case 5: Start = {0, 85, 1676.71f, 1678.31f, 121.67f, 2.70526f}; break;             // Undead: Deathknell
	case 6: Start = {1, 215, -2917.58f, -257.98f, 52.9968f, 0.f}; break;               // Tauren: Camp Narache
	case 10: Start = {530, 3430, 10349.6f, -6357.29f, 33.4026f, 5.31605f}; break;      // Blood elf: Sunstrider Isle
	case 11: Start = {530, 3524, -3961.64f, -13931.2f, 100.615f, 2.08364f}; break;     // Draenei: Ammen Vale
	default: break;
	}
	Character.MapId = Start.Map;
	Character.ZoneId = Start.Zone;
	Character.Position = FVector(Start.X, Start.Y, Start.Z);
	Character.Orientation = Start.O;
}
