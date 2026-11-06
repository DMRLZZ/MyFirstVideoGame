// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRGameState.h"

#include "LRScoreComponent.h"

ALRGameState::ALRGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ScoreComponent = CreateDefaultSubobject<ULRScoreComponent>(TEXT("ScoreComponent"));
}
