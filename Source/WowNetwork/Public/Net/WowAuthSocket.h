#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"

class FSocket;
struct FWowRealmInfo;

DECLARE_DELEGATE_OneParam(FOnAuthResult, bool /* Success */);
DECLARE_DELEGATE_OneParam(FOnRealmListReceived, const TArray<FWowRealmInfo>&);

class WOWNETWORK_API FWowAuthSocket : public FRunnable
{
public:
    FWowAuthSocket();
    virtual ~FWowAuthSocket();

    /** Connect and begin login */
    bool Connect(const FString& Host, int32 Port, const FString& Username, const FString& Password);

    /** Request realm list (after successful auth) */
    void RequestRealmList();

    /** Disconnect */
    void Disconnect();

    /** Get session key after successful auth */
    TArray<uint8> GetSessionKey() const;

    // FRunnable
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;

    FOnAuthResult OnAuthResult;
    FOnRealmListReceived OnRealmList;

private:
    void SendLogonChallenge();
    void HandleLogonChallenge();
    void HandleLogonProof();
    void HandleRealmList();

    bool SendBytes(const TArray<uint8>& Data);
    bool RecvBytes(TArray<uint8>& OutData, int32 NumBytes);

    FSocket* Socket = nullptr;
    FRunnableThread* Thread = nullptr;
    FString Username;
    FString Password;
    TArray<uint8> SessionKey;

    TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> CallbackActive = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(true);
    bool bRunning = false;
    bool bAuthComplete = false;
    bool bRealmListRequested = false;

    // SRP6 state
    TSharedPtr<class FWowSRP6> SRP6;

    mutable FCriticalSection SocketLock;
};
