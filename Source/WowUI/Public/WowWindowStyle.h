#pragma once

#include "CoreMinimal.h"
#include "WowColor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SLeafWidget.h"

class FWowFrameManager;
class UTexture2D;

// Native windows share the same chrome; FrameXML keeps its authored artwork.
// Textures are loaded from the user's data, never distributed with the client.
struct WOWUI_API FWowWindowStyle
{
    static void Load(FWowFrameManager& Frames);
    static void LoadTextures(TFunctionRef<UTexture2D*(const FString&)> LoadTexture);
    static void Reset();
    static const FSlateBrush* Background();
    static const FSlateBrush* Edge(int32 Index);
    static const FButtonStyle* Button();
    static FSlateFontInfo TitleFont(int32 Size = 18);
    static FLinearColor Gold() { return WowColor::FromWow(1.f, .82f, .1f); }
    static bool HasOriginalBorder();

    // GameTooltip backdrop: UI-Tooltip-Background tinted TOOLTIP_DEFAULT_BACKGROUND_COLOR
    // with the 16 px UI-Tooltip-Border edges, and GameTooltipHeaderText/GameTooltipText fonts.
    static const FSlateBrush* TooltipBackground();
    static const FSlateBrush* TooltipEdge(int32 Index);
    static bool HasTooltipBorder();
    // Stefan (09.10): no grey backgrounds - tooltips, menus and dropdowns all use the chat
    // window's background: black, half transparent.
    static FLinearColor ChatBackgroundColor() { return FLinearColor(0.f, 0.f, 0.f, .5f); }
    // Black like the chat, but nearly opaque: at the chat's 50 % the character sheet's stats read
    // through an item tooltip (Stefan 10.10).
    // FrameXML tooltips multiply this with UI-Tooltip-Background, which has its own alpha, so the
    // half-transparent chat value read as nearly see-through (Stefan 10.10, talent tooltip).
    static FLinearColor TooltipBackgroundColor() { return FLinearColor(0.f, 0.f, 0.f, .95f); }
    static FSlateFontInfo TooltipFont(bool bHeader) { return TitleFont(bHeader ? 14 : 11); }

    /** The stock control art (OptionsSliderTemplate, OptionsCheckButtonTemplate, UIPanelScrollBarTemplate),
     *  loaded with the window art so native windows can use it at the login screen too, where no
     *  frame manager exists. Null when the user's data does not have it. */
    static UTexture2D* ControlTexture(const FString& Path);
};

// SBorder-compatible so existing delegates, visibility, drag and hit testing
// stay on the same widget. Border art is paint only, with no input overlay.
class WOWUI_API SWowWindowFrame : public SBorder
{
public:
    void Construct(const SBorder::FArguments& Args);
    // Children scroll first; wheel events over chrome/empty space stop here.
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override
    { return FReply::Handled(); }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
        const FSlateRect& CullingRect, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
};

// Tooltip box with the original GameTooltip backdrop (native item, talent and compare tooltips).
class WOWUI_API SWowTooltipFrame : public SBorder
{
public:
    void Construct(const SBorder::FArguments& Args);
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
        const FSlateRect& CullingRect, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
};

// alphaMode="ADD" texture (ButtonHilight-Square, UI-Character-Tab-Highlight, CheckButtonHilight,
// UI-CheckBox-Highlight ...). Drawn with normal alpha blending their black background shows as a
// dark box around the button; this paints them additively (premultiplied blend, zero alpha).
class WOWUI_API SWowAddImage : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowAddImage) : _Image(nullptr), _ColorAndOpacity(FLinearColor::White) {}
        SLATE_ATTRIBUTE(const FSlateBrush*, Image)
        SLATE_ATTRIBUTE(FLinearColor, ColorAndOpacity)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Image = Args._Image; Color = Args._ColorAndOpacity; }
    virtual FVector2D ComputeDesiredSize(float) const override;
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
        const FSlateRect& CullingRect, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
private:
    TAttribute<const FSlateBrush*> Image;
    TAttribute<FLinearColor> Color;
};
