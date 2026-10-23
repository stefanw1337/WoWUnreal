#pragma once
#include "CoreMinimal.h"
#include "WowSessionState.generated.h"

UENUM(BlueprintType)
enum class EWowSessionState : uint8
{
    Disconnected,
    AuthConnecting, AuthChallengeSent, AuthProofSent, AuthRequestingRealmList, AuthHaveRealmList,
    WorldConnecting, WorldAuthChallengeReceived, WorldAuthSessionSent, WorldAuthenticated,
    WorldRequestingCharList, WorldHaveCharList, WorldEnteringWorld, WorldInGame,
    Error
};

USTRUCT(BlueprintType)
struct WOWNETWORK_API FWowRealmInfo
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString Address;
    UPROPERTY(BlueprintReadOnly) int32 Port = 0;
    UPROPERTY(BlueprintReadOnly) int32 CharacterCount = 0;
    UPROPERTY(BlueprintReadOnly) uint8 Type = 0;
    UPROPERTY(BlueprintReadOnly) int32 RealmId = 0;
    UPROPERTY(BlueprintReadOnly) uint8 Category = 0;
    UPROPERTY(BlueprintReadOnly) uint8 Flags = 0;
    UPROPERTY(BlueprintReadOnly) bool bLocked = false;
    /** Realm list population value (realm list Population column). */
    UPROPERTY(BlueprintReadOnly) float Population = 0.f;
};

USTRUCT(BlueprintType)
struct WOWNETWORK_API FWowCharacterEquipment
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 DisplayId = 0;
    UPROPERTY(BlueprintReadOnly) uint8 InventoryType = 0;
    UPROPERTY(BlueprintReadOnly) int32 EnchantAuraId = 0;
};

USTRUCT(BlueprintType)
struct WOWNETWORK_API FWowCharacterInfo
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int64 Guid = 0;
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) uint8 Race = 0;
    UPROPERTY(BlueprintReadOnly) uint8 Class = 0;
    UPROPERTY(BlueprintReadOnly) uint8 Gender = 0;
    UPROPERTY(BlueprintReadOnly) uint8 Skin = 0;
    UPROPERTY(BlueprintReadOnly) uint8 Face = 0;
    UPROPERTY(BlueprintReadOnly) uint8 HairStyle = 0;
    UPROPERTY(BlueprintReadOnly) uint8 HairColor = 0;
    UPROPERTY(BlueprintReadOnly) uint8 FacialHair = 0;
    UPROPERTY(BlueprintReadOnly) uint8 Level = 0;
    UPROPERTY(BlueprintReadOnly) int32 MapId = 0;
    UPROPERTY(BlueprintReadOnly) int32 ZoneId = 0;
    UPROPERTY(BlueprintReadOnly) FVector Position = FVector::ZeroVector;
    /** r413: SMSG_CHAR_ENUM character flags (0x2000 ghost, 0x400 helm hidden, 0x800 cloak hidden). */
    UPROPERTY(BlueprintReadOnly) int32 CharacterFlags = 0;

    // Equipment: 23 slots as sent by SMSG_CHAR_ENUM
    UPROPERTY(BlueprintReadOnly) TArray<FWowCharacterEquipment> Equipment;
};
