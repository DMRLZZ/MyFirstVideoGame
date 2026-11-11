// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRUserSettings.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ULRMusicSubsystem;
class ULRUserSettingsSubsystem;

/**
 * The player settings as menu rows (driving, sound, graphics), shared by the pause menu and the title screen.
 *
 * Rows change with left / right on the keyboard or gamepad and with a click on either half of the row. Every change is
 * saved at once by ULRUserSettingsSubsystem. The host puts it in a scroll box and adds its own "Back" button.
 */
class SLRSettingsList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLRSettingsList) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ULRUserSettingsSubsystem>, Settings)
		SLATE_ARGUMENT(TWeakObjectPtr<ULRMusicSubsystem>, Music)
		/** Name of the car in use, shown by the car row. */
		SLATE_ATTRIBUTE(FText, CarName)
		/** Adds a car row that calls this with -1 or +1. Without it the list has no car row. */
		SLATE_ARGUMENT(TFunction<void(int32)>, OnCycleCar)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** Reads the settings, lets Change edit them and stores the result. */
	void EditSettings(TFunctionRef<void(FLRUserSettings&)> Change) const;

	/** Current settings, defaults if the subsystem is gone. */
	FLRUserSettings ReadSettings() const;

	TWeakObjectPtr<ULRUserSettingsSubsystem> Settings;
	TWeakObjectPtr<ULRMusicSubsystem> Music;
	TAttribute<FText> CarName;
	TFunction<void(int32)> OnCycleCar;
	FText FolderNote;
};
