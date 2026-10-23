#include "WowSkeletalMeshBuilder.h"
#include "Formats/M2Types.h"
#include "Coord/WowCoordinate.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Components.h"

void WowBuildRuntimeSkeletalMesh(USkeletalMesh* Mesh, const FM2Data& Data,
    const TArray<int32>& BoneRemap, const TArray<int32>& VisiblePasses)
{
    Mesh->AllocateResourceForRendering();
    auto* RenderData = Mesh->GetResourceForRendering();
    auto* LOD = new FSkeletalMeshLODRenderData();
    RenderData->LODRenderData.Add(LOD);
    RenderData->NumInlinedLODs = RenderData->NumNonOptionalLODs = 1;
    LOD->bStreamedDataInlined = true;
    LOD->bIsLODOptional = false;
    const int32 BoneCount = Mesh->GetRefSkeleton().GetNum();
    for (int32 I=0; I<BoneCount; ++I)
    {
        LOD->ActiveBoneIndices.Add(I);
        LOD->RequiredBones.Add(I);
    }
    TArray<FStaticMeshBuildVertex> Vertices;
    TArray<FSkinWeightInfo> Weights;
    TArray<uint32> Indices;
    FBox Bounds(ForceInit);
    const bool bHasPasses = !Data.RenderPasses.IsEmpty() && !Data.Submeshes.IsEmpty();
    // r644 (Stefan: the login sky's floating rocks and the hanging spire washed out, a hard arc across the
    // sky): two-texture passes take their second texture from the vertex's second UV set (M2
    // texture-unit lookup 1); it was never uploaded, so the second layer (the fade masks) sat on the first
    // layer's UVs. Models without a second UV set get a copy of the first.
    bool bHasSecondUV = false;
    for (const auto& Vertex : Data.Vertices)
        if (!Vertex.TexCoord2.IsNearlyZero()) { bHasSecondUV = true; break; }
    for (int32 PassIndex : VisiblePasses)
    {
        const FM2Submesh* Submesh = bHasPasses ? &Data.Submeshes[Data.RenderPasses[PassIndex].SubmeshIndex] : nullptr;
        const int32 Start = Submesh ? Submesh->StartTriangle : 0;
        const int32 End = FMath::Min(Data.Indices.Num(), Start + (Submesh ? Submesh->NumTriangles : Data.Indices.Num()));
        auto& Section = LOD->RenderSections.AddDefaulted_GetRef();
        Section.MaterialIndex = LOD->RenderSections.Num()-1;
        Section.BaseIndex = Indices.Num();
        Section.BaseVertexIndex = Vertices.Num();
        // GPU limits apply to each section palette, not the full skeleton.
        // M2 vertices use byte bone indices, so this palette has at most 256 entries.
        Section.MaxBoneInfluences = 4;
        TArray<int32> VertexMap; VertexMap.Init(INDEX_NONE, Data.Vertices.Num());
        for (int32 T=Start; T+2<End; T+=3)
        {
            if (!Data.Vertices.IsValidIndex(Data.Indices[T]) || !Data.Vertices.IsValidIndex(Data.Indices[T+1]) || !Data.Vertices.IsValidIndex(Data.Indices[T+2])) continue;
            for (int32 Corner=0; Corner<3; ++Corner)
            {
                const int32 SourceIndex = Data.Indices[T+Corner];
                int32& Mapped = VertexMap[SourceIndex];
                if (Mapped == INDEX_NONE)
                {
                    const auto& Source = Data.Vertices[SourceIndex];
                    FStaticMeshBuildVertex V{};
                    V.Position = FVector3f(Source.Position.Y,Source.Position.X,Source.Position.Z) * FWowCoordinate::SCALE;
                    V.TangentZ = FVector3f(Source.Normal.Y,Source.Normal.X,Source.Normal.Z).GetSafeNormal();
                    V.TangentX = FVector3f::CrossProduct(V.TangentZ,FVector3f(0,1,0)).GetSafeNormal();
                    if (V.TangentX.IsNearlyZero()) V.TangentX = FVector3f::CrossProduct(V.TangentZ,FVector3f(1,0,0)).GetSafeNormal();
                    V.TangentY = FVector3f::CrossProduct(V.TangentZ,V.TangentX);
                    V.UVs[0] = FVector2f(Source.TexCoord.X,Source.TexCoord.Y);
                    V.UVs[1] = bHasSecondUV ? FVector2f(Source.TexCoord2.X,Source.TexCoord2.Y) : V.UVs[0];
                    V.Color = FColor::White;
                    Mapped = Vertices.Add(V);
                    Bounds += FVector(V.Position);
                    FSkinWeightInfo W{};
                    uint32 Total=0, Assigned=0;
                    for (int32 J=0; J<4; ++J) Total += Source.BoneWeights[J];
                    for (int32 J=0; J<4; ++J)
                    {
                        const int32 SkeletonBone = BoneRemap.IsValidIndex(Source.BoneIndices[J]) ? BoneRemap[Source.BoneIndices[J]] : 0;
                        W.InfluenceBones[J] = Section.BoneMap.AddUnique(static_cast<FBoneIndexType>(SkeletonBone));
                        W.InfluenceWeights[J] = Total ? uint32(Source.BoneWeights[J])*65535u/Total : 0;
                        Assigned += W.InfluenceWeights[J];
                    }
                    W.InfluenceWeights[0] += 65535u-Assigned;
                    Weights.Add(W);
                }
                Indices.Add(Mapped);
            }
        }
        Section.NumTriangles = (Indices.Num()-Section.BaseIndex)/3;
        Section.NumVertices = Vertices.Num()-Section.BaseVertexIndex;
        Section.DuplicatedVerticesBuffer.Init(Section.NumVertices, TMap<int32,TArray<int32>>());
        const FName Slot(*FString::Printf(TEXT("Section_%d"),Section.MaterialIndex));
        auto* MID = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface),Mesh);
        auto& Material = Mesh->GetMaterials().Add_GetRef(FSkeletalMaterial(MID,true,false,Slot));
        Material.UVChannelData.bInitialized = true;
        Material.UVChannelData.LocalUVDensities[0] = 1.f;
    }
    LOD->StaticVertexBuffers.PositionVertexBuffer.Init(Vertices);
    // UE 5.7's legacy array overload ignores its UV-count argument and
    // exposes all eight FStaticMeshBuildVertex channels. CPU skinning supports
    // at most four; M2 uses exactly the first channel here.
    FConstMeshBuildVertexView VertexView = MakeConstMeshBuildVertexView(Vertices);
    VertexView.UVs.SetNum(2); // r644: second UV set
    LOD->StaticVertexBuffers.StaticMeshVertexBuffer.Init(VertexView);
    LOD->StaticVertexBuffers.ColorVertexBuffer.Init(Vertices);
    LOD->SkinWeightVertexBuffer.SetNeedsCPUAccess(true);
    LOD->SkinWeightVertexBuffer.SetMaxBoneInfluences(4);
    LOD->SkinWeightVertexBuffer.SetUse16BitBoneIndex(false);
    LOD->SkinWeightVertexBuffer.SetUse16BitBoneWeight(true);
    LOD->SkinWeightVertexBuffer = Weights;
    LOD->MultiSizeIndexContainer.RebuildIndexBuffer(sizeof(uint32),Indices);
    Mesh->SetImportedBounds(FBoxSphereBounds(Bounds));
    Mesh->CalculateInvRefMatrices();
    // Cooked clients use a socket lookup map; transient MPQ meshes never run PostLoad.
    Mesh->RebuildSocketMap();
    Mesh->InitResources();
}
