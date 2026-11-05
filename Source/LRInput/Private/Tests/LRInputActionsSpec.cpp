// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "LRDataLoader.h"
#include "LRInputActions.h"
#include "LRInputTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

BEGIN_DEFINE_SPEC(FLRInputActionsSpec, "LeftRight.Input.Actions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

	TStrongObjectPtr<ULRInputActions> Actions;

	/** Returns the mapping of Action to Key inside Context, or null. */
	static const FEnhancedActionKeyMapping* FindMapping(const UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		if (!Context)
		{
			return nullptr;
		}
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action == Action && Mapping.Key == Key)
			{
				return &Mapping;
			}
		}
		return nullptr;
	}

	/** True when the mapping carries a modifier of class TModifier. */
	template <typename TModifier>
	static const TModifier* FindModifier(const FEnhancedActionKeyMapping* Mapping)
	{
		if (Mapping)
		{
			for (const TObjectPtr<UInputModifier>& Modifier : Mapping->Modifiers)
			{
				if (const TModifier* Typed = Cast<TModifier>(Modifier))
				{
					return Typed;
				}
			}
		}
		return nullptr;
	}

	/** True when the mapping carries a Pressed trigger. */
	static bool HasPressedTrigger(const FEnhancedActionKeyMapping* Mapping)
	{
		if (Mapping)
		{
			for (const TObjectPtr<UInputTrigger>& Trigger : Mapping->Triggers)
			{
				if (Cast<UInputTriggerPressed>(Trigger))
				{
					return true;
				}
			}
		}
		return false;
	}

	/** Checks that Action is mapped to every key in Keys inside Context. */
	void TestMappedKeys(const TCHAR* What, const UInputMappingContext* Context, const UInputAction* Action, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Key : Keys)
		{
			TestNotNull(*FString::Printf(TEXT("%s mapped to %s"), What, *Key.ToString()), FindMapping(Context, Action, Key));
		}
	}

END_DEFINE_SPEC(FLRInputActionsSpec)

void FLRInputActionsSpec::Define()
{
	BeforeEach([this]()
	{
		FLRInputTuning Tuning;
		Tuning.StickDeadZone = 0.2f;
		Tuning.TriggerDeadZone = 0.1f;
		Tuning.SteeringExponent = 2.f;
		Actions.Reset(ULRInputActions::Create(GetTransientPackage(), Tuning));
	});

	AfterEach([this]()
	{
		Actions.Reset();
	});

	Describe("Create", [this]()
	{
		It("builds every action with the right value type", [this]()
		{
			ULRInputActions* A = Actions.Get();
			if (!TestNotNull(TEXT("Actions"), A))
			{
				return;
			}
			for (UInputAction* Axis : { A->Throttle.Get(), A->Brake.Get(), A->Steer.Get() })
			{
				if (TestNotNull(TEXT("Axis action"), Axis))
				{
					TestTrue(*FString::Printf(TEXT("%s is Axis1D"), *Axis->GetName()), Axis->ValueType == EInputActionValueType::Axis1D);
				}
			}
			for (UInputAction* Button : { A->Handbrake.Get(), A->LaneLeft.Get(), A->LaneRight.Get(), A->CameraCycle.Get(), A->LookBack.Get(),
				A->Horn.Get(), A->Pause.Get(), A->Restart.Get(), A->ToggleControlScheme.Get(), A->MenuConfirm.Get(), A->MenuBack.Get() })
			{
				if (TestNotNull(TEXT("Boolean action"), Button))
				{
					TestTrue(*FString::Printf(TEXT("%s is Boolean"), *Button->GetName()), Button->ValueType == EInputActionValueType::Boolean);
				}
			}
			TArray<UInputAction*> All;
			A->GetAllActions(All);
			TestEqual(TEXT("Action count"), All.Num(), 14);
			TestNotNull(TEXT("DrivingContext"), A->DrivingContext.Get());
			TestNotNull(TEXT("MenuContext"), A->MenuContext.Get());
		});

		It("maps every action to both keyboard and gamepad", [this]()
		{
			TArray<UInputAction*> All;
			Actions->GetAllActions(All);
			for (const UInputAction* Action : All)
			{
				const FKey KeyboardKey = Actions->GetPromptKey(Action, ELRInputDevice::KeyboardMouse);
				const FKey GamepadKey = Actions->GetPromptKey(Action, ELRInputDevice::Gamepad);
				TestTrue(*FString::Printf(TEXT("%s has a keyboard key"), *Action->GetName()), KeyboardKey.IsValid() && !KeyboardKey.IsGamepadKey());
				TestTrue(*FString::Printf(TEXT("%s has a gamepad key"), *Action->GetName()), GamepadKey.IsValid() && GamepadKey.IsGamepadKey());
			}
		});

		It("maps the driving keys of the contract", [this]()
		{
			const ULRInputActions* A = Actions.Get();
			const UInputMappingContext* Drive = A->DrivingContext;
			TestMappedKeys(TEXT("Throttle"), Drive, A->Throttle, { EKeys::W, EKeys::Up, EKeys::Gamepad_RightTriggerAxis });
			TestMappedKeys(TEXT("Brake"), Drive, A->Brake, { EKeys::S, EKeys::Down, EKeys::Gamepad_LeftTriggerAxis });
			TestMappedKeys(TEXT("Steer"), Drive, A->Steer, { EKeys::D, EKeys::Right, EKeys::A, EKeys::Left, EKeys::Gamepad_LeftX });
			TestMappedKeys(TEXT("Handbrake"), Drive, A->Handbrake, { EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Right });
			TestMappedKeys(TEXT("LaneLeft"), Drive, A->LaneLeft, { EKeys::Q, EKeys::Gamepad_DPad_Left });
			TestMappedKeys(TEXT("LaneRight"), Drive, A->LaneRight, { EKeys::E, EKeys::Gamepad_DPad_Right });
			TestMappedKeys(TEXT("CameraCycle"), Drive, A->CameraCycle, { EKeys::C, EKeys::Gamepad_FaceButton_Top });
			TestMappedKeys(TEXT("LookBack"), Drive, A->LookBack, { EKeys::B, EKeys::Gamepad_RightShoulder });
			TestMappedKeys(TEXT("Horn"), Drive, A->Horn, { EKeys::H, EKeys::Gamepad_LeftThumbstick });
			TestMappedKeys(TEXT("Pause"), Drive, A->Pause, { EKeys::Escape, EKeys::P, EKeys::Gamepad_Special_Right });
			TestMappedKeys(TEXT("Restart"), Drive, A->Restart, { EKeys::R, EKeys::Gamepad_Special_Left });
			TestMappedKeys(TEXT("ToggleControlScheme"), Drive, A->ToggleControlScheme, { EKeys::T, EKeys::Gamepad_LeftShoulder });
		});

		It("maps the menu keys only in the menu context", [this]()
		{
			const ULRInputActions* A = Actions.Get();
			TestMappedKeys(TEXT("MenuConfirm"), A->MenuContext, A->MenuConfirm, { EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom });
			TestMappedKeys(TEXT("MenuBack"), A->MenuContext, A->MenuBack, { EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right });
			TestNull(TEXT("MenuConfirm not in driving"), FindMapping(A->DrivingContext, A->MenuConfirm, EKeys::Enter));
			TestNull(TEXT("Throttle not in menu"), FindMapping(A->MenuContext, A->Throttle, EKeys::W));
		});
	});

	Describe("Modifiers", [this]()
	{
		It("negates A and Left but not D and Right", [this]()
		{
			const ULRInputActions* A = Actions.Get();
			TestNotNull(TEXT("A negated"), FindModifier<UInputModifierNegate>(FindMapping(A->DrivingContext, A->Steer, EKeys::A)));
			TestNotNull(TEXT("Left negated"), FindModifier<UInputModifierNegate>(FindMapping(A->DrivingContext, A->Steer, EKeys::Left)));
			TestNull(TEXT("D not negated"), FindModifier<UInputModifierNegate>(FindMapping(A->DrivingContext, A->Steer, EKeys::D)));
			TestNull(TEXT("Right not negated"), FindModifier<UInputModifierNegate>(FindMapping(A->DrivingContext, A->Steer, EKeys::Right)));
			TestTrue(TEXT("Steer is cumulative"), A->Steer->AccumulationBehavior == EInputActionAccumulationBehavior::Cumulative);
		});

		It("applies the tuning dead zones and steering curve to the gamepad axes", [this]()
		{
			const ULRInputActions* A = Actions.Get();
			const FEnhancedActionKeyMapping* Stick = FindMapping(A->DrivingContext, A->Steer, EKeys::Gamepad_LeftX);
			const UInputModifierDeadZone* StickDeadZone = FindModifier<UInputModifierDeadZone>(Stick);
			if (TestNotNull(TEXT("Stick dead zone"), StickDeadZone))
			{
				TestEqual(TEXT("Stick dead zone value"), StickDeadZone->LowerThreshold, 0.2f);
			}
			const UInputModifierResponseCurveExponential* Curve = FindModifier<UInputModifierResponseCurveExponential>(Stick);
			if (TestNotNull(TEXT("Stick curve"), Curve))
			{
				TestEqual(TEXT("Curve exponent"), Curve->CurveExponent.X, 2.0);
			}
			const UInputModifierNegate* Invert = FindModifier<UInputModifierNegate>(Stick);
			if (TestNotNull(TEXT("Stick invert modifier"), Invert))
			{
				TestFalse(TEXT("Not inverted by default"), Invert->bX);
			}
			for (const FKey& TriggerKey : { EKeys::Gamepad_RightTriggerAxis, EKeys::Gamepad_LeftTriggerAxis })
			{
				const UInputAction* Action = TriggerKey == EKeys::Gamepad_RightTriggerAxis ? A->Throttle.Get() : A->Brake.Get();
				const UInputModifierDeadZone* DeadZone = FindModifier<UInputModifierDeadZone>(FindMapping(A->DrivingContext, Action, TriggerKey));
				if (TestNotNull(*FString::Printf(TEXT("%s dead zone"), *TriggerKey.ToString()), DeadZone))
				{
					TestEqual(TEXT("Trigger dead zone value"), DeadZone->LowerThreshold, 0.1f);
				}
			}
		});

		It("uses Pressed triggers on one-shot actions and none on held actions", [this]()
		{
			const ULRInputActions* A = Actions.Get();
			const UInputMappingContext* Drive = A->DrivingContext;
			TestTrue(TEXT("LaneLeft Q"), HasPressedTrigger(FindMapping(Drive, A->LaneLeft, EKeys::Q)));
			TestTrue(TEXT("LaneLeft DPad"), HasPressedTrigger(FindMapping(Drive, A->LaneLeft, EKeys::Gamepad_DPad_Left)));
			TestTrue(TEXT("LaneRight E"), HasPressedTrigger(FindMapping(Drive, A->LaneRight, EKeys::E)));
			TestTrue(TEXT("LaneRight DPad"), HasPressedTrigger(FindMapping(Drive, A->LaneRight, EKeys::Gamepad_DPad_Right)));
			TestTrue(TEXT("Pause"), HasPressedTrigger(FindMapping(Drive, A->Pause, EKeys::Gamepad_Special_Right)));
			TestTrue(TEXT("MenuConfirm"), HasPressedTrigger(FindMapping(A->MenuContext, A->MenuConfirm, EKeys::Gamepad_FaceButton_Bottom)));
			TestFalse(TEXT("Handbrake held"), HasPressedTrigger(FindMapping(Drive, A->Handbrake, EKeys::SpaceBar)));
			TestFalse(TEXT("LookBack held"), HasPressedTrigger(FindMapping(Drive, A->LookBack, EKeys::Gamepad_RightShoulder)));
		});

		It("ApplyTuning updates the baked modifiers in place", [this]()
		{
			FLRInputTuning Tuning;
			Tuning.StickDeadZone = 0.3f;
			Tuning.SteeringExponent = 1.f;
			Tuning.bInvertSteering = true;
			Actions->ApplyTuning(Tuning);
			const FEnhancedActionKeyMapping* Stick = FindMapping(Actions->DrivingContext, Actions->Steer, EKeys::Gamepad_LeftX);
			TestEqual(TEXT("Dead zone"), FindModifier<UInputModifierDeadZone>(Stick)->LowerThreshold, 0.3f);
			TestEqual(TEXT("Exponent"), FindModifier<UInputModifierResponseCurveExponential>(Stick)->CurveExponent.X, 1.0);
			TestTrue(TEXT("Inverted"), FindModifier<UInputModifierNegate>(Stick)->bX);
			TestTrue(TEXT("Stored tuning"), Actions->GetTuning().bInvertSteering);
		});
	});

	Describe("Tuning data", [this]()
	{
		It("parses an InputTuning envelope", [this]()
		{
			const FString Json = TEXT("{\"type\":\"InputTuning\",\"id\":\"test\",\"version\":1,\"data\":{\"StickDeadZone\":0.25,\"bInvertSteering\":true}}");
			FLRInputTuning Tuning;
			FString Error;
			TestTrue(TEXT("Parsed"), FLRDataLoader::Parse(Json, Tuning, &Error));
			TestEqual(TEXT("StickDeadZone"), Tuning.StickDeadZone, 0.25f);
			TestTrue(TEXT("bInvertSteering"), Tuning.bInvertSteering);
			TestEqual(TEXT("Default kept"), Tuning.LaneChangeThreshold, 0.55f);
		});

		It("loads Data/Input/Input.json", [this]()
		{
			FLRInputTuning Tuning;
			Tuning.StickDeadZone = 0.9f;
			FString Error;
			const bool bLoaded = ULRInputActions::LoadTuning(Tuning, &Error);
			if (TestTrue(*FString::Printf(TEXT("Loaded Input/Input.json (error: '%s')"), *Error), bLoaded))
			{
				TestEqual(TEXT("StickDeadZone from file"), Tuning.StickDeadZone, 0.15f);
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
