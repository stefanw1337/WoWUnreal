#include "WowFrameManager.h"
#include "WowFrameLayout.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

namespace
{
    FString GroupForName(const FString& Name)
    {
        if(Name.StartsWith(TEXT("ChatFrame"))||Name.StartsWith(TEXT("ChatTab"))||Name.StartsWith(TEXT("ChatMenu"))
            ||Name.StartsWith(TEXT("GeneralDock"))||Name.StartsWith(TEXT("CombatLogFilter"))||Name==TEXT("FriendsMicroButton")||Name==TEXT("WowChatVisibilityButton"))return TEXT("ChatFrame1");
        if(Name.StartsWith(TEXT("SocialNative")))return TEXT("FriendsFrame");
        return FString();
    }
}
// Unit frames and the minimap cluster are drawn with ApplyElementScales' render transform (their
// size options); a Ctrl-drag position is applied there too, not by UpdateUserFrameLayouts. Some of
// them are children of UIParent's canvas, not of the root canvas (Stefan: they could not be moved).
static bool IsElementPlacedFrameName(const FString& Name)
{
    static const TSet<FString> Names = { TEXT("PlayerFrame"), TEXT("TargetFrame"), TEXT("FocusFrame"),
        TEXT("PartyMemberFrame1"), TEXT("PartyMemberFrame2"), TEXT("PartyMemberFrame3"), TEXT("PartyMemberFrame4"),
        TEXT("MinimapCluster") };
    return Names.Contains(Name);
}
int64 FWowFrameManager::GetFrameLayoutRoot(int64 Handle) const
{
    for(int32 Guard=0;Guard<64;++Guard)
    {
        const auto* E=Frames.Find(Handle);if(!E)return -1;
        const FString Group=GroupForName(E->Def.Name);
        if(!Group.IsEmpty())return FindFrame(Group);
        const int64 Parent=GetParentHandle(Handle);
        if(Parent<0||Parent==FindFrame(TEXT("UIParent")))return Handle;
        Handle=Parent;
    }
    return -1;
}

int64 FWowFrameManager::GetMovableFrameAtScreenPosition(float X,float Y) const
{
    const auto Stack=GetFramesAtScreenPosition(X,Y);
    const auto Root=GetFrameDef(FindFrame(TEXT("UIParent")));
    if(!Root)return -1;
    for(int64 Hit:Stack)
    {
        int64 Candidate=GetFrameLayoutRoot(Hit);
        for(int32 Guard=0;Guard<64;++Guard)
        {
            const auto* E=Frames.Find(Candidate);if(!E||!E->Widget.IsValid())break;
            if(IsElementPlacedFrameName(E->Def.Name))return Candidate;
            // r419 (Stefan 18.10: "I cannot ctrl-drag the objective tracker anywhere"; log: "no movable
            // frame", top WatchFrameHeader): a UIParent child in its own strata layer (WatchFrame is LOW)
            // is not a direct child of the root canvas, so the walk passed it and stopped at UIParent.
            // A top-level frame of UIParent counts as a window too.
            if(E->Widget->GetParent()==RootCanvas.Get()||GetParentHandle(Candidate)==FindFrame(TEXT("UIParent")))
            {
                const auto& D=E->Def;
                // Move a complete window/bar, never a button, backdrop or full-screen root.
                if(D.Name!=TEXT("UIParent")&&!D.Name.IsEmpty()&&!D.Name.StartsWith(TEXT("WowFrameStack"))
                    &&D.Type!=EWowFrameType::GameTooltip && !D.Name.Contains(TEXT("Tooltip"))
                    &&!(D.Width>=Root->Width*.9f&&D.Height>=Root->Height*.9f))return Candidate;
                break;
            }
            Candidate=GetParentHandle(Candidate);if(Candidate<0)break;
        }
    }
    return -1;
}

void FWowFrameManager::UpdateUserFrameLayouts()
{
    if(!RootCanvas.IsValid())return;
    const auto* Root=GetFrameDef(FindFrame(TEXT("UIParent")));if(!Root)return;
    const FVector2D View(Root->Width*UIScale,Root->Height*UIScale);
    const auto& Saved=FWowFrameLayout::Positions();
    for(auto W:UserPositionedRegions)if(W.IsValid())W->SetRenderTranslation(FVector2D::ZeroVector);
    UserPositionedRegions.Reset();
    for(auto It=UserPositionedFrames.CreateIterator();It;++It)
    {
        auto* E=Frames.Find(*It);
        if(!E||!E->Widget.IsValid()){It.RemoveCurrent();continue;}
        const auto* Group=GetFrameDef(GetFrameLayoutRoot(*It));
        if(!Group||!Saved.Contains(Group->Name))
        {E->Widget->SetRenderTranslation(FVector2D::ZeroVector);It.RemoveCurrent();}
    }
    for(const auto& Pair:Saved)
    {
        if(IsElementPlacedFrameName(Pair.Key))continue; // ApplyElementScales places these
        const int64 H=FindFrame(Pair.Key);auto* E=Frames.Find(H);
        if(!E||!E->Widget.IsValid()||E->Widget->GetParent()!=RootCanvas.Get())continue;
        auto* Slot=Cast<UCanvasPanelSlot>(E->Widget->Slot);if(!Slot)continue;
        const FVector2D Size=Slot->GetSize()*E->Widget->GetRenderTransform().Scale;
        const FVector2D P=FWowFrameLayout::ClampPosition(Pair.Value,Size,View);
        E->Widget->SetRenderTranslation(P-Slot->GetPosition());
        UserPositionedFrames.Add(H);
        const FVector2D Delta=P-Slot->GetPosition();
        // Some repaired stock controls are physically on the root canvas even
        // though they belong to a window in the logical FrameXML hierarchy.
        for(auto& Frame:Frames)
            if(Frame.Key!=H&&Frame.Value.Widget.IsValid()&&Frame.Value.Widget->GetParent()==RootCanvas.Get()
                &&GetFrameLayoutRoot(Frame.Key)==H)
            {Frame.Value.Widget->SetRenderTranslation(Delta);UserPositionedFrames.Add(Frame.Key);}
        auto MoveRegion=[&](const FString& Name,UWidget* Widget)
        {
            if(Widget&&Widget->GetParent()==RootCanvas.Get()&&GroupForName(Name)==Pair.Key)
            {Widget->SetRenderTranslation(Delta);UserPositionedRegions.Add(Widget);}
        };
        for(const auto& Region:NamedTextureWidgets)MoveRegion(Region.Key,Region.Value.Get());
        for(const auto& Region:NamedFontStringWidgets)MoveRegion(Region.Key,Region.Value.Get());
    }
    ApplyElementScales();
}
