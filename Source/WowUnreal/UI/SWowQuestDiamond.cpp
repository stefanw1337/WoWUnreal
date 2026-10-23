#include "SWowQuestDiamond.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Rendering/DrawElements.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "WowUiVrHost.h"
#include "Engine/Texture2D.h"

namespace
{
void QuestGemTri(FSlateWindowElementList& Elements, int32 Layer, const FSlateRenderTransform& RT,
    FVector2D A, FVector2D B, FVector2D C, const FLinearColor& Color)
{
    const FColor Col = Color.ToFColor(true);
    TArray<FSlateVertex> V = {
        FSlateVertex::Make(RT, FVector2f(A), FVector2f::ZeroVector, Col),
        FSlateVertex::Make(RT, FVector2f(B), FVector2f::ZeroVector, Col),
        FSlateVertex::Make(RT, FVector2f(C), FVector2f::ZeroVector, Col)};
    TArray<SlateIndex> I = {0, 1, 2};
    FSlateDrawElement::MakeCustomVerts(Elements, Layer, FSlateResourceHandle(), V, I, nullptr, 0, 0, ESlateDrawEffect::None);
}

// A faceted, slowly turning gem: a six-sided bipyramid, taller than wide, with the girdle a
// little above the middle. Each visible facet is shaded by its angle to a light from the
// upper left, upper facets brighter than lower ones, so it reads as a 3D crystal.
void QuestGem(FSlateWindowElementList& Elements, int32 Layer, const FSlateRenderTransform& RT,
    FVector2D Center, float HalfHeight, float Radius, double Time, const FLinearColor& Color, const FLinearColor& OutlineColor)
{
    constexpr int32 Sides = 6;
    const float Angle = static_cast<float>(FMath::Fmod(Time * 0.9, 2.0 * PI));
    const FVector2D Top = Center + FVector2D(0, -HalfHeight);
    const FVector2D Bottom = Center + FVector2D(0, HalfHeight);
    const float GirdleY = Center.Y - HalfHeight * 0.22f;
    FVector2D Girdle[Sides];
    float Depth[Sides];
    for (int32 I = 0; I < Sides; ++I)
    {
        const float A = Angle + I * 2.f * PI / Sides;
        Girdle[I] = FVector2D(Center.X + Radius * FMath::Cos(A), GirdleY);
        Depth[I] = FMath::Sin(A);
    }
    // Black silhouette first (a little larger), so the gem stands out on bright ground.
    const float Pad = 2.f;
    const FVector2D OTop = Top + FVector2D(0, -Pad * 1.5f), OBottom = Bottom + FVector2D(0, Pad * 1.5f);
    const FVector2D OLeft(Center.X - Radius - Pad, GirdleY), ORight(Center.X + Radius + Pad, GirdleY);
    QuestGemTri(Elements, Layer, RT, OTop, OLeft, ORight, OutlineColor);
    QuestGemTri(Elements, Layer, RT, OBottom, OLeft, ORight, OutlineColor);
    // Front facets, far ones first.
    TArray<int32> Order;
    for (int32 I = 0; I < Sides; ++I)
        if (Depth[I] + Depth[(I + 1) % Sides] > -0.05f) Order.Add(I);
    Order.Sort([&Depth](int32 A, int32 B) { return Depth[A] + Depth[(A + 1) % Sides] < Depth[B] + Depth[(B + 1) % Sides]; });
    const float LightAngle = PI * 0.5f + 0.75f; // front, from the left
    for (int32 I : Order)
    {
        const int32 J = (I + 1) % Sides;
        const float Mid = Angle + (I + 0.5f) * 2.f * PI / Sides;
        const float Lambert = 0.5f + 0.5f * FMath::Cos(Mid - LightAngle);
        auto Shade = [&Color](float K) { return FLinearColor(Color.R * K, Color.G * K, Color.B * K, Color.A); };
        QuestGemTri(Elements, Layer + 1, RT, Top, Girdle[I], Girdle[J], Shade(0.55f + 0.6f * Lambert));
        QuestGemTri(Elements, Layer + 1, RT, Bottom, Girdle[I], Girdle[J], Shade(0.35f + 0.45f * Lambert));
    }
    // Small glint near the top.
    QuestGemTri(Elements, Layer + 2, RT, Top + FVector2D(0, 2.f), Top + FVector2D(-Radius * 0.35f, HalfHeight * 0.55f),
        Top + FVector2D(-Radius * 0.1f, HalfHeight * 0.6f), WowColor::FromWow(1.f, 1.f, 0.9f, 0.55f));
}

void Triangle(FSlateWindowElementList& Elements, int32 Layer, const FSlateRenderTransform& RT,
    FVector2D Tip, FVector2D Dir, float Len, float Half, const FLinearColor& Color)
{
    const FColor C = Color.ToFColor(true);
    const FVector2D Side(-Dir.Y, Dir.X), Base = Tip - Dir * Len;
    TArray<FSlateVertex> V = {
        FSlateVertex::Make(RT, FVector2f(Tip), FVector2f::ZeroVector, C),
        FSlateVertex::Make(RT, FVector2f(Base + Side * Half), FVector2f::ZeroVector, C),
        FSlateVertex::Make(RT, FVector2f(Base - Side * Half), FVector2f::ZeroVector, C)};
    TArray<SlateIndex> I = {0, 1, 2};
    FSlateDrawElement::MakeCustomVerts(Elements, Layer, FSlateResourceHandle(), V, I, nullptr, 0, 0, ESlateDrawEffect::None);
}
}

// r255 (Stefan 12.10: "a skull as a diamond in the world" where you died).
static void SetCorpseBrush(TStrongObjectPtr<UTexture2D>& Holder, FSlateBrush& Brush, UTexture2D* Icon)
{
    Holder.Reset(Icon);
    Brush = FSlateBrush();
    if (!Icon) return;
    Brush.SetResourceObject(Icon);
    Brush.DrawAs = ESlateBrushDrawType::Image;
    Brush.ImageSize = FVector2D(Icon->GetSizeX(), Icon->GetSizeY());
}
void SWowQuestDiamond::SetCorpseIcon(UTexture2D* Icon) { SetCorpseBrush(CorpseIcon, CorpseBrush, Icon); }
void SWowQuestDiamondCard::SetCorpseIcon(UTexture2D* Icon) { SetCorpseBrush(CorpseIcon, CorpseBrush, Icon); }

void SWowQuestDiamond::Construct(const FArguments&, APlayerController* InController, FTargetFunction InTarget)
{
    Controller = InController;
    Target = MoveTemp(InTarget);
    SetVisibility(EVisibility::HitTestInvisible);
}

int32 SWowQuestDiamond::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
    FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const
{
    APlayerController* PC = Controller.Get();
    TArray<FTarget> Targets;
    if (!PC || !PC->PlayerCameraManager || !Target) return Layer;
    // In the headset the markers are world cards (SWowQuestDiamondCard), not this flat layer.
    if (FWowUiVrHost::IsActive()) return Layer;
    Target(Targets);
    int32 ViewW = 0, ViewH = 0;
    PC->GetViewportSize(ViewW, ViewH);
    const FVector2D Local = Geometry.GetLocalSize();
    if (Targets.IsEmpty() || ViewW <= 0 || ViewH <= 0 || Local.X <= 0 || Local.Y <= 0) return Layer;
    const FVector2D PixelToLocal(Local.X / ViewW, Local.Y / ViewH);
    const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
    const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
    const FSlateRenderTransform& RT = Geometry.GetAccumulatedRenderTransform();
    const FLinearColor Gold = WowColor::FromWow(1.f, .82f, 0.f, 1.f), Outline(0.f, 0.f, 0.f, .85f);
    const FSlateFontInfo Font = FWowWindowStyle::TitleFont(11);
    const FSlateFontInfo LabelFont = FWowWindowStyle::TitleFont(10);
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    auto Text = [&](const FString& String, const FSlateFontInfo& F, FVector2D Center, float Y, const FLinearColor& Color, int32 TextLayer)
    {
        const FVector2D Size = Measure->Measure(String, F);
        const FVector2D Pos = Center + FVector2D(-Size.X * .5f, Y);
        FSlateDrawElement::MakeText(Elements, TextLayer, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos + FVector2D(1, 1)))),
            String, F, ESlateDrawEffect::None, FLinearColor::Black);
        FSlateDrawElement::MakeText(Elements, TextLayer + 1, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))),
            String, F, ESlateDrawEffect::None, Color);
    };
    // Farthest first, so nearer markers draw on top.
    Targets.Sort([](const FTarget& A, const FTarget& B) { return A.Yards > B.Yards; });
    int32 L = Layer;
    for (const FTarget& T : Targets)
    {
        // Camera-space direction decides on/off screen; behind the camera always counts as off screen.
        const FVector InCam = CamRot.UnrotateVector(T.World - CamLoc);
        FVector2D Screen;
        const float Margin = 34.f;
        bool bOnScreen = InCam.X > 0.f && PC->ProjectWorldLocationToScreen(T.World, Screen, false);
        FVector2D Pos = Screen * PixelToLocal;
        if (bOnScreen && (Pos.X < Margin || Pos.Y < Margin || Pos.X > Local.X - Margin || Pos.Y > Local.Y - Margin))
            bOnScreen = false;
        FVector2D Dir(InCam.Y, -InCam.Z);
        if (!bOnScreen)
        {
            if (Dir.IsNearlyZero()) Dir = FVector2D(0, 1);
            Dir.Normalize();
            // Push from the centre along the direction until the margin box.
            const FVector2D Half = Local * .5f - FVector2D(Margin, Margin);
            const float Step = FMath::Min(FMath::Abs(Dir.X) > KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Dir.X) : BIG_NUMBER,
                                          FMath::Abs(Dir.Y) > KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Dir.Y) : BIG_NUMBER);
            Pos = Local * .5f + Dir * Step;
        }
        const FLinearColor Mark = T.bCorpse ? FLinearColor(.85f, .85f, .85f, 1.f) : Gold;
        if (T.bCorpse && CorpseIcon.IsValid())
            FSlateDrawElement::MakeBox(Elements, L + 1, Geometry.ToPaintGeometry(FVector2f(30.f, 30.f), FSlateLayoutTransform(FVector2f(Pos - FVector2D(15.f, 15.f)))),
                &CorpseBrush, ESlateDrawEffect::None, FLinearColor::White);
        else QuestGem(Elements, L, RT, Pos, 15.f, 8.f, FPlatformTime::Seconds(), Mark, Outline);
        if (!bOnScreen)
        {
            Triangle(Elements, L, RT, Pos + Dir * 25.f, Dir, 10.f, 8.f, Outline);
            Triangle(Elements, L + 1, RT, Pos + Dir * 23.f, Dir, 8.f, 6.f, Mark);
        }
        if (!T.Label.IsEmpty()) Text(T.Label, LabelFont, Pos, -36.f, Mark, L + 3);
        Text(FString::Printf(TEXT("%d yds"), FMath::RoundToInt(T.Yards)), Font, Pos, 19.f, FLinearColor::White, L + 3);
        L += 5;
    }
    return L;
}

int32 SWowQuestDiamondCard::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
    FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const
{
    const FSlateRenderTransform& RT = Geometry.GetAccumulatedRenderTransform();
    const FLinearColor Gold = WowColor::FromWow(1.f, .82f, 0.f, 1.f), Outline(0.f, 0.f, 0.f, .85f);
    // r247 (Stefan 12.10: the diamonds and their text were hard to see in the headset): the gem and
    // both lines about 1.8x larger on a 440 x 220 card.
    const FSlateFontInfo Font = FWowWindowStyle::TitleFont(25);
    const FSlateFontInfo LabelFont = FWowWindowStyle::TitleFont(23);
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const FVector2D Center = Geometry.GetLocalSize() * .5f + FVector2D(0.f, 4.f);
    auto Text = [&](const FString& String, const FSlateFontInfo& F, float Y, const FLinearColor& Color)
    {
        const FVector2D Size = Measure->Measure(String, F);
        const FVector2D Pos = Center + FVector2D(-Size.X * .5f, Y);
        FSlateDrawElement::MakeText(Elements, Layer + 3, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos + FVector2D(1, 1)))),
            String, F, ESlateDrawEffect::None, FLinearColor::Black);
        FSlateDrawElement::MakeText(Elements, Layer + 4, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))),
            String, F, ESlateDrawEffect::None, Color);
    };
    if (CorpseIcon.IsValid())
        FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(FVector2f(72.f, 72.f), FSlateLayoutTransform(FVector2f(Center - FVector2D(36.f, 36.f)))),
            &CorpseBrush, ESlateDrawEffect::None, FLinearColor::White);
    else QuestGem(Elements, Layer, RT, Center, 36.f, 20.f, FPlatformTime::Seconds(), Gold, Outline);
    if (!Label.IsEmpty()) Text(Label, LabelFont, -84.f, CorpseIcon.IsValid() ? FLinearColor(.85f, .85f, .85f, 1.f) : Gold);
    Text(FString::Printf(TEXT("%d yds"), FMath::RoundToInt(Yards)), Font, 44.f, FLinearColor::White);
    return Layer + 5;
}
