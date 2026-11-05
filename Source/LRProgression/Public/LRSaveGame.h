// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "LRProgressionTypes.h"

#include "LRSaveGame.generated.h"

/**
 * Persistent player profile (slot LRProgression::ProfileSlotName).
 *
 * Versioned: SaveVersion is the format of the data in memory. Older profiles are upgraded step by step
 * by LRSaveMigration::Migrate right after loading. When changing the layout, bump
 * LRSaveMigration::CurrentVersion and add one migration step (see LRSaveMigration.h).
 *
 * Owned by ULRProgressionSubsystem; other systems read it through the subsystem API.
 */
UCLASS()
class LRPROGRESSION_API ULRSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Format version of this profile. New profiles are created at LRSaveMigration::CurrentVersion. */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Profile")
	int32 SaveVersion = 0;

	/** Spendable credits (never negative). */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Profile")
	int64 Credits = 0;

	/** Vehicle ids in the garage, unique. */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Profile")
	TArray<FName> OwnedVehicles;

	/** Vehicle used for the next run; always one of OwnedVehicles once the profile is sanitized. */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Profile")
	FName SelectedVehicle;

	/** Upgrade levels per vehicle id. Missing entry = stock vehicle. */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Profile")
	TMap<FName, FLRVehicleUpgradeState> Upgrades;

	/** Best score per game mode, keyed by LRProgression::GetModeKey (ELRGameModeType name). */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Profile")
	TMap<FName, int64> BestScores;

	/** Lifetime distance driven over every recorded run, meters. */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Stats")
	double TotalDistanceMeters = 0.0;

	/** Lifetime number of recorded runs. */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Stats")
	int32 TotalRuns = 0;

	/** Lifetime number of near misses over every recorded run. */
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "Stats")
	int32 TotalNearMisses = 0;

	/** True if VehicleId is in OwnedVehicles. */
	bool OwnsVehicle(FName VehicleId) const { return OwnedVehicles.Contains(VehicleId); }
};
