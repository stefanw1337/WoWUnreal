#include "WowPlayerTorchComponent.h"
#include "WowEquipmentManager.h"
#include "WowPlayerCharacter.h"
#include "WowM2ParticleComponent.h"
#include "WowClientPreferences.h"
#include "Mpq/MpqManager.h"
#include "Formats/M2Parser.h"
#include "Formats/M2Types.h"
#include "Coord/WowCoordinate.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTorch, Log, All);

namespace
{
    // ItemDisplayInfo 1045 = Club_1H_Torch_A_01 (the 3.3.5 hand torch, with its flame emitters).
    constexpr uint32 TorchDisplayId = 1045;
    const TCHAR* TorchSection = TEXT("WowUnreal.Torch");
    const FName HipSocket(TEXT("WowSheathHipWeaponLeft"));
    const FName TorchTag(TEXT("WowBeltTorch"));
    // r697 (Stefan: the belt torch should have "a bigger flame ... a real torch" - "use another torch from like
    // Stormwind walls or something else that has a bigger flame"): the hand torch's own flame is a candle-sized
    // emitter. The belt torch burns with the flame of the world's standing torch (GeneralTorch01, the one on
    // poles along the roads) instead; its own emitters stay hidden.
    const TCHAR* BigFlameModel = TEXT("WORLD\\GENERIC\\PASSIVEDOODADS\\LIGHTS\\GENERALTORCH01.M2");
    const FName BigFlameTag(TEXT("WowBeltTorchBigFlame"));
    FVector GBigFlameEmitterLocal = FVector::ZeroVector; // the world torch's flame base, model space (cm)
    // r728 (Stefan, torch out with Ctrl+T: "Flame doesn't hit the torch that it's supposed to sit on" - a fireball
    // by the shoulder, well above the torch tip): the world torch's flame is made for a 2 m pole; at full size its
    // body rises half a metre over the emission point and dwarfs a hand torch. Three quarters of that size (with
    // the character's scale), and the emission point placed on the tip at that size.
    // r738 (Stefan 23.10, Stormwind at night: "the torch's fire is also not even close to the actual torch in his belt"):
    // the flame's base was on the tip, but a world torch draws its flame card 4.8x the authored size (made to read on a 2 m
    // pole from afar) and lifts it by half its height so its base sits on the crown: at 0.75 that card was 1 m tall,
    // centred half a metre over the tip - the "fireball by the shoulder". 0.32 gives a ~43 cm card (a real hand-torch
    // flame), centred ~21 cm over the tip, its base still on it; the glow scales with it, our light stays put.
    constexpr float BigFlameScale = 0.32f;

    bool GWantLoaded = false;
    bool GWant = false;

    void LoadWant()
    {
        if (GWantLoaded) return;
        GWantLoaded = true;
        if (GConfig) GConfig->GetBool(TorchSection, TEXT("On"), GWant, GGameUserSettingsIni);
    }
}

UWowPlayerTorchComponent::UWowPlayerTorchComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after the animation has moved the hip
}

bool UWowPlayerTorchComponent::IsWanted()
{
    LoadWant();
    return GWant;
}

void UWowPlayerTorchComponent::Ensure(APawn* Pawn, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (!Pawn || !IsWanted() || Pawn->FindComponentByClass<UWowPlayerTorchComponent>()) return;
    UWowPlayerTorchComponent* Component = NewObject<UWowPlayerTorchComponent>(Pawn, TEXT("WowBeltTorch"));
    Component->Mpq = Mpq;
    Component->Cache = Cache;
    Pawn->AddInstanceComponent(Component);
    Component->RegisterComponent();
}

void UWowPlayerTorchComponent::Toggle(APawn* Pawn, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    LoadWant();
    if (!FWowClientPreferences::IsDarkerRealisticLightingEnabled())
    {
        // The torch belongs to the dark nights of realistic lighting only.
        UE_LOG(LogWowTorch, Log, TEXT("Torch: needs Options > Darker, more realistic lighting"));
        return;
    }
    GWant = !GWant;
    if (GConfig)
    {
        GConfig->SetBool(TorchSection, TEXT("On"), GWant, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    UE_LOG(LogWowTorch, Log, TEXT("Torch %s"), GWant ? TEXT("lit") : TEXT("put out"));
    if (GWant) Ensure(Pawn, Mpq, Cache);
    else if (Pawn)
        if (UWowPlayerTorchComponent* Existing = Pawn->FindComponentByClass<UWowPlayerTorchComponent>())
            Existing->DestroyComponent();
}

void UWowPlayerTorchComponent::OnUnregister()
{
    Detach();
    Super::OnUnregister();
}

void UWowPlayerTorchComponent::Detach()
{
    if (USceneComponent* T = Torch.Get())
    {
        // The item's flame particles are child components of the torch mesh.
        TArray<USceneComponent*> Children;
        T->GetChildrenComponents(true, Children);
        for (USceneComponent* Child : Children) if (Child) Child->DestroyComponent();
        T->DestroyComponent();
    }
    if (UPointLightComponent* L = Light.Get()) L->DestroyComponent();
    Torch.Reset();
    Light.Reset();
    AttachedMeshAsset.Reset();
}

void UWowPlayerTorchComponent::Attach()
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
    if (!Body || !Body->GetSkeletalMeshAsset() || !Mpq) return;
    if (!Body->DoesSocketExist(HipSocket)) return;

    // Where the flame sits on the model: its first emitter (handle at the origin).
    FString ModelPath;
    if (FWowEquipmentManager::ResolveEquipmentPaths(Mpq, TorchDisplayId, FWowEquipmentManager::EAttachmentPoint::HipWeaponLeft, ModelPath))
    {
        TArray<uint8> Raw, Skin;
        if (Mpq->ReadFile(ModelPath, Raw))
        {
            Mpq->ReadFile(ModelPath.LeftChop(3) + TEXT("00.skin"), Skin);
            const FM2Data Data = FM2Parser::Parse(Raw, Skin);
            if (Data.bIsValid && Data.ParticleEmitters.Num() > 0)
            {
                const FM2ParticleEmitter& Emitter = Data.ParticleEmitters[0];
                // r470 (Stefan: the flame burned in the air beside the torch): the emitter position
                // is already in model space (UWowM2ParticleComponent::GetEmitterFrame); adding the
                // bone pivot put the flame point twice as far out (127 cm on a 64 cm torch), so the
                // light, the torch angle and the moved flame particles all missed the real tip.
                const FVector P = Emitter.Position;
                const FVector Local = FVector(P.Y, P.X, P.Z) * FWowCoordinate::SCALE;
                if (Local.Size() > 1.0)
                {
                    FlameLocal = Local;
                    FlameAxisLocal = Local.GetSafeNormal();
                }
            }
        }
    }

    USceneComponent* Item = FWowEquipmentManager::AttachItemToSocket(Mpq, Cache, TorchDisplayId, Body, HipSocket);
    if (!Item)
    {
        UE_LOG(LogWowTorch, Warning, TEXT("Torch: could not attach item display %u to %s"), TorchDisplayId, *HipSocket.ToString());
        return;
    }
    Item->ComponentTags.AddUnique(TorchTag);
    Torch = Item;

    // r697: the big flame (see BigFlameModel).
    {
        TArray<uint8> Raw, Skin;
        const FString FlamePath(BigFlameModel);
        if (Mpq->ReadFile(FlamePath, Raw))
        {
            Mpq->ReadFile(FlamePath.LeftChop(3) + TEXT("00.skin"), Skin);
            const FM2Data FlameData = FM2Parser::Parse(Raw, Skin);
            if (FlameData.bIsValid && FlameData.ParticleEmitters.Num() > 0)
            {
                const FVector P = FlameData.ParticleEmitters[0].Position;
                GBigFlameEmitterLocal = FVector(P.Y, P.X, P.Z) * FWowCoordinate::SCALE;
                // The hand torch's own little flame goes (bHiddenInGame is not undone by SetVisibility).
                TArray<USceneComponent*> Own;
                Item->GetChildrenComponents(true, Own);
                for (USceneComponent* Child : Own)
                    if (UWowM2ParticleComponent* OwnFlame = Cast<UWowM2ParticleComponent>(Child)) OwnFlame->SetHiddenInGame(true, true);
                UWowM2ParticleComponent* Flame = NewObject<UWowM2ParticleComponent>(GetOwner(), TEXT("WowBeltTorchBigFlame"));
                Flame->ComponentTags.AddUnique(BigFlameTag);
                Flame->SetupAttachment(Item);
                Flame->SetAbsolute(false, false, true); // full world-torch size, not the 70 % belt torch scale
                GetOwner()->AddInstanceComponent(Flame);
                Flame->RegisterComponent();
                Flame->SetWorldScale3D(FVector(BigFlameScale));
                // r728: the flame emits after the torch has been moved to the hip this frame (it was moved after
                // the particles had already been emitted, so they started a frame behind and trailed off the tip).
                Flame->SetTickGroup(TG_PostUpdateWork);
                Flame->AddTickPrerequisiteComponent(this);
                Flame->SetFixturePath(FlamePath);
                Flame->InitializeFromParsedM2Data(&FlameData, Mpq, Cache);
                // Our own light (below) lights the world; the world torch's fixture light stays off.
                TArray<USceneComponent*> FlameChildren;
                Flame->GetChildrenComponents(true, FlameChildren);
                for (USceneComponent* Child : FlameChildren)
                    if (UPointLightComponent* FixtureLight = Cast<UPointLightComponent>(Child)) FixtureLight->SetHiddenInGame(true);
                UE_LOG(LogWowTorch, Log, TEXT("Torch flame: %s (%d emitters, flame base %s)"), *FlamePath,
                    FlameData.ParticleEmitters.Num(), *GBigFlameEmitterLocal.ToCompactString());
            }
            else
                UE_LOG(LogWowTorch, Warning, TEXT("Torch flame: %s has no emitters; keeping the hand torch's flame"), *FlamePath);
        }
        else
            UE_LOG(LogWowTorch, Warning, TEXT("Torch flame: %s not found; keeping the hand torch's flame"), *FlamePath);
    }
    AttachedMeshAsset = Body->GetSkeletalMeshAsset();

    UPointLightComponent* Point = NewObject<UPointLightComponent>(GetOwner(), TEXT("WowBeltTorchLight"));
    Point->SetupAttachment(Item);
    Point->SetRelativeLocation(FlameLocal);
    GetOwner()->AddInstanceComponent(Point);
    Point->SetMobility(EComponentMobility::Movable);
    Point->SetLightColor(FLinearColor(1.0f, 0.62f, 0.30f));
    Point->SetCastShadows(true);
    Point->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
    Point->SetVolumetricScatteringIntensity(0.0f);
    Point->SetIndirectLightingIntensity(0.0f);
    // r485: a flame-sized source (see the light placement in TickComponent).
    Point->SetSourceRadius(25.0f);
    Point->SetSoftSourceRadius(15.0f);
    Point->RegisterComponent();
    Light = Point;
    UE_LOG(LogWowTorch, Log, TEXT("Torch attached to %s (flame at %s)"), *HipSocket.ToString(), *FlameLocal.ToString());
}

void UWowPlayerTorchComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    // r408 (Stefan: the torch showed while swimming): a torch does not burn in the water; it is
    // hidden (and gives no light) while the character swims, back when it climbs out.
    const AWowPlayerCharacter* Swimmer = Cast<AWowPlayerCharacter>(GetOwner());
    const bool bRealistic = FWowClientPreferences::IsDarkerRealisticLightingEnabled()
        && !(Swimmer && Swimmer->IsInWowWaterSwimmingState());
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;

    // A new model (gear change, HD on/off, shapeshift) replaces the mesh: attach again.
    if (Torch.IsValid() && (!Body || AttachedMeshAsset.Get() != Body->GetSkeletalMeshAsset() || Torch->GetAttachParent() != Body))
        Detach();
    if (!Torch.IsValid() && bRealistic && FPlatformTime::Seconds() >= NextAttachTry)
    {
        NextAttachTry = FPlatformTime::Seconds() + 1.0;
        Attach();
    }
    USceneComponent* T = Torch.Get();
    if (!T || !Body) return;

    T->SetVisibility(bRealistic, true);
    if (UPointLightComponent* L = Light.Get()) L->SetVisibility(bRealistic);
    if (!bRealistic) return;

    // Hang it on the left hip, flame end tilted back and out, so the flame burns behind the hip
    // instead of pointing down like a sheathed mace. r401 (Stefan: it stood up by the shoulder):
    // the full-length torch pointing up reached the head; it is now shorter (70 %), leans 55
    // degrees back from vertical, and its grip sits a little below and behind the hip socket.
    const FTransform Socket = Body->GetSocketTransform(HipSocket, RTS_World);
    const FVector Forward = Character->GetActorForwardVector();
    const FVector Right = Character->GetActorRightVector();
    const float Size = Character->GetActorScale3D().Z;
    // r405 (Stefan, orc woman: "it sits in the belt at its very tip"; then: "or tilt it forward, for
    // more light ahead"): the belt holds the shaft a third of the way up (not the end of the handle),
    // and the flame end leans forward and out, so the body no longer stands between the flame and
    // the way ahead (the realistic shadows stay).
    const FVector Wanted = (FVector::UpVector * 0.50 + Forward * 0.60 - Right * 0.45).GetSafeNormal();
    FQuat Rotation = FQuat::FindBetweenNormals(FlameAxisLocal, Wanted);
    const float TorchScale = 0.7f * Size;
    T->SetWorldScale3D(FVector(TorchScale));
    const float BeltAlongShaft = 0.35f * FlameLocal.Size() * TorchScale;
    T->SetWorldLocationAndRotation(Socket.GetLocation() - Right * 7.0 * Size - Forward * 4.0 * Size - Wanted * BeltAlongShaft, Rotation);

    // r403 (Stefan: a glowing streak ran from the torch across to the sheathed sword): the item's
    // flame emitters fire along the model's own +Z (WoW emitters emit along their local up), and the
    // torch model is authored for the hand, not standing on the belt, so its flames shot out
    // sideways. Keep the flame particles' frame level with the world (model up = world up, turned
    // with the character) and only move them to the flame tip.
    {
        const FVector FlameWorld = T->GetComponentTransform().TransformPosition(FlameLocal);
        const FQuat Upright(FVector::UpVector, FMath::DegreesToRadians(Character->GetActorRotation().Yaw));
        const FVector Scale = T->GetComponentScale();
        TArray<USceneComponent*> Children;
        T->GetChildrenComponents(false, Children);
        for (USceneComponent* Child : Children)
            if (UWowM2ParticleComponent* Flame = Cast<UWowM2ParticleComponent>(Child))
            {
                // r697: the world torch's flame base on the belt torch's tip.
                if (Flame->ComponentHasTag(BigFlameTag))
                {
                    const float FlameScale = BigFlameScale * Size; // r728
                    Flame->SetWorldScale3D(FVector(FlameScale));
                    Flame->SetWorldLocationAndRotation(FlameWorld - Upright.RotateVector(GBigFlameEmitterLocal * FlameScale), Upright);
                    TArray<USceneComponent*> FlameChildren; // fixture lights made later stay off too
                    Flame->GetChildrenComponents(false, FlameChildren);
                    for (USceneComponent* FlameChild : FlameChildren)
                        if (FlameChild && FlameChild->IsA<UPointLightComponent>()) FlameChild->SetHiddenInGame(true); // no-op once hidden
                }
                else
                    Flame->SetWorldLocationAndRotation(FlameWorld - Upright.RotateVector(FlameLocal * Scale), Upright);
            }
    }

    // Flame flicker: two slow waves and a little noise, like the world's torches.
    FlickerTime += DeltaTime;
    if (UPointLightComponent* L = Light.Get())
    {
        // r485 (Stefan, dwarf in a night blizzard: "blocks the torch at every arm-swing, it gets
        // completely dark"): the light sat in the flame, a hand's width from the swinging arm, so the
        // arm threw a hard shadow over half the world with every step (our point-light shadow maps
        // are not soft). The light now shines from a little below and outside the flame, under the
        // arm's swing: the arm's shadow falls up and back, the ground ahead stays lit, and walls,
        // trees and the legs still cast shadows.
        // r695 (Stefan, Shadowglen at night, from behind and in first person: "like a wall in front of us,
        // eventhough the torch is there, pointing forwards. It should also light up from the top tip", "hardly
        // 1 meter of light"): 28 cm below the flame the legs threw long shadows ahead and the 9 m reach faded
        // out a couple of metres in front. The light now burns just above the tip of the flame (the swinging
        // arm is below it, so its shadow falls at the feet, not across the world), 2.5x as bright and reaching
        // 16 m, like a real torch held at the hip. The Fire light options still scale both.
        const FVector FlameWorld = T->GetComponentTransform().TransformPosition(FlameLocal);
        L->SetWorldLocation(FlameWorld + (FVector::UpVector * 14.0 + Forward * 4.0 - Right * 6.0) * Size);
        const float Flicker = 0.86f + 0.08f * FMath::Sin(FlickerTime * 7.3f) + 0.05f * FMath::Sin(FlickerTime * 17.1f + 1.3f)
            + 0.03f * FMath::FRandRange(-1.f, 1.f);
        L->SetIntensity(7000.0f * Flicker * FWowClientPreferences::GetFireLightIntensityScale());
        L->SetAttenuationRadius(1600.0f * FWowClientPreferences::GetFireLightRadiusScale());
    }
}
