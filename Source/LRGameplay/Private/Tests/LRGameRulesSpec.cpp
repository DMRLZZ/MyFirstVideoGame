// Copyright LEFTRIGHT. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LRDataLoader.h"
#include "LRGameRules.h"
#include "LRRandom.h"
#include "LRRoadTypes.h"

namespace LRGameRulesSpecPrivate
{
	/** Two oncoming lanes on the left and two player lanes on the right. */
	TArray<FLRLaneDesc> MakeTwoWayLanes()
	{
		TArray<FLRLaneDesc> Lanes;
		Lanes.SetNum(4);
		Lanes[0].Direction = -1;
		Lanes[1].Direction = -1;
		Lanes[2].Direction = 1;
		Lanes[3].Direction = 1;
		return Lanes;
	}
}

BEGIN_DEFINE_SPEC(FLRGameRulesSpec, "LeftRight.Gameplay.GameRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRGameRulesSpec)

void FLRGameRulesSpec::Define()
{
	using namespace LRGameRulesSpecPrivate;

	Describe("Parsing", [this]()
	{
		It("has the defaults of the design document", [this]()
		{
			const FLRGameRules Rules;
			TestEqual(TEXT("countdown"), Rules.CountdownSeconds, 3.f);
			TestEqual(TEXT("results delay"), Rules.ResultsDelaySeconds, 2.5f);
			TestEqual(TEXT("start speed"), Rules.StartSpeedKmh, 90.f);
			TestEqual(TEXT("vehicle"), Rules.DefaultVehicleId, FName(TEXT("bauer_r3_sport")));
			TestFalse(TEXT("forgiving"), Rules.bForgivingArcade);
			TestEqual(TEXT("scrapes"), Rules.ScrapesAllowed, 2);
		});

		It("reads every kind of field and keeps the default of the missing ones", [this]()
		{
			const FString Json = TEXT(R"json({
				"type": "GameRules", "id": "test", "version": 1,
				"data": {
					"CountdownSeconds": 1.5,
					"DefaultVehicleId": "volta_nine",
					"bForgivingArcade": true,
					"ScrapesAllowed": 4,
					"StartingLaneOffset": -1,
					"Rumble": { "CrashIntensity": 0.5 }
				}
			})json");

			FLRGameRules Rules;
			TestTrue(TEXT("parses"), FLRDataLoader::Parse(Json, Rules));
			TestEqual(TEXT("float"), Rules.CountdownSeconds, 1.5f);
			TestEqual(TEXT("name"), Rules.DefaultVehicleId, FName(TEXT("volta_nine")));
			TestTrue(TEXT("bool"), Rules.bForgivingArcade);
			TestEqual(TEXT("int"), Rules.ScrapesAllowed, 4);
			TestEqual(TEXT("negative int"), Rules.StartingLaneOffset, -1);
			TestEqual(TEXT("nested"), Rules.Rumble.CrashIntensity, 0.5f);
			TestEqual(TEXT("missing field"), Rules.StartSpeedKmh, 90.f);
			TestEqual(TEXT("missing nested field"), Rules.Rumble.CrashDurationSeconds, 0.6f);
		});

		It("loads the shipped file", [this]()
		{
			FLRGameRules Rules;
			TestTrue(TEXT("loads"), FLRDataLoader::Load(FLRGameRules::DataPath, Rules));
			TestFalse(TEXT("has a vehicle"), Rules.DefaultVehicleId.IsNone());
		});

		It("sanitizes out of range values", [this]()
		{
			FLRGameRules Rules;
			Rules.CountdownSeconds = -1.f;
			Rules.ScrapesAllowed = -3;
			Rules.Rumble.CrashIntensity = 7.f;
			const FLRGameRules Safe = Rules.Sanitized();
			TestEqual(TEXT("countdown"), Safe.CountdownSeconds, 0.f);
			TestEqual(TEXT("scrapes"), Safe.ScrapesAllowed, 0);
			TestEqual(TEXT("rumble"), Safe.Rumble.CrashIntensity, 1.f);
		});
	});

	Describe("EvaluateCrash", [this]()
	{
		It("ends the run on a severe impact in the normal rules", [this]()
		{
			const FLRGameRules Rules;
			TestTrue(TEXT("severe"), FLRGameRulesLogic::EvaluateCrash(Rules, false, true, 30.f, 0) == ELRCrashOutcome::EndRun);
			TestTrue(TEXT("light"), FLRGameRulesLogic::EvaluateCrash(Rules, false, false, 10.f, 0) == ELRCrashOutcome::Scrape);
		});

		It("consumes scrapes before ending the run in forgiving arcade", [this]()
		{
			FLRGameRules Rules;
			Rules.bForgivingArcade = true;
			Rules.ScrapesAllowed = 2;
			Rules.ScrapeMaxImpactKmh = 25.f;
			Rules.ForgivingFatalImpactKmh = 120.f;

			TestTrue(TEXT("harmless"), FLRGameRulesLogic::EvaluateCrash(Rules, false, false, 20.f, 2) == ELRCrashOutcome::Scrape);
			TestTrue(TEXT("first"), FLRGameRulesLogic::EvaluateCrash(Rules, false, true, 60.f, 0) == ELRCrashOutcome::DamageScrape);
			TestTrue(TEXT("second"), FLRGameRulesLogic::EvaluateCrash(Rules, false, true, 60.f, 1) == ELRCrashOutcome::DamageScrape);
			TestTrue(TEXT("none left"), FLRGameRulesLogic::EvaluateCrash(Rules, false, true, 60.f, 2) == ELRCrashOutcome::EndRun);
			TestTrue(TEXT("too hard"), FLRGameRulesLogic::EvaluateCrash(Rules, false, true, 150.f, 0) == ELRCrashOutcome::EndRun);

			Rules.ForgivingFatalImpactKmh = 0.f;
			TestTrue(TEXT("limit disabled"), FLRGameRulesLogic::EvaluateCrash(Rules, false, true, 150.f, 0) == ELRCrashOutcome::DamageScrape);
		});

		It("never ends the run in free ride", [this]()
		{
			FLRGameRules Rules;
			TestTrue(TEXT("severe"), FLRGameRulesLogic::EvaluateCrash(Rules, true, true, 200.f, 0) == ELRCrashOutcome::ResetCar);
			TestTrue(TEXT("light"), FLRGameRulesLogic::EvaluateCrash(Rules, true, false, 10.f, 0) == ELRCrashOutcome::Scrape);

			Rules.bForgivingArcade = true;
			TestTrue(TEXT("forgiving is ignored"), FLRGameRulesLogic::EvaluateCrash(Rules, true, true, 200.f, 99) == ELRCrashOutcome::ResetCar);
		});
	});

	Describe("ResolveStartLane", [this]()
	{
		It("applies and clamps the offset", [this]()
		{
			TArray<FLRLaneDesc> Lanes;
			Lanes.SetNum(4);
			TestEqual(TEXT("no offset"), FLRGameRulesLogic::ResolveStartLane(Lanes, 1, 0), 1);
			TestEqual(TEXT("right"), FLRGameRulesLogic::ResolveStartLane(Lanes, 1, 1), 2);
			TestEqual(TEXT("past the right edge"), FLRGameRulesLogic::ResolveStartLane(Lanes, 1, 9), 3);
			TestEqual(TEXT("past the left edge"), FLRGameRulesLogic::ResolveStartLane(Lanes, 1, -9), 0);
		});

		It("never lands on an oncoming lane", [this]()
		{
			const TArray<FLRLaneDesc> Lanes = MakeTwoWayLanes();
			TestEqual(TEXT("one to the left"), FLRGameRulesLogic::ResolveStartLane(Lanes, 2, -1), 2);
			TestEqual(TEXT("far to the left"), FLRGameRulesLogic::ResolveStartLane(Lanes, 3, -9), 2);
			TestEqual(TEXT("to the right"), FLRGameRulesLogic::ResolveStartLane(Lanes, 2, 1), 3);
		});

		It("returns no lane on an empty road", [this]()
		{
			TestEqual(TEXT("empty"), FLRGameRulesLogic::ResolveStartLane(TArray<FLRLaneDesc>(), 2, 0), static_cast<int32>(INDEX_NONE));
		});
	});

	Describe("Console arguments", [this]()
	{
		It("parses the modes of lr.Mode", [this]()
		{
			ELRGameModeType Mode = ELRGameModeType::TimeAttack;
			TestTrue(TEXT("two way"), FLRGameRulesLogic::ParseGameMode(TEXT("EndlessTwoWay"), Mode));
			TestTrue(TEXT("value"), Mode == ELRGameModeType::EndlessTwoWay);
			TestTrue(TEXT("case-insensitive"), FLRGameRulesLogic::ParseGameMode(TEXT("freeride"), Mode));
			TestTrue(TEXT("value"), Mode == ELRGameModeType::FreeRide);
			TestFalse(TEXT("time attack is not available"), FLRGameRulesLogic::ParseGameMode(TEXT("TimeAttack"), Mode));
			TestFalse(TEXT("unknown"), FLRGameRulesLogic::ParseGameMode(TEXT("Foo"), Mode));
		});

		It("parses the control schemes", [this]()
		{
			ELRControlScheme Scheme = ELRControlScheme::Arcade;
			TestTrue(TEXT("simcade"), FLRGameRulesLogic::ParseControlScheme(TEXT("Simcade"), Scheme));
			TestTrue(TEXT("value"), Scheme == ELRControlScheme::Simcade);
			TestFalse(TEXT("unknown"), FLRGameRulesLogic::ParseControlScheme(TEXT("Foo"), Scheme));
		});
	});

	Describe("MakeRunSeed", [this]()
	{
		It("gives the same seed all day in Daily mode", [this]()
		{
			const FDateTime Morning(2026, 11, 5, 1, 0, 0);
			const FDateTime Night(2026, 11, 5, 23, 0, 0);
			const FDateTime NextDay(2026, 11, 6, 1, 0, 0);
			TestEqual(TEXT("same day"), FLRGameRulesLogic::MakeRunSeed(ELRGameModeType::Daily, Morning), FLRGameRulesLogic::MakeRunSeed(ELRGameModeType::Daily, Night));
			TestEqual(TEXT("matches LRRandom"), FLRGameRulesLogic::MakeRunSeed(ELRGameModeType::Daily, Morning), LRRandom::DailySeed(2026, 11, 5));
			TestNotEqual(TEXT("next day"), FLRGameRulesLogic::MakeRunSeed(ELRGameModeType::Daily, Morning), FLRGameRulesLogic::MakeRunSeed(ELRGameModeType::Daily, NextDay));
		});

		It("changes with the clock in the other modes", [this]()
		{
			const FDateTime First(2026, 11, 5, 1, 0, 0);
			const FDateTime Second(2026, 11, 5, 1, 0, 1);
			TestNotEqual(TEXT("seeds"), FLRGameRulesLogic::MakeRunSeed(ELRGameModeType::EndlessOneWay, First), FLRGameRulesLogic::MakeRunSeed(ELRGameModeType::EndlessOneWay, Second));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
