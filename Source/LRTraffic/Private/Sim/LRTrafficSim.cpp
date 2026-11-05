// Copyright LEFTRIGHT. All Rights Reserved.

#include "Sim/LRTrafficSim.h"

#include "LRRandom.h"
#include "LRUnits.h"
#include "Misc/Crc.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace LRTrafficSimPrivate
{
	constexpr double Infinity = TNumericLimits<double>::Max();

	/** Minimum bumper-to-bumper distance kept by the hard no-overlap clamp, meters. */
	constexpr double ClampGapM = 0.2;

	/** Distance kept from the point where a solvability window would close, meters. */
	constexpr double WallEpsilonM = 0.05;

	/** How far ahead a vehicle anticipates a solvability "ghost" obstacle, meters. */
	constexpr double GhostLookaheadM = 150.0;

	/** Ghost following uses this fraction of the driver's time headway (the hard clamp is the real limit). */
	constexpr float GhostHeadwayFactor = 0.25f;

	/** Speed floor for the yaw-from-lateral-velocity computation, m/s. */
	constexpr float MinYawSpeedMs = 1.f;

	/** Below this speed the brake lights stay on (standing traffic), m/s. */
	constexpr float StandingSpeedMs = 0.5f;

	/** Leaders closer than this cap the speed of a new vehicle, meters. */
	constexpr double SpawnSpeedMatchDistanceM = 200.0;

	/** Smootherstep 6t^5 - 15t^4 + 10t^3 on [0, 1]. */
	inline float Smootherstep(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
	}

	template <typename T>
	inline void AllocateArray(TArray<T>& Array, int32 Num)
	{
		Array.Reset();
		Array.SetNumZeroed(Num);
	}

	template <typename T>
	inline void SortMinMax(T& Min, T& Max)
	{
		if (Min > Max)
		{
			Swap(Min, Max);
		}
	}

	template <typename T>
	inline uint32 Crc(const T& Value, uint32 InCrc)
	{
		return FCrc::MemCrc32(&Value, sizeof(T), InCrc);
	}
}

// ------------------------------------------------------------------------------------------------
// Setup
// ------------------------------------------------------------------------------------------------

void FLRTrafficSim::SanitizeProfile()
{
	using namespace LRTrafficSimPrivate;

	if (Profile.Archetypes.IsEmpty())
	{
		Profile.Archetypes.AddDefaulted();
	}
	if (Profile.DriverProfiles.IsEmpty())
	{
		Profile.DriverProfiles.AddDefaulted();
	}
	// Index storage is uint16.
	if (Profile.Archetypes.Num() > MAX_uint16)
	{
		Profile.Archetypes.SetNum(MAX_uint16);
	}
	if (Profile.DriverProfiles.Num() > MAX_uint16)
	{
		Profile.DriverProfiles.SetNum(MAX_uint16);
	}

	for (FLRTrafficArchetype& Arch : Profile.Archetypes)
	{
		Arch.LengthMeters = FMath::Clamp(Arch.LengthMeters, 1.f, 30.f);
		Arch.WidthMeters = FMath::Clamp(Arch.WidthMeters, 0.5f, 3.5f);
		Arch.HeightMeters = FMath::Clamp(Arch.HeightMeters, 0.5f, 6.f);
		Arch.Weight = FMath::Max(0.f, Arch.Weight);
		Arch.SpeedFactorMin = FMath::Max(0.05f, Arch.SpeedFactorMin);
		Arch.SpeedFactorMax = FMath::Max(0.05f, Arch.SpeedFactorMax);
		SortMinMax(Arch.SpeedFactorMin, Arch.SpeedFactorMax);
		if (Arch.Colors.Num() > MAX_uint8)
		{
			Arch.Colors.SetNum(MAX_uint8);
		}
	}

	for (FLRTrafficDriverProfile& Drv : Profile.DriverProfiles)
	{
		Drv.Weight = FMath::Max(0.f, Drv.Weight);
		Drv.DesiredSpeedFactorMin = FMath::Max(0.05f, Drv.DesiredSpeedFactorMin);
		Drv.DesiredSpeedFactorMax = FMath::Max(0.05f, Drv.DesiredSpeedFactorMax);
		SortMinMax(Drv.DesiredSpeedFactorMin, Drv.DesiredSpeedFactorMax);
		Drv.TimeHeadwayS = FMath::Max(0.1f, Drv.TimeHeadwayS);
		Drv.MinGapM = FMath::Max(0.5f, Drv.MinGapM);
		Drv.MaxAccelMs2 = FMath::Max(0.1f, Drv.MaxAccelMs2);
		Drv.ComfortDecelMs2 = FMath::Max(0.1f, Drv.ComfortDecelMs2);
		Drv.SafeDecelMs2 = FMath::Max(0.5f, Drv.SafeDecelMs2);
		Drv.AggressiveEventChance = FMath::Clamp(Drv.AggressiveEventChance, 0.f, 1.f);
	}

	FLRTrafficLaneChangeParams& LC = Profile.LaneChange;
	SortMinMax(LC.BlinkerMinSeconds, LC.BlinkerMaxSeconds);
	SortMinMax(LC.MoveMinSeconds, LC.MoveMaxSeconds);
	LC.MoveMinSeconds = FMath::Max(0.3f, LC.MoveMinSeconds);
	LC.MobilIntervalSeconds = FMath::Max(0.05f, LC.MobilIntervalSeconds);
	LC.AbortMoveSeconds = FMath::Max(0.3f, LC.AbortMoveSeconds);

	FLRTrafficFairness& Fair = Profile.Fairness;
	Fair.AggressiveMinBlinkSeconds = FMath::Max(1.f, Fair.AggressiveMinBlinkSeconds);
	Fair.SolvabilityWindowM = FMath::Max(1.f, Fair.SolvabilityWindowM);
	Fair.AggressiveMoveSeconds = FMath::Max(0.3f, Fair.AggressiveMoveSeconds);

	FLRTrafficWindows& Win = Profile.Windows;
	SortMinMax(Win.SpawnAheadMinM, Win.SpawnAheadMaxM);
	SortMinMax(Win.SpawnBehindMinM, Win.SpawnBehindMaxM);
	Win.SimAheadM = FMath::Max(Win.SimAheadM, Win.SpawnAheadMaxM + 50.f);
	Win.SimBehindM = FMath::Max(Win.SimBehindM, Win.SpawnBehindMaxM + 10.f);
	Win.MobilAheadM = FMath::Min(Win.MobilAheadM, Win.SimAheadM);
	Win.MobilBehindM = FMath::Min(Win.MobilBehindM, Win.SimBehindM);

	if (Profile.DensityCurve.IsEmpty())
	{
		Profile.DensityCurve.Add(FVector2D(0.0, 10.0));
	}
	Profile.DensityCurve.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });

	Profile.MaxVehicles = FMath::Clamp(Profile.MaxVehicles, 1, 1000);
	Profile.FixedStepHz = FMath::Clamp(Profile.FixedStepHz, 10.f, 240.f);
	Profile.SpawnMinGapM = FMath::Max(5.f, Profile.SpawnMinGapM);
	Profile.OncomingDensityFactor = FMath::Max(0.f, Profile.OncomingDensityFactor);

	PlayerDriverIndex = 0;
	for (int32 Index = 0; Index < Profile.DriverProfiles.Num(); ++Index)
	{
		if (Profile.DriverProfiles[Index].Name == Profile.PlayerDriverProfile)
		{
			PlayerDriverIndex = Index;
			break;
		}
	}
}

void FLRTrafficSim::Configure(const FLRTrafficProfile& InProfile, TConstArrayView<FLRLaneDesc> InLanes, float InSpeedLimitMs)
{
	using namespace LRTrafficSimPrivate;

	Profile = InProfile;
	SanitizeProfile();

	Lanes.Reset();
	Lanes.Append(InLanes.GetData(), FMath::Min<int32>(InLanes.Num(), MAX_int8));

	SetSpeedLimit(InSpeedLimitMs);

	Capacity = Profile.MaxVehicles;
	AllocateArray(S, Capacity);
	AllocateArray(PrevS, Capacity);
	AllocateArray(D, Capacity);
	AllocateArray(PrevD, Capacity);
	AllocateArray(Speed, Capacity);
	AllocateArray(DesiredSpeed, Capacity);
	AllocateArray(Accel, Capacity);
	AllocateArray(Length, Capacity);
	AllocateArray(Width, Capacity);
	AllocateArray(YawOffsetDeg, Capacity);
	AllocateArray(Lane, Capacity);
	AllocateArray(TargetLane, Capacity);
	AllocateArray(BlockLane, Capacity);
	AllocateArray(Direction, Capacity);
	AllocateArray(Phase, Capacity);
	AllocateArray(LaneChangeT, Capacity);
	AllocateArray(LaneChangeDuration, Capacity);
	AllocateArray(LaneChangeFromD, Capacity);
	AllocateArray(SignalTimer, Capacity);
	AllocateArray(PauseTimer, Capacity);
	AllocateArray(MobilTimer, Capacity);
	AllocateArray(CooldownTimer, Capacity);
	AllocateArray(WreckTimer, Capacity);
	AllocateArray(Driver, Capacity);
	AllocateArray(Archetype, Capacity);
	AllocateArray(ColorIndex, Capacity);
	AllocateArray(Flags, Capacity);
	AllocateArray(Id, Capacity);

	LaneMembers.SetNum(Lanes.Num());
	for (TArray<int32>& Members : LaneMembers)
	{
		Members.Reset();
		Members.Reserve(Capacity);
	}
	Order.Reset();
	Order.Reserve(Capacity);
	FreeSlots.Reset();
	FreeSlots.Reserve(Capacity);

	NumPlayerLanes = 0;
	NumOncomingLanes = 0;
	for (const FLRLaneDesc& LaneDesc : Lanes)
	{
		(LaneDesc.Direction >= 0 ? NumPlayerLanes : NumOncomingLanes)++;
	}
	bSolvabilityActive = Profile.Fairness.bEnforceSolvability && NumPlayerLanes >= 2;

	ClearVehicles();
}

void FLRTrafficSim::SetSpeedLimit(float InSpeedLimitMs)
{
	SpeedLimitMs = InSpeedLimitMs > 0.f ? InSpeedLimitMs : static_cast<float>(LRUnits::KmhToMs(Profile.DefaultSpeedLimitKmh));
	SpeedLimitMs = FMath::Max(1.f, SpeedLimitMs);
}

void FLRTrafficSim::ClearVehicles()
{
	for (int32 Slot = 0; Slot < Flags.Num(); ++Slot)
	{
		Flags[Slot] = 0;
	}
	FreeSlots.Reset();
	for (int32 Slot = Capacity - 1; Slot >= 0; --Slot)
	{
		FreeSlots.Add(Slot);
	}
	for (TArray<int32>& Members : LaneMembers)
	{
		Members.Reset();
	}
	Order.Reset();
	NumAlive = 0;
}

void FLRTrafficSim::Reset(int32 Seed, bool bPrefill)
{
	ClearVehicles();
	SpawnStream = LRRandom::MakeStream(Seed, LRRandom::EStream::TrafficSpawn);
	BehaviorStream = LRRandom::MakeStream(Seed, LRRandom::EStream::TrafficBehavior);
	SimTime = 0.0;
	NextId = 1;
	NumLaneChangesStarted = 0;
	NumAggressiveEvents = 0;
	NumSolvabilityViolations = 0;

	if (bPrefill)
	{
		Prefill();
	}
}

void FLRTrafficSim::SetPlayer(const FLRTrafficPlayerState& InPlayer)
{
	Player = InPlayer;
	Player.LengthM = FMath::Max(0.5f, Player.LengthM);
	Player.WidthM = FMath::Max(0.3f, Player.WidthM);
}

int32 FLRTrafficSim::AllocateSlot()
{
	return FreeSlots.IsEmpty() ? NoSlot : FreeSlots.Pop(EAllowShrinking::No);
}

void FLRTrafficSim::FreeSlot(int32 Slot)
{
	if (!IsAlive(Slot))
	{
		return;
	}
	for (TArray<int32>& Members : LaneMembers)
	{
		const int32 Index = Members.Find(Slot);
		if (Index != INDEX_NONE)
		{
			Members.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
	// Keep Order free of dead slots so a reused slot is never listed twice (removals are rare: O(n) is fine).
	const int32 OrderIndex = Order.Find(Slot);
	if (OrderIndex != INDEX_NONE)
	{
		Order.RemoveAt(OrderIndex, 1, EAllowShrinking::No);
	}
	Flags[Slot] = 0;
	FreeSlots.Add(Slot);
	--NumAlive;
}

int32 FLRTrafficSim::FindSlotById(int32 InId) const
{
	for (int32 Slot = 0; Slot < Capacity; ++Slot)
	{
		if (IsAlive(Slot) && Id[Slot] == InId)
		{
			return Slot;
		}
	}
	return NoSlot;
}

float FLRTrafficSim::EvaluateDensityCurve(const TArray<FVector2D>& Curve, float RunTimeSeconds)
{
	if (Curve.IsEmpty())
	{
		return 0.f;
	}
	if (RunTimeSeconds <= Curve[0].X)
	{
		return FMath::Max(0.f, static_cast<float>(Curve[0].Y));
	}
	for (int32 Index = 1; Index < Curve.Num(); ++Index)
	{
		const FVector2D& A = Curve[Index - 1];
		const FVector2D& B = Curve[Index];
		if (RunTimeSeconds <= B.X)
		{
			const double Span = B.X - A.X;
			const double Alpha = Span > UE_SMALL_NUMBER ? (RunTimeSeconds - A.X) / Span : 1.0;
			return FMath::Max(0.f, static_cast<float>(FMath::Lerp(A.Y, B.Y, Alpha)));
		}
	}
	return FMath::Max(0.f, static_cast<float>(Curve.Last().Y));
}

// ------------------------------------------------------------------------------------------------
// Spawning
// ------------------------------------------------------------------------------------------------

float FLRTrafficSim::LanePreferenceWeight(bool bPreferRight, bool bPreferLeft, int32 LaneIndex) const
{
	if (!Lanes.IsValidIndex(LaneIndex))
	{
		return 0.f;
	}

	// Rank of the lane among the lanes of its direction, in the driver's frame.
	const int32 Dir = Lanes[LaneIndex].Direction >= 0 ? 1 : -1;
	int32 Count = 0;
	int32 RankFromRight = 0;
	for (int32 Index = 0; Index < Lanes.Num(); ++Index)
	{
		if ((Lanes[Index].Direction >= 0 ? 1 : -1) != Dir)
		{
			continue;
		}
		++Count;
		const bool bRightOf = Dir > 0 ? Index > LaneIndex : Index < LaneIndex;
		RankFromRight += bRightOf ? 1 : 0;
	}
	const int32 RankFromLeft = Count - 1 - RankFromRight;

	if (bPreferRight)
	{
		if (Count >= 3 && RankFromLeft == 0)
		{
			return 0.f;
		}
		return RankFromRight == 0 ? 1.f : (RankFromRight == 1 ? 0.4f : 0.05f);
	}
	if (bPreferLeft)
	{
		return RankFromLeft == 0 ? 1.f : 0.3f;
	}
	return 1.f;
}

int32 FLRTrafficSim::ChooseArchetype(int32 ForLane)
{
	float Total = 0.f;
	for (const FLRTrafficArchetype& Arch : Profile.Archetypes)
	{
		Total += Arch.Weight * (ForLane >= 0 ? LanePreferenceWeight(Arch.bPreferRightLanes, false, ForLane) : 1.f);
	}
	if (Total <= 0.f)
	{
		return INDEX_NONE;
	}

	float Pick = SpawnStream.FRand() * Total;
	for (int32 Index = 0; Index < Profile.Archetypes.Num(); ++Index)
	{
		const FLRTrafficArchetype& Arch = Profile.Archetypes[Index];
		const float Weight = Arch.Weight * (ForLane >= 0 ? LanePreferenceWeight(Arch.bPreferRightLanes, false, ForLane) : 1.f);
		if (Weight <= 0.f)
		{
			continue;
		}
		Pick -= Weight;
		if (Pick <= 0.f)
		{
			return Index;
		}
	}
	return Profile.Archetypes.Num() - 1;
}

int32 FLRTrafficSim::ChooseDriver(int32 ArchetypeIndex, int32 ForLane)
{
	if (Profile.Archetypes.IsValidIndex(ArchetypeIndex))
	{
		const FName Forced = Profile.Archetypes[ArchetypeIndex].DriverProfile;
		if (!Forced.IsNone())
		{
			for (int32 Index = 0; Index < Profile.DriverProfiles.Num(); ++Index)
			{
				if (Profile.DriverProfiles[Index].Name == Forced)
				{
					return Index;
				}
			}
		}
	}

	auto WeightOf = [this, ForLane](const FLRTrafficDriverProfile& Drv)
	{
		float Weight = Drv.Weight;
		if (ForLane >= 0 && Drv.bSpawnInLeftLanes)
		{
			Weight *= LanePreferenceWeight(false, true, ForLane);
		}
		return Weight;
	};

	float Total = 0.f;
	for (const FLRTrafficDriverProfile& Drv : Profile.DriverProfiles)
	{
		Total += WeightOf(Drv);
	}
	if (Total <= 0.f)
	{
		return 0;
	}

	float Pick = SpawnStream.FRand() * Total;
	for (int32 Index = 0; Index < Profile.DriverProfiles.Num(); ++Index)
	{
		const float Weight = WeightOf(Profile.DriverProfiles[Index]);
		if (Weight <= 0.f)
		{
			continue;
		}
		Pick -= Weight;
		if (Pick <= 0.f)
		{
			return Index;
		}
	}
	return Profile.DriverProfiles.Num() - 1;
}

int32 FLRTrafficSim::ChooseLaneForArchetype(int32 ArchetypeIndex, int32 DriverIndex, int32 InDirection)
{
	const bool bPreferRight = Profile.Archetypes.IsValidIndex(ArchetypeIndex) && Profile.Archetypes[ArchetypeIndex].bPreferRightLanes;
	const bool bPreferLeft = !bPreferRight && Profile.DriverProfiles.IsValidIndex(DriverIndex) && Profile.DriverProfiles[DriverIndex].bSpawnInLeftLanes;

	float Total = 0.f;
	for (int32 Index = 0; Index < Lanes.Num(); ++Index)
	{
		if ((Lanes[Index].Direction >= 0 ? 1 : -1) == InDirection)
		{
			Total += LanePreferenceWeight(bPreferRight, bPreferLeft, Index);
		}
	}
	if (Total <= 0.f)
	{
		return INDEX_NONE;
	}

	float Pick = SpawnStream.FRand() * Total;
	int32 LastValid = INDEX_NONE;
	for (int32 Index = 0; Index < Lanes.Num(); ++Index)
	{
		if ((Lanes[Index].Direction >= 0 ? 1 : -1) != InDirection)
		{
			continue;
		}
		const float Weight = LanePreferenceWeight(bPreferRight, bPreferLeft, Index);
		if (Weight <= 0.f)
		{
			continue;
		}
		LastValid = Index;
		Pick -= Weight;
		if (Pick <= 0.f)
		{
			return Index;
		}
	}
	return LastValid;
}

int32 FLRTrafficSim::AddVehicle(const FLRTrafficSpawnRequest& Request)
{
	using namespace LRTrafficSimPrivate;

	if (!Lanes.IsValidIndex(Request.Lane) || NumAlive >= Capacity || FreeSlots.IsEmpty())
	{
		return INDEX_NONE;
	}

	const int32 LaneIndex = Request.Lane;
	const int32 Dir = Lanes[LaneIndex].Direction >= 0 ? 1 : -1;

	const int32 ArchIndex = Profile.Archetypes.IsValidIndex(Request.ArchetypeIndex) ? Request.ArchetypeIndex : ChooseArchetype(LaneIndex);
	if (!Profile.Archetypes.IsValidIndex(ArchIndex))
	{
		return INDEX_NONE;
	}
	const int32 DrvIndex = Profile.DriverProfiles.IsValidIndex(Request.DriverIndex) ? Request.DriverIndex : ChooseDriver(ArchIndex, LaneIndex);
	const FLRTrafficArchetype& Arch = Profile.Archetypes[ArchIndex];
	const FLRTrafficDriverProfile& Drv = Profile.DriverProfiles[DrvIndex];

	const float VehicleLength = Arch.LengthMeters;
	const float VehicleWidth = Arch.WidthMeters;
	const float ArchFactor = SpawnStream.FRandRange(Arch.SpeedFactorMin, Arch.SpeedFactorMax);
	const float DriverFactor = SpawnStream.FRandRange(Drv.DesiredSpeedFactorMin, Drv.DesiredSpeedFactorMax);
	const float Desired = Request.DesiredSpeedMs > 0.f ? Request.DesiredSpeedMs : SpeedLimitMs * ArchFactor * DriverFactor;
	float InitialSpeed = Request.SpeedMs >= 0.f ? Request.SpeedMs : Desired * SpawnStream.FRandRange(0.9f, 1.f);

	const FNeighbor Leader = FindLeader(LaneIndex, Request.S, VehicleLength, Dir, NoSlot);
	const FNeighbor Follower = FindFollower(LaneIndex, Request.S, VehicleLength, Dir, NoSlot);

	if (!Request.bIgnoreChecks)
	{
		const double MinGap = Profile.SpawnMinGapM;
		if (Leader.IsValid() && Leader.Gap < MinGap)
		{
			return INDEX_NONE;
		}
		if (Follower.IsValid())
		{
			const double Closing = FMath::Max(0.0, static_cast<double>(Follower.SpeedInFrame) - InitialSpeed);
			if (Follower.Gap < MinGap + 2.0 * Closing)
			{
				return INDEX_NONE;
			}
		}
		if (Dir > 0 && bSolvabilityActive)
		{
			const double Front = Request.S + 0.5 * VehicleLength;
			const double Rear = Request.S - 0.5 * VehicleLength;
			if (FirstBlockedPoint(Rear - Profile.Fairness.SolvabilityWindowM, LaneIndex, NoSlot, Front) <= Front)
			{
				return INDEX_NONE;
			}
		}
	}

	if (Request.SpeedMs < 0.f && Leader.IsValid() && Leader.Gap < SpawnSpeedMatchDistanceM)
	{
		InitialSpeed = FMath::Min(InitialSpeed, FMath::Max(0.f, Leader.SpeedInFrame));
	}

	const int32 Slot = AllocateSlot();
	if (Slot == NoSlot)
	{
		return INDEX_NONE;
	}

	S[Slot] = Request.S;
	PrevS[Slot] = Request.S;
	D[Slot] = Lanes[LaneIndex].CenterD;
	PrevD[Slot] = D[Slot];
	Speed[Slot] = FMath::Max(0.f, InitialSpeed);
	DesiredSpeed[Slot] = FMath::Max(0.5f, Desired);
	Accel[Slot] = 0.f;
	Length[Slot] = VehicleLength;
	Width[Slot] = VehicleWidth;
	YawOffsetDeg[Slot] = 0.f;
	Lane[Slot] = static_cast<int8>(LaneIndex);
	TargetLane[Slot] = static_cast<int8>(LaneIndex);
	BlockLane[Slot] = static_cast<int8>(LaneIndex);
	Direction[Slot] = static_cast<int8>(Dir);
	Phase[Slot] = static_cast<uint8>(ELRLaneChangePhase::None);
	LaneChangeT[Slot] = 0.f;
	LaneChangeDuration[Slot] = Profile.LaneChange.MoveMinSeconds;
	LaneChangeFromD[Slot] = D[Slot];
	SignalTimer[Slot] = 0.f;
	PauseTimer[Slot] = 0.f;
	MobilTimer[Slot] = SpawnStream.FRandRange(0.f, Profile.LaneChange.MobilIntervalSeconds);
	CooldownTimer[Slot] = 0.f;
	WreckTimer[Slot] = 0.f;
	Driver[Slot] = static_cast<uint16>(DrvIndex);
	Archetype[Slot] = static_cast<uint16>(ArchIndex);
	ColorIndex[Slot] = Arch.Colors.Num() > 0 ? static_cast<uint8>(SpawnStream.RandRange(0, Arch.Colors.Num() - 1)) : 0;
	Flags[Slot] = LRTrafficSimFlags::Alive;
	Id[Slot] = NextId++;

	InsertIntoLane(LaneIndex, Slot);
	Order.Add(Slot);
	++NumAlive;
	return Id[Slot];
}

void FLRTrafficSim::Prefill()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLRTrafficSim::Prefill);

	const FLRTrafficWindows& Win = Profile.Windows;
	const double PlayerS = Player.S;
	const float BaseDensity = EvaluateDensity(0.f) * DensityScale;

	for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
	{
		const bool bOncoming = Lanes[LaneIndex].Direction < 0;
		const float Density = BaseDensity * (bOncoming ? Profile.OncomingDensityFactor : 1.f);
		if (Density <= 0.01f)
		{
			continue;
		}

		const double MeanSpacing = 1000.0 / Density;
		const double Start = PlayerS + Win.InitialFillMinAheadM;
		const double End = PlayerS + Win.SpawnAheadMaxM;
		const bool bPlayerLane = PlayerOccupiesLane(LaneIndex);

		double PositionS = Start + SpawnStream.FRandRange(0.f, static_cast<float>(MeanSpacing));
		while (PositionS < End && NumAlive < Capacity)
		{
			const bool bKeepClear = bPlayerLane && PositionS < PlayerS + Win.InitialClearLaneAheadM;
			if (!bKeepClear)
			{
				FLRTrafficSpawnRequest Request;
				Request.Lane = LaneIndex;
				Request.S = PositionS;
				AddVehicle(Request);
			}
			PositionS += FMath::Max(static_cast<double>(Profile.SpawnMinGapM) + 5.0, MeanSpacing * SpawnStream.FRandRange(0.6f, 1.4f));
		}
	}
}

void FLRTrafficSim::SpawnStep()
{
	if (DensityScale <= 0.f || Profile.MaxSpawnAttemptsPerStep <= 0)
	{
		return;
	}

	const FLRTrafficWindows& Win = Profile.Windows;
	const float Density = EvaluateDensity(static_cast<float>(SimTime)) * DensityScale;
	const float WindowKm = (Win.SimAheadM + Win.SimBehindM) / 1000.f;
	const float TargetForward = Density * WindowKm * NumPlayerLanes;
	const float TargetOncoming = Density * Profile.OncomingDensityFactor * WindowKm * NumOncomingLanes;

	int32 CountForward = 0;
	int32 CountOncoming = 0;
	for (const int32 Slot : Order)
	{
		if (IsAlive(Slot))
		{
			(Direction[Slot] > 0 ? CountForward : CountOncoming)++;
		}
	}

	for (int32 Attempt = 0; Attempt < Profile.MaxSpawnAttemptsPerStep && NumAlive < Capacity; ++Attempt)
	{
		const float DeficitForward = TargetForward - CountForward;
		const float DeficitOncoming = TargetOncoming - CountOncoming;
		if (DeficitForward < 1.f && DeficitOncoming < 1.f)
		{
			break;
		}

		const float RatioForward = TargetForward > 0.f ? DeficitForward / TargetForward : -1.f;
		const float RatioOncoming = TargetOncoming > 0.f ? DeficitOncoming / TargetOncoming : -1.f;
		const int32 Dir = (DeficitForward >= 1.f && RatioForward >= RatioOncoming) || DeficitOncoming < 1.f ? 1 : -1;

		const int32 ArchIndex = ChooseArchetype(INDEX_NONE);
		if (ArchIndex == INDEX_NONE)
		{
			break;
		}
		const int32 DrvIndex = ChooseDriver(ArchIndex, INDEX_NONE);
		const int32 LaneIndex = ChooseLaneForArchetype(ArchIndex, DrvIndex, Dir);
		if (LaneIndex == INDEX_NONE)
		{
			continue;
		}

		// Behind (out of view) only while the player is slower than traffic.
		const bool bPlayerSlow = Player.bValid && Player.SpeedMs < SpeedLimitMs * 0.8f;
		double SpawnS;
		if (Dir > 0 && bPlayerSlow && SpawnStream.FRand() < 0.35f)
		{
			SpawnS = Player.S - SpawnStream.FRandRange(Win.SpawnBehindMinM, Win.SpawnBehindMaxM);
		}
		else
		{
			SpawnS = Player.S + SpawnStream.FRandRange(Win.SpawnAheadMinM, Win.SpawnAheadMaxM);
		}

		FLRTrafficSpawnRequest Request;
		Request.Lane = LaneIndex;
		Request.S = SpawnS;
		Request.ArchetypeIndex = ArchIndex;
		Request.DriverIndex = DrvIndex;
		if (AddVehicle(Request) != INDEX_NONE)
		{
			(Dir > 0 ? CountForward : CountOncoming)++;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Lane lists
// ------------------------------------------------------------------------------------------------

int32 FLRTrafficSim::LowerBoundS(int32 LaneIndex, double InS) const
{
	const TArray<int32>& Members = LaneMembers[LaneIndex];
	int32 Low = 0;
	int32 High = Members.Num();
	while (Low < High)
	{
		const int32 Mid = (Low + High) / 2;
		if (S[Members[Mid]] < InS)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid;
		}
	}
	return Low;
}

void FLRTrafficSim::InsertIntoLane(int32 LaneIndex, int32 Slot)
{
	TArray<int32>& Members = LaneMembers[LaneIndex];
	if (Members.Contains(Slot))
	{
		return;
	}
	Members.Insert(Slot, LowerBoundS(LaneIndex, S[Slot]));
}

bool FLRTrafficSim::IsMemberOfLane(int32 Slot, int32 LaneIndex) const
{
	return Lane[Slot] == LaneIndex
		|| (Phase[Slot] == static_cast<uint8>(ELRLaneChangePhase::Moving) && TargetLane[Slot] == LaneIndex);
}

void FLRTrafficSim::RebuildLaneLists()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLRTrafficSim::RebuildLaneLists);

	for (int32 LaneIndex = 0; LaneIndex < LaneMembers.Num(); ++LaneIndex)
	{
		TArray<int32>& Members = LaneMembers[LaneIndex];

		// Compact in place: drop dead vehicles and finished lane changes (origin lane).
		int32 Write = 0;
		for (int32 Read = 0; Read < Members.Num(); ++Read)
		{
			const int32 Slot = Members[Read];
			if (IsAlive(Slot) && IsMemberOfLane(Slot, LaneIndex))
			{
				Members[Write++] = Slot;
			}
		}
		Members.SetNum(Write, EAllowShrinking::No);

		// Insertion sort by S: the list is nearly (usually fully) sorted.
		for (int32 Index = 1; Index < Members.Num(); ++Index)
		{
			const int32 Key = Members[Index];
			const double KeyS = S[Key];
			int32 Scan = Index - 1;
			while (Scan >= 0 && S[Members[Scan]] > KeyS)
			{
				Members[Scan + 1] = Members[Scan];
				--Scan;
			}
			Members[Scan + 1] = Key;
		}
	}
}

void FLRTrafficSim::SortOrder()
{
	int32 Write = 0;
	for (int32 Read = 0; Read < Order.Num(); ++Read)
	{
		if (IsAlive(Order[Read]))
		{
			Order[Write++] = Order[Read];
		}
	}
	Order.SetNum(Write, EAllowShrinking::No);

	// Front first: key S * Direction, descending. Insertion sort (nearly sorted from the previous step).
	for (int32 Index = 1; Index < Order.Num(); ++Index)
	{
		const int32 Key = Order[Index];
		const double KeyValue = S[Key] * Direction[Key];
		int32 Scan = Index - 1;
		while (Scan >= 0 && S[Order[Scan]] * Direction[Order[Scan]] < KeyValue)
		{
			Order[Scan + 1] = Order[Scan];
			--Scan;
		}
		Order[Scan + 1] = Key;
	}
}

// ------------------------------------------------------------------------------------------------
// Neighbors / IDM
// ------------------------------------------------------------------------------------------------

bool FLRTrafficSim::PlayerOccupiesLane(int32 LaneIndex) const
{
	if (!Player.bValid || !Lanes.IsValidIndex(LaneIndex))
	{
		return false;
	}
	const FLRLaneDesc& LaneDesc = Lanes[LaneIndex];
	return FMath::Abs(Player.D - LaneDesc.CenterD) < 0.5f * (LaneDesc.WidthMeters + Player.WidthM);
}

FLRTrafficSim::FNeighbor FLRTrafficSim::FindLeader(int32 LaneIndex, double InS, float InLength, int32 InDirection, int32 ExcludeSlot) const
{
	FNeighbor Result;
	const TArray<int32>& Members = LaneMembers[LaneIndex];
	const int32 Bound = LowerBoundS(LaneIndex, InS);

	if (InDirection > 0)
	{
		for (int32 Index = Bound; Index < Members.Num(); ++Index)
		{
			const int32 Other = Members[Index];
			if (Other == ExcludeSlot)
			{
				continue;
			}
			Result.Slot = Other;
			Result.Gap = (S[Other] - 0.5 * Length[Other]) - (InS + 0.5 * InLength);
			Result.SpeedInFrame = Speed[Other];
			break;
		}
	}
	else
	{
		for (int32 Index = Bound - 1; Index >= 0; --Index)
		{
			const int32 Other = Members[Index];
			if (Other == ExcludeSlot)
			{
				continue;
			}
			Result.Slot = Other;
			Result.Gap = (InS - 0.5 * InLength) - (S[Other] + 0.5 * Length[Other]);
			Result.SpeedInFrame = Speed[Other];
			break;
		}
	}

	if (PlayerOccupiesLane(LaneIndex))
	{
		const double Ahead = InDirection * (Player.S - InS);
		if (Ahead > 0.0)
		{
			const double Gap = Ahead - 0.5 * (Player.LengthM + InLength);
			if (Gap < Result.Gap)
			{
				Result.Slot = NoSlot;
				Result.bPlayer = true;
				Result.Gap = Gap;
				Result.SpeedInFrame = Player.SpeedMs * InDirection;
			}
		}
	}
	return Result;
}

FLRTrafficSim::FNeighbor FLRTrafficSim::FindFollower(int32 LaneIndex, double InS, float InLength, int32 InDirection, int32 ExcludeSlot) const
{
	FNeighbor Result;
	const TArray<int32>& Members = LaneMembers[LaneIndex];
	const int32 Bound = LowerBoundS(LaneIndex, InS);

	if (InDirection > 0)
	{
		for (int32 Index = Bound - 1; Index >= 0; --Index)
		{
			const int32 Other = Members[Index];
			if (Other == ExcludeSlot)
			{
				continue;
			}
			Result.Slot = Other;
			Result.Gap = (InS - 0.5 * InLength) - (S[Other] + 0.5 * Length[Other]);
			Result.SpeedInFrame = Speed[Other];
			break;
		}
	}
	else
	{
		for (int32 Index = Bound; Index < Members.Num(); ++Index)
		{
			const int32 Other = Members[Index];
			if (Other == ExcludeSlot)
			{
				continue;
			}
			Result.Slot = Other;
			Result.Gap = (S[Other] - 0.5 * Length[Other]) - (InS + 0.5 * InLength);
			Result.SpeedInFrame = Speed[Other];
			break;
		}
	}

	if (PlayerOccupiesLane(LaneIndex))
	{
		const double Behind = InDirection * (InS - Player.S);
		if (Behind > 0.0)
		{
			const double Gap = Behind - 0.5 * (Player.LengthM + InLength);
			if (Gap < Result.Gap)
			{
				Result.Slot = NoSlot;
				Result.bPlayer = true;
				Result.Gap = Gap;
				Result.SpeedInFrame = Player.SpeedMs * InDirection;
			}
		}
	}
	return Result;
}

FLRIdmParams FLRTrafficSim::MakeIdmParams(int32 Slot) const
{
	const FLRTrafficDriverProfile& Drv = Profile.DriverProfiles[Driver[Slot]];
	FLRIdmParams Params;
	Params.DesiredSpeedMs = DesiredSpeed[Slot];
	Params.TimeHeadwayS = Drv.TimeHeadwayS;
	Params.MinGapM = Drv.MinGapM;
	Params.MaxAccelMs2 = Drv.MaxAccelMs2;
	Params.ComfortDecelMs2 = Drv.ComfortDecelMs2;
	return Params;
}

FLRIdmParams FLRTrafficSim::MakePlayerIdmParams(float PlayerSpeedInFrame) const
{
	const FLRTrafficDriverProfile& Drv = Profile.DriverProfiles[PlayerDriverIndex];
	FLRIdmParams Params;
	// The player is assumed to cruise at its current speed.
	Params.DesiredSpeedMs = FMath::Max(1.f, PlayerSpeedInFrame);
	Params.TimeHeadwayS = Drv.TimeHeadwayS;
	Params.MinGapM = Drv.MinGapM;
	Params.MaxAccelMs2 = Drv.MaxAccelMs2;
	Params.ComfortDecelMs2 = Drv.ComfortDecelMs2;
	return Params;
}

float FLRTrafficSim::AccelWithLeader(const FLRIdmParams& Params, float OwnSpeed, const FNeighbor& Leader) const
{
	if (!Leader.IsValid())
	{
		return IdmAcceleration(Params, OwnSpeed, 0.f, 0.f, false);
	}
	const float Gap = static_cast<float>(FMath::Min(Leader.Gap, 1.0e6));
	return IdmAcceleration(Params, OwnSpeed, Gap, OwnSpeed - Leader.SpeedInFrame, true);
}

float FLRTrafficSim::ComputeAcceleration(int32 Slot) const
{
	using namespace LRTrafficSimPrivate;

	const FLRIdmParams Params = MakeIdmParams(Slot);
	const float OwnSpeed = Speed[Slot];
	const int32 Dir = Direction[Slot];

	float Result = AccelWithLeader(Params, OwnSpeed, FindLeader(Lane[Slot], S[Slot], Length[Slot], Dir, Slot));
	if (Phase[Slot] == static_cast<uint8>(ELRLaneChangePhase::Moving))
	{
		Result = FMath::Min(Result, AccelWithLeader(Params, OwnSpeed, FindLeader(TargetLane[Slot], S[Slot], Length[Slot], Dir, Slot)));
	}

	// Anticipate the solvability clamp: follow a "ghost" where advancing would close the last free lane of a window.
	if (Dir > 0 && bSolvabilityActive)
	{
		const double Front = FrontOf(Slot);
		int32 Defining = NoSlot;
		const double Wall = FirstBlockedPoint(Front, BlockLane[Slot], Slot, Front + GhostLookaheadM, &Defining);
		if (Wall < Infinity && Wall > Front + 0.01)
		{
			FLRIdmParams Ghost = Params;
			Ghost.TimeHeadwayS *= GhostHeadwayFactor;
			Ghost.MinGapM = 1.f;
			const float GhostSpeed = Defining != NoSlot ? Speed[Defining] : 0.f;
			Result = FMath::Min(Result, IdmAcceleration(Ghost, OwnSpeed, static_cast<float>(Wall - Front), OwnSpeed - GhostSpeed, true));
		}
	}
	return Result;
}

// ------------------------------------------------------------------------------------------------
// Lane changes
// ------------------------------------------------------------------------------------------------

bool FLRTrafficSim::IsInMobilWindow(int32 Slot) const
{
	const double Relative = S[Slot] - Player.S;
	return Relative >= -Profile.Windows.MobilBehindM && Relative <= Profile.Windows.MobilAheadM;
}

bool FLRTrafficSim::IsTargetLaneAllowed(int32 Slot, int32 Target) const
{
	if (!Lanes.IsValidIndex(Target) || FMath::Abs(Target - Lane[Slot]) != 1)
	{
		return false;
	}
	if ((Lanes[Target].Direction >= 0 ? 1 : -1) != Direction[Slot])
	{
		return false;
	}
	const FLRTrafficArchetype& Arch = Profile.Archetypes[Archetype[Slot]];
	return !Arch.bPreferRightLanes || LanePreferenceWeight(true, false, Target) > 0.f;
}

bool FLRTrafficSim::EvaluateMobilFor(int32 Slot, int32 Target, bool bIgnorePlayerFollower, FLRMobilResult& OutResult) const
{
	if (!IsTargetLaneAllowed(Slot, Target))
	{
		return false;
	}

	const double OwnS = S[Slot];
	const float OwnLength = Length[Slot];
	const float OwnSpeed = Speed[Slot];
	const int32 Dir = Direction[Slot];
	const int32 Origin = Lane[Slot];
	const FLRIdmParams Params = MakeIdmParams(Slot);
	const FLRTrafficDriverProfile& Drv = Profile.DriverProfiles[Driver[Slot]];
	const FLRTrafficArchetype& Arch = Profile.Archetypes[Archetype[Slot]];

	const FNeighbor OldLeader = FindLeader(Origin, OwnS, OwnLength, Dir, Slot);
	const FNeighbor NewLeader = FindLeader(Target, OwnS, OwnLength, Dir, Slot);
	const FNeighbor NewFollower = FindFollower(Target, OwnS, OwnLength, Dir, Slot);
	const FNeighbor OldFollower = FindFollower(Origin, OwnS, OwnLength, Dir, Slot);

	auto FollowerParams = [this](const FNeighbor& Follower, float& OutSpeed)
	{
		OutSpeed = FMath::Max(0.f, Follower.SpeedInFrame);
		return Follower.bPlayer ? MakePlayerIdmParams(OutSpeed) : MakeIdmParams(Follower.Slot);
	};

	FLRMobilInput Input;
	Input.AccelSelfOld = AccelWithLeader(Params, OwnSpeed, OldLeader);
	Input.AccelSelfNew = AccelWithLeader(Params, OwnSpeed, NewLeader);
	Input.bHasNewLeader = NewLeader.IsValid();
	Input.GapToNewLeaderM = static_cast<float>(FMath::Min(NewLeader.Gap, 1.0e6));

	if (NewFollower.IsValid() && !(bIgnorePlayerFollower && NewFollower.bPlayer))
	{
		float FollowerSpeed = 0.f;
		const FLRIdmParams FParams = FollowerParams(NewFollower, FollowerSpeed);

		FNeighbor LeaderBefore = NewLeader;
		if (LeaderBefore.IsValid())
		{
			LeaderBefore.Gap = NewFollower.Gap + OwnLength + NewLeader.Gap;
		}
		FNeighbor SelfAsLeader;
		SelfAsLeader.Slot = Slot;
		SelfAsLeader.Gap = NewFollower.Gap;
		SelfAsLeader.SpeedInFrame = OwnSpeed;

		Input.AccelNewFollowerOld = AccelWithLeader(FParams, FollowerSpeed, LeaderBefore);
		Input.AccelNewFollowerNew = AccelWithLeader(FParams, FollowerSpeed, SelfAsLeader);
		Input.bHasNewFollower = true;
		Input.GapToNewFollowerM = static_cast<float>(FMath::Min(NewFollower.Gap, 1.0e6));
	}

	if (OldFollower.IsValid())
	{
		float FollowerSpeed = 0.f;
		const FLRIdmParams FParams = FollowerParams(OldFollower, FollowerSpeed);

		FNeighbor SelfAsLeader;
		SelfAsLeader.Slot = Slot;
		SelfAsLeader.Gap = OldFollower.Gap;
		SelfAsLeader.SpeedInFrame = OwnSpeed;

		FNeighbor LeaderAfter = OldLeader;
		if (LeaderAfter.IsValid())
		{
			LeaderAfter.Gap = OldFollower.Gap + OwnLength + OldLeader.Gap;
		}

		Input.AccelOldFollowerOld = AccelWithLeader(FParams, FollowerSpeed, SelfAsLeader);
		Input.AccelOldFollowerNew = AccelWithLeader(FParams, FollowerSpeed, LeaderAfter);
		Input.bHasOldFollower = true;
	}

	Input.bTowardRight = Dir > 0 ? Target > Origin : Target < Origin;

	FLRMobilParams MobilParams;
	MobilParams.Politeness = Drv.Politeness;
	MobilParams.ThresholdMs2 = Drv.LaneChangeThresholdMs2;
	MobilParams.KeepRightBiasMs2 = Drv.KeepRightBiasMs2 + (Arch.bPreferRightLanes ? Profile.LaneChange.HeavyKeepRightBiasMs2 : 0.f);
	MobilParams.SafeDecelMs2 = Drv.SafeDecelMs2;
	MobilParams.MinGapM = Drv.MinGapM;

	OutResult = EvaluateMobil(MobilParams, Input);
	return true;
}

bool FLRTrafficSim::IsFairTowardPlayer(int32 Slot, int32 Target, bool bAggressive) const
{
	if (!Player.bValid || !PlayerOccupiesLane(Target))
	{
		return true;
	}

	const double LongGap = FMath::Abs(S[Slot] - Player.S) - 0.5 * (Length[Slot] + Player.LengthM);
	if (LongGap < Profile.Fairness.MinGapToPlayerM)
	{
		return false;
	}
	if (bAggressive)
	{
		// Aggressive events bypass the TTC rule; they are always announced (>= AggressiveMinBlinkSeconds).
		return true;
	}

	const double Sign = Player.SpeedMs >= 0.f ? 1.0 : -1.0;
	const bool bAheadOfPlayer = (S[Slot] - Player.S) * Sign > 0.0;
	if (!bAheadOfPlayer)
	{
		return true;
	}

	const double Closing = FMath::Abs(Player.SpeedMs) - static_cast<double>(Speed[Slot]) * Direction[Slot] * Sign;
	if (Closing <= 0.0)
	{
		return true;
	}
	return LongGap / Closing >= Profile.Fairness.FairnessTtcSeconds;
}

bool FLRTrafficSim::IsLaneChangeSolvable(int32 Slot, int32 Target, double MarginM) const
{
	if (!bSolvabilityActive || Direction[Slot] < 0)
	{
		return true;
	}
	const double Front = FrontOf(Slot);
	const double Rear = RearOf(Slot);
	return FirstBlockedPoint(Rear - Profile.Fairness.SolvabilityWindowM, Target, Slot, Front + MarginM) > Front + MarginM;
}

FLRTrafficSim::FLaneChangeCandidate FLRTrafficSim::EvaluateCandidate(int32 Slot, int32 Target)
{
	FLaneChangeCandidate Candidate;
	Candidate.TargetLane = Target;

	FLRMobilResult Result;
	if (!EvaluateMobilFor(Slot, Target, false, Result))
	{
		return Candidate;
	}

	const double Margin = Profile.Fairness.SolvabilityLaneChangeMarginM;
	if (Result.bChange && IsFairTowardPlayer(Slot, Target, false) && IsLaneChangeSolvable(Slot, Target, Margin))
	{
		Candidate.bApproved = true;
		Candidate.Utility = Result.Utility;
		return Candidate;
	}

	// Aggressive driver event: only relaxes the rules that protect the player (TTC and the player's braking).
	const FLRTrafficDriverProfile& Drv = Profile.DriverProfiles[Driver[Slot]];
	if (Drv.AggressiveEventChance > 0.f && PlayerOccupiesLane(Target))
	{
		FLRMobilResult Aggressive;
		if (EvaluateMobilFor(Slot, Target, true, Aggressive) && Aggressive.bChange
			&& IsFairTowardPlayer(Slot, Target, true) && IsLaneChangeSolvable(Slot, Target, Margin)
			&& BehaviorStream.FRand() < Drv.AggressiveEventChance)
		{
			Candidate.bApproved = true;
			Candidate.bAggressive = true;
			Candidate.Utility = Aggressive.Utility;
		}
	}
	return Candidate;
}

void FLRTrafficSim::SetBlinkerFlags(int32 Slot)
{
	Flags[Slot] &= ~(LRTrafficSimFlags::BlinkerLeft | LRTrafficSimFlags::BlinkerRight);
	if (Phase[Slot] == static_cast<uint8>(ELRLaneChangePhase::None))
	{
		return;
	}
	const bool bTowardHigherIndex = TargetLane[Slot] > Lane[Slot];
	const bool bDriverRight = Direction[Slot] > 0 ? bTowardHigherIndex : !bTowardHigherIndex;
	Flags[Slot] |= bDriverRight ? LRTrafficSimFlags::BlinkerRight : LRTrafficSimFlags::BlinkerLeft;
}

void FLRTrafficSim::StartSignaling(int32 Slot, const FLaneChangeCandidate& Candidate)
{
	const FLRTrafficLaneChangeParams& LC = Profile.LaneChange;
	Phase[Slot] = static_cast<uint8>(ELRLaneChangePhase::Signaling);
	TargetLane[Slot] = static_cast<int8>(Candidate.TargetLane);

	float Blink = BehaviorStream.FRandRange(LC.BlinkerMinSeconds, LC.BlinkerMaxSeconds);
	if (Candidate.bAggressive)
	{
		Blink = FMath::Max(Blink, Profile.Fairness.AggressiveMinBlinkSeconds);
		LaneChangeDuration[Slot] = Profile.Fairness.AggressiveMoveSeconds;
		Flags[Slot] |= LRTrafficSimFlags::Aggressive;
		++NumAggressiveEvents;
	}
	else
	{
		LaneChangeDuration[Slot] = BehaviorStream.FRandRange(LC.MoveMinSeconds, LC.MoveMaxSeconds);
		Flags[Slot] &= ~LRTrafficSimFlags::Aggressive;
	}
	SignalTimer[Slot] = Blink;
	SetBlinkerFlags(Slot);
}

void FLRTrafficSim::StartMoving(int32 Slot)
{
	Phase[Slot] = static_cast<uint8>(ELRLaneChangePhase::Moving);
	LaneChangeT[Slot] = 0.f;
	LaneChangeFromD[Slot] = D[Slot];
	PauseTimer[Slot] = 0.f;
	InsertIntoLane(TargetLane[Slot], Slot);
	++NumLaneChangesStarted;
}

void FLRTrafficSim::AbortLaneChange(int32 Slot)
{
	// Move back to the origin lane: swap roles so the vehicle keeps its membership in both lanes.
	const int8 Origin = Lane[Slot];
	Lane[Slot] = TargetLane[Slot];
	TargetLane[Slot] = Origin;
	LaneChangeFromD[Slot] = D[Slot];
	LaneChangeT[Slot] = 0.f;
	LaneChangeDuration[Slot] = Profile.LaneChange.AbortMoveSeconds;
	PauseTimer[Slot] = 0.f;
	Flags[Slot] &= ~LRTrafficSimFlags::Aggressive;
	SetBlinkerFlags(Slot);
}

void FLRTrafficSim::FinishLaneChange(int32 Slot)
{
	// Leave the origin lane list right away so no stale member can break its ordering during this step.
	const int32 Origin = Lane[Slot];
	if (Origin != TargetLane[Slot] && LaneMembers.IsValidIndex(Origin))
	{
		TArray<int32>& OriginMembers = LaneMembers[Origin];
		const int32 Index = OriginMembers.Find(Slot);
		if (Index != INDEX_NONE)
		{
			OriginMembers.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}

	Lane[Slot] = TargetLane[Slot];
	BlockLane[Slot] = Lane[Slot];
	D[Slot] = Lanes[Lane[Slot]].CenterD;
	Phase[Slot] = static_cast<uint8>(ELRLaneChangePhase::None);
	LaneChangeT[Slot] = 0.f;
	CooldownTimer[Slot] = Profile.LaneChange.CooldownSeconds;
	Flags[Slot] &= ~LRTrafficSimFlags::Aggressive;
	SetBlinkerFlags(Slot);
}

void FLRTrafficSim::UpdateLaneChanges(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLRTrafficSim::UpdateLaneChanges);
	using namespace LRTrafficSimPrivate;

	const FLRTrafficLaneChangeParams& LC = Profile.LaneChange;

	for (const int32 Slot : Order)
	{
		if (!IsAlive(Slot) || IsWrecked(Slot))
		{
			continue;
		}

		CooldownTimer[Slot] = FMath::Max(0.f, CooldownTimer[Slot] - DeltaSeconds);
		const ELRLaneChangePhase CurrentPhase = static_cast<ELRLaneChangePhase>(Phase[Slot]);

		if (CurrentPhase == ELRLaneChangePhase::None)
		{
			const bool bInWindow = IsInMobilWindow(Slot);
			MobilTimer[Slot] -= DeltaSeconds;
			if (!bInWindow)
			{
				// Evaluate as soon as the vehicle enters the window.
				MobilTimer[Slot] = FMath::Max(MobilTimer[Slot], 0.f);
				continue;
			}
			if (MobilTimer[Slot] > 0.f || CooldownTimer[Slot] > 0.f)
			{
				continue;
			}
			MobilTimer[Slot] += LC.MobilIntervalSeconds;
			if (MobilTimer[Slot] <= 0.f)
			{
				MobilTimer[Slot] = LC.MobilIntervalSeconds;
			}

			FLaneChangeCandidate Best;
			for (const int32 Offset : { -1, 1 })
			{
				const FLaneChangeCandidate Candidate = EvaluateCandidate(Slot, Lane[Slot] + Offset);
				if (Candidate.bApproved && (!Best.bApproved || Candidate.Utility > Best.Utility))
				{
					Best = Candidate;
				}
			}
			if (Best.bApproved)
			{
				StartSignaling(Slot, Best);
			}
		}
		else if (CurrentPhase == ELRLaneChangePhase::Signaling)
		{
			SignalTimer[Slot] -= DeltaSeconds;
			if (SignalTimer[Slot] > 0.f)
			{
				continue;
			}

			// Re-check safety, fairness and solvability right before moving.
			const bool bAggressive = (Flags[Slot] & LRTrafficSimFlags::Aggressive) != 0;
			FLRMobilResult Result;
			const bool bOk = EvaluateMobilFor(Slot, TargetLane[Slot], bAggressive, Result) && Result.bSafe
				&& IsFairTowardPlayer(Slot, TargetLane[Slot], bAggressive)
				&& IsLaneChangeSolvable(Slot, TargetLane[Slot], Profile.Fairness.SolvabilityLaneChangeMarginM);
			if (bOk)
			{
				StartMoving(Slot);
			}
			else
			{
				Phase[Slot] = static_cast<uint8>(ELRLaneChangePhase::None);
				TargetLane[Slot] = Lane[Slot];
				CooldownTimer[Slot] = 0.5f * LC.CooldownSeconds;
				Flags[Slot] &= ~LRTrafficSimFlags::Aggressive;
				SetBlinkerFlags(Slot);
			}
		}
		else // Moving
		{
			const float Duration = FMath::Max(0.1f, LaneChangeDuration[Slot]);
			const float NewT = FMath::Min(1.f, LaneChangeT[Slot] + DeltaSeconds / Duration);
			const float FromD = LaneChangeFromD[Slot];
			const float ToD = Lanes[TargetLane[Slot]].CenterD;
			const float NewD = FMath::Lerp(FromD, ToD, Smootherstep(NewT));

			const float OriginCenter = Lanes[Lane[Slot]].CenterD;
			const int8 NewBlock = FMath::Abs(NewD - ToD) < FMath::Abs(NewD - OriginCenter) ? TargetLane[Slot] : Lane[Slot];
			if (NewBlock != BlockLane[Slot])
			{
				if (!IsLaneChangeSolvable(Slot, NewBlock, 0.0))
				{
					// Crossing mid-lane now would close the last free lane of a window: wait, then give up.
					PauseTimer[Slot] += DeltaSeconds;
					if (PauseTimer[Slot] > LC.MaxPauseSeconds)
					{
						AbortLaneChange(Slot);
					}
					continue;
				}
				BlockLane[Slot] = NewBlock;
			}

			LaneChangeT[Slot] = NewT;
			D[Slot] = NewD;
			if (NewT >= 1.f)
			{
				FinishLaneChange(Slot);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Integration
// ------------------------------------------------------------------------------------------------

void FLRTrafficSim::Integrate(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLRTrafficSim::Integrate);
	using namespace LRTrafficSimPrivate;

	for (const int32 Slot : Order)
	{
		if (!IsAlive(Slot))
		{
			continue;
		}
		if (IsWrecked(Slot))
		{
			Speed[Slot] = 0.f;
			Accel[Slot] = 0.f;
			continue;
		}

		const int32 Dir = Direction[Slot];
		const float OldSpeed = Speed[Slot];
		float NewSpeed = FMath::Max(0.f, OldSpeed + Accel[Slot] * DeltaSeconds);
		const double Advance = 0.5 * (static_cast<double>(OldSpeed) + NewSpeed) * DeltaSeconds;
		const double OldS = S[Slot];
		double NewS = OldS + Dir * Advance;
		const double HalfLength = 0.5 * Length[Slot];

		// Hard no-overlap clamp against the (already integrated) leaders of every lane the vehicle occupies.
		const bool bMoving = Phase[Slot] == static_cast<uint8>(ELRLaneChangePhase::Moving);
		for (int32 Pass = 0; Pass < (bMoving ? 2 : 1); ++Pass)
		{
			const int32 LaneIndex = Pass == 0 ? Lane[Slot] : TargetLane[Slot];
			const TArray<int32>& Members = LaneMembers[LaneIndex];
			const int32 Bound = LowerBoundS(LaneIndex, OldS);
			int32 LeaderSlot = NoSlot;
			if (Dir > 0)
			{
				for (int32 Index = Bound; Index < Members.Num(); ++Index)
				{
					if (Members[Index] != Slot)
					{
						LeaderSlot = Members[Index];
						break;
					}
				}
			}
			else
			{
				for (int32 Index = Bound - 1; Index >= 0; --Index)
				{
					if (Members[Index] != Slot)
					{
						LeaderSlot = Members[Index];
						break;
					}
				}
			}
			if (LeaderSlot == NoSlot)
			{
				continue;
			}
			const double LimitS = S[LeaderSlot] - Dir * (0.5 * Length[LeaderSlot] + HalfLength + ClampGapM);
			if (Dir * (NewS - LimitS) > 0.0)
			{
				NewS = LimitS;
				NewSpeed = FMath::Min(NewSpeed, Speed[LeaderSlot]);
			}
		}

		// Never move backwards (only possible if an overlap pre-existed).
		if (Dir * (NewS - OldS) < 0.0)
		{
			NewS = OldS;
		}

		// Solvability clamp: the front may not enter a point where every other player-direction lane is blocked.
		if (Dir > 0 && bSolvabilityActive && NewS > OldS)
		{
			const double OldFront = OldS + HalfLength;
			const double NewFront = NewS + HalfLength;
			int32 Defining = NoSlot;
			const double Wall = FirstBlockedPoint(OldFront, BlockLane[Slot], Slot, NewFront + WallEpsilonM, &Defining);
			if (Wall < Infinity)
			{
				if (Wall <= OldFront + 0.01)
				{
					// Already inside a blocked window (should not happen): do not freeze the vehicle.
					++NumSolvabilityViolations;
				}
				else if (NewFront > Wall - WallEpsilonM)
				{
					NewS = FMath::Max(OldS, Wall - WallEpsilonM - HalfLength);
					if (Defining != NoSlot)
					{
						NewSpeed = FMath::Min(NewSpeed, Speed[Defining]);
					}
				}
			}
		}

		S[Slot] = NewS;
		Speed[Slot] = NewSpeed;
	}
}

// ------------------------------------------------------------------------------------------------
// Solvability
// ------------------------------------------------------------------------------------------------

bool FLRTrafficSim::QueryCoverage(int32 LaneIndex, double X, int32 ExcludeSlot, double& OutNextStart, int32& OutSlot) const
{
	const TArray<int32>& Members = LaneMembers[LaneIndex];
	const double Window = Profile.Fairness.SolvabilityWindowM;

	// Fronts are sorted like S inside a lane (no overlaps): first member whose front reaches X.
	int32 Low = 0;
	int32 High = Members.Num();
	while (Low < High)
	{
		const int32 Mid = (Low + High) / 2;
		const int32 Other = Members[Mid];
		if (S[Other] + 0.5 * Length[Other] < X)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid;
		}
	}

	for (int32 Index = Low; Index < Members.Num(); ++Index)
	{
		const int32 Other = Members[Index];
		if (Other == ExcludeSlot || !IsAlive(Other) || IsWrecked(Other) || BlockLane[Other] != LaneIndex)
		{
			continue;
		}
		// Window [x, x + W] overlaps the footprint [rear, front] iff x in [rear - W, front].
		const double Start = S[Other] - 0.5 * Length[Other] - Window;
		OutSlot = Other;
		if (Start <= X)
		{
			OutNextStart = X;
			return true;
		}
		OutNextStart = Start;
		return false;
	}

	OutSlot = NoSlot;
	OutNextStart = LRTrafficSimPrivate::Infinity;
	return false;
}

double FLRTrafficSim::FirstBlockedPoint(double FromX, int32 ExcludeLane, int32 ExcludeSlot, double MaxX, int32* OutDefiningSlot) const
{
	using namespace LRTrafficSimPrivate;

	if (!bSolvabilityActive)
	{
		return Infinity;
	}

	double X = FromX;
	int32 Defining = NoSlot;
	// Each pass moves X to the next coverage start of an uncovered lane; it converges in a few passes.
	for (int32 Iteration = 0; Iteration < 64; ++Iteration)
	{
		bool bMoved = false;
		for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
		{
			if (Lanes[LaneIndex].Direction < 0 || LaneIndex == ExcludeLane)
			{
				continue;
			}
			double Next = Infinity;
			int32 CoverSlot = NoSlot;
			if (!QueryCoverage(LaneIndex, X, ExcludeSlot, Next, CoverSlot))
			{
				if (Next >= Infinity)
				{
					return Infinity;
				}
				X = Next;
				Defining = CoverSlot;
				bMoved = true;
				if (X > MaxX)
				{
					return Infinity;
				}
			}
			else if (Defining == NoSlot)
			{
				Defining = CoverSlot;
			}
		}
		if (!bMoved)
		{
			if (OutDefiningSlot)
			{
				*OutDefiningSlot = Defining;
			}
			return X <= MaxX ? X : Infinity;
		}
	}
	return Infinity;
}

bool FLRTrafficSim::CheckSolvability(double FromS, double ToS) const
{
	if (!bSolvabilityActive)
	{
		return true;
	}
	return FirstBlockedPoint(FromS, INDEX_NONE, NoSlot, ToS) > ToS;
}

// ------------------------------------------------------------------------------------------------
// Step
// ------------------------------------------------------------------------------------------------

void FLRTrafficSim::Step(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLRTrafficSim::Step);
	using namespace LRTrafficSimPrivate;

	if (DeltaSeconds <= 0.f || Capacity == 0)
	{
		return;
	}

	SimTime += DeltaSeconds;
	if (Player.bValid)
	{
		Player.S += static_cast<double>(Player.SpeedMs) * DeltaSeconds;
	}

	// 1. Previous state (render interpolation), wreck timers and despawn outside the window.
	const double MinS = Player.S - Profile.Windows.SimBehindM;
	const double MaxS = Player.S + Profile.Windows.SimAheadM;
	for (int32 Slot = 0; Slot < Capacity; ++Slot)
	{
		if (!IsAlive(Slot))
		{
			continue;
		}
		PrevS[Slot] = S[Slot];
		PrevD[Slot] = D[Slot];

		bool bRemove = S[Slot] < MinS || S[Slot] > MaxS;
		if (IsWrecked(Slot))
		{
			WreckTimer[Slot] -= DeltaSeconds;
			bRemove |= WreckTimer[Slot] <= 0.f;
		}
		if (bRemove)
		{
			FreeSlot(Slot);
		}
	}

	// 2. Lane lists and update order.
	RebuildLaneLists();
	SortOrder();

	// 3. Accelerations from the state at the start of the step.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FLRTrafficSim::Accelerations);
		for (const int32 Slot : Order)
		{
			Accel[Slot] = IsWrecked(Slot) ? 0.f : ComputeAcceleration(Slot);
		}
	}

	// 4. Lane change decisions and lateral motion.
	UpdateLaneChanges(DeltaSeconds);

	// 5. Longitudinal integration, front first, with exact clamps.
	Integrate(DeltaSeconds);

	// 6. New vehicles.
	SpawnStep();

	// 7. Lights and yaw.
	const float InvDt = 1.f / DeltaSeconds;
	for (const int32 Slot : Order)
	{
		if (!IsAlive(Slot))
		{
			continue;
		}
		const bool bBraking = IsWrecked(Slot) || Accel[Slot] < -Profile.BrakeLightDecelMs2 || Speed[Slot] < StandingSpeedMs;
		if (bBraking)
		{
			Flags[Slot] |= LRTrafficSimFlags::Brake;
		}
		else
		{
			Flags[Slot] &= ~LRTrafficSimFlags::Brake;
		}
		const float LateralSpeed = (D[Slot] - PrevD[Slot]) * InvDt;
		YawOffsetDeg[Slot] = Direction[Slot] * FMath::RadiansToDegrees(FMath::Atan2(LateralSpeed, FMath::Max(Speed[Slot], MinYawSpeedMs)));
	}
}

// ------------------------------------------------------------------------------------------------
// External events and queries
// ------------------------------------------------------------------------------------------------

bool FLRTrafficSim::SetWreckedById(int32 InId)
{
	const int32 Slot = FindSlotById(InId);
	if (Slot == NoSlot)
	{
		return false;
	}
	if (!IsWrecked(Slot))
	{
		Flags[Slot] |= LRTrafficSimFlags::Wrecked | LRTrafficSimFlags::Brake;
		Flags[Slot] &= ~(LRTrafficSimFlags::BlinkerLeft | LRTrafficSimFlags::BlinkerRight | LRTrafficSimFlags::Aggressive);
		WreckTimer[Slot] = Profile.WreckLifetimeSeconds;
		if (Phase[Slot] == static_cast<uint8>(ELRLaneChangePhase::Signaling))
		{
			Phase[Slot] = static_cast<uint8>(ELRLaneChangePhase::None);
			TargetLane[Slot] = Lane[Slot];
		}
	}
	Speed[Slot] = 0.f;
	Accel[Slot] = 0.f;
	return true;
}

bool FLRTrafficSim::RemoveById(int32 InId)
{
	const int32 Slot = FindSlotById(InId);
	if (Slot == NoSlot)
	{
		return false;
	}
	FreeSlot(Slot);
	return true;
}

void FLRTrafficSim::GetSnapshot(int32 Slot, FLRTrafficVehicleSnapshot& Out) const
{
	Out.Id = Id[Slot];
	Out.S = S[Slot];
	Out.D = D[Slot];
	Out.YawOffsetDeg = YawOffsetDeg[Slot];
	Out.SpeedMs = Speed[Slot];
	Out.Direction = Direction[Slot];
	Out.Archetype = Profile.Archetypes[Archetype[Slot]].Name;
	Out.LightFlags = GetLightFlags(Slot);
	Out.LengthMeters = Length[Slot];
	Out.WidthMeters = Width[Slot];
}

void FLRTrafficSim::GetInterpolated(int32 Slot, float Alpha, double& OutS, float& OutD) const
{
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	OutS = PrevS[Slot] + (S[Slot] - PrevS[Slot]) * A;
	OutD = FMath::Lerp(PrevD[Slot], D[Slot], A);
}

void FLRTrafficSim::GetLaneOccupancy(double S0, double S1, TArray<int32>& OutCounts) const
{
	OutCounts.Reset();
	OutCounts.SetNumZeroed(Lanes.Num());
	for (const int32 Slot : Order)
	{
		if (IsAlive(Slot) && S[Slot] >= S0 && S[Slot] <= S1)
		{
			++OutCounts[BlockLane[Slot]];
		}
	}
}

uint32 FLRTrafficSim::ComputeChecksum() const
{
	using LRTrafficSimPrivate::Crc;

	uint32 Result = Crc(SimTime, 0);
	Result = Crc(NumAlive, Result);
	for (int32 Slot = 0; Slot < Capacity; ++Slot)
	{
		if (!IsAlive(Slot))
		{
			continue;
		}
		Result = Crc(Id[Slot], Result);
		Result = Crc(S[Slot], Result);
		Result = Crc(D[Slot], Result);
		Result = Crc(Speed[Slot], Result);
		Result = Crc(Lane[Slot], Result);
		Result = Crc(TargetLane[Slot], Result);
		Result = Crc(Phase[Slot], Result);
		Result = Crc(Flags[Slot], Result);
	}
	return Result;
}

bool FLRTrafficSim::ValidateNoLaneOverlap(FString* OutError) const
{
	for (int32 LaneIndex = 0; LaneIndex < LaneMembers.Num(); ++LaneIndex)
	{
		const TArray<int32>& Members = LaneMembers[LaneIndex];
		int32 A = NoSlot;
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			// Skip dead slots and stale members (a lane change that finished this step leaves its origin list at the next step).
			const int32 B = Members[Index];
			if (!IsAlive(B) || !IsMemberOfLane(B, LaneIndex))
			{
				continue;
			}
			const int32 Previous = A;
			A = B;
			if (Previous == NoSlot)
			{
				continue;
			}
			if (S[B] < S[Previous])
			{
				if (OutError)
				{
					*OutError = FString::Printf(TEXT("Lane %d not sorted: id %d (S=%.3f) before id %d (S=%.3f)"), LaneIndex, Id[Previous], S[Previous], Id[B], S[B]);
				}
				return false;
			}
			const double Gap = (S[B] - 0.5 * Length[B]) - (S[Previous] + 0.5 * Length[Previous]);
			if (Gap < -1.0e-3)
			{
				if (OutError)
				{
					*OutError = FString::Printf(TEXT("Lane %d overlap %.3f m between id %d (S=%.3f) and id %d (S=%.3f)"), LaneIndex, Gap, Id[Previous], S[Previous], Id[B], S[B]);
				}
				return false;
			}
		}
	}
	return true;
}

bool FLRTrafficSim::ValidateFiniteState(FString* OutError) const
{
	for (int32 Slot = 0; Slot < Capacity; ++Slot)
	{
		if (!IsAlive(Slot))
		{
			continue;
		}
		const bool bFinite = FMath::IsFinite(S[Slot]) && FMath::IsFinite(D[Slot]) && FMath::IsFinite(Speed[Slot]) && FMath::IsFinite(Accel[Slot]);
		if (!bFinite || Speed[Slot] < 0.f)
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("Invalid state for id %d: S=%f D=%f V=%f A=%f"), Id[Slot], S[Slot], D[Slot], Speed[Slot], Accel[Slot]);
			}
			return false;
		}
	}
	return true;
}
