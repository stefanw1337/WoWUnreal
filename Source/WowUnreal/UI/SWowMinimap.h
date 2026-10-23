#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Brushes/SlateImageBrush.h"

class FWowEntityManager;
class FWowPacketHandler;
class AWowWorldManager;
struct FWowEntity;
struct FWowGroupInfo;
using FWowQuestGiverStatusMap = TMap<uint64, uint8>;

/**
 * WoW-style minimap widget showing player position, entity dots, and zone name.
 * Positioned in the top-right corner with a circular gold border.
 */
class WOWUNREAL_API SWowMinimap : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowMinimap)
    {
        _Visibility = EVisibility::Visible;
    }
        SLATE_ARGUMENT(FWowEntityManager*, EntityManager)
        SLATE_ARGUMENT(const FWowGroupInfo*, GroupInfo)
        SLATE_ARGUMENT(const FWowQuestGiverStatusMap*, QuestGiverStatuses)
        SLATE_ARGUMENT(AWowWorldManager*, WorldManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Update minimap data (called each frame or every 0.5s) */
    void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

    /** Toggle visibility */
    void SetMinimapVisibility(EVisibility InVisibility);

    /** Handle mouse wheel for zoom */
    virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

    /** Zoom controls used by the stock FrameXML buttons around the map. */
    void ZoomIn();
    void ZoomOut();

    /** Select the active innkeeper/vendor/trainer tracking category. */
    void SetTrackingMode(int32 InTrackingMode);
    int32 GetTrackingMode() const { return TrackingMode; }
	float GetZoomYards() const { return ZoomYards; }
    float GetCaptureWidth() const { return 2.0f * ZoomYards * 100.0f; }

    /** Keep minimap hover/clicks on the native overlay until stock controls are safely bridged. */
    virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

    /** Handle 'M' key for full-screen map */
    void ToggleFullScreenMap();

    /** Use a real top-down world capture when available. */
    void SetMapRenderTarget(UTextureRenderTarget2D* InRenderTarget);

    /** Quest POIs: the super-tracked quest (selected on the world map) shows its areas here and an
     *  arrow on the rim when they are off the minimap. GetMap returns the player's map id. */
    void SetQuestSource(FWowPacketHandler* InQuests, TFunction<uint32()> InGetMap) { Quests = InQuests; GetMap = MoveTemp(InGetMap); }
    /** Quest diamonds (Ctrl-click in the tracker): their world positions (UE units). Each gets a small
     *  gem on the minimap, or a faceted gold arrow on the rim pointing the way when it is off the map. */
    void SetDiamondSource(TFunction<void(TArray<FVector>&)> InTargets) { DiamondTargets = MoveTemp(InTargets); }

    /** Use the original minimap arrow loaded from the player's WoW data. */
    void SetPlayerArrowTexture(class UTexture2D* InTexture);

private:
    friend class FWowMinimapProjectionTest;
    /** Entity manager for reading entity positions */
    FWowEntityManager* EntityManager;
    const FWowGroupInfo* GroupInfo = nullptr;

    /** Server-authoritative quest marker state keyed by quest giver GUID. */
    const FWowQuestGiverStatusMap* QuestGiverStatuses = nullptr;

    /** World manager for current area/zone */
    TWeakObjectPtr<AWowWorldManager> WorldManager;

    /** Minimap parameters */
    static constexpr float MinimapSize = 184.0f;
    static constexpr float DefaultZoomYards = 64.0f;
    static constexpr float MinZoomYards = 32.0f;
    static constexpr float MaxZoomYards = 80.0f;

    /** Current zoom level in WoW yards */
    float ZoomYards = DefaultZoomYards;

    /** Zero is the stock "None" option; remaining values mirror the native menu rows. */
    int32 TrackingMode = 0;

    /** Whether full-screen map is shown */
    bool bIsFullScreen = false;

    /** Zone name text widget */
    TSharedPtr<class STextBlock> ZoneNameText;

    /** Top-down world capture supplied by the gameplay controller. */
    TWeakObjectPtr<UTextureRenderTarget2D> MapRenderTarget;
    mutable FSlateBrush MapRenderTargetBrush;
    TWeakObjectPtr<UTexture2D> PlayerArrowTexture;
    mutable FSlateBrush PlayerArrowBrush;

    /** Custom paint for the minimap circle and entities */
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                         const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                         int32 LayerId, const FWidgetStyle& InWidgetStyle,
                         bool bParentEnabled) const override;

    /** Paint the circular minimap background */
    void PaintMinimapBackground(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                               int32 LayerId, const FVector2D& Center, float Radius) const;

    /** Paint the live top-down world render target. */
    bool PaintCapturedWorldMap(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                               int32 LayerId, const FVector2D& Center, float Radius) const;

    /** Paint entity dots */
    void PaintEntityDots(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                        int32 LayerId, const FVector2D& Center, float Radius) const;

    /** Paint the super-tracked quest's areas and the rim arrow toward them. */
    void PaintQuestPoi(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                       int32 LayerId, const FVector2D& Center, float Radius) const;
    FWowPacketHandler* Quests = nullptr;
    TFunction<uint32()> GetMap;
    TFunction<void(TArray<FVector>&)> DiamondTargets;
    void PaintCorpseMarker(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                           int32 LayerId, const FVector2D& Center, float Radius) const; // r255
    void PaintDiamondMarkers(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                             int32 LayerId, const FVector2D& Center, float Radius) const;

    /** Paint the player arrow */
    void PaintPlayerArrow(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                         int32 LayerId, const FVector2D& Center) const;

    /** Paint the circular gold border */
    void PaintCircularBorder(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                            int32 LayerId, const FVector2D& Center, float Radius) const;

    /** Convert world position to minimap coordinates */
    FVector2D WorldToMinimapCoords(const FVector& WorldPos, const FVector& PlayerPos,
                                  const FVector2D& Center, float Radius, float PlayerYaw) const;

    /** Get entity color based on type/faction */
    FLinearColor GetEntityColor(const FWowEntity& Entity) const;

    /** Return whether an entity matches the current tracking category. */
    bool ShouldPaintTrackedEntity(const FWowEntity& Entity) const;

    /** Get current zone name */
    FString GetCurrentZoneName() const;

    /** Get current player position and rotation */
    bool GetPlayerPositionAndRotation(FVector& OutPosition, float& OutYaw) const;

    /** Check if position is within minimap circle */
    bool IsWithinMinimapCircle(const FVector2D& Center, float Radius, const FVector2D& Point) const;

    /** Last update time for performance throttling */
    mutable double LastUpdateTime = 0.0;
    mutable float UpdateInterval = 0.5f; // Update every 0.5 seconds
};
