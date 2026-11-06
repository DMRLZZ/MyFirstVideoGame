// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

#include "LRVehicleTypes.generated.h"

/**
 * Data model of the player vehicles (Data/Vehicles/<id>.json, envelope type "VehicleDefinition").
 *
 * Every field has a sensible sports-car default so a partially written JSON file still produces a drivable car.
 * Units are written in the field names when they are not SI (Chaos expects centimeters for wheel and chassis sizes).
 */

/** Which axle(s) receive engine torque. Mapped to the Chaos EVehicleDifferential by ULRVehicleMovementComponent. */
UENUM(BlueprintType)
enum class ELRDriveType : uint8
{
	RearWheelDrive,
	FrontWheelDrive,
	AllWheelDrive
};

/** Engine model (maps to FVehicleEngineConfig). */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLREngineParams
{
	GENERATED_BODY()

	/** Peak engine torque, Newton meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "50.0", ClampMax = "3000.0"))
	float MaxTorqueNm = 450.f;

	/** Rev limiter, revolutions per minute. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "1000.0", ClampMax = "20000.0"))
	float MaxRpm = 7000.f;

	/** Idle speed, revolutions per minute. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "300.0", ClampMax = "3000.0"))
	float IdleRpm = 900.f;

	/**
	 * Torque curve: X = rpm normalized to [0, 1] of MaxRpm, Y = torque factor in [0, 1] of MaxTorqueNm.
	 * Points must be sorted by X. Converted to an rpm-domain curve when applied to Chaos.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine")
	TArray<FVector2D> TorqueCurve = {
		FVector2D(0.0, 0.55), FVector2D(0.25, 0.80), FVector2D(0.5, 0.97),
		FVector2D(0.7, 1.0), FVector2D(0.9, 0.90), FVector2D(1.0, 0.78) };

	/** Engine braking when the throttle is released (Chaos EngineBrakeEffect, 0..1 typical). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float EngineBrakeEffect = 0.05f;

	/** Rotational inertia used when revving up (Chaos EngineRevUpMOI); lower = snappier revs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.01", ClampMax = "50.0"))
	float RevUpMOI = 5.f;

	/** How fast the rpm falls with the throttle closed, rpm per second (Chaos EngineRevDownRate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.01", ClampMax = "20000.0"))
	float RevDownRate = 600.f;
};

/** Gearbox model (maps to FVehicleTransmissionConfig). */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRTransmissionParams
{
	GENERATED_BODY()

	/** Automatic gearbox (default). Manual requires ALRVehicle::ShiftGearUp/Down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission")
	bool bAutomatic = true;

	/** Differential (final drive) ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float FinalDriveRatio = 3.4f;

	/** Forward gear ratios, first gear first, strictly decreasing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission")
	TArray<float> ForwardGearRatios = { 3.30f, 2.20f, 1.62f, 1.28f, 1.06f, 0.94f };

	/** Reverse gear ratio (positive number; Chaos applies the sign). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float ReverseGearRatio = 3.2f;

	/** Automatic upshift engine speed, rpm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "1000.0", ClampMax = "20000.0"))
	float ChangeUpRpm = 6500.f;

	/** Automatic downshift engine speed, rpm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "500.0", ClampMax = "20000.0"))
	float ChangeDownRpm = 3500.f;

	/** Time without drive torque during a gear change, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float GearChangeTime = 0.18f;

	/** Mechanical efficiency of the drivetrain, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transmission", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TransmissionEfficiency = 0.92f;
};

/** Torque split between axles (maps to FVehicleDifferentialConfig). */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRDifferentialParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Differential")
	ELRDriveType DriveType = ELRDriveType::RearWheelDrive;

	/** All-wheel drive only: fraction of torque sent to the FRONT axle (Chaos FrontRearSplit), 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Differential", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FrontRearSplit = 0.4f;
};

/** Wheel, tire, brake and suspension parameters shared by every wheel of one axle. */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRWheelAxleParams
{
	GENERATED_BODY()

	/** Skeleton bone of each wheel on this axle, left first (e.g. Phys_Wheel_FL, Phys_Wheel_FR). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels")
	TArray<FName> BoneNames;

	/** Tire radius, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "15.0", ClampMax = "80.0"))
	float RadiusCm = 34.f;

	/** Tire width, centimeters (only affects shape casts; Chaos has no runtime setter for it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "10.0", ClampMax = "60.0"))
	float WidthCm = 25.f;

	/** Grip multiplier of the tire (Chaos FrictionForceMultiplier). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float FrictionForceMultiplier = 3.f;

	/** Lateral stiffness of the tire (Chaos CorneringStiffness); higher = more direct, less slip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float CorneringStiffness = 1000.f;

	/** Maximum steering angle at full lock, degrees (0 = not steered, rear axle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float MaxSteerAngleDeg = 0.f;

	/** Service brake torque per wheel, Newton meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float MaxBrakeTorqueNm = 1500.f;

	/** Handbrake torque per wheel, Newton meters (0 = not affected by the handbrake). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float MaxHandBrakeTorqueNm = 0.f;

	/** Suspension travel above the rest position, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float SuspensionMaxRaiseCm = 8.f;

	/** Suspension travel below the rest position, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float SuspensionMaxDropCm = 10.f;

	/** Spring rate (Chaos SpringRate, N/cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "10.0", ClampMax = "5000.0"))
	float SpringRate = 250.f;

	/** Spring preload (Chaos SpringPreload, N). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.0", ClampMax = "5000.0"))
	float SpringPreload = 50.f;

	/** Damping ratio (Chaos SuspensionDampingRatio), 0..1+. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float SuspensionDampingRatio = 0.5f;
};

/** Body aerodynamics (maps to the Chaos movement component aero fields). */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRAeroParams
{
	GENERATED_BODY()

	/** Drag coefficient Cd (dimensionless). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aero", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float DragCoefficient = 0.32f;

	/** Downforce coefficient Cl (dimensionless, positive pushes the car down). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aero", meta = (ClampMin = "-0.5", ClampMax = "3.0"))
	float DownforceCoefficient = 0.1f;

	/** Frontal width used for the drag area, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aero", meta = (ClampMin = "100.0", ClampMax = "300.0"))
	float ChassisWidthCm = 185.f;

	/** Frontal height used for the drag area, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aero", meta = (ClampMin = "80.0", ClampMax = "300.0"))
	float ChassisHeightCm = 130.f;
};

/** Steering geometry and input smoothing (maps to FVehicleSteeringConfig and the steering input rate). */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRSteeringParams
{
	GENERATED_BODY()

	/** Ackermann angle ratio (Chaos AngleRatio), 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AngleRatio = 0.7f;

	/** Maximum steering input change per second when moving away from center (Chaos SteeringInputRate.RiseRate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.5", ClampMax = "50.0"))
	float InputRiseRate = 8.f;

	/** Maximum steering input change per second when returning to center (Chaos SteeringInputRate.FallRate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.5", ClampMax = "50.0"))
	float InputFallRate = 10.f;
};

/** Camera anchors, owned by the vehicle so every car can place its cameras (LRCamera reads them). */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRCameraAnchors
{
	GENERATED_BODY()

	/** Driver eye position, centimeters in actor space (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector CockpitEyeCm = FVector(-20.0, -36.0, 108.0);

	/** Hood camera position, centimeters in actor space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector HoodCm = FVector(40.0, 0.0, 125.0);

	/** Front bumper camera position, centimeters in actor space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector BumperCm = FVector(230.0, 0.0, 55.0);

	/** Viral / GoPro / dashcam mount, centimeters in actor space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector ViralMountCm = FVector(5.0, 0.0, 118.0);

	/** Far chase camera: distance behind the car, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "100.0", ClampMax = "3000.0"))
	float ChaseFarDistanceCm = 720.f;

	/** Far chase camera: height above the car origin, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float ChaseFarHeightCm = 230.f;

	/** Near chase camera: distance behind the car, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "100.0", ClampMax = "3000.0"))
	float ChaseNearDistanceCm = 520.f;

	/** Near chase camera: height above the car origin, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float ChaseNearHeightCm = 170.f;

	/** Chase cameras look at this height above the car origin, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float ChaseLookAtHeightCm = 90.f;
};

/**
 * Lane-keeping assist (Arcade scheme), consumed by FLRLaneAssist::ComputeSteering.
 *
 * Cascade: lateral error -> desired lateral speed (P, braking-distance profile and clamp) -> desired heading offset
 * atan2(vLat, v) -> heading PD with yaw-rate damping, limited by tire grip. Heading gains are scheduled with
 * (ReferenceSpeedMs / v)^SpeedGainExponent because the yaw response of a car to a steering angle grows with speed;
 * this keeps the closed loop speed-independent (same lane change at 100, 200 or 300 km/h).
 * Defaults: 3.7 m lane change in ~1.0 s, no overshoot, robust to ~0.15 s of tire / steering lag.
 */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRLaneAssistParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist")
	bool bEnabled = true;

	/** Lateral P gain: desired lateral speed per meter of error, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float LateralGain = 2.2f;

	/** Maximum lateral speed requested during a lane change, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.5", ClampMax = "15.0"))
	float MaxLateralSpeedMs = 6.f;

	/** Inner loop gain: lateral acceleration per m/s of lateral speed error, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float LateralSpeedGain = 6.f;

	/** Damping: fraction of the lateral acceleration from yaw (v * yaw rate relative to the road) that is subtracted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float YawRateDamping = 0.3f;

	/** Speed floor used in the control law, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.5", ClampMax = "20.0"))
	float MinSpeedMs = 5.f;

	/** Below this forward speed the assist is inactive and the raw steering input passes through, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float MinActiveSpeedMs = 2.f;

	/**
	 * Lateral acceleration produced by one unit of steering input at road speed, m/s^2. MEASURED in the running game
	 * (Saved/DevShots/trace.csv from Tools/Scripts/devdrive.ps1): lateral acceleration / steering in a steady turn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float SteeringToLateralAccelMs2 = 15.f;

	/** Low-speed limit of the authority: path curvature per unit of steering (~ tan(max wheel angle) / wheelbase), 1/m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.01", ClampMax = "2.0"))
	float KinematicCurvaturePerSteering = 0.25f;

	/** Lateral acceleration the assist may ask for on top of the road curve, m/s^2. Keep it below the tire grip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "1.0", ClampMax = "40.0"))
	float MaxLateralAccelMs2 = 11.f;

	/**
	 * Fraction of MaxLateralAccelMs2 budgeted to stop the lateral motion at the target: the desired lateral speed never
	 * exceeds sqrt(2 * fraction * MaxLateralAccel * |error|), which removes overshoot when the grip limit is reached.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float LateralDecelFraction = 0.25f;

	/** Output clamp, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MaxSteering = 1.f;

	/** The reported current lane only switches when the new lane center is closer by this margin, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneAssist", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float LaneChangeHysteresisMeters = 0.2f;
};

/**
 * Simcade stability aids, consumed by FLRStabilityAssist.
 *
 * Steering sensitivity vs speed, counter-steer yaw damping (ESP-like) and the per-wheel ABS / traction control flags
 * forwarded to Chaos.
 */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRStabilityAssistParams
{
	GENERATED_BODY()

	/** Scale the analog steering input with speed using SteeringScaleCurve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability")
	bool bSteeringScaleEnabled = true;

	/** Steering scale vs speed: X = speed km/h, Y = scale (0..1]. Sorted by X, non-increasing in Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability")
	TArray<FVector2D> SteeringScaleCurve = {
		FVector2D(0.0, 1.0), FVector2D(60.0, 0.8), FVector2D(120.0, 0.5),
		FVector2D(200.0, 0.32), FVector2D(300.0, 0.22), FVector2D(400.0, 0.17) };

	/** Counter-steer when the car rotates faster than the driver asked for (oversteer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability")
	bool bYawDampingEnabled = true;

	/** Counter-steer per rad/s of excess yaw rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float YawDampingGain = 0.35f;

	/** Excess yaw rate tolerated before the damping acts, rad/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float YawRateDeadbandRadS = 0.05f;

	/** Counter-steer per radian of body side slip beyond SideSlipThresholdRad. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float CounterSteerGain = 1.2f;

	/** Side slip tolerated before counter-steer, radians. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SideSlipThresholdRad = 0.06f;

	/** Maximum steering correction added by the aids, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxCorrection = 0.35f;

	/** Lateral acceleration the tires can sustain, used to cap the yaw rate the driver can ask for, m/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float MaxLateralAccelMs2 = 9.f;

	/** The aids are inactive below this speed, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float MinSpeedMs = 3.f;

	/** Chaos traction control on every driven wheel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability")
	bool bTractionControlEnabled = true;

	/** Chaos ABS on every wheel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability")
	bool bABSEnabled = true;
};

/**
 * Complete definition of a player car, loaded from Data/Vehicles/<id>.json (type "VehicleDefinition").
 * Owned by value by ALRVehicle (GetDefinition) and applied to the Chaos movement component by ULRVehicleMovementComponent.
 */
USTRUCT(BlueprintType)
struct LRVEHICLE_API FLRVehicleDefinition
{
	GENERATED_BODY()

	/** Unique id, equal to the file name without extension (e.g. "bauer_r3_sport"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName Id;

	/** Name shown in the garage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString DisplayName;

	/** Skeletal mesh with a physics asset and wheel bones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assets")
	FSoftObjectPath SkeletalMesh;

	/** Animation blueprint class (a UVehicleAnimationInstance) driving the wheel bones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assets")
	FSoftClassPath AnimClass;

	/** Id of Data/Audio/<AudioProfile>.json used by LRAudio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assets")
	FName AudioProfile;

	/** Body length (footprint X), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "2.0", ClampMax = "8.0"))
	float LengthMeters = 4.5f;

	/** Body width without mirrors (footprint Y), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "1.2", ClampMax = "3.0"))
	float WidthMeters = 1.9f;

	/** Distance between axles, meters (used by the stability assist yaw model). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "1.5", ClampMax = "5.0"))
	float WheelbaseMeters = 2.7f;

	/** Total mass, kilograms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "500.0", ClampMax = "5000.0"))
	float MassKg = 1500.f;

	/** Use CenterOfMassOffsetCm (Chaos bEnableCenterOfMassOverride). Off = center of mass from the physics asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	bool bOverrideCenterOfMass = false;

	/** Center of mass position relative to the actor origin, centimeters in actor space (Chaos CenterOfMassOverride). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	FVector CenterOfMassOffsetCm = FVector::ZeroVector;

	/** Electronic top speed, km/h. The throttle fades out over the last TopSpeedFadeFraction of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "60.0", ClampMax = "600.0"))
	float TopSpeedKmh = 250.f;

	/** Fraction of TopSpeedKmh over which the limiter fades the throttle, 0..0.5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float TopSpeedFadeFraction = 0.03f;

	/** Holding the brake engages reverse only below this absolute speed, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float ReverseEngageSpeedKmh = 5.f;

	/** Impact speed along the contact normal considered severe (ends the run under default rules), km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "1.0", ClampMax = "200.0"))
	float CrashImpactKmh = 25.f;

	/** Impacts below this speed are not reported at all (grazes, settling), km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float CrashMinReportKmh = 3.f;

	/** Repeated hits against the same actor inside this window are ignored, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float CrashDebounceSeconds = 0.25f;

	/** Hits whose normal Z exceeds this value are treated as ground contact and ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrashFloorNormalZ = 0.7f;

	/** A horizontal velocity change of at least this much within one frame counts as an impact, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float ImpactMinDeltaKmh = 8.f;

	/** ...and only if the deceleration also exceeds this (filters frame hitches), m/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "5.0", ClampMax = "500.0"))
	float ImpactMinAccelMs2 = 30.f;

	/** The vehicle counts as off the road when it is this far below the road surface, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "0.5", ClampMax = "50.0"))
	float OffRoadBelowMeters = 2.5f;

	/** The vehicle counts as off the road when it is this far beyond the outer barriers, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float OffRoadLateralMarginMeters = 3.f;

	/**
	 * Visual car body, static mesh attached to the physics skeleton. When set, the skeletal mesh is only the physics
	 * and animation rig and is hidden (this is how the Epic vehicle template is built).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FSoftObjectPath BodyMesh;

	/** Windows, static mesh attached like the body. Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FSoftObjectPath GlassMesh;

	/** One wheel, static mesh attached to each wheel bone of FrontWheels and RearWheels. Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FSoftObjectPath WheelMesh;

	/** Paint color of the visual body (material slot "Paint", parameter "PaintColor"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FLinearColor PaintColor = FLinearColor(0.55f, 0.03f, 0.02f, 1.f);

	/** Offset of the visual body from the actor origin, cm (generated cars have Z = 0 at the ground, the rig rides higher). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FVector VisualOffsetCm = FVector::ZeroVector;

	/** Front axle of the car design: X from the origin, half track Y, wheel radius Z, cm. The physics wheels move there. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FVector FrontWheelCm = FVector(135.0, 80.0, 33.0);

	/** Rear axle of the car design: X from the origin, half track Y, wheel radius Z, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FVector RearWheelCm = FVector(-129.0, 80.0, 34.0);

	/** Radius of the wheel mesh, cm: the visual wheels are scaled from it to each axle radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	float WheelMeshRadiusCm = 35.f;

	/** Time constant of the smoothed local acceleration exposed to cameras, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AccelerationSmoothingSeconds = 0.12f;

	/** Extra height added when teleporting onto the road, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float SpawnHeightOffsetCm = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drivetrain")
	FLREngineParams Engine;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drivetrain")
	FLRTransmissionParams Transmission;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drivetrain")
	FLRDifferentialParams Differential;

	/** Front axle (steered). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels")
	FLRWheelAxleParams FrontWheels;

	/** Rear axle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheels")
	FLRWheelAxleParams RearWheels;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aero")
	FLRAeroParams Aero;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering")
	FLRSteeringParams Steering;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FLRCameraAnchors Cameras;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assists")
	FLRLaneAssistParams LaneAssist;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Assists")
	FLRStabilityAssistParams StabilityAssist;

	FLRVehicleDefinition()
	{
		FrontWheels.BoneNames = { FName(TEXT("Phys_Wheel_FL")), FName(TEXT("Phys_Wheel_FR")) };
		FrontWheels.MaxSteerAngleDeg = 38.f;
		FrontWheels.MaxBrakeTorqueNm = 1800.f;
		RearWheels.BoneNames = { FName(TEXT("Phys_Wheel_BL")), FName(TEXT("Phys_Wheel_BR")) };
		RearWheels.MaxBrakeTorqueNm = 1400.f;
		RearWheels.MaxHandBrakeTorqueNm = 3000.f;
	}
};
