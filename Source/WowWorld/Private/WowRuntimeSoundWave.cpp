#include "WowRuntimeSoundWave.h"

void UWowRuntimeSoundWave::InitializePcm16(
    const TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>& InPcmData,
    int32 InSampleRate,
    int32 InNumChannels,
    ESoundGroup InSoundGroup)
{
    PcmData = InPcmData;
    PlaybackByteOffset = 0;

    SetSampleRate(FMath::Max(1, InSampleRate));
    NumChannels = FMath::Max(1, InNumChannels);
    SoundGroup = InSoundGroup;
    SampleByteSize = sizeof(int16);
    Duration = static_cast<float>(GetPcmByteCount()) /
        static_cast<float>(GetSampleRateForCurrentPlatform() * NumChannels * sizeof(int16));
}

int32 UWowRuntimeSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
    OutAudio.Reset();
    if (!PcmData.IsValid() || PcmData->IsEmpty() || NumSamples <= 0)
    {
        return 0;
    }

    const int32 RequestedBytes = NumSamples * static_cast<int32>(sizeof(int16));
    OutAudio.Reserve(RequestedBytes);

    int32 RemainingBytes = RequestedBytes;
    while (RemainingBytes > 0)
    {
        if (PlaybackByteOffset >= PcmData->Num())
        {
            if (!bLooping)
            {
                break;
            }
            PlaybackByteOffset = 0;
        }

        const int32 CopyBytes = FMath::Min(RemainingBytes, PcmData->Num() - PlaybackByteOffset);
        OutAudio.Append(PcmData->GetData() + PlaybackByteOffset, CopyBytes);
        PlaybackByteOffset += CopyBytes;
        RemainingBytes -= CopyBytes;
    }

    return OutAudio.Num() / static_cast<int32>(sizeof(int16));
}
