#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowTerrainMaterial.h"
#include "Materials/Material.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWmoSurfaceTest,
    "WowUnreal.Stormwind.SurfaceSidedness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowWmoSurfaceTest::RunTest(const FString&)
{
    UMaterial* Single=FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial(false);
    UMaterial* Double=FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial(true);
    if (!TestNotNull(TEXT("Single-sided building material is available"),Single) ||
        !TestNotNull(TEXT("Two-sided material remains available"),Double)) return false;
    TestFalse(TEXT("Back faces of solid WMO facades must be culled"),Single->IsTwoSided());
    TestTrue(TEXT("Explicitly two-sided surfaces keep their back faces"),Double->IsTwoSided());
    TestTrue(TEXT("Variants have separate cooked material assets"),Single!=Double);
    TestEqual(TEXT("Wall surfaces stay opaque"),Single->GetBlendMode(),BLEND_Opaque);
    TestTrue(TEXT("Buildings still use world lighting"),Single->GetShadingModels().HasShadingModel(MSM_DefaultLit));
    return true;
}
#endif
