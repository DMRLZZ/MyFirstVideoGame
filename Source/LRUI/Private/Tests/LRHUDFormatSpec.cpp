// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRHUDFormat.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRHUDFormatSpec, "LeftRight.UI.Format",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRHUDFormatSpec)

void FLRHUDFormatSpec::Define()
{
	Describe("Speed", [this]()
	{
		It("keeps km/h and converts to mph", [this]()
		{
			TestEqual(TEXT("100 km/h stays"), FLRHUDFormat::ConvertSpeed(100.f, ELRSpeedUnits::Kmh), 100.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("100 km/h in mph"), FLRHUDFormat::ConvertSpeed(100.f, ELRSpeedUnits::Mph), 62.137f, 0.01f);
		});

		It("rounds to the nearest integer and is never negative", [this]()
		{
			TestEqual(TEXT("237.6"), FLRHUDFormat::FormatSpeed(237.6f, ELRSpeedUnits::Kmh).ToString(), FString(TEXT("238")));
			TestEqual(TEXT("100 km/h in mph"), FLRHUDFormat::FormatSpeed(100.f, ELRSpeedUnits::Mph).ToString(), FString(TEXT("62")));
			TestEqual(TEXT("Reverse"), FLRHUDFormat::FormatSpeed(-30.f, ELRSpeedUnits::Kmh).ToString(), FString(TEXT("30")));
			TestEqual(TEXT("Zero"), FLRHUDFormat::RoundSpeedForDisplay(0.f, ELRSpeedUnits::Mph), 0);
		});

		It("labels the units", [this]()
		{
			TestEqual(TEXT("km/h"), FLRHUDFormat::GetSpeedUnitLabel(ELRSpeedUnits::Kmh).ToString(), FString(TEXT("KM/H")));
			TestEqual(TEXT("mph"), FLRHUDFormat::GetSpeedUnitLabel(ELRSpeedUnits::Mph).ToString(), FString(TEXT("MPH")));
		});
	});

	Describe("ParseSpeedUnits", [this]()
	{
		It("accepts the known spellings ignoring case and spaces", [this]()
		{
			ELRSpeedUnits Units = ELRSpeedUnits::Mph;
			TestTrue(TEXT("kmh"), FLRHUDFormat::ParseSpeedUnits(TEXT("kmh"), Units));
			TestTrue(TEXT("Parsed kmh"), Units == ELRSpeedUnits::Kmh);

			TestTrue(TEXT(" MPH "), FLRHUDFormat::ParseSpeedUnits(TEXT(" MPH "), Units));
			TestTrue(TEXT("Parsed mph"), Units == ELRSpeedUnits::Mph);

			TestTrue(TEXT("km/h"), FLRHUDFormat::ParseSpeedUnits(TEXT("km/h"), Units));
			TestTrue(TEXT("Parsed km/h"), Units == ELRSpeedUnits::Kmh);
		});

		It("rejects other values and leaves the output untouched", [this]()
		{
			ELRSpeedUnits Units = ELRSpeedUnits::Mph;
			TestFalse(TEXT("knots"), FLRHUDFormat::ParseSpeedUnits(TEXT("knots"), Units));
			TestFalse(TEXT("Empty"), FLRHUDFormat::ParseSpeedUnits(FString(), Units));
			TestTrue(TEXT("Untouched"), Units == ELRSpeedUnits::Mph);
		});
	});

	Describe("Score", [this]()
	{
		It("groups thousands", [this]()
		{
			TestEqual(TEXT("0"), FLRHUDFormat::FormatScore(0).ToString(), FString(TEXT("0")));
			TestEqual(TEXT("999"), FLRHUDFormat::FormatScore(999).ToString(), FString(TEXT("999")));
			TestEqual(TEXT("1000"), FLRHUDFormat::FormatScore(1000).ToString(), FString(TEXT("1.000")));
			TestEqual(TEXT("1234567"), FLRHUDFormat::FormatScore(1234567).ToString(), FString(TEXT("1.234.567")));
		});

		It("supports other separators and negatives", [this]()
		{
			TestEqual(TEXT("Comma"), FLRHUDFormat::GroupThousands(1234567, TEXT(",")), FString(TEXT("1,234,567")));
			TestEqual(TEXT("Empty separator"), FLRHUDFormat::GroupThousands(1234, FString()), FString(TEXT("1234")));
			TestEqual(TEXT("Negative"), FLRHUDFormat::GroupThousands(-1234, TEXT(".")), FString(TEXT("-1.234")));
			TestEqual(TEXT("Minimum"), FLRHUDFormat::GroupThousands(MIN_int64, TEXT(".")), FString(TEXT("-9.223.372.036.854.775.808")));
		});

		It("signs popup points", [this]()
		{
			TestEqual(TEXT("Positive"), FLRHUDFormat::FormatPoints(1250).ToString(), FString(TEXT("+1.250")));
			TestEqual(TEXT("Zero"), FLRHUDFormat::FormatPoints(0).ToString(), FString(TEXT("+0")));
			TestEqual(TEXT("Negative"), FLRHUDFormat::FormatPoints(-5).ToString(), FString(TEXT("-5")));
		});
	});

	Describe("Numbers", [this]()
	{
		It("formats fixed point without culture", [this]()
		{
			TestEqual(TEXT("One decimal"), FLRHUDFormat::FormatFixed(12.345, 1, TEXT(',')), FString(TEXT("12,3")));
			TestEqual(TEXT("Pads decimals"), FLRHUDFormat::FormatFixed(1.05, 2, TEXT('.')), FString(TEXT("1.05")));
			TestEqual(TEXT("No decimals"), FLRHUDFormat::FormatFixed(2.5, 0, TEXT(',')), FString(TEXT("3")));
			TestEqual(TEXT("Negative"), FLRHUDFormat::FormatFixed(-1.5, 1, TEXT(',')), FString(TEXT("-1,5")));
			TestEqual(TEXT("Rounds to zero without sign"), FLRHUDFormat::FormatFixed(-0.01, 1, TEXT(',')), FString(TEXT("0,0")));
		});

		It("formats distance in kilometers", [this]()
		{
			TestEqual(TEXT("12345 m"), FLRHUDFormat::FormatDistanceKm(12345.0).ToString(), FString(TEXT("12,3 km")));
			TestEqual(TEXT("0 m"), FLRHUDFormat::FormatDistanceKm(0.0).ToString(), FString(TEXT("0,0 km")));
			TestEqual(TEXT("Two decimals, dot"), FLRHUDFormat::FormatDistanceKm(1500.0, 2, TEXT('.')).ToString(), FString(TEXT("1.50 km")));
		});

		It("formats the gear", [this]()
		{
			TestEqual(TEXT("Reverse"), FLRHUDFormat::FormatGear(-1).ToString(), FString(TEXT("R")));
			TestEqual(TEXT("Neutral"), FLRHUDFormat::FormatGear(0).ToString(), FString(TEXT("N")));
			TestEqual(TEXT("Fourth"), FLRHUDFormat::FormatGear(4).ToString(), FString(TEXT("4")));
		});

		It("formats multipliers without trailing zeros", [this]()
		{
			TestEqual(TEXT("1"), FLRHUDFormat::FormatMultiplier(1.f).ToString(), FString(TEXT("x1")));
			TestEqual(TEXT("1.5"), FLRHUDFormat::FormatMultiplier(1.5f).ToString(), FString(TEXT("x1,5")));
			TestEqual(TEXT("1.25"), FLRHUDFormat::FormatMultiplier(1.25f).ToString(), FString(TEXT("x1,25")));
			TestEqual(TEXT("10"), FLRHUDFormat::FormatMultiplier(10.f).ToString(), FString(TEXT("x10")));
			TestEqual(TEXT("Dot"), FLRHUDFormat::FormatMultiplier(0.5f, TEXT('.')).ToString(), FString(TEXT("x0.5")));
		});

		It("formats durations as minutes and seconds", [this]()
		{
			TestEqual(TEXT("65.4 s"), FLRHUDFormat::FormatDuration(65.4f).ToString(), FString(TEXT("1:05")));
			TestEqual(TEXT("0 s"), FLRHUDFormat::FormatDuration(0.f).ToString(), FString(TEXT("0:00")));
			TestEqual(TEXT("Negative"), FLRHUDFormat::FormatDuration(-3.f).ToString(), FString(TEXT("0:00")));
		});
	});

	Describe("Labels", [this]()
	{
		It("names the near-miss tiers in Spanish", [this]()
		{
			TestEqual(TEXT("Close"), FLRHUDFormat::GetNearMissTierLabel(ELRNearMissTier::Close).ToString(), FString(TEXT("CERCA")));
			TestEqual(TEXT("Very close"), FLRHUDFormat::GetNearMissTierLabel(ELRNearMissTier::VeryClose).ToString(), FString(TEXT("MUY CERCA")));
			TestEqual(TEXT("Razor"), FLRHUDFormat::GetNearMissTierLabel(ELRNearMissTier::Razor).ToString(), FString(TEXT("¡AL MILÍMETRO!")));
			TestTrue(TEXT("None is empty"), FLRHUDFormat::GetNearMissTierLabel(ELRNearMissTier::None).IsEmpty());
		});

		It("names the tags and control schemes", [this]()
		{
			TestEqual(TEXT("Oncoming"), FLRHUDFormat::GetOncomingLabel().ToString(), FString(TEXT("CONTRAMANO")));
			TestEqual(TEXT("Thread"), FLRHUDFormat::GetThreadTheNeedleLabel().ToString(), FString(TEXT("¡ENTRE DOS!")));
			TestEqual(TEXT("Arcade"), FLRHUDFormat::GetControlSchemeLabel(ELRControlScheme::Arcade).ToString(), FString(TEXT("ARCADE")));
			TestEqual(TEXT("Simcade"), FLRHUDFormat::GetControlSchemeLabel(ELRControlScheme::Simcade).ToString(), FString(TEXT("SIMCADE")));
		});
	});

	Describe("Anim", [this]()
	{
		It("eases out and clamps", [this]()
		{
			TestEqual(TEXT("Start"), FLRHUDAnim::EaseOutCubic(0.f), 0.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("End"), FLRHUDAnim::EaseOutCubic(1.f), 1.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Mid"), FLRHUDAnim::EaseOutCubic(0.5f), 0.875f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Over"), FLRHUDAnim::EaseOutCubic(3.f), 1.f, KINDA_SMALL_NUMBER);
		});

		It("computes the fade envelope", [this]()
		{
			TestEqual(TEXT("Before"), FLRHUDAnim::FadeInOut(-0.1f, 2.f, 0.5f, 0.5f), 0.f);
			TestEqual(TEXT("Fading in"), FLRHUDAnim::FadeInOut(0.25f, 2.f, 0.5f, 0.5f), 0.5f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Hold"), FLRHUDAnim::FadeInOut(1.f, 2.f, 0.5f, 0.5f), 1.f);
			TestEqual(TEXT("Fading out"), FLRHUDAnim::FadeInOut(1.75f, 2.f, 0.5f, 0.5f), 0.5f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("After"), FLRHUDAnim::FadeInOut(2.1f, 2.f, 0.5f, 0.5f), 0.f);
			TestEqual(TEXT("No fades"), FLRHUDAnim::FadeInOut(0.f, 2.f, 0.f, 0.f), 1.f);
		});

		It("rolls the score towards its target", [this]()
		{
			const double Next = FLRHUDAnim::ApproachScore(0.0, 1000, 10.f, 0.1f);
			TestTrue(TEXT("Moves towards the target"), Next > 0.0 && Next < 1000.0);
			TestEqual(TEXT("Rate 0 snaps"), FLRHUDAnim::ApproachScore(0.0, 1000, 0.f, 0.016f), 1000.0);
			TestEqual(TEXT("Snaps when close"), FLRHUDAnim::ApproachScore(999.5, 1000, 10.f, 0.016f), 1000.0);
			TestEqual(TEXT("No time, no move"), FLRHUDAnim::ApproachScore(10.0, 1000, 10.f, 0.f), 10.0);
		});

		It("computes countdown number and phase", [this]()
		{
			TestEqual(TEXT("3.0"), FLRHUDAnim::GetCountdownNumber(3.f), 3);
			TestEqual(TEXT("2.5"), FLRHUDAnim::GetCountdownNumber(2.5f), 3);
			TestEqual(TEXT("1.0"), FLRHUDAnim::GetCountdownNumber(1.f), 1);
			TestEqual(TEXT("0.01"), FLRHUDAnim::GetCountdownNumber(0.01f), 1);
			TestEqual(TEXT("Over"), FLRHUDAnim::GetCountdownNumber(0.f), 0);
			TestEqual(TEXT("Past"), FLRHUDAnim::GetCountdownNumber(-1.f), 0);

			TestEqual(TEXT("Phase at 3"), FLRHUDAnim::GetCountdownPhase(3.f), 0.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Phase at 2.25"), FLRHUDAnim::GetCountdownPhase(2.25f), 0.75f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Phase over"), FLRHUDAnim::GetCountdownPhase(0.f), 1.f);
		});

		It("scales the UI with the canvas height", [this]()
		{
			TestEqual(TEXT("1080p"), FLRHUDAnim::ComputeUIScale(1080.f, 1080.f, 0.5f, 4.f), 1.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("4K"), FLRHUDAnim::ComputeUIScale(2160.f, 1080.f, 0.5f, 4.f), 2.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Lower clamp"), FLRHUDAnim::ComputeUIScale(100.f, 1080.f, 0.5f, 4.f), 0.5f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Upper clamp"), FLRHUDAnim::ComputeUIScale(8640.f, 1080.f, 0.5f, 4.f), 4.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Invalid"), FLRHUDAnim::ComputeUIScale(0.f, 1080.f, 0.5f, 4.f), 0.5f, KINDA_SMALL_NUMBER);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
