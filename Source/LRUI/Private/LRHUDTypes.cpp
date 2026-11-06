// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRHUDTypes.h"

const TCHAR* FLRHUDTuning::DataPath = TEXT("UI/HUD.json");
const TCHAR* FLRHUDGameRules::DataPath = TEXT("Gameplay/GameRules.json");

FLRHUDTuning FLRHUDTuning::Sanitized() const
{
	FLRHUDTuning Out = *this;

	Out.ReferenceHeight = FMath::Clamp(Out.ReferenceHeight, 240.f, 4320.f);
	Out.MinUIScale = FMath::Clamp(Out.MinUIScale, 0.1f, 2.f);
	Out.MaxUIScale = FMath::Clamp(Out.MaxUIScale, Out.MinUIScale, 8.f);
	Out.SafeMarginFraction = FMath::Clamp(Out.SafeMarginFraction, 0.f, 0.2f);

	Out.SpeedFontSize = FMath::Clamp(Out.SpeedFontSize, 6.f, 300.f);
	Out.ScoreFontSize = FMath::Clamp(Out.ScoreFontSize, 6.f, 300.f);
	Out.BannerFontSize = FMath::Clamp(Out.BannerFontSize, 6.f, 400.f);
	Out.LargeFontSize = FMath::Clamp(Out.LargeFontSize, 6.f, 200.f);
	Out.MediumFontSize = FMath::Clamp(Out.MediumFontSize, 6.f, 200.f);
	Out.SmallFontSize = FMath::Clamp(Out.SmallFontSize, 6.f, 200.f);

	Out.PopupLifetimeSeconds = FMath::Clamp(Out.PopupLifetimeSeconds, 0.1f, 10.f);
	Out.PopupFadeInSeconds = FMath::Clamp(Out.PopupFadeInSeconds, 0.f, Out.PopupLifetimeSeconds);
	Out.PopupFadeOutSeconds = FMath::Clamp(Out.PopupFadeOutSeconds, 0.f, Out.PopupLifetimeSeconds);
	Out.PopupRisePixels = FMath::Clamp(Out.PopupRisePixels, 0.f, 500.f);
	Out.PopupSlidePixels = FMath::Clamp(Out.PopupSlidePixels, 0.f, 500.f);
	Out.MaxPopups = FMath::Clamp(Out.MaxPopups, 1, 32);
	Out.PointsAttachWindowSeconds = FMath::Clamp(Out.PointsAttachWindowSeconds, 0.f, 2.f);
	Out.ControlsHintSeconds = FMath::Clamp(Out.ControlsHintSeconds, 0.f, 60.f);
	Out.ControlsHintFadeSeconds = FMath::Clamp(Out.ControlsHintFadeSeconds, 0.f, 10.f);
	Out.GoDisplaySeconds = FMath::Clamp(Out.GoDisplaySeconds, 0.f, 5.f);
	Out.ComboBrokenDisplaySeconds = FMath::Clamp(Out.ComboBrokenDisplaySeconds, 0.f, 5.f);
	Out.ScoreRollRate = FMath::Clamp(Out.ScoreRollRate, 0.f, 100.f);

	Out.RedlineFraction = FMath::Clamp(Out.RedlineFraction, 0.3f, 1.f);
	if (Out.DecimalSeparator.IsEmpty())
	{
		Out.DecimalSeparator = TEXT(",");
	}
	return Out;
}
