// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRCameraMath.h"

#include "Engine/Scene.h"

namespace LRCameraMathPrivate
{
	/** Decorrelated phase offsets of the shake channels (non-integer so channels never share lattice points). */
	constexpr double ChannelOffsets[6] = { 0.0, 37.31, 71.97, 113.53, 157.29, 199.71 };

	/** Extra offset of the low band relative to the high band. */
	constexpr double LowBandOffset = 23.17;

	float Noise(double TimeSeconds, double FrequencyHz, double Offset)
	{
		const double Phase = FMath::Fmod(TimeSeconds * FrequencyHz + Offset, FLRCameraMath::PerlinPeriod);
		return FMath::PerlinNoise1D(static_cast<float>(Phase));
	}

	/** Two-band noise of one channel, already weighted by the band amplitudes. */
	float Channel(const FLRCameraShakeParams& Params, double TimeSeconds, int32 ChannelIndex, float HighAmplitude, float LowAmplitude)
	{
		const double Offset = ChannelOffsets[ChannelIndex];
		float Result = 0.f;
		if (HighAmplitude > 0.f && Params.HighFrequencyHz > 0.f)
		{
			Result += HighAmplitude * Noise(TimeSeconds, Params.HighFrequencyHz, Offset);
		}
		if (LowAmplitude > 0.f && Params.LowFrequencyHz > 0.f)
		{
			Result += LowAmplitude * Noise(TimeSeconds, Params.LowFrequencyHz, Offset + LowBandOffset);
		}
		return Result;
	}
}

float FLRCameraMath::EvaluateSpeedAlpha(float SpeedKmh, float StartKmh, float FullKmh, float Exponent)
{
	const float Speed = FMath::Abs(SpeedKmh);
	const float Range = FullKmh - StartKmh;
	if (Range <= UE_KINDA_SMALL_NUMBER)
	{
		return Speed >= StartKmh ? 1.f : 0.f;
	}
	const float Linear = FMath::Clamp((Speed - StartKmh) / Range, 0.f, 1.f);
	const float SafeExponent = FMath::Clamp(Exponent, 0.2f, 5.f);
	return FMath::Pow(Linear, SafeExponent);
}

float FLRCameraMath::EvaluateSpeedFov(const FLRSpeedFovCurve& Curve, float SpeedKmh)
{
	const float MinFov = Curve.MinFovDeg;
	const float MaxFov = FMath::Max(Curve.MaxFovDeg, MinFov);
	const float Alpha = EvaluateSpeedAlpha(SpeedKmh, Curve.StartSpeedKmh, Curve.FullSpeedKmh, Curve.Exponent);
	return MinFov + (MaxFov - MinFov) * Alpha;
}

float FLRCameraMath::EvaluateCockpitFov(const FLRCockpitCameraParams& Params, float SpeedKmh)
{
	const float MinFov = Params.MinFovDeg;
	const float MaxFov = FMath::Max(Params.MaxFovDeg, MinFov);
	const float Alpha = EvaluateSpeedAlpha(SpeedKmh, Params.SpeedFovStartKmh, Params.SpeedFovFullKmh, 1.f);
	const float Base = FMath::Clamp(Params.FovDeg, MinFov, MaxFov);
	return FMath::Clamp(Base + FMath::Max(Params.SpeedFovExtraDeg, 0.f) * Alpha, MinFov, MaxFov);
}

float FLRCameraMath::ComputeShakeScale(const FLRCameraShakeParams& Params, float SpeedKmh, float Roughness, float GlobalScale)
{
	const float Reference = FMath::Max(Params.ReferenceSpeedKmh, 1.f);
	const float SpeedRatio = FMath::Abs(SpeedKmh) / Reference;
	const float SpeedTerm = FMath::Pow(SpeedRatio, FMath::Max(Params.SpeedExponent, 0.f));
	const float SpeedScale = FMath::Clamp(Params.IdleScale + SpeedTerm, 0.f, FMath::Max(Params.MaxScale, 0.f));
	return SpeedScale * FMath::Max(Roughness, 0.f) * FMath::Max(GlobalScale, 0.f);
}

FLRCameraShakeSample FLRCameraMath::EvaluateShake(const FLRCameraShakeParams& Params, double TimeSeconds, float Scale)
{
	using namespace LRCameraMathPrivate;

	FLRCameraShakeSample Sample;
	if (Scale <= 0.f)
	{
		return Sample;
	}

	const float HighLoc = Params.HighLocationAmplitudeCm * Scale;
	const float LowLoc = Params.LowLocationAmplitudeCm * Scale;
	const float HighRot = Params.HighRotationAmplitudeDeg * Scale;
	const float LowRot = Params.LowRotationAmplitudeDeg * Scale;

	// Vertical motion dominates on a road; lateral and longitudinal are weaker.
	Sample.LocationCm.X = 0.3f * Channel(Params, TimeSeconds, 0, HighLoc, LowLoc);
	Sample.LocationCm.Y = 0.6f * Channel(Params, TimeSeconds, 1, HighLoc, LowLoc);
	Sample.LocationCm.Z = Channel(Params, TimeSeconds, 2, HighLoc, LowLoc);
	Sample.Rotation.Pitch = Channel(Params, TimeSeconds, 3, HighRot, LowRot);
	Sample.Rotation.Yaw = 0.5f * Channel(Params, TimeSeconds, 4, HighRot, LowRot);
	Sample.Rotation.Roll = 0.8f * Channel(Params, TimeSeconds, 5, HighRot, LowRot);
	return Sample;
}

float FLRCameraMath::ComputeLookAheadYawDeg(const FLRLookAheadParams& Params, float LateralErrorMeters, float SpeedMs, float CurvaturePerM)
{
	if (Params.MaxYawDeg <= 0.f)
	{
		return 0.f;
	}

	const float Distance = FMath::Max(FMath::Abs(SpeedMs) * Params.LookAheadSeconds, FMath::Max(Params.MinDistanceMeters, 1.f));
	const float LaneYawRad = FMath::Atan2(LateralErrorMeters, Distance);
	const float CurveYawRad = Params.CurvatureScale * FMath::Atan(CurvaturePerM * Distance * 0.5f);
	const float YawDeg = FMath::RadiansToDegrees(LaneYawRad + CurveYawRad);
	return FMath::Clamp(YawDeg, -Params.MaxYawDeg, Params.MaxYawDeg);
}

FRotator FLRCameraMath::StabilizeHorizon(const FRotator& BodyRotation, float Stabilization)
{
	const double Keep = 1.0 - FMath::Clamp(static_cast<double>(Stabilization), 0.0, 1.0);
	FRotator Result = BodyRotation;
	Result.Pitch = FRotator::NormalizeAxis(BodyRotation.Pitch) * Keep;
	Result.Roll = FRotator::NormalizeAxis(BodyRotation.Roll) * Keep;
	return Result;
}

ELRCameraMode FLRCameraMath::GetNextCameraMode(ELRCameraMode Mode)
{
	switch (Mode)
	{
	case ELRCameraMode::ChaseFar:
		return ELRCameraMode::ChaseNear;
	case ELRCameraMode::ChaseNear:
		return ELRCameraMode::Hood;
	case ELRCameraMode::Hood:
		return ELRCameraMode::Cockpit;
	case ELRCameraMode::Cockpit:
		return ELRCameraMode::Bumper;
	case ELRCameraMode::Bumper:
		return ELRCameraMode::Viral;
	case ELRCameraMode::Viral:
	default:
		return ELRCameraMode::ChaseFar;
	}
}

bool FLRCameraMath::IsInteriorMode(ELRCameraMode Mode, bool bViralPresetInterior)
{
	return Mode == ELRCameraMode::Cockpit || (Mode == ELRCameraMode::Viral && bViralPresetInterior);
}

bool FLRCameraMath::ParseViralPreset(const FString& Text, ELRViralPreset& OutPreset)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	for (ELRViralPreset Preset : { ELRViralPreset::GoPro, ELRViralPreset::Dashcam, ELRViralPreset::Phone })
	{
		if (Trimmed.Equals(GetViralPresetName(Preset), ESearchCase::IgnoreCase))
		{
			OutPreset = Preset;
			return true;
		}
	}
	return false;
}

const TCHAR* FLRCameraMath::GetViralPresetName(ELRViralPreset Preset)
{
	switch (Preset)
	{
	case ELRViralPreset::Dashcam:
		return TEXT("Dashcam");
	case ELRViralPreset::Phone:
		return TEXT("Phone");
	case ELRViralPreset::GoPro:
	default:
		return TEXT("GoPro");
	}
}

void FLRCameraMath::ApplyViralPostProcess(const FLRViralPostProcessParams& Params, FPostProcessSettings& InOutSettings)
{
	if (Params.VignetteIntensity >= 0.f)
	{
		InOutSettings.bOverride_VignetteIntensity = true;
		InOutSettings.VignetteIntensity = Params.VignetteIntensity;
	}
	if (Params.FilmGrainIntensity >= 0.f)
	{
		InOutSettings.bOverride_FilmGrainIntensity = true;
		InOutSettings.FilmGrainIntensity = Params.FilmGrainIntensity;
	}
	if (Params.SceneFringeIntensity >= 0.f)
	{
		InOutSettings.bOverride_SceneFringeIntensity = true;
		InOutSettings.SceneFringeIntensity = Params.SceneFringeIntensity;
	}
	if (Params.ChromaticAberrationStartOffset >= 0.f)
	{
		InOutSettings.bOverride_ChromaticAberrationStartOffset = true;
		InOutSettings.ChromaticAberrationStartOffset = Params.ChromaticAberrationStartOffset;
	}
	if (Params.AutoExposureSpeedUp >= 0.f)
	{
		InOutSettings.bOverride_AutoExposureSpeedUp = true;
		InOutSettings.AutoExposureSpeedUp = Params.AutoExposureSpeedUp;
	}
	if (Params.AutoExposureSpeedDown >= 0.f)
	{
		InOutSettings.bOverride_AutoExposureSpeedDown = true;
		InOutSettings.AutoExposureSpeedDown = Params.AutoExposureSpeedDown;
	}
	if (Params.MotionBlurAmount >= 0.f)
	{
		InOutSettings.bOverride_MotionBlurAmount = true;
		InOutSettings.MotionBlurAmount = Params.MotionBlurAmount;
	}
	if (Params.MotionBlurMax >= 0.f)
	{
		InOutSettings.bOverride_MotionBlurMax = true;
		InOutSettings.MotionBlurMax = Params.MotionBlurMax;
	}
	if (Params.BloomIntensity >= 0.f)
	{
		InOutSettings.bOverride_BloomIntensity = true;
		InOutSettings.BloomIntensity = Params.BloomIntensity;
	}
	if (Params.Sharpen >= 0.f)
	{
		InOutSettings.bOverride_Sharpen = true;
		InOutSettings.Sharpen = Params.Sharpen;
	}
	if (Params.Saturation >= 0.f)
	{
		InOutSettings.bOverride_ColorSaturation = true;
		InOutSettings.ColorSaturation = FVector4(Params.Saturation, Params.Saturation, Params.Saturation, 1.0);
	}
	if (Params.Contrast >= 0.f)
	{
		InOutSettings.bOverride_ColorContrast = true;
		InOutSettings.ColorContrast = FVector4(Params.Contrast, Params.Contrast, Params.Contrast, 1.0);
	}
}
