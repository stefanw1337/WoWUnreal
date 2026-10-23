#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "WowCooldownWidget.generated.h"

UCLASS()
class WOWUI_API UWowCooldownWidget : public UWidget
{
    GENERATED_BODY()
public:
    float Fraction=0;
    FString Countdown;
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
