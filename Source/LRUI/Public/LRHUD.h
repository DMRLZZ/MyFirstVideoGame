// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/HUD.h"
#include "LRCoreTypes.h"
#include "LREvents.h"
#include "LRHUDFormat.h"
#include "LRHUDState.h"
#include "LRHUDTypes.h"

#include "LRHUD.generated.h"

class ALRVehicle;
class FLRHUDInputListener;
class UFont;
class ULRGameEventSubsystem;
class ULRRunContextSubsystem;

/**
 * Localized texts the HUD draws every frame, built once in BeginPlay so DrawHUD does not look them up per frame.
 * Plain data owned by ALRHUD.
 */
struct LRUI_API FLRHUDLabels
{
	FText ScoreCaption;
	FText GearCaption;
	FText SpeedTierCaption;
	FText ComboBroken;
	/** Near-miss tier labels indexed by ELRNearMissTier. */
	FText TierLabels[4];
	FText TagOncoming;
	FText TagThreadTheNeedle;
	FText SchemeArcade;
	FText SchemeSimcade;
	FText SchemeSmooth;
	FText CountdownGo;
	FText Crashed;
	FText ResultsTitle;
	FText ResultsScore;
	FText ResultsDistance;
	FText ResultsNearMisses;
	FText ResultsBestCombo;
	FText ResultsTopSpeed;
	FText ResultsDuration;
	FText ResultsCredits;
	FText RestartKeyboard;
	FText RestartGamepad;
	FText Replay;
	FText SchemeCaption;
	FText ControlsTitle;
	FText KeyboardHeader;
	FText GamepadHeader;
	/** "CARGANDO" with 0..3 trailing dots. */
	FText Loading[4];

	/** One row of the controls hint: action, keyboard prompt, gamepad prompt. */
	struct FHintRow
	{
		FText Action;
		FText Keyboard;
		FText Gamepad;
	};
	/** Rows for the Arcade scheme. */
	TArray<FHintRow> SmoothHints;
	TArray<FHintRow> ArcadeHints;
	/** Rows for the Simcade scheme. */
	TArray<FHintRow> SimcadeHints;

	/** Builds every text (LOCTEXT namespace "LRHUD"). */
	void Build();
};

/**
 * Prototype HUD of LEFTRIGHT drawn with UCanvas (no UMG assets).
 *
 * - Listens to the LRCore event bus (score, combo, speed tier, near misses, run state, results): subscribed in
 *   BeginPlay, unsubscribed in EndPlay. Reads the player car (ALRVehicle) for speed, gear and rpm.
 * - Everything is laid out at the 1080p reference height and scaled by the canvas height, with crisp runtime fonts
 *   (FSlateFontInfo over the engine fonts) rebuilt only when the resolution changes.
 * - Speed units follow the console variable lr.HUD.Units (kmh | mph).
 * - Prompts adapt to the last input device (Slate input pre-processor + FLRInputDeviceTracker).
 * Look and timing come from Data/UI/HUD.json (FLRHUDTuning); the countdown length from Data/Gameplay/GameRules.json.
 */
UCLASS()
class LRUI_API ALRHUD : public AHUD
{
	GENERATED_BODY()

public:
	ALRHUD();

	//~ AHUD
	virtual void DrawHUD() override;

	/** Speed units currently selected by lr.HUD.Units. */
	static ELRSpeedUnits GetSpeedUnits();

	/** Sets lr.HUD.Units (e.g. from a settings menu). */
	static void SetSpeedUnits(ELRSpeedUnits Units);

	/** Applied (sanitized) tuning. */
	const FLRHUDTuning& GetTuning() const { return Tuning; }

	/** Replaces the tuning at runtime (sanitized; fonts are rebuilt on the next frame). */
	void SetTuning(const FLRHUDTuning& InTuning);

	/** Countdown length used by the countdown overlay, seconds. */
	float GetCountdownSeconds() const { return GameRules.CountdownSeconds; }

	/** Run state the HUD is currently presenting. */
	ELRRunState GetDisplayedRunState() const { return CurrentState; }

	/** True when prompts are shown for a gamepad. */
	bool IsUsingGamepadPrompts() const;

protected:
	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Horizontal alignment for text drawing. */
	enum class EAlign : uint8
	{
		Left,
		Center,
		Right
	};

	/** Fonts at the current UI scale. */
	struct FFontSet
	{
		FSlateFontInfo Speed;
		FSlateFontInfo Score;
		FSlateFontInfo Banner;
		FSlateFontInfo LargeBold;
		FSlateFontInfo MediumBold;
		FSlateFontInfo Medium;
		FSlateFontInfo Small;
		FSlateFontInfo SmallBold;
	};

	// --- Setup -----------------------------------------------------------------------------------

	void LoadData();
	/** Pushes the current tuning into the pure helpers and forces fonts and texts to be rebuilt. */
	void ApplyTuning();
	/** Picks the engine font (runtime cached preferred). */
	void CacheEngineFonts();
	void BindEvents();
	void UnbindEvents();
	void RegisterInputListener();
	void UnregisterInputListener();
	void ResetRunPresentation();

	// --- Event handlers --------------------------------------------------------------------------

	void HandleNearMiss(const FLRNearMissEvent& Event);
	void HandleScoreChanged(const FLRScoreChangedEvent& Event);
	void HandleComboChanged(const FLRComboChangedEvent& Event);
	void HandleSpeedTierChanged(const FLRSpeedTierChangedEvent& Event);
	void HandleRunStateChanged(const FLRRunStateChangedEvent& Event);
	void HandleRunReset(const FLRRunResetEvent& Event);
	void HandleRunFinished(const FLRRunResult& Result);

	/** Enters a run state (records the time, counts runs for the controls hint). */
	void EnterState(ELRRunState NewState, ELRRunState OldState);

	// --- Per-frame update ------------------------------------------------------------------------

	void UpdateTime();
	void UpdateLayout();
	void RebuildFonts();
	void RefreshCachedTexts(const ALRVehicle* Vehicle);
	/** Forces every cached number text to be rebuilt on the next frame. */
	void InvalidateTextCaches();
	void RebuildSpeedTierText();
	void RebuildComboTexts();
	void RebuildResultTexts();
	ALRVehicle* FindVehicle() const;

	// --- Drawing ---------------------------------------------------------------------------------

	void DrawSpeedometer(const ALRVehicle& Vehicle);
	void DrawScoreAndCombo();
	void DrawNearMissPopups();
	void DrawCountdownOverlay();
	void DrawGoBanner();
	void DrawCrashOverlay();
	void DrawResultsPanel();
	void DrawLoading();
	void DrawReplayTag();
	void DrawControlScheme();
	void DrawControlsHint();

	/** Draws text at Position (top-left of the text box, adjusted by Align). Returns the measured size (scaled). */
	FVector2D DrawTextAt(const FText& Text, const FSlateFontInfo& Font, const FVector2D& Position, const FLinearColor& Color,
		EAlign Align = EAlign::Left, float Scale = 1.f);

	/** Measured size of a text with a font, pixels. */
	FVector2D MeasureText(const FText& Text, const FSlateFontInfo& Font) const;

	/** Name of the song, for a few seconds after it changes. */
	void DrawNowPlaying();

	/** Height of one line of a font, pixels. */
	float GetLineHeight(const FSlateFontInfo& Font) const;

	/** Translucent rectangle. */
	void DrawBox(const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color);

	/** Color of a near-miss tier. */
	FLinearColor GetTierColor(ELRNearMissTier Tier) const;

	/** Seconds since the current state started (real time). */
	float GetTimeInState() const;

	/** Reference pixels -> canvas pixels. */
	float Px(float ReferencePixels) const { return ReferencePixels * UIScale; }

	/** Color with its alpha multiplied. */
	static FLinearColor WithAlpha(const FLinearColor& Color, float AlphaScale);

	// --- Data ------------------------------------------------------------------------------------

	FLRHUDTuning Tuning;
	FLRHUDGameRules GameRules;
	FLRHUDLabels Labels;

	/** Engine font used for every text (GEngine->GetLargeFont, Roboto with a runtime cache). */
	UPROPERTY(Transient)
	TObjectPtr<UFont> HUDFont;

	/** Fallback engine font (GEngine->GetMediumFont) if the large font is missing. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> FallbackFont;

	FFontSet Fonts;
	float FontsBuiltForScale = -1.f;
	bool bRuntimeFont = true;

	// --- Layout ----------------------------------------------------------------------------------

	float UIScale = 1.f;
	float CanvasWidth = 1920.f;
	float CanvasHeight = 1080.f;
	float Margin = 40.f;

	// --- Time ------------------------------------------------------------------------------------

	double NowSeconds = 0.0;
	double LastFrameSeconds = -1.0;
	float FrameDelta = 0.f;

	// --- Run presentation state --------------------------------------------------------------------

	ELRRunState CurrentState = ELRRunState::Garage;
	ELRRunState PreviousState = ELRRunState::Garage;
	double StateEnterSeconds = 0.0;
	int32 RunningEntries = 0;
	double FirstRunningSeconds = 0.0;

	int64 Score = 0;
	double DisplayedScore = 0.0;
	float SpeedTierMultiplier = 1.f;
	FLRHUDComboTracker Combo;
	FLRHUDPopupQueue Popups;

	FLRRunResult LastResult;
	bool bHasResult = false;

	// --- Cached texts (rebuilt only when their value changes) -------------------------------------

	int64 CachedScoreValue = MIN_int64;
	FText CachedScoreText;
	int32 CachedSpeedValue = INDEX_NONE;
	ELRSpeedUnits CachedSpeedUnits = ELRSpeedUnits::Kmh;
	FText CachedSpeedText;
	FText CachedUnitText;
	int32 CachedGear = MIN_int32;
	FText CachedGearText;
	FText CachedTierText;
	float CachedComboMultiplier = -1.f;
	int32 CachedComboChain = INDEX_NONE;
	FText CachedComboText;
	FText CachedChainText;
	int32 CachedCountdownNumber = INDEX_NONE;
	FText CachedCountdownText;
	/** Points text per popup slot, refreshed when the points of the slot change. */
	TArray<FText> PopupPointsTexts;
	TArray<int64> PopupPointsValues;
	/** Result rows: score, distance, near misses, best combo, top speed, duration, credits. */
	FText ResultValues[7];
	ELRSpeedUnits ResultUnits = ELRSpeedUnits::Kmh;

	// --- Event bus and input -----------------------------------------------------------------------

	TWeakObjectPtr<ULRGameEventSubsystem> EventBus;
	TWeakObjectPtr<ULRRunContextSubsystem> RunContext;
	FDelegateHandle NearMissHandle;
	FDelegateHandle ScoreHandle;
	FDelegateHandle ComboHandle;
	FDelegateHandle SpeedTierHandle;
	FDelegateHandle RunStateHandle;
	FDelegateHandle RunResetHandle;
	FDelegateHandle RunFinishedHandle;

	TSharedPtr<FLRHUDInputListener> InputListener;
};
