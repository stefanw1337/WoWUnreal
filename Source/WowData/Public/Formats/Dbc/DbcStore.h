#pragma once
#include "CoreMinimal.h"
#include "Formats/Dbc/SkillLineDbc.h"
#include "Formats/Dbc/FactionTemplateDbc.h"
#include "Formats/Dbc/MapDbc.h"
#include "Formats/Dbc/AreaTableDbc.h"
#include "Formats/Dbc/LightDbc.h"
#include "Formats/Dbc/LightParamsDbc.h"
#include "Formats/Dbc/LightIntParamsDbc.h"
#include "Formats/Dbc/LiquidTypeDbc.h"
#include "Formats/Dbc/AnimationDataDbc.h"
#include "Formats/Dbc/ChrRacesDbc.h"
#include "Formats/Dbc/CharSectionsDbc.h"
#include "Formats/Dbc/CreatureDisplayInfoDbc.h"
#include "Formats/Dbc/CreatureDisplayInfoExtraDbc.h"
#include "Formats/Dbc/CreatureModelDataDbc.h"
#include "Formats/Dbc/GameObjectDisplayInfoDbc.h"
#include "Formats/Dbc/ItemDisplayInfoDbc.h"
#include "Formats/Dbc/ItemDbc.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Formats/Dbc/SpellVisualDbc.h"
#include "Formats/Dbc/SpellVisualKitDbc.h"
#include "Formats/Dbc/SpellVisualEffectNameDbc.h"
#include "Formats/Dbc/SoundEntriesDbc.h"
#include "Formats/Dbc/LoadingScreensDbc.h"
#include "Formats/Dbc/GroundEffectTextureDbc.h"
#include "Formats/Dbc/GroundEffectDoodadDbc.h"
#include "Formats/Dbc/EmotesTextDbc.h"
#include "Formats/Dbc/EmotesDbc.h"
#include "Formats/Dbc/TalentDbc.h"
#include "Formats/Dbc/LightFloatParamsDbc.h"
#include "Formats/Dbc/TalentTabDbc.h"
#include "Formats/Dbc/ZoneMusicDbc.h"
#include "Formats/Dbc/SoundAmbienceDbc.h"
#include "Formats/Dbc/CharComponentTextureLayoutsDbc.h"
#include "Formats/Dbc/CharComponentTextureSectionsDbc.h"
#include "Formats/Dbc/CharHairGeosetsDbc.h"
#include "Formats/Dbc/CharacterFacialHairStylesDbc.h"
#include "Formats/Dbc/CharStartOutfitDbc.h"
#include "Formats/Dbc/TaxiNodesDbc.h"
#include "Formats/Dbc/QuestSortDbc.h"
#include "Formats/Dbc/LfgDungeonsDbc.h"

class FMpqManager;

class WOWDATA_API FDbcStore
{
public:
    static FDbcStore& Get();
    FString EnchantmentName(uint32 Id) const { return EnchantmentNames.FindRef(Id); }
    /** SpellItemEnchantment effects: type (1 proc, 2 damage, 3 equip spell, 4 resistance, 5 stat ...), amount, argument. */
    struct FEnchantmentEffects { uint32 Type[3] = {}; int32 Amount[3] = {}; uint32 Arg[3] = {}; };
    const FEnchantmentEffects* EnchantmentEffects(uint32 Id) const { return EnchantmentEffectTable.Find(Id); }
    /** ItemRandomProperties (positive random property id) and ItemRandomSuffix (negative id). */
    struct FItemRandomEnchants { FString Name; uint32 Enchant[5] = {}; uint32 AllocationPct[5] = {}; };
    const FItemRandomEnchants* RandomProperty(uint32 Id) const { return RandomPropertyTable.Find(Id); }
    const FItemRandomEnchants* RandomSuffix(uint32 Id) const { return RandomSuffixTable.Find(Id); }
    /** Heirloom scaling: ScalingStatDistribution (stat types, per-stat bonus, max level) and ScalingStatValues per level. */
    struct FScalingStatDistribution { int32 StatId[10] = {}; uint32 Bonus[10] = {}; uint32 MaxLevel = 0; };
    struct FScalingStatValues { uint32 SsdMultiplier[4] = {}; uint32 ArmorMod[4] = {}; uint32 DpsMod[6] = {}; uint32 SpellPower = 0;
        uint32 SsdMultiplier2 = 0, SsdMultiplier3 = 0; uint32 ArmorMod2[5] = {}; };
    const FScalingStatDistribution* ScalingDistribution(uint32 Id) const { return ScalingDistributionTable.Find(Id); }
    const FScalingStatValues* ScalingValues(uint32 Level) const { return ScalingValuesByLevel.Find(Level); }
    /** Achievement.dbc / Achievement_Category.dbc (names, points, categories, icons). */
    struct FAchievement { uint32 Id = 0; int32 Faction = -1; FString Name, Description, Reward; uint32 Category = 0, Points = 0, Order = 0, Flags = 0, Icon = 0; };
    struct FAchievementCategory { uint32 Id = 0; int32 Parent = -1; FString Name; uint32 Order = 0; };
    const TMap<uint32, FAchievement>& Achievements() const { return AchievementTable; }
    const TMap<uint32, FAchievementCategory>& AchievementCategories() const { return AchievementCategoryTable; }

    bool LoadAll(FMpqManager& Mpq);
    void LoadDeferred(FMpqManager& Mpq);

    FMapDbc& Maps() { return MapDbc; }
    FAreaTableDbc& AreaTable() { return AreaTableDbc; }
    FLightDbc& Lights() { return LightDbc; }
    FLightParamsDbc& LightParams() { return LightParamsDbc; }
    FLightIntParamsDbc& LightIntParams() { return LightIntParamsDbc; }
    FLightFloatParamsDbc& LightFloatParams() { return LightFloatParamsDbc; }
    FLiquidTypeDbc& LiquidTypes() { return LiquidTypeDbc; }
    FAnimationDataDbc& AnimationData() { return AnimationDataDbc; }
    FChrRacesDbc& ChrRaces() { return ChrRacesDbc; }
    FCharSectionsDbc& CharSections() { return CharSectionsDbc; }
    FCreatureDisplayInfoDbc& CreatureDisplayInfo() { return CreatureDisplayInfoDbc; }
    FCreatureDisplayInfoExtraDbc& CreatureDisplayInfoExtra() { return CreatureDisplayInfoExtraDbc; }
    FCreatureModelDataDbc& CreatureModelData() { return CreatureModelDataDbc; }
    FGameObjectDisplayInfoDbc& GameObjectDisplayInfo() { return GameObjectDisplayInfoDbc; }
    FItemDisplayInfoDbc& ItemDisplayInfo() { return ItemDisplayInfoDbc; }
    FItemDbc& Items() { return ItemDbc; }
    FSkillLineDbc& SkillLines() { return SkillLineDbc; }
    FSpellDbc& Spells() { return SpellDbc; }
    FSpellIconDbc& SpellIcons() { return SpellIconDbc; }
    FSpellVisualDbc& SpellVisuals() { return SpellVisualDbc; }
    FSpellVisualKitDbc& SpellVisualKits() { return SpellVisualKitDbc; }
    FSpellVisualEffectNameDbc& SpellVisualEffectNames() { return SpellVisualEffectNameDbc; }
    FSoundEntriesDbc& SoundEntries() { return SoundEntriesDbc; }
    FLoadingScreensDbc& LoadingScreens() { return LoadingScreensDbc; }
    FGroundEffectTextureDbc& GroundEffectTextures() { return GroundEffectTextureDbc; }
    FGroundEffectDoodadDbc& GroundEffectDoodads() { return GroundEffectDoodadDbc; }
    FEmotesDbc& Emotes() { return EmotesDbc; }
    FEmotesTextDbc& EmotesText() { return EmotesTextDbc; }
    FTalentDbc& Talents() { return TalentDbc; }
    FTalentTabDbc& TalentTabs() { return TalentTabDbc; }
    FZoneMusicDbc& ZoneMusic() { return ZoneMusicDbc; }
    FSoundAmbienceDbc& SoundAmbience() { return SoundAmbienceDbc; }
    FCharComponentTextureLayoutsDbc& CharComponentTextureLayouts() { return CharComponentTextureLayoutsDbc; }
    FCharComponentTextureSectionsDbc& CharComponentTextureSections() { return CharComponentTextureSectionsDbc; }
    FCharHairGeosetsDbc& CharHairGeosets() { return CharHairGeosetsDbc; }
    FCharacterFacialHairStylesDbc& CharacterFacialHairStyles() { return CharacterFacialHairStylesDbc; }
    FCharStartOutfitDbc& CharStartOutfit() { return CharStartOutfitDbc; }
    FTaxiNodesDbc& TaxiNodes() { return TaxiNodesDbc; }
    FQuestSortDbc& QuestSorts() { return QuestSortDbc; }
    FLfgDungeonsDbc& LfgDungeons() { return LfgDungeonsDbc; }

    const FMapDbc& Maps() const { return MapDbc; }
    const FAreaTableDbc& AreaTable() const { return AreaTableDbc; }
    const FLightDbc& Lights() const { return LightDbc; }
    const FLightParamsDbc& LightParams() const { return LightParamsDbc; }
    const FLightIntParamsDbc& LightIntParams() const { return LightIntParamsDbc; }
    const FLightFloatParamsDbc& LightFloatParams() const { return LightFloatParamsDbc; }
    const FLiquidTypeDbc& LiquidTypes() const { return LiquidTypeDbc; }
    const FAnimationDataDbc& AnimationData() const { return AnimationDataDbc; }
    const FChrRacesDbc& ChrRaces() const { return ChrRacesDbc; }
    const FCharSectionsDbc& CharSections() const { return CharSectionsDbc; }
    const FCreatureDisplayInfoDbc& CreatureDisplayInfo() const { return CreatureDisplayInfoDbc; }
    const FCreatureDisplayInfoExtraDbc& CreatureDisplayInfoExtra() const { return CreatureDisplayInfoExtraDbc; }
    const FCreatureModelDataDbc& CreatureModelData() const { return CreatureModelDataDbc; }
    const FGameObjectDisplayInfoDbc& GameObjectDisplayInfo() const { return GameObjectDisplayInfoDbc; }
    const FItemDisplayInfoDbc& ItemDisplayInfo() const { return ItemDisplayInfoDbc; }
    const FItemDbc& Items() const { return ItemDbc; }
    const FSpellDbc& Spells() const { return SpellDbc; }
    const FSpellIconDbc& SpellIcons() const { return SpellIconDbc; }
    const FSpellVisualDbc& SpellVisuals() const { return SpellVisualDbc; }
    const FSpellVisualKitDbc& SpellVisualKits() const { return SpellVisualKitDbc; }
    const FSpellVisualEffectNameDbc& SpellVisualEffectNames() const { return SpellVisualEffectNameDbc; }
    const FSoundEntriesDbc& SoundEntries() const { return SoundEntriesDbc; }
    const FLoadingScreensDbc& LoadingScreens() const { return LoadingScreensDbc; }
    const FGroundEffectTextureDbc& GroundEffectTextures() const { return GroundEffectTextureDbc; }
    const FGroundEffectDoodadDbc& GroundEffectDoodads() const { return GroundEffectDoodadDbc; }
    const FEmotesDbc& Emotes() const { return EmotesDbc; }
    const FEmotesTextDbc& EmotesText() const { return EmotesTextDbc; }
    const FTalentDbc& Talents() const { return TalentDbc; }
    const FTalentTabDbc& TalentTabs() const { return TalentTabDbc; }
    const FZoneMusicDbc& ZoneMusic() const { return ZoneMusicDbc; }
    const FSoundAmbienceDbc& SoundAmbience() const { return SoundAmbienceDbc; }
    const FCharComponentTextureLayoutsDbc& CharComponentTextureLayouts() const { return CharComponentTextureLayoutsDbc; }
    const FCharComponentTextureSectionsDbc& CharComponentTextureSections() const { return CharComponentTextureSectionsDbc; }
    const FCharHairGeosetsDbc& CharHairGeosets() const { return CharHairGeosetsDbc; }
    const FCharacterFacialHairStylesDbc& CharacterFacialHairStyles() const { return CharacterFacialHairStylesDbc; }
    const FCharStartOutfitDbc& CharStartOutfit() const { return CharStartOutfitDbc; }
    const FTaxiNodesDbc& TaxiNodes() const { return TaxiNodesDbc; }
    const FQuestSortDbc& QuestSorts() const { return QuestSortDbc; }
    const FLfgDungeonsDbc& LfgDungeons() const { return LfgDungeonsDbc; }

    const FFactionTemplateDbc& FactionTemplates() const { return Factions; }

    bool IsLoaded() const { return bLoaded; }
    bool IsDeferredLoaded() const { return bDeferredLoaded; }
    /** Drop every table (some loaders append), before loading another game-data folder. */
    void ResetForReload();

private:
    FFactionTemplateDbc Factions;
    TMap<uint32,FString> EnchantmentNames;
    TMap<uint32,FEnchantmentEffects> EnchantmentEffectTable;
    TMap<uint32,FItemRandomEnchants> RandomPropertyTable, RandomSuffixTable;
    TMap<uint32,FScalingStatDistribution> ScalingDistributionTable;
    TMap<uint32,FScalingStatValues> ScalingValuesByLevel;
    TMap<uint32,FAchievement> AchievementTable;
    TMap<uint32,FAchievementCategory> AchievementCategoryTable;
    FMapDbc MapDbc;
    FAreaTableDbc AreaTableDbc;
    FLightDbc LightDbc;
    FLightParamsDbc LightParamsDbc;
    FLightIntParamsDbc LightIntParamsDbc;
    FLightFloatParamsDbc LightFloatParamsDbc;
    FLiquidTypeDbc LiquidTypeDbc;
    FAnimationDataDbc AnimationDataDbc;
    FChrRacesDbc ChrRacesDbc;
    FCharSectionsDbc CharSectionsDbc;
    FCreatureDisplayInfoDbc CreatureDisplayInfoDbc;
    FCreatureDisplayInfoExtraDbc CreatureDisplayInfoExtraDbc;
    FCreatureModelDataDbc CreatureModelDataDbc;
    FGameObjectDisplayInfoDbc GameObjectDisplayInfoDbc;
    FItemDisplayInfoDbc ItemDisplayInfoDbc;
    FItemDbc ItemDbc;
    FSpellDbc SpellDbc;
    FSkillLineDbc SkillLineDbc;
    FSpellIconDbc SpellIconDbc;
    FSpellVisualDbc SpellVisualDbc;
    FSpellVisualKitDbc SpellVisualKitDbc;
    FSpellVisualEffectNameDbc SpellVisualEffectNameDbc;
    FSoundEntriesDbc SoundEntriesDbc;
    FLoadingScreensDbc LoadingScreensDbc;
    FGroundEffectTextureDbc GroundEffectTextureDbc;
    FGroundEffectDoodadDbc GroundEffectDoodadDbc;
    FEmotesTextDbc EmotesTextDbc;
    FEmotesDbc EmotesDbc;
    FTalentDbc TalentDbc;
    FTalentTabDbc TalentTabDbc;
    FZoneMusicDbc ZoneMusicDbc;
    FSoundAmbienceDbc SoundAmbienceDbc;
    FCharComponentTextureLayoutsDbc CharComponentTextureLayoutsDbc;
    FCharComponentTextureSectionsDbc CharComponentTextureSectionsDbc;
    FCharHairGeosetsDbc CharHairGeosetsDbc;
    FCharacterFacialHairStylesDbc CharacterFacialHairStylesDbc;
    FCharStartOutfitDbc CharStartOutfitDbc;
    FTaxiNodesDbc TaxiNodesDbc;
    FQuestSortDbc QuestSortDbc;
    FLfgDungeonsDbc LfgDungeonsDbc;
    bool bLoaded = false;
    bool bDeferredLoaded = false;
};
