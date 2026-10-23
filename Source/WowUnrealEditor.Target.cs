using UnrealBuildTool;

public class WowUnrealEditorTarget : TargetRules
{
    public WowUnrealEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.AddRange(new string[] { "WowUnreal", "WowData", "WowAssets", "WowWorld", "WowUI", "WowNetwork", "WowClient" });
    }
}
