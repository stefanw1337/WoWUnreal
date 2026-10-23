#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "WowScreenshotManager.generated.h"

/** One screenshot service for login, gameplay, Lua and rebound keys. */
UCLASS()
class WOWCLIENT_API UWowScreenshotManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable) FString TakeScreenshot();
    UFUNCTION(BlueprintCallable) FString GetScreenshotDir() const;
    static FString RequestActiveViewport();
    /** F12 held: photo mode toggle (bound by the gameplay controller). */
    static FSimpleMulticastDelegate& OnPhotoModeHold();
    /** F12 tapped: returns true when photo mode took the photo itself. */
    static TDelegate<bool()>& OnPhotoModeTap();
private:
    friend class FWowScreenshotLifecycleTest;
    FString BeginRequest(const FString& Directory, bool bHDR);
    void ScreenshotProcessed();
    void SetNotice(const FString& Text);
    bool TickService(float DeltaTime);
    void AttachNotice();
    FString PendingFile;
    FString NoticeText;
    double RequestStarted = 0;
    double NoticeExpires = 0;
    FDelegateHandle ProcessedHandle;
    FTSTicker::FDelegateHandle TickHandle;
    TSharedPtr<class IInputProcessor> InputProcessor;
    TSharedPtr<class SWidget> NoticeOverlay;
    TSharedPtr<class STextBlock> NoticeWidget;
    TWeakObjectPtr<class UGameViewportClient> NoticeViewport;
};
