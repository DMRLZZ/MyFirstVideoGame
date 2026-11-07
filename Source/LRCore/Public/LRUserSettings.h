// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "LRUserSettings.generated.h"

/** What the player chooses in the settings menu. Plain values, always within range after Sanitized(). */
struct LRCORE_API FLRUserSettings
{
	float MusicVolume = 0.6f;
	float EffectsVolume = 1.f;
	bool bMusicEnabled = true;
	bool bShuffleMusic = false;

	ELRControlScheme ControlScheme = ELRControlScheme::Smooth;
	float SteeringSensitivity = 1.f;

	/** 0 = low, 1 = medium, 2 = high, 3 = epic. */
	int32 GraphicsQuality = 2;
	/** Frames per second, 0 = no limit. */
	int32 FrameRateLimit = 60;
	bool bFullscreen = true;
	bool bMotionBlur = true;

	/** Copy with every value clamped to its valid range. */
	FLRUserSettings Sanitized() const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FLROnUserSettingsChanged, const FLRUserSettings&);

/**
 * Keeps the settings of the player: reads them on startup, saves them on every change and applies the graphics ones.
 * Stored in GameUserSettings.ini, section [LeftRight.UserSettings]. Other systems read Get() or listen to OnChanged.
 */
UCLASS()
class LRCORE_API ULRUserSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Subsystem of the game instance of this context, or null. */
	static ULRUserSettingsSubsystem* Get(const UObject* WorldContext);

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const FLRUserSettings& GetSettings() const { return Settings; }

	/** Stores new values, saves them and tells every listener. Graphics are applied only when they changed. */
	void SetSettings(const FLRUserSettings& NewSettings);

	/** Fired after any change. */
	FLROnUserSettingsChanged OnChanged;

private:
	void Load();
	void Save() const;

	/** Sends quality, frame limit, window mode and motion blur to the engine. */
	void ApplyGraphics() const;

	FLRUserSettings Settings;
};
