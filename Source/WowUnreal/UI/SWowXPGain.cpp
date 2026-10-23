#include "SWowXPGain.h"
#include "WowColor.h"
#include "WowFontManager.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"

void SWowXPGain::AddGain(uint32 Amount, bool Rested)
{
    if (!Amount) return;
    if (Entries.Num()>=5) Entries.RemoveAt(0);
    Entries.Add({FString::Printf(TEXT("+XP %u"),Amount),
        // The XP bar's colours (MainMenuExpBar): rested blue 0,.39,.88, normal pink .58,0,.55 (Stefan 10.10:
        // the old violet was too bright).
        Rested?WowColor::FromWow(0.f, .39f, .88f):WowColor::FromWow(.58f, 0.f, .55f),0.f});
}
void SWowXPGain::Tick(const FGeometry& Geometry,double Time,float Delta)
{
    SCompoundWidget::Tick(Geometry,Time,Delta);
    for (auto& Entry:Entries) Entry.Age+=Delta;
    Entries.RemoveAll([](const FEntry& Entry) { return Entry.Age>=3.2f; });
}
int32 SWowXPGain::OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle&,bool) const
{
    auto Font=FWowFontManager::GetScrollFont(40);
    Font.OutlineSettings.OutlineSize=1;
    Font.OutlineSettings.OutlineColor=FLinearColor::Black;
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    for (int32 I=0;I<Entries.Num();++I)
    {
        const auto& Entry=Entries[I];
        const float Alpha=1.f-FMath::Clamp((Entry.Age-2.f)/1.2f,0.f,1.f);
        const FVector2D Size=Measure->Measure(Entry.Text,Font);
        const FVector2D Position((Geometry.GetLocalSize().X-Size.X)*.5,
            Geometry.GetLocalSize().Y*.38-Entry.Age*10.f-(Entries.Num()-1-I)*48.f);
        FSlateDrawElement::MakeText(Elements,Layer,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(Position+FVector2D(2,2))),
            Entry.Text,Font,ESlateDrawEffect::None,FLinearColor(0,0,0,Alpha*.8f));
        FSlateDrawElement::MakeText(Elements,Layer+1,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(Position)),
            Entry.Text,Font,ESlateDrawEffect::None,Entry.Color.CopyWithNewOpacity(Alpha));
    }
    return Layer+1;
}
