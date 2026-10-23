#include "Formats/Dbc/SpellVisualDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpellVisualDbc, Log, All);

bool FSpellVisualDbc::Load(const FDbcParser& Parser)
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
		FSpellVisualDbcEntry E;
		E.ID = Parser.GetUInt(i, 0);
		E.PrecastKit = Parser.GetUInt(i, 1);
		E.CastKit = Parser.GetUInt(i, 2);
		E.ImpactKit = Parser.GetUInt(i, 3);
		E.StateKit = Parser.GetUInt(i, 4);
		E.StateDoneKit = Parser.GetUInt(i, 5);
		E.ChannelKit = Parser.GetUInt(i, 6);
		E.HasMissile = Parser.GetUInt(i, 7);
		E.MissileModel = static_cast<int32>(Parser.GetUInt(i, 8));
		E.MissilePathType = Parser.GetUInt(i, 9);
		E.MissileDestinationAttachment = Parser.GetUInt(i, 10);
		E.MissileSound = Parser.GetUInt(i, 11);
		E.AnimEventSoundID = Parser.GetUInt(i, 12);
		E.Flags = Parser.GetUInt(i, 13);
		E.CasterImpactKit = Parser.GetUInt(i, 14);
		E.TargetImpactKit = Parser.GetUInt(i, 15);
		E.MissileAttachment = Parser.GetUInt(i, 16);
		E.MissileFollowGroundHeight = Parser.GetFloat(i, 17);
		E.MissileFollowGroundDropSpeed = Parser.GetFloat(i, 18);
		E.MissileFollowGroundApproach = Parser.GetFloat(i, 19);
		E.MissileFollowGroundFlags = Parser.GetUInt(i, 20);
		E.MissileMotion = Parser.GetUInt(i, 21);
		E.MissileTargetingKit = Parser.GetUInt(i, 22);
		E.InstantAreaKit = Parser.GetUInt(i, 23);
		E.ImpactAreaKit = Parser.GetUInt(i, 24);
		E.PersistentAreaKit = Parser.GetUInt(i, 25);
		E.MissileCastOffsetX = Parser.GetFloat(i, 26);
		E.MissileCastOffsetY = Parser.GetFloat(i, 27);
		E.MissileCastOffsetZ = Parser.GetFloat(i, 28);
		E.MissileImpactOffsetX = Parser.GetFloat(i, 29);
		E.MissileImpactOffsetY = Parser.GetFloat(i, 30);
		E.MissileImpactOffsetZ = Parser.GetFloat(i, 31);

		IdIndex.Add(E.ID, Entries.Num());
		Entries.Add(MoveTemp(E));
	}

	UE_LOG(LogSpellVisualDbc, Log, TEXT("Loaded SpellVisual.dbc: %d records"), Entries.Num());
	return true;
}

const FSpellVisualDbcEntry* FSpellVisualDbc::GetById(uint32 ID) const
{
	const int32* Idx = IdIndex.Find(ID);
	return Idx ? &Entries[*Idx] : nullptr;
}
