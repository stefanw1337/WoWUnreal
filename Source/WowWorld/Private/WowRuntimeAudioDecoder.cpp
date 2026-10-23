#include "WowRuntimeAudioDecoder.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "SoundFileIO/SoundFileIO.h"

#if PLATFORM_WINDOWS && !WITH_EDITOR
namespace
{
// libsndfile's stable C ABI, loaded from the runtime library staged with this game.
using FCount = int64;
struct FInfo { FCount Frames; int Rate, Channels, Format, Sections, Seekable; };
struct FVirtualIO
{
    FCount (*Length)(void*);
    FCount (*Seek)(FCount, int, void*);
    FCount (*Read)(void*, FCount, void*);
    FCount (*Write)(const void*, FCount, void*);
    FCount (*Tell)(void*);
};
struct FInput { const TArray<uint8>& Bytes; FCount Position = 0; };
FCount Length(void* P) { return static_cast<FInput*>(P)->Bytes.Num(); }
FCount Tell(void* P) { return static_cast<FInput*>(P)->Position; }
FCount Seek(FCount Offset, int Origin, void* P)
{
    auto& In = *static_cast<FInput*>(P);
    const FCount Base = Origin == 0 ? 0 : Origin == 1 ? In.Position : Origin == 2 ? In.Bytes.Num() : -1;
    if (Base < 0 || Offset < -Base || Offset > In.Bytes.Num()-Base) return -1;
    return In.Position = Base + Offset;
}
FCount Read(void* Out, FCount Count, void* P)
{
    auto& In = *static_cast<FInput*>(P);
    Count = FMath::Clamp<FCount>(Count, 0, In.Bytes.Num()-In.Position);
    FMemory::Memcpy(Out, In.Bytes.GetData()+In.Position, Count);
    In.Position += Count;
    return Count;
}
FCount Write(const void*, FCount, void*) { return 0; }
struct FDecoder
{
    void* Handle = nullptr;
    void* (*Open)(FVirtualIO*, int, FInfo*, void*) = nullptr;
    FCount (*ReadFrames)(void*, float*, FCount) = nullptr;
    int (*Close)(void*) = nullptr;
    FDecoder()
    {
        const FString Path = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/libsndfile/Win64/libsndfile-1.dll"));
        Handle = FPlatformProcess::GetDllHandle(*Path);
        if (Handle)
        {
            Open = reinterpret_cast<decltype(Open)>(FPlatformProcess::GetDllExport(Handle, TEXT("sf_open_virtual")));
            ReadFrames = reinterpret_cast<decltype(ReadFrames)>(FPlatformProcess::GetDllExport(Handle, TEXT("sf_readf_float")));
            Close = reinterpret_cast<decltype(Close)>(FPlatformProcess::GetDllExport(Handle, TEXT("sf_close")));
        }
        if (!Open || !ReadFrames || !Close) UE_LOG(LogTemp, Error, TEXT("Runtime audio decoder missing: %s"), *Path);
    }
    ~FDecoder() { if (Handle) FPlatformProcess::FreeDllHandle(Handle); }
};
}
#endif

// r476 (Stefan: the Undead intro had no narrator): UndeadNarration.mp3 has an ID3v2.3 tag followed by
// 417 zero bytes before its first MPEG frame. libsndfile does not search past such padding and
// rejected the file. When an MP3's audio does not start right after its tag, the tag and the
// padding are cut off, so the decoder sees a stream that starts with a frame.
static bool IsMpegFrameHeader(const uint8* H)
{
    return H[0] == 0xFF && (H[1] & 0xE0) == 0xE0 && ((H[1] >> 3) & 3) != 1 && ((H[1] >> 1) & 3) != 0
        && (H[2] >> 4) != 0 && (H[2] >> 4) != 15 && ((H[2] >> 2) & 3) != 3;
}
static const TArray<uint8>& SkipMp3Padding(const TArray<uint8>& Input, TArray<uint8>& Trimmed)
{
    if (Input.Num() < 14 || FMemory::Memcmp(Input.GetData(), "ID3", 3) != 0) return Input;
    const int32 TagEnd = 10 + ((Input[6] & 0x7F) << 21 | (Input[7] & 0x7F) << 14 | (Input[8] & 0x7F) << 7 | (Input[9] & 0x7F))
        + ((Input[5] & 0x10) ? 10 : 0);
    if (TagEnd + 4 > Input.Num() || IsMpegFrameHeader(Input.GetData() + TagEnd)) return Input;
    for (int32 I = TagEnd; I + 4 <= Input.Num() && I < TagEnd + 65536; ++I)
        if (IsMpegFrameHeader(Input.GetData() + I))
        {
            Trimmed.Append(Input.GetData() + I, Input.Num() - I);
            UE_LOG(LogTemp, Log, TEXT("Runtime audio: skipped %d bytes of MP3 tag padding"), I - TagEnd);
            return Trimmed;
        }
    return Input;
}

bool WowDecodeAudioToWave(const TArray<uint8>& RawInput, TArray<uint8>& Output)
{
    TArray<uint8> Trimmed;
    const TArray<uint8>& Input = SkipMp3Padding(RawInput, Trimmed);
#if PLATFORM_WINDOWS && !WITH_EDITOR
    Output.Reset();
    static FDecoder Decoder;
    if (Input.IsEmpty() || !Decoder.Open || !Decoder.ReadFrames || !Decoder.Close) return false;
    FInput Source{Input};
    FVirtualIO IO{Length, Seek, Read, Write, Tell};
    FInfo Info{};
    void* File = Decoder.Open(&IO, 0x10 /* SFM_READ */, &Info, &Source);
    if (!File) return false;
    ON_SCOPE_EXIT { Decoder.Close(File); };
    if (Info.Frames <= 0 || Info.Rate <= 0 || Info.Rate > 384000 || Info.Channels <= 0 || Info.Channels > 32 ||
        Info.Frames > (512ll*1024*1024)/(Info.Channels*int64(sizeof(float)))) return false;
    TArray<float> Samples;
    Samples.SetNumUninitialized(static_cast<int32>(Info.Frames*Info.Channels));
    FCount FramesRead = 0;
    while (FramesRead < Info.Frames)
    {
        const FCount Count = Decoder.ReadFrames(File, Samples.GetData()+FramesRead*Info.Channels, Info.Frames-FramesRead);
        if (Count <= 0) break;
        FramesRead += Count;
    }
    if (FramesRead == 0) return false;
    const uint32 DataSize = static_cast<uint32>(FramesRead*Info.Channels*sizeof(int16));
    Output.SetNumZeroed(44+DataSize);
    auto Put16 = [&](int32 Offset, uint16 Value) { FMemory::Memcpy(Output.GetData()+Offset, &Value, 2); };
    auto Put32 = [&](int32 Offset, uint32 Value) { FMemory::Memcpy(Output.GetData()+Offset, &Value, 4); };
    FMemory::Memcpy(Output.GetData(), "RIFF", 4); Put32(4, 36+DataSize);
    FMemory::Memcpy(Output.GetData()+8, "WAVEfmt ", 8); Put32(16, 16);
    Put16(20, 1); Put16(22, Info.Channels); Put32(24, Info.Rate);
    Put32(28, Info.Rate*Info.Channels*2); Put16(32, Info.Channels*2); Put16(34, 16);
    FMemory::Memcpy(Output.GetData()+36, "data", 4); Put32(40, DataSize);
    // MP3 synthesis can exceed full scale. libsndfile's short conversion wraps
    // those peaks by default; reducing the game volume afterwards cannot repair it.
    float Peak = 1.0f;
    const int32 SampleCount = static_cast<int32>(FramesRead * Info.Channels);
    for (int32 I = 0; I < SampleCount; ++I)
        if (FMath::IsFinite(Samples[I])) Peak = FMath::Max(Peak, FMath::Abs(Samples[I]));
    const float Gain = Peak > 1.0f ? 0.98f / Peak : 1.0f;
    for (int32 I = 0; I < SampleCount; ++I)
    {
        const float Value = FMath::IsFinite(Samples[I]) ? Samples[I] * Gain : 0.0f;
        const int16 Pcm = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(Value * 32767.0f), -32768, 32767));
        FMemory::Memcpy(Output.GetData() + 44 + I * sizeof(int16), &Pcm, sizeof(int16));
    }
    if (Peak > 1.0f)
        UE_LOG(LogTemp, Log, TEXT("Runtime audio: preserved decoded peaks %.3f with %.3f track gain"), Peak, Gain);
    return true;
#else
    return Audio::SoundFileUtils::ConvertAudioToWav(Input, Output);
#endif
}
