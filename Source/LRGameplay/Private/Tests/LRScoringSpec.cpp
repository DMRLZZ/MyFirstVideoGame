// Copyright LEFTRIGHT. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LRDataLoader.h"
#include "LRScoring.h"

namespace LRScoringSpecPrivate
{
	/** Player speed that keeps the combo alive in the tests, km/h. */
	constexpr float FastKmh = 200.f;

	/** Relative speed that gives a relative speed factor of exactly 1, km/h. */
	constexpr float UnitRelativeKmh = 100.f;
}

BEGIN_DEFINE_SPEC(FLRScoringSpec, "LeftRight.Gameplay.Scoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRScoringSpec)

void FLRScoringSpec::Define()
{
	using namespace LRScoringSpecPrivate;

	Describe("Distance", [this]()
	{
		It("pays 1 point per 10 m times the multiplier of each speed tier", [this]()
		{
			FLRScoreCalculator Slow;
			TestEqual(TEXT("below 120 km/h: x0.5"), Slow.AddDistance(1000.0, 100.f), int64(50));

			FLRScoreCalculator Normal;
			TestEqual(TEXT("120-200 km/h: x1"), Normal.AddDistance(1000.0, 150.f), int64(100));

			FLRScoreCalculator Fast;
			TestEqual(TEXT("200-280 km/h: x1.5"), Fast.AddDistance(1000.0, 250.f), int64(150));

			FLRScoreCalculator Fastest;
			TestEqual(TEXT("above 280 km/h: x2"), Fastest.AddDistance(1000.0, 300.f), int64(200));
		});

		It("keeps the fractions between small steps", [this]()
		{
			FLRScoreCalculator Calculator;
			int64 Total = 0;
			for (int32 Step = 0; Step < 1000; ++Step)
			{
				Total += Calculator.AddDistance(1.0, 100.f);
			}
			TestEqual(TEXT("1000 steps of 1 m at x0.5"), Total, int64(50));
			TestEqual(TEXT("the score matches"), Calculator.GetState().Score, int64(50));
			TestEqual(TEXT("the distance is tracked"), Calculator.GetState().DistanceMeters, 1000.0, 1.0e-6);
		});

		It("ignores negative distance", [this]()
		{
			FLRScoreCalculator Calculator;
			TestEqual(TEXT("no points"), Calculator.AddDistance(-50.0, 300.f), int64(0));
			TestEqual(TEXT("no distance"), Calculator.GetState().DistanceMeters, 0.0, 1.0e-9);
		});
	});

	Describe("SpeedTier", [this]()
	{
		It("finds the tier of a speed, lower limit included", [this]()
		{
			const FLRScoreCalculator Calculator;
			TestEqual(TEXT("0 km/h"), Calculator.GetSpeedTierIndex(0.f), 0);
			TestEqual(TEXT("119 km/h"), Calculator.GetSpeedTierIndex(119.f), 0);
			TestEqual(TEXT("120 km/h"), Calculator.GetSpeedTierIndex(120.f), 1);
			TestEqual(TEXT("199 km/h"), Calculator.GetSpeedTierIndex(199.f), 1);
			TestEqual(TEXT("200 km/h"), Calculator.GetSpeedTierIndex(200.f), 2);
			TestEqual(TEXT("280 km/h"), Calculator.GetSpeedTierIndex(280.f), 3);
			TestEqual(TEXT("400 km/h"), Calculator.GetSpeedTierIndex(400.f), 3);
		});

		It("sorts unordered tiers and survives an empty table", [this]()
		{
			FLRScoringRules Rules;
			Rules.SpeedTiers.SwapMemory(0, 3);
			const FLRScoringRules Sorted = Rules.Sanitized();
			TestEqual(TEXT("first tier"), Sorted.SpeedTiers[0].MinKmh, 0.f);
			TestEqual(TEXT("last tier"), Sorted.SpeedTiers[3].MinKmh, 280.f);

			FLRScoringRules Empty;
			Empty.SpeedTiers.Reset();
			TestEqual(TEXT("one tier is added"), Empty.Sanitized().SpeedTiers.Num(), 1);
		});
	});

	Describe("NearMissFormula", [this]()
	{
		const FLRScoringRules Rules = FLRScoringRules().Sanitized();

		It("applies the distance factor of each tier", [this, Rules]()
		{
			TestEqual(TEXT("Close"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::Close, UnitRelativeKmh, false, false), 250.0, 1.0e-6);
			TestEqual(TEXT("VeryClose"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::VeryClose, UnitRelativeKmh, false, false), 500.0, 1.0e-6);
			TestEqual(TEXT("Razor"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::Razor, UnitRelativeKmh, false, false), 750.0, 1.0e-6);
			TestEqual(TEXT("None"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::None, UnitRelativeKmh, false, false), 0.0, 1.0e-6);
		});

		It("clamps the relative speed factor between 0.5 and 3", [this, Rules]()
		{
			TestEqual(TEXT("20 km/h"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::Close, 20.f, false, false), 125.0, 1.0e-6);
			TestEqual(TEXT("150 km/h"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::Close, 150.f, false, false), 375.0, 1.0e-6);
			TestEqual(TEXT("500 km/h"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::Close, 500.f, false, false), 750.0, 1.0e-6);
		});

		It("doubles for oncoming traffic and for threading the needle", [this, Rules]()
		{
			TestEqual(TEXT("oncoming"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::Close, UnitRelativeKmh, true, false), 500.0, 1.0e-6);
			TestEqual(TEXT("thread"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::Close, UnitRelativeKmh, false, true), 500.0, 1.0e-6);
			TestEqual(TEXT("razor at 150 km/h, oncoming and thread"), FLRScoreCalculator::ComputeNearMissPoints(Rules, ELRNearMissTier::Razor, 150.f, true, true), 4500.0, 1.0e-6);
		});
	});

	Describe("Combo", [this]()
	{
		It("pays the first near miss x1 and grows 0.25 per chained one", [this]()
		{
			FLRScoreCalculator Calculator;
			TestEqual(TEXT("first"), Calculator.ApplyNearMiss(ELRNearMissTier::VeryClose, UnitRelativeKmh, false, false, FastKmh), int64(500));
			TestEqual(TEXT("chain after the first"), Calculator.GetState().Chain, 1);
			TestEqual(TEXT("multiplier after the first"), Calculator.GetComboMultiplier(), 1.25f);

			TestEqual(TEXT("second"), Calculator.ApplyNearMiss(ELRNearMissTier::VeryClose, UnitRelativeKmh, false, false, FastKmh), int64(625));
			TestEqual(TEXT("third"), Calculator.ApplyNearMiss(ELRNearMissTier::VeryClose, UnitRelativeKmh, false, false, FastKmh), int64(750));
			TestEqual(TEXT("score"), Calculator.GetState().Score, int64(1875));
			TestEqual(TEXT("near misses"), Calculator.GetState().NearMisses, 3);
		});

		It("caps the multiplier at x5 while the chain keeps counting", [this]()
		{
			FLRScoreCalculator Calculator;
			for (int32 Index = 0; Index < 20; ++Index)
			{
				Calculator.ApplyNearMiss(ELRNearMissTier::Close, UnitRelativeKmh, false, false, FastKmh);
			}
			TestEqual(TEXT("multiplier"), Calculator.GetComboMultiplier(), 5.f);
			TestEqual(TEXT("chain"), Calculator.GetState().Chain, 20);
			TestEqual(TEXT("best chain"), Calculator.GetState().BestChain, 20);
			TestEqual(TEXT("capped payment"), Calculator.ApplyNearMiss(ELRNearMissTier::Close, UnitRelativeKmh, false, false, FastKmh), int64(1250));
		});

		It("expires after the window and restarts with each near miss", [this]()
		{
			FLRScoreCalculator Calculator;
			Calculator.ApplyNearMiss(ELRNearMissTier::Close, UnitRelativeKmh, false, false, FastKmh);

			TestTrue(TEXT("alive at 2 s"), Calculator.Tick(2.f, FastKmh) == ELRComboTickResult::None);
			Calculator.ApplyNearMiss(ELRNearMissTier::Close, UnitRelativeKmh, false, false, FastKmh);
			TestTrue(TEXT("alive 2 s after the second one"), Calculator.Tick(2.f, FastKmh) == ELRComboTickResult::None);
			TestEqual(TEXT("chain"), Calculator.GetState().Chain, 2);

			TestTrue(TEXT("expires"), Calculator.Tick(1.5f, FastKmh) == ELRComboTickResult::Expired);
			TestEqual(TEXT("chain after expiring"), Calculator.GetState().Chain, 0);
			TestEqual(TEXT("multiplier after expiring"), Calculator.GetComboMultiplier(), 1.f);
			TestEqual(TEXT("the best chain stays"), Calculator.GetState().BestChain, 2);
			TestTrue(TEXT("nothing left to expire"), Calculator.Tick(5.f, FastKmh) == ELRComboTickResult::None);
		});

		It("breaks on a scrape", [this]()
		{
			FLRScoreCalculator Calculator;
			Calculator.ApplyNearMiss(ELRNearMissTier::Close, UnitRelativeKmh, false, false, FastKmh);
			TestTrue(TEXT("there was a chain"), Calculator.BreakCombo());
			TestEqual(TEXT("chain"), Calculator.GetState().Chain, 0);
			TestFalse(TEXT("nothing left to break"), Calculator.BreakCombo());
		});

		It("breaks below 120 km/h and does not grow there", [this]()
		{
			FLRScoreCalculator Calculator;
			Calculator.ApplyNearMiss(ELRNearMissTier::Close, UnitRelativeKmh, false, false, FastKmh);
			TestTrue(TEXT("breaks"), Calculator.Tick(0.1f, 119.f) == ELRComboTickResult::Broken);
			TestEqual(TEXT("chain"), Calculator.GetState().Chain, 0);

			TestEqual(TEXT("a slow near miss still pays"), Calculator.ApplyNearMiss(ELRNearMissTier::Close, UnitRelativeKmh, false, false, 90.f), int64(250));
			TestEqual(TEXT("but starts no chain"), Calculator.GetState().Chain, 0);
			TestEqual(TEXT("and counts"), Calculator.GetState().NearMisses, 2);
		});
	});

	Describe("Run statistics", [this]()
	{
		It("tracks duration and top speed and resets them", [this]()
		{
			FLRScoreCalculator Calculator;
			Calculator.Tick(1.f, 150.f);
			Calculator.Tick(0.5f, 240.f);
			Calculator.Tick(0.5f, 180.f);
			TestEqual(TEXT("duration"), Calculator.GetState().DurationSeconds, 2.f);
			TestEqual(TEXT("top speed"), Calculator.GetState().TopSpeedKmh, 240.f);

			Calculator.Reset();
			TestEqual(TEXT("duration after reset"), Calculator.GetState().DurationSeconds, 0.f);
			TestEqual(TEXT("top speed after reset"), Calculator.GetState().TopSpeedKmh, 0.f);
		});

		It("scores nothing with points disabled but tracks distance", [this]()
		{
			FLRScoreCalculator Calculator;
			Calculator.SetPointsEnabled(false);
			TestEqual(TEXT("distance points"), Calculator.AddDistance(1000.0, 300.f), int64(0));
			TestEqual(TEXT("near miss points"), Calculator.ApplyNearMiss(ELRNearMissTier::Razor, 200.f, true, true, FastKmh), int64(0));
			TestEqual(TEXT("score"), Calculator.GetState().Score, int64(0));
			TestEqual(TEXT("chain"), Calculator.GetState().Chain, 0);
			TestEqual(TEXT("distance"), Calculator.GetState().DistanceMeters, 1000.0, 1.0e-6);
		});
	});

	Describe("Data", [this]()
	{
		It("parses the envelope and replaces the tier table", [this]()
		{
			const FString Json = TEXT(R"json({
				"type": "ScoringRules", "id": "test", "version": 1,
				"data": {
					"NearMissBasePoints": 100,
					"ComboMaxMultiplier": 3,
					"SpeedTiers": [ { "MinKmh": 0, "Multiplier": 2 } ]
				}
			})json");

			FLRScoringRules Rules;
			TestTrue(TEXT("parses"), FLRDataLoader::Parse(Json, Rules));
			TestEqual(TEXT("base points"), Rules.NearMissBasePoints, 100.f);
			TestEqual(TEXT("combo cap"), Rules.ComboMaxMultiplier, 3.f);
			TestEqual(TEXT("tiers"), Rules.SpeedTiers.Num(), 1);
			TestEqual(TEXT("a missing field keeps the default"), Rules.ComboWindowSeconds, 3.f);
		});

		It("ships a file with the values of the design document", [this]()
		{
			FLRScoringRules Rules;
			TestTrue(TEXT("loads"), FLRDataLoader::Load(FLRScoringRules::DataPath, Rules));
			TestEqual(TEXT("four tiers"), Rules.SpeedTiers.Num(), 4);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
