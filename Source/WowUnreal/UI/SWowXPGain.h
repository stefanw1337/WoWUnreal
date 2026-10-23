#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWowXPGain : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowXPGain) {} SLATE_END_ARGS()
    void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }
    void AddGain(uint32 Amount, bool Rested);
    void Clear() { Entries.Empty(); }
    virtual void Tick(const FGeometry&, double Time, float Delta) override;
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
        FSlateWindowElementList&, int32 Layer, const FWidgetStyle&, bool) const override;
private:
    struct FEntry { FString Text; FLinearColor Color; float Age=0; };
    TArray<FEntry> Entries;
};
