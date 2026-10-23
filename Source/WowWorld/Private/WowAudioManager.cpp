#include "WowAudioManager.h"
#include "WowWorldManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "WowClientPreferences.h"
#include "Misc/App.h"
#include "WowWeatherPolicy.h"
#include "WowRuntimeAudioDecoder.h"
#include "Mpq/MpqManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundAttenuation.h"
#include "WowRuntimeSoundWave.h"
#include "Kismet/GameplayStatics.h"
#include "SoundFileIO/SoundFileIO.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowAudio, Log, All);
namespace { bool bZoneMusicSilent = false; } // current area has no zone music (one audio manager)

AWowAudioManager::AWowAudioManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.05f; // 20Hz for smooth crossfade
}

void AWowAudioManager::BeginPlay()
{
    Super::BeginPlay();

#if WITH_EDITOR || !PLATFORM_WINDOWS
    Audio::SoundFileUtils::InitSoundFileIOManager();
#endif

    // Create two music audio components for crossfading (A/B pattern)
    MusicComponentA = NewObject<UAudioComponent>(this, TEXT("MusicA"));
    MusicComponentA->SetupAttachment(GetRootComponent());
    MusicComponentA->bAutoActivate = false;
    MusicComponentA->bIsUISound = true; // Non-positional
    MusicComponentA->RegisterComponent();

    MusicComponentB = NewObject<UAudioComponent>(this, TEXT("MusicB"));
    MusicComponentB->SetupAttachment(GetRootComponent());
    MusicComponentB->bAutoActivate = false;
    MusicComponentB->bIsUISound = true;
    MusicComponentB->RegisterComponent();

    AmbienceComponent = NewObject<UAudioComponent>(this, TEXT("Ambience"));
    AmbienceComponent->SetupAttachment(GetRootComponent());
    AmbienceComponent->bAutoActivate = false;
    AmbienceComponent->bIsUISound = true;
    AmbienceComponent->RegisterComponent();

    WeatherComponent = NewObject<UAudioComponent>(this, TEXT("Weather"));
    WeatherComponent->SetupAttachment(GetRootComponent());
    WeatherComponent->bAutoActivate = false;
    WeatherComponent->bIsUISound = true;
    WeatherComponent->RegisterComponent();

    WindComponent=NewObject<UAudioComponent>(this,TEXT("WeatherWind"));
    WindComponent->SetupAttachment(GetRootComponent());
    WindComponent->bAutoActivate=false;
    WindComponent->bIsUISound=true;
    WindComponent->RegisterComponent();
    UE_LOG(LogWowAudio, Log, TEXT("Audio manager initialized"));
}

namespace { uint32 GWowWorldLoadingSources = 0; }

void AWowAudioManager::SetWorldLoading(uint32 SourceBit, bool bLoading)
{
    if (bLoading) GWowWorldLoadingSources |= SourceBit; else GWowWorldLoadingSources &= ~SourceBit;
}

bool AWowAudioManager::IsUiSound(const UAudioComponent* Component) const
{
    const USoundWave* Wave = Component ? Cast<USoundWave>(Component->Sound) : nullptr;
    return Wave && Wave->SoundGroup == SOUNDGROUP_UI;
}

void AWowAudioManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // r297: quick fade out when loading starts, a slower fade back in once the world is there.
    LoadingDuck = GWowWorldLoadingSources != 0
        ? FMath::FInterpConstantTo(LoadingDuck, 0.0f, DeltaTime, 4.0f)
        : FMath::FInterpConstantTo(LoadingDuck, 1.0f, DeltaTime, 0.6f);
    FApp::SetUnfocusedVolumeMultiplier(FWowClientPreferences::GetWorldOption(TEXT("soundinbackground")) ? 1.0f : 0.0f);
    ApplyAmbientMix();
    for (int32 I=PlayingEffects.Num()-1; I>=0; --I)
    {
        auto& Effect=PlayingEffects[I];
        Effect.Remaining-=DeltaTime;
        if (!Effect.Component.IsValid() || !Effect.Component->IsPlaying() || Effect.Remaining<=0)
        {
            if (Effect.Component.IsValid()) Effect.Component->Stop();
            PlayingEffects.RemoveAtSwap(I);
        }
        else Effect.Component->SetVolumeMultiplier(MasterVolume*SfxVolume*GetEffectMixScale()*Effect.Scale
            *(IsUiSound(Effect.Component.Get()) ? 1.0f : LoadingDuck));
    }
    if (bMusicScheduled)
    {
        const bool Loop=FWowClientPreferences::GetWorldOption(TEXT("loopmusic"));
        if (bMusicWaiting)
        {
            MusicSilenceRemaining-=DeltaTime;
            if (Loop || MusicSilenceRemaining<=0)
            { if (bLoginTrack) PlayLoginMusic(); else UpdateMusic(); }
        }
        else
        {
            MusicElapsed+=DeltaTime;
            if (Loop && MusicLength>0) MusicElapsed=FMath::Fmod(MusicElapsed,MusicLength);
            if (!Loop && MusicElapsed>=MusicLength)
            {
                if (MusicComponentA) MusicComponentA->Stop();
                if (MusicComponentB) MusicComponentB->Stop();
                bMusicWaiting=true;
                MusicSilenceRemaining=FMath::FRandRange(15.0f,30.0f);
            }
        }
    }

    if (UnderwaterMixBlend != UnderwaterMixTarget)
    {
        UnderwaterMixBlend = FMath::FInterpConstantTo(UnderwaterMixBlend, UnderwaterMixTarget, DeltaTime,
            1.0f / FWowUnderwaterAudioPolicy::BlendSeconds);
        ApplyAmbientMix();
    }

    // Handle crossfade
    if (CrossfadeProgress < 1.0f)
    {
        CrossfadeProgress = FMath::Min(CrossfadeProgress + DeltaTime / CrossfadeDuration, 1.0f);

        float FadeIn = CrossfadeProgress;
        float FadeOut = 1.0f - CrossfadeProgress;
        const float MusicScale = GetMusicMixScale();

        UAudioComponent* ActiveComp = bMusicAActive ? MusicComponentA : MusicComponentB;
        UAudioComponent* FadingComp = bMusicAActive ? MusicComponentB : MusicComponentA;

        if (ActiveComp) ActiveComp->SetVolumeMultiplier(FadeIn * MusicVolume * MasterVolume * MusicScale);
        if (FadingComp) FadingComp->SetVolumeMultiplier(FadeOut * MusicVolume * MasterVolume * MusicScale);

        if (CrossfadeProgress >= 1.0f && FadingComp)
        {
            FadingComp->Stop();
        }
    }
}

void AWowAudioManager::SetCurrentZone(uint32 ZoneId, uint32 AreaId)
{
    if (bGlueScreen) return; // character select: the world behind the glue scene stays silent
    const bool bMusicIsPlaying =
        (MusicComponentA && MusicComponentA->IsPlaying()) ||
        (MusicComponentB && MusicComponentB->IsPlaying());
    const bool bAmbienceIsPlaying = AmbienceComponent && AmbienceComponent->IsPlaying();
    if (ZoneId == CurrentZoneId && AreaId == CurrentAreaId && !bLoginTrack &&
        (bMusicIsPlaying || bMusicScheduled || (PendingZoneMusicId == 0 && bZoneMusicSilent)) && bAmbienceIsPlaying)
    {
        return;
    }

    CurrentZoneId = ZoneId;
    CurrentAreaId = AreaId;

    // Look up zone music from AreaTable.dbc
    const FAreaTableDbcEntry* AreaEntry = FDbcStore::Get().AreaTable().GetById(AreaId);
    if (!AreaEntry)
    {
        AreaEntry = FDbcStore::Get().AreaTable().GetById(ZoneId);
    }

    uint32 NewMusicId = 0;
    uint32 NewAmbienceId = 0;
    if (AreaEntry)
    {
        NewMusicId = AreaEntry->ZoneMusicID;
        NewAmbienceId = AreaEntry->AmbienceID;
    }

    const bool bMusicChanged = NewMusicId != PendingZoneMusicId;
    const bool bAmbienceChanged = NewAmbienceId != PendingAmbienceId;
    PendingZoneMusicId = NewMusicId;
    PendingAmbienceId = NewAmbienceId;

    // Offline streaming can report area 0 before the first terrain chunk has
    // finished loading. Start the audible fallback immediately, then replace it
    // with the DBC/MPQ sound as soon as a real zone is resolved.
    // The login/character-select track never continues into the world (it kept playing in
    // areas without zone music, e.g. custom server zones).
    if (AreaEntry && NewMusicId == 0)
    {
        // A real area without music is silent, as in WoW (no generated fallback tone).
        if (!bZoneMusicSilent || bLoginTrack || bMusicIsPlaying || bMusicScheduled)
        {
            bLoginTrack = false;
            StopMusic();
        }
        bZoneMusicSilent = true;
    }
    else if (bMusicChanged || bLoginTrack || (!bMusicIsPlaying && !bMusicScheduled))
    {
        bZoneMusicSilent = false;
        UpdateMusic();
    }
    if (bAmbienceChanged || !bAmbienceIsPlaying)
    {
        UpdateAmbience();
    }

    UE_LOG(LogWowAudio, Log, TEXT("Zone changed: zone=%d area=%d musicId=%d ambienceId=%d"),
        ZoneId, AreaId, PendingZoneMusicId, PendingAmbienceId);
}

void AWowAudioManager::SetUnderwaterMixEnabled(bool bEnabled)
{
    const float NewTarget = bEnabled ? 1.0f : 0.0f;
    if (bUnderwaterMixEnabled == bEnabled && FMath::IsNearlyEqual(UnderwaterMixBlend, NewTarget))
    {
        return;
    }

    bUnderwaterMixEnabled = bEnabled;
    UnderwaterMixTarget = NewTarget;
    // Short constant-rate fade (BlendSeconds, Tick) with an exponential cutoff
    // sweep: reads as crossing the surface without a hard click.
    ApplyAmbientMix();
    UE_LOG(LogWowAudio, Verbose, TEXT("Underwater audio mix: %s (low-pass %.0f Hz)"),
        bEnabled ? TEXT("enabled") : TEXT("disabled"),
        FWowUnderwaterAudioPolicy::ResolveLowPassFrequency(NewTarget));
}

void AWowAudioManager::SetMasterVolume(float NewVolume)
{
    MasterVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
    ApplyAmbientMix();
}

void AWowAudioManager::SetMusicVolume(float NewVolume)
{
    MusicVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
    ApplyAmbientMix();
}

void AWowAudioManager::SetSfxVolume(float NewVolume)
{
    SfxVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
}

void AWowAudioManager::SetAmbienceVolume(float NewVolume)
{
    AmbienceVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
    ApplyAmbientMix();
}

void AWowAudioManager::SetErrorSpeechEnabled(bool bEnabled)
{
    bErrorSpeechEnabled = bEnabled;
}

void AWowAudioManager::SetDeathKnightReverbEnabled(bool bEnabled)
{
    bDeathKnightReverbEnabled = bEnabled;
}

void AWowAudioManager::StopMusic()
{
    bMusicScheduled=false; bMusicWaiting=false;
    if (MusicComponentA)
    {
        MusicComponentA->Stop();
    }
    if (MusicComponentB)
    {
        MusicComponentB->Stop();
    }
    CrossfadeProgress = 1.0f;
}

void AWowAudioManager::StopAmbience()
{
    if (AmbienceComponent)
    {
        AmbienceComponent->Stop();
    }
    if (WeatherComponent)
    {
        WeatherComponent->Stop();
    }
    if(ThunderComponent) ThunderComponent->Stop();
    if(WindComponent) WindComponent->Stop();
    CurrentWindVolume=0;
    CurrentWeatherType = 0;
    CurrentWeatherGrade = 0.0f;
}

void AWowAudioManager::ScanUserThunderSounds()
{
    bUserThunderScanned = true;
    UserThunderFiles.Reset();
    const FString Folder = FPaths::Combine(FMpqManager::FindUserRoot(FPaths::ProjectDir()), TEXT("UserSounds"), TEXT("Thunder"));
    TArray<FString> Found;
    for (const TCHAR* Pattern : {TEXT("*.wav"), TEXT("*.ogg"), TEXT("*.mp3")})
    {
        IFileManager::Get().FindFiles(Found, *FPaths::Combine(Folder, Pattern), true, false);
        for (const FString& Name : Found) UserThunderFiles.AddUnique(FPaths::Combine(Folder, Name));
        Found.Reset();
    }
    UserThunderFiles.Sort();
    UE_LOG(LogWowAudio, Log, TEXT("User thunder sounds: %d in %s"), UserThunderFiles.Num(), *Folder);
}

void AWowAudioManager::PlayWeatherThunder(float Strength)
{
    // Respects ambience settings. Personal thunder files first, generated rumble as fallback.
    if(GetAmbienceMixScale()<=0.f) return;
    ThunderPlaybackStrength=FMath::Clamp(Strength,0.f,1.f);
    // Strong = near strike (sharp crack), weak = far away (rolling rumble, high end absorbed).
    const bool bClose=ThunderPlaybackStrength>=0.55f;
    USoundWave* Sound=nullptr;
    if(!bUserThunderScanned) ScanUserThunderSounds();
    if(UserThunderFiles.Num()>0)
    {
        TArray<int32> Pool;
        for(int32 I=0;I<UserThunderFiles.Num();++I)
        {
            const FString Name=FPaths::GetCleanFilename(UserThunderFiles[I]);
            const bool bNamedClose=Name.Contains(TEXT("close"),ESearchCase::IgnoreCase);
            const bool bNamedDistant=Name.Contains(TEXT("distant"),ESearchCase::IgnoreCase);
            if((bClose && !bNamedDistant) || (!bClose && !bNamedClose)) Pool.Add(I);
        }
        if(Pool.Num()==0) for(int32 I=0;I<UserThunderFiles.Num();++I) Pool.Add(I);
        if(Pool.Num()>1) Pool.Remove(LastUserThunderIndex);
        const int32 Index=Pool[FMath::RandRange(0,Pool.Num()-1)];
        TArray<uint8> Bytes;
        const FString& Path=UserThunderFiles[Index];
        if(DecodedSoundCache.Contains(Path) || FFileHelper::LoadFileToArray(Bytes,*Path))
            Sound=LoadSoundFromData(Path,MoveTemp(Bytes));
        if(Sound) LastUserThunderIndex=Index;
    }
    if(!Sound) Sound=GetOrCreateFallbackNoise(TEXT("Fallback:Weather:Thunder"),5.5f,0.9f,false,SOUNDGROUP_Effects);
    WeatherThunderSound=Sound;
    if(WeatherThunderSound) {
        ThunderDistanceLowPassHz=bClose ? 20000.f : FMath::Lerp(1400.f,4000.f,FMath::Clamp((ThunderPlaybackStrength-0.2f)/0.35f,0.f,1.f));
        // Thunder must cut through rain and music: a close strike is the loudest sound in the
        // world (well above the ambience bed), distant rolls stay clearly audible.
        ThunderPlaybackStrength=(bClose ? 3.2f : 1.9f)*FMath::Lerp(0.55f,1.f,ThunderPlaybackStrength);
        if(ThunderComponent) ThunderComponent->Stop();
        ThunderComponent=UGameplayStatics::SpawnSound2D(this,WeatherThunderSound,MasterVolume*AmbienceVolume*GetAmbienceMixScale()*ThunderPlaybackStrength,
            FMath::FRandRange(0.92f,1.05f));
        ApplyAmbientMix();
    }
}

// r548 (Stefan: the rain "sounds like someone running in water, footsteps in a regular beat"): the
// weather loop's level was written in two places with two formulas - SetWeatherAudio (every 0.5 s, the
// r530 file level 0.55-1.0) and ApplyAmbientMix (every frame, the old noise level = weather grade, ~0.1-0.3).
// The rain jumped up for one frame twice a second. One shared level now, used by both.
static float GWeatherLoopLevel = 0.f;

void AWowAudioManager::SetWeatherAudio(uint32 WeatherType, float Grade, float WindSpeed)
{
    CurrentWeatherType = WeatherType;
    const EWowWeatherKind Kind=WowWeather::DecodeState(WeatherType);
    const bool Rain=Kind==EWowWeatherKind::Rain||Kind==EWowWeatherKind::Storm||Kind==EWowWeatherKind::BlackRain;
    const bool Quiet=Kind==EWowWeatherKind::Clear||Kind==EWowWeatherKind::Fog;
    CurrentWeatherGrade=Quiet?0.f:FMath::Clamp(Grade,0.f,1.f)*(Rain?1.f:Kind==EWowWeatherKind::Dust?0.35f:0.10f);

    if (!WeatherComponent)
    {
        return;
    }

    CurrentWindVolume=FMath::Clamp((WindSpeed-1.f)/22.f,0.f,1.f)*0.8f;
    if(WindComponent) {
        if(CurrentWindVolume>0.005f) {
            if(!WeatherGustSound) WeatherGustSound=GetOrCreateFallbackNoise(TEXT("Fallback:Weather:Wind"),8.f,0.85f,true,SOUNDGROUP_Effects);
            if(WindComponent->GetSound()!=WeatherGustSound || !WindComponent->IsPlaying()) {
                WindComponent->SetSound(WeatherGustSound); WindComponent->Play();
            }
            WindComponent->SetPitchMultiplier(Kind==EWowWeatherKind::Dust?1.10f:Kind==EWowWeatherKind::Snow?0.85f:0.95f);
            WindComponent->SetVolumeMultiplier(MasterVolume*AmbienceVolume*GetAmbienceMixScale()*CurrentWindVolume);
        } else WindComponent->Stop();
    }
    if (WeatherType == 0 || CurrentWeatherGrade <= 0.01f)
    {
        WeatherComponent->Stop();
        return;
    }

    USoundWave* Sound = nullptr;

    // r530 (Stefan: "are there rain noises whenever it rains? WoW has ambient tracks we haven't
    // checked"): WoW ships weather loops (SoundEntries "Weather - RainLight/Medium/Heavy" 8533-8535,
    // "SnowLight/Medium/Heavy" 8536-8538, "SandstormLight/Medium/Heavy" 8556-8558). We played generated
    // noise. Use the real loop for the weather kind and strength; noise stays the fallback.
    const bool Snow=Kind==EWowWeatherKind::Snow||Kind==EWowWeatherKind::BlackSnow;
    const bool Sand=Kind==EWowWeatherKind::Dust;
    const float Strength=FMath::Clamp(Grade,0.f,1.f);
    const int32 Tier=Strength<0.34f?0:Strength<0.67f?1:2;
    const uint32 EntryId=Rain?8533u+Tier:Snow?8536u+Tier:Sand?8556u+Tier:0u;
    float FileVolume=0.f;
    if(EntryId)
    {
        static TMap<uint32,TWeakObjectPtr<USoundWave>> GWeatherLoops;
        USoundWave* Loop=GWeatherLoops.FindRef(EntryId).Get();
        if(!Loop)
        {
            const FString Path=GetSoundFilePath(EntryId,false);
            Loop=Path.IsEmpty()?nullptr:LoadSoundFromMpq(Path);
            if(Loop) { Loop->bLooping=true; GWeatherLoops.Add(EntryId,Loop); }
            UE_LOG(LogWowAudio,Log,TEXT("Weather loop %u: %s%s"),EntryId,*Path,Loop?TEXT(""):TEXT(" (not found, generated noise instead)"));
        }
        if(Loop) { Sound=Loop; FileVolume=FMath::Lerp(0.55f,1.f,Strength); }
    }
    if(!Sound)
    {
        TObjectPtr<USoundWave>& CachedWeatherSound=Rain?WeatherRainSound:WeatherWindSound;
        if(!CachedWeatherSound)
            CachedWeatherSound=GetOrCreateFallbackNoise(Rain?TEXT("Fallback:Weather:Rain"):TEXT("Fallback:Weather:Grains"),8.f,Rain?0.85f:0.6f,true,SOUNDGROUP_Effects);
        Sound=CachedWeatherSound;
    }

    if (!Sound)
    {
        return;
    }

    if (WeatherComponent->GetSound() != Sound || !WeatherComponent->IsPlaying())
    {
        WeatherComponent->Stop();
        WeatherComponent->SetSound(Sound);
        WeatherComponent->Play();
    }

    const float LowPassHz = FWowUnderwaterAudioPolicy::ResolveLowPassFrequency(UnderwaterMixBlend);
    GWeatherLoopLevel = FileVolume > 0.f ? FileVolume : CurrentWeatherGrade;
    WeatherComponent->SetVolumeMultiplier(AmbienceVolume * MasterVolume * GetAmbienceMixScale() * GWeatherLoopLevel);
    WeatherComponent->SetLowPassFilterEnabled(UnderwaterMixBlend > 0.02f);
    WeatherComponent->SetLowPassFilterFrequency(LowPassHz);
}

float AWowAudioManager::GetMusicMixScale() const
{
    // Only the Music toggle silences music (r277, Stefan): "Enable Sound" turned the music off too.
    return FWowClientPreferences::GetWorldOption(TEXT("musicenabled"))
        ? FWowUnderwaterAudioPolicy::ResolveMusicVolumeScale(UnderwaterMixBlend) * LoadingDuck : 0.0f;
}

float AWowAudioManager::GetAmbienceMixScale() const
{
    return FWowClientPreferences::GetWorldOption(TEXT("soundenabled")) && FWowClientPreferences::GetWorldOption(TEXT("ambientenabled"))
        ? FWowUnderwaterAudioPolicy::ResolveAmbienceVolumeScale(UnderwaterMixBlend) * LoadingDuck : 0.0f;
}

void AWowAudioManager::ApplyAmbientMix()
{
    const float MusicScale = GetMusicMixScale();
    const float AmbienceScale = GetAmbienceMixScale();
    const float LowPassHz = FWowUnderwaterAudioPolicy::ResolveLowPassFrequency(UnderwaterMixBlend);

    auto ApplyToMusicComponent = [this, MusicScale, LowPassHz](UAudioComponent* Component, bool bIsActive)
    {
        if (!Component)
        {
            return;
        }

        const float CrossfadeScale = bIsActive ? CrossfadeProgress : (1.0f - CrossfadeProgress);
        const float EffectiveFade = CrossfadeProgress < 1.0f ? CrossfadeScale : (bIsActive ? 1.0f : 0.0f);
        Component->SetVolumeMultiplier(EffectiveFade * MusicVolume * MasterVolume * MusicScale);
        Component->SetLowPassFilterEnabled(UnderwaterMixBlend > 0.02f);
        Component->SetLowPassFilterFrequency(LowPassHz);
    };

    ApplyToMusicComponent(MusicComponentA, bMusicAActive);
    ApplyToMusicComponent(MusicComponentB, !bMusicAActive);

    if (AmbienceComponent)
    {
        AmbienceComponent->SetVolumeMultiplier(AmbienceVolume * MasterVolume * AmbienceScale);
        AmbienceComponent->SetLowPassFilterEnabled(UnderwaterMixBlend > 0.02f);
        AmbienceComponent->SetLowPassFilterFrequency(LowPassHz);
    }

    if(WindComponent) {
        WindComponent->SetVolumeMultiplier(MasterVolume*AmbienceVolume*AmbienceScale*CurrentWindVolume);
        WindComponent->SetLowPassFilterEnabled(UnderwaterMixBlend>0.02f);
        WindComponent->SetLowPassFilterFrequency(LowPassHz);
    }
    if(ThunderComponent && ThunderComponent->IsPlaying()) {
        ThunderComponent->SetVolumeMultiplier(MasterVolume*AmbienceVolume*AmbienceScale*ThunderPlaybackStrength);
        const float ThunderHz=FMath::Min(LowPassHz,ThunderDistanceLowPassHz);
        ThunderComponent->SetLowPassFilterEnabled(ThunderHz<19000.f);
        ThunderComponent->SetLowPassFilterFrequency(ThunderHz);
    }
    if (WeatherComponent)
    {
        WeatherComponent->SetVolumeMultiplier(AmbienceVolume * MasterVolume * AmbienceScale * GWeatherLoopLevel);
        WeatherComponent->SetLowPassFilterEnabled(UnderwaterMixBlend > 0.02f);
        WeatherComponent->SetLowPassFilterFrequency(LowPassHz);
    }
    // World sound effects (spells, combat, footsteps, splashes) are muffled too; UI stays dry.
    for (const FPlayingEffect& Effect : PlayingEffects)
    {
        if (UAudioComponent* Component = Effect.bWorld ? Effect.Component.Get() : nullptr)
        {
            Component->SetLowPassFilterEnabled(UnderwaterMixBlend > 0.02f);
            Component->SetLowPassFilterFrequency(LowPassHz);
        }
    }
}

void AWowAudioManager::UpdateMusic()
{
    bLoginTrack=false;
    USoundWave* Sound = nullptr;
    FString SourceLabel = TEXT("generated zone music");
    if (Mpq && PendingZoneMusicId != 0)
    {
        if (const FZoneMusicDbcEntry* ZoneMusic = FDbcStore::Get().ZoneMusic().GetById(PendingZoneMusicId))
        {
            uint32 SoundId = bIsDaytime ? ZoneMusic->SoundDayID : ZoneMusic->SoundNightID;
            if (SoundId == 0) SoundId = ZoneMusic->SoundDayID;
            SourceLabel = GetSoundFilePath(SoundId);
            Sound = LoadSoundFromMpq(SourceLabel);
        }
    }
    if (!Sound)
    {
        Sound = GetOrCreateFallbackTone(TEXT("Fallback:Music:Zone"), 196.0f, 5.0f, 0.10f, true, SOUNDGROUP_Music);
    }
    StartMusicSound(Sound, SourceLabel);
}

void AWowAudioManager::UpdateAmbience()
{
    USoundWave* Sound = nullptr;
    FString FilePath;
    if (Mpq && PendingAmbienceId != 0)
    {
        if (const FSoundAmbienceDbcEntry* Ambience = FDbcStore::Get().SoundAmbience().GetById(PendingAmbienceId))
        {
            uint32 SoundId = bIsDaytime ? Ambience->DayAmbience : Ambience->NightAmbience;
            if (SoundId == 0) SoundId = Ambience->DayAmbience;
            FilePath = GetSoundFilePath(SoundId);
            Sound = LoadSoundFromMpq(FilePath);
        }
    }
    if (!Sound)
    {
        FilePath = TEXT("generated forest ambience");
        Sound = GetOrCreateFallbackNoise(TEXT("Fallback:Ambience:Forest"), 5.0f, 0.06f, true, SOUNDGROUP_Effects);
    }

    if (AmbienceComponent)
    {
        AmbienceComponent->Stop();
        AmbienceComponent->SetSound(Sound);
        AmbienceComponent->SetVolumeMultiplier(AmbienceVolume * MasterVolume * GetAmbienceMixScale());
        AmbienceComponent->SetLowPassFilterEnabled(UnderwaterMixBlend > 0.02f);
        AmbienceComponent->SetLowPassFilterFrequency(
            FWowUnderwaterAudioPolicy::ResolveLowPassFrequency(UnderwaterMixBlend));
        AmbienceComponent->Play();

        UE_LOG(LogWowAudio, Log, TEXT("Playing ambience: %s (AmbienceId %d, %s)"),
            *FilePath, PendingAmbienceId, bIsDaytime ? TEXT("day") : TEXT("night"));
    }
}

FString AWowAudioManager::GetSoundFilePath(uint32 SoundEntryId, bool bAvoidRepeat)
{
    const FSoundEntriesDbcEntry* SoundEntry = FDbcStore::Get().SoundEntries().GetById(SoundEntryId);
    if (!SoundEntry) return FString();

    // Pick a random file from the entry's file list
    TArray<FString> ValidFiles;
    for (int32 i = 0; i < 10; i++)
    {
        if (!SoundEntry->FileDataID[i].IsEmpty())
        {
            ValidFiles.AddUnique(SoundEntry->FileDataID[i]);
        }
    }

    if (ValidFiles.Num() == 0) return FString();

    // Character voices should not repeat the last take when alternatives exist.
    if (bAvoidRepeat && ValidFiles.Num() > 1)
        if (const FString* Previous = LastVoiceFiles.Find(SoundEntryId))
            ValidFiles.Remove(*Previous);
    FString FileName = ValidFiles[FMath::RandRange(0, ValidFiles.Num() - 1)];
    if (bAvoidRepeat) LastVoiceFiles.Add(SoundEntryId, FileName);
    FString DirBase = SoundEntry->DirectoryBase;

    // Build full path
    if (DirBase.IsEmpty())
    {
        return FString::Printf(TEXT("Sound\\%s"), *FileName);
    }
    return FString::Printf(TEXT("%s\\%s"), *DirBase, *FileName);
}

void AWowAudioManager::PlaySoundEffect(uint32 SoundEntryId)
{
    if (!Mpq || bUiSoundsSuppressed) return;

    FString FilePath = GetSoundFilePath(SoundEntryId);
    if (FilePath.IsEmpty()) return;

    USoundWave* Sound = LoadSoundFromMpq(FilePath);
    if (!Sound) return;

    Sound->SoundGroup = SOUNDGROUP_Effects;
    PlayEffect(Sound);
}

void AWowAudioManager::PlayUiSound(const FName& SoundName)
{
    if (bUiSoundsSuppressed)
    {
        return;
    }

    const FString Name = SoundName.ToString();
    if (Name.Contains(TEXT("BackPackOpen"), ESearchCase::IgnoreCase) ||
        Name.Contains(TEXT("BagOpen"), ESearchCase::IgnoreCase) ||
        Name.Contains(TEXT("InventoryOpen"), ESearchCase::IgnoreCase) ||
        Name.Contains(TEXT("ContainerOpen"), ESearchCase::IgnoreCase))
    {
        PlayContainerOpenSound();
        return;
    }
    if (Name.Contains(TEXT("BackPackClose"), ESearchCase::IgnoreCase) ||
        Name.Contains(TEXT("BagClose"), ESearchCase::IgnoreCase) ||
        Name.Contains(TEXT("InventoryClose"), ESearchCase::IgnoreCase) ||
        Name.Contains(TEXT("ContainerClose"), ESearchCase::IgnoreCase))
    {
        PlayContainerCloseSound();
        return;
    }

    USoundWave* Sound = FindSoundByName(Name);
    if (!Sound)
    {
        const float Frequency = Name.Contains(TEXT("Error"), ESearchCase::IgnoreCase) ? 165.0f : 520.0f;
        Sound = GetOrCreateFallbackTone(TEXT("Fallback:UI:") + Name, Frequency, 0.10f, 0.20f, false, SOUNDGROUP_UI);
    }
    if (Sound)
    {
        Sound->SoundGroup = SOUNDGROUP_UI;
        PlayEffect(Sound);
    }
}

bool AWowAudioManager::PlayFirstAvailableUiSound(const TArray<FString>& FilePaths,
    const FString& CueLabel, float VolumeScale)
{
    if (bUiSoundsSuppressed)
    {
        return false;
    }

    for (FString FilePath : FilePaths)
    {
        FilePath.ReplaceInline(TEXT("/"), TEXT("\\"));
        if (USoundWave* Sound = LoadSoundFromMpq(FilePath))
        {
            Sound->SoundGroup = SOUNDGROUP_UI;
            PlayEffect(Sound, FMath::Max(0.0f, VolumeScale));
            UE_LOG(LogWowAudio, Log, TEXT("Playing data-backed UI cue '%s': %s"),
                *CueLabel, *FilePath);
            return true;
        }
    }

    UE_LOG(LogWowAudio, Warning, TEXT("No data-backed file found for UI cue '%s' (%s)"),
        *CueLabel, *FString::Join(FilePaths, TEXT(", ")));
    return false;
}

bool AWowAudioManager::PlaySoundFile(const FString& FilePath, float VolumeScale)
{
    if (bUiSoundsSuppressed)
    {
        return false;
    }

    FString NormalizedPath = FilePath;
    NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));
    if (USoundWave* Sound = LoadSoundFromMpq(NormalizedPath))
    {
        Sound->SoundGroup = SOUNDGROUP_Effects;
        PlayEffect(Sound, FMath::Max(0.0f, VolumeScale));
        return true;
    }
    return false;
}

UAudioComponent* AWowAudioManager::PlayNarration(uint32 SoundEntryId)
{
    if (!Mpq || !SoundEntryId) return nullptr;
    const FString Path = GetSoundFilePath(SoundEntryId, false);
    if (Path.IsEmpty()) { UE_LOG(LogWowAudio, Warning, TEXT("Narration sound %u has no file"), SoundEntryId); return nullptr; }
    USoundWave* Sound = LoadSoundFromMpq(Path);
    if (!Sound) { UE_LOG(LogWowAudio, Warning, TEXT("Narration %s did not load"), *Path); return nullptr; }
    Sound->SoundGroup = SOUNDGROUP_Voice;
    UE_LOG(LogWowAudio, Log, TEXT("Narration %u: %s (%.1f s)"), SoundEntryId, *Path, Sound->Duration);
    return UGameplayStatics::SpawnSound2D(this, Sound, MasterVolume * FMath::Max(SfxVolume, 0.6f));
}

bool AWowAudioManager::PlaySoundFileById(uint32 FileDataId, float VolumeScale)
{
    // FileDataID 567419 is iEnterWorldA. Retail exposes it numerically while
    // the 3.3.5 MPQ stores the same cue by filename (normally as WAV).
    if (FileDataId == 567419)
    {
        return PlayFirstAvailableUiSound({
            TEXT("Sound\\Interface\\iEnterWorldA.ogg"),
            TEXT("Sound\\Interface\\iEnterWorldA.wav"),
            TEXT("Sound\\Interface\\iEnterWorldA.mp3")
        }, TEXT("EnterWorld/FileDataID 567419"), VolumeScale);
    }

    UE_LOG(LogWowAudio, Warning, TEXT("Unknown FileDataID requested: %u"), FileDataId);
    return false;
}

bool AWowAudioManager::PlayEnterWorldSound()
{
    bGlueScreen = false;
    return PlaySoundFileById(567419);
}

bool AWowAudioManager::PlayContainerOpenSound()
{
    const double NowSeconds = FPlatformTime::Seconds();
    if (NowSeconds - LastContainerOpenSoundSeconds < 0.15)
    {
        return true;
    }
    const bool bPlayed = PlayFirstAvailableUiSound({
        TEXT("Sound\\Interface\\iEquipmentContainerOpenA.wav"),
        TEXT("Sound\\Interface\\iBagOpenA.wav")
    }, TEXT("ContainerOpen"));
    if (bPlayed)
    {
        LastContainerOpenSoundSeconds = NowSeconds;
    }
    return bPlayed;
}

bool AWowAudioManager::PlayContainerCloseSound()
{
    const double NowSeconds = FPlatformTime::Seconds();
    if (NowSeconds - LastContainerCloseSoundSeconds < 0.15)
    {
        return true;
    }
    const bool bPlayed = PlayFirstAvailableUiSound({
        TEXT("Sound\\Interface\\iEquipmentContainerCloseA.wav"),
        TEXT("Sound\\Interface\\iBagCloseA.wav")
    }, TEXT("ContainerClose"));
    if (bPlayed)
    {
        LastContainerCloseSoundSeconds = NowSeconds;
    }
    return bPlayed;
}

void AWowAudioManager::PlayLoginMusic()
{
    bLoginTrack=true;
    // 3.3.5 stores the expansion title themes as loose MPQ files. Prefer the
    // Wrath main title explicitly: relying only on the DBC display name can
    // miss it on locale/archive variants even though the audio is present.
    USoundWave* Sound = nullptr;
    FString SourceLabel;
    for (const FString& Candidate : {
        FString(TEXT("Sound\\Music\\GlueScreenMusic\\WotLK_main_title.mp3")),
        FString(TEXT("Sound\\Music\\GlueScreenMusic\\WotLK_main_title.ogg")),
        FString(TEXT("Sound\\Music\\GlueScreenMusic\\BC_main_theme.mp3")),
        FString(TEXT("Sound\\Music\\GlueScreenMusic\\wow_main_theme.mp3")) })
    {
        Sound = LoadSoundFromMpq(Candidate);
        if (Sound)
        {
            SourceLabel = Candidate;
            break;
        }
    }
    if (!Sound) Sound = FindSoundByName(TEXT("GlueScreenIntro"));
    if (!Sound) Sound = FindSoundByName(TEXT("GlueScreenMusic"));
    if (!Sound) Sound = GetOrCreateFallbackTone(TEXT("Fallback:Music:Login"), 146.8f, 6.0f, 0.10f, true, SOUNDGROUP_Music);
    StartMusicSound(Sound, SourceLabel.IsEmpty() ? TEXT("login music") : SourceLabel);
}

USoundWave* AWowAudioManager::FindSoundByName(const FString& SoundName)
{
    for (const FSoundEntriesDbcEntry& Entry : FDbcStore::Get().SoundEntries().GetAll())
    {
        if (Entry.Name.Equals(SoundName, ESearchCase::IgnoreCase) || Entry.Name.Contains(SoundName, ESearchCase::IgnoreCase))
        {
            return LoadSoundFromMpq(GetSoundFilePath(Entry.ID));
        }
    }
    return nullptr;
}

void AWowAudioManager::StartMusicSound(USoundWave* Sound, const FString& SourceLabel)
{
    if (!Sound) return;
    bMusicScheduled=true; bMusicWaiting=false; MusicElapsed=0;
    MusicLength=FMath::Max(1.0f,Sound->Duration);
    Sound->SoundGroup = SOUNDGROUP_Music;
    Sound->bLooping = true;
    bMusicAActive = !bMusicAActive;
    UAudioComponent* NewActive = bMusicAActive ? MusicComponentA : MusicComponentB;
    if (!NewActive) return;
    NewActive->SetSound(Sound);
    NewActive->SetVolumeMultiplier(0.0f);
    NewActive->SetLowPassFilterEnabled(UnderwaterMixBlend > 0.02f);
    NewActive->SetLowPassFilterFrequency(
        FWowUnderwaterAudioPolicy::ResolveLowPassFrequency(UnderwaterMixBlend));
    NewActive->Play();
    CrossfadeProgress = 0.0f;
    UE_LOG(LogWowAudio, Log, TEXT("Playing music: %s"), *SourceLabel);
}

USoundWave* AWowAudioManager::LoadSoundFromMpq(const FString& FilePath)
{
    if (!Mpq) return nullptr;

    if (const FWowDecodedPcmAudio* Found = DecodedSoundCache.Find(FilePath))
    {
        return CreateRuntimeSound(*Found, SOUNDGROUP_Effects);
    }

    // Read from MPQ
    TArray<uint8> RawData;
    if (!Mpq->ReadFile(FilePath, RawData))
    {
        return nullptr;
    }

    return LoadSoundFromData(FilePath, MoveTemp(RawData));
}

USoundWave* AWowAudioManager::LoadSoundFromData(const FString& FilePath, TArray<uint8> RawData)
{
    if (const FWowDecodedPcmAudio* Found = DecodedSoundCache.Find(FilePath))
    {
        return CreateRuntimeSound(*Found, SOUNDGROUP_Effects);
    }
    if (RawData.Num() == 0) return nullptr;

    const bool bIsWav = FilePath.EndsWith(TEXT(".wav"), ESearchCase::IgnoreCase);
    if (!bIsWav)
    {
        TArray<uint8> ConvertedWaveData;
        if (!WowDecodeAudioToWave(RawData, ConvertedWaveData) || ConvertedWaveData.Num() < 44)
        {
            UE_LOG(LogWowAudio, Warning, TEXT("Failed to decode compressed MPQ sound: %s"), *FilePath);
            return nullptr;
        }
        RawData = MoveTemp(ConvertedWaveData);
    }

    // Parse RIFF chunks rather than relying on the canonical 44-byte layout.
    // SoundFileIO may emit optional chunks when converting compressed MPQ audio.
    if (RawData.Num() < 12 || FMemory::Memcmp(RawData.GetData(), "RIFF", 4) != 0 ||
        FMemory::Memcmp(RawData.GetData() + 8, "WAVE", 4) != 0)
    {
        UE_LOG(LogWowAudio, Warning, TEXT("Invalid WAV container for MPQ sound: %s"), *FilePath);
        return nullptr;
    }

    const uint8* D = RawData.GetData();
    uint16 AudioFormat = 0;
    uint16 NumChannels = 0;
    uint32 SampleRate = 0;
    uint16 BitsPerSample = 0;
    int32 DataOffset = INDEX_NONE;
    int32 DataSize = 0;

    int32 ChunkOffset = 12;
    while (ChunkOffset + 8 <= RawData.Num())
    {
        uint32 ChunkSize = 0;
        FMemory::Memcpy(&ChunkSize, D + ChunkOffset + 4, sizeof(ChunkSize));
        const int64 ChunkDataOffset = static_cast<int64>(ChunkOffset) + 8;
        const int64 ChunkEnd = ChunkDataOffset + ChunkSize;
        if (ChunkEnd > RawData.Num())
        {
            UE_LOG(LogWowAudio, Warning, TEXT("Truncated WAV chunk in MPQ sound: %s"), *FilePath);
            return nullptr;
        }

        if (FMemory::Memcmp(D + ChunkOffset, "fmt ", 4) == 0 && ChunkSize >= 16)
        {
            FMemory::Memcpy(&AudioFormat, D + ChunkDataOffset, sizeof(AudioFormat));
            FMemory::Memcpy(&NumChannels, D + ChunkDataOffset + 2, sizeof(NumChannels));
            FMemory::Memcpy(&SampleRate, D + ChunkDataOffset + 4, sizeof(SampleRate));
            FMemory::Memcpy(&BitsPerSample, D + ChunkDataOffset + 14, sizeof(BitsPerSample));
        }
        else if (FMemory::Memcmp(D + ChunkOffset, "data", 4) == 0)
        {
            DataOffset = static_cast<int32>(ChunkDataOffset);
            DataSize = static_cast<int32>(ChunkSize);
        }

        ChunkOffset = static_cast<int32>(ChunkEnd + (ChunkSize & 1u));
    }

    if (AudioFormat != 1 || BitsPerSample != 16 || NumChannels == 0 || SampleRate == 0 ||
        DataOffset == INDEX_NONE || DataSize < static_cast<int32>(sizeof(int16)))
    {
        UE_LOG(LogWowAudio, Warning,
            TEXT("Unsupported decoded PCM for %s (format=%u bits=%u channels=%u rate=%u data=%d)"),
            *FilePath, AudioFormat, BitsPerSample, NumChannels, SampleRate, DataSize);
        return nullptr;
    }

    DataSize -= DataSize % static_cast<int32>(sizeof(int16));
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> PcmData =
        MakeShared<TArray<uint8>, ESPMode::ThreadSafe>();
    PcmData->Append(D + DataOffset, DataSize);

    // A plain USoundWave created at runtime is treated as a cooked streaming
    // asset by the mixer and is rejected. The procedural wave supplies PCM
    // directly to the render thread and therefore works in editor and builds.
    FWowDecodedPcmAudio Decoded;
    Decoded.PcmData = MoveTemp(PcmData);
    Decoded.SampleRate = static_cast<int32>(SampleRate);
    Decoded.NumChannels = static_cast<int32>(NumChannels);
    DecodedSoundCache.Add(FilePath, Decoded);

    USoundWave* SoundWave = CreateRuntimeSound(Decoded, SOUNDGROUP_Effects);
    if (!SoundWave)
    {
        return nullptr;
    }
    UE_LOG(LogWowAudio, Log, TEXT("Loaded runtime sound: %s (%d PCM bytes, %u Hz, %u channels, %.2fs)"),
        *FilePath, DataSize, SampleRate, NumChannels, SoundWave->Duration);

    return SoundWave;
}

USoundWave* AWowAudioManager::CreateRuntimeSound(const FWowDecodedPcmAudio& Decoded, ESoundGroup SoundGroup)
{
    if (!Decoded.PcmData.IsValid() || Decoded.PcmData->IsEmpty() ||
        Decoded.SampleRate <= 0 || Decoded.NumChannels <= 0)
    {
        return nullptr;
    }

    UWowRuntimeSoundWave* Sound = NewObject<UWowRuntimeSoundWave>(this);
    Sound->InitializePcm16(Decoded.PcmData, Decoded.SampleRate, Decoded.NumChannels, SoundGroup);
    return Sound;
}

USoundWave* AWowAudioManager::GetOrCreateFallbackTone(const FString& CacheKey, float FrequencyHz, float DurationSeconds, float VolumeScale, bool bLooping, ESoundGroup SoundGroup)
{
    if (const FWowDecodedPcmAudio* Found = DecodedSoundCache.Find(CacheKey))
    {
        USoundWave* Sound = CreateRuntimeSound(*Found, SoundGroup);
        if (Sound) Sound->bLooping = bLooping;
        return Sound;
    }
    constexpr int32 SampleRate = 22050;
    const int32 NumSamples = FMath::Max(1, FMath::RoundToInt(DurationSeconds * SampleRate));
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> PcmData =
        MakeShared<TArray<uint8>, ESPMode::ThreadSafe>();
    PcmData->SetNumUninitialized(NumSamples * sizeof(int16));
    int16* Samples = reinterpret_cast<int16*>(PcmData->GetData());
    for (int32 Index = 0; Index < NumSamples; ++Index)
    {
        const float Envelope = bLooping ? 1.0f : FMath::Clamp(1.0f - static_cast<float>(Index) / NumSamples, 0.0f, 1.0f);
        Samples[Index] = static_cast<int16>(FMath::Sin(2.0f * PI * FrequencyHz * Index / SampleRate) * 32767.0f * VolumeScale * Envelope);
    }
    FWowDecodedPcmAudio Decoded;
    Decoded.PcmData = MoveTemp(PcmData);
    Decoded.SampleRate = SampleRate;
    Decoded.NumChannels = 1;
    DecodedSoundCache.Add(CacheKey, Decoded);
    USoundWave* Sound = CreateRuntimeSound(Decoded, SoundGroup);
    if (!Sound) return nullptr;
    Sound->bLooping = bLooping;
    return Sound;
}

USoundWave* AWowAudioManager::GetOrCreateFallbackNoise(const FString& CacheKey, float DurationSeconds, float VolumeScale, bool bLooping, ESoundGroup SoundGroup)
{
    if (const FWowDecodedPcmAudio* Found = DecodedSoundCache.Find(CacheKey))
    {
        USoundWave* Sound = CreateRuntimeSound(*Found, SoundGroup);
        if (Sound) Sound->bLooping = bLooping;
        return Sound;
    }
    constexpr int32 SampleRate = 22050;
    const int32 NumSamples = FMath::Max(1, FMath::RoundToInt(DurationSeconds * SampleRate));
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> PcmData =
        MakeShared<TArray<uint8>, ESPMode::ThreadSafe>();
    PcmData->SetNumUninitialized(NumSamples * sizeof(int16));
    int16* Samples = reinterpret_cast<int16*>(PcmData->GetData());
    float Filtered = 0.0f;
    for (int32 Index = 0; Index < NumSamples; ++Index)
    {
        const bool Thunder=CacheKey.EndsWith(TEXT(":Weather:Thunder"));
        const bool Wind=CacheKey.EndsWith(TEXT(":Weather:Wind"));
        const float WhiteNoise=FMath::FRandRange(-1.0f,1.0f);
        Filtered = FMath::Lerp(Filtered, WhiteNoise, Thunder?0.026f:Wind?0.012f:0.08f);
        const float Gust=Wind?0.65f+0.22f*FMath::Sin(2*PI*Index/NumSamples)+0.13f*FMath::Sin(6*PI*Index/NumSamples):0.9f+0.1f*FMath::Sin(4*PI*Index/NumSamples);
        const float T=float(Index)/SampleRate;
        const float Envelope=Thunder?FMath::Min(T/0.003f,1.f)*FMath::Exp(-T*0.65f)*FMath::Clamp((DurationSeconds-T)/0.65f,0.f,1.f):1.f;
        const float Rumble=Thunder?Filtered*3.f+FMath::Sin(T*2*PI*42.f)*0.16f+WhiteNoise*.95f*FMath::Exp(-T*18.f):Filtered*(Wind?3.5f:2.0f);
        Samples[Index] = static_cast<int16>(FMath::Clamp(Rumble*VolumeScale*Gust*Envelope,-0.98f,0.98f)*32767.f);
    }
    FWowDecodedPcmAudio Decoded;
    Decoded.PcmData = MoveTemp(PcmData);
    Decoded.SampleRate = SampleRate;
    Decoded.NumChannels = 1;
    DecodedSoundCache.Add(CacheKey, Decoded);
    USoundWave* Sound = CreateRuntimeSound(Decoded, SoundGroup);
    if (!Sound) return nullptr;
    Sound->bLooping = bLooping;
    return Sound;
}

float AWowAudioManager::GetEffectMixScale() const
{
    return FWowClientPreferences::GetWorldOption(TEXT("soundenabled")) && FWowClientPreferences::GetWorldOption(TEXT("soundeffects")) ? 1.0f : 0.0f;
}
void AWowAudioManager::PlayEffect(USoundWave* Sound, float Scale)
{
    if (!Sound || GetEffectMixScale()<=0) return;
    if (auto* Component=UGameplayStatics::SpawnSound2D(this,Sound,MasterVolume*SfxVolume*Scale*(Sound->SoundGroup==SOUNDGROUP_UI ? 1.0f : LoadingDuck)))
    {
        FPlayingEffect Effect; Effect.Component=Component; Effect.Scale=Scale;
        Effect.Remaining=FMath::Max(.1f,Sound->Duration)+.1f;
        PlayingEffects.Add(Effect);
    }
}

UAudioComponent* AWowAudioManager::PlayWorldSoundEffect(uint32 Id,const FVector& Location, bool bAvoidRepeat)
{
    if(!Mpq || bUiSoundsSuppressed || GetEffectMixScale()<=0 || PlayingEffects.Num()>=32) return nullptr;
    const double Now=FPlatformTime::Seconds();
    if(const double* Last=RecentWorldSounds.Find(Id);Last && Now-*Last<.08) return nullptr;
    if(RecentWorldSounds.Num()>512) RecentWorldSounds.Reset();
    RecentWorldSounds.Add(Id,Now);
    const FString Path=GetSoundFilePath(Id,bAvoidRepeat); if(Path.IsEmpty()) return nullptr;
    auto* Sound=LoadSoundFromMpq(Path); if(!Sound) return nullptr;
    Sound->SoundGroup=SOUNDGROUP_Effects;
    auto* Attenuation=NewObject<USoundAttenuation>(this);
    Attenuation->Attenuation.bAttenuate=true;
    Attenuation->Attenuation.bSpatialize=true;
    Attenuation->Attenuation.AttenuationShapeExtents=FVector(150.f);
    Attenuation->Attenuation.FalloffDistance=4500.f;
    const auto* Entry=FDbcStore::Get().SoundEntries().GetById(Id);
    const float Scale=Entry?FMath::Clamp(Entry->VolumeFloat,0.f,1.f):1.f;
    if(auto* Component=UGameplayStatics::SpawnSoundAtLocation(this,Sound,Location,FRotator::ZeroRotator,
        MasterVolume*SfxVolume*Scale*LoadingDuck,1.f,0.f,Attenuation))
    {
        Component->SetLowPassFilterEnabled(UnderwaterMixBlend > 0.02f);
        Component->SetLowPassFilterFrequency(FWowUnderwaterAudioPolicy::ResolveLowPassFrequency(UnderwaterMixBlend));
        FPlayingEffect Effect;Effect.Component=Component;Effect.Scale=Scale;Effect.bWorld=true;
        Effect.Remaining=FMath::Max(.1f,Sound->Duration)+.1f;PlayingEffects.Add(Effect);
        return Component;
    }
    return nullptr;
}
uint32 AWowAudioManager::ResolveCharacterCue(uint8 Race,uint8 Gender,const FString& Cue)
{
    static const TCHAR* Races[]={TEXT(""),TEXT("Human"),TEXT("Orc"),TEXT("Dwarf"),TEXT("NightElf"),TEXT("Undead"),TEXT("Tauren"),TEXT("Gnome"),TEXT("Troll"),TEXT(""),TEXT("BloodElf"),TEXT("Draenei")};
    if(Race>=UE_ARRAY_COUNT(Races) || !*Races[Race] || Gender>1) return 0;
    const FString Prefix=FString(Races[Race])+(Gender?TEXT("Female"):TEXT("Male"));
    TArray<FString> Names={Prefix+Cue};
    if(Cue.StartsWith(TEXT("Player")))
    {
        const FString Action=Cue.Mid(6);
        Names.Append({Prefix+TEXT("Main")+Action,Prefix+TEXT("Final")+Action,Prefix+Action});
        if(Race==5) Names.Add(FString(TEXT("Scourge"))+(Gender?TEXT("Female"):TEXT("Male"))+Action);
        if(Race==6 && Gender==1)
            Names.Add(Prefix+(Action==TEXT("Attack")?TEXT("CombatExertion"):Action==TEXT("WoundCrit")?TEXT("CombatInjuryCritica"):TEXT("CombatInjury")));
    }
    else if(Race==5) Names.Add(FString(TEXT("Scourge"))+(Gender?TEXT("Female"):TEXT("Male"))+Cue);
    for(const FString& Name:Names)
        for(const auto& Entry:FDbcStore::Get().SoundEntries().GetAll())
            if(Entry.Name.Equals(Name,ESearchCase::IgnoreCase)) return Entry.ID;
    return 0;
}
void AWowAudioManager::PlayCharacterCue(uint8 Race,uint8 Gender,const FString& Cue,const FVector& Location,bool bError)
{
    if(bError && !bErrorSpeechEnabled) return;
    const double Now=FPlatformTime::Seconds();
    double& Last=bError?LastErrorCue:LastCharacterCue;
    if (bError)
    {
        if (Now-Last < 2.5) return;
    }
    else
    {
        // A shared voice budget prevents attack and injury grunts talking over
        // each other. Impacts and spell sounds retain their own timing.
        if (!ShouldPlayCombatVoice(Now-Last, Cue==TEXT("PlayerWoundCrit"), FMath::FRand())) return;
    }
    const uint32 Id=ResolveCharacterCue(Race,Gender,Cue);if(!Id)return;
    Last=Now;
    if(bError)PlaySoundEffect(Id);else PlayWorldSoundEffect(Id,Location,true);
}

UAudioComponent* AWowAudioManager::PlayCreatureSound(uint32 DisplayId, EWowCreatureSound Kind, const FVector& Location)
{
    FWowUnitSounds::EnsureLoaded(Mpq);
    const uint32 SoundId = FWowUnitSounds::GetCreatureSound(DisplayId, Kind);
    return SoundId ? PlayWorldSoundEffect(SoundId, Location, true) : nullptr;
}

UAudioComponent* AWowAudioManager::PlayNpcSound(uint32 DisplayId, EWowNpcSound Kind, const FVector& Location)
{
    FWowUnitSounds::EnsureLoaded(Mpq);
    const uint32 SoundId = FWowUnitSounds::GetNpcSound(DisplayId, Kind);
    return SoundId ? PlayWorldSoundEffect(SoundId, Location, true) : nullptr;
}

void AWowAudioManager::PlayWeaponHit(int32 WeaponSubclass, int32 ImpactType, bool bCrit, bool bSwing, const FVector& Location)
{
    FWowUnitSounds::EnsureLoaded(Mpq);
    if (bSwing)
        if (const uint32 Swing = FWowUnitSounds::GetWeaponSwing(FWowUnitSounds::GetSwingType(WeaponSubclass), bCrit))
            PlayWorldSoundEffect(Swing, Location, true);
    if (const uint32 Impact = FWowUnitSounds::GetWeaponImpact(WeaponSubclass, ImpactType, bCrit))
        PlayWorldSoundEffect(Impact, Location, true);
}

void AWowAudioManager::EnterGlueScreen()
{
    bGlueScreen = true;
    StopMusic();
    StopAmbience();
    for (FPlayingEffect& Effect : PlayingEffects)
        if (Effect.Component.IsValid()) Effect.Component->Stop();
    PlayingEffects.Reset();
    SetUnderwaterMixEnabled(false);
    CurrentZoneId = CurrentAreaId = PendingZoneMusicId = PendingAmbienceId = 0;
    if (AWowWorldManager* World = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass())))
        World->ResetAudioArea();
    PlayLoginMusic();
}
