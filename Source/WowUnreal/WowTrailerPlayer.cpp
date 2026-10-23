#include "WowTrailerPlayer.h"
#include "WowLocale.h"
#include "WowClientPreferences.h"
#include "WowAudioManager.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "MediaSoundComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTrailer, Log, All);

namespace
{
struct FSubtitle { double Start = 0, End = 0; FString Text; };

/** Full-screen black layer that skips on Esc/Space/Enter or a click. */
class SWowTrailerOverlay : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowTrailerOverlay) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { ChildSlot[Args._Content.Widget]; }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Key) override
    {
        if (Key.GetKey() == EKeys::Escape || Key.GetKey() == EKeys::SpaceBar || Key.GetKey() == EKeys::Enter)
            FWowTrailerPlayer::Skip();
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override
    { FWowTrailerPlayer::Skip(); return FReply::Handled(); }
};

/** A resolved movie waiting to play (or playing). */
struct FMovieRequest
{
    FString Name;      // movie id, for the log
    FString PlayPath;  // local file the media player opens
    TArray<FSubtitle> Subtitles;
    FWowMovieOptions Options;
    TFunction<void(bool)> OnDone;
};

struct FTrailerState
{
    FMovieRequest Request;
    TStrongObjectPtr<UMediaPlayer> Player;
    TStrongObjectPtr<UMediaTexture> Texture;
    TWeakObjectPtr<AActor> SoundActor;
    TSharedPtr<SWidget> Overlay;
    TSharedPtr<STextBlock> SubtitleText;
    FSlateBrush Brush;
    FTSTicker::FDelegateHandle Ticker;
    double StartedAt = 0;
    double NotPlayingSince = 0;
    bool bSawPlayback = false;
};
TSharedPtr<FTrailerState> GTrailer;
TArray<FMovieRequest> GQueue;

const TCHAR* const MovieExtensions[] = { TEXT(".mp4"), TEXT(".avi") };

FString SeenKey(const FString& DataPath, const FString& Movie)
{
    return FString::Printf(TEXT("Seen_%08x_%s"), GetTypeHash(DataPath.ToLower()), *FPaths::GetBaseFilename(Movie));
}

UWorld* ViewportWorld() { return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr; }

AWowAudioManager* FindAudio()
{
    UWorld* World = ViewportWorld();
    return World ? Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(World, AWowAudioManager::StaticClass())) : nullptr;
}

FString DecodeText(const TArray<uint8>& Raw)
{
    // UTF-8 (with or without BOM); UTF-16 files carry a BOM and go through the engine helper.
    if (Raw.Num() >= 2 && ((Raw[0] == 0xFF && Raw[1] == 0xFE) || (Raw[0] == 0xFE && Raw[1] == 0xFF)))
    {
        FString Text;
        FFileHelper::BufferToString(Text, Raw.GetData(), Raw.Num());
        return Text;
    }
    const int32 Skip = Raw.Num() >= 3 && Raw[0] == 0xEF && Raw[1] == 0xBB && Raw[2] == 0xBF ? 3 : 0;
    FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Raw.GetData()) + Skip, Raw.Num() - Skip);
    return FString(Conv.Length(), Conv.Get());
}

/** Loose movie folders of a server's data: locale folder first. */
TArray<FString> LooseFolders(const FString& DataPath)
{
    if (DataPath.IsEmpty()) return {};
    return { DataPath / WowLocale::Get() / TEXT("Interface") / TEXT("Cinematics"), DataPath / TEXT("Interface") / TEXT("Cinematics") };
}

FString FindLoose(const FString& DataPath, const FString& Base, const TArray<FString>& Extensions)
{
    for (const FString& Folder : LooseFolders(DataPath))
        for (const FString& Ext : Extensions)
            if (FPaths::FileExists(Folder / Base + Ext)) return FPaths::ConvertRelativePathToFull(Folder / Base + Ext);
    return FString();
}

FString MpqPath(const FString& Base, const TCHAR* Ext) { return FString(TEXT("Interface\\Cinematics\\")) + Base + Ext; }

/** Loose file first, then the MPQs. */
bool ReadMovieSideFile(FMpqManager* Mpq, const FString& Base, const TCHAR* Ext, TArray<uint8>& Out)
{
    const FString Loose = FindLoose(Mpq ? Mpq->GetDataPath() : FString(), Base, {Ext});
    if (!Loose.IsEmpty() && FFileHelper::LoadFileToArray(Out, *Loose)) return true;
    return Mpq && Mpq->ReadFile(MpqPath(Base, Ext), Out);
}

// --- subtitles ---------------------------------------------------------------------------

bool ParseSbtTime(const FString& In, double& Out)
{
    TArray<FString> Parts;
    In.TrimStartAndEnd().ParseIntoArray(Parts, TEXT(":"));
    if (Parts.Num() != 4) return false;
    Out = FCString::Atoi(*Parts[0]) * 3600.0 + FCString::Atoi(*Parts[1]) * 60.0 + FCString::Atoi(*Parts[2]) + FCString::Atoi(*Parts[3]) / 100.0;
    return true;
}

/** "HH:MM:SS,mmm" (also '.' before the milliseconds). */
bool ParseSrtTime(const FString& In, double& Out)
{
    FString Clean = In.TrimStartAndEnd().Replace(TEXT(","), TEXT("."));
    TArray<FString> Parts;
    Clean.ParseIntoArray(Parts, TEXT(":"));
    if (Parts.Num() != 3) return false;
    Out = FCString::Atoi(*Parts[0]) * 3600.0 + FCString::Atoi(*Parts[1]) * 60.0 + FCString::Atod(*Parts[2]);
    return true;
}

/** Drops <i>-style tags and {\an8}-style override codes; subtitles are shown as plain text. */
FString StripSubtitleTags(const FString& In)
{
    FString Out;
    int32 Depth = 0;
    for (TCHAR C : In)
    {
        if (C == '<' || C == '{') { ++Depth; continue; }
        if ((C == '>' || C == '}') && Depth > 0) { --Depth; continue; }
        if (Depth == 0) Out.AppendChar(C);
    }
    return Out.TrimStartAndEnd();
}

TArray<FSubtitle> ParseSrt(const FString& Text)
{
    TArray<FSubtitle> Result;
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines, false);
    for (int32 I = 0; I < Lines.Num(); ++I)
    {
        FString A, B;
        if (!Lines[I].Split(TEXT("-->"), &A, &B)) continue;
        FSubtitle Sub;
        FString EndTime = B.TrimStartAndEnd();
        EndTime.Split(TEXT(" "), &EndTime, nullptr); // position hints after the time
        if (!ParseSrtTime(A, Sub.Start) || !ParseSrtTime(EndTime.IsEmpty() ? B : EndTime, Sub.End)) continue;
        for (++I; I < Lines.Num() && !Lines[I].TrimStartAndEnd().IsEmpty(); ++I)
        {
            const FString Line = StripSubtitleTags(Lines[I]);
            if (!Line.IsEmpty()) Sub.Text += (Sub.Text.IsEmpty() ? TEXT("") : TEXT("\n")) + Line;
        }
        if (!Sub.Text.IsEmpty() && Sub.End > Sub.Start) Result.Add(Sub);
    }
    return Result;
}

TArray<FSubtitle> ParseSbt(const FString& Text)
{
    TArray<FSubtitle> Result;
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines, false);
    for (int32 I = 0; I < Lines.Num(); ++I)
    {
        FString A, B;
        if (!Lines[I].Split(TEXT(" - "), &A, &B)) continue;
        FSubtitle Sub;
        if (!ParseSbtTime(A, Sub.Start) || !ParseSbtTime(B, Sub.End)) continue;
        for (++I; I < Lines.Num() && !Lines[I].TrimStartAndEnd().IsEmpty(); ++I)
            Sub.Text += (Sub.Text.IsEmpty() ? TEXT("") : TEXT("\n")) + Lines[I].TrimStartAndEnd();
        Result.Add(Sub);
    }
    return Result;
}

TArray<FSubtitle> LoadSubtitles(FMpqManager* Mpq, const FString& MovieBase)
{
    TArray<uint8> Raw;
    if (ReadMovieSideFile(Mpq, MovieBase, TEXT(".srt"), Raw))
    {
        TArray<FSubtitle> Subs = ParseSrt(DecodeText(Raw));
        UE_LOG(LogWowTrailer, Log, TEXT("Subtitles %s.srt: %d lines"), *MovieBase, Subs.Num());
        if (Subs.Num()) return Subs;
    }
    Raw.Reset();
    if (ReadMovieSideFile(Mpq, MovieBase, TEXT(".sbt"), Raw)) return ParseSbt(DecodeText(Raw));
    return {};
}

// --- files ---------------------------------------------------------------------------------

/** Blizzard's AVIs are DivX-family video tagged "DXGM", which no system decoder claims. Changing
    the tag to XVID (the video bytes are untouched) lets Windows try. Returns the tags changed. */
int32 PatchFourCC(TArray<uint8>& Data)
{
    int32 Patched = 0;
    const int32 Scan = FMath::Min(Data.Num() - 4, 64 * 1024); // headers sit at the start
    for (int32 I = 0; I < Scan; ++I)
    {
        uint8* P = Data.GetData() + I;
        if ((P[0] == 'D' || P[0] == 'd') && (P[1] == 'X' || P[1] == 'x') && (P[2] == 'G' || P[2] == 'g') && (P[3] == 'M' || P[3] == 'm'))
        {
            const bool bUpper = P[0] == 'D';
            P[0] = bUpper ? 'X' : 'x'; P[1] = bUpper ? 'V' : 'v'; P[2] = bUpper ? 'I' : 'i'; P[3] = bUpper ? 'D' : 'd';
            ++Patched;
        }
    }
    return Patched;
}

/** Loose AVI: a re-tagged copy in Saved\Cinematics; the player's WoW folder is never written. */
FString PrepareSystemCopy(const FString& Source)
{
    if (!Source.EndsWith(TEXT(".avi"), ESearchCase::IgnoreCase)) return Source;
    const FString Target = FPaths::ProjectSavedDir() / TEXT("Cinematics") / FPaths::GetCleanFilename(Source);
    const int64 SourceSize = IFileManager::Get().FileSize(*Source);
    if (IFileManager::Get().FileSize(*Target) == SourceSize) return Target;
    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *Source)) return Source;
    const int32 Patched = PatchFourCC(Data);
    if (!Patched) return Source;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
    if (!FFileHelper::SaveArrayToFile(Data, *Target)) return Source;
    UE_LOG(LogWowTrailer, Log, TEXT("Prepared %s (%d codec tags DXGM -> XVID)"), *Target, Patched);
    return Target;
}

/** Changes whenever an MPQ of the data folder is replaced or patched (name, size, time). */
FString ArchiveStamp(const FString& DataPath)
{
    uint32 Hash = 0;
    for (const FString& Folder : { DataPath, DataPath / WowLocale::Get() })
    {
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.mpq")), true, false);
        Files.Sort();
        for (const FString& File : Files)
        {
            const FString Full = Folder / File;
            Hash = HashCombine(Hash, GetTypeHash(File.ToLower()));
            Hash = HashCombine(Hash, GetTypeHash(IFileManager::Get().FileSize(*Full)));
            Hash = HashCombine(Hash, GetTypeHash(IFileManager::Get().GetTimeStamp(*Full).GetTicks()));
        }
    }
    return FString::Printf(TEXT("%08x"), Hash);
}

/** A movie inside the MPQs, copied to Saved\Cinematics\Mpq_<data folder hash>\ (AVIs re-tagged). */
FString ExtractFromMpq(FMpqManager* Mpq, const FString& Inner, const FString& FileName)
{
    const FString DataPath = Mpq->GetDataPath();
    const FString Target = FPaths::ProjectSavedDir() / TEXT("Cinematics") / FString::Printf(TEXT("Mpq_%08x"), GetTypeHash(DataPath.ToLower())) / FileName;
    const FString StampFile = Target + TEXT(".stamp");
    const FString Stamp = ArchiveStamp(DataPath);
    FString OldStamp;
    if (FPaths::FileExists(Target) && FFileHelper::LoadFileToString(OldStamp, *StampFile) && OldStamp == Stamp) return Target;
    const double Start = FPlatformTime::Seconds();
    TArray<uint8> Data;
    if (!Mpq->ReadFile(Inner, Data) || Data.Num() == 0)
    {
        UE_LOG(LogWowTrailer, Warning, TEXT("Movie %s could not be read from the MPQs"), *Inner);
        return FString();
    }
    const int32 Patched = FileName.EndsWith(TEXT(".avi"), ESearchCase::IgnoreCase) ? PatchFourCC(Data) : 0;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
    if (!FFileHelper::SaveArrayToFile(Data, *Target))
    {
        UE_LOG(LogWowTrailer, Warning, TEXT("Movie %s could not be written to %s"), *Inner, *Target);
        return FString();
    }
    FFileHelper::SaveStringToFile(Stamp, *StampFile);
    UE_LOG(LogWowTrailer, Log, TEXT("Extracted %s from the MPQs to %s (%.1f MB, %d codec tags DXGM -> XVID, %.2f s)"),
        *Inner, *Target, Data.Num() / 1048576.0, Patched, FPlatformTime::Seconds() - Start);
    return Target;
}

/** Local file the media player can open, or empty. */
FString ResolveMovie(FMpqManager* Mpq, const FString& MovieId)
{
    if (!Mpq) return FString();
    const FString Loose = FindLoose(Mpq->GetDataPath(), MovieId, {MovieExtensions[0], MovieExtensions[1]});
    if (!Loose.IsEmpty()) return PrepareSystemCopy(Loose);
    for (const TCHAR* Ext : MovieExtensions)
        if (Mpq->FileExists(MpqPath(MovieId, Ext))) return ExtractFromMpq(Mpq, MpqPath(MovieId, Ext), MovieId + Ext);
    return FString();
}

// --- playback ------------------------------------------------------------------------------

void StartRequest(FMovieRequest&& Request);

void StartNextQueued()
{
    if (GTrailer || GQueue.Num() == 0) return;
    FMovieRequest Next = MoveTemp(GQueue[0]);
    GQueue.RemoveAt(0);
    StartRequest(MoveTemp(Next));
}

void Finish(bool bPlayed)
{
    TSharedPtr<FTrailerState> State = GTrailer;
    GTrailer.Reset();
    if (!State) return;
    FTSTicker::GetCoreTicker().RemoveTicker(State->Ticker);
    if (State->Player) State->Player->Close();
    if (AActor* Actor = State->SoundActor.Get()) Actor->Destroy();
    if (State->Overlay && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(State->Overlay.ToSharedRef());
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    const bool bMore = GQueue.Num() > 0;
    if (State->Request.Options.bInWorld)
    {
        if (!bMore) AWowAudioManager::SetWorldLoading(AWowAudioManager::LoadingMovie, false);
    }
    else if (!bMore)
    {
        if (AWowAudioManager* Audio = FindAudio()) Audio->PlayLoginMusic();
    }
    UE_LOG(LogWowTrailer, Log, TEXT("Movie %s (%s) %s after %.1f s"), *State->Request.Name, *State->Request.Options.Reason,
        bPlayed ? TEXT("ended") : TEXT("failed"), FPlatformTime::Seconds() - State->StartedAt);
    if (State->Request.OnDone) State->Request.OnDone(bPlayed);
    // The next one starts on the next tick, outside this callback chain.
    if (bMore) FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { StartNextQueued(); return false; }));
}

void StartRequest(FMovieRequest&& Request)
{
    TSharedPtr<FTrailerState> State = MakeShared<FTrailerState>();
    State->Request = MoveTemp(Request);
    State->Player.Reset(NewObject<UMediaPlayer>(GetTransientPackage(), NAME_None, RF_Transient));
    State->Texture.Reset(NewObject<UMediaTexture>(GetTransientPackage(), NAME_None, RF_Transient));
    State->StartedAt = FPlatformTime::Seconds();
    GTrailer = State;
    if (!State->Player || !State->Texture || !GEngine || !GEngine->GameViewport)
    {
        UE_LOG(LogWowTrailer, Warning, TEXT("Movie %s: no media player"), *State->Request.Name);
        Finish(false);
        return;
    }
    State->Player->SetLooping(false);
    State->Player->PlayOnOpen = true;
    State->Texture->AutoClear = true;
    State->Texture->ClearColor = FLinearColor::Black;
    State->Texture->SetMediaPlayer(State->Player.Get());
    State->Texture->UpdateResource();

    // Movie sound goes through a media sound component on a small actor; it is not world audio,
    // so it is not ducked with it.
    if (UWorld* World = ViewportWorld())
    {
        if (AActor* Actor = World->SpawnActor<AActor>())
        {
            UMediaSoundComponent* Sound = NewObject<UMediaSoundComponent>(Actor);
            Sound->SetMediaPlayer(State->Player.Get());
            Sound->bIsUISound = true;
            Actor->SetRootComponent(Sound);
            Sound->RegisterComponent();
            State->SoundActor = Actor;
        }
    }
    if (State->Request.Options.bInWorld) AWowAudioManager::SetWorldLoading(AWowAudioManager::LoadingMovie, true);
    else if (AWowAudioManager* Audio = FindAudio()) Audio->StopMusic();

    State->Brush.SetResourceObject(State->Texture.Get());
    State->Brush.ImageSize = FVector2D(1024, 576);
    State->Brush.DrawAs = ESlateBrushDrawType::Image;
    TSharedRef<SWowTrailerOverlay> Overlay = SNew(SWowTrailerOverlay)
    [
        SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black).Padding(0)
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                [ SNew(SImage).Image(&State->Brush) ]
            ]
            + SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(40, 0, 40, 60))
            [
                SAssignNew(State->SubtitleText, STextBlock)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
                .ColorAndOpacity(FLinearColor(1.f, 0.82f, 0.f))
                .ShadowOffset(FVector2D(2, 2))
                .Justification(ETextJustify::Center)
                .AutoWrapText(true)
            ]
        ]
    ];
    State->Overlay = Overlay;
    GEngine->GameViewport->AddViewportWidgetContent(Overlay, 10000);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocus(Overlay, EFocusCause::SetDirectly);

    if (!State->Player->OpenFile(State->Request.PlayPath))
    {
        UE_LOG(LogWowTrailer, Warning, TEXT("Could not open movie %s"), *State->Request.PlayPath);
        Finish(false);
        return;
    }
    UE_LOG(LogWowTrailer, Log, TEXT("Playing movie %s (%s): %s, %d subtitles"), *State->Request.Name, *State->Request.Options.Reason,
        *State->Request.PlayPath, State->Request.Subtitles.Num());
    State->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
    {
        TSharedPtr<FTrailerState> S = GTrailer;
        if (!S || !S->Player) { Finish(false); return false; }
        const double Now = FPlatformTime::Seconds();
        const double Elapsed = Now - S->StartedAt;
        const bool bPlaying = S->Player->IsPlaying();
        S->bSawPlayback |= bPlaying;
        const double Time = S->Player->GetTime().GetTotalSeconds();
        FString Line;
        // r223 (Claude-UI): Options > Sound > Cinematic Subtitles, read live so it can change while a movie plays.
        if (FWowClientPreferences::GetWorldOption(TEXT("moviesubtitles")))
            for (const FSubtitle& Sub : S->Request.Subtitles) if (Time >= Sub.Start && Time <= Sub.End) { Line = Sub.Text; break; }
        if (S->SubtitleText) S->SubtitleText->SetText(FText::FromString(Line));
        // Ended: stopped for half a second after it had played (not a buffering gap). Or the
        // system cannot decode it: nothing after 6 s.
        if (bPlaying) S->NotPlayingSince = 0;
        else if (S->NotPlayingSince == 0) S->NotPlayingSince = Now;
        const bool bEnded = S->bSawPlayback && S->NotPlayingSince > 0 && Now - S->NotPlayingSince > 0.5;
        if (!S->bSawPlayback && Elapsed > 6.0)
        {
            UE_LOG(LogWowTrailer, Error, TEXT("Movie %s did not start within 6 s: no decoder for %s (codec?). Skipped."), *S->Request.Name, *S->Request.PlayPath);
            Finish(false);
            return false;
        }
        if (bEnded) { Finish(true); return false; }
        return true;
    }), 0.05f);
}

/** MovieRules.txt of the current data folder, read once per data folder. */
const TMap<FString, FString>& ServerRules(FMpqManager* Mpq)
{
    static FString CachedFor;
    static TMap<FString, FString> Rules;
    const FString DataPath = Mpq ? Mpq->GetDataPath() : FString();
    if (CachedFor == DataPath && !DataPath.IsEmpty()) return Rules;
    CachedFor = DataPath;
    Rules.Reset();
    TArray<uint8> Raw;
    if (!ReadMovieSideFile(Mpq, TEXT("MovieRules"), TEXT(".txt"), Raw)) return Rules;
    TArray<FString> Lines;
    DecodeText(Raw).ParseIntoArrayLines(Lines);
    for (FString Line : Lines)
    {
        Line.TrimStartAndEndInline();
        FString Key, Value;
        if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || !Line.Split(TEXT("="), &Key, &Value)) continue;
        Rules.Add(Key.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
    }
    UE_LOG(LogWowTrailer, Log, TEXT("Movie rules of %s: %d entries"), *DataPath, Rules.Num());
    return Rules;
}

/** Letters and digits only: "Death Knight" -> "DeathKnight". */
FString MovieToken(const FString& In)
{
    FString Out;
    for (TCHAR C : In) if ((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9')) Out.AppendChar(C);
    return Out;
}

bool LoadDbc(FMpqManager* Mpq, const TCHAR* Path, FDbcParser& Out)
{
    TArray<uint8> Raw;
    return Mpq && Mpq->ReadFile(Path, Raw) && Out.Parse(Raw) && Out.IsValid();
}

/** The class as the server's data names it (custom classes rename stock ones: TormentWoW's class 1
    is "Barbarian"), then its file token ("WARRIOR" -> Warrior), then the stock name. */
TArray<FString> ClassNames(FMpqManager* Mpq, uint8 Class)
{
    TArray<FString> Names;
    FDbcParser Dbc;
    if (LoadDbc(Mpq, TEXT("DBFilesClient\\ChrClasses.dbc"), Dbc))
        for (uint32 R = 0; R < Dbc.GetRecordCount(); ++R)
        {
            if (Dbc.GetUInt(R, 0) != Class) continue;
            // 3.3.5: name 4..19 (enUS/enGB in 4), file token 55.
            for (uint32 F = 4; F < FMath::Min<uint32>(20, Dbc.GetFieldCount()); ++F)
                Names.AddUnique(MovieToken(Dbc.GetString(R, F)));
            if (Dbc.GetFieldCount() > 55)
            {
                const FString Token = MovieToken(Dbc.GetString(R, 55));
                if (!Token.IsEmpty()) Names.AddUnique(Token.Left(1).ToUpper() + Token.Mid(1).ToLower());
            }
            break;
        }
    static const TCHAR* const Stock[] = { TEXT(""), TEXT("Warrior"), TEXT("Paladin"), TEXT("Hunter"), TEXT("Rogue"), TEXT("Priest"),
        TEXT("DeathKnight"), TEXT("Shaman"), TEXT("Mage"), TEXT("Warlock"), TEXT(""), TEXT("Druid") };
    if (Class < UE_ARRAY_COUNT(Stock) && *Stock[Class]) Names.AddUnique(Stock[Class]);
    Names.Remove(FString());
    return Names;
}

FString RaceName(FMpqManager* Mpq, uint8 Race)
{
    FDbcParser Dbc;
    if (LoadDbc(Mpq, TEXT("DBFilesClient\\ChrRaces.dbc"), Dbc) && Dbc.GetFieldCount() > 11)
        for (uint32 R = 0; R < Dbc.GetRecordCount(); ++R)
            if (Dbc.GetUInt(R, 0) == Race) return MovieToken(Dbc.GetString(R, 11)); // client file string
    return FString();
}
}

bool FWowTrailerPlayer::IsPlaying() { return GTrailer.IsValid(); }

void FWowTrailerPlayer::Skip()
{
    if (GTrailer && GTrailer->Request.Options.bSkippable)
    {
        UE_LOG(LogWowTrailer, Log, TEXT("Movie %s skipped"), *GTrailer->Request.Name);
        Finish(true);
    }
}

bool FWowTrailerPlayer::IsValidMovieId(const FString& MovieId)
{
    if (MovieId.IsEmpty() || MovieId.Len() > 64) return false;
    for (TCHAR C : MovieId)
        if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_' || C == '-')) return false;
    return true;
}

bool FWowTrailerPlayer::MovieExists(FMpqManager* Mpq, const FString& MovieId)
{
    if (!Mpq || !IsValidMovieId(MovieId)) return false;
    if (!FindLoose(Mpq->GetDataPath(), MovieId, {MovieExtensions[0], MovieExtensions[1]}).IsEmpty()) return true;
    for (const TCHAR* Ext : MovieExtensions) if (Mpq->FileExists(MpqPath(MovieId, Ext))) return true;
    return false;
}

FString FWowTrailerPlayer::GetServerRule(FMpqManager* Mpq, const FString& Key, const FString& Default)
{
    const FString* Value = ServerRules(Mpq).Find(Key.ToLower());
    return Value && !Value->IsEmpty() ? *Value : Default;
}

FString FWowTrailerPlayer::FindClassIntro(FMpqManager* Mpq, uint8 Race, uint8 Class, uint8 Gender)
{
    const FString Pattern = GetServerRule(Mpq, TEXT("ClassIntro"), TEXT("ClassIntro_{Class}_{Gender}"));
    const FString GenderName = Gender == 1 ? TEXT("Female") : TEXT("Male");
    const FString Race_ = RaceName(Mpq, Race);
    TArray<FString> Tried;
    for (const FString& ClassName : ClassNames(Mpq, Class))
    {
        // r381: per-class overrides first: ClassIntro.<Class>.<Gender>, then ClassIntro.<Class>,
        // then the general pattern. "none" at any level means no class intro (the race flyby plays).
        FString Rule = GetServerRule(Mpq, FString::Printf(TEXT("ClassIntro.%s.%s"), *ClassName, *GenderName), FString());
        if (Rule.IsEmpty()) Rule = GetServerRule(Mpq, FString::Printf(TEXT("ClassIntro.%s"), *ClassName), FString());
        if (Rule.IsEmpty()) Rule = Pattern;
        if (Rule.Equals(TEXT("none"), ESearchCase::IgnoreCase))
        {
            UE_LOG(LogWowTrailer, Log, TEXT("Class intro for %s %s is 'none' in the server's movie rules"), *ClassName, *GenderName);
            return FString();
        }
        const FString Id = Rule.Replace(TEXT("{Class}"), *ClassName, ESearchCase::IgnoreCase)
            .Replace(TEXT("{Gender}"), *GenderName, ESearchCase::IgnoreCase)
            .Replace(TEXT("{Race}"), *Race_, ESearchCase::IgnoreCase);
        if (!IsValidMovieId(Id)) { UE_LOG(LogWowTrailer, Warning, TEXT("Class intro rule '%s' gives an invalid movie id '%s'"), *Rule, *Id); continue; }
        Tried.AddUnique(Id);
        if (MovieExists(Mpq, Id)) return Id;
    }
    UE_LOG(LogWowTrailer, Log, TEXT("No class intro movie for race %u class %u gender %u (tried %s)"), Race, Class, Gender, *FString::Join(Tried, TEXT(", ")));
    return FString();
}

bool FWowTrailerPlayer::PlayMovie(FMpqManager* Mpq, const FString& MovieId, const FWowMovieOptions& Options, TFunction<void(bool)> OnDone)
{
    if (!IsValidMovieId(MovieId))
    {
        UE_LOG(LogWowTrailer, Warning, TEXT("Refused movie id '%s' (%s): only letters, digits, '_' and '-'"), *MovieId.Left(80), *Options.Reason);
        return false;
    }
    const FString PlayPath = ResolveMovie(Mpq, MovieId);
    if (PlayPath.IsEmpty())
    {
        UE_LOG(LogWowTrailer, Warning, TEXT("Movie %s (%s) not found in %s"), *MovieId, *Options.Reason, Mpq ? *Mpq->GetDataPath() : TEXT("(no data)"));
        return false;
    }
    FMovieRequest Request;
    Request.Name = MovieId;
    Request.PlayPath = PlayPath;
    Request.Subtitles = LoadSubtitles(Mpq, MovieId);
    Request.Options = Options;
    Request.OnDone = MoveTemp(OnDone);
    if (GTrailer)
    {
        UE_LOG(LogWowTrailer, Log, TEXT("Movie %s (%s) waits for %s"), *MovieId, *Options.Reason, *GTrailer->Request.Name);
        GQueue.Add(MoveTemp(Request));
        return true;
    }
    StartRequest(MoveTemp(Request));
    return true;
}

bool FWowTrailerPlayer::PlayIntroOnce(FMpqManager* Mpq, int32 Expansion, TFunction<void()> OnDone)
{
    // r222 (Claude-UI, Stefan 11.10): Options > Sound > Intro Movie: 0 Every time / 1 First time only (default) / 2 Never.
    const int32 Mode = FWowClientPreferences::GetIntroMovieMode();
    if (!Mpq || !GEngine || !GEngine->GameViewport || Mode == 2 || FWowClientPreferences::GetWorldOption(TEXT("skipintromovies"))) return false;
    const FString DataPath = Mpq->GetDataPath();
    if (DataPath.IsEmpty()) return false;
    FString Movie;
    const FString Rule = GetServerRule(Mpq, TEXT("LoginMovie"), FString());
    if (Rule.Equals(TEXT("none"), ESearchCase::IgnoreCase) || !FindLoose(DataPath, TEXT("ServerIntro"), {TEXT(".none")}).IsEmpty())
    {
        UE_LOG(LogWowTrailer, Log, TEXT("Server data folder asks for no intro movie"));
        return false;
    }
    if (!Rule.IsEmpty())
    {
        if (MovieExists(Mpq, Rule)) Movie = Rule;
        else UE_LOG(LogWowTrailer, Warning, TEXT("LoginMovie=%s from the server's MovieRules.txt not found; using the default"), *Rule);
    }
    if (Movie.IsEmpty() && MovieExists(Mpq, TEXT("ServerIntro"))) Movie = TEXT("ServerIntro");
    if (Movie.IsEmpty() && Expansion >= 0)
    {
        const FString Base = Expansion >= 2 ? TEXT("WOW_Intro_LK") : Expansion == 1 ? TEXT("WOW_Intro_BC") : TEXT("WOW_Intro");
        for (const TCHAR* Size : { TEXT("_1024"), TEXT("_800") })
            if (Movie.IsEmpty() && MovieExists(Mpq, Base + Size)) Movie = Base + Size;
    }
    if (Movie.IsEmpty())
    {
        UE_LOG(LogWowTrailer, Log, TEXT("No intro movie for expansion %d in %s"), Expansion, *DataPath);
        return false;
    }
    const FString Key = SeenKey(DataPath, Movie);
    bool bSeen = false;
    if (GConfig) GConfig->GetBool(TEXT("WowUnreal.IntroMovies"), *Key, bSeen, GGameUserSettingsIni);
    if (bSeen && Mode == 1) return false;
    if (GConfig)
    {
        // Marked before playing: a movie that crashes or cannot decode must not come back every login.
        GConfig->SetBool(TEXT("WowUnreal.IntroMovies"), *Key, true, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    FWowMovieOptions Options;
    Options.Reason = FString::Printf(TEXT("login, expansion %d"), Expansion);
    return PlayMovie(Mpq, Movie, Options, [Done = MoveTemp(OnDone)](bool) { if (Done) Done(); });
}
