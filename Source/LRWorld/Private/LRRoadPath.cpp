// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRoadPath.h"

#include "LRRoadLayout.h"
#include "LRUnits.h"

namespace LRRoadPathPrivate
{
	/** Tolerance used to decide whether a projection lies inside a segment, meters. */
	constexpr double SegmentTolerance = 1e-6;

	/** Tolerance of the final [StartS, EndS] validity check, meters. */
	constexpr double RangeTolerance = 1e-3;

	/** Maximum neighbor steps of a hinted walk before falling back to a global search. */
	constexpr int32 MaxWalkSteps = 32;
}

void FLRRoadPath::Reset(const FLRRoadProfile& InProfile, const FVector& StartLocationCm, float StartYawDeg)
{
	Profile = InProfile;
	LRRoadLayout::BuildLanes(Profile, Lanes);

	Segments.Reset();
	NumTrimmed = 0;

	OriginX = LRUnits::CmToMeters(StartLocationCm.X);
	OriginY = LRUnits::CmToMeters(StartLocationCm.Y);
	OriginZ = LRUnits::CmToMeters(StartLocationCm.Z);
	OriginHeading = FMath::UnwindRadians(FMath::DegreesToRadians(static_cast<double>(StartYawDeg)));
}

void FLRRoadPath::AppendStraight(double LengthM, double Grade)
{
	AppendSegment(LengthM, 0.0, Grade, Grade);
}

void FLRRoadPath::AppendArc(double LengthM, double CurvaturePerM, double Grade)
{
	AppendSegment(LengthM, CurvaturePerM, Grade, Grade);
}

void FLRRoadPath::AppendSegment(double LengthM, double CurvaturePerM, double StartGrade, double EndGrade)
{
	if (!(LengthM > 0.0))
	{
		return;
	}

	if (FMath::Abs(CurvaturePerM) < MinArcCurvature)
	{
		AppendPiece(LengthM, 0.0, StartGrade, EndGrade);
		return;
	}

	// Split long arcs so a single piece never turns more than MaxArcAngle (keeps the arc projection unambiguous).
	const double TotalAngle = FMath::Abs(CurvaturePerM) * LengthM;
	const int32 NumPieces = FMath::Max(1, FMath::CeilToInt32(TotalAngle / MaxArcAngle - 1e-9));
	const double PieceLength = LengthM / static_cast<double>(NumPieces);

	for (int32 Piece = 0; Piece < NumPieces; ++Piece)
	{
		const double Alpha0 = static_cast<double>(Piece) / static_cast<double>(NumPieces);
		const double Alpha1 = static_cast<double>(Piece + 1) / static_cast<double>(NumPieces);
		AppendPiece(PieceLength, CurvaturePerM, FMath::Lerp(StartGrade, EndGrade, Alpha0), FMath::Lerp(StartGrade, EndGrade, Alpha1));
	}
}

void FLRRoadPath::AppendPiece(double LengthM, double CurvaturePerM, double StartGrade, double EndGrade)
{
	FLRRoadSegment NewSegment;
	NewSegment.Length = LengthM;
	NewSegment.Curvature = FMath::Abs(CurvaturePerM) < MinArcCurvature ? 0.0 : CurvaturePerM;
	NewSegment.StartGrade = StartGrade;
	NewSegment.EndGrade = EndGrade;

	if (Segments.Num() == 0)
	{
		NewSegment.StartS = 0.0;
		NewSegment.StartX = OriginX;
		NewSegment.StartY = OriginY;
		NewSegment.StartZ = OriginZ;
		NewSegment.StartHeading = OriginHeading;
	}
	else
	{
		// Continuity: the new segment starts exactly where the previous one ends.
		const FLRRoadSegment& Last = Segments.Last();
		double Grade = 0.0;
		double Heading = 0.0;
		EvaluateSegment(Last, Last.Length, NewSegment.StartX, NewSegment.StartY, NewSegment.StartZ, Heading, Grade);
		NewSegment.StartS = Last.GetEndS();
		NewSegment.StartHeading = FMath::UnwindRadians(Heading);
	}

	Segments.Add(NewSegment);
}

double FLRRoadPath::GetStartS() const
{
	return Segments.Num() > 0 ? Segments[0].StartS : 0.0;
}

double FLRRoadPath::GetEndS() const
{
	return Segments.Num() > 0 ? Segments.Last().GetEndS() : 0.0;
}

double FLRRoadPath::GetEndGrade() const
{
	return Segments.Num() > 0 ? Segments.Last().EndGrade : 0.0;
}

void FLRRoadPath::TrimBefore(double S)
{
	int32 Count = 0;
	const int32 MaxRemovable = Segments.Num() - 1;
	while (Count < MaxRemovable && Segments[Count].GetEndS() <= S)
	{
		++Count;
	}

	if (Count > 0)
	{
		Segments.RemoveAt(0, Count, EAllowShrinking::No);
		NumTrimmed += Count;
	}
}

int32 FLRRoadPath::FindSegmentIndex(double S) const
{
	const int32 Num = Segments.Num();
	if (Num == 0)
	{
		return INDEX_NONE;
	}

	// Last segment whose StartS <= S, clamped to [0, Num - 1].
	int32 Lo = 0;
	int32 Hi = Num - 1;
	int32 Result = 0;
	while (Lo <= Hi)
	{
		const int32 Mid = Lo + (Hi - Lo) / 2;
		if (Segments[Mid].StartS <= S)
		{
			Result = Mid;
			Lo = Mid + 1;
		}
		else
		{
			Hi = Mid - 1;
		}
	}
	return Result;
}

void FLRRoadPath::EvaluateSegment(const FLRRoadSegment& Segment, double LocalS, double& OutX, double& OutY, double& OutZ, double& OutHeading, double& OutGrade)
{
	const double K = Segment.Curvature;
	const double Psi0 = Segment.StartHeading;

	if (FMath::Abs(K) < MinArcCurvature)
	{
		OutX = Segment.StartX + FMath::Cos(Psi0) * LocalS;
		OutY = Segment.StartY + FMath::Sin(Psi0) * LocalS;
		OutHeading = Psi0;
	}
	else
	{
		// Chord form of x = x0 + (sin psi - sin psi0) / k, y = y0 - (cos psi - cos psi0) / k (numerically stable for small k*s).
		const double HalfAngle = 0.5 * K * LocalS;
		const double Chord = 2.0 * FMath::Sin(HalfAngle) / K;
		OutX = Segment.StartX + Chord * FMath::Cos(Psi0 + HalfAngle);
		OutY = Segment.StartY + Chord * FMath::Sin(Psi0 + HalfAngle);
		OutHeading = Psi0 + K * LocalS;
	}

	const double GradeRate = Segment.Length > 0.0 ? (Segment.EndGrade - Segment.StartGrade) / Segment.Length : 0.0;
	OutZ = Segment.StartZ + Segment.StartGrade * LocalS + 0.5 * GradeRate * LocalS * LocalS;
	OutGrade = Segment.StartGrade + GradeRate * LocalS;
}

void FLRRoadPath::ProjectOntoSegment(const FLRRoadSegment& Segment, double X, double Y, double& OutLocalS, double& OutD)
{
	const double K = Segment.Curvature;
	const double SinPsi0 = FMath::Sin(Segment.StartHeading);
	const double CosPsi0 = FMath::Cos(Segment.StartHeading);

	if (FMath::Abs(K) < MinArcCurvature)
	{
		const double Vx = X - Segment.StartX;
		const double Vy = Y - Segment.StartY;
		OutLocalS = Vx * CosPsi0 + Vy * SinPsi0;
		OutD = -Vx * SinPsi0 + Vy * CosPsi0;
		return;
	}

	// Center = p0 + Right0 / k, with Right0 = (-sin psi0, cos psi0).
	const double InvK = 1.0 / K;
	const double Cx = Segment.StartX - SinPsi0 * InvK;
	const double Cy = Segment.StartY + CosPsi0 * InvK;

	// Radial vector of the start point and of the query point.
	const double R0x = Segment.StartX - Cx;
	const double R0y = Segment.StartY - Cy;
	const double Vx = X - Cx;
	const double Vy = Y - Cy;

	const double Cross = R0x * Vy - R0y * Vx;
	const double Dot = R0x * Vx + R0y * Vy;
	const double Theta = FMath::Atan2(Cross, Dot);

	OutLocalS = Theta * InvK;

	const double Radius = FMath::Abs(InvK);
	const double Distance = FMath::Sqrt(Vx * Vx + Vy * Vy);
	OutD = (K > 0.0 ? 1.0 : -1.0) * (Radius - Distance);
}

void FLRRoadPath::EvaluateAt(double S, double& OutX, double& OutY, double& OutZ, double& OutHeading, double& OutGrade) const
{
	const int32 Index = FindSegmentIndex(S);
	if (Index == INDEX_NONE)
	{
		// Empty path: straight extrapolation from the origin pose.
		OutX = OriginX + FMath::Cos(OriginHeading) * S;
		OutY = OriginY + FMath::Sin(OriginHeading) * S;
		OutZ = OriginZ;
		OutHeading = OriginHeading;
		OutGrade = 0.0;
		return;
	}

	const FLRRoadSegment& Segment = Segments[Index];
	EvaluateSegment(Segment, S - Segment.StartS, OutX, OutY, OutZ, OutHeading, OutGrade);
}

FTransform FLRRoadPath::RoadToWorld(double S, float D) const
{
	double X, Y, Z, Heading, Grade;
	EvaluateAt(S, X, Y, Z, Heading, Grade);

	const double SinPsi = FMath::Sin(Heading);
	const double CosPsi = FMath::Cos(Heading);
	const FVector Location(
		LRUnits::MetersToCm(X - SinPsi * D),
		LRUnits::MetersToCm(Y + CosPsi * D),
		LRUnits::MetersToCm(Z));

	const FRotator Rotation(FMath::RadiansToDegrees(FMath::Atan(Grade)), FMath::RadiansToDegrees(Heading), 0.0);
	return FTransform(Rotation, Location);
}

FVector FLRRoadPath::RoadToWorldLocation(double S, float D) const
{
	double X, Y, Z, Heading, Grade;
	EvaluateAt(S, X, Y, Z, Heading, Grade);

	const double SinPsi = FMath::Sin(Heading);
	const double CosPsi = FMath::Cos(Heading);
	return FVector(
		LRUnits::MetersToCm(X - SinPsi * D),
		LRUnits::MetersToCm(Y + CosPsi * D),
		LRUnits::MetersToCm(Z));
}

void FLRRoadPath::GetFrame(double S, FVector& OutCenterCm, FVector& OutForward, FVector& OutRight, FVector& OutUp) const
{
	double X, Y, Z, Heading, Grade;
	EvaluateAt(S, X, Y, Z, Heading, Grade);

	const double SinPsi = FMath::Sin(Heading);
	const double CosPsi = FMath::Cos(Heading);
	const double Pitch = FMath::Atan(Grade);
	const double SinPitch = FMath::Sin(Pitch);
	const double CosPitch = FMath::Cos(Pitch);

	OutCenterCm = FVector(LRUnits::MetersToCm(X), LRUnits::MetersToCm(Y), LRUnits::MetersToCm(Z));
	OutForward = FVector(CosPitch * CosPsi, CosPitch * SinPsi, SinPitch);
	OutRight = FVector(-SinPsi, CosPsi, 0.0);
	OutUp = FVector(-SinPitch * CosPsi, -SinPitch * SinPsi, CosPitch);
}

int32 FLRRoadPath::GuessSegmentByBinarySearch(double X, double Y) const
{
	// Along-track distance of the point from each segment start decreases with the index on a road without loops:
	// find the last segment whose start lies behind the point.
	int32 Lo = 0;
	int32 Hi = Segments.Num() - 1;
	int32 Result = 0;
	while (Lo <= Hi)
	{
		const int32 Mid = Lo + (Hi - Lo) / 2;
		const FLRRoadSegment& Segment = Segments[Mid];
		const double AlongTrack = (X - Segment.StartX) * FMath::Cos(Segment.StartHeading) + (Y - Segment.StartY) * FMath::Sin(Segment.StartHeading);
		if (AlongTrack >= 0.0)
		{
			Result = Mid;
			Lo = Mid + 1;
		}
		else
		{
			Hi = Mid - 1;
		}
	}
	return Result;
}

bool FLRRoadPath::WalkToSegment(double X, double Y, int32& InOutIndex, double& OutLocalS, double& OutD) const
{
	using namespace LRRoadPathPrivate;

	const int32 Num = Segments.Num();
	int32 PreviousDirection = 0;

	for (int32 Step = 0; Step < MaxWalkSteps; ++Step)
	{
		const FLRRoadSegment& Segment = Segments[InOutIndex];
		ProjectOntoSegment(Segment, X, Y, OutLocalS, OutD);

		if (OutLocalS < -SegmentTolerance && InOutIndex > 0)
		{
			if (PreviousDirection > 0)
			{
				// Oscillating between two segments: the point sits in the gap of a joint, accept this one.
				return true;
			}
			--InOutIndex;
			PreviousDirection = -1;
			continue;
		}

		if (OutLocalS > Segment.Length + SegmentTolerance && InOutIndex < Num - 1)
		{
			if (PreviousDirection < 0)
			{
				return true;
			}
			++InOutIndex;
			PreviousDirection = 1;
			continue;
		}

		return true;
	}

	return false;
}

bool FLRRoadPath::WorldToRoad(const FVector& LocationCm, FLRRoadCoord& Out, int32* InOutSegmentHint) const
{
	using namespace LRRoadPathPrivate;

	const int32 Num = Segments.Num();
	if (Num == 0)
	{
		return false;
	}

	const double X = LRUnits::CmToMeters(LocationCm.X);
	const double Y = LRUnits::CmToMeters(LocationCm.Y);

	int32 Index = INDEX_NONE;
	if (InOutSegmentHint && *InOutSegmentHint != INDEX_NONE)
	{
		Index = *InOutSegmentHint - NumTrimmed;
		if (!Segments.IsValidIndex(Index))
		{
			Index = INDEX_NONE;
		}
	}

	double LocalS = 0.0;
	double D = 0.0;
	bool bConverged = false;

	if (Index != INDEX_NONE)
	{
		bConverged = WalkToSegment(X, Y, Index, LocalS, D);
	}

	if (!bConverged)
	{
		Index = GuessSegmentByBinarySearch(X, Y);
		bConverged = WalkToSegment(X, Y, Index, LocalS, D);
	}

	if (!bConverged)
	{
		// Last resort (should not happen on loop-free roads): nearest valid projection over every segment.
		double BestAbsD = TNumericLimits<double>::Max();
		for (int32 Candidate = 0; Candidate < Num; ++Candidate)
		{
			double CandidateS = 0.0;
			double CandidateD = 0.0;
			ProjectOntoSegment(Segments[Candidate], X, Y, CandidateS, CandidateD);
			const bool bInside = CandidateS >= -SegmentTolerance && CandidateS <= Segments[Candidate].Length + SegmentTolerance;
			if (bInside && FMath::Abs(CandidateD) < BestAbsD)
			{
				BestAbsD = FMath::Abs(CandidateD);
				Index = Candidate;
				LocalS = CandidateS;
				D = CandidateD;
			}
		}
	}

	Out.S = Segments[Index].StartS + LocalS;
	Out.D = static_cast<float>(D);

	if (InOutSegmentHint)
	{
		*InOutSegmentHint = NumTrimmed + Index;
	}

	return Out.S >= GetStartS() - RangeTolerance && Out.S <= GetEndS() + RangeTolerance;
}

float FLRRoadPath::GetHeadingYawDegrees(double S) const
{
	double X, Y, Z, Heading, Grade;
	EvaluateAt(S, X, Y, Z, Heading, Grade);
	return static_cast<float>(FRotator::NormalizeAxis(FMath::RadiansToDegrees(Heading)));
}

float FLRRoadPath::GetCurvature(double S) const
{
	const int32 Index = FindSegmentIndex(S);
	return Index == INDEX_NONE ? 0.f : static_cast<float>(Segments[Index].Curvature);
}

float FLRRoadPath::GetGrade(double S) const
{
	double X, Y, Z, Heading, Grade;
	EvaluateAt(S, X, Y, Z, Heading, Grade);
	return static_cast<float>(Grade);
}

int32 FLRRoadPath::FindNearestLane(float D) const
{
	return LRRoadLayout::FindNearestLane(Lanes, D);
}
