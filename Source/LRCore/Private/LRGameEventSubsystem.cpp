// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRGameEventSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

ULRGameEventSubsystem* ULRGameEventSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<ULRGameEventSubsystem>() : nullptr;
}

bool ULRGameEventSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Gameplay worlds only: no bus for editor preview / inactive worlds.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULRGameEventSubsystem::Deinitialize()
{
	// Drop every listener so nothing dangles across world teardown.
	OnNearMiss.Clear();
	OnCrash.Clear();
	OnLaneChange.Clear();
	OnRunStateChanged.Clear();
	OnRunReset.Clear();
	OnScoreChanged.Clear();
	OnComboChanged.Clear();
	OnSpeedTierChanged.Clear();
	OnCameraModeChanged.Clear();
	OnRunFinished.Clear();

	Super::Deinitialize();
}
