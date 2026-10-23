#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Crypto/WowAuthCrypt.h"

struct FWowCharacterInfo;

DECLARE_DELEGATE_OneParam(FOnWorldAuthResult, bool);
DECLARE_DELEGATE_OneParam(FOnWorldDisconnected, const FString&);
DECLARE_DELEGATE_OneParam(FOnCharacterListReceived, const TArray<FWowCharacterInfo>&);
DECLARE_DELEGATE_TwoParams(FOnWorldPacket, uint16 /*Opcode*/, const TArray<uint8>& /*Data*/);

class WOWNETWORK_API FWowWorldSocket : public FRunnable
{
public:
    FWowWorldSocket();
    virtual ~FWowWorldSocket();

    /** r378: the account's expansion level from the last AUTH_OK (0 Classic, 1 TBC, 2 Wrath; -1 unknown). */
    static int32 GetLastAccountExpansion();

    bool Connect(const FString& Host, int32 Port, const FString& AccountName, const TArray<uint8>& SessionKey, uint32 RealmId);
    void Disconnect();

    /** Send CMSG_CHAR_ENUM to request character list */
    void SendCharEnum();

    /** Send CMSG_CHAR_CREATE to create a new character */
    void SendCharCreate(const FString& Name, uint8 Race, uint8 Class, uint8 Gender,
        uint8 Skin, uint8 Face, uint8 HairStyle, uint8 HairColor, uint8 FacialHair);

    /** Send CMSG_CHAR_DELETE to delete a character */
    void SendCharDelete(uint64 Guid);

    /** Send CMSG_PLAYER_LOGIN with character GUID */
    void SendPlayerLogin(uint64 Guid);

    /** Send a raw packet */
    void SendPacket(uint32 Opcode, const TArray<uint8>& Data = {});

    // FRunnable
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;

    int32 GetLatencyMs() const;
    FOnWorldDisconnected OnDisconnected;
    FOnWorldAuthResult OnAuthResult;
    FOnCharacterListReceived OnCharacterList;
    FOnWorldPacket OnPacket;

private:
    void HandleAuthChallenge(const TArray<uint8>& Data);
    void HandleAuthResponse(const TArray<uint8>& Data);
    void HandleCharEnum(const TArray<uint8>& Data);

    void FailConnection(const FString& Reason);
    bool UpdateHeartbeat();
    bool SendRawBytes(const uint8* Data, int32 Len);
    bool RecvExact(uint8* Buffer, int32 Len);

    class FSocket* Socket = nullptr;
    FRunnableThread* Thread = nullptr;

    FWowAuthCrypt Crypt;
    FString AccountName;
    TArray<uint8> SessionKey;
    uint32 RealmId = 1;

    TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> CallbackActive = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(true);
    FThreadSafeBool bRunning = false;
    bool bAuthenticated = false;
    double ConnectedAt = 0.0;
    double LastPingAt = 0.0;
    uint32 PingSequence = 0;
    TAtomic<double> PendingPingAt{0.0};
    TAtomic<int32> LatencyMs{0};
    bool bEncrypted = false;

    // Thread-safe send queue
    TQueue<TArray<uint8>, EQueueMode::Mpsc> SendQueue;

    // Socket and cipher are worker-owned until Disconnect has joined the thread.
};
