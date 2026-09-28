#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeContentImportLibrary.generated.h"

/** Bounded source/target contracts over native AssetImportTask and reimport APIs. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeContentImportLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|ContentImport", meta=(ToolRisk="ReadOnly", ToolExecution="GameThreadShort", ToolSaveBehavior="Never", ToolIntroducedVersion="3.3.0"))
	static FString PrepareImport(const FString& RequestJson);

	/** Requires the exact prepared contract, active owned sandbox and unchanged preimages. Never saves. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|ContentImport", meta=(ToolRisk="ExplicitOptIn", ToolExecution="GameThreadShort", ToolSaveBehavior="Never", ToolSupportsIdempotency="true", ToolIntroducedVersion="3.3.0"))
	static FString ExecuteImport(const FString& ContractJson);

	UFUNCTION(BlueprintCallable, Category="UnrealBridge|ContentImport", meta=(ToolRisk="ReadOnly", ToolExecution="GameThreadShort", ToolSaveBehavior="Never", ToolIntroducedVersion="3.3.0"))
	static FString ReadbackImport(const FString& ContractJson);
};
