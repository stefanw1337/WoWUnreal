#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundGroups.h"
#include "WowUnitSounds.h"
#include "WowAudioManager.generated.h"

class USoundWave;
class UAudioComponent;
class FMpqManager;

struct FWowDecodedPcmAudio
{
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> PcmData;
    int32 SampleRate = 0;
    int32 NumChannels = 0;
};

/** Shared, deterministic policy for when and how the local underwater mix is applied. */
struct FWowUnderwaterAudioPolicy
{
    // r407 (Stefan: under water the sound was only quieter, no muffle): the per-sound low-pass
    // is a gentle one-pole filter, so 800 Hz still let most of the voice and splash detail through.
    // 300 Hz leaves the dull, bassy underwater sound.
    static constexpr float LowPassFrequencyHz = 300.0f;
    /** Seconds to fade the muffle in/out when the camera crosses the surface. */
    static constexpr float BlendSeconds = 0.2f;
    static constexpr float MusicVolumeScale = 0.50f;
    static constexpr float AmbienceVolumeScale = 0.28f;

    /** The listener is the camera: only a camera below the surface sounds submerged
        (a third-person camera above a diving avatar keeps the normal mix). */
    static FORCEINLINE bool ShouldEnable(bool bCameraSubmerged, bool /*bHeadSubmerged*/)
    {
        return bCameraSubmerged;
    }

    static FORCEINLINE float ResolveLowPassFrequency(float Blend)
    {
        // Exponential sweep: perceptually even, so the 0.2 s fade does not jump at the end.
        const float T = FMath::Clamp(Blend, 0.0f, 1.0f);
        return T <= 0.0f ? 20000.0f : T >= 1.0f ? LowPassFrequencyHz
            : 20000.0f * FMath::Pow(LowPassFrequencyHz / 20000.0f, T);
    }

    static FORCEINLINE float ResolveMusicVolumeScale(float Blend)
    {
        return FMath::Lerp(1.0f, MusicVolumeScale, FMath::Clamp(Blend, 0.0f, 1.0f));
    }

    static FORCEINLINE float ResolveAmbienceVolumeScale(float Blend)
    {
        return FMath::Lerp(1.0f, AmbienceVolumeScale, FMath::Clamp(Blend, 0.0f, 1.0f));
    }
};

/**
 * Manages WoW audio: zone music, ambient sounds, and sound effects.
 * Loads MP3/WAV from MPQ archives into USoundWave, handles crossfading.
 */
UCLASS()
class WOWWORLD_API AWowAudioManager : public AActor
{
    GENERATED_BODY()
public:
    AWowAudioManager();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    /** Set the MPQ manager for loading audio files */
    void SetMpqManager(FMpqManager* InMpq) { Mpq = InMpq; }

    /** Update the current zone (triggers music/ambience change) */
    void SetCurrentZone(uint32 ZoneId, uint32 AreaId);

    /** Play a sound effect by SoundEntries.dbc ID (one-shot) */
    void PlaySoundEffect(uint32 SoundEntryId);
    /** Returns the playing component (nullptr if not played) so callers can stop it, e.g. an interrupted cast. */
    UAudioComponent* PlayWorldSoundEffect(uint32 SoundEntryId, const FVector& Location, bool bAvoidRepeat = false);
    void PlayCharacterCue(uint8 Race,uint8 Gender,const FString& Cue,const FVector& Location,bool bError=false);
    /** Original-client unit sounds (CreatureSoundData / NPCSounds / weapon swing+impact). Return the sound or null. */
    UAudioComponent* PlayCreatureSound(uint32 DisplayId, EWowCreatureSound Kind, const FVector& Location);
    UAudioComponent* PlayNpcSound(uint32 DisplayId, EWowNpcSound Kind, const FVector& Location);
    void PlayWeaponHit(int32 WeaponSubclass, int32 ImpactType, bool bCrit, bool bSwing, const FVector& Location);
    static uint32 ResolveCharacterCue(uint8 Race,uint8 Gender,const FString& Cue);
    TMap<uint32,double> RecentWorldSounds;
    double LastCharacterCue=-100, LastErrorCue=-100;
    static bool ShouldPlayCombatVoice(double Elapsed, bool bCritical, float Roll)
    {
        return Elapsed >= 3.0 && Roll < (bCritical ? 0.55f : 0.30f);
    }

    /** Play a named UI sound from SoundEntries.dbc when available, otherwise a small local fallback tone. */
    void PlayUiSound(const FName& SoundName);

    /** Play a one-shot sound from an MPQ-relative path when available. */
    bool PlaySoundFile(const FString& FilePath, float VolumeScale = 1.0f);

    /** r377: cinematic narration by SoundEntries id (2D voice). The caller keeps the component
        to stop it when the cinematic is skipped. */
    UAudioComponent* PlayNarration(uint32 SoundEntryId);

    /** Resolve a known Blizzard FileDataID to its runtime data path and play it. */
    bool PlaySoundFileById(uint32 FileDataId, float VolumeScale = 1.0f);

    /** Exact data-backed interface cues used by native client-flow bridges. */
    bool PlayEnterWorldSound();
    bool PlayContainerOpenSound();
    bool PlayContainerCloseSound();

    /** Silence incidental FrameXML OnLoad/OnHide cues during UI construction. */
    void SetUiSoundsSuppressed(bool bSuppressed) { bUiSoundsSuppressed = bSuppressed; }

    /** Start menu/login music using MPQ data when available, otherwise a local fallback loop. */
    void PlayLoginMusic();
    /** Back to character select (logout): stop zone music/ambience/weather/effects, forget the
        zone so re-entering the same area restarts it, and play the glue music. */
    void EnterGlueScreen();

    /** Stop currently playing music. */
    void StopMusic();
    /** Drop decoded sounds (after switching the game-data folder). */
    void ClearSoundCache() { DecodedSoundCache.Reset(); LastVoiceFiles.Reset(); }

    /** Stop currently playing ambience. */
    void StopAmbience();

    /** Apply or clear the local underwater mix immediately. */
    void SetUnderwaterMixEnabled(bool bEnabled);

    /** Apply weather ambience such as rain or storm loops. */
    void SetWeatherAudio(uint32 WeatherType, float Grade, float WindSpeed = 0.f);
    void PlayWeatherThunder(float Strength);
    bool IsUnderwaterMixEnabled() const { return bUnderwaterMixEnabled; }

    void SetMasterVolume(float NewVolume);
    void SetMusicVolume(float NewVolume);
    void SetSfxVolume(float NewVolume);
    void SetAmbienceVolume(float NewVolume);
    void SetErrorSpeechEnabled(bool bEnabled);
    void SetDeathKnightReverbEnabled(bool bEnabled);

    /** r297 (Stefan): mute world audio (music, ambience, weather, sound effects - not UI sounds)
     *  while a loading screen or a map change is in progress, and fade it back in afterwards.
     *  Each caller owns one bit, so the login loading screen and in-game transfers don't fight. */
    enum : uint32 { LoadingLogin = 1u, LoadingTransfer = 2u, LoadingMovie = 4u /* r380: a movie plays in the world */ };
    static void SetWorldLoading(uint32 SourceBit, bool bLoading);

    /** Master volume (0-1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float MasterVolume = 1.0f;

    /** Music volume (0-1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float MusicVolume = 0.5f;

    /** Sound effects volume (0-1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float SfxVolume = 0.75f;

    /** Ambient volume (0-1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float AmbienceVolume = 0.5f;

    /** Enables spoken/character error barks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bErrorSpeechEnabled = true;

    /** Enables the Death Knight voice/reverb effect when that class is active. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bDeathKnightReverbEnabled = false;

    /** Is it currently daytime? (affects music/ambience variant) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bIsDaytime = true;

    /** Crossfade duration in seconds */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float CrossfadeDuration = 3.0f;

private:
    FMpqManager* Mpq = nullptr;

    /** Load an audio file from MPQ into a USoundWave */
    USoundWave* LoadSoundFromMpq(const FString& FilePath);
    /** Decode WAV/MP3/OGG bytes (cached by key) into a runtime sound. */
    USoundWave* LoadSoundFromData(const FString& CacheKey, TArray<uint8> RawData);
    /** Personal thunder pool: <dist>/UserSounds/Thunder/*.wav|ogg|mp3 ("close"/"distant" in the name). */
    void ScanUserThunderSounds();
    TArray<FString> UserThunderFiles;
    bool bUserThunderScanned = false;
    int32 LastUserThunderIndex = INDEX_NONE;
    float ThunderDistanceLowPassHz = 20000.0f;

    /** Create a fresh playback cursor over cached PCM data. */
    USoundWave* CreateRuntimeSound(const FWowDecodedPcmAudio& Decoded, ESoundGroup SoundGroup);

    /** Find a SoundEntries.dbc row by name and load one of its files. */
    USoundWave* FindSoundByName(const FString& SoundName);

    /** Play the first decodable path without substituting a generated cue. */
    bool PlayFirstAvailableUiSound(const TArray<FString>& FilePaths, const FString& CueLabel,
        float VolumeScale = 1.0f);

    /** Create small generated fallback sounds for offline/no-data mode. */
    USoundWave* GetOrCreateFallbackTone(const FString& CacheKey, float FrequencyHz, float DurationSeconds, float VolumeScale, bool bLooping, ESoundGroup SoundGroup);
    USoundWave* GetOrCreateFallbackNoise(const FString& CacheKey, float DurationSeconds, float VolumeScale, bool bLooping, ESoundGroup SoundGroup);

    /** Switch active music component and begin the normal crossfade. */
    void StartMusicSound(USoundWave* Sound, const FString& SourceLabel);

    /** Get a random sound file path from a SoundEntries.dbc entry */
    FString GetSoundFilePath(uint32 SoundEntryId, bool bAvoidRepeat = false);
    TMap<uint32, FString> LastVoiceFiles;

    /** Start playing music for the current zone */
    void UpdateMusic();

    /** Start playing ambient sounds for the current zone */
    void UpdateAmbience();

    /** Reapply master/category volumes after crossfades or underwater state changes. */
    void ApplyAmbientMix();
    float GetMusicMixScale() const;
    float GetAmbienceMixScale() const;

    /** Current zone/area */
    uint32 CurrentZoneId = 0;
    uint32 CurrentAreaId = 0;
    uint32 PendingZoneMusicId = 0;
    uint32 PendingAmbienceId = 0;

    /** Music playback */
    UPROPERTY()
    TObjectPtr<UAudioComponent> MusicComponentA;

    UPROPERTY()
    TObjectPtr<UAudioComponent> MusicComponentB;

    struct FPlayingEffect
    {
        TWeakObjectPtr<UAudioComponent> Component;
        float Scale=1, Remaining=0;
        bool bWorld=false;
    };
    TArray<FPlayingEffect> PlayingEffects;
    void PlayEffect(USoundWave* Sound, float Scale=1.0f);
    float GetEffectMixScale() const;
    bool bMusicScheduled=false, bMusicWaiting=false, bLoginTrack=false;
    bool bGlueScreen=false; // zone updates ignored until the world is entered again
    float MusicElapsed=0, MusicLength=0, MusicSilenceRemaining=0;
    bool bMusicAActive = true;
    float CrossfadeProgress = 1.0f; // 1.0 = fully faded, no fade in progress

    /** Ambient playback */
    UPROPERTY()
    TObjectPtr<UAudioComponent> AmbienceComponent;

    /** Weather playback */
    UPROPERTY()
    TObjectPtr<UAudioComponent> WeatherComponent;

    /** Underwater audio muffling state. */
    bool bUnderwaterMixEnabled = false;
    float UnderwaterMixBlend = 0.0f;
    /** r297: 1 = normal, 0 = muted for loading (fades). */
    float LoadingDuck = 1.0f;
    bool IsUiSound(const UAudioComponent* Component) const;
    float UnderwaterMixTarget = 0.0f;
    UPROPERTY() TObjectPtr<USoundWave> WeatherRainSound;
    UPROPERTY() TObjectPtr<USoundWave> WeatherWindSound;
    UPROPERTY() TObjectPtr<USoundWave> WeatherGustSound;
    UPROPERTY() TObjectPtr<UAudioComponent> WindComponent;
    float CurrentWindVolume = 0;
    UPROPERTY() TObjectPtr<USoundWave> WeatherThunderSound;
    UPROPERTY() TObjectPtr<UAudioComponent> ThunderComponent;
    float ThunderPlaybackStrength = 0;
    uint32 CurrentWeatherType = 0;
    float CurrentWeatherGrade = 0.0f;

    bool bUiSoundsSuppressed = false;
    double LastContainerOpenSoundSeconds = -100.0;
    double LastContainerCloseSoundSeconds = -100.0;

    /** Decoded PCM is cached, but every playback gets an independent procedural cursor. */
    TMap<FString, FWowDecodedPcmAudio> DecodedSoundCache;
};
