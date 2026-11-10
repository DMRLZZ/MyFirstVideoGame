// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRPlaylist.h"
#include "LRUserSettings.h"
#include "Subsystems/WorldSubsystem.h"

#include "LRMusicSubsystem.generated.h"

class UMediaPlayer;
class UMediaSoundComponent;

/**
 * Plays the music of the game: the tracks in Data/Music plus whatever the player drops in Documents/LeftRight/Music.
 *
 * Uses the Media Framework, which decodes files from disk at runtime, so the player's music needs no import step.
 * It keeps playing while the game is paused. Volume and shuffle come from ULRUserSettingsSubsystem.
 */
UCLASS()
class LRAUDIO_API ULRMusicSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Subsystem of this world, or null. */
	static ULRMusicSubsystem* Get(const UObject* WorldContext);

	//~ UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	void NextTrack();
	void PreviousTrack();

	/** Looks for new files in the music folders. Keeps playing if the current track is still there. */
	void RescanTracks();

	/** Signal level leaving the music player, 0 to 1. Above zero means that sound really is coming out. */
	float GetOutputLevel() const;

	/** Name of the track that is playing, empty when the music is off. */
	FString GetCurrentTrackName() const;

	int32 GetNumTracks() const { return Playlist.Num(); }

	/** Real time of the last track change, seconds. Lets the HUD show the name for a moment. Negative = never. */
	double GetTrackChangedSeconds() const { return TrackChangedSeconds; }

	/** Creates the folder for the player's music if needed, and opens it in the file explorer. */
	void OpenUserMusicFolder() const;

	/** Documents/LeftRight/Music. */
	static FString GetUserMusicFolder();

private:
	/** Starts the current track of the playlist, or stops if the music is off or there is nothing to play. */
	void PlayCurrent();

	/** Opens a file the media player can read. */
	void OpenPlayable(const FString& Path);

	/** Converts a track in the background. Only one runs at a time. */
	void StartConversion(const FString& Track, const FString& CachePath);

	/** Plays the converted track if the player is still on it, or converts the one they moved on to. */
	void HandleConversionFinished(const FString& Track, const FString& CachePath, bool bConverted, const FString& Error);

	void HandleSettingsChanged(const FLRUserSettings& Settings);

	UFUNCTION()
	void HandleEndReached();

	UFUNCTION()
	void HandleOpenFailed(FString FailedUrl);

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> Player;

	UPROPERTY(Transient)
	TObjectPtr<UMediaSoundComponent> Sound;

	FLRPlaylist Playlist;
	FDelegateHandle SettingsHandle;
	FLRUserSettings AppliedSettings;
	double TrackChangedSeconds = -1.0;

	/** Files that failed in a row: stops looking for a playable one after a full turn of the list. */
	int32 ConsecutiveFailures = 0;

	/** Track being converted in the background, empty when none is. */
	FString ConvertingTrack;
};
