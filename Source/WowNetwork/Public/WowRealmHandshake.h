#pragma once

#include "CoreMinimal.h"

class UWowConnectionManager;

/** Feature flags a realm running mod-wowunreal sends in its CAPS reply (r259). */
namespace EWowRealmCapsFlag
{
    inline constexpr uint32 FirstPersonLock = 0x01;
    inline constexpr uint32 WaveDrift       = 0x02;
    inline constexpr uint32 WeatherGameplay = 0x04;
    inline constexpr uint32 VrAllowed       = 0x08;
}

struct WOWNETWORK_API FWowRealmCaps
{
    /** The realm answered our HELLO: it runs mod-wowunreal. */
    bool bServerHasModule = false;
    /** The realm accepted our release proof. */
    bool bVerified = false;
    uint32 Flags = 0;

    bool HasFlag(uint32 Flag) const { return bServerHasModule && (Flags & Flag) != 0; }
};

/**
 * Client half of the mod-wowunreal handshake (hidden addon whispers to self, prefix WOWUNREAL):
 *   HELLO <version> <client nonce>  ->  CAPS <flags> <server nonce>
 *   PROOF <HMAC-SHA256(release secret, server nonce | player guid low | version)>  ->  OK/FAIL <flags>
 * A realm without the module never answers, and every flag stays off.
 */
class WOWNETWORK_API FWowRealmHandshake
{
public:
    static const FWowRealmCaps& Get();
    static void Reset();
    /** Called once the character is in the world. */
    static void SendHello(UWowConnectionManager& Connection);
    /** Handles WOWUNREAL addon messages; returns true when the message was ours. */
    static bool HandleAddonMessage(UWowConnectionManager& Connection, const FString& Message);
};
