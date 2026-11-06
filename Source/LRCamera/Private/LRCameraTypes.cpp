// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRCameraTypes.h"

namespace LRCameraTypesPrivate
{
	void SanitizeFov(FLRSpeedFovCurve& Fov)
	{
		Fov.MinFovDeg = FMath::Clamp(Fov.MinFovDeg, 20.f, 170.f);
		Fov.MaxFovDeg = FMath::Clamp(Fov.MaxFovDeg, Fov.MinFovDeg, 170.f);
		Fov.StartSpeedKmh = FMath::Max(Fov.StartSpeedKmh, 0.f);
		Fov.FullSpeedKmh = FMath::Max(Fov.FullSpeedKmh, Fov.StartSpeedKmh + 1.f);
		Fov.Exponent = FMath::Clamp(Fov.Exponent, 0.2f, 5.f);
	}

	void SanitizeShake(FLRCameraShakeParams& Shake)
	{
		Shake.HighFrequencyHz = FMath::Max(Shake.HighFrequencyHz, 0.f);
		Shake.HighLocationAmplitudeCm = FMath::Max(Shake.HighLocationAmplitudeCm, 0.f);
		Shake.HighRotationAmplitudeDeg = FMath::Max(Shake.HighRotationAmplitudeDeg, 0.f);
		Shake.LowFrequencyHz = FMath::Max(Shake.LowFrequencyHz, 0.f);
		Shake.LowLocationAmplitudeCm = FMath::Max(Shake.LowLocationAmplitudeCm, 0.f);
		Shake.LowRotationAmplitudeDeg = FMath::Max(Shake.LowRotationAmplitudeDeg, 0.f);
		Shake.ReferenceSpeedKmh = FMath::Max(Shake.ReferenceSpeedKmh, 1.f);
		Shake.SpeedExponent = FMath::Max(Shake.SpeedExponent, 0.f);
		Shake.IdleScale = FMath::Max(Shake.IdleScale, 0.f);
		Shake.MaxScale = FMath::Max(Shake.MaxScale, 0.f);
	}

	void SanitizeLookAhead(FLRLookAheadParams& LookAhead)
	{
		LookAhead.LookAheadSeconds = FMath::Max(LookAhead.LookAheadSeconds, 0.f);
		LookAhead.MinDistanceMeters = FMath::Max(LookAhead.MinDistanceMeters, 1.f);
		LookAhead.MaxYawDeg = FMath::Clamp(LookAhead.MaxYawDeg, 0.f, 45.f);
		LookAhead.LagSeconds = FMath::Max(LookAhead.LagSeconds, 0.f);
		LookAhead.CurvatureScale = FMath::Max(LookAhead.CurvatureScale, 0.f);
	}

	void SanitizeHead(FLRHeadModelParams& Head)
	{
		Head.NaturalFrequencyHz = FMath::Clamp(Head.NaturalFrequencyHz, 0.2f, 10.f);
		Head.DampingRatio = FMath::Clamp(Head.DampingRatio, 0.3f, 3.f);
		Head.LongitudinalCmPerG = FMath::Max(Head.LongitudinalCmPerG, 0.f);
		Head.LateralCmPerG = FMath::Max(Head.LateralCmPerG, 0.f);
		Head.VerticalCmPerG = FMath::Max(Head.VerticalCmPerG, 0.f);
		Head.RollDegPerG = FMath::Max(Head.RollDegPerG, 0.f);
		Head.PitchDegPerG = FMath::Max(Head.PitchDegPerG, 0.f);
		Head.MaxOffsetCm = FMath::Max(Head.MaxOffsetCm, 0.f);
		Head.MaxRollDeg = FMath::Max(Head.MaxRollDeg, 0.f);
		Head.MaxPitchDeg = FMath::Max(Head.MaxPitchDeg, 0.f);
		Head.MaxInputG = FMath::Max(Head.MaxInputG, 0.1f);
		Head.Intensity = FMath::Clamp(Head.Intensity, 0.f, 3.f);
	}

	void SanitizeChase(FLRChaseCameraParams& Chase)
	{
		Chase.DistanceScale = FMath::Clamp(Chase.DistanceScale, 0.2f, 5.f);
		Chase.HeightScale = FMath::Clamp(Chase.HeightScale, 0.f, 5.f);
		Chase.YawLagSeconds = FMath::Max(Chase.YawLagSeconds, 0.f);
		Chase.HeightLagSeconds = FMath::Max(Chase.HeightLagSeconds, 0.f);
		Chase.DistanceLagSeconds = FMath::Max(Chase.DistanceLagSeconds, 0.f);
		Chase.MaxAccelDistanceCm = FMath::Max(Chase.MaxAccelDistanceCm, 0.f);
		Chase.SpeedDistanceCmPer100Kmh = FMath::Max(Chase.SpeedDistanceCmPer100Kmh, 0.f);
		Chase.LookAtAheadCm = FMath::Max(Chase.LookAtAheadCm, 0.f);
		Chase.LookAtHeightScale = FMath::Max(Chase.LookAtHeightScale, 0.f);
		Chase.MinGroundClearanceCm = FMath::Max(Chase.MinGroundClearanceCm, 0.f);
		SanitizeFov(Chase.Fov);
		SanitizeShake(Chase.Shake);
		SanitizeLookAhead(Chase.LookAhead);
	}

	void SanitizeMounted(FLRMountedCameraParams& Mounted)
	{
		Mounted.PitchOffsetDeg = FMath::Clamp(Mounted.PitchOffsetDeg, -45.f, 45.f);
		SanitizeFov(Mounted.Fov);
		SanitizeShake(Mounted.Shake);
	}

	void SanitizeViralPreset(FLRViralPresetParams& Preset)
	{
		Preset.MountPitchDeg = FMath::Clamp(Preset.MountPitchDeg, -45.f, 45.f);
		Preset.FovDeg = FMath::Clamp(Preset.FovDeg, 30.f, 170.f);
		Preset.HorizonStabilization = FMath::Clamp(Preset.HorizonStabilization, 0.f, 1.f);
		SanitizeShake(Preset.Shake);
	}
}

const FLRViralPresetParams& FLRViralCameraParams::GetPreset(ELRViralPreset Preset) const
{
	switch (Preset)
	{
	case ELRViralPreset::Dashcam:
		return Dashcam;
	case ELRViralPreset::Phone:
		return Phone;
	case ELRViralPreset::GoPro:
	default:
		return GoPro;
	}
}

FLRCameraConfig::FLRCameraConfig()
{
	// Near chase: tighter, slightly wider lens, a bit more lag for drama.
	ChaseNear.YawLagSeconds = 0.14f;
	ChaseNear.LookAtAheadCm = 900.f;
	ChaseNear.Fov.MinFovDeg = 74.f;
	ChaseNear.Fov.MaxFovDeg = 90.f;

	// Hood: rigid, mid FOV, light high-frequency vibration.
	Hood.Fov.MinFovDeg = 75.f;
	Hood.Fov.MaxFovDeg = 88.f;
	Hood.Shake.HighLocationAmplitudeCm = 0.15f;
	Hood.Shake.HighRotationAmplitudeDeg = 0.08f;
	Hood.Shake.LowLocationAmplitudeCm = 0.2f;
	Hood.Shake.LowRotationAmplitudeDeg = 0.05f;

	// Bumper: low and wide, stronger vibration (close to the road).
	Bumper.Fov.MinFovDeg = 80.f;
	Bumper.Fov.MaxFovDeg = 95.f;
	Bumper.Shake.HighFrequencyHz = 22.f;
	Bumper.Shake.HighLocationAmplitudeCm = 0.3f;
	Bumper.Shake.HighRotationAmplitudeDeg = 0.12f;
	Bumper.Shake.LowLocationAmplitudeCm = 0.25f;
	Bumper.Shake.LowRotationAmplitudeDeg = 0.05f;

	// Cockpit: subtle vibration, the head model does the rest.
	Cockpit.Shake.HighLocationAmplitudeCm = 0.08f;
	Cockpit.Shake.HighRotationAmplitudeDeg = 0.04f;
	Cockpit.Shake.LowLocationAmplitudeCm = 0.3f;
	Cockpit.Shake.LowRotationAmplitudeDeg = 0.08f;
	Cockpit.LookAhead.MaxYawDeg = 8.f;

	// GoPro on a hood suction mount.
	Viral.GoPro.DisplayName = TEXT("GoPro");
	Viral.GoPro.MountAnchor = ELRCameraMountAnchor::Hood;
	Viral.GoPro.MountOffsetCm = FVector(10.0, 0.0, 8.0);
	Viral.GoPro.MountPitchDeg = -3.f;
	Viral.GoPro.FovDeg = 125.f;
	Viral.GoPro.bInterior = false;
	Viral.GoPro.Shake.HighFrequencyHz = 24.f;
	Viral.GoPro.Shake.HighLocationAmplitudeCm = 0.35f;
	Viral.GoPro.Shake.HighRotationAmplitudeDeg = 0.18f;
	Viral.GoPro.PostProcess.Sharpen = 1.5f;
	Viral.GoPro.PostProcess.Contrast = 1.15f;
	Viral.GoPro.PostProcess.SceneFringeIntensity = 2.f;

	// Dashcam behind the rear-view mirror.
	Viral.Dashcam.DisplayName = TEXT("Dashcam");
	Viral.Dashcam.MountAnchor = ELRCameraMountAnchor::ViralMount;
	Viral.Dashcam.MountPitchDeg = -2.f;
	Viral.Dashcam.FovDeg = 100.f;
	Viral.Dashcam.bInterior = true;
	Viral.Dashcam.PostProcess.BloomIntensity = 2.f;
	Viral.Dashcam.PostProcess.Saturation = 0.85f;
	Viral.Dashcam.PostProcess.Contrast = 1.2f;
	Viral.Dashcam.PostProcess.MotionBlurAmount = 0.6f;

	// Phone held by the passenger.
	Viral.Phone.DisplayName = TEXT("Phone");
	Viral.Phone.MountAnchor = ELRCameraMountAnchor::Cockpit;
	Viral.Phone.MountOffsetCm = FVector(15.0, 72.0, -6.0);
	Viral.Phone.FovDeg = 78.f;
	Viral.Phone.bInterior = true;
	Viral.Phone.HorizonStabilization = 0.5f;
	Viral.Phone.Shake.HighLocationAmplitudeCm = 0.1f;
	Viral.Phone.Shake.HighRotationAmplitudeDeg = 0.05f;
	Viral.Phone.Shake.LowFrequencyHz = 0.7f;
	Viral.Phone.Shake.LowLocationAmplitudeCm = 2.5f;
	Viral.Phone.Shake.LowRotationAmplitudeDeg = 1.2f;
	Viral.Phone.Shake.IdleScale = 0.6f;
	Viral.Phone.Shake.SpeedExponent = 0.5f;
	Viral.Phone.PostProcess.AutoExposureSpeedUp = 0.3f;
	Viral.Phone.PostProcess.AutoExposureSpeedDown = 0.25f;
	Viral.Phone.PostProcess.SceneFringeIntensity = 0.8f;
}

void FLRCameraConfig::Sanitize()
{
	using namespace LRCameraTypesPrivate;

	RoadRoughness = FMath::Max(RoadRoughness, 0.f);
	ShoulderRoughness = FMath::Max(ShoulderRoughness, 0.f);
	RoughnessLagSeconds = FMath::Max(RoughnessLagSeconds, 0.f);
	GlobalShakeScale = FMath::Clamp(GlobalShakeScale, 0.f, 3.f);
	InteriorNearClipCm = FMath::Clamp(InteriorNearClipCm, 0.5f, 50.f);
	TeleportSnapDistanceCm = FMath::Max(TeleportSnapDistanceCm, 100.f);

	SanitizeChase(ChaseFar);
	SanitizeChase(ChaseNear);
	SanitizeMounted(Hood);
	SanitizeMounted(Bumper);

	Cockpit.MinFovDeg = FMath::Clamp(Cockpit.MinFovDeg, 30.f, 120.f);
	Cockpit.MaxFovDeg = FMath::Clamp(Cockpit.MaxFovDeg, Cockpit.MinFovDeg, 120.f);
	Cockpit.FovDeg = FMath::Clamp(Cockpit.FovDeg, Cockpit.MinFovDeg, Cockpit.MaxFovDeg);
	Cockpit.SpeedFovExtraDeg = FMath::Max(Cockpit.SpeedFovExtraDeg, 0.f);
	Cockpit.SpeedFovStartKmh = FMath::Max(Cockpit.SpeedFovStartKmh, 0.f);
	Cockpit.SpeedFovFullKmh = FMath::Max(Cockpit.SpeedFovFullKmh, Cockpit.SpeedFovStartKmh + 1.f);
	Cockpit.HorizonStabilization = FMath::Clamp(Cockpit.HorizonStabilization, 0.f, 1.f);
	SanitizeHead(Cockpit.HeadModel);
	SanitizeShake(Cockpit.Shake);
	SanitizeLookAhead(Cockpit.LookAhead);

	SanitizeViralPreset(Viral.GoPro);
	SanitizeViralPreset(Viral.Dashcam);
	SanitizeViralPreset(Viral.Phone);
}
