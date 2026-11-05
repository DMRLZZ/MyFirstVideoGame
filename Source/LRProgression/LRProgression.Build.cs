// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Save game, economy, garage, unlocks.</summary>
public class LRProgression : ModuleRules
{
	public LRProgression(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore" });
	}
}
