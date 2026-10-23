#include "SWowLoginOptions.h"
#include "Misc/DelayedAutoRegister.h"
#include "../WowVRSupport.h"
#include "../WowVRComfort.h"
#include "WowTestControls.h"
#include "WowWindowStyle.h"
#include "Widgets/Layout/SWrapBox.h"
#include "WowFrameLayout.h"

#include "WowAudioManager.h"
#include "WowClientPreferences.h"
#include "../WowCursorManager.h"
#include "WowWotlkStyle.h"
#include "WowNativeWidgetStyle.h"
#include "Widgets/Layout/SScaleBox.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SWowPanelScrollBar.h"
#include "Mpq/MpqManager.h"
#include "Rendering/DrawElements.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/SlateTypes.h"
#include "Framework/Application/SlateApplication.h"

// r418: the distance fog slider's saved value (Experimental > Weather and the settings app) is
// applied when the engine has started, so it holds from the first frame.
static FDelayedAutoRegisterHelper GWowDistanceFogRestore(EDelayedRegisterRunPhase::EndOfEngineInit, []()
{
    float Fog = 1.f;
    if (GConfig && GConfig->GetFloat(TEXT("WowUnreal.ClientPreferences"), TEXT("DistanceFog"), Fog, GGameUserSettingsIni) && FMath::IsFinite(Fog))
        if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("wow.DistanceFog")))
            C->Set(FMath::Clamp(Fog, 0.f, 2.f), ECVF_SetByCode);
});

namespace
{
const FLinearColor LoginOptionsGold(1.0f, 0.82f, 0.10f, 1.0f);
const FLinearColor LoginOptionsText(0.92f, 0.90f, 0.84f, 1.0f);
const FLinearColor LoginOptionsRed(0.38f, 0.025f, 0.012f, 1.0f);

// Stefan (09.10): no grey anywhere - tooltips, dropdowns, check boxes, sliders and scroll bars
// use the chat window's black-and-gold look instead of Slate's default grey.
const FLinearColor OptionsEdge(.55f, .45f, .2f, .9f);
const FSlateBrush* WowDarkBrush()
{
    static const FSlateRoundedBoxBrush Brush(FLinearColor(0.f, 0.f, 0.f, .85f), 3.f, OptionsEdge, 1.f);
    return &Brush;
}
TSharedRef<SToolTip> WowTip(const FString& Text)
{
    return SNew(SToolTip).BorderImage(WowDarkBrush()).TextMargin(FMargin(8.f, 6.f))
        [ SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true).WrapTextAt(440.f)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).ColorAndOpacity(LoginOptionsText) ];
}
const FCheckBoxStyle* WowCheckStyle()
{
    // Stefan 11.10: the stock check box (OptionsCheckButtonTemplate) when the user's data has it;
    // the tick and hover highlight are painted by SWowCheckButton. Rebuilt when the art (re)loads.
    static FCheckBoxStyle Style = []
    {
        FCheckBoxStyle S = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
        // UE5's check box draws its own outlined box behind the image: with the stock art that was a
        // second, square border around UI-CheckBox-Up (Stefan 11.10). The art carries its own frame.
        static const FSlateNoResource NoBackground;
        S.SetBackgroundImage(NoBackground).SetBackgroundHoveredImage(NoBackground).SetBackgroundPressedImage(NoBackground);
        const FSlateRoundedBoxBrush Off(FLinearColor(0.f, 0.f, 0.f, .6f), 3.f, OptionsEdge, 1.f, FVector2f(18.f, 18.f));
        const FSlateRoundedBoxBrush OffHover(FLinearColor(.2f, .14f, .03f, .8f), 3.f, FWowWindowStyle::Gold(), 1.f, FVector2f(18.f, 18.f));
        const FSlateRoundedBoxBrush On(FLinearColor(1.f, .82f, .1f, 1.f), 3.f, FLinearColor(0.f, 0.f, 0.f, 1.f), 3.f, FVector2f(18.f, 18.f));
        S.SetUncheckedImage(Off).SetUncheckedHoveredImage(OffHover).SetUncheckedPressedImage(OffHover)
            .SetCheckedImage(On).SetCheckedHoveredImage(On).SetCheckedPressedImage(On)
            .SetUndeterminedImage(Off).SetUndeterminedHoveredImage(OffHover).SetUndeterminedPressedImage(OffHover);
        return S;
    }();
    // r721 (Stefan: "I pressed esc and went to Video, game crashed" - Assertion Index >= 0 in FUObjectArray from
    // SCheckBox painting inside SWowCheckButton): the stock-art brushes kept raw pointers to the UI-CheckBox
    // textures. When the window style resources were reset (UI reload) those textures were collected, but the
    // style was only rebuilt when a *different* Up texture appeared - not when it disappeared, and not when Down
    // changed or a new texture reused the old address. Weak pointers now notice all of that; without the art the
    // rounded fallback boxes come back.
    static const FCheckBoxStyle Fallback = Style;
    static TWeakObjectPtr<UTexture2D> BuiltUp, BuiltDown;
    static bool bBuiltFromArt = false;
    UTexture2D* Up = FWowWindowStyle::ControlTexture(TEXT("Interface\\Buttons\\UI-CheckBox-Up"));
    UTexture2D* Down = FWowWindowStyle::ControlTexture(TEXT("Interface\\Buttons\\UI-CheckBox-Down"));
    if (!Up && bBuiltFromArt)
    {
        Style = Fallback;
        bBuiltFromArt = false;
        BuiltUp.Reset(); BuiltDown.Reset();
    }
    if (Up && (!bBuiltFromArt || Up != BuiltUp.Get() || Down != BuiltDown.Get()))
    {
        BuiltUp = Up; BuiltDown = Down; bBuiltFromArt = true;
        auto Box = [](UTexture2D* Texture)
        {
            FSlateBrush Brush;
            Brush.SetResourceObject(Texture);
            Brush.ImageSize = FVector2D(28.f, 28.f);
            Brush.DrawAs = ESlateBrushDrawType::Image;
            return Brush;
        };
        const FSlateBrush Normal = Box(Up), Pushed = Box(Down ? Down : Up);
        Style.SetUncheckedImage(Normal).SetUncheckedHoveredImage(Normal).SetUncheckedPressedImage(Pushed)
            .SetCheckedImage(Normal).SetCheckedHoveredImage(Normal).SetCheckedPressedImage(Pushed)
            .SetUndeterminedImage(Normal).SetUndeterminedHoveredImage(Normal).SetUndeterminedPressedImage(Pushed);
        Style.SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
    }
    return &Style;
}
const FScrollBarStyle* WowScrollStyle()
{
    static const FScrollBarStyle Style = []
    {
        FScrollBarStyle S = FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar");
        const FSlateRoundedBoxBrush Track(FLinearColor(0.f, 0.f, 0.f, .45f), 4.f);
        const FSlateRoundedBoxBrush Thumb(FLinearColor(.75f, .58f, .12f, 1.f), 4.f);
        const FSlateRoundedBoxBrush ThumbHot(FLinearColor(1.f, .82f, .1f, 1.f), 4.f);
        S.SetVerticalBackgroundImage(Track).SetHorizontalBackgroundImage(Track)
            .SetVerticalTopSlotImage(Track).SetVerticalBottomSlotImage(Track)
            .SetNormalThumbImage(Thumb).SetHoveredThumbImage(ThumbHot).SetDraggedThumbImage(ThumbHot);
        return S;
    }();
    return &Style;
}
// Stefan 11.10: UIPanelScrollBarTemplate (arrows, knob) at the right of every options page instead
// of Slate's gold bar.
TSharedRef<SWidget> WowScroll(const TSharedRef<SScrollBox>& Box)
{
    return SWowPanelScrollBar::Wrap(Box, 1.15f);
}
const FComboBoxStyle* WowComboStyle()
{
    static const FComboBoxStyle Style = []
    {
        FComboBoxStyle S = FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>("ComboBox");
        FComboButtonStyle Combo = S.ComboButtonStyle;
        const FSlateRoundedBoxBrush Normal(FLinearColor(0.f, 0.f, 0.f, .6f), 3.f, OptionsEdge, 1.f);
        const FSlateRoundedBoxBrush Hover(FLinearColor(.2f, .14f, .03f, .85f), 3.f, FWowWindowStyle::Gold(), 1.f);
        FButtonStyle Button = Combo.ButtonStyle;
        Button.SetNormal(Normal).SetHovered(Hover).SetPressed(Hover).SetDisabled(Normal);
        Combo.SetButtonStyle(Button);
        Combo.SetMenuBorderBrush(*WowDarkBrush());
        S.SetComboButtonStyle(Combo);
        return S;
    }();
    return &Style;
}
const FTableRowStyle* WowComboRowStyle()
{
    static const FTableRowStyle Style = []
    {
        FTableRowStyle S = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
        const FSlateNoResource None;
        const FSlateColorBrush Hover(FLinearColor(.45f, .32f, .05f, .6f));
        const FSlateColorBrush Picked(FLinearColor(.6f, .42f, .05f, .85f));
        S.SetEvenRowBackgroundBrush(None).SetOddRowBackgroundBrush(None)
            .SetEvenRowBackgroundHoveredBrush(Hover).SetOddRowBackgroundHoveredBrush(Hover)
            .SetActiveBrush(Picked).SetActiveHoveredBrush(Picked).SetInactiveBrush(Picked).SetInactiveHoveredBrush(Picked)
            .SetSelectorFocusedBrush(None);
        return S;
    }();
    return &Style;
}

TSharedPtr<IToolTip> OptionTip(const FString& Key)
{
    static const TMap<FString, FString> Tips = {
        {TEXT("instantquesttext"), TEXT("Quest and gossip text appears at once instead of word by word.")},
        {TEXT("autoquestwatch"), TEXT("Newly accepted quests are put in the objectives tracker automatically.")},
        {TEXT("showquestlevel"), TEXT("Shows each quest's level in front of its title, in the quest log and the objectives tracker.")},
        {TEXT("autotrackzonequests"), TEXT("Also tracks every quest of the zone you are in; they leave the tracker when you leave the zone.")},
        {TEXT("chatbubbles"), TEXT("Says, yells and NPC speech appear in a bubble over the speaker.")},
        {TEXT("chatbubblesparty"), TEXT("Party chat also appears in bubbles over your party members.")},
        {TEXT("floatingcombattextdamage"), TEXT("Damage and healing numbers float up from your target.")},
        {TEXT("combattextsum"), TEXT("Hits on the same target within 2 seconds are added up into one bigger number instead of many small ones.")},
        {TEXT("combattextsumheals"), TEXT("Heals and resource gains you receive are added up into one number that keeps counting, like the damage option. They float downwards, into you.")},
        {TEXT("combattextresource"), TEXT("Mana, rage, energy and runic power you gain from spells and effects float down over your character in the resource colour.")},
        {TEXT("autolootdefault"), TEXT("Loot is taken automatically when you open a body or chest. Hold the Auto Loot Key (below) to see the loot window instead.")},
        {TEXT("lootundermouse"), TEXT("The loot window opens at the mouse pointer instead of at the left side of the screen.")},
        {TEXT("actionbar3"), TEXT("Shows the extra action bar above the main bar, on the left.")},
        {TEXT("actionbar4"), TEXT("Shows the extra action bar above the main bar, on the right.")},
        {TEXT("actionbar5"), TEXT("Shows the vertical action bar at the right screen edge.")},
        {TEXT("actionbar6"), TEXT("Shows a second vertical action bar next to the right one. Like the original client it needs the Right Bar turned on.")},
        {TEXT("alwaysshowactionbars"), TEXT("Empty slots on your action bars stay visible, not only while you drag a spell or item.")},
        {TEXT("showcooldowntime"), TEXT("Writes the remaining cooldown time on action buttons.")},
        {TEXT("holdtorecast"), TEXT("Keep an action key held down and the spell is cast again as soon as it can be.")},
        {TEXT("showtargetoftarget"), TEXT("Shows a small frame with your target's target, and your focus's target.")},
        {TEXT("playerstatustext"), TEXT("Writes the health and mana/rage/energy numbers on the player and target bars.")},
        {TEXT("statustextpercentage"), TEXT("Shows health and resources as percentages instead of numbers.")},
        {TEXT("tooltipatcursor"), TEXT("Tooltips appear next to the mouse pointer instead of in the bottom-right corner.")},
        {TEXT("weatherlens"), TEXT("Raindrops and melting snow run over the camera when you are outside in bad weather.")},
        {TEXT("wetlens"), TEXT("Water drips run down the camera for a moment after you leave water.")},
        {TEXT("frostlens"), TEXT("Frost grows in the corners of the view and the picture turns a little blue in cold places.")},
        {TEXT("satelliterecord"), TEXT("The first time you stand in a part of the world in daylight, it is photographed from above for the Satellite v2 world map.")},
        {TEXT("headlook"), TEXT("Your character turns its head towards your target, or where the camera looks when you have none.")},
        {TEXT("soundenabled"), TEXT("Sound effects and ambient sounds on or off (music has its own toggle).")},
        {TEXT("soundinbackground"), TEXT("Keep playing sound when the game window is not in front.")},
        {TEXT("soundeffects"), TEXT("Spell, combat and interface sounds.")},
        {TEXT("musicenabled"), TEXT("Zone music.")},
        {TEXT("loopmusic"), TEXT("Music starts again when a track ends, instead of pausing until the next zone change.")},
        {TEXT("ambientenabled"), TEXT("Background sounds of the world: wind, birds, water, crowds.")},
        {TEXT("moviesubtitles"), TEXT("Enables subtitles during the intro cinematic.")},
        {TEXT("showhelm"), TEXT("Uncheck this box to hide your character's helm.")},
        {TEXT("showcloak"), TEXT("Uncheck this box to hide your character's cloak.")}
    };
    const FString* Tip = Tips.Find(Key);
    return Tip ? TSharedPtr<IToolTip>(WowTip(*Tip)) : TSharedPtr<IToolTip>();
}

// r388 (Stefan 16.10: the login screen's Options should look like the login screen - its blue
// buttons - and the in-game one like the game): set while the login screen's Options is built.
bool GWowOptionsGlueLook = false;

/** Glue-Panel-Button-Up/Down-Blue cropped like the login screen's buttons (SWowLoginWidget). */
const FButtonStyle* WowGlueButtonStyle()
{
    static FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
    static UTexture2D* BuiltFrom = nullptr;
    UTexture2D* Up = FWowWindowStyle::ControlTexture(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Up-Blue"));
    UTexture2D* Down = FWowWindowStyle::ControlTexture(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Down-Blue"));
    if (!Up) return FWowWindowStyle::Button();
    if (BuiltFrom != Up)
    {
        BuiltFrom = Up;
        auto Brush = [](UTexture2D* Texture)
        {
            FSlateBrush Result;
            Result.SetResourceObject(Texture);
            Result.DrawAs = ESlateBrushDrawType::Image;
            Result.SetUVRegion(FBox2f(FVector2f(8.f / 256.f, 7.f / 64.f), FVector2f(138.f / 256.f, 37.f / 64.f)));
            return Result;
        };
        const FSlateBrush Normal = Brush(Up);
        FSlateBrush Hovered = Normal;
        Hovered.TintColor = FLinearColor(1.2f, 1.2f, 1.2f);
        Style.SetNormal(Normal).SetHovered(Hovered).SetDisabled(Normal).SetPressed(Down ? Brush(Down) : Normal);
        Style.SetNormalPadding(FMargin(4, 0)).SetPressedPadding(FMargin(4, 1, 4, 0));
    }
    return &Style;
}

TSharedRef<SWidget> MakeOptionButton(const FString& Label, FOnClicked OnClicked, const FString& Tip = FString())
{
    TSharedPtr<IToolTip> ToolTip;
    if (!Tip.IsEmpty()) ToolTip = WowTip(Tip);
	return SNew(SButton)
		.ButtonStyle(GWowOptionsGlueLook ? WowGlueButtonStyle() : FWowWindowStyle::Button()).HAlign(HAlign_Center)
		.ContentPadding(FMargin(10.0f, 6.0f))
        .ToolTip(ToolTip)
		.OnClicked(OnClicked)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
			.ColorAndOpacity(LoginOptionsGold)
		];
}
}

/** OptionsSliderTemplate (3.3.5): the UI-SliderBar backdrop (8-unit border, insets 3,3,6,6) with the
 *  UI-SliderBar-Button-Horizontal thumb; click or drag anywhere on the bar, the mouse wheel steps it.
 *  Optional Low/High captions under the ends like the template's Low and High font strings.
 *  In VR a step button at each end stays (press once for a small step, hold to keep moving): easier
 *  than dragging the small thumb with the VR pointer (Stefan 09.10). Stefan 11.10: "as WotLK as
 *  possible" - no step buttons on a monitor, the stock art instead of the gold rounded boxes. */
/** One wheel notch / arrow press: Step further, or as many Steps as it takes for the setting to change.
 *  Settings round their value (sizes to 5 %, UI scale to 1 %), so a single small step was rounded back
 *  and the wheel moved a slider once and then never again (Stefan 11.10). */
static void WowSliderStep(const TAttribute<float>& Value, const FOnFloatValueChanged& OnChanged, float Step, float Direction)
{
    if (!OnChanged.IsBound() || Direction == 0.f) return;
    const float Before = Value.Get();
    for (int32 Steps = 1; Steps <= 50; ++Steps)
    {
        const float Target = FMath::Clamp(Before + Direction * Step * Steps, 0.f, 1.f);
        OnChanged.Execute(Target);
        if (!Value.IsBound() || !FMath::IsNearlyEqual(Value.Get(), Before, 1e-4f) || Target <= 0.f || Target >= 1.f) return;
    }
}

class SWowSliderBar : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowSliderBar) : _Value(0.f), _StepSize(.02f) {}
        SLATE_ATTRIBUTE(float, Value)
        SLATE_ARGUMENT(float, StepSize)
        SLATE_EVENT(FOnFloatValueChanged, OnValueChanged)
    SLATE_END_ARGS()

    static constexpr float ArtScale = 1.25f; // design units -> this window's Slate units

    void Construct(const FArguments& Args)
    {
        Value = Args._Value;
        Step = Args._StepSize;
        OnChanged = Args._OnValueChanged;
        // Stefan 11.10: the percentage followed the drag but the knob stayed put. The value is a
        // plain bound attribute, which Slate's paint caching does not watch; repaint when it changes.
        PaintedValue = Value.Get();
        RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
        {
            const float Now = Value.Get();
            if (!FMath::IsNearlyEqual(Now, PaintedValue)) { PaintedValue = Now; Invalidate(EInvalidateWidgetReason::Paint); }
            return EActiveTimerReturnType::Continue;
        }));
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(144.f, 17.f) * ArtScale; }
    virtual bool SupportsKeyboardFocus() const override { return false; }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
        int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
    {
        const bool bEnabled = bParentEnabled && IsEnabled();
        const ESlateDrawEffect Effect = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
        const FVector2D Size = G.GetLocalSize();
        const float S = FMath::Min(ArtScale, float(Size.Y) / 17.f);
        const FColor Tint = Style.GetColorAndOpacityTint().ToFColor(true);
        UTexture2D* Background = FWowWindowStyle::ControlTexture(TEXT("Interface\\Buttons\\UI-SliderBar-Background"));
        UTexture2D* Border = FWowWindowStyle::ControlTexture(TEXT("Interface\\Buttons\\UI-SliderBar-Border"));
        UTexture2D* Thumb = FWowWindowStyle::ControlTexture(TEXT("Interface\\Buttons\\UI-SliderBar-Button-Horizontal"));
        // Bar: 17 units high, centred vertically.
        const float BarH = 17.f * S;
        const FVector2D BarPos(0.f, (Size.Y - BarH) * .5f), BarSize(Size.X, BarH);
        if (Background && Border)
        {
            // Background tiled 8 x 8 inside the insets (left 3, right 3, top 6, bottom 6).
            BackgroundBrush.SetResourceObject(Background);
            BackgroundBrush.ImageSize = FVector2D(8.f, 8.f) * S;
            BackgroundBrush.Tiling = ESlateBrushTileType::Both;
            BackgroundBrush.DrawAs = ESlateBrushDrawType::Image;
            const FVector2D InsetPos = BarPos + FVector2D(3.f, 6.f) * S;
            const FVector2D InsetSize = BarSize - FVector2D(6.f, 12.f) * S;
            if (InsetSize.X > 0 && InsetSize.Y > 0)
                FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(InsetSize), FSlateLayoutTransform(FVector2f(InsetPos))), &BackgroundBrush, Effect);
            // Border: the backdrop edge atlas, 8 pieces L,R,T,B,TL,TR,BL,BR (T and B stored rotated).
            BorderBrush.SetResourceObject(Border);
            BorderBrush.DrawAs = ESlateBrushDrawType::Image;
            const float E = FMath::Min(8.f * S, float(FMath::Min(BarSize.X, BarSize.Y)) * .5f);
            auto Piece = [&](int32 Index, FVector2D Pos, FVector2D Extent, bool bRotate)
            {
                if (Extent.X <= 0 || Extent.Y <= 0) return;
                FVector2f Min(Index / 8.f, 0.f), Max((Index + 1) / 8.f, 1.f);
                const FVector2f HalfTexel(.5f / Border->GetSizeX(), .5f / Border->GetSizeY());
                Min += HalfTexel; Max -= HalfTexel;
                const FVector2D P[4] = {Pos, Pos + FVector2D(Extent.X, 0), Pos + Extent, Pos + FVector2D(0, Extent.Y)};
                const FVector2f UV[4] = { bRotate ? FVector2f(Min.X, Max.Y) : Min, bRotate ? Min : FVector2f(Max.X, Min.Y),
                    bRotate ? FVector2f(Max.X, Min.Y) : Max, bRotate ? Max : FVector2f(Min.X, Max.Y) };
                TArray<FSlateVertex> Vertices;
                for (int32 V = 0; V < 4; ++V)
                    Vertices.Add(FSlateVertex::Make(G.GetAccumulatedRenderTransform(), FVector2f(P[V]), UV[V], Tint));
                const TArray<SlateIndex> Indices = {0, 1, 2, 0, 2, 3};
                FSlateDrawElement::MakeCustomVerts(Out, Layer + 1, BorderBrush.GetRenderingResource(), Vertices, Indices, nullptr, 0, 0, Effect);
            };
            const FVector2D O = BarPos, Z = BarSize;
            Piece(0, O + FVector2D(0, E), FVector2D(E, Z.Y - 2 * E), false);
            Piece(1, O + FVector2D(Z.X - E, E), FVector2D(E, Z.Y - 2 * E), false);
            Piece(2, O + FVector2D(E, 0), FVector2D(Z.X - 2 * E, E), true);
            Piece(3, O + FVector2D(E, Z.Y - E), FVector2D(Z.X - 2 * E, E), true);
            Piece(4, O, FVector2D(E, E), false);
            Piece(5, O + FVector2D(Z.X - E, 0), FVector2D(E, E), false);
            Piece(6, O + FVector2D(0, Z.Y - E), FVector2D(E, E), false);
            Piece(7, O + FVector2D(Z.X - E, Z.Y - E), FVector2D(E, E), false);
        }
        else
        {
            static const FSlateRoundedBoxBrush Bar(FLinearColor(0.f, 0.f, 0.f, .7f), 3.f, FLinearColor(.55f, .55f, .58f, 1.f), 1.f);
            FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(float(BarSize.X), 6.f * S),
                FSlateLayoutTransform(FVector2f(0.f, float(Size.Y * .5f - 3.f * S)))), &Bar, Effect);
        }
        // Thumb: 32 x 32 art, its visible knob about 16 wide; its centre travels between the bar's ends.
        const float ThumbSize = 32.f * S;
        const float Travel = FMath::Max(0.f, float(Size.X) - 16.f * S);
        const float CentreX = 8.f * S + Travel * FMath::Clamp(Value.Get(), 0.f, 1.f);
        const FVector2D ThumbPos(CentreX - ThumbSize * .5f, Size.Y * .5f - ThumbSize * .5f);
        if (Thumb)
        {
            ThumbBrush.SetResourceObject(Thumb);
            ThumbBrush.ImageSize = FVector2D(ThumbSize);
            ThumbBrush.DrawAs = ESlateBrushDrawType::Image;
            FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(ThumbSize, ThumbSize), FSlateLayoutTransform(FVector2f(ThumbPos))), &ThumbBrush, Effect);
        }
        else
        {
            static const FSlateRoundedBoxBrush Knob(FLinearColor(.75f, .75f, .78f, 1.f), 3.f, FLinearColor::Black, 1.f);
            FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(10.f * S, 18.f * S),
                FSlateLayoutTransform(FVector2f(CentreX - 5.f * S, float(Size.Y * .5f - 9.f * S)))), &Knob, Effect);
        }
        return Layer + 2;
    }

    void SetFromPointer(const FGeometry& G, const FVector2D& Screen)
    {
        const float S = FMath::Min(ArtScale, float(G.GetLocalSize().Y) / 17.f);
        const float Travel = FMath::Max(1.f, float(G.GetLocalSize().X) - 16.f * S);
        const float Local = float(G.AbsoluteToLocal(Screen).X) - 8.f * S;
        OnChanged.ExecuteIfBound(FMath::Clamp(Local / Travel, 0.f, 1.f));
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled()) return FReply::Unhandled();
        bDragging = true;
        SetFromPointer(G, Event.GetScreenSpacePosition());
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& Event) override
    {
        if (!bDragging || !HasMouseCapture()) return FReply::Unhandled();
        SetFromPointer(G, Event.GetScreenSpacePosition());
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
    {
        bDragging = false;
        return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { bDragging = false; }
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override
    {
        // Stefan 11.10: scrolling the options page over a slider changed the setting. Like the
        // stock OptionsSliderTemplate, the wheel scrolls the page; Ctrl+wheel still steps a slider.
        if (!IsEnabled() || !Event.IsControlDown()) return FReply::Unhandled();
        WowSliderStep(Value, OnChanged, Step, Event.GetWheelDelta() > 0.f ? 1.f : -1.f);
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }
    virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
    {
        return FCursorReply::Cursor(EMouseCursor::Default);
    }

private:
    TAttribute<float> Value;
    float Step = .02f;
    FOnFloatValueChanged OnChanged;
    bool bDragging = false;
    float PaintedValue = 0.f;
    mutable FSlateBrush BackgroundBrush, BorderBrush, ThumbBrush;
};

class SWowStepSlider : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowStepSlider) : _Value(0.f), _StepSize(.02f) {}
        SLATE_ATTRIBUTE(float, Value)
        SLATE_ARGUMENT(float, StepSize)
        SLATE_EVENT(FOnFloatValueChanged, OnValueChanged)
        /** OptionsSliderTemplate's Low / High captions under the ends ("50%", "200%"). */
        SLATE_ARGUMENT(FString, LowText)
        SLATE_ARGUMENT(FString, HighText)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Value = Args._Value;
        Step = Args._StepSize;
        OnChanged = Args._OnValueChanged;
        auto Arrow = [this](const TCHAR* Label, float Direction) -> TSharedRef<SWidget>
        {
            return SNew(SButton).Tag(FName(TEXT("WowRepeat"))).ButtonStyle(FWowWindowStyle::Button()).ContentPadding(FMargin(9.f, 1.f)).IsFocusable(false)
                .Visibility_Lambda([]() { return FWowVRSupport::IsActive() ? EVisibility::Visible : EVisibility::Collapsed; })
                .OnPressed_Lambda([this, Direction]() { StartHold(Direction); })
                .OnReleased_Lambda([this]() { HoldDirection = 0.f; })
                [ SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14)).ColorAndOpacity(LoginOptionsGold) ];
        };
        // GameFontHighlightSmall: white, small.
        auto Caption = [](const FString& Text, ETextJustify::Type Justify) -> TSharedRef<SWidget>
        {
            return SNew(STextBlock).Text(FText::FromString(Text)).Justification(Justify)
                .Font(FWowWindowStyle::TitleFont(10)).ColorAndOpacity(FLinearColor::White)
                .ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(FLinearColor::Black);
        };
        const bool bCaptions = !Args._LowText.IsEmpty() || !Args._HighText.IsEmpty();
        ChildSlot
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)[ Arrow(TEXT("<"), -1.f) ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [ SNew(SWowSliderBar).Value(Value).StepSize(Step).OnValueChanged(OnChanged) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)[ Arrow(TEXT(">"), 1.f) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(2.f, 0.f, 2.f, 0.f)
            [
                SNew(SHorizontalBox).Visibility(bCaptions ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed)
                + SHorizontalBox::Slot().FillWidth(1.f)[ Caption(Args._LowText, ETextJustify::Left) ]
                + SHorizontalBox::Slot().FillWidth(1.f)[ Caption(Args._HighText, ETextJustify::Right) ]
            ]
        ];
    }

private:
    void Nudge(float Direction)
    {
        WowSliderStep(Value, OnChanged, Step, Direction);
    }
    void StartHold(float Direction)
    {
        Nudge(Direction);
        HoldDirection = Direction;
        HoldStart = FSlateApplication::Get().GetCurrentTime();
        if (!RepeatTimer.IsValid())
            RepeatTimer = RegisterActiveTimer(.06f, FWidgetActiveTimerDelegate::CreateSP(this, &SWowStepSlider::Repeat));
    }
    EActiveTimerReturnType Repeat(double Now, float)
    {
        // Stop when released (also if the release happened off the button).
        if (HoldDirection == 0.f || !FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
        {
            HoldDirection = 0.f;
            RepeatTimer.Reset();
            return EActiveTimerReturnType::Stop;
        }
        if (Now - HoldStart > .4) Nudge(HoldDirection);
        return EActiveTimerReturnType::Continue;
    }

    TAttribute<float> Value;
    float Step = .02f;
    FOnFloatValueChanged OnChanged;
    float HoldDirection = 0.f;
    double HoldStart = 0.0;
    TSharedPtr<FActiveTimerHandle> RepeatTimer;
};

/** OptionsCheckButtonTemplate (3.3.5): UI-CheckBox-Up / -Down, the -Highlight added on hover and the
 *  -Check (or -Check-Disabled) drawn over the box when ticked. SCheckBox draws one image per state,
 *  so the box comes from the style and the tick and highlight are painted on top here. */
class SWowCheckButton : public SCheckBox
{
public:
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Clip,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
    {
        int32 Top = SCheckBox::OnPaint(Args, G, Clip, Out, Layer, InWidgetStyle, bParentEnabled);
        const bool bEnabled = bParentEnabled && IsEnabled();
        const float BoxSize = WowCheckBoxSize;
        const FVector2f BoxPos(0.f, float(G.GetLocalSize().Y - BoxSize) * .5f);
        const FPaintGeometry BoxGeometry = G.ToPaintGeometry(FVector2f(BoxSize, BoxSize), FSlateLayoutTransform(BoxPos));
        if (bEnabled && IsHovered())
            if (UTexture2D* Highlight = FWowWindowStyle::ControlTexture(TEXT("Interface\\Buttons\\UI-CheckBox-Highlight")))
            {
                HighlightBrush.SetResourceObject(Highlight);
                HighlightBrush.DrawAs = ESlateBrushDrawType::Image;
                // alphaMode="ADD": premultiplied blend with zero alpha adds the colour.
                FSlateDrawElement::MakeBox(Out, ++Top, BoxGeometry, &HighlightBrush,
                    ESlateDrawEffect::PreMultipliedAlpha | ESlateDrawEffect::IgnoreTextureAlpha, FLinearColor(1.f, 1.f, 1.f, 0.f));
            }
        if (IsChecked())
            if (UTexture2D* Check = FWowWindowStyle::ControlTexture(bEnabled ? TEXT("Interface\\Buttons\\UI-CheckBox-Check") : TEXT("Interface\\Buttons\\UI-CheckBox-Check-Disabled")))
            {
                CheckBrush.SetResourceObject(Check);
                CheckBrush.DrawAs = ESlateBrushDrawType::Image;
                FSlateDrawElement::MakeBox(Out, ++Top, BoxGeometry, &CheckBrush);
            }
        return Top;
    }

    static constexpr float WowCheckBoxSize = 28.f; // 26-unit template, this window's scale

private:
    mutable FSlateBrush HighlightBrush, CheckBrush;
};

void SWowLoginOptions::Construct(const FArguments& InArgs)
{
	AudioManager = InArgs._AudioManager;
	OnClose = InArgs._OnClose;
    OnArtworkChanged = InArgs._OnArtworkChanged;
    GWowOptionsGlueLook = OnArtworkChanged.IsBound(); // r388: the login screen's Options
    ArtworkOptions = InArgs._ArtworkOptions;
    OnInterfaceOptionChanged=InArgs._OnInterfaceOptionChanged;
    for(const TCHAR* Label : {TEXT("Automatic (current weather source)"),TEXT("Clear"),TEXT("Overcast"),TEXT("Rain"),TEXT("Thunderstorm"),TEXT("Snow"),TEXT("Blizzard"),TEXT("Sandstorm"),TEXT("Ash"),TEXT("Fog"),TEXT("Mixed (checkboxes below)")})
        WeatherPreviewOptions.Add(MakeShared<FString>(Label));
	PopulateDisplayOptions();

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			// The screen dimmer behind the options: not in VR, where it darkened every panel.
			.BorderBackgroundColor_Lambda([]() { return FLinearColor(0.0f, 0.0f, 0.0f, FWowVRSupport::IsActive() ? 0.0f : 0.62f); })
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            [SNew(SBox)
			.WidthOverride(780.0f)
			.HeightOverride(680.0f)
			[
				SNew(SWowWindowFrame)
				.Padding(8.0f)
				[
					SNew(SBorder)
					.BorderImage(FWowWindowStyle::Background())
					.BorderBackgroundColor(FLinearColor::White)
					.Padding(18.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 12.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Options")))
							.Font(FWowNativeWidgetStyle::Heading(22))
							.ColorAndOpacity(LoginOptionsGold)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 14.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
							[
								MakeOptionButton(TEXT("Video"), FOnClicked::CreateSP(this, &SWowLoginOptions::ShowVideoPage))
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
							[
								MakeOptionButton(TEXT("Sound"), FOnClicked::CreateSP(this, &SWowLoginOptions::ShowSoundPage))
							]
                            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f,0.0f)
                            [SNew(SBox).Visibility(OnArtworkChanged.IsBound()?EVisibility::Collapsed:EVisibility::Visible)
                                [MakeOptionButton(TEXT("Interface"),FOnClicked::CreateLambda([this](){SetActivePage(2);return FReply::Handled();}))]]
                            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
                            [MakeOptionButton(TEXT("Experimental"),FOnClicked::CreateLambda([this](){SetActivePage(4);return FReply::Handled();}))]
                            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
                            [SNew(SBox).Visibility_Lambda([](){return WowTestControls::Enabled()?EVisibility::Visible:EVisibility::Collapsed;})
                                [MakeOptionButton(TEXT("Test"),FOnClicked::CreateLambda([this](){SetActivePage(3);return FReply::Handled();}))]]
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SAssignNew(SectionSwitcher, SWidgetSwitcher)
							.WidgetIndex(0)
							+ SWidgetSwitcher::Slot()[BuildVideoPage()]
							+ SWidgetSwitcher::Slot()[BuildSoundPage()]
                            + SWidgetSwitcher::Slot()[OnArtworkChanged.IsBound() ? SNullWidget::NullWidget : BuildInterfacePage()]
                            + SWidgetSwitcher::Slot()[WowTestControls::Enabled()?BuildTestPage():SNullWidget::NullWidget]
                            + SWidgetSwitcher::Slot()[BuildExperimentalPage()]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 14.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                            [MakeOptionButton(TEXT("Apply"), FOnClicked::CreateSP(this, &SWowLoginOptions::ApplyVideoSettings))]
                            + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                            [MakeOptionButton(TEXT("Close"), FOnClicked::CreateSP(this, &SWowLoginOptions::CloseOptions))]
						]
					]
				]
			]
		]]
	];
}

void SWowLoginOptions::SetActivePage(int32 PageIndex)
{
	if (SectionSwitcher.IsValid())
	{
		SectionSwitcher->SetActiveWidgetIndex(OnArtworkChanged.IsBound() && PageIndex == 2 ? 0 : ((PageIndex == 3 && !WowTestControls::Enabled()) ? 0 : FMath::Clamp(PageIndex, 0, 4)));
	}
}

void SWowLoginOptions::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	SyncActiveDisplayState();
    if (!WowTestControls::Enabled() && SectionSwitcher.IsValid() && SectionSwitcher->GetActiveWidgetIndex()==3) SetActivePage(0);
}

void SWowLoginOptions::SyncActiveDisplayState()
{
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
	{
		return;
	}

	const FIntPoint ActiveResolution = GEngine->GameViewport->Viewport->GetSizeXY();
	const int32 ActiveWindowMode = static_cast<int32>(
		GEngine->GameViewport->Viewport->GetWindowMode());
	if (ActiveResolution.X <= 0 || ActiveResolution.Y <= 0)
	{
		return;
	}

	// Alt+Enter changes the live viewport immediately. Keep the options page in
	// sync with that live state instead of continuing to display the resolution
	// that happened to be loaded from GameUserSettings when the widget opened.
	if (ActiveResolution != LastObservedResolution || ActiveWindowMode != LastObservedWindowMode)
	{
		// A resize/Alt+Enter may refresh the observed state, but must not
		// discard a user's selection before Apply.
		if (!bPendingDisplayChange)
		{
			PendingResolution = ActiveResolution;
			PendingWindowMode = ActiveWindowMode;
		}
		LastObservedResolution = ActiveResolution;
		LastObservedWindowMode = ActiveWindowMode;
	}
}

void SWowLoginOptions::PopulateDisplayOptions()
{
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings)
	{
		Settings->LoadSettings(false);
		PendingResolution = Settings->GetScreenResolution();
		PendingWindowMode = static_cast<int32>(Settings->GetFullscreenMode());
		PendingQuality = Settings->GetOverallScalabilityLevel();
		PendingQuality = PendingQuality < 0 ? 2 : FMath::Clamp(PendingQuality, 0, 4);
		float NormalizedScale = 1.0f;
		float MinimumScale = 50.0f;
		float MaximumScale = 100.0f;
		Settings->GetResolutionScaleInformationEx(
			NormalizedScale, PendingResolutionScale, MinimumScale, MaximumScale);
		bPendingVSync = Settings->IsVSyncEnabled();
	}
	SyncActiveDisplayState();
    if (GConfig) GConfig->GetBool(TEXT("WowUnreal.ClientPreferences"), TEXT("LoginUseMods"), bPendingLoginMods, GGameUserSettingsIni);
	PendingRefreshRate = FMath::Max(30, FWowClientPreferences::GetPreferredRefreshRate());
	bPendingDepthOfField = FWowClientPreferences::IsDepthOfFieldEnabled();
    PendingDepthOfFieldStrength = FWowClientPreferences::GetDepthOfFieldStrength();
	bPendingVolumetricFog = FWowClientPreferences::IsVolumetricFogEnabled();
	bPendingLumen = FWowClientPreferences::IsLumenEnabled();
	bPendingHardwareRayTracing = FWowClientPreferences::IsHardwareRayTracingEnabled();
	bPendingMotionBlur = FWowClientPreferences::IsMotionBlurEnabled();
    bPendingDarkerLighting = FWowClientPreferences::IsDarkerRealisticLightingEnabled();
    bPendingEntityCollision = FWowClientPreferences::IsNpcAndPlayerCollisionEnabled();
    bPendingFirstPerson = FWowClientPreferences::IsFirstPersonImmersionEnabled();
    bPendingHelmetView = FWowClientPreferences::IsFirstPersonHelmetViewEnabled();

	bPendingFrameGeneration = FWowClientPreferences::IsFrameGenerationEnabled();
	bPendingPerformanceTimings = FWowClientPreferences::ArePerformanceTimingsEnabled();
	bPendingPerformanceRenderingStatus = FWowClientPreferences::IsPerformanceRenderingStatusEnabled();
	PendingAntiAliasingMethod = FWowClientPreferences::GetAntiAliasingMethod();
	PendingFsrQualityMode = FWowClientPreferences::GetFsrQualityMode();
    PendingDlssQualityMode=FWowClientPreferences::GetDlssQualityMode();
    PendingNvidiaFGMode=FWowClientPreferences::GetNvidiaFrameGenerationMode();
    bDlssAvailable=FWowClientPreferences::IsDlssAvailable();
    bNvidiaFGAvailable=FWowClientPreferences::IsNvidiaFrameGenerationAvailable();
    NvidiaFGModes=FWowClientPreferences::GetNvidiaFrameGenerationModes();
    for (int32 Mode : NvidiaFGModes) NvidiaFGOptions.Add(MakeShared<FString>(FWowClientPreferences::GetNvidiaFrameGenerationModeLabel(Mode)));
    if (!NvidiaFGModes.Contains(PendingNvidiaFGMode) && !NvidiaFGModes.IsEmpty()) PendingNvidiaFGMode=NvidiaFGModes[0];
	bFsrAvailable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FSR.Enabled")) != nullptr;
	bFrameGenerationAvailable =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled")) != nullptr ||
		bNvidiaFGAvailable;

	TSet<FString> UniqueResolutions;
	TSet<int32> UniqueRates;
	FScreenResolutionArray NativeModes;
	if (RHIGetAvailableResolutions(NativeModes, false))
	{
		NativeModes.Sort([](const FScreenResolutionRHI& A, const FScreenResolutionRHI& B)
		{
			if (A.Width != B.Width) return A.Width < B.Width;
			if (A.Height != B.Height) return A.Height < B.Height;
			return A.RefreshRate < B.RefreshRate;
		});
		for (const FScreenResolutionRHI& Mode : NativeModes)
		{
			if (Mode.Width < 1024 || Mode.Height < 720) continue;
			UniqueResolutions.Add(FString::Printf(TEXT("%u x %u"), Mode.Width, Mode.Height));
			if (Mode.RefreshRate >= 30) UniqueRates.Add(static_cast<int32>(Mode.RefreshRate));
		}
	}
	UniqueResolutions.Add(FString::Printf(TEXT("%d x %d"), PendingResolution.X, PendingResolution.Y));
	if (UniqueRates.IsEmpty())
	{
		for (const int32 Rate : { 60, 75, 120, 144, 165, 240 }) UniqueRates.Add(Rate);
	}
	UniqueRates.Add(PendingRefreshRate);

	TArray<FString> SortedResolutions = UniqueResolutions.Array();
	SortedResolutions.Sort([](const FString& A, const FString& B)
	{
		auto Area = [](const FString& Value)
		{
			FString Left, Right;
			Value.Split(TEXT(" x "), &Left, &Right);
			return FCString::Atoi(*Left) * FCString::Atoi(*Right);
		};
		return Area(A) < Area(B);
	});
	for (const FString& Value : SortedResolutions) ResolutionOptions.Add(MakeShared<FString>(Value));

	TArray<int32> SortedRates = UniqueRates.Array();
	SortedRates.Sort();
	for (const int32 Rate : SortedRates)
	{
		RefreshRateOptions.Add(MakeShared<FString>(FString::Printf(TEXT("%d Hz"), Rate)));
	}

	WindowModeOptions = {
		MakeShared<FString>(TEXT("Fullscreen")),
		MakeShared<FString>(TEXT("Borderless Window")),
		MakeShared<FString>(TEXT("Windowed"))
	};
	QualityOptions = {
		MakeShared<FString>(TEXT("Low")),
		MakeShared<FString>(TEXT("Medium")),
		MakeShared<FString>(TEXT("High")),
		MakeShared<FString>(TEXT("Epic")),
		MakeShared<FString>(TEXT("Cinematic"))
	};
	AntiAliasingOptions = {
		MakeShared<FString>(TEXT("Off")),
		MakeShared<FString>(TEXT("FXAA")),
		MakeShared<FString>(TEXT("TAA")),
		MakeShared<FString>(TEXT("TSR")),
		MakeShared<FString>(bFsrAvailable
			? TEXT("AMD FSR 4.1 (Auto)")
			: TEXT("AMD FSR Upscaling (plugin not installed)")),
        MakeShared<FString>(bDlssAvailable ? TEXT("NVIDIA DLSS 4.5") : TEXT("NVIDIA DLSS (unavailable)")),
        MakeShared<FString>(bDlssAvailable ? TEXT("NVIDIA DLAA") : TEXT("NVIDIA DLAA (unavailable)"))
	};
    DlssQualityOptions={MakeShared<FString>(TEXT("Quality")),MakeShared<FString>(TEXT("Balanced")),
        MakeShared<FString>(TEXT("Performance")),MakeShared<FString>(TEXT("Ultra Performance"))};
	FsrQualityOptions = {
		MakeShared<FString>(TEXT("Native AA")),
		MakeShared<FString>(TEXT("Quality")),
		MakeShared<FString>(TEXT("Balanced")),
		MakeShared<FString>(TEXT("Performance")),
		MakeShared<FString>(TEXT("Ultra Performance"))
	};
}

TSharedRef<SWidget> SWowLoginOptions::MakeComboRow(
	const FString& Label,
	const TArray<TSharedPtr<FString>>* Options,
	TFunction<void(TSharedPtr<FString>)> OnSelected,
	TAttribute<FText> CurrentText)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(LoginOptionsText)
		]
		+ SHorizontalBox::Slot().FillWidth(0.55f)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.ComboBoxStyle(WowComboStyle())
			.ItemStyle(WowComboRowStyle())
			.OptionsSource(Options)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
			{
				return SNew(STextBlock)
					.Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty())
					.ColorAndOpacity(LoginOptionsText);
			})
			.OnSelectionChanged_Lambda([OnSelected](TSharedPtr<FString> Item, ESelectInfo::Type)
			{
				if (Item.IsValid()) OnSelected(Item);
			})
			[
				SNew(STextBlock).Text(CurrentText).ColorAndOpacity(LoginOptionsGold)
			]
		];
}

TSharedRef<SWidget> SWowLoginOptions::BuildVideoPage()
{
	return WowScroll(SNew(SScrollBox).ScrollBarStyle(WowScrollStyle()).ScrollBarThickness(FVector2D(12.f, 12.f)).ConsumeMouseWheel(EConsumeMouseWheel::Always)
		+ SScrollBox::Slot().Padding(6.0f)
		[
			SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [ArtworkOptions.IsValid() ? ArtworkOptions.ToSharedRef() : SNullWidget::NullWidget]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .Visibility(OnArtworkChanged.IsBound() ? EVisibility::Visible : EVisibility::Collapsed)
                .IsChecked_Lambda([this] { return bPendingLoginMods ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingLoginMods = State == ECheckBoxState::Checked; })
                .ToolTip(WowTip(TEXT("Use installed HD/custom login artwork when available. Otherwise use original artwork.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("HD / custom login background"))).ColorAndOpacity(LoginOptionsText)]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
			[
				MakeComboRow(TEXT("Display Mode"), &WindowModeOptions,
					[this](TSharedPtr<FString> Item) { PendingWindowMode = WindowModeOptions.IndexOfByKey(Item); bPendingDisplayChange = true; },
					TAttribute<FText>::CreateLambda([this]()
					{
						return WindowModeOptions.IsValidIndex(PendingWindowMode)
							? FText::FromString(*WindowModeOptions[PendingWindowMode]) : FText::GetEmpty();
					}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
			[
				MakeComboRow(TEXT("Resolution"), &ResolutionOptions,
					[this](TSharedPtr<FString> Item)
					{
						FString Width, Height;
						if (Item->Split(TEXT(" x "), &Width, &Height))
						{
							PendingResolution = FIntPoint(FCString::Atoi(*Width), FCString::Atoi(*Height));
							// Borderless always occupies the desktop. Selecting a smaller
							// window size must also select a mode that can honor it.
							if (PendingWindowMode == EWindowMode::WindowedFullscreen)
								PendingWindowMode = EWindowMode::Windowed;
							bPendingDisplayChange = true;
						}
					},
					TAttribute<FText>::CreateLambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("%d x %d"), PendingResolution.X, PendingResolution.Y));
					}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
			[
				MakeComboRow(TEXT("Refresh Rate"), &RefreshRateOptions,
					[this](TSharedPtr<FString> Item) { PendingRefreshRate = FCString::Atoi(**Item); },
					TAttribute<FText>::CreateLambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("%d Hz"), PendingRefreshRate));
					}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)
			[
				SNew(SWowCheckButton).Style(WowCheckStyle())
				.IsChecked_Lambda([this]() { return bPendingVSync ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingVSync = State == ECheckBoxState::Checked; })
				.ToolTip(WowTip(TEXT("Tear-free. With G-Sync/FreeSync on in the driver the monitor follows the frame rate. Without it VSync is used, and briefly released when frames run slow instead of halving to 30 fps.")))
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Sync: G-Sync / FreeSync, VSync fallback"))).ColorAndOpacity(LoginOptionsText)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
			[
				MakeComboRow(TEXT("Graphics Quality"), &QualityOptions,
					[this](TSharedPtr<FString> Item) { PendingQuality = QualityOptions.IndexOfByKey(Item); },
					TAttribute<FText>::CreateLambda([this]()
					{
						return QualityOptions.IsValidIndex(PendingQuality)
							? FText::FromString(*QualityOptions[PendingQuality]) : FText::GetEmpty();
					}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
			[
				MakeComboRow(TEXT("Anti-Aliasing / Upscaler"), &AntiAliasingOptions,
					[this](TSharedPtr<FString> Item)
					{
						const int32 Selected = AntiAliasingOptions.IndexOfByKey(Item);
						if (Selected>=0 && (Selected!=4 || bFsrAvailable) && (Selected<5 || bDlssAvailable)) PendingAntiAliasingMethod = Selected;
					},
					TAttribute<FText>::CreateLambda([this]()
					{
						return AntiAliasingOptions.IsValidIndex(PendingAntiAliasingMethod)
							? FText::FromString(*AntiAliasingOptions[PendingAntiAliasingMethod]) : FText::GetEmpty();
					}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
			[
                SNew(SBox).Visibility_Lambda([this]() { return PendingAntiAliasingMethod==4 ? EVisibility::Visible : EVisibility::Collapsed; })
                [MakeComboRow(TEXT("AMD FSR Quality"), &FsrQualityOptions,
					[this](TSharedPtr<FString> Item)
					{
						PendingFsrQualityMode = FsrQualityOptions.IndexOfByKey(Item);
					},
					TAttribute<FText>::CreateLambda([this]()
					{
						return FsrQualityOptions.IsValidIndex(PendingFsrQualityMode)
							? FText::FromString(*FsrQualityOptions[PendingFsrQualityMode]) : FText::GetEmpty();
					}))]
			]
            + SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [SNew(SBox).Visibility_Lambda([this]() { return PendingAntiAliasingMethod==5 ? EVisibility::Visible : EVisibility::Collapsed; })
                [MakeComboRow(TEXT("NVIDIA DLSS Quality"), &DlssQualityOptions,
                    [this](TSharedPtr<FString> Item) { PendingDlssQualityMode=DlssQualityOptions.IndexOfByKey(Item)+1; },
                    TAttribute<FText>::CreateLambda([this]() { return FText::FromString(*DlssQualityOptions[FMath::Clamp(PendingDlssQualityMode-1,0,3)]); }))]]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return PendingAntiAliasingMethod>=4 ? FText::FromString(PendingAntiAliasingMethod==6 ? TEXT("Render Scale: 100% (DLAA)") : TEXT("Render Scale: controlled by upscaler quality")) : FText::FromString(FString::Printf(TEXT("Render Scale: %.0f%%"), PendingResolutionScale)); })
				.ColorAndOpacity(LoginOptionsText)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SWowStepSlider).LowText(TEXT("50%")).HighText(TEXT("150%"))
                .IsEnabled_Lambda([this]() { return PendingAntiAliasingMethod<4; })
                .ToolTip(WowTip(TEXT("DLSS, DLAA and FSR control their render resolution through the selected quality mode.")))
				.Value_Lambda([this]() { return (PendingResolutionScale - 50.0f) / 100.0f; })
				.OnValueChanged_Lambda([this](float Value) { PendingResolutionScale = 50.0f + Value * 100.0f; })
			]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text_Lambda([]() { return FText::FromString(FString::Printf(TEXT("%s: %.0f%%"), FWowClientPreferences::IsVrUiMode() ? TEXT("UI Scale (VR headset)") : TEXT("UI Scale"), FWowClientPreferences::GetUIScale() * 100.0f)); })
                .ColorAndOpacity(LoginOptionsText)
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SWowStepSlider).LowText(TEXT("64%")).HighText(TEXT("150%"))
                .Value_Lambda([]() { return (FWowClientPreferences::GetUIScale() - 0.64f) / 0.86f; })
                .OnValueChanged_Lambda([](float Value) { FWowClientPreferences::SetUIScale(FMath::RoundToFloat((0.64f + Value * 0.86f) * 100.0f) / 100.0f); })
                .ToolTip(WowTip(TEXT("Size of the whole game interface. The monitor and the VR headset each have their own value. 64-150 %: 64-100 % like the original client, above 100 % larger than it (the default bars may then not fit on smaller screens). The interface reloads a moment after you let go.")))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,12,0,2)
            [SNew(STextBlock).ColorAndOpacity(LoginOptionsText)
                .Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Gamma: %.2f"), FWowClientPreferences::GetGamma()));})]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SWowStepSlider).Value_Lambda([](){return (FWowClientPreferences::GetGamma()-1.6f)/1.4f;})
                .OnValueChanged_Lambda([](float V){FWowClientPreferences::SetGamma(1.6f+V*1.4f);})
                .ToolTip(WowTip(TEXT("Adjust screen brightness. Higher is brighter. Saved immediately; default 2.20.")))]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0,4)
            [SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Reset Gamma")))
                .OnClicked_Lambda([](){FWowClientPreferences::SetGamma(2.2f);return FReply::Handled();})]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Advanced")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(LoginOptionsGold)
			]

            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsEnabled_Lambda([]() { return true; })
                .IsChecked_Lambda([this]() { return bPendingVolumetricFog && true ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingVolumetricFog = State == ECheckBoxState::Checked; })
                .ToolTip(WowTip(true ? TEXT("Light rays and depth in fog. Press Apply.") : TEXT("Not supported by the current renderer or graphics hardware.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Volumetric Fog"))).ColorAndOpacity(LoginOptionsText)]]

            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsEnabled_Lambda([]() { return FWowClientPreferences::IsLumenAvailable(); })
                .IsChecked_Lambda([this]() { return bPendingLumen && FWowClientPreferences::IsLumenAvailable() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingLumen = State == ECheckBoxState::Checked; })
                .ToolTip(WowTip(FWowClientPreferences::IsLumenAvailable() ? TEXT("Light bounce and reflections. Imported scenery may have incomplete lighting. Press Apply.") : TEXT("Not supported by the current renderer or graphics hardware.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Lumen lighting and reflections (experimental)"))).ColorAndOpacity(LoginOptionsText)]]

            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsEnabled_Lambda([]() { return FWowClientPreferences::IsHardwareRayTracingAvailable(); })
                .IsChecked_Lambda([this]() { return bPendingHardwareRayTracing && FWowClientPreferences::IsHardwareRayTracingAvailable() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingHardwareRayTracing = State == ECheckBoxState::Checked; })
                .ToolTip(WowTip(FWowClientPreferences::IsHardwareRayTracingAvailable() ? TEXT("Ray-traced shadows. Also uses hardware ray tracing for Lumen when both options are enabled. Press Apply.") : TEXT("Not supported by the current renderer or graphics hardware.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Hardware Ray Tracing"))).ColorAndOpacity(LoginOptionsText)]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Blurs fast movement on screen. Press Apply.")))
				.IsChecked_Lambda([this]() { return bPendingMotionBlur ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingMotionBlur = State == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Motion Blur (off by default)"))).ColorAndOpacity(LoginOptionsText)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SWowCheckButton).Style(WowCheckStyle())
				// Off while the headset is on: the VR mode forces frame generation off, and switching it
				// during VR crashed FSR when VR ended (Claude-Perf r196).
				.IsEnabled_Lambda([this]() { return bFrameGenerationAvailable && !FWowVRSupport::IsActive() && !FWowVRSupport::IsFrameGenerationBlockedUntilRestart(); })
				.IsChecked_Lambda([this]() { return bPendingFrameGeneration ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingFrameGeneration = State == ECheckBoxState::Checked; })
				.ToolTip(WowTip(bFrameGenerationAvailable
					? TEXT("Uses NVIDIA DLSS Frame Generation where supported, otherwise AMD Frame Generation with FSR selected. Only one provider runs at a time. Not available while the VR headset is on; after a VR session, restart the game to use frame generation again.")
					: TEXT("No compatible frame-generation plugin is installed.")))
				[
					SNew(STextBlock)
					.Text(FText::FromString(bFrameGenerationAvailable
						? (bNvidiaFGAvailable ? TEXT("NVIDIA DLSS Frame Generation") : TEXT("AMD Frame Generation (requires FSR)")) : TEXT("Frame Generation (unavailable)")))
					.ColorAndOpacity(bFrameGenerationAvailable ? LoginOptionsText : FLinearColor(0.42f, 0.42f, 0.42f, 1.0f))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("AMD Anti-Lag 2: lowers the delay between your mouse/keys and the picture on supported AMD cards.")))
					.IsChecked_Lambda([]()
						{
							return FWowClientPreferences::IsAntiLag2Enabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
						{
							const bool bNewState = (State == ECheckBoxState::Checked);
							FWowClientPreferences::SetAntiLag2Enabled(bNewState);
						})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("AMD Anti-Lag 2"))).ColorAndOpacity(LoginOptionsText)
				]
			]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SWowCheckButton).Style(WowCheckStyle())
				.IsChecked_Lambda([this]() { return bPendingMotionBlur ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingMotionBlur = State == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Motion Blur (off by default)"))).ColorAndOpacity(LoginOptionsText)
				]
			]
            + SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [SNew(SBox).Visibility(bNvidiaFGAvailable ? EVisibility::Visible : EVisibility::Collapsed)
                .IsEnabled_Lambda([this]() { return bPendingFrameGeneration; })
                [MakeComboRow(TEXT("NVIDIA Frame Generation Mode"), &NvidiaFGOptions,
                    [this](TSharedPtr<FString> Item) { const int32 I=NvidiaFGOptions.IndexOfByKey(Item); if (NvidiaFGModes.IsValidIndex(I)) PendingNvidiaFGMode=NvidiaFGModes[I]; },
                    TAttribute<FText>::CreateLambda([this]() { return FText::FromString(FWowClientPreferences::GetNvidiaFrameGenerationModeLabel(PendingNvidiaFGMode)); }))]]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SWowCheckButton).Style(WowCheckStyle())
				.IsChecked_Lambda([this]() { return bPendingPerformanceTimings ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingPerformanceTimings = State == ECheckBoxState::Checked; })
				.ToolTip(WowTip(TEXT("Adds Game, Render, RHI and GPU frame times to the CTRL+R FPS display.")))
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Show CPU/GPU frame timings"))).ColorAndOpacity(LoginOptionsText)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SWowCheckButton).Style(WowCheckStyle())
				.IsChecked_Lambda([this]() { return bPendingPerformanceRenderingStatus ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingPerformanceRenderingStatus = State == ECheckBoxState::Checked; })
				.ToolTip(WowTip(TEXT("Adds upscaler, frame generation, frame limit and VSync state to the CTRL+R FPS display.")))
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Show rendering feature status"))).ColorAndOpacity(LoginOptionsText)
				]
			]
		]);
}

TSharedRef<SWidget> SWowLoginOptions::MakeVolumeSlider(
	const FString& Label, float InitialValue, TFunction<void(float)> OnChanged)
{
	TSharedPtr<STextBlock> ValueText;
	// The knob follows the live value (Stefan 11.10: it was given the starting value only, so the
	// percentage moved while the knob stayed where the window opened).
	TSharedRef<float> Current = MakeShared<float>(InitialValue);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(LoginOptionsText)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SAssignNew(ValueText, STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(InitialValue * 100.0f))))
				.ColorAndOpacity(LoginOptionsGold)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SWowStepSlider).LowText(TEXT("0%")).HighText(TEXT("100%"))
			.ToolTip(WowTip(Label + TEXT(" volume, 0 to 100 %.")))
			.Value_Lambda([Current]() { return *Current; })
			.OnValueChanged_Lambda([OnChanged, ValueText, Current](float Value)
			{
				*Current = Value;
				OnChanged(Value);
				if (ValueText.IsValid())
					ValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f))));
			})
		];
}

TSharedRef<SWidget> SWowLoginOptions::BuildSoundPage()
{
    auto Rows=SNew(SVerticalBox);
    auto Toggle=[&](const TCHAR* Key,const TCHAR* Label)
    {
        const FString Name(Key);
        Rows->AddSlot().AutoHeight().Padding(0,6)
        [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(OptionTip(Name))
          .IsChecked_Lambda([Name](){return FWowClientPreferences::GetWorldOption(Name)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
          .OnCheckStateChanged_Lambda([Name](ECheckBoxState State){FWowClientPreferences::SetWorldOption(Name,State==ECheckBoxState::Checked);})
          [SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(LoginOptionsGold)]];
    };
    Toggle(TEXT("soundenabled"),TEXT("Enable Sound"));
    Rows->AddSlot().AutoHeight()[MakeVolumeSlider(TEXT("Master Volume"),FWowClientPreferences::GetMasterVolume(),[this](float V)
    { FWowClientPreferences::SetMasterVolume(V); if(AudioManager.IsValid()) AudioManager->SetMasterVolume(V); })];
    Toggle(TEXT("soundinbackground"),TEXT("Sound in Background"));
    Toggle(TEXT("soundeffects"),TEXT("Sound Effects"));
    Rows->AddSlot().AutoHeight()[MakeVolumeSlider(TEXT("Effects Volume"),FWowClientPreferences::GetSfxVolume(),[this](float V)
    { FWowClientPreferences::SetSfxVolume(V); if(AudioManager.IsValid()) AudioManager->SetSfxVolume(V); })];
    Toggle(TEXT("musicenabled"),TEXT("Music"));
    Rows->AddSlot().AutoHeight()[MakeVolumeSlider(TEXT("Music Volume"),FWowClientPreferences::GetMusicVolume(),[this](float V)
    { FWowClientPreferences::SetMusicVolume(V); if(AudioManager.IsValid()) AudioManager->SetMusicVolume(V); })];
    Toggle(TEXT("loopmusic"),TEXT("Loop Music"));
    Toggle(TEXT("ambientenabled"),TEXT("Ambient Sounds"));
    Rows->AddSlot().AutoHeight()[MakeVolumeSlider(TEXT("Ambience Volume"),FWowClientPreferences::GetAmbienceVolume(),[this](float V)
    { FWowClientPreferences::SetAmbienceVolume(V); if(AudioManager.IsValid()) AudioManager->SetAmbienceVolume(V); })];
    // r222 (Stefan 11.10): the intro movie (the expansion trailer, or a custom server's own movie at the
    // same path) before the first character select. Race intros for new characters always play.
    static TArray<TSharedPtr<FString>> IntroMovieNames={MakeShared<FString>(TEXT("Every time")),
        MakeShared<FString>(TEXT("First time only")),MakeShared<FString>(TEXT("Never"))};
    Toggle(TEXT("moviesubtitles"),TEXT("Cinematic Subtitles")); // r223
    Rows->AddSlot().AutoHeight().Padding(0,10,0,2)
    [MakeComboRow(TEXT("Intro Movie"),&IntroMovieNames,
        [](TSharedPtr<FString> Item){const int32 Mode=IntroMovieNames.IndexOfByPredicate([&Item](const TSharedPtr<FString>& N){return *N==*Item;});
          if(Mode!=INDEX_NONE) FWowClientPreferences::SetIntroMovieMode(Mode);},
        TAttribute<FText>::CreateLambda([](){return FText::FromString(*IntroMovieNames[FWowClientPreferences::GetIntroMovieMode()]);}))];
    Rows->AddSlot().AutoHeight().Padding(0,2)
    [SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)
      .Text(FText::FromString(TEXT("The expansion intro movie (or a server's own movie) plays before character select. A new character's race introduction always plays. Esc, Space, Enter or a click skips a movie.")))];
    Rows->AddSlot().AutoHeight().Padding(0,14)
    [SNew(STextBlock).Text(FText::FromString(TEXT("Changes are saved immediately. Loop Music keeps music continuous; turn it off for pauses between tracks."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    return WowScroll(SNew(SScrollBox).ScrollBarStyle(WowScrollStyle()).ScrollBarThickness(FVector2D(12.f, 12.f)).ConsumeMouseWheel(EConsumeMouseWheel::Always)+SScrollBox::Slot()[Rows]);
}

FReply SWowLoginOptions::ShowVideoPage()
{
	if (SectionSwitcher.IsValid()) SectionSwitcher->SetActiveWidgetIndex(0);
	return FReply::Handled();
}

FReply SWowLoginOptions::ShowSoundPage()
{
	if (SectionSwitcher.IsValid()) SectionSwitcher->SetActiveWidgetIndex(1);
	return FReply::Handled();
}

FReply SWowLoginOptions::ApplyVideoSettings()
{
    FWowClientPreferences::SuspendFrameGeneration();
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		if (PendingWindowMode == EWindowMode::WindowedFullscreen)
			PendingResolution = Settings->GetDesktopResolution();
		Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(FMath::Clamp(PendingWindowMode, 0, 2)));
		Settings->SetScreenResolution(PendingResolution);
		Settings->SetVSyncEnabled(bPendingVSync);
		Settings->SetOverallScalabilityLevel(FMath::Clamp(PendingQuality, 0, 4));
		Settings->SetResolutionScaleValueEx(FMath::Clamp(PendingResolutionScale, 50.0f, 150.0f));
		Settings->SetFrameRateLimit(static_cast<float>(PendingRefreshRate));
		Settings->ApplySettings(false);
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
		bPendingDisplayChange = false;
		// The resize request is asynchronous. Do not replace the just-saved
		// selection with the previous frame's viewport dimensions here.
	}
    if (OnArtworkChanged.IsBound() && GConfig)
    {
        bool bPrevious = false;
        GConfig->GetBool(TEXT("WowUnreal.ClientPreferences"), TEXT("LoginUseMods"), bPrevious, GGameUserSettingsIni);
        if (bPrevious != bPendingLoginMods)
        {
            GConfig->SetBool(TEXT("WowUnreal.ClientPreferences"), TEXT("LoginUseMods"), bPendingLoginMods, GGameUserSettingsIni);
            GConfig->Flush(false, GGameUserSettingsIni);
            OnArtworkChanged.Execute();
        }
    }
	FWowClientPreferences::SetPreferredRefreshRate(PendingRefreshRate);
	FWowClientPreferences::SetDepthOfFieldEnabled(bPendingDepthOfField);
    FWowClientPreferences::SetDepthOfFieldStrength(PendingDepthOfFieldStrength);
	FWowClientPreferences::SetVolumetricFogEnabled(bPendingVolumetricFog);
	FWowClientPreferences::SetLumenEnabled(bPendingLumen);
	FWowClientPreferences::SetHardwareRayTracingEnabled(bPendingHardwareRayTracing);
	FWowClientPreferences::SetMotionBlurEnabled(bPendingMotionBlur);
    FWowClientPreferences::SetDarkerRealisticLightingEnabled(bPendingDarkerLighting);
    FWowClientPreferences::SetNpcAndPlayerCollisionEnabled(bPendingEntityCollision);
    FWowClientPreferences::SetFirstPersonImmersionEnabled(bPendingFirstPerson);
    FWowClientPreferences::SetFirstPersonHelmetViewEnabled(bPendingHelmetView);

	FWowClientPreferences::SetAntiAliasingMethod(PendingAntiAliasingMethod);
	FWowClientPreferences::SetFsrQualityMode(PendingFsrQualityMode);
    FWowClientPreferences::SetDlssQualityMode(PendingDlssQualityMode);
    FWowClientPreferences::SetNvidiaFrameGenerationMode(PendingNvidiaFGMode);
	FWowClientPreferences::SetFrameGenerationEnabled(
		bFrameGenerationAvailable && bPendingFrameGeneration);
	FWowClientPreferences::SetPerformanceTimingsEnabled(bPendingPerformanceTimings);
	FWowClientPreferences::SetPerformanceRenderingStatusEnabled(bPendingPerformanceRenderingStatus);
	FWowClientPreferences::ApplyAdvancedVideoPreferences();
	if (IConsoleVariable* TargetRefresh = IConsoleManager::Get().FindConsoleVariable(TEXT("RHI.TargetRefreshRate")))
	{
		TargetRefresh->Set(PendingRefreshRate, ECVF_SetByGameSetting);
	}
	return FReply::Handled();
}

FReply SWowLoginOptions::CloseOptions()
{
	OnClose.ExecuteIfBound();
	return FReply::Handled();
}

TSharedRef<SWidget> SWowLoginOptions::BuildInterfacePage()
{
    // Wrath-style Interface Options (Stefan 10.10: "a more WOTLK options menu"): a category list on
    // the left (gold names; the selected one white on a soft gold bar like a selected quest log row - Stefan 11.10: no blue), the page on the right with
    // its title and a one-line description under it, both in a dark inset panel.
    auto Pages=SNew(SWidgetSwitcher);
    auto Categories=SNew(SVerticalBox);
    auto PageSubtext=[](const FString& Label)->FString
    {
        if(Label==TEXT("Quests")) return TEXT("Quest text speed and the objective tracker.");
        if(Label==TEXT("Names")) return TEXT("Which names are shown over characters in the world.");
        if(Label==TEXT("Combat")) return TEXT("Targeting and combat behaviour.");
        if(Label==TEXT("Display")) return TEXT("How your character looks to everyone. Saved on the server for this character.");
        if(Label==TEXT("Action Bars")) return TEXT("Extra action bars and how the bars behave.");
        if(Label==TEXT("Unit Frames")) return TEXT("What the unit frames show, and the size of the interface and its parts.");
        if(Label==TEXT("Tooltips")) return TEXT("Where tooltips appear and how big they are.");
        if(Label==TEXT("Camera")) return TEXT("Camera speed and the effects drawn on the camera.");
        if(Label==TEXT("Mouse")) return TEXT("The mouse pointer.");
        return FString();
    };
    auto AddPage=[&](const TCHAR* Label)
    {
        const int32 Index=Pages->GetNumWidgets();
        Categories->AddSlot().AutoHeight()
        [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).Cursor(EMouseCursor::Hand)
          .OnClicked_Lambda([Pages,Index](){Pages->SetActiveWidgetIndex(Index);return FReply::Handled();})
          [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).Padding(FMargin(10,4))
            .BorderBackgroundColor_Lambda([Pages,Index](){return Pages->GetActiveWidgetIndex()==Index?FLinearColor(0.42f,0.30f,0.02f,0.45f):FLinearColor::Transparent;})
            [SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold",13))
              .ColorAndOpacity_Lambda([Pages,Index](){return FSlateColor(Pages->GetActiveWidgetIndex()==Index?FLinearColor::White:LoginOptionsGold);})]]];
        auto Rows=SNew(SVerticalBox);
        Pages->AddSlot()[WowScroll(SNew(SScrollBox).ScrollBarStyle(WowScrollStyle()).ScrollBarThickness(FVector2D(12.f, 12.f)).ConsumeMouseWheel(EConsumeMouseWheel::Always)+SScrollBox::Slot()[Rows])];
        Rows->AddSlot().AutoHeight().Padding(0,2,0,2)
        [SNew(STextBlock).Text(FText::FromString(Label)).Font(FWowNativeWidgetStyle::Heading(20)).ColorAndOpacity(LoginOptionsGold)];
        Rows->AddSlot().AutoHeight().Padding(0,0,0,12)
        [SNew(STextBlock).Text(FText::FromString(PageSubtext(Label))).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(LoginOptionsText)];
        return Rows;
    };
    auto AddOption=[this](TSharedRef<SVerticalBox> Rows,const TCHAR* Key,const TCHAR* Label)
    {
        const FString Name(Key);
        Rows->AddSlot().AutoHeight().Padding(2,7)
        [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(OptionTip(Name))
          .IsChecked_Lambda([Name](){return FWowClientPreferences::GetWorldOption(Name)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
          .OnCheckStateChanged_Lambda([this,Name](ECheckBoxState State)
          {const bool Enabled=State==ECheckBoxState::Checked; FWowClientPreferences::SetWorldOption(Name,Enabled); OnInterfaceOptionChanged.ExecuteIfBound(Name,Enabled);})
          [SNew(STextBlock).Text(FText::FromString(Label)).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(LoginOptionsText)]];
    };
    auto Quests=AddPage(TEXT("Quests"));
    AddOption(Quests,TEXT("instantquesttext"),TEXT("Fast Quest Text"));
    Quests->AddSlot().AutoHeight().Padding(2,8)
    [SNew(STextBlock).Text(FText::FromString(TEXT("Show quest and gossip text instantly. Turn off to reveal it gradually, word by word.")))
      .AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    AddOption(Quests,TEXT("autoquestwatch"),TEXT("Automatic Quest Tracking"));
    Quests->AddSlot().AutoHeight().Padding(2,8)
    [SNew(STextBlock).Text(FText::FromString(TEXT("Newly accepted quests are tracked automatically. Tracked quests are remembered per character between sessions.")))
      .AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    AddOption(Quests,TEXT("showquestlevel"),TEXT("Show Quest Level"));
    Quests->AddSlot().AutoHeight().Padding(2,8)
    [SNew(STextBlock).Text(FText::FromString(TEXT("Show each quest's level in front of its title, e.g. [12] The Fargodeep Mine, in the quest log and the objectives tracker.")))
      .AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    AddOption(Quests,TEXT("autotrackzonequests"),TEXT("Track Quests in Current Zone"));
    Quests->AddSlot().AutoHeight().Padding(2,8)
    [SNew(STextBlock).Text(FText::FromString(TEXT("Also track every quest that belongs to the zone you are in; they leave the tracker when you leave the zone.")))
      .AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    // r383 (Stefan 16.10: "wasn't the old one supposed to be the fallback?"): the native tracker.
    AddOption(Quests,TEXT("nativequesttracker"),TEXT("Classic Native Objective Tracker"));
    Quests->AddSlot().AutoHeight().Padding(2,8)
    [SNew(STextBlock).Text(FText::FromString(TEXT("The objectives tracker drawn by the client itself instead of the interface's WatchFrame. Use it if the tracker is missing or an addon replaces it badly.")))
      .AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    auto Names=AddPage(TEXT("Names"));
    Names->AddSlot().AutoHeight().Padding(0,0,0,8)
    [SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text_Lambda([](){const TCHAR* Modes[]={TEXT("Names only"),TEXT("Names and nameplates"),TEXT("Target only")};
      return FText::FromString(FString::Printf(TEXT("V filter: %s"),Modes[FWowClientPreferences::GetNameplateMode()]));})
      .OnClicked_Lambda([](){FWowClientPreferences::SetNameplateMode((FWowClientPreferences::GetNameplateMode()+1)%3);return FReply::Handled();})];
    Names->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Press V to cycle all NPC and player labels. NPC titles and guild names appear below names; your target keeps its nameplate."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    AddOption(Names,TEXT("chatbubbles"),TEXT("Show Chat Bubbles"));
    AddOption(Names,TEXT("chatbubblesparty"),TEXT("Show Party Chat Bubbles"));
    auto Combat=AddPage(TEXT("Combat"));
    AddOption(Combat,TEXT("floatingcombattextdamage"),TEXT("Show Floating Damage Numbers"));
    AddOption(Combat,TEXT("combattextsum"),TEXT("Combine damage numbers per target (2 seconds)"));
    AddOption(Combat,TEXT("combattextsumheals"),TEXT("Combine heals and resource gains you receive"));
    AddOption(Combat,TEXT("combattextresource"),TEXT("Show Resource Gains (Mana, Rage, Energy, Runic Power)"));
    AddOption(Combat,TEXT("autolootdefault"),TEXT("Auto Loot"));
    AddOption(Combat,TEXT("lootundermouse"),TEXT("Open Loot Window at Mouse"));
    // Auto Loot Key (3.3.5 Interface > Controls dropdown): held while looting it reverses Auto Loot.
    static TArray<TSharedPtr<FString>> AutoLootKeyNames={MakeShared<FString>(TEXT("Shift")),MakeShared<FString>(TEXT("Ctrl")),
        MakeShared<FString>(TEXT("Alt")),MakeShared<FString>(TEXT("None"))};
    Combat->AddSlot().AutoHeight().Padding(2,6)
    [SNew(SBox).WidthOverride(360.f).HAlign(HAlign_Left)
      [MakeComboRow(TEXT("Auto Loot Key"),&AutoLootKeyNames,
        [](TSharedPtr<FString> Item){const int32 Key=AutoLootKeyNames.IndexOfByPredicate([&Item](const TSharedPtr<FString>& N){return *N==*Item;});
          if(Key!=INDEX_NONE) FWowClientPreferences::SetAutoLootKey(Key);},
        TAttribute<FText>::CreateLambda([](){return FText::FromString(*AutoLootKeyNames[FWowClientPreferences::GetAutoLootKey()]);}))]];
    Combat->AddSlot().AutoHeight().Padding(2,4,2,8)
    [SNew(STextBlock).Text_Lambda([](){const int32 Key=FWowClientPreferences::GetAutoLootKey();
        return FText::FromString(Key==3?FString(TEXT("No key reverses Auto Loot.")):
          FString::Printf(TEXT("Hold %s when looting to reverse Auto Loot: with Auto Loot on you get the loot window, with it off everything is taken."),**AutoLootKeyNames[Key]));})
      .AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    // r224 (Stefan 11.10): 3.3.5 Interface Options > Display: Show Helm / Show Cloak. The boxes
    // mirror the server's player flags; a click sends the change (ShowHelm/ShowCloak).
    auto Display=AddPage(TEXT("Display"));
    AddOption(Display,TEXT("showhelm"),TEXT("Show Helm"));
    AddOption(Display,TEXT("showcloak"),TEXT("Show Cloak"));
    auto Bars=AddPage(TEXT("Action Bars"));
    // The client's names, with the main-bar page each one shows (the page arrows skip pages that
    // are already on screen) - the numbers 3-6 did not match the pages (Stefan 10.10).
    AddOption(Bars,TEXT("actionbar3"),TEXT("Bottom Left Bar (page 6)"));
    AddOption(Bars,TEXT("actionbar4"),TEXT("Bottom Right Bar (page 5)"));
    AddOption(Bars,TEXT("actionbar5"),TEXT("Right Bar (page 3)"));
    AddOption(Bars,TEXT("actionbar6"),TEXT("Right Bar 2 (page 4)"));
    AddOption(Bars,TEXT("alwaysshowactionbars"),TEXT("Show empty action bar slots"));
    AddOption(Bars,TEXT("showcooldowntime"),TEXT("Show cooldown time"));
    AddOption(Bars,TEXT("holdtorecast"),TEXT("Hold an action key to cast it again"));
    auto Units=AddPage(TEXT("Unit Frames"));
    AddOption(Units,TEXT("showtargetoftarget"),TEXT("Show target of target and focus target"));
    AddOption(Units,TEXT("playerstatustext"),TEXT("Show health and resource values"));
    AddOption(Units,TEXT("statustextpercentage"),TEXT("Show health and resources as percentages"));
    Units->AddSlot().AutoHeight().Padding(0,18,0,8)
    [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("%s: %.0f%%"),FWowClientPreferences::IsVrUiMode()?TEXT("UI Scale (VR headset)"):TEXT("UI Scale"),FWowClientPreferences::GetUIScale()*100.f));}).ColorAndOpacity(LoginOptionsGold)];
    Units->AddSlot().AutoHeight()
    [SNew(SWowStepSlider).LowText(TEXT("64%")).HighText(TEXT("150%")).ToolTip(WowTip(TEXT("Size of the whole game interface. The monitor and the VR headset each have their own value. 64-150 %: 64-100 % like the original client, above 100 % larger than it (the default bars may then not fit on smaller screens). The interface reloads a moment after you let go."))).Value_Lambda([](){return (FWowClientPreferences::GetUIScale()-0.64f)/0.86f;})
      .OnValueChanged_Lambda([](float Value){FWowClientPreferences::SetUIScale(FMath::RoundToFloat((0.64f+Value*0.86f)*100.f)/100.f);})];
    // Per-group sizes on top of UI Scale; the frame manager applies them live (no reload).
    auto AddGroupScale=[&Units](const TCHAR* Label,const TCHAR* Tip,TFunction<float()> Get,TFunction<void(float)> Set)
    {
        const FString LabelText(Label);
        Units->AddSlot().AutoHeight().Padding(0,14,0,6)
        [SNew(STextBlock).Text_Lambda([LabelText,Get](){return FText::FromString(FString::Printf(TEXT("%s: %.0f%%"),*LabelText,Get()*100.f));}).ColorAndOpacity(LoginOptionsGold)];
        Units->AddSlot().AutoHeight()
        [SNew(SWowStepSlider).LowText(TEXT("50%")).HighText(TEXT("200%")).ToolTip(WowTip(Tip)).Value_Lambda([Get](){return (Get()-0.5f)/1.5f;})
          .OnValueChanged_Lambda([Set](float Value){Set(FMath::RoundToFloat((0.5f+Value*1.5f)*20.f)/20.f);})];
    };
    AddGroupScale(TEXT("Unit Frame Size"),TEXT("Size of the player, target, focus and party frames on top of UI Scale, 50 to 200 %."),
        [](){return FWowClientPreferences::GetUnitFrameScale();},[](float V){FWowClientPreferences::SetUnitFrameScale(V);});
    AddGroupScale(TEXT("Action Bar Size"),TEXT("Size of the action bars on top of UI Scale, 50 to 200 %. The bars shrink automatically if they would not fit the screen width."),
        [](){return FWowClientPreferences::GetActionBarScale();},[](float V){FWowClientPreferences::SetActionBarScale(V);});
    AddGroupScale(TEXT("Minimap Size"),TEXT("Size of the minimap on top of UI Scale, 50 to 200 %."),
        [](){return FWowClientPreferences::GetMinimapScale();},[](float V){FWowClientPreferences::SetMinimapScale(V);});
    auto Tips=AddPage(TEXT("Tooltips"));
    AddOption(Tips,TEXT("tooltipatcursor"),TEXT("Tooltip follows mouse pointer"));
    Tips->AddSlot().AutoHeight().Padding(2,8)
    [SNew(STextBlock).Text(FText::FromString(TEXT("Off: tooltips appear above the bags in the bottom right corner, like the original client.")))
      .AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    Tips->AddSlot().AutoHeight().Padding(0,18,0,8)
    [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Tooltip Scale: %.0f%%"),FWowClientPreferences::GetTooltipScale()*100.f));}).ColorAndOpacity(LoginOptionsGold)];
    Tips->AddSlot().AutoHeight()
    [SNew(SWowStepSlider).LowText(TEXT("50%")).HighText(TEXT("150%")).ToolTip(WowTip(TEXT("Size of tooltips, 50 to 150 %."))).Value_Lambda([](){return FWowClientPreferences::GetTooltipScale()-0.5f;})
      .OnValueChanged_Lambda([](float Value){FWowClientPreferences::SetTooltipScale(FMath::RoundToFloat((0.5f+Value)*20.f)/20.f);})];
    // Stefan 11.10: how long a unit's tooltip stays after the pointer leaves it (moving mobs).
    Tips->AddSlot().AutoHeight().Padding(0,18,0,8)
    [SNew(STextBlock).Text_Lambda([](){const float T=FWowClientPreferences::GetTooltipFadeTime();
        return FText::FromString(T<=0.f?FString(TEXT("Tooltip Fade Time: off")):FString::Printf(TEXT("Tooltip Fade Time: %.1f s"),T));}).ColorAndOpacity(LoginOptionsGold)];
    Tips->AddSlot().AutoHeight()
    [SNew(SWowStepSlider).LowText(TEXT("0 s")).HighText(TEXT("5 s"))
      .ToolTip(WowTip(TEXT("How long a creature's or player's tooltip stays after the pointer leaves it, before it fades out. 0 hides it at once.")))
      .Value_Lambda([](){return FWowClientPreferences::GetTooltipFadeTime()/5.f;})
      .OnValueChanged_Lambda([](float Value){FWowClientPreferences::SetTooltipFadeTime(FMath::RoundToFloat(Value*5.f*10.f)/10.f);})];
    auto Camera=AddPage(TEXT("Camera"));
    auto Mouse=AddPage(TEXT("Mouse"));
    Camera->AddSlot().AutoHeight().Padding(0,12,0,6)
    [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Camera sensitivity: %.0f%%"),FWowClientPreferences::GetCameraMouseSensitivity()*100.f));}).ColorAndOpacity(LoginOptionsGold)];
    Camera->AddSlot().AutoHeight()
    [SNew(SWowStepSlider).ToolTip(WowTip(TEXT("How fast the camera turns when you drag with the mouse."))).Value_Lambda([](){return (FWowClientPreferences::GetCameraMouseSensitivity()-.25f)/2.75f;})
      .OnValueChanged_Lambda([](float Value){FWowClientPreferences::SetCameraMouseSensitivity(FMath::RoundToFloat((.25f+Value*2.75f)*100.f)/100.f);})];
    Mouse->AddSlot().AutoHeight().Padding(0,12,0,6)
    [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Cursor size: %.0f%%"),FWowClientPreferences::GetMouseCursorScale()*100.f));}).ColorAndOpacity(LoginOptionsGold)];
    Mouse->AddSlot().AutoHeight()
    [SNew(SWowStepSlider).LowText(TEXT("50%")).HighText(TEXT("200%")).ToolTip(WowTip(TEXT("Size of the mouse pointer. In VR it is drawn 5 times bigger on top of this."))).Value_Lambda([](){return (FWowClientPreferences::GetMouseCursorScale()-.5f)/1.5f;})
      .OnValueChanged_Lambda([](float Value){FWowClientPreferences::SetMouseCursorScale(FMath::RoundToFloat((.5f+Value*1.5f)*100.f)/100.f);})];
    Mouse->AddSlot().AutoHeight().Padding(0,12)
    [SNew(STextBlock).Text(FText::FromString(TEXT("Cursor size applies to the metal glove in both cursor modes."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    // r351 (Stefan 15.10): cursor sets - every folder in <client>\User\Cursor is a set.
    {
        static TArray<TSharedPtr<FString>> GCursorSetOptions;
        static const FString DefaultSetLabel(TEXT("Default (WoW)"));
        GCursorSetOptions.Reset();
        GCursorSetOptions.Add(MakeShared<FString>(DefaultSetLabel));
        for (const FString& SetName : UWowCursorManager::ListCursorSets()) GCursorSetOptions.Add(MakeShared<FString>(SetName));
        Mouse->AddSlot().AutoHeight().Padding(0,18,0,6)
        [SNew(STextBlock).Text(FText::FromString(TEXT("Experimental"))).ColorAndOpacity(LoginOptionsGold)];
        Mouse->AddSlot().AutoHeight().Padding(0,4)
        [MakeComboRow(TEXT("Cursor set"), &GCursorSetOptions,
            [](TSharedPtr<FString> Item) { UWowCursorManager::SetActiveCursorSet(*Item == DefaultSetLabel ? FString() : *Item); },
            TAttribute<FText>::CreateLambda([]()
            {
                const FString Active = UWowCursorManager::GetActiveCursorSet();
                return FText::FromString(Active.IsEmpty() ? DefaultSetLabel : Active);
            }))];
        Mouse->AddSlot().AutoHeight().Padding(0,8)
        [SNew(STextBlock).Text(FText::FromString(FString::Printf(
            TEXT("Experimental. Every folder in %s is a cursor set: .png, .tga, .bmp, .jpg, .blp, .cur, .ico or animated .ani files up to 256 x 256. Files named like the client's cursors (Point, Attack, Speak, Buy, Quest, QuestTurnIn, Inspect, Pickup, Interact, Cast, UnablePoint ...) are used for those; Windows cursor sets work as they are (Normal, Link Select, Precision, Unavailable ...), and cursors.txt lines like \"Attack = red.ani\" choose files by hand. A cursor the set does not have stays the client's own. A set can hold a folder per race - Human, Orc, Undead, NightElf (Warcraft III), a WoW race like BloodElf, or Alliance / Horde - used for your character's race. The click point comes from a .cur/.ani file, or from hotspot.txt lines like \"Point 3 2\" (pixels); otherwise it is the top-left corner. Reopen the options to see new folders."),
            *FPaths::ConvertRelativePathToFull(UWowCursorManager::GetCursorSetsFolder()))))
            .AutoWrapText(true).ColorAndOpacity(LoginOptionsText)];
    }
    AddOption(Camera,TEXT("weatherlens"),TEXT("Rain and melting snow on the camera"));
    AddOption(Camera,TEXT("wetlens"),TEXT("Camera drips after leaving water"));
    AddOption(Camera,TEXT("frostlens"),TEXT("Corner frost and blue tint in cold weather"));
    AddOption(Camera,TEXT("satelliterecord"),TEXT("Record Satellite v2 map tiles while playing"));
    AddOption(Camera,TEXT("headlook"),TEXT("Character looks at target / camera direction"));
    // Stefan 11.10: the grey inset panels made this page look unlike the other tabs; the list and
    // the page sit on the window's own background like everywhere else.
    auto InsetPanel=[](TSharedRef<SWidget> Content,float PanelPadding)->TSharedRef<SWidget>
    {
        return SNew(SBox).Padding(PanelPadding)[Content];
    };
    return SNew(SVerticalBox)
      +SVerticalBox::Slot().FillHeight(1)
      [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)
        [SNew(SBox).WidthOverride(150.f)[InsetPanel(SNew(SScrollBox).ScrollBarStyle(WowScrollStyle())+SScrollBox::Slot()[Categories],4.f)]]
        +SHorizontalBox::Slot().FillWidth(1)
        [InsetPanel(Pages,12.f)]]
      +SVerticalBox::Slot().AutoHeight().Padding(0,8)
      [SNew(STextBlock).Text(FText::FromString(TEXT("Hold Ctrl and drag with the left mouse button to move a frame."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
      +SVerticalBox::Slot().AutoHeight()
      [MakeOptionButton(TEXT("Reset frames"),FOnClicked::CreateLambda([](){FWowFrameLayout::Reset();return FReply::Handled();}))];
}

static TSharedRef<SWidget> MakeWeatherMixRow()
{
    struct FMixBox { const TCHAR* Label; int32 Bit; };
    static const FMixBox Boxes[] = { {TEXT("Overcast"),1}, {TEXT("Fog"),2}, {TEXT("Rain"),4}, {TEXT("Thunderstorm"),8},
        {TEXT("Snow"),16}, {TEXT("Sandstorm"),32}, {TEXT("Ash"),64} };
    constexpr int32 Precipitation = 4|8|16|32|64;
    auto GetMix = [](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreviewMix")); return C?C->GetInt():0; };
    TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(14.f,4.f));
    for (const FMixBox& Box : Boxes)
    {
        const int32 Bit = Box.Bit;
        Row->AddSlot()
        [SNew(SWowCheckButton).Style(WowCheckStyle())
            .ToolTip(WowTip(TEXT("Test: combine weathers. Fog and overcast mix with anything; only one kind of rain, snow, sand or ash at a time. Uses the strength slider below.")))
            .IsChecked_Lambda([GetMix,Bit](){ return (GetMix()&Bit)?ECheckBoxState::Checked:ECheckBoxState::Unchecked; })
            .IsEnabled_Lambda([GetMix,Bit](){ const int32 Mix=GetMix(); return !(Bit&Precipitation) || (Mix&Bit) || !(Mix&Precipitation); })
            .OnCheckStateChanged_Lambda([GetMix,Bit](ECheckBoxState S){
                int32 Mix=GetMix();
                Mix = S==ECheckBoxState::Checked ? (Mix|Bit) : (Mix&~Bit);
                if(auto* M=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreviewMix"))) M->Set(Mix,ECVF_SetByCode);
                if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreview"))) C->Set(Mix?10:0,ECVF_SetByCode); })
            [SNew(STextBlock).Text(FText::FromString(Box.Label)).ColorAndOpacity(LoginOptionsText)]];
    }
    return Row;
}

TSharedRef<SWidget> SWowLoginOptions::BuildTestPage()
{
    return WowScroll(SNew(SScrollBox).ScrollBarStyle(WowScrollStyle()).ScrollBarThickness(FVector2D(12.f, 12.f)).ConsumeMouseWheel(EConsumeMouseWheel::Always) + SScrollBox::Slot()
    [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
        [SNew(STextBlock).Text(FText::FromString(TEXT("Temporary world previews. Your saved settings and the server clock are unchanged."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,12,0,3)
            [MakeComboRow(TEXT("Weather preview"),&WeatherPreviewOptions,
                [this](TSharedPtr<FString> Item) { const int32 Index=FMath::Max(0,WeatherPreviewOptions.IndexOfByKey(Item));
                    if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreview"))) C->Set(Index,ECVF_SetByCode);
                    if(Index!=10) if(auto* M=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreviewMix"))) M->Set(0,ECVF_SetByCode); },
                TAttribute<FText>::CreateLambda([this]() { const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreview")); return FText::FromString(*WeatherPreviewOptions[FMath::Clamp(C?C->GetInt():0,0,WeatherPreviewOptions.Num()-1)]); }))]
            // r534 (Stefan: "mixing weather, we could make it checkboxes, and if some weather don't mix,
            // disable the other checkbox"): fog and overcast mix with anything; one kind of precipitation.
            + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Mix weather:"))).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [MakeWeatherMixRow()]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [SNew(STextBlock).Text_Lambda([](){ const auto* P=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreview")); if(!P||P->GetInt()<3) return FText::FromString(TEXT("Weather strength: not applicable")); const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreviewStrength")); return FText::FromString(FString::Printf(TEXT("Weather strength: %.0f%%"),(C?C->GetFloat():0.7f)*100)); }).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [SNew(SWowStepSlider).ToolTip(WowTip(TEXT("Test: how strong the previewed rain, snow or fog is. 100-200 % boosts the effects.")))
                .Value_Lambda([](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreviewStrength")); return (C?C->GetFloat():0.7f)/2.f; })
                .OnValueChanged_Lambda([](float V){ if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreviewStrength"))) C->Set(V*2.f,ECVF_SetByCode); })
                .IsEnabled_Lambda([](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreview")); return C&&C->GetInt()>=3; })]
            + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,2)
            [SNew(STextBlock).Text_Lambda([](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.SeaState")); const float V=C?C->GetFloat():-1.f;
                return FText::FromString(V<0.f ? FString(TEXT("Sea state: follows the weather")) : FString::Printf(TEXT("Sea state (test): %.0f%% (0 calm, 100 storm)"),V*100.f)); }).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [SNew(SWowStepSlider).ToolTip(WowTip(TEXT("Test: waves from calm (0) to storm (100). Press the button below to let the weather decide again.")))
                .Value_Lambda([](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.SeaState")); return FMath::Max(0.f,C?C->GetFloat():0.f); })
                .OnValueChanged_Lambda([](float V){ if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.SeaState"))) C->Set(V,ECVF_SetByCode); })]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [MakeOptionButton(TEXT("Sea follows the weather"),FOnClicked::CreateLambda([](){
                if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.SeaState"))) C->Set(-1.f,ECVF_SetByCode); return FReply::Handled();}))]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2,0,12)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Clear has 16% cloud cover; Overcast has 100%. Strength applies to precipitation and fog, not cloud cover. 100-200% boosts visual effects. Automatic restores your weather source and ambient haze."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]


        + SVerticalBox::Slot().AutoHeight().Padding(0,10)
        [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Test page: the sky follows the normal game clock. Untick to choose a time below."))).IsChecked_Lambda([](){return WowTestControls::ClockMinutes()<0?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([](ECheckBoxState S){if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime"))) C->Set(S==ECheckBoxState::Checked?-1.f:720.f,ECVF_SetByCode);})
            [SNew(STextBlock).Text(FText::FromString(TEXT("Automatic time"))).ColorAndOpacity(LoginOptionsText)]]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text_Lambda([](){const int32 M=FMath::RoundToInt(FMath::Max(0.f,WowTestControls::ClockMinutes()));return FText::FromString(WowTestControls::ClockMinutes()<0?TEXT("Clock follows normal time"):FString::Printf(TEXT("Test clock: %02d:%02d"),M/60,M%60));}).ColorAndOpacity(LoginOptionsText)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,5)
        [SNew(SWowStepSlider).ToolTip(WowTip(TEXT("Test: time of day for the sky and light. Untick Automatic time first."))).Value_Lambda([](){return FMath::Max(0.f,WowTestControls::ClockMinutes())/1439.f;})
            .OnValueChanged_Lambda([](float V){if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime"))) C->Set(FMath::RoundToFloat(V*1439.f),ECVF_SetByCode);})]
        + SVerticalBox::Slot().AutoHeight().Padding(0,5)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[MakeOptionButton(TEXT("06:00"),FOnClicked::CreateLambda([](){if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime"))) C->Set(360.f,ECVF_SetByCode);return FReply::Handled();}))]
            + SHorizontalBox::Slot().AutoWidth().Padding(4,0)[MakeOptionButton(TEXT("12:00"),FOnClicked::CreateLambda([](){if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime"))) C->Set(720.f,ECVF_SetByCode);return FReply::Handled();}))]
            + SHorizontalBox::Slot().AutoWidth().Padding(4,0)[MakeOptionButton(TEXT("18:00"),FOnClicked::CreateLambda([](){if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime"))) C->Set(1080.f,ECVF_SetByCode);return FReply::Handled();}))]
            + SHorizontalBox::Slot().AutoWidth()[MakeOptionButton(TEXT("00:00"),FOnClicked::CreateLambda([](){if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime"))) C->Set(0.f,ECVF_SetByCode);return FReply::Handled();}))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,12)
        [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Raindrops and melting snow run over the camera when you are outside in bad weather."))).IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("weatherlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("weatherlens"),S==ECheckBoxState::Checked);})
            [SNew(STextBlock).Text(FText::FromString(TEXT("Rain and melting snow on camera lens"))).ColorAndOpacity(LoginOptionsText)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,4)
        [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Water drips run down the camera for a moment after you leave water."))).IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("wetlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("wetlens"),S==ECheckBoxState::Checked);})
            [SNew(STextBlock).Text(FText::FromString(TEXT("Camera drips after leaving water"))).ColorAndOpacity(LoginOptionsText)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,4)
        [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Frost grows in the corners of the view and the picture turns a little blue in cold places."))).IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("frostlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("frostlens"),S==ECheckBoxState::Checked);})
            [SNew(STextBlock).Text(FText::FromString(TEXT("Corner frost and blue tint in cold weather"))).ColorAndOpacity(LoginOptionsText)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,3)
        [SNew(STextBlock).Text(FText::FromString(TEXT("Frost builds gradually outdoors, faster in snowstorms. It thaws in shelter or warmer weather. Cosmetic only."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
        // Rendering experiments (Claude-Perf): set here, not typed as console commands.
        + SVerticalBox::Slot().AutoHeight().Padding(0,4)
        [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Feet are placed on slopes and steps instead of floating or sinking in."))).IsChecked_Lambda([](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.FootIK")); return (!C||C->GetInt()!=0)?ECheckBoxState::Checked:ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([](ECheckBoxState S){ if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.FootIK"))) C->Set(S==ECheckBoxState::Checked?1:0,ECVF_SetByCode); })
            [SNew(STextBlock).Text(FText::FromString(TEXT("Feet follow the ground (foot IK for players and humanoids)"))).ColorAndOpacity(LoginOptionsText)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,4)
        [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Mounts and animals tilt with the ground when they walk up or down hills."))).IsChecked_Lambda([](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.GroundTilt")); return (!C||C->GetInt()!=0)?ECheckBoxState::Checked:ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([](ECheckBoxState S){ if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.GroundTilt"))) C->Set(S==ECheckBoxState::Checked?1:0,ECVF_SetByCode); })
            [SNew(STextBlock).Text(FText::FromString(TEXT("Mounts and four-legged creatures lean with slopes"))).ColorAndOpacity(LoginOptionsText)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,10)
        [MakeOptionButton(TEXT("Restore automatic weather and time"),FOnClicked::CreateLambda([](){
            if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime"))) C->Set(-1.f,ECVF_SetByCode);
            if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreview"))) C->Set(0,ECVF_SetByCode);
            if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreviewMix"))) C->Set(0,ECVF_SetByCode);
            if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.SeaState"))) C->Set(-1.f,ECVF_SetByCode);
            return FReply::Handled();}))]
    ]);
}

TSharedRef<SWidget> SWowLoginOptions::BuildExperimentalPage()
{
    // r382 (Stefan 16.10: "make more tabs under Experimental so it is easier to find"): one long page
    // became tabs like the Interface page - a category list on the left, the page on the right with
    // its title and a one-line description. The weather-on-camera options (also Interface > Camera)
    // are listed under Camera & Lens and Weather too. The last tab opened is kept while the game runs.
    static int32 GExperimentalTab = 0;
    auto ExpPages=SNew(SWidgetSwitcher);
    auto ExpCategories=SNew(SVerticalBox);
    auto AddExpTab=[&](const TCHAR* Label,const TCHAR* Blurb,TSharedRef<SVerticalBox> Rows)
    {
        const int32 Index=ExpPages->GetNumWidgets();
        ExpCategories->AddSlot().AutoHeight()
        [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).Cursor(EMouseCursor::Hand)
          .OnClicked_Lambda([ExpPages,Index](){GExperimentalTab=Index;ExpPages->SetActiveWidgetIndex(Index);return FReply::Handled();})
          [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).Padding(FMargin(10,4))
            .BorderBackgroundColor_Lambda([ExpPages,Index](){return ExpPages->GetActiveWidgetIndex()==Index?FLinearColor(0.42f,0.30f,0.02f,0.45f):FLinearColor::Transparent;})
            [SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold",13))
              .ColorAndOpacity_Lambda([ExpPages,Index](){return FSlateColor(ExpPages->GetActiveWidgetIndex()==Index?FLinearColor::White:LoginOptionsGold);})]]];
        Rows->InsertSlot(0).AutoHeight().Padding(0,0,0,12)
        [SNew(STextBlock).Text(FText::FromString(Blurb)).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(LoginOptionsText)];
        Rows->InsertSlot(0).AutoHeight().Padding(0,2,0,2)
        [SNew(STextBlock).Text(FText::FromString(Label)).Font(FWowNativeWidgetStyle::Heading(20)).ColorAndOpacity(LoginOptionsGold)];
        ExpPages->AddSlot()[WowScroll(SNew(SScrollBox).ScrollBarStyle(WowScrollStyle()).ScrollBarThickness(FVector2D(12.f, 12.f)).ConsumeMouseWheel(EConsumeMouseWheel::Always)+SScrollBox::Slot()[Rows])];
    };
    // r383: one check box per world option (bInverse: the box is ticked while the option is off),
    // and the stealth look as one choice of four (none of the three options on = Shadow cloak).
    auto ExpCheck=[this](const TCHAR* Key,const TCHAR* Label,const TCHAR* Tip,bool bInverse)->TSharedRef<SWidget>
    {
        const FString Name(Key);
        return SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(Tip))
            .IsChecked_Lambda([Name,bInverse](){return FWowClientPreferences::GetWorldOption(Name)!=bInverse?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Name,bInverse](ECheckBoxState S){const bool bOn=(S==ECheckBoxState::Checked)!=bInverse; FWowClientPreferences::SetWorldOption(Name,bOn); OnInterfaceOptionChanged.ExecuteIfBound(Name,bOn);})
            [SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(LoginOptionsText)];
    };
    auto StealthLook=[this](const TCHAR* Key,const TCHAR* Label,const TCHAR* Tip)->TSharedRef<SWidget>
    {
        const FString Name(Key?Key:TEXT(""));
        return SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(Tip))
            .IsChecked_Lambda([Name]()
            {
                const bool bAny=FWowClientPreferences::GetWorldOption(TEXT("stealthclassic"))
                    || FWowClientPreferences::GetWorldOption(TEXT("stealthpredator"))
                    || FWowClientPreferences::GetWorldOption(TEXT("stealthshadowonly"));
                const bool bOn=Name.IsEmpty()?!bAny:FWowClientPreferences::GetWorldOption(Name);
                return bOn?ECheckBoxState::Checked:ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this,Name](ECheckBoxState)
            {
                for(const TCHAR* Look:{TEXT("stealthclassic"),TEXT("stealthpredator"),TEXT("stealthshadowonly")})
                {
                    const bool bOn=Name.Equals(Look);
                    FWowClientPreferences::SetWorldOption(Look,bOn);
                    OnInterfaceOptionChanged.ExecuteIfBound(Look,bOn);
                }
            })
            [SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(LoginOptionsText)];
    };
    // r395: camera style 0 normal, 1 isometric, 2 Don't Starve (one at a time, and not with VR).
    auto CameraStyleNow=[]()->int32
    {
        if(!FWowClientPreferences::GetWorldOption(TEXT("isometriccamera"))) return 0;
        return FWowClientPreferences::GetWorldOption(TEXT("isodontstarve"))?2:1;
    };
    auto CameraStyle=[this,CameraStyleNow](int32 Style,const TCHAR* Label,const TCHAR* Tip)->TSharedRef<SWidget>
    {
        return SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(Tip))
            .IsChecked_Lambda([Style,CameraStyleNow](){return !FWowVRSupport::IsActive() && CameraStyleNow()==Style?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Style](ECheckBoxState)
            {
                const bool bIso=Style!=0, bDontStarve=Style==2;
                FWowClientPreferences::SetWorldOption(TEXT("isometriccamera"),bIso);
                OnInterfaceOptionChanged.ExecuteIfBound(TEXT("isometriccamera"),bIso);
                FWowClientPreferences::SetWorldOption(TEXT("isodontstarve"),bDontStarve);
                OnInterfaceOptionChanged.ExecuteIfBound(TEXT("isodontstarve"),bDontStarve);
                if(bIso && FWowClientPreferences::GetWorldOption(TEXT("vrmode")))
                {
                    FWowClientPreferences::SetWorldOption(TEXT("vrmode"),false);
                    OnInterfaceOptionChanged.ExecuteIfBound(TEXT("vrmode"),false);
                }
            })
            [SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(LoginOptionsText)];
    };
    if (!OnArtworkChanged.IsBound()) // r388: in game only (nothing to see or use on the login screen)
    AddExpTab(TEXT("Camera & Lens"),TEXT("Depth of field, lens effects and what is drawn on the camera. The same camera effects are also under Interface > Camera."),SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
			[
				SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Blurs what is far from the focus point like a camera lens. Press Apply.")))
				.IsChecked_Lambda([this]() { return bPendingDepthOfField ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bPendingDepthOfField = State == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Depth of Field"))).ColorAndOpacity(LoginOptionsText)
				]
			]
            + SVerticalBox::Slot().AutoHeight().Padding(20,3,0,0)
            [SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(FString::Printf(
                TEXT("Depth of Field strength: %.1fx"), PendingDepthOfFieldStrength)); }).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(20,3,0,3)
            [SNew(SWowStepSlider).IsEnabled_Lambda([this]() { return bPendingDepthOfField; })
                .Value_Lambda([this]() { return (PendingDepthOfFieldStrength - 0.5f) / 3.5f; })
                .OnValueChanged_Lambda([this](float Value) { PendingDepthOfFieldStrength = 0.5f + 3.5f * Value; })
                .ToolTip(WowTip(TEXT("Focus follows the centre of the camera. Higher values blur foreground and background more. Press Apply.")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Test: set the focus distance and the sharp area yourself with the sliders below, instead of autofocus.")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("manualdof")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("manualdof"), S==ECheckBoxState::Checked);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Manual depth of field (test)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Needs Depth of Field above. These controls apply immediately and override autofocus while the test is enabled."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Focus distance: %.1f m"), FWowClientPreferences::GetFocusTestDistance()));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowStepSlider).ToolTip(WowTip(TEXT("Test: distance that is sharp when Manual depth of field is on."))).IsEnabled_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("manualdof"));})
                .Value_Lambda([](){return FMath::Sqrt((FWowClientPreferences::GetFocusTestDistance()-0.5f)/99.5f);})
                .OnValueChanged_Lambda([](float V){FWowClientPreferences::SetFocusTestDistance(0.5f+99.5f*V*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Sharp region: %.0f%% (narrow to wide)"), FWowClientPreferences::GetFocusTestWidth()*100.0f));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowStepSlider).ToolTip(WowTip(TEXT("Test: how deep the sharp area is around the focus distance."))).IsEnabled_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("manualdof"));})
                .Value_Lambda([](){return FWowClientPreferences::GetFocusTestWidth();})
                .OnValueChanged_Lambda([](float V){FWowClientPreferences::SetFocusTestWidth(V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("sunrays")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("sunrays"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Glare rays around the sun, light beams past trees and mountains, and a camera lens flare. Applies immediately.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Sun rays and lens flare"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(OptionTip(TEXT("weatherlens")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("weatherlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){const bool bOn=S==ECheckBoxState::Checked; FWowClientPreferences::SetWorldOption(TEXT("weatherlens"),bOn); OnInterfaceOptionChanged.ExecuteIfBound(TEXT("weatherlens"),bOn);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Rain and melting snow on the camera"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(OptionTip(TEXT("wetlens")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("wetlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){const bool bOn=S==ECheckBoxState::Checked; FWowClientPreferences::SetWorldOption(TEXT("wetlens"),bOn); OnInterfaceOptionChanged.ExecuteIfBound(TEXT("wetlens"),bOn);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Camera drips after leaving water"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(OptionTip(TEXT("frostlens")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("frostlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){const bool bOn=S==ECheckBoxState::Checked; FWowClientPreferences::SetWorldOption(TEXT("frostlens"),bOn); OnInterfaceOptionChanged.ExecuteIfBound(TEXT("frostlens"),bOn);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Corner frost and blue tint in cold weather"))).ColorAndOpacity(LoginOptionsText)]]
            // r395 (Stefan 16.10: the Don't Starve view is "its own fun camera alongside isometric
            // diablo view ... we can only have one camera style active at one time"): one choice of
            // camera style. Isometric = isometriccamera; Don't Starve = isometriccamera + isodontstarve
            // (Claude-Perf's WowIsometricCamera reads both); VR (Virtual Reality tab) is the third
            // style - picking any one turns the others off.
            + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,2)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Camera style"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",13)).ColorAndOpacity(LoginOptionsGold)]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [CameraStyle(0,TEXT("Normal"),TEXT("The original WoW camera."))]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [CameraStyle(1,TEXT("Isometric (Diablo-style)"),TEXT("Fixed high camera at the true isometric angle; the wheel zooms. Roofs, ceilings and trees between you and the camera are cut away. Turns VR off."))]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [CameraStyle(2,TEXT("Don't Starve (top-down 2D look)"),TEXT("Bird's-eye view from far away with a narrow lens: fixed angle, no zoom and no turning, as flat and 2D as possible. Turns VR off."))]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FWowVRSupport::IsActive()
                ? TEXT("VR is on - it is a camera style too (Virtual Reality tab); picking one above turns it off.")
                : TEXT("VR (Virtual Reality tab) is a camera style too; turning it on turns these off."));}).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(16,0,0,3)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Isometric and Don't Starve: right-click to move or act, left-click to target. Turn with right-drag, middle mouse or your Turn Left/Right keys (also Key Bindings > Camera Functions)."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
    );
    AddExpTab(TEXT("World & Sky"),TEXT("Lighting, sky, sun and moon, and how the world is drawn."),SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Darker nights and shadows, closer to real light. Off: the brighter classic look.")))
                .IsChecked_Lambda([this](){return bPendingDarkerLighting ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){bPendingDarkerLighting=S==ECheckBoxState::Checked;})
                
                [SNew(STextBlock).Text(FText::FromString(TEXT("Darker, more realistic lighting"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("realisticmoon")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("realisticmoon"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("On: the moon rises and sets at the real times for today's phase, so some nights have no moon until after midnight. Off: the moon is always up at night, opposite the sun. Applies immediately.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Realistic moon rise/set"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("cumulusclouds")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("cumulusclouds"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Real 3D cumulus clouds: separate heaps with flat grey bases and bright billowing tops, towering up in storms. Off: the smoother, flatter cloud layer. Applies immediately.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Realistic cumulus clouds"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("shortrangeao")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("shortrangeao"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Lumen contact shadows: soft darkening in corners, folds and under objects. Turn off if you see grey dotted bands moving on snow or open ground. Applies immediately.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Contact shadows (ambient occlusion)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("portalculling")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("portalculling"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("As the original client does: a building's rooms and Stormwind's districts are drawn only when you are in them or see them through a door, gate or opening. Fixes the doubled gate towers seen from the streets. Off: every part is drawn (for example when flying over a city in the offline editor). Applies immediately.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Original room and portal visibility"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("hidekeepbackdrop")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("hidekeepbackdrop"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Stormwind Keep has a low-detail mountain behind it, so from Elwynn the keep seems to stand on a mountain. Inside the city it shows as a dark smudge over the keep (the original client draws it too). On: hidden while you are in the city, still shown from outside. Needs room and portal visibility on. Applies immediately.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Stone wall instead of the dark mountain backdrop over Stormwind Keep"))).ColorAndOpacity(LoginOptionsText)]]
            // r740 (Claude-Perf, Stefan 23.10: "keep the settings separate, ground and walls"; walls on, ground off for the release)
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("stonereliefwalls")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("stonereliefwalls"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Experimental. Stone and brick walls get a 3D relief: the gaps between the stones are pressed in. Off: the flat original textures. Restart the game after changing it.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Stone relief on walls (experimental)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("stonereliefground")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("stonereliefground"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Experimental. Cobbled streets, sidewalks, flagstone and stone floors and cobbled roads get a 3D relief: the gaps between the stones are pressed in. Still rough at the stone edges; off by default. Off: the flat original textures. Restart the game after changing it.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Stone relief on streets and ground (experimental)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,3)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Moon size: %.0f%%"), FWowClientPreferences::GetMoonScale()*100.0f));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowStepSlider)
                .ToolTip(WowTip(TEXT("Size of the moon in the sky, 100 to 500 %.")))
                .Value_Lambda([](){return (FWowClientPreferences::GetMoonScale()-1.0f)/4.0f;})
                .OnValueChanged_Lambda([](float V){FWowClientPreferences::SetMoonScale(1.0f+4.0f*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Sun size: %.0f%%"), FWowClientPreferences::GetSunScale()*100.0f));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowStepSlider)
                .ToolTip(WowTip(TEXT("Size of the sun in the sky, 100 to 1000 %. A bigger sun also gives softer shadows.")))
                .Value_Lambda([](){return (FWowClientPreferences::GetSunScale()-1.0f)/9.0f;})
                .OnValueChanged_Lambda([](float V){FWowClientPreferences::SetSunScale(1.0f+9.0f*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Applies immediately. A bigger sun also gives softer shadows. For a photographic moon, put a moon colour map at Sky\\Moon.jpg in the client folder (restart to load)."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
    );
    AddExpTab(TEXT("Weather"),TEXT("Zone atmosphere, weather strength and weather on the camera."),SNew(SVerticalBox)
            // r418 (Stefan 18.10: hard to find on the Test page - "can be moved under experimental, on weather
            // tab"): Claude-Perf's distance fog (wow.DistanceFog), now saved as DistanceFog in the settings.
            + SVerticalBox::Slot().AutoHeight().Padding(0,3,0,2)
            [SNew(STextBlock).Text_Lambda([](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.DistanceFog")); const float V=C?C->GetFloat():1.f;
                return FText::FromString(V<0.01f ? FString(TEXT("Distance fog: off (light haze only)")) : FString::Printf(TEXT("Distance fog: %.0f%% of WoW's (hides far mountains in the sky colour)"),V*100.f)); }).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2,0,6)
            [SNew(SWowStepSlider).ToolTip(WowTip(TEXT("How much far land fades into the sky colour, compared with the original game. 0 = off (light haze only), 100 % = as in WoW, up to 200 % thicker.")))
                .Value_Lambda([](){ const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.DistanceFog")); return FMath::Clamp((C?C->GetFloat():1.f)/2.f,0.f,1.f); })
                .OnValueChanged_Lambda([](float V){ const float Fog=FMath::RoundToFloat(V*20.f)/10.f;
                    if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.DistanceFog"))) C->Set(Fog,ECVF_SetByCode);
                    if(GConfig){GConfig->SetFloat(TEXT("WowUnreal.ClientPreferences"),TEXT("DistanceFog"),Fog,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);} })]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Light patchy haze, dust or cold mist that fits the zone. No permanent rain.")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("ambientweather"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("ambientweather"),S==ECheckBoxState::Checked);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Subtle zone atmosphere"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Patchy outdoor haze, dust or cold mist. No permanent rain. Enable Volumetric Fog in Video for sunlight scattering. Applies immediately."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("extremeweather")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("extremeweather"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Experimental. On: the weather can become extreme - heavy blizzards, thick sandstorms and strong thunderstorms, up to full strength with strong winds. Off: all weather stays at most 30 % strong.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Extreme weather"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(OptionTip(TEXT("weatherlens")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("weatherlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){const bool bOn=S==ECheckBoxState::Checked; FWowClientPreferences::SetWorldOption(TEXT("weatherlens"),bOn); OnInterfaceOptionChanged.ExecuteIfBound(TEXT("weatherlens"),bOn);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Rain and melting snow on the camera"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(OptionTip(TEXT("wetlens")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("wetlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){const bool bOn=S==ECheckBoxState::Checked; FWowClientPreferences::SetWorldOption(TEXT("wetlens"),bOn); OnInterfaceOptionChanged.ExecuteIfBound(TEXT("wetlens"),bOn);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Camera drips after leaving water"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(OptionTip(TEXT("frostlens")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("frostlens"))?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){const bool bOn=S==ECheckBoxState::Checked; FWowClientPreferences::SetWorldOption(TEXT("frostlens"),bOn); OnInterfaceOptionChanged.ExecuteIfBound(TEXT("frostlens"),bOn);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Corner frost and blue tint in cold weather"))).ColorAndOpacity(LoginOptionsText)]]
    );
    AddExpTab(TEXT("Effects"),TEXT("Highlights in the world, the immersive interface and the dressing room."),SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Herbs, ore, chests and quest objects get a glint so they are easier to see.")))
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("questsparkles")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("questsparkles"), S==ECheckBoxState::Checked);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Sparkles on quest objects and gathering nodes"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("lootbeam")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("lootbeam"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("On: a pillar of light over every corpse you can still loot, coloured by its best item (servers with mod-loot-beam colour it before you open the loot). Off: the original WoW loot sparkles.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Loot beams"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("immersiveui")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("immersiveui"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Experimental. While you move, the interface fades away and only the minimap stays faintly visible. In combat only the action bars and unit frames stay, at 40 %. Standing still out of combat, or typing in the chat, shows everything. Open windows are never hidden.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Immersive interface (experimental)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("mountpreview")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("mountpreview"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("On: Ctrl-click a mount (Auction House, bags, links) to see it in the dressing room. The original 3.3.5 dressing room shows gear only.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Mount preview in the dressing room"))).ColorAndOpacity(LoginOptionsText)]]
    );
    AddExpTab(TEXT("Character"),TEXT("Collision, first person and standing up."),SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("You bump into NPCs, creatures and players instead of walking through them.")))
                .IsChecked_Lambda([this](){return bPendingEntityCollision ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){bPendingEntityCollision=S==ECheckBoxState::Checked;})
                
                [SNew(STextBlock).Text(FText::FromString(TEXT("Collision with NPCs, creatures and players"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("The close-in view with your body visible when you look down (mouse wheel all the way in gives the view from your character's eyes), on the monitor and in VR.")))
                .IsChecked_Lambda([this](){return bPendingFirstPerson ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){bPendingFirstPerson=S==ECheckBoxState::Checked;})
                
                [SNew(STextBlock).Text(FText::FromString(TEXT("First-person immersion"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle()).ToolTip(WowTip(TEXT("Test: your helmet model stays visible in first person (it can block the view).")))
                .IsChecked_Lambda([this](){return bPendingHelmetView ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){bPendingHelmetView=S==ECheckBoxState::Checked;})
                .IsEnabled_Lambda([this](){return bPendingFirstPerson;})
                [SNew(STextBlock).Text(FText::FromString(TEXT("Show equipped 3D helmet in first person (test)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("rootedstandup")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("rootedstandup"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Moving while seated plays the stand-up animation first and holds you in place until you are on your feet. Off = original WoW (instant stand). Applies immediately.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Stand up before moving (rooted while getting up)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [ExpCheck(TEXT("headlook"),TEXT("Character looks at target / camera direction"),TEXT("Your character turns its head towards your target, or where the camera looks. Also under Interface > Camera."),false)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [ExpCheck(TEXT("noautoretaliate"),TEXT("Attack back when attacked"),TEXT("On (as in the original client): when something hits you while you are not fighting, your character turns to it and attacks back. Off: you keep doing what you were doing."),true)]
    );
    if (!OnArtworkChanged.IsBound()) // r388: in game only (nothing to see or use on the login screen)
    AddExpTab(TEXT("Classes"),TEXT("Options for one class or race. Each heading says who it is for."),SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,2,0,2)
            [SNew(STextBlock).Text(FText::FromString(TEXT("All classes"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",13)).ColorAndOpacity(LoginOptionsGold)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3,0,6)
            [SNew(STextBlock).Text(FText::FromString(TEXT("How stealthed players look - yours and stealthed party members."))).AutoWrapText(true).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [StealthLook(nullptr,TEXT("Shadow cloak"),TEXT("The default: a stealthed player is a dark, see-through shadow."))]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [StealthLook(TEXT("stealthclassic"),TEXT("Classic"),TEXT("As in the original client: a stealthed player is just see-through."))]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [StealthLook(TEXT("stealthpredator"),TEXT("Predator"),TEXT("A stealthed player bends the light behind them, like a heat shimmer."))]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [StealthLook(TEXT("stealthshadowonly"),TEXT("Shadow only"),TEXT("A stealthed player shows only as their shadow on the ground."))]
            + SVerticalBox::Slot().AutoHeight().Padding(0,14,0,2)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Rogue, Druid, Night Elf (Shadowmeld)"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",13)).ColorAndOpacity(LoginOptionsGold)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [ExpCheck(TEXT("stealthdetectrange"),TEXT("Show enemy detection range while stealthed"),TEXT("While you are stealthed, a cone in front of each nearby enemy shows how close you can get: blue safe, amber may notice you, red seen."),false)]
    );
    AddExpTab(TEXT("HD Models"),TEXT("HD models from patch-H / HD data. Greyed out without HD data."),SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsEnabled_Lambda([](){return FMpqManager::AnyHDDataAvailable();}) // r362: grey without HD data
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("hdcharacters")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("hdcharacters"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("On: your own character uses the HD model from the HD pack in dist\\Data\\HD (an .MPQ such as patch-HD.MPQ plus its HD_Data textures; stock models stay untouched). Greyed out when no HD pack is installed. Takes effect the next time your model is built (log in again or change gear). Off, or HD data missing: the original model.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("HD character model (experimental)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsEnabled_Lambda([](){return FMpqManager::AnyHDDataAvailable();}) // r362: grey without HD data
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("hdnpcs")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("hdnpcs"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("On: humanoid NPCs (guards, vendors, quest givers of the playable races) switch to the HD model. To keep the frame rate up they are swapped one at a time, nearest first, while you stand still - so a busy city fills in over a few minutes. In photo mode and on loading screens they are swapped much faster. Needs the HD data (see HD character model).")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("NPC HD models (swapped over time)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsEnabled_Lambda([](){return FMpqManager::AnyHDDataAvailable();}) // r362: grey without HD data
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("hdplayers")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("hdplayers"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("On: other players switch to the HD model. To keep the frame rate up they are swapped one at a time, nearest first, while you stand still. In photo mode (a guild photo) and on loading screens (entering a dungeon with your group) they are swapped much faster. Needs the HD data (see HD character model).")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Player HD models (swapped over time)"))).ColorAndOpacity(LoginOptionsText)]]
    );
    if (!OnArtworkChanged.IsBound()) // r388: in game only (nothing to see or use on the login screen)
    AddExpTab(TEXT("Virtual Reality"),TEXT("Headset mode, comfort and VR graphics."),SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("vrmode")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S)
                {
                    FWowClientPreferences::SetWorldOption(TEXT("vrmode"), S==ECheckBoxState::Checked);
                    // r385/r395: VR is a camera style of its own - it turns Isometric and Don't Starve off.
                    if(S==ECheckBoxState::Checked)
                    {
                        FWowClientPreferences::SetWorldOption(TEXT("isometriccamera"), false);
                        FWowClientPreferences::SetWorldOption(TEXT("isodontstarve"), false);
                    }
                })
                .ToolTip(WowTip(TEXT("Seated VR preview through OpenXR (SteamVR, Meta, Virtual Desktop ...). Works in the game world; login and character screens stay on the monitor. Ctrl+Shift+V switches VR on/off at any time. Not remembered: every launch starts with VR off. The interface is shown on panels around you.")))
                [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FWowVRSupport::IsActive() ? TEXT("VR headset (preview) - active") : FWowVRSupport::IsHeadsetAvailable() ? TEXT("VR headset (preview)") : TEXT("VR headset (preview) - no headset found"));}).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                // Needs First-person immersion (Claude-Perf r222): shown off while immersion is unticked
                // (Apply then turns it off), and ticking it ticks immersion too, so Apply keeps both.
                .IsChecked_Lambda([this](){return bPendingFirstPerson && FWowClientPreferences::GetWorldOption(TEXT("vrfirstperson")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){const bool bOn=S==ECheckBoxState::Checked; if(bOn) bPendingFirstPerson=true; FWowClientPreferences::SetWorldOption(TEXT("vrfirstperson"), bOn);})
                .ToolTip(WowTip(TEXT("On: the headset view is always first person, with your own body visible when you look down (the head is hidden). Off: the mouse wheel zooms out to third person as on the monitor. Needs First-person immersion (switches it on).")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("VR: first person only"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("vrmousepointer")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("vrmousepointer"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Off: your head aims - the crosshair sits in the middle of the view, and clicks on the world act on what it covers. On: the mouse aims - the crosshair follows the mouse in the world and turns into the normal mouse pointer over the interface panels.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("VR: aim with the mouse instead of the head"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(16,3,0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("vrdwellclick")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowClientPreferences::SetWorldOption(TEXT("vrdwellclick"), S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Experimental. Rest the crosshair on a creature, player or object, or on a button of the interface panels: a ring fills around it and, when full, it is clicked: in the world like a right click (attack a monster, loot a body, talk to an NPC, use an object), on the interface like a left click. Look away before the same thing can be clicked again. Not while moving or turning with the mouse.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("VR: stare-to-click (experimental)"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,0)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Stare time: %.1f s"), FWowClientPreferences::GetVrDwellTime()));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowStepSlider)
                .IsEnabled_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("vrdwellclick"));})
                .ToolTip(WowTip(TEXT("How long you must keep looking at something before stare-to-click clicks it: 0.5 to 2 seconds.")))
                .Value_Lambda([](){return (FWowClientPreferences::GetVrDwellTime()-0.5f)/1.5f;})
                .OnValueChanged_Lambda([](float V){FWowClientPreferences::SetVrDwellTime(0.5f+1.5f*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(16,8,0,0)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString(TEXT("VR graphics: ")) + (FWowVRComfort::GetGraphicsLevel()==0 ? TEXT("Performance") : FWowVRComfort::GetGraphicsLevel()==2 ? TEXT("Full") : FWowVRComfort::GetGraphicsLevel()==3 ? TEXT("Potato") : TEXT("Balanced")));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(12,3,0,3)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [MakeOptionButton(TEXT("Potato"), FOnClicked::CreateLambda([](){FWowVRComfort::SetGraphicsLevel(3); return FReply::Handled();}), TEXT("Performance plus medium shadows, and Unreal's own upscaler (from 60 %) instead of FSR. Fastest; FSR features (FSR upscaling, frame generation, AMD noise/ray regeneration) are not used. Only while the headset is on."))]
                + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [MakeOptionButton(TEXT("Performance"), FOnClicked::CreateLambda([](){FWowVRComfort::SetGraphicsLevel(0); return FReply::Handled();}), TEXT("Balanced plus medium shadows. FSR stays on. Only while the headset is on."))]
                + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [MakeOptionButton(TEXT("Balanced"), FOnClicked::CreateLambda([](){FWowVRComfort::SetGraphicsLevel(1); return FReply::Handled();}), TEXT("Shadow maps instead of ray-traced shadows, slightly cheaper global illumination, no cloud shadows. Same look, noticeably faster. Recommended. Only while the headset is on."))]
                + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [MakeOptionButton(TEXT("Full"), FOnClicked::CreateLambda([](){FWowVRComfort::SetGraphicsLevel(2); return FReply::Handled();}), TEXT("Same graphics as on the monitor. Best picture, heaviest.\n\nVR graphics apply only while the headset is on; the monitor keeps its own settings. Frame generation and motion blur are always off in the headset."))]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(16,8,0,0)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString(TEXT("VR comfort: ")) + (FWowVRComfort::GetPreset()==EWowVRComfortPreset::Comfortable ? TEXT("Comfortable") : FWowVRComfort::GetPreset()==EWowVRComfortPreset::Balanced ? TEXT("Balanced") : FWowVRComfort::GetPreset()==EWowVRComfortPreset::Immersive ? TEXT("Immersive") : TEXT("Custom")));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(12,3,0,3)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [MakeOptionButton(TEXT("Comfortable"), FOnClicked::CreateLambda([](){FWowVRComfort::ApplyPreset(EWowVRComfortPreset::Comfortable); return FReply::Handled();}), TEXT("For people who get motion-sick: turning in steps, strong dark edge while moving, gentle start/stop, no depth of field in the headset."))]
                + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [MakeOptionButton(TEXT("Balanced"), FOnClicked::CreateLambda([](){FWowVRComfort::ApplyPreset(EWowVRComfortPreset::Balanced); return FReply::Handled();}), TEXT("Smooth turning, light dark edge, lean over your chest when looking down."))]
                + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [MakeOptionButton(TEXT("Immersive"), FOnClicked::CreateLambda([](){FWowVRComfort::ApplyPreset(EWowVRComfortPreset::Immersive); return FReply::Handled();}), TEXT("Smooth turning, no dark edge, look furthest down, depth of field in the headset.\n\nCustom is shown when you have changed any setting below yourself."))]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowVRComfort::GetSnapTurn() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowVRComfort::SetSnapTurn(S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Turning with mouse/keys jumps in steps instead of rotating smoothly. Helps against motion sickness.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Snap turning"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,0)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Snap turn step: %.0f degrees"), FWowVRComfort::GetSnapTurnDegrees()*1.0f));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowStepSlider)
                .ToolTip(WowTip(TEXT("How many degrees each turning step is (15-90).")))
                .Value_Lambda([](){return (FWowVRComfort::GetSnapTurnDegrees()-15.0f)/(90.0f-15.0f);})
                .OnValueChanged_Lambda([](float V){FWowVRComfort::SetSnapTurnDegrees(15.0f+(90.0f-15.0f)*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,0)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Comfort vignette: %.0f%%"), FWowVRComfort::GetVignette()*100.0f));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowStepSlider)
                .ToolTip(WowTip(TEXT("Darkens the edge of the view while moving or turning. Higher = stronger. Helps against motion sickness.")))
                .Value_Lambda([](){return (FWowVRComfort::GetVignette()-0.0f)/(1.0f-0.0f);})
                .OnValueChanged_Lambda([](float V){FWowVRComfort::SetVignette(0.0f+(1.0f-0.0f)*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,0)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Look-down gain: %.2fx"), FWowVRComfort::GetLookDownGain()*1.0f));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowStepSlider)
                .ToolTip(WowTip(TEXT("How much further the view tilts than your head when you look down, so you can see your body and feet while seated (1.0x = like your head).")))
                .Value_Lambda([](){return (FWowVRComfort::GetLookDownGain()-1.0f)/(3.0f-1.0f);})
                .OnValueChanged_Lambda([](float V){FWowVRComfort::SetLookDownGain(1.0f+(3.0f-1.0f)*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowVRComfort::GetLeanOver() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowVRComfort::SetLeanOver(S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("When you look down, the view leans out over your chest so legs and feet come into view.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Lean over when looking down"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,0)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Movement acceleration: %.2fx"), FWowVRComfort::GetAcceleration()*1.0f));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowStepSlider)
                .ToolTip(WowTip(TEXT("How quickly you start and stop running. Lower = gentler (more comfortable).")))
                .Value_Lambda([](){return (FWowVRComfort::GetAcceleration()-0.5f)/(1.5f-0.5f);})
                .OnValueChanged_Lambda([](float V){FWowVRComfort::SetAcceleration(0.5f+(1.5f-0.5f)*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowCheckButton).Style(WowCheckStyle())
                .IsChecked_Lambda([](){return FWowVRComfort::GetDepthOfField() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState S){FWowVRComfort::SetDepthOfField(S==ECheckBoxState::Checked);})
                .ToolTip(WowTip(TEXT("Blur things far from what you look at, inside the headset. Needs the general Depth of Field option as well.")))
                [SNew(STextBlock).Text(FText::FromString(TEXT("Depth of field in the headset"))).ColorAndOpacity(LoginOptionsText)]]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,0)
            [SNew(STextBlock).Text_Lambda([](){return FText::FromString(FString::Printf(TEXT("Head stillness: %.0f%%"), FWowVRComfort::GetHeadStillness()*100.0f));}).ColorAndOpacity(LoginOptionsText)]
            + SVerticalBox::Slot().AutoHeight().Padding(32,3,0,3)
            [SNew(SWowStepSlider)
                .ToolTip(WowTip(TEXT("Holds tiny head movements still. 0 % = the view follows your head exactly (recommended). Higher values can feel sticky.")))
                .Value_Lambda([](){return (FWowVRComfort::GetHeadStillness()-0.0f)/(1.0f-0.0f);})
                .OnValueChanged_Lambda([](float V){FWowVRComfort::SetHeadStillness(0.0f+(1.0f-0.0f)*V);})]
            + SVerticalBox::Slot().AutoHeight().Padding(0,3)
            [MakeOptionButton(TEXT("Recenter VR view"), FOnClicked::CreateLambda([](){FWowVRSupport::Recenter(); return FReply::Handled();}), TEXT("Makes the direction you are looking now straight ahead, and the interface panels move around it."))]
    );
    ExpPages->SetActiveWidgetIndex(FMath::Clamp(GExperimentalTab,0,ExpPages->GetNumWidgets()-1));
    return SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight().Padding(0,14,0,5)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Experimental")))
                .Font(FWowNativeWidgetStyle::Heading(20)).ColorAndOpacity(LoginOptionsGold)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Features beyond the original client. They may change or be removed; some cost performance."))).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(LoginOptionsText)]
      +SVerticalBox::Slot().FillHeight(1)
      [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)
        [SNew(SBox).WidthOverride(150.f)[SNew(SScrollBox).ScrollBarStyle(WowScrollStyle())+SScrollBox::Slot()[ExpCategories]]]
        +SHorizontalBox::Slot().FillWidth(1)
        [ExpPages]];
}
