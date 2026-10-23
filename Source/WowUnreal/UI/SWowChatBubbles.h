#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/StrongObjectPtr.h"

class UWowUIManager;
class SConstraintCanvas;
class UTexture2D;

/**
 * World chat bubbles (/say, /yell and creature speech) as the 3.3.5 client draws them:
 * ChatBubble-Background / ChatBubble-Backdrop with the ChatBubble-Tail under it, text in
 * the chat type's colour, floating above the speaker's head and following them.
 */
class WOWUNREAL_API SWowChatBubbles : public SCompoundWidget
{
public:
    /** Screen anchor (viewport widget space, bottom centre of the bubble) for a speaker. */
    using FAnchorFunction = TFunction<bool(uint64 /*Guid*/, FVector2D& /*OutPosition*/)>;

    SLATE_BEGIN_ARGS(SWowChatBubbles) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, FAnchorFunction InAnchor);

    /** Show (or replace) the bubble over a speaker. */
    void AddBubble(uint64 Guid, const FString& Text, const FLinearColor& Color);
    void ClearAll();
    /** VR: bubbles are not placed on this flat layer but handed out as world cards. */
    void SetWorldMode(bool bInWorldMode);
    bool IsWorldMode() const { return bWorldMode; }
    /** VR: the current bubble widgets by speaker. */
    void GetWorldBubbles(TArray<TPair<uint64, TSharedRef<SWidget>>>& Out) const;

    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
    struct FBubble
    {
        uint64 Guid = 0;
        double ExpireAt = 0.0;
        FVector2D Position = FVector2D::ZeroVector;
        bool bOnScreen = false;
    };

    TSharedRef<SWidget> BuildBubble(const FString& Text, const FLinearColor& Color, uint64 Guid);
    UTexture2D* Texture(const TCHAR* Path) const;
    float UIScale() const;

    FAnchorFunction Anchor;
    bool bWorldMode = false;
    TWeakObjectPtr<UWowUIManager> UIManager;
    TSharedPtr<SConstraintCanvas> Canvas;
    TMap<uint64, FBubble> Bubbles;
    TMap<uint64, TSharedPtr<SWidget>> BubbleWidgets;
    FSlateBrush TailBrush;
    /** Slate brushes do not keep their textures alive; without this the bubble art could be
     *  garbage collected while a bubble is on screen (crash in SWowBackdrop::OnPaint). */
    mutable TMap<FString, TStrongObjectPtr<UTexture2D>> HeldTextures;
};
