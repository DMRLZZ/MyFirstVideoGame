// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Game modes, run state machine, scoring, player controller.</summary>
public class LRGameplay : ModuleRules
{
	public LRGameplay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LRCore", "LRWorld", "LRTraffic", "LRVehicle", "LRInput", "LRCamera", "LRUI", "LRAudio", "LRReplay", "LRProgression" });
		PrivateDependencyModuleNames.AddRange(new string[] { "EnhancedInput", "RHI", "RenderCore", "Slate", "SlateCore" });
	}
}
