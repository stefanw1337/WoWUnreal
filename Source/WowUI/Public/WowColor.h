#pragma once
#include "CoreMinimal.h"

/**
 * WoW UI colours (Lua SetTextColor / SetVertexColor / AddLine, XML <Color>, |cAARRGGBB codes and the
 * client's colour constants) are screen (sRGB) values. Slate colours are linear and get encoded to
 * sRGB on display, so passing WoW values straight through drew every colour lighter and paler than
 * the original client (Stefan 10.10: druid orange 255,125,10 showed as 255,188,60).
 * FromWow converts once, where a WoW colour enters a Slate widget. Alpha stays as it is.
 */
namespace WowColor
{
	inline float ToLinear(float Channel)
	{
		const float C = FMath::Clamp(Channel, 0.0f, 1.0f);
		return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
	}

	inline FLinearColor FromWow(float R, float G, float B, float A = 1.0f)
	{
		return FLinearColor(ToLinear(R), ToLinear(G), ToLinear(B), A);
	}

	inline FLinearColor FromWow(const FLinearColor& Color)
	{
		return FromWow(Color.R, Color.G, Color.B, Color.A);
	}
}
