// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>HUD and menus.</summary>
public class LRUI : ModuleRules
{
	public LRUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore", "LRVehicle" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
	}
}
