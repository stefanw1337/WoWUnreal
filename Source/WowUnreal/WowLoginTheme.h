#pragma once
#include "WowExpansionTypes.h"

// Horizontal FOV for a flat background that covers the viewport without stretching.
namespace WowLoginTheme
{
inline float BackgroundCoverFov(float HalfWidth, float HalfHeight, float Distance, float Aspect)
{
    const float HalfViewWidth = FMath::Min(HalfWidth, HalfHeight * FMath::Max(Aspect, 0.01f));
    return FMath::RadiansToDegrees(2.f * FMath::Atan(HalfViewWidth / FMath::Max(Distance, 1.f)));
}
}

// These are presentation choices only; they never select a network protocol.
namespace WowLoginTheme
{
inline const TCHAR* Model(EWowExpansion Theme)
{
    switch(Theme)
    {
    case EWowExpansion::Classic:return TEXT("Interface\\Glues\\Models\\UI_MainMenu\\UI_MainMenu.m2");
    case EWowExpansion::BurningCrusade:return TEXT("Interface\\Glues\\Models\\UI_MainMenu_BurningCrusade\\UI_MainMenu_BurningCrusade.m2");
    default:return TEXT("Interface\\Glues\\Models\\UI_MainMenu_Northrend\\UI_MainMenu_Northrend.m2");
    }
}
inline const TCHAR* Logo(EWowExpansion Theme)
{
    switch(Theme)
    {
    case EWowExpansion::Classic:return TEXT("Interface\\Glues\\Common\\Glues-WoW-Logo.blp");
    case EWowExpansion::BurningCrusade:return TEXT("Interface\\Glues\\Common\\Glues-WoW-BCLogo.blp");
    default:return TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo.blp");
    }
}
inline EWowExpansion Resolve(int32 SavedChoice,const TArray<bool>& Available)
{
    if(Available.IsValidIndex(SavedChoice) && Available[SavedChoice])return static_cast<EWowExpansion>(SavedChoice);
    for(int32 I=2;I>=0;--I)if(Available.IsValidIndex(I) && Available[I])return static_cast<EWowExpansion>(I);
    return EWowExpansion::WrathOfTheLichKing;
}
}
