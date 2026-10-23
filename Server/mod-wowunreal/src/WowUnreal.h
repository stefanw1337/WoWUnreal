/*
 * mod-wowunreal - shared state and config.
 */
#ifndef MOD_WOWUNREAL_H
#define MOD_WOWUNREAL_H

#include "Define.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace WowUnreal
{
    // CAPS flag bits sent to the client after a valid handshake.
    enum CapsFlags : uint32
    {
        CAPS_FIRST_PERSON_LOCK = 0x01,
        CAPS_WAVE_DRIFT        = 0x02,
        CAPS_WEATHER_GAMEPLAY  = 0x04,
        CAPS_VR_ALLOWED        = 0x08,
    };

    struct Config
    {
        bool Enable = true;
        bool RequireClient = false;
        uint32 HandshakeTimeoutMs = 15000;
        std::vector<std::vector<uint8>> ReleaseSecrets; // accepted client release secrets (raw bytes)
        bool FirstPersonLock = false;
        bool WaveDrift = false;
        bool WeatherGameplay = false;
        bool VrAllowed = true;
        uint32 WeatherSlowMax = 20;
        bool AshEmberDamage = true;

        uint32 CapsFlags() const;
    };

    enum class HandshakeState : uint8
    {
        Waiting,    // logged in, no HELLO yet
        Challenged, // HELLO received, CAPS + server nonce sent, waiting for PROOF
        Verified,   // valid proof: a WowUnreal client
        Failed      // wrong proof (or none in time)
    };

    struct Session
    {
        HandshakeState State = HandshakeState::Waiting;
        uint32 ElapsedMs = 0;
        std::string ClientVersion;
        std::string ClientNonce;
        std::string ServerNonce; // hex, new per login
    };

    Config& GetConfig();
    std::unordered_map<uint32 /*account id*/, Session>& Sessions();

    // Addon message prefix used in both directions ("WOWUNREAL\t<payload>").
    constexpr char const* AddonPrefix = "WOWUNREAL";
}

#endif
