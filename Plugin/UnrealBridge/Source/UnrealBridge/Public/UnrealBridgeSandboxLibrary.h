#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeSandboxLibrary.generated.h"

/** Editor-only FileSandbox adapter. Never exposes delete, revert or PersistAll. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeSandboxLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|Sandbox", meta=(ToolRisk="ReadOnly",
		ToolExecution="GameThreadShort", ToolSaveBehavior="Never", ToolEngineMin="5.8.2", ToolIntroducedVersion="3.2.0"))
	static FString GetSandboxStatus();

	/** Strict unrealbridge.sandbox.v1 envelope. Writes require exact session, owner, lease and package scope. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|Sandbox", meta=(ToolRisk="ExplicitOptIn",
		ToolExecution="GameThreadShort", ToolSaveBehavior="ExplicitTargets", ToolEngineMin="5.8.2", ToolIntroducedVersion="3.2.0"))
	static FString SandboxRequest(const FString& RequestJson);
};
