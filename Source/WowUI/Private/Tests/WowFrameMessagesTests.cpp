#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "WowUiMessageScript.h"
#include "WowPacketHandler.h"
#include "WowOpcodes.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowSeparatedMessagesTest,"WowUnreal.UI.FrameMessages.SeparateQueuesAndLua",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowSeparatedMessagesTest::RunTest(const FString&)
{
    FWowLuaVM VM;if(!VM.Initialize())return false;
    FWowFrameManager Frames;FWowEventSystem Events;
    Events.SetLuaVM(&VM);Events.SetFrameManager(&Frames);Frames.SetEventSystem(&Events);
    FWowLuaContext Context;Context.FrameManager=&Frames;Context.EventSystem=&Events;
    WowLuaApi::SetContext(VM.GetState(),&Context);
    ON_SCOPE_EXIT{Frames.SetEventSystem(nullptr);WowLuaApi::ClearContext(VM.GetState());};
    Frames.Initialize(NewObject<UCanvasPanel>());
    TestTrue(TEXT("Stock handler fixture"),VM.ExecuteString(TEXT(R"LUA(
        UIErrorsFrame=CreateFrame("MessageFrame","UIErrorsFrame",UIParent)
        UIErrorsFrame:RegisterEvent("TEST_OTHER_EVENT")
        UIErrorsFrame:SetScript("OnEvent",function(self,event,value)
            if event=="TEST_OTHER_EVENT" then self.testValue=value end
        end)
    )LUA")));
    TestTrue(TEXT("Typed inventory event fixture"),VM.ExecuteString(TEXT(R"LUA(
        local f=CreateFrame("Frame","InventoryEventTypeTest",UIParent)
        f:RegisterEvent("BAG_UPDATE");f:RegisterEvent("PLAYER_EQUIPMENT_CHANGED");f:RegisterEvent("DELETE_ITEM_CONFIRM")
        f:SetScript("OnEvent",function(self,event,a,b)
            if event=="DELETE_ITEM_CONFIRM" then self.quality=b else self.slot=a end
        end)
    )LUA")));
    Events.FireEvent(TEXT("BAG_UPDATE"),{TEXT("3")});
    TestTrue(TEXT("Bag index is numeric for stock table lookups"),VM.ExecuteString(TEXT("assert(type(InventoryEventTypeTest.slot)=='number' and InventoryEventTypeTest.slot==3)")));
    Events.FireEvent(TEXT("PLAYER_EQUIPMENT_CHANGED"),{TEXT("16")});
    TestTrue(TEXT("Equipment slot compares numerically"),VM.ExecuteString(TEXT("assert(InventoryEventTypeTest.slot==16)")));
    Events.FireEvent(TEXT("DELETE_ITEM_CONFIRM"),{TEXT("Rare axe"),TEXT("3")});
    TestTrue(TEXT("Delete quality comparison can select DELETE dialog"),VM.ExecuteString(TEXT("assert(InventoryEventTypeTest.quality>=3)")));
    TestTrue(TEXT("Installs addon-visible frames"),VM.ExecuteString(WowUiMessageScript()));
    Events.FireEvent(TEXT("TEST_OTHER_EVENT"),{TEXT("preserved")});
    TestTrue(TEXT("Unrelated stock event handlers preserved"),VM.ExecuteString(TEXT("assert(UIErrorsFrame.testValue=='preserved')")));
    const int64 Error=Frames.FindFrame(TEXT("UIErrorsFrame")),Info=Frames.FindFrame(TEXT("QuestProgressFrame"));
    if(!TestTrue(TEXT("Separate named frames exist"),Error>=0 && Info>=0 && Error!=Info))return false;
    Events.FireEvent(TEXT("UI_INFO_MESSAGE"),{TEXT("Wolf hunt: 3/5")});
    for(int32 I=0;I<100;++I)Events.FireEvent(TEXT("UI_ERROR_MESSAGE"),{TEXT("Not ready yet.")});
    TestEqual(TEXT("Repeated errors coalesce"),Frames.GetScrollingMessageCount(Error),1);
    TestEqual(TEXT("Error spam does not evict objective progress"),Frames.GetScrollingMessageCount(Info),1);
    for(int32 I=0;I<10;++I)Events.FireEvent(TEXT("UI_ERROR_MESSAGE"),{FString::Printf(TEXT("Error %d"),I)});
    TestEqual(TEXT("Error queue stays bounded"),Frames.GetScrollingMessageCount(Error),3);
    const double Now=FPlatformTime::Seconds();
    Frames.TickTransientMessages(Now+4.1);
    TestEqual(TEXT("Errors expire independently"),Frames.GetScrollingMessageCount(Error),0);
    TestEqual(TEXT("Quest progress remains readable longer"),Frames.GetScrollingMessageCount(Info),1);
    Frames.TickTransientMessages(Now+7.1);
    TestEqual(TEXT("Quest messages expire without chat hover revival"),Frames.GetScrollingMessageCount(Info),0);
    TestTrue(TEXT("Addon can move, retime, add and clear messages"),VM.ExecuteString(TEXT(R"LUA(
        assert(UIErrorsFrame:GetName() == "UIErrorsFrame")
        assert(QuestProgressFrame:GetParent() == UIParent)
        QuestProgressFrame:ClearAllPoints()
        QuestProgressFrame:SetPoint("CENTER",UIParent,"CENTER",0,20)
        QuestProgressFrame:SetTimeVisible(0)
        QuestProgressFrame:SetFadeDuration(0)
        QuestProgressFrame:AddMessage("Addon objective",1,1,0)
        UIErrorsFrame:Clear()
    )LUA")));
    Frames.TickTransientMessages(FPlatformTime::Seconds()+.1);
    TestEqual(TEXT("Lua timing controls have effect"),Frames.GetScrollingMessageCount(Info),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowFrameStackTest,"WowUnreal.UI.FrameMessages.StackVisibilityAndOrdering",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowFrameStackTest::RunTest(const FString&)
{
    for(float Scale:{1.f,1.40625f,2.f})
    {
        FWowFrameManager Frames;Frames.Initialize(NewObject<UCanvasPanel>());Frames.SetUIScale(Scale);
        FWowFrameDef Parent;Parent.Name=TEXT("StackParent");Parent.Parent=TEXT("UIParent");
        Parent.Width=200;Parent.Height=200;Parent.Strata=EWowFrameStrata::HIGH;
        FWowAnchor A;A.Point=EWowAnchorPoint::TOPLEFT;A.RelativeTo=Parent.Parent;A.RelativePoint=A.Point;Parent.Anchors.Add(A);
        const int64 P=Frames.CreateFrame(Parent);
        FWowFrameDef Child=Parent;Child.Name=TEXT("StackChild");Child.Parent=Parent.Name;Child.Anchors[0].RelativeTo=Parent.Name;
        Child.Width=100;Child.Height=100;Child.FrameLevel=1;Child.bEnableMouse=true;
        const int64 C=Frames.CreateFrame(Child);
        auto Stack=Frames.GetFramesAtScreenPosition(50,50);
        TestTrue(TEXT("Non-interactive parent is still inspectable"),Stack.Contains(P));
        TestTrue(TEXT("Child is first above its parent"),!Stack.IsEmpty()&&Stack[0]==C);
        Frames.SetFrameVisible(P,false);
        Stack=Frames.GetFramesAtScreenPosition(50,50);
        TestFalse(TEXT("Hidden parent's children excluded"),Stack.Contains(C));
        Frames.ToggleFrameStack();TestTrue(TEXT("Inspector enabled"),Frames.IsFrameStackVisible());
        Frames.ToggleFrameStack();TestFalse(TEXT("Inspector closes without altering inspected UI"),Frames.IsFrameStackVisible());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowPacketMessagesTest,"WowUnreal.UI.FrameMessages.ParsedServerMessages",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowPacketMessagesTest::RunTest(const FString&)
{
    FWowPacketHandler Handler;TArray<FString> Errors,Info;
    Handler.OnUiErrorMessage.AddLambda([&](const FString& M){Errors.Add(M);});
    Handler.OnUiInfoMessage.AddLambda([&](const FString& M){Info.Add(M);});
    Handler.HandlePacket(WowOpcode::SMSG_INVENTORY_CHANGE_FAILURE,{0});
    TestEqual(TEXT("Success is not an error"),Errors.Num(),0);
    Handler.HandlePacket(WowOpcode::SMSG_INVENTORY_CHANGE_FAILURE,{50});
    TestEqual(TEXT("WotLK inventory-full code gets readable text"),Errors.Last(),FString(TEXT("Inventory is full.")));
    FWowQuestLogEntry Quest;Quest.QuestId=42;Quest.Title=TEXT("Wolf hunt");
    FWowQuestObjective Obj;Obj.CreatureOrGOId=99;Obj.Text=TEXT("Wolves defeated");Obj.Required=5;Quest.Objectives.Add(Obj);Handler.QuestLog.Add(Quest);
    TArray<uint8> Packet;auto Put=[&](uint32 V){Packet.Append(reinterpret_cast<uint8*>(&V),4);};
    Put(42);Put(99);Put(3);Put(5);Put(0);Put(0);
    Handler.HandlePacket(WowOpcode::SMSG_QUEST_UPDATE_ADD_KILL,Packet);
    TestEqual(TEXT("Objective progress carries its name and counts"),Info.Last(),FString(TEXT("Wolves defeated: 3/5")));
    Packet.Reset();Put(42);Handler.HandlePacket(WowOpcode::SMSG_QUEST_UPDATE_COMPLETE,Packet);
    TestTrue(TEXT("Completion is an info message with quest name"),Info.Last().Contains(TEXT("Wolf hunt")));
    TestEqual(TEXT("Progress never becomes an error"),Errors.Num(),1);
    return true;
}
#endif
