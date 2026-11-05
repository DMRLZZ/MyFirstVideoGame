// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"
#include "LRRoadParticipant.h"
#include "LRVehicleTypes.h"
#include "WheeledVehiclePawn.h"

#include "LRVehicle.generated.h"

/** Log category for the LRVehicle module. */
LRVEHICLE_API DECLARE_LOG_CATEGORY_EXTERN(LogLRVehicle, Log, All);

class UPrimitiveComponent;
class UStaticMeshComponent;
class ULRVehicleMovementComponent;
class ULRRoadSubsystem;
struct FHitResult;

/**
 * Player car of LEFTRIGHT: Chaos wheeled vehicle + driving assists + road awareness + crash detection.
 *
 * - Input API (SetThrottleInput, SetSteeringInput, RequestLaneChange...) is called by the player controller,
 *   an autopilot or a replay; the vehicle never reads devices itself.
 * - Arcade scheme: RequestLaneChange moves the target lane and FLRLaneAssist steers towards it every tick
 *   (real physics underneath, so crashes are real). Simcade: analog steering through FLRStabilityAssist.
 * - Every tick it caches its road coordinate (ULRRoadSubsystem::WorldToRoad with a segment hint), the current lane
 *   (broadcasting FLRLaneChangeEvent) and a smoothed local acceleration for the cameras.
 * - Mesh hits broadcast FLRCrashEvent on ULRGameEventSubsystem (debounced per actor).
 *
 * Configured from Data/Vehicles/<id>.json with LoadDefinitionById / ApplyDefinition.
 */
UCLASS()
class LRVEHICLE_API ALRVehicle : public AWheeledVehiclePawn, public ILRRoadParticipant
{
	GENERATED_BODY()

public:
	ALRVehicle(const FObjectInitializer& ObjectInitializer);

	// --- Input API -----------------------------------------------------------------------------

	/** Throttle pedal, 0..1 (clamped). */
	void SetThrottleInput(float Value01);

	/** Brake pedal, 0..1 (clamped). Held near standstill (below ReverseEngageSpeedKmh) it engages reverse. */
	void SetBrakeInput(float Value01);

	/** Analog steering, -1 (left) .. +1 (right). Used directly in Simcade; in Arcade only below the assist speed. */
	void SetSteeringInput(float ValueMinus1To1);

	/** Handbrake on/off. */
	void SetHandbrakeInput(bool bEngaged);

	/** Arcade scheme: moves the target lane one step (-1 left, +1 right). Ignored in Simcade, wrecked or disabled. */
	void RequestLaneChange(int32 Direction);

	/** Selects Arcade (lane assist) or Simcade (analog + stability assist). The target lane resyncs to the current lane. */
	void SetControlScheme(ELRControlScheme Scheme);

	/** Enables or disables driver input (countdown, menus). Assists keep the car in its lane while disabled. */
	void SetInputEnabled(bool bEnabled);

	/** Manual gearbox: requests the next gear up. */
	void ShiftGearUp();

	/** Manual gearbox: requests the next gear down. */
	void ShiftGearDown();

	// --- State ---------------------------------------------------------------------------------

	/** Magnitude of the velocity, km/h. */
	float GetSpeedKmh() const;

	/** Signed speed along the forward axis, m/s. */
	virtual float GetForwardSpeedMs() const override;

	/** Engine speed, rpm. */
	float GetEngineRpm() const;

	/** Rev limiter, rpm. */
	float GetEngineMaxRpm() const;

	/** Current gear (negative = reverse, 0 = neutral). */
	int32 GetCurrentGear() const;

	/** Driver throttle input after the speed limiter, 0..1 (what the engine receives). */
	float GetThrottleInput() const { return AppliedThrottle; }

	/** Raw driver brake input, 0..1. */
	float GetBrakeInput() const { return BrakeInput; }

	/** Steering input sent to Chaos this tick after assists, -1..1. */
	float GetAppliedSteering() const { return AppliedSteering; }

	/** Smoothed acceleration in the vehicle frame (X forward, Y right, Z up), m/s^2. */
	FVector GetLocalAccelerationMs2() const { return SmoothedLocalAccelerationMs2; }

	/** Road coordinate cached this tick (spawn coordinate before the first tick). */
	FLRRoadCoord GetRoadCoord() const { return CachedRoadCoord; }

	/** Lane the car is in (with hysteresis), INDEX_NONE without a road. */
	int32 GetCurrentLane() const { return CurrentLane; }

	/** Lane the lane assist steers to (Arcade); equals the current lane in Simcade. */
	int32 GetTargetLane() const { return TargetLane; }

	/** Vehicle yaw minus road yaw this tick, radians (+ = pointing right). */
	float GetHeadingErrorRad() const { return HeadingErrorRad; }

	/** True after SetWrecked(true): input disabled, physics coasting. */
	virtual bool IsWrecked() const override { return bWrecked; }

	/** Marks the car wrecked (inputs released, physics keeps simulating) or repaired. */
	void SetWrecked(bool bInWrecked);

	/** Footprint from the definition: X = length, Y = width, meters. */
	virtual FVector2D GetFootprintSizeMeters() const override;

	/** Definition currently applied (defaults until LoadDefinitionById / ApplyDefinition). */
	const FLRVehicleDefinition& GetDefinition() const { return Definition; }

	/** Current control scheme. */
	ELRControlScheme GetControlScheme() const { return ControlScheme; }

	/** True while driver input is accepted. */
	bool IsInputEnabled() const { return bInputEnabled; }

	/** LR movement component (never null after construction). */
	ULRVehicleMovementComponent* GetLRMovement() const;

	// --- Setup ---------------------------------------------------------------------------------

	/** Loads Data/Vehicles/<VehicleId>.json and applies it. Returns false (definition unchanged) on failure. */
	bool LoadDefinitionById(FName VehicleId);

	/**
	 * Applies a definition: skeletal mesh and animation class (visible fallback box if the mesh cannot be loaded),
	 * Chaos movement setup and assist parameters.
	 */
	void ApplyDefinition(const FLRVehicleDefinition& InDefinition);

	/**
	 * Starts a run: teleports onto the road at (S, D) facing the road heading, clears physics, wreck flag, inputs and
	 * assists, sets the forward velocity and a matching gear, and sets the target lane to the lane at D.
	 * @param InitialSpeedKmh  Rolling-start speed along the road heading, km/h (0 = standing start).
	 */
	void ResetForRun(double S, float D, float InitialSpeedKmh);

	// --- Cameras -------------------------------------------------------------------------------

	/**
	 * World transform of the camera anchor for a mode. Interior / mounted modes follow the full body rotation;
	 * chase modes use the yaw only and look at the car (ChaseLookAtHeightCm).
	 */
	FTransform GetCameraAnchorWorld(ELRCameraMode Mode) const;

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;

protected:
	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Bound to the skeletal mesh OnComponentHit. */
	UFUNCTION()
	void HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

private:
	/** Projects the car onto the road, updates heading error, curvature and the current lane (+ event). */
	void UpdateRoadState(const ULRRoadSubsystem& Road);

	/** Updates velocity caches, yaw rate, side slip and the smoothed local acceleration. */
	void UpdateKinematics(float DeltaSeconds);

	/** Computes and sends throttle, brake, steering and handbrake to Chaos. */
	void UpdateDriving();

	/** Steering for the active scheme (lane assist or stability assist). */
	float ComputeSteering() const;

	/** Shows / hides the fallback box used when the skeletal mesh is missing. */
	void SetFallbackBodyVisible(bool bVisible);

	/** Releases every driver input (also on Chaos). */
	void ClearInputs();

	/** True when RequestLaneChange may target oncoming lanes (two-way road: overtaking is the point). */
	bool AllowsOncomingLanes(const ULRRoadSubsystem& Road) const;

	/** Visible stand-in when the skeletal mesh cannot be loaded (no collision). */
	UPROPERTY(VisibleAnywhere, Category = "Vehicle")
	TObjectPtr<UStaticMeshComponent> FallbackBody;

	/**
	 * Vehicle loaded at BeginPlay when nothing applied a definition before (cars placed in a level, quick tests).
	 * Set to None to skip; gameplay normally calls LoadDefinitionById with the selected car.
	 */
	UPROPERTY(EditAnywhere, Category = "Vehicle")
	FName DefaultVehicleId = FName(TEXT("bauer_r3_sport"));

	/** Applied definition. */
	FLRVehicleDefinition Definition;

	/** True once ApplyDefinition ran. */
	bool bHasDefinition = false;

	// Driver inputs (raw).
	float ThrottleInput = 0.f;
	float BrakeInput = 0.f;
	float SteeringInput = 0.f;
	bool bHandbrakeInput = false;

	// Values sent to Chaos this tick.
	float AppliedThrottle = 0.f;
	float AppliedSteering = 0.f;

	ELRControlScheme ControlScheme = ELRControlScheme::Arcade;
	bool bInputEnabled = true;
	bool bWrecked = false;
	bool bReverseEngaged = false;

	// Road state.
	FLRRoadCoord CachedRoadCoord;
	bool bHasRoadCoord = false;
	int32 RoadSegmentHint = INDEX_NONE;
	float HeadingErrorRad = 0.f;
	float RoadCurvaturePerM = 0.f;
	int32 CurrentLane = INDEX_NONE;
	int32 TargetLane = INDEX_NONE;
	float TargetLaneCenterD = 0.f;

	// Kinematics.
	/** World velocity sampled this tick (start of frame, i.e. before this frame's physics step), cm/s. */
	FVector LastVelocityCms = FVector::ZeroVector;
	FVector SmoothedLocalAccelerationMs2 = FVector::ZeroVector;
	bool bHasPreviousVelocity = false;
	float ForwardSpeedMs = 0.f;
	float YawRateRadS = 0.f;
	float SideSlipRad = 0.f;

	// Crash debounce.
	TWeakObjectPtr<AActor> LastCrashActor;
	double LastCrashTimeSeconds = -1.0e9;
};
