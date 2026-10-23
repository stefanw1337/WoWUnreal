#pragma once
#include "CoreMinimal.h"

class FMpqManager;

/** How one movie is shown. */
struct FWowMovieOptions
{
    /** Esc, Space, Enter or a click ends it. */
    bool bSkippable = true;
    /** In the world: world audio is ducked while it plays (glue screens: the login music stops
        and comes back afterwards). */
    bool bInWorld = false;
    /** Shown in the log only (who asked for it: "login", "class intro", a future server trigger). */
    FString Reason;
};

/**
 * Full-screen movies from the game data of the server being played (r378, made generic in r380).
 *
 * Movies are named by a movie id: letters, digits, '_' and '-', at most 64 characters, no
 * extension and no folders (anything else is refused and logged). Id "X" is looked up as
 *  1. a loose file X.mp4 / X.avi in Data\<locale>\Interface\Cinematics, then Data\Interface\Cinematics;
 *  2. Interface\Cinematics\X.mp4 / X.avi inside the server's MPQs. Media players cannot read
 *     MPQs, so it is copied once to Saved\Cinematics\<data folder hash>\ (again when an MPQ changes).
 * Blizzard-style AVIs tagged "DXGM" get a copy tagged XVID so Windows can decode them.
 * Subtitles (optional): X.srt (UTF-8) next to the movie or in the MPQs, else Blizzard's X.sbt.
 * They are shown while the world option "moviesubtitles" is on (Options > Sound > Cinematic Subtitles, r223).
 *
 * Server rules: a server's data may carry Interface\Cinematics\MovieRules.txt (loose or in an
 * MPQ), "Key=Value" lines, '#' comments:
 *   LoginMovie=<id> | none      movie before character select (default: ServerIntro, then the
 *                               expansion trailer WOW_Intro[_BC|_LK]_1024/_800)
 *   ClassIntro=<pattern> | none first-login class intro (default ClassIntro_{Class}_{Gender});
 *                               {Class} = the class name from the server's ChrClasses.dbc (letters
 *                               and digits only: "Death Knight" -> DeathKnight), {Gender} = Male/Female,
 *                               {Race} = ChrRaces client file name (Human, NightElf, Scourge ...)
 *   ClassIntro.<Class>=<pattern> | none          r381: override for one class (class name as above)
 *   ClassIntro.<Class>.<Gender>=<id> | none      r381: override for one class and gender
 *   ClassIntroFlyby=replace | after   the race flyby is replaced by the class intro (default) or
 *                               flies after it
 * Example (TormentWoW): ClassIntro=D3_{Class}_{Gender} -> D3_Barbarian_Female, D3_Wizard_Male ...
 *
 * A missing movie or one that cannot be decoded (nothing shown after 6 s) is logged, closed and
 * the game goes on. One movie at a time: a request while one plays waits for it.
 * Login movie: Options > Sound > Intro Movie (r222): every time / first time only (default) / never;
 * world option "skipintromovies" also turns it off.
 */
class FWowTrailerPlayer
{
public:
    /** Before character select, once per server data folder and movie. False (nothing happens,
        OnDone not called) when there is nothing new to play. */
    static bool PlayIntroOnce(FMpqManager* Mpq, int32 Expansion, TFunction<void()> OnDone);

    /** Reusable entry point (class intros, future server/campaign triggers). True when it plays or
        is queued: OnDone(bPlayed) then runs exactly once (bPlayed false when it failed to decode).
        False when the id is invalid or the movie is missing (logged; OnDone is not called). */
    static bool PlayMovie(FMpqManager* Mpq, const FString& MovieId, const FWowMovieOptions& Options, TFunction<void(bool bPlayed)> OnDone);

    static bool IsValidMovieId(const FString& MovieId);
    /** True when the movie exists (loose or in the MPQs); nothing is extracted. */
    static bool MovieExists(FMpqManager* Mpq, const FString& MovieId);
    /** A value from the server's MovieRules.txt, or Default. */
    static FString GetServerRule(FMpqManager* Mpq, const FString& Key, const FString& Default);
    /** The class intro movie id for this character from the server rule, or empty. */
    static FString FindClassIntro(FMpqManager* Mpq, uint8 Race, uint8 Class, uint8 Gender);

    static bool IsPlaying();
    /** User skip (ignored for a movie that is not skippable). */
    static void Skip();
};
