#include "WowLightningShieldComponent.h"
#include "HAL/IConsoleManager.h"
#include "WowCookedMaterial.h"
#include "WowRuntimeMeshMaterials.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"

namespace
{
// Plasma orb: white-hot core, swirling shell and 4 jagged bolts that re-strike ~12x per second.
const TCHAR* OrbShader = TEXT(R"(
float2 p = UV * 2 - 1;
float r = length(p);
float t = TimeSec + Seed * 17.31;
float ang = atan2(p.y, p.x);
float core = exp(-r * r * 22);
float shell = exp(-pow((r - 0.30) * 10, 2)) * (0.55 + 0.45 * sin(ang * 5 + t * 7 + sin(t * 3.1) * 2));
float bolts = 0;
[unroll] for (int i = 0; i < 4; ++i)
{
    float strike = floor(t * 12 + i * 3.7);
    float baseA = frac(sin(strike * 12.9898 + i * 78.233 + Seed * 3.7) * 43758.5453) * 6.2831853;
    float da = ang - baseA;
    da = atan2(sin(da), cos(da));
    float along = cos(da) * r;
    float perp = sin(da) * r;
    float s = along * 9;
    float k = floor(s);
    float f = frac(s);
    float n0 = frac(sin((k + strike * 7) * 91.7 + i) * 4375.5) - 0.5;
    float n1 = frac(sin((k + 1 + strike * 7) * 91.7 + i) * 4375.5) - 0.5;
    float off = lerp(n0, n1, f) * 0.20 * along;
    float w = 0.018 + 0.02 * (1 - along);
    bolts += exp(-pow((perp - off) / w, 2)) * smoothstep(0.08, 0.2, along) * (1 - smoothstep(0.62, 0.92, along));
}
float glow = exp(-r * r * 5) * 0.45;
float3 c = Color.rgb;
float3 rgb = float3(1, 1, 1) * core * 3.5 + c * shell * 1.8 + lerp(c, float3(1, 1, 1), 0.65) * bolts * 3.0 + c * glow;
return rgb * Intensity * (1 - smoothstep(0.92, 1.0, r));
)");

// Lightning arc along U (0 = orb, 1 = body), jagged polyline with fine jitter, re-striking.
const TCHAR* ArcShader = TEXT(R"(
float x = UV.x;
float y = UV.y * 2 - 1;
float strike = floor(TimeSec * 14 + Seed * 13.7);
float env = sin(saturate(x) * 3.14159265);
float s = x * 9;
float k = floor(s);
float f = frac(s);
float n0 = frac(sin((k + strike * 5) * 127.1 + Seed) * 43758.5453) - 0.5;
float n1 = frac(sin((k + 1 + strike * 5) * 127.1 + Seed) * 43758.5453) - 0.5;
float off = lerp(n0, n1, f) * 1.5 * env;
float s2 = x * 33;
float k2 = floor(s2);
float f2 = frac(s2);
float m0 = frac(sin((k2 + strike * 3) * 311.7 + Seed) * 43758.5453) - 0.5;
float m1 = frac(sin((k2 + 1 + strike * 3) * 311.7 + Seed) * 43758.5453) - 0.5;
off += lerp(m0, m1, f2) * 0.34 * env;
float d = abs(y - off);
float core = exp(-pow(d / 0.06, 2));
float glow = exp(-pow(d / 0.34, 2)) * 0.55;
float on = step(0.38, frac(sin(strike * 7.13 + Seed * 1.7) * 43758.5453));
// WoW lightning is blue/violet all the way through: only a thin hint of white in the core.
float3 hot = lerp(Color.rgb, float3(1, 1, 1), 0.22);
return (hot * core * 2.6 + Color.rgb * glow * 1.8) * on * Intensity;
)");

// Expanding ground shock ring (Earth Shock etc.). "Seed" carries the 0..1 progress.
const TCHAR* RingShader = TEXT(R"(
float2 p = UV * 2 - 1;
float r = length(p);
float prog = saturate(Seed);
float w = 0.05 + 0.12 * prog;
float ang = atan2(p.y, p.x);
float rough = 0.7 + 0.3 * sin(ang * 13 + prog * 9 + TimeSec) * sin(ang * 7 - prog * 5);
float ring = exp(-pow((r - prog * 0.92) / w, 2)) * rough;
float dust = (1 - smoothstep(0, max(prog * 0.92, 0.01), r)) * 0.3 * (1 - prog);
float fade = pow(1 - prog, 1.3);
return Color.rgb * (ring * 2.2 + dust) * fade * Intensity * (1 - smoothstep(0.95, 1.0, r));
)");

// Channelled beam along U (0 = caster, 1 = target): white-hot core or a writhing strand,
// pulses flowing in the drain direction. Length (cm) keeps the pattern density constant.
const TCHAR* BeamShader = TEXT(R"(
float x = UV.x;
float y = UV.y * 2 - 1;
float L = max(Length, 1);
float t = TimeSec;
float ph = Seed * 2.17;
float env = smoothstep(0.0, 0.05, x) * (1 - smoothstep(0.95, 1.0, x));
float w1 = sin(x * L / 70 - t * 5.0 * Flow + ph);
float w2 = sin(x * L / 23 + t * 9.0 + ph * 1.7);
float off = (w1 * 0.42 + w2 * 0.14) * Writhe * env;
float d = abs(y - off);
float width = 0.11 + 0.04 * sin(x * L / 40 + t * 3 + ph);
float core = exp(-pow(d / (width * 0.45), 2));
float glow = exp(-pow(d / (width * 2.6), 2)) * 0.5;
float p = frac(x * L / 90 - t * 1.6 * Flow + ph * 0.3);
float pulse = pow(saturate(1 - abs(p - 0.5) * 2), 6) * 1.6;
float grain = 0.8 + 0.2 * sin(x * L / 9 - t * 20 * Flow + ph * 5);
float3 hot = lerp(Color.rgb, float3(1, 1, 1), 0.35);
float3 rgb = hot * core * (1.4 + pulse) * grain + Color.rgb * glow * (1 + pulse * 0.6);
return rgb * env * Intensity * (1 - smoothstep(0.9, 1.0, abs(y)));
)");

enum class ELightningMaterial : uint8 { Orb, Arc, Ring, Beam };

UMaterialInterface* LightningMaterial(ELightningMaterial Kind)
{
	const bool bArc = Kind == ELightningMaterial::Arc;
	const bool bBeam = Kind == ELightningMaterial::Beam;
	const FString Name = bBeam ? TEXT("M_WowChannelBeam") : Kind == ELightningMaterial::Ring ? TEXT("M_WowShockRing") : bArc ? TEXT("M_WowLightningArc_v2") : TEXT("M_WowLightningOrb");
	UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
	if (!Material)
	{
		Material = NewObject<UMaterial>(GetTransientPackage());
		Material->SetShadingModel(MSM_Unlit);
		Material->BlendMode = BLEND_Additive;
		Material->TwoSided = true;
		Material->bDisableDepthTest = false;
		auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
		auto* Time = NewObject<UMaterialExpressionTime>(Material);
		auto* Seed = NewObject<UMaterialExpressionScalarParameter>(Material);
		Seed->ParameterName = TEXT("Seed");
		Seed->DefaultValue = 0.f;
		auto* Intensity = NewObject<UMaterialExpressionScalarParameter>(Material);
		Intensity->ParameterName = TEXT("Intensity");
		Intensity->DefaultValue = 1.f;
		auto* Color = NewObject<UMaterialExpressionVectorParameter>(Material);
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor(0.45f, 0.55f, 1.0f);
		auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
		Shader->OutputType = CMOT_Float3;
		const auto Input = [Shader](const TCHAR* InputName, UMaterialExpression* Expression)
		{
			FCustomInput Value;
			Value.InputName = InputName;
			Value.Input.Connect(0, Expression);
			Shader->Inputs.Add(Value);
		};
		Shader->Inputs.Reset();
		Input(TEXT("UV"), UV);
		Input(TEXT("TimeSec"), Time);
		Input(TEXT("Seed"), Seed);
		Input(TEXT("Intensity"), Intensity);
		Input(TEXT("Color"), Color);
		Shader->Code = bBeam ? BeamShader : Kind == ELightningMaterial::Ring ? RingShader : bArc ? ArcShader : OrbShader;
		TArray<UMaterialExpression*> All{UV, Time, Seed, Intensity, Color, Shader};
		if (bBeam)
			for (const TCHAR* Extra : {TEXT("Length"), TEXT("Flow"), TEXT("Writhe")})
			{
				auto* Param = NewObject<UMaterialExpressionScalarParameter>(Material);
				Param->ParameterName = Extra;
				Param->DefaultValue = 1.f;
				Input(Extra, Param);
				All.Add(Param);
			}
		for (UMaterialExpression* Expression : All)
			Material->GetExpressionCollection().AddExpression(Expression);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Shader);
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material = WowCookedMaterial::Save(Material, Name);
	}
#endif
	return Material ? static_cast<UMaterialInterface*>(Material) : UMaterial::GetDefaultMaterial(MD_Surface);
}

/** Quad in the local YZ plane facing +X. Orb: centred, size 1. Arc: Y from 0 to 1, Z -0.5..0.5.
 *  Ground: centred in the XY plane facing +Z. */
UStaticMesh* BuildQuad(UObject* Outer, bool bArc, bool bGround = false)
{
	FMeshDescription Desc;
	FStaticMeshAttributes Attributes(Desc);
	Attributes.Register();
	const FPolygonGroupID Group = Desc.CreatePolygonGroup();
	const float Y0 = bArc ? 0.f : -0.5f, Y1 = bArc ? 1.f : 0.5f;
	FVector3f Points[4] = {{0, Y0, -0.5f}, {0, Y1, -0.5f}, {0, Y1, 0.5f}, {0, Y0, 0.5f}};
	if (bGround)
	{
		Points[0] = {-0.5f, -0.5f, 0}; Points[1] = {-0.5f, 0.5f, 0}; Points[2] = {0.5f, 0.5f, 0}; Points[3] = {0.5f, -0.5f, 0};
	}
	const FVector2f UVs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
	FVertexInstanceID Instances[4];
	for (int32 I = 0; I < 4; ++I)
	{
		const FVertexID Vertex = Desc.CreateVertex();
		Attributes.GetVertexPositions()[Vertex] = Points[I];
		Instances[I] = Desc.CreateVertexInstance(Vertex);
		Attributes.GetVertexInstanceNormals()[Instances[I]] = bGround ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0);
		Attributes.GetVertexInstanceTangents()[Instances[I]] = FVector3f(0, 1, 0);
		Attributes.GetVertexInstanceBinormalSigns()[Instances[I]] = 1;
		Attributes.GetVertexInstanceUVs().Set(Instances[I], 0, UVs[I]);
		Attributes.GetVertexInstanceColors()[Instances[I]] = FVector4f(1, 1, 1, 1);
	}
	Desc.CreatePolygon(Group, {Instances[0], Instances[1], Instances[2]});
	Desc.CreatePolygon(Group, {Instances[0], Instances[2], Instances[3]});
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

float Hash01(float Value)
{
	return FMath::Frac(FMath::Sin(Value * 12.9898f) * 43758.5453f);
}
}

UWowLightningShieldComponent::UWowLightningShieldComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UWowLightningShieldComponent::PrepareStandaloneMaterials()
{
	LightningMaterial(ELightningMaterial::Orb);
	LightningMaterial(ELightningMaterial::Arc);
	LightningMaterial(ELightningMaterial::Ring);
	LightningMaterial(ELightningMaterial::Beam);
}

void UWowLightningShieldComponent::Setup(float InOwnerHeight, float InOwnerRadius, const FLinearColor& InColor)
{
	OwnerHeight = FMath::Max(60.f, InOwnerHeight);
	OwnerRadius = FMath::Clamp(InOwnerRadius, 20.f, 200.f);
	Color = InColor;
	for (UMaterialInstanceDynamic* Material : OrbMaterials) if (Material) Material->SetVectorParameterValue(TEXT("Color"), Color);
	for (UMaterialInstanceDynamic* Material : ArcMaterials) if (Material) Material->SetVectorParameterValue(TEXT("Color"), Color);
	if (Light) Light->SetLightColor(FMath::Lerp(Color, FLinearColor::White, 0.3f));
	RebuildOrbs();
}

void UWowLightningShieldComponent::SetCharges(int32 InCharges)
{
	const int32 NewCharges = FMath::Clamp(InCharges > 0 ? InCharges : 3, 1, 9);
	if (NewCharges == Charges) return;
	Charges = NewCharges;
	RebuildOrbs();
}

void UWowLightningShieldComponent::OnRegister()
{
	Super::OnRegister();
	BuildParts();
}

void UWowLightningShieldComponent::BuildParts()
{
	AActor* Owner = GetOwner();
	if (bBuilt || !Owner || !GetWorld() || !GetWorld()->IsGameWorld()) return;
	bBuilt = true;
	OrbMesh = BuildQuad(this, false);
	ArcMesh = BuildQuad(this, true);
	Light = NewObject<UPointLightComponent>(Owner);
	Light->SetupAttachment(this);
	Light->SetRelativeLocation(FVector(0, 0, 0));
	Light->SetLightColor(FMath::Lerp(Color, FLinearColor::White, 0.3f));
	Light->SetIntensity(1800.f);
	Light->SetAttenuationRadius(320.f);
	Light->SetCastShadows(false);
	Owner->AddInstanceComponent(Light);
	Light->RegisterComponent();
	RebuildOrbs();
}

void UWowLightningShieldComponent::RebuildOrbs()
{
	AActor* Owner = GetOwner();
	if (!bBuilt || !Owner) return;
	while (Orbs.Num() > Charges)
	{
		if (UStaticMeshComponent* Orb = Orbs.Pop()) Orb->DestroyComponent();
		OrbMaterials.Pop();
		if (UStaticMeshComponent* Arc = Arcs.Pop()) Arc->DestroyComponent();
		ArcMaterials.Pop();
	}
	UMaterialInterface* OrbBase = LightningMaterial(ELightningMaterial::Orb);
	UMaterialInterface* ArcBase = LightningMaterial(ELightningMaterial::Arc);
	while (Orbs.Num() < Charges)
	{
		const int32 Index = Orbs.Num();
		const auto MakePart = [&](UStaticMesh* Mesh, UMaterialInterface* Base, TArray<TObjectPtr<UMaterialInstanceDynamic>>& Materials)
		{
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
			Part->SetupAttachment(this);
			Part->SetStaticMesh(Mesh);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCastShadow(false);
			Part->bAffectDistanceFieldLighting = false;
			Part->SetUsingAbsoluteRotation(true);
			Part->SetUsingAbsoluteScale(true);
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Part);
			Material->SetScalarParameterValue(TEXT("Seed"), Index * 3.17f + FMath::FRand() * 10.f);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Part->SetMaterial(0, Material);
			Owner->AddInstanceComponent(Part);
			Part->RegisterComponent();
			Materials.Add(Material);
			return Part;
		};
		Orbs.Add(MakePart(OrbMesh, OrbBase, OrbMaterials));
		Arcs.Add(MakePart(ArcMesh, ArcBase, ArcMaterials));
	}
}

void UWowLightningShieldComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bBuilt) BuildParts();
	Age += DeltaTime;

	FVector Camera = GetComponentLocation() + FVector(-500, 0, 100);
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
		Camera = CameraManager->GetCameraLocation();

	const FVector Center = GetComponentLocation();
	const float OrbitRadius = OwnerRadius * 1.25f + 18.f;
	const float OrbSize = FMath::Clamp(OwnerHeight * 0.20f, 26.f, 70.f);
	const float Spin = Age * 1.6f; // radians per second, like the original orbit speed
	for (int32 I = 0; I < Orbs.Num(); ++I)
	{
		UStaticMeshComponent* Orb = Orbs[I];
		UStaticMeshComponent* Arc = Arcs.IsValidIndex(I) ? Arcs[I].Get() : nullptr;
		if (!Orb) continue;
		const float Angle = Spin + I * (2.f * PI / FMath::Max(1, Orbs.Num()));
		const float Bob = FMath::Sin(Age * 2.3f + I * 1.9f) * OwnerHeight * 0.05f;
		const FVector OrbPos = Center + FVector(FMath::Cos(Angle) * OrbitRadius, FMath::Sin(Angle) * OrbitRadius, Bob);
		const FVector ToCamera = (Camera - OrbPos).GetSafeNormal();
		Orb->SetWorldLocationAndRotation(OrbPos, ToCamera.Rotation());
		const float Pulse = 1.f + 0.12f * FMath::Sin(Age * 17.f + I * 2.1f);
		Orb->SetWorldScale3D(FVector(1.f, OrbSize * Pulse, OrbSize * Pulse));
		if (OrbMaterials.IsValidIndex(I) && OrbMaterials[I])
			OrbMaterials[I]->SetScalarParameterValue(TEXT("Intensity"), 1.6f + 0.6f * Hash01(FMath::FloorToFloat(Age * 20.f) + I));

		if (Arc)
		{
			// Arc from the orb to a random point on the body, re-aimed every strike.
			const float Strike = FMath::FloorToFloat(Age * 14.f + I * 1.37f);
			const FVector BodyTarget = Center + FVector((Hash01(Strike + I) - 0.5f) * OwnerRadius * 0.6f,
				(Hash01(Strike * 1.7f + I) - 0.5f) * OwnerRadius * 0.6f, (Hash01(Strike * 2.3f + I) - 0.5f) * OwnerHeight * 0.4f);
			const FVector Span = BodyTarget - OrbPos;
			const float Length = Span.Size();
			if (Length > 1.f)
			{
				const FVector Dir = Span / Length;
				FVector Facing = (Camera - (OrbPos + BodyTarget) * 0.5f).GetSafeNormal();
				Facing = (Facing - Dir * FVector::DotProduct(Facing, Dir)).GetSafeNormal();
				if (Facing.IsNearlyZero()) Facing = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
				Arc->SetWorldLocationAndRotation(OrbPos, FRotationMatrix::MakeFromXY(Facing, Dir).Rotator());
				Arc->SetWorldScale3D(FVector(1.f, Length, OrbSize * 0.45f));
			}
			if (ArcMaterials.IsValidIndex(I) && ArcMaterials[I])
				ArcMaterials[I]->SetScalarParameterValue(TEXT("Intensity"), 1.4f);
		}
	}

	if (Light)
	{
		// Electrical flicker: random brightness steps 25 times a second.
		const float Step = FMath::FloorToFloat(Age * 25.f);
		LightFlicker = FMath::Lerp(LightFlicker, 0.55f + 0.9f * Hash01(Step), 0.6f);
		Light->SetIntensity(1500.f * LightFlicker * (0.6f + 0.4f * Orbs.Num() / 3.f));
	}
}

// ---------------------------------------------------------------------------
// Lightning Bolt

UWowLightningBoltComponent::UWowLightningBoltComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UWowLightningBoltComponent::Fire(AActor* InSource, const FVector& InSourceOffset, AActor* InTarget,
	const FVector& InTargetOffset, const FLinearColor& InColor, float InDuration)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;
	Source = InSource; Target = InTarget;
	SourceOffset = InSourceOffset; TargetOffset = InTargetOffset;
	Color = InColor; Age = 0.f;
	// A single bolt that flies from the hand to the target (like the original
	// missile), then flickers briefly on impact; not a standing beam.
	const FVector From = InSource ? InSource->GetActorTransform().TransformPosition(InSourceOffset) : FVector::ZeroVector;
	const FVector To = InTarget ? InTarget->GetActorTransform().TransformPosition(InTargetOffset) : From;
	TravelTime = FMath::Clamp(static_cast<float>(FVector::Dist(From, To)) / 3200.f, 0.10f, 0.6f);
	Duration = TravelTime + FMath::Max(0.1f, InDuration) * 0.45f;
	UStaticMesh* ArcMesh = BuildQuad(this, true);
	UMaterialInterface* ArcBase = LightningMaterial(ELightningMaterial::Arc);
	// One thick core, two thinner side bolts with their own strike pattern.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UStaticMeshComponent* Arc = NewObject<UStaticMeshComponent>(Owner);
		Arc->SetupAttachment(this);
		Arc->SetStaticMesh(ArcMesh);
		Arc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Arc->SetCastShadow(false);
		Arc->SetUsingAbsoluteLocation(true);
		Arc->SetUsingAbsoluteRotation(true);
		Arc->SetUsingAbsoluteScale(true);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ArcBase, Arc);
		Material->SetScalarParameterValue(TEXT("Seed"), FMath::FRand() * 100.f + Index * 17.f);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Arc->SetMaterial(0, Material);
		Owner->AddInstanceComponent(Arc);
		Arc->RegisterComponent();
		Arcs.Add(Arc); ArcMaterials.Add(Material);
	}
	const auto MakeLight = [&](float Intensity, float RadiusCm)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
		Light->SetupAttachment(this);
		Light->SetUsingAbsoluteLocation(true);
		Light->SetLightColor(FMath::Lerp(Color, FLinearColor::White, 0.35f));
		Light->SetIntensity(Intensity);
		Light->SetAttenuationRadius(RadiusCm);
		Light->SetCastShadows(false);
		Owner->AddInstanceComponent(Light);
		Light->RegisterComponent();
		return Light;
	};
	SourceLight = MakeLight(2500.f, 300.f);
	TargetLight = MakeLight(6000.f, 450.f);
}

void UWowLightningBoltComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Age += DeltaTime;
	if (Age >= Duration)
	{
		if (AActor* Owner = GetOwner()) Owner->Destroy();
		return;
	}
	if (AActor* S = Source.Get()) LastStart = S->GetActorTransform().TransformPosition(SourceOffset);
	if (AActor* T = Target.Get()) LastEnd = T->GetActorTransform().TransformPosition(TargetOffset);
	const FVector FullSpan = LastEnd - LastStart;
	const float FullLength = FullSpan.Size();
	// Head travels to the target; the bolt is a ~4.5 m jagged streak behind it.
	const float Travel = FMath::Clamp(Age / FMath::Max(TravelTime, 0.01f), 0.f, 1.f);
	const float StreakFraction = FMath::Clamp(450.f / FMath::Max(FullLength, 1.f), 0.12f, 1.f);
	const FVector Head = LastStart + FullSpan * Travel;
	const FVector Tail = LastStart + FullSpan * FMath::Max(0.f, Travel - StreakFraction);
	const FVector BoltStart = Tail;
	const FVector Span = Head - Tail;
	const float Length = Span.Size();
	if (Length < 1.f) return;
	const FVector Dir = Span / Length;
	FVector Camera = LastStart - FVector(500, 0, -100);
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
		Camera = CameraManager->GetCameraLocation();
	FVector Facing = (Camera - (Tail + Head) * 0.5f).GetSafeNormal();
	Facing = (Facing - Dir * FVector::DotProduct(Facing, Dir)).GetSafeNormal();
	if (Facing.IsNearlyZero()) Facing = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
	const FRotator Rotation = FRotationMatrix::MakeFromXY(Facing, Dir).Rotator();
	// Full while flying, then quick re-strikes at the target that fade out.
	const float After = FMath::Clamp((Age - TravelTime) / FMath::Max(Duration - TravelTime, 0.01f), 0.f, 1.f);
	const float Envelope = FMath::Pow(1.f - After, 1.6f) * (0.75f + 0.25f * Hash01(FMath::FloorToFloat(Age * 30.f)));
	const FVector Side = FVector::CrossProduct(Dir, Facing).GetSafeNormal();
	for (int32 Index = 0; Index < Arcs.Num(); ++Index)
	{
		UStaticMeshComponent* Arc = Arcs[Index];
		if (!Arc) continue;
		const float Width = Index == 0 ? FMath::Clamp(Length * 0.10f, 30.f, 90.f) : FMath::Clamp(Length * 0.07f, 20.f, 60.f);
		const FVector Offset = Index == 0 ? FVector::ZeroVector : Side * (Index == 1 ? 6.f : -6.f);
		Arc->SetWorldLocationAndRotation(BoltStart + Offset, Rotation);
		Arc->SetWorldScale3D(FVector(1.f, Length, Width));
		if (ArcMaterials.IsValidIndex(Index) && ArcMaterials[Index])
			ArcMaterials[Index]->SetScalarParameterValue(TEXT("Intensity"), (Index == 0 ? 3.2f : 1.6f) * Envelope);
	}
	// The light rides with the bolt; the impact flash only once it arrives.
	if (SourceLight) { SourceLight->SetWorldLocation(Head); SourceLight->SetIntensity(2500.f * Envelope); }
	if (TargetLight) { TargetLight->SetWorldLocation(LastEnd); TargetLight->SetIntensity(Travel >= 1.f ? 6000.f * Envelope : 0.f); }
}

// ---------------------------------------------------------------------------
// Shock wave

UWowShockwaveComponent::UWowShockwaveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UWowShockwaveComponent::Fire(float InRadius, const FLinearColor& InColor, float InDuration)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;
	Radius = FMath::Max(40.f, InRadius); Color = InColor; Duration = FMath::Max(0.2f, InDuration); Age = 0.f;
	UStaticMesh* Ground = BuildQuad(this, false, true);
	UMaterialInterface* Base = LightningMaterial(ELightningMaterial::Ring);
	const auto MakeRing = [&](TObjectPtr<UMaterialInstanceDynamic>& OutMaterial)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner);
		Mesh->SetupAttachment(this);
		Mesh->SetStaticMesh(Ground);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		OutMaterial = UMaterialInstanceDynamic::Create(Base, Mesh);
		OutMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		Mesh->SetMaterial(0, OutMaterial);
		Owner->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
		return Mesh;
	};
	Ring = MakeRing(RingMaterial);
	InnerRing = MakeRing(InnerMaterial);
	Flash = NewObject<UPointLightComponent>(Owner);
	Flash->SetupAttachment(this);
	Flash->SetRelativeLocation(FVector(0, 0, 60));
	Flash->SetLightColor(Color);
	Flash->SetIntensity(5000.f);
	Flash->SetAttenuationRadius(Radius * 3.f);
	Flash->SetCastShadows(false);
	Owner->AddInstanceComponent(Flash);
	Flash->RegisterComponent();
}

void UWowShockwaveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Age += DeltaTime;
	if (Age >= Duration)
	{
		if (AActor* Owner = GetOwner()) Owner->Destroy();
		return;
	}
	const float T = Age / Duration;
	const float Ease = 1.f - FMath::Pow(1.f - T, 3.f);
	if (Ring)
	{
		Ring->SetRelativeLocation(FVector(0, 0, 4));
		Ring->SetRelativeScale3D(FVector(Radius * 2.f, Radius * 2.f, 1.f));
		RingMaterial->SetScalarParameterValue(TEXT("Seed"), Ease);
		RingMaterial->SetScalarParameterValue(TEXT("Intensity"), 2.2f);
	}
	if (InnerRing)
	{
		// A second, faster and smaller wave just behind the first.
		const float Inner = FMath::Clamp((Age - 0.08f) / (Duration * 0.7f), 0.f, 1.f);
		InnerRing->SetRelativeLocation(FVector(0, 0, 6));
		InnerRing->SetRelativeScale3D(FVector(Radius * 1.3f, Radius * 1.3f, 1.f));
		InnerMaterial->SetScalarParameterValue(TEXT("Seed"), 1.f - FMath::Pow(1.f - Inner, 3.f));
		InnerMaterial->SetScalarParameterValue(TEXT("Intensity"), Inner > 0.f ? 1.6f : 0.f);
	}
	if (Flash) Flash->SetIntensity(5000.f * FMath::Pow(1.f - T, 2.f));
}

namespace
{
TAutoConsoleVariable<int32> CVarCustomLightningShield(TEXT("wow.CustomLightningShield"), 0,
    TEXT("1 = UE plasma orbs for Lightning Shield, 0 = the original WoW aura visual (default)."));
TAutoConsoleVariable<int32> CVarCustomLightningBolt(TEXT("wow.CustomLightningBolt"), 0,
    TEXT("1 = UE-drawn Lightning Bolt streak, 0 = the original WoW missile with improved lightning (default)."));
}

bool UWowLightningBoltComponent::IsCustomBoltEnabled()
{
    return CVarCustomLightningBolt.GetValueOnGameThread() != 0;
}

bool UWowLightningShieldComponent::IsCustomShieldEnabled()
{
    return CVarCustomLightningShield.GetValueOnGameThread() != 0;
}

// ---------------------------------------------------------------------------
// Channel beam

UWowChannelBeamComponent::UWowChannelBeamComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UWowChannelBeamComponent::Start(AActor* InSource, FName InSourceSocket, const FVector& InSourceOffset, AActor* InTarget,
	FName InTargetSocket, const FVector& InTargetOffset, const FLinearColor& InColor, float InFlow, float InWrithe)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;
	Source = InSource; Target = InTarget;
	SourceSocket = InSourceSocket; TargetSocket = InTargetSocket;
	SourceOffset = InSourceOffset; TargetOffset = InTargetOffset;
	Color = InColor; Flow = InFlow >= 0.f ? 1.f : -1.f; Writhe = InWrithe;
	Age = 0.f; bStopping = false;
	UStaticMesh* Quad = BuildQuad(this, true);
	UMaterialInterface* Base = LightningMaterial(ELightningMaterial::Beam);
	// 0 = straight white-hot core, 1..2 = writhing strands around it.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UStaticMeshComponent* Strand = NewObject<UStaticMeshComponent>(Owner);
		Strand->SetupAttachment(this);
		Strand->SetStaticMesh(Quad);
		Strand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Strand->SetCastShadow(false);
		Strand->SetUsingAbsoluteLocation(true);
		Strand->SetUsingAbsoluteRotation(true);
		Strand->SetUsingAbsoluteScale(true);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Strand);
		Material->SetScalarParameterValue(TEXT("Seed"), Index * 1.37f + FMath::FRand() * 3.f);
		Material->SetScalarParameterValue(TEXT("Flow"), Flow);
		Material->SetScalarParameterValue(TEXT("Writhe"), Index == 0 ? 0.f : Writhe);
		Material->SetScalarParameterValue(TEXT("Intensity"), 0.f);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Strand->SetMaterial(0, Material);
		Owner->AddInstanceComponent(Strand);
		Strand->RegisterComponent();
		Strands.Add(Strand); StrandMaterials.Add(Material);
	}
	const auto MakeLight = [&](float RadiusCm)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
		Light->SetupAttachment(this);
		Light->SetUsingAbsoluteLocation(true);
		Light->SetLightColor(FMath::Lerp(Color, FLinearColor::White, 0.2f));
		Light->SetIntensity(0.f);
		Light->SetAttenuationRadius(RadiusCm);
		Light->SetCastShadows(false);
		Owner->AddInstanceComponent(Light);
		Light->RegisterComponent();
		return Light;
	};
	SourceLight = MakeLight(260.f);
	TargetLight = MakeLight(360.f);
}

void UWowChannelBeamComponent::Stop()
{
	if (bStopping) return;
	bStopping = true;
	StopAge = Age;
}

FVector UWowChannelBeamComponent::EndPoint(AActor* Actor, FName Socket, const FVector& Offset) const
{
	if (Socket != NAME_None)
	{
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Actor);
		for (USkeletalMeshComponent* Mesh : Meshes)
			if (Mesh && Mesh->DoesSocketExist(Socket)) return Mesh->GetSocketLocation(Socket);
	}
	return Actor->GetActorTransform().TransformPosition(Offset);
}

void UWowChannelBeamComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Age += DeltaTime;
	AActor* S = Source.Get();
	AActor* T = Target.Get();
	if (!S || !T) Stop();
	const float FadeOut = bStopping ? FMath::Clamp(1.f - (Age - StopAge) / 0.3f, 0.f, 1.f) : 1.f;
	if (bStopping && FadeOut <= 0.f)
	{
		if (AActor* Owner = GetOwner()) Owner->Destroy();
		return;
	}
	if (!S || !T) return;
	const FVector From = EndPoint(S, SourceSocket, SourceOffset);
	const FVector To = EndPoint(T, TargetSocket, TargetOffset);
	const FVector Span = To - From;
	const float Length = Span.Size();
	// The beam grows out of the hand over 0.2 s instead of popping in.
	const float Grow = FMath::Clamp(Age / 0.2f, 0.f, 1.f);
	const float Envelope = Grow * FadeOut;
	const bool bLongEnough = Length > 20.f;
	FVector Dir = bLongEnough ? Span / Length : FVector::ForwardVector;
	FVector Camera = From - FVector(500, 0, -100);
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
		Camera = CameraManager->GetCameraLocation();
	FVector Facing = (Camera - (From + To) * 0.5f).GetSafeNormal();
	Facing = (Facing - Dir * FVector::DotProduct(Facing, Dir)).GetSafeNormal();
	if (Facing.IsNearlyZero()) Facing = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
	const FRotator Rotation = FRotationMatrix::MakeFromXY(Facing, Dir).Rotator();
	const float Shown = Length * (bStopping ? 1.f : Grow);
	for (int32 Index = 0; Index < Strands.Num(); ++Index)
	{
		UStaticMeshComponent* Strand = Strands[Index];
		if (!Strand) continue;
		Strand->SetVisibility(bLongEnough);
		if (!bLongEnough) continue;
		const float Width = Index == 0 ? 34.f : 70.f;
		Strand->SetWorldLocationAndRotation(From, Rotation);
		Strand->SetWorldScale3D(FVector(1.f, FMath::Max(Shown, 1.f), Width));
		if (UMaterialInstanceDynamic* Material = StrandMaterials.IsValidIndex(Index) ? StrandMaterials[Index].Get() : nullptr)
		{
			Material->SetScalarParameterValue(TEXT("Length"), FMath::Max(Shown, 1.f));
			Material->SetScalarParameterValue(TEXT("Intensity"), (Index == 0 ? 2.2f : 1.5f) * Envelope);
		}
	}
	// Lights breathe with the flowing pulses; the receiving end is the brighter one.
	const float Breath = 0.8f + 0.2f * FMath::Sin(Age * 7.f) * FMath::Sin(Age * 2.3f + 1.f);
	if (SourceLight) { SourceLight->SetWorldLocation(From); SourceLight->SetIntensity((Flow < 0.f ? 2600.f : 1400.f) * Envelope * Breath); }
	if (TargetLight) { TargetLight->SetWorldLocation(To); TargetLight->SetIntensity((Flow < 0.f ? 1600.f : 3000.f) * Envelope * Breath); }
}

// ---------------------------------------------------------------------------
// Loot beam

UWowLootBeamComponent::UWowLootBeamComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UWowLootBeamComponent::Start(const FLinearColor& InColor, float InHeight)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;
	Color = InColor; Height = FMath::Max(100.f, InHeight); Age = 0.f; bStopping = false;
	UStaticMesh* Quad = BuildQuad(this, true);
	UMaterialInterface* Base = LightningMaterial(ELightningMaterial::Beam);
	// 0 = thin bright core, 1 = wide soft glow around it. Pulses rise slowly.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UStaticMeshComponent* Column = NewObject<UStaticMeshComponent>(Owner);
		Column->SetupAttachment(this);
		Column->SetStaticMesh(Quad);
		Column->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Column->SetCastShadow(false);
		Column->SetUsingAbsoluteRotation(true);
		Column->SetUsingAbsoluteScale(true);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Column);
		Material->SetScalarParameterValue(TEXT("Seed"), Index * 3.1f + FMath::FRand() * 5.f);
		Material->SetScalarParameterValue(TEXT("Flow"), 1.f);
		Material->SetScalarParameterValue(TEXT("Writhe"), 0.f);
		Material->SetScalarParameterValue(TEXT("Length"), Height * 3.f); // fewer, longer pulses
		Material->SetScalarParameterValue(TEXT("Intensity"), 0.f);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Column->SetMaterial(0, Material);
		Owner->AddInstanceComponent(Column);
		Column->RegisterComponent();
		Columns.Add(Column); ColumnMaterials.Add(Material);
	}
	FootLight = NewObject<UPointLightComponent>(Owner);
	FootLight->SetupAttachment(this);
	FootLight->SetRelativeLocation(FVector(0, 0, 60));
	FootLight->SetLightColor(Color);
	FootLight->SetIntensity(0.f);
	FootLight->SetAttenuationRadius(260.f);
	FootLight->SetCastShadows(false);
	Owner->AddInstanceComponent(FootLight);
	FootLight->RegisterComponent();
}

void UWowLootBeamComponent::Stop()
{
	if (bStopping) return;
	bStopping = true;
	StopAge = Age;
}

void UWowLootBeamComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Age += DeltaTime;
	const float Fade = (bStopping ? FMath::Clamp(1.f - (Age - StopAge) / 0.4f, 0.f, 1.f) : 1.f) * FMath::Clamp(Age / 0.5f, 0.f, 1.f);
	if (bStopping && Fade <= 0.f)
	{
		if (AActor* Owner = GetOwner()) Owner->Destroy();
		return;
	}
	const FVector Foot = GetComponentLocation();
	FVector Camera = Foot + FVector(500, 0, 0);
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
		Camera = CameraManager->GetCameraLocation();
	FVector Facing = Camera - Foot;
	Facing.Z = 0.f;
	Facing = Facing.GetSafeNormal();
	if (Facing.IsNearlyZero()) Facing = FVector::ForwardVector;
	const FRotator Rotation = FRotationMatrix::MakeFromXY(Facing, FVector::UpVector).Rotator();
	const float Breath = 0.85f + 0.15f * FMath::Sin(Age * 2.2f);
	for (int32 Index = 0; Index < Columns.Num(); ++Index)
	{
		UStaticMeshComponent* Column = Columns[Index];
		if (!Column) continue;
		Column->SetWorldRotation(Rotation);
		Column->SetWorldScale3D(FVector(1.f, Height, Index == 0 ? 34.f : 150.f));
		if (UMaterialInstanceDynamic* Material = ColumnMaterials.IsValidIndex(Index) ? ColumnMaterials[Index].Get() : nullptr)
			Material->SetScalarParameterValue(TEXT("Intensity"), (Index == 0 ? 1.3f : 0.45f) * Fade * Breath);
	}
	if (FootLight) FootLight->SetIntensity(900.f * Fade * Breath);
}
