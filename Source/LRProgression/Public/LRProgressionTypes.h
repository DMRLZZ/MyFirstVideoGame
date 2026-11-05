// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"

#include "LRProgressionTypes.generated.h"

/** Upgrade categories available on every vehicle (docs/04-GAMEPLAY.md §7). */
UENUM(BlueprintType)
enum class ELRUpgradeType : uint8
{
	Engine,
	/** Turbo or supercharger, depending on the car. */
	Turbo,
	Transmission,
	Tires,
	Brakes,
	Suspension,
	Aero,
	/** Number of categories. Not a valid upgrade. */
	Count UMETA(Hidden)
};

/** Outcome of a purchase attempt (vehicle or upgrade). */
UENUM(BlueprintType)
enum class ELRPurchaseResult : uint8
{
	/** The purchase is allowed (validation) or was completed (subsystem). */
	Success,
	/** The vehicle is already in the garage. */
	AlreadyOwned,
	/** The player does not have enough credits. */
	InsufficientCredits,
	/** The item id is not listed in the economy rules. */
	UnknownItem,
	/** Upgrades can only be bought for owned vehicles. */
	NotOwned,
	/** The upgrade is already at the maximum level. */
	MaxLevel
};

/** Constants shared by the progression code. */
namespace LRProgression
{
	/** Highest level of any upgrade category (levels go 0..MaxUpgradeLevel). */
	inline constexpr int32 MaxUpgradeLevel = 5;

	/** Number of upgrade categories. */
	inline constexpr int32 NumUpgradeTypes = static_cast<int32>(ELRUpgradeType::Count);

	/** Save slot of the player profile. */
	inline constexpr const TCHAR* ProfileSlotName = TEXT("LeftRight_Profile");

	/** Platform user index used for the profile slot. */
	inline constexpr int32 ProfileUserIndex = 0;

	/** Key used to store best scores per mode: the ELRGameModeType enumerator name (e.g. "EndlessOneWay"). */
	LRPROGRESSION_API FName GetModeKey(ELRGameModeType Mode);
}

/**
 * Upgrade levels of one vehicle. Each field is a level in [0, LRProgression::MaxUpgradeLevel];
 * 0 = stock. Stored per vehicle id in ULRSaveGame::Upgrades. LRVehicle maps levels to physics parameters.
 */
USTRUCT(BlueprintType)
struct LRPROGRESSION_API FLRVehicleUpgradeState
{
	GENERATED_BODY()

	/** Engine tune level (power / torque). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Upgrades", meta = (ClampMin = "0", ClampMax = "5"))
	int32 Engine = 0;

	/** Turbo / supercharger level (boost). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Upgrades", meta = (ClampMin = "0", ClampMax = "5"))
	int32 Turbo = 0;

	/** Transmission level (shift time, ratios). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Upgrades", meta = (ClampMin = "0", ClampMax = "5"))
	int32 Transmission = 0;

	/** Tire compound level (grip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Upgrades", meta = (ClampMin = "0", ClampMax = "5"))
	int32 Tires = 0;

	/** Brake level (brake torque, fade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Upgrades", meta = (ClampMin = "0", ClampMax = "5"))
	int32 Brakes = 0;

	/** Suspension level (stiffness, ride height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Upgrades", meta = (ClampMin = "0", ClampMax = "5"))
	int32 Suspension = 0;

	/** Aerodynamics level (downforce vs drag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Upgrades", meta = (ClampMin = "0", ClampMax = "5"))
	int32 Aero = 0;

	/** Level of one category (0 for ELRUpgradeType::Count). */
	int32 GetLevel(ELRUpgradeType Type) const;

	/** Sets the level of one category, clamped to [0, MaxUpgradeLevel]. Ignores ELRUpgradeType::Count. */
	void SetLevel(ELRUpgradeType Type, int32 Level);

	/** Clamps every category to [0, MaxUpgradeLevel]. Returns true if any value changed. */
	bool ClampLevels();

	/** True when every category is at level 0. */
	bool IsStock() const;

	bool operator==(const FLRVehicleUpgradeState& Other) const = default;
};

/** Price entry of a vehicle in the dealership (Data/Economy/Economy.json). */
USTRUCT(BlueprintType)
struct LRPROGRESSION_API FLRVehiclePrice
{
	GENERATED_BODY()

	/** Vehicle id, matches Data/Vehicles/<VehicleId>.json. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	FName VehicleId;

	/** Price in credits (0 = free). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	int64 Price = 0;

	/** Multiplier applied to FLREconomyRules::UpgradeLevelPrices for this vehicle (expensive cars, expensive parts). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	float UpgradeCostMultiplier = 1.f;
};

/** Credit multiplier for one game mode. Modes not listed use 1.0. */
USTRUCT(BlueprintType)
struct LRPROGRESSION_API FLRModeCreditMultiplier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	ELRGameModeType Mode = ELRGameModeType::EndlessOneWay;

	/** Multiplier applied to the credits of a run in this mode (before MinCreditsPerRun). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	float Multiplier = 1.f;
};

/**
 * Economy tuning, loaded from Data/Economy/Economy.json (type "EconomyRules").
 * Credits are integers (int64). Every field has a sensible default so a missing file still yields a playable game.
 */
USTRUCT(BlueprintType)
struct LRPROGRESSION_API FLREconomyRules
{
	GENERATED_BODY()

	/** Credits per score point (e.g. 0.1 = 1 credit every 10 points). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	float CreditsPerPoint = 0.1f;

	/** Minimum credits awarded by any finished run that scored or drove at all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	int64 MinCreditsPerRun = 100;

	/** Credits per kilometer driven during the run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	float DistanceCreditsPerKm = 50.f;

	/** Credits of a brand new profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	int64 StartingCredits = 5000;

	/** Vehicle granted (owned and selected) to a brand new profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	FName StarterVehicle = TEXT("bauer_r3_sport");

	/** Dealership price list. Vehicles not listed cannot be bought. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	TArray<FLRVehiclePrice> VehiclePrices;

	/**
	 * Base price to go from level i to level i+1 of any upgrade category (index i = current level).
	 * Should hold LRProgression::MaxUpgradeLevel entries; missing entries reuse the last one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	TArray<int64> UpgradeLevelPrices = { 5000, 10000, 20000, 35000, 60000 };

	/** Optional per-mode credit multipliers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	TArray<FLRModeCreditMultiplier> ModeMultipliers;
};
