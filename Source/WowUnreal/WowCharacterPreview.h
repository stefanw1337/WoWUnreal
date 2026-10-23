#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowSessionState.h"
#include "WowCharacterPreview.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UDirectionalLightComponent;
class UPointLightComponent;
class AWowWorldManager;

/** Selects whether the preview owns a full glue-screen environment or an isolated UI paper doll. */
enum class EWowCharacterPreviewMode : uint8
{
    CharacterSelection,
    CharacterCreation,
    PaperDoll
};

/**
 * Renders a character model to a render target for UI preview.
 * Placed at a distant location to isolate from the main world.
 */
UCLASS()
class WOWUNREAL_API AWowCharacterPreview : public AActor
{
    GENERATED_BODY()
public:
    AWowCharacterPreview();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

    /** Initialize the capture system. Paper dolls render only the character on a transparent target. */
    void Setup(AWowWorldManager* InWorldManager, int32 Width = 512, int32 Height = 768,
        EWowCharacterPreviewMode InMode = EWowCharacterPreviewMode::CharacterSelection);

    /** Show a character by race/gender (from character info) */
    void ShowCharacter(uint8 RaceId, uint8 GenderId);

    /** Show a complete character-list appearance, including available equipment. */
    void ShowCharacter(const FWowCharacterInfo& CharacterInfo);

    /** Dressing room (r300): where this rig sits and which lighting channel (1 or 2) its lights and
     *  model use, so two previews (character sheet + dressing room) don't light each other.
     *  Call before Setup. */
    void SetRig(const FVector& Offset, int32 LightingChannel);

    /** r315: other players (inspect) use the classic model like in the world; building an HD
     *  character costs ~1 s on the game thread. Call before Setup. */
    void SetAllowHDModel(bool bAllow) { bAllowHDModel = bAllow; }

    /** Dressing room mount preview (r300): show a creature display alone, standing. */
    void ShowCreatureDisplay(uint32 CreatureDisplayId);

    /** Show an animated racial backdrop without inventing a roster character. */
    void ShowEmptySelectionScene();

    /** Rotate the displayed character around its vertical axis. */
    void RotateCharacter(float YawDeltaDegrees);
    /** r262: mouse-wheel zoom (positive = closer), 1x to 3x (the capture's field of view). */
    void AddZoom(float Steps);

    /** Suspend capture work while the owning UI panel is closed. */
    void SetPreviewActive(bool bActive) { bPreviewActive = bActive; }

    /** Clear the current preview */
    void ClearCharacter();

    /** Manually capture the scene */
    void CaptureNow();

    /** Get the render target texture for Slate display */
    UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

    /** Play an M2 animation directly on the preview actor for local visual verification. */
    bool PlayAnimationForTesting(int32 AnimationId, bool bLooping = true);

private:
    // Rebuild the shown character when Options > Experimental "HD character model" changes.
    FWowCharacterInfo LastShownCharacterInfo;
    bool bHasShownCharacterInfo = false;
    bool bShownWithHDOption = false;
    void PositionCamera(uint8 RaceId);
    void UpdateCharacterLighting();
    void GroundCharacter();
    bool ShowRaceScene(uint8 RaceId, uint8 ClassId);
    void ClearRaceScene();

    UPROPERTY()
    TObjectPtr<USceneCaptureComponent2D> CaptureComponent;

    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> RenderTarget;

    UPROPERTY()
    TObjectPtr<UDirectionalLightComponent> KeyLight;

    UPROPERTY()
    TObjectPtr<UPointLightComponent> FillLight;

    UPROPERTY()
    TObjectPtr<UPointLightComponent> RimLight;

    UPROPERTY()
    TObjectPtr<AActor> CurrentCharacterActor;

    /** MPQ-backed full-screen racial character-select environment. */
    UPROPERTY()
    TObjectPtr<AActor> CurrentRaceSceneActor;

    UPROPERTY()
    TObjectPtr<AWowWorldManager> WorldManager;

    bool bPreviewActive = true;
    EWowCharacterPreviewMode PreviewMode = EWowCharacterPreviewMode::CharacterSelection;
    float CaptureAccumulator = 0.0f;
    FString CurrentRaceScenePath;
    FVector CharacterAnchor = PREVIEW_OFFSET;
    float CharacterFacingYaw = 180.0f;
    float CharacterSceneScale = 1.0f;
    float ZoomFactor = 1.0f; // r262
    FVector AuthoredCameraLocation = FVector::ZeroVector;
    bool bGroundingPending = false;
    bool bUsesOriginalDraeneiPlatform = false;
    FVector RigOffset = PREVIEW_OFFSET;
    int32 RigLightingChannel = 1;
    bool bAllowHDModel = true;
    void ApplyRigLighting(AActor* Actor) const;

    /** Offset from world origin for isolation */
    static const FVector PREVIEW_OFFSET;
};
