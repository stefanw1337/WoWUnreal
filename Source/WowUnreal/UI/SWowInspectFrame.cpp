#include "SWowInspectFrame.h"
#include "WowColor.h"
#include "WowTextCentering.h"
#include "WowWindowStyle.h"
#include "WowItemPresentation.h"
#include "WowRetainedSlateBrush.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowNetwork/Public/WowConnectionManager.h"
#include "WowNetwork/Public/WowPacketHandler.h"
#include "WowUpdateFields.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/TalentDbc.h"
#include "Formats/Dbc/TalentTabDbc.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Widgets/SBoxPanel.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WowInspect"

void SWowInspectFrame::Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
    UIManager = InArgs._UIManager;
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UIManager->GetFrameManager()->GetUIScale();
    ChildSlot
    [
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return FrameW * Scale; })
        .HeightOverride_Lambda([this]() { return FrameH * Scale; })
    ];
    SetVisibility(EVisibility::Collapsed);
}

void SWowInspectFrame::Open(uint64 InGuid, UTextureRenderTarget2D* ModelTarget)
{
    Guid = InGuid;
    bVisible = true;
    ModelBrush.Reset();
    if (ModelTarget)
    {
        ModelBrush = MakeShared<FWowRetainedSlateBrush>();
        // r301 (Claude-Perf): the paper-doll target is HDR with inverse coverage in alpha; drawn
        // raw, the model was a black silhouette. Same UI material as the character sheet.
        UObject* Resource = ModelTarget;
        if (UMaterial* Base = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_WowPaperDollUI.M_WowPaperDollUI")))
            if (UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, GetTransientPackage()))
            {
                Material->SetTextureParameterValue(TEXT("PortraitTexture"), ModelTarget);
                Resource = Material;
            }
        ModelBrush->SetResourceObject(Resource);
        ModelBrush->DrawAs = ESlateBrushDrawType::Image;
        ModelBrush->ImageSize = FVector2D(ModelTarget->SizeX, ModelTarget->SizeY);
    }
    // CMSG_INSPECT: the server answers with SMSG_INSPECT_TALENT (talents, glyphs, items with enchants).
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
    {
        TArray<uint8> Data;
        Data.Append(reinterpret_cast<const uint8*>(&InGuid), sizeof(InGuid));
        Connection->SendRawPacket(0x114, Data);
    }
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
        if (const FWowEntity* Entity = Connection->PacketHandler.EntityManager.Find(Guid))
            for (uint8 Slot = 0; Slot < 19; ++Slot)
                if (const uint32 Entry = Entity->GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2))
                    if (!Connection->PacketHandler.ItemTemplates.Contains(Entry))
                        Connection->PacketHandler.RequestItemTemplate(Entry);
    SetVisibility(EVisibility::SelfHitTestInvisible);
    Rebuild();
}

void SWowInspectFrame::Close()
{
    if (!bVisible) return;
    bVisible = false;
    Guid = 0;
    SetVisibility(EVisibility::Collapsed);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().CloseToolTip();
}

void SWowInspectFrame::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (!bVisible) return;
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection) { Close(); return; }
    if (CurrentTime >= NextRangeCheck)
    {
        NextRangeCheck = CurrentTime + .5;
        // InspectFrame_OnUpdate: closes when the player is gone or out of inspect range (28 yards).
        auto& Handler = Connection->PacketHandler;
        const FWowEntity* Other = Handler.EntityManager.Find(Guid);
        const FWowEntity* Me = Handler.EntityManager.GetLocalPlayer();
        if (!Other || (Me && FVector::Dist(Other->Movement.Position, Me->Movement.Position) > 28.f)) { Close(); return; }
    }
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale)) { Scale = NewScale; LastStateStamp = 0; }
    }
    if (ComputeStateStamp() != LastStateStamp) Rebuild();
    // Turning the model: a left press on the model area, then the cursor's horizontal movement.
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication& App = FSlateApplication::Get();
        const bool bLeftDown = App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
        const FVector2D Cursor = App.GetCursorPos();
        const TSharedPtr<SWidget> Area = ModelArea.Pin();
        if (bLeftDown && !bLeftWasDown)
            bRotatingModel = Area.IsValid() && Area->GetCachedGeometry().IsUnderLocation(Cursor);
        else if (bLeftDown && bRotatingModel && OnRotateModel && Cursor.X != LastRotateCursor.X)
            OnRotateModel(float(LastRotateCursor.X - Cursor.X) * .8f); // same direction as the character sheet drag (it was inverted, Stefan 10.10)
        if (!bLeftDown) bRotatingModel = false;
        bLeftWasDown = bLeftDown;
        LastRotateCursor = Cursor;
    }
}

uint32 SWowInspectFrame::ComputeStateStamp() const
{
    uint32 Stamp = uint32(Guid) ^ uint32(Scale * 1000.f);
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
    {
        // Only what this window shows: the inspected name and, per slot, the item and whether its
        // template has arrived. The global template/name counts changed whenever anything in the
        // world was queried, and every rebuild replaced the hovered slot: the window and its item
        // tooltip flickered (Stefan 10.10).
        const auto& Handler = Connection->PacketHandler;
        if (const FString* Name = Handler.PlayerNameCache.Find(Guid)) Stamp = HashCombine(Stamp, GetTypeHash(*Name));
        Stamp = HashCombine(Stamp, Handler.InspectData.Revision);
        if (const FWowEntity* Entity = const_cast<FWowEntityManager&>(Handler.EntityManager).Find(Guid))
            for (uint8 Slot = 0; Slot < 19; ++Slot)
            {
                const uint32 ItemEntry = Entity->GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2);
                Stamp = HashCombine(Stamp, ItemEntry);
                Stamp = HashCombine(Stamp, uint32(ItemEntry && Handler.ItemTemplates.Contains(ItemEntry)));
            }
    }
    return Stamp;
}

FSlateFontInfo SWowInspectFrame::Fnt(float WowPixels) const
{
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
}

void SWowInspectFrame::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y) * Scale).Size(FVector2D(W, H) * Scale)[Widget];
}

const FSlateBrush* SWowInspectFrame::LoadBrush(const FString& TexturePath) const
{
    if (TexturePath.IsEmpty()) return nullptr;
    if (const TSharedPtr<FWowRetainedSlateBrush>* Existing = TextureBrushes.Find(TexturePath)) return Existing->Get();
    const double Now = FPlatformTime::Seconds();
    if (const double* Failed = FailedTextureTimes.Find(TexturePath); Failed && Now - *Failed < 2.0) return nullptr;
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager() ? UIManager->GetFrameManager()->LoadUITexture(TexturePath) : nullptr;
    if (!Texture) { FailedTextureTimes.Add(TexturePath, Now); return nullptr; }
    TSharedPtr<FWowRetainedSlateBrush> Brush = MakeShared<FWowRetainedSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    TextureBrushes.Add(TexturePath, Brush);
    return Brush.Get();
}

TSharedRef<SWidget> SWowInspectFrame::Img(const FString& Path) const
{
    return SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, Path]() { return LoadBrush(Path); });
}

TSharedRef<SWidget> SWowInspectFrame::Label(const FText& Text, float WowPixels, FLinearColor Color) const
{
    return SNew(STextBlock).Text(Text).Font(Fnt(WowPixels)).ColorAndOpacity(Color).Justification(ETextJustify::Center)
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).Visibility(EVisibility::HitTestInvisible)
        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black);
}

TSharedRef<SWidget> SWowInspectFrame::SlotButton(uint8 EquipmentSlot, const TCHAR* EmptyArt, TArray<FWowItemTooltipLine>* OutLines) const
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowEntity* Entity = Connection ? Connection->PacketHandler.EntityManager.Find(Guid) : nullptr;
    const auto& Inspect = Connection ? &Connection->PacketHandler.InspectData : nullptr;
    const bool bHaveInspect = Inspect && Inspect->Guid == Guid;
    uint32 Entry = bHaveInspect && Inspect->ItemEntries[EquipmentSlot] ? Inspect->ItemEntries[EquipmentSlot] : 0;
    if (!Entry && Entity) Entry = Entity->GetField(PlayerField::VISIBLE_ITEM_BASE + EquipmentSlot * 2);
    // Enchant and gems (SMSG_INSPECT_TALENT enchant slots): green enchant line, gem lines, random stats.
    TArray<FWowItemTooltipLine> Extra;
    if (bHaveInspect && Inspect->ItemEnchants[EquipmentSlot].Num() == 12)
        for (int32 Slot = 0; Slot < 12; ++Slot)
            if (const uint16 EnchantId = Inspect->ItemEnchants[EquipmentSlot][Slot])
            {
                const FString Name = FDbcStore::Get().EnchantmentName(EnchantId);
                if (Name.IsEmpty() || Name.Contains(TEXT("$"))) continue;
                FWowItemTooltipLine Line;
                Line.Text = Slot >= 2 && Slot <= 4 ? Name : Slot == 1 ? Name + TEXT(" (temporary)") : Name;
                Line.Color = Slot >= 2 && Slot <= 4 ? FLinearColor::White : WowColor::FromWow(.12f, 1.f, 0.f);
                Extra.Add(Line);
            }
    const FWowItemTemplateInfo* Info = Entry ? FWowItemPresentation::Info(Connection, Entry) : nullptr;
    const FString Icon = Entry ? FWowItemPresentation::IconPath(Entry, Info ? Info->DisplayId : 0)
        : FString::Printf(TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-%s"), EmptyArt);
    // Hit-testable, so the frame can show the item's tooltip while it is hovered.
    TSharedRef<SBox> Box = SNew(SBox).Visibility(EVisibility::Visible)
        [
            SNew(SOverlay)
            + SOverlay::Slot()[ Img(Icon) ]
            // Quality border like the item buttons (UI-ActionButton-Border, ADD) for uncommon and better.
            + SOverlay::Slot()
            [
                SNew(SWowAddImage).Visibility(Info && Info->Quality >= 2 ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
                .Image_Lambda([this]() { return LoadBrush(TEXT("Interface\\Buttons\\UI-ActionButton-Border")); })
                .ColorAndOpacity(Info ? FWowItemPresentation::QualityColor(Connection, Entry) : FLinearColor::White)
            ]
        ];
    // The tooltip is drawn by the frame next to the slot (Slate tool tips did not show over the game:
    // Stefan 10.10, the inspected items could not be moused over). Enchants and gems go before
    // the sell price like the original tooltip.
    if (Entry && OutLines)
    {
        *OutLines = FWowItemPresentation::Lines(Connection, Entry);
        int32 InsertAt = OutLines->Num();
        if (InsertAt > 0 && (*OutLines)[InsertAt - 1].Text.StartsWith(TEXT("Sell Price"))) --InsertAt;
        OutLines->Insert(Extra, InsertAt);
    }
    return Box;
}

void SWowInspectFrame::Rebuild()
{
    if (!Root.IsValid()) return;
    LastStateStamp = ComputeStateStamp();
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowEntity* Entity = Connection ? Connection->PacketHandler.EntityManager.Find(Guid) : nullptr;
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);

    // Portrait (live 3D portrait like the unit frames) under the frame art.
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->UnitPortraitProvider)
        if (UMaterialInterface* Portrait = UIManager->GetFrameManager()->UnitPortraitProvider(Guid))
        {
            if (!PortraitBrush.IsValid()) PortraitBrush = MakeShared<FWowRetainedSlateBrush>();
            PortraitBrush->SetResourceObject(Portrait);
            PortraitBrush->DrawAs = ESlateBrushDrawType::Image;
            PortraitBrush->ImageSize = FVector2D(64, 64);
            Place(*Canvas, 7, 6, 60, 60, SNew(SImage).Image(PortraitBrush.Get()).Visibility(EVisibility::HitTestInvisible));
        }

    // InspectFrame art: the character frame's paper doll pages.
    Place(*Canvas, 0, 0, 256, 256, Img(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopLeft")));
    Place(*Canvas, 256, 0, 128, 256, Img(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopRight")));
    Place(*Canvas, 0, 256, 256, 256, Img(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-BottomLeft")));
    Place(*Canvas, 256, 256, 128, 256, Img(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-BottomRight")));

    // Name and "Level N Race Class".
    FString Name = TEXT("Unknown");
    FString LevelLine;
    if (Entity && Connection)
    {
        if (const FString* Cached = Connection->PacketHandler.PlayerNameCache.Find(Guid)) Name = *Cached;
        static const TCHAR* ClassNames[] = { TEXT(""), TEXT("Warrior"), TEXT("Paladin"), TEXT("Hunter"), TEXT("Rogue"), TEXT("Priest"),
            TEXT("Death Knight"), TEXT("Shaman"), TEXT("Mage"), TEXT("Warlock"), TEXT(""), TEXT("Druid") };
        const FWowUnitEntity* Unit = static_cast<const FWowUnitEntity*>(Entity);
        const uint8 ClassId = Unit->GetClassId();
        FString Race;
        if (const FChrRacesDbcEntry* RaceEntry = FDbcStore::Get().ChrRaces().GetById(Unit->GetRaceId())) Race = RaceEntry->Name;
        LevelLine = FString::Printf(TEXT("Level %d %s %s"), int32(Entity->GetField(UnitField::LEVEL)), *Race,
            ClassId < UE_ARRAY_COUNT(ClassNames) ? ClassNames[ClassId] : TEXT(""));
    }
    Place(*Canvas, 72, 12, 240, 18, WowTitleBox(Label(FText::FromString(Name), 12, WowColor::FromWow(1.f, .82f, 0.f)), Fnt(12)));
    Place(*Canvas, 72, 38, 240, 16, Label(FText::FromString(LevelLine), 10, FLinearColor::White));

    // Model (CharacterModelFrame area); drag with the left button to turn it (Tick follows the
    // button and the cursor, so the game's mouse handling cannot take the drag away - Stefan 10.10).
    ModelArea.Reset();
    if (ModelBrush.IsValid())
    {
        TSharedRef<SBorder> Area = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
            [ SNew(SImage).Image(ModelBrush.Get()) ];
        ModelArea = Area;
        Place(*Canvas, 66, 76, 232, 300, Area);
    }

    // Equipment slots: left column, right column, weapons (PaperDollFrame positions).
    struct FSlotDef { uint8 Slot; const TCHAR* Art; };
    static const FSlotDef Left[] = { {0,TEXT("Head")},{1,TEXT("Neck")},{2,TEXT("Shoulder")},{14,TEXT("Chest")},{4,TEXT("Chest")},{3,TEXT("Shirt")},{18,TEXT("Tabard")},{8,TEXT("Wrists")} };
    static const FSlotDef Right[] = { {9,TEXT("Hands")},{5,TEXT("Waist")},{6,TEXT("Legs")},{7,TEXT("Feet")},{10,TEXT("Finger")},{11,TEXT("Finger")},{12,TEXT("Trinket")},{13,TEXT("Trinket")} };
    static const FSlotDef Weapons[] = { {15,TEXT("MainHand")},{16,TEXT("SecondaryHand")},{17,TEXT("Ranged")} };
    struct FTip { TWeakPtr<SWidget> Button; float X; float Y; bool bLeft; TArray<FWowItemTooltipLine> Lines; };
    TArray<FTip> Tips;
    auto PlaceSlot = [this, &Canvas, &Tips](float X, float Y, uint8 EquipmentSlot, const TCHAR* Art, bool bTipLeft)
    {
        TArray<FWowItemTooltipLine> Lines;
        const TSharedRef<SWidget> Button = SlotButton(EquipmentSlot, Art, &Lines);
        Place(*Canvas, X, Y, 37, 37, Button);
        if (!Lines.IsEmpty()) Tips.Add({Button, X, Y, bTipLeft, MoveTemp(Lines)});
    };
    for (int32 Index = 0; Index < 8; ++Index)
    {
        PlaceSlot(21, 74 + Index * 41, Left[Index].Slot, Left[Index].Art, false);
        PlaceSlot(305, 74 + Index * 41, Right[Index].Slot, Right[Index].Art, true);
    }
    for (int32 Index = 0; Index < 3; ++Index)
        PlaceSlot(122 + Index * 42, 386, Weapons[Index].Slot, Weapons[Index].Art, false);

    // Talents (InspectTalentFrame summary): points per tree of the active spec, with the tree icons.
    if (Connection && Connection->PacketHandler.InspectData.Guid == Guid && Entity)
    {
        const auto& Inspect = Connection->PacketHandler.InspectData;
        const uint8 ClassId = static_cast<const FWowUnitEntity*>(Entity)->GetClassId();
        TArray<const FTalentTabDbcEntry*> Tabs;
        for (const FTalentTabDbcEntry& Tab : FDbcStore::Get().TalentTabs().GetAll())
            if (ClassId && (Tab.ClassMask & (1u << (ClassId - 1))) && Tab.PetTalentMask == 0) Tabs.Add(&Tab);
        Tabs.Sort([](const FTalentTabDbcEntry& A, const FTalentTabDbcEntry& B) { return A.TabPage < B.TabPage; });
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible);
        FString Points;
        for (const FTalentTabDbcEntry* Tab : Tabs)
        {
            int32 Spent = 0;
            for (const TPair<uint32, uint8>& Talent : Inspect.TalentRanks)
                if (const FTalentDbcEntry* Info = FDbcStore::Get().Talents().GetById(Talent.Key); Info && Info->TalentTab == Tab->TalentTabID)
                    Spent += Talent.Value;
            Points += Points.IsEmpty() ? FString::FromInt(Spent) : TEXT("/") + FString::FromInt(Spent);
            FString IconPath;
            if (const FSpellIconDbcEntry* Icon = FDbcStore::Get().SpellIcons().GetById(Tab->SpellIcon)) IconPath = Icon->TexturePath;
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin((Row->NumSlots() ? 6 : 0) * Scale, 0, 2 * Scale, 0))
            [ SNew(SBox).WidthOverride(18 * Scale).HeightOverride(18 * Scale)[ Img(IconPath) ] ];
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s %d"), *Tab->Name, Spent))).Font(Fnt(10))
              .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ];
        }
        // Inside the frame, at the foot of the model (Stefan 10.10: the row hung below the window).
        // Scaled down to fit between the two slot columns: long tree names ("Protection 53
        // Retribution 18") ran into the gear icons (Stefan 10.10).
        Place(*Canvas, 66, 354, 232, 20, SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            .HAlign(HAlign_Center).VAlign(VAlign_Center)[ Row ]);
        Place(*Canvas, 72, 54, 240, 14, Label(FText::FromString(FString::Printf(TEXT("Talents: %s"), *Points)), 10, WowColor::FromWow(1.f, .82f, 0.f)));
    }

    // Item tooltips last, so they draw over the slots and the model.
    for (const FTip& Tip : Tips)
    {
        TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
        for (int32 Index = 0; Index < Tip.Lines.Num(); ++Index)
        {
            const FWowItemTooltipLine& Line = Tip.Lines[Index];
            const FSlateFontInfo LineFont = Fnt(Index == 0 ? 13.f : 11.f);
            if (Line.Right.IsEmpty())
                Lines->AddSlot().AutoHeight()
                [ SNew(STextBlock).Text(FText::FromString(Line.Text)).Font(LineFont).ColorAndOpacity(Line.Color).AutoWrapText(true) ];
            else
                Lines->AddSlot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f)[ SNew(STextBlock).Text(FText::FromString(Line.Text)).Font(LineFont).ColorAndOpacity(Line.Color) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(12 * Scale, 0, 0, 0))
                    [ SNew(STextBlock).Text(FText::FromString(Line.Right)).Font(LineFont).ColorAndOpacity(Line.RightColor) ]
                ];
        }
        const TWeakPtr<SWidget> Hover = Tip.Button;
        Place(*Canvas, Tip.bLeft ? Tip.X - 254 : Tip.X + 42, Tip.Y, 250, 420,
            SNew(SBox).HAlign(Tip.bLeft ? HAlign_Right : HAlign_Left).VAlign(VAlign_Top)
            .Visibility_Lambda([Hover]() { const TSharedPtr<SWidget> W = Hover.Pin(); return W.IsValid() && W->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [ SNew(SWowTooltipFrame).Padding(FMargin(8 * Scale, 6 * Scale))[ Lines ] ]);
    }

    // Close button (InspectFrameCloseButton, TOPRIGHT -30,-8).
    const FSlateBrush* CloseUp = LoadBrush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"));
    TSharedPtr<FButtonStyle> CloseStyle = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
    if (CloseUp) { CloseStyle->SetNormal(*CloseUp); CloseStyle->SetHovered(*CloseUp); CloseStyle->SetPressed(*CloseUp); }
    Place(*Canvas, 322, 8, 32, 32, SNew(SButton).ButtonStyle(CloseStyle.Get()).ContentPadding(0)
        .OnClicked_Lambda([this, CloseStyle]() { Close(); return FReply::Handled(); }));

    Root->SetContent(
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBox).Padding(FMargin(0, 0, 35 * Scale, 75 * Scale))
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
                .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
            ]
        ]
        + SOverlay::Slot()[ Canvas ]);
}

#undef LOCTEXT_NAMESPACE
