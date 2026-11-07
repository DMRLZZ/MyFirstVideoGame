// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "LRInputTypes.h"
#include "UObject/Object.h"

#include "LRInputActions.generated.h"

class UInputAction;
class UInputMappingContext;
class UInputModifierDeadZone;
class UInputModifierNegate;
class UInputModifierResponseCurveExponential;

/**
 * Runtime-built Enhanced Input setup for LEFTRIGHT (no .uasset files): every UInputAction and the two
 * UInputMappingContexts, with keyboard AND gamepad mappings for every action.
 *
 * Ownership: created with Create() and owned by its Outer (typically the ALRPlayerController, kept in a
 * UPROPERTY so it is not garbage collected). All actions, contexts, modifiers and triggers are sub-objects of it.
 *
 * Usage (player controller):
 *   Actions = ULRInputActions::Create(this, Tuning);
 *   Subsystem->AddMappingContext(Actions->DrivingContext, ULRInputActions::DrivingContextPriority);
 *   InputComponent->BindAction(Actions->Throttle, ETriggerEvent::Triggered, ...);
 *
 * Value conventions:
 * - Throttle, Brake: Axis1D in [0, 1] (gamepad triggers already dead-zoned).
 * - Steer: Axis1D, + = right. Cumulative accumulation (A + D = 0); clamp with FLRInputCurves::ClampSteer.
 *   The gamepad stick is already dead-zoned, curved (SteeringExponent) and optionally inverted.
 * - LaneLeft, LaneRight, CameraCycle, Pause, Restart, ToggleControlScheme, MenuConfirm, MenuBack: Boolean with a
 *   Pressed trigger on every mapping (fire once per press).
 * - Handbrake, LookBack, Horn: Boolean, hold semantics (default Down trigger).
 */
UCLASS(Transient)
class LRINPUT_API ULRInputActions : public UObject
{
	GENERATED_BODY()

public:
	/** Suggested priority for DrivingContext when added to UEnhancedInputLocalPlayerSubsystem. */
	static constexpr int32 DrivingContextPriority = 0;

	/** Suggested priority for MenuContext (above driving, so menus win shared keys such as Escape). */
	static constexpr int32 MenuContextPriority = 1;

	/** Relative path (under Data/) of the tuning file. */
	static const TCHAR* TuningDataPath;

	/**
	 * Builds every action and mapping context.
	 * @param Outer   Owner of the new object; the transient package is used when null.
	 * @param Tuning  Dead zones, steering curve and invert flag baked into the gamepad mappings (sanitized first).
	 */
	static ULRInputActions* Create(UObject* Outer, const FLRInputTuning& Tuning);

	/**
	 * Loads Data/Input/Input.json into InOutTuning. On failure InOutTuning is left untouched (C++ defaults stay)
	 * and OutError receives the reason.
	 */
	static bool LoadTuning(FLRInputTuning& InOutTuning, FString* OutError = nullptr);

	/**
	 * Updates the baked modifiers (dead zones, steering exponent, invert) in place without recreating actions.
	 * Callers must then ask the Enhanced Input subsystem to rebuild its control mappings
	 * (IEnhancedInputSubsystemInterface::RequestRebuildControlMappings) for the change to reach the player input.
	 */
	void ApplyTuning(const FLRInputTuning& InTuning);

	/** Tuning currently baked into the mappings (sanitized copy). */
	const FLRInputTuning& GetTuning() const { return Tuning; }

	/** Appends every action of both contexts to OutActions (driving actions first, then menu actions). */
	void GetAllActions(TArray<UInputAction*>& OutActions) const;

	/**
	 * First key mapped to Action for the given device family, searching DrivingContext then MenuContext.
	 * Used by the HUD to draw button prompts. Returns EKeys::Invalid when there is none.
	 */
	FKey GetPromptKey(const UInputAction* Action, ELRInputDevice Device) const;

	/** Accelerator pedal, Axis1D [0, 1]. Keys: W, Up, Gamepad right trigger. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> Throttle;

	/** Brake pedal (and reverse at standstill, decided by the vehicle), Axis1D [0, 1]. Keys: S, Down, Gamepad left trigger. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> Brake;

	/** Steering, Axis1D (+ = right). Keys: D/Right = +1, A/Left = -1, Gamepad left stick X. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> Steer;

	/** Handbrake, Boolean held. Keys: SpaceBar, Gamepad face button right. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> Handbrake;

	/** Arcade lane change to the left, Boolean pressed. Keys: Q, Gamepad D-pad left. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> LaneLeft;

	/** Arcade lane change to the right, Boolean pressed. Keys: E, Gamepad D-pad right. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> LaneRight;

	/** Cycles the camera mode, Boolean pressed. Keys: C, Gamepad face button top. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> CameraCycle;

	/** Look back while held, Boolean. Keys: B, Gamepad right shoulder. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> LookBack;

	/** Horn while held, Boolean. Keys: H, Gamepad left thumbstick click. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> Horn;

	/** Pause menu, Boolean pressed. Keys: Escape, P, Gamepad special right (Start/Menu). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> Pause;

	/** Restart the run, Boolean pressed. Keys: R, Gamepad special left (Back/View). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> Restart;

	/** Toggles Arcade/Simcade, Boolean pressed. Keys: T, Gamepad left shoulder. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Driving")
	TObjectPtr<UInputAction> ToggleControlScheme;

	/** Switches to the next car (no garage screen yet): V, D-pad up. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CycleVehicle;

	/** Skips to the next song. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> NextTrack;

	/** Menu confirm, Boolean pressed. Keys: Enter, SpaceBar, Gamepad face button bottom. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Menu")
	TObjectPtr<UInputAction> MenuConfirm;

	/** Menu back, Boolean pressed. Keys: Escape, BackSpace, Gamepad face button right. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Menu")
	TObjectPtr<UInputAction> MenuBack;

	/** Mapping context active while driving (all driving actions). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Contexts")
	TObjectPtr<UInputMappingContext> DrivingContext;

	/** Mapping context active in menus (MenuConfirm, MenuBack). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Input|Contexts")
	TObjectPtr<UInputMappingContext> MenuContext;

private:
	/** Creates actions, contexts and mappings. Called once by Create(). */
	void Build(const FLRInputTuning& InTuning);

	/** Sanitized tuning currently baked into the modifiers. */
	UPROPERTY(Transient)
	FLRInputTuning Tuning;

	/** Dead zone on Gamepad_LeftX (Steer). */
	UPROPERTY(Transient)
	TObjectPtr<UInputModifierDeadZone> StickDeadZoneModifier;

	/** Dead zone on Gamepad_RightTriggerAxis (Throttle). */
	UPROPERTY(Transient)
	TObjectPtr<UInputModifierDeadZone> ThrottleDeadZoneModifier;

	/** Dead zone on Gamepad_LeftTriggerAxis (Brake). */
	UPROPERTY(Transient)
	TObjectPtr<UInputModifierDeadZone> BrakeDeadZoneModifier;

	/** Steering response curve on Gamepad_LeftX. */
	UPROPERTY(Transient)
	TObjectPtr<UInputModifierResponseCurveExponential> SteerCurveModifier;

	/** Optional steering invert on Gamepad_LeftX (bX toggled by bInvertSteering). */
	UPROPERTY(Transient)
	TObjectPtr<UInputModifierNegate> SteerInvertModifier;
};
