// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FLRCoreTypesSpec, "LeftRight.Core.RoadCoord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRCoreTypesSpec)

void FLRCoreTypesSpec::Define()
{
	Describe("FLRRoadCoord", [this]()
	{
		It("defaults to the route origin", [this]()
		{
			const FLRRoadCoord Coord;
			TestEqual(TEXT("S defaults to 0"), Coord.S, 0.0);
			TestEqual(TEXT("D defaults to 0"), Coord.D, 0.f);
		});

		It("stores constructor values", [this]()
		{
			const FLRRoadCoord Coord(250.5, -3.7f);
			TestEqual(TEXT("S"), Coord.S, 250.5);
			TestEqual(TEXT("D (+ = right, - = left)"), Coord.D, -3.7f);
		});

		It("keeps centimeter precision far along the route (S is double)", [this]()
		{
			const FLRRoadCoord Coord(1.0e7 + 0.01, 0.f);
			TestEqual(TEXT("10 000 km + 1 cm keeps the centimeter"), Coord.S - 1.0e7, 0.01, 1.0e-6);
		});

		It("exposes S and D as reflected properties with the expected types (JSON keys)", [this]()
		{
			const UScriptStruct* Struct = FLRRoadCoord::StaticStruct();
			TestNotNull(TEXT("S is a double property"), CastField<FDoubleProperty>(Struct->FindPropertyByName(TEXT("S"))));
			TestNotNull(TEXT("D is a float property"), CastField<FFloatProperty>(Struct->FindPropertyByName(TEXT("D"))));
		});
	});

	Describe("Enums", [this]()
	{
		It("keep the names used by JSON data files", [this]()
		{
			TestEqual(TEXT("ELRNearMissTier::VeryClose"), StaticEnum<ELRNearMissTier>()->GetNameStringByValue(static_cast<int64>(ELRNearMissTier::VeryClose)), FString(TEXT("VeryClose")));
			TestEqual(TEXT("ELRControlScheme::Simcade"), StaticEnum<ELRControlScheme>()->GetNameStringByValue(static_cast<int64>(ELRControlScheme::Simcade)), FString(TEXT("Simcade")));
			TestEqual(TEXT("ELRGameModeType::EndlessTwoWay"), StaticEnum<ELRGameModeType>()->GetNameStringByValue(static_cast<int64>(ELRGameModeType::EndlessTwoWay)), FString(TEXT("EndlessTwoWay")));
			TestEqual(TEXT("ELRCameraMode::Viral"), StaticEnum<ELRCameraMode>()->GetNameStringByValue(static_cast<int64>(ELRCameraMode::Viral)), FString(TEXT("Viral")));
			TestEqual(TEXT("ELRRunState::Running"), StaticEnum<ELRRunState>()->GetNameStringByValue(static_cast<int64>(ELRRunState::Running)), FString(TEXT("Running")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
