// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "DSP/LRVehicleSynthBridge.h"
#include "Misc/AutomationTest.h"

namespace LRVehicleSynthBridgeSpecPrivate
{
	constexpr float TestSampleRate = 48000.f;
	constexpr int32 NumFrames = 256;
	constexpr int32 NumChannels = 2;

	/** Renders one stereo buffer and returns its peak. */
	float RenderPeak(FLRVehicleSynthBridge& Bridge)
	{
		float Buffer[NumFrames * NumChannels];
		Bridge.Render(Buffer, NumFrames * NumChannels, TestSampleRate, NumChannels);
		float Peak = 0.f;
		for (const float Sample : Buffer)
		{
			Peak = FMath::Max(Peak, FMath::Abs(Sample));
		}
		return Peak;
	}

	/** A sanitized-ready profile recognizable by its cylinder count. */
	FLREngineAudioProfile MakeProfile(int32 Cylinders)
	{
		FLREngineAudioProfile Profile;
		Profile.CylinderCount = Cylinders;
		return Profile;
	}
}

BEGIN_DEFINE_SPEC(FLRVehicleSynthBridgeSpec, "LeftRight.Audio.Bridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRVehicleSynthBridgeSpec)

void FLRVehicleSynthBridgeSpec::Define()
{
	using namespace LRVehicleSynthBridgeSpecPrivate;

	It("publishes parameters to the renderer", [this]()
	{
		FLRVehicleSynthBridge Bridge;
		TestTrue(TEXT("Silent without rpm"), RenderPeak(Bridge) < 1.0e-4f);

		FLRVehicleSynthParams Params;
		Params.Rpm = 4000.f;
		Params.MaxRpm = 7000.f;
		Params.Throttle = 1.f;
		Bridge.SetParams(Params);

		float Peak = 0.f;
		for (int32 Index = 0; Index < 20; ++Index)
		{
			Peak = FMath::Max(Peak, RenderPeak(Bridge));
		}
		TestTrue(TEXT("Engine audible"), Peak > 0.01f);
		TestTrue(TEXT("Bounded"), Peak <= 1.f);
	});

	It("delivers one-shots on the next buffer", [this]()
	{
		FLRVehicleSynthBridge Bridge;
		FLRVehicleSynthParams Params;
		Params.bEngineOn = false;
		Bridge.SetParams(Params);

		FLRVehicleSynthEvent Whoosh;
		Whoosh.Type = FLRVehicleSynthEvent::EType::Whoosh;
		Whoosh.Intensity = 1.f;
		TestTrue(TEXT("Queued"), Bridge.PushEvent(Whoosh));
		TestEqual(TEXT("Not consumed yet"), Bridge.GetSynth().GetActiveWhooshCount(), 0);

		RenderPeak(Bridge);
		TestEqual(TEXT("Voice started"), Bridge.GetSynth().GetActiveWhooshCount(), 1);
	});

	It("drops events instead of blocking when the ring is full", [this]()
	{
		FLRVehicleSynthBridge Bridge;
		FLRVehicleSynthEvent Event;
		Event.Type = FLRVehicleSynthEvent::EType::Crash;
		Event.Intensity = 0.5f;

		int32 Accepted = 0;
		for (uint32 Index = 0; Index < FLRVehicleSynthBridge::EventQueueCapacity * 2u; ++Index)
		{
			Accepted += Bridge.PushEvent(Event) ? 1 : 0;
		}
		TestTrue(TEXT("Some were accepted"), Accepted > 0);
		TestTrue(TEXT("The rest were dropped"), Accepted < static_cast<int32>(FLRVehicleSynthBridge::EventQueueCapacity * 2u));

		RenderPeak(Bridge);
		TestTrue(TEXT("Room again after a buffer"), Bridge.PushEvent(Event));
	});

	It("hands the old profile back so the render thread never frees it", [this]()
	{
		FLRVehicleSynthBridge Bridge;

		Bridge.SubmitProfile(MakeProfile(8));
		TestFalse(TEXT("Nothing retired before rendering"), Bridge.HasRetiredProfile());

		RenderPeak(Bridge);
		TestEqual(TEXT("Profile applied"), Bridge.GetSynth().GetProfile().CylinderCount, 8);
		TestTrue(TEXT("Previous profile waiting for the game thread"), Bridge.HasRetiredProfile());

		Bridge.ReclaimRetiredProfile();
		TestFalse(TEXT("Reclaimed"), Bridge.HasRetiredProfile());
	});

	It("applies a later profile because submitting reclaims the previous one first", [this]()
	{
		FLRVehicleSynthBridge Bridge;

		Bridge.SubmitProfile(MakeProfile(8));
		RenderPeak(Bridge);
		TestTrue(TEXT("Retired slot in use"), Bridge.HasRetiredProfile());

		Bridge.SubmitProfile(MakeProfile(10));
		TestFalse(TEXT("Retired slot freed by the submit"), Bridge.HasRetiredProfile());

		RenderPeak(Bridge);
		TestEqual(TEXT("Later profile applied"), Bridge.GetSynth().GetProfile().CylinderCount, 10);
	});

	It("keeps only the newest of two unconsumed profiles", [this]()
	{
		FLRVehicleSynthBridge Bridge;
		Bridge.SubmitProfile(MakeProfile(6));
		Bridge.SubmitProfile(MakeProfile(10));
		RenderPeak(Bridge);
		TestEqual(TEXT("Newest wins"), Bridge.GetSynth().GetProfile().CylinderCount, 10);
	});

	It("sanitizes submitted profiles", [this]()
	{
		FLRVehicleSynthBridge Bridge;
		Bridge.SubmitProfile(MakeProfile(1000));
		RenderPeak(Bridge);
		TestEqual(TEXT("Cylinders clamped"), Bridge.GetSynth().GetProfile().CylinderCount, 16);
		TestTrue(TEXT("Default harmonics added"), Bridge.GetSynth().GetProfile().Harmonics.Num() > 0);
	});

	It("prepares on first use and when the format changes", [this]()
	{
		FLRVehicleSynthBridge Bridge;
		float Buffer[512];

		TestEqual(TEXT("Returns the sample count"), Bridge.Render(Buffer, 512, 44100.f, 2), 512);
		TestEqual(TEXT("Sample rate"), Bridge.GetSynth().GetSampleRate(), 44100.f);
		TestEqual(TEXT("Channels"), Bridge.GetSynth().GetNumChannels(), 2);

		TestEqual(TEXT("Returns the sample count"), Bridge.Render(Buffer, 512, 48000.f, 1), 512);
		TestEqual(TEXT("New sample rate"), Bridge.GetSynth().GetSampleRate(), 48000.f);
		TestEqual(TEXT("New channels"), Bridge.GetSynth().GetNumChannels(), 1);
	});

	It("starts on the current state instead of ramping up from zero", [this]()
	{
		FLRVehicleSynthParams Params;
		Params.Rpm = 5000.f;
		Params.MaxRpm = 7000.f;
		Params.Throttle = 1.f;

		FLRVehicleSynthBridge Bridge;
		Bridge.SetParams(Params);
		RenderPeak(Bridge);

		// The synth renders right away: an engine smoothed from 0 rpm would still be near silence after one buffer.
		TestTrue(TEXT("Audible in the second buffer"), RenderPeak(Bridge) > 0.01f);
	});

	It("handles empty and partial buffers", [this]()
	{
		FLRVehicleSynthBridge Bridge;
		TestEqual(TEXT("Null buffer"), Bridge.Render(nullptr, 64, TestSampleRate, 2), 0);

		float Buffer[3] = { 5.f, 5.f, 5.f };
		TestEqual(TEXT("Not a whole number of frames"), Bridge.Render(Buffer, 3, TestSampleRate, 2), 3);
		TestEqual(TEXT("Leftover sample cleared"), Buffer[2], 0.f);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
