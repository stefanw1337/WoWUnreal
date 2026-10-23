#pragma once
#include "CoreMinimal.h"
namespace WowChatFade
{
inline float Opacity(double Now,double Added,double LastActivity,bool Hovered)
{
    if(Hovered)return 1.0f;
    const double Age=FMath::Max(0.0,Now-FMath::Max(Added,LastActivity));
    return (float)FMath::Clamp(1.0-(Age-12.0)/3.0,0.0,1.0);
}
}
