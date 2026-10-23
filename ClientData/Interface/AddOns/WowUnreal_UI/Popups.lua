-- Dialog boxes (StaticPopup1-4: exit/logout countdown, bind on equip, delete, invites ...):
-- a flat dark rectangle with a thin gold line, flat red buttons and smaller text, like the rest
-- of the WowUnreal interface (Stefan 11.10: "more flat, more rectangle shaped ... all dialogue
-- boxes", "lower the font").
local UI = WowUnrealUI

function UI.FlatDialog(frame)
    if not frame or not frame.SetBackdrop then return end
    frame:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.WHITE, tile = false, edgeSize = 1,
        insets = { left = 1, right = 1, top = 1, bottom = 1 } })
    frame:SetBackdropColor(unpack(UI.PANEL))
    frame:SetBackdropBorderColor(unpack(UI.LINE))
end

function UI.FlatButton(button)
    if not button then return end
    UI.FlatTexture(button:GetNormalTexture(), .30, .035, .012)
    UI.FlatTexture(button:GetPushedTexture(), .17, .02, .008)
    UI.FlatTexture(button:GetDisabledTexture(), .10, .10, .10)
    UI.FlatTexture(button:GetHighlightTexture(), 1, 1, 1, .12)
    if button.SetBackdrop then
        button:SetBackdrop({ edgeFile = UI.WHITE, edgeSize = 1 })
        button:SetBackdropBorderColor(unpack(UI.LINE))
    end
    UI.SetFontSize(button:GetFontString(), 11)
end

-- r276 (Stefan 13.10: "make it with frames like the exit game box"): the dialog boxes keep the
-- client's own look again - StaticPopupTemplate's UI-DialogBox backdrop and UIPanelButtonTemplate
-- red buttons, the same frame and buttons as the native exit/logout countdown. The flat
-- restyle also drew WHITE8X8 squares in the buttons' corners and turned a disabled Accept grey.
-- UI.FlatDialog / UI.FlatButton stay available for other uses.
