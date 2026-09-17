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

/** Generic editable StateTree node (evaluator, task, or condition). */
USTRUCT(BlueprintType)
struct FBridgeStateTreeNodeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Kind;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString StateId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString TransitionId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString StructPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString InstanceType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString ExpressionOperand;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") int32 ExpressionIndent = 0;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeParameterInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString StructId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString StateId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString StatePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString ValueType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString ContainerTypes;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString ValueTypeObject;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Value;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeBindingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString SourceStructId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString SourcePath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString TargetStructId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString TargetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString Description;
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
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeNodeInfo> Conditions;
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
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeNodeInfo> EnterConditions;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeTransitionInfo> Transitions;
};

USTRUCT(BlueprintType)
struct FBridgeStateTreeStructure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") FString SchemaClassPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeNodeInfo> Evaluators;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeNodeInfo> GlobalTasks;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeParameterInfo> Parameters;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree") TArray<FBridgeStateTreeBindingInfo> Bindings;
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

USTRUCT(BlueprintType)
struct FBridgeStateTreeRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") FString World;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") FString ComponentPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") FString OwnerPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") FString StateTreeReference;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") FString RunStatus;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") bool bRunning = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") bool bPaused = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") TArray<FString> ActiveStates;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|StateTree|Runtime") FString DebugInfo;
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

	/** Add a native evaluator struct to the global evaluator list. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeEvaluator(
		const FString& StateTreePath,
		const FString& EvaluatorStructPath,
		const FString& InstanceDataExportText = TEXT(""),
		bool bCompile = true,
		bool bSave = false);

	/** Add a native task struct to the global task list. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeGlobalTask(
		const FString& StateTreePath,
		const FString& TaskStructPath,
		const FString& InstanceDataExportText = TEXT(""),
		bool bCompile = true,
		bool bSave = false);

	/** Add a native enter-condition struct to one state. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeEnterCondition(
		const FString& StateTreePath,
		const FString& StateId,
		const FString& ConditionStructPath,
		const FString& InstanceDataExportText = TEXT(""),
		const FString& ExpressionOperand = TEXT("And"),
		int32 ExpressionIndent = 0,
		bool bCompile = true,
		bool bSave = false);

	/** Add a native condition struct to an existing transition. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeTransitionCondition(
		const FString& StateTreePath,
		const FString& TransitionId,
		const FString& ConditionStructPath,
		const FString& InstanceDataExportText = TEXT(""),
		const FString& ExpressionOperand = TEXT("And"),
		int32 ExpressionIndent = 0,
		bool bCompile = true,
		bool bSave = false);

	/** Add a typed public/root parameter, or a state parameter when StateId is provided. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeParameter(
		const FString& StateTreePath,
		const FString& StateId,
		const FString& Name,
		const FString& ValueType,
		const FString& ValueTypeObjectPath = TEXT(""),
		const FString& DefaultValueExportText = TEXT(""),
		bool bCompile = true,
		bool bSave = false);

	/** Add or replace an exact property binding between two existing bindable structs. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult AddStateTreeBinding(
		const FString& StateTreePath,
		const FString& SourceStructId,
		const FString& SourcePath,
		const FString& TargetStructId,
		const FString& TargetPath,
		bool bReplaceExisting = false,
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

	/** Set an instance-data property on any evaluator/task/condition by exact node GUID. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeStateTreeEditResult SetStateTreeNodeInstanceProperty(
		const FString& StateTreePath,
		const FString& NodeId,
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

	/** Read-only bounded snapshot of live GameplayStateTree components. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|StateTree|Runtime", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static TArray<FBridgeStateTreeRuntimeInfo> GetRuntimeStateTrees(int32 MaxComponents = 256);
};
