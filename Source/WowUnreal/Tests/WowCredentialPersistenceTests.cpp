#include "Misc/AutomationTest.h"
#include "WowCredentialStore.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowCredentialPersistenceTest, "WowUnreal.Standalone.ProfilePersistence",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowCredentialPersistenceTest::RunTest(const FString& Parameters)
{
    const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/ProfilePersistence"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Dir, true);
    const FString File = FPaths::Combine(Dir, TEXT("WowCredentials.json"));
    if (!TestTrue(TEXT("Isolated empty store"), FFileHelper::SaveStringToFile(TEXT("[]"), *File))) return false;
    struct FRestoreCommandLine
    {
        FString Saved = FCommandLine::Get();
        ~FRestoreCommandLine() { FCommandLine::Set(*Saved); }
    } Restore;
    FCommandLine::Set(*FString::Printf(TEXT("-GameUserSettingsINI=\"%s\""),
        *FPaths::Combine(Dir, TEXT("GameUserSettings.ini"))));
    TStrongObjectPtr<UWowCredentialStore> First(NewObject<UWowCredentialStore>());
    TestTrue(TEXT("Empty shared store loads without importing unrelated profiles"), First->LoadCredentials());
    TestEqual(TEXT("Intentionally empty remains empty"), First->GetAllCredentials().Num(), 0);
    FWowCredential C;
    C.Alias = TEXT("Regression realm"); C.Username = TEXT("FixtureUser");
    C.ServerAddress = TEXT("example.invalid"); C.AuthPort = 4321;
    C.Password = TEXT("fixture-secret-\u00e6\u00f8\u00e5"); C.bIsDefault = true;
    First->AddCredential(C);
    TestTrue(TEXT("Save alongside shared settings"), First->SaveCredentials());
    FString Encoded;
    TestTrue(TEXT("Portable file exists"), FFileHelper::LoadFileToString(Encoded, *File));
    TestFalse(TEXT("No plaintext fixture password"), Encoded.Contains(C.Password));
    TestTrue(TEXT("OS-protected format"), Encoded.Contains(TEXT("pw_dpapi_v1")));
    TestTrue(TEXT("Store retains sealed data only"), First->GetAllCredentials()[0].Password.IsEmpty());
    TStrongObjectPtr<UWowCredentialStore> Restarted(NewObject<UWowCredentialStore>());
    TestTrue(TEXT("Fresh instance loads persisted profile"), Restarted->LoadCredentials());
    TestEqual(TEXT("Profile count"), Restarted->GetAllCredentials().Num(), 1);
    const auto Restored = Restarted->GetDefaultCredential();
    TestEqual(TEXT("Alias"), Restored.Alias, C.Alias);
    TestEqual(TEXT("Realm"), Restored.ServerAddress, C.ServerAddress);
    TestEqual(TEXT("Port"), Restored.AuthPort, C.AuthPort);
    TestTrue(TEXT("Password round trip"), Restored.Password == C.Password);
    FWowCredential Vanilla=C;Vanilla.GameVersion=0;Vanilla.WebsiteUrl=TEXT("https://example.invalid/vanilla");
    Vanilla.AccountUrl=TEXT("https://example.invalid/account");Vanilla.TermsUrl=TEXT("https://example.invalid/terms");
    Restarted->AddCredential(Vanilla);
    TestEqual(TEXT("Same alias remains distinct across versions"),Restarted->GetAllCredentials().Num(),2);
    TestTrue(TEXT("Save versioned metadata"),Restarted->SaveCredentials());
    TestTrue(TEXT("Reload versioned metadata"),Restarted->LoadCredentials());
    TestEqual(TEXT("Vanilla website round trip"),Restarted->GetDefaultCredential(0).WebsiteUrl,Vanilla.WebsiteUrl);
    TestEqual(TEXT("Vanilla account URL round trip"),Restarted->GetDefaultCredential(0).AccountUrl,Vanilla.AccountUrl);
    TestEqual(TEXT("Wrath default unchanged"),Restarted->GetDefaultCredential().GameVersion,2);
    Restarted->RemoveCredential(C.Alias,0);
    TestEqual(TEXT("Removing Vanilla preserves Wrath alias"),Restarted->GetAllCredentials().Num(),1);
    Restarted->RemoveCredential(C.Alias);
    TestTrue(TEXT("Save empty roster"), Restarted->SaveCredentials());
    TestTrue(TEXT("Reload empty roster"), First->LoadCredentials());
    TestEqual(TEXT("Removed profiles stay removed"), First->GetAllCredentials().Num(), 0);
    const FString Legacy = TEXT("[{\"alias\":\"legacy\",\"username\":\"FixtureUser\",\"server\":\"example.invalid\",\"password\":\"migration-fixture\"}]");
    TestTrue(TEXT("Write isolated legacy fixture"), FFileHelper::SaveStringToFile(Legacy, *File));
    TestTrue(TEXT("Migrate legacy store"), First->LoadCredentials());
    TestEqual(TEXT("Legacy bookmark defaults to WotLK"),First->GetDefaultCredential().GameVersion,2);
    TestTrue(TEXT("Migrated password restored"), First->GetDefaultCredential().Password == TEXT("migration-fixture"));
    FFileHelper::LoadFileToString(Encoded, *File);
    TestFalse(TEXT("Legacy plaintext removed from disk"), Encoded.Contains(TEXT("migration-fixture")));
    TestTrue(TEXT("Migration writes protected format"), Encoded.Contains(TEXT("pw_dpapi_v1")));
    IFileManager::Get().Delete(*File);
    IFileManager::Get().DeleteDirectory(*Dir, false, false);
    return true;
}
#endif
