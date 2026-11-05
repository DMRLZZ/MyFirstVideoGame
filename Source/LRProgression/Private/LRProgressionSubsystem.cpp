// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRProgressionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "LRDataSubsystem.h"
#include "LREconomyCalculator.h"
#include "LREvents.h"
#include "LRProgression.h"
#include "LRSaveGame.h"
#include "LRSaveMigration.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LRProgressionSubsystem)

ULRProgressionSubsystem* ULRProgressionSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}

	if (const UGameInstance* GameInstance = Cast<UGameInstance>(WorldContext))
	{
		return GameInstance->GetSubsystem<ULRProgressionSubsystem>();
	}

	if (!GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<ULRProgressionSubsystem>() : nullptr;
}

void ULRProgressionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (ULRDataSubsystem* Data = Collection.InitializeDependency<ULRDataSubsystem>())
	{
		DataReloadedHandle = Data->OnDataReloaded.AddUObject(this, &ULRProgressionSubsystem::HandleDataReloaded);
	}

	ReloadEconomy();
	LoadProfile();
}

void ULRProgressionSubsystem::Deinitialize()
{
	if (DataReloadedHandle.IsValid())
	{
		if (const UGameInstance* GameInstance = GetGameInstance())
		{
			if (ULRDataSubsystem* Data = GameInstance->GetSubsystem<ULRDataSubsystem>())
			{
				Data->OnDataReloaded.Remove(DataReloadedHandle);
			}
		}
		DataReloadedHandle.Reset();
	}

	OnProfileChanged.Clear();
	Profile = nullptr;

	Super::Deinitialize();
}

void ULRProgressionSubsystem::ReloadEconomy()
{
	FLREconomyRules Loaded = EconomyRules;
	FString Error;
	if (FLRDataLoader::Load(EconomyDataPath, Loaded, &Error))
	{
		EconomyRules = MoveTemp(Loaded);
		UE_LOG(LogLRProgression, Log, TEXT("Economy loaded: %d vehicle prices, starter '%s'"),
			EconomyRules.VehiclePrices.Num(), *EconomyRules.StarterVehicle.ToString());
	}
	else
	{
		UE_LOG(LogLRProgression, Warning, TEXT("Could not load %s (%s); keeping previous economy rules"), EconomyDataPath, *Error);
	}
}

void ULRProgressionSubsystem::HandleDataReloaded()
{
	ReloadEconomy();
	if (Profile)
	{
		// A new starter vehicle must be granted if the designer changed it.
		SanitizeProfile();
		OnProfileChanged.Broadcast();
	}
}

ULRSaveGame* ULRProgressionSubsystem::CreateFreshProfile() const
{
	ULRSaveGame* Fresh = NewObject<ULRSaveGame>(const_cast<ULRProgressionSubsystem*>(this));
	Fresh->SaveVersion = LRSaveMigration::CurrentVersion;
	Fresh->Credits = FMath::Max<int64>(EconomyRules.StartingCredits, 0);
	if (!EconomyRules.StarterVehicle.IsNone())
	{
		Fresh->OwnedVehicles.Add(EconomyRules.StarterVehicle);
		Fresh->SelectedVehicle = EconomyRules.StarterVehicle;
	}
	return Fresh;
}

void ULRProgressionSubsystem::SanitizeProfile()
{
	check(Profile);

	Profile->Credits = FMath::Max<int64>(Profile->Credits, 0);

	const FName Starter = EconomyRules.StarterVehicle;
	if (!Starter.IsNone() && !Profile->OwnsVehicle(Starter))
	{
		Profile->OwnedVehicles.Insert(Starter, 0);
	}

	if (!Profile->OwnsVehicle(Profile->SelectedVehicle))
	{
		Profile->SelectedVehicle = Profile->OwnedVehicles.Num() > 0 ? Profile->OwnedVehicles[0] : NAME_None;
	}
}

void ULRProgressionSubsystem::LoadProfile()
{
	bProfileReadOnly = false;
	ULRSaveGame* Loaded = nullptr;

	if (UGameplayStatics::DoesSaveGameExist(LRProgression::ProfileSlotName, LRProgression::ProfileUserIndex))
	{
		Loaded = Cast<ULRSaveGame>(UGameplayStatics::LoadGameFromSlot(LRProgression::ProfileSlotName, LRProgression::ProfileUserIndex));
		if (!Loaded)
		{
			UE_LOG(LogLRProgression, Error, TEXT("Profile slot '%s' exists but could not be read; starting a fresh profile in memory"),
				LRProgression::ProfileSlotName);
			// Do not overwrite an unreadable file automatically: it may be recoverable.
			bProfileReadOnly = true;
		}
	}

	if (Loaded)
	{
		const ELRSaveMigrationResult Result = LRSaveMigration::Migrate(*Loaded);
		switch (Result)
		{
		case ELRSaveMigrationResult::UpToDate:
			break;
		case ELRSaveMigrationResult::Migrated:
			Profile = Loaded;
			SanitizeProfile();
			SaveProfile();
			break;
		case ELRSaveMigrationResult::TooNew:
		case ELRSaveMigrationResult::Failed:
		default:
			UE_LOG(LogLRProgression, Error, TEXT("Profile cannot be used by this build (version %d); it will not be overwritten"), Loaded->SaveVersion);
			bProfileReadOnly = true;
			Loaded = nullptr;
			break;
		}
	}

	Profile = Loaded ? Loaded : CreateFreshProfile();
	SanitizeProfile();

	if (!Loaded && !bProfileReadOnly)
	{
		UE_LOG(LogLRProgression, Log, TEXT("Created a fresh profile with %lld credits"), Profile->Credits);
		SaveProfile();
	}

	OnProfileChanged.Broadcast();
}

void ULRProgressionSubsystem::SaveProfile()
{
	if (!Profile)
	{
		return;
	}
	if (bProfileReadOnly)
	{
		UE_LOG(LogLRProgression, Warning, TEXT("Profile save skipped: the stored profile is newer or unreadable"));
		return;
	}

	Profile->SaveVersion = LRSaveMigration::CurrentVersion;
	if (!UGameplayStatics::SaveGameToSlot(Profile, LRProgression::ProfileSlotName, LRProgression::ProfileUserIndex))
	{
		UE_LOG(LogLRProgression, Error, TEXT("Failed to save profile slot '%s'"), LRProgression::ProfileSlotName);
	}
}

void ULRProgressionSubsystem::ResetProfile()
{
	Profile = CreateFreshProfile();
	SanitizeProfile();
	bProfileReadOnly = false;
	CommitProfileChange();
}

void ULRProgressionSubsystem::CommitProfileChange()
{
	SaveProfile();
	OnProfileChanged.Broadcast();
}

int64 ULRProgressionSubsystem::GetCredits() const
{
	return Profile ? Profile->Credits : 0;
}

ELRPurchaseResult ULRProgressionSubsystem::CanPurchaseVehicle(FName VehicleId) const
{
	if (!Profile)
	{
		return ELRPurchaseResult::UnknownItem;
	}
	return FLREconomyCalculator::ValidateVehiclePurchase(EconomyRules, Profile->Credits, Profile->OwnedVehicles, VehicleId);
}

bool ULRProgressionSubsystem::PurchaseVehicle(FName VehicleId)
{
	const ELRPurchaseResult Result = CanPurchaseVehicle(VehicleId);
	if (Result != ELRPurchaseResult::Success)
	{
		UE_LOG(LogLRProgression, Log, TEXT("Vehicle purchase '%s' refused: %s"), *VehicleId.ToString(),
			*StaticEnum<ELRPurchaseResult>()->GetNameStringByValue(static_cast<int64>(Result)));
		return false;
	}

	const int64 Price = FLREconomyCalculator::GetVehiclePrice(EconomyRules, VehicleId);
	Profile->Credits -= Price;
	Profile->OwnedVehicles.Add(VehicleId);
	UE_LOG(LogLRProgression, Log, TEXT("Bought vehicle '%s' for %lld credits"), *VehicleId.ToString(), Price);
	CommitProfileChange();
	return true;
}

bool ULRProgressionSubsystem::IsVehicleOwned(FName VehicleId) const
{
	return Profile && Profile->OwnsVehicle(VehicleId);
}

FName ULRProgressionSubsystem::GetSelectedVehicle() const
{
	return Profile ? Profile->SelectedVehicle : EconomyRules.StarterVehicle;
}

bool ULRProgressionSubsystem::SelectVehicle(FName VehicleId)
{
	if (!IsVehicleOwned(VehicleId))
	{
		return false;
	}
	if (Profile->SelectedVehicle != VehicleId)
	{
		Profile->SelectedVehicle = VehicleId;
		CommitProfileChange();
	}
	return true;
}

const TArray<FName>& ULRProgressionSubsystem::GetOwnedVehicles() const
{
	static const TArray<FName> Empty;
	return Profile ? Profile->OwnedVehicles : Empty;
}

int64 ULRProgressionSubsystem::GetVehiclePrice(FName VehicleId) const
{
	return FLREconomyCalculator::GetVehiclePrice(EconomyRules, VehicleId);
}

FLRVehicleUpgradeState ULRProgressionSubsystem::GetUpgradeState(FName VehicleId) const
{
	if (Profile)
	{
		if (const FLRVehicleUpgradeState* State = Profile->Upgrades.Find(VehicleId))
		{
			return *State;
		}
	}
	return FLRVehicleUpgradeState();
}

int32 ULRProgressionSubsystem::GetUpgradeLevel(FName VehicleId, ELRUpgradeType Type) const
{
	return GetUpgradeState(VehicleId).GetLevel(Type);
}

int64 ULRProgressionSubsystem::GetNextUpgradePrice(FName VehicleId, ELRUpgradeType Type) const
{
	if (Type == ELRUpgradeType::Count)
	{
		return INDEX_NONE;
	}
	return FLREconomyCalculator::GetUpgradePrice(EconomyRules, VehicleId, GetUpgradeLevel(VehicleId, Type));
}

ELRPurchaseResult ULRProgressionSubsystem::CanPurchaseUpgrade(FName VehicleId, ELRUpgradeType Type) const
{
	if (!Profile || Type == ELRUpgradeType::Count)
	{
		return ELRPurchaseResult::UnknownItem;
	}
	return FLREconomyCalculator::ValidateUpgradePurchase(EconomyRules, Profile->Credits, Profile->OwnsVehicle(VehicleId),
		VehicleId, GetUpgradeLevel(VehicleId, Type));
}

bool ULRProgressionSubsystem::PurchaseUpgrade(FName VehicleId, ELRUpgradeType Type)
{
	if (CanPurchaseUpgrade(VehicleId, Type) != ELRPurchaseResult::Success)
	{
		return false;
	}

	FLRVehicleUpgradeState& State = Profile->Upgrades.FindOrAdd(VehicleId);
	const int32 CurrentLevel = State.GetLevel(Type);
	const int64 Price = FLREconomyCalculator::GetUpgradePrice(EconomyRules, VehicleId, CurrentLevel);
	Profile->Credits -= Price;
	State.SetLevel(Type, CurrentLevel + 1);
	CommitProfileChange();
	return true;
}

int64 ULRProgressionSubsystem::RecordRunResult(const FLRRunResult& Result, ELRGameModeType Mode)
{
	if (!Profile)
	{
		return 0;
	}

	const int64 Earned = FLREconomyCalculator::ComputeRunCredits(EconomyRules, Result, Mode);
	Profile->Credits += Earned;
	FLREconomyCalculator::UpdateBestScore(Profile->BestScores, LRProgression::GetModeKey(Mode), Result.FinalScore);
	Profile->TotalDistanceMeters += FMath::Max(static_cast<double>(Result.DistanceMeters), 0.0);
	Profile->TotalRuns += 1;
	Profile->TotalNearMisses += FMath::Max(Result.NearMisses, 0);

	UE_LOG(LogLRProgression, Log, TEXT("Run recorded (%s): score %lld, %lld credits earned"),
		*LRProgression::GetModeKey(Mode).ToString(), Result.FinalScore, Earned);

	CommitProfileChange();
	return Earned;
}

int64 ULRProgressionSubsystem::GetBestScore(ELRGameModeType Mode) const
{
	if (!Profile)
	{
		return 0;
	}
	const int64* Best = Profile->BestScores.Find(LRProgression::GetModeKey(Mode));
	return Best ? *Best : 0;
}

double ULRProgressionSubsystem::GetTotalDistanceMeters() const
{
	return Profile ? Profile->TotalDistanceMeters : 0.0;
}

int32 ULRProgressionSubsystem::GetTotalRuns() const
{
	return Profile ? Profile->TotalRuns : 0;
}

int32 ULRProgressionSubsystem::GetTotalNearMisses() const
{
	return Profile ? Profile->TotalNearMisses : 0;
}
