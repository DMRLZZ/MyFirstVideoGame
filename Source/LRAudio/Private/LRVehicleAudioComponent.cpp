// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRVehicleAudioComponent.h"

#include "DSP/LRVehicleSynth.h"
#include "DSP/LRVehicleSynthBridge.h"
#include "GameFramework/Actor.h"
#include "LRDataSubsystem.h"
#include "LRVehicle.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Sound/SoundGenerator.h"

/**
 * Connects the audio mixer to the bridge. Created by the mixer through CreateSoundGenerator and alive for one playback;
 * it never references the UObject, so the component can be destroyed while the mixer finishes a buffer.
 */
class FLRVehicleSoundGenerator final : public ISoundGenerator
{
public:
	FLRVehicleSoundGenerator(const TSharedRef<FLRVehicleSynthBridge, ESPMode::ThreadSafe>& InBridge, float InSampleRate, int32 InNumChannels)
		: Bridge(InBridge)
		, SampleRate(InSampleRate)
		, NumChannels(InNumChannels)
	{
	}

	//~ ISoundGenerator
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
	{
		return Bridge->Render(OutAudio, NumSamples, SampleRate, NumChannels);
	}

	virtual int32 GetNumChannels() const override
	{
		return NumChannels;
	}

private:
	TSharedRef<FLRVehicleSynthBridge, ESPMode::ThreadSafe> Bridge;
	float SampleRate;
	int32 NumChannels;
};

ULRVehicleAudioComponent::ULRVehicleAudioComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	NumChannels = 2;
	bAllowSpatialization = false;

	Bridge = MakeShared<FLRVehicleSynthBridge, ESPMode::ThreadSafe>();
}

ULRVehicleAudioComponent* ULRVehicleAudioComponent::FindOrAddTo(ALRVehicle* InVehicle)
{
	if (!InVehicle)
	{
		return nullptr;
	}

	ULRVehicleAudioComponent* VehicleAudio = InVehicle->FindComponentByClass<ULRVehicleAudioComponent>();
	if (!VehicleAudio)
	{
		VehicleAudio = NewObject<ULRVehicleAudioComponent>(InVehicle, TEXT("VehicleAudio"));
		VehicleAudio->SetupAttachment(InVehicle->GetRootComponent());
		VehicleAudio->RegisterComponent();
		InVehicle->AddInstanceComponent(VehicleAudio);
	}
	VehicleAudio->BindToVehicle(InVehicle);
	return VehicleAudio;
}

void ULRVehicleAudioComponent::BindToVehicle(ALRVehicle* InVehicle)
{
	if (!InVehicle)
	{
		Vehicle.Reset();
		bProfileLoaded = false;
		UnbindDataReload();
		Stop();
		return;
	}

	if (Vehicle.Get() == InVehicle && IsPlaying())
	{
		return;
	}

	Vehicle = InVehicle;
	bEngineCut = false;
	LoadProfileFromVehicle(*InVehicle);
	PublishVehicleState(*InVehicle);

	if (!DataReloadedHandle.IsValid())
	{
		if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(this))
		{
			DataReloadedHandle = Data->OnDataReloaded.AddUObject(this, &ULRVehicleAudioComponent::HandleDataReloaded);
		}
	}

	SetComponentTickEnabled(true);
	Start();
}

void ULRVehicleAudioComponent::TriggerNearMissWhoosh(float RelativeSpeedKmh, float ClearanceMeters, float Pan)
{
	FLRVehicleSynthEvent Event;
	Event.Type = FLRVehicleSynthEvent::EType::Whoosh;
	Event.Intensity = FLRVehicleSynth::ComputeWhooshIntensity(Profile, RelativeSpeedKmh, ClearanceMeters);
	Event.Pan = Pan;
	Bridge->PushEvent(Event);
}

void ULRVehicleAudioComponent::TriggerCrash(float ImpactSpeedKmh, bool bCutEngine)
{
	FLRVehicleSynthEvent Event;
	Event.Type = FLRVehicleSynthEvent::EType::Crash;
	Event.Intensity = FLRVehicleSynth::ComputeCrashIntensity(Profile, ImpactSpeedKmh);
	Bridge->PushEvent(Event);

	bEngineCut = bEngineCut || bCutEngine;
}

void ULRVehicleAudioComponent::SetInteriorMix(bool bInInterior)
{
	bInterior = bInInterior;
}

void ULRVehicleAudioComponent::ResetForNewRun()
{
	bEngineCut = false;

	FLRVehicleSynthEvent Event;
	Event.Type = FLRVehicleSynthEvent::EType::Reset;
	Bridge->PushEvent(Event);
}

void ULRVehicleAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TRACE_CPUPROFILER_EVENT_SCOPE(ULRVehicleAudioComponent::TickComponent);

	Bridge->ReclaimRetiredProfile();

	const ALRVehicle* BoundVehicle = Vehicle.Get();
	if (!BoundVehicle)
	{
		return;
	}

	if (!bProfileLoaded || BoundVehicle->GetDefinition().AudioProfile != LoadedProfileName)
	{
		LoadProfileFromVehicle(*BoundVehicle);
	}
	PublishVehicleState(*BoundVehicle);
}

bool ULRVehicleAudioComponent::Init(int32& SampleRate)
{
	NumChannels = 2;
	return true;
}

ISoundGeneratorPtr ULRVehicleAudioComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<FLRVehicleSoundGenerator, ESPMode::ThreadSafe>(Bridge.ToSharedRef(), InParams.SampleRate, InParams.NumChannels);
}

void ULRVehicleAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindDataReload();
	Stop();

	Super::EndPlay(EndPlayReason);
}

void ULRVehicleAudioComponent::LoadProfileFromVehicle(const ALRVehicle& InVehicle)
{
	LoadedProfileName = InVehicle.GetDefinition().AudioProfile;
	FLREngineAudioProfile::LoadById(LoadedProfileName, Profile);
	bProfileLoaded = true;
	Bridge->SubmitProfile(Profile);
}

void ULRVehicleAudioComponent::PublishVehicleState(const ALRVehicle& InVehicle)
{
	FLRVehicleSynthParams Params;
	Params.Rpm = InVehicle.GetEngineRpm();
	Params.MaxRpm = InVehicle.GetEngineMaxRpm();
	Params.Throttle = InVehicle.GetThrottleInput();
	Params.SpeedMs = InVehicle.GetForwardSpeedMs();
	Params.RoadS = InVehicle.GetRoadCoord().S;
	Params.WheelbaseMeters = InVehicle.GetDefinition().WheelbaseMeters;
	Params.bInterior = bInterior;
	Params.bEngineOn = !bEngineCut && !InVehicle.IsWrecked();
	Params.bHasRoadS = InVehicle.GetCurrentLane() != INDEX_NONE;
	Bridge->SetParams(Params);
}

void ULRVehicleAudioComponent::HandleDataReloaded()
{
	if (const ALRVehicle* BoundVehicle = Vehicle.Get())
	{
		LoadProfileFromVehicle(*BoundVehicle);
	}
}

void ULRVehicleAudioComponent::UnbindDataReload()
{
	if (DataReloadedHandle.IsValid())
	{
		if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(this))
		{
			Data->OnDataReloaded.Remove(DataReloadedHandle);
		}
		DataReloadedHandle.Reset();
	}
}
