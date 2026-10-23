#include "WowFloatingText.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "WowClientPreferences.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/CoreStyle.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "WowVRSupport.h"
#include "WowVrCards.h"
#include "Camera/PlayerCameraManager.h"

UWowFloatingText::UWowFloatingText()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    TextWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("CombatText"));
    TextWidgetComponent->SetupAttachment(this);
    TextWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
    TextWidgetComponent->SetDrawSize(FVector2D(240, 80));
    TextWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
    TextWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TextWidgetComponent->SetCastShadow(false);
}

void UWowFloatingText::BeginPlay()
{
    Super::BeginPlay();
    StartLocation = GetComponentLocation();
}

void UWowFloatingText::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bIsFloating) UpdateFloating(DeltaTime);
}

UWowFloatingText* UWowFloatingText::SpawnFloatingText(AActor* TargetActor, const FWowFloatingTextInfo& TextInfo)
{
    if (!IsValid(TargetActor) || !TargetActor->GetWorld() ||
        !FWowClientPreferences::GetWorldOption(TEXT("floatingcombattextdamage"))) return nullptr;
    // No numbers for units out of sight (r252, Stefan): the text is drawn in screen space, so a
    // fight behind a wall showed its damage through the wall. Hidden only when both the unit's
    // middle and its head are blocked by world geometry (terrain, buildings, doodads) as seen
    // from the camera; other units never block.
    if (APlayerController* ViewPC = TargetActor->GetWorld()->GetFirstPlayerController())
    {
        if (ViewPC->PlayerCameraManager && TargetActor != ViewPC->GetPawn())
        {
            const FVector Eye = ViewPC->PlayerCameraManager->GetCameraLocation();
            // Creature actors stand on their feet; their click capsule marks the body centre.
            float HalfHeight = 90.0f;
            FVector Middle = TargetActor->GetActorLocation() + FVector(0, 0, HalfHeight);
            if (const auto* Capsule = TargetActor->FindComponentByClass<UCapsuleComponent>())
            {
                HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
                Middle = Capsule->GetComponentLocation();
            }
            FCollisionQueryParams Query(SCENE_QUERY_STAT(WowCombatTextSight), false, TargetActor);
            if (ViewPC->GetPawn()) Query.AddIgnoredActor(ViewPC->GetPawn());
            TArray<AActor*> Attached;
            TargetActor->GetAttachedActors(Attached, true, true);
            Query.AddIgnoredActors(Attached);
            const FCollisionObjectQueryParams WorldOnly(ECC_WorldStatic);
            FHitResult Hit;
            const FVector Head = Middle + FVector(0, 0, HalfHeight * 0.8f);
            if (TargetActor->GetWorld()->LineTraceSingleByObjectType(Hit, Eye, Middle, WorldOnly, Query) &&
                TargetActor->GetWorld()->LineTraceSingleByObjectType(Hit, Eye, Head, WorldOnly, Query))
                return nullptr;
        }
    }
    static TArray<TWeakObjectPtr<UWowFloatingText>> ActiveTexts;
    ActiveTexts.RemoveAll([](const auto& Item) { return !Item.IsValid() || !Item->bIsFloating; });
    FString Digits = TextInfo.Text;
    const bool Critical = Digits.RemoveFromEnd(TEXT("!"));
    int64 Amount = 0;
    const bool Numeric = !Digits.IsEmpty() && Digits.IsNumeric() && LexTryParseString(Amount, *Digits) && Amount > 0;
    // Running total per target: hits of the same kind (colour) keep adding up while they
    // come within SumWindow of each other - long enough for a 3.5 s two-hander swing or a
    // 2.5 s cast plus latency, even after the previous number has faded. The shown number
    // is replaced by the new total with the latest hit in brackets: "1234 (+300)".
    struct FSumState { int64 Sum = 0; double LastTime = -1000.0; FLinearColor Color; TWeakObjectPtr<UWowFloatingText> Text; };
    // One running total per target and kind (colour): incoming heals no longer reset the
    // damage total on the same unit and vice versa (r279).
    static TMap<TPair<TWeakObjectPtr<AActor>, uint32>, FSumState> Sums;
    const TPair<TWeakObjectPtr<AActor>, uint32> SumKey(TargetActor, GetTypeHash(TextInfo.Color.ToFColor(false)));
    constexpr double SumWindow = 4.5;
    const double Now = TargetActor->GetWorld()->GetTimeSeconds();
    for (auto It = Sums.CreateIterator(); It; ++It)
        if (!It.Key().Key.IsValid() || Now - It.Value().LastTime > SumWindow || Now < It.Value().LastTime) It.RemoveCurrent();
    FString DisplayText = TextInfo.Prefix + TextInfo.Text + TextInfo.Suffix;
    FSumState* Running = nullptr;
    if (Numeric && FWowClientPreferences::GetWorldOption(TextInfo.SumOption))
    {
        Running = Sums.Find(SumKey);
        if (Running && Running->Color == TextInfo.Color)
        {
            Running->Sum += Amount;
            Running->LastTime = Now;
            DisplayText = FString::Printf(TEXT("%s%lld (+%lld)%s%s"), *TextInfo.Prefix, Running->Sum, Amount,
                Critical ? TEXT("!") : TEXT(""), *TextInfo.Suffix);
            if (UWowFloatingText* Existing = Running->Text.Get(); Existing && Existing->bIsFloating && Existing->CombatText.IsValid())
            {
                // Replace the number in place and give it a full lifetime again.
                Existing->NumericAmount = Running->Sum;
                Existing->CombatText->SetText(FText::FromString(DisplayText));
                Existing->FloatTimer = 0.0f;
                Existing->CombatText->SetRenderOpacity(1.0f);
                if (AActor* ExistingHost = Existing->GetOwner()) ExistingHost->SetLifeSpan(Existing->FloatDuration + 0.1f);
                return Existing;
            }
        }
        else
        {
            Running = &Sums.Add(SumKey);
            Running->Sum = Amount;
            Running->LastTime = Now;
            Running->Color = TextInfo.Color;
        }
    }
    AActor* Host = TargetActor->GetWorld()->SpawnActor<AActor>();
    if (!Host) return nullptr;
    auto* Text = NewObject<UWowFloatingText>(Host);
    Host->AddOwnedComponent(Text);
    Host->SetRootComponent(Text);
    float HeadOffset = 200.0f;
    if (auto* Capsule = TargetActor->FindComponentByClass<UCapsuleComponent>())
        HeadOffset = Capsule->GetScaledCapsuleHalfHeight() + 45.0f;
    Text->SetWorldLocation(TargetActor->GetActorLocation() + FVector(0,0,HeadOffset));
    Text->RegisterComponent();
    if (auto* PC = TargetActor->GetWorld()->GetFirstPlayerController())
        Text->TextWidgetComponent->SetOwnerPlayer(PC->GetLocalPlayer());
    Text->TextWidgetComponent->RegisterComponent();
    Text->DamageTarget = TargetActor;
    Text->NumericAmount = Numeric ? Amount : 0;
    Text->bCriticalNumber = Critical;
    // Pixel spacing stays readable regardless of camera distance. Move older
    // numbers upward; new hits start below them instead of sharing their origin.
    // Fast hits (several within ~0.6 s) also fan out sideways so they never overlap.
    // Incoming numbers (r279) start above the head and sink; their stack grows downwards.
    Text->bDownward = TextInfo.bDownward;
    if (Text->bDownward) Text->ScreenStackOffset = -70.0f;
    int32 RecentOnTarget = 0;
    for (const auto& Item : ActiveTexts)
    {
        if (Item->DamageTarget.Get() != TargetActor || Item->bDownward != Text->bDownward) continue;
        Item->ScreenStackOffset += Text->bDownward ? 40.0f : -64.0f;
        if (Item->FloatTimer < 0.6f) ++RecentOnTarget;
    }
    static const float SideSlots[] = { 0.0f, 46.0f, -46.0f, 22.0f, -22.0f };
    Text->ScreenSideOffset = SideSlots[RecentOnTarget % UE_ARRAY_COUNT(SideSlots)] + FMath::FRandRange(-6.0f, 6.0f);
    FWowFloatingTextInfo Shown = TextInfo;
    Shown.Text = DisplayText;
    Text->StartFloating(Shown);
    ActiveTexts.Add(Text);
    if (Running) { Running->Text = Text; Text->NumericAmount = Running->Sum; }
    Host->SetLifeSpan(FMath::Max(TextInfo.Duration,0.05f) + 0.1f);
    return Text;
}

void UWowFloatingText::StartFloating(const FWowFloatingTextInfo& TextInfo)
{
    bIsFloating = true;
    FloatTimer = 0;
    FloatDuration = FMath::Max(TextInfo.Duration,0.05f);
    FloatSpeed = TextInfo.Speed;
    StartLocation = GetComponentLocation();
    BaseColor = TextInfo.Color;
    BaseFontSize = FMath::Clamp(FMath::RoundToInt(TextInfo.FontSize * 0.8f),18,48);
    CombatFont = FCoreStyle::GetDefaultFontStyle("Bold",BaseFontSize);
    CombatFont.OutlineSettings.OutlineSize = 1;
    CombatFont.OutlineSettings.OutlineColor = FLinearColor(0.025f,0.018f,0.005f,0.95f);
    TextWidgetComponent->SetSlateWidget(
        SNew(SBox).WidthOverride(240).HeightOverride(80).HAlign(HAlign_Center).VAlign(VAlign_Center)
        .Visibility(EVisibility::HitTestInvisible)
        [ SAssignNew(CombatText,STextBlock).Text(FText::FromString(TextInfo.Text))
            .Font(CombatFont).ColorAndOpacity(BaseColor)
            .ShadowOffset(FVector2D(1.5f,2.0f)).ShadowColorAndOpacity(FLinearColor(0,0,0,0.85f)) ]);
    TextWidgetComponent->SetVisibility(true);
    SetComponentTickEnabled(true);
    UpdateFloating(0);
}

void UWowFloatingText::UpdateFloating(float DeltaTime)
{
    FloatTimer += DeltaTime;
    if (FloatTimer >= FloatDuration)
    {
        bIsFloating = false;
        TextWidgetComponent->SetVisibility(false);
        SetComponentTickEnabled(false);
        return;
    }
    SetWorldLocation(StartLocation);
    // VR: a screen-space number shows in one eye only. In the headset the text is a world card
    // facing the head, and the pixel offsets (stacking, sideways fan, rise) move the card instead.
    const FVector2D PixelOffset(ScreenSideOffset, ScreenStackOffset + (bDownward ? 1.0f : -1.0f) * FloatSpeed * 0.35f * FloatTimer);
    const bool bVr = FWowVRSupport::IsActive();
    const EWidgetSpace WantSpace = bVr ? EWidgetSpace::World : EWidgetSpace::Screen;
    if (TextWidgetComponent->GetWidgetSpace() != WantSpace)
    {
        TextWidgetComponent->SetWidgetSpace(WantSpace);
        TextWidgetComponent->MarkRenderStateDirty(); // the scene proxy exists only in world space
        if (!bVr) TextWidgetComponent->SetRelativeTransform(FTransform::Identity);
    }
    if (bVr)
    {
        const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
        if (PC && PC->PlayerCameraManager)
        {
            const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
            const float Scale = WowVrCards::DistanceScale(float(FVector::Dist(Eye, StartLocation)));
            const FVector ToEye = (Eye - StartLocation).GetSafeNormal2D();
            const FVector Right = FVector::CrossProduct(FVector::UpVector, -ToEye); // viewer's right
            TextWidgetComponent->SetWorldLocation(StartLocation + Right * (PixelOffset.X * Scale) - FVector::UpVector * (PixelOffset.Y * Scale));
            WowVrCards::Face(TextWidgetComponent, Eye, Scale);
        }
    }
    if (CombatText.IsValid())
    {
        CombatText->SetRenderTransform(FSlateRenderTransform(bVr ? FVector2D::ZeroVector : PixelOffset));
        // Stay fully readable initially, then fade the glyph, outline and shadow together.
        const float FadeStart = FloatDuration * 0.65f;
        CombatText->SetRenderOpacity(1.0f - FMath::Clamp((FloatTimer-FadeStart)/(FloatDuration-FadeStart),0.0f,1.0f));
        const int32 Size = FMath::RoundToInt(BaseFontSize*(1.0f + 0.16f*(1.0f-FMath::Clamp(FloatTimer/0.16f,0.0f,1.0f))));
        if (CombatFont.Size != Size) { CombatFont.Size=Size; CombatText->SetFont(CombatFont); }
    }
}
