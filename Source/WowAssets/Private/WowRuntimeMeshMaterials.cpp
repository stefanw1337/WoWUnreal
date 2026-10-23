#include "WowRuntimeMeshMaterials.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/Material.h"
void WowPrepareRuntimeMeshMaterials(UStaticMesh* Mesh, FMeshDescription& Description)
{
    auto& Materials = Mesh->GetStaticMaterials();
    if (Materials.IsEmpty()) Materials.Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
    // Fast builds match polygon groups by slot NAME, not by array position.
    // Editor section metadata used to conceal the absent names in our imports.
    for (int32 I=0; I<Materials.Num(); ++I)
        Materials[I].MaterialSlotName = FName(*FString::Printf(TEXT("RuntimeSection%d"), I));
    auto Names = FStaticMeshAttributes(Description).GetPolygonGroupMaterialSlotNames();
    int32 Section = 0;
    for (FPolygonGroupID Group : Description.PolygonGroups().GetElementIDs())
        Names[Group] = Materials[FMath::Min(Section++, Materials.Num()-1)].MaterialSlotName;
}
