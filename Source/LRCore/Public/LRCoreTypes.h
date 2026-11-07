// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LRCoreTypes.generated.h"

/**
 * Position on the highway expressed in road coordinates.
 *
 * - S: distance along the route centerline in METERS (double, grows in the player's direction of travel).
 * - D: lateral offset from the route centerline in METERS, positive to the RIGHT of the travel direction.
 *
 * Every gameplay system (traffic, scoring, audio) reasons in (S, D); only rendering and physics use world space.
 */
USTRUCT(BlueprintType)
struct LRCORE_API FLRRoadCoord
{
	GENERATED_BODY()

	/** Distance along the route, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	double S = 0.0;

	/** Lateral offset from the centerline, meters (+ = right). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	float D = 0.f;

	FLRRoadCoord() = default;
	FLRRoadCoord(double InS, float InD) : S(InS), D(InD) {}
};

/** Lifecycle of a single run (see docs/02-ARCHITECTURE.md §5.4). */
UENUM(BlueprintType)
enum class ELRRunState : uint8
{
	Garage,
	Loading,
	Countdown,
	Running,
	Crashed,
	Replay,
	Results
};

/** Near-miss quality, decided by the minimum lateral clearance during a pass. */
UENUM(BlueprintType)
enum class ELRNearMissTier : uint8
{
	None,
	/** Clearance below the "close" threshold (default 1.0 m). */
	Close,
	/** Clearance below the "very close" threshold (default 0.5 m). */
	VeryClose,
	/** Clearance below the "razor" threshold (default 0.25 m). */
	Razor
};

/** Player camera modes (implemented by LRCamera). */
UENUM(BlueprintType)
enum class ELRCameraMode : uint8
{
	ChaseFar,
	ChaseNear,
	Hood,
	Bumper,
	Cockpit,
	/** GoPro / dashcam / phone emulation (docs/03-RENDERING.md §6). */
	Viral
};

/** Steering scheme selected by the player (docs/04-GAMEPLAY.md §2). */
UENUM(BlueprintType)
enum class ELRControlScheme : uint8
{
	/** Discrete lane changes driven by the lane-keeping assist; real physics underneath. */
	Arcade,
	/** Full analog steering with configurable stability aids. */
	Simcade,
	/** Default: the car moves sideways while steering is held and goes straight on release. No lane steps, no walls. */
	Smooth
};

/** Game modes (docs/04-GAMEPLAY.md §6). */
UENUM(BlueprintType)
enum class ELRGameModeType : uint8
{
	EndlessOneWay,
	EndlessTwoWay,
	TimeAttack,
	FreeRide,
	Daily
};
