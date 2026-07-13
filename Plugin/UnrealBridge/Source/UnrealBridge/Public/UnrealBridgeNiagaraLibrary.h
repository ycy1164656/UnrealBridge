#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeNiagaraLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeNiagaraSystemInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString ClassName;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraUserParameterInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString TypeName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	bool bIsDataInterface = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	bool bIsUObject = false;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraComponentInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString OwnerLabel;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FString SystemPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara")
	FVector Location = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraEmitterInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString HandleId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString SourceEmitterPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString Mode;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bEnabled = false;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraModuleInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString NodeGuid;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString FunctionName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString ScriptPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") int32 StackIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bEnabled = false;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraStackInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString EmitterName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString EmitterHandleId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString Usage;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString UsageId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString GraphPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") int32 TraversalNodeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") TArray<FBridgeNiagaraModuleInfo> Modules;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraSystemStructure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString SystemPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bReadyToRun = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bNeedsCompile = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") int32 TotalModuleCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") TArray<FBridgeNiagaraEmitterInfo> Emitters;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") TArray<FBridgeNiagaraStackInfo> Stacks;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") int32 ErrorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") int32 WarningCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") TArray<FString> Messages;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraEditResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bChanged = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bCompileRequested = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bSaved = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString SystemPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString EmitterHandleId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString NodeGuid;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString Message;
};

USTRUCT(BlueprintType)
struct FBridgeNiagaraCompileStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bComplete = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bReadyToRun = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") bool bNeedsCompile = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Niagara") FString Message;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeNiagaraLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static TArray<FBridgeNiagaraSystemInfo> ListNiagaraSystems(
		const FString& PackagePath = TEXT("/Game"),
		int32 MaxResults = 500);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static FString SpawnNiagaraSystemAtLocation(
		const FString& SystemPath,
		const FVector& Location,
		const FRotator& Rotation = FRotator::ZeroRotator,
		const FVector& Scale = FVector(1.0, 1.0, 1.0));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static TArray<FBridgeNiagaraUserParameterInfo> GetNiagaraUserParameters(const FString& SystemPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static TArray<FBridgeNiagaraComponentInfo> ListNiagaraComponents(const FString& ActorLabel = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static FBridgeNiagaraComponentInfo GetNiagaraComponentInfo(const FString& ComponentPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static bool SetNiagaraComponentVariableFloat(
		const FString& ComponentPath,
		const FString& VariableName,
		float Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static bool SetNiagaraComponentVariableBool(
		const FString& ComponentPath,
		const FString& VariableName,
		bool bValue);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static bool SetNiagaraComponentVariableVec3(
		const FString& ComponentPath,
		const FString& VariableName,
		const FVector& Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static bool SetNiagaraComponentVariableLinearColor(
		const FString& ComponentPath,
		const FString& VariableName,
		const FLinearColor& Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara")
	static bool SetNiagaraComponentVariableObject(
		const FString& ComponentPath,
		const FString& VariableName,
		const FString& ObjectPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeNiagaraSystemStructure GetNiagaraSystemStructure(const FString& SystemPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeNiagaraValidationResult ValidateNiagaraSystemGraph(const FString& SystemPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeNiagaraEditResult AddNiagaraEmitterToSystem(
		const FString& SystemPath,
		const FString& EmitterAssetPath,
		bool bRequestCompile = true,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeNiagaraEditResult AddNiagaraModuleToStack(
		const FString& SystemPath,
		const FString& EmitterHandleId,
		const FString& Usage,
		const FString& ModuleScriptPath,
		const FString& UsageId = TEXT(""),
		int32 TargetIndex = -1,
		bool bRequestCompile = true,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeNiagaraEditResult SetNiagaraModuleEnabled(
		const FString& SystemPath,
		const FString& NodeGuid,
		bool bEnabled,
		bool bRequestCompile = true,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeNiagaraEditResult RequestNiagaraSystemCompile(
		const FString& SystemPath,
		bool bForce = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Niagara", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "LongTaskPoll", ToolSaveBehavior = "Never"))
	static FBridgeNiagaraCompileStatus PollNiagaraSystemCompilation(const FString& SystemPath);
};
