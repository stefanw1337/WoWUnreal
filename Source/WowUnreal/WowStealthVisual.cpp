#include "WowStealthVisual.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowAudioManager.h"
#include "WowClientPreferences.h"
#include "WowCookedMaterial.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/WidgetComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "UObject/StrongObjectPtr.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowStealth, Log, All);

namespace
{
    // Build-12340 aura type SPELL_AURA_MOD_STEALTH (rogue Stealth, Vanish, Prowl, Shadowmeld).
    constexpr uint32 AuraModStealth = 16;
    // SoundEntries.dbc: 3325 "Stealth" (the Stealth spell's own cast sound), 3 "Invisibility Impact"
    // (a soft dispel whoosh) for stepping out of the shadows.
    constexpr uint32 SoundEnterStealth = 3325;
    constexpr uint32 SoundLeaveStealth = 3;

    enum class EStyle : uint8 { None, Cloak, Classic, Predator, ShadowOnly };

    EStyle CurrentStyle()
    {
        if (FWowClientPreferences::GetWorldOption(TEXT("stealthpredator"))) return EStyle::Predator;
        if (FWowClientPreferences::GetWorldOption(TEXT("stealthshadowonly"))) return EStyle::ShadowOnly;
        if (FWowClientPreferences::GetWorldOption(TEXT("stealthclassic"))) return EStyle::Classic;
        return EStyle::Cloak;
    }

    bool IsStealthed(const FWowEntity* Entity)
    {
        if (!Entity || !Entity->IsUnit()) return false;
        const FWowUnitEntity* Unit = static_cast<const FWowUnitEntity*>(Entity);
        if (Unit->GetHealth() == 0) return false;
        for (const FAuraInfo& Aura : Unit->Auras)
        {
            if (!Aura.bActive) continue;
            const auto* Spell = FDbcStore::Get().Spells().GetById(Aura.SpellId);
            if (!Spell) continue;
            for (uint32 Effect = 0; Effect < 3; ++Effect)
                if ((Aura.Flags & (1u << Effect)) && Spell->EffectApplyAuraName[Effect] == AuraModStealth) return true;
        }
        return false;
    }

    // The overlay: drawn over the body. Darken (0..1) lays a dark veil, Rim adds a cool edge glow at
    // grazing angles, Distort bends the world seen through the body (refraction), with a slow shimmer.
    const TCHAR* RimCode = TEXT(R"(
float3 N = normalize(Parameters.WorldNormal);
float3 V = normalize(Parameters.CameraVector);
float rim = pow(1 - saturate(abs(dot(N, V))), 2.6);
float shimmer = 0.75 + 0.25 * sin(TimeSec * 2.3 + dot(Pos.xyz, float3(0.013, 0.017, 0.021)));
return rim * shimmer;
)");

    struct FApplied
    {
        EStyle Style = EStyle::None;
        TArray<TWeakObjectPtr<UPrimitiveComponent>> Parts;
    };
    TMap<TWeakObjectPtr<AActor>, FApplied> GApplied;
    bool GLocalStealthed = false;
    TStrongObjectPtr<UMaterialInterface> GOverlay;
    TMap<EStyle, TStrongObjectPtr<UMaterialInstanceDynamic>> GOverlayByStyle;

    UMaterialInterface* OverlayFor(EStyle Style)
    {
        if (TStrongObjectPtr<UMaterialInstanceDynamic>* Found = GOverlayByStyle.Find(Style)) return Found->Get();
        UMaterialInterface* Base = FWowStealthVisual::GetOverlayMaterial();
        if (!Base) return nullptr;
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
        float Darken = 0.f, Rim = 0.f, Distort = 0.f;
        switch (Style)
        {
        case EStyle::Cloak:    Darken = 0.32f; Rim = 0.45f; Distort = 0.55f; break;
        case EStyle::Classic:  Darken = 0.40f; Rim = 0.0f;  Distort = 0.0f;  break;
        case EStyle::Predator: Darken = 0.0f;  Rim = 0.9f;  Distort = 1.0f;  break;
        default: break;
        }
        MID->SetScalarParameterValue(TEXT("Darken"), Darken);
        MID->SetScalarParameterValue(TEXT("Rim"), Rim);
        MID->SetScalarParameterValue(TEXT("Distort"), Distort);
        GOverlayByStyle.Add(Style, TStrongObjectPtr<UMaterialInstanceDynamic>(MID));
        return MID;
    }

    float BodyOpacity(EStyle Style)
    {
        switch (Style)
        {
        case EStyle::Cloak: return 0.45f;
        case EStyle::Classic: return 0.40f;
        case EStyle::Predator: return 0.0f;
        default: return 1.0f;
        }
    }

    bool IsBodyPart(UPrimitiveComponent* Part)
    {
        return Part && Part->IsRegistered() && Part->IsA<UMeshComponent>() && !Part->IsA<UWidgetComponent>()
            && !Part->ComponentHasTag(TEXT("WowTargetRing")) && !Part->ComponentHasTag(TEXT("WowGhostOverlay"))
            && !Part->ComponentHasTag(TEXT("WowStealthDetect"));
    }

    void Restore(AActor* Actor, FApplied& Applied)
    {
        for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : Applied.Parts)
        {
            UPrimitiveComponent* Part = Weak.Get();
            if (!Part) continue;
            if (UMeshComponent* Mesh = Cast<UMeshComponent>(Part))
            {
                Mesh->SetScalarParameterValueOnMaterials(TEXT("FirstPersonOpacity"), 1.0f);
                if (Mesh->ComponentHasTag(TEXT("WowStealthOverlay")))
                {
                    Mesh->SetOverlayMaterial(nullptr);
                    Mesh->ComponentTags.Remove(TEXT("WowStealthOverlay"));
                }
            }
            if (!Part->bRenderInMainPass) Part->SetRenderInMainPass(true);
            if (!Part->bRenderInDepthPass) Part->SetRenderInDepthPass(true);
        }
        Applied.Parts.Reset();
        Applied.Style = EStyle::None;
    }

    void Apply(AActor* Actor, FApplied& Applied, EStyle Style)
    {
        if (Applied.Style != Style) Restore(Actor, Applied);
        TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
        UMaterialInterface* Overlay = Style == EStyle::ShadowOnly ? nullptr : OverlayFor(Style);
        for (UPrimitiveComponent* Part : Parts)
        {
            if (!IsBodyPart(Part)) continue;
            Applied.Parts.AddUnique(Part);
            UMeshComponent* Mesh = Cast<UMeshComponent>(Part);
            if (Style == EStyle::ShadowOnly)
            {
                // Not drawn, but still casting its shadow (the shadow depth pass ignores these flags).
                if (Part->bRenderInMainPass) Part->SetRenderInMainPass(false);
                if (Part->bRenderInDepthPass) Part->SetRenderInDepthPass(false);
                continue;
            }
            // The character/equipment materials dither by FirstPersonOpacity (they own it for the zoom fade).
            Mesh->SetScalarParameterValueOnMaterials(TEXT("FirstPersonOpacity"), BodyOpacity(Style));
            if (Overlay && Mesh->GetOverlayMaterial() != Overlay && !Mesh->ComponentHasTag(TEXT("WowGhostOverlay")))
            {
                Mesh->SetOverlayMaterial(Overlay);
                Mesh->ComponentTags.AddUnique(TEXT("WowStealthOverlay"));
            }
        }
        Applied.Style = Style;
    }

    // ---- r605 detection range (Stefan: "others shouldn't see you in stealth ... unless they get too
    // close - a stealth-range checker"; "at least have that as another experimental for classes").
    // Who sees a stealthed player is decided by the server; this only DRAWS how close each enemy can
    // come before it notices you, with AzerothCore's own rule (WorldObject::CanDetectStealthOf):
    //   detection = 30 + (enemy level - 1) * 5 + its detect bonus - your stealth value
    //   range     = detection * 0.3 + enemy combat reach  (players capped at 30 yards)
    //   only in front of the enemy (a 180 degree cone); inside its combat reach always.
    //   "alert" (the ? suspicion, a look-around) reaches 8% + 1.5 yards further.
    // Your stealth value: the Stealth/Prowl/Shadowmeld aura amount, 5 per level (Spell.dbc base
    // points + 5 per level above the spell level). Talents and enemy detection buffs are not known
    // to the client, so the drawn range is an estimate. World option "stealthdetectrange".
    constexpr float YardUU = 100.f;
    constexpr float DetectShowYards = 45.f;
    constexpr int32 DetectMaxRings = 24;
    constexpr int32 DetectArcSegments = 28;

    struct FDetectRing
    {
        TWeakObjectPtr<UProceduralMeshComponent> Mesh;
        FVector BuiltAt = FVector(1e30);
        float BuiltYaw = 1e9f, BuiltRange = -1.f, BuiltAlert = -1.f;
        FColor BuiltColor = FColor::Transparent;
        double LastBuild = 0.0;
    };
    TMap<uint64, FDetectRing> GDetectRings;
    TStrongObjectPtr<UMaterialInterface> GDetectMaterial;

    UMaterialInterface* DetectMaterial()
    {
        if (GDetectMaterial.IsValid()) return GDetectMaterial.Get();
        const FString Name = TEXT("M_WowStealthDetect_v1");
        UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
        if (!Material)
        {
            Material = NewObject<UMaterial>(GetTransientPackage());
            Material->SetShadingModel(MSM_Unlit);
            Material->BlendMode = BLEND_Translucent;
            Material->TwoSided = true;
            auto* Color = NewObject<UMaterialExpressionVertexColor>(Material);
            Material->GetExpressionCollection().AddExpression(Color);
            // Vertex color output 0 is RGB, output 4 is alpha.
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
            Material->GetEditorOnlyData()->Opacity.Connect(4, Color);
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
            Material = WowCookedMaterial::Save(Material, Name);
        }
#endif
        if (Material) GDetectMaterial.Reset(Material);
        return Material;
    }

    int32 StealthValue(const FWowUnitEntity* Unit)
    {
        int32 Value = 0;
        const int32 Level = Unit ? Unit->GetLevel() : 1;
        if (Unit)
            for (const FAuraInfo& Aura : Unit->Auras)
            {
                if (!Aura.bActive) continue;
                const auto* Spell = FDbcStore::Get().Spells().GetById(Aura.SpellId);
                if (!Spell) continue;
                for (uint32 Effect = 0; Effect < 3; ++Effect)
                    if ((Aura.Flags & (1u << Effect)) && Spell->EffectApplyAuraName[Effect] == AuraModStealth)
                        Value = FMath::Max(Value, Spell->EffectBasePoints[Effect] + 1 + 5 * FMath::Max(0, Level - int32(FMath::Max(1u, Spell->SpellLevel))));
            }
        return Value;
    }

    void HideDetectRings()
    {
        for (auto& Pair : GDetectRings)
            if (UProceduralMeshComponent* Mesh = Pair.Value.Mesh.Get()) Mesh->DestroyComponent();
        GDetectRings.Reset();
    }

    void BuildRing(UWorld* World, AActor* Ignore, UProceduralMeshComponent* Mesh, const FVector& Feet, float Yaw,
        float Reach, float Range, float Alert, const FColor& Color)
    {
        // A fan in front of the enemy: centre, the detection arc, a bright edge, then a fading alert
        // band; plus a small full circle for its combat reach. Vertices are traced onto the ground.
        TArray<FVector> Vertices; TArray<int32> Triangles; TArray<FColor> Colors;
        TArray<FVector> Normals; TArray<FVector2D> UVs; TArray<FProcMeshTangent> Tangents;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(WowStealthDetect), false, Ignore);
        auto Ground = [&](const FVector2D& Offset) -> FVector
        {
            const FVector At = Feet + FVector(Offset.X, Offset.Y, 0.f);
            FHitResult Hit;
            if (World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 250), At - FVector(0, 0, 600),
                FCollisionObjectQueryParams(ECC_WorldStatic), Params) && Hit.ImpactNormal.Z > 0.5f)
                return FVector(Offset.X, Offset.Y, Hit.ImpactPoint.Z - Feet.Z + 6.f);
            return FVector(Offset.X, Offset.Y, 6.f);
        };
        auto Push = [&](const FVector& P, uint8 Alpha) { Vertices.Add(P); Colors.Add(FColor(Color.R, Color.G, Color.B, Alpha)); Normals.Add(FVector::UpVector); UVs.Add(FVector2D::ZeroVector); return Vertices.Num() - 1; };
        const float Radii[] = { 0.f, Range * 0.88f, Range * 0.97f, Range, Alert };
        const uint8 Alphas[] = { 10, 34, 120, 150, 0 };
        const float Yr = FMath::DegreesToRadians(Yaw);
        int32 Prev[5] = {};
        for (int32 S = 0; S <= DetectArcSegments; ++S)
        {
            const float A = Yr - HALF_PI + PI * S / DetectArcSegments;
            const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
            int32 Cur[5];
            for (int32 R = 0; R < 5; ++R)
                Cur[R] = (R == 0 && S > 0) ? Prev[0] : Push(Ground(Dir * Radii[R]), Alphas[R]);
            if (S > 0)
                for (int32 R = 0; R < 4; ++R)
                {
                    if (R == 0) { Triangles.Append({ Cur[0], Prev[1], Cur[1] }); continue; }
                    Triangles.Append({ Prev[R], Prev[R + 1], Cur[R + 1], Prev[R], Cur[R + 1], Cur[R] });
                }
            FMemory::Memcpy(Prev, Cur, sizeof(Cur));
        }
        // Combat reach: always noticed inside this, from any side.
        const int32 Centre = Push(Ground(FVector2D::ZeroVector), 28);
        int32 First = -1, Last = -1;
        for (int32 S = 0; S < 24; ++S)
        {
            const float A = 2.f * PI * S / 24;
            const int32 V = Push(Ground(FVector2D(FMath::Cos(A), FMath::Sin(A)) * Reach), 110);
            if (S == 0) First = V; else Triangles.Append({ Centre, Last, V });
            Last = V;
        }
        Triangles.Append({ Centre, Last, First });
        Mesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
    }

    void TickDetectRings(AWowGameplayController* Controller, FWowEntityManager& Entities, const FWowUnitEntity* Local, bool bShow)
    {
        APawn* Pawn = Controller->GetPawn();
        UWorld* World = Controller->GetWorld();
        UMaterialInterface* Material = bShow ? DetectMaterial() : nullptr;
        if (!bShow || !Pawn || !World || !Local || !Material) { if (GDetectRings.Num()) HideDetectRings(); return; }
        const FVector Me = Pawn->GetActorLocation();
        const int32 Stealth = StealthValue(Local);
        const double Now = World->GetTimeSeconds();
        TSet<uint64> Seen;
        for (const auto& Pair : Controller->GetSpawnedEntityActors())
        {
            if (Seen.Num() >= DetectMaxRings) break;
            AActor* Actor = Pair.Value.Get();
            const FWowEntity* Entity = Entities.Find(Pair.Key);
            if (!IsValid(Actor) || !Entity || !Entity->IsUnit() || Pair.Key == Entities.LocalPlayerGuid) continue;
            const FWowUnitEntity* Unit = static_cast<const FWowUnitEntity*>(Entity);
            if (Unit->GetHealth() == 0) continue;
            // UNIT_FLAG_NON_ATTACKABLE | IMMUNE_TO_PC | NOT_SELECTABLE: cannot notice you as an enemy.
            if (Unit->GetField(UnitField::FLAGS) & (0x00000002u | 0x00000100u | 0x02000000u)) continue;
            if (FVector::Dist2D(Actor->GetActorLocation(), Me) > DetectShowYards * YardUU) continue;
            if (Controller->ConnectionManager->GetUnitReaction(Local, Entity) >= 4) continue; // neutral/friendly
            float Reach = Unit->GetFieldFloat(static_cast<uint16>(UnitField::DISPLAYID - 1)); // UNIT_FIELD_COMBATREACH (yards)
            if (!FMath::IsFinite(Reach) || Reach <= 0.f || Reach > 20.f) Reach = 1.5f;
            const int32 Detection = 30 + (FMath::Max(1, Unit->GetLevel()) - 1) * 5 - Stealth;
            float Range = FMath::Max(0.f, Detection * 0.3f) + Reach;
            if (Entity->IsPlayer()) Range = FMath::Min(Range, 30.f);
            const float Alert = Range * 1.08f + 1.5f;
            const FVector Feet = Actor->GetActorLocation();
            const float Yaw = Actor->GetActorRotation().Yaw;
            // Red when you are inside its detection cone, amber inside the alert band, else a cool blue.
            const FVector ToMe = Me - Feet;
            const float DistYards = FVector::Dist2D(Me, Feet) / YardUU;
            const bool bInFront = FVector2D::DotProduct(FVector2D(ToMe.X, ToMe.Y), FVector2D(Actor->GetActorForwardVector().X, Actor->GetActorForwardVector().Y)) >= 0.f;
            const FColor Color = DistYards <= Reach || (bInFront && DistYards <= Range) ? FColor(255, 40, 30)
                : (bInFront && DistYards <= Alert) ? FColor(255, 170, 30) : FColor(90, 150, 255);
            Seen.Add(Pair.Key);
            FDetectRing& Ring = GDetectRings.FindOrAdd(Pair.Key);
            UProceduralMeshComponent* Mesh = Ring.Mesh.Get();
            if (!Mesh)
            {
                // PlayerControllers are hidden actors: owned by the pawn, like the target ring.
                Mesh = NewObject<UProceduralMeshComponent>(Pawn);
                Mesh->bUseAsyncCooking = false;
                Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Mesh->SetCastShadow(false);
                Mesh->bReceivesDecals = false;
                Mesh->SetUsingAbsoluteLocation(true);
                Mesh->SetUsingAbsoluteRotation(true);
                Mesh->SetUsingAbsoluteScale(true);
                Mesh->ComponentTags.Add(TEXT("WowStealthDetect"));
                Mesh->SetMaterial(0, Material);
                Pawn->AddInstanceComponent(Mesh);
                Mesh->SetupAttachment(Pawn->GetRootComponent());
                Mesh->RegisterComponent();
                Ring.Mesh = Mesh;
            }
            Mesh->SetWorldLocationAndRotation(Feet, FRotator::ZeroRotator);
            const bool bMoved = !Feet.Equals(Ring.BuiltAt, 25.f) || FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, Ring.BuiltYaw)) > 4.f;
            if (Color != Ring.BuiltColor || !FMath::IsNearlyEqual(Range, Ring.BuiltRange, 0.05f)
                || (bMoved && Now - Ring.LastBuild > 0.1))
            {
                BuildRing(World, Actor, Mesh, Feet, Yaw, Reach * YardUU, Range * YardUU, Alert * YardUU, Color);
                Ring.BuiltAt = Feet; Ring.BuiltYaw = Yaw; Ring.BuiltRange = Range; Ring.BuiltAlert = Alert;
                Ring.BuiltColor = Color; Ring.LastBuild = Now;
            }
        }
        for (auto It = GDetectRings.CreateIterator(); It; ++It)
        {
            if (Seen.Contains(It.Key())) continue;
            if (UProceduralMeshComponent* Mesh = It.Value().Mesh.Get()) Mesh->DestroyComponent();
            It.RemoveCurrent();
        }
    }
}

UMaterialInterface* FWowStealthVisual::GetOverlayMaterial()
{
    if (GOverlay.IsValid()) return GOverlay.Get();
    const FString Name = TEXT("M_WowStealthOverlay");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->SetShadingModel(MSM_Unlit);
        Material->BlendMode = BLEND_Translucent;
        Material->TwoSided = false;
        Material->RefractionMethod = RM_IndexOfRefraction;
        auto Scalar = [Material](const TCHAR* ParamName, float Default)
        {
            auto* P = NewObject<UMaterialExpressionScalarParameter>(Material);
            P->ParameterName = ParamName; P->DefaultValue = Default;
            Material->GetExpressionCollection().AddExpression(P);
            return P;
        };
        auto* Darken = Scalar(TEXT("Darken"), 0.32f);
        auto* Rim = Scalar(TEXT("Rim"), 0.45f);
        auto* Distort = Scalar(TEXT("Distort"), 0.55f);
        auto* RimColor = NewObject<UMaterialExpressionVectorParameter>(Material);
        RimColor->ParameterName = TEXT("RimColor");
        RimColor->DefaultValue = FLinearColor(0.35f, 0.45f, 0.75f);
        Material->GetExpressionCollection().AddExpression(RimColor);
        auto* Time = NewObject<UMaterialExpressionTime>(Material);
        Material->GetExpressionCollection().AddExpression(Time);
        auto* Pos = NewObject<UMaterialExpressionWorldPosition>(Material);
        Material->GetExpressionCollection().AddExpression(Pos);
        auto Custom = [Material](ECustomMaterialOutputType Type, const FString& Code)
        {
            auto* Node = NewObject<UMaterialExpressionCustom>(Material);
            Node->OutputType = Type;
            Node->Inputs.Reset();
            Node->Code = Code;
            Material->GetExpressionCollection().AddExpression(Node);
            return Node;
        };
        auto Input = [](UMaterialExpressionCustom* Node, const TCHAR* InputName, UMaterialExpression* Expression)
        {
            FCustomInput Value; Value.InputName = InputName; Value.Input.Connect(0, Expression); Node->Inputs.Add(Value);
        };
        const FString RimBody(RimCode);
        auto* RimNode = Custom(CMOT_Float1, RimBody);
        Input(RimNode, TEXT("TimeSec"), Time);
        Input(RimNode, TEXT("Pos"), Pos);
        auto* Emissive = Custom(CMOT_Float3, TEXT("return RimColor.rgb * RimV * Rim * 1.6;"));
        Input(Emissive, TEXT("RimColor"), RimColor); Input(Emissive, TEXT("RimV"), RimNode); Input(Emissive, TEXT("Rim"), Rim);
        auto* Opacity = Custom(CMOT_Float1, TEXT("return saturate(Darken + RimV * Rim * 0.55 + Distort * 0.02);"));
        Input(Opacity, TEXT("Darken"), Darken); Input(Opacity, TEXT("RimV"), RimNode); Input(Opacity, TEXT("Rim"), Rim); Input(Opacity, TEXT("Distort"), Distort);
        auto* Refraction = Custom(CMOT_Float1, TEXT("return 1.0 + Distort * (0.025 + 0.07 * RimV);"));
        Input(Refraction, TEXT("Distort"), Distort); Input(Refraction, TEXT("RimV"), RimNode);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Emissive);
        Material->GetEditorOnlyData()->Opacity.Connect(0, Opacity);
        Material->GetEditorOnlyData()->Refraction.Connect(0, Refraction);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (Material) GOverlay.Reset(Material);
    return Material;
}

void FWowStealthVisual::PrepareStandaloneMaterials()
{
    GetOverlayMaterial();
    DetectMaterial();
}

void FWowStealthVisual::Tick(AWowGameplayController* Controller, float DeltaTime)
{
    if (!Controller || !Controller->ConnectionManager) return;
    FWowEntityManager& Entities = Controller->ConnectionManager->PacketHandler.EntityManager;
    const EStyle Style = CurrentStyle();
    TSet<AActor*> Wanted;

    // The local player.
    const bool bLocal = IsStealthed(Entities.Find(Entities.LocalPlayerGuid));
    if (AActor* Pawn = Controller->GetPawn())
        if (bLocal) Wanted.Add(Pawn);
    if (bLocal != GLocalStealthed)
    {
        GLocalStealthed = bLocal;
        UE_LOG(LogWowStealth, Log, TEXT("Local player %s stealth (look %d)"), bLocal ? TEXT("entered") : TEXT("left"), int32(Style));
        if (AActor* Pawn = Controller->GetPawn())
            if (auto* Audio = Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(Controller, AWowAudioManager::StaticClass())))
                Audio->PlayWorldSoundEffect(bLocal ? SoundEnterStealth : SoundLeaveStealth, Pawn->GetActorLocation());
    }

    // Stealthed units the server shows us (party members, detected rogues and druids).
    for (const auto& Pair : Controller->GetSpawnedEntityActors())
    {
        AActor* Actor = Pair.Value.Get();
        if (!IsValid(Actor) || Pair.Key == Entities.LocalPlayerGuid) continue;
        if (IsStealthed(Entities.Find(Pair.Key))) Wanted.Add(Actor);
    }

    for (AActor* Actor : Wanted) Apply(Actor, GApplied.FindOrAdd(Actor), Style);
    TickDetectRings(Controller, Entities, Entities.FindUnit(Entities.LocalPlayerGuid),
        bLocal && FWowClientPreferences::GetWorldOption(TEXT("stealthdetectrange")));
    for (auto It = GApplied.CreateIterator(); It; ++It)
    {
        AActor* Actor = It.Key().Get();
        if (Actor && Wanted.Contains(Actor)) continue;
        if (Actor) Restore(Actor, It.Value());
        It.RemoveCurrent();
    }
}
