// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Engine audio, wind, pass-by whooshes, mix.</summary>
public class LRAudio : ModuleRules
{
	public LRAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore", "LRVehicle" });
		PrivateDependencyModuleNames.AddRange(new string[] { "AudioMixer" });
	}
}
