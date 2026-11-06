// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRDataLoader.h"
#include "LRHUD.h"
#include "LRHUDTypes.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRHUDDataSpec, "LeftRight.UI.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRHUDDataSpec)

void FLRHUDDataSpec::Define()
{
	Describe("Tuning", [this]()
	{
		It("loads Data/UI/HUD.json", [this]()
		{
			FLRHUDTuning Tuning;
			FString Error;
			const bool bLoaded = FLRDataLoader::Load(FLRHUDTuning::DataPath, Tuning, &Error);
			TestTrue(*FString::Printf(TEXT("Load: %s"), *Error), bLoaded);
			TestEqual(TEXT("Controls hint"), Tuning.ControlsHintSeconds, 8.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Typeface"), Tuning.BoldTypeface, FName(TEXT("Bold")));
			TestEqual(TEXT("Separator"), Tuning.ThousandsSeparator, FString(TEXT(".")));
			TestEqual(TEXT("Panel alpha"), Tuning.PanelColor.A, 0.5f, KINDA_SMALL_NUMBER);
		});

		It("keeps defaults for missing fields", [this]()
		{
			const FString Json = TEXT("{\"type\":\"HUDTuning\",\"id\":\"t\",\"version\":1,\"data\":{\"MaxPopups\":7}}");
			FLRHUDTuning Tuning;
			TestTrue(TEXT("Parse"), FLRDataLoader::Parse(Json, Tuning));
			TestEqual(TEXT("Read"), Tuning.MaxPopups, 7);
			TestEqual(TEXT("Default"), Tuning.ReferenceHeight, 1080.f);
		});

		It("clamps bad data when sanitized", [this]()
		{
			FLRHUDTuning Tuning;
			Tuning.ReferenceHeight = 0.f;
			Tuning.MinUIScale = 3.f;
			Tuning.MaxUIScale = 0.1f;
			Tuning.MaxPopups = 0;
			Tuning.PopupLifetimeSeconds = 1.f;
			Tuning.PopupFadeOutSeconds = 5.f;
			Tuning.RedlineFraction = 3.f;
			Tuning.DecimalSeparator.Empty();

			const FLRHUDTuning Clean = Tuning.Sanitized();
			TestEqual(TEXT("Reference height"), Clean.ReferenceHeight, 240.f);
			TestEqual(TEXT("Min scale"), Clean.MinUIScale, 2.f);
			TestTrue(TEXT("Max scale not below min"), Clean.MaxUIScale >= Clean.MinUIScale);
			TestEqual(TEXT("Popups"), Clean.MaxPopups, 1);
			TestEqual(TEXT("Fade fits in the lifetime"), Clean.PopupFadeOutSeconds, 1.f);
			TestEqual(TEXT("Redline"), Clean.RedlineFraction, 1.f);
			TestTrue(TEXT("Decimal separator"), Clean.GetDecimalSeparatorChar() == TEXT(','));
		});
	});

	Describe("GameRules", [this]()
	{
		It("reads CountdownSeconds and ignores the other rules", [this]()
		{
			const FString Json = TEXT("{\"type\":\"GameRules\",\"id\":\"default\",\"version\":1,\"data\":{\"CountdownSeconds\":5,\"SomethingElse\":true}}");
			FLRHUDGameRules Rules;
			TestTrue(TEXT("Parse"), FLRDataLoader::Parse(Json, Rules));
			TestEqual(TEXT("Countdown"), Rules.CountdownSeconds, 5.f);
		});

		It("defaults to three seconds", [this]()
		{
			TestEqual(TEXT("Default"), FLRHUDGameRules().CountdownSeconds, 3.f);
		});
	});

	Describe("Labels", [this]()
	{
		It("builds every text the HUD draws", [this]()
		{
			FLRHUDLabels Labels;
			Labels.Build();

			TestEqual(TEXT("Go"), Labels.CountdownGo.ToString(), FString(TEXT("¡YA!")));
			TestEqual(TEXT("Crash"), Labels.Crashed.ToString(), FString(TEXT("¡CHOQUE!")));
			TestEqual(TEXT("Razor label"), Labels.TierLabels[static_cast<int32>(ELRNearMissTier::Razor)].ToString(), FString(TEXT("¡AL MILÍMETRO!")));
			TestTrue(TEXT("Keyboard restart names R"), Labels.RestartKeyboard.ToString().Contains(TEXT("R")));
			TestTrue(TEXT("Gamepad restart names the button"), Labels.RestartGamepad.ToString().Contains(TEXT("View / Share")));

			TestTrue(TEXT("Arcade hints"), Labels.ArcadeHints.Num() > 0);
			TestEqual(TEXT("Same rows in both schemes"), Labels.SimcadeHints.Num(), Labels.ArcadeHints.Num());
			for (const FLRHUDLabels::FHintRow& Row : Labels.ArcadeHints)
			{
				TestFalse(TEXT("Action"), Row.Action.IsEmpty());
				TestFalse(TEXT("Keyboard"), Row.Keyboard.IsEmpty());
				TestFalse(TEXT("Gamepad"), Row.Gamepad.IsEmpty());
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
