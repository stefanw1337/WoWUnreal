#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Formats/Dbc/WorldMapData.h"
class UWowUIManager;class FMpqManager;class FWowEntityManager;class UTexture2D;class FWowPacketHandler;struct FWowTaxiData;

/**
 * World map. Levels like the 3.3.5 map: zone -> continent -> World (Kalimdor,
 * Eastern Kingdoms, Northrend) -> Cosmic (Azeroth, Outland).
 *  - Mouse wheel zooms around the cursor; zooming out at 1x goes one level up.
 *  - Left-drag pans a zoomed map; left-click opens the zone/continent under the cursor.
 *  - Right-click goes one level up.
 *  - The player arrow (position + facing) shows on every level that contains the player.
 *  - Dungeons/raids (DungeonMap.dbc floors) get Up/Down floor buttons and a route overlay toggle.
 *  - Quest POIs (3.3.5 WorldMapPOIFrame): numbered circles for unfinished quests, a "?" for
 *    finished ones, and the selected quest's objective areas as blue blobs. Clicking a POI
 *    selects (super-tracks) that quest; the minimap then points at it.
 */
class SWowWorldMap:public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SWowWorldMap){} SLATE_END_ARGS()
 void Construct(const FArguments&,UWowUIManager*,FMpqManager*,FWowEntityManager*,TFunction<uint32()>);
 void Toggle();
 /** Open the map on the zone of this quest's current POI and select it (right-click in the tracker). */
 void ShowQuest(uint32 QuestId);
 /** Quest log + POI source (SMSG_QUEST_POI_QUERY_RESPONSE). */
 void SetQuestSource(FWowPacketHandler* InQuests){Quests=InQuests;}
 /** Flight master (SMSG_SHOWTAXINODES), like the 3.3.5 TaxiFrame: the continent of the flight master
  *  with every discovered flight point; hover shows the route and price, a click calls Fly with the
  *  node route (current node first). */
 void ShowTaxi(const FWowTaxiData& Taxi,TFunction<void(const TArray<uint32>&)> Fly);
 void CloseTaxi();
 bool IsTaxiOpen()const{return bTaxiMode;}
 virtual void Tick(const FGeometry&,double,float)override;

 static constexpr int32 WorldView=-2;
 static constexpr int32 CosmicView=-3;
private:
 void Select(int32 Index,int32 Floor=0);void Current();void GoUp();
 FWowMapArea ViewArea(int32 View)const;           // map entry with the shown dungeon floor's bounds
 int32 PlayerFloor(int32 View)const;               // floor number the player is on (dungeons)
 int32 FloorCount()const;int32 FloorSlot()const;void StepFloor(int32 Direction);
 bool HasSatellite(int32 View)const;               // instance has minimap tiles (WMO or terrain)
 bool HasMapArt(int32 View)const;                  // anything to draw for this entry
 void EnsureWmo(uint32 Map);                       // loads the instance WMO group bounds on first use
 void AddInstances(FMpqManager& InMpq);            // instance minimaps + vanilla/TBC instances without map artwork
 int32 PaintMap(const FGeometry&,FSlateWindowElementList&,int32)const;
 // Input from the map surface (local position in pixels, surface size).
 void OnWheel(FVector2D Local,FVector2D Size,float Delta);
 void OnClick(FVector2D Local,FVector2D Size);
 void OnDrag(FVector2D DeltaPixels,FVector2D Size);
 void OnHover(FVector2D Local,FVector2D Size);

 FVector2D ToMap(FVector2D Local,FVector2D Size)const;   // surface pixels -> 0..1 map space
 FVector2D ToLocal(FVector2D Map,FVector2D Size)const;   // 0..1 map space -> surface pixels
 void ClampView();
 int32 ParentOf(int32 View)const;
 int32 ContinentForMap(uint32 Map)const;
 int32 PickTarget(FVector2D MapPos)const;                // view to open at a map position (or INDEX_NONE)
 FString ViewName(int32 View)const;
 uint32 PlayerMap()const;
 bool PlayerOnView(int32 View,FVector2D& OutPos,float& OutFacing)const;
 static bool HotspotForMap(int32 View,uint32 Map,FBox2D& OutRect);
 struct FQuestPoiIcon{uint32 QuestId=0;int32 Number=0;bool bComplete=false;FVector2D MapPos;};
 void GatherQuestPois(TArray<FQuestPoiIcon>& Out)const;               // POI icons on the shown zone
 bool QuestPoiAt(FVector2D Local,FVector2D Size,uint32& OutQuest)const; // click/hover hit test
 void PaintQuestPois(const FGeometry&,FSlateWindowElementList&,int32,FVector2D Size)const;
 const FSlateBrush* PoiBrush(const TCHAR* Path,FBox2f UV)const;
 FVector2D TaxiTooltipAnchor()const;
 TSharedRef<SWidget> TaxiCoin(int32 Kind);

 // Satellite v2 tiles (FWowSatelliteTiles, our own renders) are cached and rooted there: only a weak ref here.
 // Path is the BLP drawn instead while that tile is not recorded yet (empty: nothing).
 struct FTile{FString Path;FVector2D Pos,Size;FVector2D UV=FVector2D(1,1);TStrongObjectPtr<UTexture2D> Texture;FSlateBrush Brush;
  FString V2Map;FIntPoint V2Tile=FIntPoint(-1,-1);int32 V2Pixels=0;TWeakObjectPtr<UTexture2D> V2Texture;
  bool IsReady()const{return Texture.IsValid()||V2Texture.IsValid();}};
 TArray<TSharedPtr<FString>> Options;TArray<int32> Indices;
 TArray<FTile> Tiles;FWowWorldMapData Data;
 TWeakObjectPtr<UWowUIManager> UI;FWowEntityManager* Entities=nullptr;TFunction<uint32()> Zone;
 int32 Selected=INDEX_NONE,NextTile=0;bool Satellite=false;
 bool bSatelliteV2=false; // with Satellite: our recorded tiles first (button cycles Classic -> Satellite -> Satellite v2)
 float ViewZoom=1.f;FVector2D ViewCenter=FVector2D(.5f,.5f);
 int32 HoverTarget=INDEX_NONE;
 int32 CurrentFloor=0;bool bRouteOverlay=false;
 FMpqManager* Mpq=nullptr;
 TStrongObjectPtr<UTexture2D> ArrowTexture;FSlateBrush ArrowBrush;double NextArrowTry=0;
 FWowPacketHandler* Quests=nullptr;uint32 HoverQuest=0;
 // Flight master mode.
 struct FTaxiPoint{uint32 Id=0;FString Name;FVector2D MapPos;bool bCurrent=false;bool bReachable=false;uint32 Cost=0;TArray<uint32> Route;};
 struct FTaxiEdge{uint32 From=0,To=0,Cost=0;};
 bool bTaxiMode=false;bool bTaxiEdgesLoaded=false;int32 HoverTaxi=INDEX_NONE;
 TArray<FTaxiPoint> TaxiPoints;TArray<FTaxiEdge> TaxiEdges;TFunction<void(const TArray<uint32>&)> TaxiFly;
 int32 TaxiPointAt(FVector2D Local,FVector2D Size)const;
 void PaintTaxi(const FGeometry&,FSlateWindowElementList&,int32,FVector2D Size)const;
 mutable TMap<FString,TSharedPtr<FSlateBrush>> PoiBrushes;mutable TArray<TStrongObjectPtr<UTexture2D>> PoiTextures;
};
