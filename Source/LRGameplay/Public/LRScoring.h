// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"

#include "LRScoring.generated.h"

/**
 * One row of the speed multiplier table (docs/04-GAMEPLAY.md §5).
 * The tier applies from MinKmh (inclusive) up to the MinKmh of the next tier.
 */
USTRUCT(BlueprintType)
struct LRGAMEPLAY_API FLRSpeedTier
{
	GENERATED_BODY()

	/** Lowest speed of this tier, km/h (inclusive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring", meta = (ClampMin = "0.0"))
	float MinKmh = 0.f;

	/** Multiplier applied to the distance points earned in this tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring", meta = (ClampMin = "0.0"))
	float Multiplier = 1.f;
};

/**
 * Scoring tunables, loaded from Data/Gameplay/Scoring.json (envelope type "ScoringRules").
 * The C++ defaults are the values of docs/04-GAMEPLAY.md §5. Owned by value by FLRScoreCalculator.
 */
USTRUCT(BlueprintType)
struct LRGAMEPLAY_API FLRScoringRules
{
	GENERATED_BODY()

	/** Fills SpeedTiers with the default table. */
	FLRScoringRules();

	/** Path of the data file, relative to Data/. */
	static const TCHAR* DataPath;

	/** Meters to drive for one distance point (before the speed multiplier). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Distance", meta = (ClampMin = "0.01", Units = "Meters"))
	float MetersPerDistancePoint = 10.f;

	/** Speed multiplier table. Sorted by MinKmh ascending by Sanitized(). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Distance")
	TArray<FLRSpeedTier> SpeedTiers;

	/** Points of a near miss before any factor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "0.0"))
	float NearMissBasePoints = 250.f;

	/** Factor of ELRNearMissTier::Close. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "0.0"))
	float CloseFactor = 1.f;

	/** Factor of ELRNearMissTier::VeryClose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "0.0"))
	float VeryCloseFactor = 2.f;

	/** Factor of ELRNearMissTier::Razor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "0.0"))
	float RazorFactor = 3.f;

	/** Relative speed that gives a relative speed factor of 1, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "1.0"))
	float RelativeSpeedReferenceKmh = 100.f;

	/** Lower clamp of the relative speed factor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "0.0"))
	float RelativeSpeedFactorMin = 0.5f;

	/** Upper clamp of the relative speed factor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "0.0"))
	float RelativeSpeedFactorMax = 3.f;

	/** Factor against a vehicle driving in the opposite direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "0.0"))
	float OncomingFactor = 2.f;

	/** Factor when passing between two vehicles at the same time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|NearMiss", meta = (ClampMin = "0.0"))
	float ThreadTheNeedleFactor = 2.f;

	/** Seconds a combo stays alive after a near miss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Combo", meta = (ClampMin = "0.1", Units = "Seconds"))
	float ComboWindowSeconds = 3.f;

	/** Combo multiplier gained per chained near miss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Combo", meta = (ClampMin = "0.0"))
	float ComboStepPerChain = 0.25f;

	/** Upper limit of the combo multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Combo", meta = (ClampMin = "1.0"))
	float ComboMaxMultiplier = 5.f;

	/** Below this player speed the combo breaks and cannot grow, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Combo", meta = (ClampMin = "0.0"))
	float ComboBreakSpeedKmh = 120.f;

	/** Returns a copy with safe ranges, tiers sorted by MinKmh and at least one tier. */
	FLRScoringRules Sanitized() const;
};

/** What happened to the combo during FLRScoreCalculator::Tick. */
enum class ELRComboTickResult : uint8
{
	/** The chain did not end this tick. */
	None,
	/** The combo window ran out. */
	Expired,
	/** The player speed dropped below ComboBreakSpeedKmh. */
	Broken
};

/** Mutable statistics of one run. Plain data owned by FLRScoreCalculator. */
struct LRGAMEPLAY_API FLRScoreState
{
	/** Points earned so far. */
	int64 Score = 0;

	/** Distance points not yet converted to whole points. */
	double FractionalDistancePoints = 0.0;

	/** Distance driven, meters. */
	double DistanceMeters = 0.0;

	/** Seconds of Tick time accumulated. */
	float DurationSeconds = 0.f;

	/** Highest speed seen by Tick, km/h. */
	float TopSpeedKmh = 0.f;

	/** Near misses that counted. */
	int32 NearMisses = 0;

	/** Current number of chained near misses. 0 = no combo. */
	int32 Chain = 0;

	/** Longest chain of the run. */
	int32 BestChain = 0;

	/** Seconds left before the chain expires. */
	float ComboTimeRemaining = 0.f;
};

/**
 * Pure scoring logic of a run (no UObject, no world, no allocation after Configure). Owned by ULRScoreComponent.
 *
 * Combo convention: the multiplier always equals 1 + ComboStepPerChain * Chain (capped). A near miss is paid with the
 * multiplier in force when it happens and then grows the chain, so the first near miss pays x1 and the next one, if it
 * arrives inside the window, pays x1.25. The multiplier shown to the player is the one the next near miss gets.
 */
struct LRGAMEPLAY_API FLRScoreCalculator
{
	/** Replaces the rules (sanitized copy). The run statistics are kept, so data can be hot reloaded during a run. */
	void Configure(const FLRScoringRules& InRules);

	/** Clears the run statistics and enables points again. Keeps the rules. */
	void Reset();

	/** With points disabled (free ride) distance and top speed are still tracked but nothing scores. */
	void SetPointsEnabled(bool bEnabled) { bPointsEnabled = bEnabled; }

	/** True when distance and near misses earn points. */
	bool ArePointsEnabled() const { return bPointsEnabled; }

	/**
	 * Adds driven distance.
	 * @param Meters    Distance driven since the last call, meters. Values <= 0 are ignored.
	 * @param SpeedKmh  Current player speed, km/h. Selects the speed multiplier.
	 * @return Whole points added to the score by this call.
	 */
	int64 AddDistance(double Meters, float SpeedKmh);

	/**
	 * Scores a near miss with the current combo multiplier, then grows the chain and restarts the combo window.
	 * The chain does not grow when PlayerSpeedKmh is below ComboBreakSpeedKmh.
	 * @param RelativeSpeedKmh  Absolute relative speed of the pass, km/h.
	 * @param PlayerSpeedKmh    Current player speed, km/h.
	 * @return Points added to the score (0 for ELRNearMissTier::None or with points disabled).
	 */
	int64 ApplyNearMiss(ELRNearMissTier Tier, float RelativeSpeedKmh, bool bOncoming, bool bThreadTheNeedle, float PlayerSpeedKmh);

	/** Advances the duration, the top speed and the combo timer. Breaks the combo when SpeedKmh is below the limit. */
	ELRComboTickResult Tick(float DeltaSeconds, float SpeedKmh);

	/** Ends the current chain (scrape, slowdown). Returns true if there was a chain to break. */
	bool BreakCombo();

	/** Index in the rule table of the speed tier for SpeedKmh. */
	int32 GetSpeedTierIndex(float SpeedKmh) const { return FindSpeedTierIndex(Rules, SpeedKmh); }

	/** Distance multiplier for SpeedKmh. */
	float GetSpeedMultiplier(float SpeedKmh) const;

	/** Current combo multiplier (>= 1). */
	float GetComboMultiplier() const { return ComputeComboMultiplier(Rules, State.Chain); }

	/** Statistics of the run. */
	const FLRScoreState& GetState() const { return State; }

	/** Sanitized rules in use. */
	const FLRScoringRules& GetRules() const { return Rules; }

	/** Tier lookup: index of the last tier whose MinKmh <= SpeedKmh (0 below the first one). Expects sanitized rules. */
	static int32 FindSpeedTierIndex(const FLRScoringRules& InRules, float SpeedKmh);

	/** Near-miss formula without the combo: base x tier x clamp(rel / reference) x oncoming x thread. */
	static double ComputeNearMissPoints(const FLRScoringRules& InRules, ELRNearMissTier Tier, float RelativeSpeedKmh, bool bOncoming, bool bThreadTheNeedle);

	/** Multiplier of a chain: min(1 + step * chain, max). */
	static float ComputeComboMultiplier(const FLRScoringRules& InRules, int32 Chain);

private:
	FLRScoringRules Rules = FLRScoringRules().Sanitized();
	FLRScoreState State;
	bool bPointsEnabled = true;
};
