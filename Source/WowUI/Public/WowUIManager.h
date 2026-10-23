#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WowLuaVM.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowFontManager.h"
#include "WowInventoryManager.h"
#include "SWowBagWindow.h"
#include "SWowBankWindow.h"
#include "SWowCharacterPanel.h"
#include "WowUIManager.generated.h"

struct FWowLuaContext;

class FMpqManager;
class FWowAssetCache;
class UCanvasPanel;
class UWowConnectionManager;
class AWowAudioManager;

/**
 * GameInstanceSubsystem that owns the WoW UI systems (Lua VM, FrameManager, EventSystem).
 * Initialize() creates the Lua VM and wires the systems.
 * LoadUI() loads FrameXML and addons from MPQ once the data is available.
 */
UCLASS()
class WOWUI_API UWowUIManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UWowUIManager();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Load FrameXML and addons from MPQ — call once MpqManager and AssetCache are available */
	void LoadUI(FMpqManager* Mpq, FWowAssetCache* AssetCache);

	/** Set the root UMG canvas for frame rendering */
	void SetRootCanvas(UCanvasPanel* Canvas);

	/** Set connection manager for character stats */
	void SetConnectionManager(UWowConnectionManager* InConnectionManager);
	void SetAudioManager(AWowAudioManager* InAudioManager);
    bool RunOriginalBinding(const FString& Command,bool Pressed);
    void SetBindingHandler(TFunction<bool(const FString&,bool)> Handler);

	/** Accessors */
	FWowLuaVM* GetLuaVM() const { return LuaVM.Get(); }
	FWowFrameManager* GetFrameManager() const { return FrameManager.Get(); }
	FWowEventSystem* GetEventSystem() const { return EventSystem.Get(); }
	FWowFontManager* GetFontManager() const { return FontManager.Get(); }
	TSharedPtr<FWowInventoryManager> GetInventoryManager() const { return InventoryManager; }
	UWowConnectionManager* GetConnectionManager() const { return ConnectionManager.Get(); }

	bool IsUILoaded() const { return bUILoaded; }

	/** Lua ReloadUI(): handled on the next game tick (the VM cannot be rebuilt from inside Lua). */
	static void RequestReloadUI();
	/** Lua ShowAddonManager() (Game Menu "AddOns" button): opens the native AddOns list. */
	static void RequestAddonManager();
	static bool ConsumeReloadRequest();
	static bool ConsumeAddonManagerRequest();
	/** Save variables, rebuild Lua VM + frames, reload FrameXML and the enabled addons. */
	bool ReloadUI();
	/** A FrameXML GlobalStrings value (e.g. SPELL_FAILED_INTERRUPTED), empty if not defined. */
	FString GetGlobalString(const FString& Name) const;

	/** UI Window Management */
	void ToggleBagWindow();
	void HideBagWindow();
	bool IsBagWindowVisible() const;
	void ToggleCharacterPanel();
	void HideCharacterPanel();
	bool IsCharacterPanelVisible() const;
	void ShowBankWindow();
	void HideBankWindow();
	bool IsBankWindowVisible() const;

	/** Update inventory from player entity */
	void UpdateInventory();
	void BeginLocalSpellProgress(uint32 SpellId, const FString& SpellName,
		float DurationSeconds, bool bChanneling);
	void UpdateLocalSpellProgress(float RemainingSeconds);
	void EndLocalSpellProgress();
	void BeginUnitSpellProgress(uint64 UnitGuid, uint32 SpellId, const FString& SpellName,
		float DurationSeconds, bool bChanneling);
	void UpdateUnitSpellProgress(uint64 UnitGuid, float RemainingSeconds);
	void DelayUnitSpellProgress(uint64 UnitGuid, float DelaySeconds);
	void EndUnitSpellProgress(uint64 UnitGuid);
	/** A unit's current cast/channel (nameplate cast bars). Times are FPlatformTime milliseconds. */
	bool GetUnitSpellProgress(uint64 UnitGuid, FString& OutSpellName, FString& OutIconPath,
		double& OutStartMs, double& OutEndMs, bool& bOutChanneling) const;
	uint64 GetFocusGuid() const;
	/** World mouseover: sets the "mouseover" unit token and fires UPDATE_MOUSEOVER_UNIT. */
	void SetMouseoverGuid(uint64 Guid);
	void RefreshUnitFrames();

private:
	void CreateCoreSystems();
	FMpqManager* LoadedMpq = nullptr;
	FWowAssetCache* LoadedAssetCache = nullptr;

	TUniquePtr<FWowLuaVM> LuaVM;
	TUniquePtr<FWowFrameManager> FrameManager;
	TUniquePtr<FWowEventSystem> EventSystem;
	TUniquePtr<FWowFontManager> FontManager;
	TSharedPtr<FWowInventoryManager> InventoryManager;
	TObjectPtr<UWowConnectionManager> ConnectionManager;
	TObjectPtr<AWowAudioManager> AudioManager;

	FWowLuaContext* UIContext = nullptr;
	bool bUILoaded = false;
    TMap<FString,FString> BindingScripts;
    TSet<FString> BindingRunsOnRelease;

	// UI Windows
	TSharedPtr<SWowBagWindow> BagWindow;
	TSharedPtr<SWowBankWindow> BankWindow;
	TSharedPtr<SWowCharacterPanel> CharacterPanel;
	TSharedPtr<class SOverlay> UIOverlay;
};
