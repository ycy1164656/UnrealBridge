#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeFoliageLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeFoliageTypeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Foliage")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Foliage")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Foliage")
	FString ClassName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Foliage")
	FString SourcePath;
};

USTRUCT(BlueprintType)
struct FBridgeFoliageInstanceStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Foliage")
	FString FoliageTypePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Foliage")
	FString SourcePath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Foliage")
	int32 InstanceCount = 0;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeFoliageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Foliage")
	static TArray<FBridgeFoliageTypeInfo> ListFoliageTypes(
		const FString& PackagePath = TEXT("/Game"),
		int32 MaxResults = 500);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Foliage")
	static TArray<FBridgeFoliageInstanceStats> GetCurrentLevelFoliageStats();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Foliage")
	static int32 AddFoliageInstance(
		const FString& FoliageTypePath,
		const FVector& Location,
		const FRotator& Rotation = FRotator::ZeroRotator,
		const FVector& Scale = FVector(1.0, 1.0, 1.0));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Foliage")
	static int32 AddFoliageInstances(
		const FString& FoliageTypePath,
		const TArray<FTransform>& Transforms);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Foliage")
	static int32 RemoveFoliageInstancesInBox(
		const FString& FoliageTypePath,
		const FVector& BoundsMin,
		const FVector& BoundsMax);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Foliage")
	static int32 RemoveFoliageInstancesInSphere(
		const FString& FoliageTypePath,
		const FVector& Center,
		float Radius);
};
