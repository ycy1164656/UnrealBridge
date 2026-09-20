#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeEvidenceLibrary.generated.h"

/** Bounded capture of an explicitly scoped PIE viewport. Never captures the desktop or audio. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeEvidenceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|Evidence", meta=(ToolRisk="ExplicitOptIn", ToolExecution="GameThreadShort", ToolSaveBehavior="Never", ToolEngineMin="5.8.2", ToolIntroducedVersion="3.2.0"))
	static FString BeginViewportCapture(const FString& RequestJson);
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|Evidence", meta=(ToolRisk="ReadOnly", ToolExecution="GameThreadShort", ToolSaveBehavior="Never", ToolIntroducedVersion="3.2.0"))
	static FString GetViewportCapture(const FString& CaptureId);
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|Evidence", meta=(ToolRisk="ExplicitOptIn", ToolExecution="GameThreadShort", ToolSaveBehavior="Never", ToolIntroducedVersion="3.2.0"))
	static FString StopViewportCapture(const FString& CaptureId, const FString& OwnerId);
};

namespace BridgeEvidenceCapture { void Shutdown(); }
