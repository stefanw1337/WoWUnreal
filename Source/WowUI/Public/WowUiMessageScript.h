#pragma once
#include "CoreMinimal.h"

// Installed after stock FrameXML and before addons. Keep stock event names and
// UIErrorsFrame available; separate informational messages without colour guesses.
inline const TCHAR* WowUiMessageScript()
{
    return TEXT(R"LUA(
        UIErrorsFrame = UIErrorsFrame or CreateFrame("MessageFrame", "UIErrorsFrame", UIParent)
        QuestProgressFrame = QuestProgressFrame or CreateFrame("MessageFrame", "QuestProgressFrame", UIParent)
        UIErrorsFrame:ClearAllPoints()
        UIErrorsFrame:SetPoint("TOP", UIParent, "TOP", 0, -184)
        UIErrorsFrame:SetSize(512, 60)
        UIErrorsFrame:EnableMouse(false)
        UIErrorsFrame:SetTimeVisible(3)
        UIErrorsFrame:SetFadeDuration(1)
        QuestProgressFrame:ClearAllPoints()
        QuestProgressFrame:SetPoint("TOP", UIParent, "TOP", 0, -122)
        QuestProgressFrame:SetSize(512, 60)
        QuestProgressFrame:EnableMouse(false)
        QuestProgressFrame:SetTimeVisible(6)
        QuestProgressFrame:SetFadeDuration(1)
        UIErrorsFrame:RegisterEvent("UI_ERROR_MESSAGE")
        UIErrorsFrame:RegisterEvent("UI_INFO_MESSAGE")
        local previousOnEvent = UIErrorsFrame:GetScript("OnEvent")
        UIErrorsFrame:SetScript("OnEvent", function(self, event, message, ...)
            if event ~= "UI_ERROR_MESSAGE" and event ~= "UI_INFO_MESSAGE" then
                if previousOnEvent then previousOnEvent(self, event, message, ...) end
                return
            end
            if type(message) ~= "string" or message == "" then return end
            if event == "UI_ERROR_MESSAGE" then
                self:AddMessage(message, 1, 0.15, 0.1)
            elseif event == "UI_INFO_MESSAGE" then
                QuestProgressFrame:AddMessage(message, 1, 0.82, 0)
            end
        end)
        UIErrorsFrame:Show()
        QuestProgressFrame:Show()
    )LUA");
}
