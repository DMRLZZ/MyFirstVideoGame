// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRUnits.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FLRUnitsSpec, "LeftRight.Core.Units",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRUnitsSpec)

void FLRUnitsSpec::Define()
{
	Describe("Distance", [this]()
	{
		It("converts meters to centimeters and back", [this]()
		{
			TestEqual(TEXT("1 m = 100 cm"), LRUnits::MetersToCm(1.0), 100.0);
			TestEqual(TEXT("250 cm = 2.5 m"), LRUnits::CmToMeters(250.0), 2.5);

			const double Samples[] = { 0.0, 0.01, 3.7, -4.25, 1234.5678, 1.0e7 };
			for (const double Meters : Samples)
			{
				TestEqual(FString::Printf(TEXT("round trip %f m"), Meters),
					LRUnits::CmToMeters(LRUnits::MetersToCm(Meters)), Meters, 1.0e-9 * FMath::Max(1.0, FMath::Abs(Meters)));
			}
		});

		It("is usable in constant expressions", [this]()
		{
			static_assert(LRUnits::MetersToCm(2.0) == 200.0, "MetersToCm must be constexpr");
			TestTrue(TEXT("compile-time checks passed"), true);
		});
	});

	Describe("Speed", [this]()
	{
		It("converts m/s to km/h and back", [this]()
		{
			TestEqual(TEXT("10 m/s = 36 km/h"), LRUnits::MsToKmh(10.0), 36.0, 1.0e-9);
			TestEqual(TEXT("100 km/h = 27.777.. m/s"), LRUnits::KmhToMs(100.0), 100.0 / 3.6, 1.0e-9);

			const double Samples[] = { 0.0, 1.0, 27.78, 88.0, 120.5, -15.0 };
			for (const double Ms : Samples)
			{
				TestEqual(FString::Printf(TEXT("km/h round trip %f m/s"), Ms), LRUnits::KmhToMs(LRUnits::MsToKmh(Ms)), Ms, 1.0e-9);
			}
		});

		It("converts m/s to mph and back", [this]()
		{
			TestEqual(TEXT("1 m/s = 2.2369 mph"), LRUnits::MsToMph(1.0), 2.2369362920544, 1.0e-9);
			TestEqual(TEXT("60 mph = 26.8224 m/s"), LRUnits::MphToMs(60.0), 26.8224, 1.0e-6);

			const double Samples[] = { 0.0, 13.4, 44.7, 100.0 };
			for (const double Ms : Samples)
			{
				TestEqual(FString::Printf(TEXT("mph round trip %f m/s"), Ms), LRUnits::MphToMs(LRUnits::MsToMph(Ms)), Ms, 1.0e-9);
			}
		});

		It("converts Unreal velocity (cm/s) to km/h", [this]()
		{
			TestEqual(TEXT("1000 cm/s = 36 km/h"), LRUnits::CmsToKmh(1000.0), 36.0, 1.0e-9);
			TestEqual(TEXT("cm/s -> km/h consistent with m/s -> km/h"),
				LRUnits::CmsToKmh(LRUnits::MetersToCm(30.0)), LRUnits::MsToKmh(30.0), 1.0e-9);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
