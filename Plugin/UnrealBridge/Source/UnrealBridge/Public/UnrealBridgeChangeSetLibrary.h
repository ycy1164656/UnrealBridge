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
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> DirtyPackagesForJob;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> ProtectedPreexistingDirtyPackages;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|ChangeSet") TArray<FString> UnexpectedDirtyPackages;
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
	static FString BeginChangeSet(const FString& Name, const TArray<FString>& TargetPackages);

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
}
