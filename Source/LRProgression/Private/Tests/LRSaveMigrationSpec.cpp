// Copyright LEFTRIGHT. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LRSaveGame.h"
#include "LRSaveMigration.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace LRSaveMigrationSpecPrivate
{
	/** Fake step used to check the runner applies steps in order: appends "v<From>" to OwnedVehicles. */
	bool FakeStepAppendVersion(ULRSaveGame& Save)
	{
		Save.OwnedVehicles.Add(FName(*FString::Printf(TEXT("v%d"), Save.SaveVersion)));
		return true;
	}

	bool FakeStepFail(ULRSaveGame& /*Save*/)
	{
		return false;
	}
}

BEGIN_DEFINE_SPEC(FLRSaveMigrationSpec, "LeftRight.Progression.Migration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<ULRSaveGame> Save;
END_DEFINE_SPEC(FLRSaveMigrationSpec)

void FLRSaveMigrationSpec::Define()
{
	BeforeEach([this]()
	{
		Save.Reset(NewObject<ULRSaveGame>(GetTransientPackage()));
	});

	AfterEach([this]()
	{
		Save.Reset();
	});

	Describe("Migrate", [this]()
	{
		It("upgrades a fake v0 profile to v1 and normalizes it", [this]()
		{
			Save->SaveVersion = 0;
			Save->Credits = -500;
			Save->OwnedVehicles = { TEXT("bauer_r3_sport"), TEXT("bauer_r3_sport"), NAME_None, TEXT("volta_nine") };
			Save->SelectedVehicle = TEXT("vittore_saetta");

			FLRVehicleUpgradeState Over;
			Over.Engine = 9;
			Over.Tires = -2;
			Save->Upgrades.Add(TEXT("volta_nine"), Over);
			Save->Upgrades.Add(TEXT("bauer_r3_sport"), FLRVehicleUpgradeState());
			Save->Upgrades.Add(TEXT("ardent_spectre_lm"), Over);

			Save->BestScores.Add(TEXT("EndlessOneWay"), 12000);
			Save->BestScores.Add(TEXT("Daily"), -5);
			Save->TotalRuns = -3;

			const ELRSaveMigrationResult Result = LRSaveMigration::Migrate(*Save);

			TestTrue(TEXT("result"), Result == ELRSaveMigrationResult::Migrated);
			TestEqual(TEXT("version"), Save->SaveVersion, LRSaveMigration::CurrentVersion);
			TestEqual(TEXT("credits clamped"), Save->Credits, int64(0));
			TestEqual(TEXT("unique owned vehicles"), Save->OwnedVehicles.Num(), 2);
			TestEqual(TEXT("first owned"), Save->OwnedVehicles[0], FName(TEXT("bauer_r3_sport")));
			TestEqual(TEXT("selected reset to an owned vehicle"), Save->SelectedVehicle, FName(TEXT("bauer_r3_sport")));
			TestEqual(TEXT("stock and unowned upgrade entries dropped"), Save->Upgrades.Num(), 1);
			const FLRVehicleUpgradeState* Volta = Save->Upgrades.Find(TEXT("volta_nine"));
			if (TestNotNull(TEXT("volta upgrades kept"), Volta))
			{
				TestEqual(TEXT("engine clamped"), Volta->Engine, 5);
				TestEqual(TEXT("tires clamped"), Volta->Tires, 0);
			}
			TestEqual(TEXT("negative best score dropped"), Save->BestScores.Num(), 1);
			TestEqual(TEXT("best score kept"), Save->BestScores.FindRef(TEXT("EndlessOneWay")), int64(12000));
			TestEqual(TEXT("runs clamped"), Save->TotalRuns, 0);
		});

		It("leaves a current profile untouched", [this]()
		{
			Save->SaveVersion = LRSaveMigration::CurrentVersion;
			Save->Credits = -1;
			TestTrue(TEXT("result"), LRSaveMigration::Migrate(*Save) == ELRSaveMigrationResult::UpToDate);
			TestEqual(TEXT("credits untouched"), Save->Credits, int64(-1));
		});

		It("refuses a profile from a newer build", [this]()
		{
			Save->SaveVersion = LRSaveMigration::CurrentVersion + 1;
			AddExpectedMessage(TEXT("newer than supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			TestTrue(TEXT("result"), LRSaveMigration::Migrate(*Save) == ELRSaveMigrationResult::TooNew);
			TestEqual(TEXT("version untouched"), Save->SaveVersion, LRSaveMigration::CurrentVersion + 1);
		});

		It("has one built-in step per older version", [this]()
		{
			const TConstArrayView<FLRSaveMigrationStep> Steps = LRSaveMigration::GetSteps();
			TestEqual(TEXT("step count"), Steps.Num(), LRSaveMigration::CurrentVersion);
			for (int32 Index = 0; Index < Steps.Num(); ++Index)
			{
				TestEqual(FString::Printf(TEXT("step %d from"), Index), Steps[Index].FromVersion, Index);
				TestTrue(FString::Printf(TEXT("step %d bound"), Index), Steps[Index].Apply != nullptr);
			}
		});
	});

	Describe("MigrateWithSteps", [this]()
	{
		It("applies steps one by one in version order", [this]()
		{
			const FLRSaveMigrationStep Steps[] =
			{
				{ 2, &LRSaveMigrationSpecPrivate::FakeStepAppendVersion },
				{ 0, &LRSaveMigrationSpecPrivate::FakeStepAppendVersion },
				{ 1, &LRSaveMigrationSpecPrivate::FakeStepAppendVersion },
			};
			Save->SaveVersion = 0;

			const ELRSaveMigrationResult Result = LRSaveMigration::MigrateWithSteps(*Save, Steps, 3);

			TestTrue(TEXT("result"), Result == ELRSaveMigrationResult::Migrated);
			TestEqual(TEXT("version"), Save->SaveVersion, 3);
			TestEqual(TEXT("steps applied"), Save->OwnedVehicles.Num(), 3);
			if (Save->OwnedVehicles.Num() == 3)
			{
				TestEqual(TEXT("order 0"), Save->OwnedVehicles[0], FName(TEXT("v0")));
				TestEqual(TEXT("order 1"), Save->OwnedVehicles[1], FName(TEXT("v1")));
				TestEqual(TEXT("order 2"), Save->OwnedVehicles[2], FName(TEXT("v2")));
			}
		});

		It("stops at a missing step", [this]()
		{
			const FLRSaveMigrationStep Steps[] = { { 0, &LRSaveMigrationSpecPrivate::FakeStepAppendVersion } };
			Save->SaveVersion = 0;
			AddExpectedError(TEXT("No profile migration step"), EAutomationExpectedErrorFlags::Contains, 1);

			TestTrue(TEXT("result"), LRSaveMigration::MigrateWithSteps(*Save, Steps, 2) == ELRSaveMigrationResult::Failed);
			TestEqual(TEXT("stays at last good version"), Save->SaveVersion, 1);
		});

		It("stops at a failing step", [this]()
		{
			const FLRSaveMigrationStep Steps[] = { { 0, &LRSaveMigrationSpecPrivate::FakeStepFail } };
			Save->SaveVersion = 0;
			AddExpectedError(TEXT("failed"), EAutomationExpectedErrorFlags::Contains, 1);

			TestTrue(TEXT("result"), LRSaveMigration::MigrateWithSteps(*Save, Steps, 1) == ELRSaveMigrationResult::Failed);
			TestEqual(TEXT("version unchanged"), Save->SaveVersion, 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
