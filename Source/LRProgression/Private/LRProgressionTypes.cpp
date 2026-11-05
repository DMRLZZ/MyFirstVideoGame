// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRProgressionTypes.h"

#include "UObject/Class.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LRProgressionTypes)

FName LRProgression::GetModeKey(ELRGameModeType Mode)
{
	if (const UEnum* Enum = StaticEnum<ELRGameModeType>())
	{
		return FName(*Enum->GetNameStringByValue(static_cast<int64>(Mode)));
	}
	return NAME_None;
}

namespace LRProgressionTypesPrivate
{
	/** Returns the field that stores a category, or nullptr for ELRUpgradeType::Count. */
	template <typename TState>
	auto* FindLevelField(TState& State, ELRUpgradeType Type)
	{
		switch (Type)
		{
		case ELRUpgradeType::Engine:		return &State.Engine;
		case ELRUpgradeType::Turbo:			return &State.Turbo;
		case ELRUpgradeType::Transmission:	return &State.Transmission;
		case ELRUpgradeType::Tires:			return &State.Tires;
		case ELRUpgradeType::Brakes:		return &State.Brakes;
		case ELRUpgradeType::Suspension:	return &State.Suspension;
		case ELRUpgradeType::Aero:			return &State.Aero;
		default:							return static_cast<decltype(&State.Engine)>(nullptr);
		}
	}
}

int32 FLRVehicleUpgradeState::GetLevel(ELRUpgradeType Type) const
{
	const int32* Field = LRProgressionTypesPrivate::FindLevelField(*this, Type);
	return Field ? *Field : 0;
}

void FLRVehicleUpgradeState::SetLevel(ELRUpgradeType Type, int32 Level)
{
	if (int32* Field = LRProgressionTypesPrivate::FindLevelField(*this, Type))
	{
		*Field = FMath::Clamp(Level, 0, LRProgression::MaxUpgradeLevel);
	}
}

bool FLRVehicleUpgradeState::ClampLevels()
{
	bool bChanged = false;
	for (int32 Index = 0; Index < LRProgression::NumUpgradeTypes; ++Index)
	{
		int32* Field = LRProgressionTypesPrivate::FindLevelField(*this, static_cast<ELRUpgradeType>(Index));
		const int32 Clamped = FMath::Clamp(*Field, 0, LRProgression::MaxUpgradeLevel);
		bChanged |= (Clamped != *Field);
		*Field = Clamped;
	}
	return bChanged;
}

bool FLRVehicleUpgradeState::IsStock() const
{
	for (int32 Index = 0; Index < LRProgression::NumUpgradeTypes; ++Index)
	{
		if (GetLevel(static_cast<ELRUpgradeType>(Index)) != 0)
		{
			return false;
		}
	}
	return true;
}
