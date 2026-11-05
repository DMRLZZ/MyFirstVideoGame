// Copyright LEFTRIGHT. All Rights Reserved.

#include "HAL/FileManager.h"
#include "LRCoreTypes.h"
#include "LRDataLoader.h"
#include "LREvents.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FLRDataLoaderSpec, "LeftRight.Core.DataLoader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

	/** Temporary folder under Data/ used by the ListFiles tests, relative to the data root ('_' prefix: ignored by validate_data.py). */
	FString TempFolderRelative;

	/** Absolute path of TempFolderRelative. */
	FString TempFolderAbsolute;

	/** Writes a text file under the temporary folder; returns true on success. */
	bool WriteTempFile(const FString& RelativeToTemp, const FString& Contents)
	{
		const FString FullPath = FPaths::Combine(TempFolderAbsolute, RelativeToTemp);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), /*Tree*/ true);
		return FFileHelper::SaveStringToFile(Contents, *FullPath);
	}

END_DEFINE_SPEC(FLRDataLoaderSpec)

void FLRDataLoaderSpec::Define()
{
	Describe("ParseStruct", [this]()
	{
		It("fills every field of a struct from a complete envelope, including enums by name and bools", [this]()
		{
			const FString Json = TEXT(R"json(
			{
				"type": "NearMissEvent",
				"id": "spec_full",
				"version": 1,
				"data": {
					"ClearanceMeters": 0.4,
					"RelativeSpeedKmh": 85.5,
					"Tier": "VeryClose",
					"bOncoming": true,
					"bThreadTheNeedle": false,
					"Archetype": "truck",
					"TrafficVehicleId": 17,
					"PlayerCoord": { "S": 12345.25, "D": -1.75 }
				}
			})json");

			FLRNearMissEvent Event;
			FString Error;
			FString Type;
			FString Id;
			const bool bOk = FLRDataLoader::ParseStruct(Json, FLRNearMissEvent::StaticStruct(), &Event, &Error, &Type, &Id);

			TestTrue(TEXT("parse succeeded"), bOk);
			TestTrue(TEXT("no error message"), Error.IsEmpty());
			TestEqual(TEXT("envelope type"), Type, FString(TEXT("NearMissEvent")));
			TestEqual(TEXT("envelope id"), Id, FString(TEXT("spec_full")));
			TestEqual(TEXT("ClearanceMeters"), Event.ClearanceMeters, 0.4f, 1.0e-6f);
			TestEqual(TEXT("RelativeSpeedKmh"), Event.RelativeSpeedKmh, 85.5f, 1.0e-6f);
			TestTrue(TEXT("Tier parsed from its enum name"), Event.Tier == ELRNearMissTier::VeryClose);
			TestTrue(TEXT("bOncoming"), Event.bOncoming);
			TestFalse(TEXT("bThreadTheNeedle"), Event.bThreadTheNeedle);
			TestTrue(TEXT("Archetype"), Event.Archetype == FName(TEXT("truck")));
			TestEqual(TEXT("TrafficVehicleId"), Event.TrafficVehicleId, 17);
			TestEqual(TEXT("PlayerCoord.S (double)"), Event.PlayerCoord.S, 12345.25, 1.0e-9);
			TestEqual(TEXT("PlayerCoord.D"), Event.PlayerCoord.D, -1.75f, 1.0e-6f);
		});

		It("works through the typed Parse helper", [this]()
		{
			const FString Json = TEXT(R"json({"type":"NearMissEvent","id":"typed","version":1,"data":{"Tier":"Razor","bThreadTheNeedle":true}})json");
			FLRNearMissEvent Event;
			TestTrue(TEXT("parse succeeded"), FLRDataLoader::Parse(Json, Event));
			TestTrue(TEXT("Tier"), Event.Tier == ELRNearMissTier::Razor);
			TestTrue(TEXT("bThreadTheNeedle"), Event.bThreadTheNeedle);
		});

		It("returns false with an error when the data object is missing", [this]()
		{
			const FString Json = TEXT(R"json({"type":"NearMissEvent","id":"no_data","version":1})json");
			FLRNearMissEvent Event;
			FString Error;
			TestFalse(TEXT("parse failed"), FLRDataLoader::ParseStruct(Json, FLRNearMissEvent::StaticStruct(), &Event, &Error));
			TestFalse(TEXT("error message filled"), Error.IsEmpty());
			TestTrue(TEXT("error mentions data"), Error.Contains(TEXT("data")));
		});

		It("returns false when data is not an object", [this]()
		{
			const FString Json = TEXT(R"json({"type":"NearMissEvent","id":"bad_data","version":1,"data":5})json");
			FLRNearMissEvent Event;
			FString Error;
			TestFalse(TEXT("parse failed"), FLRDataLoader::ParseStruct(Json, FLRNearMissEvent::StaticStruct(), &Event, &Error));
			TestFalse(TEXT("error message filled"), Error.IsEmpty());
		});

		It("returns false with an error on malformed JSON", [this]()
		{
			const FString Json = TEXT(R"json({"type":"NearMissEvent","id":"broken","data":{"ClearanceMeters": 0.4,)json");
			FLRNearMissEvent Event;
			FString Error;
			TestFalse(TEXT("parse failed"), FLRDataLoader::ParseStruct(Json, FLRNearMissEvent::StaticStruct(), &Event, &Error));
			TestTrue(TEXT("error says malformed"), Error.Contains(TEXT("Malformed")));

			FString EmptyError;
			TestFalse(TEXT("empty text fails"), FLRDataLoader::ParseStruct(FString(), FLRNearMissEvent::StaticStruct(), &Event, &EmptyError));
			TestFalse(TEXT("empty text error filled"), EmptyError.IsEmpty());
		});

		It("returns false on invalid arguments", [this]()
		{
			const FString Json = TEXT(R"json({"type":"NearMissEvent","id":"x","version":1,"data":{}})json");
			FLRNearMissEvent Event;
			FString Error;
			TestFalse(TEXT("null struct type"), FLRDataLoader::ParseStruct(Json, nullptr, &Event, &Error));
			TestFalse(TEXT("null output"), FLRDataLoader::ParseStruct(Json, FLRNearMissEvent::StaticStruct(), nullptr, &Error));
		});

		It("ignores unknown fields", [this]()
		{
			const FString Json = TEXT(R"json(
			{
				"type": "NearMissEvent", "id": "unknown_fields", "version": 1,
				"data": { "ClearanceMeters": 0.2, "NotAField": 42, "AnotherOne": { "Nested": [1, 2, 3] } },
				"extraEnvelopeKey": "ignored"
			})json");
			FLRNearMissEvent Event;
			FString Error;
			TestTrue(TEXT("parse succeeded"), FLRDataLoader::ParseStruct(Json, FLRNearMissEvent::StaticStruct(), &Event, &Error));
			TestEqual(TEXT("known field still read"), Event.ClearanceMeters, 0.2f, 1.0e-6f);
		});

		It("keeps the C++ defaults for missing fields", [this]()
		{
			const FString Json = TEXT(R"json({"type":"NearMissEvent","id":"partial","version":1,"data":{"RelativeSpeedKmh":120}})json");
			FLRNearMissEvent Event;
			TestTrue(TEXT("parse succeeded"), FLRDataLoader::Parse(Json, Event));

			const FLRNearMissEvent Defaults;
			TestEqual(TEXT("RelativeSpeedKmh read"), Event.RelativeSpeedKmh, 120.f, 1.0e-6f);
			TestEqual(TEXT("ClearanceMeters default"), Event.ClearanceMeters, Defaults.ClearanceMeters);
			TestTrue(TEXT("Tier default"), Event.Tier == Defaults.Tier);
			TestTrue(TEXT("bOncoming default"), Event.bOncoming == Defaults.bOncoming);
			TestTrue(TEXT("Archetype default"), Event.Archetype == Defaults.Archetype);
			TestEqual(TEXT("TrafficVehicleId default"), Event.TrafficVehicleId, static_cast<int32>(INDEX_NONE));
			TestEqual(TEXT("PlayerCoord.S default"), Event.PlayerCoord.S, 0.0);
		});

		It("leaves pre-set values untouched when the field is absent", [this]()
		{
			const FString Json = TEXT(R"json({"type":"NearMissEvent","id":"overlay","version":1,"data":{"bOncoming":true}})json");
			FLRNearMissEvent Event;
			Event.TrafficVehicleId = 99;
			Event.Tier = ELRNearMissTier::Close;
			TestTrue(TEXT("parse succeeded"), FLRDataLoader::Parse(Json, Event));
			TestTrue(TEXT("bOncoming read"), Event.bOncoming);
			TestEqual(TEXT("TrafficVehicleId kept"), Event.TrafficVehicleId, 99);
			TestTrue(TEXT("Tier kept"), Event.Tier == ELRNearMissTier::Close);
		});
	});

	Describe("Paths", [this]()
	{
		It("resolves the data directory under the project", [this]()
		{
			const FString DataDir = FLRDataLoader::GetDataDirectory();
			TestFalse(TEXT("data dir is absolute"), FPaths::IsRelative(DataDir));
			TestTrue(TEXT("data dir ends with Data"), DataDir.EndsWith(TEXT("Data")) || DataDir.EndsWith(TEXT("Data/")));
			TestEqual(TEXT("ToAbsolutePath joins the data dir"),
				FLRDataLoader::ToAbsolutePath(TEXT("Gameplay/Scoring.json")), FPaths::Combine(DataDir, TEXT("Gameplay/Scoring.json")));
		});
	});

	Describe("ListFiles", [this]()
	{
		BeforeEach([this]()
		{
			TempFolderRelative = FString::Printf(TEXT("_AutomationTmp_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			TempFolderAbsolute = FLRDataLoader::ToAbsolutePath(TempFolderRelative);

			const FString Envelope = TEXT(R"json({"type":"AutomationTemp","id":"tmp","version":1,"data":{}})json");
			TestTrue(TEXT("write b.json"), WriteTempFile(TEXT("b.json"), Envelope));
			TestTrue(TEXT("write a.json"), WriteTempFile(TEXT("a.json"), Envelope));
			TestTrue(TEXT("write Sub/c.json"), WriteTempFile(TEXT("Sub/c.json"), Envelope));
			TestTrue(TEXT("write notes.txt"), WriteTempFile(TEXT("notes.txt"), TEXT("not json")));
		});

		AfterEach([this]()
		{
			if (!TempFolderAbsolute.IsEmpty())
			{
				IFileManager::Get().DeleteDirectory(*TempFolderAbsolute, /*RequireExists*/ false, /*Tree*/ true);
			}
			TempFolderRelative.Reset();
			TempFolderAbsolute.Reset();
		});

		It("returns sorted .json paths relative to the data root, recursively", [this]()
		{
			const TArray<FString> Files = FLRDataLoader::ListFiles(TempFolderRelative);

			TArray<FString> Expected;
			Expected.Add(TempFolderRelative / TEXT("a.json"));
			Expected.Add(TempFolderRelative / TEXT("b.json"));
			Expected.Add(TempFolderRelative / TEXT("Sub/c.json"));

			TestEqual(TEXT("number of json files (txt excluded)"), Files.Num(), Expected.Num());
			for (int32 Index = 0; Index < FMath::Min(Files.Num(), Expected.Num()); ++Index)
			{
				TestEqual(FString::Printf(TEXT("file %d"), Index), Files[Index], Expected[Index]);
			}
			for (const FString& File : Files)
			{
				TestTrue(FString::Printf(TEXT("'%s' is relative"), *File), FPaths::IsRelative(File));
				TestFalse(FString::Printf(TEXT("'%s' uses forward slashes"), *File), File.Contains(TEXT("\\")));
				TestTrue(FString::Printf(TEXT("'%s' resolves to an existing file"), *File),
					FPaths::FileExists(FLRDataLoader::ToAbsolutePath(File)));
			}
		});

		It("returns an empty list for a folder that does not exist", [this]()
		{
			const TArray<FString> Files = FLRDataLoader::ListFiles(TempFolderRelative / TEXT("DoesNotExist"));
			TestEqual(TEXT("no files"), Files.Num(), 0);
		});

		It("loads a listed file with LoadStruct", [this]()
		{
			const TArray<FString> Files = FLRDataLoader::ListFiles(TempFolderRelative);
			if (!TestTrue(TEXT("has files"), Files.Num() > 0))
			{
				return;
			}
			FLRNearMissEvent Event;
			FString Error;
			TestTrue(TEXT("LoadStruct succeeded"), FLRDataLoader::LoadStruct(Files[0], FLRNearMissEvent::StaticStruct(), &Event, &Error));
			TestTrue(TEXT("no error"), Error.IsEmpty());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
