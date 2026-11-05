// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UScriptStruct;

/**
 * Loads design data from text (JSON) into USTRUCTs at runtime (ADR-002: text is the source of truth).
 *
 * Files live in <ProjectDir>/Data/** and are staged into the packaged build by LeftRight.Build.cs.
 * Every file uses the same envelope, validated offline by Tools/Scripts/validate-data.ps1:
 *
 *   {
 *     "type": "ScoringRules",      // schema name, see Data/Schema/<type>.schema.json
 *     "id": "default",             // unique id inside its type
 *     "version": 1,                // schema version of this file
 *     "data": { ... }              // fields named EXACTLY like the UPROPERTYs of the target struct
 *   }
 *
 * Fields missing from "data" keep the C++ default values, so structs must have sensible defaults.
 */
struct LRCORE_API FLRDataLoader
{
	/** Absolute path of the data root (<ProjectDir>/Data/). */
	static FString GetDataDirectory();

	/** Absolute path for a path relative to the data root, e.g. "Gameplay/Scoring.json". */
	static FString ToAbsolutePath(const FString& RelativePath);

	/**
	 * Parses an envelope JSON string and converts its "data" object into StructType.
	 * @param OutType / OutId  Optional: receive the envelope "type" and "id".
	 * @return false (and fills OutError) on malformed JSON, missing "data" or conversion failure.
	 */
	static bool ParseStruct(const FString& JsonText, const UScriptStruct* StructType, void* OutStruct,
		FString* OutError = nullptr, FString* OutType = nullptr, FString* OutId = nullptr);

	/** Reads <Data>/RelativePath from disk and calls ParseStruct. */
	static bool LoadStruct(const FString& RelativePath, const UScriptStruct* StructType, void* OutStruct,
		FString* OutError = nullptr);

	/** Typed helper: FLRDataLoader::Load(TEXT("Gameplay/Scoring.json"), MyRules). */
	template <typename TStruct>
	static bool Load(const FString& RelativePath, TStruct& OutStruct, FString* OutError = nullptr)
	{
		return LoadStruct(RelativePath, TStruct::StaticStruct(), &OutStruct, OutError);
	}

	/** Typed helper for in-memory JSON (tests). */
	template <typename TStruct>
	static bool Parse(const FString& JsonText, TStruct& OutStruct, FString* OutError = nullptr)
	{
		return ParseStruct(JsonText, TStruct::StaticStruct(), &OutStruct, OutError);
	}

	/** Lists every *.json under a data sub-folder, as paths relative to the data root, sorted. */
	static TArray<FString> ListFiles(const FString& RelativeFolder);
};
