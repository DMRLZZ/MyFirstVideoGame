// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Enhanced Input setup for keyboard and gamepad, built at runtime from code.</summary>
public class LRInput : ModuleRules
{
	public LRInput(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore", "EnhancedInput", "InputCore" });
	}
}
