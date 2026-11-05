// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRRoadLayout.h"
#include "LRRoadPath.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRRoadLayoutSpec, "LeftRight.World.RoadLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRRoadLayoutSpec)

void FLRRoadLayoutSpec::Define()
{
	Describe("OneWay4Lanes", [this]()
	{
		It("centers four player lanes on D = 0", [this]()
		{
			FLRRoadProfile Profile;
			Profile.LanesPerDirection = 4;
			Profile.LaneWidthMeters = 3.7f;
			Profile.bTwoWay = false;

			TArray<FLRLaneDesc> Lanes;
			LRRoadLayout::BuildLanes(Profile, Lanes);

			const float Expected[] = { -5.55f, -1.85f, 1.85f, 5.55f };
			if (!TestEqual(TEXT("Lane count"), Lanes.Num(), 4))
			{
				return;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				TestNearlyEqual(FString::Printf(TEXT("Lane %d center"), Index), Lanes[Index].CenterD, Expected[Index], 1e-4f);
				TestEqual(FString::Printf(TEXT("Lane %d direction"), Index), Lanes[Index].Direction, 1);
				TestNearlyEqual(FString::Printf(TEXT("Lane %d width"), Index), Lanes[Index].WidthMeters, 3.7f, 1e-6f);
			}

			TestEqual(TEXT("Nearest lane to D = 0.1"), LRRoadLayout::FindNearestLane(Lanes, 0.1f), 2);
			TestEqual(TEXT("Nearest lane to D = -20"), LRRoadLayout::FindNearestLane(Lanes, -20.f), 0);
		});

		It("paints a yellow left edge, a white right edge and three dashed separators", [this]()
		{
			FLRRoadProfile Profile;
			Profile.LanesPerDirection = 4;
			FLRRoadDressing Dressing;

			TArray<FLRLaneMarking> Markings;
			LRRoadLayout::BuildMarkings(Profile, Dressing, Markings);

			if (!TestEqual(TEXT("Marking count"), Markings.Num(), 5))
			{
				return;
			}
			TestTrue(TEXT("Left edge is solid yellow"), Markings[0].Color == ELRLaneMarkingColor::Yellow && !Markings[0].bDashed);
			TestTrue(TEXT("Right edge is solid white"), Markings[4].Color == ELRLaneMarkingColor::White && !Markings[4].bDashed);
			for (int32 Index = 1; Index <= 3; ++Index)
			{
				TestTrue(TEXT("Separator is dashed white"), Markings[Index].bDashed && Markings[Index].Color == ELRLaneMarkingColor::White);
			}
			TestNearlyEqual(TEXT("Middle separator at D = 0"), Markings[2].CenterD, 0.f, 1e-4f);
		});

		It("puts the median barrier left of the left shoulder", [this]()
		{
			FLRRoadProfile Profile;
			Profile.LanesPerDirection = 4;
			Profile.ShoulderWidthMeters = 3.f;
			Profile.MedianWidthMeters = 4.f;
			FLRRoadDressing Dressing;

			const FLRRoadCrossSection Section = LRRoadLayout::ComputeCrossSection(Profile, Dressing);
			TestTrue(TEXT("Left barrier is a median barrier"), Section.bLeftBarrierIsMedian);
			TestNearlyEqual(TEXT("Lanes right edge"), Section.LanesRightD, 7.4f, 1e-4f);
			TestNearlyEqual(TEXT("Shoulder right edge"), Section.ShoulderRightD, 10.4f, 1e-4f);
			TestNearlyEqual(TEXT("Median barrier"), Section.LeftBarrierD, -12.4f, 1e-4f);
		});
	});

	Describe("TwoWay2Plus2", [this]()
	{
		It("puts oncoming lanes on the left and player lanes on the right", [this]()
		{
			FLRRoadProfile Profile;
			Profile.LanesPerDirection = 2;
			Profile.LaneWidthMeters = 3.7f;
			Profile.MedianWidthMeters = 0.6f;
			Profile.bTwoWay = true;

			TArray<FLRLaneDesc> Lanes;
			LRRoadLayout::BuildLanes(Profile, Lanes);

			const float ExpectedCenters[] = { -5.85f, -2.15f, 2.15f, 5.85f };
			const int32 ExpectedDirections[] = { -1, -1, 1, 1 };
			if (!TestEqual(TEXT("Lane count"), Lanes.Num(), 4))
			{
				return;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				TestNearlyEqual(FString::Printf(TEXT("Lane %d center"), Index), Lanes[Index].CenterD, ExpectedCenters[Index], 1e-4f);
				TestEqual(FString::Printf(TEXT("Lane %d direction"), Index), Lanes[Index].Direction, ExpectedDirections[Index]);
			}

			// The path exposes the same lanes.
			FLRRoadPath Path;
			Path.Reset(Profile, FVector::ZeroVector, 0.f);
			TestEqual(TEXT("Path lane count"), Path.GetLanes().Num(), 4);
			TestEqual(TEXT("Path nearest lane to D = 3"), Path.FindNearestLane(3.f), 2);
		});

		It("paints a double yellow center line and white outer edges without a median barrier", [this]()
		{
			FLRRoadProfile Profile;
			Profile.LanesPerDirection = 2;
			Profile.MedianWidthMeters = 0.6f;
			Profile.bTwoWay = true;
			FLRRoadDressing Dressing;

			TArray<FLRLaneMarking> Markings;
			LRRoadLayout::BuildMarkings(Profile, Dressing, Markings);

			int32 NumYellow = 0;
			int32 NumDashed = 0;
			for (const FLRLaneMarking& Marking : Markings)
			{
				NumYellow += Marking.Color == ELRLaneMarkingColor::Yellow ? 1 : 0;
				NumDashed += Marking.bDashed ? 1 : 0;
				if (Marking.Color == ELRLaneMarkingColor::Yellow)
				{
					TestTrue(TEXT("Yellow lines are solid and inside the median"), !Marking.bDashed && FMath::Abs(Marking.CenterD) < 0.3f);
				}
			}
			TestEqual(TEXT("Marking count"), Markings.Num(), 6);
			TestEqual(TEXT("Double yellow"), NumYellow, 2);
			TestEqual(TEXT("One dashed separator per direction"), NumDashed, 2);
			TestTrue(TEXT("Outer edges are white"), Markings[0].Color == ELRLaneMarkingColor::White && Markings.Last().Color == ELRLaneMarkingColor::White);

			const FLRRoadCrossSection Section = LRRoadLayout::ComputeCrossSection(Profile, Dressing);
			TestFalse(TEXT("No median barrier"), Section.bLeftBarrierIsMedian);
			TestNearlyEqual(TEXT("Symmetric barriers"), Section.LeftBarrierD, -Section.RightBarrierD, 1e-4f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
