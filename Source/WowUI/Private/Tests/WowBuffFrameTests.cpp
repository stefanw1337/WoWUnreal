#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "WowBuffFrameScript.h"
#include "WowSpellPresentation.h"
#include "WowConnectionManager.h"
#include "WowFrameManager.h"
#include "WowFrameXmlParser.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "WowPacketHandler.h"
#include "WowOpcodes.h"
#include "WowAssetCache.h"
#include "Mpq/MpqManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowEnchantClockTest,"WowUnreal.UI.BuffFrames.EnchantPacketAndClock",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowEnchantClockTest::RunTest(const FString&)
{
 FWowPacketHandler H;H.EntityManager.LocalPlayerGuid=1;
 auto& Item=H.EntityManager.PromoteToTyped(42,WowTypeMask::ITEM);
 Item.SetField(ItemField::ENCHANTMENT_1_1+3,283);
 Item.SetField(ItemField::ENCHANTMENT_1_1+4,120000);
 const double Start=Item.TempEnchantReceivedAt;
 TestEqual(TEXT("Field duration counts down in milliseconds"),Item.GetTempEnchantRemainingMs(Start+30),90000.0);
 Item.SetField(ItemField::ENCHANTMENT_1_1+5,2);
 TestEqual(TEXT("Charge update does not restart duration"),Item.TempEnchantReceivedAt,Start);
 auto Packet=[](uint64 Owner,uint32 Seconds){TArray<uint8>P;uint64 Guid=42;uint32 Slot=1;P.Append((uint8*)&Guid,8);P.Append((uint8*)&Slot,4);P.Append((uint8*)&Seconds,4);P.Append((uint8*)&Owner,8);return P;};
 H.HandlePacket(WowOpcode::SMSG_ITEM_ENCHANT_TIME_UPDATE,Packet(1,45));
 TestEqual(TEXT("Server seconds converted to remaining milliseconds"),Item.GetTempEnchantRemainingMs(Item.TempEnchantReceivedAt),45000.0);
 H.HandlePacket(WowOpcode::SMSG_ITEM_ENCHANT_TIME_UPDATE,Packet(2,999));
 TestEqual(TEXT("Other owner's packet ignored"),Item.GetTempEnchantRemainingMs(Item.TempEnchantReceivedAt),45000.0);
 TestEqual(TEXT("Expired enchant clamps to zero"),Item.GetTempEnchantRemainingMs(Item.TempEnchantDeadline+1),0.0);
 H.HandlePacket(WowOpcode::SMSG_ITEM_ENCHANT_TIME_UPDATE,{1,2});
 TestEqual(TEXT("Truncated packet cannot mutate timer"),Item.GetTempEnchantRemainingMs(Item.TempEnchantReceivedAt),45000.0);
 Item.SetField(ItemField::ENCHANTMENT_1_1+3,0);
 TestFalse(TEXT("Removal clears old lifetime"),Item.bTempEnchantTimed);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowBuffFramesTest,"WowUnreal.UI.BuffFrames.LiveLuaAndTooltip",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowBuffFramesTest::RunTest(const FString&)
{
 FString Data;FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),Data);
 FMpqManager Mpq;if(!TestTrue(TEXT("Local data"),Mpq.Initialize(Data)))return false;
 FDbcStore::Get().LoadAll(Mpq);FDbcStore::Get().LoadDeferred(Mpq);
 auto* RangeConnection=NewObject<UWowConnectionManager>();
 auto& RangeEntities=RangeConnection->PacketHandler.EntityManager;
 RangeEntities.LocalPlayerGuid=1;
 auto& RangePlayer=RangeEntities.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT);
 auto& RangeTarget=RangeEntities.PromoteToTyped(2,WowTypeMask::UNIT);
 RangePlayer.Movement.Position=FVector::ZeroVector;
 RangeTarget.Movement.Position=FVector(50,0,0);
 TestEqual(TEXT("Judgement uses DBC range at distant target"),FWowSpellPresentation::InRange(RangeConnection,20271,2),0);
 RangeTarget.Movement.Position=FVector(4,0,0);
 TestEqual(TEXT("Judgement becomes available nearby"),FWowSpellPresentation::InRange(RangeConnection,20271,2),1);
 TestEqual(TEXT("Self Seal does not depend on target distance"),FWowSpellPresentation::InRange(RangeConnection,21084,2),-1);
 // Helpful targeting must include friendly NPCs, not only players.
 RangePlayer.SetField(UnitField::HEALTH,100);
 RangePlayer.SetField(UnitField::FACTIONTEMPLATE,1);
 RangeTarget.SetField(UnitField::HEALTH,100);
 RangeTarget.SetField(UnitField::FACTIONTEMPLATE,1);
 auto& Friend=RangeEntities.PromoteToTyped(3,WowTypeMask::PLAYER|WowTypeMask::UNIT);
 Friend.SetField(UnitField::FACTIONTEMPLATE,1);Friend.SetField(UnitField::HEALTH,100);
 TestEqual(TEXT("Heal selected friendly NPC"),RangeConnection->ResolveHelpfulSpellTarget(635,2),int64(2));
 TestEqual(TEXT("Buff selected friendly player"),RangeConnection->ResolveHelpfulSpellTarget(19740,3),int64(3));
 TestEqual(TEXT("Seal remains self-only with friendly selected"),RangeConnection->ResolveHelpfulSpellTarget(21084,3),int64(1));
 RangeTarget.SetField(UnitField::FACTIONTEMPLATE,14);
 TestEqual(TEXT("Heal self with enemy selected"),RangeConnection->ResolveHelpfulSpellTarget(635,2),int64(1));
 TestEqual(TEXT("Offensive spell preserves enemy target"),RangeConnection->ResolveHelpfulSpellTarget(20271,2),int64(2));
 TestEqual(TEXT("Heal self with no target"),RangeConnection->ResolveHelpfulSpellTarget(635,0),int64(1));
 TestEqual(TEXT("Redemption does not resurrect living player"),RangeConnection->ResolveHelpfulSpellTarget(7328,3),int64(-1));
 Friend.SetField(UnitField::HEALTH,0);
 TestEqual(TEXT("Redemption uses selected dead friendly player"),RangeConnection->ResolveHelpfulSpellTarget(7328,3),int64(3));
 TestEqual(TEXT("Ordinary heal does not select dead friend"),RangeConnection->ResolveHelpfulSpellTarget(635,3),int64(1));
 RangeConnection->PendingTargetSpell=7328;
 TestFalse(TEXT("Invalid resurrection target keeps selection pending"),RangeConnection->CompleteSpellTarget(2));
 TestEqual(TEXT("Pending spell retained"),RangeConnection->PendingTargetSpell,uint32(7328));
 RangeConnection->CancelSpellTarget();
 TestEqual(TEXT("Cancel clears spell targeting"),RangeConnection->PendingTargetSpell,uint32(0));
 FWowPacketHandler H;H.EntityManager.LocalPlayerGuid=1;
 auto& Player=H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT);
 auto& Item=H.EntityManager.PromoteToTyped(42,WowTypeMask::ITEM);
 Item.SetField(ObjectField::ENTRY,2092);Item.SetField(ItemField::ENCHANTMENT_1_1+3,283);
 Item.SetField(ItemField::ENCHANTMENT_1_1+4,1800000);Item.SetField(ItemField::ENCHANTMENT_1_1+5,3);
 Player.SetField(PlayerField::INV_SLOT_HEAD+15*2,42);
 for(int I=0;I<11;++I){auto&A=Player.Auras[I];A.SpellId=I%2?1243:17;A.Flags=0x10;A.bActive=true;A.DurationMs=120000;A.RemainingMs=90000;A.ReceivedAtSeconds=FPlatformTime::Seconds();A.Charges=I==0?3:0;}
 Player.Auras[11]=Player.Auras[0];Player.Auras[11].Flags=0x80;Player.Auras[11].SpellId=589;
 FWowLuaVM VM;if(!VM.Initialize())return false;
 FWowAssetCache Cache;FWowFrameManager Frames;FWowEventSystem Events;
 Events.SetLuaVM(&VM);Events.SetFrameManager(&Frames);Frames.SetEventSystem(&Events);
 FWowLuaContext Context;Context.FrameManager=&Frames;Context.EventSystem=&Events;Context.EntityManager=&H.EntityManager;
 WowLuaApi::SetContext(VM.GetState(),&Context);
 ON_SCOPE_EXIT{Frames.SetEventSystem(nullptr);WowLuaApi::ClearContext(VM.GetState());};
 auto* Canvas=NewObject<UCanvasPanel>();Frames.Initialize(Canvas);Frames.SetMpqManager(&Mpq);Frames.SetAssetCache(&Cache);
 TestTrue(TEXT("Fixture HUD"),VM.ExecuteString(TEXT(R"LUA(
 UIParent=CreateFrame("Frame","UIParent");UIParent:SetSize(1024,768)
 MinimapCluster=CreateFrame("Frame","MinimapCluster",UIParent)
 MinimapCluster:SetSize(180,180);MinimapCluster:SetPoint("TOPRIGHT",UIParent,"TOPRIGHT",0,0)

 )LUA")));
 TFunction<void(FWowFrameDef&)> StripScripts=[&](FWowFrameDef& D){D.Scripts.Empty();for(auto&C:D.Children)StripScripts(C);};
 for(const TCHAR* File:{TEXT("BuffFrame.xml"),TEXT("GameTooltipTemplate.xml")})
 {
  TArray<uint8> Bytes;if(!TestTrue(TEXT("Stock template bytes"),Mpq.ReadFile(FString(TEXT("Interface\\FrameXML\\"))+File,Bytes)))return false;
  for(auto& Directive:FWowFrameXmlParser::ParseXml(Bytes,File))
   if(Directive.Type==FWowXmlDirective::EType::Frame && Directive.FrameDef.bVirtual)
   {auto& D=Directive.FrameDef;StripScripts(D);Frames.RegisterTemplate(D.Name,D);}
 }
 TestTrue(TEXT("Real tooltip template"),VM.ExecuteString(TEXT("GameTooltip=CreateFrame('GameTooltip','GameTooltip',UIParent,'GameTooltipTemplate')")));
 TestTrue(TEXT("Install production buff frames"),VM.ExecuteString(WowBuffFrameScript()));
 TestTrue(TEXT("Named frames, filtering and two hand API"),VM.ExecuteString(TEXT(R"LUA(
 assert(BuffFrame.visibleAuraCount==11 and DebuffFrame.visibleAuraCount==1)
 assert(BuffButton11:IsShown() and not BuffButton12)
 assert(TempEnchant1:IsShown() and TempEnchant1:GetID()==16)
 local mh,ms,mc,oh=GetWeaponEnchantInfo();assert(mh and ms>1790000 and mc==3 and not oh)
 BuffButton1:GetScript("OnEnter")(BuffButton1)
 )LUA")));
 TestTrue(TEXT("Hidden stock icon and roots recover on aura refresh"),VM.ExecuteString(TEXT(R"LUA(
 BuffButton1.icon:Hide(); BuffFrame:Hide()
 BuffFrame:GetScript("OnEvent")(BuffFrame,"UNIT_AURA","player")
 assert(BuffFrame:IsShown() and BuffButton1:IsShown() and BuffButton1.icon:IsShown())
 )LUA")));
 auto* Tip=Frames.GetTooltipState(Frames.FindFrame(TEXT("GameTooltip")));
 if(!TestNotNull(TEXT("Real tooltip state"),Tip))return false;
 TestEqual(TEXT("Tooltip resolves aura spell rather than array slot"),Tip->SpellId,17);
 TestTrue(TEXT("Tooltip includes description and stack/time"),Tip->Lines.Num()>=4);
 auto SlotFor=[&](const TCHAR* Name){return Cast<UCanvasPanelSlot>(Frames.GetWidgetForHandle(Frames.FindFrame(Name))->Slot);};
 auto* Icon=Frames.GetNamedTexture(TEXT("BuffButton1Icon"));
 auto* IconSlot=Icon?Cast<UCanvasPanelSlot>(Icon->Slot):nullptr;
 TestTrue(TEXT("Buff icon keeps zero stretch margins after texture assignment"),IconSlot && IconSlot->GetOffsets()==FMargin(0));
 TestTrue(TEXT("Buff icon has a loaded image"),Icon && Icon->GetBrush().GetResourceObject()!=nullptr);
 TestTrue(TEXT("Changing aura texture preserves its button bounds"),VM.ExecuteString(TEXT("BuffButton1.icon:SetTexture('Interface/Icons/Spell_Holy_SealOfMight')")));
 TestTrue(TEXT("Replacement texture keeps zero stretch margins"),IconSlot && IconSlot->GetOffsets()==FMargin(0));
 auto* B1=SlotFor(TEXT("BuffButton1"));auto* B9=SlotFor(TEXT("BuffButton9"));
 TestTrue(TEXT("Ninth buff wraps beneath first with same X"),B9->GetPosition().Y>B1->GetPosition().Y && FMath::IsNearlyEqual(B9->GetPosition().X,B1->GetPosition().X));
 auto* BF=SlotFor(TEXT("BuffFrame"));auto* MM=SlotFor(TEXT("MinimapCluster"));
 TestTrue(TEXT("Buff frame clears minimap"),BF->GetPosition().X+BF->GetSize().X<=MM->GetPosition().X);
 auto* EnchantRoot=SlotFor(TEXT("TemporaryEnchantFrame"));auto* DebuffRoot=SlotFor(TEXT("DebuffFrame"));
 TestTrue(TEXT("Debuff row below enchant row"),DebuffRoot->GetPosition().Y>=EnchantRoot->GetPosition().Y+EnchantRoot->GetSize().Y);
 auto* LastLine=Frames.GetNamedFontString(TEXT("GameTooltipTextLeft4"));
 auto* LastSlot=LastLine?Cast<UCanvasPanelSlot>(LastLine->Slot):nullptr;
 TestTrue(TEXT("Tooltip background contains final duration line"),LastSlot && LastSlot->GetPosition().Y+LastSlot->GetSize().Y<=SlotFor(TEXT("GameTooltip"))->GetSize().Y);
 FString Preview;
 if(FParse::Value(FCommandLine::Get(),TEXT("buffpreview="),Preview))
 {
  FWidgetRenderer Renderer(true);auto* Target=Renderer.DrawWidget(Canvas->TakeWidget(),FVector2D(1024,768));
  if(!TestNotNull(TEXT("Rendered buff HUD"),Target))return false;
  TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
  TestTrue(TEXT("Read rendered pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
  TArray64<uint8>Png;FImageUtils::PNGCompressImageArray(1024,768,Pixels,Png);
  TestTrue(TEXT("Save actual frame preview"),FFileHelper::SaveArrayToFile(Png,*Preview));
 }
 TestTrue(TEXT("Weapon tooltip"),VM.ExecuteString(TEXT("TempEnchant1:GetScript('OnEnter')(TempEnchant1)")));
 FString WeaponText;for(const auto& Line:Tip->Lines)WeaponText+=Line.Text;
 TestTrue(TEXT("Weapon tooltip contains enchant name from DBC"),!FDbcStore::Get().EnchantmentName(283).IsEmpty() && WeaponText.Contains(FDbcStore::Get().EnchantmentName(283)));
 TestTrue(TEXT("Weapon tooltip contains charges"),WeaponText.Contains(TEXT("3 charges")));
 Player.Auras[0]=FAuraInfo();Events.FireEvent(TEXT("UNIT_AURA"),{TEXT("player")});
 TestTrue(TEXT("Removal compacts buffs without mixing debuffs"),VM.ExecuteString(TEXT("assert(BuffFrame.visibleAuraCount==10 and not BuffButton11:IsShown() and DebuffFrame.visibleAuraCount==1)")));
 Item.TempEnchantDeadline=FPlatformTime::Seconds()-1;Events.FireEvent(TEXT("UNIT_INVENTORY_CHANGED"),{TEXT("player")});
 TestTrue(TEXT("Expired weapon enchant disappears"),VM.ExecuteString(TEXT("assert(not TempEnchant1:IsShown());local mh=GetWeaponEnchantInfo();assert(not mh)")));
 Item.SetField(ItemField::ENCHANTMENT_1_1,123);Item.SetField(ItemField::ENCHANTMENT_1_1+3,0);
 TestTrue(TEXT("Permanent enchant excluded"),VM.ExecuteString(TEXT("local mh=GetWeaponEnchantInfo();assert(not mh)")));
 return true;
}
#endif
