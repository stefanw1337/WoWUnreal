#pragma once
#include "CoreMinimal.h"
#include "WowCredentialStore.generated.h"

USTRUCT(BlueprintType)
struct WOWCLIENT_API FWowCredential
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Alias;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Username;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString ServerAddress;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AuthPort = 3724;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bIsDefault = false;
    // 0=Vanilla, 1=TBC, 2=WotLK. Legacy bookmarks belong to WotLK.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 GameVersion = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString WebsiteUrl;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString AccountUrl;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString TermsUrl;
    // Game-data folder used after logging in to this server; empty = the default data folder.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DataPath;
    FString Password;
    FString ProtectedPassword; // DPAPI blob; never a plaintext UI bookmark cache.
};

UCLASS(BlueprintType)
class WOWCLIENT_API UWowCredentialStore : public UObject
{
    GENERATED_BODY()
public:
    static FString ResolvePassword(const FWowCredential& Credential);
    UFUNCTION(BlueprintCallable) bool LoadCredentials();
    UFUNCTION(BlueprintCallable) bool SaveCredentials();
    UFUNCTION(BlueprintCallable) void AddCredential(const FWowCredential& Cred);
    UFUNCTION(BlueprintCallable) void RemoveCredential(const FString& Alias, int32 GameVersion = 2);
    UFUNCTION(BlueprintCallable) FWowCredential GetDefaultCredential(int32 GameVersion = 2) const;
    UFUNCTION(BlueprintCallable) TArray<FWowCredential> GetAllCredentials() const { return Creds; }
private:
    FString GetFilePath() const;
    TArray<FWowCredential> Creds;
    bool bProtectionFailed = false;
};
