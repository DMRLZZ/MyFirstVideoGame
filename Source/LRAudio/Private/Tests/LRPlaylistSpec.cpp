// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRPlaylist.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRPlaylistSpec, "LeftRight.Audio.Playlist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRPlaylistSpec)

void FLRPlaylistSpec::Define()
{
	It("recognizes the formats it can play", [this]()
	{
		TestTrue(TEXT("mp3"), FLRPlaylist::IsSupportedFile(TEXT("C:/Music/song.mp3")));
		TestTrue(TEXT("upper case"), FLRPlaylist::IsSupportedFile(TEXT("C:/Music/SONG.WAV")));
		TestTrue(TEXT("m4a"), FLRPlaylist::IsSupportedFile(TEXT("song.m4a")));
		TestFalse(TEXT("text"), FLRPlaylist::IsSupportedFile(TEXT("LEEME.txt")));
		TestFalse(TEXT("no extension"), FLRPlaylist::IsSupportedFile(TEXT("song")));
	});

	It("finds nothing in a folder that does not exist", [this]()
	{
		TestEqual(TEXT("No tracks"), FLRPlaylist::FindTracks(TEXT("C:/DoesNotExist/LeftRight")).Num(), 0);
	});

	It("is safe with no tracks", [this]()
	{
		FLRPlaylist Playlist;
		Playlist.Next();
		Playlist.Previous();
		Playlist.SetShuffle(true, 5);
		TestEqual(TEXT("Empty path"), Playlist.GetCurrent(), FString());
		TestEqual(TEXT("Empty name"), Playlist.GetCurrentName(), FString());
	});

	It("plays the player's music first and wraps around", [this]()
	{
		FLRPlaylist Playlist;
		Playlist.SetTracks({ TEXT("mine.mp3") }, { TEXT("a.mp3"), TEXT("b.mp3") });
		TestEqual(TEXT("Count"), Playlist.Num(), 3);
		TestEqual(TEXT("First"), Playlist.GetCurrent(), FString(TEXT("mine.mp3")));
		Playlist.Next();
		Playlist.Next();
		TestEqual(TEXT("Last"), Playlist.GetCurrent(), FString(TEXT("b.mp3")));
		Playlist.Next();
		TestEqual(TEXT("Wraps to the first"), Playlist.GetCurrent(), FString(TEXT("mine.mp3")));
		Playlist.Previous();
		TestEqual(TEXT("Wraps to the last"), Playlist.GetCurrent(), FString(TEXT("b.mp3")));
	});

	It("shows a clean name", [this]()
	{
		FLRPlaylist Playlist;
		Playlist.SetTracks({}, { TEXT("C:/Data/Music/Neon_Freeway.mp3") });
		TestEqual(TEXT("Name"), Playlist.GetCurrentName(), FString(TEXT("Neon Freeway")));
	});

	It("shuffles without repeating and keeps the current track", [this]()
	{
		FLRPlaylist Playlist;
		Playlist.SetTracks({}, { TEXT("a"), TEXT("b"), TEXT("c"), TEXT("d"), TEXT("e") });
		Playlist.Next();
		Playlist.SetShuffle(true, 1234);
		TestEqual(TEXT("Still playing"), Playlist.GetCurrent(), FString(TEXT("b")));

		TSet<FString> Seen;
		for (int32 Index = 0; Index < Playlist.Num(); ++Index)
		{
			Seen.Add(Playlist.GetCurrent());
			Playlist.Next();
		}
		TestEqual(TEXT("Every track once"), Seen.Num(), 5);

		Playlist.SetShuffle(false, 0);
		const FString Current = Playlist.GetCurrent();
		Playlist.Next();
		TestNotEqual(TEXT("Moves on"), Playlist.GetCurrent(), Current);
	});
}

#endif
