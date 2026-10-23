#include "SWowTaxiMap.h"
#include "WowWindowStyle.h"
#include "WowConnectionManager.h"
#include "WowOpcodes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Styling/AppStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTaxiMap, Log, All);

void SWowTaxiMap::Construct(const FArguments& InArgs)
{
    ConnectionManager = InArgs._ConnectionManager;

    ChildSlot
    [
        SAssignNew(Background, SWowWindowFrame)
        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f))
        .Visibility(EVisibility::Collapsed)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(16.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Flight Master")))
                    .ColorAndOpacity(FLinearColor::White)
                    .Font(FAppStyle::GetFontStyle("TitleFont"))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SAssignNew(CloseButton, SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                    .Text(FText::FromString(TEXT("Close")))
                    .OnClicked(this, &SWowTaxiMap::OnCloseClicked)
                ]
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SAssignNew(NodesCanvas, SCanvas)
            ]
        ]
    ];

    SetCanTick(false);
}

void SWowTaxiMap::ShowTaxiMap(const FWowTaxiData& TaxiData)
{
    CurrentTaxiData = TaxiData;
    NodeWidgets.Empty();
    NodesCanvas->ClearChildren();

    UE_LOG(LogWowTaxiMap, Log, TEXT("Showing taxi map with %d total nodes, %d known"),
        TaxiData.AllNodes.Num(), TaxiData.KnownNodes.Num());

    // Create widgets for each taxi node
    for (const FWowTaxiNode& Node : TaxiData.AllNodes)
    {
        const bool bKnown = TaxiData.IsNodeKnown(Node.Id);

        // Only show nodes on the current map for simplicity
        // In a full implementation, you'd want to show a world map or map selection
        if (Node.MapId != 0) // TODO: Get current map ID from player entity
        {
            continue;
        }

        FTaxiNodeWidget NodeWidget;
        NodeWidget.NodeId = Node.Id;
        NodeWidget.NodeData = Node;
        NodeWidget.bKnown = bKnown;

        const FVector2D CanvasPos = WorldToCanvas(Node.X, Node.Y);

        TSharedRef<SWidget> Widget = CreateNodeWidget(Node, bKnown);

        NodesCanvas->AddSlot()
            .Position(CanvasPos)
            .Size(FVector2D(80, 60))
            [
                Widget
            ];

        NodeWidgets.Add(NodeWidget);
    }

    Background->SetVisibility(EVisibility::Visible);
    FSlateApplication::Get().SetKeyboardFocus(AsShared());
}

void SWowTaxiMap::CloseTaxiMap()
{
    CurrentTaxiData.Clear();
    NodeWidgets.Empty();
    NodesCanvas->ClearChildren();
    Background->SetVisibility(EVisibility::Collapsed);
}

TSharedRef<SWidget> SWowTaxiMap::CreateNodeWidget(const FWowTaxiNode& Node, bool bKnown)
{
    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(bKnown ? FLinearColor(0.0f, 0.4f, 0.0f, 0.8f) : FLinearColor(0.4f, 0.4f, 0.4f, 0.8f))
        .Padding(4.0f)
        .ToolTipText(this, &SWowTaxiMap::GetNodeTooltipText, Node.Id)
        [
            SNew(SButton)
            .ButtonColorAndOpacity(this, &SWowTaxiMap::GetNodeButtonColor, Node.Id)
            .OnClicked(this, &SWowTaxiMap::OnNodeClicked, Node.Id)
            .IsEnabled(bKnown && Node.Id != CurrentTaxiData.CurrentNodeId)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Node.Name.Left(8))) // Truncate long names
                .ColorAndOpacity(FLinearColor::White)
                .Font(FAppStyle::GetFontStyle("SmallFont"))
                .Justification(ETextJustify::Center)
            ]
        ];
}

FReply SWowTaxiMap::OnNodeClicked(uint32 NodeId)
{
    if (!ConnectionManager.IsValid() || NodeId == CurrentTaxiData.CurrentNodeId)
    {
        return FReply::Handled();
    }

    const FWowTaxiNode* DestNode = CurrentTaxiData.FindNode(NodeId);
    if (!DestNode || !CurrentTaxiData.IsNodeKnown(NodeId))
    {
        UE_LOG(LogWowTaxiMap, Warning, TEXT("Cannot travel to unknown or invalid node %d"), NodeId);
        return FReply::Handled();
    }

    UE_LOG(LogWowTaxiMap, Log, TEXT("Requesting taxi to node %d (%s)"), NodeId, *DestNode->Name);

    // Send CMSG_ACTIVATETAXI
    TArray<uint8> Data;
    Data.SetNum(16); // uint64 + uint32 + uint32

    // Pack the data: npcGuid(8), sourceNode(4), destNode(4)
    uint64 NpcGuid = CurrentTaxiData.NpcGuid;
    uint32 SourceNode = CurrentTaxiData.CurrentNodeId;
    uint32 DestNode32 = NodeId;

    FMemory::Memcpy(Data.GetData(), &NpcGuid, 8);
    FMemory::Memcpy(Data.GetData() + 8, &SourceNode, 4);
    FMemory::Memcpy(Data.GetData() + 12, &DestNode32, 4);

    ConnectionManager->SendRawPacket(WowOpcode::CMSG_ACTIVATETAXI, Data);

    // Close the map - server will handle the flight
    CloseTaxiMap();

    return FReply::Handled();
}

FReply SWowTaxiMap::OnCloseClicked()
{
    CloseTaxiMap();
    return FReply::Handled();
}

FReply SWowTaxiMap::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape)
    {
        CloseTaxiMap();
        return FReply::Handled();
    }

    return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FVector2D SWowTaxiMap::WorldToCanvas(float X, float Y) const
{
    // Simple conversion from WoW world coordinates to canvas position
    // This is a basic implementation - a real taxi map would use proper coordinate transformation
    // based on the specific map and its bounds

    // For WoW, coordinates are typically in the range of -10000 to 10000
    // Convert to 0-1 range, then to canvas size
    const float NormalizedX = (X + 17066.0f) / 34133.0f; // Eastern Kingdoms rough bounds
    const float NormalizedY = (Y + 24533.0f) / 49067.0f;

    // Assume a 800x600 map area (will be scaled by canvas)
    return FVector2D(
        FMath::Clamp(NormalizedX * 800.0f, 50.0f, 750.0f),
        FMath::Clamp(NormalizedY * 600.0f, 50.0f, 550.0f)
    );
}

FSlateColor SWowTaxiMap::GetNodeButtonColor(uint32 NodeId) const
{
    if (NodeId == CurrentTaxiData.CurrentNodeId)
    {
        return FSlateColor(FLinearColor::Yellow); // Current location
    }
    else if (CurrentTaxiData.IsNodeKnown(NodeId))
    {
        return FSlateColor(FLinearColor::Green); // Known destination
    }
    else
    {
        return FSlateColor(FLinearColor::Gray); // Unknown
    }
}

FText SWowTaxiMap::GetNodeTooltipText(uint32 NodeId) const
{
    const FWowTaxiNode* Node = CurrentTaxiData.FindNode(NodeId);
    if (!Node)
    {
        return FText::GetEmpty();
    }

    if (NodeId == CurrentTaxiData.CurrentNodeId)
    {
        return FText::FromString(FString::Printf(TEXT("%s\n(Current Location)"), *Node->Name));
    }
    else if (CurrentTaxiData.IsNodeKnown(NodeId))
    {
        return FText::FromString(FString::Printf(TEXT("%s\nClick to travel here"), *Node->Name));
    }
    else
    {
        return FText::FromString(FString::Printf(TEXT("%s\n(Undiscovered)"), *Node->Name));
    }
}
