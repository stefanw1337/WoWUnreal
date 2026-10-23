#include "WowRuntimeLumen.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshCardBuild.h"
#include "MeshCardRepresentation.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowRuntimeLumen, Log, All);

void WowBuildRuntimeLumenCards(UStaticMesh* Mesh,
    const FMeshDescription& Description, bool bTerrain)
{
    if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty()) return;
    FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
    // Never replace cards which a scene proxy could already reference.
    if (LOD.CardRepresentationData || Description.Triangles().Num() == 0) return;

    bool bHasSurface = false;
    bool bTwoSided = false;
    for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
    {
        const UMaterialInterface* Material = Slot.MaterialInterface;
        if (Material && (Material->GetBlendMode() == BLEND_Opaque || Material->GetBlendMode() == BLEND_Masked))
        {
            bHasSurface = true;
            bTwoSided |= Material->IsTwoSided();
        }
    }
    if (!bHasSurface) return; // Do not capture additive/translucent effects as solid scenery.

    const FBox Bounds = Description.GetBounds().GetBox();
    if (!Bounds.IsValid || Bounds.ContainsNaN() || Bounds.GetSize().GetMax() < 0.01) return;
    TUniquePtr<FCardRepresentationData> Cards = MakeUnique<FCardRepresentationData>();
    FMeshCardsBuildData& Data = Cards->MeshCardsBuildData;
    Data.Bounds = Bounds;
    Data.bMostlyTwoSided = bTwoSided;

    if (bTerrain && Bounds.GetSize().X > 1.0 && Bounds.GetSize().Y > 1.0)
    {
        // An ADT is over 500m wide. A single six-view capture would spread the
        // surface cache over the entire tile. Partition triangle bounds into a
        // fixed 4x4 grid: at most 96 cards, shared by all views of this tile.
        // Keep the complete triangle bounds at cell seams (small overlap), and
        // each cell's actual height range so hills do not waste capture depth.
        constexpr int32 Grid = 4;
        FBox Cells[Grid * Grid];
        for (FBox& Cell : Cells) Cell = FBox(ForceInit);
        const auto Positions = FStaticMeshConstAttributes(Description).GetVertexPositions();
        const FVector Size = Bounds.GetSize();
        for (FTriangleID Triangle : Description.Triangles().GetElementIDs())
        {
            FBox TriangleBounds(ForceInit);
            for (FVertexID Vertex : Description.GetTriangleVertices(Triangle))
                TriangleBounds += FVector(Positions[Vertex]);
            if (TriangleBounds.ContainsNaN()) return;
            const FVector Center = TriangleBounds.GetCenter();
            const int32 X = FMath::Clamp(FMath::FloorToInt((Center.X - Bounds.Min.X) / Size.X * Grid), 0, Grid - 1);
            const int32 Y = FMath::Clamp(FMath::FloorToInt((Center.Y - Bounds.Min.Y) / Size.Y * Grid), 0, Grid - 1);
            Cells[Y * Grid + X] += TriangleBounds;
        }
        for (const FBox& Cell : Cells)
        {
            if (!Cell.IsValid) continue;
            FMeshCardsBuildData CellData;
            CellData.Bounds = Cell.ExpandBy(1.0); // Nonzero extents even on perfectly flat ground.
            MeshCardRepresentation::SetCardsFromBounds(CellData);
            Data.CardBuildData.Append(CellData.CardBuildData);
        }
    }
    else
    {
        // Same runtime six-direction method used by UE's dynamic and skeletal
        // meshes. WMO groups remain separate; this is not an interior unwrapping
        // solution for arbitrary concave meshes.
        Data.Bounds = Bounds.ExpandBy(1.0);
        MeshCardRepresentation::SetCardsFromBounds(Data);
    }

    if (Data.CardBuildData.IsEmpty() || Data.ContainsNaN()) return;
    const int32 Count = Data.CardBuildData.Num();
    // FStaticMeshLODResources owns/deletes this CPU data. No asset cook, GPU
    // readback, editor task or per-frame mesh rebuild is needed.
    LOD.CardRepresentationData = Cards.Release();
    UE_LOG(LogWowRuntimeLumen, Verbose, TEXT("Runtime Lumen cards: %s cards=%d terrain=%d twoSided=%d"),
        *Mesh->GetName(), Count, bTerrain, bTwoSided);
}
