#include "UnrealBridgeAuthoringCommon.h"
#include "UnrealBridgeUpgradeLibrary.h"
#include "UnrealBridgeChangeSetLibrary.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"

namespace BridgeAuthoring
{
	struct FReceipt { FString Digest,Reply; };
	TMap<FString,FReceipt> Receipts;
	FString Encode(const TSharedRef<FJsonObject>& Object)
	{ FString Text; FJsonSerializer::Serialize(Object,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text; }
	TSharedPtr<FJsonObject> Decode(const FString& Text)
	{ TSharedPtr<FJsonObject> Value; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Value)) return nullptr; return Value; }
	FString String(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
	{ FString Value; if (Object) Object->TryGetStringField(Key,Value); return Value; }
	bool Number(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,double& Out)
	{ const auto* Field=Object?Object->Values.Find(Key):nullptr; return Field && (*Field)->Type==EJson::Number && (*Field)->TryGetNumber(Out) && FMath::IsFinite(Out); }
	FString Hash(const FString& Text)
	{ FTCHARToUTF8 Value(*Text); return FSHA1::HashBuffer(Value.Get(),Value.Length()).ToString().ToLower(); }
	FString Error(const TCHAR* Code,const FString& Message,const TCHAR* SideEffect)
	{
		auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),false); Value->SetStringField(TEXT("status"),TEXT("rejected"));
		Value->SetStringField(TEXT("error_code"),Code); Value->SetStringField(TEXT("error"),Message); Value->SetStringField(TEXT("side_effect_state"),SideEffect);
		Value->SetBoolField(TEXT("retryable"),false); return Encode(Value);
	}
	bool Fields(const TSharedPtr<FJsonObject>& Object,std::initializer_list<const TCHAR*> Names)
	{
		if (!Object || Object->Values.Num()!=Names.size()) return false;
		for (const TCHAR* Name:Names) if (!Object->HasField(Name)) return false;
		return true;
	}
	bool Parse(const FString& Text,const TCHAR* Operation,FRequest& Out,FString& Reply)
	{
		if (!IsInGameThread() || FTCHARToUTF8(*Text).Length()>65536) { Reply=Error(TEXT("ValidationFailed"),TEXT("GameThread and 64 KiB request limit required")); return false; }
		Out.Json=Decode(Text); Out.Operation=Operation; Out.Digest=Hash(Text);
		if (!Fields(Out.Json,{TEXT("schema"),TEXT("operation_id"),TEXT("context"),TEXT("operations"),TEXT("save_policy")})
			|| String(Out.Json,TEXT("schema"))!=TEXT("unrealbridge.authoring.v1") || String(Out.Json,TEXT("operation_id"))!=Operation)
		{ Reply=Error(TEXT("UnsupportedCapability"),TEXT("Exact typed authoring envelope required")); return false; }
		const TSharedPtr<FJsonObject>* Context=nullptr; const TArray<TSharedPtr<FJsonValue>>* Ops=nullptr;
		const FString Save=String(Out.Json,TEXT("save_policy"));
		if (!Out.Json->TryGetObjectField(TEXT("context"),Context) || !Out.Json->TryGetArrayField(TEXT("operations"),Ops)
			|| Ops->Num()<1 || Ops->Num()>64 || (Save!=TEXT("never") && Save!=TEXT("declared_targets")))
		{ Reply=Error(TEXT("ValidationFailed"),TEXT("Bounded operations, context and explicit save policy required")); return false; }
		Out.Context=*Context; Out.Operations=*Ops; Out.Save=Save==TEXT("declared_targets");
		if (!Out.Context->HasTypedField<EJson::Boolean>(TEXT("dry_run")) || !Out.Context->TryGetBoolField(TEXT("dry_run"),Out.DryRun) || (Out.DryRun && Out.Save))
		{ Reply=Error(TEXT("ScopeViolation"),TEXT("Dry run cannot save")); return false; }
		Out.RequestId=String(Out.Context,TEXT("request_id"));
		// Session is checked before serving a cached result after an Editor restart.
		const auto Current=Decode(UUnrealBridgeUpgradeLibrary::GetAuthoringSnapshot(TEXT("[]")));
		if (!Current || String(Current,TEXT("editor_session_id"))!=String(Out.Context,TEXT("editor_session_id")))
		{ Reply=Error(TEXT("StaleHandle"),TEXT("Editor session changed")); return false; }
		Out.ReceiptKey=String(Out.Context,TEXT("project_identity"))+TEXT("|")+String(Out.Context,TEXT("editor_session_id"))+TEXT("|")+Out.RequestId;
		if (!Out.DryRun) if (const auto* Receipt=Receipts.Find(Out.ReceiptKey))
		{
			Reply=Receipt->Digest==Out.Digest?Receipt->Reply:Error(TEXT("ValidationFailed"),TEXT("Same request ID has different input")); return false;
		}
		Out.Snapshot=Decode(UUnrealBridgeUpgradeLibrary::ValidateUpgradeRequest(Encode(Out.Context.ToSharedRef())));
		if (!Out.Snapshot || !Out.Snapshot->GetBoolField(TEXT("ok")))
		{ Reply=Out.Snapshot?Encode(Out.Snapshot.ToSharedRef()):Error(TEXT("NeedsReconciliation"),TEXT("Native context validation failed")); return false; }
		const auto& Targets=Out.Snapshot->GetArrayField(TEXT("target_packages"));
		if (Targets.Num()<1 || (Out.Operation!=TEXT("audio.routing") && Targets.Num()!=1)) { Reply=Error(TEXT("ScopeViolation"),TEXT("Only typed audio routing accepts multiple declared related assets")); return false; }
		for (const auto& Target:Targets) Out.Targets.Add(Target->AsString());
		Out.Target=Targets[0]->AsString();
		if (!Out.DryRun && Receipts.Num()>=256) { Reply=Error(TEXT("NeedsReconciliation"),TEXT("Native authoring receipt budget reached")); return false; }
		return true;
	}
	bool Begin(FRequest& Request,FString& Reply)
	{
		if (Request.DryRun) { Reply=Error(TEXT("ScopeViolation"),TEXT("Apply requires dry_run=false")); return false; }
		Request.ChangeSet=UUnrealBridgeChangeSetLibrary::BeginGuardedChangeSet(TEXT("Author ")+Request.Operation,Request.Targets);
		const auto State=UUnrealBridgeChangeSetLibrary::PreviewChangeSet(Request.ChangeSet);
		if (Request.ChangeSet.IsEmpty() || !State.Error.IsEmpty())
		{ Reply=Error(TEXT("NeedsReconciliation"),TEXT("Guarded current Job transaction unavailable: ")+State.Error); return false; }
		return true;
	}
	FString Preview(FRequest& Request,const TSharedRef<FJsonObject>& Model)
	{
		auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),true); Value->SetStringField(TEXT("status"),TEXT("previewed"));
		Value->SetStringField(TEXT("operation_id"),Request.Operation); Value->SetStringField(TEXT("request_id"),Request.RequestId);
		Value->SetStringField(TEXT("side_effect_state"),TEXT("none")); Value->SetBoolField(TEXT("saved"),false);
		Value->SetObjectField(TEXT("model"),Model); Value->SetObjectField(TEXT("before"),Request.Snapshot.ToSharedRef()); return Encode(Value);
	}
	FString Finish(FRequest& Request,const TSharedRef<FJsonObject>& Model)
	{
		// Finalize before producing the receipt so saved means disk persistence, not a pending request.
		const auto State=BridgeChangeSetRuntime::CommitNow(Request.ChangeSet,Request.Save);
		auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),State.bSuccess);
		Value->SetStringField(TEXT("status"),State.bSuccess?TEXT("applied"):TEXT("needs_reconciliation"));
		Value->SetStringField(TEXT("operation_id"),Request.Operation); Value->SetStringField(TEXT("request_id"),Request.RequestId);
		Value->SetStringField(TEXT("change_set_id"),Request.ChangeSet); Value->SetBoolField(TEXT("saved"),State.bSaved);
		Value->SetStringField(TEXT("error"),State.Error); Value->SetStringField(TEXT("error_code"),State.bSuccess?TEXT(""):TEXT("NeedsReconciliation"));
		Value->SetStringField(TEXT("side_effect_state"),State.bSuccess?TEXT("complete"):TEXT("retained_unsaved"));
		Value->SetObjectField(TEXT("model"),Model); Value->SetArrayField(TEXT("applied_operations"),Request.Operations);
		TArray<TSharedPtr<FJsonValue>> Dirty; for (const FString& Target:State.DirtyPackagesForJob) Dirty.Add(MakeShared<FJsonValueString>(Target));
		Value->SetArrayField(TEXT("dirty_delta"),Dirty);
		const FString Reply=Encode(Value); Receipts.Add(Request.ReceiptKey,{Request.Digest,Reply}); return Reply;
	}
}
