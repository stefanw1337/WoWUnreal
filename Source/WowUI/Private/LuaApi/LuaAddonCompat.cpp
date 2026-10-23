// Functions and globals that common 3.3.5 addons (Ace3 libraries, Bartender4, DBM,
// MoveAnything, LibKeyBound, LibDBIcon ...) expect from the client but that were not
// provided yet. Every function is registered only when nothing else defines it.
#include "LuaApiRegistry.h"
#include "WowFrameManager.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowAudioManager.h"
#include "WowWorldManager.h"
#include "WowAddonSettings.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#define HAS_LUA 1
#else
#define HAS_LUA 0
#endif

namespace WowLuaApi { void RegisterTalentApi(lua_State* L); } // LuaTalentApi.cpp

#if HAS_LUA
namespace
{
AWowWorldManager* WorldManager(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	UWorld* World = Ctx && Ctx->AudioManager ? Ctx->AudioManager->GetWorld()
		: (GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr);
	return World ? Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(World, AWowWorldManager::StaticClass())) : nullptr;
}

// GetChannelName(id | name) -> id, name, instanceId (0 when not joined).
int L_GetChannelName(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	UWowConnectionManager* C = Ctx ? Ctx->ConnectionManager : nullptr;
	if (C && lua_type(L, 1) == LUA_TNUMBER)
	{
		const int32 Id = int32(lua_tointeger(L, 1));
		const FString Name = C->PacketHandler.GetChannelNameByNumber(Id);
		if (!Name.IsEmpty())
		{
			lua_pushinteger(L, Id);
			lua_pushstring(L, TCHAR_TO_UTF8(*Name));
			lua_pushinteger(L, 0);
			return 3;
		}
	}
	else if (C && lua_isstring(L, 1))
	{
		const FString Wanted = UTF8_TO_TCHAR(lua_tostring(L, 1));
		for (int32 Id = 1; Id <= 10; ++Id)
		{
			const FString Name = C->PacketHandler.GetChannelNameByNumber(Id);
			if (!Name.IsEmpty() && (Name.Equals(Wanted, ESearchCase::IgnoreCase) || Name.StartsWith(Wanted + TEXT(" - "), ESearchCase::IgnoreCase)))
			{
				lua_pushinteger(L, Id);
				lua_pushstring(L, TCHAR_TO_UTF8(*Name));
				lua_pushinteger(L, 0);
				return 3;
			}
		}
	}
	lua_pushinteger(L, 0);
	lua_pushnil(L);
	lua_pushinteger(L, 0);
	return 3;
}

// GetCurrentMapAreaID(): WorldMapArea.dbc id of the zone the player is in (DBM uses it
// to pick boss modules). WorldMapArea: ID, MapID, AreaID, ...
int L_GetCurrentMapAreaID(lua_State* L)
{
	static TMap<uint32, uint32> AreaToMapArea;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		if (FMpqManager* Mpq = FWowAddonSettings::GetMpq())
		{
			bLoaded = true;
			TArray<uint8> Bytes;
			FDbcParser P;
			if (Mpq->ReadFile(TEXT("DBFilesClient\\WorldMapArea.dbc"), Bytes) && P.Parse(Bytes) && P.GetFieldCount() >= 3)
				for (uint32 I = 0; I < P.GetRecordCount(); ++I)
					if (const uint32 Area = P.GetUInt(I, 2)) AreaToMapArea.FindOrAdd(Area, P.GetUInt(I, 0));
		}
	}
	const AWowWorldManager* World = WorldManager(L);
	const uint32* Id = World ? AreaToMapArea.Find(World->GetCurrentZoneId()) : nullptr;
	lua_pushinteger(L, Id ? int32(*Id) : 0);
	return 1;
}

void RegisterIfMissing(lua_State* L, const char* Name, lua_CFunction Function)
{
	lua_getglobal(L, Name);
	const bool bMissing = lua_isnil(L, -1);
	lua_pop(L, 1);
	if (bMissing) lua_register(L, Name, Function);
}

/** __WowFrameRegionNames(frame) -> the names of the frame's XML layer regions, layer by layer
    (textures, then font strings), for GetRegions (r230). */
int L_FrameRegionNames(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !lua_istable(L, 1)) return 0;
	lua_getfield(L, 1, "__handle");
	const int64 Handle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	const FWowFrameDef* Def = Handle >= 0 ? Ctx->FrameManager->GetFrameDef(Handle) : nullptr;
	if (!Def) return 0;
	int Count = 0;
	for (const FWowLayer& Layer : Def->Layers)
	{
		for (const FWowTextureElement& Texture : Layer.Textures)
			if (!Texture.Name.IsEmpty() && !Texture.Name.Contains(TEXT("$")) && lua_checkstack(L, 1)) { lua_pushstring(L, TCHAR_TO_UTF8(*Texture.Name)); ++Count; }
		for (const FWowFontStringElement& Text : Layer.FontStrings)
			if (!Text.Name.IsEmpty() && !Text.Name.Contains(TEXT("$")) && lua_checkstack(L, 1)) { lua_pushstring(L, TCHAR_TO_UTF8(*Text.Name)); ++Count; }
	}
	return Count;
}

// r242: log-only print for diagnostics (print() also writes to the chat frame).
int L_WowLog(lua_State* L)
{
	if (lua_isstring(L, 1)) UE_LOG(LogWowLuaFrame, Log, TEXT("[diag] %s"), UTF8_TO_TCHAR(lua_tostring(L, 1)));
	return 0;
}

const char* CompatLua = R"LUA(
local function missing(name) return _G[name] == nil end

-- r245 diagnostics: __WowDumpTree(frame) logs a frame, its regions and children with their
-- geometry; /wowdiag [name] dumps a named frame (or the frame under the mouse), and an AceConfig
-- dialog (Bartender4 /bt4 ...) is dumped once automatically the first time it opens.
function __WowDumpTree(root, maxDepth)
    if not __WowLog or type(root) ~= "table" then return end
    local lines = 0
    local function s(v) if type(v) == "number" then return string.format("%.1f", v) end return tostring(v) end
    local function geo(o)
        local ok1, l = pcall(function() return o.GetLeft and o:GetLeft() end)
        local ok2, b = pcall(function() return o.GetBottom and o:GetBottom() end)
        local ok3, w = pcall(function() return o.GetWidth and o:GetWidth() end)
        local ok4, h = pcall(function() return o.GetHeight and o:GetHeight() end)
        return "L=" .. s(ok1 and l) .. " B=" .. s(ok2 and b) .. " W=" .. s(ok3 and w) .. " H=" .. s(ok4 and h)
    end
    local function points(o)
        local out = {}
        local okN, n = pcall(function() return o.GetNumPoints and o:GetNumPoints() or 0 end)
        for i = 1, (okN and tonumber(n) or 0) do
            local ok, p, r, rp, x, y = pcall(o.GetPoint, o, i)
            if ok and p then
                local rn = type(r) == "table" and ((r.GetName and r:GetName()) or "?") or tostring(r)
                out[#out + 1] = p .. ">" .. tostring(rn) .. "." .. tostring(rp) .. "(" .. s(x) .. "," .. s(y) .. ")"
            end
        end
        return table.concat(out, " ")
    end
    local function walk(f, depth, indent)
        if lines > 2500 or depth > (maxDepth or 6) then return end -- r281b: 2500 (was 600)
        lines = lines + 1
        local okT, t = pcall(function() return f.GetObjectType and f:GetObjectType() end)
        local okS, shown = pcall(function() return f.IsShown and f:IsShown() end)
        local okV, vis = pcall(function() return f.IsVisible and f:IsVisible() end)
        local okN, nm = pcall(function() return f.GetName and f:GetName() end)
        -- r357: a button's normal texture, a slider's range and value.
        local extra = ""
        if okT and (t == "Button" or t == "CheckButton") then
            local okX, tex = pcall(function() local n = f:GetNormalTexture() return n and n:GetTexture() end)
            extra = " normal=" .. tostring(okX and tex)
        elseif okT and t == "Slider" then
            local okX, lo, hi = pcall(function() return f:GetMinMaxValues() end)
            local okY, v = pcall(function() return f:GetValue() end)
            extra = " range=" .. tostring(okX and lo) .. ".." .. tostring(okX and hi) .. " value=" .. tostring(okY and v)
        end
        __WowLog(indent .. tostring(okT and t) .. " " .. tostring(okN and nm) .. " shown=" .. tostring(okS and shown)
            .. " vis=" .. tostring(okV and vis) .. " " .. geo(f) .. " " .. points(f) .. extra)
        local okR, regions = pcall(function() return { f:GetRegions() } end)
        if okR then
            for _, r in ipairs(regions) do
                lines = lines + 1
                local okRT, rt = pcall(function() return r.GetObjectType and r:GetObjectType() end)
                local okTx, tx = pcall(function() return (r.GetText and r:GetText()) or (r.GetTexture and r:GetTexture()) end)
                local okRS, rs = pcall(function() return r.IsShown and r:IsShown() end)
                __WowLog(indent .. "  - " .. tostring(okRT and rt) .. " '" .. tostring(okTx and tx) .. "' shown=" .. tostring(okRS and rs) .. " " .. geo(r) .. " " .. points(r))
            end
        end
        local okC, children = pcall(function() return { f:GetChildren() } end)
        if okC then for _, c in ipairs(children) do walk(c, depth + 1, indent .. "  ") end end
    end
    walk(root, 0, "")
end
SLASH_WOWDIAG1 = "/wowdiag"
SlashCmdList = SlashCmdList or {}
SlashCmdList["WOWDIAG"] = function(msg)
    if msg == "auto" then -- r361: toggle the automatic AceConfig / pullout dumps
        __WowDiagAuto = not __WowDiagAuto
        if __WowLog then __WowLog("automatic dumps " .. (__WowDiagAuto and "on" or "off")) end
        return
    end
    local f = (msg and msg ~= "" and _G[msg]) or (GetMouseFocus and GetMouseFocus())
    if f then __WowDumpTree(f, 8) if __WowLog then __WowLog("dump done: " .. tostring(msg)) end end
end
-- r279: these diagnostics run from __WowDiagTick, which the UI tick calls through
-- __WowSanitizeUIPanels. The OnUpdate frames they used before are created here, before the UI
-- has its canvas, and never ticked (Stefan 13.10: /bt4 logged no frame tree).
do
    local dumped = {}
    local bt4Seen, bt4Done = nil, false
    function __WowDiagTick()
        if __WowAnimationTick then pcall(__WowAnimationTick) end -- r426
        -- r307 (Stefan 13.10: the XP bar vanished again; log "XP bar not restored: level 16 / 0" and
        -- three MainMenuExpBar:Hide() calls on entering the world): MAX_PLAYER_LEVEL stayed 0 because
        -- ReputationFrame_OnLoad, which sets it in the client, never ran here, so
        -- ReputationWatchBar_Update hid the XP bar (level < 0 is false). Set it like OnLoad does.
        if (tonumber(MAX_PLAYER_LEVEL) or 0) <= 0 and MAX_PLAYER_LEVEL_TABLE then
            local level = MAX_PLAYER_LEVEL_TABLE[GetAccountExpansionLevel and GetAccountExpansionLevel() or 2]
            if (tonumber(level) or 0) > 0 then
                MAX_PLAYER_LEVEL = level
                if __WowLog then __WowLog("MAX_PLAYER_LEVEL was 0; set to " .. level) end
                if ReputationWatchBar_Update and UnitLevel and (UnitLevel("player") or 0) > 0 then
                    pcall(ReputationWatchBar_Update, UnitLevel("player"))
                end
            end
        end
        -- r318 (Stefan 14.10: empty side action bars blinked red dots): MultiActionBars.xml gives
        -- each bar its page as an XML attribute (actionpage), which this client does not read, so
        -- MultiBarRight/Left/BottomLeft/BottomRight buttons computed page 1 and range-checked the
        -- main bar's spells (red RANGE_INDICATOR on empty buttons). Set the pages like the XML.
        if MultiBarRight and ActionButton_UpdateAction and MultiBarRight.GetAttribute and MultiBarRight:GetAttribute("actionpage") == nil then
            for bar, page in pairs({ MultiBarBottomLeft = 6, MultiBarBottomRight = 5, MultiBarRight = 3, MultiBarLeft = 4 }) do
                local frame = _G[bar]
                if frame and frame.GetAttribute and frame:GetAttribute("actionpage") == nil then
                    frame:SetAttribute("actionpage", page)
                    for i = 1, 12 do
                        local button = _G[bar .. "Button" .. i]
                        if button then pcall(ActionButton_UpdateAction, button) end
                    end
                end
            end
        end
        if __WowStateDriverPump then pcall(__WowStateDriverPump) end -- r409
        -- r422 (Stefan 18.10: the minimap's world map button opened the empty stock WorldMapFrame, not
        -- the map M opens): its OnClick is ToggleFrame(WorldMapFrame); it goes through ToggleWorldMap
        -- (the TOGGLEWORLDMAP binding), like the key.
        local mapButton = MiniMapWorldMapButton
        if mapButton and not mapButton.__wowMapRouted and mapButton.SetScript and ToggleWorldMap then
            mapButton.__wowMapRouted = true
            mapButton:SetScript("OnClick", function() ToggleWorldMap() end)
        end
        if not __WowLog then return end
        -- r361 (Stefan 15.10: /bt4 still froze): these automatic frame-tree dumps were most of
        -- the freeze (about 3 of the 3.4 s), so they now run only after "/wowdiag auto".
        local dialog = __WowDiagAuto and LibStub and LibStub("AceConfigDialog-3.0", true)
        if dialog and dialog.OpenFrames then
            for app, widget in pairs(dialog.OpenFrames) do
                if not dumped[app] and type(widget) == "table" and widget.frame then
                    dumped[app] = true
                    __WowLog("AceConfig dialog '" .. tostring(app) .. "' opened - frame tree:")
                    __WowDumpTree(widget.frame, 16) -- r281b: the option widgets sit 10+ levels deep
                end
            end
            for app in pairs(dumped) do
                if not dialog.OpenFrames[app] then dumped[app] = nil end -- dump again when reopened
            end
            -- r357 (Stefan 15.10, /bt4 Bar 3: sliders and texts still wrong): the page again, once,
            -- each time another entry of the tree is chosen (up to 8 pages a session).
            for app, widget in pairs(dialog.OpenFrames) do
                local st = dialog.Status and dialog.Status[app] and dialog.Status[app].status
                local sel = st and st.groups and st.groups.selected
                __WowDiagPages = __WowDiagPages or {}
                if sel and type(widget) == "table" and widget.frame and __WowDiagPages[app] ~= sel then
                    local first = __WowDiagPages[app] == nil
                    __WowDiagPages[app] = sel
                    __WowDiagPageCount = (__WowDiagPageCount or 0) + 1
                    if not first and __WowDiagPageCount <= 8 then
                        __WowLog("AceConfig dialog '" .. tostring(app) .. "' page '" .. tostring(sel) .. "' - frame tree:")
                        __WowDumpTree(widget.frame, 16)
                    end
                end
            end
        end
        -- r304 (Stefan 13.10: something hides the XP bar again a few seconds after Bartender's XP Bar
        -- module shows it): who calls MainMenuExpBar:Hide(), with the Lua call stack (first 20 times).
        if MainMenuExpBar and not __WowExpBarHideHooked then
            __WowExpBarHideHooked = true
            local hideCount = 0
            local originalHide = MainMenuExpBar.Hide
            MainMenuExpBar.Hide = function(self, ...)
                if hideCount < 20 and __WowLog then
                    hideCount = hideCount + 1
                    local stack = (debugstack and debugstack(2, 6, 0)) or (debug and debug.traceback and debug.traceback("", 2)) or "?"
                    local flat = tostring(stack):gsub("[\r\n\t]+", " < ")
                    __WowLog("MainMenuExpBar:Hide() called (" .. hideCount .. "): " .. flat)
                end
                return originalHide(self, ...)
            end
        end
        -- r302 (Stefan 13.10: /bt4 drop-downs open - the click sound plays - but show no list):
        -- the first time an AceGUI pullout is shown, its frame tree goes to the log.
        for i = 1, (__WowDiagAuto and 20 or 0) do
            local pullout = _G["AceGUI30Pullout" .. i]
            if not pullout then break end
            if pullout:IsShown() and not pullout.__wowDumped then
                pullout.__wowDumped = true
                __WowLog("AceGUI pullout " .. i .. " shown - frame tree:")
                __WowDumpTree(pullout, 6)
            end
        end
        -- r299 (Stefan 13.10: bars 3 and 4 enabled but nowhere): every Bartender bar once a minute -
        -- shown, place, scale/alpha, and how many of its buttons are shown / have an action.
        if _G.BT4Bar1 then
            local now = GetTime and GetTime() or 0
            if not __WowBT4BarsNext or now >= __WowBT4BarsNext then
                __WowBT4BarsNext = now + 60
                for i = 1, 10 do
                    local bar = _G["BT4Bar" .. i]
                    if bar then
                        local shownButtons, withAction, first = 0, 0, nil
                        for b = (i - 1) * 12 + 1, i * 12 do
                            local button = _G["BT4Button" .. b]
                            if button then
                                first = first or button
                                if button:IsShown() then shownButtons = shownButtons + 1 end
                                local action = button.action or (button.GetAttribute and button:GetAttribute("action"))
                                if action and HasAction and HasAction(action) then withAction = withAction + 1 end
                            end
                        end
                        local ok, p, r, rp, x, y = pcall(bar.GetPoint, bar, 1)
                        local rn = (ok and type(r) == "table" and r.GetName and r:GetName()) or tostring(r)
                        __WowLog(string.format("BT4Bar%d shown=%s visible=%s left=%s bottom=%s point=%s %s %s %s %s scale=%s alpha=%s buttons shown=%d withAction=%d firstAction=%s",
                            i, tostring(bar:IsShown()), tostring(bar:IsVisible()), tostring(bar.GetLeft and bar:GetLeft()), tostring(bar.GetBottom and bar:GetBottom()),
                            tostring(p), tostring(rn), tostring(rp), tostring(x), tostring(y), tostring(bar.GetScale and bar:GetScale()),
                            tostring(bar.GetAlpha and bar:GetAlpha()), shownButtons, withAction, tostring(first and first.action)))
                    end
                end
            end
        end
        -- r242: where Bartender4's first bars and buttons ended up, 5 s after they exist.
        if not bt4Done and _G.BT4Bar1 then
            local now = GetTime and GetTime() or 0
            bt4Seen = bt4Seen or now
            if now - bt4Seen >= 5 then
                bt4Done = true
                for _, n in ipairs({"BT4Bar1", "BT4Bar1Overlay", "BT4Button1", "BT4Button2", "BT4Button12", "BT4Bar2", "BT4Button13"}) do
                    local f = _G[n]
                    if f then
                        local ok, p, r, rp, x, y = pcall(f.GetPoint, f, 1)
                        local rn = (ok and type(r) == "table" and r.GetName and r:GetName()) or tostring(r)
                        __WowLog(string.format("%s shown=%s visible=%s left=%s bottom=%s w=%s h=%s point=%s %s %s %s %s alpha=%s scale=%s action=%s",
                            n, tostring(f:IsShown()), tostring(f:IsVisible()), tostring(f.GetLeft and f:GetLeft()), tostring(f.GetBottom and f:GetBottom()),
                            tostring(f:GetWidth()), tostring(f:GetHeight()), tostring(p), tostring(rn), tostring(rp), tostring(x), tostring(y),
                            tostring(f.GetAlpha and f:GetAlpha()), tostring(f.GetScale and f:GetScale()), tostring(f.action)))
                    end
                end
            end
        end
    end
end
)LUA"
    R"LUA(
-- r409 (Stefan 17.10, warrior with Bartender4: "I can't press 1 to charge"): Battle Stance gives
-- bonus bar offset 1, so Bartender's bar 1 pages to 7 through its state driver
-- "[bonusbar:1]7;...;0" and its secure "_onstate-page" snippet. The log kept every BT4 button on
-- its page-0 action (firstAction=1, 0 actions shown) the whole session. Two client parts can stop
-- that: SecureStateDriverManager's OnUpdate (it re-evaluates the drivers every 0.2 s) not being
-- ticked - the driver was registered at login, before the stance was known - and the restricted
-- control:ChildUpdate not reaching the buttons. Tick the manager here when its own OnUpdate has
-- not run for 0.5 s, and give Bartender buttons the action of their bar's page when the child
-- update did not (what the snippet sets: state, type and action).
do
    local origUpdate, lastTick, prevTime, pumpAt = nil, nil, nil, 0
    local lastPage, loggedFix, loggedPump = {}, {}, false
    function __WowStateDriverPump()
        local mgr = SecureStateDriverManager
        if not mgr or not mgr.GetScript or not GetTime then return end
        local now = GetTime()
        if not origUpdate then
            local ok, f = pcall(mgr.GetScript, mgr, "OnUpdate")
            if not ok or type(f) ~= "function" then return end
            origUpdate = f
            mgr:SetScript("OnUpdate", function(self, elapsed)
                lastTick = GetTime()
                return origUpdate(self, elapsed)
            end)
        end
        if now < pumpAt then return end
        local elapsed = prevTime and (now - prevTime) or 0.1
        prevTime, pumpAt = now, now + 0.1
        if not lastTick or now - lastTick > 0.5 then
            if not loggedPump and __WowLog then
                loggedPump = true
                __WowLog("SecureStateDriverManager OnUpdate is not ticking; state drivers are evaluated from the UI tick")
            end
            pcall(origUpdate, mgr, elapsed)
        end
        for i = 1, 10 do
            local bar = _G["BT4Bar" .. i]
            local page = bar and bar.buttons and bar.GetAttribute and bar:GetAttribute("state-page")
            if page ~= nil then
                page = tostring(page)
                if page ~= lastPage[i] then
                    lastPage[i] = page
                    if __WowLog then
                        __WowLog(string.format("[diag] BT4Bar%d page state %s (bonus offset %s, form %s)", i, page,
                            tostring(GetBonusBarOffset and GetBonusBarOffset()), tostring(GetShapeshiftForm and GetShapeshiftForm())))
                    end
                end
                local fixed = 0
                for _, button in pairs(bar.buttons) do
                    if type(button) == "table" and button.GetAttribute and tostring(button:GetAttribute("state")) ~= page then
                        local action = tonumber(button:GetAttribute("action-" .. page))
                        if action then
                            button:SetAttribute("state", page)
                            if action == 132 or (action > 120 and action <= 126) then
                                button:SetAttribute("type", "click")
                            else
                                button:SetAttribute("type", "action")
                            end
                            button:SetAttribute("action", action)
                            fixed = fixed + 1
                        end
                    end
                end
                if fixed > 0 and __WowLog and loggedFix[i] ~= page then
                    loggedFix[i] = page
                    __WowLog(string.format("[diag] BT4Bar%d: %d buttons had not followed page %s; set here", i, fixed, page))
                end
            end
        end
    end
end

if missing("TEXT") then function TEXT(text) return text end end

-- Frame methods some addons call that the client provides (only added when missing).
local methods = __WowFrameMethods
__WowFrameMethods = nil
if type(methods) == "table" then
    local function add(name, fn) if methods[name] == nil then methods[name] = fn end end
    local function track(setter, field, getter)
        local original = methods[setter]
        if type(original) == "function" then
            methods[setter] = function(self, value, ...)
                self[field] = value and value ~= 0 and true or false
                return original(self, value, ...)
            end
        end
        add(getter, function(self) return self[field] and 1 or nil end)
    end
    track("EnableMouse", "__mouseEnabled", "IsMouseEnabled")
    track("EnableMouseWheel", "__mouseWheelEnabled", "IsMouseWheelEnabled")
    track("EnableKeyboard", "__keyboardEnabled", "IsKeyboardEnabled")
    track("SetMovable", "__movable", "IsMovable")
    track("SetResizable", "__resizable", "IsResizable")
    track("SetToplevel", "__toplevel", "IsToplevel")
    add("DisableDrawLayer", function() end)
    add("EnableDrawLayer", function() end)
    add("StartSizing", function() end)
    add("SetNumeric", function(self, numeric) self.__numeric = numeric end)
    add("SetThumbTexture", function(self, texture) self.__thumbTexture = texture end)
    add("GetDepth", function() return 0 end)
    add("GetEffectiveAlpha", function(self)
        local alpha, frame, guard = 1, self, 0
        while frame and frame.GetAlpha and guard < 50 do
            alpha = alpha * (frame:GetAlpha() or 1)
            frame = frame.GetParent and frame:GetParent()
            guard = guard + 1
        end
        return alpha
    end)
    add("GetNumLetters", function(self)
        local text = self.GetText and self:GetText() or ""
        return (#(text:gsub("[\128-\191]", "")))
    end)
    add("Insert", function(self, text)
        if self.SetText then self:SetText((self:GetText() or "") .. (text or "")) end
    end)
    -- r230 (Stefan 12.10, ElvUI on TormentWoW): GetRegions/GetNumRegions return the frame's XML
    -- layer regions (by name, layer order) and the regions created from Lua, in creation order.
    -- ElvUI strips and skins frames through them; an empty list aborted whole modules.
    for _, creator in ipairs({ "CreateTexture", "CreateFontString" }) do
)LUA"
    R"LUA(        local original = methods[creator]
        if type(original) == "function" then
            methods[creator] = function(self, ...)
                local region = original(self, ...)
                if type(region) == "table" and type(self) == "table" then
                    local own = rawget(self, "__luaRegions")
                    if not own then own = {}; rawset(self, "__luaRegions", own) end
                    own[#own + 1] = region
                end
                return region
            end
        end
    end
    local function regionsOf(self)
        local list = {}
        if type(self) ~= "table" then return list end
        if __WowFrameRegionNames then
            for _, name in ipairs({ __WowFrameRegionNames(self) }) do
                local region = _G[name]
                if type(region) == "table" then list[#list + 1] = region end
            end
        end
        local own = rawget(self, "__luaRegions")
        if own then for i = 1, #own do list[#list + 1] = own[i] end end
        return list
    end
    add("GetRegions", function(self) return unpack(regionsOf(self)) end)
    add("GetNumRegions", function(self) return #regionsOf(self) end)
    -- Secure-template checks: nothing is protected outside combat here.
    add("CanChangeAttribute", function() return not (InCombatLockdown and InCombatLockdown()) end)
    -- Button:SetFontString(fs) makes fs the button's text (GetFontString returns it).
    do
        local getFontString = methods.GetFontString
        add("SetFontString", function(self, fontString) rawset(self, "__fontString", fontString) end)
        if type(getFontString) == "function" then
            methods.GetFontString = function(self, ...)
                local own = type(self) == "table" and rawget(self, "__fontString")
                if own then return own end
                return getFontString(self, ...)
            end
        end
    end
    -- StatusBar orientation / texture rotation are remembered (the bar itself fills left to right).
    do
        local setOrientation = methods.SetOrientation
        methods.SetOrientation = function(self, orientation, ...)
            rawset(self, "__orientation", orientation)
            if type(setOrientation) == "function" then return setOrientation(self, orientation, ...) end
        end
        add("GetOrientation", function(self) return rawget(self, "__orientation") or "HORIZONTAL" end)
        add("SetRotatesTexture", function(self, rotates) rawset(self, "__rotatesTexture", rotates and true or false) end)
        add("GetRotatesTexture", function(self) return rawget(self, "__rotatesTexture") and 1 or nil end)
    end
    -- StatusBar:GetStatusBarTexture(): the bar's fill. A hidden stand-in region whose SetTexture /
    -- SetVertexColor go to the bar, so skins that recolour or retexture the fill work through it.
    add("GetStatusBarTexture", function(self)
        if type(self) ~= "table" then return nil end
        local fill = rawget(self, "__statusBarFill")
        if not fill and self.CreateTexture then
            fill = self:CreateTexture(nil, "ARTWORK")
            if fill then
                local own = rawget(self, "__luaRegions")
                if own and own[#own] == fill then own[#own] = nil end -- not one of the frame's own regions
                if fill.Hide then fill:Hide() end
                local bar = self
                fill.SetTexture = function(_, texture)
                    if texture and bar.SetStatusBarTexture then bar:SetStatusBarTexture(texture) end
                end
                fill.SetVertexColor = function(_, r, g, b, a)
                    if bar.SetStatusBarColor then bar:SetStatusBarColor(r, g, b, a) end
                end
                fill.Show = function() end
                rawset(self, "__statusBarFill", fill)
            end
        end
        return fill
    end)
    add("SetFrameRef", function(self, label, frame)
        if self.SetAttribute then self:SetAttribute("frameref-" .. tostring(label), frame) end
    end)
end
if missing("GetClickFrame") then function GetClickFrame(name) return _G[name] end end

if missing("GetQuestGreenRange") then
    function GetQuestGreenRange()
        local level = UnitLevel and UnitLevel("player") or 1
        if level <= 5 then return 5
        elseif level <= 39 then return math.floor(level / 10) + 5
        elseif level <= 59 then return math.floor(level / 5) + 1
        else return 9 end
    end
end

-- Chat window settings are kept natively; FloatingChatFrame only needs these to exist.
for _, name in ipairs({ "SetChatWindowLocked", "SetChatWindowUninteractable", "SetChatWindowShown",
        "SetChatWindowName", "SetChatWindowColor", "SetChatWindowAlpha", "SetChatWindowSize",
        "SetChatWindowSavedPosition", "SetChatWindowSavedDimensions" }) do
    if missing(name) then _G[name] = function() end end
end

-- Override bindings (Bartender4, LibKeyBound, Clique ...). Stored per owner so addons can
-- query/clear them; r291: a key press runs its override first (see __WowRunOverrideBinding).
if missing("SetOverrideBinding") then
    local overrides = {}
    __WowUnrealOverrideBindings = overrides
    local function set(owner, key, value)
        if not owner or not key then return end
        overrides[owner] = overrides[owner] or {}
        overrides[owner][key] = value
    end
    function SetOverrideBinding(owner, isPriority, key, command) set(owner, key, command) end
    function SetOverrideBindingClick(owner, isPriority, key, button, mouseButton)
        set(owner, key, "CLICK " .. tostring(button) .. ":" .. tostring(mouseButton or "LeftButton"))
    end
    function SetOverrideBindingSpell(owner, isPriority, key, spell) set(owner, key, "SPELL " .. tostring(spell)) end
    function SetOverrideBindingItem(owner, isPriority, key, item) set(owner, key, "ITEM " .. tostring(item)) end
    function SetOverrideBindingMacro(owner, isPriority, key, macro) set(owner, key, "MACRO " .. tostring(macro)) end
    function ClearOverrideBindings(owner) if owner then overrides[owner] = nil end end
    -- r291 (Stefan 13.10: with Bartender the keys must press Bartender's buttons, the stock bar
    -- sends nothing): a key with an override binding runs it instead of the normal binding.
    -- Returns "__handled", the plain command to run, or nil when the key has no override.
    function __WowRunOverrideBinding(key)
        local command
        for _, keys in pairs(overrides) do
            if keys[key] then command = keys[key]; break end
        end
        if type(command) ~= "string" or command == "" then return nil end
        local name, mouse = command:match("^CLICK ([^:]+):?(.*)$")
        if name then
            local button = _G[name]
            if button and button.Click then button:Click(mouse ~= "" and mouse or "LeftButton") end
            return "__handled"
        end
        local spell = command:match("^SPELL (.+)$")
        if spell then if CastSpellByName then CastSpellByName(spell) end return "__handled" end
        local item = command:match("^ITEM (.+)$")
        if item then if UseItemByName then UseItemByName(item) end return "__handled" end
        local macro = command:match("^MACRO (.+)$")
        if macro then if RunMacro then RunMacro(macro) end return "__handled" end
        return command
    end
end

-- Ascension builds of some addons ask for a class bit mask.
if bit and missing("UnitClassMask") then
    local masks = { WARRIOR = 1, PALADIN = 2, HUNTER = 4, ROGUE = 8, PRIEST = 16, DEATHKNIGHT = 32,
        SHAMAN = 64, MAGE = 128, WARLOCK = 256, DRUID = 1024 }
    function UnitClassMask(unit)
        local _, token = UnitClass(unit or "player")
        return masks[token or ""] or 0
    end
    bit.contains = bit.contains or function(mask, value) return bit.band(mask or 0, value or 0) ~= 0 end
    EnumUtil = EnumUtil or {}
    EnumUtil.CombineMasks = EnumUtil.CombineMasks or function(...)
        local result = 0
        for i = 1, select("#", ...) do result = bit.bor(result, select(i, ...) or 0) end
        return result
    end
    Enum = Enum or {}
    if not Enum.ClassMask then
        Enum.ClassMask = { WARRIOR = 1, PALADIN = 2, HUNTER = 4, ROGUE = 8, PRIEST = 16, DEATHKNIGHT = 32,
            SHAMAN = 64, MAGE = 128, WARLOCK = 256, DRUID = 1024, HERO = 0 }
    end
end

)LUA" /* r424: MSVC caps one string literal at 16 KB; split here */ R"LUA(
-- Macro conditionals: "[mod:shift] 2; [bonusbar:1] 7; 1" -> value, target.
-- Used by SecureStateDriver (Bartender paging, RegisterStateDriver) and /cast-style options.
if missing("SecureCmdOptionParse") then
    local function call(f, ...) if type(f) == "function" then return f(...) end end
    local function modDown(arg)
        if not arg or arg == "" then return call(IsModifierKeyDown) and true or false end
        for k in arg:lower():gmatch("[^/]+") do
            if (k == "shift" and call(IsShiftKeyDown)) or (k == "ctrl" and call(IsControlKeyDown))
                or (k == "alt" and call(IsAltKeyDown)) then return true end
        end
        return false
    end
    local function numIn(arg, current)
        current = tonumber(current) or 0
        if not arg or arg == "" then return current ~= 0 end
        for n in arg:gmatch("[^/]+") do if tonumber(n) == current then return true end end
        return false
    end
    local function test(name, arg, unit)
        if name == "combat" then return call(UnitAffectingCombat, "player") or call(InCombatLockdown) end
        if name == "mod" or name == "modifier" then return modDown(arg) end
        if name == "stance" or name == "form" then return numIn(arg, call(GetShapeshiftForm)) end
        if name == "bonusbar" then return numIn(arg, call(GetBonusBarOffset)) end
        if name == "actionbar" or name == "bar" then return numIn(arg, call(GetActionBarPage)) end
        if name == "vehicleui" then return call(UnitHasVehicleUI, "player") end
        if name == "unithasvehicleui" then return call(UnitHasVehicleUI, unit) end
        if name == "possessbar" then return call(IsPossessBarVisible) end
        if name == "exists" then return call(UnitExists, unit) end
        if name == "harm" then return call(UnitCanAttack, "player", unit) end
        if name == "help" then return call(UnitExists, unit) and call(UnitIsFriend, "player", unit) end
        if name == "dead" then return call(UnitIsDeadOrGhost, unit) end
        if name == "party" then return (tonumber(call(GetNumPartyMembers)) or 0) > 0 end
        if name == "raid" then return (tonumber(call(GetNumRaidMembers)) or 0) > 0 end
        if name == "group" then
            if arg == "raid" then return (tonumber(call(GetNumRaidMembers)) or 0) > 0 end
            return (tonumber(call(GetNumPartyMembers)) or 0) > 0 or (tonumber(call(GetNumRaidMembers)) or 0) > 0
        end
        if name == "stealth" then return call(IsStealthed) end
        if name == "mounted" then return call(IsMounted) end
        if name == "swimming" then return call(IsSwimming) end
        if name == "flying" then return call(IsFlying) end
        if name == "flyable" then return call(IsFlyableArea) end
        if name == "indoors" then return call(IsIndoors) end
        if name == "outdoors" then return call(IsOutdoors) end
        if name == "pet" then return call(UnitExists, "pet") end
        if name == "channeling" then return call(UnitChannelInfo, "player") ~= nil end
        if name == "equipped" or name == "worn" then return call(IsEquippedItemType, arg) end
        if name == "spec" then return numIn(arg, call(GetActiveTalentGroup) or 1) end
        if name == "btn" or name == "button" then
            local clicked = call(GetMouseButtonClicked) or "LeftButton"
            for b in (arg or ""):gmatch("[^/]+") do
                if b == clicked or (b == "1" and clicked == "LeftButton") or (b == "2" and clicked == "RightButton") then return true end
            end
            return false
        end
        return false
    end
    local function trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
    function SecureCmdOptionParse(options)
        if type(options) ~= "string" then return nil end
        for clause in (options .. ";"):gmatch("([^;]*);") do
            local rest, groups = trim(clause), {}
            while rest:sub(1, 1) == "[" do
                local close = rest:find("]", 1, true)
                if not close then break end
                groups[#groups + 1] = rest:sub(2, close - 1)
                rest = trim(rest:sub(close + 1))
            end
            local matched, target = #groups == 0, nil
            for _, group in ipairs(groups) do
                local all, unit = true, nil
                for cond in (group .. ","):gmatch("([^,]*),") do
                    cond = trim(cond)
                    local t = cond:match("^@(.+)$") or cond:match("^target%s*=%s*(.+)$")
                    if t then unit = t
                    elseif cond ~= "" then
                        local neg = false
                        if cond:sub(1, 2) == "no" then neg = true; cond = cond:sub(3) end
                        local name, arg = cond:match("^([^:]+):?(.*)$")
                        local ok = test(name, arg, unit or "target") and true or false
                        if neg then ok = not ok end
                        if not ok then all = false break end
                    end
                end
                if all then matched, target = true, unit break end
            end
            if matched then return rest, target end
        end
        return nil
    end
end

)LUA" /* r426: new 16 KB segment */ R"LUA(
-- r426 (TormentWoW ElvUI): widget methods and globals the 3.3.5 client has that addon skins,
-- bags and bars call. Only added when missing; the native ones always win.
if missing("GetShapeshiftForm") then
    function GetShapeshiftForm()
        local count = GetNumShapeshiftForms and GetNumShapeshiftForms() or 0
        for index = 1, count do
            local _, _, active = GetShapeshiftFormInfo(index)
            if active then return index end
        end
        return 0
    end
end
if type(methods) == "table" then
    local function addMethod(name, fn) if methods[name] == nil then methods[name] = fn end end
    -- Slider:GetThumbTexture(): the XML thumb ($parentThumb) when there is one, else a hidden
    -- stand-in (the native slider draws its own thumb), so skins can recolour or resize it.
    addMethod("GetThumbTexture", function(self)
        if type(self) ~= "table" then return nil end
        local thumb = rawget(self, "__thumbRegion")
        if thumb then return thumb end
        local name = self.GetName and self:GetName()
        thumb = name and (_G[name .. "Thumb"] or _G[name .. "ThumbTexture"])
        if type(thumb) ~= "table" and self.CreateTexture then
            thumb = self:CreateTexture(nil, "OVERLAY")
            if thumb then
                local own = rawget(self, "__luaRegions")
                if own and own[#own] == thumb then own[#own] = nil end
                if thumb.Hide then thumb:Hide() end
                thumb.Show = function() end
            end
        end
        if type(thumb) == "table" then rawset(self, "__thumbRegion", thumb) return thumb end
        return nil
    end)
    addMethod("SetMaskTexture", function() end)
    addMethod("SetAltArrowKeyMode", function(self, enable) rawset(self, "__altArrowKeyMode", enable and true or false) end)
    addMethod("GetAltArrowKeyMode", function(self) return rawget(self, "__altArrowKeyMode") and 1 or nil end)
    addMethod("SetHorizTile", function(self, tile) rawset(self, "__horizTile", tile and true or false) end)
    addMethod("GetHorizTile", function(self) return rawget(self, "__horizTile") and 1 or nil end)
    addMethod("SetVertTile", function(self, tile) rawset(self, "__vertTile", tile and true or false) end)
    addMethod("GetVertTile", function(self) return rawget(self, "__vertTile") and 1 or nil end)
    addMethod("SetMinimumWidth", function(self, width) rawset(self, "__minimumWidth", tonumber(width) or 0) end)
    addMethod("GetMinimumWidth", function(self) return rawget(self, "__minimumWidth") or 0 end)
    -- StatusBar:GetStatusBarColor(): the last colour set (white until then).
    if methods.GetStatusBarColor == nil and type(methods.SetStatusBarColor) == "function" then
        local setStatusBarColor = methods.SetStatusBarColor
        methods.SetStatusBarColor = function(self, r, g, b, a, ...)
            if type(self) == "table" then
                if type(r) == "table" then r, g, b, a = r.r or r[1], r.g or r[2], r.b or r[3], r.a or r[4] end
                rawset(self, "__statusBarColor", { r or 1, g or 1, b or 1, a or 1 })
            end
            return setStatusBarColor(self, r, g, b, a, ...)
        end
        methods.GetStatusBarColor = function(self)
            local color = type(self) == "table" and rawget(self, "__statusBarColor")
            if color then return color[1], color[2], color[3], color[4] end
            return 1, 1, 1, 1
        end
    end

    -- Region:CreateAnimationGroup(): a Lua animation system (Alpha, Scale, Rotation, Translation)
    -- ticked from __WowDiagTick. Alpha and Rotation drive the region; scripts and looping work
    -- for every type, so addons that wait for OnFinished keep going.
    local playing = {}
    __WowAnimationGroupsPlaying = playing
    local Group, Anim = {}, {}
    Group.__index, Anim.__index = Group, Anim
    local function callScript(owner, name, ...)
        local fn = owner.__scripts and owner.__scripts[name]
        if fn then pcall(fn, owner, ...) end
        local hooks = owner.__hooks and owner.__hooks[name]
        if hooks then for _, hook in ipairs(hooks) do pcall(hook, owner, ...) end end
    end
    local function scriptMethods(class)
        function class:SetScript(name, fn) self.__scripts = self.__scripts or {} self.__scripts[name] = fn end
        function class:GetScript(name) return self.__scripts and self.__scripts[name] end
        function class:HookScript(name, fn)
            self.__hooks = self.__hooks or {}
            self.__hooks[name] = self.__hooks[name] or {}
            table.insert(self.__hooks[name], fn)
        end
    end
    scriptMethods(Group) scriptMethods(Anim)
    function Group:GetParent() return self.__parent end
    function Group:GetName() return self.__name end
    function Group:SetLooping(mode) self.__looping = mode end
    function Group:GetLooping() return self.__looping or "NONE" end
    function Group:IsPlaying() return self.__playing and 1 or nil end
    function Group:IsDone() return not self.__playing and 1 or nil end
    function Group:GetAnimations() return unpack(self.__anims) end
    function Group:CreateAnimation(kind, name)
        local anim = setmetatable({ __group = self, __type = kind or "Animation", __name = name,
            __duration = 0, __order = 1, __startDelay = 0, __endDelay = 0, __change = 0, __degrees = 0,
            __scaleX = 1, __scaleY = 1, __offsetX = 0, __offsetY = 0 }, Anim)
        table.insert(self.__anims, anim)
        return anim
    end
    function Group:Play()
        self.__elapsed, self.__playing = 0, true
        for _, anim in ipairs(self.__anims) do anim.__started, anim.__finished = nil, nil end
        playing[self] = true
        callScript(self, "OnPlay")
    end
    local function restore(group)
        for _, anim in ipairs(group.__anims) do
            local region = anim.__region or group.__parent
            if anim.__type == "Rotation" and region and region.SetRotation then pcall(region.SetRotation, region, 0) end
        end
    end
    function Group:Stop()
        if not self.__playing then return end
        self.__playing, playing[self] = false, nil
        restore(self)
        callScript(self, "OnStop", false)
    end
    function Group:Finish()
        if not self.__playing then return end
        self.__playing, playing[self] = false, nil
        restore(self)
        callScript(self, "OnFinished", false)
    end
    function Group:Pause() playing[self] = nil end
    function Anim:SetDuration(seconds) self.__duration = tonumber(seconds) or 0 end
    function Anim:GetDuration() return self.__duration end
    function Anim:SetOrder(order) self.__order = tonumber(order) or 1 end
    function Anim:GetOrder() return self.__order end
    function Anim:SetStartDelay(seconds) self.__startDelay = tonumber(seconds) or 0 end
    function Anim:SetEndDelay(seconds) self.__endDelay = tonumber(seconds) or 0 end
    function Anim:SetSmoothing(smoothing) self.__smoothing = smoothing end
    function Anim:SetChange(change) self.__change = tonumber(change) or 0 end
    function Anim:SetFromAlpha(alpha) self.__fromAlpha = tonumber(alpha) end
    function Anim:SetToAlpha(alpha) self.__toAlpha = tonumber(alpha) end
    function Anim:SetDegrees(degrees) self.__degrees = tonumber(degrees) or 0 end
    function Anim:SetRadians(radians) self.__degrees = math.deg(tonumber(radians) or 0) end
    function Anim:SetScale(x, y) self.__scaleX, self.__scaleY = tonumber(x) or 1, tonumber(y) or tonumber(x) or 1 end
    function Anim:SetOffset(x, y) self.__offsetX, self.__offsetY = tonumber(x) or 0, tonumber(y) or 0 end
    function Anim:SetOrigin() end
    function Anim:SetTarget(region) self.__region = region end
    function Anim:SetTargetName(name) self.__region = _G[name] end
    function Anim:GetRegionParent() return self.__group.__parent end
    function Anim:GetParent() return self.__group end
    function Anim:GetName() return self.__name end
    function Anim:IsPlaying() return self.__started and not self.__finished and 1 or nil end
    function Anim:Play() self.__group:Play() end
    function Anim:Stop() self.__group:Stop() end
    function Anim:GetProgress() return self.__progress or 0 end
    local function applyAnimation(anim, progress)
        anim.__progress = progress
        local region = anim.__region or anim.__group.__parent
        if not region then return end
        if anim.__type == "Alpha" then
            local from = anim.__fromAlpha or anim.__startAlpha or 1
            local to = anim.__toAlpha or (from + anim.__change)
            if region.SetAlpha then pcall(region.SetAlpha, region, math.max(0, math.min(1, from + (to - from) * progress))) end
        elseif anim.__type == "Rotation" then
            if region.SetRotation then pcall(region.SetRotation, region, math.rad(anim.__degrees * progress)) end
        end
    end
    local function tickGroup(group, elapsed)
        group.__elapsed = group.__elapsed + elapsed
        local orders = {}
        for _, anim in ipairs(group.__anims) do orders[anim.__order] = true end
        local sorted = {}
        for order in pairs(orders) do table.insert(sorted, order) end
        table.sort(sorted)
        local cursor, total = 0, 0
        for _, order in ipairs(sorted) do
            local longest = 0
            for _, anim in ipairs(group.__anims) do
                if anim.__order == order then
                    local length = anim.__startDelay + anim.__duration + anim.__endDelay
                    if length > longest then longest = length end
                    local local_time = group.__elapsed - cursor - anim.__startDelay
                    if local_time >= 0 and not anim.__finished then
                        if not anim.__started then
                            anim.__started = true
                            local region = anim.__region or group.__parent
                            anim.__startAlpha = region and region.GetAlpha and region:GetAlpha() or 1
                            callScript(anim, "OnPlay")
                        end
                        local progress = anim.__duration > 0 and math.min(1, local_time / anim.__duration) or 1
                        applyAnimation(anim, progress)
                        callScript(anim, "OnUpdate", elapsed)
                        if progress >= 1 then anim.__finished = true callScript(anim, "OnFinished", false) end
                    end
                end
            end
            cursor = cursor + longest
            total = total + longest
        end
        callScript(group, "OnUpdate", elapsed)
        if group.__elapsed >= total then
            local looping = group.__looping
            if looping == "REPEAT" or looping == "BOUNCE" then
                callScript(group, "OnLoop", "FORWARD")
                group.__elapsed = 0
                for _, anim in ipairs(group.__anims) do anim.__started, anim.__finished = nil, nil end
            else
                group.__playing, playing[group] = false, nil
                callScript(group, "OnFinished", false)
            end
        end
    end
    local lastTick
    function __WowAnimationTick()
        local now = GetTime and GetTime() or 0
        local elapsed = lastTick and (now - lastTick) or 0
        lastTick = now
        if elapsed <= 0 or elapsed > 1 then return end
        for group in pairs(playing) do
            local ok = pcall(tickGroup, group, elapsed)
            if not ok then group.__playing, playing[group] = false, nil end
        end
    end
    -- Ticked every frame by an OnUpdate frame made on first use (frames made while this file runs,
    -- before the UI has its canvas, never tick); __WowDiagTick also calls __WowAnimationTick.
    local ticker
    addMethod("CreateAnimationGroup", function(self, name)
        if not ticker and CreateFrame then
            ticker = CreateFrame("Frame")
            if ticker then ticker:SetScript("OnUpdate", function() __WowAnimationTick() end) end
        end
        local group = setmetatable({ __parent = self, __name = name, __anims = {} }, Group)
        if type(self) == "table" then
            local groups = rawget(self, "__animationGroups") or {}
            table.insert(groups, group)
            rawset(self, "__animationGroups", groups)
        end
        return group
    end)
    addMethod("GetAnimationGroups", function(self)
        local groups = type(self) == "table" and rawget(self, "__animationGroups")
        if groups then return unpack(groups) end
    end)
    addMethod("StopAnimating", function(self)
        local groups = type(self) == "table" and rawget(self, "__animationGroups")
        if groups then for _, group in ipairs(groups) do group:Stop() end end
    end)
end
)LUA";
}
#endif

void WowLuaApi::RegisterAddonCompat(lua_State* L)
{
#if HAS_LUA
	if (!L) return;
	RegisterIfMissing(L, "GetChannelName", L_GetChannelName);
	lua_register(L, "__WowFrameRegionNames", L_FrameRegionNames); // r230, for GetRegions
	lua_register(L, "__WowLog", L_WowLog); // r242
	RegisterIfMissing(L, "GetCurrentMapAreaID", L_GetCurrentMapAreaID);
	// frame:CreateTitleRegion() (LibKeyBound, dialogs): a draggable title area; a texture
	// region with the same anchoring API is enough for the callers.
	luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
	if (lua_istable(L, -1))
	{
		lua_getfield(L, -1, "CreateTitleRegion");
		const bool bMissing = lua_isnil(L, -1);
		lua_pop(L, 1);
		if (bMissing && luaL_loadstring(L,
			"local self = ...; local r = self:CreateTexture(nil, 'BACKGROUND'); "
			"if r and r.SetAllPoints then r:SetAllPoints(self) end; return r") == 0)
		{
			lua_setfield(L, -2, "CreateTitleRegion");
		}
	}
	lua_pop(L, 1);
	luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
	lua_setglobal(L, "__WowFrameMethods");
	if (luaL_dostring(L, CompatLua) != 0)
	{
		UE_LOG(LogWowLuaFrame, Warning, TEXT("Addon compat Lua failed: %s"), UTF8_TO_TCHAR(lua_tostring(L, -1)));
		lua_pop(L, 1);
	}
	// ConsoleExec (ChatFrame.lua /reload -> ConsoleExec("reloadui"), console commands from addons)
	// and the common /rl alias for /reload (SLASH_RELOAD3; GlobalStrings defines 1 and 2).
	if (luaL_dostring(L,
		"if not ConsoleExec then\n"
		"  function ConsoleExec(cmd)\n"
		"    cmd = (tostring(cmd or ''):gsub('^%s+', ''):gsub('%s+$', ''))\n"
		"    local lower = cmd:lower()\n"
		"    if lower == 'reloadui' or lower == 'reload' then if ReloadUI then ReloadUI() end return end\n"
		"    local name, value = cmd:match('^[Ss][Ee][Tt]%s+(%S+)%s*(.*)$')\n"
		"    if not name then name, value = cmd:match('^(%S+)%s+(.+)$') end\n"
		"    if name and SetCVar then SetCVar(name, value) end\n"
		"  end\n"
		"end\n"
		"if SLASH_RELOAD3 == nil then SLASH_RELOAD3 = '/rl' end\n") != 0)
	{
		UE_LOG(LogWowLuaFrame, Warning, TEXT("ConsoleExec compat failed: %s"), UTF8_TO_TCHAR(lua_tostring(L, -1)));
		lua_pop(L, 1);
	}
	// Real talent API (replaces the placeholder stubs registered earlier).
	RegisterTalentApi(L);
	// GameTooltip:SetUnit(unit) content in the 3.3.5 layout (the client builds this natively):
	// name in the reaction colour, <Guild>, "Level N Race Class (Player)" or "Level N Type
	// (Elite)", faction and PvP lines.
	static const char* UnitTooltipLua = R"LUA(
-- The client puts its string helpers on the string table too: ("x"):trim(), s:split().
if strtrim and not string.trim then string.trim = strtrim end
if strsplit and not string.split then string.split = strsplit end
if strjoin and not string.join then string.join = strjoin end
__WowClassColors = {
	HUNTER = { r = 0.67, g = 0.83, b = 0.45 }, WARLOCK = { r = 0.58, g = 0.51, b = 0.79 },
	PRIEST = { r = 1.0, g = 1.0, b = 1.0 }, PALADIN = { r = 0.96, g = 0.55, b = 0.73 },
	MAGE = { r = 0.41, g = 0.8, b = 0.94 }, ROGUE = { r = 1.0, g = 0.96, b = 0.41 },
	DRUID = { r = 1.0, g = 0.49, b = 0.04 }, SHAMAN = { r = 0.0, g = 0.44, b = 0.87 },
	WARRIOR = { r = 0.78, g = 0.61, b = 0.43 }, DEATHKNIGHT = { r = 0.77, g = 0.12, b = 0.23 },
}
function __WowUnitTooltip(tt, unit)
	if not tt or not unit or not UnitExists(unit) then return false end
	tt:ClearLines()
	local r, g, b = 1, 1, 1
	if GameTooltip_UnitColor then
		local ok, cr, cg, cb = pcall(GameTooltip_UnitColor, unit)
		if ok and cr then r, g, b = cr, cg, cb end
	end
	local name = (UnitIsPlayer(unit) and UnitPVPName and UnitPVPName(unit)) or UnitName(unit) or UNKNOWN
	-- Players: name in their class colour (RAID_CLASS_COLORS).
	local function ClassColor(u)
		local _, classFile = UnitClass(u)
		local colors = RAID_CLASS_COLORS or __WowClassColors
		local c = classFile and colors and colors[classFile]
		if c then return c.r, c.g, c.b end
	end
	-- The name keeps the reaction colour like the 3.3.5 client (GameTooltip_UnitColor: friendly
	-- players white, PvP-flagged green, attackable yellow, hostile red); only the class word below
	-- is in class colour (Stefan 10.10: a class-coloured name vanished for priests on white text).
	tt:AddLine(name, r, g, b)
	local level = UnitLevel(unit)
	local levelText = (level and level > 0) and tostring(level) or "??"
	if UnitIsPlayer(unit) then
		local guild = GetGuildInfo(unit)
		if guild and guild ~= "" then tt:AddLine("<" .. guild .. ">", 1, 1, 1) end
		local race = UnitRace(unit) or ""
		local class = UnitClass(unit) or ""
		-- The class word in its class colour too.
		local ccr, ccg, ccb = ClassColor(unit)
		if ccr and class ~= "" then
			class = format("|cff%02x%02x%02x%s|r", ccr * 255, ccg * 255, ccb * 255, class)
		end
		tt:AddLine(format(TOOLTIP_UNIT_LEVEL_RACE_CLASS_TYPE or "Level %s %s %s (%s)", levelText, race, class, PLAYER or "Player"), 1, 1, 1)
		local faction = UnitFactionGroup(unit)
		if faction then tt:AddLine(_G[strupper(faction)] or faction, 1, 1, 1) end
	else
		local creatureType = UnitCreatureType(unit)
		local classification = UnitClassification(unit)
		local line
		if classification == "elite" or classification == "rareelite" or classification == "worldboss" then
			local rank = (classification == "worldboss" and BOSS) or (classification == "rareelite" and ELITE) or ELITE
			line = creatureType and format(TOOLTIP_UNIT_LEVEL_CLASS_TYPE or "Level %s %s (%s)", levelText, creatureType, rank)
				or format(TOOLTIP_UNIT_LEVEL_TYPE or "Level %s (%s)", levelText, rank)
		else
			line = creatureType and format(TOOLTIP_UNIT_LEVEL_CLASS or "Level %s %s", levelText, creatureType)
				or format(TOOLTIP_UNIT_LEVEL or "Level %s", levelText)
		end
		tt:AddLine(line, 1, 1, 1)
		if UnitIsDead(unit) then tt:AddLine(CORPSE or "Corpse", 1, 1, 1) end
		-- Quest objectives this unit counts for (quest title once, then its objectives).
		if __WowUnitQuestLines then
			local q = {__WowUnitQuestLines(unit)}
			local lastTitle
			for i = 1, #q, 2 do
				if q[i] ~= lastTitle then tt:AddLine(q[i], 1, 0.82, 0); lastTitle = q[i] end
				tt:AddLine(q[i + 1], 1, 1, 1)
			end
		end
	end
	if UnitIsPVP and UnitIsPVP(unit) then tt:AddLine(PVP or "PvP", 1, 1, 1) end
	if UnitIsPlayer(unit) then
		if UnitIsGhost and UnitIsGhost(unit) then tt:AddLine(GHOST or "Ghost", .5, .5, .5)
		elseif UnitIsDead(unit) then tt:AddLine(DEAD or "Dead", .5, .5, .5) end
	end
	-- Who the unit is targeting (you, or the target's name in its class / reaction colour).
	local target = unit .. "target"
	local okTarget, hasTarget = pcall(UnitExists, target)
	if okTarget and hasTarget and not UnitIsUnit(unit, "player") then
		local tr, tg, tb = 1, 1, 1
		local targetName
		if UnitIsUnit(target, "player") then
			targetName, tr, tg, tb = "<< " .. (YOU or "You") .. " >>", 1, .1, .1
		else
			targetName = UnitName(target)
			if UnitIsPlayer(target) then
				local cr, cg, cb = ClassColor(target)
				if cr then tr, tg, tb = cr, cg, cb end
			elseif GameTooltip_UnitColor then
				local ok, cr, cg, cb = pcall(GameTooltip_UnitColor, target)
				if ok and cr then tr, tg, tb = cr, cg, cb end
			end
		end
		if targetName then tt:AddDoubleLine((TARGET or "Target") .. ":", targetName, 1, .82, 0, tr, tg, tb) end
	end
	return true
end
)LUA";
	if (luaL_dostring(L, UnitTooltipLua) != 0)
	{
		UE_LOG(LogWowLuaFrame, Warning, TEXT("Unit tooltip Lua failed: %s"), UTF8_TO_TCHAR(lua_tostring(L, -1)));
		lua_pop(L, 1);
	}

	// r307 (Stefan 13.10: MoveAnything.lua:5636 "attempt to index global 'EscapeMenu'"): some addon
	// builds add their button to the game menu with EscapeMenu:AddButton(text, section, func, ...).
	// The labels go to __WowEscapeMenuLabels for the native game menu, whose button closes the menu
	// and runs __WowRunEscapeMenuButton(i). Other EscapeMenu fields fall through to GameMenuFrame.
	static const char* EscapeMenuLua = R"LUA(
if EscapeMenu == nil then
	EscapeMenuSection = EscapeMenuSection or setmetatable({}, { __index = function(_, key) return key end })
	__WowEscapeMenuButtons = {}
	EscapeMenu = setmetatable({}, { __index = function(_, key)
		local menu = rawget(_G, "GameMenuFrame")
		local value = menu and menu[key]
		if type(value) == "function" then return function(_, ...) return value(menu, ...) end end
		return value
	end })
	function EscapeMenu:AddButton(text, section, func)
		if type(text) ~= "string" or type(func) ~= "function" then return end
		local list = __WowEscapeMenuButtons
		list[#list + 1] = { text = text, func = func }
		local labels = {}
		for i, entry in ipairs(list) do labels[i] = entry.text end
		__WowEscapeMenuLabels = table.concat(labels, "\n")
	end
	function __WowRunEscapeMenuButton(index)
		local entry = __WowEscapeMenuButtons[index]
		if not entry then return end
		local ok, err = pcall(entry.func)
		if not ok then
			local handler = geterrorhandler and geterrorhandler()
			if handler then handler(err) end
		end
	end
end
)LUA";
	if (luaL_dostring(L, EscapeMenuLua) != 0)
	{
		UE_LOG(LogWowLuaFrame, Warning, TEXT("EscapeMenu Lua failed: %s"), UTF8_TO_TCHAR(lua_tostring(L, -1)));
		lua_pop(L, 1);
	}
#endif
}
