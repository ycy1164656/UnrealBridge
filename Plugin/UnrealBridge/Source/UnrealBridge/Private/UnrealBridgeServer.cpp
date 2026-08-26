#include "UnrealBridgeServer.h"
#include "UnrealBridgeChangeSetLibrary.h"
#include "IPythonScriptPlugin.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Async/Async.h"
#include "SocketSubsystem.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Framework/Application/SlateApplication.h"
#include "UnrealBridgeCallLog.h"
#include "UnrealBridgeRegistryLibrary.h"
#include "UnrealBridgeUE58Library.h"
#include "UnrealBridgeVersion.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealBridge, Log, All);

namespace UnrealBridgeLimits
{
	// Max request JSON payload. 10 MB is generous for human-authored scripts;
	// the upper bound exists mainly to stop a malicious/buggy client from
	// triggering an OOM in the editor via blind SetNumUninitialized.
	constexpr int32 MaxRequestBytes = 10 * 1024 * 1024;
}

namespace
{
	bool DecodePollingStepResult(FBridgeJobResult& Result, bool& bOutComplete)
	{
		bOutComplete = false;
		TSharedPtr<FJsonObject> Payload;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Output.TrimStartAndEnd());
		if (!FJsonSerializer::Deserialize(Reader, Payload) || !Payload.IsValid()
			|| !Payload->TryGetBoolField(TEXT("complete"), bOutComplete))
		{
			Result.bSuccess = false;
			Result.Error = TEXT("poll script must print one JSON object with a boolean 'complete' field");
			Result.ErrorCode = TEXT("JOB_POLL_PROTOCOL_ERROR");
			Result.Phase = TEXT("poll");
			Result.bRetryable = false;
			return false;
		}

		bool bStepSuccess = true;
		Payload->TryGetBoolField(TEXT("success"), bStepSuccess);
		if (!bStepSuccess)
		{
			Result.bSuccess = false;
			if (!Payload->TryGetStringField(TEXT("error"), Result.Error) || Result.Error.IsEmpty())
			{
				Result.Error = TEXT("poll script reported failure");
			}
			Result.ErrorCode = TEXT("JOB_POLL_REPORTED_FAILURE");
			Result.Phase = TEXT("poll");
			Result.bRetryable = false;
			return false;
		}
		return true;
	}
}

// ─────────────────────────────────────────────────────────────
// Server lifecycle
// ─────────────────────────────────────────────────────────────

FUnrealBridgeServer::FUnrealBridgeServer()
{
}

FUnrealBridgeServer::~FUnrealBridgeServer()
{
	Stop();
}

bool FUnrealBridgeServer::Start(int32 Port)
{
	FStartConfig Cfg;
	Cfg.Port = Port;
	return Start(Cfg);
}

bool FUnrealBridgeServer::Start(const FStartConfig& Config)
{
	if (bIsRunning)
	{
		return true;
	}

	// Safety gate: binding to a non-localhost interface exposes Python exec to
	// the LAN. Refuse if the caller didn't supply a token.
	const bool bIsLoopback =
		(Config.BindAddress == FIPv4Address(127, 0, 0, 1)) ||
		(Config.BindAddress == FIPv4Address::InternalLoopback);
	if (!bIsLoopback && Config.Token.IsEmpty())
	{
		UE_LOG(LogUnrealBridge, Error,
			TEXT("Refusing to bind %s:%d without a token — set -UnrealBridgeToken=... ")
			TEXT("or use -UnrealBridgeBind=127.0.0.1"),
			*Config.BindAddress.ToString(), Config.Port);
		return false;
	}

	BindAddressStr = Config.BindAddress.ToString();
	Token = Config.Token;

	const FIPv4Endpoint Endpoint(Config.BindAddress, Config.Port);

	// 100ms poll (vs default 1s) collapses the accept-race window that produced
	// intermittent WSAECONNABORTED 10053 on clients. bInReusable=true lets Start()
	// reclaim a TIME_WAIT socket after a crash/quick-restart instead of failing
	// with "address in use". See docs/server-stability-plan.md #7.
	Listener = MakeUnique<FTcpListener>(
		Endpoint,
		FTimespan::FromMilliseconds(100),
		true /* bInReusable */
	);

	if (!Listener.IsValid() || !Listener->IsActive())
	{
		UE_LOG(LogUnrealBridge, Error, TEXT("Failed to create TCP listener on %s:%d"),
			*BindAddressStr, Config.Port);
		Listener.Reset();
		return false;
	}

	// When Port=0 the kernel picks a free ephemeral port — read it back so
	// clients and the discovery responder know where to connect.
	ListenPort = Config.Port;
	if (Listener->GetSocket() != nullptr)
	{
		TSharedRef<FInternetAddr> LocalAddr =
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		Listener->GetSocket()->GetAddress(*LocalAddr);
		const int32 ResolvedPort = LocalAddr->GetPort();
		if (ResolvedPort > 0)
		{
			ListenPort = ResolvedPort;
		}
	}

	Listener->OnConnectionAccepted().BindRaw(this, &FUnrealBridgeServer::OnConnectionAccepted);

	JobManager = MakeUnique<FBridgeJobManager>();

	// Register the GameThread ticker that drains the exec queue.
	// Using FTSTicker instead of AsyncTask(GameThread) prevents reentrancy:
	// ticker callbacks fire only from FEngineLoop::Tick, not from TaskGraph
	// pumps triggered inside user scripts (asset loads, blueprint compiles, etc.).
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FUnrealBridgeServer::TickConsumeQueue),
		0.0f /* tick every frame */
	);

	// PIE transition guard (item #11). The transition *window* is:
	//   BeginPIE  ─────────→  PostPIEStarted   (editor subsystems torn
	//       [unsafe to exec]                   down and rebuilt here)
	//   PrePIEEnded  ──────→  EndPIE           (shutdown sequence —
	//       [unsafe again]                     same teardown/rebuild)
	// Between PostPIEStarted and PrePIEEnded PIE is running stably and
	// execs are safe. An earlier version used only BeginPIE/EndPIE which
	// kept the flag True for the entire PIE session and blocked agent
	// observation calls.
	PieBeginHandle = FEditorDelegates::BeginPIE.AddLambda([this](const bool /*bIsSimulating*/)
	{
		bPieTransitionActive = true;
	});
	PiePostStartedHandle = FEditorDelegates::PostPIEStarted.AddLambda([this](const bool /*bIsSimulating*/)
	{
		bPieTransitionActive = false;
	});
	PiePreEndedHandle = FEditorDelegates::PrePIEEnded.AddLambda([this](const bool /*bIsSimulating*/)
	{
		bPieTransitionActive = true;
	});
	PieEndHandle = FEditorDelegates::EndPIE.AddLambda([this](const bool /*bIsSimulating*/)
	{
		bPieTransitionActive = false;
	});

	bIsRunning = true;
	UE_LOG(LogUnrealBridge, Log, TEXT("Listening on %s:%d%s"),
		*BindAddressStr, ListenPort,
		HasToken() ? TEXT(" (token auth enforced)") : TEXT(""));
	return true;
}

void FUnrealBridgeServer::Stop()
{
	if (!bIsRunning)
	{
		return;
	}
	bIsRunning = false;

	// 1. Stop accepting new connections.
	if (Listener.IsValid())
	{
		Listener.Reset();
	}

	// 2. Unregister GameThread ticker and editor delegates (items #11 #12).
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	if (PieBeginHandle.IsValid())
	{
		FEditorDelegates::BeginPIE.Remove(PieBeginHandle);
		PieBeginHandle.Reset();
	}
	if (PiePostStartedHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(PiePostStartedHandle);
		PiePostStartedHandle.Reset();
	}
	if (PiePreEndedHandle.IsValid())
	{
		FEditorDelegates::PrePIEEnded.Remove(PiePreEndedHandle);
		PiePreEndedHandle.Reset();
	}
	if (PieEndHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(PieEndHandle);
		PieEndHandle.Reset();
	}

	// 3. Abort queued/running Job records and wake every waiting client.
	if (JobManager.IsValid())
	{
		JobManager->Shutdown();
	}

	// 4. Force-close active client sockets so HandleClient's RecvAll
	// unblocks immediately instead of waiting for its 5 s idle timeout.
	{
		FScopeLock Lock(&ActiveSocketsLock);
		for (FSocket* S : ActiveSockets)
		{
			if (S)
			{
				S->Close();
			}
		}
	}

	// 5. Bounded wait for AsyncTask workers to drain (item #12). Beyond
	// the deadline we log and proceed — the workers will still finish
	// their cleanup (DestroySocket, ActiveClients.Decrement) but
	// ShutdownModule isn't held hostage to a stuck Python exec.
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	while (ActiveClients.GetValue() > 0 && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.01f);
	}
	const int32 Stragglers = ActiveClients.GetValue();
	if (Stragglers > 0)
	{
		UE_LOG(LogUnrealBridge, Warning,
			TEXT("Stop(): %d client worker(s) still active after 3s drain timeout"),
			Stragglers);
	}
	else
	{
		UE_LOG(LogUnrealBridge, Log, TEXT("Stop(): all client workers drained cleanly"));
	}
	JobManager.Reset();
}

namespace
{
	TSharedPtr<FJsonObject> LoadManifestMetadata()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealBridge"));
		if (!Plugin.IsValid())
		{
			return nullptr;
		}
		const FString Path = FPaths::Combine(
			Plugin->GetContentDir(), TEXT("Python"), TEXT("bridge_manifest_meta.json"));
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Metadata;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Metadata) ? Metadata : nullptr;
	}

	void AddVersionHandshake(const TSharedRef<FJsonObject>& Response)
	{
		Response->SetNumberField(TEXT("protocol_version"), UnrealBridgeVersion::Protocol);
		Response->SetStringField(TEXT("plugin_version"), UnrealBridgeVersion::Plugin);
		Response->SetStringField(TEXT("registry_hash"),
			UUnrealBridgeRegistryLibrary::GetToolRegistryHash());
		if (const TSharedPtr<FJsonObject> Metadata = LoadManifestMetadata())
		{
			FString Value;
			if (Metadata->TryGetStringField(TEXT("manifest_hash"), Value))
			{
				Response->SetStringField(TEXT("manifest_hash"), Value);
			}
			if (Metadata->TryGetStringField(TEXT("wrapper_version"), Value))
			{
				Response->SetStringField(TEXT("wrapper_version"), Value);
			}
		}
	}

	bool ValidateClientHandshake(
		const TSharedPtr<FJsonObject>& Request,
		FString& OutCode,
		FString& OutError)
	{
		bool bBootstrap = false;
		Request->TryGetBoolField(TEXT("manifest_bootstrap"), bBootstrap);
		if (bBootstrap)
		{
			return true;
		}

		double ProtocolVersion = 0.0;
		if (!Request->TryGetNumberField(TEXT("protocol_version"), ProtocolVersion)
			|| FMath::RoundToInt(ProtocolVersion) != 2)
		{
			OutCode = TEXT("PROTOCOL_VERSION_MISMATCH");
			OutError = TEXT("client protocol_version is missing or incompatible; regenerate the UnrealBridge manifest/wrapper");
			return false;
		}

		FString ClientRegistryHash;
		Request->TryGetStringField(TEXT("registry_hash"), ClientRegistryHash);
		const FString RuntimeRegistryHash = UUnrealBridgeRegistryLibrary::GetToolRegistryHash();
		if (ClientRegistryHash.IsEmpty() || ClientRegistryHash != RuntimeRegistryHash)
		{
			OutCode = TEXT("REGISTRY_HASH_MISMATCH");
			OutError = TEXT("client registry_hash does not match the loaded plugin; run python tools/gen_manifest.py");
			return false;
		}

		const TSharedPtr<FJsonObject> Metadata = LoadManifestMetadata();
		if (!Metadata.IsValid())
		{
			OutCode = TEXT("SERVER_MANIFEST_UNAVAILABLE");
			OutError = TEXT("loaded plugin has no bridge_manifest_meta.json; regenerate and synchronize the plugin");
			return false;
		}

		for (const TCHAR* Field : {TEXT("plugin_version"), TEXT("manifest_hash"), TEXT("wrapper_version")})
		{
			FString Expected;
			FString Actual;
			Metadata->TryGetStringField(Field, Expected);
			Request->TryGetStringField(Field, Actual);
			if (Expected.IsEmpty() || Actual != Expected)
			{
				OutCode = FString(Field).ToUpper() + TEXT("_MISMATCH");
				OutError = FString::Printf(
					TEXT("client %s does not match the loaded plugin wrapper; run python tools/gen_manifest.py"),
					Field);
				return false;
			}
		}
		return true;
	}
}

bool FUnrealBridgeServer::IsRunning() const
{
	return bIsRunning;
}

void FUnrealBridgeServer::SetEditorReady(bool bReady)
{
	bEditorReady = bReady;
	if (bReady)
	{
		UE_LOG(LogUnrealBridge, Log, TEXT("Editor reported ready — Python exec now accepted"));
	}
}

bool FUnrealBridgeServer::IsEditorReady() const
{
	return bEditorReady;
}

// ─────────────────────────────────────────────────────────────
// Connection handling
// ─────────────────────────────────────────────────────────────

bool FUnrealBridgeServer::OnConnectionAccepted(FSocket* ClientSocket, const FIPv4Endpoint& ClientEndpoint)
{
	const FString EndpointStr = ClientEndpoint.ToString();

	// Bound concurrent clients so a runaway caller can't saturate the
	// AsyncTask background pool and starve other editor work. When we're
	// over capacity we return false so FTcpListener destroys the accepted
	// socket itself — the client sees a clean connection reset rather than
	// a silent hang.
	const int32 Active = ActiveClients.Increment();
	if (Active > MaxConcurrentClients)
	{
		ActiveClients.Decrement();
		UE_LOG(LogUnrealBridge, Warning,
			TEXT("[conn] rejecting %s — at concurrency limit (%d/%d)"),
			*EndpointStr, Active - 1, MaxConcurrentClients);
		return false;
	}

	UE_LOG(LogUnrealBridge, Verbose, TEXT("[conn] accepted %s (active=%d)"), *EndpointStr, Active);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ClientSocket, EndpointStr]()
	{
		// Register socket so Stop() can force-close us (item #6).
		{
			FScopeLock Lock(&ActiveSocketsLock);
			ActiveSockets.Add(ClientSocket);
		}

		HandleClient(ClientSocket, EndpointStr);

		{
			FScopeLock Lock(&ActiveSocketsLock);
			ActiveSockets.Remove(ClientSocket);
		}

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ClientSocket);
		}
		ActiveClients.Decrement();
	});

	return true;
}

void FUnrealBridgeServer::HandleClient(FSocket* ClientSocket, const FString& EndpointStr)
{
	// One request-response per connection. bridge.py opens a fresh socket
	// per call; keep-alive would tie worker threads up in idle waits and
	// saturate the AsyncTask pool under high request rates.
	if (!bIsRunning)
	{
		return;
	}
	const double T0 = FPlatformTime::Seconds();

	// Per-request telemetry collected throughout this function and flushed
	// to the bridge-call ring buffer right before we return. Written-to in
	// several branches below — see each `else if` for where the fields
	// are populated. See UnrealBridgeCallLog.h.
	FBridgeCallRecord CallRecord;
	CallRecord.Endpoint = EndpointStr;
	{
		// ToUnixTimestamp truncates to whole seconds; compute fractional by
		// subtracting the epoch as FTimespan and using GetTotalSeconds().
		static const FDateTime UnixEpoch(1970, 1, 1);
		CallRecord.UnixSeconds = (FDateTime::UtcNow() - UnixEpoch).GetTotalSeconds();
	}
	ON_SCOPE_EXIT
	{
		CallRecord.TotalDurationMs = (FPlatformTime::Seconds() - T0) * 1000.0;
		FBridgeCallLog::Get().Append(MoveTemp(CallRecord));
	};

	// 1. Read 4-byte length prefix (big-endian)
	uint8 LenBuf[4];
	if (!RecvAll(ClientSocket, LenBuf, 4, 5.0f))
	{
		UE_LOG(LogUnrealBridge, Verbose,
			TEXT("[%s] recv header failed (client gave up or idle timeout)"),
			*EndpointStr);
		return;
	}

	const uint32 PayloadLen = (uint32(LenBuf[0]) << 24)
							| (uint32(LenBuf[1]) << 16)
							| (uint32(LenBuf[2]) << 8)
							| (uint32(LenBuf[3]));

	if (PayloadLen == 0 || PayloadLen > (uint32)UnrealBridgeLimits::MaxRequestBytes)
	{
		UE_LOG(LogUnrealBridge, Warning,
			TEXT("[%s] invalid payload length %u (max %d) — closing"),
			*EndpointStr, PayloadLen, UnrealBridgeLimits::MaxRequestBytes);
		return;
	}

	// 2. Read JSON payload — Reserve first so an allocation failure is detected
	// before we commit to a SetNumUninitialized of PayloadLen bytes.
	TArray<uint8> PayloadBuf;
	PayloadBuf.Reserve((int32)PayloadLen);
	if (PayloadBuf.Max() < (int32)PayloadLen)
	{
		UE_LOG(LogUnrealBridge, Warning,
			TEXT("[%s] failed to allocate %u bytes for payload"),
			*EndpointStr, PayloadLen);
		return;
	}
	PayloadBuf.SetNumUninitialized((int32)PayloadLen);
	if (!RecvAll(ClientSocket, PayloadBuf.GetData(), (int32)PayloadLen, 30.0f))
	{
		UE_LOG(LogUnrealBridge, Warning,
			TEXT("[%s] recv payload failed (expected %u bytes)"),
			*EndpointStr, PayloadLen);
		return;
	}

	FUTF8ToTCHAR Converter((const ANSICHAR*)PayloadBuf.GetData(), PayloadLen);
	FString JsonStr(Converter.Length(), Converter.Get());

	// 3. Parse JSON
	TSharedPtr<FJsonObject> Request;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
	{
		UE_LOG(LogUnrealBridge, Warning,
			TEXT("[%s] JSON parse failed (payload=%u bytes)"),
			*EndpointStr, PayloadLen);
		return;
	}

	FString RequestId;
	if (!Request->TryGetStringField(TEXT("id"), RequestId))
	{
		RequestId = TEXT("<missing>");
	}
	CallRecord.RequestId = RequestId;

	// 4. Build response
	TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("id"), RequestId);

	// 4a. Token auth — only enforced when the server was started with a token
	// (i.e. binding to a non-localhost interface). Constant-time compare so a
	// timing oracle can't whittle the secret out.
	if (!Token.IsEmpty())
	{
		FString GivenToken;
		Request->TryGetStringField(TEXT("token"), GivenToken);

		const auto* A = (const TCHAR*)*Token;
		const auto* B = (const TCHAR*)*GivenToken;
		const int32 LenA = Token.Len();
		const int32 LenB = GivenToken.Len();
		uint32 Diff = (uint32)(LenA ^ LenB);
		const int32 Cmp = FMath::Min(LenA, LenB);
		for (int32 i = 0; i < Cmp; ++i)
		{
			Diff |= (uint32)(A[i] ^ B[i]);
		}

		if (Diff != 0)
		{
			Response->SetBoolField(TEXT("success"), false);
			Response->SetStringField(TEXT("output"), TEXT(""));
			Response->SetStringField(TEXT("error"), TEXT("unauthorized: missing or invalid token"));

			FString RespJson;
			TSharedRef<TJsonWriter<>> RespWriter = TJsonWriterFactory<>::Create(&RespJson);
			FJsonSerializer::Serialize(Response, RespWriter);

			const FTCHARToUTF8 RespUtf8(*RespJson);
			const int32 RespLen = RespUtf8.Length();
			uint8 AuthRespLenBuf[4] = {
				(uint8)((RespLen >> 24) & 0xFF),
				(uint8)((RespLen >> 16) & 0xFF),
				(uint8)((RespLen >> 8) & 0xFF),
				(uint8)(RespLen & 0xFF),
			};
			SendAll(ClientSocket, AuthRespLenBuf, 4);
			SendAll(ClientSocket, (const uint8*)RespUtf8.Get(), RespLen);

			UE_LOG(LogUnrealBridge, Warning,
				TEXT("[%s] unauthorized request id=%s (bad token)"),
				*EndpointStr, *RequestId);
			return;
		}
	}

	FString Command;
	Request->TryGetStringField(TEXT("command"), Command); // optional
	CallRecord.Command = Command.IsEmpty() ? TEXT("exec") : Command;

	UE_LOG(LogUnrealBridge, Verbose,
		TEXT("[%s] request id=%s cmd=%s payload=%u"),
		*EndpointStr, *RequestId, Command.IsEmpty() ? TEXT("(exec)") : *Command, PayloadLen);

	const bool bExecutableCommand = Command.IsEmpty()
		|| Command == TEXT("exec")
		|| Command == TEXT("submit_job");
	FString HandshakeCode;
	FString HandshakeError;
	if (bExecutableCommand && !ValidateClientHandshake(Request, HandshakeCode, HandshakeError))
	{
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("output"), TEXT(""));
		Response->SetStringField(TEXT("error"), HandshakeError);
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
		TSharedRef<FJsonObject> ErrorInfo = MakeShared<FJsonObject>();
		ErrorInfo->SetStringField(TEXT("code"), HandshakeCode);
		ErrorInfo->SetStringField(TEXT("message"), HandshakeError);
		ErrorInfo->SetStringField(TEXT("phase"), TEXT("handshake"));
		ErrorInfo->SetBoolField(TEXT("retryable"), false);
		ErrorInfo->SetStringField(TEXT("side_effect_state"), TEXT("none"));
		Response->SetObjectField(TEXT("error_info"), ErrorInfo);
		AddVersionHandshake(Response);
	}
	else if (Command == TEXT("health"))
	{
		const FBridgeJobMetrics Metrics = JobManager.IsValid()
			? JobManager->GetMetrics()
			: FBridgeJobMetrics();
		TSharedRef<FJsonObject> Health = MakeShared<FJsonObject>();
		Health->SetNumberField(TEXT("queue_depth"), Metrics.QueueDepth);
		Health->SetNumberField(TEXT("tracked_jobs"), Metrics.TrackedJobs);
		Health->SetNumberField(TEXT("active_clients"), ActiveClients.GetValue());
		Health->SetNumberField(TEXT("max_clients"), MaxConcurrentClients);
		Health->SetStringField(TEXT("running_job_id"), Metrics.RunningJobId);
		Health->SetNumberField(TEXT("running_ms"), Metrics.RunningMilliseconds);
		Health->SetNumberField(TEXT("oldest_queued_ms"), Metrics.OldestQueuedMilliseconds);
		Health->SetNumberField(TEXT("total_submitted"), (double)Metrics.TotalSubmitted);
		Health->SetNumberField(TEXT("total_deduplicated"), (double)Metrics.TotalDeduplicated);
		Health->SetNumberField(TEXT("total_succeeded"), (double)Metrics.TotalSucceeded);
		Health->SetNumberField(TEXT("total_failed"), (double)Metrics.TotalFailed);
		Health->SetNumberField(TEXT("total_cancelled"), (double)Metrics.TotalCancelled);
		Health->SetNumberField(TEXT("total_expired"), (double)Metrics.TotalExpired);
		Health->SetNumberField(TEXT("total_aborted"), (double)Metrics.TotalAborted);

		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("output"), TEXT("healthy"));
		Response->SetStringField(TEXT("error"), TEXT(""));
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
		AddVersionHandshake(Response);
		Response->SetObjectField(TEXT("health"), Health);
	}
	else if (Command == TEXT("capabilities"))
	{
		TArray<TSharedPtr<FJsonValue>> Commands;
		for (const TCHAR* Name : {
			TEXT("ping"), TEXT("health"), TEXT("capabilities"), TEXT("exec"),
			TEXT("submit_job"), TEXT("get_job"), TEXT("wait_job"),
			TEXT("cancel_job"), TEXT("list_jobs"), TEXT("gamethread_ping"),
			TEXT("debug_resume")})
		{
			Commands.Add(MakeShared<FJsonValueString>(Name));
		}
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("output"), TEXT("UnrealBridge protocol v2"));
		Response->SetStringField(TEXT("error"), TEXT(""));
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
		AddVersionHandshake(Response);
		Response->SetArrayField(TEXT("commands"), Commands);
		Response->SetBoolField(TEXT("durable_jobs"), true);
		Response->SetBoolField(TEXT("polling_jobs"), true);
		Response->SetBoolField(TEXT("idempotency"), true);
		Response->SetBoolField(TEXT("queue_deadlines"), true);
		Response->SetBoolField(TEXT("ue58_toolset_registry_compiled"), UnrealBridgeUE58Adapter::IsCompiled());
		Response->SetBoolField(TEXT("official_toolset_registry_available"), UnrealBridgeUE58Adapter::IsRegistryAvailable());
		Response->SetStringField(TEXT("official_toolset_provider"), TEXT("EpicToolsetRegistry"));
		Response->SetStringField(TEXT("official_toolset_execution"), TEXT("durable-polling-job"));
		Response->SetStringField(TEXT("official_toolset_policy"), TEXT("schema-declared-read-only-only"));
		Response->SetStringField(TEXT("official_toolset_save_behavior"), TEXT("never"));
		Response->SetStringField(TEXT("official_toolset_cancel_mode"), TEXT("bridge-polling-only"));
	}
	else if (Command == TEXT("ping"))
	{
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("output"), TEXT("pong"));
		Response->SetStringField(TEXT("error"), TEXT(""));
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
		AddVersionHandshake(Response);
	}
	else if (Command == TEXT("debug_resume"))
	{
		// Recovery path for a stuck blueprint breakpoint.
		//
		// When a BP breakpoint fires, UE enters `FSlateApplication::EnterDebuggingMode`
		// — a nested Slate loop that keeps pumping the task graph on the
		// GameThread but does NOT pump the FTSTicker-based Python exec queue.
		// A prior `invoke_*` that triggered the break is still blocked inside
		// `ProcessEvent`, so the Python interpreter is occupied and new
		// `exec` commands can't land.
		//
		// Recovery requires TWO things, both dispatched via AsyncTask (task
		// graph is pumped during the nested Slate loop; FTSTicker is not):
		//
		//   1. `FSlateApplication::LeaveDebuggingMode()` — exits the nested
		//      Slate loop, unblocking `AttemptToBreakExecution` so the BP
		//      VM resumes.
		//   2. `FKismetDebugUtilities::RequestAbortingExecution()` — sets
		//      `bAbortingExecution` on the stack frame so when the VM
		//      resumes, it unwinds rather than continuing past the
		//      breakpoint (which would hit the same break again).
		//
		// Together these pop the debug-mode stack and let ProcessEvent
		// return, which unblocks the stuck Python exec, which unblocks the
		// TCP response to the original caller.
		AsyncTask(ENamedThreads::GameThread, []()
		{
			FKismetDebugUtilities::RequestAbortingExecution();
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().LeaveDebuggingMode(/*bLeavingDebugForSingleStep*/ false);
			}
		});
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("output"), TEXT("resume requested"));
		Response->SetStringField(TEXT("error"), TEXT(""));
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
	}
	else if (Command == TEXT("gamethread_ping"))
	{
		// Probe whether the GameThread is responsive without going through
		// the FTSTicker exec queue. Submits a no-op AsyncTask(GameThread)
		// and waits with a short bounded timeout (default 2s, max 10s).
		//
		// Diagnostic interpretation:
		//   - alive, low latency (~ms): GT idle, exec queue healthy
		//   - alive, high latency (~hundreds ms): GT mid-exec but TaskGraph
		//     is being pumped (asset load / BP compile inside Python) —
		//     editor is not deadlocked but the exec queue may be backed up
		//   - unresponsive: GT is fully stuck (modal dialog, deadlock,
		//     pure-Python tight loop holding the GIL with no TG pump). The
		//     FTSTicker exec queue cannot drain in this state.
		double ProbeTimeoutNum = 2.0;
		Request->TryGetNumberField(TEXT("timeout"), ProbeTimeoutNum);
		const float ProbeTimeout = FMath::Clamp((float)ProbeTimeoutNum, 0.1f, 10.0f);

		auto Probe = MakeShared<TPromise<bool>, ESPMode::ThreadSafe>();
		TFuture<bool> ProbeFuture = Probe->GetFuture();
		const double ProbeT0 = FPlatformTime::Seconds();

		AsyncTask(ENamedThreads::GameThread, [Probe]()
		{
			Probe->SetValue(true);
		});

		const bool bAlive = ProbeFuture.WaitFor(FTimespan::FromSeconds(ProbeTimeout));
		const double LatencyMs = (FPlatformTime::Seconds() - ProbeT0) * 1000.0;

		Response->SetBoolField(TEXT("success"), bAlive);
		Response->SetStringField(TEXT("output"), bAlive ? TEXT("alive") : TEXT("unresponsive"));
		Response->SetStringField(TEXT("error"), bAlive
			? TEXT("")
			: FString::Printf(TEXT("GameThread did not respond within %.1fs"), ProbeTimeout));
		Response->SetNumberField(TEXT("latency_ms"), LatencyMs);
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
		AddVersionHandshake(Response);
	}
	else if (Command == TEXT("get_job") || Command == TEXT("job_status"))
	{
		FString JobId;
		Request->TryGetStringField(TEXT("job_id"), JobId);
		FBridgeJobSnapshot Snapshot;
		if (JobId.IsEmpty() || !JobManager.IsValid() || !JobManager->GetSnapshot(JobId, Snapshot))
		{
			Response->SetBoolField(TEXT("success"), false);
			Response->SetStringField(TEXT("output"), TEXT(""));
			Response->SetStringField(TEXT("error"), JobId.IsEmpty()
				? TEXT("missing 'job_id' field")
				: FString::Printf(TEXT("unknown job '%s'"), *JobId));
		}
		else
		{
			Response->SetBoolField(TEXT("success"), true);
			Response->SetStringField(TEXT("output"), JobId);
			Response->SetStringField(TEXT("error"), TEXT(""));
			AddJobSnapshotFields(Snapshot, Response);
		}
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
	}
	else if (Command == TEXT("wait_job"))
	{
		FString JobId;
		Request->TryGetStringField(TEXT("job_id"), JobId);
		double WaitSeconds = 30.0;
		Request->TryGetNumberField(TEXT("wait_timeout"), WaitSeconds);
		WaitSeconds = FMath::Clamp(WaitSeconds, 0.0, 300.0);
		FBridgeJobSnapshot Snapshot;
		if (JobId.IsEmpty() || !JobManager.IsValid()
			|| !JobManager->WaitForJob(JobId, WaitSeconds, Snapshot))
		{
			Response->SetBoolField(TEXT("success"), false);
			Response->SetStringField(TEXT("output"), TEXT(""));
			Response->SetStringField(TEXT("error"), JobId.IsEmpty()
				? TEXT("missing 'job_id' field")
				: FString::Printf(TEXT("unknown job '%s'"), *JobId));
		}
		else
		{
			Response->SetBoolField(TEXT("success"), true);
			Response->SetStringField(TEXT("output"), JobId);
			Response->SetStringField(TEXT("error"), TEXT(""));
			Response->SetBoolField(TEXT("wait_completed"), IsBridgeJobTerminal(Snapshot.State));
			AddJobSnapshotFields(Snapshot, Response);
		}
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
	}
	else if (Command == TEXT("cancel_job"))
	{
		FString JobId;
		Request->TryGetStringField(TEXT("job_id"), JobId);
		FBridgeJobSnapshot Snapshot;
		FString CancelError;
		if (JobId.IsEmpty() || !JobManager.IsValid()
			|| !JobManager->Cancel(JobId, Snapshot, CancelError))
		{
			Response->SetBoolField(TEXT("success"), false);
			Response->SetStringField(TEXT("output"), TEXT(""));
			Response->SetStringField(TEXT("error"), JobId.IsEmpty()
				? TEXT("missing 'job_id' field")
				: CancelError);
		}
		else
		{
			Response->SetBoolField(TEXT("success"), true);
			Response->SetStringField(TEXT("output"), JobId);
			Response->SetStringField(TEXT("error"), TEXT(""));
			AddJobSnapshotFields(Snapshot, Response);
		}
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
	}
	else if (Command == TEXT("list_jobs"))
	{
		double LimitNumber = 50.0;
		Request->TryGetNumberField(TEXT("limit"), LimitNumber);
		const int32 Limit = FMath::Clamp(FMath::RoundToInt(LimitNumber), 1, 200);
		TArray<TSharedPtr<FJsonValue>> JobsJson;
		if (JobManager.IsValid())
		{
			for (const FBridgeJobSnapshot& Snapshot : JobManager->ListSnapshots(Limit))
			{
				TSharedRef<FJsonObject> JobJson = MakeShared<FJsonObject>();
				AddJobSnapshotFields(Snapshot, JobJson, false);
				JobsJson.Add(MakeShared<FJsonValueObject>(JobJson));
			}
		}
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("output"), TEXT(""));
		Response->SetStringField(TEXT("error"), TEXT(""));
		Response->SetArrayField(TEXT("jobs"), JobsJson);
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
	}
	else if (Command == TEXT("submit_job"))
	{
		FString Script;
		if (!bEditorReady || bPieTransitionActive)
		{
			Response->SetBoolField(TEXT("success"), false);
			Response->SetStringField(TEXT("output"), TEXT(""));
			Response->SetStringField(TEXT("error"), !bEditorReady
				? TEXT("editor not ready - main frame not yet created")
				: TEXT("editor in PIE transition - retry in a moment"));
		}
		else if (!Request->TryGetStringField(TEXT("script"), Script))
		{
			Response->SetBoolField(TEXT("success"), false);
			Response->SetStringField(TEXT("output"), TEXT(""));
			Response->SetStringField(TEXT("error"), TEXT("missing 'script' field"));
		}
		else
		{
			double QueueDeadline = 300.0;
			Request->TryGetNumberField(TEXT("queue_timeout"), QueueDeadline);
			QueueDeadline = FMath::Clamp(QueueDeadline, 0.1, 3600.0);
			FString IdempotencyKey;
			Request->TryGetStringField(TEXT("idempotency_key"), IdempotencyKey);
			FString PollScript;
			Request->TryGetStringField(TEXT("poll_script"), PollScript);
			double PollInterval = 0.25;
			Request->TryGetNumberField(TEXT("poll_interval"), PollInterval);
			double RunTimeout = 300.0;
			Request->TryGetNumberField(TEXT("run_timeout"), RunTimeout);
			FBridgeJobSubmitResult Submit = JobManager->Submit(
				Script, RequestId, QueueDeadline, IdempotencyKey,
				PollScript, PollInterval, RunTimeout);
			if (!Submit.Job.IsValid())
			{
				Response->SetBoolField(TEXT("success"), false);
				Response->SetStringField(TEXT("output"), TEXT(""));
				Response->SetStringField(TEXT("error"), Submit.Error);
			}
			else
			{
				FBridgeJobSnapshot Snapshot;
				JobManager->GetSnapshot(Submit.Job->JobId, Snapshot);
				Response->SetBoolField(TEXT("success"), true);
				Response->SetStringField(TEXT("output"), Submit.Job->JobId);
				Response->SetStringField(TEXT("error"), TEXT(""));
				Response->SetBoolField(TEXT("deduplicated"), Submit.bDeduplicated);
				AddJobSnapshotFields(Snapshot, Response, false);
			}
		}
		Response->SetBoolField(TEXT("ready"), (bool)bEditorReady);
	}
	else if (!bEditorReady)
	{
		// Reject Python exec while the editor is still initializing.
		// Dispatching to the GameThread during SlateRHIRenderer::CreateViewport's
		// render-fence can crash the editor, so fail fast with a clear signal.
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("output"), TEXT(""));
		Response->SetStringField(TEXT("error"), TEXT("editor not ready — main frame not yet created"));
		Response->SetBoolField(TEXT("ready"), false);
	}
	else if (bPieTransitionActive)
	{
		// Reject exec during Begin/EndPIE because editor subsystems (world,
		// GAS, anim) are torn down and rebuilt — Python running in that
		// window reliably crashes (item #11).
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("output"), TEXT(""));
		Response->SetStringField(TEXT("error"), TEXT("editor in PIE transition — retry in a moment"));
		Response->SetBoolField(TEXT("ready"), true);
	}
	else
	{
		// Execute Python script — serialized through the GameThread ticker queue.
		FString Script;
		if (!Request->TryGetStringField(TEXT("script"), Script))
		{
			Response->SetBoolField(TEXT("success"), false);
			Response->SetStringField(TEXT("output"), TEXT(""));
			Response->SetStringField(TEXT("error"), TEXT("missing 'script' field"));
			Response->SetBoolField(TEXT("ready"), true);
		}
		else
		{
			double TimeoutNum = 30.0;
			Request->TryGetNumberField(TEXT("timeout"), TimeoutNum);
			double QueueTimeoutNum = TimeoutNum;
			Request->TryGetNumberField(TEXT("queue_timeout"), QueueTimeoutNum);
			double WaitTimeoutNum = TimeoutNum;
			Request->TryGetNumberField(TEXT("wait_timeout"), WaitTimeoutNum);
			const float QueueTimeout = FMath::Clamp((float)QueueTimeoutNum, 0.1f, 3600.0f);
			const float WaitTimeout = FMath::Clamp((float)WaitTimeoutNum, 0.0f, 300.0f);
			FString IdempotencyKey;
			Request->TryGetStringField(TEXT("idempotency_key"), IdempotencyKey);

			// Capture a preview of the script for the call-log ring. Cap at
			// ~80 chars; newlines collapse to spaces so the log stays
			// single-line-scannable.
			CallRecord.ScriptPreview = Script.Left(80).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(""));

			const double ExecT0 = FPlatformTime::Seconds();
			TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> Job;
			bool bClientWaitTimedOut = false;
			bool bDeduplicated = false;
			FBridgeJobResult Result = EnqueueAndWaitForExec(
				Script,
				QueueTimeout,
				WaitTimeout,
				RequestId,
				IdempotencyKey,
				Job,
				bClientWaitTimedOut,
				bDeduplicated);
			const double ExecMs = (FPlatformTime::Seconds() - ExecT0) * 1000.0;
			CallRecord.ExecDurationMs = ExecMs;

			UE_LOG(LogUnrealBridge, Log,
				TEXT("[%s] exec id=%s ok=%s out=%dB err=%dB took=%.1fms"),
				*EndpointStr, *RequestId,
				Result.bSuccess ? TEXT("true") : TEXT("false"),
				Result.Output.Len(), Result.Error.Len(), ExecMs);

			Response->SetBoolField(TEXT("success"), Result.bSuccess);
			Response->SetStringField(TEXT("output"), Result.Output);
			Response->SetStringField(TEXT("error"), Result.Error);
			Response->SetBoolField(TEXT("client_wait_timed_out"), bClientWaitTimedOut);
			Response->SetBoolField(TEXT("deduplicated"), bDeduplicated);
			if (Job.IsValid() && JobManager.IsValid())
			{
				FBridgeJobSnapshot Snapshot;
				if (JobManager->GetSnapshot(Job->JobId, Snapshot))
				{
					AddJobSnapshotFields(Snapshot, Response, !bClientWaitTimedOut);
				}
			}
			if (!Result.ErrorCode.IsEmpty())
			{
				TSharedRef<FJsonObject> ErrorInfo = MakeShared<FJsonObject>();
				ErrorInfo->SetStringField(TEXT("code"), Result.ErrorCode);
				ErrorInfo->SetStringField(TEXT("message"), Result.Error);
				ErrorInfo->SetStringField(TEXT("phase"), Result.Phase);
				ErrorInfo->SetBoolField(TEXT("retryable"), Result.bRetryable);
				ErrorInfo->SetStringField(TEXT("side_effect_state"), Result.SideEffectState);
				if (Job.IsValid())
				{
					ErrorInfo->SetStringField(TEXT("trace_id"), Job->TraceId);
				}
				Response->SetObjectField(TEXT("error_info"), ErrorInfo);
			}
			Response->SetBoolField(TEXT("ready"), true);
		}
	}

	// Mirror the authoritative Response fields into the call record so
	// every branch (ping / resume / exec / rejected-not-ready / etc.)
	// logs consistent success/output/error sizes without bespoke wiring.
	{
		bool bOk = false;
		Response->TryGetBoolField(TEXT("success"), bOk);
		CallRecord.bSuccess = bOk;
		FString OutStr, ErrStr;
		Response->TryGetStringField(TEXT("output"), OutStr);
		Response->TryGetStringField(TEXT("error"), ErrStr);
		CallRecord.OutputBytes = OutStr.Len();
		CallRecord.ErrorBytes = ErrStr.Len();
		if (!bOk)
		{
			CallRecord.ErrorPreview = ErrStr.Left(200);
		}
	}

	// 5. Serialize and send response
	FString ResponseStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
	FJsonSerializer::Serialize(Response, Writer);

	FTCHARToUTF8 Utf8Response(*ResponseStr);
	int32 ResponseLen = Utf8Response.Length();

	uint8 RespLenBuf[4];
	RespLenBuf[0] = (ResponseLen >> 24) & 0xFF;
	RespLenBuf[1] = (ResponseLen >> 16) & 0xFF;
	RespLenBuf[2] = (ResponseLen >> 8) & 0xFF;
	RespLenBuf[3] = ResponseLen & 0xFF;

	if (!SendAll(ClientSocket, RespLenBuf, 4))
	{
		UE_LOG(LogUnrealBridge, Warning,
			TEXT("[%s] send response header failed (id=%s cmd=%s)"),
			*EndpointStr, *RequestId, *Command);
		return;
	}
	if (!SendAll(ClientSocket, (const uint8*)Utf8Response.Get(), ResponseLen))
	{
		UE_LOG(LogUnrealBridge, Warning,
			TEXT("[%s] send response body failed (id=%s cmd=%s len=%d)"),
			*EndpointStr, *RequestId, *Command, ResponseLen);
		return;
	}

	UE_LOG(LogUnrealBridge, Verbose,
		TEXT("[%s] done id=%s total=%.1fms"),
		*EndpointStr, *RequestId, (FPlatformTime::Seconds() - T0) * 1000.0);
}

void FUnrealBridgeServer::AddJobSnapshotFields(
	const FBridgeJobSnapshot& Snapshot,
	const TSharedRef<FJsonObject>& Response,
	bool bIncludeResult) const
{
	const double Now = FPlatformTime::Seconds();
	const double QueueEnd = Snapshot.StartedSeconds > 0.0
		? Snapshot.StartedSeconds
		: (Snapshot.FinishedSeconds > 0.0 ? Snapshot.FinishedSeconds : Now);
	const double RunEnd = Snapshot.FinishedSeconds > 0.0 ? Snapshot.FinishedSeconds : Now;

	Response->SetStringField(TEXT("job_id"), Snapshot.JobId);
	Response->SetStringField(TEXT("trace_id"), Snapshot.TraceId);
	Response->SetStringField(TEXT("job_state"), LexToString(Snapshot.State));
	Response->SetBoolField(TEXT("terminal"), IsBridgeJobTerminal(Snapshot.State));
	Response->SetBoolField(TEXT("cancel_requested"), Snapshot.bCancelRequested);
	Response->SetBoolField(TEXT("polling"), Snapshot.bPolling);
	Response->SetNumberField(TEXT("step_count"), Snapshot.StepCount);
	Response->SetNumberField(TEXT("queue_ms"),
		FMath::Max(0.0, QueueEnd - Snapshot.CreatedSeconds) * 1000.0);
	Response->SetNumberField(TEXT("run_ms"), Snapshot.StartedSeconds > 0.0
		? FMath::Max(0.0, RunEnd - Snapshot.StartedSeconds) * 1000.0
		: 0.0);
	Response->SetNumberField(TEXT("deadline_remaining_ms"),
		FMath::Max(0.0, Snapshot.DeadlineSeconds - Now) * 1000.0);
	Response->SetNumberField(TEXT("run_deadline_remaining_ms"), Snapshot.RunDeadlineSeconds > 0.0
		? FMath::Max(0.0, Snapshot.RunDeadlineSeconds - Now) * 1000.0
		: 0.0);
	Response->SetNumberField(TEXT("next_poll_ms"), Snapshot.NextPollSeconds > 0.0
		? FMath::Max(0.0, Snapshot.NextPollSeconds - Now) * 1000.0
		: 0.0);
	if (!Snapshot.IdempotencyKey.IsEmpty())
	{
		Response->SetStringField(TEXT("idempotency_key"), Snapshot.IdempotencyKey);
	}

	if (bIncludeResult && (IsBridgeJobTerminal(Snapshot.State)
		|| (Snapshot.bPolling && Snapshot.State == EBridgeJobState::Running)))
	{
		TSharedRef<FJsonObject> ResultJson = MakeShared<FJsonObject>();
		ResultJson->SetBoolField(TEXT("success"), Snapshot.Result.bSuccess);
		ResultJson->SetStringField(TEXT("output"), Snapshot.Result.Output);
		ResultJson->SetStringField(TEXT("error"), Snapshot.Result.Error);
		ResultJson->SetStringField(TEXT("error_code"), Snapshot.Result.ErrorCode);
		ResultJson->SetStringField(TEXT("phase"), Snapshot.Result.Phase);
		ResultJson->SetBoolField(TEXT("retryable"), Snapshot.Result.bRetryable);
		ResultJson->SetStringField(TEXT("side_effect_state"), Snapshot.Result.SideEffectState);
		Response->SetObjectField(TEXT("job_result"), ResultJson);
	}
}

// ─────────────────────────────────────────────────────────────
// Python execution pipeline
// ─────────────────────────────────────────────────────────────
//
// Worker threads submit durable FBridgeJob records. A single FTSTicker
// consumer on the GameThread claims at most one job per frame. Client wait
// deadlines are independent of job lifetime, and queue deadlines are checked
// atomically before Python can run.
// ─────────────────────────────────────────────────────────────

FBridgeJobResult FUnrealBridgeServer::EnqueueAndWaitForExec(
	const FString& Script,
	float QueueDeadlineSeconds,
	float ClientWaitSeconds,
	const FString& RequestId,
	const FString& IdempotencyKey,
	TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& OutJob,
	bool& bOutClientWaitTimedOut,
	bool& bOutDeduplicated)
{
	bOutClientWaitTimedOut = false;
	bOutDeduplicated = false;
	FBridgeJobResult Result;
	if (!JobManager.IsValid())
	{
		Result.Error = TEXT("job manager is not available");
		Result.ErrorCode = TEXT("JOB_MANAGER_UNAVAILABLE");
		Result.Phase = TEXT("submit");
		Result.bRetryable = true;
		Result.SideEffectState = TEXT("none");
		return Result;
	}

	FBridgeJobSubmitResult Submit = JobManager->Submit(
		Script, RequestId, QueueDeadlineSeconds, IdempotencyKey);
	if (!Submit.Job.IsValid())
	{
		Result.Error = Submit.Error.IsEmpty() ? TEXT("job submission failed") : Submit.Error;
		Result.ErrorCode = TEXT("JOB_SUBMIT_FAILED");
		Result.Phase = TEXT("submit");
		Result.SideEffectState = TEXT("none");
		return Result;
	}

	OutJob = Submit.Job;
	bOutDeduplicated = Submit.bDeduplicated;
	if (!JobManager->Wait(OutJob, ClientWaitSeconds))
	{
		bOutClientWaitTimedOut = true;
		Result.Error = FString::Printf(
			TEXT("client wait timed out after %.1fs; job continues and can be queried by job_id"),
			ClientWaitSeconds);
		Result.ErrorCode = TEXT("CLIENT_WAIT_TIMEOUT");
		Result.Phase = TEXT("wait");
		Result.bRetryable = true;
		Result.SideEffectState = TEXT("unknown");
		return Result;
	}

	FBridgeJobSnapshot Snapshot;
	if (!JobManager->GetSnapshot(OutJob->JobId, Snapshot))
	{
		Result.Error = TEXT("job result was evicted before it could be read");
		Result.ErrorCode = TEXT("JOB_RESULT_UNAVAILABLE");
		Result.Phase = TEXT("result");
		Result.bRetryable = true;
		Result.SideEffectState = TEXT("unknown");
		return Result;
	}
	return Snapshot.Result;
}

bool FUnrealBridgeServer::TickConsumeQueue(float /*DeltaTime*/)
{
	if (!bIsRunning)
	{
		return true; // still ticking; will be removed by Stop()
	}
	if (bExecInFlight)
	{
		return true; // belt-and-suspenders guard against ticker reentrancy
	}

	if (!JobManager.IsValid())
	{
		return true;
	}

	TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> Job = JobManager->ClaimNextJob();
	if (!Job.IsValid())
	{
		return true;
	}

	bExecInFlight = true;
	const bool bPollStep = Job->IsPollStep();
	BridgeChangeSetRuntime::BeginJob(Job->JobId);
	FBridgeJobResult Result = DoPythonExec(Job->GetExecutableScript());
	bool bPollComplete = false;
	if (Job->IsPolling() && bPollStep && Result.bSuccess)
	{
		DecodePollingStepResult(Result, bPollComplete);
	}
	BridgeChangeSetRuntime::EndJob(Job->JobId, Result.bSuccess);
	if (Job->IsPolling())
	{
		JobManager->CompletePollingStep(Job, MoveTemp(Result), bPollComplete);
	}
	else
	{
		JobManager->Complete(Job, MoveTemp(Result));
	}
	bExecInFlight = false;
	return true;
}

FBridgeJobResult FUnrealBridgeServer::DoPythonExec(const FString& Script)
{
	FBridgeJobResult Result;

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		Result.bSuccess = false;
		Result.Error = TEXT("PythonScriptPlugin is not available");
		Result.ErrorCode = TEXT("PYTHON_PLUGIN_UNAVAILABLE");
		Result.Phase = TEXT("execute");
		Result.bRetryable = false;
		Result.SideEffectState = TEXT("none");
		return Result;
	}

	// Wrap user script to capture stdout/stderr in Python-land,
	// then print the captured content so ExecPythonCommandEx can collect it via LogOutput.
	//
	// We base64-encode the user script instead of inlining it inside a
	// Python triple-quoted string. Triple-quoted strings in Python don't
	// honour backslash-escape for quotes, so any user script containing
	// `"""` (docstrings, embedded SQL/markdown) would break the old
	// escape scheme. Base64 sidesteps quoting entirely.
	const FTCHARToUTF8 ScriptUtf8(*Script);
	const FString ScriptB64 = FBase64::Encode(
		reinterpret_cast<const uint8*>(ScriptUtf8.Get()),
		ScriptUtf8.Length());

	// Output is base64-encoded before being printed because UE's Python stdout
	// shim mangles non-ASCII characters into U+FFFD on the way back to FString.
	// base64 is pure ASCII, so it survives the shim intact; the C++ side
	// decodes it back to UTF-8 bytes and rebuilds an FString via FUTF8ToTCHAR.
	FString WrappedScript = FString::Printf(TEXT(
		"import base64 as _b64, sys, io as _io, traceback as _tb\n"
		"_src = _b64.b64decode('%s').decode('utf-8')\n"
		"_ub_out, _ub_err = _io.StringIO(), _io.StringIO()\n"
		"_ub_old = sys.stdout, sys.stderr\n"
		"sys.stdout, sys.stderr = _ub_out, _ub_err\n"
		"try:\n"
		"    exec(compile(_src, '<unrealbridge>', 'exec'))\n"
		"except Exception:\n"
		"    sys.stderr.write(_tb.format_exc())\n"
		"finally:\n"
		"    sys.stdout, sys.stderr = _ub_old\n"
		"    _ub_o, _ub_e = _ub_out.getvalue(), _ub_err.getvalue()\n"
		"    _ub_out.close(); _ub_err.close()\n"
		"    _eo = _b64.b64encode(_ub_o.encode('utf-8')).decode('ascii') if _ub_o else ''\n"
		"    _ee = _b64.b64encode(_ub_e.encode('utf-8')).decode('ascii') if _ub_e else ''\n"
		"    print('__UB_B64__' + _eo + '|' + _ee + '__UB_END__')\n"
	), *ScriptB64);

	FPythonCommandEx CommandEx;
	CommandEx.Command = WrappedScript;
	CommandEx.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	CommandEx.FileExecutionScope = EPythonFileExecutionScope::Public;

	bool bExecSuccess = PythonPlugin->ExecPythonCommandEx(CommandEx);

	FString FullOutput;
	for (const FPythonLogOutputEntry& Entry : CommandEx.LogOutput)
	{
		FullOutput += Entry.Output + TEXT("\n");
	}
	if (FullOutput.IsEmpty() && !CommandEx.CommandResult.IsEmpty())
	{
		FullOutput = CommandEx.CommandResult;
	}

	// Wrapper emits `__UB_B64__<out_b64>|<err_b64>__UB_END__`. Decode each
	// half from base64 → UTF-8 bytes → FString via FUTF8ToTCHAR. Fallback to
	// raw FullOutput-as-Error when the envelope is missing (catastrophic
	// failure inside the wrapper itself, before the print could run).
	const FString EnvBegin = TEXT("__UB_B64__");
	const FString EnvEnd = TEXT("__UB_END__");
	const int32 BeginIdx = FullOutput.Find(EnvBegin);
	const int32 EndIdx = (BeginIdx != INDEX_NONE)
		? FullOutput.Find(EnvEnd, ESearchCase::CaseSensitive, ESearchDir::FromStart, BeginIdx + EnvBegin.Len())
		: INDEX_NONE;

	if (BeginIdx != INDEX_NONE && EndIdx != INDEX_NONE)
	{
		const int32 PayloadStart = BeginIdx + EnvBegin.Len();
		const FString Payload = FullOutput.Mid(PayloadStart, EndIdx - PayloadStart);

		int32 SepIdx = INDEX_NONE;
		Payload.FindChar(TEXT('|'), SepIdx);
		const FString OutB64 = (SepIdx != INDEX_NONE) ? Payload.Left(SepIdx) : Payload;
		const FString ErrB64 = (SepIdx != INDEX_NONE) ? Payload.Mid(SepIdx + 1) : FString();

		auto DecodeB64ToUtf8FString = [](const FString& B64) -> FString
		{
			if (B64.IsEmpty()) return FString();
			TArray<uint8> Bytes;
			if (!FBase64::Decode(B64, Bytes) || Bytes.Num() == 0) return FString();
			// Match the inbound-decode pattern used at line ~380 — FUTF8ToTCHAR
			// with explicit length, then construct FString from .Get() + .Length()
			// so we don't accidentally hit FString's ANSI-interpret constructor.
			FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
			return FString(Conv.Length(), Conv.Get());
		};

		Result.Output = DecodeB64ToUtf8FString(OutB64);
		Result.Error = DecodeB64ToUtf8FString(ErrB64);
		Result.bSuccess = bExecSuccess && Result.Error.IsEmpty();
	}
	else
	{
		// Wrapper crashed before emitting the envelope — surface whatever UE captured.
		Result.Output = FString();
		Result.Error = FullOutput;
		Result.bSuccess = false;
	}

	Result.Output.TrimEndInline();
	Result.Error.TrimEndInline();
	Result.ErrorCode = Result.bSuccess ? FString() : TEXT("PYTHON_EXEC_FAILED");
	Result.Phase = TEXT("execute");
	Result.bRetryable = false;
	Result.SideEffectState = Result.bSuccess ? TEXT("complete") : TEXT("unknown");
	return Result;
}

// ─────────────────────────────────────────────────────────────
// Socket helpers
// ─────────────────────────────────────────────────────────────

bool FUnrealBridgeServer::RecvAll(FSocket* Socket, uint8* Buffer, int32 NumBytes, float TimeoutSeconds)
{
	int32 BytesRead = 0;
	const double StartTime = FPlatformTime::Seconds();
	int32 ZeroReadTries = 0;

	while (BytesRead < NumBytes)
	{
		if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
		{
			return false;
		}

		// Select-level readiness probe. Without this, UE's FSocket::Recv on
		// a just-accepted FTcpListener socket can return Read=0 before the
		// kernel has delivered any data, which we'd mis-interpret as a FIN
		// and close the connection — producing WSAECONNABORTED 10053 on
		// the client mid-recv. Wait() reports readable only once real data
		// (or a real FIN) is present.
		const bool bReadable = Socket->Wait(
			ESocketWaitConditions::WaitForRead,
			FTimespan::FromMilliseconds(50));
		if (!bReadable)
		{
			continue; // Keep checking until TimeoutSeconds elapses.
		}

		uint32 PendingBytes = 0;
		const bool bHasPending = Socket->HasPendingData(PendingBytes);

		int32 Read = 0;
		const bool bRecvOk = Socket->Recv(Buffer + BytesRead, NumBytes - BytesRead, Read);
		if (bRecvOk)
		{
			if (Read == 0)
			{
				// Confirm genuine FIN: no pending kernel data AND a small retry budget
				// exhausted. Spurious zero-reads (UE socket edge case) are rare but
				// documented above.
				if (bHasPending && PendingBytes > 0)
				{
					++ZeroReadTries;
					FPlatformProcess::Sleep(0.001f);
					continue;
				}
				if (Socket->GetConnectionState() == SCS_Connected && ZeroReadTries < 5)
				{
					++ZeroReadTries;
					FPlatformProcess::Sleep(0.002f);
					continue;
				}
				return false;
			}
			BytesRead += Read;
			ZeroReadTries = 0;
		}
		else
		{
			if (Socket->GetConnectionState() != SCS_Connected)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.001f);
		}
	}

	return true;
}

bool FUnrealBridgeServer::SendAll(
	FSocket* Socket,
	const uint8* Buffer,
	int32 NumBytes,
	float TimeoutSeconds)
{
	int32 BytesSent = 0;
	int32 ZeroSendCount = 0;
	const double Deadline = FPlatformTime::Seconds() + FMath::Max(0.1f, TimeoutSeconds);

	while (BytesSent < NumBytes)
	{
		if (FPlatformTime::Seconds() >= Deadline
			|| Socket->GetConnectionState() != SCS_Connected)
		{
			return false;
		}

		if (!Socket->Wait(
			ESocketWaitConditions::WaitForWrite,
			FTimespan::FromMilliseconds(50)))
		{
			continue;
		}

		int32 Sent = 0;
		if (!Socket->Send(Buffer + BytesSent, NumBytes - BytesSent, Sent))
		{
			return false;
		}
		if (Sent <= 0)
		{
			if (++ZeroSendCount >= 8)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.001f);
			continue;
		}
		ZeroSendCount = 0;
		BytesSent += Sent;
	}

	return true;
}

