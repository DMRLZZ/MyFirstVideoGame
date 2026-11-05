// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Player vehicle: Chaos wheeled physics, driving assists, vehicle definitions.</summary>
public class LRVehicle : ModuleRules
{
	public LRVehicle(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore", "LRWorld", "ChaosVehicles" });
		PrivateDependencyModuleNames.AddRange(new string[] { "PhysicsCore" });
	}
}
