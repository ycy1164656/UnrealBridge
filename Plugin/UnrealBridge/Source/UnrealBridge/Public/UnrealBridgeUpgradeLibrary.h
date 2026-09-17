#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeUpgradeLibrary.generated.h"

/** Fresh, no-save scope/revision checks shared by incremental authoring APIs. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeUpgradeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** JSON array of at most 16 package names. Snapshot is bounded to 8 MiB per package. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Upgrade", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
		ToolEngineMin = "5.8.2", ToolIntroducedVersion = "3.1.0"))
	static FString GetAuthoringSnapshot(const FString& TargetPackagesJson);

	/** Validate unrealbridge.upgrade.v1 against current native state. Never applies or saves edits. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Upgrade", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never",
		ToolEngineMin = "5.8.2", ToolIntroducedVersion = "3.1.0"))
	static FString ValidateUpgradeRequest(const FString& RequestJson);
};
