// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRPlaylist.h"

#include "HAL/FileManager.h"
#include "Math/RandomStream.h"
#include "Misc/Paths.h"

bool FLRPlaylist::IsSupportedFile(const FString& Path)
{
	static const TCHAR* const Extensions[] = { TEXT("mp3"), TEXT("wav"), TEXT("m4a"), TEXT("aac"), TEXT("wma"), TEXT("flac") };
	const FString Extension = FPaths::GetExtension(Path);
	for (const TCHAR* Candidate : Extensions)
	{
		if (Extension.Equals(Candidate, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

TArray<FString> FLRPlaylist::FindTracks(const FString& Folder)
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *(Folder / TEXT("*")), /*Files*/ true, /*Directories*/ false);
	Names.Sort();

	TArray<FString> Paths;
	for (const FString& Name : Names)
	{
		if (IsSupportedFile(Name))
		{
			Paths.Add(Folder / Name);
		}
	}
	return Paths;
}

void FLRPlaylist::SetTracks(const TArray<FString>& UserTracks, const TArray<FString>& BuiltInTracks)
{
	Tracks = UserTracks;
	Tracks.Append(BuiltInTracks);
	Position = 0;
	BuildOrder(0);
}

void FLRPlaylist::SetShuffle(bool bEnabled, int32 Seed)
{
	const int32 CurrentTrack = Order.IsValidIndex(Position) ? Order[Position] : INDEX_NONE;
	BuildOrder(bEnabled ? Seed : 0);
	if (bEnabled)
	{
		// The track that is playing goes first, so turning shuffle on does not interrupt it.
		if (const int32 Found = Order.Find(CurrentTrack); Found != INDEX_NONE)
		{
			Order.Swap(0, Found);
		}
		Position = 0;
	}
	else
	{
		Position = FMath::Max(Order.Find(CurrentTrack), 0);
	}
}

void FLRPlaylist::BuildOrder(int32 Seed)
{
	Order.Reset(Tracks.Num());
	for (int32 Index = 0; Index < Tracks.Num(); ++Index)
	{
		Order.Add(Index);
	}
	if (Seed != 0)
	{
		FRandomStream Random(Seed);
		for (int32 Index = Order.Num() - 1; Index > 0; --Index)
		{
			Order.Swap(Index, Random.RandRange(0, Index));
		}
	}
}

FString FLRPlaylist::GetCurrent() const
{
	return Order.IsValidIndex(Position) ? Tracks[Order[Position]] : FString();
}

FString FLRPlaylist::GetCurrentName() const
{
	return FPaths::GetBaseFilename(GetCurrent()).Replace(TEXT("_"), TEXT(" "));
}

void FLRPlaylist::Next()
{
	if (Order.Num() > 0)
	{
		Position = (Position + 1) % Order.Num();
	}
}

void FLRPlaylist::Previous()
{
	if (Order.Num() > 0)
	{
		Position = (Position + Order.Num() - 1) % Order.Num();
	}
}
