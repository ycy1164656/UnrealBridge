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

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeAILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
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
};
