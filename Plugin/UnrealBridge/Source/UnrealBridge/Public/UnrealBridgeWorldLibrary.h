#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeWorldLibrary.generated.h"

class AActor;
class UWorld;

/** Session-local identities and fail-closed world selection for editor jobs. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeWorldLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** JSON world descriptors. Handles expire on world cleanup or editor restart. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|World")
	static FString GetWorldContexts(int32 MaxWorlds = 64);

	/** JSON containing scope_token; scope lasts only within the current job slice. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|World")
	static FString BeginWorldScope(const FString& WorldHandle);

	/** End the most recent scope. Invalid nesting blocks selection for this slice. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|World")
	static FString EndWorldScope(const FString& ScopeToken);

	/** Resolve a unique actor in the selected world; returns a weak identity handle. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|World")
	static FString ResolveActorReference(const FString& WorldHandle, const FString& ActorNameOrPath);

	/** Validate identity, world membership, scope and expiry without loading objects. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|World")
	static FString ValidateActorReference(const FString& ActorHandle);
};

namespace BridgeWorldContext
{
	void Startup();
	void Shutdown();
	void BeginExecution();
	void EndExecution();
	bool HasScope();
	UWorld* GetScopedWorld();
	AActor* ResolveActor(const FString& Selector, UWorld* RequiredWorld = nullptr);
}
