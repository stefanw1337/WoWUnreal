#include "WowSkeletalMeshBuilder.h"
#include "WowPerformanceScope.h"
#include "WowRuntimeAnimSequence.h"
#include "Formats/M2Types.h"
#include "Formats/BlpParser.h"
#include "Formats/BlpTypes.h"
#include "WowTextureFactory.h"
#include "WowAssetCache.h"
#include "Mpq/MpqManager.h"
#include "Coord/WowCoordinate.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
// TODO: Fix UE 5.7 compatibility
// #include "IMeshBuilderModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowSkelMesh, Log, All);
void WowBuildRuntimeSkeletalMesh(USkeletalMesh*, const FM2Data&, const TArray<int32>&, const TArray<int32>&);

FName FWowSkeletalMeshBuilder::GetBoneName(int32 BoneIndex)
{
	return FName(*FString::Printf(TEXT("Bone_%03d"), BoneIndex));
}

USkeleton* FWowSkeletalMeshBuilder::CreateSkeleton(const FM2Data& Data, const FString& ModelName)
{
	if (Data.Bones.Num() == 0)
	{
		return nullptr;
	}

	USkeleton* Skeleton = NewObject<USkeleton>();

	const int32 NumBones = Data.Bones.Num();

	// UE requires bones in topological order (parent index < child index) with
	// exactly ONE root bone at index 0. M2 models may have multiple roots or
	// non-topological ordering, so we sort and fix parent references.
	TArray<int32> SortedOrder; // SortedOrder[new_index] = original_index
	TArray<int32> RemapTable;  // RemapTable[original_index] = new_index
	SortedOrder.Reserve(NumBones);
	RemapTable.SetNumUninitialized(NumBones);

	// Build children lists and find roots
	TArray<TArray<int32>> Children;
	Children.SetNum(NumBones);
	TArray<int32> Roots;
	TSet<int32> IsExtraRoot;

	for (int32 i = 0; i < NumBones; i++)
	{
		int32 Parent = Data.Bones[i].ParentBone;
		if (Parent >= 0 && Parent < NumBones && Parent != i)
		{
			Children[Parent].Add(i);
		}
		else
		{
			Roots.Add(i);
		}
	}

	// UE allows only one root bone. Reparent extra roots under the first.
	int32 PrimaryRoot = Roots.Num() > 0 ? Roots[0] : 0;
	for (int32 r = 1; r < Roots.Num(); r++)
	{
		IsExtraRoot.Add(Roots[r]);
		Children[PrimaryRoot].Add(Roots[r]);
	}

	// BFS from primary root (extra roots reachable via reparented edges)
	SortedOrder.Add(PrimaryRoot);
	int32 Head = 0;
	while (Head < SortedOrder.Num())
	{
		int32 Idx = SortedOrder[Head++];
		for (int32 Child : Children[Idx])
		{
			SortedOrder.Add(Child);
		}
	}

	// Add any unreachable orphans
	if (SortedOrder.Num() < NumBones)
	{
		TSet<int32> Added(SortedOrder);
		for (int32 i = 0; i < NumBones; i++)
		{
			if (!Added.Contains(i))
			{
				IsExtraRoot.Add(i);
				SortedOrder.Add(i);
			}
		}
	}

	for (int32 NewIdx = 0; NewIdx < NumBones; NewIdx++)
	{
		RemapTable[SortedOrder[NewIdx]] = NewIdx;
	}

	// Add bones in topological order using FReferenceSkeletonModifier
	{
		FReferenceSkeletonModifier SkelMod(Skeleton);
		for (int32 NewIdx = 0; NewIdx < NumBones; NewIdx++)
		{
			int32 OrigIdx = SortedOrder[NewIdx];
			const FM2Bone& Bone = Data.Bones[OrigIdx];
			FName BoneName = GetBoneName(OrigIdx);

			int32 ParentIdx = INDEX_NONE;
			if (OrigIdx == PrimaryRoot)
			{
				ParentIdx = INDEX_NONE;
			}
			else if (IsExtraRoot.Contains(OrigIdx))
			{
				ParentIdx = 0;
			}
			else
			{
				int32 OrigParent = Bone.ParentBone;
				if (OrigParent >= 0 && OrigParent < NumBones)
				{
					ParentIdx = RemapTable[OrigParent];
				}
				else
				{
					ParentIdx = 0;
				}
			}

			// WoW model space -> UE local: (Y, X, Z) * SCALE
			FVector Pivot(
				Bone.PivotPoint.Y * FWowCoordinate::SCALE,
				Bone.PivotPoint.X * FWowCoordinate::SCALE,
				Bone.PivotPoint.Z * FWowCoordinate::SCALE
			);

			FVector ParentPivot = FVector::ZeroVector;
			if (ParentIdx != INDEX_NONE)
			{
				int32 ParentOrigIdx = SortedOrder[ParentIdx];
				const FM2Bone& ParentBone = Data.Bones[ParentOrigIdx];
				ParentPivot = FVector(
					ParentBone.PivotPoint.Y * FWowCoordinate::SCALE,
					ParentBone.PivotPoint.X * FWowCoordinate::SCALE,
					ParentBone.PivotPoint.Z * FWowCoordinate::SCALE
				);
			}

			FTransform BoneTransform(FQuat::Identity, Pivot - ParentPivot, FVector::OneVector);

			FMeshBoneInfo BoneInfo;
			BoneInfo.Name = BoneName;
			BoneInfo.ParentIndex = ParentIdx;
			// UE 5.7: ExportName removed, Name is already an FName

			SkelMod.Add(BoneInfo, BoneTransform);
		}
	}

	// M2 key-bone 6 is the animated head. Expose a stable socket name so the
	// first-person camera can follow the correct anatomy for every race without
	// hard-coding a raw bone index.
	int32 HeadBoneIndex = INDEX_NONE;
	for (int32 BoneIndex = 0; BoneIndex < Data.Bones.Num(); ++BoneIndex)
	{
		if (Data.Bones[BoneIndex].KeyBoneId == 6)
		{
			HeadBoneIndex = BoneIndex;
			break;
		}
	}
	if (HeadBoneIndex != INDEX_NONE)
	{
		USkeletalMeshSocket* HeadSocket = NewObject<USkeletalMeshSocket>(Skeleton);
		HeadSocket->SocketName = TEXT("WowFirstPersonHead");
		HeadSocket->BoneName = GetBoneName(HeadBoneIndex);
		HeadSocket->bForceAlwaysAnimated = true;
		Skeleton->Sockets.Add(HeadSocket);
	}

	// SpellVisualKit addresses the character's palms by M2 attachment id.
	// Stable socket names let runtime spell effects follow the animated hands
	// without guessing raw bone indices (which differ between race/gender rigs).
	auto AddAttachmentSocket = [Skeleton, &Data](uint32 AttachmentId, const FName SocketName, bool bStableStaffBack = false)
	{
		const FM2Attachment* FoundAttachment = nullptr;
		if (Data.AttachmentLookup.IsValidIndex(static_cast<int32>(AttachmentId)))
		{
			const int16 AttachmentIndex = Data.AttachmentLookup[AttachmentId];
			if (Data.Attachments.IsValidIndex(AttachmentIndex))
			{
				FoundAttachment = &Data.Attachments[AttachmentIndex];
			}
		}
		if (!FoundAttachment)
		{
			FoundAttachment = Data.Attachments.FindByPredicate(
				[AttachmentId](const FM2Attachment& Attachment)
				{
					return Attachment.Id == AttachmentId;
				});
		}
		if (!FoundAttachment || !Data.Bones.IsValidIndex(FoundAttachment->Bone))
		{
			return;
		}

		USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Skeleton);
		Socket->SocketName = SocketName;
		Socket->BoneName = GetBoneName(FoundAttachment->Bone);
		// M2 attachment positions are stored in model space, while an Unreal
		// socket transform is bone-relative. Subtract the owning bone's model-
		// space pivot or the pivot is applied twice (weapons float behind/above
		// the character and hand spell effects land near the head).
		const FVector RelativeAttachment =
			FoundAttachment->Position - Data.Bones[FoundAttachment->Bone].PivotPoint;
		Socket->RelativeLocation = FVector(
			RelativeAttachment.Y * FWowCoordinate::SCALE,
			RelativeAttachment.X * FWowCoordinate::SCALE,
			RelativeAttachment.Z * FWowCoordinate::SCALE);
        if (bStableStaffBack)
        {
            // Freeze only the dedicated attachment bone in its authored Stand
            // pose. Its torso parent still animates normally during walk/run.
            const FM2Bone& Bone = Data.Bones[FoundAttachment->Bone];
            if (Data.Bones.IsValidIndex(Bone.ParentBone))
            {
                FVector Translation = Bone.PivotPoint - Data.Bones[Bone.ParentBone].PivotPoint;
                FQuat Rotation = FQuat::Identity;
                FVector Scale = FVector::OneVector;
                if (const FM2AnimationData* Stand = Data.AnimationTracks.FindByPredicate(
                    [](const FM2AnimationData& A) { return A.AnimationId == 0 && A.SubAnimationId == 0; }))
                {
                    if (Stand->BoneTracks.IsValidIndex(FoundAttachment->Bone))
                    {
                        const FM2BoneTrack& Track = Stand->BoneTracks[FoundAttachment->Bone];
                        if (!Track.TransValues.IsEmpty()) Translation += Track.TransValues[0];
                        if (!Track.RotValues.IsEmpty()) Rotation = Track.RotValues[0];
                        if (!Track.ScaleValues.IsEmpty()) Scale = Track.ScaleValues[0];
                    }
                }
                FTransform Idle(FQuat(Rotation.Y, Rotation.X, Rotation.Z, -Rotation.W).GetNormalized(),
                    FVector(Translation.Y, Translation.X, Translation.Z) * FWowCoordinate::SCALE,
                    FVector(Scale.Y, Scale.X, Scale.Z));
                Socket->BoneName = GetBoneName(Bone.ParentBone);
                Socket->RelativeLocation = Idle.TransformPosition(Socket->RelativeLocation);
                Socket->RelativeRotation = Idle.Rotator();
                Socket->RelativeScale = Idle.GetScale3D();
            }
        }
		Socket->bForceAlwaysAnimated = true;
		Skeleton->Sockets.Add(Socket);
	};
    AddAttachmentSocket(15, TEXT("WowSpellChest"));
    AddAttachmentSocket(17, TEXT("WowSpellBreath"));
    AddAttachmentSocket(19, TEXT("WowSpellBase"));
    AddAttachmentSocket(20, TEXT("WowSpellHead"));
	AddAttachmentSocket(1, TEXT("WowSpellRightHand"));
	AddAttachmentSocket(2, TEXT("WowSpellLeftHand"));
	AddAttachmentSocket(0, TEXT("WowMountSeat"));
	AddAttachmentSocket(26, TEXT("WowSheathMainHand"));
	AddAttachmentSocket(27, TEXT("WowSheathOffHand"));
	AddAttachmentSocket(28, TEXT("WowSheathShield"));
    AddAttachmentSocket(30, TEXT("WowStaffBackLeft"), true);
    AddAttachmentSocket(31, TEXT("WowStaffBackRight"), true);
	AddAttachmentSocket(30, TEXT("WowSheathLargeWeaponLeft"));
	AddAttachmentSocket(31, TEXT("WowSheathLargeWeaponRight"));
	AddAttachmentSocket(32, TEXT("WowSheathHipWeaponLeft"));
	AddAttachmentSocket(33, TEXT("WowSheathHipWeaponRight"));
    // Every attachment point by number, for spell model attachments (SpellVisualKitModelAttach:
    // wings on the back are attachment 12; r262).
    for (int32 AttachmentId = 0; AttachmentId <= 56; ++AttachmentId)
        AddAttachmentSocket(AttachmentId, FName(*FString::Printf(TEXT("WowAttach_%d"), AttachmentId)));

	UE_LOG(LogWowSkelMesh, Log, TEXT("Created skeleton '%s' with %d bones (%d roots, primary=%d)"),
		*ModelName, NumBones, Roots.Num(), PrimaryRoot);
	return Skeleton;
}

USkeletalMesh* FWowSkeletalMeshBuilder::CreateSkeletalMesh(const FM2Data& Data, USkeleton* Skeleton,
	const FString& ModelName, FMpqManager* Mpq, FWowAssetCache* Cache,
	TArray<FGeosetSectionInfo>* OutGeosetInfo,
	const TMap<uint16, uint16>* VisibleGeosets)
{
    FWowPerformanceScope Timing(TEXT("skeletal-mesh-build"), ModelName);
	if (!Skeleton || Data.Vertices.Num() == 0 || Data.Indices.Num() == 0)
	{
		return nullptr;
	}

	const int32 NumVerts = Data.Vertices.Num();
	const int32 NumTris = Data.Indices.Num() / 3;
	const int32 NumBones = Data.Bones.Num();

	// Rebuild the same bone remap table used in CreateSkeleton (M2 index → sorted index)
	TArray<int32> BoneRemapTable;
	BoneRemapTable.SetNumUninitialized(NumBones);
	{
		TArray<int32> SortedOrder;
		SortedOrder.Reserve(NumBones);
		TArray<TArray<int32>> BoneChildren;
		BoneChildren.SetNum(NumBones);
		TArray<int32> BoneRoots;

		for (int32 i = 0; i < NumBones; i++)
		{
			int32 Parent = Data.Bones[i].ParentBone;
			if (Parent >= 0 && Parent < NumBones && Parent != i)
				BoneChildren[Parent].Add(i);
			else
				BoneRoots.Add(i);
		}

		// Reparent extra roots under primary root (same logic as CreateSkeleton)
		int32 PrimaryRoot = BoneRoots.Num() > 0 ? BoneRoots[0] : 0;
		for (int32 r = 1; r < BoneRoots.Num(); r++)
			BoneChildren[PrimaryRoot].Add(BoneRoots[r]);

		SortedOrder.Add(PrimaryRoot);
		int32 BfsHead = 0;
		while (BfsHead < SortedOrder.Num())
		{
			int32 Idx = SortedOrder[BfsHead++];
			for (int32 Child : BoneChildren[Idx])
				SortedOrder.Add(Child);
		}

		if (SortedOrder.Num() < NumBones)
		{
			TSet<int32> Added(SortedOrder);
			for (int32 i = 0; i < NumBones; i++)
				if (!Added.Contains(i))
					SortedOrder.Add(i);
		}

		for (int32 NewIdx = 0; NewIdx < NumBones; NewIdx++)
			BoneRemapTable[SortedOrder[NewIdx]] = NewIdx;
	}

	USkeletalMesh* SkelMesh = NewObject<USkeletalMesh>();
	SkelMesh->SetSkeleton(Skeleton);
	SkelMesh->SetRefSkeleton(Skeleton->GetReferenceSkeleton());

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();

	// LOD info
	FSkeletalMeshLODInfo& LODInfo = SkelMesh->AddLODInfo();
	LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;

	// UE 5.7: Create FMeshDescription locally, then move it to skeletal mesh
	FMeshDescription MeshDesc;
	// Register skeletal mesh attributes
	FSkeletalMeshAttributes SkelAttrs(MeshDesc);
	SkelAttrs.Register();

	// Build list of render passes to include (filter by geoset visibility if provided).
	const int32 NumRenderPasses = Data.RenderPasses.Num();
	const bool bHasRenderPasses = NumRenderPasses > 0 && Data.Submeshes.Num() > 0;

	// Determine which render passes are visible
	TArray<int32> VisiblePasses;
	if (bHasRenderPasses)
	{
		for (int32 i = 0; i < NumRenderPasses; ++i)
		{
			const FM2RenderPass& Pass = Data.RenderPasses[i];
			if (Pass.SubmeshIndex >= static_cast<uint16>(Data.Submeshes.Num())) continue;

			bool bInclude = true;

			// r308 (Stefan: the voidwalker's bracers hung behind and above its arms): passes whose
			// transparency or colour alpha is 0 throughout the Stand sequence are invisible in the
			// original client (the voidwalker's two shoulder-height pieces on bones 3/4 are keyed 0
			// in every sequence). Drawing them left loose parts floating beside the model.
			auto StaticallyHidden = [](const FM2MaterialTrack& Track)
			{
				if (Track.Values.IsEmpty()) return false;
				for (const float Value : Track.Values) if (Value > 0.004f) return false;
				return true;
			};
			if (StaticallyHidden(Pass.TextureWeight) || StaticallyHidden(Pass.ColorAlpha))
			{
				bInclude = false;
			}

			// Skip hair-textured passes (type 6) in the body mesh — hair gets its own mesh
			if (VisibleGeosets && Pass.TextureIndex < static_cast<uint16>(Data.TextureTypes.Num()) &&
				Data.TextureTypes[Pass.TextureIndex] == 6)
			{
				bInclude = false;
			}

			if (bInclude && VisibleGeosets)
			{
				uint16 GeosetId = Data.Submeshes[Pass.SubmeshIndex].Id;
				uint16 Group = GeosetId / 100;
				uint16 Variant = GeosetId % 100;

				const uint16* DesiredVariant = VisibleGeosets->Find(Group);
				if (DesiredVariant)
				{
					// GeosetId 0 = base body mesh, always include
					bInclude = (GeosetId == 0) || (Variant == *DesiredVariant);
				}
				// Groups not in rules: include by default (creatures, etc.)
			}
			if (bInclude)
			{
				VisiblePasses.Add(i);
			}
		}
	}
	else
	{
		VisiblePasses.Add(0); // Single section for models without render passes
	}

	// r613 (Stefan: the HD pack's login screen - a backported Shadowlands/Icecrown glue scene - showed
	// the sky and shard cards over the Icecrown gate walls): a glue scene is a stack of see-through
	// cards with no depth writes, which the client draws in the texture units' priority-plane order
	// (sky -65 ... walls -30 ... vignette +10). UE draws a mesh's translucent sections in section order,
	// so the passes are sorted by priority plane (stable: equal planes keep the file order). Only glue
	// models (Interface\Glues\...), whose sections nothing else indexes by pass number.
	if (bHasRenderPasses && ModelName.Contains(TEXT("Glues"), ESearchCase::IgnoreCase))
		VisiblePasses.StableSort([&Data](int32 A, int32 B)
			{ return Data.RenderPasses[A].PriorityPlane < Data.RenderPasses[B].PriorityPlane; });
	const int32 NumSections = VisiblePasses.Num();
	TArray<FPolygonGroupID> PolyGroups;
	PolyGroups.SetNum(NumSections);
	auto PolygonGroupMaterialSlotNames = SkelAttrs.GetPolygonGroupMaterialSlotNames();
	for (int32 i = 0; i < NumSections; ++i)
	{
		PolyGroups[i] = MeshDesc.CreatePolygonGroup();
		// CommitMeshDescription resolves skeletal sections through the imported
		// material slot name. Without this attribute every polygon group is
		// assigned to slot zero, so multi-texture M2s render one texture over
		// the entire model (especially visible in character-select ModelFFX).
		PolygonGroupMaterialSlotNames[PolyGroups[i]] =
			FName(*FString::Printf(TEXT("Section_%d"), i));
	}

	// Map from visible pass index to original render pass index
	// Output geoset info for each included section
	if (OutGeosetInfo)
	{
		OutGeosetInfo->SetNum(NumSections);
		for (int32 i = 0; i < NumSections; ++i)
		{
			FGeosetSectionInfo& Info = (*OutGeosetInfo)[i];
			Info.SectionIndex = i;
			if (bHasRenderPasses)
			{
				int32 OrigPassIdx = VisiblePasses[i];
                Info.RenderPassIndex = OrigPassIdx;
				const FM2RenderPass& Pass = Data.RenderPasses[OrigPassIdx];
				if (Pass.SubmeshIndex < static_cast<uint16>(Data.Submeshes.Num()))
				{
					Info.GeosetId = Data.Submeshes[Pass.SubmeshIndex].Id;
					Info.GeosetGroup = Info.GeosetId / 100;
					Info.GeosetVariant = Info.GeosetId % 100;
					Info.TextureIndex = Pass.TextureIndex;
					Info.BlendMode = Pass.BlendMode;
                    Info.UVScrollSpeed = Pass.UVScrollSpeed;
					Info.RenderFlags = Pass.Flags;
				}
			}
		}
	}

	if (WowUseRuntimeM2Import())
	{
		WowBuildRuntimeSkeletalMesh(SkelMesh, Data, BoneRemapTable, VisiblePasses);
		return SkelMesh;
	}

	MeshDesc.ReserveNewVertices(NumVerts);
	MeshDesc.ReserveNewVertexInstances(NumVerts);
	MeshDesc.ReserveNewPolygons(NumTris);
	MeshDesc.ReserveNewEdges(Data.Indices.Num());

	TVertexAttributesRef<FVector3f> VertexPositions = SkelAttrs.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexNormals = SkelAttrs.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> VertexTangents = SkelAttrs.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BinormalSigns = SkelAttrs.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> VertexUVs = SkelAttrs.GetVertexInstanceUVs();
	VertexUVs.SetNumChannels(1);

	FSkinWeightsVertexAttributesRef SkinWeightsRef = SkelAttrs.GetVertexSkinWeights();

	TArray<FVertexInstanceID> VertexInstanceIDs;
	VertexInstanceIDs.SetNum(NumVerts);

	for (int32 i = 0; i < NumVerts; ++i)
	{
		const FM2Vertex& V = Data.Vertices[i];
		FVertexID VertID = MeshDesc.CreateVertex();

		VertexPositions[VertID] = FVector3f(
			V.Position.Y * FWowCoordinate::SCALE,
			V.Position.X * FWowCoordinate::SCALE,
			V.Position.Z * FWowCoordinate::SCALE
		);

		TArray<UE::AnimationCore::FBoneWeight> BoneWeightArray;
		for (int32 j = 0; j < 4; ++j)
		{
			if (V.BoneWeights[j] <= 0)
			{
				continue;
			}

			const int32 OrigBoneIdx = V.BoneIndices[j];
			const int32 BoneIndex = (OrigBoneIdx >= 0 && OrigBoneIdx < NumBones) ? BoneRemapTable[OrigBoneIdx] : 0;
			BoneWeightArray.Add(UE::AnimationCore::FBoneWeight(static_cast<FBoneIndexType>(BoneIndex), V.BoneWeights[j] / 255.0f));
		}

		if (BoneWeightArray.Num() == 0)
		{
			BoneWeightArray.Add(UE::AnimationCore::FBoneWeight(0, 1.0f));
		}

		SkinWeightsRef.Set(VertID, UE::AnimationCore::FBoneWeights::Create(BoneWeightArray));

		FVertexInstanceID InstID = MeshDesc.CreateVertexInstance(VertID);
		VertexInstanceIDs[i] = InstID;

		FVector3f Normal(V.Normal.Y, V.Normal.X, V.Normal.Z);
		Normal.Normalize();
		VertexNormals[InstID] = Normal;

		FVector3f Tangent = FVector3f::CrossProduct(Normal, FVector3f(0, 1, 0));
		if (Tangent.SizeSquared() < 0.001f)
		{
			Tangent = FVector3f::CrossProduct(Normal, FVector3f(1, 0, 0));
		}
		Tangent.Normalize();
		VertexTangents[InstID] = Tangent;
		BinormalSigns[InstID] = 1.0f;
		VertexUVs.Set(InstID, 0, FVector2f(V.TexCoord.X, V.TexCoord.Y));
	}

	// Build triangles per-section. Only include render passes that are in VisiblePasses.
	if (bHasRenderPasses)
	{
		for (int32 SectionIdx = 0; SectionIdx < VisiblePasses.Num(); ++SectionIdx)
		{
			int32 OrigPassIdx = VisiblePasses[SectionIdx];
			const FM2RenderPass& Pass = Data.RenderPasses[OrigPassIdx];
			if (Pass.SubmeshIndex >= static_cast<uint16>(Data.Submeshes.Num()))
			{
				continue;
			}

			const FM2Submesh& Submesh = Data.Submeshes[Pass.SubmeshIndex];
			const int32 TriStart = Submesh.StartTriangle / 3;
			const int32 TriCount = Submesh.NumTriangles / 3;

			for (int32 t = 0; t < TriCount; ++t)
			{
				const int32 TriIdx = TriStart + t;
				if (TriIdx * 3 + 2 >= Data.Indices.Num())
				{
					break;
				}

				TArray<FVertexInstanceID> TriVerts;
				TriVerts.Reserve(3);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const int32 VertexIndex = Data.Indices[TriIdx * 3 + Corner];
					if (VertexInstanceIDs.IsValidIndex(VertexIndex))
					{
						TriVerts.Add(VertexInstanceIDs[VertexIndex]);
					}
				}

				if (TriVerts.Num() == 3)
				{
					MeshDesc.CreatePolygon(PolyGroups[SectionIdx], TriVerts);
				}
			}
		}
	}
	else
	{
		// Fallback: single polygon group for all triangles
		for (int32 i = 0; i < NumTris; ++i)
		{
			TArray<FVertexInstanceID> TriVerts;
			TriVerts.Reserve(3);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 VertexIndex = Data.Indices[i * 3 + Corner];
				if (VertexInstanceIDs.IsValidIndex(VertexIndex))
				{
					TriVerts.Add(VertexInstanceIDs[VertexIndex]);
				}
			}

			if (TriVerts.Num() == 3)
			{
				MeshDesc.CreatePolygon(PolyGroups[0], TriVerts);
			}
		}
	}

	SkelAttrs.ReserveNewBones(RefSkel.GetNum());
	auto BoneNames = SkelAttrs.GetBoneNames();
	auto BoneParentIndices = SkelAttrs.GetBoneParentIndices();
	auto BonePoses = SkelAttrs.GetBonePoses();
	for (int32 BoneIndex = 0; BoneIndex < RefSkel.GetNum(); ++BoneIndex)
	{
		FBoneID BoneID = SkelAttrs.CreateBone();
		BoneNames[BoneID] = RefSkel.GetBoneName(BoneIndex);
		BoneParentIndices[BoneID] = RefSkel.GetParentIndex(BoneIndex);
		BonePoses[BoneID] = RefSkel.GetRefBonePose()[BoneIndex];
	}

	// Materials MUST be set BEFORE Build(). Each section needs a UNIQUE material instance
	// to prevent UE5 from merging sections during Build() (which breaks per-section overrides).
	{
		UMaterial* DefaultMat = UMaterial::GetDefaultMaterial(MD_Surface);
		SkelMesh->GetMaterials().Reset();
		for (int32 i = 0; i < NumSections; ++i)
		{
			FName SlotName = FName(*FString::Printf(TEXT("Section_%d"), i));
			// Create unique MID per section so Build() keeps them separate
			UMaterialInstanceDynamic* UniqueMat = UMaterialInstanceDynamic::Create(DefaultMat, SkelMesh,
				FName(*FString::Printf(TEXT("SkelMat_%d"), i)));
			SkelMesh->GetMaterials().Add(FSkeletalMaterial(UniqueMat, true, false, SlotName));
		}
	}

	// Set bounds BEFORE Build() so the build pipeline has valid bounds data.
	// M2 vertex transform convention: UE = (WoW.Y, WoW.X, WoW.Z) * SCALE
	const FVector& BMin = Data.BoundingBox.Min;
	const FVector& BMax = Data.BoundingBox.Max;
	FBox MeshBox(
		FVector(FMath::Min(BMin.Y, BMax.Y) * FWowCoordinate::SCALE,
				FMath::Min(BMin.X, BMax.X) * FWowCoordinate::SCALE,
				FMath::Min(BMin.Z, BMax.Z) * FWowCoordinate::SCALE),
		FVector(FMath::Max(BMin.Y, BMax.Y) * FWowCoordinate::SCALE,
				FMath::Max(BMin.X, BMax.X) * FWowCoordinate::SCALE,
				FMath::Max(BMin.Z, BMax.Z) * FWowCoordinate::SCALE)
	);
	FBoxSphereBounds Bounds(MeshBox);
	SkelMesh->SetImportedBounds(Bounds);

#if WITH_EDITOR
	// UE 5.7: Use mesh description and editor build pipeline to generate runtime render data.
	if (FSkeletalMeshLODInfo* LOD0Info = SkelMesh->GetLODInfo(0))
	{
		LOD0Info->BuildSettings.bRecomputeNormals = false;
		LOD0Info->BuildSettings.bRecomputeTangents = true;
		LOD0Info->BuildSettings.bUseMikkTSpace = false;
	}

	// CommitMeshDescription expects an imported-model slot for the target LOD.
	FSkeletalMeshModel* ImportedModel = SkelMesh->GetImportedModel();
	if (ImportedModel && !ImportedModel->LODModels.IsValidIndex(0))
	{
		ImportedModel->LODModels.Add(new FSkeletalMeshLODModel());
	}

	SkelMesh->CreateMeshDescription(0, MoveTemp(MeshDesc));
	SkelMesh->CommitMeshDescription(0);

	// Build() converts mesh description to render data and initializes GPU resources.
	SkelMesh->CalculateInvRefMatrices();
	SkelMesh->Build();
#else
	// Non-editor game targets in UE 5.7 do not expose the skeletal mesh import/build APIs.
	// The verified runtime path uses the editor target (`UnrealEditor -game`), where the
	// full mesh build above is available.
	UE_LOG(LogWowSkelMesh, Warning, TEXT("Skipping skeletal mesh build for non-editor target '%s'"), *ModelName);
#endif

	UE_LOG(LogWowSkelMesh, Log, TEXT("Created skeletal mesh '%s' with %d verts, %d tris, %d bones"),
		*ModelName, NumVerts, NumTris, NumBones);

	return SkelMesh;
}

USkeletalMesh* FWowSkeletalMeshBuilder::CreateSkeletalMeshByTextureType(const FM2Data& Data,
	USkeleton* Skeleton, const FString& ModelName, FMpqManager* Mpq, FWowAssetCache* Cache,
	uint32 TextureType, const TMap<uint16, uint16>* VisibleGeosets)
{
	// Build a filter that only includes render passes matching the requested texture type
	// AND matching geoset visibility rules
	TMap<uint16, uint16> TextureTypeFilter;

	// We pass a special VisibleGeosets that allows ALL geosets but we filter render passes
	// by texture type in a custom pass filter
	// Actually, let's reuse CreateSkeletalMesh with a custom approach:
	// Create a modified M2Data that only has render passes for the desired texture type

	FM2Data FilteredData = Data; // Copy
	FilteredData.RenderPasses.Reset();

	for (const FM2RenderPass& Pass : Data.RenderPasses)
	{
		// Only include passes that reference the desired texture type
		if (Pass.TextureIndex < static_cast<uint16>(Data.TextureTypes.Num()) &&
			Data.TextureTypes[Pass.TextureIndex] == TextureType)
		{
			// Also check geoset visibility
			if (VisibleGeosets && Pass.SubmeshIndex < static_cast<uint16>(Data.Submeshes.Num()))
			{
				uint16 GeosetId = Data.Submeshes[Pass.SubmeshIndex].Id;
				uint16 Group = GeosetId / 100;
				uint16 Variant = GeosetId % 100;
				const uint16* DesiredVariant = VisibleGeosets->Find(Group);
				if (DesiredVariant)
				{
					if (GeosetId != 0 && Variant != *DesiredVariant) continue;
				}
			}
			FilteredData.RenderPasses.Add(Pass);
		}
	}

	if (FilteredData.RenderPasses.Num() == 0)
	{
		return nullptr;
	}

	return CreateSkeletalMesh(FilteredData, Skeleton, ModelName, Mpq, Cache, nullptr, nullptr);
}

TArray<UAnimSequence*> FWowSkeletalMeshBuilder::CreateAnimations(const FM2Data& Data,
	USkeleton* Skeleton, const FString& ModelName, const TSet<int32>* AnimationIds,
	TArray<int32>* OutAnimationIds, bool bFirstVariantOnly)
{
    FWowPerformanceScope Timing(TEXT("animation-import"), ModelName);
	if (OutAnimationIds)
	{
		OutAnimationIds->Reset();
	}

	auto SampleVectorTrack = [](const TArray<uint32>& Times, const TArray<FVector>& Values,
		float TimeMs, float DurationMs, bool bLooping, const FVector& DefaultValue)
	{
		const int32 Count = FMath::Min(Times.Num(), Values.Num());
		if (Count <= 0) return DefaultValue;
		if (Count == 1 || TimeMs <= static_cast<float>(Times[0])) return Values[0];

		for (int32 Index = 1; Index < Count; ++Index)
		{
			if (TimeMs <= static_cast<float>(Times[Index]))
			{
				const float Span = static_cast<float>(Times[Index] - Times[Index - 1]);
				const float Alpha = Span > SMALL_NUMBER
					? FMath::Clamp((TimeMs - Times[Index - 1]) / Span, 0.0f, 1.0f) : 0.0f;
				return FMath::Lerp(Values[Index - 1], Values[Index], Alpha);
			}
		}

		if (bLooping && DurationMs > static_cast<float>(Times[Count - 1]))
		{
			const float Span = DurationMs - Times[Count - 1] + Times[0];
			const float Alpha = Span > SMALL_NUMBER
				? FMath::Clamp((TimeMs - Times[Count - 1]) / Span, 0.0f, 1.0f) : 1.0f;
			return FMath::Lerp(Values[Count - 1], Values[0], Alpha);
		}
		return Values[Count - 1];
	};

	auto SampleRotationTrack = [](const TArray<uint32>& Times, const TArray<FQuat>& Values,
		float TimeMs, float DurationMs, bool bLooping, const FQuat& DefaultValue)
	{
		const int32 Count = FMath::Min(Times.Num(), Values.Num());
		if (Count <= 0) return DefaultValue;
		if (Count == 1 || TimeMs <= static_cast<float>(Times[0])) return Values[0];

		for (int32 Index = 1; Index < Count; ++Index)
		{
			if (TimeMs <= static_cast<float>(Times[Index]))
			{
				const float Span = static_cast<float>(Times[Index] - Times[Index - 1]);
				const float Alpha = Span > SMALL_NUMBER
					? FMath::Clamp((TimeMs - Times[Index - 1]) / Span, 0.0f, 1.0f) : 0.0f;
				return FQuat::Slerp(Values[Index - 1], Values[Index], Alpha).GetNormalized();
			}
		}

		if (bLooping && DurationMs > static_cast<float>(Times[Count - 1]))
		{
			const float Span = DurationMs - Times[Count - 1] + Times[0];
			const float Alpha = Span > SMALL_NUMBER
				? FMath::Clamp((TimeMs - Times[Count - 1]) / Span, 0.0f, 1.0f) : 1.0f;
			return FQuat::Slerp(Values[Count - 1], Values[0], Alpha).GetNormalized();
		}
		return Values[Count - 1];
	};

	TArray<UAnimSequence*> Result;
	int32 TotalRepairedAnimationKeys = 0;

	if (!Skeleton || !Data.HasAnimationData())
	{
		return Result;
	}

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	const int32 NumBones = RefSkel.GetNum();

	// UE needs one root, so CreateSkeleton hangs every extra M2 root bone under the first
	// root. In WoW those bones are independent: their keys must cancel the first root's
	// animation, otherwise they follow it (centaur bodies drifting while idle, odd walks).
	int32 PrimaryRootBone = INDEX_NONE;
	TSet<int32> ExtraRootBones;
	for (int32 BoneIndex = 0; BoneIndex < Data.Bones.Num(); ++BoneIndex)
	{
		const int32 Parent = Data.Bones[BoneIndex].ParentBone;
		if (Parent >= 0 && Parent < Data.Bones.Num() && Parent != BoneIndex) continue;
		if (PrimaryRootBone == INDEX_NONE) PrimaryRootBone = BoneIndex;
		else ExtraRootBones.Add(BoneIndex);
	}
	auto PivotUE = [&Data](int32 BoneIndex)
	{
		const FVector& P = Data.Bones[BoneIndex].PivotPoint;
		return FVector(P.Y * FWowCoordinate::SCALE, P.X * FWowCoordinate::SCALE, P.Z * FWowCoordinate::SCALE);
	};

	TSet<int32> ImportedIds;
	// r482 (Stefan: mounts never stand still): one clip per animation id takes the variation the
	// client plays most often, not simply the first one. RidingElekkElite's Stand 0-0 is a 10 %
	// fidget (head and trunk swing) and 0-1 the 90 % calm stand; looping 0-0 kept the elekk moving.
	TMap<int32, TPair<int32, int32>> MainVariant; // id -> (sub id, probability)
	if (bFirstVariantOnly)
		for (const FM2AnimSequence& Sequence : Data.Animations)
		{
			if (!Data.AnimationTracks.ContainsByPredicate([&Sequence](const FM2AnimationData& Clip)
				{ return Clip.AnimationId == Sequence.AnimationId && Clip.SubAnimationId == Sequence.SubAnimationId
					&& Clip.Duration > 0 && Clip.BoneTracks.Num() > 0; })) continue;
			TPair<int32, int32>* Best = MainVariant.Find(Sequence.AnimationId);
			if (!Best) MainVariant.Add(Sequence.AnimationId, TPair<int32, int32>(Sequence.SubAnimationId, Sequence.Probability));
			else if (Sequence.Probability > Best->Value) *Best = TPair<int32, int32>(Sequence.SubAnimationId, Sequence.Probability);
		}
	for (const FM2AnimationData& AnimData : Data.AnimationTracks)
	{
		if (AnimData.Duration == 0 || AnimData.BoneTracks.Num() == 0)
		{
			continue;
		}
		if (AnimationIds && !AnimationIds->Contains(AnimData.AnimationId))
		{
			continue;
		}

        if (bFirstVariantOnly && ImportedIds.Contains(AnimData.AnimationId)) continue;
        if (const TPair<int32, int32>* Main = bFirstVariantOnly ? MainVariant.Find(AnimData.AnimationId) : nullptr;
            Main && Main->Key != AnimData.SubAnimationId) continue;
        ImportedIds.Add(AnimData.AnimationId);

		FString AnimName = FString::Printf(TEXT("%s_Anim%d_%d"), *ModelName, AnimData.AnimationId, AnimData.SubAnimationId);

		UWowRuntimeAnimSequence* Runtime = WowUseRuntimeM2Import() ? NewObject<UWowRuntimeAnimSequence>() : nullptr;
		UAnimSequence* AnimSeq = Runtime ? Runtime : NewObject<UAnimSequence>();
		AnimSeq->SetSkeleton(Skeleton);

		// r461 (Stefan: "the grass on the tauren scene is pulsating"): bone tracks on a global
		// sequence (grass sway 6.7-12 s, windmills, banners) were baked into the clip's own length
		// (UI_Tauren Stand: 1 s), so every second the sway snapped back to its start. Static scenes
		// and doodads now bake a looping clip long enough for the longest global sequence (a whole
		// number of the clip's own length, at most 12 s), and each global track plays a whole
		// number of its cycles in it (its speed changes slightly instead of jumping at the loop).
		// Characters and creatures keep their clip lengths (gameplay timing).
		uint32 MaxGlobal = 0;
		for (const FM2BoneTrack& GlobalTrack : AnimData.BoneTracks)
			MaxGlobal = FMath::Max3(MaxGlobal, FMath::Max(GlobalTrack.TransGlobalDuration, GlobalTrack.RotGlobalDuration),
				GlobalTrack.ScaleGlobalDuration);
		const bool bTimingModel = ModelName.StartsWith(TEXT("Character"), ESearchCase::IgnoreCase)
			|| ModelName.StartsWith(TEXT("Creature"), ESearchCase::IgnoreCase)
			|| ModelName.StartsWith(TEXT("HDX"), ESearchCase::IgnoreCase);
		const bool bFitGlobals = AnimData.bIsLooping && MaxGlobal > 0 && !bTimingModel;
		const float ClipMs = static_cast<float>(AnimData.Duration);
		const float BakeMs = bFitGlobals && MaxGlobal > AnimData.Duration
			? ClipMs * FMath::Max(1, FMath::CeilToInt32(FMath::Min(float(MaxGlobal), FMath::Max(12000.0f, ClipMs)) / ClipMs))
			: ClipMs;
		auto TrackMs = [&](float TimeMs, uint32 Global)
		{
			if (!Global) return BakeMs > ClipMs ? FMath::Fmod(TimeMs, ClipMs) : TimeMs;
			if (!bFitGlobals) return FMath::Fmod(TimeMs, float(Global));
			const float Period = BakeMs / FMath::Max(1.0f, FMath::RoundToFloat(BakeMs / Global));
			return FMath::Fmod(TimeMs, Period) * float(Global) / Period;
		};
		const float DurationSec = BakeMs / 1000.0f;

        const int32 NumFrames = FMath::Max(1, FMath::CeilToInt32(DurationSec * 30.0f));
        if (Runtime) Runtime->InitializeRuntime(Skeleton, NumFrames);
#if WITH_EDITOR
        IAnimationDataController* Controller = Runtime ? nullptr : &AnimSeq->GetController();
        if (Controller)
        {
            Controller->InitializeModel();
            Controller->OpenBracket(FText::FromString(TEXT("ImportM2Anim")));
            Controller->SetFrameRate(FFrameRate(30, 1));
            Controller->SetNumberOfFrames(NumFrames);
        }
#endif

		const int32 TrackCount = FMath::Min(AnimData.BoneTracks.Num(), NumBones);
		// Component-space transform of the primary root per sample (for extra-root correction).
		TArray<FTransform> PrimaryRootPose;
		const bool bPrimaryRootAnimated = PrimaryRootBone != INDEX_NONE && !ExtraRootBones.IsEmpty()
			&& AnimData.BoneTracks.IsValidIndex(PrimaryRootBone) && !AnimData.BoneTracks[PrimaryRootBone].IsEmpty();
		if (bPrimaryRootAnimated)
		{
			const FM2BoneTrack& RootTrack = AnimData.BoneTracks[PrimaryRootBone];
			const FVector RootPivot = PivotUE(PrimaryRootBone);
			PrimaryRootPose.SetNum(NumFrames + 1);
			for (int32 k = 0; k <= NumFrames; ++k)
			{
				const float TimeMs = AnimData.bIsLooping && k == NumFrames
					? 0.0f : FMath::Min(BakeMs, k * (1000.0f / 30.0f));
				FVector T = SampleVectorTrack(RootTrack.TransTimestamps, RootTrack.TransValues,
					TrackMs(TimeMs, RootTrack.TransGlobalDuration),
					RootTrack.TransGlobalDuration ? RootTrack.TransGlobalDuration : AnimData.Duration,
					RootTrack.TransGlobalDuration != 0 || AnimData.bIsLooping, FVector::ZeroVector);
				FQuat R = SampleRotationTrack(RootTrack.RotTimestamps, RootTrack.RotValues,
					TrackMs(TimeMs, RootTrack.RotGlobalDuration),
					RootTrack.RotGlobalDuration ? RootTrack.RotGlobalDuration : AnimData.Duration,
					RootTrack.RotGlobalDuration != 0 || AnimData.bIsLooping, FQuat::Identity);
				FVector S = SampleVectorTrack(RootTrack.ScaleTimestamps, RootTrack.ScaleValues,
					TrackMs(TimeMs, RootTrack.ScaleGlobalDuration),
					RootTrack.ScaleGlobalDuration ? RootTrack.ScaleGlobalDuration : AnimData.Duration,
					RootTrack.ScaleGlobalDuration != 0 || AnimData.bIsLooping, FVector::OneVector);
				if (T.ContainsNaN() || T.GetAbsMax() > 10000.0f) T = FVector::ZeroVector;
				if (R.ContainsNaN() || R.SizeSquared() <= SMALL_NUMBER) R = FQuat::Identity; else R.Normalize();
				if (S.ContainsNaN() || S.GetAbsMax() > 1000.0f || S.GetMin() <= KINDA_SMALL_NUMBER) S = FVector::OneVector;
				PrimaryRootPose[k] = FTransform(FQuat(R.Y, R.X, R.Z, -R.W).GetNormalized(),
					RootPivot + FVector(T.Y, T.X, T.Z) * FWowCoordinate::SCALE, S);
			}
		}
		for (int32 BoneIdx = 0; BoneIdx < TrackCount; ++BoneIdx)
		{
			const FM2BoneTrack& Track = AnimData.BoneTracks[BoneIdx];
			const bool bCorrectExtraRoot = bPrimaryRootAnimated && ExtraRootBones.Contains(BoneIdx);
			if (Track.IsEmpty() && !bCorrectExtraRoot)
			{
				continue;
			}

			FName BoneName = GetBoneName(BoneIdx);
			const int32 RefBoneIndex = RefSkel.FindBoneIndex(BoneName);
			const FTransform BaseTransform = RefBoneIndex != INDEX_NONE ? RefSkel.GetRefBonePose()[RefBoneIndex] : FTransform::Identity;
			const FVector SafeBaseTranslation = BaseTransform.GetTranslation().ContainsNaN()
				? FVector::ZeroVector : BaseTransform.GetTranslation();
			FQuat SafeBaseRotation = BaseTransform.GetRotation();
			if (SafeBaseRotation.ContainsNaN() || SafeBaseRotation.SizeSquared() <= SMALL_NUMBER)
			{
				SafeBaseRotation = FQuat::Identity;
			}
			else
			{
				SafeBaseRotation.Normalize();
			}
			const FVector SafeBaseScale = BaseTransform.GetScale3D().ContainsNaN()
				? FVector::OneVector : BaseTransform.GetScale3D();
#if WITH_EDITOR
			if (Controller) Controller->AddBoneCurve(BoneName);
#endif

			TArray<FVector3f> PosKeys;
			TArray<FQuat4f> RotKeys;
			TArray<FVector3f> ScaleKeys;
			const int32 NumSampleKeys = NumFrames + 1;
			PosKeys.SetNum(NumSampleKeys);
			RotKeys.SetNum(NumSampleKeys);
			ScaleKeys.SetNum(NumSampleKeys);

			for (int32 k = 0; k < NumSampleKeys; ++k)
			{
				// UE stores NumFrames + 1 samples.  A looping clip's final sample
				// must exactly equal its first sample, otherwise even a tiny M2
				// endpoint difference becomes a visible hop at the loop boundary.
				const float TimeMs = AnimData.bIsLooping && k == NumFrames
					? 0.0f
					: FMath::Min(BakeMs, k * (1000.0f / 30.0f));
				FVector T = SampleVectorTrack(Track.TransTimestamps, Track.TransValues,
					TrackMs(TimeMs, Track.TransGlobalDuration),
                    Track.TransGlobalDuration ? Track.TransGlobalDuration : AnimData.Duration,
                    Track.TransGlobalDuration != 0 || AnimData.bIsLooping, FVector::ZeroVector);
				FQuat R = SampleRotationTrack(Track.RotTimestamps, Track.RotValues,
					TrackMs(TimeMs, Track.RotGlobalDuration),
                    Track.RotGlobalDuration ? Track.RotGlobalDuration : AnimData.Duration,
                    Track.RotGlobalDuration != 0 || AnimData.bIsLooping, SafeBaseRotation);
				FVector S = SampleVectorTrack(Track.ScaleTimestamps, Track.ScaleValues,
					TrackMs(TimeMs, Track.ScaleGlobalDuration),
                    Track.ScaleGlobalDuration ? Track.ScaleGlobalDuration : AnimData.Duration,
                    Track.ScaleGlobalDuration != 0 || AnimData.bIsLooping, SafeBaseScale);

				// Some WotLK tracks reference absent external .anim payloads or contain
				// degenerate packed quaternions. Passing those values into UE animation
				// compression creates NaN bone transforms every frame, forcing the
				// engine to repair and log them continuously. Sanitize once here at the
				// WoW-to-UE asset boundary instead of paying that cost during playback.
				if (T.ContainsNaN() || T.GetAbsMax() > 10000.0f)
				{
					T = FVector::ZeroVector;
					++TotalRepairedAnimationKeys;
				}
				if (R.ContainsNaN() || R.SizeSquared() <= SMALL_NUMBER)
				{
					R = SafeBaseRotation;
					++TotalRepairedAnimationKeys;
				}
				else
				{
					R.Normalize();
				}
				if (S.ContainsNaN() || S.GetAbsMax() > 1000.0f)
				{
					S = SafeBaseScale;
					++TotalRepairedAnimationKeys;
				}

				FVector ConvertedPosition = SafeBaseTranslation +
					FVector(T.Y * FWowCoordinate::SCALE, T.X * FWowCoordinate::SCALE, T.Z * FWowCoordinate::SCALE);
				if (ConvertedPosition.ContainsNaN())
				{
					ConvertedPosition = SafeBaseTranslation;
					++TotalRepairedAnimationKeys;
				}
				PosKeys[k] = FVector3f(ConvertedPosition);
				FQuat Converted(R.Y, R.X, R.Z, -R.W);
				if (Converted.ContainsNaN() || Converted.SizeSquared() <= SMALL_NUMBER)
				{
					Converted = FQuat(SafeBaseRotation.Y, SafeBaseRotation.X,
						SafeBaseRotation.Z, -SafeBaseRotation.W);
					++TotalRepairedAnimationKeys;
				}
				Converted.Normalize();
				RotKeys[k] = FQuat4f(Converted);
				ScaleKeys[k] = FVector3f(S);
				if (bCorrectExtraRoot && PrimaryRootPose.IsValidIndex(k))
				{
					// Desired component-space pose of this independent root, expressed
					// relative to the (animated) primary root it is attached to in UE.
					const FTransform Desired(Converted,
						PivotUE(BoneIdx) + FVector(T.Y, T.X, T.Z) * FWowCoordinate::SCALE, S);
					const FTransform Local = Desired.GetRelativeTransform(PrimaryRootPose[k]);
					if (!Local.ContainsNaN())
					{
						PosKeys[k] = FVector3f(Local.GetTranslation());
						RotKeys[k] = FQuat4f(Local.GetRotation().GetNormalized());
						ScaleKeys[k] = FVector3f(Local.GetScale3D());
					}
				}
			}

            if (Runtime) Runtime->SetRuntimeTrack(RefBoneIndex, MoveTemp(PosKeys), MoveTemp(RotKeys), MoveTemp(ScaleKeys));
#if WITH_EDITOR
            else if (Controller) Controller->SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys);
#endif
        }
#if WITH_EDITOR
        if (Controller) { Controller->CloseBracket(); Controller->NotifyPopulated(); }
#endif

		Result.Add(AnimSeq);
		if (OutAnimationIds)
		{
			OutAnimationIds->Add(AnimData.AnimationId);
		}

		UE_LOG(LogWowSkelMesh, Log, TEXT("  Created animation '%s' (%.2fs, %s)"),
			*AnimName, DurationSec, AnimData.bIsLooping ? TEXT("loop") : TEXT("once"));
	}

	UE_LOG(LogWowSkelMesh, Log, TEXT("Created %d animations for '%s'"), Result.Num(), *ModelName);
	if (TotalRepairedAnimationKeys > 0)
	{
		UE_LOG(LogWowSkelMesh, Warning,
			TEXT("Repaired %d invalid animation key samples for '%s' during import"),
			TotalRepairedAnimationKeys, *ModelName);
	}
	return Result;
}
