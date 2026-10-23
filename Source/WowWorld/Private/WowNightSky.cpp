#include "WowSkyManager.h"
#include "WowLunarPhase.h"
#include "WowCookedMaterial.h"
#include "WowRuntimeMeshMaterials.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Misc/Paths.h"
#include "Mpq/MpqManager.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
// Sky\Moon.* may be an equirectangular colour map (2:1, e.g. NASA's LRO map) or a
// plain photo of the moon's disc (about 1:1). The disc shader samples a 2:1 map, so
// a disc photo is reprojected here: the near side from the photo, the far side mirrored.
UTexture2D* LoadMoonPhoto(const FString& Path)
{
    FImage Source;
    if (!FImageUtils::LoadImage(*Path, Source) || Source.SizeX < 16 || Source.SizeY < 16) return nullptr;
    const float Aspect = float(Source.SizeX) / float(Source.SizeY);
    if (Aspect > 1.6f) return FImageUtils::ImportFileAsTexture2D(Path); // already a map
    Source.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    const TArrayView64<FColor> In = Source.AsBGRA8();
    const int32 W = 1024, H = 512;
    FImage Map(W, H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    TArrayView64<FColor> Out = Map.AsBGRA8();
    const float Cx = Source.SizeX * 0.5f, Cy = Source.SizeY * 0.5f;
    const float R = FMath::Min(Source.SizeX, Source.SizeY) * 0.475f; // disc inside the frame (padding, fake checkerboard)
    for (int32 Y = 0; Y < H; ++Y)
    {
        const float Lat = (0.5f - (Y + 0.5f) / H) * PI;
        for (int32 X = 0; X < W; ++X)
        {
            float Lon = ((X + 0.5f) / W - 0.5f) * 2.f * PI;
            if (FMath::Abs(Lon) > HALF_PI) Lon = FMath::Sign(Lon) * (PI - FMath::Abs(Lon)); // far side: mirror
            const float Px = Cx + FMath::Cos(Lat) * FMath::Sin(Lon) * R;
            const float Py = Cy - FMath::Sin(Lat) * R;
            const int32 Sx = FMath::Clamp(FMath::RoundToInt(Px), 0, int32(Source.SizeX) - 1);
            const int32 Sy = FMath::Clamp(FMath::RoundToInt(Py), 0, int32(Source.SizeY) - 1);
            FColor C = In[int64(Sy) * Source.SizeX + Sx]; C.A = 255;
            Out[int64(Y) * W + X] = C;
        }
    }
    UE_LOG(LogTemp, Log, TEXT("Moon photo %s is a disc (%dx%d): reprojected to a %dx%d map"), *Path, Source.SizeX, Source.SizeY, W, H);
    return FImageUtils::CreateTexture2DFromImage(Map);
}
}

namespace
{
UStaticMesh* BuildNightMesh(UObject* Outer, const TArray<FVector3f>& Points,
    const TArray<FVector2f>& TexCoords, const TArray<FVector4f>& Colors, const TArray<int32>& Indices)
{
    FMeshDescription Desc;
    FStaticMeshAttributes Attributes(Desc);
    Attributes.Register();
    const FPolygonGroupID Group = Desc.CreatePolygonGroup();
    TArray<FVertexInstanceID> Instances;
    for (int32 I = 0; I < Points.Num(); ++I)
    {
        const FVertexID Vertex = Desc.CreateVertex();
        Attributes.GetVertexPositions()[Vertex] = Points[I];
        const FVertexInstanceID Instance = Desc.CreateVertexInstance(Vertex);
        Instances.Add(Instance);
        Attributes.GetVertexInstanceNormals()[Instance] = FVector3f(-1,0,0);
        Attributes.GetVertexInstanceTangents()[Instance] = FVector3f(0,1,0);
        Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1;
        Attributes.GetVertexInstanceUVs().Set(Instance,0,TexCoords[I]);
        Attributes.GetVertexInstanceColors()[Instance] = Colors[I];
    }
    for (int32 I = 0; I < Indices.Num(); I += 3)
        Desc.CreatePolygon(Group, {Instances[Indices[I]], Instances[Indices[I+1]], Instances[Indices[I+2]]});
    UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer);
    FStaticMaterial Slot;
    Slot.UVChannelData.bInitialized = true;
    Mesh->GetStaticMaterials().Add(Slot);
    WowPrepareRuntimeMeshMaterials(Mesh, Desc);
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bFastBuild = true;
    Params.bBuildSimpleCollision = false;
    Mesh->BuildFromMeshDescriptions({&Desc}, Params);
    Mesh->NeverStream = true;
    return Mesh;
}

UMaterialInstanceDynamic* NightMaterial(UObject* Outer, bool bMoon)
{
    const FString Name = bMoon ? TEXT("M_WowLunarDisc") : TEXT("M_WowNightStars");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(Outer);
        Material->SetShadingModel(MSM_Unlit);
        Material->BlendMode = BLEND_Translucent;
        Material->TwoSided = true;
        // Keep mountains/buildings in front; the cloud volume composites over these distant surfaces.
        Material->bDisableDepthTest = false;
        Material->bApplyCloudFogging = true;
        Material->bComputeFogPerPixel = true;
        auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
        auto* Color = NewObject<UMaterialExpressionVertexColor>(Material);
        auto* Visibility = NewObject<UMaterialExpressionScalarParameter>(Material);
        Visibility->ParameterName = TEXT("NightVisibility");
        Visibility->DefaultValue = 0;
        auto* Phase = NewObject<UMaterialExpressionScalarParameter>(Material);
        Phase->ParameterName = TEXT("LunarPhase");
        Phase->DefaultValue = 0.5f;
        auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
        Shader->OutputType = CMOT_Float4;
        const auto Input = [Shader](const TCHAR* Name, UMaterialExpression* Expression)
        {
            FCustomInput Value;
            Value.InputName = Name;
            Value.Input.Connect(0, Expression);
            Shader->Inputs.Add(Value);
        };
        Input(TEXT("UV"),UV);
        Input(TEXT("Tint"),Color);
        Input(TEXT("Visibility"),Visibility);
        Input(TEXT("Phase"),Phase);
        TArray<UMaterialExpression*> MoonInputs;
        if (bMoon)
        {
            auto* Roll = NewObject<UMaterialExpressionScalarParameter>(Material);
            Roll->ParameterName = TEXT("TextureRoll");
            Roll->DefaultValue = 0.f;
            auto* HasTexture = NewObject<UMaterialExpressionScalarParameter>(Material);
            HasTexture->ParameterName = TEXT("HasMoonTexture");
            HasTexture->DefaultValue = 0.f;
            auto* MoonTexture = NewObject<UMaterialExpressionTextureObjectParameter>(Material);
            MoonTexture->ParameterName = TEXT("MoonTexture");
            MoonTexture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
            Input(TEXT("TextureRoll"),Roll);
            Input(TEXT("HasMoonTexture"),HasTexture);
            Input(TEXT("MoonTex"),MoonTexture);
            MoonInputs = {Roll, HasTexture, MoonTexture};
        }
        Shader->Code = bMoon ? TEXT(R"(
// The quad is 1.6x the lunar disc: the outer ring carries a soft glow.
float2 p = (UV * 2 - 1) * 1.6;
float r = length(p);
float edge = 1 - smoothstep(0.985, 1.0, r);
float z = sqrt(saturate(1 - r * r));
float3 n = float3(p.x, -p.y, z);
float angle = Phase * 6.28318530718;
float3 L = float3(-sin(angle), 0, -cos(angle));
float mu0 = dot(n, L);
float lit = saturate(mu0);
// Lommel-Seeliger: the moon looks evenly bright to its limb rather than like a lit ball.
float ls = 2 * lit / (lit + z + 0.0001);
float terminator = smoothstep(-0.015, 0.07, mu0);
// Surface in sky orientation: the disc rolls to face the sun, the face stays upright.
float cr = cos(TextureRoll); float sr = sin(TextureRoll);
float2 q = float2(cr * n.x + sr * n.y, -sr * n.x + cr * n.y);
float2 s = q / (0.55 + 0.45 * z);
// Procedural surface: maria from value-noise fbm, two crater sizes, one rayed crater.
float fbm = 0; float amp = 0.5; float2 x = s * 1.9 + float2(3.1, 7.7);
[unroll] for (int o = 0; o < 7; ++o) {
    float2 i0 = floor(x); float2 f = frac(x); f = f * f * (3 - 2 * f);
    float h00 = frac(sin(dot(i0, float2(127.1, 311.7))) * 43758.5453);
    float h10 = frac(sin(dot(i0 + float2(1, 0), float2(127.1, 311.7))) * 43758.5453);
    float h01 = frac(sin(dot(i0 + float2(0, 1), float2(127.1, 311.7))) * 43758.5453);
    float h11 = frac(sin(dot(i0 + float2(1, 1), float2(127.1, 311.7))) * 43758.5453);
    fbm += amp * lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
    x = x * 2.07 + float2(1.3, 0.7); amp *= 0.5;
}
float maria = smoothstep(0.47, 0.60, fbm + 0.10 * s.y - 0.06 * s.x);
float albedo = lerp(0.86, 0.48, maria);
float2 sunDir2 = normalize(float2(L.x, L.y) + float2(0.0001, 0));
[unroll] for (int i = 0; i < 40; ++i) {
    float big = i < 16 ? 1.0 : 0.35;
    float2 c = frac(sin(float2(i * 13.13 + 2.7, i * 7.37 + 9.1)) * 43758.54) * 1.7 - 0.85;
    float radius = (0.016 + pow(frac(sin(i * 11.9 + 4.3) * 1245.67), 2.2) * 0.10) * big;
    float2 dv = (s - c) / radius;
    float d = length(dv);
    float side = dot(dv / max(d, 0.001), sunDir2);
    float bowl = 1 - smoothstep(0.55, 0.95, d);
    float rim = exp(-pow((d - 1) * 7, 2));
    albedo += bowl * (0.05 + 0.10 * side) * lerp(1, 0.6, maria);
    albedo += rim * (0.05 - 0.07 * side);
}
float2 ray = s - float2(-0.18, 0.58);
float rayLen = length(ray);
albedo += pow(saturate(sin(atan2(ray.y, ray.x) * 23) * 0.5 + 0.5), 8) * exp(-rayLen * 3.2) * 0.35 + exp(-rayLen * rayLen * 900) * 0.25;
float grain = frac(sin(dot(floor(s * 420), float2(12.9898, 78.233))) * 43758.5453);
albedo *= 0.95 + grain * 0.07;
float3 surface = float3(0.97, 0.95, 0.90) * albedo;
// Optional photograph (Sky\Moon.jpg, equirectangular, near side centred) replaces the procedural albedo.
float lon = atan2(q.x, z); float lat = asin(clamp(q.y, -1, 1));
float3 photo = Texture2DSample(MoonTex, MoonTexSampler, float2(0.5 + lon / 6.2831853, 0.5 - lat / 3.14159265)).rgb;
surface = lerp(surface, photo * 1.15, saturate(HasMoonTexture));
float3 discColor = surface * (terminator * ls * 1.25) + surface * float3(0.20, 0.25, 0.34) * 0.06;
float litFraction = 0.5 - 0.5 * cos(angle);
float glow = exp(-max(r - 1, 0) * 6.5) * (1 - edge) * 0.32 * litFraction * (1 - smoothstep(1.45, 1.6, r));
float3 glowColor = float3(0.78, 0.84, 1.0);
return float4(discColor * edge + glowColor * (1 - edge), saturate(edge + glow) * Visibility);
)") : TEXT(R"(
float r = length(UV*2-1);
float alpha = exp(-r*r*6.0)*(1-smoothstep(0.65,1,r));
return float4(Tint.rgb*1.8,alpha*Visibility);
)");
        auto* RGB = NewObject<UMaterialExpressionComponentMask>(Material);
        RGB->Input.Connect(0,Shader); RGB->R=true; RGB->G=true; RGB->B=true;
        auto* Alpha = NewObject<UMaterialExpressionComponentMask>(Material);
        Alpha->Input.Connect(0,Shader); Alpha->A=true;
        for (UMaterialExpression* Expression : TArray<UMaterialExpression*>{UV,Color,Visibility,Phase,Shader,RGB,Alpha})
            Material->GetExpressionCollection().AddExpression(Expression);
        for (UMaterialExpression* Expression : MoonInputs)
            Material->GetExpressionCollection().AddExpression(Expression);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0,RGB);
        Material->GetEditorOnlyData()->Opacity.Connect(0,Alpha);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material,Name);
    }
#endif
    return UMaterialInstanceDynamic::Create(Material ? Material : UMaterial::GetDefaultMaterial(MD_Surface),Outer);
}
}

void AWowSkyManager::CreateNightSky()
{
    StarsMaterial = NightMaterial(this,false);
    TArray<FVector3f> Points;
    TArray<FVector2f> UVs;
    TArray<FVector4f> Colors;
    TArray<int32> Indices;
    FRandomStream Random(19760327);
    // A single mesh/draw for the entire field. Stable stars, no per-star components or ticking.
    for (int32 I=0; I<1800; ++I)
    {
        const float Z=Random.FRandRange(0.05f,1.0f);
        const float Azimuth=Random.FRandRange(0.0f,2*PI);
        const float R=FMath::Sqrt(1-Z*Z);
        const FVector3f Direction(R*FMath::Cos(Azimuth),R*FMath::Sin(Azimuth),Z);
        const FVector3f Right=FVector3f::CrossProduct(Direction,FVector3f(0,0,1)).GetSafeNormal();
        const FVector3f Up=FVector3f::CrossProduct(Right,Direction).GetSafeNormal();
        const float Size=Random.FRandRange(240.f,620.f);
        const float Brightness=FMath::Pow(Random.FRand(),2.f)*0.8f+0.2f;
        const float Warmth=Random.FRand();
        const FVector4f Color(Brightness, Brightness*FMath::Lerp(0.82f,0.96f,Warmth), Brightness*FMath::Lerp(0.66f,1.12f,Warmth),1);
        const int32 Base=Points.Num();
        for (const FVector2f UV : {FVector2f(0,0),FVector2f(1,0),FVector2f(1,1),FVector2f(0,1)})
        {
            Points.Add(Direction*650000.f + Right*((UV.X*2-1)*Size)+Up*((UV.Y*2-1)*Size));
            UVs.Add(UV); Colors.Add(Color);
        }
        Indices.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
    }
    StarsMesh = NewObject<UStaticMeshComponent>(this,TEXT("NightStars"));
    StarsMesh->SetStaticMesh(BuildNightMesh(this,Points,UVs,Colors,Indices));
    StarsMesh->SetMaterial(0,StarsMaterial);
    StarsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StarsMesh->SetCastShadow(false);
    StarsMesh->bAffectDistanceFieldLighting=false;
    StarsMesh->TranslucencySortPriority=-30;
    StarsMesh->RegisterComponent();
    StarsMesh->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
    MoonDiscMaterial = NightMaterial(this,true);
    // Optional user-supplied moon photo (e.g. NASA's public-domain LRO colour map,
    // equirectangular) in <client>\Sky\Moon.jpg or Moon.png. Nothing is shipped.
    if (MoonDiscMaterial)
    {
        const FString SkyFolder = FPaths::Combine(FMpqManager::FindUserRoot(FPaths::ProjectDir()), TEXT("Sky"));
        for (const TCHAR* FileName : {TEXT("Moon.jpg"), TEXT("Moon.png"), TEXT("moon.jpg"), TEXT("moon.png")})
        {
            const FString Path = FPaths::Combine(SkyFolder, FileName);
            if (!FPaths::FileExists(Path)) continue;
            if (UTexture2D* Photo = LoadMoonPhoto(Path))
            {
                MoonPhotoTexture = Photo;
                MoonDiscMaterial->SetTextureParameterValue(TEXT("MoonTexture"), Photo);
                MoonDiscMaterial->SetScalarParameterValue(TEXT("HasMoonTexture"), 1.f);
                UE_LOG(LogTemp, Log, TEXT("Moon photo loaded: %s"), *Path);
            }
            break;
        }
    }
    if (MoonDiscMesh)
    {
        // A readable stylized angular diameter (~2 degrees), independent of phase.
        MoonDiscMesh->SetStaticMesh(BuildNightMesh(this,
            {{0,-14400,-14400},{0,14400,-14400},{0,14400,14400},{0,-14400,14400}}, // disc radius 9000 + glow ring
            {{0,1},{1,1},{1,0},{0,0}},
            {FVector4f(1,1,1,1),FVector4f(1,1,1,1),FVector4f(1,1,1,1),FVector4f(1,1,1,1)}, {0,1,2,0,2,3}));
        MoonDiscMesh->SetMaterial(0,MoonDiscMaterial);
        MoonDiscMesh->TranslucencySortPriority=-20;
    }
}

void AWowSkyManager::UpdateNightSky()
{
    // Opt-in camera fixture for repeatable UE screenshots; never alters regular gameplay.
    if (FParse::Param(FCommandLine::Get(),TEXT("skypreview")) && MoonDiscMesh)
    {
        if (APlayerController* PC=UGameplayStatics::GetPlayerController(this,0))
        {
            if (!NightPreviewCamera)
            {
                const FVector Position=PC->PlayerCameraManager->GetCameraLocation();
                NightPreviewCamera=GetWorld()->SpawnActor<ACameraActor>(Position,FRotator(28,25,0));
                NightPreviewCamera->GetCameraComponent()->SetFieldOfView(80);
                PC->SetViewTarget(NightPreviewCamera);
            }
        }
    }
    FDateTime Utc=FDateTime::UtcNow();
    FString OverrideDate;
    if (FParse::Value(FCommandLine::Get(),TEXT("-moondate="),OverrideDate))
    {
        FDateTime Parsed;
        if (FDateTime::ParseIso8601(*OverrideDate,Parsed)) Utc=Parsed;
    }
    const int64 Minute=Utc.ToUnixTimestamp()/60;
    if (Minute != LastLunarUpdateMinute)
    {
        LastLunarUpdateMinute=Minute;
        LunarPhase=WowLunarPhase::Cycle(Utc);
        if (MoonDiscMaterial) MoonDiscMaterial->SetScalarParameterValue(TEXT("LunarPhase"),LunarPhase);
    }
    const float Night=WowLunarPhase::NightVisibility(TimeOfDay);
    const float WeatherVisibility=(1.f-0.85f*CurrentWeatherGrade)*(1.f-0.85f*FMath::Square(CurrentWeatherCloud));
    if (StarsMaterial)
        StarsMaterial->SetScalarParameterValue(TEXT("NightVisibility"),Night*WeatherVisibility*(1.f-0.35f*WowLunarPhase::Illumination(LunarPhase)));
    if (StarsMesh)
    {
        StarsMesh->SetVisibility(Night>0.001f);
        if (APlayerCameraManager* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
            StarsMesh->SetWorldLocation(Camera->GetCameraLocation());
        // Slow rotation over the real day; never tied to frame count or offline fast-forward.
        StarsMesh->SetWorldRotation(FRotator(0,TimeOfDay*0.25f,0));
    }
    if (MoonDiscMaterial)
        // r540: the moon rises into the second half of the night curve, after the sun has faded
        // (WowSkySunDuskFade), so sun and moon are never shown together.
        MoonDiscMaterial->SetScalarParameterValue(TEXT("NightVisibility"),FMath::SmoothStep(0.f,1.f,Night*2.f-1.f)*WeatherVisibility);
}
