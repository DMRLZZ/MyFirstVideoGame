// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRPlayerController.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "LREvents.h"
#include "LRGameEventSubsystem.h"
#include "LRGameModeEndless.h"
#include "LRGameRules.h"
#include "LRGameplay.h"
#include "LRInputActions.h"
#include "LRInputCurves.h"
#include "LRInputTypes.h"
#include "LRPlayerCameraManager.h"
#include "LRRunContextSubsystem.h"
#include "LRVehicle.h"

namespace LRPlayerControllerPrivate
{
	/** Rumble values of the current rules, C++ defaults without the game mode. */
	FLRRumbleTuning GetRumble(const UWorld* World)
	{
		const ALRGameModeEndless* GameMode = World ? Cast<ALRGameModeEndless>(World->GetAuthGameMode()) : nullptr;
		return GameMode ? GameMode->GetGameRules().Rumble : FLRRumbleTuning();
	}
}

ALRPlayerController::ALRPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PlayerCameraManagerClass = ALRPlayerCameraManager::StaticClass();
	OverridePlayerInputClass = UEnhancedPlayerInput::StaticClass();
	bShouldPerformFullTickWhenPaused = false;
}

void ALRPlayerController::SetupInputComponent()
{
	// The base class only creates the component when it is missing, and with the class of the project input settings.
	if (InputComponent == nullptr)
	{
		InputComponent = NewObject<UEnhancedInputComponent>(this, TEXT("PC_InputComponent0"));
		InputComponent->RegisterComponent();
	}

	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogLRGameplay, Error, TEXT("The input component is not a UEnhancedInputComponent: driving input is disabled"));
		return;
	}
	if (InputActions)
	{
		return;
	}

	FLRInputTuning Tuning;
	FString Error;
	if (!ULRInputActions::LoadTuning(Tuning, &Error))
	{
		UE_LOG(LogLRGameplay, Warning, TEXT("Using the default input tuning: %s"), *Error);
	}

	InputActions = ULRInputActions::Create(this, Tuning);
	LaneDetector.Configure(InputActions->GetTuning());

	// Without this the pause button could not resume the game.
	InputActions->Pause->bTriggerWhenPaused = true;

	const ULRInputActions* Actions = InputActions;

	EnhancedInput->BindAction(Actions->Throttle, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleThrottle);
	EnhancedInput->BindAction(Actions->Throttle, ETriggerEvent::Completed, this, &ALRPlayerController::HandleThrottleCompleted);
	EnhancedInput->BindAction(Actions->Brake, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleBrake);
	EnhancedInput->BindAction(Actions->Brake, ETriggerEvent::Completed, this, &ALRPlayerController::HandleBrakeCompleted);
	EnhancedInput->BindAction(Actions->Steer, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleSteer);
	EnhancedInput->BindAction(Actions->Steer, ETriggerEvent::Completed, this, &ALRPlayerController::HandleSteerCompleted);
	EnhancedInput->BindAction(Actions->Handbrake, ETriggerEvent::Started, this, &ALRPlayerController::HandleHandbrakeStarted);
	EnhancedInput->BindAction(Actions->Handbrake, ETriggerEvent::Completed, this, &ALRPlayerController::HandleHandbrakeCompleted);
	EnhancedInput->BindAction(Actions->LaneLeft, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleLaneLeft);
	EnhancedInput->BindAction(Actions->LaneRight, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleLaneRight);
	EnhancedInput->BindAction(Actions->CameraCycle, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleCameraCycle);
	EnhancedInput->BindAction(Actions->LookBack, ETriggerEvent::Started, this, &ALRPlayerController::HandleLookBackStarted);
	EnhancedInput->BindAction(Actions->LookBack, ETriggerEvent::Completed, this, &ALRPlayerController::HandleLookBackCompleted);
	EnhancedInput->BindAction(Actions->Restart, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleRestart);
	EnhancedInput->BindAction(Actions->Pause, ETriggerEvent::Triggered, this, &ALRPlayerController::HandlePause);
	EnhancedInput->BindAction(Actions->ToggleControlScheme, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleToggleControlScheme);
	EnhancedInput->BindAction(Actions->CycleVehicle, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleCycleVehicle);
}

void ALRPlayerController::BeginPlay()
{
	Super::BeginPlay();

	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (InputSubsystem && InputActions)
	{
		InputSubsystem->AddMappingContext(InputActions->DrivingContext, ULRInputActions::DrivingContextPriority);
	}

	if (ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		NearMissHandle = Events->OnNearMiss.AddUObject(this, &ALRPlayerController::HandleNearMiss);
		CrashHandle = Events->OnCrash.AddUObject(this, &ALRPlayerController::HandleCrash);
		RunResetHandle = Events->OnRunReset.AddUObject(this, &ALRPlayerController::HandleRunReset);
	}
}

void ALRPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		Events->OnNearMiss.Remove(NearMissHandle);
		Events->OnCrash.Remove(CrashHandle);
		Events->OnRunReset.Remove(RunResetHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ALRPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	ALRVehicle* Vehicle = GetVehicle();
	if (Vehicle && Vehicle->GetControlScheme() == ELRControlScheme::Arcade)
	{
		const int32 LaneRequest = LaneDetector.Update(SteerAxis, DeltaTime);
		if (LaneRequest != 0)
		{
			Vehicle->RequestLaneChange(LaneRequest);
		}
	}
}

void ALRPlayerController::SetControlScheme(ELRControlScheme Scheme)
{
	if (ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this))
	{
		Context->SetControlScheme(Scheme);
	}
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetControlScheme(Scheme);
	}
	LaneDetector.Reset();
}

void ALRPlayerController::ToggleControlScheme()
{
	const ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this);
	const ELRControlScheme Current = Context ? Context->GetControlScheme() : ELRControlScheme::Arcade;
	SetControlScheme(Current == ELRControlScheme::Arcade ? ELRControlScheme::Simcade : ELRControlScheme::Arcade);
}

ALRVehicle* ALRPlayerController::GetVehicle() const
{
	return Cast<ALRVehicle>(GetPawn());
}

ALRPlayerCameraManager* ALRPlayerController::GetLRCameraManager() const
{
	return Cast<ALRPlayerCameraManager>(PlayerCameraManager);
}

void ALRPlayerController::ForwardSteering() const
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetSteeringInput(SteerAxis);
	}
}

void ALRPlayerController::HandleThrottle(const FInputActionValue& Value)
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetThrottleInput(static_cast<float>(Value.Get<FInputActionValue::Axis1D>()));
	}
}

void ALRPlayerController::HandleThrottleCompleted()
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetThrottleInput(0.f);
	}
}

void ALRPlayerController::HandleBrake(const FInputActionValue& Value)
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetBrakeInput(static_cast<float>(Value.Get<FInputActionValue::Axis1D>()));
	}
}

void ALRPlayerController::HandleBrakeCompleted()
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetBrakeInput(0.f);
	}
}

void ALRPlayerController::HandleSteer(const FInputActionValue& Value)
{
	SteerAxis = FLRInputCurves::ClampSteer(static_cast<float>(Value.Get<FInputActionValue::Axis1D>()));
	ForwardSteering();
}

void ALRPlayerController::HandleSteerCompleted()
{
	SteerAxis = 0.f;
	ForwardSteering();
}

void ALRPlayerController::HandleHandbrakeStarted()
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetHandbrakeInput(true);
	}
}

void ALRPlayerController::HandleHandbrakeCompleted()
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetHandbrakeInput(false);
	}
}

void ALRPlayerController::HandleLaneLeft()
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->RequestLaneChange(-1);
	}
}

void ALRPlayerController::HandleLaneRight()
{
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->RequestLaneChange(1);
	}
}

void ALRPlayerController::HandleCameraCycle()
{
	if (ALRPlayerCameraManager* CameraManager = GetLRCameraManager())
	{
		CameraManager->CycleCameraMode();
	}
}

void ALRPlayerController::HandleLookBackStarted()
{
	if (ALRPlayerCameraManager* CameraManager = GetLRCameraManager())
	{
		CameraManager->SetLookBack(true);
	}
}

void ALRPlayerController::HandleLookBackCompleted()
{
	if (ALRPlayerCameraManager* CameraManager = GetLRCameraManager())
	{
		CameraManager->SetLookBack(false);
	}
}

void ALRPlayerController::HandleRestart()
{
	const UWorld* World = GetWorld();
	if (ALRGameModeEndless* GameMode = World ? Cast<ALRGameModeEndless>(World->GetAuthGameMode()) : nullptr)
	{
		GameMode->RestartRun();
	}
}

void ALRPlayerController::HandlePause()
{
	SetPause(!IsPaused());
}

void ALRPlayerController::HandleCycleVehicle()
{
	if (ALRGameModeEndless* GameMode = GetWorld()->GetAuthGameMode<ALRGameModeEndless>())
	{
		GameMode->CycleVehicle();
	}
}

void ALRPlayerController::HandleToggleControlScheme()
{
	ToggleControlScheme();
}

void ALRPlayerController::HandleNearMiss(const FLRNearMissEvent& Event)
{
	const FLRRumbleTuning Rumble = LRPlayerControllerPrivate::GetRumble(GetWorld());
	const float TierScale = static_cast<float>(static_cast<uint8>(Event.Tier));
	const float Intensity = FMath::Min(Rumble.NearMissIntensity * TierScale, 1.f);
	if (Intensity > 0.f)
	{
		PlayDynamicForceFeedback(Intensity, Rumble.NearMissDurationSeconds, true, true, true, true);
	}
}

void ALRPlayerController::HandleCrash(const FLRCrashEvent& Event)
{
	const FLRRumbleTuning Rumble = LRPlayerControllerPrivate::GetRumble(GetWorld());
	if (Event.bSevere)
	{
		PlayDynamicForceFeedback(Rumble.CrashIntensity, Rumble.CrashDurationSeconds, true, true, true, true);
	}
	else
	{
		PlayDynamicForceFeedback(Rumble.ScrapeIntensity, Rumble.ScrapeDurationSeconds, true, true, true, true);
	}
}

void ALRPlayerController::HandleRunReset(const FLRRunResetEvent& Event)
{
	LaneDetector.Reset();
}
