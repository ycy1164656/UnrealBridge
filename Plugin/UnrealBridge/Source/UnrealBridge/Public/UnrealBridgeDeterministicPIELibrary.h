#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeDeterministicPIELibrary.generated.h"

/** Task-owned fixed-delta PIE controller. One native request advances at most one frame. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeDeterministicPIELibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|PIE")
	static FString Begin(const FString& RunId, const FString& PIESessionId, const FString& OwnerId, double FixedDeltaSeconds);
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|PIE")
	static FString RequestOneFrame(const FString& ControllerId, const FString& OwnerId);
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|PIE")
	static FString Status(const FString& ControllerId, const FString& OwnerId);
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|PIE")
	static FString End(const FString& ControllerId, const FString& OwnerId);
};
