// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRUserSettings.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace LRUserSettingsPrivate
{
	const TCHAR* const Section = TEXT("LeftRight.UserSettings");

	/** Share of the engine motion blur that keeps the feeling of speed without smearing the traffic. */
	constexpr float MotionBlurAmount = 0.25f;
}

FLRUserSettings FLRUserSettings::Sanitized() const
{
	FLRUserSettings Safe = *this;
	Safe.MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
	Safe.EffectsVolume = FMath::Clamp(EffectsVolume, 0.f, 1.f);
	Safe.SteeringSensitivity = FMath::Clamp(SteeringSensitivity, 0.4f, 1.6f);
	Safe.GraphicsQuality = FMath::Clamp(GraphicsQuality, 0, 3);
	Safe.FrameRateLimit = FMath::Max(FrameRateLimit, 0);
	return Safe;
}

FIntPoint FLRUserSettings::ChooseWindowSize(const FIntPoint& Desktop)
{
	// Three quarters of the screen leaves room for the title bar and the taskbar. Even numbers, as video modes like them.
	const auto Fit = [](int32 DesktopPixels, int32 MinPixels)
	{
		const int32 Wanted = FMath::Max(DesktopPixels * 3 / 4, MinPixels) & ~1;
		return DesktopPixels > 0 ? FMath::Min(Wanted, DesktopPixels) : Wanted;
	};
	return FIntPoint(Fit(Desktop.X, 640), Fit(Desktop.Y, 360));
}

ULRUserSettingsSubsystem* ULRUserSettingsSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<ULRUserSettingsSubsystem>() : nullptr;
}

void ULRUserSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Load();
	ApplyGraphics();
}

void ULRUserSettingsSubsystem::SetSettings(const FLRUserSettings& NewSettings)
{
	const FLRUserSettings Safe = NewSettings.Sanitized();
	const bool bGraphicsChanged = Safe.GraphicsQuality != Settings.GraphicsQuality || Safe.FrameRateLimit != Settings.FrameRateLimit
		|| Safe.bFullscreen != Settings.bFullscreen || Safe.bMotionBlur != Settings.bMotionBlur;

	Settings = Safe;
	Save();
	if (bGraphicsChanged)
	{
		ApplyGraphics();
	}
	OnChanged.Broadcast(Settings);
}

void ULRUserSettingsSubsystem::Load()
{
	using LRUserSettingsPrivate::Section;

	FLRUserSettings Loaded;
	const FString& Ini = GGameUserSettingsIni;
	GConfig->GetFloat(Section, TEXT("MusicVolume"), Loaded.MusicVolume, Ini);
	GConfig->GetFloat(Section, TEXT("EffectsVolume"), Loaded.EffectsVolume, Ini);
	GConfig->GetBool(Section, TEXT("bMusicEnabled"), Loaded.bMusicEnabled, Ini);
	GConfig->GetBool(Section, TEXT("bShuffleMusic"), Loaded.bShuffleMusic, Ini);
	GConfig->GetFloat(Section, TEXT("SteeringSensitivity"), Loaded.SteeringSensitivity, Ini);
	GConfig->GetInt(Section, TEXT("GraphicsQuality"), Loaded.GraphicsQuality, Ini);
	GConfig->GetInt(Section, TEXT("FrameRateLimit"), Loaded.FrameRateLimit, Ini);
	GConfig->GetBool(Section, TEXT("bFullscreen"), Loaded.bFullscreen, Ini);
	GConfig->GetBool(Section, TEXT("bMotionBlur"), Loaded.bMotionBlur, Ini);

	int32 Scheme = static_cast<int32>(Loaded.ControlScheme);
	GConfig->GetInt(Section, TEXT("ControlScheme"), Scheme, Ini);
	if (Scheme >= 0 && Scheme <= static_cast<int32>(ELRControlScheme::Smooth))
	{
		Loaded.ControlScheme = static_cast<ELRControlScheme>(Scheme);
	}
	Settings = Loaded.Sanitized();
}

void ULRUserSettingsSubsystem::Save() const
{
	using LRUserSettingsPrivate::Section;

	const FString& Ini = GGameUserSettingsIni;
	GConfig->SetFloat(Section, TEXT("MusicVolume"), Settings.MusicVolume, Ini);
	GConfig->SetFloat(Section, TEXT("EffectsVolume"), Settings.EffectsVolume, Ini);
	GConfig->SetBool(Section, TEXT("bMusicEnabled"), Settings.bMusicEnabled, Ini);
	GConfig->SetBool(Section, TEXT("bShuffleMusic"), Settings.bShuffleMusic, Ini);
	GConfig->SetFloat(Section, TEXT("SteeringSensitivity"), Settings.SteeringSensitivity, Ini);
	GConfig->SetInt(Section, TEXT("GraphicsQuality"), Settings.GraphicsQuality, Ini);
	GConfig->SetInt(Section, TEXT("FrameRateLimit"), Settings.FrameRateLimit, Ini);
	GConfig->SetBool(Section, TEXT("bFullscreen"), Settings.bFullscreen, Ini);
	GConfig->SetBool(Section, TEXT("bMotionBlur"), Settings.bMotionBlur, Ini);
	GConfig->SetInt(Section, TEXT("ControlScheme"), static_cast<int32>(Settings.ControlScheme), Ini);
	GConfig->Flush(false, Ini);
}

void ULRUserSettingsSubsystem::ApplyGraphics() const
{
	// Automated runs (tests, commandlets) have no window to configure.
	if (GEngine == nullptr || IsRunningCommandlet() || !FApp::CanEverRender())
	{
		return;
	}

	UGameUserSettings* Engine = GEngine->GetGameUserSettings();
	if (Engine == nullptr)
	{
		return;
	}

	Engine->SetOverallScalabilityLevel(Settings.GraphicsQuality);
	// TSR at epic quality costs more than it gives (ADR-014).
	Engine->SetAntiAliasingQuality(FMath::Min(Settings.GraphicsQuality, 2));
	Engine->SetFrameRateLimit(static_cast<float>(Settings.FrameRateLimit));

	// A window asked for on the command line (development runs) wins over the setting.
	const bool bWindowForced = FParse::Param(FCommandLine::Get(), TEXT("windowed")) || FParse::Param(FCommandLine::Get(), TEXT("LRDevDrive"));
	if (!bWindowForced)
	{
		Engine->SetFullscreenMode(Settings.bFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		if (Settings.bFullscreen)
		{
			Engine->SetScreenResolution(Engine->GetDesktopResolution());
		}
	}
	Engine->ApplySettings(false);

	if (IConsoleVariable* Blur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlur.Amount")))
	{
		Blur->Set(Settings.bMotionBlur ? LRUserSettingsPrivate::MotionBlurAmount : 0.f, ECVF_SetByGameSetting);
	}
}
