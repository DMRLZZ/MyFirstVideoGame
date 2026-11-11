// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"

/** Look and building blocks shared by the menus of LRUI (pause menu, title screen, settings list). */
namespace LRMenu
{
	inline const FLinearColor Dim(0.f, 0.f, 0.f, 0.6f);
	inline const FLinearColor Panel(0.012f, 0.014f, 0.02f, 0.94f);
	inline const FLinearColor RowIdle(1.f, 1.f, 1.f, 0.04f);
	inline const FLinearColor RowActive(0.95f, 0.42f, 0.05f, 0.95f);
	inline const FLinearColor TextMain(0.95f, 0.95f, 0.95f);
	inline const FLinearColor TextDim(0.55f, 0.57f, 0.6f);
	inline const FLinearColor Accent(1.f, 0.62f, 0.15f);

	/** Default engine font in a style ("Regular", "Bold", "Italic", "BoldItalic"...) and size. */
	FSlateFontInfo Font(const ANSICHAR* Style, int32 Size);

	/** Style shared by every row: a flat white rounded box that each button tints. Lives as long as the module. */
	const FButtonStyle& RowStyle();

	/** "75 %" from 0.75. */
	FText Percent(float Value);

	/** "Sí" / "No". */
	FText OnOff(bool bValue);

	/** Moves Value by Direction steps of Step, snapped to the step grid and clamped to [Min, Max]. */
	float StepValue(float Value, int32 Direction, float Step, float Min, float Max);

	/** A button that is highlighted with focus or the mouse on it, and that can read left / right as value changes. */
	class SMenuRowButton : public SButton
	{
	public:
		SLATE_BEGIN_ARGS(SMenuRowButton) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_EVENT(FSimpleDelegate, OnPressed)
			/** Makes it a value row: receives -1 or +1 from left / right, a click on either half, or Enter (+1). */
			SLATE_ARGUMENT(TFunction<void(int32)>, Step)
			/** Tint while highlighted. */
			SLATE_ARGUMENT_DEFAULT(FLinearColor, ActiveColor) { RowActive };
			/** Tint while idle. */
			SLATE_ARGUMENT_DEFAULT(FLinearColor, IdleColor) { RowIdle };
			SLATE_ARGUMENT_DEFAULT(FMargin, Padding) { FMargin(16.f, 10.f) };
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs);

		/** True while the mouse is over it or it has keyboard / gamepad focus. */
		bool IsHighlighted() const { return IsHovered() || HasKeyboardFocus(); }

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
		virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	private:
		TFunction<void(int32)> Step;
		FSimpleDelegate OnPressedEvent;
		float LastClickFraction = 1.f;
	};

	/** A clickable line with a label. OutButton receives the button when given. */
	TSharedRef<SWidget> MakeButton(const FText& Label, FSimpleDelegate OnPressed, TSharedPtr<SButton>* OutButton = nullptr);

	/** A value line: label on the left, "< value >" on the right. Step receives -1 or +1. */
	TSharedRef<SWidget> MakeRow(const FText& Label, TAttribute<FText> Value, TFunction<void(int32)> Step);

	/** A section title. */
	TSharedRef<SWidget> MakeHeader(const FText& Label);
}
