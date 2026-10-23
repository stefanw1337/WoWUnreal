#pragma once
#include "CoreMinimal.h"

// Input is escaped, flat Slate rich text produced by the quest dialog parser.
// Tags and entities must never be displayed in a half-written state.
struct FWowQuestTextReveal
{
    FString Markup;
    TArray<int32> WordEnds;

    explicit FWowQuestTextReveal(FString InMarkup = FString()) : Markup(MoveTemp(InMarkup))
    {
        bool bWord = false;
        for (int32 I=0; I<Markup.Len(); ++I)
        {
            if (Markup[I]==TEXT('<'))
            {
                const int32 End=Markup.Find(TEXT(">"),ESearchCase::CaseSensitive,ESearchDir::FromStart,I+1);
                if (End!=INDEX_NONE) { I=End; continue; }
            }
            if (FChar::IsWhitespace(Markup[I]))
            {
                if (bWord) WordEnds.Add(I);
                bWord=false;
            }
            else bWord=true;
        }
        if (bWord) WordEnds.Add(Markup.Len());
    }
    double Duration() const { return FMath::Min(20.0,WordEnds.Num()/12.0); }
    int32 VisibleWords(double Elapsed, bool bInstant) const
    {
        if (bInstant) return WordEnds.Num();
        if (Elapsed<=0 || WordEnds.IsEmpty()) return 0;
        const double Rate=FMath::Max(12.0,WordEnds.Num()/20.0);
        return FMath::Clamp(FMath::FloorToInt(Elapsed*Rate),0,WordEnds.Num());
    }
    FString Prefix(int32 Words) const
    {
        if (Words<=0) return FString();
        if (Words>=WordEnds.Num()) return Markup;
        FString Result=Markup.Left(WordEnds[Words-1]);
        bool bOpen=false;
        for (int32 I=0; I<Result.Len(); ++I)
        {
            if (Result[I]!=TEXT('<')) continue;
            const int32 End=Result.Find(TEXT(">"),ESearchCase::CaseSensitive,ESearchDir::FromStart,I+1);
            if (End==INDEX_NONE) break;
            bOpen=Result[I+1]!=TEXT('/');
            I=End;
        }
        if (bOpen) Result+=TEXT("</>");
        return Result;
    }
};
