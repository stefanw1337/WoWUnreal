#include "WowRealmHandshake.h"
#include "WowConnectionManager.h"
#include "Crypto/OpenSSLIncludes.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowRealmHandshake, Log, All);

namespace
{
    // This release's secret. The realm lists the secrets of the releases it accepts in
    // WowUnreal.ReleaseSecrets (mod_wowunreal.conf). Rotate it for each public release.
    constexpr const TCHAR* HandshakeReleaseSecretHex = TEXT("617ebc50c3b63536035134ee24f49c25af248c086d5a0fc27495eef575ac3f35");
    constexpr const TCHAR* HandshakeClientVersion = TEXT("WowUnreal-1");
    constexpr const TCHAR* HandshakeAddonPrefix = TEXT("WOWUNREAL");
    constexpr int32 HandshakeWhisperType = 7; // CHAT_MSG_WHISPER

    FWowRealmCaps GRealmCaps;
    FString GRealmServerNonce;

    FString HandshakeRandomHex(int32 Bytes)
    {
        FString Out;
        for (int32 I = 0; I < Bytes; ++I) Out += FString::Printf(TEXT("%02x"), FMath::RandRange(0, 255));
        return Out;
    }

    TArray<uint8> HandshakeFromHex(const FString& Hex)
    {
        TArray<uint8> Out;
        Out.SetNumUninitialized(Hex.Len() / 2);
        HexToBytes(Hex, Out.GetData());
        return Out;
    }

    FString HandshakeProof(const FString& ServerNonce, uint64 PlayerGuid)
    {
        const TArray<uint8> Secret = HandshakeFromHex(HandshakeReleaseSecretHex);
        const FString Message = FString::Printf(TEXT("%s|%u|%s"), *ServerNonce, uint32(PlayerGuid & 0xffffffffu), HandshakeClientVersion);
        const FTCHARToUTF8 Utf8(*Message);
        uint8 Digest[32];
        unsigned int DigestLength = 0;
        HMAC(EVP_sha256(), Secret.GetData(), Secret.Num(), reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Digest, &DigestLength);
        FString Out;
        for (unsigned int I = 0; I < DigestLength; ++I) Out += FString::Printf(TEXT("%02x"), Digest[I]);
        return Out;
    }
}

const FWowRealmCaps& FWowRealmHandshake::Get()
{
    return GRealmCaps;
}

void FWowRealmHandshake::Reset()
{
    GRealmCaps = FWowRealmCaps();
    GRealmServerNonce.Reset();
}

void FWowRealmHandshake::SendHello(UWowConnectionManager& Connection)
{
    const FString Self = Connection.GetCharacterName();
    if (Self.IsEmpty()) return;
    Connection.SendAddonMessage(HandshakeAddonPrefix, FString::Printf(TEXT("HELLO %s %s"), HandshakeClientVersion, *HandshakeRandomHex(16)), HandshakeWhisperType, Self);
    UE_LOG(LogWowRealmHandshake, Log, TEXT("Realm handshake: HELLO sent"));
}

bool FWowRealmHandshake::HandleAddonMessage(UWowConnectionManager& Connection, const FString& Message)
{
    FString Payload;
    if (!Message.Split(TEXT("\t"), nullptr, &Payload) || !Message.StartsWith(FString(HandshakeAddonPrefix) + TEXT("\t"))) return false;
    TArray<FString> Parts;
    Payload.ParseIntoArrayWS(Parts);
    if (Parts.Num() < 2) return true;
    const uint32 Flags = FParse::HexNumber(*Parts[1]);
    if (Parts[0] == TEXT("CAPS") && Parts.Num() >= 3)
    {
        GRealmCaps.bServerHasModule = true;
        GRealmCaps.Flags = Flags;
        GRealmServerNonce = Parts[2];
        Connection.SendAddonMessage(HandshakeAddonPrefix, TEXT("PROOF ") + HandshakeProof(GRealmServerNonce, Connection.PacketHandler.EntityManager.LocalPlayerGuid),
            HandshakeWhisperType, Connection.GetCharacterName());
        UE_LOG(LogWowRealmHandshake, Log, TEXT("Realm handshake: mod-wowunreal found, flags 0x%x"), Flags);
    }
    else if (Parts[0] == TEXT("OK") || Parts[0] == TEXT("FAIL"))
    {
        GRealmCaps.bServerHasModule = true;
        GRealmCaps.bVerified = Parts[0] == TEXT("OK");
        GRealmCaps.Flags = Flags;
        UE_LOG(LogWowRealmHandshake, Log, TEXT("Realm handshake: %s (flags 0x%x)"),
            GRealmCaps.bVerified ? TEXT("verified") : TEXT("REJECTED by the realm"), Flags);
    }
    return true;
}
