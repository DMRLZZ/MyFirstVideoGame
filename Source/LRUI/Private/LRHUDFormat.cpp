// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRHUDFormat.h"

#include "LRUnits.h"

#define LOCTEXT_NAMESPACE "LRHUD"

namespace LRHUDFormatPrivate
{
	/** Powers of ten for FormatFixed (decimals clamped to [0, 6]). */
	static constexpr int64 PowersOfTen[] = { 1, 10, 100, 1000, 10000, 100000, 1000000 };
}

float FLRHUDFormat::ConvertSpeed(float SpeedKmh, ELRSpeedUnits Units)
{
	if (Units == ELRSpeedUnits::Mph)
	{
		return static_cast<float>(LRUnits::MsToMph(LRUnits::KmhToMs(SpeedKmh)));
	}
	return SpeedKmh;
}

int32 FLRHUDFormat::RoundSpeedForDisplay(float SpeedKmh, ELRSpeedUnits Units)
{
	const float Value = FMath::Abs(ConvertSpeed(SpeedKmh, Units));
	return FMath::Clamp(FMath::RoundToInt32(Value), 0, 99999);
}

FText FLRHUDFormat::FormatSpeed(float SpeedKmh, ELRSpeedUnits Units)
{
	return FText::AsCultureInvariant(LexToString(RoundSpeedForDisplay(SpeedKmh, Units)));
}

FText FLRHUDFormat::GetSpeedUnitLabel(ELRSpeedUnits Units)
{
	return Units == ELRSpeedUnits::Mph
		? LOCTEXT("UnitMph", "MPH")
		: LOCTEXT("UnitKmh", "KM/H");
}

bool FLRHUDFormat::ParseSpeedUnits(const FString& Value, ELRSpeedUnits& OutUnits)
{
	const FString Trimmed = Value.TrimStartAndEnd();
	if (Trimmed.Equals(TEXT("kmh"), ESearchCase::IgnoreCase)
		|| Trimmed.Equals(TEXT("km/h"), ESearchCase::IgnoreCase)
		|| Trimmed.Equals(TEXT("kph"), ESearchCase::IgnoreCase))
	{
		OutUnits = ELRSpeedUnits::Kmh;
		return true;
	}
	if (Trimmed.Equals(TEXT("mph"), ESearchCase::IgnoreCase))
	{
		OutUnits = ELRSpeedUnits::Mph;
		return true;
	}
	return false;
}

FString FLRHUDFormat::GroupThousands(int64 Value, const FString& Separator)
{
	// Work on the unsigned magnitude so INT64_MIN does not overflow.
	const bool bNegative = Value < 0;
	uint64 Magnitude = bNegative ? (static_cast<uint64>(-(Value + 1)) + 1ull) : static_cast<uint64>(Value);

	// At most 20 digits + 6 separators of arbitrary length + sign.
	TCHAR Digits[24];
	int32 NumDigits = 0;
	do
	{
		Digits[NumDigits++] = static_cast<TCHAR>(TEXT('0') + static_cast<int32>(Magnitude % 10ull));
		Magnitude /= 10ull;
	}
	while (Magnitude > 0ull && NumDigits < static_cast<int32>(UE_ARRAY_COUNT(Digits)));

	FString Result;
	Result.Reserve(NumDigits + ((NumDigits - 1) / 3) * Separator.Len() + 1);
	if (bNegative)
	{
		Result.AppendChar(TEXT('-'));
	}
	for (int32 Index = NumDigits - 1; Index >= 0; --Index)
	{
		Result.AppendChar(Digits[Index]);
		if (Index > 0 && Index % 3 == 0)
		{
			Result.Append(Separator);
		}
	}
	return Result;
}

FText FLRHUDFormat::FormatScore(int64 Score, const FString& Separator)
{
	return FText::AsCultureInvariant(GroupThousands(Score, Separator));
}

FText FLRHUDFormat::FormatPoints(int64 Points, const FString& Separator)
{
	if (Points < 0)
	{
		return FText::AsCultureInvariant(GroupThousands(Points, Separator));
	}
	return FText::Format(LOCTEXT("PointsFmt", "+{0}"), FText::AsCultureInvariant(GroupThousands(Points, Separator)));
}

FString FLRHUDFormat::FormatFixed(double Value, int32 Decimals, TCHAR DecimalSeparator)
{
	Decimals = FMath::Clamp(Decimals, 0, 6);
	const int64 Scale = LRHUDFormatPrivate::PowersOfTen[Decimals];

	const bool bNegative = Value < 0.0;
	const double Scaled = FMath::RoundToDouble(FMath::Abs(Value) * static_cast<double>(Scale));
	const int64 ScaledInt = static_cast<int64>(FMath::Min(Scaled, 9.0e18));

	const int64 IntegerPart = ScaledInt / Scale;
	const int64 FractionPart = ScaledInt % Scale;

	FString Result;
	Result.Reserve(24);
	if (bNegative && ScaledInt != 0)
	{
		Result.AppendChar(TEXT('-'));
	}
	Result.Append(LexToString(IntegerPart));
	if (Decimals > 0)
	{
		Result.AppendChar(DecimalSeparator);
		const FString Fraction = LexToString(FractionPart);
		for (int32 Pad = Fraction.Len(); Pad < Decimals; ++Pad)
		{
			Result.AppendChar(TEXT('0'));
		}
		Result.Append(Fraction);
	}
	return Result;
}

FText FLRHUDFormat::FormatDistanceKm(double DistanceMeters, int32 Decimals, TCHAR DecimalSeparator)
{
	FString Number = FormatFixed(DistanceMeters / 1000.0, Decimals, DecimalSeparator);
	return FText::Format(LOCTEXT("DistanceKmFmt", "{0} km"), FText::AsCultureInvariant(MoveTemp(Number)));
}

int32 FLRHUDFormat::ChooseDisplayedGear(int32 ReportedGear, int32 PreviousGear, float SpeedKmh)
{
	constexpr float MovingKmh = 5.f;
	const bool bShifting = ReportedGear == 0 && FMath::Abs(SpeedKmh) > MovingKmh;
	return bShifting && PreviousGear != MIN_int32 && PreviousGear != 0 ? PreviousGear : ReportedGear;
}

FText FLRHUDFormat::FormatGear(int32 Gear)
{
	if (Gear < 0)
	{
		return LOCTEXT("GearReverse", "R");
	}
	if (Gear == 0)
	{
		return LOCTEXT("GearNeutral", "N");
	}
	return FText::AsCultureInvariant(LexToString(Gear));
}

FText FLRHUDFormat::FormatMultiplier(float Multiplier, TCHAR DecimalSeparator)
{
	FString Number = FormatFixed(Multiplier, 2, DecimalSeparator);
	// Trim trailing zeros and a dangling separator ("1,50" -> "1,5", "2,00" -> "2").
	while (Number.Len() > 0 && Number.EndsWith(TEXT("0")) && Number.Contains(FString::Chr(DecimalSeparator)))
	{
		Number.LeftChopInline(1, EAllowShrinking::No);
	}
	if (Number.Len() > 0 && Number[Number.Len() - 1] == DecimalSeparator)
	{
		Number.LeftChopInline(1, EAllowShrinking::No);
	}
	return FText::Format(LOCTEXT("MultiplierFmt", "x{0}"), FText::AsCultureInvariant(MoveTemp(Number)));
}

FText FLRHUDFormat::FormatDuration(float Seconds)
{
	const int32 TotalSeconds = FMath::Max(0, FMath::FloorToInt32(FMath::Max(Seconds, 0.f)));
	const int32 Minutes = TotalSeconds / 60;
	const int32 Remainder = TotalSeconds % 60;
	return FText::AsCultureInvariant(FString::Printf(TEXT("%d:%02d"), Minutes, Remainder));
}

FText FLRHUDFormat::GetNearMissTierLabel(ELRNearMissTier Tier)
{
	switch (Tier)
	{
	case ELRNearMissTier::Close:
		return LOCTEXT("TierClose", "CERCA");
	case ELRNearMissTier::VeryClose:
		return LOCTEXT("TierVeryClose", "MUY CERCA");
	case ELRNearMissTier::Razor:
		return LOCTEXT("TierRazor", "¡AL MILÍMETRO!");
	case ELRNearMissTier::None:
	default:
		return FText::GetEmpty();
	}
}

FText FLRHUDFormat::GetOncomingLabel()
{
	return LOCTEXT("TagOncoming", "CONTRAMANO");
}

FText FLRHUDFormat::GetThreadTheNeedleLabel()
{
	return LOCTEXT("TagThreadTheNeedle", "¡ENTRE DOS!");
}

FText FLRHUDFormat::GetControlSchemeLabel(ELRControlScheme Scheme)
{
	switch (Scheme)
	{
	case ELRControlScheme::Simcade:
		return LOCTEXT("SchemeSimcade", "SIMCADE");
	case ELRControlScheme::Smooth:
		return LOCTEXT("SchemeSmooth", "SUAVE");
	default:
		return LOCTEXT("SchemeArcade", "ARCADE");
	}
}

FText FLRHUDFormat::GetGameModeName(ELRGameModeType Mode)
{
	switch (Mode)
	{
	case ELRGameModeType::EndlessTwoWay:
		return LOCTEXT("ModeTwoWay", "Doble sentido");
	case ELRGameModeType::TimeAttack:
		return LOCTEXT("ModeTimeAttack", "Contrarreloj");
	case ELRGameModeType::FreeRide:
		return LOCTEXT("ModeFreeRide", "Paseo libre");
	case ELRGameModeType::Daily:
		return LOCTEXT("ModeDaily", "Reto diario");
	default:
		return LOCTEXT("ModeOneWay", "Autopista");
	}
}

// --- FLRHUDAnim ------------------------------------------------------------------------------

float FLRHUDAnim::EaseOutCubic(float T)
{
	const float Clamped = FMath::Clamp(T, 0.f, 1.f);
	const float Inverse = 1.f - Clamped;
	return 1.f - Inverse * Inverse * Inverse;
}

float FLRHUDAnim::FadeInOut(float AgeSeconds, float LifetimeSeconds, float FadeInSeconds, float FadeOutSeconds)
{
	if (AgeSeconds < 0.f || AgeSeconds > LifetimeSeconds || LifetimeSeconds <= 0.f)
	{
		return 0.f;
	}
	float Alpha = 1.f;
	if (FadeInSeconds > 0.f && AgeSeconds < FadeInSeconds)
	{
		Alpha = AgeSeconds / FadeInSeconds;
	}
	const float TimeLeft = LifetimeSeconds - AgeSeconds;
	if (FadeOutSeconds > 0.f && TimeLeft < FadeOutSeconds)
	{
		Alpha = FMath::Min(Alpha, TimeLeft / FadeOutSeconds);
	}
	return FMath::Clamp(Alpha, 0.f, 1.f);
}

double FLRHUDAnim::ApproachScore(double Displayed, int64 Target, float RatePerSecond, float DeltaSeconds)
{
	const double TargetValue = static_cast<double>(Target);
	if (RatePerSecond <= 0.f)
	{
		return TargetValue;
	}
	const double Blend = 1.0 - FMath::Exp(-static_cast<double>(RatePerSecond) * FMath::Max(0.0, static_cast<double>(DeltaSeconds)));
	const double Next = Displayed + (TargetValue - Displayed) * Blend;
	return FMath::Abs(TargetValue - Next) < 1.0 ? TargetValue : Next;
}

int32 FLRHUDAnim::GetCountdownNumber(float RemainingSeconds)
{
	if (RemainingSeconds <= 0.f)
	{
		return 0;
	}
	return FMath::Max(1, FMath::CeilToInt32(RemainingSeconds - KINDA_SMALL_NUMBER));
}

float FLRHUDAnim::GetCountdownPhase(float RemainingSeconds)
{
	if (RemainingSeconds <= 0.f)
	{
		return 1.f;
	}
	const float Number = static_cast<float>(GetCountdownNumber(RemainingSeconds));
	return FMath::Clamp(Number - RemainingSeconds, 0.f, 1.f);
}

float FLRHUDAnim::ComputeUIScale(float CanvasHeight, float ReferenceHeight, float MinScale, float MaxScale)
{
	if (ReferenceHeight <= 0.f || CanvasHeight <= 0.f)
	{
		return FMath::Max(MinScale, 0.01f);
	}
	return FMath::Clamp(CanvasHeight / ReferenceHeight, MinScale, FMath::Max(MinScale, MaxScale));
}

#undef LOCTEXT_NAMESPACE
