#include "WowTumbleweedComponent.h"
#include "WowWeatherComponent.h"
#include "WowWorldManager.h"
#include "WowDoodadManager.h"
#include "WowCookedMaterial.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "WowRuntimeMeshMaterials.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTumbleweed, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarTumbleweeds(TEXT("wow.Tumbleweeds"), 1,
    TEXT("Local-only tumbleweeds in dry zones: 0 off, 1 on, 2 test (any zone)."));
TAutoConsoleVariable<FString> CVarTumbleweedModel(TEXT("wow.TumbleweedModel"), TEXT(""),
    TEXT("Optional M2 path used for tumbleweeds instead of the built-in twig ball."));

constexpr float SpawnMinDistance = 1600.0f;
constexpr float SpawnMaxDistance = 14000.0f; // also out in the distance
constexpr float FleeDistance = 1100.0f;   // player this close: speed off
constexpr float VanishDistance = 650.0f;  // still caught up: fade out
constexpr float DespawnDistance = 18000.0f;
constexpr int32 MaxWeeds = 24;

// Candidate original-data models; the first one present in the MPQs is used.
const TCHAR* const ModelCandidates[] = {
    TEXT("World\\Generic\\PassiveDoodads\\Tumbleweed\\Tumbleweed01.m2"),
    TEXT("World\\Generic\\PassiveDoodads\\Tumbleweeds\\Tumbleweed01.m2"),
    TEXT("World\\Kalimdor\\Barrens\\PassiveDoodads\\Tumbleweed\\BarrensTumbleweed01.m2"),
    TEXT("World\\Kalimdor\\Barrens\\PassiveDoodads\\Tumbleweed\\Tumbleweed01.m2"),
    TEXT("World\\Kalimdor\\Desolace\\PassiveDoodads\\Tumbleweed\\DesolaceTumbleweed01.m2"),
    TEXT("World\\Expansion01\\Doodads\\Generic\\Tumbleweed\\Tumbleweed01.m2"),
};
}

UWowTumbleweedComponent::UWowTumbleweedComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.0f; // smooth rolling every frame
}

void UWowTumbleweedComponent::SetWeather(UWowWeatherComponent* InWeather)
{
    Weather = InWeather;
}

bool UWowTumbleweedComponent::IsTumbleweedZone(uint32 ZoneId)
{
    switch (ZoneId)
    {
    case 3:     // Badlands
    case 14:    // Durotar
    case 17:    // The Barrens
    case 400:   // Thousand Needles
    case 405:   // Desolace
    case 440:   // Tanaris
    case 1377:  // Silithus
    case 3483:  // Hellfire Peninsula
    case 3520:  // Shadowmoon Valley
        return true;
    default:
        return false;
    }
}

UMaterialInterface* UWowTumbleweedComponent::PrepareMaterial()
{
    const FString Name = TEXT("M_WowTumbleweed_v1");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->TwoSided = true;
        auto* Tint = NewObject<UMaterialExpressionVertexColor>(Material);
        auto* Rough = NewObject<UMaterialExpressionConstant>(Material);
        Rough->R = 0.9f;
        Material->GetExpressionCollection().AddExpression(Tint);
        Material->GetExpressionCollection().AddExpression(Rough);
        Material->GetEditorOnlyData()->BaseColor.Connect(0, Tint);
        Material->GetEditorOnlyData()->Roughness.Connect(0, Rough);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    PrepareTwigMaterial(); // r367: the standalone commandlet only calls PrepareMaterial
    return Material ? static_cast<UMaterialInterface*>(Material) : UMaterial::GetDefaultMaterial(MD_Surface);
}

UMaterialInterface* UWowTumbleweedComponent::PrepareTwigMaterial()
{
    // r367 (Stefan: the outer twigs turned see-through with anti-aliasing on). The M2 texture's
    // alpha fades in the lower mips, so the thin outer twigs fell under the 0.333 clip on
    // most pixels; TAA/TSR then averaged the sparse coverage into a transparent halo.
    // Sample one mip sharper and triple the alpha: twigs stay solid, the gaps stay open.
    const FString Name = TEXT("M_WowTumbleweedTwigs_v1");
    static TStrongObjectPtr<UMaterial> Retained;
    if (Retained.IsValid()) return Retained.Get();
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->TwoSided = true;
        Material->BlendMode = BLEND_Masked;
        auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Sampler->ParameterName = FName(TEXT("Layer0Texture"));
        Sampler->SamplerType = SAMPLERTYPE_Color;
        Sampler->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
        Sampler->MipValueMode = TMVM_MipBias;
        Sampler->ConstMipValue = -1;
        auto* Boost = NewObject<UMaterialExpressionConstant>(Material);
        Boost->R = 3.0f;
        auto* Mask = NewObject<UMaterialExpressionMultiply>(Material);
        Mask->A.Connect(4, Sampler);
        Mask->B.Connect(0, Boost);
        auto* Rough = NewObject<UMaterialExpressionConstant>(Material);
        Rough->R = 0.9f;
        for (UMaterialExpression* E : {static_cast<UMaterialExpression*>(Sampler), static_cast<UMaterialExpression*>(Boost),
            static_cast<UMaterialExpression*>(Mask), static_cast<UMaterialExpression*>(Rough)})
            Material->GetExpressionCollection().AddExpression(E);
        Material->GetEditorOnlyData()->BaseColor.Connect(0, Sampler);
        Material->GetEditorOnlyData()->OpacityMask.Connect(0, Mask);
        Material->GetEditorOnlyData()->Roughness.Connect(0, Rough);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (Material) Retained.Reset(Material);
    return Material;
}

static void BuildWeedGeometry(float RadiusCm, int32 Seed, TArray<FVector>& V, TArray<int32>& T, TArray<FVector>& N, TArray<FVector2D>& UV, TArray<FLinearColor>& C)
{
    // A loose ball of curved twigs: each twig follows the sphere surface in a few
    // segments, drawn as two crossed thin quads so it reads from any side.
    FRandomStream R(Seed);
    RadiusCm = FMath::Max(RadiusCm, 30.0f);
    // Twig ends per unit of surface stay roughly constant: a 4 m clump is a dense tangle.
    const float SizeFactor = RadiusCm / 100.0f;
    const int32 TwigCount = FMath::Clamp(FMath::RoundToInt(80.0f * FMath::Pow(SizeFactor, 1.6f)), 60, 520);
    const float Radius = 1.0f; // unit ball, scaled per instance
    auto AddQuad = [&](const FVector& A, const FVector& B, const FVector& Side, const FLinearColor& Col)
    {
        const int32 Base = V.Num();
        V.Add(A - Side); V.Add(A + Side); V.Add(B + Side); V.Add(B - Side);
        const FVector Normal = FVector::CrossProduct(B - A, Side).GetSafeNormal();
        for (int32 I = 0; I < 4; ++I) { N.Add(Normal); C.Add(Col); }
        UV.Add(FVector2D(0, 0)); UV.Add(FVector2D(1, 0)); UV.Add(FVector2D(1, 1)); UV.Add(FVector2D(0, 1));
        T.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    };
    for (int32 Twig = 0; Twig < TwigCount; ++Twig)
    {
        // A third of the twigs fill the inside, so the ball is not a hollow shell.
        FVector P = R.GetUnitVector() * Radius * (Twig % 3 == 0 ? R.FRandRange(0.2f, 0.55f) : R.FRandRange(0.55f, 1.0f));
        FVector Dir = FVector::CrossProduct(P, R.GetUnitVector()).GetSafeNormal();
        const float Shade = R.FRandRange(0.0f, 1.0f);
        const FLinearColor Col = FMath::Lerp(FLinearColor(0.20f, 0.13f, 0.06f), FLinearColor(0.55f, 0.42f, 0.24f), Shade);
        // Half-width in world cm (0.7-1.3 cm, r368: was 0.5-1), whatever the ball size.
        const float Width = R.FRandRange(0.7f, 1.3f) / RadiusCm;
        // Twig length in world terms stays similar: more, shorter steps on big balls.
        const float Step = FMath::Clamp(30.0f / RadiusCm, 0.06f, 0.32f);
        const int32 Segments = R.RandRange(2, 4) + (SizeFactor > 2.0f ? 2 : 0);
        for (int32 S = 0; S < Segments; ++S)
        {
            const FVector Next = (P + Dir * Step * R.FRandRange(0.7f, 1.3f)).GetSafeNormal() * P.Size() * R.FRandRange(0.92f, 1.05f);
            const FVector Along = (Next - P).GetSafeNormal();
            const FVector Side1 = FVector::CrossProduct(Along, P.GetSafeNormal()).GetSafeNormal() * Width;
            const FVector Side2 = FVector::CrossProduct(Along, Side1).GetSafeNormal() * Width;
            AddQuad(P, Next, Side1, Col);
            AddQuad(P, Next, Side2, Col);
            Dir = (Along + R.GetUnitVector() * 0.35f).GetSafeNormal();
            P = Next;
        }
    }
}

void UWowTumbleweedComponent::BuildProceduralWeed(UProceduralMeshComponent* Mesh, float RadiusCm, int32 Seed) const
{
    TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C;
    BuildWeedGeometry(RadiusCm, Seed, V, T, N, UV, C);
    TArray<FProcMeshTangent> Tangents;
    Mesh->CreateMeshSection_LinearColor(0, V, T, N, UV, C, Tangents, false);
    Mesh->SetMaterial(0, PrepareMaterial());
}

UStaticMesh* UWowTumbleweedComponent::GetWeedMesh(float RadiusCm, int32 Variant)
{
    // Size buckets a factor sqrt(2) apart keep the twig density right; three shapes each.
    const int32 Bucket = FMath::Clamp(FMath::RoundToInt(2.0f * FMath::Log2(FMath::Max(RadiusCm, 30.0f) / 30.0f)), 0, 15);
    const int32 Key = Bucket * 8 + (Variant % 3);
    if (const TObjectPtr<UStaticMesh>* Found = WeedMeshes.Find(Key)) if (*Found) return *Found;
    TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C;
    BuildWeedGeometry(30.0f * FMath::Pow(2.0f, Bucket * 0.5f), 7919 * (Variant % 3) + 104729 * Bucket + 17, V, T, N, UV, C);
    FMeshDescription Desc;
    FStaticMeshAttributes Attributes(Desc);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    auto Normals = Attributes.GetVertexInstanceNormals();
    auto Tangents = Attributes.GetVertexInstanceTangents();
    auto Signs = Attributes.GetVertexInstanceBinormalSigns();
    auto UVs = Attributes.GetVertexInstanceUVs();
    auto Colors = Attributes.GetVertexInstanceColors();
    UVs.SetNumChannels(1);
    const FPolygonGroupID Group = Desc.CreatePolygonGroup();
    Desc.ReserveNewVertices(V.Num()); Desc.ReserveNewVertexInstances(V.Num()); Desc.ReserveNewTriangles(T.Num() / 3);
    TArray<FVertexInstanceID> Ids; Ids.SetNum(V.Num());
    for (int32 I = 0; I < V.Num(); ++I)
    {
        const FVertexID Vert = Desc.CreateVertex();
        Positions[Vert] = FVector3f(V[I]);
        const FVertexInstanceID Inst = Desc.CreateVertexInstance(Vert);
        Ids[I] = Inst;
        const FVector3f Normal(N.IsValidIndex(I) ? N[I] : FVector::UpVector);
        Normals[Inst] = Normal;
        Tangents[Inst] = FVector3f(FVector::CrossProduct(FVector(Normal), FMath::Abs(Normal.Z) < .9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal());
        Signs[Inst] = 1.0f;
        UVs.Set(Inst, 0, FVector2f(UV.IsValidIndex(I) ? UV[I] : FVector2D::ZeroVector));
        const FLinearColor Col = C.IsValidIndex(I) ? C[I] : FLinearColor::White;
        Colors[Inst] = FVector4f(Col.R, Col.G, Col.B, Col.A);
    }
    for (int32 I = 0; I + 2 < T.Num(); I += 3)
    {
        const FVertexInstanceID Tri[3] = {Ids[T[I]], Ids[T[I + 1]], Ids[T[I + 2]]};
        Desc.CreateTriangle(Group, MakeArrayView(Tri, 3));
    }
    UStaticMesh* Mesh = NewObject<UStaticMesh>(this);
    FStaticMaterial WeedMaterial(PrepareMaterial());
    WeedMaterial.UVChannelData.bInitialized = true;
    Mesh->GetStaticMaterials().Add(MoveTemp(WeedMaterial));
    WowPrepareRuntimeMeshMaterials(Mesh, Desc);
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bBuildSimpleCollision = false;
    Params.bFastBuild = true;
    Params.bCommitMeshDescription = false;
    TArray<const FMeshDescription*> Descs;
    Descs.Add(&Desc);
    Mesh->BuildFromMeshDescriptions(Descs, Params);
    Mesh->NeverStream = true;
    WeedMeshes.Add(Key, Mesh);
    return Mesh;
}

UPrimitiveComponent* UWowTumbleweedComponent::CreateVisual(float RadiusCm)
{
    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;
    if (!bResolvedModel)
    {
        bResolvedModel = true;
        AWowWorldManager* World = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass()));
        FMpqManager* Mpq = World ? World->GetMpqManager() : nullptr;
        FWowAssetCache* Cache = World ? World->GetAssetCache() : nullptr;
        TArray<FString> Candidates;
        if (!CVarTumbleweedModel.GetValueOnGameThread().IsEmpty()) Candidates.Add(CVarTumbleweedModel.GetValueOnGameThread());
        for (const TCHAR* Path : ModelCandidates) Candidates.Add(Path);
        for (const FString& Path : Candidates)
        {
            if (Mpq && Cache && Mpq->FileExists(Path))
            {
                ModelMesh = FWowDoodadManager::GetOrCreateStaticMesh(Path, Mpq, Cache);
                if (ModelMesh)
                {
                    UE_LOG(LogWowTumbleweed, Log, TEXT("Tumbleweed model: %s"), *Path);
                    break;
                }
            }
        }
        if (!ModelMesh) UE_LOG(LogWowTumbleweed, Log, TEXT("No tumbleweed M2 in the game data; using the built-in twig ball"));
    }
    UPrimitiveComponent* Visual = nullptr;
    if (ModelMesh)
    {
        auto* Static = NewObject<UStaticMeshComponent>(Owner);
        Static->SetStaticMesh(ModelMesh);
        // r367: swap masked slots to the coverage-boosted twig material (same texture).
        if (TwigMaterials.Num() != ModelMesh->GetStaticMaterials().Num())
        {
            TwigMaterials.Reset();
            UMaterialInterface* TwigBase = PrepareTwigMaterial();
            for (int32 Slot = 0; Slot < ModelMesh->GetStaticMaterials().Num(); ++Slot)
            {
                UMaterialInterface* Original = ModelMesh->GetMaterial(Slot);
                UTexture* Texture = nullptr;
                if (TwigBase && Original && Original->GetBlendMode() == BLEND_Masked &&
                    Original->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Layer0Texture")), Texture) && Texture)
                {
                    UMaterialInstanceDynamic* Twig = UMaterialInstanceDynamic::Create(TwigBase, this);
                    Twig->SetTextureParameterValue(TEXT("Layer0Texture"), Texture);
                    TwigMaterials.Add(Twig);
                }
                else TwigMaterials.Add(Original);
            }
        }
        for (int32 Slot = 0; Slot < TwigMaterials.Num(); ++Slot) Static->SetMaterial(Slot, TwigMaterials[Slot]);
        Visual = Static;
    }
    else
    {
        // r368: shared static twig ball (writes motion vectors, so TAA/TSR keep the twigs solid).
        if (UStaticMesh* WeedMesh = GetWeedMesh(RadiusCm, Random.RandRange(0, 2)))
        {
            auto* Static = NewObject<UStaticMeshComponent>(Owner);
            Static->SetMobility(EComponentMobility::Movable);
            Static->SetStaticMesh(WeedMesh);
            Static->SetMaterial(0, PrepareMaterial());
            Visual = Static;
        }
        else
        {
            auto* Proc = NewObject<UProceduralMeshComponent>(Owner);
            BuildProceduralWeed(Proc, RadiusCm, int32(Random.GetCurrentSeed() ^ Pool.Num() * 7919));
            Visual = Proc;
        }
    }
    // Cosmetic only: never collides, never blocks the camera, never affects navigation.
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetCollisionResponseToAllChannels(ECR_Ignore);
    Visual->SetGenerateOverlapEvents(false);
    Visual->SetCanEverAffectNavigation(false);
    Visual->SetAbsolute(true, true, true);
    Visual->SetCastShadow(false); // enabled near the camera in Tick
    Visual->RegisterComponent();
    Pool.Add(Visual);
    return Visual;
}

bool UWowTumbleweedComponent::ProbeGround(const FVector& Around, float& OutZ) const
{
    UWorld* World = GetWorld();
    if (!World) return false;
    FHitResult Hit;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    FCollisionQueryParams Params(TEXT("WowTumbleweedGround"), false);
    if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0)) Params.AddIgnoredActor(Pawn);
    const FVector Start = Around + FVector(0, 0, 3000);
    const FVector End = Around - FVector(0, 0, 6000);
    if (!World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params) || !Hit.bBlockingHit) return false;
    if (Hit.ImpactNormal.Z < 0.6f) return false;                       // cliffs, walls, roofs edges
    if (Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("WowWaterVolume"))) return false;
    OutZ = Hit.ImpactPoint.Z;
    return true;
}

namespace
{
bool IsBuildingComponent(const UPrimitiveComponent* Component)
{
    // WMO groups and their authored collision are named Wmo*; doodads/trees are not.
    return Component && Component->GetName().StartsWith(TEXT("Wmo"));
}
}

bool UWowTumbleweedComponent::IsForbiddenSpot(const FVector& Ground) const
{
    UWorld* World = GetWorld();
    if (!World) return true;
    FCollisionQueryParams Params(TEXT("WowTumbleweedSpot"), false);
    if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0)) Params.AddIgnoredActor(Pawn);
    // Water: the liquid overlap volumes (WorldDynamic, tagged WowWaterVolume).
    TArray<FOverlapResult> Waters;
    World->OverlapMultiByObjectType(Waters, Ground + FVector(0, 0, 25), FQuat::Identity,
        FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(20.0f), Params);
    for (const FOverlapResult& Water : Waters)
        if (Water.GetComponent() && Water.GetComponent()->ComponentHasTag(TEXT("WowWaterVolume"))) return true;
    // Indoors / under a building: any WMO overhead.
    FHitResult Roof;
    FCollisionObjectQueryParams Static;
    Static.AddObjectTypesToQuery(ECC_WorldStatic);
    return World->LineTraceSingleByObjectType(Roof, Ground + FVector(0, 0, 60), Ground + FVector(0, 0, 4000), Static, Params)
        && IsBuildingComponent(Roof.GetComponent());
}

bool UWowTumbleweedComponent::HitsBuilding(const FVector& From, const FVector& To, float Height) const
{
    UWorld* World = GetWorld();
    if (!World) return false;
    FCollisionQueryParams Params(TEXT("WowTumbleweedWall"), false);
    if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0)) Params.AddIgnoredActor(Pawn);
    FCollisionObjectQueryParams Static;
    Static.AddObjectTypesToQuery(ECC_WorldStatic);
    FHitResult Wall;
    const FVector Up(0, 0, Height);
    return World->LineTraceSingleByObjectType(Wall, From + Up, To + Up, Static, Params)
        && IsBuildingComponent(Wall.GetComponent());
}

bool UWowTumbleweedComponent::Spawn(const FVector& Camera, const FVector& ViewForward, const FVector2D& Wind)
{
    // Ahead of the camera, inside the view, rolling away (wind + a push away from the player).
    // Mostly ahead, but also out to the sides so they are seen all around the player.
    const float Yaw = Random.FRandRange(-95.0f, 95.0f);
    const FVector Forward = FVector(ViewForward.X, ViewForward.Y, 0).GetSafeNormal().RotateAngleAxis(Yaw, FVector::UpVector);
    if (Forward.IsNearlyZero()) return false;
    FVector Position = Camera + Forward * Random.FRandRange(SpawnMinDistance, SpawnMaxDistance);
    float GroundZ = 0;
    if (!ProbeGround(Position, GroundZ)) return false;
    Position.Z = GroundZ;
    if (IsForbiddenSpot(Position)) return false; // never in water or inside buildings
    // Real tumbleweeds grow to ~2 m; rare tangled clumps get car-sized. The twig ball fills
    // ~75% of its radius, so Scale 2.8-4.2 reads as ~1.4-2.1 m; one in ten is a 3-4.5 m clump.
    const float NewScale = Random.FRand() < 0.1f ? Random.FRandRange(6.0f, 9.0f) : Random.FRandRange(2.8f, 4.2f);
    FWeed& Weed = Weeds.AddDefaulted_GetRef();
    Weed.Visual = CreateVisual(34.0f * NewScale);
    if (!Weed.Visual.IsValid()) { Weeds.Pop(); return false; }
    Weed.Position = Position;
    Weed.GroundZ = GroundZ;
    Weed.Scale = NewScale;
    Weed.Radius = 34.0f * Weed.Scale;
    const FVector WindDir(Wind.X, Wind.Y, 0);
    Weed.Direction = (WindDir.GetSafeNormal() + Forward * 0.9f).GetSafeNormal2D();
    if (Weed.Direction.IsNearlyZero()) Weed.Direction = Forward;
    Weed.Spin = FQuat(Random.GetUnitVector(), Random.FRandRange(0, 2 * PI));
    Weed.BouncePhase = Random.FRandRange(0, 2 * PI);
    return true;
}

void UWowTumbleweedComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime, TickType, Function);
    const int32 Mode = CVarTumbleweeds.GetValueOnGameThread();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    UWowWeatherComponent* W = Weather.Get();
    if (!PC || !W || DeltaTime <= 0.0f) return;
    FVector Camera; FRotator Rotation;
    PC->GetPlayerViewPoint(Camera, Rotation);
    const APawn* Pawn = PC->GetPawn();
    const FVector Player = Pawn ? Pawn->GetActorLocation() : Camera;

    const FWowWeatherSample& Sample = W->GetWeather();
    const bool bSandstorm = Sample.Kind == EWowWeatherKind::Dust && Sample.Intensity > 0.05f;
    const bool bWet = Sample.Kind == EWowWeatherKind::Rain || Sample.Kind == EWowWeatherKind::Storm ||
        Sample.Kind == EWowWeatherKind::Snow || Sample.Kind == EWowWeatherKind::BlackRain || Sample.Kind == EWowWeatherKind::BlackSnow;
    const bool bOutdoors = W->GetCameraExposure() > 0.2f; // not inside buildings/caves
    const bool bZone = Mode == 2 || IsTumbleweedZone(W->GetZone());
    const float WindAngle = FMath::DegreesToRadians(Sample.Direction + 180.0f);
    const FVector2D Wind(FMath::Cos(WindAngle), FMath::Sin(WindAngle));
    const float WindSpeed = FMath::Clamp(Sample.Wind, 0.0f, 40.0f) + (bSandstorm ? 10.0f + 18.0f * Sample.Intensity : 0.0f);

    // Population follows the wind: about six on a calm day, up to 24 in a sandstorm,
    // spread from near the player out to ~140 m.
    const int32 Target = (Mode > 0 && bZone && !bWet)
        ? FMath::Clamp(6 + FMath::FloorToInt(WindSpeed / 3.0f) + (bSandstorm ? 6 : 0), 6, MaxWeeds) : 0;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Weeds.Num() < Target && bOutdoors && Now >= NextSpawn)
    {
        Spawn(Camera, Rotation.Vector(), Wind);
        NextSpawn = Now + Random.FRandRange(1.2f, 3.5f) / (bSandstorm ? 2.5f : 1.0f);
    }

    for (int32 I = Weeds.Num() - 1; I >= 0; --I)
    {
        FWeed& Weed = Weeds[I];
        UPrimitiveComponent* Visual = Weed.Visual.Get();
        if (!Visual) { Weeds.RemoveAtSwap(I); continue; }
        Weed.Age += DeltaTime;

        // Speed: a breeze rolls them at a walk, strong wind at a run, sandstorms at mount speed.
        float TargetSpeed = 140.0f + WindSpeed * 32.0f;
        const float PlayerDistance = FVector::Dist2D(Player, Weed.Position);
        if (PlayerDistance < FleeDistance)
        {
            // Never let the player reach one: race away from them.
            const FVector Away = (Weed.Position - Player).GetSafeNormal2D();
            Weed.Direction = (Weed.Direction + Away * DeltaTime * 3.0f).GetSafeNormal2D();
            TargetSpeed = FMath::Max(TargetSpeed * 2.4f, 1300.0f);
        }
        if (PlayerDistance < VanishDistance) Weed.bLeaving = true;
        // Blocked by water or a building: stop rolling on the spot while it fades.
        Weed.Speed = Weed.bBlocked ? FMath::FInterpTo(Weed.Speed, 0.0f, DeltaTime, 6.0f)
            : FMath::FInterpTo(Weed.Speed, FMath::Min(TargetSpeed, 2600.0f), DeltaTime, 1.6f);

        FVector Next = Weed.Position + Weed.Direction * Weed.Speed * DeltaTime;
        if (Now >= Weed.NextGroundProbe)
        {
            Weed.NextGroundProbe = Now + 0.08;
            float GroundZ = 0;
            if (ProbeGround(Next, GroundZ) && FMath::Abs(GroundZ - Weed.GroundZ) < 400.0f) Weed.GroundZ = GroundZ;
            else Weed.bLeaving = true; // edge of a cliff, water or no ground: drift off and fade
            // Look a body length ahead: it must never roll into water or into a house.
            const FVector Ahead = Next + Weed.Direction * (Weed.Radius + 0.35f * Weed.Speed);
            if (!Weed.bBlocked && (IsForbiddenSpot(FVector(Ahead.X, Ahead.Y, Weed.GroundZ))
                || HitsBuilding(Weed.Position, Ahead, Weed.Radius)))
            {
                Weed.bBlocked = true;
                Weed.bLeaving = true;
            }
        }
        if (Weed.bBlocked) Next = Weed.Position + Weed.Direction * Weed.Speed * DeltaTime * 0.25f;
        Weed.Position = FVector(Next.X, Next.Y, FMath::FInterpTo(Weed.Position.Z, Weed.GroundZ, DeltaTime, 10.0f));

        // Hops get bigger with speed; rolling spin matches distance travelled.
        Weed.BouncePhase += DeltaTime * (2.2f + Weed.Speed / 260.0f);
        const float Hop = FMath::Abs(FMath::Sin(Weed.BouncePhase)) * FMath::Clamp(Weed.Speed / 9.0f, 4.0f, 85.0f);
        const FVector Axis = FVector::CrossProduct(FVector::UpVector, Weed.Direction).GetSafeNormal();
        Weed.Spin = FQuat(Axis, Weed.Speed * DeltaTime / FMath::Max(Weed.Radius, 1.0f)) * Weed.Spin;

        const bool bTooFar = FVector::Dist2D(Camera, Weed.Position) > DespawnDistance || Weed.Age > 75.0f;
        if (bTooFar || Target == 0) Weed.bLeaving = true;
        Weed.Fade = FMath::FInterpConstantTo(Weed.Fade, Weed.bLeaving ? 0.0f : 1.0f, DeltaTime, Weed.bBlocked ? 3.5f : Weed.bLeaving ? 2.5f : 1.0f);
        if (Weed.bLeaving && Weed.Fade <= 0.0f)
        {
            Visual->DestroyComponent();
            Pool.Remove(Visual);
            Weeds.RemoveAtSwap(I);
            continue;
        }
        const float VisualScale = Weed.Radius * FMath::SmoothStep(0.0f, 1.0f, Weed.Fade);
        Visual->SetWorldLocationAndRotation(Weed.Position + FVector(0, 0, Weed.Radius * 0.85f + Hop), Weed.Spin);
        Visual->SetWorldScale3D(FVector(ModelMesh ? Weed.Scale * FMath::SmoothStep(0.0f, 1.0f, Weed.Fade) : VisualScale));
        // Moving shadow casters invalidate the shadow maps every frame: only weeds near the
        // camera cast one (their shadow is the only one you can read at a distance anyway).
        const bool bWantShadow = FVector::DistSquared2D(Camera, Weed.Position) < FMath::Square(3500.0f);
        if (Visual->CastShadow != bWantShadow) Visual->SetCastShadow(bWantShadow);
    }
}

void UWowTumbleweedComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    for (UPrimitiveComponent* Visual : Pool) if (Visual) Visual->DestroyComponent();
    Pool.Reset();
    Weeds.Reset();
    Super::EndPlay(Reason);
}
