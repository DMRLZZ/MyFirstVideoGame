// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRScoring.h"

namespace LRScoringPrivate
{
	/** Absorbs the rounding error of summing many tiny fractions before taking whole points. */
	constexpr double WholePointEpsilon = 1.0e-9;
}

const TCHAR* FLRScoringRules::DataPath = TEXT("Gameplay/Scoring.json");

FLRScoringRules::FLRScoringRules()
{
	SpeedTiers.Reserve(4);

	FLRSpeedTier Tier;
	Tier.MinKmh = 0.f;
	Tier.Multiplier = 0.5f;
	SpeedTiers.Add(Tier);

	Tier.MinKmh = 120.f;
	Tier.Multiplier = 1.f;
	SpeedTiers.Add(Tier);

	Tier.MinKmh = 200.f;
	Tier.Multiplier = 1.5f;
	SpeedTiers.Add(Tier);

	Tier.MinKmh = 280.f;
	Tier.Multiplier = 2.f;
	SpeedTiers.Add(Tier);
}

FLRScoringRules FLRScoringRules::Sanitized() const
{
	FLRScoringRules Out = *this;

	Out.MetersPerDistancePoint = FMath::Max(Out.MetersPerDistancePoint, 0.01f);
	Out.NearMissBasePoints = FMath::Max(Out.NearMissBasePoints, 0.f);
	Out.CloseFactor = FMath::Max(Out.CloseFactor, 0.f);
	Out.VeryCloseFactor = FMath::Max(Out.VeryCloseFactor, 0.f);
	Out.RazorFactor = FMath::Max(Out.RazorFactor, 0.f);
	Out.RelativeSpeedReferenceKmh = FMath::Max(Out.RelativeSpeedReferenceKmh, 1.f);
	Out.RelativeSpeedFactorMin = FMath::Max(Out.RelativeSpeedFactorMin, 0.f);
	Out.RelativeSpeedFactorMax = FMath::Max(Out.RelativeSpeedFactorMax, Out.RelativeSpeedFactorMin);
	Out.OncomingFactor = FMath::Max(Out.OncomingFactor, 0.f);
	Out.ThreadTheNeedleFactor = FMath::Max(Out.ThreadTheNeedleFactor, 0.f);
	Out.ComboWindowSeconds = FMath::Max(Out.ComboWindowSeconds, 0.1f);
	Out.ComboStepPerChain = FMath::Max(Out.ComboStepPerChain, 0.f);
	Out.ComboMaxMultiplier = FMath::Max(Out.ComboMaxMultiplier, 1.f);
	Out.ComboBreakSpeedKmh = FMath::Max(Out.ComboBreakSpeedKmh, 0.f);

	if (Out.SpeedTiers.IsEmpty())
	{
		Out.SpeedTiers.AddDefaulted();
	}
	for (FLRSpeedTier& Tier : Out.SpeedTiers)
	{
		Tier.MinKmh = FMath::Max(Tier.MinKmh, 0.f);
		Tier.Multiplier = FMath::Max(Tier.Multiplier, 0.f);
	}
	Out.SpeedTiers.StableSort([](const FLRSpeedTier& A, const FLRSpeedTier& B)
	{
		return A.MinKmh < B.MinKmh;
	});

	return Out;
}

void FLRScoreCalculator::Configure(const FLRScoringRules& InRules)
{
	Rules = InRules.Sanitized();
}

void FLRScoreCalculator::Reset()
{
	State = FLRScoreState();
	bPointsEnabled = true;
}

int64 FLRScoreCalculator::AddDistance(double Meters, float SpeedKmh)
{
	if (Meters <= 0.0)
	{
		return 0;
	}

	State.DistanceMeters += Meters;
	if (!bPointsEnabled)
	{
		return 0;
	}

	State.FractionalDistancePoints += (Meters / Rules.MetersPerDistancePoint) * GetSpeedMultiplier(SpeedKmh);

	const int64 WholePoints = FMath::FloorToInt64(State.FractionalDistancePoints + LRScoringPrivate::WholePointEpsilon);
	if (WholePoints <= 0)
	{
		return 0;
	}

	State.FractionalDistancePoints -= static_cast<double>(WholePoints);
	State.Score += WholePoints;
	return WholePoints;
}

int64 FLRScoreCalculator::ApplyNearMiss(ELRNearMissTier Tier, float RelativeSpeedKmh, bool bOncoming, bool bThreadTheNeedle, float PlayerSpeedKmh)
{
	if (Tier == ELRNearMissTier::None || !bPointsEnabled)
	{
		return 0;
	}

	const double BasePoints = ComputeNearMissPoints(Rules, Tier, RelativeSpeedKmh, bOncoming, bThreadTheNeedle);
	const int64 Points = FMath::RoundToInt64(BasePoints * static_cast<double>(GetComboMultiplier()));

	State.Score += Points;
	++State.NearMisses;

	if (PlayerSpeedKmh >= Rules.ComboBreakSpeedKmh)
	{
		++State.Chain;
		State.BestChain = FMath::Max(State.BestChain, State.Chain);
		State.ComboTimeRemaining = Rules.ComboWindowSeconds;
	}

	return Points;
}

ELRComboTickResult FLRScoreCalculator::Tick(float DeltaSeconds, float SpeedKmh)
{
	const float SafeDelta = FMath::Max(DeltaSeconds, 0.f);
	State.DurationSeconds += SafeDelta;
	State.TopSpeedKmh = FMath::Max(State.TopSpeedKmh, SpeedKmh);

	if (State.Chain <= 0)
	{
		return ELRComboTickResult::None;
	}

	if (SpeedKmh < Rules.ComboBreakSpeedKmh)
	{
		BreakCombo();
		return ELRComboTickResult::Broken;
	}

	State.ComboTimeRemaining -= SafeDelta;
	if (State.ComboTimeRemaining <= 0.f)
	{
		BreakCombo();
		return ELRComboTickResult::Expired;
	}

	return ELRComboTickResult::None;
}

bool FLRScoreCalculator::BreakCombo()
{
	const bool bHadChain = State.Chain > 0;
	State.Chain = 0;
	State.ComboTimeRemaining = 0.f;
	return bHadChain;
}

float FLRScoreCalculator::GetSpeedMultiplier(float SpeedKmh) const
{
	const int32 Index = GetSpeedTierIndex(SpeedKmh);
	return Rules.SpeedTiers.IsValidIndex(Index) ? Rules.SpeedTiers[Index].Multiplier : 1.f;
}

int32 FLRScoreCalculator::FindSpeedTierIndex(const FLRScoringRules& InRules, float SpeedKmh)
{
	int32 Result = 0;
	for (int32 Index = 0; Index < InRules.SpeedTiers.Num(); ++Index)
	{
		if (SpeedKmh < InRules.SpeedTiers[Index].MinKmh)
		{
			break;
		}
		Result = Index;
	}
	return Result;
}

double FLRScoreCalculator::ComputeNearMissPoints(const FLRScoringRules& InRules, ELRNearMissTier Tier, float RelativeSpeedKmh, bool bOncoming, bool bThreadTheNeedle)
{
	double TierFactor = 0.0;
	switch (Tier)
	{
	case ELRNearMissTier::Close:
		TierFactor = InRules.CloseFactor;
		break;
	case ELRNearMissTier::VeryClose:
		TierFactor = InRules.VeryCloseFactor;
		break;
	case ELRNearMissTier::Razor:
		TierFactor = InRules.RazorFactor;
		break;
	case ELRNearMissTier::None:
	default:
		return 0.0;
	}

	const double SpeedFactor = FMath::Clamp(
		static_cast<double>(RelativeSpeedKmh) / static_cast<double>(InRules.RelativeSpeedReferenceKmh),
		static_cast<double>(InRules.RelativeSpeedFactorMin),
		static_cast<double>(InRules.RelativeSpeedFactorMax));

	double Points = static_cast<double>(InRules.NearMissBasePoints) * TierFactor * SpeedFactor;
	if (bOncoming)
	{
		Points *= InRules.OncomingFactor;
	}
	if (bThreadTheNeedle)
	{
		Points *= InRules.ThreadTheNeedleFactor;
	}
	return Points;
}

float FLRScoreCalculator::ComputeComboMultiplier(const FLRScoringRules& InRules, int32 Chain)
{
	const float Multiplier = 1.f + InRules.ComboStepPerChain * static_cast<float>(FMath::Max(Chain, 0));
	return FMath::Min(Multiplier, InRules.ComboMaxMultiplier);
}
