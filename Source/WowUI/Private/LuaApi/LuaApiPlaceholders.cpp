// Every global function the stock 3.3.5 interface (FrameXML + Blizzard AddOns, loaded from the
// MPQs) calls that the client does not implement yet (Saved\Reference\MissingLuaApi.txt, 11.10).
// Each is registered LAST and only when nothing else defined it, as a placeholder: counts and
// amounts return 0, cooldowns 0, 0, 0, everything else returns nothing (nil/false) and does nothing. So no stock
// window or addon stops with "attempt to call global ... (a nil value)", and the first call of
// each placeholder is logged ("Lua API placeholder called: Name (file:line)") to show which ones
// to implement next. Real implementations go in the other LuaApi files and replace these.
#include "LuaApiRegistry.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#define HAS_LUA 1
#else
#define HAS_LUA 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowLuaPlaceholder, Log, All);

#if HAS_LUA
namespace
{
struct FPlaceholderApi { const char* Name; int Kind; }; // Kind 1: returns 0; 2: cooldowns, returns 0, 0, 0

const FPlaceholderApi PlaceholderApis[] = {
    // r259 (Stefan 12.10: right-click > Whisper left the chat on "Say:"): BNet.lua (Real ID) asks
    // GetAutoCompletePresenceID(name) first; it was missing, the error stopped the whisper setup.
    // No Battle.net here: nil = "not a Real ID friend", so it is a normal whisper.
    {"GetAutoCompletePresenceID",0}, {"BNIsSelf",0}, {"BNIsFriend",0}, {"BNGetToonInfo",0},
    {"BNGetFriendToonInfo",0}, {"BNGetNumFriendToons",1}, {"BNGetFriendIndex",0}, {"BNIsBlocked",0},
    {"BNIsToonBlocked",0},
    {"AbandonQuest",0}, {"AbandonSkill",0}, {"AcceptArenaTeam",0}, {"AcceptBattlefieldPort",0},
    {"AcceptLFGMatch",0}, {"AcceptLevelGrant",0}, {"AcceptQuest",0}, {"AcceptSkillUps",0},
    {"AcceptSockets",0}, {"AddChatWindowChannel",0}, {"AddChatWindowMessages",0}, {"AddMute",0},
    {"AddOrDelMute",0}, {"AddQuestWatch",0}, {"AddSendMailCOD",0}, {"AddSendMailMoney",0}, {"AddSkillUp",0},
    {"AddTradeMoney",0}, {"ApplyBarberShopStyle",0}, {"ArenaTeamDisband",0}, {"ArenaTeamInviteByName",0},
    {"ArenaTeamLeave",0}, {"ArenaTeamSetLeaderByName",0}, {"ArenaTeamUninviteByName",0},
    {"AutoLootMailItem",0}, {"AutoStoreGuildBankItem",0}, {"BankButtonIDToInvSlotID",0},
    {"BattlefieldMgrEntryInviteResponse",0}, {"BattlefieldMgrExitRequest",0},
    {"BattlefieldMgrQueueInviteResponse",0}, {"BindEnchant",0}, {"BuyGuildBankTab",0}, {"BuyGuildCharter",0},
    {"BuyMerchantItem",0}, {"BuyPetition",0}, {"BuySkillTier",0}, {"BuyTrainerService",0}, {"BuybackItem",0},
    {"CalendarContextEventIsGuildWide",0}, {"CalendarEventCanComplain",0}, {"CalendarEventComplain",0},
    {"CalendarEventIsGuildWide",0}, {"CalendarNewGuildWideEvent",0}, {"CallCompanion",0},
    {"CanCancelAuction",0}, {"CanComplainChat",0}, {"CanComplainInboxItem",0}, {"CanEditGuildInfo",0},
    {"CanEditGuildTabInfo",0}, {"CanEditMOTD",0}, {"CanEditOfficerNote",0}, {"CanEditPublicNote",0},
    {"CanEjectPassengerFromSeat",0}, {"CanGuildBankRepair",0}, {"CanGuildDemote",0}, {"CanGuildInvite",0},
    {"CanGuildPromote",0}, {"CanGuildRemove",0}, {"CanHearthAndResurrectFromArea",0},
    {"CanJoinBattlefieldAsGroup",0}, {"CanMerchantRepair",0}, {"CanSendLFGQuery",0},
    {"CanShowAchievementUI",0}, {"CanSignPetition",0}, {"CanSwitchVehicleSeat",0}, {"CanUseEquipmentSets",0},
    {"CanViewOfficerNote",0}, {"CanWithdrawGuildBankMoney",0}, {"CancelBarberShop",0},
    {"CancelItemTempEnchantment",0}, {"CancelPendingLFG",0}, {"CancelShapeshiftForm",0}, {"CancelSkillUps",0},
    {"CancelSummon",0}, {"CastShapeshiftForm",0}, {"ChangeChatColor",0}, {"ChannelBan",0},
    {"ChannelInvite",0}, {"ChannelKick",0}, {"ChannelModerator",0}, {"ChannelMute",0},
    {"ChannelSilenceVoice",0}, {"ChannelToggleAnnouncements",0}, {"ChannelUnSilenceVoice",0},
    {"ChannelUnban",0}, {"ChannelUnmoderator",0}, {"ChannelUnmute",0}, {"CheckBinderDist",0},
    {"CheckInbox",0}, {"CheckReadyCheckTime",0}, {"CheckTalentMasterDist",0}, {"ClearInspectPlayer",0},
    {"ClearLFGAutojoin",0}, {"ClearLFMAutofill",0}, {"ClearLookingForGroup",0}, {"ClearLookingForMore",0},
    {"ClearPartyAssignment",0}, {"ClickLandmark",0}, {"ClickSendMailItemButton",0}, {"ClickSocketButton",0},
    {"ClickStablePet",0}, {"ClickTargetTradeButton",0}, {"ClickTradeButton",0}, {"CloseBankFrame",0},
    {"CloseGuildBankFrame",0}, {"CloseGuildRegistrar",0}, {"CloseLoot",0}, {"CloseMail",0},
    {"CloseMerchant",0}, {"ClosePetStables",0}, {"ClosePetition",0}, {"ClosePetitionVendor",0},
    {"CloseQuest",0}, {"CloseSocketInfo",0}, {"CloseTabardCreation",0}, {"CloseTaxiMap",0}, {"CloseTrade",0},
    {"CloseTradeSkill",0}, {"CloseTrainer",0}, {"CollapseChannelHeader",0}, {"CollapseFactionHeader",0},
    {"CollapseQuestHeader",0}, {"CollapseSkillHeader",0}, {"CollapseTradeSkillSubClass",0},
    {"CollapseTrainerSkillLine",0}, {"CombatTextSetActiveUnit",0}, {"ComplainChat",0},
    {"ComplainInboxItem",0}, {"CompleteQuest",0}, {"ConfirmAcceptQuest",0}, {"ConfirmBindOnUse",0},
    {"ConfirmBinder",0}, {"ConfirmLootRoll",0}, {"ConfirmLootSlot",0}, {"ConfirmSummon",0},
    {"ConfirmTalentWipe",0}, {"ConsoleAddMessage",0}, {"ContainerRefundItemPurchase",0},
    {"CreateMiniWorldMapArrowFrame",0}, {"CreateWorldMapArrowFrame",0}, {"CursorCanGoInSlot",0},
    {"DeclineArenaTeam",0}, {"DeclineInvite",0}, {"DeclineLFGMatch",0}, {"DeclineLevelGrant",0},
    {"DeclineQuest",0}, {"DelMute",0}, {"DeleteEquipmentSet",0}, {"DeleteGMTicket",0}, {"DeleteInboxItem",0},
    {"DepositGuildBankMoney",0}, {"DestroyTotem",0}, {"DisableSpellAutocast",0}, {"DismissCompanion",0},
    {"DisplayChannelOwner",0}, {"DoTradeSkill",0}, {"DressUpTexturePath",0}, {"DropCursorMoney",0},
    {"DropItemOnUnit",0}, {"DungeonUsesTerrainMap",0}, {"EjectPassengerFromSeat",0},
    {"EnableSpellAutocast",0}, {"EndBoundTradeable",0}, {"EndRefund",0}, {"EquipItemByName",0},
    {"EquipmentManagerIgnoreSlotForSave",0}, {"EquipmentManagerUnignoreSlotForSave",0},
    {"EquipmentSetContainsLockedItems",0}, {"ExpandChannelHeader",0}, {"ExpandCurrencyList",0},
    {"ExpandFactionHeader",0}, {"ExpandQuestHeader",0}, {"ExpandSkillHeader",0},
    {"ExpandTradeSkillSubClass",0}, {"ExpandTrainerSkillLine",0}, {"FactionToggleAtWar",0}, {"FollowUnit",0},
    {"ForceQuit",0}, {"GMResponseNeedMoreHelp",0}, {"GMResponseResolve",0}, {"GMSurveyAnswerSubmit",0},
    {"GMSurveyCommentSubmit",0}, {"GMSurveyQuestion",0}, {"GMSurveySubmit",0}, {"GameMovieFinished",0},
    {"GetAbandonQuestItems",0}, {"GetAbandonQuestName",0}, {"GetAchievementCategory",0},
    {"GetAchievementCriteriaInfo",0}, {"GetAchievementInfoFromCriteria",0}, {"GetAchievementLink",0},
    {"GetAchievementNumCriteria",0}, {"GetActiveTitle",0}, {"GetActiveVoiceChannel",0},
    {"GetAdjustedSkillPoints",1}, {"GetArmorPenetration",1}, {"GetAttackPowerForStat",1},
    {"GetAuctionInvTypes",0}, {"GetAuctionSort",0}, {"GetAutoCompleteResults",0}, {"GetAvailableTitle",0},
    {"GetBankSlotCost",1}, {"GetBarberShopStyleInfo",0}, {"GetBarberShopTotalCost",1},
    {"GetBattlefieldEstimatedWaitTime",1}, {"GetBattlefieldInfo",0}, {"GetBattlefieldInstanceExpiration",1},
    {"GetBattlefieldInstanceInfo",0}, {"GetBattlefieldMapIconScale",1}, {"GetBattlefieldPortExpiration",1},
    {"GetBattlefieldScore",0}, {"GetBattlefieldStatInfo",0}, {"GetBattlefieldTeamInfo",0},
    {"GetBattlefieldTimeWaited",1}, {"GetBattlefieldVehicleInfo",0}, {"GetBattlefieldWinner",0},
    {"GetBattlegroundInfo",0}, {"GetBidderAuctionItems",0}, {"GetBindingByKey",0}, {"GetBuybackItemInfo",0},
    {"GetBuybackItemLink",0}, {"GetCategoryInfo",0}, {"GetCategoryNumAchievements",0}, {"GetChannelList",0},
    {"GetChannelRosterInfo",0}, {"GetChatWindowChannels",0}, {"GetChatWindowMessages",0},
    {"GetCompanionCooldown",2}, {"GetCompanionInfo",0}, {"GetComparisonCategoryNumAchievements",0},
    {"GetContainerItemDurability",0}, {"GetContainerItemGems",0}, {"GetContainerItemPurchaseInfo",0},
    {"GetContainerItemPurchaseItem",0}, {"GetCritChanceFromAgility",1}, {"GetCurrencyListInfo",0},
    {"GetCurrencyListSize",1}, {"GetCurrentGuildBankTab",1}, {"GetCurrentMapDungeonLevel",1},
    {"GetCurrentTitle",0}, {"GetDailyQuestsCompleted",1}, {"GetDebugZoneMap",0},
    {"GetEquipmentSetInfoByName",0}, {"GetEquipmentSetItemIDs",0}, {"GetExistingSocketInfo",0},
    {"GetExistingSocketLink",0}, {"GetExpertisePercent",1}, {"GetFactionInfo",0}, {"GetFirstTradeSkill",0},
    {"GetGMStatus",0}, {"GetGamma",0}, {"GetGossipActiveQuests",0}, {"GetGossipAvailableQuests",0},
    {"GetGossipOptions",0}, {"GetGossipText",0}, {"GetGreetingText",0}, {"GetGuildBankItemInfo",0},
    {"GetGuildBankItemLink",0}, {"GetGuildBankMoneyTransaction",1}, {"GetGuildBankTabCost",1},
    {"GetGuildBankTabInfo",0}, {"GetGuildBankTabPermissions",0}, {"GetGuildBankText",0},
    {"GetGuildBankTransaction",0}, {"GetGuildBankWithdrawLimit",0}, {"GetGuildBankWithdrawMoney",1},
    {"GetGuildEventInfo",0}, {"GetGuildInfoText",0}, {"GetGuildRosterLastOnline",0}, {"GetGuildRosterMOTD",0},
    {"GetGuildRosterShowOffline",0}, {"GetGuildTabardFileNames",0}, {"GetIgnoreName",0},
    {"GetInboxHeaderInfo",0}, {"GetInboxInvoiceInfo",0}, {"GetInboxItem",0}, {"GetInboxItemLink",0},
    {"GetInboxText",0}, {"GetInspectArenaTeamData",0}, {"GetInspectHonorData",0},
    {"GetInstanceBootTimeRemaining",1}, {"GetInstanceLockTimeRemaining",1}, {"GetInventoryItemCooldown",2},
    {"GetInventoryItemGems",0}, {"GetInventoryItemsForSlot",0}, {"GetItemSpell",0}, {"GetLFGPartyResults",0},
    {"GetLFGResults",0}, {"GetLFGStatusText",0}, {"GetLFGTypeEntries",0}, {"GetLFGTypes",0},
    {"GetLatestThreeSenders",0}, {"GetLookingForGroup",0}, {"GetLootSlotInfo",0}, {"GetLootSlotLink",0},
    {"GetMapContinents",0}, {"GetMapDebugObjectInfo",0}, {"GetMapInfo",0}, {"GetMapLandmarkInfo",0},
    {"GetMapOverlayInfo",0}, {"GetMapVehicleTexture",0}, {"GetMapZones",0}, {"GetMasterLootCandidate",0},
    {"GetMaxCombatRatingBonus",1}, {"GetMaxDailyQuests",1}, {"GetMerchantItemCostInfo",0},
    {"GetMerchantItemCostItem",0}, {"GetMerchantItemInfo",0}, {"GetMerchantItemLink",0},
    {"GetMerchantItemMaxStack",0}, {"GetMerchantNumItems",0}, {"GetMinigameState",0}, {"GetMinigameType",0},
    {"GetMuteName",0}, {"GetNewSocketInfo",0}, {"GetNewSocketLink",0}, {"GetNextStableSlotCost",1},
    {"GetNumActiveQuests",1}, {"GetNumAvailableQuests",1}, {"GetNumBattlefieldStats",1},
    {"GetNumBattlefieldVehicles",1}, {"GetNumBattlefields",1}, {"GetNumBattlegroundTypes",1},
    {"GetNumBuybackItems",1}, {"GetNumChannelMembers",1}, {"GetNumCompanions",1},
    {"GetNumCompletedAchievements",1}, {"GetNumDungeonMapLevels",1}, {"GetNumGossipActiveQuests",1},
    {"GetNumGossipAvailableQuests",1}, {"GetNumGossipOptions",1}, {"GetNumGuildBankMoneyTransactions",1},
    {"GetNumGuildBankTabs",1}, {"GetNumGuildBankTransactions",1}, {"GetNumGuildEvents",1},
    {"GetNumIgnores",1}, {"GetNumLFGResults",1}, {"GetNumLootItems",1}, {"GetNumMapDebugObjects",1},
    {"GetNumMapLandmarks",1}, {"GetNumMapOverlays",1}, {"GetNumMutes",1}, {"GetNumPetitionNames",1},
    {"GetNumQuestChoices",1}, {"GetNumQuestItems",1}, {"GetNumQuestLeaderBoards",1},
    {"GetNumQuestLogChoices",1}, {"GetNumQuestLogRewards",1}, {"GetNumQuestRewards",1},
    {"GetNumQuestWatches",1}, {"GetNumRoutes",1}, {"GetNumSavedInstances",1}, {"GetNumSkillLines",1},
    {"GetNumSockets",1}, {"GetNumStablePets",1}, {"GetNumStableSlots",1}, {"GetNumStationeries",1},
    {"GetNumTitles",1}, {"GetNumTrackedAchievements",1}, {"GetNumTradeSkills",1}, {"GetNumTrainerServices",1},
    {"GetNumWhoResults",1}, {"GetObjectiveText",0}, {"GetOwnerAuctionItems",0}, {"GetPVPDesired",0},
    {"GetPVPRankInfo",0}, {"GetPVPTimer",1}, {"GetPartyAssignment",0}, {"GetPetExperience",0},
    {"GetPetFoodTypes",0}, {"GetPetHappiness",0}, {"GetPetIcon",0}, {"GetPetTalentTree",0},
    {"GetPetitionInfo",0}, {"GetPetitionItemInfo",0}, {"GetPetitionNameInfo",0}, {"GetPlayerInfoByGUID",0},
    {"GetPossessInfo",0}, {"GetPreviousAchievement",0}, {"GetProgressText",0},
    {"GetQuestBackgroundMaterial",0}, {"GetQuestIndexForTimer",1}, {"GetQuestIndexForWatch",0},
    {"GetQuestItemInfo",0}, {"GetQuestItemLink",0}, {"GetQuestLink",0}, {"GetQuestLogChoiceInfo",0},
    {"GetQuestLogGroupNum",0}, {"GetQuestLogItemLink",0}, {"GetQuestLogLeaderBoard",0},
    {"GetQuestLogPushable",0}, {"GetQuestLogRequiredMoney",1}, {"GetQuestLogRewardHonor",0},
    {"GetQuestLogRewardInfo",0}, {"GetQuestLogRewardMoney",1}, {"GetQuestLogRewardSpell",0},
    {"GetQuestLogRewardTalents",0}, {"GetQuestLogRewardTitle",0}, {"GetQuestLogSelection",0},
    {"GetQuestLogSpecialItemCooldown",2}, {"GetQuestLogSpecialItemInfo",0}, {"GetQuestLogSpellLink",0},
    {"GetQuestLogTimeLeft",1}, {"GetQuestMoneyToGet",1}, {"GetQuestResetTime",1}, {"GetQuestReward",0},
    {"GetQuestSortIndex",0}, {"GetQuestSpellLink",0}, {"GetQuestText",0}, {"GetQuestTimers",1},
    {"GetQuestWorldMapAreaID",0}, {"GetRaidRosterInfo",0}, {"GetRewardHonor",0}, {"GetRewardMoney",1},
    {"GetRewardSpell",0}, {"GetRewardTalents",0}, {"GetRewardText",0}, {"GetRewardTitle",0},
    {"GetSavedInstanceInfo",0}, {"GetSelectedBattlefield",0}, {"GetSelectedDisplayChannel",0},
    {"GetSelectedFaction",0}, {"GetSelectedFriend",0}, {"GetSelectedIgnore",0}, {"GetSelectedMute",0},
    {"GetSelectedSkill",0}, {"GetSelectedStablePet",0}, {"GetSelectedStationeryTexture",0},
    {"GetSendMailCOD",0}, {"GetSendMailItem",0}, {"GetSendMailMoney",1}, {"GetSkillLineInfo",0},
    {"GetSocketItemInfo",0}, {"GetSocketTypes",0}, {"GetSpellCritChanceFromIntellect",1},
    {"GetStablePetFoodTypes",0}, {"GetStablePetInfo",0}, {"GetStationeryInfo",0}, {"GetStatistic",0},
    {"GetSuggestedGroupNum",0}, {"GetSummonConfirmAreaName",0}, {"GetSummonConfirmSummoner",0},
    {"GetSummonConfirmTimeLeft",1}, {"GetTabardInfo",0}, {"GetTerrainMip",0}, {"GetTitleName",0},
    {"GetTitleText",0}, {"GetTotalAchievementPoints",1}, {"GetTotemTimeLeft",1}, {"GetTrackedAchievement",0},
    {"GetTrackedAchievements",0}, {"GetTradeSkillCooldown",0}, {"GetTradeSkillDescription",0},
    {"GetTradeSkillIcon",0}, {"GetTradeSkillInfo",0}, {"GetTradeSkillInvSlotFilter",0},
    {"GetTradeSkillInvSlots",0}, {"GetTradeSkillItemLink",0}, {"GetTradeSkillListLink",0},
    {"GetTradeSkillNumMade",0}, {"GetTradeSkillNumReagents",0}, {"GetTradeSkillReagentInfo",0},
    {"GetTradeSkillReagentItemLink",0}, {"GetTradeSkillRecipeLink",0}, {"GetTradeSkillSelectionIndex",0},
    {"GetTradeSkillSubClassFilter",0}, {"GetTradeSkillSubClasses",0}, {"GetTradeSkillTools",0},
    {"GetTradeskillRepeatCount",1}, {"GetTrainerGreetingText",0}, {"GetTrainerSelectionIndex",0},
    {"GetTrainerServiceAbilityReq",0}, {"GetTrainerServiceCost",1}, {"GetTrainerServiceDescription",0},
    {"GetTrainerServiceIcon",0}, {"GetTrainerServiceInfo",0}, {"GetTrainerServiceItemLink",0},
    {"GetTrainerServiceLevelReq",0}, {"GetTrainerServiceNumAbilityReq",0}, {"GetTrainerServiceSkillLine",0},
    {"GetTrainerServiceSkillReq",0}, {"GetTrainerServiceStepReq",0}, {"GetTrainerServiceTypeFilter",0},
    {"GetUnitHealthModifier",0}, {"GetUnitHealthRegenRateFromSpirit",0}, {"GetUnitManaRegenRateFromSpirit",0},
    {"GetUnitMaxHealthModifier",0}, {"GetUnitPowerModifier",0}, {"GetWhoInfo",0},
    {"GetWorldPVPQueueStatus",0}, {"GetWorldStateUIInfo",0}, {"GiveMasterLoot",0}, {"GuildControlAddRank",0},
    {"GuildControlDelRank",0}, {"GuildControlGetRankFlags",0}, {"GuildControlSaveRank",0},
    {"GuildControlSetRank",0}, {"GuildControlSetRankFlag",0}, {"HasDebugZoneMap",0}, {"HasFilledPetition",0},
    {"HasInspectHonorData",0}, {"HasKey",0}, {"HasNewMail",0}, {"HasWandEquipped",0}, {"HideRepairCursor",0},
    {"InboxItemCanDelete",0}, {"InitWorldMapPing",0}, {"IsActiveQuestTrivial",0}, {"IsAtStableMaster",0},
    {"IsAvailableQuestTrivial",0}, {"IsBattlefieldArena",0}, {"IsCurrentQuestFailed",0},
    {"IsDesaturateSupported",0}, {"IsDisplayChannelModerator",0}, {"IsDisplayChannelOwner",0},
    {"IsEquippableItem",0}, {"IsFactionInactive",0}, {"IsFishingLoot",0}, {"IsGuildLeader",0},
    {"IsInLFGQueue",0}, {"IsMuted",0}, {"IsPVPTimerRunning",0}, {"IsPlayerResolutionAvailable",0},
    {"IsPossessBarVisible",0}, {"IsQuestCompletable",0}, {"IsQuestLogSpecialItemInRange",0},
    {"IsQuestWatched",0}, {"IsRealPartyLeader",0}, {"IsRealRaidLeader",0}, {"IsSilenced",0},
    {"IsSubZonePVPPOI",0}, {"IsTitleKnown",0}, {"IsTradeSkillLinked",0}, {"IsTradeskillTrainer",0},
    {"IsUnitOnQuest",0}, {"IsZoomOutAvailable",0}, {"JoinBattlefield",0}, {"KeyRingButtonIDToInvSlotID",0},
    {"LFGQuery",0}, {"LeaveBattlefield",0}, {"ListChannelByName",0}, {"ListChannels",0}, {"LoggingChat",0},
    {"LoggingCombat",0}, {"LootSlot",0}, {"LootSlotIsCoin",0}, {"LootSlotIsItem",0}, {"NewGMTicket",0},
    {"NoPlayTime",0}, {"NotWhileDeadError",0}, {"NotifyInspect",0}, {"NumTaxiNodes",0}, {"PartialPlayTime",0},
    {"PetAbandon",0}, {"PetAggressiveMode",0}, {"PetAttack",0}, {"PetCanBeAbandoned",0},
    {"PetCanBeDismissed",0}, {"PetCanBeRenamed",0}, {"PetDefensiveMode",0}, {"PetDismiss",0}, {"PetFollow",0},
    {"PetPassiveMode",0}, {"PetRename",0}, {"PetWait",0}, {"PickupBagFromSlot",0}, {"PickupCompanion",0},
    {"PickupEquipmentSet",0}, {"PickupEquipmentSetByName",0}, {"PickupGuildBankItem",0},
    {"PickupGuildBankMoney",0}, {"PickupItem",0}, {"PickupMerchantItem",0}, {"PickupPetAction",0},
    {"PickupPlayerMoney",0}, {"PickupSendMailCOD",0}, {"PickupSendMailMoney",0}, {"PickupStablePet",0},
    {"PickupTradeMoney",0}, {"PitchDownStart",0}, {"PitchDownStop",0}, {"PitchUpStart",0}, {"PitchUpStop",0},
    {"PlayerCanTeleport",0}, {"PlayerNameAutocomplete",0}, {"PositionMiniWorldMapArrowFrame",0},
    {"ProcessMapClick",0}, {"PromoteToAssistant",0}, {"PurchaseSlot",0}, {"QueryGuildBankLog",0},
    {"QueryGuildBankTab",0}, {"QueryGuildBankText",0}, {"QuestChooseRewardError",0}, {"QuestFlagsPVP",0},
    {"QuestLogPushQuest",0}, {"QuestMapGetMouseOverInfoByIndex",0}, {"QuestMapGetMouseOverPOIInfo",0},
    {"QuestMapGetNumQuestsForPOI",0}, {"QuestMapGetPOIInfo",0}, {"QuestMapGetQuestInfo",0},
    {"QuestMapUpdateAllQuests",0}, {"QuestMapUpdateMouseOverPOI",0}, {"RegisterForSave",0},
    {"RemoveChatWindowChannel",0}, {"RemoveChatWindowMessages",0}, {"RemoveGlyphFromSocket",0},
    {"RemoveQuestWatch",0}, {"RemoveSkillUp",0}, {"RemoveTrackedAchievement",0}, {"RenamePetition",0},
    {"ReplaceEnchant",0}, {"ReplaceTradeEnchant",0}, {"ReportPlayerIsPVPAFK",0},
    {"RequestBattlegroundInstanceInfo",0}, {"RequestInspectHonorData",0}, {"RequestRaidInfo",0},
    {"ResetChatWindows",0}, {"ResetTutorials",0}, {"RespondInstanceLock",0},
    {"RestoreVideoEffectsDefaults",0}, {"RestoreVideoResolutionDefaults",0}, {"RestoreVideoStereoDefaults",0},
    {"ReturnInboxItem",0}, {"RollOnLoot",0}, {"SaveEquipmentSet",0}, {"SaveGuildRoster",0},
    {"SelectActiveQuest",0}, {"SelectAvailableQuest",0}, {"SelectGossipActiveQuest",0},
    {"SelectGossipAvailableQuest",0}, {"SelectGossipOption",0}, {"SelectStationery",0},
    {"SelectTradeSkill",0}, {"SelectTrainerService",0}, {"SendMail",0}, {"SetAbandonQuest",0},
    {"SetActiveVoiceChannel",0}, {"SetActiveVoiceChannelBySessionID",0}, {"SetChannelOwner",0},
    {"SetChannelPassword",0}, {"SetChatColorNameByClass",0}, {"SetCurrencyBackpack",0},
    {"SetCurrencyUnused",0}, {"SetCurrentGuildBankTab",0}, {"SetCurrentTitle",0}, {"SetDungeonMapLevel",0},
    {"SetFactionActive",0}, {"SetFactionInactive",0}, {"SetGamma",0}, {"SetGuildBankTabInfo",0},
    {"SetGuildBankTabPermissions",0}, {"SetGuildBankTabWithdraw",0}, {"SetGuildBankText",0},
    {"SetGuildBankWithdrawLimit",0}, {"SetGuildInfoText",0}, {"SetGuildRosterShowOffline",0},
    {"SetLFGAutojoin",0}, {"SetLFMAutofill",0}, {"SetLFMType",0}, {"SetLookingForGroup",0},
    {"SetLookingForMore",0}, {"SetMapByID",0}, {"SetMapZoom",0}, {"SetNextBarberShopStyle",0},
    {"SetOptOutOfLoot",0}, {"SetPartyAssignment",0}, {"SetPetStablePaperdoll",0},
    {"SetRaidRosterSelection",0}, {"SetRaidSubgroup",0}, {"SetRaidTarget",0}, {"SetSavedInstanceExtend",0},
    {"SetSelectedBattlefield",0}, {"SetSelectedDisplayChannel",0}, {"SetSelectedFaction",0},
    {"SetSelectedFriend",0}, {"SetSelectedIgnore",0}, {"SetSelectedMute",0}, {"SetSelectedSkill",0},
    {"SetSendMailCOD",0}, {"SetSendMailMoney",0}, {"SetSendMailShowing",0}, {"SetTaxiBenchmarkMode",0},
    {"SetTaxiMap",0}, {"SetTerrainMip",0}, {"SetTextureDesaturated",0}, {"SetTrackedAchievement",0},
    {"SetTradeMoney",0}, {"SetTradeSkillInvSlotFilter",0}, {"SetTradeSkillItemLevelFilter",0},
    {"SetTradeSkillItemNameFilter",0}, {"SetTradeSkillSubClassFilter",0}, {"SetTrainerServiceTypeFilter",0},
    {"SetWatchedFactionIndex",0}, {"SetupFullscreenScale",0}, {"ShowBattlefieldList",0},
    {"ShowBuybackSellCursor",0}, {"ShowCloak",0}, {"ShowContainerSellCursor",0}, {"ShowFriends",0},
    {"ShowHelm",0}, {"ShowMerchantSellCursor",0}, {"ShowMiniWorldMapArrowFrame",0}, {"ShowRepairCursor",0},
    {"ShowingCloak",0}, {"ShowingHelm",0}, {"SocketContainerItem",0}, {"SocketInventoryItem",0},
    {"SortArenaTeamRoster",0}, {"SortBattlefieldScoreData",0}, {"SortGuildRoster",0}, {"SortLFG",0},
    {"SortWho",0}, {"SpellTargetItem",0}, {"SplitGuildBankItem",0}, {"StartDuel",0}, {"StopCinematic",0},
    {"StopMacro",0}, {"StopTradeSkillRepeat",0}, {"Stuck",0}, {"SwapRaidSubgroup",0}, {"TakeInboxItem",0},
    {"TakeInboxMoney",0}, {"TakeInboxTextItem",0}, {"TakeTaxiNode",0}, {"TargetLastFriend",0},
    {"TargetNearestPartyMember",0}, {"TargetNearestRaidMember",0}, {"TaxiGetDestX",0}, {"TaxiGetDestY",0},
    {"TaxiGetSrcX",0}, {"TaxiGetSrcY",0}, {"TaxiNodeCost",0}, {"TaxiNodeGetType",0}, {"TaxiNodeName",0},
    {"TaxiNodePosition",0}, {"TaxiNodeSetCurrent",0}, {"TeleportToDebugObject",0}, {"ToggleSpellAutocast",0},
    {"ToggleWorldStateScoreFrame",0}, {"TradeSkillOnlyShowMakeable",0}, {"TurnInArenaPetition",0},
    {"TutorialsEnabled",0}, {"UnitArmor",0}, {"UnitAttackBothHands",0}, {"UnitAttackSpeed",0},
    {"UnitClassBase",0}, {"UnitDamage",0}, {"UnitDefense",0}, {"UnitHasRelicSlot",0}, {"UnitIsInMyGuild",0},
    {"UnitIsPVPSanctuary",0}, {"UnitIsRaidOfficer",0}, {"UnitIsSameServer",0}, {"UnitIsSilenced",0},
    {"UnitIsVisible",0}, {"UnitPVPName",0}, {"UnitPVPRank",0}, {"UnitPlayerOrPetInParty",0},
    {"UnitPlayerOrPetInRaid",0}, {"UnitRangedAttack",0}, {"UnitRangedDamage",0}, {"UnitResistance",0},
    {"UnitSex",0}, {"UnitSwitchToVehicleSeat",0}, {"UnitVehicleSeatInfo",0}, {"UnitVehicleSkin",0},
    {"UpdateArenaEnemyBackground",0}, {"UpdateGMTicket",0}, {"UpdateMapHighlight",0}, {"UseEquipmentSet",0},
    {"UseItemByName",0}, {"UseQuestLogSpecialItem",0}, {"VehicleAimRequestNormAngle",0},
    {"VoiceEnumerateCaptureDevices",0}, {"VoiceEnumerateOutputDevices",0}, {"VoiceIsDisabledByClient",0},
    {"VoiceSelectCaptureDevice",0}, {"VoiceSelectOutputDevice",0}, {"WithdrawGuildBankMoney",0},
    {"ZoomOut",0},
};

int L_PlaceholderApi(lua_State* L)
{
    static TSet<FString> Logged;
    const char* Name = lua_tostring(L, lua_upvalueindex(2));
    const FString ApiName = Name ? FString(UTF8_TO_TCHAR(Name)) : FString();
    if (!Logged.Contains(ApiName) && Logged.Num() < 2000)
    {
        Logged.Add(ApiName);
        luaL_where(L, 1);
        const char* Where = lua_tostring(L, -1);
        UE_LOG(LogWowLuaPlaceholder, Warning, TEXT("Lua API placeholder called: %s (%s)"), *ApiName,
            Where ? UTF8_TO_TCHAR(Where) : TEXT("?"));
        lua_pop(L, 1);
    }
    const lua_Integer Kind = lua_tointeger(L, lua_upvalueindex(1));
    if (Kind == 1) { lua_pushinteger(L, 0); return 1; }
    // start, duration, enable: CooldownFrame_SetTimer compares all three with 0.
    if (Kind == 2) { lua_pushinteger(L, 0); lua_pushinteger(L, 0); lua_pushinteger(L, 0); return 3; }
    return 0;
}
}
#endif

void WowLuaApi::RegisterPlaceholderApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    // Small real ones built on functions the client already has.
    luaL_dostring(L, R"LUA(
if not GetContainerItemID then
    function GetContainerItemID(bag, slot)
        local link = GetContainerItemLink and GetContainerItemLink(bag, slot)
        return link and tonumber(string.match(link, "item:(%d+)")) or nil
    end
end
if not GetInventoryItemID then
    function GetInventoryItemID(unit, slot)
        local link = GetInventoryItemLink and GetInventoryItemLink(unit, slot)
        return link and tonumber(string.match(link, "item:(%d+)")) or nil
    end
end
if not GetCoinIcon then
    function GetCoinIcon(amount)
        amount = tonumber(amount) or 0
        if amount >= 10000 then return "Interface\\Icons\\INV_Misc_Coin_01" end
        if amount >= 100 then return "Interface\\Icons\\INV_Misc_Coin_03" end
        return "Interface\\Icons\\INV_Misc_Coin_05"
    end
end
)LUA");
    int32 Added = 0;
    for (const FPlaceholderApi& Api : PlaceholderApis)
    {
        lua_getglobal(L, Api.Name);
        const bool bExists = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (bExists) continue;
        lua_pushinteger(L, Api.Kind);
        lua_pushstring(L, Api.Name);
        lua_pushcclosure(L, L_PlaceholderApi, 2);
        lua_setglobal(L, Api.Name);
        ++Added;
    }
    UE_LOG(LogWowLuaPlaceholder, Log, TEXT("Lua API placeholders registered: %d of %d"), Added, int32(UE_ARRAY_COUNT(PlaceholderApis)));
#endif
}
