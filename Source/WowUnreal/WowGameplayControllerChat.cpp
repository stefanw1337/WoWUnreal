// Chat slash commands that are not handled natively in SubmitFrameXmlChat.
#include "WowGameplayController.h"
#include "WowUIManager.h"
#include "WowLuaVM.h"
#include "WowConnectionManager.h"
#include "WowEntity.h"
#include "WowAudioManager.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "Formats/Dbc/DbcStore.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowChatCommands, Log, All);

void AWowGameplayController::RunLuaSlashCommand(const FString& Command, const FString& Arguments)
{
	FWowLuaVM* Lua = UIManager ? UIManager->GetLuaVM() : nullptr;
	if (!Lua || !Lua->IsInitialized())
	{
		AddFrameXmlChatLine(FString::Printf(TEXT("Unknown chat command: /%s"), *Command), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f));
		return;
	}
	// Long-bracket strings keep quotes/backslashes intact; strip the closing
	// sequence so user text can never break out of the string literal.
	auto Quote = [](FString Text)
	{
		Text.ReplaceInline(TEXT("]=====]"), TEXT(""));
		return FString(TEXT("[=====[")) + Text + TEXT("]=====]");
	};
	const FString Code = FString::Printf(
		TEXT("if __WowUnrealSlashCommand then __WowUnrealSlashCommand(%s, %s) end"),
		*Quote(Command), *Quote(Arguments));
	UE_LOG(LogWowChatCommands, Log, TEXT("Slash command /%s"), *Command);
	if (!Lua->ExecuteString(Code, TEXT("=SlashCommand")))
	{
		AddFrameXmlChatLine(FString::Printf(TEXT("Error running /%s (see log)."), *Command), FLinearColor(1.0f, 0.2f, 0.2f, 1.0f));
	}
}

namespace
{
// Text emote tables that are only needed here: EmotesText (text ids), EmotesTextData
// (localized strings) and EmotesTextSound (voice line per race/gender).
struct FTextEmoteTables
{
	bool bLoaded = false;
	TMap<uint32, TArray<uint32>> TextIds;   // EmotesText id -> 16 EmotesTextData ids
	TMap<uint32, FString> Texts;            // EmotesTextData id -> text
	TMap<uint32, uint32> Sounds;            // (emote << 16 | race << 8 | sex) -> SoundEntries id

	void Load(FMpqManager& Mpq)
	{
		bLoaded = true;
		TArray<uint8> Bytes;
		FDbcParser P;
		// EmotesText.dbc: ID, Command, EmoteID (animation), EmoteText[16].
		if (Mpq.ReadFile(TEXT("DBFilesClient\\EmotesText.dbc"), Bytes) && P.Parse(Bytes) && P.GetFieldCount() >= 19)
			for (uint32 I = 0; I < P.GetRecordCount(); ++I)
			{
				TArray<uint32>& Ids = TextIds.Add(P.GetUInt(I, 0));
				for (uint32 F = 3; F < 19; ++F) Ids.Add(P.GetUInt(I, F));
			}
		// EmotesTextData.dbc: ID, Text (localized).
		if (Mpq.ReadFile(TEXT("DBFilesClient\\EmotesTextData.dbc"), Bytes) && P.Parse(Bytes) && P.GetFieldCount() >= 2)
			for (uint32 I = 0; I < P.GetRecordCount(); ++I)
				Texts.Add(P.GetUInt(I, 0), P.GetLocalizedString(I, 1));
		// EmotesTextSound.dbc: ID, EmotesTextId, RaceId, SexId (0 male, 1 female), SoundId.
		if (Mpq.ReadFile(TEXT("DBFilesClient\\EmotesTextSound.dbc"), Bytes) && P.Parse(Bytes) && P.GetFieldCount() >= 5)
			for (uint32 I = 0; I < P.GetRecordCount(); ++I)
				Sounds.Add((P.GetUInt(I, 1) << 16) | ((P.GetUInt(I, 2) & 0xFF) << 8) | (P.GetUInt(I, 3) & 0xFF), P.GetUInt(I, 4));
		UE_LOG(LogWowChatCommands, Log, TEXT("Text emotes: %d emotes, %d texts, %d voice lines"), TextIds.Num(), Texts.Num(), Sounds.Num());
	}

	/** EmoteText slot: 0/8 others with target (male/female), 1/9 to the target, 2 sender with
	 *  target, 4/12 others without target, 6 sender without target. Female falls back to male. */
	FString Text(uint32 Emote, int32 Slot, bool bFemale) const
	{
		const TArray<uint32>* Ids = TextIds.Find(Emote);
		if (!Ids) return FString();
		auto Get = [&](int32 Index) { return Ids->IsValidIndex(Index) && (*Ids)[Index] ? Texts.FindRef((*Ids)[Index]) : FString(); };
		FString Result = bFemale && Slot != 2 && Slot != 6 ? Get(Slot + 8) : FString();
		return Result.IsEmpty() ? Get(Slot) : Result;
	}
};
FTextEmoteTables& TextEmoteTables() { static FTextEmoteTables Tables; return Tables; }

/** Replaces each "%s" in order (the EmotesTextData strings are printf style). */
FString FillNames(FString Text, const FString& First, const FString& Second)
{
	int32 At = Text.Find(TEXT("%s"));
	if (At != INDEX_NONE) { Text = Text.Left(At) + First + Text.Mid(At + 2); At = Text.Find(TEXT("%s"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + First.Len()); }
	if (At != INDEX_NONE) Text = Text.Left(At) + Second + Text.Mid(At + 2);
	return Text;
}
}

void AWowGameplayController::OnTextEmote(uint64 SenderGuid, uint32 TextEmoteId, uint32 EmoteNum, const FString& TargetName)
{
	if (!ConnectionManager) return;
	CacheWorldResources();
	FTextEmoteTables& Tables = TextEmoteTables();
	if (!Tables.bLoaded && CachedMpq) Tables.Load(*CachedMpq);

	auto& Handler = ConnectionManager->PacketHandler;
	const uint64 LocalGuid = Handler.EntityManager.LocalPlayerGuid;
	const FWowUnitEntity* Sender = Handler.EntityManager.FindUnit(SenderGuid);
	FString SenderName = Handler.PlayerNameCache.FindRef(SenderGuid);
	if (SenderName.IsEmpty()) SenderName = TEXT("Someone");
	const FString LocalName = Handler.PlayerNameCache.FindRef(LocalGuid);

	// Race/gender of the sender: unit bytes, else the name-query cache.
	uint8 Race = 0, Gender = 0;
	if (Sender) { Race = Sender->GetRaceId(); Gender = Sender->GetGenderId(); }
	else if (const uint8* Cached = Handler.PlayerGenderCache.Find(SenderGuid)) Gender = *Cached;
	const bool bFemale = Gender == 1;

	// Chat line, as the stock client builds it.
	const bool bHasTarget = !TargetName.IsEmpty();
	FString Text;
	if (SenderGuid == LocalGuid)
		Text = FillNames(Tables.Text(TextEmoteId, bHasTarget ? 2 : 6, false), TargetName, FString());
	else if (bHasTarget && !LocalName.IsEmpty() && TargetName.Equals(LocalName, ESearchCase::IgnoreCase))
		Text = FillNames(Tables.Text(TextEmoteId, 1, bFemale), SenderName, FString());
	else
		Text = FillNames(Tables.Text(TextEmoteId, bHasTarget ? 0 : 4, bFemale), SenderName, TargetName);
	// Some EmotesTextData lines name the target with "%t" (Stefan 10.10: "Unreal tells %t a joke.").
	if (Text.Contains(TEXT("%t")))
		Text.ReplaceInline(TEXT("%t"), bHasTarget ? *TargetName : TEXT("someone"), ESearchCase::CaseSensitive);
	if (!Text.IsEmpty())
		OnChatMessage(0x0B, 0, SenderGuid, SenderName, Text, FString());
	else
		UE_LOG(LogWowChatCommands, Warning, TEXT("No text for text emote %u (target '%s')"), TextEmoteId, *TargetName);

	// Voice line (e.g. /joke, /cheer, /laugh) at the sender, like spell sounds.
	const uint32* Sound = Race ? Tables.Sounds.Find((TextEmoteId << 16) | (uint32(Race) << 8) | Gender) : nullptr;
	if (!Sound || !*Sound) return;
	AActor* Source = SenderGuid == LocalGuid ? GetPawn() : SpawnedEntityActors.FindRef(SenderGuid).Get();
	if (!IsValid(Source) || !GetPawn() || FVector::DistSquared(Source->GetActorLocation(), GetPawn()->GetActorLocation()) > FMath::Square(5000.f)) return;
	if (auto* Audio = Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AWowAudioManager::StaticClass())))
		Audio->PlayWorldSoundEffect(*Sound, Source->GetActorLocation());
}
