// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRTerrainField.h"

namespace LRTerrainFieldPrivate
{
	/** Sum of noise octaves, about [-1, 1]. */
	float FractalNoise(double X, double Y, int32 Octaves)
	{
		float Sum = 0.f;
		float Amplitude = 1.f;
		float Norm = 0.f;
		double Scale = 1.0;
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			Sum += Amplitude * FMath::PerlinNoise2D(FVector2D(X * Scale + 17.3 * Octave, Y * Scale - 9.1 * Octave));
			Norm += Amplitude;
			Amplitude *= 0.5f;
			Scale *= 2.03;
		}
		return Sum / FMath::Max(Norm, UE_SMALL_NUMBER);
	}
}

float LRTerrainField::GetHeightM(const FLRTerrainParams& Params, double WorldXM, double WorldYM, float DistanceM)
{
	using namespace LRTerrainFieldPrivate;

	const float Distance = FMath::Max(0.f, DistanceM);

	// Ditch: a smooth dip that starts and ends at road level.
	float Height = 0.f;
	if (Distance < Params.DitchWidthM && Params.DitchWidthM > 0.f)
	{
		const float Dip = FMath::Sin(PI * Distance / Params.DitchWidthM);
		Height -= Params.DitchDepthM * Dip * Dip;
	}

	// Hills: fade in after the flat verge, so they never touch the road.
	const float Ramp = FMath::SmoothStep(Params.FlatWidthM, Params.FlatWidthM + FMath::Max(1.f, Params.HillRampM), Distance);
	if (Ramp > 0.f)
	{
		const double Frequency = 1.0 / FMath::Max(1.0, static_cast<double>(Params.HillWavelengthM));
		const double Offset = 131.7 * Params.Seed;
		const float Noise = FractalNoise(WorldXM * Frequency + Offset, WorldYM * Frequency - Offset, FMath::Max(1, Params.HillOctaves));
		// Biased upwards: the road runs along a valley, there is land to look at on both sides.
		Height += Ramp * Params.HillAmplitudeM * (0.45f + 0.55f * Noise);
	}
	return Height;
}

void LRTerrainField::BuildColumnDistances(const FLRTerrainParams& Params, TArray<float>& OutDistancesM)
{
	OutDistancesM.Reset();
	const float MaxDistance = FMath::Max(1.f, Params.MaxDistanceM);

	// Dense next to the road, where the ditch needs it, and sparser with the distance.
	float Distance = 0.f;
	float Step = FMath::Max(0.5f, Params.DitchWidthM / 4.f);
	while (Distance < MaxDistance)
	{
		OutDistancesM.Add(Distance);
		Distance += Step;
		Step *= 1.45f;
	}
	OutDistancesM.Add(MaxDistance);
}

float LRTerrainField::GetRidgeHeightM(float AngleRad, float BaseHeightM, float AmplitudeM, int32 Seed)
{
	using namespace LRTerrainFieldPrivate;

	// Noise over a circle in the noise plane: the ring closes because the angle wraps.
	constexpr double RingRadius = 4.5;
	const double X = RingRadius * FMath::Cos(AngleRad) + 57.1 * Seed;
	const double Y = RingRadius * FMath::Sin(AngleRad) - 23.9 * Seed;
	const float Noise = FractalNoise(X, Y, 6);
	return BaseHeightM + AmplitudeM * FMath::Max(0.f, 0.55f + 0.9f * Noise);
}
