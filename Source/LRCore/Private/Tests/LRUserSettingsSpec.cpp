// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRUserSettings.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRUserSettingsSpec, "LeftRight.Core.UserSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRUserSettingsSpec)

void FLRUserSettingsSpec::Define()
{
	It("starts with the smooth controls and music on", [this]()
	{
		const FLRUserSettings Defaults;
		TestTrue(TEXT("Smooth"), Defaults.ControlScheme == ELRControlScheme::Smooth);
		TestTrue(TEXT("Music"), Defaults.bMusicEnabled);
	});

	It("picks a window smaller than the desktop", [this]()
	{
		const FIntPoint FullHd = FLRUserSettings::ChooseWindowSize(FIntPoint(1920, 1080));
		TestEqual(TEXT("Width on a 1080p desktop"), FullHd.X, 1440);
		TestEqual(TEXT("Height on a 1080p desktop"), FullHd.Y, 810);

		const FIntPoint Odd = FLRUserSettings::ChooseWindowSize(FIntPoint(1366, 768));
		TestTrue(TEXT("Even width"), Odd.X % 2 == 0);
		TestTrue(TEXT("Narrower than the desktop"), Odd.X < 1366 && Odd.Y < 768);

		const FIntPoint Tiny = FLRUserSettings::ChooseWindowSize(FIntPoint(600, 400));
		TestTrue(TEXT("Never larger than a small desktop"), Tiny.X <= 600 && Tiny.Y <= 400);
	});

	It("clamps every value to its valid range", [this]()
	{
		FLRUserSettings Wild;
		Wild.MusicVolume = 5.f;
		Wild.EffectsVolume = -1.f;
		Wild.SteeringSensitivity = 99.f;
		Wild.GraphicsQuality = 12;
		Wild.FrameRateLimit = -30;

		const FLRUserSettings Safe = Wild.Sanitized();
		TestEqual(TEXT("Music"), Safe.MusicVolume, 1.f);
		TestEqual(TEXT("Effects"), Safe.EffectsVolume, 0.f);
		TestEqual(TEXT("Sensitivity"), Safe.SteeringSensitivity, 1.6f);
		TestEqual(TEXT("Quality"), Safe.GraphicsQuality, 3);
		TestEqual(TEXT("Frame limit"), Safe.FrameRateLimit, 0);
	});

	It("leaves valid values untouched", [this]()
	{
		FLRUserSettings Valid;
		Valid.MusicVolume = 0.3f;
		Valid.FrameRateLimit = 120;
		const FLRUserSettings Safe = Valid.Sanitized();
		TestEqual(TEXT("Music"), Safe.MusicVolume, 0.3f);
		TestEqual(TEXT("Frame limit"), Safe.FrameRateLimit, 120);
	});
}

#endif
