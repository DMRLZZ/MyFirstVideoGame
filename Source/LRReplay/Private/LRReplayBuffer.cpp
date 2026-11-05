// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRReplayBuffer.h"

namespace LRReplayBufferPrivate
{
	/** Orders traffic snapshots by ascending Id. */
	struct FByIdAscending
	{
		bool operator()(const FLRTrafficVehicleSnapshot& Left, const FLRTrafficVehicleSnapshot& Right) const
		{
			return Left.Id < Right.Id;
		}
	};

	/** Copies every scalar field of a frame (everything except Traffic). */
	void CopyScalars(const FLRReplayFrame& Source, FLRReplayFrame& Dest)
	{
		Dest.Time = Source.Time;
		Dest.PlayerTransform = Source.PlayerTransform;
		Dest.PlayerRoadCoord = Source.PlayerRoadCoord;
		Dest.PlayerSpeedKmh = Source.PlayerSpeedKmh;
		Dest.EngineRpm = Source.EngineRpm;
		Dest.SteeringInput = Source.SteeringInput;
		Dest.ThrottleInput = Source.ThrottleInput;
		Dest.BrakeInput = Source.BrakeInput;
		Dest.Gear = Source.Gear;
	}

	/** Copies a whole frame reusing the destination Traffic allocation when it is large enough. */
	void CopyFrame(const FLRReplayFrame& Source, FLRReplayFrame& Dest)
	{
		CopyScalars(Source, Dest);
		Dest.Traffic.Reset();
		Dest.Traffic.Append(Source.Traffic.GetData(), Source.Traffic.Num());
	}
}

// ------------------------------------------------------------------------------------------------
// FLRReplaySampleClock
// ------------------------------------------------------------------------------------------------

void FLRReplaySampleClock::Configure(float SampleRateHz)
{
	Period = 1.f / FMath::Max(SampleRateHz, 1.f);
	Reset();
}

void FLRReplaySampleClock::Reset()
{
	Time = 0.f;
	Accumulator = 0.f;
	bFirstSample = true;
}

bool FLRReplaySampleClock::Advance(float DeltaSeconds)
{
	const float SafeDelta = FMath::Max(DeltaSeconds, 0.f);
	Time += SafeDelta;

	if (bFirstSample)
	{
		bFirstSample = false;
		Accumulator = 0.f;
		return true;
	}

	Accumulator += SafeDelta;
	if (Accumulator < Period)
	{
		return false;
	}

	Accumulator -= Period;
	if (Accumulator >= Period)
	{
		// Hitch: drop the excess instead of emitting a burst of samples.
		Accumulator = 0.f;
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// FLRReplayBuffer
// ------------------------------------------------------------------------------------------------

void FLRReplayBuffer::Configure(int32 Capacity, int32 MaxTrafficPerFrame)
{
	const int32 SafeCapacity = FMath::Max(Capacity, 2);
	MaxTraffic = FMath::Max(MaxTrafficPerFrame, 0);

	Frames.Empty(SafeCapacity);
	Frames.SetNum(SafeCapacity);
	for (FLRReplayFrame& Frame : Frames)
	{
		Frame.Traffic.Empty(MaxTraffic);
	}

	Head = 0;
	NumFrames = 0;
}

void FLRReplayBuffer::Configure(const FLRReplaySettings& Settings)
{
	Configure(Settings.GetFrameCapacity(), Settings.MaxTrafficVehicles);
}

bool FLRReplayBuffer::Add(const FLRReplayFrame& Frame)
{
	using namespace LRReplayBufferPrivate;

	if (Frames.Num() == 0)
	{
		return false;
	}
	if (NumFrames > 0 && Frame.Time <= GetNewestTime())
	{
		return false;
	}

	FLRReplayFrame& Dest = Frames[Head];
	CopyScalars(Frame, Dest);

	// Reset keeps the preallocated slack; at most MaxTraffic elements are appended, so this never allocates.
	Dest.Traffic.Reset();
	const int32 NumTraffic = FMath::Min(Frame.Traffic.Num(), MaxTraffic);
	Dest.Traffic.Append(Frame.Traffic.GetData(), NumTraffic);
	Dest.Traffic.Sort(FByIdAscending());

	Head = (Head + 1) % Frames.Num();
	NumFrames = FMath::Min(NumFrames + 1, Frames.Num());
	return true;
}

void FLRReplayBuffer::Clear()
{
	for (FLRReplayFrame& Frame : Frames)
	{
		Frame.Traffic.Reset();
	}
	Head = 0;
	NumFrames = 0;
}

int32 FLRReplayBuffer::ToStorageIndex(int32 ChronologicalIndex) const
{
	const int32 Capacity = Frames.Num();
	const int32 Oldest = (Head - NumFrames + Capacity) % Capacity;
	return (Oldest + ChronologicalIndex) % Capacity;
}

const FLRReplayFrame& FLRReplayBuffer::GetFrame(int32 ChronologicalIndex) const
{
	check(ChronologicalIndex >= 0 && ChronologicalIndex < NumFrames);
	return Frames[ToStorageIndex(ChronologicalIndex)];
}

float FLRReplayBuffer::GetOldestTime() const
{
	return NumFrames > 0 ? GetFrame(0).Time : 0.f;
}

float FLRReplayBuffer::GetNewestTime() const
{
	return NumFrames > 0 ? GetFrame(NumFrames - 1).Time : 0.f;
}

float FLRReplayBuffer::Duration() const
{
	return NumFrames > 1 ? GetNewestTime() - GetOldestTime() : 0.f;
}

bool FLRReplayBuffer::EvaluateAt(float TimeFromStart, FLRReplayFrame& OutFrame) const
{
	using namespace LRReplayBufferPrivate;

	if (NumFrames == 0)
	{
		return false;
	}
	if (NumFrames == 1)
	{
		CopyFrame(GetFrame(0), OutFrame);
		return true;
	}

	const float OldestTime = GetOldestTime();
	const float Target = OldestTime + FMath::Clamp(TimeFromStart, 0.f, Duration());

	// Binary search: first chronological index whose Time is greater than Target.
	int32 Low = 0;
	int32 High = NumFrames;
	while (Low < High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (GetFrame(Mid).Time <= Target)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid;
		}
	}

	if (Low >= NumFrames)
	{
		// Target is exactly the newest frame.
		CopyFrame(GetFrame(NumFrames - 1), OutFrame);
		return true;
	}

	const int32 IndexB = FMath::Max(Low, 1);
	const FLRReplayFrame& A = GetFrame(IndexB - 1);
	const FLRReplayFrame& B = GetFrame(IndexB);
	const float Span = B.Time - A.Time;
	const float Alpha = Span > UE_KINDA_SMALL_NUMBER ? (Target - A.Time) / Span : 0.f;

	InterpolateFrames(A, B, Alpha, OutFrame);
	OutFrame.Time = Target;
	return true;
}

void FLRReplayBuffer::InterpolateVehicle(const FLRTrafficVehicleSnapshot& A, const FLRTrafficVehicleSnapshot& B, float Alpha, FLRTrafficVehicleSnapshot& OutVehicle)
{
	const FLRTrafficVehicleSnapshot& Nearest = Alpha < 0.5f ? A : B;

	OutVehicle.Id = A.Id;
	OutVehicle.S = A.S + (B.S - A.S) * static_cast<double>(Alpha);
	OutVehicle.D = FMath::Lerp(A.D, B.D, Alpha);
	OutVehicle.YawOffsetDeg = A.YawOffsetDeg + FMath::FindDeltaAngleDegrees(A.YawOffsetDeg, B.YawOffsetDeg) * Alpha;
	OutVehicle.SpeedMs = FMath::Lerp(A.SpeedMs, B.SpeedMs, Alpha);
	OutVehicle.Direction = Nearest.Direction;
	OutVehicle.Archetype = Nearest.Archetype;
	OutVehicle.LightFlags = Nearest.LightFlags;
	OutVehicle.LengthMeters = Nearest.LengthMeters;
	OutVehicle.WidthMeters = Nearest.WidthMeters;
}

void FLRReplayBuffer::InterpolateFrames(const FLRReplayFrame& A, const FLRReplayFrame& B, float Alpha, FLRReplayFrame& OutFrame)
{
	check(&OutFrame != &A && &OutFrame != &B);

	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	const bool bNearestIsA = T < 0.5f;

	OutFrame.Time = FMath::Lerp(A.Time, B.Time, T);

	// Player transform: lerp location and scale, slerp rotation (shortest path, normalized).
	const FVector Location = FMath::Lerp(A.PlayerTransform.GetLocation(), B.PlayerTransform.GetLocation(), static_cast<double>(T));
	const FQuat Rotation = FQuat::Slerp(A.PlayerTransform.GetRotation(), B.PlayerTransform.GetRotation(), static_cast<double>(T));
	const FVector Scale = FMath::Lerp(A.PlayerTransform.GetScale3D(), B.PlayerTransform.GetScale3D(), static_cast<double>(T));
	OutFrame.PlayerTransform = FTransform(Rotation, Location, Scale);

	OutFrame.PlayerRoadCoord.S = A.PlayerRoadCoord.S + (B.PlayerRoadCoord.S - A.PlayerRoadCoord.S) * static_cast<double>(T);
	OutFrame.PlayerRoadCoord.D = FMath::Lerp(A.PlayerRoadCoord.D, B.PlayerRoadCoord.D, T);
	OutFrame.PlayerSpeedKmh = FMath::Lerp(A.PlayerSpeedKmh, B.PlayerSpeedKmh, T);
	OutFrame.EngineRpm = FMath::Lerp(A.EngineRpm, B.EngineRpm, T);
	OutFrame.SteeringInput = FMath::Lerp(A.SteeringInput, B.SteeringInput, T);
	OutFrame.ThrottleInput = FMath::Lerp(A.ThrottleInput, B.ThrottleInput, T);
	OutFrame.BrakeInput = FMath::Lerp(A.BrakeInput, B.BrakeInput, T);
	OutFrame.Gear = bNearestIsA ? A.Gear : B.Gear;

	// Traffic: linear merge of the two Id-sorted arrays.
	OutFrame.Traffic.Reset();
	const TArray<FLRTrafficVehicleSnapshot>& TrafficA = A.Traffic;
	const TArray<FLRTrafficVehicleSnapshot>& TrafficB = B.Traffic;
	int32 IndexA = 0;
	int32 IndexB = 0;
	while (IndexA < TrafficA.Num() || IndexB < TrafficB.Num())
	{
		const bool bHasA = IndexA < TrafficA.Num();
		const bool bHasB = IndexB < TrafficB.Num();

		if (bHasA && bHasB && TrafficA[IndexA].Id == TrafficB[IndexB].Id)
		{
			InterpolateVehicle(TrafficA[IndexA], TrafficB[IndexB], T, OutFrame.Traffic.AddDefaulted_GetRef());
			++IndexA;
			++IndexB;
		}
		else if (bHasA && (!bHasB || TrafficA[IndexA].Id < TrafficB[IndexB].Id))
		{
			// Only in A (despawned or left the radius before B).
			if (bNearestIsA)
			{
				OutFrame.Traffic.Add(TrafficA[IndexA]);
			}
			++IndexA;
		}
		else
		{
			// Only in B (spawned or entered the radius after A).
			if (!bNearestIsA)
			{
				OutFrame.Traffic.Add(TrafficB[IndexB]);
			}
			++IndexB;
		}
	}
}

SIZE_T FLRReplayBuffer::GetAllocatedSize() const
{
	SIZE_T Total = Frames.GetAllocatedSize();
	for (const FLRReplayFrame& Frame : Frames)
	{
		Total += Frame.Traffic.GetAllocatedSize();
	}
	return Total;
}
