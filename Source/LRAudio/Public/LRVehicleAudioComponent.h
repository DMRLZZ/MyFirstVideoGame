// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "Components/SynthComponent.h"
#include "CoreMinimal.h"
#include "LRAudioTypes.h"

#include "LRVehicleAudioComponent.generated.h"

class ALRVehicle;
class FLRVehicleSynthBridge;

/**
 * Procedural sound of the player car: engine, wind, tyres, expansion joints, near-miss whooshes and crash burst.
 *
 * Game-thread side of the synth: every tick it reads rpm, throttle, speed and the road coordinate from the bound
 * ALRVehicle and publishes them through FLRVehicleSynthBridge. The samples are rendered by a private ISoundGenerator
 * on the audio render thread, which never touches this UObject (the generator shares ownership of the bridge).
 *
 * Plays as a 2D stereo sound (the listener is the driver). Owned by the vehicle actor. ULRAudioSubsystem adds it to
 * the player's pawn automatically and forwards the gameplay events to it.
 */
UCLASS(ClassGroup = (LeftRight), meta = (BlueprintSpawnableComponent))
class LRAUDIO_API ULRVehicleAudioComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	ULRVehicleAudioComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * Returns the audio component of InVehicle, creating and registering one if needed, already bound and playing.
	 * @return nullptr only when InVehicle is null.
	 */
	static ULRVehicleAudioComponent* FindOrAddTo(ALRVehicle* InVehicle);

	/**
	 * Starts following a vehicle: loads Data/Audio/<Definition.AudioProfile>.json (fallback EngineAudio_Default.json),
	 * and starts generating audio. Binding the vehicle that is already bound does nothing. Passing nullptr stops the
	 * sound and releases the vehicle. The profile is reloaded by itself if the vehicle later gets another definition.
	 */
	void BindToVehicle(ALRVehicle* InVehicle);

	/** Vehicle being followed, or nullptr. */
	ALRVehicle* GetBoundVehicle() const { return Vehicle.Get(); }

	/**
	 * Plays a near-miss whoosh.
	 * @param RelativeSpeedKmh  Absolute relative speed of the pass, km/h (faster = louder).
	 * @param ClearanceMeters   Lateral clearance of the pass, meters (closer = louder).
	 * @param Pan               Side of the passed vehicle, -1 (left) .. +1 (right).
	 */
	void TriggerNearMissWhoosh(float RelativeSpeedKmh, float ClearanceMeters, float Pan);

	/**
	 * Plays the crash burst, scaled by the impact.
	 * @param ImpactSpeedKmh  Speed of the impact, km/h.
	 * @param bCutEngine      Also fades the engine out, until ResetForNewRun.
	 */
	void TriggerCrash(float ImpactSpeedKmh, bool bCutEngine);

	/** Selects the cabin mix (low-passed engine, lower wind, more structure-borne rumble) or the exterior mix. */
	void SetInteriorMix(bool bInInterior);

	/** New run: silences the one-shots, restarts the engine and forgets the road position. */
	void ResetForNewRun();

	/** Tuning in use (game-thread copy). */
	const FLREngineAudioProfile& GetProfile() const { return Profile; }

	/** True after a crash cut the engine and until ResetForNewRun. */
	bool IsEngineCut() const { return bEngineCut; }

	//~ UActorComponent
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	//~ USynthComponent
	virtual bool Init(int32& SampleRate) override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

	//~ UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Loads the profile named by the bound vehicle's definition and sends it to the render thread. */
	void LoadProfileFromVehicle(const ALRVehicle& InVehicle);

	/** Publishes the current vehicle state for the render thread. */
	void PublishVehicleState(const ALRVehicle& InVehicle);

	/** Data subsystem callback (lr.Data.Reload): re-reads the profile. */
	void HandleDataReloaded();

	/** Removes the data reload binding. */
	void UnbindDataReload();

	/** Vehicle being followed (owned by the world). */
	TWeakObjectPtr<ALRVehicle> Vehicle;

	/** Shared with the sound generator, which may outlive this component for a few buffers. Created in the constructor. */
	TSharedPtr<FLRVehicleSynthBridge, ESPMode::ThreadSafe> Bridge;

	/** Tuning in use, game-thread copy (the render thread has its own). */
	FLREngineAudioProfile Profile;

	/** Definition.AudioProfile that Profile was loaded for. */
	FName LoadedProfileName;

	/** True once a profile was loaded for the bound vehicle. */
	bool bProfileLoaded = false;

	/** Camera inside the cabin. */
	bool bInterior = false;

	/** A crash cut the engine. */
	bool bEngineCut = false;

	FDelegateHandle DataReloadedHandle;
};
