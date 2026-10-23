#include "Net/WowAuthSocket.h"
#include "WowLocale.h"
#include "Crypto/WowSRP6.h"
#include "WowSessionState.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "HAL/RunnableThread.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowAuth, Log, All);

// Auth opcodes
enum EAuthCmd : uint8
{
    CMD_AUTH_LOGON_CHALLENGE = 0x00,
    CMD_AUTH_LOGON_PROOF     = 0x01,
    CMD_REALM_LIST           = 0x10,
};

FWowAuthSocket::FWowAuthSocket()
{
}

FWowAuthSocket::~FWowAuthSocket()
{
    Disconnect();
}

bool FWowAuthSocket::Connect(const FString& InHost, int32 InPort, const FString& InUsername, const FString& InPassword)
{
    CallbackActive = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(true);
    Username = InUsername.ToUpper();
    Password = InPassword.ToUpper();

    ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSub)
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to get socket subsystem"));
        return false;
    }

    Socket = SocketSub->CreateSocket(NAME_Stream, TEXT("WowAuth"), false);
    if (!Socket)
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to create socket"));
        return false;
    }

    TSharedRef<FInternetAddr> Addr = SocketSub->CreateInternetAddr();

    // Resolve hostname
    ESocketErrors ResolveError = SocketSub->GetHostByName(TCHAR_TO_ANSI(*InHost), *Addr);
    if (ResolveError != SE_NO_ERROR)
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to resolve host: %s"), *InHost);
        SocketSub->DestroySocket(Socket);
        Socket = nullptr;
        return false;
    }

    Addr->SetPort(InPort);

    if (!Socket->Connect(*Addr))
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to connect to %s:%d"), *InHost, InPort);
        SocketSub->DestroySocket(Socket);
        Socket = nullptr;
        return false;
    }

    UE_LOG(LogWowAuth, Log, TEXT("Connected to auth server %s:%d"), *InHost, InPort);

    bRunning = true;
    bAuthComplete = false;
    bRealmListRequested = false;
    Thread = FRunnableThread::Create(this, TEXT("WowAuthSocket"), 0, TPri_Normal);

    return true;
}

void FWowAuthSocket::Disconnect()
{
    *CallbackActive = false;
    bRunning = false;

    {
        FScopeLock Lock(&SocketLock);
        if (Socket)
        {
            Socket->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
            Socket = nullptr;
        }
    }

    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
}

TArray<uint8> FWowAuthSocket::GetSessionKey() const
{
    return SessionKey;
}

bool FWowAuthSocket::Init()
{
    return true;
}

uint32 FWowAuthSocket::Run()
{
    // Step 1: Send logon challenge
    SendLogonChallenge();

    // Step 2: Handle logon challenge response
    HandleLogonChallenge();

    if (!bRunning)
    {
        return 1;
    }

    // Step 3: Handle logon proof response
    HandleLogonProof();

    // Step 4: If realm list is requested, handle that in a loop
    while (bRunning && bAuthComplete)
    {
        if (bRealmListRequested)
        {
            bRealmListRequested = false;

            // Send CMD_REALM_LIST
            TArray<uint8> Packet;
            Packet.Add(CMD_REALM_LIST);
            Packet.Add(0); Packet.Add(0); Packet.Add(0); Packet.Add(0); // uint32 padding = 0
            SendBytes(Packet);

            HandleRealmList();
        }
        else
        {
            FPlatformProcess::Sleep(0.05f);
        }
    }

    return 0;
}

void FWowAuthSocket::Stop()
{
    bRunning = false;
}

void FWowAuthSocket::RequestRealmList()
{
    bRealmListRequested = true;
}

void FWowAuthSocket::SendLogonChallenge()
{
    // Build CMD_AUTH_LOGON_CHALLENGE packet
    FTCHARToUTF8 UserUtf8(*Username);
    int32 UserLen = UserUtf8.Length();

    // Packet size (everything after cmd + error + size fields)
    uint16 PacketSize = 4 + 3 + 2 + 4 + 4 + 4 + 4 + 4 + 1 + UserLen; // = 30 + UserLen

    TArray<uint8> Packet;
    Packet.Reserve(4 + PacketSize);

    // cmd
    Packet.Add(CMD_AUTH_LOGON_CHALLENGE);
    // error (protocol version)
    Packet.Add(0x08);
    // size (little-endian uint16)
    Packet.Add(PacketSize & 0xFF);
    Packet.Add((PacketSize >> 8) & 0xFF);

    // gamename "WoW\0" (4 bytes, reversed)
    Packet.Add('W'); Packet.Add('o'); Packet.Add('W'); Packet.Add(0);

    // version 3.3.5
    Packet.Add(3); Packet.Add(3); Packet.Add(5);

    // build 12340 (little-endian uint16)
    uint16 Build = 12340;
    Packet.Add(Build & 0xFF);
    Packet.Add((Build >> 8) & 0xFF);

    // platform "x86" stored as "68x\0" so the auth server reverses it back to "x86"
    Packet.Add('6'); Packet.Add('8'); Packet.Add('x'); Packet.Add(0);

    // os "Win" stored as "niW\0" so the auth server reverses it back to "Win"
    Packet.Add('n'); Packet.Add('i'); Packet.Add('W'); Packet.Add(0);

    // Authentication carries the selected four-character client locale.
    const FString Locale = WowLocale::Get();
    for (int32 Index = 3; Index >= 0; --Index) Packet.Add(static_cast<uint8>(Locale[Index]));

    // timezone_bias (uint32 LE)
    Packet.Add(0); Packet.Add(0); Packet.Add(0); Packet.Add(0);

    // ip (uint32 LE)
    Packet.Add(0); Packet.Add(0); Packet.Add(0); Packet.Add(0);

    // username length
    Packet.Add(static_cast<uint8>(UserLen));

    // username bytes
    for (int32 i = 0; i < UserLen; ++i)
    {
        Packet.Add(static_cast<uint8>(UserUtf8.Get()[i]));
    }

    SendBytes(Packet);
    UE_LOG(LogWowAuth, Log, TEXT("Sent logon challenge for user: %s"), *Username);
}

void FWowAuthSocket::HandleLogonChallenge()
{
    // Read response header: cmd(1) + unk(1) + error(1) = 3 bytes
    TArray<uint8> Header;
    if (!RecvBytes(Header, 3))
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to receive challenge response header"));
        bRunning = false;
        AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(false); });
        return;
    }

    uint8 Cmd = Header[0];
    // uint8 Unk = Header[1];
    uint8 Error = Header[2];

    if (Cmd != CMD_AUTH_LOGON_CHALLENGE || Error != 0)
    {
        UE_LOG(LogWowAuth, Error, TEXT("Logon challenge failed: cmd=%d error=%d"), Cmd, Error);
        bRunning = false;
        AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(false); });
        return;
    }

    // Read B (32 bytes)
    TArray<uint8> B_bytes;
    if (!RecvBytes(B_bytes, 32))
    {
        bRunning = false;
        return;
    }

    // g_len (1 byte)
    TArray<uint8> gLenBuf;
    if (!RecvBytes(gLenBuf, 1))
    {
        bRunning = false;
        return;
    }
    uint8 gLen = gLenBuf[0];

    // g (g_len bytes)
    TArray<uint8> g_bytes;
    if (!RecvBytes(g_bytes, gLen))
    {
        bRunning = false;
        return;
    }

    // N_len (1 byte)
    TArray<uint8> NLenBuf;
    if (!RecvBytes(NLenBuf, 1))
    {
        bRunning = false;
        return;
    }
    uint8 NLen = NLenBuf[0];

    // N (N_len bytes)
    TArray<uint8> N_bytes;
    if (!RecvBytes(N_bytes, NLen))
    {
        bRunning = false;
        return;
    }

    // salt (32 bytes)
    TArray<uint8> s_bytes;
    if (!RecvBytes(s_bytes, 32))
    {
        bRunning = false;
        return;
    }

    // crc_salt (16 bytes) - unused
    TArray<uint8> CrcSalt;
    if (!RecvBytes(CrcSalt, 16))
    {
        bRunning = false;
        return;
    }

    // security_flags (1 byte)
    TArray<uint8> SecFlagsBuf;
    if (!RecvBytes(SecFlagsBuf, 1))
    {
        bRunning = false;
        return;
    }
    uint8 SecurityFlags = SecFlagsBuf[0];

    // Read additional security data if flags are set
    if (SecurityFlags & 0x01)
    {
        TArray<uint8> PinData;
        RecvBytes(PinData, 20); // uint32 + 16 bytes
    }
    if (SecurityFlags & 0x02)
    {
        TArray<uint8> MatrixData;
        RecvBytes(MatrixData, 12); // 4 uint8 + uint64
    }
    if (SecurityFlags & 0x04)
    {
        TArray<uint8> TokenData;
        RecvBytes(TokenData, 1);
    }

    UE_LOG(LogWowAuth, Log, TEXT("Received logon challenge response: B=%d bytes, g=%d bytes, N=%d bytes"),
           B_bytes.Num(), g_bytes.Num(), N_bytes.Num());

    // Compute SRP6 credentials
    SRP6 = MakeShared<FWowSRP6>();
    if (!SRP6->ComputeCredentials(Username, Password, B_bytes, g_bytes, N_bytes, s_bytes))
    {
        UE_LOG(LogWowAuth, Error, TEXT("SRP6 credential computation failed"));
        bRunning = false;
        AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(false); });
        return;
    }

    // Send CMD_AUTH_LOGON_PROOF
    TArray<uint8> A_bytes = SRP6->GetA();
    TArray<uint8> M1_bytes = SRP6->GetM1();

    TArray<uint8> ProofPacket;
    ProofPacket.Reserve(1 + 32 + 20 + 20 + 1 + 1);

    // cmd
    ProofPacket.Add(CMD_AUTH_LOGON_PROOF);
    // A (32 bytes)
    ProofPacket.Append(A_bytes);
    // M1 (20 bytes)
    ProofPacket.Append(M1_bytes);
    // crc_hash (20 bytes of zeros)
    for (int32 i = 0; i < 20; ++i)
    {
        ProofPacket.Add(0);
    }
    // number_of_keys
    ProofPacket.Add(0);
    // security_flags
    ProofPacket.Add(0);

    SendBytes(ProofPacket);
    UE_LOG(LogWowAuth, Log, TEXT("Sent logon proof"));
}

void FWowAuthSocket::HandleLogonProof()
{
    // Read response: cmd(1) + error(1) = 2 bytes minimum
    TArray<uint8> Header;
    if (!RecvBytes(Header, 2))
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to receive proof response"));
        bRunning = false;
        AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(false); });
        return;
    }

    uint8 Cmd = Header[0];
    uint8 Error = Header[1];

    if (Cmd != CMD_AUTH_LOGON_PROOF)
    {
        UE_LOG(LogWowAuth, Error, TEXT("Unexpected response cmd=%d"), Cmd);
        bRunning = false;
        AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(false); });
        return;
    }

    if (Error != 0)
    {
        UE_LOG(LogWowAuth, Error, TEXT("Logon proof failed with error: %d"), Error);
        // On error, server sends 2 more bytes (LoginFlags)
        TArray<uint8> ExtraData;
        RecvBytes(ExtraData, 2);
        bRunning = false;
        AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(false); });
        return;
    }

    // Success: read M2(20) + account_flags(4) + survey_id(4) + login_flags(2)
    TArray<uint8> SuccessData;
    if (!RecvBytes(SuccessData, 20 + 4 + 4 + 2))
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to receive proof success data"));
        bRunning = false;
        AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(false); });
        return;
    }

    // Extract M2 (first 20 bytes)
    TArray<uint8> M2;
    M2.SetNumUninitialized(20);
    FMemory::Memcpy(M2.GetData(), SuccessData.GetData(), 20);

    // Verify M2
    if (!SRP6->VerifyM2(M2))
    {
        UE_LOG(LogWowAuth, Error, TEXT("Server proof M2 verification failed!"));
        bRunning = false;
        AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(false); });
        return;
    }

    SessionKey = SRP6->GetSessionKey();
    bAuthComplete = true;

    UE_LOG(LogWowAuth, Log, TEXT("Authentication successful! Session key acquired (%d bytes)"), SessionKey.Num());
    AsyncTask(ENamedThreads::GameThread, [Callback = OnAuthResult, Active = CallbackActive]() { if (*Active) Callback.ExecuteIfBound(true); });
}

void FWowAuthSocket::HandleRealmList()
{
    // Read header: cmd(1) + size(2)
    TArray<uint8> Header;
    if (!RecvBytes(Header, 3))
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to receive realm list header"));
        return;
    }

    uint16 BodySize = Header[1] | (Header[2] << 8);

    // Read the body
    TArray<uint8> Body;
    if (!RecvBytes(Body, BodySize))
    {
        UE_LOG(LogWowAuth, Error, TEXT("Failed to receive realm list body"));
        return;
    }

    // Parse realm list
    // uint32 unused (4 bytes)
    // uint16 realm_count (2 bytes) -- in 3.3.5 this is actually at offset 0 after the unused field
    if (Body.Num() < 6)
    {
        UE_LOG(LogWowAuth, Error, TEXT("Realm list body too small"));
        return;
    }

    int32 Offset = 0;
    // uint32 unused
    Offset += 4;

    // uint16 realm count
    uint16 RealmCount = Body[Offset] | (Body[Offset + 1] << 8);
    Offset += 2;

    TArray<FWowRealmInfo> Realms;
    for (int32 i = 0; i < RealmCount && Offset < Body.Num(); ++i)
    {
        FWowRealmInfo Realm;

        // uint8 type (icon)
        if (Offset >= Body.Num()) break;
        Realm.Type = Body[Offset++];

        // uint8 locked
        if (Offset >= Body.Num()) break;
        Realm.bLocked = Body[Offset++] != 0;

        // uint8 flags
        if (Offset >= Body.Num()) break;
        uint8 Flags = Body[Offset++];
        Realm.Flags = Flags;

        // null-terminated name string
        FString Name;
        while (Offset < Body.Num() && Body[Offset] != 0)
        {
            Name += static_cast<TCHAR>(Body[Offset++]);
        }
        Offset++; // skip null terminator
        Realm.Name = Name;

        // null-terminated address:port string
        FString AddressPort;
        while (Offset < Body.Num() && Body[Offset] != 0)
        {
            AddressPort += static_cast<TCHAR>(Body[Offset++]);
        }
        Offset++; // skip null terminator

        // Parse address:port
        FString AddrStr, PortStr;
        if (AddressPort.Split(TEXT(":"), &AddrStr, &PortStr))
        {
            Realm.Address = AddrStr;
            Realm.Port = FCString::Atoi(*PortStr);
        }
        else
        {
            Realm.Address = AddressPort;
            Realm.Port = 8085;
        }

        // float population (4 bytes, little endian)
        if (Offset + 4 <= Body.Num())
        {
            uint32 Bits = Body[Offset] | (Body[Offset + 1] << 8) | (Body[Offset + 2] << 16) | (uint32(Body[Offset + 3]) << 24);
            float Population = 0.f;
            FMemory::Memcpy(&Population, &Bits, sizeof(Population));
            Realm.Population = FMath::IsFinite(Population) ? Population : 0.f;
        }
        Offset += 4;

        // uint8 char count
        if (Offset < Body.Num())
        {
            Realm.CharacterCount = Body[Offset++];
        }

        // Realm category is the protocol timezone byte, used by the group tabs.
        if (Offset >= Body.Num()) break;
        Realm.Category = Body[Offset++];

        // uint8 realm_id
        if (Offset < Body.Num())
        {
            Realm.RealmId = Body[Offset++];
        }

        // If flag & 0x04, there are extra bytes (specifybuild)
        if (Flags & 0x04)
        {
            Offset += 5; // uint8 major, minor, bugfix + uint16 build
        }

        Realms.Add(Realm);
        UE_LOG(LogWowAuth, Log, TEXT("Realm: %s id=%u (%s:%d) - %d chars"),
               *Realm.Name, Realm.RealmId, *Realm.Address, Realm.Port, Realm.CharacterCount);
    }

    AsyncTask(ENamedThreads::GameThread, [Callback = OnRealmList, Active = CallbackActive, Realms]() { if (*Active) Callback.ExecuteIfBound(Realms); });
}

bool FWowAuthSocket::SendBytes(const TArray<uint8>& Data)
{
    FScopeLock Lock(&SocketLock);
    if (!Socket)
    {
        return false;
    }

    int32 BytesSent = 0;
    return Socket->Send(Data.GetData(), Data.Num(), BytesSent);
}

bool FWowAuthSocket::RecvBytes(TArray<uint8>& OutData, int32 NumBytes)
{
    OutData.SetNumUninitialized(NumBytes);
    int32 TotalRead = 0;
    constexpr double TimeoutSeconds = 30.0;
    double IdleStart = FPlatformTime::Seconds();

    while (TotalRead < NumBytes && bRunning)
    {
        int32 BytesRead = 0;
        {
            FScopeLock Lock(&SocketLock);
            if (!Socket)
            {
                return false;
            }
            if (!Socket->Recv(OutData.GetData() + TotalRead, NumBytes - TotalRead, BytesRead))
            {
                UE_LOG(LogWowAuth, Error, TEXT("Socket recv failed"));
                return false;
            }
        }

        if (BytesRead == 0)
        {
            if (FPlatformTime::Seconds() - IdleStart > TimeoutSeconds)
            {
                UE_LOG(LogWowAuth, Error, TEXT("Socket recv timed out after %.0fs"), TimeoutSeconds);
                return false;
            }
            FPlatformProcess::Sleep(0.01f);
            continue;
        }

        TotalRead += BytesRead;
        IdleStart = FPlatformTime::Seconds();
    }

    return TotalRead == NumBytes;
}
