// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRInputActions.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "LRDataLoader.h"
#include "LRInput.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "LRInputActions"

const TCHAR* ULRInputActions::TuningDataPath = TEXT("Input/Input.json");

namespace LRInputActionsPrivate
{
	/** Creates one transient input action sub-object. */
	static UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType, const FText& Description,
		EInputActionAccumulationBehavior Accumulation = EInputActionAccumulationBehavior::TakeHighestAbsoluteValue)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, FName(Name), RF_Transient);
		Action->ValueType = ValueType;
		Action->AccumulationBehavior = Accumulation;
		Action->ActionDescription = Description;
		return Action;
	}

	/** Maps a key with no modifiers or triggers (hold semantics for Boolean actions). */
	static void MapPlain(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		Context->MapKey(Action, Key);
	}

	/** Maps a key with a Pressed trigger (fires once per press). */
	static void MapPressed(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		Mapping.Triggers.Add(NewObject<UInputTriggerPressed>(Context, NAME_None, RF_Transient));
	}

	/** Maps a key with a Negate modifier (keyboard "minus" direction of an axis). */
	static void MapNegated(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context, NAME_None, RF_Transient));
	}

	/** Creates an axial dead zone modifier owned by Context. */
	static UInputModifierDeadZone* MakeDeadZone(UInputMappingContext* Context, float LowerThreshold)
	{
		UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Context, NAME_None, RF_Transient);
		DeadZone->LowerThreshold = LowerThreshold;
		DeadZone->UpperThreshold = 1.f;
		DeadZone->Type = EDeadZoneType::Axial;
		return DeadZone;
	}

	/** Maps an analog key with an axial dead zone; returns the modifier so it can be retuned later. */
	static UInputModifierDeadZone* MapWithDeadZone(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key, float LowerThreshold)
	{
		UInputModifierDeadZone* DeadZone = MakeDeadZone(Context, LowerThreshold);
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		Mapping.Modifiers.Add(DeadZone);
		return DeadZone;
	}
}

ULRInputActions* ULRInputActions::Create(UObject* Outer, const FLRInputTuning& InTuning)
{
	UObject* const SafeOuter = Outer ? Outer : GetTransientPackage();
	ULRInputActions* Actions = NewObject<ULRInputActions>(SafeOuter, NAME_None, RF_Transient);
	Actions->Build(InTuning);
	return Actions;
}

bool ULRInputActions::LoadTuning(FLRInputTuning& InOutTuning, FString* OutError)
{
	FLRInputTuning Loaded = InOutTuning;
	FString Error;
	if (!FLRDataLoader::Load(TuningDataPath, Loaded, &Error))
	{
		UE_LOG(LogLRInput, Warning, TEXT("Could not load %s, keeping current input tuning: %s"), TuningDataPath, *Error);
		if (OutError)
		{
			*OutError = Error;
		}
		return false;
	}
	InOutTuning = Loaded.Sanitized();
	return true;
}

void ULRInputActions::Build(const FLRInputTuning& InTuning)
{
	using namespace LRInputActionsPrivate;

	Tuning = InTuning.Sanitized();

	// Actions.
	Throttle = MakeAction(this, TEXT("IA_Throttle"), EInputActionValueType::Axis1D, LOCTEXT("Throttle", "Throttle"));
	Brake = MakeAction(this, TEXT("IA_Brake"), EInputActionValueType::Axis1D, LOCTEXT("Brake", "Brake / Reverse"));
	Steer = MakeAction(this, TEXT("IA_Steer"), EInputActionValueType::Axis1D, LOCTEXT("Steer", "Steer"),
		EInputActionAccumulationBehavior::Cumulative);
	Handbrake = MakeAction(this, TEXT("IA_Handbrake"), EInputActionValueType::Boolean, LOCTEXT("Handbrake", "Handbrake"));
	LaneLeft = MakeAction(this, TEXT("IA_LaneLeft"), EInputActionValueType::Boolean, LOCTEXT("LaneLeft", "Lane Left"));
	LaneRight = MakeAction(this, TEXT("IA_LaneRight"), EInputActionValueType::Boolean, LOCTEXT("LaneRight", "Lane Right"));
	CameraCycle = MakeAction(this, TEXT("IA_CameraCycle"), EInputActionValueType::Boolean, LOCTEXT("CameraCycle", "Change Camera"));
	LookBack = MakeAction(this, TEXT("IA_LookBack"), EInputActionValueType::Boolean, LOCTEXT("LookBack", "Look Back"));
	Horn = MakeAction(this, TEXT("IA_Horn"), EInputActionValueType::Boolean, LOCTEXT("Horn", "Horn"));
	Pause = MakeAction(this, TEXT("IA_Pause"), EInputActionValueType::Boolean, LOCTEXT("Pause", "Pause"));
	Restart = MakeAction(this, TEXT("IA_Restart"), EInputActionValueType::Boolean, LOCTEXT("Restart", "Restart"));
	ToggleControlScheme = MakeAction(this, TEXT("IA_ToggleControlScheme"), EInputActionValueType::Boolean,
		LOCTEXT("ToggleControlScheme", "Arcade / Simcade"));
	CycleVehicle = MakeAction(this, TEXT("IA_CycleVehicle"), EInputActionValueType::Boolean,
		LOCTEXT("CycleVehicle", "Next car"));
	NextTrack = MakeAction(this, TEXT("IA_NextTrack"), EInputActionValueType::Boolean, LOCTEXT("NextTrack", "Next song"));
	MenuConfirm = MakeAction(this, TEXT("IA_MenuConfirm"), EInputActionValueType::Boolean, LOCTEXT("MenuConfirm", "Confirm"));
	MenuBack = MakeAction(this, TEXT("IA_MenuBack"), EInputActionValueType::Boolean, LOCTEXT("MenuBack", "Back"));

	// Driving context.
	DrivingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Driving"), RF_Transient);
	UInputMappingContext* const Drive = DrivingContext;

	MapPlain(Drive, Throttle, EKeys::W);
	MapPlain(Drive, Throttle, EKeys::Up);
	ThrottleDeadZoneModifier = MapWithDeadZone(Drive, Throttle, EKeys::Gamepad_RightTriggerAxis, Tuning.TriggerDeadZone);

	MapPlain(Drive, Brake, EKeys::S);
	MapPlain(Drive, Brake, EKeys::Down);
	BrakeDeadZoneModifier = MapWithDeadZone(Drive, Brake, EKeys::Gamepad_LeftTriggerAxis, Tuning.TriggerDeadZone);

	MapPlain(Drive, Steer, EKeys::D);
	MapPlain(Drive, Steer, EKeys::Right);
	MapNegated(Drive, Steer, EKeys::A);
	MapNegated(Drive, Steer, EKeys::Left);
	{
		// Gamepad stick chain: dead zone -> response curve -> optional invert (same order as FLRInputCurves::ShapeStickSteering).
		StickDeadZoneModifier = MakeDeadZone(Drive, Tuning.StickDeadZone);
		SteerCurveModifier = NewObject<UInputModifierResponseCurveExponential>(Drive, NAME_None, RF_Transient);
		SteerInvertModifier = NewObject<UInputModifierNegate>(Drive, NAME_None, RF_Transient);
		SteerInvertModifier->bY = false;
		SteerInvertModifier->bZ = false;

		FEnhancedActionKeyMapping& StickMapping = Drive->MapKey(Steer, EKeys::Gamepad_LeftX);
		StickMapping.Modifiers.Add(StickDeadZoneModifier);
		StickMapping.Modifiers.Add(SteerCurveModifier);
		StickMapping.Modifiers.Add(SteerInvertModifier);
	}

	MapPlain(Drive, Handbrake, EKeys::SpaceBar);
	MapPlain(Drive, Handbrake, EKeys::Gamepad_FaceButton_Right);

	MapPressed(Drive, LaneLeft, EKeys::Q);
	MapPressed(Drive, LaneLeft, EKeys::Gamepad_DPad_Left);
	MapPressed(Drive, LaneRight, EKeys::E);
	MapPressed(Drive, LaneRight, EKeys::Gamepad_DPad_Right);

	MapPressed(Drive, CameraCycle, EKeys::C);
	MapPressed(Drive, CameraCycle, EKeys::Gamepad_FaceButton_Top);

	MapPlain(Drive, LookBack, EKeys::B);
	MapPlain(Drive, LookBack, EKeys::Gamepad_RightShoulder);

	MapPlain(Drive, Horn, EKeys::H);
	MapPlain(Drive, Horn, EKeys::Gamepad_LeftThumbstick);

	MapPressed(Drive, Pause, EKeys::Escape);
	MapPressed(Drive, Pause, EKeys::P);
	MapPressed(Drive, Pause, EKeys::Gamepad_Special_Right);

	MapPressed(Drive, Restart, EKeys::R);
	MapPressed(Drive, Restart, EKeys::Gamepad_Special_Left);

	MapPressed(Drive, ToggleControlScheme, EKeys::T);
	MapPressed(Drive, ToggleControlScheme, EKeys::Gamepad_LeftShoulder);
	MapPressed(Drive, CycleVehicle, EKeys::V);
	MapPressed(Drive, CycleVehicle, EKeys::Gamepad_DPad_Up);
	MapPressed(Drive, NextTrack, EKeys::M);
	MapPressed(Drive, NextTrack, EKeys::Gamepad_DPad_Down);

	// Menu context.
	MenuContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Menu"), RF_Transient);
	UInputMappingContext* const Menu = MenuContext;

	MapPressed(Menu, MenuConfirm, EKeys::Enter);
	MapPressed(Menu, MenuConfirm, EKeys::SpaceBar);
	MapPressed(Menu, MenuConfirm, EKeys::Gamepad_FaceButton_Bottom);

	MapPressed(Menu, MenuBack, EKeys::Escape);
	MapPressed(Menu, MenuBack, EKeys::BackSpace);
	MapPressed(Menu, MenuBack, EKeys::Gamepad_FaceButton_Right);

	ApplyTuning(Tuning);
}

void ULRInputActions::ApplyTuning(const FLRInputTuning& InTuning)
{
	Tuning = InTuning.Sanitized();

	if (StickDeadZoneModifier)
	{
		StickDeadZoneModifier->LowerThreshold = Tuning.StickDeadZone;
	}
	if (ThrottleDeadZoneModifier)
	{
		ThrottleDeadZoneModifier->LowerThreshold = Tuning.TriggerDeadZone;
	}
	if (BrakeDeadZoneModifier)
	{
		BrakeDeadZoneModifier->LowerThreshold = Tuning.TriggerDeadZone;
	}
	if (SteerCurveModifier)
	{
		SteerCurveModifier->CurveExponent = FVector(static_cast<double>(Tuning.SteeringExponent), 1.0, 1.0);
	}
	if (SteerInvertModifier)
	{
		SteerInvertModifier->bX = Tuning.bInvertSteering;
	}
}

void ULRInputActions::GetAllActions(TArray<UInputAction*>& OutActions) const
{
	OutActions.Reserve(OutActions.Num() + 14);
	for (const TObjectPtr<UInputAction>& Action : { Throttle, Brake, Steer, Handbrake, LaneLeft, LaneRight, CameraCycle,
		LookBack, Horn, Pause, Restart, ToggleControlScheme, MenuConfirm, MenuBack })
	{
		if (Action)
		{
			OutActions.Add(Action.Get());
		}
	}
}

FKey ULRInputActions::GetPromptKey(const UInputAction* Action, ELRInputDevice Device) const
{
	if (!Action)
	{
		return EKeys::Invalid;
	}
	const bool bWantGamepad = Device == ELRInputDevice::Gamepad;
	for (const UInputMappingContext* Context : { DrivingContext.Get(), MenuContext.Get() })
	{
		if (!Context)
		{
			continue;
		}
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action == Action && Mapping.Key.IsGamepadKey() == bWantGamepad)
			{
				return Mapping.Key;
			}
		}
	}
	return EKeys::Invalid;
}

#undef LOCTEXT_NAMESPACE
