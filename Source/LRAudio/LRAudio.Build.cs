// Copyright LEFTRIGHT. All Rights Reserved.

using UnrealBuildTool;

/// <summary>Procedural engine audio, wind, tyres, pass-by whooshes, crash burst, interior mix.</summary>
public class LRAudio : ModuleRules
{
	public LRAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// AudioMixer, AudioMixerCore and AudioExtensions are public: LRVehicleAudioComponent.h derives from USynthComponent.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AudioMixer", "AudioMixerCore", "AudioExtensions", "LRCore", "LRVehicle" });

		// Read-only: the near-miss whoosh looks up which side the passed vehicle is on (GetSnapshot).
		// MediaAssets: the music is decoded from files on disk, so the player can add their own without any import.
		PrivateDependencyModuleNames.AddRange(new string[] { "LRTraffic", "MediaAssets" });

		// Media Foundation decodes the MP3 and FLAC files of the player (LRAudioFileDecoder).
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.AddRange(new string[] { "mfplat.lib", "mfreadwrite.lib", "mfuuid.lib" });
		}
	}
}
