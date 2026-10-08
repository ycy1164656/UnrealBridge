#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeValidationLibrary.generated.h"

/** Bounded, NeverSave UE validation for explicitly named assets and the current editor map. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeValidationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Request: asset_paths (exact object paths), max_assets (1..256). No folder sweep. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|Validation")
	static FString ValidateAssetsJson(const FString& RequestJson);

	/** Runs MAP CHECK DONTDISPLAYDIALOG on the current Editor World. Never saves. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|Validation")
	static FString MapCheckJson();
};
