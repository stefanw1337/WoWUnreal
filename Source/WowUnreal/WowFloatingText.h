#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Styling/SlateTypes.h"
class UWidgetComponent;
class STextBlock;
#include "WowFloatingText.generated.h"

USTRUCT(BlueprintType)
struct WOWUNREAL_API FWowFloatingTextInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FString Text;

    UPROPERTY(BlueprintReadWrite)
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(BlueprintReadWrite)
    float Duration = 2.0f;

    UPROPERTY(BlueprintReadWrite)
    float Speed = 100.0f; // Units per second to float upward

    UPROPERTY(BlueprintReadWrite)
    float FontSize = 24.0f;

    /** Shown around the number ("+", " Mana"); Text itself stays the bare number so it can be summed (r279). */
    FString Prefix;
    FString Suffix;
    /** Incoming (heals and resources you receive): float down into the unit instead of up. */
    bool bDownward = false;
    /** World option that turns on summing for this kind of number. */
    FString SumOption = TEXT("combattextsum");
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WOWUNREAL_API UWowFloatingText : public USceneComponent
{
    GENERATED_BODY()

public:
    UWowFloatingText();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Spawn floating text above an actor */
    UFUNCTION(BlueprintCallable)
    static UWowFloatingText* SpawnFloatingText(AActor* TargetActor, const FWowFloatingTextInfo& TextInfo);

    /** Start the floating text animation */
    UFUNCTION(BlueprintCallable)
    void StartFloating(const FWowFloatingTextInfo& TextInfo);

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UWidgetComponent> TextWidgetComponent;
    TSharedPtr<STextBlock> CombatText;
    FSlateFontInfo CombatFont;
    int32 BaseFontSize = 24;
    TWeakObjectPtr<AActor> DamageTarget;
    float ScreenStackOffset = 0.0f;
    float ScreenSideOffset = 0.0f;
    int64 NumericAmount = 0;
    bool bCriticalNumber = false;
    bool bDownward = false;

    bool bIsFloating = false;
    float FloatTimer = 0.0f;
    float FloatDuration = 2.0f;
    float FloatSpeed = 100.0f;
    FVector StartLocation;
    FLinearColor BaseColor = FLinearColor::White;

    void UpdateFloating(float DeltaTime);
};