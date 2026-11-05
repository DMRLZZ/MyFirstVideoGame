// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"
#include "LRRoadTypes.h"

/**
 * One analytic piece of the route centerline: a straight (Curvature ~ 0) or a constant-curvature arc,
 * with a grade that varies linearly from StartGrade to EndGrade (parabolic vertical curve).
 *
 * All values are SI (meters, radians) in world axes: X/Y/Z are the Unreal world axes divided by 100.
 * Heading is the Unreal yaw (from +X toward +Y); Curvature > 0 turns right (heading increases).
 */
struct FLRRoadSegment
{
	/** Route distance at the start of the segment, meters. */
	double StartS = 0.0;

	/** Horizontal length of the segment, meters. */
	double Length = 0.0;

	/** Signed curvature, 1/m (+ = turns right). Values below FLRRoadPath::MinArcCurvature mean a straight. */
	double Curvature = 0.0;

	/** Grade (rise / run) at the start of the segment. */
	double StartGrade = 0.0;

	/** Grade (rise / run) at the end of the segment. */
	double EndGrade = 0.0;

	/** World position of the start point, meters. */
	double StartX = 0.0;
	double StartY = 0.0;
	double StartZ = 0.0;

	/** Heading at the start point, radians (Unreal yaw). */
	double StartHeading = 0.0;

	/** Route distance at the end of the segment, meters. */
	double GetEndS() const { return StartS + Length; }
};

/**
 * Pure, analytic model of an (endless) road: a chain of straight / arc segments plus the lane layout.
 *
 * - S is the distance along the centerline in meters (double); D is the lateral offset in meters (+ = right).
 * - World positions cross the API in centimeters (Unreal units); everything inside is meters.
 * - Position, heading and height are continuous at every joint (each segment starts where the previous ends).
 * - Arcs are split so that no piece turns more than 90 degrees, which keeps WorldToRoad unambiguous.
 *
 * No UObject, no world: owned by ULRRoadSubsystem, testable in isolation. Not thread-safe for concurrent writes.
 */
class LRWORLD_API FLRRoadPath
{
public:
	/** Curvatures with a smaller magnitude are treated as straights, 1/m. */
	static constexpr double MinArcCurvature = 1e-8;

	/** Maximum heading change of a single stored arc piece, radians (longer arcs are split). */
	static constexpr double MaxArcAngle = UE_DOUBLE_HALF_PI;

	/** Clears every segment, stores the profile (and rebuilds lanes) and sets the start pose of S = 0. */
	void Reset(const FLRRoadProfile& InProfile, const FVector& StartLocationCm, float StartYawDeg);

	/** Appends a straight of LengthM meters with a constant grade. */
	void AppendStraight(double LengthM, double Grade = 0.0);

	/** Appends a constant-curvature arc (1/m, + = right) with a constant grade. Near-zero curvature appends a straight. */
	void AppendArc(double LengthM, double CurvaturePerM, double Grade = 0.0);

	/**
	 * Appends a segment whose grade changes linearly from StartGrade to EndGrade (smooth vertical curve).
	 * For slope continuity pass StartGrade == GetEndGrade(). Arcs turning more than MaxArcAngle are split.
	 */
	void AppendSegment(double LengthM, double CurvaturePerM, double StartGrade, double EndGrade);

	/** First valid S, meters (grows when TrimBefore removes segments). */
	double GetStartS() const;

	/** Last valid S, meters. */
	double GetEndS() const;

	/** Removes whole segments that end at or before S (always keeps at least one). Segment hints stay valid. */
	void TrimBefore(double S);

	/**
	 * Converts road coordinates to a world transform: location in cm on the road surface plane (no banking),
	 * rotation yaw = road heading, pitch = atan(grade). S outside the path is extrapolated from the end segments.
	 */
	FTransform RoadToWorld(double S, float D) const;

	/** Location part of RoadToWorld, centimeters. */
	FVector RoadToWorldLocation(double S, float D) const;

	/**
	 * Centerline point (cm) and unit frame at S: Forward follows heading and grade, Right is horizontal, Up = road normal.
	 */
	void GetFrame(double S, FVector& OutCenterCm, FVector& OutForward, FVector& OutRight, FVector& OutUp) const;

	/**
	 * Projects a world location (cm) onto the road (height is ignored).
	 * @param InOutSegmentHint  Optional opaque cache (keep one per querying entity, start with INDEX_NONE).
	 *                          Amortized O(1) when the hint is valid; it survives TrimBefore.
	 * @return false when the path is empty or the projection falls outside [GetStartS(), GetEndS()]
	 *         (Out is still filled with the extrapolated coordinate in the latter case).
	 */
	bool WorldToRoad(const FVector& LocationCm, FLRRoadCoord& Out, int32* InOutSegmentHint = nullptr) const;

	/** Road heading at S, degrees in [-180, 180] (Unreal yaw). */
	float GetHeadingYawDegrees(double S) const;

	/** Signed curvature at S, 1/m (+ = right). */
	float GetCurvature(double S) const;

	/** Grade (rise / run) at S. */
	float GetGrade(double S) const;

	/** Grade at the end of the path (start grade of the next appended segment for slope continuity). */
	double GetEndGrade() const;

	/** Lanes of the profile, sorted left to right. */
	const TArray<FLRLaneDesc>& GetLanes() const { return Lanes; }

	/** Index of the lane nearest to D, or INDEX_NONE if there are no lanes. */
	int32 FindNearestLane(float D) const;

	/** Profile given to Reset. */
	const FLRRoadProfile& GetProfile() const { return Profile; }

	/** True when no segment has been appended since Reset. */
	bool IsEmpty() const { return Segments.Num() == 0; }

	/** Number of stored segments. */
	int32 GetNumSegments() const { return Segments.Num(); }

	/** Stored segments (front = oldest). */
	const TArray<FLRRoadSegment>& GetSegments() const { return Segments; }

	/** Number of segments removed by TrimBefore since Reset (hint = NumTrimmed + index). */
	int32 GetNumTrimmedSegments() const { return NumTrimmed; }

	/** Index of the stored segment containing S (clamped to the first / last segment), INDEX_NONE when empty. */
	int32 FindSegmentIndex(double S) const;

	/**
	 * Evaluates a segment at LocalS meters from its start (may be outside [0, Length]: extrapolates).
	 * Outputs world meters, heading in radians and grade.
	 */
	static void EvaluateSegment(const FLRRoadSegment& Segment, double LocalS, double& OutX, double& OutY, double& OutZ, double& OutHeading, double& OutGrade);

	/** Projects a horizontal world point (meters) on a segment. Returns LocalS (unclamped) and D (+ = right). */
	static void ProjectOntoSegment(const FLRRoadSegment& Segment, double X, double Y, double& OutLocalS, double& OutD);

private:
	/** Evaluates the centerline at S (clamped segment, extrapolated position). */
	void EvaluateAt(double S, double& OutX, double& OutY, double& OutZ, double& OutHeading, double& OutGrade) const;

	/** Initial guess of the segment containing a world point when no hint is available. */
	int32 GuessSegmentByBinarySearch(double X, double Y) const;

	/** Walks from StartIndex to the neighbor segment containing the projection. Returns false if it did not converge. */
	bool WalkToSegment(double X, double Y, int32& InOutIndex, double& OutLocalS, double& OutD) const;

	/** Appends one piece without splitting. */
	void AppendPiece(double LengthM, double CurvaturePerM, double StartGrade, double EndGrade);

	FLRRoadProfile Profile;
	TArray<FLRLaneDesc> Lanes;
	TArray<FLRRoadSegment> Segments;

	/** Start pose used while the path is empty (meters / radians). */
	double OriginX = 0.0;
	double OriginY = 0.0;
	double OriginZ = 0.0;
	double OriginHeading = 0.0;

	/** Number of segments trimmed since Reset. */
	int32 NumTrimmed = 0;
};
