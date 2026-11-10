// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRRoadTypes.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRRoadsidePropSpec, "LeftRight.World.RoadsideProp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRRoadsidePropSpec)

void FLRRoadsidePropSpec::Define()
{
	It("leaves valid values alone", [this]()
	{
		FLRRoadsideProp Prop;
		Prop.CountPer100M = 3.f;
		Prop.SpacingM = 2300.f;
		Prop.MinDistanceM = 12.f;
		Prop.MaxDistanceM = 400.f;
		Prop.MinScale = 0.7f;
		Prop.MaxScale = 1.4f;
		Prop.KeepClearAlongM = 90.f;
		Prop.KeepClearReachM = 75.f;

		const FLRRoadsideProp Safe = Prop.Sanitized();
		TestEqual(TEXT("Count"), Safe.CountPer100M, 3.f);
		TestEqual(TEXT("Spacing"), Safe.SpacingM, 2300.f);
		TestEqual(TEXT("Min distance"), Safe.MinDistanceM, 12.f);
		TestEqual(TEXT("Max distance"), Safe.MaxDistanceM, 400.f);
		TestEqual(TEXT("Min scale"), Safe.MinScale, 0.7f);
		TestEqual(TEXT("Max scale"), Safe.MaxScale, 1.4f);
		TestEqual(TEXT("Clearing along the road"), Safe.KeepClearAlongM, 90.f);
		TestEqual(TEXT("Clearing reach"), Safe.KeepClearReachM, 75.f);
	});

	It("never keeps a spacing that would loop forever", [this]()
	{
		FLRRoadsideProp Prop;
		Prop.SpacingM = 0.f;
		TestTrue(TEXT("Zero spacing"), Prop.Sanitized().SpacingM >= 10.f);
		Prop.SpacingM = -500.f;
		TestTrue(TEXT("Negative spacing"), Prop.Sanitized().SpacingM >= 10.f);
		Prop.SpacingM = 0.001f;
		TestTrue(TEXT("Tiny spacing"), Prop.Sanitized().SpacingM >= 10.f);
	});

	It("limits how many objects a chunk can get", [this]()
	{
		FLRRoadsideProp Prop;
		Prop.CountPer100M = 1.0e9f;
		TestTrue(TEXT("Huge count"), Prop.Sanitized().CountPer100M <= 100.f);
		Prop.CountPer100M = -4.f;
		TestEqual(TEXT("Negative count"), Prop.Sanitized().CountPer100M, 0.f);
	});

	It("orders distances and scales", [this]()
	{
		FLRRoadsideProp Prop;
		Prop.MinDistanceM = 200.f;
		Prop.MaxDistanceM = 50.f;
		Prop.MinScale = 2.f;
		Prop.MaxScale = 0.f;
		Prop.DistancePower = 0.f;
		Prop.KeepClearAlongM = -10.f;

		const FLRRoadsideProp Safe = Prop.Sanitized();
		TestTrue(TEXT("Max distance not below min"), Safe.MaxDistanceM >= Safe.MinDistanceM);
		TestTrue(TEXT("Max scale not below min"), Safe.MaxScale >= Safe.MinScale);
		TestTrue(TEXT("Scale above zero"), Safe.MinScale > 0.f);
		TestTrue(TEXT("Usable distance power"), Safe.DistancePower >= 0.1f);
		TestEqual(TEXT("No negative clearing"), Safe.KeepClearAlongM, 0.f);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
