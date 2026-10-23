using UnrealBuildTool;

public class WowUnreal : ModuleRules
{
    public WowUnreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "RHI", "RenderCore", "WowData", "WowAssets", "WowWorld", "WowNetwork", "WowUI", "WowClient", "MediaAssets", "Media", "ProceduralMeshComponent" });

        PrivateIncludePaths.Add(ModuleDirectory); // r312: UI/ files include root headers (WowSatelliteTiles.h)
        PrivateDependencyModuleNames.Add("Json");
        PrivateDependencyModuleNames.Add("HTTP"); // r233: the server alert (news) from http://<patchlist>/alert
        PrivateDependencyModuleNames.Add("ImageCore"); // r295 Satellite v2 tiles (FImage)
        PrivateDependencyModuleNames.Add("HeadMountedDisplay"); // seated VR preview (FWowVRSupport)

        if (Target.Platform == UnrealTargetPlatform.Win64) PrivateDependencyModuleNames.Add("FFXFrameInterpolation");

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
    }
}
