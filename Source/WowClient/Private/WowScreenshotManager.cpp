#include "WowScreenshotManager.h"
#include "WowScreenshotInput.h"
#include "Mpq/MpqManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowScreenshot, Log, All);

namespace
{
FSlateFontInfo ScreenshotNoticeFont()
{
    // Reuse the original font extracted by WowFontManager from the user's data.
    // Resolve on each notice: the service exists before MPQs/UI initialize.
    static FSlateFontInfo CachedFont;
    static FString CachedPath;
    const FString Path=FPaths::ProjectSavedDir()/TEXT("Fonts/FRIZQT__.TTF");
    if(CachedPath==Path && CachedFont.CompositeFont.IsValid())return CachedFont;
    if(!IFileManager::Get().FileExists(*Path))return FCoreStyle::GetDefaultFontStyle("Bold",22);
    auto Composite=MakeShared<FCompositeFont>();
    FTypefaceEntry Entry;
    Entry.Name=TEXT("FRIZQT__");
    Entry.Font=FFontData(Path,EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    Composite->DefaultTypeface.Fonts.Add(Entry);
    CachedFont=FSlateFontInfo();
    CachedFont.Size=22;
    CachedFont.CompositeFont=Composite;
    CachedFont.TypefaceFontName=Entry.Name;
    CachedFont.OutlineSettings.OutlineSize=1;
    CachedFont.OutlineSettings.OutlineColor=FLinearColor::Black;
    CachedPath=Path;
    return CachedFont;
}
}


void UWowScreenshotManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (IsRunningDedicatedServer() || IsRunningCommandlet() || !FSlateApplication::IsInitialized()) return;
    InputProcessor=MakeShared<FWowScreenshotInput>(
        [WeakThis=TWeakObjectPtr<UWowScreenshotManager>(this)]()
        { return WeakThis.IsValid() && !WeakThis->TakeScreenshot().IsEmpty(); },
        [WeakThis=TWeakObjectPtr<UWowScreenshotManager>(this)]()
        {
            if (OnPhotoModeTap().IsBound() && OnPhotoModeTap().Execute()) return;
            if (WeakThis.IsValid()) WeakThis->TakeScreenshot();
        },
        []() { OnPhotoModeHold().Broadcast(); });
    FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor,0);
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UWowScreenshotManager::TickService));
}

void UWowScreenshotManager::Deinitialize()
{
    if (FSlateApplication::IsInitialized() && InputProcessor.IsValid())
        FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
    InputProcessor.Reset();
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ProcessedHandle);
    if (!PendingFile.IsEmpty() && FScreenshotRequest::GetFilename()==PendingFile) FScreenshotRequest::Reset();
    PendingFile.Empty();
    if (NoticeViewport.IsValid() && NoticeOverlay.IsValid())
        NoticeViewport->RemoveViewportWidgetContent(NoticeOverlay.ToSharedRef());
    NoticeViewport.Reset();NoticeOverlay.Reset();NoticeWidget.Reset();
    Super::Deinitialize();
}

FSimpleMulticastDelegate& UWowScreenshotManager::OnPhotoModeHold()
{
    static FSimpleMulticastDelegate Delegate;
    return Delegate;
}

TDelegate<bool()>& UWowScreenshotManager::OnPhotoModeTap()
{
    static TDelegate<bool()> Delegate;
    return Delegate;
}

FString UWowScreenshotManager::GetScreenshotDir() const
{
    return FMpqManager::FindUserRoot(FPaths::ProjectDir()) / TEXT("Screenshot");
}

FString UWowScreenshotManager::RequestActiveViewport()
{
    UGameInstance* Instance=GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr;
    auto* Service=Instance ? Instance->GetSubsystem<UWowScreenshotManager>() : nullptr;
    return Service ? Service->TakeScreenshot() : FString();
}

FString UWowScreenshotManager::TakeScreenshot()
{
    UGameViewportClient* Viewport=GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
    if (!Viewport || !Viewport->Viewport)
    {
        SetNotice(TEXT("Screenshot failed"));
        return FString();
    }
    AttachNotice();
    return BeginRequest(GetScreenshotDir(),Viewport->Viewport->GetSceneHDREnabled());
}

FString UWowScreenshotManager::BeginRequest(const FString& Directory,bool bHDR)
{
    if (!PendingFile.IsEmpty()) return PendingFile;
    // Never replace an in-flight engine/automation capture.
    if (FScreenshotRequest::IsScreenshotRequested()) return FString();
    SetNotice(TEXT("")); // Also hide the previous success message before a rapid second capture.
    if (!FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory))
    {
        SetNotice(TEXT("Screenshot failed"));
        UE_LOG(LogWowScreenshot,Error,TEXT("Cannot create screenshot directory: %s"),*Directory);
        return FString();
    }
    const FString Base=Directory / FString::Printf(TEXT("WoW_%s"),*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    const TCHAR* Extension=bHDR ? TEXT("exr") : TEXT("png");
    int32 Suffix=0;
    do { PendingFile=FString::Printf(TEXT("%s_%03d.%s"),*Base,Suffix++,Extension); }
    while (IFileManager::Get().FileExists(*PendingFile));
    RequestStarted=FPlatformTime::Seconds();
    ProcessedHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this,&UWowScreenshotManager::ScreenshotProcessed);
    FScreenshotRequest::RequestScreenshot(PendingFile,true,false,bHDR);
    UE_LOG(LogWowScreenshot,Log,TEXT("Screenshot requested: %s"),*PendingFile);
    return PendingFile;
}

void UWowScreenshotManager::ScreenshotProcessed()
{
    if (PendingFile.IsEmpty()) return;
    const FString File=MoveTemp(PendingFile);
    PendingFile.Empty();
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ProcessedHandle);
    ProcessedHandle.Reset();
    // The engine emits this event after writing the image, including failures.
    // Only claim success for a non-empty file that actually exists on disk.
    const bool bSaved=IFileManager::Get().FileSize(*File)>0;
    SetNotice(bSaved ? TEXT("Screenshot saved") : TEXT("Screenshot failed"));
    if(bSaved) { UE_LOG(LogWowScreenshot,Log,TEXT("Screenshot saved: %s"),*File); }
    else { UE_LOG(LogWowScreenshot,Warning,TEXT("Screenshot capture did not produce a file: %s"),*File); }
}

void UWowScreenshotManager::SetNotice(const FString& Text)
{
    NoticeText=Text;
    NoticeExpires=FPlatformTime::Seconds()+3.0;
    if(NoticeWidget.IsValid())
    {
        if(!Text.IsEmpty())NoticeWidget->SetFont(ScreenshotNoticeFont());
        NoticeWidget->SetText(FText::FromString(Text));
        NoticeWidget->SetVisibility(Text.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
    }
}

void UWowScreenshotManager::AttachNotice()
{
    UGameViewportClient* Viewport=GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
    if(!Viewport || NoticeViewport.Get()==Viewport || !FSlateApplication::IsInitialized())return;
    if(NoticeViewport.IsValid() && NoticeOverlay.IsValid())
        NoticeViewport->RemoveViewportWidgetContent(NoticeOverlay.ToSharedRef());
    NoticeOverlay=SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SAssignNew(NoticeWidget,STextBlock).Text(FText::FromString(NoticeText))
            .Font(ScreenshotNoticeFont())
            .ColorAndOpacity(FLinearColor(1.f,.82f,.1f)).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)
            .Visibility(NoticeText.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)];
    Viewport->AddViewportWidgetContent(NoticeOverlay.ToSharedRef(),5000);
    NoticeViewport=Viewport;
}

bool UWowScreenshotManager::TickService(float)
{
    AttachNotice();
    const double Now=FPlatformTime::Seconds();
    if(!PendingFile.IsEmpty() && Now-RequestStarted>15)
    {
        if(FScreenshotRequest::GetFilename()==PendingFile)FScreenshotRequest::Reset();
        ScreenshotProcessed();
    }
    if(!NoticeText.IsEmpty() && Now>NoticeExpires)SetNotice(TEXT(""));
    return true;
}
