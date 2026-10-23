#pragma once
#include "CoreMinimal.h"

// Registered WoW frames/regions remain visible to Lua, /fstack and frame movement.
inline FString WowBuffFrameScript()
{
    return TEXT(R"LUA(
local function root(name)
    local f = _G[name] or CreateFrame("Frame", name, UIParent)
    f:SetParent(UIParent)
    f:EnableMouse(false)
    f:SetFrameStrata("MEDIUM")
    f:Show()
    return f
end
BuffFrame = root("BuffFrame")
TemporaryEnchantFrame = root("TemporaryEnchantFrame")
DebuffFrame = root("DebuffFrame")
BuffFrame:ClearAllPoints()
BuffFrame:SetPoint("TOPRIGHT", MinimapCluster or UIParent, MinimapCluster and "TOPLEFT" or "TOPRIGHT", MinimapCluster and -12 or -205, -13)
BuffFrame:SetSize(282, 42)
TemporaryEnchantFrame:ClearAllPoints()
TemporaryEnchantFrame:SetPoint("TOPRIGHT", BuffFrame, "BOTTOMRIGHT", 0, -6)
TemporaryEnchantFrame:SetSize(66,42)
DebuffFrame:ClearAllPoints()
DebuffFrame:SetPoint("TOPRIGHT", TemporaryEnchantFrame, "BOTTOMRIGHT", 0, -6)
DebuffFrame:SetSize(282,42)
for _,name in ipairs({"ConsolidatedBuffs","ConsolidatedBuffsTooltip"}) do
    if _G[name] then _G[name]:SetScript("OnHide",nil); _G[name]:Hide() end
end
local function button(name, parent)
    local b=_G[name] or CreateFrame("Button",name,parent,"AuraButtonTemplate")
    b:SetParent(parent)
    b:SetSize(30,30)
    b:EnableMouse(true)
    b:SetScript("OnUpdate",nil)
    -- r376 (Stefan 16.10: right-clicking a buff must remove it, weapon enchants such as poisons too):
    -- BuffButton_OnClick / TempEnchantButton_OnClick. Debuffs cannot be cancelled.
    if b.RegisterForClicks then b:RegisterForClicks("RightButtonUp") end
    b:SetScript("OnClick",function(self,mouse)
        if mouse and mouse~="RightButton" then return end
        if self.inventorySlot then
            if CancelItemTempEnchantment then CancelItemTempEnchantment(self.inventorySlot-15) end
        elseif self.filter=="HELPFUL" and CancelUnitBuff then
            CancelUnitBuff("player",self:GetID(),self.filter)
        end
    end)
    b:SetScript("OnLeave",function() if GameTooltip then GameTooltip:Hide() end end)
    local icon=_G[name.."Icon"] or b:CreateTexture(name.."Icon","ARTWORK")
    icon:ClearAllPoints(); icon:SetAllPoints(b)
    b.icon=icon
    local duration=_G[name.."Duration"] or b:CreateFontString(name.."Duration","OVERLAY")
    duration:SetFont("Fonts\\FRIZQT__.TTF",10,"OUTLINE")
    duration:ClearAllPoints(); duration:SetPoint("TOP",b,"BOTTOM",0,-2)
    duration:SetTextColor(1,.82,0)
    b.duration=duration
    local count=_G[name.."Count"] or b:CreateFontString(name.."Count","OVERLAY")
    count:SetFont("Fonts\\FRIZQT__.TTF",11,"OUTLINE")
    count:ClearAllPoints(); count:SetPoint("BOTTOMRIGHT",b,"BOTTOMRIGHT",-1,1)
    b.count=count
    return b
end
local function timeText(seconds)
    if seconds<=0 then return "" end
    if seconds>=3600 then return math.ceil(seconds/3600).."h" end
    if seconds>=60 then return math.ceil(seconds/60).."m" end
    return math.ceil(seconds).."s"
end
local function tooltip(b)
    if not GameTooltip then return end
    GameTooltip:SetOwner(b,"ANCHOR_BOTTOMLEFT")
    if b.inventorySlot then GameTooltip:SetInventoryItem("player",b.inventorySlot)
    else GameTooltip:SetUnitAura("player",b:GetID(),b.filter) end
end
local function appearance(b,texture,count,remaining)
    if b.lastTexture~=texture then
        b.icon:SetTexture(texture or "Interface\\Icons\\INV_Misc_QuestionMark")
        b.lastTexture=texture
    end
    b.icon:Show(); b.icon:SetAlpha(1)
    b.count:SetText(count and count>1 and tostring(count) or "")
    b.count:Show()
    b.duration:SetText(timeText(remaining)); b.duration:Show()
    b:SetAlpha(remaining>0 and remaining<10 and (.65+.35*math.abs(math.sin(GetTime()*3))) or 1)
    b:Show()
    if GameTooltip and GameTooltip:IsOwned(b) then tooltip(b) end
end
local function auraRows(parent,prefix,filter)
    local shown=0
    for i=1,64 do
        local name,rank,texture,count,kind,duration,expiry=UnitAura("player",i,filter)
        local b=_G[prefix..i]
        if name then
            shown=shown+1
            b=b or button(prefix..i,parent)
            -- Existing stock buttons are also normalized once.
            if not b.wowBuffReady then
                b=button(prefix..i,parent); b.wowBuffReady=true
                b:SetScript("OnEnter",tooltip)
            end
            b:SetID(i); b.filter=filter
            b:ClearAllPoints()
            b:SetPoint("TOPRIGHT",parent,"TOPRIGHT",-((shown-1)%8)*36,-math.floor((shown-1)/8)*46)
            appearance(b,texture,count,duration and duration>0 and math.max(0,expiry-GetTime()) or 0)
            if filter=="HARMFUL" then
                local border=_G[prefix..i.."Border"] or b:CreateTexture(prefix..i.."Border","OVERLAY")
                border:SetTexture("Interface\\Buttons\\UI-Debuff-Overlays")
                border:SetTexCoord(.296875,.5703125,0,.515625)
                border:SetAllPoints(b)
                local c=DebuffTypeColor and DebuffTypeColor[kind or "none"]
                border:SetVertexColor(c and c.r or .8,c and c.g or 0,c and c.b or 0)
                border:Show()
            end
        elseif b then
            if GameTooltip and GameTooltip:IsOwned(b) then GameTooltip:Hide() end
            b:Hide()
        end
    end
    parent:SetHeight(math.max(1,math.ceil(shown/8))*46)
    parent.visibleAuraCount=shown
end
local function refresh()
    BuffFrame:Show(); TemporaryEnchantFrame:Show(); DebuffFrame:Show()
    auraRows(BuffFrame,"BuffButton","HELPFUL")
    local mh,mt,mc,oh,ot,oc=GetWeaponEnchantInfo()
    local active={mh,oh}; local times={mt or 0,ot or 0}; local charges={mc or 0,oc or 0}
    local shown=0
    for i=1,2 do
        local name="TempEnchant"..i
        local b=_G[name]
        if active[i] then
            shown=shown+1
            if not b or not b.wowBuffReady then
                b=button(name,TemporaryEnchantFrame); b.wowBuffReady=true
                b.inventorySlot=15+i; b:SetID(15+i)
                b:SetScript("OnEnter",tooltip)
                local border=_G[name.."Border"] or b:CreateTexture(name.."Border","OVERLAY")
                border:SetTexture("Interface\\Buttons\\UI-TempEnchant-Border"); border:SetAllPoints(b); border:Show()
            end
            b:ClearAllPoints(); b:SetPoint("TOPRIGHT",TemporaryEnchantFrame,"TOPRIGHT",-(shown-1)*36,0)
            appearance(b,GetInventoryItemTexture("player",15+i),charges[i],times[i]/1000)
        elseif b then
            b:SetScript("OnUpdate",nil)
            if GameTooltip and GameTooltip:IsOwned(b) then GameTooltip:Hide() end
            b:Hide()
        end
    end
    TemporaryEnchantFrame:SetHeight(shown>0 and 46 or 0)
    -- These are sibling roots for independent dragging. Re-resolve dependent
    -- anchors after the preceding rows change height.
    TemporaryEnchantFrame:SetPoint("TOPRIGHT",BuffFrame,"BOTTOMRIGHT",0,-6)
    DebuffFrame:SetPoint("TOPRIGHT",TemporaryEnchantFrame,"BOTTOMRIGHT",0,-6)
    auraRows(DebuffFrame,"DebuffButton","HARMFUL")
end
-- Replace stock consolidation/anchoring callbacks, which otherwise reset moved frames.
BuffFrame_Update=refresh
BuffFrame_UpdatePositions=refresh
BuffFrame:SetScript("OnEvent",function(self,event,unit)
    if event~="UNIT_AURA" or unit=="player" then refresh() end
end)
for _,event in ipairs({"UNIT_AURA","PLAYER_ENTERING_WORLD","PLAYER_EQUIPMENT_CHANGED","UNIT_INVENTORY_CHANGED"}) do BuffFrame:RegisterEvent(event) end
local elapsedTime=0
BuffFrame:SetScript("OnUpdate",function(self,elapsed)
    elapsedTime=elapsedTime+elapsed
    if elapsedTime>=.2 then elapsedTime=0; refresh() end
end)
TemporaryEnchantFrame:SetScript("OnUpdate",nil)
refresh()
)LUA");
}
