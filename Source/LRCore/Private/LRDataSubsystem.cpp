// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRDataSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LRCore.h"

ULRDataSubsystem* ULRDataSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<ULRDataSubsystem>() : nullptr;
}

void ULRDataSubsystem::ReloadAll()
{
	UE_LOG(LogLRCore, Log, TEXT("Reloading design data from %s"), *FLRDataLoader::GetDataDirectory());
	OnDataReloaded.Broadcast();
}

static FAutoConsoleCommand GLRDataReloadCommand(
	TEXT("lr.Data.Reload"),
	TEXT("Re-reads every JSON design data file (Data/**) in all running game instances."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEngine)
		{
			return;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (const UGameInstance* GameInstance = Context.OwningGameInstance)
			{
				if (ULRDataSubsystem* Data = GameInstance->GetSubsystem<ULRDataSubsystem>())
				{
					Data->ReloadAll();
				}
			}
		}
	}));
