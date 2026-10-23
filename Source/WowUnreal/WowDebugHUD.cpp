#include "WowDebugHUD.h"
#include "WowWorldManager.h"
#include "WowAssetCache.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AWowDebugHUD::DrawHUD()
{
    Super::DrawHUD();

    // Keep diagnostics available without drawing them over the real FrameXML
    // HUD during normal offline or online gameplay.
    if (!FParse::Param(FCommandLine::Get(), TEXT("debughud"))) return;
    if (!Canvas) return;

    float Y = Margin;

    // FPS
    float DeltaTime = GetWorld()->GetDeltaSeconds();
    float FPS = (DeltaTime > 0.0f) ? 1.0f / DeltaTime : 0.0f;
    FLinearColor FPSColor = (FPS >= 60.0f) ? FLinearColor::Green : (FPS >= 30.0f) ? FLinearColor::Yellow : FLinearColor::Red;
    DrawTextLine(Y, FString::Printf(TEXT("FPS: %.0f (%.1f ms)"), FPS, DeltaTime * 1000.0f), FPSColor);

    // Camera position
    APlayerController* PC = GetOwningPlayerController();
    if (PC)
    {
        FVector CamLoc;
        FRotator CamRot;
        PC->GetPlayerViewPoint(CamLoc, CamRot);
        DrawTextLine(Y, FString::Printf(TEXT("Pos: X=%.0f Y=%.0f Z=%.0f"), CamLoc.X, CamLoc.Y, CamLoc.Z));
    }

    // World manager stats
    AWowWorldManager* WorldMgr = nullptr;
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWowWorldManager::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        WorldMgr = Cast<AWowWorldManager>(Found[0]);
    }

    if (WorldMgr)
    {
        FIntPoint Tile = WorldMgr->GetCameraTile();
        DrawTextLine(Y, FString::Printf(TEXT("Map: %s  Tile: %d, %d"), *WorldMgr->GetMapName(), Tile.X, Tile.Y));
        DrawTextLine(Y, FString::Printf(TEXT("Tiles: %d loaded, %d pending"),
            WorldMgr->GetLoadedTileCount(), WorldMgr->GetPendingTileCount()));
        DrawTextLine(Y, FString::Printf(TEXT("Doodads: %d  WMO groups: %d"),
            WorldMgr->GetActiveDoodadCount(), WorldMgr->GetActiveWmoGroupCount()));

        // Asset cache memory stats
        FWowAssetCache* Cache = WorldMgr->GetAssetCache();
        if (Cache)
        {
            FWowCacheStats CStats = Cache->GetStats();
            int64 MB = CStats.TotalEstimatedMemory() / (1024 * 1024);
            int64 BudgetMB = Cache->MemoryBudget / (1024 * 1024);
            FLinearColor MemColor = Cache->IsOverBudget() ? FLinearColor::Red : FLinearColor::White;
            DrawTextLine(Y, FString::Printf(TEXT("Cache: %lld MB / %lld MB (%d tex, %d mesh)"),
                MB, BudgetMB, CStats.TextureCount, CStats.MeshCount), MemColor);
        }
    }
    else
    {
        DrawTextLine(Y, TEXT("WorldManager: not found"), FLinearColor::Red);
    }
}

void AWowDebugHUD::DrawTextLine(float& Y, const FString& Text, FLinearColor Color)
{
    // Draw shadow
    Canvas->DrawColor = FColor::Black;
    Canvas->DrawText(GEngine->GetSmallFont(), Text, Margin + 1.0f, Y + 1.0f);
    // Draw text
    Canvas->DrawColor = Color.ToFColor(true);
    Canvas->DrawText(GEngine->GetSmallFont(), Text, Margin, Y);
    Y += LineHeight;
}
