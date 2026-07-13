#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUnrealBridgeServer;
class FBridgeDiscoveryService;
class FUnrealBridgeHttpServer;

class FUnrealBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<FUnrealBridgeServer> Server;
	TUniquePtr<FUnrealBridgeHttpServer> HttpServer;
	TUniquePtr<FBridgeDiscoveryService> Discovery;
};
