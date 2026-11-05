// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Highway model (road coordinates s/d), segment streaming, environment and weather.</summary>
public class LRWorld : ModuleRules
{
	public LRWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "ProceduralMeshComponent" });
	}
}
