-- Cast bar spell names sit in the middle of the bar. The stock FontString hangs from the bar's top
-- edge (with the border art around it), which left the name low and to one side; 1.5 units up
-- because the text box leaves room for descenders (Stefan 11.10: "a little higher"; the target's
-- "Immolate" stood above its bar). The black outline and the capitals' fine offset are drawn by
-- the client itself.
for _, name in ipairs({ "CastingBarFrame", "PetCastingBarFrame", "TargetFrameSpellBar", "FocusFrameSpellBar" }) do
    local bar, text = _G[name], _G[name .. "Text"]
    if bar and text then
        text:ClearAllPoints()
        text:SetPoint("CENTER", bar, "CENTER", 0, 1.5)
    end
end
