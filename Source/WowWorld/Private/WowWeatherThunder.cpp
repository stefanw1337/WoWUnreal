#include "WowWeatherComponent.h"
#include "WowAudioManager.h"
#include "WowSkyManager.h"
#include "ProceduralMeshComponent.h"
#include "WowWorldManager.h"
#include "WowDoodadManager.h"
#include "WowTerrainMaterial.h"
#include "WowM2ParticleComponent.h"
#include "Formats/Dbc/DbcStore.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PointLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
// The storm bolt reuses the texture of the shaman Lightning Bolt (spell 403) missile, so the
// sky's lightning reads exactly like the spell: its ribbon texture, else a lightning-named one.
UTexture2D* ResolveLightningBoltTexture(const UObject* Context)
{
    AWowWorldManager* World = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(Context, AWowWorldManager::StaticClass()));
    FMpqManager* Mpq = World ? World->GetMpqManager() : nullptr;
    FWowAssetCache* Cache = World ? World->GetAssetCache() : nullptr;
    if (!Mpq || !Cache) return nullptr;
    const FDbcStore& Store = FDbcStore::Get();
    TArray<FString> Models;
    for (uint32 SpellId : {403u, 421u}) // Lightning Bolt, Chain Lightning
    {
        const auto* Spell = Store.Spells().GetById(SpellId);
        const auto* Visual = Spell ? Store.SpellVisuals().GetById(Spell->SpellVisual[0] ? Spell->SpellVisual[0] : Spell->SpellVisual[1]) : nullptr;
        const auto* Model = Visual && Visual->MissileModel > 0 ? Store.SpellVisualEffectNames().GetById(uint32(Visual->MissileModel)) : nullptr;
        if (Model && !Model->FilePath.IsEmpty()) Models.Add(Model->FilePath);
    }
    for (const FString& Path : Models)
    {
        const TSharedPtr<FM2Data> Data = FWowDoodadManager::GetOrParseM2(Path, Mpq);
        if (!Data.IsValid() || !Data->bIsValid) continue;
        FString Chosen;
        for (const FM2RibbonEmitter& Ribbon : Data->RibbonEmitters)
            if (Data->TexturePaths.IsValidIndex(Ribbon.Texture) && !Data->TexturePaths[Ribbon.Texture].IsEmpty()) { Chosen = Data->TexturePaths[Ribbon.Texture]; break; }
        if (Chosen.IsEmpty())
            for (const FString& Texture : Data->TexturePaths)
                if (Texture.Contains(TEXT("lightning")) || Texture.Contains(TEXT("electric"))) { Chosen = Texture; break; }
        if (Chosen.IsEmpty()) continue;
        if (UTexture2D* Texture = FWowTerrainMaterial::LoadBlpTexture(Chosen, Mpq, Cache))
        {
            UE_LOG(LogTemp, Log, TEXT("Storm lightning uses the spell texture %s (from %s)"), *Chosen, *Path);
            return Texture;
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("Storm lightning: no Lightning Bolt texture found, using the plain bolt"));
    return nullptr;
}

// Jagged path by midpoint displacement: big kinks at the top level, fine crackle below.
void BuildBoltPath(FRandomStream& Random, const FVector& From, const FVector& To, int32 Depth, float Roughness, TArray<FVector>& Out)
{
    Out = {From, To};
    for (int32 Level = 0; Level < Depth; ++Level)
    {
        TArray<FVector> Next; Next.Reserve(Out.Num() * 2);
        for (int32 I = 0; I + 1 < Out.Num(); ++I)
        {
            const FVector A = Out[I], B = Out[I + 1];
            const float Length = float(FVector::Dist(A, B));
            const FVector Axis = (B - A).GetSafeNormal();
            FVector Offset = Random.GetUnitVector(); Offset -= Axis * FVector::DotProduct(Offset, Axis);
            Next.Add(A);
            Next.Add((A + B) * 0.5 + Offset.GetSafeNormal() * Length * Roughness * Random.FRandRange(0.3f, 1.0f));
        }
        Next.Add(Out.Last());
        Out = MoveTemp(Next);
    }
}
}

void UWowWeatherComponent::CreateLightning(const FVector& Camera, const FVector& CameraRight)
{
    if(!LightningMesh)
    {
        LightningMesh=NewObject<UProceduralMeshComponent>(GetOwner(),TEXT("WeatherLightning"));
        LightningMesh->SetupAttachment(GetOwner()->GetRootComponent());
        LightningMesh->SetAbsolute(true,true,true);
        LightningMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        LightningMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
        LightningMesh->ComponentTags.AddUnique(TEXT("WowNoCameraFocus"));
        LightningMesh->SetCastShadow(false);
        LightningMesh->bAffectDistanceFieldLighting=false;
        LightningMesh->bAffectDynamicIndirectLighting=false;
        LightningMesh->bVisibleInReflectionCaptures=false;
        LightningMesh->bVisibleInRealTimeSkyCaptures=false;
        LightningMesh->RegisterComponent();
        LightningMesh->SetMaterial(0,PrepareMaterial(this));
        if(!bResolvedBoltTexture)
        {
            bResolvedBoltTexture=true;
            UTexture2D* BoltTexture=ResolveLightningBoltTexture(this);
            UMaterial* Additive=BoltTexture?UWowM2ParticleComponent::GetAdditiveEmitterMaterial():nullptr;
            if(Additive)
            {
                LightningBoltMaterial=UMaterialInstanceDynamic::Create(Additive,this);
                LightningBoltMaterial->SetTextureParameterValue(TEXT("EffectTexture"),BoltTexture);
                LightningBoltMaterial->SetScalarParameterValue(TEXT("UseTextureAlpha"),0.f);
                LightningBoltMaterial->SetScalarParameterValue(TEXT("Strength"),6.f);
            }
        }
        if(LightningBoltMaterial) LightningMesh->SetMaterial(1,LightningBoltMaterial);
        LightningLight=NewObject<UPointLightComponent>(GetOwner(),TEXT("WeatherLightningFlash"));
        LightningLight->SetupAttachment(GetOwner()->GetRootComponent());
        LightningLight->SetAbsolute(true,true,true);
        LightningLight->SetMobility(EComponentMobility::Movable);
        LightningLight->SetCastShadows(false);
        LightningLight->SetIndirectLightingIntensity(0.f);
        LightningLight->SetVolumetricScatteringIntensity(0.3f);
        LightningLight->bUseInverseSquaredFalloff=false;
        LightningLight->SetAttenuationRadius(65000.f);
        LightningLight->SetLightColor(FLinearColor(0.70f,0.80f,1.f));
        LightningLight->SetIntensity(0.f);
        LightningLight->RegisterComponent();
    }
    // Put most strikes in the visible sky, with some genuinely off-screen thunder.
    const float ViewAngle=FMath::Atan2(-CameraRight.X,CameraRight.Y);
    const float Angle=ViewAngle+FMath::DegreesToRadians(StormRandom.FRandRange(-65.f,65.f));
    const float Distance=StormRandom.FRandRange(9000.f,28000.f);
    const FVector Base=Camera+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0);
    FVector End=Base-FVector(0,0,2000);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(WeatherLightningGround),true);
    Params.AddIgnoredActor(GetOwner());
    if(APawn* Pawn=UGameplayStatics::GetPlayerPawn(this,0)) Params.AddIgnoredActor(Pawn);
    FHitResult Hit;
    if(GetWorld()->LineTraceSingleByObjectType(Hit,Base+FVector(0,0,12000),Base-FVector(0,0,25000),FCollisionObjectQueryParams(ECC_WorldStatic),Params))
        End=Hit.ImpactPoint;
    LightningPosition=End;
    LightningMesh->SetWorldLocation(End);
    // Colour: electric blue by day; at night storms also flash violet, purple or pink.
    float Minutes=720.f;
    if(const AWowSkyManager* Sky=Cast<AWowSkyManager>(GetOwner())) Minutes=Sky->GetTimeOfDay();
    const bool bNight=Minutes<330.f||Minutes>1230.f;
    static const FLinearColor DayHue(0.42f,0.58f,1.0f);
    static const FLinearColor NightHues[]={FLinearColor(0.42f,0.58f,1.0f),FLinearColor(0.58f,0.42f,1.0f),
        FLinearColor(0.78f,0.36f,1.0f),FLinearColor(1.0f,0.40f,0.86f)};
    const FLinearColor Hue=bNight?NightHues[StormRandom.RandRange(0,UE_ARRAY_COUNT(NightHues)-1)]:DayHue;
    const FLinearColor Core=FMath::Lerp(Hue,FLinearColor::White,0.35f)*14.f;
    const FLinearColor Glow=FLinearColor(Hue.R*5.f,Hue.G*5.f,Hue.B*5.f,0.45f);
    LightningLight->SetLightColor(FMath::Lerp(Hue,FLinearColor::White,0.25f));
    const FVector Top=Base+FVector(0,0,StormRandom.FRandRange(12000.f,22000.f));
    // Section 0: soft coloured glow. Section 1: the bolt itself, textured with the
    // Lightning Bolt spell's ribbon texture (U along the bolt, V across), tapering
    // towards the ground and on every branch. Without the spell texture the core
    // falls back to the plain hot quads in section 0.
    TArray<FVector> GV,GN,BV,BN;
    TArray<FVector2D> GT,BT;
    TArray<FLinearColor> GC,BC;
    TArray<int32> GI,BI;
    const bool bTextured=LightningBoltMaterial!=nullptr;
    const auto Ribbon=[&](const TArray<FVector>& Path,float WidthTop,float WidthEnd,const FLinearColor& Colour,bool bGlow)
    {
        TArray<FVector>& V=bGlow?GV:BV; TArray<FVector>& N=bGlow?GN:BN; TArray<FVector2D>& T=bGlow?GT:BT;
        TArray<FLinearColor>& C=bGlow?GC:BC; TArray<int32>& I=bGlow?GI:BI;
        float U=StormRandom.FRand();
        for(int32 K=0;K+1<Path.Num();++K)
        {
            const FVector A=Path[K],B=Path[K+1];
            const float S0=float(K)/(Path.Num()-1),S1=float(K+1)/(Path.Num()-1);
            const float W0=FMath::Lerp(WidthTop,WidthEnd,S0),W1=FMath::Lerp(WidthTop,WidthEnd,S1);
            FVector Side=FVector::CrossProduct(B-A,Camera-(A+B)*0.5).GetSafeNormal();
            if(Side.IsNearlyZero()) Side=CameraRight;
            const float U1=U+float(FVector::Dist(A,B))/FMath::Max(W0*8.f,1.f);
            const int32 Index=V.Num();
            V.Append({A-End-Side*W0,A-End+Side*W0,B-End+Side*W1,B-End-Side*W1});
            N.Append({FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
            if(bGlow) T.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
            else T.Append({FVector2D(U,0),FVector2D(U,1),FVector2D(U1,1),FVector2D(U1,0)});
            for(int32 J=0;J<4;++J) C.Add(Colour);
            I.Append({Index,Index+1,Index+2,Index,Index+2,Index+3});
            U=U1;
        }
    };
    const FLinearColor BoltColour=FMath::Lerp(Hue,FLinearColor::White,0.2f);
    const auto Bolt=[&](const TArray<FVector>& Path,float WidthTop,float WidthEnd)
    {
        Ribbon(Path,WidthTop*4.5f,WidthEnd*4.5f,Glow,true);
        if(bTextured) Ribbon(Path,WidthTop*1.6f,WidthEnd*1.6f,BoltColour,false);
        else Ribbon(Path,WidthTop,WidthEnd,Core,true);
    };
    TArray<FVector> Main;
    BuildBoltPath(StormRandom,Top,End,6,0.22f,Main);
    Bolt(Main,130.f,70.f);
    // Two to four forks leave the upper two thirds and die out in mid-air.
    const int32 Forks=StormRandom.RandRange(2,4);
    for(int32 F=0;F<Forks;++F)
    {
        const int32 From=StormRandom.RandRange(Main.Num()/8,Main.Num()*2/3);
        const FVector Start=Main[From];
        const float Remaining=float(FVector::Dist(Start,End));
        FVector Dir=(End-Start).GetSafeNormal()+StormRandom.GetUnitVector()*0.9f; Dir.Z=-FMath::Abs(Dir.Z)-0.3f;
        TArray<FVector> Fork;
        BuildBoltPath(StormRandom,Start,Start+Dir.GetSafeNormal()*Remaining*StormRandom.FRandRange(0.2f,0.45f),4,0.25f,Fork);
        Bolt(Fork,60.f,6.f);
    }
    LightningMesh->ClearAllMeshSections();
    LightningMesh->CreateMeshSection_LinearColor(0,GV,GI,GN,GT,GC,{},false);
    if(bTextured) LightningMesh->CreateMeshSection_LinearColor(1,BV,BI,BN,BT,BC,{},false);
    LightningLight->SetWorldLocation(Base+FVector(0,0,7000));
    FlashAge=0;
    ThunderAt=FPlatformTime::Seconds()+FVector::Distance(Camera,End)/34300.f;
    ThunderStrength=FMath::Lerp(0.9f,0.4f,(Distance-9000.f)/19000.f)*FMath::Lerp(.45f,1.f,Current.Intensity);
}

void UWowWeatherComponent::UpdateThunder(float Dt)
{
    const double Now=FPlatformTime::Seconds();
    APlayerController* PC=UGameplayStatics::GetPlayerController(this,0);
    if(!PC) return;
    FVector Camera; FRotator Rotation; PC->GetPlayerViewPoint(Camera,Rotation);
    const bool Storm=Current.Kind==EWowWeatherKind::Storm&&Current.Intensity>0.005f;
    if(Storm && !bThunderstormActive) NextLightning=Now+StormRandom.FRandRange(5.f,9.f);
    bThunderstormActive=Storm;
    if(Storm && Now>=NextLightning && Zone!=0)
    {
        CreateLightning(Camera,Rotation.RotateVector(FVector::RightVector));
        // Strength drives the rate: a light storm every ~25 s, a full (100%) storm every ~4 s,
        // and the test range above 100% faster still.
        const float Strength=FMath::Max(Current.Intensity,0.f);
        NextLightning=Now+StormRandom.FRandRange(16.f,30.f)*FMath::Lerp(1.6f,0.17f,FMath::Clamp(Strength,0.f,1.f))
            /(1.f+FMath::Max(0.f,Strength-1.f));
    }
    else if(!Storm) NextLightning=Now+StormRandom.FRandRange(6.f,14.f);
    FlashAge+=Dt;
    const float Flash=GetLightningFlash();
    if(LightningMesh) LightningMesh->SetVisibility(Flash>0.f);
    if(LightningLight) LightningLight->SetIntensity(Flash*7.f*FMath::Lerp(.3f,1.f,Current.Intensity));
    if(ThunderAt>0 && Now>=ThunderAt)
    {
        if(!Audio.IsValid()) Audio=Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(this,AWowAudioManager::StaticClass()));
        if(Audio.IsValid()) Audio->PlayWeatherThunder(ThunderStrength*FMath::Lerp(0.25f,1.f,Exposure));
        ThunderAt=0;
    }
}
