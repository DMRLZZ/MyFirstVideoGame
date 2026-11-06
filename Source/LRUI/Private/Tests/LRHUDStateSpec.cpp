// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LREvents.h"
#include "LRHUDState.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRHUDStateSpec, "LeftRight.UI.State",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

	/** Combo event with the given values. */
	static FLRComboChangedEvent MakeCombo(int32 Chain, float Multiplier, float TimeRemaining, bool bBroken = false)
	{
		FLRComboChangedEvent Event;
		Event.Chain = Chain;
		Event.Multiplier = Multiplier;
		Event.TimeRemaining = TimeRemaining;
		Event.bBroken = bBroken;
		return Event;
	}

END_DEFINE_SPEC(FLRHUDStateSpec)

void FLRHUDStateSpec::Define()
{
	Describe("PopupQueue", [this]()
	{
		It("copies the near-miss event into a popup", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(3, 1.f, 0.25f);

			FLRNearMissEvent Event;
			Event.Tier = ELRNearMissTier::Razor;
			Event.bOncoming = true;
			Event.bThreadTheNeedle = true;
			const int32 Slot = Queue.Push(Event);

			TestEqual(TEXT("Active"), Queue.NumActive(), 1);
			const FLRHUDNearMissPopup& Popup = Queue.GetSlots()[Slot];
			TestTrue(TEXT("Tier"), Popup.Tier == ELRNearMissTier::Razor);
			TestTrue(TEXT("Oncoming"), Popup.bOncoming);
			TestTrue(TEXT("Thread"), Popup.bThreadTheNeedle);
			TestFalse(TEXT("Points unknown"), Popup.HasPoints());
		});

		It("attaches points that arrive after the popup", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(3, 1.f, 0.25f);

			const int32 Slot = Queue.Push(ELRNearMissTier::Close, false, false);
			Queue.Tick(0.1f);
			Queue.ReportPoints(500);

			TestEqual(TEXT("Points"), Queue.GetSlots()[Slot].Points, static_cast<int64>(500));
		});

		It("attaches points that arrive before the popup", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(3, 1.f, 0.25f);

			Queue.ReportPoints(750);
			Queue.Tick(0.1f);
			const int32 Slot = Queue.Push(ELRNearMissTier::Close, false, false);

			TestEqual(TEXT("Points"), Queue.GetSlots()[Slot].Points, static_cast<int64>(750));
		});

		It("gives the points to the newest popup without points", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(3, 1.f, 0.25f);

			const int32 Older = Queue.Push(ELRNearMissTier::Close, false, false);
			const int32 Newer = Queue.Push(ELRNearMissTier::VeryClose, false, false);
			Queue.ReportPoints(100);
			Queue.ReportPoints(200);

			TestEqual(TEXT("Newer first"), Queue.GetSlots()[Newer].Points, static_cast<int64>(100));
			TestEqual(TEXT("Then older"), Queue.GetSlots()[Older].Points, static_cast<int64>(200));
		});

		It("drops pending points after the attach window", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(3, 1.f, 0.25f);

			Queue.ReportPoints(750);
			Queue.Tick(0.3f);
			const int32 Slot = Queue.Push(ELRNearMissTier::Close, false, false);

			TestFalse(TEXT("No points"), Queue.GetSlots()[Slot].HasPoints());
		});

		It("does not attach points to a popup outside the window", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(3, 1.f, 0.25f);

			const int32 Slot = Queue.Push(ELRNearMissTier::Close, false, false);
			Queue.Tick(0.3f);
			Queue.ReportPoints(100);

			TestFalse(TEXT("No points"), Queue.GetSlots()[Slot].HasPoints());
		});

		It("expires popups after their lifetime", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(3, 1.f, 0.25f);

			Queue.Push(ELRNearMissTier::Close, false, false);
			Queue.Tick(0.9f);
			TestEqual(TEXT("Still alive"), Queue.NumActive(), 1);
			Queue.Tick(0.2f);
			TestEqual(TEXT("Expired"), Queue.NumActive(), 0);
		});

		It("recycles the oldest popup when full", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(2, 1.f, 0.25f);

			const int32 First = Queue.Push(ELRNearMissTier::Close, false, false);
			const int32 Second = Queue.Push(ELRNearMissTier::Close, false, false);
			const int32 Third = Queue.Push(ELRNearMissTier::Razor, false, false);

			TestEqual(TEXT("Capacity"), Queue.GetSlots().Num(), 2);
			TestEqual(TEXT("Active"), Queue.NumActive(), 2);
			TestEqual(TEXT("Reuses the oldest slot"), Third, First);
			TestTrue(TEXT("Keeps the newer one"), Third != Second);
			TestTrue(TEXT("Replaced"), Queue.GetSlots()[Third].Tier == ELRNearMissTier::Razor);
		});

		It("ranks popups from newest to oldest", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(4, 1.f, 0.25f);

			const int32 First = Queue.Push(ELRNearMissTier::Close, false, false);
			const int32 Second = Queue.Push(ELRNearMissTier::Close, false, false);
			const int32 Third = Queue.Push(ELRNearMissTier::Close, false, false);

			TestEqual(TEXT("Newest"), Queue.GetStackRank(Third), 0);
			TestEqual(TEXT("Middle"), Queue.GetStackRank(Second), 1);
			TestEqual(TEXT("Oldest"), Queue.GetStackRank(First), 2);
			TestEqual(TEXT("Invalid slot"), Queue.GetStackRank(99), static_cast<int32>(INDEX_NONE));
			TestEqual(TEXT("Inactive slot"), Queue.GetStackRank(3), static_cast<int32>(INDEX_NONE));
		});

		It("clears everything on reset", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Configure(2, 1.f, 0.25f);

			Queue.Push(ELRNearMissTier::Close, false, false);
			Queue.ReportPoints(10);
			Queue.ReportPoints(20);
			Queue.Reset();

			TestEqual(TEXT("No popups"), Queue.NumActive(), 0);
			const int32 Slot = Queue.Push(ELRNearMissTier::Close, false, false);
			TestFalse(TEXT("No pending points"), Queue.GetSlots()[Slot].HasPoints());
		});

		It("works without Configure", [this]()
		{
			FLRHUDPopupQueue Queue;
			Queue.Push(ELRNearMissTier::Close, false, false);
			TestEqual(TEXT("One popup"), Queue.NumActive(), 1);
		});
	});

	Describe("ComboTracker", [this]()
	{
		It("mirrors the event and counts down", [this]()
		{
			FLRHUDComboTracker Tracker;
			Tracker.Apply(MakeCombo(2, 1.5f, 3.f));

			TestTrue(TEXT("Active"), Tracker.IsActive());
			TestEqual(TEXT("Chain"), Tracker.GetChain(), 2);
			TestEqual(TEXT("Multiplier"), Tracker.GetMultiplier(), 1.5f);
			TestEqual(TEXT("Full bar"), Tracker.GetRemainingFraction(), 1.f, KINDA_SMALL_NUMBER);

			Tracker.Tick(1.5f);
			TestEqual(TEXT("Half bar"), Tracker.GetRemainingFraction(), 0.5f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Time left"), Tracker.GetTimeRemaining(), 1.5f, KINDA_SMALL_NUMBER);

			Tracker.Tick(2.f);
			TestFalse(TEXT("Expired"), Tracker.IsActive());
			TestEqual(TEXT("Empty bar"), Tracker.GetRemainingFraction(), 0.f);
		});

		It("restarts the window when the chain grows", [this]()
		{
			FLRHUDComboTracker Tracker;
			Tracker.Apply(MakeCombo(1, 1.25f, 3.f));
			Tracker.Tick(2.f);
			Tracker.Apply(MakeCombo(2, 1.5f, 3.f));

			TestEqual(TEXT("Full bar again"), Tracker.GetRemainingFraction(), 1.f, KINDA_SMALL_NUMBER);
		});

		It("keeps the window on a sync event of the same chain", [this]()
		{
			FLRHUDComboTracker Tracker;
			Tracker.Apply(MakeCombo(1, 1.25f, 3.f));
			Tracker.Tick(1.f);
			Tracker.Apply(MakeCombo(1, 1.25f, 1.5f));

			TestEqual(TEXT("Half bar"), Tracker.GetRemainingFraction(), 0.5f, KINDA_SMALL_NUMBER);
		});

		It("shows the broken feedback for a while", [this]()
		{
			FLRHUDComboTracker Tracker;
			Tracker.BrokenDisplaySeconds = 1.f;
			Tracker.Apply(MakeCombo(3, 1.75f, 2.f));
			Tracker.Apply(MakeCombo(0, 1.f, 0.f, true));

			TestFalse(TEXT("Not active"), Tracker.IsActive());
			TestTrue(TEXT("Broken visible"), Tracker.IsShowingBroken());
			Tracker.Tick(1.1f);
			TestFalse(TEXT("Broken gone"), Tracker.IsShowingBroken());
		});

		It("clamps the multiplier and clears on reset", [this]()
		{
			FLRHUDComboTracker Tracker;
			Tracker.Apply(MakeCombo(1, 0.2f, 3.f));
			TestEqual(TEXT("At least 1"), Tracker.GetMultiplier(), 1.f);

			Tracker.Reset();
			TestEqual(TEXT("Chain"), Tracker.GetChain(), 0);
			TestFalse(TEXT("Inactive"), Tracker.IsActive());
			TestFalse(TEXT("No broken feedback"), Tracker.IsShowingBroken());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
