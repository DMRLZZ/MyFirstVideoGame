// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRHUD.h"

#include "LRMusicSubsystem.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LRDataLoader.h"
#include "LRGameEventSubsystem.h"
#include "LRHUDInputListener.h"
#include "LRRunContextSubsystem.h"
#include "LRUI.h"
#include "LRVehicle.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/SlateRenderer.h"

#define LOCTEXT_NAMESPACE "LRHUD"

namespace LRHUDPrivate
{
	/** Selected speed units, cached from lr.HUD.Units so the HUD does not copy a string every frame. Game thread only. */
	static ELRSpeedUnits GSpeedUnits = ELRSpeedUnits::Kmh;

	/** Console callback: parses the new value of lr.HUD.Units. */
	static void OnSpeedUnitsChanged(IConsoleVariable* Variable)
	{
		if (!Variable)
		{
			return;
		}
		const FString Value = Variable->GetString();
		if (!FLRHUDFormat::ParseSpeedUnits(Value, GSpeedUnits))
		{
			UE_LOG(LogLRUI, Warning, TEXT("lr.HUD.Units: unknown value '%s' (use kmh or mph)"), *Value);
		}
	}

	static FAutoConsoleVariable CVarHUDUnits(
		TEXT("lr.HUD.Units"),
		TEXT("kmh"),
		TEXT("Speed units shown by the HUD: kmh | mph."),
		FConsoleVariableDelegate::CreateStatic(&OnSpeedUnitsChanged),
		ECVF_Default);

	/** Reason of the score events that carry the points of a near miss. */
	static const FName NearMissReason(TEXT("NearMiss"));

	/** Scale that makes an offline (bitmap) font reach the size of a font info. */
	static float GetOfflineFontScale(const FSlateFontInfo& Info, const UFont* Font)
	{
		// Slate sizes are points at 96 DPI.
		const float MaxHeight = Font ? Font->GetMaxCharHeight() : 0.f;
		return MaxHeight > 0.f ? Info.Size * (96.f / 72.f) / MaxHeight : 1.f;
	}

	/** Slate font measuring service, or null when there is no renderer (e.g. -nullrhi). */
	static FSlateRenderer* GetSlateRenderer()
	{
		return FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
	}
}

// --- FLRHUDLabels ------------------------------------------------------------------------------

void FLRHUDLabels::Build()
{
	ScoreCaption = LOCTEXT("ScoreCaption", "PUNTOS");
	GearCaption = LOCTEXT("GearCaption", "MARCHA");
	SpeedTierCaption = LOCTEXT("SpeedTierCaption", "VELOCIDAD");
	ComboBroken = LOCTEXT("ComboBroken", "COMBO ROTO");

	TierLabels[static_cast<int32>(ELRNearMissTier::None)] = FText::GetEmpty();
	TierLabels[static_cast<int32>(ELRNearMissTier::Close)] = FLRHUDFormat::GetNearMissTierLabel(ELRNearMissTier::Close);
	TierLabels[static_cast<int32>(ELRNearMissTier::VeryClose)] = FLRHUDFormat::GetNearMissTierLabel(ELRNearMissTier::VeryClose);
	TierLabels[static_cast<int32>(ELRNearMissTier::Razor)] = FLRHUDFormat::GetNearMissTierLabel(ELRNearMissTier::Razor);
	TagOncoming = FLRHUDFormat::GetOncomingLabel();
	TagThreadTheNeedle = FLRHUDFormat::GetThreadTheNeedleLabel();
	SchemeArcade = FLRHUDFormat::GetControlSchemeLabel(ELRControlScheme::Arcade);
	SchemeSimcade = FLRHUDFormat::GetControlSchemeLabel(ELRControlScheme::Simcade);
	SchemeSmooth = FLRHUDFormat::GetControlSchemeLabel(ELRControlScheme::Smooth);

	CountdownGo = LOCTEXT("CountdownGo", "¡YA!");
	Crashed = LOCTEXT("Crashed", "¡CHOQUE!");

	ResultsTitle = LOCTEXT("ResultsTitle", "RESULTADOS");
	ResultsScore = LOCTEXT("ResultsScore", "Puntos");
	ResultsDistance = LOCTEXT("ResultsDistance", "Distancia");
	ResultsNearMisses = LOCTEXT("ResultsNearMisses", "Roces evitados");
	ResultsBestCombo = LOCTEXT("ResultsBestCombo", "Mejor combo");
	ResultsTopSpeed = LOCTEXT("ResultsTopSpeed", "Velocidad punta");
	ResultsDuration = LOCTEXT("ResultsDuration", "Tiempo");
	ResultsCredits = LOCTEXT("ResultsCredits", "Créditos");
	RestartKeyboard = LOCTEXT("RestartKeyboard", "Pulsa R para reiniciar");
	RestartGamepad = LOCTEXT("RestartGamepad", "Pulsa View / Share para reiniciar");

	GarageTitle = LOCTEXT("GarageTitle", "GARAJE");
	GarageKeyboard = LOCTEXT("GarageKeyboard", "Pulsa Enter para conducir");
	GarageGamepad = LOCTEXT("GarageGamepad", "Pulsa A / Cruz para conducir");
	Replay = LOCTEXT("Replay", "REPETICIÓN");

	SchemeCaption = LOCTEXT("SchemeCaption", "CONTROL");
	ControlsTitle = LOCTEXT("ControlsTitle", "CONTROLES");
	KeyboardHeader = LOCTEXT("KeyboardHeader", "Teclado");
	GamepadHeader = LOCTEXT("GamepadHeader", "Mando");

	Loading[0] = LOCTEXT("Loading0", "CARGANDO");
	Loading[1] = LOCTEXT("Loading1", "CARGANDO.");
	Loading[2] = LOCTEXT("Loading2", "CARGANDO..");
	Loading[3] = LOCTEXT("Loading3", "CARGANDO...");

	// Prompts match the mappings of ULRInputActions (see Source/LRInput/README.md).
	const FHintRow Throttle{ LOCTEXT("HintThrottle", "Acelerar"), LOCTEXT("HintThrottleKey", "W"), LOCTEXT("HintThrottlePad", "RT") };
	const FHintRow Brake{ LOCTEXT("HintBrake", "Frenar"), LOCTEXT("HintBrakeKey", "S"), LOCTEXT("HintBrakePad", "LT") };
	const FHintRow Dodge{ LOCTEXT("HintDodge", "Moverse"), LOCTEXT("HintDodgeKey", "A / D"), LOCTEXT("HintDodgePad", "Stick izq.") };
	const FHintRow LaneChange{ LOCTEXT("HintLane", "Cambiar de carril"), LOCTEXT("HintLaneKey", "A / D o Q / E"), LOCTEXT("HintLanePad", "Stick izq. / cruceta") };
	const FHintRow Steering{ LOCTEXT("HintSteer", "Dirección"), LOCTEXT("HintSteerKey", "A / D"), LOCTEXT("HintSteerPad", "Stick izq.") };
	const FHintRow Handbrake{ LOCTEXT("HintHandbrake", "Freno de mano"), LOCTEXT("HintHandbrakeKey", "Espacio"), LOCTEXT("HintHandbrakePad", "B / Círculo") };
	const FHintRow Camera{ LOCTEXT("HintCamera", "Cámara"), LOCTEXT("HintCameraKey", "C"), LOCTEXT("HintCameraPad", "Y / Triángulo") };
	const FHintRow LookBack{ LOCTEXT("HintLookBack", "Mirar atrás"), LOCTEXT("HintLookBackKey", "B"), LOCTEXT("HintLookBackPad", "RB") };
	const FHintRow Scheme{ LOCTEXT("HintScheme", "Cambiar esquema"), LOCTEXT("HintSchemeKey", "T"), LOCTEXT("HintSchemePad", "LB") };
	const FHintRow Restart{ LOCTEXT("HintRestart", "Reiniciar"), LOCTEXT("HintRestartKey", "R"), LOCTEXT("HintRestartPad", "View / Share") };

	const FHintRow Menu{ LOCTEXT("HintMenu", "Menú y ajustes"), LOCTEXT("HintMenuKey", "Esc"), LOCTEXT("HintMenuPad", "Menu / Options") };
	const FHintRow NextSong{ LOCTEXT("HintNextSong", "Siguiente canción"), LOCTEXT("HintNextSongKey", "M"), LOCTEXT("HintNextSongPad", "Cruceta abajo") };
	const FHintRow NextCar{ LOCTEXT("HintNextCar", "Cambiar de coche"), LOCTEXT("HintNextCarKey", "V"), LOCTEXT("HintNextCarPad", "Cruceta arriba") };
	SmoothHints = { Throttle, Brake, Dodge, Handbrake, Camera, LookBack, NextCar, NextSong, Restart, Menu };
	ArcadeHints = { Throttle, Brake, LaneChange, Handbrake, Camera, LookBack, Scheme, NextCar, NextSong, Restart, Menu };
	SimcadeHints = { Throttle, Brake, Steering, Handbrake, Camera, LookBack, Scheme, NextCar, NextSong, Restart, Menu };
}

// --- ALRHUD: setup -------------------------------------------------------------------------------

ALRHUD::ALRHUD()
{
	// Everything happens in DrawHUD, which keeps running while the game is paused.
	PrimaryActorTick.bCanEverTick = false;
}

ELRSpeedUnits ALRHUD::GetSpeedUnits()
{
	return LRHUDPrivate::GSpeedUnits;
}

void ALRHUD::SetSpeedUnits(ELRSpeedUnits Units)
{
	// Console priority so the value always wins over what the console or an ini set before.
	LRHUDPrivate::CVarHUDUnits->Set(Units == ELRSpeedUnits::Mph ? TEXT("mph") : TEXT("kmh"), ECVF_SetByConsole);
	LRHUDPrivate::GSpeedUnits = Units;
}

void ALRHUD::SetTuning(const FLRHUDTuning& InTuning)
{
	Tuning = InTuning.Sanitized();
	ApplyTuning();
}

bool ALRHUD::IsUsingGamepadPrompts() const
{
	return InputListener.IsValid() && InputListener->GetTracker().IsUsingGamepad();
}

void ALRHUD::BeginPlay()
{
	Super::BeginPlay();

	Labels.Build();
	CacheEngineFonts();
	LoadData();
	ApplyTuning();
	ResetRunPresentation();

	RunContext = ULRRunContextSubsystem::Get(this);
	BindEvents();
	RegisterInputListener();

	// The HUD can spawn after gameplay already moved the run forward.
	if (const ULRRunContextSubsystem* Context = RunContext.Get())
	{
		const ELRRunState State = Context->GetRunState();
		EnterState(State, State);
	}
}

void ALRHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindEvents();
	UnregisterInputListener();

	Super::EndPlay(EndPlayReason);
}

void ALRHUD::LoadData()
{
	FString Error;
	FLRHUDTuning LoadedTuning;
	if (FLRDataLoader::Load(FLRHUDTuning::DataPath, LoadedTuning, &Error))
	{
		Tuning = LoadedTuning.Sanitized();
	}
	else
	{
		UE_LOG(LogLRUI, Warning, TEXT("HUD tuning not loaded, using defaults: %s"), *Error);
		Tuning = FLRHUDTuning().Sanitized();
	}

	FLRHUDGameRules LoadedRules;
	if (FLRDataLoader::Load(FLRHUDGameRules::DataPath, LoadedRules, &Error))
	{
		GameRules = LoadedRules;
	}
	else
	{
		UE_LOG(LogLRUI, Log, TEXT("Game rules not loaded, the countdown lasts %.1f s: %s"), GameRules.CountdownSeconds, *Error);
	}
	GameRules.CountdownSeconds = FMath::Clamp(GameRules.CountdownSeconds, 0.f, 10.f);
}

void ALRHUD::ApplyTuning()
{
	Popups.Configure(Tuning.MaxPopups, Tuning.PopupLifetimeSeconds, Tuning.PointsAttachWindowSeconds);
	Combo.BrokenDisplaySeconds = Tuning.ComboBrokenDisplaySeconds;

	PopupPointsTexts.Reset();
	PopupPointsTexts.SetNum(Popups.GetSlots().Num());
	PopupPointsValues.Reset();
	PopupPointsValues.Init(INDEX_NONE, Popups.GetSlots().Num());

	FontsBuiltForScale = -1.f;
	InvalidateTextCaches();
	if (bHasResult)
	{
		RebuildResultTexts();
	}
}

void ALRHUD::CacheEngineFonts()
{
	HUDFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	FallbackFont = GEngine ? GEngine->GetMediumFont() : nullptr;

	// Runtime cached fonts stay crisp at any size, so they are preferred over a bitmap font.
	const bool bHUDFontRuntime = HUDFont && HUDFont->FontCacheType == EFontCacheType::Runtime;
	if (!HUDFont || (!bHUDFontRuntime && FallbackFont && FallbackFont->FontCacheType == EFontCacheType::Runtime))
	{
		HUDFont = FallbackFont;
	}
	bRuntimeFont = HUDFont && HUDFont->FontCacheType == EFontCacheType::Runtime;
	FontsBuiltForScale = -1.f;

	if (!HUDFont)
	{
		UE_LOG(LogLRUI, Warning, TEXT("No engine font available: the HUD will not draw text"));
	}
}

void ALRHUD::BindEvents()
{
	ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this);
	EventBus = Events;
	if (!Events)
	{
		return;
	}

	NearMissHandle = Events->OnNearMiss.AddUObject(this, &ALRHUD::HandleNearMiss);
	ScoreHandle = Events->OnScoreChanged.AddUObject(this, &ALRHUD::HandleScoreChanged);
	ComboHandle = Events->OnComboChanged.AddUObject(this, &ALRHUD::HandleComboChanged);
	SpeedTierHandle = Events->OnSpeedTierChanged.AddUObject(this, &ALRHUD::HandleSpeedTierChanged);
	RunStateHandle = Events->OnRunStateChanged.AddUObject(this, &ALRHUD::HandleRunStateChanged);
	RunResetHandle = Events->OnRunReset.AddUObject(this, &ALRHUD::HandleRunReset);
	RunFinishedHandle = Events->OnRunFinished.AddUObject(this, &ALRHUD::HandleRunFinished);
}

void ALRHUD::UnbindEvents()
{
	if (ULRGameEventSubsystem* Events = EventBus.Get())
	{
		Events->OnNearMiss.Remove(NearMissHandle);
		Events->OnScoreChanged.Remove(ScoreHandle);
		Events->OnComboChanged.Remove(ComboHandle);
		Events->OnSpeedTierChanged.Remove(SpeedTierHandle);
		Events->OnRunStateChanged.Remove(RunStateHandle);
		Events->OnRunReset.Remove(RunResetHandle);
		Events->OnRunFinished.Remove(RunFinishedHandle);
	}
	EventBus.Reset();
}

void ALRHUD::RegisterInputListener()
{
	if (FSlateApplication::IsInitialized() && !InputListener.IsValid())
	{
		InputListener = MakeShared<FLRHUDInputListener>();
		FSlateApplication::Get().RegisterInputPreProcessor(InputListener);
	}
}

void ALRHUD::UnregisterInputListener()
{
	if (InputListener.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputListener);
	}
	InputListener.Reset();
}

void ALRHUD::ResetRunPresentation()
{
	Score = 0;
	DisplayedScore = 0.0;
	SpeedTierMultiplier = 1.f;
	Combo.Reset();
	Popups.Reset();
	bHasResult = false;
	LastResult = FLRRunResult();

	RebuildSpeedTierText();
	RebuildComboTexts();
}

// --- ALRHUD: events ------------------------------------------------------------------------------

void ALRHUD::HandleNearMiss(const FLRNearMissEvent& Event)
{
	Popups.Push(Event);
}

void ALRHUD::HandleScoreChanged(const FLRScoreChangedEvent& Event)
{
	Score = Event.Score;
	if (Event.Reason == LRHUDPrivate::NearMissReason)
	{
		Popups.ReportPoints(Event.Delta);
	}
}

void ALRHUD::HandleComboChanged(const FLRComboChangedEvent& Event)
{
	Combo.Apply(Event);
	RebuildComboTexts();
}

void ALRHUD::HandleSpeedTierChanged(const FLRSpeedTierChangedEvent& Event)
{
	SpeedTierMultiplier = Event.Multiplier;
	RebuildSpeedTierText();
}

void ALRHUD::HandleRunStateChanged(const FLRRunStateChangedEvent& Event)
{
	EnterState(Event.Current, Event.Previous);
}

void ALRHUD::HandleRunReset(const FLRRunResetEvent& Event)
{
	ResetRunPresentation();
}

void ALRHUD::HandleRunFinished(const FLRRunResult& Result)
{
	LastResult = Result;
	bHasResult = true;
	Score = Result.FinalScore;
	RebuildResultTexts();
}

void ALRHUD::EnterState(ELRRunState NewState, ELRRunState OldState)
{
	if (const UWorld* World = GetWorld())
	{
		NowSeconds = World->GetRealTimeSeconds();
	}

	PreviousState = OldState;
	CurrentState = NewState;
	StateEnterSeconds = NowSeconds;

	if (NewState == ELRRunState::Running)
	{
		if (RunningEntries == 0)
		{
			FirstRunningSeconds = NowSeconds;
		}
		++RunningEntries;
	}
	else if (NewState == ELRRunState::Countdown)
	{
		CachedCountdownNumber = INDEX_NONE;
	}
}

// --- ALRHUD: per-frame update --------------------------------------------------------------------

void ALRHUD::DrawHUD()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALRHUD::DrawHUD);

	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	UpdateTime();
	UpdateLayout();

	const ALRVehicle* Vehicle = FindVehicle();
	RefreshCachedTexts(Vehicle);

	switch (CurrentState)
	{
	case ELRRunState::Garage:
		DrawGarage(Vehicle);
		break;

	case ELRRunState::Loading:
		DrawLoading();
		break;

	case ELRRunState::Countdown:
		if (Vehicle)
		{
			DrawSpeedometer(*Vehicle);
		}
		DrawScoreAndCombo();
		DrawControlScheme();
		DrawControlsHint();
		DrawCountdownOverlay();
		break;

	case ELRRunState::Running:
		if (Vehicle)
		{
			DrawSpeedometer(*Vehicle);
		}
		DrawScoreAndCombo();
		DrawNearMissPopups();
		DrawControlScheme();
		DrawControlsHint();
		DrawGoBanner();
		break;

	case ELRRunState::Crashed:
		if (Vehicle)
		{
			DrawSpeedometer(*Vehicle);
		}
		DrawScoreAndCombo();
		DrawNearMissPopups();
		DrawCrashOverlay();
		break;

	case ELRRunState::Replay:
		DrawReplayTag();
		break;

	case ELRRunState::Results:
		DrawResultsPanel();
		break;

	default:
		break;
	}
	DrawNowPlaying();
}

void ALRHUD::UpdateTime()
{
	const UWorld* World = GetWorld();
	NowSeconds = World ? World->GetRealTimeSeconds() : NowSeconds;

	// A long hitch must not make every animation jump to its end.
	FrameDelta = LastFrameSeconds >= 0.0 ? FMath::Clamp(static_cast<float>(NowSeconds - LastFrameSeconds), 0.f, 0.25f) : 0.f;
	LastFrameSeconds = NowSeconds;

	if (World && World->IsPaused())
	{
		return;
	}

	Popups.Tick(FrameDelta);
	Combo.Tick(FrameDelta);
	DisplayedScore = FLRHUDAnim::ApproachScore(DisplayedScore, Score, Tuning.ScoreRollRate, FrameDelta);
}

void ALRHUD::UpdateLayout()
{
	CanvasWidth = Canvas->ClipX;
	CanvasHeight = Canvas->ClipY;
	UIScale = FLRHUDAnim::ComputeUIScale(CanvasHeight, Tuning.ReferenceHeight, Tuning.MinUIScale, Tuning.MaxUIScale);
	Margin = CanvasHeight * Tuning.SafeMarginFraction;

	if (!FMath::IsNearlyEqual(FontsBuiltForScale, UIScale, 0.001f))
	{
		RebuildFonts();
	}
}

void ALRHUD::RebuildFonts()
{
	FontsBuiltForScale = UIScale;

	const auto MakeFont = [this](float ReferenceSize, const FName& Typeface)
	{
		// Whole sizes so close resolutions share glyphs in the font cache.
		return FSlateFontInfo(HUDFont.Get(), FMath::Max(FMath::RoundToFloat(ReferenceSize * UIScale), 1.f), Typeface);
	};

	Fonts.Speed = MakeFont(Tuning.SpeedFontSize, Tuning.BoldTypeface);
	Fonts.Score = MakeFont(Tuning.ScoreFontSize, Tuning.BoldTypeface);
	Fonts.Banner = MakeFont(Tuning.BannerFontSize, Tuning.BoldTypeface);
	Fonts.LargeBold = MakeFont(Tuning.LargeFontSize, Tuning.BoldTypeface);
	Fonts.MediumBold = MakeFont(Tuning.MediumFontSize, Tuning.BoldTypeface);
	Fonts.Medium = MakeFont(Tuning.MediumFontSize, Tuning.RegularTypeface);
	Fonts.SmallBold = MakeFont(Tuning.SmallFontSize, Tuning.BoldTypeface);
	Fonts.Small = MakeFont(Tuning.SmallFontSize, Tuning.RegularTypeface);
}

void ALRHUD::InvalidateTextCaches()
{
	CachedScoreValue = MIN_int64;
	CachedSpeedValue = INDEX_NONE;
	CachedUnitText = FText::GetEmpty();
	CachedGear = MIN_int32;
	CachedCountdownNumber = INDEX_NONE;
	CachedComboMultiplier = -1.f;
	CachedComboChain = INDEX_NONE;
	CachedVehicleId = NAME_None;
	CachedVehicleName = FText::GetEmpty();

	for (int64& Points : PopupPointsValues)
	{
		Points = INDEX_NONE;
	}

	RebuildSpeedTierText();
	RebuildComboTexts();
}

void ALRHUD::RebuildSpeedTierText()
{
	CachedTierText = FLRHUDFormat::FormatMultiplier(SpeedTierMultiplier, Tuning.GetDecimalSeparatorChar());
}

void ALRHUD::RebuildComboTexts()
{
	const float Multiplier = Combo.GetMultiplier();
	const int32 Chain = Combo.GetChain();
	if (Multiplier != CachedComboMultiplier)
	{
		CachedComboMultiplier = Multiplier;
		CachedComboText = FText::Format(LOCTEXT("ComboFmt", "COMBO {0}"), FLRHUDFormat::FormatMultiplier(Multiplier, Tuning.GetDecimalSeparatorChar()));
	}
	if (Chain != CachedComboChain)
	{
		CachedComboChain = Chain;
		CachedChainText = FText::Format(LOCTEXT("ComboChainFmt", "{0} seguidos"), FText::AsCultureInvariant(LexToString(Chain)));
	}
}

void ALRHUD::RebuildResultTexts()
{
	ResultUnits = GetSpeedUnits();
	const TCHAR Decimal = Tuning.GetDecimalSeparatorChar();

	ResultValues[0] = FLRHUDFormat::FormatScore(LastResult.FinalScore, Tuning.ThousandsSeparator);
	ResultValues[1] = FLRHUDFormat::FormatDistanceKm(LastResult.DistanceMeters, 1, Decimal);
	ResultValues[2] = FLRHUDFormat::FormatScore(LastResult.NearMisses, Tuning.ThousandsSeparator);
	ResultValues[3] = FLRHUDFormat::FormatScore(LastResult.BestCombo, Tuning.ThousandsSeparator);
	ResultValues[4] = FText::Format(LOCTEXT("TopSpeedFmt", "{0} {1}"), FLRHUDFormat::FormatSpeed(LastResult.TopSpeedKmh, ResultUnits), FLRHUDFormat::GetSpeedUnitLabel(ResultUnits));
	ResultValues[5] = FLRHUDFormat::FormatDuration(LastResult.DurationSeconds);
	ResultValues[6] = FLRHUDFormat::FormatPoints(LastResult.CreditsEarned, Tuning.ThousandsSeparator);
}

void ALRHUD::RefreshCachedTexts(const ALRVehicle* Vehicle)
{
	const int64 ShownScore = static_cast<int64>(FMath::RoundToDouble(DisplayedScore));
	if (ShownScore != CachedScoreValue)
	{
		CachedScoreValue = ShownScore;
		CachedScoreText = FLRHUDFormat::FormatScore(ShownScore, Tuning.ThousandsSeparator);
	}

	const ELRSpeedUnits Units = GetSpeedUnits();
	if (Units != CachedSpeedUnits || CachedUnitText.IsEmpty())
	{
		CachedSpeedUnits = Units;
		CachedUnitText = FLRHUDFormat::GetSpeedUnitLabel(Units);
		CachedSpeedValue = INDEX_NONE;
	}

	if (Vehicle)
	{
		const float SpeedKmh = Vehicle->GetSpeedKmh();
		const int32 ShownSpeed = FLRHUDFormat::RoundSpeedForDisplay(SpeedKmh, Units);
		if (ShownSpeed != CachedSpeedValue)
		{
			CachedSpeedValue = ShownSpeed;
			CachedSpeedText = FLRHUDFormat::FormatSpeed(SpeedKmh, Units);
		}

		const int32 Gear = FLRHUDFormat::ChooseDisplayedGear(Vehicle->GetCurrentGear(), CachedGear, SpeedKmh);
		if (Gear != CachedGear)
		{
			CachedGear = Gear;
			CachedGearText = FLRHUDFormat::FormatGear(Gear);
		}
	}

	const TArray<FLRHUDNearMissPopup>& Slots = Popups.GetSlots();
	for (int32 Index = 0; Index < Slots.Num() && Index < PopupPointsValues.Num(); ++Index)
	{
		const FLRHUDNearMissPopup& Slot = Slots[Index];
		if (Slot.bActive && Slot.HasPoints() && Slot.Points != PopupPointsValues[Index])
		{
			PopupPointsValues[Index] = Slot.Points;
			PopupPointsTexts[Index] = FLRHUDFormat::FormatPoints(Slot.Points, Tuning.ThousandsSeparator);
		}
	}

	if (bHasResult && ResultUnits != Units)
	{
		RebuildResultTexts();
	}
}

ALRVehicle* ALRHUD::FindVehicle() const
{
	if (const ULRRunContextSubsystem* Context = RunContext.Get())
	{
		if (ALRVehicle* Vehicle = Cast<ALRVehicle>(Context->GetPlayerPawn()))
		{
			return Vehicle;
		}
	}
	return PlayerOwner ? Cast<ALRVehicle>(PlayerOwner->GetPawn()) : nullptr;
}

float ALRHUD::GetTimeInState() const
{
	return static_cast<float>(NowSeconds - StateEnterSeconds);
}

// --- ALRHUD: drawing -----------------------------------------------------------------------------

void ALRHUD::DrawSpeedometer(const ALRVehicle& Vehicle)
{
	const float Width = Px(380.f);
	const float Height = Px(178.f);
	const float Padding = Px(16.f);
	const float X = CanvasWidth - Margin - Width;
	const float Y = CanvasHeight - Margin - Height;

	DrawBox(FVector2D(X, Y), FVector2D(Width, Height), Tuning.PanelColor);

	// Large speed number with the unit next to its lower right corner.
	const float SpeedRight = X + Px(232.f);
	const float SpeedTop = Y + Px(2.f);
	DrawTextAt(CachedSpeedText, Fonts.Speed, FVector2D(SpeedRight, SpeedTop), Tuning.PrimaryColor, EAlign::Right);
	const float UnitY = SpeedTop + GetLineHeight(Fonts.Speed) - GetLineHeight(Fonts.SmallBold) - Px(14.f);
	DrawTextAt(CachedUnitText, Fonts.SmallBold, FVector2D(SpeedRight + Px(8.f), UnitY), Tuning.SecondaryColor);

	// Gear.
	const float GearX = X + Width - Px(52.f);
	DrawTextAt(Labels.GearCaption, Fonts.Small, FVector2D(GearX, Y + Px(10.f)), Tuning.SecondaryColor, EAlign::Center);
	DrawTextAt(CachedGearText, Fonts.LargeBold, FVector2D(GearX, Y + Px(32.f)), Tuning.PrimaryColor, EAlign::Center);

	// RPM bar: track, red zone, fill and a mark on the redline.
	const float MaxRpm = Vehicle.GetEngineMaxRpm();
	const float RpmFraction = MaxRpm > 0.f ? FMath::Clamp(Vehicle.GetEngineRpm() / MaxRpm, 0.f, 1.f) : 0.f;
	const float BarX = X + Padding;
	const float BarY = Y + Height - Px(62.f);
	const float BarWidth = Width - Padding * 2.f;
	const float BarHeight = Px(16.f);
	const float RedlineWidth = BarWidth * Tuning.RedlineFraction;

	DrawBox(FVector2D(BarX, BarY), FVector2D(RedlineWidth, BarHeight), FLinearColor(1.f, 1.f, 1.f, 0.12f));
	DrawBox(FVector2D(BarX + RedlineWidth, BarY), FVector2D(BarWidth - RedlineWidth, BarHeight), WithAlpha(Tuning.DangerColor, 0.35f));

	const float FillWidth = BarWidth * RpmFraction;
	DrawBox(FVector2D(BarX, BarY), FVector2D(FMath::Min(FillWidth, RedlineWidth), BarHeight), Tuning.RpmColor);
	if (FillWidth > RedlineWidth)
	{
		DrawBox(FVector2D(BarX + RedlineWidth, BarY), FVector2D(FillWidth - RedlineWidth, BarHeight), Tuning.DangerColor);
	}
	DrawBox(FVector2D(BarX + RedlineWidth - Px(1.f), BarY - Px(3.f)), FVector2D(FMath::Max(Px(2.f), 1.f), BarHeight + Px(6.f)), Tuning.PrimaryColor);

	// Multiplier of the current speed tier.
	const float TierY = Y + Height - Px(38.f);
	DrawTextAt(Labels.SpeedTierCaption, Fonts.Small, FVector2D(BarX, TierY), Tuning.SecondaryColor);
	DrawTextAt(CachedTierText, Fonts.MediumBold, FVector2D(BarX + BarWidth, TierY - Px(5.f)), Tuning.AccentColor, EAlign::Right);
}

void ALRHUD::DrawScoreAndCombo()
{
	const float CenterX = CanvasWidth * 0.5f;
	const float CaptionHeight = GetLineHeight(Fonts.SmallBold);
	const float ScoreHeight = GetLineHeight(Fonts.Score);

	// Score panel, wide enough for the number.
	const float PanelWidth = FMath::Max(Px(260.f), static_cast<float>(MeasureText(CachedScoreText, Fonts.Score).X) + Px(48.f));
	const float PanelHeight = CaptionHeight + ScoreHeight + Px(12.f);
	DrawBox(FVector2D(CenterX - PanelWidth * 0.5f, Margin), FVector2D(PanelWidth, PanelHeight), Tuning.PanelColor);

	float Y = Margin + Px(6.f);
	DrawTextAt(Labels.ScoreCaption, Fonts.SmallBold, FVector2D(CenterX, Y), Tuning.SecondaryColor, EAlign::Center);
	Y += CaptionHeight;
	DrawTextAt(CachedScoreText, Fonts.Score, FVector2D(CenterX, Y), Tuning.PrimaryColor, EAlign::Center);
	Y += ScoreHeight + Px(12.f);

	if (Combo.IsActive())
	{
		DrawTextAt(CachedComboText, Fonts.LargeBold, FVector2D(CenterX, Y), Tuning.AccentColor, EAlign::Center);
		Y += GetLineHeight(Fonts.LargeBold) + Px(2.f);

		// Remaining time, red in the last quarter.
		const float Fraction = Combo.GetRemainingFraction();
		const float BarWidth = Px(280.f);
		const float BarHeight = Px(8.f);
		const float BarX = CenterX - BarWidth * 0.5f;
		DrawBox(FVector2D(BarX, Y), FVector2D(BarWidth, BarHeight), FLinearColor(1.f, 1.f, 1.f, 0.15f));
		DrawBox(FVector2D(BarX, Y), FVector2D(BarWidth * Fraction, BarHeight), Fraction < 0.25f ? Tuning.DangerColor : Tuning.AccentColor);
		Y += BarHeight + Px(4.f);

		DrawTextAt(CachedChainText, Fonts.Small, FVector2D(CenterX, Y), Tuning.SecondaryColor, EAlign::Center);
	}
	else if (Combo.IsShowingBroken())
	{
		const float Alpha = Tuning.ComboBrokenDisplaySeconds > 0.f ? FMath::Clamp(Combo.GetBrokenTimer() / Tuning.ComboBrokenDisplaySeconds, 0.f, 1.f) : 0.f;
		DrawTextAt(Labels.ComboBroken, Fonts.LargeBold, FVector2D(CenterX, Y), WithAlpha(Tuning.DangerColor, Alpha), EAlign::Center);
	}
}

void ALRHUD::DrawNearMissPopups()
{
	const TArray<FLRHUDNearMissPopup>& Slots = Popups.GetSlots();
	const float Lifetime = Popups.GetLifetimeSeconds();
	const float CenterX = CanvasWidth * 0.5f;
	const float AnchorY = CanvasHeight * 0.34f;
	const float RowHeight = GetLineHeight(Fonts.LargeBold) + Px(4.f);
	const float Gap = Px(14.f);

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FLRHUDNearMissPopup& Popup = Slots[Index];
		if (!Popup.bActive)
		{
			continue;
		}

		const float Alpha = FLRHUDAnim::FadeInOut(Popup.AgeSeconds, Lifetime, Tuning.PopupFadeInSeconds, Tuning.PopupFadeOutSeconds);
		if (Alpha <= 0.f)
		{
			continue;
		}

		// The row slides in from the right during the first quarter of its life and keeps rising until it fades.
		const float LifeFraction = Lifetime > 0.f ? Popup.AgeSeconds / Lifetime : 1.f;
		const float SlideOffset = (1.f - FLRHUDAnim::EaseOutCubic(LifeFraction * 4.f)) * Px(Tuning.PopupSlidePixels);
		const float RiseOffset = FLRHUDAnim::EaseOutCubic(LifeFraction) * Px(Tuning.PopupRisePixels);
		const float Y = AnchorY - RiseOffset - RowHeight * Popups.GetStackRank(Index);

		// Up to four parts in one centered row; sizes are served by the font measure cache.
		const FText* Parts[4];
		const FSlateFontInfo* PartFonts[4];
		FLinearColor PartColors[4];
		int32 NumParts = 0;

		const int32 TierIndex = static_cast<int32>(Popup.Tier);
		if (TierIndex > 0 && TierIndex < static_cast<int32>(UE_ARRAY_COUNT(Labels.TierLabels)))
		{
			Parts[NumParts] = &Labels.TierLabels[TierIndex];
			PartFonts[NumParts] = &Fonts.LargeBold;
			PartColors[NumParts++] = GetTierColor(Popup.Tier);
		}
		if (Popup.bOncoming)
		{
			Parts[NumParts] = &Labels.TagOncoming;
			PartFonts[NumParts] = &Fonts.MediumBold;
			PartColors[NumParts++] = Tuning.OncomingColor;
		}
		if (Popup.bThreadTheNeedle)
		{
			Parts[NumParts] = &Labels.TagThreadTheNeedle;
			PartFonts[NumParts] = &Fonts.MediumBold;
			PartColors[NumParts++] = Tuning.ThreadColor;
		}
		if (Popup.HasPoints() && PopupPointsTexts.IsValidIndex(Index))
		{
			Parts[NumParts] = &PopupPointsTexts[Index];
			PartFonts[NumParts] = &Fonts.LargeBold;
			PartColors[NumParts++] = Tuning.PrimaryColor;
		}
		if (NumParts == 0)
		{
			continue;
		}

		float PartWidths[4];
		float TotalWidth = Gap * (NumParts - 1);
		for (int32 Part = 0; Part < NumParts; ++Part)
		{
			PartWidths[Part] = static_cast<float>(MeasureText(*Parts[Part], *PartFonts[Part]).X);
			TotalWidth += PartWidths[Part];
		}

		float X = CenterX - TotalWidth * 0.5f + SlideOffset;
		const float LargeHeight = GetLineHeight(Fonts.LargeBold);
		for (int32 Part = 0; Part < NumParts; ++Part)
		{
			// Smaller tags share the vertical center of the row.
			const float PartY = Y + (LargeHeight - GetLineHeight(*PartFonts[Part])) * 0.5f;
			DrawTextAt(*Parts[Part], *PartFonts[Part], FVector2D(X, PartY), WithAlpha(PartColors[Part], Alpha));
			X += PartWidths[Part] + Gap;
		}
	}
}

void ALRHUD::DrawCountdownOverlay()
{
	const float CenterX = CanvasWidth * 0.5f;
	const float BannerY = CanvasHeight * 0.32f;
	const float Remaining = GameRules.CountdownSeconds - GetTimeInState();
	const int32 Number = FLRHUDAnim::GetCountdownNumber(Remaining);

	if (Number <= 0)
	{
		// Gameplay has not switched to Running yet.
		DrawTextAt(Labels.CountdownGo, Fonts.Banner, FVector2D(CenterX, BannerY), Tuning.AccentColor, EAlign::Center);
		return;
	}

	if (Number != CachedCountdownNumber)
	{
		CachedCountdownNumber = Number;
		CachedCountdownText = FText::AsCultureInvariant(LexToString(Number));
	}

	// Each number pops in and fades during its second.
	const float Phase = FLRHUDAnim::GetCountdownPhase(Remaining);
	const float Scale = FMath::Lerp(1.3f, 1.f, FLRHUDAnim::EaseOutCubic(Phase * 4.f));
	const float Alpha = FLRHUDAnim::FadeInOut(Phase, 1.f, 0.05f, 0.25f);
	DrawTextAt(CachedCountdownText, Fonts.Banner, FVector2D(CenterX, BannerY), WithAlpha(Tuning.PrimaryColor, Alpha), EAlign::Center, Scale);
}

void ALRHUD::DrawGoBanner()
{
	const float Time = GetTimeInState();
	if (PreviousState != ELRRunState::Countdown || Time >= Tuning.GoDisplaySeconds)
	{
		return;
	}

	const float Alpha = FLRHUDAnim::FadeInOut(Time, Tuning.GoDisplaySeconds, 0.f, Tuning.GoDisplaySeconds * 0.6f);
	const float Scale = FMath::Lerp(1.f, 1.25f, FLRHUDAnim::EaseOutCubic(Time / Tuning.GoDisplaySeconds));
	DrawTextAt(Labels.CountdownGo, Fonts.Banner, FVector2D(CanvasWidth * 0.5f, CanvasHeight * 0.32f), WithAlpha(Tuning.AccentColor, Alpha), EAlign::Center, Scale);
}

void ALRHUD::DrawCrashOverlay()
{
	const float Time = GetTimeInState();

	// Red flash on impact, then the banner stays until gameplay leaves the state.
	const float Flash = FMath::Clamp(1.f - Time / 0.35f, 0.f, 1.f);
	if (Flash > 0.f)
	{
		DrawBox(FVector2D::ZeroVector, FVector2D(CanvasWidth, CanvasHeight), WithAlpha(Tuning.DangerColor, 0.35f * Flash));
	}

	const float Scale = FMath::Lerp(1.4f, 1.f, FLRHUDAnim::EaseOutCubic(Time / 0.25f));
	const float Alpha = FMath::Clamp(Time / 0.08f, 0.f, 1.f);
	DrawTextAt(Labels.Crashed, Fonts.Banner, FVector2D(CanvasWidth * 0.5f, CanvasHeight * 0.32f), WithAlpha(Tuning.DangerColor, Alpha), EAlign::Center, Scale);
}

void ALRHUD::DrawResultsPanel()
{
	const float Time = GetTimeInState();
	const float Appear = FLRHUDAnim::EaseOutCubic(Time / 0.35f);

	DrawBox(FVector2D::ZeroVector, FVector2D(CanvasWidth, CanvasHeight), WithAlpha(Tuning.DimColor, Appear));

	const FText* Captions[UE_ARRAY_COUNT(ResultValues)] = {
		&Labels.ResultsScore,
		&Labels.ResultsDistance,
		&Labels.ResultsNearMisses,
		&Labels.ResultsBestCombo,
		&Labels.ResultsTopSpeed,
		&Labels.ResultsDuration,
		&Labels.ResultsCredits
	};
	const int32 NumRows = bHasResult ? static_cast<int32>(UE_ARRAY_COUNT(ResultValues)) : 0;

	const float TitleHeight = GetLineHeight(Fonts.LargeBold);
	const float RowHeight = GetLineHeight(Fonts.Medium) + Px(6.f);
	const float PromptHeight = GetLineHeight(Fonts.MediumBold);
	const float Padding = Px(28.f);

	const float Width = Px(620.f);
	const float Height = Padding * 2.f + TitleHeight + Px(16.f) + RowHeight * NumRows + Px(18.f) + PromptHeight;
	const float X = (CanvasWidth - Width) * 0.5f;
	const float Y = (CanvasHeight - Height) * 0.5f + (1.f - Appear) * Px(30.f);

	DrawBox(FVector2D(X, Y), FVector2D(Width, Height), WithAlpha(Tuning.PanelColor, Appear));

	float RowY = Y + Padding;
	DrawTextAt(Labels.ResultsTitle, Fonts.LargeBold, FVector2D(CanvasWidth * 0.5f, RowY), WithAlpha(Tuning.AccentColor, Appear), EAlign::Center);
	RowY += TitleHeight + Px(16.f);

	const FLinearColor CaptionColor = WithAlpha(Tuning.SecondaryColor, Appear);
	const FLinearColor ValueColor = WithAlpha(Tuning.PrimaryColor, Appear);
	const FLinearColor HighlightColor = WithAlpha(Tuning.AccentColor, Appear);
	for (int32 Row = 0; Row < NumRows; ++Row)
	{
		const bool bHighlight = Row == 0 || Row == NumRows - 1; // Score and credits.
		DrawTextAt(*Captions[Row], Fonts.Medium, FVector2D(X + Padding, RowY), CaptionColor);
		DrawTextAt(ResultValues[Row], Fonts.MediumBold, FVector2D(X + Width - Padding, RowY), bHighlight ? HighlightColor : ValueColor, EAlign::Right);
		RowY += RowHeight;
	}
	RowY += Px(18.f);

	const FText& Prompt = IsUsingGamepadPrompts() ? Labels.RestartGamepad : Labels.RestartKeyboard;
	DrawTextAt(Prompt, Fonts.MediumBold, FVector2D(CanvasWidth * 0.5f, RowY), ValueColor, EAlign::Center);
}

void ALRHUD::DrawGarage(const ALRVehicle* Vehicle)
{
	const float CenterX = CanvasWidth * 0.5f;
	float Y = CanvasHeight - Margin - Px(150.f);

	DrawTextAt(Labels.GarageTitle, Fonts.LargeBold, FVector2D(CenterX, Y), Tuning.AccentColor, EAlign::Center);
	Y += GetLineHeight(Fonts.LargeBold);

	if (Vehicle)
	{
		const FLRVehicleDefinition& Definition = Vehicle->GetDefinition();
		if (Definition.Id != CachedVehicleId || CachedVehicleName.IsEmpty())
		{
			CachedVehicleId = Definition.Id;
			CachedVehicleName = FText::FromString(Definition.DisplayName);
		}
		DrawTextAt(CachedVehicleName, Fonts.MediumBold, FVector2D(CenterX, Y), Tuning.PrimaryColor, EAlign::Center);
	}
	Y += GetLineHeight(Fonts.MediumBold) + Px(8.f);

	const FText& Prompt = IsUsingGamepadPrompts() ? Labels.GarageGamepad : Labels.GarageKeyboard;
	DrawTextAt(Prompt, Fonts.Medium, FVector2D(CenterX, Y), Tuning.SecondaryColor, EAlign::Center);
}

void ALRHUD::DrawLoading()
{
	// One more dot every 0.4 s; the left edge stays fixed while the dots grow.
	const int32 NumDots = static_cast<int32>(FMath::Fmod(FMath::Max(GetTimeInState(), 0.f) / 0.4f, 4.f));
	const float X = CanvasWidth * 0.5f - static_cast<float>(MeasureText(Labels.Loading[3], Fonts.LargeBold).X) * 0.5f;
	DrawTextAt(Labels.Loading[FMath::Clamp(NumDots, 0, 3)], Fonts.LargeBold, FVector2D(X, CanvasHeight * 0.5f), Tuning.PrimaryColor);
}

void ALRHUD::DrawReplayTag()
{
	// Slow blink, like a recorder.
	const float Alpha = 0.55f + 0.45f * FMath::Cos(GetTimeInState() * 3.f);
	DrawTextAt(Labels.Replay, Fonts.LargeBold, FVector2D(CanvasWidth - Margin, Margin), WithAlpha(Tuning.DangerColor, Alpha), EAlign::Right);
}

void ALRHUD::DrawControlScheme()
{
	const ULRRunContextSubsystem* Context = RunContext.Get();
	if (!Context)
	{
		return;
	}

	const ELRControlScheme ActiveScheme = Context->GetControlScheme();
	const FText& SchemeLabel = ActiveScheme == ELRControlScheme::Smooth ? Labels.SchemeSmooth : ActiveScheme == ELRControlScheme::Simcade ? Labels.SchemeSimcade : Labels.SchemeArcade;
	DrawTextAt(Labels.SchemeCaption, Fonts.Small, FVector2D(Margin, Margin), Tuning.SecondaryColor);
	DrawTextAt(SchemeLabel, Fonts.SmallBold, FVector2D(Margin, Margin + GetLineHeight(Fonts.Small)), Tuning.AccentColor);
}

void ALRHUD::DrawNowPlaying()
{
	constexpr double VisibleSeconds = 4.0;
	const ULRMusicSubsystem* Music = ULRMusicSubsystem::Get(this);
	if (Music == nullptr || Music->GetTrackChangedSeconds() < 0.0 || NowSeconds - Music->GetTrackChangedSeconds() > VisibleSeconds)
	{
		return;
	}
	const FString Name = Music->GetCurrentTrackName();
	if (Name.IsEmpty())
	{
		return;
	}

	const FText Text = FText::Format(LOCTEXT("NowPlaying", "MÚSICA: {0}"), FText::FromString(Name));
	const FVector2D Size = MeasureText(Text, Fonts.SmallBold);
	DrawTextAt(Text, Fonts.SmallBold, FVector2D(CanvasWidth - Margin - Size.X, Margin), Tuning.AccentColor);
}

void ALRHUD::DrawControlsHint()
{
	// Visible through the first countdown and then for the first seconds of the first run.
	float Alpha = 0.f;
	if (CurrentState == ELRRunState::Countdown && RunningEntries == 0)
	{
		Alpha = 1.f;
	}
	else if (CurrentState == ELRRunState::Running && RunningEntries == 1)
	{
		const float Time = static_cast<float>(NowSeconds - FirstRunningSeconds);
		Alpha = FLRHUDAnim::FadeInOut(Time, Tuning.ControlsHintSeconds, 0.f, Tuning.ControlsHintFadeSeconds);
	}
	if (Alpha <= 0.f)
	{
		return;
	}

	const ULRRunContextSubsystem* Context = RunContext.Get();
	const ELRControlScheme ActiveScheme = Context ? Context->GetControlScheme() : ELRControlScheme::Smooth;
	const TArray<FLRHUDLabels::FHintRow>& Rows = ActiveScheme == ELRControlScheme::Smooth ? Labels.SmoothHints
		: (ActiveScheme == ELRControlScheme::Simcade ? Labels.SimcadeHints : Labels.ArcadeHints);

	const float TitleHeight = GetLineHeight(Fonts.MediumBold);
	const float RowHeight = GetLineHeight(Fonts.Small) + Px(4.f);
	const float Padding = Px(14.f);
	const float Width = Px(540.f);
	const float Height = Padding * 2.f + TitleHeight + Px(4.f) + RowHeight * (Rows.Num() + 1);
	const float X = Margin;
	const float Y = CanvasHeight * 0.26f;
	const float KeyboardX = X + Px(210.f);
	const float GamepadX = X + Px(340.f);

	DrawBox(FVector2D(X, Y), FVector2D(Width, Height), WithAlpha(Tuning.PanelColor, Alpha));

	float RowY = Y + Padding;
	DrawTextAt(Labels.ControlsTitle, Fonts.MediumBold, FVector2D(X + Padding, RowY), WithAlpha(Tuning.PrimaryColor, Alpha));
	RowY += TitleHeight + Px(4.f);

	// The column of the device in use is highlighted.
	const bool bGamepad = IsUsingGamepadPrompts();
	const FLinearColor Active = WithAlpha(Tuning.AccentColor, Alpha);
	const FLinearColor Inactive = WithAlpha(Tuning.SecondaryColor, Alpha);
	const FLinearColor ActionColor = WithAlpha(Tuning.PrimaryColor, Alpha);

	DrawTextAt(Labels.KeyboardHeader, Fonts.SmallBold, FVector2D(KeyboardX, RowY), bGamepad ? Inactive : Active);
	DrawTextAt(Labels.GamepadHeader, Fonts.SmallBold, FVector2D(GamepadX, RowY), bGamepad ? Active : Inactive);
	RowY += RowHeight;

	for (const FLRHUDLabels::FHintRow& Row : Rows)
	{
		DrawTextAt(Row.Action, Fonts.Small, FVector2D(X + Padding, RowY), ActionColor);
		DrawTextAt(Row.Keyboard, Fonts.Small, FVector2D(KeyboardX, RowY), bGamepad ? Inactive : Active);
		DrawTextAt(Row.Gamepad, Fonts.Small, FVector2D(GamepadX, RowY), bGamepad ? Active : Inactive);
		RowY += RowHeight;
	}
}

FVector2D ALRHUD::DrawTextAt(const FText& Text, const FSlateFontInfo& Font, const FVector2D& Position, const FLinearColor& Color,
	EAlign Align, float Scale)
{
	if (!Canvas || !HUDFont || Text.IsEmpty())
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D Size = MeasureText(Text, Font) * Scale;
	if (Color.A <= 0.f)
	{
		return Size;
	}

	FVector2D DrawPosition = Position;
	if (Align == EAlign::Center)
	{
		DrawPosition.X -= Size.X * 0.5;
	}
	else if (Align == EAlign::Right)
	{
		DrawPosition.X -= Size.X;
	}
	// A scaled text grows around the center of its line.
	if (Scale != 1.f)
	{
		DrawPosition.Y -= (Size.Y - Size.Y / Scale) * 0.5;
	}

	// Whole pixels keep the glyphs sharp.
	DrawPosition.X = FMath::RoundToDouble(DrawPosition.X);
	DrawPosition.Y = FMath::RoundToDouble(DrawPosition.Y);

	FCanvasTextItem Item(DrawPosition, Text, Font, Color);
	if (bRuntimeFont)
	{
		Item.Scale = FVector2D(Scale, Scale);
	}
	else
	{
		// Bitmap font: the size comes from scaling its glyphs.
		const float FontScale = Scale * LRHUDPrivate::GetOfflineFontScale(Font, HUDFont);
		Item.Scale = FVector2D(FontScale, FontScale);
		Item.BlendMode = SE_BLEND_Translucent;
	}
	const double ShadowOffset = FMath::Max(1.0, FMath::RoundToDouble(Px(2.f)));
	Item.EnableShadow(Tuning.ShadowColor, FVector2D(ShadowOffset, ShadowOffset));
	Item.bDontCorrectStereoscopic = true;
	Canvas->DrawItem(Item);

	return Size;
}

FVector2D ALRHUD::MeasureText(const FText& Text, const FSlateFontInfo& Font) const
{
	if (!HUDFont || Text.IsEmpty())
	{
		return FVector2D::ZeroVector;
	}

	if (!bRuntimeFont)
	{
		const FString& String = Text.ToString();
		const float FontScale = LRHUDPrivate::GetOfflineFontScale(Font, HUDFont);
		return FVector2D(HUDFont->GetStringSize(*String) * FontScale, HUDFont->GetStringHeightSize(*String) * FontScale);
	}

	if (const FSlateRenderer* Renderer = LRHUDPrivate::GetSlateRenderer())
	{
		return Renderer->GetFontMeasureService()->Measure(Text, Font, 1.f);
	}
	return FVector2D::ZeroVector;
}

float ALRHUD::GetLineHeight(const FSlateFontInfo& Font) const
{
	if (!HUDFont)
	{
		return 0.f;
	}

	if (!bRuntimeFont)
	{
		return HUDFont->GetMaxCharHeight() * LRHUDPrivate::GetOfflineFontScale(Font, HUDFont);
	}

	if (const FSlateRenderer* Renderer = LRHUDPrivate::GetSlateRenderer())
	{
		return static_cast<float>(Renderer->GetFontMeasureService()->GetMaxCharacterHeight(Font, 1.f));
	}
	return 0.f;
}

void ALRHUD::DrawBox(const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
{
	if (Size.X > 0.0 && Size.Y > 0.0 && Color.A > 0.f)
	{
		DrawRect(Color, static_cast<float>(Position.X), static_cast<float>(Position.Y), static_cast<float>(Size.X), static_cast<float>(Size.Y));
	}
}

FLinearColor ALRHUD::GetTierColor(ELRNearMissTier Tier) const
{
	switch (Tier)
	{
	case ELRNearMissTier::VeryClose:
		return Tuning.VeryCloseColor;
	case ELRNearMissTier::Razor:
		return Tuning.RazorColor;
	case ELRNearMissTier::Close:
	case ELRNearMissTier::None:
	default:
		return Tuning.CloseColor;
	}
}

FLinearColor ALRHUD::WithAlpha(const FLinearColor& Color, float AlphaScale)
{
	return FLinearColor(Color.R, Color.G, Color.B, Color.A * FMath::Clamp(AlphaScale, 0.f, 1.f));
}

#undef LOCTEXT_NAMESPACE
