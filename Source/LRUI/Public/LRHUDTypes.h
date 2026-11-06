// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LRHUDTypes.generated.h"

/**
 * Look and timing of the Canvas HUD, loaded from Data/UI/HUD.json (envelope type "HUDTuning").
 *
 * Sizes and distances are expressed at the 1080p reference height and multiplied by the UI scale
 * (canvas height / ReferenceHeight), so the HUD reads the same from 1080p to 4K.
 * Missing JSON fields keep the C++ defaults. Owned by value by ALRHUD.
 */
USTRUCT(BlueprintType)
struct LRUI_API FLRHUDTuning
{
	GENERATED_BODY()

	// --- Layout ----------------------------------------------------------------------------------

	/** Canvas height (pixels) where the UI scale is 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "240.0", ClampMax = "4320.0"))
	float ReferenceHeight = 1080.f;

	/** Lower clamp of the UI scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MinUIScale = 0.5f;

	/** Upper clamp of the UI scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float MaxUIScale = 4.f;

	/** Safe margin around the screen edges, fraction of the canvas height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float SafeMarginFraction = 0.04f;

	// --- Fonts (points at the reference height) ---------------------------------------------------

	/** Typeface of the engine font used for numbers and banners (falls back to the default face if missing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FName BoldTypeface = FName(TEXT("Bold"));

	/** Typeface used for labels and body text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FName RegularTypeface = FName(TEXT("Regular"));

	/** Speedometer number. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "6.0", ClampMax = "300.0"))
	float SpeedFontSize = 64.f;

	/** Score at the top center. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "6.0", ClampMax = "300.0"))
	float ScoreFontSize = 40.f;

	/** Countdown numbers and full-screen banners. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "6.0", ClampMax = "400.0"))
	float BannerFontSize = 110.f;

	/** Panel titles, gear and near-miss labels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "6.0", ClampMax = "200.0"))
	float LargeFontSize = 30.f;

	/** Values and secondary labels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "6.0", ClampMax = "200.0"))
	float MediumFontSize = 20.f;

	/** Small captions and hints. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "6.0", ClampMax = "200.0"))
	float SmallFontSize = 14.f;

	// --- Timing ----------------------------------------------------------------------------------

	/** Lifetime of a near-miss popup, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float PopupLifetimeSeconds = 1.6f;

	/** Fade-in of a popup, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PopupFadeInSeconds = 0.12f;

	/** Fade-out at the end of a popup's life, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PopupFadeOutSeconds = 0.5f;

	/** Upward drift of a popup over its lifetime, pixels at the reference height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float PopupRisePixels = 70.f;

	/** Horizontal slide-in distance of a popup, pixels at the reference height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float PopupSlidePixels = 80.f;

	/** Maximum popups on screen (the oldest is recycled). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxPopups = 5;

	/** Max delay between a near-miss event and its score event to show the points on the popup, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PointsAttachWindowSeconds = 0.25f;

	/** Controls hint duration at the start of the first run, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float ControlsHintSeconds = 8.f;

	/** Fade-out of the controls hint, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float ControlsHintFadeSeconds = 1.f;

	/** How long "¡YA!" stays after the countdown, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float GoDisplaySeconds = 0.9f;

	/** How long "COMBO ROTO" stays visible, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ComboBrokenDisplaySeconds = 1.f;

	/** Convergence rate of the rolling score counter, 1/s (0 = no rolling). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float ScoreRollRate = 10.f;

	// --- Instruments -------------------------------------------------------------------------------

	/** Start of the red zone of the RPM bar, fraction of the rev limiter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Instruments", meta = (ClampMin = "0.3", ClampMax = "1.0"))
	float RedlineFraction = 0.88f;

	/** Thousands separator of scores and credits (may be empty). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Instruments")
	FString ThousandsSeparator = TEXT(".");

	/** Decimal separator (first character used). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Instruments")
	FString DecimalSeparator = TEXT(",");

	// --- Colors ------------------------------------------------------------------------------------

	/** Main text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor PrimaryColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** Secondary labels and captions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor SecondaryColor = FLinearColor(0.72f, 0.75f, 0.8f, 1.f);

	/** Highlights: combo, multipliers, active prompts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor AccentColor = FLinearColor(1.f, 0.62f, 0.05f, 1.f);

	/** Crash banner, redline, broken combo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor DangerColor = FLinearColor(1.f, 0.12f, 0.08f, 1.f);

	/** Panel background (use alpha for transparency). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor PanelColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

	/** Full-screen dimming behind the results panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor DimColor = FLinearColor(0.f, 0.f, 0.f, 0.45f);

	/** Filled part of the RPM bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor RpmColor = FLinearColor(0.85f, 0.9f, 1.f, 1.f);

	/** Near-miss tier "CERCA". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor CloseColor = FLinearColor(0.55f, 0.85f, 1.f, 1.f);

	/** Near-miss tier "MUY CERCA". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor VeryCloseColor = FLinearColor(1.f, 0.8f, 0.1f, 1.f);

	/** Near-miss tier "¡AL MILÍMETRO!". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor RazorColor = FLinearColor(1.f, 0.25f, 0.55f, 1.f);

	/** "CONTRAMANO" tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor OncomingColor = FLinearColor(1.f, 0.35f, 0.15f, 1.f);

	/** "¡ENTRE DOS!" tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor ThreadColor = FLinearColor(0.4f, 1.f, 0.5f, 1.f);

	/** Text shadow color. The canvas draws it with the alpha of its text, so its own alpha is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor ShadowColor = FLinearColor(0.f, 0.f, 0.f, 0.8f);

	/** Returns a copy with every value clamped to a safe range (protects the HUD from bad data). */
	FLRHUDTuning Sanitized() const;

	/** First character of DecimalSeparator, ',' when empty. */
	TCHAR GetDecimalSeparatorChar() const { return DecimalSeparator.Len() > 0 ? DecimalSeparator[0] : TEXT(','); }

	/** Relative path (under Data/) of the tuning file. */
	static const TCHAR* DataPath;
};

/**
 * Subset of Data/Gameplay/GameRules.json read by the HUD (the file is owned by LRGameplay; unknown fields are ignored).
 * Lets the countdown overlay match the gameplay countdown without depending on LRGameplay.
 */
USTRUCT(BlueprintType)
struct LRUI_API FLRHUDGameRules
{
	GENERATED_BODY()

	/** Duration of the "3, 2, 1" countdown before a run, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float CountdownSeconds = 3.f;

	/** Relative path (under Data/) of the gameplay rules file. */
	static const TCHAR* DataPath;
};
