#include "WowCooldownWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"

class SWowCooldown : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowCooldown) {} SLATE_END_ARGS()
    TWeakObjectPtr<UWowCooldownWidget> Owner;
    void Construct(const FArguments&,UWowCooldownWidget* InOwner) { Owner=InOwner; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(36,36); }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,
        int32 Layer,const FWidgetStyle& Style,bool) const override
    {
        if (!Owner.IsValid() || Owner->Fraction<=0) return Layer;
        // r284 (Stefan 13.10, immersive interface: rows of "0.4" stayed when the bars had faded out):
        // the sweep and the countdown follow the opacity inherited from the button and its bar.
        const float Inherited=Style.GetColorAndOpacityTint().A;
        if (Inherited<=.01f) return Layer;
        const float F=FMath::Clamp(Owner->Fraction,0.f,1.f);
        const FVector2f Size(G.GetLocalSize()),Center=Size*.5f;
        TArray<FSlateVertex> Vertices; TArray<SlateIndex> Indices;
        const FColor Color=FLinearColor(.08f,.08f,.08f,(.35f+.4f*F)*Inherited).ToFColor(true);
        const auto Transform=G.GetAccumulatedRenderTransform();
        auto Add=[&](FVector2f P){Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,P,FVector2f::ZeroVector,Color));};
        Add(Center);
        const float Start=(1.f-F)*2.f*PI;
        // Keep square corners in the fan so the entire unavailable icon is covered.
        TArray<float> Angles; Angles.Add(Start);
        for(int32 I=0;I<=64;++I) {float A=I*2.f*PI/64.f;if(A>Start) Angles.Add(A);}
        for(float A:Angles)
        {
            const FVector2f D(FMath::Sin(A),-FMath::Cos(A));
            const float Scale=1.f/FMath::Max(FMath::Abs(D.X),FMath::Abs(D.Y));
            Add(Center+D*Center*Scale);
        }
        for(int32 I=1;I<Vertices.Num()-1;++I){Indices.Add(0);Indices.Add(I);Indices.Add(I+1);}
        auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
        FSlateDrawElement::MakeCustomVerts(Out,Layer,Resource,Vertices,Indices,nullptr,0,0);
        if (!Owner->Countdown.IsEmpty())
        {
            // r411 (Stefan 17.10: the countdown was hard to read over bright icons): a black outline
            // around the white digits, kept under the 1 px drop shadow.
            auto Font=FCoreStyle::GetDefaultFontStyle("Bold",FMath::Clamp(int32(Size.Y*.32f),9,16));
            Font.OutlineSettings.OutlineSize=1;
            Font.OutlineSettings.OutlineColor=FLinearColor(0.f,0.f,0.f,Inherited);
            const FVector2D Extent=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Owner->Countdown,Font);
            const FVector2D P=(G.GetLocalSize()-Extent)*.5;
            FSlateDrawElement::MakeText(Out,Layer+1,G.ToPaintGeometry(Extent,FSlateLayoutTransform(P+FVector2D(1,1))),Owner->Countdown,Font,ESlateDrawEffect::None,FLinearColor(0,0,0,Inherited));
            FSlateDrawElement::MakeText(Out,Layer+2,G.ToPaintGeometry(Extent,FSlateLayoutTransform(P)),Owner->Countdown,Font,ESlateDrawEffect::None,FLinearColor(1,1,1,Inherited));
        }
        return Layer+2;
    }
};
TSharedRef<SWidget> UWowCooldownWidget::RebuildWidget() {return SNew(SWowCooldown,this);}
