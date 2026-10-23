#include "WowPrepareStandaloneCommandlet.h"
#include "WowEquipmentManager.h"
#include "WowM2ParticleComponent.h"
#include "WowSkyManager.h"
#include "WowLightningShieldComponent.h"
#include "WowDeathManager.h"
#include "WowStealthVisual.h"
#include "WowIsometricCamera.h"
#include "WowWaterMaterial.h"
#include "WowTumbleweedComponent.h"
#include "WowTerrainMaterial.h"
#include "WowNodeGlintComponent.h"
#include "WowVrUiPresenter.h"
#include "Engine/World.h"
#include "ShaderCompiler.h"
void WowPreparePortraitMaterial();
void WowPreparePaperDollMaterial();
#include "WowCharacterBuilder.h"
void WowPrepareTargetRingMaterial();
void WowPrepareCharacterHighlightMaterial();
void WowPrepareLoginGammaMaterial(); // r616
int32 UWowPrepareStandaloneCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
    if (!FParse::Param(FCommandLine::Get(), TEXT("preparestandalone"))) return 1;
    if (FParse::Param(FCommandLine::Get(),TEXT("paperdollonly")))
    {
        WowPreparePaperDollMaterial();
        GShaderCompilingManager->FinishAllCompilation();
        return 0;
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("rainonly")))
    {
        FWowTerrainMaterial::PrepareRainMaterials();
        FWowTerrainMaterial::GetLampGlassMaterial();
    FWowTerrainMaterial::GetOpaqueWindowLightMaterial();
        GShaderCompilingManager->FinishAllCompilation();
        return 0;
    }
    WowPreparePaperDollMaterial();
    FWowTerrainMaterial::GetLampGlassMaterial();
    FWowTerrainMaterial::GetOpaqueWindowLightMaterial();
    FWowEquipmentManager::PrepareStandaloneMaterials();
    NewObject<UWowM2ParticleComponent>()->PrepareStandaloneMaterials();
    WowPreparePortraitMaterial();
    FWowCharacterBuilder::PrepareGlueMaterials();
    WowPrepareTargetRingMaterial();
    FWowTerrainMaterial::GetSimpleWaterfallObjectMaterial();
    FWowTerrainMaterial::GetSimpleObjectMaterial(); // M_WowSimpleObjectV2 (skeletal usage)
    FWowTerrainMaterial::GetSimpleObjectMaterialSingleSided(); // r306 M_WowSimpleObjectSingleSidedV1
    FWowTerrainMaterial::GetSimpleAlphaKeyObjectMaterial();
    WowPrepareCharacterHighlightMaterial();
    UWowNodeGlintComponent::GetGlintMaterial();
    UWowVrUiPresenter::GetPanelMaterial();
    UWowLightningShieldComponent::PrepareStandaloneMaterials();
    UWowDeathManager::PrepareStandaloneMaterials();
    FWowStealthVisual::PrepareStandaloneMaterials(); // r605 M_WowStealthOverlay, M_WowStealthDetect_v1
    FWowIsometricCamera::PrepareStandaloneMaterials(); // r610 M_WowIsoCutaway_v1
    WowPrepareLoginGammaMaterial(); // r616 M_WowGlueGammaPost_v1
    FWowWaterMaterial::GetWaterMaterial();
    FWowWaterMaterial::GetOceanMaterial();
    UWowTumbleweedComponent::PrepareMaterial();
    FWowTerrainMaterial::PrepareRainMaterials(); // includes the rain puddle material
    FWowWaterMaterial::GetLavaMaterial();
    FWowWaterMaterial::GetSlimeMaterial();
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
    World->SpawnActor<AWowSkyManager>()->PrepareStandaloneMaterials();
    GShaderCompilingManager->FinishAllCompilation();
    World->DestroyWorld(false);
    UE_LOG(LogTemp, Display, TEXT("Standalone material preparation complete."));
    return 0;
#else
    return 1;
#endif
}
