// Copyright LEFTRIGHT. All Rights Reserved.

#include "SLRSettingsList.h"

#include "LRCoreTypes.h"
#include "LRMenuWidgets.h"
#include "LRMusicSubsystem.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LRSettingsList"

void SLRSettingsList::Construct(const FArguments& InArgs)
{
	using namespace LRMenu;

	Settings = InArgs._Settings;
	Music = InArgs._Music;
	CarName = InArgs._CarName;
	OnCycleCar = InArgs._OnCycleCar;

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const auto Add = [&Box](const TSharedRef<SWidget>& Item)
	{
		Box->AddSlot().AutoHeight()[Item];
	};

	Add(MakeHeader(LOCTEXT("HeaderDriving", "CONDUCCIÓN")));
	Add(MakeRow(LOCTEXT("Scheme", "Dirección"),
		TAttribute<FText>::CreateLambda([this]()
		{
			switch (ReadSettings().ControlScheme)
			{
			case ELRControlScheme::Arcade: return LOCTEXT("SchemeLanes", "Por carriles");
			case ELRControlScheme::Simcade: return LOCTEXT("SchemeSim", "Simulación");
			default: return LOCTEXT("SchemeSmoothOption", "Suave");
			}
		}),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S)
			{
				static const ELRControlScheme Order[] = { ELRControlScheme::Smooth, ELRControlScheme::Arcade, ELRControlScheme::Simcade };
				int32 Index = 0;
				for (int32 Candidate = 0; Candidate < UE_ARRAY_COUNT(Order); ++Candidate)
				{
					Index = Order[Candidate] == S.ControlScheme ? Candidate : Index;
				}
				S.ControlScheme = Order[(Index + Direction + UE_ARRAY_COUNT(Order)) % UE_ARRAY_COUNT(Order)];
			});
		}));
	Add(MakeRow(LOCTEXT("Sensitivity", "Sensibilidad"),
		TAttribute<FText>::CreateLambda([this]() { return Percent(ReadSettings().SteeringSensitivity); }),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S) { S.SteeringSensitivity = StepValue(S.SteeringSensitivity, Direction, 0.1f, 0.4f, 1.6f); });
		}));
	if (OnCycleCar)
	{
		Add(MakeRow(LOCTEXT("Car", "Coche"), CarName, [this](int32 Direction) { OnCycleCar(Direction); }));
	}

	Add(MakeHeader(LOCTEXT("HeaderSound", "SONIDO")));
	Add(MakeRow(LOCTEXT("MusicOn", "Música"),
		TAttribute<FText>::CreateLambda([this]() { return OnOff(ReadSettings().bMusicEnabled); }),
		[this](int32) { EditSettings([](FLRUserSettings& S) { S.bMusicEnabled = !S.bMusicEnabled; }); }));
	Add(MakeRow(LOCTEXT("MusicVolume", "Volumen de la música"),
		TAttribute<FText>::CreateLambda([this]() { return Percent(ReadSettings().MusicVolume); }),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S) { S.MusicVolume = StepValue(S.MusicVolume, Direction, 0.1f, 0.f, 1.f); });
		}));
	Add(MakeRow(LOCTEXT("EffectsVolume", "Volumen del coche y efectos"),
		TAttribute<FText>::CreateLambda([this]() { return Percent(ReadSettings().EffectsVolume); }),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S) { S.EffectsVolume = StepValue(S.EffectsVolume, Direction, 0.1f, 0.f, 1.f); });
		}));
	Add(MakeRow(LOCTEXT("Track", "Canción"),
		TAttribute<FText>::CreateLambda([this]()
		{
			const ULRMusicSubsystem* Player = Music.Get();
			const FString Name = Player ? Player->GetCurrentTrackName() : FString();
			return Name.IsEmpty() ? LOCTEXT("NoTrack", "Sin música") : FText::FromString(Name);
		}),
		[this](int32 Direction)
		{
			if (ULRMusicSubsystem* Player = Music.Get())
			{
				Direction > 0 ? Player->NextTrack() : Player->PreviousTrack();
			}
		}));
	Add(MakeRow(LOCTEXT("Shuffle", "Orden aleatorio"),
		TAttribute<FText>::CreateLambda([this]() { return OnOff(ReadSettings().bShuffleMusic); }),
		[this](int32) { EditSettings([](FLRUserSettings& S) { S.bShuffleMusic = !S.bShuffleMusic; }); }));
	Add(SNew(SBox).Padding(0.f, 2.f)
	[
		MakeButton(LOCTEXT("AddMusic", "Añadir mi música (abre la carpeta)"), FSimpleDelegate::CreateLambda([this]()
		{
			if (const ULRMusicSubsystem* Player = Music.Get())
			{
				Player->OpenUserMusicFolder();
			}
		}))
	]);
	Add(SNew(SBox).Padding(0.f, 2.f)
	[
		MakeButton(LOCTEXT("Rescan", "Buscar música nueva"), FSimpleDelegate::CreateLambda([this]()
		{
			if (ULRMusicSubsystem* Player = Music.Get())
			{
				Player->RescanTracks();
				FolderNote = FText::Format(LOCTEXT("RescanResult", "{0} canciones en la lista"), FText::AsNumber(Player->GetNumTracks()));
			}
		}))
	]);
	Add(SNew(SBox).Padding(16.f, 2.f, 0.f, 0.f)
	[
		SNew(STextBlock).Text_Lambda([this]() { return FolderNote; }).Font(Font("Regular", 12)).ColorAndOpacity(FSlateColor(TextDim))
	]);

	Add(MakeHeader(LOCTEXT("HeaderGraphics", "GRÁFICOS")));
	Add(MakeRow(LOCTEXT("Quality", "Calidad"),
		TAttribute<FText>::CreateLambda([this]()
		{
			switch (ReadSettings().GraphicsQuality)
			{
			case 0: return LOCTEXT("QualityLow", "Baja");
			case 1: return LOCTEXT("QualityMedium", "Media");
			case 3: return LOCTEXT("QualityEpic", "Épica");
			default: return LOCTEXT("QualityHigh", "Alta");
			}
		}),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S) { S.GraphicsQuality = FMath::Clamp(S.GraphicsQuality + Direction, 0, 3); });
		}));
	Add(MakeRow(LOCTEXT("FrameLimit", "Límite de fotogramas"),
		TAttribute<FText>::CreateLambda([this]()
		{
			const int32 Limit = ReadSettings().FrameRateLimit;
			return Limit <= 0 ? LOCTEXT("NoLimit", "Sin límite") : FText::Format(LOCTEXT("FpsFormat", "{0} fps"), FText::AsNumber(Limit));
		}),
		[this](int32 Direction)
		{
			EditSettings([Direction](FLRUserSettings& S)
			{
				static const int32 Limits[] = { 30, 60, 90, 120, 144, 0 };
				int32 Index = 1;
				for (int32 Candidate = 0; Candidate < UE_ARRAY_COUNT(Limits); ++Candidate)
				{
					Index = Limits[Candidate] == S.FrameRateLimit ? Candidate : Index;
				}
				S.FrameRateLimit = Limits[FMath::Clamp(Index + Direction, 0, static_cast<int32>(UE_ARRAY_COUNT(Limits)) - 1)];
			});
		}));
	Add(MakeRow(LOCTEXT("Fullscreen", "Pantalla completa"),
		TAttribute<FText>::CreateLambda([this]() { return OnOff(ReadSettings().bFullscreen); }),
		[this](int32) { EditSettings([](FLRUserSettings& S) { S.bFullscreen = !S.bFullscreen; }); }));
	Add(MakeRow(LOCTEXT("MotionBlur", "Desenfoque de movimiento"),
		TAttribute<FText>::CreateLambda([this]() { return OnOff(ReadSettings().bMotionBlur); }),
		[this](int32) { EditSettings([](FLRUserSettings& S) { S.bMotionBlur = !S.bMotionBlur; }); }));

	ChildSlot
	[
		Box
	];
}

void SLRSettingsList::EditSettings(TFunctionRef<void(FLRUserSettings&)> Change) const
{
	if (ULRUserSettingsSubsystem* Subsystem = Settings.Get())
	{
		FLRUserSettings Edited = Subsystem->GetSettings();
		Change(Edited);
		Subsystem->SetSettings(Edited);
	}
}

FLRUserSettings SLRSettingsList::ReadSettings() const
{
	const ULRUserSettingsSubsystem* Subsystem = Settings.Get();
	return Subsystem ? Subsystem->GetSettings() : FLRUserSettings();
}

#undef LOCTEXT_NAMESPACE
