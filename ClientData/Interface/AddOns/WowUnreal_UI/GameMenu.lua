-- Game Menu "AddOns" button: opens the client's AddOns list in game (the 3.3.5 client only has it
-- on the character select screen).
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
