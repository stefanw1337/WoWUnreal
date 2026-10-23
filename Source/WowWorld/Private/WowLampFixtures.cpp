#include "WowLampFixtures.h"
#include "WowFixturePolicy.h"
#include "WowDuskLightComponent.h"
#include "WowTerrainMaterial.h"
#include "Formats/M2Types.h"
#include "Formats/WmoTypes.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "GameFramework/Actor.h"
#include "WowClientPreferences.h"

bool FWowLampFixtures::IsSupported(const FString& Path)
{
    return FWowFixturePolicy::IsManagedLamp(Path);
}
bool FWowLampFixtures::IsGlass(const FString& Path)
{
    return Path.Contains(TEXT("STORMWINDLAMPGLASS")) || Path.Contains(TEXT("LIGHTGLASS02")) ||
        Path.Contains(TEXT("STORMWIND_LAMP_WINDOW_")); // Installed HD street-lamp glass.
}
bool FWowLampFixtures::IsOutdoor(const FString& Path) { return Path.Contains(TEXT("STREETLAMP")) || FWowFixturePolicy::IsElwynnRoadLamp(Path); }
bool FWowLampFixtures::IsNightWindow(uint32 Flags,const FString& Path)
{
    // F_SIDN identifies panes authored to brighten at night, not every window.
    return (Flags&0x10u)!=0 && Path.Contains(TEXT("WINDOW"));
}
bool FWowLampFixtures::FindM2Glass(const FM2Data& Data,FBox& Bounds,TArray<int32>& Slots,const FString& Path)
{
    Bounds=FBox(ForceInit);Slots.Reset();
    int32 Slot=0;
    for (const auto& Pass:Data.RenderPasses)
    {
        if (!Data.Submeshes.IsValidIndex(Pass.SubmeshIndex)) continue;
        if (FWowFixturePolicy::SkipLegacyPass(Data,Path.IsEmpty()?TEXT("GENERALHANGINGLANTERN"):Path,Pass)) continue;
        const auto& Sub=Data.Submeshes[Pass.SubmeshIndex];
        if (Sub.NumTriangles<3 || int32(Sub.StartTriangle)+Sub.NumTriangles>Data.Indices.Num()) continue;
        const FString Texture=Data.TexturePaths.IsValidIndex(Pass.TextureIndex)?Data.TexturePaths[Pass.TextureIndex]:FString();
        const bool bWindow=IsGlass(Texture) || ((Path.Contains(TEXT("GENERALLANTERN0")) || FWowFixturePolicy::IsElwynnRoadLamp(Path)) && Texture.Contains(TEXT("ELWYNNLANTERN"))) ||
            (Path.Contains(TEXT("NIGHTELFHANGINGLANTERN")) && (Pass.Flags&1)!=0) ||
            (FWowFixturePolicy::IsBrazier(Path) && (Pass.Flags&1)!=0);
        if (bWindow)
        {
            Slots.Add(Slot);
            for (int32 I=Sub.StartTriangle;I<int32(Sub.StartTriangle)+Sub.NumTriangles;++I)
                if (Data.Vertices.IsValidIndex(Data.Indices[I]))
                {
                    const auto& P=Data.Vertices[Data.Indices[I]].Position;
                    Bounds+=FVector(P.Y,P.X,P.Z)*100.f;
                }
        }
        ++Slot;
    }
    return Bounds.IsValid && !Slots.IsEmpty();
}
bool FWowLampFixtures::FindM2Source(const FM2Data& Data,const FString& Path,FVector& Position,FLinearColor& Color,TArray<int32>& Slots)
{
    if (FWowFixturePolicy::IsHearth(Path))
    {
        Slots.Reset();
        // Use the active flame source, never the mesh pivot or an ember above it.
        for (const auto& E:Data.ParticleEmitters)
        {
            if (!E.bEnabledInStand || !Data.TexturePaths.IsValidIndex(E.Texture) ||
                !FWowFixturePolicy::IsFlameTexture(Data.TexturePaths[E.Texture])) continue;
            Position=FVector(E.Position.Y,E.Position.X,E.Position.Z)*100.f+FVector(0,0,35);
            Color=FLinearColor(E.ColorMiddle[0]/255.f,E.ColorMiddle[1]/255.f,E.ColorMiddle[2]/255.f);
            if (Color.GetMax()<.01f) Color=FLinearColor(1,.58f,.22f);
            return true;
        }
        return false; // An empty/unlit firepit must not acquire a light.
    }
    FBox Bounds;const bool bGlass=FindM2Glass(Data,Bounds,Slots,Path);
    Color=FLinearColor(1,.58f,.22f);
    if (bGlass) Position=Bounds.GetCenter();
    if (FWowFixturePolicy::IsMagicalLamp(Path) || FWowFixturePolicy::IsBrazier(Path))
    {
        for (const auto& E:Data.ParticleEmitters)
        {
            if (!E.bEnabledInStand || !Data.TexturePaths.IsValidIndex(E.Texture)) continue;
            const FString& Texture=Data.TexturePaths[E.Texture];
            const bool bSource=FWowFixturePolicy::IsBrazier(Path)?Texture.Contains(TEXT("LAVALUMP")):
                (Texture.Contains(TEXT("flame")) || Texture.Contains(TEXT("glowball")) ||
                    (Path.Contains(TEXT("NIGHTELFHANGINGLANTERN")) && Texture.Contains(TEXT("glow"))));
            if (!bSource) continue;
            Position=FVector(E.Position.Y,E.Position.X,E.Position.Z)*100.f;
            if (FWowFixturePolicy::IsBrazier(Path) && bGlass) Position.Z=FMath::Max(Position.Z,Bounds.Max.Z+8.f);
            if (FWowFixturePolicy::IsMagicalLamp(Path))
                Color=FLinearColor(E.ColorMiddle[0]/255.f,E.ColorMiddle[1]/255.f,E.ColorMiddle[2]/255.f);
            return true;
        }
    }
    return bGlass;
}
namespace
{
TArray<UMaterialInstanceDynamic*> LightGlass(UMeshComponent* Mesh,const TArray<int32>& Slots,const FString& Path=FString())
{
    TArray<UMaterialInstanceDynamic*> Materials;
    for (int32 Slot:Slots)
    {
        UTexture* Texture=nullptr;
        auto* Previous=Mesh->GetMaterial(Slot);
        if (!Previous || !Previous->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Layer0Texture")),Texture)) continue;
        // WMO opaque window alpha is not a transparency mask. Preserve its
        // authored coverage so frames and mullions cannot disappear at night.
        auto* Base=Path==TEXT("NightWindow") && Previous->GetBlendMode()==BLEND_Opaque
            ?FWowTerrainMaterial::GetOpaqueWindowLightMaterial():FWowTerrainMaterial::GetLampGlassMaterial();
        if (!Base) continue;
        // Instanced street lamps share material slots: reuse the existing glass MID.
        auto* Material=Cast<UMaterialInstanceDynamic>(Previous);
        if (!Material || Material->Parent!=Base) Material=UMaterialInstanceDynamic::Create(Base,Mesh);
        Material->SetTextureParameterValue(TEXT("Layer0Texture"),Texture);
        Material->SetScalarParameterValue(TEXT("LampGlow"),0);
        // Select lit glass/coal colors, leaving dark frames and metal non-emissive.
        const FLinearColor Mask=Path==TEXT("NightWindow")?FLinearColor(0,3,0,-.18f):(Path.Contains(TEXT("GENERALLANTERN0")) || FWowFixturePolicy::IsElwynnRoadLamp(Path)) || FWowFixturePolicy::IsBrazier(Path)
            ?FLinearColor(0,6,-6,-.48f):Path.Contains(TEXT("NIGHTELFHANGINGLANTERN"))
                ?FLinearColor(-5,0,5,-.15f):FLinearColor(0,0,0,1);
        Material->SetVectorParameterValue(TEXT("LampEmissionMask"),Mask);
        Mesh->SetMaterial(Slot,Material); Materials.Add(Material);
    }
    return Materials;
}
UWowDuskLightComponent* AddLight(AActor* Owner,const FVector& Position,bool bOutdoor,const TArray<UMaterialInstanceDynamic*>& Materials,const FLinearColor& Color=FLinearColor(1,.58f,.22f),float Intensity=0)
{
    auto* Light=NewObject<UWowDuskLightComponent>(Owner);
    Owner->AddInstanceComponent(Light);
    Light->SetupAttachment(Owner->GetRootComponent());
    Light->SetWorldLocation(Position);
    Light->bOutdoor=bOutdoor;
    Light->FixtureIntensity=Intensity;Light->SetLightColor(Color);
    for (auto* Material:Materials) Light->GlassMaterials.Add(Material);
    Light->ComponentTags.Add(TEXT("WowFixtureLight"));
    Light->SetCastShadows(true);
    // A lantern is an area source; a zero-radius point creates razor-edged
    // silhouettes of its metal frame on nearby masonry.
    // r321: a lamp flame is a few cm, but its glass/glow spreads the light; larger source radii
    // give the soft shadow edges of the original look instead of razor-sharp ones.
    Light->SetSourceRadius(8.f*FWowClientPreferences::GetFireLightSoftness());
    Light->SetSoftSourceRadius(12.f*FWowClientPreferences::GetFireLightSoftness());
    Light->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
    Light->SetVolumetricScatteringIntensity(0);
    // r554 (Stefan, a lantern by the Stormwind canal at night: "the lantern light is in the water, not in
    // the lantern"): the mirror-smooth water reflected the light's small bright source as a disc below the
    // lantern, far brighter than the lantern's own glass. Lamps light their surroundings; their highlight
    // on glossy surfaces (water, wet stone) is kept faint.
    Light->SetSpecularScale(0.25f);
    Light->SetIndirectLightingIntensity(FWowClientPreferences::GetFireLightIndirectScale());
    Light->ShadowResolutionScale=.5f;
    Light->SetAttenuationRadius((bOutdoor?1100.f:850.f)*FWowClientPreferences::GetFireLightRadiusScale());
    Light->SetMaxDrawDistance(7000);
    Light->RegisterComponent();
    return Light;
}
}
bool FWowLampFixtures::AttachM2(AActor* Owner,UMeshComponent* Mesh,const FM2Data& Data,
    const FString& Path,const FTransform& Transform)
{
    if (!Owner || (!Mesh && !FWowFixturePolicy::IsHearth(Path)) || !IsSupported(Path)) return false;
    FVector Position;FLinearColor Color;TArray<int32> Slots;
    if (!FindM2Source(Data,Path,Position,Color,Slots)) return false;
    auto Materials=Mesh?LightGlass(Mesh,Slots,Path):TArray<UMaterialInstanceDynamic*>();
    // r696 (Stefan, Shadowglen at night with darker realistic lighting: "These lamps ... could use some lighting",
    // "They are very weak"): Teldrassil's Kalidar street lamps (already outdoor lamps: 11 m) burn at 5200 like
    // the other street lamps instead of the 2500 of the small night elf lanterns.
    const float Intensity=FWowFixturePolicy::IsHearth(Path)?2600.f:FWowFixturePolicy::IsBrazier(Path)?2200.f:
        Path.Contains(TEXT("KALIDARSTREETLAMP"))?5200.f:FWowFixturePolicy::IsMagicalLamp(Path)?2500.f:
        (Path.Contains(TEXT("GENERALLANTERN0")) || FWowFixturePolicy::IsElwynnRoadLamp(Path))?1800.f:0.f;
    // A brazier is a bed of embers, not a lamp: a soft, low glow just above the coals.
    // At full strength from inside the box it threw long hard shadows across the ground.
    const bool bBrazier=FWowFixturePolicy::IsBrazier(Path);
    auto* Light=AddLight(Owner,Transform.TransformPosition(Position)+(bBrazier?FVector(0,0,35.f):FVector::ZeroVector),
        IsOutdoor(Path),Materials,Color,bBrazier?800.f:Intensity);
    if (bBrazier && Light) { Light->SetAttenuationRadius(520.f); Light->SetSourceRadius(30.f); Light->SetSoftSourceRadius(40.f); }
    return true;
}
void FWowLampFixtures::AttachWmo(AActor* Owner,UMeshComponent* Mesh,const FWmoGroupData& Data,const FWmoRootData& Root)
{
    TArray<int32> Slots; TArray<FBox> Lamps;
    for (int32 Slot=0;Slot<Data.Batches.Num();++Slot)
    {
        const auto& Batch=Data.Batches[Slot];
        if (!Root.Materials.IsValidIndex(Batch.MaterialIndex) || !IsGlass(Root.Materials[Batch.MaterialIndex].TexturePath1)) continue;
        Slots.Add(Slot);
        for (uint32 I=Batch.IndexStart;I+2<Batch.IndexStart+Batch.IndexCount && I+2<uint32(Data.Indices.Num());I+=3)
        {
            FBox Box(ForceInit);
            for (int32 J=0;J<3;++J) if (Data.Vertices.IsValidIndex(Data.Indices[I+J]))
            {const auto& P=Data.Vertices[Data.Indices[I+J]];Box+=FVector(P.Y,P.X,P.Z)*100.f;}
            if (!Box.IsValid) continue;
            // Weld disconnected glass panels of one lamp, never the lamp posts
            // or windows. Merge transitively across UV seams and batches.
            for (int32 J=0;J<Lamps.Num();)
            {
                if (Box.ExpandBy(15).Intersect(Lamps[J])) {Box+=Lamps[J];Lamps.RemoveAtSwap(J);J=0;}
                else ++J;
            }
            Lamps.Add(Box);
        }
    }
    if (Lamps.IsEmpty()) return;
    auto Materials=LightGlass(Mesh,Slots);
    bool First=true;
    for (const auto& Lamp:Lamps)
    {
        AddLight(Owner,Mesh->GetComponentTransform().TransformPosition(Lamp.GetCenter()),true,
            First?Materials:TArray<UMaterialInstanceDynamic*>());
        First=false;
    }
}

void FWowLampFixtures::AttachWmoWindows(AActor* Owner,UMeshComponent* Mesh,const FWmoGroupData& Data,const FWmoRootData& Root)
{
    if (!Owner || !Mesh) return;
    struct FWindow { FBox Box=FBox(ForceInit); FVector Normal=FVector::ZeroVector; };
    TArray<FWindow> Windows;TArray<int32> Slots;
    for (int32 Slot=0;Slot<Data.Batches.Num();++Slot)
    {
        const auto& Batch=Data.Batches[Slot];
        if (!Root.Materials.IsValidIndex(Batch.MaterialIndex)) continue;
        const auto& Material=Root.Materials[Batch.MaterialIndex];
        if (!IsNightWindow(Material.Flags,Material.TexturePath1)) continue;
        Slots.Add(Slot);
        for (uint32 I=Batch.IndexStart;I+2<Batch.IndexStart+Batch.IndexCount && I+2<uint32(Data.Indices.Num());I+=3)
        {
            FWindow Window;
            for (uint32 J=0;J<3;++J)
            {
                const int32 Index=Data.Indices[I+J];
                if (!Data.Vertices.IsValidIndex(Index)) continue;
                const FVector P=Data.Vertices[Index];Window.Box+=FVector(P.Y,P.X,P.Z)*100;
                if (Data.Normals.IsValidIndex(Index))
                {const FVector N=Data.Normals[Index];Window.Normal+=FVector(N.Y,N.X,N.Z);}
            }
            if (!Window.Box.IsValid || Window.Normal.IsNearlyZero()) continue;
            Window.Normal.Normalize();
            for (int32 J=0;J<Windows.Num();)
            {
                if (Window.Box.ExpandBy(3).Intersect(Windows[J].Box) && FVector::DotProduct(Window.Normal,Windows[J].Normal)>.9)
                { Window.Box+=Windows[J].Box;Windows.RemoveAtSwap(J);J=0; }
                else ++J;
            }
            Windows.Add(Window);
        }
    }
    if (Windows.IsEmpty()) return;
    auto Materials=LightGlass(Mesh,Slots,TEXT("NightWindow"));
    bool First=true;
    for (const auto& Window:Windows)
    {
        // Position just outside the pane, so its own wall doesn't swallow the light.
        const FVector Position=Mesh->GetComponentTransform().TransformPosition(Window.Box.GetCenter()+Window.Normal*12);
        auto* Light=AddLight(Owner,Position,true,First?Materials:TArray<UMaterialInstanceDynamic*>(),FLinearColor(1,.64f,.30f),450.f*3.f/FMath::Max(3.f,FWowClientPreferences::GetFireLightIntensityScale())); // r294: windows keep the r287 level
        Light->SetAttenuationRadius(350.f);Light->SetMaxDrawDistance(3500.f);
        Light->ComponentTags.Add(TEXT("WowWindowLight"));
        First=false;
    }
}
