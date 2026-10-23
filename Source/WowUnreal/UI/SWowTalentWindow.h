#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UWowConnectionManager;
class UWowUIManager;
class SCanvas;
class SBox;
class SScrollBox;
class SToolTip;
class FWowRetainedSlateBrush;
struct FTalentDbcEntry;
struct FTalentTabDbcEntry;
struct FSlateBrush;
struct FButtonStyle;

/**
 * Native replica of the 3.3.5 PlayerTalentFrame (Blizzard_TalentUI.xml / TalentFrameBase.lua):
 * same 384x512 layout and art (character-sheet frame, tree backgrounds, talent buttons with
 * slot/rank art, branch and arrow pieces placed by the original TALENT_BRANCH_ARRAY rules,
 * bottom tabs, points bar, preview Learn/Reset bar, dual-spec tabs/Activate) plus the
 * GlyphFrame tab. Art is read from the user's MPQs at runtime. Drawn at the FrameXML UI scale.
 */
class WOWUNREAL_API SWowTalentWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowTalentWindow) {}
        SLATE_ARGUMENT(UWowUIManager*, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager);
    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

    void ToggleVisibility();
    bool IsVisible() const;
    /** Rebuild the frame for the current state. */
    void RefreshTalentData();
    /** Size of the frame in Slate units (384x512 at the UI scale). */
    FVector2D GetPanelSize() const;

private:
    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;
    TWeakObjectPtr<UWowUIManager> UIManager;
    float Scale = 1.25f;
    /** 0..2 = talent trees, 3 = glyphs (GLYPH_TALENT_TAB). */
    int32 SelectedTab = 0;
    /** Spec shown (0-based); -1 = the active spec. */
    int32 ViewGroup = -1;
    bool bConfirmLearn = false;
    uint64 BuiltStamp = 0;
    float SavedScroll = 0.f;

    TSharedPtr<SBox> Root;
    TSharedPtr<SScrollBox> TreeScroll;

    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> Brushes;
    mutable TMap<FString, double> FailedBrushes;
    mutable TMap<FString, TSharedPtr<FButtonStyle>> ButtonStyles;

    UWowConnectionManager* Connection() const;
    int32 ShownGroup() const;
    bool IsViewingActive() const;
    uint64 StateStamp() const;
    TArray<const FTalentTabDbcEntry*> PlayerTabs() const;
    bool ShowGlyphTab() const;

    const FSlateBrush* Brush(const FString& Path, float U0 = 0.f, float U1 = 1.f, float V0 = 0.f, float V1 = 1.f) const;
    const FButtonStyle* TextureButtonStyle(const FString& Up, const FString& Down, const FString& Highlight,
        float U0 = 0.f, float U1 = 1.f, float V0 = 0.f, float V1 = 1.f) const;
    FSlateFontInfo Font(float WowPixels) const;
    float TextWidth(const FString& Text, const FSlateFontInfo& FontInfo) const;

    // Places Widget at the WoW-unit rectangle (X, Y from the top-left, W x H).
    void Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    void PlaceImage(SCanvas& Canvas, float X, float Y, float W, float H, const FSlateBrush* Image,
        const FLinearColor& Tint = FLinearColor::White, bool bDesaturate = false) const;
    void PlaceText(SCanvas& Canvas, float X, float Y, float W, float H, const FString& Text, const FLinearColor& Color,
        float WowPixels, EHorizontalAlignment HAlign = HAlign_Center) const;

    TSharedRef<SWidget> BuildFrame();
    void BuildTree(SCanvas& Canvas, const FTalentTabDbcEntry& Tab, float ScrollHeight);
    void BuildGlyphs(SCanvas& Canvas);
    void BuildTabs(SCanvas& Canvas);
    void BuildSpecTabs(SCanvas& Canvas);
    void BuildBars(SCanvas& Canvas, const FTalentTabDbcEntry* Tab, float& OutPointsBarTop);
    TSharedRef<SToolTip> MakeTooltip(const TArray<struct FWowTalentTooltipLine>& Lines) const;

    FReply OnTalentMouse(uint32 TalentId, const FPointerEvent& Event);
    void ScrollBy(float Delta);
};
