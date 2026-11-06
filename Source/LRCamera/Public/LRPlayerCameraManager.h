// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "LRCameraMath.h"
#include "LRCameraSpring.h"
#include "LRCameraTypes.h"
#include "LRCockpitHeadModel.h"
#include "LRCoreTypes.h"

#include "LRPlayerCameraManager.generated.h"

class ALRVehicle;
class ULRRoadSubsystem;
struct FLRRunResetEvent;

/**
 * Player camera of LEFTRIGHT (set as PlayerCameraManagerClass by ALRPlayerController in LRGameplay).
 *
 * When the view target is an ALRVehicle it computes the view itself (falls back to APlayerCameraManager otherwise):
 * - ChaseFar / ChaseNear: arm behind the car with a critically damped yaw lag, distance / height from the vehicle
 *   definition (FLRCameraAnchors) stretched by speed and acceleration, look-at point ahead of the car, look-ahead yaw
 *   toward the target lane, FOV that widens with speed, procedural shake and a ground clearance guard.
 * - Hood / Bumper: rigid on the body with a high-frequency shake.
 * - Cockpit: driver eye anchor + FLRCockpitHeadModel (G forces), partial horizon stabilization, look-ahead,
 *   FOV 60-95 from data (SetCockpitFov).
 * - Viral: GoPro / Dashcam / Phone presets (mount, FOV, shake, post-process artifacts that can be disabled).
 * - LookBack rotates the view 180 degrees (chase: the arm swings in front of the car looking back).
 *
 * Config: Data/Camera/Cameras.json (type "CameraConfig"), re-read on lr.Data.Reload.
 * Events: broadcasts FLRCameraModeChangedEvent on every mode / interior change and once at start; resets all
 * smoothing when ULRGameEventSubsystem::OnRunReset fires.
 * Console: lr.Camera.ViralPreset <GoPro|Dashcam|Phone>, lr.Camera.ViralArtifacts <0|1>, lr.Camera.Cycle.
 */
UCLASS()
class LRCAMERA_API ALRPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	ALRPlayerCameraManager(const FObjectInitializer& ObjectInitializer);

	// --- Modes ---------------------------------------------------------------------------------

	/** Switches the camera mode (snaps the smoothing) and broadcasts FLRCameraModeChangedEvent if it changed. */
	void SetCameraMode(ELRCameraMode Mode);

	/** Next mode: ChaseFar -> ChaseNear -> Hood -> Cockpit -> Bumper -> Viral -> ChaseFar. */
	void CycleCameraMode();

	/** Active camera mode. */
	ELRCameraMode GetCameraMode() const { return CameraMode; }

	/** True while the active camera is inside the cabin (Cockpit or an interior Viral preset). */
	bool IsInteriorCamera() const;

	/** Look back (held button): rotates the view 180 degrees while true. */
	void SetLookBack(bool bInLookBack);

	/** True while looking back. */
	bool IsLookingBack() const { return bLookBack; }

	// --- Viral camera --------------------------------------------------------------------------

	/** Selects the Viral preset (broadcasts the mode event when the interior flag changes in Viral mode). */
	void SetViralPreset(ELRViralPreset Preset);

	/** Active Viral preset. */
	ELRViralPreset GetViralPreset() const { return ViralPreset; }

	/** Accessibility: enables or disables the Viral post-process artifacts (grain, fringe, vignette, blur...). */
	void SetViralArtifactsEnabled(bool bEnabled);

	/** True when the Viral post-process artifacts are applied. */
	bool AreViralArtifactsEnabled() const { return bViralArtifactsEnabled; }

	// --- Settings ------------------------------------------------------------------------------

	/** Cockpit base FOV (user setting), degrees; clamped to the data range (default 60..95). */
	void SetCockpitFov(float FovDeg);

	/** Cockpit base FOV, degrees. */
	float GetCockpitFov() const { return Config.Cockpit.FovDeg; }

	/** Accessibility multiplier of every procedural shake (0..3, 0 = none). */
	void SetShakeScale(float Scale);

	/** Accessibility multiplier of the cockpit head motion (0..3, 0 = rigid). */
	void SetHeadMotionIntensity(float Intensity);

	// --- Config --------------------------------------------------------------------------------

	/** Re-reads Data/Camera/Cameras.json (keeps the current config on failure) and re-applies the user settings. */
	void ReloadConfig();

	/** Applies a config (sanitized copy). Resets the smoothing. */
	void ApplyConfig(const FLRCameraConfig& InConfig);

	/** Active config. */
	const FLRCameraConfig& GetConfig() const { return Config; }

	/** Snaps every spring and the head model on the next update (run reset, teleports). */
	void ResetSmoothing();

protected:
	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//~ APlayerCameraManager
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

private:
	/** Computes the POV for a vehicle view target. */
	void UpdateVehicleView(const ALRVehicle& Vehicle, FMinimalViewInfo& OutPOV, float DeltaTime);

	/** Chase cameras. */
	void UpdateChaseView(const ALRVehicle& Vehicle, const FLRChaseCameraParams& Params, bool bNear, FMinimalViewInfo& OutPOV, float DeltaTime);

	/** Hood and bumper. */
	void UpdateMountedView(const ALRVehicle& Vehicle, const FLRMountedCameraParams& Params, ELRCameraMode Mode, FMinimalViewInfo& OutPOV);

	/** Driver eye camera with the head model. */
	void UpdateCockpitView(const ALRVehicle& Vehicle, FMinimalViewInfo& OutPOV, float DeltaTime);

	/** Viral presets. */
	void UpdateViralView(const ALRVehicle& Vehicle, FMinimalViewInfo& OutPOV);

	/** Smoothed look-ahead yaw toward the target lane and the curve, degrees. */
	float UpdateLookAhead(const ALRVehicle& Vehicle, const FLRLookAheadParams& Params, float DeltaTime);

	/** Shake sample for a profile at the current speed and road roughness, in camera space. */
	FLRCameraShakeSample SampleShake(const FLRCameraShakeParams& Params) const;

	/** Applies a camera-space shake sample to a view. */
	static void ApplyShake(const FLRCameraShakeSample& Shake, FMinimalViewInfo& InOutPOV);

	/** Target road roughness under the car (lane or shoulder), before smoothing. */
	float ComputeRoadRoughness(const ALRVehicle& Vehicle) const;

	/** Height of the road surface under a world location, cm. Returns false without a road. */
	bool GetRoadSurfaceZ(const FVector& WorldLocationCm, double& OutZ) const;

	/** Broadcasts FLRCameraModeChangedEvent with the current mode and interior flag. */
	void BroadcastModeChanged();

	/** Re-applies the user settings (cockpit FOV, shake scale, head intensity, artifacts) on top of a fresh config. */
	void ApplyUserSettings();

	/** Bound to ULRGameEventSubsystem::OnRunReset. */
	void HandleRunReset(const FLRRunResetEvent& Event);

	/** Bound to ULRDataSubsystem::OnDataReloaded. */
	void HandleDataReloaded();

	/** Active configuration (sanitized). */
	FLRCameraConfig Config;

	ELRCameraMode CameraMode = ELRCameraMode::ChaseFar;
	ELRViralPreset ViralPreset = ELRViralPreset::GoPro;
	bool bViralArtifactsEnabled = true;
	bool bLookBack = false;

	/** SetCameraMode / SetViralPreset ran before BeginPlay: the data defaults must not overwrite them. */
	bool bHasExplicitMode = false;
	bool bHasExplicitPreset = false;

	/** User overrides, re-applied after a config reload (negative = use the data value). */
	float UserCockpitFovDeg = -1.f;
	float UserShakeScale = -1.f;
	float UserHeadIntensity = -1.f;
	int32 UserArtifactsEnabled = -1;

	/** Interior flag of the last broadcast, to re-broadcast when only the preset changes it. */
	bool bLastBroadcastInterior = false;

	/** Broadcast the initial mode on the first camera update (after every BeginPlay bound its listeners). */
	bool bPendingInitialBroadcast = true;

	/** Snap every smoother on the next vehicle update. */
	bool bSnapNextUpdate = true;

	/** Frame of the last state advance (the view target can be evaluated twice per frame during blends). */
	uint64 LastAdvanceFrame = MAX_uint64;

	/** Vehicle the smoothing state belongs to (snap when it changes). */
	TWeakObjectPtr<const ALRVehicle> LastVehicle;

	/** Vehicle location at the last update, centimeters (teleport detection). */
	FVector LastVehicleLocationCm = FVector::ZeroVector;

	/** Shake clock, seconds. */
	double ShakeTimeSeconds = 0.0;

	/** Speed and roughness of the frame, cached for SampleShake. */
	float FrameSpeedKmh = 0.f;
	float FrameRoughness = 1.f;

	/** Road segment hint for camera-location road queries. */
	mutable int32 CameraRoadHint = INDEX_NONE;

	// Smoothing state.
	FLRCriticallyDampedSpring ChaseYawSpring;
	FLRCriticallyDampedSpring ChaseDistanceSpring;
	FLRCriticallyDampedSpringDouble ChasePivotZSpring;
	FLRCriticallyDampedSpring LookAheadSpring;
	FLRCriticallyDampedSpring RoughnessSpring;
	FLRCockpitHeadModel HeadModel;

	FDelegateHandle RunResetHandle;
	FDelegateHandle DataReloadedHandle;
};
