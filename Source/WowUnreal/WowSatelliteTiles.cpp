#include "WowSatelliteTiles.h"
#include "Coord/WowCoordinate.h"
#include "WowWorldManager.h"
#include "WowSkyManager.h"
#include "WowWeatherComponent.h"
#include "WowClientPreferences.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "EngineUtils.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogWowSatellite, Log, All);

namespace
{
constexpr int32 GSubGrid = 4;                                   // 4x4 captures per tile
constexpr int32 GSubPixels = FWowSatelliteTiles::TilePixels / GSubGrid;
constexpr float GSettleSeconds = 12.f;                          // streaming settles first
constexpr float GSubInterval = 0.15f;                           // one small capture per step

struct FState
{
    TMap<FString, TSet<FIntPoint>> Known;                        // map -> recorded tiles
    TMap<FString, UTexture2D*> Textures;                         // "map|x|y|size", rooted
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AWowWorldManager> WorldManager;
    TWeakObjectPtr<AWowSkyManager> Sky;
    TWeakObjectPtr<ASceneCapture2D> CaptureActor;
    UTextureRenderTarget2D* Target = nullptr;                    // rooted (no static smart pointers: they outlive the UObject system)
    FString JobMap;
    FIntPoint Tile = FIntPoint(-1, -1);
    float Dwell = 0.f;
    float NextStep = 0.f;
    int32 NextSub = 0;                                           // 0..15, -1 = idle
    FImage Full;
    bool bJob = false;
    // r546 (Stefan: offline froze ~16 s on opening the world map in Satellite v2): tiles were
    // decoded and resized on the game thread, one JPEG per request. Now off the game thread.
    struct FLoadJob { std::atomic<bool> bDone{false}; bool bOk = false; FImage Image; };
    TMap<FString, TSharedRef<FLoadJob, ESPMode::ThreadSafe>> Jobs;  // same keys as Textures
    uint64 CreateFrame = 0;
    int32 CreatedThisFrame = 0;
};
FState& State() { static FState S; return S; }

FString MapDir(const FString& MapName) { return FPaths::ProjectSavedDir() / TEXT("MapTiles") / MapName; }

TSet<FIntPoint>& KnownTiles(const FString& MapName)
{
    FState& S = State();
    if (TSet<FIntPoint>* Found = S.Known.Find(MapName)) return *Found;
    TSet<FIntPoint>& Set = S.Known.Add(MapName);
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(MapDir(MapName) / TEXT("*.jpg")), true, false);
    for (const FString& File : Files)
    {
        FString X, Y;
        if (FPaths::GetBaseFilename(File).Split(TEXT("_"), &X, &Y)) Set.Add(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)));
    }
    return Set;
}

bool EnsureCapture(UWorld* World)
{
    FState& S = State();
    if (!S.Target)
    {
        UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), TEXT("WowSatelliteTileTarget"));
        Target->RenderTargetFormat = RTF_RGBA8;
        Target->ClearColor = FLinearColor(0.095f, 0.125f, 0.055f, 1.0f);
        Target->InitAutoFormat(GSubPixels, GSubPixels);
        Target->UpdateResourceImmediate(true);
        Target->AddToRoot();
        S.Target = Target;
    }
    if (!S.CaptureActor.IsValid() || S.CaptureActor->GetWorld() != World)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.ObjectFlags |= RF_Transient;
        ASceneCapture2D* Actor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), FVector::ZeroVector, FRotator(-90.f, 0.f, 0.f), Params);
        if (!Actor || !Actor->GetCaptureComponent2D()) return false;
        // Same settings as the minimap capture, so Satellite v2 looks like the minimap.
        USceneCaptureComponent2D* Capture = Actor->GetCaptureComponent2D();
        Capture->TextureTarget = S.Target;
        Capture->ProjectionType = ECameraProjectionMode::Orthographic;
        Capture->OrthoWidth = FWowCoordinate::TILE_SIZE * FWowCoordinate::SCALE / GSubGrid;
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
        Capture->bAutoCalculateOrthoPlanes = false;
        Capture->bUpdateOrthoPlanes = false;
        Capture->bUseCameraHeightAsViewTarget = false;
        Capture->LODDistanceFactor = 4.f;
        Capture->MaxViewDistanceOverride = 80000.f;
        Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
        Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        Capture->CompositeMode = ESceneCaptureCompositeMode::SCCM_Overwrite;
        Capture->ShowFlags.SetFog(false);
        Capture->ShowFlags.SetAtmosphere(false);
        Capture->ShowFlags.SetCloud(false);
        Capture->ShowFlags.SetVolumetricFog(false);
        Capture->ShowFlags.SetPostProcessing(false);
        Capture->ShowFlags.SetPointLights(false);
        Capture->ShowFlags.SetSpotLights(false);
        Capture->ShowFlags.SetRectLights(false);
        Capture->ShowFlags.SetParticles(false);
        // r363 (Stefan: water on the Satellite v2 map and minimap looks very noisy): the daytime
        // capture froze the sun glints on the wave normals into white dashes. Map tiles show
        // the water colour only - no specular, no reflections.
        Capture->ShowFlags.SetSpecular(false);
        Capture->ShowFlags.SetReflectionEnvironment(false);
        Capture->ShowFlags.SetScreenSpaceReflections(false);
        const float Half = Capture->OrthoWidth * 0.5f;
        Capture->bUseCustomProjectionMatrix = true;
        Capture->CustomProjectionMatrix = FReversedZOrthoMatrix(Half, Half, 1.f / 80000.f, 0.f);
        S.CaptureActor = Actor;
    }
    return true;
}

bool IsDaytime(UWorld* World)
{
    FState& S = State();
    if (!S.Sky.IsValid()) for (TActorIterator<AWowSkyManager> It(World); It; ++It) { S.Sky = *It; break; }
    if (!S.Sky.IsValid()) return true;
    const float Minutes = S.Sky->GetTimeOfDay();
    if (Minutes < 8.f * 60.f || Minutes > 17.f * 60.f) return false; // flat, bright light as on the minimap
    // r526 (Stefan: on Satellite v2 the Dun Morogh snow of tile 32_43 came out brown-beige next to
    // white neighbours): it was recorded at 14:31 in fog weather, whose dimmed, tinted light baked
    // into the tile. Record only in clear, mostly sunny weather.
    if (const UWowWeatherComponent* Weather = S.Sky->FindComponentByClass<UWowWeatherComponent>())
    {
        const FWowWeatherSample& Now = Weather->GetWeather();
        if ((Now.Kind != EWowWeatherKind::Clear && Now.Intensity > 0.02f) || Now.Cloud > 0.45f) return false;
    }
    return true;
}

// r545 (Stefan and Claude-UI: Satellite v2 snow came out cream/brown, the game view shows it
// neutral white-grey): the capture has post-processing off, so the sun's warm Light.dbc colour went
// into the file untouched and bright snow was never compressed like the player's tonemapper does.
// Per capture: neutralise the sun colour (white balance), then a soft shoulder tuned on Claude-UI's
// Kharanos measurements at noon in clear weather (tile ~(225,215,190) -> game ~(160,166,152)).
struct FTileGrade { uint8 Lut[3][256]; };
FTileGrade MakeTileGrade(UWorld* World)
{
    FState& S = State();
    FLinearColor Sun(1.f, 1.f, 1.f);
    if (S.Sky.IsValid())
    {
        TArray<UDirectionalLightComponent*> Lights;
        S.Sky->GetComponents<UDirectionalLightComponent>(Lights);
        for (UDirectionalLightComponent* Light : Lights)
            if (Light && Light->GetFName() == TEXT("SunLight"))
            {
                Sun = Light->GetLightColor();
                if (Light->bUseTemperature) Sun *= FLinearColor::MakeFromColorTemperature(Light->Temperature);
                break;
            }
    }
    const float Luma = FMath::Max(0.2126f * Sun.R + 0.7152f * Sun.G + 0.0722f * Sun.B, 1e-3f);
    const float Gain[3] = {
        FMath::Clamp(Luma / FMath::Max(Sun.R, 1e-3f), 0.7f, 1.5f),
        FMath::Clamp(Luma / FMath::Max(Sun.G, 1e-3f), 0.7f, 1.5f),
        FMath::Clamp(Luma / FMath::Max(Sun.B, 1e-3f), 0.7f, 1.5f) };
    FTileGrade Grade;
    for (int32 C = 0; C < 3; ++C)
        for (int32 V = 0; V < 256; ++V)
        {
            const float Linear = FLinearColor::FromSRGBColor(FColor(uint8(V), 0, 0)).R * Gain[C];
            const float Toned = Linear * 1.19f / (1.f + 1.98f * Linear); // shoulder: snow ~0.76 -> ~0.36
            Grade.Lut[C][V] = FLinearColor(Toned, 0.f, 0.f).ToFColor(true).R;
        }
    return Grade;
}

// One 1/16 capture: sub (I east, J south) of the tile, read back into the full image.
bool CaptureSub(UWorld* World, int32 Sub, AActor* HiddenPawn)
{
    FState& S = State();
    const int32 I = Sub % GSubGrid, J = Sub / GSubGrid;
    const float Step = FWowCoordinate::TILE_SIZE / GSubGrid;
    const FVector Center = FWowCoordinate::AdtToUE(S.Tile.X * FWowCoordinate::TILE_SIZE + (I + 0.5f) * Step, 0.f,
        S.Tile.Y * FWowCoordinate::TILE_SIZE + (J + 0.5f) * Step);
    // Highest ground in this square (centre and corners), so hills are not cut off.
    double Top = -1e9;
    const float R = Step * FWowCoordinate::SCALE * 0.45f;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(WowSatelliteGround), false, HiddenPawn);
    for (const FVector2D& O : { FVector2D(0, 0), FVector2D(R, R), FVector2D(R, -R), FVector2D(-R, R), FVector2D(-R, -R) })
    {
        FHitResult Hit;
        const FVector From(Center.X + O.X, Center.Y + O.Y, 300000.0), To(Center.X + O.X, Center.Y + O.Y, -300000.0);
        if (World->LineTraceSingleByChannel(Hit, From, To, ECC_WorldStatic, Params)) Top = FMath::Max(Top, Hit.ImpactPoint.Z);
    }
    if (Top < -1e8) return false; // nothing loaded here yet
    ASceneCapture2D* Actor = S.CaptureActor.Get();
    Actor->SetActorLocationAndRotation(FVector(Center.X, Center.Y, Top + 6000.0), FRotator(-90.f, 0.f, 0.f));
    USceneCaptureComponent2D* Capture = Actor->GetCaptureComponent2D();
    Capture->HiddenActors.Reset();
    if (HiddenPawn) Capture->HiddenActors.Add(HiddenPawn);
    Capture->CaptureScene();
    FImage Part;
    if (!FImageUtils::GetRenderTargetImage(S.Target, Part)) return false;
    Part.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    if (Part.SizeX != GSubPixels || Part.SizeY != GSubPixels) return false;
    {
        const FTileGrade Grade = MakeTileGrade(World); // BGRA8
        uint8* Px = Part.RawData.GetData();
        for (int64 N = int64(GSubPixels) * GSubPixels; N > 0; --N, Px += 4)
        {
            Px[0] = Grade.Lut[2][Px[0]];
            Px[1] = Grade.Lut[1][Px[1]];
            Px[2] = Grade.Lut[0][Px[2]];
        }
    }
    // Capture looks straight down with UE +X (north) up and +Y (east) right.
    for (int32 Row = 0; Row < GSubPixels; ++Row)
    {
        const uint8* Src = Part.RawData.GetData() + int64(Row) * GSubPixels * 4;
        uint8* Dst = S.Full.RawData.GetData() + (int64(J * GSubPixels + Row) * FWowSatelliteTiles::TilePixels + I * GSubPixels) * 4;
        FMemory::Memcpy(Dst, Src, GSubPixels * 4);
    }
    return true;
}
}

FString FWowSatelliteTiles::TileFile(const FString& MapName, int32 TileX, int32 TileY)
{
    return MapDir(MapName) / FString::Printf(TEXT("%d_%d.jpg"), TileX, TileY);
}

bool FWowSatelliteTiles::HasTile(const FString& MapName, int32 TileX, int32 TileY)
{
    return !MapName.IsEmpty() && KnownTiles(MapName).Contains(FIntPoint(TileX, TileY));
}

UTexture2D* FWowSatelliteTiles::LoadTile(const FString& MapName, int32 TileX, int32 TileY, int32 MaxPixels)
{
    if (!HasTile(MapName, TileX, TileY)) return nullptr;
    const int32 Size = FMath::Clamp(int32(FMath::RoundUpToPowerOfTwo(uint32(FMath::Max(MaxPixels, 16)))), 16, TilePixels);
    const FString Key = FString::Printf(TEXT("%s|%d|%d|%d"), *MapName, TileX, TileY, Size);
    FState& S = State();
    if (UTexture2D* const* Found = S.Textures.Find(Key)) return *Found;
    using FJobRef = TSharedRef<FState::FLoadJob, ESPMode::ThreadSafe>;
    if (const FJobRef* Running = S.Jobs.Find(Key))
    {
        const FJobRef Job = *Running;
        if (!Job->bDone.load()) return nullptr;
        // At most two new textures per frame keep the map smooth while it fills in.
        if (S.CreateFrame != GFrameCounter) { S.CreateFrame = GFrameCounter; S.CreatedThisFrame = 0; }
        if (Job->bOk && S.CreatedThisFrame >= 2) return nullptr;
        S.Jobs.Remove(Key);
        if (!Job->bOk) return nullptr; // still being written: asked again later
        ++S.CreatedThisFrame;
        UTexture2D* Texture = FImageUtils::CreateTexture2DFromImage(Job->Image);
        if (!Texture) return nullptr;
        Texture->Filter = TF_Trilinear;
        Texture->AddToRoot();
        S.Textures.Add(Key, Texture);
        return Texture;
    }
    if (S.Jobs.Num() >= 4) return nullptr; // a few decodes at a time; the rest on later frames
    FJobRef Job = MakeShared<FState::FLoadJob, ESPMode::ThreadSafe>();
    S.Jobs.Add(Key, Job);
    Async(EAsyncExecution::ThreadPool, [Job, File = TileFile(MapName, TileX, TileY), Size]()
    {
        FImage Image;
        if (FImageUtils::LoadImage(*File, Image))
        {
            if (Image.SizeX != Size || Image.SizeY != Size)
            {
                FImage Small;
                Image.ResizeTo(Small, Size, Size, Image.Format, Image.GammaSpace);
                Image = MoveTemp(Small);
            }
            Job->Image = MoveTemp(Image);
            Job->bOk = true;
        }
        Job->bDone.store(true);
    });
    return nullptr;
}

void FWowSatelliteTiles::ClearCache()
{
    for (const auto& Pair : State().Textures) if (IsValid(Pair.Value)) Pair.Value->RemoveFromRoot();
    State().Textures.Reset();
    State().Jobs.Reset(); // running decodes finish into their own shared job and are dropped
}

void FWowSatelliteTiles::Tick(UWorld* World, const FVector& PlayerLocation, float DeltaSeconds, AActor* HiddenPawn)
{
    if (!World || !FWowClientPreferences::GetWorldOption(TEXT("satelliterecord"))) return;
    FState& S = State();
    if (S.World.Get() != World) { S.World = World; S.WorldManager.Reset(); S.Sky.Reset(); S.bJob = false; S.Tile = FIntPoint(-1, -1); }
    if (!S.WorldManager.IsValid()) for (TActorIterator<AWowWorldManager> It(World); It; ++It) { S.WorldManager = *It; break; }
    if (!S.WorldManager.IsValid()) return;
    const FString MapName = S.WorldManager->GetMapName();
    if (MapName.IsEmpty()) return;

    const FIntPoint Tile = FWowCoordinate::WorldToTile(PlayerLocation);
    if (Tile != S.Tile || MapName != S.JobMap)
    {
        S.Tile = Tile; S.JobMap = MapName; S.Dwell = 0.f; S.bJob = false;
    }
    S.Dwell += DeltaSeconds;
    if (Tile.X < 0 || Tile.Y < 0 || Tile.X > 63 || Tile.Y > 63 || HasTile(MapName, Tile.X, Tile.Y)) return;
    if (!S.bJob)
    {
        if (S.Dwell < GSettleSeconds || !IsDaytime(World)) return;
        S.bJob = true; S.NextSub = 0; S.NextStep = 0.f;
        S.Full.Init(TilePixels, TilePixels, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    }
    S.NextStep -= DeltaSeconds;
    if (S.NextStep > 0.f) return;
    S.NextStep = GSubInterval;
    if (!EnsureCapture(World)) { S.bJob = false; return; }
    if (!CaptureSub(World, S.NextSub, HiddenPawn))
    {
        S.bJob = false; S.Dwell = 0.f; // try again later
        return;
    }
    if (++S.NextSub < GSubGrid * GSubGrid) return;

    // Done: write it off the game thread.
    S.bJob = false;
    FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("ImageWrapper"));
    const FString File = TileFile(MapName, Tile.X, Tile.Y);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    KnownTiles(MapName).Add(Tile);
    Async(EAsyncExecution::ThreadPool, [Image = MoveTemp(S.Full), File]()
    {
        if (!FImageUtils::SaveImageByExtension(*File, Image, 90))
            UE_LOG(LogWowSatellite, Warning, TEXT("Could not write %s"), *File);
    });
    S.Full = FImage();
    UE_LOG(LogWowSatellite, Log, TEXT("Satellite v2 tile recorded: %s %d_%d"), *MapName, Tile.X, Tile.Y);
}
