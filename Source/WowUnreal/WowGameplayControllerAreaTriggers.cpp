// r557 (Claude-Perf; Stefan: dungeon portals "need fixing as well as a teleport to dungeon they represent").
//
// Area triggers the way the 3.3.5 client handles them: AreaTrigger.dbc lists spheres and rotated
// boxes per map. When the player's feet enter one, the client sends CMSG_AREATRIGGER (trigger id)
// and the server decides what it means: a dungeon entrance or exit (areatrigger_teleport), an inn
// (rest area), a quest exploration objective, a scripted event. So every trigger is reported, once
// per entry; standing inside one does not repeat it, and after a teleport the triggers the player
// arrives in stay quiet until he has left them (a dungeon's exit trigger sits at its start point).
//
// Offline there is no server. r558: the portals then use the server's own destinations, a table made
// from AzerothCore's areatrigger_teleport (WowAreaTriggerTeleports.inl). For a trigger missing there
// they are resolved from the client data: Map.dbc gives each dungeon or raid the map and point of its
// entrance outside (the ghost entrance), the trigger nearest that point on the outside map is the
// portal, and the dungeon's own first trigger is its exit by its start point (Stockade 503,
// Deadmines 119, Shadowfang 194, Ragefire 2226). Walking into either moves the player across with
// the transfer loading screen.

#include "WowGameplayController.h"
#include "SWowLoadingScreen.h"
#include "WowConnectionManager.h"
#include "WowLoginController.h"
#include "WowSkyManager.h"
#include "WowWorldManager.h"
#include "Coord/WowCoordinate.h"
#include "Formats/DbcParser.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowAreaTrigger, Log, All);

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// r570 (build fix): the offline test of WowGameplayController.cpp is local to that file; the same
// test here (the offline flow is started with -offlineflow, -offlineloading or -offlineworld).
static bool WowAreaTriggerOfflineSession()
{
    const TCHAR* CmdLine = FCommandLine::Get();
    return FParse::Param(CmdLine, TEXT("offlineflow")) || FParse::Param(CmdLine, TEXT("offlineloading"))
        || FParse::Param(CmdLine, TEXT("offlineworld"));
}

namespace
{
    struct FWowAreaTrigger
    {
        uint32 Id = 0;
        uint32 Map = 0;
        FVector Pos = FVector::ZeroVector; // WoW world coordinates
        float Radius = 0.f;
        float Length = 0.f, Width = 0.f, Height = 0.f, Yaw = 0.f;

        bool Contains(const FVector& P) const
        {
            if (Radius > 0.f)
                return FVector::DistSquared(P, Pos) <= FMath::Square(Radius);
            // Box: length along the box's X, width along its Y, height around the centre; turned by Yaw.
            const FVector D = P - Pos;
            const float C = FMath::Cos(Yaw), S = FMath::Sin(Yaw);
            const float X = D.X * C + D.Y * S;
            const float Y = -D.X * S + D.Y * C;
            return FMath::Abs(X) <= Length * 0.5f && FMath::Abs(Y) <= Width * 0.5f && FMath::Abs(D.Z) <= Height * 0.5f;
        }
    };

    struct FWowOfflineDestination { uint32 Map = 0; FVector Pos = FVector::ZeroVector; float Orientation = TNumericLimits<float>::Max(); };

    struct FWowTeleportRow { uint32 Id; uint32 Map; float X, Y, Z, O; };
    const FWowTeleportRow GTeleportRows[] =
    {
#include "WowAreaTriggerTeleports.inl"
    };

    TArray<FWowAreaTrigger> GTriggers;
    TMap<uint32, FWowOfflineDestination> GOfflineDestinations; // trigger id -> where it leads offline
    bool GTriggersLoaded = false;

    // Detection state.
    FString GTriggerMapName;
    int32 GTriggerMapId = -1;
    TArray<int32> GMapTriggers;   // indices into GTriggers for the current map
    TSet<uint32> GInside;         // triggers the player stands in now
    bool GPrimed = false;         // false: the next check only records where we stand (no reports)
    FVector GLastFeet = FVector::ZeroVector;
    double GNextCheck = 0.0;

    // Offline transfer state.
    enum class EOfflineTransfer : uint8 { None, Queued, Loading };
    EOfflineTransfer GTransfer = EOfflineTransfer::None;
    uint32 GTransferMap = 0;
    FVector GTransferWow = FVector::ZeroVector;
    float GTransferOrientation = TNumericLimits<float>::Max(); // Max: keep the current facing
    double GTransferNotBefore = 0.0, GTransferStartedAt = 0.0, GTransferStableSince = 0.0;
    uint64 GTransferRevision = 0;

    void LoadTriggers(FMpqManager* Mpq)
    {
        if (GTriggersLoaded || !Mpq) return;
        GTriggersLoaded = true;
        TArray<uint8> Bytes;
        FDbcParser Parser;
        // AreaTrigger.dbc (3.3.5): ID, ContinentID, X, Y, Z, Radius, BoxLength, BoxWidth, BoxHeight, BoxYaw.
        if (!Mpq->ReadFile(TEXT("DBFilesClient\\AreaTrigger.dbc"), Bytes) || !Parser.Parse(Bytes) || Parser.GetFieldCount() < 10)
        {
            UE_LOG(LogWowAreaTrigger, Warning, TEXT("AreaTrigger.dbc not readable; area triggers are off"));
            return;
        }
        for (uint32 Row = 0; Row < Parser.GetRecordCount(); ++Row)
        {
            FWowAreaTrigger T;
            T.Id = Parser.GetUInt(Row, 0);
            T.Map = Parser.GetUInt(Row, 1);
            T.Pos = FVector(Parser.GetFloat(Row, 2), Parser.GetFloat(Row, 3), Parser.GetFloat(Row, 4));
            T.Radius = Parser.GetFloat(Row, 5);
            T.Length = Parser.GetFloat(Row, 6);
            T.Width = Parser.GetFloat(Row, 7);
            T.Height = Parser.GetFloat(Row, 8);
            T.Yaw = Parser.GetFloat(Row, 9);
            if (T.Radius <= 0.f && (T.Length <= 0.f || T.Width <= 0.f || T.Height <= 0.f)) continue;
            GTriggers.Add(T);
        }

        // r558: the server's destinations first.
        for (const FWowTeleportRow& Row : GTeleportRows)
            GOfflineDestinations.Add(Row.Id, { Row.Map, FVector(Row.X, Row.Y, Row.Z), Row.O });

        // Offline dungeon portals. Map.dbc (3.3.5): 0 ID, 2 InstanceType (1 dungeon, 2 raid),
        // 59 corpse (entrance) map, 60/61 entrance X/Y.
        Bytes.Reset();
        FDbcParser Maps;
        if (Mpq->ReadFile(TEXT("DBFilesClient\\Map.dbc"), Bytes) && Maps.Parse(Bytes) && Maps.GetFieldCount() > 61)
            for (uint32 Row = 0; Row < Maps.GetRecordCount(); ++Row)
            {
                const uint32 MapId = Maps.GetUInt(Row, 0);
                const uint32 Type = Maps.GetUInt(Row, 2);
                const uint32 EntranceMap = Maps.GetUInt(Row, 59);
                const FVector2D Entrance(Maps.GetFloat(Row, 60), Maps.GetFloat(Row, 61));
                if ((Type != 1 && Type != 2) || EntranceMap == MAX_uint32 || EntranceMap == MapId || Entrance.IsNearlyZero()) continue;
                const FWowAreaTrigger* Portal = nullptr;
                const FWowAreaTrigger* Exit = nullptr;
                double Best = FMath::Square(40.0);
                for (const FWowAreaTrigger& T : GTriggers)
                {
                    if (T.Map == EntranceMap)
                    {
                        const double D = FVector2D::DistSquared(FVector2D(T.Pos.X, T.Pos.Y), Entrance);
                        if (D < Best) { Best = D; Portal = &T; }
                    }
                    else if (T.Map == MapId && (!Exit || T.Id < Exit->Id)) Exit = &T;
                }
                if (!Portal || !Exit) continue;
                if (!GOfflineDestinations.Contains(Portal->Id)) GOfflineDestinations.Add(Portal->Id, { MapId, Exit->Pos });
                if (!GOfflineDestinations.Contains(Exit->Id)) GOfflineDestinations.Add(Exit->Id, { EntranceMap, Portal->Pos });
            }
        UE_LOG(LogWowAreaTrigger, Log, TEXT("Area triggers: %d loaded, %d offline teleports"), GTriggers.Num(), GOfflineDestinations.Num());
    }

    int32 MapIdByName(const FString& Name)
    {
        for (const auto& Map : FDbcStore::Get().Maps().GetAll())
            if (Map.InternalName == Name) return int32(Map.ID);
        return -1;
    }
}

void AWowGameplayController::TickAreaTriggers()
{
    TickOfflineMapTransfer();
    const double Now = FPlatformTime::Seconds();
    if (Now < GNextCheck) return;
    GNextCheck = Now + 0.1;

    ACharacter* Body = Cast<ACharacter>(GetPawn());
    auto* World = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass()));
    const bool bOffline = WowAreaTriggerOfflineSession();
    const bool bOnline = ConnectionManager && ConnectionManager->GetState() == EWowSessionState::WorldInGame;
    if (!Body || !World || !World->GetMpqManager() || (!bOffline && !bOnline) || !bGameplayHudVisible ||
        OfflineTransferScreen.IsValid() || bMapTerrainPending || GTransfer != EOfflineTransfer::None)
    {
        GPrimed = false;
        return;
    }
    LoadTriggers(World->GetMpqManager());
    if (GTriggers.Num() == 0) return;

    if (World->GetMapName() != GTriggerMapName)
    {
        GTriggerMapName = World->GetMapName();
        GTriggerMapId = MapIdByName(GTriggerMapName);
        GMapTriggers.Reset();
        for (int32 I = 0; I < GTriggers.Num(); ++I)
            if (int32(GTriggers[I].Map) == GTriggerMapId) GMapTriggers.Add(I);
        GPrimed = false;
    }

    const float Half = Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
    const FVector Feet = FWowCoordinate::UEToWow(Body->GetActorLocation() - FVector(0, 0, Half));
    // A jump of more than 50 yards since the last check is a teleport: record, don't report.
    if (FVector::DistSquared(Feet, GLastFeet) > FMath::Square(50.0)) GPrimed = false;
    GLastFeet = Feet;

    TSet<uint32> Inside;
    for (int32 Index : GMapTriggers)
        if (GTriggers[Index].Contains(Feet)) Inside.Add(GTriggers[Index].Id);

    if (GPrimed)
        for (uint32 Id : Inside)
        {
            if (GInside.Contains(Id)) continue;
            if (bOnline)
            {
                // The server measures the distance to the trigger from our last reported position:
                // send where we stand first (as for using objects, r373).
                OutgoingMove.LastSendSeconds = 0.0;
                SendMovementUpdate();
                TArray<uint8> Payload;
                Payload.Append(reinterpret_cast<const uint8*>(&Id), sizeof(Id));
                ConnectionManager->SendRawPacket(0x00B4, Payload); // CMSG_AREATRIGGER
                UE_LOG(LogWowAreaTrigger, Log, TEXT("Sent CMSG_AREATRIGGER %u at %s (map %d)"), Id, *Feet.ToString(), GTriggerMapId);
            }
            else if (const FWowOfflineDestination* Destination = GOfflineDestinations.Find(Id))
            {
                UE_LOG(LogWowAreaTrigger, Log, TEXT("Offline area trigger %u: to map %u at %s"), Id, Destination->Map, *Destination->Pos.ToString());
                StartOfflineMapTransfer(Destination->Map, Destination->Pos);
                if (GTransfer != EOfflineTransfer::None) GTransferOrientation = Destination->Orientation;
                break;
            }
            else
                UE_LOG(LogWowAreaTrigger, Verbose, TEXT("Offline area trigger %u entered (no offline action)"), Id);
        }
    GInside = MoveTemp(Inside);
    GPrimed = true;
}

void AWowGameplayController::StartOfflineMapTransfer(uint32 MapId, const FVector& WowFeet)
{
    if (GTransfer != EOfflineTransfer::None || OfflineTransferScreen.IsValid()) return;
    ACharacter* Body = Cast<ACharacter>(GetPawn());
    if (!Body || !FDbcStore::Get().Maps().GetById(MapId)) return;
    GTransfer = EOfflineTransfer::Queued;
    GTransferMap = MapId;
    GTransferWow = WowFeet;
    GTransferOrientation = TNumericLimits<float>::Max();
    GTransferStartedAt = FPlatformTime::Seconds();
    GTransferNotBefore = GTransferStartedAt + 0.1; // let the loading screen draw before the old map goes
    auto* Login = Cast<AWowLoginController>(UGameplayStatics::GetActorOfClass(this, AWowLoginController::StaticClass()));
    OfflineTransferScreen = Login ? Login->CreateTransferLoadingScreen(MapId) : SNew(SWowLoadingScreen);
    if (GEngine && GEngine->GameViewport)
        GEngine->GameViewport->AddViewportWidgetContent(OfflineTransferScreen.ToSharedRef(), 1000);
    SetIgnoreMoveInput(true); SetIgnoreLookInput(true);
    Body->GetCharacterMovement()->StopMovementImmediately();
    Body->GetCharacterMovement()->DisableMovement();
}

void AWowGameplayController::TickOfflineMapTransfer()
{
    if (GTransfer == EOfflineTransfer::None) return;
    const double Now = FPlatformTime::Seconds();
    ACharacter* Body = Cast<ACharacter>(GetPawn());
    auto* World = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass()));
    auto Finish = [&](const TCHAR* Why)
    {
        GTransfer = EOfflineTransfer::None;
        if (World) World->EndWorldEntryWarmup();
        bMapTerrainPending = false;
        FinishOfflineTransferLoading();
        if (Body) Body->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        UE_LOG(LogWowAreaTrigger, Log, TEXT("Offline map transfer to %u: %s after %.1f s"), GTransferMap, Why, Now - GTransferStartedAt);
    };
    if (!Body || !World) { Finish(TEXT("no player or world")); return; }

    if (GTransfer == EOfflineTransfer::Queued)
    {
        if (Now < GTransferNotBefore) return;
        if (!World->SelectMap(GTransferMap)) { Finish(TEXT("map could not be loaded")); return; }
        if (auto* Sky = Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(this, AWowSkyManager::StaticClass())))
            Sky->LoadLightZones(int32(GTransferMap));
        // Positions are feet; the pawn's location is the capsule centre.
        const FVector Destination = FWowCoordinate::WowToUE(GTransferWow) +
            FVector(0, 0, Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f);
        Body->GetCharacterMovement()->StopMovementImmediately();
        Body->SetActorLocation(Destination, false, nullptr, ETeleportType::TeleportPhysics);
        if (GTransferOrientation < TNumericLimits<float>::Max())
        {
            const FRotator Facing(0.f, FWowCoordinate::WowOrientationToUEYaw(GTransferOrientation), 0.f);
            Body->SetActorRotation(Facing);
            SetControlRotation(Facing);
            DesiredFacingYawDegrees = Facing.Yaw;
            bHasDesiredFacingYaw = true;
            GameplayCameraYawDegrees = Facing.Yaw;
            bHasGameplayCameraRotation = true;
            ApplyGameplayCameraRotation();
        }
        LastServerPosition = Destination;
        World->BeginWorldEntryWarmup(Destination);
        World->LoadTilesAroundPosition(Destination, 2);
        bMapTerrainPending = true;
        if (ConnectionManager)
            if (auto* Me = ConnectionManager->PacketHandler.EntityManager.Find(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid))
                Me->Movement.Position = GTransferWow;
        GTransfer = EOfflineTransfer::Loading;
        GTransferRevision = World->GetObjectStreamingRevision();
        GTransferStableSince = Now;
        return;
    }

    // Loading: like the transfer screen of the offline editor, wait until the area has streamed in
    // and stayed settled for 1.5 s (at most 60 s).
    const uint64 Revision = World->GetObjectStreamingRevision();
    if (Revision != GTransferRevision || !World->IsInitialLoadComplete()) { GTransferRevision = Revision; GTransferStableSince = Now; }
    if (OfflineTransferScreen.IsValid())
    {
        const float Fraction = World->IsWmoOnlyMap() ? (World->IsInitialLoadComplete() ? 1.f : .5f)
            : float(World->GetInitialTilesLoaded()) / FMath::Max(1, World->GetInitialTilesQueued());
        OfflineTransferScreen->SetProgress(.05f + .8f * Fraction, TEXT("Loading..."));
    }
    const bool bReady = World->IsInitialLoadComplete() && World->IsTerrainReadyAtPosition(LastServerPosition) &&
        World->GetObjectStreamingPassCount() >= 2 && Now - GTransferStableSince >= 1.5;
    if (bReady) Finish(TEXT("arrived"));
    else if (Now - GTransferStartedAt > 60.0) Finish(TEXT("timed out"));
}
