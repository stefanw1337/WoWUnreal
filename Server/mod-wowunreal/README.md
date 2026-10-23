# mod-wowunreal

AzerothCore module for the WowUnreal client (Unreal Engine 5 client for 3.3.5a).

Status: skeleton (handshake + config). Weather gameplay is not implemented yet.

## What it does

- Recognises WowUnreal clients through a hidden addon-message handshake after login.
- Sends the realm's feature flags (CAPS) to the client: first-person lock, wave drift, weather gameplay, VR.
- Optionally kicks clients that do not complete the handshake (`WowUnreal.RequireClient = 1`).

## Install

1. Copy this folder to `azerothcore-wotlk/modules/mod-wowunreal`.
2. Re-run CMake and build the worldserver.
3. Copy `conf/mod_wowunreal.conf.dist` to your config folder as `mod_wowunreal.conf` and adjust.

## Handshake

Hidden whispers to self in LANG_ADDON, prefix `WOWUNREAL`:

| Direction | Message |
|---|---|
| client -> server | `HELLO <version> <client nonce hex>` |
| server -> client | `CAPS <flags hex> <server nonce hex>` |
| client -> server | `PROOF <hex HMAC-SHA256(release secret, server nonce \| player guid low \| version)>` |
| server -> client | `OK <flags hex>` or `FAIL <flags hex>` |

CAPS flags: `0x01` first-person lock, `0x02` wave drift, `0x04` weather gameplay, `0x08` VR allowed.

With `WowUnreal.ReleaseSecrets` empty every client that sends a PROOF is accepted (development).

## Notes

- No client check can be made unbreakable: the secret lives in the client binary. Fairness comes
  from keeping every gameplay rule on the server; the handshake only keeps casual stock clients out.
- For account protection use AzerothCore's built-in TOTP 2FA (`.account 2fa setup`).
- Compiles against AzerothCore master (checked 10 Oct 2026): hooks `OnPlayerLogin`, `OnPlayerLogout`,
  `OnPlayerUpdate`, `OnPlayerBeforeSendChatMessage` (addon messages), `OnAfterConfigLoad`.
