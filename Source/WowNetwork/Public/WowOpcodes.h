#pragma once
#include "CoreMinimal.h"

// WoW 3.3.5a (build 12340) opcodes — subset used by the client
// Reference: azerothcore-wotlk/src/server/game/Server/Protocol/Opcodes.h
namespace WowOpcode
{
    inline constexpr uint16 CMSG_GAMEOBJECT_QUERY = 0x005E;
    inline constexpr uint16 SMSG_GAMEOBJECT_QUERY_RESPONSE = 0x005F;
    // Readable gameobjects (plaques, books): the server answers CMSG_GAMEOBJ_USE with PAGETEXT, the
    // client then queries the pages.
    inline constexpr uint16 CMSG_PAGE_TEXT_QUERY = 0x005A;            // uint32 pageId, uint64 guid
    inline constexpr uint16 SMSG_PAGE_TEXT_QUERY_RESPONSE = 0x005B;   // uint32 pageId, cstring text, uint32 nextPageId
    inline constexpr uint16 SMSG_GAMEOBJECT_PAGETEXT = 0x01DF;        // uint64 guid
    inline constexpr uint16 SMSG_GAMEOBJECT_CUSTOM_ANIM  = 0x00B3; // uint64 guid, uint32 anim (fishing bobber splash)
    inline constexpr uint16 SMSG_ITEM_ENCHANT_TIME_UPDATE = 0x01EB;
    inline constexpr uint16 SMSG_LOGIN_SETTIMESPEED = 0x0042;
    // ── Client → Server ──────────────────────────────────────────────────────
    inline constexpr uint16 CMSG_CHAR_CREATE            = 0x0036;
    inline constexpr uint16 CMSG_CHAR_ENUM              = 0x0037;
    inline constexpr uint16 CMSG_CHAR_DELETE            = 0x0038;
    inline constexpr uint16 CMSG_PLAYER_LOGIN           = 0x003D;
    inline constexpr uint16 CMSG_LOGOUT_REQUEST         = 0x004B;
    inline constexpr uint16 CMSG_LOGOUT_CANCEL          = 0x004E;
    inline constexpr uint16 CMSG_SET_SELECTION            = 0x013D;
    inline constexpr uint16 CMSG_MESSAGECHAT            = 0x0095;
    inline constexpr uint16 CMSG_CAST_SPELL             = 0x012E;
    inline constexpr uint16 CMSG_CANCEL_CAST            = 0x012F;
    inline constexpr uint16 CMSG_CANCEL_CHANNELLING     = 0x013B;
    inline constexpr uint16 CMSG_ATTACKSWING            = 0x0141;
    inline constexpr uint16 CMSG_ATTACKSTOP             = 0x0142;
    inline constexpr uint16 SMSG_ATTACKSTART            = 0x0143;
    inline constexpr uint16 SMSG_ATTACKSTOP             = 0x0144;
    inline constexpr uint16 CMSG_STANDSTATECHANGE       = 0x0101;
    inline constexpr uint16 CMSG_TEXT_EMOTE             = 0x0104;
    inline constexpr uint16 CMSG_SET_ACTION_BUTTON      = 0x0128;
    inline constexpr uint16 CMSG_SET_SHEATHED            = 0x01E0;
    inline constexpr uint16 CMSG_AUTH_SESSION            = 0x01ED;
    inline constexpr uint16 CMSG_SET_ACTIVE_MOVER       = 0x026A;
    inline constexpr uint16 CMSG_MOVE_TIME_SKIPPED      = 0x02CE;
    inline constexpr uint16 CMSG_TIME_SYNC_RESP          = 0x0391;
    inline constexpr uint16 CMSG_KEEP_ALIVE             = 0x0407;
    inline constexpr uint16 CMSG_WORLD_STATE_UI_TIMER_UPDATE = 0x04F6;
    inline constexpr uint16 CMSG_WARDEN_DATA             = 0x02E7;
    // r240: calendar (Blizzard_Calendar)
    inline constexpr uint16 CMSG_CALENDAR_GET_CALENDAR   = 0x0429;
    inline constexpr uint16 CMSG_CALENDAR_GET_NUM_PENDING = 0x0447;

    // Wrath 3.3.5a Dungeon Finder and Looking For Raid browser
    inline constexpr uint16 CMSG_LFG_JOIN                = 0x035C;
    inline constexpr uint16 CMSG_LFG_LEAVE               = 0x035D;
    inline constexpr uint16 CMSG_SEARCH_LFG_JOIN         = 0x035E;
    inline constexpr uint16 CMSG_SEARCH_LFG_LEAVE        = 0x035F;
    inline constexpr uint16 CMSG_LFG_PROPOSAL_RESULT     = 0x0362;
    inline constexpr uint16 CMSG_SET_LFG_COMMENT         = 0x0366;
    inline constexpr uint16 CMSG_LFG_SET_ROLES           = 0x036A;
    inline constexpr uint16 CMSG_LFG_SET_NEEDS           = 0x036B;
    inline constexpr uint16 CMSG_LFG_SET_BOOT_VOTE       = 0x036C;
    inline constexpr uint16 CMSG_LFD_PLAYER_LOCK_INFO_REQUEST = 0x036E;
    inline constexpr uint16 CMSG_LFG_TELEPORT            = 0x0370;
    inline constexpr uint16 CMSG_LFD_PARTY_LOCK_INFO_REQUEST = 0x0371;
    inline constexpr uint16 CMSG_LFG_GET_STATUS          = 0x0296;

    // ── Movement (bidirectional MSG) ─────────────────────────────────────────
    inline constexpr uint16 MSG_MOVE_START_FORWARD       = 0x00B5;
    inline constexpr uint16 MSG_MOVE_START_BACKWARD      = 0x00B6;
    inline constexpr uint16 MSG_MOVE_STOP                = 0x00B7;
    inline constexpr uint16 MSG_MOVE_START_STRAFE_LEFT   = 0x00B8;
    inline constexpr uint16 MSG_MOVE_START_STRAFE_RIGHT  = 0x00B9;
    inline constexpr uint16 MSG_MOVE_STOP_STRAFE         = 0x00BA;
    inline constexpr uint16 MSG_MOVE_JUMP                = 0x00BB;
    inline constexpr uint16 MSG_MOVE_START_TURN_LEFT     = 0x00BC;
    inline constexpr uint16 MSG_MOVE_START_TURN_RIGHT    = 0x00BD;
    inline constexpr uint16 MSG_MOVE_STOP_TURN           = 0x00BE;
    inline constexpr uint16 MSG_MOVE_START_PITCH_UP      = 0x00BF;
    inline constexpr uint16 MSG_MOVE_START_PITCH_DOWN    = 0x00C0;
    inline constexpr uint16 MSG_MOVE_STOP_PITCH          = 0x00C1;
    inline constexpr uint16 MSG_MOVE_SET_RUN_MODE        = 0x00C2;
    inline constexpr uint16 MSG_MOVE_SET_WALK_MODE       = 0x00C3;
    inline constexpr uint16 MSG_MOVE_TELEPORT            = 0x00C5;
    inline constexpr uint16 MSG_MOVE_TELEPORT_ACK        = 0x00C7;
    inline constexpr uint16 MSG_MOVE_FALL_LAND           = 0x00C9;
    inline constexpr uint16 MSG_MOVE_START_SWIM          = 0x00CA;
    inline constexpr uint16 MSG_MOVE_STOP_SWIM           = 0x00CB;
    inline constexpr uint16 MSG_MOVE_SET_RUN_SPEED       = 0x00CD;
    inline constexpr uint16 MSG_MOVE_SET_RUN_BACK_SPEED  = 0x00CF;
    inline constexpr uint16 MSG_MOVE_SET_WALK_SPEED      = 0x00D1;
    inline constexpr uint16 MSG_MOVE_SET_SWIM_SPEED      = 0x00D3;
    inline constexpr uint16 MSG_MOVE_SET_SWIM_BACK_SPEED = 0x00D5;
    inline constexpr uint16 MSG_MOVE_SET_TURN_RATE       = 0x00D8;
    inline constexpr uint16 MSG_MOVE_SET_FACING          = 0x00DA;
    inline constexpr uint16 MSG_MOVE_SET_PITCH           = 0x00DB;
    inline constexpr uint16 MSG_MOVE_WORLDPORT_ACK       = 0x00DC;
    inline constexpr uint16 MSG_MOVE_ROOT                = 0x00EC;
    inline constexpr uint16 MSG_MOVE_UNROOT              = 0x00ED;
    inline constexpr uint16 MSG_MOVE_HEARTBEAT           = 0x00EE;

    // ── Server → Client ──────────────────────────────────────────────────────
    inline constexpr uint16 SMSG_MESSAGECHAT             = 0x0096;
    inline constexpr uint16 SMSG_LOGOUT_RESPONSE         = 0x004C;
    inline constexpr uint16 SMSG_LOGOUT_COMPLETE         = 0x004D;
    inline constexpr uint16 SMSG_LOGOUT_CANCEL_ACK       = 0x004F;
    inline constexpr uint16 SMSG_UPDATE_OBJECT           = 0x00A9;
    inline constexpr uint16 SMSG_DESTROY_OBJECT          = 0x00AA;
    inline constexpr uint16 SMSG_MONSTER_MOVE            = 0x00DD;
    inline constexpr uint16 SMSG_EMOTE                   = 0x0103;
    inline constexpr uint16 SMSG_TEXT_EMOTE              = 0x0105;
    inline constexpr uint16 SMSG_AI_REACTION             = 0x013C;
    inline constexpr uint16 SMSG_PLAY_MUSIC              = 0x0277;
    inline constexpr uint16 SMSG_PLAY_OBJECT_SOUND       = 0x0278;
    inline constexpr uint16 SMSG_PLAY_SOUND              = 0x02D2;
    inline constexpr uint16 SMSG_TUTORIAL_FLAGS          = 0x00FD;
    inline constexpr uint16 SMSG_ACTION_BUTTONS          = 0x0129;
    inline constexpr uint16 SMSG_INITIAL_SPELLS          = 0x012A;
    inline constexpr uint16 SMSG_SET_PROFICIENCY         = 0x0127;
    inline constexpr uint16 SMSG_SPELL_START             = 0x0131;
    inline constexpr uint16 SMSG_SPELL_GO                = 0x0132;
    inline constexpr uint16 MSG_CHANNEL_START            = 0x0139;
    inline constexpr uint16 MSG_CHANNEL_UPDATE           = 0x013A;
    inline constexpr uint16 SMSG_SPELL_DELAYED           = 0x01E2;
    inline constexpr uint16 SMSG_AUTH_CHALLENGE          = 0x01EC;
    inline constexpr uint16 SMSG_AUTH_RESPONSE           = 0x01EE;
    inline constexpr uint16 SMSG_COMPRESSED_UPDATE_OBJECT = 0x01F6;
    inline constexpr uint16 SMSG_ACCOUNT_DATA_TIMES      = 0x0209;
    inline constexpr uint16 SMSG_LOGIN_VERIFY_WORLD      = 0x0236;
    inline constexpr uint16 SMSG_ADDON_INFO              = 0x02EF;
    inline constexpr uint16 SMSG_WARDEN_DATA             = 0x02E6;
    inline constexpr uint16 SMSG_CALENDAR_SEND_CALENDAR  = 0x0436; // r240
    inline constexpr uint16 SMSG_CALENDAR_SEND_NUM_PENDING = 0x0448; // r240
    inline constexpr uint16 SMSG_MOTD                    = 0x033D;
    inline constexpr uint16 SMSG_TIME_SYNC_REQ           = 0x0390;
    inline constexpr uint16 SMSG_POWER_UPDATE            = 0x0480;
    inline constexpr uint16 SMSG_AURA_UPDATE_ALL         = 0x0495;
    inline constexpr uint16 SMSG_AURA_UPDATE             = 0x0496;

    // Wrath 3.3.5a Dungeon Finder and Looking For Raid browser
    inline constexpr uint16 SMSG_UPDATE_LFG_LIST         = 0x0360;
    inline constexpr uint16 SMSG_LFG_PROPOSAL_UPDATE     = 0x0361;
    inline constexpr uint16 SMSG_LFG_ROLE_CHECK_UPDATE   = 0x0363;
    inline constexpr uint16 SMSG_LFG_JOIN_RESULT         = 0x0364;
    inline constexpr uint16 SMSG_LFG_QUEUE_STATUS        = 0x0365;
    inline constexpr uint16 SMSG_LFG_UPDATE_PLAYER       = 0x0367;
    inline constexpr uint16 SMSG_LFG_UPDATE_PARTY        = 0x0368;
    inline constexpr uint16 SMSG_LFG_UPDATE_SEARCH       = 0x0369;
    inline constexpr uint16 SMSG_LFG_BOOT_PROPOSAL_UPDATE = 0x036D;
    inline constexpr uint16 SMSG_LFG_PLAYER_INFO         = 0x036F;
    inline constexpr uint16 SMSG_LFG_PARTY_INFO          = 0x0372;
    inline constexpr uint16 SMSG_LFG_ROLE_CHOSEN         = 0x02BB;
    inline constexpr uint16 SMSG_LFG_OFFER_CONTINUE      = 0x0293;
    inline constexpr uint16 SMSG_LFG_DISABLED            = 0x0398;
    inline constexpr uint16 SMSG_ATTACKERSTATEUPDATE     = 0x014A;
    inline constexpr uint16 SMSG_CAST_FAILED             = 0x0130;
    inline constexpr uint16 SMSG_SPELL_FAILURE           = 0x0133;
    inline constexpr uint16 SMSG_SPELL_COOLDOWN          = 0x0134;
    inline constexpr uint16 SMSG_COOLDOWN_EVENT          = 0x0135;
    inline constexpr uint16 SMSG_CHAR_CREATE              = 0x003A;
    inline constexpr uint16 SMSG_CHAR_ENUM               = 0x003B;
    inline constexpr uint16 SMSG_CHAR_DELETE              = 0x003C;
    inline constexpr uint16 SMSG_TRANSFER_PENDING        = 0x003F;
    inline constexpr uint16 SMSG_NEW_WORLD               = 0x003E;

    // Player progression opcodes
    inline constexpr uint16 SMSG_LEVELUP_INFO            = 0x01D4;
    inline constexpr uint16 SMSG_LOG_XPGAIN              = 0x01D0;
    inline constexpr uint16 SMSG_EXPLORATION_EXPERIENCE  = 0x01F8;
    inline constexpr uint16 SMSG_ENVIRONMENTAL_DAMAGE_LOG = 0x01FC;
    // Combat log (build 12340)
    inline constexpr uint16 SMSG_SPELLHEALLOG            = 0x0150;
    inline constexpr uint16 SMSG_SPELLENERGIZELOG        = 0x0151;
    inline constexpr uint16 SMSG_PARTYKILLLOG            = 0x01F5;
    inline constexpr uint16 SMSG_SPELLLOGMISS            = 0x024B;
    inline constexpr uint16 SMSG_PERIODICAURALOG         = 0x024E;
    inline constexpr uint16 SMSG_SPELLDAMAGESHIELD       = 0x024F;
    inline constexpr uint16 SMSG_SPELLNONMELEEDAMAGELOG  = 0x0250;
	inline constexpr uint16 SMSG_START_MIRROR_TIMER       = 0x01D9;
	inline constexpr uint16 SMSG_PAUSE_MIRROR_TIMER       = 0x01DA;
	inline constexpr uint16 SMSG_STOP_MIRROR_TIMER        = 0x01DB;
    inline constexpr uint16 SMSG_BINDPOINTUPDATE         = 0x0155;
    inline constexpr uint16 SMSG_PLAYED_TIME             = 0x01CD;

    // World state opcodes
    inline constexpr uint16 SMSG_WEATHER                 = 0x02F4;
    inline constexpr uint16 SMSG_INIT_WORLD_STATES       = 0x02C2;
    inline constexpr uint16 SMSG_UPDATE_WORLD_STATE      = 0x02C3;

    // Party opcodes
    inline constexpr uint16 SMSG_PARTY_MEMBER_STATS      = 0x007E;

    // Inventory system opcodes
    inline constexpr uint16 CMSG_AUTOEQUIP_ITEM          = 0x010A;
    inline constexpr uint16 CMSG_AUTOSTORE_BAG_ITEM      = 0x010B;
    inline constexpr uint16 CMSG_BUYBACK_ITEM = 0x0290;
    inline constexpr uint16 CMSG_SWAP_ITEM               = 0x010C;
    inline constexpr uint16 CMSG_SWAP_INV_ITEM           = 0x010D;
    inline constexpr uint16 CMSG_SPLIT_ITEM              = 0x010E;
    inline constexpr uint16 CMSG_AUTOSTORE_LOOT_ITEM     = 0x0108;
    inline constexpr uint16 CMSG_DESTROYITEM             = 0x0111;
    inline constexpr uint16 SMSG_INVENTORY_CHANGE_FAILURE = 0x0112;
    inline constexpr uint16 CMSG_LOOT                    = 0x015D;
    inline constexpr uint16 CMSG_LOOT_MONEY              = 0x015E;
    inline constexpr uint16 CMSG_LOOT_RELEASE            = 0x015F;
    inline constexpr uint16 SMSG_LOOT_REMOVED             = 0x0162;
    inline constexpr uint16 SMSG_LOOT_CLEAR_MONEY         = 0x0165;
    inline constexpr uint16 SMSG_LOOT_RESPONSE           = 0x0160;
    inline constexpr uint16 SMSG_LOOT_RELEASE_RESPONSE   = 0x0161;
    inline constexpr uint16 SMSG_ITEM_PUSH_RESULT        = 0x0166;

    // Trade opcodes
    inline constexpr uint16 CMSG_INITIATE_TRADE          = 0x0116;
    inline constexpr uint16 CMSG_BEGIN_TRADE             = 0x0117;
    inline constexpr uint16 CMSG_BUSY_TRADE              = 0x0118;
    inline constexpr uint16 CMSG_IGNORE_TRADE            = 0x0119;
    inline constexpr uint16 CMSG_ACCEPT_TRADE            = 0x011A;
    inline constexpr uint16 CMSG_UNACCEPT_TRADE          = 0x011B;
    inline constexpr uint16 CMSG_CANCEL_TRADE            = 0x011C;
    inline constexpr uint16 CMSG_SET_TRADE_ITEM          = 0x011D;
    inline constexpr uint16 CMSG_CLEAR_TRADE_ITEM        = 0x011E;
    inline constexpr uint16 CMSG_SET_TRADE_GOLD          = 0x011F;
    inline constexpr uint16 SMSG_TRADE_STATUS            = 0x0120;
    inline constexpr uint16 SMSG_TRADE_STATUS_EXTENDED   = 0x0121;

    // Duel opcodes
    inline constexpr uint16 SMSG_DUEL_REQUESTED          = 0x0167;
    inline constexpr uint16 SMSG_DUEL_OUTOFBOUNDS        = 0x0168;
    inline constexpr uint16 SMSG_DUEL_INBOUNDS           = 0x0169;
    inline constexpr uint16 SMSG_DUEL_COMPLETE           = 0x016A;
    inline constexpr uint16 SMSG_DUEL_WINNER             = 0x016B;
    inline constexpr uint16 CMSG_DUEL_ACCEPTED           = 0x016C;
    inline constexpr uint16 CMSG_DUEL_CANCELLED          = 0x016D;
    inline constexpr uint16 SMSG_DUEL_COUNTDOWN          = 0x02B7;

    // Pet opcodes
    inline constexpr uint16 CMSG_PET_SET_ACTION          = 0x0174;
    inline constexpr uint16 CMSG_PET_ACTION              = 0x0175;
    inline constexpr uint16 SMSG_PET_SPELLS              = 0x0179;
    inline constexpr uint16 SMSG_PET_MODE                = 0x017A;
    inline constexpr uint16 CMSG_PET_CAST_SPELL          = 0x01F0;
    inline constexpr uint16 CMSG_REQUEST_PET_INFO        = 0x0279;
    inline constexpr uint16 SMSG_PET_ACTION_FEEDBACK     = 0x02C6;
    inline constexpr uint16 CMSG_PET_STOP_ATTACK         = 0x02EA;
    inline constexpr uint16 CMSG_PET_SPELL_AUTOCAST      = 0x02F3;

    // Vendor opcodes
    inline constexpr uint16 CMSG_ITEM_QUERY_SINGLE       = 0x0056;
    inline constexpr uint16 SMSG_ITEM_QUERY_SINGLE_RESPONSE = 0x0058;
    inline constexpr uint16 SMSG_BUY_FAILED               = 0x01A5;
    inline constexpr uint16 CMSG_LIST_INVENTORY          = 0x019E;
    inline constexpr uint16 SMSG_LIST_INVENTORY          = 0x019F;
    inline constexpr uint16 CMSG_SELL_ITEM               = 0x01A0;
    inline constexpr uint16 CMSG_REPAIR_ITEM             = 0x02A8;
    inline constexpr uint16 SMSG_SELL_ITEM               = 0x01A1;
    inline constexpr uint16 CMSG_BUY_ITEM                = 0x01A2;
    inline constexpr uint16 SMSG_BUY_ITEM                = 0x01A4;

    // NPC interaction opcodes
    inline constexpr uint16 CMSG_GOSSIP_HELLO            = 0x017B;
    inline constexpr uint16 SMSG_GOSSIP_MESSAGE          = 0x017D;
    inline constexpr uint16 CMSG_GOSSIP_SELECT_OPTION    = 0x017C;
    inline constexpr uint16 SMSG_GOSSIP_COMPLETE         = 0x017E;

    inline constexpr uint16 CMSG_NPC_TEXT_QUERY          = 0x017F;
    inline constexpr uint16 SMSG_NPC_TEXT_UPDATE         = 0x0180;

    // Quest system opcodes
    inline constexpr uint16 CMSG_QUESTGIVER_STATUS_QUERY = 0x0182;
    inline constexpr uint16 SMSG_QUESTGIVER_STATUS       = 0x0183;
    inline constexpr uint16 CMSG_QUESTGIVER_HELLO        = 0x0184;
    inline constexpr uint16 SMSG_QUESTGIVER_QUEST_LIST   = 0x0185;
    inline constexpr uint16 CMSG_QUESTGIVER_QUERY_QUEST  = 0x0186;
    inline constexpr uint16 SMSG_QUESTGIVER_QUEST_DETAILS = 0x0188;
    inline constexpr uint16 CMSG_QUESTGIVER_ACCEPT_QUEST = 0x0189;
    inline constexpr uint16 CMSG_QUESTGIVER_COMPLETE_QUEST = 0x018A;
    inline constexpr uint16 SMSG_QUESTGIVER_REQUEST_ITEMS = 0x018B;
    inline constexpr uint16 CMSG_QUESTGIVER_REQUEST_REWARD = 0x018C;
    inline constexpr uint16 SMSG_QUESTGIVER_OFFER_REWARD = 0x018D;
    inline constexpr uint16 CMSG_QUESTGIVER_CHOOSE_REWARD = 0x018E;
    inline constexpr uint16 SMSG_QUESTGIVER_QUEST_COMPLETE = 0x0191;
    inline constexpr uint16 SMSG_QUESTLOG_FULL           = 0x0195;
    inline constexpr uint16 SMSG_QUEST_UPDATE_COMPLETE   = 0x0198;
    inline constexpr uint16 SMSG_QUEST_UPDATE_ADD_KILL   = 0x0199;
    inline constexpr uint16 CMSG_QUEST_QUERY             = 0x005C;
    inline constexpr uint16 SMSG_QUEST_QUERY_RESPONSE    = 0x005D;
    inline constexpr uint16 CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY = 0x0417;
    inline constexpr uint16 SMSG_QUESTGIVER_STATUS_MULTIPLE = 0x0418;

    // Talent system opcodes
    inline constexpr uint16 SMSG_LEARNED_SPELL           = 0x012B;
    inline constexpr uint16 SMSG_REMOVED_SPELL           = 0x0203;
    inline constexpr uint16 CMSG_LEARN_TALENT            = 0x0251;
    inline constexpr uint16 SMSG_TALENTS_INFO            = 0x04C0;
    inline constexpr uint16 CMSG_LEARN_TALENTS_MULTIPLE  = 0x04C1;

    // ── Social / Guild / Friends ────────────────────────────────────────────────
    inline constexpr uint16 SMSG_FRIEND_LIST             = 0x0067;
    inline constexpr uint16 SMSG_FRIEND_STATUS           = 0x0068;
    inline constexpr uint16 CMSG_FRIEND_LIST             = 0x0066;
    inline constexpr uint16 CMSG_ADD_FRIEND              = 0x0069;
    inline constexpr uint16 CMSG_DEL_FRIEND              = 0x006A;
    inline constexpr uint16 CMSG_ADD_IGNORE              = 0x006C;
    inline constexpr uint16 CMSG_DEL_IGNORE              = 0x006D;
    inline constexpr uint16 SMSG_IGNORE_LIST             = 0x006B;
    inline constexpr uint16 SMSG_GUILD_ROSTER            = 0x008A;
    inline constexpr uint16 SMSG_GUILD_EVENT             = 0x0092;
    inline constexpr uint16 CMSG_GUILD_ROSTER            = 0x0089;
    inline constexpr uint16 SMSG_CHANNEL_NOTIFY          = 0x0099;
    inline constexpr uint16 SMSG_WHO                     = 0x0063;
    inline constexpr uint16 CMSG_WHO                     = 0x0062;
    inline constexpr uint16 SMSG_GROUP_LIST              = 0x007D;
    inline constexpr uint16 SMSG_PARTY_COMMAND_RESULT    = 0x007F;
    inline constexpr uint16 SMSG_NAME_QUERY_RESPONSE     = 0x0051;
    inline constexpr uint16 SMSG_CREATURE_QUERY_RESPONSE = 0x0061;
    inline constexpr uint16 CMSG_GROUP_INVITE            = 0x006E;
    inline constexpr uint16 SMSG_GROUP_INVITE            = 0x006F;
    inline constexpr uint16 CMSG_GROUP_ACCEPT            = 0x0072;
    inline constexpr uint16 CMSG_GROUP_DECLINE           = 0x0073;
    inline constexpr uint16 CMSG_GROUP_UNINVITE          = 0x0075;
    inline constexpr uint16 CMSG_GROUP_DISBAND           = 0x007B;
    inline constexpr uint16 SMSG_RAID_GROUP_ONLY         = 0x0286;
    inline constexpr uint16 MSG_RAID_TARGET_UPDATE       = 0x0321;
    inline constexpr uint16 MSG_RAID_READY_CHECK         = 0x0322;
    inline constexpr uint16 MSG_RAID_READY_CHECK_CONFIRM = 0x03AE;
    inline constexpr uint16 MSG_RAID_READY_CHECK_FINISHED = 0x03C6;
    inline constexpr uint16 SMSG_RAID_READY_CHECK_ERROR  = 0x0408;
    inline constexpr uint16 CMSG_NAME_QUERY              = 0x0050;
    inline constexpr uint16 CMSG_GET_MIRRORIMAGE_DATA    = 0x0400; // uint64 guid
    inline constexpr uint16 SMSG_MIRRORIMAGE_DATA        = 0x0401; // appearance + 11 item display ids
    inline constexpr uint16 CMSG_CREATURE_QUERY          = 0x0060;

    // ── Taxi / Flight Path System ───────────────────────────────────────────
    inline constexpr uint16 SMSG_SHOWTAXINODES          = 0x01A9;
    inline constexpr uint16 SMSG_ACTIVATETAXIREPLY      = 0x01AE;
    inline constexpr uint16 CMSG_ACTIVATETAXI           = 0x01AD;
    inline constexpr uint16 CMSG_ACTIVATETAXIEXPRESS    = 0x0312; // guid, uint32 count, count x uint32 node
    inline constexpr uint16 SMSG_NEW_TAXI_PATH          = 0x01AF;

    // ── Death / Corpse / Resurrection ────────────────────────────────────────
    inline constexpr uint16 CMSG_REPOP_REQUEST          = 0x015A;
    inline constexpr uint16 SMSG_CORPSE_RECLAIM_DELAY   = 0x0269;
    inline constexpr uint16 SMSG_RESURRECT_REQUEST      = 0x015B;
    inline constexpr uint16 CMSG_RESURRECT_RESPONSE     = 0x015C;

    // ── Mail System ─────────────────────────────────────────────────────────
    inline constexpr uint16 CMSG_SEND_MAIL             = 0x0238;
    inline constexpr uint16 CMSG_GET_MAIL_LIST         = 0x023A;
    inline constexpr uint16 SMSG_MAIL_LIST_RESULT      = 0x023B;
    inline constexpr uint16 SMSG_SEND_MAIL_RESULT      = 0x0239;
    inline constexpr uint16 CMSG_MAIL_TAKE_MONEY       = 0x0245;
    inline constexpr uint16 CMSG_MAIL_TAKE_ITEM        = 0x0246;
    inline constexpr uint16 CMSG_MAIL_MARK_AS_READ     = 0x0247;
    inline constexpr uint16 CMSG_MAIL_RETURN_TO_SENDER = 0x0248;
    inline constexpr uint16 CMSG_MAIL_DELETE           = 0x0249;
    inline constexpr uint16 SMSG_RECEIVED_MAIL         = 0x0285;
    inline constexpr uint16 SMSG_SHOW_MAILBOX          = 0x0297;

    // ── Quest POI (world map blobs / numbered circles) ──────────────────────
    inline constexpr uint16 CMSG_QUEST_POI_QUERY           = 0x01E3;
    inline constexpr uint16 SMSG_QUEST_POI_QUERY_RESPONSE  = 0x01E4;

    // ── GameObject Interaction ─────────────────────────────────────────────
    inline constexpr uint16 CMSG_GAMEOBJ_USE           = 0x00B1;

    // ── Cinematics (r377: race intro flyby) ────────────────────────────────
    inline constexpr uint16 SMSG_TRIGGER_CINEMATIC     = 0x00FA; // uint32 CinematicSequences id
    inline constexpr uint16 CMSG_NEXT_CINEMATIC_CAMERA = 0x00FB;
    inline constexpr uint16 CMSG_COMPLETE_CINEMATIC    = 0x00FC;

    // ── Bank System ────────────────────────────────────────────────────────
    inline constexpr uint16 CMSG_BANKER_ACTIVATE       = 0x01B7;
    inline constexpr uint16 SMSG_SHOW_BANK             = 0x01B8;

    // Auth result codes
    inline constexpr uint8 AUTH_OK = 0x0C;

    // Helper to check if an opcode is a movement MSG
    inline bool IsMovementOpcode(uint16 Op)
    {
        return (Op >= MSG_MOVE_START_FORWARD && Op <= MSG_MOVE_SET_PITCH)
            || Op == MSG_MOVE_ROOT || Op == MSG_MOVE_UNROOT || Op == MSG_MOVE_HEARTBEAT;
    }

    // Opcode name for logging
    const TCHAR* GetName(uint16 Opcode);
}
