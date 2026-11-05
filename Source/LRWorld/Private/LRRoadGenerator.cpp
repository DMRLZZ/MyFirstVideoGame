// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRoadGenerator.h"

#include "LRRoadPath.h"

void FLRRoadGenerator::Reset()
{
	LastTurnSign = 0;
	bLastWasCurve = true;
}

FLRRoadGenParams FLRRoadGenerator::Sanitize(const FLRRoadGenParams& Params)
{
	FLRRoadGenParams Out = Params;

	Out.InitialStraightLengthM = FMath::Max(0.f, Out.InitialStraightLengthM);

	Out.MinStraightLengthM = FMath::Max(10.f, Out.MinStraightLengthM);
	Out.MaxStraightLengthM = FMath::Max(Out.MinStraightLengthM, Out.MaxStraightLengthM);

	Out.MinArcLengthM = FMath::Max(10.f, Out.MinArcLengthM);
	Out.MaxArcLengthM = FMath::Max(Out.MinArcLengthM, Out.MaxArcLengthM);

	Out.MinRadiusM = FMath::Max(HardMinRadiusM, Out.MinRadiusM);
	Out.MaxRadiusM = FMath::Max(Out.MinRadiusM, Out.MaxRadiusM);

	Out.CurveProbability = FMath::Clamp(Out.CurveProbability, 0.f, 1.f);
	Out.TurnReversalProbability = FMath::Clamp(Out.TurnReversalProbability, 0.f, 1.f);
	Out.MaxArcHeadingChangeDeg = FMath::Clamp(Out.MaxArcHeadingChangeDeg, 1.f, 90.f);

	Out.TransitionLengthM = FMath::Max(0.f, Out.TransitionLengthM);
	Out.TransitionSteps = FMath::Clamp(Out.TransitionSteps, 1, 16);

	Out.MaxGrade = FMath::Clamp(Out.MaxGrade, 0.f, 0.08f);
	Out.GradeChangeProbability = FMath::Clamp(Out.GradeChangeProbability, 0.f, 1.f);
	Out.MinVerticalCurveRadiusM = FMath::Max(500.f, Out.MinVerticalCurveRadiusM);
	return Out;
}

double FLRRoadGenerator::PickTargetGrade(double CurrentGrade, double PieceLength, FRandomStream& Stream, const FLRRoadGenParams& Params)
{
	// Always draw both numbers so the stream advances identically whatever the outcome.
	const double ChangeRoll = Stream.FRand();
	const double CandidateGrade = Stream.FRandRange(-Params.MaxGrade, Params.MaxGrade);

	if (ChangeRoll >= Params.GradeChangeProbability)
	{
		return FMath::Clamp(CurrentGrade, -static_cast<double>(Params.MaxGrade), static_cast<double>(Params.MaxGrade));
	}

	// Limit the grade change by the minimum vertical curve radius: |dG| <= Length / Rv.
	const double MaxDelta = PieceLength / static_cast<double>(Params.MinVerticalCurveRadiusM);
	return FMath::Clamp(CandidateGrade, CurrentGrade - MaxDelta, CurrentGrade + MaxDelta);
}

void FLRRoadGenerator::AppendStraightPiece(FLRRoadPath& Path, double Length, FRandomStream& Stream, const FLRRoadGenParams& Params)
{
	const double StartGrade = Path.GetEndGrade();
	const double EndGrade = PickTargetGrade(StartGrade, Length, Stream, Params);
	Path.AppendSegment(Length, 0.0, StartGrade, EndGrade);
	bLastWasCurve = false;
}

void FLRRoadGenerator::AppendCurvePiece(FLRRoadPath& Path, FRandomStream& Stream, const FLRRoadGenParams& Params)
{
	// Fixed number of draws per curve: reversal roll, side roll, radius, arc length (+ grade inside PickTargetGrade).
	const double ReversalRoll = Stream.FRand();
	const double SideRoll = Stream.FRand();
	const double Radius = Stream.FRandRange(Params.MinRadiusM, Params.MaxRadiusM);
	double ArcLength = Stream.FRandRange(Params.MinArcLengthM, Params.MaxArcLengthM);

	int32 Sign = SideRoll < 0.5 ? -1 : 1;
	if (LastTurnSign != 0 && ReversalRoll < Params.TurnReversalProbability)
	{
		Sign = -LastTurnSign;
	}

	const double SafeRadius = FMath::Max(static_cast<double>(HardMinRadiusM), Radius);
	ArcLength = FMath::Min(ArcLength, FMath::DegreesToRadians(static_cast<double>(Params.MaxArcHeadingChangeDeg)) * SafeRadius);

	const double Curvature = static_cast<double>(Sign) / SafeRadius;
	const int32 Steps = Params.TransitionLengthM > 0.f ? Params.TransitionSteps : 0;
	const double StepLength = Steps > 0 ? static_cast<double>(Params.TransitionLengthM) / static_cast<double>(Steps) : 0.0;
	const double TotalLength = ArcLength + 2.0 * StepLength * static_cast<double>(Steps);

	const double StartGrade = Path.GetEndGrade();
	const double EndGrade = PickTargetGrade(StartGrade, TotalLength, Stream, Params);

	// Distribute the grade change linearly along the whole curve so the slope stays continuous between pieces.
	double Covered = 0.0;
	auto AppendPart = [&](double Length, double PartCurvature)
	{
		const double G0 = FMath::Lerp(StartGrade, EndGrade, Covered / TotalLength);
		Covered += Length;
		const double G1 = FMath::Lerp(StartGrade, EndGrade, Covered / TotalLength);
		Path.AppendSegment(Length, PartCurvature, G0, G1);
	};

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		AppendPart(StepLength, Curvature * (static_cast<double>(Step) + 0.5) / static_cast<double>(Steps));
	}
	AppendPart(ArcLength, Curvature);
	for (int32 Step = Steps - 1; Step >= 0; --Step)
	{
		AppendPart(StepLength, Curvature * (static_cast<double>(Step) + 0.5) / static_cast<double>(Steps));
	}

	LastTurnSign = Sign;
	bLastWasCurve = true;
}

void FLRRoadGenerator::GenerateUntil(FLRRoadPath& Path, double TargetEndS, FRandomStream& Stream, const FLRRoadGenParams& InParams)
{
	const FLRRoadGenParams Params = Sanitize(InParams);

	if (Path.IsEmpty() && Params.InitialStraightLengthM > 0.f)
	{
		Path.AppendSegment(Params.InitialStraightLengthM, 0.0, 0.0, 0.0);
		bLastWasCurve = false;
	}

	while (Path.GetEndS() < TargetEndS)
	{
		if (bLastWasCurve)
		{
			AppendStraightPiece(Path, Stream.FRandRange(Params.MinStraightLengthM, Params.MaxStraightLengthM), Stream, Params);
			continue;
		}

		const double CurveRoll = Stream.FRand();
		if (CurveRoll < Params.CurveProbability)
		{
			AppendCurvePiece(Path, Stream, Params);
		}
		else
		{
			AppendStraightPiece(Path, Stream.FRandRange(Params.MinStraightLengthM, Params.MaxStraightLengthM), Stream, Params);
		}
	}
}
