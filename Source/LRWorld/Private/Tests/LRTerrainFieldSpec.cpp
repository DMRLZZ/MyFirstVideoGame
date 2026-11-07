// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRTerrainField.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRTerrainFieldSpec, "LeftRight.World.TerrainField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRTerrainFieldSpec)

void FLRTerrainFieldSpec::Define()
{
	Describe("GetHeightM", [this]()
	{
		It("starts at road level behind the barrier", [this]()
		{
			const FLRTerrainParams Params;
			TestEqual(TEXT("Height at the barrier"), LRTerrainField::GetHeightM(Params, 1234.0, -987.0, 0.f), 0.f, 0.001f);
		});

		It("dips below the road in the ditch and never above it on the verge", [this]()
		{
			const FLRTerrainParams Params;
			TestTrue(TEXT("Ditch is below the road"), LRTerrainField::GetHeightM(Params, 10.0, 20.0, 0.5f * Params.DitchWidthM) < 0.f);
			TestEqual(TEXT("Flat verge"), LRTerrainField::GetHeightM(Params, 10.0, 20.0, Params.FlatWidthM), 0.f, 0.001f);
		});

		It("gives the same height for the same place", [this]()
		{
			const FLRTerrainParams Params;
			const float A = LRTerrainField::GetHeightM(Params, 4321.5, 876.25, 300.f);
			const float B = LRTerrainField::GetHeightM(Params, 4321.5, 876.25, 300.f);
			TestEqual(TEXT("Deterministic"), A, B);
		});

		It("has no steps between close points", [this]()
		{
			const FLRTerrainParams Params;
			float Previous = LRTerrainField::GetHeightM(Params, 0.0, 0.0, 0.f);
			for (int32 Step = 1; Step <= 2000; ++Step)
			{
				const float Distance = 0.5f * Step;
				const float Height = LRTerrainField::GetHeightM(Params, Distance, 0.0, Distance);
				if (!TestTrue(TEXT("Continuous"), FMath::Abs(Height - Previous) < 1.5f))
				{
					return;
				}
				Previous = Height;
			}
		});

		It("changes with the seed", [this]()
		{
			FLRTerrainParams A;
			FLRTerrainParams B;
			B.Seed = A.Seed + 1;
			TestNotEqual(TEXT("Other hills"), LRTerrainField::GetHeightM(A, 500.0, 500.0, 500.f), LRTerrainField::GetHeightM(B, 500.0, 500.0, 500.f));
		});
	});

	Describe("BuildColumnDistances", [this]()
	{
		It("goes from the barrier to the maximum distance in ascending order", [this]()
		{
			const FLRTerrainParams Params;
			TArray<float> Distances;
			LRTerrainField::BuildColumnDistances(Params, Distances);
			if (!TestTrue(TEXT("Has columns"), Distances.Num() >= 2))
			{
				return;
			}
			TestEqual(TEXT("First"), Distances[0], 0.f);
			TestEqual(TEXT("Last"), Distances.Last(), Params.MaxDistanceM);
			for (int32 Index = 1; Index < Distances.Num(); ++Index)
			{
				TestTrue(TEXT("Ascending"), Distances[Index] > Distances[Index - 1]);
			}
		});
	});

	Describe("GetRidgeHeightM", [this]()
	{
		It("closes the ring", [this]()
		{
			const float Start = LRTerrainField::GetRidgeHeightM(0.f, 100.f, 500.f, 3);
			const float End = LRTerrainField::GetRidgeHeightM(2.f * PI, 100.f, 500.f, 3);
			TestEqual(TEXT("Same height after a full turn"), Start, End, 0.5f);
		});

		It("never goes below its base", [this]()
		{
			for (int32 Step = 0; Step < 360; ++Step)
			{
				const float Height = LRTerrainField::GetRidgeHeightM(FMath::DegreesToRadians(static_cast<float>(Step)), 100.f, 500.f, 3);
				if (!TestTrue(TEXT("Above base"), Height >= 100.f))
				{
					return;
				}
			}
		});
	});
}

#endif
