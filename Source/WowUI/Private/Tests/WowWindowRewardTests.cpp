#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowPanelLayout.h"
#include "WowChatFade.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowItemPresentation.h"
#include "WowConnectionManager.h"
#include "WowInventoryManager.h"
#include "Components/CanvasPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWindowRewardTest,"WowUnreal.UI.WindowsAndRewards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowWindowRewardTest::RunTest(const FString&)
{
    TestEqual(TEXT("Recent message is fully visible"),WowChatFade::Opacity(10,0,0,false),1.0f);
    TestEqual(TEXT("Messages fade smoothly"),WowChatFade::Opacity(13.5,0,0,false),.5f);
    TestEqual(TEXT("Old message fades out"),WowChatFade::Opacity(16,0,0,false),0.0f);
    TestEqual(TEXT("Hover restores old messages"),WowChatFade::Opacity(100,0,0,true),1.0f);
    TestEqual(TEXT("Mouse-leave restarts the hold period"),WowChatFade::Opacity(105,0,100,false),1.0f);
    TestEqual(TEXT("New activity revives older lines"),WowChatFade::Opacity(203,0,200,false),1.0f);
    const TArray<FVector2D> Sizes={FVector2D(540,720),FVector2D(430,580),FVector2D(430,600)};
    for(FVector2D View:{FVector2D(1920,1080),FVector2D(1280,720),FVector2D(800,600)})
    {
        const auto Layout=FWowPanelLayout::Arrange(Sizes,View);
        for(int32 I=0;I<Layout.Num();++I)
        {
            const FVector2D Edge=Layout[I].Position+Sizes[I]*Layout[I].Scale;
            TestTrue(TEXT("Window actions remain inside viewport"),Edge.X<=View.X&&Edge.Y<=View.Y);
            if(I)TestTrue(TEXT("Adjacent panels never overlap"),Layout[I].Position.X>=Layout[I-1].Position.X+Sizes[I-1].X*Layout[I-1].Scale);
        }
    }
    auto* Connection=NewObject<UWowConnectionManager>();
    TestEqual(TEXT("Missing item is described as loading, never a fake reward"),FWowItemPresentation::Name(Connection,719),FString(TEXT("Loading item...")));
    FWowItemTemplateInfo Item;Item.Entry=719;Item.Name=TEXT("Dwarven Test Gloves");Item.Class=4;Item.SubClass=2;
    Item.Quality=2;Item.InventoryType=10;Item.Armor=12;Item.Stats.Add({7,3});
    Connection->PacketHandler.ItemTemplates.Add(719,Item);
    TestEqual(TEXT("Late server data replaces placeholder"),FWowItemPresentation::Name(Connection,719),Item.Name);
    const auto Lines=FWowItemPresentation::Lines(Connection,719);
    TestEqual(TEXT("Reward uses quality color"),Lines[0].Color,FWowInventoryManager::GetQualityColor(2));
    TestTrue(TEXT("Equipment slot and armor type are visible"),Lines.ContainsByPredicate([](const auto& L){return L.Text.Contains(TEXT("Hands"))&&L.Text.Contains(TEXT("Leather"));}));
    TestTrue(TEXT("Reward stats are shown"),Lines.ContainsByPredicate([](const auto& L){return L.Text.Contains(TEXT("+3 Stamina"));}));
    FWowFrameManager Frames;Frames.Initialize(NewObject<UCanvasPanel>());
    auto Create=[&](const TCHAR* Name){FWowFrameDef Def;Def.Name=Name;Def.Parent=TEXT("UIParent");Def.Width=384;Def.Height=512;Def.bHidden=true;return Frames.CreateFrame(Def);};
    const auto Character=Create(TEXT("CharacterFrame")),Spellbook=Create(TEXT("SpellBookFrame")),Vendor=Create(TEXT("MerchantFrame"));
    Frames.SetFrameVisible(Character,true);Frames.SetFrameVisible(Vendor,true);Frames.SetFrameVisible(Spellbook,true);
    TestTrue(TEXT("Character stays open alongside vendor and spellbook"),Frames.IsFrameVisible(Character));
    TestTrue(TEXT("Vendor remains open"),Frames.IsFrameVisible(Vendor));
    Frames.PlaceDockPanel(Character,FVector2D(12,80),.75f);
    const auto* Def=Frames.GetFrameDef(Character);
    TestEqual(TEXT("Moved panel retains an explicit anchor"),Def->Anchors[0].OffsetX,12.0f);
    FWowEventSystem Events;Frames.SetEventSystem(&Events);Events.SetFrameManager(&Frames);
    auto Child=[&](const TCHAR* Name,const TCHAR* Parent)
    {
        FWowFrameDef D;D.Name=Name;D.Parent=Parent;D.Width=100;D.Height=28;
        return Frames.CreateFrame(D);
    };
    const auto General=Child(TEXT("ChatFrame1"),TEXT("UIParent"));
    const auto Combat=Child(TEXT("ChatFrame2"),TEXT("UIParent"));
    const auto CombatTab=Child(TEXT("ChatFrame2Tab"),TEXT("UIParent"));
    Frames.ConfigureChatTab(1,TEXT("General"),true);
    Frames.ConfigureChatTab(2,TEXT("Combat Log"),true);
    Frames.SetActiveChatTab(1);
    TestFalse(TEXT("Combat Log starts hidden behind General"),Frames.IsFrameVisible(Combat));
    Frames.DispatchClick(CombatTab,TEXT("LeftButton"),false);
    TestTrue(TEXT("Combat Log click selects its message pane"),Frames.IsFrameVisible(Combat));
    TestFalse(TEXT("General pane hides without hiding Combat Log"),Frames.IsFrameVisible(General));
    const auto Paper=Child(TEXT("PaperDollFrame"),TEXT("CharacterFrame"));
    const auto Reputation=Child(TEXT("ReputationFrame"),TEXT("CharacterFrame"));
    const auto Skills=Child(TEXT("SkillFrame"),TEXT("CharacterFrame"));
    const auto RepTab=Child(TEXT("CharacterFrameTab2"),TEXT("CharacterFrame"));
    const auto SkillTab=Child(TEXT("CharacterFrameTab3"),TEXT("CharacterFrame"));
    Frames.DispatchClick(RepTab,TEXT("LeftButton"),false);
    TestTrue(TEXT("Reputation tab opens its page"),Frames.IsFrameVisible(Reputation));
    TestFalse(TEXT("Paper doll hides on Reputation page"),Frames.IsFrameVisible(Paper));
    Frames.DispatchClick(SkillTab,TEXT("LeftButton"),false);
    TestTrue(TEXT("Skills tab opens its page"),Frames.IsFrameVisible(Skills));
    TestFalse(TEXT("Reputation page closes on Skills tab"),Frames.IsFrameVisible(Reputation));
    Frames.SetEventSystem(nullptr);
    return true;
}
#endif
