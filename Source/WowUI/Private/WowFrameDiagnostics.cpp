#include "WowFrameManager.h"
#include "WowFontManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/SWowFrameStack.h"

void FWowFrameManager::AddTransientMessage(int64 Handle, const FString& Text, const FLinearColor& Color)
{
    const auto* Entry = Frames.Find(Handle);
    auto* Canvas = Entry ? Cast<UCanvasPanel>(Entry->Widget.Get()) : nullptr;
    if (!Canvas || Text.TrimStartAndEnd().IsEmpty()) return;
    auto& State = TransientMessages.FindOrAdd(Handle);
    const double Now = FPlatformTime::Seconds();
    for (auto& Line : State.Lines)
    {
        if (Line.Text == Text && Line.Widget.IsValid())
        {
            // A held action key must not allocate a new line on every failure.
            Line.AddedAt = Now;
            Line.Widget->SetColorAndOpacity(Color);
            return;
        }
    }
    const int32 Limit = Entry->Def.Name == TEXT("QuestProgressFrame") ? 4 : 3;
    while (State.Lines.Num() >= Limit)
    {
        if (State.Lines[0].Widget.IsValid()) State.Lines[0].Widget->RemoveFromParent();
        State.Lines.RemoveAt(0);
    }
    UTextBlock* Label = NewObject<UTextBlock>(Canvas);
    Label->SetText(FText::FromString(Text));
    Label->SetColorAndOpacity(Color);
    Label->SetJustification(ETextJustify::Center);
    Label->SetAutoWrapText(true);
    Label->SetShadowOffset(FVector2D(1, 1));
    Label->SetShadowColorAndOpacity(FLinearColor::Black);
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    Canvas->AddChildToCanvas(Label);
    State.Lines.Add({Text, Label, Now});
    TickTransientMessages(Now);
}

void FWowFrameManager::SetMessageTiming(int64 Handle, double Seconds, bool bFade)
{
    if (!FMath::IsFinite(Seconds)) return;
    auto& State = TransientMessages.FindOrAdd(Handle);
    (bFade ? State.FadeSeconds : State.VisibleSeconds) = FMath::Clamp(Seconds, 0.0, 120.0);
}

void FWowFrameManager::TickTransientMessages(double Now)
{
    for (auto It = TransientMessages.CreateIterator(); It; ++It)
    {
        const auto* Frame = Frames.Find(It.Key());
        if (!Frame || !Frame->Widget.IsValid()) { It.RemoveCurrent(); continue; }
        auto& State = It.Value();
        for (int32 I = State.Lines.Num()-1; I >= 0; --I)
        {
            auto& Line = State.Lines[I];
            const double Age = FMath::Max(0.0, Now-Line.AddedAt);
            if (!Line.Widget.IsValid() || Age >= State.VisibleSeconds+State.FadeSeconds)
            {
                if (Line.Widget.IsValid()) Line.Widget->RemoveFromParent();
                State.Lines.RemoveAt(I);
                continue;
            }
            Line.Widget->SetRenderOpacity(Age <= State.VisibleSeconds ? 1.f :
                float(1.0-(Age-State.VisibleSeconds)/FMath::Max(.001, State.FadeSeconds)));
        }
        const float Width = FMath::Max(1.f, Frame->Def.Width*UIScale);
        const float Height = FMath::Max(1.f, Frame->Def.Height*UIScale);
        // Original UIErrorsFrame: ErrorFont, 16 px (Slate points are 96 DPI, so x0.75), ~18 px rows.
        const float RowHeight = FMath::Min(19.f*UIScale, Height/FMath::Max(1, State.Lines.Num()));
        const int32 FontPoints = FMath::Max(8, FMath::RoundToInt(16.f*0.75f*UIScale));
        for (int32 I=0; I<State.Lines.Num(); ++I)
        {
            auto* Label = State.Lines[I].Widget.Get();
            if (!Label) continue;
            FSlateFontInfo Font = FontManager && FontManager->IsInitialized() ?
                FontManager->GetFont(TEXT("GameFontNormalLarge"), FontPoints) :
                FCoreStyle::GetDefaultFontStyle("Bold", FontPoints);
            Font.OutlineSettings.OutlineSize=1;
            Label->SetFont(Font);
            if (auto* Slot=Cast<UCanvasPanelSlot>(Label->Slot))
            {
                Slot->SetPosition(FVector2D(0, I*RowHeight));
                Slot->SetSize(FVector2D(Width, RowHeight));
            }
        }
    }
}

TArray<int64> FWowFrameManager::GetFramesAtScreenPosition(float X, float Y) const
{
    TArray<int64> Result;
    if (!RootCanvas.IsValid() || UIScale<=0 || RawViewportScale<=0) return Result;
    FVector2D Point(X/RawViewportScale*UIScale, Y/RawViewportScale*UIScale);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        auto View = GEngine->GameViewport->GetGameViewportWidget();
        const auto& RootGeometry=RootCanvas->GetCachedGeometry();
        if (View.IsValid() && RootGeometry.GetLocalSize().X>0)
        {
            const auto Size=GEngine->GameViewport->Viewport->GetSizeXY();
            const FVector2D ViewSize=View->GetCachedGeometry().GetLocalSize();
            if (Size.X>0 && Size.Y>0)
                Point=RootGeometry.AbsoluteToLocal(View->GetCachedGeometry().LocalToAbsolute(
                    FVector2D(X/Size.X*ViewSize.X,Y/Size.Y*ViewSize.Y)));
        }
    }
    for (const auto& Pair:Frames)
    {
        const auto& E=Pair.Value;
        if (!E.Widget.IsValid() || !IsFrameEffectivelyShown(Pair.Key) || E.Def.Name.StartsWith(TEXT("WowFrameStack"))) continue;
        bool bWidgetShown=true;
        for (UWidget* W=E.Widget.Get(); W; W=W->GetParent())
        {
            if (W->GetVisibility()==ESlateVisibility::Hidden || W->GetVisibility()==ESlateVisibility::Collapsed)
            { bWidgetShown=false; break; }
        }
        if (!bWidgetShown) continue;
        FVector2D Local, Size;
        const auto& G=E.Widget->GetCachedGeometry();
        if (G.GetLocalSize().X>0 && RootCanvas->GetCachedGeometry().GetLocalSize().X>0)
        {
            Local=G.AbsoluteToLocal(RootCanvas->GetCachedGeometry().LocalToAbsolute(Point));
            Size=G.GetLocalSize();
        }
        else
        {
            auto* Slot=Cast<UCanvasPanelSlot>(E.Widget->Slot);
            if (!Slot) continue;
            Size=Slot->GetSize();FVector2D Position=FVector2D::ZeroVector;
            UWidget* W=E.Widget.Get();int32 Depth=0;
            while(W && W!=RootCanvas.Get() && Depth++<100)
            {
                if(auto* S=Cast<UCanvasPanelSlot>(W->Slot))Position+=S->GetPosition();
                Position+=W->GetRenderTransform().Translation;
                W=W->GetParent();
            }
            if(W!=RootCanvas.Get())continue;
            Local=Point-Position;
        }
        if(Size.X>0 && Size.Y>0 && Local.X>=0 && Local.Y>=0 && Local.X<=Size.X && Local.Y<=Size.Y)Result.Add(Pair.Key);
    }
    auto Order=[this](int64 H)
    {
        const auto& E=Frames.FindChecked(H);
        int32 Z=int32(E.Def.Strata)*1000+E.Def.FrameLevel;
        if(auto* Slot=Cast<UCanvasPanelSlot>(E.Widget->Slot))Z=FMath::Max(Z,Slot->GetZOrder());
        return Z;
    };
    auto Depth=[this](int64 H)
    {
        int32 D=0;while((H=GetParentHandle(H))>=0 && D<100)++D;return D;
    };
    Result.Sort([&](int64 A,int64 B)
    {
        if(Order(A)!=Order(B))return Order(A)>Order(B);
        if(Depth(A)!=Depth(B))return Depth(A)>Depth(B);
        return A>B;
    });
    return Result;
}

TArray<FWowFrameManager::FFrameStackEntry> FWowFrameManager::GetFrameStackAt(FVector2D AbsolutePosition) const
{
    TArray<FFrameStackEntry> Out;
    for (const auto& Pair:Frames)
    {
        const auto& E=Pair.Value;
        UWidget* Widget=E.Widget.Get();
        if (!Widget || !IsFrameEffectivelyShown(Pair.Key) || E.Def.Name.StartsWith(TEXT("WowFrameStack"))) continue;
        bool bShown=true;
        for (UWidget* W=Widget; W; W=W->GetParent())
        {
            const ESlateVisibility V=W->GetVisibility();
            if (V==ESlateVisibility::Hidden || V==ESlateVisibility::Collapsed || W->GetRenderOpacity()<=0.01f) { bShown=false; break; }
        }
        if (!bShown || !Widget->GetCachedWidget().IsValid()) continue;
        const FGeometry& G=Widget->GetCachedGeometry();
        const FVector2D Local=G.GetLocalSize();
        if (Local.X<=0 || Local.Y<=0) continue;
        const FVector2D A=G.LocalToAbsolute(FVector2D::ZeroVector),B=G.LocalToAbsolute(Local);
        const FVector2D Min(FMath::Min(A.X,B.X),FMath::Min(A.Y,B.Y)),Max(FMath::Max(A.X,B.X),FMath::Max(A.Y,B.Y));
        if (AbsolutePosition.X<Min.X || AbsolutePosition.Y<Min.Y || AbsolutePosition.X>Max.X || AbsolutePosition.Y>Max.Y) continue;
        FFrameStackEntry S;
        S.Handle=Pair.Key;S.Name=E.Def.Name.IsEmpty()?FString(TEXT("(anonymous)")):E.Def.Name;
        S.Parent=E.ParentHandle>=0?GetFrameName(E.ParentHandle):E.Def.Parent;
        S.Strata=E.Def.Strata;S.Level=E.Def.FrameLevel;S.AbsMin=Min;S.AbsMax=Max;
        S.Size=FVector2D(E.Def.Width,E.Def.Height);S.bMouseEnabled=E.bMouseEnabled;
        for (int64 H=E.ParentHandle; H>=0 && S.Depth<100; H=GetParentHandle(H)) ++S.Depth;
        Out.Add(MoveTemp(S));
    }
    Out.Sort([](const FFrameStackEntry& L,const FFrameStackEntry& R)
    {
        if (L.Strata!=R.Strata) return L.Strata>R.Strata;
        if (L.Level!=R.Level) return L.Level>R.Level;
        if (L.Depth!=R.Depth) return L.Depth>R.Depth;
        return L.Handle>R.Handle;
    });
    return Out;
}

void FWowFrameManager::ToggleFrameStack()
{
    bFrameStackVisible=!bFrameStackVisible;
    // The earlier text-only version was a FrameXML frame; keep it hidden if it exists.
    const int64 OldFrame=FindFrame(TEXT("WowFrameStack"));
    if (OldFrame>=0) SetFrameVisible(OldFrame,false);
    UGameViewportClient* Viewport=GEngine?GEngine->GameViewport:nullptr;
    if (bFrameStackVisible)
    {
        if (Viewport && !FrameStackOverlay.IsValid())
        {
            FrameStackOverlay=SNew(SWowFrameStack).Manager(this).ManagerAlive(FrameStackLifetime);
            Viewport->AddViewportWidgetContent(FrameStackOverlay.ToSharedRef(),100000);
        }
    }
    else if (FrameStackOverlay.IsValid())
    {
        if (Viewport) Viewport->RemoveViewportWidgetContent(FrameStackOverlay.ToSharedRef());
        FrameStackOverlay.Reset();
    }
}

void FWowFrameManager::TickFrameStack()
{
    // The /fstack overlay (SWowFrameStack) updates itself every frame.
}
