using UnrealBuildTool;
using System.IO;

public class WowData : ModuleRules
{
    public WowData(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject"
            }
        );

        string StormLibRoot = Path.Combine(
            ModuleDirectory,
            "..",
            "ThirdParty",
            "StormLib"
        );

        string StormLibSource = Path.Combine(
            StormLibRoot,
            "src"
        );

        if (Directory.Exists(StormLibSource))
        {
            PublicIncludePaths.Add(StormLibSource);

            // Prevent StormLib.h from auto-linking StormLibRUD.lib.
            PublicDefinitions.Add("__STORMLIB_NO_STATIC_LINK__=1");

            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                string StormLibLibrary = Path.Combine(
                    StormLibRoot,
                    "bin",
                    "StormLib",
                    "x64",
                    "ReleaseAD",
                    "StormLibRAD.lib"
                );

                PublicAdditionalLibraries.Add(StormLibLibrary);
            }
        }
    }
}