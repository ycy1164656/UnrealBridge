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

	/** Session/revision snapshot. IncludeCatalog adds the full raw array; metadata alone is cheap.
	 * Registration, removal and name-filter changes invalidate the revision. Existing raw discovery is unchanged.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString GetOfficialToolsetCatalogSnapshotJson(bool bIncludeCatalog = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString GetOfficialToolsetSchemaJson(const FString& ToolsetName);

	/** Return the deny-by-default, exact-id official execution policy bundled with the plugin. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString GetOfficialToolPolicyJson();

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

	/** Start an audited Slate/Automation/PIE/GameFeature-style runtime interaction. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "RuntimeInteraction", ToolExecution = "PollingJob", ToolSaveBehavior = "Never",
			ToolSupportsIdempotency = "false", ToolProvider = "EpicToolsetRegistry",
			ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString StartOfficialRuntimeToolsetCall(
		const FString& CallId,
		const FString& ToolsetName,
		const FString& ToolName,
		const FString& JsonInput,
		bool bAllowRuntimeSideEffects = false);

	/**
	 * Execute an audited non-destructive official mutation inside the current
	 * bridge Job's explicit ChangeSet. The provider must complete immediately;
	 * saving is never performed. bApply=false always requests rollback preview.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString ExecuteOfficialTransactionalToolsetCall(
		const FString& ToolsetName,
		const FString& ToolName,
		const FString& JsonInput,
		const TArray<FString>& TargetPackages,
		bool bApply = false);

	/**
	 * Execute several audited immediate reads/mutations inside one ChangeSet.
	 * JsonCalls is an array of {toolset, tool, arguments} objects and must contain
	 * at least one TransactionalSync mutation. ReadOnly calls may be interleaved
	 * for before/after validation. Any policy, provider, or readback failure rolls
	 * the entire batch back; packages are never saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UE58",
		meta = (ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
			ToolProvider = "EpicToolsetRegistry", ToolEngineMin = "5.8.0", ToolIntroducedVersion = "3.0.0"))
	static FString ExecuteOfficialTransactionalToolsetBatch(
		const FString& JsonCalls,
		const TArray<FString>& TargetPackages,
		bool bApply = false,
		const FString& NiagaraSystemPathToCompile = TEXT(""),
		const FString& NiagaraUserParameterRenamesJson = TEXT("[]"));

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
