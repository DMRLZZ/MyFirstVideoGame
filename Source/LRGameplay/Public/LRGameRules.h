// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"
#include "Misc/DateTime.h"

#include "LRGameRules.generated.h"

struct FLRLaneDesc;

/** Gamepad rumble tunables, part of FLRGameRules. Intensities are normalized 0..1. */
USTRUCT(BlueprintType)
struct LRGAMEPLAY_API FLRRumbleTuning
{
	GENERATED_BODY()

	/** Intensity of a Close near miss. VeryClose gets twice this value and Razor three times (capped at 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rumble", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NearMissIntensity = 0.25f;

	/** Length of the near-miss rumble, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rumble", meta = (ClampMin = "0.0", Units = "Seconds"))
	float NearMissDurationSeconds = 0.15f;

	/** Intensity of a contact that is not severe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rumble", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScrapeIntensity = 0.4f;

	/** Length of the scrape rumble, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rumble", meta = (ClampMin = "0.0", Units = "Seconds"))
	float ScrapeDurationSeconds = 0.2f;

	/** Intensity of a severe crash. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rumble", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrashIntensity = 1.f;

	/** Length of the crash rumble, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rumble", meta = (ClampMin = "0.0", Units = "Seconds"))
	float CrashDurationSeconds = 0.6f;
};

/**
 * Rules of a run, loaded from Data/Gameplay/GameRules.json (envelope type "GameRules").
 * Owned by value by ALRGameModeEndless. ALRHUD reads CountdownSeconds from the same file.
 */
USTRUCT(BlueprintType)
struct LRGAMEPLAY_API FLRGameRules
{
	GENERATED_BODY()

	/** Path of the data file, relative to Data/. */
	static const TCHAR* DataPath;

	/** Length of the countdown before a run, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.0", Units = "Seconds"))
	float CountdownSeconds = 3.f;

	/** Time between the crash and the results screen, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.0", Units = "Seconds"))
	float ResultsDelaySeconds = 2.5f;

	/** Rolling start speed of a run, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.0"))
	float StartSpeedKmh = 90.f;

	/** Vehicle used when the selected one cannot be loaded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules")
	FName DefaultVehicleId = FName(TEXT("bauer_r3_sport"));

	/** "Forgiving arcade": impacts consume scrapes before they end the run (docs/04-GAMEPLAY.md §5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules")
	bool bForgivingArcade = false;

	/** Forgiving arcade: impacts above ScrapeMaxImpactKmh that the run survives. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0"))
	int32 ScrapesAllowed = 2;

	/** Forgiving arcade: impacts up to this speed are harmless and consume nothing, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.0"))
	float ScrapeMaxImpactKmh = 25.f;

	/** Forgiving arcade: impacts from this speed end the run even with scrapes left, km/h. 0 disables the limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.0"))
	float ForgivingFatalImpactKmh = 120.f;

	/** Start lane relative to ULRRoadSubsystem::GetDefaultPlayerLane(), lanes (+ = right). Never lands on an oncoming lane. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules")
	int32 StartingLaneOffset = 0;

	/** Time after a severe impact that did not end the run before the car (free ride) and the engine sound recover, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.01", Units = "Seconds"))
	float CrashRecoverySeconds = 1.5f;

	/** Free ride: how far ahead of the crash the car reappears, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.0", Units = "Meters"))
	float FreeRideResetForwardMeters = 30.f;

	/** Title screen: speed the player car cruises at behind the menu, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.0"))
	float TitleCruiseKmh = 100.f;

	/** Title screen: gap the cruising car keeps to the vehicle ahead in its lane, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "5.0", Units = "Meters"))
	float TitleFollowGapMeters = 45.f;

	/** Gamepad rumble. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules")
	FLRRumbleTuning Rumble;

	/** Returns a copy with every field in a safe range. */
	FLRGameRules Sanitized() const;
};

/** What a contact reported by the vehicle means for the run. */
enum class ELRCrashOutcome : uint8
{
	/** Light contact: the run goes on and nothing is consumed (the combo still breaks). */
	Scrape,
	/** Forgiving arcade: the run goes on but one scrape is consumed. */
	DamageScrape,
	/** Free ride: the car is put back on the road. */
	ResetCar,
	/** The run ends. */
	EndRun
};

/** Pure rules of a run (no UObject, no world). */
struct LRGAMEPLAY_API FLRGameRulesLogic
{
	/**
	 * Decides what a contact means.
	 * @param bFreeRide       The current mode is free ride: it never ends the run, a severe impact resets the car.
	 * @param bSevere         FLRCrashEvent::bSevere (impact >= the vehicle's CrashImpactKmh).
	 * @param ImpactSpeedKmh  FLRCrashEvent::ImpactSpeedKmh.
	 * @param ScrapesUsed     Scrapes consumed so far in this run.
	 */
	static ELRCrashOutcome EvaluateCrash(const FLRGameRules& Rules, bool bFreeRide, bool bSevere, float ImpactSpeedKmh, int32 ScrapesUsed);

	/**
	 * Applies StartingLaneOffset to the default lane: clamped to the road and moved back towards the default lane if it
	 * lands on an oncoming lane. Returns INDEX_NONE without lanes.
	 */
	static int32 ResolveStartLane(const TArray<FLRLaneDesc>& Lanes, int32 DefaultLane, int32 LaneOffset);

	/** Parses a mode selectable with lr.Mode: EndlessOneWay, EndlessTwoWay, FreeRide or Daily (case-insensitive). */
	static bool ParseGameMode(const FString& Text, ELRGameModeType& OutMode);

	/** Parses a control scheme: Arcade or Simcade (case-insensitive). */
	static bool ParseControlScheme(const FString& Text, ELRControlScheme& OutScheme);

	/** Seed of a new run: LRRandom::DailySeed of the UTC date in Daily mode, derived from the clock otherwise. */
	static int32 MakeRunSeed(ELRGameModeType Mode, const FDateTime& UtcNow);
};
