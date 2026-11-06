// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "DSP/LRVehicleSynth.h"
#include "Misc/AutomationTest.h"

namespace LRVehicleSynthSpecPrivate
{
	constexpr float TestSampleRate = 48000.f;
	constexpr int32 BufferFrames = 480;

	/** What a rendered passage measured. */
	struct FRenderStats
	{
		double SumLeft = 0.0;
		double SumRight = 0.0;
		double SumDifference = 0.0;
		float Peak = 0.f;
		int64 NumFrames = 0;
		bool bAllFinite = true;
		bool bAllBounded = true;

		float RmsLeft() const { return NumFrames > 0 ? static_cast<float>(FMath::Sqrt(SumLeft / NumFrames)) : 0.f; }
		float RmsRight() const { return NumFrames > 0 ? static_cast<float>(FMath::Sqrt(SumRight / NumFrames)) : 0.f; }
		float Rms() const { return 0.5f * (RmsLeft() + RmsRight()); }

		/** RMS of the sample-to-sample difference of the left channel: rises with high-frequency content. */
		float RmsDifference() const { return NumFrames > 0 ? static_cast<float>(FMath::Sqrt(SumDifference / NumFrames)) : 0.f; }
	};

	/** Renders Seconds of stereo audio in fixed buffers and measures it. */
	FRenderStats RenderSeconds(FLRVehicleSynth& Synth, float Seconds)
	{
		FRenderStats Stats;
		float Buffer[BufferFrames * 2];
		float Previous = 0.f;
		const int32 NumBuffers = FMath::Max(FMath::RoundToInt(Seconds * TestSampleRate / BufferFrames), 1);
		for (int32 BufferIndex = 0; BufferIndex < NumBuffers; ++BufferIndex)
		{
			Synth.Render(Buffer, BufferFrames);
			for (int32 Frame = 0; Frame < BufferFrames; ++Frame)
			{
				const float Left = Buffer[Frame * 2];
				const float Right = Buffer[Frame * 2 + 1];
				Stats.bAllFinite = Stats.bAllFinite && FMath::IsFinite(Left) && FMath::IsFinite(Right);
				Stats.bAllBounded = Stats.bAllBounded && FMath::Abs(Left) <= 1.f && FMath::Abs(Right) <= 1.f;
				Stats.Peak = FMath::Max3(Stats.Peak, FMath::Abs(Left), FMath::Abs(Right));
				Stats.SumLeft += Left * Left;
				Stats.SumRight += Right * Right;
				Stats.SumDifference += (Left - Previous) * (Left - Previous);
				Previous = Left;
				++Stats.NumFrames;
			}
		}
		return Stats;
	}

	/** Prepares a synth and snaps it to Params (no smoothing ramp in the measurement). */
	void Setup(FLRVehicleSynth& Synth, const FLRVehicleSynthParams& Params)
	{
		Synth.Prepare(TestSampleRate, 2);
		Synth.SetParams(Params);
		Synth.Reset();
		Synth.SetParams(Params);
	}

	/** Engine running at a steady state; no vehicle movement. */
	FLRVehicleSynthParams EngineParams(float Rpm, float Throttle)
	{
		FLRVehicleSynthParams Params;
		Params.Rpm = Rpm;
		Params.MaxRpm = 7000.f;
		Params.Throttle = Throttle;
		return Params;
	}

	/** Engine off, standing still: only one-shots sound. */
	FLRVehicleSynthParams SilentParams()
	{
		FLRVehicleSynthParams Params;
		Params.bEngineOn = false;
		return Params;
	}

	/** Profile whose only audible layer is the wind, without gusts. */
	FLREngineAudioProfile WindOnlyProfile()
	{
		FLREngineAudioProfile Profile;
		Profile.EngineGain = 0.f;
		Profile.TyreGain = 0.f;
		Profile.JointGain = 0.f;
		Profile.WindGustDepth = 0.f;
		Profile.WindGain = 0.1f;
		return Profile;
	}

	/** Profile whose only audible layer is the tyre / joint one. */
	FLREngineAudioProfile JointOnlyProfile()
	{
		FLREngineAudioProfile Profile;
		Profile.EngineGain = 0.f;
		Profile.WindGain = 0.f;
		Profile.TyreGain = 0.f;
		return Profile;
	}

	/** Moves along the road for Seconds at SpeedMs, updating the parameters every buffer as the game would. */
	FRenderStats DriveRoad(FLRVehicleSynth& Synth, FLRVehicleSynthParams Params, float SpeedMs, float Seconds)
	{
		FRenderStats Total;
		Params.SpeedMs = SpeedMs;
		Params.bHasRoadS = true;
		Setup(Synth, Params);
		const int32 NumBuffers = FMath::RoundToInt(Seconds * TestSampleRate / BufferFrames);
		for (int32 BufferIndex = 0; BufferIndex < NumBuffers; ++BufferIndex)
		{
			const FRenderStats Stats = RenderSeconds(Synth, BufferFrames / TestSampleRate);
			Total.SumLeft += Stats.SumLeft;
			Total.SumRight += Stats.SumRight;
			Total.NumFrames += Stats.NumFrames;
			Total.Peak = FMath::Max(Total.Peak, Stats.Peak);
			Params.RoadS += SpeedMs * (BufferFrames / TestSampleRate);
			Synth.SetParams(Params);
		}
		return Total;
	}
}

BEGIN_DEFINE_SPEC(FLRVehicleSynthSpec, "LeftRight.Audio.Synth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRVehicleSynthSpec)

void FLRVehicleSynthSpec::Define()
{
	using namespace LRVehicleSynthSpecPrivate;

	Describe("Output safety", [this]()
	{
		It("stays finite and inside [-1, 1] with every layer at its most extreme", [this]()
		{
			FLREngineAudioProfile Loud;
			Loud.EngineGain = 2.f;
			Loud.WindGain = 2.f;
			Loud.WindMaxScale = 4.f;
			Loud.TyreGain = 2.f;
			Loud.JointGain = 2.f;
			Loud.WhooshGain = 2.f;
			Loud.CrashGain = 2.f;
			Loud.MasterGain = 2.f;
			Loud.Drive = 8.f;

			const float RpmValues[] = { 0.f, 800.f, 7000.f, 20000.f, 1.0e7f, -500.f };
			const float SpeedValues[] = { -50.f, 0.f, 100.f, 1000.f };
			for (const float Rpm : RpmValues)
			{
				for (const float SpeedMs : SpeedValues)
				{
					FLRVehicleSynth Synth;
					Synth.ApplyProfile(Loud);
					FLRVehicleSynthParams Params = EngineParams(Rpm, 1.f);
					Params.SpeedMs = SpeedMs;
					Params.bHasRoadS = true;
					Setup(Synth, Params);
					Synth.TriggerWhoosh(1.f, 1.f);
					Synth.TriggerWhoosh(1.f, -1.f);
					Synth.TriggerCrash(1.f);

					const FRenderStats Stats = RenderSeconds(Synth, 0.25f);
					const FString Case = FString::Printf(TEXT("Rpm %f, speed %f"), Rpm, SpeedMs);
					TestTrue(*FString::Printf(TEXT("Finite: %s"), *Case), Stats.bAllFinite);
					TestTrue(*FString::Printf(TEXT("Bounded: %s"), *Case), Stats.bAllBounded);
				}
			}
		});

		It("survives non-finite parameters from the game thread", [this]()
		{
			volatile float Huge = TNumericLimits<float>::Max();
			const float Infinity = Huge * 2.f;
			const float NotANumber = Infinity - Infinity;

			FLRVehicleSynth Synth;
			FLRVehicleSynthParams Params = EngineParams(NotANumber, NotANumber);
			Params.MaxRpm = Infinity;
			Params.SpeedMs = NotANumber;
			Params.RoadS = static_cast<double>(NotANumber);
			Params.bHasRoadS = true;
			Params.WheelbaseMeters = NotANumber;
			Setup(Synth, Params);

			const FRenderStats Stats = RenderSeconds(Synth, 0.2f);
			TestTrue(TEXT("Finite"), Stats.bAllFinite);
			TestTrue(TEXT("Bounded"), Stats.bAllBounded);
		});

		It("writes silence on the extra channels and folds to mono", [this]()
		{
			FLRVehicleSynth Synth;
			Synth.Prepare(TestSampleRate, 4);
			Synth.SetParams(EngineParams(4000.f, 1.f));

			float Quad[256 * 4];
			Synth.Render(Quad, 256);
			float PeakFront = 0.f;
			for (int32 Frame = 0; Frame < 256; ++Frame)
			{
				PeakFront = FMath::Max(PeakFront, FMath::Abs(Quad[Frame * 4]));
				TestEqual(TEXT("Third channel"), Quad[Frame * 4 + 2], 0.f);
				TestEqual(TEXT("Fourth channel"), Quad[Frame * 4 + 3], 0.f);
			}
			TestTrue(TEXT("Front channels carry the sound"), PeakFront > 0.f);

			FLRVehicleSynth Mono;
			Mono.Prepare(TestSampleRate, 1);
			Mono.SetParams(EngineParams(4000.f, 1.f));
			float MonoBuffer[256];
			Mono.Render(MonoBuffer, 256);
			float PeakMono = 0.f;
			for (const float Sample : MonoBuffer)
			{
				PeakMono = FMath::Max(PeakMono, FMath::Abs(Sample));
			}
			TestTrue(TEXT("Mono carries the sound"), PeakMono > 0.f && PeakMono <= 1.f);
		});

		It("renders the same samples from the same inputs", [this]()
		{
			FLRVehicleSynth A;
			FLRVehicleSynth B;
			Setup(A, EngineParams(3500.f, 0.7f));
			Setup(B, EngineParams(3500.f, 0.7f));
			float BufferA[512];
			float BufferB[512];
			A.Render(BufferA, 256);
			B.Render(BufferB, 256);
			TestEqual(TEXT("Identical buffers"), FMemory::Memcmp(BufferA, BufferB, sizeof(BufferA)), 0);
		});
	});

	Describe("Engine layer", [this]()
	{
		It("is silent at 0 rpm and with the engine off", [this]()
		{
			FLRVehicleSynth Stopped;
			Setup(Stopped, EngineParams(0.f, 1.f));
			TestTrue(TEXT("0 rpm"), RenderSeconds(Stopped, 0.1f).Peak < 1.0e-4f);

			FLRVehicleSynth Off;
			FLRVehicleSynthParams Params = EngineParams(5000.f, 1.f);
			Params.bEngineOn = false;
			Setup(Off, Params);
			TestTrue(TEXT("Engine off"), RenderSeconds(Off, 0.1f).Peak < 1.0e-4f);
		});

		It("is louder under load than coasting", [this]()
		{
			FLRVehicleSynth OffLoad;
			Setup(OffLoad, EngineParams(4000.f, 0.f));
			const float OffLoadRms = RenderSeconds(OffLoad, 0.5f).Rms();

			FLRVehicleSynth OnLoad;
			Setup(OnLoad, EngineParams(4000.f, 1.f));
			const float OnLoadRms = RenderSeconds(OnLoad, 0.5f).Rms();

			TestTrue(*FString::Printf(TEXT("%f > %f"), OnLoadRms, OffLoadRms), OnLoadRms > OffLoadRms * 1.2f);
		});

		It("rises in pitch with rpm", [this]()
		{
			FLRVehicleSynth Low;
			Setup(Low, EngineParams(1500.f, 1.f));
			const FRenderStats LowStats = RenderSeconds(Low, 0.5f);

			FLRVehicleSynth High;
			Setup(High, EngineParams(6500.f, 1.f));
			const FRenderStats HighStats = RenderSeconds(High, 0.5f);

			const float LowBrightness = LowStats.RmsDifference() / FMath::Max(LowStats.RmsLeft(), 1.0e-6f);
			const float HighBrightness = HighStats.RmsDifference() / FMath::Max(HighStats.RmsLeft(), 1.0e-6f);
			TestTrue(*FString::Printf(TEXT("%f > %f"), HighBrightness, LowBrightness), HighBrightness > LowBrightness * 1.5f);
		});

		It("fades out after an engine cut", [this]()
		{
			FLRVehicleSynthParams Params = EngineParams(4000.f, 1.f);
			FLRVehicleSynth Synth;
			Setup(Synth, Params);
			const float RunningRms = RenderSeconds(Synth, 0.2f).Rms();

			Params.bEngineOn = false;
			Synth.SetParams(Params);
			RenderSeconds(Synth, Synth.GetProfile().EngineCutFadeSeconds * 8.f);
			const float CutRms = RenderSeconds(Synth, 0.1f).Rms();

			TestTrue(TEXT("Was running"), RunningRms > 0.01f);
			TestTrue(*FString::Printf(TEXT("Faded out: %f"), CutRms), CutRms < RunningRms * 0.01f);
		});
	});

	Describe("Wind layer", [this]()
	{
		It("scales with the square of the speed", [this]()
		{
			FLRVehicleSynthParams Params = SilentParams();
			Params.SpeedMs = 100.f / 3.6f;
			FLRVehicleSynth Slow;
			Slow.ApplyProfile(WindOnlyProfile());
			Setup(Slow, Params);
			const float SlowRms = RenderSeconds(Slow, 1.f).Rms();

			Params.SpeedMs = 200.f / 3.6f;
			FLRVehicleSynth Fast;
			Fast.ApplyProfile(WindOnlyProfile());
			Setup(Fast, Params);
			const float FastRms = RenderSeconds(Fast, 1.f).Rms();

			const float Ratio = FastRms / FMath::Max(SlowRms, 1.0e-6f);
			TestTrue(*FString::Printf(TEXT("Twice the speed is about four times the level: %f"), Ratio), Ratio > 3.f && Ratio < 5.f);
		});

		It("is silent when stopped and decorrelated between channels", [this]()
		{
			FLRVehicleSynth Stopped;
			Stopped.ApplyProfile(WindOnlyProfile());
			Setup(Stopped, SilentParams());
			TestTrue(TEXT("No wind when stopped"), RenderSeconds(Stopped, 0.1f).Peak < 1.0e-5f);

			FLRVehicleSynthParams Params = SilentParams();
			Params.SpeedMs = 60.f;
			FLRVehicleSynth Moving;
			Moving.ApplyProfile(WindOnlyProfile());
			Setup(Moving, Params);
			float Buffer[BufferFrames * 2];
			Moving.Render(Buffer, BufferFrames);
			bool bDifferent = false;
			for (int32 Frame = 0; Frame < BufferFrames; ++Frame)
			{
				bDifferent = bDifferent || Buffer[Frame * 2] != Buffer[Frame * 2 + 1];
			}
			TestTrue(TEXT("Left and right differ"), bDifferent);
		});
	});

	Describe("Expansion joints", [this]()
	{
		It("thump every JointSpacingMeters of road", [this]()
		{
			FLREngineAudioProfile Profile = JointOnlyProfile();
			Profile.JointSpacingMeters = 12.f;
			FLRVehicleSynth Synth;
			Synth.ApplyProfile(Profile);

			FLRVehicleSynthParams Params = SilentParams();
			Params.RoadS = 1.0;
			const FRenderStats Stats = DriveRoad(Synth, Params, 30.f, 2.f);

			// 60 m of road: 60 / 12 = 5 joints (one more or less depending on the exact sub-buffer phase).
			TestTrue(*FString::Printf(TEXT("Joint count %d"), Synth.GetJointCount()), FMath::Abs(Synth.GetJointCount() - 5) <= 1);
			TestTrue(TEXT("The thumps are audible"), Stats.Peak > 0.01f);
		});

		It("double the rate at double the speed", [this]()
		{
			FLRVehicleSynth Slow;
			Slow.ApplyProfile(JointOnlyProfile());
			FLRVehicleSynthParams Params = SilentParams();
			Params.RoadS = 1.0;
			DriveRoad(Slow, Params, 20.f, 3.f);

			FLRVehicleSynth Fast;
			Fast.ApplyProfile(JointOnlyProfile());
			DriveRoad(Fast, Params, 40.f, 3.f);

			TestTrue(TEXT("Twice the joints"), FMath::Abs(Fast.GetJointCount() - 2 * Slow.GetJointCount()) <= 2);
		});

		It("are disabled by a zero spacing, without a road and at standstill", [this]()
		{
			FLREngineAudioProfile Profile = JointOnlyProfile();
			Profile.JointSpacingMeters = 0.f;
			FLRVehicleSynth NoSpacing;
			NoSpacing.ApplyProfile(Profile);
			DriveRoad(NoSpacing, SilentParams(), 30.f, 1.f);
			TestEqual(TEXT("Zero spacing"), NoSpacing.GetJointCount(), 0);

			FLRVehicleSynth NoRoad;
			NoRoad.ApplyProfile(JointOnlyProfile());
			FLRVehicleSynthParams Params = SilentParams();
			Params.SpeedMs = 30.f;
			Params.bHasRoadS = false;
			Setup(NoRoad, Params);
			RenderSeconds(NoRoad, 1.f);
			TestEqual(TEXT("No road"), NoRoad.GetJointCount(), 0);

			FLRVehicleSynth Stopped;
			Stopped.ApplyProfile(JointOnlyProfile());
			DriveRoad(Stopped, SilentParams(), 0.f, 1.f);
			TestEqual(TEXT("Standing still"), Stopped.GetJointCount(), 0);
		});

		It("do not fire when the car teleports to another road position", [this]()
		{
			FLRVehicleSynth Synth;
			Synth.ApplyProfile(JointOnlyProfile());
			FLRVehicleSynthParams Params = SilentParams();
			Params.SpeedMs = 1.f;
			Params.RoadS = 1.0;
			Params.bHasRoadS = true;
			Setup(Synth, Params);
			RenderSeconds(Synth, 0.05f);

			Params.RoadS = 5000.0;
			Synth.SetParams(Params);
			RenderSeconds(Synth, 0.05f);
			TestEqual(TEXT("No thump from the jump"), Synth.GetJointCount(), 0);
		});
	});

	Describe("Near-miss whoosh", [this]()
	{
		It("sounds louder on the side of the passed vehicle", [this]()
		{
			FLRVehicleSynth Right;
			Setup(Right, SilentParams());
			Right.TriggerWhoosh(1.f, 1.f);
			const FRenderStats RightStats = RenderSeconds(Right, 0.3f);
			TestTrue(TEXT("Audible"), RightStats.Rms() > 0.01f);
			TestTrue(*FString::Printf(TEXT("Right side: R %f, L %f"), RightStats.RmsRight(), RightStats.RmsLeft()), RightStats.RmsRight() > RightStats.RmsLeft() * 2.f);

			FLRVehicleSynth Left;
			Setup(Left, SilentParams());
			Left.TriggerWhoosh(1.f, -1.f);
			const FRenderStats LeftStats = RenderSeconds(Left, 0.3f);
			TestTrue(*FString::Printf(TEXT("Left side: L %f, R %f"), LeftStats.RmsLeft(), LeftStats.RmsRight()), LeftStats.RmsLeft() > LeftStats.RmsRight() * 2.f);
		});

		It("sweeps downward", [this]()
		{
			FLRVehicleSynth Synth;
			FLREngineAudioProfile Profile;
			Profile.WhooshDurationSeconds = 1.f;
			Synth.ApplyProfile(Profile);
			Setup(Synth, SilentParams());
			Synth.TriggerWhoosh(1.f, 0.f);

			const FRenderStats Start = RenderSeconds(Synth, 0.25f);
			RenderSeconds(Synth, 0.25f);
			const FRenderStats End = RenderSeconds(Synth, 0.25f);

			const float StartBrightness = Start.RmsDifference() / FMath::Max(Start.RmsLeft(), 1.0e-6f);
			const float EndBrightness = End.RmsDifference() / FMath::Max(End.RmsLeft(), 1.0e-6f);
			TestTrue(*FString::Printf(TEXT("%f > %f"), StartBrightness, EndBrightness), StartBrightness > EndBrightness);
		});

		It("ends after its duration and steals the oldest voice", [this]()
		{
			FLRVehicleSynth Synth;
			Setup(Synth, SilentParams());
			for (int32 Index = 0; Index < FLRVehicleSynth::MaxWhooshVoices + 3; ++Index)
			{
				Synth.TriggerWhoosh(1.f, 0.f);
			}
			TestEqual(TEXT("Voices limited"), Synth.GetActiveWhooshCount(), FLRVehicleSynth::MaxWhooshVoices);

			RenderSeconds(Synth, Synth.GetProfile().WhooshDurationSeconds + 0.05f);
			TestEqual(TEXT("All finished"), Synth.GetActiveWhooshCount(), 0);
		});

		It("gets stronger with relative speed and smaller clearance", [this]()
		{
			const FLREngineAudioProfile Profile;
			const float Base = FLRVehicleSynth::ComputeWhooshIntensity(Profile, 60.f, 1.f);
			TestTrue(TEXT("Faster is stronger"), FLRVehicleSynth::ComputeWhooshIntensity(Profile, 100.f, 1.f) > Base);
			TestTrue(TEXT("Closer is stronger"), FLRVehicleSynth::ComputeWhooshIntensity(Profile, 60.f, 0.25f) > Base);
			TestEqual(TEXT("No relative speed, no sound"), FLRVehicleSynth::ComputeWhooshIntensity(Profile, 0.f, 0.25f), 0.f);
			TestTrue(TEXT("Capped at 1"), FLRVehicleSynth::ComputeWhooshIntensity(Profile, 1000.f, 0.f) <= 1.f);
		});

		It("pans toward greater D for a car on the right", [this]()
		{
			TestEqual(TEXT("Right"), FLRVehicleSynth::ComputeWhooshPan(1.85f, 5.55f), 1.f);
			TestEqual(TEXT("Left"), FLRVehicleSynth::ComputeWhooshPan(1.85f, -1.85f), -1.f);
			TestEqual(TEXT("Aligned"), FLRVehicleSynth::ComputeWhooshPan(1.f, 1.f), 0.f);
		});
	});

	Describe("Crash", [this]()
	{
		It("is a loud burst that decays", [this]()
		{
			FLRVehicleSynth Synth;
			Setup(Synth, SilentParams());
			Synth.TriggerCrash(1.f);

			const FRenderStats Impact = RenderSeconds(Synth, 0.1f);
			TestTrue(*FString::Printf(TEXT("Loud: %f"), Impact.Rms()), Impact.Rms() > 0.2f);
			TestTrue(TEXT("Bounded"), Impact.bAllBounded);

			RenderSeconds(Synth, Synth.GetProfile().CrashDurationSeconds);
			TestTrue(TEXT("Decayed"), RenderSeconds(Synth, 0.1f).Peak < 1.0e-3f);
		});

		It("a weak hit does not cut a stronger crash short", [this]()
		{
			FLRVehicleSynth Reference;
			Setup(Reference, SilentParams());
			Reference.TriggerCrash(1.f);
			const float ReferenceRms = RenderSeconds(Reference, 0.1f).Rms();

			FLRVehicleSynth Synth;
			Setup(Synth, SilentParams());
			Synth.TriggerCrash(1.f);
			Synth.TriggerCrash(0.05f);
			TestNearlyEqual(TEXT("Level unchanged"), RenderSeconds(Synth, 0.1f).Rms(), ReferenceRms, 1.0e-3f);
		});

		It("intensity follows the impact speed", [this]()
		{
			const FLREngineAudioProfile Profile;
			TestEqual(TEXT("No impact"), FLRVehicleSynth::ComputeCrashIntensity(Profile, 0.f), 0.f);
			TestNearlyEqual(TEXT("Half"), FLRVehicleSynth::ComputeCrashIntensity(Profile, Profile.CrashReferenceKmh * 0.5f), 0.5f, 1.0e-4f);
			TestEqual(TEXT("Capped"), FLRVehicleSynth::ComputeCrashIntensity(Profile, 1000.f), 1.f);
		});
	});

	Describe("Interior mix", [this]()
	{
		It("muffles the engine and lowers the wind", [this]()
		{
			FLRVehicleSynthParams Engine = EngineParams(6000.f, 1.f);
			FLRVehicleSynth ExteriorEngine;
			Setup(ExteriorEngine, Engine);
			const float ExteriorHigh = RenderSeconds(ExteriorEngine, 0.5f).RmsDifference();

			Engine.bInterior = true;
			FLRVehicleSynth InteriorEngine;
			Setup(InteriorEngine, Engine);
			const float InteriorHigh = RenderSeconds(InteriorEngine, 0.5f).RmsDifference();
			TestTrue(*FString::Printf(TEXT("Less high frequency inside: %f < %f"), InteriorHigh, ExteriorHigh), InteriorHigh < ExteriorHigh * 0.7f);

			FLRVehicleSynthParams Wind = SilentParams();
			Wind.SpeedMs = 70.f;
			FLRVehicleSynth ExteriorWind;
			ExteriorWind.ApplyProfile(WindOnlyProfile());
			Setup(ExteriorWind, Wind);
			const float ExteriorWindRms = RenderSeconds(ExteriorWind, 0.5f).Rms();

			Wind.bInterior = true;
			FLRVehicleSynth InteriorWind;
			InteriorWind.ApplyProfile(WindOnlyProfile());
			Setup(InteriorWind, Wind);
			const float InteriorWindRms = RenderSeconds(InteriorWind, 0.5f).Rms();
			TestTrue(*FString::Printf(TEXT("Less wind inside: %f < %f"), InteriorWindRms, ExteriorWindRms), InteriorWindRms < ExteriorWindRms * 0.6f);
		});

		It("crossfades smoothly when the camera changes", [this]()
		{
			FLRVehicleSynthParams Params = EngineParams(4000.f, 1.f);
			FLRVehicleSynth Synth;
			Setup(Synth, Params);
			RenderSeconds(Synth, 0.1f);

			Params.bInterior = true;
			Synth.SetParams(Params);
			const FRenderStats Stats = RenderSeconds(Synth, 0.5f);
			TestTrue(TEXT("Finite"), Stats.bAllFinite);
			TestTrue(TEXT("Bounded"), Stats.bAllBounded);
		});
	});

	Describe("Events", [this]()
	{
		It("Reset event silences the one-shots", [this]()
		{
			FLRVehicleSynth Synth;
			Setup(Synth, SilentParams());
			Synth.TriggerCrash(1.f);
			Synth.TriggerWhoosh(1.f, 0.f);

			FLRVehicleSynthEvent Event;
			Event.Type = FLRVehicleSynthEvent::EType::Reset;
			Synth.HandleEvent(Event);

			TestEqual(TEXT("No whoosh"), Synth.GetActiveWhooshCount(), 0);
			TestTrue(TEXT("Silence"), RenderSeconds(Synth, 0.05f).Peak < 1.0e-4f);
		});

		It("swapping the profile returns the previous one", [this]()
		{
			FLRVehicleSynth Synth;
			FLREngineAudioProfile Incoming = FLREngineAudioProfile().Sanitized();
			Incoming.Id = FName(TEXT("incoming"));
			Incoming.CylinderCount = 10;
			const int32 PreviousCylinders = Synth.GetProfile().CylinderCount;

			Synth.SwapProfile(Incoming);

			TestEqual(TEXT("New profile active"), Synth.GetProfile().CylinderCount, 10);
			TestEqual(TEXT("Previous profile handed back"), Incoming.CylinderCount, PreviousCylinders);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
