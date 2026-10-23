#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowQuestTextReveal.h"
#include "Widgets/SWowProgressiveQuestText.h"
#include "WowClientPreferences.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestWordsTest,"WowUnreal.UI.QuestText.MarkupAndTiming",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FQuestWordsTest::RunTest(const FString&)
{
    const FString Source=TEXT("Hello <gold>brave &amp; bold</> adventurer.\n\u00c5pne sekken!");
    const FWowQuestTextReveal Text(Source);
    TestEqual(TEXT("Formatting tags do not consume words"),Text.WordEnds.Num(),7);
    TestEqual(TEXT("Opening frame contains no partial letters"),Text.Prefix(0),FString());
    TestEqual(TEXT("First word complete"),Text.Prefix(1),FString(TEXT("Hello")));
    TestEqual(TEXT("Partial colored span closes its tag"),Text.Prefix(2),FString(TEXT("Hello <gold>brave</>")));
    TestEqual(TEXT("Entity stays complete"),Text.Prefix(3),FString(TEXT("Hello <gold>brave &amp;</>")));
    TestEqual(TEXT("Completed markup is unchanged"),Text.Prefix(99),Source);
    TestEqual(TEXT("Queued paragraph stays hidden before its delay"),Text.VisibleWords(-1,false),0);
    TestEqual(TEXT("Twelve words per second"),Text.VisibleWords(.25,false),3);
    TestEqual(TEXT("Fast mode skips a paragraph delay"),Text.VisibleWords(-3,true),7);
    TestEqual(TEXT("Full text at completion"),Text.VisibleWords(Text.Duration()+.01,false),7);
    TestEqual(TEXT("Empty paragraphs finish immediately"),FWowQuestTextReveal().Duration(),0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestRevealWidgetTest,"WowUnreal.UI.QuestText.StableLayoutAndToggle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FQuestRevealWidgetTest::RunTest(const FString&)
{
    auto Styles=MakeShared<FSlateStyleSet>(TEXT("QuestRevealTest"));
    Styles->Set(TEXT("body"),FTextBlockStyle().SetFont(FCoreStyle::GetDefaultFontStyle("Regular",12)));
    bool bFast=false;
    const double Started=FSlateApplication::Get().GetCurrentTime();
    auto Widget=SNew(SWowProgressiveQuestText,FString(TEXT("One two three four five six seven eight nine ten.")),Styles,FName(TEXT("body")))
        .Delay(1).Instant_Lambda([&](){return bFast;});
    Widget->SlatePrepass();
    const auto FullSize=Widget->GetDesiredSize();
    TestEqual(TEXT("Starts empty"),Widget->GetVisibleWordCount(),0);
    Widget->Tick(FGeometry(),Started+.5,.5f);
    TestEqual(TEXT("Waits for preceding narrative"),Widget->GetVisibleWordCount(),0);
    Widget->Tick(FGeometry(),Started+1.25,.75f);
    TestEqual(TEXT("Then reveals complete words"),Widget->GetVisibleWordCount(),3);
    Widget->SlatePrepass();
    TestTrue(TEXT("Partial text reserves full paragraph dimensions"),Widget->GetDesiredSize().Equals(FullSize));
    bFast=true;
    Widget->Tick(FGeometry(),Started+1.3,.05f);
    TestEqual(TEXT("Turning Fast Quest Text on reveals remainder immediately"),Widget->GetVisibleWordCount(),10);
    bFast=false;
    Widget->Tick(FGeometry(),Started+1.4,.1f);
    TestEqual(TEXT("Turning fast off does not erase already-read text"),Widget->GetVisibleWordCount(),10);
    Widget->SlatePrepass();
    TestTrue(TEXT("Rewards below the paragraph keep their position"),Widget->GetDesiredSize().Equals(FullSize));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestTextPreferenceTest,"WowUnreal.UI.QuestText.SavedPreference",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FQuestTextPreferenceTest::RunTest(const FString&)
{
    const FString OriginalIni=GGameUserSettingsIni;
    const FString Fixture=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation"),FGuid::NewGuid().ToString()+TEXT("-QuestText.ini"));
    FConfigFile Config; Config.bCanSaveAllSections=true; Config.NoSave=false;
    GConfig->Add(Fixture,Config);
    GGameUserSettingsIni=Fixture;
    ON_SCOPE_EXIT { GGameUserSettingsIni=OriginalIni; GConfig->UnloadFile(Fixture); IFileManager::Get().Delete(*Fixture); };
    TestTrue(TEXT("CVar uses the shared preference registry"),FWowClientPreferences::HasWorldOption(TEXT("instantQuestText")));
    const bool Before=FWowClientPreferences::GetWorldOption(TEXT("instantquesttext"));
    ON_SCOPE_EXIT { FWowClientPreferences::SetWorldOption(TEXT("instantquesttext"),Before); };
    FWowClientPreferences::SetWorldOption(TEXT("instantquesttext"),true);
    FWowClientPreferences::SetWorldOption(TEXT("instantquesttext"),false);
    bool Stored=true;
    GConfig->GetBool(TEXT("WowUnreal.ClientPreferences"),TEXT("instantquesttext"),Stored,GGameUserSettingsIni);
    TestFalse(TEXT("Slow mode is saved"),Stored);
    FConfigFile OnDisk; OnDisk.Read(Fixture); bool DiskValue=true;
    TestTrue(TEXT("Preference exists on disk"),OnDisk.GetBool(TEXT("WowUnreal.ClientPreferences"),TEXT("instantquesttext"),DiskValue));
    TestFalse(TEXT("Slow mode persists across a file reload"),DiskValue);
    FWowClientPreferences::SetWorldOption(TEXT("instantquesttext"),true);
    GConfig->GetBool(TEXT("WowUnreal.ClientPreferences"),TEXT("instantquesttext"),Stored,GGameUserSettingsIni);
    TestTrue(TEXT("Fast mode is saved"),Stored);
    return true;
}
#endif

