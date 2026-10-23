#pragma once
#include "Framework/Application/IInputProcessor.h"
#include "HAL/PlatformTime.h"
#include "WowKeyBindings.h"

// Runs before focused chat/options widgets. The binding editor registers its
// capture processor at index 0 later, so rebinding still takes priority.
// The key bound to SCREENSHOT (F12 / Print Screen by default, or your own):
// tap = screenshot (a photo while photo mode is on), hold = toggle photo mode.
class FWowScreenshotInput : public IInputProcessor
{
public:
    FWowScreenshotInput(TFunction<bool()> InCapture, TFunction<void()> InTap = nullptr, TFunction<void()> InHold = nullptr)
        : Capture(MoveTemp(InCapture)), Tap(MoveTemp(InTap)), Hold(MoveTemp(InHold)) {}
    static constexpr double HoldSeconds = 0.45;
    virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override
    {
        if (DownAt > 0.0 && !bHoldFired && Hold && FPlatformTime::Seconds() - DownAt >= HoldSeconds)
        {
            bHoldFired = true;
            Hold();
        }
    }
    virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        if (!IsScreenshotKey(Event)) return false;
        if (Event.IsRepeat() || DownAt > 0.0) return true;
        // Without a hold action (no photo mode host) capture immediately, as before.
        if (!Hold) { Capture(); return true; }
        DownAt = FPlatformTime::Seconds();
        DownKey = Event.GetKey();
        bHoldFired = false;
        return true;
    }
    virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        if (DownAt > 0.0 && Event.GetKey() == DownKey)
        {
            const bool bTap = !bHoldFired;
            DownAt = 0.0;
            bHoldFired = false;
            if (bTap) { if (Tap) Tap(); else Capture(); }
            return true;
        }
        return false;
    }
private:
    static bool IsScreenshotKey(const FKeyEvent& Event)
    {
        const FString Key=FWowKeyBindings::KeyName(Event.GetKey().GetFName().ToString(),
            Event.IsControlDown(), Event.IsShiftDown(), Event.IsAltDown());
        return FWowKeyBindings::Command(Key)==TEXT("SCREENSHOT");
    }
    TFunction<bool()> Capture;
    TFunction<void()> Tap;
    TFunction<void()> Hold;
    double DownAt = 0.0;
    FKey DownKey;
    bool bHoldFired = false;
};
