// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"

/** Speed units shown by the HUD (console variable lr.HUD.Units kmh|mph). Simulation always stays in SI units. */
enum class ELRSpeedUnits : uint8
{
	/** Kilometers per hour (default). */
	Kmh,
	/** Miles per hour. */
	Mph
};

/**
 * Pure text formatting helpers for the HUD (no UObject, no world: unit-testable).
 *
 * Numbers are grouped and separated with explicit separators instead of the active culture so the HUD looks the same
 * on every machine and tests are deterministic. Every visible word goes through LOCTEXT (namespace "LRHUD").
 */
struct LRUI_API FLRHUDFormat
{
	/** Converts a speed in km/h to the requested display units. */
	static float ConvertSpeed(float SpeedKmh, ELRSpeedUnits Units);

	/** Absolute speed in display units rounded to the nearest integer (what the speedometer shows). */
	static int32 RoundSpeedForDisplay(float SpeedKmh, ELRSpeedUnits Units);

	/** Speedometer number without unit, e.g. 237.6 km/h -> "238" (or "148" in mph). Never negative. */
	static FText FormatSpeed(float SpeedKmh, ELRSpeedUnits Units);

	/** Unit label shown under the speedometer: "KM/H" or "MPH". */
	static FText GetSpeedUnitLabel(ELRSpeedUnits Units);

	/**
	 * Parses a console / config value ("kmh", "km/h", "kph", "mph", case-insensitive, surrounding spaces ignored).
	 * @return false (OutUnits untouched) when the value is not recognized.
	 */
	static bool ParseSpeedUnits(const FString& Value, ELRSpeedUnits& OutUnits);

	/** Integer with a separator every three digits, e.g. (1234567, ".") -> "1.234.567". Handles negatives and INT64_MIN. */
	static FString GroupThousands(int64 Value, const FString& Separator);

	/** Score text with thousands separators, e.g. 1234567 -> "1.234.567". */
	static FText FormatScore(int64 Score, const FString& Separator = FString(TEXT(".")));

	/** Signed points of a popup, e.g. 1250 -> "+1.250" (negative values keep their minus sign). */
	static FText FormatPoints(int64 Points, const FString& Separator = FString(TEXT(".")));

	/**
	 * Fixed-point number without culture, rounded half away from zero, e.g. (12.345, 1, ',') -> "12,3".
	 * @param Decimals  Number of decimals, clamped to [0, 6].
	 */
	static FString FormatFixed(double Value, int32 Decimals, TCHAR DecimalSeparator);

	/** Distance in kilometers from meters, e.g. (12345 m, 1, ',') -> "12,3 km". */
	static FText FormatDistanceKm(double DistanceMeters, int32 Decimals = 1, TCHAR DecimalSeparator = TEXT(','));

	/** Gear indicator: negative -> "R", 0 -> "N", otherwise the gear number. */
	static FText FormatGear(int32 Gear);

	/** Multiplier with at most two decimals and no trailing zeros: 1 -> "x1", 1.5 -> "x1,5", 1.25 -> "x1,25". */
	static FText FormatMultiplier(float Multiplier, TCHAR DecimalSeparator = TEXT(','));

	/** Duration as minutes:seconds, e.g. 65.4 s -> "1:05". Negative values are treated as 0. */
	static FText FormatDuration(float Seconds);

	/** Spanish near-miss tier label: "CERCA", "MUY CERCA", "¡AL MILÍMETRO!" (empty for None). */
	static FText GetNearMissTierLabel(ELRNearMissTier Tier);

	/** Tag shown when the near miss was against oncoming traffic: "CONTRAMANO". */
	static FText GetOncomingLabel();

	/** Tag shown when the player threaded between two vehicles: "¡ENTRE DOS!". */
	static FText GetThreadTheNeedleLabel();

	/** Control scheme indicator: "ARCADE" or "SIMCADE". */
	static FText GetControlSchemeLabel(ELRControlScheme Scheme);
};

/** Pure animation and layout math used by the HUD (unit-testable). */
struct LRUI_API FLRHUDAnim
{
	/** Cubic ease-out of T clamped to [0, 1]. */
	static float EaseOutCubic(float T);

	/**
	 * Opacity envelope of a transient element: ramps 0 -> 1 during FadeInSeconds, holds, then 1 -> 0 during the last
	 * FadeOutSeconds of LifetimeSeconds. Returns 0 outside [0, LifetimeSeconds].
	 */
	static float FadeInOut(float AgeSeconds, float LifetimeSeconds, float FadeInSeconds, float FadeOutSeconds);

	/**
	 * Exponential approach of the displayed (rolling) score to the real one. Snaps when closer than one point.
	 * @param RatePerSecond  Convergence rate (higher = faster), 1/s.
	 */
	static double ApproachScore(double Displayed, int64 Target, float RatePerSecond, float DeltaSeconds);

	/** Number shown by the countdown for the remaining time: ceil(Remaining), 0 when the countdown is over. */
	static int32 GetCountdownNumber(float RemainingSeconds);

	/** Progress inside the current countdown second: 0 when a new number appears, approaching 1 right before the next. */
	static float GetCountdownPhase(float RemainingSeconds);

	/** HUD scale factor for a canvas height: CanvasHeight / ReferenceHeight clamped to [MinScale, MaxScale]. */
	static float ComputeUIScale(float CanvasHeight, float ReferenceHeight, float MinScale, float MaxScale);
};
