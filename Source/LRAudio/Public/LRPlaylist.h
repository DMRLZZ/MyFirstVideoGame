// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * List of music files and which one is playing. Pure logic, no playback.
 * Shuffle plays every track once before repeating any of them.
 */
class LRAUDIO_API FLRPlaylist
{
public:
	/** True for the audio formats Windows can decode for the media player. */
	static bool IsSupportedFile(const FString& Path);

	/** Paths of the supported files of a folder (not recursive), sorted by name. Empty if the folder does not exist. */
	static TArray<FString> FindTracks(const FString& Folder);

	/** Replaces the list and goes back to its first track. The player's own music goes before the built-in tracks. */
	void SetTracks(const TArray<FString>& UserTracks, const TArray<FString>& BuiltInTracks);

	/** Turns shuffle on or off, keeping the current track. The seed makes the order reproducible. */
	void SetShuffle(bool bEnabled, int32 Seed);

	int32 Num() const { return Tracks.Num(); }

	/** Path of the current track, empty when there are none. */
	FString GetCurrent() const;

	/** Current track as the player reads it: the file name without folder or extension. */
	FString GetCurrentName() const;

	/** Moves to the next or previous track, wrapping around at the ends. */
	void Next();
	void Previous();

private:
	void BuildOrder(int32 Seed);

	TArray<FString> Tracks;
	/** Positions in Tracks, in playing order. */
	TArray<int32> Order;
	int32 Position = 0;
};
