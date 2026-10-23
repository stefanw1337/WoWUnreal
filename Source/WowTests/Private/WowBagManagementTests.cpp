#include "Misc/AutomationTest.h"
#include "WowConnectionManager.h"
#include "WowItemDestroyPolicy.h"
#include "WowUpdateFields.h"

namespace
{
void BagGuidField(FWowEntity& E, uint16 Field, uint64 Guid)
{ E.SetField(Field, uint32(Guid)); E.SetField(Field + 1, uint32(Guid >> 32)); }
FWowEntity& BagEntity(FWowPacketHandler& H, uint64 Guid, uint32 Type, uint32 Entry = 0)
{
    auto& E = H.EntityManager.PromoteToTyped(Guid, Type); E.SetField(ObjectField::TYPE, Type);
    E.Entry = Entry; E.SetField(ObjectField::ENTRY, Entry); return E;
}
// Player 1 with one item (guid 2, entry 100) in backpack slot 1.
UWowConnectionManager* MakeBagFixture(uint32 Quality, uint32 Bonding, uint32 ItemFlags = 0)
{
    auto* C = NewObject<UWowConnectionManager>(); auto& H = C->PacketHandler; H.EntityManager.LocalPlayerGuid = 1;
    auto& P = BagEntity(H, 1, WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
    auto& I = BagEntity(H, 2, WowTypeMask::OBJECT | WowTypeMask::ITEM, 100);
    BagGuidField(I, ItemField::OWNER, 1); I.SetField(ItemField::STACK_COUNT, 3); I.SetField(ItemField::FLAGS, ItemFlags);
    BagGuidField(P, PlayerField::PACK_SLOT_START, 2);
    auto& Info = H.ItemTemplates.FindOrAdd(100);
    Info.Entry = 100; Info.Name = TEXT("Test item"); Info.Quality = Quality; Info.Bonding = Bonding; Info.MaxStack = 20;
    return C;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowItemDestroyPolicyTest, "WowUnreal.Bags.DestroyPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWowItemDestroyPolicyTest::RunTest(const FString&)
{
    using P = FWowItemDestroyPolicy;
    TestEqual(TEXT("Grey: Yes/No"), int32(P::Get(0, 0, 0)), int32(EWowDestroyConfirm::Confirm));
    TestEqual(TEXT("White: Yes/No"), int32(P::Get(1, 1, 0)), int32(EWowDestroyConfirm::Confirm));
    TestEqual(TEXT("Quest item: Yes/No"), int32(P::Get(1, P::BondingQuest, 0)), int32(EWowDestroyConfirm::Confirm));
    TestEqual(TEXT("Green BoP: Yes/No"), int32(P::Get(2, 1, 0)), int32(EWowDestroyConfirm::Confirm));
    TestEqual(TEXT("Blue BoP: Yes/No"), int32(P::Get(3, 1, 1)), int32(EWowDestroyConfirm::Confirm));
    TestEqual(TEXT("Green BoE: typed"), int32(P::Get(2, 2, 0)), int32(EWowDestroyConfirm::TypeDelete));
    TestEqual(TEXT("White BoE: typed"), int32(P::Get(1, 2, 0)), int32(EWowDestroyConfirm::TypeDelete));
    TestEqual(TEXT("Soulbound green BoE: Yes/No"), int32(P::Get(2, 2, P::ItemFlagSoulbound)), int32(EWowDestroyConfirm::Confirm));
    TestEqual(TEXT("Epic: typed"), int32(P::Get(4, 1, 1)), int32(EWowDestroyConfirm::TypeDelete));
    TestEqual(TEXT("Legendary: typed"), int32(P::Get(5, 1, 1)), int32(EWowDestroyConfirm::TypeDelete));
    TestEqual(TEXT("Heirloom: typed"), int32(P::Get(7, 0, 0)), int32(EWowDestroyConfirm::TypeDelete));
    TestTrue(TEXT("DELETE"), P::IsConfirmText(TEXT("DELETE")));
    TestTrue(TEXT("delete"), P::IsConfirmText(TEXT("delete")));
    TestTrue(TEXT("spaces"), P::IsConfirmText(TEXT(" delete ")));
    TestFalse(TEXT("partial"), P::IsConfirmText(TEXT("DELET")));
    TestFalse(TEXT("empty"), P::IsConfirmText(TEXT("")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowItemDestroyFlowTest, "WowUnreal.Bags.DestroyFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWowItemDestroyFlowTest::RunTest(const FString&)
{
    {
        auto* C = MakeBagFixture(0, 0);
        int32 Requests = 0; C->OnItemConfirmation.AddLambda([&](const FString&, const TArray<FString>&) { ++Requests; });
        C->PickupBagItemCursor(255, 0, 100);
        TestTrue(TEXT("Grey world drop destroys"), C->RequestDeleteCursorItem());
        TestEqual(TEXT("Grey asks nothing"), Requests, 0);
        TestFalse(TEXT("Grey cursor cleared"), C->HasCursorItemPayload());
    }
    {
        auto* C = MakeBagFixture(2, 1);
        TArray<FString> Args; FString Event;
        C->OnItemConfirmation.AddLambda([&](const FString& E, const TArray<FString>& A) { Event = E; Args = A; });
        C->PickupBagItemCursor(255, 0, 100);
        TestTrue(TEXT("Green requests confirmation"), C->RequestDeleteCursorItem());
        TestEqual(TEXT("Event"), Event, FString(TEXT("DELETE_ITEM_CONFIRM")));
        TestEqual(TEXT("Mode Yes/No"), Args.IsValidIndex(2) ? Args[2] : FString(), FString(TEXT("1")));
        TestEqual(TEXT("Whole stack"), Args.IsValidIndex(3) ? Args[3] : FString(), FString(TEXT("3")));
        TestEqual(TEXT("Entry"), Args.IsValidIndex(4) ? Args[4] : FString(), FString(TEXT("100")));
        TestTrue(TEXT("Still on cursor until confirmed"), C->HasCursorItemPayload());
        TestTrue(TEXT("Confirm destroys"), C->DeleteCursorItem());
    }
    {
        auto* C = MakeBagFixture(2, 2);
        TArray<FString> Args; C->OnItemConfirmation.AddLambda([&](const FString&, const TArray<FString>& A) { Args = A; });
        C->PickupBagItemCursor(255, 0, 100);
        C->RequestDeleteCursorItem();
        TestEqual(TEXT("BoE needs DELETE"), Args.IsValidIndex(2) ? Args[2] : FString(), FString(TEXT("2")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowItemActionBarTest, "WowUnreal.Bags.ItemToActionBar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWowItemActionBarTest::RunTest(const FString&)
{
    auto* C = MakeBagFixture(1, 0);
    auto& Info = C->PacketHandler.ItemTemplates.FindOrAdd(100);
    C->PickupBagItemCursor(255, 0, 100);
    TestTrue(TEXT("Non-usable item consumed by the drop"), C->PlaceCursorIntoActionSlot(5));
    TestFalse(TEXT("Non-usable item not placed"), C->PacketHandler.ActionButtons.IsValidIndex(5) && C->PacketHandler.ActionButtons[5] != 0);

    Info.SpellIds[0] = 1234; Info.SpellTriggers[0] = 0; // potion-like on-use
    C->PickupBagItemCursor(255, 0, 100);
    TestTrue(TEXT("Usable item placed"), C->PlaceCursorIntoActionSlot(5));
    TestEqual(TEXT("Item action type 0x80"), C->PacketHandler.ActionButtons[5], (100u | (0x80u << 24)));
    TestFalse(TEXT("Item returns to bag (cursor cleared)"), C->HasCursorItemPayload());
    TestEqual(TEXT("Item still in backpack"), C->GetItemGuidAtPosition(255, 23), uint64(2));
    TestEqual(TEXT("Count across bags"), C->CountPlayerItems(100), 3);
    TestEqual(TEXT("Use spell"), C->GetItemUseSpell(100), 1234u);
    uint8 Bag = 0, Slot = 0;
    TestTrue(TEXT("Found in backpack"), C->FindPlayerItemByEntry(100, Bag, Slot));
    TestEqual(TEXT("Backpack network bag"), Bag, uint8(255));
    TestEqual(TEXT("Backpack network slot"), Slot, uint8(23));
    return true;
}
