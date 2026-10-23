#include "WowWeatherComponent.h"
#include "WowSkyManager.h"
#include "WowDuskLightComponent.h"
#include "WowClientPreferences.h"
#include "WowAudioManager.h"
#include "WowTestControls.h"
#include "Components/LocalFogVolumeComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

void UWowWeatherComponent::UpdateAmbientAtmosphere(float Dt)
{
    const bool Enabled = FWowClientPreferences::GetWorldOption(TEXT("ambientweather"));
    const bool Volumetric = Enabled && FWowClientPreferences::IsVolumetricFogEnabled();
    // Opt into real light scattering; the legacy analytical path remains available.
    if (auto* C = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LocalFogVolume.RenderIntoVolumetricFog")))
        if (C->GetInt() != int32(Volumetric)) C->Set(int32(Volumetric),ECVF_SetByCode);
    const auto* Preview = IConsoleManager::Get().FindConsoleVariable(TEXT("wow.WeatherPreview"));
    const bool PreviewActive = WowTestControls::Enabled() && Preview && Preview->GetInt() != 0;
    const bool Active = Enabled && !bFixture && !PreviewActive && Zone != 0 && bHaveCamera
        && !(Audio.IsValid() && Audio->IsUnderwaterMixEnabled());
    if (!Active)
    {
        for (const auto& Mist : AmbientMist) if (Mist) Mist->SetVisibility(false);
        AWowSkyManager::SetAmbientHaze(0.f);
        return;
    }
    constexpr float CellSize = 6000.f;
    const FIntPoint Center(FMath::FloorToInt(LastCamera.X/CellSize),FMath::FloorToInt(LastCamera.Y/CellSize));
    while (AmbientMist.Num() < 9)
    {
        auto* Mist = NewObject<ULocalFogVolumeComponent>(GetOwner());
        Mist->SetMobility(EComponentMobility::Movable);
        Mist->SetVisibility(false);
        Mist->SetFogPhaseG(.55f);
        Mist->SetFogEmissive(FLinearColor::Black);
        Mist->SetHeightFogFalloff(1500.f);
        GetOwner()->AddInstanceComponent(Mist);
        Mist->RegisterComponent();
        AmbientMist.Add(Mist); AmbientPatches.AddDefaulted();
    }
    // Stable world cells prevent the atmosphere from following the camera.
    for (int32 Y=-1; Y<=1; ++Y) for (int32 X=-1; X<=1; ++X)
    {
        const FIntPoint Cell = Center+FIntPoint(X,Y);
        const int32 Slot = ((Cell.X%3+3)%3)+3*((Cell.Y%3+3)%3);
        auto& Patch = AmbientPatches[Slot];
        if (Patch.Cell != Cell)
        {
            Patch.Cell = Cell; Patch.Valid = false; Patch.Fade = 0.f;
            AmbientMist[Slot]->SetVisibility(false);
        }
    }
    const double Time = GetWorld()->GetTimeSeconds();
    if (Time >= NextAmbientProbe)
    {
        NextAmbientProbe = Time + .25;
        auto& Patch = AmbientPatches[AmbientProbe++%9];
        FRandomStream Seed(uint32(Patch.Cell.X)*73856093u ^ uint32(Patch.Cell.Y)*19349663u ^ Zone);
        const FVector Sample((Patch.Cell.X+.5f)*CellSize+Seed.FRandRange(-900.f,900.f),
            (Patch.Cell.Y+.5f)*CellSize+Seed.FRandRange(-900.f,900.f),LastCamera.Z);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(AmbientWeatherGround),true);
        Params.AddIgnoredActor(GetOwner());
        if (auto* PC=UGameplayStatics::GetPlayerController(this,0))
            if (PC->GetPawn()) Params.AddIgnoredActor(PC->GetPawn());
        FHitResult Ground, Roof;
        const auto Objects = FCollisionObjectQueryParams(ECC_WorldStatic);
        const bool Found = GetWorld()->LineTraceSingleByObjectType(Ground,Sample+FVector(0,0,1500),
            Sample-FVector(0,0,6000),Objects,Params);
        Patch.Valid = Found && Ground.ImpactNormal.Z > .65f
            && !GetWorld()->LineTraceSingleByObjectType(Roof,Ground.ImpactPoint+FVector(0,0,80),
                Ground.ImpactPoint+FVector(0,0,12000),Objects,Params);
        if (Patch.Valid) Patch.Anchor = Ground.ImpactPoint;
    }
    float Amount = WowWeather::AmbientStrength(Zone,Time,Current.Wind);
    // r702/r703 (Stefan, Shadowglen at night: "no haze or anything"): old forests keep a thicker mist at night -
    // up to 1.8x between dusk and dawn (Teldrassil, Darnassus, Ashenvale, Felwood, Duskwood, Grizzly Hills).
    if (Zone == 141 || Zone == 1657 || Zone == 331 || Zone == 361 || Zone == 10 || Zone == 394)
    {
        const auto* Sky = Cast<AWowSkyManager>(GetOwner());
        const float Night = UWowDuskLightComponent::NightFactor(Sky ? Sky->GetTimeOfDay() : 720.f);
        Amount *= FMath::Lerp(1.f, 1.8f, Night);
    }
    AWowSkyManager::SetAmbientHaze(Amount * Exposure); // r438: the same haze, landscape-wide
    const FLinearColor Tint = ZoneClimate==EWowClimate::Dry ? FLinearColor(.72f,.59f,.40f)
        : ZoneClimate==EWowClimate::Ash ? FLinearColor(.42f,.40f,.38f)
        : ZoneClimate==EWowClimate::Cold ? FLinearColor(.82f,.89f,.98f)
        : FLinearColor(.83f,.88f,.90f);
    for (int32 I=0; I<9; ++I)
    {
        auto& Patch = AmbientPatches[I];
        auto* Mist = AmbientMist[I].Get();
        const float Distance = FVector::Dist2D(LastCamera,Patch.Anchor);
        const float Edge = FMath::Clamp((10000.f-Distance)/2500.f,0.f,1.f);
        Patch.Fade = FMath::FInterpConstantTo(Patch.Fade,Patch.Valid?Edge*Exposure:0.f,Dt,.15f);
        const double Phase = double((uint32(Patch.Cell.X)*31u ^ uint32(Patch.Cell.Y)*17u)%997u);
        const float Breathing = .55f+.45f*FMath::Square(float(FMath::Sin(Time/71.0+Phase)));
        Mist->SetWorldLocation(Patch.Anchor+FVector(0,0,90));
        Mist->SetWorldScale3D(FVector(5.f));
        // Both density profiles must fade together, including in shadow.
        // A fixed radial extinction leaves dense dark patches at low strength.
        const float Density = Amount * Breathing * Patch.Fade;
        Mist->SetRadialFogExtinction(.32f * Density);
        Mist->SetHeightFogExtinction(Density);
        Mist->SetFogAlbedo(Tint);
        Mist->SetFogEmissive(Tint*FogVolumeSelfLight()); // r477: no black mist in shadow
        Mist->SetVisibility(Patch.Fade>.002f);
    }
}
