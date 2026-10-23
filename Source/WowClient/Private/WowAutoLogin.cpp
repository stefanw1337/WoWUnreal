#include "WowAutoLogin.h"
#include "WowCredentialStore.h"
#include "WowConnectionManager.h"
#include "WowSessionState.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowAutoLogin, Log, All);

void UWowAutoLogin::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Parse command-line flags
    const TCHAR* CmdLine = FCommandLine::Get();

    bAutoLoginRequested = FParse::Param(CmdLine, TEXT("autologin"));

    FParse::Value(CmdLine, TEXT("-account="), AccountAlias);

    if (bAutoLoginRequested)
    {
        UE_LOG(LogWowAutoLogin, Log, TEXT("Autologin requested (account alias: '%s')"), *AccountAlias);
    }
}

void UWowAutoLogin::TryAutoLogin()
{
    if (!bAutoLoginRequested)
    {
        UE_LOG(LogWowAutoLogin, Log, TEXT("Autologin not requested, skipping"));
        return;
    }

    if (AutoLoginConnMgr)
    {
        UE_LOG(LogWowAutoLogin, Log, TEXT("Autologin already started"));
        return;
    }

    // Load credentials
    UWowCredentialStore* CredStore = NewObject<UWowCredentialStore>(this);
    if (!CredStore->LoadCredentials())
    {
        UE_LOG(LogWowAutoLogin, Error, TEXT("Failed to load credentials for autologin"));
        return;
    }

    // Find the credential to use
    FWowCredential Cred;
    if (AccountAlias.IsEmpty())
    {
        Cred = CredStore->GetDefaultCredential();
    }
    else
    {
        bool bFound = false;
        for (const FWowCredential& C : CredStore->GetAllCredentials())
        {
            if (C.Alias.Equals(AccountAlias, ESearchCase::IgnoreCase))
            {
                Cred = C;
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            UE_LOG(LogWowAutoLogin, Error, TEXT("Credential alias '%s' not found"), *AccountAlias);
            return;
        }
    }

    Cred.Password = UWowCredentialStore::ResolvePassword(Cred);
    if (Cred.Username.IsEmpty() || Cred.Password.IsEmpty())
    {
        UE_LOG(LogWowAutoLogin, Error, TEXT("Credential '%s' has empty username or password"), *Cred.Alias);
        return;
    }

    UE_LOG(LogWowAutoLogin, Log, TEXT("Autologin: using account '%s' (%s@%s:%d)"),
           *Cred.Alias, *Cred.Username, *Cred.ServerAddress, Cred.AuthPort);

    // Create connection manager
    UWowConnectionManager* ConnMgr = NewObject<UWowConnectionManager>(GetGameInstance());
    ConnMgr->AddToRoot(); // prevent GC during async operations

    // Bind delegates for the auto-login flow
    ConnMgr->OnRealmList.AddDynamic(this, &UWowAutoLogin::OnRealmListReceived);
    ConnMgr->OnCharacterList.AddDynamic(this, &UWowAutoLogin::OnCharacterListReceived);
    ConnMgr->OnError.AddDynamic(this, &UWowAutoLogin::OnAutoLoginError);
    ConnMgr->OnStateChanged.AddDynamic(this, &UWowAutoLogin::OnStateChanged);

    AutoLoginConnMgr = ConnMgr;

    // Start login
    ConnMgr->Login(Cred.Username, Cred.Password, Cred.ServerAddress, Cred.AuthPort);
}

void UWowAutoLogin::OnRealmListReceived(const TArray<FWowRealmInfo>& Realms)
{
    if (!AutoLoginConnMgr) return;

    if (Realms.Num() == 0)
    {
        UE_LOG(LogWowAutoLogin, Error, TEXT("Autologin: no realms available"));
        return;
    }

    UE_LOG(LogWowAutoLogin, Log, TEXT("Autologin: selecting realm 0 ('%s')"), *Realms[0].Name);
    AutoLoginConnMgr->SelectRealm(0);
}

void UWowAutoLogin::OnStateChanged(EWowSessionState NewState)
{
    UE_LOG(LogWowAutoLogin, Log, TEXT("Autologin: state changed to %d"), static_cast<int32>(NewState));

    if (!AutoLoginConnMgr) return;

    // When world auth is complete, request character list
    if (NewState == EWowSessionState::WorldAuthenticated)
    {
        UE_LOG(LogWowAutoLogin, Log, TEXT("Autologin: world authenticated, requesting character list"));
        AutoLoginConnMgr->RequestCharacterList();
    }
}

void UWowAutoLogin::OnCharacterListReceived(const TArray<FWowCharacterInfo>& Characters)
{
    if (!AutoLoginConnMgr) return;

    if (Characters.Num() == 0)
    {
        UE_LOG(LogWowAutoLogin, Error, TEXT("Autologin: no characters on this account"));
        return;
    }

    // Enter world with the first character
    const FWowCharacterInfo& Char = Characters[0];
    UE_LOG(LogWowAutoLogin, Log, TEXT("Autologin: entering world with '%s' (Level %d, GUID %llu)"),
           *Char.Name, Char.Level, Char.Guid);
    AutoLoginConnMgr->EnterWorld(Char.Guid);
}

void UWowAutoLogin::OnAutoLoginError(const FString& Msg)
{
    UE_LOG(LogWowAutoLogin, Error, TEXT("Autologin error: %s"), *Msg);

    if (AutoLoginConnMgr)
    {
        AutoLoginConnMgr->RemoveFromRoot();
        AutoLoginConnMgr = nullptr;
    }
}
