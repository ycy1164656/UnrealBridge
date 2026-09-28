#pragma once
#include "CoreMinimal.h"

// Shared native trust boundary. Resolves Windows file handles, not lexical prefixes.
bool UnrealBridgeAuthorizeSourceFile(const FString& Source, FString& Resolved, FString& Error);
