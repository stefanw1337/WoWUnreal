#include "WowDeathManager.h"
#include "WowGameplayController.h"
#include "WowPlayerCharacter.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "WowCharacterBuilder.h"
#include "WowAnimationController.h"
#include "WowCookedMaterial.h"
#include "WowSkyManager.h"
#include "Coord/WowCoordinate.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowDeath, Log, All);

namespace
{
constexpr uint32 PLAYER_FLAGS_GHOST = 0x10;
// Corpse update fields (3.3.5a, offsets from OBJECT_END).
constexpr uint16 CORPSE_ITEM = ObjectField::END + 0x05;     // 19: display id | inventory type << 24
constexpr uint16 CORPSE_BYTES_1 = ObjectField::END + 0x18;  // -, race, gender, skin
constexpr uint16 CORPSE_BYTES_2 = ObjectField::END + 0x19;  // face, hair style, hair colour, facial hair
constexpr uint16 CORPSE_FLAGS = ObjectField::END + 0x1B;
constexpr uint32 CORPSE_FLAG_BONES = 0x01;
constexpr int32 MaxCorpseActors = 16;

// Spirit rim: soft cold glow at grazing angles with a slow breath, drawn over the body.
const TCHAR* GhostShader = TEXT(R"(
float3 N = normalize(Parameters.WorldNormal);
float3 V = normalize(Parameters.CameraVector);
float rim = pow(1 - saturate(abs(dot(N, V))), 2.4);
float breath = 0.82 + 0.18 * sin(TimeSec * 1.6);
return Color.rgb * (0.10 + rim * 1.25) * breath * Intensity;
)");

UMaterialInterface* GhostOverlayMaterial()
{
    const FString Name = TEXT("M_WowGhostOverlay");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->SetShadingModel(MSM_Unlit);
        Material->BlendMode = BLEND_Additive;
        Material->TwoSided = false;
        auto* Time = NewObject<UMaterialExpressionTime>(Material);
        auto* Intensity = NewObject<UMaterialExpressionScalarParameter>(Material);
        Intensity->ParameterName = TEXT("Intensity");
        Intensity->DefaultValue = 1.f;
        auto* Color = NewObject<UMaterialExpressionVectorParameter>(Material);
        Color->ParameterName = TEXT("Color");
        Color->DefaultValue = FLinearColor(0.55f, 0.75f, 1.0f);
        auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
        Shader->OutputType = CMOT_Float3;
        Shader->Inputs.Reset();
        const auto Input = [Shader](const TCHAR* InputName, UMaterialExpression* Expression)
        {
            FCustomInput Value;
            Value.InputName = InputName;
            Value.Input.Connect(0, Expression);
            Shader->Inputs.Add(Value);
        };
        Input(TEXT("TimeSec"), Time);
        Input(TEXT("Intensity"), Intensity);
        Input(TEXT("Color"), Color);
        Shader->Code = GhostShader;
        for (UMaterialExpression* Expression : TArray<UMaterialExpression*>{Time, Intensity, Color, Shader})
            Material->GetExpressionCollection().AddExpression(Expression);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Shader);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    return Material;
}

// r414 (Stefan: "as a ghost the whole character could be see-through"): every textured pass of
// the spirit drawn unlit, pale blue and translucent. Masked passes (hair, cloth edges) keep their
// cut-out; opaque ones ignore the texture alpha (skin alpha is not coverage).
UMaterialInterface* SpiritBodyMaterial()
{
    static TStrongObjectPtr<UMaterial> Retained;
    if (Retained.IsValid()) return Retained.Get();
    const FString Name = TEXT("M_WowSpiritBody_v1");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->SetShadingModel(MSM_Unlit);
        Material->BlendMode = BLEND_Translucent;
        Material->TwoSided = false;
        auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Sampler->ParameterName = TEXT("BaseTexture");
        Sampler->SamplerType = SAMPLERTYPE_Color;
        Sampler->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
        auto* Opacity = NewObject<UMaterialExpressionScalarParameter>(Material);
        Opacity->ParameterName = TEXT("Opacity");
        Opacity->DefaultValue = 0.42f;
        auto* Cut = NewObject<UMaterialExpressionScalarParameter>(Material);
        Cut->ParameterName = TEXT("AlphaCut");
        Cut->DefaultValue = 0.f;
        auto* Glow = NewObject<UMaterialExpressionScalarParameter>(Material);
        Glow->ParameterName = TEXT("Glow");
        Glow->DefaultValue = 0.9f;
        auto* Tint = NewObject<UMaterialExpressionVectorParameter>(Material);
        Tint->ParameterName = TEXT("Tint");
        Tint->DefaultValue = FLinearColor(0.62f, 0.80f, 1.0f);
        auto* Colour = NewObject<UMaterialExpressionCustom>(Material);
        Colour->OutputType = CMOT_Float3;
        Colour->Inputs.Reset();
        auto* Alpha = NewObject<UMaterialExpressionCustom>(Material);
        Alpha->OutputType = CMOT_Float1;
        Alpha->Inputs.Reset();
        const auto Add = [](UMaterialExpressionCustom* Shader, const TCHAR* InputName, UMaterialExpression* Expression, int32 Output)
        {
            FCustomInput Value;
            Value.InputName = InputName;
            Value.Input.Connect(Output, Expression);
            Shader->Inputs.Add(Value);
        };
        Add(Colour, TEXT("Tex"), Sampler, 0);
        Add(Colour, TEXT("TintIn"), Tint, 0);
        Add(Colour, TEXT("GlowIn"), Glow, 0);
        // Greyed body with the spirit's cold tint; a little floor so dark cloth still reads.
        Colour->Code = TEXT("float L = dot(Tex, float3(0.3, 0.59, 0.11)); return (0.08 + lerp(Tex, L.xxx, 0.6)) * TintIn * GlowIn;");
        Add(Alpha, TEXT("A"), Sampler, 4);
        Add(Alpha, TEXT("Op"), Opacity, 0);
        Add(Alpha, TEXT("Cut"), Cut, 0);
        Alpha->Code = TEXT("return lerp(1.0, saturate((A - 0.3) * 20.0), Cut) * Op;");
        for (UMaterialExpression* Expression : TArray<UMaterialExpression*>{Sampler, Opacity, Cut, Glow, Tint, Colour, Alpha})
            Material->GetExpressionCollection().AddExpression(Expression);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Colour);
        Material->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (Material) Retained.Reset(Material);
    return Material;
}

/** Replaces each textured slot that is not spirit yet; returns the replaced materials (nullptr = untouched). */
TArray<UMaterialInterface*> SpiritifyMesh(UMeshComponent* Mesh, UMaterialInterface* Base)
{
    TArray<UMaterialInterface*> Replaced;
    Replaced.SetNumZeroed(Mesh->GetNumMaterials());
    for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
    {
        UMaterialInterface* Source = Mesh->GetMaterial(Slot);
        if (!Source || Source->GetMaterial() == Base->GetMaterial()) continue;
        UTexture* Texture = nullptr;
        if ((!Source->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseTexture")), Texture) || !Texture)
            && (!Source->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Layer0Texture")), Texture) || !Texture))
            continue; // glows and other untextured passes stay as they are
        UMaterialInstanceDynamic* Spirit = UMaterialInstanceDynamic::Create(Base, Mesh);
        Spirit->SetTextureParameterValue(TEXT("BaseTexture"), Texture);
        Spirit->SetScalarParameterValue(TEXT("AlphaCut"), Source->GetBlendMode() == BLEND_Masked ? 1.f : 0.f);
        Mesh->SetMaterial(Slot, Spirit);
        Replaced[Slot] = Source;
    }
    return Replaced;
}

bool IsSpiritCandidate(const UMeshComponent* Mesh)
{
    return Mesh && Mesh->GetNumMaterials() > 0 && !Mesh->ComponentHasTag(TEXT("WowBeltTorch"))
        && !Mesh->GetClass()->GetName().Contains(TEXT("SpellMesh"));
}

bool IsGhostPlayer(const FWowEntity& Entity)
{
    return Entity.IsPlayer() && (Entity.GetField(PlayerField::FLAGS) & PLAYER_FLAGS_GHOST) != 0;
}
}

UMaterialInterface* UWowDeathManager::GetGhostOverlayMaterial()
{
    return GhostOverlayMaterial();
}

void UWowDeathManager::MakeSpiritBody(AActor* Actor)
{
    UMaterialInterface* Base = Actor ? SpiritBodyMaterial() : nullptr;
    if (!Base) return;
    TArray<UMeshComponent*> Meshes;
    Actor->GetComponents<UMeshComponent>(Meshes, true);
    for (UMeshComponent* Mesh : Meshes)
        if (IsSpiritCandidate(Mesh))
        {
            SpiritifyMesh(Mesh, Base);
            Mesh->SetCastShadow(false);
        }
}

void UWowDeathManager::SetSpiritBody(AActor* Actor, bool bOn)
{
    if (!Actor) return;
    UMaterialInterface* Base = bOn ? SpiritBodyMaterial() : nullptr;
    if (bOn && !Base) return;
    TArray<UMeshComponent*> Meshes;
    Actor->GetComponents<UMeshComponent>(Meshes, true);
    for (UMeshComponent* Mesh : Meshes)
    {
        if (!IsSpiritCandidate(Mesh)) continue;
        const TWeakObjectPtr<UMeshComponent> Key(Mesh);
        if (bOn)
        {
            // Also catches slots that were rebuilt since (gear change, HD switch): they are
            // ordinary materials again and get replaced on the next pass.
            const TArray<UMaterialInterface*> Replaced = SpiritifyMesh(Mesh, Base);
            TArray<UMaterialInterface*>& Originals = SpiritOriginals.FindOrAdd(Key);
            Originals.SetNumZeroed(FMath::Max(Originals.Num(), Replaced.Num()));
            for (int32 Slot = 0; Slot < Replaced.Num(); ++Slot)
                if (Replaced[Slot])
                {
                    Originals[Slot] = Replaced[Slot];
                    SpiritKept.Add(Replaced[Slot]);
                }
            if (!SpiritShadows.Contains(Key))
            {
                SpiritShadows.Add(Key, Mesh->CastShadow);
                Mesh->SetCastShadow(false); // a ghost casts no shadow
            }
        }
        else if (TArray<UMaterialInterface*>* Originals = SpiritOriginals.Find(Key))
        {
            for (int32 Slot = 0; Slot < Originals->Num() && Slot < Mesh->GetNumMaterials(); ++Slot)
                if ((*Originals)[Slot]) Mesh->SetMaterial(Slot, (*Originals)[Slot]);
            if (const bool* Shadow = SpiritShadows.Find(Key)) Mesh->SetCastShadow(*Shadow);
            SpiritOriginals.Remove(Key);
            SpiritShadows.Remove(Key);
        }
    }
    // Forget meshes that are gone; once nothing is spirit any more, let the originals go.
    for (auto It = SpiritOriginals.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) { SpiritShadows.Remove(It.Key()); It.RemoveCurrent(); }
    if (SpiritOriginals.IsEmpty()) SpiritKept.Reset();
}

void UWowDeathManager::PrepareStandaloneMaterials()
{
    GhostOverlayMaterial();
    SpiritBodyMaterial(); // r414: M_WowSpiritBody_v1
}

void UWowDeathManager::Initialize(UWowConnectionManager* ConnectionMgr, TSharedPtr<SWidget>)
{
    ConnectionManager = ConnectionMgr;
}

void UWowDeathManager::Shutdown()
{
    for (auto& Pair : CorpseActors)
        if (IsValid(Pair.Value)) Pair.Value->Destroy();
    CorpseActors.Reset();
    for (const TWeakObjectPtr<AActor>& Actor : OverlaidActors)
        if (Actor.IsValid()) SetOverlay(Actor.Get(), false);
    OverlaidActors.Reset();
    if (IsValid(DeathVolume)) DeathVolume->Destroy();
    DeathVolume = nullptr;
    ConnectionManager = nullptr;
}

void UWowDeathManager::Tick(float DeltaTime)
{
    if (!ConnectionManager) return;
    const FWowDeathState& Death = ConnectionManager->PacketHandler.DeathState;
    UpdateLocalBody(Death.bDead, Death.bGhost);
    UpdatePostProcess(DeltaTime);
    const double Now = FPlatformTime::Seconds();
    if (Now >= NextCorpseScan)
    {
        NextCorpseScan = Now + 0.5;
        UpdateGhostOverlays();
        UpdateCorpses();
    }
}

void UWowDeathManager::UpdateLocalBody(bool bDead, bool bGhost)
{
    AWowGameplayController* Controller = GetTypedOuter<AWowGameplayController>();
    AWowPlayerCharacter* Player = Controller ? Cast<AWowPlayerCharacter>(Controller->GetPawn()) : nullptr;
    if (bDead != bLocalDead)
    {
        bLocalDead = bDead;
        if (Player)
        {
            if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
            {
                if (bDead) { Movement->StopMovementImmediately(); Movement->DisableMovement(); }
                else Movement->SetMovementMode(MOVE_Walking);
            }
            if (Player->AnimationController) Player->AnimationController->SetDead(bDead);
        }
        UE_LOG(LogWowDeath, Log, TEXT("Local player %s"), bDead ? TEXT("died: body down, movement locked") : TEXT("is no longer a corpse"));
    }
    if (bGhost != bLocalGhost)
    {
        bLocalGhost = bGhost;
        UE_LOG(LogWowDeath, Log, TEXT("Local player %s the spirit world"), bGhost ? TEXT("entered") : TEXT("left"));
    }
    // The spirit (and anyone else's) wears the rim glow; re-applied when the pawn rebuilds its meshes.
    if (Player) SetOverlay(Player, bLocalGhost);
}

void UWowDeathManager::UpdatePostProcess(float DeltaTime)
{
    const float DeadTarget = bLocalDead ? 1.f : 0.f;
    const float GhostTarget = bLocalGhost ? 1.f : 0.f;
    // Death drains the colour over ~2 s; the spirit world comes in with the release.
    DeadBlend = FMath::FInterpConstantTo(DeadBlend, DeadTarget, DeltaTime, 0.5f);
    GhostBlend = FMath::FInterpConstantTo(GhostBlend, GhostTarget, DeltaTime, GhostTarget > GhostBlend ? 0.8f : 0.6f);
    const float Weight = FMath::Max(DeadBlend, GhostBlend);
    // r414 (Stefan: more blue fog as a ghost, so you cannot see far): the sky manager thickens the
    // height fog into a cold blue haze with the spirit world.
    AWowSkyManager::SetSpiritFog(GhostBlend);
    if (Weight <= 0.001f)
    {
        if (IsValid(DeathVolume)) DeathVolume->BlendWeight = 0.f;
        return;
    }
    UWorld* World = GetWorld();
    if (!World) return;
    if (!IsValid(DeathVolume))
    {
        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        DeathVolume = World->SpawnActor<APostProcessVolume>(Params);
        if (!DeathVolume) return;
        DeathVolume->bUnbound = true;
        DeathVolume->Priority = 40.f;
    }
    // Mix: grey for a corpse, pale blue-white spirit world for a ghost.
    const float G = GhostBlend / FMath::Max(Weight, 0.001f);
    FPostProcessSettings& S = DeathVolume->Settings;
    S.bOverride_ColorSaturation = true;
    S.ColorSaturation = FVector4(FMath::Lerp(0.18f, 0.06f, G), FMath::Lerp(0.18f, 0.08f, G), FMath::Lerp(0.18f, 0.12f, G), 1.f);
    S.bOverride_ColorContrast = true;
    S.ColorContrast = FVector4(1.f, 1.f, 1.f, FMath::Lerp(1.04f, 1.12f, G));
    S.bOverride_ColorGain = true;
    S.ColorGain = FVector4(FMath::Lerp(0.92f, 0.84f, G), FMath::Lerp(0.92f, 0.96f, G), FMath::Lerp(0.92f, 1.18f, G), FMath::Lerp(0.95f, 1.08f, G));
    S.bOverride_ColorOffset = true;
    S.ColorOffset = FVector4(0.f, 0.f, 0.f, FMath::Lerp(0.f, 0.015f, G)); // a little haze lifts the blacks
    S.bOverride_BloomIntensity = true;
    S.BloomIntensity = FMath::Lerp(0.675f, 1.6f, G);
    S.bOverride_VignetteIntensity = true;
    S.VignetteIntensity = FMath::Lerp(0.55f, 0.7f, G);
    DeathVolume->BlendWeight = Weight;
}

void UWowDeathManager::SetOverlay(AActor* Actor, bool bOn)
{
    if (!Actor) return;
    if (bOn && !GhostOverlay) GhostOverlay = GhostOverlayMaterial();
    if (bOn && !GhostOverlay) return;
    TArray<USkeletalMeshComponent*> Meshes;
    Actor->GetComponents<USkeletalMeshComponent>(Meshes, true);
    for (USkeletalMeshComponent* Mesh : Meshes)
    {
        if (!Mesh || !Mesh->GetSkeletalMeshAsset()) continue;
        const bool bHas = Mesh->GetOverlayMaterial() != nullptr && Mesh->ComponentHasTag(TEXT("WowGhostOverlay"));
        if (bOn && !bHas)
        {
            Mesh->SetOverlayMaterial(GhostOverlay);
            Mesh->ComponentTags.AddUnique(TEXT("WowGhostOverlay"));
        }
        else if (!bOn && bHas)
        {
            Mesh->SetOverlayMaterial(nullptr);
            Mesh->ComponentTags.Remove(TEXT("WowGhostOverlay"));
        }
    }
    SetSpiritBody(Actor, bOn);
    if (bOn) OverlaidActors.Add(Actor); else OverlaidActors.Remove(Actor);
}

void UWowDeathManager::UpdateGhostOverlays()
{
    AWowGameplayController* Controller = GetTypedOuter<AWowGameplayController>();
    if (!Controller || !ConnectionManager) return;
    // Other ghosts (only sent to us while we are one ourselves).
    TSet<TWeakObjectPtr<AActor>> Wanted;
    if (AActor* Pawn = Controller->GetPawn(); Pawn && bLocalGhost) Wanted.Add(Pawn);
    for (const auto& Pair : Controller->SpawnedEntityActors)
    {
        const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
        if (Entity && IsValid(Pair.Value) && IsGhostPlayer(*Entity))
        {
            SetOverlay(Pair.Value, true);
            Wanted.Add(Pair.Value.Get());
        }
    }
    TArray<TWeakObjectPtr<AActor>> Stale;
    for (const TWeakObjectPtr<AActor>& Actor : OverlaidActors)
        if (!Wanted.Contains(Actor)) Stale.Add(Actor);
    for (const TWeakObjectPtr<AActor>& Actor : Stale)
    {
        if (Actor.IsValid()) SetOverlay(Actor.Get(), false);
        OverlaidActors.Remove(Actor);
    }
}

void UWowDeathManager::UpdateCorpses()
{
    // Player corpses lie where they died (the stock client draws the corpse object as the
    // dead body). Bones (CORPSE_FLAG_BONES) are left to the world for now.
    TSet<uint64> Present;
    const auto& All = ConnectionManager->PacketHandler.EntityManager.GetAll();
    for (const auto& Pair : All)
    {
        const FWowEntity* Entity = Pair.Value.Get();
        if (!Entity || !Entity->IsCorpse() || (Entity->GetField(CORPSE_FLAGS) & CORPSE_FLAG_BONES)) continue;
        if (Entity->Movement.Position.IsNearlyZero()) continue;
        Present.Add(Pair.Key);
        if (CorpseActors.Contains(Pair.Key)) continue;
        if (CorpseActors.Num() >= MaxCorpseActors) continue;
        if (AActor* Actor = SpawnCorpseActor(*Entity)) CorpseActors.Add(Pair.Key, Actor);
        else CorpseActors.Add(Pair.Key, nullptr); // do not retry every scan
    }
    for (auto It = CorpseActors.CreateIterator(); It; ++It)
        if (!Present.Contains(It.Key()))
        {
            if (IsValid(It.Value())) It.Value()->Destroy();
            It.RemoveCurrent();
        }
}

AActor* UWowDeathManager::SpawnCorpseActor(const FWowEntity& Corpse)
{
    AWowGameplayController* Controller = GetTypedOuter<AWowGameplayController>();
    UWorld* World = GetWorld();
    if (!Controller || !World || !Controller->CachedMpq || !Controller->CachedAssetCache) return nullptr;
    const uint8 Race = Corpse.GetFieldByte(CORPSE_BYTES_1, 1);
    const uint8 Gender = Corpse.GetFieldByte(CORPSE_BYTES_1, 2);
    if (Race == 0 || Gender > 1) return nullptr;

    FWowCharacterBuilder::FCharacterParams Params;
    Params.Race = static_cast<FWowCharacterBuilder::ERace>(Race);
    Params.Gender = static_cast<FWowCharacterBuilder::EGender>(Gender);
    auto& C = Params.Customization;
    C.RaceId = Race;
    C.Gender = Gender;
    C.SkinColor = Corpse.GetFieldByte(CORPSE_BYTES_1, 3);
    C.FaceVariation = Corpse.GetFieldByte(CORPSE_BYTES_2, 0);
    C.HairStyle = Corpse.GetFieldByte(CORPSE_BYTES_2, 1);
    C.HairColor = Corpse.GetFieldByte(CORPSE_BYTES_2, 2);
    C.FacialHairStyle = Corpse.GetFieldByte(CORPSE_BYTES_2, 3);
    auto& E = Params.BodyEquipment;
    E.RaceId = Race;
    E.Gender = Gender;
    for (int32 Slot = 0; Slot < 19; ++Slot)
    {
        const uint32 Value = Corpse.GetField(CORPSE_ITEM + Slot);
        const uint32 Display = Value & 0x00FFFFFF;
        const uint32 InventoryType = Value >> 24;
        if (!Display) continue;
        switch (InventoryType)
        {
        case 4: E.ShirtDisplayId = Display; break;
        case 5: E.ChestDisplayId = Display; break;
        case 20: E.ChestDisplayId = Display; E.bIsRobeEquipped = true; break;
        case 6: E.BeltDisplayId = Display; break;
        case 7: E.PantsDisplayId = Display; break;
        case 8: E.BootsDisplayId = Display; break;
        case 9: E.BracersDisplayId = Display; break;
        case 10: E.GlovesDisplayId = Display; break;
        case 16: E.CapeDisplayId = Display; break;
        case 19: E.TabardDisplayId = Display; break;
        default: break; // helm, shoulders and weapons stay with the spirit
        }
    }
    const FVector Location = FWowCoordinate::WowToUE(Corpse.Movement.Position);
    const FRotator Rotation(0.f, FWowCoordinate::WowOrientationToUEYaw(Corpse.Movement.Orientation), 0.f);
    AActor* Actor = FWowCharacterBuilder::SpawnCharacterWithEquipment(World, Controller->CachedMpq, Controller->CachedAssetCache,
        Params, Location, Rotation);
    if (!Actor) return nullptr;
    Actor->Tags.AddUnique(TEXT("WowCorpse"));
    if (UWowAnimationController* Anim = FWowCharacterBuilder::GetAnimationController(Actor))
        Anim->ShowCorpsePose();
    // A corpse is scenery: it never blocks the spirit walking back to it.
    TArray<UPrimitiveComponent*> Primitives;
    Actor->GetComponents<UPrimitiveComponent>(Primitives);
    for (UPrimitiveComponent* Primitive : Primitives)
        if (Primitive) Primitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    UE_LOG(LogWowDeath, Log, TEXT("Corpse of race %u at %s"), Race, *Location.ToString());
    return Actor;
}
