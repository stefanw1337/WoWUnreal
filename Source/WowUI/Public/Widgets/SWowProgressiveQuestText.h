#pragma once
#include "WowQuestTextReveal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Styling/SlateStyle.h"
#include "Framework/Application/SlateApplication.h"

class SWowProgressiveQuestText : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowProgressiveQuestText) : _Instant(true), _Delay(0) {}
        SLATE_ATTRIBUTE(bool, Instant)
        SLATE_ARGUMENT(double, Delay)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args, FString Markup, TSharedRef<FSlateStyleSet> Styles, FName DefaultStyle)
    {
        Text=FWowQuestTextReveal(MoveTemp(Markup));
        StyleOwner=Styles;
        Instant=Args._Instant;
        Delay=Args._Delay;
        Started=FSlateApplication::Get().GetCurrentTime();
        auto Full=SNew(SRichTextBlock).Text(FText::FromString(Text.Markup))
            .TextStyle(&Styles->GetWidgetStyle<FTextBlockStyle>(DefaultStyle))
            .DecoratorStyleSet(&Styles.Get()).AutoWrapText(true);
        Full->SetRenderOpacity(0);
        // Reserve the complete paragraph's layout. Rewards, links and buttons
        // must not move under the cursor as words appear above them.
        ChildSlot[SNew(SOverlay)
            + SOverlay::Slot()[Full]
            + SOverlay::Slot().VAlign(VAlign_Top)
            [SAssignNew(Revealed,SRichTextBlock)
                .TextStyle(&Styles->GetWidgetStyle<FTextBlockStyle>(DefaultStyle))
                .DecoratorStyleSet(&Styles.Get()).AutoWrapText(true)]];
        Update(0);
    }
    virtual void Tick(const FGeometry& G, double Time, float Delta) override
    {
        SCompoundWidget::Tick(G,Time,Delta);
        Update(Time-Started);
    }
    int32 GetVisibleWordCount() const { return Visible; }
private:
    void Update(double Elapsed)
    {
        if (bFinished) return;
        const int32 Next=Text.VisibleWords(Elapsed-Delay,Instant.Get());
        if (Next!=Visible)
        {
            Visible=Next;
            Revealed->SetText(FText::FromString(Text.Prefix(Visible)));
        }
        bFinished=Visible==Text.WordEnds.Num();
    }
    FWowQuestTextReveal Text;
    TSharedPtr<FSlateStyleSet> StyleOwner;
    TSharedPtr<SRichTextBlock> Revealed;
    TAttribute<bool> Instant;
    double Started=0,Delay=0;
    int32 Visible=INDEX_NONE;
    bool bFinished=false;
};
