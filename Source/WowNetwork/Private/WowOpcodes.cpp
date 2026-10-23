#include "WowOpcodes.h"

const TCHAR* WowOpcode::GetName(uint16 Opcode)
{
    switch (Opcode)
    {
    case SMSG_UPDATE_OBJECT:            return TEXT("SMSG_UPDATE_OBJECT");
    case SMSG_COMPRESSED_UPDATE_OBJECT: return TEXT("SMSG_COMPRESSED_UPDATE_OBJECT");
    case SMSG_DESTROY_OBJECT:           return TEXT("SMSG_DESTROY_OBJECT");
    case SMSG_LOGIN_VERIFY_WORLD:       return TEXT("SMSG_LOGIN_VERIFY_WORLD");
    case SMSG_MESSAGECHAT:              return TEXT("SMSG_MESSAGECHAT");
    case SMSG_INITIAL_SPELLS:           return TEXT("SMSG_INITIAL_SPELLS");
    case SMSG_ACTION_BUTTONS:           return TEXT("SMSG_ACTION_BUTTONS");
    case SMSG_AUTH_CHALLENGE:           return TEXT("SMSG_AUTH_CHALLENGE");
    case SMSG_AUTH_RESPONSE:            return TEXT("SMSG_AUTH_RESPONSE");
    case SMSG_CHAR_ENUM:                return TEXT("SMSG_CHAR_ENUM");
    case SMSG_MOTD:                     return TEXT("SMSG_MOTD");
    case SMSG_MAIL_LIST_RESULT:         return TEXT("SMSG_MAIL_LIST_RESULT");
    case SMSG_SHOW_MAILBOX:             return TEXT("SMSG_SHOW_MAILBOX");
    case SMSG_SEND_MAIL_RESULT:         return TEXT("SMSG_SEND_MAIL_RESULT");
    case SMSG_RECEIVED_MAIL:            return TEXT("SMSG_RECEIVED_MAIL");
    case SMSG_SHOW_BANK:                return TEXT("SMSG_SHOW_BANK");
    case SMSG_TRADE_STATUS:             return TEXT("SMSG_TRADE_STATUS");
    case SMSG_TRADE_STATUS_EXTENDED:    return TEXT("SMSG_TRADE_STATUS_EXTENDED");
    case SMSG_DUEL_REQUESTED:           return TEXT("SMSG_DUEL_REQUESTED");
    case SMSG_DUEL_OUTOFBOUNDS:         return TEXT("SMSG_DUEL_OUTOFBOUNDS");
    case SMSG_DUEL_INBOUNDS:            return TEXT("SMSG_DUEL_INBOUNDS");
    case SMSG_DUEL_COMPLETE:            return TEXT("SMSG_DUEL_COMPLETE");
    case SMSG_DUEL_WINNER:              return TEXT("SMSG_DUEL_WINNER");
    case SMSG_DUEL_COUNTDOWN:           return TEXT("SMSG_DUEL_COUNTDOWN");
	case SMSG_START_MIRROR_TIMER:      return TEXT("SMSG_START_MIRROR_TIMER");
	case SMSG_PAUSE_MIRROR_TIMER:      return TEXT("SMSG_PAUSE_MIRROR_TIMER");
	case SMSG_STOP_MIRROR_TIMER:       return TEXT("SMSG_STOP_MIRROR_TIMER");
    case SMSG_PET_SPELLS:               return TEXT("SMSG_PET_SPELLS");
    case SMSG_PET_MODE:                 return TEXT("SMSG_PET_MODE");
    case SMSG_PET_ACTION_FEEDBACK:      return TEXT("SMSG_PET_ACTION_FEEDBACK");
    case SMSG_TIME_SYNC_REQ:            return TEXT("SMSG_TIME_SYNC_REQ");
    case SMSG_TUTORIAL_FLAGS:           return TEXT("SMSG_TUTORIAL_FLAGS");
    case SMSG_ACCOUNT_DATA_TIMES:       return TEXT("SMSG_ACCOUNT_DATA_TIMES");
    case SMSG_SET_PROFICIENCY:          return TEXT("SMSG_SET_PROFICIENCY");
    case SMSG_POWER_UPDATE:             return TEXT("SMSG_POWER_UPDATE");
    case SMSG_AURA_UPDATE:              return TEXT("SMSG_AURA_UPDATE");
    case SMSG_ADDON_INFO:               return TEXT("SMSG_ADDON_INFO");
    case SMSG_WARDEN_DATA:              return TEXT("SMSG_WARDEN_DATA");
    case SMSG_MONSTER_MOVE:             return TEXT("SMSG_MONSTER_MOVE");
    case SMSG_SPELL_START:              return TEXT("SMSG_SPELL_START");
    case SMSG_SPELL_GO:                 return TEXT("SMSG_SPELL_GO");
    case MSG_MOVE_HEARTBEAT:            return TEXT("MSG_MOVE_HEARTBEAT");
    case CMSG_PLAYER_LOGIN:             return TEXT("CMSG_PLAYER_LOGIN");
    case CMSG_CHAR_ENUM:                return TEXT("CMSG_CHAR_ENUM");
    case CMSG_KEEP_ALIVE:               return TEXT("CMSG_KEEP_ALIVE");
    case CMSG_GAMEOBJ_USE:              return TEXT("CMSG_GAMEOBJ_USE");
    case CMSG_GET_MAIL_LIST:            return TEXT("CMSG_GET_MAIL_LIST");
    case CMSG_SEND_MAIL:                return TEXT("CMSG_SEND_MAIL");
    case CMSG_MAIL_TAKE_MONEY:          return TEXT("CMSG_MAIL_TAKE_MONEY");
    case CMSG_MAIL_TAKE_ITEM:           return TEXT("CMSG_MAIL_TAKE_ITEM");
    case CMSG_MAIL_MARK_AS_READ:        return TEXT("CMSG_MAIL_MARK_AS_READ");
    case CMSG_MAIL_RETURN_TO_SENDER:    return TEXT("CMSG_MAIL_RETURN_TO_SENDER");
    case CMSG_MAIL_DELETE:              return TEXT("CMSG_MAIL_DELETE");
    case CMSG_QUEST_POI_QUERY:          return TEXT("CMSG_QUEST_POI_QUERY");
    case SMSG_QUEST_POI_QUERY_RESPONSE: return TEXT("SMSG_QUEST_POI_QUERY_RESPONSE");
    case CMSG_BANKER_ACTIVATE:          return TEXT("CMSG_BANKER_ACTIVATE");
    case CMSG_INITIATE_TRADE:           return TEXT("CMSG_INITIATE_TRADE");
    case CMSG_BEGIN_TRADE:              return TEXT("CMSG_BEGIN_TRADE");
    case CMSG_ACCEPT_TRADE:             return TEXT("CMSG_ACCEPT_TRADE");
    case CMSG_UNACCEPT_TRADE:           return TEXT("CMSG_UNACCEPT_TRADE");
    case CMSG_CANCEL_TRADE:             return TEXT("CMSG_CANCEL_TRADE");
    case CMSG_SET_TRADE_GOLD:           return TEXT("CMSG_SET_TRADE_GOLD");
    case CMSG_DUEL_ACCEPTED:            return TEXT("CMSG_DUEL_ACCEPTED");
    case CMSG_DUEL_CANCELLED:           return TEXT("CMSG_DUEL_CANCELLED");
    case CMSG_PET_SET_ACTION:           return TEXT("CMSG_PET_SET_ACTION");
    case CMSG_PET_ACTION:               return TEXT("CMSG_PET_ACTION");
    case CMSG_PET_CAST_SPELL:           return TEXT("CMSG_PET_CAST_SPELL");
    case CMSG_REQUEST_PET_INFO:         return TEXT("CMSG_REQUEST_PET_INFO");
    case CMSG_PET_STOP_ATTACK:          return TEXT("CMSG_PET_STOP_ATTACK");
    case CMSG_PET_SPELL_AUTOCAST:       return TEXT("CMSG_PET_SPELL_AUTOCAST");
    case CMSG_SET_ACTION_BUTTON:        return TEXT("CMSG_SET_ACTION_BUTTON");
    case CMSG_LFG_JOIN:                 return TEXT("CMSG_LFG_JOIN");
    case CMSG_LFG_LEAVE:                return TEXT("CMSG_LFG_LEAVE");
    case CMSG_SEARCH_LFG_JOIN:          return TEXT("CMSG_SEARCH_LFG_JOIN");
    case CMSG_SEARCH_LFG_LEAVE:         return TEXT("CMSG_SEARCH_LFG_LEAVE");
    case CMSG_LFG_PROPOSAL_RESULT:      return TEXT("CMSG_LFG_PROPOSAL_RESULT");
    case CMSG_SET_LFG_COMMENT:          return TEXT("CMSG_SET_LFG_COMMENT");
    case CMSG_LFG_SET_ROLES:            return TEXT("CMSG_LFG_SET_ROLES");
    case CMSG_LFG_GET_STATUS:           return TEXT("CMSG_LFG_GET_STATUS");
    case SMSG_UPDATE_LFG_LIST:          return TEXT("SMSG_UPDATE_LFG_LIST");
    case SMSG_LFG_PROPOSAL_UPDATE:      return TEXT("SMSG_LFG_PROPOSAL_UPDATE");
    case SMSG_LFG_ROLE_CHECK_UPDATE:    return TEXT("SMSG_LFG_ROLE_CHECK_UPDATE");
    case SMSG_LFG_JOIN_RESULT:          return TEXT("SMSG_LFG_JOIN_RESULT");
    case SMSG_LFG_QUEUE_STATUS:         return TEXT("SMSG_LFG_QUEUE_STATUS");
    case SMSG_LFG_UPDATE_PLAYER:        return TEXT("SMSG_LFG_UPDATE_PLAYER");
    case SMSG_LFG_UPDATE_PARTY:         return TEXT("SMSG_LFG_UPDATE_PARTY");
    case SMSG_LFG_UPDATE_SEARCH:        return TEXT("SMSG_LFG_UPDATE_SEARCH");
    case SMSG_LFG_DISABLED:             return TEXT("SMSG_LFG_DISABLED");

    // Social / Guild / Friends
    case SMSG_FRIEND_LIST:              return TEXT("SMSG_FRIEND_LIST");
    case SMSG_FRIEND_STATUS:            return TEXT("SMSG_FRIEND_STATUS");
    case SMSG_GUILD_ROSTER:             return TEXT("SMSG_GUILD_ROSTER");
    case SMSG_GUILD_EVENT:              return TEXT("SMSG_GUILD_EVENT");
    case SMSG_CHANNEL_NOTIFY:           return TEXT("SMSG_CHANNEL_NOTIFY");
    case SMSG_GROUP_LIST:               return TEXT("SMSG_GROUP_LIST");
    case SMSG_PARTY_COMMAND_RESULT:     return TEXT("SMSG_PARTY_COMMAND_RESULT");
    case SMSG_RAID_GROUP_ONLY:          return TEXT("SMSG_RAID_GROUP_ONLY");
    case MSG_RAID_TARGET_UPDATE:        return TEXT("MSG_RAID_TARGET_UPDATE");
    case MSG_RAID_READY_CHECK:          return TEXT("MSG_RAID_READY_CHECK");
    case MSG_RAID_READY_CHECK_CONFIRM:  return TEXT("MSG_RAID_READY_CHECK_CONFIRM");
    case MSG_RAID_READY_CHECK_FINISHED: return TEXT("MSG_RAID_READY_CHECK_FINISHED");
    case SMSG_RAID_READY_CHECK_ERROR:   return TEXT("SMSG_RAID_READY_CHECK_ERROR");
    case SMSG_WHO:                      return TEXT("SMSG_WHO");
    case CMSG_FRIEND_LIST:              return TEXT("CMSG_FRIEND_LIST");
    case CMSG_ADD_FRIEND:               return TEXT("CMSG_ADD_FRIEND");
    case CMSG_DEL_FRIEND:               return TEXT("CMSG_DEL_FRIEND");
    case CMSG_ADD_IGNORE:               return TEXT("CMSG_ADD_IGNORE");
    case CMSG_DEL_IGNORE:               return TEXT("CMSG_DEL_IGNORE");
    case SMSG_IGNORE_LIST:              return TEXT("SMSG_IGNORE_LIST");
    case CMSG_GUILD_ROSTER:             return TEXT("CMSG_GUILD_ROSTER");
    case CMSG_WHO:                      return TEXT("CMSG_WHO");
    case CMSG_GROUP_INVITE:             return TEXT("CMSG_GROUP_INVITE");
    case CMSG_GROUP_ACCEPT:             return TEXT("CMSG_GROUP_ACCEPT");

    // Warden and teleport opcodes
    case CMSG_WARDEN_DATA:              return TEXT("CMSG_WARDEN_DATA");
    case SMSG_TRANSFER_PENDING:         return TEXT("SMSG_TRANSFER_PENDING");
    case SMSG_NEW_WORLD:                return TEXT("SMSG_NEW_WORLD");
    case MSG_MOVE_TELEPORT:             return TEXT("MSG_MOVE_TELEPORT");
    case MSG_MOVE_WORLDPORT_ACK:        return TEXT("MSG_MOVE_WORLDPORT_ACK");

    default:                            return TEXT("UNKNOWN");
    }
}
