// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Camera modes: chase, hood, bumper, cockpit, viral camera.</summary>
public class LRCamera : ModuleRules
{
	public LRCamera(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore", "LRVehicle" });
		PrivateDependencyModuleNames.AddRange(new string[] { "LRWorld" });
	}
}
