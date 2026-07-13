#pragma once

#include "CoreMinimal.h"
#include "HttpRequestHandler.h"
#include "HttpRouteHandle.h"

class FJsonObject;
class FJsonValue;
class FUnrealBridgeServer;
class IHttpRouter;
struct FHttpServerRequest;

/**
 * Loopback-only HTTP and MCP facade over the existing durable JobManager.
 * Handlers never wait for Python execution; mutating work is submitted as a
 * Job and polled through bridge_get_job or the REST job endpoint.
 */
class FUnrealBridgeHttpServer
{
public:
	struct FStartConfig
	{
		int32 Port = 11438;
		FString Token;
	};

	explicit FUnrealBridgeHttpServer(TSharedPtr<FUnrealBridgeServer> InServer);
	~FUnrealBridgeHttpServer();

	bool Start(const FStartConfig& Config);
	void Stop();
	bool IsRunning() const { return bRunning; }
	int32 GetPort() const { return Port; }

private:
	struct FToolGroup
	{
		FString WrapperClass;
		FString FullLibraryName;
		TSharedPtr<FJsonObject> LibrarySchema;
	};

	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSubmitJob(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleListJobs(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleGetJob(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleCancelJob(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMcpPost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMcpGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	bool ValidateRequestSecurity(const FHttpServerRequest& Request, FString& OutError) const;
	bool ParseJsonBody(const FHttpServerRequest& Request, TSharedPtr<FJsonObject>& OutObject, FString& OutError) const;
	bool ValidateProtocolHeader(const FHttpServerRequest& Request, FString& OutError) const;
	void RefreshToolRegistry();

	TSharedRef<FJsonObject> BuildHealthObject() const;
	TSharedRef<FJsonObject> BuildCapabilitiesObject() const;
	TArray<TSharedPtr<FJsonValue>> BuildMcpTools() const;
	TSharedRef<FJsonObject> InvokeMcpTool(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		bool& bOutProtocolError,
		FString& OutProtocolError) const;
	TSharedRef<FJsonObject> SubmitScript(
		const FString& Script,
		const FString& RequestId,
		double QueueTimeout,
		const FString& IdempotencyKey,
		const FString& PollScript,
		double PollInterval,
		double RunTimeout,
		bool& bOutSuccess,
		FString& OutError) const;
	FString BuildGroupedCallScript(
		const FToolGroup& Group,
		const FString& Operation,
		const TSharedPtr<FJsonObject>& Arguments,
		const TSharedPtr<FJsonObject>& FunctionSchema) const;

	TSharedPtr<FUnrealBridgeServer> Server;
	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
	TMap<FString, FToolGroup> ToolGroups;
	FString Token;
	int32 Port = 0;
	bool bRunning = false;
};
