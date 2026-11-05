// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRVehicleMath.h"

#include "LRRoadTypes.h"
#include "LRUnits.h"
#include "LRVehicleTypes.h"

namespace LRVehicleMath
{
	float EvaluateCurve(TConstArrayView<FVector2D> Points, float X, float DefaultValue)
	{
		const int32 Num = Points.Num();
		if (Num == 0)
		{
			return DefaultValue;
		}
		if (X <= Points[0].X)
		{
			return static_cast<float>(Points[0].Y);
		}
		for (int32 Index = 1; Index < Num; ++Index)
		{
			const FVector2D& A = Points[Index - 1];
			const FVector2D& B = Points[Index];
			if (X <= B.X)
			{
				const double Span = B.X - A.X;
				const double Alpha = Span > UE_DOUBLE_SMALL_NUMBER ? (X - A.X) / Span : 1.0;
				return static_cast<float>(FMath::Lerp(A.Y, B.Y, Alpha));
			}
		}
		return static_cast<float>(Points[Num - 1].Y);
	}

	float WrapAngleRad(float AngleRad)
	{
		return FMath::UnwindRadians(AngleRad);
	}

	float ComputeTopSpeedThrottleScale(float SpeedKmh, float TopSpeedKmh, float FadeFraction)
	{
		if (TopSpeedKmh <= 0.f || SpeedKmh <= 0.f)
		{
			return 1.f;
		}
		const float Band = FMath::Max(TopSpeedKmh * FMath::Clamp(FadeFraction, 0.001f, 0.5f), 0.1f);
		return FMath::Clamp((TopSpeedKmh - SpeedKmh) / Band, 0.f, 1.f);
	}

	float ComputeEngineRpm(const FLRTransmissionParams& Transmission, float WheelRadiusCm, float SpeedMs, int32 Gear)
	{
		if (Transmission.ForwardGearRatios.Num() == 0 || WheelRadiusCm <= 0.f)
		{
			return 0.f;
		}
		const int32 GearIndex = FMath::Clamp(Gear, 1, Transmission.ForwardGearRatios.Num()) - 1;
		const double WheelRadiusM = LRUnits::CmToMeters(WheelRadiusCm);
		const double WheelOmega = FMath::Abs(SpeedMs) / WheelRadiusM;
		const double EngineOmega = WheelOmega * Transmission.ForwardGearRatios[GearIndex] * Transmission.FinalDriveRatio;
		return static_cast<float>(EngineOmega * 60.0 / UE_DOUBLE_TWO_PI);
	}

	int32 SelectGearForSpeed(const FLRTransmissionParams& Transmission, float WheelRadiusCm, float SpeedMs)
	{
		const int32 NumGears = Transmission.ForwardGearRatios.Num();
		if (NumGears == 0)
		{
			return 1;
		}
		for (int32 Gear = 1; Gear <= NumGears; ++Gear)
		{
			if (ComputeEngineRpm(Transmission, WheelRadiusCm, SpeedMs, Gear) < Transmission.ChangeUpRpm)
			{
				return Gear;
			}
		}
		return NumGears;
	}

	float ComputeImpactSpeedKmh(const FVector& RelativeVelocityCms, const FVector& Normal)
	{
		const FVector SafeNormal = Normal.GetSafeNormal();
		if (SafeNormal.IsNearlyZero())
		{
			return static_cast<float>(LRUnits::CmsToKmh(RelativeVelocityCms.Size()));
		}
		return static_cast<float>(LRUnits::CmsToKmh(FMath::Abs(FVector::DotProduct(RelativeVelocityCms, SafeNormal))));
	}

	float ComputeSmoothingAlpha(float DeltaSeconds, float TimeConstantSeconds)
	{
		if (TimeConstantSeconds <= 0.f)
		{
			return 1.f;
		}
		if (DeltaSeconds <= 0.f)
		{
			return 0.f;
		}
		return 1.f - FMath::Exp(-DeltaSeconds / TimeConstantSeconds);
	}

	int32 FindNearestLane(TConstArrayView<FLRLaneDesc> Lanes, float D)
	{
		int32 Best = INDEX_NONE;
		float BestDistance = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Lanes.Num(); ++Index)
		{
			const float Distance = FMath::Abs(D - Lanes[Index].CenterD);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Index;
			}
		}
		return Best;
	}

	int32 UpdateCurrentLane(TConstArrayView<FLRLaneDesc> Lanes, int32 CurrentLane, float D, float HysteresisMeters)
	{
		const int32 Nearest = FindNearestLane(Lanes, D);
		if (Nearest == INDEX_NONE || !Lanes.IsValidIndex(CurrentLane) || Nearest == CurrentLane)
		{
			return Nearest;
		}
		const float CurrentDistance = FMath::Abs(D - Lanes[CurrentLane].CenterD);
		const float NearestDistance = FMath::Abs(D - Lanes[Nearest].CenterD);
		return (NearestDistance + FMath::Max(HysteresisMeters, 0.f) < CurrentDistance) ? Nearest : CurrentLane;
	}

	int32 StepTargetLane(TConstArrayView<FLRLaneDesc> Lanes, int32 TargetLane, int32 Direction, bool bAllowOncoming)
	{
		if (!Lanes.IsValidIndex(TargetLane) || Direction == 0)
		{
			return TargetLane;
		}
		const int32 Candidate = TargetLane + FMath::Sign(Direction);
		if (!Lanes.IsValidIndex(Candidate))
		{
			return TargetLane;
		}
		if (!bAllowOncoming && Lanes[Candidate].Direction < 0)
		{
			return TargetLane;
		}
		return Candidate;
	}

	namespace
	{
		void ValidateAxle(const FLRWheelAxleParams& Axle, const TCHAR* Label, bool bSteered, TArray<FString>& OutProblems)
		{
			if (Axle.BoneNames.Num() == 0)
			{
				OutProblems.Add(FString::Printf(TEXT("%s: no wheel bones"), Label));
			}
			for (const FName& Bone : Axle.BoneNames)
			{
				if (Bone.IsNone())
				{
					OutProblems.Add(FString::Printf(TEXT("%s: empty bone name"), Label));
				}
			}
			if (Axle.RadiusCm < 15.f || Axle.RadiusCm > 80.f)
			{
				OutProblems.Add(FString::Printf(TEXT("%s: RadiusCm %.1f outside [15, 80]"), Label, Axle.RadiusCm));
			}
			if (Axle.FrictionForceMultiplier <= 0.f)
			{
				OutProblems.Add(FString::Printf(TEXT("%s: FrictionForceMultiplier must be positive"), Label));
			}
			if (Axle.CorneringStiffness <= 0.f)
			{
				OutProblems.Add(FString::Printf(TEXT("%s: CorneringStiffness must be positive"), Label));
			}
			if (bSteered && (Axle.MaxSteerAngleDeg < 5.f || Axle.MaxSteerAngleDeg > 60.f))
			{
				OutProblems.Add(FString::Printf(TEXT("%s: MaxSteerAngleDeg %.1f outside [5, 60]"), Label, Axle.MaxSteerAngleDeg));
			}
			if (Axle.MaxBrakeTorqueNm <= 0.f)
			{
				OutProblems.Add(FString::Printf(TEXT("%s: MaxBrakeTorqueNm must be positive"), Label));
			}
			if (Axle.SpringRate <= 0.f || Axle.SuspensionDampingRatio <= 0.f)
			{
				OutProblems.Add(FString::Printf(TEXT("%s: spring rate and damping must be positive"), Label));
			}
		}

		bool IsSortedByX(TConstArrayView<FVector2D> Points)
		{
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				if (Points[Index].X <= Points[Index - 1].X)
				{
					return false;
				}
			}
			return true;
		}
	}

	bool ValidateDefinition(const FLRVehicleDefinition& Def, TArray<FString>& OutProblems)
	{
		const int32 InitialCount = OutProblems.Num();

		if (Def.Id.IsNone())
		{
			OutProblems.Add(TEXT("Id is empty"));
		}
		if (Def.DisplayName.IsEmpty())
		{
			OutProblems.Add(TEXT("DisplayName is empty"));
		}
		if (Def.SkeletalMesh.IsNull())
		{
			OutProblems.Add(TEXT("SkeletalMesh is empty"));
		}
		if (Def.MassKg < 500.f || Def.MassKg > 5000.f)
		{
			OutProblems.Add(FString::Printf(TEXT("MassKg %.0f outside [500, 5000]"), Def.MassKg));
		}
		if (Def.LengthMeters < 2.f || Def.LengthMeters > 8.f || Def.WidthMeters < 1.2f || Def.WidthMeters > 3.f)
		{
			OutProblems.Add(TEXT("Footprint outside plausible car sizes"));
		}
		if (Def.WheelbaseMeters <= 0.f || Def.WheelbaseMeters >= Def.LengthMeters)
		{
			OutProblems.Add(TEXT("WheelbaseMeters must be positive and shorter than the body"));
		}
		if (Def.TopSpeedKmh < 60.f || Def.TopSpeedKmh > 600.f)
		{
			OutProblems.Add(FString::Printf(TEXT("TopSpeedKmh %.0f outside [60, 600]"), Def.TopSpeedKmh));
		}
		if (Def.CrashImpactKmh <= Def.CrashMinReportKmh)
		{
			OutProblems.Add(TEXT("CrashImpactKmh must be above CrashMinReportKmh"));
		}

		// Engine.
		if (Def.Engine.MaxTorqueNm <= 0.f)
		{
			OutProblems.Add(TEXT("Engine.MaxTorqueNm must be positive"));
		}
		if (Def.Engine.IdleRpm <= 0.f || Def.Engine.IdleRpm >= Def.Engine.MaxRpm)
		{
			OutProblems.Add(TEXT("Engine.IdleRpm must be in (0, MaxRpm)"));
		}
		if (Def.Engine.TorqueCurve.Num() < 2 || !IsSortedByX(Def.Engine.TorqueCurve))
		{
			OutProblems.Add(TEXT("Engine.TorqueCurve needs at least 2 points sorted by X"));
		}
		for (const FVector2D& Point : Def.Engine.TorqueCurve)
		{
			if (Point.X < 0.0 || Point.X > 1.0 || Point.Y < 0.0 || Point.Y > 1.0)
			{
				OutProblems.Add(TEXT("Engine.TorqueCurve points must be normalized to [0, 1]"));
				break;
			}
		}

		// Transmission.
		const TArray<float>& Ratios = Def.Transmission.ForwardGearRatios;
		if (Ratios.Num() == 0)
		{
			OutProblems.Add(TEXT("Transmission.ForwardGearRatios is empty"));
		}
		for (int32 Index = 0; Index < Ratios.Num(); ++Index)
		{
			if (Ratios[Index] <= 0.f || (Index > 0 && Ratios[Index] >= Ratios[Index - 1]))
			{
				OutProblems.Add(TEXT("Transmission.ForwardGearRatios must be positive and strictly decreasing"));
				break;
			}
		}
		if (Def.Transmission.ChangeDownRpm >= Def.Transmission.ChangeUpRpm || Def.Transmission.ChangeUpRpm > Def.Engine.MaxRpm)
		{
			OutProblems.Add(TEXT("Transmission shift points must satisfy ChangeDownRpm < ChangeUpRpm <= Engine.MaxRpm"));
		}

		ValidateAxle(Def.FrontWheels, TEXT("FrontWheels"), true, OutProblems);
		ValidateAxle(Def.RearWheels, TEXT("RearWheels"), false, OutProblems);

		// Assists.
		if (!IsSortedByX(Def.StabilityAssist.SteeringScaleCurve))
		{
			OutProblems.Add(TEXT("StabilityAssist.SteeringScaleCurve must be sorted by X"));
		}
		for (int32 Index = 0; Index < Def.StabilityAssist.SteeringScaleCurve.Num(); ++Index)
		{
			const FVector2D& Point = Def.StabilityAssist.SteeringScaleCurve[Index];
			if (Point.Y <= 0.0 || Point.Y > 1.0
				|| (Index > 0 && Point.Y > Def.StabilityAssist.SteeringScaleCurve[Index - 1].Y))
			{
				OutProblems.Add(TEXT("StabilityAssist.SteeringScaleCurve must be in (0, 1] and non-increasing"));
				break;
			}
		}
		if (Def.LaneAssist.LateralGain <= 0.f || Def.LaneAssist.MaxLateralSpeedMs <= 0.f || Def.LaneAssist.HeadingGain <= 0.f)
		{
			OutProblems.Add(TEXT("LaneAssist gains must be positive"));
		}

		// The gearing must be able to reach the advertised top speed.
		const float GearingTopSpeed = ComputeGearingTopSpeedKmh(Def);
		if (GearingTopSpeed < Def.TopSpeedKmh)
		{
			OutProblems.Add(FString::Printf(TEXT("Gearing tops out at %.0f km/h, below TopSpeedKmh %.0f"), GearingTopSpeed, Def.TopSpeedKmh));
		}

		return OutProblems.Num() == InitialCount;
	}

	float ComputeGearingTopSpeedKmh(const FLRVehicleDefinition& Def)
	{
		const TArray<float>& Ratios = Def.Transmission.ForwardGearRatios;
		if (Ratios.Num() == 0 || Ratios.Last() <= 0.f || Def.Transmission.FinalDriveRatio <= 0.f)
		{
			return 0.f;
		}
		const double EngineOmega = Def.Engine.MaxRpm * UE_DOUBLE_TWO_PI / 60.0;
		const double WheelOmega = EngineOmega / (Ratios.Last() * Def.Transmission.FinalDriveRatio);
		const double SpeedMs = WheelOmega * LRUnits::CmToMeters(Def.RearWheels.RadiusCm);
		return static_cast<float>(LRUnits::MsToKmh(SpeedMs));
	}
}
