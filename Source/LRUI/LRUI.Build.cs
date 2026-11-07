// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>HUD and menus.</summary>
public class LRUI : ModuleRules
{
	public LRUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// SlateCore is public because LRHUD.h stores FSlateFontInfo.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "SlateCore", "LRCore", "LRVehicle" });

		// Slate + InputCore + LRInput: input pre-processor that detects the last device used (FLRInputDeviceTracker).
		// LRAudio: the pause menu and the HUD show and control the music.
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "InputCore", "LRInput", "LRAudio" });
	}
}
