#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "WowRuntimeSoundWave.generated.h"

/**
 * Runtime PCM sound used for audio decoded from WoW MPQ archives.
 * Unlike a regular USoundWave, this never asks Unreal for cooked compressed data.
 */
UCLASS()
class WOWWORLD_API UWowRuntimeSoundWave : public USoundWaveProcedural
{
    GENERATED_BODY()

public:
    /** Uses immutable shared interleaved signed 16-bit PCM data. */
    void InitializePcm16(
        const TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>& InPcmData,
        int32 InSampleRate,
        int32 InNumChannels,
        ESoundGroup InSoundGroup);

    virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

    int32 GetPcmByteCount() const { return PcmData.IsValid() ? PcmData->Num() : 0; }

private:
    /** Immutable after playback starts; shared with the manager's decoded-data cache. */
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> PcmData;
    int32 PlaybackByteOffset = 0;
};
