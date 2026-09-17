#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeNetworkSessionLibrary.generated.h"

/** Explicit PIE topology, isolated transient settings and exact session ownership. */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeNetworkSessionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Typed JSON unrealbridge.network_session.v2; refuses existing PIE/Dirty. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|NetworkSession")
	static FString StartNetworkSession(const FString& RequestJson);
	/** Read native session/world/player/NetDriver evidence. Never adopts existing PIE. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|NetworkSession")
	static FString GetNetworkSessionState(const FString& RunId);
	/** A sequential client ordinal and exact nonce are required; request is queued for next tick. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|NetworkSession")
	static FString JoinNetworkClient(const FString& RunId,const FString& ExpectedPIESession,const FString& RequestId,int32 RemoteClientOrdinal);
	/** Stops only this run's exact PIE session; returns stopping until worlds disappear. */
	UFUNCTION(BlueprintCallable, Category="UnrealBridge|NetworkSession")
	static FString StopOwnedNetworkSession(const FString& RunId,const FString& ExpectedPIESession,const FString& RequestId);
};
