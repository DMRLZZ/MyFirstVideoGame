// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRAudioTypes.h"

#include "LRAudio.h"
#include "LRDataLoader.h"

namespace LRAudioTypesPrivate
{
	/** Data file of a profile id, relative to Data/. */
	FString MakeProfilePath(FName ProfileId)
	{
		return FString::Printf(TEXT("Audio/%s.json"), *ProfileId.ToString());
	}

	/** Loads a profile file. Out is untouched on failure. */
	bool LoadProfileFile(FName ProfileId, FLREngineAudioProfile& Out, FString& OutError)
	{
		FLREngineAudioProfile Loaded;
		if (!FLRDataLoader::Load(MakeProfilePath(ProfileId), Loaded, &OutError))
		{
			return false;
		}
		Out = Loaded.Sanitized();
		return true;
	}
}

FLREngineAudioProfile FLREngineAudioProfile::Sanitized() const
{
	// The ranges mirror the UPROPERTY metadata and Data/Schema/EngineAudioProfile.schema.json.
	FLREngineAudioProfile Out = *this;

	Out.CylinderCount = FMath::Clamp(CylinderCount, 1, 16);
	Out.EngineGain = FMath::Clamp(EngineGain, 0.f, 2.f);
	Out.OffLoadLevel = FMath::Clamp(OffLoadLevel, 0.f, 1.f);
	Out.IdleLevel = FMath::Clamp(IdleLevel, 0.f, 1.f);
	Out.OffLoadCutoffHz = FMath::Clamp(OffLoadCutoffHz, 100.f, 16000.f);
	Out.OnLoadCutoffHz = FMath::Clamp(OnLoadCutoffHz, 100.f, 16000.f);
	Out.CutoffRpmTracking = FMath::Clamp(CutoffRpmTracking, 0.f, 1.f);
	Out.Drive = FMath::Clamp(Drive, 0.5f, 8.f);
	Out.CycleJitter = FMath::Clamp(CycleJitter, 0.f, 0.05f);
	Out.AmplitudeJitter = FMath::Clamp(AmplitudeJitter, 0.f, 0.5f);
	Out.IntakeNoiseGain = FMath::Clamp(IntakeNoiseGain, 0.f, 1.f);
	Out.IntakeNoiseOrder = FMath::Clamp(IntakeNoiseOrder, 0.5f, 16.f);
	Out.IntakeNoiseQ = FMath::Clamp(IntakeNoiseQ, 0.3f, 10.f);
	Out.ExhaustNoiseGain = FMath::Clamp(ExhaustNoiseGain, 0.f, 1.f);
	Out.ExhaustNoiseCutoffHz = FMath::Clamp(ExhaustNoiseCutoffHz, 50.f, 8000.f);
	Out.OverrunCrackleProbability = FMath::Clamp(OverrunCrackleProbability, 0.f, 0.5f);
	Out.OverrunCrackleGain = FMath::Clamp(OverrunCrackleGain, 0.f, 1.f);
	Out.OverrunMinRpmFraction = FMath::Clamp(OverrunMinRpmFraction, 0.f, 1.f);
	Out.LimiterRpmFraction = FMath::Clamp(LimiterRpmFraction, 0.8f, 1.f);
	Out.LimiterCutHz = FMath::Clamp(LimiterCutHz, 2.f, 60.f);
	Out.RpmSmoothingSeconds = FMath::Clamp(RpmSmoothingSeconds, 0.f, 1.f);
	Out.LoadSmoothingSeconds = FMath::Clamp(LoadSmoothingSeconds, 0.f, 1.f);
	Out.EngineCutFadeSeconds = FMath::Clamp(EngineCutFadeSeconds, 0.f, 2.f);
	Out.FallbackMaxRpm = FMath::Clamp(FallbackMaxRpm, 1000.f, 20000.f);
	Out.WindGain = FMath::Clamp(WindGain, 0.f, 2.f);
	Out.WindReferenceSpeedKmh = FMath::Clamp(WindReferenceSpeedKmh, 20.f, 500.f);
	Out.WindMaxScale = FMath::Clamp(WindMaxScale, 0.f, 4.f);
	Out.WindLowHz = FMath::Clamp(WindLowHz, 50.f, 8000.f);
	Out.WindHighHz = FMath::Clamp(WindHighHz, 50.f, 12000.f);
	Out.WindQ = FMath::Clamp(WindQ, 0.3f, 10.f);
	Out.WindGustDepth = FMath::Clamp(WindGustDepth, 0.f, 1.f);
	Out.WindGustRateHz = FMath::Clamp(WindGustRateHz, 0.01f, 5.f);
	Out.TyreGain = FMath::Clamp(TyreGain, 0.f, 2.f);
	Out.TyreReferenceSpeedKmh = FMath::Clamp(TyreReferenceSpeedKmh, 10.f, 500.f);
	Out.TyreSpeedExponent = FMath::Clamp(TyreSpeedExponent, 0.3f, 3.f);
	Out.TyreCutoffHz = FMath::Clamp(TyreCutoffHz, 30.f, 4000.f);
	Out.JointSpacingMeters = FMath::Clamp(JointSpacingMeters, 0.f, 200.f);
	Out.JointGain = FMath::Clamp(JointGain, 0.f, 2.f);
	Out.JointToneHz = FMath::Clamp(JointToneHz, 20.f, 400.f);
	Out.JointDecaySeconds = FMath::Clamp(JointDecaySeconds, 0.005f, 0.5f);
	Out.WhooshGain = FMath::Clamp(WhooshGain, 0.f, 2.f);
	Out.WhooshDurationSeconds = FMath::Clamp(WhooshDurationSeconds, 0.05f, 3.f);
	Out.WhooshStartHz = FMath::Clamp(WhooshStartHz, 100.f, 12000.f);
	Out.WhooshEndHz = FMath::Clamp(WhooshEndHz, 50.f, 12000.f);
	Out.WhooshQ = FMath::Clamp(WhooshQ, 0.3f, 10.f);
	Out.WhooshReferenceRelativeKmh = FMath::Clamp(WhooshReferenceRelativeKmh, 10.f, 500.f);
	Out.WhooshClearanceFalloffMeters = FMath::Clamp(WhooshClearanceFalloffMeters, 0.1f, 10.f);
	Out.WhooshMaxPan = FMath::Clamp(WhooshMaxPan, 0.f, 1.f);
	Out.WhooshSideProbeRadiusMeters = FMath::Clamp(WhooshSideProbeRadiusMeters, 1.f, 50.f);
	Out.CrashGain = FMath::Clamp(CrashGain, 0.f, 2.f);
	Out.CrashDurationSeconds = FMath::Clamp(CrashDurationSeconds, 0.05f, 5.f);
	Out.CrashCutoffHz = FMath::Clamp(CrashCutoffHz, 100.f, 16000.f);
	Out.CrashThumpHz = FMath::Clamp(CrashThumpHz, 20.f, 300.f);
	Out.CrashReferenceKmh = FMath::Clamp(CrashReferenceKmh, 5.f, 400.f);
	Out.MasterGain = FMath::Clamp(MasterGain, 0.f, 2.f);
	Out.InteriorEngineCutoffHz = FMath::Clamp(InteriorEngineCutoffHz, 100.f, 20000.f);
	Out.InteriorEngineGain = FMath::Clamp(InteriorEngineGain, 0.f, 2.f);
	Out.InteriorWindGain = FMath::Clamp(InteriorWindGain, 0.f, 2.f);
	Out.InteriorTyreGain = FMath::Clamp(InteriorTyreGain, 0.f, 2.f);
	Out.InteriorWhooshGain = FMath::Clamp(InteriorWhooshGain, 0.f, 2.f);
	Out.MixSmoothingSeconds = FMath::Clamp(MixSmoothingSeconds, 0.f, 2.f);

	if (Out.Harmonics.Num() == 0)
	{
		Out.Harmonics = MakeDefaultHarmonics();
	}
	else if (Out.Harmonics.Num() > MaxHarmonics)
	{
		Out.Harmonics.SetNum(MaxHarmonics);
	}
	for (FLREngineHarmonic& Harmonic : Out.Harmonics)
	{
		Harmonic.Order = FMath::Clamp(Harmonic.Order, 0.5f, 16.f);
		Harmonic.OffLoadGain = FMath::Clamp(Harmonic.OffLoadGain, 0.f, 1.f);
		Harmonic.OnLoadGain = FMath::Clamp(Harmonic.OnLoadGain, 0.f, 1.f);
	}
	return Out;
}

TArray<FLREngineHarmonic> FLREngineAudioProfile::MakeDefaultHarmonics()
{
	struct FPartial
	{
		float Order;
		float OffLoadGain;
		float OnLoadGain;
	};
	static const FPartial Partials[] = {
		{ 0.5f, 0.08f, 0.12f },
		{ 1.f, 0.5f, 0.6f },
		{ 1.5f, 0.06f, 0.1f },
		{ 2.f, 0.25f, 0.4f },
		{ 3.f, 0.12f, 0.25f },
		{ 4.f, 0.06f, 0.15f },
		{ 6.f, 0.03f, 0.08f }
	};

	TArray<FLREngineHarmonic> Harmonics;
	Harmonics.Reserve(UE_ARRAY_COUNT(Partials));
	for (const FPartial& Partial : Partials)
	{
		FLREngineHarmonic& Harmonic = Harmonics.AddDefaulted_GetRef();
		Harmonic.Order = Partial.Order;
		Harmonic.OffLoadGain = Partial.OffLoadGain;
		Harmonic.OnLoadGain = Partial.OnLoadGain;
	}
	return Harmonics;
}

bool FLREngineAudioProfile::LoadById(FName ProfileId, FLREngineAudioProfile& Out)
{
	using namespace LRAudioTypesPrivate;

	FString Error;
	if (!ProfileId.IsNone())
	{
		if (LoadProfileFile(ProfileId, Out, Error))
		{
			return true;
		}
		UE_LOG(LogLRAudio, Warning, TEXT("Audio profile '%s' not loaded (%s). Using the default profile."), *ProfileId.ToString(), *Error);
	}

	if (!LoadProfileFile(GetDefaultProfileId(), Out, Error))
	{
		UE_LOG(LogLRAudio, Warning, TEXT("Default audio profile not loaded (%s). Using the C++ defaults."), *Error);
		Out = FLREngineAudioProfile().Sanitized();
	}
	return false;
}

FName FLREngineAudioProfile::GetDefaultProfileId()
{
	static const FName DefaultProfileId(TEXT("EngineAudio_Default"));
	return DefaultProfileId;
}
