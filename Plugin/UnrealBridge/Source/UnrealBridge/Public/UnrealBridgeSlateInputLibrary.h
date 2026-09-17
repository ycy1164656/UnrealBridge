#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeSlateInputLibrary.generated.h"

/** Window/World-bound pointer routing. Runtime effects are deferred to Slate ticks. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeSlateInputLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|SlateInput",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString GetOwnedPIEWindowGeometry(const FString& RunId,const FString& WorldHandle);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|SlateInput",meta=(ToolRisk="RuntimeInteraction",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString SetOwnedPIEWindowGeometry(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|SlateInput",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="ExternalFile",ToolExecution="AsyncJob"))
	static FString CaptureOwnedPIEWindow(const FString& RunId,const FString& WorldHandle);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|SlateInput",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString GetWidgetInputGeometry(const FString& WorldHandle,const FString& WidgetPath,int32 LocalPlayerIndex);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|SlateInput",meta=(ToolRisk="RuntimeInteraction",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString SubmitPointerSequence(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|SlateInput",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString GetPointerSequenceState(const FString& OperationId);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|SlateInput",meta=(ToolRisk="RuntimeInteraction",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString CancelPointerSequence(const FString& OperationId,const FString& WorldHandle);
};

namespace BridgeSlateInput { void Shutdown(); }
