// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRProgressionTypes.h"

struct FLRRunResult;

/**
 * Pure economy rules (no UObject, no world): credits earned by a run, prices and purchase validation.
 * All amounts are credits (int64). ULRProgressionSubsystem applies the results to the profile.
 */
struct LRPROGRESSION_API FLREconomyCalculator
{
	/**
	 * Credits earned by a finished run:
	 *   (FinalScore * CreditsPerPoint + DistanceKm * DistanceCreditsPerKm) * ModeMultiplier, floored,
	 * then raised to MinCreditsPerRun if the run scored or drove at all (an empty run earns 0).
	 * Negative inputs count as 0; the result is clamped to avoid int64 overflow.
	 */
	static int64 ComputeRunCredits(const FLREconomyRules& Rules, const FLRRunResult& Result, ELRGameModeType Mode);

	/** Credit multiplier of a mode (1.0 if not listed in Rules.ModeMultipliers). */
	static float GetModeMultiplier(const FLREconomyRules& Rules, ELRGameModeType Mode);

	/** Price entry of a vehicle, or nullptr if the vehicle is not sold. */
	static const FLRVehiclePrice* FindVehiclePrice(const FLREconomyRules& Rules, FName VehicleId);

	/** Price of a vehicle in credits, or INDEX_NONE (-1) if the vehicle is not sold. */
	static int64 GetVehiclePrice(const FLREconomyRules& Rules, FName VehicleId);

	/**
	 * Price to raise an upgrade from CurrentLevel to CurrentLevel + 1 on VehicleId:
	 * UpgradeLevelPrices[CurrentLevel] (last entry reused if the array is short) * vehicle UpgradeCostMultiplier.
	 * Returns INDEX_NONE (-1) when CurrentLevel is already at MaxUpgradeLevel or invalid.
	 */
	static int64 GetUpgradePrice(const FLREconomyRules& Rules, FName VehicleId, int32 CurrentLevel);

	/** Checks whether a vehicle can be bought: sold, not already owned, enough credits. */
	static ELRPurchaseResult ValidateVehiclePurchase(const FLREconomyRules& Rules, int64 Credits, const TArray<FName>& OwnedVehicles, FName VehicleId);

	/** Checks whether an upgrade level can be bought: vehicle owned, below max level, enough credits. */
	static ELRPurchaseResult ValidateUpgradePurchase(const FLREconomyRules& Rules, int64 Credits, bool bVehicleOwned, FName VehicleId, int32 CurrentLevel);

	/**
	 * Stores Score in BestScores[ModeKey] if it beats the previous best (or there is none).
	 * Scores <= 0 never create a record. Returns true if a new best was recorded.
	 */
	static bool UpdateBestScore(TMap<FName, int64>& BestScores, FName ModeKey, int64 Score);
};
