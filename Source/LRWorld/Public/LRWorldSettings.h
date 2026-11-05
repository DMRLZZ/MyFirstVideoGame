// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "LRWorldSettings.generated.h"

/**
 * Project settings of the LRWorld module (Project Settings > Game > LEFTRIGHT World).
 *
 * Stored in DefaultGame.ini under [/Script/LRWorld.LRWorldSettings]; the C++ defaults below are used when the
 * ini has no entry. Paths are relative to the Data/ folder (see FLRDataLoader).
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "LEFTRIGHT World"))
class LRWORLD_API ULRWorldSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULRWorldSettings();

	/** Returns the settings CDO (never null). */
	static const ULRWorldSettings* Get();

	/** Route used by one-way modes (endless, time attack, free ride, daily). */
	UPROPERTY(config, EditAnywhere, Category = "Routes")
	FString OneWayRouteFile;

	/** Route used by EndlessTwoWay. */
	UPROPERTY(config, EditAnywhere, Category = "Routes")
	FString TwoWayRouteFile;

	/** Spawns ALRRoadStreamer at world begin play (disable for authored maps that bring their own road meshes). */
	UPROPERTY(config, EditAnywhere, Category = "Spawning")
	bool bSpawnRoadStreamer;

	/** Spawns ALREnvironmentActor at world begin play (disable for maps that bring their own sky and lights). */
	UPROPERTY(config, EditAnywhere, Category = "Spawning")
	bool bSpawnEnvironment;

	/** Seed used when no run reset happened before world begin play (map opened without the game mode). */
	UPROPERTY(config, EditAnywhere, Category = "Spawning")
	int32 FallbackSeed;

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override;
};
