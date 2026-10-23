#include "WowUIManager.h"
#include "WowUiSurfaces.h"
#include "WowUiVrHost.h"
#include "WowWindowStyle.h"
#include "WowUiMessageScript.h"
#include "WowBuffFrameScript.h"
#include "WowKeyBindings.h"
#include "XmlFile.h"
#include "XmlNode.h"
#include "WowLuaVM.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowFontManager.h"
#include "WowFrameXmlParser.h"
#include "WowAddonLoader.h"
#include "WowAddonSettings.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Mpq/MpqManager.h"
#include "Components/CanvasPanel.h"
#include "WowEntity.h"
#include "WowEntityManager.h"
#include "WowConnectionManager.h"
#include "WowSavedVariables.h"
#include "WowClientPreferences.h"
#include "Components/TextBlock.h"
#include "WowAudioManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowSpellPresentation.h"
#include "Widgets/SOverlay.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#define HAS_LUA 1
#else
#define HAS_LUA 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowUIManager, Log, All);

namespace WowLuaApi
{
	void SetAddonLoaderContext(FMpqManager* Mpq, FWowLuaVM* LuaVM, FWowFrameManager* FrameManager, FWowEventSystem* EventSystem);
}

namespace
{
FString ResolveFrameXmlAssetPath(const FString& InPath)
{
	FString ResolvedPath = InPath;
	ResolvedPath.ReplaceInline(TEXT("/"), TEXT("\\"));

	if (!ResolvedPath.StartsWith(TEXT("Interface\\"), ESearchCase::IgnoreCase))
	{
		ResolvedPath = TEXT("Interface\\FrameXML\\") + ResolvedPath;
	}

	return ResolvedPath;
}

bool TryReadUiAssetFile(FMpqManager* Mpq, const FString& InPath, TArray<uint8>& OutData)
{
	if (!Mpq)
	{
		return false;
	}

	FString NormalizedPath = InPath;
	NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));

	TArray<FString> Candidates;
	Candidates.AddUnique(NormalizedPath);
	Candidates.AddUnique(NormalizedPath.ToLower());
	Candidates.AddUnique(NormalizedPath.Replace(TEXT("\\"), TEXT("/")));
	Candidates.AddUnique(NormalizedPath.ToLower().Replace(TEXT("\\"), TEXT("/")));

	for (const FString& Candidate : Candidates)
	{
		if (Mpq->ReadFile(Candidate, OutData))
		{
			return true;
		}
	}

	return false;
}
}

UWowUIManager::UWowUIManager()
{
}

void UWowUIManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CreateCoreSystems();
}

void UWowUIManager::CreateCoreSystems()
{
	// Create core UI systems
	LuaVM = MakeUnique<FWowLuaVM>();
	FrameManager = MakeUnique<FWowFrameManager>();
	EventSystem = MakeUnique<FWowEventSystem>();
	if (!FontManager)
	{
		// Kept across ReloadUI: native widgets keep using its font faces.
		FontManager = MakeUnique<FWowFontManager>();
	}
	if (!InventoryManager.IsValid())
	{
		InventoryManager = MakeShareable(new FWowInventoryManager());
	}

	// Initialize the Lua VM
	if (!LuaVM->Initialize())
	{
		UE_LOG(LogWowUIManager, Error, TEXT("Failed to initialize Lua VM"));
		return;
	}

	// Wire the systems together
	EventSystem->SetLuaVM(LuaVM.Get());
	EventSystem->SetFrameManager(FrameManager.Get());
	FrameManager->SetEventSystem(EventSystem.Get());

	// Set up the Lua context so API functions can access game systems
	// EntityManager and ConnectionManager will be set later when networking is ready
	UIContext = new FWowLuaContext();
	UIContext->EventSystem = EventSystem.Get();
	UIContext->FrameManager = FrameManager.Get();
	WowLuaApi::SetContext(LuaVM->GetState(), UIContext);

	UE_LOG(LogWowUIManager, Log, TEXT("WoW UI Manager initialized (Lua VM + FrameManager + EventSystem)"));
}

namespace
{
	bool GReloadUIRequested = false;
	bool GAddonManagerRequested = false;
}

void UWowUIManager::RequestReloadUI() { GReloadUIRequested = true; }
void UWowUIManager::RequestAddonManager() { GAddonManagerRequested = true; }
bool UWowUIManager::ConsumeReloadRequest() { const bool bRequested = GReloadUIRequested; GReloadUIRequested = false; return bRequested; }
bool UWowUIManager::ConsumeAddonManagerRequest() { const bool bRequested = GAddonManagerRequested; GAddonManagerRequested = false; return bRequested; }

bool UWowUIManager::ReloadUI()
{
	UCanvasPanel* Canvas = FrameManager ? FrameManager->GetRootCanvas() : nullptr;
	if (!bUILoaded || !LoadedMpq || !LoadedAssetCache || !Canvas)
	{
		UE_LOG(LogWowUIManager, Warning, TEXT("ReloadUI ignored: UI not loaded"));
		return false;
	}
	UE_LOG(LogWowUIManager, Log, TEXT("ReloadUI: saving variables and rebuilding the UI"));

	TFunction<bool(const FString&, bool)> Binding = UIContext ? UIContext->RunBinding : nullptr;
	const uint64 FocusGuid = UIContext ? UIContext->FocusGuid : 0;

	if (UIOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		FWowUiSurfaces::Remove(UIOverlay.ToSharedRef());
	}
	UIOverlay.Reset();
	BankWindow.Reset();
	if (LuaVM && LuaVM->GetState())
	{
		WowLuaApi::ClearContext(LuaVM->GetState());
	}
	delete UIContext;
	UIContext = nullptr;
	if (LuaVM) LuaVM->Shutdown();
	FWowWindowStyle::Reset();
	EventSystem.Reset();
	FWowUiVrHost::ClearFrameManager(FrameManager.Get());
	FrameManager.Reset();
	LuaVM.Reset();
	Canvas->ClearChildren();
	bUILoaded = false;
	BindingScripts.Reset();
	BindingRunsOnRelease.Reset();

	CreateCoreSystems();
	if (UIContext)
	{
		UIContext->RunBinding = MoveTemp(Binding);
		UIContext->FocusGuid = FocusGuid;
	}
	SetRootCanvas(Canvas);
	if (ConnectionManager) SetConnectionManager(ConnectionManager);
	SetAudioManager(AudioManager);
	// FrameXML OnLoad/OnHide scripts are not user actions: no panel sounds while rebuilding.
	if (AudioManager) AudioManager->SetUiSoundsSuppressed(true);
	LoadUI(LoadedMpq, LoadedAssetCache);
	if (AudioManager) AudioManager->SetUiSoundsSuppressed(false);
	UE_LOG(LogWowUIManager, Log, TEXT("ReloadUI: %s"), bUILoaded ? TEXT("done") : TEXT("FAILED"));
	return bUILoaded;
}

FString UWowUIManager::GetGlobalString(const FString& Name) const
{
#if HAS_LUA
	lua_State* L = LuaVM ? LuaVM->GetState() : nullptr;
	if (!L || Name.IsEmpty()) return FString();
	lua_getglobal(L, TCHAR_TO_UTF8(*Name));
	const FString Value = lua_type(L, -1) == LUA_TSTRING ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
	lua_pop(L, 1);
	return Value;
#else
	return FString();
#endif
}

void UWowUIManager::Deinitialize()
{
	if (LuaVM && LuaVM->GetState())
	{
		WowLuaApi::ClearContext(LuaVM->GetState());
	}

	delete UIContext;
	UIContext = nullptr;

	if (LuaVM)
	{
		LuaVM->Shutdown();
	}

	FWowWindowStyle::Reset();
	EventSystem.Reset();
	FWowUiVrHost::ClearFrameManager(FrameManager.Get());
	FrameManager.Reset();
	FontManager.Reset();
	LuaVM.Reset();

	bUILoaded = false;

	UE_LOG(LogWowUIManager, Log, TEXT("WoW UI Manager deinitialized"));

	Super::Deinitialize();
}


// r428 (Stefan 23.10: "What's that for, that can't go in the code? And why is it on for TheraWoW?"):
// the client's own Lua changes to the stock frames, formerly the built-in addon WowUnreal_UI. They run
// after FrameXML and before the player's addons (which can restyle anything again), and are not an
// entry in the AddOns list. The flat dialog restyle (Popups.lua) is gone: the dialogs keep the
// client's own look since r276, but an old copy of that file still flattened them on dist-play.
static const char* WowClientUiTweaksLua = R"LUA(
-- Cast bar spell names in the middle of the bar (the stock text hangs from the bar's top edge).
for _, name in ipairs({ "CastingBarFrame", "PetCastingBarFrame", "TargetFrameSpellBar", "FocusFrameSpellBar" }) do
    local bar, text = _G[name], _G[name .. "Text"]
    if bar and text then
        text:ClearAllPoints()
        text:SetPoint("CENTER", bar, "CENTER", 0, 1.5)
    end
end

-- Game Menu "AddOns" button: the client's AddOns list in game (3.3.5 only has it at character select).
if GameMenuFrame and GameMenuButtonMacros and GameMenuButtonLogout and not GameMenuButtonAddOns then
    local button = CreateFrame("Button", "GameMenuButtonAddOns", GameMenuFrame, "GameMenuButtonTemplate")
    button:SetText(ADDONS or "AddOns")
    button:ClearAllPoints()
    button:SetPoint("TOP", GameMenuButtonMacros, "BOTTOM", 0, -1)
    GameMenuButtonLogout:ClearAllPoints()
    GameMenuButtonLogout:SetPoint("TOP", button, "BOTTOM", 0, -16)
    GameMenuFrame:SetHeight((GameMenuFrame:GetHeight() or 248) + 21)
    button:SetScript("OnClick", function()
        if PlaySound then PlaySound("igMainMenuOption") end
        HideUIPanel(GameMenuFrame)
        if ShowAddonManager then ShowAddonManager() end
    end)
end

-- "Use your WoW client's settings?": asked once per realm when the WoW client folder has a WTF
-- folder; remembered per realm, applies after a reload. /wowsettings shows it again.
do
    local WHITE = "Interface\\Buttons\\WHITE8X8"
    local LINE, PANEL = { .62, .52, .32, 1 }, { .035, .035, .04, .94 }
    local function flat(texture, r, g, b, a)
        texture:SetTexture(WHITE) texture:SetTexCoord(0, 1, 0, 1) texture:SetVertexColor(r, g, b, a or 1)
    end
    local function fontSize(fontString, size)
        local file, _, flags = fontString:GetFont()
        fontString:SetFont(file or "Fonts\\FRIZQT__.TTF", size, flags)
    end
    local CHOICES = {
        { mode = 1, label = "Fresh copy each launch",
          text = "Read your WoW client's settings every time. Changes made here are thrown away at the next launch. The WoW client's files are never changed." },
        { mode = 2, label = "Copy once, keep my own",
          text = "Use your WoW client's settings for anything this client has not saved yet; after that this client keeps its own. The WoW client's files are never changed." },
        { mode = 0, label = "Keep separate", text = "This client keeps its own settings only." },
        { mode = 3, label = "Share (with backup)",
          text = "Both clients read and write the same files. A backup is made at every launch. Run one client at a time: the one that saves last wins." },
    }
    local WIDTH, PAD, ROW, FIRST_ROW, BUTTON_W, BUTTON_H = 500, 20, 64, 92, 180, 24
    local HEIGHT = FIRST_ROW + #CHOICES * ROW + 44
    local frame
    local function text(parent, template, x, y, w, h, justifyH)
        local fs = parent:CreateFontString(nil, "OVERLAY", template)
        fs:SetPoint("TOPLEFT", parent, "TOPLEFT", x, -y)
        fs:SetWidth(w) fs:SetHeight(h)
        fs:SetJustifyH(justifyH or "LEFT") fs:SetJustifyV("TOP")
        return fs
    end
    local function button(parent, label, x, y, w, onClick, fromBottom)
        local b = CreateFrame("Button", nil, parent)
        b:SetWidth(w) b:SetHeight(BUTTON_H)
        if fromBottom then b:SetPoint("BOTTOM", parent, "BOTTOM", 0, y) else b:SetPoint("TOPLEFT", parent, "TOPLEFT", x, -y) end
        local bg = b:CreateTexture(nil, "BACKGROUND") bg:SetAllPoints(b) flat(bg, .30, .035, .012)
        local edge = b:CreateTexture(nil, "BORDER")
        edge:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0) edge:SetPoint("BOTTOMRIGHT", b, "TOPRIGHT", 0, -1)
        flat(edge, unpack(LINE))
        local hl = b:CreateTexture(nil, "HIGHLIGHT") hl:SetAllPoints(b) flat(hl, 1, 1, 1, .12)
        local fs = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        fs:SetAllPoints(b) fs:SetJustifyH("CENTER") fs:SetJustifyV("MIDDLE") fs:SetText(label) fontSize(fs, 12)
        b:SetScript("OnClick", onClick)
        return b
    end
    local function build()
        frame = CreateFrame("Frame", "WowUnrealWtfPrompt", UIParent)
        frame:SetFrameStrata("DIALOG")
        frame:SetWidth(WIDTH) frame:SetHeight(HEIGHT)
        frame:SetPoint("CENTER", UIParent, "CENTER", 0, 40)
        frame:SetBackdrop({ bgFile = WHITE, edgeFile = WHITE, tile = false, edgeSize = 1,
            insets = { left = 1, right = 1, top = 1, bottom = 1 } })
        frame:SetBackdropColor(unpack(PANEL))
        frame:SetBackdropBorderColor(unpack(LINE))
        frame:EnableMouse(true)
        frame:Hide()
        frame.title = text(frame, "GameFontNormalLarge", PAD, 16, WIDTH - 2 * PAD, 24, "CENTER")
        frame.intro = text(frame, "GameFontHighlightSmall", PAD, 46, WIDTH - 2 * PAD, 36, "CENTER")
        for i, choice in ipairs(CHOICES) do
            local y = FIRST_ROW + (i - 1) * ROW
            button(frame, choice.label, PAD, y, BUTTON_W, function()
                __WowSetWtfMode(choice.mode)
                frame:Hide()
                ReloadUI()
            end)
            local note = text(frame, "GameFontHighlightSmall", PAD + BUTTON_W + 14, y + 1,
                WIDTH - 2 * PAD - BUTTON_W - 14, ROW - 8, "LEFT")
            note:SetText(choice.text)
        end
        button(frame, "Ask me later", 0, 14, 140, function() frame:Hide() end, true)
        tinsert(UISpecialFrames, "WowUnrealWtfPrompt")
    end
    local function show()
        if not __WowWtfInfo then return end
        local _, hasClientWtf, realm = __WowWtfInfo()
        if not hasClientWtf then
            DEFAULT_CHAT_FRAME:AddMessage("WowUnreal: no WoW client WTF folder was found beside the Data folder.")
            return
        end
        if not frame then build() end
        frame.title:SetText("Use your WoW client's settings?")
        frame.intro:SetText("Addon settings, macros, key bindings and the AddOns list for " .. (realm or "this realm")
            .. ". You can change this later with /wowsettings.")
        frame:Show()
    end
    local watcher = CreateFrame("Frame")
    watcher:RegisterEvent("PLAYER_ENTERING_WORLD")
    watcher:SetScript("OnEvent", function(self)
        self:UnregisterEvent("PLAYER_ENTERING_WORLD")
        if not __WowWtfInfo then return end
        local chosen, hasClientWtf = __WowWtfInfo()
        if chosen == -1 and hasClientWtf then show() end
    end)
    SLASH_WOWUNREALSETTINGS1 = "/wowsettings"
    SlashCmdList["WOWUNREALSETTINGS"] = show
end
)LUA";

void UWowUIManager::LoadUI(FMpqManager* Mpq, FWowAssetCache* AssetCache)
{
	// A fresh load already reads the current AddOns list; drop a reload requested at character select.
	GReloadUIRequested = false;
	if (!Mpq || !AssetCache || !LuaVM || !LuaVM->IsInitialized())
	{
		UE_LOG(LogWowUIManager, Error, TEXT("Cannot load UI: null MPQ/AssetCache or Lua VM not initialized"));
		return;
	}

	if (bUILoaded)
	{
		UE_LOG(LogWowUIManager, Warning, TEXT("UI already loaded, skipping"));
		return;
	}

	UE_LOG(LogWowUIManager, Log, TEXT("Loading WoW UI from MPQ..."));
	LoadedMpq = Mpq;
	LoadedAssetCache = AssetCache;
	FWowAddonSettings::SetMpq(Mpq);

	// 0. Initialize font manager first (required for FrameXML font rendering)
	if (!FontManager->Initialize(Mpq))
	{
		UE_LOG(LogWowUIManager, Error, TEXT("Failed to initialize font manager"));
		return;
	}

	// Wire dependencies to frame manager
	FrameManager->SetFontManager(FontManager.Get());
	FrameManager->SetMpqManager(Mpq);
	FrameManager->SetAssetCache(AssetCache);
	FWowWindowStyle::Load(*FrameManager);

	if (FrameManager->FindFrame(TEXT("UIParent")) == -1)
	{
		UE_LOG(LogWowUIManager, Error, TEXT("Cannot load UI before SetRootCanvas initializes the FrameManager/UIParent"));
		return;
	}

	WowLuaApi::SetAddonLoaderContext(Mpq, LuaVM.Get(), FrameManager.Get(), EventSystem.Get());

	// 1. Load FrameXML (Interface/FrameXML/FrameXML.toc) — the core UI system
	TArray<FWowXmlDirective> FrameXmlDirectives = FWowFrameXmlParser::LoadFrameXml(Mpq);
	UE_LOG(LogWowUIManager, Log, TEXT("LoadFrameXml returned %d directives"), FrameXmlDirectives.Num());

	int32 FrameCount = 0;
	if (EventSystem)
	{
		EventSystem->BeginOnLoadBatch();
	}

	for (const FWowXmlDirective& Dir : FrameXmlDirectives)
	{
		switch (Dir.Type)
		{
		case FWowXmlDirective::EType::Script:
		{
			if (!Dir.FilePath.IsEmpty())
			{
				const FString ScriptPath = ResolveFrameXmlAssetPath(Dir.FilePath);
				TArray<uint8> ScriptData;
				if (TryReadUiAssetFile(Mpq, ScriptPath, ScriptData))
				{
					LuaVM->ExecuteBuffer(ScriptData, ScriptPath);
				}
				else
				{
					UE_LOG(LogWowUIManager, Warning, TEXT("Failed to load FrameXML script: %s"), *ScriptPath);
				}
			}
			break;
		}
		case FWowXmlDirective::EType::Frame:
		{
			if (FrameManager)
			{
				FrameManager->CreateFrame(Dir.FrameDef);
				FrameCount++;
			}
			break;
		}
		case FWowXmlDirective::EType::Include:
			// LoadFrameXml now expands includes recursively; keep this as a no-op guard.
			break;
		case FWowXmlDirective::EType::Font:
		{
			if (FontManager && !Dir.FontName.IsEmpty())
			{
				FontManager->RegisterFontMapping(Dir.FontName, Dir.FontInherits, static_cast<int32>(Dir.FontHeight), Dir.FontFile);
				FontManager->RegisterFontStyle(Dir.FontName, Dir.FontInherits, Dir.bFontHasHeight, Dir.FontHeight,
					Dir.bFontHasColor ? &Dir.FontColor : nullptr);
				WowLuaApi::RegisterFontObject(LuaVM->GetState(), Dir.FontName, Dir.FontInherits,
					Dir.bFontHasHeight ? Dir.FontHeight : 0.f, Dir.FontFlags, Dir.FontFile,
					Dir.bFontHasColor ? &Dir.FontColor : nullptr);
				UE_LOG(LogWowUIManager, Verbose, TEXT("Registered FrameXML Font: %s"), *Dir.FontName);
			}
			break;
		}
		}
	}

	if (EventSystem)
	{
		EventSystem->EndOnLoadBatch();
	}

	UE_LOG(LogWowUIManager, Log, TEXT("FrameXML: processed %d directives, created %d frames"),
		FrameXmlDirectives.Num(), FrameCount);

    TArray<uint8> BindingData;
    if(TryReadUiAssetFile(Mpq,TEXT("Interface\\FrameXML\\Bindings.xml"),BindingData))
    {
        FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(BindingData.GetData()),BindingData.Num());
        FXmlFile Xml(FString(Text.Length(),Text.Get()),EConstructMethod::ConstructFromBuffer);
        if(const FXmlNode* Root=Xml.GetRootNode())for(const FXmlNode* Node:Root->GetChildrenNodes())
        {
            const FString Command=Node->GetAttribute(TEXT("name"));
            if(Node->GetTag()!=TEXT("Binding") || Command.IsEmpty())continue;
            BindingScripts.Add(Command,Node->GetContent());
            if(Node->GetAttribute(TEXT("runOnUp"))==TEXT("true"))BindingRunsOnRelease.Add(Command);
        }
    }

	LuaVM->ExecuteString(WowUiMessageScript(),TEXT("=SeparatedUiMessages"));

	// Our own Lua changes to the stock frames (cast bar text position, Game Menu AddOns button,
	// WoW client settings prompt); r428: part of the client, no longer an addon.
	LuaVM->ExecuteString(UTF8_TO_TCHAR(WowClientUiTweaksLua), TEXT("=WowUnrealUI"));
	// The combat log (ChatFrame2) is docked under ChatFrame1 like FCF_DockFrame does in the client
	// (TOPLEFT to TOPLEFT); our native chat layout placed it without an anchor, and
	// Blizzard_CombatLog_AdjustCombatLogHeight then read a nil x offset from GetPoint 16 times at
	// login ("uiparent-manage" errors, Blizzard_CombatLog.lua:3451).
	LuaVM->ExecuteString(TEXT("if ChatFrame2 and ChatFrame1 and ChatFrame2.GetNumPoints and ChatFrame2:GetNumPoints() == 0 then")
		TEXT(" ChatFrame2:SetPoint('TOPLEFT', ChatFrame1, 'TOPLEFT', 0, 0) end"), TEXT("=DockCombatLog"));
	// The native quest dialog is the gossip and quest window. Its conversation now fires the stock
	// events (GOSSIP_SHOW, QUEST_DETAIL ...) for addons; the stock GossipFrame and QuestFrame must not
	// open a second copy on top of it.
	LuaVM->ExecuteString(TEXT("for _,n in ipairs({'GossipFrame','QuestFrame'}) do local f=_G[n]; if f and f.UnregisterAllEvents then f:UnregisterAllEvents() end end"),
		TEXT("=NativeQuestDialog"));
	// Cast bar spell name with a black outline so it reads on the bright fill (Stefan's request;
	// the stock bar only has a drop shadow); WowClientUiTweaksLua centres it on the bar.
	// Set before addons load, so addons can restyle it.
	if (FrameManager)
		for (const TCHAR* Name : {TEXT("CastingBarFrameText"), TEXT("PetCastingBarFrameText"), TEXT("TargetFrameSpellBarText"), TEXT("FocusFrameSpellBarText")})
			if (UTextBlock* Text = FrameManager->GetNamedFontString(Name))
			{
				FSlateFontInfo Font = Text->GetFont();
				Font.OutlineSettings.OutlineSize = 1;
				Font.OutlineSettings.OutlineColor = FLinearColor::Black;
				Text->SetFont(Font);
				// Centre the capitals on the bar both ways (the line box put "Smite" low):
				// cap height ~0.70 em, offset = half line box minus the capitals' middle.
				Text->SetJustification(ETextJustify::Center);
				if (FSlateApplication::IsInitialized())
				{
					const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
					const float LineH = static_cast<float>(Measure->GetMaxCharacterHeight(Font));
					const float Descender = -static_cast<float>(Measure->GetBaseline(Font));
					const float CapH = 0.70f * Font.Size * (96.f / 72.f);
					const float CapOffset = LineH > 0.f ? FMath::RoundToFloat(LineH * .5f - ((LineH - Descender) - CapH * .5f)) : 0.f;
					// Anchored to the bar centre above; only the capitals' optical offset remains.
					Text->SetRenderTranslation(FVector2D(0.f, CapOffset));
				}
			}



	// Addons read UnitClass/UnitRace/UnitLevel("player") and the talent tabs while their files
	// load (ElvUI: E.myclass at file scope). The real client has the player object by then;
	// here the UI loads right after LOGIN_VERIFY_WORLD, before the player's first update.
	// Seed race/class/gender/level from the character list until the server's values arrive.
	if (ConnectionManager)
	{
		FWowEntityManager& Entities = ConnectionManager->PacketHandler.EntityManager;
		FWowCharacterInfo Character;
		if (Entities.LocalPlayerGuid != 0 && ConnectionManager->GetSelectedCharacterInfo(Character)
			&& static_cast<uint64>(Character.Guid) == Entities.LocalPlayerGuid)
		{
			const FWowPlayerEntity* Existing = Entities.FindPlayer(Entities.LocalPlayerGuid);
			if (!Existing || Existing->GetClassId() == 0)
			{
				FWowEntity& Player = Entities.PromoteToTyped(Entities.LocalPlayerGuid,
					WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
				Player.SetField(UnitField::BYTES_0, static_cast<uint32>(Character.Race)
					| (static_cast<uint32>(Character.Class) << 8) | (static_cast<uint32>(Character.Gender) << 16));
				Player.SetField(UnitField::LEVEL, Character.Level);
				UE_LOG(LogTemp, Log, TEXT("UI load: seeded local player race %d class %d level %d from the character list"),
					Character.Race, Character.Class, Character.Level);
			}
		}
	}

	// Hearthstone tooltip ("$z"): the home location from SMSG_BINDPOINTUPDATE.
	if (ConnectionManager)
	{
		FWowSpellPresentation::SetHomeAreaId(ConnectionManager->PacketHandler.BindAreaId);
		if (!ConnectionManager->PacketHandler.OnBindPointUpdate.IsBound())
		{
			ConnectionManager->PacketHandler.OnBindPointUpdate.AddLambda([](float, float, float, uint32, uint32 AreaId)
			{
				FWowSpellPresentation::SetHomeAreaId(AreaId);
			});
		}
	}

	// SavedVariables, macros and AddOns-per-character data go to WTF\Account\<ACCOUNT>\<Realm>\
	// <Character> like the client. SetIdentity was never called, so every account, realm and
	// character shared "WowTestUser\WowUnreal\Player" (Stefan 11.10: Bartender4 settings).
	// Offline keeps those defaults.
	if (ConnectionManager)
		FWowSavedVariables::SetIdentity(ConnectionManager->GetAccountName().ToUpper(),
			ConnectionManager->GetSelectedRealmName(), ConnectionManager->GetCharacterName());
	// The WoW client's own WTF folder (beside its Data folder) and how this realm uses it (chosen
	// in the WowUnreal_UI prompt; not chosen yet / offline = our own settings only).
	{
		const FString ClientWtf = Mpq ? FPaths::Combine(FPaths::GetPath(Mpq->GetDataPath()), TEXT("WTF")) : FString();
		const int32 WtfMode = FWowSavedVariables::HasIdentity() && ConnectionManager
			? FWowClientPreferences::GetClientWtfMode(ConnectionManager->GetSelectedRealmName()) : 0;
		FWowSavedVariables::ConfigureClientWtf(ClientWtf,
			static_cast<FWowSavedVariables::EClientWtfMode>(FMath::Clamp(WtfMode, 0, 3)));
		FWowAddonSettings::Reload();
	}

	// 2. Load startup addons (Blizzard + user addons) in dependency order.
	// Each addon fires ADDON_LOADED(addonName) as it finishes loading.
	// Each addon fires ADDON_LOADED(addonName) after loading
	FWowAddonLoader::LoadAllAddons(Mpq, LuaVM.Get(), FrameManager.Get(), EventSystem.Get());

	if (FrameManager)
	{
		FrameManager->SyncChildVisibility();
	}

	// 2.5. Post-load Lua fixups: initialize fields that OnLoad should have set
	// but didn't (e.g., frame templates missing from MPQ XML, OnLoad not reaching
	// child frames, etc.). This runs after all frames and addons are loaded.
#if HAS_LUA
	if (LuaVM && LuaVM->IsInitialized())
	{
		const char* PostLoadFixup = R"LUA(
            SHOW_MULTI_ACTIONBAR_1, SHOW_MULTI_ACTIONBAR_2, SHOW_MULTI_ACTIONBAR_3, SHOW_MULTI_ACTIONBAR_4 = GetActionBarToggles()
            ALWAYS_SHOW_MULTIBARS = GetCVar("alwaysshowactionbars")
            for i, label in ipairs({"Actionbar 3 (bottom)", "Actionbar 4 (bottom)", "Actionbar 5 (right)", "Actionbar 6 (right)"}) do
                _G["SHOW_MULTIBAR"..i.."_TEXT"] = label
            end

			-- Bind the player's health/power event handlers after all region globals
			-- exist. The full legacy PlayerFrame_OnLoad also starts unrelated PvP,
			-- vehicle and pet bootstrap paths whose APIs are not implemented yet.
			if PlayerFrame and not PlayerFrame.unit and UnitFrame_Initialize then
				UnitFrame_Initialize(PlayerFrame, "player", PlayerName, PlayerPortrait,
					PlayerFrameHealthBar, PlayerFrameHealthBarText,
					PlayerFrameManaBar, PlayerFrameManaBarText, PlayerFrameFlash)
			end
			-- Seed stock unit-frame preferences independently of options-panel loading.
			SHOW_TARGET_OF_TARGET = GetCVar("showTargetOfTarget")
			SHOW_TARGET_OF_TARGET_STATE = GetCVar("targetOfTargetMode")
			for _, entry in ipairs({
				{PlayerFrameHealthBar, PlayerFrameHealthBarText},
				{PlayerFrameManaBar, PlayerFrameManaBarText}
			}) do
				local bar, text = entry[1], entry[2]
				if bar and text then
					bar.TextString = text
					bar.cvar = "playerStatusText"
					bar.cvarLabel = "STATUS_TEXT_PLAYER"
					bar.textLockable = 1
					bar.lockShow = bar.lockShow or 0
					bar:RegisterEvent("CVAR_UPDATE")
				end
			end
            -- Keep health text numeric, including corpses. Refresh identity after
            -- delayed object fields arrive, even if a stock OnLoad ran too early.
            function WowRefreshUnitFrames()
                local entries = {
                    {"player", PlayerFrame, PlayerName, PlayerLevelText, PlayerPortrait,
                     PlayerFrameHealthBar, PlayerFrameHealthBarText, PlayerFrameManaBar, PlayerFrameManaBarText},
                    {"target", TargetFrame, TargetFrameTextureFrameName, TargetFrameTextureFrameLevelText, TargetFramePortrait,
                     TargetFrameHealthBar, TargetFrameTextureFrameHealthBarText, TargetFrameManaBar, TargetFrameTextureFrameManaBarText},
                    {"focus", FocusFrame, FocusFrameTextureFrameName, FocusFrameTextureFrameLevelText, FocusFramePortrait,
                     FocusFrameHealthBar, FocusFrameTextureFrameHealthBarText, FocusFrameManaBar, FocusFrameTextureFrameManaBarText}
                }
                for _, e in ipairs(entries) do
                    local unit, frame = e[1], e[2]
                    if frame and UnitExists(unit) then
                        frame.unit = unit
                        if e[3] then
                            e[3]:SetText(UnitName(unit) or "")
                            -- Size and outline come from the frame manager (fitted to the plate,
                            -- r194); a SetFont here undid that on every refresh.
                            e[3]:SetTextColor(1, 1, 1)
                            e[3]:SetShadowColor(0, 0, 0, 1)
                            e[3]:SetShadowOffset(1, -1)
                        end
                        if e[4] then e[4]:SetText(tostring(UnitLevel(unit) or "")) end
                        if e[5] then SetPortraitTexture(e[5], unit) end
                        for i = 6, 8, 2 do
                            local bar, text = e[i], e[i+1]
                            local value = i == 6 and UnitHealth(unit) or UnitPower(unit)
                            local maximum = i == 6 and UnitHealthMax(unit) or UnitPowerMax(unit)
                            if bar and text and maximum and maximum > 0 then
                                bar.TextString = text
                                bar.forceShow, bar.showNumeric, bar.zeroText, bar.pauseUpdates = true, true, nil, nil
                                bar:SetMinMaxValues(0, maximum)
                                bar:SetValue(value or 0)
                                text:SetText(string.format("%d / %d", value or 0, maximum))
                                text:Show()
                            elseif text then text:SetText("") end
                        end
                        local prefix = unit == "player" and "Player" or unit == "target" and "TargetFrame" or "FocusFrame"
                        local icon = unit == "player" and PlayerPVPIcon or _G[prefix.."TextureFramePVPIcon"]
                        if icon then
                            local faction = UnitFactionGroup(unit)
                            if UnitIsPVPFreeForAll(unit) then
                                icon:SetTexture("Interface\\TargetingFrame\\UI-PVP-FFA")
                                icon:Show()
                            elseif faction and UnitIsPVP(unit) then
                                icon:SetTexture("Interface\\TargetingFrame\\UI-PVP-"..faction)
                                icon:Show()
                            else icon:Hide() end
                        end
                        local background = unit == "target" and TargetFrameNameBackground or unit == "focus" and FocusFrameNameBackground
                        if background then
                            if not UnitPlayerControlled(unit) and UnitIsTapped(unit) and not UnitIsTappedByPlayer(unit) and not UnitIsTappedByAllThreatList(unit) then
                                background:SetVertexColor(0.5, 0.5, 0.5)
                            else
                                -- TargetFrame_CheckFaction: the full reaction colour (Stefan 11.10: it
                                -- turned dark red a few seconds after targeting).
                                local r,g,b = UnitSelectionColor(unit)
                                background:SetVertexColor(r, g, b, 1)
                            end
                        end
                        if frame.deadText then frame.deadText:Hide() end
                    end
                end
            end
            -- Recover spellbook setup skipped by the narrow OnLoad whitelist.
            -- Keep the original frame/page handlers and supply its native refresh API.
            function UpdateSpells()
                if not SpellBookFrame or not SpellButton_UpdateButton then return end
                local selected=math.min(SpellBookFrame.selectedSkillLine or 1, GetNumSpellTabs())
                SpellBookFrame.selectedSkillLine=math.max(1,selected)
                local _,_,offset,count=SpellBook_GetTabInfo(SpellBookFrame.selectedSkillLine)
                SpellBookFrame.selectedSkillLineOffset=offset or 0
                local maxPage=math.max(1,math.ceil((count or 0)/(SPELLS_PER_PAGE or 12)))
                SPELLBOOK_PAGENUMBERS[SpellBookFrame.selectedSkillLine]=math.max(1,math.min(maxPage,SPELLBOOK_PAGENUMBERS[SpellBookFrame.selectedSkillLine] or 1))
                for i=1,(SPELLS_PER_PAGE or 12) do
                    local button=_G["SpellButton"..i]
                    if button then SpellButton_UpdateButton(button) end
                end
                local page=SPELLBOOK_PAGENUMBERS[SpellBookFrame.selectedSkillLine]
                if page<=1 then SpellBookPrevPageButton:Disable() else SpellBookPrevPageButton:Enable() end
                if page>=maxPage then SpellBookNextPageButton:Disable() else SpellBookNextPageButton:Enable() end
                -- r390: the tab name and the tab shading follow every tab click and page turn (the
                -- SpellBookFrame_Update hook alone left "General" over Subtlety's spells).
                if __WowTidySpellBook then pcall(__WowTidySpellBook) end
            end
            if SpellBookFrame then
                SpellBookFrame.bookType = BOOKTYPE_SPELL or "spell"
                SpellBookFrame.selectedSkillLine = 1
                SpellBookFrame.selectedSkillLineOffset = 0
                SPELLBOOK_PAGENUMBERS = SPELLBOOK_PAGENUMBERS or {}
                for i=1,8 do SPELLBOOK_PAGENUMBERS[i] = 1 end
                SPELLBOOK_PAGENUMBERS[BOOKTYPE_PET or "pet"] = 1
                SpellBookFrame:RegisterEvent("SPELLS_CHANGED")
                -- Stefan 11.10: after a few UI-scale reloads the book opened with no skill-line tabs
                -- and empty slots - SpellBookFrame_Update shows tabs and spell icons only for
                -- bookType "spell". ToggleSpellBook(bookType) sets the type like UIParent.lua's
                -- (the native toggle ignored its argument), and the book never opens without one.
                local nativeToggleSpellBook = ToggleSpellBook
                ToggleSpellBook = function(bookType)
                    if bookType == (BOOKTYPE_PET or "pet") and not (HasPetSpells and HasPetSpells()) then return end
                    if SpellBookFrame and not SpellBookFrame:IsShown() then
                        SpellBookFrame.bookType = bookType or BOOKTYPE_SPELL or "spell"
                    end
                    return nativeToggleSpellBook(bookType)
                end
                -- r267: HookScript now chains (it used to replace SpellBookFrame_OnShow); hook once per frame.
                local hookSpellBookOnce = not SpellBookFrame.__wowOnShowHooks
                SpellBookFrame.__wowOnShowHooks = true
                if SpellBookFrame.HookScript and hookSpellBookOnce then
                    SpellBookFrame:HookScript("OnShow", function(self)
                        if self.bookType ~= (BOOKTYPE_PET or "pet") and self.bookType ~= (BOOKTYPE_SPELL or "spell") then
                            self.bookType = BOOKTYPE_SPELL or "spell"
                            if SpellBookFrame_Update then pcall(SpellBookFrame_Update, 1) end
                        end
                    end)
                end
                for i=1,8 do
                    local tab=_G["SpellBookSkillLineTab"..i]
                    if tab then
                        tab:RegisterForClicks("LeftButtonUp")
                        local index=i
                        tab:SetScript("OnClick",function(self) SpellBookSkillLineTab_OnClick(self,index); UpdateSpells() end)
                    end
                end
                for i=1,(SPELLS_PER_PAGE or 12) do
                    local button=_G["SpellButton"..i]
                    if button and SpellButton_OnLoad then SpellButton_OnLoad(button) end
                end
)LUA"
R"LUA(
                -- Page arrows step the page directly (Stefan 09.10: the next page needed two clicks).
                local function TurnPage(delta)
                    if SpellBookFrame.bookType == (BOOKTYPE_PET or "pet") then
                        if delta > 0 and SpellBookNextPageButton_OnClick then SpellBookNextPageButton_OnClick()
                        elseif delta < 0 and SpellBookPrevPageButton_OnClick then SpellBookPrevPageButton_OnClick() end
                        return
                    end
                    local sel=SpellBookFrame.selectedSkillLine or 1
                    local _,_,_,count=SpellBook_GetTabInfo(sel)
                    local maxPage=math.max(1,math.ceil((count or 0)/(SPELLS_PER_PAGE or 12)))
                    SPELLBOOK_PAGENUMBERS[sel]=math.max(1,math.min(maxPage,(SPELLBOOK_PAGENUMBERS[sel] or 1)+delta))
                    if PlaySound then PlaySound("igAbiliityPageTurn") end
                    UpdateSpells()
                    if SpellBookPageText and PAGE_NUMBER then SpellBookPageText:SetFormattedText(PAGE_NUMBER, SPELLBOOK_PAGENUMBERS[sel]) end
                end
                if SpellBookNextPageButton then
                    SpellBookNextPageButton:RegisterForClicks("LeftButtonUp")
                    SpellBookNextPageButton:SetScript("OnClick",function() TurnPage(1) end)
                end
                if SpellBookPrevPageButton then
                    SpellBookPrevPageButton:RegisterForClicks("LeftButtonUp")
                    SpellBookPrevPageButton:SetScript("OnClick",function() TurnPage(-1) end)
                end
                -- Stefan 10.10: no "Spellbook" tab at the bottom while it is the only book (no pet spells),
                -- and "Page N" in light brown ink on the parchment.
                local function TidySpellBook()
                    if SpellBookFrameTabButton1 and not (HasPetSpells and HasPetSpells()) then SpellBookFrameTabButton1:Hide() end
                    -- r369 (Stefan 16.10): the name of the chosen tab (General, Combat ...) centred under
                    -- "Spellbook", so the tab you are on can be read.
                    if SpellBookFrame and SpellBookTitleText and SpellBookFrame.CreateFontString then
                        local label = SpellBookFrame.__wowTabLabel
                        if not label then
                            label = SpellBookFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
                            label:SetPoint("TOP", SpellBookTitleText, "BOTTOM", 0, -10)
                            SpellBookFrame.__wowTabLabel = label
                        end
                        local tabName
                        if SpellBookFrame.bookType ~= (BOOKTYPE_PET or "pet") and GetSpellTabInfo then
                            tabName = GetSpellTabInfo(SpellBookFrame.selectedSkillLine or 1)
                        end
                        label:SetText(tabName or "")
                    end
                    -- r390 (Stefan 16.10: the tab icons did not show which tab is open): the open
                    -- tab's icon at full colour, the others darker but readable.
                    if SpellBookFrame then
                        local open = SpellBookFrame.bookType ~= (BOOKTYPE_PET or "pet") and (SpellBookFrame.selectedSkillLine or 1) or 0
                        for i = 1, 8 do
                            local tab = _G["SpellBookSkillLineTab" .. i]
                            local art = tab and tab.GetNormalTexture and tab:GetNormalTexture()
                            if art and art.SetVertexColor then
                                if i == open then art:SetVertexColor(1, 1, 1) else art:SetVertexColor(0.55, 0.55, 0.55) end
                            end
                        end
                    end
                    -- Stefan 10.10 (2): darker brown, centred between the page arrows (their centres
                    -- are BOTTOMLEFT 50,105 and 314,105).
                    if SpellBookPageText then
                        SpellBookPageText:SetTextColor(0.24, 0.14, 0.05)
                        if SpellBookPageText.SetJustifyH then SpellBookPageText:SetJustifyH("CENTER") end
                    end
                end
                if SpellBookPageText and SpellBookPageText.ClearAllPoints then
                    SpellBookPageText:ClearAllPoints()
                    SpellBookPageText:SetPoint("CENTER", SpellBookFrame, "BOTTOMLEFT", 182, 105)
                end
                -- The native Auction House replaces Blizzard_AuctionUI: AUCTION_HOUSE_SHOW must not load
                -- and show the FrameXML AuctionFrame. It opened as a doublewide panel for one frame,
                -- closed every other window (spellbook, character, bags) and made the native window
                -- flicker on open and on Search (Stefan 10.10). Addons still get the event.
                AuctionFrame_LoadUI = function() end
                -- Blizzard_Calendar (checked against its Lua, 10.10): C API it calls that the client
                -- does not have yet. Party/raid counts without the battleground group; roster
                -- requests are no-ops (the replies come by event).
                if not GetRealNumPartyMembers then GetRealNumPartyMembers = function() return GetNumPartyMembers and GetNumPartyMembers() or 0 end end
                if not GetRealNumRaidMembers then GetRealNumRaidMembers = function() return GetNumRaidMembers and GetNumRaidMembers() or 0 end end
                if not GuildRoster then GuildRoster = function() end end
                if not ArenaTeamRoster then ArenaTeamRoster = function() end end
                -- PVPFrame.lua (10.10): its OnShow stopped at GetCurrentArenaSeason, so the honor
                -- numbers, faction icon and portrait were never filled in (Stefan: PvP window).
                -- No arena teams yet: empty rosters, season 8 (AzerothCore's default).
                if not GetCurrentArenaSeason then GetCurrentArenaSeason = function() return 8 end end
                if not GetPreviousArenaSeason then GetPreviousArenaSeason = function() return 7 end end
                if not CloseBattlefield then CloseBattlefield = function() end end
                if not GetNumArenaTeamMembers then GetNumArenaTeamMembers = function() return 0 end end
                if not GetArenaTeamRosterInfo then GetArenaTeamRosterInfo = function() return nil end end
                if not GetArenaTeamRosterSelection then GetArenaTeamRosterSelection = function() return 0 end end
                if not SetArenaTeamRosterSelection then SetArenaTeamRosterSelection = function() end end
                if not IsArenaTeamCaptain then IsArenaTeamCaptain = function() return false end end
                -- FriendsFrame / MailFrame / PaperDollFrame OnShow/OnHide stopped at these (11.10 log).
                if not SetGuildRosterSelection then SetGuildRosterSelection = function() end end
                if not GetGuildRosterSelection then GetGuildRosterSelection = function() return 0 end end
                if not GetInboxNumItems then GetInboxNumItems = function() return 0, 0 end end
                if not EquipmentManagerClearIgnoredSlotsForSave then EquipmentManagerClearIgnoredSlotsForSave = function() end end
                -- r267 (12.10 log): OnLoad setup skipped by the narrow function="" OnLoad list.
                -- CharacterFrame_OnLoad's PanelTemplates_SetNumTabs (the micro button's click then
                -- failed in PanelTemplates_UpdateTabs: 'for' limit), MailFrame_OnLoad's InboxFrame.pageNum
                -- (OnHide failed on arithmetic with nil) and GearManagerDialogPopup_OnLoad (:SetSelection).
                if CharacterFrame and not CharacterFrame.numTabs and PanelTemplates_SetNumTabs then
                    PanelTemplates_SetNumTabs(CharacterFrame, 5)
                    if PanelTemplates_SetTab then pcall(PanelTemplates_SetTab, CharacterFrame, 1) end
                end
                if InboxFrame and not InboxFrame.pageNum then InboxFrame.pageNum = 1 end
                if GearManagerDialogPopup and not GearManagerDialogPopup.SetSelection and GearManagerDialogPopup_OnLoad then
                    pcall(GearManagerDialogPopup_OnLoad, GearManagerDialogPopup)
                end
                if not SetDesaturation then
                    SetDesaturation = function(texture, desaturation)
                        if texture and texture.SetDesaturated then texture:SetDesaturated(desaturation and true or false) end
                    end
                end
                if not GetDungeonNameWithDifficulty then
                    GetDungeonNameWithDifficulty = function(name, difficultyName)
                        name = name or ""
                        if difficultyName and difficultyName ~= "" then return name.." ("..difficultyName..")" end
                        return name
                    end
                end
                if hooksecurefunc and SpellBookFrame_Update then hooksecurefunc("SpellBookFrame_Update", TidySpellBook) end
                __WowTidySpellBook = TidySpellBook
                -- r369 (Stefan 16.10: empty spell slots showed a small square frame, only some the plain
                -- slot): an empty slot shows only its slot background, without the button frame.
                if hooksecurefunc and SpellButton_UpdateButton and hookSpellBookOnce then
                    hooksecurefunc("SpellButton_UpdateButton", function(self)
                        local name = self and self.GetName and self:GetName()
                        local icon = name and _G[name .. "IconTexture"]
                        local normal = name and _G[name .. "NormalTexture"]
                        if normal and normal.SetVertexColor and icon and icon.IsShown and not icon:IsShown() then
                            normal:SetVertexColor(1, 1, 1, 0)
                        end
                    end)
                end
                -- UIParent's panel slots (left/center/right ...) still named windows that native code hid
                -- with a plain Hide (Esc, NPC dialogs, redirects to native windows). ShowUIPanel then
                -- "replaced" the stale slot and hid the window it had just opened (Stefan 10.10:
                -- Spellbook, Talents, Quest Log, Social and PvP buttons opened nothing). Hidden windows
                -- are dropped from the slots; the C++ tick calls this a few times a second.
                do
                    local delegate
                    if debug and debug.getupvalue and GetUIPanel then
                        for i = 1, 8 do
                            local n, v = debug.getupvalue(GetUIPanel, i)
                            if not n then break end
                            if n == "FramePositionDelegate" then delegate = v; break end
                        end
                    end
)LUA"
R"LUA(
                    -- r392 (Stefan 16.10: in key binding mode "we need to see what all the action bar
                    -- slots have for keybindings"): while LibKeyBound's binding mode is on, every
                    -- slot of the shown action bars shows (empty ones too, like "Always show action
                    -- bars"), so a key can be bound to any of them; afterwards they hide again.
                    function __WowKeyBoundGridTick()
                        if __WowKeyBoundHooked or not LibStub then return end
                        local kb = LibStub("LibKeyBound-1.0", true)
                        if not kb or not kb.RegisterCallback then return end
                        __WowKeyBoundHooked = true
                        local owner, shown = {}, {}
                        local function EachSlot(fn)
                            local bars = Bartender4 and Bartender4.GetModule and Bartender4:GetModule("ActionBars", true)
                            if bars and bars.actionbars then
                                for _, bar in pairs(bars.actionbars) do
                                    if bar.buttons and not (bar.config and bar.config.showgrid) then
                                        for _, button in ipairs(bar.buttons) do fn(button, true) end
                                    end
                                end
                            end
                            for _, prefix in ipairs({"ActionButton", "MultiBarBottomLeftButton", "MultiBarBottomRightButton", "MultiBarRightButton", "MultiBarLeftButton"}) do
                                for i = 1, 12 do
                                    local button = _G[prefix .. i]
                                    local bar = button and button:GetParent()
                                    if bar and bar:IsVisible() then fn(button, false) end
                                end
                            end
                        end
                        kb.RegisterCallback(owner, "LIBKEYBOUND_ENABLED", function()
                            if InCombatLockdown and InCombatLockdown() then return end
                            EachSlot(function(button, bt4)
                                if bt4 then
                                    if button.ShowGrid and not button.gridShown then button:ShowGrid(); shown[button] = true end
                                elseif ActionButton_ShowGrid and not shown[button] then
                                    ActionButton_ShowGrid(button); shown[button] = true
                                end
                            end)
                        end)
                        kb.RegisterCallback(owner, "LIBKEYBOUND_DISABLED", function()
                            for button in pairs(shown) do
                                if button.HideGrid and button.gridShown ~= nil then button:HideGrid()
                                elseif ActionButton_HideGrid then ActionButton_HideGrid(button) end
                            end
                            shown = {}
                        end)
                        if __WowLog then __WowLog("LibKeyBound: binding mode shows every action slot") end
                    end
                    -- r404 (Stefan 16.10: "Make all tooltip use the same fade slider setting"): GameTooltip:Hide()
                    -- from a spell, item or button fades the tooltip out over Options > Interface > Tooltip Fade
                    -- Time (it stays, then fades over the last part, at most 0.4 s), like the world tooltip
                    -- (GC). New content (SetOwner / Show / ClearLines) ends the fade at once. The world
                    -- tooltip (owner UIParent) keeps its own fade in GC; 0 = hide at once, as before.
                    function __WowTooltipFadeTick()
                        local tip = GameTooltip
                        if not tip or tip.__wowFadeHooked or not __WowTooltipFadeTime or not CreateFrame or not GetTime then return end
                        tip.__wowFadeHooked = true
                        local realHide = tip.Hide
                        local fadeStart
                        local function StopFade() fadeStart = nil; if tip.SetAlpha then tip:SetAlpha(1) end end
                        tip.Hide = function(self, ...)
                            if self ~= tip then return realHide(self, ...) end
                            if fadeStart then return end -- already fading out
                            local total = __WowTooltipFadeTime() or 0
                            local owner = tip.GetOwner and tip:GetOwner()
                            if total <= 0 or not tip:IsShown() or owner == UIParent then return realHide(self, ...) end
                            fadeStart = GetTime()
                            -- r414 (Stefan 17.10: the bag tooltip stuck again): a fading tooltip has no owner
                            -- any more, as after Hide in the client. Bag and paper-doll slots refresh their
                            -- tooltip from OnUpdate while GameTooltip:IsOwned(self), which re-filled and
                            -- re-showed it during the fade and kept it up for good.
                            tip.__tooltipOwner = nil
                        end
                        -- r406 (Stefan 16.10: the player frame's tooltip never went away): unit frames leave
                        -- with GameTooltip:FadeOut(), which did nothing here; it fades like Hide now.
                        tip.FadeOut = function(self) return tip.Hide(self) end
                        -- r414: only a new owner ends the fade; Show / ClearLines alone come from the old
                        -- owner refreshing its lines and must not bring the tooltip back.
                        for _, method in ipairs({"SetOwner"}) do
                            local original = tip[method]
                            if original then
                                tip[method] = function(self, ...)
                                    if fadeStart then StopFade() end
                                    return original(self, ...)
                                end
                            end
                        end
                        local driver = CreateFrame("Frame")
                        driver:SetScript("OnUpdate", function()
                            if not fadeStart then return end
                            local total = __WowTooltipFadeTime() or 0
                            local fade = math.min(total, 0.4)
                            local hold = total - fade
                            local age = GetTime() - fadeStart
                            if age >= total then
                                fadeStart = nil
                                realHide(tip)
                                tip:SetAlpha(1)
                            elseif age > hold and fade > 0 then
                                tip:SetAlpha(1 - (age - hold) / fade)
                            end
                        end)
                    end
                    function __WowSanitizeUIPanels()
                        if __WowTooltipFadeTick then pcall(__WowTooltipFadeTick) end -- r404
                        if __WowKeyBoundGridTick then pcall(__WowKeyBoundGridTick) end -- r392
                        if __WowDiagTick then pcall(__WowDiagTick) end -- r279: AceConfig / Bartender diagnostics
                        if __WowWatchFrameTick then pcall(__WowWatchFrameTick) end -- r339: WatchFrame click hooks (LuaQuestLogApi.cpp)
                        if not delegate then return end
                        for _, key in ipairs({"left", "center", "right", "doublewide", "fullscreen"}) do
                            local f = rawget(delegate, key)
                            if f and f.IsShown and not f:IsShown() then rawset(delegate, key, nil) end
                        end
                    end
                end
                if SpellBookFrame.HookScript and hookSpellBookOnce then SpellBookFrame:HookScript("OnShow", TidySpellBook) end
                TidySpellBook()
)LUA"
R"LUA(
                -- r265 diagnostic (Stefan 12.10, r09t41: the book shows empty slots and no tabs while
                -- the log builds General 8 + three class tabs): what the book holds after it opens.
                -- r271 diagnostic (Stefan 13.10: no tooltip over spellbook spells): what the tooltip
                -- holds and where it is right after SpellButton_OnEnter.
                if hooksecurefunc and SpellButton_OnEnter and __WowLog and hookSpellBookOnce then
                    hooksecurefunc("SpellButton_OnEnter", function(self)
                        pcall(function()
                            local tip = GameTooltip
                            __WowLog(string.format("Spellbook tooltip: %s id=%s shown=%s lines=%s at %s,%s size %sx%s owner=%s",
                                tostring(self:GetName()), tostring(SpellBook_GetSpellID(self:GetID())), tostring(tip:IsShown()),
                                tostring(tip.NumLines and tip:NumLines()), tostring(tip:GetLeft()), tostring(tip:GetTop()),
                                tostring(tip:GetWidth()), tostring(tip:GetHeight()),
                                tostring(tip.GetOwner and tip:GetOwner() and tip:GetOwner():GetName())))
                        end)
                    end)
                end
                if SpellBookFrame.HookScript and __WowLog and hookSpellBookOnce then
                    SpellBookFrame:HookScript("OnShow", function(self)
                        pcall(function()
                            local line = self.selectedSkillLine or 1
                            local _, _, off, num = SpellBook_GetTabInfo(line)
                            local id, disp = SpellBook_GetSpellID(1)
                            local b, ic, nm, t = SpellButton1, SpellButton1IconTexture, SpellButton1SpellName, SpellBookSkillLineTab1
                            __WowLog(string.format("Spellbook state: book=%s line=%s tabs=%s off=%s num=%s page=%s id=%s disp=%s btn1 shown=%s enabled=%s icon=%s %s name=%s %s tab1 shown=%s at %s,%s showAll=%s",
                                tostring(self.bookType), tostring(line), tostring(GetNumSpellTabs()), tostring(off), tostring(num),
                                tostring(SPELLBOOK_PAGENUMBERS and SPELLBOOK_PAGENUMBERS[line]), tostring(id), tostring(disp),
                                tostring(b and b:IsShown()), tostring(b and b.IsEnabled and b:IsEnabled()),
                                tostring(ic and ic:IsShown()), tostring(ic and ic:GetTexture()),
                                tostring(nm and nm:IsShown()), tostring(nm and nm:GetText()),
                                tostring(t and t:IsShown()), tostring(t and t:GetLeft()), tostring(t and t:GetTop()),
                                tostring(GetCVarBool and GetCVarBool("ShowAllSpellRanks"))))
                        end)
                    end)
                end
            end
			-- BuffFrame: ensure OnLoad fields exist (OnLoad may have run on wrong self)
			if BuffFrame and BuffFrame.BuffFrameUpdateTime == nil then
				BuffFrame.BuffFrameUpdateTime = 0
				BuffFrame.BuffFrameFlashTime = 0
				BuffFrame.BuffFrameFlashState = 1
				BuffFrame.BuffAlphaValue = 1
				BuffFrame.numEnchants = 0
				BuffFrame.numConsolidated = 0
			end
			-- TemporaryEnchantFrame: needs TempEnchant button references
			if TemporaryEnchantFrame and TemporaryEnchantFrame.TemporaryEnchantFrameUpdateTime == nil then
				TemporaryEnchantFrame.TemporaryEnchantFrameUpdateTime = 0
			end
			-- CombatFeedback: initialize feedbackText directly on frames that have
			-- CombatFeedback_OnUpdate but never had CombatFeedback_Initialize called.
			-- The OnUpdate runs on these frames, not their child HitIndicator frames.
			local function EnsureCombatFeedback(frameName)
				local f = _G[frameName]
				if f and not f.feedbackText then
					-- Create a stub FontString that safely no-ops all method calls
					local noop = function() end
					local fs = setmetatable({ __objectType = "FontString" }, { __index = function(_, key)
						if key == "IsShown" or key == "IsVisible" then return function() return false end end
						return noop
					end })
					f.feedbackText = fs
					f.feedbackFontHeight = 20
				end
			end
			EnsureCombatFeedback("PlayerFrame")
			EnsureCombatFeedback("TargetFrame")
			EnsureCombatFeedback("PetFrame")

			-- Populate the player unit frame after all globals and the local entity
			-- context exist.  The stock event path can run too early during our
			-- combined FrameXML/addon load, leaving the portrait placeholder blank.
			if PlayerPortrait and SetPortraitTexture then
				SetPortraitTexture(PlayerPortrait, "player")
			end
			if MicroButtonPortrait and SetPortraitTexture then
				SetPortraitTexture(MicroButtonPortrait, "player")
			end
			if PlayerName and UnitName then
				PlayerName:SetText(UnitName("player") or "Player")
			end
			if PlayerLevelText and UnitLevel then
				PlayerLevelText:SetText(tostring(UnitLevel("player") or ""))
			end
			if UnitHealth and UnitHealthMax then
				local hp, hpMax = UnitHealth("player") or 1, UnitHealthMax("player") or 1
				if hpMax <= 0 then hpMax = 1 end
				if hp <= 0 and hpMax == 1 then hp = 1 end
				if PlayerFrameHealthBar then
					PlayerFrameHealthBar:SetMinMaxValues(0, hpMax)
					PlayerFrameHealthBar:SetValue(hp)
					PlayerFrameHealthBar:SetStatusBarColor(0.0, 1.0, 0.0, 1.0) -- HealthBar_OnValueChanged
				end
				if PlayerFrameHealthBarText then
					PlayerFrameHealthBarText:SetText(string.format("%d / %d", hp, hpMax))
				end
			end
			if UnitPower and UnitPowerMax then
				local mana, manaMax = UnitPower("player") or 1, UnitPowerMax("player") or 1
				if manaMax <= 0 then manaMax = 1 end
				if mana <= 0 and manaMax == 1 then mana = 1 end
				if PlayerFrameManaBar then
					PlayerFrameManaBar:SetMinMaxValues(0, manaMax)
					PlayerFrameManaBar:SetValue(mana)
					local powerToken = UnitPowerType and select(2, UnitPowerType("player"))
					local info = PowerBarColor and (PowerBarColor[powerToken or "MANA"] or PowerBarColor["MANA"])
					PlayerFrameManaBar:SetStatusBarColor(info and info.r or 0, info and info.g or 0, info and info.b or 1, 1.0)
				end
				if PlayerFrameManaBarText then
					PlayerFrameManaBarText:SetText(string.format("%d / %d", mana, manaMax))
				end
			end
			if MainMenuExpBar and UnitXP and UnitXPMax then
				local xp, xpMax = UnitXP("player") or 0, UnitXPMax("player") or 1
				MainMenuExpBar:SetMinMaxValues(0, xpMax)
				MainMenuExpBar:SetValue(xp)
				-- ExhaustionTick colours: rested blue, normal purple.
				if GetRestState and GetRestState() == 1 then
					MainMenuExpBar:SetStatusBarColor(0.0, 0.39, 0.88, 1.0)
				else
					MainMenuExpBar:SetStatusBarColor(0.58, 0.0, 0.55, 1.0)
				end
				if MainMenuExpBarOverlayFrameText then
					MainMenuExpBarOverlayFrameText:SetText(string.format("XP %d / %d", xp, xpMax))
				end
			end

			-- This helper normally only copies the equipped bag icon into the portrait
			-- texture. Some 3.3.5 FrameXML distributions provide it from glue code that
			-- is not present in the in-world TOC. The backpack artwork already contains
			-- its portrait, so a no-op fallback lets the stock layout generator finish.
			if not SetBagPortraitTexture then
				SetBagPortraitTexture = function() end
			end
			local function CloseEveryContainer()
				for i = 1, 13 do
					local frame = _G["ContainerFrame"..i]
					if frame and frame.Hide then
						frame:Hide()
					end
				end
			end
			local function FindContainerForBag(bag)
				for i = 1, 13 do
					local frame = _G["ContainerFrame"..i]
					if frame and frame.GetID and frame:GetID() == bag then
						return frame
					end
				end
			end
			local function FindFreeContainer(bag)
				local first = bag == 0 and 1 or 2
				local last = bag == 0 and 1 or 5
				for i = first, last do
					local frame = _G["ContainerFrame"..i]
					if frame and frame.IsShown and not frame:IsShown() then
						return frame
					end
				end
			end
			local function ShowContainer(bag)
				bag = bag or 0
				local slots = GetContainerNumSlots and GetContainerNumSlots(bag) or 0
				if not slots or slots <= 0 then
					return
				end
				local frame = FindContainerForBag(bag) or FindFreeContainer(bag)
				if frame and ContainerFrame_GenerateFrame then
					local ok = pcall(ContainerFrame_GenerateFrame, frame, slots, bag)
					if not ok and frame.Show then
						frame:Show()
					end
				elseif frame and frame.Show then
					frame:Show()
				end
			end
			ToggleBackpack = function()
				if ContainerFrame1 and ContainerFrame1.IsShown and ContainerFrame1:IsShown() then
					ContainerFrame1:Hide()
				else
					ShowContainer(0)
				end
			end
			OpenBackpack = function() ShowContainer(0) end
			CloseBackpack = function()
				if ContainerFrame1 and ContainerFrame1.Hide then ContainerFrame1:Hide() end
			end
			ToggleBag = function(bag)
				bag = bag or 0
				local frame = FindContainerForBag(bag)
				if frame and frame.IsShown and frame:IsShown() then
					frame:Hide()
				else
					ShowContainer(bag)
				end
			end
			OpenBag = ShowContainer
			CloseBag = function(bag)
				local frame = FindContainerForBag(bag or 0)
				if frame and frame.Hide then frame:Hide() end
			end
			OpenAllBags = function()
				for bag = 0, 4 do ShowContainer(bag) end
			end
			CloseAllBags = CloseEveryContainer
			ToggleAllBags = function()
				local anyShown = false
				for i = 1, 5 do
					local frame = _G["ContainerFrame"..i]
					if frame and frame.IsShown and frame:IsShown() then
						anyShown = true
						break
					end
				end
				if anyShown then
					CloseEveryContainer()
				else
					OpenAllBags()
				end
			end
			-- Container templates are authored hidden. Keep all of them closed until
			-- a bag binding explicitly opens a real container.
			CloseEveryContainer()
			-- Stock panels start closed. Keeping CharacterFrame hidden here also
			-- prevents opening the backpack from exposing the paper-doll template.
			if CharacterFrame and CharacterFrame.Hide then
				CharacterFrame:Hide()
			end

			-- Replace the bootstrap placeholder with the real FrameXML chat frames.
			if ChatFrame1 then
				DEFAULT_CHAT_FRAME = ChatFrame1
				SELECTED_CHAT_FRAME = ChatFrame1
                -- Scoped OnLoad recovery must include the numeric fade state;
                -- the stock fade functions otherwise abort UIParent updates.
                for i = 1, (NUM_CHAT_WINDOWS or 10) do
                    local frame = _G["ChatFrame"..i]
                    local tab = _G["ChatFrame"..i.."Tab"]
                    if frame and type(frame.oldAlpha) ~= "number" then
                        local _, _, _, _, _, alpha = GetChatWindowInfo(i)
                        frame.oldAlpha = tonumber(alpha) or DEFAULT_CHATFRAME_ALPHA or .25
                    end
                    if tab then
                        tab.mouseOverAlpha = tonumber(tab.mouseOverAlpha) or CHAT_FRAME_TAB_NORMAL_MOUSEOVER_ALPHA or .6
                        tab.noMouseAlpha = tonumber(tab.noMouseAlpha) or CHAT_FRAME_TAB_NORMAL_NOMOUSE_ALPHA or .2
                    end
                end
				CHAT_FRAMES = { "ChatFrame1", "ChatFrame2" }
			end
            -- Interface settings are hosted by the native options panel. Do not
            -- create a visible category over the unfinished stock Controls pane.
)LUA";

		lua_State* L = LuaVM->GetState();
		if (L && luaL_dostring(L, PostLoadFixup) != 0)
		{
			UE_LOG(LogWowUIManager, Warning, TEXT("Post-load fixup error: %hs"), lua_tostring(L, -1));
			lua_pop(L, 1);
		}
		// Mirror timers (breath/fatigue/fire) are presented natively from the
		// authoritative timer state. FrameXML's MirrorTimer_Show picking its own
		// slot as well showed a second, unlabeled bar at the water surface.
		static const char* MirrorTimerFixup = R"LUA(
            MirrorTimer_Show = function() end
            for i = 1, (MIRRORTIMER_NUMTIMERS or 3) do
                local timerFrame = _G["MirrorTimer"..i]
                if timerFrame then
                    timerFrame:SetScript("OnUpdate", nil)
                    timerFrame:SetScript("OnEvent", nil)
                end
            end
)LUA";
		if (L && luaL_dostring(L, MirrorTimerFixup) != 0)
		{
			UE_LOG(LogWowUIManager, Warning, TEXT("Mirror timer fixup error: %hs"), lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
#endif

	if (FrameManager)
	{
		FrameManager->SetActiveChatTab(1);
	}

	// 3. Fire post-addon startup events in WoW order.
	EventSystem->FireEvent(TEXT("VARIABLES_LOADED"));
	LuaVM->ExecuteString(TEXT("__WowLoggedIn = true"), TEXT("=WowLoggedIn")); // r360: IsLoggedIn() is true from PLAYER_LOGIN on (offline world too)
	EventSystem->FireEvent(TEXT("PLAYER_LOGIN"));
    // Install after startup addons and stock layout initialization.
    LuaVM->ExecuteString(WowBuffFrameScript(),TEXT("=PlayerBuffFrames"));

	EventSystem->FireEvent(TEXT("PLAYER_ENTERING_WORLD"));
	EventSystem->FireEvent(TEXT("UNIT_PORTRAIT_UPDATE"), {TEXT("player")});

	// Second visibility pass — catches addon frames (GMChatUI etc.) created after first SyncChildVisibility
	if (FrameManager)
	{
		FrameManager->SyncChildVisibility();
		// PLAYER_ENTERING_WORLD can briefly expose stock panels while their event
		// handlers initialize. Gameplay panels must remain closed until the player
		// explicitly opens them.
		static const TCHAR* StartupClosedFrames[] = {
			TEXT("CharacterFrame"), TEXT("FriendsFrame"), TEXT("QuestLogFrame"),
			TEXT("SpellBookFrame"), TEXT("TalentFrame"), TEXT("PlayerTalentFrame"), TEXT("GuildFrame"),
			TEXT("WorldMapFrame"), TEXT("GossipFrame"), TEXT("QuestFrame"),
			TEXT("MerchantFrame"), TEXT("LootFrame"), TEXT("GameMenuFrame")
		};
		for (const TCHAR* FrameName : StartupClosedFrames)
		{
			const int64 FrameHandle = FrameManager->FindFrame(FrameName);
			if (FrameHandle >= 0)
			{
				FrameManager->SetFrameVisible(FrameHandle, false);
			}
		}
	}

	bUILoaded = true;

	UE_LOG(LogWowUIManager, Log, TEXT("WoW UI loaded successfully (%d frames total)"),
		FrameManager ? FrameManager->GetFrameCount() : 0);

	if (FrameManager)
	{
		FrameManager->DebugDumpLayout();
	}
}

void UWowUIManager::SetRootCanvas(UCanvasPanel* Canvas)
{
	// First, initialize the frame manager with the canvas if provided
	if (FrameManager && Canvas)
	{
		FrameManager->Initialize(Canvas);
		FWowUiVrHost::SetFrameManager(FrameManager.Get());
		UE_LOG(LogWowUIManager, Log, TEXT("Root canvas set for FrameManager"));
	}

	// Create the UI overlay for Slate widgets
	if (FSlateApplication::IsInitialized())
	{
		UIOverlay = SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible); // Pass clicks through to 3D world

		BankWindow = SNew(SWowBankWindow, InventoryManager);
		UIOverlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0, 0, 220, 0)
		[
			BankWindow.ToSharedRef()
		];

		// Add the overlay to the viewport
		if (GEngine && GEngine->GameViewport)
		{
			FWowUiSurfaces::Add(UIOverlay.ToSharedRef(), EWowUiZone::Hud, 80, TEXT("UIOverlay")); // Z-order above main UI
		}

		UE_LOG(LogWowUIManager, Log, TEXT("Created native bank overlay; bags and character sheet use FrameXML exclusively"));
	}
}

void UWowUIManager::ToggleBagWindow()
{
	if (BagWindow.IsValid())
	{
		BagWindow->ToggleVisibility();
	}
}

void UWowUIManager::HideBagWindow()
{
	if (BagWindow.IsValid())
	{
		BagWindow->SetBagVisibility(EVisibility::Hidden);
	}
}

bool UWowUIManager::IsBagWindowVisible() const
{
	return BagWindow.IsValid() && BagWindow->IsBagVisible();
}

void UWowUIManager::ToggleCharacterPanel()
{
	if (CharacterPanel.IsValid())
	{
		CharacterPanel->ToggleVisibility();
	}
}

void UWowUIManager::HideCharacterPanel()
{
	if (CharacterPanel.IsValid())
	{
		CharacterPanel->SetPanelVisibility(EVisibility::Hidden);
	}
}

bool UWowUIManager::IsCharacterPanelVisible() const
{
	return CharacterPanel.IsValid() && CharacterPanel->IsPanelVisible();
}

void UWowUIManager::ShowBankWindow()
{
	UpdateInventory();

	if (BankWindow.IsValid())
	{
		BankWindow->Show();
	}
}

void UWowUIManager::HideBankWindow()
{
	if (BankWindow.IsValid())
	{
		BankWindow->Hide();
	}
}

bool UWowUIManager::IsBankWindowVisible() const
{
	return BankWindow.IsValid() && BankWindow->IsBankVisible();
}

void UWowUIManager::UpdateInventory()
{
	if (!InventoryManager.IsValid() || !ConnectionManager)
		return;

	FWowPlayerEntity* LocalPlayer = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(LogWowUIManager, Verbose, TEXT("UpdateInventory skipped: local player entity not ready"));
		return;
	}

	InventoryManager->UpdateFromPlayerEntity(*LocalPlayer, ConnectionManager->PacketHandler.EntityManager);
	if (FrameManager)
	{
		FrameManager->UpdateCharacterSheet(*LocalPlayer, ConnectionManager->GetCharacterName());
		FrameManager->UpdateBackpackMoney(LocalPlayer->GetCoinage());
	}
}

void UWowUIManager::BeginLocalSpellProgress(uint32 SpellId, const FString& SpellName,
	float DurationSeconds, bool bChanneling)
{
	const uint64 LocalGuid = ConnectionManager
		? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid : 0;
	BeginUnitSpellProgress(LocalGuid, SpellId, SpellName, DurationSeconds, bChanneling);
}

void UWowUIManager::BeginUnitSpellProgress(uint64 UnitGuid, uint32 SpellId,
	const FString& SpellName, float DurationSeconds, bool bChanneling)
{
	if (!UIContext || UnitGuid == 0)
	{
		return;
	}

	FWowUnitSpellProgress& Progress = UIContext->UnitSpellProgress.FindOrAdd(UnitGuid);
	Progress.bChanneling = bChanneling;
	Progress.SpellName = SpellName;
	Progress.SpellIcon = TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
	if (const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(SpellId))
	{
		if (const FSpellIconDbcEntry* Icon = FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID);
			Icon && !Icon->TexturePath.IsEmpty())
		{
			Progress.SpellIcon = Icon->TexturePath;
		}
	}
	Progress.StartMs = FPlatformTime::Seconds() * 1000.0;
	Progress.EndMs = Progress.StartMs
		+ FMath::Max(DurationSeconds, 0.001f) * 1000.0;
	Progress.CastId = SpellId;
}

void UWowUIManager::UpdateLocalSpellProgress(float RemainingSeconds)
{
	const uint64 LocalGuid = ConnectionManager
		? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid : 0;
	UpdateUnitSpellProgress(LocalGuid, RemainingSeconds);
}

void UWowUIManager::UpdateUnitSpellProgress(uint64 UnitGuid, float RemainingSeconds)
{
	if (UIContext)
	{
		if (FWowUnitSpellProgress* Progress = UIContext->UnitSpellProgress.Find(UnitGuid))
		{
			Progress->EndMs = FPlatformTime::Seconds() * 1000.0
				+ FMath::Max(RemainingSeconds, 0.0f) * 1000.0;
		}
	}
}

void UWowUIManager::DelayUnitSpellProgress(uint64 UnitGuid, float DelaySeconds)
{
	if (UIContext)
	{
		if (FWowUnitSpellProgress* Progress = UIContext->UnitSpellProgress.Find(UnitGuid))
		{
			Progress->EndMs += FMath::Max(DelaySeconds, 0.0f) * 1000.0;
		}
	}
}

void UWowUIManager::EndLocalSpellProgress()
{
	const uint64 LocalGuid = ConnectionManager
		? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid : 0;
	EndUnitSpellProgress(LocalGuid);
}

void UWowUIManager::EndUnitSpellProgress(uint64 UnitGuid)
{
	if (UIContext)
	{
		UIContext->UnitSpellProgress.Remove(UnitGuid);
	}
}

bool UWowUIManager::GetUnitSpellProgress(uint64 UnitGuid, FString& OutSpellName, FString& OutIconPath,
	double& OutStartMs, double& OutEndMs, bool& bOutChanneling) const
{
	const FWowUnitSpellProgress* Progress = UIContext ? UIContext->UnitSpellProgress.Find(UnitGuid) : nullptr;
	if (!Progress || Progress->EndMs <= FPlatformTime::Seconds() * 1000.0) return false;
	OutSpellName = Progress->SpellName;
	OutIconPath = Progress->SpellIcon;
	OutStartMs = Progress->StartMs;
	OutEndMs = Progress->EndMs;
	bOutChanneling = Progress->bChanneling;
	return true;
}

uint64 UWowUIManager::GetFocusGuid() const
{
	return UIContext ? UIContext->FocusGuid : 0;
}

void UWowUIManager::SetMouseoverGuid(uint64 Guid)
{
	if (!UIContext || UIContext->MouseoverGuid == Guid) return;
	UIContext->MouseoverGuid = Guid;
	if (Guid && EventSystem) EventSystem->FireEvent(TEXT("UPDATE_MOUSEOVER_UNIT"));
}

void UWowUIManager::SetConnectionManager(UWowConnectionManager* InConnectionManager)
{
	ConnectionManager = InConnectionManager;
    if (FrameManager) FrameManager->SetItemConnection(InConnectionManager);

	// Update the Lua context with connection manager and entity manager
	if (UIContext && ConnectionManager)
	{
		UIContext->ConnectionManager = ConnectionManager;
		UIContext->EntityManager = &ConnectionManager->PacketHandler.EntityManager;

		UE_LOG(LogWowUIManager, Log, TEXT("Updated Lua context with ConnectionManager and EntityManager"));
	}

	if (InventoryManager.IsValid())
	{
		InventoryManager->SetConnectionManager(ConnectionManager);
	}

	if (CharacterPanel.IsValid())
	{
		CharacterPanel->SetConnectionManager(ConnectionManager);
	}
}

void UWowUIManager::SetAudioManager(AWowAudioManager* InAudioManager)
{
	AudioManager = InAudioManager;
	if (UIContext)
	{
		UIContext->AudioManager = AudioManager;
	}
}

void UWowUIManager::RefreshUnitFrames()
{
    if (bUILoaded && LuaVM)
    {
        LuaVM->ExecuteString(TEXT("if WowRefreshUnitFrames then WowRefreshUnitFrames() end"), TEXT("=UnitFrameRefresh"));
        // MultiCastActionBarFrame (totem bar) exists only with active totem slots, as
        // MultiCastActionBarFrame_Update decides. Addons that re-parent it (Bartender4's
        // totem bar) call :Show() on it; keep the client rule so an empty bar with its raw
        // UI-TotemBar atlas is never drawn.
        LuaVM->ExecuteString(TEXT("local f=MultiCastActionBarFrame if f and f:IsShown() and HasMultiCastActionBar and not HasMultiCastActionBar() then f:Hide() end"),
            TEXT("=MultiCastBarRule"));
    }
}

void UWowUIManager::SetBindingHandler(TFunction<bool(const FString&,bool)> Handler)
{if(UIContext)UIContext->RunBinding=MoveTemp(Handler);}
bool UWowUIManager::RunOriginalBinding(const FString& Command,bool Pressed)
{
    const FString* Script=BindingScripts.Find(Command);
    if(!Script || !LuaVM)return false;
    if(!Pressed && !BindingRunsOnRelease.Contains(Command))return true;
    return LuaVM->ExecuteString(FString::Printf(TEXT("local keystate='%s'\n"),Pressed?TEXT("down"):TEXT("up"))+*Script,TEXT("=binding_")+Command);
}
