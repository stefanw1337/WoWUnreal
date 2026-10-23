#include "Formats/Dbc/TalentTabDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogTalentTabDbc, Log, All);

bool FTalentTabDbc::Load(const FDbcParser& Parser)
{
	if (!Parser.IsValid())
	{
		return false;
	}

	Entries.Reset();
	IdIndex.Reset();
	Entries.Reserve(Parser.GetRecordCount());

	for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
	{
		FTalentTabDbcEntry E;
		E.TalentTabID = Parser.GetUInt(i, 0);
		E.Name = Parser.GetLocalizedString(i, 1);
		// Skip columns 2-17 (Name localized)
		// 3.3.5 layout: 1-17 Name_lang (16 locales + flags), 18 SpellIconID, 19 RaceMask,
		// 20 ClassMask, 21 PetTalentMask, 22 OrderIndex, 23 BackgroundFile.
		E.NameFlags = Parser.GetUInt(i, 17);
		E.SpellIcon = Parser.GetUInt(i, 18);
		E.ClassMask = Parser.GetUInt(i, 20);
		E.PetTalentMask = Parser.GetUInt(i, 21);
		E.TabPage = Parser.GetUInt(i, 22);
		E.InternalName = Parser.GetString(i, 23);

		IdIndex.Add(E.TalentTabID, Entries.Num());
		Entries.Add(MoveTemp(E));
	}

	UE_LOG(LogTalentTabDbc, Log, TEXT("Loaded TalentTab.dbc: %d records"), Entries.Num());
	return true;
}

const FTalentTabDbcEntry* FTalentTabDbc::GetById(uint32 ID) const
{
	const int32* Idx = IdIndex.Find(ID);
	return Idx ? &Entries[*Idx] : nullptr;
}