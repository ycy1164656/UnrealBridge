#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeUE58Library.generated.h"

/**
 * UE 5.8 ToolsetRegistry federation surface.
 *
 * The public API deliberately uses only CoreUObject types so UE 5.3-5.7 do
 * not parse or link Experimental 5.8 headers. On older engines every method
 * returns a structured "unsupported" result.
 */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeUE58Library : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static bool IsOfficialToolsetRegistryAvailable();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString GetOfficialToolsetCatalogJson();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString GetOfficialToolsetSchemaJson(const FString& ToolsetName);

	/** Start an official call without waiting. Only schema-declared read-only tools are accepted. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "ReadOnly", ToolExecution = "PollingJob", ToolSaveBehavior = "Never",
			ToolSupportsIdempotency = "true", ToolProvider = "EpicToolsetRegistry",
			ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString StartOfficialToolsetCall(
		const FString& CallId,
		const FString& ToolsetName,
		const FString& ToolName,
		const FString& JsonInput);

	/** Poll a previously started call. The returned JSON always contains complete and success. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString PollOfficialToolsetCall(const FString& CallId);

	/** Release local tracking only; UE 5.8 ToolsetRegistry has no provider-wide cancellation contract. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "Modify", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString AbandonOfficialToolsetCall(const FString& CallId);
};

namespace UnrealBridgeUE58Adapter
{
	UNREALBRIDGE_API void Startup();
	UNREALBRIDGE_API void Shutdown();
	UNREALBRIDGE_API bool IsCompiled();
	UNREALBRIDGE_API bool IsRegistryAvailable();
}
