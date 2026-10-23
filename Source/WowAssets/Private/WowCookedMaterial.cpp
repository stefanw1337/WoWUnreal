#include "WowCookedMaterial.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "UObject/SavePackage.h"
#endif
UMaterial* WowCookedMaterial::Load(const FString& Name)
{
#if WITH_EDITOR
    if (FParse::Param(FCommandLine::Get(), TEXT("preparestandalone"))) return nullptr;
#endif
    const FString Package = TEXT("/Game/Materials/") + Name;
    return LoadObject<UMaterial>(nullptr, *(Package + TEXT(".") + Name));
}
UMaterial* WowCookedMaterial::Save(UMaterial* Material, const FString& Name)
{
#if WITH_EDITOR
    if (Material && FParse::Param(FCommandLine::Get(), TEXT("preparestandalone")))
    {
        const FString PackageName = TEXT("/Game/Materials/") + Name;
        UPackage* Package = CreatePackage(*PackageName);
        // This dedicated package is fully regenerated from its sole shader graph.
        Package->MarkAsFullyLoaded();
        UMaterial* Saved = DuplicateObject<UMaterial>(Material, Package, *Name);
        Saved->ClearFlags(RF_Transient);
        ForEachObjectWithOuter(Saved, [](UObject* Child) { Child->ClearFlags(RF_Transient); }, true);
        Saved->SetFlags(RF_Public | RF_Standalone);
        Saved->PostEditChange();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        if (!UPackage::SavePackage(Package, Saved, *Filename, Args))
            UE_LOG(LogTemp, Fatal, TEXT("Cannot save standalone shader %s"), *Filename);
        return Saved;
    }
#endif
    return Material;
}
