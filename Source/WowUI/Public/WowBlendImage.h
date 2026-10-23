#pragma once

#include "CoreMinimal.h"
#include "Components/Image.h"
#include "WowBlendImage.generated.h"

/**
 * Texture region with the client's blend modes (r237). texture:SetBlendMode("ADD") draws the
 * texture additively (premultiplied blend with zero alpha, like SWowAddImage), so glow and
 * highlight textures (UI-Character-Tab-Highlight, ButtonHilight-Square ...) lose their black box.
 * Every other mode draws like a normal UImage. Brush, colour and render opacity are read from the
 * UImage each paint, so SetBrush / SetColorAndOpacity / SetRenderOpacity work unchanged.
 */
UCLASS()
class WOWUI_API UWowBlendImage : public UImage
{
	GENERATED_BODY()

public:
	void SetAdditive(bool bInAdditive) { bAdditive = bInAdditive; }
	bool IsAdditive() const { return bAdditive; }

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	bool bAdditive = false;
	TSharedPtr<SWidget> BlendWidget;
};
