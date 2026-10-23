#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Mpq/MpqManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowMpqDiscoveryTest,
    "WowUnreal.MpqDiscovery.ArchivesAndPrecedence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowMpqDiscoveryTest::RunTest(const FString&)
{
    FMpqManager Manager;
    const FString Root = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectDir() / TEXT("Source/WowTests/Fixtures/MpqDiscovery"));
    AddExpectedError(TEXT("Failed to open MPQ: invalid.mpq"), EAutomationExpectedErrorFlags::Contains, 1);
    if (!TestTrue(TEXT("Recursively discovers real synthetic archives"), Manager.Initialize(Root))) return false;
    auto Read = [&](const TCHAR* Name)
    {
        TArray<uint8> Bytes;
        if (!TestTrue(FString::Printf(TEXT("Reads %s"), Name), Manager.ReadFile(Name, Bytes))) return FString();
        Bytes.Add(0);
        return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData())));
    };
    TestEqual(TEXT("Base files remain available beneath overrides"), Read(TEXT("base.txt")), FString(TEXT("base")));
    TestEqual(TEXT("Preserves stock locale patch precedence"), Read(TEXT("stock.txt")), FString(TEXT("locale3")));
    TestEqual(TEXT("Discovers a named mixed-case HD archive in nested folders"),
        Read(TEXT("world/hd-only.txt")), FString(TEXT("hd textures")));
    TestEqual(TEXT("Custom patch replaces original/locale content"), Read(TEXT("override.txt")), FString(TEXT("patchA")));
    TestEqual(TEXT("Natural numeric order: patch-10 wins over patch-9"), Read(TEXT("numeric.txt")), FString(TEXT("ten")));
    TestEqual(TEXT("Letter patch order: Z wins over A"), Read(TEXT("letters.txt")), FString(TEXT("Z")));
    TestEqual(TEXT("Nested same-name archive wins over root; ties deterministic"),
        Read(TEXT("nested.txt")), FString(TEXT("nested Z")));
    TestTrue(TEXT("FileExists also sees HD archives"), Manager.FileExists(TEXT("World\\HD-Only.txt")));
    TestFalse(TEXT("Ignores non-MPQ extensions"), Manager.FileExists(TEXT("ignored.txt")));
    TestFalse(TEXT("Absent file stays absent"), Manager.FileExists(TEXT("not-found.txt")));

    TestEqual(TEXT("Foreign stock locale is only fallback"), Read(TEXT("foreign-only.txt")), FString(TEXT("ru patch3")));
    TestEqual(TEXT("Foreign stock patches keep version priority"), Read(TEXT("foreign-version.txt")), FString(TEXT("ru patch3")));

    FMpqManager Stock;
    TestTrue(TEXT("Original WotLK rule mounts stock archives alongside mods"), Stock.Initialize(Root,{},EWowAssetProfile::OriginalWotlk335));
    TestFalse(TEXT("Original rules exclude HD-only files"),Stock.FileExists(TEXT("world/hd-only.txt")));
    TestTrue(TEXT("Original rules retain base files"),Stock.FileExists(TEXT("base.txt")));
    TestEqual(TEXT("Original rules exclude loose overrides"),Stock.GetLooseFileRoots().Num(),0);

    // A different root removes all old handles and overrides; opening twice must
    // not append the previous set or pin a removed mod until process exit.
    TestTrue(TEXT("Reinitialize manager with base-only root"), Manager.Initialize(Root / TEXT("StockOnly")));
    TestFalse(TEXT("Old HD files no longer visible after root change"), Manager.FileExists(TEXT("world/hd-only.txt")));
    TestEqual(TEXT("Base-only read works after root change"), Read(TEXT("isolated.txt")), FString(TEXT("isolated")));
    Manager.Shutdown();
    TestFalse(TEXT("Shutdown clears initialization"), Manager.IsInitialized());
    TestFalse(TEXT("Shutdown closes HD archive access"), Manager.FileExists(TEXT("isolated.txt")));
    return true;
}
#endif
