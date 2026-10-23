#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "WowNameplateWidget.generated.h"

UCLASS()
class WOWUNREAL_API UWowNameplateWidget : public UUserWidget
{
    GENERATED_BODY()
    friend class FWowCombatRegressionTest;

public:
    /** Initialize the nameplate with entity data */
    void SetupNameplate(const FString& EntityName, int32 Health, int32 MaxHealth,
        bool bIsHostile = false, bool bIsPlayer = false);

    /** Replace a temporary/query name without rebuilding the widget. */
    void UpdateName(const FString& EntityName);
    void UpdateSubtitle(const FString& Text);
    void SetReactionColor(const FLinearColor& Color);
    FLinearColor ReactionColor=FLinearColor::Green;

    /** Update health values */
    void UpdateHealth(int32 Health, int32 MaxHealth);
    void SetDisplayOptions(bool ShowName, bool ShowBar);

    /** Update the server-authoritative quest marker shown above the NPC. */
    void UpdateQuestStatus(uint8 QuestStatus);

    /** Level box right of the bar, coloured by difficulty against the player's level (0 = "??"). */
    void UpdateLevel(int32 Level, int32 PlayerLevel);
    /** Small gold "?" left of the name: this unit counts for one of your unfinished quest objectives. */
    void SetQuestObjective(bool bObjective);
    /** Cast bar under the health bar; times in FPlatformTime milliseconds, Icon may be null. */
    void UpdateCast(bool bCasting, const FString& SpellName, class UTexture2D* Icon, double StartMs, double EndMs, bool bChanneling);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** Text block for entity name (optional bind widget) */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> NameText;

    /** Progress bar for health (optional bind widget) */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UProgressBar> HealthBar;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> QuestMarkerText;

private:
    FString CachedName, CachedSubtitle;
    UPROPERTY() TObjectPtr<UTextBlock> SubtitleText;
    int32 CachedHealth=0, CachedMaxHealth=1;
    uint8 CachedQuestStatus=0;
    bool bShowName=true, bShowBar=true;
    UPROPERTY() TObjectPtr<class USizeBox> HealthBox;
    UPROPERTY() TObjectPtr<UTextBlock> HealthValueText;
    UPROPERTY() TObjectPtr<UTextBlock> LevelText;
    UPROPERTY() TObjectPtr<UTextBlock> ObjectiveIcon;
    bool bQuestObjective=false;
    UPROPERTY() TObjectPtr<class USizeBox> CastBox;
    UPROPERTY() TObjectPtr<UProgressBar> CastBar;
    UPROPERTY() TObjectPtr<UTextBlock> CastText;
    UPROPERTY() TObjectPtr<class UImage> CastIcon;
    UPROPERTY() TObjectPtr<class UTexture2D> CastIconTexture;
    int32 CachedLevel=-1, CachedPlayerLevel=0;
    bool bCasting=false, bCastChanneling=false;
    double CastStartMs=0.0, CastEndMs=0.0;
    FString CastSpellName;
    bool bIsHostileEntity = false;
    bool bIsPlayerEntity = false;
};
