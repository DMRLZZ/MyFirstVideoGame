// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRAudioFileDecoder.h"

#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/Archive.h"
#include "Templates/UniquePtr.h"

#if PLATFORM_WINDOWS
#include "Microsoft/COMPointer.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

bool LRAudioFileDecoder::NeedsConversion(const FString& Path)
{
	const FString Extension = FPaths::GetExtension(Path);
	return Extension.Equals(TEXT("mp3"), ESearchCase::IgnoreCase) || Extension.Equals(TEXT("flac"), ESearchCase::IgnoreCase);
}

FString LRAudioFileDecoder::GetCachePath(const FString& Source)
{
	const int64 Size = IFileManager::Get().FileSize(*Source);
	const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Source);
	const FString Key = FString::Printf(TEXT("%s|%lld|%lld"), *Source, Size, Stamp.GetTicks());
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MusicCache") / (FMD5::HashAnsiString(*Key) + TEXT(".wav")));
}

#if PLATFORM_WINDOWS

namespace LRAudioFileDecoderPrivate
{
	/** The sizes of a WAV are 32-bit: the most audio one file can hold, bytes. */
	constexpr uint64 MaxWavDataBytes = MAX_uint32 - 64;

	/** RIFF header of a 16-bit PCM file. */
	void WriteWavHeader(FArchive& Writer, uint32 Channels, uint32 SampleRate, uint32 DataBytes)
	{
		const auto Tag = [&Writer](const char* Text) { Writer.Serialize(const_cast<char*>(Text), 4); };
		const auto U32 = [&Writer](uint32 Value) { Writer.Serialize(&Value, sizeof(Value)); };
		const auto U16 = [&Writer](uint16 Value) { Writer.Serialize(&Value, sizeof(Value)); };

		Tag("RIFF");
		U32(36 + DataBytes);
		Tag("WAVE");
		Tag("fmt ");
		U32(16);
		U16(1); // PCM.
		U16(static_cast<uint16>(Channels));
		U32(SampleRate);
		U32(SampleRate * Channels * 2);
		U16(static_cast<uint16>(Channels * 2));
		U16(16);
		Tag("data");
		U32(DataBytes);
	}

	bool Decode(const FString& Source, FArchive& Writer, FString& OutError)
	{
		TComPtr<IMFSourceReader> Reader;
		if (FAILED(MFCreateSourceReaderFromURL(*Source, nullptr, &Reader)))
		{
			OutError = TEXT("Windows cannot read this file");
			return false;
		}

		// Ask for plain PCM and read back what Windows really gives: it keeps the rate and channels of the file.
		TComPtr<IMFMediaType> Wanted;
		MFCreateMediaType(&Wanted);
		Wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		Wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
		Wanted->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

		const DWORD Stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
		Reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), 0);
		Reader->SetStreamSelection(Stream, 1);
		if (FAILED(Reader->SetCurrentMediaType(Stream, nullptr, Wanted)))
		{
			OutError = TEXT("Windows cannot decode this file to PCM");
			return false;
		}

		TComPtr<IMFMediaType> Actual;
		if (FAILED(Reader->GetCurrentMediaType(Stream, &Actual)))
		{
			OutError = TEXT("No audio format");
			return false;
		}
		UINT32 Channels = 2;
		UINT32 SampleRate = 44100;
		Actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &Channels);
		Actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &SampleRate);

		WriteWavHeader(Writer, Channels, SampleRate, 0);

		uint32 DataBytes = 0;
		while (true)
		{
			DWORD Flags = 0;
			TComPtr<IMFSample> Sample;
			if (FAILED(Reader->ReadSample(Stream, 0, nullptr, &Flags, nullptr, &Sample)))
			{
				OutError = TEXT("Reading failed halfway");
				return false;
			}
			if (Flags & MF_SOURCE_READERF_ENDOFSTREAM)
			{
				break;
			}
			if (!Sample.IsValid())
			{
				continue;
			}

			TComPtr<IMFMediaBuffer> Buffer;
			if (FAILED(Sample->ConvertToContiguousBuffer(&Buffer)))
			{
				OutError = TEXT("Bad audio buffer");
				return false;
			}
			BYTE* Data = nullptr;
			DWORD Length = 0;
			if (SUCCEEDED(Buffer->Lock(&Data, nullptr, &Length)))
			{
				const bool bFits = static_cast<uint64>(DataBytes) + Length <= MaxWavDataBytes;
				if (bFits)
				{
					Writer.Serialize(Data, Length);
					DataBytes += Length;
				}
				Buffer->Unlock();
				if (!bFits)
				{
					OutError = TEXT("The file is too long to convert");
					return false;
				}
			}

			// A full disk must not leave half a song behind with a valid name.
			if (Writer.IsError())
			{
				OutError = TEXT("Cannot write to the music cache");
				return false;
			}
		}

		if (DataBytes == 0)
		{
			OutError = TEXT("The file has no audio");
			return false;
		}
		Writer.Seek(0);
		WriteWavHeader(Writer, Channels, SampleRate, DataBytes);
		if (Writer.IsError())
		{
			OutError = TEXT("Cannot write to the music cache");
			return false;
		}
		return true;
	}
}

bool LRAudioFileDecoder::DecodeToWav(const FString& Source, const FString& Destination, FString& OutError)
{
	IFileManager& Files = IFileManager::Get();
	const FString Temporary = Destination + TEXT(".part");
	Files.MakeDirectory(*FPaths::GetPath(Destination), /*Tree*/ true);

	const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(MFStartup(MF_VERSION)))
	{
		OutError = TEXT("Media Foundation is not available");
		if (SUCCEEDED(ComResult))
		{
			CoUninitialize();
		}
		return false;
	}

	bool bDecoded = false;
	{
		const TUniquePtr<FArchive> Writer(Files.CreateFileWriter(*Temporary));
		if (Writer.IsValid())
		{
			bDecoded = LRAudioFileDecoderPrivate::Decode(Source, *Writer, OutError);
			if (!Writer->Close() && bDecoded)
			{
				// The last block is written on close.
				bDecoded = false;
				OutError = TEXT("Cannot write to the music cache");
			}
		}
		else
		{
			OutError = TEXT("Cannot write to the music cache");
		}
	}

	MFShutdown();
	if (SUCCEEDED(ComResult))
	{
		CoUninitialize();
	}

	// Only a complete file gets the final name: a run cut short leaves nothing the player could pick up.
	if (bDecoded)
	{
		bDecoded = Files.Move(*Destination, *Temporary);
	}
	else
	{
		Files.Delete(*Temporary);
	}
	return bDecoded;
}

#else

bool LRAudioFileDecoder::DecodeToWav(const FString& Source, const FString& Destination, FString& OutError)
{
	OutError = TEXT("Only available on Windows");
	return false;
}

#endif
