// Copyright LEFTRIGHT. All Rights Reserved.

#include "LREconomyCalculator.h"

#include "LREvents.h"

namespace LREconomyCalculatorPrivate
{
	/** Upper bound for any computed amount; far below INT64_MAX so later sums cannot overflow. */
	constexpr double MaxCredits = 1.0e15;

	int64 ToCredits(double Value)
	{
		return static_cast<int64>(FMath::Clamp(FMath::FloorToDouble(Value), 0.0, MaxCredits));
	}
}

float FLREconomyCalculator::GetModeMultiplier(const FLREconomyRules& Rules, ELRGameModeType Mode)
{
	for (const FLRModeCreditMultiplier& Entry : Rules.ModeMultipliers)
	{
		if (Entry.Mode == Mode)
		{
			return FMath::Max(Entry.Multiplier, 0.f);
		}
	}
	return 1.f;
}

int64 FLREconomyCalculator::ComputeRunCredits(const FLREconomyRules& Rules, const FLRRunResult& Result, ELRGameModeType Mode)
{
	const double Score = static_cast<double>(FMath::Max<int64>(Result.FinalScore, 0));
	const double DistanceKm = FMath::Max(static_cast<double>(Result.DistanceMeters), 0.0) / 1000.0;

	if (Score <= 0.0 && DistanceKm <= 0.0)
	{
		return 0;
	}

	const double Raw = (Score * FMath::Max(static_cast<double>(Rules.CreditsPerPoint), 0.0)
		+ DistanceKm * FMath::Max(static_cast<double>(Rules.DistanceCreditsPerKm), 0.0))
		* static_cast<double>(GetModeMultiplier(Rules, Mode));

	const int64 Credits = LREconomyCalculatorPrivate::ToCredits(Raw);
	return FMath::Max(Credits, FMath::Max<int64>(Rules.MinCreditsPerRun, 0));
}

const FLRVehiclePrice* FLREconomyCalculator::FindVehiclePrice(const FLREconomyRules& Rules, FName VehicleId)
{
	if (VehicleId.IsNone())
	{
		return nullptr;
	}
	return Rules.VehiclePrices.FindByPredicate([VehicleId](const FLRVehiclePrice& Entry)
	{
		return Entry.VehicleId == VehicleId;
	});
}

int64 FLREconomyCalculator::GetVehiclePrice(const FLREconomyRules& Rules, FName VehicleId)
{
	const FLRVehiclePrice* Entry = FindVehiclePrice(Rules, VehicleId);
	return Entry ? FMath::Max<int64>(Entry->Price, 0) : INDEX_NONE;
}

int64 FLREconomyCalculator::GetUpgradePrice(const FLREconomyRules& Rules, FName VehicleId, int32 CurrentLevel)
{
	if (CurrentLevel < 0 || CurrentLevel >= LRProgression::MaxUpgradeLevel)
	{
		return INDEX_NONE;
	}

	int64 BasePrice = 0;
	if (Rules.UpgradeLevelPrices.Num() > 0)
	{
		BasePrice = Rules.UpgradeLevelPrices[FMath::Min(CurrentLevel, Rules.UpgradeLevelPrices.Num() - 1)];
	}

	const FLRVehiclePrice* Entry = FindVehiclePrice(Rules, VehicleId);
	const double Multiplier = Entry ? FMath::Max(static_cast<double>(Entry->UpgradeCostMultiplier), 0.0) : 1.0;
	return LREconomyCalculatorPrivate::ToCredits(static_cast<double>(FMath::Max<int64>(BasePrice, 0)) * Multiplier);
}

ELRPurchaseResult FLREconomyCalculator::ValidateVehiclePurchase(const FLREconomyRules& Rules, int64 Credits, const TArray<FName>& OwnedVehicles, FName VehicleId)
{
	const int64 Price = GetVehiclePrice(Rules, VehicleId);
	if (Price == INDEX_NONE)
	{
		return ELRPurchaseResult::UnknownItem;
	}
	if (OwnedVehicles.Contains(VehicleId))
	{
		return ELRPurchaseResult::AlreadyOwned;
	}
	if (Credits < Price)
	{
		return ELRPurchaseResult::InsufficientCredits;
	}
	return ELRPurchaseResult::Success;
}

ELRPurchaseResult FLREconomyCalculator::ValidateUpgradePurchase(const FLREconomyRules& Rules, int64 Credits, bool bVehicleOwned, FName VehicleId, int32 CurrentLevel)
{
	if (VehicleId.IsNone())
	{
		return ELRPurchaseResult::UnknownItem;
	}
	if (!bVehicleOwned)
	{
		return ELRPurchaseResult::NotOwned;
	}
	const int64 Price = GetUpgradePrice(Rules, VehicleId, CurrentLevel);
	if (Price == INDEX_NONE)
	{
		return ELRPurchaseResult::MaxLevel;
	}
	if (Credits < Price)
	{
		return ELRPurchaseResult::InsufficientCredits;
	}
	return ELRPurchaseResult::Success;
}

bool FLREconomyCalculator::UpdateBestScore(TMap<FName, int64>& BestScores, FName ModeKey, int64 Score)
{
	if (ModeKey.IsNone() || Score <= 0)
	{
		return false;
	}

	int64& Best = BestScores.FindOrAdd(ModeKey, int64(0));
	if (Score > Best)
	{
		Best = Score;
		return true;
	}
	return false;
}
