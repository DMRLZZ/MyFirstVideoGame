// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

class ULRSaveGame;

/** Result of a profile migration. */
enum class ELRSaveMigrationResult : uint8
{
	/** The profile was already at the target version; nothing changed. */
	UpToDate,
	/** One or more steps were applied; the profile is now at the target version. */
	Migrated,
	/** The profile was written by a newer build. It is left untouched and must not be overwritten. */
	TooNew,
	/** A step is missing or failed; the profile is left at the last successful version. */
	Failed
};

/**
 * One migration step: upgrades a profile from FromVersion to FromVersion + 1, in place.
 * Steps must be pure (no world, no I/O) so they can be unit tested. Return false on unrecoverable data.
 */
struct LRPROGRESSION_API FLRSaveMigrationStep
{
	/** Version this step reads. It writes FromVersion + 1. */
	int32 FromVersion = 0;

	/** Upgrade function. Must not change SaveVersion (the runner does it). */
	bool (*Apply)(ULRSaveGame& Save) = nullptr;
};

/**
 * Versioned save upgrades (docs/02-ARCHITECTURE.md §5.8).
 *
 * To change the profile layout:
 *  1. Bump CurrentVersion.
 *  2. Write a step function upgrading CurrentVersion-1 -> CurrentVersion and register it in GetSteps() (LRSaveMigration.cpp).
 *  3. Add a test that builds a fake old profile and checks the result.
 */
namespace LRSaveMigration
{
	/** Version written by this build. */
	inline constexpr int32 CurrentVersion = 1;

	/** Built-in migration steps, sorted by FromVersion. */
	LRPROGRESSION_API TConstArrayView<FLRSaveMigrationStep> GetSteps();

	/** Upgrades Save step by step to CurrentVersion using the built-in steps. */
	LRPROGRESSION_API ELRSaveMigrationResult Migrate(ULRSaveGame& Save);

	/**
	 * Generic runner (exposed for tests): applies the step whose FromVersion equals Save.SaveVersion, bumps the version,
	 * and repeats until TargetVersion. Stops with Failed if a step is missing or returns false.
	 */
	LRPROGRESSION_API ELRSaveMigrationResult MigrateWithSteps(ULRSaveGame& Save, TConstArrayView<FLRSaveMigrationStep> Steps, int32 TargetVersion);

	/**
	 * v0 -> v1. Version 0 is the unversioned prototype profile. The step normalizes it:
	 * negative credits and stats clamped to 0, duplicate / None vehicle ids removed, upgrade levels clamped to [0, 5],
	 * stock upgrade entries and entries of unowned vehicles dropped, negative best scores dropped,
	 * SelectedVehicle reset to the first owned vehicle when it is not owned.
	 */
	LRPROGRESSION_API bool MigrateV0ToV1(ULRSaveGame& Save);
}
