#include "WowClientPreferences.h"
#include "RHI.h"
#include "DLSSLibrary.h"
#include "StreamlineLibraryDLSSG.h"
#include "StreamlineLibraryReflex.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"

#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowClientPreferences, Log, All);

namespace
{
constexpr TCHAR PreferencesSection[] = TEXT("WowUnreal.ClientPreferences");
constexpr TCHAR DarkerLightingKey[] = TEXT("DarkerRealisticLighting");
constexpr TCHAR EntityCollisionKey[] = TEXT("NpcAndPlayerCollision");
constexpr TCHAR FirstPersonImmersionKey[] = TEXT("FirstPersonImmersion");
constexpr TCHAR FirstPersonHelmetViewKey[] = TEXT("FirstPersonHelmetView");
constexpr TCHAR MasterVolumeKey[] = TEXT("MasterVolume");
constexpr TCHAR MusicVolumeKey[] = TEXT("MusicVolume");
constexpr TCHAR SfxVolumeKey[] = TEXT("SfxVolume");
constexpr TCHAR AmbienceVolumeKey[] = TEXT("AmbienceVolume");
constexpr TCHAR PreferredRefreshRateKey[] = TEXT("PreferredRefreshRate");
constexpr TCHAR DepthOfFieldKey[] = TEXT("DepthOfField");
constexpr TCHAR MotionBlurKey[] = TEXT("MotionBlur");
constexpr TCHAR AntiAliasingMethodKey[] = TEXT("AntiAliasingMethod");
constexpr TCHAR FsrQualityModeKey[] = TEXT("FsrQualityMode");
constexpr TCHAR FrameGenerationKey[] = TEXT("FrameGeneration");
constexpr TCHAR PerformanceTimingsKey[] = TEXT("PerformanceTimings");
constexpr TCHAR PerformanceRenderingStatusKey[] = TEXT("PerformanceRenderingStatus");

TMap<FString,bool> WorldOptions = {
    {TEXT("manualdof"),false},
    {TEXT("moviesubtitles"),true}, // r223: subtitles on intro movies (the .sbt files in the MPQs)
    {TEXT("showhelm"),true}, {TEXT("showcloak"),true}, // r224: mirror of the server's player flags (set from them)
    {TEXT("questsparkles"),true},
    {TEXT("rootedstandup"),false}, // Experimental: moving while seated plays the stand-up clip rooted
    {TEXT("realisticmoon"),false}, // Experimental: real moon rise/set for the date's phase
    {TEXT("vrmode"),false}, // Experimental: seated VR preview (OpenXR); also on with -vr
    {TEXT("vrfirstperson"),true}, // VR: always first person while the headset view is on
    {TEXT("vrmousepointer"),false}, // VR: the mouse pointer also aims in the world (off: a gaze dot, the head aims)
    {TEXT("vrdwellclick"),false}, // VR (experimental): resting the crosshair on something clicks it
    {TEXT("mountpreview"),true}, // r300 Experimental: Ctrl-click a mount shows it in the dressing room (not in 3.3.5)
    {TEXT("hdnpcs"),false}, // r331 Experimental: humanoid NPCs swap to the HD model one at a time
    {TEXT("hdplayers"),false}, // r331 Experimental: other players swap to the HD model one at a time
    {TEXT("hdcharacters"),false}, // Experimental: own character uses the HD model (patch-H.MPQ + HD_Data via HDX\\ paths)
    {TEXT("immersiveui"),false}, // r257 Experimental: the interface fades while moving (minimap faint) and in combat (bars + unit frames at 40 %)
    {TEXT("lootbeam"),false}, // Experimental: light beam over lootable corpses (idea: wxl-loot-beam by manatactical); off = WoW's loot sparkles
    {TEXT("autosellgrey"),false}, // Vendor window: sell grey items when it opens
    {TEXT("autorepair"),false}, // Vendor window: repair everything at armorers when it opens
    {TEXT("alwayscompareitems"),false}, // cvar alwaysCompareItems: compare without holding the compare key (Ctrl)
    {TEXT("floatingcombattextdamage"),true},
    {TEXT("combattextsum"),false},
    {TEXT("combattextsumheals"),false}, // r279: add up heals you receive (and resource gains) per source window
    {TEXT("combattextresource"),true}, // r279: "+25 Mana" etc. when a spell gives you mana/rage/energy/runic power
    {TEXT("weatherlens"),true},
    {TEXT("ambientweather"),true},
    {TEXT("wetlens"),true},
    {TEXT("frostlens"),false},
    {TEXT("cumulusclouds"),true}, // r514: our volumetric cumulus clouds (off: UE's simple cloud layer)
    {TEXT("sunrays"),true}, // r513: sun glare rays, light shafts and lens flare
    {TEXT("shortrangeao"),true}, // r523: Lumen short-range (contact) ambient occlusion
    {TEXT("portalculling"),true}, // r539/r547: WMO rooms and districts drawn only when seen through their portals, like the original
    {TEXT("hidekeepbackdrop"),true}, // r562: hide the low-detail mountain over Stormwind Keep while inside the city
    // r740 Experimental (Stefan 23.10, before the release: "Maybe walls is fine but ground needs more work"): the stone relief
    // split in two - stone and brick walls on, ground (WMO floors, streets, sidewalks, terrain cobbles) off until it is reworked.
    {TEXT("stonereliefwalls"),true}, {TEXT("stonereliefground"),false},
    {TEXT("extremeweather"),false}, // r495 Experimental: weather may go above 30 % (blizzards, sandstorms, thunderstorms at full strength)
    {TEXT("satelliterecord"),true}, // r295: record Satellite v2 world-map tiles while playing
    {TEXT("headlook"),true}, // r292: your character turns its head towards the target / where the camera looks
    {TEXT("instantquesttext"),true},
    {TEXT("autoquestwatch"),true},
    {TEXT("chatbubbles"),true}, {TEXT("chatbubblesparty"),false}, // cvars chatBubbles / chatBubblesParty // cvar autoQuestWatch: newly accepted quests are tracked
    {TEXT("autotrackzonequests"),false}, // track every quest of the zone you are in
    {TEXT("nativequesttracker"),false}, // r339: the old native objectives tracker instead of the stock WatchFrame
    // r383: Claude-Perf's experimental options (r605/r610), so Options can switch them. All off = defaults.
    {TEXT("stealthclassic"),false}, {TEXT("stealthpredator"),false}, {TEXT("stealthshadowonly"),false}, // stealth look; none = shadow cloak
    {TEXT("stealthdetectrange"),false}, // enemy detection cones while stealthed
    {TEXT("noautoretaliate"),false}, // off = attack back when attacked, like the client
    {TEXT("isometriccamera"),false}, // fixed high Diablo-style camera
    {TEXT("showallspellranks"),true}, // r398: cvar ShowAllSpellRanks (spellbook "Show all ranks"), kept between sessions; on like the client
    {TEXT("isodontstarve"),false}, // r395: Don't Starve camera (with isometriccamera on: fixed top-down, no zoom/turn)
    {TEXT("showquestlevel"),true},
    {TEXT("holdtorecast"),true}, // a held action-bar key casts again when the action is ready // "[12] Title" in the quest log and objectives tracker
    {TEXT("soundenabled"),true}, {TEXT("soundeffects"),true},
    {TEXT("musicenabled"),true}, {TEXT("ambientenabled"),true},
    {TEXT("loopmusic"),true}, {TEXT("soundinbackground"),false},
    {TEXT("autolootdefault"),false}, {TEXT("lootundermouse"),false}, {TEXT("tooltipatcursor"),false},
    {TEXT("showtargetoftarget"),true}, {TEXT("playerstatustext"),true}, {TEXT("statustextpercentage"),false},
    {TEXT("nameplateshowenemies"),true}, {TEXT("nameplateshowfriends"),true},
    {TEXT("unitnamenpc"),false}, {TEXT("unitnameinteractablenpc"),true},
    {TEXT("unitnamemonster"),true}, {TEXT("unitnamefriendlyplayername"),true},
    {TEXT("unitnameenemyplayername"),true}, {TEXT("unitnamenoncombatcreaturename"),true},
    {TEXT("actionbar3"),true}, {TEXT("actionbar4"),true},
    {TEXT("actionbar5"),true}, {TEXT("actionbar6"),true}, {TEXT("alwaysshowactionbars"),true}, {TEXT("showcooldowntime"),false}
};
int32 NameplateMode = 0; // names only, names and bars, target only
int32 AutoLootKey = 0; // Shift, Ctrl, Alt, None
int32 IntroMovieMode = 1; // every time, first time only, never
bool bPreferencesLoaded = false;
bool bDarkerRealisticLighting = false;
bool bNpcAndPlayerCollision = false;
bool bFirstPersonImmersion = false;
bool bFirstPersonHelmetView = false;
float DisplayGamma = 2.2f;
float InterfaceScale = 1.0f;
float VrInterfaceScale = 1.0f; // own UI scale while the interface is in the headset
bool bVrUiMode = false;
float CameraMouseSensitivity = 1.0f;
float MouseCursorScale = 1.0f;
float TooltipScale = 1.0f;
float TooltipFadeTime = 2.0f;
float UnitFrameScale = 1.0f;
float ActionBarScale = 1.0f;
float MinimapScale = 1.0f;
float MoonScale = 2.5f;
float VrDwellTime = 1.0f;
float SunScale = 1.0f;
float MasterVolume = 1.0f;
float MusicVolume = 0.5f;
float SfxVolume = 0.75f;
float AmbienceVolume = 0.5f;
int32 PreferredRefreshRate = 60;
bool bDepthOfField = false;
float DepthOfFieldStrength = 2.0f;
float FocusTestDistance = 10.0f;
float FocusTestWidth = 0.5f;
bool bVolumetricFog = false;
bool bLumen = false;
bool bHardwareRayTracing = false;
bool bMotionBlur = false;
int32 AntiAliasingMethod = 3;
int32 FsrQualityMode = 1;
int32 DlssQualityMode = 1;
int32 NvidiaFrameGenerationMode = 17; // Official On2X enum value; conservative default.
bool bFrameGeneration = false;
bool bGameplayRendering = false;
bool bNvidiaForcedVsyncOff = false;
// Adaptive sync state (TickAdaptiveSync).
bool bAdaptiveSyncReleased = false;
float AdaptiveSyncSlowSeconds = 0.0f;
float AdaptiveSyncFastSeconds = 0.0f;
float AdaptiveSyncWorkMs = 0.0f;
float AdaptiveSyncFrameMs = 0.0f;
double AdaptiveSyncReleasedAt = 0.0; // r503
int32 AdaptiveSyncHz = 60;
TAutoConsoleVariable<int32> CVarAdaptiveSync(TEXT("wow.AdaptiveSync"), 1,
    TEXT("1 = release VSync while frames miss the refresh (no 60->30 halving), 0 = plain VSync."));
int32 QueryDisplayRefreshHz()
{
#if PLATFORM_WINDOWS
    DEVMODEW Mode = {};
    Mode.dmSize = sizeof(Mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &Mode) && Mode.dmDisplayFrequency > 1)
        return static_cast<int32>(Mode.dmDisplayFrequency);
#endif
    return 0;
}
bool bPerformanceTimings = false;
bool bPerformanceRenderingStatus = false;

void LoadPreferences()
{
	if (bPreferencesLoaded)
	{
		return;
	}

	bPreferencesLoaded = true;
	if (GConfig)
	{
        GConfig->GetInt(PreferencesSection,TEXT("WorldLabelMode"),NameplateMode,GGameUserSettingsIni);
        NameplateMode=FMath::Clamp(NameplateMode,0,2);
        GConfig->GetInt(PreferencesSection,TEXT("AutoLootKey"),AutoLootKey,GGameUserSettingsIni);
        AutoLootKey=FMath::Clamp(AutoLootKey,0,3);
        GConfig->GetInt(PreferencesSection,TEXT("IntroMovieMode"),IntroMovieMode,GGameUserSettingsIni);
        IntroMovieMode=FMath::Clamp(IntroMovieMode,0,2);
        for(auto& Option : WorldOptions)
            GConfig->GetBool(PreferencesSection, *Option.Key, Option.Value, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, DarkerLightingKey,
			bDarkerRealisticLighting, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, EntityCollisionKey,
			bNpcAndPlayerCollision, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, FirstPersonImmersionKey,
			bFirstPersonImmersion, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, FirstPersonHelmetViewKey,
			bFirstPersonHelmetView, GGameUserSettingsIni);
		// "VR: first person only" needs first-person immersion (Stefan, r222): an old
		// config with the VR option on and immersion off gets immersion on.
		if (const bool* VRFirstPerson = WorldOptions.Find(TEXT("vrfirstperson")); VRFirstPerson && *VRFirstPerson)
			bFirstPersonImmersion = true;
        GConfig->GetFloat(PreferencesSection, TEXT("UIScale"), InterfaceScale, GGameUserSettingsIni);
        GConfig->GetFloat(PreferencesSection, TEXT("VRUIScale"), VrInterfaceScale, GGameUserSettingsIni);
        GConfig->GetFloat(PreferencesSection, TEXT("CameraMouseSensitivity"), CameraMouseSensitivity, GGameUserSettingsIni);
        GConfig->GetFloat(PreferencesSection, TEXT("MouseCursorScale"), MouseCursorScale, GGameUserSettingsIni);
        CameraMouseSensitivity = FMath::IsFinite(CameraMouseSensitivity) ? FMath::Clamp(CameraMouseSensitivity, .25f, 3.0f) : 1.0f;
        MouseCursorScale = FMath::IsFinite(MouseCursorScale) ? FMath::Clamp(MouseCursorScale, .5f, 2.0f) : 1.0f;
        GConfig->GetFloat(PreferencesSection, TEXT("TooltipScale"), TooltipScale, GGameUserSettingsIni);
        TooltipScale = FMath::IsFinite(TooltipScale) ? FMath::Clamp(TooltipScale, .5f, 1.5f) : 1.0f;
        GConfig->GetFloat(PreferencesSection, TEXT("TooltipFadeTime"), TooltipFadeTime, GGameUserSettingsIni);
        TooltipFadeTime = FMath::IsFinite(TooltipFadeTime) ? FMath::Clamp(TooltipFadeTime, 0.f, 5.f) : 2.0f;
        GConfig->GetFloat(PreferencesSection, TEXT("UnitFrameScale"), UnitFrameScale, GGameUserSettingsIni);
        GConfig->GetFloat(PreferencesSection, TEXT("ActionBarScale"), ActionBarScale, GGameUserSettingsIni);
        GConfig->GetFloat(PreferencesSection, TEXT("MinimapScale"), MinimapScale, GGameUserSettingsIni);
        UnitFrameScale = FMath::IsFinite(UnitFrameScale) ? FMath::Clamp(UnitFrameScale, .5f, 2.0f) : 1.0f;
        ActionBarScale = FMath::IsFinite(ActionBarScale) ? FMath::Clamp(ActionBarScale, .5f, 2.0f) : 1.0f;
        MinimapScale = FMath::IsFinite(MinimapScale) ? FMath::Clamp(MinimapScale, .5f, 2.0f) : 1.0f;
        GConfig->GetFloat(PreferencesSection, TEXT("VrDwellTime"), VrDwellTime, GGameUserSettingsIni);
        VrDwellTime = FMath::IsFinite(VrDwellTime) ? FMath::Clamp(VrDwellTime, .5f, 2.0f) : 1.0f;
        GConfig->GetFloat(PreferencesSection, TEXT("MoonScale"), MoonScale, GGameUserSettingsIni);
        MoonScale = FMath::IsFinite(MoonScale) ? FMath::Clamp(MoonScale, 1.0f, 5.0f) : 2.5f;
        GConfig->GetFloat(PreferencesSection, TEXT("SunScale"), SunScale, GGameUserSettingsIni);
        SunScale = FMath::IsFinite(SunScale) ? FMath::Clamp(SunScale, 1.0f, 10.0f) : 1.0f;
        // 64-100 % like the original client: above 100 % UIParent is narrower than the default
        // main bar (Stefan 09.10: bars cut at the ends, side bars in the corner or gone at 120-150 %).
        InterfaceScale = FMath::IsFinite(InterfaceScale) ? FMath::Clamp(InterfaceScale, 0.64f, 1.5f) : 1.0f;
        VrInterfaceScale = FMath::IsFinite(VrInterfaceScale) ? FMath::Clamp(VrInterfaceScale, 0.64f, 1.5f) : 1.0f;
		GConfig->GetFloat(PreferencesSection, MasterVolumeKey, MasterVolume, GGameUserSettingsIni);
		GConfig->GetFloat(PreferencesSection, MusicVolumeKey, MusicVolume, GGameUserSettingsIni);
		GConfig->GetFloat(PreferencesSection, SfxVolumeKey, SfxVolume, GGameUserSettingsIni);
		GConfig->GetFloat(PreferencesSection, AmbienceVolumeKey, AmbienceVolume, GGameUserSettingsIni);
		GConfig->GetInt(PreferencesSection, PreferredRefreshRateKey,
			PreferredRefreshRate, GGameUserSettingsIni);
        GConfig->GetBool(PreferencesSection, DepthOfFieldKey, bDepthOfField, GGameUserSettingsIni);
        GConfig->GetFloat(PreferencesSection, TEXT("DepthOfFieldStrength"), DepthOfFieldStrength, GGameUserSettingsIni);
        GConfig->GetFloat(PreferencesSection, TEXT("FocusTestDistance"), FocusTestDistance, GGameUserSettingsIni);
        GConfig->GetFloat(PreferencesSection, TEXT("FocusTestWidth"), FocusTestWidth, GGameUserSettingsIni);
        FocusTestDistance = FMath::IsFinite(FocusTestDistance) ? FMath::Clamp(FocusTestDistance, 0.5f, 100.0f) : 10.0f;
        FocusTestWidth = FMath::IsFinite(FocusTestWidth) ? FMath::Clamp(FocusTestWidth, 0.0f, 1.0f) : 0.5f;
        DepthOfFieldStrength = FMath::IsFinite(DepthOfFieldStrength) ? FMath::Clamp(DepthOfFieldStrength, 0.5f, 4.0f) : 2.0f;
		GConfig->GetFloat(PreferencesSection, TEXT("Gamma"), DisplayGamma, GGameUserSettingsIni);
        DisplayGamma = FMath::IsFinite(DisplayGamma) ? FMath::Clamp(DisplayGamma, 1.6f, 3.0f) : 2.2f;
        GConfig->GetBool(PreferencesSection, TEXT("VolumetricFog"), bVolumetricFog, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, TEXT("Lumen"), bLumen, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, TEXT("HardwareRayTracing"), bHardwareRayTracing, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, MotionBlurKey, bMotionBlur, GGameUserSettingsIni);
		GConfig->GetInt(PreferencesSection, AntiAliasingMethodKey,
			AntiAliasingMethod, GGameUserSettingsIni);
		GConfig->GetInt(PreferencesSection, FsrQualityModeKey,
			FsrQualityMode, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, FrameGenerationKey,
			bFrameGeneration, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, PerformanceTimingsKey,
			bPerformanceTimings, GGameUserSettingsIni);
		GConfig->GetBool(PreferencesSection, PerformanceRenderingStatusKey,
			bPerformanceRenderingStatus, GGameUserSettingsIni);
		MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
		MusicVolume = FMath::Clamp(MusicVolume, 0.0f, 1.0f);
		SfxVolume = FMath::Clamp(SfxVolume, 0.0f, 1.0f);
		AmbienceVolume = FMath::Clamp(AmbienceVolume, 0.0f, 1.0f);
		PreferredRefreshRate = FMath::Clamp(PreferredRefreshRate, 0, 1000);
		AntiAliasingMethod = FMath::Clamp(AntiAliasingMethod, 0, 6);
		FsrQualityMode = FMath::Clamp(FsrQualityMode, 0, 4);
        GConfig->GetInt(PreferencesSection, TEXT("DlssQualityMode"), DlssQualityMode, GGameUserSettingsIni);
        DlssQualityMode = FMath::Clamp(DlssQualityMode, 1, 4);
        GConfig->GetInt(PreferencesSection, TEXT("NvidiaFrameGenerationMode"), NvidiaFrameGenerationMode, GGameUserSettingsIni);
	}
}

void SavePreference(const TCHAR* Key, bool bValue)
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetBool(PreferencesSection, Key, bValue, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void SaveFloatPreference(const TCHAR* Key, float Value)
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetFloat(PreferencesSection, Key, Value, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void SaveIntPreference(const TCHAR* Key, int32 Value)
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetInt(PreferencesSection, Key, Value, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}
}

float FWowClientPreferences::GetGamma() { LoadPreferences(); return DisplayGamma; }
void FWowClientPreferences::SetGamma(float Value)
{
    LoadPreferences();
    DisplayGamma = FMath::IsFinite(Value) ? FMath::Clamp(Value, 1.6f, 3.0f) : 2.2f;
    SaveFloatPreference(TEXT("Gamma"), DisplayGamma);
    if (GEngine) GEngine->DisplayGamma = DisplayGamma;
}

bool FWowClientPreferences::IsDarkerRealisticLightingEnabled()
{
	LoadPreferences();
	return bDarkerRealisticLighting;
}

void FWowClientPreferences::SetDarkerRealisticLightingEnabled(bool bEnabled)
{
	LoadPreferences();
	bDarkerRealisticLighting = bEnabled;
	SavePreference(DarkerLightingKey, bEnabled);
}

bool FWowClientPreferences::IsNpcAndPlayerCollisionEnabled()
{
	LoadPreferences();
	return bNpcAndPlayerCollision;
}

void FWowClientPreferences::SetNpcAndPlayerCollisionEnabled(bool bEnabled)
{
	LoadPreferences();
	bNpcAndPlayerCollision = bEnabled;
	SavePreference(EntityCollisionKey, bEnabled);
}

bool FWowClientPreferences::IsFirstPersonImmersionEnabled()
{
	LoadPreferences();
	return bFirstPersonImmersion;
}

void FWowClientPreferences::SetFirstPersonImmersionEnabled(bool bEnabled)
{
	LoadPreferences();
	bFirstPersonImmersion = bEnabled;
	SavePreference(FirstPersonImmersionKey, bEnabled);
	// Immersion off also turns "VR: first person only" off (it needs immersion); immersion
	// on leaves the VR option as it is.
	if (!bEnabled) SetWorldOption(TEXT("vrfirstperson"), false);
}

bool FWowClientPreferences::IsFirstPersonHelmetViewEnabled()
{
	LoadPreferences();
	return bFirstPersonHelmetView;
}

void FWowClientPreferences::SetFirstPersonHelmetViewEnabled(bool bEnabled)
{
	LoadPreferences();
	bFirstPersonHelmetView = bEnabled;
	SavePreference(FirstPersonHelmetViewKey, bEnabled);
}

float FWowClientPreferences::GetMasterVolume()
{
	LoadPreferences();
	return MasterVolume;
}

void FWowClientPreferences::SetMasterVolume(float Volume)
{
	LoadPreferences();
	MasterVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	SaveFloatPreference(MasterVolumeKey, MasterVolume);
}

float FWowClientPreferences::GetMusicVolume()
{
	LoadPreferences();
	return MusicVolume;
}

void FWowClientPreferences::SetMusicVolume(float Volume)
{
	LoadPreferences();
	MusicVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	SaveFloatPreference(MusicVolumeKey, MusicVolume);
}

float FWowClientPreferences::GetSfxVolume()
{
	LoadPreferences();
	return SfxVolume;
}

void FWowClientPreferences::SetSfxVolume(float Volume)
{
	LoadPreferences();
	SfxVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	SaveFloatPreference(SfxVolumeKey, SfxVolume);
}

float FWowClientPreferences::GetAmbienceVolume()
{
	LoadPreferences();
	return AmbienceVolume;
}

void FWowClientPreferences::SetAmbienceVolume(float Volume)
{
	LoadPreferences();
	AmbienceVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	SaveFloatPreference(AmbienceVolumeKey, AmbienceVolume);
}

int32 FWowClientPreferences::GetPreferredRefreshRate()
{
	LoadPreferences();
	return PreferredRefreshRate;
}

void FWowClientPreferences::SetPreferredRefreshRate(int32 RefreshRate)
{
	LoadPreferences();
	PreferredRefreshRate = FMath::Clamp(RefreshRate, 0, 1000);
	SaveIntPreference(PreferredRefreshRateKey, PreferredRefreshRate);
}

float FWowClientPreferences::GetFocusTestDistance() { LoadPreferences(); return FocusTestDistance; }
float FWowClientPreferences::GetFocusTestWidth() { LoadPreferences(); return FocusTestWidth; }
void FWowClientPreferences::SetFocusTestDistance(float Value)
{
    LoadPreferences(); if (!FMath::IsFinite(Value)) return;
    FocusTestDistance = FMath::Clamp(Value, 0.5f, 100.0f);
    SaveFloatPreference(TEXT("FocusTestDistance"), FocusTestDistance);
}
void FWowClientPreferences::SetFocusTestWidth(float Value)
{
    LoadPreferences(); if (!FMath::IsFinite(Value)) return;
    FocusTestWidth = FMath::Clamp(Value, 0.0f, 1.0f);
    SaveFloatPreference(TEXT("FocusTestWidth"), FocusTestWidth);
}
float FWowClientPreferences::GetDepthOfFieldStrength()
{
    LoadPreferences(); return DepthOfFieldStrength;
}
void FWowClientPreferences::SetDepthOfFieldStrength(float Strength)
{
    LoadPreferences();
    if (!FMath::IsFinite(Strength)) return;
    DepthOfFieldStrength = FMath::Clamp(Strength, 0.5f, 4.0f);
    SaveFloatPreference(TEXT("DepthOfFieldStrength"), DepthOfFieldStrength);
}

bool FWowClientPreferences::IsDepthOfFieldEnabled()
{
	LoadPreferences();
	return bDepthOfField;
}

void FWowClientPreferences::SetDepthOfFieldEnabled(bool bEnabled)
{
	LoadPreferences();
	bDepthOfField = bEnabled;
	SavePreference(DepthOfFieldKey, bEnabled);
}

bool FWowClientPreferences::IsMotionBlurEnabled()
{
	LoadPreferences();
	return bMotionBlur;
}

void FWowClientPreferences::SetMotionBlurEnabled(bool bEnabled)
{
	LoadPreferences();
	bMotionBlur = bEnabled;
	SavePreference(MotionBlurKey, bEnabled);
}

int32 FWowClientPreferences::GetAntiAliasingMethod()
{
	LoadPreferences();
	return AntiAliasingMethod;
}

void FWowClientPreferences::SetAntiAliasingMethod(int32 Method)
{
	LoadPreferences();
	AntiAliasingMethod = FMath::Clamp(Method, 0, 6);
	SaveIntPreference(AntiAliasingMethodKey, AntiAliasingMethod);
}

int32 FWowClientPreferences::GetFsrQualityMode()
{
	LoadPreferences();
	return FsrQualityMode;
}

void FWowClientPreferences::SetFsrQualityMode(int32 Mode)
{
	LoadPreferences();
	FsrQualityMode = FMath::Clamp(Mode, 0, 4);
	SaveIntPreference(FsrQualityModeKey, FsrQualityMode);
}

bool FWowClientPreferences::IsDlssAvailable() { return UDLSSLibrary::IsDLSSSupported(); }
int32 FWowClientPreferences::GetDlssQualityMode() { LoadPreferences(); return DlssQualityMode; }
void FWowClientPreferences::SetDlssQualityMode(int32 Mode)
{
    LoadPreferences(); DlssQualityMode=FMath::Clamp(Mode,1,4);
    SaveIntPreference(TEXT("DlssQualityMode"),DlssQualityMode);
}
bool FWowClientPreferences::IsNvidiaFrameGenerationAvailable() { return UStreamlineLibraryDLSSG::IsDLSSGSupported(); }
bool FWowClientPreferences::IsNvidiaFrameGenerationActive()
{
    if (!IsNvidiaFrameGenerationAvailable() || UStreamlineLibraryDLSSG::GetDLSSGMode()==EStreamlineDLSSGMode::Off) return false;
    float Rate=0; int32 Presented=0;
    UStreamlineLibraryDLSSG::GetDLSSGFrameTiming(Rate,Presented);
    return Presented>1;
}
int32 FWowClientPreferences::GetNvidiaPresentedFramesPerRender()
{
    if (!IsNvidiaFrameGenerationAvailable() || UStreamlineLibraryDLSSG::GetDLSSGMode()==EStreamlineDLSSGMode::Off) return 1;
    float Rate=0; int32 Presented=0;
    UStreamlineLibraryDLSSG::GetDLSSGFrameTiming(Rate,Presented);
    return FMath::Clamp(Presented,1,6);
}
TArray<int32> FWowClientPreferences::GetNvidiaFrameGenerationModes()
{
    TArray<int32> Result;
    for (auto Mode : UStreamlineLibraryDLSSG::GetSupportedDLSSGModes())
        if (Mode!=EStreamlineDLSSGMode::Off) Result.Add(static_cast<int32>(Mode));
    return Result;
}
int32 FWowClientPreferences::GetNvidiaFrameGenerationMode() { LoadPreferences(); return NvidiaFrameGenerationMode; }
void FWowClientPreferences::SetNvidiaFrameGenerationMode(int32 Mode)
{
    LoadPreferences();
    if (GetNvidiaFrameGenerationModes().Contains(Mode))
    {
        NvidiaFrameGenerationMode=Mode;
        SaveIntPreference(TEXT("NvidiaFrameGenerationMode"),Mode);
    }
}
FString FWowClientPreferences::GetNvidiaFrameGenerationModeLabel(int32 Mode)
{
    switch (static_cast<EStreamlineDLSSGMode>(Mode))
    {
    case EStreamlineDLSSGMode::Auto: return TEXT("Auto");
    case EStreamlineDLSSGMode::OnDynamic: return TEXT("Dynamic");
    case EStreamlineDLSSGMode::On2X: return TEXT("2x");
    case EStreamlineDLSSGMode::On3X: return TEXT("3x");
    case EStreamlineDLSSGMode::On4X: return TEXT("4x");
    case EStreamlineDLSSGMode::On5X: return TEXT("5x");
    case EStreamlineDLSSGMode::On6X: return TEXT("6x");
    default: return TEXT("Unavailable");
    }
}
void FWowClientPreferences::SuspendFrameGeneration()
{
    if (auto* FI=IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled"))) FI->Set(0,ECVF_SetByCode);
    UStreamlineLibraryDLSSG::SetDLSSGMode(EStreamlineDLSSGMode::Off);
}

void FWowClientPreferences::SetGameplayRendering(bool bActive)
{
    if (bGameplayRendering==bActive) return;
    bGameplayRendering=bActive;
    ApplyFrameGeneration();
}
namespace { void (*GRenderOverrideHook)() = nullptr; }
void FWowClientPreferences::SetRenderOverrideHook(void (*Hook)()) { GRenderOverrideHook = Hook; }

void FWowClientPreferences::ApplyFrameGeneration()
{
    LoadPreferences();
    ApplySavedAntiLag2();
    SuspendFrameGeneration();
    auto SetCVar=[](const TCHAR* Name,int32 Value) {
        if (auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name)) Variable->Set(Value,ECVF_SetByCode);
    };
    if (bNvidiaForcedVsyncOff)
    {
        if (GEngine && GEngine->GetGameUserSettings()) SetCVar(TEXT("r.VSync"),GEngine->GetGameUserSettings()->IsVSyncEnabled() ? 1 : 0);
        bNvidiaForcedVsyncOff=false;
    }
    const bool bNvidiaFG=IsNvidiaFrameGenerationAvailable();
    if (bFrameGeneration && bNvidiaFG && bGameplayRendering)
    {
        auto Mode=static_cast<EStreamlineDLSSGMode>(NvidiaFrameGenerationMode);
        if (!UStreamlineLibraryDLSSG::IsDLSSGModeSupported(Mode) || Mode==EStreamlineDLSSGMode::Off)
            Mode=EStreamlineDLSSGMode::On2X;
        if (UStreamlineLibraryReflex::IsReflexSupported()) UStreamlineLibraryReflex::SetReflexMode(EStreamlineReflexMode::Enabled);
        UStreamlineLibraryDLSSG::SetDLSSGMode(Mode);
        if (!UStreamlineLibraryDLSSG::GetDLSSGIsVsyncSupportAvailable())
        {
            SetCVar(TEXT("r.VSync"),0);
            bNvidiaForcedVsyncOff=true;
        }
    }
    else if (bFrameGeneration && !bNvidiaFG && AntiAliasingMethod==4)
        SetCVar(TEXT("r.FidelityFX.FI.Enabled"),1);
    if (GRenderOverrideHook) GRenderOverrideHook();
}

bool FWowClientPreferences::IsFrameGenerationEnabled()
{
	LoadPreferences();
	return bFrameGeneration;
}

void FWowClientPreferences::SetFrameGenerationEnabled(bool bEnabled)
{
	LoadPreferences();
	bFrameGeneration = bEnabled;
	SavePreference(FrameGenerationKey, bEnabled);
}

// The AMD plugin registers "r.AMD.AntiLag2" (default 1). The previous lookup used
// "r.AntiLag2", which does not exist, so the option never changed anything.
static IConsoleVariable* FindAntiLag2CVar()
{
    return IConsoleManager::Get().FindConsoleVariable(TEXT("r.AMD.AntiLag2"));
}

bool FWowClientPreferences::IsAntiLag2Enabled()
{
    bool bSaved = true;
    if (GConfig) GConfig->GetBool(TEXT("VideoSettings"), TEXT("AntiLag2"), bSaved, GGameUserSettingsIni);
    IConsoleVariable* CVar = FindAntiLag2CVar();
    return CVar ? (CVar->GetInt() > 0) : bSaved;
}

void FWowClientPreferences::SetAntiLag2Enabled(bool bEnabled)
{
    if (IConsoleVariable* CVar = FindAntiLag2CVar())
    {
        CVar->Set(bEnabled ? 1 : 0, ECVF_SetByCode);
    }
    if (GConfig)
    {
        GConfig->SetBool(TEXT("VideoSettings"), TEXT("AntiLag2"), bEnabled, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
}

void FWowClientPreferences::ApplySavedAntiLag2()
{
    bool bSaved = true;
    if (GConfig) GConfig->GetBool(TEXT("VideoSettings"), TEXT("AntiLag2"), bSaved, GGameUserSettingsIni);
    if (IConsoleVariable* CVar = FindAntiLag2CVar()) CVar->Set(bSaved ? 1 : 0, ECVF_SetByCode);

    // Ray tracing geometry pool: engine default is 400 MB and the on-screen red warning fires at 20%
    // of it. WoW scenes keep far more always-resident geometry, so raise the pool (real fix, avoids
    // eviction/rebuild churn) and only warn above half of it. Applied in code as well as in
    // DefaultEngine/DefaultScalability so no config layer can leave the 400 MB default in place.
    IConsoleManager& CVars = IConsoleManager::Get();
    if (IConsoleVariable* Pool = CVars.FindConsoleVariable(TEXT("r.RayTracing.ResidentGeometryMemoryPoolSizeInMB")))
    {
        // The engine converts this MB value to bytes in int32: 2048 or more overflows to a
        // negative budget (shown as "15.999 EiB" and every geometry counts as over budget).
        const int32 PoolMB = Pool->GetInt();
        // Largest value that still fits; busy zones (Durotar/Barrens) sit around 0.8 GB.
        if (PoolMB != 2047) Pool->Set(2047, ECVF_SetByCode);
    }
    // The red always-resident line is a developer diagnostic, not a player warning:
    // only show it when the pool is genuinely full.
    if (IConsoleVariable* Warn = CVars.FindConsoleVariable(TEXT("r.RayTracing.Debug.GeometryMemoryPool.AlwaysResidentWarningPercentage")))
    {
        if (Warn->GetInt() < 100) Warn->Set(100, ECVF_SetByCode);
    }
}

bool FWowClientPreferences::ArePerformanceTimingsEnabled()
{
	LoadPreferences();
	return bPerformanceTimings;
}

void FWowClientPreferences::SetPerformanceTimingsEnabled(bool bEnabled)
{
	LoadPreferences();
	bPerformanceTimings = bEnabled;
	SavePreference(PerformanceTimingsKey, bEnabled);
}

bool FWowClientPreferences::IsPerformanceRenderingStatusEnabled()
{
	LoadPreferences();
	return bPerformanceRenderingStatus;
}

void FWowClientPreferences::SetPerformanceRenderingStatusEnabled(bool bEnabled)
{
	LoadPreferences();
	bPerformanceRenderingStatus = bEnabled;
	SavePreference(PerformanceRenderingStatusKey, bEnabled);
}

bool FWowClientPreferences::IsVolumetricFogEnabled()
{
    LoadPreferences();
    return bVolumetricFog;
}
void FWowClientPreferences::SetVolumetricFogEnabled(bool bEnabled)
{
    LoadPreferences();
    bVolumetricFog = bEnabled;
    SavePreference(TEXT("VolumetricFog"), bEnabled);
}
bool FWowClientPreferences::IsLumenEnabled()
{
    LoadPreferences();
    return bLumen;
}
void FWowClientPreferences::SetLumenEnabled(bool bEnabled)
{
    LoadPreferences();
    bLumen = bEnabled;
    SavePreference(TEXT("Lumen"), bEnabled);
}
bool FWowClientPreferences::IsHardwareRayTracingEnabled()
{
    LoadPreferences();
    return bHardwareRayTracing;
}
void FWowClientPreferences::SetHardwareRayTracingEnabled(bool bEnabled)
{
    LoadPreferences();
    bHardwareRayTracing = bEnabled;
    SavePreference(TEXT("HardwareRayTracing"), bEnabled);
}
bool FWowClientPreferences::IsHardwareRayTracingAvailable()
{
#if RHI_RAYTRACING
    return GDynamicRHI && IsRayTracingAllowed() && RHISupportsRayTracing(GMaxRHIShaderPlatform);
#else
    return false;
#endif
}

bool FWowClientPreferences::IsLumenAvailable()
{
    auto Value = [](const TCHAR* Name, int32 Default)
    {
        const auto* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
        return CVar ? CVar->GetInt() : Default;
    };
    return GDynamicRHI && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5
        && Value(TEXT("r.ForwardShading"), 0) == 0
        && Value(TEXT("r.Lumen.Supported"), 0) != 0
        && (Value(TEXT("r.GenerateMeshDistanceFields"), 0) != 0 || IsHardwareRayTracingAvailable());
}

void FWowClientPreferences::ApplyAdvancedVideoPreferences()
{
	LoadPreferences();
	auto SetCVar = [](const TCHAR* Name, int32 Value) -> int32
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			// DefaultEngine.ini and several renderer defaults are registered above
			// SetByGameSetting priority. The native Options panel is the user's
			// explicit runtime choice, so apply it at code priority; otherwise the
			// menu can persist "FSR" while the live cvar silently remains disabled.
			Variable->Set(Value, ECVF_SetByCode);
			return Variable->GetInt();
		}
		return INDEX_NONE;
	};

    if (GEngine) GEngine->DisplayGamma = DisplayGamma;
    // Enabling distance fields for Lumen must not silently add DFAO to the classic renderer.
    SetCVar(TEXT("r.DistanceFieldAO"), bDarkerRealisticLighting ? 1 : 0);
    const bool bUseHardwareRT = bHardwareRayTracing && IsHardwareRayTracingAvailable();
    const bool bUseLumen = bLumen && IsLumenAvailable();
    SetCVar(TEXT("r.VolumetricFog"), bVolumetricFog ? 1 : 0);
    SetCVar(TEXT("r.RayTracing.Enable"), bUseHardwareRT ? 1 : 0);
    SetCVar(TEXT("r.RayTracing.Shadows"), bUseHardwareRT ? 1 : 0);
    SetCVar(TEXT("r.Lumen.HardwareRayTracing"), bUseHardwareRT ? 1 : 0);
    // Evaluate direct light at actual triangle hits. Runtime-imported solid
    // scenery now supplies surface cards for cached indirect lighting as well.
    SetCVar(TEXT("r.Lumen.HardwareRayTracing.LightingMode"), bUseHardwareRT ? 1 : 0);
    // Mode 1 bypasses the surface-cache indirect term in UE's hit-lighting
    // shader and substitutes unoccluded sky. Restore mode 2: with Lumen GI the
    // hit consumes cached sky + bounced light, including material colour.
    SetCVar(TEXT("r.Lumen.HardwareRayTracing.HitLighting.Skylight"), 2);
    SetCVar(TEXT("r.DynamicGlobalIlluminationMethod"), bUseLumen ? 1 : 0);
    SetCVar(TEXT("r.ReflectionMethod"), bUseLumen ? 1 : 0);
    SetCVar(TEXT("r.Lumen.DiffuseIndirect.Allow"), bUseLumen ? 1 : 0);
    SetCVar(TEXT("r.Lumen.Reflections.Allow"), bUseLumen ? 1 : 0);
    SetCVar(TEXT("r.Lumen.TranslucencyReflections.FrontLayer.Allow"), bUseLumen ? 1 : 0);
    // Water reflections behind trees: a reflected ray that passes just behind a
    // foreground trunk/leaf counted as a miss (thickness 0.5 %), fell through to the
    // world trace (masked foliage is thin there) and returned bright sky: a light
    // halo hugging the tree that smeared along with camera motion. Thicker screen
    // hits keep the occluder's own colour in that gap instead.
    if (IConsoleVariable* Thickness = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.Lumen.Reflections.HierarchicalScreenTraces.RelativeDepthThickness")))
        Thickness->Set(bUseLumen ? 0.03f : 0.005f, ECVF_SetByCode);
    // WoW zones are dense with small doodads (grass tufts, reeds, crates): the default 4096
    // surface-cache atlas overflowed ("oversubscribed" on screen, flickering GI). 6144 costs
    // roughly 200 MB more VRAM and only applies with Lumen on.
    // r466 (Stefan, Shadowglen: the red "oversubscribed by 2.89%" again): Teldrassil's tree-dense
    // zones overflow 6144 too. 8192 (about 350 MB more than 6144) with Lumen on.
    if (IConsoleVariable* Atlas = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LumenScene.SurfaceCache.AtlasSize")))
        Atlas->Set(bUseLumen ? 8192 : 4096, ECVF_SetByCode);
    // r523 (Stefan: shadows popped in when he turned and walked closer to a brazier): the shadow
    // scalability group at level 2 drops casters smaller than 4 % of the screen from shadow maps
    // (r.Shadow.RadiusThreshold 0.04), so small props' shadows appeared only up close. Use the
    // epic-level 1 % threshold at every quality level; the cost is a few extra small casters.
    if (IConsoleVariable* Radius = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.RadiusThreshold")))
        Radius->Set(0.01f, ECVF_SetByCode);
    // r523 (Stefan, Dun Morogh snow with "darker realistic lighting": dotted grey bands that keep
    // changing on the snow, worse the lower the camera): Lumen's short-range AO searches the depth
    // buffer for an occluding horizon in 2 slices x 3 steps on the half-resolution HZB. On WoW's
    // faceted terrain with smoothed normals the triangle folds read as occluders and the few noisy
    // samples stipple into moving bands (the darker profile has no fill light to hide them). Search
    // the full-resolution depth with 4 x 4 samples; option "shortrangeao" switches it off entirely.
    auto SetFloat = [](const TCHAR* Name, float Value)
    {
        if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name)) Variable->Set(Value, ECVF_SetByCode);
    };
    SetCVar(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO"), GetWorldOption(TEXT("shortrangeao")) ? 1 : 0);
    SetCVar(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.HZB"), 0);
    SetCVar(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.SliceCount"), 4);
    SetCVar(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.StepsPerSlice"), 4);
    SetFloat(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.SlopeCompareToleranceScale"), 2.0f);
    UE_LOG(LogWowClientPreferences, Display, TEXT("Lighting options: volumetricFog=%d Lumen requested=%d active=%d hardwareRT requested=%d active=%d"),
        bVolumetricFog, bLumen, bUseLumen, bHardwareRayTracing, bUseHardwareRT);

	SetCVar(TEXT("r.DefaultFeature.DepthOfField"), bDepthOfField ? 1 : 0);
	SetCVar(TEXT("r.DepthOfFieldQuality"), bDepthOfField ? 2 : 0);
    // Depth of field (r249, Stefan): the blur is gathered at half of the render resolution, which
    // with FSR Performance/Ultra Performance is a sixth of the screen, so a large foreground blur
    // turned characters into coloured blocks with a smeared ("slimy") rim. Smaller maximum blur
    // radii, more gather rings and the full-quality recombine keep it soft instead of blocky.
    if (bDepthOfField)
    {
        if (IConsoleVariable* Fg = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DOF.Kernel.MaxForegroundRadius")))
            Fg->Set(0.012f, ECVF_SetByCode);
        if (IConsoleVariable* Bg = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DOF.Kernel.MaxBackgroundRadius")))
            Bg->Set(0.018f, ECVF_SetByCode);
        SetCVar(TEXT("r.DOF.Gather.RingCount"), 5);
        SetCVar(TEXT("r.DOF.Gather.AccumulatorQuality"), 1);
        SetCVar(TEXT("r.DOF.Recombine.Quality"), 2);
    }
	SetCVar(TEXT("r.DefaultFeature.MotionBlur"), bMotionBlur ? 1 : 0);
	SetCVar(TEXT("r.MotionBlurQuality"), bMotionBlur ? 3 : 0);

    const bool bFsrAvailable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FSR.Enabled"))!=nullptr;
    const bool bUseFsr=AntiAliasingMethod==4 && bFsrAvailable;
    const bool bUseDlss=(AntiAliasingMethod==5 || AntiAliasingMethod==6) && IsDlssAvailable();
    const int32 NativeAaMethod=bUseDlss ? 2 : ((AntiAliasingMethod==3 || bUseFsr || AntiAliasingMethod>=4) ? 4 : AntiAliasingMethod);
    // Never run two temporal upscalers or two frame-generation providers together.
    SuspendFrameGeneration();
    SetCVar(TEXT("r.FidelityFX.FSR.Enabled"),0);
    UDLSSLibrary::EnableDLSS(false);
    SetCVar(TEXT("r.AntiAliasingMethod"),NativeAaMethod);
    SetCVar(TEXT("r.TemporalAA.Upsampling"),(NativeAaMethod==4 || bUseDlss) ? 1 : 0);
    SetCVar(TEXT("r.FidelityFX.FSR.QualityMode"),FsrQualityMode);
    const int32 ActualFsrEnabled=SetCVar(TEXT("r.FidelityFX.FSR.Enabled"),bUseFsr ? 1 : 0);
    float RenderScale=100.f;
    if (GEngine && GEngine->GetGameUserSettings())
    {
        float Normalized=1,Min=50,Max=100;
        GEngine->GetGameUserSettings()->GetResolutionScaleInformationEx(Normalized,RenderScale,Min,Max);
    }
    if (bUseDlss)
    {
        static const UDLSSMode Modes[]={UDLSSMode::DLAA,UDLSSMode::Quality,UDLSSMode::Balanced,UDLSSMode::Performance,UDLSSMode::UltraPerformance};
        const UDLSSMode Mode=AntiAliasingMethod==6 ? UDLSSMode::DLAA : Modes[DlssQualityMode];
        bool Supported=false,Fixed=false; float Min=0,Max=100,Sharpness=0;
        UDLSSLibrary::GetDLSSModeInformation(Mode,FVector2D::ZeroVector,Supported,RenderScale,Fixed,Min,Max,Sharpness);
        if (!Supported) RenderScale=100.f;
        UDLSSLibrary::EnableDLSS(true);
    }
    if (!bUseFsr)
        if (auto* Scale=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))) Scale->Set(RenderScale,ECVF_SetByCode);

    ApplyFrameGeneration();
    const bool bNvidiaFG=IsNvidiaFrameGenerationAvailable();
    UE_LOG(LogWowClientPreferences,Display,TEXT("NVIDIA: DLSS supported=%d active=%d DLAA=%d FG supported=%d mode=%d"),
        IsDlssAvailable(),UDLSSLibrary::IsDLSSEnabled(),bUseDlss && AntiAliasingMethod==6,bNvidiaFG,
        static_cast<int32>(UStreamlineLibraryDLSSG::GetDLSSGMode()));

    const auto* ActualFI = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled"));
    const auto* ActualDOF = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DepthOfFieldQuality"));
    UE_LOG(LogWowClientPreferences, Display, TEXT("Camera/video options: DOF requested=%d quality=%d; FrameGeneration requested=%d enabled=%d"),
        bDepthOfField, ActualDOF ? ActualDOF->GetInt() : -1, bFrameGeneration, ActualFI ? ActualFI->GetInt() : -1);
	const IConsoleVariable* ActualAa =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
	const IConsoleVariable* ActualFsrQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FSR.QualityMode"));
	const IConsoleVariable* ActualScreenPercentage =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	UE_LOG(LogWowClientPreferences, Display,
		TEXT("Video settings applied: requestedAA=%d actualAA=%d requestedFSR=%d actualFSR=%d quality=%d screenPercentage=%.1f"),
		AntiAliasingMethod,
		ActualAa ? ActualAa->GetInt() : INDEX_NONE,
		bUseFsr ? 1 : 0,
		ActualFsrEnabled,
		ActualFsrQuality ? ActualFsrQuality->GetInt() : INDEX_NONE,
		ActualScreenPercentage ? ActualScreenPercentage->GetFloat() : -1.0f);
    if (GRenderOverrideHook) GRenderOverrideHook();
}

// Two interface presets (Stefan 09.10): the monitor one and a separate one for the headset, so
// tuning the VR interface never changes the normal one. The VR UI host switches the mode.
void FWowClientPreferences::SetVrUiMode(bool bVr) { bVrUiMode = bVr; }
bool FWowClientPreferences::IsVrUiMode() { return bVrUiMode; }

float FWowClientPreferences::GetUIScale()
{
    LoadPreferences();
    return bVrUiMode ? VrInterfaceScale : InterfaceScale;
}

void FWowClientPreferences::SetUIScale(float Scale)
{
    LoadPreferences();
    if (!FMath::IsFinite(Scale)) return;
    float& Target = bVrUiMode ? VrInterfaceScale : InterfaceScale;
    // Up to 150 % (Stefan 10.10: the original 64-100 % range only made the interface smaller).
    Target = FMath::Clamp(Scale, 0.64f, 1.5f);
    if (GConfig)
    {
        GConfig->SetFloat(PreferencesSection, bVrUiMode ? TEXT("VRUIScale") : TEXT("UIScale"), Target, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
}

bool FWowClientPreferences::HasWorldOption(const FString& Name) { return WorldOptions.Contains(Name.ToLower()); }
bool FWowClientPreferences::GetWorldOption(const FString& Name)
{ LoadPreferences(); const bool* Value=WorldOptions.Find(Name.ToLower()); return Value && *Value; }
void FWowClientPreferences::SetWorldOption(const FString& Name,bool Enabled)
{
    LoadPreferences(); const FString Key=Name.ToLower();
    if(bool* Value=WorldOptions.Find(Key); Value && *Value!=Enabled) { *Value=Enabled; SavePreference(*Key,Enabled); }
    // "VR: first person only" on also turns first-person immersion on (r222).
    if (Enabled && Key == TEXT("vrfirstperson") && !bFirstPersonImmersion) SetFirstPersonImmersionEnabled(true);
    if (Key == TEXT("shortrangeao")) ApplyAdvancedVideoPreferences(); // r523: live from Options
}

int32 FWowClientPreferences::GetNameplateMode() { LoadPreferences(); return NameplateMode; }
void FWowClientPreferences::SetNameplateMode(int32 Mode)
{
    LoadPreferences(); NameplateMode=FMath::Clamp(Mode,0,2);
    if (GConfig) {GConfig->SetInt(PreferencesSection,TEXT("WorldLabelMode"),NameplateMode,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);}
}

int32 FWowClientPreferences::GetAutoLootKey() { LoadPreferences(); return AutoLootKey; }
void FWowClientPreferences::SetAutoLootKey(int32 Key)
{
    LoadPreferences(); AutoLootKey=FMath::Clamp(Key,0,3);
    if (GConfig) {GConfig->SetInt(PreferencesSection,TEXT("AutoLootKey"),AutoLootKey,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);}
}

int32 FWowClientPreferences::GetIntroMovieMode() { LoadPreferences(); return IntroMovieMode; }
void FWowClientPreferences::SetIntroMovieMode(int32 Mode)
{
    LoadPreferences(); IntroMovieMode=FMath::Clamp(Mode,0,2);
    if (GConfig) {GConfig->SetInt(PreferencesSection,TEXT("IntroMovieMode"),IntroMovieMode,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);}
}

static FString ClientWtfModeKey(const FString& RealmName)
{
    FString Key = TEXT("ClientWtfMode_");
    for (const TCHAR Char : RealmName) Key.AppendChar(FChar::IsAlnum(Char) ? Char : TEXT('_'));
    return Key;
}

int32 FWowClientPreferences::GetClientWtfMode(const FString& RealmName)
{
    int32 Mode = -1;
    if (GConfig && !RealmName.IsEmpty()) GConfig->GetInt(PreferencesSection, *ClientWtfModeKey(RealmName), Mode, GGameUserSettingsIni);
    return Mode >= -1 && Mode <= 3 ? Mode : -1;
}

void FWowClientPreferences::SetClientWtfMode(const FString& RealmName, int32 Mode)
{
    if (!GConfig || RealmName.IsEmpty()) return;
    GConfig->SetInt(PreferencesSection, *ClientWtfModeKey(RealmName), FMath::Clamp(Mode, 0, 3), GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

FString FWowClientPreferences::GetDataSource()
{
    FString Path;
    if (GConfig) GConfig->GetString(PreferencesSection, TEXT("DataSource"), Path, GGameUserSettingsIni);
    if (Path.IsEmpty())
    {
        // Retain installations selected by the old development launcher.
        FString Json;
        TSharedPtr<FJsonObject> Object;
        if (FFileHelper::LoadFileToString(Json, *(FPaths::ProjectSavedDir()/TEXT("Config/WowUnrealLauncher.json"))) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) && Object.IsValid())
            Object->TryGetStringField(TEXT("DataSource"), Path);
    }
    return Path;
}

bool FWowClientPreferences::SetDataSource(const FString& Value)
{
    FString Path = Value.TrimStartAndEnd().TrimQuotes();
    if (FPaths::FileExists(Path/TEXT("Data/common.MPQ"))) Path /= TEXT("Data");
    if (!GConfig || !FPaths::FileExists(Path/TEXT("common.MPQ"))) return false;
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Path);
    GConfig->SetString(PreferencesSection, TEXT("DataSource"), *Path, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
    return true;
}

int32 FWowClientPreferences::GetCursorMode()
{
    int32 Mode = 0;
    if (GConfig) GConfig->GetInt(TEXT("WowUnreal.ClientPreferences"), TEXT("CursorMode"), Mode, GGameUserSettingsIni);
    return Mode == 1 ? 1 : 0;
}

float FWowClientPreferences::GetCameraMouseSensitivity() { LoadPreferences(); return CameraMouseSensitivity; }
void FWowClientPreferences::SetCameraMouseSensitivity(float Value)
{
    LoadPreferences();
    CameraMouseSensitivity = FMath::IsFinite(Value) ? FMath::Clamp(Value, .25f, 3.0f) : 1.0f;
    SaveFloatPreference(TEXT("CameraMouseSensitivity"), CameraMouseSensitivity);
}
float FWowClientPreferences::GetMoonScale() { LoadPreferences(); return MoonScale; }
float FWowClientPreferences::GetVrDwellTime() { LoadPreferences(); return VrDwellTime; }
void FWowClientPreferences::SetVrDwellTime(float Seconds)
{
    LoadPreferences(); if (!FMath::IsFinite(Seconds)) return;
    VrDwellTime = FMath::Clamp(Seconds, .5f, 2.0f);
    SaveFloatPreference(TEXT("VrDwellTime"), VrDwellTime);
}
float FWowClientPreferences::GetSunScale() { LoadPreferences(); return SunScale; }
void FWowClientPreferences::SetMoonScale(float Value)
{
    LoadPreferences(); if (!FMath::IsFinite(Value)) return;
    MoonScale = FMath::Clamp(Value, 1.0f, 5.0f);
    SaveFloatPreference(TEXT("MoonScale"), MoonScale);
}
void FWowClientPreferences::SetSunScale(float Value)
{
    LoadPreferences(); if (!FMath::IsFinite(Value)) return;
    SunScale = FMath::Clamp(Value, 1.0f, 10.0f);
    SaveFloatPreference(TEXT("SunScale"), SunScale);
}
float FWowClientPreferences::GetTooltipScale() { LoadPreferences(); return TooltipScale; }
void FWowClientPreferences::SetTooltipScale(float Value)
{
    LoadPreferences();
    TooltipScale = FMath::IsFinite(Value) ? FMath::Clamp(Value, .5f, 1.5f) : 1.0f;
    SaveFloatPreference(TEXT("TooltipScale"), TooltipScale);
}
float FWowClientPreferences::GetTooltipFadeTime() { LoadPreferences(); return TooltipFadeTime; }
void FWowClientPreferences::SetTooltipFadeTime(float Value)
{
    LoadPreferences();
    TooltipFadeTime = FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 5.f) : 2.0f;
    SaveFloatPreference(TEXT("TooltipFadeTime"), TooltipFadeTime);
}
float FWowClientPreferences::GetUnitFrameScale() { LoadPreferences(); return UnitFrameScale; }
void FWowClientPreferences::SetUnitFrameScale(float Value)
{
    LoadPreferences();
    UnitFrameScale = FMath::IsFinite(Value) ? FMath::Clamp(Value, .5f, 2.0f) : 1.0f;
    SaveFloatPreference(TEXT("UnitFrameScale"), UnitFrameScale);
}
float FWowClientPreferences::GetActionBarScale() { LoadPreferences(); return ActionBarScale; }
void FWowClientPreferences::SetActionBarScale(float Value)
{
    LoadPreferences();
    ActionBarScale = FMath::IsFinite(Value) ? FMath::Clamp(Value, .5f, 2.0f) : 1.0f;
    SaveFloatPreference(TEXT("ActionBarScale"), ActionBarScale);
}
float FWowClientPreferences::GetMinimapScale() { LoadPreferences(); return MinimapScale; }
void FWowClientPreferences::SetMinimapScale(float Value)
{
    LoadPreferences();
    MinimapScale = FMath::IsFinite(Value) ? FMath::Clamp(Value, .5f, 2.0f) : 1.0f;
    SaveFloatPreference(TEXT("MinimapScale"), MinimapScale);
}
float FWowClientPreferences::GetMouseCursorScale() { LoadPreferences(); return MouseCursorScale; }
void FWowClientPreferences::SetMouseCursorScale(float Value)
{
    LoadPreferences();
    MouseCursorScale = FMath::IsFinite(Value) ? FMath::Clamp(Value, .5f, 2.0f) : 1.0f;
    SaveFloatPreference(TEXT("MouseCursorScale"), MouseCursorScale);
}
static void TickAdaptiveSyncState(float DeltaTime);
void FWowClientPreferences::TickAdaptiveSync(float DeltaTime)
{
    TickAdaptiveSyncState(DeltaTime);
    // The UE frame limiter and VSync at the same rate beat against each other:
    // the limiter's sleep lands just past a vblank and every other refresh is
    // missed, locking 60 Hz to 30 fps. While VSync is presenting, VSync is the
    // cap; the user's limit applies only when VSync is off or released.
    UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    static IConsoleVariable* const VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
    if (!Settings || !VSync || GIsEditor) return;
    const float UserLimit = FMath::Max(0.0f, Settings->GetFrameRateLimit());
    float Wanted = VSync->GetInt() != 0 ? 0.0f : UserLimit;
    // r503: while VSync is released to FreeSync/G-Sync, cap 2 fps under the refresh so frames stay
    // inside the variable-refresh range (at the refresh rate itself it tears or falls back to VSync).
    if (bAdaptiveSyncReleased && AdaptiveSyncHz >= 30)
        Wanted = Wanted > 0.0f ? FMath::Min(Wanted, float(AdaptiveSyncHz - 2)) : float(AdaptiveSyncHz - 2);
    if (!FMath::IsNearlyEqual(GEngine->GetMaxFPS(), Wanted)) GEngine->SetMaxFPS(Wanted);
}
static void TickAdaptiveSyncState(float DeltaTime)
{
    static IConsoleVariable* const VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
    UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    if (!VSync || !Settings || GIsEditor || DeltaTime <= 0.0f) return;
    LoadPreferences();
    // Frame generation manages its own present/VSync rules.
    const bool bManaged = Settings->IsVSyncEnabled() && !bNvidiaForcedVsyncOff && !bFrameGeneration;
    if (!bManaged || CVarAdaptiveSync.GetValueOnGameThread() == 0)
    {
        // r.VSync is set by code here, which outranks the game-settings write from
        // Options: keep it equal to the checkbox so turning sync off still works.
        const int32 Desired = Settings->IsVSyncEnabled() ? 1 : 0;
        if (!bNvidiaForcedVsyncOff && !bFrameGeneration && VSync->GetInt() != Desired)
            VSync->Set(Desired, ECVF_SetByCode);
        bAdaptiveSyncReleased = false;
        AdaptiveSyncSlowSeconds = AdaptiveSyncFastSeconds = 0.0f;
        return;
    }
    const int32 DisplayHz = QueryDisplayRefreshHz();
    AdaptiveSyncHz = DisplayHz >= 30 ? DisplayHz : FMath::Max(30, PreferredRefreshRate);
    const float BudgetMs = 1000.0f / AdaptiveSyncHz;
    // Work per frame without waits. The game-thread figure includes the VSync /
    // render-fence wait (it read 16.3 ms at 60 fps while the real cost was ~6 ms),
    // so it alone released VSync on a GPU at 40 % load. Frame-limiter sleep is
    // removed via the idle time; render thread and GPU are the real costs.
    const float GameMs = FMath::Max(0.0f, static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime))
        - static_cast<float>(FApp::GetIdleTime() * 1000.0));
    const float WorkMs = FMath::Max(
        static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime)),
        static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0))));
    const float Smooth = FMath::Clamp(DeltaTime * 8.0f, 0.0f, 1.0f);
    AdaptiveSyncWorkMs = AdaptiveSyncWorkMs <= 0.0f ? WorkMs : FMath::Lerp(AdaptiveSyncWorkMs, WorkMs, Smooth);
    const float FrameMs = DeltaTime * 1000.0f;
    AdaptiveSyncFrameMs = AdaptiveSyncFrameMs <= 0.0f ? FrameMs : FMath::Lerp(AdaptiveSyncFrameMs, FrameMs, Smooth);
    if (!bAdaptiveSyncReleased)
    {
        if (VSync->GetInt() == 0) VSync->Set(1, ECVF_SetByCode); // the setting is authoritative
        // Release only on real missed refreshes: the frame time itself jumps past the
        // refresh interval (60 -> 30 halving), and the render/GPU work explains it.
        const bool bMissing = AdaptiveSyncFrameMs > BudgetMs * 1.15f
            && FMath::Max(AdaptiveSyncWorkMs, GameMs) > BudgetMs * 0.9f;
        AdaptiveSyncSlowSeconds = bMissing ? AdaptiveSyncSlowSeconds + DeltaTime : 0.0f;
        if (AdaptiveSyncSlowSeconds > 0.25f)
        {
            VSync->Set(0, ECVF_SetByCode);
            bAdaptiveSyncReleased = true;
            AdaptiveSyncFastSeconds = 0.0f;
            AdaptiveSyncReleasedAt = FPlatformTime::Seconds();
        }
    }
    else
    {
        // r503 (Stefan: "keeps turning FreeSync and VSync on and off ... re-syncing all the time"): with
        // the game thread near the 16.7 ms budget (busy Valley of Trials) every spawn hitch released
        // VSync and 1.5 s of slightly lighter frames took it back, over and over. Back to VSync only
        // after 20 s released and 5 s of clear headroom (work under 75 % of a refresh).
        AdaptiveSyncFastSeconds = FMath::Max(AdaptiveSyncWorkMs, GameMs) < BudgetMs * 0.75f ? AdaptiveSyncFastSeconds + DeltaTime : 0.0f;
        if (AdaptiveSyncFastSeconds > 5.0f && FPlatformTime::Seconds() - AdaptiveSyncReleasedAt > 20.0)
        {
            VSync->Set(1, ECVF_SetByCode);
            bAdaptiveSyncReleased = false;
            AdaptiveSyncSlowSeconds = 0.0f;
        }
    }
}

FString FWowClientPreferences::GetSyncStatusLabel()
{
    UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    // Name the variable-refresh tech by GPU vendor (the monitor decides whether it engages).
    const TCHAR* Vrr = IsRHIDeviceAMD() ? TEXT("FreeSync") : IsRHIDeviceNVIDIA() ? TEXT("G-Sync") : TEXT("Adaptive Sync");
    if (!Settings || !Settings->IsVSyncEnabled()) return FString::Printf(TEXT("%s only, no cap (VSync OFF)"), Vrr);
    if (bNvidiaForcedVsyncOff) return TEXT("DLSS-G (VSync off)");
    if (bFrameGeneration) return TEXT("VSync + frame gen");
    if (CVarAdaptiveSync.GetValueOnGameThread() == 0) return FString::Printf(TEXT("VSync %d Hz"), AdaptiveSyncHz);
    // Active mode first: VSync while frames keep up with the refresh, FreeSync/G-Sync
    // (VSync released) while they don't.
    const float Fps = AdaptiveSyncFrameMs > 0.0f ? 1000.0f / AdaptiveSyncFrameMs : 0.0f;
    if (!bAdaptiveSyncReleased) return FString::Printf(TEXT("VSync ACTIVE %d Hz (%s on drops)"), AdaptiveSyncHz, Vrr);
    // Released: variable refresh only covers frame rates at or below the refresh.
    // r503: released means the frame cap sits 2 under the refresh, inside the variable-refresh range.
    return FString::Printf(TEXT("%s ACTIVE (VSync released, cap %d fps)"), Vrr, AdaptiveSyncHz - 2);
}
