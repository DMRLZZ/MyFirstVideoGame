// Copyright LEFTRIGHT. All Rights Reserved.

#include "SLRTitleMenu.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "LRHUDFormat.h"
#include "LRMenuWidgets.h"
#include "Rendering/DrawElements.h"
#include "SLRSettingsList.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LRTitleMenu"

namespace LRTitleMenuPrivate
{
	/** Length of the entrance animation, seconds. */
	constexpr float EntranceSeconds = 0.7f;

	/** How far the column slides in from the left during the entrance, pixels. */
	constexpr float EntranceSlide = 60.f;

	/** Slant of the logo letters (horizontal shear). */
	constexpr float LogoShear = -0.18f;

	const FLinearColor PlayIdle(1.f, 0.62f, 0.15f, 0.82f);
	const FLinearColor PlayActive(1.f, 0.74f, 0.32f, 1.f);
	const FLinearColor PlayText(0.03f, 0.03f, 0.04f);

	/** A gradient stop: fraction 0..1 of the widget size along the gradient, and color. */
	using FGradientStop = TPair<float, FLinearColor>;

	/** A rectangle filled with a gradient. Stops are given as fractions of the widget size along the gradient. */
	class SGradient : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGradient) {}
			SLATE_ARGUMENT(TArray<FGradientStop>, Stops)
			/** Orient_Vertical varies left to right, Orient_Horizontal top to bottom. */
			SLATE_ARGUMENT_DEFAULT(EOrientation, Orientation) { Orient_Vertical };
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Stops = InArgs._Stops;
			Orientation = InArgs._Orientation;
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2f Size = AllottedGeometry.GetLocalSize();
			TArray<FSlateGradientStop> GradientStops;
			GradientStops.Reserve(Stops.Num());
			for (const FGradientStop& Stop : Stops)
			{
				FLinearColor Color = Stop.Value;
				Color.A *= InWidgetStyle.GetColorAndOpacityTint().A;
				GradientStops.Emplace(Size * Stop.Key, Color);
			}
			FSlateDrawElement::MakeGradient(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), GradientStops, Orientation);
			return LayerId;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D::ZeroVector;
		}

	private:
		TArray<FGradientStop> Stops;
		EOrientation Orientation = Orient_Vertical;
	};

	/** Font with extra letter spacing (1/1000 em). */
	FSlateFontInfo Spaced(const ANSICHAR* Style, int32 Size, int32 LetterSpacing)
	{
		FSlateFontInfo Info = LRMenu::Font(Style, Size);
		Info.LetterSpacing = LetterSpacing;
		return Info;
	}
}

void SLRTitleMenu::Construct(const FArguments& InArgs)
{
	using namespace LRMenu;
	using namespace LRTitleMenuPrivate;

	OnPlay = InArgs._OnPlay;
	OnQuit = InArgs._OnQuit;
	OnCycleMode = InArgs._OnCycleMode;
	OnCycleCar = InArgs._OnCycleCar;
	EntranceStartTime = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;

	static const FSlateColorBrush WhiteBrush(FLinearColor::White);

	// --- Main page -------------------------------------------------------------------------------
	TSharedRef<SVerticalBox> MainPage = SNew(SVerticalBox);
	MainPage->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
	[
		SAssignNew(PlayButton, SMenuRowButton)
		.OnPressed(FSimpleDelegate::CreateLambda([this]() { OnPlay.ExecuteIfBound(); }))
		.IdleColor(PlayIdle)
		.ActiveColor(PlayActive)
		.Padding(FMargin(22.f, 14.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("Play", "JUGAR")).Font(Spaced("Black", 28, 120)).ColorAndOpacity(FSlateColor(PlayText))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("PlayArrow", ">>")).Font(Font("Black", 22)).ColorAndOpacity(FSlateColor(PlayText))
				.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform>
				{
					// The arrows nudge forward while "Play" is highlighted.
					const bool bHighlighted = PlayButton.IsValid() && StaticCastSharedPtr<SMenuRowButton>(PlayButton)->IsHighlighted();
					const double Time = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;
					const float Offset = bHighlighted ? 6.f * (0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * 6.f)) : 0.f;
					return FSlateRenderTransform(FVector2f(Offset, 0.f));
				})
			]
		]
	];
	MainPage->AddSlot().AutoHeight()
	[
		MakeRow(LOCTEXT("Mode", "Modo"), InArgs._ModeName, [this](int32 Direction) { if (OnCycleMode) { OnCycleMode(Direction); } })
	];
	MainPage->AddSlot().AutoHeight()
	[
		MakeRow(LOCTEXT("Car", "Coche"), InArgs._CarName, [this](int32 Direction) { if (OnCycleCar) { OnCycleCar(Direction); } })
	];
	MainPage->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)
	[
		MakeButton(LOCTEXT("Settings", "Ajustes"), FSimpleDelegate::CreateSP(this, &SLRTitleMenu::ShowSettingsPage), &SettingsOpenButton)
	];
	MainPage->AddSlot().AutoHeight().Padding(0.f, 4.f)
	[
		MakeButton(LOCTEXT("Quit", "Salir del juego"), FSimpleDelegate::CreateLambda([this]() { OnQuit.ExecuteIfBound(); }))
	];
	MainPage->AddSlot().AutoHeight().Padding(4.f, 26.f, 0.f, 0.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(LOCTEXT("Best", "RÉCORD")).Font(Spaced("Bold", 12, 250)).ColorAndOpacity(FSlateColor(TextDim))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(InArgs._BestScore).Font(Font("Black", 30)).ColorAndOpacity(FSlateColor(Accent))
		]
	];

	// --- Settings page ---------------------------------------------------------------------------
	TSharedRef<SVerticalBox> SettingsPage = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(STextBlock).Text(LOCTEXT("SettingsTitle", "AJUSTES")).Font(Spaced("Bold", 18, 200)).ColorAndOpacity(FSlateColor(Accent))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(560.f)
			[
				SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
				+ SScrollBox::Slot()
				[
					SNew(SLRSettingsList).Settings(InArgs._Settings).Music(InArgs._Music)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
		[
			MakeButton(LOCTEXT("Back", "Volver"), FSimpleDelegate::CreateSP(this, &SLRTitleMenu::ShowMainPage), &SettingsBackButton)
		];

	// --- Logo ------------------------------------------------------------------------------------
	const FSlateRenderTransform Slant(FShear2D(LogoShear, 0.f));
	TSharedRef<SWidget> Logo = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(LOCTEXT("LogoLeft", "LEFT")).Font(Font("Black", 76)).ColorAndOpacity(FSlateColor(TextMain))
				.RenderTransform(Slant).RenderTransformPivot(FVector2D(0.f, 1.f))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(LOCTEXT("LogoRight", "RIGHT")).Font(Font("Black", 76)).ColorAndOpacity(FSlateColor(Accent))
				.RenderTransform(Slant).RenderTransformPivot(FVector2D(0.f, 1.f))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(64.f).HeightOverride(4.f)
				[
					SNew(SBorder).BorderImage(&WhiteBrush).BorderBackgroundColor(Accent)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("Tagline", "ESQUIVA  ·  ARRIESGA  ·  PUNTÚA")).Font(Spaced("Bold", 13, 300)).ColorAndOpacity(FSlateColor(TextDim))
			]
		];

	// --- Layout ----------------------------------------------------------------------------------
	ChildSlot
	[
		SNew(SOverlay)
		// Dark side panel that fades into the scene, and a darker floor for the hints.
		+ SOverlay::Slot()
		[
			SNew(SGradient).Orientation(Orient_Vertical).Stops({
				{ 0.f, FLinearColor(0.f, 0.f, 0.f, 0.88f) },
				{ 0.32f, FLinearColor(0.f, 0.f, 0.f, 0.72f) },
				{ 0.62f, FLinearColor(0.f, 0.f, 0.f, 0.f) },
				{ 1.f, FLinearColor(0.f, 0.f, 0.f, 0.f) } })
		]
		+ SOverlay::Slot()
		[
			SNew(SGradient).Orientation(Orient_Horizontal).Stops({
				{ 0.f, FLinearColor(0.f, 0.f, 0.f, 0.f) },
				{ 0.78f, FLinearColor(0.f, 0.f, 0.f, 0.f) },
				{ 1.f, FLinearColor(0.f, 0.f, 0.f, 0.55f) } })
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(FMargin(96.f, 40.f, 40.f, 80.f))
		[
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(0.f)
			.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, GetEntranceAlpha()); })
			.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform>
			{
				return FSlateRenderTransform(FVector2f(-EntranceSlide * (1.f - GetEntranceAlpha()), 0.f));
			})
			[
				SNew(SBox).WidthOverride(560.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 40.f)
					[
						Logo
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(Pages, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()[MainPage]
						+ SWidgetSwitcher::Slot()[SettingsPage]
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(96.f, 0.f, 0.f, 36.f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Hints", "Enter / A  aceptar        < >  cambiar        Esc / B  volver"))
			.Font(Spaced("Regular", 13, 60))
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(LRMenu::TextDim.CopyWithNewOpacity(GetEntranceAlpha())); })
		]
	];
}

float SLRTitleMenu::GetEntranceAlpha() const
{
	const double Now = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : EntranceStartTime;
	const float T = static_cast<float>(Now - EntranceStartTime) / LRTitleMenuPrivate::EntranceSeconds;
	return FLRHUDAnim::EaseOutCubic(FMath::Clamp(T, 0.f, 1.f));
}

void SLRTitleMenu::FocusFirstItem()
{
	if (FSlateApplication::IsInitialized())
	{
		EntranceStartTime = FSlateApplication::Get().GetCurrentTime();
	}
	if (Pages.IsValid())
	{
		Pages->SetActiveWidgetIndex(0);
	}
	if (PlayButton.IsValid())
	{
		FSlateApplication::Get().SetAllUserFocus(PlayButton, EFocusCause::SetDirectly);
	}
}

TSharedPtr<SWidget> SLRTitleMenu::GetInitialFocus() const
{
	return PlayButton;
}

void SLRTitleMenu::ShowMainPage()
{
	if (Pages.IsValid())
	{
		Pages->SetActiveWidgetIndex(0);
	}
	if (SettingsOpenButton.IsValid())
	{
		FSlateApplication::Get().SetAllUserFocus(SettingsOpenButton, EFocusCause::SetDirectly);
	}
}

void SLRTitleMenu::ShowSettingsPage()
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

FReply SLRTitleMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right)
	{
		// Back from the settings. On the main page there is nothing to go back to: quitting needs the "Quit" row.
		if (!InKeyEvent.IsRepeat() && Pages.IsValid() && Pages->GetActiveWidgetIndex() == 1)
		{
			ShowMainPage();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
