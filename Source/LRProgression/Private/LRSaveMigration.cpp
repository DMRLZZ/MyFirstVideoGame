// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRSaveMigration.h"

#include "LRProgression.h"
#include "LRSaveGame.h"

namespace LRSaveMigration
{
	static const FLRSaveMigrationStep GBuiltInSteps[] =
	{
		{ 0, &MigrateV0ToV1 },
	};

	static_assert(UE_ARRAY_COUNT(GBuiltInSteps) == CurrentVersion, "Every version below CurrentVersion needs exactly one migration step.");

	TConstArrayView<FLRSaveMigrationStep> GetSteps()
	{
		return TConstArrayView<FLRSaveMigrationStep>(GBuiltInSteps, UE_ARRAY_COUNT(GBuiltInSteps));
	}

	ELRSaveMigrationResult Migrate(ULRSaveGame& Save)
	{
		return MigrateWithSteps(Save, GetSteps(), CurrentVersion);
	}

	ELRSaveMigrationResult MigrateWithSteps(ULRSaveGame& Save, TConstArrayView<FLRSaveMigrationStep> Steps, int32 TargetVersion)
	{
		if (Save.SaveVersion > TargetVersion)
		{
			UE_LOG(LogLRProgression, Warning, TEXT("Profile version %d is newer than supported version %d"), Save.SaveVersion, TargetVersion);
			return ELRSaveMigrationResult::TooNew;
		}
		if (Save.SaveVersion == TargetVersion)
		{
			return ELRSaveMigrationResult::UpToDate;
		}

		// Unknown negative versions are treated as the oldest format.
		Save.SaveVersion = FMath::Max(Save.SaveVersion, 0);

		while (Save.SaveVersion < TargetVersion)
		{
			const int32 From = Save.SaveVersion;
			const FLRSaveMigrationStep* Step = Steps.FindByPredicate([From](const FLRSaveMigrationStep& Candidate)
			{
				return Candidate.FromVersion == From;
			});

			if (!Step || !Step->Apply)
			{
				UE_LOG(LogLRProgression, Error, TEXT("No profile migration step from version %d"), From);
				return ELRSaveMigrationResult::Failed;
			}
			if (!Step->Apply(Save))
			{
				UE_LOG(LogLRProgression, Error, TEXT("Profile migration step %d -> %d failed"), From, From + 1);
				Save.SaveVersion = From;
				return ELRSaveMigrationResult::Failed;
			}

			Save.SaveVersion = From + 1;
			UE_LOG(LogLRProgression, Log, TEXT("Profile migrated from version %d to %d"), From, Save.SaveVersion);
		}

		return ELRSaveMigrationResult::Migrated;
	}

	bool MigrateV0ToV1(ULRSaveGame& Save)
	{
		Save.Credits = FMath::Max<int64>(Save.Credits, 0);
		Save.TotalDistanceMeters = FMath::Max(Save.TotalDistanceMeters, 0.0);
		Save.TotalRuns = FMath::Max(Save.TotalRuns, 0);
		Save.TotalNearMisses = FMath::Max(Save.TotalNearMisses, 0);

		// Unique, valid vehicle ids, preserving order.
		TArray<FName> UniqueVehicles;
		UniqueVehicles.Reserve(Save.OwnedVehicles.Num());
		for (const FName VehicleId : Save.OwnedVehicles)
		{
			if (!VehicleId.IsNone())
			{
				UniqueVehicles.AddUnique(VehicleId);
			}
		}
		Save.OwnedVehicles = MoveTemp(UniqueVehicles);

		for (auto It = Save.Upgrades.CreateIterator(); It; ++It)
		{
			It->Value.ClampLevels();
			if (!Save.OwnedVehicles.Contains(It->Key) || It->Value.IsStock())
			{
				It.RemoveCurrent();
			}
		}

		for (auto It = Save.BestScores.CreateIterator(); It; ++It)
		{
			if (It->Key.IsNone() || It->Value < 0)
			{
				It.RemoveCurrent();
			}
		}

		if (!Save.OwnedVehicles.Contains(Save.SelectedVehicle))
		{
			Save.SelectedVehicle = Save.OwnedVehicles.Num() > 0 ? Save.OwnedVehicles[0] : NAME_None;
		}

		return true;
	}
}
