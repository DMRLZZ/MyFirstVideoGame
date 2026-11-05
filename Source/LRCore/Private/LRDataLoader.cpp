// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRDataLoader.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "LRCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"

namespace LRDataLoaderPrivate
{
	static void SetError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}
}

FString FLRDataLoader::GetDataDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data")));
}

FString FLRDataLoader::ToAbsolutePath(const FString& RelativePath)
{
	return FPaths::Combine(GetDataDirectory(), RelativePath);
}

bool FLRDataLoader::ParseStruct(const FString& JsonText, const UScriptStruct* StructType, void* OutStruct,
	FString* OutError, FString* OutType, FString* OutId)
{
	using namespace LRDataLoaderPrivate;

	if (!StructType || !OutStruct)
	{
		SetError(OutError, TEXT("Invalid struct type or output pointer"));
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		SetError(OutError, FString::Printf(TEXT("Malformed JSON: %s"), *Reader->GetErrorMessage()));
		return false;
	}

	if (OutType)
	{
		Root->TryGetStringField(TEXT("type"), *OutType);
	}
	if (OutId)
	{
		Root->TryGetStringField(TEXT("id"), *OutId);
	}

	const TSharedPtr<FJsonObject>* DataObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("data"), DataObject) || !DataObject || !DataObject->IsValid())
	{
		SetError(OutError, TEXT("Missing \"data\" object in data envelope"));
		return false;
	}

	// Non-strict: unknown JSON fields are ignored, missing fields keep the C++ defaults.
	if (!FJsonObjectConverter::JsonObjectToUStruct(DataObject->ToSharedRef(), StructType, OutStruct, 0, 0))
	{
		SetError(OutError, FString::Printf(TEXT("Could not convert \"data\" into %s"), *StructType->GetName()));
		return false;
	}

	return true;
}

bool FLRDataLoader::LoadStruct(const FString& RelativePath, const UScriptStruct* StructType, void* OutStruct, FString* OutError)
{
	const FString AbsolutePath = ToAbsolutePath(RelativePath);

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *AbsolutePath))
	{
		LRDataLoaderPrivate::SetError(OutError, FString::Printf(TEXT("Data file not found: %s"), *AbsolutePath));
		UE_LOG(LogLRCore, Warning, TEXT("Data file not found: %s"), *AbsolutePath);
		return false;
	}

	FString ParseError;
	if (!ParseStruct(JsonText, StructType, OutStruct, &ParseError))
	{
		LRDataLoaderPrivate::SetError(OutError, FString::Printf(TEXT("%s (%s)"), *ParseError, *RelativePath));
		UE_LOG(LogLRCore, Error, TEXT("Failed to load data %s: %s"), *RelativePath, *ParseError);
		return false;
	}

	UE_LOG(LogLRCore, Verbose, TEXT("Loaded data %s into %s"), *RelativePath, *StructType->GetName());
	return true;
}

TArray<FString> FLRDataLoader::ListFiles(const FString& RelativeFolder)
{
	const FString Root = GetDataDirectory();
	const FString Folder = FPaths::Combine(Root, RelativeFolder);

	TArray<FString> AbsoluteFiles;
	IFileManager::Get().FindFilesRecursive(AbsoluteFiles, *Folder, TEXT("*.json"), /*Files*/ true, /*Directories*/ false);

	TArray<FString> Result;
	Result.Reserve(AbsoluteFiles.Num());
	for (FString& File : AbsoluteFiles)
	{
		FPaths::MakePathRelativeTo(File, *(Root / TEXT("")));
		Result.Add(MoveTemp(File));
	}

	Result.Sort();
	return Result;
}
