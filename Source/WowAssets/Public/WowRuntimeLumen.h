#pragma once
#include "CoreMinimal.h"

class UStaticMesh;
struct FMeshDescription;

// Call once after building a NEW mesh, before publishing it to any component.
// Runtime cards supply the surface cache; hardware ray tracing supplies geometry.
// This does not generate the distance fields required by software Lumen.
WOWASSETS_API void WowBuildRuntimeLumenCards(UStaticMesh* Mesh,
    const FMeshDescription& Description, bool bTerrain = false);
