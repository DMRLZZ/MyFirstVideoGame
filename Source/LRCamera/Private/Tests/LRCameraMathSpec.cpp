// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Scene.h"
#include "LRCameraMath.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRCameraMathSpec, "LeftRight.Camera.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRCameraMathSpec)

void FLRCameraMathSpec::Define()
{
	Describe("Speed FOV", [this]()
	{
		It("is monotonic for every curve shape", [this]()
		{
			for (const float Exponent : { 0.2f, 0.5f, 1.f, 1.3f, 3.f, 5.f })
			{
				FLRSpeedFovCurve Curve;
				Curve.Exponent = Exponent;

				float Previous = FLRCameraMath::EvaluateSpeedFov(Curve, 0.f);
				bool bMonotonic = true;
				for (float Speed = 1.f; Speed <= 450.f; Speed += 1.f)
				{
					const float Fov = FLRCameraMath::EvaluateSpeedFov(Curve, Speed);
					bMonotonic &= Fov >= Previous;
					Previous = Fov;
				}
				TestTrue(*FString::Printf(TEXT("Monotonic with exponent %.1f"), Exponent), bMonotonic);
			}
		});

		It("reaches the end points", [this]()
		{
			const FLRSpeedFovCurve Curve;
			TestEqual(TEXT("Standing"), FLRCameraMath::EvaluateSpeedFov(Curve, 0.f), Curve.MinFovDeg);
			TestEqual(TEXT("At the start speed"), FLRCameraMath::EvaluateSpeedFov(Curve, Curve.StartSpeedKmh), Curve.MinFovDeg);
			TestEqual(TEXT("At the full speed"), FLRCameraMath::EvaluateSpeedFov(Curve, Curve.FullSpeedKmh), Curve.MaxFovDeg, 1.e-3f);
			TestEqual(TEXT("Beyond the full speed"), FLRCameraMath::EvaluateSpeedFov(Curve, 900.f), Curve.MaxFovDeg, 1.e-3f);
		});

		It("uses the magnitude of the speed", [this]()
		{
			const FLRSpeedFovCurve Curve;
			TestEqual(TEXT("Reverse"), FLRCameraMath::EvaluateSpeedFov(Curve, -200.f), FLRCameraMath::EvaluateSpeedFov(Curve, 200.f));
		});

		It("survives degenerate curves", [this]()
		{
			FLRSpeedFovCurve Inverted;
			Inverted.MinFovDeg = 90.f;
			Inverted.MaxFovDeg = 60.f;
			TestEqual(TEXT("Max below min stays at min"), FLRCameraMath::EvaluateSpeedFov(Inverted, 300.f), 90.f);

			FLRSpeedFovCurve Step;
			Step.StartSpeedKmh = 100.f;
			Step.FullSpeedKmh = 100.f;
			TestEqual(TEXT("Below the step"), FLRCameraMath::EvaluateSpeedFov(Step, 50.f), Step.MinFovDeg);
			TestEqual(TEXT("Above the step"), FLRCameraMath::EvaluateSpeedFov(Step, 150.f), Step.MaxFovDeg);
		});

		It("keeps the cockpit FOV inside its range", [this]()
		{
			FLRCockpitCameraParams Params;
			float Previous = FLRCameraMath::EvaluateCockpitFov(Params, 0.f);
			TestEqual(TEXT("Base FOV at low speed"), Previous, Params.FovDeg);
			bool bMonotonic = true;
			for (float Speed = 1.f; Speed <= 450.f; Speed += 1.f)
			{
				const float Fov = FLRCameraMath::EvaluateCockpitFov(Params, Speed);
				bMonotonic &= Fov >= Previous;
				Previous = Fov;
			}
			TestTrue(TEXT("Monotonic"), bMonotonic);
			TestEqual(TEXT("Extra at top speed"), Previous, Params.FovDeg + Params.SpeedFovExtraDeg, 1.e-3f);

			Params.FovDeg = 95.f;
			TestEqual(TEXT("Clamped to the maximum"), FLRCameraMath::EvaluateCockpitFov(Params, 400.f), 95.f);
			Params.FovDeg = 10.f;
			TestEqual(TEXT("Clamped to the minimum"), FLRCameraMath::EvaluateCockpitFov(Params, 0.f), 60.f);
		});
	});

	Describe("Shake", [this]()
	{
		It("is deterministic", [this]()
		{
			const FLRCameraShakeParams Params;
			bool bIdentical = true;
			for (int32 Step = 0; Step < 500; ++Step)
			{
				const double Time = Step * 0.013;
				const FLRCameraShakeSample A = FLRCameraMath::EvaluateShake(Params, Time, 1.f);
				const FLRCameraShakeSample B = FLRCameraMath::EvaluateShake(Params, Time, 1.f);
				bIdentical &= A.LocationCm == B.LocationCm && A.Rotation == B.Rotation;
			}
			TestTrue(TEXT("Same input, same sample"), bIdentical);
		});

		It("actually moves over time, with decorrelated channels", [this]()
		{
			const FLRCameraShakeParams Params;
			double MaxZ = 0.0;
			int32 NumDifferentChannels = 0;
			for (int32 Step = 0; Step < 500; ++Step)
			{
				const FLRCameraShakeSample Sample = FLRCameraMath::EvaluateShake(Params, Step * 0.013, 1.f);
				MaxZ = FMath::Max(MaxZ, FMath::Abs(Sample.LocationCm.Z));
				NumDifferentChannels += FMath::Abs(Sample.Rotation.Pitch - Sample.Rotation.Roll) > 1.e-3 ? 1 : 0;
			}
			TestTrue(TEXT("Moves"), MaxZ > 0.01);
			TestTrue(TEXT("Channels differ"), NumDifferentChannels > 250);
		});

		It("scales linearly and is silent at scale zero", [this]()
		{
			const FLRCameraShakeParams Params;
			const FLRCameraShakeSample One = FLRCameraMath::EvaluateShake(Params, 3.21, 1.f);
			const FLRCameraShakeSample Two = FLRCameraMath::EvaluateShake(Params, 3.21, 2.f);
			TestEqual(TEXT("Z"), Two.LocationCm.Z, One.LocationCm.Z * 2.0, 1.e-3);
			TestEqual(TEXT("Pitch"), Two.Rotation.Pitch, One.Rotation.Pitch * 2.0, 1.e-3);

			const FLRCameraShakeSample Zero = FLRCameraMath::EvaluateShake(Params, 3.21, 0.f);
			TestTrue(TEXT("Silent location"), Zero.LocationCm.IsZero());
			TestTrue(TEXT("Silent rotation"), Zero.Rotation.IsZero());
		});

		It("stays within its amplitudes, also after hours of play", [this]()
		{
			const FLRCameraShakeParams Params;
			const double MaxZ = Params.HighLocationAmplitudeCm + Params.LowLocationAmplitudeCm;
			bool bBounded = true;
			for (int32 Step = 0; Step < 1000; ++Step)
			{
				const FLRCameraShakeSample Sample = FLRCameraMath::EvaluateShake(Params, 36000.0 + Step * 0.0071, 1.f);
				bBounded &= !Sample.LocationCm.ContainsNaN() && FMath::Abs(Sample.LocationCm.Z) <= MaxZ * 1.01;
			}
			TestTrue(TEXT("Bounded"), bBounded);
		});

		It("grows with speed and roughness, up to its clamp", [this]()
		{
			const FLRCameraShakeParams Params;
			TestEqual(TEXT("Idle"), FLRCameraMath::ComputeShakeScale(Params, 0.f, 1.f, 1.f), Params.IdleScale);
			TestTrue(TEXT("Faster shakes more"),
				FLRCameraMath::ComputeShakeScale(Params, 200.f, 1.f, 1.f) > FLRCameraMath::ComputeShakeScale(Params, 100.f, 1.f, 1.f));
			TestEqual(TEXT("Clamped"), FLRCameraMath::ComputeShakeScale(Params, 5000.f, 1.f, 1.f), Params.MaxScale);
			TestEqual(TEXT("Roughness multiplies"),
				FLRCameraMath::ComputeShakeScale(Params, 100.f, 2.5f, 1.f), FLRCameraMath::ComputeShakeScale(Params, 100.f, 1.f, 1.f) * 2.5f, 1.e-4f);
			TestEqual(TEXT("Accessibility silences it"), FLRCameraMath::ComputeShakeScale(Params, 200.f, 1.f, 0.f), 0.f);
			TestEqual(TEXT("A perfect road silences it"), FLRCameraMath::ComputeShakeScale(Params, 200.f, 0.f, 1.f), 0.f);
		});
	});

	Describe("Look-ahead", [this]()
	{
		It("turns toward the target lane", [this]()
		{
			const FLRLookAheadParams Params;
			TestTrue(TEXT("Right lane"), FLRCameraMath::ComputeLookAheadYawDeg(Params, 3.7f, 50.f, 0.f) > 0.f);
			TestTrue(TEXT("Left lane"), FLRCameraMath::ComputeLookAheadYawDeg(Params, -3.7f, 50.f, 0.f) < 0.f);
			TestEqual(TEXT("Centered"), FLRCameraMath::ComputeLookAheadYawDeg(Params, 0.f, 50.f, 0.f), 0.f);
		});

		It("turns into the curve", [this]()
		{
			const FLRLookAheadParams Params;
			TestTrue(TEXT("Right curve"), FLRCameraMath::ComputeLookAheadYawDeg(Params, 0.f, 50.f, 0.002f) > 0.f);
			TestTrue(TEXT("Left curve"), FLRCameraMath::ComputeLookAheadYawDeg(Params, 0.f, 50.f, -0.002f) < 0.f);
		});

		It("is smaller at high speed for the same lane error", [this]()
		{
			const FLRLookAheadParams Params;
			const float Slow = FLRCameraMath::ComputeLookAheadYawDeg(Params, 3.7f, 40.f, 0.f);
			const float Fast = FLRCameraMath::ComputeLookAheadYawDeg(Params, 3.7f, 90.f, 0.f);
			TestTrue(TEXT("Farther point, smaller angle"), Fast < Slow);
		});

		It("respects the limits", [this]()
		{
			FLRLookAheadParams Params;
			Params.MaxYawDeg = 2.f;
			TestEqual(TEXT("Clamped right"), FLRCameraMath::ComputeLookAheadYawDeg(Params, 100.f, 1.f, 0.f), 2.f);
			TestEqual(TEXT("Clamped left"), FLRCameraMath::ComputeLookAheadYawDeg(Params, -100.f, 1.f, 0.f), -2.f);
			Params.MaxYawDeg = 0.f;
			TestEqual(TEXT("Disabled"), FLRCameraMath::ComputeLookAheadYawDeg(Params, 100.f, 1.f, 0.01f), 0.f);
		});
	});

	Describe("Horizon", [this]()
	{
		It("removes the requested fraction of pitch and roll, and keeps the yaw", [this]()
		{
			const FRotator Body(10.0, 123.0, -6.0);

			const FRotator Rigid = FLRCameraMath::StabilizeHorizon(Body, 0.f);
			TestTrue(TEXT("0 keeps the body rotation"), Rigid.Equals(Body, 1.e-6));

			const FRotator Locked = FLRCameraMath::StabilizeHorizon(Body, 1.f);
			TestTrue(TEXT("1 locks the horizon"), Locked.Equals(FRotator(0.0, 123.0, 0.0), 1.e-6));

			const FRotator Half = FLRCameraMath::StabilizeHorizon(Body, 0.5f);
			TestTrue(TEXT("0.5 halves"), Half.Equals(FRotator(5.0, 123.0, -3.0), 1.e-6));
		});

		It("treats 350 degrees as -10", [this]()
		{
			const FRotator Half = FLRCameraMath::StabilizeHorizon(FRotator(350.0, 0.0, 0.0), 0.5f);
			TestEqual(TEXT("Pitch"), Half.Pitch, -5.0, 1.e-6);
		});
	});

	Describe("Modes", [this]()
	{
		It("cycles in the agreed order and comes back", [this]()
		{
			const ELRCameraMode Expected[] = {
				ELRCameraMode::ChaseNear, ELRCameraMode::Hood, ELRCameraMode::Cockpit,
				ELRCameraMode::Bumper, ELRCameraMode::Viral, ELRCameraMode::ChaseFar };

			ELRCameraMode Mode = ELRCameraMode::ChaseFar;
			for (const ELRCameraMode Next : Expected)
			{
				Mode = FLRCameraMath::GetNextCameraMode(Mode);
				TestTrue(TEXT("Next mode"), Mode == Next);
			}
		});

		It("knows which modes are interior", [this]()
		{
			TestTrue(TEXT("Cockpit"), FLRCameraMath::IsInteriorMode(ELRCameraMode::Cockpit, false));
			TestTrue(TEXT("Interior viral preset"), FLRCameraMath::IsInteriorMode(ELRCameraMode::Viral, true));
			TestFalse(TEXT("Exterior viral preset"), FLRCameraMath::IsInteriorMode(ELRCameraMode::Viral, false));
			TestFalse(TEXT("Chase ignores the preset"), FLRCameraMath::IsInteriorMode(ELRCameraMode::ChaseFar, true));
			TestFalse(TEXT("Hood"), FLRCameraMath::IsInteriorMode(ELRCameraMode::Hood, false));
		});
	});

	Describe("Viral presets", [this]()
	{
		It("parses preset names", [this]()
		{
			ELRViralPreset Preset = ELRViralPreset::GoPro;
			TestTrue(TEXT("Phone"), FLRCameraMath::ParseViralPreset(TEXT("Phone"), Preset));
			TestTrue(TEXT("Phone value"), Preset == ELRViralPreset::Phone);
			TestTrue(TEXT("Case-insensitive"), FLRCameraMath::ParseViralPreset(TEXT("  dashCAM "), Preset));
			TestTrue(TEXT("Dashcam value"), Preset == ELRViralPreset::Dashcam);
			TestFalse(TEXT("Unknown"), FLRCameraMath::ParseViralPreset(TEXT("Drone"), Preset));
			TestTrue(TEXT("Unknown keeps the value"), Preset == ELRViralPreset::Dashcam);
		});

		It("round-trips preset names", [this]()
		{
			for (const ELRViralPreset Preset : { ELRViralPreset::GoPro, ELRViralPreset::Dashcam, ELRViralPreset::Phone })
			{
				ELRViralPreset Parsed = ELRViralPreset::GoPro;
				TestTrue(TEXT("Parses"), FLRCameraMath::ParseViralPreset(FLRCameraMath::GetViralPresetName(Preset), Parsed));
				TestTrue(TEXT("Same preset"), Parsed == Preset);
			}
		});

		It("writes the post-process overrides and skips negative values", [this]()
		{
			FLRViralPostProcessParams Params;
			Params.VignetteIntensity = 0.7f;
			Params.Contrast = 1.2f;
			Params.Sharpen = -1.f;

			FPostProcessSettings Settings;
			FLRCameraMath::ApplyViralPostProcess(Params, Settings);

			TestTrue(TEXT("Vignette override"), Settings.bOverride_VignetteIntensity != 0);
			TestEqual(TEXT("Vignette"), Settings.VignetteIntensity, 0.7f);
			TestTrue(TEXT("Grain override"), Settings.bOverride_FilmGrainIntensity != 0);
			TestTrue(TEXT("Fringe override"), Settings.bOverride_SceneFringeIntensity != 0);
			TestTrue(TEXT("Exposure override"), Settings.bOverride_AutoExposureSpeedUp != 0 && Settings.bOverride_AutoExposureSpeedDown != 0);
			TestTrue(TEXT("Motion blur override"), Settings.bOverride_MotionBlurAmount != 0 && Settings.bOverride_MotionBlurMax != 0);
			TestTrue(TEXT("Bloom override"), Settings.bOverride_BloomIntensity != 0);
			TestTrue(TEXT("Contrast override"), Settings.bOverride_ColorContrast != 0);
			TestEqual(TEXT("Contrast"), Settings.ColorContrast.X, 1.2, 1.e-5);
			TestFalse(TEXT("Negative sharpen is skipped"), Settings.bOverride_Sharpen != 0);
			TestFalse(TEXT("Negative saturation is skipped"), Settings.bOverride_ColorSaturation != 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
