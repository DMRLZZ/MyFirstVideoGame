// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "DSP/LRSynthDSP.h"
#include "Misc/AutomationTest.h"

namespace LRSynthDSPSpecPrivate
{
	constexpr float TestSampleRate = 48000.f;

	/** +infinity, computed at runtime (a constant expression would trigger an overflow warning). */
	float MakeInfinity()
	{
		volatile float Huge = TNumericLimits<float>::Max();
		return Huge * 2.f;
	}

	/** Peak amplitude of a filter's steady-state response to a sine (first half of the signal discarded). */
	template <typename TFilter>
	float MeasureSinePeak(TFilter& Filter, float FrequencyHz, float SampleRate)
	{
		const int32 NumSamples = static_cast<int32>(SampleRate);
		float Peak = 0.f;
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			const float Time = static_cast<float>(Index) / SampleRate;
			const float Output = Filter.Process(FMath::Sin(LRSynthDSP::TwoPi * FrequencyHz * Time));
			if (Index >= NumSamples / 2)
			{
				Peak = FMath::Max(Peak, FMath::Abs(Output));
			}
		}
		return Peak;
	}

	/** Counts rising zero crossings of an oscillator bank during one second. */
	int32 CountRisingCrossings(FLROscillatorBank& Bank, float FundamentalHz, float SampleRate)
	{
		int32 Crossings = 0;
		float Previous = 0.f;
		const int32 NumSamples = static_cast<int32>(SampleRate);
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			const float Sample = Bank.Process(FundamentalHz, SampleRate);
			if (Previous <= 0.f && Sample > 0.f)
			{
				++Crossings;
			}
			Previous = Sample;
		}
		return Crossings;
	}
}

BEGIN_DEFINE_SPEC(FLRSynthDSPSpec, "LeftRight.Audio.DSP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRSynthDSPSpec)

void FLRSynthDSPSpec::Define()
{
	using namespace LRSynthDSPSpecPrivate;

	Describe("FiringFrequencyHz", [this]()
	{
		It("maps rpm to rpm / 60 * cylinders / 2", [this]()
		{
			TestNearlyEqual(TEXT("V8 at 6000 rpm"), LRSynthDSP::FiringFrequencyHz(6000.f, 8), 400.f, 1.0e-3f);
			TestNearlyEqual(TEXT("I6 at 3000 rpm"), LRSynthDSP::FiringFrequencyHz(3000.f, 6), 150.f, 1.0e-3f);
			TestNearlyEqual(TEXT("V10 at 8700 rpm"), LRSynthDSP::FiringFrequencyHz(8700.f, 10), 725.f, 1.0e-3f);
			TestNearlyEqual(TEXT("I4 at 900 rpm"), LRSynthDSP::FiringFrequencyHz(900.f, 4), 30.f, 1.0e-3f);
		});

		It("is safe for negative rpm and invalid cylinder counts", [this]()
		{
			TestEqual(TEXT("Negative rpm"), LRSynthDSP::FiringFrequencyHz(-1000.f, 8), 0.f);
			TestNearlyEqual(TEXT("Zero cylinders act as one"), LRSynthDSP::FiringFrequencyHz(6000.f, 0), 50.f, 1.0e-3f);
		});
	});

	Describe("SoftClip", [this]()
	{
		It("stays inside [-1, 1] for any input", [this]()
		{
			for (float X = -100.f; X <= 100.f; X += 0.01f)
			{
				const float Y = LRSynthDSP::SoftClip(X);
				if (Y < -1.f || Y > 1.f)
				{
					AddError(FString::Printf(TEXT("SoftClip(%f) = %f"), X, Y));
					return;
				}
			}
			TestEqual(TEXT("+inf"), LRSynthDSP::SoftClip(MakeInfinity()), 1.f);
			TestEqual(TEXT("-inf"), LRSynthDSP::SoftClip(-MakeInfinity()), -1.f);
		});

		It("turns NaN into silence", [this]()
		{
			const float Infinity = MakeInfinity();
			TestEqual(TEXT("NaN"), LRSynthDSP::SoftClip(Infinity - Infinity), 0.f);
		});

		It("is odd, monotonic and transparent near zero", [this]()
		{
			float Previous = LRSynthDSP::SoftClip(-4.f);
			for (float X = -4.f; X <= 4.f; X += 0.01f)
			{
				const float Y = LRSynthDSP::SoftClip(X);
				if (Y < Previous || !FMath::IsNearlyEqual(Y, -LRSynthDSP::SoftClip(-X), 1.0e-5f))
				{
					AddError(FString::Printf(TEXT("Not monotonic or not odd at %f"), X));
					return;
				}
				Previous = Y;
			}
			TestNearlyEqual(TEXT("Unit slope at zero"), LRSynthDSP::SoftClip(0.01f), 0.01f, 1.0e-4f);
		});
	});

	Describe("FLROscillatorBank", [this]()
	{
		It("produces the requested frequency for integer and half orders", [this]()
		{
			const float Hz = LRSynthDSP::FiringFrequencyHz(6000.f, 8);
			const float Gain = 1.f;

			FLROscillatorBank Firing;
			const float FiringOrder = 1.f;
			Firing.Configure(&FiringOrder, &Gain, 1);
			TestTrue(TEXT("Order 1 runs at 400 Hz"), FMath::Abs(CountRisingCrossings(Firing, Hz, TestSampleRate) - 400) <= 1);

			FLROscillatorBank Rumble;
			const float RumbleOrder = 0.5f;
			Rumble.Configure(&RumbleOrder, &Gain, 1);
			TestTrue(TEXT("Order 0.5 runs at 200 Hz"), FMath::Abs(CountRisingCrossings(Rumble, Hz, TestSampleRate) - 200) <= 1);
		});

		It("keeps the sum bounded by the sum of the gains", [this]()
		{
			const float Orders[] = { 0.5f, 1.f, 1.5f, 2.f, 3.f, 4.f };
			const float Gains[] = { 0.1f, 0.3f, 0.1f, 0.2f, 0.2f, 0.1f };
			FLROscillatorBank Bank;
			Bank.Configure(Orders, Gains, UE_ARRAY_COUNT(Orders));

			for (int32 Index = 0; Index < 96000; ++Index)
			{
				if (FMath::Abs(Bank.Process(437.f, TestSampleRate)) > 1.0001f)
				{
					AddError(TEXT("Partial sum exceeded the gain sum"));
					return;
				}
			}
		});

		It("skips partials above the Nyquist guard", [this]()
		{
			const float Order = 8.f;
			const float Gain = 1.f;
			FLROscillatorBank Bank;
			Bank.Configure(&Order, &Gain, 1);

			float Peak = 0.f;
			for (int32 Index = 0; Index < 1000; ++Index)
			{
				Peak = FMath::Max(Peak, FMath::Abs(Bank.Process(5000.f, TestSampleRate)));
			}
			TestEqual(TEXT("40 kHz partial is silent at 48 kHz"), Peak, 0.f);
		});

		It("signals one wrap per two fundamental cycles", [this]()
		{
			const float Order = 1.f;
			const float Gain = 1.f;
			FLROscillatorBank Bank;
			Bank.Configure(&Order, &Gain, 1);

			int32 Wraps = 0;
			for (int32 Index = 0; Index < static_cast<int32>(TestSampleRate); ++Index)
			{
				Bank.Process(100.f, TestSampleRate);
				Wraps += Bank.bWrapped ? 1 : 0;
			}
			TestTrue(TEXT("50 wraps per second at 100 Hz"), FMath::Abs(Wraps - 50) <= 1);
		});
	});

	Describe("One-pole filters", [this]()
	{
		It("low-pass passes DC and attenuates high frequencies by the expected amount", [this]()
		{
			FLROnePoleLowPass LowPass;
			LowPass.SetCutoff(1000.f, TestSampleRate);

			float Output = 0.f;
			for (int32 Index = 0; Index < 48000; ++Index)
			{
				Output = LowPass.Process(1.f);
			}
			TestNearlyEqual(TEXT("DC gain"), Output, 1.f, 1.0e-3f);

			LowPass.Reset();
			TestTrue(TEXT("About -3 dB at the cutoff"), FMath::Abs(MeasureSinePeak(LowPass, 1000.f, TestSampleRate) - 0.707f) < 0.05f);
			LowPass.Reset();
			TestTrue(TEXT("Attenuates a decade above"), MeasureSinePeak(LowPass, 10000.f, TestSampleRate) < 0.2f);
		});

		It("high-pass rejects DC", [this]()
		{
			FLROnePoleHighPass HighPass;
			HighPass.SetCutoff(100.f, TestSampleRate);

			float Output = 1.f;
			for (int32 Index = 0; Index < 48000; ++Index)
			{
				Output = HighPass.Process(1.f);
			}
			TestTrue(TEXT("DC removed"), FMath::Abs(Output) < 1.0e-3f);
		});

		It("coefficient stays in (0, 1] for any cutoff", [this]()
		{
			const float Cutoffs[] = { -100.f, 0.f, 1.f, 100.f, 24000.f, 1.0e6f };
			for (const float Cutoff : Cutoffs)
			{
				const float Coefficient = LRSynthDSP::OnePoleCoefficient(Cutoff, TestSampleRate);
				TestTrue(FString::Printf(TEXT("Cutoff %f"), Cutoff), Coefficient > 0.f && Coefficient <= 1.f);
			}
		});
	});

	Describe("FLRBiquad", [this]()
	{
		It("band-pass is unity at the center and attenuates away from it", [this]()
		{
			FLRBiquad BandPass;
			BandPass.SetBandPass(1000.f, 2.f, TestSampleRate);
			TestTrue(TEXT("Unity at the center"), FMath::Abs(MeasureSinePeak(BandPass, 1000.f, TestSampleRate) - 1.f) < 0.02f);
			BandPass.Reset();
			TestTrue(TEXT("Attenuates 3 octaves below"), MeasureSinePeak(BandPass, 125.f, TestSampleRate) < 0.2f);
			BandPass.Reset();
			TestTrue(TEXT("Attenuates 3 octaves above"), MeasureSinePeak(BandPass, 8000.f, TestSampleRate) < 0.2f);
		});

		It("low-pass passes DC", [this]()
		{
			FLRBiquad LowPass;
			LowPass.SetLowPass(500.f, 0.7071f, TestSampleRate);
			float Output = 0.f;
			for (int32 Index = 0; Index < 48000; ++Index)
			{
				Output = LowPass.Process(1.f);
			}
			TestNearlyEqual(TEXT("DC gain"), Output, 1.f, 1.0e-3f);
		});

		It("is stable and its impulse response decays over the whole parameter range", [this]()
		{
			const float Frequencies[] = { 0.f, 1.f, 20.f, 200.f, 2000.f, 12000.f, 23000.f, 24000.f, 1.0e6f };
			const float Qualities[] = { 0.f, 0.05f, 0.3f, 0.7071f, 2.f, 10.f, 50.f };
			const float SampleRates[] = { 8000.f, 44100.f, 48000.f, 96000.f };

			for (const float SampleRate : SampleRates)
			{
				for (const float Frequency : Frequencies)
				{
					for (const float Quality : Qualities)
					{
						for (int32 Mode = 0; Mode < 2; ++Mode)
						{
							FLRBiquad Filter;
							if (Mode == 0)
							{
								Filter.SetBandPass(Frequency, Quality, SampleRate);
							}
							else
							{
								Filter.SetLowPass(Frequency, Quality, SampleRate);
							}
							const FString Case = FString::Printf(TEXT("Mode %d, %f Hz, Q %f, Fs %f"), Mode, Frequency, Quality, SampleRate);
							if (!Filter.IsStable())
							{
								AddError(FString::Printf(TEXT("Poles outside the unit circle: %s"), *Case));
								return;
							}
							// Excite with an impulse; a decayed response has no energy left at the end.
							float Output = Filter.Process(1.f);
							for (int32 Index = 0; Index < 100000; ++Index)
							{
								Output = Filter.Process(0.f);
								if (!FMath::IsFinite(Output))
								{
									AddError(FString::Printf(TEXT("Non-finite output: %s"), *Case));
									return;
								}
							}
							if (FMath::Abs(Output) > 0.01f)
							{
								AddError(FString::Printf(TEXT("Impulse response did not decay (%f): %s"), Output, *Case));
								return;
							}
						}
					}
				}
			}
		});

		It("stays bounded while the coefficients sweep every block", [this]()
		{
			FLRNoise Noise;
			FLRBiquad Filter;
			float CenterHz = 8000.f;
			float Peak = 0.f;
			for (int32 Block = 0; Block < 4000; ++Block)
			{
				Filter.SetBandPass(CenterHz, 4.f, TestSampleRate);
				CenterHz = CenterHz > 100.f ? CenterHz * 0.995f : 8000.f;
				for (int32 Index = 0; Index < 32; ++Index)
				{
					Peak = FMath::Max(Peak, FMath::Abs(Filter.Process(Noise.NextBipolar())));
				}
			}
			TestTrue(TEXT("No blow-up"), Peak < 8.f);
		});
	});

	Describe("Helpers", [this]()
	{
		It("noise stays in range, is deterministic and has a mean near zero", [this]()
		{
			FLRNoise A;
			FLRNoise B;
			A.Seed(1234u);
			B.Seed(1234u);

			double Sum = 0.0;
			constexpr int32 NumSamples = 200000;
			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				const float Value = A.NextBipolar();
				if (Value < -1.f || Value >= 1.f || Value != B.NextBipolar())
				{
					AddError(TEXT("Noise out of range or not deterministic"));
					return;
				}
				Sum += Value;
			}
			TestTrue(TEXT("Mean near zero"), FMath::Abs(Sum / NumSamples) < 0.01);
		});

		It("seed 0 still produces noise", [this]()
		{
			FLRNoise Noise;
			Noise.Seed(0u);
			TestTrue(TEXT("Not stuck at zero"), Noise.NextUInt() != 0u);
		});

		It("pan law keeps constant power and moves toward the requested side", [this]()
		{
			for (float Pan = -1.f; Pan <= 1.f; Pan += 0.1f)
			{
				const float Left = LRSynthDSP::PanGainLeft(Pan);
				const float Right = LRSynthDSP::PanGainRight(Pan);
				TestNearlyEqual(TEXT("Constant power"), Left * Left + Right * Right, 1.f, 1.0e-4f);
			}
			TestTrue(TEXT("Hard left"), LRSynthDSP::PanGainLeft(-1.f) > 0.99f && LRSynthDSP::PanGainRight(-1.f) < 0.01f);
			TestTrue(TEXT("Hard right"), LRSynthDSP::PanGainRight(1.f) > 0.99f && LRSynthDSP::PanGainLeft(1.f) < 0.01f);
		});

		It("parameter smoother reaches 63 percent after one time constant", [this]()
		{
			FLRParamSmoother Smoother;
			Smoother.SetTime(0.01f, TestSampleRate);
			Smoother.Snap(0.f);
			Smoother.SetTarget(1.f);
			float Value = 0.f;
			for (int32 Index = 0; Index < 480; ++Index)
			{
				Value = Smoother.Next();
			}
			TestNearlyEqual(TEXT("One time constant"), Value, 0.632f, 0.01f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
