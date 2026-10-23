#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Mpq/MpqManager.h"
#include "WowAddonLoader.h"
#include "WowLuaVM.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLooseFilesTest,
    "WowUnreal.LooseFiles.OverridesAndAddons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowLooseFilesTest::RunTest(const FString&)
{
    const FString Fixture = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
        TEXT("Automation/LooseFiles") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString Client = Fixture / TEXT("Portable");
    const FString Wow = Fixture / TEXT("Original");
    const FString Data = Wow / TEXT("Data");
    IFileManager& Files = IFileManager::Get();
    Files.MakeDirectory(*Data, true);
    const FString Archive = FPaths::ProjectDir() / TEXT("Source/WowTests/Fixtures/MpqDiscovery/common.MPQ");
    if (!TestEqual(TEXT("Copies synthetic base MPQ"), Files.Copy(*(Data / TEXT("common.MPQ")), *Archive), COPY_OK)) return false;
    auto Write = [&](const FString& Root, const TCHAR* Path, const TCHAR* Content)
    {
        const FString Full = Root / Path;
        Files.MakeDirectory(*FPaths::GetPath(Full), true);
        return TestTrue(TEXT("Writes isolated fixture"), FFileHelper::SaveStringToFile(Content, *Full,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    };
    Write(Client, TEXT("ClientPaths.ini"), TEXT("fixture"));
    TestEqual(TEXT("Finds launcher root above packaged project"),
        FMpqManager::FindClientRoot(Client / TEXT("Client/WowUnreal")), Client);
    TestEqual(TEXT("Editor without launcher marker keeps project root"),
        FMpqManager::FindClientRoot(Fixture / TEXT("EditorProject")), Fixture / TEXT("EditorProject"));
    Write(Data, TEXT("override.txt"), TEXT("legacy"));
    Write(Wow, TEXT("override.txt"), TEXT("original loose"));
    Write(Client, TEXT("override.txt"), TEXT("client loose"));
    Write(Client, TEXT("Sound/Creature/Test.wav"), TEXT("loose audio bytes"));
    Write(Client, TEXT("Interface/Icons/Test.blp"), TEXT("loose texture bytes"));
    Write(Client, TEXT("World/Custom/test.bin"), TEXT("future folder"));
    Write(Client, TEXT("empty.txt"), TEXT(""));
    Write(Fixture, TEXT("outside.txt"), TEXT("not an asset"));
    Write(Client, TEXT("Interface/AddOns/OverrideProbe/OverrideProbe.toc"), TEXT("## Interface: 30300\nMain.lua\n"));
    Write(Client, TEXT("Interface/AddOns/OverrideProbe/Main.lua"), TEXT("LooseFileProbe = 'client'"));
    Write(Wow, TEXT("Interface/AddOns/overrideprobe/overrideprobe.toc"), TEXT("## Interface: 30300\nMain.lua\n"));
    Write(Wow, TEXT("Interface/AddOns/overrideprobe/Main.lua"), TEXT("LooseFileProbe = 'wrong priority'"));
    Write(Data, TEXT("Interface/AddOns/LegacyProbe/LegacyProbe.toc"), TEXT("## Interface: 30300\nMain.lua\n"));
    Write(Data, TEXT("Interface/AddOns/LegacyProbe/Main.lua"), TEXT("LegacyFileProbe = true"));
    FMpqManager Manager;
    if (!TestTrue(TEXT("Initializes original data plus portable overrides"), Manager.Initialize(Data, {Client, Client}))) return false;
    TestEqual(TEXT("Duplicate roots are removed"), Manager.GetLooseFileRoots().Num(), 3);
    auto Read = [&](const TCHAR* Name)
    {
        TArray<uint8> Bytes;
        if (!TestTrue(Name, Manager.ReadFile(Name, Bytes))) return FString();
        Bytes.Add(0);
        return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData())));
    };
    TestEqual(TEXT("Client overrides original loose and archives"), Read(TEXT("override.txt")), FString(TEXT("client loose")));
    TestEqual(TEXT("MPQ remains fallback"), Read(TEXT("base.txt")), FString(TEXT("base")));
    TestEqual(TEXT("Loose sound with WoW separators and case"), Read(TEXT("SOUND\\CREATURE\\TEST.WAV")), FString(TEXT("loose audio bytes")));
    TestTrue(TEXT("Existence agrees with texture read"), Manager.FileExists(TEXT("interface\\icons\\test.blp")));
    TestEqual(TEXT("Texture bytes use same resolver"), Read(TEXT("Interface/Icons/Test.blp")), FString(TEXT("loose texture bytes")));
    TestEqual(TEXT("Future directories need no whitelist change"), Read(TEXT("World/Custom/test.bin")), FString(TEXT("future folder")));
    TestEqual(TEXT("An empty file is a valid override"), Read(TEXT("empty.txt")), FString());
    TArray<uint8> Ignored;
    for (const FString& Bad : {FString(TEXT("../outside.txt")), FString(TEXT("Sound/../../outside.txt")),
        Fixture / TEXT("outside.txt"), FString(TEXT("//server/share/test")), FString(TEXT("Sound/test.wav:stream"))})
    {
        TestFalse(TEXT("Rejects non-relative asset read"), Manager.ReadFile(Bad, Ignored));
        TestFalse(TEXT("Rejects non-relative existence check"), Manager.FileExists(Bad));
    }
    const TArray<FString> Addons = FWowAddonLoader::DiscoverAddons(&Manager);
    TestEqual(TEXT("Same addon in multiple roots only discovered once"), Addons.Num(), 2);
    TestTrue(TEXT("Discovers portable addon"), Addons.Contains(TEXT("OverrideProbe")));
    TestTrue(TEXT("Keeps legacy Data addon support"), Addons.Contains(TEXT("LegacyProbe")));
    FWowLuaVM VM;
    if (TestTrue(TEXT("Initializes Lua"), VM.Initialize()))
    {
        FWowAddonLoader::LoadAllAddons(&Manager, &VM);
        TestTrue(TEXT("TOC loads actual Lua from correct roots"), VM.ExecuteString(
            TEXT("assert(LooseFileProbe == 'client'); assert(LegacyFileProbe == true)")));
    }
    TestTrue(TEXT("Reinitialization removes explicit client root"), Manager.Initialize(Data));
    TestEqual(TEXT("Original install loose file wins over Data and MPQ"), Read(TEXT("override.txt")), FString(TEXT("original loose")));
    TestFalse(TEXT("Former portable-only sound disappears"), Manager.FileExists(TEXT("Sound/Creature/Test.wav")));
    Files.Delete(*(Wow / TEXT("override.txt")));
    TestEqual(TEXT("Legacy Data loose file is next fallback"), Read(TEXT("override.txt")), FString(TEXT("legacy")));
    Files.Delete(*(Data / TEXT("override.txt")));
    TestTrue(TEXT("After removing loose overrides, archive file is visible"), Manager.FileExists(TEXT("override.txt")));
    Manager.Shutdown();
    TestTrue(TEXT("Shutdown clears loose roots"), Manager.GetLooseFileRoots().IsEmpty());
    TestFalse(TEXT("Shutdown prevents loose reads"), Manager.ReadFile(TEXT("empty.txt"), Ignored));
    return true;
}
#endif
