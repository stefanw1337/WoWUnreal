/*
 * mod-wowunreal - handshake with the WowUnreal client.
 *
 * Protocol (hidden addon whispers to self, prefix WOWUNREAL):
 *   client -> server  HELLO <version> <client nonce hex>
 *   server -> client  CAPS <flags hex> <server nonce hex>
 *   client -> server  PROOF <hex HMAC-SHA256(release secret, server nonce | player guid low | version)>
 *   server -> client  OK <flags hex>      (or FAIL)
 *
 * A session without a valid PROOF within WowUnreal.HandshakeTimeout seconds is kicked when
 * WowUnreal.RequireClient = 1. Every gameplay rule stays on the server; the client check only
 * keeps casual stock clients out (see the design notes in README.md).
 *
 * Written against AzerothCore master (OnPlayer* hooks with enabled-hook lists).
 */

#include "WowUnreal.h"
#include "Chat.h"
#include "Config.h"
#include "CryptoHash.h"
#include "CryptoRandom.h"
#include "HMAC.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Tokenize.h"
#include "Util.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <sstream>

namespace WowUnreal
{
    Config& GetConfig()
    {
        static Config Instance;
        return Instance;
    }

    std::unordered_map<uint32, Session>& Sessions()
    {
        static std::unordered_map<uint32, Session> Instance;
        return Instance;
    }

    uint32 Config::CapsFlags() const
    {
        uint32 Flags = 0;
        if (FirstPersonLock) Flags |= CAPS_FIRST_PERSON_LOCK;
        if (WaveDrift)       Flags |= CAPS_WAVE_DRIFT;
        if (WeatherGameplay) Flags |= CAPS_WEATHER_GAMEPLAY;
        if (VrAllowed)       Flags |= CAPS_VR_ALLOWED;
        return Flags;
    }

    namespace
    {
        std::string ToHex(uint8 const* Data, size_t Size)
        {
            static char const* Digits = "0123456789abcdef";
            std::string Out;
            Out.reserve(Size * 2);
            for (size_t I = 0; I < Size; ++I)
            {
                Out.push_back(Digits[Data[I] >> 4]);
                Out.push_back(Digits[Data[I] & 0xF]);
            }
            return Out;
        }

        std::vector<uint8> FromHex(std::string const& Hex)
        {
            std::vector<uint8> Out;
            if (Hex.size() % 2) return Out;
            for (size_t I = 0; I + 1 < Hex.size(); I += 2)
            {
                auto Nibble = [](char C) -> int
                {
                    if (C >= '0' && C <= '9') return C - '0';
                    if (C >= 'a' && C <= 'f') return C - 'a' + 10;
                    if (C >= 'A' && C <= 'F') return C - 'A' + 10;
                    return -1;
                };
                int const High = Nibble(Hex[I]), Low = Nibble(Hex[I + 1]);
                if (High < 0 || Low < 0) return {};
                Out.push_back(uint8(High << 4 | Low));
            }
            return Out;
        }

        // Hidden addon whisper to the player himself (the client reads it, the chat frame does not).
        void SendAddon(Player* player, std::string const& Payload)
        {
            WorldPacket Data;
            std::string const Message = std::string(AddonPrefix) + "\t" + Payload;
            ChatHandler::BuildChatPacket(Data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Message);
            player->GetSession()->SendPacket(&Data);
        }

        bool ProofMatches(Session const& State, uint32 PlayerGuidLow, std::string const& ProofHex)
        {
            std::vector<uint8> const Proof = FromHex(ProofHex);
            if (Proof.size() != Acore::Crypto::Constants::SHA256_DIGEST_LENGTH_BYTES) return false;
            std::string const Message = State.ServerNonce + "|" + std::to_string(PlayerGuidLow) + "|" + State.ClientVersion;
            for (std::vector<uint8> const& Secret : GetConfig().ReleaseSecrets)
            {
                Acore::Crypto::HMAC_SHA256 Hmac(Secret.data(), Secret.size());
                Hmac.UpdateData(Message);
                Hmac.Finalize();
                auto const& Digest = Hmac.GetDigest();
                if (std::equal(Digest.begin(), Digest.end(), Proof.begin())) return true;
            }
            return false;
        }

        void HandleClientMessage(Player* player, std::string const& Payload)
        {
            uint32 const AccountId = player->GetSession()->GetAccountId();
            Session& State = Sessions()[AccountId];
            std::istringstream In(Payload);
            std::string Command;
            In >> Command;

            if (Command == "HELLO")
            {
                In >> State.ClientVersion >> State.ClientNonce;
                std::array<uint8, 16> Nonce = Acore::Crypto::GetRandomBytes<16>();
                State.ServerNonce = ToHex(Nonce.data(), Nonce.size());
                State.State = HandshakeState::Challenged;
                std::ostringstream Out;
                Out << "CAPS " << std::hex << GetConfig().CapsFlags() << " " << State.ServerNonce;
                SendAddon(player, Out.str());
                LOG_INFO("module", "mod-wowunreal: {} (account {}) says HELLO, client {}",
                    player->GetName(), AccountId, State.ClientVersion);
            }
            else if (Command == "PROOF" && State.State == HandshakeState::Challenged)
            {
                std::string ProofHex;
                In >> ProofHex;
                // No secrets configured: accept any client that speaks the handshake (development).
                bool const bValid = GetConfig().ReleaseSecrets.empty() || ProofMatches(State, player->GetGUID().GetCounter(), ProofHex);
                State.State = bValid ? HandshakeState::Verified : HandshakeState::Failed;
                std::ostringstream Out;
                Out << (bValid ? "OK " : "FAIL ") << std::hex << GetConfig().CapsFlags();
                SendAddon(player, Out.str());
                LOG_INFO("module", "mod-wowunreal: {} handshake {}", player->GetName(), bValid ? "verified" : "FAILED");
            }
        }

        void LoadConfig()
        {
            Config& C = GetConfig();
            C.Enable = sConfigMgr->GetOption<bool>("WowUnreal.Enable", true);
            C.RequireClient = sConfigMgr->GetOption<bool>("WowUnreal.RequireClient", false);
            C.HandshakeTimeoutMs = sConfigMgr->GetOption<uint32>("WowUnreal.HandshakeTimeout", 15) * IN_MILLISECONDS;
            C.FirstPersonLock = sConfigMgr->GetOption<bool>("WowUnreal.FirstPersonLock", false);
            C.WaveDrift = sConfigMgr->GetOption<bool>("WowUnreal.WaveDrift", false);
            C.WeatherGameplay = sConfigMgr->GetOption<bool>("WowUnreal.WeatherGameplay", false);
            C.VrAllowed = sConfigMgr->GetOption<bool>("WowUnreal.VrAllowed", true);
            C.WeatherSlowMax = sConfigMgr->GetOption<uint32>("WowUnreal.WeatherSlowMax", 20);
            C.AshEmberDamage = sConfigMgr->GetOption<bool>("WowUnreal.AshEmberDamage", true);
            C.ReleaseSecrets.clear();
            std::string const Secrets = sConfigMgr->GetOption<std::string>("WowUnreal.ReleaseSecrets", "");
            for (std::string_view Token : Acore::Tokenize(Secrets, ',', false))
            {
                std::vector<uint8> Secret = FromHex(std::string(Token));
                if (!Secret.empty()) C.ReleaseSecrets.push_back(std::move(Secret));
            }
        }
    }
}

using namespace WowUnreal;

class WowUnrealWorldScript : public WorldScript
{
public:
    WowUnrealWorldScript() : WorldScript("WowUnrealWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }
};

class WowUnrealPlayerScript : public PlayerScript
{
public:
    WowUnrealPlayerScript() : PlayerScript("WowUnrealPlayerScript", {
        PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!GetConfig().Enable) return;
        Sessions()[player->GetSession()->GetAccountId()] = Session(); // new nonce on every login
    }

    void OnPlayerLogout(Player* player) override
    {
        Sessions().erase(player->GetSession()->GetAccountId());
    }

    // Addon messages (any chat type, LANG_ADDON): "WOWUNREAL\t<payload>". The client whispers them
    // to itself; every CMSG_MESSAGECHAT passes this hook before it is delivered.
    void OnPlayerBeforeSendChatMessage(Player* player, uint32& /*type*/, uint32& Lang, std::string& Message) override
    {
        if (!GetConfig().Enable || Lang != uint32(LANG_ADDON)) return;
        std::string const Prefix = std::string(AddonPrefix) + "\t";
        if (Message.compare(0, Prefix.size(), Prefix) != 0) return;
        HandleClientMessage(player, Message.substr(Prefix.size()));
    }

    void OnPlayerUpdate(Player* player, uint32 Diff) override
    {
        Config const& C = GetConfig();
        if (!C.Enable || !C.RequireClient) return;
        auto Found = Sessions().find(player->GetSession()->GetAccountId());
        if (Found == Sessions().end() || Found->second.State == HandshakeState::Verified) return;
        Session& State = Found->second;
        State.ElapsedMs += Diff;
        if (State.State == HandshakeState::Failed || State.ElapsedMs >= C.HandshakeTimeoutMs)
        {
            LOG_INFO("module", "mod-wowunreal: kicking {} (no valid WowUnreal handshake)", player->GetName());
            ChatHandler(player->GetSession()).PSendSysMessage("This realm requires the WowUnreal client.");
            player->GetSession()->KickPlayer("mod-wowunreal: no valid client handshake");
            State.State = HandshakeState::Verified; // kick once
        }
    }
};

void AddWowUnrealScripts()
{
    new WowUnrealWorldScript();
    new WowUnrealPlayerScript();
}
