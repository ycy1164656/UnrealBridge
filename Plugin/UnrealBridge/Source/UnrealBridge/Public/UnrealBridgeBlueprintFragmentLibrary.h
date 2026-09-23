#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeBlueprintFragmentLibrary.generated.h"

/**
 * Controlled export/import of native K2 graph fragments (the engine's own
 * clipboard representation), for reusing proven Blueprint logic.
 *
 * Scope boundaries this library deliberately keeps:
 *  - K2 event/function graphs only. Material, Niagara, AnimGraph, StateTree,
 *    BehaviorTree and UMG WidgetTree each need their own domain mechanism and
 *    are NOT covered by clipboard text.
 *  - Export and Inspect are read-only. Import mutates the target graph but
 *    never saves the package; saving stays with the caller's ChangeSet.
 *  - Fragment text is untrusted data. A SHA256 only proves byte identity, not
 *    that the contained logic is safe, authorized or correct.
 */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeBlueprintFragmentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Export selected nodes of one K2 graph as a native fragment.
	 *
	 * NodeGuids may be empty to export every node in the graph. Returns JSON:
	 * { ok, fragment_text, fragment_sha256, node_count, nodes[], blueprint, graph,
	 *   external_dependencies[], skipped_non_duplicable[] }. Each node carries its
	 *   full pins[] plus a pin_signature_sha256; comparing that signature against a
	 *   post-import readback is what proves dynamic pins and internal wiring survived.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|BlueprintFragment", meta = (
		ToolRisk = "ReadOnly",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Never",
		ToolSupportsIdempotency = "true",
		ToolIntroducedVersion = "3.2.1"))
	static FString ExportFragment(
		const FString& BlueprintPath,
		const FString& GraphName,
		const TArray<FString>& NodeGuids);

	/**
	 * Parse a fragment without importing it. Read-only; never touches a graph.
	 *
	 * Returns JSON: { ok, fragment_sha256, fragment_length, unresolved_classes[], basis }.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|BlueprintFragment", meta = (
		ToolRisk = "ReadOnly",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Never",
		ToolSupportsIdempotency = "true",
		ToolIntroducedVersion = "3.2.1"))
	static FString InspectFragment(const FString& FragmentText);

	/**
	 * Preflight one fragment against one target graph. Read-only.
	 *
	 * Reports engine acceptance (CanImportNodesFromText), unresolved classes and
	 * external dependencies that the target cannot currently bind. A pass here is
	 * a precondition, never an authorization or a completion.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|BlueprintFragment", meta = (
		ToolRisk = "ReadOnly",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Never",
		ToolSupportsIdempotency = "true",
		ToolIntroducedVersion = "3.2.1"))
	static FString PrepareImport(
		const FString& TargetBlueprintPath,
		const FString& TargetGraphName,
		const FString& FragmentText);

	/**
	 * Import a fragment into one target K2 graph.
	 *
	 * Marks the Blueprint structurally modified and optionally compiles it. The
	 * package is left dirty and is NEVER saved here. Returns JSON including the
	 * new node GUIDs so the caller can reconcile or undo a partial result.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|BlueprintFragment", meta = (
		ToolRisk = "Mutating",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Never",
		ToolSupportsIdempotency = "false",
		ToolIntroducedVersion = "3.2.1"))
	static FString ImportFragment(
		const FString& TargetBlueprintPath,
		const FString& TargetGraphName,
		const FString& FragmentText,
		bool bCompile = true);

	/**
	 * Structural readback of nodes in a graph, for comparing an import against
	 * its source fragment. NodeGuids may be empty to read the whole graph.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|BlueprintFragment", meta = (
		ToolRisk = "ReadOnly",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Never",
		ToolSupportsIdempotency = "true",
		ToolIntroducedVersion = "3.2.1"))
	static FString ReadbackFragment(
		const FString& BlueprintPath,
		const FString& GraphName,
		const TArray<FString>& NodeGuids);

	/** List the K2 graphs of one Blueprint, for choosing an export source or import target. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|BlueprintFragment", meta = (
		ToolRisk = "ReadOnly",
		ToolExecution = "GameThreadShort",
		ToolSaveBehavior = "Never",
		ToolSupportsIdempotency = "true",
		ToolIntroducedVersion = "3.2.1"))
	static FString ListFragmentGraphs(const FString& BlueprintPath);
};
