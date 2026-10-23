#include "LuaApiRegistry.h"
#include "WowColor.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "WowFrameTypes.h"
#include "WowEntity.h"
#include "WowSpellPresentation.h"
#include "WowConnectionManager.h"
#include "WowItemPresentation.h"
#include "WowInventoryManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowEntityManager.h"
#include "Components/ProgressBar.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "WowBlendImage.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/CanvasPanel.h"
#include "Slate/WidgetTransform.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Texture2D.h"
#include "Slate/SlateBrushAsset.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

DEFINE_LOG_CATEGORY(LogWowLuaFrame);

// ── Helpers ──────────────────────────────────────────────────────────────────────

static UImage* GetTextureImage(lua_State* L, int idx = 1);
static int LF_SetText(lua_State* L);
static int LF_GetText(lua_State* L);
static int LF_FontStringSetTextColor(lua_State* L);
static void PushNewFontObject(lua_State* L, const char* Name);
static int LF_SetFontObject(lua_State* L);
static int LF_GetFontObject(lua_State* L);
// r293: a fontstring's own colour while a "|cff...|r" text shows its markup colour (see LF_SetText).
static TMap<TWeakObjectPtr<UTextBlock>, FSlateColor> GWowMarkupBaseColors;

// Get the frame handle from a frame table (self argument at ArgIdx)
static int64 GetFrameHandle(lua_State* L, int ArgIdx = 1)
{
	luaL_checktype(L, ArgIdx, LUA_TTABLE);
	lua_getfield(L, ArgIdx, "__handle");
	int64 Handle = static_cast<int64>(lua_tointeger(L, -1));
	lua_pop(L, 1);
	return Handle;
}

static int64 GetOptionalFrameHandle(lua_State* L, int ArgIdx = 1)
{
	luaL_checktype(L, ArgIdx, LUA_TTABLE);
	lua_getfield(L, ArgIdx, "__handle");
	int64 Handle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	return Handle;
}

static int64 GetMouseQueryHandle(lua_State* L, int ArgIdx = 1)
{
	luaL_checktype(L, ArgIdx, LUA_TTABLE);

	lua_getfield(L, ArgIdx, "__handle");
	if (lua_isnumber(L, -1))
	{
		const int64 Handle = static_cast<int64>(lua_tointeger(L, -1));
		lua_pop(L, 1);
		return Handle;
	}
	lua_pop(L, 1);

	lua_getfield(L, ArgIdx, "__parentHandle");
	const int64 ParentHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	return ParentHandle;
}

static UButton* GetButtonWidgetForTable(lua_State* L, int Idx = 1)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return nullptr;

	const int64 Handle = GetOptionalFrameHandle(L, Idx);
	if (Handle < 0) return nullptr;

	if (UButton* Button = Ctx->FrameManager->GetButtonWidget(Handle))
	{
		return Button;
	}

	return Cast<UButton>(Ctx->FrameManager->GetWidgetForHandle(Handle));
}

static UEditableTextBox* GetEditBoxWidgetForTable(lua_State* L, int Idx = 1)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return nullptr;

	const int64 Handle = GetOptionalFrameHandle(L, Idx);
	if (Handle < 0) return nullptr;

	if (UEditableTextBox* EditBox = Ctx->FrameManager->GetEditBoxWidget(Handle))
	{
		return EditBox;
	}

	return Cast<UEditableTextBox>(Ctx->FrameManager->GetWidgetForHandle(Handle));
}

static UTextBlock* GetTextBlockWidgetForTable(lua_State* L, int Idx = 1)
{
	luaL_checktype(L, Idx, LUA_TTABLE);

	lua_getfield(L, Idx, "__textWidget");
	if (lua_islightuserdata(L, -1))
	{
		UTextBlock* TextBlock = static_cast<UTextBlock*>(lua_touserdata(L, -1));
		lua_pop(L, 1);
		return TextBlock;
	}
	lua_pop(L, 1);

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return nullptr;

	lua_getfield(L, Idx, "__name");
	if (lua_isstring(L, -1))
	{
		if (UTextBlock* NamedText = Ctx->FrameManager->GetNamedFontString(UTF8_TO_TCHAR(lua_tostring(L, -1))))
		{
			lua_pop(L, 1);
			return NamedText;
		}
	}
	lua_pop(L, 1);

	const int64 Handle = GetOptionalFrameHandle(L, Idx);
	if (Handle >= 0)
	{
		if (UTextBlock* PrimaryText = Ctx->FrameManager->GetPrimaryTextWidget(Handle))
		{
			return PrimaryText;
		}

		return Cast<UTextBlock>(Ctx->FrameManager->GetWidgetForHandle(Handle));
	}

	return nullptr;
}

static bool TableRepresentsRegion(lua_State* L, int Idx = 1)
{
	luaL_checktype(L, Idx, LUA_TTABLE);

	lua_getfield(L, Idx, "__objectType");
	const bool bHasObjectType = (lua_isstring(L, -1) != 0);
	lua_pop(L, 1);
	if (bHasObjectType)
	{
		return true;
	}

	lua_getfield(L, Idx, "__imageWidget");
	const bool bHasImageWidget = lua_islightuserdata(L, -1);
	lua_pop(L, 1);
	if (bHasImageWidget)
	{
		return true;
	}

	lua_getfield(L, Idx, "__textWidget");
	const bool bHasTextWidget = lua_islightuserdata(L, -1);
	lua_pop(L, 1);
	return bHasTextWidget;
}

static int LF_PlayObject(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 1);
	lua_setfield(L, 1, "__playing");
	return 0;
}

static int LF_StopObject(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 0);
	lua_setfield(L, 1, "__playing");
	return 0;
}

static int LF_IsObjectPlaying(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "__playing");
	const bool bPlaying = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	lua_pushboolean(L, bPlaying ? 1 : 0);
	return 1;
}

static int LF_IsMouseOver(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushboolean(L, 0);
		return 1;
	}

	const int64 Handle = GetMouseQueryHandle(L, 1);
	if (Handle < 0)
	{
		lua_pushboolean(L, 0);
		return 1;
	}

	const float TopOffset = static_cast<float>(luaL_optnumber(L, 2, 0.0));
	const float BottomOffset = static_cast<float>(luaL_optnumber(L, 3, 0.0));
	const float LeftOffset = static_cast<float>(luaL_optnumber(L, 4, 0.0));
	const float RightOffset = static_cast<float>(luaL_optnumber(L, 5, 0.0));

	lua_pushboolean(L, Ctx->FrameManager->IsMouseOverFrame(Handle, TopOffset, BottomOffset, LeftOffset, RightOffset) ? 1 : 0);
	return 1;
}

// ── Global: CreateFrame(type, name, parent, template) ────────────────────────────

static EWowFrameType ParseFrameType(const char* TypeStr)
{
	if (!TypeStr) return EWowFrameType::Frame;
	if (strcmp(TypeStr, "Button") == 0) return EWowFrameType::Button;
	if (strcmp(TypeStr, "CheckButton") == 0) return EWowFrameType::CheckButton;
	if (strcmp(TypeStr, "EditBox") == 0) return EWowFrameType::EditBox;
	if (strcmp(TypeStr, "ScrollFrame") == 0) return EWowFrameType::ScrollFrame;
	if (strcmp(TypeStr, "Slider") == 0) return EWowFrameType::Slider;
	if (strcmp(TypeStr, "StatusBar") == 0) return EWowFrameType::StatusBar;
	if (strcmp(TypeStr, "SimpleHTML") == 0) return EWowFrameType::SimpleHTML;
	if (strcmp(TypeStr, "Cooldown") == 0) return EWowFrameType::Cooldown;
	if (strcmp(TypeStr, "GameTooltip") == 0) return EWowFrameType::GameTooltip;
	if (strcmp(TypeStr, "Minimap") == 0) return EWowFrameType::Minimap;
	if (strcmp(TypeStr, "Model") == 0) return EWowFrameType::Model;
	if (strcmp(TypeStr, "PlayerModel") == 0) return EWowFrameType::PlayerModel;
	if (strcmp(TypeStr, "MessageFrame") == 0) return EWowFrameType::MessageFrame;
	if (strcmp(TypeStr, "ScrollingMessageFrame") == 0) return EWowFrameType::ScrollingMessageFrame;
	return EWowFrameType::Frame;
}

static int L_CreateFrame(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !Ctx->EventSystem) { lua_pushnil(L); return 1; }

	const char* TypeStr = luaL_optstring(L, 1, "Frame");
	const char* Name = (lua_type(L, 2) == LUA_TSTRING) ? lua_tostring(L, 2) : nullptr;
	if (Name && !Name[0]) Name = nullptr;
	// arg 3 = parent (frame table, frame name, or nil)
	const char* TemplateName = (lua_type(L, 4) == LUA_TSTRING) ? lua_tostring(L, 4) : nullptr;

	FWowFrameDef Def;
	Def.Type = ParseFrameType(TypeStr);
	if (Name)
	{
		Def.Name = UTF8_TO_TCHAR(Name);
	}
	else
	{
		// The layout keys frame rects and parent links by name, so an anonymous frame
		// gets an internal one. GetName() still returns nil, like the original client;
		// without this every unnamed addon frame lost its (unnamed) parent and size.
		static int64 AnonymousFrameCounter = 0;
		Def.Name = FString::Printf(TEXT("%s%lld_"), WowLuaApi::ANONYMOUS_FRAME_PREFIX, ++AnonymousFrameCounter);
	}
	if (TemplateName) Def.Inherits = UTF8_TO_TCHAR(TemplateName);
	Def.bCreatedByLua = true;
	// A GameTooltip only shows once something owns it (SetOwner + Show). Addons create
	// scanning tooltips with CreateFrame("GameTooltip") and never show them; they must not
	// appear as empty boxes (ElvUI's __WowAnon tooltip at the top left).
	if (Def.Type == EWowFrameType::GameTooltip) Def.bHidden = true;

	// Resolve parent
	if (lua_istable(L, 3))
	{
		lua_getfield(L, 3, "__name");
		if (lua_isstring(L, -1))
		{
			Def.Parent = UTF8_TO_TCHAR(lua_tostring(L, -1));
		}
		lua_pop(L, 1);
	}
	else if (lua_isstring(L, 3))
	{
		Def.Parent = UTF8_TO_TCHAR(lua_tostring(L, 3));
	}

	int64 Handle = Ctx->FrameManager->CreateFrame(Def);
	if (Handle < 0) { lua_pushnil(L); return 1; }
	// r296 diagnostic (Stefan 13.10: AceGUI's Close button, UIPanelButtonTemplate, showed no art):
	// the state textures a Lua button got from its template, for the first 40 such buttons.
	if (TemplateName && (Def.Type == EWowFrameType::Button || Def.Type == EWowFrameType::CheckButton))
	{
		static int32 Logged = 0;
		if (Logged < 40)
			if (const FWowFrameDef* Made = Ctx->FrameManager->GetFrameDef(Handle))
			{
				++Logged;
				UE_LOG(LogTemp, Log, TEXT("Lua button template: %s inherits=%s normal='%s' pushed='%s' highlight='%s' layers=%d"),
					*Made->Name, UTF8_TO_TCHAR(TemplateName), *Made->NormalTexture, *Made->PushedTexture, *Made->HighlightTexture, Made->Layers.Num());
			}
	}

	// Return the same table scripts receive as self, so fields set on the returned
	// object (f.text = ...) are visible inside OnEvent/OnUpdate/OnClick handlers.
	if (!Ctx->EventSystem->PushFrameObject(Handle))
	{
		Ctx->EventSystem->CreateFrameObject(Handle, Def.Name);
		if (!Ctx->EventSystem->PushFrameObject(Handle))
		{
			lua_newtable(L);
			lua_pushinteger(L, Handle);
			lua_setfield(L, -2, "__handle");
			lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<uintptr_t>(Handle + 1)));
			lua_rawseti(L, -2, 0);
			luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
			lua_setmetatable(L, -2);
		}
	}
	return 1;
}

// ── Frame Methods ────────────────────────────────────────────────────────────────

// frame:GetName()
static int LF_GetName(lua_State* L)
{
	lua_getfield(L, 1, "__name");
	const char* Name = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
	if (!Name || strncmp(Name, WowLuaApi::ANONYMOUS_FRAME_PREFIX_ANSI, strlen(WowLuaApi::ANONYMOUS_FRAME_PREFIX_ANSI)) == 0)
	{
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

// frame:GetObjectType()
static int LF_GetObjectType(lua_State* L)
{
	lua_getfield(L, 1, "__objectType");
	if (lua_isstring(L, -1))
	{
		return 1;
	}
	lua_pop(L, 1);

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			switch (Def->Type)
			{
			case EWowFrameType::Button: lua_pushstring(L, "Button"); break;
			case EWowFrameType::CheckButton: lua_pushstring(L, "CheckButton"); break;
			case EWowFrameType::EditBox: lua_pushstring(L, "EditBox"); break;
			case EWowFrameType::ScrollFrame: lua_pushstring(L, "ScrollFrame"); break;
			case EWowFrameType::Slider: lua_pushstring(L, "Slider"); break;
			case EWowFrameType::StatusBar: lua_pushstring(L, "StatusBar"); break;
			case EWowFrameType::SimpleHTML: lua_pushstring(L, "SimpleHTML"); break;
			case EWowFrameType::Cooldown: lua_pushstring(L, "Cooldown"); break;
			case EWowFrameType::GameTooltip: lua_pushstring(L, "GameTooltip"); break;
			case EWowFrameType::Minimap: lua_pushstring(L, "Minimap"); break;
			case EWowFrameType::Model: lua_pushstring(L, "Model"); break;
			case EWowFrameType::PlayerModel: lua_pushstring(L, "PlayerModel"); break;
			case EWowFrameType::MessageFrame: lua_pushstring(L, "MessageFrame"); break;
			case EWowFrameType::ScrollingMessageFrame: lua_pushstring(L, "ScrollingMessageFrame"); break;
			default: lua_pushstring(L, "Frame"); break;
			}
			return 1;
		}
	}
	lua_pushstring(L, "Frame");
	return 1;
}

// object:IsObjectType(type) - true for the object's own type and every type it inherits from
// (UIObject > Region > LayeredRegion > Texture/FontString, Region > Frame > Button > CheckButton ...).
static int LF_IsObjectType(lua_State* L)
{
	const FString Wanted = UTF8_TO_TCHAR(luaL_checkstring(L, 2));
	FString Type = TEXT("Frame");
	lua_getfield(L, 1, "GetObjectType");
	if (lua_isfunction(L, -1))
	{
		lua_pushvalue(L, 1);
		if (lua_pcall(L, 1, 1, 0) == 0 && lua_isstring(L, -1)) Type = UTF8_TO_TCHAR(lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	else
	{
		lua_pop(L, 1);
	}
	auto Is = [&Wanted](const TCHAR* Name) { return Wanted.Equals(Name, ESearchCase::IgnoreCase); };
	const bool bLayered = Type.Equals(TEXT("Texture"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("FontString"), ESearchCase::IgnoreCase);
	bool bResult = Wanted.Equals(Type, ESearchCase::IgnoreCase) || Is(TEXT("UIObject")) || Is(TEXT("ParentedObject")) || Is(TEXT("Region"));
	if (!bResult && bLayered) bResult = Is(TEXT("LayeredRegion"));
	if (!bResult && !bLayered)
	{
		bResult = Is(TEXT("Frame")) || Is(TEXT("ScriptObject"))
			|| (Is(TEXT("Button")) && Type.Equals(TEXT("CheckButton"), ESearchCase::IgnoreCase))
			|| (Is(TEXT("Model")) && (Type.Equals(TEXT("PlayerModel"), ESearchCase::IgnoreCase)
				|| Type.Equals(TEXT("DressUpModel"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("TabardModel"), ESearchCase::IgnoreCase)));
	}
	lua_pushboolean(L, bResult);
	return 1;
}

// frame:IsEventRegistered(event)
static int LF_IsEventRegistered(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	const char* Event = luaL_checkstring(L, 2);
	lua_pushboolean(L, Ctx && Ctx->EventSystem && Ctx->EventSystem->IsEventRegistered(Handle, UTF8_TO_TCHAR(Event)));
	return 1;
}

// frame:RegisterEvent(event)
static int LF_RegisterEvent(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	const char* Event = luaL_checkstring(L, 2);
	if (Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->RegisterEvent(Handle, UTF8_TO_TCHAR(Event));
	}
	return 0;
}

// frame:UnregisterEvent(event)
static int LF_UnregisterEvent(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	const char* Event = luaL_checkstring(L, 2);
	if (Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->UnregisterEvent(Handle, UTF8_TO_TCHAR(Event));
	}
	return 0;
}

// frame:UnregisterAllEvents()
static int LF_UnregisterAllEvents(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->UnregisterAllEvents(Handle);
	}
	// r239: Bartender4:HideBlizzard() hides the stock action buttons and unregisters their events.
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->NoteScriptReleasedFrame(Handle);
	}
	return 0;
}

// frame:SetScript(scriptType, handler)
static int LF_SetScript(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	const char* ScriptType = luaL_checkstring(L, 2);

	if (Ctx && Ctx->EventSystem)
	{
		if (lua_isfunction(L, 3))
		{
			// Store the Lua function reference directly in the event system
			lua_pushvalue(L, 3);
			int32 Ref = luaL_ref(L, LUA_REGISTRYINDEX);
			Ctx->EventSystem->SetFrameScriptRef(Handle, UTF8_TO_TCHAR(ScriptType), Ref);

			// Also store in a global key so GetScript can retrieve it
			FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_%s"),
				Handle, UTF8_TO_TCHAR(ScriptType));
			FTCHARToUTF8 UTF8GlobalKey(*GlobalKey);
			lua_pushvalue(L, 3);
			lua_setglobal(L, UTF8GlobalKey.Get());
		}
		else if (lua_isstring(L, 3))
		{
			// Code string passed - compile it
			const char* CodeStr = lua_tostring(L, 3);
			Ctx->EventSystem->SetFrameScript(Handle, UTF8_TO_TCHAR(ScriptType), UTF8_TO_TCHAR(CodeStr));
		}
		else if (lua_isnil(L, 3))
		{
			// Clear the script
			Ctx->EventSystem->SetFrameScript(Handle, UTF8_TO_TCHAR(ScriptType), FString());
		}
	}
	return 0;
}

// frame:GetScript(scriptType)
static int LF_GetScript(lua_State* L)
{
	int64 Handle = GetFrameHandle(L);
	const char* ScriptType = luaL_checkstring(L, 2);

	// Try to find the stored function in the global
	FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_%s"),
		Handle, UTF8_TO_TCHAR(ScriptType));
	FTCHARToUTF8 UTF8Key(*GlobalKey);

	lua_getglobal(L, UTF8Key.Get());
	if (lua_isfunction(L, -1))
	{
		return 1;
	}
	lua_pop(L, 1);
	// r267: the XML handler (OnShow function="..." or inline code) when no Lua code set one.
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->EventSystem && Handle >= 0 && Ctx->EventSystem->PushFrameScript(Handle, UTF8_TO_TCHAR(ScriptType)))
	{
		return 1;
	}
	lua_pushnil(L);
	return 1;
}

// frame:Show()
static int LF_Show(lua_State* L)
{
	if (TableRepresentsRegion(L, 1))
	{
		if (UImage* Image = GetTextureImage(L, 1))
		{
			// r239: a HIGHLIGHT-layer region stays hover-only (r237).
			FWowLuaContext* RegionCtx = WowLuaApi::GetContext(L);
			if (RegionCtx && RegionCtx->FrameManager && RegionCtx->FrameManager->IsHighlightLayerRegion(Image))
				RegionCtx->FrameManager->SetHighlightLayerRegionScriptShown(Image, true);
			else
				Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			return 0;
		}
		if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			TextBlock->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			return 0;
		}
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameVisible(Handle, true);
	}
	if (Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->RunFrameScript(Handle, TEXT("OnShow"));
	}
	return 0;
}

// frame:Hide()
static int LF_Hide(lua_State* L)
{
	if (TableRepresentsRegion(L, 1))
	{
		if (UImage* Image = GetTextureImage(L, 1))
		{
			FWowLuaContext* RegionCtx = WowLuaApi::GetContext(L);
			if (RegionCtx && RegionCtx->FrameManager && RegionCtx->FrameManager->IsHighlightLayerRegion(Image))
				RegionCtx->FrameManager->SetHighlightLayerRegionScriptShown(Image, false);
			Image->SetVisibility(ESlateVisibility::Collapsed);
			return 0;
		}
		if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			TextBlock->SetVisibility(ESlateVisibility::Collapsed);
			return 0;
		}
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	// r393 (16.10 log, Bartender4 key binding mode: "OnHide: C stack overflow"): OnHide runs only when
	// the frame was shown, like the client. LibKeyBound's binder hides itself from its own OnHide
	// (Binder:OnHide -> LibKeyBound:Set(nil) -> binder:Hide()), which recursed until the C stack ran
	// out, so the binder never worked and binding mode could not bind any key.
	const bool bWasShown = Ctx && Ctx->FrameManager && Ctx->FrameManager->IsFrameVisible(Handle);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameVisible(Handle, false);
	}
	if (bWasShown && Ctx->EventSystem)
	{
		Ctx->EventSystem->RunFrameScript(Handle, TEXT("OnHide"));
	}
	// r406 (Stefan 16.10: a bag button's tooltip never went away): a hidden tooltip has no owner, as
	// in the client. The owner stayed, so GameTooltip:IsOwned(bagButton) kept answering yes and the
	// bag button's events (BAG_UPDATE ...) drew its tooltip again right after it was hidden.
	lua_getfield(L, 1, "__tooltipOwner");
	const bool bHadTooltipOwner = !lua_isnil(L, -1);
	lua_pop(L, 1);
	if (bHadTooltipOwner)
	{
		lua_pushnil(L);
		lua_setfield(L, 1, "__tooltipOwner");
	}
	return 0;
}

// frame:IsShown()
static int LF_IsShown(lua_State* L)
{
	if (TableRepresentsRegion(L, 1))
	{
		if (UImage* Image = GetTextureImage(L, 1))
		{
			lua_pushboolean(L, Image->GetVisibility() != ESlateVisibility::Collapsed);
			return 1;
		}
		if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			lua_pushboolean(L, TextBlock->GetVisibility() != ESlateVisibility::Collapsed);
			return 1;
		}
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		lua_pushboolean(L, Ctx->FrameManager->IsFrameVisible(Handle) ? 1 : 0);
		return 1;
	}
	lua_pushboolean(L, 1);
	return 1;
}

// frame:IsVisible()
static int LF_IsVisible(lua_State* L)
{
	// IsVisible checks both self and parent chain — for now same as IsShown
	return LF_IsShown(L);
}

// Parse a WoW anchor point string to enum
static EWowAnchorPoint ParseAnchorPoint(const char* Str)
{
	if (!Str) return EWowAnchorPoint::CENTER;
	// The client accepts any case ("topleft", "BottomRight").
	struct FName { const char* N; EWowAnchorPoint P; };
	static const FName Names[] = {
		{"TOPLEFT", EWowAnchorPoint::TOPLEFT}, {"TOP", EWowAnchorPoint::TOP}, {"TOPRIGHT", EWowAnchorPoint::TOPRIGHT},
		{"LEFT", EWowAnchorPoint::LEFT}, {"CENTER", EWowAnchorPoint::CENTER}, {"RIGHT", EWowAnchorPoint::RIGHT},
		{"BOTTOMLEFT", EWowAnchorPoint::BOTTOMLEFT}, {"BOTTOM", EWowAnchorPoint::BOTTOM}, {"BOTTOMRIGHT", EWowAnchorPoint::BOTTOMRIGHT} };
	for (const FName& Entry : Names)
	{
		if (FCStringAnsi::Stricmp(Str, Entry.N) == 0) return Entry.P;
	}
	return EWowAnchorPoint::CENTER;
}

static bool IsAnchorPointName(const char* Str)
{
	if (!Str) return false;
	static const char* Names[] = {"TOPLEFT","TOP","TOPRIGHT","LEFT","CENTER","RIGHT","BOTTOMLEFT","BOTTOM","BOTTOMRIGHT"};
	for (const char* N : Names) if (FCStringAnsi::Stricmp(Str, N) == 0) return true;
	return false;
}

// Convert anchor point enum to string
static const char* AnchorPointToString(EWowAnchorPoint Point)
{
	switch (Point)
	{
	case EWowAnchorPoint::TOPLEFT: return "TOPLEFT";
	case EWowAnchorPoint::TOP: return "TOP";
	case EWowAnchorPoint::TOPRIGHT: return "TOPRIGHT";
	case EWowAnchorPoint::LEFT: return "LEFT";
	case EWowAnchorPoint::CENTER: return "CENTER";
	case EWowAnchorPoint::RIGHT: return "RIGHT";
	case EWowAnchorPoint::BOTTOMLEFT: return "BOTTOMLEFT";
	case EWowAnchorPoint::BOTTOM: return "BOTTOM";
	case EWowAnchorPoint::BOTTOMRIGHT: return "BOTTOMRIGHT";
	default: return "CENTER";
	}
}

// Unit-cast sparks use the actual UMG fill geometry. Stock Lua reanchors the
// region immediately after SetValue; don't let its generic region fallback
// reset the position (or accidentally resolve a region as frame handle zero).
static bool UpdateUnitCastSparkRegion(lua_State* L)
{
	lua_getfield(L, 1, "__name");
	const FString Name = lua_isstring(L, -1) ? UTF8_TO_TCHAR(lua_tostring(L, -1)) : TEXT("");
	lua_pop(L, 1);
	if (Name != TEXT("TargetFrameSpellBarSpark") && Name != TEXT("FocusFrameSpellBarSpark") &&
		Name != TEXT("CastingBarFrameSpark") && Name != TEXT("PetCastingBarFrameSpark"))
	{
		return false;
	}
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		const int64 Handle = Ctx->FrameManager->FindFrame(Name.LeftChop(5));
		if (UProgressBar* Bar = Ctx->FrameManager->GetStatusBarWidget(Handle))
		{
			Ctx->FrameManager->UpdateStatusBarSpark(Handle, Bar->GetPercent());
		}
	}
	return true;
}

// ── Region (Texture / FontString) layout ─────────────────────────────────────────
// In the 3.3.5 client a region is laid out against its parent frame's rect. Each
// dynamic or XML region table keeps its points in "__points"
// ({ [POINT] = { relPoint, x, y, relX, relY } }) and its size in __width/__height;
// the canvas slot is rebuilt from that state on every change. Two opposite points
// on an axis stretch the region on that axis, like SetPoint TOPLEFT + BOTTOMRIGHT.
static FVector2D AnchorFraction(EWowAnchorPoint P)
{
	switch (P)
	{
	case EWowAnchorPoint::TOPLEFT: return FVector2D(0.f, 0.f);
	case EWowAnchorPoint::TOP: return FVector2D(0.5f, 0.f);
	case EWowAnchorPoint::TOPRIGHT: return FVector2D(1.f, 0.f);
	case EWowAnchorPoint::LEFT: return FVector2D(0.f, 0.5f);
	case EWowAnchorPoint::RIGHT: return FVector2D(1.f, 0.5f);
	case EWowAnchorPoint::BOTTOMLEFT: return FVector2D(0.f, 1.f);
	case EWowAnchorPoint::BOTTOM: return FVector2D(0.5f, 1.f);
	case EWowAnchorPoint::BOTTOMRIGHT: return FVector2D(1.f, 1.f);
	default: return FVector2D(0.5f, 0.5f);
	}
}

/** Widget of a Texture/FontString table (never a frame). */
static UWidget* GetRegionWidget(lua_State* L, int Idx)
{
	if (!lua_istable(L, Idx)) return nullptr;
	lua_getfield(L, Idx, "__handle");
	const bool bFrame = lua_isnumber(L, -1) != 0;
	lua_pop(L, 1);
	if (bFrame) return nullptr;
	if (UImage* Image = GetTextureImage(L, Idx)) return Image;
	return GetTextBlockWidgetForTable(L, Idx);
}

/** Texture/FontString table (has region markers, no frame handle). */
static bool IsRegionTable(lua_State* L, int Idx)
{
	if (!lua_istable(L, Idx)) return false;
	lua_getfield(L, Idx, "__handle");
	const bool bFrame = lua_isnumber(L, -1) != 0;
	lua_pop(L, 1);
	return !bFrame && TableRepresentsRegion(L, Idx);
}

static float RegionNumberField(lua_State* L, int Idx, const char* Field, float Default)
{
	lua_getfield(L, Idx, Field);
	const float Value = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : Default;
	lua_pop(L, 1);
	return Value;
}

static void ApplyRegionLayout(lua_State* L, int Idx, UWidget* Widget)
{
	UCanvasPanelSlot* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
	if (!Slot) return;
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const float S = (Ctx && Ctx->FrameManager) ? Ctx->FrameManager->GetUIScale() : 1.0f;
	float Width = RegionNumberField(L, Idx, "__width", 0.f);
	float Height = RegionNumberField(L, Idx, "__height", 0.f);
	UTextBlock* Text = Cast<UTextBlock>(Widget);
	// r243: an XML region keeps its authored size when a script re-anchors it (UIDropDownMenu's
	// Left/Middle/Right caps: AceGUI moved them and they fell back to the texture's own size).
	if (Width <= 0.f || Height <= 0.f)
	{
		const FAnchors Current = Slot->GetAnchors();
		const FMargin Cur = Slot->GetOffsets();
		if (!Slot->GetAutoSize() && S > 0.f)
		{
			if (Width <= 0.f && Current.Minimum.X == Current.Maximum.X && Cur.Right > 0.f) Width = Cur.Right / S;
			if (Height <= 0.f && Current.Minimum.Y == Current.Maximum.Y && Cur.Bottom > 0.f) Height = Cur.Bottom / S;
		}
	}

	lua_getfield(L, Idx, "__setAllPoints");
	const bool bAllPoints = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	if (bAllPoints)
	{
		Slot->SetAutoSize(false);
		Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetOffsets(FMargin(0));
		return;
	}

	struct FRegionPoint { EWowAnchorPoint Point; FVector2D Rel; float X; float Y; };
	TArray<FRegionPoint, TInlineAllocator<4>> Points;
	lua_getfield(L, Idx, "__points");
	if (lua_istable(L, -1))
	{
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1))
			{
				FRegionPoint P;
				P.Point = ParseAnchorPoint(lua_tostring(L, -2));
				lua_rawgeti(L, -1, 1); P.Rel.X = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_rawgeti(L, -1, 2); P.Rel.Y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_rawgeti(L, -1, 3); P.X = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_rawgeti(L, -1, 4); P.Y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				Points.Add(P);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	if (Points.Num() == 0)
	{
		// No Lua points: keep the current position, resize only non-stretched axes.
		const FAnchors Current = Slot->GetAnchors();
		const bool bStretchX = Current.Minimum.X != Current.Maximum.X;
		const bool bStretchY = Current.Minimum.Y != Current.Maximum.Y;
		FMargin O = Slot->GetOffsets();
		if (Slot->GetAutoSize())
		{
			const FVector2D Desired = Widget->GetDesiredSize();
			if (!bStretchX) O.Right = Desired.X;
			if (!bStretchY) O.Bottom = Desired.Y;
		}
		bool bChanged = false;
		if (!bStretchX && Width > 0.f) { O.Right = Width * S; bChanged = true; }
		if (!bStretchY && Height > 0.f) { O.Bottom = Height * S; bChanged = true; }
		if (bChanged)
		{
			Slot->SetAutoSize(false);
			Slot->SetOffsets(O);
		}
		return;
	}

	FAnchors Anchors;
	FMargin Offsets;
	FVector2D Alignment(0.f, 0.f);
	bool bNeedsAutoSize = false;
	bool bTextStretchedX = false; // r392
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const FRegionPoint* Low = nullptr; const FRegionPoint* High = nullptr; const FRegionPoint* Mid = nullptr;
		for (const FRegionPoint& P : Points)
		{
			const float F = static_cast<float>(AnchorFraction(P.Point)[Axis]);
			if (F <= 0.f) { if (!Low) Low = &P; }
			else if (F >= 1.f) { if (!High) High = &P; }
			else if (!Mid) Mid = &P;
		}
		// WoW offsets: +x right, +y up. Canvas: +y down.
		auto Offset = [Axis, S](const FRegionPoint& P) { return (Axis == 0 ? P.X : -P.Y) * S; };
		FVector2D::FReal& MinA = Axis == 0 ? Anchors.Minimum.X : Anchors.Minimum.Y;
		FVector2D::FReal& MaxA = Axis == 0 ? Anchors.Maximum.X : Anchors.Maximum.Y;
		float& Lead = Axis == 0 ? Offsets.Left : Offsets.Top;
		float& Trail = Axis == 0 ? Offsets.Right : Offsets.Bottom;
		if (Low && High && FMath::IsNearlyEqual(Low->Rel[Axis], High->Rel[Axis]))
		{
			// r296 (Stefan 13.10, /bt4: check marks never showed): both edges on the same point of the
			// parent (a sibling region's TOPLEFT + BOTTOMRIGHT, as SetAllPoints(checkbg) gives) is not
			// a stretch in UMG - Right/Bottom are then the size, and the inset made it -24 wide.
			MinA = MaxA = Low->Rel[Axis];
			Lead = Offset(*Low);
			Trail = FMath::Max(0.f, Offset(*High) - Offset(*Low));
			Alignment[Axis] = 0.f;
		}
		else if (Low && High)
		{
			MinA = Low->Rel[Axis];
			MaxA = High->Rel[Axis];
			Lead = Offset(*Low);
			Trail = -Offset(*High);
			Alignment[Axis] = 0.f;
			if (Axis == 0) bTextStretchedX = true;
		}
		else
		{
			const FRegionPoint& P = Low ? *Low : (High ? *High : *Mid);
			MinA = MaxA = P.Rel[Axis];
			Alignment[Axis] = AnchorFraction(P.Point)[Axis];
			Lead = Offset(P);
			float Size = (Axis == 0 ? Width : Height) * S;
			if (Size <= 0.f && Text && Axis == 1 && Width > 0.f)
			{
				// Fixed-width text without a height: one line of the current font.
				Size = Text->GetFont().Size * 1.25f * S;
			}
			if (Size <= 0.f) bNeedsAutoSize = true;
			Trail = Size;
		}
	}
	// AutoSize uses the desired size on every non-stretched axis; only use it when
	// a non-stretched axis has no explicit size.
	Slot->SetAutoSize(bNeedsAutoSize);
	Slot->SetAnchors(Anchors);
	Slot->SetAlignment(Alignment);
	Slot->SetOffsets(Offsets);
	// r392 (Stefan 16.10: LibKeyBound's "Binding Mode" help ran far out of its box): a fontstring
	// held between a left and a right point (TOPLEFT 18,-32 + BOTTOMRIGHT -18,48) wraps at the width
	// those points give it, like one with a set width.
	if (Text && (Width > 0.f || bTextStretchedX))
	{
		Text->SetAutoWrapText(true);
	}
}

/** r243: a point on a sibling region (same parent canvas) as a parent fraction plus a pixel offset
 *  (canvas units, y down), read from the sibling's current slot - exact also after the parent
 *  resizes, as long as the sibling keeps its anchoring (UIDropDownMenu Left/Right caps). */
static bool SiblingSlotPoint(UWidget* Rel, UWidget* Self, const FVector2D& Fraction, FVector2D& OutFraction, FVector2D& OutOffset)
{
	UCanvasPanelSlot* RelSlot = Rel ? Cast<UCanvasPanelSlot>(Rel->Slot) : nullptr;
	UCanvasPanelSlot* SelfSlot = Self ? Cast<UCanvasPanelSlot>(Self->Slot) : nullptr;
	if (!RelSlot || !SelfSlot || Rel == Self || RelSlot->Parent != SelfSlot->Parent) return false;
	const FAnchors A = RelSlot->GetAnchors();
	const FMargin O = RelSlot->GetOffsets();
	const FVector2D Align = RelSlot->GetAlignment();
	const FVector2D Desired = Rel->GetDesiredSize();
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const float Min = static_cast<float>(Axis == 0 ? A.Minimum.X : A.Minimum.Y);
		const float Max = static_cast<float>(Axis == 0 ? A.Maximum.X : A.Maximum.Y);
		const float Lead = Axis == 0 ? O.Left : O.Top;
		const float Trail = Axis == 0 ? O.Right : O.Bottom;
		const float F = static_cast<float>(Fraction[Axis]);
		if (Min == Max)
		{
			const float Size = RelSlot->GetAutoSize() ? static_cast<float>(Desired[Axis]) : Trail;
			OutFraction[Axis] = Min;
			OutOffset[Axis] = Lead - static_cast<float>(Align[Axis]) * Size + F * Size;
		}
		else
		{
			OutFraction[Axis] = (1.f - F) * Min + F * Max;
			OutOffset[Axis] = (1.f - F) * Lead - F * Trail;
		}
	}
	return true;
}

/** r380 (Stefan 16.10: the reputation rows' dark name bands sat outside the window, left of the
 *  rows): a point on a region inside a child frame of this region's parent - ReputationBar's
 *  Background is placed TOPRIGHT on ReputationBarLeftTexture, a texture of the row's status bar.
 *  The point inside that child frame (fixed size, as its slot gives it) plus the child frame's own
 *  top-left in the parent canvas, as a parent fraction and a pixel offset like SiblingSlotPoint. */
static bool NestedSlotPoint(UWidget* Rel, UWidget* Self, const FVector2D& Fraction, FVector2D& OutFraction, FVector2D& OutOffset)
{
	UCanvasPanelSlot* RelSlot = Rel ? Cast<UCanvasPanelSlot>(Rel->Slot) : nullptr;
	UCanvasPanelSlot* SelfSlot = Self ? Cast<UCanvasPanelSlot>(Self->Slot) : nullptr;
	if (!RelSlot || !SelfSlot || !SelfSlot->Parent || RelSlot->Parent == SelfSlot->Parent) return false;
	// The child frame's widget: the ancestor of Rel's canvas that sits directly in Self's canvas.
	UWidget* Holder = RelSlot->Parent;
	for (int32 Depth = 0; Holder && Holder->GetParent() != SelfSlot->Parent && Depth < 4; ++Depth) Holder = Holder->GetParent();
	UCanvasPanelSlot* HolderSlot = Holder ? Cast<UCanvasPanelSlot>(Holder->Slot) : nullptr;
	if (!HolderSlot || HolderSlot->Parent != SelfSlot->Parent) return false;
	// r384: also a child frame sized by two anchors or by its content (the rows change type).
	const FAnchors HA = HolderSlot->GetAnchors();
	const FMargin HO = HolderSlot->GetOffsets();
	const FVector2D CanvasPx = SelfSlot->Parent->GetCachedGeometry().GetLocalSize();
	FVector2D HolderSize = HolderSlot->GetAutoSize() ? Holder->GetDesiredSize() : FVector2D(HO.Right, HO.Bottom);
	if (HA.Minimum.X != HA.Maximum.X)
	{
		if (CanvasPx.X <= 0.0) return false;
		HolderSize.X = (HA.Maximum.X - HA.Minimum.X) * CanvasPx.X - HO.Left - HO.Right;
	}
	if (HA.Minimum.Y != HA.Maximum.Y)
	{
		if (CanvasPx.Y <= 0.0) return false;
		HolderSize.Y = (HA.Maximum.Y - HA.Minimum.Y) * CanvasPx.Y - HO.Top - HO.Bottom;
	}
	if (HolderSize.X <= 0.0 || HolderSize.Y <= 0.0) return false;
	// The point inside the child frame, in canvas pixels from its top-left.
	const FAnchors A = RelSlot->GetAnchors();
	const FMargin O = RelSlot->GetOffsets();
	const FVector2D Align = RelSlot->GetAlignment();
	const FVector2D Desired = Rel->GetDesiredSize();
	FVector2D Inner;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const float Min = static_cast<float>(Axis == 0 ? A.Minimum.X : A.Minimum.Y);
		const float Max = static_cast<float>(Axis == 0 ? A.Maximum.X : A.Maximum.Y);
		const float Lead = Axis == 0 ? O.Left : O.Top;
		const float Trail = Axis == 0 ? O.Right : O.Bottom;
		const float Box = static_cast<float>(HolderSize[Axis]);
		const float F = static_cast<float>(Fraction[Axis]);
		if (Min == Max)
		{
			const float Size = RelSlot->GetAutoSize() ? static_cast<float>(Desired[Axis]) : Trail;
			Inner[Axis] = Min * Box + Lead - static_cast<float>(Align[Axis]) * Size + F * Size;
		}
		else
		{
			const float Start = Min * Box + Lead, End = Max * Box - Trail;
			Inner[Axis] = Start + F * (End - Start);
		}
	}
	FVector2D HolderFraction, HolderOffset;
	if (!SiblingSlotPoint(Holder, Self, FVector2D::ZeroVector, HolderFraction, HolderOffset)) return false;
	OutFraction = HolderFraction;
	OutOffset = HolderOffset + Inner;
	return true;
}

/** Relative point of `relativeTo` expressed as a fraction of the region's parent. */
static FVector2D ResolveRegionRelativePoint(lua_State* L, int RegionIdx, int RelIdx, EWowAnchorPoint RelPoint)
{
	const FVector2D Fraction = AnchorFraction(RelPoint);
	if (RelIdx <= 0 || !lua_istable(L, RelIdx)) return Fraction;
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return Fraction;

	lua_getfield(L, RegionIdx, "__parentHandle");
	const int64 ParentHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	lua_getfield(L, RelIdx, "__handle");
	const int64 RelHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	if (RelHandle >= 0 && RelHandle == ParentHandle) return Fraction;

	// Sibling region with a single parent-relative point and a known size: resolve
	// its rect inside the parent so "text LEFT of icon RIGHT" lands next to the icon.
	const FWowFrameDef* ParentDef = ParentHandle >= 0 ? Ctx->FrameManager->GetFrameDef(ParentHandle) : nullptr;
	if (RelHandle < 0 && ParentDef && ParentDef->Width > 0.f && ParentDef->Height > 0.f)
	{
		lua_getfield(L, RelIdx, "__parentHandle");
		const int64 RelParent = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
		lua_pop(L, 1);
		const float W = RegionNumberField(L, RelIdx, "__width", 0.f);
		const float H = RegionNumberField(L, RelIdx, "__height", 0.f);
		if (RelParent == ParentHandle && W > 0.f && H > 0.f)
		{
			lua_getfield(L, RelIdx, "__points");
			if (lua_istable(L, -1))
			{
				lua_pushnil(L);
				if (lua_next(L, -2) != 0)
				{
					FVector2D Result = Fraction;
					if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1))
					{
						const FVector2D Own = AnchorFraction(ParseAnchorPoint(lua_tostring(L, -2)));
						lua_rawgeti(L, -1, 1); const float RX = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_rawgeti(L, -1, 2); const float RY = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_rawgeti(L, -1, 3); const float OX = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_rawgeti(L, -1, 4); const float OY = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						// Top-left of the sibling in parent units (y down).
						const float Left = RX * ParentDef->Width + OX - Own.X * W;
						const float Top = RY * ParentDef->Height - OY - Own.Y * H;
						Result.X = (Left + Fraction.X * W) / ParentDef->Width;
						Result.Y = (Top + Fraction.Y * H) / ParentDef->Height;
					}
					lua_pop(L, 3); // value, key, __points
					return Result;
				}
			}
			lua_pop(L, 1);
		}
	}
	return Fraction;
}

// r349 (Stefan 15.10, /bt4: dropdowns showed only their end caps, heading lines ran through their
// text): a region anchored to another region was placed from that region's slot at the moment of the
// call. AceGUI clears and re-anchors in an order where the other region has not moved yet
// (DropDown: middle:SetPoint("RIGHT", right, "LEFT") before right:SetPoint("TOPRIGHT", ...)), and a
// heading's label only gets its text (and width) later. Like the client, a point on a region now
// follows that region: each region remembers who is anchored to it and re-applies their points when
// it moves, resizes or changes its text.
// rel.__dependents = weak set of dependent regions; dep.__regionRefs[point] = { rel, relPoint, x, y }
static void RegisterRegionDependent(lua_State* L, int SelfIdx, int RelIdx, const char* Point, const char* RelPoint, float X, float Y)
{
	lua_getfield(L, RelIdx, "__dependents");
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_newtable(L);
		lua_pushstring(L, "k");
		lua_setfield(L, -2, "__mode");
		lua_setmetatable(L, -2);
		lua_pushvalue(L, -1);
		lua_setfield(L, RelIdx, "__dependents");
	}
	lua_pushvalue(L, SelfIdx);
	lua_pushboolean(L, 1);
	lua_settable(L, -3);
	lua_pop(L, 1);
	lua_getfield(L, SelfIdx, "__regionRefs");
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, SelfIdx, "__regionRefs");
	}
	lua_newtable(L);
	lua_pushvalue(L, RelIdx); lua_rawseti(L, -2, 1);
	lua_pushstring(L, RelPoint); lua_rawseti(L, -2, 2);
	lua_pushnumber(L, X); lua_rawseti(L, -2, 3);
	lua_pushnumber(L, Y); lua_rawseti(L, -2, 4);
	lua_setfield(L, -2, Point);
	lua_pop(L, 1);
}

static void ForgetRegionRef(lua_State* L, int SelfIdx, const char* Point)
{
	lua_getfield(L, SelfIdx, "__regionRefs");
	if (lua_istable(L, -1))
	{
		lua_pushnil(L);
		lua_setfield(L, -2, Point);
	}
	lua_pop(L, 1);
}

static int GWowRegionReplayDepth = 0;

/** Re-apply the points of every region anchored to the region at SelfIdx. */
static void ReplayRegionDependents(lua_State* L, int SelfIdx)
{
	if (!lua_istable(L, SelfIdx) || GWowRegionReplayDepth >= 4) return;
	lua_getfield(L, SelfIdx, "__dependents");
	if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
	const int DepsIdx = lua_gettop(L);
	lua_newtable(L);
	const int ListIdx = lua_gettop(L);
	int Count = 0;
	lua_pushnil(L);
	while (lua_next(L, DepsIdx) != 0)
	{
		lua_pop(L, 1);
		lua_pushvalue(L, -1);
		lua_rawseti(L, ListIdx, ++Count);
	}
	static const char* const Points[] = { "TOPLEFT", "TOP", "TOPRIGHT", "LEFT", "CENTER", "RIGHT", "BOTTOMLEFT", "BOTTOM", "BOTTOMRIGHT" };
	++GWowRegionReplayDepth;
	for (int Index = 1; Index <= Count; ++Index)
	{
		lua_rawgeti(L, ListIdx, Index);
		const int DepIdx = lua_gettop(L);
		lua_getfield(L, DepIdx, "__regionRefs");
		const int RefsIdx = lua_gettop(L);
		if (lua_istable(L, RefsIdx))
		{
			for (const char* Point : Points)
			{
				lua_getfield(L, RefsIdx, Point);
				const int RefIdx = lua_gettop(L);
				bool bMine = false;
				if (lua_istable(L, RefIdx))
				{
					lua_rawgeti(L, RefIdx, 1);
					bMine = lua_rawequal(L, -1, SelfIdx) != 0;
					lua_pop(L, 1);
				}
				if (bMine)
				{
					lua_getfield(L, DepIdx, "SetPoint");
					if (lua_isfunction(L, -1))
					{
						lua_pushvalue(L, DepIdx);
						lua_pushstring(L, Point);
						lua_rawgeti(L, RefIdx, 1);
						lua_rawgeti(L, RefIdx, 2);
						lua_rawgeti(L, RefIdx, 3);
						lua_rawgeti(L, RefIdx, 4);
						if (lua_pcall(L, 6, 0, 0) != 0) lua_pop(L, 1);
					}
					else
					{
						lua_pop(L, 1);
					}
				}
				lua_settop(L, RefIdx - 1);
			}
		}
		lua_settop(L, DepIdx - 1);
	}
	--GWowRegionReplayDepth;
	lua_settop(L, DepsIdx - 1);
}

/** The name frames use to anchor to a region: its own, else one given to it on first use. */
static FString RegionAnchorName(lua_State* L, int Idx, bool bCreate)
{
	lua_getfield(L, Idx, "__name");
	FString Name = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
	lua_pop(L, 1);
	if (!Name.IsEmpty()) return Name;
	lua_getfield(L, Idx, "__anchorName");
	Name = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
	lua_pop(L, 1);
	if (!Name.IsEmpty() || !bCreate) return Name;
	static int32 GWowRegionAnchorCounter = 0;
	Name = FString::Printf(TEXT("__WowRegionAnchor%d"), ++GWowRegionAnchorCounter);
	lua_pushstring(L, TCHAR_TO_UTF8(*Name));
	lua_setfield(L, Idx, "__anchorName");
	return Name;
}

static UWidget* GetRegionWidget(lua_State* L, int Idx);

/** After a region moved, resized or changed its text: frames and regions anchored to it follow. */
static void AfterRegionChanged(lua_State* L, int Idx)
{
	if (!lua_istable(L, Idx)) return;
	lua_getfield(L, Idx, "__dependents");
	const bool bHasDependents = lua_istable(L, -1);
	lua_pop(L, 1);
	// Only the name given for a frame's anchor (r349): XML regions with names are followed through
	// SetPoint as before; walking every frame on each SetText of a named fontstring is too costly.
	lua_getfield(L, Idx, "__anchorName");
	FString Name = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
	lua_pop(L, 1);
	// r380: a named XML region an XML frame is anchored to (ReputationDetailAtWarCheckBoxText, whose
	// text OnLoad sets) moves that frame too.
	if (Name.IsEmpty())
	{
		lua_getfield(L, Idx, "__name");
		const FString OwnName = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
		lua_pop(L, 1);
		FWowLuaContext* NameCtx = OwnName.IsEmpty() ? nullptr : WowLuaApi::GetContext(L);
		if (NameCtx && NameCtx->FrameManager && NameCtx->FrameManager->IsFrameAnchorRegion(OwnName)) Name = OwnName;
	}
	if (!bHasDependents && Name.IsEmpty()) return;
	UWidget* Widget = GetRegionWidget(L, Idx);
	if (Widget && Cast<UTextBlock>(Widget)) Widget->ForceLayoutPrepass(); // the new text's width
	if (bHasDependents) ReplayRegionDependents(L, Idx);
	lua_getfield(L, Idx, "__parentHandle");
	const int64 ParentHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	if (!Name.IsEmpty() && Widget && ParentHandle >= 0)
		if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager)
			Ctx->FrameManager->NoteRegionMoved(Name, Widget, ParentHandle);
}

/** region:SetPoint(point [, relativeTo [, relativePoint]] [, x, y]) */
static int LR_SetPoint(lua_State* L, UWidget* Widget)
{
	const char* PointStr = luaL_checkstring(L, 2);
	int RelIdx = 0;
	const char* RelPointStr = PointStr;
	float X = 0.f, Y = 0.f;
	if (lua_istable(L, 3) || (lua_isstring(L, 3) && !lua_isnumber(L, 3) && !IsAnchorPointName(lua_tostring(L, 3))))
	{
		if (lua_istable(L, 3)) RelIdx = 3;
		else
		{
			lua_getglobal(L, lua_tostring(L, 3));
			lua_replace(L, 3);
			if (lua_istable(L, 3)) RelIdx = 3;
		}
		if (lua_isstring(L, 4) && !lua_isnumber(L, 4)) { RelPointStr = lua_tostring(L, 4); X = (float)luaL_optnumber(L, 5, 0); Y = (float)luaL_optnumber(L, 6, 0); }
		else { X = (float)luaL_optnumber(L, 4, 0); Y = (float)luaL_optnumber(L, 5, 0); }
	}
	else if (lua_isstring(L, 3) && !lua_isnumber(L, 3))
	{
		RelPointStr = lua_tostring(L, 3);
		X = (float)luaL_optnumber(L, 4, 0); Y = (float)luaL_optnumber(L, 5, 0);
	}
	else if (lua_isnumber(L, 3))
	{
		X = (float)lua_tonumber(L, 3); Y = (float)luaL_optnumber(L, 4, 0);
	}
	else
	{
		// SetPoint("POINT", nil, "REL", x, y)
		if (lua_isstring(L, 4) && !lua_isnumber(L, 4)) { RelPointStr = lua_tostring(L, 4); X = (float)luaL_optnumber(L, 5, 0); Y = (float)luaL_optnumber(L, 6, 0); }
		else { X = (float)luaL_optnumber(L, 4, 0); Y = (float)luaL_optnumber(L, 5, 0); }
	}
	FVector2D Rel = ResolveRegionRelativePoint(L, 1, RelIdx, ParseAnchorPoint(RelPointStr));
	// r349: remember the point on another region so it follows that region later.
	if (RelIdx > 0 && IsRegionTable(L, RelIdx))
		RegisterRegionDependent(L, 1, RelIdx, AnchorPointToString(ParseAnchorPoint(PointStr)), AnchorPointToString(ParseAnchorPoint(RelPointStr)), X, Y);
	else
		ForgetRegionRef(L, 1, AnchorPointToString(ParseAnchorPoint(PointStr)));
	// r243: a sibling region (XML or Lua) is resolved from its live slot: fraction + offset.
	bool bRegionPointResolved = false;
	if (RelIdx > 0 && IsRegionTable(L, RelIdx))
	{
		FWowLuaContext* SibCtx = WowLuaApi::GetContext(L);
		const float S = (SibCtx && SibCtx->FrameManager) ? FMath::Max(0.01f, SibCtx->FrameManager->GetUIScale()) : 1.f;
		FVector2D SibFraction, SibOffset;
		if (SiblingSlotPoint(GetRegionWidget(L, RelIdx), Widget, AnchorFraction(ParseAnchorPoint(RelPointStr)), SibFraction, SibOffset)
			|| NestedSlotPoint(GetRegionWidget(L, RelIdx), Widget, AnchorFraction(ParseAnchorPoint(RelPointStr)), SibFraction, SibOffset))
		{
			bRegionPointResolved = true;
			Rel = SibFraction;
			X += static_cast<float>(SibOffset.X) / S;
			Y -= static_cast<float>(SibOffset.Y) / S;
		}
	}

	// r384 (Stefan 16.10: after moving factions in and out of Inactive one row's name band shrank to a
	// line at the row's left): a point on a region of another frame that cannot be worked out right
	// now (its frame not laid out yet) keeps the point it had instead of falling back to the parent.
	if (RelIdx > 0 && IsRegionTable(L, RelIdx) && !bRegionPointResolved)
	{
		lua_getfield(L, RelIdx, "__parentHandle");
		const int64 RelOwner = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
		lua_pop(L, 1);
		lua_getfield(L, 1, "__parentHandle");
		const int64 SelfOwner = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
		lua_pop(L, 1);
		if (RelOwner >= 0 && SelfOwner >= 0 && RelOwner != SelfOwner)
		{
			lua_getfield(L, 1, "__points");
			bool bHadPoint = false;
			if (lua_istable(L, -1))
			{
				lua_getfield(L, -1, AnchorPointToString(ParseAnchorPoint(PointStr)));
				bHadPoint = lua_istable(L, -1);
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
			if (bHadPoint) return 0;
		}
	}
	lua_pushboolean(L, 0);
	lua_setfield(L, 1, "__setAllPoints");
	lua_getfield(L, 1, "__points");
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, 1, "__points");
	}
	lua_newtable(L);
	lua_pushnumber(L, Rel.X); lua_rawseti(L, -2, 1);
	lua_pushnumber(L, Rel.Y); lua_rawseti(L, -2, 2);
	lua_pushnumber(L, X); lua_rawseti(L, -2, 3);
	lua_pushnumber(L, Y); lua_rawseti(L, -2, 4);
	lua_pushstring(L, AnchorPointToString(ParseAnchorPoint(PointStr)));
	lua_insert(L, -2);
	lua_settable(L, -3);
	lua_pop(L, 1);
	// Legacy fields read by SetTexture when it builds the image late.
	lua_pushnumber(L, X); lua_setfield(L, 1, "__posX");
	lua_pushnumber(L, -Y); lua_setfield(L, 1, "__posY");
	// XML-declared regions are laid out by the frame manager. Only move them here
	// when the anchor is their own parent; an anchor to another frame would need
	// that frame's live rect, so their authored layout is kept instead.
	lua_getfield(L, 1, "__parentHandle");
	const bool bDynamic = lua_isnumber(L, -1) != 0;
	const int64 ParentHandle = bDynamic ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);

	// r228 (Stefan 11.10: AceGUI / dialog text landed on the parent's edge instead of next to the
	// frame it names): a point on a frame other than the parent is resolved by the frame manager
	// from that frame's live rect, and again whenever that frame (or the parent) moves or resizes.
	// __dynpoints[point] = { relativeFrameName ("" = parent), relativePoint, x, y, onRegion }
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 RelHandle = -1;
	if (RelIdx > 0)
	{
		lua_getfield(L, RelIdx, "__handle");
		RelHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
		lua_pop(L, 1);
	}
	const bool bOnRegion = RelIdx > 0 && RelHandle < 0;
	FString RelName;
	if (RelHandle >= 0 && RelHandle != ParentHandle && Ctx && Ctx->FrameManager)
		if (const FWowFrameDef* RelDef = Ctx->FrameManager->GetFrameDef(RelHandle)) RelName = RelDef->Name;
	lua_getfield(L, 1, "__dynpoints");
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, 1, "__dynpoints");
	}
	lua_newtable(L);
	lua_pushstring(L, TCHAR_TO_UTF8(*RelName)); lua_rawseti(L, -2, 1);
	lua_pushstring(L, AnchorPointToString(ParseAnchorPoint(RelPointStr))); lua_rawseti(L, -2, 2);
	lua_pushnumber(L, X); lua_rawseti(L, -2, 3);
	lua_pushnumber(L, Y); lua_rawseti(L, -2, 4);
	lua_pushboolean(L, bOnRegion ? 1 : 0); lua_rawseti(L, -2, 5);
	lua_setfield(L, -2, AnchorPointToString(ParseAnchorPoint(PointStr)));
	TArray<FWowAnchor> DynAnchors;
	bool bAnyFrame = false, bAnyRegion = false;
	lua_pushnil(L);
	while (lua_next(L, -2) != 0)
	{
		if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1))
		{
			FWowAnchor Anchor;
			Anchor.Point = ParseAnchorPoint(lua_tostring(L, -2));
			lua_rawgeti(L, -1, 1); Anchor.RelativeTo = UTF8_TO_TCHAR(lua_isstring(L, -1) ? lua_tostring(L, -1) : ""); lua_pop(L, 1);
			lua_rawgeti(L, -1, 2); Anchor.RelativePoint = ParseAnchorPoint(lua_isstring(L, -1) ? lua_tostring(L, -1) : "TOPLEFT"); lua_pop(L, 1);
			lua_rawgeti(L, -1, 3); Anchor.OffsetX = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
			lua_rawgeti(L, -1, 4); Anchor.OffsetY = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
			lua_rawgeti(L, -1, 5); bAnyRegion |= lua_toboolean(L, -1) != 0; lua_pop(L, 1);
			bAnyFrame |= !Anchor.RelativeTo.IsEmpty();
			DynAnchors.Add(Anchor);
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1); // __dynpoints
	// r243: XML regions carry their authored size in the slot, not in __width/__height.
	auto SizeOrSlot = [&](const char* Field, bool bWidth) -> float
	{
		const float V = RegionNumberField(L, 1, Field, 0.f);
		if (V > 0.f || !Widget || !Ctx || !Ctx->FrameManager) return V;
		const UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot);
		if (!Slot || Slot->GetAutoSize()) return V;
		const FAnchors A = Slot->GetAnchors();
		if (bWidth ? A.Minimum.X != A.Maximum.X : A.Minimum.Y != A.Maximum.Y) return V;
		const float Px = bWidth ? Slot->GetOffsets().Right : Slot->GetOffsets().Bottom;
		return Px > 0.f ? Px / FMath::Max(0.01f, Ctx->FrameManager->GetUIScale()) : V;
	};
	// r243: frames anchored to a named region (UIDropDownMenu's button on $parentRight) follow it.
	auto NoteMoved = [&]()
	{
		if (!Ctx || !Ctx->FrameManager || !bDynamic) return;
		const FString AnchorName = RegionAnchorName(L, 1, false); // r349: also an unnamed region frames anchor to
		if (!AnchorName.IsEmpty()) Ctx->FrameManager->NoteRegionMoved(AnchorName, Widget, ParentHandle);
	};
	if (bDynamic && bAnyFrame && !bAnyRegion && Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetDynamicRegionAnchors(Widget, ParentHandle, DynAnchors,
			SizeOrSlot("__width", true), SizeOrSlot("__height", false));
		NoteMoved();
		return 0;
	}
	if (Ctx && Ctx->FrameManager) Ctx->FrameManager->ClearDynamicRegionAnchors(Widget);
	if (bDynamic || RelIdx == 0)
	{
		ApplyRegionLayout(L, 1, Widget);
		NoteMoved();
	}
	return 0;
}

static int LR_ClearAllPoints(lua_State* L, UWidget* Widget)
{
	lua_pushnil(L); lua_setfield(L, 1, "__dynpoints"); // r228
	lua_pushnil(L); lua_setfield(L, 1, "__regionRefs"); // r349
	if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager) Ctx->FrameManager->ClearDynamicRegionAnchors(Widget);
	lua_pushnil(L); lua_setfield(L, 1, "__points");
	lua_pushboolean(L, 0); lua_setfield(L, 1, "__setAllPoints");
	if (UCanvasPanelSlot* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
	{
		Slot->SetAnchors(FAnchors(0.f, 0.f));
		Slot->SetAlignment(FVector2D::ZeroVector);
		const FMargin O = Slot->GetOffsets();
		Slot->SetOffsets(FMargin(0.f, 0.f, O.Right, O.Bottom));
	}
	return 0;
}

static int LR_SetAllPoints(lua_State* L, UWidget* Widget)
{
	// r281 (Stefan 13.10, /bt4): SetAllPoints(otherRegion) covers that region, not the whole frame
	// (AceGUI's check mark and highlight are SetAllPoints(checkbg) and filled the entire row).
	if (lua_istable(L, 2) && IsRegionTable(L, 2))
	{
		lua_settop(L, 2);
		lua_getfield(L, 1, "ClearAllPoints");
		if (lua_isfunction(L, -1)) { lua_pushvalue(L, 1); lua_call(L, 1, 0); } else lua_pop(L, 1);
		for (const char* Point : {"TOPLEFT", "BOTTOMRIGHT"})
		{
			lua_getfield(L, 1, "SetPoint");
			if (!lua_isfunction(L, -1)) { lua_pop(L, 1); break; }
			lua_pushvalue(L, 1); lua_pushstring(L, Point); lua_pushvalue(L, 2); lua_pushstring(L, Point);
			lua_pushnumber(L, 0); lua_pushnumber(L, 0);
			lua_call(L, 6, 0);
		}
		return 0;
	}
	lua_pushnil(L); lua_setfield(L, 1, "__dynpoints"); // r228
	if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager) Ctx->FrameManager->ClearDynamicRegionAnchors(Widget);
	lua_pushnil(L); lua_setfield(L, 1, "__points");
	lua_pushboolean(L, 1); lua_setfield(L, 1, "__setAllPoints");
	ApplyRegionLayout(L, 1, Widget);
	return 0;
}

static int LR_SetSize(lua_State* L, UWidget* Widget, bool bWidth, bool bHeight)
{
	int Arg = 2;
	if (bWidth) { lua_pushnumber(L, luaL_checknumber(L, Arg++)); lua_setfield(L, 1, "__width"); }
	if (bHeight) { lua_pushnumber(L, luaL_checknumber(L, Arg)); lua_setfield(L, 1, "__height"); }
	// r228: a region placed against another frame keeps that placement at its new size.
	if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager
		&& Ctx->FrameManager->UpdateDynamicRegionSize(Widget, RegionNumberField(L, 1, "__width", 0.f), RegionNumberField(L, 1, "__height", 0.f)))
		return 0;
	ApplyRegionLayout(L, 1, Widget);
	return 0;
}

/** r363 (Stefan 15.10, /bt4: "a lot of air between each option", texts a bit off): the size of a
 *  font string's text in canvas pixels, measured with its font like the client does - wrapped at
 *  WrapPx when that is > 0. GetStringWidth guessed half the font size per character (in pixels,
 *  not UI units) and a font string without an explicit size reported 0 x 0 until Slate's next
 *  prepass, so AceGUI sized labels, check boxes and the window title from wrong numbers. */
namespace WowTextMeasure
{
	static bool MeasurePx(UTextBlock* Text, float WrapPx, float& OutW, float& OutH)
	{
		if (!Text || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer()) return false;
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo Font = Text->GetFont();
		const float LineHeight = static_cast<float>(Measure->GetMaxCharacterHeight(Font));
		const FString All = Text->GetText().ToString();
		TArray<FString> Lines;
		All.ParseIntoArray(Lines, TEXT("\n"), false);
		if (Lines.IsEmpty()) Lines.Add(FString());
		float MaxWidth = 0.f;
		int32 LineCount = 0;
		for (const FString& Line : Lines)
		{
			const float LineWidth = static_cast<float>(Measure->Measure(Line, Font).X);
			if (WrapPx <= 0.f || LineWidth <= WrapPx)
			{
				MaxWidth = FMath::Max(MaxWidth, LineWidth);
				++LineCount;
				continue;
			}
			// Greedy word wrap, like the client's font strings.
			TArray<FString> Words;
			Line.ParseIntoArray(Words, TEXT(" "), true);
			FString Current;
			for (const FString& Word : Words)
			{
				const FString Candidate = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
				if (!Current.IsEmpty() && Measure->Measure(Candidate, Font).X > WrapPx)
				{
					MaxWidth = FMath::Max(MaxWidth, static_cast<float>(Measure->Measure(Current, Font).X));
					++LineCount;
					Current = Word;
				}
				else
				{
					Current = Candidate;
				}
			}
			MaxWidth = FMath::Max(MaxWidth, FMath::Min(WrapPx, static_cast<float>(Measure->Measure(Current, Font).X)));
			++LineCount;
		}
		OutW = MaxWidth;
		OutH = LineHeight * FMath::Max(1, LineCount);
		return true;
	}
}

static int LR_GetSize(lua_State* L, UWidget* Widget, bool bWidth)
{
	float Value = RegionNumberField(L, 1, bWidth ? "__width" : "__height", 0.f);
	// r363: a font string without that explicit size reports its text's size (wrapped at its width).
	if (Value <= 0.f)
	{
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			FWowLuaContext* MeasureCtx = WowLuaApi::GetContext(L);
			const float MeasureScale = (MeasureCtx && MeasureCtx->FrameManager) ? FMath::Max(0.01f, MeasureCtx->FrameManager->GetUIScale()) : 1.0f;
			float MeasuredW = 0.f, MeasuredH = 0.f;
			const float WrapUnits = bWidth ? 0.f : RegionNumberField(L, 1, "__width", 0.f);
			if (WowTextMeasure::MeasurePx(Text, WrapUnits * MeasureScale, MeasuredW, MeasuredH))
			{
				const float Measured = (bWidth ? MeasuredW : MeasuredH) / MeasureScale;
				if (Measured > 0.f || !bWidth)
				{
					lua_pushnumber(L, Text->GetText().IsEmpty() && bWidth ? 0.f : Measured);
					return 1;
				}
			}
		}
	}
	if (Value <= 0.f && Widget)
	{
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		const float S = (Ctx && Ctx->FrameManager) ? FMath::Max(0.01f, Ctx->FrameManager->GetUIScale()) : 1.0f;
		const FVector2D Desired = Widget->GetDesiredSize();
		Value = (bWidth ? Desired.X : Desired.Y);
		if (Value <= 0.f)
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				const FAnchors A = Slot->GetAnchors();
				const bool bStretch = bWidth ? A.Minimum.X != A.Maximum.X : A.Minimum.Y != A.Maximum.Y;
				if (!bStretch && !Slot->GetAutoSize()) Value = (bWidth ? Slot->GetOffsets().Right : Slot->GetOffsets().Bottom) / S;
			}
		}
		else
		{
			Value /= S;
		}
	}
	lua_pushnumber(L, Value);
	return 1;
}

static int LF_SetPoint(lua_State* L);
static int LF_SetSize(lua_State* L);
static int LF_SetWidth(lua_State* L);
static int LF_SetHeight(lua_State* L);
static int LF_FontStringSetFormattedText(lua_State* L);
// r349: the region methods that move or resize a region, then let its dependents follow.
static int LF_SetPointTracked(lua_State* L) { const int R = LF_SetPoint(L); if (IsRegionTable(L, 1)) AfterRegionChanged(L, 1); return R; }
// r350: a named region resized from Lua (a tab's Middle in PanelTemplates_TabResize): the XML regions
// anchored to it follow (FWowFrameManager::NoteRegionMoved).
static void AfterNamedRegionResized(lua_State* L, int Idx)
{
	lua_getfield(L, Idx, "__name");
	const FString Name = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
	lua_pop(L, 1);
	lua_getfield(L, Idx, "__parentHandle");
	const int64 ParentHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	UWidget* Widget = Name.IsEmpty() || ParentHandle < 0 ? nullptr : GetRegionWidget(L, Idx);
	if (!Widget) return;
	if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager)
		Ctx->FrameManager->NoteRegionMoved(Name, Widget, ParentHandle);
}
static int LF_SetSizeTracked(lua_State* L) { const int R = LF_SetSize(L); if (IsRegionTable(L, 1)) { AfterRegionChanged(L, 1); AfterNamedRegionResized(L, 1); } return R; }
static int LF_SetWidthTracked(lua_State* L) { const int R = LF_SetWidth(L); if (IsRegionTable(L, 1)) { AfterRegionChanged(L, 1); AfterNamedRegionResized(L, 1); } return R; }
static int LF_SetHeightTracked(lua_State* L) { const int R = LF_SetHeight(L); if (IsRegionTable(L, 1)) { AfterRegionChanged(L, 1); AfterNamedRegionResized(L, 1); } return R; }
static int LF_SetTextTracked(lua_State* L) { const int R = LF_SetText(L); if (IsRegionTable(L, 1)) AfterRegionChanged(L, 1); return R; }
static int LF_SetFormattedTextTracked(lua_State* L) { const int R = LF_FontStringSetFormattedText(L); if (IsRegionTable(L, 1)) AfterRegionChanged(L, 1); return R; }

// frame:SetPoint(point, relativeTo, relativePoint, ofsx, ofsy)
static int LF_SetPoint(lua_State* L)
{
	if (UpdateUnitCastSparkRegion(L)) return 0;
	if (IsRegionTable(L, 1)) return LR_SetPoint(L, GetRegionWidget(L, 1));
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	const char* PointStr = luaL_checkstring(L, 2);
	FWowAnchor Anchor;
	Anchor.Point = ParseAnchorPoint(PointStr);

	// arg3 = relativeTo (frame table, frame name string, or nil)
	if (lua_istable(L, 3) && IsRegionTable(L, 3))
	{
		// r349 (Stefan 15.10, /bt4: sliders sat a row too low, under the next label): a point on a
		// texture/fontstring without a name (AceGUI: slider:SetPoint("TOP", label, "BOTTOM")) became a
		// point on the parent frame. The region gets a name of its own and its rect is registered, so
		// the frame is placed against it and follows it (FWowFrameManager::NoteRegionMoved).
		Anchor.RelativeTo = RegionAnchorName(L, 3, true);
		lua_getfield(L, 3, "__parentHandle");
		const int64 RegionParent = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
		lua_pop(L, 1);
		if (UWidget* RegionWidget = GetRegionWidget(L, 3); RegionWidget && RegionParent >= 0)
			Ctx->FrameManager->NoteRegionMoved(Anchor.RelativeTo, RegionWidget, RegionParent);
		Anchor.RelativePoint = ParseAnchorPoint(luaL_optstring(L, 4, PointStr));
		Anchor.OffsetX = static_cast<float>(luaL_optnumber(L, 5, 0));
		Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 6, 0));
	}
	else if (lua_istable(L, 3))
	{
		lua_getfield(L, 3, "__name");
		if (lua_isstring(L, -1))
			Anchor.RelativeTo = UTF8_TO_TCHAR(lua_tostring(L, -1));
		lua_pop(L, 1);

		// When relativeTo is a frame, arg4 = relativePoint, arg5 = x, arg6 = y
		Anchor.RelativePoint = ParseAnchorPoint(luaL_optstring(L, 4, PointStr));
		Anchor.OffsetX = static_cast<float>(luaL_optnumber(L, 5, 0));
		Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 6, 0));
	}
	else if (lua_type(L, 3) == LUA_TSTRING)
	{
		// lua_isstring() is also true for numbers: SetPoint("BOTTOMLEFT", 14, 10) used to
		// become relativeTo "14" with no offsets (Bartender's unlock dialog, many addons).
		// Could be relativeTo name or relativePoint if no frame given
		const char* Arg3 = lua_tostring(L, 3);
		// Check if arg3 looks like an anchor point name
		if (IsAnchorPointName(Arg3))
		{
			// SetPoint("POINT", "RELATIVEPOINT", x, y) — no relativeTo frame
			Anchor.RelativePoint = ParseAnchorPoint(Arg3);
			Anchor.OffsetX = static_cast<float>(luaL_optnumber(L, 4, 0));
			Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 5, 0));
		}
		else
		{
			// SetPoint("POINT", "frameName", "RELATIVEPOINT", x, y)
			Anchor.RelativeTo = UTF8_TO_TCHAR(Arg3);
			Anchor.RelativePoint = ParseAnchorPoint(luaL_optstring(L, 4, PointStr));
			Anchor.OffsetX = static_cast<float>(luaL_optnumber(L, 5, 0));
			Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 6, 0));
		}
	}
	else
	{
		// SetPoint("POINT", nil, "RELPOINT", x, y), SetPoint("POINT", nil, nil, x, y) or SetPoint("POINT", x, y)
		Anchor.RelativePoint = lua_type(L, 4) == LUA_TSTRING ? ParseAnchorPoint(lua_tostring(L, 4)) : Anchor.Point;
		if (lua_isnumber(L, 3))
		{
			Anchor.OffsetX = static_cast<float>(lua_tonumber(L, 3));
			Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 4, 0));
		}
		else
		{
			Anchor.OffsetX = static_cast<float>(luaL_optnumber(L, 5, 0));
			Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 6, 0));
		}
	}

	// Replace existing anchor with same point, or append if new
	FWowFrameDef* Def = Ctx->FrameManager->GetMutableFrameDef(Handle);
	if (Def)
	{
		bool bReplaced = false;
		for (int32 i = 0; i < Def->Anchors.Num(); i++)
		{
			if (Def->Anchors[i].Point == Anchor.Point)
			{
				Def->Anchors[i] = Anchor;
				bReplaced = true;
				break;
			}
		}
		if (!bReplaced)
		{
			Def->Anchors.Add(Anchor);
		}
		Ctx->FrameManager->SetFrameAnchors(Handle, Def->Anchors);
	}
	return 0;
}

// frame:ClearAllPoints()
static int LF_ClearAllPoints(lua_State* L)
{
	if (IsRegionTable(L, 1)) return LR_ClearAllPoints(L, GetRegionWidget(L, 1));
    if (UImage* Image = GetTextureImage(L))
    {
        if (auto* Slot = Cast<UCanvasPanelSlot>(Image->Slot))
        {
            Slot->SetAnchors(FAnchors(0.f,0.f));
            Slot->SetOffsets(FMargin(0));
        }
        return 0;
    }
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->ClearFrameAnchors(Handle);
	}
	return 0;
}

// frame:SetAllPoints(relativeTo)
// r242: SetAllPoints(other) on a frame or region that is not the parent: the original anchors
// TOPLEFT and BOTTOMRIGHT to the other object's corners. AceGUI's check box does
// check:SetAllPoints(checkbg); filling the parent instead stretched the tick across the widget.
static bool SetAllPointsToOther(lua_State* L)
{
	if (!lua_istable(L, 2) || lua_rawequal(L, 1, 2)) return false;
	// The parent frame (or nothing) keeps the plain fill-the-parent path.
	lua_getfield(L, 1, "__parentHandle");
	const int64 ParentHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	lua_getfield(L, 2, "__handle");
	const int64 RelHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	if (RelHandle >= 0 && RelHandle == ParentHandle) return false;
	// r296 (Stefan 13.10, /bt4: the title sat below its plate): a frame on a texture/font string
	// (AceGUI's title bar: title:SetAllPoints(titlebg)) takes that region's place in the region's
	// frame - one point plus the region's size, read from its slot. Stretched regions keep the fill.
	if (ParentHandle < 0 && RelHandle < 0)
	{
		FWowLuaContext* RegionCtx = WowLuaApi::GetContext(L);
		UWidget* RegionWidget = IsRegionTable(L, 2) ? GetRegionWidget(L, 2) : nullptr;
		UCanvasPanelSlot* RegionSlot = RegionWidget ? Cast<UCanvasPanelSlot>(RegionWidget->Slot) : nullptr;
		if (!RegionCtx || !RegionCtx->FrameManager || !RegionSlot) return false;
		const FAnchors RA = RegionSlot->GetAnchors();
		if (RA.Minimum.X != RA.Maximum.X || RA.Minimum.Y != RA.Maximum.Y) return false;
		auto PointName = [](double FX, double FY) -> const char*
		{
			const int32 X = FMath::IsNearlyEqual(FX, 0.0) ? 0 : (FMath::IsNearlyEqual(FX, 1.0) ? 2 : (FMath::IsNearlyEqual(FX, 0.5) ? 1 : -1));
			const int32 Y = FMath::IsNearlyEqual(FY, 0.0) ? 0 : (FMath::IsNearlyEqual(FY, 1.0) ? 2 : (FMath::IsNearlyEqual(FY, 0.5) ? 1 : -1));
			static const char* Names[3][3] = { {"TOPLEFT", "TOP", "TOPRIGHT"}, {"LEFT", "CENTER", "RIGHT"}, {"BOTTOMLEFT", "BOTTOM", "BOTTOMRIGHT"} };
			return X < 0 || Y < 0 ? nullptr : Names[Y][X];
		};
		const char* RelPoint = PointName(RA.Minimum.X, RA.Minimum.Y);
		if (!RelPoint) return false;
		const float S = FMath::Max(0.01f, RegionCtx->FrameManager->GetUIScale());
		const FMargin O = RegionSlot->GetOffsets();
		const FVector2D Align = RegionSlot->GetAlignment();
		const FVector2D Size = RegionSlot->GetAutoSize() ? RegionWidget->GetDesiredSize() : FVector2D(O.Right, O.Bottom);
		const float X = static_cast<float>((O.Left - Align.X * Size.X) / S);
		const float Y = static_cast<float>(-(O.Top - Align.Y * Size.Y) / S);
		lua_getfield(L, 2, "GetParent");
		if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
		lua_pushvalue(L, 2);
		if (lua_pcall(L, 1, 1, 0) != 0 || !lua_istable(L, -1)) { lua_pop(L, 1); return false; }
		const int RegionFrame = lua_gettop(L);
		lua_getfield(L, 1, "ClearAllPoints");
		if (lua_isfunction(L, -1)) { lua_pushvalue(L, 1); if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1); } else lua_pop(L, 1);
		lua_getfield(L, 1, "SetPoint");
		if (lua_isfunction(L, -1))
		{
			lua_pushvalue(L, 1); lua_pushstring(L, "TOPLEFT"); lua_pushvalue(L, RegionFrame); lua_pushstring(L, RelPoint);
			lua_pushnumber(L, X); lua_pushnumber(L, Y);
			if (lua_pcall(L, 6, 0, 0) != 0) lua_pop(L, 1);
		}
		else lua_pop(L, 1);
		lua_getfield(L, 1, "SetSize");
		if (lua_isfunction(L, -1))
		{
			lua_pushvalue(L, 1); lua_pushnumber(L, Size.X / S); lua_pushnumber(L, Size.Y / S);
			if (lua_pcall(L, 3, 0, 0) != 0) lua_pop(L, 1);
		}
		else lua_pop(L, 1);
		lua_settop(L, RegionFrame - 1);
		return true;
	}
	if (RelHandle >= 0 && ParentHandle < 0)
	{
		// A frame: compare with its parent frame (GetParent()).
		lua_getfield(L, 1, "GetParent");
		if (lua_isfunction(L, -1))
		{
			lua_pushvalue(L, 1);
			if (lua_pcall(L, 1, 1, 0) == 0)
			{
				const bool bSame = lua_rawequal(L, -1, 2) != 0;
				lua_pop(L, 1);
				if (bSame) return false;
			}
			else lua_pop(L, 1);
		}
		else lua_pop(L, 1);
	}
	lua_getfield(L, 1, "ClearAllPoints");
	if (lua_isfunction(L, -1)) { lua_pushvalue(L, 1); if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1); }
	else lua_pop(L, 1);
	for (const char* Point : { "TOPLEFT", "BOTTOMRIGHT" })
	{
		lua_getfield(L, 1, "SetPoint");
		if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
		lua_pushvalue(L, 1);
		lua_pushstring(L, Point);
		lua_pushvalue(L, 2);
		lua_pushstring(L, Point);
		lua_pushnumber(L, 0);
		lua_pushnumber(L, 0);
		if (lua_pcall(L, 6, 0, 0) != 0) lua_pop(L, 1);
	}
	return true;
}

static int LF_SetAllPoints(lua_State* L)
{
	if (SetAllPointsToOther(L)) return 0;
	if (IsRegionTable(L, 1)) return LR_SetAllPoints(L, GetRegionWidget(L, 1));
    // XML texture regions share the frame metatable but have no frame handle.
    if (UImage* Image = GetTextureImage(L))
    {
        if (auto* Slot = Cast<UCanvasPanelSlot>(Image->Slot))
        {
            Slot->SetAutoSize(false);
            Slot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f));
            Slot->SetOffsets(FMargin(0));
        }
        return 0;
    }
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		FWowFrameDef* Def = Ctx->FrameManager->GetMutableFrameDef(Handle);
		if (Def)
		{
			Def->bSetAllPoints = true;
			Def->Anchors.Empty();
			Ctx->FrameManager->SetFrameAnchors(Handle, Def->Anchors);
		}
	}
	return 0;
}

// frame:SetSize(width, height)
static int LF_SetSize(lua_State* L)
{
	if (IsRegionTable(L, 1)) return LR_SetSize(L, GetRegionWidget(L, 1), true, true);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		float W = static_cast<float>(luaL_checknumber(L, 2));
		float H = static_cast<float>(luaL_checknumber(L, 3));
		Ctx->FrameManager->SetFrameSize(Handle, W, H);
	}
	return 0;
}

// frame:SetWidth(width)
static int LF_SetWidth(lua_State* L)
{
	if (IsRegionTable(L, 1)) return LR_SetSize(L, GetRegionWidget(L, 1), true, false);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		float W = static_cast<float>(luaL_checknumber(L, 2));
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		float H = Def ? Def->Height : 0.f;
		Ctx->FrameManager->SetFrameSize(Handle, W, H);
	}
	return 0;
}

// frame:SetHeight(height)
static int LF_SetHeight(lua_State* L)
{
	if (IsRegionTable(L, 1)) return LR_SetSize(L, GetRegionWidget(L, 1), false, true);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		float H = static_cast<float>(luaL_checknumber(L, 2));
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		float W = Def ? Def->Width : 0.f;
		Ctx->FrameManager->SetFrameSize(Handle, W, H);
	}
	return 0;
}

/** r228: a frame sized by its anchors (two points, SetAllPoints) reports the size they give it,
 *  like the original; AceGUI sizes its content from GetWidth/GetHeight of such frames. */
static bool AnchoredFrameSize(FWowLuaContext* Ctx, int64 Handle, const FWowFrameDef& Def, bool bWidth, float& Out)
{
	if (!Def.bSetAllPoints && Def.Anchors.Num() < 2) return false;
	Ctx->FrameManager->FlushDependentLayout();
	float Left = 0.f, Bottom = 0.f, Right = 0.f, Top = 0.f;
	if (!Ctx->FrameManager->GetFrameScreenRect(Handle, Left, Bottom, Right, Top)) return false;
	Out = bWidth ? Right - Left : Top - Bottom;
	return Out > 0.f;
}

// frame:GetWidth()
static int LF_GetWidth(lua_State* L)
{
	if (IsRegionTable(L, 1)) return LR_GetSize(L, GetRegionWidget(L, 1), true);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			float Anchored = 0.f;
			lua_pushnumber(L, AnchoredFrameSize(Ctx, Handle, *Def, true, Anchored) ? Anchored : Def->Width);
			return 1;
		}
	}
	lua_pushnumber(L, 0);
	return 1;
}

// frame:GetHeight()
static int LF_GetHeight(lua_State* L)
{
	if (IsRegionTable(L, 1)) return LR_GetSize(L, GetRegionWidget(L, 1), false);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			float Anchored = 0.f;
			lua_pushnumber(L, AnchoredFrameSize(Ctx, Handle, *Def, false, Anchored) ? Anchored : Def->Height);
			return 1;
		}
	}
	lua_pushnumber(L, 0);
	return 1;
}

// frame:SetAlpha(alpha)
static int LF_SetAlpha(lua_State* L)
{
	if (TableRepresentsRegion(L, 1))
	{
		float Alpha = static_cast<float>(luaL_checknumber(L, 2));
		if (UImage* Image = GetTextureImage(L, 1))
		{
			Image->SetRenderOpacity(Alpha);
		}
		else if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			TextBlock->SetRenderOpacity(Alpha);
		}

		lua_pushnumber(L, Alpha);
		lua_setfield(L, 1, "__alpha");
		return 0;
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		float Alpha = static_cast<float>(luaL_checknumber(L, 2));
		Ctx->FrameManager->SetFrameAlpha(Handle, Alpha);
		// Store alpha on frame table for GetAlpha
		lua_pushnumber(L, Alpha);
		lua_setfield(L, 1, "__alpha");
	}
	return 0;
}

// frame:GetAlpha()
static int LF_GetAlpha(lua_State* L)
{
	lua_getfield(L, 1, "__alpha");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 1.0);
	}
	return 1;
}

// Parse strata string
static EWowFrameStrata ParseFrameStrata(const char* Str)
{
	if (!Str) return EWowFrameStrata::MEDIUM;
	if (strcmp(Str, "BACKGROUND") == 0) return EWowFrameStrata::BACKGROUND;
	if (strcmp(Str, "LOW") == 0) return EWowFrameStrata::LOW;
	if (strcmp(Str, "MEDIUM") == 0) return EWowFrameStrata::MEDIUM;
	if (strcmp(Str, "HIGH") == 0) return EWowFrameStrata::HIGH;
	if (strcmp(Str, "DIALOG") == 0) return EWowFrameStrata::DIALOG;
	if (strcmp(Str, "FULLSCREEN") == 0) return EWowFrameStrata::FULLSCREEN;
	if (strcmp(Str, "FULLSCREEN_DIALOG") == 0) return EWowFrameStrata::FULLSCREEN_DIALOG;
	if (strcmp(Str, "TOOLTIP") == 0) return EWowFrameStrata::TOOLTIP;
	return EWowFrameStrata::MEDIUM;
}

static const char* StrataToString(EWowFrameStrata S)
{
	switch (S)
	{
	case EWowFrameStrata::BACKGROUND: return "BACKGROUND";
	case EWowFrameStrata::LOW: return "LOW";
	case EWowFrameStrata::MEDIUM: return "MEDIUM";
	case EWowFrameStrata::HIGH: return "HIGH";
	case EWowFrameStrata::DIALOG: return "DIALOG";
	case EWowFrameStrata::FULLSCREEN: return "FULLSCREEN";
	case EWowFrameStrata::FULLSCREEN_DIALOG: return "FULLSCREEN_DIALOG";
	case EWowFrameStrata::TOOLTIP: return "TOOLTIP";
	default: return "MEDIUM";
	}
}

// frame:SetFrameStrata(strata)
static int LF_SetFrameStrata(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const char* StrataStr = luaL_checkstring(L, 2);
		Ctx->FrameManager->SetFrameStrata(Handle, ParseFrameStrata(StrataStr));
	}
	return 0;
}

// frame:GetFrameStrata()
static int LF_GetFrameStrata(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			lua_pushstring(L, StrataToString(Def->Strata));
			return 1;
		}
	}
	lua_pushstring(L, "MEDIUM");
	return 1;
}

// frame:SetFrameLevel(level)
static int LF_SetFrameLevel(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		int32 Level = static_cast<int32>(luaL_checknumber(L, 2));
		Ctx->FrameManager->SetFrameLevel(Handle, Level);
	}
	return 0;
}

// frame:GetFrameLevel()
static int LF_GetFrameLevel(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			lua_pushnumber(L, Def->FrameLevel);
			return 1;
		}
	}
	lua_pushnumber(L, 0);
	return 1;
}

static int LF_SetHitRectInsets(lua_State* L)
{
    auto* Ctx = WowLuaApi::GetContext(L);
    const int64 Handle = GetFrameHandle(L);
    const float Left = luaL_checknumber(L,2), Right = luaL_checknumber(L,3);
    const float Top = luaL_checknumber(L,4), Bottom = luaL_checknumber(L,5);
    if (Ctx && Ctx->FrameManager)
        Ctx->FrameManager->SetFrameHitRectInsets(Handle,FMargin(Left,Top,Right,Bottom));
    return 0;
}

static int LF_GetHitRectInsets(lua_State* L)
{
    auto* Ctx = WowLuaApi::GetContext(L);
    const int64 Handle = GetFrameHandle(L);
    const auto* Def = Ctx && Ctx->FrameManager ? Ctx->FrameManager->GetFrameDef(Handle) : nullptr;
    const FMargin Insets = Def ? Def->HitRectInsets.Get(FMargin(0)) : FMargin(0);
    lua_pushnumber(L,Insets.Left); lua_pushnumber(L,Insets.Right);
    lua_pushnumber(L,Insets.Top); lua_pushnumber(L,Insets.Bottom);
    return 4;
}

// frame:EnableMouse(enable)
static int LF_EnableMouse(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	const bool bEnable = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameMouseEnabled(Handle, bEnable);
	}
	return 0;
}

// frame:EnableKeyboard(enable)
static int LF_EnableKeyboard(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	const bool bEnable = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameKeyboardEnabled(Handle, bEnable);
	}
	return 0;
}

// frame:EnableMouseWheel(enable)
static int LF_EnableMouseWheel(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	const bool bEnable = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameMouseWheelEnabled(Handle, bEnable);
	}
	return 0;
}

// frame:SetMovable(movable)
static int LF_SetMovable(lua_State* L)
{
	return 0;
}

// frame:SetResizable(resizable)
static int LF_SetResizable(lua_State* L)
{
	return 0;
}

// Dressing room (DressUpModel): the frame's methods go to FWowFrameManager::DressUpHandler.
static bool IsDressUpModelFrame(lua_State* L, FWowLuaContext*& OutCtx)
{
	OutCtx = WowLuaApi::GetContext(L);
	if (!OutCtx || !OutCtx->FrameManager || !lua_istable(L, 1)) return false;
	const int64 Handle = GetOptionalFrameHandle(L, 1);
	const FWowFrameDef* Def = Handle >= 0 ? OutCtx->FrameManager->GetFrameDef(Handle) : nullptr;
	return Def && Def->Name == TEXT("DressUpModel") && OutCtx->FrameManager->DressUpHandler;
}

// Item entry from a link ("item:1234:..."), an "item:1234" string or a number.
static uint32 DressUpItemEntryArg(lua_State* L, int Index)
{
	if (lua_type(L, Index) == LUA_TNUMBER) return uint32(lua_tonumber(L, Index));
	if (!lua_isstring(L, Index)) return 0;
	const FString Link = UTF8_TO_TCHAR(lua_tostring(L, Index));
	const int32 At = Link.Find(TEXT("item:"));
	return uint32(FCString::Atoi(*Link.Mid(At >= 0 ? At + 5 : 0)));
}

static int LF_DressUpReset(lua_State* L)
{
	FWowLuaContext* Ctx = nullptr;
	if (IsDressUpModelFrame(L, Ctx)) Ctx->FrameManager->DressUpHandler(TEXT("reset"), 0, 0.f);
	return 0;
}

static int LF_DressUpUndress(lua_State* L)
{
	FWowLuaContext* Ctx = nullptr;
	if (IsDressUpModelFrame(L, Ctx)) Ctx->FrameManager->DressUpHandler(TEXT("undress"), 0, 0.f);
	return 0;
}

static int LF_DressUpTryOn(lua_State* L)
{
	FWowLuaContext* Ctx = nullptr;
	if (IsDressUpModelFrame(L, Ctx))
		if (const uint32 TryOnEntry = DressUpItemEntryArg(L, 2))
			Ctx->FrameManager->DressUpHandler(TEXT("tryon"), TryOnEntry, 0.f);
	return 0;
}

// model:SetRotation(radians) / SetFacing: Model_RotateLeft/Right and Model_OnUpdate (rotate
// buttons) turn the dressing room preview by the change since the last call.
static int LF_ModelSetRotation(lua_State* L)
{
	FWowLuaContext* Ctx = nullptr;
	if (!IsDressUpModelFrame(L, Ctx)) return 0;
	const double NewRotation = luaL_optnumber(L, 2, 0.0);
	lua_getfield(L, 1, "__wowLastRotation");
	const bool bHadRotation = lua_isnumber(L, -1) != 0;
	const double OldRotation = bHadRotation ? lua_tonumber(L, -1) : NewRotation;
	lua_pop(L, 1);
	lua_pushnumber(L, NewRotation);
	lua_setfield(L, 1, "__wowLastRotation");
	const double DeltaDegrees = FMath::RadiansToDegrees(NewRotation - OldRotation);
	if (bHadRotation && FMath::Abs(DeltaDegrees) > 0.01 && FMath::Abs(DeltaDegrees) < 180.0)
		Ctx->FrameManager->DressUpHandler(TEXT("rotate"), 0, float(DeltaDegrees)); // left button = negative, like the character sheet
	return 0;
}

// frame:SetClampedToScreen(clamped)
static int LF_SetClampedToScreen(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !lua_istable(L, 1)) return 0;
	const int64 Handle = GetOptionalFrameHandle(L, 1);
	if (FWowFrameDef* ClampDef = Handle >= 0 ? Ctx->FrameManager->GetMutableFrameDef(Handle) : nullptr)
		ClampDef->bClampedToScreen = lua_toboolean(L, 2) != 0;
	return 0;
}

// frame:IsClampedToScreen()
static int LF_IsClampedToScreen(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = (Ctx && Ctx->FrameManager && lua_istable(L, 1)) ? GetOptionalFrameHandle(L, 1) : -1;
	const FWowFrameDef* ClampDef = Handle >= 0 ? Ctx->FrameManager->GetFrameDef(Handle) : nullptr;
	lua_pushboolean(L, ClampDef && (ClampDef->bClampedToScreen || ClampDef->Type == EWowFrameType::GameTooltip));
	return 1;
}

// frame:SetParent(parent)
static int LF_SetParent(lua_State* L)
{
	// frame:SetParent(parent | "ParentName" | nil). AceGUI recycles its widgets and
	// re-parents them into each container; without this they all drew at UIParent's corner.
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !lua_istable(L, 1)) return 0;
	const int64 Handle = GetOptionalFrameHandle(L, 1);
	if (Handle < 0) return 0;
	int64 NewParent = -1;
	if (lua_istable(L, 2))
	{
		NewParent = GetOptionalFrameHandle(L, 2);
		if (NewParent < 0)
		{
			lua_getfield(L, 2, "__name");
			if (lua_type(L, -1) == LUA_TSTRING) NewParent = Ctx->FrameManager->FindFrame(UTF8_TO_TCHAR(lua_tostring(L, -1)));
			lua_pop(L, 1);
		}
	}
	else if (lua_type(L, 2) == LUA_TSTRING)
	{
		NewParent = Ctx->FrameManager->FindFrame(UTF8_TO_TCHAR(lua_tostring(L, 2)));
	}
	if (NewParent < 0) NewParent = Ctx->FrameManager->FindFrame(TEXT("UIParent"));
	Ctx->FrameManager->SetFrameParent(Handle, NewParent);
	return 0;
}

// frame:GetParent()
static int LF_GetParent(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !lua_istable(L, 1)) { lua_pushnil(L); return 1; }
	int64 ParentHandle = -1;
	const int64 Handle = GetOptionalFrameHandle(L, 1);
	if (Handle >= 0)
	{
		ParentHandle = Ctx->FrameManager->GetParentHandle(Handle);
	}
	else
	{
		// Texture/FontString: the frame that created it.
		lua_getfield(L, 1, "__parentHandle");
		ParentHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
		lua_pop(L, 1);
	}
	if (ParentHandle >= 0)
	{
		// Canonical object first: anonymous parents have no global to look up.
		if (Ctx->EventSystem && Ctx->EventSystem->PushFrameObject(ParentHandle)) return 1;
		const FString ParentName = Ctx->FrameManager->GetFrameName(ParentHandle);
		if (!ParentName.IsEmpty())
		{
			lua_getglobal(L, TCHAR_TO_UTF8(*ParentName));
			if (lua_istable(L, -1)) return 1;
			lua_pop(L, 1);
		}
	}
	lua_pushnil(L);
	return 1;
}

// frame:GetChildren()
static int LF_GetChildren(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		TArray<int64> Children = Ctx->FrameManager->GetChildHandles(Handle);
		int Count = 0;
		for (int64 ChildHandle : Children)
		{
			FString ChildName = Ctx->FrameManager->GetFrameName(ChildHandle);
			if (!ChildName.IsEmpty())
			{
				// Push named children as Lua globals
				lua_getglobal(L, TCHAR_TO_UTF8(*ChildName));
				if (lua_istable(L, -1))
				{
					Count++;
				}
				else
				{
					lua_pop(L, 1);
				}
			}
			else
			{
				// Push nil for unnamed children to match GetNumChildren count
				lua_pushnil(L);
				Count++;
			}
		}
		return Count;
	}
	// Push nil when FrameManager is null to maintain stack balance
	lua_pushnil(L);
	return 1;
}

// frame:GetNumChildren()
static int LF_GetNumChildren(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		TArray<int64> Children = Ctx->FrameManager->GetChildHandles(Handle);
		lua_pushnumber(L, Children.Num());
		return 1;
	}
	lua_pushnumber(L, 0);
	return 1;
}

// Helper: get the UImage* from a texture Lua table (either named or dynamic)
static UImage* GetTextureImage(lua_State* L, int idx)
{
	// Try dynamic image widget first (from CreateTexture + SetTexture)
	lua_getfield(L, idx, "__imageWidget");
	if (lua_islightuserdata(L, -1))
	{
		UImage* Img = static_cast<UImage*>(lua_touserdata(L, -1));
		lua_pop(L, 1);
		return Img;
	}
	lua_pop(L, 1);

	// Try named texture region (from XML <Texture name="...">)
	lua_getfield(L, idx, "__name");
	if (lua_isstring(L, -1))
	{
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (Ctx && Ctx->FrameManager)
		{
			UImage* Img = Ctx->FrameManager->GetNamedTexture(UTF8_TO_TCHAR(lua_tostring(L, -1)));
			lua_pop(L, 1);
			return Img;
		}
	}
	lua_pop(L, 1);

	return nullptr;
}

// Texture region methods
static int LF_TextureSetTexture(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	// r325 (Stefan 14.10: a stray down-arrow button in the minimap = MiniMapLFGFrameDropDownButton):
	// SetTexture("") clears like nil. UIDropDownMenu_Initialize(..., "MENU") hides a dropdown's
	// button this way (its Normal/Pushed/Disabled/Highlight textures set to ""); those are the
	// button's state brushes here, so they are cleared on the button as well.
	if (lua_type(L, 2) == LUA_TSTRING && lua_tostring(L, 2)[0] == '\0')
	{
		lua_getfield(L, 1, "__name");
		const FString RegionName = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
		lua_pop(L, 1);
		FWowLuaContext* ClearCtx = WowLuaApi::GetContext(L);
		static const TCHAR* StateSuffixes[] = { TEXT("NormalTexture"), TEXT("PushedTexture"), TEXT("DisabledTexture"), TEXT("HighlightTexture") };
		for (int32 StateIndex = 0; ClearCtx && ClearCtx->FrameManager && StateIndex < 4; ++StateIndex)
		{
			if (!RegionName.EndsWith(StateSuffixes[StateIndex])) continue;
			const FString ButtonName = RegionName.LeftChop(FCString::Strlen(StateSuffixes[StateIndex]));
			if (UButton* Button = ClearCtx->FrameManager->GetButtonWidget(ClearCtx->FrameManager->FindFrame(ButtonName)))
			{
				FSlateBrush NoBrush;
				NoBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
				FButtonStyle Style = Button->GetStyle();
				const UObject* OldNormal = Style.Normal.GetResourceObject();
				switch (StateIndex)
				{
				case 0:
					Style.Normal = NoBrush;
					if (Style.Hovered.GetResourceObject() == OldNormal) Style.Hovered = NoBrush;
					if (Style.Pressed.GetResourceObject() == OldNormal) Style.Pressed = NoBrush;
					break;
				case 1: Style.Pressed = NoBrush; break;
				case 2: Style.Disabled = NoBrush; break;
				default: Style.Hovered = NoBrush; break;
				}
				Button->SetStyle(Style);
			}
			break;
		}
		lua_settop(L, 1);
		lua_pushnil(L);
	}
	// WoW allows SetTexture(nil) to clear the texture
	if (lua_isnil(L, 2) || lua_isnone(L, 2))
	{
		lua_pushnil(L);
		lua_setfield(L, 1, "__texture");
		if (UImage* Image = GetTextureImage(L, 1))
		{
			FSlateBrush EmptyBrush = Image->GetBrush();
			EmptyBrush.SetResourceObject(nullptr);
			EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			Image->SetBrush(EmptyBrush);
			Image->SetRenderOpacity(0.0f);
		}
		return 0;
	}
	// SetTexture(r, g, b[, a]) fills the region with a solid colour (3.3.5; AceGUI scroll and
	// tree backgrounds use SetTexture(0, 0, 0, 0.4)). It used to load a file named "0".
	if (lua_isnumber(L, 2) && lua_isnumber(L, 3))
	{
		const lua_Number R = lua_tonumber(L, 2), G = lua_tonumber(L, 3);
		const lua_Number B = lua_isnumber(L, 4) ? lua_tonumber(L, 4) : 0.0;
		const lua_Number A = lua_isnumber(L, 5) ? lua_tonumber(L, 5) : 1.0;
		lua_settop(L, 1);
		lua_pushstring(L, "Interface\\Buttons\\WHITE8X8");
		LF_TextureSetTexture(L);
		lua_settop(L, 1);
		lua_getfield(L, 1, "SetVertexColor");
		if (lua_isfunction(L, -1))
		{
			lua_pushvalue(L, 1);
			lua_pushnumber(L, R); lua_pushnumber(L, G); lua_pushnumber(L, B); lua_pushnumber(L, A);
			if (lua_pcall(L, 5, 0, 0) != 0) lua_pop(L, 1);
		}
		else lua_pop(L, 1);
		return 0;
	}
	// Also handle number arg (texture ID) — convert to string
	const char* TexturePath;
	if (lua_isnumber(L, 2))
	{
		lua_pushfstring(L, "%d", (int)lua_tonumber(L, 2));
		TexturePath = lua_tostring(L, -1);
		lua_pop(L, 1);
	}
	else
	{
		TexturePath = luaL_checkstring(L, 2);
	}

	// Store in Lua table
	lua_pushstring(L, TexturePath);
	lua_setfield(L, 1, "__texture");

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		// First, try to find this texture region by name (from XML Texture name="Foo")
		lua_getfield(L, 1, "__name");
		const char* TexName = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
		lua_pop(L, 1);

		UImage* NamedImage = TexName ? Ctx->FrameManager->GetNamedTexture(UTF8_TO_TCHAR(TexName)) : nullptr;

		UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));

		// r331 (Stefan 14.10: black squares with a blue rim over the spellbook): a button's named
		// <NormalTexture>/<PushedTexture>/<HighlightTexture>/<DisabledTexture> region
		// (SpellButton1Highlight) is that state's brush on the button. SpellButton_UpdateButton
		// calls highlightTexture:SetTexture("...ButtonHilight-Square"); that made a new, always
		// shown 64 x 64 image at the button's top-left instead of changing the hover art.
		if (Texture && !NamedImage && TexName)
		{
			lua_getfield(L, 1, "__parentHandle");
			const int64 StateParent = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
			lua_pop(L, 1);
			const FWowFrameDef* ParentDef = StateParent >= 0 ? Ctx->FrameManager->GetFrameDef(StateParent) : nullptr;
			UButton* StateButton = ParentDef ? Ctx->FrameManager->GetButtonWidget(StateParent) : nullptr;
			const FString RegionName = UTF8_TO_TCHAR(TexName);
			int32 StateIndex = INDEX_NONE;
			if (StateButton)
			{
				if (RegionName == ParentDef->NormalTextureName) StateIndex = 0;
				else if (RegionName == ParentDef->PushedTextureName) StateIndex = 1;
				else if (RegionName == ParentDef->HighlightTextureName) StateIndex = 2;
				else if (RegionName == ParentDef->DisabledTextureName) StateIndex = 3;
			}
			if (StateIndex != INDEX_NONE)
			{
				FButtonStyle Style = StateButton->GetStyle();
				const UObject* OldNormal = Style.Normal.GetResourceObject();
				auto Retexture = [Texture](FSlateBrush& Brush)
				{
					// Keeps the state's UVs and size; only the image changes.
					Brush.SetResourceObject(Texture);
					Brush.DrawAs = ESlateBrushDrawType::Image;
				};
				switch (StateIndex)
				{
				case 0:
					if (Style.Hovered.GetResourceObject() == OldNormal) { Retexture(Style.Hovered); }
					if (Style.Pressed.GetResourceObject() == OldNormal) { Retexture(Style.Pressed); }
					if (Style.Disabled.GetResourceObject() == OldNormal) { Retexture(Style.Disabled); }
					Retexture(Style.Normal);
					break;
				case 1: Retexture(Style.Pressed); break;
				case 2: Retexture(Style.Hovered); break;
				default: Retexture(Style.Disabled); break;
				}
				StateButton->SetStyle(Style);
				return 0;
			}
		}
		if (Texture && NamedImage)
		{
			// SetTexture changes the image resource, not the region's XML/Lua UVs.
			// Rebuilding the brush loses the atlas crop and misaligns unit-frame art.
			FSlateBrush Brush = NamedImage->GetBrush();
			Brush.SetResourceObject(Texture);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Brush.Tiling = ESlateBrushTileType::NoTile;

			// Apply stored UV coordinates if any, handling WoW's Left>Right flip convention
			lua_getfield(L, 1, "__texLeft");
			if (lua_isnumber(L, -1))
			{
				float UVLeft = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_getfield(L, 1, "__texRight"); float UVRight = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_getfield(L, 1, "__texTop"); float UVTop = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_getfield(L, 1, "__texBottom"); float UVBottom = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);

				float NL = FMath::Min(UVLeft, UVRight), NR = FMath::Max(UVLeft, UVRight);
				float NT = FMath::Min(UVTop, UVBottom), NB = FMath::Max(UVTop, UVBottom);
				FBox2D UVRegion(FVector2D(NL, NT), FVector2D(NR, NB));
				Brush.SetUVRegion(UVRegion);

				bool bFlipH = UVLeft > UVRight;
				bool bFlipV = UVTop > UVBottom;
				if (bFlipH || bFlipV)
				{
					FVector2D Scale(bFlipH ? -1.0f : 1.0f, bFlipV ? -1.0f : 1.0f);
					NamedImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
					NamedImage->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, Scale, FVector2D::ZeroVector, 0.0f));
				}
			}
			else
			{
				lua_pop(L, 1);
			}

			NamedImage->SetBrush(Brush);
			NamedImage->SetRenderOpacity(1.0f); // Make visible (may have been hidden as empty placeholder)

			// If the slot was auto-sized (from creation with no explicit size),
			// update it to match the texture dimensions × UIScale.
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(NamedImage->Slot))
			{
				FVector2D CurrentSize = Slot->GetSize();
				const FAnchors Anchors = Slot->GetAnchors();
				const bool bStretched = Anchors.Minimum != Anchors.Maximum;
				// Stretch offsets are margins, not an empty texture size.
				if (!bStretched && (CurrentSize.X <= 0.f || CurrentSize.Y <= 0.f))
				{
					float UIScale = Ctx->FrameManager->GetUIScale();
					Slot->SetAutoSize(false);
					Slot->SetSize(FVector2D(Brush.ImageSize.X * UIScale, Brush.ImageSize.Y * UIScale));
				}
			}
			return 0;
		}

		// Fallback: try parent handle approach (dynamic textures from CreateTexture)
		lua_getfield(L, 1, "__parentHandle");
		if (lua_isnumber(L, -1))
		{
			int64 ParentHandle = static_cast<int64>(lua_tointeger(L, -1));
			UWidget* ParentWidget = Ctx->FrameManager->GetWidgetForHandle(ParentHandle);

			if (!Texture)
				Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));
			if (Texture)
			{
				FSlateBrush Brush;
				Brush.SetResourceObject(Texture);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
				Brush.Tiling = ESlateBrushTileType::NoTile;

				// Apply stored UV coordinates if any, handling WoW flip convention
				bool bDynFlipH = false, bDynFlipV = false;
				lua_getfield(L, 1, "__texLeft");
				if (lua_isnumber(L, -1))
				{
					float UVLeft = static_cast<float>(lua_tonumber(L, -1));
					lua_pop(L, 1);
					lua_getfield(L, 1, "__texRight"); float UVRight = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
					lua_getfield(L, 1, "__texTop"); float UVTop = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
					lua_getfield(L, 1, "__texBottom"); float UVBottom = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);

					bDynFlipH = UVLeft > UVRight;
					bDynFlipV = UVTop > UVBottom;
					float NL = FMath::Min(UVLeft, UVRight), NR = FMath::Max(UVLeft, UVRight);
					float NT = FMath::Min(UVTop, UVBottom), NB = FMath::Max(UVTop, UVBottom);
					FBox2D UVRegion(FVector2D(NL, NT), FVector2D(NR, NB));
					Brush.SetUVRegion(UVRegion);
				}
				else
				{
					lua_pop(L, 1);
				}

				if (UButton* Button = Cast<UButton>(ParentWidget))
				{
					FButtonStyle ButtonStyle = Button->GetStyle();
					ButtonStyle.Normal = Brush;
					Button->SetStyle(ButtonStyle);
				}
				else if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(ParentWidget))
				{
					// Check if we already have an image widget for this texture
					UImage* Img = nullptr;
					lua_getfield(L, 1, "__imageWidget");
					if (lua_islightuserdata(L, -1))
					{
						Img = static_cast<UImage*>(lua_touserdata(L, -1));
					}
					lua_pop(L, 1);

					if (!Img)
					{
						Img = NewObject<UImage>(Canvas);
						UCanvasPanelSlot* ImgSlot = Canvas->AddChildToCanvas(Img);
						if (ImgSlot)
						{
							// Check if SetAllPoints was called
							lua_getfield(L, 1, "__setAllPoints");
							bool bFillParent = lua_toboolean(L, -1) != 0;
							lua_pop(L, 1);

							if (bFillParent)
							{
								ImgSlot->SetAnchors(FAnchors(0, 0, 1, 1));
								ImgSlot->SetOffsets(FMargin(0));
							}
							else
							{
								// Check for stored size
								lua_getfield(L, 1, "__width");
								float W = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
								lua_pop(L, 1);
								lua_getfield(L, 1, "__height");
								float H = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
								lua_pop(L, 1);

								float UIScale = Ctx->FrameManager->GetUIScale();
								ImgSlot->SetAnchors(FAnchors(0.0f, 0.0f));
								ImgSlot->SetAlignment(FVector2D(0.0f, 0.0f));

								if (W > 0.f && H > 0.f)
								{
									ImgSlot->SetSize(FVector2D(W * UIScale, H * UIScale));
								}
								else
								{
									// Use texture natural size as default
									ImgSlot->SetSize(FVector2D(Texture->GetSizeX() * UIScale, Texture->GetSizeY() * UIScale));
								}

								// Apply stored position
								lua_getfield(L, 1, "__posX");
								float PosX = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
								lua_pop(L, 1);
								lua_getfield(L, 1, "__posY");
								float PosY = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
								lua_pop(L, 1);
								ImgSlot->SetPosition(FVector2D(PosX * UIScale, PosY * UIScale));
							}
						}

						// Store image widget reference
						lua_pushlightuserdata(L, Img);
						lua_setfield(L, 1, "__imageWidget");
					}

					Img->SetBrush(Brush);

					// Apply stored vertex color
					lua_getfield(L, 1, "__colorR");
					if (lua_isnumber(L, -1))
					{
						float R = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_getfield(L, 1, "__colorG"); float G = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_getfield(L, 1, "__colorB"); float B = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_getfield(L, 1, "__colorA"); float A = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 1.0f; lua_pop(L, 1);
						Img->SetColorAndOpacity(WowColor::FromWow(R, G, B, A));
					}
					else
					{
						lua_pop(L, 1);
					}

					// Apply stored alpha
					lua_getfield(L, 1, "__alpha");
					if (lua_isnumber(L, -1))
					{
						float Alpha = static_cast<float>(lua_tonumber(L, -1));
						Img->SetRenderOpacity(Alpha);
					}
					else
					{
						Img->SetRenderOpacity(1.0f);
					}
					lua_pop(L, 1);
					Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				}
			}
		}
		lua_pop(L, 1);
	}

	return 0;
}

static int LF_TextureSetTexCoord(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float Left = static_cast<float>(luaL_checknumber(L, 2));
	float Right = static_cast<float>(luaL_checknumber(L, 3));
	float Top = static_cast<float>(luaL_checknumber(L, 4));
	float Bottom = static_cast<float>(luaL_checknumber(L, 5));

	// Store in Lua table
	lua_pushnumber(L, Left); lua_setfield(L, 1, "__texLeft");
	lua_pushnumber(L, Right); lua_setfield(L, 1, "__texRight");
	lua_pushnumber(L, Top); lua_setfield(L, 1, "__texTop");
	lua_pushnumber(L, Bottom); lua_setfield(L, 1, "__texBottom");

	// Apply UV region to the UImage widget
	// WoW uses Left > Right for horizontal flip and Top > Bottom for vertical flip.
	// Normalize UVs for FBox2D and apply render transform for flipping.
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		float UVL = FMath::Min(Left, Right);
		float UVR = FMath::Max(Left, Right);
		float UVT = FMath::Min(Top, Bottom);
		float UVB = FMath::Max(Top, Bottom);

		FSlateBrush Brush = Img->GetBrush();
		FBox2D UVRegion(FVector2D(UVL, UVT), FVector2D(UVR, UVB));
		Brush.SetUVRegion(UVRegion);
		Img->SetBrush(Brush);

		bool bFlipH = Left > Right;
		bool bFlipV = Top > Bottom;
		if (bFlipH || bFlipV)
		{
			FVector2D Scale(bFlipH ? -1.0f : 1.0f, bFlipV ? -1.0f : 1.0f);
			Img->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			Img->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, Scale, FVector2D::ZeroVector, 0.0f));
		}
		else
		{
			// Clear any previous flip transform
			Img->SetRenderTransform(FWidgetTransform());
		}
	}

	return 0;
}

static int LF_TextureSetVertexColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float R = static_cast<float>(luaL_checknumber(L, 2));
	float G = static_cast<float>(luaL_checknumber(L, 3));
	float B = static_cast<float>(luaL_checknumber(L, 4));
	float A = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 1.0f;

	// Store in Lua table
	lua_pushnumber(L, R); lua_setfield(L, 1, "__colorR");
	lua_pushnumber(L, G); lua_setfield(L, 1, "__colorG");
	lua_pushnumber(L, B); lua_setfield(L, 1, "__colorB");
	lua_pushnumber(L, A); lua_setfield(L, 1, "__colorA");

	// Apply color tinting to the texture
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		Img->SetColorAndOpacity(WowColor::FromWow(R, G, B, A));
	}

	return 0;
}

/** r369: SetVertexColor on a button's named state texture (SpellButton1NormalTexture ...) tints that
 *  state's brush, like the client. It did nothing (no image behind those globals), so the spellbook's
 *  black frame on passive spells and the hidden frame on empty slots never showed. */
static bool TrySetButtonStateTint(lua_State* L)
{
	if (!lua_istable(L, 1)) return false;
	lua_getfield(L, 1, "__name");
	const FString RegionName = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
	lua_pop(L, 1);
	if (RegionName.IsEmpty()) return false;
	lua_getfield(L, 1, "__parentHandle");
	const int64 StateParent = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (StateParent < 0 || !Ctx || !Ctx->FrameManager) return false;
	const FWowFrameDef* ParentDef = Ctx->FrameManager->GetFrameDef(StateParent);
	UButton* StateButton = ParentDef ? Ctx->FrameManager->GetButtonWidget(StateParent) : nullptr;
	if (!StateButton) return false;
	int32 StateIndex = INDEX_NONE;
	if (RegionName == ParentDef->NormalTextureName) StateIndex = 0;
	else if (RegionName == ParentDef->PushedTextureName) StateIndex = 1;
	else if (RegionName == ParentDef->HighlightTextureName) StateIndex = 2;
	else if (RegionName == ParentDef->DisabledTextureName) StateIndex = 3;
	if (StateIndex == INDEX_NONE) return false;
	const FLinearColor Tint(static_cast<float>(luaL_optnumber(L, 2, 1.0)), static_cast<float>(luaL_optnumber(L, 3, 1.0)),
		static_cast<float>(luaL_optnumber(L, 4, 1.0)), static_cast<float>(luaL_optnumber(L, 5, 1.0)));
	FButtonStyle Style = StateButton->GetStyle();
	auto ApplyTint = [&Tint](FSlateBrush& Brush) { Brush.TintColor = FSlateColor(Tint); };
	switch (StateIndex)
	{
	case 0:
	{
		// The Pressed/Disabled states drawing the same image follow it (the frame manager sets them
		// from the normal art); a separate highlight keeps its own colour.
		const UObject* NormalResource = Style.Normal.GetResourceObject();
		ApplyTint(Style.Normal);
		if (NormalResource && Style.Hovered.GetResourceObject() == NormalResource) ApplyTint(Style.Hovered);
		if (NormalResource && Style.Pressed.GetResourceObject() == NormalResource) ApplyTint(Style.Pressed);
		if (NormalResource && Style.Disabled.GetResourceObject() == NormalResource) ApplyTint(Style.Disabled);
		break;
	}
	case 1: ApplyTint(Style.Pressed); break;
	case 2: ApplyTint(Style.Hovered); break;
	default: ApplyTint(Style.Disabled); break;
	}
	StateButton->SetStyle(Style);
	return true;
}

static int LF_SetVertexColorDispatch(lua_State* L)
{
	if (GetTextureImage(L, 1))
	{
		return LF_TextureSetVertexColor(L);
	}
	if (TrySetButtonStateTint(L)) return 0; // r369

	return LF_FontStringSetTextColor(L);
}

static int LF_TextureSetAlpha(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float Alpha = static_cast<float>(luaL_checknumber(L, 2));

	// Store in Lua table
	lua_pushnumber(L, Alpha);
	lua_setfield(L, 1, "__alpha");

	// Apply alpha to the texture region
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		Img->SetRenderOpacity(Alpha);
	}

	return 0;
}

// Dynamic texture: Show
static int LF_TextureShow(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (Ctx && Ctx->FrameManager && Ctx->FrameManager->IsHighlightLayerRegion(Img))
			Ctx->FrameManager->SetHighlightLayerRegionScriptShown(Img, true);
		else
			Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	return 0;
}

// Dynamic texture: Hide
static int LF_TextureHide(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (Ctx && Ctx->FrameManager && Ctx->FrameManager->IsHighlightLayerRegion(Img))
			Ctx->FrameManager->SetHighlightLayerRegionScriptShown(Img, false);
		Img->SetVisibility(ESlateVisibility::Collapsed);
	}
	return 0;
}








// frame:CreateTexture(name, layer, inherits)
// Canvas z-order for a region created from Lua: drawLayer (arg) like the XML <Layers>
// (BACKGROUND 0 .. HIGHLIGHT 4, 100 apart); default ARTWORK. Lua regions sit after the XML
// ones of the same layer.
static int32 LuaRegionZOrder(lua_State* L, int Idx, const char* DefaultLayer = "ARTWORK")
{
	const char* Layer = lua_type(L, Idx) == LUA_TSTRING ? lua_tostring(L, Idx) : DefaultLayer;
	int32 Level = 2;
	if (!FCStringAnsi::Stricmp(Layer, "BACKGROUND")) Level = 0;
	else if (!FCStringAnsi::Stricmp(Layer, "BORDER")) Level = 1;
	else if (!FCStringAnsi::Stricmp(Layer, "ARTWORK")) Level = 2;
	else if (!FCStringAnsi::Stricmp(Layer, "OVERLAY")) Level = 3;
	else if (!FCStringAnsi::Stricmp(Layer, "HIGHLIGHT")) Level = 4;
	return Level * 100 + 60;
}

// r237: texture:SetBlendMode(mode) - "ADD" draws additively (UWowBlendImage); other modes blend normally.
static int LF_TextureSetBlendMode(lua_State* L)
{
	if (!lua_istable(L, 1)) return 0;
	UImage* Img = GetTextureImage(L, 1);
	if (UWowBlendImage* Blend = Cast<UWowBlendImage>(Img))
	{
		const char* Mode = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : "BLEND";
		Blend->SetAdditive(!FCStringAnsi::Stricmp(Mode, "ADD"));
	}
	return 0;
}

static bool IsHighlightLayerArg(lua_State* L, int Idx)
{
	return lua_type(L, Idx) == LUA_TSTRING && !FCStringAnsi::Stricmp(lua_tostring(L, Idx), "HIGHLIGHT");
}

static int LF_CreateTexture(lua_State* L)
{
	// Create a minimal texture table with a metatable
	lua_newtable(L);

	// Self should be a table (frame) at arg 1 from colon-syntax call
	if (!lua_istable(L, 1))
	{
		// Gracefully handle wrong arg types - return empty texture table
		luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
		if (!lua_isnil(L, -1)) lua_setmetatable(L, -2); else lua_pop(L, 1);
		return 1;
	}

	// Store parent frame handle for texture regions
	int64 ParentHandle = GetFrameHandle(L);
	lua_pushinteger(L, ParentHandle);
	lua_setfield(L, -2, "__parentHandle");

	// Store name if provided (more forgiving - accept string, nil, or ignore tables)
	const char* Name = nullptr;
	if (lua_isstring(L, 2)) {
		Name = lua_tostring(L, 2);
	} else if (lua_isnil(L, 2) || lua_isnone(L, 2)) {
		Name = nullptr;
	} else {
		// Ignore other types (like tables) - don't error
		Name = nullptr;
	}

	if (Name && Name[0])
	{
		lua_pushstring(L, Name);
		lua_setfield(L, -2, "__name");
	}

	lua_pushstring(L, "Texture");
	lua_setfield(L, -2, "__objectType");

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UCanvasPanel* ParentCanvas = Cast<UCanvasPanel>(Ctx->FrameManager->GetWidgetForHandle(ParentHandle)))
		{
			UImage* ImageWidget = NewObject<UWowBlendImage>(ParentCanvas);
			FSlateBrush EmptyBrush;
			EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			ImageWidget->SetBrush(EmptyBrush);
			ImageWidget->SetRenderOpacity(0.0f);
			ImageWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* Slot = ParentCanvas->AddChildToCanvas(ImageWidget))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetAutoSize(true);
				Slot->SetZOrder(LuaRegionZOrder(L, 3));
			}
			// r237: a HIGHLIGHT-layer texture shows only while the mouse is over the frame
			// (AceGUI tabs, dropdown items and buttons drew their highlight glow all the time).
			if (IsHighlightLayerArg(L, 3))
			{
				Ctx->FrameManager->RegisterHighlightLayerRegion(ParentHandle, ImageWidget);
			}

			lua_pushlightuserdata(L, ImageWidget);
			lua_setfield(L, -2, "__imageWidget");
		}
	}

	// Texture methods
	lua_pushcfunction(L, LF_TextureSetTexture);
	lua_setfield(L, -2, "SetTexture");
	lua_pushcfunction(L, LF_TextureSetTexCoord);
	lua_setfield(L, -2, "SetTexCoord");
	lua_pushcfunction(L, LF_TextureSetVertexColor);
	lua_setfield(L, -2, "SetVertexColor");
	lua_pushcfunction(L, LF_TextureSetAlpha);
	lua_setfield(L, -2, "SetAlpha");
	lua_pushcfunction(L, LF_TextureShow);
	lua_setfield(L, -2, "Show");
	lua_pushcfunction(L, LF_TextureHide);
	lua_setfield(L, -2, "Hide");
	lua_pushcfunction(L, LF_SetPoint);
	lua_setfield(L, -2, "SetPoint");
	lua_pushcfunction(L, LF_ClearAllPoints);
	lua_setfield(L, -2, "ClearAllPoints");
	lua_pushcfunction(L, LF_SetAllPoints);
	lua_setfield(L, -2, "SetAllPoints");
	lua_pushcfunction(L, LF_SetSize);
	lua_setfield(L, -2, "SetSize");
	lua_pushcfunction(L, LF_SetWidth);
	lua_setfield(L, -2, "SetWidth");
	lua_pushcfunction(L, LF_SetHeight);
	lua_setfield(L, -2, "SetHeight");
	lua_pushcfunction(L, LF_GetWidth);
	lua_setfield(L, -2, "GetWidth");
	lua_pushcfunction(L, LF_GetHeight);
	lua_setfield(L, -2, "GetHeight");
	lua_pushcfunction(L, LF_IsShown);
	lua_setfield(L, -2, "IsShown");
	lua_pushcfunction(L, LF_IsShown);
	lua_setfield(L, -2, "IsVisible");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushstring(L2, "Texture"); return 1; }); // GetObjectType
	lua_setfield(L, -2, "GetObjectType");
	lua_pushcfunction(L, LF_TextureSetBlendMode);
	lua_setfield(L, -2, "SetBlendMode");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetDesaturated
	lua_setfield(L, -2, "SetDesaturated");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetNonBlocking
	lua_setfield(L, -2, "SetNonBlocking");
	lua_pushcfunction(L, [](lua_State* L2) -> int { // SetDrawLayer(layer)
		lua_getfield(L2, 1, "__imageWidget");
		UImage* Img = lua_islightuserdata(L2, -1) ? static_cast<UImage*>(lua_touserdata(L2, -1)) : nullptr;
		lua_pop(L2, 1);
		if (Img && IsValid(Img))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Img->Slot)) Slot->SetZOrder(LuaRegionZOrder(L2, 2));
			// r237: moving into / out of the HIGHLIGHT layer switches hover-only drawing.
			FWowLuaContext* Ctx2 = WowLuaApi::GetContext(L2);
			if (Ctx2 && Ctx2->FrameManager)
			{
				const bool bWasHighlight = Ctx2->FrameManager->IsHighlightLayerRegion(Img);
				if (IsHighlightLayerArg(L2, 2) && !bWasHighlight)
				{
					lua_getfield(L2, 1, "__parentHandle");
					const int64 Parent = lua_isnumber(L2, -1) ? (int64)lua_tointeger(L2, -1) : -1;
					lua_pop(L2, 1);
					Ctx2->FrameManager->RegisterHighlightLayerRegion(Parent, Img, Img->GetVisibility() != ESlateVisibility::Collapsed);
				}
				else if (!IsHighlightLayerArg(L2, 2) && bWasHighlight)
				{
					Ctx2->FrameManager->UnregisterHighlightLayerRegion(Img);
					Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				}
			}
		}
		return 0;
	});
	lua_setfield(L, -2, "SetDrawLayer");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetGradient
	lua_setfield(L, -2, "SetGradient");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetGradientAlpha
	lua_setfield(L, -2, "SetGradientAlpha");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetRotation
	lua_setfield(L, -2, "SetRotation");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetTexCoordModifiesRect
	lua_setfield(L, -2, "SetTexCoordModifiesRect");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushnumber(L2, 0); lua_pushnumber(L2, 0); lua_pushnumber(L2, 1); lua_pushnumber(L2, 1); return 4; }); // GetTexCoord
	lua_setfield(L, -2, "GetTexCoord");

	// Methods not set above (GetParent, SetDrawLayer, ...) resolve through the frame
	// metatable, and getmetatable(texture).__index works for addon toolkits.
	luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
	if (lua_istable(L, -1)) lua_setmetatable(L, -2); else lua_pop(L, 1);

	if (Name && Name[0])
	{
		lua_pushvalue(L, -1);
		lua_setglobal(L, Name);
	}

	return 1;
}

// FontString methods
static int LF_FontStringSetText(lua_State* L)
{
	return LF_SetText(L);
}

static int LF_FontStringGetText(lua_State* L)
{
	return LF_GetText(L);
}

static int LF_FontStringSetTextColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (UTextBlock* MarkupBlock = GetTextBlockWidgetForTable(L, 1)) GWowMarkupBaseColors.Remove(MarkupBlock); // r293: new own colour
	float R = static_cast<float>(luaL_checknumber(L, 2));
	float G = static_cast<float>(luaL_checknumber(L, 3));
	float B = static_cast<float>(luaL_checknumber(L, 4));
	float A = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 1.0f;

	// Store in Lua table
	lua_pushnumber(L, R); lua_setfield(L, 1, "__textColorR");
	lua_pushnumber(L, G); lua_setfield(L, 1, "__textColorG");
	lua_pushnumber(L, B); lua_setfield(L, 1, "__textColorB");
	lua_pushnumber(L, A); lua_setfield(L, 1, "__textColorA");

	// Apply to widget
	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		const FLinearColor TextColor = WowColor::FromWow(R, G, B, A); // WoW colours are sRGB
		TextBlock->SetColorAndOpacity(FSlateColor(TextColor));
	}

	return 0;
}

// fontstring:SetFont(fontPath, size, flags)
static int LF_FontStringSetFont(lua_State* L)
{
	// Store font info in the Lua table for later retrieval
	const char* FontPath = luaL_optstring(L, 2, "Fonts\\FRIZQT__.TTF");
	float FontSize = static_cast<float>(luaL_optnumber(L, 3, 12.0));
	const char* Flags = luaL_optstring(L, 4, "");

	lua_pushstring(L, FontPath);
	lua_setfield(L, 1, "__fontPath");
	lua_pushnumber(L, FontSize);
	lua_setfield(L, 1, "__fontSize");
	lua_pushstring(L, Flags);
	lua_setfield(L, 1, "__fontFlags");

	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		FSlateFontInfo FontInfo = TextBlock->GetFont();
		FontInfo.Size = FMath::RoundToInt(FontSize);
		// Flags "OUTLINE" / "THICKOUTLINE" (addons such as ElvUI outline most of their text).
		const FString FlagText = FString(UTF8_TO_TCHAR(Flags)).ToUpper();
		FontInfo.OutlineSettings.OutlineSize = FlagText.Contains(TEXT("THICKOUTLINE")) ? 2 : FlagText.Contains(TEXT("OUTLINE")) ? 1 : 0;
		FontInfo.OutlineSettings.OutlineColor = FLinearColor::Black;
		TextBlock->SetFont(FontInfo);
	}

	return 0;
}

// fontstring:GetFont() -> fontPath, size, flags
static int LF_FontStringGetFont(lua_State* L)
{
	lua_getfield(L, 1, "__fontPath");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushstring(L, "Fonts\\FRIZQT__.TTF");
	}

	lua_getfield(L, 1, "__fontSize");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 12.0);
	}

	lua_getfield(L, 1, "__fontFlags");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushstring(L, "");
	}

	return 3;
}

// fontstring:GetStringWidth() -> approximate width
static int LF_FontStringGetStringWidth(lua_State* L)
{
	FString Text;
	float FontSize = 12.0f;

	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		Text = TextBlock->GetText().ToString();
		FontSize = static_cast<float>(TextBlock->GetFont().Size);
	}
	else
	{
		lua_getfield(L, 1, "__text");
		Text = lua_isstring(L, -1) ? UTF8_TO_TCHAR(lua_tostring(L, -1)) : TEXT("");
		lua_pop(L, 1);

		lua_getfield(L, 1, "__fontSize");
		FontSize = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 12.0f;
		lua_pop(L, 1);
	}

	// r363: measured with the font string's font (UI units); the old guess stays as the fallback.
	if (UTextBlock* Measured = GetTextBlockWidgetForTable(L, 1))
	{
		FWowLuaContext* MeasureCtx = WowLuaApi::GetContext(L);
		const float MeasureScale = (MeasureCtx && MeasureCtx->FrameManager) ? FMath::Max(0.01f, MeasureCtx->FrameManager->GetUIScale()) : 1.0f;
		float MeasuredW = 0.f, MeasuredH = 0.f;
		if (WowTextMeasure::MeasurePx(Measured, 0.f, MeasuredW, MeasuredH))
		{
			lua_pushnumber(L, MeasuredW / MeasureScale);
			return 1;
		}
	}
	// Rough estimate: average character width is ~0.5 * font height
	float Width = static_cast<float>(Text.Len()) * FontSize * 0.5f;
	lua_pushnumber(L, Width);
	return 1;
}

// fontString:SetJustifyH("LEFT"|"CENTER"|"RIGHT")
static int LF_FontStringSetJustifyH(lua_State* L)
{
	if (!lua_istable(L, 1)) return 0;
	const char* Mode = luaL_optstring(L, 2, "CENTER");
	lua_pushstring(L, Mode);
	lua_setfield(L, 1, "__justifyH");
	if (UTextBlock* Text = GetTextBlockWidgetForTable(L, 1))
	{
		ETextJustify::Type Justify = ETextJustify::Center;
		if (FCStringAnsi::Stricmp(Mode, "LEFT") == 0) Justify = ETextJustify::Left;
		else if (FCStringAnsi::Stricmp(Mode, "RIGHT") == 0) Justify = ETextJustify::Right;
		Text->SetJustification(Justify);
	}
	return 0;
}

static int LF_FontStringGetJustifyH(lua_State* L)
{
	lua_getfield(L, 1, "__justifyH");
	if (!lua_isstring(L, -1)) { lua_pop(L, 1); lua_pushstring(L, "CENTER"); }
	return 1;
}

// fontString:SetFormattedText(format, ...) == SetText(format:format(...))
static int LF_FontStringSetFormattedText(lua_State* L)
{
	const int Args = lua_gettop(L);
	if (Args < 2) return 0;
	lua_getglobal(L, "string");
	lua_getfield(L, -1, "format");
	lua_remove(L, -2);
	for (int I = 2; I <= Args; ++I) lua_pushvalue(L, I);
	lua_call(L, Args - 1, 1);
	lua_replace(L, 2);
	lua_settop(L, 2);
	return LF_SetText(L);
}

static int LF_FontStringGetStringHeight(lua_State* L)
{
	float Height = 0.f;
	if (UTextBlock* Text = GetTextBlockWidgetForTable(L, 1))
	{
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		const float S = (Ctx && Ctx->FrameManager) ? FMath::Max(0.01f, Ctx->FrameManager->GetUIScale()) : 1.0f;
		// r363: measured and wrapped at the font string's width, like the client.
		float MeasuredW = 0.f, MeasuredH = 0.f;
		if (WowTextMeasure::MeasurePx(Text, RegionNumberField(L, 1, "__width", 0.f) * S, MeasuredW, MeasuredH)) Height = MeasuredH / S;
		if (Height <= 0.f) Height = Text->GetDesiredSize().Y / S;
		if (Height <= 0.f) Height = Text->GetFont().Size / S;
	}
	lua_pushnumber(L, Height);
	return 1;
}

// frame:CreateFontString(name, layer, inherits)
static int LF_CreateFontString(lua_State* L)
{
	lua_newtable(L);

	// Store parent frame handle for FontString regions
	int64 ParentHandle = GetFrameHandle(L);
	lua_pushinteger(L, ParentHandle);
	lua_setfield(L, -2, "__parentHandle");

	const char* Name = (lua_type(L, 2) == LUA_TSTRING) ? lua_tostring(L, 2) : nullptr;
	if (Name && Name[0])
	{
		lua_pushstring(L, Name);
		lua_setfield(L, -2, "__name");
	}

	lua_pushstring(L, "FontString");
	lua_setfield(L, -2, "__objectType");

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UCanvasPanel* ParentCanvas = Cast<UCanvasPanel>(Ctx->FrameManager->GetWidgetForHandle(ParentHandle)))
		{
			UTextBlock* TextWidget = NewObject<UTextBlock>(ParentCanvas);
			TextWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* Slot = ParentCanvas->AddChildToCanvas(TextWidget))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetAutoSize(true);
				Slot->SetZOrder(LuaRegionZOrder(L, 3));
			}

			lua_pushlightuserdata(L, TextWidget);
			lua_setfield(L, -2, "__textWidget");
		}
	}

	// FontString methods
	lua_pushcfunction(L, LF_FontStringSetText);
	lua_setfield(L, -2, "SetText");
	lua_pushcfunction(L, LF_FontStringGetText);
	lua_setfield(L, -2, "GetText");
	lua_pushcfunction(L, LF_FontStringSetTextColor);
	lua_setfield(L, -2, "SetTextColor");
	lua_pushcfunction(L, LF_FontStringSetFont); // SetFont
	lua_setfield(L, -2, "SetFont");
	lua_pushcfunction(L, LF_SetFontObject);
	lua_setfield(L, -2, "SetFontObject");
	lua_pushcfunction(L, LF_GetFontObject);
	lua_setfield(L, -2, "GetFontObject");
	lua_pushcfunction(L, LF_FontStringGetFont); // GetFont
	lua_setfield(L, -2, "GetFont");
	lua_pushcfunction(L, LF_FontStringSetJustifyH);
	lua_setfield(L, -2, "SetJustifyH");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetJustifyV
	lua_setfield(L, -2, "SetJustifyV");
	lua_pushcfunction(L, LF_SetPoint);
	lua_setfield(L, -2, "SetPoint");
	lua_pushcfunction(L, LF_ClearAllPoints);
	lua_setfield(L, -2, "ClearAllPoints");
	lua_pushcfunction(L, LF_SetAllPoints);
	lua_setfield(L, -2, "SetAllPoints");
	lua_pushcfunction(L, LF_Show);
	lua_setfield(L, -2, "Show");
	lua_pushcfunction(L, LF_Hide);
	lua_setfield(L, -2, "Hide");
	lua_pushcfunction(L, LF_SetAlpha);
	lua_setfield(L, -2, "SetAlpha");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetShadowOffset
	lua_setfield(L, -2, "SetShadowOffset");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetShadowColor
	lua_setfield(L, -2, "SetShadowColor");
	lua_pushcfunction(L, LF_FontStringGetStringWidth); // GetStringWidth
	lua_setfield(L, -2, "GetStringWidth");
	lua_pushcfunction(L, LF_FontStringGetStringHeight);
	lua_setfield(L, -2, "GetStringHeight");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushstring(L2, "FontString"); return 1; }); // GetObjectType
	lua_setfield(L, -2, "GetObjectType");
	lua_pushcfunction(L, LF_FontStringSetFormattedText);
	lua_setfield(L, -2, "SetFormattedText");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetWordWrap
	lua_setfield(L, -2, "SetWordWrap");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetNonSpaceWrap
	lua_setfield(L, -2, "SetNonSpaceWrap");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetMaxLines
	lua_setfield(L, -2, "SetMaxLines");
	lua_pushcfunction(L, LF_FontStringGetStringWidth); // GetTextWidth (alias for GetStringWidth)
	lua_setfield(L, -2, "GetTextWidth");
	lua_pushcfunction(L, LF_FontStringSetTextColor); // SetVertexColor (alias for SetTextColor for fontstrings)
	lua_setfield(L, -2, "SetVertexColor");

	for (const TPair<const char*, lua_CFunction>& Method : {
		TPair<const char*, lua_CFunction>("SetWidth", LF_SetWidth),
		TPair<const char*, lua_CFunction>("SetHeight", LF_SetHeight),
		TPair<const char*, lua_CFunction>("SetSize", LF_SetSize),
		TPair<const char*, lua_CFunction>("GetWidth", LF_GetWidth),
		TPair<const char*, lua_CFunction>("GetHeight", LF_GetHeight),
		TPair<const char*, lua_CFunction>("IsShown", LF_IsShown),
		TPair<const char*, lua_CFunction>("IsVisible", LF_IsShown),
		TPair<const char*, lua_CFunction>("GetJustifyH", LF_FontStringGetJustifyH),
		TPair<const char*, lua_CFunction>("GetAlpha", LF_GetAlpha) })
	{
		lua_pushcfunction(L, Method.Value);
		lua_setfield(L, -2, Method.Key);
	}
	// Default justification of a new FontString is CENTER, as in the client.
	if (UTextBlock* Created = GetTextBlockWidgetForTable(L, lua_gettop(L)))
	{
		Created->SetJustification(ETextJustify::Center);
	}
	// Fall back to the frame metatable for everything not set above (GetParent,
	// SetDrawLayer, SetTextHeight, ...), instead of erroring on a nil method.
	luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
	if (lua_istable(L, -1)) lua_setmetatable(L, -2); else lua_pop(L, 1);

	// CreateFontString(name, layer, "GameFontNormal"): the inherited font object.
	if (lua_type(L, 4) == LUA_TSTRING)
	{
		const int Created = lua_gettop(L);
		lua_pushcfunction(L, LF_SetFontObject);
		lua_pushvalue(L, Created);
		lua_pushvalue(L, 4);
		lua_call(L, 2, 0);
	}

	if (Name && Name[0])
	{
		lua_pushvalue(L, -1);
		lua_setglobal(L, Name);
	}

	return 1;
}

// Global CreateTexture(name, parent, drawLayer, inherits, subLevel) - WoW 3.3.5 API
static int LF_CreateTextureGlobal(lua_State* L)
{
	// Create a minimal texture table with a metatable
	lua_newtable(L);

	// Store name if provided (arg 1)
	const char* Name = luaL_optstring(L, 1, nullptr);
	if (Name && Name[0]) {
		lua_pushstring(L, Name);
		lua_setfield(L, -2, "__name");
	}

	lua_pushstring(L, "Texture");
	lua_setfield(L, -2, "__objectType");

	// Store parent frame handle if provided (arg 2)
	int64 ParentHandle = -1;
	if (lua_istable(L, 2)) {
		lua_pushvalue(L, 2); // Push parent table to top
		ParentHandle = GetFrameHandle(L);
		lua_pop(L, 1); // Remove parent table copy
		lua_pushinteger(L, ParentHandle);
		lua_setfield(L, -2, "__parentHandle");
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager && ParentHandle >= 0)
	{
		if (UCanvasPanel* ParentCanvas = Cast<UCanvasPanel>(Ctx->FrameManager->GetWidgetForHandle(ParentHandle)))
		{
			UImage* ImageWidget = NewObject<UWowBlendImage>(ParentCanvas);
			FSlateBrush EmptyBrush;
			EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			ImageWidget->SetBrush(EmptyBrush);
			ImageWidget->SetRenderOpacity(0.0f);
			ImageWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* Slot = ParentCanvas->AddChildToCanvas(ImageWidget))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetAutoSize(true);
				Slot->SetZOrder(LuaRegionZOrder(L, 3));
			}
			// r237: a HIGHLIGHT-layer texture shows only while the mouse is over the frame
			// (AceGUI tabs, dropdown items and buttons drew their highlight glow all the time).
			if (IsHighlightLayerArg(L, 3))
			{
				Ctx->FrameManager->RegisterHighlightLayerRegion(ParentHandle, ImageWidget);
			}

			lua_pushlightuserdata(L, ImageWidget);
			lua_setfield(L, -2, "__imageWidget");
		}
	}

	// Store draw layer if provided (arg 3)
	const char* DrawLayer = luaL_optstring(L, 3, nullptr);
	if (DrawLayer && DrawLayer[0]) {
		lua_pushstring(L, DrawLayer);
		lua_setfield(L, -2, "__drawLayer");
	}

	// Store inherits template if provided (arg 4)
	const char* Inherits = luaL_optstring(L, 4, nullptr);
	if (Inherits && Inherits[0]) {
		lua_pushstring(L, Inherits);
		lua_setfield(L, -2, "__inherits");
	}

	// Store sublevel if provided (arg 5)
	if (lua_isnumber(L, 5)) {
		lua_pushvalue(L, 5);
		lua_setfield(L, -2, "__subLevel");
	}

	// Texture methods
	lua_pushcfunction(L, LF_TextureSetTexture);
	lua_setfield(L, -2, "SetTexture");
	lua_pushcfunction(L, LF_TextureSetTexCoord);
	lua_setfield(L, -2, "SetTexCoord");
	lua_pushcfunction(L, LF_TextureSetVertexColor);
	lua_setfield(L, -2, "SetVertexColor");
	lua_pushcfunction(L, LF_TextureSetAlpha);
	lua_setfield(L, -2, "SetAlpha");
	lua_pushcfunction(L, LF_TextureShow);
	lua_setfield(L, -2, "Show");
	lua_pushcfunction(L, LF_TextureHide);
	lua_setfield(L, -2, "Hide");

	// Common region methods
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetPoint
	lua_setfield(L, -2, "SetPoint");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; }); // GetWidth
	lua_setfield(L, -2, "GetWidth");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; }); // GetHeight
	lua_setfield(L, -2, "GetHeight");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetWidth
	lua_setfield(L, -2, "SetWidth");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetHeight
	lua_setfield(L, -2, "SetHeight");
	lua_pushcfunction(L, [](lua_State* L2) -> int {
		lua_pushstring(L2, "Texture");
		return 1;
	}); // GetObjectType
	lua_setfield(L, -2, "GetObjectType");

	if (Name && Name[0])
	{
		lua_pushvalue(L, -1);
		lua_setglobal(L, Name);
	}

	return 1;
}

// Global CreateFont() - WoW 3.3.5 API for creating font objects
static int LF_CreateFont(lua_State* L)
{
	// CreateFont("name") -> Font object (global when named), shared WowFont metatable.
	const char* FontName = (lua_type(L, 1) == LUA_TSTRING) ? lua_tostring(L, 1) : nullptr;
	PushNewFontObject(L, FontName);
	lua_pushstring(L, "Fonts\\FRIZQT__.TTF");
	lua_setfield(L, -2, "__fontFile");
	return 1;
}

// frame:SetBackdrop(backdrop)
static int LF_SetBackdrop(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);

	if (!lua_istable(L, 2))
	{
		lua_pushnil(L);
		lua_setfield(L, 1, "__backdrop");
		if (Ctx && Ctx->FrameManager) Ctx->FrameManager->SetFrameBackdrop(Handle, nullptr);
		return 0;
	}
	lua_pushvalue(L, 2);
	lua_setfield(L, 1, "__backdrop");

	// { bgFile, edgeFile, tile, tileSize, edgeSize, insets = { left, right, top, bottom } }
	FWowBackdrop Backdrop;
	lua_getfield(L, 2, "bgFile");
	if (lua_type(L, -1) == LUA_TSTRING) Backdrop.BgFile = UTF8_TO_TCHAR(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, 2, "edgeFile");
	if (lua_type(L, -1) == LUA_TSTRING) Backdrop.EdgeFile = UTF8_TO_TCHAR(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, 2, "tile");
	Backdrop.Tile = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	lua_getfield(L, 2, "tileSize");
	Backdrop.TileSize = lua_isnumber(L, -1) ? static_cast<int32>(lua_tonumber(L, -1)) : 0;
	lua_pop(L, 1);
	lua_getfield(L, 2, "edgeSize");
	Backdrop.EdgeSize = lua_isnumber(L, -1) ? static_cast<int32>(lua_tonumber(L, -1)) : 0;
	lua_pop(L, 1);
	lua_getfield(L, 2, "insets");
	if (lua_istable(L, -1))
	{
		auto Inset = [L](const char* Key) { lua_getfield(L, -1, Key); const float V = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f; lua_pop(L, 1); return V; };
		Backdrop.InsetLeft = Inset("left");
		Backdrop.InsetRight = Inset("right");
		Backdrop.InsetTop = Inset("top");
		Backdrop.InsetBottom = Inset("bottom");
	}
	lua_pop(L, 1);
	if (Backdrop.EdgeSize <= 0 && !Backdrop.EdgeFile.IsEmpty()) Backdrop.EdgeSize = 16; // client default edge size
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameBackdrop(Handle, &Backdrop);
		// A new backdrop keeps colours already set on the frame.
		for (const char* Field : { "__backdropColor", "__backdropBorderColor" })
		{
			lua_getfield(L, 1, Field);
			if (lua_istable(L, -1))
			{
				float C[4] = { 1.f, 1.f, 1.f, 1.f };
				for (int32 I = 0; I < 4; ++I) { lua_rawgeti(L, -1, I + 1); if (lua_isnumber(L, -1)) C[I] = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1); }
				Ctx->FrameManager->SetFrameBackdropColor(Handle, WowColor::FromWow(C[0], C[1], C[2], C[3]), Field[10] == 'B');
			}
			lua_pop(L, 1);
		}
	}
	return 0;
}

// Pushes the colour table built by SetBackdropColor/BorderColor into the frame's backdrop.
static void ApplyBackdropColorTable(lua_State* L, bool bBorder)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !lua_istable(L, -1)) return;
	float C[4] = { 1.f, 1.f, 1.f, 1.f };
	for (int32 I = 0; I < 4; ++I) { lua_rawgeti(L, -1, I + 1); if (lua_isnumber(L, -1)) C[I] = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1); }
	Ctx->FrameManager->SetFrameBackdropColor(GetFrameHandle(L), WowColor::FromWow(C[0], C[1], C[2], C[3]), bBorder);
}

// Helper: read a color value from the stack, tolerating tables at any position
static double SafeColorArg(lua_State* L, int idx, double def)
{
	if (lua_isnumber(L, idx)) return lua_tonumber(L, idx);
	if (lua_istable(L, idx)) return def; // skip tables silently
	return def;
}

// frame:SetBackdropColor(r, g, b, a) or frame:SetBackdropColor(colorTable)
static int LF_SetBackdropColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_newtable(L);

	if (lua_type(L, 2) == LUA_TTABLE)
	{
		lua_getfield(L, 2, "r"); double r = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "g"); double g = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "b"); double b = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "a"); double a = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 1.0; lua_pop(L, 1);
		lua_pushnumber(L, r); lua_rawseti(L, -2, 1);
		lua_pushnumber(L, g); lua_rawseti(L, -2, 2);
		lua_pushnumber(L, b); lua_rawseti(L, -2, 3);
		lua_pushnumber(L, a); lua_rawseti(L, -2, 4);
	}
	else
	{
		lua_pushnumber(L, SafeColorArg(L, 2, 1.0)); lua_rawseti(L, -2, 1);
		lua_pushnumber(L, SafeColorArg(L, 3, 1.0)); lua_rawseti(L, -2, 2);
		lua_pushnumber(L, SafeColorArg(L, 4, 1.0)); lua_rawseti(L, -2, 3);
		lua_pushnumber(L, SafeColorArg(L, 5, 1.0)); lua_rawseti(L, -2, 4);
	}

	ApplyBackdropColorTable(L, false);
	lua_setfield(L, 1, "__backdropColor");
	return 0;
}

// frame:SetBackdropBorderColor(r, g, b, a) or frame:SetBackdropBorderColor(colorTable)
static int LF_SetBackdropBorderColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_newtable(L);

	if (lua_type(L, 2) == LUA_TTABLE)
	{
		lua_getfield(L, 2, "r"); double r = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "g"); double g = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "b"); double b = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "a"); double a = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 1.0; lua_pop(L, 1);
		lua_pushnumber(L, r); lua_rawseti(L, -2, 1);
		lua_pushnumber(L, g); lua_rawseti(L, -2, 2);
		lua_pushnumber(L, b); lua_rawseti(L, -2, 3);
		lua_pushnumber(L, a); lua_rawseti(L, -2, 4);
	}
	else
	{
		lua_pushnumber(L, SafeColorArg(L, 2, 1.0)); lua_rawseti(L, -2, 1);
		lua_pushnumber(L, SafeColorArg(L, 3, 1.0)); lua_rawseti(L, -2, 2);
		lua_pushnumber(L, SafeColorArg(L, 4, 1.0)); lua_rawseti(L, -2, 3);
		lua_pushnumber(L, SafeColorArg(L, 5, 1.0)); lua_rawseti(L, -2, 4);
	}

	ApplyBackdropColorTable(L, true);
	lua_setfield(L, 1, "__backdropBorderColor");
	return 0;
}

// frame:SetID(id)
static int LF_SetID(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 2);
	lua_setfield(L, 1, "__id");
	return 0;
}

// frame:GetID()
static int LF_GetID(lua_State* L)
{
	lua_getfield(L, 1, "__id");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 0);
	}
	return 1;
}

// frame:SetAttribute(name, value)
static int LF_SetAttribute(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	const char* AttrName = luaL_checkstring(L, 2);
	const int Top = lua_gettop(L);
	// Store as __attr_<name> on the frame table
	FString Key = FString::Printf(TEXT("__attr_%s"), UTF8_TO_TCHAR(AttrName));
	FTCHARToUTF8 UTF8Key(*Key);
	if (Top >= 3)
	{
		lua_pushvalue(L, 3);
	}
	else
	{
		lua_pushnil(L);
	}
	lua_setfield(L, 1, UTF8Key.Get());

	const int64 Handle = GetOptionalFrameHandle(L);
	if (Handle >= 0)
	{
		FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_OnAttributeChanged"), Handle);
		FTCHARToUTF8 UTF8GlobalKey(*GlobalKey);
		lua_getglobal(L, UTF8GlobalKey.Get());
		if (lua_isfunction(L, -1))
		{
			lua_pushvalue(L, 1);
			lua_pushvalue(L, 2);
			if (Top >= 3)
			{
				lua_pushvalue(L, 3);
			}
			else
			{
				lua_pushnil(L);
			}

			if (lua_pcall(L, 3, 0, 0) != 0)
			{
				UE_LOG(LogWowLuaFrame, Warning, TEXT("SetAttribute OnAttributeChanged error [%lld] %s: %s"),
					Handle, UTF8_TO_TCHAR(AttrName), UTF8_TO_TCHAR(lua_tostring(L, -1)));
				lua_pop(L, 1);
			}
		}
		else
		{
			lua_pop(L, 1);
		}
	}
	return 0;
}

// frame:GetAttribute(name)
static int LF_GetAttribute(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	// r270 (Stefan 13.10: every Bartender button showed action 1): GetAttribute(prefix, name, suffix)
	// is a lookup with wildcards in the client, not a plain concatenation. SecureButton_GetModifiedAttribute
	// asks ("", "action", "1"); Bartender sets plain "action", so the old concatenated key "action1"
	// was never found and ActionButton_CalculateAction fell back to action 1 on every button.
	// Client order: prefix..name..suffix, *name..suffix, prefix..name*, *name*, name..suffix, name*,
	// prefix..name, *name, name.
	if (lua_gettop(L) >= 4 && lua_isstring(L, 3))
	{
		const FString Prefix = lua_isstring(L, 2) ? FString(UTF8_TO_TCHAR(lua_tostring(L, 2))) : FString();
		const FString Name = UTF8_TO_TCHAR(lua_tostring(L, 3));
		const FString Suffix = lua_isstring(L, 4) ? FString(UTF8_TO_TCHAR(lua_tostring(L, 4))) : FString();
		const FString Candidates[] = {
			Prefix + Name + Suffix, TEXT("*") + Name + Suffix, Prefix + Name + TEXT("*"), TEXT("*") + Name + TEXT("*"),
			Name + Suffix, Name + TEXT("*"), Prefix + Name, TEXT("*") + Name, Name };
		for (const FString& Candidate : Candidates)
		{
			const FString CandidateKey = FString::Printf(TEXT("__attr_%s"), *Candidate);
			lua_getfield(L, 1, TCHAR_TO_UTF8(*CandidateKey));
			if (!lua_isnil(L, -1)) return 1;
			lua_pop(L, 1);
		}
		lua_pushnil(L);
		return 1;
	}
	FString AttrName;
	int32 NumArgs = lua_gettop(L) - 1; // exclude self
	for (int32 i = 2; i <= lua_gettop(L); i++)
	{
		const char* Part = lua_tostring(L, i);
		if (Part)
		{
			AttrName += UTF8_TO_TCHAR(Part);
		}
	}
	if (AttrName.IsEmpty())
	{
		lua_pushnil(L);
		return 1;
	}
	FString Key = FString::Printf(TEXT("__attr_%s"), *AttrName);
	FTCHARToUTF8 UTF8Key(*Key);
	lua_getfield(L, 1, UTF8Key.Get());
	return 1;
}

// frame:HookScript(scriptType, handler)
// r267 (Stefan 12.10, r09t41: spellbook with empty slots and no tabs): HookScript behaved like
// SetScript, so a hook replaced the frame's own handler. The client's SpellBookFrame OnShow hooks
// (bookType fix, page tidy) replaced SpellBookFrame_OnShow - SpellBookFrame_Update(1) never ran, the
// tabs stayed hidden and every slot empty (and no open sound). Addons' HookScript calls (Bartender,
// Ace, LibDBIcon ...) lost the hooked frame's handler the same way. Now: the original runs first,
// then the hook, with the same arguments, as in the client.
static int HookedScriptThunk(lua_State* L)
{
	const int32 NumArgs = lua_gettop(L);
	for (int32 Upvalue = 1; Upvalue <= 2; ++Upvalue)
	{
		lua_pushvalue(L, lua_upvalueindex(Upvalue));
		for (int32 Arg = 1; Arg <= NumArgs; ++Arg) lua_pushvalue(L, Arg);
		lua_call(L, NumArgs, 0);
	}
	return 0;
}

static int LF_HookScript(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	const char* ScriptType = luaL_checkstring(L, 2);
	if (!lua_isfunction(L, 3) || !Ctx || !Ctx->EventSystem || Handle < 0) return LF_SetScript(L);
	lua_settop(L, 3);
	// The current handler: one set from Lua (GetScript's global) or the XML/compiled one.
	FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_%s"), Handle, UTF8_TO_TCHAR(ScriptType));
	lua_getglobal(L, TCHAR_TO_UTF8(*GlobalKey));
	if (!lua_isfunction(L, -1))
	{
		lua_pop(L, 1);
		if (!Ctx->EventSystem->PushFrameScript(Handle, UTF8_TO_TCHAR(ScriptType))) return LF_SetScript(L);
	}
	lua_pushvalue(L, 3);
	lua_pushcclosure(L, HookedScriptThunk, 2);
	lua_replace(L, 3);
	return LF_SetScript(L);
}

// frame:HasScript(scriptType)
static int LF_HasScript(lua_State* L)
{
	lua_pushboolean(L, 1);
	return 1;
}

// Helper: compute frame position from first anchor and dimensions
static void GetFrameRect(lua_State* L, float& OutLeft, float& OutBottom, float& OutRight, float& OutTop)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	OutLeft = OutBottom = OutRight = OutTop = 0.f;

	if (!Ctx || !Ctx->FrameManager) return;
	const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
	if (!Def) return;
	// r384 (Stefan 16.10: the objective tracker stayed empty with 4 quests tracked): WatchFrame
	// anchors each line under the last one and asks lastLine:GetBottom() at once to see whether the
	// quest still fits; the line was not laid out yet, so it read the anchor offset (near 0), every
	// quest "did not fit" and was dropped. Like the client, a rect read resolves pending layout first.
	Ctx->FrameManager->FlushDependentLayout();

	// The real laid-out rect: LibDBIcon and other drag code compare GetCenter() with
	// GetCursorPosition(), so the old anchor-offset guess made minimap buttons orbit.
	if (Ctx->FrameManager->GetFrameScreenRect(Handle, OutLeft, OutBottom, OutRight, OutTop)) return;

	float W = Def->Width;
	float H = Def->Height;
	float X = 0.f, Y = 0.f;

	if (!Def->Anchors.IsEmpty())
	{
		X = Def->Anchors[0].OffsetX;
		Y = Def->Anchors[0].OffsetY;
	}

	// WoW UI coordinates: origin bottom-left, Y goes up
	// Approximate position from anchor offset
	OutLeft = X;
	OutBottom = -Y; // WoW Y is inverted vs UMG
	OutRight = X + W;
	OutTop = -Y + H;
}

// frame:GetCenter()
static int LF_GetCenter(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, (Left + Right) * 0.5f);
	lua_pushnumber(L, (Bottom + Top) * 0.5f);
	return 2;
}

// frame:GetLeft/Right/Top/Bottom
static int LF_GetLeft(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, Left);
	return 1;
}

static int LF_GetRight(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, Right);
	return 1;
}

static int LF_GetTop(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, Top);
	return 1;
}

static int LF_GetBottom(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, Bottom);
	return 1;
}

// button:SetHighlightTexture(textureRegion) / SetPushedTexture / SetDisabledTexture:
// the region becomes that state's art and is only shown while the state is active.
static bool TryAssignButtonStateRegion(lua_State* L, EWowButtonStateRegion State, const char* Field)
{
	if (!lua_istable(L, 1) || !lua_istable(L, 2)) return false;
	UImage* Image = GetTextureImage(L, 2);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetOptionalFrameHandle(L, 1);
	if (!Image || !Ctx || !Ctx->FrameManager || Handle < 0) return false;
	Ctx->FrameManager->SetButtonStateRegion(Handle, State, Image);
	if (State == EWowButtonStateRegion::Disabled)
	{
		lua_getfield(L, 1, "__enabled");
		const bool bDisabled = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
		lua_pop(L, 1);
		Ctx->FrameManager->SetButtonStateRegionShown(Handle, State, bDisabled);
	}
	lua_pushvalue(L, 2);
	lua_setfield(L, 1, Field);
	return true;
}

static int PushButtonStateRegion(lua_State* L, const char* Field)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, Field);
	if (lua_istable(L, -1)) return 1;
	lua_pop(L, 1);
	lua_newtable(L);
	luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
	if (!lua_isnil(L, -1)) lua_setmetatable(L, -2); else lua_pop(L, 1);
	return 1;
}

// r265 (Stefan 12.10, calendar): button:GetNormalTexture() for a NormalTexture without a name
// returned a detached stub, so CalendarFrame_InitDay's SetTexCoord (one 91 x 91 parchment tile of
// CalendarBackground per day) did nothing and every day showed the whole 2 x 2 texture. The
// object now edits the button's own normal brush; other methods behave as before.
static UButton* NormalProxyButton(lua_State* L)
{
	if (!lua_istable(L, 1)) return nullptr;
	lua_getfield(L, 1, "__owner");
	UButton* Button = lua_istable(L, -1) ? GetButtonWidgetForTable(L, lua_gettop(L)) : nullptr;
	lua_pop(L, 1);
	return Button;
}
static void EditNormalBrush(UButton* Button, TFunctionRef<void(FSlateBrush&)> Edit)
{
	FButtonStyle Style = Button->GetStyle();
	const UObject* Resource = Style.Normal.GetResourceObject();
	Edit(Style.Normal);
	// Hovered/Pressed drawing the same image follow it (the highlight is its own region).
	if (Resource && Style.Hovered.GetResourceObject() == Resource) Edit(Style.Hovered);
	if (Resource && Style.Pressed.GetResourceObject() == Resource) Edit(Style.Pressed);
	// r278: the Disabled state draws the normal image too (FrameManager sets all four from it).
	if (Resource && Style.Disabled.GetResourceObject() == Resource) Edit(Style.Disabled);
	Button->SetStyle(Style);
}
static int NormalProxy_SetTexCoord(lua_State* L)
{
	UButton* Button = NormalProxyButton(L);
	// r278 diagnostic (Stefan 13.10: calendar days still showed the whole 2 x 2 texture after r265).
	static int32 GLoggedProxyTexCoords = 0;
	if (GLoggedProxyTexCoords < 6)
	{
		++GLoggedProxyTexCoords;
		FString OwnerName = TEXT("?");
		if (lua_istable(L, 1))
		{
			lua_getfield(L, 1, "__owner");
			if (lua_istable(L, -1))
			{
				lua_getfield(L, -1, "__name");
				if (lua_isstring(L, -1)) OwnerName = UTF8_TO_TCHAR(lua_tostring(L, -1));
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		UE_LOG(LogWowLuaFrame, Log, TEXT("NormalTexture SetTexCoord: %s button=%s enabled=%d resource=%s args=%d"), *OwnerName,
			Button ? TEXT("yes") : TEXT("NO"), Button ? Button->GetIsEnabled() : 0,
			Button && Button->GetStyle().Normal.GetResourceObject() ? *Button->GetStyle().Normal.GetResourceObject()->GetName() : TEXT("none"),
			lua_gettop(L));
	}
	if (!Button) return 0;
	double Left, Right, Top, Bottom;
	if (lua_gettop(L) >= 9)
	{
		// ULx, ULy, LLx, LLy, URx, URy, LRx, LRy
		Left = lua_tonumber(L, 2); Top = lua_tonumber(L, 3); Bottom = lua_tonumber(L, 5); Right = lua_tonumber(L, 6);
	}
	else
	{
		Left = luaL_optnumber(L, 2, 0); Right = luaL_optnumber(L, 3, 1); Top = luaL_optnumber(L, 4, 0); Bottom = luaL_optnumber(L, 5, 1);
	}
	EditNormalBrush(Button, [&](FSlateBrush& Brush)
	{
		Brush.SetUVRegion(FBox2D(FVector2D(FMath::Min(Left, Right), FMath::Min(Top, Bottom)), FVector2D(FMath::Max(Left, Right), FMath::Max(Top, Bottom))));
		Brush.Mirroring = Left > Right
			? (Top > Bottom ? ESlateBrushMirrorType::Both : ESlateBrushMirrorType::Horizontal)
			: (Top > Bottom ? ESlateBrushMirrorType::Vertical : ESlateBrushMirrorType::NoMirror);
	});
	return 0;
}
static int NormalProxy_SetVertexColor(lua_State* L)
{
	if (UButton* Button = NormalProxyButton(L))
	{
		const FLinearColor Color(float(luaL_optnumber(L, 2, 1)), float(luaL_optnumber(L, 3, 1)), float(luaL_optnumber(L, 4, 1)), float(luaL_optnumber(L, 5, 1)));
		EditNormalBrush(Button, [&](FSlateBrush& Brush) { Brush.TintColor = FSlateColor(Color); });
	}
	return 0;
}
static int NormalProxy_SetAlpha(lua_State* L)
{
	if (UButton* Button = NormalProxyButton(L))
	{
		const float Alpha = static_cast<float>(luaL_optnumber(L, 2, 1));
		EditNormalBrush(Button, [&](FSlateBrush& Brush)
		{
			FLinearColor Color = Brush.TintColor.GetSpecifiedColor(); Color.A = Alpha; Brush.TintColor = FSlateColor(Color);
		});
	}
	return 0;
}
static int NormalProxy_GetTexture(lua_State* L)
{
	if (!lua_istable(L, 1)) return 0;
	lua_getfield(L, 1, "__owner");
	if (!lua_istable(L, -1)) return 0;
	lua_getfield(L, -1, "__normalTexture");
	return 1;
}
static int NormalProxy_GetParent(lua_State* L)
{
	if (!lua_istable(L, 1)) return 0;
	lua_getfield(L, 1, "__owner");
	return 1;
}
static int NormalProxy_Nothing(lua_State* L) { return 0; }
static int NormalProxy_True(lua_State* L) { lua_pushboolean(L, 1); return 1; }
static int NormalProxy_ObjectType(lua_State* L) { lua_pushstring(L, "Texture"); return 1; }

static int PushButtonNormalProxy(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "__normalProxy");
	if (lua_istable(L, -1)) return 1;
	lua_pop(L, 1);
	lua_newtable(L);
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "__owner");
	if (luaL_newmetatable(L, "WowButtonNormalProxy"))
	{
		lua_newtable(L); // methods; anything else falls through to the frame methods (the old stub)
		static const luaL_Reg Methods[] = {
			{"SetTexCoord", NormalProxy_SetTexCoord}, {"SetVertexColor", NormalProxy_SetVertexColor},
			{"SetAlpha", NormalProxy_SetAlpha}, {"GetTexture", NormalProxy_GetTexture}, {"GetParent", NormalProxy_GetParent},
			{"SetDrawLayer", NormalProxy_Nothing}, {"SetBlendMode", NormalProxy_Nothing}, {"SetDesaturated", NormalProxy_Nothing},
			{"IsShown", NormalProxy_True}, {"IsVisible", NormalProxy_True}, {"GetObjectType", NormalProxy_ObjectType},
			{nullptr, nullptr}};
		for (const luaL_Reg* Method = Methods; Method->name; ++Method)
		{
			lua_pushcfunction(L, Method->func);
			lua_setfield(L, -2, Method->name);
		}
		luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
		if (!lua_isnil(L, -1)) lua_setmetatable(L, -2); else lua_pop(L, 1);
		lua_setfield(L, -2, "__index");
	}
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	lua_setfield(L, 1, "__normalProxy");
	return 1;
}

// frame:SetTexture(texture) — for Button Normal/Pushed/etc textures
static int LF_SetNormalTexture(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_isnil(L, 2) || lua_isnone(L, 2)) return 0;

	const char* TexturePath = "";
	if (lua_isstring(L, 2)) {
		TexturePath = lua_tostring(L, 2);
	} else if (lua_istable(L, 2)) {
		// Handle texture object tables - extract path if available
		lua_getfield(L, 2, "__texturePath");
		if (lua_isstring(L, -1)) {
			TexturePath = lua_tostring(L, -1);
		}
		lua_pop(L, 1);
		// If no path, silently accept the texture table
		if (!TexturePath || !TexturePath[0]) return 0;
	} else {
		// Silently ignore other types to prevent errors
		return 0;
	}

	// Store in Lua table
	lua_pushstring(L, TexturePath);
	lua_setfield(L, 1, "__normalTexture");

	// Update UMG widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		if (UButton* Button = GetButtonWidgetForTable(L))
		{
			UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));
			if (Texture)
			{
				FSlateBrush NormalBrush;
				NormalBrush.SetResourceObject(Texture);
				NormalBrush.DrawAs = ESlateBrushDrawType::Image;
				FButtonStyle ButtonStyle = Button->GetStyle();
				const bool bHadNoXmlNormal = ButtonStyle.Normal.DrawAs == ESlateBrushDrawType::NoDrawType;
				ButtonStyle.Normal = NormalBrush;
				// r310 (Stefan 13.10: the spellbook's skill-line tabs showed only their socket art, so the
				// class tabs - and Smite - could not be found): a button's state textures sit under its
				// BACKGROUND/BORDER layers here (z -100), but in the client NormalTexture is ARTWORK, above
				// them. With an opaque socket texture the icon set from Lua was covered. A NormalTexture
				// that only Lua sets (no XML one, like SpellBookSkillLineTabN) goes between BORDER and
				// ARTWORK, its check overlay just above it.
				if (bHadNoXmlNormal)
					if (UCanvasPanelSlot* ButtonSlot = Cast<UCanvasPanelSlot>(Button->Slot); ButtonSlot && ButtonSlot->GetZOrder() < 150)
					{
						ButtonSlot->SetZOrder(150);
						if (UCanvasPanel* Container = Cast<UCanvasPanel>(Button->GetParent()))
							for (UWidget* Child : Container->GetAllChildren())
								if (UCanvasPanelSlot* ChildSlot = Child ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
									ChildSlot && Child->IsA<UImage>() && ChildSlot->GetZOrder() == -50)
									ChildSlot->SetZOrder(160);
					}
				// The button draws its states through its own (never hovered) Slate states; a frame
				// created without an XML NormalTexture was made transparent, which hid an icon set
				// later from Lua (Stefan 09.10: spellbook skill-line tabs had no icons).
				if (ButtonStyle.Hovered.DrawAs == ESlateBrushDrawType::NoDrawType) ButtonStyle.Hovered = NormalBrush;
				if (ButtonStyle.Pressed.DrawAs == ESlateBrushDrawType::NoDrawType) ButtonStyle.Pressed = NormalBrush;
				Button->SetStyle(ButtonStyle);
				Button->SetBackgroundColor(FLinearColor::White);
			}
		}
	}

	return 0;
}

static int LF_SetPushedTexture(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_isnil(L, 2) || lua_isnone(L, 2)) return 0;

	const char* TexturePath = "";
	if (lua_isstring(L, 2)) {
		TexturePath = lua_tostring(L, 2);
	} else if (lua_istable(L, 2)) {
		if (TryAssignButtonStateRegion(L, EWowButtonStateRegion::Pushed, "__pushedRegion")) return 0;
		// Handle texture object tables - extract path if available
		lua_getfield(L, 2, "__texturePath");
		if (lua_isstring(L, -1)) {
			TexturePath = lua_tostring(L, -1);
		}
		lua_pop(L, 1);
		// If no path, silently accept the texture table
		if (!TexturePath || !TexturePath[0]) return 0;
	} else {
		// Silently ignore other types to prevent errors
		return 0;
	}

	// Store in Lua table
	lua_pushstring(L, TexturePath);
	lua_setfield(L, 1, "__pushedTexture");

	// Update UMG widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		if (UButton* Button = GetButtonWidgetForTable(L))
		{
			UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));
			if (Texture)
			{
				FSlateBrush PressedBrush;
				PressedBrush.SetResourceObject(Texture);
				PressedBrush.DrawAs = ESlateBrushDrawType::Image;
				FButtonStyle ButtonStyle = Button->GetStyle();
				ButtonStyle.Pressed = PressedBrush;
				Button->SetStyle(ButtonStyle);
			}
		}
	}

	return 0;
}

static int LF_SetHighlightTexture(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_isnil(L, 2) || lua_isnone(L, 2)) return 0;

	const char* TexturePath = "";
	if (lua_isstring(L, 2)) {
		TexturePath = lua_tostring(L, 2);
	} else if (lua_istable(L, 2)) {
		if (TryAssignButtonStateRegion(L, EWowButtonStateRegion::Highlight, "__highlightRegion")) return 0;
		// Handle texture object tables - extract path if available
		lua_getfield(L, 2, "__texturePath");
		if (lua_isstring(L, -1)) {
			TexturePath = lua_tostring(L, -1);
		}
		lua_pop(L, 1);
		// If no path, silently accept the texture table
		if (!TexturePath || !TexturePath[0]) return 0;
	} else {
		// Silently ignore other types to prevent errors
		return 0;
	}

	// Store in Lua table
	lua_pushstring(L, TexturePath);
	lua_setfield(L, 1, "__highlightTexture");

	// Update UMG widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		if (UButton* Button = GetButtonWidgetForTable(L))
		{
			UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));
			if (Texture)
			{
				FSlateBrush HoveredBrush;
				HoveredBrush.SetResourceObject(Texture);
				HoveredBrush.DrawAs = ESlateBrushDrawType::Image;
				FButtonStyle ButtonStyle = Button->GetStyle();
				ButtonStyle.Hovered = HoveredBrush;
				Button->SetStyle(ButtonStyle);
			}
		}
	}

	return 0;
}

static int LF_SetDisabledTexture(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_isnil(L, 2) || lua_isnone(L, 2)) return 0;

	const char* TexturePath = "";
	if (lua_isstring(L, 2)) {
		TexturePath = lua_tostring(L, 2);
	} else if (lua_istable(L, 2)) {
		if (TryAssignButtonStateRegion(L, EWowButtonStateRegion::Disabled, "__disabledRegion")) return 0;
		// Handle texture object tables - extract path if available
		lua_getfield(L, 2, "__texturePath");
		if (lua_isstring(L, -1)) {
			TexturePath = lua_tostring(L, -1);
		}
		lua_pop(L, 1);
		// If no path, silently accept the texture table
		if (!TexturePath || !TexturePath[0]) return 0;
	} else {
		// Silently ignore other types to prevent errors
		return 0;
	}

	// Store in Lua table
	lua_pushstring(L, TexturePath);
	lua_setfield(L, 1, "__disabledTexture");

	// Update UMG widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		if (UButton* Button = GetButtonWidgetForTable(L))
		{
			UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));
			if (Texture)
			{
				FSlateBrush DisabledBrush;
				DisabledBrush.SetResourceObject(Texture);
				DisabledBrush.DrawAs = ESlateBrushDrawType::Image;
				FButtonStyle ButtonStyle = Button->GetStyle();
				ButtonStyle.Disabled = DisabledBrush;
				Button->SetStyle(ButtonStyle);
			}
		}
	}

	return 0;
}

// r263 (Stefan 12.10: the death dialog read "6 |4Minute:Minutes; until release"): the client's
// grammar escape |4singular:plural; picks a form by the number written before it
// (DEATH_RELEASE_TIMER = "%d |4Minute:Minutes; until release", SECONDS / MINUTES ...).
static FString ResolveWowGrammar(const FString& In)
{
	if (!In.Contains(TEXT("|4"))) return In;
	FString Out;
	for (int32 I = 0; I < In.Len();)
	{
		const int32 Esc = In.Find(TEXT("|4"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
		if (Esc == INDEX_NONE) { Out += In.Mid(I); break; }
		const int32 End = In.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Esc + 2);
		if (End == INDEX_NONE) { Out += In.Mid(I); break; }
		Out += In.Mid(I, Esc - I);
		TArray<FString> Forms;
		In.Mid(Esc + 2, End - Esc - 2).ParseIntoArray(Forms, TEXT(":"), false);
		// The last number before the escape (spaces skipped).
		int32 P = Out.Len() - 1;
		while (P >= 0 && FChar::IsWhitespace(Out[P])) --P;
		int32 DigitsEnd = P;
		while (P >= 0 && (FChar::IsDigit(Out[P]) || Out[P] == TEXT('.'))) --P;
		const bool bHasNumber = DigitsEnd > P;
		const double Number = bHasNumber ? FCString::Atod(*Out.Mid(P + 1, DigitsEnd - P)) : 0.0;
		if (Forms.Num() > 0)
			Out += (bHasNumber && Number == 1.0) || Forms.Num() == 1 ? Forms[0] : Forms[1];
		I = End + 1;
	}
	return Out;
}

// Button methods
// r293 (Stefan 13.10, /bt4: the tree showed "|cff808080Stance Bar|r"): fontstrings are one-colour
// text blocks, so the client's colour escapes were drawn as text. They are removed; a text that is
// one colour run (the usual "|cffRRGGBBname|r") takes that colour, and the block's own colour comes
// back when a later text has none. Links keep their visible text.
static bool StripWowColorMarkup(const FString& In, FString& Out, FLinearColor& OutColor, bool& bOneColor)
{
	Out.Reset();
	bOneColor = false;
	if (!In.Contains(TEXT("|"))) { Out = In; return false; }
	bool bChanged = false, bHasColor = false, bMixed = false, bAnyPlain = false;
	FLinearColor Current = FLinearColor::White, First = FLinearColor::White;
	bool bCurrentSet = false;
	auto Hex = [](TCHAR C) { return FChar::IsHexDigit(C); };
	for (int32 I = 0; I < In.Len(); ++I)
	{
		const TCHAR C = In[I];
		if (C == TEXT('|') && I + 1 < In.Len())
		{
			const TCHAR N = In[I + 1];
			if ((N == TEXT('c') || N == TEXT('C')) && I + 9 < In.Len())
			{
				bool bHex = true;
				for (int32 K = 2; K < 10; ++K) bHex &= Hex(In[I + K]);
				if (bHex)
				{
					const FColor Parsed = FColor::FromHex(In.Mid(I + 4, 6));
					Current = FLinearColor::FromSRGBColor(Parsed);
					bCurrentSet = true;
					I += 9; bChanged = true; continue;
				}
			}
			if (N == TEXT('r') || N == TEXT('R')) { bCurrentSet = false; ++I; bChanged = true; continue; }
			if (N == TEXT('H'))
			{
				const int32 End = In.Find(TEXT("|h"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I + 2);
				if (End != INDEX_NONE) { I = End + 1; bChanged = true; continue; }
			}
			if (N == TEXT('h')) { ++I; bChanged = true; continue; }
			if (N == TEXT('|')) { Out.AppendChar(TEXT('|')); ++I; bChanged = true; continue; }
		}
		Out.AppendChar(C);
		if (FChar::IsWhitespace(C)) continue;
		if (!bCurrentSet) { bAnyPlain = true; continue; }
		if (!bHasColor) { First = Current; bHasColor = true; }
		else if (!First.Equals(Current, 0.002f)) bMixed = true;
	}
	bOneColor = bHasColor && !bMixed && !bAnyPlain;
	OutColor = First;
	return bChanged;
}

static int LF_SetText(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	const char* RawText = luaL_optstring(L, 2, ""); // WoW treats nil as empty string
	const FString Resolved = ResolveWowGrammar(UTF8_TO_TCHAR(RawText));
	FTCHARToUTF8 ResolvedUtf8(*Resolved);
	const char* Text = ResolvedUtf8.Get();

	// Store in Lua table
	lua_pushstring(L, Text);
	lua_setfield(L, 1, "__text");

	// Check if this is a tooltip frame
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L, 1);
		const FWowFrameDef* FrameDef = Ctx->FrameManager->GetFrameDef(Handle);
		if (FrameDef && FrameDef->Type == EWowFrameType::GameTooltip)
		{
			// This is a tooltip - use tooltip SetText
			Ctx->FrameManager->TooltipSetText(Handle, UTF8_TO_TCHAR(Text));
			return 0;
		}
	}

	// Update UMG widget for other frame types
	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		FString Plain;
		FLinearColor RunColor;
		bool bOneColor = false;
		const FString Source = UTF8_TO_TCHAR(Text);
		const bool bMarkup = StripWowColorMarkup(Source, Plain, RunColor, bOneColor);
		const TWeakObjectPtr<UTextBlock> Key(TextBlock);
		if (bMarkup && bOneColor)
		{
			if (!GWowMarkupBaseColors.Contains(Key)) GWowMarkupBaseColors.Add(Key, TextBlock->GetColorAndOpacity());
			TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(RunColor.R, RunColor.G, RunColor.B,
				TextBlock->GetColorAndOpacity().GetSpecifiedColor().A)));
		}
		else if (const FSlateColor* Base = GWowMarkupBaseColors.Find(Key))
		{
			TextBlock->SetColorAndOpacity(*Base);
			GWowMarkupBaseColors.Remove(Key);
		}
		TextBlock->SetText(FText::FromString(bMarkup ? Plain : Source));
	}
	else if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L, 1))
	{
		FText EditContent = FText::FromString(UTF8_TO_TCHAR(Text));
		EditBox->SetText(EditContent);
	}

	return 0;
}

static int LF_GetText(lua_State* L)
{
	// Try to get text from widget first
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		// r293: the text as set (with its colour escapes), like the client; the widget shows it stripped.
		lua_getfield(L, 1, "__text");
		if (lua_isstring(L, -1) && FCStringAnsi::Strchr(lua_tostring(L, -1), '|'))
		{
			FString Plain; FLinearColor Unused; bool bUnused = false;
			StripWowColorMarkup(UTF8_TO_TCHAR(lua_tostring(L, -1)), Plain, Unused, bUnused);
			if (Plain == TextBlock->GetText().ToString()) return 1; // still the text Lua set
		}
		lua_pop(L, 1);
		FString TextContent = TextBlock->GetText().ToString();
		lua_pushstring(L, (const char*)StringCast<UTF8CHAR>(*TextContent).Get());
		return 1;
	}
	else if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L, 1))
	{
		FString EditText = EditBox->GetText().ToString();
		lua_pushstring(L, (const char*)StringCast<UTF8CHAR>(*EditText).Get());
		return 1;
	}

	// Fallback to stored value
	lua_getfield(L, 1, "__text");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushstring(L, "");
	}
	return 1;
}

static int LF_Disable(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 0);
	lua_setfield(L, 1, "__enabled");
	if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager)
		Ctx->FrameManager->SetButtonStateRegionShown(GetOptionalFrameHandle(L, 1), EWowButtonStateRegion::Disabled, true);
	if (UButton* Button = GetButtonWidgetForTable(L, 1)) Button->SetIsEnabled(false);
	if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L, 1)) EditBox->SetIsEnabled(false);
	return 0;
}

static int LF_Enable(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 1);
	lua_setfield(L, 1, "__enabled");
	if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager)
		Ctx->FrameManager->SetButtonStateRegionShown(GetOptionalFrameHandle(L, 1), EWowButtonStateRegion::Disabled, false);
	if (UButton* Button = GetButtonWidgetForTable(L, 1)) Button->SetIsEnabled(true);
	if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L, 1)) EditBox->SetIsEnabled(true);
	return 0;
}

static int LF_IsEnabled(lua_State* L)
{
	lua_getfield(L, 1, "__enabled");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushboolean(L, 1);
	}
	return 1;
}

// r291: Button:Click([button[, down]]) runs the whole click like a mouse click (PreClick, the secure
// action, OnClick, PostClick) - Bartender's key bindings are "CLICK BT4ButtonN:LeftButton".
static int LF_Click(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;
	const int64 Handle = GetFrameHandle(L);
	if (Handle < 0) return 0;
	const FString Button = lua_isstring(L, 2) ? FString(UTF8_TO_TCHAR(lua_tostring(L, 2))) : FString(TEXT("LeftButton"));
	static int32 Depth = 0; // a click script clicking frames that click back
	if (Depth >= 4) return 0;
	++Depth;
	Ctx->FrameManager->DispatchClick(Handle, Button.IsEmpty() ? FString(TEXT("LeftButton")) : Button, lua_toboolean(L, 3) != 0);
	--Depth;
	return 0;
}

// StatusBar methods
// The always-visible XP overlay is owned by our layout, not stock hover text.
// Refresh it from the same values that drive the fill, including max-only changes.
static void RefreshXPOverlay(lua_State* L)
{
    auto* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->FrameManager) return;
    const int64 Handle = GetFrameHandle(L);
    if (Handle < 0 || Handle != Ctx->FrameManager->FindFrame(TEXT("MainMenuExpBar"))) return;
    auto* Label = Ctx->FrameManager->GetNamedFontString(TEXT("MainMenuExpBarOverlayFrameText"));
    if (!Label) return;
    lua_getfield(L, 1, "__value");
    const double Value = lua_tonumber(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "__maxValue");
    const double Maximum = lua_tonumber(L, -1); lua_pop(L, 1);
    Label->SetText(FText::FromString(FString::Printf(TEXT("XP %.0f / %.0f"), Value, Maximum)));
}

static int LF_SetMinMaxValues(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float MinValue = static_cast<float>(luaL_checknumber(L, 2));
	float MaxValue = static_cast<float>(luaL_checknumber(L, 3));

	UE_LOG(LogWowLuaFrame, Verbose, TEXT("SetMinMaxValues called: Min=%.1f, Max=%.1f"), MinValue, MaxValue);

	// Store in Lua table
	lua_pushnumber(L, MinValue);
	lua_setfield(L, 1, "__minValue");
	lua_pushnumber(L, MaxValue);
	lua_setfield(L, 1, "__maxValue");

	// Update UMG widget if current value exists
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		UProgressBar* ProgressBar = Ctx->FrameManager->GetStatusBarWidget(Handle);
		if (ProgressBar)
		{
			// Get current value and recalculate percentage
			lua_getfield(L, 1, "__value");
			float CurrentValue = lua_isnil(L, -1) ? 0.0f : static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 1);

			float Range = MaxValue - MinValue;
			float Percent = (Range > 0.0f) ? (CurrentValue - MinValue) / Range : 0.0f;
			ProgressBar->SetPercent(FMath::Clamp(Percent, 0.0f, 1.0f));
		}
	}

	RefreshXPOverlay(L);
	return 0;
}

static int LF_GetMinMaxValues(lua_State* L)
{
	lua_getfield(L, 1, "__minValue");
	if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 0); }
	lua_getfield(L, 1, "__maxValue");
	if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 100); }
	return 2;
}

static int LF_SetValue(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	// Store in Lua table
	lua_pushvalue(L, 2);
	lua_setfield(L, 1, "__value");

	// Update UMG widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager && lua_isnumber(L, 2))
	{
		const float Value = static_cast<float>(lua_tonumber(L, 2));
		int64 Handle = GetFrameHandle(L);
		UProgressBar* ProgressBar = Ctx->FrameManager->GetStatusBarWidget(Handle);
		if (ProgressBar)
		{
			// Get min/max values from Lua table
			lua_getfield(L, 1, "__minValue");
			float MinValue = lua_isnil(L, -1) ? 0.0f : static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 1);

			lua_getfield(L, 1, "__maxValue");
			float MaxValue = lua_isnil(L, -1) ? 100.0f : static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 1);

			// Calculate percentage and update progress bar
			float Range = MaxValue - MinValue;
			float Percent = (Range > 0.0f) ? (Value - MinValue) / Range : 0.0f;
			float ClampedPercent = FMath::Clamp(Percent, 0.0f, 1.0f);

			ProgressBar->SetPercent(ClampedPercent);
			Ctx->FrameManager->UpdateStatusBarSpark(Handle, ClampedPercent);
		}
		else
		{
			// Try Slider widget (Sliders also use SetValue/GetValue)
			USlider* Slider = Ctx->FrameManager->GetSliderWidget(Handle);
			if (Slider)
			{
				lua_getfield(L, 1, "__minValue");
				float MinValue = lua_isnil(L, -1) ? 0.0f : static_cast<float>(lua_tonumber(L, -1));
				lua_pop(L, 1);
				lua_getfield(L, 1, "__maxValue");
				float MaxValue = lua_isnil(L, -1) ? 1.0f : static_cast<float>(lua_tonumber(L, -1));
				lua_pop(L, 1);
				float Range = MaxValue - MinValue;
				float Normalized = (Range > 0.0f) ? (Value - MinValue) / Range : 0.0f;
				Slider->SetValue(FMath::Clamp(Normalized, 0.0f, 1.0f));
			}
		}
	}
	else
	{
		UE_LOG(LogWowLuaFrame, Warning, TEXT("SetValue called but missing context or non-numeric value"));
	}

	RefreshXPOverlay(L);
	return 0;
}

static int LF_GetValue(lua_State* L)
{
	lua_getfield(L, 1, "__value");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 0);
	}
	return 1;
}

static int LF_SetStatusBarTexture(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager && lua_isstring(L, 2))
	{
		UProgressBar* Bar = Ctx->FrameManager->GetStatusBarWidget(GetFrameHandle(L));
		UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(lua_tostring(L, 2)));
		if (Bar && Texture)
		{
			FProgressBarStyle Style = Bar->GetWidgetStyle();
			FSlateBrush Fill = Style.FillImage;
			Fill.SetResourceObject(Texture);
			Fill.DrawAs = ESlateBrushDrawType::Image;
			Fill.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Style.SetFillImage(Fill);
			Bar->SetWidgetStyle(Style);
		}
	}
	return 0;
}
static int LF_SetStatusBarColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float R = static_cast<float>(luaL_checknumber(L, 2));
	float G = static_cast<float>(luaL_checknumber(L, 3));
	float B = static_cast<float>(luaL_checknumber(L, 4));
	float A = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 1.0f;

	// Store in Lua table
	lua_pushnumber(L, R); lua_setfield(L, 1, "__barColorR");
	lua_pushnumber(L, G); lua_setfield(L, 1, "__barColorG");
	lua_pushnumber(L, B); lua_setfield(L, 1, "__barColorB");
	lua_pushnumber(L, A); lua_setfield(L, 1, "__barColorA");

	// Update UMG widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		UProgressBar* ProgressBar = Ctx->FrameManager->GetStatusBarWidget(Handle);
		if (ProgressBar)
		{
			// Ensure the progress bar has a solid fill brush
			FProgressBarStyle BarStyle = ProgressBar->GetWidgetStyle();
			FSlateBrush FillBrush = BarStyle.FillImage;
			if (FillBrush.DrawAs == ESlateBrushDrawType::NoDrawType)
			{
				FillBrush.DrawAs = ESlateBrushDrawType::Box;
				FillBrush.SetResourceObject(nullptr); // solid color brush
				BarStyle.SetFillImage(FillBrush);
				ProgressBar->SetWidgetStyle(BarStyle);
			}

			const FLinearColor Color = WowColor::FromWow(R, G, B, A);
			ProgressBar->SetFillColorAndOpacity(Color);
		}
	}

	return 0;
}
static int LF_SetOrientation(lua_State* L) { return 0; }

// EditBox methods
static int LF_SetMaxLetters(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	int MaxLetters = static_cast<int>(luaL_checknumber(L, 2));

	// Store in Lua table
	lua_pushnumber(L, MaxLetters);
	lua_setfield(L, 1, "__maxLetters");

	// Apply to widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L))
		{
			// UEditableTextBox doesn't have a direct max length property in UE5
			// This would need custom implementation
		}
	}

	return 0;
}

static int LF_SetAutoFocus(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	bool AutoFocus = (lua_toboolean(L, 2) != 0);

	// Store in Lua table
	lua_pushboolean(L, AutoFocus);
	lua_setfield(L, 1, "__autoFocus");

	return 0;
}

static int LF_ClearFocus(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L))
		{
			// Clear focus from the edit box
			EditBox->SetUserFocus(nullptr);
		}
	}

	return 0;
}

static int LF_SetFocus(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L))
		{
			// Set focus to the edit box
			EditBox->SetKeyboardFocus();
		}
	}

	return 0;
}

static int LF_HasFocus(lua_State* L)
{
	bool HasFocus = false;

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L))
		{
			HasFocus = EditBox->HasKeyboardFocus();
		}
	}

	lua_pushboolean(L, HasFocus);
	return 1;
}
static int LF_HighlightText(lua_State* L) { return 0; }
static int LF_SetCursorPosition(lua_State* L) { return 0; }
static int LF_SetTextInsets(lua_State* L) { return 0; }
static int LF_SetMultiLine(lua_State* L) { return 0; }
// ── Font objects (XML <Font>, CreateFont) ──────────────────────────────────────────
// One shared "WowFont" metatable, so addon toolkits can extend it via
// getmetatable(GameFontNormal).__index like in the client.
static const char* FONT_METATABLE = "WowFont";

static int LFont_SetFont(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_isstring(L, 2)) { lua_pushvalue(L, 2); lua_setfield(L, 1, "__fontFile"); }
	if (lua_isnumber(L, 3)) { lua_pushvalue(L, 3); lua_setfield(L, 1, "__fontSize"); }
	lua_pushstring(L, luaL_optstring(L, 4, "")); lua_setfield(L, 1, "__fontFlags");
	lua_pushboolean(L, 1);
	return 1;
}
static int LFont_GetFont(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "__fontFile"); if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushstring(L, "Fonts\\FRIZQT__.TTF"); }
	lua_getfield(L, 1, "__fontSize"); if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 12); }
	lua_getfield(L, 1, "__fontFlags"); if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushstring(L, ""); }
	return 3;
}
static int LFont_StoreNumbers(lua_State* L, const char* Prefix, int Count)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	for (int I = 0; I < Count; ++I)
	{
		char Key[32];
		snprintf(Key, sizeof(Key), "%s%d", Prefix, I);
		lua_pushnumber(L, luaL_optnumber(L, 2 + I, I == 3 ? 1.0 : 0.0));
		lua_setfield(L, 1, Key);
	}
	return 0;
}
static int LFont_PushNumbers(lua_State* L, const char* Prefix, int Count, double Default)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	for (int I = 0; I < Count; ++I)
	{
		char Key[32];
		snprintf(Key, sizeof(Key), "%s%d", Prefix, I);
		lua_getfield(L, 1, Key);
		if (!lua_isnumber(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, (I == 3 || Default != 0.0) ? 1.0 : 0.0); }
	}
	return Count;
}
static void CopyFontFields(lua_State* L, int To, int From)
{
	static const char* Fields[] = { "__fontFile", "__fontSize", "__fontFlags", "__color0", "__color1", "__color2", "__color3",
		"__shadow0", "__shadow1", "__shadow2", "__shadow3", "__shadowOff0", "__shadowOff1", "__justifyH", "__justifyV", "__spacing" };
	for (const char* Field : Fields)
	{
		lua_getfield(L, From, Field);
		if (!lua_isnil(L, -1)) lua_setfield(L, To, Field); else lua_pop(L, 1);
	}
}
static int LFont_CopyFontObject(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_isstring(L, 2)) { lua_getglobal(L, lua_tostring(L, 2)); lua_replace(L, 2); }
	if (lua_istable(L, 2)) CopyFontFields(L, 1, 2);
	return 0;
}

/** Push a new Font object (registered as a global when named). */
static void PushNewFontObject(lua_State* L, const char* Name)
{
	lua_newtable(L);
	if (Name && Name[0]) { lua_pushstring(L, Name); lua_setfield(L, -2, "__name"); }
	lua_pushstring(L, "Font"); lua_setfield(L, -2, "__objectType");
	if (luaL_newmetatable(L, FONT_METATABLE))
	{
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
		const luaL_Reg Methods[] = {
			{"SetFont", LFont_SetFont}, {"GetFont", LFont_GetFont},
			{"SetTextColor", [](lua_State* S) -> int { return LFont_StoreNumbers(S, "__color", 4); }},
			{"GetTextColor", [](lua_State* S) -> int { return LFont_PushNumbers(S, "__color", 4, 1.0); }},
			{"SetShadowColor", [](lua_State* S) -> int { return LFont_StoreNumbers(S, "__shadow", 4); }},
			{"GetShadowColor", [](lua_State* S) -> int { return LFont_PushNumbers(S, "__shadow", 4, 0.0); }},
			{"SetShadowOffset", [](lua_State* S) -> int { return LFont_StoreNumbers(S, "__shadowOff", 2); }},
			{"GetShadowOffset", [](lua_State* S) -> int { return LFont_PushNumbers(S, "__shadowOff", 2, 0.0); }},
			{"SetJustifyH", [](lua_State* S) -> int { lua_pushstring(S, luaL_optstring(S, 2, "CENTER")); lua_setfield(S, 1, "__justifyH"); return 0; }},
			{"GetJustifyH", [](lua_State* S) -> int { lua_getfield(S, 1, "__justifyH"); if (lua_isnil(S, -1)) { lua_pop(S, 1); lua_pushstring(S, "CENTER"); } return 1; }},
			{"SetJustifyV", [](lua_State* S) -> int { lua_pushstring(S, luaL_optstring(S, 2, "MIDDLE")); lua_setfield(S, 1, "__justifyV"); return 0; }},
			{"GetJustifyV", [](lua_State* S) -> int { lua_getfield(S, 1, "__justifyV"); if (lua_isnil(S, -1)) { lua_pop(S, 1); lua_pushstring(S, "MIDDLE"); } return 1; }},
			{"SetSpacing", [](lua_State* S) -> int { lua_pushnumber(S, luaL_optnumber(S, 2, 0)); lua_setfield(S, 1, "__spacing"); return 0; }},
			{"GetSpacing", [](lua_State* S) -> int { lua_getfield(S, 1, "__spacing"); if (lua_isnil(S, -1)) { lua_pop(S, 1); lua_pushnumber(S, 0); } return 1; }},
			{"SetFontObject", LFont_CopyFontObject}, {"CopyFontObject", LFont_CopyFontObject},
			{"GetFontObject", [](lua_State* S) -> int { lua_pushvalue(S, 1); return 1; }},
			{"SetAlpha", [](lua_State* S) -> int { lua_pushnumber(S, luaL_optnumber(S, 2, 1)); lua_setfield(S, 1, "__alpha"); return 0; }},
			{"GetAlpha", [](lua_State* S) -> int { lua_getfield(S, 1, "__alpha"); if (lua_isnil(S, -1)) { lua_pop(S, 1); lua_pushnumber(S, 1); } return 1; }},
			{"GetObjectType", [](lua_State* S) -> int { lua_pushstring(S, "Font"); return 1; }},
			{"IsObjectType", [](lua_State* S) -> int { lua_pushboolean(S, FCStringAnsi::Stricmp(luaL_optstring(S, 2, ""), "Font") == 0); return 1; }},
			{"GetName", [](lua_State* S) -> int { lua_getfield(S, 1, "__name"); return 1; }},
			{nullptr, nullptr} };
		for (const luaL_Reg* Reg = Methods; Reg->name; ++Reg)
		{
			lua_pushcfunction(L, Reg->func);
			lua_setfield(L, -2, Reg->name);
		}
	}
	lua_setmetatable(L, -2);
	if (Name && Name[0])
	{
		lua_pushvalue(L, -1);
		lua_setglobal(L, Name);
	}
}

namespace WowLuaApi
{
	/** Font object for an XML <Font> (FrameXML/addons). Inherits copy the parent's fields. */
	void RegisterFontObject(lua_State* L, const FString& Name, const FString& Inherits, float Height, const FString& Flags,
		const FString& FontFile, const FLinearColor* Color)
	{
		if (!L || Name.IsEmpty()) return;
		FTCHARToUTF8 NameUtf8(*Name);
		lua_getglobal(L, NameUtf8.Get());
		const bool bExists = lua_istable(L, -1);
		lua_pop(L, 1);
		if (bExists) return;
		PushNewFontObject(L, NameUtf8.Get());
		const int Font = lua_gettop(L);
		if (!Inherits.IsEmpty())
		{
			lua_getglobal(L, TCHAR_TO_UTF8(*Inherits));
			if (lua_istable(L, -1)) CopyFontFields(L, Font, lua_gettop(L));
			lua_pop(L, 1);
		}
		if (!FontFile.IsEmpty()) { lua_pushstring(L, TCHAR_TO_UTF8(*FontFile)); lua_setfield(L, Font, "__fontFile"); }
		lua_getfield(L, Font, "__fontFile");
		const bool bHasFile = !lua_isnil(L, -1);
		lua_pop(L, 1);
		if (!bHasFile) { lua_pushstring(L, "Fonts\\FRIZQT__.TTF"); lua_setfield(L, Font, "__fontFile"); }
		if (Height > 0.f) { lua_pushnumber(L, Height); lua_setfield(L, Font, "__fontSize"); }
		// <Color> (GameFontNormal gold, GameFontHighlight white, ...); children inherit it via CopyFontFields.
		if (Color)
		{
			const float Channels[4] = { Color->R, Color->G, Color->B, Color->A };
			for (int32 I = 0; I < 4; ++I)
			{
				char Key[16];
				snprintf(Key, sizeof(Key), "__color%d", I);
				lua_pushnumber(L, Channels[I]);
				lua_setfield(L, Font, Key);
			}
		}
		if (!Flags.IsEmpty()) { lua_pushstring(L, TCHAR_TO_UTF8(*Flags)); lua_setfield(L, Font, "__fontFlags"); }
		lua_pop(L, 1);
	}
}

static int LF_FontStringSetFont(lua_State* L);
static int LF_FontStringSetTextColor(lua_State* L);

// fontString:SetFontObject(fontObject | "FontName"): apply its font, size and colour.
static int LF_SetFontObject(lua_State* L)
{
	if (!lua_istable(L, 1)) return 0;
	if (lua_isstring(L, 2)) { lua_getglobal(L, lua_tostring(L, 2)); lua_replace(L, 2); }
	if (!lua_istable(L, 2)) return 0;
	lua_pushvalue(L, 2);
	lua_setfield(L, 1, "__fontObject");
	lua_getfield(L, 2, "__fontFile");
	lua_getfield(L, 2, "__fontSize");
	lua_getfield(L, 2, "__fontFlags");
	const bool bHasFont = !lua_isnil(L, -3) || !lua_isnil(L, -2);
	const int Top = lua_gettop(L);
	lua_getfield(L, 2, "__color0");
	const bool bHasColor = lua_isnumber(L, -1) != 0;
	lua_pop(L, 1);
	if (bHasFont && GetTextBlockWidgetForTable(L, 1))
	{
		// Call SetFont(self, file, size, flags) with a fresh frame of arguments.
		lua_pushcfunction(L, LF_FontStringSetFont);
		lua_pushvalue(L, 1);
		lua_pushvalue(L, Top - 2);
		if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushstring(L, "Fonts\\FRIZQT__.TTF"); }
		lua_pushvalue(L, Top - 1);
		if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 12); }
		lua_pushvalue(L, Top);
		if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushstring(L, ""); }
		lua_call(L, 4, 0);
	}
	lua_settop(L, 2);
	if (bHasColor && GetTextBlockWidgetForTable(L, 1))
	{
		lua_pushcfunction(L, LF_FontStringSetTextColor);
		lua_pushvalue(L, 1);
		for (int I = 0; I < 4; ++I)
		{
			char Key[16];
			snprintf(Key, sizeof(Key), "__color%d", I);
			lua_getfield(L, 2, Key);
			if (!lua_isnumber(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 1); }
		}
		lua_call(L, 5, 0);
	}
	return 0;
}

static int LF_GetFontObject(lua_State* L)
{
	if (!lua_istable(L, 1)) { lua_pushnil(L); return 1; }
	lua_getfield(L, 1, "__fontObject");
	if (!lua_isnil(L, -1)) return 1;
	lua_pop(L, 1);
	// r383 (Stefan 16.10: "Move to Inactive" never showed ticked): an XML fontstring's font object is
	// its inherits= font (GameFontNormalSmall ...), GameFontNormal without one. GetFontObject returned
	// nil for those, so ReputationFrame_Update stopped with an error at
	// ReputationDetailInactiveCheckBoxText:GetFontObject():GetTextColor() before setting the boxes.
	lua_getfield(L, 1, "__name");
	const FString RegionName = lua_isstring(L, -1) ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
	lua_pop(L, 1);
	lua_getfield(L, 1, "__parentHandle");
	const int64 OwnerHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	FString FontName;
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!RegionName.IsEmpty() && OwnerHandle >= 0 && Ctx && Ctx->FrameManager)
		if (const FWowFrameDef* OwnerDef = Ctx->FrameManager->GetFrameDef(OwnerHandle))
			for (const FWowLayer& Layer : OwnerDef->Layers)
				for (const FWowFontStringElement& Element : Layer.FontStrings)
					if (Element.Name.Equals(RegionName, ESearchCase::IgnoreCase))
						FontName = Element.Inherits.IsEmpty() ? FString(TEXT("GameFontNormal")) : Element.Inherits;
	if (FontName.IsEmpty()) { lua_pushnil(L); return 1; }
	lua_getglobal(L, TCHAR_TO_UTF8(*FontName));
	if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }
	lua_pushvalue(L, -1);
	lua_setfield(L, 1, "__fontObject");
	return 1;
}

// frame/region:GetSize() -> width, height
static int LF_GetWidth(lua_State* L);
static int LF_GetHeight(lua_State* L);
static int LF_GetSize(lua_State* L)
{
	lua_settop(L, 1);
	LF_GetWidth(L);
	const float Width = static_cast<float>(lua_tonumber(L, -1));
	lua_settop(L, 1);
	LF_GetHeight(L);
	const float Height = static_cast<float>(lua_tonumber(L, -1));
	lua_settop(L, 1);
	lua_pushnumber(L, Width);
	lua_pushnumber(L, Height);
	return 2;
}

// EnumerateFrames([frame]) -> next frame object in creation order
static int L_EnumerateFrames(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !Ctx->EventSystem) { lua_pushnil(L); return 1; }
	int64 After = -1;
	if (lua_istable(L, 1))
	{
		lua_getfield(L, 1, "__handle");
		After = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
		lua_pop(L, 1);
		if (After < 0) { lua_pushnil(L); return 1; }
	}
	for (int64 Handle = Ctx->FrameManager->GetNextFrameHandle(After); Handle >= 0;
		Handle = Ctx->FrameManager->GetNextFrameHandle(Handle))
	{
		if (Ctx->EventSystem->PushFrameObject(Handle)) return 1;
	}
	lua_pushnil(L);
	return 1;
}

// ScrollFrame methods
static int LF_SetScrollChild(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0) return 0;

	// Check if second parameter is a frame table or nil
	if (lua_isnil(L, 2))
	{
		// Clear scroll child - for now, just log
		UE_LOG(LogTemp, Log, TEXT("SetScrollChild: clearing child for frame %lld"), ScrollFrameHandle);
		return 0;
	}

	int64 ChildHandle = GetOptionalFrameHandle(L, 2);
	if (ChildHandle < 0) return 0;

	Ctx->FrameManager->SetScrollChild(ScrollFrameHandle, ChildHandle);
	UE_LOG(LogTemp, Log, TEXT("ScrollFrame API: SetScrollChild called for frames %lld -> %lld"),
		ScrollFrameHandle, ChildHandle);
	return 0;
}

static int LF_GetScrollChild(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !Ctx->EventSystem)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 ChildHandle = Ctx->FrameManager->GetScrollChild(ScrollFrameHandle);
	if (ChildHandle < 0)
	{
		lua_pushnil(L);
		return 1;
	}

	// Push the child's Lua frame table (stored as global by name)
	const FString ChildName = Ctx->FrameManager->GetFrameName(ChildHandle);
	if (!ChildName.IsEmpty())
	{
		FTCHARToUTF8 UTF8Name(*ChildName);
		lua_getglobal(L, UTF8Name.Get());
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

static int LF_SetVerticalScroll(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0) return 0;

	float Offset = static_cast<float>(luaL_checknumber(L, 2));
	Ctx->FrameManager->SetVerticalScroll(ScrollFrameHandle, Offset);
	UE_LOG(LogTemp, Log, TEXT("ScrollFrame API: SetVerticalScroll called for frame %lld offset %.2f"),
		ScrollFrameHandle, Offset);
	return 0;
}

static int LF_GetVerticalScroll(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	float Offset = Ctx->FrameManager->GetVerticalScroll(ScrollFrameHandle);
	lua_pushnumber(L, Offset);
	return 1;
}

static int LF_SetHorizontalScroll(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0) return 0;

	float Offset = static_cast<float>(luaL_checknumber(L, 2));
	Ctx->FrameManager->SetHorizontalScroll(ScrollFrameHandle, Offset);
	return 0;
}

static int LF_GetHorizontalScroll(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	float Offset = Ctx->FrameManager->GetHorizontalScroll(ScrollFrameHandle);
	lua_pushnumber(L, Offset);
	return 1;
}

// Cooldown methods
static int LF_SetCooldown(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float StartTime = static_cast<float>(luaL_checknumber(L, 2));
	float Duration = static_cast<float>(luaL_checknumber(L, 3));

	// Store in Lua table
	lua_pushnumber(L, StartTime);
	lua_setfield(L, 1, "__cooldownStart");
	lua_pushnumber(L, Duration);
	lua_setfield(L, 1, "__cooldownDuration");

	// Update visual cooldown overlay
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		Ctx->FrameManager->SetCooldown(Handle, static_cast<double>(StartTime), Duration);
	}

	return 0;
}

// frame:Raise() / frame:Lower()
static int LF_Raise(lua_State* L) { return 0; }
static int LF_Lower(lua_State* L) { return 0; }

// frame:SetToplevel
static int LF_SetToplevel(lua_State* L) { return 0; }

// frame:RegisterForDrag
static int LF_RegisterForDrag(lua_State* L)
{
    auto* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->FrameManager) return 0;
    TArray<FString> Buttons;
    for (int32 Index = 2; Index <= lua_gettop(L); ++Index)
        Buttons.Add(UTF8_TO_TCHAR(luaL_checkstring(L, Index)));
    Ctx->FrameManager->SetFrameRegisteredDragButtons(GetFrameHandle(L), Buttons);
    return 0;
}

// frame:StartMoving / StopMovingOrSizing
static int LF_StartMoving(lua_State* L) { return 0; }
static int LF_StopMovingOrSizing(lua_State* L) { return 0; }

// ── Fixed Frame API Methods ───────────────────────────────────────────────────────

// frame:GetScale() — returns the frame's explicit scale (default 1.0)
// For now, always return 1.0 since we don't track per-frame scale yet
static int LF_GetScale(lua_State* L)
{
	lua_pushnumber(L, 1.0);
	return 1;
}

// frame:SetScale(scale) — stub, would need to affect widget render transform
static int LF_SetScale(lua_State* L)
{
	// TODO: Apply render transform scale to the widget
	return 0;
}

// frame:GetEffectiveScale() — returns the cumulative scale (frame * parent * UIScale)
static int LF_GetEffectiveScale(lua_State* L)
{
	// GetCursorPosition() already returns WoW UI units, so the matching effective scale is 1:
	// addons divide the cursor by it (x / frame:GetEffectiveScale()).
	lua_pushnumber(L, 1.0);
	return 1;
}

// frame:GetPoint(index) — returns anchor info for the nth anchor (1-based)
static int LF_GetPoint(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int Index = static_cast<int>(luaL_optnumber(L, 2, 1)) - 1; // 1-based to 0-based
	const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
	float X = 0.f, Y = 0.f, W = 0.f, H = 0.f, ParentHeight = 0.f;
	if (Def && Index == 0 && Def->Anchors.Num() == 0 && !Def->bSetAllPoints
		&& Ctx->FrameManager->GetFrameLivePlacement(Handle, X, Y, W, H, ParentHeight))
	{
		// Natively placed frame (chat boxes): report its live spot so code such as
		// Blizzard_CombatLog's "point, relativeTo, relativePoint, xOfs, yOfs" math works.
		lua_pushstring(L, "BOTTOMLEFT");
		lua_getglobal(L, "UIParent");
		lua_pushstring(L, "BOTTOMLEFT");
		lua_pushnumber(L, X);
		lua_pushnumber(L, ParentHeight - (Y + H));
		return 5;
	}
	if (!Def || Index < 0 || Index >= Def->Anchors.Num()) return 0;

	const FWowAnchor& A = Def->Anchors[Index];
	lua_pushstring(L, AnchorPointToString(A.Point));
	// Push relativeTo frame (as table or nil)
	if (!A.RelativeTo.IsEmpty())
	{
		lua_getglobal(L, TCHAR_TO_UTF8(*A.RelativeTo));
	}
	else
	{
		lua_pushnil(L);
	}
	lua_pushstring(L, AnchorPointToString(A.RelativePoint));
	lua_pushnumber(L, A.OffsetX);
	lua_pushnumber(L, A.OffsetY);
	return 5;
}

// frame:GetNumPoints() — returns the number of anchors
static int LF_GetNumPoints(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			lua_pushnumber(L, Def->Anchors.Num());
			return 1;
		}
	}
	lua_pushnumber(L, 0);
	return 1;
}

// frame:GetRect() — returns left, bottom, width, height in WoW coordinates
static int LF_GetRect(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			// Returns left, bottom, width, height in WoW coordinates
			// For simplicity, return based on frame rect
			lua_pushnumber(L, 0); // left
			lua_pushnumber(L, 0); // bottom
			lua_pushnumber(L, Def->Width);
			lua_pushnumber(L, Def->Height);
			return 4;
		}
	}
	return 0;
}

// button:RegisterForClicks(...) — store explicit click edge registrations
static int LF_RegisterForClicks(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager && Handle >= 0)
	{
		TArray<FString> ClickTypes;
		const int32 NumArgs = lua_gettop(L);
		for (int32 ArgIdx = 2; ArgIdx <= NumArgs; ++ArgIdx)
		{
			ClickTypes.Add(UTF8_TO_TCHAR(luaL_checkstring(L, ArgIdx)));
		}

		Ctx->FrameManager->SetFrameRegisteredClicks(Handle, ClickTypes);
	}

	return 0;
}

// scrollingmsgframe:AddMessage(text, r, g, b)
static int LF_AddMessage(lua_State* L)
{
	const char* Text = luaL_checkstring(L, 2);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		const float R = static_cast<float>(luaL_optnumber(L, 3, 1.0));
		const float G = static_cast<float>(luaL_optnumber(L, 4, 1.0));
		const float B = static_cast<float>(luaL_optnumber(L, 5, 1.0));
		Ctx->FrameManager->AddScrollingMessage(GetFrameHandle(L), UTF8_TO_TCHAR(Text),
			WowColor::FromWow(R, G, B, 1.0f));
	}
	return 0;
}

static FWowFrameManager* GetScrollingMessageManager(lua_State* L, int64& OutHandle)
{
	OutHandle = GetFrameHandle(L, 1);
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	return Ctx && Ctx->FrameManager && OutHandle >= 0 ? Ctx->FrameManager : nullptr;
}

static int LF_ClearMessages(lua_State* L)
{
	int64 Handle = -1;
	if (FWowFrameManager* Manager = GetScrollingMessageManager(L, Handle)) Manager->ClearScrollingMessages(Handle);
	return 0;
}

static int LF_GetNumMessages(lua_State* L)
{
	int64 Handle = -1;
	FWowFrameManager* Manager = GetScrollingMessageManager(L, Handle);
	lua_pushnumber(L, Manager ? Manager->GetScrollingMessageCount(Handle) : 0);
	return 1;
}

static int LF_GetNumLinesDisplayed(lua_State* L)
{
	int64 Handle = -1;
	FWowFrameManager* Manager = GetScrollingMessageManager(L, Handle);
	lua_pushnumber(L, Manager ? FMath::Min(7, Manager->GetScrollingMessageCount(Handle)) : 0);
	return 1;
}

static int LF_MessagesAtTop(lua_State* L)
{
	int64 Handle = -1;
	FWowFrameManager* Manager = GetScrollingMessageManager(L, Handle);
	lua_pushboolean(L, Manager && Manager->AreMessagesAtTop(Handle));
	return 1;
}

static int LF_MessagesAtBottom(lua_State* L)
{
	int64 Handle = -1;
	FWowFrameManager* Manager = GetScrollingMessageManager(L, Handle);
	lua_pushboolean(L, Manager && Manager->AreMessagesAtBottom(Handle));
	return 1;
}

static int LF_ScrollMessagesBy(lua_State* L, int32 Delta)
{
	int64 Handle = -1;
	if (FWowFrameManager* Manager = GetScrollingMessageManager(L, Handle)) Manager->ScrollMessages(Handle, Delta);
	return 0;
}

static int LF_ScrollMessagesToTop(lua_State* L)
{
	int64 Handle = -1;
	if (FWowFrameManager* Manager = GetScrollingMessageManager(L, Handle)) Manager->ScrollMessagesToTop(Handle);
	return 0;
}

static int LF_ScrollMessagesToBottom(lua_State* L)
{
	int64 Handle = -1;
	if (FWowFrameManager* Manager = GetScrollingMessageManager(L, Handle)) Manager->ScrollMessagesToBottom(Handle);
	return 0;
}

// (FontString API functions defined above near LF_CreateFontString)

// ── Tooltip Functions ─────────────────────────────────────────────────────────────

// tooltip:SetOwner(owner, anchor)
static int LF_TooltipSetOwner(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	int64 OwnerHandle = GetOptionalFrameHandle(L, 2);
	const char* AnchorType = luaL_optstring(L, 3, "ANCHOR_CURSOR");

	Ctx->FrameManager->TooltipSetOwner(TooltipHandle, OwnerHandle, UTF8_TO_TCHAR(AnchorType));
	lua_pushvalue(L, 2);
	lua_setfield(L, 1, "__tooltipOwner");
	return 0;
}

// tooltip:AddLine(text, r, g, b, wrap)
static int LF_TooltipAddLine(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* Text = luaL_checkstring(L, 2);
	float R = static_cast<float>(luaL_optnumber(L, 3, 1.0));
	float G = static_cast<float>(luaL_optnumber(L, 4, 1.0));
	float B = static_cast<float>(luaL_optnumber(L, 5, 1.0));
	bool bWrap = (lua_toboolean(L, 6) != 0);

	Ctx->FrameManager->TooltipAddLine(TooltipHandle, UTF8_TO_TCHAR(Text), R, G, B, bWrap);
	return 0;
}

// tooltip:AddDoubleLine(leftText, rightText, lr, lg, lb, rr, rg, rb)
static int LF_TooltipAddDoubleLine(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* LeftText = luaL_checkstring(L, 2);
	const char* RightText = luaL_checkstring(L, 3);
	float LR = static_cast<float>(luaL_optnumber(L, 4, 1.0));
	float LG = static_cast<float>(luaL_optnumber(L, 5, 1.0));
	float LB = static_cast<float>(luaL_optnumber(L, 6, 1.0));
	float RR = static_cast<float>(luaL_optnumber(L, 7, 1.0));
	float RG = static_cast<float>(luaL_optnumber(L, 8, 1.0));
	float RB = static_cast<float>(luaL_optnumber(L, 9, 1.0));

	Ctx->FrameManager->TooltipAddDoubleLine(TooltipHandle, UTF8_TO_TCHAR(LeftText), UTF8_TO_TCHAR(RightText),
											LR, LG, LB, RR, RG, RB);
	return 0;
}

// tooltip:ClearLines()
static int LF_TooltipClearLines(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	Ctx->FrameManager->TooltipClearLines(TooltipHandle);
	return 0;
}

// tooltip:NumLines()
static int LF_TooltipNumLines(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	int64 TooltipHandle = GetFrameHandle(L, 1);
	int32 LineCount = Ctx->FrameManager->TooltipNumLines(TooltipHandle);
	lua_pushnumber(L, LineCount);
	return 1;
}

// tooltip:SetText(text)
static int LF_TooltipSetText(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* Text = luaL_checkstring(L, 2);
	Ctx->FrameManager->TooltipSetText(TooltipHandle, UTF8_TO_TCHAR(Text));
	return 0;
}

// tooltip:AppendText(text)
static int LF_TooltipAppendText(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* Text = luaL_checkstring(L, 2);
	Ctx->FrameManager->TooltipAppendText(TooltipHandle, UTF8_TO_TCHAR(Text));
	return 0;
}

// tooltip:GetItem() -- returns item link for currently displayed item
static int LF_TooltipGetItem(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const FTooltipState* State = Ctx->FrameManager->GetTooltipState(TooltipHandle);
	if (!State || State->ItemLink.IsEmpty() || !State->ItemEntry)
	{
		lua_pushnil(L);
		return 1;
	}
	// name, link (GameTooltip_ShowCompareItem: local item, link = self:GetItem()).
	const FWowItemTemplateInfo* Info = Ctx->ConnectionManager
		? FWowItemPresentation::Info(Ctx->ConnectionManager, State->ItemEntry) : nullptr;
	const FString Name = Info ? Info->Name : FString();
	FString Link = State->ItemLink;
	if (!Link.Contains(TEXT("|H")))
	{
		const FLinearColor Color = Info ? FWowInventoryManager::GetQualityColor(static_cast<uint8>(Info->Quality)) : FLinearColor::White;
		const FColor C = Color.ToFColor(true);
		Link = FString::Printf(TEXT("|cff%02x%02x%02x|Hitem:%u:0:0:0:0:0:0:0:0|h[%s]|h|r"), C.R, C.G, C.B, State->ItemEntry, *Name);
	}
	lua_pushstring(L, TCHAR_TO_UTF8(*Name));
	lua_pushstring(L, TCHAR_TO_UTF8(*Link));
	return 2;
}

// tooltip:GetSpell() -- returns spell ID for currently displayed spell
static int LF_TooltipGetSpell(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const FTooltipState* State = Ctx->FrameManager->GetTooltipState(TooltipHandle);
	if (State && State->SpellId > 0)
	{
		lua_pushnumber(L, State->SpellId);
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// tooltip:GetUnit() -- returns unit token for currently displayed unit
static int LF_TooltipGetUnit(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const FTooltipState* State = Ctx->FrameManager->GetTooltipState(TooltipHandle);
	if (State && !State->UnitToken.IsEmpty())
	{
		lua_pushstring(L, TCHAR_TO_UTF8(*State->UnitToken));
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// tooltip:SetItem(itemLink) -- sets item context for tooltip
static int LF_TooltipSetItem(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* ItemLink = luaL_checkstring(L, 2);
	Ctx->FrameManager->TooltipSetItem(TooltipHandle, UTF8_TO_TCHAR(ItemLink));
	return 0;
}

static FString BuildTooltipItemLink(uint32 ItemEntry)
{
	return FString::Printf(TEXT("|cffffffff|Hitem:%u:0:0:0:0:0:0:0:0:0|h[Item %u]|h|r"),
		ItemEntry, ItemEntry);
}

static int LF_TooltipSetBagItem(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !Ctx->EntityManager) return 0;

	const int64 TooltipHandle = GetFrameHandle(L, 1);
	const int32 BagId = static_cast<int32>(luaL_checknumber(L, 2));
	const int32 SlotId = static_cast<int32>(luaL_checknumber(L, 3));
	FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
	if (!Player || SlotId <= 0) return 0;

	uint64 ItemGuid = 0;
	if (BagId == 0)
	{
		ItemGuid = Player->GetBackpackItemGuid(static_cast<uint8>(SlotId - 1));
	}
	else if (BagId >= 1 && BagId <= 4)
	{
		if (FWowContainerEntity* Container =
			Ctx->EntityManager->FindContainer(Player->GetBagGuid(static_cast<uint8>(BagId))))
		{
			ItemGuid = Container->GetItemGuidAtSlot(SlotId - 1);
		}
	}

	if (FWowItemEntity* Item = Ctx->EntityManager->FindItem(ItemGuid))
	{
		const uint32 ItemEntry = Item->GetField(ObjectField::ENTRY);
		Ctx->FrameManager->TooltipSetItemDetails(
			TooltipHandle,
			ItemEntry,
			BuildTooltipItemLink(ItemEntry),
			FMath::Max(Item->GetStackCount(), 1),
			FString(), // the original tooltip shows no bag/location line
			Item->GetDurability(),
			Item->GetMaxDurability(),
			/*bEquipped*/ false,
			ItemGuid);
        // Stock ContainerFrame relies on SetBagItem itself showing the tooltip.
        Ctx->FrameManager->SetFrameVisible(TooltipHandle, true);
	}
	return 0;
}

static const TCHAR* GetTooltipEquipmentSlotName(int32 SlotId)
{
	static const TCHAR* SlotNames[] = {
		TEXT("Head"), TEXT("Neck"), TEXT("Shoulder"), TEXT("Shirt"), TEXT("Chest"),
		TEXT("Waist"), TEXT("Legs"), TEXT("Feet"), TEXT("Wrist"), TEXT("Hands"),
		TEXT("Finger"), TEXT("Finger"), TEXT("Trinket"), TEXT("Trinket"), TEXT("Back"),
		TEXT("Main Hand"), TEXT("Off Hand"), TEXT("Ranged"), TEXT("Tabard")
	};
	return SlotId >= 1 && SlotId <= UE_ARRAY_COUNT(SlotNames) ? SlotNames[SlotId - 1] : TEXT("Equipped");
}

// r285: GameTooltip:SetGuildBankItem(tab, slot) for the guild bank's item buttons.
static int LF_TooltipSetGuildBankItem(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !Ctx->ConnectionManager) return 0;
	const int64 TooltipHandle = GetFrameHandle(L, 1);
	const int32 Tab = static_cast<int32>(luaL_optnumber(L, 2, 0)) - 1;
	const int32 Slot = static_cast<int32>(luaL_optnumber(L, 3, 0)) - 1;
	const FWowGuildBankState& Bank = Ctx->ConnectionManager->PacketHandler.GuildBank;
	if (!Bank.Tabs.IsValidIndex(Tab) || Slot < 0 || Slot > 97) return 0;
	const FWowGuildBankItem* Item = Bank.Tabs[Tab].Items.Find(static_cast<uint8>(Slot));
	if (!Item || !Item->Entry) return 0;
	Ctx->FrameManager->TooltipSetItemDetails(TooltipHandle, Item->Entry, BuildTooltipItemLink(Item->Entry),
		FMath::Max(Item->Count, 1), FString());
	if (Item->EnchantId)
	{
		const FString Name = FDbcStore::Get().EnchantmentName(static_cast<uint32>(Item->EnchantId));
		if (!Name.IsEmpty()) Ctx->FrameManager->TooltipAddLine(TooltipHandle, Name, .2f, 1, .2f, true);
	}
	Ctx->FrameManager->SetFrameVisible(TooltipHandle, true);
	return 0;
}

static int LF_TooltipSetInventoryItem(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !Ctx->EntityManager) return 0;

	const int64 TooltipHandle = GetFrameHandle(L, 1);
	const FString UnitToken = UTF8_TO_TCHAR(luaL_checkstring(L, 2));
	const int32 SlotId = static_cast<int32>(luaL_checknumber(L, 3));
	// r282: inventory slots 20-74 (bags, backpack, bank, bank bags: the BankFrame's buttons) show the
	// item without the "Equipped" line.
	if (UnitToken.Equals(TEXT("player"), ESearchCase::IgnoreCase) && SlotId >= 20 && SlotId <= 74)
	{
		FWowPlayerEntity* Me = Ctx->EntityManager->GetLocalPlayer();
		const uint64 Guid = Me ? Me->GetField64(static_cast<uint16>(PlayerField::INV_SLOT_HEAD + (SlotId - 1) * 2)) : 0;
		FWowItemEntity* Stored = Guid ? Ctx->EntityManager->FindItem(Guid) : nullptr;
		if (!Stored) return 0;
		const uint32 StoredEntry = Stored->GetField(ObjectField::ENTRY);
		Ctx->FrameManager->TooltipSetItemDetails(TooltipHandle, StoredEntry, BuildTooltipItemLink(StoredEntry),
			FMath::Max(Stored->GetStackCount(), 1), FString(), Stored->GetDurability(), Stored->GetMaxDurability(),
			/*bEquipped*/ false, Guid);
		Ctx->FrameManager->SetFrameVisible(TooltipHandle, true);
		lua_pushboolean(L, true); lua_pushboolean(L, false); lua_pushnumber(L, 0);
		return 3;
	}
	if (!UnitToken.Equals(TEXT("player"), ESearchCase::IgnoreCase) || SlotId < 1 || SlotId > 19)
	{
		return 0;
	}

	FWowPlayerEntity* Player = Ctx->EntityManager->GetLocalPlayer();
	if (!Player) return 0;
	const uint64 ItemGuid = Player->GetEquipmentItemGuid(static_cast<uint8>(SlotId - 1));
	if (FWowItemEntity* Item = Ctx->EntityManager->FindItem(ItemGuid))
	{
		const uint32 ItemEntry = Item->GetField(ObjectField::ENTRY);
		Ctx->FrameManager->TooltipSetItemDetails(
			TooltipHandle,
			ItemEntry,
			BuildTooltipItemLink(ItemEntry),
			FMath::Max(Item->GetStackCount(), 1),
			FString::Printf(TEXT("Equipped: %s"), GetTooltipEquipmentSlotName(SlotId)),
			Item->GetDurability(),
			Item->GetMaxDurability(),
			/*bEquipped*/ true,
			ItemGuid);
        const uint32 Enchant=Item->GetEnchantmentId(1);
        if(Enchant && (!Item->bTempEnchantTimed || Item->GetTempEnchantRemainingMs()>0))
        {
            FString Name=FDbcStore::Get().EnchantmentName(Enchant);
            if(Name.IsEmpty()) Name=FString::Printf(TEXT("Temporary enchant %u"),Enchant);
            Ctx->FrameManager->TooltipAddLine(TooltipHandle,Name,.2f,1,.2f,true);
            if(Item->bTempEnchantTimed) Ctx->FrameManager->TooltipAddLine(TooltipHandle,FString::Printf(TEXT("%d seconds remaining"),FMath::CeilToInt(Item->GetTempEnchantRemainingMs()/1000)),.8f,.8f,.8f,false);
            if(Item->GetEnchantmentCharges(1)>0) Ctx->FrameManager->TooltipAddLine(TooltipHandle,FString::Printf(TEXT("%u charges"),Item->GetEnchantmentCharges(1)),1,1,1,false);
        }
        Ctx->FrameManager->SetFrameVisible(TooltipHandle,true);
        lua_pushboolean(L, true); lua_pushboolean(L, false); lua_pushnumber(L, 0);
        return 3;
	}
    // Visible item entries arrive before the item instance in some realm updates.
    const uint32 Entry = Player->GetField(PlayerField::VISIBLE_ITEM_BASE + (SlotId-1)*2);
    if (Entry)
    {
        Ctx->FrameManager->TooltipSetItemDetails(TooltipHandle,Entry,BuildTooltipItemLink(Entry),1,FString());
        Ctx->FrameManager->SetFrameVisible(TooltipHandle,true);
    }
    lua_pushboolean(L, Entry != 0); lua_pushboolean(L, false); lua_pushnumber(L, 0);
    return 3;
}

static int LF_TooltipSetAura(lua_State* L, const char* Api)
{
    auto* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->FrameManager) return 0;
    const int64 Handle=GetFrameHandle(L,1);
    const int Args=lua_gettop(L);
    lua_getglobal(L,Api);
    for(int I=2;I<=Args;++I) lua_pushvalue(L,I);
    if(lua_pcall(L,Args-1,11,0)!=0) { lua_pop(L,1); return 0; }
    const uint32 Id=uint32(lua_tointeger(L,-1));
    const int Count=int(lua_tointeger(L,-8));
    const double Duration=lua_tonumber(L,-6), Expiry=lua_tonumber(L,-5);
    FString Name=lua_isstring(L,-11)?UTF8_TO_TCHAR(lua_tostring(L,-11)):TEXT("");
    lua_pop(L,11);
    Ctx->FrameManager->TooltipClearLines(Handle);
    if(!Id) { Ctx->FrameManager->SetFrameVisible(Handle,false); return 0; }
    if(auto* State=Ctx->FrameManager->GetTooltipState(Handle))
    { State->SpellId=Id; State->ItemLink.Empty(); State->UnitToken.Empty(); }
    Ctx->FrameManager->TooltipAddLine(Handle,Name,1,.82f,0,false);
    Ctx->FrameManager->TooltipAddLine(Handle,FWowSpellPresentation::Description(Id,true),1,1,1,true);
    if(Count>1) Ctx->FrameManager->TooltipAddLine(Handle,FString::Printf(TEXT("%d stacks"),Count),1,1,1,false);
    if(Duration>0) Ctx->FrameManager->TooltipAddLine(Handle,FString::Printf(TEXT("%d seconds remaining"),FMath::Max(0,FMath::CeilToInt(Expiry-FPlatformTime::Seconds()))),.8f,.8f,.8f,false);
    Ctx->FrameManager->SetFrameVisible(Handle,true);
    return 0;
}

static int LF_TooltipSetHyperlink(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;
	const int64 TooltipHandle = GetFrameHandle(L, 1);
	const FString ItemLink = UTF8_TO_TCHAR(luaL_checkstring(L, 2));
	Ctx->FrameManager->TooltipSetItem(TooltipHandle, ItemLink);
	return 0;
}

// tooltip:SetSpell(spellId) -- sets spell context for tooltip
static int LF_TooltipSetSpell(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
    // r391 (16.10 log: SpellBookFrame.lua:335 "bad argument #1 to 'SetSpell'" on every empty
    // spellbook slot): SpellButton_OnEnter passes SpellBook_GetSpellID's nil for an empty slot; the
    // tooltip then shows nothing and SetSpell returns nothing, without a Lua error.
    if (!lua_isnumber(L, 2)) return 0;
    int32 SpellId=static_cast<int32>(lua_tonumber(L,2));
    if(lua_type(L,3) == LUA_TSTRING)
    {
        uint32 Resolved=0;
        if (!WowLuaApi::ResolveBookSpellSlot(L, SpellId, lua_tostring(L,3), Resolved)) return 0;
        SpellId=static_cast<int32>(Resolved);
    }
	Ctx->FrameManager->TooltipSetSpell(TooltipHandle, SpellId);
	lua_pushboolean(L, 1);
	return 1;
}

static int LF_TooltipSetAction(lua_State* L)
{
    auto* Ctx=WowLuaApi::GetContext(L); if(!Ctx || !Ctx->FrameManager || !Ctx->ConnectionManager) return 0;
    const int32 Slot=int32(luaL_checknumber(L,2))-1;
    const auto& Actions=Ctx->ConnectionManager->PacketHandler.ActionButtons;
    if(!Actions.IsValidIndex(Slot) || !Actions[Slot]) return 0;
    const uint32 Id=Actions[Slot]&0xffffff, Type=Actions[Slot]>>24;
    const int64 Handle=GetFrameHandle(L,1);
    if(Type==0) Ctx->FrameManager->TooltipSetSpell(Handle,Id);
    else if(Type==128) Ctx->FrameManager->TooltipSetItemDetails(Handle,Id,FString(),1,FString());
    return 0;
}

// tooltip:SetUnit(unitToken) -- sets unit context for tooltip
static int LF_TooltipSetUnit(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* UnitToken = luaL_checkstring(L, 2);
	Ctx->FrameManager->TooltipSetUnit(TooltipHandle, UTF8_TO_TCHAR(UnitToken));
	return 0;
}

// ── ColorSelect ──────────────────────────────────────────────────────────────────
// ColorPickerFrame and addon colour pickers (ElvUI E:GrabColorPickerValues) read and write the
// selected colour. The value lives on the frame table; RGB and HSV views of the same colour.

static void ColorSelectStore(lua_State* L, double R, double G, double B)
{
	lua_pushnumber(L, FMath::Clamp(R, 0.0, 1.0)); lua_setfield(L, 1, "__csR");
	lua_pushnumber(L, FMath::Clamp(G, 0.0, 1.0)); lua_setfield(L, 1, "__csG");
	lua_pushnumber(L, FMath::Clamp(B, 0.0, 1.0)); lua_setfield(L, 1, "__csB");
}

static void ColorSelectRead(lua_State* L, double& R, double& G, double& B)
{
	lua_getfield(L, 1, "__csR"); R = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 1.0; lua_pop(L, 1);
	lua_getfield(L, 1, "__csG"); G = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 1.0; lua_pop(L, 1);
	lua_getfield(L, 1, "__csB"); B = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 1.0; lua_pop(L, 1);
}

static int LF_SetColorRGB(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	ColorSelectStore(L, luaL_optnumber(L, 2, 1.0), luaL_optnumber(L, 3, 1.0), luaL_optnumber(L, 4, 1.0));
	return 0;
}

static int LF_GetColorRGB(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	double R, G, B;
	ColorSelectRead(L, R, G, B);
	lua_pushnumber(L, R); lua_pushnumber(L, G); lua_pushnumber(L, B);
	return 3;
}

static int LF_SetColorHSV(lua_State* L)
{
	// WoW: hue 0-360, saturation and value 0-1.
	luaL_checktype(L, 1, LUA_TTABLE);
	const double H = FMath::Fmod(FMath::Fmod(luaL_optnumber(L, 2, 0.0), 360.0) + 360.0, 360.0) / 60.0;
	const double S = FMath::Clamp(luaL_optnumber(L, 3, 0.0), 0.0, 1.0);
	const double V = FMath::Clamp(luaL_optnumber(L, 4, 1.0), 0.0, 1.0);
	const int32 Sector = FMath::Min(5, static_cast<int32>(H));
	const double F = H - Sector, P = V * (1 - S), Q = V * (1 - S * F), T = V * (1 - S * (1 - F));
	const double Rgb[6][3] = {{V, T, P}, {Q, V, P}, {P, V, T}, {P, Q, V}, {T, P, V}, {V, P, Q}};
	ColorSelectStore(L, Rgb[Sector][0], Rgb[Sector][1], Rgb[Sector][2]);
	return 0;
}

static int LF_GetColorHSV(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	double R, G, B;
	ColorSelectRead(L, R, G, B);
	const double Max = FMath::Max3(R, G, B), Min = FMath::Min3(R, G, B), D = Max - Min;
	double H = 0.0;
	if (D > 0.0)
	{
		if (Max == R) H = 60.0 * FMath::Fmod((G - B) / D + 6.0, 6.0);
		else if (Max == G) H = 60.0 * ((B - R) / D + 2.0);
		else H = 60.0 * ((R - G) / D + 4.0);
	}
	lua_pushnumber(L, H);
	lua_pushnumber(L, Max > 0.0 ? D / Max : 0.0);
	lua_pushnumber(L, Max);
	return 3;
}

// ── Metatable Setup ──────────────────────────────────────────────────────────────

static const luaL_Reg FrameMethods[] =
{
	// Core
	{"GetName", LF_GetName},
	{"GetObjectType", LF_GetObjectType},
	{"IsObjectType", LF_IsObjectType},
	{"IsEventRegistered", LF_IsEventRegistered},
	{"RegisterEvent", LF_RegisterEvent},
	{"UnregisterEvent", LF_UnregisterEvent},
	{"UnregisterAllEvents", LF_UnregisterAllEvents},
	{"SetScript", LF_SetScript},
	{"GetScript", LF_GetScript},
	{"HookScript", LF_HookScript},
	{"HasScript", LF_HasScript},

	// Visibility
	{"Show", LF_Show},
	{"Hide", LF_Hide},
	{"IsShown", LF_IsShown},
	{"IsVisible", LF_IsVisible},
	{"Play", LF_PlayObject},
	{"Stop", LF_StopObject},
	{"IsPlaying", LF_IsObjectPlaying},
	{"IsMouseOver", LF_IsMouseOver},

	// Positioning
	{"SetPoint", LF_SetPointTracked},
	{"ClearAllPoints", LF_ClearAllPoints},
	{"SetAllPoints", LF_SetAllPoints},
	{"SetSize", LF_SetSizeTracked},
	{"SetWidth", LF_SetWidthTracked},
	{"SetHeight", LF_SetHeightTracked},
	{"GetWidth", LF_GetWidth},
	{"GetHeight", LF_GetHeight},
	{"GetCenter", LF_GetCenter},
	{"GetLeft", LF_GetLeft},
	{"GetRight", LF_GetRight},
	{"GetTop", LF_GetTop},
	{"GetBottom", LF_GetBottom},

	// Appearance
	{"SetAlpha", LF_SetAlpha},
	{"GetAlpha", LF_GetAlpha},
	{"SetBackdrop", LF_SetBackdrop},
	{"SetBackdropColor", LF_SetBackdropColor},
	{"SetBackdropBorderColor", LF_SetBackdropBorderColor},

	// Strata/Level
	{"SetFrameStrata", LF_SetFrameStrata},
	{"GetFrameStrata", LF_GetFrameStrata},
	{"SetFrameLevel", LF_SetFrameLevel},
	{"GetFrameLevel", LF_GetFrameLevel},
	{"Raise", LF_Raise},
	{"Lower", LF_Lower},
	{"SetToplevel", LF_SetToplevel},

	// Input
	{"EnableMouse", LF_EnableMouse},
	{"EnableKeyboard", LF_EnableKeyboard},
	{"EnableMouseWheel", LF_EnableMouseWheel},
	{"SetMovable", LF_SetMovable},
	{"SetResizable", LF_SetResizable},
	{"SetClampedToScreen", LF_SetClampedToScreen},
	{"RegisterForDrag", LF_RegisterForDrag},
	{"StartMoving", LF_StartMoving},
	{"StopMovingOrSizing", LF_StopMovingOrSizing},

	// Hierarchy
	{"SetParent", LF_SetParent},
	{"GetParent", LF_GetParent},
	{"GetChildren", LF_GetChildren},
	{"GetNumChildren", LF_GetNumChildren},

	// ID/Attributes
	{"SetID", LF_SetID},
	{"GetID", LF_GetID},
	{"SetAttribute", LF_SetAttribute},
	{"GetAttribute", LF_GetAttribute},

	// Creation
	{"CreateTexture", LF_CreateTexture},
	{"CreateFontString", LF_CreateFontString},

	// Button
	{"SetNormalTexture", LF_SetNormalTexture},
	{"SetPushedTexture", LF_SetPushedTexture},
	{"SetHighlightTexture", LF_SetHighlightTexture},
	{"SetDisabledTexture", LF_SetDisabledTexture},
	{"SetText", LF_SetTextTracked},
	{"GetText", LF_GetText},
	{"Disable", LF_Disable},
	{"Enable", LF_Enable},
	{"IsEnabled", LF_IsEnabled},
	{"Click", LF_Click},

	// StatusBar
	{"SetMinMaxValues", LF_SetMinMaxValues},
	// ColorSelect
	{"SetColorRGB", LF_SetColorRGB},
	{"GetColorRGB", LF_GetColorRGB},
	{"SetColorHSV", LF_SetColorHSV},
	{"GetColorHSV", LF_GetColorHSV},
	{"GetMinMaxValues", LF_GetMinMaxValues},
	{"SetValue", LF_SetValue},
	{"GetValue", LF_GetValue},
	{"SetStatusBarTexture", LF_SetStatusBarTexture},
	{"SetStatusBarColor", LF_SetStatusBarColor},
	{"SetOrientation", LF_SetOrientation},

	// EditBox
	{"SetMaxLetters", LF_SetMaxLetters},
	{"SetAutoFocus", LF_SetAutoFocus},
	{"ClearFocus", LF_ClearFocus},
	{"SetFocus", LF_SetFocus},
	{"HasFocus", LF_HasFocus},
	{"HighlightText", LF_HighlightText},
	{"SetCursorPosition", LF_SetCursorPosition},
	{"SetTextInsets", LF_SetTextInsets},
	{"SetMultiLine", LF_SetMultiLine},
	{"SetFontObject", LF_SetFontObject},
	{"GetInputLanguage", [](lua_State* L) -> int { lua_pushstring(L, "ROMAN"); return 1; }},

	// ScrollFrame
	{"SetScrollChild", LF_SetScrollChild},
	{"GetScrollChild", LF_GetScrollChild},
	{"SetVerticalScroll", LF_SetVerticalScroll},
	{"GetVerticalScroll", LF_GetVerticalScroll},
	{"SetHorizontalScroll", LF_SetHorizontalScroll},
	{"GetHorizontalScroll", LF_GetHorizontalScroll},

	// Cooldown
	{"SetCooldown", LF_SetCooldown},

	// Additional stubs to prevent OnUpdate error spam
	{"IsOwned", [](lua_State* L) -> int {
		// GameTooltip:IsOwned(frame): the owner given to the last SetOwner.
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, 1, "__tooltipOwner");
		lua_pushboolean(L, !lua_isnil(L, -1) && lua_rawequal(L, -1, 2));
		return 1;
	}},
	{"GetFontString", [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, 1, "__name");
		if (lua_isstring(L, -1)) {
			// Try FrameNameText first, then FrameNameFontString
			FString BaseName = UTF8_TO_TCHAR(lua_tostring(L, -1));
			lua_pop(L, 1);
			lua_getglobal(L, TCHAR_TO_UTF8(*(BaseName + TEXT("Text"))));
			if (!lua_isnil(L, -1)) return 1;
			lua_pop(L, 1);
			lua_getglobal(L, TCHAR_TO_UTF8(*(BaseName + TEXT("FontString"))));
			if (!lua_isnil(L, -1)) return 1;
			lua_pop(L, 1);
		} else { lua_pop(L, 1); }
		// Return minimal table so callers don't crash
		lua_newtable(L);
		luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
		if (!lua_isnil(L, -1)) lua_setmetatable(L, -2); else lua_pop(L, 1);
		return 1;
	}},
	// CheckButton: checked state is kept natively so clicks and Lua agree. Like 3.3.5,
	// GetChecked returns 1 or nil, and SetChecked(nil/false/0) unchecks.
	{"SetChecked", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		const int64 Handle = GetOptionalFrameHandle(L);
		const bool bChecked = lua_toboolean(L, 2) && !(lua_type(L, 2) == LUA_TNUMBER && lua_tonumber(L, 2) == 0);
		if (Ctx && Ctx->FrameManager && Handle >= 0) Ctx->FrameManager->SetButtonChecked(Handle, bChecked);
		return 0;
	}},
	{"GetChecked", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		const int64 Handle = GetOptionalFrameHandle(L);
		if (Ctx && Ctx->FrameManager && Handle >= 0 && Ctx->FrameManager->IsButtonChecked(Handle)) lua_pushinteger(L, 1);
		else lua_pushnil(L);
		return 1;
	}},
	{"SetCheckedTexture", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		const int64 Handle = GetOptionalFrameHandle(L);
		FString Path;
		if (lua_isstring(L, 2)) Path = UTF8_TO_TCHAR(lua_tostring(L, 2));
		else if (lua_istable(L, 2)) { lua_getfield(L, 2, "__texturePath"); if (lua_isstring(L, -1)) Path = UTF8_TO_TCHAR(lua_tostring(L, -1)); lua_pop(L, 1); }
		if (Ctx && Ctx->FrameManager && Handle >= 0) Ctx->FrameManager->SetCheckedTexture(Handle, Path);
		return 0;
	}},
	{"GetCheckedTexture", [](lua_State* L) -> int { return PushButtonStateRegion(L, "__checkedRegion"); }},
	{"SetNormalFontObject", [](lua_State* L) -> int { return 0; }},
	{"SetHighlightFontObject", [](lua_State* L) -> int { return 0; }},
	{"SetDisabledFontObject", [](lua_State* L) -> int { return 0; }},
	{"LockHighlight", [](lua_State* L) -> int { return 0; }},
	{"UnlockHighlight", [](lua_State* L) -> int { return 0; }},
	{"GetNormalTexture", [](lua_State* L) -> int {
		// Try to find the named texture global: FrameNameNormalTexture
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, 1, "__name");
		const char* frameName = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
		lua_pop(L, 1);
		if (frameName)
		{
			char globalName[256];
			snprintf(globalName, sizeof(globalName), "%sNormalTexture", frameName);
			lua_getglobal(L, globalName);
			if (!lua_isnil(L, -1)) return 1;
			lua_pop(L, 1);
		}
		// An unnamed NormalTexture: an object that edits the button's normal brush (r265).
		return PushButtonNormalProxy(L);
	}},
	{"GetPushedTexture", [](lua_State* L) -> int { return PushButtonStateRegion(L, "__pushedRegion"); }},
	{"GetHighlightTexture", [](lua_State* L) -> int { return PushButtonStateRegion(L, "__highlightRegion"); }},
	{"GetDisabledTexture", [](lua_State* L) -> int { return PushButtonStateRegion(L, "__disabledRegion"); }},
	{"SetButtonState", [](lua_State* L) -> int { return 0; }},
	{"GetButtonState", [](lua_State* L) -> int { lua_pushstring(L, "NORMAL"); return 1; }},
	{"RegisterForClicks", LF_RegisterForClicks},

	// Layout/Hit testing
	{"SetHitRectInsets", LF_SetHitRectInsets},
	{"GetHitRectInsets", LF_GetHitRectInsets},
	{"GetRect", LF_GetRect},
	{"GetScale", LF_GetScale},
	{"SetScale", LF_SetScale},
	{"GetEffectiveScale", LF_GetEffectiveScale},
	{"SetClipsChildren", [](lua_State* L) -> int { return 0; }},
	{"IsDragging", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"GetNumPoints", LF_GetNumPoints},
	{"GetPoint", LF_GetPoint},
	// isProtected, explicitlyProtected: frames built on SecureFrameTemplate (protected="true").
	{"IsProtected", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		const int64 Handle = GetOptionalFrameHandle(L);
		const bool bProtected = Ctx && Ctx->FrameManager && Handle >= 0 && Ctx->FrameManager->IsFrameProtected(Handle);
		lua_pushboolean(L, bProtected ? 1 : 0);
		lua_pushboolean(L, bProtected ? 1 : 0);
		return 2;
	}},
	{"CanChangeProtectedState", [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
	{"SetUserPlaced", [](lua_State* L) -> int { return 0; }},
	{"IsUserPlaced", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"SetDontSavePosition", [](lua_State* L) -> int { return 0; }},
	{"SetMaxResize", [](lua_State* L) -> int { return 0; }},
	{"SetMinResize", [](lua_State* L) -> int { return 0; }},

	// Texture methods on frames
	{"SetRotation", LF_ModelSetRotation},
	{"SetSequence", [](lua_State* L) -> int { return 0; }},
	{"SetDesaturated", [](lua_State* L) -> int { return 0; }},
	{"IsDesaturated", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"SetBlendMode", LF_TextureSetBlendMode},
	{"SetNonBlocking", [](lua_State* L) -> int { return 0; }},
	{"SetDrawLayer", [](lua_State* L) -> int { return 0; }},

	// Text methods on frames
	{"GetTextHeight", [](lua_State* L) -> int { lua_pushnumber(L, 12); return 1; }},
	{"SetVertexColor", LF_SetVertexColorDispatch},
	{"GetTextWidth", LF_FontStringGetStringWidth},
	{"GetStringWidth", LF_FontStringGetStringWidth},
	{"GetStringHeight", LF_FontStringGetStringHeight},
	{"SetTextHeight", [](lua_State* L) -> int { return 0; }},
	{"SetShadowOffset", [](lua_State* L) -> int { return 0; }},
	{"SetShadowColor", [](lua_State* L) -> int { return 0; }},
	{"SetSpacing", [](lua_State* L) -> int { return 0; }},
	{"SetJustifyH", LF_FontStringSetJustifyH},
	{"SetFormattedText", LF_SetFormattedTextTracked},
	{"SetWordWrap", [](lua_State* L) -> int { return 0; }},
	{"SetNonSpaceWrap", [](lua_State* L) -> int { return 0; }},
	{"SetJustifyV", [](lua_State* L) -> int { return 0; }},

	// Button methods
	{"SetMotionScriptsWhileDisabled", [](lua_State* L) -> int { return 0; }},

	// Missing common methods
	{"SetPadding", [](lua_State* L) -> int { return 0; }},
	{"GetPadding", [](lua_State* L) -> int { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 4; }},
	{"SetIndentedWordWrap", [](lua_State* L) -> int { return 0; }},
	{"SetClampRectInsets", [](lua_State* L) -> int { return 0; }},
	{"GetClampRectInsets", [](lua_State* L) -> int { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 4; }},
	{"IsClampedToScreen", LF_IsClampedToScreen},
	// EditBox:GetNumber/SetNumber (calendar time and level boxes): the text as a number.
	{"GetNumber", [](lua_State* L) -> int {
		lua_settop(L, 1);
		LF_GetText(L);
		const lua_Number NumberValue = lua_isstring(L, -1) ? lua_tonumber(L, -1) : 0;
		lua_settop(L, 0);
		lua_pushnumber(L, NumberValue);
		return 1;
	}},
	{"SetNumber", [](lua_State* L) -> int {
		lua_settop(L, 2);
		if (lua_isnumber(L, 2)) lua_tostring(L, 2); // converts in place
		return LF_SetText(L);
	}},
	// Blizzard_Calendar: the RSVP buttons' flash reads its animation's progress; HybridScrollFrame
	// (calendar invite lists) asks its scroll frame to re-measure the child. Both errored as nil.
	{"GetSmoothProgress", [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
	{"UpdateScrollChildRect", [](lua_State* L) -> int { return 0; }},
	{"SetHyperlinksEnabled", [](lua_State* L) -> int { return 0; }},
	{"GetMaxLines", [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
	{"SetHistoryLines", [](lua_State* L) -> int { return 0; }},
	{"SetFontObject", LF_SetFontObject},
	{"GetFontObject", LF_GetFontObject},
	{"GetSize", LF_GetSize},
	// GameTooltip:AddFontStrings(left, right) — LibHealComm/LibItemSearch scanning tooltips.
	{"AddFontStrings", [](lua_State* L) -> int {
		if (!lua_istable(L, 1)) return 0;
		lua_getfield(L, 1, "__scanLines");
		if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, 1, "__scanLines"); }
		const int Lines = lua_gettop(L);
		const int Count = static_cast<int>(lua_objlen(L, Lines));
		lua_pushvalue(L, 2); lua_rawseti(L, Lines, Count + 1);
		lua_pushvalue(L, 3); lua_rawseti(L, Lines, Count + 2);
		return 0;
	}},

	// ScrollingMessageFrame methods
	{"AddMessage", LF_AddMessage},
	{"SetMaxLines", [](lua_State* L) -> int { return 0; }},
	{"SetFading", [](lua_State* L) -> int { return 0; }},
	{"SetFadeDuration", [](lua_State* L) -> int { int64 H; if(auto* M=GetScrollingMessageManager(L,H))M->SetMessageTiming(H,luaL_checknumber(L,2),true); return 0; }},
	{"SetTimeVisible", [](lua_State* L) -> int { int64 H; if(auto* M=GetScrollingMessageManager(L,H))M->SetMessageTiming(H,luaL_checknumber(L,2),false); return 0; }},
	{"SetInsertMode", [](lua_State* L) -> int { return 0; }},
	{"Clear", LF_ClearMessages},
	{"GetVerticalScrollRange", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (!Ctx || !Ctx->FrameManager)
		{
			lua_pushnumber(L, 0);
			return 1;
		}

		int64 ScrollFrameHandle = GetFrameHandle(L, 1);
		if (ScrollFrameHandle < 0)
		{
			lua_pushnumber(L, 0);
			return 1;
		}

		float Range = Ctx->FrameManager->GetVerticalScrollRange(ScrollFrameHandle);
		lua_pushnumber(L, Range);
		return 1;
	}},
	{"GetHorizontalScrollRange", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (!Ctx || !Ctx->FrameManager)
		{
			lua_pushnumber(L, 0);
			return 1;
		}

		int64 ScrollFrameHandle = GetFrameHandle(L, 1);
		if (ScrollFrameHandle < 0)
		{
			lua_pushnumber(L, 0);
			return 1;
		}

		float Range = Ctx->FrameManager->GetHorizontalScrollRange(ScrollFrameHandle);
		lua_pushnumber(L, Range);
		return 1;
	}},
	{"SetScrollRange", [](lua_State* L) -> int { return 0; }},
	{"GetNumMessages", LF_GetNumMessages},
	{"GetNumLinesDisplayed", LF_GetNumLinesDisplayed},
	{"AtTop", LF_MessagesAtTop},
	{"AtBottom", LF_MessagesAtBottom},
	{"ScrollUp", [](lua_State* L) -> int { return LF_ScrollMessagesBy(L, 1); }},
	{"ScrollDown", [](lua_State* L) -> int { return LF_ScrollMessagesBy(L, -1); }},
	{"ScrollToTop", LF_ScrollMessagesToTop},
	{"ScrollToBottom", LF_ScrollMessagesToBottom},
	{"PageUp", [](lua_State* L) -> int { return LF_ScrollMessagesBy(L, 7); }},
	{"PageDown", [](lua_State* L) -> int { return LF_ScrollMessagesBy(L, -7); }},

	// Model/Dress methods
	{"SetModel", [](lua_State* L) -> int { return 0; }},
	{"SetModelScale", [](lua_State* L) -> int { return 0; }},
	{"SetPosition", [](lua_State* L) -> int { return 0; }},
	{"SetFacing", LF_ModelSetRotation},
	{"SetUnit", [](lua_State* L) -> int {
		// Check if this is a tooltip frame
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (Ctx && Ctx->FrameManager)
		{
			int64 Handle = GetFrameHandle(L, 1);
			const FWowFrameDef* FrameDef = Ctx->FrameManager->GetFrameDef(Handle);
			if (FrameDef && FrameDef->Type == EWowFrameType::GameTooltip)
			{
				const char* UnitToken = luaL_checkstring(L, 2);
				lua_getglobal(L, "__WowUnitTooltip");
				if (lua_isfunction(L, -1))
				{
					lua_pushvalue(L, 1);
					lua_pushstring(L, UnitToken);
					if (lua_pcall(L, 2, 0, 0) != 0)
					{
						UE_LOG(LogWowLuaFrame, Warning, TEXT("GameTooltip:SetUnit: %s"), UTF8_TO_TCHAR(lua_tostring(L, -1)));
						lua_pop(L, 1);
					}
					return 0;
				}
				lua_pop(L, 1);
				Ctx->FrameManager->TooltipSetUnit(Handle, UTF8_TO_TCHAR(UnitToken));
				return 0;
			}
		}
		// DressUpModel:SetUnit("player"): the dressing room shows you in your current gear.
		return LF_DressUpReset(L);
	}},
	{"SetCreature", [](lua_State* L) -> int { return 0; }},
	{"SetCamera", [](lua_State* L) -> int { return 0; }},
	{"RefreshUnit", [](lua_State* L) -> int { return 0; }},
	{"SetPortraitZoom", [](lua_State* L) -> int { return 0; }},
	{"ClearModel", [](lua_State* L) -> int { return 0; }},
	{"HasCustomBackground", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"SetCustomRace", [](lua_State* L) -> int { return 0; }},
	{"Undress", LF_DressUpUndress},
	{"TryOn", LF_DressUpTryOn},
	{"Dress", LF_DressUpReset},

	// Minimap methods
	{"SetZoom", [](lua_State* L) -> int { return 0; }},
	{"GetZoom", [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
	{"GetZoomLevels", [](lua_State* L) -> int { lua_pushnumber(L, 5); return 1; }},
	{"SetBlipTexture", [](lua_State* L) -> int { return 0; }},
	{"SetStaticArrowTexture", [](lua_State* L) -> int { return 0; }},

	// Tooltip methods
	{"SetOwner", LF_TooltipSetOwner},
	{"SetUnitBuff", [](lua_State* L) -> int { return LF_TooltipSetAura(L,"UnitBuff"); }},
	{"SetUnitDebuff", [](lua_State* L) -> int { return LF_TooltipSetAura(L,"UnitDebuff"); }},
    {"SetTracking", [](lua_State* L) -> int {
        auto* Ctx = WowLuaApi::GetContext(L);
        if (!Ctx || !Ctx->FrameManager) return 0;
        const int64 Handle = GetFrameHandle(L);
        Ctx->FrameManager->TooltipSetText(Handle, TEXT("Tracking"));
        Ctx->FrameManager->TooltipAddLine(Handle, TEXT("Click to choose tracking type."), 1.f, 1.f, 1.f, true);
        Ctx->FrameManager->SetFrameVisible(Handle, true);
        return 0;
    }},
	{"SetUnitAura", [](lua_State* L) -> int { return LF_TooltipSetAura(L,"UnitAura"); }},
	{"SetInventoryItem", LF_TooltipSetInventoryItem},
	{"SetBagItem", LF_TooltipSetBagItem},
	{"SetGuildBankItem", LF_TooltipSetGuildBankItem},
	{"SetAction", LF_TooltipSetAction},
	{"SetSpell", LF_TooltipSetSpell},
	{"SetSpellByID", LF_TooltipSetSpell},
	{"SetHyperlink", LF_TooltipSetHyperlink},
	{"AddLine", LF_TooltipAddLine},
	{"AddDoubleLine", LF_TooltipAddDoubleLine},
	{"ClearLines", LF_TooltipClearLines},
	{"NumLines", LF_TooltipNumLines},
	{"SetMinimumWidth", [](lua_State* L) -> int { return 0; }},
	{"AppendText", LF_TooltipAppendText},
	{"GetItem", LF_TooltipGetItem},
	{"GetSpell", LF_TooltipGetSpell},
	{"GetUnit", LF_TooltipGetUnit},
	{"SetItem", LF_TooltipSetItem},
	{"GetAnchorType", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		const FTooltipState* State = Ctx && Ctx->FrameManager ? Ctx->FrameManager->GetTooltipState(GetFrameHandle(L, 1)) : nullptr;
		lua_pushstring(L, State && !State->AnchorType.IsEmpty() ? TCHAR_TO_UTF8(*State->AnchorType) : "ANCHOR_NONE");
		return 1;
	}},
	// SetAnchorType(anchor, x, y): GameTooltip_ShowCompareItem slides the tooltip so the
	// comparison fits; keep the type (offsets are not applied yet).
	{"SetAnchorType", [](lua_State* L) -> int { return 0; }},
	{"IsEquippedItem", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		lua_pushboolean(L, Ctx && Ctx->FrameManager && Ctx->FrameManager->IsTooltipItemEquipped(GetFrameHandle(L, 1)) ? 1 : 0);
		return 1;
	}},
	{"SetHyperlinkCompareItem", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		const char* Link = lua_tostring(L, 2);
		const int32 Index = lua_isnumber(L, 3) ? static_cast<int32>(lua_tonumber(L, 3)) : 1;
		const bool bOk = Ctx && Ctx->FrameManager && Link
			&& Ctx->FrameManager->TooltipSetCompareItem(GetFrameHandle(L, 1), UTF8_TO_TCHAR(Link), Index);
		lua_pushboolean(L, bOk ? 1 : 0);
		return 1;
	}},
	{"FadeOut", [](lua_State* L) -> int { return 0; }},

	// Cooldown frame
	{"SetReverse", [](lua_State* L) -> int { return 0; }},
	{"SetSwipeColor", [](lua_State* L) -> int { return 0; }},
	{"SetBlingTexture", [](lua_State* L) -> int { return 0; }},
	{"SetEdgeTexture", [](lua_State* L) -> int { return 0; }},

	// Slider
	{"SetValueStep", [](lua_State* L) -> int { return 0; }},
	{"SetObeyStepOnDrag", [](lua_State* L) -> int { return 0; }},

	// Texture access (region globals use frame metatable)
	{"SetTexture", LF_TextureSetTexture},
	{"SetTexCoord", LF_TextureSetTexCoord},
	{"GetTexture", [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, 1, "__texture");
		if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushstring(L, ""); }
		return 1;
	}},
	{"GetOwner", [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		// Tooltips: the SetOwner frame; regions: their parent handle.
		lua_getfield(L, 1, "__tooltipOwner");
		if (!lua_isnil(L, -1)) return 1;
		lua_pop(L, 1);
		lua_getfield(L, 1, "__parentHandle");
		return 1;
	}},

	// Font access
	{"SetFont", LF_FontStringSetFont},
	{"GetFont", LF_FontStringGetFont},
	{"SetTextColor", LF_FontStringSetTextColor},
	{"GetTextColor", [](lua_State* L) -> int { lua_pushnumber(L, 1); lua_pushnumber(L, 1); lua_pushnumber(L, 1); lua_pushnumber(L, 1); return 4; }},

	{nullptr, nullptr}
};

// RestrictedFrames.lua hands frame methods a surrogate table { [0] = frame[0], [1] = frame }
// with the frame metatable. Swap any such argument for the real frame table so field-based
// lookups (__handle, __name, attributes) see the frame itself.
static void ResolveFrameSurrogate(lua_State* L, int Idx)
{
	lua_rawgeti(L, Idx, 0);
	if (!lua_islightuserdata(L, -1)) { lua_pop(L, 1); return; }
	lua_pushliteral(L, "__handle");
	lua_rawget(L, Idx);
	const bool bRealFrame = !lua_isnil(L, -1);
	lua_pop(L, 1);
	if (bRealFrame) { lua_pop(L, 1); return; }
	lua_rawgeti(L, Idx, 1);
	if (lua_istable(L, -1))
	{
		lua_rawgeti(L, -1, 0);
		const bool bSame = lua_rawequal(L, -1, -3) != 0;
		lua_pop(L, 1);
		if (bSame)
		{
			lua_replace(L, Idx); // frame table -> Idx
			lua_pop(L, 1);       // [0]
			return;
		}
	}
	lua_pop(L, 2);
}

static int LF_MethodDispatch(lua_State* L)
{
	const int Top = lua_gettop(L);
	for (int Idx = 1; Idx <= Top; ++Idx)
		if (lua_type(L, Idx) == LUA_TTABLE) ResolveFrameSurrogate(L, Idx);
	const lua_CFunction Method = reinterpret_cast<lua_CFunction>(lua_touserdata(L, lua_upvalueindex(1)));
	return Method(L);
}

void WowLuaApi::RegisterFrameApi(lua_State* L)
{
	// Create the WowFrame metatable
	luaL_newmetatable(L, FRAME_METATABLE);

	// Set __index to itself so methods are found via metatable lookup
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	// Register all frame methods. Each goes through LF_MethodDispatch so restricted-frame
	// surrogates ({ [0] = frame[0], [1] = frame } sharing this metatable) act as the frame.
	for (const luaL_Reg* Reg = FrameMethods; Reg->name; Reg++)
	{
		lua_pushlightuserdata(L, reinterpret_cast<void*>(Reg->func));
		lua_pushcclosure(L, LF_MethodDispatch, 1);
		lua_setfield(L, -2, Reg->name);
	}

	lua_pop(L, 1); // pop metatable

	// Register global CreateFrame
	lua_register(L, "CreateFrame", L_CreateFrame);

	// Register global CreateTexture(name, parent, drawLayer, inherits, subLevel)
	// WoW API — MoneyFrame.lua and others call this as a global function
	lua_register(L, "CreateTexture", LF_CreateTextureGlobal);

	// Register global CreateFont() - WoW 3.3.5 API for creating font objects
	lua_register(L, "CreateFont", LF_CreateFont);
	lua_register(L, "EnumerateFrames", L_EnumerateFrames);

	UE_LOG(LogWowLuaFrame, Log, TEXT("Registered WoW Frame API (CreateFrame + %d methods)"),
		(int)(sizeof(FrameMethods) / sizeof(FrameMethods[0]) - 1));
}

#else
void WowLuaApi::RegisterFrameApi(lua_State*) {}
#endif
