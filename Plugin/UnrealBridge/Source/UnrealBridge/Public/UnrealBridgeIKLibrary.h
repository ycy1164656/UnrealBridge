#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeIKLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeIKAssetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK")
	FString ClassName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK")
	TArray<FString> TopLevelProperties;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK")
	TMap<FString, FString> PropertyValues;
};

USTRUCT(BlueprintType)
struct FBridgeIKGoalInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString BoneName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") TArray<int32> ConnectedSolverIndices;
};

USTRUCT(BlueprintType)
struct FBridgeIKSolverInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString StructPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString StartBone;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString EndBone;
};

USTRUCT(BlueprintType)
struct FBridgeIKRetargetChainInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString StartBone;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString EndBone;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString GoalName;
};

USTRUCT(BlueprintType)
struct FBridgeIKRigStructure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString SkeletalMeshPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString RetargetRoot;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") TArray<FBridgeIKGoalInfo> Goals;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") TArray<FBridgeIKSolverInfo> Solvers;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") TArray<FBridgeIKRetargetChainInfo> Chains;
};

USTRUCT(BlueprintType)
struct FBridgeIKRetargetMappingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString TargetChain;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString SourceChain;
};

USTRUCT(BlueprintType)
struct FBridgeIKRetargeterStructure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString SourceIKRigPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString TargetIKRigPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString MappingOpName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") TArray<FBridgeIKRetargetMappingInfo> Mappings;
};

USTRUCT(BlueprintType)
struct FBridgeIKValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") int32 ErrorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") int32 WarningCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") TArray<FString> Messages;
};

USTRUCT(BlueprintType)
struct FBridgeIKEditResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|IK") FString Error;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeIKLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK")
	static TArray<FBridgeIKAssetInfo> ListIKRigAssets(
		const FString& PackagePath = TEXT("/Game"),
		int32 MaxResults = 500);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK")
	static TArray<FBridgeIKAssetInfo> ListIKRetargeterAssets(
		const FString& PackagePath = TEXT("/Game"),
		int32 MaxResults = 500);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK")
	static FString CreateIKRig(
		const FString& Path,
		const FString& Name,
		const FString& FactoryClassPath = TEXT("/Script/IKRigEditor.IKRigDefinitionFactory"),
		const FString& AssetClassPath = TEXT("/Script/IKRig.IKRigDefinition"),
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK")
	static FString CreateIKRetargeter(
		const FString& Path,
		const FString& Name,
		const FString& FactoryClassPath = TEXT("/Script/IKRigEditor.IKRetargetFactory"),
		const FString& AssetClassPath = TEXT("/Script/IKRig.IKRetargeter"),
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK")
	static FBridgeIKAssetInfo GetIKAssetInfo(
		const FString& AssetPath,
		int32 MaxPropertyValueLength = 4096);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK")
	static FString GetIKAssetProperty(
		const FString& AssetPath,
		const FString& PropertyName,
		bool& bOutSuccess);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK")
	static bool SetIKAssetProperty(
		const FString& AssetPath,
		const FString& PropertyName,
		const FString& ValueExportText,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeIKRigStructure GetIKRigStructure(const FString& IKRigPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeIKValidationResult ValidateIKRig(const FString& IKRigPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeIKEditResult AddIKRigGoal(
		const FString& IKRigPath,
		const FString& GoalName,
		const FString& BoneName,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeIKEditResult AddIKRigSolver(
		const FString& IKRigPath,
		const FString& SolverStructPath,
		const FString& StartBone = TEXT(""),
		const FString& EndBone = TEXT(""),
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeIKEditResult ConnectIKRigGoalToSolver(
		const FString& IKRigPath,
		const FString& GoalName,
		int32 SolverIndex,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeIKEditResult AddIKRigRetargetChain(
		const FString& IKRigPath,
		const FString& ChainName,
		const FString& StartBone,
		const FString& EndBone,
		const FString& GoalName = TEXT(""),
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeIKRetargeterStructure GetIKRetargeterStructure(
		const FString& RetargeterPath,
		const FString& MappingOpName = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeIKValidationResult ValidateIKRetargeter(
		const FString& RetargeterPath,
		const FString& MappingOpName = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeIKEditResult SetIKRetargeterRigs(
		const FString& RetargeterPath,
		const FString& SourceIKRigPath,
		const FString& TargetIKRigPath,
		bool bAddDefaultOps = true,
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeIKEditResult SetIKRetargetChainMapping(
		const FString& RetargeterPath,
		const FString& TargetChainName,
		const FString& SourceChainName,
		const FString& MappingOpName = TEXT(""),
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|IK", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FBridgeIKEditResult AutoMapIKRetargetChains(
		const FString& RetargeterPath,
		const FString& Mode = TEXT("Fuzzy"),
		bool bForceRemap = false,
		const FString& MappingOpName = TEXT(""),
		bool bSave = false);
};
