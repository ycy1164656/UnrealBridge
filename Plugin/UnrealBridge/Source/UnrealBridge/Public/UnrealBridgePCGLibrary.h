#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgePCGLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgePCGComponentEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString ActorLabel;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString ComponentName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString GraphPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bGenerated = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bGenerating = false;
};

USTRUCT(BlueprintType)
struct FBridgePCGComponentState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString GraphPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bGenerated = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bDirty = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bGenerating = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FBox GeneratedBounds = FBox(ForceInit);
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString LastGenerationIso;
};

USTRUCT(BlueprintType)
struct FBridgePCGOverrideEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString TypeStr;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString ValueStr;
};

USTRUCT(BlueprintType)
struct FBridgePCGWaitResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") float ElapsedMs = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString Note;
};

USTRUCT(BlueprintType)
struct FBridgePCGGenerationPollResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bComplete = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bGenerated = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString Status;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgePCGPinInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString Label;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") int64 AllowedTypes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bOutput = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bRequired = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") int32 ConnectionCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgePCGNodeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString Title;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString SettingsClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") int32 PositionX = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") int32 PositionY = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bInputNode = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bOutputNode = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bEnabled = true;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") TArray<FBridgePCGPinInfo> Pins;
};

USTRUCT(BlueprintType)
struct FBridgePCGEdgeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString FromNodeId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString FromPin;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString ToNodeId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString ToPin;
};

USTRUCT(BlueprintType)
struct FBridgePCGGraphInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") TArray<FBridgePCGNodeInfo> Nodes;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") TArray<FBridgePCGEdgeInfo> Edges;
};

USTRUCT(BlueprintType)
struct FBridgePCGGraphValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") int32 ErrorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") int32 WarningCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") TArray<FString> Messages;
};

USTRUCT(BlueprintType)
struct FBridgePCGGraphEditResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString NodeId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|PCG") FString Error;
};

/**
 * PCG inspection, graph editing, and asynchronous generation control.
 *
 * Hard contract (roadmap §5, §8):
	 *  - Topology edits use UPCGGraph's public API and never remove nodes.
	 *  - Whole library is gated to UE 5.6+. On 5.4-5.5 the UFUNCTIONs exist
 *    (UHT requires unconditional decls) but bodies live in the
 *    UnrealBridgePCGLibrary_Stubs.cpp and log a warning + return T{}.
 *  - Editor-world only — runtime/PIE PCG generation is PCG's own concern;
 *    bridge does not duplicate it.
	 *  - Wait operations are non-blocking polls. Client-side orchestration
	 *    sleeps between calls so PCG and the Editor keep ticking.
 */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgePCGLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** L3-1 — list UPCGGraph assets in the project, optionally filtered by short-name substring. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG")
	static TArray<FString> ListPCGGraphAssets(const FString& Filter, int32 Max = 200);

	/** L3-2 — list UPCGComponent on actors in the editor world. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG")
	static TArray<FBridgePCGComponentEntry> ListPCGComponentsInLevel(const FString& LevelFilter, int32 Max = 200);

	/** L3-3 — full state for one component on a level actor. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG")
	static FBridgePCGComponentState GetPCGComponentState(const FString& ActorLabel, const FString& ComponentName);

	/** L3-4 — list user-parameter values exposed by the component's graph instance. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG")
	static TArray<FBridgePCGOverrideEntry> GetPCGComponentOverrides(const FString& ActorLabel, const FString& ComponentName);

	/**
	 * L3-5 — set a single user-parameter override via Property->ImportText.
	 * `ExportedValue` must be in UE's standard exported form for the property's
	 * type (e.g. `42` for int, `(X=1, Y=2, Z=3)` for FVector, `"hello"` for FString).
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG")
	static bool SetPCGComponentOverride(const FString& ActorLabel, const FString& ComponentName, const FString& Name, const FString& ExportedValue);

	/**
	 * L3-6 — request asynchronous generation. Returns immediately after the
	 * request is queued; use `WaitForPCGGenerate` to block until done.
	 *
	 * @param bForce true → run even if not dirty.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG")
	static bool TriggerPCGGenerate(const FString& ActorLabel, const FString& ComponentName, bool bForce = false);

	/**
	 * L3-7 — block (with 50ms polling) until the component finishes generating
	 * or `TimeoutSec` elapses. Returns elapsed_ms + success flag.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG")
	static FBridgePCGWaitResult WaitForPCGGenerate(const FString& ActorLabel, const FString& ComponentName, float TimeoutSec = 60.0f);

	/** Non-blocking PCG generation poll; call repeatedly across Editor ticks. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "AsyncPoll", ToolSaveBehavior = "Never"))
	static FBridgePCGGenerationPollResult WaitPCGGeneration(
		const FString& ActorLabel,
		const FString& ComponentName);

	/** L3-8 — clean up generated content (purge actors / components tagged by this PCG generation). */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG")
	static bool CleanupPCGComponent(const FString& ActorLabel, const FString& ComponentName, bool bRemoveComponents = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgePCGGraphInfo GetPCGGraphStructure(const FString& GraphPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgePCGGraphValidationResult ValidatePCGGraph(const FString& GraphPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgePCGGraphEditResult AddPCGGraphNode(
		const FString& GraphPath,
		const FString& SettingsClassPath,
		const FString& NodeTitle,
		int32 PositionX,
		int32 PositionY,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgePCGGraphEditResult ConnectPCGGraphNodes(
		const FString& GraphPath,
		const FString& FromNodeId,
		const FString& FromPin,
		const FString& ToNodeId,
		const FString& ToPin,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgePCGGraphEditResult SetPCGNodeSettingsProperty(
		const FString& GraphPath,
		const FString& NodeId,
		const FString& PropertyName,
		const FString& ValueExportText,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|PCG", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgePCGGraphEditResult SetPCGNodeEnabled(
		const FString& GraphPath,
		const FString& NodeId,
		bool bEnabled,
		bool bSave = false);
};
