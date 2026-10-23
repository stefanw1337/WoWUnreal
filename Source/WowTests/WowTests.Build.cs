using UnrealBuildTool;

public class WowTests : ModuleRules
{
    public WowTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UMG"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "WowData",
            "WowNetwork",
            "WowAssets",
            "ProceduralMeshComponent",
            "WowWorld",
            "WowUI",
            "Lua"
        });
    }
}