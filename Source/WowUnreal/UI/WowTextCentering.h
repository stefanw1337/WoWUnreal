#pragma once

#include "CoreMinimal.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/SlateRenderTransform.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SBox.h"

/**
 * Optical centring for button and cell labels.
 *
 * Slate centres a text block by its line box (font ascender + descender). WoW's fonts have
 * tall ascenders and deep descenders, so the capital letters do not sit on the middle of the
 * box; on a button the word looks too high or too low depending on the font and size.
 * WowCapCenterTransform returns a vertical render offset that puts the middle of the capital
 * letters on the middle of the box. Use it on any label that sits in a VAlign_Center slot.
 */
inline float WowCapCenterOffset(const FSlateFontInfo& Font)
{
    if (!FSlateApplication::IsInitialized()) return 0.f;
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const float LineH = static_cast<float>(Measure->GetMaxCharacterHeight(Font));
    const float Descender = -static_cast<float>(Measure->GetBaseline(Font)); // GetBaseline is <= 0
    if (LineH <= 0.f) return 0.f;
    // Slate font sizes are points at 96 DPI; capitals of FRIZQT/MORPHEUS are ~0.70 em tall.
    const float CapH = 0.70f * Font.Size * (96.f / 72.f);
    const float CapCenterFromTop = (LineH - Descender) - CapH * .5f;
    return FMath::RoundToFloat(LineH * .5f - CapCenterFromTop);
}

inline TOptional<FSlateRenderTransform> WowCapCenterTransform(const FSlateFontInfo& Font)
{
    return FSlateRenderTransform(FVector2f(0.f, WowCapCenterOffset(Font)));
}

/**
 * Label offset for the red/blue Glue-Panel-Button art (TexCoords 0-.75 of a 64-row texture):
 * the painted button only covers rows 7-37 of the 48 shown, so its middle sits 1/24 of the
 * button height above the widget's middle. Centres the capitals on the painted art.
 * Pass the widget that has the button's size (read from its last layout).
 */
inline TAttribute<TOptional<FSlateRenderTransform>> WowGlueLabelTransform(TWeakPtr<SWidget> SizedWidget, const FSlateFontInfo& Font)
{
    const float CapOffset = WowCapCenterOffset(Font);
    return TAttribute<TOptional<FSlateRenderTransform>>::CreateLambda([SizedWidget, CapOffset]() -> TOptional<FSlateRenderTransform>
    {
        const TSharedPtr<SWidget> Pinned = SizedWidget.Pin();
        const float H = Pinned.IsValid() ? Pinned->GetCachedGeometry().GetLocalSize().Y : 0.f;
        return FSlateRenderTransform(FVector2f(0.f, FMath::RoundToFloat(CapOffset - H / 24.f)));
    });
}

/**
 * Window title (FrameXML FontString with justifyV MIDDLE, like CharacterNameText on the
 * character sheet): the line is centred in its box and the capitals sit on the box's middle.
 * Native windows used top-aligned text, so their titles sat lower than the character name.
 */
inline TSharedRef<SWidget> WowTitleBox(TSharedRef<SWidget> Text, const FSlateFontInfo& Font)
{
    return SNew(SBox).VAlign(VAlign_Center).Visibility(EVisibility::HitTestInvisible)
        [ SNew(SBox).RenderTransform(WowCapCenterTransform(Font))[Text] ];
}
