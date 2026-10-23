#pragma once
#include "CoreMinimal.h"
#include "Formats/M2Types.h"
struct FWowFixturePolicy
{
    static FString Leaf(const FString& Path)
    { FString P=Path;P.ReplaceInline(TEXT("\\"),TEXT("/"));int32 I;return P.FindLastChar('/',I)?P.Mid(I+1):P; }
    static bool IsHearth(const FString& Path)
    {
        const FString Name=Leaf(Path);
        return !IsUnlit(Path) && (Name.Contains(TEXT("campfire")) || Name.Contains(TEXT("bonfire")) ||
            Name.Contains(TEXT("firepit")) || Name.StartsWith(TEXT("FIREWOODPILE01")) || Name.StartsWith(TEXT("FIREWOODPILE04")) || Name.Contains(TEXT("UndeadFireSmall")) || Name.Contains(TEXT("UndeadFireLarge")));
    }
    static bool IsCandle(const FString& Path)
    { return Path.Contains(TEXT("candle")) || Path.Contains(TEXT("candelabra")) || Path.Contains(TEXT("chandelier")); }
    static bool IsUnlit(const FString& Path)
    { return Path.Contains(TEXT("candleoff")) || Path.Contains(TEXT("torchoff")) || Leaf(Path).Contains(TEXT("fireoff")) ||
        Leaf(Path).Contains(TEXT("bonfire_off")) || Leaf(Path).StartsWith(TEXT("OUTLANDDEADCAMPFIRE")); }
    static bool IsElwynnRoadLamp(const FString& Path)
    { return Leaf(Path).Equals(TEXT("LAMPPOST.M2"),ESearchCase::IgnoreCase) || Leaf(Path).Equals(TEXT("LAMPPOST.MDX"),ESearchCase::IgnoreCase) || Leaf(Path).Equals(TEXT("LAMPPOST"),ESearchCase::IgnoreCase); }
    static bool IsManagedLamp(const FString& Path)
    { return IsElwynnRoadLamp(Path) || Path.Contains(TEXT("STORMWINDSTREETLAMP")) || Path.Contains(TEXT("GENERALHANGINGLANTERN")) ||
        Path.Contains(TEXT("GENERALLANTERN0")) || IsMagicalLamp(Path) || IsBrazier(Path) || IsHearth(Path); }
    static bool IsMagicalLamp(const FString& Path)
    { return Path.Contains(TEXT("KALIDARSTREETLAMP")) || Path.Contains(TEXT("NIGHTELFHANGINGLANTERN")) || Path.Contains(TEXT("NIGHTELFLANTERN01")); }
    static bool IsBrazier(const FString& Path) { return Path.Contains(TEXT("STORMWINDDWARFBRAZIER")); }
    static bool IsLegacyHalo(const FString& Texture)
    { return (Texture.Contains(TEXT("glow")) && !Texture.Contains(TEXT("glowball"))) || Texture.Contains(TEXT("lensflare")); }
    static bool HasMagicalCore(const FString& Path)
    { return Path.Contains(TEXT("KALIDARSTREETLAMP")) || Path.Contains(TEXT("NIGHTELFHANGINGLANTERN")); }
    static bool SuppressHaloEmitter(const FString& Path)
    { return Path.Contains(TEXT("GENERALHANGINGLANTERN")) || Path.Contains(TEXT("NIGHTELFLANTERN01")); }
    static bool IsFlameTexture(const FString& Texture)
    { const FString Name=Leaf(Texture); return Name.Contains(TEXT("flame")) || Name.Contains(TEXT("fire")) || Name.Contains(TEXT("ember")); }
    static bool HasReplacementFlame(const FM2Data& Data,const FString& Path)
    {
        if (IsUnlit(Path) || (!IsCandle(Path) && !Path.Contains(TEXT("torch")) && !IsHearth(Path))) return false;
        for (const auto& E:Data.ParticleEmitters)
            if (E.bEnabledInStand && Data.TexturePaths.IsValidIndex(E.Texture) && IsFlameTexture(Data.TexturePaths[E.Texture])) return true;
        return false;
    }
    /** r418: the model burns (an emitter in Stand with a flame/fire/ember texture). */
    static bool HasFireEmitter(const FM2Data& Data)
    {
        for (const auto& E:Data.ParticleEmitters)
            if (E.bEnabledInStand && Data.TexturePaths.IsValidIndex(E.Texture) && IsFlameTexture(Data.TexturePaths[E.Texture])) return true;
        return false;
    }
    /** r418 (Stefan: "remove WoW's old flame/light effects entirely, replace them with ours" - the flat
     *  soft blob over braziers and the belt torch): on anything that burns, the authored glow card
     *  (GLOWORANGE32, GLOWWHITE32, lens flares) goes. Its flame particles and our point light stay. */
    static bool IsFireGlowCard(const FM2Data& Data,const FString& Path,const FM2RenderPass& Pass)
    {
        if (Pass.BlendMode<2 || !Data.TexturePaths.IsValidIndex(Pass.TextureIndex) || IsUnlit(Path)) return false;
        return IsLegacyHalo(Data.TexturePaths[Pass.TextureIndex]) && HasFireEmitter(Data);
    }
    /** r431 (Stefan, again: "remove WoW's old lighting effects entirely and replace them with ours"):
     *  any of WoW's flat light cards - a blended pass with a halo texture (GLOW*, lens flare; not
     *  glowballs) on a small card of at most 8 vertices - on any model, burning or not. The doodad
     *  manager gives a model that loses one our own point light at the card instead. */
    static bool IsLegacyGlowCard(const FM2Data& Data,const FM2RenderPass& Pass)
    {
        if (Pass.BlendMode<2 || !Data.TexturePaths.IsValidIndex(Pass.TextureIndex) || !IsLegacyHalo(Data.TexturePaths[Pass.TextureIndex])) return false;
        return Data.Submeshes.IsValidIndex(Pass.SubmeshIndex) && Data.Submeshes[Pass.SubmeshIndex].NumVertices<=8;
    }
    /** r444 (Stefan: "Lightning Shield became just a square now"): in a spell model the glow cards ARE
     *  the effect (the shield orbs, hand glows), so r431's light-card removal leaves spells alone. */
    static bool IsSpellModel(const FString& Path)
    {
        return Path.StartsWith(TEXT("Spells\\"),ESearchCase::IgnoreCase) || Path.StartsWith(TEXT("Spells/"),ESearchCase::IgnoreCase);
    }
    /** r447 (Stefan: Orgrimmar "grey and black flat walls in a big box shape", "holes in the sky"):
     *  OrgrimmarFloatingEmbers / OrgrimmarSmokeEmitter carry a 24-vertex helper box (up to 100 m wide at
     *  their placement scale) textured TEMP\64.BLP with a texture weight that is 0 in every key, so the
     *  original never shows it; only their particles are meant to be seen. A pass whose weight is
     *  authored as zero throughout is invisible and is left out of the mesh (spell models keep their
     *  passes: their states may fade in through the Hold tracks). */
    static bool IsAlwaysTransparent(const FString& Path,const FM2RenderPass& Pass)
    {
        if (IsSpellModel(Path) || Pass.TextureWeight.Values.IsEmpty()) return false;
        for (const float Value : Pass.TextureWeight.Values) if (Value > 0.004f) return false;
        return true;
    }
    static bool SkipLegacyPass(const FM2Data& Data,const FString& Path,const FM2RenderPass& Pass)
    {
        if (IsAlwaysTransparent(Path,Pass)) return true;
        if (IsFireGlowCard(Data,Path,Pass) || (IsLegacyGlowCard(Data,Pass) && !IsSpellModel(Path))) return true;
        if (Pass.BlendMode<2 || !Data.TexturePaths.IsValidIndex(Pass.TextureIndex)) return false;
        const FString& Texture=Data.TexturePaths[Pass.TextureIndex];
        if (IsManagedLamp(Path) && IsLegacyHalo(Texture)) return true;
        return HasReplacementFlame(Data,Path) && (IsFlameTexture(Texture) || Texture.Contains(TEXT("glow")));
    }
    // Centred flame cards need their lower edge raised to the authored wick/crown.
    static float FlameCentreLift(float HalfSize,bool Candle,bool Torch,bool Equipment)
    { return !Equipment && (Candle||Torch) ? HalfSize : 0.f; }
};
