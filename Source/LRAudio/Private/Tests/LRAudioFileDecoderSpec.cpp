// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRAudioFileDecoder.h"
#include "Misc/AutomationTest.h"

namespace LRAudioFileDecoderSpecPrivate
{
	LRAudioFileDecoder::FCacheEntry MakeEntry(const TCHAR* Path, int64 SizeBytes, int32 Day)
	{
		LRAudioFileDecoder::FCacheEntry Entry;
		Entry.Path = Path;
		Entry.SizeBytes = SizeBytes;
		Entry.LastUsed = FDateTime(2026, 1, Day);
		return Entry;
	}
}

BEGIN_DEFINE_SPEC(FLRAudioFileDecoderSpec, "LeftRight.Audio.FileDecoder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRAudioFileDecoderSpec)

void FLRAudioFileDecoderSpec::Define()
{
	using namespace LRAudioFileDecoderSpecPrivate;

	It("converts only the formats the media player rejects", [this]()
	{
		TestTrue(TEXT("mp3"), LRAudioFileDecoder::NeedsConversion(TEXT("C:/Music/song.MP3")));
		TestTrue(TEXT("flac"), LRAudioFileDecoder::NeedsConversion(TEXT("song.flac")));
		TestFalse(TEXT("m4a"), LRAudioFileDecoder::NeedsConversion(TEXT("song.m4a")));
		TestFalse(TEXT("wav"), LRAudioFileDecoder::NeedsConversion(TEXT("song.wav")));
	});

	Describe("ChooseCacheFilesToDelete", [this]()
	{
		It("deletes nothing while the cache fits", [this]()
		{
			const TArray<LRAudioFileDecoder::FCacheEntry> Entries = { MakeEntry(TEXT("C:/Cache/a.wav"), 40, 1), MakeEntry(TEXT("C:/Cache/b.wav"), 60, 2) };
			TestEqual(TEXT("Nothing to delete"), LRAudioFileDecoder::ChooseCacheFilesToDelete(Entries, 100, FString()).Num(), 0);
		});

		It("deletes the copies that went longest without being played, until the rest fits", [this]()
		{
			const TArray<LRAudioFileDecoder::FCacheEntry> Entries = {
				MakeEntry(TEXT("C:/Cache/new.wav"), 50, 9), MakeEntry(TEXT("C:/Cache/old.wav"), 50, 1), MakeEntry(TEXT("C:/Cache/mid.wav"), 50, 5) };

			const TArray<FString> OneOver = LRAudioFileDecoder::ChooseCacheFilesToDelete(Entries, 100, FString());
			if (TestEqual(TEXT("One file over the limit"), OneOver.Num(), 1))
			{
				TestEqual(TEXT("Oldest first"), OneOver[0], FString(TEXT("C:/Cache/old.wav")));
			}

			const TArray<FString> TwoOver = LRAudioFileDecoder::ChooseCacheFilesToDelete(Entries, 60, FString());
			if (TestEqual(TEXT("Two files over the limit"), TwoOver.Num(), 2))
			{
				TestEqual(TEXT("Oldest first"), TwoOver[0], FString(TEXT("C:/Cache/old.wav")));
				TestEqual(TEXT("Then the next oldest"), TwoOver[1], FString(TEXT("C:/Cache/mid.wav")));
			}
		});

		It("never deletes the copy that was just made", [this]()
		{
			const TArray<LRAudioFileDecoder::FCacheEntry> Entries = { MakeEntry(TEXT("C:/Cache/kept.wav"), 500, 1), MakeEntry(TEXT("C:/Cache/other.wav"), 50, 2) };
			const TArray<FString> ToDelete = LRAudioFileDecoder::ChooseCacheFilesToDelete(Entries, 100, TEXT("C:/Cache/kept.wav"));
			TestFalse(TEXT("Kept file survives"), ToDelete.Contains(FString(TEXT("C:/Cache/kept.wav"))));
			TestTrue(TEXT("The other one goes"), ToDelete.Contains(FString(TEXT("C:/Cache/other.wav"))));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
