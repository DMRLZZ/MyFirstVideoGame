// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"

#include "LREvents.generated.h"

class AActor;

/**
 * Payloads of the gameplay event bus (ULRGameEventSubsystem).
 *
 * They are declared in LRCore so presentation systems (UI, audio, camera) can react to gameplay
 * without depending on the modules that produce the events (traffic, gameplay, vehicle).
 */

/** A traffic vehicle was passed without contact and with a small lateral clearance. Emitted by LRTraffic. */
USTRUCT(BlueprintType)
struct LRCORE_API FLRNearMissEvent
{
	GENERATED_BODY()

	/** Minimum lateral clearance between both footprints during the pass, meters. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float ClearanceMeters = 0.f;

	/** Absolute relative speed between player and traffic vehicle at the closest point, km/h. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float RelativeSpeedKmh = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	ELRNearMissTier Tier = ELRNearMissTier::None;

	/** The traffic vehicle drives in the opposite direction (two-way mode). */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	bool bOncoming = false;

	/** The player passed between two vehicles on both sides at the same time. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	bool bThreadTheNeedle = false;

	/** Archetype id of the traffic vehicle (e.g. "sedan", "truck"). */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	FName Archetype;

	/** Stable id of the traffic vehicle inside the traffic simulation. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 TrafficVehicleId = INDEX_NONE;

	/** Road position of the player when the pass completed. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	FLRRoadCoord PlayerCoord;
};

/** The player vehicle hit something. Emitted by LRVehicle. */
USTRUCT(BlueprintType)
struct LRCORE_API FLRCrashEvent
{
	GENERATED_BODY()

	/** Speed of the impact along the contact normal (approximate), km/h. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float ImpactSpeedKmh = 0.f;

	/** Player speed right before the impact, km/h. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float PlayerSpeedKmh = 0.f;

	/** World location of the contact. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	FVector Location = FVector::ZeroVector;

	/** What the player hit (traffic vehicle actor, barrier...). May be null. */
	UPROPERTY()
	TWeakObjectPtr<AActor> OtherActor;

	/** The impact is strong enough to end the run under the current rules (decided by the receiver of the event if false). */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	bool bSevere = false;
};

/** The player changed lane (center of the vehicle crossed into another lane). Emitted by LRVehicle. */
USTRUCT(BlueprintType)
struct LRCORE_API FLRLaneChangeEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 FromLane = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 ToLane = INDEX_NONE;

	/** The new lane carries oncoming traffic (two-way mode). */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	bool bIntoOncoming = false;
};

/** Run state transition. Emitted by LRGameplay through ULRRunContextSubsystem. */
USTRUCT(BlueprintType)
struct LRCORE_API FLRRunStateChangedEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	ELRRunState Previous = ELRRunState::Garage;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	ELRRunState Current = ELRRunState::Garage;
};

/**
 * A new run starts (first run or restart). Every world system must reset itself deterministically from Seed:
 * road layout, traffic, vehicle position, cameras, score. Emitted by LRGameplay.
 */
USTRUCT(BlueprintType)
struct LRCORE_API FLRRunResetEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 Seed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	ELRGameModeType Mode = ELRGameModeType::EndlessOneWay;
};

/** Score changed. Emitted by LRGameplay. */
USTRUCT(BlueprintType)
struct LRCORE_API FLRScoreChangedEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int64 Score = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int64 Delta = 0;

	/** Why the score changed: "Distance", "NearMiss", "Bonus"... */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	FName Reason;
};

/** Combo chain changed (grew, decayed or broke). Emitted by LRGameplay. */
USTRUCT(BlueprintType)
struct LRCORE_API FLRComboChangedEvent
{
	GENERATED_BODY()

	/** Number of chained near misses (0 = no combo). */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 Chain = 0;

	/** Current combo multiplier (>= 1). */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float Multiplier = 1.f;

	/** Seconds left before the combo expires. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float TimeRemaining = 0.f;

	/** The combo was broken by a scrape / slowdown (UI feedback). */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	bool bBroken = false;
};

/** The speed multiplier tier changed (docs/04-GAMEPLAY.md §5). Emitted by LRGameplay. */
USTRUCT(BlueprintType)
struct LRCORE_API FLRSpeedTierChangedEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 TierIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float Multiplier = 1.f;
};

/** The active camera mode changed. Emitted by LRCamera (audio uses bInterior for the cabin mix). */
USTRUCT(BlueprintType)
struct LRCORE_API FLRCameraModeChangedEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	ELRCameraMode Mode = ELRCameraMode::ChaseFar;

	/** The camera is inside the cabin (cockpit / dashcam). */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	bool bInterior = false;
};

/** Final statistics of a run, shown on the results screen. Emitted by LRGameplay when the run ends. */
USTRUCT(BlueprintType)
struct LRCORE_API FLRRunResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int64 FinalScore = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float DistanceMeters = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float DurationSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 NearMisses = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 BestCombo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	float TopSpeedKmh = 0.f;

	/** Credits earned by this run. */
	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int64 CreditsEarned = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Event")
	int32 Seed = 0;
};
