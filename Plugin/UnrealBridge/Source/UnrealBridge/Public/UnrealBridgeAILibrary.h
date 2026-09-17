#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeAILibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeBlackboardKeyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI")
	FString KeyType;
};

USTRUCT(BlueprintType)
struct FBridgeBehaviorTreeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI")
	FString RootNodeName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI")
	FString BlackboardPath;
};

USTRUCT(BlueprintType)
struct FBridgeBlackboardRuntimeValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	FString KeyType;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	FString Value;
};

USTRUCT(BlueprintType)
struct FBridgeBehaviorTreeRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	FString World;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	FString ComponentPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	FString OwnerPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	FString TreePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	FString ActiveNode;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	bool bRunning = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	bool bPaused = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Runtime")
	TArray<FBridgeBlackboardRuntimeValue> BlackboardValues;
};

USTRUCT(BlueprintType)
struct FBridgeEQSItemRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	float Score = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	FString ActorPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	FVector Location = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FBridgeEQSRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	FString World;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	FString WrapperPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	FString OwnerPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	int32 QueryId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	FString Status;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	FString ItemType;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	int32 TotalItemCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|EQS")
	TArray<FBridgeEQSItemRuntimeInfo> Items;
};

USTRUCT(BlueprintType)
struct FBridgePerceptionStimulusRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	FString TargetActorPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	FString SenseClass;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	bool bSuccessfullySensed = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	bool bExpired = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	float Age = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	float Strength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	FVector StimulusLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	FVector ReceiverLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	FName Tag;
};

USTRUCT(BlueprintType)
struct FBridgePerceptionRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	FString World;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	FString ComponentPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	FString OwnerPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	int32 TotalKnownActorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|Perception")
	TArray<FBridgePerceptionStimulusRuntimeInfo> Stimuli;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeAILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|AI|Authoring",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString GetBehaviorTreeEditModel(const FString& BehaviorTreePath);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|AI|Authoring",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString PreviewBehaviorTreeOps(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|AI|Authoring",meta=(ToolRisk="Mutating",ToolSaveBehavior="Optional",ToolExecution="GameThreadShort"))
	static FString ApplyBehaviorTreeOps(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|AI|Authoring",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString ValidateBehaviorTreeAsset(const FString& BehaviorTreePath);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|AI|Authoring",meta=(ToolRisk="Mutating",ToolSaveBehavior="Optional",ToolExecution="GameThreadShort"))
	static FString ApplyBlackboardKeyOps(const FString& RequestJson);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI")
	static FString CreateBehaviorTree(const FString& Path, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI")
	static FString CreateBlackboard(const FString& Path, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI")
	static bool SetBehaviorTreeBlackboard(const FString& BehaviorTreePath, const FString& BlackboardPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI")
	static bool AddBlackboardKey(
		const FString& BlackboardPath,
		const FString& KeyName,
		const FString& KeyType = TEXT("object"),
		const FString& BaseClassPath = TEXT(""),
		const FString& EnumPath = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI")
	static TArray<FBridgeBlackboardKeyInfo> GetBlackboardKeys(const FString& BlackboardPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI")
	static FBridgeBehaviorTreeInfo GetBehaviorTreeInfo(const FString& BehaviorTreePath);

	/** Read-only bounded snapshot of live PIE/game behavior trees and blackboards. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI|Runtime")
	static TArray<FBridgeBehaviorTreeRuntimeInfo> GetRuntimeBehaviorTrees(
		int32 MaxComponents = 128,
		int32 MaxBlackboardKeysPerComponent = 128);

	/** Read-only bounded snapshot of active/completed EQS wrapper results. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI|Runtime")
	static TArray<FBridgeEQSRuntimeInfo> GetRuntimeEQSQueries(
		int32 MaxQueries = 128,
		int32 MaxItemsPerQuery = 64);

	/** Read-only bounded snapshot of live perception listeners and stimuli. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI|Runtime")
	static TArray<FBridgePerceptionRuntimeInfo> GetRuntimePerception(
		int32 MaxComponents = 128,
		int32 MaxStimuliPerComponent = 256);
};
