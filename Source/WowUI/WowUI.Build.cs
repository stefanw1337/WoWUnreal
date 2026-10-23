using UnrealBuildTool;
using System.IO;

public class WowUI : ModuleRules
{
    public WowUI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UMG",
            "Slate",
            "SlateCore",
            "WowData",
            "WowAssets",
            "WowClient",
            "WowNetwork",
            "WowWorld",
            "XmlParser",
            "InputCore",
            "Lua"
        });
        PrivateDependencyModuleNames.Add("ApplicationCore"); // clipboard for /fstack

        // pugixml for robust XML parsing (WoW FrameXML)
        string PugiPath = Path.Combine(
            ModuleDirectory,
            "..",
            "ThirdParty",
            "pugixml"
        );

        if (Directory.Exists(PugiPath))
        {
            PublicIncludePaths.Add(PugiPath);
            PrivateDefinitions.Add("HAS_PUGIXML=1");
        }
    }
}
