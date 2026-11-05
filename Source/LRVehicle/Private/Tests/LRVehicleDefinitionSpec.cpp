// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Assist/LRStabilityAssist.h"
#include "LRDataLoader.h"
#include "LRVehicleMath.h"
#include "LRVehicleTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace LRVehicleDefinitionSpecPrivate
{
	/** Vehicles that must ship with the game (garage starter and the first unlocks). */
	const TCHAR* RequiredVehicleIds[] = { TEXT("bauer_r3_sport"), TEXT("aurelion_vanta_s"), TEXT("ironclad_hellion"), TEXT("ardent_vireo") };

	/** Loads Data/<RelativePath> as a VehicleDefinition envelope. */
	bool LoadDefinition(const FString& RelativePath, FLRVehicleDefinition& OutDefinition, FString& OutType, FString& OutId, FString& OutError)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *FLRDataLoader::ToAbsolutePath(RelativePath)))
		{
			OutError = TEXT("file not readable");
			return false;
		}
		return FLRDataLoader::ParseStruct(JsonText, FLRVehicleDefinition::StaticStruct(), &OutDefinition, &OutError, &OutType, &OutId);
	}

	/** A few numbers that summarize how a car feels, used to check that the cars really differ. */
	struct FHandlingSignature
	{
		float TopSpeedKmh = 0.f;
		float PowerToWeight = 0.f;
		float RearToFrontGrip = 0.f;
		float HighSpeedSteeringScale = 0.f;
		float MassKg = 0.f;
	};

	FHandlingSignature MakeSignature(const FLRVehicleDefinition& Definition)
	{
		FHandlingSignature Signature;
		Signature.TopSpeedKmh = Definition.TopSpeedKmh;
		Signature.PowerToWeight = Definition.Engine.MaxTorqueNm / Definition.MassKg;
		Signature.RearToFrontGrip = Definition.RearWheels.FrictionForceMultiplier / FMath::Max(Definition.FrontWheels.FrictionForceMultiplier, 0.01f);
		Signature.HighSpeedSteeringScale = FLRStabilityAssist::EvaluateSteeringScale(Definition.StabilityAssist, 200.f);
		Signature.MassKg = Definition.MassKg;
		return Signature;
	}
}

BEGIN_DEFINE_SPEC(FLRVehicleDefinitionSpec, "LeftRight.Vehicle.Definitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRVehicleDefinitionSpec)

void FLRVehicleDefinitionSpec::Define()
{
	using namespace LRVehicleDefinitionSpecPrivate;

	Describe("C++ defaults", [this]()
	{
		It("describe a valid car apart from identity and assets", [this]()
		{
			FLRVehicleDefinition Definition;
			Definition.Id = FName(TEXT("default"));
			Definition.DisplayName = TEXT("Default");
			Definition.SkeletalMesh = FSoftObjectPath(TEXT("/Game/Vehicles/SportsCar/SKM_SportsCar.SKM_SportsCar"));

			TArray<FString> Problems;
			const bool bValid = LRVehicleMath::ValidateDefinition(Definition, Problems);
			TestTrue(FString::Printf(TEXT("Defaults valid: %s"), *FString::Join(Problems, TEXT("; "))), bValid);
			TestEqual(TEXT("Default severe crash threshold"), Definition.CrashImpactKmh, 25.f);
		});
	});

	Describe("Data/Vehicles", [this]()
	{
		It("contains every required vehicle", [this]()
		{
			const TArray<FString> Files = FLRDataLoader::ListFiles(TEXT("Vehicles"));
			for (const TCHAR* Id : RequiredVehicleIds)
			{
				const FString Expected = FString::Printf(TEXT("Vehicles/%s.json"), Id);
				TestTrue(FString::Printf(TEXT("%s exists"), *Expected), Files.Contains(Expected));
			}
		});

		It("parses every vehicle file into a plausible definition", [this]()
		{
			const TArray<FString> Files = FLRDataLoader::ListFiles(TEXT("Vehicles"));
			TestTrue(TEXT("At least four vehicles"), Files.Num() >= 4);

			for (const FString& File : Files)
			{
				FLRVehicleDefinition Definition;
				FString Type;
				FString Id;
				FString Error;
				const bool bLoaded = LoadDefinition(File, Definition, Type, Id, Error);
				if (!TestTrue(FString::Printf(TEXT("%s parses (%s)"), *File, *Error), bLoaded))
				{
					continue;
				}

				const FString BaseName = FPaths::GetBaseFilename(File);
				TestEqual(FString::Printf(TEXT("%s envelope type"), *File), Type, FString(TEXT("VehicleDefinition")));
				TestEqual(FString::Printf(TEXT("%s envelope id matches the file name"), *File), Id, BaseName);
				TestEqual(FString::Printf(TEXT("%s Id matches the file name"), *File), Definition.Id.ToString(), BaseName);

				TArray<FString> Problems;
				const bool bValid = LRVehicleMath::ValidateDefinition(Definition, Problems);
				TestTrue(FString::Printf(TEXT("%s is valid: %s"), *File, *FString::Join(Problems, TEXT("; "))), bValid);

				// Plausibility of a road car in this game (docs/04-GAMEPLAY.md: 250 to 400+ km/h).
				TestTrue(FString::Printf(TEXT("%s top speed in [200, 450] km/h"), *File), Definition.TopSpeedKmh >= 200.f && Definition.TopSpeedKmh <= 450.f);
				TestTrue(FString::Printf(TEXT("%s mass in [900, 2600] kg"), *File), Definition.MassKg >= 900.f && Definition.MassKg <= 2600.f);
				TestTrue(FString::Printf(TEXT("%s has 2 front and 2 rear wheels"), *File),
					Definition.FrontWheels.BoneNames.Num() == 2 && Definition.RearWheels.BoneNames.Num() == 2);
				TestTrue(FString::Printf(TEXT("%s has an audio profile"), *File), !Definition.AudioProfile.IsNone());
				TestTrue(FString::Printf(TEXT("%s has an animation class"), *File), !Definition.AnimClass.IsNull());
				TestTrue(FString::Printf(TEXT("%s crash threshold in [10, 60] km/h"), *File), Definition.CrashImpactKmh >= 10.f && Definition.CrashImpactKmh <= 60.f);

				// Simcade steering scale must never grow with speed.
				float Previous = 1.f;
				for (float SpeedKmh = 0.f; SpeedKmh <= 450.f; SpeedKmh += 10.f)
				{
					const float Scale = FLRStabilityAssist::EvaluateSteeringScale(Definition.StabilityAssist, SpeedKmh);
					if (!TestTrue(FString::Printf(TEXT("%s steering scale monotonic at %.0f km/h"), *File, SpeedKmh), Scale <= Previous + KINDA_SMALL_NUMBER))
					{
						break;
					}
					Previous = Scale;
				}
			}
		});

		It("gives the four launch cars clearly different handling", [this]()
		{
			TArray<FHandlingSignature> Signatures;
			TMap<FString, FLRVehicleDefinition> Definitions;
			for (const TCHAR* Id : RequiredVehicleIds)
			{
				FLRVehicleDefinition Definition;
				FString Type;
				FString LoadedId;
				FString Error;
				const bool bLoaded = LoadDefinition(FString::Printf(TEXT("Vehicles/%s.json"), Id), Definition, Type, LoadedId, Error);
				if (TestTrue(FString::Printf(TEXT("%s loads (%s)"), Id, *Error), bLoaded))
				{
					Signatures.Add(MakeSignature(Definition));
					Definitions.Add(Id, Definition);
				}
			}
			if (Signatures.Num() != static_cast<int32>(UE_ARRAY_COUNT(RequiredVehicleIds)))
			{
				return;
			}

			// Pairwise: at least two signature values differ by more than 5 %.
			auto Differs = [](float A, float B) { return FMath::Abs(A - B) > 0.05f * FMath::Max(FMath::Abs(A), FMath::Abs(B)); };
			for (int32 I = 0; I < Signatures.Num(); ++I)
			{
				for (int32 J = I + 1; J < Signatures.Num(); ++J)
				{
					const FHandlingSignature& A = Signatures[I];
					const FHandlingSignature& B = Signatures[J];
					const int32 NumDifferences = Differs(A.TopSpeedKmh, B.TopSpeedKmh) + Differs(A.PowerToWeight, B.PowerToWeight)
						+ Differs(A.RearToFrontGrip, B.RearToFrontGrip) + Differs(A.HighSpeedSteeringScale, B.HighSpeedSteeringScale)
						+ Differs(A.MassKg, B.MassKg);
					TestTrue(FString::Printf(TEXT("%s vs %s differ (%d)"), RequiredVehicleIds[I], RequiredVehicleIds[J], NumDifferences), NumDifferences >= 2);
				}
			}

			// Roles described in the design.
			const FLRVehicleDefinition& Starter = Definitions[TEXT("bauer_r3_sport")];
			const FLRVehicleDefinition& Supercar = Definitions[TEXT("ardent_vireo")];
			const FLRVehicleDefinition& Muscle = Definitions[TEXT("ironclad_hellion")];
			const FLRVehicleDefinition& Sedan = Definitions[TEXT("aurelion_vanta_s")];
			TestTrue(TEXT("Starter tops out around 250 km/h"), FMath::IsNearlyEqual(Starter.TopSpeedKmh, 250.f, 15.f));
			TestTrue(TEXT("Supercar tops out around 330 km/h"), FMath::IsNearlyEqual(Supercar.TopSpeedKmh, 330.f, 15.f));
			TestTrue(TEXT("Supercar is the fastest"), Supercar.TopSpeedKmh > FMath::Max3(Starter.TopSpeedKmh, Muscle.TopSpeedKmh, Sedan.TopSpeedKmh));
			TestTrue(TEXT("Muscle car is rear-biased on grip (nervous)"), MakeSignature(Muscle).RearToFrontGrip < MakeSignature(Starter).RearToFrontGrip);
			TestTrue(TEXT("Muscle car has the weakest stability aid"), Muscle.StabilityAssist.YawDampingGain < FMath::Min3(Starter.StabilityAssist.YawDampingGain,
				Sedan.StabilityAssist.YawDampingGain, Supercar.StabilityAssist.YawDampingGain));
			TestTrue(TEXT("Sedan is the heaviest"), Sedan.MassKg > FMath::Max3(Starter.MassKg, Muscle.MassKg, Supercar.MassKg));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
