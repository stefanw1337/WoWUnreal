#include "WowHDCharacter.h"
#include "Mpq/MpqManager.h"
#include "WowPerformanceScope.h"
#include "Formats/Dbc/DbcStore.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowHD, Log, All);

namespace WowHD
{
FCriticalSection Lock;
TMap<FString, TSharedPtr<FJsonObject>> JsonCache;          // lower-case classic path -> json (null = none)
TMap<FString, TWeakObjectPtr<UTexture2D>> TextureCache;    // appearance key + type
// r519: textures composed on a worker thread, waiting for Build to turn them into UTexture2Ds.
struct FReadyTexture { int32 W = 0, H = 0; TArray<TArray<uint8>> Mips; };
TMap<FString, TMap<int32, FReadyTexture>> ReadyTextures; // appearance key -> type -> mip chain
TSet<FString> PendingTextures;                            // keys being composed right now
TSet<FString> FailedTextures;                             // keys whose composing found no body texture

FString HDModelPath(const FString& Classic)
{
    return FString(TEXT("HDX\\")) + Classic.Replace(TEXT("/"), TEXT("\\"));
}

FString JsonPath(const FString& Classic)
{
    FString Base = Classic.Replace(TEXT("/"), TEXT("\\"));
    if (Base.EndsWith(TEXT(".m2"), ESearchCase::IgnoreCase)) Base.LeftChopInline(3);
    return FString(TEXT("HDX\\")) + Base + TEXT(".customization.json");
}

TSharedPtr<FJsonObject> LoadJson(FMpqManager* Mpq, const FString& Classic)
{
    const FString Key = Classic.ToLower();
    {
        FScopeLock Guard(&Lock);
        if (const TSharedPtr<FJsonObject>* Found = JsonCache.Find(Key)) return *Found;
    }
    TSharedPtr<FJsonObject> Root;
    TArray<uint8> Bytes;
    if (Mpq && Mpq->ReadFile(JsonPath(Classic), Bytes) && Bytes.Num() > 2)
    {
        FString Text;
        FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num());
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            UE_LOG(LogWowHD, Warning, TEXT("HD customization JSON did not parse: %s"), *JsonPath(Classic));
            Root.Reset();
        }
    }
    FScopeLock Guard(&Lock);
    JsonCache.Add(Key, Root);
    return Root;
}

int64 NumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, int64 Default = 0)
{
    double Value = 0.0;
    return Object.IsValid() && Object->TryGetNumberField(Name, Value) ? int64(Value) : Default;
}

const TArray<TSharedPtr<FJsonValue>>* ArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
    return Object.IsValid() && Object->TryGetArrayField(Name, Out) ? Out : nullptr;
}

TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    const TSharedPtr<FJsonObject>* Out = nullptr;
    return Object.IsValid() && Object->TryGetObjectField(Name, Out) && Out ? *Out : nullptr;
}

struct FLayer
{
    int32 Type = 1;
    int32 Order = 0;
    int32 Blend = 1;
    int64 SectionMask = -1;
    FString File;
};

// Bilinear sample of Src (SW x SH) over the normalised source rectangle U0..U1/V0..V1,
// written into Dst at DstRect with WoW's opaque (0) or alpha-over (others) blend.
void PasteSampled(TArray<uint8>& Dst, int32 DW, int32 DH, const TArray<uint8>& Src, int32 SW, int32 SH,
    int32 X0, int32 Y0, int32 W, int32 H, float U0, float V0, float U1, float V1, int32 Blend)
{
    if (W <= 0 || H <= 0 || SW <= 0 || SH <= 0 || Src.Num() < SW * SH * 4) return;
    for (int32 Y = 0; Y < H; ++Y)
    {
        const int32 OY = Y0 + Y;
        if (OY < 0 || OY >= DH) continue;
        const float V = (V0 + (Y + 0.5f) / H * (V1 - V0)) * SH - 0.5f;
        const int32 Sy0 = FMath::Clamp(FMath::FloorToInt32(V), 0, SH - 1), Sy1 = FMath::Min(Sy0 + 1, SH - 1);
        const float Fy = FMath::Clamp(V - FMath::FloorToFloat(V), 0.0f, 1.0f);
        for (int32 X = 0; X < W; ++X)
        {
            const int32 OX = X0 + X;
            if (OX < 0 || OX >= DW) continue;
            const float U = (U0 + (X + 0.5f) / W * (U1 - U0)) * SW - 0.5f;
            const int32 Sx0 = FMath::Clamp(FMath::FloorToInt32(U), 0, SW - 1), Sx1 = FMath::Min(Sx0 + 1, SW - 1);
            const float Fx = FMath::Clamp(U - FMath::FloorToFloat(U), 0.0f, 1.0f);
            float C[4];
            for (int32 Ch = 0; Ch < 4; ++Ch)
            {
                const float A = Src[(Sy0 * SW + Sx0) * 4 + Ch] * (1 - Fx) + Src[(Sy0 * SW + Sx1) * 4 + Ch] * Fx;
                const float B = Src[(Sy1 * SW + Sx0) * 4 + Ch] * (1 - Fx) + Src[(Sy1 * SW + Sx1) * 4 + Ch] * Fx;
                C[Ch] = A * (1 - Fy) + B * Fy;
            }
            uint8* P = &Dst[(OY * DW + OX) * 4];
            if (Blend == 0)
            {
                for (int32 Ch = 0; Ch < 3; ++Ch) P[Ch] = uint8(FMath::Clamp(FMath::RoundToInt32(C[Ch]), 0, 255));
                P[3] = 255;
                continue;
            }
            const float Sa = C[3] / 255.0f;
            if (Sa <= 0.0f) continue;
            const float Da = P[3] / 255.0f;
            const float Oa = Sa + Da * (1.0f - Sa);
            for (int32 Ch = 0; Ch < 3; ++Ch)
            {
                const float Out = (C[Ch] * Sa + P[Ch] * Da * (1.0f - Sa)) / FMath::Max(Oa, 1e-4f);
                P[Ch] = uint8(FMath::Clamp(FMath::RoundToInt32(Out), 0, 255));
            }
            P[3] = uint8(FMath::Clamp(FMath::RoundToInt32(Oa * 255.0f), 0, 255));
        }
    }
}

// r519: the box-filtered mip chain, CPU only (safe on a worker thread).
TArray<TArray<uint8>> BuildMipChain(const TArray<uint8>& Pixels, int32 W, int32 H)
{
    TArray<TArray<uint8>> Chain;
    TArray<uint8> Level = Pixels;
    int32 LW = W, LH = H;
    for (;;)
    {
        if (LW == 1 && LH == 1) { Chain.Add(MoveTemp(Level)); break; }
        const int32 NW = FMath::Max(LW / 2, 1), NH = FMath::Max(LH / 2, 1);
        TArray<uint8> Next; Next.SetNumUninitialized(NW * NH * 4);
        for (int32 Y = 0; Y < NH; ++Y)
            for (int32 X = 0; X < NW; ++X)
                for (int32 Ch = 0; Ch < 4; ++Ch)
                {
                    const int32 X0 = FMath::Min(X * 2, LW - 1), X1 = FMath::Min(X * 2 + 1, LW - 1);
                    const int32 Y0 = FMath::Min(Y * 2, LH - 1), Y1 = FMath::Min(Y * 2 + 1, LH - 1);
                    const int32 Sum = Level[(Y0 * LW + X0) * 4 + Ch] + Level[(Y0 * LW + X1) * 4 + Ch]
                        + Level[(Y1 * LW + X0) * 4 + Ch] + Level[(Y1 * LW + X1) * 4 + Ch];
                    Next[(Y * NW + X) * 4 + Ch] = uint8((Sum + 2) / 4);
                }
        Chain.Add(MoveTemp(Level));
        Level = MoveTemp(Next); LW = NW; LH = NH;
    }
    return Chain;
}

// Game thread: a transient texture from a ready mip chain (copies only).
UTexture2D* CreateTextureFromMips(const TArray<TArray<uint8>>& Chain, int32 W, int32 H, const TCHAR* Name)
{
    if (Chain.Num() == 0) return nullptr;
    UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_R8G8B8A8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(Name)));
    if (!Tex) return nullptr;
    Tex->Filter = TF_Trilinear;
    Tex->SRGB = true;
    Tex->NeverStream = true;
    FTexturePlatformData* Data = Tex->GetPlatformData();
    if (!Data) return nullptr;
    Data->Mips.Empty();
    int32 LW = W, LH = H;
    for (const TArray<uint8>& Level : Chain)
    {
        FTexture2DMipMap* Mip = new FTexture2DMipMap();
        Data->Mips.Add(Mip);
        Mip->SizeX = LW; Mip->SizeY = LH;
        Mip->BulkData.Lock(LOCK_READ_WRITE);
        void* Dest = Mip->BulkData.Realloc(Level.Num());
        FMemory::Memcpy(Dest, Level.GetData(), Level.Num());
        Mip->BulkData.Unlock();
        LW = FMath::Max(LW / 2, 1); LH = FMath::Max(LH / 2, 1);
    }
    Tex->UpdateResource();
    return Tex;
}

UTexture2D* CreateMippedTexture(const TArray<uint8>& Pixels, int32 W, int32 H, const TCHAR* Name)
{
    return CreateTextureFromMips(BuildMipChain(Pixels, W, H), W, H, Name);
}

// r519: the layer compositing of Build, shared by the game-thread path and the background prefetch.
// Cache may be null (worker thread: gear overlays then read their textures from the MPQs).
// Returns false when no body texture was found (no HD_Data); OutCanvases keeps painted types only.
bool ComposeCanvases(FMpqManager* Mpq, FWowAssetCache* Cache, const TArray<FLayer>& SortedLayers,
    const TMap<int32, FIntPoint>& Sizes, const FIntPoint& LayoutSize, const TMap<int32, FIntRect>& Sections,
    const FWowCharacterTexture::FBodyEquipment& Equipment, TMap<int32, TArray<uint8>>& OutCanvases, int32& OutMissing)
{
    OutCanvases.Reset();
    OutMissing = 0;
    TSet<int32> Painted; // types that received at least one real texture
    struct FDecoded { TArray<uint8> Pixels; uint32 W = 0, H = 0; };
    TMap<FString, FDecoded> Decoded;
    for (const FLayer& Layer : SortedLayers)
    {
        const FIntPoint Size = Sizes.Contains(Layer.Type) ? Sizes[Layer.Type] : LayoutSize;
        TArray<uint8>& Canvas = OutCanvases.FindOrAdd(Layer.Type);
        if (Canvas.Num() != Size.X * Size.Y * 4) Canvas.SetNumZeroed(Size.X * Size.Y * 4);
        FDecoded* Image = Decoded.Find(Layer.File);
        if (!Image)
        {
            Image = &Decoded.Add(Layer.File);
            Image->Pixels = FWowCharacterTexture::LoadBlpRGBA(Mpq,
                FString(TEXT("HDX\\")) + Layer.File.Replace(TEXT("/"), TEXT("\\")), Image->W, Image->H);
            if (Image->Pixels.Num() == 0)
            {
                ++OutMissing;
                UE_LOG(LogWowHD, Warning, TEXT("HD texture missing (copy HD_Data into dist\\Data\\HD): %s"), *Layer.File);
            }
        }
        if (Image->Pixels.Num() == 0) continue;
        Painted.Add(Layer.Type);
        TArray<int32> Bits;
        if (Layer.SectionMask != -1)
            for (const auto& Pair : Sections)
                if (Pair.Key >= 0 && Pair.Key < 63 && (Layer.SectionMask & (int64(1) << Pair.Key))) Bits.Add(Pair.Key);
        const int32 SW = int32(Image->W), SH = int32(Image->H);
        if (Layer.Type != 1 || Layer.SectionMask == -1 || Bits.Num() == 0)
            PasteSampled(Canvas, Size.X, Size.Y, Image->Pixels, SW, SH, 0, 0, Size.X, Size.Y, 0, 0, 1, 1, Layer.Blend);
        else if (Bits.Num() == 1)
        {
            const FIntRect& S = Sections[Bits[0]];
            PasteSampled(Canvas, Size.X, Size.Y, Image->Pixels, SW, SH, S.Min.X, S.Min.Y, S.Max.X, S.Max.Y, 0, 0, 1, 1, Layer.Blend);
        }
        else
        {
            // One texture spanning the whole layout, clipped to these sections.
            for (const int32 Bit : Bits)
            {
                const FIntRect& S = Sections[Bit];
                PasteSampled(Canvas, Size.X, Size.Y, Image->Pixels, SW, SH, S.Min.X, S.Min.Y, S.Max.X, S.Max.Y,
                    float(S.Min.X) / Size.X, float(S.Min.Y) / Size.Y,
                    float(S.Min.X + S.Max.X) / Size.X, float(S.Min.Y + S.Max.Y) / Size.Y, Layer.Blend);
            }
        }
    }
    if (!Painted.Contains(1)) return false;
    for (auto It = OutCanvases.CreateIterator(); It; ++It)
    {
        if (!Painted.Contains(It.Key())) { It.RemoveCurrent(); continue; }
        if (It.Key() == 1)
        {
            const FIntPoint Size = Sizes.Contains(1) ? Sizes[1] : LayoutSize;
            // The body atlas is opaque, like the classic composite; gear goes on top.
            TArray<uint8>& Pixels = It.Value();
            for (int32 I = 3; I < Pixels.Num(); I += 4) Pixels[I] = 255;
            FWowCharacterTexture::ApplyEquipmentOverlays(Pixels, uint32(Size.X), uint32(Size.Y), Equipment, Mpq, Cache);
        }
    }
    return true;
}
}

namespace WowHD
{
FVector ToLab(const FLinearColor& Linear)
{
    const double X = (0.4124 * Linear.R + 0.3576 * Linear.G + 0.1805 * Linear.B) / 0.9505;
    const double Y = 0.2126 * Linear.R + 0.7152 * Linear.G + 0.0722 * Linear.B;
    const double Z = (0.0193 * Linear.R + 0.1192 * Linear.G + 0.9505 * Linear.B) / 1.089;
    auto F = [](double V) { return V > 0.008856 ? FMath::Pow(V, 1.0 / 3.0) : 7.787 * V + 16.0 / 116.0; };
    return FVector(116.0 * F(Y) - 16.0, 500.0 * (F(X) - F(Y)), 200.0 * (F(Y) - F(Z)));
}

bool SwatchLab(const TSharedPtr<FJsonObject>& Choice, FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Swatches = ArrayField(Choice, TEXT("swatch"));
    if (!Swatches || Swatches->Num() == 0) return false;
    const FString Hex = (*Swatches)[0]->AsString();
    if (Hex.Len() < 7) return false;
    Out = ToLab(FLinearColor(FColor::FromHex(Hex)));
    return true;
}

// Colour of the classic hair the player picked (CharSections hair texture), so the HD
// choice can be matched by colour: the HD lists are ordered differently from 3.3.5.
bool ClassicHairLab(FMpqManager* Mpq, const FWowCharacterTexture::FCustomization& C, FVector& Out)
{
    for (const FCharSectionsDbcEntry& Entry : FDbcStore::Get().CharSections().GetAll())
    {
        if (Entry.RaceID != C.RaceId || Entry.SexID != C.Gender || Entry.Type != 3
            || Entry.Variation != C.HairStyle || Entry.Color != C.HairColor || Entry.Textures[0].IsEmpty()) continue;
        uint32 W = 0, H = 0;
        const TArray<uint8> Pixels = FWowCharacterTexture::LoadBlpRGBA(Mpq, Entry.Textures[0], W, H);
        if (Pixels.Num() < 16) return false;
        double Sum[3] = {0, 0, 0}, Count = 0, MeanLum = 0, LumCount = 0;
        for (int32 I = 0; I + 3 < Pixels.Num(); I += 4)
            if (Pixels[I + 3] > 128) { MeanLum += Pixels[I] + Pixels[I + 1] + Pixels[I + 2]; ++LumCount; }
        if (LumCount == 0) return false;
        MeanLum /= LumCount;
        for (int32 I = 0; I + 3 < Pixels.Num(); I += 4)
            if (Pixels[I + 3] > 128 && Pixels[I] + Pixels[I + 1] + Pixels[I + 2] >= MeanLum)
            { Sum[0] += Pixels[I]; Sum[1] += Pixels[I + 1]; Sum[2] += Pixels[I + 2]; ++Count; }
        if (Count == 0) return false;
        Out = ToLab(FLinearColor(FColor(uint8(Sum[0] / Count), uint8(Sum[1] / Count), uint8(Sum[2] / Count))));
        return true;
    }
    return false;
}
}

namespace WowHD
{
// r394 (Stefan: gnome red hair/beard turned brown with HD on): match HD choices to the classic
// customization by texture name first. Most HD lists reuse the classic file names
// ("Hair00_05.blp" -> "hair00_05_hd.blp", "GnomeMaleSkin00_05" -> "gnomemaleskin00_05_hd");
// a name match is certain. The old rule (any swatch more than 15 Lab away from the classic hair
// texture's average is replaced by the nearest swatch) turned the gnome's red 5 into brown 1.
FString TextureStem(const FString& Path)
{
    FString Stem = FPaths::GetBaseFilename(Path.Replace(TEXT("\\"), TEXT("/"))).ToLower();
    if (Stem.EndsWith(TEXT("_hd"))) Stem.LeftChopInline(3);
    int32 Underscore = INDEX_NONE;
    if (Stem.FindLastChar(TEXT('_'), Underscore) && Stem.Len() - Underscore - 1 >= 6)
    {
        bool bDigits = true;
        for (int32 I = Underscore + 1; I < Stem.Len(); ++I) bDigits &= FChar::IsDigit(Stem[I]);
        if (bDigits) Stem.LeftInline(Underscore);
    }
    Stem.ReplaceInline(TEXT("_"), TEXT(""));
    return Stem;
}

// Texture 0 only for skin and hair (texture 1 is the shared tauren fur "_Extra" / scalp), both
// face halves for faces.
TSet<FString> ClassicSectionStems(const FWowCharacterTexture::FCustomization& C, uint32 Type, uint32 Variation, uint32 Color)
{
    const int32 Count = Type == 1 ? 2 : 1;
    TSet<FString> Stems;
    for (const FCharSectionsDbcEntry& Entry : FDbcStore::Get().CharSections().GetAll())
        if (Entry.RaceID == C.RaceId && Entry.SexID == C.Gender && Entry.Type == Type
            && Entry.Variation == Variation && Entry.Color == Color)
            for (int32 Index = 0; Index < Count; ++Index)
                if (!Entry.Textures[Index].IsEmpty()) Stems.Add(TextureStem(Entry.Textures[Index]));
    return Stems;
}

bool ChoiceUsesStem(const TSharedPtr<FJsonObject>& Choice, const TSet<FString>& Stems)
{
    if (Stems.Num() == 0) return false;
    if (const TArray<TSharedPtr<FJsonValue>>* Elements = ArrayField(Choice, TEXT("elements")))
        for (const TSharedPtr<FJsonValue>& ElementValue : *Elements)
            if (const TSharedPtr<FJsonObject> Material = ObjectField(ElementValue->AsObject(), TEXT("material")))
                if (const TArray<TSharedPtr<FJsonValue>>* Files = ArrayField(Material, TEXT("files")))
                    for (const TSharedPtr<FJsonValue>& FileValue : *Files)
                    {
                        FString Path;
                        const TSharedPtr<FJsonObject> FileObject = FileValue->AsObject();
                        if (FileObject.IsValid() && FileObject->TryGetStringField(TEXT("file"), Path) && Stems.Contains(TextureStem(Path)))
                            return true;
                    }
    return false;
}

// Not gated (type 2) or unlocked (type 4) retail content: the only candidates for the colour
// fallback. (The index itself stays raw: 3.3.5 has the gnome NPC skins "_100"/"_101" at 5/6 too.)
bool IsClassicEligible(const TSharedPtr<FJsonObject>& Choice)
{
    const TSharedPtr<FJsonObject> Requirement = ObjectField(Choice, TEXT("requirement"));
    const int64 Type = NumberField(Requirement, TEXT("type"), 0);
    return Type != 2 && Type != 4;
}
}

bool FWowHDCharacter::IsAvailable(FMpqManager* Mpq, const FString& ClassicModelPath)
{
    return Mpq && Mpq->HasHDData() && Mpq->FileExists(WowHD::HDModelPath(ClassicModelPath))
        && WowHD::LoadJson(Mpq, ClassicModelPath).IsValid();
}

void FWowHDCharacter::ClearCaches()
{
    FScopeLock Guard(&WowHD::Lock);
    WowHD::JsonCache.Reset();
    WowHD::TextureCache.Reset();
    WowHD::ReadyTextures.Reset(); // r519 (pending work still finishes and is stored)
    WowHD::FailedTextures.Reset();
}

static bool WowHDBuildImpl(FMpqManager* Mpq, FWowAssetCache* Cache, const FString& ClassicModelPath,
    const FWowCharacterTexture::FCustomization& Customization,
    const FWowCharacterTexture::FBodyEquipment& Equipment,
    const TMap<uint16, uint16>& ClassicDefaultGeosets,
    const TMap<uint16, uint16>& ClassicEquipmentGeosets,
    FWowHDCharacter::FAppearance& Out, bool bPrefetchOnly)
{
    using namespace WowHD;
    if (!FWowHDCharacter::IsAvailable(Mpq, ClassicModelPath)) return bPrefetchOnly; // nothing to wait for
    FWowPerformanceScope Timing(bPrefetchOnly ? TEXT("hd-prefetch") : TEXT("hd-character"), ClassicModelPath);
    const TSharedPtr<FJsonObject> Root = LoadJson(Mpq, ClassicModelPath);
    const TArray<TSharedPtr<FJsonValue>>* Options = ArrayField(Root, TEXT("options"));
    if (!Options) return false;

    // ── Choices: the five classic bytes, everything else its first choice ─────
    auto HasFacialGeosets = [](const TSharedPtr<FJsonObject>& Option)
    {
        if (const TArray<TSharedPtr<FJsonValue>>* Choices = ArrayField(Option, TEXT("choices")))
            for (const TSharedPtr<FJsonValue>& ChoiceValue : *Choices)
                if (const TArray<TSharedPtr<FJsonValue>>* Elements = ArrayField(ChoiceValue->AsObject(), TEXT("elements")))
                    for (const TSharedPtr<FJsonValue>& ElementValue : *Elements)
                    {
                        const TSharedPtr<FJsonObject> Geoset = ObjectField(ElementValue->AsObject(), TEXT("geoset"));
                        const int64 Type = NumberField(Geoset, TEXT("type"), -1);
                        if (Type >= 1 && Type <= 3) return true;
                    }
        return false;
    };
    bool bFacialAssigned = false;
    FString ChoiceLog;
    TArray<TSharedPtr<FJsonObject>> Chosen;
    TSet<int64> ChosenIds;
    for (const TSharedPtr<FJsonValue>& OptionValue : *Options)
    {
        const TSharedPtr<FJsonObject> Option = OptionValue.IsValid() ? OptionValue->AsObject() : nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Choices = ArrayField(Option, TEXT("choices"));
        if (!Choices || Choices->Num() == 0) continue;
        FString Name;
        Option->TryGetStringField(TEXT("name"), Name);
        int32 Classic = -1;          // classic byte for this option, -1 = none (first choice)
        TSet<FString> ClassicStems;  // classic CharSections texture names for this option
        const auto& C = Customization;
        if (Name.Equals(TEXT("Skin Color"), ESearchCase::IgnoreCase))
        { Classic = int32(C.SkinColor); ClassicStems = ClassicSectionStems(C, 0, 0, C.SkinColor); }
        else if (Name.Equals(TEXT("Face"), ESearchCase::IgnoreCase))
        { Classic = int32(C.FaceVariation); ClassicStems = ClassicSectionStems(C, 1, C.FaceVariation, C.SkinColor); }
        else if (Name.Equals(TEXT("Hair Style"), ESearchCase::IgnoreCase)) Classic = int32(C.HairStyle);
        else if (Name.Equals(TEXT("Hair Color"), ESearchCase::IgnoreCase))
        { Classic = int32(C.HairColor); ClassicStems = ClassicSectionStems(C, 3, C.HairStyle, C.HairColor); }
        else if (!bFacialAssigned && HasFacialGeosets(Option))
        {
            // Facial Hair / Tusks / Horns / Earrings ...: the race's classic fifth byte. The retail
            // lists keep the classic order here (gnome 2 = Chops = classic mutton chops).
            Classic = int32(C.FacialHairStyle);
            bFacialAssigned = true;
        }
        int32 Pick = 0;
        const TCHAR* How = TEXT("default");
        if (Classic >= 0)
        {
            // 1. The HD choice painted with the classic texture (same file name).
            for (int32 Index = 0; Index < Choices->Num() && ClassicStems.Num() > 0; ++Index)
                if (ChoiceUsesStem((*Choices)[Index]->AsObject(), ClassicStems)) { Pick = Index; How = TEXT("texture name"); break; }
            // 2. Else the classic index: the retail lists keep the classic order (hair styles and
            //    beards line up for all 20 models, checked against CharHairGeosets).
            if (FCString::Strcmp(How, TEXT("texture name")) != 0)
            {
                Pick = FMath::Clamp(Classic, 0, Choices->Num() - 1);
                How = TEXT("index");
                // 3. Hair colour only: a clearly different swatch is replaced by the closest one
                //    (Forever's own colour lists, e.g. blood elves). r394: needs a big, clear gap;
                //    the old 15 Lab rule turned the gnome's red into brown.
                FVector ClassicLab, IndexLab;
                if (Name.Equals(TEXT("Hair Color"), ESearchCase::IgnoreCase) && ClassicHairLab(Mpq, C, ClassicLab)
                    && SwatchLab((*Choices)[Pick]->AsObject(), IndexLab))
                {
                    const double IndexDistance = FVector::Dist(IndexLab, ClassicLab);
                    double Best = TNumericLimits<double>::Max();
                    int32 BestIndex = Pick;
                    for (int32 Index = 0; Index < Choices->Num(); ++Index)
                    {
                        FVector ChoiceLab;
                        if (!IsClassicEligible((*Choices)[Index]->AsObject()) || !SwatchLab((*Choices)[Index]->AsObject(), ChoiceLab)) continue;
                        const double Distance = FVector::Dist(ChoiceLab, ClassicLab);
                        if (Distance < Best) { Best = Distance; BestIndex = Index; }
                    }
                    if (IndexDistance > 30.0 && Best < 0.6 * IndexDistance) { Pick = BestIndex; How = TEXT("swatch colour"); }
                }
            }
        }
        const TSharedPtr<FJsonObject> Choice = (*Choices)[Pick]->AsObject();
        if (!Choice.IsValid()) continue;
        Chosen.Add(Choice);
        ChosenIds.Add(NumberField(Choice, TEXT("id"), -1));
        if (Classic >= 0)
        {
            FString ChoiceName;
            Choice->TryGetStringField(TEXT("name"), ChoiceName);
            ChoiceLog += FString::Printf(TEXT(" %s %d->%d%s%s (%s);"), *Name, Classic, Pick,
                ChoiceName.IsEmpty() ? TEXT("") : TEXT(" "), *ChoiceName, How);
        }
    }

    // ── Elements: geosets and texture layers ───────────────────────────────
    TMap<int32, int32> ChosenGeosets;   // type -> mesh id
    TArray<FLayer> Layers;
    for (const TSharedPtr<FJsonObject>& Choice : Chosen)
    {
        const TArray<TSharedPtr<FJsonValue>>* Elements = ArrayField(Choice, TEXT("elements"));
        if (!Elements) continue;
        for (const TSharedPtr<FJsonValue>& ElementValue : *Elements)
        {
            const TSharedPtr<FJsonObject> Element = ElementValue.IsValid() ? ElementValue->AsObject() : nullptr;
            if (!Element.IsValid()) continue;
            const int64 OnlyWith = NumberField(Element, TEXT("only_with_choice"), 0);
            if (OnlyWith != 0 && !ChosenIds.Contains(OnlyWith)) continue;
            if (const TSharedPtr<FJsonObject> Geoset = ObjectField(Element, TEXT("geoset")))
                ChosenGeosets.Add(int32(NumberField(Geoset, TEXT("type"))), int32(NumberField(Geoset, TEXT("mesh_id"))));
            if (const TSharedPtr<FJsonObject> Material = ObjectField(Element, TEXT("material")))
            {
                FString File, AnyFile;
                if (const TArray<TSharedPtr<FJsonValue>>* Files = ArrayField(Material, TEXT("files")))
                    for (const TSharedPtr<FJsonValue>& FileValue : *Files)
                    {
                        const TSharedPtr<FJsonObject> FileObject = FileValue->AsObject();
                        FString Path;
                        if (!FileObject.IsValid() || !FileObject->TryGetStringField(TEXT("file"), Path)) continue;
                        if (AnyFile.IsEmpty()) AnyFile = Path;
                        if (File.IsEmpty() && NumberField(FileObject, TEXT("usage")) == 0) File = Path;
                    }
                if (File.IsEmpty()) File = AnyFile;
                if (File.IsEmpty()) continue;
                if (const TArray<TSharedPtr<FJsonValue>>* LayerValues = ArrayField(Material, TEXT("layers")))
                    for (const TSharedPtr<FJsonValue>& LayerValue : *LayerValues)
                    {
                        const TSharedPtr<FJsonObject> L = LayerValue->AsObject();
                        FLayer Layer;
                        Layer.Type = int32(NumberField(L, TEXT("texture_type"), 1));
                        Layer.Order = int32(NumberField(L, TEXT("layer")));
                        Layer.Blend = int32(NumberField(L, TEXT("blend_mode"), 1));
                        Layer.SectionMask = NumberField(L, TEXT("section_mask"), -1);
                        Layer.File = File;
                        Layers.Add(Layer);
                    }
            }
        }
    }

    // ── Geosets: chosen, else the model's defaults (null = hidden), then gear ──
    TSet<int32> MeshIds;
    TSet<int32> Groups;
    if (const TArray<TSharedPtr<FJsonValue>>* Meshes = ArrayField(Root, TEXT("mesh_geosets")))
        for (const TSharedPtr<FJsonValue>& Mesh : *Meshes)
        {
            const int32 Id = int32(Mesh->AsNumber());
            MeshIds.Add(Id);
            Groups.Add(Id / 100);
        }
    TMap<int32, int32> Defaults;
    if (const TSharedPtr<FJsonObject> DefaultObject = ObjectField(Root, TEXT("geoset_defaults")))
        for (const auto& Pair : DefaultObject->Values)
            Defaults.Add(FCString::Atoi(*Pair.Key), Pair.Value.IsValid() && !Pair.Value->IsNull() ? int32(Pair.Value->AsNumber()) : -1);
    Out.Geosets.Reset();
    Out.Geosets.Add(0, uint16(FMath::Clamp(ChosenGeosets.FindRef(0), 0, 99)));
    for (const int32 Group : Groups)
    {
        if (Group == 0) continue;
        int32 Want = -1;
        if (const int32* Picked = ChosenGeosets.Find(Group)) Want = *Picked;
        else if (const int32* Default = Defaults.Find(Group)) Want = *Default;
        int32 Variant = Want >= Group * 100 ? Want - Group * 100 : 0;
        // r475 (Stefan: the HD troll has no ears, the HD tauren's earrings hang in the air): group 7
        // (ears) came out hidden (700) for the tauren and troll, whose ears no option or default
        // switches on. Ears are never hidden on a bare head: take the model's first ear mesh.
        if (Group == 7 && Variant == 0)
            for (int32 Ear = 1; Ear < 10; ++Ear)
                if (MeshIds.Contains(700 + Ear)) { Variant = Ear; break; }
        Out.Geosets.Add(uint16(Group), uint16(FMath::Clamp(Variant, 0, 99)));
    }
    for (const int32 GearGroup : {4, 5, 8, 9, 10, 11, 12, 13, 15, 18, 20, 22, 23})
    {
        const uint16 Group = uint16(GearGroup);
        const uint16* Gear = ClassicEquipmentGeosets.Find(Group);
        const uint16* Bare = ClassicDefaultGeosets.Find(Group);
        if (!Gear || (Bare && *Bare == *Gear)) continue;
        if (MeshIds.Contains(Group * 100 + *Gear)) Out.Geosets.Add(Group, *Gear);
        else UE_LOG(LogWowHD, Verbose, TEXT("HD model has no geoset %d for gear; keeping its default"), Group * 100 + *Gear);
    }
    // r634 (Stefan: "learn NOT to show toes in shoes - whenever a boot is on, smooth over all toes so it
    // becomes a sock/boot"): HD models carry their feet in group 20, the bare foot with toes as the model's
    // default and a smooth toeless foot as another variant, which the retail client switches to under any
    // boots. Classic boots without a feet geoset of their own (low shoes) kept the bare toes. Under boots,
    // take the model's other feet variant.
    if (Equipment.BootsDisplayId != 0 && !ClassicEquipmentGeosets.Contains(20) && Groups.Contains(20))
    {
        const int32 BareFeet = Out.Geosets.Contains(20) ? int32(Out.Geosets[20]) : 1;
        int32 Shod = -1;
        for (int32 Variant = 1; Variant < 10 && Shod < 0; ++Variant)
            if (Variant != BareFeet && MeshIds.Contains(2000 + Variant)) Shod = Variant;
        if (Shod > 0) Out.Geosets.Add(20, uint16(Shod));
        UE_LOG(LogWowHD, Log, TEXT("HD character %s: boots on, feet %d -> %d"), *ClassicModelPath, 2000 + BareFeet, Shod > 0 ? 2000 + Shod : 2000 + BareFeet);
    }

    // ── Textures ──────────────────────────────────────────────────────────
    const auto& C = Customization;
    const auto& E = Equipment;
    FString Key = FString::Printf(TEXT("%s:%u:%u:%u:%u:%u"), *ClassicModelPath.ToLower(),
        C.SkinColor, C.FaceVariation, C.HairStyle, C.HairColor, C.FacialHairStyle);
    for (uint32 Value : {E.ChestDisplayId, E.PantsDisplayId, E.BootsDisplayId, E.GlovesDisplayId, E.BracersDisplayId,
        E.ShirtDisplayId, E.TabardDisplayId, E.BeltDisplayId, E.CapeDisplayId, uint32(E.bIsRobeEquipped)})
        Key += FString::Printf(TEXT(":%u"), Value);

    TMap<int32, FIntPoint> Sizes;
    const TSharedPtr<FJsonObject> Layout = ObjectField(Root, TEXT("texture_layout"));
    const FIntPoint LayoutSize(int32(NumberField(Layout, TEXT("width"), 2048)), int32(NumberField(Layout, TEXT("height"), 1024)));
    if (const TArray<TSharedPtr<FJsonValue>>* ModelTextures = ArrayField(Root, TEXT("model_textures")))
        for (const TSharedPtr<FJsonValue>& Value : *ModelTextures)
        {
            const TSharedPtr<FJsonObject> T = Value->AsObject();
            Sizes.Add(int32(NumberField(T, TEXT("texture_type"))),
                FIntPoint(int32(NumberField(T, TEXT("width"), 512)), int32(NumberField(T, TEXT("height"), 512))));
        }
    TMap<int32, FIntRect> Sections;   // min = x,y; max = width,height
    if (const TArray<TSharedPtr<FJsonValue>>* SectionValues = ArrayField(Layout, TEXT("sections")))
        for (const TSharedPtr<FJsonValue>& Value : *SectionValues)
        {
            const TSharedPtr<FJsonObject> S = Value->AsObject();
            Sections.Add(int32(NumberField(S, TEXT("section"))), FIntRect(int32(NumberField(S, TEXT("x"))), int32(NumberField(S, TEXT("y"))),
                int32(NumberField(S, TEXT("width"))), int32(NumberField(S, TEXT("height")))));
        }

    TSet<int32> Types;
    for (const FLayer& Layer : Layers) Types.Add(Layer.Type);
    bool bAllCached = true;
    Out.Textures.Reset();
    {
        FScopeLock Guard(&Lock);
        for (const int32 Type : Types)
        {
            UTexture2D* Cached = TextureCache.FindRef(Key + FString::Printf(TEXT("#%d"), Type)).Get();
            if (Cached) Out.Textures.Add(uint32(Type), Cached); else bAllCached = false;
        }
    }
    if (bAllCached && bPrefetchOnly) return true;
    Layers.StableSort([](const FLayer& A, const FLayer& B) { return A.Order < B.Order; });
    if (!bAllCached)
    {
        // r519: composed in the background already? Then only the texture objects are made here.
        TMap<int32, FReadyTexture> Ready;
        bool bFailed = false, bPending = false;
        {
            FScopeLock Guard(&Lock);
            if (TMap<int32, FReadyTexture>* Found = ReadyTextures.Find(Key))
            {
                if (!bPrefetchOnly) { Ready = MoveTemp(*Found); ReadyTextures.Remove(Key); }
                else return true;
            }
            bFailed = FailedTextures.Contains(Key);
            bPending = PendingTextures.Contains(Key);
        }
        if (bPrefetchOnly)
        {
            if (bFailed) return true;
            if (bPending) return false;
            {
                FScopeLock Guard(&Lock);
                PendingTextures.Add(Key);
            }
            FWowCharacterTexture::BeginAsyncWork();
            Async(EAsyncExecution::ThreadPool, [Mpq, Key, ClassicModelPath, SortedLayers = Layers, Sizes, LayoutSize, Sections, Equipment]()
            {
                TMap<int32, TArray<uint8>> Canvases;
                int32 Missing = 0;
                const double Started = FPlatformTime::Seconds();
                const bool bBody = ComposeCanvases(Mpq, nullptr, SortedLayers, Sizes, LayoutSize, Sections, Equipment, Canvases, Missing);
                FWowCharacterTexture::EndAsyncWork(); // no MPQ access past this point
                TMap<int32, FReadyTexture> Result;
                if (bBody)
                    for (auto& Pair : Canvases)
                    {
                        const FIntPoint Size = Sizes.Contains(Pair.Key) ? Sizes[Pair.Key] : LayoutSize;
                        FReadyTexture& R = Result.Add(Pair.Key);
                        R.W = Size.X; R.H = Size.Y;
                        R.Mips = BuildMipChain(Pair.Value, Size.X, Size.Y);
                    }
                FScopeLock Guard(&Lock);
                PendingTextures.Remove(Key);
                if (!bBody) { FailedTextures.Add(Key); return; }
                if (ReadyTextures.Num() >= 6) ReadyTextures.Reset(); // at most a few waiting (~10 MB each)
                ReadyTextures.Add(Key, MoveTemp(Result));
                UE_LOG(LogWowHD, Log, TEXT("HD textures composed in the background: %s (%.0f ms, %d missing files)"),
                    *ClassicModelPath, (FPlatformTime::Seconds() - Started) * 1000.0, Missing);
            });
            return false;
        }
        Out.Textures.Reset();
        if (Ready.Num() > 0)
        {
            for (const auto& Pair : Ready)
                if (UTexture2D* Texture = CreateTextureFromMips(Pair.Value.Mips, Pair.Value.W, Pair.Value.H, *FString::Printf(TEXT("WowHDCharTex%d"), Pair.Key)))
                {
                    Out.Textures.Add(uint32(Pair.Key), Texture);
                    FScopeLock Guard(&Lock);
                    if (TextureCache.Num() >= 256) TextureCache.Reset();
                    TextureCache.Add(Key + FString::Printf(TEXT("#%d"), Pair.Key), Texture);
                }
            UE_LOG(LogWowHD, Log, TEXT("HD character %s: %d textures from the background composer"), *ClassicModelPath, Out.Textures.Num());
        }
        else
        {
        TMap<int32, TArray<uint8>> Canvases;
        int32 Missing = 0;
        // Without the HD_Data textures every atlas would be black: use the classic model instead.
        if (!ComposeCanvases(Mpq, Cache, Layers, Sizes, LayoutSize, Sections, Equipment, Canvases, Missing))
        {
            UE_LOG(LogWowHD, Warning, TEXT("HD character %s: no body textures found (copy HD_Data into dist\\Data\\HD); using the classic model"), *ClassicModelPath);
            return false;
        }
        for (auto& Pair : Canvases)
        {
            const FIntPoint Size = Sizes.Contains(Pair.Key) ? Sizes[Pair.Key] : LayoutSize;
            if (UTexture2D* Texture = CreateMippedTexture(Pair.Value, Size.X, Size.Y, *FString::Printf(TEXT("WowHDCharTex%d"), Pair.Key)))
            {
                Out.Textures.Add(uint32(Pair.Key), Texture);
                FScopeLock Guard(&Lock);
                if (TextureCache.Num() >= 256) TextureCache.Reset();
                TextureCache.Add(Key + FString::Printf(TEXT("#%d"), Pair.Key), Texture);
            }
        }
        UE_LOG(LogWowHD, Log, TEXT("HD character %s: %d layers, %d textures, %d missing files, %d geoset rules"),
            *ClassicModelPath, Layers.Num(), Out.Textures.Num(), Missing, Out.Geosets.Num());
        FString GeosetLog;
        for (const auto& Pair : Out.Geosets) GeosetLog += FString::Printf(TEXT(" %d"), int32(Pair.Key) * 100 + int32(Pair.Value));
        UE_LOG(LogWowHD, Log, TEXT("HD character %s choices:%s geosets:%s"), *ClassicModelPath, *ChoiceLog, *GeosetLog);
        }
    }
    if (!Out.Textures.Contains(1)) return false;
    Out.ModelPath = HDModelPath(ClassicModelPath);
    return true;
}

bool FWowHDCharacter::Build(FMpqManager* Mpq, FWowAssetCache* Cache, const FString& ClassicModelPath,
    const FWowCharacterTexture::FCustomization& Customization,
    const FWowCharacterTexture::FBodyEquipment& Equipment,
    const TMap<uint16, uint16>& ClassicDefaultGeosets,
    const TMap<uint16, uint16>& ClassicEquipmentGeosets,
    FAppearance& Out)
{
    return WowHDBuildImpl(Mpq, Cache, ClassicModelPath, Customization, Equipment, ClassicDefaultGeosets, ClassicEquipmentGeosets, Out, false);
}

bool FWowHDCharacter::PrefetchTextures(FMpqManager* Mpq, const FString& ClassicModelPath,
    const FWowCharacterTexture::FCustomization& Customization,
    const FWowCharacterTexture::FBodyEquipment& Equipment)
{
    FAppearance Unused;
    const TMap<uint16, uint16> NoGeosets;
    return WowHDBuildImpl(Mpq, nullptr, ClassicModelPath, Customization, Equipment, NoGeosets, NoGeosets, Unused, true);
}
