#pragma once

#include "CoreMinimal.h"

class UWowConnectionManager;

/** One macro as the 3.3.5 client keeps it: name (16 letters), icon texture, up to 255 letters of text. */
struct FWowMacro
{
    FString Name;
    FString Icon;
    FString Body;
};

/**
 * Macros like the original client (MacroFrame, macros-cache.txt): 36 general macros shared by the
 * account (indexes 1-36) and 18 per character (indexes 37-54). Stored in the stock text format under
 * Saved/WTF/Account/<account>/macros-cache.txt and .../<realm>/<character>/macros-cache.txt.
 * Action buttons hold macros as type 64 with the macro index, the same numbers as the client.
 */
struct WOWUI_API FWowMacros
{
    static constexpr int32 MaxAccount = 36;
    static constexpr int32 MaxCharacter = 18;
    static constexpr int32 MaxNameLength = 16;
    static constexpr int32 MaxBodyLength = 255;

    static int32 NumAccount();
    static int32 NumCharacter();
    /** 1-36 general, 37-54 character; null when empty. */
    static const FWowMacro* Get(int32 Index);
    static int32 FindByName(const FString& Name);
    /** Returns the new macro's index, 0 when that list is full. */
    static int32 Create(const FString& Name, const FString& Icon, const FString& Body, bool bPerCharacter);
    /** Returns the macro's index (unchanged), 0 when it does not exist. */
    static int32 Edit(int32 Index, const FString& Name, const FString& Icon, const FString& Body);
    /** Deletes a macro; later macros of the same list move up one, and action buttons follow them. */
    static bool Delete(int32 Index, UWowConnectionManager* Connection);

    /** Icon choices (GetMacroIconInfo): the question mark first, then every Interface\Icons texture. */
    static const TArray<FString>& Icons();
    /** The icon to draw: the chosen one, or for the question mark the first /cast spell's icon. */
    static FString DisplayIcon(const FWowMacro& Macro, UWowConnectionManager* Connection);

    /** Bumped on every change (windows and action bars redraw). */
    static uint32 Revision();
    static FSimpleMulticastDelegate& OnChanged();

    static const TCHAR* QuestionMarkIcon() { return TEXT("Interface\\Icons\\INV_Misc_QuestionMark"); }

private:
    static void EnsureLoaded();
    static void Save(bool bCharacter);
};
