-- "Use your WoW client's settings?" - asked once per realm when the WoW client folder (beside the
-- Data folder) has a WTF folder. The choice is remembered per realm and applies after a reload.
-- /wowsettings shows it again.
-- r227 (Stefan 11.10: "a disaster"): every region is anchored straight to the dialog with a fixed
-- size (no chains of text-to-text anchors, no stock button template), so nothing can land outside.
local UI = WowUnrealUI

local CHOICES = {
    { mode = 1, label = "Fresh copy each launch",
      text = "Read your WoW client's settings every time. Changes made here are thrown away at the next launch. The WoW client's files are never changed." },
    { mode = 2, label = "Copy once, keep my own",
      text = "Use your WoW client's settings for anything this client has not saved yet; after that this client keeps its own. The WoW client's files are never changed." },
    { mode = 0, label = "Keep separate",
      text = "This client keeps its own settings only." },
    { mode = 3, label = "Share (with backup)",
      text = "Both clients read and write the same files. A backup is made at every launch. Run one client at a time: the one that saves last wins." },
}

local WIDTH, PAD, ROW, FIRST_ROW = 500, 20, 64, 92
local BUTTON_W, BUTTON_H = 180, 24
local HEIGHT = FIRST_ROW + #CHOICES * ROW + 44

local frame

local function Text(parent, template, x, y, w, h, justifyH)
    local fs = parent:CreateFontString(nil, "OVERLAY", template)
    fs:SetPoint("TOPLEFT", parent, "TOPLEFT", x, -y)
    fs:SetWidth(w)
    fs:SetHeight(h)
    fs:SetJustifyH(justifyH or "LEFT")
    fs:SetJustifyV("TOP")
    return fs
end

-- A flat red button of our own: background, hover light and centred label, all on the button.
local function Button(parent, label, x, y, w, onClick, fromBottom)
    local b = CreateFrame("Button", nil, parent)
    b:SetWidth(w)
    b:SetHeight(BUTTON_H)
    if fromBottom then
        b:SetPoint("BOTTOM", parent, "BOTTOM", 0, y)
    else
        b:SetPoint("TOPLEFT", parent, "TOPLEFT", x, -y)
    end
    local bg = b:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(b)
    UI.FlatTexture(bg, .30, .035, .012)
    local edge = b:CreateTexture(nil, "BORDER")
    edge:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
    edge:SetPoint("BOTTOMRIGHT", b, "TOPRIGHT", 0, -1)
    UI.FlatTexture(edge, unpack(UI.LINE))
    local hl = b:CreateTexture(nil, "HIGHLIGHT")
    hl:SetAllPoints(b)
    UI.FlatTexture(hl, 1, 1, 1, .12)
    local fs = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    fs:SetAllPoints(b)
    fs:SetJustifyH("CENTER")
    fs:SetJustifyV("MIDDLE")
    fs:SetText(label)
    UI.SetFontSize(fs, 12)
    b:SetScript("OnClick", onClick)
    return b
end

local function Build()
    frame = CreateFrame("Frame", "WowUnrealWtfPrompt", UIParent)
    frame:SetFrameStrata("DIALOG")
    frame:SetWidth(WIDTH)
    frame:SetHeight(HEIGHT)
    frame:SetPoint("CENTER", UIParent, "CENTER", 0, 40)
    UI.FlatDialog(frame)
    frame:EnableMouse(true)
    frame:Hide()

    frame.title = Text(frame, "GameFontNormalLarge", PAD, 16, WIDTH - 2 * PAD, 24, "CENTER")
    frame.intro = Text(frame, "GameFontHighlightSmall", PAD, 46, WIDTH - 2 * PAD, 36, "CENTER")

    for i, choice in ipairs(CHOICES) do
        local y = FIRST_ROW + (i - 1) * ROW
        Button(frame, choice.label, PAD, y, BUTTON_W, function()
            __WowSetWtfMode(choice.mode)
            frame:Hide()
            ReloadUI()
        end)
        local note = Text(frame, "GameFontHighlightSmall", PAD + BUTTON_W + 14, y + 1,
            WIDTH - 2 * PAD - BUTTON_W - 14, ROW - 8, "LEFT")
        note:SetText(choice.text)
    end

    Button(frame, "Ask me later", 0, 14, 140, function() frame:Hide() end, true)
    tinsert(UISpecialFrames, "WowUnrealWtfPrompt") -- Esc closes it, like other dialogs
end

function UI.ShowWtfPrompt()
    local chosen, hasClientWtf, realm = __WowWtfInfo()
    if not hasClientWtf then
        DEFAULT_CHAT_FRAME:AddMessage("WowUnreal: no WoW client WTF folder was found beside the Data folder.")
        return
    end
    if not frame then Build() end
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
    if chosen == -1 and hasClientWtf then UI.ShowWtfPrompt() end
end)

SLASH_WOWUNREALSETTINGS1 = "/wowsettings"
SlashCmdList["WOWUNREALSETTINGS"] = function() UI.ShowWtfPrompt() end
