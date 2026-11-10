// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Converts the music files of the player that the media player cannot open directly, such as MP3 and FLAC.
 *
 * The Windows media plugin rejects those extensions, even though Windows itself decodes them. So they are decoded with
 * Media Foundation into a WAV in Saved/MusicCache, once per file, which the media player can open.
 */
namespace LRAudioFileDecoder
{
	/** True for formats that have to go through DecodeToWav before they can be played. */
	LRAUDIO_API bool NeedsConversion(const FString& Path);

	/** Where the converted copy of a file lives. Changes when the source file changes. */
	LRAUDIO_API FString GetCachePath(const FString& Source);

	/** Decodes Source into a 16-bit WAV at Destination. Blocking: call it from a worker thread. */
	LRAUDIO_API bool DecodeToWav(const FString& Source, const FString& Destination, FString& OutError);

	/** Folder of the converted copies, Saved/MusicCache. */
	LRAUDIO_API FString GetCacheFolder();

	/** One converted file of the cache. */
	struct FCacheEntry
	{
		FString Path;
		int64 SizeBytes = 0;
		/** Last time it was converted or played. */
		FDateTime LastUsed;
	};

	/** Files to delete, least recently used first, so that the rest fits in MaxBytes. Never includes KeepPath. */
	LRAUDIO_API TArray<FString> ChooseCacheFilesToDelete(TArray<FCacheEntry> Entries, int64 MaxBytes, const FString& KeepPath);

	/**
	 * Keeps the cache under MaxBytes by deleting the copies that have gone longest without being played, and removes
	 * what an interrupted conversion left behind. Blocking, and only safe while no conversion is running.
	 */
	LRAUDIO_API void PruneCache(int64 MaxBytes, const FString& KeepPath);
}
