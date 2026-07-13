#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeRegistryLibrary.generated.h"

/** Native UFunction/FProperty registry used by manifest and MCP schema generation. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeRegistryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "UnrealBridge|Registry", meta = (
		UnrealBridgeTool,
		ToolRisk = "ReadOnly",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Never"))
	static FString GetToolRegistryJson();

	UFUNCTION(BlueprintPure, Category = "UnrealBridge|Registry", meta = (
		UnrealBridgeTool,
		ToolRisk = "ReadOnly",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Never"))
	static FString GetToolRegistryHash();
};
