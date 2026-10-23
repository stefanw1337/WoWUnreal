#include "WowPacketHandler.h"
#include "WowOpcodes.h"
#include "WowDifficultyState.h"
#include "WowUpdateFields.h"
#include "WowWardenHandler.h"
#include "WowMovementPacket.h"
#include "Compression/CompressedBuffer.h"
#include "Misc/Compression.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/AreaTableDbc.h"
#include "Formats/Dbc/QuestSortDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowPacket, Log, All);


namespace
{
// Build 12340 MovementInfo, without CREATE_OBJECT speeds/spline data.
// Parse into a copy so truncated messages cannot corrupt a live entity.
bool ReadMovementSnapshot(FPacketReader& R, FWowMovementInfo& Info, uint32& Timestamp)
{
    Info.MoveFlags = R.ReadU32();
    Info.MoveFlags2 = R.ReadU16();
    Timestamp = R.ReadU32();
    Info.Position.X = R.ReadFloat();
    Info.Position.Y = R.ReadFloat();
    Info.Position.Z = R.ReadFloat();
    Info.Orientation = R.ReadFloat();
    Info.TransportGuid = 0;
    Info.TransportOffset = FVector::ZeroVector;
    Info.TransportOrientation = 0.0f;
    if (Info.MoveFlags & 0x00000200)
    {
        Info.TransportGuid = R.ReadPackedGuid();
        Info.TransportOffset.X = R.ReadFloat();
        Info.TransportOffset.Y = R.ReadFloat();
        Info.TransportOffset.Z = R.ReadFloat();
        Info.TransportOrientation = R.ReadFloat();
        R.ReadU32(); R.ReadU8(); // transport time and seat
        if (Info.MoveFlags2 & 0x0400) R.ReadU32(); // interpolated transport time
    }
    if ((Info.MoveFlags & 0x02200000) || (Info.MoveFlags2 & 0x0020)) R.ReadFloat();
    Info.FallTime = R.ReadU32();
    Info.JumpZSpeed = 0.0f;
    if (Info.MoveFlags & 0x00001000) { Info.JumpZSpeed = R.ReadFloat(); R.Skip(12); } // jump velocity and direction
    if (Info.MoveFlags & 0x04000000) R.ReadFloat();
    if (R.HasFailed()) return false;
    if (Info.Position.ContainsNaN() || !FMath::IsFinite(Info.Orientation))
    {
        R.Fail(TEXT("Invalid movement coordinates"));
        return false;
    }
    Info.MovementTimestamp = Timestamp;
    Info.MovementReceivedAtSeconds = FPlatformTime::Seconds();
    return true;
}
}

FWowPacketHandler::FWowPacketHandler()
{
    Handlers.Add(0x1B1,&FWowPacketHandler::HandleTrainerList);
    Handlers.Add(0x1B3,&FWowPacketHandler::HandleTrainerSuccess);
    Handlers.Add(0x1B4,&FWowPacketHandler::HandleTrainerFailure);
    Handlers.Add(0x255,&FWowPacketHandler::HandleAuctionHello);
    Handlers.Add(0x25C,&FWowPacketHandler::HandleAuctionList);
    Handlers.Add(0x25D,&FWowPacketHandler::HandleAuctionOwnerList);
    Handlers.Add(0x265,&FWowPacketHandler::HandleAuctionBidderList);
    Handlers.Add(0x25B,&FWowPacketHandler::HandleAuctionResult);
    Handlers.Add(0x3F4,&FWowPacketHandler::HandleInspectTalent); // SMSG_INSPECT_TALENT
    Handlers.Add(0x47D,&FWowPacketHandler::HandleAllAchievementData); // SMSG_ALL_ACHIEVEMENT_DATA
    Handlers.Add(0x468,&FWowPacketHandler::HandleAchievementEarned);  // SMSG_ACHIEVEMENT_EARNED
    Handlers.Add(0x46A,&FWowPacketHandler::HandleCriteriaUpdate);     // SMSG_CRITERIA_UPDATE
    // Register handlers
    Handlers.Add(WowOpcode::SMSG_LOGIN_VERIFY_WORLD,      &FWowPacketHandler::HandleLoginVerifyWorld);
    Handlers.Add(WowOpcode::SMSG_UPDATE_OBJECT,            &FWowPacketHandler::HandleUpdateObject);
    Handlers.Add(WowOpcode::SMSG_COMPRESSED_UPDATE_OBJECT, &FWowPacketHandler::HandleCompressedUpdateObject);
    Handlers.Add(WowOpcode::SMSG_DESTROY_OBJECT,           &FWowPacketHandler::HandleDestroyObject);
    Handlers.Add(WowOpcode::SMSG_MESSAGECHAT,              &FWowPacketHandler::HandleMessageChat);
    Handlers.Add(WowOpcode::SMSG_MOTD,                     &FWowPacketHandler::HandleMotd);
    Handlers.Add(WowOpcode::SMSG_INITIAL_SPELLS,           &FWowPacketHandler::HandleInitialSpells);
    Handlers.Add(WowOpcode::SMSG_ACTION_BUTTONS,           &FWowPacketHandler::HandleActionButtons);
    Handlers.Add(WowOpcode::SMSG_QUEST_QUERY_RESPONSE,     &FWowPacketHandler::HandleQuestQueryResponse);
    Handlers.Add(WowOpcode::SMSG_LOGIN_SETTIMESPEED, &FWowPacketHandler::HandleLoginTimeSpeed);
    Handlers.Add(WowOpcode::SMSG_TIME_SYNC_REQ,            &FWowPacketHandler::HandleTimeSyncReq);
    Handlers.Add(WowOpcode::SMSG_SPELL_START,              &FWowPacketHandler::HandleSpellStart);
    Handlers.Add(WowOpcode::SMSG_SPELL_GO,                 &FWowPacketHandler::HandleSpellGo);
    Handlers.Add(WowOpcode::SMSG_SPELL_FAILURE,            &FWowPacketHandler::HandleSpellFailure);
    Handlers.Add(WowOpcode::SMSG_CAST_FAILED,              &FWowPacketHandler::HandleCastFailed);
    Handlers.Add(WowOpcode::SMSG_SPELL_COOLDOWN,           &FWowPacketHandler::HandleSpellCooldown);
    Handlers.Add(WowOpcode::MSG_CHANNEL_START,             &FWowPacketHandler::HandleChannelStart);
    Handlers.Add(WowOpcode::MSG_CHANNEL_UPDATE,            &FWowPacketHandler::HandleChannelUpdate);
    Handlers.Add(WowOpcode::SMSG_SPELL_DELAYED,            &FWowPacketHandler::HandleSpellDelayed);
    Handlers.Add(WowOpcode::SMSG_ATTACKERSTATEUPDATE,      &FWowPacketHandler::HandleAttackerStateUpdate);
    Handlers.Add(WowOpcode::SMSG_SPELLNONMELEEDAMAGELOG,   &FWowPacketHandler::HandleSpellNonMeleeDamageLog);
    Handlers.Add(WowOpcode::SMSG_SPELLHEALLOG,             &FWowPacketHandler::HandleSpellHealLog);
    Handlers.Add(WowOpcode::SMSG_SPELLENERGIZELOG,         &FWowPacketHandler::HandleSpellEnergizeLog);
    Handlers.Add(WowOpcode::SMSG_PERIODICAURALOG,          &FWowPacketHandler::HandlePeriodicAuraLog);
    Handlers.Add(WowOpcode::SMSG_SPELLLOGMISS,             &FWowPacketHandler::HandleSpellLogMiss);
    Handlers.Add(WowOpcode::SMSG_SPELLDAMAGESHIELD,        &FWowPacketHandler::HandleSpellDamageShield);
    Handlers.Add(WowOpcode::SMSG_PARTYKILLLOG,             &FWowPacketHandler::HandlePartyKillLog);
    Handlers.Add(WowOpcode::SMSG_ATTACKSTART,              &FWowPacketHandler::HandleAttackStart);
    Handlers.Add(WowOpcode::SMSG_ATTACKSTOP,               &FWowPacketHandler::HandleAttackStop);
    Handlers.Add(WowOpcode::SMSG_ITEM_ENCHANT_TIME_UPDATE, &FWowPacketHandler::HandleItemEnchantTimeUpdate);
    Handlers.Add(WowOpcode::SMSG_AURA_UPDATE,              &FWowPacketHandler::HandleAuraUpdate);
    Handlers.Add(WowOpcode::SMSG_AURA_UPDATE_ALL, &FWowPacketHandler::HandleAuraUpdateAll);
    Handlers.Add(WowOpcode::SMSG_POWER_UPDATE,             &FWowPacketHandler::HandlePowerUpdate);
    Handlers.Add(WowOpcode::SMSG_MONSTER_MOVE,             &FWowPacketHandler::HandleMonsterMove);

    // Inventory system handlers
    Handlers.Add(WowOpcode::SMSG_INVENTORY_CHANGE_FAILURE, &FWowPacketHandler::HandleInventoryChangeFailure);
    Handlers.Add(WowOpcode::SMSG_LOOT_RESPONSE,            &FWowPacketHandler::HandleLootResponse);
    Handlers.Add(WowOpcode::SMSG_LOOT_RELEASE_RESPONSE,    &FWowPacketHandler::HandleLootReleaseResponse);
    Handlers.Add(WowOpcode::SMSG_LOOT_REMOVED, &FWowPacketHandler::HandleLootRemoved);
    Handlers.Add(WowOpcode::SMSG_LOOT_CLEAR_MONEY, &FWowPacketHandler::HandleLootClearMoney);
    Handlers.Add(WowOpcode::SMSG_ITEM_PUSH_RESULT,         &FWowPacketHandler::HandleItemPushResult);

    // Vendor system handlers
    Handlers.Add(WowOpcode::SMSG_LIST_INVENTORY,           &FWowPacketHandler::HandleListInventory);
    Handlers.Add(WowOpcode::SMSG_ITEM_QUERY_SINGLE_RESPONSE, &FWowPacketHandler::HandleItemQueryResponse);
    Handlers.Add(WowOpcode::SMSG_BUY_ITEM, &FWowPacketHandler::HandleBuyItemResult);
    Handlers.Add(WowOpcode::SMSG_BUY_FAILED, &FWowPacketHandler::HandleBuyItemFailure);
    Handlers.Add(WowOpcode::SMSG_SELL_ITEM, &FWowPacketHandler::HandleSellItemResult);

    // Quest system handlers
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_STATUS,        &FWowPacketHandler::HandleQuestgiverStatus);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_STATUS_MULTIPLE, &FWowPacketHandler::HandleQuestgiverStatusMultiple);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_QUEST_LIST,    &FWowPacketHandler::HandleQuestgiverQuestList);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_QUEST_DETAILS, &FWowPacketHandler::HandleQuestgiverQuestDetails);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_REQUEST_ITEMS, &FWowPacketHandler::HandleQuestgiverRequestItems);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_OFFER_REWARD,  &FWowPacketHandler::HandleQuestgiverOfferReward);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_QUEST_COMPLETE, &FWowPacketHandler::HandleQuestgiverQuestComplete);
    Handlers.Add(WowOpcode::SMSG_QUEST_UPDATE_ADD_KILL,    &FWowPacketHandler::HandleQuestUpdateAddKill);
    Handlers.Add(WowOpcode::SMSG_QUEST_UPDATE_COMPLETE,    &FWowPacketHandler::HandleQuestUpdateComplete);

    // Talent system handlers
    Handlers.Add(WowOpcode::SMSG_TALENTS_INFO,             &FWowPacketHandler::HandleTalentsInfo);
    Handlers.Add(WowOpcode::SMSG_LEARNED_SPELL,            &FWowPacketHandler::HandleLearnedSpell);
    Handlers.Add(WowOpcode::SMSG_REMOVED_SPELL,            &FWowPacketHandler::HandleRemovedSpell);

    // ── Social / Guild / Friends handlers ──────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_FRIEND_LIST,              &FWowPacketHandler::HandleFriendList);
    Handlers.Add(WowOpcode::SMSG_FRIEND_STATUS,            &FWowPacketHandler::HandleFriendStatus);
    Handlers.Add(0x0055, &FWowPacketHandler::HandleGuildName); // SMSG_GUILD_QUERY_RESPONSE
    Handlers.Add(WowOpcode::SMSG_GUILD_ROSTER,             &FWowPacketHandler::HandleGuildRoster);
    // r285: guild bank (3.3.5 opcodes).
    Handlers.Add(0x3E8, &FWowPacketHandler::HandleGuildBankList);
    Handlers.Add(0x3FC, &FWowPacketHandler::HandleGuildPermissions);
    Handlers.Add(0x3FE, &FWowPacketHandler::HandleGuildBankMoneyWithdrawn);
    Handlers.Add(0x39D, &FWowPacketHandler::HandleUpdateComboPoints); // r317: SMSG_UPDATE_COMBO_POINTS
    Handlers.Add(0x3EE, &FWowPacketHandler::HandleGuildBankLog);
    Handlers.Add(0x40A, &FWowPacketHandler::HandleGuildBankText);
    Handlers.Add(WowOpcode::SMSG_GUILD_EVENT,              &FWowPacketHandler::HandleGuildEvent);
    Handlers.Add(WowOpcode::SMSG_CHANNEL_NOTIFY,           &FWowPacketHandler::HandleChannelNotify);
    Handlers.Add(WowOpcode::SMSG_GROUP_LIST,               &FWowPacketHandler::HandleGroupList);
    Handlers.Add(WowOpcode::MSG_RAID_TARGET_UPDATE,        &FWowPacketHandler::HandleRaidTargetUpdate);
    Handlers.Add(WowOpcode::MSG_RAID_READY_CHECK,          &FWowPacketHandler::HandleRaidReadyCheck);
    Handlers.Add(WowOpcode::MSG_RAID_READY_CHECK_CONFIRM,  &FWowPacketHandler::HandleRaidReadyCheckConfirm);
    Handlers.Add(WowOpcode::MSG_RAID_READY_CHECK_FINISHED, &FWowPacketHandler::HandleRaidReadyCheckFinished);
    Handlers.Add(WowOpcode::SMSG_PARTY_COMMAND_RESULT,     &FWowPacketHandler::HandlePartyCommandResult);
    Handlers.Add(WowOpcode::SMSG_GROUP_INVITE,             &FWowPacketHandler::HandleGroupInvite);
    Handlers.Add(WowOpcode::SMSG_WHO,                      &FWowPacketHandler::HandleWho);
    Handlers.Add(0x01FB /*MSG_RANDOM_ROLL*/,               &FWowPacketHandler::HandleRandomRoll);

    // Wrath Dungeon Finder and Looking For Raid browser
    Handlers.Add(WowOpcode::SMSG_LFG_QUEUE_STATUS,          &FWowPacketHandler::HandleLfgQueueStatus);
    Handlers.Add(WowOpcode::SMSG_LFG_PROPOSAL_UPDATE,       &FWowPacketHandler::HandleLfgProposalUpdate);
    Handlers.Add(WowOpcode::SMSG_LFG_ROLE_CHECK_UPDATE,     &FWowPacketHandler::HandleLfgRoleCheckUpdate);
    Handlers.Add(WowOpcode::SMSG_LFG_JOIN_RESULT,           &FWowPacketHandler::HandleLfgJoinResult);
    Handlers.Add(WowOpcode::SMSG_LFG_UPDATE_PLAYER,         &FWowPacketHandler::HandleLfgUpdatePlayer);
    Handlers.Add(WowOpcode::SMSG_LFG_UPDATE_PARTY,          &FWowPacketHandler::HandleLfgUpdateParty);
    Handlers.Add(WowOpcode::SMSG_LFG_UPDATE_SEARCH,         &FWowPacketHandler::HandleLfgUpdateSearch);
    Handlers.Add(WowOpcode::SMSG_UPDATE_LFG_LIST,           &FWowPacketHandler::HandleLfgList);
    Handlers.Add(WowOpcode::SMSG_LFG_DISABLED,              &FWowPacketHandler::HandleLfgDisabled);

    // ── Taxi / Flight Path handlers ─────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_SHOWTAXINODES,            &FWowPacketHandler::HandleShowTaxiNodes);
    Handlers.Add(WowOpcode::SMSG_ACTIVATETAXIREPLY,        &FWowPacketHandler::HandleActivateTaxiReply);
    Handlers.Add(WowOpcode::SMSG_NEW_TAXI_PATH,            &FWowPacketHandler::HandleNewTaxiPath);

    // ── Death / Corpse / Resurrection handlers ──────────────────────────────
    Handlers.Add(WowOpcode::SMSG_CORPSE_RECLAIM_DELAY,     &FWowPacketHandler::HandleCorpseReclaimDelay);
    Handlers.Add(WowOpcode::SMSG_RESURRECT_REQUEST,        &FWowPacketHandler::HandleResurrectRequest);
    Handlers.Add(0x222, &FWowPacketHandler::HandleSpiritHealerConfirm);   // SMSG_SPIRIT_HEALER_CONFIRM
    Handlers.Add(0x216, &FWowPacketHandler::HandleCorpseQuery);           // MSG_CORPSE_QUERY
    Handlers.Add(0x2E4, &FWowPacketHandler::HandleAreaSpiritHealerTime);  // SMSG_AREA_SPIRIT_HEALER_TIME

    // ── Name query handlers ─────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_NAME_QUERY_RESPONSE,      &FWowPacketHandler::HandleNameQueryResponse);
    Handlers.Add(WowOpcode::SMSG_GAMEOBJECT_QUERY_RESPONSE, &FWowPacketHandler::HandleGameObjectQueryResponse);
    Handlers.Add(WowOpcode::SMSG_GAMEOBJECT_PAGETEXT, &FWowPacketHandler::HandleGameObjectPageText);
    Handlers.Add(0x0329, &FWowPacketHandler::HandleSetDungeonDifficulty); // r563: MSG_SET_DUNGEON_DIFFICULTY
    Handlers.Add(0x04EB, &FWowPacketHandler::HandleSetRaidDifficulty);    // r563: MSG_SET_RAID_DIFFICULTY
    Handlers.Add(WowOpcode::SMSG_PAGE_TEXT_QUERY_RESPONSE, &FWowPacketHandler::HandlePageTextQueryResponse);
    Handlers.Add(WowOpcode::SMSG_CREATURE_QUERY_RESPONSE,  &FWowPacketHandler::HandleCreatureQueryResponse);

    // ── Emote handlers ──────────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_EMOTE,                    &FWowPacketHandler::HandleEmote);
    Handlers.Add(WowOpcode::SMSG_PLAY_SOUND,               &FWowPacketHandler::HandlePlaySound);
    Handlers.Add(WowOpcode::SMSG_PLAY_OBJECT_SOUND,        &FWowPacketHandler::HandlePlayObjectSound);
    Handlers.Add(WowOpcode::SMSG_GAMEOBJECT_CUSTOM_ANIM,   &FWowPacketHandler::HandleGameObjectCustomAnim);
    Handlers.Add(WowOpcode::SMSG_MIRRORIMAGE_DATA,         &FWowPacketHandler::HandleMirrorImageData);
    Handlers.Add(WowOpcode::SMSG_PLAY_MUSIC,               &FWowPacketHandler::HandlePlayMusic);
    Handlers.Add(WowOpcode::SMSG_AI_REACTION,              &FWowPacketHandler::HandleAIReaction);
    Handlers.Add(WowOpcode::SMSG_TEXT_EMOTE,               &FWowPacketHandler::HandleTextEmote);

    // Movement handlers — all use the same parser
    for (uint16 Op = WowOpcode::MSG_MOVE_START_FORWARD; Op <= WowOpcode::MSG_MOVE_SET_PITCH; ++Op)
    {
        Handlers.Add(Op, &FWowPacketHandler::HandleMovement);
    }
    Handlers.Add(WowOpcode::MSG_MOVE_ROOT, &FWowPacketHandler::HandleMovement);
    Handlers.Add(WowOpcode::MSG_MOVE_UNROOT, &FWowPacketHandler::HandleMovement);
    Handlers.Add(WowOpcode::MSG_MOVE_HEARTBEAT, &FWowPacketHandler::HandleMovement);

    // Other movers' speed changes carry a trailing speed after MovementInfo.
    for (const uint16 Op : { 0x0CD, 0x0CF, 0x0D1, 0x0D3, 0x0D5, 0x0D8, 0x37E, 0x380, 0x45B })
        Handlers.Add(Op, &FWowPacketHandler::HandleMoveSetSpeed);
    // Creature speed changes: packed guid + speed.
    for (const uint16 Op : { 0x2FE, 0x2FF, 0x300, 0x301, 0x302, 0x385, 0x386 })
        Handlers.Add(Op, &FWowPacketHandler::HandleSplineSetSpeed);
    // Forced changes to the local mover; each needs a CMSG_*_ACK with MovementInfo.
    for (const uint16 Op : { 0x0E2, 0x0E4, 0x0E6, 0x0E8, 0x0EA, 0x0EF, 0x0DE, 0x0DF, 0x0F2, 0x0F3,
                             0x0F4, 0x0F5, 0x2DA, 0x2DC, 0x2DE, 0x343, 0x344, 0x381, 0x383, 0x45C })
        Handlers.Add(Op, &FWowPacketHandler::HandleForcedMovement);

    // Warden and teleport handlers
    Handlers.Add(WowOpcode::SMSG_WARDEN_DATA,       &FWowPacketHandler::HandleWardenData);
    Handlers.Add(WowOpcode::SMSG_CALENDAR_SEND_CALENDAR,    &FWowPacketHandler::HandleCalendarSendCalendar);
    Handlers.Add(WowOpcode::SMSG_CALENDAR_SEND_NUM_PENDING, &FWowPacketHandler::HandleCalendarSendNumPending);
    Handlers.Add(WowOpcode::MSG_MOVE_TELEPORT,      &FWowPacketHandler::HandleMoveTeleport);
    Handlers.Add(WowOpcode::MSG_MOVE_TELEPORT_ACK, &FWowPacketHandler::HandleMoveTeleportAck);
    Handlers.Add(WowOpcode::SMSG_TRANSFER_PENDING,  &FWowPacketHandler::HandleTransferPending);
    Handlers.Add(WowOpcode::SMSG_NEW_WORLD,         &FWowPacketHandler::HandleNewWorld);

    // ── Player progression handlers ─────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_LEVELUP_INFO,            &FWowPacketHandler::HandleLevelUpInfo);
    Handlers.Add(WowOpcode::SMSG_LOG_XPGAIN,              &FWowPacketHandler::HandleLogXPGain);
    Handlers.Add(WowOpcode::SMSG_EXPLORATION_EXPERIENCE,  &FWowPacketHandler::HandleExplorationExperience);
    Handlers.Add(WowOpcode::SMSG_ENVIRONMENTAL_DAMAGE_LOG, &FWowPacketHandler::HandleEnvironmentalDamageLog);
    Handlers.Add(WowOpcode::SMSG_START_MIRROR_TIMER,      &FWowPacketHandler::HandleStartMirrorTimer);
    Handlers.Add(WowOpcode::SMSG_PAUSE_MIRROR_TIMER,      &FWowPacketHandler::HandlePauseMirrorTimer);
    Handlers.Add(WowOpcode::SMSG_STOP_MIRROR_TIMER,       &FWowPacketHandler::HandleStopMirrorTimer);
    Handlers.Add(WowOpcode::SMSG_BINDPOINTUPDATE,         &FWowPacketHandler::HandleBindPointUpdate);
    Handlers.Add(SMSG_BINDER_CONFIRM,                       &FWowPacketHandler::HandleBinderConfirm); // r527
    Handlers.Add(SMSG_PLAYERBOUND,                          &FWowPacketHandler::HandlePlayerBound);   // r527
    Handlers.Add(SMSG_INITIALIZE_FACTIONS,                  &FWowPacketHandler::HandleInitializeFactions); // r340
    Handlers.Add(SMSG_SET_FACTION_VISIBLE,                  &FWowPacketHandler::HandleSetFactionVisible);  // r340
    Handlers.Add(SMSG_SET_FACTION_STANDING,                 &FWowPacketHandler::HandleSetFactionStanding); // r340
    Handlers.Add(WowOpcode::SMSG_PLAYED_TIME,             &FWowPacketHandler::HandlePlayedTime);

    // ── World state handlers ────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_WEATHER,                 &FWowPacketHandler::HandleWeather);
    Handlers.Add(WowOpcode::SMSG_INIT_WORLD_STATES,       &FWowPacketHandler::HandleInitWorldStates);
    Handlers.Add(WowOpcode::SMSG_UPDATE_WORLD_STATE,      &FWowPacketHandler::HandleUpdateWorldState);

    // ── Account/Party handlers ──────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_SET_PROFICIENCY,         &FWowPacketHandler::HandleSetProficiency);
    Handlers.Add(WowOpcode::SMSG_ACCOUNT_DATA_TIMES,      &FWowPacketHandler::HandleAccountDataTimes);
    Handlers.Add(WowOpcode::SMSG_PARTY_MEMBER_STATS,      &FWowPacketHandler::HandlePartyMemberStats);

    // ── NPC interaction handlers ────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_GOSSIP_MESSAGE,          &FWowPacketHandler::HandleGossipMessage);
    Handlers.Add(WowOpcode::SMSG_GOSSIP_COMPLETE,         &FWowPacketHandler::HandleGossipComplete);
    Handlers.Add(WowOpcode::SMSG_NPC_TEXT_UPDATE,         &FWowPacketHandler::HandleNpcText);

    // ── Mail system handlers ────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_SHOW_MAILBOX,            &FWowPacketHandler::HandleShowMailbox);
    Handlers.Add(WowOpcode::SMSG_MAIL_LIST_RESULT,        &FWowPacketHandler::HandleMailListResult);
    Handlers.Add(WowOpcode::SMSG_SEND_MAIL_RESULT,        &FWowPacketHandler::HandleSendMailResult);
    Handlers.Add(WowOpcode::SMSG_RECEIVED_MAIL,           &FWowPacketHandler::HandleReceivedMail);
    Handlers.Add(WowOpcode::SMSG_QUEST_POI_QUERY_RESPONSE, &FWowPacketHandler::HandleQuestPOIQueryResponse);

    // ── Pet system handlers ─────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_PET_SPELLS,              &FWowPacketHandler::HandlePetSpells);

    // ── Duel system handlers ────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_DUEL_REQUESTED,          &FWowPacketHandler::HandleDuelRequested);
    Handlers.Add(WowOpcode::SMSG_DUEL_OUTOFBOUNDS,        &FWowPacketHandler::HandleDuelOutOfBounds);
    Handlers.Add(WowOpcode::SMSG_DUEL_INBOUNDS,           &FWowPacketHandler::HandleDuelInBounds);
    Handlers.Add(WowOpcode::SMSG_DUEL_COMPLETE,           &FWowPacketHandler::HandleDuelComplete);
    Handlers.Add(WowOpcode::SMSG_DUEL_WINNER,             &FWowPacketHandler::HandleDuelWinner);
    Handlers.Add(WowOpcode::SMSG_DUEL_COUNTDOWN,          &FWowPacketHandler::HandleDuelCountdown);

    // ── Trade system handlers ───────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_TRADE_STATUS,            &FWowPacketHandler::HandleTradeStatus);
    Handlers.Add(WowOpcode::SMSG_TRADE_STATUS_EXTENDED,   &FWowPacketHandler::HandleTradeStatusExtended);

    // ── Bank system handlers ────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_SHOW_BANK,               &FWowPacketHandler::HandleShowBank);

    // Bind Warden response delegate to send packets
    WardenHandler.OnSendResponse.BindLambda([this](uint32 Opcode, const TArray<uint8>& Data)
    {
        SendWardenResponse(Opcode, Data);
    });
}

void FWowPacketHandler::HandlePacket(uint16 Opcode, const TArray<uint8>& Data)
{
    // Validate opcode is in valid WoW 3.3.5a range (max opcode ~0x04F6)
    if (Opcode > 0x0FFF)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("Ignoring out-of-range opcode 0x%04X (%d bytes)"), Opcode, Data.Num());
        return;
    }

    HandlerFunc* Found = Handlers.Find(Opcode);
    if (Found)
    {
        FPacketReader R(Data);
        CurrentOpcode = Opcode;
        (this->**Found)(R);
        if (R.HasFailed())
        {
            UE_LOG(LogWowPacket, Warning,
                TEXT("Rejected malformed opcode 0x%04X (%s), %d bytes: %s"),
                Opcode, WowOpcode::GetName(Opcode), Data.Num(), *R.GetFailureReason());
            return;
        }
    }
    else
    {
        UE_LOG(LogWowPacket, Verbose, TEXT("Unhandled opcode 0x%04X (%s) — %d bytes"),
            Opcode, WowOpcode::GetName(Opcode), Data.Num());
    }

    // Notify subscribers (EventSystem wires this to fire WoW UI events)
    OnOpcodeReceived.Broadcast(Opcode);
}

// ── SMSG_LOGIN_VERIFY_WORLD ──────────────────────────────────────────────────
// uint32 mapId, float x, float y, float z, float orientation

void FWowPacketHandler::HandleLoginVerifyWorld(FPacketReader& R)
{
    uint32 MapId = R.ReadU32();
    float X = R.ReadFloat();
    float Y = R.ReadFloat();
    float Z = R.ReadFloat();
    float O = R.ReadFloat();
    if (R.HasFailed()) return;

    UE_LOG(LogWowPacket, Log, TEXT("LOGIN_VERIFY_WORLD: map=%d pos=(%.1f, %.1f, %.1f) orient=%.2f"),
        MapId, X, Y, Z, O);

    DeathState.CurrentMap = MapId;
    OnLoginVerifyWorld.Broadcast(MapId, X, Y, Z, O);
}

// ── SMSG_UPDATE_OBJECT ───────────────────────────────────────────────────────
// uint32 blockCount, then blockCount update blocks

void FWowPacketHandler::HandleUpdateObject(FPacketReader& R)
{
    uint32 BlockCount = R.ReadU32();

    for (uint32 i = 0; i < BlockCount && R.Remaining() > 0; ++i)
    {
        ParseUpdateBlock(R);
    }

    // Publish only after every block has been applied: player GUID slots can
    // precede the item/container entities they reference in the same packet.
    if (bInventoryUpdatePending)
    {
        bInventoryUpdatePending = false;
        ++VendorRevision;
        RefreshQuestItemCounts(true);
        OnPlayerInventoryUpdate.Broadcast();
    }

    UE_LOG(LogWowPacket, Log, TEXT("UPDATE_OBJECT: %d blocks (total entities: %d created, %d updated, %d destroyed, %d tracked)"),
        BlockCount, EntitiesCreated, EntitiesUpdated, EntitiesDestroyed, EntityManager.Num());
}

// ── SMSG_COMPRESSED_UPDATE_OBJECT ────────────────────────────────────────────
// uint32 decompressedSize, then zlib-compressed UPDATE_OBJECT payload

void FWowPacketHandler::HandleCompressedUpdateObject(FPacketReader& R)
{
    uint32 DecompSize = R.ReadU32();
    int32 CompSize = R.Remaining();

    if (CompSize <= 0 || DecompSize == 0 || DecompSize > 10 * 1024 * 1024)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("COMPRESSED_UPDATE_OBJECT: invalid sizes (comp=%d, decomp=%u)"), CompSize, DecompSize);
        return;
    }

    TArray<uint8> Decompressed;
    Decompressed.SetNumUninitialized(DecompSize);

    // zlib decompress
    int32 UncompSize = static_cast<int32>(DecompSize);
    if (!FCompression::UncompressMemory(NAME_Zlib, Decompressed.GetData(), UncompSize,
            R.Data + R.Pos, CompSize))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("COMPRESSED_UPDATE_OBJECT: zlib decompress failed"));
        return;
    }

    UE_LOG(LogWowPacket, Verbose, TEXT("COMPRESSED_UPDATE_OBJECT: decompressed %d → %d bytes"), CompSize, UncompSize);

    // Parse as regular UPDATE_OBJECT
    FPacketReader DecompReader(Decompressed);
    HandleUpdateObject(DecompReader);
}

// ── Parse a single update block ──────────────────────────────────────────────

void FWowPacketHandler::ParseUpdateBlock(FPacketReader& R)
{
    uint8 Type = R.ReadU8();

    switch (Type)
    {
    case UpdateType::VALUES:
    {
        uint64 Guid = R.ReadPackedGuid();
        FWowEntity* Entity = EntityManager.Find(Guid);
        if (Entity)
        {
            ParseUpdateFields(R, *Entity);
            // r314 (Stefan: a hunter pet showed as a huge worg): the server sets OBJECT_FIELD_SCALE_X
            // in later VALUES updates (pet family scale, size auras). Scale was only read on create,
            // so a pet whose display is a 6.5x boss worg stayed 6.5x instead of being scaled down.
            if (const float NewScale = Entity->GetFieldFloat(ObjectField::SCALE_X); FMath::IsFinite(NewScale) && NewScale > 0.0f)
            {
                if (!FMath::IsNearlyEqual(NewScale, Entity->Scale, 0.001f))
                    UE_LOG(LogWowPacket, Log, TEXT("SCALE_X update: GUID=%llu %.3f -> %.3f"), Guid, Entity->Scale, NewScale);
                Entity->Scale = NewScale;
            }
            if (const uint32 UpdatedTypeMask = Entity->GetField(ObjectField::TYPE); UpdatedTypeMask != 0)
            {
                Entity = &EntityManager.PromoteToTyped(Guid, UpdatedTypeMask);
                Entity->Entry = Entity->GetField(ObjectField::ENTRY);
                Entity->Scale = Entity->GetFieldFloat(ObjectField::SCALE_X);
                if (Entity->Scale == 0.0f)
                {
                    Entity->Scale = 1.0f;
                }
            }

            // Check for player death
            if (Entity->Guid == EntityManager.LocalPlayerGuid && Entity->IsUnit())
            {
                int32 Health = Entity->GetHealth();
                if (Health == 0)
                {
                    UE_LOG(LogWowPacket, Warning, TEXT("Player has died! Health = 0"));
                    OnPlayerDeath.Broadcast();
                }
            }

            EntitiesUpdated++;
            EntityManager.OnEntityUpdated.Broadcast(*Entity);
        }
        else
        {
            UE_LOG(LogWowPacket, Warning, TEXT("VALUES update for unknown GUID %llu"), Guid);
            // Must consume the field block to keep packet aligned
            uint8 SkipBlockCount = R.ReadU8();
            TArray<uint32> SkipMask;
            SkipMask.SetNum(SkipBlockCount);
            for (int32 m = 0; m < SkipBlockCount; ++m)
            {
                SkipMask[m] = R.ReadU32();
            }
            for (int32 Block = 0; Block < SkipBlockCount; ++Block)
            {
                for (int32 Bit = 0; Bit < 32; ++Bit)
                {
                    if (SkipMask[Block] & (1u << Bit))
                    {
                        R.Skip(4);
                    }
                }
            }
            break;
        }
        break;
    }

    case UpdateType::MOVEMENT:
    {
        uint64 Guid = R.ReadPackedGuid();
        FWowEntity& Entity = EntityManager.GetOrCreate(Guid);
        ParseObjectMovement(R, Entity.Movement);
        EntityManager.OnEntityUpdated.Broadcast(Entity);
        break;
    }

    case UpdateType::CREATE_OBJECT:
    case UpdateType::CREATE_OBJECT2:
    {
        uint64 Guid = R.ReadPackedGuid();
        uint8 ObjTypeId = R.ReadU8();

        FWowEntity& BaseEntity = EntityManager.GetOrCreate(Guid);
        BaseEntity.ObjectTypeId = ObjTypeId;

        ParseObjectMovement(R, BaseEntity.Movement);

        // Update fields
        ParseUpdateFields(R, BaseEntity);

        // Extract common fields
        BaseEntity.TypeMask = BaseEntity.GetField(ObjectField::TYPE);
        BaseEntity.Entry = BaseEntity.GetField(ObjectField::ENTRY);
        BaseEntity.Scale = BaseEntity.GetFieldFloat(ObjectField::SCALE_X);
        if (BaseEntity.Scale == 0.0f)
        {
            BaseEntity.Scale = 1.0f;
        }

        FWowEntity& Entity = EntityManager.PromoteToTyped(Guid, BaseEntity.TypeMask);
        Entity.ObjectTypeId = ObjTypeId;
        Entity.Entry = Entity.GetField(ObjectField::ENTRY);
        Entity.Scale = Entity.GetFieldFloat(ObjectField::SCALE_X);
        if (Entity.Scale == 0.0f)
        {
            Entity.Scale = 1.0f;
        }
        if (!FMath::IsNearlyEqual(Entity.Scale, 1.0f, 0.001f))
            UE_LOG(LogWowPacket, Log, TEXT("SCALE_X on create: GUID=%llu scale=%.3f"), Guid, Entity.Scale);

        EntitiesCreated++;
        EntityManager.OnEntityCreated.Broadcast(Entity);

        UE_LOG(LogWowPacket, Verbose, TEXT("Created %s GUID=%llu type=%d entry=%u pos=(%.1f,%.1f,%.1f)"),
            Entity.GetEntityKindName(), Guid, ObjTypeId, Entity.Entry,
            Entity.Movement.Position.X, Entity.Movement.Position.Y, Entity.Movement.Position.Z);
        break;
    }

    case UpdateType::OUT_OF_RANGE_OBJECTS:
    {
        uint32 Count = R.ReadU32();
        for (uint32 i = 0; i < Count && R.Remaining() > 0; ++i)
        {
            uint64 Guid = R.ReadPackedGuid();
            EntityManager.Remove(Guid);
            EntitiesDestroyed++;
        }
        UE_LOG(LogWowPacket, Verbose, TEXT("OUT_OF_RANGE: %d objects removed"), Count);
        break;
    }

    case UpdateType::NEAR_OBJECTS:
    {
        uint32 Count = R.ReadU32();
        for (uint32 i = 0; i < Count && R.Remaining() > 0; ++i)
        {
            R.ReadPackedGuid(); // just acknowledge
        }
        break;
    }

    default:
        UE_LOG(LogWowPacket, Warning, TEXT("Unknown update block type: %d (remaining: %d bytes) - skipping rest of packet to avoid corruption"),
            Type, R.Remaining());

        // Unknown block type - we can't safely parse this packet any further
        // Skip to end of packet to avoid reading corrupted data
        R.Skip(R.Remaining());
        return;
    }
}

// ── Parse movement info for LIVING entities ──────────────────────────────────

void FWowPacketHandler::ParseObjectMovement(FPacketReader& R, FWowMovementInfo& Out)
{
    const uint16 Flags=R.ReadU16();
    if (Flags & UpdateFlag::LIVING) ParseMovementInfo(R,Out);
    else if (Flags & UpdateFlag::POSITION)
    {
        Out.TransportGuid=R.ReadPackedGuid();
        Out.Position.X=R.ReadFloat(); Out.Position.Y=R.ReadFloat(); Out.Position.Z=R.ReadFloat();
        Out.TransportOffset.X=R.ReadFloat(); Out.TransportOffset.Y=R.ReadFloat(); Out.TransportOffset.Z=R.ReadFloat();
        Out.Orientation=R.ReadFloat();
        R.ReadFloat(); // Corpse facing (zero for other object types).
    }
    else if (Flags & UpdateFlag::STATIONARY_POSITION)
    {
        Out.Position.X=R.ReadFloat(); Out.Position.Y=R.ReadFloat(); Out.Position.Z=R.ReadFloat();
        Out.Orientation=R.ReadFloat();
    }
    // Both CREATE and MOVEMENT carry this same trailer in build 12340.
    if (Flags & 0x0008) R.ReadU32(); // UPDATEFLAG_UNKNOWN
    if (Flags & UpdateFlag::LOWGUID) R.ReadU32();
    if (Flags & UpdateFlag::HAS_TARGET) R.ReadPackedGuid();
    // r598: the transport path timer (ms). Boats/zeppelins and elevators are moved by the client from it.
    if (Flags & UpdateFlag::TRANSPORT) { Out.TransportTimer = R.ReadU32(); Out.TransportTimerAt = FPlatformTime::Seconds(); Out.bHasTransportTimer = true; }
    if (Flags & UpdateFlag::VEHICLE) { R.ReadU32(); R.ReadFloat(); }
    if (Flags & UpdateFlag::ROTATION)
    {
        // Packed quaternion (3.3.5): x in the top 22 bits, y and z in 21 bits each, w implied
        // non-negative. Signpost arms and other tilted game objects need it (r285).
        const int64 Packed = int64(R.ReadU64());
        const double X = double(Packed >> 42) / double(1 << 21);
        const double Y = double((Packed << 22) >> 43) / double(1 << 20);
        const double Z = double((Packed << 43) >> 43) / double(1 << 20);
        const double W2 = 1.0 - (X * X + Y * Y + Z * Z);
        if (FMath::IsFinite(W2) && W2 >= -0.01)
        {
            Out.PackedRotation = FQuat(X, Y, Z, FMath::Sqrt(FMath::Max(0.0, W2)));
            Out.PackedRotation.Normalize();
            Out.bHasPackedRotation = true;
        }
    }
}

void FWowPacketHandler::ParseMovementInfo(FPacketReader& R, FWowMovementInfo& Out)
{
    Out.MoveFlags = R.ReadU32();
    Out.MoveFlags2 = R.ReadU16();
    Out.MovementTimestamp = R.ReadU32();
    Out.MovementReceivedAtSeconds = FPlatformTime::Seconds();

    Out.Position.X = R.ReadFloat();
    Out.Position.Y = R.ReadFloat();
    Out.Position.Z = R.ReadFloat();
    Out.Orientation = R.ReadFloat();

    // Transport
    if (Out.MoveFlags & 0x00000200) // MOVEMENTFLAG_ONTRANSPORT
    {
        Out.TransportGuid = R.ReadPackedGuid();
        Out.TransportOffset.X = R.ReadFloat();
        Out.TransportOffset.Y = R.ReadFloat();
        Out.TransportOffset.Z = R.ReadFloat();
        Out.TransportOrientation = R.ReadFloat();
        R.ReadU32(); // transport time
        R.ReadU8();  // transport seat

        if (Out.MoveFlags2 & 0x0400) // MOVEMENTFLAG2_INTERPOLATED_MOVEMENT
        {
            R.ReadU32(); // transport time 2
        }
    }

    // Swimming / Flying
    if ((Out.MoveFlags & (0x00200000 | 0x02000000)) || (Out.MoveFlags2 & 0x0020)) // SWIMMING | FLYING | ALWAYS_ALLOW_PITCHING
    {
        R.ReadFloat(); // pitch
    }

    Out.FallTime = R.ReadU32();

    // Falling
    if (Out.MoveFlags & 0x00001000) // FALLING
    {
        Out.JumpZSpeed = R.ReadFloat(); // jump vertical velocity
        R.ReadFloat(); // jump sin
        R.ReadFloat(); // jump cos
        R.ReadFloat(); // jump xy speed
    }

    // Spline elevation
    if (Out.MoveFlags & 0x04000000) // SPLINE_ELEVATION
    {
        R.ReadFloat();
    }

    // Speeds (9 speeds for living entities)
    Out.WalkSpeed = R.ReadFloat();
    Out.RunSpeed = R.ReadFloat();
    Out.RunBackSpeed = R.ReadFloat();
    Out.SwimSpeed = R.ReadFloat();
    Out.SwimBackSpeed = R.ReadFloat();
    Out.FlightSpeed = R.ReadFloat();
    Out.FlightBackSpeed = R.ReadFloat();
    Out.TurnRate = R.ReadFloat();
    Out.PitchRate = R.ReadFloat();

    // Spline data embedded in CREATE_OBJECT for units already moving when they stream in.
    if (Out.MoveFlags & 0x08000000) // SPLINE_ENABLED
    {
        constexpr uint32 SplineFinalPoint = 0x00008000;
        constexpr uint32 SplineFinalTarget = 0x00010000;
        constexpr uint32 SplineFinalAngle = 0x00020000;
        constexpr uint32 SplineCyclic = 0x00080000;

        Out.SplineFlags = R.ReadU32();
        Out.SplineMoveType = 0;
        Out.SplineFacingPoint = FVector::ZeroVector;
        Out.SplineFacingTargetGuid = 0;
        Out.SplineFacingAngle = Out.Orientation;

        if (Out.SplineFlags & SplineFinalAngle)
        {
            Out.SplineMoveType = 4;
            Out.SplineFacingAngle = R.ReadFloat();
        }
        else if (Out.SplineFlags & SplineFinalTarget)
        {
            Out.SplineMoveType = 3;
            Out.SplineFacingTargetGuid = R.ReadU64();
        }
        else if (Out.SplineFlags & SplineFinalPoint)
        {
            Out.SplineMoveType = 2;
            const float FaceX = R.ReadFloat();
            const float FaceY = R.ReadFloat();
            const float FaceZ = R.ReadFloat();
            Out.SplineFacingPoint = FVector(FaceX, FaceY, FaceZ);
        }

        const uint32 SplineTimePassed = R.ReadU32();
        Out.SplineDuration = R.ReadU32();
        Out.SplineId = R.ReadU32();

        R.ReadFloat(); // spline duration multiplier
        R.ReadFloat(); // spline duration multiplier next

        R.ReadFloat(); // spline vertical acceleration
        R.ReadU32(); // spline effect start time

        const uint32 RawSplineCount = R.ReadU32();
        const uint32 SplineCount = FMath::Min(RawSplineCount, 500u);
        const int32 BytesAvailableForPoints = FMath::Max(0, R.Remaining() - 13);
        const uint32 ReadableSplineCount = FMath::Min(
            SplineCount, static_cast<uint32>(BytesAvailableForPoints / 12));
        Out.SplineWaypoints.Empty();
        Out.SplineStartPosition = Out.Position;
        for (uint32 PointIndex = 0; PointIndex < ReadableSplineCount; ++PointIndex)
        {
            const float PointX = R.ReadFloat();
            const float PointY = R.ReadFloat();
            const float PointZ = R.ReadFloat();
            Out.SplineWaypoints.Add(FWowSplineWaypoint(FVector(PointX, PointY, PointZ)));
        }

        const uint32 UnparsedPointCount = RawSplineCount - ReadableSplineCount;
        const uint64 UnparsedPointBytes = static_cast<uint64>(UnparsedPointCount) * 12u;
        const int32 SkippablePointBytes = FMath::Max(0, R.Remaining() - 13);
        R.Skip(static_cast<int32>(FMath::Min<uint64>(UnparsedPointBytes, SkippablePointBytes)));

        if (!R.CanRead(13))
        {
            Out.bHasActiveSpline = false;
            return;
        }

        R.ReadU8(); // spline mode

        const float EndpointX = R.ReadFloat();
        const float EndpointY = R.ReadFloat();
        const float EndpointZ = R.ReadFloat();
        const FVector Endpoint(EndpointX, EndpointY, EndpointZ);
        if ((Out.SplineFlags & SplineCyclic) == 0 &&
            !Endpoint.ContainsNaN() &&
            (Out.SplineWaypoints.Num() == 0 || !Out.SplineWaypoints.Last().Position.Equals(Endpoint, 0.01f)))
        {
            Out.SplineWaypoints.Add(FWowSplineWaypoint(Endpoint));
        }

        Out.UpdateSplineSpeed();
        Out.SplineElapsed = static_cast<float>(FMath::Min(SplineTimePassed, Out.SplineDuration));
        Out.bHasActiveSpline = Out.SplineDuration > SplineTimePassed && Out.SplineWaypoints.Num() > 0;
    }
}

// ── Parse bitmask-based update fields ────────────────────────────────────────

void FWowPacketHandler::ParseUpdateFields(FPacketReader& R, FWowEntity& Entity)
{
    // The update fields block starts with a count of uint32 bitmask blocks
    uint8 BlockCount = R.ReadU8();

    if (BlockCount == 0) return;

    // Read the bitmask
    TArray<uint32> Mask;
    Mask.SetNum(BlockCount);
    for (int32 i = 0; i < BlockCount; ++i)
    {
        Mask[i] = R.ReadU32();
    }

    bool bInventoryFieldUpdated = false;
    bool bHealthFieldUpdated = false;
    bool bPowerFieldUpdated = false;
    bool bQuestLogFieldUpdated = false;

    // Each set bit in the mask means a uint32 value follows
    for (int32 Block = 0; Block < BlockCount; ++Block)
    {
        for (int32 Bit = 0; Bit < 32; ++Bit)
        {
            if (Mask[Block] & (1u << Bit))
            {
                uint16 FieldIndex = static_cast<uint16>(Block * 32 + Bit);
                uint32 Value = R.ReadU32();
                Entity.SetField(FieldIndex, Value);

                if (Entity.Guid == EntityManager.LocalPlayerGuid &&
                    FieldIndex >= PlayerField::QUEST_LOG_START &&
                    FieldIndex <= PlayerField::QUEST_LOG_END)
                {
                    bQuestLogFieldUpdated = true;
                }

                // Check if this is a player inventory field update
                if (Entity.Guid == EntityManager.LocalPlayerGuid &&
                    ((FieldIndex >= PlayerField::INV_SLOT_HEAD && FieldIndex <= PlayerField::INV_SLOT_LAST + 1) ||
                     (FieldIndex >= PlayerField::PACK_SLOT_START && FieldIndex <= PlayerField::PACK_SLOT_END) ||
                     (FieldIndex >= PlayerField::BANK_SLOT_START && FieldIndex <= PlayerField::BANK_SLOT_END) ||
                     (FieldIndex >= PlayerField::BANKBAG_SLOT_1 && FieldIndex <= PlayerField::BANKBAG_SLOT_END) ||
                     (FieldIndex >= PlayerField::BUYBACK_SLOT_START && FieldIndex < PlayerField::BUYBACK_SLOT_START + 24) ||
                     (FieldIndex >= PlayerField::BUYBACK_PRICE_START && FieldIndex < PlayerField::BUYBACK_TIME_START + 12) ||
                     FieldIndex == PlayerField::BYTES_2 || FieldIndex == PlayerField::COINAGE))
                {
                    bInventoryFieldUpdated = true;
                }

                // Check if this is a unit health field update
                if (Entity.IsUnit() && (FieldIndex == UnitField::HEALTH || FieldIndex == UnitField::MAXHEALTH))
                {
                    bHealthFieldUpdated = true;
                }

                // Check if this is a unit power field update (covers all 7 power types: mana, rage, focus, energy, happiness, runes, runic power)
                if (Entity.IsUnit() && ((FieldIndex >= UnitField::POWER1 && FieldIndex <= UnitField::POWER7) ||
                                        (FieldIndex >= UnitField::MAXPOWER1 && FieldIndex <= UnitField::MAXPOWER7)))
                {
                    bPowerFieldUpdated = true;
                }
            }
        }
    }

    // Item stack, binding, durability and container-slot changes also affect UI.
    // CREATE blocks may still have a base entity until their fields are parsed.
    const uint32 TypeMask = Entity.GetField(ObjectField::TYPE);
    const bool bOwnedItem = (Entity.IsItem() || (TypeMask & WowTypeMask::ITEM) != 0) &&
        Entity.GetField64(ItemField::OWNER) == EntityManager.LocalPlayerGuid &&
        EntityManager.LocalPlayerGuid != 0;
    bInventoryUpdatePending |= bInventoryFieldUpdated || bOwnedItem;

    // Fire health update event if unit health fields were updated
    if (bHealthFieldUpdated)
    {
        OnUnitHealthUpdate.Broadcast(Entity.Guid);
    }

    // Fire power update event if unit power fields were updated
    if (bPowerFieldUpdated)
    {
        OnUnitPowerUpdate.Broadcast(Entity.Guid);
    }

    if (bQuestLogFieldUpdated)
    {
        RebuildQuestLogFromPlayerFields(Entity);
    }
}

void FWowPacketHandler::RefreshQuestItemCounts(bool bNotifyProgress)
{
    const FWowPlayerEntity* Player = EntityManager.GetLocalPlayer();
    if (!Player) return;
    TMap<uint32, uint32> Counts;
    TSet<uint64> Seen;
    bool bCompleteSnapshot = true;
    auto CountItem = [&](uint64 Guid)
    {
        if (!Guid || Seen.Contains(Guid)) return;
        Seen.Add(Guid);
        const FWowItemEntity* Item = EntityManager.FindItem(Guid);
        if (!Item) { bCompleteSnapshot = false; return; }
        Counts.FindOrAdd(Item->Entry) += Item->GetStackCount();
    };
    for (uint8 Slot = 0; Slot < 16; ++Slot) CountItem(Player->GetBackpackItemGuid(Slot));
    for (uint8 Bag = 1; Bag <= 4; ++Bag)
    {
        const uint64 Guid = Player->GetBagGuid(Bag);
        if (!Guid) continue;
        const FWowContainerEntity* Container = EntityManager.FindContainer(Guid);
        if (!Container) { bCompleteSnapshot = false; continue; }
        for (int32 Slot = 0; Slot < Container->GetNumSlots(); ++Slot)
            CountItem(Container->GetItemGuidAtSlot(Slot));
    }
    // A referenced item may arrive in a subsequent packet. Do not momentarily
    // reset progress and then repeat the pickup notification when it arrives.
    if (!bCompleteSnapshot) return;
    bool bChanged = false;
    for (FWowQuestLogEntry& Quest : QuestLog)
    {
        bool bHasItemObjective = false;
        for (FWowQuestObjective& Objective : Quest.Objectives)
        {
            if (!Objective.ItemId || !Objective.Required) continue;
            bHasItemObjective = true;
            const uint32 Count = FMath::Min(Counts.FindRef(Objective.ItemId), Objective.Required);
            if (Count == Objective.Count) continue;
            if (bNotifyProgress && InitializedItemQuests.Contains(Quest.QuestId) && Count > Objective.Count)
            {
                const FWowItemTemplateInfo* Item = ItemTemplates.Find(Objective.ItemId);
                const FString Label = Item ? Item->Name : Objective.Text;
                OnUiInfoMessage.Broadcast(FString::Printf(TEXT("%s: %u/%u"),
                    Label.IsEmpty() ? TEXT("Quest item") : *Label, Count, Objective.Required));
            }
            Objective.Count = Count;
            bChanged = true;
        }
        if (bHasItemObjective) InitializedItemQuests.Add(Quest.QuestId);
    }
    if (bChanged) OnQuestLogUpdated.Broadcast();
}

void FWowPacketHandler::RebuildQuestLogFromPlayerFields(const FWowEntity& PlayerEntity)
{
    TMap<uint32, bool> TrackedByQuestId;
    TMap<uint32, FWowQuestLogEntry> ExistingByQuestId;
    for (const FWowQuestLogEntry& Existing : QuestLog)
    {
        TrackedByQuestId.Add(Existing.QuestId, Existing.bIsTracked);
        ExistingByQuestId.Add(Existing.QuestId, Existing);
    }

    TArray<FWowQuestLogEntry> RebuiltLog;
    RebuiltLog.Reserve(PlayerField::QUEST_LOG_SLOT_COUNT);

    for (uint16 SlotIndex = 0; SlotIndex < PlayerField::QUEST_LOG_SLOT_COUNT; ++SlotIndex)
    {
        const uint16 SlotBase = PlayerField::QUEST_LOG_START +
            SlotIndex * PlayerField::QUEST_LOG_FIELDS_PER_SLOT;
        const uint32 QuestId = PlayerEntity.GetField(SlotBase);
        if (QuestId == 0)
        {
            continue;
        }

        FWowQuestLogEntry Entry;
        if (const FWowQuestLogEntry* Existing = ExistingByQuestId.Find(QuestId))
        {
            Entry = *Existing;
        }
        else if (const FWowQuestLogEntry* Template = QuestTemplateCache.Find(QuestId))
        {
            Entry = *Template;
        }

        Entry.QuestId = QuestId;
        Entry.bIsTracked = TrackedByQuestId.FindRef(QuestId);

        const uint32 QuestState = PlayerEntity.GetField(SlotBase + 1);
        Entry.State = (QuestState & 0x00000002u) != 0 ? 2u
            : ((QuestState & 0x00000001u) != 0 ? 1u : 0u);

        Entry.ObjectiveCounts.SetNumZeroed(4);
        const uint32 PackedCountsA = PlayerEntity.GetField(SlotBase + 2);
        const uint32 PackedCountsB = PlayerEntity.GetField(SlotBase + 3);
        Entry.ObjectiveCounts[0] = PackedCountsA & 0xFFFFu;
        Entry.ObjectiveCounts[1] = (PackedCountsA >> 16) & 0xFFFFu;
        Entry.ObjectiveCounts[2] = PackedCountsB & 0xFFFFu;
        Entry.ObjectiveCounts[3] = (PackedCountsB >> 16) & 0xFFFFu;

        int32 CreatureObjectiveIndex = 0;
        for (FWowQuestObjective& Objective : Entry.Objectives)
        {
            if (Objective.CreatureOrGOId != 0 && CreatureObjectiveIndex < Entry.ObjectiveCounts.Num())
            {
                Objective.Count = Entry.ObjectiveCounts[CreatureObjectiveIndex++];
            }
        }

        RebuiltLog.Add(MoveTemp(Entry));

        if (!QuestTemplateCache.Contains(QuestId) && !PendingQuestTemplateQueries.Contains(QuestId))
        {
            TArray<uint8> QueryData;
            QueryData.Append(reinterpret_cast<const uint8*>(&QuestId), sizeof(QuestId));
            PendingQuestTemplateQueries.Add(QuestId);
            if (OnSendPacket.IsBound())
            {
                OnSendPacket.Execute(WowOpcode::CMSG_QUEST_QUERY, QueryData);
            }
        }
    }

    TArray<uint32> Accepted;
    for (const FWowQuestLogEntry& Entry : RebuiltLog)
    {
        if (!ExistingByQuestId.Contains(Entry.QuestId)) Accepted.Add(Entry.QuestId);
    }
    QuestLog = MoveTemp(RebuiltLog);
    bInventoryUpdatePending = true;
    RequestQuestPOIs();
    OnQuestLogUpdated.Broadcast();
    for (uint32 QuestId : Accepted) OnQuestAccepted.Broadcast(QuestId);
}

FString FWowPacketHandler::ResolveQuestCategoryName(int32 ZoneOrSort) const
{
    if (ZoneOrSort > 0)
    {
        if (const FAreaTableDbcEntry* Area = FDbcStore::Get().AreaTable().GetById(
            static_cast<uint32>(ZoneOrSort)))
        {
            return Area->Name;
        }
        return FString::Printf(TEXT("Zone %d"), ZoneOrSort);
    }

    if (ZoneOrSort < 0)
    {
        if (const FQuestSortDbcEntry* Sort = FDbcStore::Get().QuestSorts().GetById(
            static_cast<uint32>(-ZoneOrSort)))
        {
            return Sort->Name;
        }
        return FString::Printf(TEXT("Quest Category %d"), -ZoneOrSort);
    }

    return TEXT("Miscellaneous");
}

// ── SMSG_DESTROY_OBJECT ──────────────────────────────────────────────────────

void FWowPacketHandler::HandleDestroyObject(FPacketReader& R)
{
    uint64 Guid = R.ReadU64();
    uint8 OnDeath = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("DESTROY_OBJECT: GUID=%llu onDeath=%d"), Guid, OnDeath);

    // onDeath=1 means the CORPSE is being removed (~60s after death), not the death itself.
    // Death animation is triggered by health→0 in OnEntityUpdated.
    // Always remove from entity manager — the gameplay controller's OnEntityDestroyed
    // will clean up the actor.
    EntityManager.Remove(Guid);
    EntitiesDestroyed++;
}

// ── Movement packets ─────────────────────────────────────────────────────────

void FWowPacketHandler::HandleMovement(FPacketReader& R)
{
    const uint64 Guid = R.ReadPackedGuid();
    FWowEntity* Entity = EntityManager.Find(Guid);
    if (!Entity || R.HasFailed()) return;
    FWowMovementInfo Info = Entity->Movement; // retain server-supplied movement speeds
    uint32 Timestamp = 0;
    if (!ReadMovementSnapshot(R, Info, Timestamp)) return;
    Info.bHasActiveSpline = false;
    Info.SplineWaypoints.Reset();
    Entity->Movement = MoveTemp(Info);
    // Render ticks consume movement directly. Movement packets do not change
    // appearance, health or inventory; avoid running those expensive UI paths.
    if (Guid == EntityManager.LocalPlayerGuid)
        EntityManager.OnEntityUpdated.Broadcast(*Entity);
}

namespace
{
float* MovementSpeedField(FWowMovementInfo& Movement, EWowSpeedType Type)
{
    switch (Type)
    {
    case EWowSpeedType::Walk:       return &Movement.WalkSpeed;
    case EWowSpeedType::Run:        return &Movement.RunSpeed;
    case EWowSpeedType::RunBack:    return &Movement.RunBackSpeed;
    case EWowSpeedType::Swim:       return &Movement.SwimSpeed;
    case EWowSpeedType::SwimBack:   return &Movement.SwimBackSpeed;
    case EWowSpeedType::TurnRate:   return &Movement.TurnRate;
    case EWowSpeedType::Flight:     return &Movement.FlightSpeed;
    case EWowSpeedType::FlightBack: return &Movement.FlightBackSpeed;
    case EWowSpeedType::PitchRate:  return &Movement.PitchRate;
    }
    return nullptr;
}

bool SpeedTypeForOpcode(uint16 Op, EWowSpeedType& Out)
{
    switch (Op)
    {
    // MSG_MOVE_SET_*_SPEED
    case 0x0CD: Out = EWowSpeedType::Run; return true;
    case 0x0CF: Out = EWowSpeedType::RunBack; return true;
    case 0x0D1: Out = EWowSpeedType::Walk; return true;
    case 0x0D3: Out = EWowSpeedType::Swim; return true;
    case 0x0D5: Out = EWowSpeedType::SwimBack; return true;
    case 0x0D8: Out = EWowSpeedType::TurnRate; return true;
    case 0x37E: Out = EWowSpeedType::Flight; return true;
    case 0x380: Out = EWowSpeedType::FlightBack; return true;
    case 0x45B: Out = EWowSpeedType::PitchRate; return true;
    // SMSG_SPLINE_SET_*_SPEED
    case 0x2FE: Out = EWowSpeedType::Run; return true;
    case 0x2FF: Out = EWowSpeedType::RunBack; return true;
    case 0x300: Out = EWowSpeedType::Swim; return true;
    case 0x301: Out = EWowSpeedType::Walk; return true;
    case 0x302: Out = EWowSpeedType::SwimBack; return true;
    case 0x385: Out = EWowSpeedType::Flight; return true;
    case 0x386: Out = EWowSpeedType::FlightBack; return true;
    // SMSG_FORCE_*_SPEED_CHANGE
    case 0x0E2: Out = EWowSpeedType::Run; return true;
    case 0x0E4: Out = EWowSpeedType::RunBack; return true;
    case 0x0E6: Out = EWowSpeedType::Swim; return true;
    case 0x2DA: Out = EWowSpeedType::Walk; return true;
    case 0x2DC: Out = EWowSpeedType::SwimBack; return true;
    case 0x2DE: Out = EWowSpeedType::TurnRate; return true;
    case 0x381: Out = EWowSpeedType::Flight; return true;
    case 0x383: Out = EWowSpeedType::FlightBack; return true;
    case 0x45C: Out = EWowSpeedType::PitchRate; return true;
    default: return false;
    }
}
}

void FWowPacketHandler::HandleMoveSetSpeed(FPacketReader& R)
{
    const uint64 Guid = R.ReadPackedGuid();
    FWowEntity* Entity = EntityManager.Find(Guid);
    if (!Entity || R.HasFailed()) return;
    FWowMovementInfo Info = Entity->Movement;
    uint32 Timestamp = 0;
    if (!ReadMovementSnapshot(R, Info, Timestamp)) return;
    const float Speed = R.ReadFloat();
    if (R.HasFailed()) return;
    EWowSpeedType Type;
    if (SpeedTypeForOpcode(CurrentOpcode, Type) && FMath::IsFinite(Speed) && Speed >= 0.0f)
        if (float* Field = MovementSpeedField(Info, Type)) *Field = Speed;
    Info.bHasActiveSpline = false;
    Info.SplineWaypoints.Reset();
    Entity->Movement = MoveTemp(Info);
}

void FWowPacketHandler::HandleSplineSetSpeed(FPacketReader& R)
{
    const uint64 Guid = R.ReadPackedGuid();
    const float Speed = R.ReadFloat();
    if (R.HasFailed() || !FMath::IsFinite(Speed) || Speed < 0.0f) return;
    FWowEntity* Entity = EntityManager.Find(Guid);
    EWowSpeedType Type;
    if (Entity && SpeedTypeForOpcode(CurrentOpcode, Type))
        if (float* Field = MovementSpeedField(Entity->Movement, Type)) *Field = Speed;
}

void FWowPacketHandler::HandleForcedMovement(FPacketReader& R)
{
    FWowForcedMovement Change;
    const uint64 Guid = R.ReadPackedGuid();
    Change.Counter = R.ReadU32();
    const uint16 Op = CurrentOpcode;
    switch (Op)
    {
    case 0x0E2: // SMSG_FORCE_RUN_SPEED_CHANGE has an extra u8 before the speed.
        R.ReadU8();
        Change.Kind = EWowForcedMoveKind::Speed;
        Change.Speed = R.ReadFloat();
        Change.AckOpcode = 0x0E3;
        break;
    case 0x0E4: Change.Kind = EWowForcedMoveKind::Speed; Change.Speed = R.ReadFloat(); Change.AckOpcode = 0x0E5; break;
    case 0x0E6: Change.Kind = EWowForcedMoveKind::Speed; Change.Speed = R.ReadFloat(); Change.AckOpcode = 0x0E7; break;
    case 0x2DA: Change.Kind = EWowForcedMoveKind::Speed; Change.Speed = R.ReadFloat(); Change.AckOpcode = 0x2DB; break;
    case 0x2DC: Change.Kind = EWowForcedMoveKind::Speed; Change.Speed = R.ReadFloat(); Change.AckOpcode = 0x2DD; break;
    case 0x2DE: Change.Kind = EWowForcedMoveKind::Speed; Change.Speed = R.ReadFloat(); Change.AckOpcode = 0x2DF; break;
    case 0x381: Change.Kind = EWowForcedMoveKind::Speed; Change.Speed = R.ReadFloat(); Change.AckOpcode = 0x382; break;
    case 0x383: Change.Kind = EWowForcedMoveKind::Speed; Change.Speed = R.ReadFloat(); Change.AckOpcode = 0x384; break;
    case 0x45C: Change.Kind = EWowForcedMoveKind::Speed; Change.Speed = R.ReadFloat(); Change.AckOpcode = 0x45D; break;
    case 0x0E8: Change.Kind = EWowForcedMoveKind::Root;        Change.AckOpcode = 0x0E9; break;
    case 0x0EA: Change.Kind = EWowForcedMoveKind::Unroot;      Change.AckOpcode = 0x0EB; break;
    case 0x0DE: Change.Kind = EWowForcedMoveKind::WaterWalk;   Change.AckOpcode = 0x2D0; break;
    case 0x0DF: Change.Kind = EWowForcedMoveKind::LandWalk;    Change.AckOpcode = 0x2D0; break;
    case 0x0F2: Change.Kind = EWowForcedMoveKind::FeatherFall; Change.AckOpcode = 0x2CF; break;
    case 0x0F3: Change.Kind = EWowForcedMoveKind::NormalFall;  Change.AckOpcode = 0x2CF; break;
    case 0x0F4: Change.Kind = EWowForcedMoveKind::Hover;       Change.AckOpcode = 0x0F6; break;
    case 0x0F5: Change.Kind = EWowForcedMoveKind::Unhover;     Change.AckOpcode = 0x0F6; break;
    case 0x343: Change.Kind = EWowForcedMoveKind::SetCanFly;   Change.AckOpcode = 0x345; break;
    case 0x344: Change.Kind = EWowForcedMoveKind::UnsetCanFly; Change.AckOpcode = 0x345; break;
    case 0x0EF:
        Change.Kind = EWowForcedMoveKind::KnockBack;
        Change.KnockCos = R.ReadFloat();
        Change.KnockSin = R.ReadFloat();
        Change.KnockSpeedXY = R.ReadFloat();
        // The server writes -speedZ; positive vertical speed means upwards.
        Change.KnockSpeedZ = -R.ReadFloat();
        Change.AckOpcode = 0x0F0;
        break;
    default:
        return;
    }
    if (R.HasFailed()) return;
    if (Guid != EntityManager.LocalPlayerGuid) return; // forced messages are only sent to the mover

    if (Change.Kind == EWowForcedMoveKind::Speed)
    {
        if (!FMath::IsFinite(Change.Speed) || Change.Speed < 0.0f) return;
        SpeedTypeForOpcode(Op, Change.SpeedType);
        if (FWowEntity* Entity = EntityManager.Find(Guid))
        {
            if (float* Field = MovementSpeedField(Entity->Movement, Change.SpeedType)) *Field = Change.Speed;
            EntityManager.OnEntityUpdated.Broadcast(*Entity);
        }
    }
    UE_LOG(LogWowPacket, Log, TEXT("Forced movement 0x%03X counter=%u speed=%.3f"), Op, Change.Counter, Change.Speed);
    OnForcedMovement.Broadcast(Change);
}

// ── SMSG_MESSAGECHAT ─────────────────────────────────────────────────────────

void FWowPacketHandler::HandleMessageChat(FPacketReader& R)
{
    // Build 12340 layout (ChatHandler::BuildChatPacket):
    // u8 type, i32 language, u64 sender, u32 flags, <type specific>, u32 len, text, u8 chatTag
    const uint8 Type = R.ReadU8();
    const uint32 Language = R.ReadU32();
    const uint64 SenderGuid = R.ReadU64();
    R.ReadU32(); // flags

    FString Channel;
    FString InlineSenderName;
    auto ReadSizedString = [&R]() -> FString
    {
        const uint32 Len = R.ReadU32();
        if (R.HasFailed() || Len == 0) return FString();
        if (!R.CanRead(static_cast<int32>(FMath::Min<uint32>(Len, 0x7FFFFFFF))))
        {
            R.Fail(TEXT("Chat string exceeds packet"));
            return FString();
        }
        return R.ReadCString();
    };
    auto IsPlayerOrPetGuid = [](uint64 Guid)
    {
        const uint32 High = static_cast<uint32>(Guid >> 48);
        return Guid != 0 && (High == 0x0000 || High == 0xF140 || High == 0xF040);
    };

    switch (Type)
    {
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: // MONSTER_SAY/PARTY/YELL/WHISPER/EMOTE
    case 0x29: case 0x2A: case 0x2F:                       // RAID_BOSS_EMOTE/WHISPER, BATTLENET
    {
        InlineSenderName = ReadSizedString();
        const uint64 Receiver = R.ReadU64();
        if (Receiver != 0 && !IsPlayerOrPetGuid(Receiver)) ReadSizedString(); // receiver name
        break;
    }
    case 0x08: // WHISPER_FOREIGN
        InlineSenderName = ReadSizedString();
        R.ReadU64();
        break;
    case 0x24: case 0x25: case 0x26: // BG_SYSTEM_NEUTRAL/ALLIANCE/HORDE
    {
        const uint64 Receiver = R.ReadU64();
        if (Receiver != 0 && !IsPlayerOrPetGuid(Receiver)) ReadSizedString();
        break;
    }
    case 0x30: case 0x31: // ACHIEVEMENT / GUILD_ACHIEVEMENT
        R.ReadU64();
        break;
    default:
        if (Type == 0x11) Channel = R.ReadCString(); // CHANNEL
        R.ReadU64(); // receiver / target guid
        break;
    }

    // LANG_ADDON text is "prefix<TAB>payload" with an arbitrary-byte payload: keep it byte for byte.
    FString Message;
    if (Language == 0xFFFFFFFFu)
    {
        const uint32 Len = R.ReadU32();
        if (!R.HasFailed() && Len != 0)
        {
            if (R.CanRead(static_cast<int32>(FMath::Min<uint32>(Len, 0x7FFFFFFF)))) Message = R.ReadCStringBytes();
            else R.Fail(TEXT("Chat string exceeds packet"));
        }
    }
    else
    {
        Message = ReadSizedString();
    }
    const uint8 ChatTag = R.CanRead(1) ? R.ReadU8() : 0;
    if (R.HasFailed()) return;

    FString SenderName = InlineSenderName;
    if (SenderName.IsEmpty() && SenderGuid != 0)
    {
        if (const FString* CachedName = PlayerNameCache.Find(SenderGuid)) SenderName = *CachedName;
        else SenderName = FString::Printf(TEXT("Player-%llu"), SenderGuid);
    }
    LastChatTag = ChatTag;

    UE_LOG(LogWowPacket, Verbose, TEXT("CHAT type=%d lang=%d sender=%s channel=%s tag=%d: %s"),
           Type, static_cast<int32>(Language), *SenderName, *Channel, ChatTag, *Message);
    OnChatMessage.Broadcast(Type, Language, SenderGuid, SenderName, Message, Channel);
}

void FWowPacketHandler::HandleMotd(FPacketReader& R)
{
    if (!R.CanRead(4)) return;
    const uint32 LineCount = FMath::Min<uint32>(R.ReadU32(), 64);
    for (uint32 LineIndex = 0; LineIndex < LineCount && R.CanRead(1); ++LineIndex)
    {
        const FString MotdLine = R.ReadCString();
        if (!MotdLine.IsEmpty())
        {
            // Use the normal system-message route so colours, fading, scrolling
            // and persistent ChatLog.txt output stay identical to regular chat.
            OnChatMessage.Broadcast(0, 0, 0, FString(), MotdLine, FString());
        }
    }
}

// ── SMSG_INITIAL_SPELLS ──────────────────────────────────────────────────────

void FWowPacketHandler::HandleInitialSpells(FPacketReader& R)
{
    R.ReadU8(); // talent spec
    uint16 SpellCount = R.ReadU16();

    KnownSpells.Empty(SpellCount);
    for (int32 i = 0; i < SpellCount; ++i)
    {
        uint32 SpellId = R.ReadU32();
        R.ReadU16(); // unknown (slot?)
        KnownSpells.Add(SpellId);
    }

    uint16 CooldownCount = R.ReadU16();
    for (int32 i = 0; i < CooldownCount; ++i)
    {
        uint32 SpellId = R.ReadU32();
        R.ReadU16(); // item ID
        R.ReadU16(); // spell category
        uint32 CooldownMs = R.ReadU32();
        R.ReadU32(); // category cooldown

        if (CooldownMs > 0)
        {
            double ExpiryTime = FPlatformTime::Seconds() + CooldownMs / 1000.0;
            RecordSpellCooldown(SpellId, CooldownMs / 1000.f);
            OnSpellCooldown.Broadcast(SpellId, CooldownMs / 1000.0f);
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("INITIAL_SPELLS: %d spells stored, %d cooldowns"), SpellCount, CooldownCount);

    // Fire delegate with loaded spells
    OnInitialSpells.Broadcast(KnownSpells.Array());
}

// ── SMSG_ACTION_BUTTONS ──────────────────────────────────────────────────────

void FWowPacketHandler::HandleActionButtons(FPacketReader& R)
{
    // 3.3.5: first byte is packet type (0=initial, 1=update, 2=clear)
    if (R.Remaining() < 1) return;
    uint8 PacketType = R.ReadU8();
    UE_LOG(LogWowPacket, Log, TEXT("ACTION_BUTTONS packet type: %d"), PacketType);

    // 144 action buttons × 4 bytes each = 576 bytes
    int32 ButtonCount = FMath::Min(R.Remaining() / 4, 144);

    ActionButtons.Empty(ButtonCount);
    ActionButtons.AddZeroed(ButtonCount);

    int32 NonEmpty = 0;
    for (int32 i = 0; i < ButtonCount; ++i)
    {
        uint32 PackedAction = R.ReadU32();
        ActionButtons[i] = PackedAction;
        if (PackedAction != 0) NonEmpty++;
    }

    UE_LOG(LogWowPacket, Log, TEXT("ACTION_BUTTONS: %d/%d slots assigned"), NonEmpty, ButtonCount);

    // Debug: log first 12 slots (main action bar)
    for (int32 i = 0; i < FMath::Min(12, ButtonCount); ++i)
    {
        uint32 Data = ActionButtons[i];
        if (Data != 0)
        {
            uint8 Type = (Data >> 24) & 0xFF;
            uint32 Id = Data & 0x00FFFFFF;
            UE_LOG(LogWowPacket, Log, TEXT("  Slot %d: type=%d id=%d (raw=0x%08X)"), i+1, Type, Id, Data);
        }
    }

    // Fire delegate for action bar updates
    OnActionButtonsUpdated.Broadcast();
}

// ── SMSG_TIME_SYNC_REQ ──────────────────────────────────────────────────────

// ── Calendar (r240) ──────────────────────────────────────────────────────
void FWowPacketHandler::RequestCalendar()
{
    // CMSG_CALENDAR_GET_CALENDAR has no body; the pending-invite count rides along.
    OnSendPacket.ExecuteIfBound(WowOpcode::CMSG_CALENDAR_GET_CALENDAR, TArray<uint8>());
    OnSendPacket.ExecuteIfBound(WowOpcode::CMSG_CALENDAR_GET_NUM_PENDING, TArray<uint8>());
}

// SMSG_CALENDAR_SEND_CALENDAR: uint32 invites {uint64 event, uint64 invite, uint8 status, uint8 rank,
// uint8 guild event, packed guid creator}; uint32 events {uint64 id, string title, uint32 type, packed
// time, uint32 flags, int32 dungeon, packed guid creator}; uint32 server time, packed realm time;
// uint32 bound instances {uint32 map, uint32 difficulty, uint32 seconds left, uint64 instance};
// uint32 reset origin; uint32 raid resets {int32 map, int32 period, int32 offset}; uint32 holidays
// (each 5 + 26 + 10 + 10 uint32 and a texture name).
void FWowPacketHandler::HandleCalendarSendCalendar(FPacketReader& R)
{
    FWowCalendarState NewState;
    NewState.NumPendingInvites = Calendar.NumPendingInvites;
    const uint32 NumInvites = R.ReadU32();
    for (uint32 I = 0; I < NumInvites && !R.HasFailed() && I < 1000; ++I)
    {
        FWowCalendarInvite Invite;
        Invite.EventId = R.ReadU64();
        Invite.InviteId = R.ReadU64();
        Invite.Status = R.ReadU8();
        Invite.Rank = R.ReadU8();
        Invite.bGuildEvent = R.ReadU8() != 0;
        Invite.CreatorGuid = R.ReadPackedGuid();
        NewState.Invites.Add(Invite);
    }
    const uint32 NumEvents = R.ReadU32();
    for (uint32 I = 0; I < NumEvents && !R.HasFailed() && I < 1000; ++I)
    {
        FWowCalendarEvent Event;
        Event.EventId = R.ReadU64();
        Event.Title = R.ReadCString();
        Event.Type = R.ReadU32();
        Event.PackedTime = R.ReadU32();
        Event.Flags = R.ReadU32();
        Event.DungeonId = static_cast<int32>(R.ReadU32());
        Event.CreatorGuid = R.ReadPackedGuid();
        NewState.Events.Add(Event);
    }
    NewState.ServerUnix = R.ReadU32();
    NewState.ServerPackedTime = R.ReadU32();
    const int64 ReceivedUnix = FDateTime::UtcNow().ToUnixTimestamp();
    const uint32 NumLockouts = R.ReadU32();
    for (uint32 I = 0; I < NumLockouts && !R.HasFailed() && I < 1000; ++I)
    {
        FWowCalendarLockout Lockout;
        Lockout.MapId = R.ReadU32();
        Lockout.Difficulty = R.ReadU32();
        Lockout.ResetUnix = ReceivedUnix + static_cast<int64>(R.ReadU32());
        Lockout.InstanceId = R.ReadU64();
        NewState.Lockouts.Add(Lockout);
    }
    NewState.BaseUnix = R.ReadU32();
    const uint32 NumResets = R.ReadU32();
    for (uint32 I = 0; I < NumResets && !R.HasFailed() && I < 1000; ++I)
    {
        FWowCalendarRaidReset Reset;
        Reset.MapId = R.ReadU32();
        Reset.Period = static_cast<int32>(R.ReadU32());
        Reset.Offset = static_cast<int32>(R.ReadU32());
        NewState.RaidResets.Add(Reset);
    }
    // Holidays: servers send none (the client reads Holidays.dbc); skipped if present.
    if (R.HasFailed()) return;
    NewState.bReceived = true;
    NewState.Revision = Calendar.Revision + 1;
    UE_LOG(LogWowPacket, Log, TEXT("Calendar: %d invites, %d events, %d bound instances, %d raid resets (server time %lld)"),
        NewState.Invites.Num(), NewState.Events.Num(), NewState.Lockouts.Num(), NewState.RaidResets.Num(), NewState.ServerUnix);
    Calendar = MoveTemp(NewState);
}

void FWowPacketHandler::HandleCalendarSendNumPending(FPacketReader& R)
{
    Calendar.NumPendingInvites = static_cast<int32>(R.ReadU32());
}

void FWowPacketHandler::HandleTimeSyncReq(FPacketReader& R)
{
    uint32 Counter = R.ReadU32();
    UE_LOG(LogWowPacket, Verbose, TEXT("TIME_SYNC_REQ: counter=%u"), Counter);

    // Respond with CMSG_TIME_SYNC_RESP: uint32 counter + uint32 clientTicks
    TArray<uint8> Resp;
    Resp.SetNumUninitialized(8);
    FMemory::Memcpy(Resp.GetData(), &Counter, 4);
    // Same millisecond clock as MovementInfo.time; servers derive the clock
    // delta from this reply and compare it against movement timestamps.
    uint32 ClientTicks = WowMovementPacket::ClientTimeMs();
    FMemory::Memcpy(Resp.GetData() + 4, &ClientTicks, 4);

    OnSendPacket.ExecuteIfBound(WowOpcode::CMSG_TIME_SYNC_RESP, Resp);
}

// ── SMSG_SPELL_START ─────────────────────────────────────────────────────
// packed guid caster, packed guid caster again, uint8 cast counter, uint32 spell id, uint32 cast flags, int32 cast time, (targets)

void FWowPacketHandler::HandleSpellStart(FPacketReader& R)
{
    if (!R.CanRead(18)) // minimum: 2 packed guids (2 bytes each) + cast counter + spell id + cast flags + cast time
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SPELL_START: packet too short"));
        return;
    }

    uint64 CasterGuid1 = R.ReadPackedGuid(); // Item or caster guid
    uint64 CasterGuid = R.ReadPackedGuid();  // Actual caster guid
    uint8 CastCounter = R.ReadU8();
    uint32 SpellId = R.ReadU32();
    uint32 CastFlags = R.ReadU32();
    int32 CastTime = static_cast<int32>(R.ReadU32());

    UE_LOG(LogWowPacket, Log, TEXT("SPELL_START: caster=%llu spell=%u castTime=%dms"),
        CasterGuid, SpellId, CastTime);

    StartGlobalCooldown(CasterGuid, SpellId, CastCounter);
    OnSpellStart.Broadcast(CasterGuid, SpellId, CastFlags, CastTime);

    // Skip remaining data (targets and additional cast flag data)
}

// ── SMSG_SPELL_GO ───────────────────────────────────────────────────────
// packed guid caster, packed guid caster again, uint8 cast counter, uint32 spell id, uint32 cast flags, uint32 timestamp,
// uint8 hit count, uint64 GUIDs, uint8 miss count, uint64 GUIDs + miss reasons

void FWowPacketHandler::HandleSpellGo(FPacketReader& R)
{
    if (!R.CanRead(17)) // two empty packed GUIDs, fixed fields and two target counts
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SPELL_GO: packet too short"));
        return;
    }

    uint64 CasterGuid1 = R.ReadPackedGuid(); // Item or caster guid
    uint64 CasterGuid = R.ReadPackedGuid();  // Actual caster guid
    uint8 CastCounter = R.ReadU8();
    uint32 SpellId = R.ReadU32();
    uint32 CastFlags = R.ReadU32();
    uint32 Timestamp = R.ReadU32();

    // Build 12340 packs the caster GUIDs, but hit/miss lists contain full
    // uint64 GUIDs. Treating those lists as packed corrupts every later target.
    const uint8 HitCount = R.ReadU8();
    if (!R.Require(static_cast<int32>(HitCount) * 8 + 1, TEXT("spell hit targets"))) return;
    TArray<uint64> HitTargets;
    HitTargets.Reserve(HitCount);
    for (uint32 I = 0; I < HitCount; ++I)
    {
        const uint64 Guid = R.ReadU64();
        if (Guid != 0) HitTargets.AddUnique(Guid);
    }
    const uint8 MissCount = R.ReadU8();
    for (uint32 I = 0; I < MissCount; ++I)
    {
        if (!R.Require(9, TEXT("spell miss target"))) return;
        R.ReadU64();
        if (R.ReadU8() == 11) R.ReadU8(); // reflected spell result
        if (R.HasFailed()) return;
    }
    if (R.HasFailed()) return;
    const uint64 FirstHitTargetGuid = HitTargets.IsEmpty() ? 0 : HitTargets[0];
    StartGlobalCooldown(CasterGuid, SpellId, CastCounter);
    if (CasterGuid==EntityManager.LocalPlayerGuid)
    {
        if (const auto* Info=FDbcStore::Get().Spells().GetById(SpellId))
        {
            if (Info->RecoveryTime>0 && GetSpellCooldownRemaining(SpellId)<=0)
                RecordSpellCooldown(SpellId,Info->RecoveryTime/1000.f);
            if (Info->Category && Info->CategoryRecoveryTime)
                for(uint32 Known:KnownSpells)
                    if (const auto* Related=FDbcStore::Get().Spells().GetById(Known); Related && Related->Category==Info->Category)
                        if (GetSpellCooldownRemaining(Known)<Info->CategoryRecoveryTime/1000.f)
                            RecordSpellCooldown(Known,Info->CategoryRecoveryTime/1000.f);
        }
    }
    OnSpellGo.Broadcast(CasterGuid, FirstHitTargetGuid, SpellId, CastFlags);
    OnSpellHitTargets.Broadcast(CasterGuid, SpellId, HitTargets);

    // Skip remaining data (targets and additional cast flag data)
}

// ── MSG_CHANNEL_START / MSG_CHANNEL_UPDATE ─────────────────────────────
// AzerothCore 3.3.5a sends packed caster GUID + spell/duration for start,
// followed by packed caster GUID + remaining duration for updates.

void FWowPacketHandler::HandleChannelStart(FPacketReader& R)
{
    if (R.Remaining() < 9)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("CHANNEL_START: packet too short"));
        return;
    }

    const uint64 CasterGuid = R.ReadPackedGuid();
    if (!R.CanRead(8))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("CHANNEL_START: truncated after caster GUID"));
        return;
    }

    const uint32 SpellId = R.ReadU32();
    const uint32 DurationMs = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("CHANNEL_START: caster=%llu spell=%u duration=%ums"),
        CasterGuid, SpellId, DurationMs);
    OnChannelStart.Broadcast(CasterGuid, SpellId, DurationMs);
}

void FWowPacketHandler::HandleChannelUpdate(FPacketReader& R)
{
    if (R.Remaining() < 5)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("CHANNEL_UPDATE: packet too short"));
        return;
    }

    const uint64 CasterGuid = R.ReadPackedGuid();
    if (!R.CanRead(4))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("CHANNEL_UPDATE: truncated after caster GUID"));
        return;
    }

    const uint32 RemainingMs = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("CHANNEL_UPDATE: caster=%llu remaining=%ums"),
        CasterGuid, RemainingMs);
    OnChannelUpdate.Broadcast(CasterGuid, RemainingMs);
}

void FWowPacketHandler::HandleSpellDelayed(FPacketReader& R)
{
    if (R.Remaining() < 5)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SPELL_DELAYED: packet too short"));
        return;
    }

    const uint64 CasterGuid = R.ReadPackedGuid();
    if (!R.CanRead(4))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SPELL_DELAYED: truncated after caster GUID"));
        return;
    }

    const uint32 DelayMs = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("SPELL_DELAYED: caster=%llu delay=%ums"),
        CasterGuid, DelayMs);
    OnSpellDelayed.Broadcast(CasterGuid, DelayMs);
}

// ── SMSG_SPELL_FAILURE ──────────────────────────────────────────────────
// packed guid caster, uint8 cast counter, uint32 spell id, uint8 failure reason

void FWowPacketHandler::HandleSpellFailure(FPacketReader& R)
{
    if (!R.CanRead(7)) // minimum: packed guid (1) + cast counter + spell id + failure reason
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SPELL_FAILURE: packet too short"));
        return;
    }

    uint64 CasterGuid = R.ReadPackedGuid();
    uint8 CastCounter = R.ReadU8();
    uint32 SpellId = R.ReadU32();
    uint8 FailureReason = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("SPELL_FAILURE: caster=%llu spell=%u reason=%u"),
        CasterGuid, SpellId, FailureReason);

    OnSpellFailure.Broadcast(CasterGuid, SpellId, FailureReason);
}

// ── SMSG_CAST_FAILED ────────────────────────────────────────────────────
// uint8 cast count, uint32 spell id, uint8 SpellCastResult (+ result-specific data, ignored).
// Sent only to the caster when the server refuses a cast before it starts: this is where
// "Out of range", "Your cast didn't land in fishable water" and most red errors come from.

void FWowPacketHandler::HandleCastFailed(FPacketReader& R)
{
    if (!R.CanRead(6))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("CAST_FAILED: packet too short"));
        return;
    }
    R.ReadU8(); // cast count
    const uint32 SpellId = R.ReadU32();
    const uint8 Result = R.ReadU8();
    UE_LOG(LogWowPacket, Log, TEXT("CAST_FAILED: spell=%u result=%u"), SpellId, Result);
    OnCastFailed.Broadcast(SpellId, Result);
}

// ── SMSG_SPELL_COOLDOWN ──────────────────────────────────────────────────
// SMSG_SPELL_COOLDOWN format (3.3.5a):
// - uint64 CasterGuid
// - uint8 Flags (1 = ON_LOGIN)
// - Then repeated until end of packet:
//   - uint32 SpellId
//   - uint32 CooldownMs

void FWowPacketHandler::HandleSpellCooldown(FPacketReader& R)
{
    if (!R.CanRead(9)) // minimum: guid (8) + flags (1)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SPELL_COOLDOWN: packet too short"));
        return;
    }

    uint64 CasterGuid = R.ReadU64();
    uint8 Flags = R.ReadU8();
    if (CasterGuid != EntityManager.LocalPlayerGuid) return;

    int32 CooldownsProcessed = 0;
    while (R.CanRead(8)) // spell id (4) + cooldown ms (4)
    {
        uint32 SpellId = R.ReadU32();
        uint32 CooldownMs = R.ReadU32();

        if (CooldownMs > 0)
        {
            double ExpiryTime = FPlatformTime::Seconds() + CooldownMs / 1000.0;
            RecordSpellCooldown(SpellId, CooldownMs / 1000.f);
            OnSpellCooldown.Broadcast(SpellId, CooldownMs / 1000.0f);
            CooldownsProcessed++;
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("SPELL_COOLDOWN: caster=%llu flags=%u processed=%d cooldowns"),
        CasterGuid, Flags, CooldownsProcessed);
}

// ── SMSG_ATTACKERSTATEUPDATE ────────────────────────────────────────────
// uint32 hit_info, packed guid attacker, packed guid target, uint32 damage, uint8 damage_school_count
// for each school: uint32 damage, uint32 absorbed

void FWowPacketHandler::HandleAttackerStateUpdate(FPacketReader& R)
{
    const uint32 HitInfo = R.ReadU32();
    const uint64 AttackerGuid = R.ReadPackedGuid();
    const uint64 TargetGuid = R.ReadPackedGuid();
    const uint32 Damage = R.ReadU32();
    const uint32 Overkill = R.ReadU32(); // overkill precedes the school count in build 12340
    const uint8 SchoolCount = R.ReadU8();
    uint32 SchoolMask = 1;
    for (uint8 Index = 0; Index < SchoolCount; ++Index)
    {
        const uint32 Mask = R.ReadU32();
        R.ReadFloat(); R.ReadU32(); // float damage, uint32 damage
        if (Index == 0 && Mask) SchoolMask = Mask;
    }
    uint32 Absorbed = 0, Resisted = 0, Blocked = 0;
    if (HitInfo & 0x60) for (uint8 Index = 0; Index < SchoolCount; ++Index) Absorbed += R.ReadU32();
    if (HitInfo & 0x180) for (uint8 Index = 0; Index < SchoolCount; ++Index) Resisted += R.ReadU32();
    const uint8 VictimState = R.ReadU8();
    R.ReadU32(); // attacker state
    R.ReadU32(); // melee spell
    if (HitInfo & 0x2000) Blocked = R.ReadU32();
    if (HitInfo & 0x800000) R.ReadU32(); // rage gain
    if (HitInfo & 1) R.Skip(48); // debug fields
    if (R.HasFailed()) return;
    OnAttackerStateUpdate.Broadcast(AttackerGuid, TargetGuid, HitInfo, Damage);

    // HITINFO: 0x10 miss, 0x20/0x40 full/partial absorb, 0x80/0x100 full/partial resist,
    // 0x200 crit, 0x4000 glancing, 0x8000 crushing. VictimState: 2 dodge, 3 parry, 6 evade, 7 immune, 8 deflect.
    FWowCombatLogEntry Entry;
    Entry.SourceGuid = AttackerGuid;
    Entry.DestGuid = TargetGuid;
    Entry.School = uint8(SchoolMask & 0x7F);
    const TCHAR* Miss = nullptr;
    if (HitInfo & 0x10) Miss = TEXT("MISS");
    else if (VictimState == 2) Miss = TEXT("DODGE");
    else if (VictimState == 3) Miss = TEXT("PARRY");
    else if (VictimState == 6) Miss = TEXT("EVADE");
    else if (VictimState == 7) Miss = TEXT("IMMUNE");
    else if (VictimState == 8) Miss = TEXT("DEFLECT");
    else if (Damage == 0 && Blocked > 0) Miss = TEXT("BLOCK");
    else if (Damage == 0 && (HitInfo & 0x20)) Miss = TEXT("ABSORB");
    else if (Damage == 0 && (HitInfo & 0x80)) Miss = TEXT("RESIST");
    if (Miss)
    {
        Entry.Event = TEXT("SWING_MISSED");
        Entry.MissType = Miss;
        Entry.Amount = int32(Blocked + Absorbed + Resisted);
    }
    else
    {
        Entry.Event = TEXT("SWING_DAMAGE");
        Entry.Amount = int32(Damage);
        Entry.Overkill = int32(Overkill);
        Entry.Absorbed = int32(Absorbed);
        Entry.Resisted = int32(Resisted);
        Entry.Blocked = int32(Blocked);
        Entry.bCritical = (HitInfo & 0x200) != 0;
        Entry.bGlancing = (HitInfo & 0x4000) != 0;
        Entry.bCrushing = (HitInfo & 0x8000) != 0;
    }
    OnCombatLog.Broadcast(Entry);
}

namespace
{
const TCHAR* WowSpellMissTypeName(uint8 MissInfo)
{
    switch (MissInfo)
    {
    case 2: return TEXT("RESIST");
    case 3: return TEXT("DODGE");
    case 4: return TEXT("PARRY");
    case 5: return TEXT("BLOCK");
    case 6: return TEXT("EVADE");
    case 7: case 8: return TEXT("IMMUNE");
    case 9: return TEXT("DEFLECT");
    case 10: return TEXT("ABSORB");
    case 11: return TEXT("REFLECT");
    default: return TEXT("MISS");
    }
}

bool IsWowRangedAutoSpell(uint32 SpellId)
{
    return SpellId == 75 || SpellId == 5019 || SpellId == 2764 || SpellId == 3018;
}
}

// packed target, packed attacker, spell, damage, overkill, uint8 school, absorb, resist,
// uint8 physical, uint8 unused, blocked, hit flags (0x2 crit), uint8 extended
void FWowPacketHandler::HandleSpellNonMeleeDamageLog(FPacketReader& R)
{
    FWowCombatLogEntry Entry;
    Entry.DestGuid = R.ReadPackedGuid();
    Entry.SourceGuid = R.ReadPackedGuid();
    Entry.SpellId = R.ReadU32();
    Entry.Amount = int32(R.ReadU32());
    Entry.Overkill = int32(R.ReadU32());
    Entry.School = R.ReadU8();
    Entry.Absorbed = int32(R.ReadU32());
    Entry.Resisted = int32(R.ReadU32());
    R.ReadU8(); R.ReadU8();
    Entry.Blocked = int32(R.ReadU32());
    const uint32 HitFlags = R.ReadU32();
    if (R.HasFailed()) return;
    Entry.bCritical = (HitFlags & 0x2) != 0;
    Entry.Event = IsWowRangedAutoSpell(Entry.SpellId) ? TEXT("RANGE_DAMAGE") : TEXT("SPELL_DAMAGE");
    OnCombatLog.Broadcast(Entry);
}

// packed target, packed caster, spell, heal, overheal, absorb, uint8 crit, uint8 unused
void FWowPacketHandler::HandleSpellHealLog(FPacketReader& R)
{
    FWowCombatLogEntry Entry;
    Entry.DestGuid = R.ReadPackedGuid();
    Entry.SourceGuid = R.ReadPackedGuid();
    Entry.SpellId = R.ReadU32();
    Entry.Amount = int32(R.ReadU32());
    Entry.Overkill = int32(R.ReadU32());
    Entry.Absorbed = int32(R.ReadU32());
    Entry.bCritical = R.ReadU8() != 0;
    if (R.HasFailed()) return;
    Entry.School = 2;
    Entry.Event = TEXT("SPELL_HEAL");
    OnCombatLog.Broadcast(Entry);
}

// packed target, packed caster, spell, power type, amount
void FWowPacketHandler::HandleSpellEnergizeLog(FPacketReader& R)
{
    FWowCombatLogEntry Entry;
    Entry.DestGuid = R.ReadPackedGuid();
    Entry.SourceGuid = R.ReadPackedGuid();
    Entry.SpellId = R.ReadU32();
    Entry.PowerType = int32(R.ReadU32());
    Entry.Amount = int32(R.ReadU32());
    if (R.HasFailed()) return;
    Entry.Event = TEXT("SPELL_ENERGIZE");
    OnCombatLog.Broadcast(Entry);
}

// packed target, packed caster, spell, count, then per entry: aura type + type-specific fields
void FWowPacketHandler::HandlePeriodicAuraLog(FPacketReader& R)
{
    const uint64 Target = R.ReadPackedGuid();
    const uint64 Caster = R.ReadPackedGuid();
    const uint32 SpellId = R.ReadU32();
    const uint32 Count = R.ReadU32();
    for (uint32 Index = 0; Index < Count && Index < 8 && !R.HasFailed(); ++Index)
    {
        FWowCombatLogEntry Entry;
        Entry.DestGuid = Target;
        Entry.SourceGuid = Caster;
        Entry.SpellId = SpellId;
        const uint32 AuraType = R.ReadU32();
        switch (AuraType)
        {
        case 3: case 89: // PERIODIC_DAMAGE, PERIODIC_DAMAGE_PERCENT
            Entry.Amount = int32(R.ReadU32());
            Entry.Overkill = int32(R.ReadU32());
            Entry.School = uint8(R.ReadU32());
            Entry.Absorbed = int32(R.ReadU32());
            Entry.Resisted = int32(R.ReadU32());
            Entry.bCritical = R.ReadU8() != 0;
            Entry.Event = TEXT("SPELL_PERIODIC_DAMAGE");
            break;
        case 8: case 20: // PERIODIC_HEAL, OBS_MOD_HEALTH
            Entry.Amount = int32(R.ReadU32());
            Entry.Overkill = int32(R.ReadU32());
            Entry.Absorbed = int32(R.ReadU32());
            Entry.bCritical = R.ReadU8() != 0;
            Entry.School = 2;
            Entry.Event = TEXT("SPELL_PERIODIC_HEAL");
            break;
        case 21: case 24: // OBS_MOD_POWER, PERIODIC_ENERGIZE
            Entry.PowerType = int32(R.ReadU32());
            Entry.Amount = int32(R.ReadU32());
            Entry.Event = TEXT("SPELL_PERIODIC_ENERGIZE");
            break;
        case 64: // PERIODIC_MANA_LEECH
        {
            Entry.PowerType = int32(R.ReadU32());
            Entry.Amount = int32(R.ReadU32());
            const float Multiplier = R.ReadFloat();
            Entry.ExtraAmount = FMath::IsFinite(Multiplier) ? FMath::RoundToInt(Entry.Amount * Multiplier) : 0;
            Entry.Event = TEXT("SPELL_PERIODIC_LEECH");
            break;
        }
        default:
            UE_LOG(LogWowPacket, Verbose, TEXT("PERIODICAURALOG: unhandled aura type %u (spell %u)"), AuraType, SpellId);
            return;
        }
        if (!R.HasFailed()) OnCombatLog.Broadcast(Entry);
    }
}

// spell, uint64 caster, uint8 flag, count, then per target: uint64 target, uint8 miss info [, 2 floats if flag]
void FWowPacketHandler::HandleSpellLogMiss(FPacketReader& R)
{
    const uint32 SpellId = R.ReadU32();
    const uint64 Caster = R.ReadU64();
    const uint8 Flag = R.ReadU8();
    const uint32 Count = R.ReadU32();
    for (uint32 Index = 0; Index < Count && Index < 64 && !R.HasFailed(); ++Index)
    {
        FWowCombatLogEntry Entry;
        Entry.SourceGuid = Caster;
        Entry.DestGuid = R.ReadU64();
        const uint8 MissInfo = R.ReadU8();
        if (Flag & 1) R.Skip(8);
        if (R.HasFailed() || MissInfo == 0) continue;
        Entry.SpellId = SpellId;
        Entry.MissType = WowSpellMissTypeName(MissInfo);
        Entry.Event = IsWowRangedAutoSpell(SpellId) ? TEXT("RANGE_MISSED") : TEXT("SPELL_MISSED");
        OnCombatLog.Broadcast(Entry);
    }
}

// uint64 victim, uint64 shield owner, spell, damage, overkill, school mask, resist
void FWowPacketHandler::HandleSpellDamageShield(FPacketReader& R)
{
    FWowCombatLogEntry Entry;
    Entry.DestGuid = R.ReadU64();
    Entry.SourceGuid = R.ReadU64();
    Entry.SpellId = R.ReadU32();
    Entry.Amount = int32(R.ReadU32());
    Entry.Overkill = int32(R.ReadU32());
    Entry.School = uint8(R.ReadU32());
    Entry.Resisted = int32(R.ReadU32());
    if (R.HasFailed()) return;
    Entry.Event = TEXT("DAMAGE_SHIELD");
    OnCombatLog.Broadcast(Entry);
}

// uint64 killer, uint64 victim
void FWowPacketHandler::HandlePartyKillLog(FPacketReader& R)
{
    FWowCombatLogEntry Entry;
    Entry.SourceGuid = R.ReadU64();
    Entry.DestGuid = R.ReadU64();
    if (R.HasFailed()) return;
    Entry.Event = TEXT("PARTY_KILL");
    OnCombatLog.Broadcast(Entry);
}

void FWowPacketHandler::HandleAttackStart(FPacketReader& R)
{
    uint64 AttackerGuid = R.ReadU64();
    uint64 VictimGuid = R.ReadU64();
    UE_LOG(LogWowPacket, Log, TEXT("SMSG_ATTACKSTART: attacker=%llu victim=%llu"), AttackerGuid, VictimGuid);
    if (!R.HasFailed()) OnAttackStart.Broadcast(AttackerGuid, VictimGuid);
}

void FWowPacketHandler::HandleAttackStop(FPacketReader& R)
{
    uint64 AttackerGuid = R.ReadPackedGuid();
    uint64 VictimGuid = R.ReadPackedGuid();
    R.ReadU32(); // mandatory stop reason in build 12340
    UE_LOG(LogWowPacket, Log, TEXT("SMSG_ATTACKSTOP: attacker=%llu victim=%llu"), AttackerGuid, VictimGuid);
    if (!R.HasFailed()) OnAttackStop.Broadcast(AttackerGuid, VictimGuid);
}

// ── SMSG_AURA_UPDATE ────────────────────────────────────────────────────
// packed guid target, uint8 slot, [if slot != 0xFF: uint32 spell id, uint8 flags, uint8 level, uint8 charges, possible packed guid caster]

void FWowPacketHandler::HandleItemEnchantTimeUpdate(FPacketReader& R)
{
    // Build 12340: item GUID, enchant slot, remaining seconds, player GUID.
    if (!R.CanRead(24)) return;
    const uint64 ItemGuid = R.ReadU64();
    const uint32 Slot = R.ReadU32(), Seconds = R.ReadU32();
    const uint64 Owner = R.ReadU64();
    if (!ItemGuid || Slot != 1 || Owner != EntityManager.LocalPlayerGuid) return;
    if (FWowItemEntity* Item = EntityManager.FindItem(ItemGuid))
    {
        Item->TempEnchantReceivedAt = FPlatformTime::Seconds();
        Item->TempEnchantDeadline = Item->TempEnchantReceivedAt + double(Seconds);
        Item->bTempEnchantTimed = true;
        OnAurasChanged.Broadcast(Owner);
    }
}

void FWowPacketHandler::HandleAuraUpdate(FPacketReader& R)
{
    ParseAuraUpdate(R, false);
}

void FWowPacketHandler::HandleAuraUpdateAll(FPacketReader& R)
{
    ParseAuraUpdate(R, true);
}

void FWowPacketHandler::ParseAuraUpdate(FPacketReader& R, bool bReplaceAll)
{
    const uint64 TargetGuid = R.ReadPackedGuid();
    FWowEntity* Entity = EntityManager.Find(TargetGuid);
    if (!Entity || R.HasFailed()) return;
    TArray<FAuraInfo> Updated = Entity->Auras;
    if (bReplaceAll) for (FAuraInfo& Aura : Updated) Aura = FAuraInfo();
    while (R.Remaining() > 0 && !R.HasFailed())
    {
        const uint8 Slot = R.ReadU8();
        FAuraInfo Aura;
        Aura.SpellId = R.ReadU32();
        if (Aura.SpellId != 0)
        {
            Aura.Flags = R.ReadU8();
            Aura.Level = R.ReadU8();
            Aura.Charges = R.ReadU8();
            // AFLAG_CASTER means self-cast: only OTHER casters have a GUID here.
            Aura.CasterGuid = (Aura.Flags & 0x08) ? TargetGuid : R.ReadPackedGuid();
            if (Aura.Flags & 0x20)
            {
                Aura.DurationMs = R.ReadU32();
                Aura.RemainingMs = R.ReadU32();
            }
            Aura.ReceivedAtSeconds = FPlatformTime::Seconds();
            Aura.bActive = true;
        }
        if (Updated.IsValidIndex(Slot)) Updated[Slot] = Aura;
    }
    if (!R.HasFailed())
    {
        if (!bReplaceAll)
        {
            // Combat log aura events for incremental changes only (a full update is a unit coming into view).
            const int32 SlotCount = FMath::Min(Updated.Num(), Entity->Auras.Num());
            for (int32 Slot = 0; Slot < SlotCount; ++Slot)
            {
                const FAuraInfo& Old = Entity->Auras[Slot];
                const FAuraInfo& New = Updated[Slot];
                const bool bOld = Old.bActive && Old.SpellId != 0;
                const bool bNew = New.bActive && New.SpellId != 0;
                auto Emit = [&](const TCHAR* Event, const FAuraInfo& Aura, int32 Amount)
                {
                    FWowCombatLogEntry Entry;
                    Entry.Event = Event;
                    Entry.SourceGuid = Aura.CasterGuid;
                    Entry.DestGuid = TargetGuid;
                    Entry.SpellId = Aura.SpellId;
                    Entry.Amount = Amount;
                    Entry.AuraType = (Aura.Flags & 0x80) ? TEXT("DEBUFF") : TEXT("BUFF");
                    OnCombatLog.Broadcast(Entry);
                };
                if (bOld && (!bNew || New.SpellId != Old.SpellId)) Emit(TEXT("SPELL_AURA_REMOVED"), Old, 0);
                if (bNew && (!bOld || New.SpellId != Old.SpellId)) Emit(TEXT("SPELL_AURA_APPLIED"), New, 0);
                else if (bNew && bOld)
                {
                    if (New.Charges > Old.Charges) Emit(TEXT("SPELL_AURA_APPLIED_DOSE"), New, New.Charges);
                    else if (New.Charges < Old.Charges) Emit(TEXT("SPELL_AURA_REMOVED_DOSE"), New, New.Charges);
                    else if (New.RemainingMs > Old.RemainingMs || New.ReceivedAtSeconds != Old.ReceivedAtSeconds)
                        Emit(TEXT("SPELL_AURA_REFRESH"), New, 0);
                }
            }
        }
        Entity->Auras = MoveTemp(Updated);
        OnAurasChanged.Broadcast(TargetGuid);
    }
}

// ── SMSG_POWER_UPDATE ───────────────────────────────────────────────────
// packed guid target, uint8 power type, uint32 power value

void FWowPacketHandler::HandlePowerUpdate(FPacketReader& R)
{
    if (!R.CanRead(6)) // minimum: packed guid + power type + power value
    {
        UE_LOG(LogWowPacket, Warning, TEXT("POWER_UPDATE: packet too short"));
        return;
    }

    uint64 TargetGuid = R.ReadPackedGuid();
    uint8 PowerType = R.ReadU8();
    uint32 PowerValue = R.ReadU32();

    FWowEntity* Entity = EntityManager.Find(TargetGuid);
    if (Entity)
    {
        // Update power field in entity based on power type
        if (PowerType < 7) // Valid power types: 0=mana, 1=rage, 2=focus, 3=energy, 4=happiness, 5=runes, 6=runic power
        {
            // WoW power fields start at UNIT_FIELD_POWER1 + power_type
            uint16 PowerFieldIndex = UnitField::POWER1 + PowerType;
            Entity->SetField(PowerFieldIndex, PowerValue);
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("POWER_UPDATE: target=%llu type=%d value=%u"),
        TargetGuid, PowerType, PowerValue);
}

// ── SMSG_MONSTER_MOVE ───────────────────────────────────────────────────
// packed guid target, uint8 movement_flags_2, 3 floats position, uint32 spline_id,
// uint8 move_type, optional final facing, spline flags, duration, and path points.

void FWowPacketHandler::HandleMonsterMove(FPacketReader& R)
{
    constexpr uint8 MoveNormal = 0;
    constexpr uint8 MoveStop = 1;
    constexpr uint8 MoveFacingPoint = 2;
    constexpr uint8 MoveFacingTarget = 3;
    constexpr uint8 MoveFacingAngle = 4;
    constexpr uint32 SplineParabolic = 0x00000800;
    constexpr uint32 SplineFlying = 0x00002000;
    constexpr uint32 SplineCatmullRom = 0x00040000;
    constexpr uint32 SplineCyclic = 0x00080000;
    constexpr uint32 SplineAnimation = 0x00200000;

    if (!R.CanRead(18))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: packet too short"));
        return;
    }

    uint64 TargetGuid = R.ReadPackedGuid();
    const uint8 MovementFlags2 = R.ReadU8();
    float PosX = R.ReadFloat();
    float PosY = R.ReadFloat();
    float PosZ = R.ReadFloat();
    const uint32 SplineId = R.ReadU32();
    const uint8 MoveType = R.ReadU8();

    FWowEntity* Entity = EntityManager.Find(TargetGuid);
    if (!Entity)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: entity with GUID %llu not found"), TargetGuid);
        return;
    }

    // The packet provides the starting position of this movement segment
    FVector PacketStartPos(PosX, PosY, PosZ);
    Entity->Movement.SplineStartPosition = PacketStartPos;
    Entity->Movement.Position = PacketStartPos;
    Entity->Movement.SplineMoveType = MoveType;
    Entity->Movement.SplineId = SplineId;
    Entity->Movement.SplineFacingPoint = FVector::ZeroVector;
    Entity->Movement.SplineFacingTargetGuid = 0;
    Entity->Movement.SplineFacingAngle = Entity->Movement.Orientation;

    if (MoveType == MoveStop)
    {
        Entity->Movement.bHasActiveSpline = false;
        Entity->Movement.SplineWaypoints.Empty();
        UE_LOG(LogWowPacket, Verbose,
            TEXT("MONSTER_MOVE: STOP guid=%llu spline=%u pos=(%.1f,%.1f,%.1f) flags2=0x%02X"),
            TargetGuid, SplineId, PosX, PosY, PosZ, MovementFlags2);
        EntityManager.OnEntityUpdated.Broadcast(*Entity);
        return;
    }

    if (MoveType == MoveFacingPoint)
    {
        if (!R.CanRead(12))
        {
            UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: missing final facing point"));
            return;
        }
        const float FaceX = R.ReadFloat();
        const float FaceY = R.ReadFloat();
        const float FaceZ = R.ReadFloat();
        Entity->Movement.SplineFacingPoint = FVector(FaceX, FaceY, FaceZ);
    }
    else if (MoveType == MoveFacingTarget)
    {
        if (!R.CanRead(8))
        {
            UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: missing final facing target"));
            return;
        }
        Entity->Movement.SplineFacingTargetGuid = R.ReadU64();
    }
    else if (MoveType == MoveFacingAngle)
    {
        if (!R.CanRead(4))
        {
            UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: missing final facing angle"));
            return;
        }
        Entity->Movement.SplineFacingAngle = R.ReadFloat();
    }
    else if (MoveType != MoveNormal)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: unknown move type %u for guid %llu"), MoveType, TargetGuid);
        return;
    }

    if (!R.CanRead(8))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: missing spline flags or duration"));
        return;
    }

    const uint32 SplineFlags = R.ReadU32();
    if ((SplineFlags & SplineAnimation) != 0)
    {
        if (!R.CanRead(9))
        {
            UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: truncated animation spline data"));
            return;
        }
        R.ReadU8();
        R.ReadU32();
    }

    const uint32 Duration = R.ReadU32();
    if ((SplineFlags & SplineParabolic) != 0)
    {
        if (!R.CanRead(8))
        {
            UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: truncated parabolic spline data"));
            return;
        }
        R.ReadFloat();
        R.ReadU32();
    }

    if (!R.CanRead(4))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: missing spline point count"));
        return;
    }

    const uint32 RawPointCount = R.ReadU32();
    const uint32 PointCount = FMath::Min(RawPointCount, 500u);
    if (RawPointCount > PointCount)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("MONSTER_MOVE: excessive point count %u, capping"), RawPointCount);
    }

    Entity->Movement.SplineFlags = SplineFlags;
    Entity->Movement.SplineDuration = Duration;
    Entity->Movement.SplineElapsed = 0.0f;
    Entity->Movement.SplineWaypoints.Empty();

    const bool bSmoothPath = (SplineFlags & (SplineFlying | SplineCatmullRom)) != 0;
    if (PointCount > 0 && bSmoothPath)
    {
        const uint32 ReadablePointCount = FMath::Min(PointCount, static_cast<uint32>(R.Remaining() / 12));
        for (uint32 PointIndex = 0; PointIndex < ReadablePointCount; ++PointIndex)
        {
            const float PointX = R.ReadFloat();
            const float PointY = R.ReadFloat();
            const float PointZ = R.ReadFloat();
            Entity->Movement.SplineWaypoints.Add(FWowSplineWaypoint(FVector(PointX, PointY, PointZ)));
        }
    }
    else if (PointCount > 0 && R.CanRead(12))
    {
        const float DestinationX = R.ReadFloat();
        const float DestinationY = R.ReadFloat();
        const float DestinationZ = R.ReadFloat();
        const FVector Destination(DestinationX, DestinationY, DestinationZ);
        const FVector MidPoint = (PacketStartPos + Destination) * 0.5f;
        const uint32 PackedPointCount = PointCount - 1;
        const uint32 ReadablePackedCount = FMath::Min(PackedPointCount, static_cast<uint32>(R.Remaining() / 4));

        for (uint32 PointIndex = 0; PointIndex < ReadablePackedCount; ++PointIndex)
        {
            const uint32 Packed = R.ReadU32();
            int32 PackedX = static_cast<int32>(Packed & 0x7FF);
            int32 PackedY = static_cast<int32>((Packed >> 11) & 0x7FF);
            int32 PackedZ = static_cast<int32>((Packed >> 22) & 0x3FF);
            if (PackedX & 0x400) PackedX |= ~0x7FF;
            if (PackedY & 0x400) PackedY |= ~0x7FF;
            if (PackedZ & 0x200) PackedZ |= ~0x3FF;

            const FVector Offset(
                static_cast<float>(PackedX) * 0.25f,
                static_cast<float>(PackedY) * 0.25f,
                static_cast<float>(PackedZ) * 0.25f);
            Entity->Movement.SplineWaypoints.Add(FWowSplineWaypoint(MidPoint - Offset));
        }
        Entity->Movement.SplineWaypoints.Add(FWowSplineWaypoint(Destination));
    }

    Entity->Movement.bHasActiveSpline = Duration > 0 && Entity->Movement.SplineWaypoints.Num() > 0;

    if (!Entity->Movement.bHasActiveSpline)
    {
        if (MoveType == MoveFacingPoint)
        {
            const FVector FaceDirection = Entity->Movement.SplineFacingPoint - PacketStartPos;
            if (!FaceDirection.IsNearlyZero())
            {
                Entity->Movement.Orientation = FMath::Atan2(FaceDirection.Y, FaceDirection.X);
            }
        }
        else if (MoveType == MoveFacingTarget)
        {
            if (const FWowEntity* FacingTarget = EntityManager.Find(Entity->Movement.SplineFacingTargetGuid))
            {
                const FVector FaceDirection = FacingTarget->Movement.Position - PacketStartPos;
                if (!FaceDirection.IsNearlyZero())
                {
                    Entity->Movement.Orientation = FMath::Atan2(FaceDirection.Y, FaceDirection.X);
                }
            }
        }
        else if (MoveType == MoveFacingAngle)
        {
            Entity->Movement.Orientation = Entity->Movement.SplineFacingAngle;
        }
    }

    Entity->Movement.UpdateSplineSpeed();

    UE_LOG(LogWowPacket, Verbose,
        TEXT("MONSTER_MOVE: guid=%llu spline=%u type=%u duration=%ums rawPoints=%u resolvedPoints=%d flags=0x%X cyclic=%d"),
        TargetGuid, SplineId, MoveType, Duration, RawPointCount,
        Entity->Movement.SplineWaypoints.Num(), SplineFlags, (SplineFlags & SplineCyclic) != 0);

    EntityManager.OnEntityUpdated.Broadcast(*Entity);
}

FString FWowPacketHandler::InventoryErrorText(uint8 ErrorCode, uint32 RequiredLevel)
{
    if(ErrorCode==0)return FString();
    FString Message;
    switch(ErrorCode)
    {
    case 1: Message=FString::Printf(TEXT("You must reach level %u to use that item."),RequiredLevel);break;
    case 2: Message=TEXT("You aren't skilled enough to use that item.");break;
    case 3: Message=TEXT("That item does not go in that slot.");break;
    case 4:case 53:case 56:case 62:case 74: Message=TEXT("That bag is full.");break;
    case 5: Message=TEXT("Can't put non-empty bags in other bags.");break;
    case 6: Message=TEXT("You can't trade equipped bags.");break;
    case 7: Message=TEXT("Only ammo can go there.");break;
    case 8: Message=TEXT("You do not have the required proficiency for that item.");break;
    case 9:case 12:case 18: Message=TEXT("No equipment slot is available for that item.");break;
    case 10:case 11: Message=TEXT("You can never use that item.");break;
    case 13: Message=TEXT("Cannot equip that with a two-handed weapon.");break;
    case 14: Message=TEXT("You cannot dual-wield");break;
    case 15:case 16: Message=TEXT("That item doesn't go in that container.");break;
    case 17:case 84: Message=TEXT("You can't carry any more of those items.");break;
    case 19:case 55: Message=TEXT("This item cannot stack.");break;
    case 20: Message=TEXT("This item cannot be equipped.");break;
    case 21: Message=TEXT("These items can't be swapped.");break;
    case 22: Message=TEXT("That slot is empty.");break;
    case 23:case 54: Message=TEXT("The item was not found.");break;
    // Also the server's answer to destroying an indestructible item.
    case 24: Message=TEXT("You can't drop a soulbound item.");break;
    case 25: Message=TEXT("Out of range.");break;
    case 26: Message=TEXT("Tried to split more than number in stack.");break;
    case 27: Message=TEXT("Couldn't split those items.");break;
    case 28: Message=TEXT("Missing reagent");break;
    case 29: Message=TEXT("You don't have enough money.");break;
    case 30: Message=TEXT("Not a bag.");break;
    case 31: Message=TEXT("You can only do that with empty bags.");break;
    case 32: Message=TEXT("You don't own that item.");break;
    case 33:case 41: Message=TEXT("You can only equip one quiver.");break;
    case 34: Message=TEXT("You must purchase that bag slot first");break;
    case 35: Message=TEXT("You are too far away from a bank.");break;
    case 36: Message=TEXT("Item is locked.");break;
    case 37: Message=TEXT("You are stunned");break;
    case 38: Message=TEXT("You can't do that when you're dead.");break;
    case 39: Message=TEXT("You can't do that right now.");break;
    case 40: Message=TEXT("Internal Bag Error");break;
    case 42: Message=TEXT("You can only equip one ammo pouch.");break;
    case 43: Message=TEXT("Stackable items can't be wrapped.");break;
    case 44: Message=TEXT("Equipped items can't be wrapped.");break;
    case 45: Message=TEXT("Wrapped items can't be wrapped.");break;
    case 46: Message=TEXT("Bound items can't be wrapped.");break;
    case 47: Message=TEXT("Unique items can't be wrapped.");break;
    case 48: Message=TEXT("Bags can't be wrapped.");break;
    case 49: Message=TEXT("Already looted");break;
    case 50:case 91: Message=TEXT("Inventory is full.");break;
    case 51: Message=TEXT("Your bank is full");break;
    case 52:case 57: Message=TEXT("That item is currently sold out.");break;
    case 58: Message=TEXT("That object is busy.");break;
    case 60: Message=TEXT("You can't do that while in combat");break;
    case 61: Message=TEXT("You can't do that while disarmed");break;
    case 63: Message=TEXT("You don't have the required rank for that item");break;
    case 64: Message=TEXT("You don't have the required reputation for that item");break;
    case 65: Message=TEXT("You cannot equip another bag of that type");break;
    case 66: Message=TEXT("You can't loot that item now.");break;
    case 67:case 85:case 89: Message=TEXT("You cannot equip more than one of those.");break;
    case 68: Message=TEXT("You do not have the required items for that purchase");break;
    case 69: Message=TEXT("You don't have enough honor points");break;
    case 70: Message=TEXT("You don't have enough arena points");break;
    case 71: Message=TEXT("You have the maximum number of those gems in your inventory or socketed into items.");break;
    case 72: Message=TEXT("You can't mail soulbound items.");break;
    case 73: Message=TEXT("You can't split items while prospecting.");break;
    case 75: Message=TEXT("You have the maximum number of those gems socketed into equipped items.");break;
    case 76: Message=TEXT("You cannot socket more than one of those gems into a single item.");break;
    case 77: Message=TEXT("At gold limit");break;
    case 78: Message=TEXT("You can't do that while in an arena match");break;
    case 79: Message=TEXT("You can't trade a soulbound item.");break;
    case 80: Message=TEXT("You don't have the personal or team rating required to buy that item");break;
    case 82: Message=TEXT("Artifacts can only be mailed to your own characters.");break;
    case 86: Message=TEXT("Your level is too high to use that item");break;
    case 87: Message=FString::Printf(TEXT("You must reach level %u to purchase that item."),RequiredLevel);break;
    case 88: Message=TEXT("You do not have the required talent to equip that.");break;
    default: Message=TEXT("You can't do that right now.");break;
    }
    return Message;
}

// ── SMSG_INVENTORY_CHANGE_FAILURE ─────────────────────────────────────────
// uint8 error code

void FWowPacketHandler::HandleInventoryChangeFailure(FPacketReader& R)
{
    if (!R.CanRead(1)) return;

    uint8 ErrorCode = R.ReadU8();

    // WotLK InventoryResult values (TrinityCore 3.3.5 ItemDefines.h), shown with the
    // client's own GlobalStrings ERR_* texts. Packet: u8 result, then u64 item, u64 item2,
    // u8 bag and, for the level results, u32 required level.
    if(ErrorCode==0)return;
    uint32 RequiredLevel=0;
    if((ErrorCode==1||ErrorCode==87)&&R.CanRead(8+8+1+4)){R.ReadU64();R.ReadU64();R.ReadU8();RequiredLevel=R.ReadU32();}
    const FString Message=InventoryErrorText(ErrorCode,RequiredLevel);
    OnUiErrorMessage.Broadcast(Message);
    UE_LOG(LogWowPacket,Log,TEXT("Inventory error %u: %s"),ErrorCode,*Message);
}

// ── SMSG_LOOT_RESPONSE ──────────────────────────────────────────────────
// packed guid loot_guid, uint8 loot_type, uint32 gold, uint8 item_count, then for each item:
// uint8 index, uint32 item_id, uint32 count, uint32 display_id, uint8 quality, ...

void FWowPacketHandler::HandleLootResponse(FPacketReader& R)
{
    const uint64 LootGuid = R.ReadU64();
    const uint8 LootType = R.ReadU8();
    if (LootType == 0)
    {
        const uint8 Error = R.ReadU8();
        if (!R.HasFailed()) UE_LOG(LogWowPacket, Warning, TEXT("Loot rejected for %llu (reason %u)"), LootGuid, Error);
        return;
    }
    const uint32 Gold = R.ReadU32();
    const uint8 Count = R.ReadU8();
    if (!R.Require(static_cast<int32>(Count) * 22, TEXT("loot items"))) return;
    TArray<FWowLootItem> Items;
    Items.Reserve(Count);
    for (uint32 I = 0; I < Count; ++I)
    {
        FWowLootItem& Item = Items.AddDefaulted_GetRef();
        Item.Index = R.ReadU8();
        Item.ItemId = R.ReadU32();
        Item.Count = R.ReadU32();
        Item.DisplayId = R.ReadU32();
        Item.RandomSuffix = R.ReadU32();
        Item.RandomProperty = R.ReadU32();
        Item.SlotType = R.ReadU8();
    }
    if (R.HasFailed()) return;
    for (const FWowLootItem& Item : Items)
        UE_LOG(LogWowPacket, Log, TEXT("Loot %llu slot %u: item %u x%u slotType %u"), LootGuid, Item.Index, Item.ItemId, Item.Count, Item.SlotType);
    ActiveLootGuid = LootGuid;
    OnLootOpened.Broadcast(LootGuid, LootType, Gold, Items);
}

void FWowPacketHandler::HandleLootReleaseResponse(FPacketReader& R)
{
    const uint64 Guid = R.ReadU64();
    R.ReadU8();
    if (!R.HasFailed() && Guid != ActiveLootGuid)
        // A release for loot we never opened: the server refused CMSG_LOOT (out of range,
        // not lootable yet, or already being looted) without a LOOT_RESPONSE error.
        UE_LOG(LogWowPacket, Warning, TEXT("Loot for %llu refused by server (released without opening)"), Guid);
    if (R.HasFailed() || Guid != ActiveLootGuid) return;
    ActiveLootGuid = 0;
    OnLootClosed.Broadcast();
}

void FWowPacketHandler::HandleLootRemoved(FPacketReader& R)
{
    const uint8 Slot = R.ReadU8();
    if (!R.HasFailed() && ActiveLootGuid != 0) OnLootSlotRemoved.Broadcast(Slot);
}

void FWowPacketHandler::HandleLootClearMoney(FPacketReader& R)
{
    if (ActiveLootGuid != 0) OnLootMoneyCleared.Broadcast();
}

// ── SMSG_ITEM_PUSH_RESULT ──────────────────────────────────────────────────
// packed guid target_guid, uint8 created (0=existing item, 1=new), uint8 created2,
// uint8 bag_slot, uint32 item_slot, uint32 item_id, uint32 random_property_id,
// uint32 random_suffix, uint32 count, uint32 charges_count, uint8 charges1, uint8 charges2,
// uint8 charges3, uint8 charges4, uint8 charges5, uint32 duration, (optional item bonuses)

void FWowPacketHandler::HandleItemPushResult(FPacketReader& R)
{
    if (!R.CanRead(2)) return; // minimum: packed guid + created flag

    uint64 TargetGuid = R.ReadPackedGuid();

    if (!R.CanRead(18)) return; // created + created2 + bag + slot + item_id + other fields
    uint8 Created = R.ReadU8();
    uint8 Created2 = R.ReadU8();
    uint8 BagSlot = R.ReadU8();
    uint32 ItemSlot = R.ReadU32();
    uint32 ItemId = R.ReadU32();
    uint32 RandomPropertyId = R.ReadU32();

    if (!R.CanRead(8)) return; // random_suffix + count
    uint32 RandomSuffix = R.ReadU32();
    uint32 Count = R.ReadU32();

    // Skip remaining fields for now (charges, duration, etc.)

    const TCHAR* CreatedMsg = (Created == 1) ? TEXT("new") : TEXT("existing");
    UE_LOG(LogWowPacket, Log, TEXT("ITEM_PUSH_RESULT: target=%llu %s item=%u count=%u bag=%d slot=%u"),
           TargetGuid, CreatedMsg, ItemId, Count, BagSlot, ItemSlot);
}

// ── SMSG_LIST_INVENTORY ──────────────────────────────────────────────────────
// Full uint64 GUID, uint8 row count, then eight uint32 fields per row.
void FWowPacketHandler::HandleListInventory(FPacketReader& R)
{
    if (!R.CanRead(9)) return;
    const uint64 VendorGuid = R.ReadU64();
    const uint8 Count = R.ReadU8();
    if (!R.CanRead(Count ? Count * 32 : 1)) return;
    TArray<FWowVendorItem> Items;
    for (uint8 Index = 0; Index < Count; ++Index)
    {
        FWowVendorItem Item;
        Item.Slot = R.ReadU32(); Item.ItemId = R.ReadU32(); Item.DisplayId = R.ReadU32();
        Item.MaxCount = R.ReadU32(); Item.Price = R.ReadU32(); Item.MaxDurability = R.ReadU32();
        Item.BuyCount = R.ReadU32(); Item.ExtendedCost = R.ReadU32();
        if (!Item.Slot || !Item.ItemId || !Item.BuyCount) return;
        Items.Add(Item);
    }
    if (!Count) R.ReadU8();
    ActiveVendorGuid = VendorGuid;
    VendorItems = Items;
    VendorMessage = Count ? FString() : TEXT("This merchant has no goods available.");
    ++VendorRevision;
    OnVendorOpened.Broadcast(VendorGuid, Items);
}

void FWowPacketHandler::RequestItemTemplate(uint32 Entry)
{
    if(!Entry || ItemTemplates.Contains(Entry) || UnavailableItemQueries.Contains(Entry) || !OnSendPacket.IsBound()) return;
    const double Now=FPlatformTime::Seconds();
    if(const double* Sent=PendingItemQueries.Find(Entry)) if(Now-*Sent<10.0) return;
    PendingItemQueries.Add(Entry,Now);
    TArray<uint8> Query; Query.SetNumZeroed(12);
    FMemory::Memcpy(Query.GetData(),&Entry,4);
    OnSendPacket.Execute(WowOpcode::CMSG_ITEM_QUERY_SINGLE,Query);
}

void FWowPacketHandler::HandleInspectTalent(FPacketReader& R)
{
    // Player::BuildPlayerTalentsInfoData + BuildEnchantmentsInfoData (3.3.5).
    FInspectData Data;
    Data.Guid = R.ReadPackedGuid();
    Data.FreeTalentPoints = R.ReadU32();
    const uint8 SpecCount = R.ReadU8();
    const uint8 ActiveSpec = R.ReadU8();
    if (SpecCount > 2 || R.HasFailed()) return;
    for (uint8 Spec = 0; Spec < SpecCount; ++Spec)
    {
        const uint8 TalentCount = R.ReadU8();
        for (uint8 I = 0; I < TalentCount; ++I)
        {
            const uint32 TalentId = R.ReadU32();
            const uint8 Rank = R.ReadU8();
            if (Spec == ActiveSpec) Data.TalentRanks.Add(TalentId, uint8(Rank + 1));
        }
        const uint8 GlyphCount = R.ReadU8();
        for (uint8 I = 0; I < GlyphCount; ++I)
        {
            const uint16 Glyph = R.ReadU16();
            if (Spec == ActiveSpec) Data.Glyphs.Add(Glyph);
        }
    }
    const uint32 SlotMask = R.ReadU32();
    for (int32 Slot = 0; Slot < 19 && !R.HasFailed(); ++Slot)
    {
        if (!(SlotMask & (1u << Slot))) continue;
        Data.ItemEntries[Slot] = R.ReadU32();
        const uint16 EnchantMask = R.ReadU16();
        Data.ItemEnchants[Slot].SetNumZeroed(12);
        for (int32 Enchant = 0; Enchant < 12; ++Enchant)
            if (EnchantMask & (1u << Enchant)) Data.ItemEnchants[Slot][Enchant] = R.ReadU16();
        R.ReadU16();        // unknown
        R.ReadPackedGuid(); // creator
        R.ReadU32();        // unknown
        if (Data.ItemEntries[Slot]) RequestItemTemplate(Data.ItemEntries[Slot]);
    }
    if (R.HasFailed() || !Data.Guid) return;
    Data.Revision = InspectData.Revision + 1;
    InspectData = MoveTemp(Data);
}

void FWowPacketHandler::HandleAllAchievementData(FPacketReader& R)
{
    // AchievementMgr::BuildAllDataPacket: (u32 id, u32 packed date)... -1, then criteria
    // (u32 id, packed counter, packed guid, u32 flags, u32 date, u32 timer, u32 timer)... -1.
    TMap<uint32, uint32> Completed;
    TMap<uint32, uint64> Criteria;
    while (R.CanRead(4))
    {
        const uint32 Id = R.ReadU32();
        if (Id == 0xFFFFFFFFu) break;
        Completed.Add(Id, R.ReadU32());
    }
    while (R.CanRead(4))
    {
        const uint32 Id = R.ReadU32();
        if (Id == 0xFFFFFFFFu) break;
        const uint64 Counter = R.ReadPackedGuid();
        R.ReadPackedGuid();
        R.ReadU32(); R.ReadU32(); R.ReadU32(); R.ReadU32();
        Criteria.Add(Id, Counter);
    }
    if (R.HasFailed()) return;
    CompletedAchievements = MoveTemp(Completed);
    AchievementCriteria = MoveTemp(Criteria);
    ++AchievementRevision;
    UE_LOG(LogWowPacket, Log, TEXT("ALL_ACHIEVEMENT_DATA: %d achievements, %d criteria"), CompletedAchievements.Num(), AchievementCriteria.Num());
}

void FWowPacketHandler::HandleAchievementEarned(FPacketReader& R)
{
    const uint64 Guid = R.ReadPackedGuid();
    const uint32 Id = R.ReadU32();
    const uint32 Date = R.ReadU32();
    if (R.HasFailed() || Guid != EntityManager.LocalPlayerGuid) return;
    CompletedAchievements.Add(Id, Date);
    ++AchievementRevision;
}

void FWowPacketHandler::HandleCriteriaUpdate(FPacketReader& R)
{
    const uint32 Id = R.ReadU32();
    const uint64 Counter = R.ReadPackedGuid();
    if (R.HasFailed()) return;
    AchievementCriteria.Add(Id, Counter);
    ++AchievementRevision;
}

void FWowPacketHandler::HandleItemQueryResponse(FPacketReader& R)
{
    if (!R.CanRead(4)) return;
    const uint32 Entry = R.ReadU32();
    // An explicit negative reply differs from a missing or malformed response.
    if (Entry & 0x80000000) { PendingItemQueries.Remove(Entry&0x7fffffffu); UnavailableItemQueries.Add(Entry&0x7fffffffu); return; }
    FWowItemTemplateInfo Item; Item.Entry = Entry;
    Item.Class = R.ReadU32(); Item.SubClass = R.ReadU32(); R.ReadU32();
    Item.Name = R.ReadCString();
    for (int32 Index = 0; Index < 3; ++Index) R.ReadCString();
    if (!R.CanRead(88)) return;
    Item.DisplayId = R.ReadU32(); Item.Quality = R.ReadU32(); Item.Flags = R.ReadU32(); R.ReadU32();
    Item.BuyPrice = R.ReadU32(); Item.SellPrice = R.ReadU32(); Item.InventoryType = R.ReadU32();
    R.Skip(8); Item.ItemLevel = R.ReadU32(); Item.RequiredLevel = R.ReadU32();
    R.Skip(32); Item.MaxStack = R.ReadU32(); Item.ContainerSlots = R.ReadU32();
    const uint32 StatCount = R.ReadU32();
    if (StatCount > 10 || !R.CanRead(StatCount * 8 + 8 + 24 + 28 + 12 + 120 + 4)) return;
    for (uint32 Index = 0; Index < StatCount; ++Index)
    {
        const uint32 Type = R.ReadU32(); const int32 Value = static_cast<int32>(R.ReadU32());
        Item.Stats.Emplace(Type, Value);
    }
    Item.ScalingStatDistribution = R.ReadU32(); Item.ScalingStatValue = R.ReadU32();
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const float Min = R.ReadFloat(), Max = R.ReadFloat(); R.ReadU32();
        if (!FMath::IsFinite(Min) || !FMath::IsFinite(Max)) return;
        Item.DamageMin += Min; Item.DamageMax += Max;
    }
    Item.Armor = R.ReadU32();
    for (uint32& Resistance : Item.Resistances) Resistance = R.ReadU32();
    Item.DelayMs = R.ReadU32(); R.Skip(8);
    for (int32 Index = 0; Index < 5; ++Index)
    {
        Item.SpellIds[Index] = R.ReadU32(); Item.SpellTriggers[Index] = R.ReadU32(); R.Skip(16);
    }
    Item.Bonding = R.ReadU32(); Item.Description = R.ReadCString();
    if (!R.CanRead(116)) return;
    R.Skip(36); Item.Block = R.ReadU32(); R.ReadU32(); Item.MaxDurability = R.ReadU32(); R.Skip(16);
    for (uint32& Color : Item.SocketColors) { Color = R.ReadU32(); R.ReadU32(); }
    Item.SocketBonus = R.ReadU32(); R.Skip(24);
    if(R.HasFailed() || Item.Name.IsEmpty()) return;
    UnavailableItemQueries.Remove(Entry);
    PendingItemQueries.Remove(Entry);
    ItemTemplates.Add(Entry, MoveTemp(Item));
    ++VendorRevision;
    const FString& ItemName=ItemTemplates.FindChecked(Entry).Name;
    bool QuestUsesItem=false;
    for (auto& Quest : QuestLog)
        for (auto& Objective : Quest.Objectives)
            if (Objective.ItemId==Entry && Objective.Text!=ItemName) {Objective.Text=ItemName;QuestUsesItem=true;}
    for(auto& Pair : QuestTemplateCache)
        for(auto& Objective : Pair.Value.Objectives)
            if(Objective.ItemId==Entry) Objective.Text=ItemName;
    if (QuestUsesItem) OnQuestLogUpdated.Broadcast();
}

void FWowPacketHandler::HandleBuyItemResult(FPacketReader& R)
{
    if (!R.CanRead(20)) return;
    const uint64 Guid = R.ReadU64(); const uint32 Slot = R.ReadU32();
    const uint32 Stock = R.ReadU32(); R.ReadU32();
    if (Guid != ActiveVendorGuid) return;
    for (FWowVendorItem& Item : VendorItems) if (Item.Slot == Slot) Item.MaxCount = Stock;
    VendorMessage = TEXT("Purchase complete."); ++VendorRevision;
}

void FWowPacketHandler::HandleBuyItemFailure(FPacketReader& R)
{
    if (!R.CanRead(13)) return;
    const uint64 Guid = R.ReadU64(); R.ReadU32(); const uint8 Error = R.ReadU8();
    if (Guid != ActiveVendorGuid) return;
    VendorMessage = Error == 2 ? TEXT("You do not have enough money.")
        : TEXT("The merchant could not complete this purchase.");
    ++VendorRevision;
}

void FWowPacketHandler::HandleSellItemResult(FPacketReader& R)
{
    if (!R.CanRead(17)) return;
    const uint64 Guid = R.ReadU64(); R.ReadU64(); const uint8 Error = R.ReadU8();
    if (Guid != ActiveVendorGuid) return;
    VendorMessage = Error ? TEXT("This item could not be sold.") : TEXT("Item sold.");
    ++VendorRevision;
}

// ── ──────────────────────────────────────────────────────────────────────────
// ── Social / Guild / Friends System Handlers ────────────────────────────────
// ── ──────────────────────────────────────────────────────────────────────────

// 3.3.5 SMSG_CONTACT_LIST: scoped snapshot, not necessarily all contacts.
void FWowPacketHandler::HandleFriendList(FPacketReader& R)
{
    if (!R.Require(8, TEXT("contact list header"))) return;
    const uint32 RequestedFlags = R.ReadU32();
    const uint32 Count = R.ReadU32();
    // Every entry needs GUID, flags and at least the note terminator.
    if (Count > static_cast<uint32>(R.Remaining() / 13))
    {
        R.Fail(TEXT("Contact count exceeds packet size"));
        return;
    }
    TArray<FWowFriendInfo> Received;
    Received.Reserve(Count);
    for (uint32 i = 0; i < Count; ++i)
    {
        if (!R.Require(13, TEXT("contact"))) return;
        FWowFriendInfo Friend;
        Friend.Guid = R.ReadU64();
        Friend.Flags = R.ReadU32();
        Friend.Note = R.ReadCString();
        if (R.HasFailed()) return;
        if (Friend.Flags & 0x1)
        {
            if (!R.Require(1, TEXT("friend status"))) return;
            Friend.Status = R.ReadU8();
            if (Friend.Status)
            {
                if (!R.Require(12, TEXT("friend presence"))) return;
                Friend.AreaId = R.ReadU32();
                Friend.Level = static_cast<uint8>(FMath::Min<uint32>(R.ReadU32(), 255));
                Friend.Class = static_cast<uint8>(FMath::Min<uint32>(R.ReadU32(), 255));
            }
        }
        Received.Add(MoveTemp(Friend));
    }

    // Commit only a complete packet. Preserve other list types and resolved names.
    TArray<FWowFriendInfo> Updated = FriendsList;
    for (FWowFriendInfo& Contact : Updated) Contact.Flags &= ~RequestedFlags;
    for (FWowFriendInfo& Contact : Received)
    {
        FWowFriendInfo* Existing = Updated.FindByPredicate(
            [&Contact](const FWowFriendInfo& Item) { return Item.Guid == Contact.Guid; });
        if (Existing)
        {
            Contact.Name = Existing->Name;
            if (!(Contact.Flags & 0x1) && (Existing->Flags & 0x1))
            {
                Contact.Status = Existing->Status;
                Contact.AreaId = Existing->AreaId;
                Contact.Level = Existing->Level;
                Contact.Class = Existing->Class;
            }
            Contact.Flags |= Existing->Flags;
            *Existing = MoveTemp(Contact);
        }
        else Updated.Add(MoveTemp(Contact));
    }
    Updated.RemoveAll([](const FWowFriendInfo& Contact) { return Contact.Flags == 0; });
    FriendsList = MoveTemp(Updated);
    OnFriendListUpdated.Broadcast();
}

// SMSG_FRIEND_STATUS has a result code and GUID, with result-dependent fields.
// Presence (online/AFK/DND) is a separate bitmask, not the result code.
void FWowPacketHandler::HandleFriendStatus(FPacketReader& R)
{
    if (!R.Require(9, TEXT("friend result"))) return;
    const uint8 Result = R.ReadU8();
    const uint64 Guid = R.ReadU64();
    FWowFriendInfo* Existing = FriendsList.FindByPredicate(
        [Guid](const FWowFriendInfo& Item) { return Item.Guid == Guid; });
    FWowFriendInfo Contact = Existing ? *Existing : FWowFriendInfo();
    Contact.Guid = Guid;
    if (Result == 0x06 || Result == 0x07) Contact.Note = R.ReadCString();
    if (R.HasFailed()) return;
    if (Result == 0x02 || Result == 0x06)
    {
        if (!R.Require(13, TEXT("friend online presence"))) return;
        Contact.Status = R.ReadU8();
        Contact.AreaId = R.ReadU32();
        Contact.Level = static_cast<uint8>(FMath::Min<uint32>(R.ReadU32(), 255));
        Contact.Class = static_cast<uint8>(FMath::Min<uint32>(R.ReadU32(), 255));
    }
    switch (Result)
    {
    case 0x02: // FRIEND_ONLINE
    case 0x03: // FRIEND_OFFLINE
        if (!Existing || !(Contact.Flags & 0x1)) return;
        if (Result == 0x03) Contact.Status = 0;
        break;
    case 0x06: // FRIEND_ADDED_ONLINE
    case 0x07: // FRIEND_ADDED_OFFLINE
        Contact.Flags |= 0x1;
        if (Result == 0x07) Contact.Status = 0;
        break;
    case 0x05: Contact.Flags &= ~0x1u; Contact.Status = 0; break;
    case 0x0F: Contact.Flags |= 0x2; break; // IGNORE_ADDED
    case 0x10: Contact.Flags &= ~0x2u; break;
    case 0x16: Contact.Flags |= 0x4; break; // MUTE_ADDED
    case 0x17: Contact.Flags &= ~0x4u; break;
    default: return; // Failed requests must not mutate the local list.
    }
    if (!Contact.Status) { Contact.AreaId = 0; Contact.Level = 0; Contact.Class = 0; }
    if (Existing) *Existing = MoveTemp(Contact);
    else if (Contact.Flags) FriendsList.Add(MoveTemp(Contact));
    FriendsList.RemoveAll([](const FWowFriendInfo& Item) { return Item.Flags == 0; });
    OnFriendListUpdated.Broadcast();
}

// ── SMSG_GUILD_ROSTER ───────────────────────────────────────────────────────
// uint32 memberCount, FString motd, FString guildInfo, uint32 rankCount,
// for each rank: uint32 rights + uint32 goldLimit
// for each member: uint64 guid, uint8 status, FString name, uint32 rankId, uint8 level, uint8 class, uint8 gender, uint32 zoneId, FString publicNote, FString officerNote

void FWowPacketHandler::HandleGuildRoster(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 MemberCount = R.ReadU32();
    GuildMotd = R.ReadCString();
    FString GuildInfo = R.ReadCString();

    if (!R.CanRead(4)) return;
    uint32 RankCount = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("GUILD_ROSTER: %d members, %d ranks"), MemberCount, RankCount);
    UE_LOG(LogWowPacket, Verbose, TEXT("  MOTD: %s"), *GuildMotd);

    // Rank data: rights, gold per day and, r285, the six bank tabs' rights and stacks per day
    // (3.3.5 sends 56 bytes per rank; reading 8 put every member after the first rank out of step).
    GuildRanks.Reset();
    for (uint32 i = 0; i < RankCount && R.CanRead(56); ++i)
    {
        FWowGuildRankRights& Rank = GuildRanks.AddDefaulted_GetRef();
        Rank.Rights = R.ReadU32();
        Rank.GoldPerDay = R.ReadU32();
        for (int32 Tab = 0; Tab < 6; ++Tab)
        {
            Rank.TabRights[Tab] = R.ReadU32();
            Rank.TabSlots[Tab] = R.ReadU32();
        }
    }

    GuildRoster.Empty(MemberCount);

    // Read member data
    for (uint32 i = 0; i < MemberCount; ++i)
    {
        if (!R.CanRead(9)) break; // minimum: guid + status

        FWowGuildMember Member;
        Member.Guid = R.ReadU64();
        Member.Status = R.ReadU8();
        Member.Name = R.ReadCString();

        if (!R.CanRead(11)) break; // rankId + level + class + gender + zoneId
        Member.RankId = R.ReadU32();
        Member.Level = R.ReadU8();
        Member.Class = R.ReadU8();
        uint8 Gender = R.ReadU8(); // not stored in member struct
        Member.ZoneId = R.ReadU32();
        if (Member.Status == 0 && R.CanRead(4)) R.ReadFloat(); // r285: offline members: days since last logout

        Member.PublicNote = R.ReadCString();
        Member.OfficerNote = R.ReadCString();

        GuildRoster.Add(Member);

        UE_LOG(LogWowPacket, Verbose, TEXT("  Member: %s (level %d) rank=%d zone=%d"),
               *Member.Name, Member.Level, Member.RankId, Member.ZoneId);
    }

    OnGuildRosterUpdated.Broadcast();
}

// ── r285: guild bank ────────────────────────────────────────────────────────
// SMSG_GUILD_BANK_LIST: uint64 money, uint8 tab, int32 withdrawals left, uint8 full; (tab 0 + full) uint8 tab
// count + name/icon strings; uint8 item count, items: uint8 slot, uint32 entry (0 = empty) + details.
void FWowPacketHandler::HandleGuildBankList(FPacketReader& R)
{
    if (!R.CanRead(14)) return;
    GuildBank.Money = R.ReadU64();
    const uint8 Tab = R.ReadU8();
    const int32 Withdrawals = static_cast<int32>(R.ReadU32());
    const bool bFull = R.ReadU8() != 0;
    bool bTabsChanged = false;
    if (Tab == 0 && bFull)
    {
        const uint8 NumTabs = R.ReadU8();
        GuildBank.Tabs.SetNum(NumTabs);
        for (uint8 Index = 0; Index < NumTabs && !R.HasFailed(); ++Index)
        {
            GuildBank.Tabs[Index].Name = R.ReadCString();
            GuildBank.Tabs[Index].Icon = R.ReadCString();
        }
        bTabsChanged = true;
    }
    if (Tab >= GuildBank.Tabs.Num()) GuildBank.Tabs.SetNum(Tab + 1);
    FWowGuildBankTab& Target = GuildBank.Tabs[Tab];
    Target.WithdrawalsRemaining = Withdrawals;
    if (bFull) Target.Items.Reset(); // a full list leaves the empty slots out
    const uint8 Count = R.CanRead(1) ? R.ReadU8() : 0;
    for (uint8 Index = 0; Index < Count && R.CanRead(5); ++Index)
    {
        const uint8 Slot = R.ReadU8();
        const uint32 Entry = R.ReadU32();
        if (!Entry) { Target.Items.Remove(Slot); continue; }
        FWowGuildBankItem Item;
        Item.Entry = Entry;
        R.ReadU32(); // item flags
        Item.RandomPropertyId = static_cast<int32>(R.ReadU32());
        if (Item.RandomPropertyId) R.ReadU32(); // suffix factor
        Item.Count = static_cast<int32>(R.ReadU32());
        Item.EnchantId = static_cast<int32>(R.ReadU32());
        R.ReadU8(); // charges
        const uint8 Sockets = R.ReadU8();
        for (uint8 Socket = 0; Socket < Sockets && R.CanRead(5); ++Socket) { R.ReadU8(); R.ReadU32(); }
        if (R.HasFailed()) break;
        Target.Items.Add(Slot, Item);
        if (!ItemTemplates.Contains(Entry)) RequestItemTemplate(Entry);
    }
    UE_LOG(LogWowPacket, Log, TEXT("GUILD_BANK_LIST: tab %u full=%d items=%u tabs=%d money=%llu withdrawals=%d"),
        Tab, bFull ? 1 : 0, Count, GuildBank.Tabs.Num(), GuildBank.Money, Withdrawals);
    if (bTabsChanged) OnGuildBankEvent.Broadcast(TEXT("GUILDBANK_UPDATE_TABS"), -1);
    OnGuildBankEvent.Broadcast(TEXT("GUILDBANKBAGSLOTS_CHANGED"), -1);
    OnGuildBankEvent.Broadcast(TEXT("GUILDBANK_UPDATE_MONEY"), -1);
}

// MSG_GUILD_PERMISSIONS: uint32 rank, int32 rights, int32 gold per day, int8 purchased tabs,
// 6 x (int32 tab rights, int32 stacks left today).
void FWowPacketHandler::HandleGuildPermissions(FPacketReader& R)
{
    if (!R.CanRead(13)) return;
    GuildBank.RankId = R.ReadU32();
    GuildBank.RankRights = R.ReadU32();
    GuildBank.WithdrawGoldPerDay = static_cast<int32>(R.ReadU32());
    GuildBank.PurchasedTabs = static_cast<int8>(R.ReadU8());
    for (int32 Tab = 0; Tab < 6 && R.CanRead(8); ++Tab)
    {
        GuildBank.TabRights[Tab] = R.ReadU32();
        GuildBank.TabWithdrawLimit[Tab] = static_cast<int32>(R.ReadU32());
    }
    GuildBank.bPermissionsKnown = true;
    UE_LOG(LogWowPacket, Log, TEXT("GUILD_PERMISSIONS: rank %u rights 0x%08X tabs %d"), GuildBank.RankId, GuildBank.RankRights, GuildBank.PurchasedTabs);
    OnGuildBankEvent.Broadcast(TEXT("GUILDBANK_UPDATE_TABS"), -1);
}

void FWowPacketHandler::HandleGuildBankMoneyWithdrawn(FPacketReader& R)
{
    if (!R.CanRead(4)) return;
    GuildBank.RemainingWithdrawMoney = static_cast<int32>(R.ReadU32());
    OnGuildBankEvent.Broadcast(TEXT("GUILDBANK_UPDATE_WITHDRAWMONEY"), -1);
}

// r317 (Stefan 14.10: rogue combo points showed nowhere): SMSG_UPDATE_COMBO_POINTS = packed guid of
// the unit the points are on, uint8 points (0 clears). PLAYER_COMBO_POINTS makes ComboFrame update.
void FWowPacketHandler::HandleUpdateComboPoints(FPacketReader& R)
{
    const uint64 Target = R.ReadPackedGuid();
    if (!R.CanRead(1)) return;
    ComboTargetGuid = Target;
    ComboPoints = R.ReadU8();
    UE_LOG(LogTemp, Verbose, TEXT("Combo points: %u on %llu"), ComboPoints, ComboTargetGuid);
    OnGuildBankEvent.Broadcast(TEXT("PLAYER_COMBO_POINTS"), -1); // generic Lua event relay
}

// MSG_GUILD_BANK_LOG_QUERY: uint8 tab (6 = money), uint8 count, entries: int8 type, uint64 player,
// item types: uint32 item, uint32 count (+ uint8 other tab for moves); money types: uint32 copper;
// then uint32 seconds ago.
void FWowPacketHandler::HandleGuildBankLog(FPacketReader& R)
{
    if (!R.CanRead(2)) return;
    const uint8 Tab = R.ReadU8();
    const uint8 Count = R.ReadU8();
    TArray<FWowGuildBankLogEntry>& Log = GuildBank.Logs.FindOrAdd(Tab);
    Log.Reset();
    for (uint8 Index = 0; Index < Count && R.CanRead(9); ++Index)
    {
        FWowGuildBankLogEntry Entry;
        Entry.Type = static_cast<int8>(R.ReadU8());
        Entry.Player = R.ReadU64();
        if (Entry.Type == 1 || Entry.Type == 2 || Entry.Type == 3 || Entry.Type == 7)
        {
            Entry.Entry = R.ReadU32();
            Entry.Count = R.ReadU32();
            if (Entry.Type == 3 || Entry.Type == 7) Entry.OtherTab = R.ReadU8();
            if (Entry.Entry && !ItemTemplates.Contains(Entry.Entry)) RequestItemTemplate(Entry.Entry);
        }
        else
        {
            Entry.Money = R.ReadU32();
        }
        Entry.SecondsAgo = R.ReadU32();
        if (R.HasFailed()) break;
        Log.Add(Entry);
    }
    OnGuildBankEvent.Broadcast(TEXT("GUILDBANKLOG_UPDATE"), -1);
}

void FWowPacketHandler::HandleGuildBankText(FPacketReader& R)
{
    if (!R.CanRead(1)) return;
    const uint8 Tab = R.ReadU8();
    const FString Text = R.ReadCString();
    if (Tab >= GuildBank.Tabs.Num()) GuildBank.Tabs.SetNum(Tab + 1);
    GuildBank.Tabs[Tab].Text = Text;
    OnGuildBankEvent.Broadcast(TEXT("GUILDBANK_UPDATE_TEXT"), Tab + 1);
}

// ── SMSG_GUILD_EVENT ────────────────────────────────────────────────────────
// uint8 eventType, uint8 stringCount, then read each string

void FWowPacketHandler::HandleGuildEvent(FPacketReader& R)
{
    if (!R.CanRead(2)) return;

    uint8 EventType = R.ReadU8();
    uint8 StringCount = R.ReadU8();

    TArray<FString> Strings;
    Strings.Reserve(StringCount);

    for (int32 i = 0; i < StringCount; ++i)
    {
        FString Str = R.ReadCString();
        Strings.Add(Str);
    }

    UE_LOG(LogWowPacket, Log, TEXT("GUILD_EVENT: type=%d strings=%d"), EventType, StringCount);
    for (int32 i = 0; i < Strings.Num(); ++i)
    {
        UE_LOG(LogWowPacket, Verbose, TEXT("  String[%d]: %s"), i, *Strings[i]);
    }
}

// ── SMSG_CHANNEL_NOTIFY ─────────────────────────────────────────────────────
// uint8 type, FString channelName

void FWowPacketHandler::HandleChannelNotify(FPacketReader& R)
{
    if (!R.CanRead(1)) return;

    uint8 Type = R.ReadU8();
    FString ChannelName = R.ReadCString();
    if (R.HasFailed()) return;

    // CHAT_YOU_JOINED_NOTICE (0x02): name, uint8 flags, uint32 ChatChannels.dbc id, uint32.
    // CHAT_YOU_LEFT_NOTICE (0x03): name, uint32 id, uint8. The original client numbers
    // channels by the first free slot at join time (/1 General, /2 Trade ...), and a zone
    // change (the server swaps "General - Hellfire Peninsula" for "General - Shattrath City"
    // without a leave notice) keeps the built-in channel in its slot: "Changed Channel".
    JoinedChannelIds.SetNum(JoinedChannels.Num());
    // UE 5.8's checked Printf needs literal formats: the verb is passed as text instead.
    const auto Notice = [this](const TCHAR* Verb, int32 Number, const FString& Name)
    {
        OnChatMessage.Broadcast(0, 0, 0, FString(), FString::Printf(TEXT("%s Channel: [%d. %s]"), Verb, Number, *Name), FString());
    };
    if (Type == 0x02)
    {
        uint32 ChannelId = 0;
        if (R.CanRead(5)) { R.Skip(1); ChannelId = R.ReadU32(); }
        if (GetChannelNumber(ChannelName) == 0)
        {
            int32 Slot = ChannelId != 0 ? JoinedChannelIds.IndexOfByKey(ChannelId) : INDEX_NONE;
            const bool bChanged = Slot != INDEX_NONE;
            if (!bChanged) Slot = JoinedChannels.IndexOfByKey(FString());
            if (Slot == INDEX_NONE) { Slot = JoinedChannels.Add(ChannelName); JoinedChannelIds.Add(ChannelId); }
            else { JoinedChannels[Slot] = ChannelName; JoinedChannelIds[Slot] = ChannelId; }
            // Built-in channels joined on login/zone entry stay quiet like the original; a zone
            // swap says "Changed Channel", and channels you /join say "Joined Channel".
            if (bChanged) Notice(TEXT("Changed"), Slot + 1, ChannelName);
            else if (ChannelId == 0) Notice(TEXT("Joined"), Slot + 1, ChannelName);
        }
    }
    else if (Type == 0x03)
    {
        const int32 Number = GetChannelNumber(ChannelName);
        if (Number > 0)
        {
            Notice(TEXT("Left"), Number, JoinedChannels[Number - 1]);
            JoinedChannels[Number - 1].Reset();
            JoinedChannelIds[Number - 1] = 0;
        }
        while (JoinedChannels.Num() > 0 && JoinedChannels.Last().IsEmpty()) { JoinedChannels.Pop(); JoinedChannelIds.Pop(); }
    }
    UE_LOG(LogWowPacket, Log, TEXT("CHANNEL_NOTIFY: type=%d channel='%s'"), Type, *ChannelName);
}

int32 FWowPacketHandler::GetChannelNumber(const FString& ChannelName) const
{
    for (int32 I = 0; I < JoinedChannels.Num(); ++I)
        if (!JoinedChannels[I].IsEmpty() && JoinedChannels[I].Equals(ChannelName, ESearchCase::IgnoreCase)) return I + 1;
    // Zone channels may be named "General - Elwynn Forest" while a caller passes "General".
    for (int32 I = 0; I < JoinedChannels.Num(); ++I)
    {
        FString Base = JoinedChannels[I];
        Base.Split(TEXT(" - "), &Base, nullptr);
        if (!Base.IsEmpty() && Base.Equals(ChannelName, ESearchCase::IgnoreCase)) return I + 1;
    }
    return 0;
}

FString FWowPacketHandler::GetChannelNameByNumber(int32 Number) const
{
    return JoinedChannels.IsValidIndex(Number - 1) ? JoinedChannels[Number - 1] : FString();
}

// ── SMSG_GROUP_LIST ─────────────────────────────────────────────────────────
// uint8 groupFlags, uint8 selfSubgroup, uint8 selfFlags, uint8 selfRoles,
// if LFG: uint8 completedFlag, uint32 dungeonId,
// uint64 groupGuid, uint32 counter, uint32 otherMemberCount,
// repeated otherMemberCount: CString name, uint64 guid, uint8 online, uint8 subgroup, uint8 flags, uint8 roles,
// uint64 leaderGuid, optional loot data

void FWowPacketHandler::HandleGroupList(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    const uint8 RawGroupType = R.ReadU8();
    const uint8 SelfSubgroup = R.ReadU8();
    const uint8 SelfFlags = R.ReadU8();
    const uint8 SelfRoles = R.ReadU8();

    if ((RawGroupType & WowGroupType::LFG) != 0)
    {
        if (!R.CanRead(5)) return;
        R.Skip(1); // completed dungeon flag
        R.Skip(4); // dungeon id
    }

    if (!R.CanRead(16)) return;
    const uint64 GroupGuid = R.ReadU64();
    const uint32 Counter = R.ReadU32();
    const uint32 OtherMemberCount = R.ReadU32();

    const uint64 PreviousGroupGuid = GroupInfo.GroupGuid;
    const bool bHadRaidTargets = RaidTargets.HasAnyIcons();
    const bool bHadReadyCheck = ReadyCheck.bActive;

    GroupInfo.Clear();
    GroupInfo.RawGroupType = RawGroupType;
    GroupInfo.GroupType = (RawGroupType & WowGroupType::RAID) != 0 ? 1 : 0;
    GroupInfo.GroupGuid = GroupGuid;
    GroupInfo.SelfSubgroup = SelfSubgroup;
    GroupInfo.SelfFlags = SelfFlags;
    GroupInfo.SelfRoles = SelfRoles;
    GroupInfo.Members.Reserve(OtherMemberCount);

    for (uint32 i = 0; i < OtherMemberCount; ++i)
    {
        const FString MemberName = R.ReadCString();

        if (!R.CanRead(12)) break; // guid + online + subgroup + flags + roles
        const uint64 MemberGuid = R.ReadU64();
        const uint8 Online = R.ReadU8();
        const uint8 MemberSubgroup = R.ReadU8();
        const uint8 MemberFlags = R.ReadU8();
        const uint8 MemberRoles = R.ReadU8();

        FWowGroupMember Member;
        Member.Guid = MemberGuid;
        Member.Name = MemberName;
        Member.Group = MemberSubgroup;
        Member.Flags = MemberFlags;
        Member.Roles = MemberRoles;
        Member.Status = Online;

        // Try to get level and class from entity data
        if (auto* Entity = EntityManager.Find(MemberGuid))
        {
            if (Entity->IsUnit())
            {
                Member.Level = static_cast<uint8>(Entity->GetLevel());
                Member.Class = static_cast<uint8>(Entity->GetFieldByte(UnitField::BYTES_0, 1));
            }
        }

        GroupInfo.Members.Add(Member);

        UE_LOG(LogWowPacket, Verbose, TEXT("  Member: %s (GUID=%llu online=%d level=%d class=%d)"),
               *MemberName, MemberGuid, Online, Member.Level, Member.Class);
    }

    if (!R.CanRead(8)) return;
    GroupInfo.LeaderGuid = R.ReadU64();

    if (OtherMemberCount > 0 && R.CanRead(12))
    {
        R.Skip(1); // loot method
        R.Skip(8); // master looter guid
        R.Skip(1); // loot threshold
        R.Skip(1); // dungeon difficulty
        R.Skip(1); // raid difficulty
        if (R.CanRead(1))
        {
            R.Skip(1); // heroic raid difficulty toggle
        }
    }

    const bool bHasGroup = (GroupInfo.LeaderGuid != 0) || (OtherMemberCount > 0);
    GroupInfo.MemberCount = bHasGroup ? static_cast<uint8>(FMath::Min<uint32>(OtherMemberCount + 1, MAX_uint8)) : 0;

    if (!bHasGroup)
    {
        GroupInfo.Clear();
    }

    const bool bGroupChanged = PreviousGroupGuid != 0 && PreviousGroupGuid != GroupInfo.GroupGuid;

    if (!GroupInfo.IsRaidGroup() || bGroupChanged)
    {
        RaidTargets.Clear();
        ReadyCheck.Clear();
    }

    UE_LOG(LogWowPacket, Log, TEXT("GROUP_LIST: rawType=0x%02X normalizedType=%d members=%d leader=%llu groupGuid=%llu counter=%u"),
        RawGroupType,
        GroupInfo.GroupType,
        GroupInfo.MemberCount,
        GroupInfo.LeaderGuid,
        GroupGuid,
        Counter);

    OnGroupUpdated.Broadcast();

    if ((bHadRaidTargets && !RaidTargets.HasAnyIcons()) || (bHadReadyCheck && !ReadyCheck.bActive))
    {
        if (bHadRaidTargets)
        {
            OnRaidTargetsUpdated.Broadcast(RaidTargets);
        }
        if (bHadReadyCheck)
        {
            OnReadyCheckUpdated.Broadcast(ReadyCheck);
        }
    }
}

// ── MSG_RAID_TARGET_UPDATE ─────────────────────────────────────────────────
// uint8 mode
// mode 1: repeated uint8 icon, uint64 targetGuid
// mode 0: uint64 setterGuid, uint8 icon, uint64 targetGuid

void FWowPacketHandler::HandleRaidTargetUpdate(FPacketReader& R)
{
    if (!R.CanRead(1)) return;

    const uint8 Mode = R.ReadU8();
    if (Mode == 1)
    {
        RaidTargets.Clear();
        while (R.CanRead(9))
        {
            const uint8 IconIndex = R.ReadU8();
            const uint64 TargetGuid = R.ReadU64();
            RaidTargets.SetTarget(IconIndex, TargetGuid);
        }

        UE_LOG(LogWowPacket, Log, TEXT("RAID_TARGET_UPDATE: received icon list"));
    }
    else
    {
        if (!R.CanRead(17)) return;

        const uint64 SetterGuid = R.ReadU64();
        const uint8 IconIndex = R.ReadU8();
        const uint64 TargetGuid = R.ReadU64();

        RaidTargets.SetTarget(IconIndex, TargetGuid);
        UE_LOG(LogWowPacket, Log, TEXT("RAID_TARGET_UPDATE: setter=%llu icon=%d target=%llu"), SetterGuid, IconIndex, TargetGuid);
    }

    OnRaidTargetsUpdated.Broadcast(RaidTargets);
}

// ── MSG_RAID_READY_CHECK ───────────────────────────────────────────────────
// uint64 initiatorGuid

void FWowPacketHandler::HandleRaidReadyCheck(FPacketReader& R)
{
    if (!R.CanRead(8)) return;

    const uint64 InitiatorGuid = R.ReadU64();
    ReadyCheck.Begin(InitiatorGuid);
    UE_LOG(LogWowPacket, Log, TEXT("RAID_READY_CHECK: initiator=%llu"), InitiatorGuid);
    OnReadyCheckUpdated.Broadcast(ReadyCheck);
}

// ── MSG_RAID_READY_CHECK_CONFIRM ───────────────────────────────────────────
// uint64 responderGuid, uint8 state

void FWowPacketHandler::HandleRaidReadyCheckConfirm(FPacketReader& R)
{
    if (!R.CanRead(9)) return;

    const uint64 ResponderGuid = R.ReadU64();
    const uint8 ResponseState = R.ReadU8();

    if (!ReadyCheck.bActive)
    {
        ReadyCheck.Begin(0);
    }

    ReadyCheck.SetResponse(ResponderGuid, ResponseState);
    UE_LOG(LogWowPacket, Log, TEXT("RAID_READY_CHECK_CONFIRM: responder=%llu state=%d"), ResponderGuid, ResponseState);
    OnReadyCheckUpdated.Broadcast(ReadyCheck);
}

// ── MSG_RAID_READY_CHECK_FINISHED ──────────────────────────────────────────

void FWowPacketHandler::HandleRaidReadyCheckFinished(FPacketReader& R)
{
    static_cast<void>(R);
    ReadyCheck.Clear();
    UE_LOG(LogWowPacket, Log, TEXT("RAID_READY_CHECK_FINISHED"));
    OnReadyCheckUpdated.Broadcast(ReadyCheck);
}

// ── SMSG_PARTY_COMMAND_RESULT ──────────────────────────────────────────────
// uint32 command, FString member, uint32 result

void FWowPacketHandler::HandlePartyCommandResult(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 Command = R.ReadU32();
    FString Member = R.ReadCString();

    if (!R.CanRead(4)) return;
    uint32 Result = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("PARTY_COMMAND_RESULT: command=%d member='%s' result=%d"),
           Command, *Member, Result);

    OnPartyCommandResult.Broadcast(static_cast<uint8>(Command), Member, static_cast<uint8>(Result));
}

// ── SMSG_GROUP_INVITE ───────────────────────────────────────────────────────
// FString inviterName

void FWowPacketHandler::HandleGroupInvite(FPacketReader& R)
{
    // 3.3.5: uint8 status (1 = may accept), CString inviter, uint32, uint8 count, uint32.
    // The status byte used to end up as a garbage first character of the name.
    R.ReadU8();
    FString InviterName = R.ReadCString();
    UE_LOG(LogWowPacket, Log, TEXT("GROUP_INVITE from '%s'"), *InviterName);

    OnGroupInviteReceived.Broadcast(InviterName);
}

// ── SMSG_SHOWTAXINODES ──────────────────────────────────────────────────────
// uint32 windowFlags, uint64 npcGuid, uint32 currentNodeId, variable-length knownNodes bitmask

void FWowPacketHandler::HandleShowTaxiNodes(FPacketReader& R)
{
    if (!R.CanRead(16)) return; // windowFlags + npcGuid + currentNodeId

    uint32 WindowFlags = R.ReadU32();
    uint64 NpcGuid = R.ReadU64();
    uint32 CurrentNodeId = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("SHOWTAXINODES: npc=%llu current=%d flags=0x%X"),
           NpcGuid, CurrentNodeId, WindowFlags);

    // Clear and update taxi data
    TaxiData.Clear();
    TaxiData.NpcGuid = NpcGuid;
    TaxiData.CurrentNodeId = CurrentNodeId;
    TaxiData.bTaxiWindowOpen = true;

    // Load taxi nodes from DBC if not already loaded
    if (TaxiData.AllNodes.IsEmpty())
    {
        if (FDbcStore::Get().IsLoaded())
        {
            const auto& TaxiNodesDbc = FDbcStore::Get().TaxiNodes();
            for (const auto& DbcNode : TaxiNodesDbc.GetAll())
            {
                FWowTaxiNode Node(DbcNode.ID, DbcNode.MapID, DbcNode.X, DbcNode.Y, DbcNode.Z, DbcNode.Name);
                TaxiData.AllNodes.Add(Node);
            }
            UE_LOG(LogWowPacket, Log, TEXT("Loaded %d taxi nodes from DBC"), TaxiData.AllNodes.Num());
        }
    }

    // Read known nodes bitmask (variable length)
    // Each byte contains 8 node flags, calculate how many bytes we need
    int32 MaxNodeId = 256;
    if (!TaxiData.AllNodes.IsEmpty())
    {
        MaxNodeId = 0;
        for (const auto& Node : TaxiData.AllNodes)
        {
            MaxNodeId = FMath::Max(MaxNodeId, (int32)Node.Id);
        }
    }
    const int32 BitmaskBytes = (MaxNodeId + 7) / 8;

    TaxiData.KnownNodes.SetNum(MaxNodeId + 1);
    TaxiData.KnownNodes.Init(false, MaxNodeId + 1);

    for (int32 ByteIndex = 0; ByteIndex < BitmaskBytes && R.CanRead(1); ++ByteIndex)
    {
        uint8 Byte = R.ReadU8();
        for (int32 BitIndex = 0; BitIndex < 8; ++BitIndex)
        {
            // TaxiMask bit n is node n+1 (server: (id-1)/32, 1 << ((id-1)%32)): every flight
            // point showed as its neighbour's (Stefan 10.10: undiscovered nodes listed as known).
            const int32 NodeId = ByteIndex * 8 + BitIndex + 1;
            if (NodeId < TaxiData.KnownNodes.Num() && (Byte & (1 << BitIndex)))
            {
                TaxiData.KnownNodes[NodeId] = true;
            }
        }
    }

    int32 KnownCount = 0;
    for (bool bKnown : TaxiData.KnownNodes)
    {
        if (bKnown) KnownCount++;
    }
    UE_LOG(LogWowPacket, Log, TEXT("Player knows %d taxi nodes"), KnownCount);

    OnTaxiNodesShown.Broadcast(NpcGuid, CurrentNodeId);
}

// ── SMSG_ACTIVATETAXIREPLY ──────────────────────────────────────────────────
// uint32 result (0=OK, 1=unspecified error, etc.)

void FWowPacketHandler::HandleActivateTaxiReply(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 Result = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("ACTIVATETAXIREPLY: result=%d"), Result);

    OnTaxiActivateReply.Broadcast(static_cast<uint8>(Result));

    // Close taxi window
    TaxiData.bTaxiWindowOpen = false;
}

// ── SMSG_NEW_TAXI_PATH ──────────────────────────────────────────────────────
// No payload, just refresh taxi status

void FWowPacketHandler::HandleNewTaxiPath(FPacketReader& R)
{
    UE_LOG(LogWowPacket, Log, TEXT("NEW_TAXI_PATH: refreshing taxi data"));

    // This packet indicates that the player's taxi path has been updated
    // The client should refresh its taxi information
    TaxiData.bTaxiWindowOpen = false;
}

// ── SMSG_WHO ────────────────────────────────────────────────────────────────
// uint32 displayCount, uint32 matchCount, for each: FString name, FString guildName,
// uint32 level, uint32 classId, uint32 raceId, uint8 gender, uint32 zoneId

void FWowPacketHandler::HandleWho(FPacketReader& R)
{
    if (!R.CanRead(8)) return;

    uint32 DisplayCount = R.ReadU32();
    uint32 MatchCount = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("WHO: displaying %d of %d matches"), DisplayCount, MatchCount);
    const bool bToFrame = bWhoForFrame;
    bWhoForFrame = false;
    WhoResults.Reset();
    WhoTotalCount = MatchCount;

    for (uint32 i = 0; i < DisplayCount; ++i)
    {
        FString Name = R.ReadCString();
        FString PlayerGuild = R.ReadCString();

        if (!R.CanRead(17)) break; // level + class + race + gender (u8) + zone
        uint32 Level = R.ReadU32();
        uint32 ClassId = R.ReadU32();
        uint32 RaceId = R.ReadU32();
        R.ReadU8(); // gender (3.3.5 sends it between race and zone)
        uint32 ZoneId = R.ReadU32();

        UE_LOG(LogWowPacket, Verbose, TEXT("  %s <%s> level %d class=%d race=%d zone=%d"),
               *Name, *PlayerGuild, Level, ClassId, RaceId, ZoneId);
        FWowWhoEntry& Who = WhoResults.AddDefaulted_GetRef();
        Who.Name = Name; Who.Guild = PlayerGuild; Who.Level = Level; Who.Class = ClassId; Who.Race = RaceId; Who.Zone = ZoneId;
        if (bToFrame) continue; // shown in the Who window instead of chat

        // Shown in chat like the original /who output (WHO_LIST_FORMAT).
        static const TCHAR* Classes[] = {TEXT(""),TEXT("Warrior"),TEXT("Paladin"),TEXT("Hunter"),TEXT("Rogue"),
            TEXT("Priest"),TEXT("Death Knight"),TEXT("Shaman"),TEXT("Mage"),TEXT("Warlock"),TEXT(""),TEXT("Druid")};
        const FChrRacesDbcEntry* Race = FDbcStore::Get().ChrRaces().GetById(RaceId);
        const FAreaTableDbcEntry* Zone = FDbcStore::Get().AreaTable().GetById(ZoneId);
        FString Line = FString::Printf(TEXT("|Hplayer:%s|h[%s]|h: Level %u %s %s"), *Name, *Name, Level,
            Race ? *Race->Name : TEXT(""), ClassId < UE_ARRAY_COUNT(Classes) ? Classes[ClassId] : TEXT(""));
        if (!PlayerGuild.IsEmpty()) Line += FString::Printf(TEXT(" <%s>"), *PlayerGuild);
        if (Zone) Line += FString::Printf(TEXT(" - %s"), *Zone->Name);
        OnChatMessage.Broadcast(0, 0, 0, FString(), Line, FString());
    }
    if (!bToFrame)
        OnChatMessage.Broadcast(0, 0, 0, FString(), FString::Printf(TEXT("%u player%s total"), MatchCount, MatchCount == 1 ? TEXT("") : TEXT("s")), FString());
    OnWhoListUpdated.Broadcast();
}

void FWowPacketHandler::HandleRandomRoll(FPacketReader& R)
{
    // MSG_RANDOM_ROLL from the server: uint32 min, uint32 max, uint32 result, uint64 roller.
    if (!R.CanRead(20)) return;
    const uint32 Min = R.ReadU32();
    const uint32 Max = R.ReadU32();
    const uint32 Result = R.ReadU32();
    const uint64 Roller = R.ReadU64();
    const FString* Name = PlayerNameCache.Find(Roller);
    const FString Who = Name && !Name->IsEmpty() ? *Name : (Roller == EntityManager.LocalPlayerGuid ? TEXT("You") : TEXT("Someone"));
    OnChatMessage.Broadcast(0, 0, 0, FString(), FString::Printf(TEXT("%s rolls %u (%u-%u)"), *Who, Result, Min, Max), FString());
}

void FWowPacketHandler::HandleLfgQueueStatus(FPacketReader& R)
{
    FWowLfgQueueStatus& Status = LfgState.QueueStatus;
    Status.DungeonEntry = R.ReadU32();
    Status.AverageWaitSeconds = static_cast<int32>(R.ReadU32());
    Status.PlayerWaitSeconds = static_cast<int32>(R.ReadU32());
    Status.TankWaitSeconds = static_cast<int32>(R.ReadU32());
    Status.HealerWaitSeconds = static_cast<int32>(R.ReadU32());
    Status.DamageWaitSeconds = static_cast<int32>(R.ReadU32());
    Status.TanksNeeded = R.ReadU8();
    Status.HealersNeeded = R.ReadU8();
    Status.DamageNeeded = R.ReadU8();
    Status.QueuedSeconds = R.ReadU32();
    if (R.HasFailed()) return;

    LfgState.bQueued = true;
    LfgState.Mode = EWowLfgMode::Queued;
    OnLfgUpdated.Broadcast();
}

void FWowPacketHandler::HandleLfgProposalUpdate(FPacketReader& R)
{
    FWowLfgProposal Proposal;
    Proposal.DungeonEntry = R.ReadU32();
    Proposal.State = R.ReadU8();
    Proposal.ProposalId = R.ReadU32();
    Proposal.EncountersMask = R.ReadU32();
    Proposal.bSilent = R.ReadU8() != 0;
    const uint8 MemberCount = R.ReadU8();
    Proposal.Members.Reserve(MemberCount);
    for (uint8 Index = 0; Index < MemberCount && !R.HasFailed(); ++Index)
    {
        FWowLfgProposalMember Member;
        Member.Roles = R.ReadU32();
        Member.bIsPlayer = R.ReadU8() != 0;
        Member.bInDungeon = R.ReadU8() != 0;
        Member.bSameGroup = R.ReadU8() != 0;
        Member.bAnswered = R.ReadU8() != 0;
        Member.bAccepted = R.ReadU8() != 0;
        Proposal.Members.Add(Member);
    }
    if (R.HasFailed()) return;

    LfgState.Proposal = MoveTemp(Proposal);
    LfgState.bQueued = true;
    LfgState.Mode = EWowLfgMode::Proposal;
    OnLfgUpdated.Broadcast();
}

void FWowPacketHandler::HandleLfgRoleCheckUpdate(FPacketReader& R)
{
    FWowLfgRoleCheck RoleCheck;
    RoleCheck.State = R.ReadU32();
    RoleCheck.bInitializing = R.ReadU8() != 0;
    const uint8 DungeonCount = R.ReadU8();
    RoleCheck.DungeonEntries.Reserve(DungeonCount);
    for (uint8 Index = 0; Index < DungeonCount && !R.HasFailed(); ++Index)
    {
        RoleCheck.DungeonEntries.Add(R.ReadU32());
    }

    const uint8 MemberCount = R.ReadU8();
    RoleCheck.Members.Reserve(MemberCount);
    for (uint8 Index = 0; Index < MemberCount && !R.HasFailed(); ++Index)
    {
        FWowLfgRoleCheckMember Member;
        Member.Guid = R.ReadU64();
        Member.bReady = R.ReadU8() != 0;
        Member.Roles = R.ReadU32();
        Member.Level = R.ReadU8();
        RoleCheck.Members.Add(Member);
    }
    if (R.HasFailed()) return;

    LfgState.RoleCheck = MoveTemp(RoleCheck);
    LfgState.Mode = EWowLfgMode::RoleCheck;
    OnLfgUpdated.Broadcast();
}

void FWowPacketHandler::HandleLfgJoinResult(FPacketReader& R)
{
    LfgState.LastJoinResult = R.ReadU32();
    LfgState.LastJoinState = R.ReadU32();
    if (R.HasFailed()) return;

    if (LfgState.LastJoinResult != 0)
    {
        LfgState.ClearQueue();
    }
    OnLfgUpdated.Broadcast();
}

void FWowPacketHandler::HandleLfgUpdatePlayer(FPacketReader& R)
{
    const uint8 UpdateType = R.ReadU8();
    const bool bHasExtra = R.ReadU8() != 0;
    if (bHasExtra)
    {
        const bool bQueued = R.ReadU8() != 0;
        R.ReadU8();
        R.ReadU8();
        const uint8 DungeonCount = R.ReadU8();
        TArray<uint32> Dungeons;
        Dungeons.Reserve(DungeonCount);
        for (uint8 Index = 0; Index < DungeonCount && !R.HasFailed(); ++Index)
        {
            Dungeons.Add(R.ReadU32());
        }
        const FString Comment = R.ReadCString();
        if (R.HasFailed()) return;

        LfgState.SelectedDungeonEntries = MoveTemp(Dungeons);
        LfgState.Comment = Comment;
        LfgState.bQueued = bQueued;
        LfgState.Mode = bQueued ? EWowLfgMode::Queued : EWowLfgMode::RaidListing;
        LfgState.bListedForRaid = !bQueued && !LfgState.SelectedDungeonEntries.IsEmpty();
    }
    else if (UpdateType != 0)
    {
        LfgState.ClearQueue();
    }
    OnLfgUpdated.Broadcast();
}

void FWowPacketHandler::HandleLfgUpdateParty(FPacketReader& R)
{
    const uint8 UpdateType = R.ReadU8();
    const bool bHasExtra = R.ReadU8() != 0;
    if (bHasExtra)
    {
        R.ReadU8();
        const bool bQueued = R.ReadU8() != 0;
        R.ReadU8();
        R.ReadU8();
        R.ReadU8();
        R.ReadU8();
        R.ReadU8();
        const uint8 DungeonCount = R.ReadU8();
        TArray<uint32> Dungeons;
        Dungeons.Reserve(DungeonCount);
        for (uint8 Index = 0; Index < DungeonCount && !R.HasFailed(); ++Index)
        {
            Dungeons.Add(R.ReadU32());
        }
        const FString Comment = R.ReadCString();
        if (R.HasFailed()) return;

        LfgState.SelectedDungeonEntries = MoveTemp(Dungeons);
        LfgState.Comment = Comment;
        LfgState.bQueued = bQueued;
        LfgState.Mode = bQueued ? EWowLfgMode::Queued : EWowLfgMode::RaidListing;
        LfgState.bListedForRaid = !bQueued && !LfgState.SelectedDungeonEntries.IsEmpty();
    }
    else if (UpdateType != 0)
    {
        LfgState.ClearQueue();
    }
    OnLfgUpdated.Broadcast();
}

void FWowPacketHandler::HandleLfgUpdateSearch(FPacketReader& R)
{
    // This message is only a notification; the actual browser rows arrive in
    // SMSG_UPDATE_LFG_LIST.
    R.Skip(R.Remaining());
    OnLfgUpdated.Broadcast();
}

void FWowPacketHandler::HandleLfgList(FPacketReader& R)
{
    constexpr uint32 CharacterInfo = 0x01;
    constexpr uint32 Comment = 0x02;
    constexpr uint32 GroupLeader = 0x04;
    constexpr uint32 GroupGuid = 0x08;
    constexpr uint32 Roles = 0x10;
    constexpr uint32 Area = 0x20;
    constexpr uint32 Status = 0x40;
    constexpr uint32 Binded = 0x80;

    R.ReadU32(); // LFG type (raid in the 3.3.5 browser)
    LfgState.RaidSearchEntry = R.ReadU32();
    const bool bDifference = R.ReadU8() != 0;

    if (!bDifference)
    {
        LfgState.RaidResults.Reset();
    }
    else
    {
        const uint32 DeletedCount = R.ReadU32();
        if (DeletedCount > 4096)
        {
            R.Fail(TEXT("LFG browser deleted-row count is unreasonable"));
            return;
        }
        for (uint32 Index = 0; Index < DeletedCount && !R.HasFailed(); ++Index)
        {
            const uint64 DeletedGuid = R.ReadU64();
            LfgState.RaidResults.RemoveAll([DeletedGuid](const FWowLfgRaidBrowserEntry& Entry)
            {
                return Entry.PlayerGuid == DeletedGuid || Entry.GroupGuid == DeletedGuid;
            });
        }
    }

    TMap<uint64, FString> GroupComments;
    const uint32 GroupCount = R.ReadU32();
    R.ReadU32(); // reserved
    if (GroupCount > 4096)
    {
        R.Fail(TEXT("LFG browser group count is unreasonable"));
        return;
    }
    for (uint32 Index = 0; Index < GroupCount && !R.HasFailed(); ++Index)
    {
        const uint64 Guid = R.ReadU64();
        const uint32 Flags = R.ReadU32();
        FString GroupComment;
        if (Flags & Comment) GroupComment = R.ReadCString();
        if (Flags & Roles) R.Skip(3);
        if (Flags & Binded)
        {
            R.ReadU64();
            R.ReadU32();
        }
        GroupComments.Add(Guid, MoveTemp(GroupComment));
    }

    const uint32 PlayerCount = R.ReadU32();
    R.ReadU32(); // reserved
    if (PlayerCount > 4096)
    {
        R.Fail(TEXT("LFG browser player count is unreasonable"));
        return;
    }
    for (uint32 Index = 0; Index < PlayerCount && !R.HasFailed(); ++Index)
    {
        FWowLfgRaidBrowserEntry Entry;
        Entry.PlayerGuid = R.ReadU64();
        const uint32 Flags = R.ReadU32();
        if (Flags & CharacterInfo)
        {
            Entry.Level = R.ReadU8();
            Entry.ClassId = R.ReadU8();
            R.ReadU8(); // race
            R.Skip(3); // talent tree point totals
            R.Skip(6 * 4); // armor, damage/healing, and three crit ratings
            R.Skip(2 * 4); // mana regeneration values
            R.Skip(5 * 4); // attack power, agility, health, mana, online marker
            Entry.AverageItemLevel = R.ReadFloat();
            R.Skip(6 * 4); // defense, dodge, block, parry, haste, expertise
        }
        if (Flags & Comment) Entry.Comment = R.ReadCString();
        if (Flags & GroupLeader)
        {
            Entry.bGroupLeader = R.ReadU8() != 0;
        }
        if (Flags & GroupGuid) Entry.GroupGuid = R.ReadU64();
        if (Flags & Roles) Entry.Roles = R.ReadU8();
        if (Flags & Area) Entry.AreaId = R.ReadU32();
        if (Flags & Status) Entry.Status = R.ReadU8();
        if (Flags & Binded)
        {
            R.ReadU64();
            R.ReadU32();
        }
        if (R.HasFailed()) return;

        if (Entry.Comment.IsEmpty() && Entry.GroupGuid != 0)
        {
            if (const FString* GroupComment = GroupComments.Find(Entry.GroupGuid))
            {
                Entry.Comment = *GroupComment;
            }
        }
        if (const FString* Name = PlayerNameCache.Find(Entry.PlayerGuid))
        {
            Entry.PlayerName = *Name;
        }
        else
        {
            Entry.PlayerName = FString::Printf(TEXT("Player %u"), static_cast<uint32>(Entry.PlayerGuid));
            TArray<uint8> Query;
            Query.SetNumUninitialized(8);
            FMemory::Memcpy(Query.GetData(), &Entry.PlayerGuid, 8);
            OnSendPacket.ExecuteIfBound(WowOpcode::CMSG_NAME_QUERY, Query);
        }

        const int32 ExistingIndex = LfgState.RaidResults.IndexOfByPredicate([&Entry](const FWowLfgRaidBrowserEntry& Existing)
        {
            return Existing.PlayerGuid == Entry.PlayerGuid;
        });
        if (ExistingIndex == INDEX_NONE)
        {
            LfgState.RaidResults.Add(MoveTemp(Entry));
        }
        else
        {
            LfgState.RaidResults[ExistingIndex] = MoveTemp(Entry);
        }
    }

    if (R.HasFailed()) return;
    LfgState.Mode = EWowLfgMode::RaidSearch;
    OnLfgUpdated.Broadcast();
}

void FWowPacketHandler::HandleLfgDisabled(FPacketReader& R)
{
    R.Skip(R.Remaining());
    LfgState.ClearQueue();
    LfgState.bListedForRaid = false;
    LfgState.Mode = EWowLfgMode::Disabled;
    OnLfgUpdated.Broadcast();
}

// ── SMSG_QUEST_QUERY_RESPONSE ────────────────────────────────────────────
// Static quest data requested with CMSG_QUEST_QUERY. The layout below is the
// build 12340 layout used by AzerothCore and the original 3.3.5a client.

void FWowPacketHandler::HandleQuestQueryResponse(FPacketReader& R)
{
    if (!R.CanRead(4))
    {
        return;
    }

    const uint32 RawQuestId = R.ReadU32();
    const uint32 QuestId = RawQuestId & 0x7FFFFFFFu;
    PendingQuestTemplateQueries.Remove(QuestId);

    // A high-bit response means the server has no template for this quest ID.
    if ((RawQuestId & 0x80000000u) != 0 || !R.CanRead(24))
    {
        return;
    }

    R.ReadU32(); // quest method
    FWowQuestLogEntry Template;
    Template.QuestId = QuestId;
    Template.Level = static_cast<int32>(R.ReadU32());
    R.ReadU32(); // minimum level
    Template.ZoneOrSort = static_cast<int32>(R.ReadU32());
    R.ReadU32(); // quest type
    R.ReadU32(); // suggested players

    // Reputation, rewards, POI, and other fixed fields precede the strings:
    // 58 x 4-byte fields. [6] money, [8] reward spell, [19..26] reward items
    // (id, count) x4, [27..38] choice items (id, count) x6.
    constexpr int32 FixedFieldsBeforeStringsBytes = 232;
    if (!R.CanRead(FixedFieldsBeforeStringsBytes))
    {
        return;
    }
    uint32 Fixed[FixedFieldsBeforeStringsBytes / 4];
    for (uint32& Value : Fixed) Value = R.ReadU32();
    Template.RewardMoney = static_cast<int32>(Fixed[6]);
    Template.RewardSpell = Fixed[8] ? Fixed[8] : Fixed[9]; // displayed spell, else the cast spell
    // r320: [12] SrcItemId - the item the quest hands out (Foreman's Blackjack for Lazy Peons).
    Template.SourceItemId = Fixed[12];
    if (Template.SourceItemId) RequestItemTemplate(Template.SourceItemId);
    for (int32 I = 0; I < 4; ++I)
        if (Fixed[19 + I * 2]) Template.RewardItems.Emplace(Fixed[19 + I * 2], FMath::Max(1u, Fixed[20 + I * 2]));
    for (int32 I = 0; I < 6; ++I)
        if (Fixed[27 + I * 2]) Template.RewardChoiceItems.Emplace(Fixed[27 + I * 2], FMath::Max(1u, Fixed[28 + I * 2]));
    for (const auto& Reward : Template.RewardItems) RequestItemTemplate(Reward.Key);
    for (const auto& Reward : Template.RewardChoiceItems) RequestItemTemplate(Reward.Key);

    Template.Title = R.ReadCString();
    Template.ObjectiveText = R.ReadCString();
    Template.Details = R.ReadCString();
    R.ReadCString(); // area description
    Template.CompletionText = R.ReadCString(); // r339: completed text (WatchFrame's line for a finished quest)
    if (R.HasFailed())
    {
        return;
    }

    Template.ZoneName = ResolveQuestCategoryName(Template.ZoneOrSort);
    Template.ObjectiveCounts.SetNumZeroed(4);

    TArray<FWowQuestObjective> CreatureObjectives;
    CreatureObjectives.SetNum(4);
    for (int32 ObjectiveIndex = 0; ObjectiveIndex < 4; ++ObjectiveIndex)
    {
        FWowQuestObjective& Objective = CreatureObjectives[ObjectiveIndex];
        Objective.CreatureOrGOId = R.ReadU32();
        Objective.Required = R.ReadU32();
        R.ReadU32(); // item dropped by this creature/gameobject
        R.ReadU32(); // required source count
    }

    TArray<FWowQuestObjective> ItemObjectives;
    ItemObjectives.SetNum(6);
    for (int32 ObjectiveIndex = 0; ObjectiveIndex < 6; ++ObjectiveIndex)
    {
        FWowQuestObjective& Objective = ItemObjectives[ObjectiveIndex];
        Objective.ItemId = R.ReadU32();
        Objective.Required = R.ReadU32();
    }

    for (int32 ObjectiveIndex = 0; ObjectiveIndex < 4; ++ObjectiveIndex)
    {
        CreatureObjectives[ObjectiveIndex].Text = R.ReadCString();
    }
    if (R.HasFailed())
    {
        return;
    }

    for (FWowQuestObjective& Objective : CreatureObjectives)
    {
        if (Objective.CreatureOrGOId != 0 || Objective.Required != 0 || !Objective.Text.IsEmpty())
        {
            if (Objective.Text.IsEmpty() && Objective.CreatureOrGOId && !(Objective.CreatureOrGOId&0x80000000u))
            {
                if (const auto* Name=CreatureNameCache.Find(Objective.CreatureOrGOId)) Objective.Text=*Name;
                else if (OnSendPacket.IsBound())
                {
                    TArray<uint8> Query; Query.SetNumZeroed(12);
                    FMemory::Memcpy(Query.GetData(),&Objective.CreatureOrGOId,4);
                    OnSendPacket.Execute(WowOpcode::CMSG_CREATURE_QUERY,Query);
                }
            }
            Template.Objectives.Add(MoveTemp(Objective));
        }
    }
    for (FWowQuestObjective& Objective : ItemObjectives)
    {
        if (Objective.ItemId != 0 && Objective.Required != 0)
        {
            if(const auto* Item=ItemTemplates.Find(Objective.ItemId)) Objective.Text=Item->Name;
            else RequestItemTemplate(Objective.ItemId);
            Template.Objectives.Add(MoveTemp(Objective));
        }
    }

    QuestTitleCache.Add(QuestId, Template.Title);
    QuestObjectiveTextCache.Add(QuestId, Template.ObjectiveText);
    QuestTemplateCache.Add(QuestId, Template);

    for (FWowQuestLogEntry& Entry : QuestLog)
    {
        if (Entry.QuestId != QuestId)
        {
            continue;
        }

        const uint32 CurrentState = Entry.State;
        const bool bWasTracked = Entry.bIsTracked;
        const TArray<uint32> CurrentCounts = Entry.ObjectiveCounts;
        Entry = Template;
        Entry.State = CurrentState;
        Entry.bIsTracked = bWasTracked;
        Entry.ObjectiveCounts = CurrentCounts;

        int32 CreatureObjectiveIndex = 0;
        for (FWowQuestObjective& Objective : Entry.Objectives)
        {
            if (Objective.CreatureOrGOId != 0 && CreatureObjectiveIndex < CurrentCounts.Num())
            {
                Objective.Count = CurrentCounts[CreatureObjectiveIndex++];
            }
        }
        break;
    }

    RefreshQuestItemCounts(false);
    UE_LOG(LogWowPacket, Verbose, TEXT("QUEST_QUERY_RESPONSE: quest=%u level=%d category='%s'"),
        QuestId, Template.Level, *Template.ZoneName);
    OnQuestLogUpdated.Broadcast();
}

// ── SMSG_QUESTGIVER_STATUS ───────────────────────────────────────────────
// uint64 guid, uint8 status

void FWowPacketHandler::HandleQuestgiverStatus(FPacketReader& R)
{
    if (!R.CanRead(9)) return;

    uint64 Guid = R.ReadU64();
    uint8 Status = R.ReadU8();

    const TCHAR* StatusMsg = TEXT("Unknown");
    switch (Status)
    {
    case 0: StatusMsg = TEXT("None"); break;
    case 1: StatusMsg = TEXT("Unavailable"); break;
    case 2: StatusMsg = TEXT("Low-level available"); break;
    case 3: StatusMsg = TEXT("Low-level reward rep"); break;
    case 4: StatusMsg = TEXT("Low-level available rep"); break;
    case 5: StatusMsg = TEXT("Incomplete"); break;
    case 6: StatusMsg = TEXT("Reward rep"); break;
    case 7: StatusMsg = TEXT("Available rep"); break;
    case 8: StatusMsg = TEXT("Available"); break;
    case 9: StatusMsg = TEXT("Reward2"); break;
    case 10: StatusMsg = TEXT("Reward"); break;
    }

    QuestGiverStatuses.Add(Guid, Status);
    UE_LOG(LogWowPacket, Log, TEXT("QUESTGIVER_STATUS: guid=%llu status=%d (%s)"), Guid, Status, StatusMsg);
    OnQuestGiverStatus.Broadcast(Guid, Status);
}

void FWowPacketHandler::HandleQuestgiverStatusMultiple(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    const uint32 Count = FMath::Min<uint32>(R.ReadU32(), 4096);
    for (uint32 Index = 0; Index < Count && R.CanRead(9); ++Index)
    {
        const uint64 Guid = R.ReadU64();
        const uint8 Status = R.ReadU8();
        QuestGiverStatuses.Add(Guid, Status);
        OnQuestGiverStatus.Broadcast(Guid, Status);
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUESTGIVER_STATUS_MULTIPLE: cached %u quest giver markers"), Count);
}

void FWowPacketHandler::HandleQuestgiverQuestList(FPacketReader& R)
{
    if (!R.CanRead(9)) return;

    FWowGossipMenu Menu;
    Menu.NpcGuid = R.ReadU64();
    if (const FWowEntity* Npc = EntityManager.Find(Menu.NpcGuid))
    {
        Menu.NpcName = CreatureNameCache.FindRef(Npc->Entry);
    }
    Menu.Greeting = R.ReadCString();
    if (!R.CanRead(9)) return;
    R.ReadU32(); // emote delay
    R.ReadU32(); // emote id
    const uint32 QuestCount = FMath::Min<uint32>(R.ReadU8(), 64);

    for (uint32 Index = 0; Index < QuestCount; ++Index)
    {
        if (!R.CanRead(17)) break;
        FWowQuestMenuItem& Quest = Menu.Quests.AddDefaulted_GetRef();
        Quest.QuestId = R.ReadU32();
        Quest.Icon = R.ReadU32();
        Quest.Level = static_cast<int32>(R.ReadU32());
        Quest.Flags = R.ReadU32();
        Quest.bRepeatable = R.ReadU8() != 0;
        Quest.Title = R.ReadCString();
        QuestTitleCache.Add(Quest.QuestId, Quest.Title);
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUESTGIVER_QUEST_LIST: guid=%llu quests=%d greeting=\"%s\""),
        Menu.NpcGuid, Menu.Quests.Num(), *Menu.Greeting);
    OnGossipMessage.Broadcast(Menu);
}

// ── SMSG_QUESTGIVER_QUEST_DETAILS ────────────────────────────────────────
// uint64 guid, uint32 questId, title (null-term string), details string

void FWowPacketHandler::HandleQuestgiverQuestDetails(FPacketReader& R)
{
    if (!R.CanRead(20)) return;

    const uint64 Guid = R.ReadU64();
    R.ReadU64(); // divider guid
    const uint32 QuestId = R.ReadU32();

    FString Title = R.ReadCString();
    FString Details = R.ReadCString();
    FString Objectives = R.ReadCString();

    FWowQuestDetails QuestDetails;
    QuestDetails.QuestGiverGuid = Guid;
    if (const FWowEntity* Npc = EntityManager.Find(Guid))
    {
        QuestDetails.QuestGiverName = CreatureNameCache.FindRef(Npc->Entry);
    }
    QuestDetails.QuestId = QuestId;
    QuestDetails.Title = Title;
    QuestTitleCache.Add(QuestId, Title);
    QuestDetails.Details = Details;
    QuestDetails.Objectives = Objectives;
    QuestObjectiveTextCache.Add(QuestId, Objectives);

    if (!R.CanRead(10)) return;
    R.ReadU8(); // activate accept
    R.ReadU32(); // quest flags
    R.ReadU32(); // suggested players
    R.ReadU8(); // auto-finished

    if (!R.CanRead(4)) return;
    const uint32 ChoiceCount = FMath::Min<uint32>(R.ReadU32(), 6);
    for (uint32 Index = 0; Index < ChoiceCount && R.CanRead(12); ++Index)
    {
        FWowQuestRewardItem& Item = QuestDetails.ChoiceRewards.AddDefaulted_GetRef();
        Item.ItemId = R.ReadU32();
        Item.Count = R.ReadU32();
        Item.DisplayId = R.ReadU32();
    }

    if (!R.CanRead(4)) return;
    const uint32 RewardCount = FMath::Min<uint32>(R.ReadU32(), 4);
    for (uint32 Index = 0; Index < RewardCount && R.CanRead(12); ++Index)
    {
        FWowQuestRewardItem& Item = QuestDetails.RewardItems.AddDefaulted_GetRef();
        Item.ItemId = R.ReadU32();
        Item.Count = R.ReadU32();
        Item.DisplayId = R.ReadU32();
    }

    if (R.CanRead(8))
    {
        QuestDetails.RewardMoney = R.ReadU32();
        QuestDetails.RewardXP = R.ReadU32();
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUESTGIVER_QUEST_DETAILS: guid=%llu quest=%u title=\"%s\""),
           Guid, QuestId, *Title);

    OnQuestDialog.Broadcast(QuestDetails);
}

void FWowPacketHandler::HandleQuestgiverRequestItems(FPacketReader& R)
{
    if (!R.CanRead(12)) return;

    FWowQuestDetails QuestDetails;
    QuestDetails.QuestGiverGuid = R.ReadU64();
    if (const FWowEntity* Npc = EntityManager.Find(QuestDetails.QuestGiverGuid))
    {
        QuestDetails.QuestGiverName = CreatureNameCache.FindRef(Npc->Entry);
    }
    QuestDetails.QuestId = R.ReadU32();
    QuestDetails.Title = R.ReadCString();
    QuestDetails.Details = R.ReadCString();
    QuestTitleCache.Add(QuestDetails.QuestId, QuestDetails.Title);

    if (!R.CanRead(28)) return;
    R.ReadU32(); // reserved
    R.ReadU32(); // completion emote
    R.ReadU32(); // close-on-cancel
    R.ReadU32(); // quest flags
    R.ReadU32(); // suggested players
    QuestDetails.RequiredMoney = R.ReadU32();

    const uint32 RequiredItemCount = FMath::Min<uint32>(R.ReadU32(), 6);
    for (uint32 Index = 0; Index < RequiredItemCount && R.CanRead(12); ++Index)
    {
        FWowQuestRewardItem& Item = QuestDetails.RequiredItems.AddDefaulted_GetRef();
        Item.ItemId = R.ReadU32();
        Item.Count = R.ReadU32();
        Item.DisplayId = R.ReadU32();
    }

    if (R.CanRead(4))
    {
        QuestDetails.bCanComplete = R.ReadU32() != 0;
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUESTGIVER_REQUEST_ITEMS: guid=%llu quest=%u canComplete=%d items=%d"),
        QuestDetails.QuestGiverGuid, QuestDetails.QuestId, QuestDetails.bCanComplete,
        QuestDetails.RequiredItems.Num());
    OnQuestProgressDialog.Broadcast(QuestDetails);
}

// ── SMSG_QUESTGIVER_OFFER_REWARD ─────────────────────────────────────────
// uint64 guid, uint32 questId, title string

void FWowPacketHandler::HandleQuestgiverOfferReward(FPacketReader& R)
{
    if (!R.CanRead(12)) return;

    uint64 Guid = R.ReadU64();
    uint32 QuestId = R.ReadU32();

    FString Title = R.ReadCString();
    FString RewardText = R.ReadCString();

    FWowQuestDetails QuestDetails;
    QuestDetails.QuestGiverGuid = Guid;
    if (const FWowEntity* Npc = EntityManager.Find(Guid))
    {
        QuestDetails.QuestGiverName = CreatureNameCache.FindRef(Npc->Entry);
    }
    QuestDetails.QuestId = QuestId;
    QuestDetails.Title = Title;
    QuestDetails.Details = RewardText;
    QuestDetails.bCanComplete = true;

    if (!R.CanRead(13)) return;
    R.ReadU8(); // enable next quest
    R.ReadU32(); // quest flags
    R.ReadU32(); // suggested players
    const uint32 EmoteCount = FMath::Min<uint32>(R.ReadU32(), 4);
    if (!R.CanRead(static_cast<int32>(EmoteCount * 8))) return;
    R.Skip(static_cast<int32>(EmoteCount * 8));

    if (!R.CanRead(4)) return;
    const uint32 ChoiceCount = FMath::Min<uint32>(R.ReadU32(), 6);
    for (uint32 Index = 0; Index < ChoiceCount && R.CanRead(12); ++Index)
    {
        FWowQuestRewardItem& Item = QuestDetails.ChoiceRewards.AddDefaulted_GetRef();
        Item.ItemId = R.ReadU32();
        Item.Count = R.ReadU32();
        Item.DisplayId = R.ReadU32();
    }

    if (!R.CanRead(4)) return;
    const uint32 RewardCount = FMath::Min<uint32>(R.ReadU32(), 4);
    for (uint32 Index = 0; Index < RewardCount && R.CanRead(12); ++Index)
    {
        FWowQuestRewardItem& Item = QuestDetails.RewardItems.AddDefaulted_GetRef();
        Item.ItemId = R.ReadU32();
        Item.Count = R.ReadU32();
        Item.DisplayId = R.ReadU32();
    }

    if (R.CanRead(8))
    {
        QuestDetails.RewardMoney = R.ReadU32();
        QuestDetails.RewardXP = R.ReadU32();
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUESTGIVER_OFFER_REWARD: guid=%llu quest=%u title=\"%s\""),
           Guid, QuestId, *Title);

    OnQuestRewardDialog.Broadcast(QuestDetails);
}

void FWowPacketHandler::HandleQuestgiverQuestComplete(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    const uint32 QuestId = R.ReadU32();
    QuestLog.RemoveAll([QuestId](const FWowQuestLogEntry& Entry)
    {
        return Entry.QuestId == QuestId;
    });
    UE_LOG(LogWowPacket, Log, TEXT("QUESTGIVER_QUEST_COMPLETE: quest=%u"), QuestId);
    OnQuestComplete.Broadcast(QuestId);
}

// ── SMSG_QUEST_UPDATE_ADD_KILL ───────────────────────────────────────────
// uint32 questId, uint32 entry, uint32 count, uint32 required, uint64 guid

void FWowPacketHandler::HandleQuestUpdateAddKill(FPacketReader& R)
{
    if (!R.CanRead(24)) return;

    uint32 QuestId = R.ReadU32();
    uint32 Entry = R.ReadU32();
    uint32 Count = R.ReadU32();
    uint32 Required = R.ReadU32();
    uint64 Guid = R.ReadU64();

    FString ObjectiveLabel=TEXT("Quest objective");
    // Update quest log
    for (FWowQuestLogEntry& Quest : QuestLog)
    {
        if (Quest.QuestId == QuestId)
        {
            if(!Quest.Title.IsEmpty())ObjectiveLabel=Quest.Title;
            bool bFoundObjective = false;
            for (FWowQuestObjective& Obj : Quest.Objectives)
            {
                if (Obj.CreatureOrGOId == Entry)
                {
                    if(!Obj.Text.IsEmpty())ObjectiveLabel=Obj.Text;
                    Obj.Count = Count;
                    Obj.Required = Required;
                    bFoundObjective = true;
                    break;
                }
            }

            if (!bFoundObjective)
            {
                FWowQuestObjective NewObj;
                NewObj.CreatureOrGOId = Entry;
                NewObj.Count = Count;
                NewObj.Required = Required;
                Quest.Objectives.Add(NewObj);
            }
            break;
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUEST_UPDATE_ADD_KILL: quest=%u entry=%u count=%u/%u"),
           QuestId, Entry, Count, Required);
    if(Count>0 && Required>0)OnUiInfoMessage.Broadcast(FString::Printf(TEXT("%s: %u/%u"),*ObjectiveLabel,Count,Required));
    OnQuestLogUpdated.Broadcast();
}

// ── SMSG_QUEST_UPDATE_COMPLETE ───────────────────────────────────────────
// uint32 questId

void FWowPacketHandler::HandleQuestUpdateComplete(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 QuestId = R.ReadU32();

    // Update quest log
    for (FWowQuestLogEntry& Quest : QuestLog)
    {
        if (Quest.QuestId == QuestId)
        {
            Quest.State = 1; // Complete
            OnUiInfoMessage.Broadcast((Quest.Title.IsEmpty()?TEXT("Quest objectives"):Quest.Title)+TEXT(" — completed"));
            break;
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUEST_UPDATE_COMPLETE: quest=%u"), QuestId);
    OnQuestComplete.Broadcast(QuestId);
}

// ── SMSG_TALENTS_INFO ────────────────────────────────────────────────────
// uint8 talentGroupCount, uint8 activeTalentGroup, then for each group:
// uint32 pointsSpent[3], uint8 talentCount, then for each talent: uint32 talentId + uint8 rank

void FWowPacketHandler::HandleTalentsInfo(FPacketReader& R)
{
    // 3.3.5: uint8 isPet, uint32 unspent points, then
    //   player: uint8 specCount, uint8 activeSpec, per spec { uint8 n, n x (uint32 talentId,
    //           uint8 rankIndex), uint8 glyphSlots, glyphSlots x uint16 glyph }
    //   pet:    uint8 n, n x (uint32 talentId, uint8 rankIndex)
    // rankIndex is 0-based; FWowTalentInfo::Rank stores the learned rank count.
    if (!R.CanRead(5)) return;
    const uint8 bPet = R.ReadU8();
    const uint32 Unspent = R.ReadU32();
    auto ReadTalents = [&R](TArray<FWowTalentInfo>& Out)
    {
        if (!R.CanRead(1)) return;
        const uint8 Count = R.ReadU8();
        for (int32 I = 0; I < Count && R.CanRead(5); ++I)
        {
            FWowTalentInfo Talent;
            Talent.TalentId = R.ReadU32();
            Talent.Rank = static_cast<uint8>(R.ReadU8() + 1);
            Out.Add(Talent);
        }
    };
    if (bPet)
    {
        PetTalents.Reset();
        ReadTalents(PetTalents);
        PetUnspentTalentPoints = Unspent;
        ++TalentRevision;
        UE_LOG(LogWowPacket, Log, TEXT("TALENTS_INFO (pet): %d talents, %u unspent"), PetTalents.Num(), Unspent);
        OnTalentsUpdated.Broadcast();
        return;
    }
    if (!R.CanRead(2)) return;
    const uint8 GroupCount = R.ReadU8();
    const uint8 Active = R.ReadU8();
    TArray<TArray<FWowTalentInfo>> Groups;
    TArray<TArray<uint16>> Glyphs;
    for (int32 Group = 0; Group < GroupCount; ++Group)
    {
        ReadTalents(Groups.AddDefaulted_GetRef());
        TArray<uint16>& GroupGlyphs = Glyphs.AddDefaulted_GetRef();
        if (!R.CanRead(1)) break;
        const uint8 GlyphCount = R.ReadU8();
        for (int32 I = 0; I < GlyphCount && R.CanRead(2); ++I) GroupGlyphs.Add(R.ReadU16());
    }
    TalentGroups = MoveTemp(Groups);
    TalentGlyphs = MoveTemp(Glyphs);
    ActiveTalentGroup = Active;
    UnspentTalentPoints = Unspent;
    bTalentInfoReceived = true;
    Talents = TalentGroups.IsValidIndex(Active) ? TalentGroups[Active] : TArray<FWowTalentInfo>();
    ++TalentRevision;

    UE_LOG(LogWowPacket, Log, TEXT("TALENTS_INFO: %d specs, active=%d, %d talents, %u unspent"),
           GroupCount, Active, Talents.Num(), Unspent);
    OnTalentsUpdated.Broadcast();
}

// ── SMSG_LEARNED_SPELL ───────────────────────────────────────────────────
// uint32 spellId, uint16 unknown

void FWowPacketHandler::HandleLearnedSpell(FPacketReader& R)
{
    if (!R.CanRead(6)) return;

    uint32 SpellId = R.ReadU32();
    uint16 Unknown = R.ReadU16();

    if (SpellId == 0 || KnownSpells.Contains(SpellId)) return;
    KnownSpells.Add(SpellId);
    OnKnownSpellsChanged.Broadcast(true);

    UE_LOG(LogWowPacket, Log, TEXT("LEARNED_SPELL: spell=%u"), SpellId);
}

// ── SMSG_REMOVED_SPELL ───────────────────────────────────────────────────
// uint32 spellId

void FWowPacketHandler::HandleRemovedSpell(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 SpellId = R.ReadU32();

    if (KnownSpells.Remove(SpellId) == 0) return;
    OnKnownSpellsChanged.Broadcast(false);

    UE_LOG(LogWowPacket, Log, TEXT("REMOVED_SPELL: spell=%u"), SpellId);
}

// ── Warden Anti-Cheat ────────────────────────────────────────────────────────

void FWowPacketHandler::InitializeWarden(const TArray<uint8>& SessionKey)
{
    WardenHandler.InitializeEncryption(SessionKey);
}

void FWowPacketHandler::HandleWardenData(FPacketReader& R)
{
    TArray<uint8> Data;
    Data.SetNumUninitialized(R.Remaining());
    if (R.Remaining() > 0)
    {
        FMemory::Memcpy(Data.GetData(), R.Data + R.Pos, R.Remaining());
    }

    WardenHandler.HandleWardenData(Data);
}

void FWowPacketHandler::SendWardenResponse(uint32 Opcode, const TArray<uint8>& Data)
{
    if (OnSendPacket.IsBound())
    {
        OnSendPacket.Execute(Opcode, Data);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Death / Corpse / Resurrection Handlers
// ──────────────────────────────────────────────────────────────────────────

// ── SMSG_CORPSE_RECLAIM_DELAY ────────────────────────────────────────────
// uint32 delayTimeInSeconds

void FWowPacketHandler::HandleCorpseReclaimDelay(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 DelayTime = R.ReadU32();
    float DelayInSeconds = static_cast<float>(DelayTime) / 1000.0f;
    DeathState.ReclaimReadyTime = FPlatformTime::Seconds() + DelayInSeconds;

    UE_LOG(LogWowPacket, Log, TEXT("CORPSE_RECLAIM_DELAY: delay=%u milliseconds"), DelayTime);

    OnCorpseReclaimDelay.Broadcast(DelayInSeconds);
}

// ── SMSG_RESURRECT_REQUEST ───────────────────────────────────────────────
// uint64 resurrecter, uint32 name length (with terminator), cstring name, uint8 0,
// uint8 sickness (1 when the resurrecter is not a player), [uint32 0: ignores the corpse delay]

void FWowPacketHandler::HandleResurrectRequest(FPacketReader& R)
{
    if (!R.CanRead(12)) return;

    const uint64 ResurrectorGuid = R.ReadU64();
    R.ReadU32(); // name length
    const FString ResurrectorName = R.ReadCString();
    if (R.CanRead(1)) R.ReadU8();
    const bool bSickness = R.CanRead(1) && R.ReadU8() != 0;
    const bool bNoTimer = R.CanRead(4);

    DeathState.ResurrecterGuid = ResurrectorGuid;
    DeathState.ResurrecterName = ResurrectorName;
    DeathState.bResurrectSickness = bSickness;
    DeathState.bResurrectTimer = !bNoTimer;

    UE_LOG(LogWowPacket, Log, TEXT("RESURRECT_REQUEST: resurrector=%llu name=%s sickness=%d timer=%d"),
        ResurrectorGuid, *ResurrectorName, bSickness ? 1 : 0, bNoTimer ? 0 : 1);

    OnResurrectRequest.Broadcast(ResurrectorName);
}

// ── SMSG_SPIRIT_HEALER_CONFIRM ───────────────────────────────────────────
// uint64 spirit healer: "Return me to life" was chosen; the client asks CONFIRM_XP_LOSS.

void FWowPacketHandler::HandleSpiritHealerConfirm(FPacketReader& R)
{
    if (!R.CanRead(8)) return;
    DeathState.SpiritHealerGuid = R.ReadU64();
    UE_LOG(LogWowPacket, Log, TEXT("SPIRIT_HEALER_CONFIRM: healer=%llu"), DeathState.SpiritHealerGuid);
    OnSpiritHealerConfirm.Broadcast(DeathState.SpiritHealerGuid);
}

// ── MSG_CORPSE_QUERY ─────────────────────────────────────────────────────
// uint8 found, [uint32 map, float x, y, z, uint32 corpse map, uint32 unk]

void FWowPacketHandler::HandleCorpseQuery(FPacketReader& R)
{
    if (!R.CanRead(1)) return;
    if (R.ReadU8() == 0 || !R.CanRead(16))
    {
        UE_LOG(LogWowPacket, Log, TEXT("CORPSE_QUERY: no corpse"));
        return;
    }
    const uint32 Map = R.ReadU32();
    const float X = R.ReadFloat();
    const float Y = R.ReadFloat();
    const float Z = R.ReadFloat();
    DeathState.bHasCorpse = true;
    DeathState.CorpseMap = Map;
    DeathState.CorpsePos = FVector(X, Y, Z);
    UE_LOG(LogWowPacket, Log, TEXT("CORPSE_QUERY: map=%u (%.1f, %.1f, %.1f)"), Map, X, Y, Z);
}

// ── SMSG_AREA_SPIRIT_HEALER_TIME ─────────────────────────────────────────
// uint64 spirit guide, uint32 milliseconds until the next wave

void FWowPacketHandler::HandleAreaSpiritHealerTime(FPacketReader& R)
{
    if (!R.CanRead(12)) return;
    DeathState.AreaSpiritHealerGuid = R.ReadU64();
    DeathState.AreaSpiritHealerReadyTime = FPlatformTime::Seconds() + R.ReadU32() / 1000.0;
}

// ── Teleport Handling ────────────────────────────────────────────────────────

void FWowPacketHandler::HandleMoveTeleport(FPacketReader& R)
{
    // Broadcast to observers: packed GUID + MovementInfo, with no order counter.
    HandleMovement(R);
}

void FWowPacketHandler::HandleMoveTeleportAck(FPacketReader& R)
{
    // Directed to the local mover: packed GUID + order counter + MovementInfo.
    const uint64 Guid = R.ReadPackedGuid();
    const uint32 Counter = R.ReadU32();
    FWowEntity* Entity = EntityManager.Find(Guid);
    FWowMovementInfo Info = Entity ? Entity->Movement : FWowMovementInfo();
    uint32 Timestamp = 0;
    if (!ReadMovementSnapshot(R, Info, Timestamp)) return;
    if (Guid != EntityManager.LocalPlayerGuid) return;
    if (Entity)
    {
        Info.bHasActiveSpline = false;
        Info.SplineWaypoints.Reset();
        Entity->Movement = Info;
    }
    OnTeleportRequest.Broadcast(Guid, Counter, Timestamp, Info.Position, Info.Orientation);
}

// r600: transport entry of a map change on board (SMSG_TRANSFER_PENDING), taken by the transport code
// (WowGameplayControllerTransports.cpp) when SMSG_NEW_WORLD arrives: its position is then the deck offset.
static uint32 GWowPendingTransportTransfer = 0;
WOWNETWORK_API uint32 WowTakePendingTransportTransfer()
{
    const uint32 Entry = GWowPendingTransportTransfer;
    GWowPendingTransportTransfer = 0;
    return Entry;
}

void FWowPacketHandler::HandleTransferPending(FPacketReader& R)
{
    // SMSG_TRANSFER_PENDING (3.3.5): uint32 mapId; on a transport also uint32 transport entry, uint32 old map.

    uint32 MapId = R.ReadU32();
    GWowPendingTransportTransfer = 0;
    if (R.CanRead(8))
    {
        GWowPendingTransportTransfer = R.ReadU32();
        const uint32 OldMap = R.ReadU32();
        UE_LOG(LogWowPacket, Log, TEXT("TRANSFER_PENDING: mapId=%u on transport entry %u (from map %u)"), MapId, GWowPendingTransportTransfer, OldMap);
        return;
    }

    UE_LOG(LogWowPacket, Log, TEXT("TRANSFER_PENDING: mapId=%u"), MapId);

    // This indicates a map transfer is about to happen
    // The client should expect SMSG_NEW_WORLD next
}

void FWowPacketHandler::HandleNewWorld(FPacketReader& R)
{
    // SMSG_NEW_WORLD
    // uint32 mapId + float x + float y + float z + float orientation

    if (!R.CanRead(20)) // 4 + 4*4
    {
        UE_LOG(LogWowPacket, Error, TEXT("NEW_WORLD packet too short"));
        return;
    }

    uint32 MapId = R.ReadU32();
    float X = R.ReadFloat();
    float Y = R.ReadFloat();
    float Z = R.ReadFloat();
    float Orientation = R.ReadFloat();

    UE_LOG(LogWowPacket, Log, TEXT("NEW_WORLD: map=%u pos=(%.1f,%.1f,%.1f) orient=%.2f"),
           MapId, X, Y, Z, Orientation);

    // Broadcast map transfer
    DeathState.CurrentMap = MapId;
    OnMapTransfer.Broadcast(MapId, X, Y, Z, Orientation);
}

// ── SMSG_NAME_QUERY_RESPONSE ─────────────────────────────────────────────────
// packedGUID, uint8 found, if found: string name, string realmName, uint8 race, uint8 gender, uint8 class

void FWowPacketHandler::HandleNameQueryResponse(FPacketReader& R)
{
    uint64 Guid = R.ReadPackedGuid();
    const uint8 Result = R.ReadU8();

    // 3.3.5 sends zero for a successful name lookup and a non-zero error code
    // when the character is not found. This was previously interpreted in
    // reverse, leaving friends, chat senders, targets and combat actors unnamed.
    if (Result == 0)
    {
        FString Name = R.ReadCString();
        FString RealmName = R.ReadCString();
        uint8 Race = R.ReadU8();
        uint8 Gender = R.ReadU8();
        uint8 Class = R.ReadU8();

        PlayerNameCache.Add(Guid, Name);
        PlayerClassCache.Add(Guid, Class);
        if (Gender < 2) PlayerGenderCache.Add(Guid, Gender);
        bool bUpdatedLfgName = false;
        for (FWowLfgRaidBrowserEntry& Entry : LfgState.RaidResults)
        {
            if (Entry.PlayerGuid == Guid)
            {
                Entry.PlayerName = Name;
                bUpdatedLfgName = true;
            }
        }

        UE_LOG(LogWowPacket, Log, TEXT("NAME_QUERY_RESPONSE: GUID=%llu, Name=%s, Realm=%s"),
            Guid, *Name, *RealmName);

        OnPlayerNameReceived.Broadcast(Guid, Name);
        if (bUpdatedLfgName)
        {
            OnLfgUpdated.Broadcast();
        }
    }
    else
    {
        UE_LOG(LogWowPacket, Warning, TEXT("NAME_QUERY_RESPONSE: GUID=%llu failed result=%u"), Guid, Result);
    }
}

// ── SMSG_CREATURE_QUERY_RESPONSE ─────────────────────────────────────────────────
// uint32 entry, string name, string subName, string iconName, uint32 typeFlags,
// uint32 creatureType, uint32 creatureFamily, uint32 rank, uint32 killCredit[2],
// uint32 displayId[4], float hpMod, float manaMod, uint8 racialLeader

void FWowPacketHandler::HandleCreatureQueryResponse(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    // Build 12340 (QueryHandler::HandleCreatureQueryOpcode): entry (high bit = unknown),
    // name, three alternate names, subname/title, cursor icon, type flags, type,
    // family, rank, kill credit x2, display ids x4, hp/mana modifiers,
    // racial leader, quest items x6, movement id.
    uint32 Entry = R.ReadU32();
    if (Entry & 0x80000000u) return; // creature unknown to the server
    FString Name = R.ReadCString();
    R.ReadCString(); R.ReadCString(); R.ReadCString(); // alternate names
    FString Title = R.ReadCString(); // SubName, e.g. "Innkeeper"
    if (R.HasFailed()) return;
    if (R.CanRead(1))
    {
        FWowCreatureTemplate Template;
        Template.IconName = R.ReadCString();
        Template.TypeFlags = R.ReadU32();
        Template.CreatureType = R.ReadU32();
        Template.Family = R.ReadU32();
        Template.Rank = R.ReadU32();
        if (R.CanRead(4 * 2 + 4 * 4 + 4 * 2 + 1 + 4 * 6))
        {
            R.Skip(4 * 2); // kill credit
            for (uint32& Display : Template.DisplayIds) Display = R.ReadU32();
            R.Skip(4 * 2 + 1); // hp/mana modifiers, racial leader
            for (uint32& Item : Template.QuestItems) Item = R.ReadU32();
        }
        // A short tail (custom servers) still keeps the names parsed above.
        if (!R.HasFailed()) CreatureTemplateCache.Add(Entry, Template);
    }

    if (!Name.IsEmpty())
    {
        CreatureNameCache.Add(Entry, Name);
        CreatureTitleCache.Add(Entry, Title);

        UE_LOG(LogWowPacket, Log, TEXT("CREATURE_QUERY_RESPONSE: Entry=%u, Name=%s, Title=%s"),
            Entry, *Name, *Title);

        bool QuestChanged=false;
        auto Resolve=[&](FWowQuestLogEntry& Quest)
        {
            for (auto& Objective:Quest.Objectives)
                if (Objective.CreatureOrGOId==Entry && Objective.Text.IsEmpty())
                { Objective.Text=Name; QuestChanged=true; }
        };
        for (auto& Quest:QuestLog) Resolve(Quest);
        for (auto& Pair:QuestTemplateCache) Resolve(Pair.Value);
        if (QuestChanged) OnQuestLogUpdated.Broadcast();
        OnCreatureNameReceived.Broadcast(Entry, Name, Title);
    }

    // Skip the rest of the packet (icon name, type flags, etc.)
}

// ── Cooldown Helper Methods ──────────────────────────────────────────────────

bool FWowPacketHandler::IsSpellOnCooldown(uint32 SpellId) const
{
    const double* ExpiryTime = SpellCooldowns.Find(SpellId);
    return ExpiryTime && FPlatformTime::Seconds() < *ExpiryTime;
}

float FWowPacketHandler::GetSpellCooldownRemaining(uint32 SpellId) const
{
    const double* ExpiryTime = SpellCooldowns.Find(SpellId);
    if (!ExpiryTime)
    {
        return 0.0f;
    }

    double Remaining = *ExpiryTime - FPlatformTime::Seconds();
    return FMath::Max(0.0f, static_cast<float>(Remaining));
}

// ── Emote Handlers ───────────────────────────────────────────────────────

void FWowPacketHandler::HandleEmote(FPacketReader& R)
{
    // SMSG_EMOTE
    // uint32 emoteId + uint64 guid

    if (!R.CanRead(12)) // 4 + 8
    {
        UE_LOG(LogWowPacket, Error, TEXT("EMOTE packet too short"));
        return;
    }

    uint32 EmoteId = R.ReadU32();
    uint64 Guid = R.ReadU64();

    UE_LOG(LogWowPacket, Log, TEXT("EMOTE: EmoteId=%u, GUID=%llu"), EmoteId, Guid);

    // Broadcast emote event for animation system
    OnEmote.Broadcast(Guid, EmoteId);
}

void FWowPacketHandler::HandlePlaySound(FPacketReader& R)
{
    // SMSG_PLAY_SOUND: uint32 soundId (SoundEntries.dbc) - scripted boss/NPC lines, events.
    if (!R.CanRead(4)) return;
    const uint32 SoundId = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("PLAY_SOUND: %u"), SoundId);
    OnPlayServerSound.Broadcast(SoundId, 0);
}

void FWowPacketHandler::HandlePlayObjectSound(FPacketReader& R)
{
    // SMSG_PLAY_OBJECT_SOUND: uint32 soundId, uint64 source guid (positional).
    if (!R.CanRead(12)) return;
    const uint32 SoundId = R.ReadU32();
    const uint64 Guid = R.ReadU64();
    UE_LOG(LogWowPacket, Log, TEXT("PLAY_OBJECT_SOUND: %u from %llu"), SoundId, Guid);
    OnPlayServerSound.Broadcast(SoundId, Guid);
}

void FWowPacketHandler::HandleGameObjectCustomAnim(FPacketReader& R)
{
    // SMSG_GAMEOBJECT_CUSTOM_ANIM: uint64 guid, uint32 anim. A fishing bobber gets anim 0 when a
    // fish bites: the splash that tells the player to click it (r253; was unhandled).
    if (!R.CanRead(12)) return;
    const uint64 Guid = R.ReadU64();
    const uint32 Anim = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("GAMEOBJECT_CUSTOM_ANIM: %llu anim=%u"), Guid, Anim);
    OnGameObjectCustomAnim.Broadcast(Guid, Anim);
}

void FWowPacketHandler::HandleMirrorImageData(FPacketReader& R)
{
    // uint64 guid, uint32 display; for a dressed character also race, gender, class, skin, face,
    // hair style, hair colour, facial hair (u8 each), guild id (u32) and 11 item display ids (r257).
    if (!R.CanRead(12)) return;
    FWowMirrorImageData Data;
    Data.Guid = R.ReadU64();
    Data.DisplayId = R.ReadU32();
    if (R.CanRead(8 + 4 + 11 * 4))
    {
        Data.Race = R.ReadU8(); Data.Gender = R.ReadU8(); Data.Class = R.ReadU8();
        Data.Skin = R.ReadU8(); Data.Face = R.ReadU8(); Data.Hair = R.ReadU8();
        Data.HairColor = R.ReadU8(); Data.FacialHair = R.ReadU8();
        R.ReadU32(); // guild
        for (uint32& Display : Data.ItemDisplay) Display = R.ReadU32();
        Data.bHasAppearance = !R.HasFailed() && Data.Race > 0 && Data.Gender <= 1;
    }
    UE_LOG(LogWowPacket, Log, TEXT("MIRRORIMAGE_DATA: %llu display=%u race=%u gender=%u dressed=%d"),
        Data.Guid, Data.DisplayId, Data.Race, Data.Gender, Data.bHasAppearance ? 1 : 0);
    OnMirrorImageData.Broadcast(Data);
}

void FWowPacketHandler::HandlePlayMusic(FPacketReader& R)
{
    // SMSG_PLAY_MUSIC: uint32 soundId - scripted music (encounters, events).
    if (!R.CanRead(4)) return;
    const uint32 SoundId = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("PLAY_MUSIC: %u"), SoundId);
    OnPlayServerMusic.Broadcast(SoundId);
}

void FWowPacketHandler::HandleAIReaction(FPacketReader& R)
{
    // SMSG_AI_REACTION: uint64 guid, uint32 reaction (2 = AI_REACTION_HOSTILE: the creature aggroed).
    if (!R.CanRead(12)) return;
    const uint64 Guid = R.ReadU64();
    const uint32 Reaction = R.ReadU32();
    OnAIReaction.Broadcast(Guid, Reaction);
}

void FWowPacketHandler::HandleTextEmote(FPacketReader& R)
{
    // SMSG_TEXT_EMOTE (3.3.5a): uint64 senderGuid, uint32 textEmoteId (EmotesText.dbc),
    // uint32 emoteNum, uint32 targetNameLength, then the target name (a single 0 byte
    // when there is no target). The client builds the chat text itself from
    // EmotesText/EmotesTextData and plays the race/gender voice line; the animation
    // comes separately (SMSG_EMOTE or UNIT_NPC_EMOTESTATE).
    if (!R.CanRead(20)) // 8 + 4 + 4 + 4
    {
        UE_LOG(LogWowPacket, Error, TEXT("TEXT_EMOTE packet too short"));
        return;
    }
    const uint64 SenderGuid = R.ReadU64();
    const uint32 TextEmoteId = R.ReadU32();
    const uint32 EmoteNum = R.ReadU32();
    const uint32 NameLength = R.ReadU32();
    FString TargetName;
    if (NameLength > 1 && R.CanRead(1))
    {
        TargetName = R.ReadCString();
    }
    UE_LOG(LogWowPacket, Log, TEXT("TEXT_EMOTE: Sender=%llu TextEmoteId=%u EmoteNum=%u Target=%s"),
           SenderGuid, TextEmoteId, EmoteNum, *TargetName);
    OnTextEmote.Broadcast(SenderGuid, TextEmoteId, EmoteNum, TargetName);
}

// ── Player progression handlers ─────────────────────────────────────────

void FWowPacketHandler::HandleLevelUpInfo(FPacketReader& R)
{
    // SMSG_LEVELUP_INFO (build 12340)
    // uint32 Level, uint32 HealthGained, 7x uint32 power gains (MAX_POWERS), 5x uint32 stat gains (STR/AGI/STA/INT/SPI)

    if (!R.CanRead(56)) // 14 * 4 bytes
    {
        UE_LOG(LogWowPacket, Warning, TEXT("LEVELUP_INFO: packet too short"));
        return;
    }

    uint32 Level = R.ReadU32();
    uint32 HealthGained = R.ReadU32();
    uint32 ManaGained = R.ReadU32();
    R.Skip(6 * 4); // rage, focus, energy, happiness, runes, runic power
    uint32 StrGained = R.ReadU32();
    uint32 AgiGained = R.ReadU32();
    uint32 StaGained = R.ReadU32();
    uint32 IntGained = R.ReadU32();
    uint32 SpiGained = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("LEVELUP_INFO: Level %u, HP+%u, MP+%u, STR+%u, AGI+%u, STA+%u, INT+%u, SPI+%u"),
           Level, HealthGained, ManaGained, StrGained, AgiGained, StaGained, IntGained, SpiGained);

    LastLevelUpGains = { int32(HealthGained), int32(ManaGained), int32(StrGained), int32(AgiGained),
        int32(StaGained), int32(IntGained), int32(SpiGained) };
    OnLevelUp.Broadcast(Level);
}

void FWowPacketHandler::HandleLogXPGain(FPacketReader& R)
{
    // SMSG_LOG_XPGAIN
    // uint64 VictimGuid, uint32 XPAmount, uint8 Type (0=kill, 1=non-kill)

    if (!R.CanRead(13)) // 8 + 4 + 1
    {
        UE_LOG(LogWowPacket, Warning, TEXT("LOG_XPGAIN: packet too short"));
        return;
    }

    uint64 VictimGuid = R.ReadU64();
    uint32 XPAmount = R.ReadU32();
    uint8 Type = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("LOG_XPGAIN: +%u XP from GUID %llu (type %u)"), XPAmount, VictimGuid, Type);

    if (Type == 0)
    {
        // WotLK SMSG_LOG_XPGAIN: base XP, group multiplier, then RaF flag.
        // Bonus XP is rested only when the server explicitly excludes RaF.
        // https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Entities/Player/Player.cpp
        if (!R.CanRead(8)) { UE_LOG(LogWowPacket,Warning,TEXT("Truncated kill XP details")); return; }
        const uint32 BaseXP=R.ReadU32();
        R.ReadFloat();
        const bool HasBonusKind=R.CanRead(1);
        const bool RecruitAFriend=HasBonusKind && R.ReadU8()!=0;
        const uint32 RestedBonus=HasBonusKind && !RecruitAFriend && XPAmount>BaseXP ? XPAmount-BaseXP : 0;
        OnKillXPGain.Broadcast(XPAmount,RestedBonus);
    }
    else if (XPAmount>0)
    {
        // Quest turn-in / other non-kill XP: show the same floating +XP as kills.
        OnKillXPGain.Broadcast(XPAmount,0);
    }
    OnXPGain.Broadcast(XPAmount, Type);
}

void FWowPacketHandler::HandleExplorationExperience(FPacketReader& R)
{
    // SMSG_EXPLORATION_EXPERIENCE
    // uint32 AreaId, uint32 XPAmount

    if (!R.CanRead(8)) // 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("EXPLORATION_EXPERIENCE: packet too short"));
        return;
    }

    uint32 AreaId = R.ReadU32();
    uint32 XPAmount = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("EXPLORATION_EXPERIENCE: +%u XP for exploring area %u"), XPAmount, AreaId);

    // Broadcast as XP gain (type 1 = non-kill)
    OnXPGain.Broadcast(XPAmount, 1);

    // r274 (Stefan 13.10: discovering a new area showed nothing in the General window): the client
    // prints ERR_ZONE_EXPLORED_XP "Discovered %s: %d experience gained" (ERR_ZONE_EXPLORED
    // "Discovered %s" without XP) as a yellow system message.
    const FAreaTableDbcEntry* Area = FDbcStore::Get().AreaTable().GetById(AreaId);
    const FString AreaName = Area && !Area->Name.IsEmpty() ? Area->Name : FString::Printf(TEXT("area %u"), AreaId);
    const FString Line = XPAmount > 0
        ? FString::Printf(TEXT("Discovered %s: %u experience gained"), *AreaName, XPAmount)
        : FString::Printf(TEXT("Discovered %s"), *AreaName);
    OnChatMessage.Broadcast(0, 0, 0, FString(), Line, FString());
}

void FWowPacketHandler::HandleEnvironmentalDamageLog(FPacketReader& R)
{
    // SMSG_ENVIRONMENTAL_DAMAGE_LOG
    // uint64 VictimGuid, uint8 DamageType, uint32 Damage, uint32 Absorbed, uint32 Resisted

    if (!R.CanRead(21)) // 8 + 1 + 4 + 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("ENVIRONMENTAL_DAMAGE_LOG: packet too short"));
        return;
    }

    uint64 VictimGuid = R.ReadU64();
    uint8 DamageType = R.ReadU8();
    uint32 Damage = R.ReadU32();
    uint32 Absorbed = R.ReadU32();
    uint32 Resisted = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("ENVIRONMENTAL_DAMAGE_LOG: GUID %llu, type %u, %u damage (%u absorbed, %u resisted)"),
           VictimGuid, DamageType, Damage, Absorbed, Resisted);

    static const TCHAR* const EnvironmentalTypes[] = { TEXT("FATIGUE"), TEXT("DROWNING"), TEXT("FALLING"),
        TEXT("LAVA"), TEXT("SLIME"), TEXT("FIRE"), TEXT("FALLING") };
    FWowCombatLogEntry Entry;
    Entry.Event = TEXT("ENVIRONMENTAL_DAMAGE");
    Entry.DestGuid = VictimGuid;
    Entry.EnvironmentalType = EnvironmentalTypes[DamageType < UE_ARRAY_COUNT(EnvironmentalTypes) ? DamageType : 2];
    Entry.Amount = int32(Damage);
    Entry.Absorbed = int32(Absorbed);
    Entry.Resisted = int32(Resisted);
    Entry.School = DamageType == 3 || DamageType == 5 ? 4 : DamageType == 4 ? 8 : 1;
    OnCombatLog.Broadcast(Entry);
}

void FWowPacketHandler::HandleStartMirrorTimer(FPacketReader& R)
{
    // uint32 timer, uint32 current, uint32 maximum, int32 scale, uint8 paused, uint32 spell
    if (!R.Require(21, TEXT("SMSG_START_MIRROR_TIMER")))
    {
        return;
    }

    const uint32 TimerId = R.ReadU32();
    const int32 ValueMs = static_cast<int32>(R.ReadU32());
    const int32 MaxValueMs = static_cast<int32>(R.ReadU32());
    const int32 Scale = static_cast<int32>(R.ReadU32());
    const bool bPaused = R.ReadU8() != 0;
    const uint32 SpellId = R.ReadU32();

    if (TimerId > 2 || ValueMs < 0 || MaxValueMs <= 0)
    {
        R.Fail(FString::Printf(TEXT("Invalid mirror timer id/value/max: %u/%d/%d"),
            TimerId, ValueMs, MaxValueMs));
        return;
    }

    OnMirrorTimerStarted.Broadcast(TimerId, ValueMs, MaxValueMs, Scale, bPaused, SpellId);
}

void FWowPacketHandler::HandlePauseMirrorTimer(FPacketReader& R)
{
    if (!R.Require(5, TEXT("SMSG_PAUSE_MIRROR_TIMER")))
    {
        return;
    }

    const uint32 TimerId = R.ReadU32();
    const bool bPaused = R.ReadU8() != 0;
    if (TimerId > 2)
    {
        R.Fail(FString::Printf(TEXT("Invalid mirror timer id: %u"), TimerId));
        return;
    }

    OnMirrorTimerPaused.Broadcast(TimerId, bPaused);
}

void FWowPacketHandler::HandleStopMirrorTimer(FPacketReader& R)
{
    if (!R.Require(4, TEXT("SMSG_STOP_MIRROR_TIMER")))
    {
        return;
    }

    const uint32 TimerId = R.ReadU32();
    if (TimerId > 2)
    {
        R.Fail(FString::Printf(TEXT("Invalid mirror timer id: %u"), TimerId));
        return;
    }

    OnMirrorTimerStopped.Broadcast(TimerId);
}

void FWowPacketHandler::HandleBindPointUpdate(FPacketReader& R)
{
    // SMSG_BINDPOINTUPDATE
    // float X, float Y, float Z, uint32 MapId, uint32 AreaId

    if (!R.CanRead(20)) // 3*4 + 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("BINDPOINTUPDATE: packet too short"));
        return;
    }

    float X = R.ReadFloat();
    float Y = R.ReadFloat();
    float Z = R.ReadFloat();
    uint32 MapId = R.ReadU32();
    uint32 AreaId = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("BINDPOINTUPDATE: Map %u, Area %u, Position (%.2f, %.2f, %.2f)"),
           MapId, AreaId, X, Y, Z);

    BindAreaId = AreaId;
    OnBindPointUpdate.Broadcast(X, Y, Z, MapId, AreaId);
}

// r527 (Stefan: setting a new hearthstone location gave no effect on him and no cast from the innkeeper):
// "Make this inn your home" does not bind yet. AzerothCore answers with SMSG_BINDER_CONFIRM (uint64
// innkeeper GUID) and waits for CMSG_BINDER_ACTIVATE; we ignored it, so nothing was bound and no Bind
// spell (3286, the innkeeper's cast and the effect and sound on us) followed. The UI asks the original
// question (StaticPopup CONFIRM_BINDER) and sends CMSG_BINDER_ACTIVATE with PendingBinderGuid.
void FWowPacketHandler::HandleBinderConfirm(FPacketReader& R)
{
    if (!R.CanRead(8))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("BINDER_CONFIRM: packet too short"));
        return;
    }
    PendingBinderGuid = R.ReadU64();
    UE_LOG(LogWowPacket, Log, TEXT("BINDER_CONFIRM: innkeeper %llu asks to make this inn our home"), PendingBinderGuid);
    OnBinderConfirm.Broadcast(PendingBinderGuid);
}

// SMSG_PLAYERBOUND: uint64 binder GUID, uint32 AreaTable id ("<area> is now your home." in the original).
void FWowPacketHandler::HandlePlayerBound(FPacketReader& R)
{
    if (!R.CanRead(12))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PLAYERBOUND: packet too short"));
        return;
    }
    const uint64 Binder = R.ReadU64();
    const uint32 AreaId = R.ReadU32();
    PendingBinderGuid = 0;
    UE_LOG(LogWowPacket, Log, TEXT("PLAYERBOUND: bound by %llu, area %u"), Binder, AreaId);
    OnPlayerBound.Broadcast(Binder, AreaId);
}

// r340 (OPEN_ISSUES: the Reputation tab had no data - GetNumFactions 0): the faction standings.
// SMSG_INITIALIZE_FACTIONS: uint32 count (128), then per ReputationListID uint8 flags, int32 standing.
void FWowPacketHandler::HandleInitializeFactions(FPacketReader& R)
{
    if (!R.CanRead(4)) return;
    const uint32 Count = R.ReadU32();
    if (Count > 1024 || !R.CanRead(Count * 5)) { UE_LOG(LogWowPacket, Warning, TEXT("INITIALIZE_FACTIONS: bad count %u"), Count); return; }
    FactionStates.SetNum(Count);
    int32 Visible = 0;
    for (uint32 I = 0; I < Count; ++I)
    {
        FactionStates[I].Flags = R.ReadU8();
        FactionStates[I].Standing = static_cast<int32>(R.ReadU32());
        Visible += (FactionStates[I].Flags & 0x01) ? 1 : 0;
    }
    ++FactionRevision;
    UE_LOG(LogWowPacket, Log, TEXT("INITIALIZE_FACTIONS: %u factions, %d visible"), Count, Visible);
    OnGuildBankEvent.Broadcast(TEXT("UPDATE_FACTION"), -1); // generic Lua event relay
}

// SMSG_SET_FACTION_VISIBLE: uint32 ReputationListID - a faction you just met shows in the list.
void FWowPacketHandler::HandleSetFactionVisible(FPacketReader& R)
{
    if (!R.CanRead(4)) return;
    const uint32 Index = R.ReadU32();
    if (Index >= 1024) return;
    if (FactionStates.Num() <= int32(Index)) FactionStates.SetNum(Index + 1);
    FactionStates[Index].Flags |= 0x01;
    ++FactionRevision;
    OnGuildBankEvent.Broadcast(TEXT("UPDATE_FACTION"), -1);
}

// SMSG_SET_FACTION_STANDING: float refer-a-friend bonus, uint8 "increased" (plays the sound),
// uint32 count, then (uint32 ReputationListID, int32 standing) pairs.
void FWowPacketHandler::HandleSetFactionStanding(FPacketReader& R)
{
    if (!R.CanRead(9)) return;
    R.ReadFloat();
    R.ReadU8();
    const uint32 Count = R.ReadU32();
    if (Count > 1024 || !R.CanRead(Count * 8)) return;
    for (uint32 I = 0; I < Count; ++I)
    {
        const uint32 Index = R.ReadU32();
        const int32 Standing = static_cast<int32>(R.ReadU32());
        if (Index >= 1024) continue;
        if (FactionStates.Num() <= int32(Index)) FactionStates.SetNum(Index + 1);
        FactionStates[Index].Standing = Standing;
        FactionStates[Index].Flags |= 0x01;
    }
    ++FactionRevision;
    OnGuildBankEvent.Broadcast(TEXT("UPDATE_FACTION"), -1);
}

void FWowPacketHandler::HandlePlayedTime(FPacketReader& R)
{
    // SMSG_PLAYED_TIME
    // uint32 TotalTime, uint32 LevelTime, uint8 TriggerEvent

    if (!R.CanRead(9)) // 4 + 4 + 1
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PLAYED_TIME: packet too short"));
        return;
    }

    uint32 TotalTime = R.ReadU32();
    uint32 LevelTime = R.ReadU32();
    uint8 TriggerEvent = R.ReadU8();

    uint32 TotalHours = TotalTime / 3600;
    uint32 TotalMinutes = (TotalTime % 3600) / 60;
    uint32 LevelHours = LevelTime / 3600;
    uint32 LevelMinutes = (LevelTime % 3600) / 60;

    UE_LOG(LogWowPacket, Log, TEXT("PLAYED_TIME: Total %uh %um, This Level %uh %um (event %u)"),
           TotalHours, TotalMinutes, LevelHours, LevelMinutes, TriggerEvent);
    if (TriggerEvent)
    {
        auto Format = [](uint32 Seconds)
        {
            return FString::Printf(TEXT("%u days, %u hours, %u minutes, %u seconds"),
                Seconds / 86400, Seconds / 3600 % 24, Seconds / 60 % 60, Seconds % 60);
        };
        OnChatMessage.Broadcast(0, 0, 0, FString(), TEXT("Total time played: ") + Format(TotalTime), FString());
        OnChatMessage.Broadcast(0, 0, 0, FString(), TEXT("Time played this level: ") + Format(LevelTime), FString());
    }
}

// ── World state handlers ────────────────────────────────────────────────

void FWowPacketHandler::HandleWeather(FPacketReader& R)
{
    // SMSG_WEATHER
    // uint32 WeatherState, float Grade, uint8 Abrupt

    if (!R.CanRead(9)) // 4 + 4 + 1
    {
        UE_LOG(LogWowPacket, Warning, TEXT("WEATHER: packet too short"));
        return;
    }

    uint32 WeatherState = R.ReadU32();
    float Grade = R.ReadFloat();
    uint8 Abrupt = R.ReadU8();
    if (!FMath::IsFinite(Grade)) return;
    Grade=FMath::Clamp(Grade,0.f,1.f);

    UE_LOG(LogWowPacket, Log, TEXT("WEATHER: State %u, Grade %.2f, Abrupt %u"), WeatherState, Grade, Abrupt);

    OnWeatherUpdate.Broadcast(WeatherState, Grade, Abrupt);
}

void FWowPacketHandler::HandleInitWorldStates(FPacketReader& R)
{
    // SMSG_INIT_WORLD_STATES
    // uint32 MapId, uint32 AreaId, uint32 SubAreaId, uint16 Count, then Count x (uint32 Field, uint32 Value)

    if (!R.CanRead(14)) // 4 + 4 + 4 + 2
    {
        UE_LOG(LogWowPacket, Warning, TEXT("INIT_WORLD_STATES: packet too short"));
        return;
    }

    uint32 MapId = R.ReadU32();
    uint32 AreaId = R.ReadU32();
    uint32 SubAreaId = R.ReadU32();
    uint16 Count = R.ReadU16();

    UE_LOG(LogWowPacket, Log, TEXT("INIT_WORLD_STATES: Map %u, Area %u, SubArea %u, %u states"), MapId, AreaId, SubAreaId, Count);

    // Clear existing world states
    WorldStates.Empty();

    // Read each world state
    for (uint16 i = 0; i < Count && R.CanRead(8); ++i)
    {
        uint32 Field = R.ReadU32();
        uint32 Value = R.ReadU32();
        WorldStates.Add(Field, Value);
        UE_LOG(LogWowPacket, VeryVerbose, TEXT("  World State %u = %u"), Field, Value);
    }

    OnWorldStatesInit.Broadcast(MapId, AreaId, SubAreaId);
}

void FWowPacketHandler::HandleUpdateWorldState(FPacketReader& R)
{
    // SMSG_UPDATE_WORLD_STATE
    // uint32 Field, uint32 Value

    if (!R.CanRead(8)) // 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("UPDATE_WORLD_STATE: packet too short"));
        return;
    }

    uint32 Field = R.ReadU32();
    uint32 Value = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("UPDATE_WORLD_STATE: Field %u = %u"), Field, Value);

    // Update world state
    WorldStates.FindOrAdd(Field) = Value;

    OnWorldStateUpdate.Broadcast(Field, Value);
}

// ── Account/Party handlers ──────────────────────────────────────────────

void FWowPacketHandler::HandleSetProficiency(FPacketReader& R)
{
    // SMSG_SET_PROFICIENCY
    // uint8 ItemClass, uint32 ItemSubclassMask

    if (!R.CanRead(5)) // 1 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SET_PROFICIENCY: packet too short"));
        return;
    }

    uint8 ItemClass = R.ReadU8();
    uint32 ItemSubclassMask = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("SET_PROFICIENCY: ItemClass %u, SubclassMask 0x%08X"), ItemClass, ItemSubclassMask);

    OnProficiencySet.Broadcast(ItemClass, ItemSubclassMask);
}

void FWowPacketHandler::HandleAccountDataTimes(FPacketReader& R)
{
    // SMSG_ACCOUNT_DATA_TIMES
    // uint32 Time, uint8 Mask, then up to 8x uint32 timestamps

    if (!R.CanRead(5)) // 4 + 1
    {
        UE_LOG(LogWowPacket, Warning, TEXT("ACCOUNT_DATA_TIMES: packet too short"));
        return;
    }

    uint32 Time = R.ReadU32();
    uint8 Mask = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("ACCOUNT_DATA_TIMES: Time %u, Mask 0x%02X"), Time, Mask);

    // Read timestamps for each enabled bit
    for (int32 i = 0; i < 8; ++i)
    {
        if ((Mask & (1 << i)) && R.CanRead(4))
        {
            uint32 Timestamp = R.ReadU32();
            UE_LOG(LogWowPacket, VeryVerbose, TEXT("  Account data %d timestamp: %u"), i, Timestamp);
        }
    }

    // No action needed - just prevents server disconnect
}

void FWowPacketHandler::HandlePartyMemberStats(FPacketReader& R)
{
    // SMSG_PARTY_MEMBER_STATS
    // Complex packet with variable structure

    if (!R.CanRead(9)) // packed guid + mask
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PARTY_MEMBER_STATS: packet too short"));
        return;
    }

    uint64 MemberGuid = R.ReadPackedGuid();
    uint32 Mask = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("PARTY_MEMBER_STATS: Member GUID %llu, Mask 0x%08X"), MemberGuid, Mask);

    // For now, just skip the rest - full implementation would parse all the conditional fields
    // This prevents errors when receiving these packets
}

// ── NPC interaction handlers ────────────────────────────────────────────

void FWowPacketHandler::HandleGossipMessage(FPacketReader& R)
{
    // SMSG_GOSSIP_MESSAGE
    // uint64 NpcGuid, uint32 MenuId, uint32 TextId, uint32 OptionCount
    // Then OptionCount x (uint32 OptionId, uint8 OptionIcon, uint8 OptionCoded, uint32 BoxMoney, FString Text, FString BoxText)
    // Then uint32 QuestCount, QuestCount x (uint32 QuestId, uint32 QuestIcon, int32 QuestLevel, uint32 QuestFlags, uint8 Repeatable, FString Title)

    if (!R.CanRead(20)) // 8 + 4 + 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("GOSSIP_MESSAGE: packet too short"));
        return;
    }

    FWowGossipMenu Menu;
    Menu.NpcGuid = R.ReadU64();
    if (const FWowEntity* Npc = EntityManager.Find(Menu.NpcGuid))
    {
        // Gameobjects (books, plaques - HIGHGUID 0xF110) use the GO name, not the creature cache
        // whose entries can collide (Stefan 10.10: "Draconic for Dummies" titled "Quest").
        const bool bGossipFromObject = ((Menu.NpcGuid >> 48) & 0xFFFF) == 0xF110;
        Menu.NpcName = bGossipFromObject ? GameObjectNames.FindRef(Npc->Entry) : CreatureNameCache.FindRef(Npc->Entry);
    }
    Menu.MenuId = R.ReadU32();
    Menu.TextId = R.ReadU32();
    uint32 OptionCount = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("GOSSIP_MESSAGE: NPC GUID %llu, Text %u, Menu %u, %u options"),
           Menu.NpcGuid, Menu.TextId, Menu.MenuId, OptionCount);

    // Read gossip options
    for (uint32 i = 0; i < OptionCount && R.Remaining() > 14; ++i) // minimum: 4 + 1 + 1 + 4 + 2 nulls = 12
    {
        if (!R.CanRead(10)) break; // minimum for option header

        FWowGossipOption& Option = Menu.Options.AddDefaulted_GetRef();
        Option.OptionId = R.ReadU32();
        Option.Icon = R.ReadU8();
        Option.bCoded = R.ReadU8() != 0;
        Option.BoxMoney = R.ReadU32();
        Option.Text = R.ReadCString();
        Option.BoxText = R.ReadCString();

        UE_LOG(LogWowPacket, VeryVerbose, TEXT("  Option %u: Icon %u, Text '%s'"),
            Option.OptionId, Option.Icon, *Option.Text);
    }

    // Read quest options
    if (R.CanRead(4))
    {
        uint32 QuestCount = R.ReadU32();
        UE_LOG(LogWowPacket, Log, TEXT("  %u quest options"), QuestCount);

        for (uint32 i = 0; i < QuestCount && R.Remaining() > 18; ++i) // minimum for quest header
        {
            if (!R.CanRead(17)) break; // minimum: 4 + 4 + 4 + 4 + 1

            FWowQuestMenuItem& Quest = Menu.Quests.AddDefaulted_GetRef();
            Quest.QuestId = R.ReadU32();
            Quest.Icon = R.ReadU32();
            Quest.Level = static_cast<int32>(R.ReadU32());
            Quest.Flags = R.ReadU32();
            Quest.bRepeatable = R.ReadU8() != 0;
            Quest.Title = R.ReadCString();
            QuestTitleCache.Add(Quest.QuestId, Quest.Title);

            UE_LOG(LogWowPacket, VeryVerbose, TEXT("  Quest %u: Level %d, Title '%s'"),
                Quest.QuestId, Quest.Level, *Quest.Title);
        }
    }

    if (R.HasFailed()) return;
    Menu.Greeting = NpcTextCache.FindRef(Menu.TextId);
    OnGossipMessage.Broadcast(Menu);
    if (Menu.TextId != 0 && !NpcTextCache.Contains(Menu.TextId) && OnSendPacket.IsBound())
    {
        TArray<uint8> Query;
        Query.Append(reinterpret_cast<const uint8*>(&Menu.TextId), sizeof(Menu.TextId));
        Query.Append(reinterpret_cast<const uint8*>(&Menu.NpcGuid), sizeof(Menu.NpcGuid));
        OnSendPacket.Execute(WowOpcode::CMSG_NPC_TEXT_QUERY, Query);
    }
}

void FWowPacketHandler::HandleGossipComplete(FPacketReader& R)
{
    (void)R;
    UE_LOG(LogWowPacket, Log, TEXT("GOSSIP_COMPLETE"));
    OnGossipComplete.Broadcast();
}

// ── Mail system handlers ────────────────────────────────────────────

void FWowPacketHandler::HandleShowMailbox(FPacketReader& R)
{
    if (!R.CanRead(8))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SHOW_MAILBOX: packet too short"));
        return;
    }

    CurrentMailboxGuid = R.ReadU64();

    UE_LOG(LogWowPacket, Log, TEXT("SHOW_MAILBOX: mailbox GUID %llu"), CurrentMailboxGuid);
    OnMailboxShown.Broadcast(CurrentMailboxGuid);
}

void FWowPacketHandler::HandleMailListResult(FPacketReader& R)
{
    // SMSG_MAIL_LIST_RESULT
    // uint32 totalMailCount, uint8 displayedMailCount, repeated mail blocks.

    if (!R.CanRead(5))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("MAIL_LIST_RESULT: packet too short"));
        return;
    }

    constexpr int32 MailAttachmentEnchantSlots = 7;

    MailInboxTotalCount = R.ReadU32();
    const uint8 MailCount = R.ReadU8();

    TArray<FWowMailMessage> ParsedMail;
    ParsedMail.Reserve(MailCount);

    for (uint8 MailIndex = 0; MailIndex < MailCount; ++MailIndex)
    {
        if (!R.CanRead(2))
        {
            UE_LOG(LogWowPacket, Warning, TEXT("MAIL_LIST_RESULT: missing block size for mail %d/%d"), MailIndex + 1, MailCount);
            break;
        }

        const int32 BlockStart = R.Pos;
        const uint16 BlockSize = R.ReadU16();
        const int32 BlockEnd = BlockStart + BlockSize;
        if (BlockSize < 2 || BlockEnd > R.Size)
        {
            UE_LOG(LogWowPacket, Warning, TEXT("MAIL_LIST_RESULT: invalid block size %u for mail %d/%d"), BlockSize, MailIndex + 1, MailCount);
            break;
        }

        FPacketReader MailReader(R.Data + R.Pos, BlockEnd - R.Pos);

        FWowMailMessage Mail;
        Mail.MessageId = MailReader.ReadU32();
        Mail.MessageType = MailReader.ReadU8();

        if (Mail.MessageType == WowMailMessageType::NORMAL)
        {
            Mail.SenderGuid = MailReader.ReadU64();
        }
        else
        {
            Mail.SenderEntry = MailReader.ReadU32();
        }

        Mail.COD = MailReader.ReadU32();
        MailReader.ReadU32(); // 3.3.5a unknown field
        Mail.Stationery = MailReader.ReadU32();
        Mail.Money = MailReader.ReadU32();
        Mail.Checked = MailReader.ReadU32();
        Mail.DaysLeft = MailReader.ReadFloat();
        Mail.MailTemplateId = MailReader.ReadU32();
        Mail.Subject = MailReader.ReadCString();
        Mail.Body = MailReader.ReadCString();

        const uint8 AttachmentCount = MailReader.ReadU8();
        Mail.Attachments.Reserve(AttachmentCount);

        for (uint8 AttachmentIndex = 0; AttachmentIndex < AttachmentCount; ++AttachmentIndex)
        {
            if (!MailReader.CanRead(1 + 4 + 4 + (MailAttachmentEnchantSlots * 12) + 4 + 4 + 4 + 4 + 4 + 4 + 1))
            {
                UE_LOG(LogWowPacket, Warning, TEXT("MAIL_LIST_RESULT: truncated attachment %d for mail %u"), AttachmentIndex, Mail.MessageId);
                break;
            }

            FWowMailAttachment Attachment;
            Attachment.AttachmentIndex = MailReader.ReadU8();
            Attachment.ItemGuidLow = MailReader.ReadU32();
            Attachment.ItemEntry = MailReader.ReadU32();

            for (int32 EnchantIndex = 0; EnchantIndex < MailAttachmentEnchantSlots; ++EnchantIndex)
            {
                MailReader.Skip(12);
            }

            Attachment.RandomPropertyId = static_cast<int32>(MailReader.ReadU32());
            Attachment.SuffixFactor = MailReader.ReadU32();
            Attachment.Count = MailReader.ReadU32();
            Attachment.Charges = MailReader.ReadU32();
            Attachment.MaxDurability = MailReader.ReadU32();
            Attachment.Durability = MailReader.ReadU32();
            MailReader.ReadU8(); // 3.3.5a trailing unknown byte

            Mail.Attachments.Add(Attachment);
        }

        if (MailReader.Pos > MailReader.Size)
        {
            UE_LOG(LogWowPacket, Warning, TEXT("MAIL_LIST_RESULT: mail %u parser overran its block"), Mail.MessageId);
        }

        R.Pos = BlockEnd;

        ParsedMail.Add(MoveTemp(Mail));
    }

    MailInbox = MoveTemp(ParsedMail);

    UE_LOG(LogWowPacket, Log, TEXT("MAIL_LIST_RESULT: received %d displayed mails (%u total)"), MailInbox.Num(), MailInboxTotalCount);
    OnMailListReceived.Broadcast(MailInbox);
}

void FWowPacketHandler::HandleSendMailResult(FPacketReader& R)
{
    // SMSG_SEND_MAIL_RESULT: u32 mailId, u32 action, u32 error; then u32 equipError when
    // error == MAIL_ERR_EQUIP_ERROR (1), or u32 itemGuidLow + u32 count for MAIL_ITEM_TAKEN.
    if (!R.CanRead(12))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SEND_MAIL_RESULT: packet too short"));
        return;
    }
    const uint32 MailId = R.ReadU32();
    const uint32 Action = R.ReadU32();
    const uint32 Error = R.ReadU32();
    uint32 EquipError = 0;
    if (Error == 1 && R.CanRead(4)) EquipError = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("SEND_MAIL_RESULT: mail %u action %u error %u equip %u"), MailId, Action, Error, EquipError);

    FString Message;
    switch (Error)
    {
    case 0: if (Action == 0) Message = TEXT("Mail sent."); break;
    case 1: Message = InventoryErrorText(static_cast<uint8>(EquipError)); break;
    case 2: Message = TEXT("You can't send mail to yourself."); break;
    case 3: Message = TEXT("You don't have enough money."); break;
    case 4: Message = TEXT("Cannot find mail recipient."); break;
    case 5: Message = TEXT("You can't send mail to that player."); break;
    case 15: Message = TEXT("You have reached the in-game cap of unique mail recipients"); break;
    case 16: Message = TEXT("You can't send wrapped items C.O.D."); break;
    case 18: Message = TEXT("A mail included too many attachments."); break;
    case 19: Message = TEXT("A mail attachment was invalid."); break;
    case 21: Message = TEXT("That item has expired."); break;
    default: Message = TEXT("Internal mail database error."); break;
    }
    // "Mail sent." is a yellow system message in the original client; errors are red.
    if (!Message.IsEmpty())
    {
        if (Error == 0) OnUiInfoMessage.Broadcast(Message);
        else OnUiErrorMessage.Broadcast(Message);
    }
    OnMailResult.Broadcast(MailId, Action, Error, EquipError);
}

void FWowPacketHandler::HandleReceivedMail(FPacketReader& R)
{
    UE_LOG(LogWowPacket, Log, TEXT("RECEIVED_MAIL: new mail"));
    OnNewMailReceived.Broadcast();
}

void FWowPacketHandler::RequestQuestPOIs()
{
    if (!OnSendPacket.IsBound()) return;
    if (SuperTrackedQuestId != 0 && !QuestLog.ContainsByPredicate([this](const FWowQuestLogEntry& E) { return E.QuestId == SuperTrackedQuestId; }))
        SuperTrackedQuestId = 0;
    TArray<uint32> Missing;
    for (const FWowQuestLogEntry& Entry : QuestLog)
    {
        if (Entry.QuestId != 0 && !QuestPOIRequested.Contains(Entry.QuestId))
        {
            Missing.Add(Entry.QuestId);
            QuestPOIRequested.Add(Entry.QuestId);
        }
    }
    for (int32 Start = 0; Start < Missing.Num(); Start += 25) // MAX_QUEST_LOG_SIZE per query
    {
        const int32 Count = FMath::Min(25, Missing.Num() - Start);
        TArray<uint8> Query;
        Query.SetNumUninitialized(4 + Count * 4);
        const uint32 Count32 = uint32(Count);
        FMemory::Memcpy(Query.GetData(), &Count32, 4);
        FMemory::Memcpy(Query.GetData() + 4, Missing.GetData() + Start, Count * 4);
        OnSendPacket.Execute(WowOpcode::CMSG_QUEST_POI_QUERY, Query);
    }
}

void FWowPacketHandler::HandleQuestPOIQueryResponse(FPacketReader& R)
{
    const uint32 QuestCount = R.ReadU32();
    for (uint32 Q = 0; Q < QuestCount && !R.HasFailed(); ++Q)
    {
        const uint32 QuestId = R.ReadU32();
        const uint32 PoiCount = R.ReadU32();
        TArray<FWowQuestPOI>& List = QuestPOIs.FindOrAdd(QuestId);
        List.Reset();
        for (uint32 P = 0; P < PoiCount && !R.HasFailed(); ++P)
        {
            FWowQuestPOI Poi;
            Poi.Id = R.ReadU32();
            Poi.ObjectiveIndex = int32(R.ReadU32());
            Poi.MapId = R.ReadU32();
            Poi.WorldMapAreaId = R.ReadU32();
            Poi.Floor = R.ReadU32();
            R.ReadU32(); // unk3
            R.ReadU32(); // unk4
            const uint32 PointCount = R.ReadU32();
            for (uint32 I = 0; I < PointCount && !R.HasFailed(); ++I)
            {
                const int32 X = int32(R.ReadU32());
                const int32 Y = int32(R.ReadU32());
                Poi.Points.Add(FVector2D(X, Y));
            }
            List.Add(MoveTemp(Poi));
        }
    }
    if (R.HasFailed())
        UE_LOG(LogWowPacket, Warning, TEXT("QUEST_POI_QUERY_RESPONSE: %s"), *R.GetFailureReason());
    UE_LOG(LogWowPacket, Log, TEXT("QUEST_POI_QUERY_RESPONSE: %u quests"), QuestCount);
}

void FWowPacketHandler::HandlePetSpells(FPacketReader& R)
{
    if (!R.CanRead(8))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PET_SPELLS: packet too short"));
        return;
    }

    const uint64 PetGuid = R.ReadU64();
    PetActionBar.Clear();

    if (PetGuid == 0)
    {
        UE_LOG(LogWowPacket, Log, TEXT("PET_SPELLS: cleared pet action bar"));
        OnPetBarUpdated.Broadcast(PetActionBar);
        return;
    }

    if (!R.CanRead(2 + 4 + 1 + 1 + 2))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PET_SPELLS: missing action bar header"));
        return;
    }

    PetActionBar.PetGuid = PetGuid;
    PetActionBar.Family = R.ReadU16();
    PetActionBar.DurationMs = R.ReadU32();
    PetActionBar.ReactState = R.ReadU8();
    PetActionBar.CommandState = R.ReadU8();
    PetActionBar.Flags = R.ReadU16();

    for (int32 SlotIndex = 0; SlotIndex < WOW_PET_ACTION_SLOT_COUNT; ++SlotIndex)
    {
        if (!R.CanRead(4))
        {
            UE_LOG(LogWowPacket, Warning, TEXT("PET_SPELLS: truncated action bar at slot %d"), SlotIndex);
            break;
        }

        PetActionBar.ActionSlots[SlotIndex].SetPackedData(R.ReadU32());
    }

    if (!R.CanRead(1))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PET_SPELLS: missing spell count"));
        OnPetBarUpdated.Broadcast(PetActionBar);
        return;
    }

    const uint8 SpellCount = R.ReadU8();
    PetActionBar.KnownSpells.Reserve(SpellCount);
    for (uint8 SpellIndex = 0; SpellIndex < SpellCount && R.CanRead(4); ++SpellIndex)
    {
        const uint32 PackedSpell = R.ReadU32();
        const uint32 SpellId = PackedSpell & 0x00FFFFFF;
        if (SpellId != 0)
        {
            PetActionBar.KnownSpells.Add(SpellId);
        }
    }

    if (!R.CanRead(1))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PET_SPELLS: missing cooldown count"));
        OnPetBarUpdated.Broadcast(PetActionBar);
        return;
    }

    const uint8 CooldownCount = R.ReadU8();
    const double NowSeconds = FPlatformTime::Seconds();
    for (uint8 CooldownIndex = 0; CooldownIndex < CooldownCount; ++CooldownIndex)
    {
        if (!R.CanRead(4 + 2 + 4 + 4))
        {
            break;
        }

        const uint32 SpellId = R.ReadU32();
        R.ReadU16(); // category
        const uint32 CooldownMs = R.ReadU32();
        const uint32 CategoryCooldownMs = R.ReadU32();
        const uint32 EffectiveCooldownMs = FMath::Max(CooldownMs, CategoryCooldownMs);
        if (SpellId != 0 && EffectiveCooldownMs > 0)
        {
            PetActionBar.SpellCooldownExpirySeconds.Add(
                SpellId,
                NowSeconds + static_cast<double>(EffectiveCooldownMs) / 1000.0);
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("PET_SPELLS: pet=%llu family=%u actions=%d spells=%d cooldowns=%d"),
        PetActionBar.PetGuid, PetActionBar.Family, PetActionBar.ActionSlots.Num(), PetActionBar.KnownSpells.Num(), CooldownCount);

    OnPetBarUpdated.Broadcast(PetActionBar);
}

void FWowPacketHandler::HandleDuelRequested(FPacketReader& R)
{
    if (!R.CanRead(16))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("DUEL_REQUESTED: packet too short"));
        return;
    }

    const uint64 ArbiterGuid = R.ReadU64();
    const uint64 InitiatorGuid = R.ReadU64();

    CurrentDuel.BeginRequest(ArbiterGuid, InitiatorGuid);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_REQUESTED: arbiter=%llu initiator=%llu"), ArbiterGuid, InitiatorGuid);
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelOutOfBounds(FPacketReader& R)
{
    static_cast<void>(R);
    CurrentDuel.SetInBounds(false);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_OUTOFBOUNDS"));
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelInBounds(FPacketReader& R)
{
    static_cast<void>(R);
    CurrentDuel.SetInBounds(true);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_INBOUNDS"));
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelComplete(FPacketReader& R)
{
    if (!R.CanRead(1))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("DUEL_COMPLETE: packet too short"));
        return;
    }

    const bool bFinishedNormally = (R.ReadU8() != 0);
    if (bFinishedNormally)
    {
        CurrentDuel.MarkCompleted();
    }
    else
    {
        CurrentDuel.MarkInterrupted();
    }

    UE_LOG(LogWowPacket, Log, TEXT("DUEL_COMPLETE: normal=%d"), bFinishedNormally ? 1 : 0);
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelWinner(FPacketReader& R)
{
    if (!R.CanRead(1))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("DUEL_WINNER: packet too short"));
        return;
    }

    const uint8 ResultReason = R.ReadU8();
    const FString WinnerName = R.ReadCString();
    const FString LoserName = R.ReadCString();

    CurrentDuel.SetWinner(ResultReason, WinnerName, LoserName);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_WINNER: reason=%u winner='%s' loser='%s'"),
        ResultReason, *WinnerName, *LoserName);
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelCountdown(FPacketReader& R)
{
    if (!R.CanRead(4))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("DUEL_COUNTDOWN: packet too short"));
        return;
    }

    const uint32 CountdownMilliseconds = R.ReadU32();
    CurrentDuel.BeginCountdown(static_cast<double>(CountdownMilliseconds) / 1000.0);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_COUNTDOWN: duration_ms=%u"), CountdownMilliseconds);
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleTradeStatus(FPacketReader& R)
{
    if (!R.CanRead(4))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("TRADE_STATUS: packet too short"));
        return;
    }

    const uint32 Status = R.ReadU32();
    CurrentTrade.Status = Status;
    auto ResetTradeOffers = [this]()
    {
        CurrentTrade.bTradeOpen = false;
        CurrentTrade.bLocalAccepted = false;
        CurrentTrade.bTargetAccepted = false;
        CurrentTrade.PlayerMoney = 0;
        CurrentTrade.TargetMoney = 0;
        CurrentTrade.PlayerSpell = 0;
        CurrentTrade.TargetSpell = 0;
        CurrentTrade.PlayerItems.Reset();
        CurrentTrade.TargetItems.Reset();
    };

    switch (Status)
    {
    case WowTradeStatus::BEGIN_TRADE:
        CurrentTrade = FWowTradeState{};
        CurrentTrade.Status = Status;
        CurrentTrade.TraderGuid = R.CanRead(8) ? R.ReadU64() : 0;
        UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS: begin trade with %llu"), CurrentTrade.TraderGuid);
        break;

    case WowTradeStatus::OPEN_WINDOW:
        if (R.CanRead(4))
        {
            R.ReadU32(); // unknown 2.4.0+ value
        }
        CurrentTrade.bTradeOpen = true;
        CurrentTrade.bLocalAccepted = false;
        CurrentTrade.bTargetAccepted = false;
        UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS: open window"));
        break;

    case WowTradeStatus::TRADE_ACCEPT:
        CurrentTrade.bTradeOpen = true;
        CurrentTrade.bTargetAccepted = true;
        UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS: remote player accepted"));
        break;

    case WowTradeStatus::BACK_TO_TRADE:
        CurrentTrade.bTradeOpen = true;
        CurrentTrade.bLocalAccepted = false;
        CurrentTrade.bTargetAccepted = false;
        UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS: acceptance reset"));
        break;

    case WowTradeStatus::CLOSE_WINDOW:
        if (R.CanRead(4))
        {
            R.ReadU32();
        }
        if (R.CanRead(1))
        {
            R.ReadU8();
        }
        if (R.CanRead(4))
        {
            R.ReadU32();
        }
        ResetTradeOffers();
        UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS: close window"));
        break;

    case WowTradeStatus::TRADE_CANCELED:
    case WowTradeStatus::TRADE_COMPLETE:
        ResetTradeOffers();
        UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS: terminal status %u"), Status);
        break;

    case WowTradeStatus::ONLY_CONJURED:
    case WowTradeStatus::NOT_ELIGIBLE:
        if (R.CanRead(1))
        {
            R.ReadU8();
        }
        UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS: restricted status %u"), Status);
        break;

    default:
        UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS: status %u"), Status);
        break;
    }

    OnTradeUpdated.Broadcast(CurrentTrade);
}

void FWowPacketHandler::HandleTradeStatusExtended(FPacketReader& R)
{
    if (!R.CanRead(1 + 4 + 4 + 4 + 4 + 4))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("TRADE_STATUS_EXTENDED: packet too short"));
        return;
    }

    const bool bTraderData = (R.ReadU8() != 0);
    R.ReadU32(); // unknown window/session id
    const uint32 RawSlotCount = R.ReadU32();
    const uint32 SlotCount = FMath::Min(RawSlotCount, 7u); // WoW 3.3.5a max 7 trade slots
    R.ReadU32(); // duplicated slot count
    const uint32 Money = R.ReadU32();
    const uint32 SpellId = R.ReadU32();

    TArray<FWowTradeItem> ParsedItems;
    ParsedItems.SetNum(static_cast<int32>(SlotCount));

    for (uint32 SlotIndex = 0; SlotIndex < SlotCount && R.Remaining() > 0; ++SlotIndex)
    {
        if (!R.CanRead(1))
        {
            break;
        }

        const uint8 TradeSlot = R.ReadU8();
        FWowTradeItem Item;
        Item.Slot = TradeSlot;
        Item.ItemId = R.ReadU32();
        Item.DisplayId = R.ReadU32();
        Item.Count = R.ReadU32();
        Item.bWrapped = (R.ReadU32() != 0);
        Item.GiftCreatorGuid = R.ReadU64();
        Item.PermanentEnchantId = R.ReadU32();
        Item.GemEnchantId1 = R.ReadU32();
        Item.GemEnchantId2 = R.ReadU32();
        Item.GemEnchantId3 = R.ReadU32();
        Item.CreatorGuid = R.ReadU64();
        Item.Charges = R.ReadU32();
        Item.SuffixFactor = R.ReadU32();
        Item.RandomPropertyId = static_cast<int32>(R.ReadU32());
        Item.LockId = R.ReadU32();
        Item.MaxDurability = R.ReadU32();
        Item.Durability = R.ReadU32();

        if (ParsedItems.IsValidIndex(TradeSlot))
        {
            ParsedItems[TradeSlot] = Item;
        }
    }

    CurrentTrade.bTradeOpen = true;
    CurrentTrade.Status = WowTradeStatus::OPEN_WINDOW;
    if (bTraderData)
    {
        CurrentTrade.TargetMoney = Money;
        CurrentTrade.TargetSpell = SpellId;
        CurrentTrade.TargetItems = MoveTemp(ParsedItems);
    }
    else
    {
        CurrentTrade.PlayerMoney = Money;
        CurrentTrade.PlayerSpell = SpellId;
        CurrentTrade.PlayerItems = MoveTemp(ParsedItems);
    }

    UE_LOG(LogWowPacket, Log, TEXT("TRADE_STATUS_EXTENDED: %s side updated (%u slots, money=%u)"),
        bTraderData ? TEXT("target") : TEXT("player"),
        SlotCount,
        Money);

    OnTradeUpdated.Broadcast(CurrentTrade);
}

// ── Bank system handlers ────────────────────────────────────────────

void FWowPacketHandler::HandleShowBank(FPacketReader& R)
{
    // SMSG_SHOW_BANK
    // uint64 bankerGuid

    if (!R.CanRead(8))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SHOW_BANK: packet too short"));
        return;
    }

    uint64 BankerGuid = R.ReadU64();

    UE_LOG(LogWowPacket, Log, TEXT("SHOW_BANK: Bank opened by banker GUID %llu"), BankerGuid);
    OnBankOpened.Broadcast(BankerGuid);
}

void FWowPacketHandler::HandleNpcText(FPacketReader& R)
{
    const uint32 TextId = R.ReadU32();
    FString Greeting;
    float BestProbability = -1.0f;
    for (int32 I = 0; I < 8; ++I)
    {
        const float Probability = R.ReadFloat();
        const FString FirstText = R.ReadCString();
        const FString SecondText = R.ReadCString();
        R.Skip(28); // language plus three (delay, emote) pairs
        if (R.HasFailed()) return;
        const FString& Candidate = FirstText.IsEmpty() ? SecondText : FirstText;
        if (!Candidate.IsEmpty() && (Greeting.IsEmpty() || Probability > BestProbability))
        {
            Greeting = Candidate;
            BestProbability = Probability;
        }
    }
    NpcTextCache.Add(TextId, Greeting);
    OnNpcText.Broadcast(TextId, Greeting);
}

void FWowPacketHandler::HandleLoginTimeSpeed(FPacketReader& R)
{
    if (R.Remaining() < 8) return;
    const uint32 PackedTime = R.ReadU32();
    const float Speed = R.ReadFloat();
    if (RealmClock.Synchronize(PackedTime, Speed, FPlatformTime::Seconds()))
        UE_LOG(LogWowPacket, Log, TEXT("Realm clock synchronized: %.0f minutes, speed %.8f game minutes/second"), RealmClock.MinutesAtSync, Speed);
}


FString FWowPacketHandler::ResolveGuildName(uint32 GuildId)
{
    if(!GuildId) return FString();
    if(const FString* Name=GuildNames.Find(GuildId)) return *Name;
    const double Now=FPlatformTime::Seconds();
    const double* Last=GuildQueryTimes.Find(GuildId);
    if(Now>=NextGuildNameQuery && (!Last || Now-*Last>30) && OnSendPacket.IsBound())
    {
        TArray<uint8> Data; for(int32 I=0;I<4;++I) Data.Add(uint8(GuildId>>(I*8)));
        GuildQueryTimes.Add(GuildId,Now); NextGuildNameQuery=Now+.2;
        OnSendPacket.Execute(0x0054,Data); // CMSG_GUILD_QUERY (12340): uint32 guild ID
    }
    return FString();
}
static TMap<uint32,uint32> GGuildTabardDesigns;
static uint32 GGuildTabardRevision=0;
uint32 FWowPacketHandler::GetGuildTabardDesign(uint32 GuildId) { return GGuildTabardDesigns.FindRef(GuildId); }
uint32 FWowPacketHandler::GetGuildTabardRevision() { return GGuildTabardRevision; }

void FWowPacketHandler::HandleGuildName(FPacketReader& R)
{
    if(!R.CanRead(5)) return;
    const uint32 Id=R.ReadU32(); const FString Name=R.ReadCString();
    if(Id && !Name.IsEmpty()) GuildNames.Add(Id,Name);
    // r343 (12340): 10 rank names, then EmblemStyle, EmblemColor, BorderStyle, BorderColor, BackgroundColor.
    for(int32 Rank=0;Rank<10;++Rank) { if(!R.CanRead(1)) return; R.ReadCString(); }
    if(!Id || !R.CanRead(20)) return;
    const uint32 EmblemStyle=R.ReadU32(), EmblemColor=R.ReadU32(), BorderStyle=R.ReadU32(), BorderColor=R.ReadU32(), Background=R.ReadU32();
    // A guild without a design reports 0xFFFFFFFF; keep it blank like the original client.
    // r551 (Stefan: a guild member's tabard stayed plain grey; log "background 32 (no design)"): background
    // colours run 0-50 in 3.3.5, but only 5 bits (0-31) were kept. New layout: bit 31 valid, emblem 21-28,
    // emblem colour 16-20, border style 12-15, border colour 7-11, background 0-6.
    const bool bDesigned=EmblemStyle<256 && EmblemColor<32 && BorderStyle<16 && BorderColor<32 && Background<128;
    const uint32 Packed=bDesigned ? (0x80000000u|((EmblemStyle&0xffu)<<21)|((EmblemColor&0x1fu)<<16)|((BorderStyle&0xfu)<<12)|((BorderColor&0x1fu)<<7)|(Background&0x7fu)) : 0u;
    if(GGuildTabardDesigns.FindRef(Id)!=Packed || !GGuildTabardDesigns.Contains(Id))
    {
        GGuildTabardDesigns.Add(Id,Packed); ++GGuildTabardRevision;
        UE_LOG(LogWowPacket,Log,TEXT("Guild %u tabard: emblem %u/%u border %u/%u background %u%s"),Id,EmblemStyle,EmblemColor,BorderStyle,BorderColor,Background,bDesigned?TEXT(""):TEXT(" (no design)"));
    }
}

void FWowPacketHandler::RecordSpellCooldown(uint32 Spell, float Duration)
{
    const double Now=FPlatformTime::Seconds();
    SpellCooldowns.Add(Spell,Now+Duration);
    SpellCooldownWindows.Add(Spell,{Now,Duration});
}

void FWowPacketHandler::StartGlobalCooldown(uint64 Caster,uint32 Spell,uint8 Counter)
{
    if (Caster != EntityManager.LocalPlayerGuid) return;
    const double Now=FPlatformTime::Seconds();
    // START and GO describe the same cast, including instant casts.
    if (LastGlobalSpell==Spell && LastGlobalCounter==Counter && Now-LastGlobalCast<60.) return;
    LastGlobalSpell=Spell; LastGlobalCounter=Counter; LastGlobalCast=Now;
    const auto* Info=FDbcStore::Get().Spells().GetById(Spell);
    if (!Info || !Info->StartRecoveryCategory || !Info->StartRecoveryTime) return;
    float Duration=Info->StartRecoveryTime/1000.f;
    if (Info->StartRecoveryCategory==133 && Info->StartRecoveryTime==1500 && Info->DmgClass<2 && !(Info->Attributes&0x10))
        if (const auto* Player=EntityManager.GetLocalPlayer())
        {
            // Build 12340 UNIT_MOD_CAST_SPEED is a server-supplied time multiplier.
            const float Multiplier=Player->GetFieldFloat(UnitField::BASE+0x004A);
            if (FMath::IsFinite(Multiplier) && Multiplier>0) Duration=FMath::Clamp(Duration*Multiplier,1.f,1.5f);
        }
    GlobalCooldownWindows.Add(Info->StartRecoveryCategory,{Now,Duration});
}

FWowPacketHandler::FCooldownWindow FWowPacketHandler::GetVisibleSpellCooldown(uint32 Spell) const
{
    const double Now=FPlatformTime::Seconds();
    FCooldownWindow Result;
    if (const auto* Own=SpellCooldownWindows.Find(Spell); Own && Own->Start+Own->Duration>Now) Result=*Own;
    if (const auto* Info=FDbcStore::Get().Spells().GetById(Spell))
        if (const auto* Global=GlobalCooldownWindows.Find(Info->StartRecoveryCategory);
            Global && Global->Start+Global->Duration>Now && Global->Start+Global->Duration>Result.Start+Result.Duration)
            Result=*Global;
    return Result;
}

// r598: game object template data per entry (type + data0..23), read by the transport code
// (WowGameplayControllerTransports.cpp) without a header change.
static TMap<uint32, TArray<uint32>> GWowGameObjectTemplates;
static void WowStoreGameObjectTemplate(uint32 Entry, uint32 Type, const uint32 (&Values)[24])
{
    TArray<uint32>& Data = GWowGameObjectTemplates.FindOrAdd(Entry);
    Data.SetNum(25);
    Data[0] = Type;
    for (int32 I = 0; I < 24; ++I) Data[I + 1] = Values[I];
}
WOWNETWORK_API const TArray<uint32>* WowFindGameObjectTemplate(uint32 Entry) { return GWowGameObjectTemplates.Find(Entry); }

void FWowPacketHandler::HandleGameObjectQueryResponse(FPacketReader& R)
{
    if (!R.CanRead(12)) return;
    const uint32 Entry = R.ReadU32();
    if (Entry & 0x80000000u) return;
    const uint32 Type = R.ReadU32();
    R.ReadU32(); // display id
    const FString Name = R.ReadCString();
    for (int I=1; I<7; ++I) R.ReadCString(); // three more names, icon, cast caption, unknown
    if (!Name.IsEmpty()) GameObjectNames.Add(Entry, Name);
    if (!R.CanRead(24*4)) return;
    uint32 Values[24];
    for (auto& Value : Values) Value = R.ReadU32();
    uint32 Lock = 0;
    if (Type==3 || Type==10) Lock=Values[0];
    else if (Type==0 || Type==1 || Type==2) Lock=Values[1];
    GameObjectLocks.Add(Entry, Lock);
    // r598: transports need their template data (type 15: data0 taxi path, data1 speed, data2 acceleration).
    WowStoreGameObjectTemplate(Entry, Type, Values);
    if (Type == 9) // GAMEOBJECT_TYPE_TEXT: data0 page, data1 language, data2 page material
    {
        GameObjectPageTexts.Add(Entry, Values[0]);
        GameObjectPageMaterials.Add(Entry, Values[2]);
    }
    if (OnGameObjectInfo) OnGameObjectInfo(Entry);
}

// r563: the server's dungeon/raid difficulty (uint32 difficulty, uint32 1, uint32 in group), sent at
// login, when it changes and when a group leader sets it. Dungeon portals show it (skull, colour).
void FWowPacketHandler::HandleSetDungeonDifficulty(FPacketReader& R)
{
    if (!R.CanRead(4)) return;
    const uint32 Difficulty = R.ReadU32();
    FWowDifficultyState::SetDungeon(int32(FMath::Min<uint32>(Difficulty, 1)));
    UE_LOG(LogWowPacket, Log, TEXT("Dungeon difficulty from server: %u"), Difficulty);
}

void FWowPacketHandler::HandleSetRaidDifficulty(FPacketReader& R)
{
    if (!R.CanRead(4)) return;
    const uint32 Difficulty = R.ReadU32();
    FWowDifficultyState::SetRaid(int32(FMath::Min<uint32>(Difficulty, 3)));
    UE_LOG(LogWowPacket, Log, TEXT("Raid difficulty from server: %u"), Difficulty);
}

// ── Readable gameobjects (plaques, books, notes) ────────────────────────────
// Stefan 10.10: right-clicking a plaque did nothing. The server answers CMSG_GAMEOBJ_USE with
// SMSG_GAMEOBJECT_PAGETEXT; the client queries the object's pages and shows ItemTextFrame.

void FWowPacketHandler::HandleGameObjectPageText(FPacketReader& R)
{
    if (!R.CanRead(8)) return;
    const uint64 Guid = R.ReadU64();
    const FWowEntity* Object = EntityManager.Find(Guid);
    const uint32 Entry = Object ? Object->Entry : 0;
    const uint32 FirstPage = GameObjectPageTexts.FindRef(Entry);
    UE_LOG(LogWowPacket, Log, TEXT("GAMEOBJECT_PAGETEXT: guid=%llu entry=%u page=%u"), Guid, Entry, FirstPage);
    if (!FirstPage) return;
    static const TCHAR* Materials[] = {TEXT("Parchment"), TEXT("Parchment"), TEXT("Stone"), TEXT("Marble"),
        TEXT("Silver"), TEXT("Bronze"), TEXT("Valentine"), TEXT("Illidan")};
    const uint32 Material = GameObjectPageMaterials.FindRef(Entry);
    ItemText = FWowItemTextState();
    ItemText.bOpen = true;
    ItemText.SourceGuid = Guid;
    ItemText.Title = GameObjectNames.FindRef(Entry);
    ItemText.Material = Material < UE_ARRAY_COUNT(Materials) ? Materials[Material] : TEXT("Parchment");
    ItemText.PageId = FirstPage;
    ItemText.History.Add(FirstPage);
    ItemText.bBeginPending = true;
    RequestPageText(FirstPage);
}

void FWowPacketHandler::RequestPageText(uint32 PageId)
{
    if (!PageId) return;
    if (PageTexts.Contains(PageId))
    {
        if (ItemText.bOpen && ItemText.PageId == PageId) ItemText.bReadyPending = true;
        return;
    }
    TArray<uint8> Data;
    Data.Append(reinterpret_cast<const uint8*>(&PageId), sizeof(PageId));
    Data.Append(reinterpret_cast<const uint8*>(&ItemText.SourceGuid), sizeof(ItemText.SourceGuid));
    OnSendPacket.ExecuteIfBound(WowOpcode::CMSG_PAGE_TEXT_QUERY, Data);
}

void FWowPacketHandler::HandlePageTextQueryResponse(FPacketReader& R)
{
    if (!R.CanRead(4)) return;
    const uint32 PageId = R.ReadU32();
    FWowPageText Page;
    Page.Text = R.ReadCString();
    Page.NextPage = R.CanRead(4) ? R.ReadU32() : 0;
    // Page texts mark line breaks with $B.
    Page.Text.ReplaceInline(TEXT("$B"), TEXT("\n"), ESearchCase::IgnoreCase);
    UE_LOG(LogWowPacket, Log, TEXT("PAGE_TEXT_QUERY_RESPONSE: page=%u next=%u chars=%d"), PageId, Page.NextPage, Page.Text.Len());
    PageTexts.Add(PageId, MoveTemp(Page));
    if (ItemText.bOpen && ItemText.PageId == PageId) ItemText.bReadyPending = true;
}

void FWowPacketHandler::ItemTextTurnPage(int32 Delta)
{
    if (!ItemText.bOpen) return;
    if (Delta > 0)
    {
        const FWowPageText* Page = PageTexts.Find(ItemText.PageId);
        if (!Page || !Page->NextPage) return;
        ItemText.PageId = Page->NextPage;
        ItemText.History.Add(ItemText.PageId);
    }
    else
    {
        if (ItemText.History.Num() < 2) return;
        ItemText.History.Pop();
        ItemText.PageId = ItemText.History.Last();
    }
    RequestPageText(ItemText.PageId);
}

void FWowPacketHandler::CloseItemText()
{
    if (!ItemText.bOpen) return;
    ItemText.bOpen = false;
    ItemText.bClosedPending = true;
}
