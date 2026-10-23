#include "WowFrameLayout.h"
#include "Misc/ConfigCacheIni.h"
#include "WowClientPreferences.h"

namespace
{
    // Frame positions per interface preset: the headset has its own (Stefan 09.10).
    const TCHAR* SectionFor(bool bVr){return bVr?TEXT("WowUnreal.FrameLayoutVR"):TEXT("WowUnreal.FrameLayout");}
    const TCHAR* Section=SectionFor(false);
    FString LoadedIni;
    bool bLoadedVr=false;
    TMap<FString,FVector2D> SavedPositions;
    void Load()
    {
        const bool bVr=FWowClientPreferences::IsVrUiMode();
        if(LoadedIni==GGameUserSettingsIni&&bLoadedVr==bVr)return;
        LoadedIni=GGameUserSettingsIni;bLoadedVr=bVr;Section=SectionFor(bVr);SavedPositions.Reset();
        if(!GConfig)return;
        TArray<FString> Lines;GConfig->GetSection(Section,Lines,LoadedIni);
        for(const FString& Line:Lines)
        {
            FString Name,Value,X,Y;
            if(!Line.Split(TEXT("="),&Name,&Value)||!Value.Split(TEXT(","),&X,&Y))continue;
            double PX=0,PY=0;
            if(!LexTryParseString(PX,*X)||!LexTryParseString(PY,*Y)||!FMath::IsFinite(PX)||!FMath::IsFinite(PY))continue;
            SavedPositions.Add(Name,FVector2D(FMath::Clamp(PX,0.,1.),FMath::Clamp(PY,0.,1.)));
        }
    }
}
const TMap<FString,FVector2D>& FWowFrameLayout::Positions(){Load();return SavedPositions;}
void FWowFrameLayout::Set(const FString& Name,FVector2D P)
{
    Load();if(Name.IsEmpty()||Name.Contains(TEXT("="))||Name.Contains(TEXT("\n"))||!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y))return;
    SavedPositions.Add(Name,FVector2D(FMath::Clamp(P.X,0.,1.),FMath::Clamp(P.Y,0.,1.)));
}
void FWowFrameLayout::Save()
{
    Load();if(!GConfig||LoadedIni.IsEmpty())return;
    GConfig->EmptySection(Section,LoadedIni);
    for(const auto& Pair:SavedPositions)
        GConfig->SetString(Section,*Pair.Key,*FString::Printf(TEXT("%.8f,%.8f"),Pair.Value.X,Pair.Value.Y),LoadedIni);
    GConfig->Flush(false,LoadedIni);
}
void FWowFrameLayout::Reset(){Load();SavedPositions.Reset();Save();}
FVector2D FWowFrameLayout::ClampPosition(FVector2D P,FVector2D Size,FVector2D View)
{
    return FVector2D(FMath::Clamp(P.X*View.X,0.,FMath::Max(0.,View.X-Size.X)),
        FMath::Clamp(P.Y*View.Y,0.,FMath::Max(0.,View.Y-Size.Y)));
}
