// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRandom.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FLRRandomSpec, "LeftRight.Core.Random",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

	/** Every stream id currently defined (keep in sync with LRRandom::EStream). */
	static constexpr LRRandom::EStream AllStreams[] = {
		LRRandom::EStream::RoadLayout,
		LRRandom::EStream::RoadDecoration,
		LRRandom::EStream::TrafficSpawn,
		LRRandom::EStream::TrafficBehavior,
		LRRandom::EStream::Weather,
		LRRandom::EStream::Events
	};

END_DEFINE_SPEC(FLRRandomSpec)

void FLRRandomSpec::Define()
{
	Describe("DeriveSeed", [this]()
	{
		It("is deterministic for the same inputs", [this]()
		{
			const int32 Seeds[] = { 0, 1, -1, 12345, MAX_int32, MIN_int32 };
			for (const int32 Seed : Seeds)
			{
				for (const LRRandom::EStream Stream : AllStreams)
				{
					TestEqual(FString::Printf(TEXT("seed %d stream %u"), Seed, static_cast<uint32>(Stream)),
						LRRandom::DeriveSeed(Seed, Stream), LRRandom::DeriveSeed(Seed, Stream));
				}
			}
		});

		It("matches the golden values (algorithm must never change: replays and daily seeds depend on it)", [this]()
		{
			// Reference values computed with an independent implementation of the same SplitMix64 mix.
			TestEqual(TEXT("(12345, RoadLayout)"), LRRandom::DeriveSeed(12345, LRRandom::EStream::RoadLayout), 537145584);
			TestEqual(TEXT("(12345, TrafficSpawn)"), LRRandom::DeriveSeed(12345, LRRandom::EStream::TrafficSpawn), -1768557641);
			TestEqual(TEXT("(0, RoadLayout)"), LRRandom::DeriveSeed(0, LRRandom::EStream::RoadLayout), 403206445);
			TestEqual(TEXT("(-1, Events)"), LRRandom::DeriveSeed(-1, LRRandom::EStream::Events), -564233902);
		});

		It("gives different seeds to different streams of the same run", [this]()
		{
			const int32 Seeds[] = { 0, 7, 12345, -987654 };
			for (const int32 Seed : Seeds)
			{
				TSet<int32> Derived;
				for (const LRRandom::EStream Stream : AllStreams)
				{
					Derived.Add(LRRandom::DeriveSeed(Seed, Stream));
				}
				TestEqual(FString::Printf(TEXT("all streams distinct for seed %d"), Seed), Derived.Num(), static_cast<int32>(UE_ARRAY_COUNT(AllStreams)));
			}
		});

		It("gives different seeds to different runs on the same stream", [this]()
		{
			TSet<int32> Derived;
			constexpr int32 NumRuns = 1000;
			for (int32 Seed = 0; Seed < NumRuns; ++Seed)
			{
				Derived.Add(LRRandom::DeriveSeed(Seed, LRRandom::EStream::TrafficSpawn));
			}
			TestEqual(TEXT("1000 consecutive run seeds give 1000 distinct sub-seeds"), Derived.Num(), NumRuns);
		});
	});

	Describe("MakeStream", [this]()
	{
		It("produces reproducible sequences", [this]()
		{
			FRandomStream A = LRRandom::MakeStream(4242, LRRandom::EStream::TrafficBehavior);
			FRandomStream B = LRRandom::MakeStream(4242, LRRandom::EStream::TrafficBehavior);
			bool bAllEqual = true;
			for (int32 Index = 0; Index < 256; ++Index)
			{
				const float FA = A.FRand();
				const float FB = B.FRand();
				const int32 IA = A.RandRange(0, 1000);
				const int32 IB = B.RandRange(0, 1000);
				bAllEqual &= (FA == FB) && (IA == IB);
			}
			TestTrue(TEXT("two streams built from the same seed and id yield identical values"), bAllEqual);
		});

		It("uses the derived seed", [this]()
		{
			const FRandomStream Stream = LRRandom::MakeStream(99, LRRandom::EStream::Weather);
			TestEqual(TEXT("initial seed"), Stream.GetInitialSeed(), LRRandom::DeriveSeed(99, LRRandom::EStream::Weather));
		});

		It("produces different sequences for different streams", [this]()
		{
			FRandomStream A = LRRandom::MakeStream(4242, LRRandom::EStream::RoadLayout);
			FRandomStream B = LRRandom::MakeStream(4242, LRRandom::EStream::TrafficSpawn);
			int32 NumDifferent = 0;
			for (int32 Index = 0; Index < 64; ++Index)
			{
				NumDifferent += (A.GetUnsignedInt() != B.GetUnsignedInt()) ? 1 : 0;
			}
			TestTrue(TEXT("sequences of independent streams differ"), NumDifferent > 32);
		});
	});

	Describe("DailySeed", [this]()
	{
		It("is stable for the same date", [this]()
		{
			TestEqual(TEXT("same date twice"), LRRandom::DailySeed(2026, 11, 5), LRRandom::DailySeed(2026, 11, 5));
			TestEqual(TEXT("defined as DeriveSeed(YYYYMMDD, Events)"),
				LRRandom::DailySeed(2026, 11, 5), LRRandom::DeriveSeed(20261105, LRRandom::EStream::Events));
			TestEqual(TEXT("golden value for 2026-11-05"), LRRandom::DailySeed(2026, 11, 5), -422020207);
		});

		It("changes from one day to the next", [this]()
		{
			TSet<int32> Seeds;
			for (int32 Day = 1; Day <= 31; ++Day)
			{
				Seeds.Add(LRRandom::DailySeed(2027, 1, Day));
			}
			TestEqual(TEXT("31 days give 31 distinct seeds"), Seeds.Num(), 31);
			TestNotEqual(TEXT("Dec 31 vs Jan 1"), LRRandom::DailySeed(2026, 12, 31), LRRandom::DailySeed(2027, 1, 1));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
