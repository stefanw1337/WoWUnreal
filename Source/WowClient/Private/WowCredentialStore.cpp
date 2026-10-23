#include "WowCredentialStore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <dpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowCred, Log, All);

FString UWowCredentialStore::GetFilePath() const
{
    FString SettingsFile;
    if (FParse::Value(FCommandLine::Get(), TEXT("GameUserSettingsINI="), SettingsFile)
        && !SettingsFile.IsEmpty())
    {
        return FPaths::Combine(FPaths::GetPath(FPaths::ConvertRelativePathToFull(SettingsFile)),
            TEXT("WowCredentials.json"));
    }
    // Development and packaged launchers share the same portable profile store.
    FString SharedDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("dist/User/Settings"));
    if (!FPaths::FileExists(FPaths::Combine(SharedDir, TEXT("GameUserSettings.ini"))))
        SharedDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("dist/Settings"));
    if (FPaths::FileExists(FPaths::Combine(SharedDir, TEXT("GameUserSettings.ini"))))
        return FPaths::Combine(SharedDir, TEXT("WowCredentials.json"));
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WowCredentials.json"));
}

// Windows owns the key; never embed an encryption key in the executable.
static bool ProtectPassword(const FString& Plain, FString& Sealed)
{
    if (Plain.IsEmpty()) { Sealed.Empty(); return true; }
#if PLATFORM_WINDOWS
    DATA_BLOB Input{static_cast<DWORD>(Plain.Len() * sizeof(TCHAR)),
        reinterpret_cast<BYTE*>(const_cast<TCHAR*>(*Plain))};
    DATA_BLOB Output{};
    if (!CryptProtectData(&Input, L"WowUnreal saved login", nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &Output)) return false;
    Sealed = FBase64::Encode(Output.pbData, Output.cbData);
    LocalFree(Output.pbData);
    return true;
#else
    // A platform credential vault must be implemented before saving passwords.
    return false;
#endif
}

FString UWowCredentialStore::ResolvePassword(const FWowCredential& Credential)
{
    if (!Credential.Password.IsEmpty()) return Credential.Password;
    if (Credential.ProtectedPassword.IsEmpty()) return FString();
#if PLATFORM_WINDOWS
    TArray<uint8> Bytes;
    if (!FBase64::Decode(Credential.ProtectedPassword, Bytes)) return FString();
    DATA_BLOB Input{static_cast<DWORD>(Bytes.Num()), Bytes.GetData()}, Output{};
    if (!CryptUnprotectData(&Input, nullptr, nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &Output))
    {
        UE_LOG(LogWowCred, Warning, TEXT("Saved password unavailable for this Windows account; re-enter it to sign in."));
        return FString();
    }
    FString Plain;
    if (Output.cbData % sizeof(TCHAR) == 0)
        Plain = FString(Output.cbData / sizeof(TCHAR), reinterpret_cast<TCHAR*>(Output.pbData));
    SecureZeroMemory(Output.pbData, Output.cbData);
    LocalFree(Output.pbData);
    return Plain;
#else
    return FString();
#endif
}

static void ClearPassword(FString& Password)
{
    if (!Password.IsEmpty()) FMemory::Memzero(Password.GetCharArray().GetData(), Password.GetCharArray().Num() * sizeof(TCHAR));
    Password.Empty();
}

// Read-only migration of the old, insecure format. Never write it again.
static FString DeobfuscatePassword(const FString& Encoded)
{
	TArray<uint8> Bytes;
	if (!FBase64::Decode(Encoded, Bytes)) return FString();
	FString Result;
	Result.Reserve(Bytes.Num());
	for (int32 i = 0; i < Bytes.Num(); ++i)
	{
		Result.AppendChar(static_cast<TCHAR>(Bytes[i] ^ 0xA5 ^ (i & 0xFF)));
	}
	return Result;
}

bool UWowCredentialStore::LoadCredentials()
{
	FString Json;
    const FString TargetPath = GetFilePath();
    const FString LegacyPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WowCredentials.json"));
    // Never replace an existing shared store, including an intentionally empty one.
    const bool bMigrate = !FPaths::FileExists(TargetPath)
        && !FPaths::IsSamePath(TargetPath, LegacyPath) && FPaths::FileExists(LegacyPath);
    if (!FFileHelper::LoadFileToString(Json, *(bMigrate ? LegacyPath : TargetPath))) return false;
	TSharedPtr<FJsonValue> V;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(R, V) || !V.IsValid()) return false;

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!V->TryGetArray(Arr) || !Arr) return false;

	Creds.Empty();
    bool bUpgrade = bMigrate;
	for (const TSharedPtr<FJsonValue>& I : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!I->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid()) continue;
		const TSharedPtr<FJsonObject>& O = *ObjPtr;

		FWowCredential C;
		O->TryGetStringField(TEXT("alias"), C.Alias);
		O->TryGetStringField(TEXT("username"), C.Username);
		O->TryGetStringField(TEXT("server"), C.ServerAddress);
		O->TryGetNumberField(TEXT("port"), C.AuthPort);
        O->TryGetNumberField(TEXT("game_version"), C.GameVersion);
        if (C.GameVersion < 0 || C.GameVersion > 2) C.GameVersion = 2;
		O->TryGetBoolField(TEXT("default"), C.bIsDefault);

        O->TryGetStringField(TEXT("website"), C.WebsiteUrl);
        O->TryGetStringField(TEXT("account_url"), C.AccountUrl);
        O->TryGetStringField(TEXT("terms_url"), C.TermsUrl);
        O->TryGetStringField(TEXT("data_path"), C.DataPath);
        FString EncodedPw;
        if (O->TryGetStringField(TEXT("pw_dpapi_v1"), C.ProtectedPassword))
        {
            // Keep only sealed data until this profile is selected for login.
        }
        else
        {
            if (O->TryGetStringField(TEXT("pw"), EncodedPw)) C.Password = DeobfuscatePassword(EncodedPw);
            else O->TryGetStringField(TEXT("password"), C.Password);
            if (!ProtectPassword(C.Password, C.ProtectedPassword))
            {
                UE_LOG(LogWowCred, Error, TEXT("Could not protect legacy saved password; original store was left unchanged."));
                ClearPassword(C.Password);
                Creds.Empty();
                bProtectionFailed = true;
                return false;
            }
            ClearPassword(C.Password);
            bUpgrade = true;
        }

		Creds.Add(C);
	}
    if (bUpgrade && !SaveCredentials())
        UE_LOG(LogWowCred, Error, TEXT("Could not persist encrypted profile upgrade; original store was left unchanged."));
    UE_LOG(LogWowCred, Log, TEXT("Loaded %d saved server profiles"), Creds.Num());
    return true;
}

bool UWowCredentialStore::SaveCredentials()
{
    if (bProtectionFailed) return false;
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FWowCredential& C : Creds)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("alias"), C.Alias);
		O->SetStringField(TEXT("username"), C.Username);
		O->SetStringField(TEXT("server"), C.ServerAddress);
		O->SetNumberField(TEXT("port"), C.AuthPort);
        O->SetNumberField(TEXT("game_version"), C.GameVersion);
		O->SetBoolField(TEXT("default"), C.bIsDefault);
        O->SetStringField(TEXT("website"), C.WebsiteUrl);
        O->SetStringField(TEXT("account_url"), C.AccountUrl);
        O->SetStringField(TEXT("terms_url"), C.TermsUrl);
        if (!C.DataPath.IsEmpty()) O->SetStringField(TEXT("data_path"), C.DataPath);
        FString Sealed = C.ProtectedPassword;
        if (!C.Password.IsEmpty() && !ProtectPassword(C.Password, Sealed))
        {
            UE_LOG(LogWowCred, Error, TEXT("Password protection failed; profiles were not overwritten."));
            return false;
        }
        O->SetStringField(TEXT("pw_dpapi_v1"), Sealed);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Arr, W);
    const FString TargetPath = GetFilePath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(TargetPath), true);
    const FString TempPath = TargetPath + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(Out, *TempPath)) return false;
    return IFileManager::Get().Move(*TargetPath, *TempPath, true, true);
}

void UWowCredentialStore::AddCredential(const FWowCredential& C)
{
    FWowCredential Sealed = C;
    if (!C.Password.IsEmpty())
    {
        if (!ProtectPassword(C.Password, Sealed.ProtectedPassword))
        {
            ClearPassword(Sealed.Password);
            bProtectionFailed = true;
            UE_LOG(LogWowCred, Error, TEXT("Could not protect saved login; profile was not changed."));
            return;
        }
        ClearPassword(Sealed.Password);
    }
    for (FWowCredential& E : Creds) { if (E.Alias == C.Alias && E.GameVersion == C.GameVersion) { E = MoveTemp(Sealed); return; } }
    Creds.Add(MoveTemp(Sealed));
}

void UWowCredentialStore::RemoveCredential(const FString& A, int32 Version) { Creds.RemoveAll([&](const FWowCredential& C){ return C.Alias == A && C.GameVersion == Version; }); }

FWowCredential UWowCredentialStore::GetDefaultCredential(int32 Version) const
{
    FWowCredential Result;
    Result.GameVersion = Version;
    for (const FWowCredential& C : Creds) if (C.GameVersion == Version) { Result = C; break; }
    for (const FWowCredential& C : Creds) if (C.GameVersion == Version && C.bIsDefault) { Result = C; break; }
    Result.Password = ResolvePassword(Result);
    return Result;
}
