// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRRoadGenerator.h"
#include "LRRandom.h"
#include "LRRoadPath.h"
#include "Misc/AutomationTest.h"

namespace LRRoadPathSpecPrivate
{
	/** Maximum allowed round-trip error, meters (1 cm). */
	constexpr double MaxRoundTripErrorM = 0.01;

	/** Lateral offsets exercised by the round-trip tests, meters. */
	const float TestOffsets[] = { -9.f, -5.55f, -1.85f, 0.f, 1.85f, 3.7f, 9.f };

	FLRRoadProfile MakeOneWayProfile()
	{
		FLRRoadProfile Profile;
		Profile.LanesPerDirection = 4;
		Profile.LaneWidthMeters = 3.7f;
		Profile.bTwoWay = false;
		return Profile;
	}
}

BEGIN_DEFINE_SPEC(FLRRoadPathSpec, "LeftRight.World.RoadPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

	/** Checks RoadToWorld -> WorldToRoad for every S in SValues and every test offset. */
	void CheckRoundTrip(const FLRRoadPath& Path, TConstArrayView<double> SValues)
	{
		using namespace LRRoadPathSpecPrivate;

		int32 Hint = INDEX_NONE;
		for (const double S : SValues)
		{
			for (const float D : TestOffsets)
			{
				const FVector Location = Path.RoadToWorld(S, D).GetLocation();
				FLRRoadCoord Coord;
				const bool bOk = Path.WorldToRoad(Location, Coord, &Hint);
				TestTrue(FString::Printf(TEXT("WorldToRoad succeeds at S=%.2f D=%.2f"), S, D), bOk);
				TestNearlyEqual(FString::Printf(TEXT("S round trip at S=%.2f D=%.2f"), S, D), Coord.S, S, MaxRoundTripErrorM);
				TestNearlyEqual(FString::Printf(TEXT("D round trip at S=%.2f D=%.2f"), S, D), static_cast<double>(Coord.D), static_cast<double>(D), MaxRoundTripErrorM);

				// Without a hint the binary-search fallback must find the same answer.
				FLRRoadCoord Unhinted;
				Path.WorldToRoad(Location, Unhinted);
				TestNearlyEqual(FString::Printf(TEXT("Unhinted S at S=%.2f D=%.2f"), S, D), Unhinted.S, S, MaxRoundTripErrorM);
			}
		}
	}

END_DEFINE_SPEC(FLRRoadPathSpec)

void FLRRoadPathSpec::Define()
{
	using namespace LRRoadPathSpecPrivate;

	Describe("Straight", [this]()
	{
		It("round-trips RoadToWorld -> WorldToRoad within 1 cm", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector(1000.0, -500.0, 200.0), 30.f);
			Path.AppendStraight(500.0);
			const double SValues[] = { 0.0, 10.0, 100.0, 250.0, 499.0, 500.0 };
			CheckRoundTrip(Path, SValues);
		});

		It("puts positive D on the right of the heading", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector::ZeroVector, 0.f);
			Path.AppendStraight(100.0);
			const FVector Center = Path.RoadToWorldLocation(50.0, 0.f);
			const FVector Right = Path.RoadToWorldLocation(50.0, 1.f);
			TestNearlyEqual(TEXT("Center X (cm)"), Center.X, 5000.0, 1e-6);
			TestNearlyEqual(TEXT("D = +1 m moves +100 cm along Y"), Right.Y - Center.Y, 100.0, 1e-6);
		});

		It("raises height with the grade and pitches the transform", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector::ZeroVector, 0.f);
			Path.AppendStraight(100.0, 0.05);
			const FTransform Transform = Path.RoadToWorld(100.0, 0.f);
			TestNearlyEqual(TEXT("Z after 100 m at 5 %"), Transform.GetLocation().Z, 500.0, 1e-6);
			TestNearlyEqual(TEXT("Pitch"), Transform.Rotator().Pitch, FMath::RadiansToDegrees(FMath::Atan(0.05)), 1e-3);
		});
	});

	Describe("Arc", [this]()
	{
		It("round-trips on right and left arcs within 1 cm", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector(0.0, 0.0, 0.0), -15.f);
			Path.AppendStraight(100.0);
			Path.AppendArc(800.0, 1.0 / 1200.0);
			Path.AppendArc(600.0, -1.0 / 1500.0, 0.02);
			Path.AppendStraight(200.0, -0.01);
			const double SValues[] = { 50.0, 100.0, 101.0, 350.0, 899.0, 900.0, 1200.0, 1499.0, 1600.0, 1700.0 };
			CheckRoundTrip(Path, SValues);
		});

		It("turns right (yaw increases) for positive curvature", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector::ZeroVector, 0.f);
			Path.AppendArc(500.0, 1.0 / 1000.0);
			TestNearlyEqual(TEXT("Heading after 500 m on R = 1000 m"), static_cast<double>(Path.GetHeadingYawDegrees(500.0)),
				FMath::RadiansToDegrees(0.5), 1e-3);
			TestTrue(TEXT("Moves toward +Y"), Path.RoadToWorldLocation(500.0, 0.f).Y > 0.0);
			TestNearlyEqual(TEXT("Curvature"), static_cast<double>(Path.GetCurvature(250.0)), 1.0 / 1000.0, 1e-9);
		});

		It("splits arcs turning more than 90 degrees and keeps round trips valid", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector::ZeroVector, 0.f);
			Path.AppendArc(4000.0, 1.0 / 1000.0);
			for (const FLRRoadSegment& Segment : Path.GetSegments())
			{
				TestTrue(TEXT("Piece turns at most 90 degrees"), FMath::Abs(Segment.Curvature) * Segment.Length <= FLRRoadPath::MaxArcAngle + 1e-9);
			}
			TestNearlyEqual(TEXT("Total length preserved"), Path.GetEndS(), 4000.0, 1e-6);
			const double SValues[] = { 100.0, 1500.0, 2500.0, 3900.0 };
			CheckRoundTrip(Path, SValues);
		});

		It("treats near-zero curvature as a straight", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector::ZeroVector, 0.f);
			Path.AppendArc(100.0, 1e-12);
			TestNearlyEqual(TEXT("Stored curvature"), Path.GetSegments()[0].Curvature, 0.0, 0.0);
			TestNearlyEqual(TEXT("End X (cm)"), Path.RoadToWorldLocation(100.0, 0.f).X, 10000.0, 1e-6);
		});
	});

	Describe("Joints", [this]()
	{
		It("keep position, heading and height continuous", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector(12345.0, -6789.0, 300.0), 47.f);
			Path.AppendStraight(150.0, 0.01);
			Path.AppendSegment(300.0, 1.0 / 1100.0, 0.01, -0.02);
			Path.AppendSegment(250.0, -1.0 / 2000.0, -0.02, 0.0);
			Path.AppendStraight(100.0);

			const TArray<FLRRoadSegment>& Segments = Path.GetSegments();
			for (int32 Index = 0; Index + 1 < Segments.Num(); ++Index)
			{
				double X, Y, Z, Heading, Grade;
				FLRRoadPath::EvaluateSegment(Segments[Index], Segments[Index].Length, X, Y, Z, Heading, Grade);
				const FLRRoadSegment& Next = Segments[Index + 1];
				TestNearlyEqual(TEXT("Joint X"), X, Next.StartX, 1e-6);
				TestNearlyEqual(TEXT("Joint Y"), Y, Next.StartY, 1e-6);
				TestNearlyEqual(TEXT("Joint Z"), Z, Next.StartZ, 1e-6);
				TestNearlyEqual(TEXT("Joint heading"), FMath::UnwindRadians(Heading - Next.StartHeading), 0.0, 1e-9);
				TestNearlyEqual(TEXT("Joint S"), Segments[Index].GetEndS(), Next.StartS, 1e-9);

				// Evaluating through the public API just before / after the joint gives (almost) the same point.
				const FVector Before = Path.RoadToWorldLocation(Next.StartS - 1e-4, 2.f);
				const FVector After = Path.RoadToWorldLocation(Next.StartS + 1e-4, 2.f);
				TestTrue(TEXT("No visible step at the joint (< 1 mm)"), FVector::Dist(Before, After) < 0.1);
			}
		});
	});

	Describe("TrimBefore", [this]()
	{
		It("keeps queries and hints valid after trimming", [this]()
		{
			FLRRoadPath Path;
			Path.Reset(MakeOneWayProfile(), FVector::ZeroVector, 0.f);
			FLRRoadGenerator Generator;
			Generator.Reset();
			FRandomStream Stream = LRRandom::MakeStream(42, LRRandom::EStream::RoadLayout);
			FLRRoadGenParams Params;
			Generator.GenerateUntil(Path, 12000.0, Stream, Params);

			// Warm a hint near S = 6000.
			int32 Hint = INDEX_NONE;
			FLRRoadCoord Coord;
			TestTrue(TEXT("Initial query"), Path.WorldToRoad(Path.RoadToWorld(6000.0, 1.85f).GetLocation(), Coord, &Hint));

			const FVector LocationBefore = Path.RoadToWorldLocation(7000.0, -3.f);
			Path.TrimBefore(5000.0);

			TestTrue(TEXT("Some segments were trimmed"), Path.GetNumTrimmedSegments() > 0);
			TestTrue(TEXT("Start moved forward"), Path.GetStartS() > 0.0 && Path.GetStartS() <= 5000.0);
			TestTrue(TEXT("End unchanged"), Path.GetEndS() >= 12000.0);
			TestTrue(TEXT("Geometry unchanged by trimming"), FVector::Dist(LocationBefore, Path.RoadToWorldLocation(7000.0, -3.f)) < 1e-6);

			// The hint obtained before trimming still resolves correctly.
			FLRRoadCoord After;
			TestTrue(TEXT("Hinted query after trim"), Path.WorldToRoad(Path.RoadToWorld(6100.0, 1.85f).GetLocation(), After, &Hint));
			TestNearlyEqual(TEXT("S after trim"), After.S, 6100.0, MaxRoundTripErrorM);
			TestNearlyEqual(TEXT("D after trim"), static_cast<double>(After.D), 1.85, MaxRoundTripErrorM);

			const double SValues[] = { 5200.0, 8000.0, 11000.0 };
			CheckRoundTrip(Path, SValues);

			// Trimming never removes the last segment.
			Path.TrimBefore(1e9);
			TestEqual(TEXT("One segment left"), Path.GetNumSegments(), 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
