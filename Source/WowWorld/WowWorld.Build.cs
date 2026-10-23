using UnrealBuildTool;
public class WowWorld : ModuleRules
{
    public WowWorld(ReadOnlyTargetRules Target) : base(Target)
    {
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/libsndfile/Win64/libsndfile-1.dll");
        }
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AudioExtensions", "AudioMixer", "WowData", "WowAssets", "WowClient", "MeshDescription", "StaticMeshDescription", "Water", "ProceduralMeshComponent", "HTTP", "Json", "ImageCore" });
    }
}
