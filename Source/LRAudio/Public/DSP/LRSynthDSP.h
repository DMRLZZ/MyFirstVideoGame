// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Pure, allocation-free DSP building blocks of the procedural vehicle sound.
 *
 * No UObject, no world, no engine audio dependency: everything here is plain C++ usable from the audio render
 * thread and from unit tests. All processors are tiny value types that keep their own state; call Reset() to clear
 * it. Frequencies are in Hz, times in seconds, sample rates in Hz.
 */
namespace LRSynthDSP
{
	/** 2 * pi. */
	inline constexpr float TwoPi = 6.28318530717958647692f;

	/** pi. */
	inline constexpr float Pi = 3.14159265358979323846f;

	/**
	 * Firing frequency of a four-stroke engine: every cylinder fires once per two crankshaft revolutions.
	 * @param Rpm        Crankshaft speed, rpm (negative values are treated as 0).
	 * @param Cylinders  Number of cylinders (at least 1).
	 * @return Firing frequency in Hz = rpm / 60 * cylinders / 2.
	 */
	inline float FiringFrequencyHz(float Rpm, int32 Cylinders)
	{
		return FMath::Max(Rpm, 0.f) / 60.f * static_cast<float>(FMath::Max(Cylinders, 1)) * 0.5f;
	}

	/**
	 * Smooth saturating curve (rational tanh approximation), exactly bounded to [-1, 1], odd and monotonic,
	 * with unit slope at 0. Values beyond +-3 return +-1.
	 */
	inline float SoftClip(float X)
	{
		if (!(X > -3.f))
		{
			// Also catches NaN: a NaN never reaches the output.
			return X != X ? 0.f : -1.f;
		}
		if (X >= 3.f)
		{
			return 1.f;
		}
		const float X2 = X * X;

		// The curve reaches exactly 1 at X = 3; the clamp removes the rounding error right below that point.
		return FMath::Clamp(X * (27.f + X2) / (27.f + 9.f * X2), -1.f, 1.f);
	}

	/** Clamps a cutoff frequency to the stable range [1 Hz, 0.49 * SampleRate]. */
	inline float ClampFrequency(float FrequencyHz, float SampleRate)
	{
		return FMath::Clamp(FrequencyHz, 1.f, 0.49f * FMath::Max(SampleRate, 1.f));
	}

	/** Coefficient of a one-pole low-pass (0..1, 1 = no filtering) for a -3 dB cutoff. */
	inline float OnePoleCoefficient(float CutoffHz, float SampleRate)
	{
		const float Fs = FMath::Max(SampleRate, 1.f);
		return 1.f - FMath::Exp(-TwoPi * ClampFrequency(CutoffHz, Fs) / Fs);
	}

	/** Per-sample coefficient of an exponential smoother with time constant TimeSeconds (<= 0 = instant). */
	inline float SmoothingCoefficient(float TimeSeconds, float SampleRate)
	{
		if (TimeSeconds <= 0.f || SampleRate <= 0.f)
		{
			return 1.f;
		}
		return 1.f - FMath::Exp(-1.f / (TimeSeconds * SampleRate));
	}

	/** Equal-power pan gain of the LEFT channel for Pan in [-1 (left), +1 (right)]. */
	inline float PanGainLeft(float Pan)
	{
		const float Angle = (FMath::Clamp(Pan, -1.f, 1.f) + 1.f) * 0.25f * Pi;
		return FMath::Cos(Angle);
	}

	/** Equal-power pan gain of the RIGHT channel for Pan in [-1 (left), +1 (right)]. */
	inline float PanGainRight(float Pan)
	{
		const float Angle = (FMath::Clamp(Pan, -1.f, 1.f) + 1.f) * 0.25f * Pi;
		return FMath::Sin(Angle);
	}

	/**
	 * Gain that brings band-passed white noise back to roughly the RMS of the unfiltered noise:
	 * sqrt(Nyquist / bandwidth), with bandwidth = center / Q. Clamped to [1, 12].
	 */
	inline float BandPassNoiseMakeup(float CenterHz, float Q, float SampleRate)
	{
		const float Bandwidth = FMath::Max(ClampFrequency(CenterHz, SampleRate) / FMath::Max(Q, 0.05f), 1.f);
		return FMath::Clamp(FMath::Sqrt(0.5f * SampleRate / Bandwidth), 1.f, 12.f);
	}
}

/**
 * Deterministic white-noise generator (xorshift32). Cheap, allocation free, thread-agnostic.
 * Not used for game simulation: it only feeds audio and never touches the run seed.
 */
struct FLRNoise
{
	/** Generator state (never 0). */
	uint32 State = 0x9E3779B9u;

	/** Sets the state from any seed (0 is remapped). */
	void Seed(uint32 InSeed)
	{
		State = InSeed != 0u ? InSeed : 0x9E3779B9u;
	}

	/** Next raw 32-bit value. */
	uint32 NextUInt()
	{
		uint32 X = State;
		X ^= X << 13;
		X ^= X >> 17;
		X ^= X << 5;
		State = X;
		return X;
	}

	/** Uniform value in [0, 1). */
	float NextUnit()
	{
		return static_cast<float>(NextUInt() >> 8) * (1.f / 16777216.f);
	}

	/** Uniform value in [-1, 1). */
	float NextBipolar()
	{
		return NextUnit() * 2.f - 1.f;
	}
};

/** One-pole low-pass filter (6 dB/octave). Unconditionally stable for coefficients in (0, 1]. */
struct FLROnePoleLowPass
{
	/** Smoothing coefficient (0..1). */
	float Coefficient = 1.f;

	/** Filter state (last output). */
	float State = 0.f;

	/** Sets the -3 dB cutoff. */
	void SetCutoff(float CutoffHz, float SampleRate)
	{
		Coefficient = LRSynthDSP::OnePoleCoefficient(CutoffHz, SampleRate);
	}

	/** Filters one sample. */
	float Process(float Input)
	{
		State += Coefficient * (Input - State);
		return State;
	}

	/** Clears the state. */
	void Reset(float Value = 0.f)
	{
		State = Value;
	}
};

/** One-pole high-pass filter (input minus its one-pole low-pass). */
struct FLROnePoleHighPass
{
	/** Internal low-pass. */
	FLROnePoleLowPass LowPass;

	/** Sets the -3 dB cutoff. */
	void SetCutoff(float CutoffHz, float SampleRate)
	{
		LowPass.SetCutoff(CutoffHz, SampleRate);
	}

	/** Filters one sample. */
	float Process(float Input)
	{
		return Input - LowPass.Process(Input);
	}

	/** Clears the state. */
	void Reset()
	{
		LowPass.Reset();
	}
};

/**
 * Second-order IIR filter (RBJ cookbook coefficients, transposed direct form II).
 * Band-pass uses the constant 0 dB peak gain form. Coefficients may be changed between samples.
 */
struct FLRBiquad
{
	/** Feed-forward coefficients (normalized by a0). */
	float B0 = 1.f;
	float B1 = 0.f;
	float B2 = 0.f;

	/** Feedback coefficients (normalized by a0). */
	float A1 = 0.f;
	float A2 = 0.f;

	/** Filter state. */
	float Z1 = 0.f;
	float Z2 = 0.f;

	/** Band-pass centered on CenterHz with quality factor Q (unity gain at the center). */
	void SetBandPass(float CenterHz, float Q, float SampleRate)
	{
		const float W0 = LRSynthDSP::TwoPi * LRSynthDSP::ClampFrequency(CenterHz, SampleRate) / FMath::Max(SampleRate, 1.f);
		const float Alpha = FMath::Sin(W0) / (2.f * FMath::Max(Q, 0.05f));
		const float CosW0 = FMath::Cos(W0);
		const float InvA0 = 1.f / (1.f + Alpha);
		B0 = Alpha * InvA0;
		B1 = 0.f;
		B2 = -Alpha * InvA0;
		A1 = -2.f * CosW0 * InvA0;
		A2 = (1.f - Alpha) * InvA0;
		EnsureStable();
	}

	/** Low-pass with -3 dB-ish cutoff CutoffHz and resonance Q (0.7071 = Butterworth). */
	void SetLowPass(float CutoffHz, float Q, float SampleRate)
	{
		const float W0 = LRSynthDSP::TwoPi * LRSynthDSP::ClampFrequency(CutoffHz, SampleRate) / FMath::Max(SampleRate, 1.f);
		const float Alpha = FMath::Sin(W0) / (2.f * FMath::Max(Q, 0.05f));
		const float CosW0 = FMath::Cos(W0);
		const float InvA0 = 1.f / (1.f + Alpha);
		B0 = (1.f - CosW0) * 0.5f * InvA0;
		B1 = (1.f - CosW0) * InvA0;
		B2 = B0;
		A1 = -2.f * CosW0 * InvA0;
		A2 = (1.f - Alpha) * InvA0;
		EnsureStable();
	}

	/** Filters one sample. */
	float Process(float Input)
	{
		const float Output = B0 * Input + Z1;
		Z1 = B1 * Input - A1 * Output + Z2;
		Z2 = B2 * Input - A2 * Output;
		return Output;
	}

	/** Clears the state. */
	void Reset()
	{
		Z1 = 0.f;
		Z2 = 0.f;
	}

	/** True when both poles lie strictly inside the unit circle (Jury criterion). */
	bool IsStable() const
	{
		return FMath::Abs(A2) < 1.f && FMath::Abs(A1) < 1.f + A2;
	}

private:
	/**
	 * At very low frequencies a pole sits so close to 1 that float rounding can push it onto the unit circle. Pulls A1
	 * back by the smallest amount that keeps the Jury criterion true (inaudible: it only affects sub-audio poles).
	 */
	void EnsureStable()
	{
		const float Limit = (1.f + A2) * (1.f - 1.0e-6f);
		A1 = FMath::Clamp(A1, -Limit, Limit);
	}
};

/** Exponential parameter smoother (de-zippers values coming from the game thread). */
struct FLRParamSmoother
{
	/** Current smoothed value. */
	float Value = 0.f;

	/** Value being approached. */
	float Target = 0.f;

	/** Per-sample coefficient (1 = instant). */
	float Coefficient = 1.f;

	/** Sets the time constant, seconds. */
	void SetTime(float TimeSeconds, float SampleRate)
	{
		Coefficient = LRSynthDSP::SmoothingCoefficient(TimeSeconds, SampleRate);
	}

	/** Sets the value to approach. */
	void SetTarget(float InTarget)
	{
		Target = InTarget;
	}

	/** Jumps to a value immediately. */
	void Snap(float InValue)
	{
		Value = InValue;
		Target = InValue;
	}

	/** Advances one sample and returns the smoothed value. */
	float Next()
	{
		Value += Coefficient * (Target - Value);
		return Value;
	}
};

/**
 * Bank of phase-locked sine partials at multiples of 0.5 of a fundamental (additive engine tone).
 *
 * A single phase accumulator runs at HALF the fundamental, so half orders (0.5, 1.5...) stay phase-coherent with
 * the integer ones. Partials at or above 0.45 * SampleRate are skipped (no aliasing).
 */
struct FLROscillatorBank
{
	/** Maximum number of partials. */
	static constexpr int32 MaxPartials = 12;

	/** Integer multiple of HALF the fundamental for each partial (order 1 -> 2, order 0.5 -> 1). */
	int32 HalfOrders[MaxPartials] = {};

	/** Linear gain of each partial (may be changed between samples). */
	float Gains[MaxPartials] = {};

	/** Number of active partials. */
	int32 NumPartials = 0;

	/** Phase of the half-fundamental, cycles in [0, 1). */
	float Phase = 0.f;

	/** True if the last Process call completed a half-fundamental cycle (= two fundamental cycles). */
	bool bWrapped = false;

	/** Sets the partials. Orders are rounded to the nearest 0.5 (minimum 0.5); extra partials are ignored. */
	void Configure(const float* Orders, const float* InGains, int32 Count)
	{
		NumPartials = FMath::Clamp(Count, 0, MaxPartials);
		for (int32 Index = 0; Index < NumPartials; ++Index)
		{
			HalfOrders[Index] = FMath::Max(1, FMath::RoundToInt(Orders[Index] * 2.f));
			Gains[Index] = InGains[Index];
		}
	}

	/** Phase of the fundamental in [0, 1) (useful to gate noise on every firing). */
	float GetFundamentalPhase() const
	{
		const float Doubled = Phase * 2.f;
		return Doubled - FMath::FloorToFloat(Doubled);
	}

	/**
	 * Advances one sample and returns the sum of the partials (not normalized).
	 * @param FundamentalHz  Frequency of order 1, Hz (<= 0 holds the phase and outputs the current value).
	 */
	float Process(float FundamentalHz, float SampleRate)
	{
		const float Fs = FMath::Max(SampleRate, 1.f);
		const float Nyquist = 0.45f * Fs;
		const float SafeFundamental = FMath::Max(FundamentalHz, 0.f);

		float Sum = 0.f;
		for (int32 Index = 0; Index < NumPartials; ++Index)
		{
			const float PartialHz = SafeFundamental * 0.5f * static_cast<float>(HalfOrders[Index]);
			if (PartialHz >= Nyquist)
			{
				continue;
			}
			float PartialPhase = Phase * static_cast<float>(HalfOrders[Index]);
			PartialPhase -= FMath::FloorToFloat(PartialPhase);
			Sum += Gains[Index] * FMath::Sin(LRSynthDSP::TwoPi * PartialPhase);
		}

		Phase += SafeFundamental * 0.5f / Fs;
		bWrapped = Phase >= 1.f;
		if (bWrapped)
		{
			Phase -= FMath::FloorToFloat(Phase);
		}
		return Sum;
	}

	/** Restarts the phase. */
	void Reset()
	{
		Phase = 0.f;
		bWrapped = false;
	}
};
