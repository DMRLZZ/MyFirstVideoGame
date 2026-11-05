// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Unit conversion helpers.
 *
 * Convention: gameplay and simulation code works in SI units (meters, seconds, m/s).
 * Unreal world space is in centimeters. Convert only at the boundary (rendering / physics).
 */
namespace LRUnits
{
	inline constexpr double CmPerMeter = 100.0;
	inline constexpr double MetersPerCm = 0.01;
	inline constexpr double KmhPerMs = 3.6;
	inline constexpr double MphPerMs = 2.2369362920544;

	/** Meters -> Unreal units (cm). */
	inline constexpr double MetersToCm(double Meters) { return Meters * CmPerMeter; }

	/** Unreal units (cm) -> meters. */
	inline constexpr double CmToMeters(double Cm) { return Cm * MetersPerCm; }

	/** m/s -> km/h. */
	inline constexpr double MsToKmh(double Ms) { return Ms * KmhPerMs; }

	/** km/h -> m/s. */
	inline constexpr double KmhToMs(double Kmh) { return Kmh / KmhPerMs; }

	/** m/s -> mph. */
	inline constexpr double MsToMph(double Ms) { return Ms * MphPerMs; }

	/** mph -> m/s. */
	inline constexpr double MphToMs(double Mph) { return Mph / MphPerMs; }

	/** cm/s (Unreal velocity) -> km/h. */
	inline constexpr double CmsToKmh(double Cms) { return MsToKmh(CmToMeters(Cms)); }
}
