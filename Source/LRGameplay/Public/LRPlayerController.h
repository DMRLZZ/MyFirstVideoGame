// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LRCoreTypes.h"
#include "LRLaneChangeDetector.h"

#include "LRPlayerController.generated.h"

class ALRPlayerCameraManager;
class ALRVehicle;
class ULRInputActions;
struct FInputActionValue;
struct FLRCrashEvent;
struct FLRNearMissEvent;
struct FLRRunResetEvent;

/**
 * Local player controller: the only place that turns devices into game actions.
 *
 * - Creates ULRInputActions (Enhanced Input built at runtime, tuning from Data/Input/Input.json) and activates DrivingContext.
 *   Uses UEnhancedPlayerInput and UEnhancedInputComponent itself, so it does not depend on the project input settings.
 * - Forwards throttle, brake, steering and handbrake to the possessed ALRVehicle. Arcade scheme: the steering axis also
 *   goes through FLRLaneChangeDetector to RequestLaneChange, plus the LaneLeft / LaneRight buttons.
 * - CameraCycle and LookBack go to ALRPlayerCameraManager, Restart to ALRGameModeEndless, Pause toggles the world pause.
 * - Shows the title screen (SLRTitleMenu) while the run state is Garage and forwards its choices to ALRGameModeEndless.
 * - Plays gamepad rumble on near misses (scaled by tier) and crashes, with the values of FLRGameRules::Rumble.
 */
UCLASS()
class LRGAMEPLAY_API ALRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ALRPlayerController(const FObjectInitializer& ObjectInitializer);

	/** Applies a control scheme to the run context and the possessed vehicle. */
	void SetControlScheme(ELRControlScheme Scheme);

	/** Switches between Arcade and Simcade. */
	void ToggleControlScheme();

	/** Input actions of this player (owned by this controller), or nullptr before the input system starts. */
	ULRInputActions* GetInputActions() const { return InputActions; }

	/** Opens the pause menu, or closes it if it is open. */
	void TogglePauseMenu();

	bool IsPauseMenuOpen() const { return PauseMenu.IsValid(); }

	/** True while the title screen is on screen. */
	bool IsTitleMenuOpen() const { return TitleMenu.IsValid(); }

	//~ APlayerController
	virtual void PlayerTick(float DeltaTime) override;

protected:
	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//~ APlayerController
	virtual void SetupInputComponent() override;

private:
	// Input handlers. Axes return to 0 on Completed.
	void HandleThrottle(const FInputActionValue& Value);
	void HandleThrottleCompleted();
	void HandleBrake(const FInputActionValue& Value);
	void HandleBrakeCompleted();
	void HandleSteer(const FInputActionValue& Value);
	void HandleSteerCompleted();
	void HandleHandbrakeStarted();
	void HandleHandbrakeCompleted();
	void HandleLaneLeft();
	void HandleLaneRight();
	void HandleCameraCycle();
	void HandleLookBackStarted();
	void HandleLookBackCompleted();
	void HandleRestart();
	void HandlePause();
	void HandleToggleControlScheme();
	void HandleCycleVehicle();
	void HandleNextTrack();
	void ClosePauseMenu();
	void OpenTitleMenu();
	void CloseTitleMenu();

	/** Opens the title screen in the Garage state and closes it in every other state. */
	void SyncTitleMenu();

	/** The game mode, or nullptr. */
	class ALRGameModeEndless* GetGameMode() const;
	void ReleaseDrivingInput();
	void HandleSettingsChanged(const struct FLRUserSettings& Settings);

	/** Bus callback: rumble scaled by the near-miss tier. */
	void HandleNearMiss(const FLRNearMissEvent& Event);

	/** Bus callback: strong rumble on a severe crash, light on a scrape. */
	void HandleCrash(const FLRCrashEvent& Event);

	/** Bus callback: forgets the held steering direction. */
	void HandleRunReset(const FLRRunResetEvent& Event);

	/** Sends the current steering value to the possessed vehicle. */
	void ForwardSteering() const;

	/** The possessed car, or nullptr. */
	ALRVehicle* GetVehicle() const;

	/** The camera manager, or nullptr. */
	ALRPlayerCameraManager* GetLRCameraManager() const;

	/** Runtime-built actions and mapping contexts. */
	UPROPERTY(Transient)
	TObjectPtr<ULRInputActions> InputActions;

	/** Turns the steering axis into lane change requests (Arcade). */
	FLRLaneChangeDetector LaneDetector;

	/** Menu on screen and the wrapper that keeps it in the viewport. Null while closed. */
	TSharedPtr<class SLRPauseMenu> PauseMenu;
	TSharedPtr<class SWidget> PauseMenuContainer;

	/** Title screen and its viewport wrapper. Null while closed. */
	TSharedPtr<class SLRTitleMenu> TitleMenu;
	TSharedPtr<class SWidget> TitleMenuContainer;
	FDelegateHandle SettingsHandle;

	/** Last steering value, clamped to -1..1. */
	float SteerAxis = 0.f;

	FDelegateHandle NearMissHandle;
	FDelegateHandle CrashHandle;
	FDelegateHandle RunResetHandle;
};
