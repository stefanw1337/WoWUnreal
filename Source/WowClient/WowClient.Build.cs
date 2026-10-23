using UnrealBuildTool;
public class WowClient : ModuleRules
{
    public WowClient(ReadOnlyTargetRules Target) : base(Target)
    {
        if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("Crypt32.lib");
        PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "Slate", "SlateCore", "InputCore", "WowData", "DLSSBlueprint", "StreamlineDLSSGBlueprint", "StreamlineReflexBlueprint", "StreamlineBlueprint" });
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "WowNetwork", "Json", "JsonUtilities" });
    }
}
