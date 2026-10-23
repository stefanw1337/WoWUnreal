#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowSessionState.h"
#include "WowExpansionTypes.h"
#include "WowOfflineCharacterStore.h"
#include "RenderCommandFence.h"
#include "WowLoginController.generated.h"

class UWowConnectionManager;
class AWowWorldManager;
class AWowAudioManager;
class UWowUIManager;
class FWowCinematicManager;
class AWowCharacterPreview;
class SWowLoginWidget;
class SWowRealmSelectWidget;
class SWowCharacterSelectWidget;
class SWowCharacterCreateWidget;
class SWowLoadingScreen;
class SWowLoginOptions;
class UTexture2D;
class FMpqManager;
class FWowAssetCache;
class ACameraActor;
class ADirectionalLight;

UCLASS()
class WOWUNREAL_API AWowLoginController : public AActor
{
    GENERATED_BODY()
public:
    AWowLoginController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    void StartLoginFlow();

    /** Opens character creation without connecting, for local visual verification. */
    void ShowCharacterCreatePreviewForTesting();

    /** Opens character selection without connecting, for local visual verification. */
    void ShowCharacterSelectPreviewForTesting();

    /** Opens the loading screen without connecting, for local visual verification. */
    void ShowLoadingScreenForTesting();

    /** Enters the offline world through the real loading screen flow. */
    void StartOfflineWorldLoadingForTesting();

    /** Return an offline session to its persistent local character roster. */
    void ReturnToOfflineCharacterSelect();
    bool CanEditOfflineRoster() const;
    void SaveOfflineLocation(const FVector& Feet,float Orientation);
    TSharedRef<SWowLoadingScreen> CreateTransferLoadingScreen(uint32 MapId);
    void ConfigureLoadingBar(TSharedRef<SWowLoadingScreen> Screen);

    UPROPERTY()
    TObjectPtr<UWowConnectionManager> ConnectionManager;

    /** Set by GameMode so we can enable streaming on world entry */
    UPROPERTY()
    TObjectPtr<AWowWorldManager> WorldManager;

private:
    friend class FWowLoginSceneRecoveryTest;
    bool SpawnLoginGlueScene(EWowExpansion Expansion, FMpqManager* Mpq, FWowAssetCache* Cache);
    UFUNCTION() void OnStateChanged(EWowSessionState NewState);
    UFUNCTION() void OnRealmList(const TArray<FWowRealmInfo>& Realms);
    UFUNCTION() void OnCharacterList(const TArray<FWowCharacterInfo>& Characters);
    UFUNCTION() void OnError(const FString& Msg);
    UFUNCTION() void OnWorldDisconnected(const FString& Msg);
    UFUNCTION() void OnCharCreateResult(uint8 ResultCode);
    UFUNCTION() void OnLogoutComplete();
    /** Clears the world weather (rain, lens drops, puddles, thunder) when back at the glue screens. */
    void ResetWorldWeatherForGlue();
    UFUNCTION() void OnLoginVerifyWorld(uint32 MapId, float X, float Y, float Z, float Orientation);

    void HandleLoginSubmit(const FString& Server, int32 Port, const FString& User, const FString& Pass);
    void HandleRealmSelected(int32 Index);
    void HandleCharacterSelected(int64 Guid);
    void HandleCreateCharacterRequest();
    void HandleDeleteCharacterRequest(int64 Guid);
    void HandleCharacterCreated(const FString& Name, uint8 Race, uint8 Class, uint8 Gender,
        uint8 Skin, uint8 Face, uint8 HairStyle, uint8 HairColor, uint8 FacialHair);
    void HandleBackToCharSelect();
    void HandleBackFromCharacterSelect();
    void HandleChangeRealm();
    void ShowConnectingDialog();
    void CloseGlueDialog();
    void HandleExpansionChanged(uint8 ExpansionIndex);
    void HandleCharacterHighlighted(uint8 Race, uint8 Gender);
    void HandleCharacterInfoHighlighted(const FWowCharacterInfo& CharacterInfo);
    void HandleCharacterPreviewRotated(float YawDeltaDegrees);
	void LoadOfflineRoster();
	void RefreshOfflineCharacterInfos();
	void PrepareOfflinePlayerState();
	void BeginOfflineWorldLoading();
	void PopulateOfflineStarterEquipment(FWowCharacterInfo& CharacterInfo) const;
	void CloseStartupGameplayPanels() const;
	AWowAudioManager* GetOrCreateAudioManager();
	void PlayGlueUiSound(const FName& SoundName);
	void SaveOnlineLoginBookmark(const FString& Server, int32 Port, const FString& User, const FString& Pass);
	void ShowLoginOptions();
	void HideLoginOptions();
	void HandleLoginExitRequested();

    /** Initialize world systems when entering the game world */
    void InitializeWorldSystems();

    /** r233: download the server alert (http://<patchlist>/alert) into character select's news panel. */
    void RequestServerAlert(TWeakPtr<SWowCharacterSelectWidget> Widget);

    /** Initialize and start cinematic playback */
    void InitializeCinematics();
    void TryInitializeLoginBackgroundWhenReady();
    void UpdateStartupHandoff();
    FRenderCommandFence StartupRenderFence;
    double StartupWaitStarted = 0;
    double StartupVisualsReadyAt = 0;
    uint64 StartupVisualsReadyFrame = 0;
    bool bStartupHandoffComplete = false;
    bool bStartupFenceStarted = false;
    bool TryApplyMpqLoginBackground(EWowExpansion Expansion);
    bool TrySpawnLoginGlueScene(EWowExpansion Expansion);
    void ClearLoginGlueScene();
    UTexture2D* LoadFirstMpqBlpTexture(const TArray<FString>& Paths, const FString& LogContext) const;
    void LogWotlkGlueSceneAvailability(FMpqManager* Mpq) const;

    /** Show the loading screen and start terrain loading around spawn */
    void ShowLoadingScreen();

    /** Show loading screen for entering the world */
    void ShowLoadingScreen(uint32 MapId);
    UTexture2D* ResolveLoadingScreenTexture(uint32 MapId, bool bAllowFallback) const;
    UTexture2D* LoadLoadingScreenTexture(const FString& SourcePath) const;
    void UpdateLoadingScreenBackground(uint32 MapId, bool bAllowFallback);

    /** Called each tick while loading — checks progress and transitions to game */
    void UpdateLoadingProgress(float DeltaTime);

    /** Finalize world entry: teleport pawn, hide loading screen, enable input */
    void FinalizeWorldEntry();
    bool TrySnapPawnToGround(APawn* Pawn, bool bLogFailure);
    void SuspendPawnUntilGroundReady(APawn* Pawn);
    void RestorePawnGameplayMovement(APawn* Pawn);
    void TryFinalizeGroundSnap();

    void ShowLoginScreen();
    void ShowRealmSelectScreen(const TArray<FWowRealmInfo>& Realms);
    void ShowCharacterSelectScreen(const TArray<FWowCharacterInfo>& Characters);
    void ShowCharacterCreateScreen();
    void ClearCurrentScreen();
    void SetStatusText(const FString& Text);
    void SuppressDebugMessagesForGlueScreen();

    TSharedPtr<SWowLoginWidget> LoginWidget;
    TSharedPtr<SWowRealmSelectWidget> RealmSelectWidget;
    TSharedPtr<SWowCharacterSelectWidget> CharSelectWidget;
    TSharedPtr<SWowCharacterCreateWidget> CharCreateWidget;
    TSharedPtr<SWowLoadingScreen> LoadingScreenWidget;
	TSharedPtr<SWowLoginOptions> LoginOptionsWidget;
    FTimerHandle ConnectionTimeoutHandle;
    TSharedPtr<SWidget> GlueDialog;
    TSharedPtr<class STextBlock> ConnectionStatusLabel;
    TSharedPtr<class STextBlock> ConnectionButtonLabel;
    FString RealmPreferenceKey;
    int32 PendingRealmId = INDEX_NONE;
    // Per-server game data: chosen after a successful login, applied before the realm list.
    bool PrepareServerDataSwitch();
    void RunServerDataSwitch();
    FString LoginServerAddress;
    int32 LoginServerPort = 0;
    int32 LoginServerVersion = 2;
    FString PendingDataPath;
    int32 PendingDataSwitchFrames = 0;
    int32 PendingRealmIndex = INDEX_NONE;
    int32 ActiveRealmIndex = INDEX_NONE;
    TSharedPtr<SWidget> CurrentWidget;
    TSharedPtr<SWidget> LoadingWidget; // Separate from CurrentWidget so it overlays
    bool bReturningFromLogout = false;
	bool bOfflineFlow = false;
	bool bLoginMusicStarted = false;
	TArray<FWowOfflineCharacterRecord> OfflineCharacters;
	TArray<FWowCharacterInfo> OfflineCharacterInfos;

    /** Cinematic manager for login screen backgrounds (owned, cleaned up in EndPlay) */
    FWowCinematicManager* CinematicManager = nullptr;
    TSharedPtr<FMpqManager> LoginOriginalData;
    TSharedPtr<FWowAssetCache> LoginOriginalCache;

    /** Runtime MPQ-backed 3D glue scene actors for the login screen. */
    UPROPERTY()
    TObjectPtr<AActor> LoginGlueActor;
    UPROPERTY()
    TObjectPtr<ACameraActor> LoginGlueCamera;
    FVector LoginBackgroundPlate = FVector::ZeroVector;
    void UpdateLoginBackgroundFit();
    UPROPERTY()
    TObjectPtr<ADirectionalLight> LoginGlueLight;

    /** Character preview for 3D model display in character select */
    UPROPERTY()
    TObjectPtr<AWowCharacterPreview> CharacterPreview;

    /** Loading state */
    bool bLoginBackgroundInitialized = false;
    bool bWaitingForInitialLoad = false;
    FVector PendingSpawnPosition = FVector::ZeroVector;
    float PendingSpawnOrientation = 0.0f;
    bool bHasPendingSpawn = false;
    bool bPendingGroundSnap = false;
    bool bDeferredSpawnAppliedForEntry = false;
    int32 GroundValidationStableFrames = 0;
    float InitialLoadCompleteSettleTime = 0.0f;
    /** Require this much completely quiet nearby streaming time before revealing gameplay. */
    float InitialLoadSettleDuration = 2.0f;
    float WorldEntryWarmupElapsed = 0.0f;
    float WorldEntryWarmupTimeout = 45.0f;
    double WorldEntryWarmupStartedAtSeconds = 0.0;
    uint64 WorldEntryWarmupStartPass = 0;
    uint64 LastWorldEntryObjectRevision = 0;
    uint64 LastWorldEntryEntityRevision = 0;
    bool bEntryPresentationPrepared = false;
    bool bInitialLoadSettleLogged = false;
    bool bSavedOnScreenDebugState = false;
    bool bPreviousOnScreenDebugEnabled = true;
    bool bPreviousGlobalScreenMessagesEnabled = true;
    bool bHasSelectedCharacterInfo = false;
    FWowCharacterInfo SelectedCharacterInfo;
    int64 SelectedCharacterGuid = 0;

    /** Character creation state */
    bool bCharCreateSent = false;
    bool bAutoEnterAfterCreate = false;

    /** Handle for login verify world delegate binding */
    FDelegateHandle LoginVerifyWorldHandle;

    /** Delays heavy offline initialization long enough for the loading screen to render. */
    FTimerHandle OfflineWorldInitTimerHandle;
};
