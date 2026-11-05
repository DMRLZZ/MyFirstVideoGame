// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRRoadTypes.h"
#include "Math/RandomStream.h"

class FLRRoadPath;

/**
 * Deterministic procedural layout of an endless highway (pure, no UObject).
 *
 * Alternates straights and gentle curves: after a straight, a curve follows with probability CurveProbability
 * (otherwise another straight); a curve is always followed by a straight. Each curve is made of a stepped-curvature
 * entry easement, a constant-radius arc (radius in [MinRadiusM, MaxRadiusM]) and a mirrored exit easement.
 * Grade changes are smooth: each piece interpolates the grade linearly from the previous end grade to a new target,
 * limited by MinVerticalCurveRadiusM.
 *
 * Same seed + same params + same call sequence => identical segments. Owned by ULRRoadSubsystem (one per route build).
 */
struct LRWORLD_API FLRRoadGenerator
{
	/** Forgets the generator state (call together with FLRRoadPath::Reset). */
	void Reset();

	/**
	 * Appends pieces to Path until Path.GetEndS() >= TargetEndS. The first piece of an empty path is a straight of
	 * InitialStraightLengthM. Randomness only comes from Stream (derive it with LRRandom::MakeStream(Seed, RoadLayout)).
	 */
	void GenerateUntil(FLRRoadPath& Path, double TargetEndS, FRandomStream& Stream, const FLRRoadGenParams& Params);

	/** Returns a copy of Params with ordered ranges and hard safety limits applied (e.g. MinRadiusM >= 1000 m). */
	static FLRRoadGenParams Sanitize(const FLRRoadGenParams& Params);

	/** Hard lower bound of curve radii, meters (drivable at 300 km/h with lane assist). */
	static constexpr float HardMinRadiusM = 1000.f;

private:
	/** Appends one straight piece. */
	void AppendStraightPiece(FLRRoadPath& Path, double Length, FRandomStream& Stream, const FLRRoadGenParams& Params);

	/** Appends a complete curve (entry easement, arc, exit easement). */
	void AppendCurvePiece(FLRRoadPath& Path, FRandomStream& Stream, const FLRRoadGenParams& Params);

	/** Picks the end grade of the next piece (always consumes the same number of random draws). */
	static double PickTargetGrade(double CurrentGrade, double PieceLength, FRandomStream& Stream, const FLRRoadGenParams& Params);

	/** Sign of the last curve (-1 left, +1 right, 0 none yet). */
	int32 LastTurnSign = 0;

	/** True when the last appended piece was a curve (or nothing was generated yet, which forces a straight first). */
	bool bLastWasCurve = true;
};
