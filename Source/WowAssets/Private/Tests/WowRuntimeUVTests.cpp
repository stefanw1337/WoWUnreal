#include "Misc/AutomationTest.h"
#include "WowSkeletalMeshBuilder.h"
#include "Formats/M2Types.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
extern void WowBuildRuntimeSkeletalMesh(USkeletalMesh*, const FM2Data&,
    const TArray<int32>&, const TArray<int32>&);
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowRuntimeUVTest, "WowUnreal.Standalone.RuntimeSkinUVs",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowRuntimeUVTest::RunTest(const FString& Parameters)
{
    FM2Data Data;
    Data.Bones.AddDefaulted();
    for (int32 I = 0; I < 3; ++I)
    {
        auto& V = Data.Vertices.AddDefaulted_GetRef();
        V.Position = FVector(I == 1, I == 2, 0);
        V.Normal = FVector::UpVector;
        V.TexCoord = FVector2D(I * 0.25, 0.5);
        V.BoneWeights[0] = 255;
    }
    Data.Indices = {0, 1, 2};
    TStrongObjectPtr<USkeleton> Skeleton(FWowSkeletalMeshBuilder::CreateSkeleton(Data, TEXT("UVRegression")));
    if (!TestNotNull(TEXT("Skeleton"), Skeleton.Get())) return false;
    TStrongObjectPtr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>());
    Mesh->SetSkeleton(Skeleton.Get());
    Mesh->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
    Mesh->AddLODInfo();
    WowBuildRuntimeSkeletalMesh(Mesh.Get(), Data, {0}, {0});
    FlushRenderingCommands();
    const auto& Buffer = Mesh->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.StaticMeshVertexBuffer;
    TestEqual(TEXT("Runtime UV channels remain CPU-skin compatible"), Buffer.GetNumTexCoords(), 1u);
    TestEqual(TEXT("Triangle vertices"), Buffer.GetNumVertices(), 3u);
    for (uint32 I = 0; I < 3; ++I)
        TestTrue(TEXT("M2 UV coordinates survive resource initialization"),
            Buffer.GetVertexUV(I, 0).Equals(FVector2f(I * 0.25f, 0.5f)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowRuntimeBonePaletteTest, "WowUnreal.Standalone.RuntimeBonePalette",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowRuntimeBonePaletteTest::RunTest(const FString&)
{
    FM2Data Data; Data.Bones.SetNum(288);
    for (int32 I=0; I<3; ++I)
    {
        auto& V=Data.Vertices.AddDefaulted_GetRef();
        V.Position=FVector(I==1,I==2,0); V.Normal=FVector::UpVector;
        V.BoneIndices[0]=I; V.BoneWeights[0]=255;
    }
    Data.Indices={0,1,2};
    TStrongObjectPtr<USkeleton> Skeleton(FWowSkeletalMeshBuilder::CreateSkeleton(Data,TEXT("PaletteRegression")));
    TStrongObjectPtr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>());
    Mesh->SetSkeleton(Skeleton.Get()); Mesh->SetRefSkeleton(Skeleton->GetReferenceSkeleton()); Mesh->AddLODInfo();
    // Global indices above 255 must survive through the section palette.
    WowBuildRuntimeSkeletalMesh(Mesh.Get(),Data,{287,260,12},{0});
    FlushRenderingCommands();
    auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];
    const auto& Palette=LOD.RenderSections[0].BoneMap;
    TestEqual(TEXT("Large skeleton uses only referenced section bones"),Palette.Num(),3);
    const int32 Expected[]={287,260,12};
    for (int32 I=0;I<3;++I)
        TestEqual(TEXT("Section-local skin weight resolves to original skeleton bone"),
            int32(Palette[LOD.SkinWeightVertexBuffer.GetBoneIndex(I,0)]),Expected[I]);
    return true;
}
#endif
