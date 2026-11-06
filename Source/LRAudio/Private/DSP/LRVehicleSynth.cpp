// Copyright LEFTRIGHT. All Rights Reserved.

#include "DSP/LRVehicleSynth.h"

static_assert(FLREngineAudioProfile::MaxHarmonics == FLROscillatorBank::MaxPartials, "Profile harmonics must fit the oscillator bank");

namespace LRVehicleSynthPrivate
{
	/** High-pass cutoff that removes DC from the engine layer, Hz. */
	constexpr float EngineDcBlockHz = 15.f;

	/** Decay time constant of an overrun crackle, seconds. */
	constexpr float CrackleDecaySeconds = 0.012f;

	/** Smoothing of the rev limiter gate (avoids clicks), seconds. */
	constexpr float LimiterGateSmoothingSeconds = 0.002f;

	/** Gate level during a limiter fuel cut. */
	constexpr float LimiterCutLevel = 0.35f;

	/** The engine fades in from 0 rpm up to this speed (a stopped engine is silent), rpm. */
	constexpr float EngineAudibleRpm = 200.f;

	/** Time constant of the speed smoothing, seconds. */
	constexpr float SpeedSmoothingSeconds = 0.05f;

	/** Low-pass of the noise part of a joint thump, Hz. */
	constexpr float JointNoiseCutoffHz = 400.f;

	/** The audio-side road distance snaps to the game value beyond this error, meters. */
	constexpr double RoadResyncMeters = 5.0;

	/** Fraction of the road distance error corrected per parameter update. */
	constexpr double RoadCorrectionRate = 0.25;

	/** Second gust oscillator rate, as a multiple of WindGustRateHz (non-integer: no audible loop). */
	constexpr float SecondGustRateRatio = 2.37f;

	/** Fraction of a whoosh spent in the attack. */
	constexpr float WhooshAttackFraction = 0.25f;

	/** Crash envelope time constant as a fraction of CrashDurationSeconds. */
	constexpr float CrashDecayFraction = 0.25f;

	/** Below this absolute speed no joint is triggered, m/s. */
	constexpr float MinJointSpeedMs = 0.5f;

	/** Per-sample exponential decay factor for a time constant. */
	float DecayFactor(float TimeSeconds, float SampleRate)
	{
		return TimeSeconds > 0.f ? FMath::Exp(-1.f / (TimeSeconds * SampleRate)) : 0.f;
	}

	/** Makeup gain for low-passed white noise: sqrt(Nyquist / cutoff), clamped to [1, 12]. */
	float LowPassNoiseMakeup(float CutoffHz, float SampleRate)
	{
		return FMath::Clamp(FMath::Sqrt(0.5f * SampleRate / LRSynthDSP::ClampFrequency(CutoffHz, SampleRate)), 1.f, 12.f);
	}

	/** Returns Value if it is finite, otherwise Fallback. */
	float FiniteOr(float Value, float Fallback)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	}

	/** Whoosh amplitude envelope for a normalized age X in [0, 1]. */
	float WhooshEnvelope(float X)
	{
		if (X < WhooshAttackFraction)
		{
			const float T = X / WhooshAttackFraction;
			return T * T * (3.f - 2.f * T);
		}
		const float Decay = FMath::Clamp(1.f - (X - WhooshAttackFraction) / (1.f - WhooshAttackFraction), 0.f, 1.f);
		return Decay * Decay;
	}
}

FLRVehicleSynth::FLRVehicleSynth()
	: Profile(FLREngineAudioProfile().Sanitized())
{
	EngineNoise.Seed(0x1234567u);
	WindNoiseLeft.Seed(0x2468ACEu);
	WindNoiseRight.Seed(0x13579BDu);
	TyreNoise.Seed(0x0F1E2D3u);
	OneShotNoise.Seed(0x5A5A5A5u);
	ConfigureFromProfile();
	Reset();
}

void FLRVehicleSynth::Prepare(float InSampleRate, int32 InNumChannels)
{
	SampleRate = FMath::Max(InSampleRate, 8000.f);
	InvSampleRate = 1.f / SampleRate;
	NumChannels = FMath::Max(InNumChannels, 1);
	ConfigureFromProfile();
	Reset();
}

void FLRVehicleSynth::SwapProfile(FLREngineAudioProfile& InOutProfile)
{
	Swap(Profile, InOutProfile);
	ConfigureFromProfile();
}

void FLRVehicleSynth::ApplyProfile(const FLREngineAudioProfile& InProfile)
{
	FLREngineAudioProfile Sanitized = InProfile.Sanitized();
	SwapProfile(Sanitized);
}

void FLRVehicleSynth::ConfigureFromProfile()
{
	using namespace LRVehicleSynthPrivate;

	// Partials. Sanitized profiles always carry at least one. Fixed arrays, no allocation.
	float Orders[FLROscillatorBank::MaxPartials];
	const int32 Count = FMath::Min(Profile.Harmonics.Num(), FLROscillatorBank::MaxPartials);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FLREngineHarmonic& Harmonic = Profile.Harmonics[Index];
		Orders[Index] = Harmonic.Order;
		OffLoadGains[Index] = FMath::Clamp(Harmonic.OffLoadGain, 0.f, 1.f);
		OnLoadGains[Index] = FMath::Clamp(Harmonic.OnLoadGain, 0.f, 1.f);
	}
	Bank.Configure(Orders, OffLoadGains, Count);

	// Normalize so the partial sum never exceeds 1 at any load.
	float PeakSum = 0.f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		PeakSum += FMath::Max(OffLoadGains[Index], OnLoadGains[Index]);
	}
	BankNormalization = 1.f / FMath::Max(PeakSum, 0.001f);

	RpmSmoother.SetTime(Profile.RpmSmoothingSeconds, SampleRate);
	LoadSmoother.SetTime(Profile.LoadSmoothingSeconds, SampleRate);
	SpeedSmoother.SetTime(SpeedSmoothingSeconds, SampleRate);
	EngineOnSmoother.SetTime(Profile.EngineCutFadeSeconds, SampleRate);
	InteriorSmoother.SetTime(Profile.MixSmoothingSeconds, SampleRate);

	ExhaustLowPass.SetCutoff(Profile.ExhaustNoiseCutoffHz, SampleRate);
	ExhaustMakeup = LowPassNoiseMakeup(Profile.ExhaustNoiseCutoffHz, SampleRate);
	EngineDcBlock.SetCutoff(EngineDcBlockHz, SampleRate);
	DriveNormalization = 1.f / FMath::Max(LRSynthDSP::SoftClip(FMath::Max(Profile.Drive, 0.1f)), 0.05f);

	CrackleDecay = DecayFactor(CrackleDecaySeconds, SampleRate);
	LimiterGateCoefficient = LRSynthDSP::SmoothingCoefficient(LimiterGateSmoothingSeconds, SampleRate);

	TyreLowPass.SetLowPass(Profile.TyreCutoffHz, 0.9f, SampleRate);
	TyreMakeup = LowPassNoiseMakeup(Profile.TyreCutoffHz, SampleRate);
	JointDecay = DecayFactor(Profile.JointDecaySeconds, SampleRate);
	JointNoiseLowPass.SetCutoff(JointNoiseCutoffHz, SampleRate);

	CrashDecay = DecayFactor(Profile.CrashDurationSeconds * CrashDecayFraction, SampleRate);
	CrashLowPass.SetCutoff(Profile.CrashCutoffHz, SampleRate);
	CrashMakeup = LowPassNoiseMakeup(Profile.CrashCutoffHz, SampleRate);
}

void FLRVehicleSynth::SetParams(const FLRVehicleSynthParams& InParams)
{
	using LRVehicleSynthPrivate::FiniteOr;

	Params = InParams;
	Params.Rpm = FiniteOr(InParams.Rpm, 0.f);
	Params.MaxRpm = FiniteOr(InParams.MaxRpm, 0.f);
	Params.Throttle = FiniteOr(InParams.Throttle, 0.f);
	Params.SpeedMs = FiniteOr(InParams.SpeedMs, 0.f);
	Params.WheelbaseMeters = FiniteOr(InParams.WheelbaseMeters, 2.7f);
	Params.bHasRoadS = InParams.bHasRoadS && FMath::IsFinite(InParams.RoadS);

	RpmSmoother.SetTarget(FMath::Max(Params.Rpm, 0.f));
	LoadSmoother.SetTarget(FMath::Clamp(Params.Throttle, 0.f, 1.f));
	SpeedSmoother.SetTarget(Params.SpeedMs);
	EngineOnSmoother.SetTarget(Params.bEngineOn ? 1.f : 0.f);
	InteriorSmoother.SetTarget(Params.bInterior ? 1.f : 0.f);

	// Road distance: the audio thread integrates speed per sample and is pulled toward the game value.
	if (!Params.bHasRoadS || Profile.JointSpacingMeters <= 0.f)
	{
		bJointTrackerValid = false;
		return;
	}
	const double Spacing = static_cast<double>(Profile.JointSpacingMeters);
	if (!bJointTrackerValid || FMath::Abs(Params.RoadS - AudioRoadS) > LRVehicleSynthPrivate::RoadResyncMeters)
	{
		AudioRoadS = Params.RoadS;
		LastJointIndex = static_cast<int64>(FMath::FloorToDouble(AudioRoadS / Spacing));
		bJointTrackerValid = true;
	}
	else
	{
		AudioRoadS += LRVehicleSynthPrivate::RoadCorrectionRate * (Params.RoadS - AudioRoadS);
	}
}

void FLRVehicleSynth::TriggerWhoosh(float Intensity, float Pan)
{
	const float ClampedIntensity = FMath::Clamp(Intensity, 0.f, 1.f);
	if (ClampedIntensity <= 0.f)
	{
		return;
	}

	// Free voice, or steal the oldest one.
	int32 VoiceIndex = 0;
	float OldestAge = -1.f;
	for (int32 Index = 0; Index < MaxWhooshVoices; ++Index)
	{
		if (!WhooshVoices[Index].bActive)
		{
			VoiceIndex = Index;
			break;
		}
		if (WhooshVoices[Index].Age > OldestAge)
		{
			OldestAge = WhooshVoices[Index].Age;
			VoiceIndex = Index;
		}
	}

	FWhooshVoice& Voice = WhooshVoices[VoiceIndex];
	Voice.bActive = true;
	Voice.Age = 0.f;
	Voice.Intensity = ClampedIntensity;
	const float ClampedPan = FMath::Clamp(Pan, -1.f, 1.f) * Profile.WhooshMaxPan;
	Voice.GainLeft = LRSynthDSP::PanGainLeft(ClampedPan);
	Voice.GainRight = LRSynthDSP::PanGainRight(ClampedPan);
	Voice.BandPass.Reset();
	Voice.BandPass.SetBandPass(Profile.WhooshStartHz, Profile.WhooshQ, SampleRate);
	Voice.Makeup = LRSynthDSP::BandPassNoiseMakeup(Profile.WhooshStartHz, Profile.WhooshQ, SampleRate);
}

void FLRVehicleSynth::TriggerCrash(float Intensity)
{
	const float ClampedIntensity = FMath::Clamp(Intensity, 0.f, 1.f);
	if (ClampedIntensity <= 0.f)
	{
		return;
	}
	// A weaker hit never cuts a louder crash short.
	if (bCrashActive && CrashEnvelope * CrashIntensity > ClampedIntensity)
	{
		return;
	}
	bCrashActive = true;
	CrashAge = 0.f;
	CrashIntensity = ClampedIntensity;
	CrashEnvelope = 1.f;
	CrashThumpPhase = 0.f;
}

void FLRVehicleSynth::HandleEvent(const FLRVehicleSynthEvent& Event)
{
	switch (Event.Type)
	{
	case FLRVehicleSynthEvent::EType::Whoosh:
		TriggerWhoosh(Event.Intensity, Event.Pan);
		break;
	case FLRVehicleSynthEvent::EType::Crash:
		TriggerCrash(Event.Intensity);
		break;
	case FLRVehicleSynthEvent::EType::Reset:
		for (FWhooshVoice& Voice : WhooshVoices)
		{
			Voice.bActive = false;
		}
		bCrashActive = false;
		CrashEnvelope = 0.f;
		bJointTrackerValid = false;
		JointEnvelope = 0.f;
		RearAxleDelaySamples = -1.f;
		break;
	default:
		break;
	}
}

void FLRVehicleSynth::Reset()
{
	RpmSmoother.Snap(FMath::Max(Params.Rpm, 0.f));
	LoadSmoother.Snap(FMath::Clamp(Params.Throttle, 0.f, 1.f));
	SpeedSmoother.Snap(Params.SpeedMs);
	EngineOnSmoother.Snap(Params.bEngineOn ? 1.f : 0.f);
	InteriorSmoother.Snap(Params.bInterior ? 1.f : 0.f);

	Bank.Reset();
	EngineTone.Reset();
	IntakeBand.Reset();
	ExhaustLowPass.Reset();
	EngineDcBlock.Reset();
	InteriorLowPass.Reset();
	CycleFrequencyJitter = 0.f;
	CycleAmplitude = 1.f;
	LastFiringPhase = 0.f;
	CrackleEnvelope = 0.f;
	LimiterPhase = 0.f;
	LimiterGate = 1.f;

	WindBandLeft.Reset();
	WindBandRight.Reset();
	GustPhase = 0.f;
	GustPhase2 = 0.f;

	TyreLowPass.Reset();
	bJointTrackerValid = false;
	JointEnvelope = 0.f;
	JointPhase = 0.f;
	RearAxleDelaySamples = -1.f;
	JointNoiseLowPass.Reset();
	JointCount = 0;

	for (FWhooshVoice& Voice : WhooshVoices)
	{
		Voice.bActive = false;
		Voice.BandPass.Reset();
	}
	bCrashActive = false;
	CrashEnvelope = 0.f;
	CrashLowPass.Reset();

	UpdateControl();
}

int32 FLRVehicleSynth::GetActiveWhooshCount() const
{
	int32 Count = 0;
	for (const FWhooshVoice& Voice : WhooshVoices)
	{
		Count += Voice.bActive ? 1 : 0;
	}
	return Count;
}

float FLRVehicleSynth::ComputeWhooshIntensity(const FLREngineAudioProfile& InProfile, float RelativeSpeedKmh, float ClearanceMeters)
{
	const float SpeedFactor = FMath::Clamp(FMath::Abs(RelativeSpeedKmh) / FMath::Max(InProfile.WhooshReferenceRelativeKmh, 1.f), 0.f, 1.5f);
	const float ClearanceFactor = FMath::Exp(-FMath::Max(ClearanceMeters, 0.f) / FMath::Max(InProfile.WhooshClearanceFalloffMeters, 0.01f));
	return FMath::Clamp(SpeedFactor * ClearanceFactor, 0.f, 1.f);
}

float FLRVehicleSynth::ComputeCrashIntensity(const FLREngineAudioProfile& InProfile, float ImpactSpeedKmh)
{
	return FMath::Clamp(FMath::Abs(ImpactSpeedKmh) / FMath::Max(InProfile.CrashReferenceKmh, 1.f), 0.f, 1.f);
}

float FLRVehicleSynth::ComputeWhooshPan(float PlayerD, float VehicleD)
{
	const float Delta = VehicleD - PlayerD;
	return Delta > 0.f ? 1.f : (Delta < 0.f ? -1.f : 0.f);
}

void FLRVehicleSynth::UpdateControl()
{
	const float MaxRpm = Params.MaxRpm > 100.f ? Params.MaxRpm : Profile.FallbackMaxRpm;
	RpmNormalized = FMath::Clamp(RpmSmoother.Value / FMath::Max(MaxRpm, 100.f), 0.f, 1.2f);
	const float RpmUnit = FMath::Min(RpmNormalized, 1.f);
	Load = FMath::Clamp(LoadSmoother.Value, 0.f, 1.f);
	FiringHz = LRSynthDSP::FiringFrequencyHz(RpmSmoother.Value, Profile.CylinderCount);

	// Engine timbre: partial gains, tone cutoff, intake band and level follow load and rpm.
	for (int32 Index = 0; Index < Bank.NumPartials; ++Index)
	{
		Bank.Gains[Index] = FMath::Lerp(OffLoadGains[Index], OnLoadGains[Index], Load) * BankNormalization;
	}
	const float RpmTrack = FMath::Lerp(1.f, FMath::Clamp(RpmUnit, 0.15f, 1.f), Profile.CutoffRpmTracking);
	EngineTone.SetLowPass(FMath::Lerp(Profile.OffLoadCutoffHz, Profile.OnLoadCutoffHz, Load) * RpmTrack, 0.7071f, SampleRate);

	const float IntakeCenterHz = FMath::Max(FiringHz * Profile.IntakeNoiseOrder, 60.f);
	IntakeBand.SetBandPass(IntakeCenterHz, Profile.IntakeNoiseQ, SampleRate);
	IntakeMakeup = LRSynthDSP::BandPassNoiseMakeup(IntakeCenterHz, Profile.IntakeNoiseQ, SampleRate);

	const float RpmPresence = FMath::Clamp(RpmSmoother.Value / LRVehicleSynthPrivate::EngineAudibleRpm, 0.f, 1.f);
	EngineLevel = Profile.EngineGain * FMath::Lerp(Profile.OffLoadLevel, 1.f, Load) * FMath::Lerp(Profile.IdleLevel, 1.f, RpmUnit) * RpmPresence;

	// Cabin mix.
	const float Interior = FMath::Clamp(InteriorSmoother.Value, 0.f, 1.f);
	InteriorLowPass.SetCutoff(FMath::Lerp(20000.f, Profile.InteriorEngineCutoffHz, Interior), SampleRate);
	EngineMix = FMath::Lerp(1.f, Profile.InteriorEngineGain, Interior);
	WindMix = FMath::Lerp(1.f, Profile.InteriorWindGain, Interior);
	TyreMix = FMath::Lerp(1.f, Profile.InteriorTyreGain, Interior);
	WhooshMix = FMath::Lerp(1.f, Profile.InteriorWhooshGain, Interior);

	// Wind ~ v^2, band rising with speed.
	const float SpeedKmh = FMath::Abs(SpeedSmoother.Value) * 3.6f;
	const float WindRatio = SpeedKmh / FMath::Max(Profile.WindReferenceSpeedKmh, 1.f);
	WindLevel = Profile.WindGain * FMath::Min(WindRatio * WindRatio, Profile.WindMaxScale);
	const float WindCenterHz = FMath::Lerp(Profile.WindLowHz, Profile.WindHighHz, FMath::Clamp(WindRatio, 0.f, 1.5f));
	WindBandLeft.SetBandPass(WindCenterHz, Profile.WindQ, SampleRate);
	WindBandRight.SetBandPass(WindCenterHz * 1.07f, Profile.WindQ, SampleRate);
	WindMakeup = LRSynthDSP::BandPassNoiseMakeup(WindCenterHz, Profile.WindQ, SampleRate);

	// Tyre rumble.
	const float TyreRatio = SpeedKmh / FMath::Max(Profile.TyreReferenceSpeedKmh, 1.f);
	const float TyreScale = FMath::Min(FMath::Pow(TyreRatio, Profile.TyreSpeedExponent), 1.5f);
	TyreLevel = Profile.TyreGain * TyreScale;
	JointSpeedScale = FMath::Min(TyreRatio, 1.5f);

	// Whoosh sweeps (exponential fall from start to end frequency).
	const float Duration = FMath::Max(Profile.WhooshDurationSeconds, 0.01f);
	const float SweepRatio = Profile.WhooshEndHz / FMath::Max(Profile.WhooshStartHz, 1.f);
	for (FWhooshVoice& Voice : WhooshVoices)
	{
		if (!Voice.bActive)
		{
			continue;
		}
		const float X = FMath::Clamp(Voice.Age / Duration, 0.f, 1.f);
		const float CenterHz = Profile.WhooshStartHz * FMath::Pow(SweepRatio, X);
		Voice.BandPass.SetBandPass(CenterHz, Profile.WhooshQ, SampleRate);
		Voice.Makeup = LRSynthDSP::BandPassNoiseMakeup(CenterHz, Profile.WhooshQ, SampleRate);
	}
}

float FLRVehicleSynth::RenderEngineSample()
{
	using namespace LRVehicleSynthPrivate;

	const float EngineOn = EngineOnSmoother.Next();
	RpmSmoother.Next();
	LoadSmoother.Next();

	const float Tone = Bank.Process(FiringHz * (1.f + CycleFrequencyJitter), SampleRate);
	if (Bank.bWrapped)
	{
		// New engine cycle: small random deviation of pitch and strength (combustion is never perfectly even).
		CycleFrequencyJitter = EngineNoise.NextBipolar() * Profile.CycleJitter;
		CycleAmplitude = 1.f + EngineNoise.NextBipolar() * Profile.AmplitudeJitter;
	}

	const float FiringPhase = Bank.GetFundamentalPhase();
	const bool bFired = FiringPhase < LastFiringPhase;
	LastFiringPhase = FiringPhase;

	const float Noise = EngineNoise.NextBipolar();
	const float RpmUnit = FMath::Min(RpmNormalized, 1.f);

	// Exhaust: low-passed noise gated by a pulse that starts at each firing.
	const float PulseBase = 1.f - FiringPhase;
	const float Pulse = PulseBase * PulseBase * PulseBase;
	const float Exhaust = ExhaustLowPass.Process(Noise) * ExhaustMakeup * Pulse * Profile.ExhaustNoiseGain * (0.4f + 0.6f * Load);

	// Intake roar: band-passed noise tracking the firing frequency.
	const float Intake = IntakeBand.Process(Noise) * IntakeMakeup * Profile.IntakeNoiseGain * (0.25f + 0.75f * Load) * RpmUnit;

	// Overrun crackles: random pops on firing with the throttle closed at high rpm.
	if (bFired && Load < 0.1f && RpmNormalized > Profile.OverrunMinRpmFraction && Profile.OverrunCrackleProbability > 0.f
		&& EngineNoise.NextUnit() < Profile.OverrunCrackleProbability)
	{
		CrackleEnvelope = 0.6f + 0.4f * EngineNoise.NextUnit();
	}
	const float Crackle = CrackleEnvelope * Noise * Profile.OverrunCrackleGain * 2.f;
	CrackleEnvelope *= CrackleDecay;

	// Rev limiter fuel cut.
	float GateTarget = 1.f;
	if (RpmNormalized >= Profile.LimiterRpmFraction)
	{
		LimiterPhase += Profile.LimiterCutHz * InvSampleRate;
		LimiterPhase -= FMath::FloorToFloat(LimiterPhase);
		GateTarget = LimiterPhase < 0.5f ? 1.f : LimiterCutLevel;
	}
	else
	{
		LimiterPhase = 0.f;
	}
	LimiterGate += LimiterGateCoefficient * (GateTarget - LimiterGate);

	const float Mixed = EngineTone.Process(Tone * CycleAmplitude) + Intake + Exhaust + Crackle;
	const float Shaped = LRSynthDSP::SoftClip(Profile.Drive * Mixed) * DriveNormalization;
	const float Engine = EngineDcBlock.Process(Shaped) * EngineLevel * LimiterGate * EngineOn;
	return InteriorLowPass.Process(Engine) * EngineMix;
}

float FLRVehicleSynth::RenderJointSample(float SpeedMs)
{
	using namespace LRVehicleSynthPrivate;

	if (bJointTrackerValid && Profile.JointSpacingMeters > 0.f)
	{
		AudioRoadS += static_cast<double>(SpeedMs * InvSampleRate);
		const int64 JointIndex = static_cast<int64>(FMath::FloorToDouble(AudioRoadS / static_cast<double>(Profile.JointSpacingMeters)));
		const bool bForwardCross = SpeedMs >= MinJointSpeedMs && JointIndex > LastJointIndex;
		const bool bBackwardCross = SpeedMs <= -MinJointSpeedMs && JointIndex < LastJointIndex;
		if (bForwardCross || bBackwardCross)
		{
			LastJointIndex = JointIndex;
			++JointCount;
			JointEnvelope = 1.f;
			JointPhase = 0.f;
			JointAmplitude = Profile.JointGain * JointSpeedScale;
			// Rear axle hits the same joint one wheelbase later.
			RearAxleDelaySamples = FMath::Max(Params.WheelbaseMeters, 0.5f) / FMath::Abs(SpeedMs) * SampleRate;
		}
	}

	if (RearAxleDelaySamples >= 0.f)
	{
		RearAxleDelaySamples -= 1.f;
		if (RearAxleDelaySamples < 0.f)
		{
			RearAxleDelaySamples = -1.f;
			JointEnvelope = 1.f;
			JointPhase = 0.f;
			JointAmplitude = Profile.JointGain * JointSpeedScale * 0.85f;
		}
	}

	if (JointEnvelope < 1.0e-4f)
	{
		JointEnvelope = 0.f;
		return 0.f;
	}

	JointPhase += Profile.JointToneHz * InvSampleRate;
	JointPhase -= FMath::FloorToFloat(JointPhase);
	const float Thump = JointEnvelope * JointAmplitude
		* (FMath::Sin(LRSynthDSP::TwoPi * JointPhase) + 0.6f * JointNoiseLowPass.Process(TyreNoise.NextBipolar()) * 3.f);
	JointEnvelope *= JointDecay;
	return Thump;
}

void FLRVehicleSynth::Render(float* Out, int32 NumFrames)
{
	using namespace LRVehicleSynthPrivate;

	if (!Out || NumFrames <= 0)
	{
		return;
	}

	const float WhooshDuration = FMath::Max(Profile.WhooshDurationSeconds, 0.01f);
	const float CrashDuration = FMath::Max(Profile.CrashDurationSeconds, 0.01f);
	const float GustStep = Profile.WindGustRateHz * InvSampleRate;
	const float GustStep2 = GustStep * SecondGustRateRatio;

	int32 Frame = 0;
	while (Frame < NumFrames)
	{
		UpdateControl();
		const int32 BlockEnd = FMath::Min(Frame + ControlBlockFrames, NumFrames);
		float Probe = 0.f;

		for (; Frame < BlockEnd; ++Frame)
		{
			const float Engine = RenderEngineSample();
			const float SpeedMs = SpeedSmoother.Next();
			InteriorSmoother.Next();

			// Wind (decorrelated left / right).
			GustPhase += GustStep;
			GustPhase -= FMath::FloorToFloat(GustPhase);
			GustPhase2 += GustStep2;
			GustPhase2 -= FMath::FloorToFloat(GustPhase2);
			const float Gust = 1.f + Profile.WindGustDepth
				* (0.6f * FMath::Sin(LRSynthDSP::TwoPi * GustPhase) + 0.4f * FMath::Sin(LRSynthDSP::TwoPi * GustPhase2));
			const float WindScale = WindMakeup * WindLevel * Gust * WindMix;
			const float WindLeft = WindBandLeft.Process(WindNoiseLeft.NextBipolar()) * WindScale;
			const float WindRight = WindBandRight.Process(WindNoiseRight.NextBipolar()) * WindScale;

			// Tyres and joints.
			const float Rumble = TyreLowPass.Process(TyreNoise.NextBipolar()) * TyreMakeup * TyreLevel;
			const float Tyres = (Rumble + RenderJointSample(SpeedMs)) * TyreMix;

			// Whooshes.
			float WhooshLeft = 0.f;
			float WhooshRight = 0.f;
			for (FWhooshVoice& Voice : WhooshVoices)
			{
				if (!Voice.bActive)
				{
					continue;
				}
				const float Envelope = WhooshEnvelope(Voice.Age / WhooshDuration);
				const float Sample = Voice.BandPass.Process(OneShotNoise.NextBipolar()) * Voice.Makeup * Envelope * Voice.Intensity
					* Profile.WhooshGain * WhooshMix;
				WhooshLeft += Sample * Voice.GainLeft;
				WhooshRight += Sample * Voice.GainRight;
				Voice.Age += InvSampleRate;
				if (Voice.Age >= WhooshDuration)
				{
					Voice.bActive = false;
				}
			}

			// Crash: broadband burst plus a low body thump.
			float Crash = 0.f;
			if (bCrashActive)
			{
				CrashThumpPhase += Profile.CrashThumpHz * InvSampleRate;
				CrashThumpPhase -= FMath::FloorToFloat(CrashThumpPhase);
				const float Burst = CrashLowPass.Process(OneShotNoise.NextBipolar()) * CrashMakeup * CrashEnvelope;
				const float Thump = FMath::Sin(LRSynthDSP::TwoPi * CrashThumpPhase) * CrashEnvelope * CrashEnvelope * 1.5f;
				Crash = (Burst + Thump) * CrashIntensity * Profile.CrashGain;
				CrashEnvelope *= CrashDecay;
				CrashAge += InvSampleRate;
				if (CrashAge >= CrashDuration)
				{
					bCrashActive = false;
					CrashEnvelope = 0.f;
				}
			}

			const float Center = Engine + Tyres + Crash;
			const float PreLeft = Profile.MasterGain * (Center + WindLeft + WhooshLeft);
			const float PreRight = Profile.MasterGain * (Center + WindRight + WhooshRight);
			Probe = PreLeft + PreRight;

			const float Left = LRSynthDSP::SoftClip(PreLeft);
			const float Right = LRSynthDSP::SoftClip(PreRight);

			float* FrameOut = Out + static_cast<int64>(Frame) * NumChannels;
			if (NumChannels == 1)
			{
				FrameOut[0] = 0.5f * (Left + Right);
			}
			else
			{
				FrameOut[0] = Left;
				FrameOut[1] = Right;
				for (int32 Channel = 2; Channel < NumChannels; ++Channel)
				{
					FrameOut[Channel] = 0.f;
				}
			}
		}

		// A filter blew up (should never happen with sanitized parameters): clear every state and continue.
		if (!FMath::IsFinite(Probe))
		{
			Reset();
		}
	}
}
