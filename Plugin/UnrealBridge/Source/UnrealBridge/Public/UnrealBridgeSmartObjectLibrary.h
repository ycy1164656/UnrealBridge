#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeSmartObjectLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeSmartObjectSlotRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString Handle;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString State;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	bool bHasLocation = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FVector Location = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FBridgeSmartObjectRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString World;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString ComponentPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString OwnerPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString DefinitionPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString Handle;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString RegistrationType;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FString InstanceTags;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	bool bRegistered = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	bool bBoundToSimulation = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	bool bEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FVector BoundsCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	FVector BoundsExtent = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	int32 TotalSlotCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|AI|SmartObject")
	TArray<FBridgeSmartObjectSlotRuntimeInfo> Slots;
};

/** Read-only Smart Object component, registration, slot and tag inspection. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeSmartObjectLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|AI|SmartObject")
	static TArray<FBridgeSmartObjectRuntimeInfo> GetRuntimeSmartObjects(
		int32 MaxComponents = 512,
		int32 MaxSlotsPerComponent = 128,
		bool bRuntimeWorldsOnly = true);
};
