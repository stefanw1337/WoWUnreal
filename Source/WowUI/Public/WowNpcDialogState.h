#pragma once

#include "CoreMinimal.h"
#include "WowEntity.h"

/**
 * The NPC conversation the native quest/gossip dialog shows, for the Lua API the stock interface
 * and addons use (GetGossipOptions, GetTitleText, AcceptQuest, GetQuestReward ...; LuaNpcDialogApi.cpp).
 * UWowGameUI fills it when it shows the dialog and clears it when the dialog closes; the actions
 * run the same code as the dialog's own buttons.
 */
struct WOWUI_API FWowNpcDialogState
{
    enum class EMode : uint8 { None, Gossip, Greeting, Detail, Progress, Reward };

    EMode Mode = EMode::None;
    FWowGossipMenu Menu;          // Gossip / Greeting
    FWowQuestDetails Quest;       // Detail / Progress / Reward

    TFunction<void(uint32 /*QuestId*/, bool /*bActive*/)> SelectQuest;
    TFunction<void(int32 /*OptionIndex, 0-based*/)> SelectOption;
    TFunction<void()> Accept;
    TFunction<void()> Decline;
    TFunction<void()> Continue;
    TFunction<void(int32 /*RewardChoice, 0-based*/)> Complete;
    TFunction<void()> Close;
    /** False once the native dialog was closed by its own buttons, Escape or walking away. */
    TFunction<bool()> IsOpen;

    EMode ShownMode() const { return Mode != EMode::None && (!IsOpen || IsOpen()) ? Mode : EMode::None; }

    /** Quests of the list: active ones (in your log, icon 4: the dialog shows "?") and the ones on offer. */
    static bool IsActiveQuest(const FWowQuestMenuItem& Item) { return Item.Icon == 4; }

    static FWowNpcDialogState& Get();
};
