#include "UnrealBridgeUE58Library.h"

#include "UnrealBridgeChangeSetLibrary.h"
#include "UnrealBridgeNiagaraLibrary.h"
#include "UnrealBridgeRegistryLibrary.h"
#include "UnrealBridgeVersion.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "Policies/CondensedJsonPrintPolicy.h"
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
	void UE58_GatherNiagaraGraphs(
		UNiagaraSystem* System,
		TArray<UNiagaraGraph*>& OutGraphs)
	{
		if (!System)
		{
			return;
		}

		TSet<UNiagaraGraph*> UniqueGraphs;
		UNiagaraScript* SystemScript = System->GetSystemSpawnScript();
		if (!SystemScript)
		{
			SystemScript = System->GetSystemUpdateScript();
		}
		if (UNiagaraScriptSource* Source = SystemScript
			? Cast<UNiagaraScriptSource>(SystemScript->GetLatestSource())
			: nullptr)
		{
			if (Source->NodeGraph)
			{
				UniqueGraphs.Add(Source->NodeGraph);
			}
		}

		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
			UNiagaraScriptSource* Source = EmitterData
				? Cast<UNiagaraScriptSource>(EmitterData->GraphSource)
				: nullptr;
			if (Source && Source->NodeGraph)
			{
				UniqueGraphs.Add(Source->NodeGraph);
			}
		}

		OutGraphs.Reserve(OutGraphs.Num() + UniqueGraphs.Num());
		for (UNiagaraGraph* Graph : UniqueGraphs)
		{
			OutGraphs.Add(Graph);
		}
	}

	// Keep this implementation local and dependency-free.  UE 5.8's own
	// MCPClientToolset uses the same approach because
	// FPlatformMisc::GetSHA256Signature deliberately check-fails on platforms
	// without an override (including the Windows editor configuration used by
	// ShooterRoyal).
	uint32 UE58_SHA256RotateRight(uint32 Value, uint32 Count)
	{
		return (Value >> Count) | (Value << (32u - Count));
	}

	void UE58_ComputeSHA256(const uint8* Message, uint32 MessageLength, uint8 OutHash[32])
	{
		static const uint32 Constants[64] = {
			0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
			0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
			0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
			0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
			0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
			0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
			0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
			0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
		};
		uint32 Hash[8] = {
			0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
			0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
		};

		const uint32 PaddedLength = ((MessageLength + 9 + 63) / 64) * 64;
		TArray<uint8> Padded;
		Padded.SetNumZeroed(PaddedLength);
		if (MessageLength > 0)
		{
			FMemory::Memcpy(Padded.GetData(), Message, MessageLength);
		}
		Padded[MessageLength] = 0x80;
		const uint64 BitLength = static_cast<uint64>(MessageLength) * 8;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Padded[PaddedLength - 8 + Index] =
				static_cast<uint8>(BitLength >> (56 - Index * 8));
		}

		for (uint32 Offset = 0; Offset < PaddedLength; Offset += 64)
		{
			uint32 Schedule[64];
			for (int32 Index = 0; Index < 16; ++Index)
			{
				Schedule[Index] =
					(static_cast<uint32>(Padded[Offset + Index * 4]) << 24)
					| (static_cast<uint32>(Padded[Offset + Index * 4 + 1]) << 16)
					| (static_cast<uint32>(Padded[Offset + Index * 4 + 2]) << 8)
					| static_cast<uint32>(Padded[Offset + Index * 4 + 3]);
			}
			for (int32 Index = 16; Index < 64; ++Index)
			{
				const uint32 Sigma0 =
					UE58_SHA256RotateRight(Schedule[Index - 15], 7)
					^ UE58_SHA256RotateRight(Schedule[Index - 15], 18)
					^ (Schedule[Index - 15] >> 3);
				const uint32 Sigma1 =
					UE58_SHA256RotateRight(Schedule[Index - 2], 17)
					^ UE58_SHA256RotateRight(Schedule[Index - 2], 19)
					^ (Schedule[Index - 2] >> 10);
				Schedule[Index] = Schedule[Index - 16] + Sigma0 + Schedule[Index - 7] + Sigma1;
			}

			uint32 A = Hash[0];
			uint32 B = Hash[1];
			uint32 C = Hash[2];
			uint32 D = Hash[3];
			uint32 E = Hash[4];
			uint32 F = Hash[5];
			uint32 G = Hash[6];
			uint32 H = Hash[7];
			for (int32 Index = 0; Index < 64; ++Index)
			{
				const uint32 UpperSigma1 =
					UE58_SHA256RotateRight(E, 6)
					^ UE58_SHA256RotateRight(E, 11)
					^ UE58_SHA256RotateRight(E, 25);
				const uint32 Choice = (E & F) ^ (~E & G);
				const uint32 Temp1 = H + UpperSigma1 + Choice + Constants[Index] + Schedule[Index];
				const uint32 UpperSigma0 =
					UE58_SHA256RotateRight(A, 2)
					^ UE58_SHA256RotateRight(A, 13)
					^ UE58_SHA256RotateRight(A, 22);
				const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
				const uint32 Temp2 = UpperSigma0 + Majority;
				H = G;
				G = F;
				F = E;
				E = D + Temp1;
				D = C;
				C = B;
				B = A;
				A = Temp1 + Temp2;
			}
			Hash[0] += A;
			Hash[1] += B;
			Hash[2] += C;
			Hash[3] += D;
			Hash[4] += E;
			Hash[5] += F;
			Hash[6] += G;
			Hash[7] += H;
		}

		for (int32 Index = 0; Index < 8; ++Index)
		{
			OutHash[Index * 4] = static_cast<uint8>(Hash[Index] >> 24);
			OutHash[Index * 4 + 1] = static_cast<uint8>(Hash[Index] >> 16);
			OutHash[Index * 4 + 2] = static_cast<uint8>(Hash[Index] >> 8);
			OutHash[Index * 4 + 3] = static_cast<uint8>(Hash[Index]);
		}
	}

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
	struct FUE58OfficialPolicyEntry
	{
		FString Access;
		FString SchemaSha256;
		FString Risk;
		FString SaveBehavior;
	};

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
	FString GUE58CatalogSession;
	uint64 GUE58CatalogRevision = 0;
	FDelegateHandle GUE58CatalogChangedHandle;
	TWeakObjectPtr<UToolsetRegistrySubsystem> GUE58CatalogSubsystem;

	void UE58_AttachCatalogRevision(UToolsetRegistrySubsystem* Subsystem)
	{
		if (GUE58CatalogSession.IsEmpty()) GUE58CatalogSession = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		if (GUE58CatalogSubsystem.Get() == Subsystem && GUE58CatalogChangedHandle.IsValid()) return;
		if (auto* Old = GUE58CatalogSubsystem.Get()) Old->ToolsetRegistry.OnToolsetRegistered().Remove(GUE58CatalogChangedHandle);
		GUE58CatalogSubsystem = Subsystem;
		++GUE58CatalogRevision;
		GUE58CatalogChangedHandle = Subsystem->ToolsetRegistry.OnToolsetRegistered().AddLambda([] { ++GUE58CatalogRevision; });
	}
	TMap<FString, FUE58OfficialPolicyEntry> GUE58OfficialPolicy;
	FString GUE58OfficialPolicyJson;
	FString GUE58OfficialPolicyError;
	bool GUE58OfficialPolicyLoaded = false;

	bool UE58_IsDocumentationSchemaKey(const FString& Key)
	{
		return Key == TEXT("description")
			|| Key == TEXT("title")
			|| Key == TEXT("examples")
			|| Key == TEXT("$comment");
	}

	FString UE58_JsonScalar(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid() || Value->IsNull())
		{
			return TEXT("null");
		}
		FString Json;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		switch (Value->Type)
		{
		case EJson::String:
			Writer->WriteValue(Value->AsString());
			break;
		case EJson::Number:
			Writer->WriteValue(Value->AsNumber());
			break;
		case EJson::Boolean:
			Writer->WriteValue(Value->AsBool());
			break;
		default:
			return TEXT("null");
		}
		Writer->Close();
		return Json;
	}

	FString UE58_CanonicalSchemaValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid() || Value->IsNull())
		{
			return TEXT("null");
		}
		if (Value->Type == EJson::Array)
		{
			FString Result(TEXT("["));
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				if (Index > 0)
				{
					Result += TEXT(",");
				}
				Result += UE58_CanonicalSchemaValue(Items[Index]);
			}
			return Result + TEXT("]");
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			TArray<FJsonObject::FStringType> Keys;
			Object->Values.GetKeys(Keys);
			Keys.RemoveAll([](const FJsonObject::FStringType& Key)
			{
				return UE58_IsDocumentationSchemaKey(FString(Key.ToView()));
			});
			Keys.Sort();
			FString Result(TEXT("{"));
			bool bFirst = true;
			for (const FJsonObject::FStringType& Key : Keys)
			{
				const TSharedPtr<FJsonValue>* Child = Object->Values.Find(Key);
				if (!Child)
				{
					continue;
				}
				if (!bFirst)
				{
					Result += TEXT(",");
				}
				bFirst = false;
				Result += UE58_JsonScalar(
					MakeShared<FJsonValueString>(FString(Key.ToView())));
				Result += TEXT(":");
				Result += UE58_CanonicalSchemaValue(*Child);
			}
			return Result + TEXT("}");
		}
		return UE58_JsonScalar(Value);
	}

	FString UE58_SchemaSha256(const TSharedPtr<FJsonObject>& Schema)
	{
		const FString Canonical = UE58_CanonicalSchemaValue(MakeShared<FJsonValueObject>(Schema));
		FTCHARToUTF8 Utf8(*Canonical);
		uint8 Hash[32];
		UE58_ComputeSHA256(
			reinterpret_cast<const uint8*>(Utf8.Get()),
			static_cast<uint32>(Utf8.Length()),
			Hash);
		return BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
	}

	bool UE58_LoadOfficialPolicy(FString& OutError)
	{
		if (GUE58OfficialPolicyLoaded)
		{
			OutError = GUE58OfficialPolicyError;
			return GUE58OfficialPolicyError.IsEmpty();
		}
		GUE58OfficialPolicyLoaded = true;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealBridge"));
		if (!Plugin.IsValid())
		{
			GUE58OfficialPolicyError = TEXT("Could not locate the UnrealBridge plugin");
			OutError = GUE58OfficialPolicyError;
			return false;
		}
		const FString PolicyPath = FPaths::Combine(
			Plugin->GetBaseDir(), TEXT("Resources"), TEXT("ue58_official_tool_policy.json"));
		if (!FFileHelper::LoadFileToString(GUE58OfficialPolicyJson, *PolicyPath))
		{
			GUE58OfficialPolicyError = FString::Printf(TEXT("Could not read official tool policy '%s'"), *PolicyPath);
			OutError = GUE58OfficialPolicyError;
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GUE58OfficialPolicyJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			GUE58OfficialPolicyError = TEXT("Official tool policy contains invalid JSON");
			OutError = GUE58OfficialPolicyError;
			return false;
		}
		bool bDenyByDefault = false;
		const TSharedPtr<FJsonObject>* Tools = nullptr;
		if (!Root->TryGetBoolField(TEXT("deny_by_default"), bDenyByDefault)
			|| !bDenyByDefault
			|| !Root->TryGetObjectField(TEXT("tools"), Tools)
			|| !Tools)
		{
			GUE58OfficialPolicyError = TEXT("Official tool policy must be deny-by-default and contain tools");
			OutError = GUE58OfficialPolicyError;
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Tools)->Values)
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> EntryObject = Pair.Value->AsObject();
			FUE58OfficialPolicyEntry Entry;
			EntryObject->TryGetStringField(TEXT("access"), Entry.Access);
			EntryObject->TryGetStringField(TEXT("schema_sha256"), Entry.SchemaSha256);
			EntryObject->TryGetStringField(TEXT("risk"), Entry.Risk);
			EntryObject->TryGetStringField(TEXT("save_behavior"), Entry.SaveBehavior);
			if (!Entry.Access.IsEmpty() && Entry.SchemaSha256.Len() == 64)
			{
				GUE58OfficialPolicy.Add(Pair.Key, MoveTemp(Entry));
			}
		}
		if (GUE58OfficialPolicy.IsEmpty())
		{
			GUE58OfficialPolicyError = TEXT("Official tool policy contains no valid exact entries");
		}
		OutError = GUE58OfficialPolicyError;
		return GUE58OfficialPolicyError.IsEmpty();
	}

	bool UE58_FindOfficialTool(
		const FString& ToolsetName,
		const FString& ToolName,
		TSharedPtr<UE::ToolsetRegistry::FToolset>& OutToolset,
		TSharedPtr<FJsonObject>& OutToolSchema,
		FString& OutError)
	{
		const auto Subsystem = UToolsetRegistrySubsystem::Get(TEXT("UnrealBridge official tool policy"));
		if (Subsystem.HasError())
		{
			OutError = Subsystem.GetError();
			return false;
		}
		FString FindError;
		OutToolset = Subsystem.GetValue()->ToolsetRegistry.Find(ToolsetName, false, &FindError);
		if (!OutToolset.IsValid())
		{
			OutError = FindError.IsEmpty()
				? FString::Printf(TEXT("Unknown toolset '%s'"), *ToolsetName)
				: FindError;
			return false;
		}
		TSharedPtr<FJsonObject> Schema;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutToolset->GetJsonSchema());
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
			if (SchemaName == ToolName || SchemaName == QualifiedName)
			{
				OutToolSchema = Tool;
				return true;
			}
		}
		OutError = FString::Printf(TEXT("Unknown official tool '%s'"), *QualifiedName);
		return false;
	}

	bool UE58_AuthorizeOfficialTool(
		const FString& ToolsetName,
		const FString& ToolName,
		const FString& RequiredAccess,
		TSharedPtr<UE::ToolsetRegistry::FToolset>& OutToolset,
		FString& OutError)
	{
		FString PolicyError;
		if (!UE58_LoadOfficialPolicy(PolicyError))
		{
			OutError = PolicyError;
			return false;
		}
		const FString Key = ToolsetName + TEXT("|") + ToolName;
		const FUE58OfficialPolicyEntry* Entry = GUE58OfficialPolicy.Find(Key);
		if (!Entry)
		{
			OutError = FString::Printf(TEXT("Official tool '%s.%s' is absent from the exact audit policy"), *ToolsetName, *ToolName);
			return false;
		}
		if (Entry->Access != RequiredAccess)
		{
			OutError = FString::Printf(
				TEXT("Official tool '%s.%s' is policy '%s', not '%s'"),
				*ToolsetName, *ToolName, *Entry->Access, *RequiredAccess);
			return false;
		}
		TSharedPtr<FJsonObject> ToolSchema;
		if (!UE58_FindOfficialTool(ToolsetName, ToolName, OutToolset, ToolSchema, OutError))
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* InputSchema = nullptr;
		const TSharedPtr<FJsonObject> EmptySchema = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject> SchemaToHash =
			ToolSchema->TryGetObjectField(TEXT("inputSchema"), InputSchema) && InputSchema
				? *InputSchema
				: EmptySchema;
		const FString ActualHash = UE58_SchemaSha256(SchemaToHash);
		if (ActualHash.IsEmpty() || ActualHash != Entry->SchemaSha256)
		{
			OutError = FString::Printf(
				TEXT("Official tool '%s.%s' schema hash drifted (expected %s, got %s); regenerate and review policy"),
				*ToolsetName, *ToolName, *Entry->SchemaSha256, *ActualHash);
			return false;
		}
		return true;
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

	bool UE58_ParseInputObject(const FString& JsonInput, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
			JsonInput.IsEmpty() ? TEXT("{}") : JsonInput);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	FString UE58_StartAuditedCall(
		const FString& CallId,
		const FString& ToolsetName,
		const FString& ToolName,
		const FString& JsonInput,
		const FString& Access)
	{
		if (!IsInGameThread())
		{
			return UE58_Error(TEXT("GAME_THREAD_REQUIRED"), TEXT("Official tool calls must start on the GameThread"));
		}
		if (CallId.IsEmpty() || ToolsetName.IsEmpty() || ToolName.IsEmpty())
		{
			return UE58_Error(TEXT("INVALID_ARGUMENT"), TEXT("call_id, toolset_name and tool_name are required"));
		}
		TSharedPtr<UE::ToolsetRegistry::FToolset> AuditedToolset;
		FString PolicyError;
		if (!UE58_AuthorizeOfficialTool(
			ToolsetName, ToolName, Access, AuditedToolset, PolicyError))
		{
			return UE58_Error(TEXT("OFFICIAL_TOOL_NOT_APPROVED"), PolicyError);
		}
		TSharedPtr<FJsonObject> InputObject;
		if (!UE58_ParseInputObject(JsonInput, InputObject))
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
		Pending->Risk = Access;
		GUE58PendingCalls.Add(CallId, Pending);

		TSharedRef<FJsonObject> Result = UE58_BaseResult(true);
		Result->SetBoolField(TEXT("complete"), false);
		Result->SetStringField(TEXT("call_id"), CallId);
		Result->SetStringField(TEXT("toolset"), ToolsetName);
		Result->SetStringField(TEXT("tool"), ToolName);
		Result->SetStringField(TEXT("risk"), Access);
		Result->SetStringField(TEXT("execution"), TEXT("PollingJob"));
		Result->SetStringField(TEXT("save_behavior"), TEXT("Never"));
		Result->SetStringField(
			TEXT("side_effect_state"),
			Access == TEXT("ReadOnly") ? TEXT("none") : TEXT("runtime-requested"));
		return UE58_Serialize(Result);
	}

	TArray<TSharedPtr<FJsonValue>> UE58_StringArray(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Reserve(Values.Num());
		for (const FString& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value));
		}
		return Result;
	}

	TSharedRef<FJsonObject> UE58_ChangeSetJson(const FBridgeChangeSetInfo& Info)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("change_set_id"), Info.ChangeSetId);
		Result->SetStringField(TEXT("job_id"), Info.JobId);
		Result->SetStringField(TEXT("name"), Info.Name);
		Result->SetStringField(TEXT("status"), Info.Status);
		Result->SetBoolField(TEXT("success"), Info.bSuccess);
		Result->SetBoolField(TEXT("can_commit"), Info.bCanCommit);
		Result->SetBoolField(TEXT("saved"), Info.bSaved);
		Result->SetArrayField(TEXT("target_packages"), UE58_StringArray(Info.TargetPackages));
		Result->SetArrayField(TEXT("target_packages_dirty_at_begin"), UE58_StringArray(Info.TargetPackagesDirtyAtBegin));
		Result->SetArrayField(TEXT("dirty_packages_for_job"), UE58_StringArray(Info.DirtyPackagesForJob));
		Result->SetArrayField(TEXT("protected_preexisting_dirty_packages"), UE58_StringArray(Info.ProtectedPreexistingDirtyPackages));
		Result->SetArrayField(TEXT("unexpected_dirty_packages"), UE58_StringArray(Info.UnexpectedDirtyPackages));
		Result->SetArrayField(TEXT("created_assets_for_job"), UE58_StringArray(Info.CreatedAssetsForJob));
		Result->SetArrayField(TEXT("removed_created_assets_during_rollback"), UE58_StringArray(Info.RemovedCreatedAssetsDuringRollback));
		Result->SetNumberField(TEXT("captured_objects_for_undo"), Info.CapturedObjectsForUndo);
		Result->SetArrayField(TEXT("reload_packages_on_rollback"), UE58_StringArray(Info.ReloadPackagesOnRollback));
		Result->SetArrayField(TEXT("reloaded_packages_during_rollback"), UE58_StringArray(Info.ReloadedPackagesDuringRollback));
		Result->SetBoolField(TEXT("rollback_verified"), Info.bRollbackVerified);
		Result->SetStringField(TEXT("error"), Info.Error);
		return Result;
	}

	FString UE58_ChangeSetSideEffectState(const FBridgeChangeSetInfo& Info)
	{
		if (Info.Status == TEXT("Committed"))
		{
			return TEXT("committed-unsaved");
		}
		if (Info.bSuccess
			&& (Info.Status == TEXT("RolledBack")
				|| Info.Status == TEXT("RolledBackAfterCommit")))
		{
			return TEXT("rolled-back");
		}
		return TEXT("rollback-failed");
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
			return FString::Printf(TEXT(R"json({"name":"UnrealBridge","version":"%s","description":"Read-only UnrealBridge capability discovery; mutations require the authenticated durable Job endpoint.","tools":[{"name":"UnrealBridge.Capabilities","description":"Return UnrealBridge version, protocol, registry hash, provider and mutation transport.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true,"destructiveHint":false}},{"name":"UnrealBridge.RegistryHash","description":"Return the strict native tool registry hash.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true,"destructiveHint":false}}]})json"), UnrealBridgeVersion::Plugin);
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

FString UUnrealBridgeUE58Library::GetOfficialToolsetCatalogSnapshotJson(bool bIncludeCatalog)
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!IsInGameThread()) return UE58_Error(TEXT("GAME_THREAD_REQUIRED"), TEXT("Catalog snapshots require the GameThread"));
	const auto Subsystem = UToolsetRegistrySubsystem::Get(TEXT("UnrealBridge catalog snapshot"));
	if (Subsystem.HasError()) return UE58_Error(TEXT("TOOLSET_REGISTRY_UNAVAILABLE"), Subsystem.GetError());
	UE58_AttachCatalogRevision(Subsystem.GetValue());
	auto Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("schema"), TEXT("unrealbridge.tool_catalog.v1"));
	Out->SetStringField(TEXT("editor_session_id"), GUE58CatalogSession);
	Out->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	Out->SetStringField(TEXT("registry_revision"), LexToString(GUE58CatalogRevision));
	if (bIncludeCatalog)
	{
		const FString Raw = UToolsetRegistry::GetAllToolsetJsonSchemas();
		if (FTCHARToUTF8(*Raw).Length() > 8 * 1024 * 1024) return UE58_Error(TEXT("CATALOG_SIZE_LIMIT"), TEXT("Catalog exceeds 8 MiB; no partial catalog is published"));
		TArray<TSharedPtr<FJsonValue>> Rows;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw), Rows)) return UE58_Error(TEXT("CATALOG_INVALID_JSON"), TEXT("Registry did not return a JSON array"));
		Out->SetArrayField(TEXT("catalog"), Rows);
	}
	return UE58_Serialize(Out);
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Catalog snapshots require UE 5.8 or newer"));
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

FString UUnrealBridgeUE58Library::GetOfficialToolPolicyJson()
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	FString PolicyError;
	if (!UE58_LoadOfficialPolicy(PolicyError))
	{
		return UE58_Error(TEXT("OFFICIAL_POLICY_UNAVAILABLE"), PolicyError);
	}
	return GUE58OfficialPolicyJson;
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
	return UE58_StartAuditedCall(
		CallId, ToolsetName, ToolName, JsonInput, TEXT("ReadOnly"));
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Official ToolsetRegistry federation requires UE 5.8 or newer"));
#endif
}

FString UUnrealBridgeUE58Library::StartOfficialRuntimeToolsetCall(
	const FString& CallId,
	const FString& ToolsetName,
	const FString& ToolName,
	const FString& JsonInput,
	bool bAllowRuntimeSideEffects)
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!bAllowRuntimeSideEffects)
	{
		return UE58_Error(
			TEXT("RUNTIME_OPT_IN_REQUIRED"),
			TEXT("Set allow_runtime_side_effects=true for audited Slate, Automation, PIE, or GameFeature interactions"));
	}
	return UE58_StartAuditedCall(
		CallId, ToolsetName, ToolName, JsonInput, TEXT("RuntimeInteraction"));
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Official ToolsetRegistry federation requires UE 5.8 or newer"));
#endif
}

FString UUnrealBridgeUE58Library::ExecuteOfficialTransactionalToolsetCall(
	const FString& ToolsetName,
	const FString& ToolName,
	const FString& JsonInput,
	const TArray<FString>& TargetPackages,
	bool bApply)
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!IsInGameThread())
	{
		return UE58_Error(TEXT("GAME_THREAD_REQUIRED"), TEXT("Transactional official calls must run on the GameThread"));
	}
	if (ToolsetName.IsEmpty() || ToolName.IsEmpty() || TargetPackages.IsEmpty())
	{
		return UE58_Error(
			TEXT("INVALID_ARGUMENT"),
			TEXT("toolset_name, tool_name, and at least one explicit target package are required"));
	}
	TSharedPtr<FJsonObject> InputObject;
	if (!UE58_ParseInputObject(JsonInput, InputObject))
	{
		return UE58_Error(TEXT("INVALID_JSON_INPUT"), TEXT("json_input must be a JSON object"));
	}
	TSharedPtr<UE::ToolsetRegistry::FToolset> Toolset;
	FString PolicyError;
	if (!UE58_AuthorizeOfficialTool(
		ToolsetName, ToolName, TEXT("TransactionalSync"), Toolset, PolicyError))
	{
		return UE58_Error(TEXT("OFFICIAL_TOOL_NOT_APPROVED"), PolicyError);
	}

	const FString ChangeSetId = UUnrealBridgeChangeSetLibrary::BeginChangeSet(
		FString::Printf(TEXT("Official %s.%s"), *ToolsetName, *ToolName),
		TargetPackages,
		ToolsetName.Contains(TEXT("controlrig"), ESearchCase::IgnoreCase));
	if (ChangeSetId.IsEmpty())
	{
		return UE58_Error(
			TEXT("CHANGE_SET_REQUIRED"),
			TEXT("Transactional official tools must execute inside an authenticated UnrealBridge Job"));
	}

	TFuture<TValueOrError<FString, FString>> Future = Toolset->ExecuteTool(
		ToolName, JsonInput.IsEmpty() ? TEXT("{}") : JsonInput);
	if (!Future.IsReady())
	{
		const FBridgeChangeSetInfo Rollback =
			BridgeChangeSetRuntime::RollbackNow(
				ChangeSetId,
				TEXT("Official mutation returned asynchronously"));
		TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
		Result->SetBoolField(TEXT("complete"), true);
		Result->SetStringField(TEXT("error_code"), TEXT("ASYNC_MUTATION_REFUSED"));
		Result->SetStringField(
			TEXT("error"),
			TEXT("Policy requires immediate completion; this provider returned an asynchronous mutation"));
		Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(Rollback));
		Result->SetStringField(
			TEXT("side_effect_state"),
			UE58_ChangeSetSideEffectState(Rollback));
		return UE58_Serialize(Result);
	}

	TValueOrError<FString, FString> ToolResult = Future.Get();
	if (ToolResult.HasError())
	{
		const FString ToolError = ToolResult.StealError();
		const FBridgeChangeSetInfo Rollback =
			BridgeChangeSetRuntime::RollbackNow(
				ChangeSetId,
				TEXT("Official mutation failed"));
		TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
		Result->SetBoolField(TEXT("complete"), true);
		Result->SetStringField(TEXT("error_code"), TEXT("OFFICIAL_TOOL_FAILED"));
		Result->SetStringField(TEXT("error"), ToolError);
		Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(Rollback));
		Result->SetStringField(
			TEXT("side_effect_state"),
			UE58_ChangeSetSideEffectState(Rollback));
		return UE58_Serialize(Result);
	}

	const FString Value = ToolResult.StealValue();
	const FBridgeChangeSetInfo Preview =
		UUnrealBridgeChangeSetLibrary::PreviewChangeSet(ChangeSetId);
	FBridgeChangeSetInfo FinalAction;
	if (!Preview.bCanCommit || !bApply)
	{
		FinalAction = BridgeChangeSetRuntime::RollbackNow(
			ChangeSetId,
			bApply
				? TEXT("ChangeSet preview rejected commit")
				: TEXT("Preview completed and rolled back"));
	}
	else
	{
		FinalAction = BridgeChangeSetRuntime::CommitNow(ChangeSetId, false);
	}

	TSharedRef<FJsonObject> Result = UE58_BaseResult(FinalAction.bSuccess);
	Result->SetBoolField(TEXT("complete"), true);
	Result->SetStringField(TEXT("toolset"), ToolsetName);
	Result->SetStringField(TEXT("tool"), ToolName);
	Result->SetBoolField(TEXT("apply_requested"), bApply);
	Result->SetBoolField(TEXT("saved"), false);
	Result->SetStringField(
		TEXT("side_effect_state"),
		UE58_ChangeSetSideEffectState(FinalAction));
	Result->SetObjectField(TEXT("preview"), UE58_ChangeSetJson(Preview));
	Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(FinalAction));
	TSharedPtr<FJsonValue> ParsedValue;
	const TSharedRef<TJsonReader<>> ValueReader = TJsonReaderFactory<>::Create(Value);
	if (FJsonSerializer::Deserialize(ValueReader, ParsedValue) && ParsedValue.IsValid())
	{
		Result->SetField(TEXT("result"), ParsedValue);
	}
	else
	{
		Result->SetStringField(TEXT("result_text"), Value);
	}
	if (!FinalAction.bSuccess)
	{
		Result->SetStringField(TEXT("error_code"), TEXT("CHANGE_SET_REJECTED"));
		Result->SetStringField(TEXT("error"), FinalAction.Error);
	}
	return UE58_Serialize(Result);
#else
	return UE58_Error(TEXT("UNSUPPORTED_ENGINE"), TEXT("Official ToolsetRegistry federation requires UE 5.8 or newer"));
#endif
}

FString UUnrealBridgeUE58Library::ExecuteOfficialTransactionalToolsetBatch(
	const FString& JsonCalls,
	const TArray<FString>& TargetPackages,
	bool bApply,
	const FString& NiagaraSystemPathToCompile,
	const FString& NiagaraUserParameterRenamesJson)
{
#if UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
	if (!IsInGameThread())
	{
		return UE58_Error(TEXT("GAME_THREAD_REQUIRED"), TEXT("Transactional official batches must run on the GameThread"));
	}
	if (TargetPackages.IsEmpty())
	{
		return UE58_Error(TEXT("INVALID_ARGUMENT"), TEXT("At least one explicit target package is required"));
	}

	TSharedPtr<FJsonValue> RootValue;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCalls);
	if (!FJsonSerializer::Deserialize(Reader, RootValue)
		|| !RootValue.IsValid()
		|| RootValue->Type != EJson::Array)
	{
		return UE58_Error(TEXT("INVALID_JSON_INPUT"), TEXT("json_calls must be a JSON array"));
	}
	const TArray<TSharedPtr<FJsonValue>>& CallValues = RootValue->AsArray();
	if (CallValues.Num() > 64)
	{
		return UE58_Error(TEXT("INVALID_ARGUMENT"), TEXT("json_calls may contain at most 64 calls"));
	}

	struct FNiagaraUserParameterRename
	{
		FString OldName;
		FString NewName;
	};
	TArray<FNiagaraUserParameterRename> NiagaraRenames;
	TSharedPtr<FJsonValue> RenameRootValue;
	const TSharedRef<TJsonReader<>> RenameReader = TJsonReaderFactory<>::Create(
		NiagaraUserParameterRenamesJson.IsEmpty()
			? TEXT("[]")
			: NiagaraUserParameterRenamesJson);
	if (!FJsonSerializer::Deserialize(RenameReader, RenameRootValue)
		|| !RenameRootValue.IsValid()
		|| RenameRootValue->Type != EJson::Array)
	{
		return UE58_Error(
			TEXT("INVALID_JSON_INPUT"),
			TEXT("niagara_user_parameter_renames_json must be a JSON array"));
	}
	const TArray<TSharedPtr<FJsonValue>>& RenameValues = RenameRootValue->AsArray();
	if (RenameValues.Num() > 16)
	{
		return UE58_Error(
			TEXT("INVALID_ARGUMENT"),
			TEXT("At most 16 Niagara user parameters may be renamed per batch"));
	}
	for (int32 Index = 0; Index < RenameValues.Num(); ++Index)
	{
		if (!RenameValues[Index].IsValid()
			|| RenameValues[Index]->Type != EJson::Object)
		{
			return UE58_Error(
				TEXT("INVALID_JSON_INPUT"),
				FString::Printf(
					TEXT("niagara_user_parameter_renames_json[%d] must be an object"),
					Index));
		}
		FNiagaraUserParameterRename Rename;
		const TSharedPtr<FJsonObject> RenameObject = RenameValues[Index]->AsObject();
		if (!RenameObject->TryGetStringField(TEXT("old_name"), Rename.OldName)
			|| !RenameObject->TryGetStringField(TEXT("new_name"), Rename.NewName)
			|| !Rename.OldName.StartsWith(TEXT("User."))
			|| !Rename.NewName.StartsWith(TEXT("User."))
			|| Rename.OldName == Rename.NewName)
		{
			return UE58_Error(
				TEXT("INVALID_ARGUMENT"),
				FString::Printf(
					TEXT("Niagara rename %d requires distinct old_name/new_name values in the User namespace"),
					Index));
		}
		NiagaraRenames.Add(MoveTemp(Rename));
	}
	if (CallValues.IsEmpty() && NiagaraRenames.IsEmpty())
	{
		return UE58_Error(
			TEXT("INVALID_ARGUMENT"),
			TEXT("The batch requires at least one official call or Niagara user-parameter rename"));
	}
	if (!NiagaraRenames.IsEmpty() && NiagaraSystemPathToCompile.IsEmpty())
	{
		return UE58_Error(
			TEXT("INVALID_ARGUMENT"),
			TEXT("Niagara user-parameter renames require niagara_system_path_to_compile"));
	}

	struct FPreparedOfficialCall
	{
		FString ToolsetName;
		FString ToolName;
		FString JsonInput;
		FString Access;
		TSharedPtr<UE::ToolsetRegistry::FToolset> Toolset;
	};
	TArray<FPreparedOfficialCall> Prepared;
	Prepared.Reserve(CallValues.Num());
	bool bHasMutation = false;
	for (int32 Index = 0; Index < CallValues.Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& CallValue = CallValues[Index];
		if (!CallValue.IsValid() || CallValue->Type != EJson::Object)
		{
			return UE58_Error(
				TEXT("INVALID_JSON_INPUT"),
				FString::Printf(TEXT("json_calls[%d] must be an object"), Index));
		}
		const TSharedPtr<FJsonObject> CallObject = CallValue->AsObject();
		FPreparedOfficialCall Call;
		const TSharedPtr<FJsonObject>* Arguments = nullptr;
		if (!CallObject->TryGetStringField(TEXT("toolset"), Call.ToolsetName)
			|| !CallObject->TryGetStringField(TEXT("tool"), Call.ToolName)
			|| Call.ToolsetName.IsEmpty()
			|| Call.ToolName.IsEmpty()
			|| !CallObject->TryGetObjectField(TEXT("arguments"), Arguments)
			|| !Arguments)
		{
			return UE58_Error(
				TEXT("INVALID_JSON_INPUT"),
				FString::Printf(
					TEXT("json_calls[%d] requires toolset, tool, and an arguments object"), Index));
		}
		Call.JsonInput = UE58_Serialize((*Arguments).ToSharedRef());
		FString PolicyError;
		if (!UE58_LoadOfficialPolicy(PolicyError))
		{
			return UE58_Error(TEXT("OFFICIAL_POLICY_UNAVAILABLE"), PolicyError);
		}
		const FString PolicyKey = Call.ToolsetName + TEXT("|") + Call.ToolName;
		const FUE58OfficialPolicyEntry* PolicyEntry = GUE58OfficialPolicy.Find(PolicyKey);
		if (!PolicyEntry
			|| (PolicyEntry->Access != TEXT("ReadOnly")
				&& PolicyEntry->Access != TEXT("TransactionalSync")))
		{
			return UE58_Error(
				TEXT("OFFICIAL_TOOL_NOT_APPROVED"),
				FString::Printf(
					TEXT("json_calls[%d] %s.%s must be ReadOnly or TransactionalSync"),
					Index, *Call.ToolsetName, *Call.ToolName));
		}
		Call.Access = PolicyEntry->Access;
		if (!UE58_AuthorizeOfficialTool(
			Call.ToolsetName,
			Call.ToolName,
			Call.Access,
			Call.Toolset,
			PolicyError))
		{
			return UE58_Error(
				TEXT("OFFICIAL_TOOL_NOT_APPROVED"),
				FString::Printf(TEXT("json_calls[%d]: %s"), Index, *PolicyError));
		}
		bHasMutation |= Call.Access == TEXT("TransactionalSync");
		Prepared.Add(MoveTemp(Call));
	}
	bHasMutation |= !NiagaraRenames.IsEmpty();
	if (!bHasMutation)
	{
		return UE58_Error(
			TEXT("TRANSACTION_REQUIRED"),
			TEXT("A transactional workflow must contain at least one TransactionalSync call"));
	}
	if (!NiagaraRenames.IsEmpty())
	{
		bool bSeenReadback = false;
		for (const FPreparedOfficialCall& Call : Prepared)
		{
			bSeenReadback |= Call.Access == TEXT("ReadOnly");
			if (bSeenReadback && Call.Access == TEXT("TransactionalSync"))
			{
				return UE58_Error(
					TEXT("INVALID_CALL_ORDER"),
					TEXT("When Niagara user parameters are renamed, all mutations must precede all readbacks"));
			}
		}
	}

	const FString ChangeSetId = UUnrealBridgeChangeSetLibrary::BeginChangeSet(
		FString::Printf(TEXT("Official transactional batch (%d calls)"), Prepared.Num()),
		TargetPackages,
		Prepared.ContainsByPredicate(
			[](const FPreparedOfficialCall& Call)
			{
				return Call.ToolsetName.Contains(
					TEXT("controlrig"), ESearchCase::IgnoreCase);
			}));
	if (ChangeSetId.IsEmpty())
	{
		return UE58_Error(
			TEXT("CHANGE_SET_REQUIRED"),
			TEXT("Transactional official batches must execute inside an authenticated UnrealBridge Job"));
	}

	TArray<TSharedPtr<FJsonValue>> CallResults;
	CallResults.Reserve(Prepared.Num());
	TArray<TSharedPtr<FJsonValue>> NiagaraRenameResults;
	NiagaraRenameResults.Reserve(NiagaraRenames.Num());
	bool bNiagaraRenamesApplied = NiagaraRenames.IsEmpty();
	FString NiagaraRenameError;
	auto ApplyNiagaraRenames = [&]() -> bool
	{
		if (bNiagaraRenamesApplied)
		{
			return true;
		}
		UNiagaraSystem* NiagaraSystem = LoadObject<UNiagaraSystem>(
			nullptr, *NiagaraSystemPathToCompile);
		if (!NiagaraSystem)
		{
			NiagaraRenameError = FString::Printf(
				TEXT("Could not load Niagara system '%s' for user-parameter rename"),
				*NiagaraSystemPathToCompile);
			return false;
		}

		for (const FNiagaraUserParameterRename& Rename : NiagaraRenames)
		{
			const FString OldLeafName = Rename.OldName.Mid(5);
			const FString NewLeafName = Rename.NewName.Mid(5);
			TArray<FNiagaraVariable> UserParameters;
			NiagaraSystem->GetExposedParameters().GetUserParameters(UserParameters);
			const FNiagaraVariable* OldParameter = UserParameters.FindByPredicate(
				[&OldLeafName](const FNiagaraVariable& Parameter)
				{
					return Parameter.GetName().ToString() == OldLeafName;
				});
			const bool bNewNameExists = UserParameters.ContainsByPredicate(
				[&NewLeafName](const FNiagaraVariable& Parameter)
				{
					return Parameter.GetName().ToString() == NewLeafName;
				});
			if (!OldParameter || bNewNameExists)
			{
				NiagaraRenameError = !OldParameter
					? FString::Printf(
						TEXT("Niagara user parameter '%s' was not found"),
						*Rename.OldName)
					: FString::Printf(
						TEXT("Niagara user parameter '%s' already exists"),
						*Rename.NewName);
				return false;
			}

			FNiagaraVariable OldCanonicalParameter = *OldParameter;
			if (!NiagaraSystem->GetExposedParameters().RedirectUserVariable(
				OldCanonicalParameter))
			{
				NiagaraRenameError = FString::Printf(
					TEXT("Could not resolve canonical Niagara user parameter '%s'"),
					*Rename.OldName);
				return false;
			}
			FNiagaraVariable NewCanonicalParameter = OldCanonicalParameter;
			NewCanonicalParameter.SetName(FName(*Rename.NewName));
			NiagaraSystem->Modify();
			TArray<UNiagaraGraph*> NiagaraGraphs;
			UE58_GatherNiagaraGraphs(NiagaraSystem, NiagaraGraphs);
			int32 GraphRenameCount = 0;
			for (UNiagaraGraph* Graph : NiagaraGraphs)
			{
				if (!Graph)
				{
					continue;
				}
				Graph->Modify();
				if (Graph->RenameParameter(
					OldCanonicalParameter,
					FName(*Rename.NewName),
					true))
				{
					++GraphRenameCount;
				}
			}
			if (UNiagaraSystemEditorData* EditorData =
				Cast<UNiagaraSystemEditorData>(NiagaraSystem->GetEditorData()))
			{
				EditorData->Modify();
				EditorData->RenameUserScriptVariable(
					OldCanonicalParameter, FName(*Rename.NewName));
			}
			NiagaraSystem->HandleVariableRenamed(
				OldCanonicalParameter, NewCanonicalParameter, true);

			TArray<FNiagaraVariable> RenamedParameters;
			NiagaraSystem->GetExposedParameters().GetUserParameters(RenamedParameters);
			const bool bOldStillExists = RenamedParameters.ContainsByPredicate(
				[&OldLeafName](const FNiagaraVariable& Parameter)
				{
					return Parameter.GetName().ToString() == OldLeafName;
				});
			const bool bNewExists = RenamedParameters.ContainsByPredicate(
				[&NewLeafName](const FNiagaraVariable& Parameter)
				{
					return Parameter.GetName().ToString() == NewLeafName;
				});
			if (bOldStillExists || !bNewExists)
			{
				NiagaraRenameError = FString::Printf(
					TEXT("Niagara user-parameter rename '%s' -> '%s' failed readback"),
					*Rename.OldName,
					*Rename.NewName);
				return false;
			}

			TSharedRef<FJsonObject> RenameResult = MakeShared<FJsonObject>();
			RenameResult->SetStringField(TEXT("old_name"), Rename.OldName);
			RenameResult->SetStringField(TEXT("new_name"), Rename.NewName);
			RenameResult->SetBoolField(TEXT("reference_update_requested"), true);
			RenameResult->SetNumberField(TEXT("graph_rename_count"), GraphRenameCount);
			RenameResult->SetBoolField(TEXT("readback_verified"), true);
			NiagaraRenameResults.Add(MakeShared<FJsonValueObject>(RenameResult));
		}
		bNiagaraRenamesApplied = true;
		return true;
	};
	auto AbortNiagaraRename = [&]() -> FString
	{
		const FBridgeChangeSetInfo Rollback = BridgeChangeSetRuntime::RollbackNow(
			ChangeSetId, TEXT("Niagara user-parameter rename failed"));
		TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
		Result->SetBoolField(TEXT("complete"), true);
		Result->SetStringField(
			TEXT("error_code"), TEXT("NIAGARA_USER_PARAMETER_RENAME_FAILED"));
		Result->SetStringField(TEXT("error"), NiagaraRenameError);
		Result->SetArrayField(TEXT("calls"), CallResults);
		Result->SetArrayField(
			TEXT("niagara_user_parameter_renames"), NiagaraRenameResults);
		Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(Rollback));
		Result->SetStringField(
			TEXT("side_effect_state"), UE58_ChangeSetSideEffectState(Rollback));
		return UE58_Serialize(Result);
	};
	for (int32 Index = 0; Index < Prepared.Num(); ++Index)
	{
		FPreparedOfficialCall& Call = Prepared[Index];
		if (Call.Access == TEXT("ReadOnly") && !ApplyNiagaraRenames())
		{
			return AbortNiagaraRename();
		}
		TFuture<TValueOrError<FString, FString>> Future = Call.Toolset->ExecuteTool(
			Call.ToolName, Call.JsonInput);
		if (!Future.IsReady())
		{
			const FBridgeChangeSetInfo Rollback =
				BridgeChangeSetRuntime::RollbackNow(
					ChangeSetId,
					TEXT("Official workflow call returned asynchronously"));
			TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
			Result->SetBoolField(TEXT("complete"), true);
			Result->SetStringField(TEXT("error_code"), TEXT("ASYNC_WORKFLOW_CALL_REFUSED"));
			Result->SetStringField(
				TEXT("error"),
				FString::Printf(
					TEXT("json_calls[%d] %s.%s returned asynchronously; transactional workflows require immediate completion"),
					Index, *Call.ToolsetName, *Call.ToolName));
			Result->SetNumberField(TEXT("failed_call_index"), Index);
			Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(Rollback));
			Result->SetStringField(
				TEXT("side_effect_state"),
				UE58_ChangeSetSideEffectState(Rollback));
			return UE58_Serialize(Result);
		}

		TValueOrError<FString, FString> ToolResult = Future.Get();
		if (ToolResult.HasError())
		{
			const FString ToolError = ToolResult.StealError();
			const FBridgeChangeSetInfo Rollback =
				BridgeChangeSetRuntime::RollbackNow(
					ChangeSetId,
					TEXT("Official workflow call failed"));
			TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
			Result->SetBoolField(TEXT("complete"), true);
			Result->SetStringField(TEXT("error_code"), TEXT("OFFICIAL_TOOL_FAILED"));
			Result->SetStringField(
				TEXT("error"),
				FString::Printf(
					TEXT("json_calls[%d] %s.%s failed: %s"),
					Index, *Call.ToolsetName, *Call.ToolName, *ToolError));
			Result->SetNumberField(TEXT("failed_call_index"), Index);
			Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(Rollback));
			Result->SetStringField(
				TEXT("side_effect_state"),
				UE58_ChangeSetSideEffectState(Rollback));
			return UE58_Serialize(Result);
		}

		const FString Value = ToolResult.StealValue();
		TSharedPtr<FJsonValue> ParsedValue;
		const TSharedRef<TJsonReader<>> ValueReader = TJsonReaderFactory<>::Create(Value);
		TSharedRef<FJsonObject> CallResult = MakeShared<FJsonObject>();
		CallResult->SetStringField(TEXT("toolset"), Call.ToolsetName);
		CallResult->SetStringField(TEXT("tool"), Call.ToolName);
		CallResult->SetStringField(TEXT("access"), Call.Access);
		if (FJsonSerializer::Deserialize(ValueReader, ParsedValue) && ParsedValue.IsValid())
		{
			CallResult->SetField(TEXT("result"), ParsedValue);
		}
		else
		{
			CallResult->SetStringField(TEXT("result_text"), Value);
		}
		CallResults.Add(MakeShared<FJsonValueObject>(CallResult));
	}
	if (!ApplyNiagaraRenames())
	{
		return AbortNiagaraRename();
	}

	TSharedPtr<FJsonObject> NiagaraCompileResult;
	TSharedPtr<FJsonObject> NiagaraValidationResult;
	if (!NiagaraSystemPathToCompile.IsEmpty())
	{
		NiagaraCompileResult = MakeShared<FJsonObject>();
		NiagaraCompileResult->SetBoolField(TEXT("requested"), true);
		NiagaraCompileResult->SetStringField(
			TEXT("system_path"), NiagaraSystemPathToCompile);

		UNiagaraSystem* NiagaraSystem = LoadObject<UNiagaraSystem>(
			nullptr, *NiagaraSystemPathToCompile);
		if (!NiagaraSystem)
		{
			const FBridgeChangeSetInfo Rollback =
				BridgeChangeSetRuntime::RollbackNow(
					ChangeSetId,
					TEXT("Niagara compile target could not be loaded"));
			TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
			Result->SetBoolField(TEXT("complete"), true);
			Result->SetStringField(TEXT("error_code"), TEXT("NIAGARA_COMPILE_TARGET_NOT_FOUND"));
			Result->SetStringField(
				TEXT("error"),
				FString::Printf(
					TEXT("Could not load Niagara system '%s' after transactional mutations"),
					*NiagaraSystemPathToCompile));
			Result->SetArrayField(TEXT("calls"), MoveTemp(CallResults));
			NiagaraCompileResult->SetBoolField(TEXT("success"), false);
			Result->SetObjectField(TEXT("niagara_compile"), NiagaraCompileResult);
			Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(Rollback));
			Result->SetStringField(
				TEXT("side_effect_state"),
				UE58_ChangeSetSideEffectState(Rollback));
			return UE58_Serialize(Result);
		}

		const double CompileStartSeconds = FPlatformTime::Seconds();
		NiagaraSystem->RequestCompile(true);
		NiagaraSystem->WaitForCompilationComplete(false, false);
		const bool bPollReturnedComplete =
			NiagaraSystem->PollForCompilationComplete(false);
		const bool bOutstanding = NiagaraSystem->HasOutstandingCompilationRequests(false);
		const bool bNeedsRequest = NiagaraSystem->NeedsRequestCompile();
		const bool bReadyToRun = NiagaraSystem->IsReadyToRun();
		const bool bCompileComplete = !bOutstanding && !bNeedsRequest;
		const bool bCompileSucceeded = bCompileComplete && bReadyToRun;
		NiagaraCompileResult->SetBoolField(TEXT("success"), bCompileSucceeded);
		NiagaraCompileResult->SetBoolField(TEXT("compile_complete"), bCompileComplete);
		NiagaraCompileResult->SetBoolField(
			TEXT("poll_returned_complete"), bPollReturnedComplete);
		NiagaraCompileResult->SetBoolField(TEXT("outstanding_requests"), bOutstanding);
		NiagaraCompileResult->SetBoolField(TEXT("needs_request_compile"), bNeedsRequest);
		NiagaraCompileResult->SetBoolField(TEXT("ready_to_run"), bReadyToRun);
		NiagaraCompileResult->SetNumberField(
			TEXT("duration_seconds"), FPlatformTime::Seconds() - CompileStartSeconds);

		if (!bCompileSucceeded)
		{
			const FBridgeChangeSetInfo Rollback =
				BridgeChangeSetRuntime::RollbackNow(
					ChangeSetId,
					TEXT("Niagara compile postcondition failed"));
			TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
			Result->SetBoolField(TEXT("complete"), true);
			Result->SetStringField(TEXT("error_code"), TEXT("NIAGARA_COMPILE_FAILED"));
			Result->SetStringField(
				TEXT("error"),
				TEXT("Niagara system did not become compiled and ready to run"));
			Result->SetArrayField(TEXT("calls"), MoveTemp(CallResults));
			Result->SetObjectField(TEXT("niagara_compile"), NiagaraCompileResult);
			Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(Rollback));
			Result->SetStringField(
				TEXT("side_effect_state"),
				UE58_ChangeSetSideEffectState(Rollback));
			return UE58_Serialize(Result);
		}

		const FBridgeNiagaraValidationResult Validation =
			UUnrealBridgeNiagaraLibrary::ValidateNiagaraSystemGraph(
				NiagaraSystemPathToCompile);
		NiagaraValidationResult = MakeShared<FJsonObject>();
		NiagaraValidationResult->SetBoolField(TEXT("success"), Validation.bSuccess);
		NiagaraValidationResult->SetNumberField(
			TEXT("error_count"), Validation.ErrorCount);
		NiagaraValidationResult->SetNumberField(
			TEXT("warning_count"), Validation.WarningCount);
		NiagaraValidationResult->SetArrayField(
			TEXT("messages"), UE58_StringArray(Validation.Messages));
		if (!Validation.bSuccess)
		{
			const FBridgeChangeSetInfo Rollback =
				BridgeChangeSetRuntime::RollbackNow(
					ChangeSetId,
					TEXT("Niagara graph validation postcondition failed"));
			TSharedRef<FJsonObject> Result = UE58_BaseResult(false);
			Result->SetBoolField(TEXT("complete"), true);
			Result->SetStringField(
				TEXT("error_code"), TEXT("NIAGARA_VALIDATION_FAILED"));
			Result->SetStringField(
				TEXT("error"),
				TEXT("Niagara graph validation reported one or more structural errors"));
			Result->SetArrayField(TEXT("calls"), MoveTemp(CallResults));
			Result->SetObjectField(TEXT("niagara_compile"), NiagaraCompileResult);
			Result->SetObjectField(
				TEXT("niagara_validation"), NiagaraValidationResult);
			Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(Rollback));
			Result->SetStringField(
				TEXT("side_effect_state"),
				UE58_ChangeSetSideEffectState(Rollback));
			return UE58_Serialize(Result);
		}
	}

	const FBridgeChangeSetInfo Preview =
		UUnrealBridgeChangeSetLibrary::PreviewChangeSet(ChangeSetId);
	FBridgeChangeSetInfo FinalAction;
	if (!Preview.bCanCommit || !bApply)
	{
		FinalAction = BridgeChangeSetRuntime::RollbackNow(
			ChangeSetId,
			bApply
				? TEXT("ChangeSet preview rejected workflow commit")
				: TEXT("Workflow preview completed and rolled back"));
	}
	else
	{
		FinalAction = BridgeChangeSetRuntime::CommitNow(ChangeSetId, false);
	}

	TSharedRef<FJsonObject> Result = UE58_BaseResult(FinalAction.bSuccess);
	Result->SetBoolField(TEXT("complete"), true);
	Result->SetBoolField(TEXT("apply_requested"), bApply);
	Result->SetBoolField(TEXT("saved"), false);
	Result->SetNumberField(TEXT("call_count"), Prepared.Num());
	Result->SetNumberField(
		TEXT("niagara_user_parameter_rename_count"), NiagaraRenameResults.Num());
	Result->SetBoolField(TEXT("contains_readback"), Prepared.ContainsByPredicate(
		[](const FPreparedOfficialCall& Call)
		{
			return Call.Access == TEXT("ReadOnly");
		}));
	Result->SetArrayField(TEXT("calls"), MoveTemp(CallResults));
	Result->SetArrayField(
		TEXT("niagara_user_parameter_renames"), MoveTemp(NiagaraRenameResults));
	if (NiagaraCompileResult.IsValid())
	{
		Result->SetObjectField(TEXT("niagara_compile"), NiagaraCompileResult);
	}
	if (NiagaraValidationResult.IsValid())
	{
		Result->SetObjectField(
			TEXT("niagara_validation"), NiagaraValidationResult);
	}
	Result->SetObjectField(TEXT("preview"), UE58_ChangeSetJson(Preview));
	Result->SetObjectField(TEXT("change_set"), UE58_ChangeSetJson(FinalAction));
	Result->SetStringField(
		TEXT("side_effect_state"),
		UE58_ChangeSetSideEffectState(FinalAction));
	if (!FinalAction.bSuccess)
	{
		Result->SetStringField(TEXT("error_code"), TEXT("CHANGE_SET_REJECTED"));
		Result->SetStringField(TEXT("error"), FinalAction.Error);
	}
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
	UE58_AttachCatalogRevision(Subsystem.GetValue());
	FString PolicyError;
	if (!UE58_LoadOfficialPolicy(PolicyError))
	{
		UE_LOG(LogUnrealBridgeUE58, Error, TEXT("Official Toolset policy unavailable; execution remains denied: %s"), *PolicyError);
	}
	else
	{
		UE_LOG(LogUnrealBridgeUE58, Log, TEXT("Loaded %d exact official Toolset policy entries"), GUE58OfficialPolicy.Num());
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
	if (auto* Subsystem = GUE58CatalogSubsystem.Get()) Subsystem->ToolsetRegistry.OnToolsetRegistered().Remove(GUE58CatalogChangedHandle);
	GUE58CatalogChangedHandle.Reset();
	GUE58CatalogSubsystem.Reset();
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
	GUE58OfficialPolicy.Reset();
	GUE58OfficialPolicyJson.Reset();
	GUE58OfficialPolicyError.Reset();
	GUE58OfficialPolicyLoaded = false;
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
