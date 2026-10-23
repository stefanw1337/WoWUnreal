#pragma once
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/Texture2D.h"
#include "WowCursorManager.generated.h"

class FMpqManager;
class FWowAssetCache;

/** Original cursor artwork, shared by hardware and software presentation. */
UENUM(BlueprintType)
enum class EWowCursorType : uint8
{
    Point,          // Default cursor
    Attack,         // When hovering attackable target
    Speak,          // When hovering NPC
    Buy,            // Vendor
    Quest,          // Quest giver
    Inspect,        // Inspectable
    Pickup,         // Lootable
    UnableAttack,   // Cannot attack
    UnablePoint,    // Cannot interact
    Cast,
    Interact,
    QuestTurnIn,    // Quest giver with a quest ready to hand in (yellow "?")
    // r333 (Stefan 14.10): the grey "Unable" cursors the client shows over something out of reach.
    UnableInteract, UnableSpeak, UnableBuy, UnableQuest, UnableQuestTurnIn, UnablePickup,
    // r353 (Stefan 15.10): a cursor set's target cursor per reaction - Attack is the hostile (red) one,
    // these the neutral (yellow) and friendly (green) ones; without them they are the Attack cursor.
    AttackNeutral, AttackFriendly
};

/**
 * Manages WoW-style custom cursor textures.
 */
UCLASS(BlueprintType)
class WOWUNREAL_API UWowCursorManager : public UObject
{
    GENERATED_BODY()

public:
    UWowCursorManager();
    virtual void BeginDestroy() override;
    bool HasOriginalCursors() const { return CursorPixels.Contains(EWowCursorType::Point); }
    bool ApplyHardwareCursor(int32 Size);
    void ReleaseHardwareCursors();
    // A small CPU copy is required by the OS cursor API; never shipped/extracted.
    // HotX/HotY: the click point as a fraction of the image (r351: a cursor set's own; < 0 = the
    // gauntlet's fingertip of the client's art).
    // r352: an animated cursor (.ani) keeps all its frames (same size); RGBA is the first one.
    struct FCursorFrame { TArray<uint8> RGBA; float Seconds = 0.1f; };
    struct FCursorPixels
    {
        int32 Width = 0, Height = 0; TArray<uint8> RGBA; float HotX = -1.f, HotY = -1.f;
        TArray<FCursorFrame> Frames;    // empty = still
        TArray<int32> FrameTextures;    // index into AnimTextures per frame
    };
    const FCursorPixels* GetPixels(EWowCursorType Type) const { return CursorPixels.Find(Type); }
    void* GetHardwareHandle(EWowCursorType Type) const { void* const* H = HardwareCursors.Find(int32(Type) << 8); return H ? *H : nullptr; }


    /** r351 (Stefan 15.10): cursor sets - each folder in <client>\User\Cursor is a set, chosen under
     *  Interface > Mouse > Experimental. "" = the client's own cursors. */
    static FString GetCursorSetsFolder();
    static TArray<FString> ListCursorSets();
    static FString GetActiveCursorSet();
    static void SetActiveCursorSet(const FString& SetName);
    /** The chosen set is not the one loaded, or the character's race changed while a set is used
     *  (sets can have a folder per race, r352): LoadCursors() again (the controller does, every tick). */
    bool NeedsReload() const { return LoadedCursorSet != GetActiveCursorSet() || (!LoadedCursorSet.IsEmpty() && LoadedRace != PlayerRace); }
    /** r352: the character's race (ChrRaces id; 0 = none yet) picks a set's race folder. */
    void SetPlayerRace(uint8 RaceId) { PlayerRace = RaceId; }
    /** r352: the cursor is animated (its texture changes over time; ask GetCursorTexture each frame). */
    bool IsAnimated(EWowCursorType Type) const;
    /** The click point of a cursor as a fraction of its image. */
    FVector2D GetHotspot(EWowCursorType Type) const;

    /** Initialize with MPQ manager and asset cache */
    void Initialize(FMpqManager* Mpq, FWowAssetCache* Cache);

    /** Load all cursor textures from MPQ */
    void LoadCursors();

    /** Set the current cursor type */
    UFUNCTION(BlueprintCallable, Category = "Cursor")
    void SetCursorType(EWowCursorType CursorType);

    /** Get cursor texture for a specific type */
    UFUNCTION(BlueprintCallable, Category = "Cursor")
    UTexture2D* GetCursorTexture(EWowCursorType CursorType) const;

    /** Get the active cursor state. */
    UFUNCTION(BlueprintCallable, Category = "Cursor")
    EWowCursorType GetCursorType() const { return CurrentCursorType; }

private:
    /** Load a project-local fallback cursor texture. */
    UTexture2D* LoadFallbackCursorTexture(EWowCursorType CursorType);

    /** Load a single cursor texture */
    UTexture2D* LoadCursorTexture(const FString& CursorName);

    /** Get the filename for a cursor type */
    FString GetCursorFilename(EWowCursorType CursorType) const;

    /** Get the project-local fallback filename for a cursor type. */
    FString GetFallbackCursorFilename(EWowCursorType CursorType) const;

    /** r351/r352: replace one cursor with the active set's file, if the set has it (folders in order of preference). */
    bool LoadSetCursor(EWowCursorType CursorType, const TArray<FString>& Folders, const FString& FactionPrefix);

    /** r352: the frame of an animated cursor shown now (0 for a still one). */
    int32 CurrentFrame(const FCursorPixels& Pixels) const;

    /** The set LoadCursors() last used ("" = none), and the race it was loaded for. */
    FString LoadedCursorSet;
    uint8 PlayerRace = 0;
    uint8 LoadedRace = 0;

    TMap<EWowCursorType, FCursorPixels> CursorPixels;
    TMap<int32, void*> HardwareCursors; // (type << 8) | frame
    int32 HardwareSize = 0;
    void* ActiveHardware = nullptr;

    /** MPQ manager for loading files */
    FMpqManager* MpqManager = nullptr;

    /** Asset cache for texture caching */
    FWowAssetCache* AssetCache = nullptr;

    /** Loaded cursor textures */
    UPROPERTY()
    TMap<EWowCursorType, TObjectPtr<UTexture2D>> CursorTextures;

    /** r352: the frames of animated set cursors. */
    UPROPERTY()
    TArray<TObjectPtr<UTexture2D>> AnimTextures;

    /** Currently active cursor type */
    UPROPERTY()
    EWowCursorType CurrentCursorType = EWowCursorType::Point;
};
