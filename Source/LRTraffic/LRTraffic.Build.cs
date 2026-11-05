// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Traffic simulation (IDM + MOBIL), spawning, pooling, near-miss detection.</summary>
public class LRTraffic : ModuleRules
{
	public LRTraffic(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore", "LRWorld" });
	}
}
