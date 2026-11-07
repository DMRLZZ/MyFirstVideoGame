// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"

#include "LRAudioSubsystem.generated.h"

class ULRVehicleAudioComponent;
struct FLRCameraModeChangedEvent;
struct FLRCrashEvent;
struct FLRNearMissEvent;
struct FLRRunResetEvent;

/**
 * Connects gameplay events to the player's car sound (docs/02-ARCHITECTURE.md §5.6).
 *
 * Listens to OnNearMiss, OnCrash, OnCameraModeChanged and OnRunReset on ULRGameEventSubsystem and forwards them to the
 * ULRVehicleAudioComponent of the player pawn (ULRRunContextSubsystem). If the pawn is an ALRVehicle without that
 * component, it adds one, so sound works without any setup. It re-checks the pawn a few times per second because the
 * run context has no "player changed" event.
 *
 * Exists only in Game / PIE worlds. Budget: negligible (no work per frame; only events and the slow pawn check).
 */
UCLASS()
class LRAUDIO_API ULRAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Returns the audio subsystem of WorldContext's world, or nullptr (no world / unsupported world type). */
	static ULRAudioSubsystem* Get(const UObject* WorldContext);

	/** Audio component of the current player car, or nullptr while there is none. */
	ULRVehicleAudioComponent* GetPlayerAudioComponent() const { return PlayerAudio.Get(); }

	//~ UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Bus callback: a car was passed closely. */
	void HandleNearMiss(const FLRNearMissEvent& Event);

	/** Sends the volume chosen by the player to the sound of the car. */
	void ApplyEffectsVolume() const;

	FDelegateHandle SettingsHandle;

	/** Bus callback: the player hit something. */
	void HandleCrash(const FLRCrashEvent& Event);

	/** Bus callback: the camera changed. */
	void HandleCameraModeChanged(const FLRCameraModeChangedEvent& Event);

	/** Bus callback: a new run starts. */
	void HandleRunReset(const FLRRunResetEvent& Event);

	/** Finds the player car and moves the sound onto it when it changed. */
	void RefreshPlayerAudio();

	/** Side of the passed vehicle as a stereo position, -1..1 (0 when it cannot be found). */
	float FindWhooshPan(const FLRNearMissEvent& Event, float ProbeRadiusMeters) const;

	/** Audio component of the player car (owned by the car). */
	TWeakObjectPtr<ULRVehicleAudioComponent> PlayerAudio;

	/** Last camera state, applied to a component created later. */
	bool bInteriorCamera = false;

	FTimerHandle RefreshTimer;

	FDelegateHandle NearMissHandle;
	FDelegateHandle CrashHandle;
	FDelegateHandle CameraModeHandle;
	FDelegateHandle RunResetHandle;
};
