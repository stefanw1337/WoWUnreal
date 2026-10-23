#include "SWowWeatherLens.h"
#include "Camera/PlayerCameraManager.h"
namespace { constexpr int32 MaxLensDrops=48; }
#include "WowClientPreferences.h"
#include "WowSkyManager.h"
#include "WowAudioManager.h"
#include "../WowPlayerCharacter.h"
#include "WowM2ParticleComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

// Shared frost level for frame frost painted elsewhere (r233); one lens per game window.
static float GWowLensFrost=0.f;

void SWowWeatherLens::Construct(const FArguments& Args)
{
    World=Args._World; Minimap=Args._Minimap;
    Trails.Reserve(384);
    SetVisibility(EVisibility::HitTestInvisible);
    Drops.Reserve(32);
    SetCanTick(true);
}

void SWowWeatherLens::Tick(const FGeometry&, double Now, float Delta)
{
    if (!World.IsValid()) { Drops.Reset(); WasUnderwater=false; Frost=0; return; }
    const bool WeatherLens=FWowClientPreferences::GetWorldOption(TEXT("weatherlens"));
    const bool WetLens=FWowClientPreferences::GetWorldOption(TEXT("wetlens"));
    const bool FrostLens=FWowClientPreferences::GetWorldOption(TEXT("frostlens"));
    if ((!Weather.IsValid() || !Audio.IsValid()) && Now >= NextFind)
    {
        NextFind=Now+1;
        if (!Weather.IsValid())
            for (TActorIterator<AWowSkyManager> It(World.Get()); It; ++It)
                if (auto* Component=It->FindComponentByClass<UWowWeatherComponent>()) { Weather=Component; break; }
        if (!Audio.IsValid())
            for (TActorIterator<AWowAudioManager> It(World.Get()); It; ++It) { Audio=*It; break; }
    }
    const auto* ViewController = World->GetFirstPlayerController();
    const auto* Player = ViewController ? Cast<AWowPlayerCharacter>(ViewController->GetPawn()) : nullptr;
    const bool Underwater = Player && Player->IsViewUnderWater();
    // Keep observing while disabled, so turning the option on does not invent an exit.
    const bool Emerged=Player && WasUnderwater && !Underwater;
    WasUnderwater=Underwater;
    Drops.RemoveAll([WeatherLens,WetLens](const FDrop& D) { return D.FromWater ? !WetLens : !WeatherLens; });
    const float Dt=FMath::Clamp(Delta,0.f,.1f);
    if ((WeatherLens || FrostLens) && Now>=NextHeat) {
        NextHeat=Now+.5; NearbyHeat=0;
        if(auto* PC=World->GetFirstPlayerController()) {
            FVector Camera; FRotator Rotation; PC->GetPlayerViewPoint(Camera,Rotation);
            NearbyHeat=UWowM2ParticleComponent::GetNearbyFireHeat(World.Get(),Camera,PC->GetPawn());
        }
    }
    const bool Sheltered=Weather.IsValid() && Weather->GetCameraExposure()<.15f;
    const bool Cold=Weather.IsValid() && (Weather->IsColdClimate() || Weather->GetWeather().Kind==EWowWeatherKind::Snow);
    const float Warmth=FMath::Max(NearbyHeat*2.f,(Sheltered || !Cold)?1.f:0.f);
    Trails.RemoveAll([WeatherLens,WetLens](const FTrail& T){return T.FromWater?!WetLens:!WeatherLens;});
    for(int32 I=Trails.Num()-1;I>=0;--I) { Trails[I].Age+=Dt; if(Trails[I].Age>=Trails[I].Life) Trails.RemoveAtSwap(I); }
    float TargetFrost=0;
    if (FrostLens && !Underwater && Weather.IsValid())
    {
        const auto& Sample=Weather->GetWeather();
        // Blizzard pushes it all the way to the deep-frost stages (r232).
        const float Snow=Sample.Kind==EWowWeatherKind::Snow ? FMath::Clamp(Sample.Intensity*(1.f+Weather->GetBlizzardStrength()),0.f,1.f) : 0.f;
        const float ClimateFrost=Weather->IsColdClimate() ? .28f : 0.f;
        TargetFrost=FMath::Max(ClimateFrost,Snow)*Weather->GetCameraExposure()*(1.f-FMath::Clamp(NearbyHeat*2.f,0.f,1.f));
    }
    // Independent opt-in: frost also works with rain/snow droplets disabled.
    if (!FrostLens) Frost=0;
    else Frost=FMath::FInterpConstantTo(Frost,TargetFrost,Dt,
        // r486 (Stefan: "the minimap gets frozen too fast, we need a timer for like 10 minutes"): the frost
        // reaches its target in ~10 minutes of cold exposure, whatever the target (it took 45 s in a
        // blizzard). The minimap rim rimes over the first minutes; the frosted glass covers it only
        // after ~9 minutes in a blizzard. It thaws quickly in water or by a fire; elsewhere (a roof, a
        // tree overhead, the snow easing) over ~5 minutes, so a short stop under a tree does not reset it.
        TargetFrost>Frost ? FMath::Max(TargetFrost,.1f)/600.f : (Underwater?.3f:NearbyHeat>.1f?.05f:1.f/300.f));
    GWowLensFrost=Frost;
    if (Underwater || (!WeatherLens && !WetLens))
    {
        Drops.Reset(); Trails.Reset(); SpawnBudget=0;
        Invalidate(EInvalidateWidgetReason::Paint);
        return;
    }
    if (WetLens && Emerged)
        for (int32 I=0; I<12; ++I) AddDrop(false,true);
    for (int32 I=Drops.Num()-1; I>=0; --I)
    {
        auto& D=Drops[I]; D.Age+=Dt;
        if(D.Ice>.001f) {
            if(D.Age>D.Hold) D.Ice=FMath::Max(0.f,D.Ice-Dt*FMath::Max(.18f,Warmth)*D.MeltRate);
            if(D.Ice>.001f) continue;
            D.Radius*=D.MeltSize; D.Age=.3f; D.Snow=false; D.Hold=0; D.LastTrail=D.Position;
        }
        if(D.Age>D.Hold) {
            const float Speed=D.Speed*FMath::Clamp((D.Age-D.Hold)*1.5f,.15f,1.7f);
            D.Velocity=FVector2D(Speed*(D.Drift+.23f*FMath::Sin(D.Age*1.4f+D.Phase)+.10f*FMath::Sin(D.Age*3.1f+D.Phase)),Speed);
            const FVector2D Step=D.Velocity*Dt;
            D.Position+=Step;
            D.Water=FMath::Max(0.f,D.Water-float(Step.Size())*(1.1f+D.TrailStrength*.5f)-Dt*.025f);
            D.TrailClock+=Dt;
            if(D.TrailClock>.12f && D.TrailStrength>.1f) {
                if(Trails.Num()>=384) Trails.RemoveAt(0);
                FTrail T; T.A=D.LastTrail; T.B=D.Position; T.Life=3.f+D.TrailStrength*4.f;
                T.Width=FMath::Max(1.f,D.Radius*.26f*FMath::Sqrt(D.Water)); T.Alpha=.045f+D.TrailStrength*.075f; T.FromWater=D.FromWater;
                Trails.Add(T); D.LastTrail=D.Position; D.TrailClock=0;
            }
        }
        if(D.Age>=D.Life || D.Water<.04f || D.Position.Y>1.06f || D.Position.X<-.06f || D.Position.X>1.06f) Drops.RemoveAtSwap(I);
    }
    if (WeatherLens && Weather.IsValid())
    {
        const auto& Sample=Weather->GetWeather();
        const bool Snow=Sample.Kind==EWowWeatherKind::Snow;
        const bool Rain=Sample.Kind==EWowWeatherKind::Rain || Sample.Kind==EWowWeatherKind::Storm;
        const float Exposure=Weather->GetCameraExposure();
        // First person is the character's own eyes, not a camera: no lens drops there.
        bool bFirstPerson=false;
        if (APlayerController* PC=World->GetFirstPlayerController(); PC && PC->PlayerCameraManager && PC->GetPawn())
            bFirstPerson=FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(),PC->GetPawn()->GetPawnViewLocation())<60.f;
        if (bFirstPerson) for (auto& D : Drops) D.Life=FMath::Min(D.Life,D.Age+0.25f); // clear quickly
        if ((Snow || Rain) && Exposure>.15f && !bFirstPerson)
        {
            // Looking up into the rain catches far more drops than looking at the ground.
            float Pitch=0.f;
            if (APlayerController* PC=World->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
                Pitch=PC->PlayerCameraManager->GetCameraRotation().Pitch;
            LensFacingUp=FMath::Clamp(Pitch/55.f,0.f,1.f);
            const float Facing=Pitch>=0.f ? 1.f+2.6f*LensFacingUp : FMath::Lerp(1.f,.35f,FMath::Clamp(-Pitch/45.f,0.f,1.f));
            SpawnBudget+=Dt*Sample.Intensity*Exposure*(Snow?2.0f:5.0f)*Facing;
            if (SpawnBudget>=1 && Drops.Num()<MaxLensDrops)
            {
                SpawnBudget=0;
                AddDrop(Snow,false);
            }
        }
        else SpawnBudget=0;
    }
    if (!WeatherLens) SpawnBudget=0;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SWowWeatherLens::AddDrop(bool Snow, bool FromWater)
{
    // Water exits replace old residue if the weather has filled the lens budget.
    if (Drops.Num()>=MaxLensDrops) { if (!FromWater) return; Drops.RemoveAt(0); }
    FDrop Drop;
    Drop.Hold=Snow?Random.FRandRange(3.f,6.f):Random.FRandRange(.3f,2.4f);
    Drop.Phase=Random.FRandRange(0,2*PI); Drop.Speed=Random.FRandRange(.022f,.065f); Drop.Drift=Random.FRandRange(-.24f,.24f);
    Drop.Ice=Snow?1.f:0.f;
    Drop.MeltRate=Random.FRandRange(.22f,.65f);
    Drop.MeltSize=Random.FRand()<.45f ? Random.FRandRange(.65f,.8f) : Random.FRandRange(.28f,.48f);
    Drop.TrailStrength=FromWater?.7f:Weather.IsValid()?FMath::Clamp(Weather->GetWeather().Intensity,0.f,1.f):.4f;
    Drop.Position=FVector2D(Random.FRandRange(.02f,.98f),Random.FRandRange(.02f,.82f));
    // Keep the centre clear while looking ahead; looking up, drops land anywhere.
    if (LensFacingUp<.5f && Drop.Position.X>.35f && Drop.Position.X<.65f && Drop.Position.Y>.25f && Drop.Position.Y<.75f)
        Drop.Position.X=Random.FRand()<.5f ? Random.FRandRange(.02f,.25f) : Random.FRandRange(.75f,.98f);
    Drop.Snow=Snow; Drop.FromWater=FromWater;
    Drop.Life=FromWater ? Random.FRandRange(4.f,9.f) : Random.FRandRange(5.f,8.f);
    Drop.Radius=Random.FRandRange(4,14); Drop.LastTrail=Drop.Position;
    Drops.Add(Drop);
}

int32 SWowWeatherLens::OnPaint(const FPaintArgs&, const FGeometry& Geo, const FSlateRect&,
    FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
    PaintFrost(Geo,Out,Layer);

    const FVector2D Screen=Geo.GetLocalSize();
    for(const auto& T : Trails) {
        const TArray<FVector2D> Points{T.A*Screen,T.B*Screen};
        const float Fade=FMath::Square(FMath::Clamp(1.f-T.Age/T.Life,0.f,1.f));
        FSlateDrawElement::MakeLines(Out,Layer,Geo.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.48f,.65f,.76f,T.Alpha*Fade),true,T.Width);
    }
    for (const auto& D : Drops)
    {
        float Fade=FMath::Clamp(D.Age/.3f,0.f,1.f)*(D.Ice>.001f?1.f:FMath::Clamp((D.Life-D.Age)/1.2f,0.f,1.f))*FMath::Clamp(D.Water*4.f,0.f,1.f);
        const FVector2D P=D.Position*Screen;
        float R=D.Radius*FMath::Sqrt(D.Water);
        if(D.Ice>.001f) {
            // Water gradually appears only as the remaining crystal melts away.
            for(int32 Arm=0;Arm<6;++Arm) {
                const float A=Arm*PI/3+D.Phase;
                const FVector2D Dir(FMath::Cos(A),FMath::Sin(A)), Side(-Dir.Y,Dir.X);
                const float Length=R*(.8f+.2f*FMath::Sin(D.Phase+Arm))*FMath::Lerp(D.MeltSize,1.f,D.Ice);
                const TArray<FVector2D> Stem{P,P+Dir*Length};
                const FLinearColor Color(.84f,.94f,1.f,.72f*FMath::Sqrt(D.Ice)*Fade);
                FSlateDrawElement::MakeLines(Out,Layer+1,Geo.ToPaintGeometry(),Stem,ESlateDrawEffect::None,Color,true,1.1f);
                for(int32 Sign : {-1,1}) {
                    const TArray<FVector2D> Twig{P+Dir*Length*.52f,P+Dir*Length*.76f+Side*Length*.22f*Sign};
                    FSlateDrawElement::MakeLines(Out,Layer+1,Geo.ToPaintGeometry(),Twig,ESlateDrawEffect::None,Color,true,1.f);
                }
            }
            if(D.Ice>=.4f) continue;
            R*=D.MeltSize;
            Fade*=1.f-D.Ice/.4f;
        }
        const FVector2D Flow=(D.Velocity*Screen).GetSafeNormal();
        const FVector2D Down=Flow.IsNearlyZero()?FVector2D(0,1):Flow, Right(Down.Y,-Down.X);
        const float Stretch=FMath::Clamp(float(D.Velocity.Size())*12.f,0.f,1.f);
        TArray<FVector2D> Rim;
        TArray<FSlateVertex> V;
        TArray<SlateIndex> Indices;
        V.Add(FSlateVertex::Make(Geo.GetAccumulatedRenderTransform(),FVector2f(P),FVector2f::ZeroVector,FLinearColor(.04f,.10f,.14f,.13f*Fade).ToFColor(true)));
        constexpr int32 Segments=28;
        for(int32 I=0;I<=Segments;++I) {
            const float A=I*2*PI/Segments;
            const float C=FMath::Cos(A), S=FMath::Sin(A);
            const float Wobble=1.f+.06f*FMath::Sin(A*3+D.Phase);
            const float Width=1.f-Stretch*.32f*(1.f-C);
            const FVector2D Point=P+Right*(S*R*Width*Wobble)+Down*(C*R*(1.f+Stretch*.85f));
            Rim.Add(Point);
            V.Add(FSlateVertex::Make(Geo.GetAccumulatedRenderTransform(),FVector2f(Point),FVector2f::ZeroVector,FLinearColor(.13f,.22f,.29f,.035f*Fade).ToFColor(true)));
            if(I<Segments) Indices.Append({0,SlateIndex(I+1),SlateIndex(I+2)});
        }
        FSlateDrawElement::MakeCustomVerts(Out,Layer,FSlateResourceHandle(),V,Indices,nullptr,0,0,ESlateDrawEffect::None);
        // Complete but faint edge; stronger glints on two unequal arcs, no missing side or cross.
        FSlateDrawElement::MakeLines(Out,Layer+1,Geo.ToPaintGeometry(),Rim,ESlateDrawEffect::None,FLinearColor(.66f,.79f,.88f,.10f*Fade),true,.8f);
        for(int32 Arc=0;Arc<2;++Arc) {
            TArray<FVector2D> Glint;
            const int32 Start=Arc==0?4:19, Count=Arc==0?5:3;
            for(int32 I=0;I<=Count;++I) Glint.Add(Rim[Start+I]);
            FSlateDrawElement::MakeLines(Out,Layer+1,Geo.ToPaintGeometry(),Glint,ESlateDrawEffect::None,FLinearColor(.83f,.92f,1.f,(Arc==0?.24f:.13f)*Fade),true,1.f);
        }
    }
    return Layer+1;
}


void SWowWeatherLens::PaintFrost(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer) const
{
    if (Frost<=.001f || !FWowClientPreferences::GetWorldOption(TEXT("frostlens"))) return;
    const FVector2D Size=Geo.GetLocalSize();
    // A gentle cool wash under the HUD; no exposure, lighting or DoF changes.
    FSlateDrawElement::MakeBox(Out,Layer,Geo.ToPaintGeometry(),FCoreStyle::Get().GetBrush("WhiteBrush"),
        ESlateDrawEffect::None,FLinearColor(.08f,.29f,.65f,.12f*Frost));
    constexpr int32 Rings=7, Segments=20;
    TArray<FSlateVertex> Verts;
    TArray<SlateIndex> Indices;
    Verts.Reserve(4*(Rings+1)*(Segments+1));
    Indices.Reserve(4*Rings*Segments*6);
    // r233 (Stefan): deep frost grows on the interface (minimap, bars, frames - see
    // PaintFrameFrost) instead of over the screen; the screen corners stay as before r232.
    const float Growth=FMath::Sqrt(Frost);
    for(int32 Corner=0;Corner<4;++Corner)
    {
        const FVector2D Origin((Corner&1)?Size.X:0,(Corner&2)?Size.Y:0);
        const FVector2D Sign((Corner&1)?-1:1,(Corner&2)?-1:1);
        const float CornerScale[]={1.f,.53f,.72f,.86f};
        const FVector2D Extent(Size.X*.30f*Growth*CornerScale[Corner],Size.Y*.38f*Growth*(.66f+.34f*CornerScale[(Corner+1)%4]));
        const int32 Base=Verts.Num();
        for(int32 Ring=0;Ring<=Rings;++Ring)
            for(int32 Segment=0;Segment<=Segments;++Segment)
            {
                const float T=float(Ring)/Rings;
                const float A=float(Segment)/Segments*PI*.5f;
                const float Jagged=.90f+.06f*FMath::Sin(A*19+Corner*3)+.04f*FMath::Cos(A*31+Corner);
                const FVector2D Pos=Origin+Sign*Extent*FVector2D(FMath::Cos(A),FMath::Sin(A))*T*Jagged;
                const float Patch=.65f+.22f*FMath::Sin(A*11+Corner*5)+.13f*FMath::Cos(A*23+T*9+Corner);
                const float Alpha=.5f*Frost*FMath::Square(1.f-T)*Patch;
                Verts.Add(FSlateVertex::Make(Geo.GetAccumulatedRenderTransform(),FVector2f(Pos),FVector2f::ZeroVector,FLinearColor(.58f,.79f,.94f,Alpha).ToFColor(true)));
                if(Ring<Rings && Segment<Segments)
                {
                    const int32 I=Base+Ring*(Segments+1)+Segment;
                    // Alternate winding after mirroring a corner.
                    if(Corner==1 || Corner==2) Indices.Append({SlateIndex(I),SlateIndex(I+Segments+1),SlateIndex(I+1),SlateIndex(I+1),SlateIndex(I+Segments+1),SlateIndex(I+Segments+2)});
                    else Indices.Append({SlateIndex(I),SlateIndex(I+1),SlateIndex(I+Segments+1),SlateIndex(I+1),SlateIndex(I+Segments+2),SlateIndex(I+Segments+1)});
                }
            }
    }
    FSlateDrawElement::MakeCustomVerts(Out,Layer,FSlateResourceHandle(),Verts,Indices,nullptr,0,0,ESlateDrawEffect::None);
    // Small irregular crystalline patches, not identical spokes radiating from four corners.
    FRandomStream Pattern(73519);
    for(int32 Corner=0;Corner<4;++Corner) {
        const FVector2D Origin((Corner&1)?Size.X:0,(Corner&2)?Size.Y:0), Sign((Corner&1)?-1:1,(Corner&2)?-1:1);
        for(int32 Crystal=0;Crystal<12+Corner*3;++Crystal) {
            const float A=Pattern.FRandRange(0,PI*.5f), Spread=Pattern.FRandRange(.06f,.8f);
            const FVector2D P=Origin+Sign*FVector2D(FMath::Cos(A)*Size.X*.24f,FMath::Sin(A)*Size.Y*.28f)*Growth*Spread;
            const float Length=Pattern.FRandRange(3,15)*Growth, Rotation=Pattern.FRandRange(0,PI);
            for(int32 Arm=0;Arm<3;++Arm) {
                const float Angle=Rotation+Arm*PI/3;
                const FVector2D V(FMath::Cos(Angle)*Length,FMath::Sin(Angle)*Length);
                const TArray<FVector2D> Points{P-V,P+V};
                FSlateDrawElement::MakeLines(Out,Layer+1,Geo.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.77f,.90f,1.f,.2f*Frost*(1.f-Spread)),true,1.f);
            }
        }
    }
}

float SWowWeatherLens::GetFrostLevel()
{
    return FWowClientPreferences::GetWorldOption(TEXT("frostlens")) ? GWowLensFrost : 0.f;
}

void SWowWeatherLens::PaintFrameFrost(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer, const FSlateRect& Rect, int32 Seed)
{
    const float Frost=GetFrostLevel();
    if(Frost<.05f || Rect.GetSize().X<2 || Rect.GetSize().Y<2) return;
    // Frames ice over a little after the screen corners (from 0.3), the deep stage frosts them over.
    const float Rime=FMath::Clamp((Frost-.3f)/.4f,0.f,1.f);
    const float Deep=FMath::SmoothStep(.55f,.95f,Frost);
    const float Cover=FMath::Clamp((Deep-.55f)/.45f,0.f,1.f);
    if(Rime<=.001f) return;
    const FVector2D Min=Rect.GetTopLeft(), Size=Rect.GetSize();
    const float Band=FMath::Min(Size.X,Size.Y)*FMath::Lerp(.12f,.35f,Deep);
    TArray<FSlateVertex> Verts; TArray<SlateIndex> Indices;
    auto Vert=[&](const FVector2D& P,float A){ Verts.Add(FSlateVertex::Make(Geo.GetAccumulatedRenderTransform(),FVector2f(P),FVector2f::ZeroVector,FLinearColor(.70f,.86f,.98f,A).ToFColor(true))); };
    // Rime band along all four edges, ragged on the inside.
    constexpr int32 Steps=24;
    for(int32 Edge=0;Edge<4;++Edge)
    {
        const bool Horizontal=Edge<2;
        const float Length=Horizontal?Size.X:Size.Y;
        const int32 Base=Verts.Num();
        for(int32 I=0;I<=Steps;++I)
        {
            const float T=float(I)/Steps, Along=T*Length;
            const float Ragged=.55f+.3f*FMath::Sin(T*29+Edge*3+Seed)+.15f*FMath::Cos(T*67+Seed*.7f);
            auto At=[&](float In){ return Min+(Edge==0?FVector2D(Along,In):Edge==1?FVector2D(Along,Size.Y-In):Edge==2?FVector2D(In,Along):FVector2D(Size.X-In,Along)); };
            Vert(At(0),.55f*Rime); Vert(At(Band*Ragged*Rime),0.f);
            if(I<Steps){ const SlateIndex A=SlateIndex(Base+I*2); Indices.Append({A,SlateIndex(A+1),SlateIndex(A+2),SlateIndex(A+1),SlateIndex(A+3),SlateIndex(A+2)}); }
        }
    }
    // Frosted glass over the whole frame at the last stage.
    if(Cover>.001f)
    {
        const int32 Base=Verts.Num();
        Vert(Min,.5f*Cover); Vert(Min+FVector2D(Size.X,0),.5f*Cover); Vert(Min+Size,.5f*Cover); Vert(Min+FVector2D(0,Size.Y),.5f*Cover);
        Indices.Append({SlateIndex(Base),SlateIndex(Base+1),SlateIndex(Base+2),SlateIndex(Base),SlateIndex(Base+2),SlateIndex(Base+3)});
    }
    FSlateDrawElement::MakeCustomVerts(Out,Layer,FSlateResourceHandle(),Verts,Indices,nullptr,0,0,ESlateDrawEffect::None);
    // Crystals, more as it deepens.
    FRandomStream Pattern(Seed*7919+17);
    const int32 Crystals=FMath::RoundToInt((3.f+Size.X*Size.Y/1500.f)*Rime*(1.f+Deep));
    for(int32 C=0;C<FMath::Min(Crystals,40);++C)
    {
        // Mostly near the edges, some across the frame once it frosts over.
        const bool Inside=Pattern.FRand()<Cover*.6f;
        FVector2D P=Min+FVector2D(Pattern.FRand()*Size.X,Pattern.FRand()*Size.Y);
        if(!Inside){ const int32 E=Pattern.RandRange(0,3); if(E==0)P.Y=Min.Y+Pattern.FRand()*Band; else if(E==1)P.Y=Min.Y+Size.Y-Pattern.FRand()*Band; else if(E==2)P.X=Min.X+Pattern.FRand()*Band; else P.X=Min.X+Size.X-Pattern.FRand()*Band; }
        const float Len=Pattern.FRandRange(2.5f,7.f)*FMath::Lerp(.6f,1.f,Rime), Rot=Pattern.FRandRange(0,PI);
        for(int32 Arm=0;Arm<3;++Arm)
        {
            const FVector2D V(FMath::Cos(Rot+Arm*PI/3)*Len,FMath::Sin(Rot+Arm*PI/3)*Len);
            FSlateDrawElement::MakeLines(Out,Layer+1,Geo.ToPaintGeometry(),TArray<FVector2D>{P-V,P+V},ESlateDrawEffect::None,FLinearColor(.88f,.95f,1.f,.6f*Rime),true,1.f);
        }
    }
}

void SWowWeatherLens::PaintMinimapFrost(const FGeometry& Geo,FSlateWindowElementList& Out,int32 Layer) const
{
    const auto Map=Minimap.Get();
    if(!FWowClientPreferences::GetWorldOption(TEXT("frostlens")) || Frost<.01f || !Map.IsValid() || !Map->GetVisibility().IsVisible()) return;
    const FGeometry& MG=Map->GetCachedGeometry();
    if(MG.GetLocalSize().X<1) return;
    const FVector2D Center=Geo.AbsoluteToLocal(MG.LocalToAbsolute(FVector2D(92,92)));
    const float Scale=float((Geo.AbsoluteToLocal(MG.LocalToAbsolute(FVector2D(184,92)))-Center).Size())/92.f;
    // Drawn in the FrameXML canvas above the gold rim and below clock/bags.
    // r232: with deep frost the rim ices over all the way round (from the screen's top-right
    // corner frost), and at the last stage the map itself frosts over so it is hard to read.
    const float Deep=FMath::SmoothStep(.55f,.95f,Frost);
    const int32 Patches=4+FMath::RoundToInt(5.f*Deep);
    for(int32 Patch=0;Patch<Patches;++Patch) {
        const float Start=2.3f+Patch*.73f;
        for(int32 Ring=0;Ring<4;++Ring) {
            TArray<FVector2D> Points;
            for(int32 I=0;I<=12;++I) {
                const float A=Start+I*.032f;
                const float R=(91.f+Ring*1.6f+FMath::Sin(A*19)*1.2f)*Scale;
                Points.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*R);
            }
            FSlateDrawElement::MakeLines(Out,Layer+1,Geo.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.72f,.87f,1.f,FMath::Sqrt(Frost)*.70f*(1.f-Ring*.16f)),true,1.5f*Scale);
        }
    }
    const float Cover=FMath::Clamp((Deep-.55f)/.45f,0.f,1.f);
    if(Cover>.001f)
    {
        // Frosted glass over the map: thicker at the rim, thinnest in the middle, uneven.
        constexpr int32 Rings=6, Segments=40;
        TArray<FSlateVertex> Verts; TArray<SlateIndex> Indices;
        for(int32 Ring=0;Ring<=Rings;++Ring)
            for(int32 Segment=0;Segment<=Segments;++Segment)
            {
                const float T=float(Ring)/Rings, A=float(Segment)/Segments*2.f*PI;
                const float Patch=.75f+.15f*FMath::Sin(A*7+T*5)+.1f*FMath::Cos(A*13-T*3);
                const float Alpha=Cover*Patch*FMath::Lerp(.55f,.85f,T);
                Verts.Add(FSlateVertex::Make(Geo.GetAccumulatedRenderTransform(),FVector2f(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*T*90.f*Scale),
                    FVector2f::ZeroVector,FLinearColor(.80f,.90f,.98f,Alpha).ToFColor(true)));
                if(Ring<Rings && Segment<Segments)
                {
                    const int32 I=Ring*(Segments+1)+Segment;
                    Indices.Append({SlateIndex(I),SlateIndex(I+1),SlateIndex(I+Segments+1),SlateIndex(I+1),SlateIndex(I+Segments+2),SlateIndex(I+Segments+1)});
                }
            }
        FSlateDrawElement::MakeCustomVerts(Out,Layer+1,FSlateResourceHandle(),Verts,Indices,nullptr,0,0,ESlateDrawEffect::None);
        // Ice crystals on top.
        FRandomStream Pattern(91733);
        for(int32 Crystal=0;Crystal<18;++Crystal)
        {
            const float A=Pattern.FRandRange(0,2*PI), R=FMath::Sqrt(Pattern.FRand())*80.f*Scale;
            const FVector2D P=Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*R;
            const float Len=Pattern.FRandRange(4,12)*Scale*Cover, Rot=Pattern.FRandRange(0,PI);
            for(int32 Arm=0;Arm<3;++Arm)
            {
                const FVector2D V(FMath::Cos(Rot+Arm*PI/3)*Len,FMath::Sin(Rot+Arm*PI/3)*Len);
                FSlateDrawElement::MakeLines(Out,Layer+2,Geo.ToPaintGeometry(),TArray<FVector2D>{P-V,P+V},ESlateDrawEffect::None,FLinearColor(.9f,.96f,1.f,.55f*Cover),true,1.f);
            }
        }
    }
}

namespace {
class SWowMinimapFrostOverlay final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowMinimapFrostOverlay) {} SLATE_ARGUMENT(TWeakPtr<SWowWeatherLens>, Lens) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Lens=Args._Lens; SetVisibility(EVisibility::HitTestInvisible); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geo,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool) const override
    { if(auto Owner=Lens.Pin()) Owner->PaintMinimapFrost(Geo,Out,Layer); return Layer+1; }
private:
    TWeakPtr<SWowWeatherLens> Lens;
};
}
TSharedRef<SWidget> SWowWeatherLens::CreateMinimapFrostOverlay()
{
    return SNew(SWowMinimapFrostOverlay).Lens(SharedThis(this));
}
