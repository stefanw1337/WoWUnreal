using UnrealBuildTool;
public class WowAssets : ModuleRules
{
    public WowAssets(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RenderCore", "RHI", "WowData", "WowNetwork", "MeshDescription", "StaticMeshDescription", "SkeletalMeshDescription", "AnimationCore", "ProceduralMeshComponent" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Json" });

        // Add MeshUtilities and MeshBuilder only for editor targets
        if (Target.Type == TargetType.Editor)
        {
            PublicDependencyModuleNames.Add("MeshUtilities");
            PublicDependencyModuleNames.Add("MeshBuilder");
        }
    }
}
