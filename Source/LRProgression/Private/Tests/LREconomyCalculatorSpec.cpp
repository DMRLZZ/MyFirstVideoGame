// Copyright LEFTRIGHT. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LRDataLoader.h"
#include "LREconomyCalculator.h"
#include "LREvents.h"
#include "LRProgressionTypes.h"

namespace LREconomyCalculatorSpecPrivate
{
	/** Small rule set independent from the shipped JSON so tests do not break when designers retune prices. */
	FLREconomyRules MakeRules()
	{
		FLREconomyRules Rules;
		Rules.CreditsPerPoint = 0.1f;
		Rules.MinCreditsPerRun = 100;
		Rules.DistanceCreditsPerKm = 50.f;
		Rules.StartingCredits = 5000;
		Rules.StarterVehicle = TEXT("bauer_r3_sport");

		FLRVehiclePrice Starter;
		Starter.VehicleId = TEXT("bauer_r3_sport");
		Starter.Price = 0;
		Starter.UpgradeCostMultiplier = 0.5f;
		Rules.VehiclePrices.Add(Starter);

		FLRVehiclePrice Volta;
		Volta.VehicleId = TEXT("volta_nine");
		Volta.Price = 120000;
		Volta.UpgradeCostMultiplier = 2.f;
		Rules.VehiclePrices.Add(Volta);

		Rules.UpgradeLevelPrices = { 1000, 2000, 4000 };

		FLRModeCreditMultiplier Daily;
		Daily.Mode = ELRGameModeType::Daily;
		Daily.Multiplier = 1.5f;
		Rules.ModeMultipliers.Add(Daily);
		return Rules;
	}

	FLRRunResult MakeRun(int64 Score, float DistanceMeters)
	{
		FLRRunResult Result;
		Result.FinalScore = Score;
		Result.DistanceMeters = DistanceMeters;
		return Result;
	}

	bool IsResult(ELRPurchaseResult Actual, ELRPurchaseResult Expected)
	{
		return Actual == Expected;
	}
}

BEGIN_DEFINE_SPEC(FLREconomyCalculatorSpec, "LeftRight.Progression.Economy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	FLREconomyRules Rules;
END_DEFINE_SPEC(FLREconomyCalculatorSpec)

void FLREconomyCalculatorSpec::Define()
{
	using namespace LREconomyCalculatorSpecPrivate;

	BeforeEach([this]()
	{
		Rules = MakeRules();
	});

	Describe("ComputeRunCredits", [this]()
	{
		It("adds score and distance credits", [this]()
		{
			// 50000 * 0.1 + 10 km * 50 = 5500
			TestEqual(TEXT("credits"), FLREconomyCalculator::ComputeRunCredits(Rules, MakeRun(50000, 10000.f), ELRGameModeType::EndlessOneWay), int64(5500));
		});

		It("floors fractional credits", [this]()
		{
			// 1239 * 0.1 = 123.9 -> 123
			TestEqual(TEXT("credits"), FLREconomyCalculator::ComputeRunCredits(Rules, MakeRun(1239, 0.f), ELRGameModeType::EndlessOneWay), int64(123));
		});

		It("applies the minimum to short runs", [this]()
		{
			TestEqual(TEXT("credits"), FLREconomyCalculator::ComputeRunCredits(Rules, MakeRun(10, 50.f), ELRGameModeType::EndlessOneWay), int64(100));
		});

		It("gives nothing for an empty run", [this]()
		{
			TestEqual(TEXT("credits"), FLREconomyCalculator::ComputeRunCredits(Rules, MakeRun(0, 0.f), ELRGameModeType::EndlessOneWay), int64(0));
			TestEqual(TEXT("negative"), FLREconomyCalculator::ComputeRunCredits(Rules, MakeRun(-100, -5.f), ELRGameModeType::EndlessOneWay), int64(0));
		});

		It("applies the mode multiplier", [this]()
		{
			TestEqual(TEXT("daily"), FLREconomyCalculator::ComputeRunCredits(Rules, MakeRun(50000, 10000.f), ELRGameModeType::Daily), int64(8250));
			TestEqual(TEXT("unlisted mode"), FLREconomyCalculator::GetModeMultiplier(Rules, ELRGameModeType::FreeRide), 1.f);
		});

		It("does not overflow on absurd scores", [this]()
		{
			const int64 Credits = FLREconomyCalculator::ComputeRunCredits(Rules, MakeRun(MAX_int64, 1.0e9f), ELRGameModeType::EndlessOneWay);
			TestTrue(TEXT("positive and bounded"), Credits > 0 && Credits <= int64(1000000000000000));
		});
	});

	Describe("Prices", [this]()
	{
		It("returns vehicle prices and -1 for unknown vehicles", [this]()
		{
			TestEqual(TEXT("volta"), FLREconomyCalculator::GetVehiclePrice(Rules, TEXT("volta_nine")), int64(120000));
			TestEqual(TEXT("unknown"), FLREconomyCalculator::GetVehiclePrice(Rules, TEXT("not_a_car")), int64(INDEX_NONE));
		});

		It("scales upgrade prices per vehicle and reuses the last level price", [this]()
		{
			TestEqual(TEXT("volta level 0"), FLREconomyCalculator::GetUpgradePrice(Rules, TEXT("volta_nine"), 0), int64(2000));
			TestEqual(TEXT("starter level 1"), FLREconomyCalculator::GetUpgradePrice(Rules, TEXT("bauer_r3_sport"), 1), int64(1000));
			TestEqual(TEXT("volta level 4 reuses last"), FLREconomyCalculator::GetUpgradePrice(Rules, TEXT("volta_nine"), 4), int64(8000));
			TestEqual(TEXT("max level"), FLREconomyCalculator::GetUpgradePrice(Rules, TEXT("volta_nine"), LRProgression::MaxUpgradeLevel), int64(INDEX_NONE));
		});
	});

	Describe("ValidateVehiclePurchase", [this]()
	{
		It("allows a purchase with enough credits", [this]()
		{
			const TArray<FName> Owned = { TEXT("bauer_r3_sport") };
			TestTrue(TEXT("success"), IsResult(FLREconomyCalculator::ValidateVehiclePurchase(Rules, 120000, Owned, TEXT("volta_nine")), ELRPurchaseResult::Success));
		});

		It("refuses buying the same vehicle twice", [this]()
		{
			const TArray<FName> Owned = { TEXT("bauer_r3_sport"), TEXT("volta_nine") };
			TestTrue(TEXT("already owned"), IsResult(FLREconomyCalculator::ValidateVehiclePurchase(Rules, 1000000, Owned, TEXT("volta_nine")), ELRPurchaseResult::AlreadyOwned));
		});

		It("refuses with insufficient credits", [this]()
		{
			const TArray<FName> Owned = { TEXT("bauer_r3_sport") };
			TestTrue(TEXT("insufficient"), IsResult(FLREconomyCalculator::ValidateVehiclePurchase(Rules, 119999, Owned, TEXT("volta_nine")), ELRPurchaseResult::InsufficientCredits));
		});

		It("refuses unknown vehicles", [this]()
		{
			const TArray<FName> Owned;
			TestTrue(TEXT("unknown"), IsResult(FLREconomyCalculator::ValidateVehiclePurchase(Rules, 1000000, Owned, TEXT("not_a_car")), ELRPurchaseResult::UnknownItem));
			TestTrue(TEXT("none"), IsResult(FLREconomyCalculator::ValidateVehiclePurchase(Rules, 1000000, Owned, NAME_None), ELRPurchaseResult::UnknownItem));
		});
	});

	Describe("ValidateUpgradePurchase", [this]()
	{
		It("checks ownership, level and credits", [this]()
		{
			TestTrue(TEXT("success"), IsResult(FLREconomyCalculator::ValidateUpgradePurchase(Rules, 2000, true, TEXT("volta_nine"), 0), ELRPurchaseResult::Success));
			TestTrue(TEXT("not owned"), IsResult(FLREconomyCalculator::ValidateUpgradePurchase(Rules, 2000, false, TEXT("volta_nine"), 0), ELRPurchaseResult::NotOwned));
			TestTrue(TEXT("insufficient"), IsResult(FLREconomyCalculator::ValidateUpgradePurchase(Rules, 1999, true, TEXT("volta_nine"), 0), ELRPurchaseResult::InsufficientCredits));
			TestTrue(TEXT("max level"), IsResult(FLREconomyCalculator::ValidateUpgradePurchase(Rules, 1000000, true, TEXT("volta_nine"), LRProgression::MaxUpgradeLevel), ELRPurchaseResult::MaxLevel));
		});
	});

	Describe("UpdateBestScore", [this]()
	{
		It("records the first score and only improves afterwards", [this]()
		{
			TMap<FName, int64> Best;
			const FName Key = LRProgression::GetModeKey(ELRGameModeType::EndlessOneWay);

			TestTrue(TEXT("first score recorded"), FLREconomyCalculator::UpdateBestScore(Best, Key, 1000));
			TestEqual(TEXT("best after first"), Best.FindRef(Key), int64(1000));
			TestFalse(TEXT("lower score ignored"), FLREconomyCalculator::UpdateBestScore(Best, Key, 500));
			TestFalse(TEXT("equal score ignored"), FLREconomyCalculator::UpdateBestScore(Best, Key, 1000));
			TestEqual(TEXT("best unchanged"), Best.FindRef(Key), int64(1000));
			TestTrue(TEXT("higher score recorded"), FLREconomyCalculator::UpdateBestScore(Best, Key, 2500));
			TestEqual(TEXT("best improved"), Best.FindRef(Key), int64(2500));
		});

		It("keeps modes separate and ignores empty scores", [this]()
		{
			TMap<FName, int64> Best;
			const FName OneWay = LRProgression::GetModeKey(ELRGameModeType::EndlessOneWay);
			const FName TwoWay = LRProgression::GetModeKey(ELRGameModeType::EndlessTwoWay);

			FLREconomyCalculator::UpdateBestScore(Best, OneWay, 1000);
			TestFalse(TEXT("zero score ignored"), FLREconomyCalculator::UpdateBestScore(Best, TwoWay, 0));
			TestFalse(TEXT("no record created"), Best.Contains(TwoWay));
			TestTrue(TEXT("two-way recorded"), FLREconomyCalculator::UpdateBestScore(Best, TwoWay, 300));
			TestEqual(TEXT("one-way untouched"), Best.FindRef(OneWay), int64(1000));
		});

		It("uses the enum name as mode key", [this]()
		{
			TestEqual(TEXT("key"), LRProgression::GetModeKey(ELRGameModeType::EndlessTwoWay), FName(TEXT("EndlessTwoWay")));
		});
	});

	Describe("Data", [this]()
	{
		It("parses an EconomyRules envelope", [this]()
		{
			const FString Json = TEXT(R"({"type":"EconomyRules","id":"test","version":1,"data":{
				"CreditsPerPoint":0.2,"StartingCredits":777,"StarterVehicle":"volta_nine",
				"VehiclePrices":[{"VehicleId":"volta_nine","Price":10,"UpgradeCostMultiplier":1.5}],
				"UpgradeLevelPrices":[1,2,3,4,5],
				"ModeMultipliers":[{"Mode":"FreeRide","Multiplier":0.5}]}})");

			FLREconomyRules Parsed;
			FString Error;
			const bool bParsed = FLRDataLoader::Parse(Json, Parsed, &Error);
			TestTrue(*FString::Printf(TEXT("parsed (%s)"), *Error), bParsed);
			TestEqual(TEXT("starting credits"), Parsed.StartingCredits, int64(777));
			TestEqual(TEXT("starter"), Parsed.StarterVehicle, FName(TEXT("volta_nine")));
			TestEqual(TEXT("prices"), Parsed.VehiclePrices.Num(), 1);
			TestEqual(TEXT("levels"), Parsed.UpgradeLevelPrices.Num(), 5);
			TestEqual(TEXT("free ride multiplier"), FLREconomyCalculator::GetModeMultiplier(Parsed, ELRGameModeType::FreeRide), 0.5f);
			TestEqual(TEXT("missing field keeps default"), Parsed.MinCreditsPerRun, int64(100));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
