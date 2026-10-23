#include "WowWindowStyle.h"
#include "WowColor.h"
#include "WowFrameManager.h"
#include "Fonts/CompositeFont.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
struct FWindowResources : FGCObject
{
    FSlateBrush Background;
    FSlateBrush Edges[8];
    FButtonStyle Button;
    bool bHasBorder = false;
    FSlateBrush TooltipBackground;
    FSlateBrush TooltipEdges[8];
    bool bHasTooltipBorder = false;
    TMap<FString, TObjectPtr<UTexture2D>> Controls;

    FWindowResources() { Reset(); }
    void Reset()
    {
        bHasBorder = false;
        Background = FSlateRoundedBoxBrush(WowColor::FromWow(.025f, .025f, .028f), 1.f,
            WowColor::FromWow(.42f, .42f, .45f), 2.f);
        for (auto& Edge : Edges) Edge = FSlateBrush();
        bHasTooltipBorder = false;
        TooltipBackground = FSlateRoundedBoxBrush(FLinearColor::White, 2.f, WowColor::FromWow(.6f, .6f, .62f), 1.f);
        for (auto& Edge : TooltipEdges) Edge = FSlateBrush();
        Button = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
        Button.SetNormal(FSlateRoundedBoxBrush(WowColor::FromWow(.23f, .012f, .006f), 2.f, WowColor::FromWow(.5f, .46f, .38f), 1.f));
        Button.SetHovered(FSlateRoundedBoxBrush(WowColor::FromWow(.38f, .024f, .01f), 2.f, WowColor::FromWow(.9f, .72f, .35f), 1.f));
        Button.SetPressed(FSlateRoundedBoxBrush(WowColor::FromWow(.12f, .005f, .002f), 2.f, WowColor::FromWow(.6f, .53f, .4f), 1.f));
        Button.SetDisabled(FSlateRoundedBoxBrush(WowColor::FromWow(.065f, .065f, .07f), 2.f, WowColor::FromWow(.2f, .2f, .2f), 1.f));
        Button.SetNormalForeground(FWowWindowStyle::Gold());
        Button.SetHoveredForeground(FLinearColor::White);
        Button.SetPressedForeground(FWowWindowStyle::Gold());
        Button.SetDisabledForeground(WowColor::FromWow(.42f, .42f, .42f));
        Button.SetNormalPadding(FMargin(10,4));
        Button.SetPressedPadding(FMargin(11,5,9,3));
    }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Background.AddReferencedObjects(Collector);
        for (auto& Edge : Edges) Edge.AddReferencedObjects(Collector);
        TooltipBackground.AddReferencedObjects(Collector);
        for (auto& Control : Controls) Collector.AddReferencedObject(Control.Value);
        for (auto& Edge : TooltipEdges) Edge.AddReferencedObjects(Collector);
        Button.Normal.AddReferencedObjects(Collector);
        Button.Hovered.AddReferencedObjects(Collector);
        Button.Pressed.AddReferencedObjects(Collector);
        Button.Disabled.AddReferencedObjects(Collector);
    }
    virtual FString GetReferencerName() const override { return TEXT("FWowWindowStyle"); }
};
FWindowResources& Resources() { static FWindowResources Value; return Value; }

FSlateBrush TextureBrush(UTexture2D* Texture, FVector2D Size)
{
    FSlateBrush Brush;
    Brush.SetResourceObject(Texture);
    Brush.ImageSize = Size;
    Brush.DrawAs = ESlateBrushDrawType::Image;
    return Brush;
}
}

void FWowWindowStyle::Load(FWowFrameManager& Frames)
{
    LoadTextures([&Frames](const FString& Path){return Frames.LoadUITexture(Path);});
}

void FWowWindowStyle::LoadTextures(TFunctionRef<UTexture2D*(const FString&)> LoadTexture)
{
    auto& R = Resources();
    R.Reset();
    if (auto* Texture = LoadTexture(TEXT("Interface\\DialogFrame\\UI-DialogBox-Background")))
    {
        R.Background = TextureBrush(Texture, FVector2D(64,64));
        R.Background.Tiling = ESlateBrushTileType::Both;
    }
    if (auto* Texture = LoadTexture(TEXT("Interface\\DialogFrame\\UI-DialogBox-Border")))
    {
        // WoW backdrop atlas: L,R,T,B,TL,TR,BL,BR. T/B are stored vertically.
        // Reject unrelated HD replacements rather than stretching a wrong atlas.
        if (Texture->GetSizeX() == Texture->GetSizeY() * 8)
        {
            for (int32 I = 0; I < 8; ++I)
            {
                R.Edges[I] = TextureBrush(Texture, FVector2D(24,24));
                R.Edges[I].SetUVRegion(FBox2f(FVector2f(I / 8.f,0), FVector2f((I+1) / 8.f,1)));
            }
            R.bHasBorder = true;
        }
    }
    if (auto* Texture = LoadTexture(TEXT("Interface\\Tooltips\\UI-Tooltip-Background")))
    {
        R.TooltipBackground = TextureBrush(Texture, FVector2D(16,16));
        R.TooltipBackground.Tiling = ESlateBrushTileType::Both;
    }
    if (auto* Texture = LoadTexture(TEXT("Interface\\Tooltips\\UI-Tooltip-Border")))
    {
        if (Texture->GetSizeX() == Texture->GetSizeY() * 8)
        {
            for (int32 I = 0; I < 8; ++I)
            {
                R.TooltipEdges[I] = TextureBrush(Texture, FVector2D(16,16));
                R.TooltipEdges[I].SetUVRegion(FBox2f(FVector2f(I / 8.f,0), FVector2f((I+1) / 8.f,1)));
            }
            R.bHasTooltipBorder = true;
        }
    }
    auto LoadButton = [&LoadTexture](const TCHAR* Path, FSlateBrush& Brush)
    {
        if (auto* Texture = LoadTexture(Path))
        {
            if (Texture->GetSizeX() != Texture->GetSizeY() * 4) return;
            Brush = TextureBrush(Texture, FVector2D(80,22));
            Brush.DrawAs = ESlateBrushDrawType::Box;
            Brush.SetUVRegion(FBox2f(FVector2f(0,0),FVector2f(.625f,.6875f)));
            // UIPanelButtonTemplate is Left / Middle / Right pieces stretched to the button height:
            // a 3-slice. A vertical box margin as well repeated the dark middle rows of the art as a
            // band through the centre of every red button (Stefan 10.10, Accept/Decline/Goodbye).
            Brush.Margin = FMargin(.15f,0.f);
        }
    };
    LoadButton(TEXT("Interface\\Buttons\\UI-Panel-Button-Up"), R.Button.Normal);
    R.Button.Hovered = R.Button.Normal;
    R.Button.Hovered.TintColor = WowColor::FromWow(1.3f, 1.3f, 1.3f);
    LoadButton(TEXT("Interface\\Buttons\\UI-Panel-Button-Down"), R.Button.Pressed);
    LoadButton(TEXT("Interface\\Buttons\\UI-Panel-Button-Disabled"), R.Button.Disabled);
    // Stefan 11.10: the options window's sliders, check boxes and scroll bars as in 3.3.5.
    R.Controls.Reset();
    for (const TCHAR* Path : {
        TEXT("Interface\\Buttons\\UI-SliderBar-Background"), TEXT("Interface\\Buttons\\UI-SliderBar-Border"),
        TEXT("Interface\\Buttons\\UI-SliderBar-Button-Horizontal"),
        TEXT("Interface\\Buttons\\UI-CheckBox-Up"), TEXT("Interface\\Buttons\\UI-CheckBox-Down"),
        TEXT("Interface\\Buttons\\UI-CheckBox-Highlight"), TEXT("Interface\\Buttons\\UI-CheckBox-Check"),
        TEXT("Interface\\Buttons\\UI-CheckBox-Check-Disabled"),
        TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"),
        TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Up"), TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Down"),
        TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Disabled"),
        TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Up"), TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Down"),
        TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Disabled") })
        if (UTexture2D* Texture = LoadTexture(Path)) R.Controls.Add(Path, Texture);
}

UTexture2D* FWowWindowStyle::ControlTexture(const FString& Path)
{
    const TObjectPtr<UTexture2D>* Found = Resources().Controls.Find(Path);
    return Found ? Found->Get() : nullptr;
}

void FWowWindowStyle::Reset() { Resources().Reset(); }
const FSlateBrush* FWowWindowStyle::Background() { return &Resources().Background; }
const FSlateBrush* FWowWindowStyle::Edge(int32 I) { return &Resources().Edges[FMath::Clamp(I,0,7)]; }
const FButtonStyle* FWowWindowStyle::Button() { return &Resources().Button; }
bool FWowWindowStyle::HasOriginalBorder() { return Resources().bHasBorder; }
const FSlateBrush* FWowWindowStyle::TooltipBackground() { return &Resources().TooltipBackground; }
const FSlateBrush* FWowWindowStyle::TooltipEdge(int32 I) { return &Resources().TooltipEdges[FMath::Clamp(I,0,7)]; }
bool FWowWindowStyle::HasTooltipBorder() { return Resources().bHasTooltipBorder; }
FSlateFontInfo FWowWindowStyle::TitleFont(int32 Size)
{
    const FString Path = FPaths::ProjectSavedDir() / TEXT("Fonts/FRIZQT__.TTF");
    if (!IFileManager::Get().FileExists(*Path)) return FCoreStyle::GetDefaultFontStyle("Bold",Size);
    static TSharedPtr<const FCompositeFont> Font = MakeShared<FCompositeFont>(
        FName(TEXT("WoW")),Path,EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    return FSlateFontInfo(Font,Size);
}

void SWowWindowFrame::Construct(const SBorder::FArguments& Args)
{
    SBorder::FArguments Styled = Args;
    Styled.BorderImage(FWowWindowStyle::Background());
    Styled.BorderBackgroundColor(FLinearColor::White);
    const auto P = Args._Padding.Get();
    // Keep room for the 24px corner art. Larger authored spacing is preserved.
    Styled.Padding(FMargin(FMath::Max(12.f,P.Left),FMath::Max(12.f,P.Top),
        FMath::Max(12.f,P.Right),FMath::Max(12.f,P.Bottom)));
    SBorder::Construct(Styled);
}

namespace
{
// Paints the 8-piece backdrop edge atlas (L,R,T,B,TL,TR,BL,BR; T/B stored rotated) inside G.
int32 PaintBackdropEdges(const FGeometry& G, FSlateWindowElementList& Out, int32 Top, const FWidgetStyle& Style,
    ESlateDrawEffect Effect, float EdgeSize, TFunctionRef<const FSlateBrush*(int32)> EdgeBrush)
{
    const FVector2D Size = G.GetLocalSize();
    const float E = FMath::Min(EdgeSize, float(FMath::Min(Size.X,Size.Y) / 2));
    if (E <= 0) return Top;
    auto Box = [&](int32 I, FVector2D Pos, FVector2D Extent, bool bRotate = false)
    {
        if (Extent.X <= 0 || Extent.Y <= 0) return;
        const FSlateBrush* Brush=EdgeBrush(I);
        // Explicit final rectangles keep every edge inside the frame. Rotate
        // the atlas UVs, not a tall paint rectangle around a distant pivot.
        const FBox2f Region=Brush->GetUVRegion();
        FVector2f Min=Region.bIsValid?Region.Min:FVector2f(0,0);
        FVector2f Max=Region.bIsValid?Region.Max:FVector2f(1,1);
        if (const auto* Texture=Cast<UTexture2D>(Brush->GetResourceObject()))
        {
            const FVector2f Inset(.5f/Texture->GetSizeX(),.5f/Texture->GetSizeY());
            Min+=Inset; Max-=Inset;
        }
        const FVector2D Points[4]={Pos,Pos+FVector2D(Extent.X,0),Pos+Extent,Pos+FVector2D(0,Extent.Y)};
        const FVector2f UV[4]= { bRotate?FVector2f(Min.X,Max.Y):Min,
            bRotate?Min:FVector2f(Max.X,Min.Y),
            bRotate?FVector2f(Max.X,Min.Y):Max,
            bRotate?Max:FVector2f(Min.X,Max.Y) };
        TArray<FSlateVertex> Vertices;
        const FColor Tint=Style.GetColorAndOpacityTint().ToFColor(true);
        for (int32 V=0;V<4;++V)
            Vertices.Add(FSlateVertex::Make(G.GetAccumulatedRenderTransform(),FVector2f(Points[V]),UV[V],Tint));
        const TArray<SlateIndex> Indices={0,1,2,0,2,3};
        FSlateDrawElement::MakeCustomVerts(Out,Top+1,Brush->GetRenderingResource(),Vertices,Indices,nullptr,0,0,Effect);
    };
    Box(0,{0,E},{E,Size.Y-2*E}); Box(1,{Size.X-E,E},{E,Size.Y-2*E});
    Box(2,{E,0},{Size.X-2*E,E},true); Box(3,{E,Size.Y-E},{Size.X-2*E,E},true);
    Box(4,{0,0},{E,E}); Box(5,{Size.X-E,0},{E,E});
    Box(6,{0,Size.Y-E},{E,E}); Box(7,{Size.X-E,Size.Y-E},{E,E});
    return Top+1;
}
}

int32 SWowWindowFrame::OnPaint(const FPaintArgs& Args, const FGeometry& G,
    const FSlateRect& Clip, FSlateWindowElementList& Out, int32 Layer,
    const FWidgetStyle& Style, bool bParentEnabled) const
{
    const int32 Top = SBorder::OnPaint(Args,G,Clip,Out,Layer,Style,bParentEnabled);
    if (!FWowWindowStyle::HasOriginalBorder()) return Top;
    const auto Effect = IsEnabled() && bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
    return PaintBackdropEdges(G, Out, Top, Style, Effect, 24.f, [](int32 I) { return FWowWindowStyle::Edge(I); });
}

void SWowTooltipFrame::Construct(const SBorder::FArguments& Args)
{
    SBorder::FArguments Styled = Args;
    // r305 (Stefan 13.10: the character-create Name box has round corners but a square dark
    // fill): the background is painted in OnPaint, inside the rounded edges (backdrop insets).
    Styled.BorderImage(FCoreStyle::Get().GetBrush("NoBrush"));
    Styled.BorderBackgroundColor(FWowWindowStyle::TooltipBackgroundColor());
    const auto P = Args._Padding.Get();
    // GameTooltip: 5 px backdrop insets plus 5 px text padding.
    Styled.Padding(FMargin(FMath::Max(10.f,P.Left),FMath::Max(10.f,P.Top),
        FMath::Max(10.f,P.Right),FMath::Max(10.f,P.Bottom)));
    SBorder::Construct(Styled);
}

int32 SWowTooltipFrame::OnPaint(const FPaintArgs& Args, const FGeometry& G,
    const FSlateRect& Clip, FSlateWindowElementList& Out, int32 Layer,
    const FWidgetStyle& Style, bool bParentEnabled) const
{
    // r305: the backdrop's background sits inside its 16-unit rounded edges (GameTooltip's insets
    // are 5 of 16), so no square corners show outside the curve; without edge art it fills all.
    if (const FSlateBrush* Background = FWowWindowStyle::TooltipBackground())
    {
        const bool bEdges = FWowWindowStyle::HasTooltipBorder();
        const FVector2f Size(G.GetLocalSize());
        const float Inset = bEdges ? FMath::Min(4.f, FMath::Min(Size.X, Size.Y) * .25f) : 0.f;
        FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(FMath::Max(0.f, Size.X - 2.f * Inset),
            FMath::Max(0.f, Size.Y - 2.f * Inset)), FSlateLayoutTransform(FVector2f(Inset, Inset))), Background,
            ESlateDrawEffect::None, FWowWindowStyle::TooltipBackgroundColor() * Style.GetColorAndOpacityTint());
    }
    const int32 Top = SBorder::OnPaint(Args,G,Clip,Out,Layer + 1,Style,bParentEnabled);
    if (!FWowWindowStyle::HasTooltipBorder()) return Top;
    return PaintBackdropEdges(G, Out, Top, Style, ESlateDrawEffect::None, 16.f, [](int32 I) { return FWowWindowStyle::TooltipEdge(I); });
}

FVector2D SWowAddImage::ComputeDesiredSize(float) const
{
    const FSlateBrush* Brush = Image.Get();
    return Brush ? Brush->ImageSize : FVector2D(1, 1);
}

int32 SWowAddImage::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
    const FSlateRect& CullingRect, FSlateWindowElementList& Elements,
    int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
    const FSlateBrush* Brush = Image.Get();
    if (!Brush) return Layer;
    FLinearColor Tint = Color.Get() * Style.GetColorAndOpacityTint();
    // Premultiplied blend (One, InvSrcAlpha) with the texture alpha ignored and a zero vertex
    // alpha gives dest + rgb: additive, like the original client's ADD blend mode.
    Tint.R *= Tint.A; Tint.G *= Tint.A; Tint.B *= Tint.A; Tint.A = 0.f;
    FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(), Brush,
        ESlateDrawEffect::PreMultipliedAlpha | ESlateDrawEffect::IgnoreTextureAlpha, Tint);
    return Layer;
}
