#include "WowTalentModel.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/TalentDbc.h"
#include "Formats/Dbc/TalentTabDbc.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Formats/DbcParser.h"
#include "Mpq/MpqManager.h"
#include "WowAddonSettings.h"
#include "WowSpellPresentation.h"

namespace
{
    // Previewed extra ranks per talent; valid only for the talent data revision
    // they were made against (new SMSG_TALENTS_INFO drops them).
    TMap<uint32, int32> TalentGPreview;
    uint32 TalentGPreviewDataRevision = 0;
    uint32 TalentGRevision = 1;

    void TalentSyncPreview(UWowConnectionManager* Connection)
    {
        const uint32 DataRevision = Connection ? Connection->PacketHandler.TalentRevision : 0;
        if (DataRevision != TalentGPreviewDataRevision)
        {
            TalentGPreviewDataRevision = DataRevision;
            if (!TalentGPreview.IsEmpty()) { TalentGPreview.Reset(); ++TalentGRevision; }
        }
    }

    // Rank lookup that can apply a hypothetical preview map.
    int32 TalentRankWith(UWowConnectionManager* Connection, int32 GroupIndex, uint32 TalentId, const TMap<uint32, int32>* Preview)
    {
        int32 Result = 0;
        if (const TArray<FWowTalentInfo>* Talents = FWowTalentModel::Group(Connection, GroupIndex))
            for (const FWowTalentInfo& Info : *Talents)
                if (Info.TalentId == TalentId) { Result = Info.Rank; break; }
        if (Preview) Result += Preview->FindRef(TalentId);
        return Result;
    }

    // Points spent in a tree in tiers strictly below MaxTier (MaxTier < 0 = all tiers).
    int32 TalentPointsBelow(UWowConnectionManager* Connection, int32 GroupIndex, uint32 TabId, int32 MaxTier, const TMap<uint32, int32>* Preview)
    {
        int32 Points = 0;
        for (const FTalentDbcEntry* Talent : FWowTalentModel::TalentsInTab(TabId))
            if (MaxTier < 0 || static_cast<int32>(Talent->Row) < MaxTier)
                Points += TalentRankWith(Connection, GroupIndex, Talent->TalentID, Preview);
        return Points;
    }

    // Every learned or previewed talent of the tree still satisfies its tier and prerequisite.
    bool TalentTreeValid(UWowConnectionManager* Connection, int32 GroupIndex, uint32 TabId, const TMap<uint32, int32>& Preview)
    {
        for (const FTalentDbcEntry* Talent : FWowTalentModel::TalentsInTab(TabId))
        {
            if (TalentRankWith(Connection, GroupIndex, Talent->TalentID, &Preview) <= 0) continue;
            if (TalentPointsBelow(Connection, GroupIndex, TabId, Talent->Row, &Preview) < static_cast<int32>(Talent->Row) * FWowTalentModel::PointsPerTier)
                return false;
            if (Talent->DependsOn && TalentRankWith(Connection, GroupIndex, Talent->DependsOn, &Preview) < static_cast<int32>(Talent->DependsOnRank) + 1)
                return false;
        }
        return true;
    }
}

uint8 FWowTalentModel::PlayerClass(UWowConnectionManager* Connection)
{
    const FWowPlayerEntity* Player = Connection ? Connection->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    return Player ? Player->GetClassId() : 0;
}

TArray<const FTalentTabDbcEntry*> FWowTalentModel::Tabs(uint8 ClassId)
{
    TArray<const FTalentTabDbcEntry*> Result;
    if (ClassId == 0 || ClassId > 31) return Result;
    const uint32 Mask = 1u << (ClassId - 1);
    for (const FTalentTabDbcEntry& Tab : FDbcStore::Get().TalentTabs().GetAll())
        if ((Tab.ClassMask & Mask) != 0 && Tab.PetTalentMask == 0) Result.Add(&Tab);
    Result.Sort([](const FTalentTabDbcEntry& A, const FTalentTabDbcEntry& B) { return A.TabPage < B.TabPage; });
    return Result;
}

TArray<const FTalentDbcEntry*> FWowTalentModel::TalentsInTab(uint32 TabId)
{
    // Cached per tab; DBCs do not change while running.
    static TMap<uint32, TArray<const FTalentDbcEntry*>> Cache;
    static int32 CachedCount = -1;
    const FTalentDbc& Dbc = FDbcStore::Get().Talents();
    if (CachedCount != Dbc.Num()) { Cache.Reset(); CachedCount = Dbc.Num(); }
    if (const TArray<const FTalentDbcEntry*>* Found = Cache.Find(TabId)) return *Found;
    TArray<const FTalentDbcEntry*>& Result = Cache.Add(TabId);
    for (const FTalentDbcEntry& Talent : Dbc.GetAll())
        if (Talent.TalentTab == TabId) Result.Add(&Talent);
    Result.Sort([](const FTalentDbcEntry& A, const FTalentDbcEntry& B)
        { return A.Row != B.Row ? A.Row < B.Row : A.Col < B.Col; });
    return Result;
}

int32 FWowTalentModel::MaxRank(const FTalentDbcEntry& Talent)
{
    for (int32 I = 4; I >= 0; --I)
        if (Talent.RankID[I] != 0) return I + 1;
    return 0;
}

uint32 FWowTalentModel::SpellForRank(const FTalentDbcEntry& Talent, int32 Rank)
{
    const int32 Max = MaxRank(Talent);
    if (Max <= 0) return 0;
    return Talent.RankID[FMath::Clamp(Rank, 1, Max) - 1];
}

FString FWowTalentModel::SpellName(const FTalentDbcEntry& Talent)
{
    const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(SpellForRank(Talent, 1));
    return Spell ? Spell->SpellName : FString::Printf(TEXT("Talent %u"), Talent.TalentID);
}

FString FWowTalentModel::IconPath(const FTalentDbcEntry& Talent)
{
    const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(SpellForRank(Talent, 1));
    const FSpellIconDbcEntry* Icon = Spell ? FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID) : nullptr;
    return Icon && !Icon->TexturePath.IsEmpty() ? Icon->TexturePath : FString(TEXT("Interface\\Icons\\INV_Misc_QuestionMark"));
}

FString FWowTalentModel::IconPath(const FTalentTabDbcEntry& Tab)
{
    const FSpellIconDbcEntry* Icon = FDbcStore::Get().SpellIcons().GetById(Tab.SpellIcon);
    return Icon && !Icon->TexturePath.IsEmpty() ? Icon->TexturePath : FString(TEXT("Interface\\Icons\\INV_Misc_QuestionMark"));
}

FString FWowTalentModel::BackgroundPath(const FTalentTabDbcEntry& Tab)
{
    return Tab.InternalName.IsEmpty() ? FString() : TEXT("Interface\\TalentFrame\\") + Tab.InternalName;
}

int32 FWowTalentModel::NumGroups(UWowConnectionManager* Connection)
{
    return Connection ? FMath::Max(1, Connection->PacketHandler.TalentGroups.Num()) : 1;
}

int32 FWowTalentModel::ActiveGroup(UWowConnectionManager* Connection)
{
    return Connection ? Connection->PacketHandler.ActiveTalentGroup : 0;
}

const TArray<FWowTalentInfo>* FWowTalentModel::Group(UWowConnectionManager* Connection, int32 GroupIndex)
{
    if (!Connection) return nullptr;
    const FWowPacketHandler& Packets = Connection->PacketHandler;
    if (Packets.TalentGroups.IsValidIndex(GroupIndex)) return &Packets.TalentGroups[GroupIndex];
    return GroupIndex == Packets.ActiveTalentGroup ? &Packets.Talents : nullptr;
}

int32 FWowTalentModel::Unspent(UWowConnectionManager* Connection)
{
    if (!Connection) return 0;
    FWowPacketHandler& Packets = Connection->PacketHandler;
    if (Packets.bTalentInfoReceived) return static_cast<int32>(Packets.UnspentTalentPoints);
    // Before the first SMSG_TALENTS_INFO: one point per level from 10.
    const FWowPlayerEntity* Player = Packets.EntityManager.GetLocalPlayer();
    int32 Spent = 0;
    for (const FWowTalentInfo& Info : Packets.Talents) Spent += Info.Rank;
    return Player ? FMath::Max(0, Player->GetLevel() - 9 - Spent) : 0;
}

int32 FWowTalentModel::Rank(UWowConnectionManager* Connection, int32 GroupIndex, uint32 TalentId, bool bWithPreview)
{
    TalentSyncPreview(Connection);
    const bool bPreview = bWithPreview && GroupIndex == ActiveGroup(Connection);
    return TalentRankWith(Connection, GroupIndex, TalentId, bPreview ? &TalentGPreview : nullptr);
}

int32 FWowTalentModel::PointsInTab(UWowConnectionManager* Connection, int32 GroupIndex, uint32 TabId, bool bWithPreview)
{
    TalentSyncPreview(Connection);
    const bool bPreview = bWithPreview && GroupIndex == ActiveGroup(Connection);
    return TalentPointsBelow(Connection, GroupIndex, TabId, -1, bPreview ? &TalentGPreview : nullptr);
}

bool FWowTalentModel::TierUnlocked(UWowConnectionManager* Connection, int32 GroupIndex, const FTalentDbcEntry& Talent, bool bWithPreview)
{
    return PointsInTab(Connection, GroupIndex, Talent.TalentTab, bWithPreview) >= static_cast<int32>(Talent.Row) * PointsPerTier;
}

bool FWowTalentModel::PrereqMet(UWowConnectionManager* Connection, int32 GroupIndex, const FTalentDbcEntry& Talent, bool bWithPreview)
{
    // DependsOnRank is the prerequisite's 0-based rank index.
    return Talent.DependsOn == 0
        || Rank(Connection, GroupIndex, Talent.DependsOn, bWithPreview) >= static_cast<int32>(Talent.DependsOnRank) + 1;
}

int32 FWowTalentModel::PreviewRanks(uint32 TalentId)
{
    return TalentGPreview.FindRef(TalentId);
}

int32 FWowTalentModel::PreviewSpent()
{
    int32 Total = 0;
    for (const TPair<uint32, int32>& Pair : TalentGPreview) Total += Pair.Value;
    return Total;
}

bool FWowTalentModel::CanAddPreview(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent)
{
    if (!Connection) return false;
    TalentSyncPreview(Connection);
    const int32 GroupIdx = ActiveGroup(Connection);
    return Unspent(Connection) - PreviewSpent() > 0
        && Rank(Connection, GroupIdx, Talent.TalentID, true) < MaxRank(Talent)
        && TierUnlocked(Connection, GroupIdx, Talent, true)
        && PrereqMet(Connection, GroupIdx, Talent, true);
}

bool FWowTalentModel::CanRemovePreview(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent)
{
    if (!Connection) return false;
    TalentSyncPreview(Connection);
    if (PreviewRanks(Talent.TalentID) <= 0) return false;
    TMap<uint32, int32> Test = TalentGPreview;
    if (--Test.FindChecked(Talent.TalentID) <= 0) Test.Remove(Talent.TalentID);
    return TalentTreeValid(Connection, ActiveGroup(Connection), Talent.TalentTab, Test);
}

bool FWowTalentModel::AddPreview(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent)
{
    if (!CanAddPreview(Connection, Talent)) return false;
    ++TalentGPreview.FindOrAdd(Talent.TalentID);
    ++TalentGRevision;
    return true;
}

bool FWowTalentModel::RemovePreview(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent)
{
    if (!CanRemovePreview(Connection, Talent)) return false;
    if (--TalentGPreview.FindChecked(Talent.TalentID) <= 0) TalentGPreview.Remove(Talent.TalentID);
    ++TalentGRevision;
    return true;
}

void FWowTalentModel::ClearPreview()
{
    if (TalentGPreview.IsEmpty()) return;
    TalentGPreview.Reset();
    ++TalentGRevision;
}

bool FWowTalentModel::LearnPreview(UWowConnectionManager* Connection)
{
    if (!Connection) return false;
    TalentSyncPreview(Connection);
    if (TalentGPreview.IsEmpty()) return false;
    // The server learns them in packet order, so prerequisites and lower tiers go first.
    TArray<const FTalentDbcEntry*> Ordered;
    const FTalentDbc& Dbc = FDbcStore::Get().Talents();
    for (const TPair<uint32, int32>& Pair : TalentGPreview)
        if (const FTalentDbcEntry* Talent = Dbc.GetById(Pair.Key)) Ordered.Add(Talent);
    Ordered.Sort([](const FTalentDbcEntry& A, const FTalentDbcEntry& B)
        { return A.Row != B.Row ? A.Row < B.Row : A.Col < B.Col; });
    TArray<FIntPoint> Ranks;
    const int32 GroupIdx = ActiveGroup(Connection);
    for (const FTalentDbcEntry* Talent : Ordered)
    {
        // CMSG_LEARN_PREVIEW_TALENTS takes the final 0-based rank index.
        const int32 Final = Rank(Connection, GroupIdx, Talent->TalentID, true);
        if (Final > 0) Ranks.Add(FIntPoint(static_cast<int32>(Talent->TalentID), Final - 1));
    }
    Connection->SendLearnPreviewTalents(Ranks);
    // Keep the preview on screen until the server answers with new talent data.
    return true;
}

bool FWowTalentModel::LearnNow(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent)
{
    if (!CanAddPreview(Connection, Talent) || HasPreview()) return false;
    // CMSG_LEARN_TALENT takes the 0-based index of the rank to learn.
    Connection->SendLearnTalent(static_cast<int32>(Talent.TalentID), Rank(Connection, ActiveGroup(Connection), Talent.TalentID, false));
    return true;
}

uint32 FWowTalentModel::Revision()
{
    return TalentGRevision;
}

void FWowTalentModel::Touch()
{
    ++TalentGRevision;
}

TArray<FWowTalentTooltipLine> FWowTalentModel::TooltipLines(UWowConnectionManager* Connection, int32 GroupIndex,
    const FTalentTabDbcEntry& Tab, const FTalentDbcEntry& Talent, bool bWithPreview)
{
    const bool bPreview = bWithPreview && GroupIndex == ActiveGroup(Connection);
    const int32 TalentRank = Rank(Connection, GroupIndex, Talent.TalentID, bPreview);
    const int32 Max = MaxRank(Talent);
    const FLinearColor White = FLinearColor::White, Red(1.f, .125f, .125f), Gold(1.f, .82f, 0.f), Green(.125f, 1.f, .125f);
    TArray<FWowTalentTooltipLine> Lines;
    Lines.Add({SpellName(Talent), White, false});
    Lines.Add({FString::Printf(TEXT("Rank %d/%d"), TalentRank, Max), White, false});
    if (!TierUnlocked(Connection, GroupIndex, Talent, bPreview))
        Lines.Add({FString::Printf(TEXT("Requires %d points in %s Talents"), static_cast<int32>(Talent.Row) * PointsPerTier, *Tab.Name), Red, true});
    if (!PrereqMet(Connection, GroupIndex, Talent, bPreview))
        if (const FTalentDbcEntry* Prereq = FDbcStore::Get().Talents().GetById(Talent.DependsOn))
        {
            const int32 Need = static_cast<int32>(Talent.DependsOnRank) + 1;
            Lines.Add({FString::Printf(TEXT("Requires %d %s in %s"), Need, Need == 1 ? TEXT("point") : TEXT("points"), *SpellName(*Prereq)), Red, true});
        }
    const FString Current = FWowSpellPresentation::Description(SpellForRank(Talent, FMath::Max(1, TalentRank)));
    if (!Current.IsEmpty()) Lines.Add({Current, Gold, true});
    if (TalentRank > 0 && TalentRank < Max)
    {
        Lines.Add({TEXT(" "), White, false});
        Lines.Add({TEXT("Next rank:"), White, false});
        const FString Next = FWowSpellPresentation::Description(SpellForRank(Talent, TalentRank + 1));
        if (!Next.IsEmpty()) Lines.Add({Next, Gold, true});
    }
    if (GroupIndex == ActiveGroup(Connection) && CanAddPreview(Connection, Talent))
        Lines.Add({TEXT("Click to learn"), Green, false});
    return Lines;
}

int32 FWowTalentModel::PrimaryTab(UWowConnectionManager* Connection, int32 GroupIndex)
{
    const TArray<const FTalentTabDbcEntry*> AllTabs = Tabs(PlayerClass(Connection));
    int32 Best = INDEX_NONE, BestPoints = 0, Second = 0;
    for (int32 Index = 0; Index < AllTabs.Num(); ++Index)
    {
        const int32 Points = PointsInTab(Connection, GroupIndex, AllTabs[Index]->TalentTabID, false);
        if (Points > BestPoints) { Second = BestPoints; BestPoints = Points; Best = Index; }
        else if (Points > Second) Second = Points;
    }
    return BestPoints > 0 && BestPoints > Second ? Best : INDEX_NONE;
}

namespace
{
struct FTalentGlyphRecord { uint32 SpellId = 0, IconId = 0; };
const FTalentGlyphRecord* TalentFindGlyphRecord(uint32 GlyphId)
{
    // GlyphProperties.dbc: ID, SpellID, TypeFlags, SpellIconID.
    static TMap<uint32, FTalentGlyphRecord> Glyphs;
    static bool bLoaded = false;
    if (!bLoaded)
        if (FMpqManager* Mpq = FWowAddonSettings::GetMpq())
        {
            bLoaded = true;
            TArray<uint8> Bytes;
            FDbcParser P;
            if (Mpq->ReadFile(TEXT("DBFilesClient\\GlyphProperties.dbc"), Bytes) && P.Parse(Bytes) && P.GetFieldCount() >= 4)
                for (uint32 I = 0; I < P.GetRecordCount(); ++I)
                    Glyphs.Add(P.GetUInt(I, 0), { P.GetUInt(I, 1), P.GetUInt(I, 3) });
        }
    return Glyphs.Find(GlyphId);
}
}

FWowGlyphSocket FWowTalentModel::GlyphSocket(UWowConnectionManager* Connection, int32 Socket, int32 GroupIndex)
{
    // 3.3.5 socket order: type (1 major, 2 minor) and unlock level per socket id.
    static const int32 Types[6] = { 1, 2, 2, 1, 2, 1 };
    static const int32 Levels[6] = { 15, 15, 50, 30, 70, 80 };
    FWowGlyphSocket Result;
    if (Socket < 1 || Socket > 6) return Result;
    Result.Type = Types[Socket - 1];
    Result.UnlockLevel = Levels[Socket - 1];
    const FWowPlayerEntity* Player = Connection ? Connection->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    Result.bEnabled = Player && Player->GetLevel() >= Result.UnlockLevel;
    if (!Connection) return Result;
    const TArray<TArray<uint16>>& Glyphs = Connection->PacketHandler.TalentGlyphs;
    if (Glyphs.IsValidIndex(GroupIndex) && Glyphs[GroupIndex].IsValidIndex(Socket - 1)) Result.GlyphId = Glyphs[GroupIndex][Socket - 1];
    if (const FTalentGlyphRecord* Record = Result.GlyphId ? TalentFindGlyphRecord(Result.GlyphId) : nullptr)
    {
        Result.SpellId = Record->SpellId;
        if (const FSpellIconDbcEntry* Icon = FDbcStore::Get().SpellIcons().GetById(Record->IconId)) Result.IconPath = Icon->TexturePath;
    }
    return Result;
}

TArray<FWowTalentTooltipLine> FWowTalentModel::GlyphTooltipLines(UWowConnectionManager* Connection, int32 Socket, int32 GroupIndex)
{
    const FWowGlyphSocket Info = GlyphSocket(Connection, Socket, GroupIndex);
    const FLinearColor White = FLinearColor::White, Gold(1.f, .82f, 0.f), Grey(.5f, .5f, .5f), Red(1.f, .125f, .125f), Kind(.6f, .8f, 1.f);
    TArray<FWowTalentTooltipLine> Lines;
    const TCHAR* TypeName = Info.Type == 1 ? TEXT("Major Glyph") : TEXT("Minor Glyph");
    if (!Info.bEnabled)
    {
        Lines.Add({TEXT("Locked"), Grey, false});
        Lines.Add({TypeName, Kind, false});
        Lines.Add({FString::Printf(TEXT("Unlocked at level %d."), Info.UnlockLevel), Red, true});
        return Lines;
    }
    const FSpellDbcEntry* Spell = Info.SpellId ? FDbcStore::Get().Spells().GetById(Info.SpellId) : nullptr;
    if (!Spell)
    {
        Lines.Add({TEXT("Empty"), White, false});
        Lines.Add({TypeName, Kind, false});
        Lines.Add({TEXT("Use a Glyph from your inventory to inscribe your spellbook."), Gold, true});
        return Lines;
    }
    Lines.Add({Spell->SpellName, White, false});
    Lines.Add({TypeName, Kind, false});
    const FString Text = FWowSpellPresentation::Description(Spell->ID);
    if (!Text.IsEmpty()) Lines.Add({Text, Gold, true});
    Lines.Add({TEXT("<Shift Right Click to Remove>"), Grey, false});
    return Lines;
}
