#include "WowWeatherComponent.h"
#include "WowClothMaterial.h"
#include "Misc/ScopeExit.h"
#include "WowClientPreferences.h"
#include "WowTerrainMaterial.h"
#include "WowWaterRenderer.h"
#include "WowWaterMaterial.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "WowIndoorFogMask.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "WowTestControls.h"
#include "WowWeatherParticlePolicy.h"
#include "WowSkyManager.h"
#include "WowWorldManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowAudioManager.h"
#include "WowTumbleweedComponent.h"
#include "WowCookedMaterial.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowWeather,Log,All);
namespace {
TAutoConsoleVariable<int32> WeatherPreview(TEXT("wow.WeatherPreview"),0,TEXT("Session-only weather preview: 0 automatic, 1 clear, 2 overcast, 3 rain, 4 thunderstorm, 5 snow, 6 blizzard, 7 sand, 8 ash, 9 fog."));
// r534: Options > Test "Mix weather" checkboxes (with wow.WeatherPreview 10): 1 overcast, 2 fog, 4 rain,
// 8 thunderstorm, 16 snow, 32 sandstorm, 64 ash. One kind of precipitation at most; fog and overcast mix with any.
TAutoConsoleVariable<int32> WeatherPreviewMix(TEXT("wow.WeatherPreviewMix"),0,TEXT("Mixed weather preview bits: 1 overcast, 2 fog, 4 rain, 8 thunderstorm, 16 snow, 32 sand, 64 ash."));
TAutoConsoleVariable<float> WeatherPreviewStrength(TEXT("wow.WeatherPreviewStrength"),0.7f,TEXT("Preview weather strength: 0 to 1 normal, 1 to 2 extreme test range."));
constexpr int32 DropCount=4096, Grid=12; // 1.8 m cells: a doorway or a small roof is resolved
constexpr float Radius=1100, CellSize=Radius*2/Grid;
const TCHAR* Prefs=TEXT("WowUnreal.ClientPreferences");
// Ash weather embers (r228, Stefan: more of them, with fire light): every 9th ash flake is a
// glowing ember, and the closest few carry a small flickering orange light.
constexpr int32 EmberEvery=9, EmberLightCount=6;
TArray<TWeakObjectPtr<UPointLightComponent>> GEmberLights;
void HideEmberLights() { for(auto& L:GEmberLights) if(L.IsValid()) L->SetVisibility(false); }
bool IsRain(EWowWeatherKind K) { return K==EWowWeatherKind::Rain||K==EWowWeatherKind::Storm||K==EWowWeatherKind::BlackRain; }
bool IsSnow(EWowWeatherKind K) { return K==EWowWeatherKind::Snow; }
FString CacheFile() { return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("WeatherCurrent.json")); }
// r575 (Stefan, Stormwind in rain: "we should have seen rain where the roof ends and then outwards for about
// 20 yards maybe more", and from a high open view further still): the near drops fill only a ~22 m box around
// the camera. A sparse far layer (mesh section 2) fills a 120 m box around it; its streaks grow with distance
// so they stay about a pixel wide, and they start where the near box ends. A coarse roof grid (5 m cells, traced
// a few per frame) keeps them out of buildings, so from inside a hall they fall only beyond the door.
// r585 (Stefan: the far rain is still not as much as the near rain at the deck edge): a denser, closer far layer
// (9000 streaks in a 90 m box instead of 6000 in 120 m) with longer streaks, so rain 10-40 m away reads like
// the near rain does.
constexpr int32 FarDropCount=9000, FarGrid=24;
constexpr float FarRadius=4500, FarCell=FarRadius*2/FarGrid, FarBottom=-2500, FarTop=3500;
struct FFarRoofCell { FIntPoint Key=FIntPoint(MAX_int32,MAX_int32); bool bRoof=false; };
TArray<FVector> GFarDrops;
TArray<FFarRoofCell> GFarRoof;
TArray<FVector> GFarVertices, GFarNormals;
TArray<FVector2D> GFarUV;
TArray<FLinearColor> GFarColors;
int32 GFarRoofIndex=0;
bool GFarShown=false;
// r576 (Stefan 15.10, walking over the bridge between Elwynn and Stormwind: "the weather is changing FAST on me,
// from raining to foggy ... you can see the entire change from the skies", within seconds): each zone has its
// own weather, and crossing the border blended to the other zone's at the normal rates (clouds 0.07/s, about
// ten seconds for the whole sky). For 90 s after walking over a zone border the blend runs at a quarter of
// that speed, so the sky turns over about a minute; walking back and forth no longer flips it.
double GZoneBlendUntil=0.0;
int32 FarRoofSlot(const FIntPoint& Key) { return ((Key.Y%FarGrid+FarGrid)%FarGrid)*FarGrid+((Key.X%FarGrid+FarGrid)%FarGrid); }
FIntPoint FarRoofKey(const FVector& World) { return FIntPoint(FMath::FloorToInt32(World.X/FarCell),FMath::FloorToInt32(World.Y/FarCell)); }
}

UMaterialInterface* UWowWeatherComponent::PrepareMaterial(UObject* Outer)
{
    // v2 (r228): quads with UV.x above 1.5 are hot embers - drawn 8x brighter (vertex colours
    // stop at 1) and fully round; everything else exactly as before.
    const FString Name=TEXT("M_WowWeatherParticles_v2");
    UMaterial* Material=WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material=NewObject<UMaterial>(Outer);
        Material->SetShadingModel(MSM_Unlit);
        Material->BlendMode=BLEND_Translucent;
        Material->TwoSided=true;
        Material->bDisableDepthTest=false;
        Material->bComputeFogPerPixel=true;
        auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Material);
        auto* Tint=NewObject<UMaterialExpressionVertexColor>(Material);
        auto* Shader=NewObject<UMaterialExpressionCustom>(Material);
        Shader->OutputType=CMOT_Float4;
        FCustomInput A; A.InputName=TEXT("UV"); A.Input.Connect(0,UV); Shader->Inputs.Add(A);
        FCustomInput B; B.InputName=TEXT("Tint"); B.Input.Connect(0,Tint); Shader->Inputs.Add(B);
        FCustomInput C; C.InputName=TEXT("TintAlpha"); C.Input.Connect(4,Tint); Shader->Inputs.Add(C);
        Shader->Code=TEXT("float hot=step(1.5,UV.x); float2 p=frac(UV)*2-1; float r=dot(p,p);"
            " float alpha=lerp(exp(-r*3)*(1-smoothstep(0.5,1,r)), exp(-r*5)*(1-smoothstep(0.6,1,r)), hot);"
            " return float4(Tint.rgb*lerp(1,8,hot),TintAlpha*alpha);");
        auto* RGB=NewObject<UMaterialExpressionComponentMask>(Material);
        RGB->Input.Connect(0,Shader); RGB->R=true; RGB->G=true; RGB->B=true;
        auto* Alpha=NewObject<UMaterialExpressionComponentMask>(Material);
        Alpha->Input.Connect(0,Shader); Alpha->A=true;
        for (UMaterialExpression* E : TArray<UMaterialExpression*>{UV,Tint,Shader,RGB,Alpha}) Material->GetExpressionCollection().AddExpression(E);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0,RGB);
        Material->GetEditorOnlyData()->Opacity.Connect(0,Alpha);
        Material->PreEditChange(nullptr); Material->PostEditChange();
        Material=WowCookedMaterial::Save(Material,Name);
    }
#endif
    return Material ? Material : UMaterial::GetDefaultMaterial(MD_Surface);
}

UWowWeatherComponent::UWowWeatherComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=1.f/30.f;
}

void UWowWeatherComponent::BeginPlay()
{
    // UE defaults local volumes to a 20 m near exclusion. Gusts and water mist
    // must remain visible next to the camera as well as in the distance.
    if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.LocalFogVolume.GlobalStartDistance"))) C->Set(20.f,ECVF_SetByCode);

    // Keep small shoreline volumes in the analytical local-fog pass. Injecting
    // these metre-sized spheres into the scene fog grid loses them at distance
    // and at reduced internal rendering resolution.
    if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.LocalFogVolume.RenderIntoVolumetricFog"))) C->Set(0,ECVF_SetByCode);

    Super::BeginPlay();
    GConfig->GetInt(Prefs,TEXT("WeatherSource"),Source,GGameUserSettingsIni);
    Source=FMath::Clamp(Source,0,2);
    GConfig->GetBool(Prefs,TEXT("WeatherLocationConfigured"),bConfigured,GGameUserSettingsIni);
    GConfig->GetDouble(Prefs,TEXT("WeatherLatitude"),Latitude,GGameUserSettingsIni);
    GConfig->GetDouble(Prefs,TEXT("WeatherLongitude"),Longitude,GGameUserSettingsIni);
    bConfigured=bConfigured&&WowWeather::CoordinatesValid(Latitude,Longitude);
    FString Fixture;
    if (FParse::Value(FCommandLine::Get(),TEXT("weather="),Fixture))
    {
        bFixture=true;
        if (Fixture==TEXT("rain")) Target.Kind=EWowWeatherKind::Rain;
        else if (Fixture==TEXT("snow")) Target.Kind=EWowWeatherKind::Snow;
        else if (Fixture==TEXT("dust")) Target.Kind=EWowWeatherKind::Dust;
        else if (Fixture==TEXT("fog")) Target.Kind=EWowWeatherKind::Fog;
        else if (Fixture==TEXT("storm")) Target.Kind=EWowWeatherKind::Storm;
        else if (Fixture==TEXT("ash")) Target.Kind=EWowWeatherKind::BlackSnow;
        else if (Fixture==TEXT("blizzard")) Target.Kind=EWowWeatherKind::Snow;
        Target.Intensity=Target.Kind==EWowWeatherKind::Clear?0:0.8f;
        FParse::Value(FCommandLine::Get(),TEXT("weathergrade="),Target.Intensity);
        Target.Intensity=FMath::Clamp(Target.Intensity,0.f,1.f);
        Target.Cloud=Target.Intensity; Target.Wind=Fixture==TEXT("blizzard")?22.f:9.f; Target.Direction=45;
        Current=Target;
    }
    if (Source==1 && bConfigured && !bFixture)
    {
        FString Json;
        TSharedPtr<FJsonObject> Root;
        if (FFileHelper::LoadFileToString(Json,*CacheFile()) && Json.Len()<65536 &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)&&Root.IsValid())
        {
            double Lat,Lon;
            if (Root->TryGetNumberField(TEXT("requested_latitude"),Lat)&&Root->TryGetNumberField(TEXT("requested_longitude"),Lon)&&
                FMath::IsNearlyEqual(Lat,Latitude,0.00001)&&FMath::IsNearlyEqual(Lon,Longitude,0.00001))
                WowWeather::ParseCurrent(Json,FDateTime::UtcNow().ToUnixTimestamp(),Real,RealUtc);
        }
    }
    if (!GetWorld()->IsEditorWorld() || GetWorld()->IsPlayInEditor())
    {
        Tumbleweeds=NewObject<UWowTumbleweedComponent>(GetOwner(),TEXT("Tumbleweeds"));
        Tumbleweeds->SetWeather(this);
        Tumbleweeds->RegisterComponent();
    }
    Mesh=NewObject<UProceduralMeshComponent>(GetOwner(),TEXT("WeatherPrecipitation"));
    Mesh->SetupAttachment(GetOwner()->GetRootComponent());
    Mesh->SetAbsolute(true,true,true);
    Mesh->SetWorldRotation(FRotator::ZeroRotator);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->ComponentTags.AddUnique(TEXT("WowNoCameraFocus"));
    Mesh->SetCastShadow(false);
    Mesh->bAffectDistanceFieldLighting=false;
    Mesh->RegisterComponent();
    Mesh->SetMaterial(0,PrepareMaterial(this));
    Surfaces.SetNum(Grid*Grid);
    for(int32 I=0;I<DropCount;++I)
    {
        Drops.Add(FVector(Random.FRandRange(-Radius,Radius),Random.FRandRange(-Radius,Radius),Random.FRandRange(-650,1050)));
        const int32 V=Vertices.Num();
        Vertices.AddZeroed(4); Normals.Append({FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
        UV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
        Colors.Append({FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent});
        Triangles.Append({V,V+1,V+2,V,V+2,V+3});
    }
    Mesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colors,{},false);
    {
        GFarDrops.Reset(); GFarVertices.Reset(); GFarNormals.Reset(); GFarUV.Reset(); GFarColors.Reset();
        GFarRoof.Init(FFarRoofCell(),FarGrid*FarGrid);
        GFarShown=false;
        TArray<int32> FarTriangles;
        for(int32 I=0;I<FarDropCount;++I)
        {
            GFarDrops.Add(FVector(Random.FRandRange(-FarRadius,FarRadius),Random.FRandRange(-FarRadius,FarRadius),Random.FRandRange(FarBottom,FarTop)));
            const int32 V=GFarVertices.Num();
            GFarVertices.AddZeroed(4); GFarNormals.Append({FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
            GFarUV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
            GFarColors.Append({FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent});
            FarTriangles.Append({V,V+1,V+2,V,V+2,V+3});
        }
        Mesh->CreateMeshSection_LinearColor(2,GFarVertices,FarTriangles,GFarNormals,GFarUV,GFarColors,{},false);
        Mesh->SetMaterial(2,Mesh->GetMaterial(0));
    }
    Mesh->SetVisibility(false);
    NextLightning=FPlatformTime::Seconds()+StormRandom.FRandRange(8.f,16.f);
    RefreshTarget();
    UE_LOG(LogWowWeather,Log,TEXT("Weather ready: source=%d locationConfigured=%d boundedParticles=%d fixture=%d"),Source,bConfigured,DropCount,bFixture);
}

void UWowWeatherComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    for(const auto& Mist : AmbientMist) if(Mist) Mist->DestroyComponent();
    AmbientMist.Reset(); AmbientPatches.Reset();
    for(const auto& Gust : StormGusts) if(Gust) Gust->DestroyComponent();
    StormGusts.Reset();
    for(const auto& Mist : WaterMist) if(Mist) Mist->DestroyComponent();
    WaterMist.Reset(); MistWater.Reset();
    if(Request.IsValid()) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); Request.Reset(); }
    if (Audio.IsValid()) Audio->SetWeatherAudio(0,0);
    for(auto& Light:GEmberLights) if(Light.IsValid()) Light->DestroyComponent();
    GEmberLights.Reset();
    if(GroundWaterMesh) GroundWaterMesh->DestroyComponent();
    GroundWaterPatches.Reset(); GroundWaterIndices.Reset();
    if(Mesh) Mesh->DestroyComponent();
    if(LightningMesh) LightningMesh->DestroyComponent();
    if(LightningLight) LightningLight->DestroyComponent();
    Super::EndPlay(Reason);
}

void WowSetOceanZone(uint32 Zone); // r596: WowOceanSurfaceMaterial.cpp, sea colour per zone

void UWowWeatherComponent::SetZone(uint32 ZoneId,const FString& Name)
{
    WowSetOceanZone(ZoneId);
    if (Zone != ZoneId) { ThunderAt=0; NextLightning=FPlatformTime::Seconds()+12; }
    const bool bNewZone = Zone != ZoneId;
    Zone=ZoneId; ZoneClimate=WowWeather::Climate(Name,ZoneId);
    UE_LOG(LogWowWeather,Log,TEXT("Weather zone=%u %s climate=%d"),Zone,*Name,int32(ZoneClimate));
    RefreshTarget();
    // r723: what the zone turns the weather into (real/realm/front weather after the climate translation).
    UE_LOG(LogWowWeather,Log,TEXT("Weather target: kind=%d intensity=%.2f cloud=%.2f wind=%.1f source=%d"),
        int32(Target.Kind),Target.Intensity,Target.Cloud,Target.Wind,Source);
    if (bNewZone) GZoneBlendUntil=FPlatformTime::Seconds()+90.0;
    // r491: a zone entered by login or teleport starts with its own weather, not a slow change from
    // the last place (walking across a zone border still changes gradually).
    if (bNewZone && FPlatformTime::Seconds()-LastJumpAt<20.0) SnapToTarget(TEXT("zone after a jump"));
}

// r491 (Stefan: logged out of Whiteknight in the Dun Morogh snow, into a troll in Durotar: it snowed
// there too, then slowly turned into a sandstorm. "Make sure the weather is loaded before the loading
// screen is done"): after a login, teleport or instance entry the weather is set at once (still behind
// the loading screen) instead of fading over a minute from whatever it was before.
// r676 (Stefan, Northshire in rain: "what happened to the wet ground? I don't see any wet ground or puddles anymore"):
// the ground's wetness was the rain's intensity, and the server's rain there was light (0.20) - the terrain barely
// darkened and the puddles stayed empty (they start at 0.12, full at 0.87). Real ground soaks in any rain: rain of
// any strength wets it at least halfway, heavier rain up to fully.
static float WowRainSoak(float Rain)
{
    return Rain > .005f ? FMath::Lerp(.5f, 1.f, FMath::Clamp(Rain, 0.f, 1.f)) : 0.f;
}
void UWowWeatherComponent::SnapToTarget(const TCHAR* Why)
{
    if(bFixture) return;
    RefreshTarget();
    Current=Target;
    Current.Intensity=Target.Intensity;
    SurfaceWetness=IsRain(Current.Kind) ? WowRainSoak(Current.Intensity*VisualStrength) : 0.f; // r676
    UE_LOG(LogWowWeather,Log,TEXT("Weather snapped (%s): kind=%d intensity=%.2f zone=%u"),Why,int32(Current.Kind),Current.Intensity,Zone);
}
void UWowWeatherComponent::ResetForGlueScreen()
{
    Zone=0;
    Target={}; Current={};
    SurfaceWetness=0.f;
    ThunderAt=0; FlashAge=10; bThunderstormActive=false;
    NextLightning=FPlatformTime::Seconds()+12;
    GroundWaterPatches.Reset();
    if(GroundWaterMesh) GroundWaterMesh->SetVisibility(false);
    if(LightningMesh) LightningMesh->SetVisibility(false);
    if(LightningLight) LightningLight->SetIntensity(0.f);
    RefreshTarget();
    UE_LOG(LogWowWeather,Log,TEXT("Weather reset for the login/character screen"));
}

void UWowWeatherComponent::SetOnline(bool Value)
{
    if(bOnline==Value) return;
    bOnline=Value;
    if(!bOnline) { bHaveServer=false; Server={}; }
    RefreshTarget();
}
void UWowWeatherComponent::ReceiveServer(uint32 State,float Intensity,bool bAbrupt)
{
    if (!FMath::IsFinite(Intensity)) return;
    Server.Kind=WowWeather::DecodeState(State);
    Server.Intensity=Server.Kind==EWowWeatherKind::Clear?0:FMath::Clamp(Intensity,0.f,1.f);
    Server.Cloud=Server.Kind==EWowWeatherKind::Clear?0.2f:FMath::Max(0.6f,Server.Intensity);
    Server.Wind=2+Server.Intensity*12;
    bHaveServer=true;
    RefreshTarget();
    if(bAbrupt && Source==0 && !bFixture && (!WowTestControls::Enabled() || WeatherPreview.GetValueOnGameThread()==0)) Current=Target;
    else if(FPlatformTime::Seconds()-LastJumpAt<20.0) SnapToTarget(TEXT("realm weather after a jump")); // r491
}

// r497 (Stefan: "make the starting zones easier with lower weather, at least in the starting-starting
// areas, not Durotar but like Valley of Trials. Max 20 %"): the race starting valleys and the places
// inside them, by their AreaTable name (stable across realms; ids of the inner places vary).
static bool IsStartingArea(uint32 AreaId)
{
    static const TCHAR* Names[] = {
        TEXT("Northshire Valley"), TEXT("Northshire Abbey"), TEXT("Northshire Vineyards"), TEXT("Echo Ridge Mine"),
        TEXT("Coldridge Valley"), TEXT("Coldridge Pass"), TEXT("Anvilmar"),
        TEXT("Shadowglen"), TEXT("Aldrassil"), TEXT("Shadowthread Cave"), TEXT("Fel Rock"),
        TEXT("Deathknell"), TEXT("Night Web's Hollow"),
        TEXT("Valley of Trials"), TEXT("Burning Blade Coven"),
        TEXT("Camp Narache"), TEXT("Red Cloud Mesa"),
        TEXT("Sunstrider Isle"), TEXT("Ammen Vale"), TEXT("Crash Site") };
    const FAreaTableDbcEntry* Area = AreaId && FDbcStore::Get().IsLoaded() ? FDbcStore::Get().AreaTable().GetById(AreaId) : nullptr;
    if (!Area) return false;
    for (const TCHAR* Name : Names) if (Area->Name.Equals(Name, ESearchCase::IgnoreCase)) return true;
    return false;
}

void UWowWeatherComponent::RefreshTarget()
{
    if(bFixture) return;
    // r495 (Stefan: "we also need an experimental extreme weather; the weather can be extreme, and
    // that might be anything above 30 %"): without Options > Experimental > Extreme weather, every
    // weather (realm, regional or the zone's own fronts) stays at most 30 % strong, with moderate
    // wind (no blizzard gusts). The test weather presets are not limited.
    ON_SCOPE_EXIT
    {
        const bool bPreview = WowTestControls::Enabled() && WeatherPreview.GetValueOnGameThread() > 0;
        if (!bPreview && !FWowClientPreferences::GetWorldOption(TEXT("extremeweather")))
        {
            Target.Intensity = FMath::Min(Target.Intensity, 0.30f);
            Target.Wind = FMath::Min(Target.Wind, 20.f);
        }
        // r497: the starting valleys stay gentle, extreme weather or not.
        if (!bPreview)
        {
            const AWowWorldManager* World = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass()));
            if (World && IsStartingArea(World->GetCurrentAreaId()))
            {
                Target.Intensity = FMath::Min(Target.Intensity, 0.20f);
                Target.Wind = FMath::Min(Target.Wind, 14.f);
            }
        }
    };
    TargetVisualStrength=1.f;
    if(Zone==0) { Target={}; return; }
    const int32 Preview=WowTestControls::Enabled()?WeatherPreview.GetValueOnGameThread():0;
    if(Preview==10)
    {
        const int32 Mix=WeatherPreviewMix.GetValueOnGameThread();
        const float Requested=FMath::Clamp(WeatherPreviewStrength.GetValueOnGameThread(),0.f,2.f);
        const float Grade=FMath::Min(Requested,1.f);
        TargetVisualStrength=FMath::Max(1.f,Requested);
        Target={};
        Target.Kind=(Mix&8)?EWowWeatherKind::Storm:(Mix&4)?EWowWeatherKind::Rain:(Mix&16)?EWowWeatherKind::Snow
            :(Mix&32)?EWowWeatherKind::Dust:(Mix&64)?EWowWeatherKind::BlackSnow:EWowWeatherKind::Clear;
        const bool bPrecip=Target.Kind!=EWowWeatherKind::Clear;
        Target.Intensity=bPrecip?Grade:0.f;
        // Fog alone is the Fog kind (its own look); with precipitation it is mist on top.
        if(!bPrecip && (Mix&2)) { Target.Kind=EWowWeatherKind::Fog; Target.Intensity=Grade; }
        else if(Mix&2) Target.Mist=Grade;
        // Without "overcast" the sky stays mostly clear, rain or not (a sun shower).
        Target.Cloud=(Mix&1)?FMath::Lerp(0.8f,1.f,Grade):bPrecip?0.25f:0.16f;
        Target.Wind=bPrecip?FMath::Lerp(2.f,(Mix&8)?28.f:18.f,Grade):2.f;
        Target.Direction=45.f;
        return;
    }
    if(Preview>0 && Preview<=9)
    {
        static constexpr EWowWeatherKind Kinds[]={EWowWeatherKind::Clear,EWowWeatherKind::Clear,EWowWeatherKind::Clear,EWowWeatherKind::Rain,EWowWeatherKind::Storm,EWowWeatherKind::Snow,EWowWeatherKind::Snow,EWowWeatherKind::Dust,EWowWeatherKind::BlackSnow,EWowWeatherKind::Fog};
        const float Requested=FMath::Clamp(WeatherPreviewStrength.GetValueOnGameThread(),0.f,2.f);
        const float Grade=FMath::Min(Requested,1.f);
        TargetVisualStrength=FMath::Max(1.f,Requested);
        Target.Kind=Kinds[Preview];
        Target.Intensity=Preview<=2?0.f:Grade;
        Target.Cloud=Preview==1?0.16f:Preview==2?1.f:FMath::Lerp(0.45f,1.f,Grade);
        Target.Wind=Preview<=2?2.f:FMath::Lerp(2.f,Preview==6?56.f:Preview==7?28.f:18.f,Grade);
        Target.Direction=45.f;
        return;
    }
    if(Source==1 && bConfigured && RealUtc>0 && FDateTime::UtcNow().ToUnixTimestamp()-RealUtc<=7200)
    {
        Target=WowWeather::Adapt(Real,ZoneClimate); return;
    }
    // Online default preserves the realm's explicit weather, including scripted effects.
    if(Source!=2 && bOnline)
    {
        Target=bHaveServer?Server:FWowWeatherSample{};
        // Keep explicit exotic realm effects; adapt ordinary precipitation to the biome.
        if (Target.Kind!=EWowWeatherKind::BlackRain && Target.Kind!=EWowWeatherKind::BlackSnow && Target.Kind!=EWowWeatherKind::Dust)
            Target=WowWeather::Adapt(Target,ZoneClimate);
        // r468 (Stefan, Shadowglen: "there must be some type of weather, at least 10%; it feels
        // strangely empty"): most zones get no weather from the realm at all (always "fine").
        // Then the zone's own slow fronts (below) play, kept light: drizzle/snow of 10-35 %, or
        // the dry fronts' thin mist. Real realm weather always wins.
        if (Target.Kind==EWowWeatherKind::Clear && Target.Intensity<0.1f)
        {
            FWowWeatherSample Front=ZoneFront();
            if (Front.Kind!=EWowWeatherKind::Fog)
            {
                Front.Intensity=FMath::Clamp(Front.Intensity*0.35f,0.1f,0.35f);
                if (Front.Kind==EWowWeatherKind::Storm) Front.Kind=EWowWeatherKind::Rain;
            }
            Target=WowWeather::Adapt(Front,ZoneClimate); // deserts: light dust, ash zones: ash instead of mist
        }
        return;
    }
    if(Zone==0) { Target={}; return; } // No weather in character selection/loading before a world zone exists.
    Target=WowWeather::Adapt(ZoneFront(),ZoneClimate);
}

FWowWeatherSample UWowWeatherComponent::ZoneFront() const
{
    const FDateTime Now=FDateTime::Now();
    const int64 Hour=Now.ToUnixTimestamp()/1200; // Twenty-minute regional fronts, smoothly approached below.
    FRandomStream Daily(uint32(Hour)^Zone*2654435761u);
    FWowWeatherSample S;
    const float Season=0.5f+0.5f*FMath::Cos((Now.GetMonth()-1)*PI/6);
    const float Chance=ZoneClimate==EWowClimate::Tropical?0.62f:FMath::Lerp(0.25f,0.5f,Season);
    const bool Wet=Daily.FRand()<Chance;
    S.Kind=Wet?EWowWeatherKind::Rain:EWowWeatherKind::Clear;
    S.Intensity=Wet?Daily.FRandRange(0.12f,1.0f):0;
    S.Cloud=Wet?Daily.FRandRange(0.65f,1.f):Daily.FRandRange(0.05f,0.45f);
    S.Wind=Daily.FRandRange(1.f,Wet?24.f:9.f); S.Direction=Daily.FRandRange(0.f,360.f);
    if(Wet && S.Intensity>0.75f) S.Kind=EWowWeatherKind::Storm;
    // r534 (Stefan: weathers should mix): about a quarter of the rain fronts bring mist as well, one in
    // ten light rains falls from a mostly clear sky (a sun shower), and dry misty fronts are often overcast.
    const float MixRoll=Daily.FRand();
    if(Wet && MixRoll<0.25f) S.Mist=Daily.FRandRange(0.2f,0.5f);
    else if(Wet && S.Intensity<0.45f && MixRoll<0.35f) S.Cloud=Daily.FRandRange(0.15f,0.35f);
    // r468: a dry front is never empty air: a thin mist of 20-35 % (fog volumes only, no particles).
    if(!Wet) { S.Kind=EWowWeatherKind::Fog; S.Intensity=Daily.FRandRange(0.2f,0.35f); if(MixRoll<0.4f) S.Cloud=Daily.FRandRange(0.75f,1.f); }
    // r723: a seasonal temperature (about -1 C in January, 17 C in July, +-5 C per front) so the zone fronts of
    // mountain zones fall as snow or rain like the real weather does. Rolled last: the fronts above stay the same.
    S.TemperatureC=FMath::Lerp(17.f,-1.f,Season)+Daily.FRandRange(-5.f,5.f); S.bHasTemperature=true;
    return S;
}

void UWowWeatherComponent::RequestWeather()
{
    if(Request.IsValid()||Source!=1||!bConfigured||bFixture) return;
    NextRequest=FPlatformTime::Seconds()+300;
    // [HTTP] bEnableHttp=False in the project config turns every request into an
    // immediate failure; use the server/zone weather without retrying.
    if(!FHttpModule::Get().IsHttpEnabled()) { NextRequest=TNumericLimits<double>::Max(); return; }
    Request=FHttpModule::Get().CreateRequest();
    if(!Request.IsValid()) return;
    Request->SetURL(FString::Printf(TEXT("https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f&current=weather_code,cloud_cover,wind_speed_10m,wind_direction_10m,temperature_2m&wind_speed_unit=ms&timeformat=unixtime&forecast_days=1"),Latitude,Longitude));
    Request->SetVerb(TEXT("GET")); Request->SetTimeout(12.f);
    Request->SetHeader(TEXT("User-Agent"),TEXT("WowUnreal-weather/1.0"));
    Request->OnProcessRequestComplete().BindWeakLambda(this,[this](FHttpRequestPtr,FHttpResponsePtr Response,bool Success)
    {
        Request.Reset();
        if(!Success||!Response.IsValid()||Response->GetResponseCode()!=200||Response->GetContent().Num()>65536)
        { UE_LOG(LogWowWeather,Warning,TEXT("Real weather unavailable; retaining fresh cache or using server/zone fallback. Retry in 5 minutes.")); return; }
        const FString Json=Response->GetContentAsString();
        if(!WowWeather::ParseCurrent(Json,FDateTime::UtcNow().ToUnixTimestamp(),Real,RealUtc))
        { UE_LOG(LogWowWeather,Warning,TEXT("Real weather response invalid/stale; keeping fallback.")); return; }
        NextRequest=FPlatformTime::Seconds()+900;
        TSharedPtr<FJsonObject> Root;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)&&Root.IsValid())
        {
            Root->SetNumberField(TEXT("requested_latitude"),Latitude); Root->SetNumberField(TEXT("requested_longitude"),Longitude);
            FString Cache; FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Cache));
            FFileHelper::SaveStringToFile(Cache,*CacheFile());
        }
        RefreshTarget();
        const FString RealTemperature=Real.bHasTemperature ? FString::Printf(TEXT("%.1fC"),Real.TemperatureC) : FString(TEXT("n/a")); // r723
        UE_LOG(LogWowWeather,Log,TEXT("Open-Meteo current weather refreshed: kind=%d intensity=%.2f cloud=%.0f%% wind=%.1fm/s temp=%s"),int32(Real.Kind),Real.Intensity,Real.Cloud*100.f,Real.Wind,*RealTemperature);
    });
    // The completion delegate may run synchronously inside ProcessRequest (e.g. offline
    // or DNS failure) and reset Request; keep a local reference for the failure path.
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Pending = Request.ToSharedRef();
    if(!Pending->ProcessRequest()) { Pending->OnProcessRequestComplete().Unbind(); Request.Reset(); }
}

void UWowWeatherComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,TickType,Function);
    const double Now=FPlatformTime::Seconds();
    if(Now>=NextTarget) { NextTarget=Now+2; RefreshTarget(); }
    if(Now>=NextRequest) RequestWeather();
    const float Dt=FMath::Min(DeltaTime,0.1f);
    const float Blend=Now<GZoneBlendUntil?0.25f:1.f; // r576
    // Rain and thunderstorm share particles; other family changes fade out first.
    if(IsRain(Current.Kind)&&IsRain(Target.Kind)) Current.Kind=Target.Kind;
    const float Gust=0.92f+0.08f*FMath::Sin(float(Now*0.075)+Zone*0.013f);
    const float DesiredIntensity=Target.Intensity*(bFixture?1.f:Gust);
    // Fade precipitation out before changing family (rain -> snow), then fade in.
    if(Current.Kind!=Target.Kind)
    {
        Current.Intensity=FMath::FInterpConstantTo(Current.Intensity,0.f,Dt,0.06f*Blend);
        if(Current.Intensity<0.005f) Current.Kind=Target.Kind;
    }
    else Current.Intensity=FMath::FInterpConstantTo(Current.Intensity,DesiredIntensity,Dt,0.035f*Blend);
    Current.Cloud=FMath::FInterpConstantTo(Current.Cloud,Target.Cloud,Dt,0.07f*Blend);
    Current.Mist=FMath::FInterpConstantTo(Current.Mist,Target.Mist,Dt,0.05f*Blend); // r534
    Current.Wind=FMath::FInterpTo(Current.Wind,Target.Wind,Dt,0.3f*Blend);
    // Server weather has no heading. Supply a slowly varying zonal heading;
    // regional weather retains the measured direction, including calm periods.
    const bool bRegionalWind=Source==1 && bConfigured && RealUtc>0
        && FDateTime::UtcNow().ToUnixTimestamp()-RealUtc<7200;
    const double WindTime=double(FDateTime::UtcNow().ToUnixTimestamp());
    const float Direction=(bFixture || bRegionalWind) ? Target.Direction
        : WowWeather::ZoneWindDirection(Zone,WindTime);
    Current.Direction=FMath::UnwindDegrees(Current.Direction+FMath::FindDeltaAngleDegrees(Current.Direction,Direction)*FMath::Min(Dt*0.08f,1.f));
    const float WindAngle=FMath::DegreesToRadians(Current.Direction+180.f);
    MistWindOffset+=FVector2D(FMath::Cos(WindAngle),FMath::Sin(WindAngle))
        *FMath::Clamp(Current.Wind,0.f,30.f)*6.f*Dt;
    VisualStrength=FMath::FInterpConstantTo(VisualStrength,TargetVisualStrength,Dt,.1f);
    // r535: banners, flags and tents move with this wind (0 calm .. 1 at ~25 m/s).
    WowCloth::SetWind(GetWorld(),FVector2D(FMath::Cos(WindAngle),FMath::Sin(WindAngle)),FMath::Clamp(Current.Wind/25.f,0.f,1.f));
    // Sea state follows the weather: wind height/direction, chop, rain roughness, storms.
    FWowWaterMaterial::SetOceanWeather(FVector2D(FMath::Cos(WindAngle),FMath::Sin(WindAngle)),
        Current.Wind*FMath::Lerp(1.f,1.35f,FMath::Clamp(VisualStrength-1.f,0.f,1.f)),
        IsRain(Current.Kind)?Current.Intensity:0.f, bThunderstormActive);
    // Accumulated water does not evaporate during a brief precipitation gust lull.
    const float RainWetness=IsRain(Current.Kind) ? WowRainSoak(Current.Intensity*VisualStrength) : 0.f; // r676
    // r676: it soaks in over about half a minute (was a quarter of that).
    const float WetTarget=RainWetness>.005f ? FMath::Max(SurfaceWetness,RainWetness) : 0.f;
    SurfaceWetness=FMath::FInterpConstantTo(SurfaceWetness,WetTarget,Dt,WetTarget>SurfaceWetness?.02f:.006f);
    FWowTerrainMaterial::SetRainWetness(GetWorld(),SurfaceWetness);
    UpdateParticles(Dt);
    UpdateGroundWater(Dt,WetTarget);
    UpdateStormGusts(Dt);
    UpdateWaterMist(Dt);
    UpdateAmbientAtmosphere(Dt);
    UpdateThunder(Dt);
    if(Now>=NextAudio)
    {
        NextAudio=Now+0.5;
        if(!Audio.IsValid()) Audio=Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(this,AWowAudioManager::StaticClass()));
        // Indoors the rain is still heard through walls and windows, just softer.
        const float Heard=Exposure*(bSheltered?.45f:1.f);
        if(Audio.IsValid()) Audio->SetWeatherAudio(WowWeather::EncodeState(Current.Kind),Current.Intensity*FMath::Lerp(0.18f,1.f,Heard),Current.Wind*FMath::Lerp(0.12f,1.f,Heard));
    }
}

// r400: a hit on building (WMO) geometry - the WMO renderer names its group and collision
// components "WmoGroup_N" / "WmoAuthoredCollision_N_...". Trees, rocks and terrain are not buildings.
static bool IsBuildingHit(const FHitResult& Hit)
{
    const UPrimitiveComponent* Component=Hit.GetComponent();
    return Component && Component->GetName().StartsWith(TEXT("Wmo"));
}

// r574 (Stefan, under the Stormwind Keep gate in rain: no rain on the courtyard until one more step out):
// indoors every rain cell looks up for a roof, and over the Keep courtyard it found the city's always-drawn
// mountain backdrop (group 82, ~240 m tall, hidden in the city since r562) - so every cell was "under a
// roof". A group taller than ~140 m is skyline scenery, never a roof.
static bool IsRoofHit(const FHitResult& Hit)
{
    const UPrimitiveComponent* Component=Hit.GetComponent();
    return IsBuildingHit(Hit) && Component->Bounds.BoxExtent.Z<6800.f;
}

// r536 (Stefan: "if wind is blowing towards an entrance, the rain could be carried 1-2 metres inside"):
// the direction the drops come from - up and upwind, as steep as the fall speed against the wind.
// Shelter tests look along it instead of straight up, so a windward doorway gets wet inside.
static FVector RainSourceDirection(const FWowWeatherSample& W, float VisualStrength)
{
    const float Angle=FMath::DegreesToRadians(W.Direction+180.f);
    const float Speed=W.Wind*65.f*FMath::Lerp(1.f,1.5f,VisualStrength-1.f);
    return FVector(-FMath::Cos(Angle)*Speed,-FMath::Sin(Angle)*Speed,1900.f).GetSafeNormal();
}

void UWowWeatherComponent::UpdateParticles(float Dt)
{
    APlayerController* PC=UGameplayStatics::GetPlayerController(this,0);
    if(!PC||!Mesh) return;
    FVector Camera; FRotator Rotation; PC->GetPlayerViewPoint(Camera,Rotation);
    const FVector CameraDelta=bHaveCamera?Camera-LastCamera:FVector::ZeroVector;
    LastCamera=Camera; bHaveCamera=true;
    // r491: a jump of more than 300 m in one frame is a login, teleport or loading screen, not travel.
    if(CameraDelta.SizeSquared()>FMath::Square(30000.0))
    {
        LastJumpAt=FPlatformTime::Seconds();
        if(Zone!=0) SnapToTarget(TEXT("camera jump"));
    }
    const double Now=FPlatformTime::Seconds();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(WeatherShelter),true);
    Params.AddIgnoredActor(GetOwner());
    if(PC->GetPawn()) Params.AddIgnoredActor(PC->GetPawn());
    // r401 (Stefan: the weather changed when he turned the camera): roof and walls are probed from
    // the character's head, not from the camera. A third-person camera swinging past a pillar or
    // into a wall used to switch "indoors" on and off with every turn.
    FVector Probe=Camera;
    if(const APawn* Body=PC->GetPawn()) Probe=Body->GetActorLocation()+FVector(0,0,70);
    if(Now>=NextShelter)
    {
        NextShelter=Now+0.25;
        FHitResult Hit;
        bSheltered=GetWorld()->LineTraceSingleByObjectType(Hit,Probe,Probe+FVector(0,0,12000),FCollisionObjectQueryParams(ECC_WorldStatic),Params);
        // Indoors (r234): a roof within 25 m and walls in at least 3 of 4 directions within 30 m.
        // A tree or an arch overhead alone keeps the weather fog (it is still outside).
        // r400 (Stefan: the blizzard fog vanished when he backed up among the Coldridge pines): only a
        // building counts - roof and walls must be WMO geometry, not trees, rocks or the hillside.
        bIndoors=false;
        IndoorOpenness=1.f;
        // r506 (Stefan, Orgrimmar bank in a sandstorm: sandy haze inside the hall, at the door and deep
        // inside): the hall's roof is higher than 25 m and its far wall more than 30 m away, so the
        // roof/wall traces called it outdoors. The WMO itself says which groups are rooms (MOGP flag
        // 0x2000, as the real client uses for "indoors"): under a building's roof and inside such a
        // group's box counts as indoors whatever the room's size.
        const bool bUnderBuilding=bSheltered && IsRoofHit(Hit);
        const bool bInRoom=bUnderBuilding && FWowIndoorFogMask::IsInsideIndoorGroup(Probe);
        // r576 (Stefan, under the wooden struts over the Stormwind bridge gate: the weather fog came and went,
        // "these struts at the walls making me inside"): a beam overhead is not a roof. Outside a WMO room, a
        // roof must also be over 4 of 4 points 2 m around the head (or a wall closer on that side).
        bool bSolidRoof=bInRoom;
        if(bUnderBuilding && !bInRoom && Hit.Distance<2500.f)
        {
            int32 Covered=0;
            for(const FVector& Offset:{FVector(200,0,0),FVector(-200,0,0),FVector(0,200,0),FVector(0,-200,0)})
            {
                // A wall within 2 m on that side counts as covered too (narrow passages, doorways).
                FHitResult Side, Over;
                if(GetWorld()->LineTraceSingleByObjectType(Side,Probe,Probe+Offset,FCollisionObjectQueryParams(ECC_WorldStatic),Params) && IsBuildingHit(Side)) { ++Covered; continue; }
                Covered+=GetWorld()->LineTraceSingleByObjectType(Over,Probe+Offset,Probe+Offset+FVector(0,0,2500),FCollisionObjectQueryParams(ECC_WorldStatic),Params)
                    && IsRoofHit(Over);
            }
            bSolidRoof=Covered==4;
        }
        if(bUnderBuilding && bSolidRoof)
        {
            int32 Walls=0;
            for(const FVector& Dir:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0)})
            {
                FHitResult Wall;
                Walls+=GetWorld()->LineTraceSingleByObjectType(Wall,Probe,Probe+Dir*3000.f,FCollisionObjectQueryParams(ECC_WorldStatic),Params)
                    && IsBuildingHit(Wall);
            }
            bIndoors=Walls>=3 || bInRoom;
            // r529 (Stefan, on the Stormwind gatehouse balcony in rain fog: the fog vanished whenever he
            // turned the camera down): the view rays hit the balcony's wooden floor, a building surface,
            // so the view counted as "inside" and the weather fog went. A building surface open to the
            // sky (no building roof above it) is outside too: balconies, wall walks, outer walls.
            // r555 (Stefan, after r552: "fog appears inside sometimes still, depends on the angle of the
            // camera"): one ray out of the 45 calling "outside" brings back the full fog, and three kinds of
            // ray did that from deep inside a room: a ray hitting a doodad (crate, table, lamp - not WMO
            // geometry, so "not a building"), a ray whose up-test hit a chandelier or banner under the
            // ceiling, and a ray that ran 60 m down a long hall without hitting anything. A point is now
            // outside only if no building roof is above it; doodads on the way up are stepped through
            // (up to 6), and a ray that hits nothing is judged at its end point the same way.
            // r572 (Stefan, inside a Stormwind Keep hall in a thunderstorm: the doorway showed no storm until
            // he came close): the sky test went 120 m up and found Stormwind's huge always-drawn mountain
            // backdrop (group 82, hidden in the city since r562 but still hit by traces) over the courtyard,
            // so the street outside counted as "under a roof". A roof is near: only building geometry within
            // 50 m above the point counts.
            auto BuildingRoofAbove=[&](FVector From)
            {
                const double Top=From.Z+5000.0;
                for(int32 Step=0;Step<6;++Step)
                {
                    FHitResult Up;
                    if(!GetWorld()->LineTraceSingleByObjectType(Up,From,FVector(From.X,From.Y,Top),FCollisionObjectQueryParams(ECC_WorldStatic),Params))
                        return false;
                    if(IsRoofHit(Up)) return true;
                    From=Up.ImpactPoint+FVector(0,0,5);
                    if(From.Z>=Top) return false;
                }
                return false;
            };
            auto OpenToSky=[&](const FHitResult& Surface)
            {
                return !BuildingRoofAbove(Surface.ImpactPoint+Surface.ImpactNormal*20.f+FVector(0,0,5));
            };
            auto RayOutside=[&](bool bHit,const FHitResult& Ray,const FVector& End)
            {
                return bHit ? OpenToSky(Ray) : !BuildingRoofAbove(End);
            };
            if(bIndoors)
            {
                // r400: how much of the outside the camera sees (a doorway looking out vs. deep in a
                // hall): share of 8 level rays that leave the building within 60 m. The weather fog
                // keeps that share, so a blizzard still fills the view through the door.
                int32 Open=0;
                for(int32 R=0;R<8;++R)
                {
                    const float A=R*PI/4.f;
                    FHitResult Ray;
                    const FVector End=Probe+FVector(FMath::Cos(A),FMath::Sin(A),0)*6000.f;
                    const bool bHit=GetWorld()->LineTraceSingleByObjectType(Ray,Probe,End,FCollisionObjectQueryParams(ECC_WorldStatic),Params);
                    Open+=RayOutside(bHit,Ray,End);
                }
                IndoorOpenness=Open/8.f;
                // r483 (Stefan, Coldridge doorway in a blizzard: inside the sky turned clear night blue
                // and the fog was gone; two metres out it was a white-out): the level rays from the
                // head found 1 of 8 directions open, so a door filling half the view kept only a
                // quarter of the fog. Also measure what the camera actually looks at: a 7x3 fan of
                // rays over the view; those that leave the building (sky, terrain, trees) are the
                // outside share. The indoor fog mask keeps that fog off the room's own pixels.
                // r552 (Stefan, Stormwind gatehouse in rain fog, looking out past a wall corner: the fog went
                // as soon as the wall filled most of the view - "if I don't see it, it doesn't need to be
                // there, else it must maintain"): the fan now covers the whole view (9 x 5 rays across the
                // camera's field of view), and any ray that reaches the outside keeps the full weather fog.
                // The indoor fog mask already keeps that fog off the room's own pixels.
                const float HalfFov=(PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.f)*0.5f;
                int32 Seen=0;
                for(int32 Row=0;Row<5 && Seen==0;++Row)
                    for(int32 Col=0;Col<9 && Seen==0;++Col)
                    {
                        const FRotator Dir(Rotation.Pitch+(Row-2)*HalfFov*0.6f*0.5f,Rotation.Yaw+(Col-4)*HalfFov*0.25f,0.f);
                        FHitResult Ray;
                        const FVector End=Camera+Dir.Vector()*6000.f;
                        const bool bHit=GetWorld()->LineTraceSingleByObjectType(Ray,Camera,End,FCollisionObjectQueryParams(ECC_WorldStatic),Params);
                        Seen+=RayOutside(bHit,Ray,End);
                    }
                if(Seen>0) IndoorOpenness=1.f;
            }
        }
    }
    const bool Underwater=Audio.IsValid()&&Audio->IsUnderwaterMixEnabled();
    // Under a roof the rain keeps falling outside: each surface cell's downward trace
    // finds the roof and hides drops below it, so only the sheltered area is dry.
    // Only being underwater removes the weather entirely.
    // r235: no snow or rain inside buildings either (Ironforge's halls are deeper than the per-cell
    // roof traces start, so flakes fell indoors).
    // r395 (Stefan: snow outside a Coldridge doorway vanished while standing inside): indoors only
    // the weather fog, gusts, splashes and loud audio go (Exposure). The flakes stay drawn
    // (ParticleExposure); indoors every cell also traces UP from head height, so cells under the
    // building's roof (however high, Ironforge) stay empty and the snow beyond the door still falls.
    Exposure=FMath::FInterpTo(Exposure,(Underwater||bIndoors)?0.f:1.f,Dt,4.f);
    ParticleExposure=FMath::FInterpTo(ParticleExposure,Underwater?0.f:1.f,Dt,4.f);
    const bool Rain=IsRain(Current.Kind),Snow=IsSnow(Current.Kind),Dust=Current.Kind==EWowWeatherKind::Dust, Ash=Current.Kind==EWowWeatherKind::BlackSnow;
    const bool Visible=(Rain||Snow||Dust||Ash)&&Current.Intensity>0.005f&&ParticleExposure>0.005f;
    // r583 (Stefan, on the open deck of the Stormwind gryphon tower in rain: "no rain from this angle either"):
    // log the rain state whenever it changes (indoors/sheltered/visible and how many drops are drawn, in steps of
    // 500), so the next report shows whether the drops were switched off or only hard to see.
    {
        static int32 LastState=-1;
        int32 Drawn=0; for(int32 I=0;I<Colors.Num();I+=4) if(Colors[I].A>0.01f) ++Drawn;
        const int32 State=(bIndoors?1:0)|(bSheltered?2:0)|(Visible?4:0)|(FMath::Min(Drawn/500,15)<<3);
        if(State!=LastState && (Rain||Snow))
        {
            LastState=State;
            UE_LOG(LogWowWeather,Log,TEXT("Weather drops: indoors=%d sheltered=%d visible=%d drawn=%d exposure=%.2f flakes=%.2f intensity=%.2f camera=%s"),
                bIndoors,bSheltered,Visible,Drawn,Exposure,ParticleExposure,Current.Intensity,*Camera.ToCompactString());
        }
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("weatherdiagnostics")) && Now>=NextDiagnostic)
    {
        NextDiagnostic=Now+3;
        int32 Rendered=0; for(int32 I=0;I<Colors.Num();I+=4) if(Colors[I].A>0.01f) ++Rendered;
        UE_LOG(LogWowWeather,Log,TEXT("Weather draw: visible=%d shelter=%d indoors=%d submerged=%d exposure=%.2f flakes=%.2f active=%d camera=%s"),Visible,bSheltered,bIndoors,Underwater,Exposure,ParticleExposure,Rendered,*Camera.ToString());
    }
    Mesh->SetVisibility(Visible);
    if(!Visible || Current.Kind!=EWowWeatherKind::BlackSnow) HideEmberLights();
    if(!Visible)
    {
        if(!Splashes.IsEmpty()) UpdateRainSplashes(Dt,Camera,Rotation,false);
        return;
    }
    // One spatially cached surface trace per update; no per-particle collision or actor spawning.
    // r459 (Stefan, offline in Northshire Abbey: no rain outside the door while standing inside): the
    // rain volume is only ~22 m wide around the camera, so from inside a building the yard beyond the
    // door lay outside it. Indoors the volume moves 3/4 of its radius ahead of the view, so the rain
    // falls where you look out (cells under the roof still stay dry, r395).
    static FVector GLastCentreOffset=FVector::ZeroVector;
    // r584 (Stefan, gryphon tower deck in rain: "way more rain looking when going to the edge, I want this rain
    // to show also when further inside"): the dense near rain only reached ~11 m, so from a few metres back
    // the drops beyond the edge were gone. Outdoors the volume now also sits ahead of the view (half its radius,
    // ~16 m ahead): behind the camera drops are never seen anyway.
    // Looking down (a tower deck over the canal, same report: "still standing same spot, rain gone") the volume
    // also sinks with the view, up to 12 m: the drops only reached 6.5 m below the camera, so the canal far
    // below had none.
    const float DownLook=FMath::Clamp(FMath::Sin(FMath::DegreesToRadians(FRotator::NormalizeAxis(Rotation.Pitch))),-1.f,0.f);
    const FVector CentreOffset=FRotator(0,Rotation.Yaw,0).Vector()*Radius*(bIndoors?.75f:.5f)+FVector(0,0,DownLook*1200.f);
    const FVector CentreShift=CentreOffset-GLastCentreOffset;
    GLastCentreOffset=CentreOffset;
    const FVector Centre=Camera+CentreOffset;
    const FVector Origin(FMath::FloorToDouble(Centre.X/CellSize)*CellSize-Radius,FMath::FloorToDouble(Centre.Y/CellSize)*CellSize-Radius,Camera.Z);
    // Four cell traces per update, so the whole grid follows a walk under a roof in ~0.5 s.
    // r533 (Stefan: the rain "more or less goes away when stepping inside", 200 % rain): indoors a cell
    // stays empty until it has been traced, and the rain volume moves ahead of the view, so with 4 traces
    // per update most of the 144 cells were empty for seconds after every step or turn. Indoors 24.
    for(int32 Trace=0;Trace<(bIndoors?24:4);++Trace)
    {
        const FVector SamplePosition=Origin+FVector((SurfaceIndex%Grid+0.5f)*CellSize,(SurfaceIndex/Grid+0.5f)*CellSize,0);
        FSurfaceCell& Surface=Surfaces[WowWeatherParticles::SurfaceSlot(SamplePosition,CellSize,Grid)];
        Surface.Position=SamplePosition;
        FHitResult Ground;
        // Start well above tall halls so their roof (not the floor) is what stops the rain.
        FHitResult Roof;
        // r400: only the building's roof empties a cell (a pine outside the door does not).
        const FVector RoofFrom(Surface.Position.X,Surface.Position.Y,Probe.Z);
        if(bIndoors && GetWorld()->LineTraceSingleByObjectType(Roof,RoofFrom,RoofFrom+RainSourceDirection(Current,VisualStrength)*12000.f,FCollisionObjectQueryParams(ECC_WorldStatic),Params)
            && IsRoofHit(Roof))
        {
            // Under this building's roof (r395): no flakes in this cell at any height.
            Surface.Valid=true;
            Surface.Height=Camera.Z+1.0e7f;
        }
        else
        {
            Surface.Valid=GetWorld()->LineTraceSingleByObjectType(Ground,Surface.Position+FVector(0,0,4000),Surface.Position-FVector(0,0,2500),FCollisionObjectQueryParams(ECC_WorldStatic),Params);
            Surface.Height=Surface.Valid?Ground.ImpactPoint.Z:Camera.Z-650;
        }
        SurfaceIndex=(SurfaceIndex+1)%Surfaces.Num();
    }
    Mesh->SetWorldLocation(Centre);
    const FVector CameraRight=Rotation.RotateVector(FVector::RightVector);
    const FVector CameraUp=Rotation.RotateVector(FVector::UpVector);
    const float StormMotion=(Dust?1.f:GetBlizzardStrength())*Current.Intensity;
    const float GustPulse=.5f+.5f*FMath::Sin(float(Now)*.85f+FMath::Sin(float(Now)*.23f));
    const float Angle=FMath::DegreesToRadians(Current.Direction+180.f+StormMotion*22.f*FMath::Sin(float(Now)*.38f));
    const FVector Wind=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Current.Wind*65.f*(Ash?.25f:1.f)*FMath::Lerp(1.f,1.5f,VisualStrength-1.f)*(0.85f+0.15f*FMath::Sin(float(Now)*0.43f))*(1.f+StormMotion*(GustPulse*.65f-.15f));
    const FVector Velocity=Wind+FVector(0,0,Rain?-1900.f:Snow?FMath::Lerp(-85.f,-230.f,Current.Intensity):-25.f);
    const FVector Sway=FVector(-FMath::Sin(Angle),FMath::Cos(Angle),0);
    const int32 Active=FMath::Clamp(FMath::RoundToInt(Current.Intensity*VisualStrength*(Rain?4096:Snow?1800*(1.f+GetBlizzardStrength()):Ash?2400:2200)),0,DropCount);
    const float Night=CastChecked<AWowSkyManager>(GetOwner())->GetTimeOfDay();
    const float Brightness=(Night<330||Night>1140)?0.25f:0.8f;
    TArray<TPair<float,int32>> EmberNear; // (distance, index) of embers that are drawn
    for(int32 I=0;I<DropCount;++I)
    {
        FVector& P=Drops[I];
        P+=Velocity*Dt-CameraDelta-CentreShift;
        if(P.ContainsNaN()||CameraDelta.SizeSquared()>Radius*Radius)
            P=FVector(Random.FRandRange(-Radius,Radius),Random.FRandRange(-Radius,Radius),Random.FRandRange(-650,1050));
        if(Snow||Dust||Ash) P+=Sway*(FMath::Sin(float(Now)*1.1f+I)*(22.f+Current.Wind*7.f)*Dt);
        if(StormMotion>.01f) {
            const float Wave=float(Now)*1.8f+float(P.X)*.0018f+float(P.Y)*.0025f;
            P+=Sway*(FMath::Sin(Wave)*StormMotion*Current.Wind*18.f*Dt);
            P.Z+=FMath::Cos(Wave*.73f+I*.1f)*StormMotion*150.f*Dt;
        }
        if(Dust||Ash) P.Z+=FMath::Sin(float(Now)*0.8f+I)*Current.Wind*4.f*Dt;
        P.X-=FMath::FloorToDouble((P.X+Radius)/(Radius*2))*Radius*2;
        P.Y-=FMath::FloorToDouble((P.Y+Radius)/(Radius*2))*Radius*2;
        P.Z-=FMath::FloorToDouble((P.Z+650)/1700)*1700;
        const FVector World=Centre+P;
        const int32 X=FMath::Clamp(FMath::FloorToInt((World.X-Origin.X)/CellSize),0,Grid-1);
        const int32 Y=FMath::Clamp(FMath::FloorToInt((World.Y-Origin.Y)/CellSize),0,Grid-1);
        const FSurfaceCell& Cell=Surfaces[WowWeatherParticles::SurfaceSlot(World,CellSize,Grid)];
        const bool CellFresh=FVector::DistSquaredXY(Cell.Position,Origin+FVector((X+0.5f)*CellSize,(Y+0.5f)*CellSize,0))<100;
        const float GroundFade=WowWeatherParticles::GroundOpacity(CellFresh,Cell.Valid,World.Z-Cell.Height);
        const float Edge=FMath::Clamp((Radius-FMath::Max(FMath::Abs(P.X),FMath::Abs(P.Y)))/180,0.f,1.f)*FMath::Clamp((1050-P.Z)/120,0.f,1.f);
        // Indoors a cell not traced yet stays empty (r395) rather than snowing in the room.
        const float IndoorFade=(bIndoors&&!CellFresh)?0.f:1.f;
        // r459 (Stefan: "these trails still look bad"): rain drops a metre or two from the lens were
        // drawn as broad grey bands across the view (big, and smeared by depth of field). Rain now
        // fades out over 1.5-4.5 m from the camera; snow and dust keep the old near fade.
        const float NearFade=Rain?FMath::Clamp((float(FVector::Dist(World,Camera))-150)/300,0.f,1.f):FMath::Clamp((float(FVector::Dist(World,Camera))-70)/200,0.f,1.f);
        float Alpha=I<Active?ParticleExposure*IndoorFade*Edge*GroundFade*NearFade:0.f;
        // Atmospheric haze comes from the volume, not metre-wide white billboards.
        const bool Ember=Ash && I%EmberEvery==0;
        FVector Right=CameraRight*(Rain?0.8f:Snow?2.f+(I%5):Ash?0.7f+(I%4)*0.55f:0.5f+(I%3)*0.35f);
        FVector Up=Rain?-Velocity.GetSafeNormal()*(15.f+(I%4)*5):Dust?-Velocity.GetSafeNormal()*(3.f+(I%5)):CameraUp*Right.Size()*(Ash?0.45f:1.f);
        if(Ash) {
            const float Spin=FMath::Sin(float(Now)*(0.8f+(I%4)*0.2f)+I);
            Up+=CameraRight*Right.Size()*Spin*0.65f;
            P.Z+=(Ember?60.f+(I%5)*25.f:15.f)*Dt;
            // Embers ride the hot air: a slow corkscrew on top of the wind.
            if(Ember) P+=(Sway*FMath::Sin(float(Now)*2.1f+I)+FVector(FMath::Cos(float(Now)*1.7f+I*.5f),0,0))*30.f*Dt;
        }
        if(Ember) { Right=CameraRight*(1.3f+(I%3)*.5f); Up=CameraUp*Right.Size(); }
        FLinearColor Color=Rain?FLinearColor(0.65,0.76,0.87):Snow?FLinearColor(0.88,0.92,1):FLinearColor(0.54,0.32,0.08);
        if(Ash) { const float Grey=0.025f+(I%8)*0.025f; Color=FLinearColor(Grey,Grey*0.95f,Grey*0.90f); }
        if(Current.Kind==EWowWeatherKind::BlackRain) Color=FLinearColor(0.18,0.17,0.15);
        Color*=Brightness;
        const float Flicker=Ember ? .35f+.65f*FMath::Abs(FMath::Sin(float(Now)*(3.f+(I%4)*.9f)+I)) : 1.f;
        if(Ember) Color=FLinearColor(1.f,.32f,.03f)*Flicker;
        Color.A=Alpha*(Rain?0.35f:Snow?0.9f:Ash?(Ember?1.f:0.8f):0.30f);
        if(Ember && Alpha>.5f) EmberNear.Add({float(P.Size()),I});
        {
            // Hot quads have their UV moved up by 2 (the v2 material reads it as "ember").
            const float Hot=Ember?2.f:0.f;
            UV[I*4]=FVector2D(Hot,0); UV[I*4+1]=FVector2D(Hot+1,0); UV[I*4+2]=FVector2D(Hot+1,1); UV[I*4+3]=FVector2D(Hot,1);
        }
        const int32 V=I*4;
        Vertices[V]=P-Right-Up; Vertices[V+1]=P+Right-Up; Vertices[V+2]=P+Right+Up; Vertices[V+3]=P-Right+Up;
        for(int32 J=0;J<4;++J) Colors[V+J]=Color;
    }
    Mesh->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,Colors,{});
    auto UpdateFarRain=[&]()
    {
    if(!Mesh || GFarDrops.Num()!=FarDropCount) return;
    const int32 Active=Rain?FMath::Clamp(FMath::RoundToInt(Current.Intensity*VisualStrength*FarDropCount),0,FarDropCount):0;
    if(Active==0)
    {
        if(GFarShown)
        {
            for(FLinearColor& C:GFarColors) C=FLinearColor::Transparent;
            Mesh->UpdateMeshSection_LinearColor(2,GFarVertices,GFarNormals,GFarUV,GFarColors,{});
            GFarShown=false;
        }
        return;
    }
    GFarShown=true;
    // Roof grid: 24 cells per frame (the whole 120 m box in ~0.4 s). A cell is roofed when building geometry
    // (not skyline scenery, r574) is within 50 m above head height; doodads on the way are stepped through.
    for(int32 Trace=0;Trace<24;++Trace)
    {
        GFarRoofIndex=(GFarRoofIndex+1)%(FarGrid*FarGrid);
        const FIntPoint Base=FarRoofKey(Camera-FVector(FarRadius,FarRadius,0));
        const FIntPoint Key(Base.X+GFarRoofIndex%FarGrid,Base.Y+GFarRoofIndex/FarGrid);
        FFarRoofCell& Cell=GFarRoof[FarRoofSlot(Key)];
        Cell.Key=Key;
        FVector From((Key.X+.5)*FarCell,(Key.Y+.5)*FarCell,Camera.Z+100.0);
        const double Top=From.Z+5000.0;
        Cell.bRoof=false;
        for(int32 Step=0;Step<4;++Step)
        {
            FHitResult Up;
            if(!GetWorld()->LineTraceSingleByObjectType(Up,From,FVector(From.X,From.Y,Top),FCollisionObjectQueryParams(ECC_WorldStatic),Params)) break;
            if(IsRoofHit(Up)) { Cell.bRoof=true; break; }
            From=Up.ImpactPoint+FVector(0,0,5);
            if(From.Z>=Top) break;
        }
    }
    const FVector Dir=-Velocity.GetSafeNormal();
    FLinearColor Color=Current.Kind==EWowWeatherKind::BlackRain?FLinearColor(0.18,0.17,0.15):FLinearColor(0.65,0.76,0.87);
    Color*=Brightness;
    for(int32 I=0;I<FarDropCount;++I)
    {
        FVector& P=GFarDrops[I];
        P+=Velocity*Dt-CameraDelta;
        if(P.ContainsNaN()||CameraDelta.SizeSquared()>FarRadius*FarRadius)
            P=FVector(Random.FRandRange(-FarRadius,FarRadius),Random.FRandRange(-FarRadius,FarRadius),Random.FRandRange(FarBottom,FarTop));
        P.X-=FMath::FloorToDouble((P.X+FarRadius)/(FarRadius*2))*FarRadius*2;
        P.Y-=FMath::FloorToDouble((P.Y+FarRadius)/(FarRadius*2))*FarRadius*2;
        P.Z-=FMath::FloorToDouble((P.Z-FarBottom)/(FarTop-FarBottom))*(FarTop-FarBottom);
        const FVector World=Camera+P;
        const FIntPoint Key=FarRoofKey(World);
        const FFarRoofCell& Cell=GFarRoof[FarRoofSlot(Key)];
        const float Distance=float(P.Size());
        // Fresh outside cells only; start where the near drops (11 m box) end; fade at the box edges.
        float Alpha=0.f;
        if(I<Active && Cell.Key==Key && !Cell.bRoof)
            Alpha=FMath::Clamp((float(P.Size2D())-1000.f)/500.f,0.f,1.f)
                *FMath::Clamp((FarRadius-float(FMath::Max(FMath::Abs(P.X),FMath::Abs(P.Y))))/800.f,0.f,1.f)
                *FMath::Clamp((FarTop-float(P.Z))/300.f,0.f,1.f)*FMath::Clamp((float(P.Z)-FarBottom)/300.f,0.f,1.f);
        // About a pixel wide at any distance (1080p, 90 degrees: ~0.1 % of the distance per pixel).
        const FVector Side=FVector::CrossProduct(Dir,(World-Camera).GetSafeNormal()).GetSafeNormal()*FMath::Max(.8f,Distance*.0006f);
        const FVector Along=Dir*(15.f+Distance*.025f); // r585: .012 -> .025
        const FVector Local=P-CentreOffset;
        const int32 V=I*4;
        GFarVertices[V]=Local-Side-Along; GFarVertices[V+1]=Local+Side-Along; GFarVertices[V+2]=Local+Side+Along; GFarVertices[V+3]=Local-Side+Along;
        FLinearColor C=Color; C.A=Alpha*ParticleExposure*.45f; // r584: .3 -> .45
        for(int32 J=0;J<4;++J) GFarColors[V+J]=C;
    }
    Mesh->UpdateMeshSection_LinearColor(2,GFarVertices,GFarNormals,GFarUV,GFarColors,{});
    };
    UpdateFarRain();
    // Fire light on the closest embers (not right in the face): small, shadowless, flickering.
    if(Ash)
    {
        EmberNear.RemoveAll([](const TPair<float,int32>& E){return E.Key<120.f;});
        EmberNear.Sort([](const TPair<float,int32>& A,const TPair<float,int32>& B){return A.Key<B.Key;});
        GEmberLights.SetNum(EmberLightCount);
        for(int32 L=0;L<EmberLightCount;++L)
        {
            UPointLightComponent* Light=GEmberLights[L].Get();
            if(!Light && GetOwner())
            {
                Light=NewObject<UPointLightComponent>(GetOwner());
                Light->SetupAttachment(GetOwner()->GetRootComponent());
                Light->SetUsingAbsoluteLocation(true);
                Light->SetCastShadows(false);
                Light->SetIntensityUnits(ELightUnits::Candelas);
                Light->SetAttenuationRadius(260.f);
                Light->SetSourceRadius(2.f);
                Light->SetLightColor(FLinearColor(1.f,.42f,.08f));
                Light->SetVolumetricScatteringIntensity(0.f);
                Light->SetSpecularScale(.5f);
                Light->bAffectTranslucentLighting=false;
                Light->RegisterComponent();
                GEmberLights[L]=Light;
            }
            if(!Light) continue;
            if(!EmberNear.IsValidIndex(L)) { Light->SetVisibility(false); continue; }
            const int32 I=EmberNear[L].Value;
            Light->SetWorldLocation(Mesh->GetComponentLocation()+Drops[I]);
            Light->SetIntensity(6.f*(.35f+.65f*FMath::Abs(FMath::Sin(float(Now)*(3.f+(I%4)*.9f)+I))));
            Light->SetVisibility(true);
        }
    }
    if(Rain || !Splashes.IsEmpty()) UpdateRainSplashes(Dt,Camera,Rotation,Rain);
}


// r477 (Stefan, Orgrimmar sandstorm: "what's this black thing"): local fog volumes are lit only by
// the sun. Where a dust or mist volume reaches into a building's shadow its in-scattering drops to
// nothing while its density stays, so it drew a black hole over the doorway (and darkened the view
// out of a building). A little self-light in the fog's own colour, scaled with daylight, keeps a
// shadowed part a dim haze instead of black.
float UWowWeatherComponent::FogVolumeSelfLight() const
{
    const auto* Sky=Cast<AWowSkyManager>(GetOwner());
    const float Minutes=Sky?Sky->GetTimeOfDay():720.f;
    const float Day=FMath::Clamp((Minutes-300.f)/120.f,0.f,1.f)*FMath::Clamp((1260.f-Minutes)/120.f,0.f,1.f);
    // r702: a moonlit floor at night (was .015 - the night mist was black on black and could not be seen).
    return FMath::Lerp(.045f,.115f,Day);
}

void UWowWeatherComponent::UpdateStormGusts(float)
{
    const bool Dust=Current.Kind==EWowWeatherKind::Dust;
    const bool Ash=Current.Kind==EWowWeatherKind::BlackSnow;
    // r400: under a tree the gusts stay (indoors Exposure already removes them).
    const float Strength=((Dust||Ash)?1.f:GetBlizzardStrength())*Current.Intensity*Exposure*VisualStrength;
    if(Strength<.01f) { for(const auto& Gust : StormGusts) if(Gust) Gust->SetVisibility(false); return; }
    if(StormGusts.IsEmpty())
        for(int32 I=0;I<3;++I) {
            auto* Gust=NewObject<ULocalFogVolumeComponent>(GetOwner());
            Gust->SetMobility(EComponentMobility::Movable);
            Gust->SetHeightFogExtinction(0.f);
            Gust->SetFogPhaseG(.25f);
            Gust->RegisterComponent();
            StormGusts.Add(Gust);
        }
    // r484 (Stefan, Coldridge blizzard at night: "strange shadows out in the snow"): a gust is lit
    // by the sun/moon and the sky light, the weather's height fog around it is not (it has its own
    // colour). At night the gusts came out much darker than the fog and drifted over the snow as
    // grey shadows. Their self-light now reaches the height fog's own colour, so a gust is never
    // darker than the fog it sits in (by day the lit albedo still makes it a brighter drift).
    const FLinearColor FogGlow=[this]()
    {
        const AActor* Owner=GetOwner();
        const auto* Fog=Owner?Owner->FindComponentByClass<UExponentialHeightFogComponent>():nullptr;
        return Fog?Fog->FogInscatteringLuminance*.9f:FLinearColor::Black;
    }();
    const float Time=GetWorld()->GetTimeSeconds();
    const float Angle=FMath::DegreesToRadians(Current.Direction+180.f+(Ash?4.f:22.f)*FMath::Sin(Time*(Ash?.025f:.38f)));
    const FVector Wind(FMath::Cos(Angle),FMath::Sin(Angle),0), Side(-Wind.Y,Wind.X,0);
    for(int32 I=0;I<StormGusts.Num();++I)
    {
        auto* Gust=StormGusts[I].Get();
        const float Phase=FMath::Frac(Time/(Ash?48.f+I*9.f:8.f+I*1.7f)+I*.327f);
        const float Envelope=FMath::Square(FMath::Sin(Phase*PI));
        Gust->SetWorldLocation(LastCamera+Wind*FMath::Lerp(-3600.f,3600.f,Phase)+Side*(FMath::Sin(Time*(Ash?.06f:.4f)+I*2)*700.f)+FVector(0,0,-170.f+I*110.f));
        Gust->SetWorldScale3D(FVector((Ash?3.5f:2.2f)+I*.3f));
        Gust->SetRadialFogExtinction((Ash?.55f:Dust?.65f:.45f)*Strength*Envelope);
        const FLinearColor GustAlbedo=Ash?FLinearColor(.19f,.18f,.17f):Dust?FLinearColor(.78f,.49f,.16f):FLinearColor(.79f,.86f,.94f);
        Gust->SetFogAlbedo(GustAlbedo);
        const FLinearColor Self=GustAlbedo*FogVolumeSelfLight();
        const FLinearColor Glow(FMath::Max(Self.R,FogGlow.R),FMath::Max(Self.G,FogGlow.G),FMath::Max(Self.B,FogGlow.B));
        Gust->SetFogEmissive(Glow);
        Gust->SetVisibility(true);
    }
}


void UWowWeatherComponent::UpdateWaterMist(float)
{
    auto* Sky=Cast<AWowSkyManager>(GetOwner());
    if(!Sky || Zone==0) {
        for(const auto& Mist : WaterMist) if(Mist) Mist->SetVisibility(false);
        return;
    }
    const float Minutes=Sky->GetTimeOfDay();
    const float Dawn=FMath::Clamp((Minutes-270.f)/90.f,0.f,1.f)*FMath::Clamp((540.f-Minutes)/150.f,0.f,1.f);
    const float WeatherMist=Current.Kind==EWowWeatherKind::Fog ? Current.Intensity*.4f : 0.f;
    const float Amount=FMath::Max(Dawn,WeatherMist)*FMath::Clamp(1.f-Current.Wind/40.f,.05f,1.f);
    if(Amount<.005f || (Audio.IsValid() && Audio->IsUnderwaterMixEnabled())) {
        for(const auto& Mist : WaterMist) if(Mist) Mist->SetVisibility(false);
        return;
    }
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now>=NextMistSearch) {
        NextMistSearch=Now+2;
        FVector Forward=FVector::ForwardVector;
        if(auto* PC=UGameplayStatics::GetPlayerController(this,0)) {
            FVector View; FRotator Facing; PC->GetPlayerViewPoint(View,Facing);
            Forward=Facing.Vector().GetSafeNormal2D();
        }
        MistFocus=LastCamera+Forward*1000.f;
        TArray<UPrimitiveComponent*> Candidates;
        FWowWaterRenderer::GatherMistWater(GetWorld(),LastCamera,12000.f,Candidates);
        auto SurfaceDistance=[&](const UPrimitiveComponent& C) {
            const FBox B=C.Bounds.GetBox();
            return FVector::DistSquared(MistFocus,FVector(FMath::Clamp(MistFocus.X,B.Min.X,B.Max.X),FMath::Clamp(MistFocus.Y,B.Min.Y,B.Max.Y),B.Max.Z-18.f));
        };
        Candidates.Sort([&](const UPrimitiveComponent& A,const UPrimitiveComponent& B){return SurfaceDistance(A)<SurfaceDistance(B);});
        if(Now>=NextMistDiagnostic) {
            NextMistDiagnostic=Now+10;
            auto Value=[](const TCHAR* Name){auto* C=IConsoleManager::Get().FindConsoleVariable(Name);return C?C->GetFloat():-1.f;};
            UE_LOG(LogWowWeather,Log,TEXT("Water mist: minutes=%.0f strength=%.2f waterSurfaces=%d localFog=%.0f support=%.0f startCm=%.0f volumetric=%.0f inject=%.0f translucent=%.0f"),Minutes,Amount,Candidates.Num(),Value(TEXT("r.LocalFogVolume")),Value(TEXT("r.SupportLocalFogVolumes")),Value(TEXT("r.LocalFogVolume.GlobalStartDistance")),Value(TEXT("r.VolumetricFog")),Value(TEXT("r.LocalFogVolume.RenderIntoVolumetricFog")),Value(TEXT("r.LocalFogVolume.ApplyOnTranslucent")));
        }
        MistWater.Reset();
        for(int32 I=0;I<FMath::Min(24,Candidates.Num());++I) MistWater.Add(Candidates[I]);
    }
    while(WaterMist.Num()<MistWater.Num()*2) {
        auto* Mist=NewObject<ULocalFogVolumeComponent>(GetOwner());
        Mist->SetMobility(EComponentMobility::Movable);
        Mist->SetRadialFogExtinction(.45f);
        // UE multiplies radial and height coverage (also in volumetric mode).
        // A near-zero radial extinction gates away the entire height layer.
        // Falloff is additionally scaled by 0.01 in UE's scene data.
        Mist->SetHeightFogFalloff(1000.f);
        Mist->SetHeightFogOffset(0.f);
        Mist->SetFogAlbedo(FLinearColor(.78f,.85f,.90f));
        // A small ambient term keeps dawn mist readable over dark water even
        // before the directional sun illuminates it. Opacity still fades by time.
        Mist->SetFogEmissive(FLinearColor(.035f,.043f,.05f));
        GetOwner()->AddInstanceComponent(Mist);
        Mist->RegisterComponent(); WaterMist.Add(Mist);
    }
    for(int32 I=0;I<WaterMist.Num();++I) {
        auto* Mist=WaterMist[I].Get();
        const int32 MistSurfaceIndex=I/2;
        auto* Water=MistWater.IsValidIndex(MistSurfaceIndex)?MistWater[MistSurfaceIndex].Get():nullptr;
        if(!Water) { Mist->SetVisibility(false); continue; }
        const FVector Extent=Water->Bounds.BoxExtent, Center=Water->Bounds.Origin;
        // Seed by water location, never by distance-sorted slot or camera position.
        // Neighbouring water rows consequently receive different, stable patches.
        const uint32 Seed=HashCombineFast(GetTypeHash(FMath::RoundToInt(Center.X)),
            HashCombineFast(GetTypeHash(FMath::RoundToInt(Center.Y)),GetTypeHash(FMath::RoundToInt(Center.Z))));
        FRandomStream MistRandom(HashCombineFast(Seed,uint32(I%2)));
        const float PatchRadius=FMath::Clamp(float(FMath::Min(Extent.X,Extent.Y))*MistRandom.FRandRange(.65f,.95f),20.f,800.f);
        const float LayerHeight=MistRandom.FRandRange(28.f,55.f);
        const float Density=MistRandom.FRandRange(.28f,.46f);
        FVector P=Center;
        const float RoomX=FMath::Max(0.f,float(Extent.X)-PatchRadius);
        const float RoomY=FMath::Max(0.f,float(Extent.Y)-PatchRadius);
        P.X+=MistRandom.FRandRange(-RoomX,RoomX);
        P.Y+=MistRandom.FRandRange(-RoomY,RoomY);
        const float Phase=MistRandom.FRandRange(0.f,2.f*PI);
        // Advect with the same wind as weather particles. Wrap inside each
        // water surface and fade at its edges so recycling is never a visible jump.
        const float Crosswind=FMath::Sin(Now*.041+Phase)*PatchRadius*.12f;
        const float WindAngle=FMath::DegreesToRadians(Current.Direction+180.f);
        auto Wrap=[](double Position,float Room) {
            if(Room<1.f) return 0.f;
            const double Span=2.0*Room;
            return float(FMath::Fmod(FMath::Fmod(Position+Room,Span)+Span,Span)-Room);
        };
        const float X=Wrap(P.X-Center.X+MistWindOffset.X-FMath::Sin(WindAngle)*Crosswind,RoomX);
        const float Y=Wrap(P.Y-Center.Y+MistWindOffset.Y+FMath::Cos(WindAngle)*Crosswind,RoomY);
        P.X=Center.X+X; P.Y=Center.Y+Y;
        const float EdgeX=RoomX<1.f ? 1.f : FMath::Clamp((RoomX-FMath::Abs(X))/FMath::Min(RoomX,PatchRadius*.3f),0.f,1.f);
        const float EdgeY=RoomY<1.f ? 1.f : FMath::Clamp((RoomY-FMath::Abs(Y))/FMath::Min(RoomY,PatchRadius*.3f),0.f,1.f);
        const float Fade=FMath::Min(EdgeX,EdgeY)*(.85f+.15f*FMath::Sin(Now*.025+Phase));
        P.Z=Center.Z+Extent.Z-18.f;
        Mist->SetWorldLocation(P);
        Mist->SetWorldScale3D(FVector(PatchRadius/ULocalFogVolumeComponent::GetBaseVolumeSize()));
        // LFV scales uniformly: flatten its density, not its transform. The engine
        // multiplies falloff by .01 in unit-sphere space. This gives a 28-55cm
        // e-folding height regardless of the horizontal patch PatchRadius.
        Mist->SetHeightFogFalloff(100.f*PatchRadius/LayerHeight);
        Mist->SetRadialFogExtinction(Density*Amount*Fade);
        Mist->SetHeightFogExtinction(.8f*Amount*Fade);
        Mist->SetVisibility(true);
    }
}

void UWowWeatherComponent::UpdateRainSplashes(float Dt, const FVector& Camera, const FRotator& Rotation, bool bRainVisible)
{
    constexpr int32 Count=96, RingSegments=20, Quads=RingSegments+3;
    if(Splashes.IsEmpty())
    {
        Splashes.SetNum(Count);
        TArray<int32> Indices;
        for(int32 I=0;I<Count*Quads;++I)
        {
            const int32 V=SplashVertices.Num();
            SplashVertices.AddZeroed(4);
            SplashNormals.Append({FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
            if(I%Quads<RingSegments) SplashUV.Append({FVector2D(.5,.5),FVector2D(.5,.5),FVector2D(.5,.5),FVector2D(.5,.5)});
            else SplashUV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
            SplashColors.Append({FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent});
            Indices.Append({V,V+1,V+2,V,V+2,V+3});
        }
        Mesh->CreateMeshSection_LinearColor(1,SplashVertices,Indices,SplashNormals,SplashUV,SplashColors,{},false);
        Mesh->SetMaterial(1,Mesh->GetMaterial(0));
    }
    for(auto& Splash:Splashes) Splash.Age+=Dt;
    if(!bRainVisible)
    {
        SplashBudget=0;
        for(auto& Splash:Splashes) Splash.Age=10;
        Mesh->SetMeshSectionVisible(1,false);
        return;
    }
    Mesh->SetMeshSectionVisible(1,true);
    SplashBudget=FMath::Min(3.f,SplashBudget+Dt*90.f*Current.Intensity*VisualStrength*Exposure);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WeatherRainSplash),true,GetOwner());
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0)) if(PC->GetPawn()) Query.AddIgnoredActor(PC->GetPawn());
    const FVector Forward=FRotator(0,Rotation.Yaw,0).Vector();
    const double SplashNow=GetWorld()->GetTimeSeconds();
    if(SplashNow>=NextSplashWaterSearch)
    {
        NextSplashWaterSearch=SplashNow+.5;
        TArray<UPrimitiveComponent*> Nearby;
        FWowWaterRenderer::GatherMistWater(GetWorld(),Camera,4000.f,Nearby,true);
        SplashWater.Reset();for(auto* Water:Nearby) SplashWater.Add(Water);
    }
    while(SplashBudget>=1.f)
    {
        SplashBudget-=1.f;
        const float Angle=SplashRandom.FRandRange(0.f,2.f*PI);
        const float SplashRadius=FMath::Sqrt(SplashRandom.FRand())*650.f;
        const auto* Player=UGameplayStatics::GetPlayerPawn(this,0);
        const FVector Anchor=Player ? Player->GetActorLocation() : Camera;
        const FVector P=Anchor+Forward*300.f+FVector(FMath::Cos(Angle)*SplashRadius,FMath::Sin(Angle)*SplashRadius,0);
        FHitResult Hit;
        // At most three actual surface traces per 30 Hz update, no per-drop collisions.
        if(!GetWorld()->LineTraceSingleByObjectType(Hit,P+FVector(0,0,800),P-FVector(0,0,3500),
            FCollisionObjectQueryParams(ECC_WorldStatic),Query) || Hit.ImpactNormal.Z<.5f) continue;
        // r533 (Stefan, inside the Stormwind gatehouse in heavy rain: "specks of rain inside"): the trace
        // starts 8 m above the player, below a high roof, so it landed on the room's floor and splashed
        // there. A splash needs open sky above it: nothing of a building overhead (r536: looking up the
        // way the rain comes, so wind-blown rain splashes a metre or two inside a windward door).
        {
            FHitResult Over;
            const FVector From=Hit.ImpactPoint+FVector(0,0,10);
            if(GetWorld()->LineTraceSingleByObjectType(Over,From,From+RainSourceDirection(Current,VisualStrength)*12000.f,FCollisionObjectQueryParams(ECC_WorldStatic),Query)
                && IsRoofHit(Over)) continue; // r586: skyline scenery (Stormwind's far keep) is not a roof
        }
        bool bWater=false;
        for(const auto& Weak:SplashWater) if(const auto* Water=Weak.Get())
        {
            const FBox Box=Water->Bounds.GetBox();const float Z=Box.Max.Z-18.f;
            if(P.X>=Box.Min.X&&P.X<=Box.Max.X&&P.Y>=Box.Min.Y&&P.Y<=Box.Max.Y &&
                Z>Hit.ImpactPoint.Z && Z<P.Z+800)
            {
                Hit.ImpactPoint=FVector(P.X,P.Y,Z);Hit.ImpactNormal=FVector::UpVector;bWater=true;
            }
        }
        auto& Splash=Splashes[NextSplash]; NextSplash=(NextSplash+1)%Count;
        Splash.bWater=bWater;
        Splash.Position=Hit.ImpactPoint+Hit.ImpactNormal*2.5f;
        Splash.Normal=Hit.ImpactNormal;
        Splash.Age=0; Splash.Life=bWater ? SplashRandom.FRandRange(.45f,.7f) : SplashRandom.FRandRange(.12f,.24f);
        Splash.Size=bWater ? SplashRandom.FRandRange(5.f,10.f) : SplashRandom.FRandRange(2.f,4.f);
    }
    const FVector CameraRight=Rotation.RotateVector(FVector::RightVector);
    const float Minutes=CastChecked<AWowSkyManager>(GetOwner())->GetTimeOfDay();
    const float Brightness=(Minutes<330||Minutes>1140)?.32f:.75f;
    for(int32 I=0;I<Count;++I)
    {
        const auto& Splash=Splashes[I];
        const float T=FMath::Clamp(Splash.Age/FMath::Max(Splash.Life,.01f),0.f,1.f);
        if(T>=1.f)
        {
            for(int32 V=I*Quads*4;V<(I+1)*Quads*4;++V) { SplashVertices[V]=FVector::ZeroVector; SplashColors[V]=FLinearColor::Transparent; }
            continue;
        }
        const float Alpha=(1.f-T)*Exposure*.3f;
        FVector X,Y; Splash.Normal.FindBestAxisVectors(X,Y);
        const FVector Centre=Splash.Position-Camera;
        const float SplashRadius=Splash.Size*(.3f+T*1.8f), Width=.25f*(1.f-T)+.12f;
        for(int32 Q=0;Q<Quads;++Q)
        {
            const int32 V=(I*Quads+Q)*4;
            if(Q<RingSegments)
            {
                const float A=2.f*PI*Q/RingSegments, B=2.f*PI*(Q+1)/RingSegments;
                const FVector D0=X*FMath::Cos(A)+Y*FMath::Sin(A),D1=X*FMath::Cos(B)+Y*FMath::Sin(B);
                SplashVertices[V]=Centre+D0*(SplashRadius-Width); SplashVertices[V+1]=Centre+D1*(SplashRadius-Width);
                SplashVertices[V+2]=Centre+D1*(SplashRadius+Width); SplashVertices[V+3]=Centre+D0*(SplashRadius+Width);
            }
            else
            {
                const float A=(Q-RingSegments)*2.f*PI/3.f+I;
                const FVector P=Centre+(X*FMath::Cos(A)+Y*FMath::Sin(A))*Splash.Size*T
                    +Splash.Normal*(FMath::Sin(T*PI)*Splash.Size*1.7f);
                const FVector R=CameraRight*.65f, U=Splash.Normal*(1.f+2.f*(1.f-T));
                SplashVertices[V]=P-R-U; SplashVertices[V+1]=P+R-U;
                SplashVertices[V+2]=P+R+U; SplashVertices[V+3]=P-R+U;
            }
            const float PartAlpha=Q<RingSegments ? (Splash.bWater ? Alpha*.45f : 0.f) : Alpha;
            for(int32 J=0;J<4;++J) SplashColors[V+J]=FLinearColor(.42f*Brightness,.48f*Brightness,.52f*Brightness,PartAlpha);
        }
    }
    Mesh->UpdateMeshSection_LinearColor(1,SplashVertices,SplashNormals,SplashUV,SplashColors,{});
}
