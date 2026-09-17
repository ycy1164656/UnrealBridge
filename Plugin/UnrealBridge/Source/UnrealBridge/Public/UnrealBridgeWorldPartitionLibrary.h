#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeWorldPartitionLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeWorldPartitionStreamingSourceInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FVector Velocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FString TargetState;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FString Priority;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FString TargetBehavior;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") TArray<FString> TargetGrids;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") int32 ShapeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") bool bBlockOnSlowLoading = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") bool bRemote = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") bool bReplay = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") bool bForce2D = false;
};

USTRUCT(BlueprintType)
struct FBridgeDataLayerRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString FullName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString ObjectPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString ParentPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString Type;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString InitialRuntimeState;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString RuntimeState;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") FString EffectiveRuntimeState;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") int32 ChildCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") int32 StreamingPriority = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") bool bRuntime = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") bool bClientOnly = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") bool bServerOnly = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") bool bVisibleInEditor = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") bool bLoadedInEditor = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|DataLayer") bool bEffectiveLoadedInEditor = false;
};

USTRUCT(BlueprintType)
struct FBridgeWorldPartitionIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|Validation") FString Severity;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|Validation") FString Code;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|Validation") FString Message;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition|Validation") FString ObjectPath;
};

USTRUCT(BlueprintType)
struct FBridgeWorldPartitionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FString World;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FString WorldPackage;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FString WorldType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") FString NetMode;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") int32 PIEInstance = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") bool bPartitioned = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") bool bInitialized = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") bool bStreamingCompleted = true;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") int32 TotalStreamingSourceCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") int32 TotalDataLayerCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") TArray<FBridgeWorldPartitionStreamingSourceInfo> StreamingSources;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") TArray<FBridgeDataLayerRuntimeInfo> DataLayers;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|WorldPartition") TArray<FBridgeWorldPartitionIssue> Issues;
};

/** Public-API-only World Partition, streaming-source, and Data Layer diagnostics. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeWorldPartitionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Capture editor and runtime/PIE worlds so multi-client streaming state can be compared. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|WorldPartition", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static TArray<FBridgeWorldPartitionSnapshot> GetWorldPartitionSnapshots(
		int32 MaxStreamingSourcesPerWorld = 256,
		int32 MaxDataLayersPerWorld = 4096);

	/** Return only bounded validation findings; does not change Data Layer runtime state. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|WorldPartition|Validation", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static TArray<FBridgeWorldPartitionIssue> ValidateWorldPartitionRuntime(
		bool bWarnWhenStreamingIncomplete = true,
		int32 MaxIssues = 2048);
};
