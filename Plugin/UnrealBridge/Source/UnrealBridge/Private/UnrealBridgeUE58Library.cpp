#include "UnrealBridgeUE58Library.h"

#include "UnrealBridgeRegistryLibrary.h"
#include "UnrealBridgeVersion.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
#include "ToolsetRegistry/ToolCallAsyncResultString.h"
#include "ToolsetRegistry/Toolset.h"
#include "ToolsetRegistry/ToolsetRegistry.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogUnrealBridgeUE58, Log, All);

namespace
{
	FString UE58_Serialize(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Object, Writer);
		return Json;
	}

	TSharedRef<FJsonObject> UE58_BaseResult(bool bSuccess)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetStringField(TEXT("provider"), TEXT("EpicToolsetRegistry"));
		Result->SetStringField(TEXT("engine_min"), UnrealBridgeVersion::UE58ToolsetMinimumEngine);
		return Result;
	}

	FString UE58_Error(const FString& Code, const FString& Message, bool bComplete = true)
	{
		TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
		Result->SetBoolField(TEXT("complete"), bComplete);
		Result->SetStringField(TEXT("error_code"), Code);
		Result->SetStringField(TEXT("error"), Message);
		Result->SetBoolField(TEXT("retryable"), false);
		Result->SetStringField(TEXT("side_effect_state"), TEXT("none"));
		return UE58_Serialize(Result);
	}

#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	struct FUE58PendingCall
	{
		TStrongObjectPtr<UToolCallAsyncResultString> Result;
		double CreatedSeconds = 0.0;
		FString ToolsetName;
		FString ToolName;
		FString Risk;

		explicit FUE58PendingCall(UToolCallAsyncResultString* InResult)
			: Result(InResult)
		{
		}
	};

	TMap<FString, TSharedPtr<FUE58PendingCall>> GUE58PendingCalls;
	TSharedPtr<UE::ToolsetRegistry::FToolset> GUE58BridgeToolset;

	bool UE58_IsApprovedReadOnlyTool(
		const FString& ToolsetName,
		const FString& ToolName,
		FString& OutError)
	{
		const auto Subsystem = UToolsetRegistrySubsystem::Get(TEXT("UnrealBridge official tool policy"));
		if (Subsystem.HasError())
		{
			OutError = Subsystem.GetError();
			return false;
		}

		FString FindError;
		const TSharedPtr<UE::ToolsetRegistry::FToolset> Toolset =
			Subsystem.GetValue()->ToolsetRegistry.Find(ToolsetName, false, &FindError);
		if (!Toolset.IsValid())
		{
			OutError = FindError.IsEmpty()
				? FString::Printf(TEXT("Unknown toolset '%s'"), *ToolsetName)
				: FindError;
			return false;
		}

		TSharedPtr<FJsonObject> Schema;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Toolset->GetJsonSchema());
		if (!FJsonSerializer::Deserialize(Reader, Schema) || !Schema.IsValid())
		{
			OutError = FString::Printf(TEXT("Toolset '%s' returned an invalid JSON schema"), *ToolsetName);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
		if (!Schema->TryGetArrayField(TEXT("tools"), Tools) || !Tools)
		{
			OutError = FString::Printf(TEXT("Toolset '%s' schema has no tools array"), *ToolsetName);
			return false;
		}

		const FString QualifiedName = ToolsetName + TEXT(".") + ToolName;
		for (const TSharedPtr<FJsonValue>& ToolValue : *Tools)
		{
			if (!ToolValue.IsValid() || ToolValue->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> Tool = ToolValue->AsObject();
			FString SchemaName;
			Tool->TryGetStringField(TEXT("name"), SchemaName);
			if (SchemaName != ToolName && SchemaName != QualifiedName)
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* Annotations = nullptr;
			if (!Tool->TryGetObjectField(TEXT("annotations"), Annotations) || !Annotations)
			{
				OutError = FString::Printf(
					TEXT("Official tool '%s' has no side-effect annotations; use an audited typed UnrealBridge wrapper"),
					*QualifiedName);
				return false;
			}
			bool bReadOnly = false;
			bool bDestructive = false;
			const bool bHasReadOnly = (*Annotations)->TryGetBoolField(TEXT("readOnlyHint"), bReadOnly);
			(*Annotations)->TryGetBoolField(TEXT("destructiveHint"), bDestructive);
			if (!bHasReadOnly || !bReadOnly || bDestructive)
			{
				OutError = FString::Printf(
					TEXT("Official tool '%s' is not explicitly approved as non-destructive read-only"),
					*QualifiedName);
				return false;
			}
			return true;
		}

		OutError = FString::Printf(TEXT("Unknown official tool '%s'"), *QualifiedName);
		return false;
	}

	void UE58_CleanupCalls()
	{
		const double Now = FPlatformTime::Seconds();
		for (auto It = GUE58PendingCalls.CreateIterator(); It; ++It)
		{
			const TSharedPtr<FUE58PendingCall>& Pending = It.Value();
			if (!Pending.IsValid() || !Pending->Result.IsValid()
				|| Now - Pending->CreatedSeconds > 3600.0)
			{
				It.RemoveCurrent();
			}
		}
	}

	class FUnrealBridgeMetaToolset final : public UE::ToolsetRegistry::FToolset
	{
	public:
		virtual FString GetToolsetName() const override { return TEXT("UnrealBridge"); }
		virtual FString GetToolsetVersion() const override { return UnrealBridgeVersion::Plugin; }
		virtual FString GetToolsetDescription() const override
		{
			return TEXT("Read-only UnrealBridge 3 capability and schema discovery. Mutations remain on the authenticated UnrealBridge Job endpoint.");
		}

	protected:
		virtual TFuture<TValueOrError<FString, FString>> ExecuteToolInternal(
			const FString& ToolName, const FString&) override
		{
			if (ToolName == TEXT("Capabilities"))
			{
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("plugin_version"), UnrealBridgeVersion::Plugin);
				Result->SetNumberField(TEXT("protocol_version"), UnrealBridgeVersion::Protocol);
				Result->SetStringField(TEXT("registry_hash"), UUnrealBridgeRegistryLibrary::GetToolRegistryHash());
				Result->SetStringField(TEXT("provider"), TEXT("UnrealBridge"));
				Result->SetStringField(TEXT("mutation_transport"), TEXT("authenticated-durable-job"));
				return MakeFulfilledPromise<TValueOrError<FString, FString>>(
					MakeValue(UE58_Serialize(Result))).GetFuture();
			}
			if (ToolName == TEXT("RegistryHash"))
			{
				return MakeFulfilledPromise<TValueOrError<FString, FString>>(
					MakeValue(UUnrealBridgeRegistryLibrary::GetToolRegistryHash())).GetFuture();
			}
			return MakeFulfilledPromise<TValueOrError<FString, FString>>(
				MakeError(FString::Printf(TEXT("Unknown UnrealBridge tool '%s'"), *ToolName))).GetFuture();
		}

		virtual FString GetJsonSchemaInternal() const override
		{
			return TEXT(R"json({"name":"UnrealBridge","version":"3.0.0","description":"Read-only UnrealBridge capability discovery; mutations require the authenticated durable Job endpoint.","tools":[{"name":"UnrealBridge.Capabilities","description":"Return UnrealBridge version, protocol, registry hash, provider and mutation transport.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true,"destructiveHint":false}},{"name":"UnrealBridge.RegistryHash","description":"Return the strict native tool registry hash.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true,"destructiveHint":false}}]})json");
		}
	};
#endif
}

bool UUnrealBridgeUE58Library::IsOfficialToolsetRegistryAvailable()
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	return IsInGameThread() && UToolsetRegistry::IsAvailable();
#else
	return false;
#endif
}

FString UUnrealBridgeUE58Library::GetOfficialToolsetCatalogJson()
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!IsInGameThread())
	{
		return UE58_Error(TEXT("GAME_THREAD_REQUIRED"), TEXT("ToolsetRegistry discovery must run on the GameThread"));
	}
	if (!UToolsetRegistry::IsAvailable())
	{
		return UE58_Error(TEXT("TOOLSET_REGISTRY_UNAVAILABLE"), TEXT("UE 5.8 ToolsetRegistry is not available"));
	}
	return UToolsetRegistry::GetAllToolsetJsonSchemas();
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Official ToolsetRegistry federation requires UE 5.8 or newer"));
#endif
}

FString UUnrealBridgeUE58Library::GetOfficialToolsetSchemaJson(const FString& ToolsetName)
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!IsInGameThread())
	{
		return UE58_Error(TEXT("GAME_THREAD_REQUIRED"), TEXT("ToolsetRegistry discovery must run on the GameThread"));
	}
	const auto Subsystem = UToolsetRegistrySubsystem::Get(TEXT("UnrealBridge GetOfficialToolsetSchemaJson"));
	if (Subsystem.HasError())
	{
		return UE58_Error(TEXT("TOOLSET_REGISTRY_UNAVAILABLE"), Subsystem.GetError());
	}
	FString FindError;
	const TSharedPtr<UE::ToolsetRegistry::FToolset> Toolset =
		Subsystem.GetValue()->ToolsetRegistry.Find(ToolsetName, false, &FindError);
	if (!Toolset.IsValid())
	{
		return UE58_Error(TEXT("UNKNOWN_TOOLSET"), FindError.IsEmpty()
			? FString::Printf(TEXT("Unknown toolset '%s'"), *ToolsetName) : FindError);
	}
	return Toolset->GetJsonSchema();
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Official ToolsetRegistry federation requires UE 5.8 or newer"));
#endif
}

FString UUnrealBridgeUE58Library::StartOfficialToolsetCall(
	const FString& CallId,
	const FString& ToolsetName,
	const FString& ToolName,
	const FString& JsonInput)
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!IsInGameThread())
	{
		return UE58_Error(TEXT("GAME_THREAD_REQUIRED"), TEXT("Official tool calls must start on the GameThread"));
	}
	if (CallId.IsEmpty() || ToolsetName.IsEmpty() || ToolName.IsEmpty())
	{
		return UE58_Error(TEXT("INVALID_ARGUMENT"), TEXT("call_id, toolset_name and tool_name are required"));
	}
	FString PolicyError;
	if (!UE58_IsApprovedReadOnlyTool(ToolsetName, ToolName, PolicyError))
	{
		return UE58_Error(TEXT("OFFICIAL_TOOL_NOT_APPROVED"), PolicyError);
	}
	TSharedPtr<FJsonObject> InputObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonInput.IsEmpty() ? TEXT("{}") : JsonInput);
	if (!FJsonSerializer::Deserialize(Reader, InputObject) || !InputObject.IsValid())
	{
		return UE58_Error(TEXT("INVALID_JSON_INPUT"), TEXT("json_input must be a JSON object"));
	}
	UE58_CleanupCalls();
	if (GUE58PendingCalls.Contains(CallId))
	{
		return UE58_Error(TEXT("DUPLICATE_CALL_ID"), FString::Printf(TEXT("call_id '%s' is already tracked"), *CallId));
	}
	UToolCallAsyncResultString* AsyncResult = UToolsetRegistry::ExecuteTool(
		ToolsetName, ToolName, JsonInput.IsEmpty() ? TEXT("{}") : JsonInput);
	if (!AsyncResult)
	{
		return UE58_Error(TEXT("TOOLSET_START_FAILED"), TEXT("ToolsetRegistry returned no async result"));
	}
	TSharedPtr<FUE58PendingCall> Pending = MakeShared<FUE58PendingCall>(AsyncResult);
	Pending->CreatedSeconds = FPlatformTime::Seconds();
	Pending->ToolsetName = ToolsetName;
	Pending->ToolName = ToolName;
	Pending->Risk = TEXT("ReadOnly");
	GUE58PendingCalls.Add(CallId, Pending);

	TSharedRef<FJsonObject> Result = UE58_BaseResult(true);
	Result->SetBoolField(TEXT("complete"), false);
	Result->SetStringField(TEXT("call_id"), CallId);
	Result->SetStringField(TEXT("toolset"), ToolsetName);
	Result->SetStringField(TEXT("tool"), ToolName);
	Result->SetStringField(TEXT("risk"), TEXT("ReadOnly"));
	Result->SetStringField(TEXT("execution"), TEXT("PollingJob"));
	Result->SetStringField(TEXT("save_behavior"), TEXT("Never"));
	Result->SetStringField(TEXT("side_effect_state"), TEXT("none"));
	return UE58_Serialize(Result);
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Official ToolsetRegistry federation requires UE 5.8 or newer"));
#endif
}

FString UUnrealBridgeUE58Library::PollOfficialToolsetCall(const FString& CallId)
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!IsInGameThread())
	{
		return UE58_Error(TEXT("GAME_THREAD_REQUIRED"), TEXT("Official tool calls must be polled on the GameThread"), false);
	}
	UE58_CleanupCalls();
	const TSharedPtr<FUE58PendingCall>* Found = GUE58PendingCalls.Find(CallId);
	if (!Found || !Found->IsValid() || !(*Found)->Result.IsValid())
	{
		return UE58_Error(TEXT("UNKNOWN_CALL"), FString::Printf(TEXT("unknown official call '%s'"), *CallId));
	}
	const TSharedPtr<FUE58PendingCall> Pending = *Found;
	UToolCallAsyncResultString* AsyncResult = Pending->Result.Get();
	TSharedRef<FJsonObject> Result = UE58_BaseResult(true);
	Result->SetStringField(TEXT("call_id"), CallId);
	Result->SetStringField(TEXT("toolset"), Pending->ToolsetName);
	Result->SetStringField(TEXT("tool"), Pending->ToolName);
	Result->SetStringField(TEXT("risk"), Pending->Risk);
	Result->SetStringField(TEXT("side_effect_state"), Pending->Risk == TEXT("ReadOnly") ? TEXT("none") : TEXT("unknown"));
	if (!AsyncResult->bIsComplete)
	{
		Result->SetBoolField(TEXT("complete"), false);
		return UE58_Serialize(Result);
	}

	Result->SetBoolField(TEXT("complete"), true);
	if (!AsyncResult->Error.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error_code"), TEXT("OFFICIAL_TOOL_FAILED"));
		Result->SetStringField(TEXT("error"), AsyncResult->Error);
		Result->SetBoolField(TEXT("retryable"), false);
	}
	else
	{
		TSharedPtr<FJsonValue> ParsedValue;
		const TSharedRef<TJsonReader<>> ValueReader = TJsonReaderFactory<>::Create(AsyncResult->Value);
		if (FJsonSerializer::Deserialize(ValueReader, ParsedValue) && ParsedValue.IsValid())
		{
			Result->SetField(TEXT("result"), ParsedValue);
		}
		else
		{
			Result->SetStringField(TEXT("result_text"), AsyncResult->Value);
		}
	}
	GUE58PendingCalls.Remove(CallId);
	return UE58_Serialize(Result);
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Official ToolsetRegistry federation requires UE 5.8 or newer"));
#endif
}

FString UUnrealBridgeUE58Library::AbandonOfficialToolsetCall(const FString& CallId)
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	const bool bRemoved = GUE58PendingCalls.Remove(CallId) > 0;
	TSharedRef<FJsonObject> Result = UE58_BaseResult(bRemoved);
	Result->SetBoolField(TEXT("complete"), true);
	Result->SetStringField(TEXT("call_id"), CallId);
	Result->SetStringField(TEXT("cancellation"), TEXT("local-tracking-only"));
	Result->SetStringField(TEXT("side_effect_state"), bRemoved ? TEXT("unknown") : TEXT("none"));
	if (!bRemoved)
	{
		Result->SetStringField(TEXT("error_code"), TEXT("UNKNOWN_CALL"));
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("unknown official call '%s'"), *CallId));
	}
	return UE58_Serialize(Result);
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Official ToolsetRegistry federation requires UE 5.8 or newer"));
#endif
}

void UnrealBridgeUE58Adapter::Startup()
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!IsInGameThread())
	{
		return;
	}
	const auto Subsystem = UToolsetRegistrySubsystem::Get(TEXT("UnrealBridge 3 startup"));
	if (Subsystem.HasError())
	{
		UE_LOG(LogUnrealBridgeUE58, Warning, TEXT("ToolsetRegistry unavailable: %s"), *Subsystem.GetError());
		return;
	}
	GUE58BridgeToolset = MakeShared<FUnrealBridgeMetaToolset>();
	if (!Subsystem.GetValue()->ToolsetRegistry.RegisterToolset(GUE58BridgeToolset))
	{
		UE_LOG(LogUnrealBridgeUE58, Warning, TEXT("Could not register UnrealBridge read-only meta toolset"));
		GUE58BridgeToolset.Reset();
	}
	else
	{
		UE_LOG(LogUnrealBridgeUE58, Log, TEXT("Registered UnrealBridge 3 meta toolset with UE 5.8 ToolsetRegistry"));
	}
#endif
}

void UnrealBridgeUE58Adapter::Shutdown()
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (GUE58BridgeToolset.IsValid())
	{
		const auto Subsystem = UToolsetRegistrySubsystem::Get();
		if (Subsystem.HasValue())
		{
			Subsystem.GetValue()->ToolsetRegistry.UnregisterToolset(GUE58BridgeToolset);
		}
		GUE58BridgeToolset.Reset();
	}
	GUE58PendingCalls.Reset();
#endif
}

bool UnrealBridgeUE58Adapter::IsCompiled()
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	return true;
#else
	return false;
#endif
}

bool UnrealBridgeUE58Adapter::IsRegistryAvailable()
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	return GUE58BridgeToolset.IsValid();
#else
	return false;
#endif
}
