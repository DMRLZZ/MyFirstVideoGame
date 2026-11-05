// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRoadSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "LRDataLoader.h"
#include "LREnvironmentActor.h"
#include "LREvents.h"
#include "LRGameEventSubsystem.h"
#include "LRRandom.h"
#include "LRRoadStreamer.h"
#include "LRRunContextSubsystem.h"
#include "LRWorld.h"
#include "LRWorldSettings.h"

ULRRoadSubsystem* ULRRoadSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<ULRRoadSubsystem>() : nullptr;
}

bool ULRRoadSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULRRoadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<ULRGameEventSubsystem>();
	Collection.InitializeDependency<ULRRunContextSubsystem>();

	if (ULRGameEventSubsystem* Events = GetWorld()->GetSubsystem<ULRGameEventSubsystem>())
	{
		RunResetHandle = Events->OnRunReset.AddUObject(this, &ULRRoadSubsystem::HandleRunReset);
	}
}

void ULRRoadSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (ULRGameEventSubsystem* Events = World->GetSubsystem<ULRGameEventSubsystem>())
		{
			Events->OnRunReset.Remove(RunResetHandle);
		}
	}
	RunResetHandle.Reset();

	RoadStreamer = nullptr;
	EnvironmentActor = nullptr;
	bHasRoad = false;

	Super::Deinitialize();
}

void ULRRoadSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bWorldBegunPlay = true;
	SpawnWorldActors(InWorld);

	if (!bHasRoad)
	{
		// No run reset happened yet (map opened without the game mode): build a default road so the map is drivable.
		const ULRRunContextSubsystem* RunContext = InWorld.GetSubsystem<ULRRunContextSubsystem>();
		const bool bTwoWay = RunContext && RunContext->GetGameMode() == ELRGameModeType::EndlessTwoWay;
		RebuildForSeed(ULRWorldSettings::Get()->FallbackSeed, bTwoWay);
	}
	else
	{
		ApplyRouteToActors();
	}
}

void ULRRoadSubsystem::SpawnWorldActors(UWorld& InWorld)
{
	const ULRWorldSettings* Settings = ULRWorldSettings::Get();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (Settings->bSpawnRoadStreamer && !RoadStreamer)
	{
		RoadStreamer = InWorld.SpawnActor<ALRRoadStreamer>(ALRRoadStreamer::StaticClass(), FTransform::Identity, SpawnParams);
	}

	if (Settings->bSpawnEnvironment && !EnvironmentActor)
	{
		EnvironmentActor = InWorld.SpawnActor<ALREnvironmentActor>(ALREnvironmentActor::StaticClass(), FTransform::Identity, SpawnParams);
	}
}

void ULRRoadSubsystem::HandleRunReset(const FLRRunResetEvent& Event)
{
	RebuildForSeed(Event.Seed, Event.Mode == ELRGameModeType::EndlessTwoWay);
}

FLRRouteDefinition ULRRoadSubsystem::LoadRoute(const FString& RelativePath, bool bTwoWayFallback) const
{
	FLRRouteDefinition Loaded;
	if (bTwoWayFallback)
	{
		// C++ fallback mirrors Route_GreyboxTwoWay.json in case the file is missing.
		Loaded.Profile.LanesPerDirection = 2;
		Loaded.Profile.MedianWidthMeters = 0.6f;
		Loaded.Profile.bTwoWay = true;
		Loaded.DefaultPlayerLane = 3;
	}

	FString Error;
	if (!FLRDataLoader::Load(RelativePath, Loaded, &Error))
	{
		UE_LOG(LogLRWorld, Warning, TEXT("Route '%s' could not be loaded (%s); using C++ defaults."), *RelativePath, *Error);
	}

	if (Loaded.Profile.bTwoWay != bTwoWayFallback)
	{
		UE_LOG(LogLRWorld, Warning, TEXT("Route '%s' has bTwoWay=%d but the mode requires %d; overriding."),
			*RelativePath, Loaded.Profile.bTwoWay ? 1 : 0, bTwoWayFallback ? 1 : 0);
		Loaded.Profile.bTwoWay = bTwoWayFallback;
	}

	// Streaming invariants: generate beyond the visible window, keep at least the visible part behind.
	FLRRoadStreamingParams& Streaming = Loaded.Streaming;
	Streaming.ChunkLengthM = FMath::Max(20.f, Streaming.ChunkLengthM);
	Streaming.GenerateAheadM = FMath::Max(Streaming.GenerateAheadM, Streaming.VisibleAheadM + 2.f * Streaming.ChunkLengthM);
	Streaming.KeepBehindM = FMath::Max(Streaming.KeepBehindM, Streaming.VisibleBehindM + 2.f * Streaming.ChunkLengthM);
	Loaded.SpawnS = FMath::Max(0.0, Loaded.SpawnS);
	return Loaded;
}

void ULRRoadSubsystem::RebuildForSeed(int32 Seed, bool bTwoWay)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRRoadSubsystem::RebuildForSeed);

	const ULRWorldSettings* Settings = ULRWorldSettings::Get();
	Route = LoadRoute(bTwoWay ? Settings->TwoWayRouteFile : Settings->OneWayRouteFile, bTwoWay);
	CrossSection = LRRoadLayout::ComputeCrossSection(Route.Profile, Route.Dressing);

	RoadSeed = Seed;
	LayoutStream = LRRandom::MakeStream(Seed, LRRandom::EStream::RoadLayout);
	Generator.Reset();
	Path.Reset(Route.Profile, Route.OriginCm, Route.StartYawDeg);
	Generator.GenerateUntil(Path, Route.SpawnS + Route.Streaming.GenerateAheadM, LayoutStream, Route.GenParams);

	PlayerSegmentHint = INDEX_NONE;
	bHasPlayerCoord = false;
	CachedPlayerCoord = FLRRoadCoord(Route.SpawnS, GetLaneCenterD(GetDefaultPlayerLane()));
	LastFocusS = Route.SpawnS;
	bHasRoad = true;

	UE_LOG(LogLRWorld, Log, TEXT("Road rebuilt: seed=%d twoWay=%d lanes=%d segments=%d end=%.0f m"),
		Seed, bTwoWay ? 1 : 0, GetNumLanes(), Path.GetNumSegments(), Path.GetEndS());

	ApplyRouteToActors();
}

void ULRRoadSubsystem::ApplyRouteToActors()
{
	if (!bHasRoad)
	{
		return;
	}

	if (RoadStreamer)
	{
		RoadStreamer->Configure(Route);
		RoadStreamer->ResetStreaming(Path, LastFocusS);
	}

	if (EnvironmentActor && !Route.EnvironmentPresetFile.IsEmpty())
	{
		EnvironmentActor->LoadAndApplyPreset(Route.EnvironmentPresetFile);
	}
}

void ULRRoadSubsystem::UpdatePlayerCoord()
{
	const UWorld* World = GetWorld();
	const APawn* Pawn = nullptr;

	if (const ULRRunContextSubsystem* RunContext = World ? World->GetSubsystem<ULRRunContextSubsystem>() : nullptr)
	{
		Pawn = RunContext->GetPlayerPawn();
	}
	if (!Pawn && World)
	{
		if (const APlayerController* Controller = World->GetFirstPlayerController())
		{
			Pawn = Controller->GetPawn();
		}
	}

	if (!Pawn)
	{
		bHasPlayerCoord = false;
		return;
	}

	FLRRoadCoord Coord;
	bHasPlayerCoord = Path.WorldToRoad(Pawn->GetActorLocation(), Coord, &PlayerSegmentHint);
	if (bHasPlayerCoord)
	{
		CachedPlayerCoord = Coord;
	}
}

void ULRRoadSubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRRoadSubsystem::Tick);

	Super::Tick(DeltaTime);

	if (!bHasRoad)
	{
		return;
	}

	UpdatePlayerCoord();

	// Keep streaming around the last known position if the player is momentarily off the path.
	if (bHasPlayerCoord)
	{
		LastFocusS = CachedPlayerCoord.S;
	}
	const double FocusS = LastFocusS;
	const FLRRoadStreamingParams& Streaming = Route.Streaming;

	Generator.GenerateUntil(Path, FocusS + Streaming.GenerateAheadM, LayoutStream, Route.GenParams);

	// Never trim inside the streamer window (first visible chunk starts at most one chunk before FocusS - VisibleBehindM).
	const double TrimDistance = FMath::Max<double>(Streaming.KeepBehindM, Streaming.VisibleBehindM + Streaming.ChunkLengthM);
	Path.TrimBefore(FocusS - TrimDistance);

	if (RoadStreamer)
	{
		RoadStreamer->UpdateWindow(Path, FocusS);
	}
}

TStatId ULRRoadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULRRoadSubsystem, STATGROUP_Tickables);
}

const FLRLaneDesc& ULRRoadSubsystem::GetLane(int32 LaneIndex) const
{
	static const FLRLaneDesc InvalidLane = []()
	{
		FLRLaneDesc Lane;
		Lane.CenterD = 0.f;
		Lane.WidthMeters = 0.f;
		Lane.Direction = 0;
		return Lane;
	}();

	const TArray<FLRLaneDesc>& Lanes = Path.GetLanes();
	return Lanes.IsValidIndex(LaneIndex) ? Lanes[LaneIndex] : InvalidLane;
}

float ULRRoadSubsystem::GetSpeedLimitKmh(double S) const
{
	// Constant per route for now; S is part of the API for authored routes with speed zones.
	return Route.SpeedLimitKmh;
}

bool ULRRoadSubsystem::GetPlayerCoord(FLRRoadCoord& Out) const
{
	if (!bHasPlayerCoord)
	{
		return false;
	}
	Out = CachedPlayerCoord;
	return true;
}

int32 ULRRoadSubsystem::GetDefaultPlayerLane() const
{
	const TArray<FLRLaneDesc>& Lanes = Path.GetLanes();
	if (Lanes.IsValidIndex(Route.DefaultPlayerLane) && Lanes[Route.DefaultPlayerLane].Direction > 0)
	{
		return Route.DefaultPlayerLane;
	}

	// Fall back to the player-direction lane closest to the requested index.
	int32 Best = INDEX_NONE;
	int32 BestDistance = MAX_int32;
	for (int32 Index = 0; Index < Lanes.Num(); ++Index)
	{
		const int32 Distance = FMath::Abs(Index - Route.DefaultPlayerLane);
		if (Lanes[Index].Direction > 0 && Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best == INDEX_NONE ? 0 : Best;
}
