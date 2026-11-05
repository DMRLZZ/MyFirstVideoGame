// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// Shared foundation: common types, the gameplay event bus, run context,
/// deterministic seeding, unit helpers and the runtime JSON data loader.
/// Must not depend on any other LR* module.
/// </summary>
public class LRCore : ModuleRules
{
	public LRCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayTags", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "JsonUtilities" });
	}
}
