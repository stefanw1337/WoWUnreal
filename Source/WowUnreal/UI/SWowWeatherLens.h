#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "WowWeatherComponent.h"

class AWowAudioManager;

// Cosmetic lens residue. HitTestInvisible and below the HUD; never captures input.
class SWowWeatherLens : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowWeatherLens) {} SLATE_ARGUMENT(UWorld*, World) SLATE_ATTRIBUTE(TSharedPtr<SWidget>, Minimap) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    TSharedRef<SWidget> CreateMinimapFrostOverlay();
    void PaintMinimapFrost(const FGeometry&, FSlateWindowElementList&, int32) const;
    /** Current frost-lens level, 0 (none) - 1 (deep frost); 0 when the option is off (r233). */
    static float GetFrostLevel();
    /** Frost on an interface frame (action bars, unit frames...): rime along its edges, crystals,
        and at the deepest stage frosted glass over it. Rect in Geo's local space; Seed varies the
        pattern per frame. Draws nothing without frost. (r233, for Claude-UI's frame painting) */
    static void PaintFrameFrost(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer, const FSlateRect& Rect, int32 Seed);
    virtual void Tick(const FGeometry&, double Now, float Dt) override;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
private:
    struct FDrop {
        FVector2D Position, Velocity=FVector2D::ZeroVector, LastTrail;
        float Age=0, Life=8, Radius=8, Ice=0, Hold=1, Phase=0, Speed=.04f, Drift=0, Water=1, TrailClock=0, TrailStrength=0;
        float MeltRate=.25f, MeltSize=.55f;
        bool Snow=false, FromWater=false;
    };
    struct FTrail { FVector2D A,B; float Age=0, Life=4, Width=1, Alpha=.1f; bool FromWater=false; };
    TArray<FTrail> Trails;
    TAttribute<TSharedPtr<SWidget>> Minimap;
    float NearbyHeat=0;
    double NextHeat=0;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UWowWeatherComponent> Weather;
    TWeakObjectPtr<AWowAudioManager> Audio;
    bool WasUnderwater=false;
    void AddDrop(bool Snow, bool FromWater);
    TArray<FDrop> Drops;
    FRandomStream Random{378321};
    double NextFind=0;
    float SpawnBudget=0;
    float LensFacingUp=0; // 0 level/down .. 1 looking steeply up (more and centred drops)
    float Frost=0;
    void PaintFrost(const FGeometry&, FSlateWindowElementList&, int32) const;
};
