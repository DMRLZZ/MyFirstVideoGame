// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"
#include "LRInputDeviceTracker.h"

/**
 * Slate input pre-processor that only OBSERVES key, analog and mouse-button events (never consumes them) and feeds
 * FLRInputDeviceTracker, so the HUD can show keyboard or gamepad prompts for the last device used.
 * Owned by ALRHUD through a TSharedPtr; registered in BeginPlay and unregistered in EndPlay.
 */
class FLRHUDInputListener : public IInputProcessor
{
public:
	//~ IInputProcessor
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		Tracker.NotifyKey(InKeyEvent.GetKey());
		return false;
	}

	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
	{
		Tracker.NotifyKey(InAnalogInputEvent.GetKey(), InAnalogInputEvent.GetAnalogValue());
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		Tracker.NotifyKey(MouseEvent.GetEffectingButton());
		return false;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("LRHUDInputListener");
	}

	/** Last device family seen by this listener. */
	const FLRInputDeviceTracker& GetTracker() const { return Tracker; }

private:
	FLRInputDeviceTracker Tracker;
};
