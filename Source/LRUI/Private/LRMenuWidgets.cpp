// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRMenuWidgets.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LRMenu"

namespace LRMenu
{
	FSlateFontInfo Font(const ANSICHAR* Style, int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Style, Size);
	}

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
		return FText::Format(LOCTEXT("PercentFormat", "{0} %"), FText::AsNumber(FMath::RoundToInt32(Value * 100.f)));
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

	void SMenuRowButton::Construct(const FArguments& InArgs)
	{
		Step = InArgs._Step;
		OnPressedEvent = InArgs._OnPressed;
		const FLinearColor Active = InArgs._ActiveColor;
		const FLinearColor Idle = InArgs._IdleColor;

		SButton::Construct(SButton::FArguments()
			.ButtonStyle(&RowStyle())
			.ContentPadding(InArgs._Padding)
			.ButtonColorAndOpacity_Lambda([this, Active, Idle]()
			{
				return FSlateColor(IsHighlighted() ? Active : Idle);
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

	FReply SMenuRowButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		const float Width = FMath::Max(MyGeometry.GetLocalSize().X, 1.f);
		LastClickFraction = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X / Width;
		return SButton::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	FNavigationReply SMenuRowButton::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
	{
		const EUINavigation Direction = InNavigationEvent.GetNavigationType();
		if (Step && (Direction == EUINavigation::Left || Direction == EUINavigation::Right))
		{
			Step(Direction == EUINavigation::Right ? 1 : -1);
			return FNavigationReply::Stop();
		}
		return SButton::OnNavigation(MyGeometry, InNavigationEvent);
	}

	FReply SMenuRowButton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
	{
		// Enter on a value row moves forward, like a click on its right half.
		if (Step && FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
		{
			Step(1);
			return FReply::Handled();
		}
		return SButton::OnKeyDown(MyGeometry, InKeyEvent);
	}

	TSharedRef<SWidget> MakeButton(const FText& Label, FSimpleDelegate OnPressed, TSharedPtr<SButton>* OutButton)
	{
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

	TSharedRef<SWidget> MakeRow(const FText& Label, TAttribute<FText> Value, TFunction<void(int32)> Step)
	{
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

	TSharedRef<SWidget> MakeHeader(const FText& Label)
	{
		return SNew(SBox).Padding(0.f, 14.f, 0.f, 6.f)
		[
			SNew(STextBlock).Text(Label).Font(Font("Bold", 13)).ColorAndOpacity(FSlateColor(Accent))
		];
	}
}

#undef LOCTEXT_NAMESPACE
