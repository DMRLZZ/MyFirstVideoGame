// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRDataLoader.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "LRDataSubsystem.generated.h"

/**
 * Game-instance access point for design data, with hot reload.
 *
 * Systems load their typed structs through Get() and subscribe to OnDataReloaded to re-read them
 * when a designer edits a JSON file and runs the console command `lr.Data.Reload`.
 */
UCLASS()
class LRCORE_API ULRDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnDataReloaded);

	/** Finds the subsystem from any object living in a game world. */
	static ULRDataSubsystem* Get(const UObject* WorldContext);

	/** Loads a data file into a struct. Returns false and keeps OutStruct untouched on failure. */
	template <typename TStruct>
	bool Get(const FString& RelativePath, TStruct& OutStruct) const
	{
		TStruct Loaded = OutStruct;
		if (FLRDataLoader::Load(RelativePath, Loaded))
		{
			OutStruct = MoveTemp(Loaded);
			return true;
		}
		return false;
	}

	/** Broadcasts OnDataReloaded so every listener re-reads its files. */
	void ReloadAll();

	/** Fired by ReloadAll() (console: lr.Data.Reload). */
	FOnDataReloaded OnDataReloaded;
};
