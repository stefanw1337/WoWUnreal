// Bag management: destroy-item confirmation (drag out of bags into the world) and
// the held-item cursor icon. Kept out of WowGameplayController.cpp on purpose.
#include "WowGameplayController.h"
#include "WowUiSurfaces.h"
#include "Widgets/SWowDestroyItemDialog.h"
#include "WowConnectionManager.h"
#include "WowItemDestroyPolicy.h"
#include "WowUIManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/AppStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowBags, Log, All);

namespace
{
// Same z-order band as the duel invite; the software cursor sits at 50000.
constexpr int32 DestroyDialogZOrder = 220;
constexpr int32 CursorItemIconZOrder = 49990;
}

bool AWowGameplayController::IsDestroyItemDialogVisible() const
{
    return DestroyItemViewportWidget.IsValid() && DestroyItemViewportWidget->GetVisibility().IsVisible();
}

void AWowGameplayController::ShowDestroyItemDialog(const TArray<FString>& Args)
{
    // Args: name, quality, mode (1 Yes/No, 2 typed DELETE), count, item entry.
    if (!ConnectionManager || !GEngine || !GEngine->GameViewport) return;
    // Only one request at a time; the open dialog already owns the held item.
    if (IsDestroyItemDialogVisible() || !ConnectionManager->HasCursorItemPayload()) return;

    const FString Name = Args.IsValidIndex(0) ? Args[0] : FString();
    const uint32 Quality = Args.IsValidIndex(1) ? static_cast<uint32>(FCString::Atoi(*Args[1])) : 0;
    const int32 ModeValue = Args.IsValidIndex(2) ? FCString::Atoi(*Args[2])
        : static_cast<int32>(Quality >= FWowItemDestroyPolicy::QualityEpic ? EWowDestroyConfirm::TypeDelete : EWowDestroyConfirm::Confirm);
    const int32 Count = Args.IsValidIndex(3) ? FCString::Atoi(*Args[3]) : 1;
    const uint32 Entry = Args.IsValidIndex(4) ? static_cast<uint32>(FCString::Atoi(*Args[4])) : 0;
    const bool bTyped = ModeValue == static_cast<int32>(EWowDestroyConfirm::TypeDelete);

    ReleaseGameplayMouseLookForModalUi();
    UE_LOG(LogWowBags, Log, TEXT("Destroy request: %s x%d quality=%u typed=%d"), *Name, Count, Quality, bTyped ? 1 : 0);

    TWeakObjectPtr<UWowConnectionManager> WeakConnection(ConnectionManager);
    DestroyItemDialog = SNew(SWowDestroyItemDialog)
        .ItemName(Name)
        .ItemEntry(Entry)
        .Quality(Quality)
        .Count(Count)
        .bRequireTypedDelete(bTyped)
        .ConnectionManager(WeakConnection)
        .UIManager(TWeakObjectPtr<UWowUIManager>(UIManager))
        .IsStillValid_Lambda([WeakConnection]() { return WeakConnection.IsValid() && WeakConnection->HasCursorItemPayload(); })
        .OnConfirmed(FSimpleDelegate::CreateWeakLambda(this, [this]() { HideDestroyItemDialog(true); }))
        .OnCancelled(FSimpleDelegate::CreateWeakLambda(this, [this]() { HideDestroyItemDialog(false); }));

    DestroyItemViewportWidget =
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            // Near-transparent full-screen layer: world clicks stay blocked while asking.
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.001f))
        ]
        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Top)
        .Padding(0, 140, 0, 0) // StaticPopup1 position in the original client
        [
            DestroyItemDialog.ToSharedRef()
        ];
    FWowUiSurfaces::Add(DestroyItemViewportWidget.ToSharedRef(), EWowUiZone::Popups, DestroyDialogZOrder);
    DestroyItemDialog->FocusInput();
}

void AWowGameplayController::HideDestroyItemDialog(bool bConfirmed)
{
    if (!DestroyItemViewportWidget.IsValid()) return;
    // Release the widgets first: the dialog's callback is what called us.
    const TSharedPtr<SWidget> Viewport = DestroyItemViewportWidget;
    DestroyItemViewportWidget.Reset();
    DestroyItemDialog.Reset();
    if (GEngine && GEngine->GameViewport)
        FWowUiSurfaces::Remove(Viewport.ToSharedRef());

    if (ConnectionManager)
    {
        if (bConfirmed)
        {
            if (!ConnectionManager->DeleteCursorItem())
            {
                UE_LOG(LogWowBags, Warning, TEXT("Destroy confirmed but the item moved; nothing destroyed"));
                ConnectionManager->ClearCursorPayload();
            }
        }
        else if (ConnectionManager->HasCursorItemPayload())
        {
            // "No"/Esc: the item goes back where it came from (the server never moved it).
            ConnectionManager->ClearCursorPayload();
        }
    }
    if (FSlateApplication::IsInitialized())
        FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
}

void AWowGameplayController::CreateCursorItemIcon()
{
    if (CursorItemIconWidget.IsValid() || !GEngine || !GEngine->GameViewport) return;
    TWeakObjectPtr<UWowConnectionManager> WeakConnection(ConnectionManager);
    TWeakObjectPtr<AWowGameplayController> WeakThis(this);
    CursorItemIconWidget = SNew(SWowCursorItemIcon)
        .ConnectionManager(WeakConnection)
        .UIManager(TWeakObjectPtr<UWowUIManager>(UIManager))
        .GetItemEntry_Lambda([WeakThis]() -> uint32
        {
            const AWowGameplayController* Self = WeakThis.Get();
            if (!Self || !Self->ConnectionManager || !Self->ConnectionManager->HasCursorItemPayload()) return 0;
            if (Self->bLeftMouseLooking || Self->bRightMouseLooking) return 0;
            FString Type, Detail; int32 Id = 0;
            return Self->ConnectionManager->GetCursorInfo(Type, Id, Detail) ? static_cast<uint32>(FMath::Max(Id, 0)) : 0;
        });
    FWowUiSurfaces::Add(CursorItemIconWidget.ToSharedRef(), EWowUiZone::Cursor, CursorItemIconZOrder);
}
