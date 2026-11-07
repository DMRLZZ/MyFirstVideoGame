// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRGameRules.h"

#include "LRRandom.h"
#include "LRRoadTypes.h"

const TCHAR* FLRGameRules::DataPath = TEXT("Gameplay/GameRules.json");

FLRGameRules FLRGameRules::Sanitized() const
{
	FLRGameRules Out = *this;

	Out.CountdownSeconds = FMath::Max(Out.CountdownSeconds, 0.f);
	Out.ResultsDelaySeconds = FMath::Max(Out.ResultsDelaySeconds, 0.f);
	Out.StartSpeedKmh = FMath::Max(Out.StartSpeedKmh, 0.f);
	Out.ScrapesAllowed = FMath::Max(Out.ScrapesAllowed, 0);
	Out.ScrapeMaxImpactKmh = FMath::Max(Out.ScrapeMaxImpactKmh, 0.f);
	Out.ForgivingFatalImpactKmh = FMath::Max(Out.ForgivingFatalImpactKmh, 0.f);
	Out.CrashRecoverySeconds = FMath::Max(Out.CrashRecoverySeconds, 0.01f);
	Out.FreeRideResetForwardMeters = FMath::Max(Out.FreeRideResetForwardMeters, 0.f);

	Out.Rumble.NearMissIntensity = FMath::Clamp(Out.Rumble.NearMissIntensity, 0.f, 1.f);
	Out.Rumble.NearMissDurationSeconds = FMath::Max(Out.Rumble.NearMissDurationSeconds, 0.f);
	Out.Rumble.ScrapeIntensity = FMath::Clamp(Out.Rumble.ScrapeIntensity, 0.f, 1.f);
	Out.Rumble.ScrapeDurationSeconds = FMath::Max(Out.Rumble.ScrapeDurationSeconds, 0.f);
	Out.Rumble.CrashIntensity = FMath::Clamp(Out.Rumble.CrashIntensity, 0.f, 1.f);
	Out.Rumble.CrashDurationSeconds = FMath::Max(Out.Rumble.CrashDurationSeconds, 0.f);

	return Out;
}

ELRCrashOutcome FLRGameRulesLogic::EvaluateCrash(const FLRGameRules& Rules, bool bFreeRide, bool bSevere, float ImpactSpeedKmh, int32 ScrapesUsed)
{
	if (bFreeRide)
	{
		return bSevere ? ELRCrashOutcome::ResetCar : ELRCrashOutcome::Scrape;
	}

	if (!Rules.bForgivingArcade)
	{
		return bSevere ? ELRCrashOutcome::EndRun : ELRCrashOutcome::Scrape;
	}

	if (Rules.ForgivingFatalImpactKmh > 0.f && ImpactSpeedKmh >= Rules.ForgivingFatalImpactKmh)
	{
		return ELRCrashOutcome::EndRun;
	}
	if (ImpactSpeedKmh <= Rules.ScrapeMaxImpactKmh)
	{
		return ELRCrashOutcome::Scrape;
	}
	return ScrapesUsed < Rules.ScrapesAllowed ? ELRCrashOutcome::DamageScrape : ELRCrashOutcome::EndRun;
}

int32 FLRGameRulesLogic::ResolveStartLane(const TArray<FLRLaneDesc>& Lanes, int32 DefaultLane, int32 LaneOffset)
{
	if (Lanes.IsEmpty())
	{
		return INDEX_NONE;
	}

	const int32 LastLane = Lanes.Num() - 1;
	const int32 SafeDefault = FMath::Clamp(DefaultLane, 0, LastLane);
	int32 Lane = FMath::Clamp(SafeDefault + LaneOffset, 0, LastLane);

	const int32 StepBack = Lane > SafeDefault ? -1 : 1;
	while (Lane != SafeDefault && Lanes[Lane].Direction < 0)
	{
		Lane += StepBack;
	}
	return Lane;
}

bool FLRGameRulesLogic::ParseGameMode(const FString& Text, ELRGameModeType& OutMode)
{
	if (Text.Equals(TEXT("EndlessOneWay"), ESearchCase::IgnoreCase))
	{
		OutMode = ELRGameModeType::EndlessOneWay;
		return true;
	}
	if (Text.Equals(TEXT("EndlessTwoWay"), ESearchCase::IgnoreCase))
	{
		OutMode = ELRGameModeType::EndlessTwoWay;
		return true;
	}
	if (Text.Equals(TEXT("FreeRide"), ESearchCase::IgnoreCase))
	{
		OutMode = ELRGameModeType::FreeRide;
		return true;
	}
	if (Text.Equals(TEXT("Daily"), ESearchCase::IgnoreCase))
	{
		OutMode = ELRGameModeType::Daily;
		return true;
	}
	return false;
}

bool FLRGameRulesLogic::ParseControlScheme(const FString& Text, ELRControlScheme& OutScheme)
{
	if (Text.Equals(TEXT("Arcade"), ESearchCase::IgnoreCase))
	{
		OutScheme = ELRControlScheme::Arcade;
		return true;
	}
	if (Text.Equals(TEXT("Smooth"), ESearchCase::IgnoreCase))
	{
		OutScheme = ELRControlScheme::Smooth;
		return true;
	}
	if (Text.Equals(TEXT("Simcade"), ESearchCase::IgnoreCase))
	{
		OutScheme = ELRControlScheme::Simcade;
		return true;
	}
	return false;
}

int32 FLRGameRulesLogic::MakeRunSeed(ELRGameModeType Mode, const FDateTime& UtcNow)
{
	if (Mode == ELRGameModeType::Daily)
	{
		return LRRandom::DailySeed(UtcNow.GetYear(), UtcNow.GetMonth(), UtcNow.GetDay());
	}

	const uint64 Ticks = static_cast<uint64>(UtcNow.GetTicks());
	return static_cast<int32>(static_cast<uint32>(Ticks ^ (Ticks >> 32)));
}
