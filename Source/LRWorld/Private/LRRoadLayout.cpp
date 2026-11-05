// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRoadLayout.h"

namespace LRRoadLayout
{
	void BuildLanes(const FLRRoadProfile& Profile, TArray<FLRLaneDesc>& OutLanes)
	{
		OutLanes.Reset();

		const int32 N = FMath::Max(1, Profile.LanesPerDirection);
		const float W = FMath::Max(0.1f, Profile.LaneWidthMeters);

		if (!Profile.bTwoWay)
		{
			OutLanes.Reserve(N);
			const float HalfSpan = 0.5f * static_cast<float>(N - 1);
			for (int32 Index = 0; Index < N; ++Index)
			{
				FLRLaneDesc& Lane = OutLanes.AddDefaulted_GetRef();
				Lane.CenterD = (static_cast<float>(Index) - HalfSpan) * W;
				Lane.WidthMeters = W;
				Lane.Direction = 1;
			}
			return;
		}

		OutLanes.Reserve(2 * N);
		const float HalfMedian = 0.5f * FMath::Max(0.f, Profile.MedianWidthMeters);

		// Oncoming lanes on the left: the outermost first so the array stays sorted by CenterD.
		for (int32 Index = 0; Index < N; ++Index)
		{
			FLRLaneDesc& Lane = OutLanes.AddDefaulted_GetRef();
			Lane.CenterD = -(HalfMedian + (static_cast<float>(N - 1 - Index) + 0.5f) * W);
			Lane.WidthMeters = W;
			Lane.Direction = -1;
		}

		// Player lanes on the right, from the median outwards.
		for (int32 Index = 0; Index < N; ++Index)
		{
			FLRLaneDesc& Lane = OutLanes.AddDefaulted_GetRef();
			Lane.CenterD = HalfMedian + (static_cast<float>(Index) + 0.5f) * W;
			Lane.WidthMeters = W;
			Lane.Direction = 1;
		}
	}

	FLRRoadCrossSection ComputeCrossSection(const FLRRoadProfile& Profile, const FLRRoadDressing& Dressing)
	{
		const int32 N = FMath::Max(1, Profile.LanesPerDirection);
		const float W = FMath::Max(0.1f, Profile.LaneWidthMeters);
		const float Shoulder = FMath::Max(0.f, Profile.ShoulderWidthMeters);
		const float Median = FMath::Max(0.f, Profile.MedianWidthMeters);
		const float BarrierOffset = FMath::Max(0.f, Dressing.BarrierOffsetM);

		FLRRoadCrossSection Section;
		if (Profile.bTwoWay)
		{
			Section.LanesRightD = 0.5f * Median + static_cast<float>(N) * W;
			Section.LanesLeftD = -Section.LanesRightD;
		}
		else
		{
			Section.LanesRightD = 0.5f * static_cast<float>(N) * W;
			Section.LanesLeftD = -Section.LanesRightD;
		}

		Section.ShoulderLeftD = Section.LanesLeftD - Shoulder;
		Section.ShoulderRightD = Section.LanesRightD + Shoulder;
		Section.RightBarrierD = Section.ShoulderRightD + BarrierOffset;

		// One-way with a median: the left barrier stands in the middle of the median (the opposite carriageway is not modeled).
		Section.bLeftBarrierIsMedian = !Profile.bTwoWay && Median > 0.f;
		Section.LeftBarrierD = Section.ShoulderLeftD - (Section.bLeftBarrierIsMedian ? FMath::Max(0.5f * Median, BarrierOffset) : BarrierOffset);
		return Section;
	}

	void BuildMarkings(const FLRRoadProfile& Profile, const FLRRoadDressing& Dressing, TArray<FLRLaneMarking>& OutMarkings)
	{
		OutMarkings.Reset();

		const int32 N = FMath::Max(1, Profile.LanesPerDirection);
		const float W = FMath::Max(0.1f, Profile.LaneWidthMeters);
		const float EdgeWidth = Dressing.EdgeLineWidthM;
		const FLRRoadCrossSection Section = ComputeCrossSection(Profile, Dressing);

		auto Add = [&OutMarkings](float CenterD, float Width, bool bDashed, ELRLaneMarkingColor Color)
		{
			FLRLaneMarking& Marking = OutMarkings.AddDefaulted_GetRef();
			Marking.CenterD = CenterD;
			Marking.WidthMeters = Width;
			Marking.bDashed = bDashed;
			Marking.Color = Color;
		};

		OutMarkings.Reserve(2 * N + 4);

		if (!Profile.bTwoWay)
		{
			Add(Section.LanesLeftD + 0.5f * EdgeWidth, EdgeWidth, false, ELRLaneMarkingColor::Yellow);
			for (int32 Boundary = 1; Boundary < N; ++Boundary)
			{
				Add(Section.LanesLeftD + static_cast<float>(Boundary) * W, Dressing.LineWidthM, true, ELRLaneMarkingColor::White);
			}
			Add(Section.LanesRightD - 0.5f * EdgeWidth, EdgeWidth, false, ELRLaneMarkingColor::White);
			return;
		}

		const float HalfMedian = 0.5f * FMath::Max(0.f, Profile.MedianWidthMeters);
		const float HalfSpacing = 0.5f * Dressing.DoubleLineSpacingM;

		Add(Section.LanesLeftD + 0.5f * EdgeWidth, EdgeWidth, false, ELRLaneMarkingColor::White);
		for (int32 Boundary = N - 1; Boundary >= 1; --Boundary)
		{
			Add(-(HalfMedian + static_cast<float>(Boundary) * W), Dressing.LineWidthM, true, ELRLaneMarkingColor::White);
		}
		Add(-HalfSpacing, EdgeWidth, false, ELRLaneMarkingColor::Yellow);
		Add(HalfSpacing, EdgeWidth, false, ELRLaneMarkingColor::Yellow);
		for (int32 Boundary = 1; Boundary < N; ++Boundary)
		{
			Add(HalfMedian + static_cast<float>(Boundary) * W, Dressing.LineWidthM, true, ELRLaneMarkingColor::White);
		}
		Add(Section.LanesRightD - 0.5f * EdgeWidth, EdgeWidth, false, ELRLaneMarkingColor::White);
	}

	int32 FindNearestLane(TConstArrayView<FLRLaneDesc> Lanes, float D)
	{
		int32 Best = INDEX_NONE;
		float BestDistance = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Lanes.Num(); ++Index)
		{
			const float Distance = FMath::Abs(D - Lanes[Index].CenterD);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Index;
			}
		}
		return Best;
	}
}
