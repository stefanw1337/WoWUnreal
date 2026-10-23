#include "WowTooltipManager.h"
#include "SWowTooltip.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "Widgets/SOverlay.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTooltip, Log, All);

UWowTooltipManager::UWowTooltipManager()
{
    HoverTimer = 0.0f;
    HoverDelay = 0.5f;
    CurrentHoveredEntity = nullptr;
}

void UWowTooltipManager::Initialize(UWowConnectionManager* ConnectionMgr, TSharedPtr<SWidget> RootWidget)
{
    ConnectionManager = ConnectionMgr;
    RootWidgetPtr = RootWidget;

    if (!ConnectionManager)
    {
        UE_LOG(LogWowTooltip, Error, TEXT("Failed to initialize WowTooltipManager: ConnectionManager is null"));
        return;
    }

    UE_LOG(LogWowTooltip, Log, TEXT("WowTooltipManager initialized"));
}

void UWowTooltipManager::Shutdown()
{
    HideTooltip();
    ConnectionManager = nullptr;
    RootWidgetPtr.Reset();
    CurrentHoveredEntity = nullptr;

    UE_LOG(LogWowTooltip, Log, TEXT("WowTooltipManager shut down"));
}

void UWowTooltipManager::Update(const FVector2D& MousePosition)
{
    if (!ConnectionManager)
    {
        return;
    }

    // Check if mouse moved significantly
    bool bMouseMoved = FVector2D::Distance(MousePosition, LastMousePosition) > 5.0f;
    LastMousePosition = MousePosition;

    // Find entity under cursor
    const FWowEntity* HoveredEntity = FindEntityUnderCursor(MousePosition);

    // Check if hovered entity changed
    if (HoveredEntity != CurrentHoveredEntity)
    {
        CurrentHoveredEntity = HoveredEntity;
        HoverTimer = 0.0f;

        if (!HoveredEntity)
        {
            HideTooltip();
        }
    }

    // Update hover timer
    if (CurrentHoveredEntity && !TooltipWidget.IsValid())
    {
        if (bMouseMoved)
        {
            HoverTimer = 0.0f; // Reset timer on mouse movement
        }
        else
        {
            HoverTimer += 0.016f; // Approximate frame time

            // Show tooltip after delay
            if (HoverTimer >= HoverDelay)
            {
                ShowTooltipForEntity(CurrentHoveredEntity, MousePosition);
            }
        }
    }

    // Update tooltip position if visible
    if (TooltipWidget.IsValid() && CurrentHoveredEntity)
    {
        // TODO: Update tooltip position to follow mouse
        // This would require access to the Slate widget hierarchy
    }
}

void UWowTooltipManager::ShowTooltip(uint64 EntityGuid, const FVector2D& Position)
{
    if (!ConnectionManager)
    {
        return;
    }

    const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(EntityGuid);
    ShowTooltipForEntity(Entity, Position);
}

void UWowTooltipManager::ShowTooltipForEntity(const FWowEntity* Entity, const FVector2D& Position)
{
    if (!Entity || TooltipWidget.IsValid() || !RootWidgetPtr.IsValid())
    {
        return;
    }

    // Create tooltip widget
    TooltipWidget = SNew(SWowTooltip);
    TooltipWidget->SetEntity(Entity);

    // Add to root widget as overlay
    if (TSharedPtr<SWidget> Root = RootWidgetPtr.Pin())
    {
        if (TSharedPtr<SOverlay> RootOverlay = StaticCastSharedPtr<SOverlay>(Root))
        {
            RootOverlay->AddSlot()
            .HAlign(HAlign_Right)
            .VAlign(VAlign_Bottom)
            .Padding(FMargin(0.0f, 0.0f, 18.0f, 102.0f))
            [
                TooltipWidget.ToSharedRef()
            ];
        }
    }

    UE_LOG(LogWowTooltip, Verbose, TEXT("Tooltip shown for entity GUID %llu"), Entity->Guid);
}

void UWowTooltipManager::HideTooltip()
{
    if (!TooltipWidget.IsValid())
    {
        return;
    }

    // Remove from parent
    if (TSharedPtr<SWidget> Parent = TooltipWidget->GetParentWidget())
    {
        if (TSharedPtr<SOverlay> ParentOverlay = StaticCastSharedPtr<SOverlay>(Parent))
        {
            ParentOverlay->RemoveSlot(TooltipWidget.ToSharedRef());
        }
    }

    TooltipWidget.Reset();

    UE_LOG(LogWowTooltip, Verbose, TEXT("Tooltip hidden"));
}

bool UWowTooltipManager::IsTooltipVisible() const
{
    return TooltipWidget.IsValid();
}

const FWowEntity* UWowTooltipManager::FindEntityUnderCursor(const FVector2D& ScreenPosition) const
{
    if (!ConnectionManager)
    {
        return nullptr;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    // Get player controller
    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC)
    {
        return nullptr;
    }

    // Perform line trace from mouse cursor into world
    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.bTraceComplex = true;
    if (PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit))
    {
        AActor* HitActor = Hit.GetActor();
        if (HitActor && HitActor->Tags.Num() > 0)
        {
            // Check if the actor has an entity GUID tag
            FString GuidTag = HitActor->Tags[0].ToString();
            uint64 EntityGuid = FCString::Atoi64(*GuidTag);

            if (EntityGuid != 0)
            {
                // Find the entity in the entity manager
                return ConnectionManager->PacketHandler.EntityManager.Find(EntityGuid);
            }
        }
    }

    return nullptr;
}

UWorld* UWowTooltipManager::GetWorld() const
{
    if (ConnectionManager)
    {
        return ConnectionManager->GetWorld();
    }

    return GEngine ? GEngine->GetWorldFromContextObjectChecked(this) : nullptr;
}
