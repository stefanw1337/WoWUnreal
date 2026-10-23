#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WowLightningShieldComponent.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;

/**
 * Shaman Lightning Shield drawn with UE effects instead of the legacy M2 orbs:
 * HDR plasma orbs (bloom), procedural lightning arcs that re-strike several times
 * a second, and a flickering point light that lights the character.
 * One orb per remaining charge; the orbs circle the owner at waist height.
 */
UCLASS()
class WOWWORLD_API UWowLightningShieldComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWowLightningShieldComponent();

	/** Height and radius of the owner (UE units), used to place the orbit. */
	void Setup(float InOwnerHeight, float InOwnerRadius, const FLinearColor& InColor);
	/** Remaining charges (0 keeps the default of 3 orbs). */
	void SetCharges(int32 InCharges);

	/** Creates and saves the cooked shader graphs (standalone material preparation). */
	static void PrepareStandaloneMaterials();
	/** wow.CustomLightningShield (default 0): the original M2 aura visual is used unless enabled. */
	static bool IsCustomShieldEnabled();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void OnRegister() override;

private:
	void BuildParts();
	void RebuildOrbs();

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Orbs;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Arcs;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> OrbMaterials;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ArcMaterials;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<UStaticMesh> OrbMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ArcMesh;

	FLinearColor Color = FLinearColor(0.45f, 0.55f, 1.0f);
	float OwnerHeight = 180.f;
	float OwnerRadius = 40.f;
	int32 Charges = 3;
	float Age = 0.f;
	float NextStrike = 0.f;
	float LightFlicker = 1.f;
	bool bBuilt = false;
};

/**
 * Lightning Bolt drawn as real lightning: several jagged, re-striking arcs from the
 * caster's hand to the target, a bright core and light flashes at both ends.
 * Destroys its owning actor when done.
 */
UCLASS()
class WOWWORLD_API UWowLightningBoltComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWowLightningBoltComponent();
	/** wow.CustomLightningBolt (default 0): the original missile is used unless this is on. */
	static bool IsCustomBoltEnabled();
	/** Endpoints follow the actors (offsets in their local space); duration in seconds. */
	void Fire(AActor* InSource, const FVector& InSourceOffset, AActor* InTarget, const FVector& InTargetOffset,
		const FLinearColor& InColor, float InDuration = 0.45f);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Arcs;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ArcMaterials;
	UPROPERTY() TObjectPtr<UPointLightComponent> SourceLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> TargetLight;
	TWeakObjectPtr<AActor> Source, Target;
	FVector SourceOffset = FVector::ZeroVector, TargetOffset = FVector::ZeroVector;
	FVector LastStart = FVector::ZeroVector, LastEnd = FVector::ZeroVector;
	FLinearColor Color = FLinearColor(0.55f, 0.65f, 1.0f);
	float Duration = 0.45f;
	float TravelTime = 0.25f;
	float Age = 0.f;
};

/**
 * Ground shock wave (Earth Shock and similar): an expanding, rough ring of energy on
 * the ground at the target plus a short coloured light flash. Destroys its owner when done.
 */
UCLASS()
class WOWWORLD_API UWowShockwaveComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWowShockwaveComponent();
	void Fire(float InRadius, const FLinearColor& InColor, float InDuration = 0.8f);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ring;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> InnerRing;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> InnerMaterial;
	UPROPERTY() TObjectPtr<UPointLightComponent> Flash;
	float Radius = 150.f;
	float Duration = 0.8f;
	float Age = 0.f;
	FLinearColor Color = FLinearColor(0.9f, 0.6f, 0.25f);
};

/**
 * Channelled beam between caster and target (Drain Life, Drain Soul, Drain Mana, Health
 * Funnel, Mind Flay): a bright core with two writhing strands, pulses that flow in the
 * direction of the drain, and coloured lights at both ends. Stays until Stop(); then fades
 * and destroys its owning actor.
 */
UCLASS()
class WOWWORLD_API UWowChannelBeamComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWowChannelBeamComponent();
	/** Ends follow the sockets when present, else the actor-local offsets. Flow +1 = caster to target, -1 = drained towards the caster. */
	void Start(AActor* InSource, FName InSourceSocket, const FVector& InSourceOffset, AActor* InTarget, FName InTargetSocket,
		const FVector& InTargetOffset, const FLinearColor& InColor, float InFlow, float InWrithe = 1.f);
	void Stop();
	bool IsStopping() const { return bStopping; }
	AActor* GetBeamTarget() const { return Target.Get(); }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector EndPoint(AActor* Actor, FName Socket, const FVector& Offset) const;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Strands;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> StrandMaterials;
	UPROPERTY() TObjectPtr<UPointLightComponent> SourceLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> TargetLight;
	TWeakObjectPtr<AActor> Source, Target;
	FName SourceSocket, TargetSocket;
	FVector SourceOffset = FVector::ZeroVector, TargetOffset = FVector::ZeroVector;
	FLinearColor Color = FLinearColor(1.f, 0.15f, 0.1f);
	float Flow = -1.f;
	float Writhe = 1.f;
	float Age = 0.f;
	float StopAge = 0.f;
	bool bStopping = false;
};

/**
 * Loot beam: a soft vertical column of light over a lootable corpse, coloured by the best
 * item quality known for it, with a small light at its foot. Our own implementation
 * (the idea is credited to manatactical's Loot Beam). Stop() fades it and destroys the owner.
 */
UCLASS()
class WOWWORLD_API UWowLootBeamComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWowLootBeamComponent();
	void Start(const FLinearColor& InColor, float InHeight = 900.f);
	void Stop();
	const FLinearColor& GetBeamColor() const { return Color; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Columns;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ColumnMaterials;
	UPROPERTY() TObjectPtr<UPointLightComponent> FootLight;
	FLinearColor Color = FLinearColor::White;
	float Height = 900.f;
	float Age = 0.f;
	float StopAge = 0.f;
	bool bStopping = false;
};
