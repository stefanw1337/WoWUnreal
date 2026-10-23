#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowTerrainMaterial.h"
#include "Materials/Material.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowMaterialLifetimeTest,
    "WowUnreal.Streaming.MaterialLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowMaterialLifetimeTest::RunTest(const FString&)
{
    TArray<TWeakObjectPtr<UMaterial>> Materials;
    Materials.Add(FWowTerrainMaterial::GetBaseMaterial());
    Materials.Add(FWowTerrainMaterial::GetSimpleObjectMaterial());
    Materials.Add(FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial(false));
    Materials.Add(FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial(true));
    Materials.Add(FWowTerrainMaterial::GetSimpleFoliageObjectMaterial());
    Materials.Add(FWowTerrainMaterial::GetSimpleTranslucentObjectMaterial());
    Materials.Add(FWowTerrainMaterial::GetSimpleAdditiveObjectMaterial());
    Materials.Add(FWowTerrainMaterial::GetLampGlassMaterial());
    Materials.Add(FWowTerrainMaterial::GetOpaqueWindowLightMaterial());
    // Runtime GC does not retain assets merely because the editor marks them
    // standalone. Simulate a gap with no mesh instances in the new map yet.
    for (const auto& Material : Materials)
    {
        if (!TestTrue(TEXT("Material loaded"), Material.IsValid())) return false;
    }
    TArray<bool> WasStandalone;
    for (const auto& Material : Materials)
    {
        WasStandalone.Add(Material->HasAnyFlags(RF_Standalone));
        Material->ClearFlags(RF_Standalone);
    }
    CollectGarbage(RF_NoFlags);
    for (int32 Index=0; Index<Materials.Num(); ++Index)
    {
        TestTrue(TEXT("Base material survives a map unload with no mesh owners"), Materials[Index].IsValid());
        if (Materials[Index].IsValid() && WasStandalone[Index]) Materials[Index]->SetFlags(RF_Standalone);
    }
    TestEqual(TEXT("Opaque parent retains identity across map unload"),
        FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial(false), Materials[2].Get());
    TestEqual(TEXT("Window parent retains identity across map unload"),
        FWowTerrainMaterial::GetOpaqueWindowLightMaterial(), Materials[8].Get());
    return true;
}
#endif
