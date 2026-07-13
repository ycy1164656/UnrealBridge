#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeAssetFactoryLibrary.generated.h"

/** Small asset creation helpers for common editor-only asset types. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeAssetFactoryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a Blueprint derived from ParentClassPath. Existing assets are returned unchanged.
	 * The new package is marked dirty but is only saved when bSave is true.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AssetFactory", meta = (
		ToolRisk = "Mutating",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Optional",
		ToolSupportsIdempotency = "true"))
	static FString CreateBlueprint(
		const FString& Path,
		const FString& Name,
		const FString& ParentClassPath,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AssetFactory")
	static FString CreateUserDefinedEnum(
		const FString& Path,
		const FString& Name,
		const TArray<FString>& Entries);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AssetFactory")
	static FString CreateUserDefinedStruct(const FString& Path, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AssetFactory")
	static FString CreateDataTable(
		const FString& Path,
		const FString& Name,
		const FString& RowStructPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AssetFactory")
	static FString CreateInputAction(
		const FString& Path,
		const FString& Name,
		const FString& ValueType = TEXT("Boolean"));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AssetFactory")
	static FString CreateInputMappingContext(const FString& Path, const FString& Name);
};
