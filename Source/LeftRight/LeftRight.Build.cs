// Copyright LEFTRIGHT. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

/// <summary>
/// Primary game module. Wires every runtime module together and stages the
/// text data (Data/**/*.json) into the packaged game.
/// </summary>
public class LeftRight : ModuleRules
{
	public LeftRight(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"LRCore",
			"LRWorld",
			"LRTraffic",
			"LRVehicle",
			"LRInput",
			"LRCamera",
			"LRUI",
			"LRAudio",
			"LRReplay",
			"LRProgression",
			"LRGameplay"
		});

		// Design data lives as JSON at <Project>/Data and is loaded at runtime by ULRDataSubsystem.
		RuntimeDependencies.Add(Path.Combine("$(ProjectDir)", "Data", "..."), StagedFileType.UFS);
	}
}
