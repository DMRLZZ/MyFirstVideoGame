// Copyright LEFTRIGHT. All Rights Reserved.

#include "SLRPauseMenu.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "InputCoreTypes.h"
#include "LRCoreTypes.h"
#include "LRHUDFormat.h"
#include "LRMenuWidgets.h"
#include "LRMusicSubsystem.h"
#include "SLRSettingsList.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LRPauseMenu"

void SLRPauseMenu::Construct(const FArguments& InArgs)
{
	using namespace LRMenu;

	OnResume = InArgs._OnResume;
	OnRestart = InArgs._OnRestart;
	OnNextCar = InArgs._OnNextCar;
	OnQuit = InArgs._OnQuit;

	static const FSlateColorBrush DimBrush(FLinearColor::White);
	static const FSlateRoundedBoxBrush PanelBrush(FLinearColor::White, 12.f);

	TSharedRef<SVerticalBox> MainPage = SNew(SVerticalBox);
	const auto AddMain = [&MainPage](const TSharedRef<SWidget>& Item)
	{
		MainPage->AddSlot().AutoHeight().Padding(0.f, 4.f)[Item];
	};
	AddMain(MakeButton(LOCTEXT("Resume", "Continuar"), OnResume, &FirstMainButton));
	AddMain(MakeButton(LOCTEXT("Restart", "Reiniciar carrera"), OnRestart));
	AddMain(MakeButton(LOCTEXT("Settings", "Ajustes"), FSimpleDelegate::CreateSP(this, &SLRPauseMenu::ShowSettingsPage), &SettingsOpenButton));
	AddMain(MakeButton(LOCTEXT("Quit", "Salir del juego"), OnQuit));

	TWeakPtr<SLRPauseMenu> WeakThis = SharedThis(this);
	TSharedRef<SLRSettingsList> SettingsRows = SNew(SLRSettingsList)
		.Settings(InArgs._Settings)
		.Music(InArgs._Music)
		.CarName(InArgs._CarName)
		.OnCycleCar([WeakThis](int32)
		{
			if (const TSharedPtr<SLRPauseMenu> Menu = WeakThis.Pin())
			{
				Menu->OnNextCar.ExecuteIfBound();
			}
		});

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(&DimBrush).BorderBackgroundColor(Dim)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(760.f).MaxDesiredHeight(860.f)
			[
				SNew(SBorder).BorderImage(&PanelBrush).BorderBackgroundColor(Panel).Padding(FMargin(32.f, 26.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 18.f)
					[
						SNew(STextBlock).Text(LOCTEXT("Title", "PAUSA")).Font(Font("Bold", 30)).ColorAndOpacity(FSlateColor(Accent))
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(Pages, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()[MainPage]
						+ SWidgetSwitcher::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().FillHeight(1.f)
							[
								SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
								+ SScrollBox::Slot()[SettingsRows]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
							[
								MakeButton(LOCTEXT("Back", "Volver"), FSimpleDelegate::CreateSP(this, &SLRPauseMenu::ShowMainPage), &SettingsBackButton)
							]
						]
					]
				]
			]
		]
	];
}

void SLRPauseMenu::FocusFirstItem()
{
	ShowMainPage();
	if (FirstMainButton.IsValid())
	{
		FSlateApplication::Get().SetAllUserFocus(FirstMainButton, EFocusCause::SetDirectly);
	}
}

TSharedPtr<SWidget> SLRPauseMenu::GetInitialFocus() const
{
	return FirstMainButton;
}

void SLRPauseMenu::ShowMainPage()
{
	const bool bWasSettings = Pages.IsValid() && Pages->GetActiveWidgetIndex() == 1;
	if (Pages.IsValid())
	{
		Pages->SetActiveWidgetIndex(0);
	}
	if (bWasSettings && SettingsOpenButton.IsValid())
	{
		FSlateApplication::Get().SetAllUserFocus(SettingsOpenButton, EFocusCause::SetDirectly);
	}
}

void SLRPauseMenu::ShowSettingsPage()
{
	if (Pages.IsValid())
	{
		Pages->SetActiveWidgetIndex(1);
	}
	if (SettingsBackButton.IsValid())
	{
		FSlateApplication::Get().SetAllUserFocus(SettingsBackButton, EFocusCause::SetDirectly);
	}
}

FReply SLRPauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right)
	{
		// A key that is still held from opening the menu repeats: it must not close it again.
		if (InKeyEvent.IsRepeat())
		{
			return FReply::Handled();
		}
		if (Pages.IsValid() && Pages->GetActiveWidgetIndex() == 1)
		{
			ShowMainPage();
		}
		else
		{
			OnResume.ExecuteIfBound();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
