#pragma once

#include "CoreMinimal.h"

// How much confirmation destroying an item needs (drag out of the bags into the world).
//   None       - (unused) destroy without asking.
//   Confirm    - every other item asks Yes/No: grey, white, green, blue and quest items.
//   TypeDelete - epic and above (incl. legendary/artifact/heirloom) and every
//                Bind-on-Equip item that is not yet soulbound need "DELETE" typed.
enum class EWowDestroyConfirm : uint8
{
    None = 0,
    Confirm = 1,
    TypeDelete = 2,
};

struct FWowItemDestroyPolicy
{
    static constexpr uint32 QualityUncommon = 2;
    static constexpr uint32 QualityEpic = 4;
    static constexpr uint32 BondingOnEquip = 2;
    static constexpr uint32 BondingQuest = 4;
    static constexpr uint32 ItemFlagSoulbound = 0x1; // ITEM_FIELD_FLAGS, 3.3.5

    static EWowDestroyConfirm Get(uint32 Quality, uint32 Bonding, uint32 ItemFieldFlags)
    {
        const bool bUnboundBoE = Bonding == BondingOnEquip && (ItemFieldFlags & ItemFlagSoulbound) == 0;
        if (Quality >= QualityEpic || bUnboundBoE) return EWowDestroyConfirm::TypeDelete;
        // Stefan: grey and white items also get the Yes/No so a drag can never destroy by accident.
        return EWowDestroyConfirm::Confirm;
    }

    // "delete" and "DELETE" (any case, surrounding spaces ignored).
    static bool IsConfirmText(const FString& Text)
    {
        return Text.TrimStartAndEnd().Equals(TEXT("DELETE"), ESearchCase::IgnoreCase);
    }
};
