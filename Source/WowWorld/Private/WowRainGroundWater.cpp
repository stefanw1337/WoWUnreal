#include "WowWeatherComponent.h"
#include "WowCookedMaterial.h"
#include "ProceduralMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/ExponentialHeightFogComponent.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionDepthFade.h"
#endif

namespace
{
// The distance fade. r682 (Stefan: "they appear as I walk now - they must have LOONG ranges so they don't just
// appear"): puddles reach 90 m and fade out over the last 20 m (was 35 m).
constexpr float FadeStart=7000.f, FadeLength=2000.f;
}

UMaterialInterface* UWowWeatherComponent::PrepareGroundWaterMaterial(UObject* Outer)
{
    // A missing cooked asset made every call flush the async loader (a hitch every
    // ~0.1 s, which in VR halved the frame rate and shook the view). Try once.
    static TWeakObjectPtr<UMaterial> Cached; static bool bTried=false;
    if(Cached.IsValid()) return Cached.Get();
#if !WITH_EDITOR
    if(bTried) return nullptr;
#endif
    bTried=true;
    UMaterial* M=WowCookedMaterial::Load(TEXT("M_WowRainGroundWater_v8")); // r724: smooth edge noise
    if(M) {Cached=M; return M;}
#if WITH_EDITOR
    if(!M)
    {
        M=NewObject<UMaterial>(Outer);
        M->SetShadingModel(MSM_DefaultLit);
        M->BlendMode=BLEND_Translucent;
        M->TranslucencyLightingMode=TLM_SurfacePerPixelLighting;
        M->TwoSided=true;
        M->bTangentSpaceNormal=false;
        // r682 (Stefan: some puddles had a "smear" - grey streaks stretched across the water): screen-space
        // reflections on a translucent surface smear at grazing angles and where the reflected thing is off screen.
        // Off: the sky mirror comes from the emission below, the ripples from the normal.
        M->bScreenSpaceReflections=false;
        auto& G=M->GetExpressionCollection();
        auto Scalar=[&](float Value){auto* E=NewObject<UMaterialExpressionConstant>(M);E->R=Value;G.AddExpression(E);return E;};
        // Clear water over wet ground: looking down you see the ground through it (slightly
        // darker); towards the horizon it turns into a mirror of the sky (Fresnel). Translucent
        // surfaces get almost no Lumen / ray-traced reflections, so the sky reflection is
        // added as emission from SkyColor (fed from the zone's fog/sky colour each update).
        auto* Color=NewObject<UMaterialExpressionConstant3Vector>(M); Color->Constant=FLinearColor(.02f,.022f,.022f);
        auto* Sky=NewObject<UMaterialExpressionVectorParameter>(M); Sky->ParameterName=TEXT("SkyColor"); Sky->DefaultValue=FLinearColor(.45f,.5f,.55f);
        auto* RainAmount=NewObject<UMaterialExpressionScalarParameter>(M); RainAmount->ParameterName=TEXT("RainRipples"); RainAmount->DefaultValue=1.f;
        G.AddExpression(Sky); G.AddExpression(RainAmount);
        auto* Vertex=NewObject<UMaterialExpressionVertexColor>(M);
        auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(M);
        auto* Time=NewObject<UMaterialExpressionTime>(M);
        auto* Normal=NewObject<UMaterialExpressionCustom>(M); Normal->OutputType=CMOT_Float3;
        FCustomInput A; A.InputName=TEXT("UV"); A.Input.Connect(0,UV); Normal->Inputs.Add(A);
        FCustomInput B; B.InputName=TEXT("Time"); B.Input.Connect(0,Time); Normal->Inputs.Add(B);
        FCustomInput C; C.InputName=TEXT("Flow"); C.Input.Connect(0,Vertex); Normal->Inputs.Add(C);
        FCustomInput R; R.InputName=TEXT("Rain"); R.Input.Connect(0,RainAmount); Normal->Inputs.Add(R);
        // Vertex RG stores downhill direction, B runoff strength. Raindrops make expanding
        // rings in random cells (two layers, no regular pattern); runoff adds a gentle
        // irregular flow. The old straight sine waves read as "rails".
        Normal->Code=TEXT(
            "float2 n=0;"
            "float2 p=UV*8.0;"
            "for(int k=0;k<2;k++){"
            " float2 c=floor(p); float2 q=frac(p)-0.5;"
            " float h=frac(sin(dot(c,float2(12.9898,78.233))+k*17.0)*43758.5453);"
            " float2 o=float2(frac(h*7.13),frac(h*13.71))-0.5;"
            " float2 v=q-o*0.3; float r=length(v);"
            " float t=frac(Time*0.85+h);"
            " float ring=r-t*0.3;"
            " float w=exp(-ring*ring*700.0)*(1-t)*(1-t);"
            // A ring is drawn only inside its own cell; fading it out before the cell edge
            // hides the square cell borders (they showed as a checkerboard in the rain).
            " w*=saturate((0.5-max(abs(q.x),abs(q.y)))*12.0);"
            // r682 (Stefan: the raindrop rings "make sense, but the execution isn't right" - they showed as bold
            // grey swirls across the pool): a third of the strength, and twice as many, half the size.
            " n+=v/max(r,1e-3)*w*0.15*Rain;"
            " p=p*1.618+float2(5.3,9.1);"
            "}"
            // No flow waves: on screen they read as tyre tracks crossing every film.
            "return normalize(float3(n,1));");
        for(UMaterialExpression* E:TArray<UMaterialExpression*>{Color,Vertex,UV,Time,Normal}) G.AddExpression(E);
        M->GetEditorOnlyData()->BaseColor.Connect(0,Color);
        // Organic outlines: the per-vertex fill is thresholded against smooth world-space
        // noise per pixel, so puddle edges curve instead of following the sample grid.
        auto* Shape=NewObject<UMaterialExpressionCustom>(M); Shape->OutputType=CMOT_Float1;
        FCustomInput SA; SA.InputName=TEXT("Fill"); SA.Input.Connect(4,Vertex); Shape->Inputs.Add(SA);
        FCustomInput SU; SU.InputName=TEXT("UV"); SU.Input.Connect(0,UV); Shape->Inputs.Add(SU);
        // Several octaves (metres): broad lobes plus finer wobble, so the 60 cm
        // sample grid never shows through as straight or diamond-shaped edges.
        // r724 (Stefan, close up: "Strange edges on some of the puddles" - straight streaks across the soft rim):
        // the sine terms were straight wave fronts ((x+y), (x-y)), which show as parallel stripes wherever the rim
        // is soft. Smooth value noise now (three octaves, each turned and scaled, hashed on integer cells so the
        // large world coordinates keep their precision): round, irregular wobble with no direction to it.
        Shape->Code=TEXT("float n=0; float amp=0.5; float2 q=UV*1.3;"
            "for(int o=0;o<3;o++){"
            " float2 ip=floor(q); float2 f=q-ip; float2 u=f*f*(3.0-2.0*f); float v[4];"
            " for(int k=0;k<4;k++){"
            "  int2 c=int2(ip)+int2(k&1,k>>1);"
            "  uint h=uint(c.x)*374761393u+uint(c.y)*668265263u+uint(o)*2246822519u;"
            "  h=(h^(h>>13))*1274126177u; h=h^(h>>16);"
            "  v[k]=float(h&65535u)/65535.0;"
            " }"
            " n+=amp*(lerp(lerp(v[0],v[1],u.x),lerp(v[2],v[3],u.x),u.y)-0.5);"
            " q=float2(q.x*1.6-q.y*1.2,q.x*1.2+q.y*1.6)*1.45; amp*=0.5;"
            "}"
            "n*=2.7;"
            // Soft, wobbly rim: the grid's linear fill ramps are cut by strong noise and
            // feathered, so a film never shows a straight hard side.
            // r682 (Stefan: the round pools still ended in a "too sharp" edge - "the form of it is cool"): the noise
            // could push the fill past the threshold all the way out to the mesh's rim, which then showed as a hard
            // polygon side. The noise now only wobbles the edge (+-0.17 of the fill) and the rim (fill 0) is always
            // clear, with a wide feather: a soft shore, never the mesh outline.
            "float shape=smoothstep(0.18,0.75,Fill+n*.12);"
            "float F=0.02+0.98*pow(1-saturate(abs(Parameters.CameraVector.z)),5);"
            "return shape*(0.16+0.74*F);");
        G.AddExpression(Shape);
        // r682 (Stefan: a pool with a sharp straight notch cut out of it): where a grass hump rises through the
        // water the surface ended on a hard intersection line. The water now fades out over the last 2 cm above
        // the ground (depth fade), so the ground meets it softly.
        auto* Soft=NewObject<UMaterialExpressionDepthFade>(M);
        Soft->InOpacity.Connect(0,Shape);
        Soft->FadeDistanceDefault=2.f;
        G.AddExpression(Soft);
        M->GetEditorOnlyData()->Opacity.Connect(0,Soft);
        // Emission is multiplied by opacity in translucency, so divide the opacity curve out:
        // what remains is SkyColor * Fresnel.
        auto* Reflection=NewObject<UMaterialExpressionCustom>(M); Reflection->OutputType=CMOT_Float3;
        FCustomInput RS; RS.InputName=TEXT("Sky"); RS.Input.Connect(0,Sky); Reflection->Inputs.Add(RS);
        Reflection->Code=TEXT("float F=0.02+0.98*pow(1-saturate(abs(Parameters.CameraVector.z)),5); return Sky*F/(0.16+0.74*F);");
        G.AddExpression(Reflection);
        M->GetEditorOnlyData()->EmissiveColor.Connect(0,Reflection);
        // Standing water: near-mirror surface (rain ripples come from the animated normal).
        // Water's real reflectance (F0 ~0.02, UE specular ~0.3): it was 0.75 and
        // turned the puddles into glowing white sheets under an overcast sky.
        M->GetEditorOnlyData()->Roughness.Connect(0,Scalar(.06f));
        M->GetEditorOnlyData()->Specular.Connect(0,Scalar(.5f));
        M->GetEditorOnlyData()->Normal.Connect(0,Normal);
        M->PostEditChange();
        M=WowCookedMaterial::Save(M,TEXT("M_WowRainGroundWater_v8"));
    }
#endif
    Cached=M;
    return M;
}

namespace
{
TWeakObjectPtr<UWowWeatherComponent> GPuddleSource;
// Hashed lattice value per spot (r680: puddle spots, sizes and shapes).
float PuddleHash(int32 X, int32 Y)
{
    uint32 H = uint32(X) * 374761393u + uint32(Y) * 668265263u;
    H = (H ^ (H >> 13)) * 1274126177u;
    return float((H ^ (H >> 16)) & 0xFFFF) / 65535.f;
}
}

float UWowWeatherComponent::GetPuddleDepthAt(const FVector& Location)
{
    // r680: PuddlePoints hold each drawn pool's centre and its wet radius (cm) in W.
    const UWowWeatherComponent* Source = GPuddleSource.Get();
    if (!Source) return 0.f;
    for (const FVector4f& P : Source->PuddlePoints)
    {
        const float D2 = FMath::Square(float(Location.X) - P.X) + FMath::Square(float(Location.Y) - P.Y);
        if (D2 < FMath::Square(P.W) && FMath::Abs(float(Location.Z) - P.Z) < 60.f) return 1.f;
    }
    return 0.f;
}

// r680 (Stefan, a puddle in the rain: square, with straight sharp sides - "need to round up the puddles, and I want
// different sized ones, some bigger over time, and some this size, maybe even smaller, and I want to see them a lot
// around, the more rain the more"): the old films were cells of a 60 cm sample grid filled where the ground held
// water, so every puddle was built from squares and ended on a straight cell side. Puddles are now round pools of
// their own: candidate spots on a jittered 3 m world grid (fixed per spot, so they come back in the same places),
// each with a size class - small 35-70 cm, medium 0.8-1.5 m, a few large 1.8-3.5 m - and a slightly oval, turned
// outline. The more rain, the more of the spots fill (a spot's own threshold against the wetness), and each pool
// grows from a quarter of its size: small ones in ~15 s, medium in a minute, large ones over four minutes; when the
// rain stops they shrink away again. A pool is a disc (centre, inner ring, rim) laid on the traced ground, only on
// level, open-sky terrain or building floors. Its alpha runs from full inside to nothing at the rim, so the
// material's noise threshold gives a soft, wobbly edge - round for small pools, lobed for big ones.
namespace
{
// r682: spots out to 90 m (kept to 110 m); 12 segments a ring keeps the traces and the mesh small at that range.
constexpr float SiteSpacing=300.f, SiteReach=9000.f, SiteForget=11000.f;
constexpr int32 RingSegments=12, SiteVertices=1+2*RingSegments;
constexpr float MaxDensity=.72f;
// r684: how much rain it takes depends on the ground - grass holds a fifth of the puddles, bare ground up to 1.35x.
inline float PuddleGroundShare(float Ground) { return FMath::Lerp(.2f,1.35f,FMath::Clamp(Ground,0.f,1.f)); }
// r724 (Stefan, Stormwind in the rain: "These puddles are very round, all of them. Make them more random, but also
// they can combine, and become bigger. No sharp edges, just round edges."): a pool is no longer one oval disc. Each
// spot holds a main lobe and up to three smaller ones around it, each with a lumpy outline (2nd-4th harmonics of its
// radius). The side lobes start later in the rain and grow into the main one, and pools that grow into each other
// merge: the lobes add up as a smooth field (metaballs) drawn on a fine grid, so where two pools meet the water
// runs together instead of overlapping as two discs. The grid also removes the 12-slice fan whose alpha ramps
// showed as streaks in the rim.
struct FPuddleLobe
{
    FVector2D Offset=FVector2D::ZeroVector; // spot frame (before Aspect/Angle), in MaxRadius units
    float Radius=.7f;                       // visible radius at full growth, MaxRadius units
    float Delay=0.f;                        // share of the pool's growth before this lobe starts
    float Harm[3]={0.f,0.f,0.f}, Phase[3]={0.f,0.f,0.f}; // outline harmonics 2, 3, 4
};
struct FPuddleSite
{
    bool bTested=false, bValid=false;
    float Presence=1.f;          // the wetness share at which this spot fills
    FVector2D Spot=FVector2D::ZeroVector;
    FVector Centre=FVector::ZeroVector;
    float MaxRadius=50.f, Aspect=1.f, Angle=0.f, GrowSeconds=15.f, Grow=0.f;
    float Ground=1.f;            // r684: 0 grass .. 1 dirt, road, stone, building floor
    TArray<float> Heights;       // per vertex, relative to Centre.Z, traced at MaxRadius
    int32 Probe=0;               // next vertex to trace
    TArray<FPuddleLobe,TInlineAllocator<4>> Lobes; // r724
};
struct FPuddleField { TMap<FIntPoint,FPuddleSite> Sites; double LastDraw=0.0; float RainSeconds=0.f; };
TMap<TWeakObjectPtr<UWowWeatherComponent>,FPuddleField> GPuddleFields;
FVector2D PuddleVertexOffset(const FPuddleSite& S,int32 Vertex,float Radius)
{
    if(Vertex==0) return FVector2D::ZeroVector;
    const int32 Ring=(Vertex-1)/RingSegments, K=(Vertex-1)%RingSegments;
    const float A=2.f*PI*K/RingSegments, R=Radius*(Ring==0?.6f:1.f);
    return FVector2D(FMath::Cos(A)*R,FMath::Sin(A)*R*S.Aspect).GetRotated(FMath::RadiansToDegrees(S.Angle));
}
bool PuddleGround(const FHitResult& Hit,float MinNormalZ)
{
    if(Hit.ImpactNormal.Z<MinNormalZ) return false;
    const FString Name=Hit.GetComponent()?Hit.GetComponent()->GetFName().ToString():FString();
    return Name.StartsWith(TEXT("TerrainMesh"))||Name.StartsWith(TEXT("Wmo"));
}
// r724: the field a lobe adds is (1 - x^2)^2 out to 1.5x its outline radius; the material's threshold (fill ~0.47)
// puts the visible shore at ~0.84 of the outline, with a soft feather from ~0.55 to ~1.15.
constexpr float LobeKernel=1.5f, LobeVisibleReach=1.14f;
float LobeHarmonicSum(const FPuddleLobe& L) { return L.Harm[0]+L.Harm[1]+L.Harm[2]; }
void MakePuddleLobes(FPuddleSite& S,const FIntPoint& Key,int32 SizeClass)
{
    auto H=[&Key](int32 Salt){ return PuddleHash(Key.X*7+Salt*131,Key.Y*13-Salt*71); };
    auto Outline=[&H](FPuddleLobe& L,int32 Salt,float Amount)
    {
        static constexpr float Share[3]={.55f,.3f,.15f};
        for(int32 I=0;I<3;++I) { L.Harm[I]=Amount*Share[I]*H(Salt+I); L.Phase[I]=2.f*PI*H(Salt+5+I); }
    };
    S.Lobes.Reset();
    // Everything stays inside the traced outline (MaxRadius) at full size, ~5 % slack.
    FPuddleLobe Main; Outline(Main,20,.45f);
    Main.Radius=FMath::Min(FMath::Lerp(.6f,.78f,H(1)),1.05f/((1.f+LobeHarmonicSum(Main))*LobeVisibleReach));
    S.Lobes.Add(Main);
    const int32 Extra=SizeClass==0 ? (H(2)<.55f?1:0) : SizeClass==1 ? 1+(H(3)<.5f?1:0) : 2+(H(4)<.5f?1:0);
    for(int32 E=0;E<Extra;++E)
    {
        FPuddleLobe L; const int32 Salt=100+E*37;
        Outline(L,Salt+1,.35f);
        L.Radius=FMath::Lerp(.24f,.44f,H(Salt));
        const float Reach=FMath::Max(.12f,1.05f-L.Radius*(1.f+LobeHarmonicSum(L))*LobeVisibleReach);
        const float Angle=2.f*PI*H(Salt+3);
        L.Offset=FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*FMath::Lerp(.35f,1.f,H(Salt+2))*Reach;
        L.Delay=FMath::Lerp(.1f,.65f,H(Salt+4)); // side lobes come later in the rain and grow into the main one
        S.Lobes.Add(L);
    }
}
// World XY -> the spot's own frame (Angle undone, Aspect undone), in MaxRadius units.
FVector2D PuddleSpotFrame(const FPuddleSite& S,const FVector2D& P)
{
    FVector2D U=(P-FVector2D(S.Centre)).GetRotated(-FMath::RadiansToDegrees(S.Angle));
    U.Y/=FMath::Max(S.Aspect,.3f);
    return U/S.MaxRadius;
}
// The lobe's field at U; Size is its growth scale (0..1).
float LobeField(const FPuddleLobe& L,const FVector2D& U,float Size)
{
    if(Size<=0.f) return 0.f;
    const FVector2D V=U-L.Offset;
    const float D2=float(V.SizeSquared());
    const float Max=L.Radius*Size*LobeKernel*(1.f+LobeHarmonicSum(L));
    if(D2>=Max*Max) return 0.f;
    const float T=float(FMath::Atan2(V.Y,V.X));
    const float R=L.Radius*Size*LobeKernel*(1.f+L.Harm[0]*FMath::Cos(2.f*T+L.Phase[0])+L.Harm[1]*FMath::Cos(3.f*T+L.Phase[1])+L.Harm[2]*FMath::Cos(4.f*T+L.Phase[2]));
    const float X2=D2/FMath::Square(FMath::Max(R,1e-3f));
    return X2<1.f ? FMath::Square(1.f-X2) : 0.f;
}
// Ground height under U (spot frame) from the traced centre and rings, relative to Centre.Z.
float PuddleGroundHeight(const FPuddleSite& S,const FVector2D& U)
{
    const float Rho=float(U.Size());
    float T=float(FMath::Atan2(U.Y,U.X)); if(T<0.f) T+=2.f*PI;
    const float F=T/(2.f*PI)*RingSegments;
    const int32 K0=FMath::FloorToInt(F)%RingSegments, K1=(K0+1)%RingSegments;
    const float A=F-FMath::FloorToFloat(F);
    const float Inner=FMath::Lerp(S.Heights[1+K0],S.Heights[1+K1],A);
    const float Outer=FMath::Lerp(S.Heights[1+RingSegments+K0],S.Heights[1+RingSegments+K1],A);
    if(Rho<=.6f) return FMath::Lerp(S.Heights[0],Inner,Rho/.6f);
    return FMath::Lerp(Inner,Outer,FMath::Min((Rho-.6f)/.4f,1.f));
}
}

float WowTerrainPuddleGroundAt(const FVector& Location); // WowTerrainTile.cpp (r684)

void UWowWeatherComponent::UpdateGroundWater(float Dt,float Rain)
{
    GPuddleSource = this;
    auto* World=GetWorld(); auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0);
    for(auto It=GPuddleFields.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
    FPuddleField& Field=GPuddleFields.FindOrAdd(this);
    if(!World||!Pawn||Zone==0||SurfaceWetness<.03f)
    {
        if(GroundWaterMesh) GroundWaterMesh->SetVisibility(false);
        if(Zone==0||SurfaceWetness<.03f) Field.Sites.Reset();
        PuddlePoints.Reset();
        return;
    }
    const FVector Anchor=Pawn->GetActorLocation();
    const double Now=World->GetTimeSeconds();
    const float Fill=FMath::Clamp((SurfaceWetness-.12f)/.75f,0.f,1.f);
    // More rain, more puddles: the share of spots that hold water.
    const float Density=FMath::Lerp(.12f,MaxDensity,Fill);
    if(Now>=NextGroundWaterSearch)
    {
        NextGroundWaterSearch=Now+1;
        for(auto It=Field.Sites.CreateIterator();It;++It)
        {
            const FPuddleSite& S=It.Value();
            if(FVector2D::Distance(S.Spot,FVector2D(Anchor))>SiteForget || (S.bValid && FMath::Abs(S.Centre.Z-Anchor.Z)>1500.f)) It.RemoveCurrent();
        }
        const int32 Reach=FMath::CeilToInt(SiteReach/SiteSpacing);
        const FIntPoint C(FMath::FloorToInt(Anchor.X/SiteSpacing),FMath::FloorToInt(Anchor.Y/SiteSpacing));
        for(int32 Y=-Reach;Y<=Reach;++Y) for(int32 X=-Reach;X<=Reach;++X)
        {
            const FIntPoint Key=C+FIntPoint(X,Y);
            if(Field.Sites.Contains(Key)) continue;
            FPuddleSite S;
            S.Presence=PuddleHash(Key.X,Key.Y);
            S.Spot=FVector2D((Key.X+.15f+.7f*PuddleHash(Key.X+311,Key.Y-97))*SiteSpacing,(Key.Y+.15f+.7f*PuddleHash(Key.X-53,Key.Y+701))*SiteSpacing);
            if(FVector2D::Distance(S.Spot,FVector2D(Anchor))>SiteReach) continue;
            const float SizeRoll=PuddleHash(Key.X+911,Key.Y-377), SizeIn=PuddleHash(Key.X-271,Key.Y+433);
            int32 SizeClass=0;
            if(SizeRoll<.5f)       { S.MaxRadius=FMath::Lerp(35.f,70.f,SizeIn);  S.GrowSeconds=15.f; }
            else if(SizeRoll<.88f) { S.MaxRadius=FMath::Lerp(80.f,150.f,SizeIn); S.GrowSeconds=60.f; SizeClass=1; }
            else                   { S.MaxRadius=FMath::Lerp(180.f,350.f,SizeIn);S.GrowSeconds=240.f; SizeClass=2; }
            S.Aspect=FMath::Lerp(.7f,1.f,PuddleHash(Key.X+57,Key.Y+59));
            S.Angle=PI*PuddleHash(Key.X-613,Key.Y-89);
            MakePuddleLobes(S,Key,SizeClass); // r724
            // Spots that could never fill are settled without a trace.
            if(S.Presence>=MaxDensity*PuddleGroundShare(1.f)) { S.bTested=true; S.bValid=false; }
            Field.Sites.Add(Key,MoveTemp(S));
        }
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RainGroundWater),true,GetOwner()); Query.AddIgnoredActor(Pawn);
    const FCollisionObjectQueryParams Static(ECC_WorldStatic);
    int32 Budget=200;
    // r682: nearest spots first, so the ones you can see best are found first.
    TArray<FPuddleSite*> Pending;
    for(auto& Pair:Field.Sites)
        if(!Pair.Value.bTested && Pair.Value.Presence<=Density*PuddleGroundShare(1.f)+.08f) Pending.Add(&Pair.Value); // only spots this rain can fill
    if(Pending.Num()>1)
        Pending.Sort([&Anchor](const FPuddleSite& A,const FPuddleSite& B){return FVector2D::DistSquared(A.Spot,FVector2D(Anchor))<FVector2D::DistSquared(B.Spot,FVector2D(Anchor));});
    for(FPuddleSite* Site:Pending)
    {
        FPuddleSite& S=*Site;
        if(Budget<=0) break;
        if(S.Probe==0)
        {
            Budget-=3;
            FHitResult Hit,Roof;
            const FVector At(S.Spot.X,S.Spot.Y,Anchor.Z);
            S.bTested=true; S.bValid=false;
            if(!World->LineTraceSingleByObjectType(Hit,At+FVector(0,0,700),At-FVector(0,0,1300),Static,Query)||!PuddleGround(Hit,.985f)) continue; // r682: <10 degrees
            const bool bTerrain=Hit.GetComponent()&&Hit.GetComponent()->GetFName().ToString().StartsWith(TEXT("TerrainMesh"));
            // Open sky only (building floors look 60 m up: Stormwind's far keep hangs over the city, r587).
            if(World->LineTraceSingleByObjectType(Roof,Hit.ImpactPoint+FVector(0,0,4),Hit.ImpactPoint+FVector(0,0,bTerrain?12000:6000),Static,Query)) continue;
            TArray<FOverlapResult> Waters;
            World->OverlapMultiByObjectType(Waters,Hit.ImpactPoint+FVector(0,0,3),FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldDynamic),FCollisionShape::MakeSphere(2.f),Query);
            bool InWater=false; for(const auto& W:Waters) if(W.GetComponent()&&W.GetComponent()->ComponentHasTag(TEXT("WowWaterVolume"))) InWater=true;
            if(InWater) continue;
            S.Centre=Hit.ImpactPoint; S.Heights.Init(0.f,SiteVertices); S.Probe=1;
            {
                // r684: building floors (Stormwind's streets and squares) count as stone; unknown ground as half.
                const float Ground=bTerrain ? WowTerrainPuddleGroundAt(Hit.ImpactPoint) : 1.f;
                S.Ground=Ground<0.f ? .5f : Ground;
                if(S.Presence>Density*PuddleGroundShare(S.Ground)+.08f && S.Presence>=MaxDensity*PuddleGroundShare(S.Ground))
                { S.bTested=true; S.bValid=false; continue; } // this ground never fills here
            }
            S.bTested=false; // the outline is traced next
        }
        // The outline must lie on the same level ground. r682 (Stefan: "they can't be in downhill slopes, only flat
        // to semi-flat angles"): every outline point within about 3 degrees of the centre's height (was 6 cm + 2.3
        // degrees, and the centre itself could lean 16 degrees), its ground under 14 degrees, and the pool must lie
        // in a dip or on the level - the centre no higher than the outline around it.
        const float Tolerance=2.f+S.MaxRadius*.05f;
        while(Budget>0 && S.Probe<SiteVertices)
        {
            --Budget;
            const FVector2D O=PuddleVertexOffset(S,S.Probe,S.MaxRadius);
            const FVector At(S.Centre.X+O.X,S.Centre.Y+O.Y,S.Centre.Z);
            FHitResult Hit;
            if(!World->LineTraceSingleByObjectType(Hit,At+FVector(0,0,150),At-FVector(0,0,150),Static,Query)||!PuddleGround(Hit,.97f)
                ||FMath::Abs(Hit.ImpactPoint.Z-S.Centre.Z)>Tolerance)
            { S.bTested=true; S.bValid=false; S.Probe=SiteVertices; break; }
            S.Heights[S.Probe]=float(Hit.ImpactPoint.Z-S.Centre.Z);
            if(++S.Probe==SiteVertices)
            {
                float Rim=0.f; for(int32 K=0;K<RingSegments;++K) Rim+=S.Heights[1+RingSegments+K];
                S.bTested=true; S.bValid=Rim/RingSegments>=-2.f; // r682: not on a crest or a slope's bulge
                // r682: a pool found out at range has been there as long as the rain: no growing in front of you.
                S.Grow=FMath::Min(1.f,Field.RainSeconds/S.GrowSeconds);
            }
        }
    }
    GroundWaterDrawTime+=Dt; if(GroundWaterDrawTime<.1f) return;
    const float DrawDt=FMath::Min(GroundWaterDrawTime,1.f); GroundWaterDrawTime=0;
    Field.RainSeconds=Rain>.005f ? Field.RainSeconds+DrawDt : FMath::Max(0.f,Field.RainSeconds-DrawDt*2.f);
    if(!GroundWaterMesh)
    {
        auto* Material=PrepareGroundWaterMaterial(GetOwner()); if(!Material) return;
        GroundWaterMesh=NewObject<UProceduralMeshComponent>(GetOwner());
        GetOwner()->AddInstanceComponent(GroundWaterMesh);
        GroundWaterMesh->SetMobility(EComponentMobility::Movable);
        GroundWaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        GroundWaterMesh->SetCanEverAffectNavigation(false);
        GroundWaterMesh->SetCastShadow(false);
        GroundWaterMesh->RegisterComponent(); GroundWaterMesh->SetMaterial(0,Material);
        GroundWaterMesh->CreateDynamicMaterialInstance(0,Material);
    }
    if(auto* Water=Cast<UMaterialInstanceDynamic>(GroundWaterMesh->GetMaterial(0)))
    {
        Water->SetScalarParameterValue(TEXT("RainRipples"),FMath::Clamp(Rain*1.5f,0.f,1.f));
        // The sky the puddles mirror: the zone's fog colour is the horizon sky colour.
        FLinearColor SkyColor(.45f,.5f,.55f);
        if(const auto* Fog=GetOwner()->FindComponentByClass<UExponentialHeightFogComponent>())
            SkyColor=Fog->FogInscatteringLuminance;
        const float Peak=FMath::Max3(SkyColor.R,SkyColor.G,SkyColor.B);
        if(Peak>2.f) SkyColor*=2.f/Peak;
        // r724 (Stefan's Stormwind screenshots at night in the rain: the pools read as pale grey plates on the dark
        // street): the mirrored sky is 30 % dimmer, so a pool is a dark, wet mirror rather than a light patch.
        SkyColor*=.7f;
        SkyColor.R=FMath::Max(SkyColor.R,.01f); SkyColor.G=FMath::Max(SkyColor.G,.01f); SkyColor.B=FMath::Max(SkyColor.B,.012f);
        Water->SetVectorParameterValue(TEXT("SkyColor"),SkyColor);
    }
    TArray<FVector> V,N; TArray<FVector2D> GroundUV; TArray<FLinearColor> GroundColors; TArray<int32> Indices;
    PuddlePoints.Reset();
    int32 Shown=0, Valid=0;
    // r724: the pools to draw this time, each with its lobes' current size and weight.
    struct FShownPool
    {
        FPuddleSite* S=nullptr; FIntPoint Key; float Reach=0.f;
        TArray<float,TInlineAllocator<4>> Size, Weight;
    };
    TArray<FShownPool> Pools;
    for(auto& Pair:Field.Sites)
    {
        FPuddleSite& S=Pair.Value;
        if(!S.bValid) continue;
        ++Valid;
        // Filling: a spot holds water while the wetness share is above its own threshold (soft over 6 %).
        const float Wet=FMath::Clamp((Density*PuddleGroundShare(S.Ground)-S.Presence)/.06f,0.f,1.f); // r684: by ground
        S.Grow=Wet>0.f ? FMath::Min(1.f,S.Grow+DrawDt/S.GrowSeconds) : FMath::Max(0.f,S.Grow-DrawDt/20.f);
        if(S.Grow<=0.f && Wet<=0.f) continue;
        const float Fade=FMath::Clamp((FadeStart+FadeLength-float(FVector::DistXY(S.Centre,Anchor)))/FadeLength,0.f,1.f);
        const float Alpha=FMath::Max(Wet,S.Grow*.999f)*Fade;
        if(Alpha<.02f || S.Lobes.IsEmpty()) continue;
        ++Shown;
        FShownPool Pool; Pool.S=&S; Pool.Key=Pair.Key; Pool.Reach=S.MaxRadius*1.1f;
        for(int32 L=0;L<S.Lobes.Num();++L)
        {
            const FPuddleLobe& Lobe=S.Lobes[L];
            const float G=Lobe.Delay<=0.f ? S.Grow : FMath::Clamp((S.Grow-Lobe.Delay)/(1.f-Lobe.Delay),0.f,1.f);
            const float Size=(.25f+.75f*G)*(.6f+.4f*Fill);
            const float Weight=Alpha*(Lobe.Delay<=0.f ? 1.f : FMath::Clamp((S.Grow-Lobe.Delay)/.1f,0.f,1.f));
            Pool.Size.Add(Size); Pool.Weight.Add(Weight);
            // r683: the splash reach is the drawn water (~0.85 of the outline) plus a foot's width (20 cm).
            if(Weight>.3f)
            {
                const FVector2D O=FVector2D(Lobe.Offset.X*S.MaxRadius,Lobe.Offset.Y*S.MaxRadius*S.Aspect).GetRotated(FMath::RadiansToDegrees(S.Angle));
                PuddlePoints.Add(FVector4f(float(S.Centre.X+O.X),float(S.Centre.Y+O.Y),float(S.Centre.Z),Lobe.Radius*Size*S.MaxRadius*.85f+20.f));
            }
        }
        Pools.Add(MoveTemp(Pool));
    }
    // Pools that can reach each other form one cluster, drawn as one field so they merge.
    TArray<int32> Parent; Parent.SetNumUninitialized(Pools.Num());
    for(int32 I=0;I<Pools.Num();++I) Parent[I]=I;
    auto Root=[&Parent](int32 I){ while(Parent[I]!=I) { Parent[I]=Parent[Parent[I]]; I=Parent[I]; } return I; };
    {
        TMap<FIntPoint,int32> ByKey; ByKey.Reserve(Pools.Num());
        for(int32 I=0;I<Pools.Num();++I) ByKey.Add(Pools[I].Key,I);
        const int32 KeyReach=FMath::CeilToInt(2.f*350.f*1.1f/SiteSpacing);
        for(int32 I=0;I<Pools.Num();++I)
            for(int32 DY=-KeyReach;DY<=KeyReach;++DY) for(int32 DX=-KeyReach;DX<=KeyReach;++DX)
            {
                const int32* J=ByKey.Find(Pools[I].Key+FIntPoint(DX,DY));
                if(!J || *J<=I) continue;
                if(FVector::DistXY(Pools[I].S->Centre,Pools[*J].S->Centre)<Pools[I].Reach+Pools[*J].Reach
                    && FMath::Abs(Pools[I].S->Centre.Z-Pools[*J].S->Centre.Z)<40.0)
                    Parent[Root(*J)]=Root(I);
            }
    }
    TMap<int32,TArray<int32>> Clusters;
    for(int32 I=0;I<Pools.Num();++I) Clusters.FindOrAdd(Root(I)).Add(I);
    TArray<TArray<int32>> Ordered; Ordered.Reserve(Clusters.Num());
    for(auto& Pair:Clusters) Ordered.Add(MoveTemp(Pair.Value));
    auto ClusterDistance=[&Pools,&Anchor](const TArray<int32>& C)
    {
        double Best=TNumericLimits<double>::Max();
        for(const int32 I:C) Best=FMath::Min(Best,FVector::DistXY(Pools[I].S->Centre,Anchor)-Pools[I].Reach);
        return Best;
    };
    // A stable order (by the clusters' first spot), so the mesh keeps its layout from one update to the next and
    // is only updated, not rebuilt, while you walk.
    auto FirstKey=[&Pools](const TArray<int32>& C)
    {
        FIntPoint Best(MAX_int32,MAX_int32);
        for(const int32 I:C) if(Pools[I].Key.Y<Best.Y || (Pools[I].Key.Y==Best.Y && Pools[I].Key.X<Best.X)) Best=Pools[I].Key;
        return Best;
    };
    Ordered.Sort([&FirstKey](const TArray<int32>& A,const TArray<int32>& B)
    {
        const FIntPoint KA=FirstKey(A), KB=FirstKey(B);
        return KA.Y<KB.Y || (KA.Y==KB.Y && KA.X<KB.X);
    });
    int32 Merged=0;
    constexpr int32 MaxVertices=150000;
    TArray<int32> GridIndex;
    for(const TArray<int32>& Cluster:Ordered)
    {
        const double Distance=ClusterDistance(Cluster);
        if(V.Num()>=MaxVertices && Distance>2000.0) continue; // over budget: only the nearest still drawn
        if(Cluster.Num()>1) ++Merged;
        // A finer grid close by (smooth shores), coarser far away. World-aligned, so it does not swim as you move.
        const double Cell=Distance<2000.0 ? 20.0 : Distance<4500.0 ? 35.0 : 60.0;
        FBox2D Box(ForceInit);
        for(const int32 I:Cluster) Box+=FBox2D(FVector2D(Pools[I].S->Centre)-FVector2D(Pools[I].Reach),FVector2D(Pools[I].S->Centre)+FVector2D(Pools[I].Reach));
        const int64 X0=FMath::FloorToInt64(Box.Min.X/Cell), Y0=FMath::FloorToInt64(Box.Min.Y/Cell);
        const int32 NX=FMath::Clamp(int32(FMath::CeilToInt64(Box.Max.X/Cell)-X0)+1,2,220);
        const int32 NY=FMath::Clamp(int32(FMath::CeilToInt64(Box.Max.Y/Cell)-Y0)+1,2,220);
        // Per grid point: the field, the ground height (weighted by each pool's share) and whether it lies on a
        // pool's traced ground at all (cells with no such corner are left out, so the mesh keeps its shape while
        // the pools grow and shrink and only the fill changes).
        TArray<float> Fills; Fills.SetNumZeroed(NX*NY);
        TArray<double> Heights; Heights.SetNumZeroed(NX*NY);
        TArray<bool> OnGround; OnGround.SetNumZeroed(NX*NY);
        for(int32 GY=0;GY<NY;++GY) for(int32 GX=0;GX<NX;++GX)
        {
            const FVector2D Pt(double(X0+GX)*Cell,double(Y0+GY)*Cell);
            float Total=0.f; double ZSum=0.0, ZWeight=0.0, NearestRho=TNumericLimits<double>::Max(), NearestZ=0.0;
            bool bGround=false;
            for(const int32 I:Cluster)
            {
                const FShownPool& Pool=Pools[I]; const FPuddleSite& S=*Pool.S;
                const FVector2D U=PuddleSpotFrame(S,Pt);
                const double Rho=U.Size();
                if(Rho>1.1) continue;
                bGround=true;
                const double Z=S.Centre.Z+PuddleGroundHeight(S,U);
                if(Rho<NearestRho) { NearestRho=Rho; NearestZ=Z; }
                float Own=0.f;
                for(int32 L=0;L<S.Lobes.Num();++L) Own+=Pool.Weight[L]*LobeField(S.Lobes[L],U,Pool.Size[L]);
                if(Own>0.f) { Total+=Own; ZSum+=Z*Own; ZWeight+=Own; }
            }
            const int32 Index=GY*NX+GX;
            Fills[Index]=FMath::Min(Total,1.f);
            Heights[Index]=ZWeight>0.0 ? ZSum/ZWeight : NearestZ;
            OnGround[Index]=bGround;
        }
        GridIndex.Init(INDEX_NONE,NX*NY);
        auto Vertex=[&](int32 GX,int32 GY)
        {
            const int32 Index=GY*NX+GX;
            if(GridIndex[Index]!=INDEX_NONE) return GridIndex[Index];
            // r682: 3 cm over the ground, above the material's 2 cm depth fade.
            const FVector P(double(X0+GX)*Cell,double(Y0+GY)*Cell,Heights[Index]+3.0);
            GridIndex[Index]=V.Num();
            V.Add(P-Anchor); N.Add(FVector::UpVector);
            GroundUV.Add(FVector2D(P.X*.01,P.Y*.01));
            GroundColors.Add(FLinearColor(.5f,.5f,0.f,Fills[Index]));
            return GridIndex[Index];
        };
        for(int32 GY=0;GY+1<NY;++GY) for(int32 GX=0;GX+1<NX;++GX)
        {
            const int32 A=GY*NX+GX, B=A+1, C=A+NX, D=C+1;
            if(!(OnGround[A]||OnGround[B]||OnGround[C]||OnGround[D])) continue;
            const int32 VA=Vertex(GX,GY), VB=Vertex(GX+1,GY), VC=Vertex(GX,GY+1), VD=Vertex(GX+1,GY+1);
            // Same winding as the other water surfaces (the material is two-sided anyway).
            Indices.Append({VA,VC,VB, VB,VC,VD});
        }
    }
    GroundWaterMesh->SetWorldLocation(Anchor);
    if(!Indices.IsEmpty())
    {
        const auto* Section=GroundWaterMesh->GetProcMeshSection(0);
        if(!Section||Section->ProcVertexBuffer.Num()!=V.Num()||GroundWaterIndices!=Indices)
        {
            GroundWaterMesh->ClearAllMeshSections();
            GroundWaterMesh->CreateMeshSection_LinearColor(0,V,Indices,N,GroundUV,GroundColors,{},false);
            GroundWaterIndices=Indices;
        }
        else GroundWaterMesh->UpdateMeshSection_LinearColor(0,V,N,GroundUV,GroundColors,{});
    }
    GroundWaterMesh->SetVisibility(!Indices.IsEmpty());
    if(Now>=NextGroundWaterDiagnostic)
    {
        NextGroundWaterDiagnostic=Now+10;
        UE_LOG(LogTemp,Log,TEXT("Rain puddles: wetness=%.2f density=%.2f spots=%d level=%d shown=%d clusters=%d merged=%d vertices=%d triangles=%d material=%s"),
            SurfaceWetness,Density,Field.Sites.Num(),Valid,Shown,Ordered.Num(),Merged,V.Num(),Indices.Num()/3,*GetNameSafe(GroundWaterMesh->GetMaterial(0)));
    }
}
