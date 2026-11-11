// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SButton;
class SWidgetSwitcher;
class ULRMusicSubsystem;
class ULRUserSettingsSubsystem;

/**
 * Title screen drawn over the live highway: logo, Play, mode and car selectors, settings and quit, and the best score
 * of the selected mode. Works with mouse, keyboard and gamepad.
 *
 * It only shows state and reports choices: the player controller owns it, opens it while the run state is Garage and
 * forwards the choices to ALRGameModeEndless. Selector rows change with left / right or a click on either half.
 */
class LRUI_API SLRTitleMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLRTitleMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ULRUserSettingsSubsystem>, Settings)
		SLATE_ARGUMENT(TWeakObjectPtr<ULRMusicSubsystem>, Music)
		/** Name of the selected mode. */
		SLATE_ATTRIBUTE(FText, ModeName)
		/** Name of the selected car. */
		SLATE_ATTRIBUTE(FText, CarName)
		/** Best score of the selected mode, already formatted. */
		SLATE_ATTRIBUTE(FText, BestScore)
		SLATE_EVENT(FSimpleDelegate, OnPlay)
		/** Receives -1 or +1. */
		SLATE_ARGUMENT(TFunction<void(int32)>, OnCycleMode)
		/** Receives -1 or +1. */
		SLATE_ARGUMENT(TFunction<void(int32)>, OnCycleCar)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Shows the main page, restarts the entrance animation and focuses "Play". */
	void FocusFirstItem();

	/** Widget that takes the focus when the menu opens. */
	TSharedPtr<SWidget> GetInitialFocus() const;

	//~ SWidget
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	void ShowMainPage();
	void ShowSettingsPage();

	/** 0 when the menu appears, 1 when the entrance animation ends. */
	float GetEntranceAlpha() const;

	FSimpleDelegate OnPlay;
	FSimpleDelegate OnQuit;
	TFunction<void(int32)> OnCycleMode;
	TFunction<void(int32)> OnCycleCar;

	TSharedPtr<SWidgetSwitcher> Pages;
	TSharedPtr<SButton> PlayButton;
	TSharedPtr<SButton> SettingsOpenButton;
	TSharedPtr<SButton> SettingsBackButton;

	/** Slate time when the entrance animation started. */
	double EntranceStartTime = 0.0;
};
