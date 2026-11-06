// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"

#include "LRGameState.generated.h"

class ULRScoreComponent;

/** Game state of LEFTRIGHT. Owns the score of the current run. Created by ALRGameModeEndless. */
UCLASS()
class LRGAMEPLAY_API ALRGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ALRGameState(const FObjectInitializer& ObjectInitializer);

	/** Score of the current run (never null after construction). */
	ULRScoreComponent* GetScoreComponent() const { return ScoreComponent; }

private:
	/** Default subobject that accumulates the score. */
	UPROPERTY(VisibleAnywhere, Category = "Score")
	TObjectPtr<ULRScoreComponent> ScoreComponent;
};
