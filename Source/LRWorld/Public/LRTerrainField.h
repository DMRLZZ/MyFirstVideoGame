// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRRoadTypes.h"

/**
 * Height of the land around the road, as a pure function (no UObject, no state).
 *
 * The land follows the road elevation at the same S: a shallow ditch right behind the barrier, a flat verge, then
 * hills that grow with the distance. Hills come from noise in world space, so two chunks always agree on their
 * shared border and the same place looks the same when it is streamed again.
 */
namespace LRTerrainField
{
	/**
	 * Height over the road surface, meters.
	 * @param WorldXM, WorldYM  Position in world space, meters.
	 * @param DistanceM         Distance from the barrier, meters (>= 0).
	 */
	LRWORLD_API float GetHeightM(const FLRTerrainParams& Params, double WorldXM, double WorldYM, float DistanceM);

	/** Columns of the terrain mesh as distances from the barrier, meters, ascending, from 0 to Params.MaxDistanceM. */
	LRWORLD_API void BuildColumnDistances(const FLRTerrainParams& Params, TArray<float>& OutDistancesM);

	/** Height of a distant ridge over the road, meters. Periodic in the angle, so a full ring closes without a seam. */
	LRWORLD_API float GetRidgeHeightM(float AngleRad, float BaseHeightM, float AmplitudeM, int32 Seed);
}
