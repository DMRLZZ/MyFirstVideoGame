// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Editor target: game modules plus editor-only tooling (LREditor).</summary>
public class LeftRightEditorTarget : TargetRules
{
	public LeftRightEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "LeftRight", "LREditor" });
	}
}
