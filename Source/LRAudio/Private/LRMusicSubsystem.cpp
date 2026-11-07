// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRMusicSubsystem.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "LRAudio.h"
#include "LRAudioFileDecoder.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace LRMusicSubsystemPrivate
{
	FString GetBuiltInMusicFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/Music"));
	}
}

ULRMusicSubsystem* ULRMusicSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<ULRMusicSubsystem>() : nullptr;
}

bool ULRMusicSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULRMusicSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (ULRUserSettingsSubsystem* Settings = ULRUserSettingsSubsystem::Get(&InWorld))
	{
		AppliedSettings = Settings->GetSettings();
		SettingsHandle = Settings->OnChanged.AddUObject(this, &ULRMusicSubsystem::HandleSettingsChanged);
	}

	Player = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
	Player->PlayOnOpen = true;
	Player->SetLooping(false);
	Player->OnEndReached.AddDynamic(this, &ULRMusicSubsystem::HandleEndReached);
	Player->OnMediaOpenFailed.AddDynamic(this, &ULRMusicSubsystem::HandleOpenFailed);

	// The sound needs a component in the world, ticking and audible while the game is paused.
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	AActor* Host = InWorld.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (Host == nullptr)
	{
		return;
	}
	Host->SetTickableWhenPaused(true);

	Sound = NewObject<UMediaSoundComponent>(Host, NAME_None, RF_Transient);
	Sound->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Sound->bIsUISound = true;
	Sound->bAllowSpatialization = false;
	Sound->SetMediaPlayer(Player);
	Sound->RegisterComponent();
	Sound->SetVolumeMultiplier(AppliedSettings.MusicVolume);
	Sound->SetEnableEnvelopeFollowing(true);
	Sound->Start();

	RescanTracks();
	PlayCurrent();
}

void ULRMusicSubsystem::Deinitialize()
{
	if (ULRUserSettingsSubsystem* Settings = ULRUserSettingsSubsystem::Get(GetWorld()))
	{
		Settings->OnChanged.Remove(SettingsHandle);
	}
	if (Player)
	{
		Player->Close();
	}
	Super::Deinitialize();
}

void ULRMusicSubsystem::NextTrack()
{
	Playlist.Next();
	ConsecutiveFailures = 0;
	PlayCurrent();
}

void ULRMusicSubsystem::PreviousTrack()
{
	Playlist.Previous();
	ConsecutiveFailures = 0;
	PlayCurrent();
}

void ULRMusicSubsystem::RescanTracks()
{
	const FString Current = Playlist.GetCurrent();

	Playlist.SetTracks(FLRPlaylist::FindTracks(GetUserMusicFolder()), FLRPlaylist::FindTracks(LRMusicSubsystemPrivate::GetBuiltInMusicFolder()));
	Playlist.SetShuffle(AppliedSettings.bShuffleMusic, static_cast<int32>(FDateTime::UtcNow().GetTicks() % 100000) + 1);

	// Back to the track that was playing, if it is still in the list.
	for (int32 Step = 0; Step < Playlist.Num() && !Current.IsEmpty(); ++Step)
	{
		if (Playlist.GetCurrent() == Current)
		{
			break;
		}
		Playlist.Next();
	}
	UE_LOG(LogLRAudio, Log, TEXT("Music: %d track(s) found"), Playlist.Num());
}

FString ULRMusicSubsystem::GetCurrentTrackName() const
{
	return AppliedSettings.bMusicEnabled ? Playlist.GetCurrentName() : FString();
}

FString ULRMusicSubsystem::GetUserMusicFolder()
{
	return FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserDir()) / TEXT("LeftRight/Music"));
}

void ULRMusicSubsystem::OpenUserMusicFolder() const
{
	const FString Folder = GetUserMusicFolder();
	IFileManager::Get().MakeDirectory(*Folder, /*Tree*/ true);

	const FString Note = Folder / TEXT("LEEME.txt");
	if (!IFileManager::Get().FileExists(*Note))
	{
		FFileHelper::SaveStringToFile(
			TEXT("Copia aqui tu musica (mp3, wav, m4a, aac, wma o flac).\r\nLuego, en el juego, abre el menu con Esc y pulsa Buscar musica nueva.\r\n"),
			*Note);
	}
	FPlatformProcess::ExploreFolder(*Folder);
}

void ULRMusicSubsystem::PlayCurrent()
{
	if (Player == nullptr)
	{
		return;
	}

	++PlayRequest;
	const FString Track = Playlist.GetCurrent();
	if (!AppliedSettings.bMusicEnabled || Track.IsEmpty())
	{
		Player->Close();
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		TrackChangedSeconds = World->GetRealTimeSeconds();
	}

	if (!LRAudioFileDecoder::NeedsConversion(Track))
	{
		OpenPlayable(Track);
		return;
	}

	const FString CachePath = LRAudioFileDecoder::GetCachePath(Track);
	if (IFileManager::Get().FileExists(*CachePath))
	{
		OpenPlayable(CachePath);
	}
	else
	{
		Player->Close();
		ConvertAndPlay(Track, CachePath);
	}
}

void ULRMusicSubsystem::OpenPlayable(const FString& Path)
{
	if (Player->OpenFile(Path))
	{
		UE_LOG(LogLRAudio, Log, TEXT("Music: playing %s"), *Path);
	}
	else
	{
		UE_LOG(LogLRAudio, Warning, TEXT("Music: could not open %s"), *Path);
		HandleOpenFailed(Path);
	}
}

void ULRMusicSubsystem::ConvertAndPlay(const FString& Track, const FString& CachePath)
{
	UE_LOG(LogLRAudio, Log, TEXT("Music: converting %s"), *Track);
	TWeakObjectPtr<ULRMusicSubsystem> WeakThis(this);
	const uint32 Request = PlayRequest;

	Async(EAsyncExecution::Thread, [WeakThis, Track, CachePath, Request]()
	{
		FString Error;
		const bool bConverted = LRAudioFileDecoder::DecodeToWav(Track, CachePath, Error);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Track, CachePath, Request, bConverted, Error]()
		{
			ULRMusicSubsystem* Self = WeakThis.Get();
			if (Self == nullptr || Self->PlayRequest != Request)
			{
				return;
			}
			if (bConverted)
			{
				Self->OpenPlayable(CachePath);
			}
			else
			{
				UE_LOG(LogLRAudio, Warning, TEXT("Music: could not convert %s: %s"), *Track, *Error);
				Self->HandleOpenFailed(Track);
			}
		});
	});
}

float ULRMusicSubsystem::GetOutputLevel() const
{
	return Sound ? Sound->GetEnvelopeValue() : 0.f;
}

void ULRMusicSubsystem::HandleSettingsChanged(const FLRUserSettings& Settings)
{
	const bool bPowerChanged = Settings.bMusicEnabled != AppliedSettings.bMusicEnabled;
	const bool bShuffleChanged = Settings.bShuffleMusic != AppliedSettings.bShuffleMusic;
	AppliedSettings = Settings;

	if (Sound)
	{
		Sound->SetVolumeMultiplier(Settings.MusicVolume);
	}
	if (bShuffleChanged)
	{
		Playlist.SetShuffle(Settings.bShuffleMusic, static_cast<int32>(FDateTime::UtcNow().GetTicks() % 100000) + 1);
	}
	if (bPowerChanged)
	{
		PlayCurrent();
	}
}

void ULRMusicSubsystem::HandleEndReached()
{
	NextTrack();
}

void ULRMusicSubsystem::HandleOpenFailed(FString FailedUrl)
{
	UE_LOG(LogLRAudio, Warning, TEXT("Music: cannot play %s, skipping it"), *FailedUrl);
	if (++ConsecutiveFailures < Playlist.Num())
	{
		Playlist.Next();
		PlayCurrent();
	}
}
