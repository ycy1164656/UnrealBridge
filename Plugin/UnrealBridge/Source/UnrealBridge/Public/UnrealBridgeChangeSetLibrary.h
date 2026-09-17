#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeChangeSetLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeChangeSetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") FString ChangeSetId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") FString JobId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") FString Status;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") bool bCanCommit = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") bool bSaved = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> TargetPackages;
	/** Target packages that were already Dirty before this change set began. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> TargetPackagesDirtyAtBegin;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> DirtyPackagesForJob;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> ProtectedPreexistingDirtyPackages;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> UnexpectedDirtyPackages;
	/** New top-level assets created in packages that did not exist when the change set began. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> CreatedAssetsForJob;
	/** Unsaved newly-created assets explicitly removed from the Asset Registry during rollback. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> RemovedCreatedAssetsDuringRollback;
	/** Existing transactional objects whose pre-change state was captured for strict Undo. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") int32 CapturedObjectsForUndo = 0;
	/** Clean existing packages that require a post-Undo reload to restore provider caches. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> ReloadPackagesOnRollback;
	/** Packages successfully reloaded after Undo. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> ReloadedPackagesDuringRollback;
	/** True only after Undo and target-package/Asset Registry state have been checked. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") bool bRollbackVerified = false;
	/** Guarded opt-in retains failed changes unsaved instead of automatically undoing them. */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") bool bRetainOnFailure = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") FString Error;
};

/** Transaction, diff, rollback, and bounded-save control for the current bridge Job. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeChangeSetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Configure the current Job's automatic transaction as an explicit change set. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|ChangeSet", meta = (
		ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FString BeginChangeSet(
		const FString& Name,
		const TArray<FString>& TargetPackages,
		bool bReloadCleanTargetsOnRollback = false);

	/** New authoring path: retain failures for review. Existing BeginChangeSet semantics are unchanged. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|ChangeSet", meta = (
		ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
		ToolEngineMin = "5.8.2", ToolIntroducedVersion = "3.1.0"))
	static FString BeginGuardedChangeSet(const FString& Name, const TArray<FString>& TargetPackages);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|ChangeSet", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeChangeSetInfo PreviewChangeSet(const FString& ChangeSetId);

	/** Commit the current Job transaction. Save only the declared/owned package set when bSave is true. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|ChangeSet", meta = (
		ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Optional"))
	static FBridgeChangeSetInfo CommitChangeSet(const FString& ChangeSetId, bool bSave = false);

	/** Undo only when the top transaction is the current Job's uniquely-titled transaction. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|ChangeSet", meta = (
		ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeChangeSetInfo RollbackChangeSet(const FString& ChangeSetId);

	/** Immediately commit-unsaved or roll back the active explicit ChangeSet. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|ChangeSet", meta = (
		ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeChangeSetInfo FinalizeChangeSet(
		const FString& ChangeSetId,
		bool bApply = false);

	/**
	 * Undo a previously committed, unsaved bridge change set only when its uniquely
	 * titled transaction is still the top entry in the Editor undo buffer.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|ChangeSet", meta = (
		ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeChangeSetInfo RollbackCommittedChangeSet(const FString& ChangeSetId);

	/** Return packages attributed to an active or recently completed Job. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|ChangeSet", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static TArray<FString> GetDirtyPackagesForJob(const FString& JobId);
};

namespace BridgeChangeSetRuntime
{
	/** Called by the server immediately before/after Python execution on the GameThread. */
	UNREALBRIDGE_API void BeginJob(const FString& JobId);
	UNREALBRIDGE_API void EndJob(const FString& JobId, bool bPythonSuccess);

	/** Finalize an official immediate adapter before it returns its evidence payload. */
	UNREALBRIDGE_API FBridgeChangeSetInfo RollbackNow(
		const FString& ChangeSetId,
		const FString& Reason);
	UNREALBRIDGE_API FBridgeChangeSetInfo CommitNow(
		const FString& ChangeSetId,
		bool bSave);
}
