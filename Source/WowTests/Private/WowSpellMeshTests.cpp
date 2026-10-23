#include "WowSpellMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Formats/M2Parser.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "WowSkeletalMeshBuilder.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WowCookedMaterial.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellMeshDeformationTest,"WowUnreal.Combat.Spells.AuthoredMeshMotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellMeshDeformationTest::RunTest(const FString& Parameters)
{
    FM2Data Data;
    FM2Bone Root; Root.PivotPoint=FVector(0,0,2); Data.Bones.Add(Root);
    FM2Bone Child; Child.PivotPoint=FVector(0,0,3); Child.ParentBone=0; Data.Bones.Add(Child);
    FM2Vertex V; V.Position=FVector(0,0,4); V.Normal=FVector::UpVector; V.BoneWeights[0]=255; V.BoneIndices[0]=1; Data.Vertices.Add(V);
    FM2AnimationData Clip; Clip.Duration=1000; Clip.BoneTracks.SetNum(2);
    Clip.BoneTracks[0].TransTimestamps={0,1000}; Clip.BoneTracks[0].TransValues={FVector::ZeroVector,FVector(0,0,2)}; Data.AnimationTracks.Add(Clip);
    TArray<FVector> P,N;
    UWowSpellMeshComponent::EvaluateVertices(Data,0,false,P,N);
    TestTrue(TEXT("Bind pose applies pivot only once"),P[0].Equals(FVector(0,0,400)));
    UWowSpellMeshComponent::EvaluateVertices(Data,.5f,false,P,N);
    TestTrue(TEXT("Child follows authored parent motion"),P[0].Equals(FVector(0,0,500)));
    UWowSpellMeshComponent::EvaluateVertices(Data,1.5f,false,P,N);
    TestTrue(TEXT("Impact does not restart"),P[0].Equals(FVector(0,0,600)));
    UWowSpellMeshComponent::EvaluateVertices(Data,1.5f,true,P,N);
    TestTrue(TEXT("Persistent effect repeats its authored animation"),P[0].Equals(FVector(0,0,500)));
    Data.Vertices[0].BoneIndices[0]=250;
    UWowSpellMeshComponent::EvaluateVertices(Data,.5f,false,P,N);
    TestTrue(TEXT("Invalid weights retain source position"),P[0].Equals(FVector(0,0,400)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellAttachmentTest,"WowUnreal.Combat.Spells.AuthoredSockets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellAttachmentTest::RunTest(const FString& Parameters)
{
    FM2Data Data; Data.bIsValid=true;
    FM2Bone Bone; Bone.PivotPoint=FVector(1,2,3); Data.Bones.Add(Bone);
    for(uint32 Id:{15u,17u,19u,20u})
    {
        FM2Attachment A; A.Id=Id; A.Bone=0; A.Position=FVector(1,2,4); Data.Attachments.Add(A);
    }
    USkeleton* Skeleton=FWowSkeletalMeshBuilder::CreateSkeleton(Data,TEXT("SpellAttachmentFixture"));
    if (!TestNotNull(TEXT("Skeleton"),Skeleton)) return false;
    for(const TCHAR* Name:{TEXT("WowSpellChest"),TEXT("WowSpellBreath"),TEXT("WowSpellBase"),TEXT("WowSpellHead")})
    {
        const USkeletalMeshSocket* Socket=Skeleton->FindSocket(FName(Name));
        if(TestNotNull(Name,Socket)) TestTrue(TEXT("Attachment subtracts model-space pivot"),Socket->RelativeLocation.Equals(FVector(0,0,100)));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPaladinOriginalMeshTest,"WowUnreal.Combat.Spells.PaladinOriginalMotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FPaladinOriginalMeshTest::RunTest(const FString& Parameters)
{
    FString Folder; FParse::Value(FCommandLine::Get(),TEXT("paladinvfxfixture="),Folder);
    if (Folder.IsEmpty()) { AddError(TEXT("Missing local Paladin fixture directory")); return false; }
    for(const TCHAR* Name:{TEXT("BlessingofMight_Base"),TEXT("sealofrighteous_impact_base"),TEXT("judgement_impact_chest"),TEXT("HolyLight_Low_Head")})
    {
        TArray<uint8> Model,Skin;
        if(!FFileHelper::LoadFileToArray(Model,*(Folder/(FString(Name)+TEXT(".m2")))) ||
           !FFileHelper::LoadFileToArray(Skin,*(Folder/(FString(Name)+TEXT("00.skin"))))) { AddError(FString(TEXT("Missing fixture "))+Name); continue; }
        const FM2Data Data=FM2Parser::Parse(Model,Skin);
        TestTrue(FString(Name)+TEXT(" valid"),Data.bIsValid && Data.HasAnimationData());
        TArray<FVector> Start,Now,Normals;
        UWowSpellMeshComponent::EvaluateVertices(Data,0,false,Start,Normals);
        float MaxDistance=0;
        for(float Time:{.15f,.5f,1.f})
        {
            UWowSpellMeshComponent::EvaluateVertices(Data,Time,false,Now,Normals);
            for(int32 I=0;I<Now.Num();++I)
            {
                if(Now[I].ContainsNaN()) { AddError(TEXT("Non-finite original vertex")); return false; }
                MaxDistance=FMath::Max(MaxDistance,float(FVector::Distance(Start[I],Now[I])));
            }
        }
        AddInfo(FString::Printf(TEXT("%s: %d vertices, authored displacement %.2f cm"),Name,Start.Num(),MaxDistance));
        TestTrue(FString(Name)+TEXT(" no longer frozen in bind pose"),MaxDistance>1.f);
        UStaticMesh* Materials=NewObject<UStaticMesh>();
        UMaterial* Base=WowCookedMaterial::Load(TEXT("M_WowM2Billboard"));
        TestNotNull(TEXT("Packaged glow shader is available"),Base);
        UMaterialInterface* SourceBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowSimpleObjectAdditive.M_WowSimpleObjectAdditive"));
        UTexture2D* Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        if(!TestNotNull(TEXT("Source effect material"),SourceBase) || !TestNotNull(TEXT("Test texture"),Texture)) return false;
        for(const auto& Pass:Data.RenderPasses)
        {
            UMaterialInstanceDynamic* Source=UMaterialInstanceDynamic::Create(SourceBase,Materials);
            Source->SetTextureParameterValue(TEXT("Layer0Texture"),Texture);
            Materials->GetStaticMaterials().Add(FStaticMaterial(Source));
        }
        UWowSpellMeshComponent* Component=NewObject<UWowSpellMeshComponent>();
        TestTrue(TEXT("Original sections build for CPU rendering"),Component->Initialize(MakeShared<FM2Data>(Data),Materials,false));
        TestEqual(TEXT("Every original render pass retained"),Component->GetNumSections(),Data.RenderPasses.Num());
        for(int32 I=0;I<Data.RenderPasses.Num();++I)
        {
            const auto& Pass=Data.RenderPasses[I]; if(Pass.BlendMode!=4) continue;
            UMaterialInstanceDynamic* MID=Cast<UMaterialInstanceDynamic>(Component->GetMaterial(I));
            if(!TestNotNull(TEXT("Per-instance animated material"),MID)) continue;
            float Alpha=-1;
            TestTrue(TEXT("Glow exposes opacity"),MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Opacity")),Alpha));
            const float Weight=Pass.TextureWeight.Values.IsEmpty()?1.f:Pass.TextureWeight.Values[0];
            const float ColorAlpha=Pass.ColorAlpha.Values.IsEmpty()?1.f:Pass.ColorAlpha.Values[0];
            TestTrue(TEXT("Initial fade follows original tracks"),FMath::IsNearlyEqual(Alpha,Weight*ColorAlpha));
        }
    }
    return true;
}
