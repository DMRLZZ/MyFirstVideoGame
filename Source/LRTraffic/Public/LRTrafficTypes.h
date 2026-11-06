// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LRTrafficTypes.generated.h"

/**
 * Data types of the traffic module.
 *
 * FLRTrafficProfile is loaded from Data/Traffic/TrafficProfile_*.json (type "TrafficProfile",
 * schema Data/Schema/TrafficProfile.schema.json). Every field has a sensible C++ default so a partial file works.
 * All values are SI (meters, seconds, m/s, m/s^2) unless the name says otherwise (Kmh, Deg).
 */

/** Light bits of FLRTrafficVehicleSnapshot::LightFlags (driver's frame: "left" is the driver's left). */
namespace LRTrafficLights
{
	inline constexpr uint8 Brake = 1 << 0;
	inline constexpr uint8 BlinkerLeft = 1 << 1;
	inline constexpr uint8 BlinkerRight = 1 << 2;
}

/**
 * Read-only view of one simulated traffic vehicle, produced by ULRTrafficSubsystem::GetSnapshot.
 * Consumers (replay, minimap, audio) copy it; it is never a live reference into the simulation.
 */
USTRUCT(BlueprintType)
struct LRTRAFFIC_API FLRTrafficVehicleSnapshot
{
	GENERATED_BODY()

	/** Stable id of the vehicle for its whole life in the simulation (never reused within a run). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 Id = INDEX_NONE;

	/** Road distance of the vehicle center, meters. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double S = 0.0;

	/** Lateral offset of the vehicle center, meters (+ = right of the route direction). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float D = 0.f;

	/** Heading offset relative to the vehicle's own travel direction (from lateral velocity), degrees (+ = turning right). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float YawOffsetDeg = 0.f;

	/** Speed magnitude along the travel direction, m/s (always >= 0; see Direction). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float SpeedMs = 0.f;

	/** +1 = drives toward increasing S (player's direction), -1 = oncoming (toward decreasing S). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 Direction = 1;

	/** Archetype name (e.g. "sedan", "truck"). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FName Archetype;

	/** LRTrafficLights bits: bit0 brake, bit1 left blinker, bit2 right blinker. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	uint8 LightFlags = 0;

	/** Body length, meters. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float LengthMeters = 4.6f;

	/** Body width, meters. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float WidthMeters = 1.85f;
};

/**
 * Behavior of a class of drivers: IDM (longitudinal) and MOBIL (lane change) parameters.
 * Each spawned vehicle picks one profile by Weight (or the one forced by its archetype).
 */
USTRUCT(BlueprintType)
struct LRTRAFFIC_API FLRTrafficDriverProfile
{
	GENERATED_BODY()

	/** Unique name referenced by archetypes and by FLRTrafficProfile::PlayerDriverProfile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver")
	FName Name = TEXT("normal");

	/** Relative spawn probability among profiles that are not forced by an archetype (>= 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/** Lower bound of the factor applied to (speed limit x archetype factor) to obtain the desired speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float DesiredSpeedFactorMin = 0.95f;

	/** Upper bound of the desired speed factor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float DesiredSpeedFactorMax = 1.05f;

	/** IDM safe time headway T, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float TimeHeadwayS = 1.5f;

	/** IDM jam distance s0, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.5", ClampMax = "20.0"))
	float MinGapM = 2.f;

	/** IDM maximum acceleration a, m/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float MaxAccelMs2 = 1.5f;

	/** IDM comfortable deceleration b, m/s^2 (positive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.1", ClampMax = "9.0"))
	float ComfortDecelMs2 = 2.f;

	/** MOBIL politeness p (0 = selfish, 1 = altruistic). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float Politeness = 0.3f;

	/** MOBIL switching threshold, m/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float LaneChangeThresholdMs2 = 0.2f;

	/** MOBIL keep-right bias, m/s^2 (> 0 favours moving right, < 0 favours the left lanes, e.g. "sunday" drivers). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "-2.0", ClampMax = "2.0"))
	float KeepRightBiasMs2 = 0.15f;

	/** MOBIL safety: maximum deceleration imposed on the new follower, m/s^2 (positive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.5", ClampMax = "9.0"))
	float SafeDecelMs2 = 4.f;

	/** Probability (0..1) per MOBIL evaluation that a lane change blocked only by the player fairness rules happens anyway as an "aggressive driver" event (always announced). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AggressiveEventChance = 0.f;

	/** Spawn preferably in the left lanes (the classic "sunday driver" plug). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver")
	bool bSpawnInLeftLanes = false;
};

/** Body type of traffic vehicles: footprint, speed range, lane preference and grey-box visual proportions. */
USTRUCT(BlueprintType)
struct LRTRAFFIC_API FLRTrafficArchetype
{
	GENERATED_BODY()

	/**
	 * Real car model (static mesh). When set it replaces the grey-box shapes: it is scaled uniformly to LengthMeters and
	 * the collision box follows its bounds. Empty = grey-box shapes built from the dimensions below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FSoftObjectPath ModelMesh;

	/** Optional wheel mesh, for models whose body comes without wheels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FSoftObjectPath ModelWheelMesh;

	/** Wheel centers relative to the model origin, left-front side, cm at model scale: X = axle front, Y = half track, Z = height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FVector ModelWheelOffsetCm = FVector::ZeroVector;

	/** Distance from the front axle back to the rear axle, cm at model scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	float ModelWheelbaseCm = 0.f;

	/** Yaw that makes the model face +X, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	float ModelYawDeg = 0.f;

	/** The material slot whose slot or material name contains this text gets the paint color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FString PaintMaterialHint = TEXT("Paint");

	/** Vector parameter of that material receiving the paint color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FName PaintParameter = FName(TEXT("PaintColor"));

	/** Unique archetype name (reported in near-miss events and snapshots). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype")
	FName Name = TEXT("sedan");

	/** Body length, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype", meta = (ClampMin = "2.0", ClampMax = "25.0"))
	float LengthMeters = 4.7f;

	/** Body width, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float WidthMeters = 1.85f;

	/** Body height, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype", meta = (ClampMin = "0.8", ClampMax = "5.0"))
	float HeightMeters = 1.45f;

	/** Relative spawn probability (>= 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/** Lower bound of the factor applied to the posted speed limit (before the driver factor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float SpeedFactorMin = 0.8f;

	/** Upper bound of the speed limit factor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float SpeedFactorMax = 1.f;

	/** Spawns in the right lanes, gets an extra keep-right bias and never uses the leftmost lane on roads with 3+ lanes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype")
	bool bPreferRightLanes = false;

	/** Driver profile forced for this archetype (e.g. "truck"); None = weighted random among all profiles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype")
	FName DriverProfile;

	/** Mass of the physics wreck, kilograms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype", meta = (ClampMin = "100.0", ClampMax = "40000.0"))
	float MassKg = 1500.f;

	/** Paint colors (linear); one is picked per vehicle. Empty = light grey. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype")
	TArray<FLinearColor> Colors;

	/** Grey-box: cabin length as a fraction of the body length (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float CabinLengthFraction = 0.5f;

	/** Grey-box: cabin center offset along the body as a fraction of the length (+ = toward the front). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual", meta = (ClampMin = "-0.5", ClampMax = "0.5"))
	float CabinOffsetFraction = -0.05f;

	/** Grey-box: height of the lower body as a fraction of the total height (the cabin fills the rest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float LowerBodyHeightFraction = 0.55f;
};

/** Longitudinal windows around the player, meters (relative to the player S, in the player's direction). */
USTRUCT(BlueprintType)
struct LRTRAFFIC_API FLRTrafficWindows
{
	GENERATED_BODY()

	/** Vehicles further behind the player than this are despawned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "50.0", ClampMax = "2000.0"))
	float SimBehindM = 250.f;

	/** Vehicles further ahead of the player than this are despawned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "200.0", ClampMax = "5000.0"))
	float SimAheadM = 1500.f;

	/** MOBIL (lane changes) only runs from playerS - this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float MobilBehindM = 150.f;

	/** MOBIL (lane changes) only runs up to playerS + this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "0.0", ClampMax = "5000.0"))
	float MobilAheadM = 600.f;

	/** Closest spawn distance ahead of the player (never inside the near field of view). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float SpawnAheadMinM = 600.f;

	/** Furthest spawn distance ahead of the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float SpawnAheadMaxM = 1200.f;

	/** Closest spawn distance behind the player (only used while the player is slower than traffic). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "50.0", ClampMax = "2000.0"))
	float SpawnBehindMinM = 180.f;

	/** Furthest spawn distance behind the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "50.0", ClampMax = "2000.0"))
	float SpawnBehindMaxM = 240.f;

	/** Pooled collision actors are used from playerS - this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float ActorBehindM = 150.f;

	/** Pooled collision actors are used up to playerS + this distance; further vehicles use the far instanced mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "50.0", ClampMax = "5000.0"))
	float ActorAheadM = 600.f;

	/** Run reset prefill: closest distance ahead of the player where vehicles are placed (exception to SpawnAheadMinM, the scene is not visible yet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	float InitialFillMinAheadM = 60.f;

	/** Run reset prefill: the lanes occupied by the player stay empty up to this distance ahead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Windows", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float InitialClearLaneAheadM = 150.f;
};

/** Fair-play rules toward the player (docs/02-ARCHITECTURE.md §5.2, non negotiable). */
USTRUCT(BlueprintType)
struct LRTRAFFIC_API FLRTrafficFairness
{
	GENERATED_BODY()

	/** No AI lane change into the player's lane ahead of the player when time-to-collision is below this, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fairness", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float FairnessTtcSeconds = 1.2f;

	/** No AI lane change ends closer than this to the player (any direction), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fairness", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float MinGapToPlayerM = 8.f;

	/** Enforces the solvability invariant (never all player-direction lanes blocked in a window of SolvabilityWindowM). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fairness")
	bool bEnforceSolvability = true;

	/** Length of the solvability window, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fairness", meta = (ClampMin = "5.0", ClampMax = "200.0"))
	float SolvabilityWindowM = 40.f;

	/** Extra distance checked ahead of the vehicle when approving a lane change (motion until the lane flip), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fairness", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SolvabilityLaneChangeMarginM = 10.f;

	/** Aggressive driver events blink at least this long before moving, seconds (>= 1.0 by design). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fairness", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float AggressiveMinBlinkSeconds = 1.f;

	/** Duration of the lateral move of an aggressive lane change, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fairness", meta = (ClampMin = "0.5", ClampMax = "6.0"))
	float AggressiveMoveSeconds = 1.6f;
};

/** Lane change timing (blinker, lateral motion) and MOBIL scheduling. */
USTRUCT(BlueprintType)
struct LRTRAFFIC_API FLRTrafficLaneChangeParams
{
	GENERATED_BODY()

	/** Minimum blinker time before the lateral motion starts, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float BlinkerMinSeconds = 1.f;

	/** Maximum blinker time before the lateral motion starts, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float BlinkerMaxSeconds = 2.f;

	/** Minimum duration of the lateral smootherstep motion, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MoveMinSeconds = 2.5f;

	/** Maximum duration of the lateral smootherstep motion, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MoveMaxSeconds = 4.5f;

	/** Period of the MOBIL evaluation of each vehicle, seconds (vehicles are phase shifted). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float MobilIntervalSeconds = 0.5f;

	/** Minimum time between two lane changes of the same vehicle, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float CooldownSeconds = 4.f;

	/** Duration of the move back when a lane change is aborted, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.3", ClampMax = "5.0"))
	float AbortMoveSeconds = 1.5f;

	/** A lane change paused at mid-lane (solvability) is aborted after this long, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float MaxPauseSeconds = 3.f;

	/** Extra keep-right bias for archetypes with bPreferRightLanes, m/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LaneChange", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float HeavyKeepRightBiasMs2 = 0.3f;
};

/** Near-miss detection thresholds (docs/04-GAMEPLAY.md §5). */
USTRUCT(BlueprintType)
struct LRTRAFFIC_API FLRNearMissParams
{
	GENERATED_BODY()

	/** Clearance below which a pass counts as "Close", meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NearMiss", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float CloseThresholdM = 1.f;

	/** Clearance below which a pass counts as "VeryClose", meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NearMiss", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float VeryCloseThresholdM = 0.5f;

	/** Clearance below which a pass counts as "Razor", meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NearMiss", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float RazorThresholdM = 0.25f;

	/** Minimum relative speed for a pass to count, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NearMiss", meta = (ClampMin = "0.0", ClampMax = "300.0"))
	float MinRelativeSpeedKmh = 20.f;

	/** A qualifying pass on the opposite side completed within this time makes "thread the needle", seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NearMiss", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ThreadWindowSeconds = 0.6f;
};

/**
 * Complete traffic configuration (Data/Traffic/TrafficProfile_Default.json, type "TrafficProfile").
 * Read by ULRTrafficSubsystem and handed to the pure FLRTrafficSim.
 */
USTRUCT(BlueprintType)
struct LRTRAFFIC_API FLRTrafficProfile
{
	GENERATED_BODY()

	/** Vehicle body types. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	TArray<FLRTrafficArchetype> Archetypes;

	/** Driver behavior classes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	TArray<FLRTrafficDriverProfile> DriverProfiles;

	/** Profile used to predict the player's reaction when the player is the follower in MOBIL. None = first profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	FName PlayerDriverProfile = TEXT("normal");

	/** Density over run time: X = seconds since the run started, Y = vehicles per km per lane (piecewise linear, clamped). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	TArray<FVector2D> DensityCurve;

	/** Multiplier of the density of oncoming lanes (two-way roads). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float OncomingDensityFactor = 0.8f;

	/** Simulation capacity (vehicles in both directions). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 MaxVehicles = 200;

	/** Number of pooled collision actors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaxPooledActors = 60;

	/** Fixed simulation rate, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "10.0", ClampMax = "240.0"))
	float FixedStepHz = 60.f;

	/** Maximum simulation steps per rendered frame (excess time is dropped). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxStepsPerFrame = 4;

	/** Spawn attempts per simulation step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "0", ClampMax = "32"))
	int32 MaxSpawnAttemptsPerStep = 2;

	/** Minimum bumper-to-bumper distance to the neighbors in the lane of a new vehicle, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "5.0", ClampMax = "500.0"))
	float SpawnMinGapM = 30.f;

	/** Wrecked vehicles stay as stopped obstacles for this long before being removed, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float WreckLifetimeSeconds = 10.f;

	/**
	 * Impulse given to a traffic vehicle that becomes a wreck, as a fraction of (its mass x the player's impact speed),
	 * pushed away from the contact point (0 = keep only the kinematic velocity).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float WreckImpulseScale = 0.5f;

	/** Draws vehicles beyond the pooled actor window (up to Windows.SimAheadM) as one instanced mesh without collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	bool bDrawFarVehicles = true;

	/** Brake lights turn on below this acceleration (i.e. deceleration above its absolute value), m/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "0.0", ClampMax = "9.0"))
	float BrakeLightDecelMs2 = 0.8f;

	/** Blinker frequency, Hz (visual only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float BlinkerFrequencyHz = 1.5f;

	/** Speed limit used when the road gives none, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic", meta = (ClampMin = "30.0", ClampMax = "300.0"))
	float DefaultSpeedLimitKmh = 130.f;

	/** Longitudinal windows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	FLRTrafficWindows Windows;

	/** Fair-play rules. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	FLRTrafficFairness Fairness;

	/** Lane change timing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	FLRTrafficLaneChangeParams LaneChange;

	/** Near-miss thresholds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	FLRNearMissParams NearMiss;
};
