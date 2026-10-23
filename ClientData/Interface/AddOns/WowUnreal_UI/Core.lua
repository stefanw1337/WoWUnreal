-- WowUnreal_UI: the client's own changes to the stock 3.3.5 interface, written as a normal addon.
-- Blizzard's FrameXML is loaded unchanged from the player's MPQs (and a custom server's patch
-- MPQs); this addon only adjusts the finished frames. It is shipped in the client's base Data
-- folder (User\Data\Interface\AddOns), always enabled, and loads before the player's addons so
-- they can restyle anything again. Edit these files and /reload: no client rebuild needed.

WowUnrealUI = WowUnrealUI or {}
local UI = WowUnrealUI

-- Plain white texture for flat fills and 1-pixel lines.
UI.WHITE = "Interface\\Buttons\\WHITE8X8"
-- The interface's gold line colour and dark panel colour.
UI.LINE = { .62, .52, .32, 1 }
UI.PANEL = { .035, .035, .04, .94 }

function UI.SetFontSize(fontString, size)
    if not fontString or not fontString.GetFont then return end
    local file, _, flags = fontString:GetFont()
    fontString:SetFont(file or "Fonts\\FRIZQT__.TTF", size, flags)
end

function UI.FlatTexture(texture, r, g, b, a)
    if not texture then return end
    texture:SetTexture(UI.WHITE)
    texture:SetTexCoord(0, 1, 0, 1)
    texture:SetVertexColor(r, g, b, a or 1)
end
