#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeNetworkingLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeActorNetworkingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	FString ActorLabel;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	FString ActorPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	bool bReplicates = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	bool bReplicateMovement = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	bool bAlwaysRelevant = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	FString NetDormancy;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	float NetUpdateFrequency = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	float MinNetUpdateFrequency = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	float NetPriority = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking")
	float NetCullDistanceSquared = 0.0f;
};

USTRUCT(BlueprintType)
struct FBridgeReplicatedPropertyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString OwnerClass;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString CppType;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString RepNotifyFunction;
};

USTRUCT(BlueprintType)
struct FBridgeRPCFunctionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString OwnerClass;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString Direction;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	bool bReliable = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	bool bWithValidation = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	TArray<FString> Parameters;
};

USTRUCT(BlueprintType)
struct FBridgeNetworkClassAudit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString ClassPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	int32 TotalReplicatedPropertyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	int32 TotalRPCCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	TArray<FBridgeReplicatedPropertyInfo> ReplicatedProperties;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	TArray<FBridgeRPCFunctionInfo> RPCs;
};

USTRUCT(BlueprintType)
struct FBridgeNetworkActorRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString ActorPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString ClassPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString OwnerPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString LocalRole;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString RemoteRole;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString Dormancy;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString NetConnectionPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	bool bReplicates = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	bool bTearOff = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	float NetUpdateFrequency = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	float MinNetUpdateFrequency = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	float NetPriority = 0.0f;
};

USTRUCT(BlueprintType)
struct FBridgeNetworkActorAudit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString World;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString WorldType;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FString NetMode;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FBridgeActorNetworkingInfo Settings;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FBridgeNetworkActorRuntimeInfo Runtime;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Audit")
	FBridgeNetworkClassAudit ClassAudit;
};

USTRUCT(BlueprintType)
struct FBridgeNetworkWorldSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString World;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString WorldType;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString NetMode;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	FString NetDriverPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	int32 PIEInstance = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	int32 TotalActorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	int32 TotalReplicatedActorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Networking|Runtime")
	TArray<FBridgeNetworkActorRuntimeInfo> Actors;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeNetworkingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking")
	static FBridgeActorNetworkingInfo GetActorNetworkingInfo(const FString& ActorNameOrLabel);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking")
	static bool SetActorReplicates(const FString& ActorNameOrLabel, bool bReplicates);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking")
	static bool SetActorReplicateMovement(const FString& ActorNameOrLabel, bool bReplicateMovement);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking")
	static bool SetActorAlwaysRelevant(const FString& ActorNameOrLabel, bool bAlwaysRelevant);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking")
	static bool SetActorNetDormancy(const FString& ActorNameOrLabel, const FString& Dormancy);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking")
	static bool SetActorNetUpdateSettings(
		const FString& ActorNameOrLabel,
		float NetUpdateFrequency,
		float MinNetUpdateFrequency = -1.0f,
		float NetPriority = -1.0f,
		float NetCullDistanceSquared = -1.0f);

	/** Reflect replicated properties and RPC declarations without mutating a class. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking|Audit")
	static FBridgeNetworkClassAudit AuditNetworkClass(
		const FString& ClassPath,
		int32 MaxProperties = 2048,
		int32 MaxRPCs = 1024);

	/** Combine class replication metadata with one live/editor actor's current settings. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking|Audit")
	static FBridgeNetworkActorAudit AuditNetworkActor(
		const FString& ActorNameOrPath,
		int32 MaxProperties = 2048,
		int32 MaxRPCs = 1024);

	/** Bounded multi-world sample for server/client PIE comparison. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Networking|Runtime")
	static TArray<FBridgeNetworkWorldSnapshot> GetNetworkWorldSnapshots(
		bool bReplicatedActorsOnly = true,
		int32 MaxActorsPerWorld = 4096);
};
