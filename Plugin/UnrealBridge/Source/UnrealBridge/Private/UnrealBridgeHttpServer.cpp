#include "UnrealBridgeHttpServer.h"

#include "UnrealBridgeRegistryLibrary.h"
#include "UnrealBridgeServer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpPath.h"
#include "HttpServerConstants.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "IPAddress.h"
#include "Misc/Base64.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealBridgeHttp, Log, All);

namespace BridgeHttp
{
	constexpr int32 MaxRequestBytes = 1024 * 1024;
	constexpr const TCHAR* McpProtocolVersion = TEXT("2025-11-25");

	FString SerializeObject(const TSharedRef<FJsonObject>& Object)
	{
		FString Result;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
		FJsonSerializer::Serialize(Object, Writer);
		return Result;
	}

	TUniquePtr<FHttpServerResponse> JsonResponse(
		const TSharedRef<FJsonObject>& Object,
		EHttpServerResponseCodes Code = EHttpServerResponseCodes::Ok)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(
			SerializeObject(Object), TEXT("application/json; charset=utf-8"));
		Response->Code = Code;
		Response->Headers.FindOrAdd(TEXT("Cache-Control")).Add(TEXT("no-store"));
		Response->Headers.FindOrAdd(TEXT("X-Content-Type-Options")).Add(TEXT("nosniff"));
		Response->Headers.FindOrAdd(TEXT("MCP-Protocol-Version")).Add(McpProtocolVersion);
		return Response;
	}

	TUniquePtr<FHttpServerResponse> EmptyResponse(EHttpServerResponseCodes Code)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(
			FString(), TEXT("application/json; charset=utf-8"));
		Response->Code = Code;
		Response->Headers.FindOrAdd(TEXT("Cache-Control")).Add(TEXT("no-store"));
		Response->Headers.FindOrAdd(TEXT("X-Content-Type-Options")).Add(TEXT("nosniff"));
		return Response;
	}

	TUniquePtr<FHttpServerResponse> ErrorResponse(
		EHttpServerResponseCodes Code,
		const FString& ErrorCode,
		const FString& Message)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("code"), ErrorCode);
		Error->SetStringField(TEXT("message"), Message);
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetObjectField(TEXT("error"), Error);
		TUniquePtr<FHttpServerResponse> Response = JsonResponse(Root, Code);
		if (Code == EHttpServerResponseCodes::Denied)
		{
			Response->Headers.FindOrAdd(TEXT("WWW-Authenticate")).Add(TEXT("Bearer realm=\"UnrealBridge\""));
		}
		return Response;
	}

	bool GetHeader(const FHttpServerRequest& Request, const FString& Name, FString& OutValue)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && !Pair.Value.IsEmpty())
			{
				OutValue = Pair.Value[0];
				return true;
			}
		}
		return false;
	}

	bool ConstantTimeEquals(const FString& Expected, const FString& Actual)
	{
		uint32 Difference = static_cast<uint32>(Expected.Len() ^ Actual.Len());
		const int32 ComparedLength = FMath::Min(Expected.Len(), Actual.Len());
		for (int32 Index = 0; Index < ComparedLength; ++Index)
		{
			Difference |= static_cast<uint32>(Expected[Index] ^ Actual[Index]);
		}
		return Difference == 0;
	}

	bool IsLoopbackPeer(const FHttpServerRequest& Request)
	{
		if (!Request.PeerAddress.IsValid())
		{
			return false;
		}
		const TArray<uint8> Raw = Request.PeerAddress->GetRawIp();
		if (Raw.Num() == 4 && Raw[0] == 127)
		{
			return true;
		}
		if (Raw.Num() == 16)
		{
			bool bIpv6Loopback = Raw[15] == 1;
			for (int32 Index = 0; Index < 15 && bIpv6Loopback; ++Index)
			{
				bIpv6Loopback = Raw[Index] == 0;
			}
			if (bIpv6Loopback)
			{
				return true;
			}
		}
		const FString Address = Request.PeerAddress->ToString(false);
		return Address.StartsWith(TEXT("127.")) || Address == TEXT("::1") || Address == TEXT("[::1]");
	}

	bool IsAllowedOrigin(const FString& Origin)
	{
		if (Origin.IsEmpty())
		{
			return true;
		}
		const FString Lower = Origin.ToLower();
		for (const TCHAR* Prefix : {
			TEXT("http://localhost"), TEXT("https://localhost"),
			TEXT("http://127.0.0.1"), TEXT("https://127.0.0.1"),
			TEXT("http://[::1]"), TEXT("https://[::1]")})
		{
			const FString Allowed(Prefix);
			if (Lower == Allowed || Lower.StartsWith(Allowed + TEXT(":")))
			{
				return true;
			}
		}
		return false;
	}

	FString CamelToSnake(const FString& Value)
	{
		FString Result;
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			const TCHAR Ch = Value[Index];
			if (FChar::IsUpper(Ch) && Index > 0)
			{
				const TCHAR Previous = Value[Index - 1];
				const bool bNextLower = Index + 1 < Value.Len() && FChar::IsLower(Value[Index + 1]);
				if (FChar::IsLower(Previous) || FChar::IsDigit(Previous)
					|| (FChar::IsUpper(Previous) && bNextLower))
				{
					Result.AppendChar(TEXT('_'));
				}
			}
			Result.AppendChar(FChar::ToLower(Ch));
		}
		return Result;
	}

	TSharedRef<FJsonObject> EmptyObjectSchema()
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		Schema->SetBoolField(TEXT("additionalProperties"), false);
		return Schema;
	}

	TSharedRef<FJsonObject> StringSchema(const FString& Description = FString())
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("string"));
		if (!Description.IsEmpty())
		{
			Schema->SetStringField(TEXT("description"), Description);
		}
		return Schema;
	}

	TSharedRef<FJsonObject> NumberSchema(double Minimum, double Maximum, double DefaultValue)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("number"));
		Schema->SetNumberField(TEXT("minimum"), Minimum);
		Schema->SetNumberField(TEXT("maximum"), Maximum);
		Schema->SetNumberField(TEXT("default"), DefaultValue);
		return Schema;
	}

	TSharedRef<FJsonObject> MakeTool(
		const FString& Name,
		const FString& Description,
		const TSharedRef<FJsonObject>& InputSchema)
	{
		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"), Name);
		Tool->SetStringField(TEXT("description"), Description);
		Tool->SetObjectField(TEXT("inputSchema"), InputSchema);
		return Tool;
	}

	TSharedRef<FJsonObject> MakeObjectSchema(
		const TSharedRef<FJsonObject>& Properties,
		const TArray<FString>& Required,
		bool bAdditionalProperties = false)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), Properties);
		TArray<TSharedPtr<FJsonValue>> RequiredValues;
		for (const FString& Name : Required)
		{
			RequiredValues.Add(MakeShared<FJsonValueString>(Name));
		}
		Schema->SetArrayField(TEXT("required"), RequiredValues);
		Schema->SetBoolField(TEXT("additionalProperties"), bAdditionalProperties);
		return Schema;
	}

	TSharedRef<FJsonObject> MakeToolResult(
		const TSharedRef<FJsonObject>& Structured,
		bool bIsError,
		const FString& OverrideText = FString())
	{
		const FString Text = OverrideText.IsEmpty() ? SerializeObject(Structured) : OverrideText;
		TSharedRef<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), Text);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(TextContent)});
		Result->SetObjectField(TEXT("structuredContent"), Structured);
		Result->SetBoolField(TEXT("isError"), bIsError);
		return Result;
	}

	TSharedRef<FJsonObject> MakeToolError(const FString& Code, const FString& Message)
	{
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), false);
		Data->SetStringField(TEXT("error_code"), Code);
		Data->SetStringField(TEXT("error"), Message);
		return MakeToolResult(Data, true, Message);
	}

	TSharedRef<FJsonObject> MakeJsonRpcResponse(
		const TSharedPtr<FJsonValue>& Id,
		const TSharedRef<FJsonObject>& Result)
	{
		TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Response->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
		Response->SetObjectField(TEXT("result"), Result);
		return Response;
	}

	TSharedRef<FJsonObject> MakeJsonRpcError(
		const TSharedPtr<FJsonValue>& Id,
		int32 Code,
		const FString& Message)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);
		TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Response->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
		Response->SetObjectField(TEXT("error"), Error);
		return Response;
	}

	bool JsonTypeMatches(const TSharedPtr<FJsonValue>& Value, const FString& ExpectedType)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (ExpectedType == TEXT("null")) return Value->Type == EJson::Null;
		if (ExpectedType == TEXT("boolean")) return Value->Type == EJson::Boolean;
		if (ExpectedType == TEXT("number")) return Value->Type == EJson::Number;
		if (ExpectedType == TEXT("integer"))
		{
			return Value->Type == EJson::Number
				&& FMath::IsNearlyEqual(Value->AsNumber(), FMath::RoundToDouble(Value->AsNumber()));
		}
		if (ExpectedType == TEXT("string")) return Value->Type == EJson::String;
		if (ExpectedType == TEXT("array")) return Value->Type == EJson::Array;
		if (ExpectedType == TEXT("object")) return Value->Type == EJson::Object;
		return true;
	}

	bool ValidateValueAgainstSchema(
		const TSharedPtr<FJsonValue>& Value,
		const TSharedPtr<FJsonObject>& Schema,
		const FString& Path,
		FString& OutError)
	{
		if (!Schema.IsValid())
		{
			return true;
		}

		TArray<FString> ExpectedTypes;
		FString SingleType;
		if (Schema->TryGetStringField(TEXT("type"), SingleType))
		{
			ExpectedTypes.Add(SingleType);
		}
		else
		{
			const TArray<TSharedPtr<FJsonValue>>* TypeValues = nullptr;
			if (Schema->TryGetArrayField(TEXT("type"), TypeValues) && TypeValues)
			{
				for (const TSharedPtr<FJsonValue>& TypeValue : *TypeValues)
				{
					if (TypeValue.IsValid() && TypeValue->Type == EJson::String)
					{
						ExpectedTypes.Add(TypeValue->AsString());
					}
				}
			}
		}

		if (!ExpectedTypes.IsEmpty())
		{
			bool bMatches = false;
			for (const FString& Expected : ExpectedTypes)
			{
				bMatches |= JsonTypeMatches(Value, Expected);
			}
			if (!bMatches)
			{
				OutError = FString::Printf(TEXT("%s has the wrong JSON type; expected %s"),
					*Path, *FString::Join(ExpectedTypes, TEXT(" or ")));
				return false;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* EnumValues = nullptr;
		if (Schema->TryGetArrayField(TEXT("enum"), EnumValues) && EnumValues)
		{
			bool bFound = false;
			for (const TSharedPtr<FJsonValue>& EnumValue : *EnumValues)
			{
				if (Value->Type == EJson::String && EnumValue->Type == EJson::String
					&& Value->AsString() == EnumValue->AsString())
				{
					bFound = true;
					break;
				}
				if (Value->Type == EJson::Number && EnumValue->Type == EJson::Number
					&& FMath::IsNearlyEqual(Value->AsNumber(), EnumValue->AsNumber()))
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				OutError = FString::Printf(TEXT("%s is not an allowed enum value"), *Path);
				return false;
			}
		}

		if (Value->Type == EJson::Array)
		{
			const TSharedPtr<FJsonObject>* ItemSchema = nullptr;
			if (Schema->TryGetObjectField(TEXT("items"), ItemSchema) && ItemSchema)
			{
				const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
				for (int32 Index = 0; Index < Items.Num(); ++Index)
				{
					if (!ValidateValueAgainstSchema(Items[Index], *ItemSchema,
						FString::Printf(TEXT("%s[%d]"), *Path, Index), OutError))
					{
						return false;
					}
				}
			}
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			Schema->TryGetObjectField(TEXT("properties"), Properties);
			const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
			if (Schema->TryGetArrayField(TEXT("required"), Required) && Required)
			{
				for (const TSharedPtr<FJsonValue>& RequiredName : *Required)
				{
					if (RequiredName.IsValid() && RequiredName->Type == EJson::String
						&& !Object->HasField(RequiredName->AsString()))
					{
						OutError = FString::Printf(TEXT("%s is missing required field '%s'"),
							*Path, *RequiredName->AsString());
						return false;
					}
				}
			}

			bool bAdditionalProperties = true;
			Schema->TryGetBoolField(TEXT("additionalProperties"), bAdditionalProperties);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
			{
				const TSharedPtr<FJsonObject>* PropertySchema = nullptr;
				if (Properties && Properties->IsValid()
					&& (*Properties)->TryGetObjectField(Pair.Key, PropertySchema) && PropertySchema)
				{
					if (!ValidateValueAgainstSchema(Pair.Value, *PropertySchema,
						Path + TEXT(".") + Pair.Key, OutError))
					{
						return false;
					}
				}
				else if (!bAdditionalProperties)
				{
					OutError = FString::Printf(TEXT("%s has unexpected field '%s'"), *Path, *Pair.Key);
					return false;
				}
			}
		}

		return true;
	}
}

FUnrealBridgeHttpServer::FUnrealBridgeHttpServer(TSharedPtr<FUnrealBridgeServer> InServer)
	: Server(MoveTemp(InServer))
{
}

FUnrealBridgeHttpServer::~FUnrealBridgeHttpServer()
{
	Stop();
}

bool FUnrealBridgeHttpServer::Start(const FStartConfig& Config)
{
	check(IsInGameThread());
	if (bRunning)
	{
		return true;
	}
	if (!Server.IsValid() || Config.Port <= 0 || Config.Port > 65535 || Config.Token.IsEmpty())
	{
		return false;
	}

	Port = Config.Port;
	Token = Config.Token;
	FHttpServerModule& HttpModule = FHttpServerModule::Get();
	HttpModule.StartAllListeners();
	Router = HttpModule.GetHttpRouter(static_cast<uint32>(Port), true);
	if (!Router.IsValid())
	{
		Port = 0;
		Token.Reset();
		return false;
	}

	auto Bind = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, FHttpRequestHandler Handler)
	{
		FHttpRouteHandle Handle = Router->BindRoute(FHttpPath(Path), Verbs, Handler);
		if (Handle.IsValid())
		{
			RouteHandles.Add(Handle);
			return true;
		}
		return false;
	};

	const bool bBound =
		Bind(TEXT("/unrealbridge/health"), EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateRaw(this, &FUnrealBridgeHttpServer::HandleHealth))
		&& Bind(TEXT("/unrealbridge/jobs"),
			EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& Complete)
			{
				return Request.Verb == EHttpServerRequestVerbs::VERB_GET
					? HandleListJobs(Request, Complete)
					: HandleSubmitJob(Request, Complete);
			}))
		&& Bind(TEXT("/unrealbridge/jobs/:job_id"), EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateRaw(this, &FUnrealBridgeHttpServer::HandleGetJob))
		&& Bind(TEXT("/unrealbridge/jobs/:job_id/cancel"), EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateRaw(this, &FUnrealBridgeHttpServer::HandleCancelJob))
		&& Bind(TEXT("/mcp"),
			EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& Complete)
			{
				return Request.Verb == EHttpServerRequestVerbs::VERB_GET
					? HandleMcpGet(Request, Complete)
					: HandleMcpPost(Request, Complete);
			}));

	if (!bBound)
	{
		Stop();
		return false;
	}

	RefreshToolRegistry();
	bRunning = true;
	UE_LOG(LogUnrealBridgeHttp, Log,
		TEXT("Loopback HTTP MCP listening on 127.0.0.1:%d (%d grouped libraries)"),
		Port, ToolGroups.Num());
	return true;
}

void FUnrealBridgeHttpServer::Stop()
{
	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			if (Handle.IsValid())
			{
				Router->UnbindRoute(Handle);
			}
		}
	}
	RouteHandles.Reset();
	Router.Reset();
	ToolGroups.Reset();
	Token.Reset();
	Port = 0;
	bRunning = false;
}

bool FUnrealBridgeHttpServer::ValidateRequestSecurity(
	const FHttpServerRequest& Request,
	FString& OutError) const
{
	if (!BridgeHttp::IsLoopbackPeer(Request))
	{
		OutError = TEXT("HTTP MCP accepts loopback clients only");
		return false;
	}

	FString Origin;
	BridgeHttp::GetHeader(Request, TEXT("Origin"), Origin);
	if (!BridgeHttp::IsAllowedOrigin(Origin))
	{
		OutError = TEXT("Origin is not allowed for this loopback MCP endpoint");
		return false;
	}

	FString SuppliedToken;
	FString Authorization;
	if (BridgeHttp::GetHeader(Request, TEXT("Authorization"), Authorization)
		&& Authorization.StartsWith(TEXT("Bearer "), ESearchCase::IgnoreCase))
	{
		SuppliedToken = Authorization.Mid(7).TrimStartAndEnd();
	}
	if (SuppliedToken.IsEmpty())
	{
		BridgeHttp::GetHeader(Request, TEXT("X-UnrealBridge-Token"), SuppliedToken);
	}
	if (!BridgeHttp::ConstantTimeEquals(Token, SuppliedToken))
	{
		OutError = TEXT("missing or invalid bearer token");
		return false;
	}
	return true;
}

bool FUnrealBridgeHttpServer::ValidateProtocolHeader(
	const FHttpServerRequest& Request,
	FString& OutError) const
{
	FString Version;
	if (!BridgeHttp::GetHeader(Request, TEXT("MCP-Protocol-Version"), Version))
	{
		return true;
	}
	if (Version == TEXT("2025-11-25") || Version == TEXT("2025-06-18")
		|| Version == TEXT("2025-03-26"))
	{
		return true;
	}
	OutError = FString::Printf(TEXT("unsupported MCP-Protocol-Version '%s'"), *Version);
	return false;
}

bool FUnrealBridgeHttpServer::ParseJsonBody(
	const FHttpServerRequest& Request,
	TSharedPtr<FJsonObject>& OutObject,
	FString& OutError) const
{
	if (Request.Body.IsEmpty())
	{
		OutError = TEXT("request body is empty");
		return false;
	}
	if (Request.Body.Num() > BridgeHttp::MaxRequestBytes)
	{
		OutError = FString::Printf(TEXT("request body exceeds %d bytes"), BridgeHttp::MaxRequestBytes);
		return false;
	}
	FUTF8ToTCHAR Converter(
		reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
	const FString Body(Converter.Length(), Converter.Get());
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
	{
		OutError = TEXT("request body is not a JSON object");
		return false;
	}
	return true;
}

TSharedRef<FJsonObject> FUnrealBridgeHttpServer::BuildHealthObject() const
{
	const FBridgeJobMetrics Metrics = Server.IsValid() && Server->JobManager.IsValid()
		? Server->JobManager->GetMetrics()
		: FBridgeJobMetrics();
	TSharedRef<FJsonObject> Health = MakeShared<FJsonObject>();
	Health->SetBoolField(TEXT("success"), Server.IsValid());
	Health->SetBoolField(TEXT("ready"), Server.IsValid() && Server->IsEditorReady());
	Health->SetStringField(TEXT("status"), Server.IsValid() && Server->IsEditorReady()
		? TEXT("healthy") : TEXT("initializing"));
	Health->SetStringField(TEXT("transport"), TEXT("http-mcp"));
	Health->SetStringField(TEXT("bind"), TEXT("127.0.0.1"));
	Health->SetNumberField(TEXT("port"), Port);
	Health->SetNumberField(TEXT("protocol_version"), 2);
	Health->SetStringField(TEXT("mcp_protocol_version"), BridgeHttp::McpProtocolVersion);
	Health->SetStringField(TEXT("plugin_version"), TEXT("2.0.0"));
	Health->SetStringField(TEXT("registry_hash"), UUnrealBridgeRegistryLibrary::GetToolRegistryHash());
	Health->SetNumberField(TEXT("queue_depth"), Metrics.QueueDepth);
	Health->SetNumberField(TEXT("tracked_jobs"), Metrics.TrackedJobs);
	Health->SetStringField(TEXT("running_job_id"), Metrics.RunningJobId);
	Health->SetNumberField(TEXT("running_ms"), Metrics.RunningMilliseconds);
	Health->SetNumberField(TEXT("oldest_queued_ms"), Metrics.OldestQueuedMilliseconds);
	Health->SetNumberField(TEXT("total_submitted"), static_cast<double>(Metrics.TotalSubmitted));
	Health->SetNumberField(TEXT("total_deduplicated"), static_cast<double>(Metrics.TotalDeduplicated));
	Health->SetNumberField(TEXT("total_succeeded"), static_cast<double>(Metrics.TotalSucceeded));
	Health->SetNumberField(TEXT("total_failed"), static_cast<double>(Metrics.TotalFailed));
	Health->SetNumberField(TEXT("total_cancelled"), static_cast<double>(Metrics.TotalCancelled));
	Health->SetNumberField(TEXT("total_expired"), static_cast<double>(Metrics.TotalExpired));
	Health->SetNumberField(TEXT("total_aborted"), static_cast<double>(Metrics.TotalAborted));
	return Health;
}

TSharedRef<FJsonObject> FUnrealBridgeHttpServer::BuildCapabilitiesObject() const
{
	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetStringField(TEXT("server"), TEXT("UnrealBridge"));
	Capabilities->SetStringField(TEXT("plugin_version"), TEXT("2.0.0"));
	Capabilities->SetNumberField(TEXT("protocol_version"), 2);
	Capabilities->SetStringField(TEXT("mcp_protocol_version"), BridgeHttp::McpProtocolVersion);
	Capabilities->SetStringField(TEXT("registry_hash"), UUnrealBridgeRegistryLibrary::GetToolRegistryHash());
	Capabilities->SetBoolField(TEXT("durable_jobs"), true);
	Capabilities->SetBoolField(TEXT("polling_jobs"), true);
	Capabilities->SetBoolField(TEXT("idempotency"), true);
	Capabilities->SetBoolField(TEXT("queue_deadlines"), true);
	Capabilities->SetBoolField(TEXT("streamable_http_json"), true);
	Capabilities->SetBoolField(TEXT("sse"), false);
	Capabilities->SetNumberField(TEXT("grouped_library_tools"), ToolGroups.Num());
	return Capabilities;
}

void FUnrealBridgeHttpServer::RefreshToolRegistry()
{
	ToolGroups.Reset();
	TSharedPtr<FJsonObject> Registry;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
		UUnrealBridgeRegistryLibrary::GetToolRegistryJson());
	if (!FJsonSerializer::Deserialize(Reader, Registry) || !Registry.IsValid())
	{
		UE_LOG(LogUnrealBridgeHttp, Warning, TEXT("Could not parse native tool registry"));
		return;
	}

	const TSharedPtr<FJsonObject>* Libraries = nullptr;
	if (!Registry->TryGetObjectField(TEXT("libraries"), Libraries) || !Libraries)
	{
		return;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Libraries)->Values)
	{
		if (!Pair.Key.StartsWith(TEXT("UnrealBridge")) || !Pair.Key.EndsWith(TEXT("Library"))
			|| !Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
		{
			continue;
		}
		const FString WrapperClass = Pair.Key.Mid(
			FCString::Strlen(TEXT("UnrealBridge")),
			Pair.Key.Len() - FCString::Strlen(TEXT("UnrealBridge")) - FCString::Strlen(TEXT("Library")));
		FToolGroup Group;
		Group.WrapperClass = WrapperClass;
		Group.FullLibraryName = Pair.Key;
		Group.LibrarySchema = Pair.Value->AsObject();
		ToolGroups.Add(BridgeHttp::CamelToSnake(WrapperClass) + TEXT("_op"), MoveTemp(Group));
	}
}

TArray<TSharedPtr<FJsonValue>> FUnrealBridgeHttpServer::BuildMcpTools() const
{
	using namespace BridgeHttp;
	TArray<TSharedPtr<FJsonValue>> Tools;
	auto Add = [&Tools](const TSharedRef<FJsonObject>& Tool)
	{
		Tools.Add(MakeShared<FJsonValueObject>(Tool));
	};

	Add(MakeTool(TEXT("bridge_health"),
		TEXT("Return editor readiness and durable Job queue health."), EmptyObjectSchema()));
	Add(MakeTool(TEXT("bridge_capabilities"),
		TEXT("Return protocol, registry, transport, and Job capabilities."), EmptyObjectSchema()));

	{
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetObjectField(TEXT("script"), StringSchema(TEXT("Python source executed by Unreal Editor.")));
		Properties->SetObjectField(TEXT("idempotency_key"), StringSchema());
		Properties->SetObjectField(TEXT("queue_timeout_seconds"), NumberSchema(0.1, 3600.0, 300.0));
		Properties->SetObjectField(TEXT("poll_script"), StringSchema(TEXT("Optional short script that prints a JSON object with boolean complete.")));
		Properties->SetObjectField(TEXT("poll_interval_seconds"), NumberSchema(0.01, 60.0, 0.25));
		Properties->SetObjectField(TEXT("run_timeout_seconds"), NumberSchema(0.1, 86400.0, 300.0));
		Add(MakeTool(TEXT("bridge_submit_job"),
			TEXT("Submit a durable one-shot or multi-frame polling Job and return immediately with job_id."),
			MakeObjectSchema(Properties, {TEXT("script")})));
	}
	for (const FString ToolName : {TEXT("bridge_get_job"), TEXT("bridge_cancel_job")})
	{
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetObjectField(TEXT("job_id"), StringSchema());
		Add(MakeTool(ToolName,
			ToolName.EndsWith(TEXT("get_job"))
				? TEXT("Get one durable Job snapshot and terminal result.")
				: TEXT("Cancel a queued Job or request cooperative cancellation."),
			MakeObjectSchema(Properties, {TEXT("job_id")})));
	}
	{
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Limit = MakeShared<FJsonObject>();
		Limit->SetStringField(TEXT("type"), TEXT("integer"));
		Limit->SetNumberField(TEXT("minimum"), 1);
		Limit->SetNumberField(TEXT("maximum"), 200);
		Limit->SetNumberField(TEXT("default"), 50);
		Properties->SetObjectField(TEXT("limit"), Limit);
		Add(MakeTool(TEXT("bridge_list_jobs"), TEXT("List recent durable Job snapshots."),
			MakeObjectSchema(Properties, {})));
	}
	{
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetObjectField(TEXT("tool"), StringSchema(TEXT("Grouped tool name such as level_op.")));
		Properties->SetObjectField(TEXT("operation"), StringSchema());
		Add(MakeTool(TEXT("bridge_describe"),
			TEXT("Return strict registry schema and risk metadata for one grouped operation."),
			MakeObjectSchema(Properties, {TEXT("tool"), TEXT("operation")})));
	}

	TArray<FString> GroupNames;
	ToolGroups.GetKeys(GroupNames);
	GroupNames.Sort();
	for (const FString& ToolName : GroupNames)
	{
		const FToolGroup& Group = ToolGroups.FindChecked(ToolName);
		const TSharedPtr<FJsonObject>* Functions = nullptr;
		if (!Group.LibrarySchema.IsValid()
			|| !Group.LibrarySchema->TryGetObjectField(TEXT("functions"), Functions) || !Functions)
		{
			continue;
		}
		TArray<FString> Operations;
		(*Functions)->Values.GetKeys(Operations);
		Operations.Sort();
		TArray<TSharedPtr<FJsonValue>> OperationValues;
		for (const FString& Operation : Operations)
		{
			OperationValues.Add(MakeShared<FJsonValueString>(Operation));
		}

		TSharedRef<FJsonObject> OperationSchema = StringSchema();
		OperationSchema->SetArrayField(TEXT("enum"), OperationValues);
		TSharedRef<FJsonObject> KwargsSchema = MakeShared<FJsonObject>();
		KwargsSchema->SetStringField(TEXT("type"), TEXT("object"));
		KwargsSchema->SetBoolField(TEXT("additionalProperties"), true);
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetObjectField(TEXT("operation"), OperationSchema);
		Properties->SetObjectField(TEXT("arguments"), KwargsSchema);
		Properties->SetObjectField(TEXT("idempotency_key"), StringSchema());
		Properties->SetObjectField(TEXT("queue_timeout_seconds"), NumberSchema(0.1, 3600.0, 300.0));
		Add(MakeTool(ToolName,
			FString::Printf(TEXT("Submit one UnrealBridge %s operation as a durable Job."), *Group.WrapperClass),
			MakeObjectSchema(Properties, {TEXT("operation")})));
	}
	return Tools;
}

TSharedRef<FJsonObject> FUnrealBridgeHttpServer::SubmitScript(
	const FString& Script,
	const FString& RequestId,
	double QueueTimeout,
	const FString& IdempotencyKey,
	const FString& PollScript,
	double PollInterval,
	double RunTimeout,
	bool& bOutSuccess,
	FString& OutError) const
{
	bOutSuccess = false;
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Server.IsValid() || !Server->JobManager.IsValid())
	{
		OutError = TEXT("Job manager is unavailable");
		return Result;
	}
	if (!Server->IsEditorReady() || Server->bPieTransitionActive)
	{
		OutError = !Server->IsEditorReady()
			? TEXT("editor is not ready")
			: TEXT("editor is in a PIE transition");
		return Result;
	}
	if (Script.IsEmpty())
	{
		OutError = TEXT("script must not be empty");
		return Result;
	}

	FBridgeJobSubmitResult Submitted = Server->JobManager->Submit(
		Script,
		RequestId.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower) : RequestId,
		FMath::Clamp(QueueTimeout, 0.1, 3600.0),
		IdempotencyKey,
		PollScript,
		FMath::Clamp(PollInterval, 0.01, 60.0),
		FMath::Clamp(RunTimeout, 0.1, 86400.0));
	if (!Submitted.Job.IsValid())
	{
		OutError = Submitted.Error.IsEmpty() ? TEXT("Job submission failed") : Submitted.Error;
		return Result;
	}

	FBridgeJobSnapshot Snapshot;
	Server->JobManager->GetSnapshot(Submitted.Job->JobId, Snapshot);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("deduplicated"), Submitted.bDeduplicated);
	Server->AddJobSnapshotFields(Snapshot, Result, false);
	bOutSuccess = true;
	return Result;
}

FString FUnrealBridgeHttpServer::BuildGroupedCallScript(
	const FToolGroup& Group,
	const FString& Operation,
	const TSharedPtr<FJsonObject>& Arguments,
	const TSharedPtr<FJsonObject>& FunctionSchema) const
{
	TSharedRef<FJsonObject> Schemas = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* InputSchema = nullptr;
	const TSharedPtr<FJsonObject>* Properties = nullptr;
	if (FunctionSchema.IsValid()
		&& FunctionSchema->TryGetObjectField(TEXT("input_schema"), InputSchema) && InputSchema
		&& (*InputSchema)->TryGetObjectField(TEXT("properties"), Properties) && Properties)
	{
		Schemas = (*Properties).ToSharedRef();
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("library"), Group.WrapperClass);
	Payload->SetStringField(TEXT("operation"), Operation);
	Payload->SetObjectField(TEXT("arguments"), Arguments.IsValid() ? Arguments.ToSharedRef() : MakeShared<FJsonObject>());
	Payload->SetObjectField(TEXT("schemas"), Schemas);
	const FString PayloadJson = BridgeHttp::SerializeObject(Payload);
	const FTCHARToUTF8 PayloadUtf8(*PayloadJson);
	const FString Encoded = FBase64::Encode(
		reinterpret_cast<const uint8*>(PayloadUtf8.Get()), static_cast<uint32>(PayloadUtf8.Length()));

	return FString::Printf(TEXT(R"PY(import base64
import json
import unreal
import unreal_bridge as _ub

def _ub_jsonable(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, (list, tuple)):
        return [_ub_jsonable(item) for item in value]
    if isinstance(value, dict):
        return {str(key): _ub_jsonable(item) for key, item in value.items()}
    if hasattr(value, "export_text"):
        try:
            return value.export_text()
        except Exception:
            pass
    if hasattr(value, "get_path_name"):
        try:
            return value.get_path_name()
        except Exception:
            pass
    return str(value)

def _ub_coerce(value, schema):
    if value is None:
        return None
    enum_path = schema.get("x-unreal-enum")
    if enum_path and isinstance(value, str):
        enum_name = enum_path.rsplit(".", 1)[-1]
        enum_class = getattr(unreal, enum_name, None)
        if enum_class is None and enum_name.startswith("E"):
            enum_class = getattr(unreal, enum_name[1:], None)
        if enum_class is not None:
            return getattr(enum_class, value)
    struct_name = schema.get("x-unreal-struct")
    if struct_name:
        struct_class = getattr(unreal, struct_name, None)
        if struct_class is not None and isinstance(value, dict):
            return struct_class(**value)
        if struct_class is not None and isinstance(value, (list, tuple)):
            return struct_class(*value)
    if schema.get("format") == "unreal-class-path" and isinstance(value, str):
        return unreal.load_class(None, value)
    if schema.get("format") == "unreal-object-path" and isinstance(value, str):
        return unreal.load_object(None, value)
    if isinstance(value, list) and isinstance(schema.get("items"), dict):
        return [_ub_coerce(item, schema["items"]) for item in value]
    if isinstance(value, dict) and isinstance(schema.get("additionalProperties"), dict):
        return {key: _ub_coerce(item, schema["additionalProperties"]) for key, item in value.items()}
    return value

_payload = json.loads(base64.b64decode("%s").decode("utf-8"))
_arguments = {
    key: _ub_coerce(value, _payload["schemas"].get(key, {}))
    for key, value in _payload["arguments"].items()
}
_wrapper = getattr(_ub, _payload["library"])
_result = getattr(_wrapper, _payload["operation"])(**_arguments)
print(json.dumps({"ok": True, "result": _ub_jsonable(_result)}, ensure_ascii=False))
)PY"), *Encoded);
}

TSharedRef<FJsonObject> FUnrealBridgeHttpServer::InvokeMcpTool(
	const FString& ToolName,
	const TSharedPtr<FJsonObject>& Arguments,
	bool& bOutProtocolError,
	FString& OutProtocolError) const
{
	bOutProtocolError = false;
	const TSharedPtr<FJsonObject> SafeArguments = Arguments.IsValid()
		? Arguments : MakeShared<FJsonObject>();

	if (ToolName == TEXT("bridge_health"))
	{
		return BridgeHttp::MakeToolResult(BuildHealthObject(), false);
	}
	if (ToolName == TEXT("bridge_capabilities"))
	{
		return BridgeHttp::MakeToolResult(BuildCapabilitiesObject(), false);
	}
	if (ToolName == TEXT("bridge_submit_job"))
	{
		FString Script;
		SafeArguments->TryGetStringField(TEXT("script"), Script);
		double QueueTimeout = 300.0;
		SafeArguments->TryGetNumberField(TEXT("queue_timeout_seconds"), QueueTimeout);
		FString IdempotencyKey;
		SafeArguments->TryGetStringField(TEXT("idempotency_key"), IdempotencyKey);
		FString PollScript;
		SafeArguments->TryGetStringField(TEXT("poll_script"), PollScript);
		double PollInterval = 0.25;
		SafeArguments->TryGetNumberField(TEXT("poll_interval_seconds"), PollInterval);
		double RunTimeout = 300.0;
		SafeArguments->TryGetNumberField(TEXT("run_timeout_seconds"), RunTimeout);
		bool bSuccess = false;
		FString Error;
		TSharedRef<FJsonObject> Submitted = SubmitScript(
			Script, FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower),
			QueueTimeout, IdempotencyKey, PollScript, PollInterval, RunTimeout,
			bSuccess, Error);
		return bSuccess ? BridgeHttp::MakeToolResult(Submitted, false)
			: BridgeHttp::MakeToolError(TEXT("JOB_SUBMIT_FAILED"), Error);
	}
	if (ToolName == TEXT("bridge_get_job"))
	{
		FString JobId;
		SafeArguments->TryGetStringField(TEXT("job_id"), JobId);
		FBridgeJobSnapshot Snapshot;
		if (JobId.IsEmpty() || !Server.IsValid() || !Server->JobManager.IsValid()
			|| !Server->JobManager->GetSnapshot(JobId, Snapshot))
		{
			return BridgeHttp::MakeToolError(TEXT("UNKNOWN_JOB"),
				JobId.IsEmpty() ? TEXT("job_id is required")
					: FString::Printf(TEXT("unknown job '%s'"), *JobId));
		}
		TSharedRef<FJsonObject> Job = MakeShared<FJsonObject>();
		Job->SetBoolField(TEXT("success"), true);
		Server->AddJobSnapshotFields(Snapshot, Job, true);
		return BridgeHttp::MakeToolResult(Job, false);
	}
	if (ToolName == TEXT("bridge_cancel_job"))
	{
		FString JobId;
		SafeArguments->TryGetStringField(TEXT("job_id"), JobId);
		FBridgeJobSnapshot Snapshot;
		FString Error;
		if (JobId.IsEmpty() || !Server.IsValid() || !Server->JobManager.IsValid()
			|| !Server->JobManager->Cancel(JobId, Snapshot, Error))
		{
			return BridgeHttp::MakeToolError(TEXT("CANCEL_FAILED"),
				JobId.IsEmpty() ? TEXT("job_id is required") : Error);
		}
		TSharedRef<FJsonObject> Job = MakeShared<FJsonObject>();
		Job->SetBoolField(TEXT("success"), true);
		Server->AddJobSnapshotFields(Snapshot, Job, true);
		return BridgeHttp::MakeToolResult(Job, false);
	}
	if (ToolName == TEXT("bridge_list_jobs"))
	{
		double LimitValue = 50.0;
		SafeArguments->TryGetNumberField(TEXT("limit"), LimitValue);
		const int32 Limit = FMath::Clamp(FMath::RoundToInt(LimitValue), 1, 200);
		TArray<TSharedPtr<FJsonValue>> Jobs;
		if (Server.IsValid() && Server->JobManager.IsValid())
		{
			for (const FBridgeJobSnapshot& Snapshot : Server->JobManager->ListSnapshots(Limit))
			{
				TSharedRef<FJsonObject> Job = MakeShared<FJsonObject>();
				Server->AddJobSnapshotFields(Snapshot, Job, false);
				Jobs.Add(MakeShared<FJsonValueObject>(Job));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetArrayField(TEXT("jobs"), Jobs);
		return BridgeHttp::MakeToolResult(Result, false);
	}
	if (ToolName == TEXT("bridge_describe"))
	{
		FString GroupName;
		FString Operation;
		SafeArguments->TryGetStringField(TEXT("tool"), GroupName);
		SafeArguments->TryGetStringField(TEXT("operation"), Operation);
		const FToolGroup* Group = ToolGroups.Find(GroupName);
		const TSharedPtr<FJsonObject>* Functions = nullptr;
		const TSharedPtr<FJsonObject>* Function = nullptr;
		if (!Group || !Group->LibrarySchema.IsValid()
			|| !Group->LibrarySchema->TryGetObjectField(TEXT("functions"), Functions) || !Functions
			|| !(*Functions)->TryGetObjectField(Operation, Function) || !Function)
		{
			return BridgeHttp::MakeToolError(TEXT("UNKNOWN_OPERATION"),
				FString::Printf(TEXT("unknown operation %s.%s"), *GroupName, *Operation));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("tool"), GroupName);
		Result->SetStringField(TEXT("operation"), Operation);
		Result->SetObjectField(TEXT("schema"), (*Function).ToSharedRef());
		return BridgeHttp::MakeToolResult(Result, false);
	}

	const FToolGroup* Group = ToolGroups.Find(ToolName);
	if (!Group)
	{
		bOutProtocolError = true;
		OutProtocolError = FString::Printf(TEXT("unknown tool '%s'"), *ToolName);
		return BridgeHttp::MakeToolError(TEXT("UNKNOWN_TOOL"), OutProtocolError);
	}

	FString Operation;
	if (!SafeArguments->TryGetStringField(TEXT("operation"), Operation) || Operation.IsEmpty())
	{
		return BridgeHttp::MakeToolError(TEXT("INVALID_ARGUMENTS"), TEXT("operation is required"));
	}
	const TSharedPtr<FJsonObject>* Functions = nullptr;
	const TSharedPtr<FJsonObject>* FunctionSchema = nullptr;
	if (!Group->LibrarySchema.IsValid()
		|| !Group->LibrarySchema->TryGetObjectField(TEXT("functions"), Functions) || !Functions
		|| !(*Functions)->TryGetObjectField(Operation, FunctionSchema) || !FunctionSchema)
	{
		return BridgeHttp::MakeToolError(TEXT("UNKNOWN_OPERATION"),
			FString::Printf(TEXT("unknown operation %s.%s"), *ToolName, *Operation));
	}

	const TSharedPtr<FJsonObject>* CallArgumentsPtr = nullptr;
	SafeArguments->TryGetObjectField(TEXT("arguments"), CallArgumentsPtr);
	const TSharedPtr<FJsonObject> CallArguments = CallArgumentsPtr && CallArgumentsPtr->IsValid()
		? *CallArgumentsPtr : MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* InputSchema = nullptr;
	if ((*FunctionSchema)->TryGetObjectField(TEXT("input_schema"), InputSchema) && InputSchema)
	{
		FString ValidationError;
		if (!BridgeHttp::ValidateValueAgainstSchema(
			MakeShared<FJsonValueObject>(CallArguments.ToSharedRef()), *InputSchema,
			TEXT("arguments"), ValidationError))
		{
			return BridgeHttp::MakeToolError(TEXT("SCHEMA_VALIDATION_FAILED"), ValidationError);
		}
	}

	double QueueTimeout = 300.0;
	SafeArguments->TryGetNumberField(TEXT("queue_timeout_seconds"), QueueTimeout);
	FString IdempotencyKey;
	SafeArguments->TryGetStringField(TEXT("idempotency_key"), IdempotencyKey);
	const FString Script = BuildGroupedCallScript(*Group, Operation, CallArguments, *FunctionSchema);
	bool bSuccess = false;
	FString Error;
	TSharedRef<FJsonObject> Submitted = SubmitScript(
		Script, FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower),
		QueueTimeout, IdempotencyKey, FString(), 0.25, 300.0, bSuccess, Error);
	return bSuccess ? BridgeHttp::MakeToolResult(Submitted, false)
		: BridgeHttp::MakeToolError(TEXT("JOB_SUBMIT_FAILED"), Error);
}

bool FUnrealBridgeHttpServer::HandleHealth(
	const FHttpServerRequest& Request,
	const FHttpResultCallback& OnComplete)
{
	FString Error;
	if (!ValidateRequestSecurity(Request, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::Denied, TEXT("UNAUTHORIZED"), Error));
		return true;
	}
	OnComplete(BridgeHttp::JsonResponse(BuildHealthObject()));
	return true;
}

bool FUnrealBridgeHttpServer::HandleSubmitJob(
	const FHttpServerRequest& Request,
	const FHttpResultCallback& OnComplete)
{
	FString Error;
	if (!ValidateRequestSecurity(Request, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::Denied, TEXT("UNAUTHORIZED"), Error));
		return true;
	}
	TSharedPtr<FJsonObject> Body;
	if (!ParseJsonBody(Request, Body, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("INVALID_JSON"), Error));
		return true;
	}
	FString Script;
	Body->TryGetStringField(TEXT("script"), Script);
	double QueueTimeout = 300.0;
	Body->TryGetNumberField(TEXT("queue_timeout_seconds"), QueueTimeout);
	FString IdempotencyKey;
	Body->TryGetStringField(TEXT("idempotency_key"), IdempotencyKey);
	FString PollScript;
	Body->TryGetStringField(TEXT("poll_script"), PollScript);
	double PollInterval = 0.25;
	Body->TryGetNumberField(TEXT("poll_interval_seconds"), PollInterval);
	double RunTimeout = 300.0;
	Body->TryGetNumberField(TEXT("run_timeout_seconds"), RunTimeout);
	FString RequestId;
	Body->TryGetStringField(TEXT("request_id"), RequestId);
	bool bSuccess = false;
	TSharedRef<FJsonObject> Result = SubmitScript(
		Script, RequestId, QueueTimeout, IdempotencyKey,
		PollScript, PollInterval, RunTimeout, bSuccess, Error);
	if (!bSuccess)
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::Conflict, TEXT("JOB_SUBMIT_FAILED"), Error));
		return true;
	}
	OnComplete(BridgeHttp::JsonResponse(Result, EHttpServerResponseCodes::Accepted));
	return true;
}

bool FUnrealBridgeHttpServer::HandleListJobs(
	const FHttpServerRequest& Request,
	const FHttpResultCallback& OnComplete)
{
	FString Error;
	if (!ValidateRequestSecurity(Request, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::Denied, TEXT("UNAUTHORIZED"), Error));
		return true;
	}
	int32 Limit = 50;
	if (const FString* QueryLimit = Request.QueryParams.Find(TEXT("limit")))
	{
		Limit = FMath::Clamp(FCString::Atoi(**QueryLimit), 1, 200);
	}
	TArray<TSharedPtr<FJsonValue>> Jobs;
	if (Server.IsValid() && Server->JobManager.IsValid())
	{
		for (const FBridgeJobSnapshot& Snapshot : Server->JobManager->ListSnapshots(Limit))
		{
			TSharedRef<FJsonObject> Job = MakeShared<FJsonObject>();
			Server->AddJobSnapshotFields(Snapshot, Job, false);
			Jobs.Add(MakeShared<FJsonValueObject>(Job));
		}
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("jobs"), Jobs);
	OnComplete(BridgeHttp::JsonResponse(Result));
	return true;
}

bool FUnrealBridgeHttpServer::HandleGetJob(
	const FHttpServerRequest& Request,
	const FHttpResultCallback& OnComplete)
{
	FString Error;
	if (!ValidateRequestSecurity(Request, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::Denied, TEXT("UNAUTHORIZED"), Error));
		return true;
	}
	const FString JobId = Request.PathParams.FindRef(TEXT("job_id"));
	FBridgeJobSnapshot Snapshot;
	if (JobId.IsEmpty() || !Server.IsValid() || !Server->JobManager.IsValid()
		|| !Server->JobManager->GetSnapshot(JobId, Snapshot))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("UNKNOWN_JOB"),
			FString::Printf(TEXT("unknown job '%s'"), *JobId)));
		return true;
	}
	TSharedRef<FJsonObject> Job = MakeShared<FJsonObject>();
	Server->AddJobSnapshotFields(Snapshot, Job, true);
	OnComplete(BridgeHttp::JsonResponse(Job));
	return true;
}

bool FUnrealBridgeHttpServer::HandleCancelJob(
	const FHttpServerRequest& Request,
	const FHttpResultCallback& OnComplete)
{
	FString Error;
	if (!ValidateRequestSecurity(Request, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::Denied, TEXT("UNAUTHORIZED"), Error));
		return true;
	}
	const FString JobId = Request.PathParams.FindRef(TEXT("job_id"));
	FBridgeJobSnapshot Snapshot;
	if (JobId.IsEmpty() || !Server.IsValid() || !Server->JobManager.IsValid()
		|| !Server->JobManager->Cancel(JobId, Snapshot, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("CANCEL_FAILED"), Error));
		return true;
	}
	TSharedRef<FJsonObject> Job = MakeShared<FJsonObject>();
	Server->AddJobSnapshotFields(Snapshot, Job, true);
	OnComplete(BridgeHttp::JsonResponse(Job));
	return true;
}

bool FUnrealBridgeHttpServer::HandleMcpGet(
	const FHttpServerRequest& Request,
	const FHttpResultCallback& OnComplete)
{
	FString Error;
	if (!ValidateRequestSecurity(Request, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::Denied, TEXT("UNAUTHORIZED"), Error));
		return true;
	}
	OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::BadMethod,
		TEXT("SSE_NOT_SUPPORTED"), TEXT("SSE is not enabled; use POST /mcp with application/json")));
	return true;
}

bool FUnrealBridgeHttpServer::HandleMcpPost(
	const FHttpServerRequest& Request,
	const FHttpResultCallback& OnComplete)
{
	FString Error;
	if (!ValidateRequestSecurity(Request, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::Denied, TEXT("UNAUTHORIZED"), Error));
		return true;
	}
	if (!ValidateProtocolHeader(Request, Error))
	{
		OnComplete(BridgeHttp::ErrorResponse(EHttpServerResponseCodes::BadRequest,
			TEXT("UNSUPPORTED_PROTOCOL_VERSION"), Error));
		return true;
	}

	TSharedPtr<FJsonObject> Message;
	if (!ParseJsonBody(Request, Message, Error))
	{
		OnComplete(BridgeHttp::JsonResponse(
			BridgeHttp::MakeJsonRpcError(nullptr, -32700, Error), EHttpServerResponseCodes::BadRequest));
		return true;
	}
	FString JsonRpc;
	FString Method;
	Message->TryGetStringField(TEXT("jsonrpc"), JsonRpc);
	Message->TryGetStringField(TEXT("method"), Method);
	const TSharedPtr<FJsonValue> Id = Message->TryGetField(TEXT("id"));
	if (JsonRpc != TEXT("2.0") || Method.IsEmpty())
	{
		OnComplete(BridgeHttp::JsonResponse(
			BridgeHttp::MakeJsonRpcError(Id, -32600, TEXT("invalid JSON-RPC request")),
			EHttpServerResponseCodes::BadRequest));
		return true;
	}

	if (!Id.IsValid())
	{
		// MCP notifications are accepted without a JSON-RPC response body.
		OnComplete(BridgeHttp::EmptyResponse(EHttpServerResponseCodes::Accepted));
		return true;
	}

	if (Method == TEXT("initialize"))
	{
		FString RequestedVersion;
		const TSharedPtr<FJsonObject>* Params = nullptr;
		if (Message->TryGetObjectField(TEXT("params"), Params) && Params)
		{
			(*Params)->TryGetStringField(TEXT("protocolVersion"), RequestedVersion);
		}
		const FString NegotiatedVersion =
			RequestedVersion == TEXT("2025-03-26") || RequestedVersion == TEXT("2025-06-18")
			|| RequestedVersion == TEXT("2025-11-25")
				? RequestedVersion : FString(BridgeHttp::McpProtocolVersion);
		TSharedRef<FJsonObject> ToolsCapability = MakeShared<FJsonObject>();
		ToolsCapability->SetBoolField(TEXT("listChanged"), false);
		TSharedRef<FJsonObject> Experimental = MakeShared<FJsonObject>();
		Experimental->SetBoolField(TEXT("durableJobs"), true);
		TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
		Capabilities->SetObjectField(TEXT("tools"), ToolsCapability);
		Capabilities->SetObjectField(TEXT("experimental"), Experimental);
		TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
		ServerInfo->SetStringField(TEXT("name"), TEXT("UnrealBridge"));
		ServerInfo->SetStringField(TEXT("version"), TEXT("2.0.0"));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("protocolVersion"), NegotiatedVersion);
		Result->SetObjectField(TEXT("capabilities"), Capabilities);
		Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
		Result->SetStringField(TEXT("instructions"),
			TEXT("Unreal operations are durable Jobs. Poll bridge_get_job until terminal."));
		OnComplete(BridgeHttp::JsonResponse(BridgeHttp::MakeJsonRpcResponse(Id, Result)));
		return true;
	}
	if (Method == TEXT("ping"))
	{
		OnComplete(BridgeHttp::JsonResponse(
			BridgeHttp::MakeJsonRpcResponse(Id, MakeShared<FJsonObject>())));
		return true;
	}
	if (Method == TEXT("tools/list"))
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("tools"), BuildMcpTools());
		OnComplete(BridgeHttp::JsonResponse(BridgeHttp::MakeJsonRpcResponse(Id, Result)));
		return true;
	}
	if (Method == TEXT("tools/call"))
	{
		const TSharedPtr<FJsonObject>* Params = nullptr;
		FString ToolName;
		const TSharedPtr<FJsonObject>* Arguments = nullptr;
		if (!Message->TryGetObjectField(TEXT("params"), Params) || !Params
			|| !(*Params)->TryGetStringField(TEXT("name"), ToolName))
		{
			OnComplete(BridgeHttp::JsonResponse(BridgeHttp::MakeJsonRpcError(
				Id, -32602, TEXT("tools/call requires params.name"))));
			return true;
		}
		(*Params)->TryGetObjectField(TEXT("arguments"), Arguments);
		bool bProtocolError = false;
		FString ProtocolError;
		TSharedRef<FJsonObject> Result = InvokeMcpTool(
			ToolName, Arguments ? *Arguments : nullptr, bProtocolError, ProtocolError);
		if (bProtocolError)
		{
			OnComplete(BridgeHttp::JsonResponse(
				BridgeHttp::MakeJsonRpcError(Id, -32602, ProtocolError)));
		}
		else
		{
			OnComplete(BridgeHttp::JsonResponse(BridgeHttp::MakeJsonRpcResponse(Id, Result)));
		}
		return true;
	}

	OnComplete(BridgeHttp::JsonResponse(
		BridgeHttp::MakeJsonRpcError(Id, -32601,
			FString::Printf(TEXT("method '%s' is not supported"), *Method))));
	return true;
}
