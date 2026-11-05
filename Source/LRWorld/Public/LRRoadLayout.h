// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRRoadTypes.h"

/** Paint color of a lane marking. */
enum class ELRLaneMarkingColor : uint8
{
	White,
	Yellow
};

/** One longitudinal paint line of the road cross-section (pure data, no UObject). */
struct FLRLaneMarking
{
	/** Lateral position of the line center, meters (+ = right). */
	float CenterD = 0.f;

	/** Line width, meters. */
	float WidthMeters = 0.15f;

	/** Dashed (lane separator) or solid (edge / center line). */
	bool bDashed = false;

	/** Paint color. */
	ELRLaneMarkingColor Color = ELRLaneMarkingColor::White;
};

/**
 * Lateral layout of the road surface and barriers, in road coordinates (meters, + = right).
 * Computed from a profile and the dressing by LRRoadLayout::ComputeCrossSection.
 */
struct FLRRoadCrossSection
{
	/** Left edge of the leftmost lane. */
	float LanesLeftD = 0.f;

	/** Right edge of the rightmost lane. */
	float LanesRightD = 0.f;

	/** Outer edge of the left shoulder. */
	float ShoulderLeftD = 0.f;

	/** Outer edge of the right shoulder. */
	float ShoulderRightD = 0.f;

	/** Center line of the left barrier (also the left limit of the paved surface). */
	float LeftBarrierD = 0.f;

	/** Center line of the right barrier (also the right limit of the paved surface). */
	float RightBarrierD = 0.f;

	/** True when the left barrier is a median barrier (one-way profile with a median). */
	bool bLeftBarrierIsMedian = false;
};

/**
 * Pure helpers that turn a FLRRoadProfile into lanes, a cross-section and paint markings.
 * Used by FLRRoadPath (lanes), ALRRoadStreamer (geometry) and the tests.
 */
namespace LRRoadLayout
{
	/**
	 * Builds the lane list of a profile, sorted left to right (CenterD ascending).
	 * One-way: N lanes centered on D = 0, all Direction +1.
	 * Two-way: N oncoming lanes (Direction -1, D < 0), then N player lanes (Direction +1, D > 0), separated by the median.
	 */
	LRWORLD_API void BuildLanes(const FLRRoadProfile& Profile, TArray<FLRLaneDesc>& OutLanes);

	/** Computes lane, shoulder and barrier lateral positions. */
	LRWORLD_API FLRRoadCrossSection ComputeCrossSection(const FLRRoadProfile& Profile, const FLRRoadDressing& Dressing);

	/**
	 * Builds the paint lines of a profile.
	 * One-way: yellow solid left edge, white solid right edge, white dashed separators.
	 * Two-way: white solid outer edges, double yellow solid center line, white dashed separators between same-direction lanes.
	 */
	LRWORLD_API void BuildMarkings(const FLRRoadProfile& Profile, const FLRRoadDressing& Dressing, TArray<FLRLaneMarking>& OutMarkings);

	/** Index of the lane whose center is nearest to D, or INDEX_NONE if Lanes is empty. */
	LRWORLD_API int32 FindNearestLane(TConstArrayView<FLRLaneDesc> Lanes, float D);
}
