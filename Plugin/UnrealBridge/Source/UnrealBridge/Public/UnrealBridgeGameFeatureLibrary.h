#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeGameFeatureLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeGameFeatureDependencyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString PluginName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString PluginUrl;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString State;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	bool bShouldActivate = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	bool bResolved = false;
};

USTRUCT(BlueprintType)
struct FBridgeGameFeatureActionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString ObjectPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString ClassPath;
};

USTRUCT(BlueprintType)
struct FBridgeGameFeatureValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString Severity;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString Message;
};

USTRUCT(BlueprintType)
struct FBridgeGameFeatureInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString PluginName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString PluginUrl;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString DescriptorFile;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString State;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString GameFeatureDataPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	bool bEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	bool bInErrorState = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	bool bDescriptorDetailsAvailable = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	FString ValidationResult;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	TArray<FBridgeGameFeatureDependencyInfo> Dependencies;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	TArray<FBridgeGameFeatureActionInfo> Actions;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|GameFeature")
	TArray<FBridgeGameFeatureValidationIssue> ValidationIssues;
};

/** Stable, read-only GameFeature diagnostics for dependency/state/action audits. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeGameFeatureLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|GameFeature")
	static TArray<FBridgeGameFeatureInfo> ListGameFeatures(
		bool bIncludeDisabled = true,
		bool bRunDataValidation = true,
		int32 MaxPlugins = 512);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|GameFeature")
	static FBridgeGameFeatureInfo GetGameFeatureInfo(
		const FString& PluginNameOrUrl,
		bool bRunDataValidation = true);
};
