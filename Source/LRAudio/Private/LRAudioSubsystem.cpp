// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRAudioSubsystem.h"

#include "LRUserSettings.h"

#include "DSP/LRVehicleSynth.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LREvents.h"
#include "LRGameEventSubsystem.h"
#include "LRRunContextSubsystem.h"
#include "LRTrafficSubsystem.h"
#include "LRTrafficTypes.h"
#include "LRVehicle.h"
#include "LRVehicleAudioComponent.h"
#include "TimerManager.h"

namespace LRAudioSubsystemPrivate
{
	/** Seconds between checks of the player pawn. Not a design value: only how fast a new pawn gets its sound. */
	constexpr float RefreshIntervalSeconds = 0.25f;
}

ULRAudioSubsystem* ULRAudioSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<ULRAudioSubsystem>() : nullptr;
}

bool ULRAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULRAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<ULRGameEventSubsystem>();
	Collection.InitializeDependency<ULRRunContextSubsystem>();

	if (ULRGameEventSubsystem* Events = GetWorld()->GetSubsystem<ULRGameEventSubsystem>())
	{
		NearMissHandle = Events->OnNearMiss.AddUObject(this, &ULRAudioSubsystem::HandleNearMiss);
		CrashHandle = Events->OnCrash.AddUObject(this, &ULRAudioSubsystem::HandleCrash);
		CameraModeHandle = Events->OnCameraModeChanged.AddUObject(this, &ULRAudioSubsystem::HandleCameraModeChanged);
		RunResetHandle = Events->OnRunReset.AddUObject(this, &ULRAudioSubsystem::HandleRunReset);
	}
}

void ULRAudioSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);

		if (ULRGameEventSubsystem* Events = World->GetSubsystem<ULRGameEventSubsystem>())
		{
			Events->OnNearMiss.Remove(NearMissHandle);
			Events->OnCrash.Remove(CrashHandle);
			Events->OnCameraModeChanged.Remove(CameraModeHandle);
			Events->OnRunReset.Remove(RunResetHandle);
		}
	}
	NearMissHandle.Reset();
	CrashHandle.Reset();
	CameraModeHandle.Reset();
	RunResetHandle.Reset();
	PlayerAudio.Reset();

	Super::Deinitialize();
}

void ULRAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	RefreshPlayerAudio();
	ApplyEffectsVolume();
	if (ULRUserSettingsSubsystem* Settings = ULRUserSettingsSubsystem::Get(this))
	{
		SettingsHandle = Settings->OnChanged.AddWeakLambda(this, [this](const FLRUserSettings&) { ApplyEffectsVolume(); });
	}
	InWorld.GetTimerManager().SetTimer(RefreshTimer, FTimerDelegate::CreateUObject(this, &ULRAudioSubsystem::RefreshPlayerAudio),
		LRAudioSubsystemPrivate::RefreshIntervalSeconds, true);
}

void ULRAudioSubsystem::ApplyEffectsVolume() const
{
	const ULRUserSettingsSubsystem* Settings = ULRUserSettingsSubsystem::Get(this);
	if (ULRVehicleAudioComponent* VehicleAudio = PlayerAudio.Get(); VehicleAudio && Settings)
	{
		VehicleAudio->SetVolumeMultiplier(Settings->GetSettings().EffectsVolume);
	}
}

void ULRAudioSubsystem::HandleNearMiss(const FLRNearMissEvent& Event)
{
	RefreshPlayerAudio();
	if (ULRVehicleAudioComponent* VehicleAudio = PlayerAudio.Get())
	{
		const float Pan = FindWhooshPan(Event, VehicleAudio->GetProfile().WhooshSideProbeRadiusMeters);
		VehicleAudio->TriggerNearMissWhoosh(Event.RelativeSpeedKmh, Event.ClearanceMeters, Pan);
	}
}

void ULRAudioSubsystem::HandleCrash(const FLRCrashEvent& Event)
{
	RefreshPlayerAudio();
	if (ULRVehicleAudioComponent* VehicleAudio = PlayerAudio.Get())
	{
		// A scrape keeps the engine running; only an impact that can end the run kills it.
		VehicleAudio->TriggerCrash(Event.ImpactSpeedKmh, Event.bSevere);
	}
}

void ULRAudioSubsystem::HandleCameraModeChanged(const FLRCameraModeChangedEvent& Event)
{
	bInteriorCamera = Event.bInterior;
	RefreshPlayerAudio();
	if (ULRVehicleAudioComponent* VehicleAudio = PlayerAudio.Get())
	{
		VehicleAudio->SetInteriorMix(bInteriorCamera);
	}
}

void ULRAudioSubsystem::HandleRunReset(const FLRRunResetEvent& Event)
{
	RefreshPlayerAudio();
	if (ULRVehicleAudioComponent* VehicleAudio = PlayerAudio.Get())
	{
		VehicleAudio->ResetForNewRun();
	}
}

void ULRAudioSubsystem::RefreshPlayerAudio()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APawn* Pawn = nullptr;
	if (const ULRRunContextSubsystem* RunContext = World->GetSubsystem<ULRRunContextSubsystem>())
	{
		Pawn = RunContext->GetPlayerPawn();
	}
	if (!Pawn)
	{
		if (const APlayerController* Controller = World->GetFirstPlayerController())
		{
			Pawn = Controller->GetPawn();
		}
	}

	ALRVehicle* PlayerVehicle = Cast<ALRVehicle>(Pawn);
	if (!PlayerVehicle)
	{
		return;
	}

	ULRVehicleAudioComponent* Current = PlayerAudio.Get();
	if (Current && Current->GetBoundVehicle() == PlayerVehicle)
	{
		return;
	}

	if (Current)
	{
		Current->BindToVehicle(nullptr);
	}
	ULRVehicleAudioComponent* VehicleAudio = ULRVehicleAudioComponent::FindOrAddTo(PlayerVehicle);
	VehicleAudio->SetInteriorMix(bInteriorCamera);
	PlayerAudio = VehicleAudio;
}

float ULRAudioSubsystem::FindWhooshPan(const FLRNearMissEvent& Event, float ProbeRadiusMeters) const
{
	const ULRTrafficSubsystem* Traffic = ULRTrafficSubsystem::Get(this);
	if (!Traffic)
	{
		return 0.f;
	}

	// Near misses are rare events (not per frame), so a short-lived array is fine here.
	TArray<FLRTrafficVehicleSnapshot> NearbyVehicles;
	const double CenterS = Event.PlayerCoord.S;
	Traffic->GetSnapshot(NearbyVehicles, CenterS - ProbeRadiusMeters, CenterS + ProbeRadiusMeters);
	for (const FLRTrafficVehicleSnapshot& TrafficVehicle : NearbyVehicles)
	{
		if (TrafficVehicle.Id == Event.TrafficVehicleId)
		{
			return FLRVehicleSynth::ComputeWhooshPan(Event.PlayerCoord.D, TrafficVehicle.D);
		}
	}
	return 0.f;
}
