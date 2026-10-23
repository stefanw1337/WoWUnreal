#include "WowEquipmentManager.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowRuntimeLumen.h"
#include "WowCookedMaterial.h"
#include "WowM2ParticleComponent.h"
#include "WowFixturePolicy.h"
#include "WowSkeletalMeshBuilder.h"
#include "WowAssetCache.h"
#include "WowTextureFactory.h"
#include "Mpq/MpqManager.h"
#include "Formats/M2Parser.h"
#include "Formats/M2Types.h"
#include "Formats/BlpParser.h"
#include "Formats/BlpTypes.h"
#include "Formats/Dbc/DbcStore.h"
#include "Coord/WowCoordinate.h"
#include "WowSpellMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/AnimSequence.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "UObject/UObjectGlobals.h"
#endif

// Defined in WowCharacterBuilder.cpp: shared M2 pass material (blend variants,
// UV scrolling, PassTint/PassOpacity). Declared here, outside the anonymous namespace.
UMaterial* WowGetM2PassMaterial(uint16 WowBlendMode, bool bLit, bool bAnimatedUV);

DEFINE_LOG_CATEGORY_STATIC(LogWowEquip, Log, All);

namespace
{
FString NormalizeAssetPath(const FString& Path, const TCHAR* Extension)
{
    FString Result = Path;
    Result.ReplaceInline(TEXT("/"), TEXT("\\"));

    if (Result.EndsWith(TEXT(".mdx"), ESearchCase::IgnoreCase) && FCString::Stricmp(Extension, TEXT(".m2")) == 0)
    {
        Result = Result.LeftChop(4) + TEXT(".m2");
    }
    else if (!Result.EndsWith(Extension, ESearchCase::IgnoreCase))
    {
        Result += Extension;
    }

    return Result;
}


struct FEquipmentVisualTables
{
    FMpqManager* Source = nullptr;
    TMap<uint32, TArray<uint32>> Visuals;
    TMap<uint32, FString> Effects;
    TMap<uint32, uint32> Enchantments;
    void Load(FMpqManager* Mpq)
    {
        if (!Mpq || Source == Mpq) return;
        Source = Mpq;
        Visuals.Reset(); Effects.Reset(); Enchantments.Reset();
        TArray<uint8> Raw;
        FDbcParser Parser;
        if (Mpq->ReadFile(TEXT("DBFilesClient\\ItemVisuals.dbc"), Raw) && Parser.Parse(Raw) && Parser.GetFieldCount() == 6)
            for (uint32 Row = 0; Row < Parser.GetRecordCount(); ++Row)
            {
                TArray<uint32>& Slots = Visuals.Add(Parser.GetUInt(Row, 0));
                for (uint32 Slot = 1; Slot <= 5; ++Slot) Slots.Add(Parser.GetUInt(Row, Slot));
            }
        if (Mpq->ReadFile(TEXT("DBFilesClient\\ItemVisualEffects.dbc"), Raw) && Parser.Parse(Raw) && Parser.GetFieldCount() == 2)
            for (uint32 Row = 0; Row < Parser.GetRecordCount(); ++Row)
                Effects.Add(Parser.GetUInt(Row, 0), NormalizeAssetPath(Parser.GetString(Row, 1), TEXT(".m2")));
        // Wrath: ID + charges + four triples + 16 localized names + locale flags.
        if (Mpq->ReadFile(TEXT("DBFilesClient\\SpellItemEnchantment.dbc"), Raw) && Parser.Parse(Raw) && Parser.GetFieldCount() == 38)
            for (uint32 Row = 0; Row < Parser.GetRecordCount(); ++Row)
                Enchantments.Add(Parser.GetUInt(Row, 0), Parser.GetUInt(Row, 31));
    }
};

static FEquipmentVisualTables GEquipmentVisualTables;

FEquipmentVisualTables& GetEquipmentVisualTables(FMpqManager* Mpq)
{
    GEquipmentVisualTables.Load(Mpq);
    return GEquipmentVisualTables;
}

bool ReadEquipmentEffectModel(FMpqManager* Mpq, const FString& Path, FM2Data& OutData)
{
    TArray<uint8> Raw, Skin;
    if (!Mpq || !Mpq->ReadFile(Path, Raw) || !Mpq->ReadFile(Path.LeftChop(3) + TEXT("00.skin"), Skin)) return false;
    OutData = FM2Parser::Parse(Raw, Skin);
    return OutData.bIsValid;
}

void AddEquipmentParticleEffect(USceneComponent* Parent, const FM2Data& Data,
    FMpqManager* Mpq, FWowAssetCache* Cache, const FVector& Position, FName Kind)
{
    if (Data.ParticleEmitters.IsEmpty()) return;
    AActor* Owner = Parent->GetOwner();
    UWowM2ParticleComponent* Effect = NewObject<UWowM2ParticleComponent>(Owner);
    Owner->AddInstanceComponent(Effect);
    Effect->ComponentTags.Add(Kind);
    Effect->ComponentTags.Add(TEXT("WowEquipmentEffect"));
    Effect->SetupAttachment(Parent);
    Effect->SetRelativeLocation(Position);
    Effect->RegisterComponent();
    Effect->InitializeFromParsedM2Data(&Data, Mpq, Cache);
}


UTexture2D* LoadBlpTexture(FMpqManager* Mpq, FWowAssetCache* Cache, const FString& TexturePath);

UMaterial* GetItemEffectMaterial(uint16 BlendMode)
{
    BlendMode = FMath::Min<uint16>(BlendMode, 3);
    // r320: "Tint" carries the pass colour and transparency (Executioner was full-bright flames).
    const FString CookedName = FString::Printf(TEXT("M_WowItemEffectTint%d"), BlendMode);
    static TMap<uint16,TWeakObjectPtr<UMaterial>> Materials;
    if (UMaterial* Existing = Materials.FindRef(BlendMode).Get()) return Existing;
    if (UMaterial* Saved = WowCookedMaterial::Load(CookedName)) { Materials.Add(BlendMode, Saved); return Saved; }
#if WITH_EDITOR
    UMaterial* Material = NewObject<UMaterial>();
    Material->SetShadingModel(MSM_Unlit);
    Material->BlendMode = BlendMode == 1 ? BLEND_Masked : (BlendMode >= 3 ? BLEND_Additive : (BlendMode == 2 ? BLEND_Translucent : BLEND_Opaque));
    Material->TwoSided = true;
    auto* Texture = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Texture->ParameterName = TEXT("BaseTexture"); Texture->SamplerType = SAMPLERTYPE_Color;
    Texture->Texture = LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    Material->GetExpressionCollection().AddExpression(Texture);
    auto* Tint = NewObject<UMaterialExpressionVectorParameter>(Material);
    Tint->ParameterName = TEXT("Tint"); Tint->DefaultValue = FLinearColor::White;
    Material->GetExpressionCollection().AddExpression(Tint);
    auto* TintedColor = NewObject<UMaterialExpressionMultiply>(Material);
    TintedColor->A.Connect(0,Texture); TintedColor->B.Connect(0,Tint);
    Material->GetExpressionCollection().AddExpression(TintedColor);
    auto* TintedAlpha = NewObject<UMaterialExpressionMultiply>(Material);
    TintedAlpha->A.Connect(4,Texture); TintedAlpha->B.Connect(4,Tint);
    Material->GetExpressionCollection().AddExpression(TintedAlpha);
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0,TintedColor);
    Material->GetEditorOnlyData()->Opacity.Connect(0,TintedAlpha);
    Material->GetEditorOnlyData()->OpacityMask.Connect(0,TintedAlpha);
    bool bRecompile = false; Material->SetMaterialUsage(bRecompile,MATUSAGE_SkeletalMesh);
    Material->PreEditChange(nullptr); Material->PostEditChange();
    Material = WowCookedMaterial::Save(Material, CookedName);
    Materials.Add(BlendMode,Material);
    return Material;
#else
    return UMaterial::GetDefaultMaterial(MD_Surface);
#endif
}

bool AddEquipmentModelEffect(USceneComponent* Parent, const FM2Data& Data, const FString& Path,
    FMpqManager* Mpq, FWowAssetCache* Cache, const FVector& Position, FName Kind)
{
    if (Data.Vertices.IsEmpty() || Data.Indices.IsEmpty() || Data.Bones.IsEmpty()) return false;
    const FString CacheKey = TEXT("EquipmentEffect:") + Path.ToLower();
    USkeletalMesh* Mesh = Cache ? Cache->FindSkelMesh(CacheKey) : nullptr;
    TArray<FGeosetSectionInfo> Sections;
    if (!Mesh)
    {
        USkeleton* Skeleton = FWowSkeletalMeshBuilder::CreateSkeleton(Data,CacheKey);
        Mesh = FWowSkeletalMeshBuilder::CreateSkeletalMesh(Data,Skeleton,CacheKey,Mpq,Cache,&Sections);
        if (!Mesh) return false;
        for (const auto& Section : Sections)
        {
            if (!Data.TexturePaths.IsValidIndex(Section.TextureIndex)) continue;
            UTexture2D* Texture = LoadBlpTexture(Mpq,Cache,Data.TexturePaths[Section.TextureIndex]);
            UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(GetItemEffectMaterial(Section.BlendMode),Mesh);
            Material->SetTextureParameterValue(TEXT("BaseTexture"),Texture);
            // r320 (Stefan: Blood Draining's ExecutionerGlow was a sheet of bright flames): apply the
            // pass colour (1, .27, .17) and transparency (0.1). Additive blends ignore opacity, so the
            // colour is premultiplied by the alpha there.
            if (Data.RenderPasses.IsValidIndex(Section.RenderPassIndex))
            {
                const FM2RenderPass& Pass = Data.RenderPasses[Section.RenderPassIndex];
                auto First = [](const FM2MaterialTrack& T, float Default)
                { return T.Values.Num() > 0 ? T.Values[0] : Default; };
                const float Alpha = FMath::Clamp(First(Pass.ColorAlpha, 1.f) * First(Pass.TextureWeight, 1.f), 0.f, 1.f);
                FLinearColor Tint(FMath::Clamp(First(Pass.ColorRed, 1.f), 0.f, 1.f),
                    FMath::Clamp(First(Pass.ColorGreen, 1.f), 0.f, 1.f), FMath::Clamp(First(Pass.ColorBlue, 1.f), 0.f, 1.f), Alpha);
                if (Section.BlendMode >= 3) { Tint.R *= Alpha; Tint.G *= Alpha; Tint.B *= Alpha; }
                Material->SetVectorParameterValue(TEXT("Tint"), Tint);
            }
            if (Mesh->GetMaterials().IsValidIndex(Section.SectionIndex))
                Mesh->GetMaterials()[Section.SectionIndex].MaterialInterface = Material;
        }
        if (Cache) Cache->CacheSkelMesh(CacheKey,Mesh);
    }
    static TMap<FString,TWeakObjectPtr<UAnimSequence>> Animations;
    UAnimSequence* Animation = Animations.FindRef(CacheKey).Get();
    if (!Animation || Animation->GetSkeleton() != Mesh->GetSkeleton())
    {
        const TSet<int32> StandOnly = {0};
        const auto Clips = FWowSkeletalMeshBuilder::CreateAnimations(Data,Mesh->GetSkeleton(),CacheKey,&StandOnly);
        Animation = Clips.IsEmpty() ? nullptr : Clips[0];
        if (Animation) Animations.Add(CacheKey,Animation);
    }
    AActor* Owner = Parent->GetOwner();
    auto* Root = NewObject<UWowM2ParticleComponent>(Owner);
    Owner->AddInstanceComponent(Root);
    Root->ComponentTags.Add(Kind); Root->ComponentTags.Add(TEXT("WowEquipmentEffect"));
    Root->SetupAttachment(Parent); Root->SetRelativeLocation(Position); Root->RegisterComponent();
    Root->SetComponentTickEnabled(false);
    auto* Visual = NewObject<USkeletalMeshComponent>(Owner);
    Owner->AddInstanceComponent(Visual); Root->OwnEquipmentVisual(Visual);
    Visual->ComponentTags.Add(TEXT("WowEquipmentEffectGeometry"));
    Visual->SetupAttachment(Root); Visual->SetSkeletalMesh(Mesh);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetCastShadow(false);
    Visual->RegisterComponent();
    if (Animation) Visual->PlayAnimation(Animation,true);
    // Original skull geometry is animated. Glow emitters now follow the skull
    // bones instead of sitting at two fixed points on the end of the staff.
    for (const FM2ParticleEmitter& Emitter : Data.ParticleEmitters)
    {
        if (!Emitter.bEnabledInStand) continue;
        FM2Data SingleEmitter = Data;
        SingleEmitter.ParticleEmitters = {Emitter};
        FName BoneName = NAME_None;
        if (Data.Bones.IsValidIndex(Emitter.Bone))
        {
            BoneName = FWowSkeletalMeshBuilder::GetBoneName(Emitter.Bone);
            SingleEmitter.ParticleEmitters[0].Position -= Data.Bones[Emitter.Bone].PivotPoint;
        }
        auto* Particles = NewObject<UWowM2ParticleComponent>(Owner);
        Owner->AddInstanceComponent(Particles); Root->OwnEquipmentVisual(Particles);
        Particles->ComponentTags.Add(TEXT("WowEquipmentEffect"));
        Particles->SetupAttachment(Visual,BoneName); Particles->RegisterComponent();
        Particles->AddTickPrerequisiteComponent(Visual);
        Particles->InitializeFromParsedM2Data(&SingleEmitter,Mpq,Cache);
    }
    return true;
}

void AddEquipmentItemVisual(USceneComponent* Equipment, const FM2Data& WeaponData,
    uint32 VisualId, FMpqManager* Mpq, FWowAssetCache* Cache, FName Kind)
{
    FEquipmentVisualTables& Tables = GetEquipmentVisualTables(Mpq);
    const TArray<uint32>* Slots = Tables.Visuals.Find(VisualId);
    if (!Slots) return;
    for (int32 Slot = 0; Slot < Slots->Num(); ++Slot)
    {
        const FString* Path = Tables.Effects.Find((*Slots)[Slot]);
        const FM2Attachment* Attachment = WeaponData.Attachments.FindByPredicate(
            [Slot](const FM2Attachment& A) { return A.Id == static_cast<uint32>(Slot); });
        if (!Path || !Attachment) continue;
        FM2Data EffectData;
        if (!ReadEquipmentEffectModel(Mpq, *Path, EffectData)) continue;
        const FVector P = Attachment->Position;
        const FVector Position = FVector(P.Y,P.X,P.Z) * FWowCoordinate::SCALE;
        // Enchant effects (Rockbiter rocks, Flametongue flames) are authored for an
        // average blade; on a thick weapon head they orbited inside the model. Widen
        // the ORBIT until it clears the weapon's cross-section (the two smaller
        // bounding-box axes) while every rock/flame keeps its own size.
        float Orbit = 1.0f;
        // Only weapon enchants (Rockbiter etc.); a weapon's own built-in effect is
        // authored for that exact model and stays as designed.
        if (Kind == TEXT("WowEnchantmentEffect") && WeaponData.BoundingBox.IsValid && !EffectData.Vertices.IsEmpty())
        {
            const FVector Size = WeaponData.BoundingBox.GetSize();
            const float CrossHalf = 0.5f * (Size.X + Size.Y + Size.Z - Size.GetMax() - Size.GetMin());
            // Measure the real orbit by playing the effect's animation: for every piece
            // (vertices sharing a dominant bone) its closest distance to the centre and its
            // own half-size. Then widen just enough that the inner edge of each piece clears
            // the weapon's surface by ~10 % (the previous estimate hit the 3x cap and ran wide).
            const float WeaponHalf = CrossHalf * FWowCoordinate::SCALE;
            float Centre = TNumericLimits<float>::Max(), PieceHalf = 0.0f;
            auto Dominant = [](const FM2Vertex& V) { int32 Best = 0; for (int32 J = 1; J < 4; ++J) if (V.BoneWeights[J] > V.BoneWeights[Best]) Best = J; return int32(V.BoneIndices[Best]); };
            const float DurationSec = EffectData.AnimationTracks.IsEmpty() ? 0.0f : EffectData.AnimationTracks[0].Duration / 1000.0f;
            TArray<FVector> Posed, Normals;
            for (int32 Sample = 0; Sample < 8; ++Sample)
            {
                UWowSpellMeshComponent::EvaluateVertices(EffectData, DurationSec * Sample / 8.0f, true, Posed, Normals);
                TMap<int32, TPair<FVector, int32>> Sum;
                for (int32 V = 0; V < Posed.Num(); ++V) { auto& S = Sum.FindOrAdd(Dominant(EffectData.Vertices[V])); S.Key += Posed[V]; ++S.Value; }
                for (int32 V = 0; V < Posed.Num(); ++V)
                {
                    const auto& S = Sum[Dominant(EffectData.Vertices[V])];
                    const FVector C = S.Key / FMath::Max(S.Value, 1);
                    if (C.Size() < 0.5f) continue; // a glow sitting on the centre is not part of the orbit
                    Centre = FMath::Min(Centre, static_cast<float>(C.Size()));
                    PieceHalf = FMath::Max(PieceHalf, static_cast<float>(FVector::Dist(Posed[V], C)));
                }
            }
            if (Centre < TNumericLimits<float>::Max() && Centre > KINDA_SMALL_NUMBER)
                Orbit = FMath::RoundToFloat(FMath::Clamp((WeaponHalf * 1.1f + PieceHalf) / Centre, 1.0f, 3.0f) * 10.0f) / 10.0f;
            UE_LOG(LogWowEquip, Log, TEXT("Enchant %s on weapon: orbit centre %.1f cm, piece %.1f cm, weapon half %.1f cm -> x%.1f"),
                **Path, Centre, PieceHalf, WeaponHalf, Orbit);
        }
        FString EffectKey = *Path;
        if (Orbit > 1.05f)
        {
            // Spread the layout by Orbit: pivots, animated offsets and emitter spots move
            // out, but each piece (vertices sharing a dominant bone) keeps its shape
            // around its own centre.
            TMap<int32, TPair<FVector, int32>> Pieces;
            auto Dominant = [](const FM2Vertex& V) { int32 Best = 0; for (int32 J = 1; J < 4; ++J) if (V.BoneWeights[J] > V.BoneWeights[Best]) Best = J; return int32(V.BoneIndices[Best]); };
            for (const FM2Vertex& V : EffectData.Vertices) { auto& Piece = Pieces.FindOrAdd(Dominant(V)); Piece.Key += V.Position; ++Piece.Value; }
            for (FM2Vertex& V : EffectData.Vertices)
            {
                const auto& Piece = Pieces[Dominant(V)];
                const FVector Centre = Piece.Key / FMath::Max(Piece.Value, 1);
                V.Position = Centre * Orbit + (V.Position - Centre);
            }
            for (FM2Bone& Bone : EffectData.Bones) Bone.PivotPoint *= Orbit;
            for (FM2AnimationData& Clip : EffectData.AnimationTracks)
                for (FM2BoneTrack& Track : Clip.BoneTracks)
                    for (FVector& Offset : Track.TransValues) Offset *= Orbit;
            for (FM2ParticleEmitter& Emitter : EffectData.ParticleEmitters) Emitter.Position *= Orbit;
            EffectKey += FString::Printf(TEXT("@orbit%.1f"), Orbit); // distinct cached mesh per spread
        }
        if (!AddEquipmentModelEffect(Equipment,EffectData,EffectKey,Mpq,Cache,Position,Kind))
            AddEquipmentParticleEffect(Equipment,EffectData,Mpq,Cache,Position,Kind);
    }
}

FString EquipmentTagValue(const USceneComponent* Equipment, const TCHAR* Prefix)
{
    for (FName Tag : Equipment->ComponentTags)
    {
        FString Text = Tag.ToString();
        if (Text.RemoveFromStart(Prefix)) return Text;
    }
    return FString();
}

bool TryResolveAssetPath(FMpqManager* Mpq, const FString& FileName, const TCHAR* Extension,
    const TArray<FString>& CandidateDirs, FString& OutPath)
{
    if (!Mpq || FileName.IsEmpty())
    {
        return false;
    }

    TArray<FString> Candidates;
    Candidates.Reserve(1 + CandidateDirs.Num());

    const FString NormalizedFile = NormalizeAssetPath(FileName, Extension);
    Candidates.Add(NormalizedFile);

    for (const FString& Dir : CandidateDirs)
    {
        Candidates.Add(Dir + FPaths::GetCleanFilename(NormalizedFile));
    }

    for (const FString& Candidate : Candidates)
    {
        TArray<uint8> Raw;
        if (Mpq->ReadFile(Candidate, Raw))
        {
            OutPath = Candidate;
            return true;
        }
    }

    return false;
}

TArray<FString> GetCandidateDirs(FWowEquipmentManager::EAttachmentPoint AttachPoint)
{
    switch (AttachPoint)
    {
    case FWowEquipmentManager::EAttachmentPoint::Helmet:
        return {
            TEXT("Item\\ObjectComponents\\Head\\"),
            TEXT("Item\\ObjectComponents\\Helmet\\")
        };
    case FWowEquipmentManager::EAttachmentPoint::LeftShoulder:
    case FWowEquipmentManager::EAttachmentPoint::RightShoulder:
        return {
            TEXT("Item\\ObjectComponents\\Shoulder\\")
        };
    case FWowEquipmentManager::EAttachmentPoint::Back:
        return {
            TEXT("Item\\ObjectComponents\\Cape\\"),
            TEXT("Item\\ObjectComponents\\Cloak\\")
        };
    case FWowEquipmentManager::EAttachmentPoint::ShieldHand:
    case FWowEquipmentManager::EAttachmentPoint::Shield:
        return {
            TEXT("Item\\ObjectComponents\\Shield\\"),
            TEXT("Item\\ObjectComponents\\Weapon\\")
        };
    default:
        return {
            TEXT("Item\\ObjectComponents\\Weapon\\"),
            TEXT("Item\\ObjectComponents\\Shield\\"),
            TEXT("Item\\ObjectComponents\\Bow\\"),
            TEXT("Item\\ObjectComponents\\Crossbow\\"),
            TEXT("Item\\ObjectComponents\\Gun\\"),
            TEXT("Item\\ObjectComponents\\Thrown\\"),
            TEXT("Item\\ObjectComponents\\Misc\\")
        };
    }
}

const FM2Attachment* FindAttachment(const FM2Data& M2Data, FWowEquipmentManager::EAttachmentPoint AttachPoint)
{
    const uint32 PointId = static_cast<uint32>(AttachPoint);

    if (PointId < static_cast<uint32>(M2Data.AttachmentLookup.Num()))
    {
        const int16 AttachIdx = M2Data.AttachmentLookup[PointId];
        if (AttachIdx >= 0 && AttachIdx < M2Data.Attachments.Num())
        {
            return &M2Data.Attachments[AttachIdx];
        }
    }

    for (const FM2Attachment& Attach : M2Data.Attachments)
    {
        if (Attach.Id == PointId)
        {
            return &Attach;
        }
    }

    return nullptr;
}

UTexture2D* LoadBlpTexture(FMpqManager* Mpq, FWowAssetCache* Cache, const FString& TexturePath)
{
    if (!Mpq || TexturePath.IsEmpty())
    {
        return nullptr;
    }

    if (Cache)
    {
        if (UTexture2D* Cached = Cache->FindTexture(TexturePath))
        {
            return Cached;
        }
    }

    TArray<uint8> BlpRaw;
    if (!Mpq->ReadFile(TexturePath, BlpRaw))
    {
        return nullptr;
    }

    FBlpTexture Blp = FBlpParser::Parse(BlpRaw);
    if (!Blp.bIsValid)
    {
        return nullptr;
    }

    UTexture2D* Texture = FWowTextureFactory::CreateTexture(Blp, TexturePath);
    if (Texture && Cache)
    {
        Cache->CacheTexture(TexturePath, Texture);
    }
    return Texture;
}

UMaterial* GetEquipmentMaterial()
{
    // r422 (Stefan: "shield should not be see-through"): the texture's alpha cut holes into every
    // item. In WoW only alpha-key passes (blend 1) use it as a cut-out; on opaque passes (blend 0)
    // the alpha is the reflection mask of the armour shine, so a shield's metal frame showed the
    // world through it. v2 takes a per-instance AlphaKey (1 = cut out, 0 = ignore the alpha);
    // the first-person dither is kept either way.
    static TWeakObjectPtr<UMaterial> CachedMaterial;
    if (!CachedMaterial.IsValid()) CachedMaterial = WowCookedMaterial::Load(TEXT("M_WowEquipment_v2"));
    if (!CachedMaterial.IsValid())
    {
#if WITH_EDITOR
        UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_WowEquipmentRuntimeTransient"));
        if (Material)
        {
            // Equipment must participate in the same world lighting as its
            // owner.  The old unlit/emissive setup made pale weapon textures
            // glow at night while the character and ground were correctly dark.
            Material->SetShadingModel(MSM_DefaultLit);
            Material->BlendMode = BLEND_Masked;
            Material->TwoSided = false;

            UTexture2D* DefaultTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
            auto& Expressions = Material->GetExpressionCollection();

            UMaterialExpressionTextureSampleParameter2D* TextureSampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
            TextureSampler->ParameterName = TEXT("BaseTexture");
            TextureSampler->SamplerType = SAMPLERTYPE_Color;
            TextureSampler->Texture = DefaultTexture;
            Expressions.AddExpression(TextureSampler);

            Material->GetEditorOnlyData()->BaseColor.Connect(0, TextureSampler);

			UMaterialExpressionConstant* Roughness = NewObject<UMaterialExpressionConstant>(Material);
			Roughness->R = 0.68f;
			Expressions.AddExpression(Roughness);
			Material->GetEditorOnlyData()->Roughness.Connect(0, Roughness);

			UMaterialExpressionConstant* Specular = NewObject<UMaterialExpressionConstant>(Material);
			Specular->R = 0.32f;
			Expressions.AddExpression(Specular);
			Material->GetEditorOnlyData()->Specular.Connect(0, Specular);

            UMaterialExpressionScalarParameter* FirstPersonOpacity =
                NewObject<UMaterialExpressionScalarParameter>(Material);
            FirstPersonOpacity->ParameterName = TEXT("FirstPersonOpacity");
            FirstPersonOpacity->DefaultValue = 1.0f;
            Expressions.AddExpression(FirstPersonOpacity);

            UMaterialExpressionScalarBlueNoise* BlueNoise =
                NewObject<UMaterialExpressionScalarBlueNoise>(Material);
            Expressions.AddExpression(BlueNoise);

            UMaterialExpressionConstant* One = NewObject<UMaterialExpressionConstant>(Material);
            One->R = 1.0f;
            Expressions.AddExpression(One);
            UMaterialExpressionConstant* Zero = NewObject<UMaterialExpressionConstant>(Material);
            Zero->R = 0.0f;
            Expressions.AddExpression(Zero);

            UMaterialExpressionIf* Dither = NewObject<UMaterialExpressionIf>(Material);
            Dither->A.Connect(0, FirstPersonOpacity);
            Dither->B.Connect(0, BlueNoise);
            Dither->AGreaterThanB.Connect(0, One);
            Dither->AEqualsB.Connect(0, One);
            Dither->ALessThanB.Connect(0, Zero);
            Expressions.AddExpression(Dither);

            UMaterialExpressionScalarParameter* AlphaKey = NewObject<UMaterialExpressionScalarParameter>(Material);
            AlphaKey->ParameterName = TEXT("AlphaKey");
            AlphaKey->DefaultValue = 0.0f;
            Expressions.AddExpression(AlphaKey);
            UMaterialExpressionLinearInterpolate* CutOut = NewObject<UMaterialExpressionLinearInterpolate>(Material);
            CutOut->A.Connect(0, One);
            CutOut->B.Connect(4, TextureSampler);
            CutOut->Alpha.Connect(0, AlphaKey);
            Expressions.AddExpression(CutOut);

            UMaterialExpressionMultiply* MaskedOpacity =
                NewObject<UMaterialExpressionMultiply>(Material);
            MaskedOpacity->A.Connect(0, CutOut);
            MaskedOpacity->B.Connect(0, Dither);
            Expressions.AddExpression(MaskedOpacity);
            Material->GetEditorOnlyData()->OpacityMask.Connect(0, MaskedOpacity);

            bool bNeedsRecompile = false;
            Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh);
            Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_StaticMesh);
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
            Material->ForceRecompileForRendering();
            CachedMaterial = WowCookedMaterial::Save(Material, TEXT("M_WowEquipment_v2"));
        }
#endif

        // A package made before r422 has only the old material (alpha always cuts out).
        if (!CachedMaterial.IsValid()) CachedMaterial = WowCookedMaterial::Load(TEXT("M_WowEquipment"));
        if (!CachedMaterial.IsValid())
        {
            CachedMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
        }
    }
    return CachedMaterial.Get();
}

/**
 * One mesh section per skin submesh, with that submesh's first render pass (texture + blend).
 * Item models carry extra cards (glows, alpha-keyed spikes, a second texture) that are not the
 * item texture; drawing every triangle with the item texture showed those cards as flat,
 * one-sided copies of the whole texture atlas (e.g. a shield's glow plane).
 */
struct FEquipmentSection { int32 Start = 0, Count = 0; FM2RenderPass Pass; bool bLayer = false; };
float FirstTrackValue(const FM2MaterialTrack& Track, float Default)
{
    return Track.Values.Num() > 0 && FMath::IsFinite(Track.Values[0]) ? Track.Values[0] : Default;
}
/** A pulsing layer (keys 0 -> 0.5 -> 0) has no single value; drawn still, it shows its mean. */
float MeanTrackValue(const FM2MaterialTrack& Track, float Default)
{
    if (Track.Values.Num() < 2) return FirstTrackValue(Track, Default);
    double Sum = 0.0; int32 Count = 0;
    for (const float Value : Track.Values) if (FMath::IsFinite(Value)) { Sum += Value; ++Count; }
    return Count > 0 ? float(Sum / Count) : Default;
}
bool IsEnvironmentTexture(const FM2Data& Data, int32 TextureIndex)
{
    return Data.TexturePaths.IsValidIndex(TextureIndex)
        && Data.TexturePaths[TextureIndex].Contains(TEXT("ArmorReflect"), ESearchCase::IgnoreCase);
}
TArray<FEquipmentSection> GetEquipmentSections(const FM2Data& Data)
{
    TArray<FEquipmentSection> Out;
    TSet<int32> Seen;
    // r422 (Stefan: "the effects on these swords look wrong"): only the first pass of each submesh
    // was drawn, so a blade's glow layers (an additive pulse, a scrolling energy texture over the
    // fuller) never showed and the strip looked like plain, pale metal. Additive and scrolling
    // layers on top of a submesh now get their own section (the same triangles again); a
    // blended item-texture layer after such a glow is the frame drawn back over it. Blended
    // layers straight on the base are the armour shine and stay out (as do env-map layers).
    TMap<int32, bool> HasGlow;
    for (const FM2RenderPass& Pass : Data.RenderPasses)
    {
        if (!Data.Submeshes.IsValidIndex(Pass.SubmeshIndex)) continue;
        if (Seen.Contains(Pass.SubmeshIndex))
        {
            if (IsEnvironmentTexture(Data, Pass.TextureIndex) || FWowFixturePolicy::IsFireGlowCard(Data, FString(), Pass)
                || FWowFixturePolicy::IsLegacyGlowCard(Data, Pass)) continue;
            const bool bGlow = Pass.BlendMode >= 3 || (!Pass.UVScrollSpeed.IsNearlyZero() && Pass.BlendMode >= 2);
            const bool bFrameBack = Pass.BlendMode == 2 && HasGlow.FindRef(Pass.SubmeshIndex);
            if (!bGlow && !bFrameBack) continue;
            const FM2Submesh& Sub = Data.Submeshes[Pass.SubmeshIndex];
            const int32 Start = int32(Sub.StartTriangle), Count = int32(Sub.NumTriangles) / 3 * 3;
            if (Count <= 0 || Start < 0 || Start + Count > Data.Indices.Num()) continue;
            if (bGlow) HasGlow.Add(Pass.SubmeshIndex, true);
            FEquipmentSection Layer; Layer.Start = Start; Layer.Count = Count; Layer.Pass = Pass; Layer.bLayer = true;
            Out.Add(Layer);
            continue;
        }
        // r418: a burning item (the torch) loses its flat glow card; its flame particles stay.
        // r431: every old flat light card on an item goes, burning or not.
        if (FWowFixturePolicy::IsFireGlowCard(Data, FString(), Pass) || FWowFixturePolicy::IsLegacyGlowCard(Data, Pass)) continue;
        const FM2Submesh& Sub = Data.Submeshes[Pass.SubmeshIndex];
        const int32 Start = int32(Sub.StartTriangle), Count = int32(Sub.NumTriangles) / 3 * 3;
        if (Count <= 0 || Start < 0 || Start + Count > Data.Indices.Num()) continue;
        Seen.Add(Pass.SubmeshIndex);
        FEquipmentSection Section; Section.Start = Start; Section.Count = Count; Section.Pass = Pass;
        Out.Add(Section);
    }
    return Out;
}

void ApplyEquipmentSectionMaterials(UStaticMeshComponent* Component, const FM2Data& Data, UTexture2D* ItemTexture,
    FMpqManager* Mpq, FWowAssetCache* Cache)
{
    const TArray<FEquipmentSection> Sections = GetEquipmentSections(Data);
    if (!Component || !Component->GetStaticMesh() || Sections.Num() < 2
        || Component->GetStaticMesh()->GetStaticMaterials().Num() != Sections.Num()) return;
    UMaterial* Base = GetEquipmentMaterial();
    for (int32 Slot = 0; Slot < Sections.Num(); ++Slot)
    {
        const FM2RenderPass& Pass = Sections[Slot].Pass;
        const bool bLayer = Sections[Slot].bLayer;
        UTexture2D* Texture = ItemTexture;
        // Type 0 = a texture named in the model itself (glow cards, gems); other types are
        // the item's own texture from ItemDisplayInfo.
        if (Data.TextureTypes.IsValidIndex(Pass.TextureIndex) && Data.TextureTypes[Pass.TextureIndex] == 0
            && Data.TexturePaths.IsValidIndex(Pass.TextureIndex) && !Data.TexturePaths[Pass.TextureIndex].IsEmpty())
        {
            FString Path = Data.TexturePaths[Pass.TextureIndex];
            if (!Path.EndsWith(TEXT(".blp"), ESearchCase::IgnoreCase)) Path += TEXT(".blp");
            if (UTexture2D* Own = LoadBlpTexture(Mpq, Cache, Path)) Texture = Own;
        }
        if (!Texture) continue;
        // Opaque / alpha-key passes stay lit like the item. Blended and additive cards glow,
        // two-sided, with their authored UV scrolling (flowing energy on blades) and colour.
        const bool bScrolls = !Pass.UVScrollSpeed.IsNearlyZero();
        const bool bEffect = Pass.BlendMode >= 2 || bScrolls;
        // The frame drawn back over a glow (blend 2, item texture) is lit like the item itself.
        const bool bLitLayer = bLayer && Pass.BlendMode == 2 && !bScrolls;
        UMaterial* Parent = bEffect ? WowGetM2PassMaterial(Pass.BlendMode, bLitLayer, bScrolls) : Base;
        if (!Parent) continue;
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Component);
        MID->SetTextureParameterValue(TEXT("BaseTexture"), Texture);
        if (!bEffect) MID->SetScalarParameterValue(TEXT("AlphaKey"), Pass.BlendMode == 1 ? 1.f : 0.f);
        if (bEffect)
        {
            auto Value = [bLayer](const FM2MaterialTrack& Track) { return bLayer ? MeanTrackValue(Track, 1.f) : FirstTrackValue(Track, 1.f); };
            MID->SetVectorParameterValue(TEXT("UVScrollSpeed"), FLinearColor(Pass.UVScrollSpeed.X, Pass.UVScrollSpeed.Y, 0, 0));
            MID->SetVectorParameterValue(TEXT("PassTint"), FLinearColor(Value(Pass.ColorRed),
                Value(Pass.ColorGreen), Value(Pass.ColorBlue), 1.f));
            MID->SetScalarParameterValue(TEXT("PassOpacity"),
                FMath::Clamp(Value(Pass.ColorAlpha) * Value(Pass.TextureWeight), 0.f, 1.f));
        }
        Component->SetMaterial(Slot, MID);
    }
}

void ApplyEquipmentMaterial(UPrimitiveComponent* Component, UTexture2D* Texture, bool bAlphaKey)
{
    if (!Component || !Texture)
    {
        return;
    }

    if (UMaterial* BaseMat = GetEquipmentMaterial())
    {
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, Component);
        MID->SetTextureParameterValue(TEXT("BaseTexture"), Texture);
        MID->SetScalarParameterValue(TEXT("AlphaKey"), bAlphaKey ? 1.f : 0.f);
        Component->SetMaterial(0, MID);
    }
}
}

FName FWowEquipmentManager::GetAttachmentBoneName(const FM2Data& M2Data, EAttachmentPoint AttachPoint)
{
    if (const FM2Attachment* Attachment = FindAttachment(M2Data, AttachPoint))
    {
        if (Attachment->Bone >= 0)
        {
            return FWowSkeletalMeshBuilder::GetBoneName(Attachment->Bone);
        }
    }

    UE_LOG(LogWowEquip, Warning, TEXT("Attachment point %d not found in M2 data"), static_cast<uint32>(AttachPoint));
    return NAME_None;
}

bool FWowEquipmentManager::ResolveEquipmentPaths(FMpqManager* Mpq, uint32 ItemDisplayId, EAttachmentPoint AttachPoint,
    FString& OutModelPath, FString* OutTexturePath)
{
    const FItemDisplayInfoDbcEntry* DisplayInfo = FDbcStore::Get().ItemDisplayInfo().GetById(ItemDisplayId);
    if (!DisplayInfo)
    {
        return false;
    }

    FString ModelFile;
    FString TextureFile;

    // ItemDisplayInfo supplies independently authored left/right shoulder models.
    // Keep their side-specific geometry (including intentionally asymmetric sets).
    if (AttachPoint == EAttachmentPoint::LeftShoulder)
    {
        ModelFile = DisplayInfo->ModelNames[0];
        TextureFile = DisplayInfo->ModelTextures[0];
    }
    else if (AttachPoint == EAttachmentPoint::RightShoulder)
    {
        // No right model = a one-sided shoulder (121 displays in 3.3.5 have only a left one):
        // the right stays empty, like the client (r278; it used to mirror the left model).
        ModelFile = DisplayInfo->ModelNames[1];
        TextureFile = DisplayInfo->ModelTextures[1];
        if (TextureFile.IsEmpty()) TextureFile = DisplayInfo->ModelTextures[0];
    }
    else if (AttachPoint == EAttachmentPoint::LeftHand)
    {
        ModelFile = DisplayInfo->ModelNames[1];
        TextureFile = DisplayInfo->ModelTextures[1];
        if (ModelFile.IsEmpty()) ModelFile = DisplayInfo->ModelNames[0];
        if (TextureFile.IsEmpty()) TextureFile = DisplayInfo->ModelTextures[0];
    }
    else
    {
        ModelFile = DisplayInfo->ModelNames[0];
        TextureFile = DisplayInfo->ModelTextures[0];
    }

    if (ModelFile.IsEmpty())
    {
        return false;
    }

    const TArray<FString> CandidateDirs = GetCandidateDirs(AttachPoint);
    if (!TryResolveAssetPath(Mpq, ModelFile, TEXT(".m2"), CandidateDirs, OutModelPath))
    {
        return false;
    }

    if (OutTexturePath)
    {
        OutTexturePath->Reset();
        if (!TextureFile.IsEmpty())
        {
            TryResolveAssetPath(Mpq, TextureFile, TEXT(".blp"), CandidateDirs, *OutTexturePath);
        }
    }

    return true;
}

USceneComponent* FWowEquipmentManager::AttachEquipment(FMpqManager* Mpq, FWowAssetCache* Cache,
    uint32 ItemDisplayId, EAttachmentPoint AttachPoint,
    USkeletalMeshComponent* CharacterMesh, const FM2Data& CharacterM2, uint32 EnchantmentVisualId)
{
    return AttachEquipmentImpl(Mpq, Cache, ItemDisplayId, AttachPoint, CharacterMesh, &CharacterM2, NAME_None, EnchantmentVisualId);
}

USceneComponent* FWowEquipmentManager::AttachItemToSocket(FMpqManager* Mpq, FWowAssetCache* Cache,
    uint32 ItemDisplayId, USkeletalMeshComponent* CharacterMesh, FName Socket)
{
    // r398 (torch on the belt): the socket already exists on the built skeleton, so no M2 data needed.
    if (!CharacterMesh || Socket.IsNone() || !CharacterMesh->DoesSocketExist(Socket)) return nullptr;
    return AttachEquipmentImpl(Mpq, Cache, ItemDisplayId, EAttachmentPoint::HipWeaponLeft, CharacterMesh, nullptr, Socket, 0);
}

USceneComponent* FWowEquipmentManager::AttachEquipmentImpl(FMpqManager* Mpq, FWowAssetCache* Cache,
    uint32 ItemDisplayId, EAttachmentPoint AttachPoint,
    USkeletalMeshComponent* CharacterMesh, const FM2Data* CharacterM2Ptr, FName ForcedSocket, uint32 EnchantmentVisualId)
{
    if (!Mpq || !CharacterMesh) return nullptr;

    FString ModelPath;
    FString OverrideTexturePath;
    if (!ResolveEquipmentPaths(Mpq, ItemDisplayId, AttachPoint, ModelPath, &OverrideTexturePath))
    {
        UE_LOG(LogWowEquip, Verbose, TEXT("No equipment visual for display %d at attachment %d"),
            ItemDisplayId, static_cast<uint32>(AttachPoint));
        return nullptr;
    }

    FName BoneName = !ForcedSocket.IsNone() ? ForcedSocket
        : (CharacterM2Ptr ? GetAttachmentBoneName(*CharacterM2Ptr, AttachPoint) : FName(NAME_None));
    if (BoneName == NAME_None)
    {
        UE_LOG(LogWowEquip, Warning, TEXT("Cannot find bone for attachment point %d"),
            static_cast<uint32>(AttachPoint));
        return nullptr;
    }

    // Load the weapon M2
    TArray<uint8> M2Raw;
    if (!Mpq->ReadFile(ModelPath, M2Raw))
    {
        UE_LOG(LogWowEquip, Warning, TEXT("Failed to read equipment M2: %s"), *ModelPath);
        return nullptr;
    }

    FString SkinPath = ModelPath;
    if (SkinPath.EndsWith(TEXT(".m2"), ESearchCase::IgnoreCase))
        SkinPath = SkinPath.Left(SkinPath.Len() - 3);
    SkinPath += TEXT("00.skin");

    TArray<uint8> SkinRaw;
    Mpq->ReadFile(SkinPath, SkinRaw);

    FM2Data WeaponData = FM2Parser::Parse(M2Raw, SkinRaw);
    if (!WeaponData.bIsValid)
    {
        UE_LOG(LogWowEquip, Warning, TEXT("Failed to parse equipment M2: %s"), *ModelPath);
        return nullptr;
    }

    // Build a static mesh for the weapon (weapons don't animate independently)
    // Using the existing mesh building pipeline from doodad manager
    FString NormPath = ModelPath.ToLower();
    NormPath.ReplaceInline(TEXT("/"), TEXT("\\"));
    UStaticMesh* WeaponMesh = Cache ? Cache->FindMesh(NormPath) : nullptr;

    if (!WeaponMesh)
    {
        // Build static mesh from M2 data
        FMeshDescription MeshDesc;
        FStaticMeshAttributes MeshAttribs(MeshDesc);
        MeshAttribs.Register();

        if (WeaponData.Vertices.Num() > 0 && WeaponData.Indices.Num() > 0)
        {
            MeshDesc.ReserveNewVertices(WeaponData.Vertices.Num());
            FPolygonGroupID PGId = MeshDesc.CreatePolygonGroup();

            TVertexAttributesRef<FVector3f> Positions = MeshAttribs.GetVertexPositions();
            TVertexInstanceAttributesRef<FVector3f> Normals = MeshAttribs.GetVertexInstanceNormals();
            TVertexInstanceAttributesRef<FVector2f> UVs = MeshAttribs.GetVertexInstanceUVs();

            const TArray<FEquipmentSection> EquipSections = GetEquipmentSections(WeaponData);
            TArray<FPolygonGroupID> SectionGroups;
            // r429 (Stefan: the belt torch had no flame and a flat plate around it, seen only from the
            // front and above): r418 dropped the torch's glow card from the sections, which left one
            // section - and with fewer than two the whole index buffer was drawn, card included, as an
            // opaque one-sided square of the item texture hiding the flame. Draw only the sections.
            if (EquipSections.Num() >= 1)
            {
                SectionGroups.Add(PGId);
                for (int32 Extra = 1; Extra < EquipSections.Num(); ++Extra) SectionGroups.Add(MeshDesc.CreatePolygonGroup());
            }
            TArray<FVertexID> VertexIds;
            VertexIds.SetNum(WeaponData.Vertices.Num());
            for (int32 v = 0; v < WeaponData.Vertices.Num(); v++)
            {
                VertexIds[v] = MeshDesc.CreateVertex();
                const FM2Vertex& Vertex = WeaponData.Vertices[v];
                Positions[VertexIds[v]] = FVector3f(
                    Vertex.Position.Y * FWowCoordinate::SCALE,
                    Vertex.Position.X * FWowCoordinate::SCALE,
                    Vertex.Position.Z * FWowCoordinate::SCALE);
            }

            auto AddTriangles = [&](int32 First, int32 End, FPolygonGroupID Group)
            {
            for (int32 t = First; t + 2 < End; t += 3)
            {
                TArray<FVertexInstanceID> Insts;
                for (int32 j = 0; j < 3; j++)
                {
                    int32 Idx = WeaponData.Indices[t + j];
                    if (Idx >= WeaponData.Vertices.Num()) continue;
                    FVertexInstanceID VI = MeshDesc.CreateVertexInstance(VertexIds[Idx]);
                    const FM2Vertex& Vertex = WeaponData.Vertices[Idx];
                    FVector3f Normal(Vertex.Normal.Y, Vertex.Normal.X, Vertex.Normal.Z);
                    Normal.Normalize();
                    Normals[VI] = Normal;
                    UVs[VI] = FVector2f(Vertex.TexCoord.X, Vertex.TexCoord.Y);
                    Insts.Add(VI);
                }
                if (Insts.Num() == 3)
                {
                    MeshDesc.CreatePolygon(Group, Insts);
                }
            }
            };
            if (SectionGroups.Num() >= 1)
                for (int32 Section = 0; Section < EquipSections.Num(); ++Section)
                    AddTriangles(EquipSections[Section].Start, EquipSections[Section].Start + EquipSections[Section].Count, SectionGroups[Section]);
            else
                AddTriangles(0, WeaponData.Indices.Num(), PGId);

            WeaponMesh = NewObject<UStaticMesh>();
            // One material slot per section (WowPrepareRuntimeMeshMaterials names them in order).
            for (int32 Slot = 0; Slot < SectionGroups.Num(); ++Slot)
                WeaponMesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
            TArray<const FMeshDescription*> MeshDescs;
            MeshDescs.Add(&MeshDesc);
            UStaticMesh::FBuildMeshDescriptionsParams Params;
            Params.bFastBuild = true;
            WowPrepareRuntimeMeshMaterials(WeaponMesh, const_cast<FMeshDescription&>(*MeshDescs[0]));
            WeaponMesh->BuildFromMeshDescriptions(MeshDescs, Params);
            WowBuildRuntimeLumenCards(WeaponMesh, MeshDesc);
            WeaponMesh->NeverStream = true;
            WeaponMesh->InitResources();
            if (WeaponMesh->GetStaticMaterials().Num() == 0)
            {
                WeaponMesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
            }
            for (FStaticMaterial& Mat : WeaponMesh->GetStaticMaterials())
            {
                Mat.UVChannelData.bInitialized = true;
            }

            if (Cache) Cache->CacheMesh(NormPath, WeaponMesh);
        }
    }

    if (!WeaponMesh) return nullptr;

    // Attach to character bone
    AActor* Owner = CharacterMesh->GetOwner();
    // Both shoulders share a display ID and owner. Reusing an explicit UObject
    // name replaces the previous component, making the first shoulder disappear.
    // Unique names also protect identical dual-wield items and appearance rebuilds.
    const FName EquipmentName = MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(),
        *FString::Printf(TEXT("Equip_%u_%u"), ItemDisplayId, static_cast<uint32>(AttachPoint)));
    UStaticMeshComponent* WeaponComp = NewObject<UStaticMeshComponent>(Owner, EquipmentName);
    Owner->AddInstanceComponent(WeaponComp);
    WeaponComp->SetStaticMesh(WeaponMesh);
    const bool bStaff = ModelPath.Contains(TEXT("Stave_"), ESearchCase::IgnoreCase);
    if (bStaff)
    {
        const FName HandSocket = AttachPoint == EAttachmentPoint::LeftHand ? TEXT("WowSpellLeftHand") : TEXT("WowSpellRightHand");
        if (CharacterMesh->DoesSocketExist(HandSocket)) BoneName = HandSocket;
        WeaponComp->ComponentTags.AddUnique(TEXT("WowStaffWeapon"));
        ApplyDrawnTransform(WeaponComp);
    }
    WeaponComp->SetupAttachment(CharacterMesh, BoneName);
	if (AttachPoint == EAttachmentPoint::Helmet)
	{
		WeaponComp->ComponentTags.AddUnique(TEXT("WowHeadEquipment"));
	}
	else if (AttachPoint == EAttachmentPoint::LeftShoulder ||
		AttachPoint == EAttachmentPoint::RightShoulder)
	{
		WeaponComp->ComponentTags.AddUnique(TEXT("WowShoulderEquipment"));
	}
    WeaponComp->SetCastShadow(true);
    WeaponComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Attachment Position is in MODEL SPACE, not bone-relative.
    // The bone already provides the correct animated position.
    // Equipment attaches directly at the bone origin.
    WeaponComp->SetRelativeLocation(FVector::ZeroVector);

    WeaponComp->RegisterComponent();

    UTexture2D* EquipmentTexture = nullptr;
    if (!OverrideTexturePath.IsEmpty())
    {
        EquipmentTexture = LoadBlpTexture(Mpq, Cache, OverrideTexturePath);
    }

    if (!EquipmentTexture && WeaponData.TexturePaths.Num() > 0 && !WeaponData.TexturePaths[0].IsEmpty())
    {
        FString TexPath = WeaponData.TexturePaths[0];
        if (!TexPath.EndsWith(TEXT(".blp"), ESearchCase::IgnoreCase))
        {
            TexPath += TEXT(".blp");
        }
        EquipmentTexture = LoadBlpTexture(Mpq, Cache, TexPath);
    }

    // One-section items: the first pass decides whether the texture's alpha cuts out (r422).
    ApplyEquipmentMaterial(WeaponComp, EquipmentTexture, WeaponData.RenderPasses.Num() > 0 && WeaponData.RenderPasses[0].BlendMode == 1);
    ApplyEquipmentSectionMaterials(WeaponComp, WeaponData, EquipmentTexture, Mpq, Cache);
    const FItemDisplayInfoDbcEntry* Display = FDbcStore::Get().ItemDisplayInfo().GetById(ItemDisplayId);
    const uint32 IntrinsicVisual = Display ? Display->ItemVisual : 0;
    WeaponComp->ComponentTags.Add(FName(*(TEXT("WowEquipmentModel=") + ModelPath)));
    WeaponComp->ComponentTags.Add(FName(*FString::Printf(TEXT("WowIntrinsicVisual=%u"), IntrinsicVisual)));
    WeaponComp->ComponentTags.Add(FName(*FString::Printf(TEXT("WowEnchantmentVisual=%u"), EnchantmentVisualId)));
    AddEquipmentParticleEffect(WeaponComp, WeaponData, Mpq, Cache, FVector::ZeroVector, TEXT("WowIntrinsicEffect"));
    AddEquipmentItemVisual(WeaponComp, WeaponData, IntrinsicVisual, Mpq, Cache, TEXT("WowIntrinsicEffect"));
    if (EnchantmentVisualId && EnchantmentVisualId != IntrinsicVisual)
        AddEquipmentItemVisual(WeaponComp, WeaponData, EnchantmentVisualId, Mpq, Cache, TEXT("WowEnchantmentEffect"));

    UE_LOG(LogWowEquip, Log, TEXT("Attached equipment %d (%s) to bone %s"),
        ItemDisplayId, *ModelPath, *BoneName.ToString());

    return WeaponComp;
}


uint32 FWowEquipmentManager::ResolveEnchantmentVisual(FMpqManager* Mpq, uint32 PackedEnchantments)
{
    const auto& Table = GetEquipmentVisualTables(Mpq).Enchantments;
    const uint32 Temporary = Table.FindRef(PackedEnchantments >> 16);
    return Temporary ? Temporary : Table.FindRef(PackedEnchantments & 0xffff);
}

void FWowEquipmentManager::RestoreEquipmentEffects(FMpqManager* Mpq, FWowAssetCache* Cache, USceneComponent* Equipment)
{
    if (!Equipment) return;
    FM2Data Data;
    if (!ReadEquipmentEffectModel(Mpq, EquipmentTagValue(Equipment, TEXT("WowEquipmentModel=")), Data)) return;
    const uint32 Intrinsic = FCString::Atoi(*EquipmentTagValue(Equipment, TEXT("WowIntrinsicVisual=")));
    const uint32 Enchantment = FCString::Atoi(*EquipmentTagValue(Equipment, TEXT("WowEnchantmentVisual=")));
    AddEquipmentParticleEffect(Equipment, Data, Mpq, Cache, FVector::ZeroVector, TEXT("WowIntrinsicEffect"));
    AddEquipmentItemVisual(Equipment, Data, Intrinsic, Mpq, Cache, TEXT("WowIntrinsicEffect"));
    if (Enchantment && Enchantment != Intrinsic)
        AddEquipmentItemVisual(Equipment, Data, Enchantment, Mpq, Cache, TEXT("WowEnchantmentEffect"));
}


void FWowEquipmentManager::UpdateEquipmentEnchantment(FMpqManager* Mpq, FWowAssetCache* Cache,
    USceneComponent* Equipment, uint32 VisualId)
{
    if (!Equipment) return;
    const FString Previous = EquipmentTagValue(Equipment, TEXT("WowEnchantmentVisual="));
    if (static_cast<uint32>(FCString::Atoi(*Previous)) == VisualId) return;
    Equipment->ComponentTags.Remove(FName(*(TEXT("WowEnchantmentVisual=") + Previous)));
    Equipment->ComponentTags.Add(FName(*FString::Printf(TEXT("WowEnchantmentVisual=%u"), VisualId)));
    TArray<USceneComponent*> Children;
    Equipment->GetChildrenComponents(false, Children);
    for (USceneComponent* Child : Children)
        if (Child->ComponentHasTag(TEXT("WowEnchantmentEffect")))
        {
            if (UWowM2ParticleComponent* Particles = Cast<UWowM2ParticleComponent>(Child)) Particles->ClearParticles();
            Child->DestroyComponent();
        }
    const uint32 Intrinsic = FCString::Atoi(*EquipmentTagValue(Equipment, TEXT("WowIntrinsicVisual=")));
    if (!VisualId || VisualId == Intrinsic) return;
    FM2Data Data;
    if (ReadEquipmentEffectModel(Mpq, EquipmentTagValue(Equipment, TEXT("WowEquipmentModel=")), Data))
        AddEquipmentItemVisual(Equipment, Data, VisualId, Mpq, Cache, TEXT("WowEnchantmentEffect"));
}

void FWowEquipmentManager::ApplyDrawnTransform(UStaticMeshComponent* Equipment)
{
    Equipment->SetRelativeTransform(FTransform::Identity);
    // User reference: tip the drawn staff down 90 degrees around the palm.
    // Grip origin stays fixed; the back pose remains independent.
    if (Equipment->ComponentHasTag(TEXT("WowStaffWeapon")))
        Equipment->SetRelativeRotation(FRotator(0.0f, 0.0f, 15.0f));
}

// r549 (Stefan: the two-hand sword "turned" - diagonal after login, nearly vertical after taking it off
// and on again, "we didn't touch that code"): the sheath pose was worked out from the back socket in the
// CURRENT animation pose (GetSocketTransform), so it changed with whatever the body was doing at that
// moment (idle sway, the equip or cast animation). Use the socket in the skeleton's reference pose: the
// same result every time.
static FTransform WowRefPoseSocketTransform(USkeletalMeshComponent* CharacterMesh, FName SocketName)
{
    const USkeletalMesh* Mesh = CharacterMesh ? CharacterMesh->GetSkeletalMeshAsset() : nullptr;
    const USkeletalMeshSocket* Socket = Mesh ? Mesh->FindSocket(SocketName) : nullptr;
    if (!Socket) return CharacterMesh ? CharacterMesh->GetSocketTransform(SocketName, RTS_Component) : FTransform::Identity;
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    int32 Bone = Ref.FindBoneIndex(Socket->BoneName);
    if (Bone == INDEX_NONE) return CharacterMesh->GetSocketTransform(SocketName, RTS_Component);
    const TArray<FTransform>& Pose = Ref.GetRefBonePose();
    FTransform BoneCS = Pose[Bone];
    for (int32 Parent = Ref.GetParentIndex(Bone); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
        BoneCS = BoneCS * Pose[Parent];
    return Socket->GetSocketLocalTransform() * BoneCS;
}

void FWowEquipmentManager::ApplySheathedTransform(UStaticMeshComponent* Equipment, USkeletalMeshComponent* CharacterMesh)
{
    Equipment->SetRelativeTransform(FTransform::Identity);
    if (!Equipment->ComponentHasTag(TEXT("WowSheathTwoHandWeapon")))
    {
        // r304 (Stefan, 1H sword on the back): the authored back sockets 26/27 sit a hand's
        // width behind the body and high, so the sword stood off the back with its hilt over
        // the shoulder pad. Bring one-handers on the back in against the body and lower.
        if (CharacterMesh && (Equipment->ComponentHasTag(TEXT("WowSheathMainHand")) || Equipment->ComponentHasTag(TEXT("WowSheathOffHand"))))
        {
            const FTransform BackSocket = WowRefPoseSocketTransform(CharacterMesh, Equipment->GetAttachSocketName());
            // r323 (Stefan: now it hangs too low): only 3 cm below the authored socket.
            // r348 (Stefan: "a little lower and a little to the left"): 6 cm down, 4 cm toward the spine
            // (+X is the character's left; models face +Y).
            // r402 (Stefan, back and side views of a 1H sword: "a little too far left and too far out
            // from the body", the blade leaning away from the back): lay the blade flat on the back
            // (its long axis loses the front/back component), then 6 cm further right (X 4 -> -2)
            // and 12 cm closer to the back (Y 9 -> 21; +Y is toward the body, models face +Y).
            if (const UStaticMesh* Mesh = Equipment->GetStaticMesh())
            {
                const FVector Extent = Mesh->GetBounds().BoxExtent;
                const FVector LongLocal = Extent.Z > Extent.X && Extent.Z > Extent.Y ? FVector::UpVector :
                    (Extent.Y > Extent.X ? FVector::RightVector : FVector::ForwardVector);
                const FQuat SocketRotation = BackSocket.GetRotation();
                const FVector Long = SocketRotation.RotateVector(LongLocal);
                // r421 (Stefan, screenshot from behind: the 1H sword still slants too far to the left - its
                // tip hangs out past the left hip): the blade hangs much steeper, less than half of its
                // authored sideways slant, so it stays on the back from the right shoulder down.
                const FVector Flat = FVector(Long.X * 0.4, 0.0, Long.Z).GetSafeNormal();
                if (!Flat.IsNearlyZero())
                {
                    const FQuat Delta = FQuat::FindBetweenNormals(Long.GetSafeNormal(), Flat);
                    Equipment->SetRelativeRotation(SocketRotation.Inverse() * Delta * SocketRotation);
                }
            }
            Equipment->SetRelativeLocation(BackSocket.InverseTransformVector(FVector(-2.0f, 21.0f, -6.0f)));
        }
        return;
    }
    const bool bSword = Equipment->ComponentHasTag(TEXT("WowSheathTwoHandSword"));
    const bool bAxe = Equipment->ComponentHasTag(TEXT("WowSheathTwoHandAxe"));
    const bool bStaff = Equipment->ComponentHasTag(TEXT("WowStaffWeapon")) ||
        Equipment->ComponentHasTag(TEXT("WowSheathFishingPole")) || // poles sit on the staff socket (r256)
        Equipment->ComponentHasTag(TEXT("WowSheathPolearm")); // r338: polearms hang like staves
    // r693 (Stefan, a blood elf priest's staff on the back: "a bit too upright, needs to be a little more to the
    // right"): staves lean 27 degrees instead of 15.
    // r710 (Stefan, the same staff from behind: "needs to be twisted right a bit more. And also I think I made the
    // mistake of turning the skull forward, when it's supposed to be towards the camera in the WoW client"): staves
    // lean 35 degrees and turn half round on their own length, so the head's face looks back, out from the back.
    // Fishing poles and polearms (they share the staff socket) keep 27 degrees and their side.
    const bool bRealStaff = Equipment->ComponentHasTag(TEXT("WowStaffWeapon"));
    // r713 (Stefan on r710 from behind, the head over her left shoulder: "Top the right? This is the left?"): 15, 27
    // and 35 degrees all leaned the head further to the character's left; about 20 is upright here. Staves now lean
    // the other way, the head over the right shoulder, as far from upright as before (0 degrees).
    // r729 (Stefan, an OG screenshot of a blood elf's staff from behind: "I think we've got it close?"): the OG has
    // the head over her LEFT shoulder, about 24 degrees from upright, the foot toward the right hip - ours was the
    // mirror image. With about 20 upright, staves now lean 42 degrees; the r710 half turn (skull facing back) stays.
    FQuat Rotation = FRotator(0.0f, 0.0f, (bSword || bAxe) ? 240.0f : (bRealStaff ? 42.0f : (bStaff ? 27.0f : -75.0f))).Quaternion();
    if (bRealStaff && Equipment->GetStaticMesh())
    {
        const FVector Extent = Equipment->GetStaticMesh()->GetBounds().BoxExtent;
        const FVector LongAxis = Extent.Z > Extent.X && Extent.Z > Extent.Y ? FVector::UpVector :
            (Extent.Y > Extent.X ? FVector::RightVector : FVector::ForwardVector);
        Rotation *= FQuat(LongAxis, PI);
    }
    const FTransform Socket = WowRefPoseSocketTransform(CharacterMesh, Equipment->GetAttachSocketName());
    // r686 (Stefan, the axe from behind and from the side after r678: the head far out past the left hip and the
    // whole axe standing off the back - "follow the back, and point the axe top tip like 45 degrees"): the log of
    // r678 showed why. The pose was worked out in the skeleton's reference pose, but in the animated pose the back
    // socket is turned almost a right angle from it (long axis wanted (-0.50, 0, 0.87), drawn (0.72, -0.58, 0.38)).
    // Two-hand axes, hammers and maces are now placed in the body's own frame from the live socket, every frame
    // (GC re-applies it while sheathed): the head end down on the socket's side, the handle up over the other
    // shoulder, 45 degrees from vertical, the blade flat against the back; offsets toward the spine, in against
    // the body and down are in the body frame too.
    // r716 (Stefan, a human warrior's two-hand sword lying almost level across the back, the tip far out past the
    // left hip): swords had the same reference-pose problem r686 fixed for the axes. They are placed the same way
    // now, from the live socket in the body's frame: hilt up over the shoulder, tip down toward the other hip,
    // 30 degrees from vertical, the blade flat against the back, the grip on the socket.
    if ((bAxe || bSword) && CharacterMesh && Equipment->GetStaticMesh())
    {
        const FBoxSphereBounds Bounds = Equipment->GetStaticMesh()->GetBounds();
        const FVector Extent = Bounds.BoxExtent;
        FVector HeadLocal = Extent.Z > Extent.X && Extent.Z > Extent.Y ? FVector::UpVector :
            (Extent.Y > Extent.X ? FVector::RightVector : FVector::ForwardVector);
        // The grip is the model's origin: the head is the end that reaches further from it.
        if (FVector::DotProduct(Bounds.Origin, HeadLocal) < 0.0) HeadLocal = -HeadLocal;
        FVector ThinLocal = FVector::ForwardVector; double Thinnest = Extent.X;
        if (Extent.Y < Thinnest) { Thinnest = Extent.Y; ThinLocal = FVector::RightVector; }
        if (Extent.Z < Thinnest) { Thinnest = Extent.Z; ThinLocal = FVector::UpVector; }
        const FName SocketName = Equipment->GetAttachSocketName();
        const FTransform Live = CharacterMesh->GetSocketTransform(SocketName, RTS_Component);
        const double Side = Socket.GetLocation().X >= 0.0 ? 1.0 : -1.0; // +X is the character's left
        const double Tilt = FMath::DegreesToRadians(bSword && !bAxe ? 30.0 : 45.0);
        const FVector Thin(0.0, 1.0, 0.0); // blade flat on the back: its thickness points front/back
        auto BodyRotationFor = [&HeadLocal, &ThinLocal, &Thin](const FVector& Head)
        {
            const FQuat Align = FQuat::FindBetweenNormals(HeadLocal, Head);
            const FVector ThinNow = Align.RotateVector(ThinLocal);
            const double Twist = FMath::Atan2(FVector::DotProduct(Head, FVector::CrossProduct(ThinNow, Thin)), FVector::DotProduct(ThinNow, Thin));
            return FQuat(Head, Twist) * Align;
        };
        // r693 (Stefan, a night elf's two-hander from behind on the r686 build: "this is also turned the wrong way
        // now"): r686 hung the head down by the hip. The head goes up now, over the shoulder on the other side,
        // the handle down toward the socket's hip - the same 45-degree line, end for end. The weapon's middle stays
        // where r686 put it, so the head does not stand a metre above the shoulder.
        const FVector HeadDown(Side * FMath::Sin(Tilt), 0.0, -FMath::Cos(Tilt));
        if (bSword && !bAxe)
        {
            // The blade is the end reaching furthest from the grip (HeadLocal); it points down across the back
            // to the other side, the hilt up by this socket's shoulder.
            // r726 (Stefan, from behind on the r716 build: "Sword is going the wrong way, mirror it so it points to
            // the left instead"): mirrored across the spine - the hilt over the other shoulder, the tip down toward
            // this socket's (left) hip. The grip moves to the socket's mirror image across the body's middle
            // (component X = 0), with the same offsets toward the spine and in against the back.
            const FVector TipDown(Side * FMath::Sin(Tilt), 0.0, -FMath::Cos(Tilt));
            const FQuat SwordRotation = BodyRotationFor(TipDown);
            Equipment->SetRelativeRotation(Live.GetRotation().Inverse() * SwordRotation);
            // Same body offsets as the axes but only 3 cm down (r549): toward the spine, in against the back.
            const FVector Mirror(-2.0 * Live.GetLocation().X, 0.0, 0.0);
            Equipment->SetRelativeLocation(Live.InverseTransformVector(Mirror + FVector(Side * 6.0, 7.0, -3.0)));
            return;
        }
        // r714 (Stefan, a night elf's two-hand mace: "needs to have the head in the other end, and also I think this
        // needs to be turned to get the 'cogs' down and upwards rather than inwards and outwards"): maces and hammers
        // keep the head down by the hip (as before r693) and turn a quarter round their length, so the flanges lie
        // along the back instead of pointing into it and out from it. Axes keep the head up, blade flat.
        const bool bMace = Equipment->ComponentHasTag(TEXT("WowSheathTwoHandMace"));
        // r720 (Stefan, a human's two-hand axe from behind, head up over the shoulder: "Axes are also the wrong way
        // around, like the 2h maces"): axes hang head down by the hip again too (r686), blade flat on the back.
        const FQuat BodyRotation = bMace ? FQuat(HeadDown, HALF_PI) * BodyRotationFor(HeadDown) : BodyRotationFor(HeadDown);
        const FVector Centre = Bounds.Origin * Equipment->GetRelativeScale3D();
        // Same offsets as before (r267/r304/r250): 14 cm toward the spine, 7 cm in against the body, 8 cm down.
        // r729 (Stefan, an OG screenshot of a two-hand axe from behind: "This is how high the axe sits, ours sits sooo
        // low"): the OG has the head at the hip and the handle reaching well above the opposite shoulder. Axes, maces
        // and hammers slide 25 cm up along their own line (about 18 cm up and 18 cm toward the other shoulder).
        constexpr double SlideUp = 25.0;
        const FVector OldCentre = FVector(-Side * 14.0, 7.0, -8.0) - HeadDown * SlideUp + BodyRotationFor(HeadDown).RotateVector(Centre);
        Equipment->SetRelativeRotation(Live.GetRotation().Inverse() * BodyRotation);
        Equipment->SetRelativeLocation(Live.InverseTransformVector(OldCentre - BodyRotation.RotateVector(Centre)));
        return;
    }
    // Two-hand axes turn the cutting edge outward like swords (r250, Stefan: the blade faced in).
    if ((bSword || bAxe) && Equipment->GetStaticMesh())
    {
        const FVector Extent = Equipment->GetStaticMesh()->GetBounds().BoxExtent;
        const FVector LongAxis = Extent.Z > Extent.X && Extent.Z > Extent.Y ? FVector::UpVector :
            (Extent.Y > Extent.X ? FVector::RightVector : FVector::ForwardVector);
        Rotation *= FQuat(LongAxis, PI);
        // r425 (Stefan, side view of his two-hand sword: "too far from the body STILL"): the blade left
        // the socket leaning backwards, so its tip stood half a metre off the back. Lay it flat on the
        // back like the one-handers (r402): the long axis keeps its slant across the back and loses its
        // front/back component (component +Y is the character's front).
        const FQuat SocketRotation = Socket.GetRotation();
        const FVector Long = SocketRotation.RotateVector(Rotation.RotateVector(LongAxis)).GetSafeNormal();
        // r517 (Stefan, a tauren's Sword_2H_PvEHorde_D_01 on the right back socket lay almost level across
        // the back: "tip needs to point more towards the ground"): two-hand swords hang steeper, about half
        // of their authored sideways slant (like the one-handers in r421), so the tip points down past the hip.
        // r564 (Stefan, after r549 made the pose the same every time: the sword now always lay at the
        // flat "after login" angle, tip far out to the left hip): r549 measures in the skeleton's reference
        // pose, where the back socket slants more than in the idle pose the old value was tuned on. Two-hand
        // swords keep a fifth of the slant now, about 30 degrees from vertical, hilt up by the right shoulder.
        // r673 (Stefan, a human warrior's 2H axe lying sideways across the back: "wasn't this sideways last time I
        // checked"): r564 only gave swords the steeper slant; axes kept the full reference-pose slant that r549
        // brought in. The OG reference (sheathed-2haxe-back.png) hangs the axe like the sword, handle up by the
        // right shoulder, head down - axes keep a fifth of the slant too.
        // r678 (Stefan, the 17.10 build, his Axe_2H_Horde_D_04 from behind: "no, this is even worse" - it lay about
        // 30 degrees from level, head far out past the left hip): scaling the slant does nothing useful when the
        // long axis hardly points up or down at all (the axe lay almost level, so a fifth of almost nothing is still
        // level). Axes (and the two-hand hammers and maces that hang like them) now get their direction outright:
        // 30 degrees from vertical across the back, head down on the side it already hung on - the OG reference.
        FVector Flat;
        if (bAxe)
        {
            const double Side = Long.X >= 0.0 ? 1.0 : -1.0;
            const double Down = Long.Z > 0.05 ? 1.0 : -1.0;
            Flat = FVector(Side * 0.577, 0.0, Down).GetSafeNormal();
        }
        else
            Flat = FVector(Long.X * (bSword ? 0.2 : 1.0), 0.0, Long.Z).GetSafeNormal();
        if (!Flat.IsNearlyZero())
            Rotation = SocketRotation.Inverse() * FQuat::FindBetweenNormals(Long, Flat) * SocketRotation * Rotation;
    }
    // r336 (Stefan: the fishing pole still pointed the wrong way, from the right shoulder up and
    // forward): a staff is held in the middle, a fishing pole at its butt end (model X -0.3..1.8).
    // Turn the pole end for end so its tip goes where a staff's head goes, and centre it on the
    // back like a staff instead of letting the whole pole stick out from the socket.
    FVector PoleCentre = FVector::ZeroVector;
    if (Equipment->ComponentHasTag(TEXT("WowSheathFishingPole")))
    {
        Rotation *= FQuat(FVector::UpVector, PI);
        if (const UStaticMesh* Mesh = Equipment->GetStaticMesh()) PoleCentre = Mesh->GetBounds().Origin;
    }
    Equipment->SetRelativeRotation(Rotation);
    // M2 forward is +X, converted to +Y in skeletal-mesh space. Keep the
    // clearance bone-relative so it follows the chest during both idle and casting.
    // The staff's authored socket already sits behind the torso. It only needs
    // a small gap; the generic two-hand offset made it float away from the back.
    // r304 (Stefan: 2H axe and fishing pole stand a hand's width off the back): the large-weapon
    // and staff sockets are already behind the body, so pull them in (negative = toward the body).
    // r313 (Stefan, polearm side view): polearms and 2H maces kept the old +15 cm generic offset and
    // stood a hand's width off the back; they now sit as close as the 2H axes and swords.
    const float Clearance = (bSword || bAxe) ? -7.0f : (bStaff ? -10.0f : -7.0f); // r305: OG staves lie flush on the back
    // r250 lowered swords and axes 16 cm; the too-high swords were really a wrong socket (fixed
    // in r248), and a normal two-hand sword then hung with its hilt mid-back (Stefan, r264).
    // Swords keep the authored height; axes stay 8 cm lower.
    // r428 (Stefan, from behind on the 01:27 build: "it must go further down and to the left"): the
    // two-hand sword's hilt stood above the right shoulder pad; swords now sit 15 cm lower.
    // r454 (Stefan: "sword still hangs a bit low", then "up and right a little"): 15 cm was too much;
    // swords sit 7 cm lower, and on the left back socket 5 cm toward the character's right.
    // r549 (Stefan: "up and to the right a bit more"): swords 4 cm higher (7 -> 3).
    const float Drop = bAxe ? 8.0f : 3.0f;
    // Two-hand swords/axes move toward the spine (r267, Stefan): a pair on both back sockets
    // spread out in a V; pulled 14 cm toward the middle they cross like the original, and a
    // single one sits more centred. Socket X tells which side of the body it is on.
    // r425 moved a single two-hander on the left back socket 8 cm further right (Stefan, side view);
    // r428 (Stefan, from behind: "further down and to the left"): the hilt sat over the right shoulder
    // pad, so a sword on that socket goes back 14 cm to the left of r267's place, hilt next to the neck
    // instead of on the shoulder (-X is the character's right). Axes keep r267's -14.
    // r549: a sword on the left back socket 4 cm further to the character's right (-5 -> -9).
    const float TowardSpine = (bSword || bAxe) ? (Socket.GetLocation().X >= 0.0 ? (bSword ? -9.0f : -14.0f) : 14.0f) : 0.0f;
    Equipment->SetRelativeLocation(Socket.InverseTransformVector(FVector(TowardSpine, -Clearance, -Drop))
        - Rotation.RotateVector(PoleCentre)); // r336: centre the pole on the back
}


void FWowEquipmentManager::PrepareStandaloneMaterials()
{
    GetEquipmentMaterial();
    for (uint16 Blend = 0; Blend < 4; ++Blend) GetItemEffectMaterial(Blend);
}

void FWowEquipmentManager::ClearCaches()
{
    GEquipmentVisualTables.Source = nullptr;
}
