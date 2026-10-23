#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"
#include "Misc/Paths.h"
struct FWowMapArea
{
 uint32 Id=0,Map=0,Area=0;FString Folder;float Left=0,Right=0,Top=0,Bottom=0;
 // Dungeon/raid entries have no bounds in WorldMapArea; their floors come from DungeonMap.dbc.
 bool bDungeon=false;int32 DefaultFloor=0;
 // Instance without a WorldMapArea entry (most vanilla/TBC dungeons): shown from its WMO minimap only.
 bool bSynthetic=false;FString Name;
 FVector2D Project(double X,double Y)const{return FVector2D((Y-Left)/(Right-Left),(X-Top)/(Bottom-Top));}
};
struct FWowMapOverlay{uint32 Area;FString Name;int32 Width,Height,X,Y;};
// DungeonMap.dbc (3.3.5): ID, MapID, FloorIndex, Min(x,y), Max(x,y), ParentWorldMapID[, Flags].
struct FWowDungeonFloor
{
 uint32 Id=0,Map=0,ParentWorldMap=0;int32 Floor=0;float MinX=0,MinY=0,MaxX=0,MaxY=0;
 bool Contains(double X,double Y)const{return X>=MinX&&X<=MaxX&&Y>=MinY&&Y<=MaxY;}
 double AreaSize()const{return double(MaxX-MinX)*double(MaxY-MinY);}
};
// Minimap tiles of an instance's global WMO (Textures\Minimap, "<wmo>_<group>_<x>_<y>.blp").
// This is the "satellite" view for dungeons, and the only map for instances without
// WorldMapArea artwork. Tile size and axis order are fitted against the group bounds
// because they are not documented; the fit result is logged.
struct FWowWmoMinimap
{
 struct FTile{int32 Group=0,X=0,Y=0;FString Path;};
 FString WmoPath;FVector Position=FVector::ZeroVector;float Yaw=0;
 TArray<FBox> Groups;TArray<FTile> Tiles;
 double TileSize=256;bool bSwapXY=false;bool bLoaded=false;
 double MinX=0,MinY=0,MaxX=0,MaxY=0; // world bounds of the groups that have tiles
 bool IsValid()const{return Tiles.Num()>0&&MaxX>MinX&&MaxY>MinY;}
 static constexpr double MapOrigin=17066.666666;
 /** WMO local (x,y) -> world (X,Y). Yaw is snapped to 90 degrees (instance WMOs are axis aligned). */
 FVector2D ToWorld(double LX,double LY)const
 {
  const int32 Quarter=(FMath::RoundToInt(Yaw/90.f)%4+4)%4;
  double RX=LX,RY=LY;
  if(Quarter==1){RX=-LY;RY=LX;}else if(Quarter==2){RX=-LX;RY=-LY;}else if(Quarter==3){RX=LY;RY=-LX;}
  return FVector2D(MapOrigin-Position.Z+RX,MapOrigin-Position.X+RY);
 }
 /** World rectangle (min X/Y, max X/Y) covered by one tile. */
 FBox2D TileWorld(const FTile& T)const
 {
  const FBox& B=Groups[T.Group];
  const int32 IX=bSwapXY?T.Y:T.X,IY=bSwapXY?T.X:T.Y;
  const double X0=B.Min.X+IX*TileSize,Y0=B.Min.Y+IY*TileSize;
  const FVector2D A=ToWorld(X0,Y0),C=ToWorld(X0+TileSize,Y0+TileSize);
  return FBox2D(FVector2D(FMath::Min(A.X,C.X),FMath::Min(A.Y,C.Y)),FVector2D(FMath::Max(A.X,C.X),FMath::Max(A.Y,C.Y)));
 }
 FBox2D GroupWorld(int32 G)const
 {
  const FBox& B=Groups[G];
  const FVector2D A=ToWorld(B.Min.X,B.Min.Y),C=ToWorld(B.Max.X,B.Max.Y);
  return FBox2D(FVector2D(FMath::Min(A.X,C.X),FMath::Min(A.Y,C.Y)),FVector2D(FMath::Max(A.X,C.X),FMath::Max(A.Y,C.Y)));
 }
 /** Chunk scan of a WDT: global WMO name (MWMO) and its placement (MODF). */
 static bool ReadWdt(const TArray<uint8>& Bytes,FString& OutPath,FVector& OutPos,float& OutYaw)
 {
  for(int32 O=0;O+8<=Bytes.Num();)
  {
   const uint8* C=Bytes.GetData()+O;const int32 Size=*reinterpret_cast<const int32*>(C+4);
   if(Size<0||O+8+Size>Bytes.Num())break;
   const bool Mwmo=FMemory::Memcmp(C,"OMWM",4)==0||FMemory::Memcmp(C,"MWMO",4)==0;
   const bool Modf=FMemory::Memcmp(C,"FDOM",4)==0||FMemory::Memcmp(C,"MODF",4)==0;
   if(Mwmo&&Size>1)OutPath=FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(C+8)));
   if(Modf&&Size>=64)
   {
    const float* F=reinterpret_cast<const float*>(C+8+8);
    OutPos=FVector(F[0],F[1],F[2]);OutYaw=F[4];
   }
   O+=8+Size;
  }
  return !OutPath.IsEmpty();
 }
 /** Group bounding boxes from the root file's MOGI chunk (32 bytes per group). */
 static TArray<FBox> ReadGroupBounds(const TArray<uint8>& Bytes)
 {
  TArray<FBox> Out;
  for(int32 O=0;O+8<=Bytes.Num();)
  {
   const uint8* C=Bytes.GetData()+O;const int32 Size=*reinterpret_cast<const int32*>(C+4);
   if(Size<0||O+8+Size>Bytes.Num())break;
   if(FMemory::Memcmp(C,"IGOM",4)==0||FMemory::Memcmp(C,"MOGI",4)==0)
   {
    for(int32 G=0;G+32<=Size;G+=32)
    {const float* F=reinterpret_cast<const float*>(C+8+G+4);Out.Add(FBox(FVector(F[0],F[1],F[2]),FVector(F[3],F[4],F[5])));}
    break;
   }
   O+=8+Size;
  }
  return Out;
 }
 /** Tiles from md5translate for this WMO, then fit tile size/axes and compute the world bounds. */
 void Build(const TMap<FString,FString>& Satellite)
 {
  Tiles.Reset();
  FString Base=FPaths::GetBaseFilename(WmoPath).ToLower()+TEXT("_");
  for(const auto& KV:Satellite)
  {
   const FString File=FPaths::GetCleanFilename(KV.Key);
   if(!File.StartsWith(Base)||!File.EndsWith(TEXT(".blp")))continue;
   TArray<FString> Parts;File.Mid(Base.Len(),File.Len()-Base.Len()-4).ParseIntoArray(Parts,TEXT("_"));
   if(Parts.Num()!=3||!Parts[0].IsNumeric()||!Parts[1].IsNumeric()||!Parts[2].IsNumeric())continue;
   FTile T;T.Group=FCString::Atoi(*Parts[0]);T.X=FCString::Atoi(*Parts[1]);T.Y=FCString::Atoi(*Parts[2]);T.Path=KV.Value;
   if(Groups.IsValidIndex(T.Group))Tiles.Add(MoveTemp(T));
  }
  // Fit: per group the tile count along each axis should be ceil(extent / tile size).
  double BestError=TNumericLimits<double>::Max();
  for(const double Size:{256.0,533.333333/2.0,533.333333,128.0})for(const bool Swap:{false,true})
  {
   TMap<int32,FIntPoint> MaxIndex;
   for(const auto& T:Tiles){FIntPoint& M=MaxIndex.FindOrAdd(T.Group,FIntPoint(-1,-1));M.X=FMath::Max(M.X,T.X);M.Y=FMath::Max(M.Y,T.Y);}
   double Error=0;
   for(const auto& KV:MaxIndex)
   {
    const FVector Ext=Groups[KV.Key].GetSize();
    const double EX=Swap?Ext.Y:Ext.X,EY=Swap?Ext.X:Ext.Y;
    Error+=FMath::Abs(FMath::CeilToDouble(EX/Size)-(KV.Value.X+1))+FMath::Abs(FMath::CeilToDouble(EY/Size)-(KV.Value.Y+1));
   }
   if(Error<BestError-1e-6){BestError=Error;TileSize=Size;bSwapXY=Swap;}
  }
  bool First=true;
  for(const auto& T:Tiles)
  {
   const FBox2D W=GroupWorld(T.Group);
   if(First){MinX=W.Min.X;MinY=W.Min.Y;MaxX=W.Max.X;MaxY=W.Max.Y;First=false;}
   else{MinX=FMath::Min(MinX,W.Min.X);MinY=FMath::Min(MinY,W.Min.Y);MaxX=FMath::Max(MaxX,W.Max.X);MaxY=FMath::Max(MaxY,W.Max.Y);}
  }
  UE_LOG(LogTemp,Log,TEXT("[WorldMap] WMO minimap %s: %d groups, %d tiles, tile %.1f yd, swapXY %d, fit error %.0f, yaw %.0f"),
   *WmoPath,Groups.Num(),Tiles.Num(),TileSize,bSwapXY?1:0,BestError,Yaw);
 }
};
struct FWowWorldMapData
{
 TArray<FWowMapArea> Areas;TArray<FWowMapOverlay> Overlays;TArray<FWowDungeonFloor> Floors;TMap<FString,FString> Satellite;
 TMap<uint32,FWowWmoMinimap> Wmo; // instance map id -> global WMO minimap
 void LoadAreas(const FDbcParser& P)
 {
  Areas.Reset();if(P.GetFieldCount()<8)return;
  for(uint32 I=0;I<P.GetRecordCount();++I)
  {
   FWowMapArea A;A.Id=P.GetUInt(I,0);A.Map=P.GetUInt(I,1);A.Area=P.GetUInt(I,2);A.Folder=P.GetString(I,3);
   A.Left=P.GetFloat(I,4);A.Right=P.GetFloat(I,5);A.Top=P.GetFloat(I,6);A.Bottom=P.GetFloat(I,7);
   if(P.GetFieldCount()>=10)A.DefaultFloor=int32(P.GetUInt(I,9));
   if(A.Folder.IsEmpty())continue;
   A.bDungeon=A.Left==A.Right||A.Top==A.Bottom;
   Areas.Add(MoveTemp(A));
  }
 }
 void LoadOverlays(const FDbcParser& P)
 {
  Overlays.Reset();if(P.GetFieldCount()<13)return;
  for(uint32 I=0;I<P.GetRecordCount();++I)Overlays.Add({P.GetUInt(I,1),P.GetString(I,8),int32(P.GetUInt(I,9)),int32(P.GetUInt(I,10)),int32(P.GetUInt(I,11)),int32(P.GetUInt(I,12))});
 }
 void LoadFloors(const FDbcParser& P)
 {
  Floors.Reset();if(P.GetFieldCount()<8)return;
  for(uint32 I=0;I<P.GetRecordCount();++I)
  {
   FWowDungeonFloor F;F.Id=P.GetUInt(I,0);F.Map=P.GetUInt(I,1);F.Floor=int32(P.GetUInt(I,2));
   const float X0=P.GetFloat(I,3),Y0=P.GetFloat(I,4),X1=P.GetFloat(I,5),Y1=P.GetFloat(I,6);
   F.MinX=FMath::Min(X0,X1);F.MaxX=FMath::Max(X0,X1);F.MinY=FMath::Min(Y0,Y1);F.MaxY=FMath::Max(Y0,Y1);
   F.ParentWorldMap=P.GetUInt(I,7);
   if(F.MaxX>F.MinX&&F.MaxY>F.MinY)Floors.Add(F);
  }
 }
 /** The floors drawn for a dungeon map entry, sorted by floor number. */
 TArray<const FWowDungeonFloor*> FloorsFor(const FWowMapArea& A)const
 {
  TArray<const FWowDungeonFloor*> Out;
  for(const auto& F:Floors)if(F.ParentWorldMap==A.Id)Out.Add(&F);
  if(Out.IsEmpty())for(const auto& F:Floors)if(F.Map==A.Map&&F.ParentWorldMap==0)Out.Add(&F);
  Out.Sort([](const FWowDungeonFloor& L,const FWowDungeonFloor& R){return L.Floor<R.Floor;});
  return Out;
 }
 /** A map entry with the bounds of one dungeon floor (same projection as zones). */
 static FWowMapArea WithFloor(FWowMapArea A,const FWowDungeonFloor& F)
 {
  A.Left=F.MaxY;A.Right=F.MinY;A.Top=F.MaxX;A.Bottom=F.MinX;return A;
 }
 /** A map entry framing a whole WMO minimap at the 1002x668 page aspect, with a small margin. */
 static FWowMapArea WithWmo(FWowMapArea A,const FWowWmoMinimap& W)
 {
  double CX=(W.MinX+W.MaxX)*.5,CY=(W.MinY+W.MaxY)*.5;
  double HX=(W.MaxX-W.MinX)*.54,HY=(W.MaxY-W.MinY)*.54; // X is vertical on the page, Y horizontal
  const double Aspect=1002.0/668.0;
  if(HY<HX*Aspect)HY=HX*Aspect;else HX=HY/Aspect;
  A.Left=float(CY+HY);A.Right=float(CY-HY);A.Top=float(CX+HX);A.Bottom=float(CX-HX);return A;
 }
 void LoadSatellite(const FString& Text)
 {
  Satellite.Reset();TArray<FString> Lines;Text.ParseIntoArrayLines(Lines);
  for(FString Line:Lines){FString Key,Value;if(Line.Split(TEXT("\t"),&Key,&Value)){Key.TrimStartAndEndInline();Value.TrimStartAndEndInline();Satellite.Add(Key.ToLower(),Value);}}
 }
};
