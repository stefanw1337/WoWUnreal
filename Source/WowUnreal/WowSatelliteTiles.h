#pragma once
#include "CoreMinimal.h"

class UWorld;
class AActor;
class UTexture2D;

/**
 * Satellite v2 (r295, Stefan): an extra world-map layer made from our own top-down renders
 * (the same look as the minimap), next to Blizzard's satellite BLPs.
 *
 * One 1024 px JPEG per ADT tile: Saved/MapTiles/<Map>/<X>_<Y>.jpg. X is the east tile index
 * and Y the south index - the same numbers as md5translate's "mapX_Y" and the ADT file names,
 * so the world map can place a tile exactly where it places the BLP tile.
 *
 * Tiles are recorded while playing: the tile you stand in is photographed (4x4 small
 * captures spread over a few seconds, daytime only, once streaming has settled) the first
 * time you are there. Game thread only.
 */
struct FWowSatelliteTiles
{
    static constexpr int32 TilePixels = 1024;

    static FString TileFile(const FString& MapName, int32 TileX, int32 TileY);
    /** Cheap: one directory scan per map, kept up to date as tiles are recorded. */
    static bool HasTile(const FString& MapName, int32 TileX, int32 TileY);
    /** The tile scaled to at most MaxPixels (power of two, 16..1024), cached; nullptr when the
     *  tile is not recorded yet. A continent view should ask for small sizes (64-128). */
    static UTexture2D* LoadTile(const FString& MapName, int32 TileX, int32 TileY, int32 MaxPixels = 256);
    /** Call every frame with the player's location. Records the current tile when needed. */
    static void Tick(UWorld* World, const FVector& PlayerLocation, float DeltaSeconds, AActor* HiddenPawn);
    /** Drop cached textures (map change, low memory). */
    static void ClearCache();
};
