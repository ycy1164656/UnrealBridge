#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeSplineLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeSplineInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	FString ActorLabel;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	FString ActorPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	FString ComponentName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	int32 PointCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	bool bClosedLoop = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	float Length = 0.0f;
};

USTRUCT(BlueprintType)
struct FBridgeSplinePointInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	FVector Tangent = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	FVector Scale = FVector::OneVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Spline")
	FString PointType;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeSplineLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static TArray<FBridgeSplineInfo> ListSplineComponents(const FString& ActorLabel = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static FString AddSplineComponent(
		const FString& ActorLabel,
		const FString& ComponentName = TEXT("Spline"));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static TArray<FBridgeSplinePointInfo> GetSplinePoints(
		const FString& ActorLabel,
		const FString& ComponentName = TEXT(""),
		bool bWorldSpace = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static bool AddSplinePoint(
		const FString& ActorLabel,
		const FVector& Location,
		const FString& ComponentName = TEXT(""),
		int32 Index = -1,
		bool bWorldSpace = true,
		const FString& PointType = TEXT("Curve"),
		bool bUpdateSpline = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static bool SetSplinePointLocation(
		const FString& ActorLabel,
		int32 Index,
		const FVector& Location,
		const FString& ComponentName = TEXT(""),
		bool bWorldSpace = true,
		bool bUpdateSpline = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static bool SetSplinePointType(
		const FString& ActorLabel,
		int32 Index,
		const FString& PointType,
		const FString& ComponentName = TEXT(""),
		bool bUpdateSpline = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static bool RemoveSplinePoint(
		const FString& ActorLabel,
		int32 Index,
		const FString& ComponentName = TEXT(""),
		bool bUpdateSpline = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static bool ClearSplinePoints(
		const FString& ActorLabel,
		const FString& ComponentName = TEXT(""),
		bool bUpdateSpline = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Spline")
	static bool SetSplineClosedLoop(
		const FString& ActorLabel,
		bool bClosedLoop,
		const FString& ComponentName = TEXT(""),
		bool bUpdateSpline = true);
};
