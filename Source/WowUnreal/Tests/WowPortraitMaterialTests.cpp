#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../WowPortraitMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture2D.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowPortraitMaterialTest,
    "WowUnreal.UI.PortraitMaterialCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowPortraitMaterialTest::RunTest(const FString&)
{
    UMaterial* Base = LoadObject<UMaterial>(nullptr,TEXT("/Game/Materials/M_WowCharacterV2.M_WowCharacterV2"));
    if (!TestNotNull(TEXT("Actual character shader"),Base)) return false;
    auto* Variant = NewObject<UMaterialInstanceConstant>();
    Variant->SetParentEditorOnly(Base);
    auto* Skin = UTexture2D::CreateTransient(2,2);
    auto* UpdatedSkin = UTexture2D::CreateTransient(2,2);
    auto* Source = UMaterialInstanceDynamic::Create(Variant,GetTransientPackage());
    Source->SetTextureParameterValue(TEXT("BaseTexture"),Skin);
    Source->SetScalarParameterValue(TEXT("FirstPersonOpacity"),.15f);
    Source->SetVectorParameterValue(TEXT("PortraitTestTint"),FLinearColor(.2f,.4f,.7f,1));
    auto* Portrait = WowPortraitMaterial::Create(Source,GetTransientPackage());
    if (!TestNotNull(TEXT("Portrait copy"),Portrait)) return false;
    TestTrue(TEXT("Portrait has a valid constant parent, preserving shader variant"),Portrait->Parent==Variant);
    UTexture* Texture = nullptr;
    TestTrue(TEXT("Skin texture parameter exists"),Portrait->GetTextureParameterValue(TEXT("BaseTexture"),Texture));
    TestTrue(TEXT("Live skin texture is preserved instead of white default"),Texture==Skin);
    float Opacity = 0;
    Portrait->GetScalarParameterValue(TEXT("FirstPersonOpacity"),Opacity);
    TestEqual(TEXT("Portrait stays visible in first person"),Opacity,1.f);
    Source->GetScalarParameterValue(TEXT("FirstPersonOpacity"),Opacity);
    TestEqual(TEXT("World character fade remains unchanged"),Opacity,.15f);
    FLinearColor Tint;
    Portrait->GetVectorParameterValue(TEXT("PortraitTestTint"),Tint);
    TestEqual(TEXT("Vector overrides are retained too"),Tint,FLinearColor(.2f,.4f,.7f,1));
    Source->SetTextureParameterValue(TEXT("BaseTexture"),UpdatedSkin);
    auto* Refreshed = WowPortraitMaterial::Create(Source,GetTransientPackage());
    Refreshed->GetTextureParameterValue(TEXT("BaseTexture"),Texture);
    TestTrue(TEXT("Late skin/equipment updates reach rebuilt portrait"),Texture==UpdatedSkin);
    auto* Plain = WowPortraitMaterial::Create(Base,GetTransientPackage());
    TestTrue(TEXT("Plain material source remains supported"),Plain && Plain->Parent==Base);
    TestNull(TEXT("Missing source safely returns no copy"),WowPortraitMaterial::Create(nullptr,GetTransientPackage()));
    return true;
}
#endif
