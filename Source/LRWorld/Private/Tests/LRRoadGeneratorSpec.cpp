// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRRandom.h"
#include "LRRoadGenerator.h"
#include "LRRoadPath.h"
#include "Misc/AutomationTest.h"

namespace LRRoadGeneratorSpecPrivate
{
	/** Builds a path of TargetLength meters from Seed with the given params. */
	void Generate(FLRRoadPath& OutPath, int32 Seed, const FLRRoadGenParams& Params, double TargetLength)
	{
		FLRRoadProfile Profile;
		OutPath.Reset(Profile, FVector::ZeroVector, 0.f);
		FLRRoadGenerator Generator;
		Generator.Reset();
		FRandomStream Stream = LRRandom::MakeStream(Seed, LRRandom::EStream::RoadLayout);
		Generator.GenerateUntil(OutPath, TargetLength, Stream, Params);
	}

	bool SegmentsIdentical(const FLRRoadSegment& A, const FLRRoadSegment& B)
	{
		return A.StartS == B.StartS && A.Length == B.Length && A.Curvature == B.Curvature
			&& A.StartGrade == B.StartGrade && A.EndGrade == B.EndGrade
			&& A.StartX == B.StartX && A.StartY == B.StartY && A.StartZ == B.StartZ && A.StartHeading == B.StartHeading;
	}
}

BEGIN_DEFINE_SPEC(FLRRoadGeneratorSpec, "LeftRight.World.RoadGenerator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRRoadGeneratorSpec)

void FLRRoadGeneratorSpec::Define()
{
	using namespace LRRoadGeneratorSpecPrivate;

	It("produces identical segments for the same seed", [this]()
	{
		FLRRoadGenParams Params;
		FLRRoadPath PathA;
		FLRRoadPath PathB;
		Generate(PathA, 1234, Params, 50000.0);
		Generate(PathB, 1234, Params, 50000.0);

		if (!TestEqual(TEXT("Segment count"), PathA.GetNumSegments(), PathB.GetNumSegments()))
		{
			return;
		}
		bool bAllIdentical = true;
		for (int32 Index = 0; Index < PathA.GetNumSegments(); ++Index)
		{
			bAllIdentical &= SegmentsIdentical(PathA.GetSegments()[Index], PathB.GetSegments()[Index]);
		}
		TestTrue(TEXT("Every segment is bit-identical"), bAllIdentical);
	});

	It("does not depend on how the target distance is split across calls", [this]()
	{
		FLRRoadGenParams Params;
		FLRRoadPath OneShot;
		Generate(OneShot, 77, Params, 20000.0);

		FLRRoadPath Incremental;
		Incremental.Reset(FLRRoadProfile(), FVector::ZeroVector, 0.f);
		FLRRoadGenerator Generator;
		Generator.Reset();
		FRandomStream Stream = LRRandom::MakeStream(77, LRRandom::EStream::RoadLayout);
		for (double Target = 500.0; Target <= 20000.0; Target += 333.0)
		{
			Generator.GenerateUntil(Incremental, Target, Stream, Params);
		}
		Generator.GenerateUntil(Incremental, 20000.0, Stream, Params);

		const int32 Common = FMath::Min(OneShot.GetNumSegments(), Incremental.GetNumSegments());
		TestTrue(TEXT("Both paths have segments"), Common > 0);
		bool bAllIdentical = true;
		for (int32 Index = 0; Index < Common; ++Index)
		{
			bAllIdentical &= SegmentsIdentical(OneShot.GetSegments()[Index], Incremental.GetSegments()[Index]);
		}
		TestTrue(TEXT("Incremental generation matches one-shot generation"), bAllIdentical);
	});

	It("produces different layouts for different seeds", [this]()
	{
		FLRRoadGenParams Params;
		FLRRoadPath PathA;
		FLRRoadPath PathB;
		Generate(PathA, 1, Params, 20000.0);
		Generate(PathB, 2, Params, 20000.0);

		bool bAnyDifference = PathA.GetNumSegments() != PathB.GetNumSegments();
		for (int32 Index = 0; !bAnyDifference && Index < PathA.GetNumSegments(); ++Index)
		{
			bAnyDifference = !SegmentsIdentical(PathA.GetSegments()[Index], PathB.GetSegments()[Index]);
		}
		TestTrue(TEXT("Seeds 1 and 2 differ"), bAnyDifference);
	});

	It("respects radius, grade and length limits", [this]()
	{
		FLRRoadGenParams Params;
		Params.MinRadiusM = 1500.f;
		Params.MaxRadiusM = 3000.f;
		Params.MaxGrade = 0.03f;
		Params.CurveProbability = 0.9f;
		Params.InitialStraightLengthM = 800.f;

		for (int32 Seed = 0; Seed < 20; ++Seed)
		{
			FLRRoadPath Path;
			Generate(Path, Seed, Params, 30000.0);

			TestTrue(TEXT("Reached the target length"), Path.GetEndS() >= 30000.0);
			const TArray<FLRRoadSegment>& Segments = Path.GetSegments();
			TestNearlyEqual(TEXT("Initial straight length"), Segments[0].Length, 800.0, 1e-6);
			TestNearlyEqual(TEXT("Initial straight curvature"), Segments[0].Curvature, 0.0, 0.0);

			bool bRadiusOk = true;
			bool bGradeOk = true;
			bool bGradeContinuous = true;
			bool bHasCurve = false;
			for (int32 Index = 0; Index < Segments.Num(); ++Index)
			{
				const FLRRoadSegment& Segment = Segments[Index];
				if (Segment.Curvature != 0.0)
				{
					bHasCurve = true;
					bRadiusOk &= 1.0 / FMath::Abs(Segment.Curvature) >= Params.MinRadiusM - 1e-3;
				}
				bGradeOk &= FMath::Abs(Segment.StartGrade) <= Params.MaxGrade + 1e-6 && FMath::Abs(Segment.EndGrade) <= Params.MaxGrade + 1e-6;
				if (Index > 0)
				{
					bGradeContinuous &= FMath::IsNearlyEqual(Segments[Index - 1].EndGrade, Segment.StartGrade, 1e-9);
				}
			}
			TestTrue(FString::Printf(TEXT("Seed %d: every radius >= MinRadiusM"), Seed), bRadiusOk);
			TestTrue(FString::Printf(TEXT("Seed %d: every grade within MaxGrade"), Seed), bGradeOk);
			TestTrue(FString::Printf(TEXT("Seed %d: grade is continuous at joints"), Seed), bGradeContinuous);
			TestTrue(FString::Printf(TEXT("Seed %d: contains curves"), Seed), bHasCurve);
		}
	});

	It("clamps unsafe radii to the hard minimum", [this]()
	{
		FLRRoadGenParams Params;
		Params.MinRadiusM = 200.f;
		Params.MaxRadiusM = 300.f;
		const FLRRoadGenParams Safe = FLRRoadGenerator::Sanitize(Params);
		TestTrue(TEXT("MinRadiusM clamped"), Safe.MinRadiusM >= FLRRoadGenerator::HardMinRadiusM);
		TestTrue(TEXT("MaxRadiusM >= MinRadiusM"), Safe.MaxRadiusM >= Safe.MinRadiusM);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
