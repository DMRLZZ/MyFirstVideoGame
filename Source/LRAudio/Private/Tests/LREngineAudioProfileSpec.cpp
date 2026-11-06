// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "DSP/LRVehicleSynth.h"
#include "LRAudioTypes.h"
#include "LRDataLoader.h"
#include "LRVehicleTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

BEGIN_DEFINE_SPEC(FLREngineAudioProfileSpec, "LeftRight.Audio.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLREngineAudioProfileSpec)

void FLREngineAudioProfileSpec::Define()
{
	Describe("Sanitized", [this]()
	{
		It("clamps out-of-range values", [this]()
		{
			FLREngineAudioProfile Profile;
			Profile.CylinderCount = 0;
			Profile.Drive = 100.f;
			Profile.WhooshStartHz = -5.f;
			Profile.MasterGain = 10.f;
			Profile.JointSpacingMeters = -1.f;

			const FLREngineAudioProfile Safe = Profile.Sanitized();
			TestEqual(TEXT("Cylinders"), Safe.CylinderCount, 1);
			TestEqual(TEXT("Drive"), Safe.Drive, 8.f);
			TestEqual(TEXT("Whoosh start"), Safe.WhooshStartHz, 100.f);
			TestEqual(TEXT("Master"), Safe.MasterGain, 2.f);
			TestEqual(TEXT("Joint spacing"), Safe.JointSpacingMeters, 0.f);
		});

		It("adds the default harmonics when empty and truncates long lists", [this]()
		{
			const FLREngineAudioProfile Empty;
			TestEqual(TEXT("Defaults"), Empty.Sanitized().Harmonics.Num(), FLREngineAudioProfile::MakeDefaultHarmonics().Num());
			TestTrue(TEXT("Defaults exist"), FLREngineAudioProfile::MakeDefaultHarmonics().Num() > 0);

			FLREngineAudioProfile Long;
			Long.Harmonics.SetNum(FLREngineAudioProfile::MaxHarmonics + 8);
			TestEqual(TEXT("Truncated"), Long.Sanitized().Harmonics.Num(), FLREngineAudioProfile::MaxHarmonics);
		});

		It("clamps every harmonic", [this]()
		{
			FLREngineAudioProfile Profile;
			FLREngineHarmonic& Harmonic = Profile.Harmonics.AddDefaulted_GetRef();
			Harmonic.Order = 100.f;
			Harmonic.OffLoadGain = -1.f;
			Harmonic.OnLoadGain = 5.f;

			const FLREngineHarmonic& Safe = Profile.Sanitized().Harmonics[0];
			TestEqual(TEXT("Order"), Safe.Order, 16.f);
			TestEqual(TEXT("Off load"), Safe.OffLoadGain, 0.f);
			TestEqual(TEXT("On load"), Safe.OnLoadGain, 1.f);
		});
	});

	Describe("Data files", [this]()
	{
		It("the default profile exists and is valid", [this]()
		{
			FLREngineAudioProfile Profile;
			TestTrue(TEXT("Loaded"), FLREngineAudioProfile::LoadById(FLREngineAudioProfile::GetDefaultProfileId(), Profile));
			TestEqual(TEXT("Id"), Profile.Id, FLREngineAudioProfile::GetDefaultProfileId());
			TestTrue(TEXT("Has harmonics"), Profile.Harmonics.Num() > 0);
		});

		It("an unknown or empty id falls back to the default profile", [this]()
		{
			AddExpectedError(TEXT("Data file not found"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("not loaded"), EAutomationExpectedErrorFlags::Contains, 0);

			FLREngineAudioProfile Unknown;
			TestFalse(TEXT("Unknown id reports false"), FLREngineAudioProfile::LoadById(FName(TEXT("no_such_audio_profile")), Unknown));
			TestEqual(TEXT("Unknown id gets the default"), Unknown.Id, FLREngineAudioProfile::GetDefaultProfileId());

			FLREngineAudioProfile Empty;
			TestFalse(TEXT("No id reports false"), FLREngineAudioProfile::LoadById(NAME_None, Empty));
			TestEqual(TEXT("No id gets the default"), Empty.Id, FLREngineAudioProfile::GetDefaultProfileId());
		});

		It("every profile file loads, is named after its id and needs no clamping", [this]()
		{
			const TArray<FString> Files = FLRDataLoader::ListFiles(TEXT("Audio"));
			TestTrue(TEXT("Files found"), Files.Num() >= 5);

			for (const FString& File : Files)
			{
				FLREngineAudioProfile Profile;
				FString Error;
				const bool bLoaded = FLRDataLoader::Load(File, Profile, &Error);
				if (!TestTrue(*FString::Printf(TEXT("Loads %s: %s"), *File, *Error), bLoaded))
				{
					continue;
				}
				TestEqual(*FString::Printf(TEXT("Id of %s"), *File), Profile.Id.ToString(), FPaths::GetBaseFilename(File));
				TestTrue(*FString::Printf(TEXT("%s has harmonics"), *File), Profile.Harmonics.Num() > 0);
				TestTrue(*FString::Printf(TEXT("%s fits the oscillator bank"), *File), Profile.Harmonics.Num() <= FLREngineAudioProfile::MaxHarmonics);

				const FLREngineAudioProfile Safe = Profile.Sanitized();
				TestEqual(*FString::Printf(TEXT("Cylinders of %s"), *File), Safe.CylinderCount, Profile.CylinderCount);
				TestEqual(*FString::Printf(TEXT("Engine gain of %s"), *File), Safe.EngineGain, Profile.EngineGain);
				TestEqual(*FString::Printf(TEXT("Drive of %s"), *File), Safe.Drive, Profile.Drive);
			}
		});

		It("every vehicle references an existing audio profile", [this]()
		{
			const TArray<FString> Vehicles = FLRDataLoader::ListFiles(TEXT("Vehicles"));
			TestTrue(TEXT("Vehicles found"), Vehicles.Num() > 0);

			for (const FString& File : Vehicles)
			{
				FLRVehicleDefinition Definition;
				if (!TestTrue(*FString::Printf(TEXT("Loads %s"), *File), FLRDataLoader::Load(File, Definition)))
				{
					continue;
				}
				FLREngineAudioProfile Profile;
				const bool bLoaded = FLREngineAudioProfile::LoadById(Definition.AudioProfile, Profile);
				TestTrue(*FString::Printf(TEXT("%s -> %s"), *File, *Definition.AudioProfile.ToString()), bLoaded);
			}
		});

		It("the shipped engines have their real cylinder counts and differ in character", [this]()
		{
			struct FExpected
			{
				const TCHAR* Id;
				int32 Cylinders;
			};
			const FExpected Expected[] = {
				{ TEXT("engine_i6_turbo"), 6 },
				{ TEXT("engine_v8_biturbo"), 8 },
				{ TEXT("engine_v8_supercharged"), 8 },
				{ TEXT("engine_v8_supercar"), 8 },
				{ TEXT("engine_v10_na"), 10 }
			};

			for (const FExpected& Engine : Expected)
			{
				FLREngineAudioProfile Profile;
				TestTrue(*FString::Printf(TEXT("%s loads"), Engine.Id), FLREngineAudioProfile::LoadById(FName(Engine.Id), Profile));
				TestEqual(*FString::Printf(TEXT("%s cylinders"), Engine.Id), Profile.CylinderCount, Engine.Cylinders);
			}

			// The muscle V8 must lope more than the flat-plane supercar V8.
			const auto HalfOrderGain = [](const FLREngineAudioProfile& Profile)
			{
				for (const FLREngineHarmonic& Harmonic : Profile.Harmonics)
				{
					if (FMath::IsNearlyEqual(Harmonic.Order, 0.5f))
					{
						return Harmonic.OnLoadGain;
					}
				}
				return 0.f;
			};
			FLREngineAudioProfile Muscle;
			FLREngineAudioProfile Supercar;
			FLREngineAudioProfile::LoadById(FName(TEXT("engine_v8_supercharged")), Muscle);
			FLREngineAudioProfile::LoadById(FName(TEXT("engine_v8_supercar")), Supercar);
			TestTrue(TEXT("More half-order rumble on the muscle car"), HalfOrderGain(Muscle) > HalfOrderGain(Supercar) * 3.f);
		});

		It("every profile renders a bounded signal at full load", [this]()
		{
			for (const FString& File : FLRDataLoader::ListFiles(TEXT("Audio")))
			{
				FLREngineAudioProfile Profile;
				if (!FLRDataLoader::Load(File, Profile))
				{
					continue;
				}

				FLRVehicleSynth Synth;
				Synth.ApplyProfile(Profile);
				Synth.Prepare(48000.f, 2);

				FLRVehicleSynthParams Params;
				Params.Rpm = Profile.FallbackMaxRpm;
				Params.MaxRpm = Profile.FallbackMaxRpm;
				Params.Throttle = 1.f;
				Params.SpeedMs = 90.f;
				Synth.SetParams(Params);
				Synth.TriggerWhoosh(1.f, 1.f);

				float Buffer[1024];
				float Peak = 0.f;
				bool bBounded = true;
				for (int32 Block = 0; Block < 100; ++Block)
				{
					Synth.Render(Buffer, 512);
					for (const float Sample : Buffer)
					{
						bBounded = bBounded && FMath::IsFinite(Sample) && FMath::Abs(Sample) <= 1.f;
						Peak = FMath::Max(Peak, FMath::Abs(Sample));
					}
				}
				TestTrue(*FString::Printf(TEXT("%s bounded"), *File), bBounded);
				TestTrue(*FString::Printf(TEXT("%s audible"), *File), Peak > 0.05f);
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
