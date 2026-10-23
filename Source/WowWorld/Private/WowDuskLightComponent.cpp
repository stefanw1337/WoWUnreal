#include "WowDuskLightComponent.h"
#include "WowSkyManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "WowClientPreferences.h"

UWowDuskLightComponent::UWowDuskLightComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=.5f;
    SetMobility(EComponentMobility::Movable);
    SetIntensity(0);
    SetAttenuationRadius(1100);
    SetLightColor(FLinearColor(1,.58f,.22f));
    SetCastShadows(false);
    MaxDrawDistance=14000;
    MaxDistanceFadeRange=2500;
}
void UWowDuskLightComponent::BeginPlay() { Super::BeginPlay(); UpdateDuskLight(); }
void UWowDuskLightComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{ Super::TickComponent(Dt,Type,Function); UpdateDuskLight(); }
void UWowDuskLightComponent::UpdateDuskLight()
{
    if(!Sky.IsValid()) Sky=Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(GetWorld(),AWowSkyManager::StaticClass()));
    if(!Sky.IsValid()) { if(!bOutdoor) ApplyTime(720); return; }
    ApplyTime(Sky->GetTimeOfDay());
}
float UWowDuskLightComponent::NightFactor(float Minutes)
{
    if (!FMath::IsFinite(Minutes)) return 1.f;
    Minutes=FMath::Fmod(FMath::Fmod(Minutes,1440.f)+1440.f,1440.f);
    const float Day=Minutes<720 ? FMath::Clamp((Minutes-300)/120,0.0f,1.0f)
        : FMath::Clamp((1140-Minutes)/120,0.0f,1.0f);
    return 1-Day*Day*(3-2*Day);
}
void UWowDuskLightComponent::ApplyTime(float Minutes)
{
    const float Factor=bOutdoor?NightFactor(Minutes):1.f;
    SetIntensity((FixtureIntensity>0?FixtureIntensity:(bOutdoor?5200.f:3200.f))*Factor*FWowClientPreferences::GetFireLightIntensityScale());
    SetVisibility(Factor>.001f);
    for (const auto& Material:GlassMaterials) if(Material) Material->SetScalarParameterValue(TEXT("LampGlow"),Factor);
}
