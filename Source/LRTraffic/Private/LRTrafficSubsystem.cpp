// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRTrafficSubsystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "LRDataLoader.h"
#include "LRDataSubsystem.h"
#include "LREvents.h"
#include "LRGameEventSubsystem.h"
#include "LRRoadParticipant.h"
#include "LRRoadSubsystem.h"
#include "LRRunContextSubsystem.h"
#include "LRTraffic.h"
#include "LRTrafficVehicleActor.h"
#include "LRUnits.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace LRTrafficSubsystemPrivate
{
	/** Longest frame time fed to the accumulator, seconds (hitches are not caught up). */
	constexpr double MaxFrameSeconds = 0.25;

	/** Vehicles keep their actor this far beyond the actor window (avoids flicker at the edge), meters. */
	constexpr double ActorHysteresisM = 20.0;

	/** Extra longitudinal range around the overlap zone handed to the near-miss tracker, meters. */
	constexpr double NearMissMarginM = 15.0;

	/** Fallback player footprint when the pawn does not implement ILRRoadParticipant, meters. */
	constexpr float DefaultPlayerLengthM = 4.5f;
	constexpr float DefaultPlayerWidthM = 1.9f;

	/** Parking spot of hidden far instances, centimeters. */
	const FVector HiddenInstanceLocation(0.0, 0.0, -100000.0);

	const FLinearColor DefaultPaint(0.6f, 0.6f, 0.6f, 1.f);
}

const TCHAR* ULRTrafficSubsystem::ProfileFile = TEXT("Traffic/TrafficProfile_Default.json");

ULRTrafficSubsystem* ULRTrafficSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<ULRTrafficSubsystem>() : nullptr;
}

bool ULRTrafficSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------

void ULRTrafficSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<ULRGameEventSubsystem>();
	Collection.InitializeDependency<ULRRunContextSubsystem>();
	Collection.InitializeDependency<ULRRoadSubsystem>();

	LoadProfile();

	if (ULRGameEventSubsystem* Events = GetWorld()->GetSubsystem<ULRGameEventSubsystem>())
	{
		RunResetHandle = Events->OnRunReset.AddUObject(this, &ULRTrafficSubsystem::HandleRunReset);
		CrashHandle = Events->OnCrash.AddUObject(this, &ULRTrafficSubsystem::HandleCrash);
	}

	if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(GetWorld()))
	{
		DataReloadedHandle = Data->OnDataReloaded.AddUObject(this, &ULRTrafficSubsystem::HandleDataReloaded);
	}
}

void ULRTrafficSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (ULRGameEventSubsystem* Events = World->GetSubsystem<ULRGameEventSubsystem>())
		{
			Events->OnRunReset.Remove(RunResetHandle);
			Events->OnCrash.Remove(CrashHandle);
		}
		if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(World))
		{
			Data->OnDataReloaded.Remove(DataReloadedHandle);
		}
	}
	RunResetHandle.Reset();
	CrashHandle.Reset();
	DataReloadedHandle.Reset();

	// The world destroys the actors; only drop the references.
	ActorPool.Reset();
	ActorSlot.Reset();
	SlotActor.Reset();
	FreeActors.Reset();
	FarTrafficActor = nullptr;
	FarTrafficInstances = nullptr;
	NumFarInstancesAllocated = 0;
	bHasTraffic = false;
	bResetPending = false;

	Super::Deinitialize();
}

void ULRTrafficSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	SpawnPresentation(InWorld);
}

TStatId ULRTrafficSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULRTrafficSubsystem, STATGROUP_Tickables);
}

void ULRTrafficSubsystem::LoadProfile()
{
	FLRTrafficProfile Loaded;
	FString Error;
	if (FLRDataLoader::Load(ProfileFile, Loaded, &Error))
	{
		Profile = MoveTemp(Loaded);
	}
	else
	{
		UE_LOG(LogLRTraffic, Warning, TEXT("Traffic profile '%s' could not be loaded (%s); using C++ defaults."), ProfileFile, *Error);
		Profile = FLRTrafficProfile();
	}
}

void ULRTrafficSubsystem::ReloadProfile()
{
	LoadProfile();
	if (bHasTraffic || bResetPending)
	{
		ResetTraffic(CurrentSeed);
	}
}

void ULRTrafficSubsystem::HandleDataReloaded()
{
	ReloadProfile();
}

// ------------------------------------------------------------------------------------------------
// Public control
// ------------------------------------------------------------------------------------------------

void ULRTrafficSubsystem::ResetTraffic(int32 Seed)
{
	CurrentSeed = Seed;
	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (Road && Road->HasRoad())
	{
		ResetNow(Seed);
	}
	else
	{
		bResetPending = true;
	}
}

void ULRTrafficSubsystem::HandleRunReset(const FLRRunResetEvent& Event)
{
	// Multicast delegates run their listeners in reverse binding order, so the road may not have rebuilt yet:
	// reset now only if the road already holds this run, otherwise wait for the next tick.
	CurrentSeed = Event.Seed;
	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	const bool bTwoWay = Event.Mode == ELRGameModeType::EndlessTwoWay;
	if (Road && Road->HasRoad() && Road->GetRoadSeed() == Event.Seed && Road->GetProfile().bTwoWay == bTwoWay)
	{
		ResetNow(Event.Seed);
	}
	else
	{
		bResetPending = true;
	}
}

void ULRTrafficSubsystem::ResetNow(int32 Seed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRTrafficSubsystem::ResetNow);
	using namespace LRTrafficSubsystemPrivate;

	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (!Road || !Road->HasRoad())
	{
		bResetPending = true;
		return;
	}

	const double SpawnS = Road->GetSpawnS();
	Simulation.Configure(Profile, Road->GetPath().GetLanes(), static_cast<float>(LRUnits::KmhToMs(Road->GetSpeedLimitKmh(SpawnS))));
	Simulation.SetDensityScale(DensityScale);

	// The player is about to be placed at the spawn point (vehicle reset runs on the same event).
	FLRTrafficPlayerState Start;
	Start.bValid = true;
	Start.S = SpawnS;
	Start.D = Road->GetLaneCenterD(Road->GetDefaultPlayerLane());
	Start.LengthM = DefaultPlayerLengthM;
	Start.WidthM = DefaultPlayerWidthM;
	if (const ILRRoadParticipant* Participant = Cast<ILRRoadParticipant>(FindPlayerPawn()))
	{
		const FVector2D Footprint = Participant->GetFootprintSizeMeters();
		Start.LengthM = static_cast<float>(Footprint.X);
		Start.WidthM = static_cast<float>(Footprint.Y);
	}
	Simulation.SetPlayer(Start);
	Simulation.Reset(Seed, true);

	NearMissTracker.Configure(Simulation.GetProfile().NearMiss);
	NearMissTracker.Reset();

	ReleaseAllActors();
	SlotActor.Init(INDEX_NONE, Simulation.GetCapacity());

	Accumulator = 0.0;
	PlayerSegmentHint = INDEX_NONE;
	CurrentSeed = Seed;
	bHasTraffic = true;
	bResetPending = false;

	UE_LOG(LogLRTraffic, Log, TEXT("Traffic reset: seed=%d lanes=%d vehicles=%d"), Seed, Simulation.GetLanes().Num(), Simulation.GetNumVehicles());
}

void ULRTrafficSubsystem::SetSimulationEnabled(bool bEnabled)
{
	if (bEnabled && !bSimulationEnabled)
	{
		Accumulator = 0.0;
	}
	bSimulationEnabled = bEnabled;
}

void ULRTrafficSubsystem::SetDensityScale(float Scale)
{
	DensityScale = FMath::Max(0.f, Scale);
	Simulation.SetDensityScale(DensityScale);
}

void ULRTrafficSubsystem::GetSnapshot(TArray<FLRTrafficVehicleSnapshot>& Out, double MinS, double MaxS) const
{
	Out.Reset();
	Out.Reserve(Simulation.GetNumVehicles());
	for (int32 Slot = 0; Slot < Simulation.GetCapacity(); ++Slot)
	{
		if (!Simulation.IsAlive(Slot))
		{
			continue;
		}
		const double VehicleS = Simulation.GetS(Slot);
		if (VehicleS >= MinS && VehicleS <= MaxS)
		{
			Simulation.GetSnapshot(Slot, Out.AddDefaulted_GetRef());
		}
	}
}

bool ULRTrafficSubsystem::IsTrafficActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	for (const TObjectPtr<ALRTrafficVehicleActor>& Pooled : ActorPool)
	{
		if (Pooled.Get() == Actor)
		{
			return true;
		}
	}
	return false;
}

int32 ULRTrafficSubsystem::GetVehicleIdForActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < ActorPool.Num(); ++Index)
	{
		if (ActorPool[Index].Get() == Actor)
		{
			return ActorSlot.IsValidIndex(Index) && ActorSlot[Index] != INDEX_NONE ? ActorPool[Index]->GetTrafficVehicleId() : INDEX_NONE;
		}
	}
	return INDEX_NONE;
}

// ------------------------------------------------------------------------------------------------
// Crashes
// ------------------------------------------------------------------------------------------------

void ULRTrafficSubsystem::HandleCrash(const FLRCrashEvent& Event)
{
	using namespace LRTrafficSubsystemPrivate;

	const AActor* Other = Event.OtherActor.Get();
	if (!Other)
	{
		return;
	}

	int32 ActorIndex = INDEX_NONE;
	for (int32 Index = 0; Index < ActorPool.Num(); ++Index)
	{
		if (ActorPool[Index].Get() == Other)
		{
			ActorIndex = Index;
			break;
		}
	}
	if (ActorIndex == INDEX_NONE || !ActorSlot.IsValidIndex(ActorIndex) || ActorSlot[ActorIndex] == INDEX_NONE)
	{
		return;
	}

	ALRTrafficVehicleActor* Actor = ActorPool[ActorIndex];
	const int32 Slot = ActorSlot[ActorIndex];
	if (!Actor || !Simulation.IsAlive(Slot) || Simulation.GetId(Slot) != Actor->GetTrafficVehicleId())
	{
		return;
	}

	const int32 VehicleId = Simulation.GetId(Slot);
	NearMissTracker.NotifyContact(VehicleId);
	Simulation.SetWreckedById(VehicleId);

	// Push the wreck away from the contact point, proportionally to its mass and to the impact speed.
	const FLRTrafficProfile& SimProfile = Simulation.GetProfile();
	const FLRTrafficArchetype& Arch = SimProfile.Archetypes[Simulation.GetArchetypeIndex(Slot)];
	FVector Away = Actor->GetActorLocation() - Event.Location;
	Away.Z = 0.0;
	Away = Away.GetSafeNormal();
	const double ImpactCms = LRUnits::MetersToCm(LRUnits::KmhToMs(Event.ImpactSpeedKmh));
	const FVector Impulse = Away * (Arch.MassKg * ImpactCms * SimProfile.WreckImpulseScale);
	Actor->EnterWreckMode(Impulse);

	UE_LOG(LogLRTraffic, Verbose, TEXT("Traffic vehicle %d wrecked (impact %.1f km/h)."), VehicleId, Event.ImpactSpeedKmh);
}

// ------------------------------------------------------------------------------------------------
// Tick
// ------------------------------------------------------------------------------------------------

APawn* ULRTrafficSubsystem::FindPlayerPawn() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	if (const ULRRunContextSubsystem* RunContext = World->GetSubsystem<ULRRunContextSubsystem>())
	{
		if (APawn* Pawn = RunContext->GetPlayerPawn())
		{
			return Pawn;
		}
	}
	const APlayerController* Controller = World->GetFirstPlayerController();
	return Controller ? Controller->GetPawn() : nullptr;
}

bool ULRTrafficSubsystem::SamplePlayer(const ULRRoadSubsystem& Road, FLRTrafficPlayerState& OutPlayer)
{
	using namespace LRTrafficSubsystemPrivate;

	const APawn* Pawn = FindPlayerPawn();
	if (!Pawn)
	{
		return false;
	}

	// Own projection (fresh this frame) instead of the road cache, which may have been updated before the pawn moved.
	FLRRoadCoord Coord;
	if (!Road.WorldToRoad(Pawn->GetActorLocation(), Coord, &PlayerSegmentHint))
	{
		return false;
	}

	const FVector RoadForward = Road.RoadToWorld(Coord.S, Coord.D).GetRotation().GetForwardVector();
	OutPlayer.bValid = true;
	OutPlayer.S = Coord.S;
	OutPlayer.D = Coord.D;
	OutPlayer.SpeedMs = static_cast<float>(LRUnits::CmToMeters(FVector::DotProduct(Pawn->GetVelocity(), RoadForward)));
	OutPlayer.LengthM = DefaultPlayerLengthM;
	OutPlayer.WidthM = DefaultPlayerWidthM;
	if (const ILRRoadParticipant* Participant = Cast<ILRRoadParticipant>(Pawn))
	{
		const FVector2D Footprint = Participant->GetFootprintSizeMeters();
		OutPlayer.LengthM = FMath::Max(0.5f, static_cast<float>(Footprint.X));
		OutPlayer.WidthM = FMath::Max(0.3f, static_cast<float>(Footprint.Y));
	}
	return true;
}

void ULRTrafficSubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRTrafficSubsystem::Tick);
	using namespace LRTrafficSubsystemPrivate;

	Super::Tick(DeltaTime);

	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (!Road || !Road->HasRoad())
	{
		return;
	}

	if (bResetPending)
	{
		ResetNow(CurrentSeed);
	}
	else if (!bHasTraffic)
	{
		// The map was opened without a run reset (no game mode): follow the fallback road.
		ResetNow(Road->GetRoadSeed());
	}
	if (!bHasTraffic)
	{
		return;
	}

	FLRTrafficPlayerState PlayerState;
	const bool bHasPlayer = SamplePlayer(*Road, PlayerState);

	float Alpha = 1.f;
	if (bSimulationEnabled)
	{
		const double StepSeconds = 1.0 / Simulation.GetProfile().FixedStepHz;
		Accumulator += FMath::Clamp(static_cast<double>(DeltaTime), 0.0, MaxFrameSeconds);

		// The simulation lags real time by the accumulator: hand it the player where it was at simulation time.
		FLRTrafficPlayerState SimPlayer = PlayerState;
		if (bHasPlayer)
		{
			SimPlayer.S -= static_cast<double>(PlayerState.SpeedMs) * Accumulator;
			Simulation.SetSpeedLimit(static_cast<float>(LRUnits::KmhToMs(Road->GetSpeedLimitKmh(PlayerState.S))));
		}
		else
		{
			SimPlayer = Simulation.GetPlayer();
			SimPlayer.bValid = false;
		}
		Simulation.SetPlayer(SimPlayer);

		int32 Steps = 0;
		const int32 MaxSteps = FMath::Max(1, Simulation.GetProfile().MaxStepsPerFrame);
		while (Accumulator >= StepSeconds && Steps < MaxSteps)
		{
			Simulation.Step(static_cast<float>(StepSeconds));
			Accumulator -= StepSeconds;
			++Steps;
		}
		if (Accumulator >= StepSeconds)
		{
			// Too slow to catch up: drop the excess instead of spiralling.
			Accumulator = FMath::Fmod(Accumulator, StepSeconds);
		}
		Alpha = static_cast<float>(Accumulator / StepSeconds);
	}

	UpdatePresentation(*Road, Alpha, DeltaTime);

	if (bHasPlayer && bSimulationEnabled)
	{
		UpdateNearMisses(PlayerState, Alpha);
	}
}

void ULRTrafficSubsystem::UpdateNearMisses(const FLRTrafficPlayerState& PlayerState, float Alpha)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRTrafficSubsystem::UpdateNearMisses);
	using namespace LRTrafficSubsystemPrivate;

	FLRNearMissPlayerState Player;
	Player.S = PlayerState.S;
	Player.D = PlayerState.D;
	Player.LengthM = PlayerState.LengthM;
	Player.WidthM = PlayerState.WidthM;
	Player.SpeedMs = PlayerState.SpeedMs;

	const FLRTrafficProfile& SimProfile = Simulation.GetProfile();
	NearMissVehicles.Reset();
	for (int32 Slot = 0; Slot < Simulation.GetCapacity(); ++Slot)
	{
		if (!Simulation.IsAlive(Slot) || Simulation.IsWrecked(Slot))
		{
			continue;
		}
		double VehicleS = 0.0;
		float VehicleD = 0.f;
		Simulation.GetInterpolated(Slot, Alpha, VehicleS, VehicleD);
		const double Range = 0.5 * (static_cast<double>(Player.LengthM) + Simulation.GetLength(Slot)) + NearMissMarginM;
		if (FMath::Abs(VehicleS - Player.S) > Range)
		{
			continue;
		}

		FLRNearMissVehicleState& Vehicle = NearMissVehicles.AddDefaulted_GetRef();
		Vehicle.Id = Simulation.GetId(Slot);
		Vehicle.S = VehicleS;
		Vehicle.D = VehicleD;
		Vehicle.LengthM = Simulation.GetLength(Slot);
		Vehicle.WidthM = Simulation.GetWidth(Slot);
		Vehicle.SpeedMs = Simulation.GetSpeed(Slot) * Simulation.GetDirection(Slot);
		Vehicle.Archetype = SimProfile.Archetypes[Simulation.GetArchetypeIndex(Slot)].Name;
		Vehicle.bOncoming = Simulation.GetDirection(Slot) < 0;
	}

	NearMissEvents.Reset();
	NearMissTracker.Update(GetWorld()->GetTimeSeconds(), Player, NearMissVehicles, NearMissEvents);
	if (NearMissEvents.IsEmpty())
	{
		return;
	}

	if (const ULRGameEventSubsystem* Events = GetWorld()->GetSubsystem<ULRGameEventSubsystem>())
	{
		for (const FLRNearMissEvent& Event : NearMissEvents)
		{
			Events->BroadcastNearMiss(Event);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Presentation
// ------------------------------------------------------------------------------------------------

void ULRTrafficSubsystem::SpawnPresentation(UWorld& InWorld)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ActorPool.IsEmpty())
	{
		const int32 Count = FMath::Clamp(Profile.MaxPooledActors, 0, 256);
		ActorPool.Reserve(Count);
		const FTransform PoolTransform(LRTrafficSubsystemPrivate::HiddenInstanceLocation);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (ALRTrafficVehicleActor* Actor = InWorld.SpawnActor<ALRTrafficVehicleActor>(ALRTrafficVehicleActor::StaticClass(), PoolTransform, SpawnParams))
			{
				ActorPool.Add(Actor);
			}
		}
		ActorSlot.Init(INDEX_NONE, ActorPool.Num());
		FreeActors.Reset(ActorPool.Num());
		for (int32 Index = ActorPool.Num() - 1; Index >= 0; --Index)
		{
			FreeActors.Add(Index);
		}
		ActorCandidates.Reserve(Profile.MaxVehicles);
	}

	if (!FarTrafficActor && Profile.bDrawFarVehicles)
	{
		FarTrafficActor = InWorld.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (FarTrafficActor)
		{
			UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(FarTrafficActor, TEXT("FarTrafficInstances"));
			Instances->SetMobility(EComponentMobility::Movable);
			Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Instances->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			Instances->SetGenerateOverlapEvents(false);
			Instances->SetCanEverAffectNavigation(false);
			if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				Instances->SetStaticMesh(Cube);
			}
			if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
			{
				Instances->SetMaterial(0, Material);
			}
			FarTrafficActor->SetRootComponent(Instances);
			Instances->RegisterComponent();
			FarTrafficActor->AddInstanceComponent(Instances);
			FarTrafficInstances = Instances;
			NumFarInstancesAllocated = 0;
			FarTransforms.Reserve(Profile.MaxVehicles);
		}
	}
}

void ULRTrafficSubsystem::ReleaseActor(int32 ActorIndex)
{
	if (!ActorSlot.IsValidIndex(ActorIndex) || ActorSlot[ActorIndex] == INDEX_NONE)
	{
		return;
	}
	const int32 Slot = ActorSlot[ActorIndex];
	if (SlotActor.IsValidIndex(Slot) && SlotActor[Slot] == ActorIndex)
	{
		SlotActor[Slot] = INDEX_NONE;
	}
	ActorSlot[ActorIndex] = INDEX_NONE;
	if (ALRTrafficVehicleActor* Actor = ActorPool[ActorIndex])
	{
		Actor->Deactivate();
	}
	FreeActors.Add(ActorIndex);
}

void ULRTrafficSubsystem::ReleaseAllActors()
{
	for (int32 Index = 0; Index < ActorSlot.Num(); ++Index)
	{
		ReleaseActor(Index);
	}
}

FTransform ULRTrafficSubsystem::ComputeVehicleTransform(const ULRRoadSubsystem& Road, int32 Slot, double InS, float InD, bool bGroundLevel) const
{
	const FTransform RoadTransform = Road.RoadToWorld(InS, InD);

	// Local yaw in the road frame: travel direction (oncoming = 180) plus the lane change heading offset.
	const float LocalYaw = Simulation.GetYawOffsetDeg(Slot) + (Simulation.GetDirection(Slot) < 0 ? 180.f : 0.f);
	const FQuat Rotation = RoadTransform.GetRotation() * FQuat(FRotator(0.f, LocalYaw, 0.f));
	if (bGroundLevel)
	{
		return FTransform(Rotation, RoadTransform.GetLocation());
	}

	// Body center for the far instanced cube (basic cube = 1 m, so the scale is the footprint in meters).
	const FLRTrafficArchetype& Arch = Simulation.GetProfile().Archetypes[Simulation.GetArchetypeIndex(Slot)];
	const FVector Size(Simulation.GetLength(Slot), Simulation.GetWidth(Slot), Arch.HeightMeters);
	const FVector Center = RoadTransform.GetLocation() + Rotation.GetUpVector() * LRUnits::MetersToCm(0.5 * Arch.HeightMeters);
	return FTransform(Rotation, Center, Size);
}

void ULRTrafficSubsystem::UpdatePresentation(const ULRRoadSubsystem& Road, float Alpha, float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRTrafficSubsystem::UpdatePresentation);
	using namespace LRTrafficSubsystemPrivate;

	const FLRTrafficProfile& SimProfile = Simulation.GetProfile();
	const double PlayerS = Simulation.GetPlayer().S;
	const double ActorMinS = PlayerS - SimProfile.Windows.ActorBehindM;
	const double ActorMaxS = PlayerS + SimProfile.Windows.ActorAheadM;

	// 1. Release actors whose vehicle is gone, recycled or out of the actor window (wrecks stay until removed).
	for (int32 Index = 0; Index < ActorPool.Num(); ++Index)
	{
		const int32 Slot = ActorSlot[Index];
		if (Slot == INDEX_NONE)
		{
			continue;
		}
		const ALRTrafficVehicleActor* Actor = ActorPool[Index];
		bool bKeep = Actor && Simulation.IsAlive(Slot) && Simulation.GetId(Slot) == Actor->GetTrafficVehicleId();
		if (bKeep && !Simulation.IsWrecked(Slot))
		{
			const double VehicleS = Simulation.GetS(Slot);
			bKeep = VehicleS >= ActorMinS - ActorHysteresisM && VehicleS <= ActorMaxS + ActorHysteresisM;
		}
		if (!bKeep)
		{
			ReleaseActor(Index);
		}
	}

	// 2. Nearest vehicles of the actor window without an actor get one.
	ActorCandidates.Reset();
	for (int32 Slot = 0; Slot < Simulation.GetCapacity(); ++Slot)
	{
		if (!Simulation.IsAlive(Slot) || Simulation.IsWrecked(Slot) || SlotActor[Slot] != INDEX_NONE)
		{
			continue;
		}
		const double VehicleS = Simulation.GetS(Slot);
		if (VehicleS >= ActorMinS && VehicleS <= ActorMaxS)
		{
			ActorCandidates.Emplace(static_cast<float>(FMath::Abs(VehicleS - PlayerS)), Slot);
		}
	}
	if (!ActorCandidates.IsEmpty() && !FreeActors.IsEmpty())
	{
		ActorCandidates.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
		for (const TPair<float, int32>& Candidate : ActorCandidates)
		{
			if (FreeActors.IsEmpty())
			{
				break;
			}
			const int32 Slot = Candidate.Value;
			const int32 ActorIndex = FreeActors.Pop(EAllowShrinking::No);
			ALRTrafficVehicleActor* Actor = ActorPool[ActorIndex];
			if (!Actor)
			{
				continue;
			}
			const FLRTrafficArchetype& Arch = SimProfile.Archetypes[Simulation.GetArchetypeIndex(Slot)];
			const int32 ColorIndex = Simulation.GetColorIndex(Slot);
			const FLinearColor Paint = Arch.Colors.IsValidIndex(ColorIndex) ? Arch.Colors[ColorIndex] : DefaultPaint;
			Actor->ApplyArchetype(Arch, Paint);
			Actor->Activate(Simulation.GetId(Slot));
			ActorSlot[ActorIndex] = Slot;
			SlotActor[Slot] = ActorIndex;
		}
	}

	// 3. Move the kinematic actors (interpolated between the last two simulation steps).
	const UWorld* World = GetWorld();
	const double TimeSeconds = World ? World->GetTimeSeconds() : 0.0;
	const bool bBlinkOn = FMath::Frac(TimeSeconds * SimProfile.BlinkerFrequencyHz) < 0.5;
	for (int32 Index = 0; Index < ActorPool.Num(); ++Index)
	{
		const int32 Slot = ActorSlot[Index];
		ALRTrafficVehicleActor* Actor = ActorPool[Index];
		if (Slot == INDEX_NONE || !Actor || Actor->IsWrecked())
		{
			continue;
		}
		double VehicleS = 0.0;
		float VehicleD = 0.f;
		Simulation.GetInterpolated(Slot, Alpha, VehicleS, VehicleD);
		const FTransform Ground = ComputeVehicleTransform(Road, Slot, VehicleS, VehicleD, true);
		Actor->UpdateKinematic(Ground.GetLocation(), Ground.Rotator(), Simulation.GetLightFlags(Slot), bBlinkOn, DeltaTime);
	}

	// 4. Every other live vehicle is drawn as one instance of the far mesh (no collision).
	if (!FarTrafficInstances || !SimProfile.bDrawFarVehicles)
	{
		return;
	}
	FarTransforms.Reset();
	for (int32 Slot = 0; Slot < Simulation.GetCapacity(); ++Slot)
	{
		if (!Simulation.IsAlive(Slot) || SlotActor[Slot] != INDEX_NONE)
		{
			continue;
		}
		double VehicleS = 0.0;
		float VehicleD = 0.f;
		Simulation.GetInterpolated(Slot, Alpha, VehicleS, VehicleD);
		FarTransforms.Add(ComputeVehicleTransform(Road, Slot, VehicleS, VehicleD, false));
	}

	// Instances only grow (high-water mark); unused ones are parked hidden with a zero scale.
	while (NumFarInstancesAllocated < FarTransforms.Num())
	{
		FarTrafficInstances->AddInstance(FTransform(FQuat::Identity, HiddenInstanceLocation, FVector::ZeroVector), false);
		++NumFarInstancesAllocated;
	}
	while (FarTransforms.Num() < NumFarInstancesAllocated)
	{
		FarTransforms.Emplace(FQuat::Identity, HiddenInstanceLocation, FVector::ZeroVector);
	}
	if (NumFarInstancesAllocated > 0)
	{
		FarTrafficInstances->BatchUpdateInstancesTransforms(0, FarTransforms, true, true, true);
	}
}
