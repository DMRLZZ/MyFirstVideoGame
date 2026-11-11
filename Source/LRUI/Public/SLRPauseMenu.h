// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRUserSettings.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SButton;
class SVerticalBox;
class SWidgetSwitcher;
class ULRMusicSubsystem;
class ULRUserSettingsSubsystem;

/**
 * Menu that opens with Esc: resume, restart, settings and quit. Works with mouse, keyboard and gamepad.
 *
 * Settings rows change with left / right on the keyboard or gamepad, and with a click on either half of the row.
 * Every change is saved at once by ULRUserSettingsSubsystem. The player controller owns the widget, pauses the game
 * and gives input back when it closes.
 */
class LRUI_API SLRPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLRPauseMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ULRUserSettingsSubsystem>, Settings)
		SLATE_ARGUMENT(TWeakObjectPtr<ULRMusicSubsystem>, Music)
		/** Name of the car in use. */
		SLATE_ATTRIBUTE(FText, CarName)
		SLATE_EVENT(FSimpleDelegate, OnResume)
		SLATE_EVENT(FSimpleDelegate, OnRestart)
		SLATE_EVENT(FSimpleDelegate, OnNextCar)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Gives keyboard and gamepad focus to the first button of the main page. */
	void FocusFirstItem();

	/** Widget that takes the focus when the menu opens. */
	TSharedPtr<SWidget> GetInitialFocus() const;

	//~ SWidget
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	void ShowMainPage();
	void ShowSettingsPage();

	FSimpleDelegate OnResume;
	FSimpleDelegate OnRestart;
	FSimpleDelegate OnNextCar;
	FSimpleDelegate OnQuit;

	TSharedPtr<SWidgetSwitcher> Pages;
	TSharedPtr<SButton> FirstMainButton;
	TSharedPtr<SButton> SettingsBackButton;
	TSharedPtr<SButton> SettingsOpenButton;
};
