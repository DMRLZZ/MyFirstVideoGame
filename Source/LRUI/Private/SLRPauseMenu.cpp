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
#include "LRMusicSubsystem.h"
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

namespace LRPauseMenuPrivate
{
	const FLinearColor Dim(0.f, 0.f, 0.f, 0.6f);
	const FLinearColor Panel(0.012f, 0.014f, 0.02f, 0.94f);
	const FLinearColor RowIdle(1.f, 1.f, 1.f, 0.04f);
	const FLinearColor RowActive(0.95f, 0.42f, 0.05f, 0.95f);
	const FLinearColor TextMain(0.95f, 0.95f, 0.95f);
	const FLinearColor TextDim(0.55f, 0.57f, 0.6f);
	const FLinearColor Accent(1.f, 0.62f, 0.15f);

	FSlateFontInfo Font(const ANSICHAR* Style, int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Style, Size);
	}

	/** Style shared by every row: a flat white rounded box that each button tints. Lives as long as the module. */
	const FButtonStyle& RowStyle()
	{
		static const FButtonStyle Style = []()
		{
			const FSlateRoundedBoxBrush Box(FLinearColor::White, 6.f);
			return FButtonStyle().SetNormal(Box).SetHovered(Box).SetPressed(Box).SetDisabled(Box)
				.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		}();
		return Style;
	}

	FText Percent(float Value)
	{
		return FText::AsNumber(FMath::RoundToInt32(Value * 100.f)).IsEmpty() ? FText::GetEmpty()
			: FText::Format(LOCTEXT("PercentFormat", "{0} %"), FText::AsNumber(FMath::RoundToInt32(Value * 100.f)));
	}

	FText OnOff(bool bValue)
	{
		return bValue ? LOCTEXT("On", "Sí") : LOCTEXT("Off", "No");
	}

	float StepValue(float Value, int32 Direction, float Step, float Min, float Max)
	{
		const float Steps = FMath::RoundToFloat(Value / Step) + static_cast<float>(Direction);
		return FMath::Clamp(Steps * Step, Min, Max);
	}

	/** A button that is highlighted with focus or the mouse on it, and that can read left / right as value changes. */
	class SMenuRowButton : public SButton
	{
	public:
		SLATE_BEGIN_ARGS(SMenuRowButton) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_EVENT(FSimpleDelegate, OnPressed)
			SLATE_ARGUMENT(TFunction<void(int32)>, Step)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Step = InArgs._Step;
			OnPressedEvent = InArgs._OnPressed;

			SButton::Construct(SButton::FArguments()
				.ButtonStyle(&RowStyle())
				.ContentPadding(FMargin(16.f, 10.f))
				.ButtonColorAndOpacity_Lambda([this]()
				{
					return FSlateColor(IsHovered() || HasKeyboardFocus() ? RowActive : RowIdle);
				})
				.OnClicked_Lambda([this]()
				{
					if (Step)
					{
						// The right half of the row raises the value and the left half lowers it.
						Step(LastClickFraction >= 0.5f ? 1 : -1);
					}
					else
					{
						OnPressedEvent.ExecuteIfBound();
					}
					return FReply::Handled();
				})
				[
					InArgs._Content.Widget
				]);
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			const float Width = FMath::Max(MyGeometry.GetLocalSize().X, 1.f);
			LastClickFraction = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X / Width;
			return SButton::OnMouseButtonDown(MyGeometry, MouseEvent);
		}

		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			SButton::OnMouseEnter(MyGeometry, MouseEvent);
		}

		virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override
		{
			const EUINavigation Direction = InNavigationEvent.GetNavigationType();
			if (Step && (Direction == EUINavigation::Left || Direction == EUINavigation::Right))
			{
				Step(Direction == EUINavigation::Right ? 1 : -1);
				return FNavigationReply::Stop();
			}
			return SButton::OnNavigation(MyGeometry, InNavigationEvent);
		}

		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
		{
			// Enter on a value row moves forward, like a click on its right half.
			if (Step && FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
			{
				Step(1);
				return FReply::Handled();
			}
			return SButton::OnKeyDown(MyGeometry, InKeyEvent);
		}

	private:
		TFunction<void(int32)> Step;
		FSimpleDelegate OnPressedEvent;
		float LastClickFraction = 1.f;
	};
}

void SLRPauseMenu::Construct(const FArguments& InArgs)
{
	using namespace LRPauseMenuPrivate;

	Settings = InArgs._Settings;
	Music = InArgs._Music;
	CarName = InArgs._CarName;
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

	TSharedRef<SVerticalBox> SettingsRows = SNew(SVerticalBox);
	BuildSettingsRows(SettingsRows);

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

TSharedRef<SWidget> SLRPauseMenu::MakeButton(const FText& Label, FSimpleDelegate OnPressed, TSharedPtr<SButton>* OutButton)
{
	using namespace LRPauseMenuPrivate;

	TSharedRef<SMenuRowButton> Button = SNew(SMenuRowButton).OnPressed(OnPressed)
	[
		SNew(STextBlock).Text(Label).Font(Font("Regular", 18)).ColorAndOpacity(FSlateColor(TextMain))
	];
	if (OutButton != nullptr)
	{
		*OutButton = Button;
	}
	return Button;
}

TSharedRef<SWidget> SLRPauseMenu::MakeRow(const FText& Label, TAttribute<FText> Value, TFunction<void(int32)> Step)
{
	using namespace LRPauseMenuPrivate;

	return SNew(SBox).Padding(0.f, 2.f)
	[
		SNew(SMenuRowButton).Step(MoveTemp(Step))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label).Font(Font("Regular", 15)).ColorAndOpacity(FSlateColor(TextMain))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("ArrowLeft", "<  ")).Font(Font("Bold", 15)).ColorAndOpacity(FSlateColor(TextDim))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(260.f).HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(Value).Font(Font("Bold", 15)).ColorAndOpacity(FSlateColor(TextMain)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("ArrowRight", "  >")).Font(Font("Bold", 15)).ColorAndOpacity(FSlateColor(TextDim))
			]
		]
	];
}

TSharedRef<SWidget> SLRPauseMenu::MakeHeader(const FText& Label) const
{
	using namespace LRPauseMenuPrivate;

	return SNew(SBox).Padding(0.f, 14.f, 0.f, 6.f)
	[
		SNew(STextBlock).Text(Label).Font(Font("Bold", 13)).ColorAndOpacity(FSlateColor(Accent))
	];
}

void SLRPauseMenu::BuildSettingsRows(const TSharedRef<SVerticalBox>& Box)
{
	using namespace LRPauseMenuPrivate;

	const auto Add = [&Box](const TSharedRef<SWidget>& Item)
	{
		Box->AddSlot().AutoHeight()[Item];
	};

	Add(MakeHeader(LOCTEXT("HeaderDriving", "CONDUCCIÓN")));
	Add(MakeRow(LOCTEXT("Scheme", "Dirección"),
		TAttribute<FText>::CreateLambda([this]()
		{
			switch (ReadSettings().ControlScheme)
			{
			case ELRControlScheme::Arcade: return LOCTEXT("SchemeLanes", "Por carriles");
			case ELRControlScheme::Simcade: return LOCTEXT("SchemeSim", "Simulación");
			default: return LOCTEXT("SchemeSmoothOption", "Suave");
			}
		}),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S)
			{
				static const ELRControlScheme Order[] = { ELRControlScheme::Smooth, ELRControlScheme::Arcade, ELRControlScheme::Simcade };
				int32 Index = 0;
				for (int32 Candidate = 0; Candidate < UE_ARRAY_COUNT(Order); ++Candidate)
				{
					Index = Order[Candidate] == S.ControlScheme ? Candidate : Index;
				}
				S.ControlScheme = Order[(Index + Direction + UE_ARRAY_COUNT(Order)) % UE_ARRAY_COUNT(Order)];
			});
		}));
	Add(MakeRow(LOCTEXT("Sensitivity", "Sensibilidad"),
		TAttribute<FText>::CreateLambda([this]() { return Percent(ReadSettings().SteeringSensitivity); }),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S) { S.SteeringSensitivity = StepValue(S.SteeringSensitivity, Direction, 0.1f, 0.4f, 1.6f); });
		}));
	Add(MakeRow(LOCTEXT("Car", "Coche"), CarName, [this](int32) { OnNextCar.ExecuteIfBound(); }));

	Add(MakeHeader(LOCTEXT("HeaderSound", "SONIDO")));
	Add(MakeRow(LOCTEXT("MusicOn", "Música"),
		TAttribute<FText>::CreateLambda([this]() { return OnOff(ReadSettings().bMusicEnabled); }),
		[this](int32) { EditSettings([](FLRUserSettings& S) { S.bMusicEnabled = !S.bMusicEnabled; }); }));
	Add(MakeRow(LOCTEXT("MusicVolume", "Volumen de la música"),
		TAttribute<FText>::CreateLambda([this]() { return Percent(ReadSettings().MusicVolume); }),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S) { S.MusicVolume = StepValue(S.MusicVolume, Direction, 0.1f, 0.f, 1.f); });
		}));
	Add(MakeRow(LOCTEXT("EffectsVolume", "Volumen del coche y efectos"),
		TAttribute<FText>::CreateLambda([this]() { return Percent(ReadSettings().EffectsVolume); }),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S) { S.EffectsVolume = StepValue(S.EffectsVolume, Direction, 0.1f, 0.f, 1.f); });
		}));
	Add(MakeRow(LOCTEXT("Track", "Canción"),
		TAttribute<FText>::CreateLambda([this]()
		{
			const ULRMusicSubsystem* Player = Music.Get();
			const FString Name = Player ? Player->GetCurrentTrackName() : FString();
			return Name.IsEmpty() ? LOCTEXT("NoTrack", "Sin música") : FText::FromString(Name);
		}),
		[this](int32 Direction)
		{
			if (ULRMusicSubsystem* Player = Music.Get())
			{
				Direction > 0 ? Player->NextTrack() : Player->PreviousTrack();
			}
		}));
	Add(MakeRow(LOCTEXT("Shuffle", "Orden aleatorio"),
		TAttribute<FText>::CreateLambda([this]() { return OnOff(ReadSettings().bShuffleMusic); }),
		[this](int32) { EditSettings([](FLRUserSettings& S) { S.bShuffleMusic = !S.bShuffleMusic; }); }));
	Add(SNew(SBox).Padding(0.f, 2.f)
	[
		MakeButton(LOCTEXT("AddMusic", "Añadir mi música (abre la carpeta)"), FSimpleDelegate::CreateLambda([this]()
		{
			if (const ULRMusicSubsystem* Player = Music.Get())
			{
				Player->OpenUserMusicFolder();
			}
		}))
	]);
	Add(SNew(SBox).Padding(0.f, 2.f)
	[
		MakeButton(LOCTEXT("Rescan", "Buscar música nueva"), FSimpleDelegate::CreateLambda([this]()
		{
			if (ULRMusicSubsystem* Player = Music.Get())
			{
				Player->RescanTracks();
				FolderNote = FText::Format(LOCTEXT("RescanResult", "{0} canciones en la lista"), FText::AsNumber(Player->GetNumTracks()));
			}
		}))
	]);
	Add(SNew(SBox).Padding(16.f, 2.f, 0.f, 0.f)
	[
		SNew(STextBlock).Text_Lambda([this]() { return FolderNote; }).Font(Font("Regular", 12)).ColorAndOpacity(FSlateColor(TextDim))
	]);

	Add(MakeHeader(LOCTEXT("HeaderGraphics", "GRÁFICOS")));
	Add(MakeRow(LOCTEXT("Quality", "Calidad"),
		TAttribute<FText>::CreateLambda([this]()
		{
			switch (ReadSettings().GraphicsQuality)
			{
			case 0: return LOCTEXT("QualityLow", "Baja");
			case 1: return LOCTEXT("QualityMedium", "Media");
			case 3: return LOCTEXT("QualityEpic", "Épica");
			default: return LOCTEXT("QualityHigh", "Alta");
			}
		}),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S) { S.GraphicsQuality = FMath::Clamp(S.GraphicsQuality + Direction, 0, 3); });
		}));
	Add(MakeRow(LOCTEXT("FrameLimit", "Límite de fotogramas"),
		TAttribute<FText>::CreateLambda([this]()
		{
			const int32 Limit = ReadSettings().FrameRateLimit;
			return Limit <= 0 ? LOCTEXT("NoLimit", "Sin límite") : FText::Format(LOCTEXT("FpsFormat", "{0} fps"), FText::AsNumber(Limit));
		}),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S)
			{
				static const int32 Limits[] = { 30, 60, 90, 120, 144, 0 };
				int32 Index = 1;
				for (int32 Candidate = 0; Candidate < UE_ARRAY_COUNT(Limits); ++Candidate)
				{
					Index = Limits[Candidate] == S.FrameRateLimit ? Candidate : Index;
				}
				S.FrameRateLimit = Limits[FMath::Clamp(Index + Direction, 0, static_cast<int32>(UE_ARRAY_COUNT(Limits)) - 1)];
			});
		}));
	Add(MakeRow(LOCTEXT("Fullscreen", "Pantalla completa"),
		TAttribute<FText>::CreateLambda([this]() { return OnOff(ReadSettings().bFullscreen); }),
		[this](int32) { EditSettings([](FLRUserSettings& S) { S.bFullscreen = !S.bFullscreen; }); }));
	Add(MakeRow(LOCTEXT("MotionBlur", "Desenfoque de movimiento"),
		TAttribute<FText>::CreateLambda([this]() { return OnOff(ReadSettings().bMotionBlur); }),
		[this](int32) { EditSettings([](FLRUserSettings& S) { S.bMotionBlur = !S.bMotionBlur; }); }));
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

void SLRPauseMenu::EditSettings(TFunctionRef<void(FLRUserSettings&)> Change) const
{
	if (ULRUserSettingsSubsystem* Subsystem = Settings.Get())
	{
		FLRUserSettings Edited = Subsystem->GetSettings();
		Change(Edited);
		Subsystem->SetSettings(Edited);
	}
}

FLRUserSettings SLRPauseMenu::ReadSettings() const
{
	const ULRUserSettingsSubsystem* Subsystem = Settings.Get();
	return Subsystem ? Subsystem->GetSettings() : FLRUserSettings();
}

#undef LOCTEXT_NAMESPACE
