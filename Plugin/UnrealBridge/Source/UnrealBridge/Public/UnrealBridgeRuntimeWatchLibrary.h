#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeRuntimeWatchLibrary.generated.h"

/** One bounded, read-only, same-PIE multi-world actor snapshot. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeRuntimeWatchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Request uses exact world handles, actor_name/class_path and up to 32 bounded fields. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|Runtime")
	static FString GetMultiWorldWatchSnapshot(const FString& RequestJson);
};
