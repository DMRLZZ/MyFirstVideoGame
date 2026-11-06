// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRCameraTypes.h"
#include "LRDataLoader.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRCameraConfigSpec, "LeftRight.Camera.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRCameraConfigSpec)

void FLRCameraConfigSpec::Define()
{
	Describe("Data file", [this]()
	{
		It("loads Data/Camera/Cameras.json", [this]()
		{
			FLRCameraConfig Config;
			FString Error;
			const bool bLoaded = FLRDataLoader::Load(FLRCameraConfig::GetDefaultDataPath(), Config, &Error);
			TestTrue(*FString::Printf(TEXT("Load: %s"), *Error), bLoaded);

			TestTrue(TEXT("Default mode"), Config.DefaultMode == ELRCameraMode::ChaseFar);
			TestEqual(TEXT("Cockpit minimum FOV"), Config.Cockpit.MinFovDeg, 60.f);
			TestEqual(TEXT("Cockpit maximum FOV"), Config.Cockpit.MaxFovDeg, 95.f);
			TestTrue(TEXT("Artifacts on by default"), Config.Viral.bViralArtifactsEnabled);

			TestTrue(TEXT("GoPro on the hood"), Config.Viral.GoPro.MountAnchor == ELRCameraMountAnchor::Hood);
			TestFalse(TEXT("GoPro is exterior"), Config.Viral.GoPro.bInterior);
			TestTrue(TEXT("GoPro is ultra wide"), Config.Viral.GoPro.FovDeg >= 120.f && Config.Viral.GoPro.FovDeg <= 150.f);
			TestTrue(TEXT("Dashcam behind the mirror"), Config.Viral.Dashcam.MountAnchor == ELRCameraMountAnchor::ViralMount);
			TestTrue(TEXT("Dashcam is interior"), Config.Viral.Dashcam.bInterior);
			TestTrue(TEXT("Phone is interior"), Config.Viral.Phone.bInterior);
			TestEqual(TEXT("Phone mount offset"), Config.Viral.Phone.MountOffsetCm.Y, 72.0, 1.e-3);
		});

		It("is already sane", [this]()
		{
			FLRCameraConfig Config;
			FLRDataLoader::Load(FLRCameraConfig::GetDefaultDataPath(), Config);

			FLRCameraConfig Sanitized = Config;
			Sanitized.Sanitize();
			TestEqual(TEXT("Cockpit FOV"), Sanitized.Cockpit.FovDeg, Config.Cockpit.FovDeg);
			TestEqual(TEXT("Far maximum FOV"), Sanitized.ChaseFar.Fov.MaxFovDeg, Config.ChaseFar.Fov.MaxFovDeg);
			TestEqual(TEXT("Near maximum FOV"), Sanitized.ChaseNear.Fov.MaxFovDeg, Config.ChaseNear.Fov.MaxFovDeg);
			TestEqual(TEXT("Hood full speed"), Sanitized.Hood.Fov.FullSpeedKmh, Config.Hood.Fov.FullSpeedKmh);
			TestEqual(TEXT("Bumper full speed"), Sanitized.Bumper.Fov.FullSpeedKmh, Config.Bumper.Fov.FullSpeedKmh);
		});

		It("matches the C++ defaults, so a missing file changes nothing", [this]()
		{
			const FLRCameraConfig Defaults;
			FLRCameraConfig Config;
			FLRDataLoader::Load(FLRCameraConfig::GetDefaultDataPath(), Config);

			TestEqual(TEXT("Near yaw lag"), Config.ChaseNear.YawLagSeconds, Defaults.ChaseNear.YawLagSeconds);
			TestEqual(TEXT("Bumper shake"), Config.Bumper.Shake.HighFrequencyHz, Defaults.Bumper.Shake.HighFrequencyHz);
			TestEqual(TEXT("Cockpit look-ahead"), Config.Cockpit.LookAhead.MaxYawDeg, Defaults.Cockpit.LookAhead.MaxYawDeg);
			TestEqual(TEXT("GoPro FOV"), Config.Viral.GoPro.FovDeg, Defaults.Viral.GoPro.FovDeg);
			TestEqual(TEXT("Phone hand motion"), Config.Viral.Phone.Shake.LowRotationAmplitudeDeg, Defaults.Viral.Phone.Shake.LowRotationAmplitudeDeg);
			TestEqual(TEXT("Dashcam bloom"), Config.Viral.Dashcam.PostProcess.BloomIntensity, Defaults.Viral.Dashcam.PostProcess.BloomIntensity);
		});
	});

	Describe("Sanitize", [this]()
	{
		It("repairs broken values", [this]()
		{
			FLRCameraConfig Config;
			Config.ChaseFar.Fov.MinFovDeg = 90.f;
			Config.ChaseFar.Fov.MaxFovDeg = 60.f;
			Config.ChaseFar.Fov.StartSpeedKmh = 300.f;
			Config.ChaseFar.Fov.FullSpeedKmh = 100.f;
			Config.Cockpit.FovDeg = 200.f;
			Config.Cockpit.HorizonStabilization = 4.f;
			Config.Cockpit.HeadModel.Intensity = -1.f;
			Config.GlobalShakeScale = -2.f;
			Config.RoughnessLagSeconds = -1.f;
			Config.Viral.Phone.FovDeg = 1.f;
			Config.Sanitize();

			TestEqual(TEXT("FOV order"), Config.ChaseFar.Fov.MaxFovDeg, 90.f);
			TestTrue(TEXT("Speed order"), Config.ChaseFar.Fov.FullSpeedKmh > Config.ChaseFar.Fov.StartSpeedKmh);
			TestEqual(TEXT("Cockpit FOV inside its range"), Config.Cockpit.FovDeg, Config.Cockpit.MaxFovDeg);
			TestEqual(TEXT("Stabilization"), Config.Cockpit.HorizonStabilization, 1.f);
			TestEqual(TEXT("Head intensity"), Config.Cockpit.HeadModel.Intensity, 0.f);
			TestEqual(TEXT("Shake scale"), Config.GlobalShakeScale, 0.f);
			TestEqual(TEXT("Roughness lag"), Config.RoughnessLagSeconds, 0.f);
			TestEqual(TEXT("Preset FOV"), Config.Viral.Phone.FovDeg, 30.f);
		});
	});

	Describe("Presets", [this]()
	{
		It("are selected by enum", [this]()
		{
			const FLRViralCameraParams Viral = FLRCameraConfig().Viral;
			TestEqual(TEXT("GoPro"), Viral.GetPreset(ELRViralPreset::GoPro).DisplayName, FString(TEXT("GoPro")));
			TestEqual(TEXT("Dashcam"), Viral.GetPreset(ELRViralPreset::Dashcam).DisplayName, FString(TEXT("Dashcam")));
			TestEqual(TEXT("Phone"), Viral.GetPreset(ELRViralPreset::Phone).DisplayName, FString(TEXT("Phone")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
