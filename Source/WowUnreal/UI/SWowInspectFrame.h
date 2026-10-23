#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UWowConnectionManager;
class UWowUIManager;
class UTextureRenderTarget2D;
class SCanvas;
class SBox;
class FWowRetainedSlateBrush;
struct FSlateBrush;
struct FButtonStyle;
struct FWowItemTooltipLine;

/**
 * Native InspectFrame (3.3.5 Blizzard_InspectUI, paper doll tab): the other player's portrait,
 * name, "Level N Race Class", the 3D model (rendered by an AWowCharacterPreview the controller
 * owns) and the 19 equipment slots read from the player's visible item fields, with tooltips.
 * Positions are InspectFrame / PaperDollFrame.xml units x UI scale (384 x 512 frame).
 */
class WOWUNREAL_API SWowInspectFrame : public SCompoundWidget
{
public:
    static constexpr float FrameW = 384.f;
    static constexpr float FrameH = 512.f;

    SLATE_BEGIN_ARGS(SWowInspectFrame) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager);
    void Open(uint64 InGuid, UTextureRenderTarget2D* ModelTarget);
    void Close();
    bool IsOpen() const { return bVisible; }
    uint64 GetInspectGuid() const { return Guid; }
    FVector2D GetPanelSize() const { return FVector2D(FrameW, FrameH) * Scale; }
    /** Called by the controller when the inspected player's model needs rotating. */
    TFunction<void(float)> OnRotateModel;

    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
    void Rebuild();
    uint32 ComputeStateStamp() const;
    void Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    FSlateFontInfo Fnt(float WowPixels) const;
    TSharedRef<SWidget> Img(const FString& Path) const;
    TSharedRef<SWidget> Label(const FText& Text, float WowPixels, FLinearColor Color) const;
    const FSlateBrush* LoadBrush(const FString& TexturePath) const;
    /** OutLines: the item's tooltip lines (with the inspected enchants and gems), empty for an empty slot. */
    TSharedRef<SWidget> SlotButton(uint8 EquipmentSlot, const TCHAR* EmptyArt, TArray<FWowItemTooltipLine>* OutLines = nullptr) const;
    /** The model area (drag with the left button to turn the model). */
    TWeakPtr<SWidget> ModelArea;
    bool bRotatingModel = false;
    bool bLeftWasDown = false;
    FVector2D LastRotateCursor = FVector2D::ZeroVector;

    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;
    TWeakObjectPtr<UWowUIManager> UIManager;
    uint64 Guid = 0;
    bool bVisible = false;
    float Scale = 1.f;
    uint32 LastStateStamp = 0;
    double NextRangeCheck = 0.0;
    TSharedPtr<SBox> Root;
    TSharedPtr<FWowRetainedSlateBrush> ModelBrush;
    TSharedPtr<FWowRetainedSlateBrush> PortraitBrush;
    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> TextureBrushes;
    mutable TMap<FString, double> FailedTextureTimes;
};
