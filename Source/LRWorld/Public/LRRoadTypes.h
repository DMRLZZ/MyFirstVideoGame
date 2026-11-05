// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LRRoadTypes.generated.h"

/**
 * Cross-section profile of the highway (shared by every segment of a route).
 *
 * One-way: LanesPerDirection lanes, all in the player's direction, centered on D = 0.
 * Two-way (US right-hand traffic): LanesPerDirection player lanes on the right (D > 0) and the same number
 * of oncoming lanes on the left (D < 0), separated by MedianWidthMeters centered on D = 0.
 * Loaded as part of FLRRouteDefinition (Data/World/Route_*.json).
 */
USTRUCT(BlueprintType)
struct LRWORLD_API FLRRoadProfile
{
	GENERATED_BODY()

	/** Number of lanes per direction of travel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road", meta = (ClampMin = "1", ClampMax = "6"))
	int32 LanesPerDirection = 4;

	/** Width of every lane, meters (US standard 12 ft = 3.66 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road", meta = (ClampMin = "2.5", ClampMax = "5.0"))
	float LaneWidthMeters = 3.7f;

	/** Paved shoulder width on each outer side, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road", meta = (ClampMin = "0.0", ClampMax = "6.0"))
	float ShoulderWidthMeters = 3.0f;

	/**
	 * Median width, meters. Two-way: painted gap between both directions (double yellow line, no barrier).
	 * One-way: distance from the left shoulder edge to the median barrier is half of this value (0 = plain edge barrier).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float MedianWidthMeters = 4.0f;

	/** True for a two-way road with oncoming lanes on the left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road")
	bool bTwoWay = false;
};

/** Description of one lane in road coordinates. Lanes are indexed 0..N-1 sorted by CenterD ascending (left to right). */
USTRUCT(BlueprintType)
struct LRWORLD_API FLRLaneDesc
{
	GENERATED_BODY()

	/** Lateral position of the lane center, meters (+ = right of the route direction). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road")
	float CenterD = 0.f;

	/** Lane width, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road")
	float WidthMeters = 3.7f;

	/** +1 = player's direction of travel, -1 = oncoming traffic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Road")
	int32 Direction = 1;
};

/**
 * Parameters of the procedural road layout generator (FLRRoadGenerator).
 * Data/Schema/RoadGenParams.schema.json; embedded in FLRRouteDefinition::GenParams.
 */
USTRUCT(BlueprintType)
struct LRWORLD_API FLRRoadGenParams
{
	GENERATED_BODY()

	/** Length of the first straight of every route (spawn area), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "0.0"))
	float InitialStraightLengthM = 800.f;

	/** Minimum length of a straight, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "10.0"))
	float MinStraightLengthM = 400.f;

	/** Maximum length of a straight, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "10.0"))
	float MaxStraightLengthM = 1500.f;

	/** Minimum length of the constant-radius part of a curve, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "10.0"))
	float MinArcLengthM = 200.f;

	/** Maximum length of the constant-radius part of a curve, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "10.0"))
	float MaxArcLengthM = 800.f;

	/** Minimum curve radius, meters. Must be >= 1000 m to be drivable at 300 km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "1000.0"))
	float MinRadiusM = 1500.f;

	/** Maximum curve radius, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "1000.0"))
	float MaxRadiusM = 4000.f;

	/** Probability (0..1) that a straight is followed by a curve instead of another straight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CurveProbability = 0.6f;

	/** Probability (0..1) that a curve turns the opposite way of the previous curve (keeps the route meandering). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TurnReversalProbability = 0.65f;

	/** Maximum heading change of the constant-radius part of a curve, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float MaxArcHeadingChangeDeg = 45.f;

	/** Length of the stepped-curvature easement at each end of a curve, meters (0 = no easement). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "0.0"))
	float TransitionLengthM = 120.f;

	/** Number of constant-curvature steps that approximate each easement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "1", ClampMax = "16"))
	int32 TransitionSteps = 4;

	/** Maximum absolute grade (rise / run, 0.04 = 4 %). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "0.0", ClampMax = "0.08"))
	float MaxGrade = 0.03f;

	/** Probability (0..1) that a new piece (straight or curve) changes the grade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GradeChangeProbability = 0.35f;

	/** Minimum radius of vertical curves, meters: limits the grade change per meter to 1 / this value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "500.0"))
	float MinVerticalCurveRadiusM = 8000.f;
};

/**
 * Lighting / atmosphere preset applied by ALREnvironmentActor.
 * Data/World/Environment_*.json (type "EnvironmentPreset").
 */
USTRUCT(BlueprintType)
struct LRWORLD_API FLREnvironmentPreset
{
	GENERATED_BODY()

	/** Pitch of the sun light direction, degrees. Negative = light points down (sun above the horizon). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float SunPitchDeg = -50.f;

	/** Yaw of the sun light direction, degrees (Unreal yaw). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment")
	float SunYawDeg = 30.f;

	/** Illuminance of the directional light, lux (clear midday sun ~ 100000 lux, full moon ~ 0.3 lux). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment", meta = (ClampMin = "0.0"))
	float SunIntensityLux = 100000.f;

	/** Color of the directional light (linear). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment")
	FLinearColor SunColor = FLinearColor(1.f, 0.96f, 0.9f, 1.f);

	/** Angular diameter of the light source, degrees (sun ~ 0.53). Drives soft shadows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float SunSourceAngleDeg = 0.5357f;

	/** Sky light intensity scale (real-time captured sky). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment", meta = (ClampMin = "0.0"))
	float SkyLightIntensity = 1.f;

	/** Exponential height fog density. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float FogDensity = 0.01f;

	/** Exponential height fog height falloff. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment", meta = (ClampMin = "0.001", ClampMax = "2.0"))
	float FogHeightFalloff = 0.2f;

	/** Fog inscattering color (linear). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment")
	FLinearColor FogInscatteringColor = FLinearColor(0.447f, 0.638f, 1.f, 1.f);

	/** Enables volumetric fog (GPU cost ~0.5 ms). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment")
	bool bVolumetricFog = false;

	/** Exposure compensation (EV) applied by the unbound post process. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment", meta = (ClampMin = "-15.0", ClampMax = "15.0"))
	float ExposureCompensation = 0.f;

	/** Night preset: other systems (headlights, street lamps) read it through ALREnvironmentActor::IsNight(). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Environment")
	bool bNight = false;
};

/** Distances that drive geometry generation and mesh streaming around the player. Part of FLRRouteDefinition. */
USTRUCT(BlueprintType)
struct LRWORLD_API FLRRoadStreamingParams
{
	GENERATED_BODY()

	/** The road layout is generated until playerS + this distance, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "500.0"))
	float GenerateAheadM = 3000.f;

	/** Road layout before playerS - this distance is trimmed, meters (never inside the visible window). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "100.0"))
	float KeepBehindM = 1000.f;

	/** Road meshes are kept down to playerS - this distance, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "50.0"))
	float VisibleBehindM = 300.f;

	/** Road meshes are built up to playerS + this distance, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "200.0"))
	float VisibleAheadM = 2000.f;

	/** Length of one streamed mesh chunk, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "20.0", ClampMax = "500.0"))
	float ChunkLengthM = 100.f;

	/** Longitudinal sampling step of chunk meshes, meters (chord error on R = 1000 m at 5 m ~ 3 mm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "0.5", ClampMax = "50.0"))
	float MeshSampleStepM = 5.f;

	/** Maximum chunks built per frame during normal streaming (a run reset builds the whole window at once). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxChunkBuildsPerFrame = 2;

	/** On a run reset, chunks closer than this to the player cook collision synchronously, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "0.0"))
	float SyncCookRadiusM = 250.f;
};

/** Visual dressing of the greybox road: markings, barriers, roadside rhythm objects and colors. Part of FLRRouteDefinition. */
USTRUCT(BlueprintType)
struct LRWORLD_API FLRRoadDressing
{
	GENERATED_BODY()

	/** Width of dashed lane lines, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Markings", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float LineWidthM = 0.15f;

	/** Width of solid edge lines and each center yellow line, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Markings", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float EdgeLineWidthM = 0.2f;

	/** Length of one dash, meters (US: 10 ft). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Markings", meta = (ClampMin = "0.5"))
	float DashLengthM = 3.f;

	/** Gap between dashes, meters (US: 30 ft). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Markings", meta = (ClampMin = "0.5"))
	float DashGapM = 9.f;

	/** Center-to-center distance of the two lines of the double yellow center line, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Markings", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float DoubleLineSpacingM = 0.35f;

	/** Height of paint above the asphalt (avoids z-fighting), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Markings", meta = (ClampMin = "0.001", ClampMax = "0.05"))
	float PaintHeightM = 0.01f;

	/** Distance from the outer shoulder edge to the barrier center line, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barriers", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float BarrierOffsetM = 0.6f;

	/** Barrier width at the base, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barriers", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float BarrierBaseWidthM = 0.6f;

	/** Barrier width at the top, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barriers", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float BarrierTopWidthM = 0.2f;

	/** Barrier height, meters (jersey barrier ~ 0.81 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barriers", meta = (ClampMin = "0.2", ClampMax = "3.0"))
	float BarrierHeightM = 0.85f;

	/** Spacing of lamp posts along S, meters (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roadside", meta = (ClampMin = "0.0"))
	float LampPostSpacingM = 50.f;

	/** Lamp post height, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roadside", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float LampPostHeightM = 10.f;

	/** Lamp post radius, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roadside", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float LampPostRadiusM = 0.12f;

	/** Length of the lamp arm reaching over the road, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roadside", meta = (ClampMin = "0.0", ClampMax = "6.0"))
	float LampArmLengthM = 2.f;

	/** Distance from the barrier center line to the lamp posts (outwards), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roadside", meta = (ClampMin = "0.3", ClampMax = "10.0"))
	float LampSetbackM = 1.f;

	/** Spacing of reflector posts along S (mounted on the barriers), meters (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roadside", meta = (ClampMin = "0.0"))
	float ReflectorSpacingM = 25.f;

	/** Reflector post height above the barrier top, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roadside", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float ReflectorHeightM = 0.25f;

	/** Reflector post radius, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roadside", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float ReflectorRadiusM = 0.05f;

	/** Asphalt color (linear). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor AsphaltColor = FLinearColor(0.035f, 0.035f, 0.038f, 1.f);

	/** White paint color (linear). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor WhitePaintColor = FLinearColor(0.8f, 0.8f, 0.78f, 1.f);

	/** Yellow paint color (linear). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor YellowPaintColor = FLinearColor(0.85f, 0.55f, 0.05f, 1.f);

	/** Concrete color (barriers), linear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor ConcreteColor = FLinearColor(0.45f, 0.44f, 0.42f, 1.f);

	/** Metal color (lamp posts), linear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor MetalColor = FLinearColor(0.25f, 0.26f, 0.27f, 1.f);
};

/**
 * Complete description of a procedural route: profile, generator, streaming, dressing and environment.
 * Data/World/Route_*.json (type "RouteDefinition"). Selected by ULRWorldSettings.
 */
USTRUCT(BlueprintType)
struct LRWORLD_API FLRRouteDefinition
{
	GENERATED_BODY()

	/** Cross-section of the road. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FLRRoadProfile Profile;

	/** Procedural layout parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FLRRoadGenParams GenParams;

	/** Posted speed limit, km/h (constant along the route for now). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "30.0", ClampMax = "300.0"))
	float SpeedLimitKmh = 130.f;

	/** Road coordinate S where the player starts each run, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0.0"))
	double SpawnS = 400.0;

	/** Environment preset file relative to Data/ (e.g. "World/Environment_Day.json"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FString EnvironmentPresetFile = TEXT("World/Environment_Day.json");

	/** Global lane index (0 = leftmost) where the player starts. Must be a player-direction lane. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0"))
	int32 DefaultPlayerLane = 1;

	/** World location of S = 0 on the centerline, centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FVector OriginCm = FVector::ZeroVector;

	/** Heading of the route at S = 0, degrees (Unreal yaw). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	float StartYawDeg = 0.f;

	/** Streaming distances. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FLRRoadStreamingParams Streaming;

	/** Greybox dressing (markings, barriers, roadside objects, colors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FLRRoadDressing Dressing;
};
