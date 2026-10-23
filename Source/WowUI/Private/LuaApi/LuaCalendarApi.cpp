// Calendar data from the server (r240): the player's events and invites, raid lockouts and the raid
// reset schedule from SMSG_CALENDAR_SEND_CALENDAR, served to Blizzard_Calendar through the
// CalendarGetDayEvent family. Month bookkeeping (CalendarGetMonth ...) stays in LuaStubs.cpp.
// r241: holidays from the client's own Holidays.dbc (+ HolidayNames / HolidayDescriptions), like the
// original client: the server's holiday list is empty on AzerothCore/TrinityCore.
// Not yet: opening player events (CMSG_CALENDAR_GET_EVENT), creating events.
#include "CoreMinimal.h"
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/DbcParser.h"
#include "Mpq/MpqManager.h"
#include "WowAddonSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowCalendar, Log, All);

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace
{
struct FDayEvent
{
    FString Title;
    int32 Hour = 0;
    int32 Minute = 0;
    FString CalendarType;
    int32 EventType = 0;
    FString ModStatus;
    int32 InviteStatus = 0;
    int32 Difficulty = 0;
    FString DifficultyName;
    int32 InviteType = 1;
    uint64 RaidId = 0;
    int64 SortKey = 0;
    FString SequenceType;    // HOLIDAY: START / ONGOING / END / "" (one day)
    FString Texture;         // HOLIDAY: Interface\Calendar\Holidays\ base name
    int32 HolidayId = 0;
};

// Holidays.dbc (3.3.5, 55 fields): 0 ID, 1-10 Duration (hours), 11-36 Date (packed), 37 Region,
// 38 Looping, 39-48 CalendarFlags, 49 HolidayNameID, 50 HolidayDescriptionID, 51 TextureFilename,
// 52 Priority, 53 CalendarFilterType, 54 Flags.
struct FHoliday
{
    int32 Id = 0;
    int32 Duration[10] = {};
    uint32 Date[26] = {};
    bool bLooping = false;
    FString Name, Description, Texture;
    int32 Priority = 0;
    int32 FilterType = 0;
};
bool GHolidaysLoaded = false;
TArray<FHoliday> GHolidays;

TMap<int32, FString> LoadLocalizedNames(FMpqManager& Mpq, const TCHAR* Path)
{
    TMap<int32, FString> Names;
    TArray<uint8> Data;
    FDbcParser Parser;
    if (!Mpq.ReadFile(Path, Data) || !Parser.Parse(Data)) return Names;
    for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
        Names.Add(static_cast<int32>(Parser.GetUInt(R, 0)), Parser.GetLocalizedString(R, 1));
    return Names;
}

const TArray<FHoliday>& Holidays()
{
    if (GHolidaysLoaded) return GHolidays;
    FMpqManager* Mpq = FWowAddonSettings::GetMpq();
    if (!Mpq) return GHolidays; // retried on the next call
    GHolidaysLoaded = true;
    TArray<uint8> Data;
    FDbcParser Parser;
    if (!Mpq->ReadFile(TEXT("DBFilesClient\\Holidays.dbc"), Data) || !Parser.Parse(Data))
    {
        UE_LOG(LogWowCalendar, Warning, TEXT("Holidays.dbc not found - no holidays in the calendar"));
        return GHolidays;
    }
    if (Parser.GetFieldCount() != 55)
    {
        UE_LOG(LogWowCalendar, Warning, TEXT("Holidays.dbc has %u fields (expected 55) - no holidays in the calendar"), Parser.GetFieldCount());
        return GHolidays;
    }
    const TMap<int32, FString> Names = LoadLocalizedNames(*Mpq, TEXT("DBFilesClient\\HolidayNames.dbc"));
    const TMap<int32, FString> Descriptions = LoadLocalizedNames(*Mpq, TEXT("DBFilesClient\\HolidayDescriptions.dbc"));
    for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
    {
        FHoliday H;
        H.Id = static_cast<int32>(Parser.GetUInt(R, 0));
        for (int32 I = 0; I < 10; ++I) H.Duration[I] = static_cast<int32>(Parser.GetUInt(R, 1 + I));
        for (int32 I = 0; I < 26; ++I) H.Date[I] = Parser.GetUInt(R, 11 + I);
        H.bLooping = Parser.GetUInt(R, 38) != 0;
        H.Name = Names.FindRef(static_cast<int32>(Parser.GetUInt(R, 49)));
        H.Description = Descriptions.FindRef(static_cast<int32>(Parser.GetUInt(R, 50)));
        H.Texture = Parser.GetString(R, 51);
        H.Priority = static_cast<int32>(Parser.GetUInt(R, 52));
        H.FilterType = static_cast<int32>(Parser.GetUInt(R, 53));
        if (H.Name.IsEmpty() || H.Date[0] == 0) continue;
        // One line per holiday so the dates the client data knows are visible in the log.
        auto Fmt = [](uint32 P) { return ((P >> 24) & 0x1F) == 31
            ? FString::Printf(TEXT("yearly %02u-%02u %02u:%02u"), ((P >> 20) & 0xF) + 1, ((P >> 14) & 0x3F) + 1, (P >> 6) & 0x1F, P & 0x3F)
            : FString::Printf(TEXT("%04u-%02u-%02u %02u:%02u"), ((P >> 24) & 0x1F) + 2000, ((P >> 20) & 0xF) + 1, ((P >> 14) & 0x3F) + 1, (P >> 6) & 0x1F, P & 0x3F); };
        int32 NumDates = 0;
        while (NumDates < 26 && H.Date[NumDates]) ++NumDates;
        UE_LOG(LogWowCalendar, Log, TEXT("Holiday %d '%s': %d dates, first %s, last %s, %d h, looping %d, filter %d, texture %s"),
            H.Id, *H.Name, NumDates, *Fmt(H.Date[0]), *Fmt(H.Date[NumDates - 1]), H.Duration[0], H.bLooping ? 1 : 0, H.FilterType, *H.Texture);
        GHolidays.Add(MoveTemp(H));
    }
    UE_LOG(LogWowCalendar, Log, TEXT("Calendar: %d holidays from Holidays.dbc"), GHolidays.Num());
    return GHolidays;
}

// Start times (realm-local, as unix-like seconds of a local FDateTime) of a holiday's occurrences
// that can touch [From, To): explicit dates, yearly dates (year bits 31), or a looping cycle
// (start = first date + k * sum of the durations).
void HolidayStarts(const FHoliday& H, int64 From, int64 To, int64 Length, TArray<int64>& Out)
{
    auto Decode = [](uint32 P, int32 Year, int64& OutT) -> bool
    {
        const int32 Month = static_cast<int32>((P >> 20) & 0xF) + 1;
        const int32 Day = static_cast<int32>((P >> 14) & 0x3F) + 1;
        const int32 Hour = static_cast<int32>((P >> 6) & 0x1F);
        const int32 Minute = static_cast<int32>(P & 0x3F);
        if (!FDateTime::Validate(Year, Month, Day, Hour, Minute, 0, 0)) return false;
        OutT = FDateTime(Year, Month, Day, Hour, Minute).ToUnixTimestamp();
        return true;
    };
    if (H.bLooping)
    {
        int64 Period = 0;
        for (int32 I = 0; I < 10; ++I) Period += static_cast<int64>(FMath::Max(0, H.Duration[I])) * 3600;
        int64 First = 0;
        const uint32 P = H.Date[0];
        const int32 Year = ((P >> 24) & 0x1F) == 31 ? FDateTime::Now().GetYear() - 1 : static_cast<int32>((P >> 24) & 0x1F) + 2000;
        if (Period < 3600 || !Decode(P, Year, First)) return;
        int64 K = FMath::Max<int64>(0, (From - Length - First) / Period);
        for (int64 T = First + K * Period; T < To; T += Period)
            if (T + Length > From) Out.Add(T);
        return;
    }
    for (int32 I = 0; I < 26 && H.Date[I]; ++I)
    {
        const uint32 P = H.Date[I];
        if (((P >> 24) & 0x1F) == 31)
        {
            const int32 FromYear = FDateTime::FromUnixTimestamp(From).GetYear();
            for (int32 Year = FromYear - 1; Year <= FromYear + 1; ++Year)
            {
                int64 T = 0;
                if (Decode(P, Year, T) && T < To && T + Length > From) Out.AddUnique(T);
            }
        }
        else
        {
            int64 T = 0;
            if (Decode(P, static_cast<int32>((P >> 24) & 0x1F) + 2000, T) && T < To && T + Length > From) Out.AddUnique(T);
        }
    }
}

// The event the calendar has open (CalendarOpenEvent / CalendarGetEventIndex).
int32 GOpenMonthOffset = 0, GOpenDay = 0, GOpenIndex = 0;
double GLastRequest = -100.0;

const FWowCalendarState* State(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx && Ctx->ConnectionManager ? &Ctx->ConnectionManager->PacketHandler.Calendar : nullptr;
}

// The shown month + offset, from the Lua side's CalendarGetMonth (month, year, ...).
bool ShownMonth(lua_State* L, int32 MonthOffset, int32& OutMonth, int32& OutYear)
{
    lua_getglobal(L, "CalendarGetMonth");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
    lua_pushnumber(L, MonthOffset);
    if (lua_pcall(L, 1, 2, 0) != 0) { lua_pop(L, 1); return false; }
    OutMonth = static_cast<int32>(lua_tonumber(L, -2));
    OutYear = static_cast<int32>(lua_tonumber(L, -1));
    lua_pop(L, 2);
    return OutMonth >= 1 && OutMonth <= 12 && OutYear > 1970;
}

bool CVarOn(lua_State* L, const char* Name, bool bDefault)
{
    lua_getglobal(L, "GetCVar");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return bDefault; }
    lua_pushstring(L, Name);
    if (lua_pcall(L, 1, 1, 0) != 0) { lua_pop(L, 1); return bDefault; }
    const char* Value = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
    const bool bOn = Value && Value[0] ? FCStringAnsi::Atoi(Value) != 0 : bDefault;
    lua_pop(L, 1);
    return bOn;
}

// Seconds to add to unix time for the realm's local time (from the packed realm time it sent).
int64 RealmOffset(const FWowCalendarState& Cal)
{
    if (Cal.ServerPackedTime != 0 && Cal.ServerUnix > 0)
    {
        const uint32 P = Cal.ServerPackedTime;
        const int32 Year = static_cast<int32>((P >> 24) & 0x1F) + 2000;
        const int32 Month = static_cast<int32>((P >> 20) & 0xF) + 1;
        const int32 Day = static_cast<int32>((P >> 14) & 0x3F) + 1;
        const int32 Hour = static_cast<int32>((P >> 6) & 0x1F);
        const int32 Minute = static_cast<int32>(P & 0x3F);
        if (FDateTime::Validate(Year, Month, Day, Hour, Minute, 0, 0))
        {
            const int64 Local = FDateTime(Year, Month, Day, Hour, Minute).ToUnixTimestamp();
            return FMath::RoundToInt64(static_cast<double>(Local - Cal.ServerUnix) / 900.0) * 900;
        }
    }
    return static_cast<int64>((FDateTime::Now() - FDateTime::UtcNow()).GetTotalSeconds() + 0.5);
}

FString MapName(uint32 MapId, bool& bOutRaid)
{
    bOutRaid = false;
    if (const FMapDbcEntry* Map = FDbcStore::Get().Maps().GetById(MapId))
    {
        bOutRaid = Map->MapType == 2;
        return Map->Name;
    }
    return FString::Printf(TEXT("Map %u"), MapId);
}

FString DifficultyName(bool bRaid, uint32 Difficulty)
{
    static const TCHAR* const Raid[] = { TEXT("10 Player"), TEXT("25 Player"), TEXT("10 Player (Heroic)"), TEXT("25 Player (Heroic)") };
    static const TCHAR* const Dungeon[] = { TEXT("5 Player"), TEXT("5 Player (Heroic)") };
    if (bRaid) return Difficulty < 4 ? Raid[Difficulty] : TEXT("");
    return Difficulty < 2 ? Dungeon[Difficulty] : TEXT("");
}

TArray<FDayEvent> DayEvents(lua_State* L, int32 MonthOffset, int32 Day)
{
    TArray<FDayEvent> Out;
    const FWowCalendarState* Cal = State(L);
    int32 Month = 0, Year = 0;
    if (Day < 1 || !ShownMonth(L, MonthOffset, Month, Year) || !FDateTime::Validate(Year, Month, Day, 0, 0, 0, 0)) return Out;
    // Server data (events, lockouts, resets) once SMSG_CALENDAR_SEND_CALENDAR has arrived;
    // holidays below come from the client data and need no server answer.
    static const FWowCalendarState Empty;
    const bool bServer = Cal && Cal->bReceived;
    if (!bServer) Cal = &Empty;
    const int64 Offset = bServer ? RealmOffset(*Cal) : 0;
    const int64 DayStart = FDateTime(Year, Month, Day).ToUnixTimestamp() - Offset; // unix of realm midnight
    const int64 DayEnd = DayStart + 86400;
    auto RealmClock = [Offset](int64 Unix, int32& OutHour, int32& OutMinute)
    {
        const FDateTime T = FDateTime::FromUnixTimestamp(Unix + Offset);
        OutHour = T.GetHour();
        OutMinute = T.GetMinute();
    };

    // The player's events (and guild events / announcements).
    for (const FWowCalendarEvent& Event : Cal->Events)
    {
        const uint32 P = Event.PackedTime;
        if (static_cast<int32>((P >> 24) & 0x1F) + 2000 != Year || static_cast<int32>((P >> 20) & 0xF) + 1 != Month
            || static_cast<int32>((P >> 14) & 0x3F) + 1 != Day) continue;
        FDayEvent E;
        E.Title = Event.Title;
        E.Hour = static_cast<int32>((P >> 6) & 0x1F);
        E.Minute = static_cast<int32>(P & 0x3F);
        E.CalendarType = (Event.Flags & 0x400) ? ((Event.Flags & 0x40) ? TEXT("GUILD_ANNOUNCEMENT") : TEXT("GUILD_EVENT")) : TEXT("PLAYER");
        E.EventType = static_cast<int32>(Event.Type) + 1;
        if (const FWowCalendarInvite* Invite = Cal->Invites.FindByPredicate([&](const FWowCalendarInvite& I) { return I.EventId == Event.EventId; }))
        {
            E.InviteStatus = Invite->Status + 1;
            E.ModStatus = Invite->Rank == 2 ? TEXT("CREATOR") : (Invite->Rank == 1 ? TEXT("MODERATOR") : TEXT(""));
        }
        E.InviteType = (Event.Flags & 0x400) && !(Event.Flags & 0x40) ? 2 : 1;
        E.SortKey = E.Hour * 60 + E.Minute;
        Out.Add(E);
    }

    // Raid lockouts end on their reset day.
    if (CVarOn(L, "calendarShowLockouts", true))
    {
        for (const FWowCalendarLockout& Lockout : Cal->Lockouts)
        {
            if (Lockout.ResetUnix < DayStart || Lockout.ResetUnix >= DayEnd) continue;
            bool bRaid = false;
            FDayEvent E;
            E.Title = MapName(Lockout.MapId, bRaid);
            E.CalendarType = TEXT("RAID_LOCKOUT");
            RealmClock(Lockout.ResetUnix, E.Hour, E.Minute);
            E.Difficulty = static_cast<int32>(Lockout.Difficulty);
            E.DifficultyName = DifficultyName(bRaid, Lockout.Difficulty);
            E.RaidId = Lockout.InstanceId;
            E.SortKey = E.Hour * 60 + E.Minute;
            Out.Add(E);
        }
    }

    // The raid reset schedule: origin + offset + k * period.
    if (CVarOn(L, "calendarShowResets", false))
    {
        for (const FWowCalendarRaidReset& Reset : Cal->RaidResets)
        {
            if (Reset.Period <= 0) continue;
            const int64 First = Cal->BaseUnix + Reset.Offset;
            int64 K = (DayStart - First) / Reset.Period;
            for (int64 T = First + K * Reset.Period; T < DayEnd; T += Reset.Period)
            {
                if (T < DayStart) continue;
                bool bRaid = false;
                FDayEvent E;
                E.Title = MapName(Reset.MapId, bRaid);
                E.CalendarType = TEXT("RAID_RESET");
                RealmClock(T, E.Hour, E.Minute);
                E.SortKey = E.Hour * 60 + E.Minute;
                Out.Add(E);
            }
        }
    }
    // Holidays (realm-local dates straight from the data, so no time zone shift).
    {
        const int64 LocalDayStart = FDateTime(Year, Month, Day).ToUnixTimestamp();
        const int64 LocalDayEnd = LocalDayStart + 86400;
        const bool bDarkmoon = CVarOn(L, "calendarShowDarkmoon", true);
        const bool bWeekly = CVarOn(L, "calendarShowWeeklyHolidays", true);
        const bool bBattlegrounds = CVarOn(L, "calendarShowBattlegrounds", true);
        for (const FHoliday& H : Holidays())
        {
            if (!bDarkmoon && H.Texture.Contains(TEXT("Darkmoon"))) continue;
            if (!bBattlegrounds && H.Texture.Contains(TEXT("CallToArms"))) continue;
            if (!bWeekly && H.bLooping) continue;
            const int64 Length = static_cast<int64>(FMath::Max(1, H.Duration[0])) * 3600;
            TArray<int64> Starts;
            HolidayStarts(H, LocalDayStart, LocalDayEnd, Length, Starts);
            for (const int64 Start : Starts)
            {
                const int64 End = Start + Length;
                const bool bStartsToday = Start >= LocalDayStart && Start < LocalDayEnd;
                const bool bEndsToday = End > LocalDayStart && End <= LocalDayEnd;
                FDayEvent E;
                E.Title = H.Name;
                E.CalendarType = TEXT("HOLIDAY");
                E.SequenceType = bStartsToday && bEndsToday ? TEXT("") : (bStartsToday ? TEXT("START") : (bEndsToday ? TEXT("END") : TEXT("ONGOING")));
                const FDateTime Clock = FDateTime::FromUnixTimestamp(bStartsToday || !bEndsToday ? Start : End);
                E.Hour = Clock.GetHour();
                E.Minute = Clock.GetMinute();
                E.Texture = H.Texture;
                E.HolidayId = H.Id;
                E.SortKey = -1000 + H.Priority; // holidays first, like the client
                Out.Add(E);
            }
        }
    }
    Out.StableSort([](const FDayEvent& A, const FDayEvent& B) { return A.SortKey < B.SortKey; });
    return Out;
}

void PushString(lua_State* L, const FString& S)
{
    FTCHARToUTF8 Utf8(*S);
    lua_pushstring(L, Utf8.Get());
}

int L_OpenCalendar(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (Ctx && Ctx->ConnectionManager && FPlatformTime::Seconds() - GLastRequest > 2.0)
    {
        GLastRequest = FPlatformTime::Seconds();
        Ctx->ConnectionManager->PacketHandler.RequestCalendar();
    }
    return 0;
}

int L_GetNumDayEvents(lua_State* L)
{
    const int32 MonthOffset = static_cast<int32>(luaL_optnumber(L, 1, 0));
    const int32 Day = static_cast<int32>(luaL_optnumber(L, 2, 0));
    lua_pushnumber(L, DayEvents(L, MonthOffset, Day).Num());
    return 1;
}

// title, hour, minute, calendarType, sequenceType, eventType, texture, modStatus, inviteStatus,
// invitedBy, difficulty, inviteType, sequenceIndex, numSequenceDays, difficultyName
int L_GetDayEvent(lua_State* L)
{
    const int32 MonthOffset = static_cast<int32>(luaL_optnumber(L, 1, 0));
    const int32 Day = static_cast<int32>(luaL_optnumber(L, 2, 0));
    const int32 Index = static_cast<int32>(luaL_optnumber(L, 3, 0));
    const TArray<FDayEvent> Events = DayEvents(L, MonthOffset, Day);
    if (!Events.IsValidIndex(Index - 1)) return 0;
    const FDayEvent& E = Events[Index - 1];
    PushString(L, E.Title);
    lua_pushnumber(L, E.Hour);
    lua_pushnumber(L, E.Minute);
    PushString(L, E.CalendarType);
    PushString(L, E.SequenceType);
    lua_pushnumber(L, E.EventType);
    PushString(L, E.Texture);
    PushString(L, E.ModStatus);
    lua_pushnumber(L, E.InviteStatus);
    lua_pushstring(L, "");
    lua_pushnumber(L, E.Difficulty);
    lua_pushnumber(L, E.InviteType);
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 1);
    PushString(L, E.DifficultyName);
    return 15;
}

int L_GetDayEventSequenceInfo(lua_State* L)
{
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 1);
    return 2;
}

int L_OpenEvent(lua_State* L)
{
    const int32 MonthOffset = static_cast<int32>(luaL_optnumber(L, 1, 0));
    const int32 Day = static_cast<int32>(luaL_optnumber(L, 2, 0));
    const int32 Index = static_cast<int32>(luaL_optnumber(L, 3, 0));
    const TArray<FDayEvent> Events = DayEvents(L, MonthOffset, Day);
    if (!Events.IsValidIndex(Index - 1)) return 0;
    const FDayEvent& E = Events[Index - 1];
    // Only lockouts and resets open here (CalendarViewRaidFrame); player events need the event's
    // details from the server (CMSG_CALENDAR_GET_EVENT), which is not in yet.
    if (E.CalendarType != TEXT("RAID_LOCKOUT") && E.CalendarType != TEXT("RAID_RESET") && E.CalendarType != TEXT("HOLIDAY")) return 0;
    GOpenMonthOffset = MonthOffset; GOpenDay = Day; GOpenIndex = Index;
    if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem)
        Ctx->EventSystem->FireEvent(TEXT("CALENDAR_OPEN_EVENT"), { E.CalendarType });
    return 0;
}

int L_GetEventIndex(lua_State* L)
{
    if (GOpenIndex <= 0) return 0;
    lua_pushnumber(L, GOpenMonthOffset);
    lua_pushnumber(L, GOpenDay);
    lua_pushnumber(L, GOpenIndex);
    return 3;
}

int L_CloseEvent(lua_State* L)
{
    GOpenMonthOffset = GOpenDay = GOpenIndex = 0;
    return 0;
}

// name, calendarType, raidID, hour, minute, difficulty, difficultyName
int L_GetRaidInfo(lua_State* L)
{
    const int32 MonthOffset = static_cast<int32>(luaL_optnumber(L, 1, 0));
    const int32 Day = static_cast<int32>(luaL_optnumber(L, 2, 0));
    const int32 Index = static_cast<int32>(luaL_optnumber(L, 3, 0));
    const TArray<FDayEvent> Events = DayEvents(L, MonthOffset, Day);
    if (!Events.IsValidIndex(Index - 1)) return 0;
    const FDayEvent& E = Events[Index - 1];
    PushString(L, E.Title);
    PushString(L, E.CalendarType);
    lua_pushnumber(L, static_cast<double>(E.RaidId));
    lua_pushnumber(L, E.Hour);
    lua_pushnumber(L, E.Minute);
    lua_pushnumber(L, E.Difficulty);
    PushString(L, E.DifficultyName);
    return 7;
}

// name, description, texture
int L_GetHolidayInfo(lua_State* L)
{
    const int32 MonthOffset = static_cast<int32>(luaL_optnumber(L, 1, 0));
    const int32 Day = static_cast<int32>(luaL_optnumber(L, 2, 0));
    const int32 Index = static_cast<int32>(luaL_optnumber(L, 3, 0));
    const TArray<FDayEvent> Events = DayEvents(L, MonthOffset, Day);
    if (!Events.IsValidIndex(Index - 1) || Events[Index - 1].CalendarType != TEXT("HOLIDAY")) return 0;
    const FDayEvent& E = Events[Index - 1];
    const FHoliday* H = Holidays().FindByPredicate([&](const FHoliday& X) { return X.Id == E.HolidayId; });
    PushString(L, E.Title);
    PushString(L, H ? H->Description : FString());
    PushString(L, E.Texture);
    return 3;
}

int L_GetNumPendingInvites(lua_State* L)
{
    const FWowCalendarState* Cal = State(L);
    int32 Count = Cal ? Cal->NumPendingInvites : 0;
    if (Cal && Cal->bReceived)
    {
        int32 Invited = 0;
        for (const FWowCalendarInvite& Invite : Cal->Invites) if (Invite.Status == 0) ++Invited;
        Count = FMath::Max(Count, Invited);
    }
    lua_pushnumber(L, Count);
    return 1;
}

// r265 (Stefan 12.10, calendar "not right"): CalendarGetFirstPendingInvite(monthOffset, day) was an
// empty placeholder returning nothing; CalendarFrame_UpdateDayEvents compares it with 0, so the
// month update stopped at the first day (no today mark, empty event slots left showing). Index of
// the day's first event still waiting for an answer (CALENDAR_INVITESTATUS_INVITED = 1), else 0.
int L_GetFirstPendingInvite(lua_State* L)
{
    const int32 MonthOffset = static_cast<int32>(luaL_optnumber(L, 1, 0));
    const int32 Day = static_cast<int32>(luaL_optnumber(L, 2, 0));
    const TArray<FDayEvent> Events = DayEvents(L, MonthOffset, Day);
    int32 First = 0;
    for (int32 I = 0; I < Events.Num() && !First; ++I)
        if (Events[I].InviteStatus == 1 && Events[I].CalendarType != TEXT("HOLIDAY") && Events[I].CalendarType != TEXT("RAID_LOCKOUT")
            && Events[I].CalendarType != TEXT("RAID_RESET")) First = I + 1;
    lua_pushnumber(L, First);
    return 1;
}
}

/** Called from LuaStubs.cpp after the month functions and the empty placeholders. */
void WowRegisterCalendarServerApi(lua_State* L)
{
    lua_register(L, "OpenCalendar", L_OpenCalendar);
    lua_register(L, "CalendarGetNumDayEvents", L_GetNumDayEvents);
    lua_register(L, "CalendarGetDayEvent", L_GetDayEvent);
    lua_register(L, "CalendarGetDayEventSequenceInfo", L_GetDayEventSequenceInfo);
    lua_register(L, "CalendarOpenEvent", L_OpenEvent);
    lua_register(L, "CalendarGetEventIndex", L_GetEventIndex);
    lua_register(L, "CalendarCloseEvent", L_CloseEvent);
    lua_register(L, "CalendarGetRaidInfo", L_GetRaidInfo);
    lua_register(L, "CalendarGetHolidayInfo", L_GetHolidayInfo);
    lua_register(L, "CalendarGetNumPendingInvites", L_GetNumPendingInvites);
    lua_register(L, "GetNumPendingCalendarInvites", L_GetNumPendingInvites);
    lua_register(L, "CalendarGetFirstPendingInvite", L_GetFirstPendingInvite);
}
#endif
