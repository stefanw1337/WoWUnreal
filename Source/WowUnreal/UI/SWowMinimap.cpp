#include "SWowMinimap.h"
#include "WowColor.h"
#include "WowEntityManager.h"
#include "WowEntity.h"
#include "WowPacketHandler.h"
#include "WowWorldManager.h"
#include "WowUpdateFields.h"
#include "Coord/WowCoordinate.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/AreaTableDbc.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowMinimap, Log, All);

namespace
{
    void PaintFilledDisc(const FSlateRenderTransform& RenderTransform,
                         FSlateWindowElementList& OutDrawElements, int32 LayerId,
                         const FVector2D& Center, float Radius, const FLinearColor& Color,
                         int32 NumSegments = 24)
    {
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;

        Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(Center), FVector2f::ZeroVector, Color.ToFColor(true)));

        const float AngleStep = 2.0f * PI / static_cast<float>(NumSegments);
        for (int32 i = 0; i <= NumSegments; ++i)
        {
            const float Angle = i * AngleStep;
            const FVector2D Pos = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
            Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(Pos), FVector2f::ZeroVector, Color.ToFColor(true)));
        }

        for (int32 i = 0; i < NumSegments; ++i)
        {
            Indices.Add(0);
            Indices.Add(i + 1);
            Indices.Add(i + 2);
        }

        FSlateDrawElement::MakeCustomVerts(
            OutDrawElements,
            LayerId,
            FSlateResourceHandle(),
            Vertices,
            Indices,
            nullptr,
            0,
            0,
            ESlateDrawEffect::None
        );
    }

    void PaintDiamond(const FSlateRenderTransform& RenderTransform,
                      FSlateWindowElementList& OutDrawElements, int32 LayerId,
                      const FVector2D& Center, float Radius, const FLinearColor& Color)
    {
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;

        const FVector2D Points[4] = {
            Center + FVector2D(0.0f, -Radius),
            Center + FVector2D(Radius, 0.0f),
            Center + FVector2D(0.0f, Radius),
            Center + FVector2D(-Radius, 0.0f)
        };

        for (const FVector2D& Point : Points)
        {
            Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(Point), FVector2f::ZeroVector, Color.ToFColor(true)));
        }

        Indices.Add(0);
        Indices.Add(1);
        Indices.Add(2);
        Indices.Add(0);
        Indices.Add(2);
        Indices.Add(3);

        FSlateDrawElement::MakeCustomVerts(
            OutDrawElements,
            LayerId,
            FSlateResourceHandle(),
            Vertices,
            Indices,
            nullptr,
            0,
            0,
            ESlateDrawEffect::None
        );
    }

    void PaintTexturedDisc(const FSlateRenderTransform& RenderTransform,
                           FSlateWindowElementList& OutDrawElements, int32 LayerId,
                           const FSlateBrush& Brush, const FVector2D& Center, float Radius,
                           const FLinearColor& TintColor = FLinearColor::White,
                           int32 NumSegments = 64)
    {
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        const FColor Tint = TintColor.ToFColor(true);

        Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(Center), FVector2f(0.5f, 0.5f), Tint));

        const float AngleStep = 2.0f * PI / static_cast<float>(NumSegments);
        for (int32 i = 0; i <= NumSegments; ++i)
        {
            const float Angle = i * AngleStep;
            const FVector2D Unit(FMath::Cos(Angle), FMath::Sin(Angle));
            const FVector2D Pos = Center + Unit * Radius;
            const FVector2D UV(0.5f + Unit.X * 0.5f, 0.5f + Unit.Y * 0.5f);
            Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(Pos), FVector2f(UV), Tint));
        }

        for (int32 i = 0; i < NumSegments; ++i)
        {
            Indices.Add(0);
            Indices.Add(i + 1);
            Indices.Add(i + 2);
        }

        FSlateDrawElement::MakeCustomVerts(
            OutDrawElements,
            LayerId,
            Brush.GetRenderingResource(),
            Vertices,
            Indices,
            nullptr,
            0,
            0,
            ESlateDrawEffect::None
        );
    }
}

void SWowMinimap::Construct(const FArguments& InArgs)
{
    EntityManager = InArgs._EntityManager;
    QuestGiverStatuses = InArgs._QuestGiverStatuses;
    WorldManager = InArgs._WorldManager;
    GroupInfo = InArgs._GroupInfo;

    ChildSlot
    [
        SNew(SBox)
        .WidthOverride(MinimapSize)
        .HeightOverride(MinimapSize)
        [
            SNullWidget::NullWidget // The actual minimap is drawn via OnPaint.
        ]
    ];
}

void SWowMinimap::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    // Update zone name periodically
    if (InCurrentTime - LastUpdateTime >= UpdateInterval)
    {
        LastUpdateTime = InCurrentTime;

        // Update zone name
        FString ZoneName = GetCurrentZoneName();
        if (ZoneNameText.IsValid())
        {
            ZoneNameText->SetText(FText::FromString(ZoneName));
        }
    }
}

void SWowMinimap::SetMinimapVisibility(EVisibility InVisibility)
{
    SetVisibility(InVisibility);
}

FReply SWowMinimap::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    // Check if mouse is over the minimap circle
    FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    FVector2D Center = FVector2D(MinimapSize * 0.5f, MinimapSize * 0.5f);
    float Radius = MinimapSize * 0.5f;

    if (IsWithinMinimapCircle(Center, Radius, LocalPos))
    {
        MouseEvent.GetWheelDelta() > 0.0f ? ZoomIn() : ZoomOut();
        return FReply::Handled();
    }

    return FReply::Unhandled();
}

void SWowMinimap::ZoomIn()
{
    ZoomYards = FMath::Clamp(ZoomYards * 0.8f, MinZoomYards, MaxZoomYards);
    UE_LOG(LogWowMinimap, Log, TEXT("Minimap zoomed in to %.1f yards"), ZoomYards);
}

void SWowMinimap::ZoomOut()
{
    ZoomYards = FMath::Clamp(ZoomYards * 1.25f, MinZoomYards, MaxZoomYards);
    UE_LOG(LogWowMinimap, Log, TEXT("Minimap zoomed out to %.1f yards"), ZoomYards);
}

void SWowMinimap::SetTrackingMode(int32 InTrackingMode)
{
    TrackingMode = FMath::Clamp(InTrackingMode, 0, 11);
    UE_LOG(LogWowMinimap, Log, TEXT("Minimap tracking mode changed to %d"), TrackingMode);
}

FReply SWowMinimap::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    const FVector2D Center(MinimapSize * 0.5f, MinimapSize * 0.5f);
    const float Radius = MinimapSize * 0.5f;

    return IsWithinMinimapCircle(Center, Radius, LocalPos)
        ? FReply::Handled()
        : FReply::Unhandled();
}

FReply SWowMinimap::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    const FVector2D Center(MinimapSize * 0.5f, MinimapSize * 0.5f);
    const float Radius = MinimapSize * 0.5f;

    return IsWithinMinimapCircle(Center, Radius, LocalPos)
        ? FReply::Handled()
        : FReply::Unhandled();
}

FReply SWowMinimap::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    const FVector2D Center(MinimapSize * 0.5f, MinimapSize * 0.5f);
    const float Radius = MinimapSize * 0.5f;

    return IsWithinMinimapCircle(Center, Radius, LocalPos)
        ? FReply::Handled()
        : FReply::Unhandled();
}

void SWowMinimap::ToggleFullScreenMap()
{
    bIsFullScreen = !bIsFullScreen;
    // For now, just show a larger version of the same minimap
    // TODO: Implement proper full-screen world map
    UE_LOG(LogWowMinimap, Log, TEXT("Full-screen map toggled: %s"), bIsFullScreen ? TEXT("ON") : TEXT("OFF"));
}

void SWowMinimap::SetMapRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
    MapRenderTarget = InRenderTarget;
    MapRenderTargetBrush = FSlateBrush();
    if (InRenderTarget)
    {
        MapRenderTargetBrush.SetResourceObject(InRenderTarget);
        MapRenderTargetBrush.DrawAs = ESlateBrushDrawType::Image;
        MapRenderTargetBrush.ImageSize = FVector2D(
            static_cast<float>(InRenderTarget->SizeX),
            static_cast<float>(InRenderTarget->SizeY));
    }
}

void SWowMinimap::SetPlayerArrowTexture(UTexture2D* InTexture)
{
    PlayerArrowTexture = InTexture;
    PlayerArrowBrush = FSlateBrush();
    if (InTexture)
    {
        PlayerArrowBrush.SetResourceObject(InTexture);
        PlayerArrowBrush.DrawAs = ESlateBrushDrawType::Image;
        PlayerArrowBrush.ImageSize = FVector2D(
            static_cast<float>(InTexture->GetSizeX()),
            static_cast<float>(InTexture->GetSizeY()));
    }
}

int32 SWowMinimap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                          const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                          int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    // Calculate minimap center and radius. The stock FrameXML ring sits above
    // this widget, so the live map content must stay inside the ring opening.
    FVector2D Center = FVector2D(MinimapSize * 0.5f, MinimapSize * 0.5f);
    const float Radius = MinimapSize * 0.5f;
    // Paint beneath the ring instead of stopping short of it. The circular
    // geometry is the mask, while the authored FrameXML art hides the outer
    // few pixels and supplies the metal rim.
    const float PaintRadius = Radius - 1.0f;

    // Always paint the lightweight terrain impression first. The live overhead
    // capture is then layered above it, so unloaded/transparent capture areas
    // read as terrain instead of black void.
    PaintMinimapBackground(AllottedGeometry, OutDrawElements, LayerId, Center, PaintRadius);
    PaintCapturedWorldMap(AllottedGeometry, OutDrawElements, LayerId + 3, Center, PaintRadius);

    PaintQuestPoi(AllottedGeometry, OutDrawElements, LayerId + 4, Center, PaintRadius);
    PaintDiamondMarkers(AllottedGeometry, OutDrawElements, LayerId + 6, Center, PaintRadius);
    PaintCorpseMarker(AllottedGeometry, OutDrawElements, LayerId + 6, Center, PaintRadius); // r255

    // Paint entity dots
    PaintEntityDots(AllottedGeometry, OutDrawElements, LayerId + 5, Center, PaintRadius);

    // Paint player arrow (always on top)
    PaintPlayerArrow(AllottedGeometry, OutDrawElements, LayerId + 8, Center);

    // Paint border (always on top)
    return LayerId + 10;
}

bool SWowMinimap::PaintCapturedWorldMap(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                                       int32 LayerId, const FVector2D& Center, float Radius) const
{
    UTextureRenderTarget2D* RenderTarget = MapRenderTarget.Get();
    if (!RenderTarget)
    {
        return false;
    }

    if (MapRenderTargetBrush.GetResourceObject() != RenderTarget)
    {
        MapRenderTargetBrush.SetResourceObject(RenderTarget);
        MapRenderTargetBrush.DrawAs = ESlateBrushDrawType::Image;
        MapRenderTargetBrush.ImageSize = FVector2D(
            static_cast<float>(RenderTarget->SizeX),
            static_cast<float>(RenderTarget->SizeY));
    }

    // r344 (Stefan 14.10, card r20t357: the minimap was green in Dun Morogh's snow): the capture was
    // drawn at 72 % with a green tint (0.58, 0.66, 0.49) over the dark green fallback disc, plus four
    // green-tinted blur copies and an olive film, so white snow came out green-grey. Like the client's
    // minimap the ground is now drawn as it is: once, untinted, opaque.
    const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
    PaintTexturedDisc(RenderTransform, OutDrawElements,
        LayerId + 1, MapRenderTargetBrush, Center, Radius, FLinearColor::White, 96);
    return true;
}

void SWowMinimap::PaintMinimapBackground(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                                        int32 LayerId, const FVector2D& Center, float Radius) const
{
    FVector PlayerPos;
    float PlayerYaw = 0.0f;
    const bool bHasPlayerPosition = GetPlayerPositionAndRotation(PlayerPos, PlayerYaw);

    // Original fallback minimap: a lightweight, local top-down terrain impression.
    // Later this can be replaced with optional MPQ minimap tile import when local data is present.
    const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
    PaintFilledDisc(RenderTransform, OutDrawElements, LayerId, Center, Radius, WowColor::FromWow(0.050f, 0.073f, 0.035f, 0.98f), 48);

    // The procedural fallback contains illustrative roads and contours. Do not
    // blend those into the live capture because they rotate with the player and
    // appear as a false line across the real minimap.
    if (MapRenderTarget.IsValid())
    {
        return;
    }

    if (!bHasPlayerPosition)
    {
        return;
    }

    const float ZoomRangeUE = ZoomYards * 100.0f;
    const float CosYaw = FMath::Cos(PlayerYaw);
    const float SinYaw = FMath::Sin(PlayerYaw);
    const FVector2D WorldAnchor(PlayerPos.X * 0.00008f, PlayerPos.Y * 0.00008f);

    for (int32 BlobIndex = 0; BlobIndex < 18; ++BlobIndex)
    {
        const float AngleSeed = static_cast<float>(BlobIndex) * 2.399963f;
        const float DriftA = FMath::PerlinNoise2D(WorldAnchor + FVector2D(BlobIndex * 0.31f, 4.7f));
        const float DriftB = FMath::PerlinNoise2D(WorldAnchor + FVector2D(-2.1f, BlobIndex * 0.27f));
        const float Distance = Radius * (0.20f + 0.68f * FMath::Frac(0.37f * static_cast<float>(BlobIndex)));
        FVector2D LocalOffset(FMath::Cos(AngleSeed + DriftA * 0.9f), FMath::Sin(AngleSeed + DriftB * 0.9f));
        LocalOffset *= Distance;

        const FVector2D WorldOffsetBeforeRotation = LocalOffset * (ZoomRangeUE / Radius);
        const FVector2D WorldOffset(
            WorldOffsetBeforeRotation.X * CosYaw - WorldOffsetBeforeRotation.Y * SinYaw,
            WorldOffsetBeforeRotation.X * SinYaw + WorldOffsetBeforeRotation.Y * CosYaw
        );
        const FVector2D SampleWorld(PlayerPos.X + WorldOffset.X, PlayerPos.Y + WorldOffset.Y);
        const float TerrainNoise = FMath::PerlinNoise2D(SampleWorld * 0.00016f);

        const bool bForest = TerrainNoise > 0.10f;
        const bool bLowland = TerrainNoise < -0.42f;
        const FLinearColor BlobColor = bLowland
            ? WowColor::FromWow(0.040f, 0.095f, 0.070f, 0.16f)
            : (bForest
                ? WowColor::FromWow(0.028f, 0.092f, 0.026f, 0.22f)
                : WowColor::FromWow(0.145f, 0.135f, 0.070f, 0.09f));

        PaintFilledDisc(RenderTransform, OutDrawElements, LayerId + 1,
            Center + LocalOffset, Radius * (0.13f + 0.045f * FMath::Abs(TerrainNoise)),
            BlobColor, 28);
    }

    auto RotateLocal = [CosYaw, SinYaw](const FVector2D& Local) -> FVector2D
    {
        return FVector2D(
            Local.X * CosYaw - Local.Y * SinYaw,
            Local.X * SinYaw + Local.Y * CosYaw
        );
    };

    TArray<FVector2D> RoadPoints;
    RoadPoints.Add(Center + RotateLocal(FVector2D(-Radius * 0.86f, Radius * 0.58f)));
    RoadPoints.Add(Center + RotateLocal(FVector2D(-Radius * 0.40f, Radius * 0.18f)));
    RoadPoints.Add(Center + RotateLocal(FVector2D( Radius * 0.02f, -Radius * 0.05f)));
    RoadPoints.Add(Center + RotateLocal(FVector2D( Radius * 0.55f, -Radius * 0.46f)));

    FSlateDrawElement::MakeLines(
        OutDrawElements,
        LayerId + 2,
        AllottedGeometry.ToPaintGeometry(),
        RoadPoints,
        ESlateDrawEffect::None,
        WowColor::FromWow(0.31f, 0.24f, 0.13f, 0.52f),
        true,
        4.0f);

    FSlateDrawElement::MakeLines(
        OutDrawElements,
        LayerId + 3,
        AllottedGeometry.ToPaintGeometry(),
        RoadPoints,
        ESlateDrawEffect::None,
        WowColor::FromWow(0.55f, 0.45f, 0.25f, 0.18f),
        true,
        1.3f);

    for (int32 ContourIndex = 0; ContourIndex < 5; ++ContourIndex)
    {
        TArray<FVector2D> ContourPoints;
        const float ContourRadius = Radius * (0.24f + ContourIndex * 0.125f);
        const float OffsetPhase = static_cast<float>(ContourIndex) * 0.78f
            + FMath::PerlinNoise2D(WorldAnchor + FVector2D(ContourIndex * 0.41f, -1.3f)) * 0.40f;

        for (int32 PointIndex = 0; PointIndex <= 32; ++PointIndex)
        {
            const float T = static_cast<float>(PointIndex) / 32.0f;
            const float Angle = T * 2.0f * PI + OffsetPhase;
            const float Wobble = 1.0f + FMath::Sin(Angle * 3.0f + OffsetPhase) * 0.045f;
            const FVector2D Local(FMath::Cos(Angle), FMath::Sin(Angle));
            ContourPoints.Add(Center + RotateLocal(Local * ContourRadius * Wobble));
        }

        FSlateDrawElement::MakeLines(
            OutDrawElements,
            LayerId + 2,
            AllottedGeometry.ToPaintGeometry(),
            ContourPoints,
            ESlateDrawEffect::None,
            WowColor::FromWow(0.105f, 0.085f, 0.040f, 0.18f),
            true,
            0.75f);
    }

    // Subtle dark vignette so icons stay readable and the edge feels recessed under the ring.
    PaintFilledDisc(RenderTransform, OutDrawElements, LayerId + 4, Center, Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.14f), 48);
}

void SWowMinimap::PaintEntityDots(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                                 int32 LayerId, const FVector2D& Center, float Radius) const
{
    if (!EntityManager)
    {
        return;
    }

    FVector PlayerPos;
    float PlayerYaw;
    if (!GetPlayerPositionAndRotation(PlayerPos, PlayerYaw))
    {
        return;
    }

    const TMap<uint64, TSharedPtr<FWowEntity>>& AllEntities = EntityManager->GetAll();
    const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();

    auto PaintDot=[&](FVector2D Position,float Size,FLinearColor Color) {
        PaintFilledDisc(RenderTransform,OutDrawElements,LayerId,Position+FVector2D(.5f,.7f),Size+1.1f,FLinearColor(0,0,0,.95f),18);
        PaintFilledDisc(RenderTransform,OutDrawElements,LayerId+1,Position,Size,Color*.5f,18);
        PaintFilledDisc(RenderTransform,OutDrawElements,LayerId+2,Position+FVector2D(-.2f,-.35f),Size*.70f,Color,18);
        PaintFilledDisc(RenderTransform,OutDrawElements,LayerId+3,Position+FVector2D(-.7f,-.9f),Size*.22f,WowColor::FromWow(.95f, .97f, 1.f, .8f),12);
    };
    for (const auto& Pair : AllEntities)
    {
        const FWowEntity& Entity = *Pair.Value;

        // Skip local player
        if (Entity.Guid == EntityManager->LocalPlayerGuid)
        {
            continue;
        }

        // Show units and selected world objects. Resource nodes are usually gameobjects.
        if (!Entity.IsUnit() && !Entity.IsGameObject())
        {
            continue;
        }

        const uint8 QuestStatus = QuestGiverStatuses
            ? QuestGiverStatuses->FindRef(Entity.Guid)
            : WowQuestGiverStatus::NONE;
        const bool bReadyForMinimapTurnIn = QuestStatus == WowQuestGiverStatus::REWARD;
        const bool bTrackedLowLevelQuest = TrackingMode == 11 &&
            (QuestStatus == WowQuestGiverStatus::LOW_LEVEL_AVAILABLE ||
             QuestStatus == WowQuestGiverStatus::LOW_LEVEL_AVAILABLE_REP ||
             QuestStatus == WowQuestGiverStatus::LOW_LEVEL_REWARD_REP);

        if (!bReadyForMinimapTurnIn && !bTrackedLowLevelQuest && !ShouldPaintTrackedEntity(Entity))
        {
            continue;
        }

        const FVector EntityPos = FWowCoordinate::WowToUE(Entity.Movement.Position);

        // Check if entity is within radius (convert zoom yards to UE units)
        const float MaxDistanceYards = ZoomYards;
        const float DistanceYards = FVector::Dist2D(PlayerPos, EntityPos) / FWowCoordinate::SCALE;
        if (DistanceYards > MaxDistanceYards)
        {
            continue;
        }

        // Convert to minimap coordinates
        FVector2D MinimapPos = WorldToMinimapCoords(EntityPos, PlayerPos, Center, Radius, PlayerYaw);

        // Check if position is within the minimap circle
        if (!IsWithinMinimapCircle(Center, Radius, MinimapPos))
        {
            continue;
        }

        const FLinearColor DotColor = GetEntityColor(Entity);

        if (Entity.IsUnit())
        {
            if (bReadyForMinimapTurnIn)
            {
                PaintDot(MinimapPos,4.2f,WowColor::FromWow(1.f, .82f, .05f));
            }
            else if (bTrackedLowLevelQuest)
            {
                const FSlateFontInfo MarkerFont = FCoreStyle::GetDefaultFontStyle("Bold", 15);
                const FVector2D ShadowOffset(1.0f, 1.0f);
                const FVector2D TextPos = MinimapPos + FVector2D(-3.5f, -11.0f);

                FSlateDrawElement::MakeText(
                    OutDrawElements,
                    LayerId + 1,
                    AllottedGeometry.ToPaintGeometry(FVector2D(14.0f, 18.0f), FSlateLayoutTransform(TextPos + ShadowOffset)),
                    FString(TEXT("!")),
                    MarkerFont,
                    ESlateDrawEffect::None,
                    FLinearColor::Black
                );
                FSlateDrawElement::MakeText(
                    OutDrawElements,
                    LayerId + 2,
                    AllottedGeometry.ToPaintGeometry(FVector2D(14.0f, 18.0f), FSlateLayoutTransform(TextPos)),
                    FString(TEXT("!")),
                    MarkerFont,
                    ESlateDrawEffect::None,
                    WowColor::FromWow(0.55f, 0.55f, 0.55f, 1.0f)
                );
            }
            else
            {
                PaintDot(MinimapPos,Entity.IsPlayer()?3.6f:3.0f,DotColor);
            }
        }
        else if (Entity.IsGameObject())
        {
            const FWowGameObjectEntity* GameObjectEntity = static_cast<const FWowGameObjectEntity*>(&Entity);
            const uint8 GameObjectType = GameObjectEntity ? GameObjectEntity->GetGameObjectType() : 0;

            if (GameObjectType == WowGameObjectType::MAILBOX)
            {
                PaintDiamond(RenderTransform, OutDrawElements, LayerId, MinimapPos, 4.0f, WowColor::FromWow(0.65f, 0.85f, 1.0f, 1.0f));
            }
            else if (GameObjectType == 3) // Chest-type nodes include many herb/mining resource spawns in 3.3.5a servers.
            {
                PaintDiamond(RenderTransform, OutDrawElements, LayerId, MinimapPos, 3.6f, WowColor::FromWow(1.0f, 0.68f, 0.18f, 1.0f));
            }
        }
    }
}

bool SWowMinimap::ShouldPaintTrackedEntity(const FWowEntity& Entity) const
{
    if (Entity.IsPlayer())
    {
        if (GroupInfo && GroupInfo->FindMember(Entity.Guid)) return true;
        const FWowPlayerEntity* Player=EntityManager?EntityManager->GetLocalPlayer():nullptr;
        // PLAYER_TRACK_CREATURES, WotLK 12340: humanoid category 7 -> bit 6.
        // Use the server's active tracking field, never the player's class alone.
        constexpr uint16 TrackCreaturesField=UnitField::END+0x036A;
        return Player && (Player->GetField(TrackCreaturesField)&(1u<<6)) &&
            !static_cast<const FWowUnitEntity&>(Entity).IsDead();
    }

    if (Entity.IsGameObject())
    {
        const FWowGameObjectEntity* GameObject = static_cast<const FWowGameObjectEntity*>(&Entity);
        return TrackingMode == 10 && GameObject &&
            GameObject->GetGameObjectType() == WowGameObjectType::MAILBOX;
    }

    if (!Entity.IsUnit())
    {
        return false;
    }

    const FWowUnitEntity* Unit = static_cast<const FWowUnitEntity*>(&Entity);
    const uint32 Flags = Unit ? Unit->GetNpcFlags() : 0;
    switch (TrackingMode)
    {
    case 1:  return (Flags & WowNpcFlags::REPAIR) != 0;
    case 2:  return (Flags & WowNpcFlags::VENDOR_FOOD) != 0;
    case 3:  return (Flags & WowNpcFlags::VENDOR_REAGENT) != 0;
    case 4:  return (Flags & WowNpcFlags::INNKEEPER) != 0;
    case 5:  return (Flags & WowNpcFlags::FLIGHTMASTER) != 0;
    case 6:  return (Flags & WowNpcFlags::TRAINER_CLASS) != 0;
    case 7:  return (Flags & WowNpcFlags::TRAINER_PROF) != 0;
    case 8:  return (Flags & WowNpcFlags::AUCTIONEER) != 0;
    case 9:  return (Flags & WowNpcFlags::BANKER) != 0;
    case 10: return false; // Mailbox is represented by a game object, not a unit.
    case 11: return false;
    default: return false;
    }
}

void SWowMinimap::PaintQuestPoi(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                                int32 LayerId, const FVector2D& Center, float Radius) const
{
    if (!Quests || Quests->SuperTrackedQuestId == 0)
        return;
    const uint32 QuestId = Quests->SuperTrackedQuestId;
    const FWowQuestLogEntry* Quest = Quests->QuestLog.FindByPredicate(
        [QuestId](const FWowQuestLogEntry& Q) { return Q.QuestId == QuestId; });
    const TArray<FWowQuestPOI>* Pois = Quest ? Quests->QuestPOIs.Find(QuestId) : nullptr;
    FVector PlayerPos; float PlayerYaw = 0.f;
    if (!Pois || !GetPlayerPositionAndRotation(PlayerPos, PlayerYaw))
        return;
    const uint32 Map = GetMap ? GetMap() : MAX_uint32;
    const bool bComplete = Quest->State == 1;
    const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();

    // Circle as a convex polygon, used to clip the (convex) POI hulls to the round map.
    constexpr int32 RimSegments = 40;
    TArray<FVector2D> Rim;
    for (int32 I = 0; I < RimSegments; ++I)
    {
        const float A = 2.f * PI * I / RimSegments;
        Rim.Add(Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
    }
    auto Cross = [](const FVector2D& A, const FVector2D& B, const FVector2D& P)
        { return (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X); };
    auto ClipToRim = [&](TArray<FVector2D> Poly)
    {
        // Sutherland-Hodgman; Rim is clockwise in screen space (Y down), so "inside" is Cross >= 0.
        for (int32 E = 0; E < Rim.Num() && Poly.Num(); ++E)
        {
            const FVector2D A = Rim[E], B = Rim[(E + 1) % Rim.Num()];
            TArray<FVector2D> Out;
            for (int32 I = 0; I < Poly.Num(); ++I)
            {
                const FVector2D P = Poly[I], Q = Poly[(I + 1) % Poly.Num()];
                const float Dp = Cross(A, B, P), Dq = Cross(A, B, Q);
                if (Dp >= 0.f) Out.Add(P);
                if ((Dp >= 0.f) != (Dq >= 0.f)) Out.Add(P + (Q - P) * (Dp / (Dp - Dq)));
            }
            Poly = MoveTemp(Out);
        }
        return Poly;
    };
    auto ToMinimap = [&](const FVector2D& World)
    {
        return WorldToMinimapCoords(FWowCoordinate::WowToUE(FVector(World.X, World.Y, 0.0)), PlayerPos, Center, Radius, PlayerYaw);
    };

    float NearestDist = TNumericLimits<float>::Max();
    FVector2D NearestPos = Center;
    bool bAnyVisible = false;
    for (const FWowQuestPOI& Poi : *Pois)
    {
        if (Poi.Points.IsEmpty() || (Map != MAX_uint32 && Poi.MapId != Map))
            continue;
        // Unfinished: objective areas. Finished: the turn-in point.
        if (bComplete ? Poi.ObjectiveIndex >= 0 : Poi.ObjectiveIndex < 0)
            continue;
        // Hull in minimap space (the projection is linear, so hull order is preserved).
        TArray<FVector2D> Pts;
        FVector2D Sum = FVector2D::ZeroVector;
        for (const FVector2D& W : Poi.Points) { Pts.Add(ToMinimap(W)); Sum += Pts.Last(); }
        const FVector2D Mid = Sum / Pts.Num();
        Pts.Sort([&Mid](const FVector2D& A, const FVector2D& B)
            { return FMath::Atan2(A.Y - Mid.Y, A.X - Mid.X) < FMath::Atan2(B.Y - Mid.Y, B.X - Mid.X); });
        const float Dist = FVector2D::Distance(Mid, Center);
        if (Dist < NearestDist) { NearestDist = Dist; NearestPos = Mid; }
        if (bComplete || Pts.Num() < 3)
        {
            if (Dist <= Radius - 4.f)
            {
                bAnyVisible = true;
                PaintFilledDisc(RenderTransform, OutDrawElements, LayerId, Mid, 5.f, FLinearColor(0, 0, 0, .8f), 16);
                PaintFilledDisc(RenderTransform, OutDrawElements, LayerId, Mid, 3.8f, WowColor::FromWow(1.f, .82f, 0.f), 16);
            }
            continue;
        }
        const TArray<FVector2D> Clipped = ClipToRim(Pts);
        if (Clipped.Num() < 3)
            continue;
        bAnyVisible = true;
        const FColor Fill = WowColor::FromWow(.24f, .55f, 1.f, .32f).ToFColor(true);
        TArray<FSlateVertex> Verts; TArray<SlateIndex> Idx;
        for (const FVector2D& P : Clipped)
            Verts.Add(FSlateVertex::Make(RenderTransform, FVector2f(P), FVector2f::ZeroVector, Fill));
        for (int32 I = 1; I + 1 < Clipped.Num(); ++I) { Idx.Add(0); Idx.Add(SlateIndex(I)); Idx.Add(SlateIndex(I + 1)); }
        FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, FSlateResourceHandle(), Verts, Idx, nullptr, 0, 0, ESlateDrawEffect::None);
    }

    // The rim arrow belongs to the quest diamonds now (PaintDiamondMarkers, Stefan 11.10: only when a
    // diamond is set, and faceted like it); the selected quest only shows its areas here.
    (void)bAnyVisible; (void)NearestPos;
}


// r255 (Stefan 12.10: "show WHERE I died so I can walk back - a gravestone on the world map and minimap"):
// a small grey headstone with a dark cross, drawn as triangles (outline first).
static void PaintMinimapGravestone(FSlateWindowElementList& Elements, int32 Layer, const FSlateRenderTransform& RT,
    const FVector2D& C, float H)
{
    auto Tri = [&](const FVector2D& A, const FVector2D& B, const FVector2D& D, const FLinearColor& Color, int32 L)
    {
        const FColor Col = Color.ToFColor(true);
        TArray<FSlateVertex> V = {FSlateVertex::Make(RT, FVector2f(A), FVector2f::ZeroVector, Col),
            FSlateVertex::Make(RT, FVector2f(B), FVector2f::ZeroVector, Col), FSlateVertex::Make(RT, FVector2f(D), FVector2f::ZeroVector, Col)};
        TArray<SlateIndex> I = {0, 1, 2};
        FSlateDrawElement::MakeCustomVerts(Elements, L, FSlateResourceHandle(), V, I, nullptr, 0, 0, ESlateDrawEffect::None);
    };
    auto Stone = [&](float Grow, const FLinearColor& Color, int32 L)
    {
        const float W = H * .36f + Grow, Bottom = C.Y + H * .5f + Grow, Shoulder = C.Y - H * .14f;
        const FVector2D BL(C.X - W, Bottom), BR(C.X + W, Bottom), SL(C.X - W, Shoulder), SR(C.X + W, Shoulder);
        Tri(BL, BR, SR, Color, L); Tri(BL, SR, SL, Color, L);
        const FVector2D Arc(C.X, Shoulder);
        constexpr int32 Steps = 8;
        for (int32 S = 0; S < Steps; ++S)
        {
            const float A0 = PI * S / Steps, A1 = PI * (S + 1) / Steps;
            Tri(Arc, Arc + FVector2D(-FMath::Cos(A0) * W, -FMath::Sin(A0) * W), Arc + FVector2D(-FMath::Cos(A1) * W, -FMath::Sin(A1) * W), Color, L);
        }
    };
    Stone(1.6f, FLinearColor(0.f, 0.f, 0.f, .9f), Layer);
    Stone(0.f, FLinearColor(.62f, .62f, .6f, 1.f), Layer + 1);
    const FLinearColor Cross(.18f, .18f, .18f, 1.f);
    const float T = FMath::Max(1.f, H * .08f);
    const FVector2D V0(C.X - T, C.Y - H * .3f), V1(C.X + T, C.Y + H * .25f);
    Tri(V0, FVector2D(V1.X, V0.Y), V1, Cross, Layer + 2); Tri(V0, V1, FVector2D(V0.X, V1.Y), Cross, Layer + 2);
    const FVector2D H0(C.X - H * .2f, C.Y - H * .13f - T), H1(C.X + H * .2f, C.Y - H * .13f + T);
    Tri(H0, FVector2D(H1.X, H0.Y), H1, Cross, Layer + 2); Tri(H0, H1, FVector2D(H0.X, H1.Y), Cross, Layer + 2);
}

void SWowMinimap::PaintCorpseMarker(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                                    int32 LayerId, const FVector2D& Center, float Radius) const
{
    // The corpse while you are a ghost on its map: a gravestone on the map, or a grey arrow on the
    // rim pointing the way with the gravestone just inside it.
    if (!Quests) return;
    const FWowDeathState& Death = Quests->DeathState;
    if (!Death.bGhost || !Death.bHasCorpse || Death.CorpseMap != Death.CurrentMap) return;
    FVector PlayerPos; float PlayerYaw = 0.f;
    if (!GetPlayerPositionAndRotation(PlayerPos, PlayerYaw)) return;
    const FVector World = FWowCoordinate::WowToUE(Death.CorpsePos);
    const FVector2D Pos = WorldToMinimapCoords(World, PlayerPos, Center, Radius, PlayerYaw);
    const FVector2D Offset = Pos - Center;
    const FSlateRenderTransform RT = AllottedGeometry.GetAccumulatedRenderTransform();
    if (Offset.Size() <= Radius - 9.f)
    {
        PaintMinimapGravestone(OutDrawElements, LayerId, RT, Pos, 15.f);
        return;
    }
    const FVector2D Dir = Offset.GetSafeNormal(), Side(-Dir.Y, Dir.X);
    const FVector2D Tip = Center + Dir * (Radius - 2.f), Notch = Tip - Dir * 10.f;
    const FVector2D Left = Tip - Dir * 15.f + Side * 7.5f, Right = Tip - Dir * 15.f - Side * 7.5f;
    auto Tri = [&](const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color, int32 L)
    {
        const FColor Col = Color.ToFColor(true);
        TArray<FSlateVertex> V = {FSlateVertex::Make(RT, FVector2f(A), FVector2f::ZeroVector, Col),
            FSlateVertex::Make(RT, FVector2f(B), FVector2f::ZeroVector, Col), FSlateVertex::Make(RT, FVector2f(C), FVector2f::ZeroVector, Col)};
        TArray<SlateIndex> I = {0, 1, 2};
        FSlateDrawElement::MakeCustomVerts(OutDrawElements, L, FSlateResourceHandle(), V, I, nullptr, 0, 0, ESlateDrawEffect::None);
    };
    const FLinearColor Outline(0.f, 0.f, 0.f, .85f);
    Tri(Tip + Dir * 2.f, Left + Side * 1.8f - Dir * 1.2f, Notch - Dir * 2.f, Outline, LayerId);
    Tri(Tip + Dir * 2.f, Notch - Dir * 2.f, Right - Side * 1.8f - Dir * 1.2f, Outline, LayerId);
    Tri(Tip, Left, Notch, FLinearColor(.85f, .85f, .85f, 1.f), LayerId + 1);
    Tri(Tip, Notch, Right, FLinearColor(.55f, .55f, .55f, 1.f), LayerId + 1);
    PaintMinimapGravestone(OutDrawElements, LayerId, RT, Tip - Dir * 26.f, 13.f);
}

void SWowMinimap::PaintDiamondMarkers(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                                      int32 LayerId, const FVector2D& Center, float Radius) const
{
    if (!DiamondTargets) return;
    TArray<FVector> Targets;
    DiamondTargets(Targets);
    FVector PlayerPos; float PlayerYaw = 0.f;
    if (Targets.IsEmpty() || !GetPlayerPositionAndRotation(PlayerPos, PlayerYaw)) return;
    const FSlateRenderTransform RT = AllottedGeometry.GetAccumulatedRenderTransform();
    auto Tri = [&](const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color, int32 Layer)
    {
        const FColor Col = Color.ToFColor(true);
        TArray<FSlateVertex> V = {
            FSlateVertex::Make(RT, FVector2f(A), FVector2f::ZeroVector, Col),
            FSlateVertex::Make(RT, FVector2f(B), FVector2f::ZeroVector, Col),
            FSlateVertex::Make(RT, FVector2f(C), FVector2f::ZeroVector, Col)};
        TArray<SlateIndex> I = {0, 1, 2};
        FSlateDrawElement::MakeCustomVerts(OutDrawElements, Layer, FSlateResourceHandle(), V, I, nullptr, 0, 0, ESlateDrawEffect::None);
    };
    // Gold facets lit from the upper left, like the world diamond: a facet facing the light is
    // pale gold, one facing away deep amber.
    const FVector2D Light = FVector2D(-1.f, -1.f).GetSafeNormal();
    auto Facet = [&](const FVector2D& Normal)
    {
        const float Lit = FMath::Clamp(.5f + .5f * float(FVector2D::DotProduct(Normal.GetSafeNormal(), Light)), 0.f, 1.f);
        return FMath::Lerp(WowColor::FromWow(.62f, .38f, 0.f), WowColor::FromWow(1.f, .93f, .45f), Lit);
    };
    const FLinearColor Outline(0.f, 0.f, 0.f, .85f);
    for (const FVector& World : Targets)
    {
        const FVector2D Pos = WorldToMinimapCoords(World, PlayerPos, Center, Radius, PlayerYaw);
        const FVector2D Offset = Pos - Center;
        if (Offset.Size() <= Radius - 7.f)
        {
            // On the map: a small four-facet gem.
            const float H = 6.5f, W = 4.5f;
            const FVector2D Top = Pos + FVector2D(0, -H), Bottom = Pos + FVector2D(0, H), L = Pos + FVector2D(-W, 0), R = Pos + FVector2D(W, 0);
            Tri(Top + FVector2D(0, -1.5f), L + FVector2D(-1.5f, 0), R + FVector2D(1.5f, 0), Outline, LayerId);
            Tri(Bottom + FVector2D(0, 1.5f), L + FVector2D(-1.5f, 0), R + FVector2D(1.5f, 0), Outline, LayerId);
            Tri(Top, L, Pos, Facet(FVector2D(-1.f, -1.f)), LayerId + 1);
            Tri(Top, Pos, R, Facet(FVector2D(1.f, -1.f)), LayerId + 1);
            Tri(Bottom, Pos, L, Facet(FVector2D(-1.f, 1.f)), LayerId + 1);
            Tri(Bottom, R, Pos, Facet(FVector2D(1.f, 1.f)), LayerId + 1);
            continue;
        }
        // Off the map: an arrowhead on the rim, cut into two facets along its spine with a notch at
        // the back, so it reads as a solid gold piece turning with the direction.
        const FVector2D Dir = Offset.GetSafeNormal();
        const FVector2D Side(-Dir.Y, Dir.X);
        const FVector2D Tip = Center + Dir * (Radius - 2.f);
        const FVector2D Notch = Tip - Dir * 10.f;
        const FVector2D Left = Tip - Dir * 15.f + Side * 7.5f, Right = Tip - Dir * 15.f - Side * 7.5f;
        const FVector2D OTip = Tip + Dir * 2.f, ONotch = Notch - Dir * 2.f;
        const FVector2D OLeft = Left + (Side * 1.8f - Dir * 1.2f), ORight = Right + (-Side * 1.8f - Dir * 1.2f);
        Tri(OTip, OLeft, ONotch, Outline, LayerId);
        Tri(OTip, ONotch, ORight, Outline, LayerId);
        Tri(Tip, Left, Notch, Facet(Side + Dir * .3f), LayerId + 1);
        Tri(Tip, Notch, Right, Facet(-Side + Dir * .3f), LayerId + 1);
        // A thin bright edge along the spine catches the light.
        Tri(Tip, Notch + Side * .9f, Notch - Side * .9f, WowColor::FromWow(1.f, .97f, .7f, .85f), LayerId + 2);
    }
}

void SWowMinimap::PaintPlayerArrow(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                                  int32 LayerId, const FVector2D& Center) const
{
    FVector PlayerPos;
    float PlayerYaw;
    if (!GetPlayerPositionAndRotation(PlayerPos, PlayerYaw))
    {
        return;
    }

    UTexture2D* ArrowTexture = PlayerArrowTexture.Get();
    if (!ArrowTexture)
    {
        return;
    }
    if (PlayerArrowBrush.GetResourceObject() != ArrowTexture)
    {
        PlayerArrowBrush.SetResourceObject(ArrowTexture);
        PlayerArrowBrush.DrawAs = ESlateBrushDrawType::Image;
    }

    // Keep the now-functional rotating marker readable without letting its
    // transparent source padding turn it into the dominant minimap element.
    const float HalfSize = 16.5f;
    TArray<FSlateVertex> Vertices;
    TArray<SlateIndex> Indices;
    const FVector2D ArrowVerts[4] = {
        FVector2D(-HalfSize, -HalfSize),
        FVector2D(HalfSize, -HalfSize),
        FVector2D(HalfSize, HalfSize),
        FVector2D(-HalfSize, HalfSize)
    };
    const FVector2D ArrowUVs[4] = {
        FVector2D(0.0f, 0.0f),
        FVector2D(1.0f, 0.0f),
        FVector2D(1.0f, 1.0f),
        FVector2D(0.0f, 1.0f)
    };

    const float CosYaw = FMath::Cos(PlayerYaw);
    const float SinYaw = FMath::Sin(PlayerYaw);
    const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();

    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVector2D Rotated(
            ArrowVerts[Index].X * CosYaw - ArrowVerts[Index].Y * SinYaw,
            ArrowVerts[Index].X * SinYaw + ArrowVerts[Index].Y * CosYaw
        );
        Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(Center + Rotated),
            FVector2f(ArrowUVs[Index]), FColor::White));
    }

    Indices.Add(0);
    Indices.Add(1);
    Indices.Add(2);
    Indices.Add(0);
    Indices.Add(2);
    Indices.Add(3);

    FSlateDrawElement::MakeCustomVerts(
        OutDrawElements,
        LayerId,
        PlayerArrowBrush.GetRenderingResource(),
        Vertices,
        Indices,
        nullptr,
        0,
        0,
        ESlateDrawEffect::None
    );
}

void SWowMinimap::PaintCircularBorder(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
                                     int32 LayerId, const FVector2D& Center, float Radius) const
{
    // Paint gold circular border
    FLinearColor BorderColor = WowColor::FromWow(1.0f, 0.8f, 0.0f, 1.0f); // Gold
    const float BorderThickness = 3.0f;

    // Create circle outline
    TArray<FSlateVertex> Vertices;
    TArray<SlateIndex> Indices;

    const int32 NumSegments = 64;
    const float AngleStep = 2.0f * PI / NumSegments;
    const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();

    // Outer circle vertices
    for (int32 i = 0; i <= NumSegments; ++i)
    {
        float Angle = i * AngleStep;
        FVector2D OuterPos = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
        FVector2D InnerPos = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (Radius - BorderThickness);

        Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(OuterPos), FVector2f::ZeroVector, BorderColor.ToFColor(true)));
        Vertices.Add(FSlateVertex::Make(RenderTransform, FVector2f(InnerPos), FVector2f::ZeroVector, BorderColor.ToFColor(true)));
    }

    // Create quad strips for border
    for (int32 i = 0; i < NumSegments; ++i)
    {
        int32 Base = i * 2;
        int32 Next = ((i + 1) % (NumSegments + 1)) * 2;

        // First triangle
        Indices.Add(Base);
        Indices.Add(Base + 1);
        Indices.Add(Next);

        // Second triangle
        Indices.Add(Next);
        Indices.Add(Base + 1);
        Indices.Add(Next + 1);
    }

    FSlateDrawElement::MakeCustomVerts(
        OutDrawElements,
        LayerId,
        FSlateResourceHandle(),
        Vertices,
        Indices,
        nullptr,
        0,
        0,
        ESlateDrawEffect::None
    );
}

FVector2D SWowMinimap::WorldToMinimapCoords(const FVector& WorldPos, const FVector& PlayerPos,
                                          const FVector2D& Center, float Radius, float PlayerYaw) const
{
    const FVector WorldOffset = WorldPos - PlayerPos;
    // The world capture is north-locked. WoW X maps to UE X while WoW Y maps
    // to negative UE Y, then the top-down capture maps UE Y horizontally and
    // UE X vertically.
    const FVector2D NorthLockedOffset(WorldOffset.Y, -WorldOffset.X);
    const FVector2D ScaledOffset = NorthLockedOffset * (Radius / (ZoomYards * FWowCoordinate::SCALE));
    (void)PlayerYaw;

    return Center + ScaledOffset;
}

FLinearColor SWowMinimap::GetEntityColor(const FWowEntity& Entity) const
{
    // Check if entity is dead by checking health or dynamic flags
    if (Entity.IsUnit())
    {
        const FWowUnitEntity* UnitEntity = static_cast<const FWowUnitEntity*>(&Entity);
        if (UnitEntity->IsDead())
        {
            return WowColor::FromWow(0.5f, 0.5f, 0.5f, 1.0f); // Grey for dead entities
        }
    }

    if (Entity.IsPlayer())
    {
        // Other players are blue
        return GroupInfo && GroupInfo->FindMember(Entity.Guid)
            ? WowColor::FromWow(.25f, .6f, 1.f) : WowColor::FromWow(1.f, .85f, .12f);
    }
    else if (Entity.IsUnit())
    {
        // Cast to unit entity to access NPC flags
        const FWowUnitEntity* UnitEntity = static_cast<const FWowUnitEntity*>(&Entity);

        // Check NPC flags for interaction types
        uint32 NpcFlags = UnitEntity->GetNpcFlags();

        // Quest givers get special yellow color (like exclamation mark)
        if (UnitEntity->IsQuestGiver())
        {
            return FLinearColor(1.0f, 1.0f, 0.0f, 1.0f); // Yellow for quest givers
        }

        // Interactable NPCs (vendors, trainers, etc.) are green
        if (UnitEntity->IsInteractable())
        {
            return FLinearColor::Green; // Green for friendly/interactable NPCs
        }

        // Check faction/hostility for combat NPCs
        uint32 FactionTemplate = Entity.GetField(UnitField::FACTIONTEMPLATE);

        // Simple faction check - this would need proper faction relationship logic
        // For now, just use basic colors
        if (FactionTemplate == 14 || FactionTemplate == 16) // Some hostile factions
        {
            return FLinearColor::Red; // Red for hostile creatures
        }
        else
        {
            return FLinearColor::Green; // Green for friendly creatures
        }
    }

    if (Entity.IsGameObject())
    {
        const FWowGameObjectEntity* GameObjectEntity = static_cast<const FWowGameObjectEntity*>(&Entity);
        const uint8 GameObjectType = GameObjectEntity ? GameObjectEntity->GetGameObjectType() : 0;
        if (GameObjectType == WowGameObjectType::MAILBOX)
        {
            return WowColor::FromWow(0.65f, 0.85f, 1.0f, 1.0f);
        }
        if (GameObjectType == 3)
        {
            return WowColor::FromWow(1.0f, 0.68f, 0.18f, 1.0f);
        }
    }

    return FLinearColor::White; // Default for other objects
}

FString SWowMinimap::GetCurrentZoneName() const
{
    if (!WorldManager.IsValid()) return FString();
    const FString Area=WorldManager->GetCurrentAreaName();
    return Area.IsEmpty()?WorldManager->GetCurrentZoneName():Area;
}

bool SWowMinimap::GetPlayerPositionAndRotation(FVector& OutPosition, float& OutYaw) const
{
    UWorld* World = WorldManager.IsValid() ? WorldManager->GetWorld() : nullptr;
    // The possessed pawn is the source of truth for the facing visible on this
    // client. Server movement orientation updates are deliberately sparse and
    // left the minimap arrow frozen at its login heading between packets.
    if (World)
    {
        if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
        {
            if (APawn* Pawn = PlayerController->GetPawn())
            {
                OutPosition = Pawn->GetActorLocation();
                OutYaw = FMath::DegreesToRadians(Pawn->GetActorRotation().Yaw);
                return true;
            }
        }
    }

    // Keep a network-state fallback for the short interval before possession.
    if (EntityManager)
    {
        if (FWowPlayerEntity* LocalPlayer = EntityManager->GetLocalPlayer())
        {
            OutPosition = FWowCoordinate::WowToUE(LocalPlayer->Movement.Position);
            OutYaw = FMath::DegreesToRadians(
                FWowCoordinate::WowOrientationToUEYaw(LocalPlayer->Movement.Orientation));
            return true;
        }
    }

    return false;
}

bool SWowMinimap::IsWithinMinimapCircle(const FVector2D& Center, float Radius, const FVector2D& Point) const
{
    return FVector2D::Distance(Center, Point) <= Radius;
}
