// Macros (MacroFrame / macros-cache.txt) - see WowMacros.h.
#include "WowMacros.h"
#include "WowSavedVariables.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowMacros, Log, All);

namespace
{
    TArray<FWowMacro> GAccountMacros, GCharacterMacros;
    FString GLoadedAccountDir, GLoadedCharacterDir;
    uint32 GRevision = 1;

    /** Where macros-cache.txt is read from in this WTF mode (may be the WoW client's own copy). */
    FString CacheReadFile(bool bCharacter)
    {
        const FString Relative = FPaths::Combine(bCharacter ? FWowSavedVariables::GetCharacterRelative()
            : FWowSavedVariables::GetAccountRelative(), TEXT("macros-cache.txt"));
        const FString Found = FWowSavedVariables::ResolveReadPath(Relative);
        FWowSavedVariables::NoteRead(FWowSavedVariables::GetWritePath(Relative));
        return Found;
    }

    FString CacheFile(bool bCharacter)
    {
        return FPaths::Combine(bCharacter ? FWowSavedVariables::GetCharacterDirectory() : FWowSavedVariables::GetAccountDirectory(),
            TEXT("macros-cache.txt"));
    }

    // MACRO <n> "<name>" <icon>
    // <text lines>
    // END
    TArray<FWowMacro> ReadCache(const FString& Path)
    {
        TArray<FWowMacro> Out;
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Path)) return Out;
        TArray<FString> Lines;
        Text.ParseIntoArrayLines(Lines, false);
        FWowMacro* Current = nullptr;
        TArray<FString> BodyLines;
        for (const FString& RawLine : Lines)
        {
            const FString Line = RawLine.TrimEnd();
            if (!Current && Line.StartsWith(TEXT("MACRO ")))
            {
                FWowMacro Macro;
                int32 FirstQuote = INDEX_NONE, LastQuote = INDEX_NONE;
                Line.FindChar(TEXT('"'), FirstQuote);
                Line.FindLastChar(TEXT('"'), LastQuote);
                if (FirstQuote != INDEX_NONE && LastQuote > FirstQuote)
                {
                    Macro.Name = Line.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
                    Macro.Icon = Line.Mid(LastQuote + 1).TrimStartAndEnd();
                }
                if (Macro.Icon.IsEmpty()) Macro.Icon = FWowMacros::QuestionMarkIcon();
                else if (!Macro.Icon.Contains(TEXT("\\"))) Macro.Icon = TEXT("Interface\\Icons\\") + Macro.Icon;
                Out.Add(MoveTemp(Macro));
                Current = &Out.Last();
                BodyLines.Reset();
            }
            else if (Current && Line == TEXT("END"))
            {
                Current->Body = FString::Join(BodyLines, TEXT("\n")).Left(FWowMacros::MaxBodyLength);
                Current = nullptr;
            }
            else if (Current)
            {
                BodyLines.Add(RawLine);
            }
        }
        if (Current) Current->Body = FString::Join(BodyLines, TEXT("\n")).Left(FWowMacros::MaxBodyLength);
        return Out;
    }

    void WriteCache(const FString& Path, const TArray<FWowMacro>& Macros, int32 FirstIndex)
    {
        FString Text;
        for (int32 I = 0; I < Macros.Num(); ++I)
        {
            FString Icon = Macros[I].Icon;
            Icon.RemoveFromStart(TEXT("Interface\\Icons\\"), ESearchCase::IgnoreCase);
            Text += FString::Printf(TEXT("MACRO %d \"%s\" %s\r\n"), FirstIndex + I, *Macros[I].Name.Replace(TEXT("\""), TEXT("'")), *Icon);
            if (!Macros[I].Body.IsEmpty()) Text += Macros[I].Body.Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\n"), TEXT("\r\n")) + TEXT("\r\n");
            Text += TEXT("END\r\n");
        }
        if (!FWowSavedVariables::SaveWtfFile(Path, Text))
            UE_LOG(LogWowMacros, Warning, TEXT("Could not save macros to %s"), *Path);
    }

    void Changed()
    {
        ++GRevision;
        FWowMacros::OnChanged().Broadcast();
    }

    FString CleanName(const FString& Name)
    {
        FString Out = Name.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd();
        return Out.Left(FWowMacros::MaxNameLength);
    }

    FString CleanBody(const FString& Body)
    {
        return Body.Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\r"), TEXT("\n")).Left(FWowMacros::MaxBodyLength);
    }
}

FSimpleMulticastDelegate& FWowMacros::OnChanged()
{
    static FSimpleMulticastDelegate Delegate;
    return Delegate;
}

uint32 FWowMacros::Revision()
{
    EnsureLoaded();
    return GRevision;
}

void FWowMacros::EnsureLoaded()
{
    // Reload when the account or character changes (the identity is set at login / world entry).
    const FString AccountDir = FWowSavedVariables::GetAccountDirectory();
    const FString CharacterDir = FWowSavedVariables::GetCharacterDirectory();
    bool bReloaded = false;
    if (AccountDir != GLoadedAccountDir)
    {
        GLoadedAccountDir = AccountDir;
        GAccountMacros = ReadCache(CacheReadFile(false));
        if (GAccountMacros.Num() > MaxAccount) GAccountMacros.SetNum(MaxAccount);
        bReloaded = true;
    }
    if (CharacterDir != GLoadedCharacterDir)
    {
        GLoadedCharacterDir = CharacterDir;
        GCharacterMacros = ReadCache(CacheReadFile(true));
        if (GCharacterMacros.Num() > MaxCharacter) GCharacterMacros.SetNum(MaxCharacter);
        bReloaded = true;
    }
    if (bReloaded) ++GRevision;
}

void FWowMacros::Save(bool bCharacter)
{
    if (bCharacter) WriteCache(CacheFile(true), GCharacterMacros, MaxAccount + 1);
    else WriteCache(CacheFile(false), GAccountMacros, 1);
}

int32 FWowMacros::NumAccount() { EnsureLoaded(); return GAccountMacros.Num(); }
int32 FWowMacros::NumCharacter() { EnsureLoaded(); return GCharacterMacros.Num(); }

const FWowMacro* FWowMacros::Get(int32 Index)
{
    EnsureLoaded();
    if (Index >= 1 && Index <= MaxAccount) return GAccountMacros.IsValidIndex(Index - 1) ? &GAccountMacros[Index - 1] : nullptr;
    if (Index > MaxAccount && Index <= MaxAccount + MaxCharacter)
        return GCharacterMacros.IsValidIndex(Index - MaxAccount - 1) ? &GCharacterMacros[Index - MaxAccount - 1] : nullptr;
    return nullptr;
}

int32 FWowMacros::FindByName(const FString& Name)
{
    EnsureLoaded();
    for (int32 I = 0; I < GAccountMacros.Num(); ++I)
        if (GAccountMacros[I].Name.Equals(Name, ESearchCase::IgnoreCase)) return I + 1;
    for (int32 I = 0; I < GCharacterMacros.Num(); ++I)
        if (GCharacterMacros[I].Name.Equals(Name, ESearchCase::IgnoreCase)) return MaxAccount + I + 1;
    return 0;
}

int32 FWowMacros::Create(const FString& Name, const FString& Icon, const FString& Body, bool bPerCharacter)
{
    EnsureLoaded();
    TArray<FWowMacro>& List = bPerCharacter ? GCharacterMacros : GAccountMacros;
    if (List.Num() >= (bPerCharacter ? MaxCharacter : MaxAccount)) return 0;
    FWowMacro& Macro = List.AddDefaulted_GetRef();
    Macro.Name = CleanName(Name);
    Macro.Icon = Icon.IsEmpty() ? FString(QuestionMarkIcon()) : Icon;
    Macro.Body = CleanBody(Body);
    Save(bPerCharacter);
    Changed();
    return bPerCharacter ? MaxAccount + List.Num() : List.Num();
}

int32 FWowMacros::Edit(int32 Index, const FString& Name, const FString& Icon, const FString& Body)
{
    EnsureLoaded();
    FWowMacro* Macro = const_cast<FWowMacro*>(Get(Index));
    if (!Macro) return 0;
    Macro->Name = CleanName(Name);
    if (!Icon.IsEmpty()) Macro->Icon = Icon;
    Macro->Body = CleanBody(Body);
    Save(Index > MaxAccount);
    Changed();
    return Index;
}

bool FWowMacros::Delete(int32 Index, UWowConnectionManager* Connection)
{
    EnsureLoaded();
    if (!Get(Index)) return false;
    const bool bCharacter = Index > MaxAccount;
    TArray<FWowMacro>& List = bCharacter ? GCharacterMacros : GAccountMacros;
    const int32 First = bCharacter ? MaxAccount + 1 : 1;
    const int32 Last = First + List.Num() - 1;
    List.RemoveAt(Index - First);
    Save(bCharacter);
    // Action buttons hold macro numbers: the deleted one is cleared, later ones of the same list
    // move down with their macro (the client renumbers them the same way).
    if (Connection)
    {
        TArray<uint32>& Buttons = Connection->PacketHandler.ActionButtons;
        for (int32 Slot = 0; Slot < Buttons.Num(); ++Slot)
        {
            const uint32 Packed = Buttons[Slot];
            if ((Packed >> 24) != 64) continue;
            const int32 MacroId = int32(Packed & 0x00FFFFFFu);
            if (MacroId == Index)
            {
                Buttons[Slot] = 0;
                Connection->SendClearActionButton(Slot);
            }
            else if (MacroId > Index && MacroId <= Last)
            {
                Buttons[Slot] = uint32(MacroId - 1) | (64u << 24);
                Connection->SendSetActionButton(Slot, uint32(MacroId - 1), 64);
            }
        }
    }
    Changed();
    return true;
}

const TArray<FString>& FWowMacros::Icons()
{
    static TArray<FString> List;
    static int32 BuiltFrom = -1;
    const TArray<FSpellIconDbcEntry>& Entries = FDbcStore::Get().SpellIcons().GetAll();
    if (BuiltFrom != Entries.Num())
    {
        BuiltFrom = Entries.Num();
        List.Reset();
        List.Add(QuestionMarkIcon());
        TSet<FString> Seen;
        Seen.Add(FString(QuestionMarkIcon()).ToLower());
        for (const FSpellIconDbcEntry& Entry : Entries)
        {
            const FString Lower = Entry.TexturePath.ToLower();
            if (!Lower.StartsWith(TEXT("interface\\icons\\")) || Seen.Contains(Lower)) continue;
            Seen.Add(Lower);
            List.Add(Entry.TexturePath);
        }
    }
    return List;
}

FString FWowMacros::DisplayIcon(const FWowMacro& Macro, UWowConnectionManager* Connection)
{
    if (!Macro.Icon.Equals(QuestionMarkIcon(), ESearchCase::IgnoreCase) || !Connection) return Macro.Icon;
    // "?" shows the spell the macro casts: #showtooltip <spell>, or the first /cast line.
    TArray<FString> Lines;
    Macro.Body.ParseIntoArrayLines(Lines);
    for (const FString& RawLine : Lines)
    {
        FString Line = RawLine.TrimStartAndEnd();
        FString Rest;
        if (Line.StartsWith(TEXT("#showtooltip"), ESearchCase::IgnoreCase)) Rest = Line.Mid(12);
        else if (Line.StartsWith(TEXT("/cast "), ESearchCase::IgnoreCase)) Rest = Line.Mid(6);
        else continue;
        // Drop [conditions] and ";" alternatives, keep the first spell name, strip "(Rank n)".
        Rest = Rest.TrimStartAndEnd();
        while (Rest.StartsWith(TEXT("[")))
        {
            int32 Close = INDEX_NONE;
            if (!Rest.FindChar(TEXT(']'), Close)) break;
            Rest = Rest.Mid(Close + 1).TrimStart();
        }
        int32 Semi = INDEX_NONE;
        if (Rest.FindChar(TEXT(';'), Semi)) Rest = Rest.Left(Semi);
        int32 Paren = INDEX_NONE;
        if (Rest.FindChar(TEXT('('), Paren)) Rest = Rest.Left(Paren);
        Rest.RemoveFromStart(TEXT("!"));
        Rest = Rest.TrimStartAndEnd();
        if (Rest.IsEmpty()) continue;
        uint32 Best = 0;
        for (const uint32 SpellId : Connection->PacketHandler.KnownSpells)
            if (const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(SpellId))
                if (Spell->SpellName.Equals(Rest, ESearchCase::IgnoreCase) && SpellId > Best) Best = SpellId;
        if (const FSpellDbcEntry* Spell = Best ? FDbcStore::Get().Spells().GetById(Best) : nullptr)
            if (const FSpellIconDbcEntry* Icon = FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID))
                if (!Icon->TexturePath.IsEmpty()) return Icon->TexturePath;
        break;
    }
    return Macro.Icon;
}
