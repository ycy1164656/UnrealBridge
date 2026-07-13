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
};
