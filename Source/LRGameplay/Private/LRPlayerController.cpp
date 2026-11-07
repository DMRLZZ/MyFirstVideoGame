// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRPlayerController.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Framework/Application/SlateApplication.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LREvents.h"
#include "LRGameEventSubsystem.h"
#include "LRGameModeEndless.h"
#include "LRGameRules.h"
#include "LRGameplay.h"
#include "LRInputActions.h"
#include "LRInputCurves.h"
#include "LRInputTypes.h"
#include "LRMusicSubsystem.h"
#include "LRPlayerCameraManager.h"
#include "LRRunContextSubsystem.h"
#include "LRUserSettings.h"
#include "LRVehicle.h"
#include "SLRPauseMenu.h"
#include "Widgets/SWeakWidget.h"

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
	EnhancedInput->BindAction(Actions->NextTrack, ETriggerEvent::Triggered, this, &ALRPlayerController::HandleNextTrack);
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

	if (ULRUserSettingsSubsystem* Settings = ULRUserSettingsSubsystem::Get(this))
	{
		SettingsHandle = Settings->OnChanged.AddUObject(this, &ALRPlayerController::HandleSettingsChanged);
		SetControlScheme(Settings->GetSettings().ControlScheme);
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
	if (ULRUserSettingsSubsystem* Settings = ULRUserSettingsSubsystem::Get(this))
	{
		Settings->OnChanged.Remove(SettingsHandle);
	}
	if (PauseMenu.IsValid())
	{
		ClosePauseMenu();
	}

	Super::EndPlay(EndPlayReason);
}

void ALRPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	ALRVehicle* Vehicle = GetVehicle();
	if (const ULRUserSettingsSubsystem* Settings = ULRUserSettingsSubsystem::Get(this); Settings && Vehicle)
	{
		Vehicle->SetSteeringSensitivity(Settings->GetSettings().SteeringSensitivity);
	}
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
	const ELRControlScheme Current = Context ? Context->GetControlScheme() : ELRControlScheme::Smooth;
	const ELRControlScheme Next = Current == ELRControlScheme::Smooth ? ELRControlScheme::Arcade
		: (Current == ELRControlScheme::Arcade ? ELRControlScheme::Simcade : ELRControlScheme::Smooth);
	SetControlScheme(Next);

	// The key and the menu are two views of the same setting.
	if (ULRUserSettingsSubsystem* Settings = ULRUserSettingsSubsystem::Get(this))
	{
		FLRUserSettings Edited = Settings->GetSettings();
		Edited.ControlScheme = Next;
		Settings->SetSettings(Edited);
	}
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
	TogglePauseMenu();
}

void ALRPlayerController::TogglePauseMenu()
{
	if (PauseMenu.IsValid())
	{
		ClosePauseMenu();
		return;
	}

	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
	if (Viewport == nullptr || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// Key releases do not arrive while the menu has the input: without this the car would keep the keys "held".
	ReleaseDrivingInput();
	SetPause(true);

	TWeakObjectPtr<ALRPlayerController> WeakThis(this);
	PauseMenu = SNew(SLRPauseMenu)
		.Settings(ULRUserSettingsSubsystem::Get(this))
		.Music(ULRMusicSubsystem::Get(this))
		.CarName_Lambda([WeakThis]()
		{
			const ALRVehicle* Vehicle = WeakThis.IsValid() ? WeakThis->GetVehicle() : nullptr;
			return Vehicle ? FText::FromString(Vehicle->GetDefinition().DisplayName) : FText::GetEmpty();
		})
		.OnResume_Lambda([WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->ClosePauseMenu();
			}
		})
		.OnRestart_Lambda([WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->ClosePauseMenu();
				WeakThis->HandleRestart();
			}
		})
		.OnNextCar_Lambda([WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleCycleVehicle();
			}
		})
		.OnQuit_Lambda([WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				UKismetSystemLibrary::QuitGame(WeakThis.Get(), WeakThis.Get(), EQuitPreference::Quit, false);
			}
		});

	PauseMenuContainer = SNew(SWeakWidget).PossiblyNullContent(PauseMenu);
	Viewport->AddViewportWidgetContent(PauseMenuContainer.ToSharedRef(), 100);

	// The input mode applies its focus later in the frame, so it has to name the widget that should end up with it.
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(PauseMenu->GetInitialFocus());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	PauseMenu->FocusFirstItem();
}

void ALRPlayerController::ClosePauseMenu()
{
	if (UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr; Viewport && PauseMenuContainer.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(PauseMenuContainer.ToSharedRef());
	}
	PauseMenu.Reset();
	PauseMenuContainer.Reset();

	SetShowMouseCursor(false);
	SetInputMode(FInputModeGameOnly());
	SetPause(false);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ALRPlayerController::ReleaseDrivingInput()
{
	SteerAxis = 0.f;
	if (ALRVehicle* Vehicle = GetVehicle())
	{
		Vehicle->SetThrottleInput(0.f);
		Vehicle->SetBrakeInput(0.f);
		Vehicle->SetSteeringInput(0.f);
		Vehicle->SetHandbrakeInput(false);
	}
	if (ALRPlayerCameraManager* CameraManager = GetLRCameraManager())
	{
		CameraManager->SetLookBack(false);
	}
}

void ALRPlayerController::HandleSettingsChanged(const FLRUserSettings& Settings)
{
	const ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this);
	if (Context && Context->GetControlScheme() != Settings.ControlScheme)
	{
		SetControlScheme(Settings.ControlScheme);
	}
}

void ALRPlayerController::HandleNextTrack()
{
	if (ULRMusicSubsystem* Music = ULRMusicSubsystem::Get(this))
	{
		Music->NextTrack();
	}
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
