#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeStateTreeLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeStateTreeAssetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	FString ClassName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	TArray<FString> TopLevelProperties;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree")
	TMap<FString, FString> PropertyValues;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeTaskInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString StructPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString InstanceType;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeTransitionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Trigger;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString TransitionType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString TargetStateId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString TargetStateName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString RequiredEventTag;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeStateInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString ParentId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Path;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString StateType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString SelectionBehavior;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") bool bEnabled = true;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") int32 ChildCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeTaskInfo> Tasks;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeTransitionInfo> Transitions;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeStructure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString SchemaClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeStateInfo> States;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") bool bCompiled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") int32 ErrorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") int32 WarningCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FString> Messages;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeEditResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString CreatedId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Error;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FBridgeStateTreeValidationResult Validation;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeStateTreeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static TArray<FBridgeStateTreeAssetInfo> ListStateTreeAssets(
		const FString& PackagePath = TEXT("/Game"),
		int32 MaxResults = 500);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FString CreateStateTree(
		const FString& Path,
		const FString& Name,
		const FString& SchemaClassPath,
		const FString& FactoryClassPath = TEXT("/Script/StateTreeEditorModule.StateTreeFactory"),
		const FString& AssetClassPath = TEXT("/Script/StateTreeModule.StateTree"),
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FBridgeStateTreeAssetInfo GetStateTreeInfo(
		const FString& StateTreePath,
		int32 MaxPropertyValueLength = 4096);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static FString GetStateTreeProperty(
		const FString& StateTreePath,
		const FString& PropertyName,
		bool& bOutSuccess);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree")
	static bool SetStateTreeProperty(
		const FString& StateTreePath,
		const FString& PropertyName,
		const FString& ValueExportText,
		bool bCompile = true,
		bool bSave = false);

	/** Read the complete editable state hierarchy without mutating the asset. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeStateTreeStructure GetStateTreeStructure(const FString& StateTreePath);

	/** Validate and compile a transient duplicate so this read operation never dirties the source asset. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeStateTreeValidationResult ValidateStateTreeAsset(const FString& StateTreePath);

	/** Add a top-level subtree when ParentStateId is empty, otherwise add a child state. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeState(
		const FString& StateTreePath,
		const FString& ParentStateId,
		const FString& Name,
		const FString& StateType = TEXT("State"),
		bool bCompile = true,
		bool bSave = false);

	/** Add a native StateTree task struct and optionally import its instance struct value. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeTask(
		const FString& StateTreePath,
		const FString& StateId,
		const FString& TaskStructPath,
		const FString& InstanceDataExportText = TEXT(""),
		bool bCompile = true,
		bool bSave = false);

	/** Add a transition. TargetStateId is required only for GotoState. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeTransition(
		const FString& StateTreePath,
		const FString& SourceStateId,
		const FString& Trigger,
		const FString& TransitionType,
		const FString& TargetStateId = TEXT(""),
		const FString& RequiredEventTag = TEXT(""),
		bool bCompile = true,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult SetStateTreeStateEnabled(
		const FString& StateTreePath,
		const FString& StateId,
		bool bEnabled,
		bool bCompile = true,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult SetStateTreeTaskInstanceProperty(
		const FString& StateTreePath,
		const FString& TaskId,
		const FString& PropertyName,
		const FString& ValueExportText,
		bool bCompile = true,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult SetStateTreeTransitionEnabled(
		const FString& StateTreePath,
		const FString& TransitionId,
		bool bEnabled,
		bool bCompile = true,
		bool bSave = false);
};
