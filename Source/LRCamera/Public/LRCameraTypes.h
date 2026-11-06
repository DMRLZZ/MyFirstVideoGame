// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"

#include "LRCameraTypes.generated.h"

/**
 * Data model of the player cameras (Data/Camera/Cameras.json, envelope type "CameraConfig").
 *
 * Units: distances in centimeters (they are applied directly in world space at the camera boundary), angles in degrees,
 * speeds in km/h (designer friendly), times in seconds. Every field has a sensible default so a partial JSON file
 * still produces usable cameras. Owned by value by ALRPlayerCameraManager (GetConfig / ApplyConfig).
 */

/** Presets of the Viral camera mode (docs/03-RENDERING.md §6, layer 2). */
UENUM(BlueprintType)
enum class ELRViralPreset : uint8
{
	/** Action camera on a suction mount: very wide FOV, mount vibration, crushed highlights. */
	GoPro,
	/** Dashcam behind the rear-view mirror: poor dynamic range, glare, interior. */
	Dashcam,
	/** Phone held by the passenger: low-frequency hand motion, slow auto exposure. */
	Phone
};

/** Vehicle camera anchor a mounted camera starts from (see FLRCameraAnchors in LRVehicleTypes.h). */
UENUM(BlueprintType)
enum class ELRCameraMountAnchor : uint8
{
	/** FLRCameraAnchors::HoodCm. */
	Hood,
	/** FLRCameraAnchors::BumperCm. */
	Bumper,
	/** FLRCameraAnchors::CockpitEyeCm (driver eyes). */
	Cockpit,
	/** FLRCameraAnchors::ViralMountCm (behind the rear-view mirror). */
	ViralMount
};

/**
 * FOV that widens with speed: Fov = MinFovDeg + (MaxFovDeg - MinFovDeg) * Alpha^Exponent,
 * Alpha = clamp((Speed - StartSpeedKmh) / (FullSpeedKmh - StartSpeedKmh), 0, 1). Monotonic non-decreasing in speed.
 */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRSpeedFovCurve
{
	GENERATED_BODY()

	/** Horizontal FOV at or below StartSpeedKmh, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fov", meta = (ClampMin = "20.0", ClampMax = "170.0"))
	float MinFovDeg = 72.f;

	/** Horizontal FOV at or above FullSpeedKmh, degrees (values below MinFovDeg are raised to MinFovDeg). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fov", meta = (ClampMin = "20.0", ClampMax = "170.0"))
	float MaxFovDeg = 86.f;

	/** Speed where the widening starts, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fov", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float StartSpeedKmh = 40.f;

	/** Speed where MaxFovDeg is reached, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fov", meta = (ClampMin = "1.0", ClampMax = "600.0"))
	float FullSpeedKmh = 320.f;

	/** Shape of the curve (1 = linear, > 1 = the widening concentrates at high speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fov", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float Exponent = 1.3f;
};

/**
 * Deterministic procedural shake profile (Perlin noise of time, no randomness). Two bands:
 * a high-frequency band (road texture, engine) and a low-frequency band (suspension, hand motion).
 * The amplitudes apply at ReferenceSpeedKmh on a road of roughness 1 and scale with
 * IdleScale + (Speed / ReferenceSpeedKmh)^SpeedExponent, clamped to MaxScale.
 */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRCameraShakeParams
{
	GENERATED_BODY()

	/** Frequency of the high band, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float HighFrequencyHz = 17.f;

	/** Location amplitude of the high band, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float HighLocationAmplitudeCm = 0.25f;

	/** Rotation amplitude of the high band, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float HighRotationAmplitudeDeg = 0.06f;

	/** Frequency of the low band, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float LowFrequencyHz = 1.3f;

	/** Location amplitude of the low band, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float LowLocationAmplitudeCm = 0.6f;

	/** Rotation amplitude of the low band, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float LowRotationAmplitudeDeg = 0.12f;

	/** Speed at which the amplitudes apply unscaled, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "1.0", ClampMax = "600.0"))
	float ReferenceSpeedKmh = 200.f;

	/** Exponent of the speed scaling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SpeedExponent = 1.2f;

	/** Scale at standstill (engine idle vibration). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float IdleScale = 0.05f;

	/** Upper clamp of the speed scale (before roughness and the accessibility scale). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shake", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float MaxScale = 2.f;
};

/**
 * Look-ahead toward the target lane and into the curve: the view yaws toward the point the car will reach in
 * LookAheadSeconds (at least MinDistanceMeters ahead), clamped to MaxYawDeg and smoothed with LagSeconds.
 */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRLookAheadParams
{
	GENERATED_BODY()

	/** Time horizon of the look-ahead point, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LookAhead", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float LookAheadSeconds = 0.6f;

	/** Minimum distance of the look-ahead point, meters (avoids huge angles at low speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LookAhead", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float MinDistanceMeters = 25.f;

	/** Maximum look-ahead yaw, degrees (0 disables the look-ahead). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LookAhead", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float MaxYawDeg = 5.f;

	/** Smoothing time of the look-ahead yaw (critically damped), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LookAhead", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float LagSeconds = 0.3f;

	/** Weight of the road curvature term (0 = target lane only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LookAhead", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float CurvatureScale = 1.f;
};

/**
 * Spring-damper model of the driver's head in the cockpit, driven by the smoothed local acceleration of the car.
 * The head is thrown opposite to the acceleration: offset = -G * CmPerG (per axis), roll = -LateralG * RollDegPerG,
 * pitch = LongitudinalG * PitchDegPerG. All outputs are multiplied by Intensity and clamped.
 */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRHeadModelParams
{
	GENERATED_BODY()

	/** Natural frequency of the neck spring, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.2", ClampMax = "10.0"))
	float NaturalFrequencyHz = 1.8f;

	/** Damping ratio (1 = critical, < 1 = slight overshoot). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.3", ClampMax = "3.0"))
	float DampingRatio = 0.75f;

	/** Head travel per G of longitudinal acceleration, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float LongitudinalCmPerG = 5.f;

	/** Head travel per G of lateral acceleration, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float LateralCmPerG = 4.f;

	/** Head travel per G of vertical acceleration, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float VerticalCmPerG = 1.5f;

	/** Head roll per G of lateral acceleration (leaning out of the turn), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float RollDegPerG = 2.5f;

	/** Head pitch per G of longitudinal acceleration (up when accelerating, down when braking), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float PitchDegPerG = 1.5f;

	/** Clamp of the head offset on each axis, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "40.0"))
	float MaxOffsetCm = 8.f;

	/** Clamp of the head roll, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float MaxRollDeg = 4.f;

	/** Clamp of the head pitch, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float MaxPitchDeg = 3.f;

	/** Input acceleration clamp, G (crashes must not throw the head away). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float MaxInputG = 3.f;

	/** Global intensity of the head motion (0 = rigid camera, accessibility). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Head", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float Intensity = 1.f;
};

/** Far / near chase camera. Distance and height come from the vehicle definition (FLRCameraAnchors). */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRChaseCameraParams
{
	GENERATED_BODY()

	/** Multiplier of the vehicle's chase distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float DistanceScale = 1.f;

	/** Multiplier of the vehicle's chase height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float HeightScale = 1.f;

	/** Smoothing time of the arm yaw behind the car (critically damped), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float YawLagSeconds = 0.18f;

	/** Smoothing time of the pivot height (bumps, grades), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float HeightLagSeconds = 0.1f;

	/** Smoothing time of the arm length (acceleration and speed stretch), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float DistanceLagSeconds = 0.35f;

	/** Arm stretch per m/s^2 of longitudinal acceleration (positive = camera falls back when accelerating), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "-50.0", ClampMax = "50.0"))
	float AccelDistanceCmPerMs2 = 6.f;

	/** Clamp of the acceleration stretch, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float MaxAccelDistanceCm = 80.f;

	/** Extra arm length per 100 km/h, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "300.0"))
	float SpeedDistanceCmPer100Kmh = 15.f;

	/** The camera looks at a point this far ahead of the car origin, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float LookAtAheadCm = 1200.f;

	/** Multiplier of the vehicle's ChaseLookAtHeightCm used for the look-at point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float LookAtHeightScale = 1.f;

	/** Minimum height of the camera above the road surface, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float MinGroundClearanceCm = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FLRSpeedFovCurve Fov;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FLRCameraShakeParams Shake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FLRLookAheadParams LookAhead;
};

/** Rigid body-mounted camera (hood, bumper): follows the full body rotation plus a high-frequency shake. */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRMountedCameraParams
{
	GENERATED_BODY()

	/** Extra pitch of the mount, degrees (negative = looking down). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mounted", meta = (ClampMin = "-45.0", ClampMax = "45.0"))
	float PitchOffsetDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mounted")
	FLRSpeedFovCurve Fov;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mounted")
	FLRCameraShakeParams Shake;
};

/** Driver's eye camera: head model, look-ahead, partial horizon stabilization, FOV 60-95. */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRCockpitCameraParams
{
	GENERATED_BODY()

	/** Base horizontal FOV (user setting), degrees. Clamped to [MinFovDeg, MaxFovDeg]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit", meta = (ClampMin = "30.0", ClampMax = "120.0"))
	float FovDeg = 75.f;

	/** Lowest FOV the setting accepts, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit", meta = (ClampMin = "30.0", ClampMax = "120.0"))
	float MinFovDeg = 60.f;

	/** Highest FOV the setting accepts, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit", meta = (ClampMin = "30.0", ClampMax = "120.0"))
	float MaxFovDeg = 95.f;

	/** FOV added at high speed on top of FovDeg (the result stays within [MinFovDeg, MaxFovDeg]), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float SpeedFovExtraDeg = 4.f;

	/** Speed where the extra FOV starts, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float SpeedFovStartKmh = 60.f;

	/** Speed where the full extra FOV is reached, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit", meta = (ClampMin = "1.0", ClampMax = "600.0"))
	float SpeedFovFullKmh = 320.f;

	/** Fraction of the body roll and pitch removed from the view (1 = horizon locked, 0 = rigid). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HorizonStabilization = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit")
	FLRHeadModelParams HeadModel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit")
	FLRCameraShakeParams Shake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cockpit")
	FLRLookAheadParams LookAhead;
};

/**
 * Post-process overrides of a Viral preset, written into FMinimalViewInfo::PostProcessSettings (with the matching
 * bOverride_ flags) only while the Viral mode is active and the artifacts are enabled. Negative values leave the
 * engine / volume setting untouched.
 */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRViralPostProcessParams
{
	GENERATED_BODY()

	/** FPostProcessSettings::VignetteIntensity (0..1 typical). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float VignetteIntensity = 0.5f;

	/** FPostProcessSettings::FilmGrainIntensity (sensor noise, 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float FilmGrainIntensity = 0.2f;

	/** FPostProcessSettings::SceneFringeIntensity (chromatic aberration, 0..5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "5.0"))
	float SceneFringeIntensity = 1.5f;

	/** FPostProcessSettings::ChromaticAberrationStartOffset (0 = from the center, 1 = only the edges). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ChromaticAberrationStartOffset = 0.4f;

	/** FPostProcessSettings::AutoExposureSpeedUp (lower = slower adaptation to bright scenes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "20.0"))
	float AutoExposureSpeedUp = 0.6f;

	/** FPostProcessSettings::AutoExposureSpeedDown (lower = slower adaptation to dark scenes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "20.0"))
	float AutoExposureSpeedDown = 0.4f;

	/** FPostProcessSettings::MotionBlurAmount (slow shutter, 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MotionBlurAmount = 0.9f;

	/** FPostProcessSettings::MotionBlurMax (percent of the screen width). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "100.0"))
	float MotionBlurMax = 8.f;

	/** FPostProcessSettings::BloomIntensity (glare of the headlights). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "8.0"))
	float BloomIntensity = 1.2f;

	/** FPostProcessSettings::Sharpen (aggressive action-camera sharpening, 0..10). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "10.0"))
	float Sharpen = -1.f;

	/** Uniform FPostProcessSettings::ColorSaturation (1 = neutral). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "2.0"))
	float Saturation = -1.f;

	/** Uniform FPostProcessSettings::ColorContrast (1 = neutral, > 1 = narrow dynamic range). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (ClampMin = "-1.0", ClampMax = "2.0"))
	float Contrast = -1.f;
};

/** One preset of the Viral camera (GoPro, Dashcam or Phone). */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRViralPresetParams
{
	GENERATED_BODY()

	/** Name shown in menus and the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	FString DisplayName;

	/** Vehicle anchor the mount starts from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	ELRCameraMountAnchor MountAnchor = ELRCameraMountAnchor::ViralMount;

	/** Offset from the anchor, centimeters in actor space (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	FVector MountOffsetCm = FVector::ZeroVector;

	/** Extra pitch of the mount, degrees (negative = looking down). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral", meta = (ClampMin = "-45.0", ClampMax = "45.0"))
	float MountPitchDeg = 0.f;

	/** Horizontal FOV, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral", meta = (ClampMin = "30.0", ClampMax = "170.0"))
	float FovDeg = 110.f;

	/** The camera is inside the cabin (FLRCameraModeChangedEvent::bInterior, cabin audio mix). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	bool bInterior = false;

	/** Fraction of the body roll and pitch removed from the view (gimbal / hand stabilization), 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HorizonStabilization = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	FLRCameraShakeParams Shake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	FLRViralPostProcessParams PostProcess;
};

/** Viral camera mode (docs/03-RENDERING.md §6). */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRViralCameraParams
{
	GENERATED_BODY()

	/** Preset active when the game starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	ELRViralPreset DefaultPreset = ELRViralPreset::GoPro;

	/** Accessibility: false disables every artifact effect (post-process overrides: grain, fringe, blur, vignette...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	bool bViralArtifactsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	FLRViralPresetParams GoPro;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	FLRViralPresetParams Dashcam;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viral")
	FLRViralPresetParams Phone;

	/** Preset parameters by enum. */
	const FLRViralPresetParams& GetPreset(ELRViralPreset Preset) const;
};

/** Complete camera configuration, loaded from Data/Camera/Cameras.json (type "CameraConfig"). */
USTRUCT(BlueprintType)
struct LRCAMERA_API FLRCameraConfig
{
	GENERATED_BODY()

	/** Mode used when the camera manager starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	ELRCameraMode DefaultMode = ELRCameraMode::ChaseFar;

	/** Roughness of the lane surface (shake multiplier). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float RoadRoughness = 1.f;

	/** Roughness when the car is outside the lanes (shoulder, rumble strips). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float ShoulderRoughness = 2.5f;

	/** Smoothing time of the roughness when the car crosses from lane to shoulder (no shake pops), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RoughnessLagSeconds = 0.15f;

	/** Accessibility multiplier of every procedural shake (0 = no shake). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float GlobalShakeScale = 1.f;

	/** Near clip plane of the interior cameras (cockpit, interior viral presets), centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.5", ClampMax = "50.0"))
	float InteriorNearClipCm = 4.f;

	/** The car moving more than this in one camera update counts as a teleport and snaps the smoothing, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "100.0", ClampMax = "100000.0"))
	float TeleportSnapDistanceCm = 5000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FLRChaseCameraParams ChaseFar;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FLRChaseCameraParams ChaseNear;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FLRMountedCameraParams Hood;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FLRMountedCameraParams Bumper;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FLRCockpitCameraParams Cockpit;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FLRViralCameraParams Viral;

	FLRCameraConfig();

	/** Clamps every field to a safe range (FOV order, non-negative times, cockpit FOV in [MinFovDeg, MaxFovDeg]). */
	void Sanitize();

	/** Path of the default config file, relative to the data root. */
	static const TCHAR* GetDefaultDataPath() { return TEXT("Camera/Cameras.json"); }
};
