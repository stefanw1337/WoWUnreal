using UnrealBuildTool;

public class WowUnrealTarget : TargetRules
{
    public WowUnrealTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.AddRange(new string[] { "WowUnreal", "WowData", "WowAssets", "WowWorld", "WowUI", "WowNetwork", "WowClient" });
    }
}
