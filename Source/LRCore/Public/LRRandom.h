// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * Deterministic seeding helpers.
 *
 * Every run has a single 32-bit seed. Each subsystem derives its own independent stream from it,
 * so adding random calls in one system never changes the sequence of another (replays, daily challenge).
 * Never use FMath::Rand() in simulation code.
 */
namespace LRRandom
{
	/** Stable identifiers for per-subsystem random streams. Append only: never reorder or reuse values. */
	enum class EStream : uint32
	{
		RoadLayout = 1,
		RoadDecoration = 2,
		TrafficSpawn = 3,
		TrafficBehavior = 4,
		Weather = 5,
		Events = 6
	};

	/** Derives a sub-seed from the run seed and a stream id (SplitMix-style 64-bit mix, deterministic across platforms). */
	inline int32 DeriveSeed(int32 RunSeed, EStream Stream)
	{
		uint64 X = (static_cast<uint64>(static_cast<uint32>(RunSeed)) << 32) ^ static_cast<uint64>(Stream);
		X += 0x9E3779B97F4A7C15ull;
		X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
		X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
		X = X ^ (X >> 31);
		return static_cast<int32>(static_cast<uint32>(X ^ (X >> 32)));
	}

	/** Convenience: builds an FRandomStream for a subsystem. */
	inline FRandomStream MakeStream(int32 RunSeed, EStream Stream)
	{
		return FRandomStream(DeriveSeed(RunSeed, Stream));
	}

	/** Seed for the daily challenge: same value for every player on the same UTC date (YYYYMMDD). */
	inline int32 DailySeed(int32 Year, int32 Month, int32 Day)
	{
		return DeriveSeed(Year * 10000 + Month * 100 + Day, EStream::Events);
	}
}
