#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include <initializer_list>

DEFINE_LOG_CATEGORY_STATIC(LogDbcStore, Log, All);

FDbcStore& FDbcStore::Get()
{
    static FDbcStore Instance;
    return Instance;
}

void FDbcStore::ResetForReload()
{
    *this = FDbcStore();
}

static bool TryLoadSingleDbc(FMpqManager& Mpq, const TCHAR* DbcPath, FDbcParser& OutParser)
{
    TArray<uint8> Data;
    if (!Mpq.ReadFile(DbcPath, Data))
    {
        return false;
    }
    if (!OutParser.Parse(Data))
    {
        return false;
    }
    return true;
}

static bool LoadSingleDbc(FMpqManager& Mpq, const TCHAR* DbcPath, FDbcParser& OutParser)
{
    if (!TryLoadSingleDbc(Mpq, DbcPath, OutParser))
    {
        UE_LOG(LogDbcStore, Warning, TEXT("Failed to load %s"), DbcPath);
        return false;
    }

    return true;
}

static bool LoadSingleDbcAny(FMpqManager& Mpq, std::initializer_list<const TCHAR*> Paths, FDbcParser& OutParser, FString& OutLoadedPath)
{
    for (const TCHAR* Path : Paths)
    {
        if (TryLoadSingleDbc(Mpq, Path, OutParser))
        {
            OutLoadedPath = Path;
            return true;
        }
    }

    FString Attempted;
    bool bFirst = true;
    for (const TCHAR* Path : Paths)
    {
        if (!bFirst)
        {
            Attempted += TEXT(", ");
        }
        Attempted += Path;
        bFirst = false;
    }

    UE_LOG(LogDbcStore, Warning, TEXT("Failed to load any of: %s"), *Attempted);
    return false;
}

bool FDbcStore::LoadAll(FMpqManager& Mpq)
{
    // Load Tier 1 (Essential) DBC tables for initial world display
    FDbcParser Parser;
    int32 Loaded = 0;
    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\FactionTemplate.dbc"), Parser)) Factions.Load(Parser);

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\Map.dbc"), Parser))
    {
        MapDbc.Load(Parser);
        ++Loaded;

        // Debug: print first 3 map entries
        for (int32 i = 0; i < FMath::Min(3, MapDbc.Num()); ++i)
        {
            const FMapDbcEntry& E = MapDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("Map[%d]: ID=%d Name='%s' Type=%d"), i, E.ID, *E.Name, E.MapType);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\AreaTable.dbc"), Parser))
    {
        AreaTableDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, AreaTableDbc.Num()); ++i)
        {
            const FAreaTableDbcEntry& E = AreaTableDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("Area[%d]: ID=%d MapID=%d Name='%s'"), i, E.ID, E.MapID, *E.Name);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\QuestSort.dbc"), Parser))
    {
        QuestSortDbc.Load(Parser);
        ++Loaded;
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\LFGDungeons.dbc"), Parser))
    {
        LfgDungeonsDbc.Load(Parser);
        ++Loaded;
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\Light.dbc"), Parser))
    {
        LightDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, LightDbc.Num()); ++i)
        {
            const FLightDbcEntry& E = LightDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("Light[%d]: ID=%d MapID=%d Pos=(%.1f,%.1f,%.1f)"), i, E.ID, E.MapID, E.X, E.Y, E.Z);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\LightParams.dbc"), Parser))
    {
        LightParamsDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, LightParamsDbc.Num()); ++i)
        {
            const FLightParamsDbcEntry& E = LightParamsDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("LightParams[%d]: ID=%d SkyboxID=%d Glow=%.2f"), i, E.ID, E.SkyboxID, E.Glow);
        }
    }

    FString LoadedPath;
    // WotLK stores these bands as LightIntBand.dbc; keep the wrapper name aligned with the project specs.
    if (LoadSingleDbcAny(Mpq, {TEXT("DBFilesClient\\LightIntParams.dbc"), TEXT("DBFilesClient\\LightIntBand.dbc")}, Parser, LoadedPath))
    {
        LightIntParamsDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, LightIntParamsDbc.Num()); ++i)
        {
            const FLightIntParamsDbcEntry& E = LightIntParamsDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("LightIntParams[%d]: ID=%d Entries=%d FirstTime=%d Source='%s'"),
                i, E.ID, E.EntryCount, E.Times[0], *LoadedPath);
        }
    }

    if (LoadSingleDbcAny(Mpq, {TEXT("DBFilesClient\\LightFloatParams.dbc"), TEXT("DBFilesClient\\LightFloatBand.dbc")}, Parser, LoadedPath))
    {
        LightFloatParamsDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, LightFloatParamsDbc.Num()); ++i)
        {
            const FLightFloatParamsDbcEntry& E = LightFloatParamsDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("LightFloatParams[%d]: ID=%d Entries=%d FirstTime=%d Source='%s'"),
                i, E.ID, E.EntryCount, E.Times[0], *LoadedPath);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\LiquidType.dbc"), Parser))
    {
        LiquidTypeDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, LiquidTypeDbc.Num()); ++i)
        {
            const FLiquidTypeDbcEntry& E = LiquidTypeDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("LiquidType[%d]: ID=%d Name='%s' Type=%d Spell=%d"),
                i, E.ID, *E.Name, E.Type, E.SpellID);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\AnimationData.dbc"), Parser))
    {
        AnimationDataDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, AnimationDataDbc.Num()); ++i)
        {
            const FAnimationDataDbcEntry& E = AnimationDataDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("AnimationData[%d]: ID=%d Name='%s' Flags=%d Fallback=%d"),
                i, E.ID, *E.Name, E.Flags, E.Fallback);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\ChrRaces.dbc"), Parser))
    {
        ChrRacesDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, ChrRacesDbc.Num()); ++i)
        {
            const FChrRacesDbcEntry& E = ChrRacesDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("ChrRaces[%d]: ID=%d Name='%s' Prefix='%s' Models=%d/%d"),
                i, E.ID, *E.Name, *E.ClientPrefix, E.MaleModelID, E.FemaleModelID);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\CharSections.dbc"), Parser))
    {
        CharSectionsDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, CharSectionsDbc.Num()); ++i)
        {
            const FCharSectionsDbcEntry& E = CharSectionsDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("CharSections[%d]: ID=%d Race=%d Sex=%d Type=%d Variation=%d Color=%d"),
                i, E.ID, E.RaceID, E.SexID, E.Type, E.Variation, E.Color);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\CreatureDisplayInfo.dbc"), Parser))
    {
        CreatureDisplayInfoDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, CreatureDisplayInfoDbc.Num()); ++i)
        {
            const FCreatureDisplayInfoDbcEntry& E = CreatureDisplayInfoDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("CreatureDisplayInfo[%d]: ID=%d Model=%d Scale=%.2f Tex1='%s'"),
                i, E.ID, E.ModelID, E.Scale, *E.Texture1);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\CreatureDisplayInfoExtra.dbc"), Parser))
    {
        CreatureDisplayInfoExtraDbc.Load(Parser);
        ++Loaded;
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\CreatureModelData.dbc"), Parser))
    {
        CreatureModelDataDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, CreatureModelDataDbc.Num()); ++i)
        {
            const FCreatureModelDataDbcEntry& E = CreatureModelDataDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("CreatureModelData[%d]: ID=%d Path='%s' SizeClass=%d Scale=%.2f"),
                i, E.ID, *E.ModelPath, E.SizeClass, E.Scale);
        }
    }

	// GameObject visuals are resolved client-side from the display ID carried by
	// UPDATE_OBJECT. This is essential world data (doors, mailboxes, fires,
	// transports), not deferred UI data.
	if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\GameObjectDisplayInfo.dbc"), Parser))
	{
		GameObjectDisplayInfoDbc.Load(Parser);
		++Loaded;

		for (int32 Index = 0; Index < FMath::Min(3, GameObjectDisplayInfoDbc.Num()); ++Index)
		{
			const FGameObjectDisplayInfoDbcEntry& Entry = GameObjectDisplayInfoDbc.GetAll()[Index];
			UE_LOG(LogDbcStore, Verbose,
				TEXT("GameObjectDisplayInfo[%d]: ID=%u Model='%s' Bounds=%s..%s"),
				Index, Entry.ID, *Entry.ModelPath,
				*Entry.BoundsMin.ToCompactString(), *Entry.BoundsMax.ToCompactString());
		}
	}

    // Ground effects are required while initial ADT tiles are built, so these
    // tables must be ready before asynchronous terrain loading begins.
    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\GroundEffectTexture.dbc"), Parser))
    {
        GroundEffectTextureDbc.Load(Parser);
        ++Loaded;
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\GroundEffectDoodad.dbc"), Parser))
    {
        GroundEffectDoodadDbc.Load(Parser);
        ++Loaded;
    }

    bLoaded = Loaded > 0;
    UE_LOG(LogDbcStore, Log, TEXT("DbcStore: loaded %d/18 essential DBC tables (Tier 1)"), Loaded);
    return bLoaded;
}

void FDbcStore::LoadDeferred(FMpqManager& Mpq)
{
    // Load Tier 2 (Deferred) DBC tables that can wait
    FDbcParser Parser;
    int32 Loaded = 0;

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\Item.dbc"), Parser))
    {
        ItemDbc.Load(Parser);
        ++Loaded;
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\ItemDisplayInfo.dbc"), Parser))
    {
        ItemDisplayInfoDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, ItemDisplayInfoDbc.Num()); ++i)
        {
            const FItemDisplayInfoDbcEntry& E = ItemDisplayInfoDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("ItemDisplayInfo[%d]: ID=%d Icon='%s' ModelL='%s' ModelR='%s'"),
                i, E.ID, *E.Icon1, *E.ModelNames[0], *E.ModelNames[1]);
        }
    }

    EnchantmentNames.Reset();
    EnchantmentEffectTable.Reset();
    if (LoadSingleDbc(Mpq,TEXT("DBFilesClient\\SpellItemEnchantment.dbc"),Parser) && Parser.GetFieldCount() >= 38)
        for (uint32 Row=0; Row<Parser.GetRecordCount(); ++Row)
        {
            FString Name = Parser.GetLocalizedString(Row,14);
            for (uint32 Locale=14; Locale<30 && Name.IsEmpty(); ++Locale) Name=Parser.GetString(Row,Locale);
            EnchantmentNames.Add(Parser.GetUInt(Row,0),Name);
            // 3.3.5 layout: ID, Charges, Effect[3] (2-4), EffectPointsMin[3] (5-7), EffectPointsMax[3], EffectArg[3] (11-13).
            FEnchantmentEffects& Effects = EnchantmentEffectTable.Add(Parser.GetUInt(Row,0));
            for (uint32 I=0; I<3; ++I)
            {
                Effects.Type[I] = Parser.GetUInt(Row,2+I);
                Effects.Amount[I] = static_cast<int32>(Parser.GetUInt(Row,5+I));
                Effects.Arg[I] = Parser.GetUInt(Row,11+I);
            }
        }
    // Random "of the Bear" stats on item instances (ITEM_FIELD_RANDOM_PROPERTIES_ID).
    RandomPropertyTable.Reset();
    RandomSuffixTable.Reset();
    // ItemRandomProperties.dbc: ID, InternalName, Enchantment[5] (2-6), Name[17] (7-23).
    if (TryLoadSingleDbc(Mpq,TEXT("DBFilesClient\\ItemRandomProperties.dbc"),Parser) && Parser.GetFieldCount() >= 8)
        for (uint32 Row=0; Row<Parser.GetRecordCount(); ++Row)
        {
            FItemRandomEnchants& Entry = RandomPropertyTable.Add(Parser.GetUInt(Row,0));
            for (uint32 I=0; I<5; ++I) Entry.Enchant[I] = Parser.GetUInt(Row,2+I);
            Entry.Name = Parser.GetLocalizedString(Row,7);
        }
    // ItemRandomSuffix.dbc: ID, Name[17] (1-17), InternalName (18), Enchantment[5] (19-23), AllocationPct[5] (24-28).
    if (TryLoadSingleDbc(Mpq,TEXT("DBFilesClient\\ItemRandomSuffix.dbc"),Parser) && Parser.GetFieldCount() >= 29)
        for (uint32 Row=0; Row<Parser.GetRecordCount(); ++Row)
        {
            FItemRandomEnchants& Entry = RandomSuffixTable.Add(Parser.GetUInt(Row,0));
            Entry.Name = Parser.GetLocalizedString(Row,1);
            for (uint32 I=0; I<5; ++I) { Entry.Enchant[I] = Parser.GetUInt(Row,19+I); Entry.AllocationPct[I] = Parser.GetUInt(Row,24+I); }
        }
    // ScalingStatDistribution.dbc: ID, StatID[10] (1-10, -1 = none), Bonus[10] (11-20), MaxLevel (21).
    ScalingDistributionTable.Reset();
    if (TryLoadSingleDbc(Mpq,TEXT("DBFilesClient\\ScalingStatDistribution.dbc"),Parser) && Parser.GetFieldCount() >= 22)
        for (uint32 Row=0; Row<Parser.GetRecordCount(); ++Row)
        {
            FScalingStatDistribution& Entry = ScalingDistributionTable.Add(Parser.GetUInt(Row,0));
            for (uint32 I=0; I<10; ++I) { Entry.StatId[I] = static_cast<int32>(Parser.GetUInt(Row,1+I)); Entry.Bonus[I] = Parser.GetUInt(Row,11+I); }
            Entry.MaxLevel = Parser.GetUInt(Row,21);
        }
    // ScalingStatValues.dbc: ID, Charlevel, ShoulderBudget/TrinketBudget/WeaponBudget1H/RangedBudget (2-5),
    // ClothShoulder/LeatherShoulder/MailShoulder/PlateShoulder armor (6-9), DPS 1H/2H/caster 1H/2H/ranged/wand (10-15),
    // SpellPower (16), PrimaryBudget (17), TertiaryBudget (18), cloth/leather/mail/plate cloak... armor (19-23).
    ScalingValuesByLevel.Reset();
    if (TryLoadSingleDbc(Mpq,TEXT("DBFilesClient\\ScalingStatValues.dbc"),Parser) && Parser.GetFieldCount() >= 24)
        for (uint32 Row=0; Row<Parser.GetRecordCount(); ++Row)
        {
            FScalingStatValues& Entry = ScalingValuesByLevel.Add(Parser.GetUInt(Row,1));
            for (uint32 I=0; I<4; ++I) { Entry.SsdMultiplier[I] = Parser.GetUInt(Row,2+I); Entry.ArmorMod[I] = Parser.GetUInt(Row,6+I); }
            for (uint32 I=0; I<6; ++I) Entry.DpsMod[I] = Parser.GetUInt(Row,10+I);
            Entry.SpellPower = Parser.GetUInt(Row,16);
            Entry.SsdMultiplier2 = Parser.GetUInt(Row,17);
            Entry.SsdMultiplier3 = Parser.GetUInt(Row,18);
            for (uint32 I=0; I<5; ++I) Entry.ArmorMod2[I] = Parser.GetUInt(Row,19+I);
        }
    // Achievement.dbc (3.3.5): ID, Faction (-1 both, 0 Horde, 1 Alliance), Map, Parent, Name[17] (4-20),
    // Description[17] (21-37), Category (38), Points (39), OrderInCategory (40), Flags (41), Icon (42), Reward[17] (43-59).
    AchievementTable.Reset();
    if (TryLoadSingleDbc(Mpq,TEXT("DBFilesClient\\Achievement.dbc"),Parser) && Parser.GetFieldCount() >= 60)
        for (uint32 Row=0; Row<Parser.GetRecordCount(); ++Row)
        {
            FAchievement& Entry = AchievementTable.Add(Parser.GetUInt(Row,0));
            Entry.Id = Parser.GetUInt(Row,0);
            Entry.Faction = static_cast<int32>(Parser.GetUInt(Row,1));
            Entry.Name = Parser.GetLocalizedString(Row,4);
            Entry.Description = Parser.GetLocalizedString(Row,21);
            Entry.Category = Parser.GetUInt(Row,38);
            Entry.Points = Parser.GetUInt(Row,39);
            Entry.Order = Parser.GetUInt(Row,40);
            Entry.Flags = Parser.GetUInt(Row,41);
            Entry.Icon = Parser.GetUInt(Row,42);
            Entry.Reward = Parser.GetLocalizedString(Row,43);
        }
    // Achievement_Category.dbc: ID, Parent (-1 = top level), Name[17] (2-18), SortOrder (19).
    AchievementCategoryTable.Reset();
    if (TryLoadSingleDbc(Mpq,TEXT("DBFilesClient\\Achievement_Category.dbc"),Parser) && Parser.GetFieldCount() >= 20)
        for (uint32 Row=0; Row<Parser.GetRecordCount(); ++Row)
        {
            FAchievementCategory& Entry = AchievementCategoryTable.Add(Parser.GetUInt(Row,0));
            Entry.Id = Parser.GetUInt(Row,0);
            Entry.Parent = static_cast<int32>(Parser.GetUInt(Row,1));
            Entry.Name = Parser.GetLocalizedString(Row,2);
            Entry.Order = Parser.GetUInt(Row,19);
        }
    UE_LOG(LogDbcStore, Log, TEXT("Achievements: %d in %d categories"), AchievementTable.Num(), AchievementCategoryTable.Num());
    UE_LOG(LogDbcStore, Log, TEXT("Item enchantments: %d, random properties: %d, random suffixes: %d, scaling distributions: %d, scaling levels: %d"),
        EnchantmentEffectTable.Num(), RandomPropertyTable.Num(), RandomSuffixTable.Num(), ScalingDistributionTable.Num(), ScalingValuesByLevel.Num());

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\Spell.dbc"), Parser))
    {
        SpellDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, SpellDbc.Num()); ++i)
        {
            const FSpellDbcEntry& E = SpellDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("Spell[%d]: ID=%d Name='%s' School=%d Level=%d"),
                i, E.ID, *E.SpellName, E.SchoolMask, E.SpellLevel);
        }
    }
    if (LoadSingleDbc(Mpq,TEXT("DBFilesClient\\SpellShapeshiftForm.dbc"),Parser)) SpellDbc.LoadForms(Parser);
    if (LoadSingleDbc(Mpq,TEXT("DBFilesClient\\SkillLine.dbc"),Parser)) SkillLineDbc.LoadLines(Parser);
    if (LoadSingleDbc(Mpq,TEXT("DBFilesClient\\SkillLineAbility.dbc"),Parser)) SkillLineDbc.LoadAbilities(Parser);
    if(LoadSingleDbc(Mpq,TEXT("DBFilesClient\\SpellDuration.dbc"),Parser)) SpellDbc.LoadTiming(Parser,0);
    if(LoadSingleDbc(Mpq,TEXT("DBFilesClient\\SpellCastTimes.dbc"),Parser)) SpellDbc.LoadTiming(Parser,1);
    if(LoadSingleDbc(Mpq,TEXT("DBFilesClient\\SpellRange.dbc"),Parser)) SpellDbc.LoadTiming(Parser,2);
    if(LoadSingleDbc(Mpq,TEXT("DBFilesClient\\SpellRadius.dbc"),Parser)) SpellDbc.LoadTiming(Parser,3);
    if(LoadSingleDbc(Mpq,TEXT("DBFilesClient\\Lock.dbc"),Parser)) SpellDbc.LoadLocks(Parser);


    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\SpellIcon.dbc"), Parser))
    {
        SpellIconDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, SpellIconDbc.Num()); ++i)
        {
            const FSpellIconDbcEntry& E = SpellIconDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("SpellIcon[%d]: ID=%d Path='%s'"),
                i, E.ID, *E.TexturePath);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\SpellVisual.dbc"), Parser))
    {
        SpellVisualDbc.Load(Parser);
        ++Loaded;

        UE_LOG(LogDbcStore, Verbose, TEXT("SpellVisual: %d records"), SpellVisualDbc.Num());
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\SpellVisualKit.dbc"), Parser))
    {
        SpellVisualKitDbc.Load(Parser);
        ++Loaded;

        UE_LOG(LogDbcStore, Verbose, TEXT("SpellVisualKit: %d records"), SpellVisualKitDbc.Num());
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\SpellVisualEffectName.dbc"), Parser))
    {
        SpellVisualEffectNameDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, SpellVisualEffectNameDbc.Num()); ++i)
        {
            const FSpellVisualEffectNameDbcEntry& E = SpellVisualEffectNameDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("SpellVisualEffectName[%d]: ID=%d Name='%s' FilePath='%s' AreaEffectSize=%.2f Scale=%.2f"),
                i, E.ID, *E.Name, *E.FilePath, E.AreaEffectSize, E.Scale);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\SoundEntries.dbc"), Parser))
    {
        SoundEntriesDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, SoundEntriesDbc.Num()); ++i)
        {
            const FSoundEntriesDbcEntry& E = SoundEntriesDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("SoundEntries[%d]: ID=%d Name='%s' Type=%d"),
                i, E.ID, *E.Name, E.SoundType);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\LoadingScreens.dbc"), Parser))
    {
        LoadingScreensDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, LoadingScreensDbc.Num()); ++i)
        {
            const FLoadingScreensDbcEntry& E = LoadingScreensDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("LoadingScreens[%d]: ID=%d Name='%s' File='%s'"),
                i, E.ID, *E.Name, *E.FileName);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\Emotes.dbc"), Parser))
        EmotesDbc.Load(Parser);
    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\EmotesText.dbc"), Parser))
    {
        EmotesTextDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, EmotesTextDbc.Num()); ++i)
        {
            const FEmotesTextDbcEntry& E = EmotesTextDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("EmotesText[%d]: ID=%d Name='%s' EmoteID=%d"),
                i, E.ID, *E.Name, E.EmoteID);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\Talent.dbc"), Parser))
    {
        TalentDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, TalentDbc.Num()); ++i)
        {
            const FTalentDbcEntry& E = TalentDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("Talent[%d]: ID=%d Tab=%d Row=%d Col=%d Rank0=%d"),
                i, E.TalentID, E.TalentTab, E.Row, E.Col, E.RankID[0]);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\TalentTab.dbc"), Parser))
    {
        TalentTabDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, TalentTabDbc.Num()); ++i)
        {
            const FTalentTabDbcEntry& E = TalentTabDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("TalentTab[%d]: ID=%d Name='%s' ClassMask=%d Page=%d"),
                i, E.TalentTabID, *E.Name, E.ClassMask, E.TabPage);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\ZoneMusic.dbc"), Parser))
    {
        ZoneMusicDbc.Load(Parser);
        ++Loaded;
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\SoundAmbience.dbc"), Parser))
    {
        SoundAmbienceDbc.Load(Parser);
        ++Loaded;
    }

    // r736 (Claude-UI flagged these on TormentWoW 23.10; they are in every log, Thera too): CharComponentTextureLayouts.dbc
    // and CharComponentTextureSections.dbc only exist from Cataclysm on. 3.3.5 data never has them, and WowCharacterTexture
    // then uses its built-in 512x512 layout and regions, so their absence is a note, not a warning.
    if (TryLoadSingleDbc(Mpq, TEXT("DBFilesClient\\CharComponentTextureLayouts.dbc"), Parser))
    {
        CharComponentTextureLayoutsDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, CharComponentTextureLayoutsDbc.Num()); ++i)
        {
            const FCharComponentTextureLayoutsDbcEntry& E = CharComponentTextureLayoutsDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("CharComponentTextureLayouts[%d]: ID=%d Size=%dx%d"),
                i, E.ID, E.Width, E.Height);
        }
    }

    if (TryLoadSingleDbc(Mpq, TEXT("DBFilesClient\\CharComponentTextureSections.dbc"), Parser))
    {
        CharComponentTextureSectionsDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, CharComponentTextureSectionsDbc.Num()); ++i)
        {
            const FCharComponentTextureSectionsDbcEntry& E = CharComponentTextureSectionsDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("CharComponentTextureSections[%d]: ID=%d LayoutID=%d Type=%d Rect=(%d,%d,%dx%d)"),
                i, E.ID, E.CharComponentTextureLayoutID, E.SectionType, E.X, E.Y, E.Width, E.Height);
        }
    }

    if (CharComponentTextureLayoutsDbc.Num() == 0 || CharComponentTextureSectionsDbc.Num() == 0)
    {
        UE_LOG(LogDbcStore, Log, TEXT("CharComponentTextureLayouts/Sections.dbc not in this data (Cataclysm and later only): character textures use the built-in 3.3.5 layout"));
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\CharHairGeosets.dbc"), Parser))
    {
        CharHairGeosetsDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, CharHairGeosetsDbc.Num()); ++i)
        {
            const FCharHairGeosetsDbcEntry& E = CharHairGeosetsDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("CharHairGeosets[%d]: ID=%d Race=%d Sex=%d Variation=%d GeosetID=%d"),
                i, E.ID, E.RaceID, E.SexID, E.VariationID, E.GeosetID);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\CharacterFacialHairStyles.dbc"), Parser))
    {
        CharacterFacialHairStylesDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, CharacterFacialHairStylesDbc.Num()); ++i)
        {
            const FCharacterFacialHairStylesDbcEntry& E = CharacterFacialHairStylesDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("CharacterFacialHairStyles[%d]: Race=%d Sex=%d Variation=%d Geosets=[%d,%d,%d,%d,%d]"),
                i, E.RaceID, E.SexID, E.VariationID, E.Geosets[0], E.Geosets[1], E.Geosets[2], E.Geosets[3], E.Geosets[4]);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\CharStartOutfit.dbc"), Parser))
    {
        CharStartOutfitDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, CharStartOutfitDbc.Num()); ++i)
        {
            const FCharStartOutfitDbcEntry& E = CharStartOutfitDbc.GetAll()[i];
            int32 ItemCount = 0;
            for (int32 j = 0; j < 24; ++j)
            {
                if (E.ItemDisplayIds[j] > 0) ItemCount++;
            }
            UE_LOG(LogDbcStore, Verbose, TEXT("CharStartOutfit[%d]: ID=%d Race=%d Class=%d Gender=%d Items=%d"),
                i, E.ID, E.RaceID, E.ClassID, E.GenderID, ItemCount);
        }
    }

    if (LoadSingleDbc(Mpq, TEXT("DBFilesClient\\TaxiNodes.dbc"), Parser))
    {
        TaxiNodesDbc.Load(Parser);
        ++Loaded;

        for (int32 i = 0; i < FMath::Min(3, TaxiNodesDbc.Num()); ++i)
        {
            const FTaxiNodesDbcEntry& E = TaxiNodesDbc.GetAll()[i];
            UE_LOG(LogDbcStore, Verbose, TEXT("TaxiNode[%d]: ID=%d MapID=%d Pos=(%.1f,%.1f,%.1f) Name='%s'"),
                i, E.ID, E.MapID, E.X, E.Y, E.Z, *E.Name);
        }
    }

    bDeferredLoaded = true;
    UE_LOG(LogDbcStore, Log, TEXT("DbcStore: loaded %d/20 deferred DBC tables (Tier 2)"), Loaded);
}
