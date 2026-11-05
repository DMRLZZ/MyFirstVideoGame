// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRunContextSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "LRCore.h"
#include "LRGameEventSubsystem.h"

ULRRunContextSubsystem* ULRRunContextSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<ULRRunContextSubsystem>() : nullptr;
}

bool ULRRunContextSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULRRunContextSubsystem::BeginNewRun(int32 Seed, ELRGameModeType Mode)
{
	RunSeed = Seed;
	GameMode = Mode;

	UE_LOG(LogLRCore, Log, TEXT("New run: seed=%d mode=%d"), Seed, static_cast<int32>(Mode));

	if (const ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		FLRRunResetEvent Event;
		Event.Seed = Seed;
		Event.Mode = Mode;
		Events->BroadcastRunReset(Event);
	}
}

void ULRRunContextSubsystem::SetRunState(ELRRunState NewState)
{
	if (NewState == RunState)
	{
		return;
	}

	FLRRunStateChangedEvent Event;
	Event.Previous = RunState;
	Event.Current = NewState;
	RunState = NewState;

	if (const ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		Events->BroadcastRunStateChanged(Event);
	}
}

void ULRRunContextSubsystem::SetPlayerPawn(APawn* Pawn)
{
	PlayerPawn = Pawn;
}
