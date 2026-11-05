// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"
#include "LRProgressionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "LRProgressionSubsystem.generated.h"

class ULRSaveGame;
struct FLRRunResult;

/**
 * Owner of the player profile and the economy (credits, garage, upgrades, records).
 *
 * - Loads FLREconomyRules from Data/Economy/Economy.json and re-reads it on ULRDataSubsystem::OnDataReloaded.
 * - Loads the profile from slot "LeftRight_Profile" on Initialize (migrating old versions) or creates a fresh one
 *   with StartingCredits and the starter vehicle.
 * - Every successful mutation (purchase, selection, run result) saves the profile synchronously.
 *
 * Lives in the game instance, so the profile survives map changes. Not used by the simulation (no determinism impact).
 */
UCLASS()
class LRPROGRESSION_API ULRProgressionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Fired after any change of the profile (credits, garage, selection, upgrades, records, reload). */
	DECLARE_MULTICAST_DELEGATE(FOnProfileChanged);

	/** Relative path of the economy data file. */
	static constexpr const TCHAR* EconomyDataPath = TEXT("Economy/Economy.json");

	/** Finds the subsystem from any object living in a game world (or the game instance itself). */
	static ULRProgressionSubsystem* Get(const UObject* WorldContext);

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Spendable credits. */
	int64 GetCredits() const;

	/** Buys a vehicle (deducts credits, adds it to the garage, saves). False if validation fails. */
	bool PurchaseVehicle(FName VehicleId);

	/** Same rules as PurchaseVehicle, with the detailed reason. Does not change anything. */
	ELRPurchaseResult CanPurchaseVehicle(FName VehicleId) const;

	/** True if the vehicle is in the garage. */
	bool IsVehicleOwned(FName VehicleId) const;

	/** Vehicle used for the next run (always owned). */
	FName GetSelectedVehicle() const;

	/** Selects an owned vehicle and saves. False if it is not owned. */
	bool SelectVehicle(FName VehicleId);

	/** Vehicle ids in the garage. */
	const TArray<FName>& GetOwnedVehicles() const;

	/** Dealership price, or INDEX_NONE if the vehicle is not sold. */
	int64 GetVehiclePrice(FName VehicleId) const;

	/** Upgrade levels of a vehicle (stock state if never upgraded). */
	FLRVehicleUpgradeState GetUpgradeState(FName VehicleId) const;

	/** Level of one upgrade category on a vehicle, 0..LRProgression::MaxUpgradeLevel. */
	int32 GetUpgradeLevel(FName VehicleId, ELRUpgradeType Type) const;

	/** Price of the next level of an upgrade, or INDEX_NONE if already at max level. */
	int64 GetNextUpgradePrice(FName VehicleId, ELRUpgradeType Type) const;

	/** Same rules as PurchaseUpgrade, with the detailed reason. Does not change anything. */
	ELRPurchaseResult CanPurchaseUpgrade(FName VehicleId, ELRUpgradeType Type) const;

	/** Buys the next level of an upgrade on an owned vehicle (deducts credits, saves). False if validation fails. */
	bool PurchaseUpgrade(FName VehicleId, ELRUpgradeType Type);

	/**
	 * Applies a finished run: credits earned (FLREconomyCalculator::ComputeRunCredits), best score for Mode,
	 * lifetime stats. Saves the profile. Returns the credits earned.
	 */
	int64 RecordRunResult(const FLRRunResult& Result, ELRGameModeType Mode);

	/** Best score recorded in a mode (0 if none). */
	int64 GetBestScore(ELRGameModeType Mode) const;

	/** Lifetime distance driven, meters. */
	double GetTotalDistanceMeters() const;

	/** Lifetime number of recorded runs. */
	int32 GetTotalRuns() const;

	/** Lifetime number of near misses. */
	int32 GetTotalNearMisses() const;

	/** Writes the profile to slot "LeftRight_Profile". Skipped if the stored profile is newer than this build. */
	void SaveProfile();

	/** Reads the profile from disk (migrating it), or creates a fresh one if none exists or it cannot be read. */
	void LoadProfile();

	/** Replaces the profile with a fresh one (StartingCredits + starter vehicle) and saves it. */
	void ResetProfile();

	/** Re-reads Data/Economy/Economy.json (keeps the previous rules if the file is missing or invalid). */
	void ReloadEconomy();

	/** Current economy rules. */
	const FLREconomyRules& GetEconomyRules() const { return EconomyRules; }

	/** Read-only access to the profile (never null after Initialize). */
	const ULRSaveGame* GetProfile() const { return Profile; }

	/** Fired after any change of the profile. */
	FOnProfileChanged OnProfileChanged;

private:
	/** Builds a new profile at the current save version from the economy rules. */
	ULRSaveGame* CreateFreshProfile() const;

	/** Guarantees invariants: starter owned, selection owned, credits >= 0. */
	void SanitizeProfile();

	/** Saves and broadcasts OnProfileChanged. */
	void CommitProfileChange();

	/** OnDataReloaded handler. */
	void HandleDataReloaded();

	/** Current profile. */
	UPROPERTY(Transient)
	TObjectPtr<ULRSaveGame> Profile;

	/** Economy tuning loaded from data. */
	FLREconomyRules EconomyRules;

	/** OnDataReloaded binding. */
	FDelegateHandle DataReloadedHandle;

	/** The stored profile was written by a newer build: never overwrite it. */
	bool bProfileReadOnly = false;
};
