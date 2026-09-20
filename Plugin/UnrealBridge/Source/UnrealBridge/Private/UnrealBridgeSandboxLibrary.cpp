#include "UnrealBridgeSandboxLibrary.h"
#include "UnrealBridgeWorldLibrary.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"

#if UNREALBRIDGE_WITH_FILE_SANDBOX
#include "IFileSandboxCoreModule.h"
#include "ISandboxManager.h"
#include "ISandboxInstance.h"
#include "Types/EBreakBehavior.h"
#include "Types/Manager/NewSandboxArgs.h"
#include "Types/Manager/LoadSandboxByNameArgs.h"
#include "Types/Sandbox/PersistArgs.h"
#endif

namespace BridgeSandboxImpl
{
	TSharedPtr<FJsonObject> Lease;
	int32 Generation = 0;
	FString Json(const TSharedPtr<FJsonObject>& Value)
	{
		FString Out;
		FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
		return Out;
	}
	TSharedPtr<FJsonObject> Read(const FString& Text)
	{
		TSharedPtr<FJsonObject> Value;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value);
		return Value;
	}
	FString Get(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		FString Value;
		if (Object) Object->TryGetStringField(Key, Value);
		return Value;
	}
	FString SandboxError(const TCHAR* Code, const FString& Reason, const TCHAR* Effects = TEXT("none"))
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("ok"), false);
		Out->SetStringField(TEXT("error_code"), Code);
		Out->SetStringField(TEXT("error"), Reason);
		Out->SetStringField(TEXT("side_effect_state"), Effects);
		return Json(Out);
	}
	FString Normalize(FString Path)
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Path);
#if PLATFORM_WINDOWS
		Path.ToLowerInline();
#endif
		return Path;
	}
	bool Identifier(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 96) return false;
		for (TCHAR C : Value) if (!FChar::IsAlnum(C) && C != '_' && C != '-') return false;
		return true;
	}
	FString Session()
	{
		return Get(Read(UUnrealBridgeWorldLibrary::GetWorldContexts(64)), TEXT("editor_session_id"));
	}
	FString HashBytes(const TArray<uint8>& Data)
	{
		FSHAHash Hash;
		FSHA1::HashBuffer(Data.GetData(), Data.Num(), Hash.Hash);
		return Hash.ToString().ToLower();
	}
	FString HashText(const FString& Text)
	{
		FTCHARToUTF8 Bytes(*Text);
		FSHAHash Hash;
		FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
		return Hash.ToString().ToLower();
	}
	FString FileHash(const FString& Path, bool bPhysical)
	{
		IPlatformFile* Files = &FPlatformFileManager::Get().GetPlatformFile();
		if (bPhysical) while (Files->GetLowerLevel()) Files = Files->GetLowerLevel();
		if (!Files->FileExists(*Path)) return TEXT("absent");
		const int64 Size = Files->FileSize(*Path);
		if (Size < 0 || Size > 32 * 1024 * 1024) return TEXT("unreadable_or_over_budget");
		TUniquePtr<IFileHandle> Handle(Files->OpenRead(*Path));
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Size);
		if (!Handle || !Handle->Read(Bytes.GetData(), Size)) return TEXT("unreadable_or_over_budget");
		return HashBytes(Bytes);
	}
	bool Quiescent()
	{
		if (!GEditor || GEditor->PlayWorld || GEditor->IsPlaySessionRequestQueued()
			|| (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
			|| FAssetCompilingManager::Get().GetNumRemainingAssets() > 0) return false;
		TArray<UPackage*> Dirty;
		FEditorFileUtils::GetDirtyContentPackages(Dirty);
		FEditorFileUtils::GetDirtyWorldPackages(Dirty);
		return Dirty.IsEmpty();
	}
	FString JournalPath(const FString& Name)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealBridge/SandboxLeases"), Name + TEXT(".json"));
	}
	bool Journal()
	{
		const FString Path = JournalPath(Get(Lease, TEXT("name")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		const FString Temp = Path + TEXT(".pending");
		return FFileHelper::SaveStringToFile(Json(Lease), *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			&& IFileManager::Get().Move(*Path, *Temp, true, true);
	}
#if UNREALBRIDGE_WITH_FILE_SANDBOX
	using namespace UE::FileSandboxCore;
	ISandboxManager* Manager()
	{
		auto* Module = FModuleManager::GetModulePtr<IFileSandboxCoreModule>(TEXT("FileSandboxCore"));
		return Module ? &Module->GetSandboxManager() : nullptr;
	}
	bool Owned(ISandboxInstance* Instance)
	{
		return Instance && Lease && Normalize(Instance->GetRootDirectory()) == Get(Lease, TEXT("root"));
	}
	TSharedPtr<FJsonObject> Changes(ISandboxInstance* Instance)
	{
		auto Out = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Rows;
		bool Complete = true;
		Instance->EnumerateFileChanges([&](const FSandboxedFileChangeInfo& Change)
		{
			if (Rows.Num() >= 128) { Complete = false; return EBreakBehavior::Break; }
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("path"), Normalize(Change.Path));
			Row->SetNumberField(TEXT("action"), static_cast<int32>(Change.Action));
			Row->SetStringField(TEXT("main_sha1"), FileHash(Change.Path, true));
			Row->SetStringField(TEXT("view_sha1"), FileHash(Change.Path, false));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			return EBreakBehavior::Continue;
		});
		Rows.Sort([](const auto& A, const auto& B) { return Get(A->AsObject(), TEXT("path")) < Get(B->AsObject(), TEXT("path")); });
		Out->SetArrayField(TEXT("changes"), Rows);
		Out->SetBoolField(TEXT("complete"), Complete);
		return Out;
	}
#endif
}

FString UUnrealBridgeSandboxLibrary::GetSandboxStatus()
{
	using namespace BridgeSandboxImpl;
	auto Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), true);
	Out->SetStringField(TEXT("schema"), TEXT("unrealbridge.sandbox.v1"));
	Out->SetStringField(TEXT("project_identity"), Normalize(FPaths::GetProjectFilePath()));
	Out->SetStringField(TEXT("editor_session_id"), Session());
	Out->SetNumberField(TEXT("generation"), Generation);
	Out->SetBoolField(TEXT("quiescent"), Quiescent());
	Out->SetBoolField(TEXT("atomic_multi_asset"), false);
	Out->SetBoolField(TEXT("external_writer_race_eliminated"), false);
#if UNREALBRIDGE_WITH_FILE_SANDBOX
	auto* M = Manager();
	Out->SetBoolField(TEXT("supported"), M != nullptr);
	auto* Instance = M ? M->GetActiveSandboxInstance() : nullptr;
	Out->SetBoolField(TEXT("active"), Instance != nullptr);
	Out->SetBoolField(TEXT("owned"), Owned(Instance));
	if (Instance)
	{
		Out->SetStringField(TEXT("root"), Normalize(Instance->GetRootDirectory()));
		Out->SetObjectField(TEXT("change_manifest"), Changes(Instance));
		if (Owned(Instance))
		{
			Out->SetObjectField(TEXT("lease_record"), Lease);
			Out->SetStringField(TEXT("journal_sha1"), HashText(Json(Lease)));
		}
	}
#else
	Out->SetBoolField(TEXT("supported"), false);
	Out->SetBoolField(TEXT("active"), false);
	Out->SetStringField(TEXT("reason"), TEXT("FileSandbox was absent or disabled when built"));
#endif
	return Json(Out);
}

FString UUnrealBridgeSandboxLibrary::SandboxRequest(const FString& RequestJson)
{
	using namespace BridgeSandboxImpl;
	if (!IsInGameThread() || RequestJson.Len() > 32768) return SandboxError(TEXT("ValidationFailed"), TEXT("Invalid thread or request size"));
	auto Request = Read(RequestJson);
	if (!Request || Get(Request, TEXT("schema")) != TEXT("unrealbridge.sandbox.v1")) return SandboxError(TEXT("ValidationFailed"), TEXT("Invalid schema"));
	const TSet<FString> Fields = {TEXT("schema"), TEXT("action"), TEXT("project_identity"), TEXT("editor_session_id"),
		TEXT("work_order_id"), TEXT("name"), TEXT("target_packages"), TEXT("lease_id"), TEXT("review_hash"), TEXT("saved_hashes")};
	for (const auto& Pair : Request->Values) if (!Fields.Contains(FString(Pair.Key))) return SandboxError(TEXT("ValidationFailed"), FString(TEXT("Unknown field: ")) + FString(Pair.Key));
	if (Normalize(Get(Request, TEXT("project_identity"))) != Normalize(FPaths::GetProjectFilePath())
		|| Get(Request, TEXT("editor_session_id")) != Session()) return SandboxError(TEXT("StaleHandle"), TEXT("Project/session mismatch"));
#if !UNREALBRIDGE_WITH_FILE_SANDBOX
	return SandboxError(TEXT("UnsupportedCapability"), TEXT("FileSandbox not compiled"));
#else
	auto* M = Manager();
	if (!M) return SandboxError(TEXT("UnsupportedCapability"), TEXT("FileSandbox module is not loaded"));
	const FString Action = Get(Request, TEXT("action"));
	const FString Owner = Get(Request, TEXT("work_order_id"));
	if (!Identifier(Owner)) return SandboxError(TEXT("ValidationFailed"), TEXT("Exact work_order_id required"));
	if (!Quiescent()) return SandboxError(TEXT("DirtyConflict"), TEXT("Sandbox transition/review requires no Dirty, PIE or compilation"));
	if (Action == TEXT("begin") || Action == TEXT("restore_for_review"))
	{
		if (M->HasActiveSandbox()) return SandboxError(TEXT("ScopeViolation"), TEXT("An existing sandbox must not be taken over"));
		const FString Name = Get(Request, TEXT("name"));
		if (!Identifier(Name) || !Name.StartsWith(TEXT("UB_"))) return SandboxError(TEXT("ValidationFailed"), TEXT("Expected a bounded UB_ sandbox name"));
		if (Action == TEXT("restore_for_review"))
		{
			FString Saved;
			if (!FFileHelper::LoadFileToString(Saved, *JournalPath(Name))) return SandboxError(TEXT("NeedsReconciliation"), TEXT("No owned journal"));
			auto Record = Read(Saved);
			if (!Record || Get(Record, TEXT("owner")) != Owner || Get(Record, TEXT("project")) != Normalize(FPaths::GetProjectFilePath())
				|| HashText(Saved) != Get(Request, TEXT("review_hash"))) return SandboxError(TEXT("ScopeViolation"), TEXT("Restore journal identity/hash changed"));
			const auto RestoreResult = M->LoadNamedSandbox(FLoadSandboxByNameArgs(Name));
			if (!RestoreResult.HasValue()) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Cannot restore named sandbox"));
			ISandboxInstance* Restored = NotNullGet(RestoreResult.GetValue());
			Lease = Record;
			Lease->SetStringField(TEXT("lease_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Lease->SetStringField(TEXT("root"), Normalize(Restored->GetRootDirectory()));
			// Retain immutable saved receipts across a crash, including files already
			// persisted. The new lease/generation invalidates every old review hash;
			// preview/reconciliation rehashes the actual files before further writes.
			Lease->SetStringField(TEXT("state"), TEXT("restored_requires_saved_hash_reconciliation"));
		}
		else
		{
			const TArray<TSharedPtr<FJsonValue>>* Targets;
			if (!Request->TryGetArrayField(TEXT("target_packages"), Targets) || Targets->Num() < 1 || Targets->Num() > 16)
				return SandboxError(TEXT("ScopeViolation"), TEXT("1..16 exact packages required"));
			auto Baselines = MakeShared<FJsonObject>();
			for (const auto& Value : *Targets)
			{
				FString Package, Path;
				if (!Value->TryGetString(Package) || !FPackageName::IsValidLongPackageName(Package) || Package.Contains(TEXT("."))
					|| !FPackageName::TryConvertLongPackageNameToFilename(Package, Path, FPackageName::GetAssetPackageExtension()))
					return SandboxError(TEXT("ScopeViolation"), TEXT("Invalid package target"));
				Path = Normalize(Path);
				const FString Content = Normalize(FPaths::ProjectContentDir());
				const FString Plugins = Normalize(FPaths::ProjectPluginsDir());
				if ((!FPaths::IsUnderDirectory(Path, Content) && !FPaths::IsUnderDirectory(Path, Plugins)) || Baselines->HasField(Path))
					return SandboxError(TEXT("ScopeViolation"), TEXT("Only unique project-owned content is allowed"));
				const FString Hash = FileHash(Path, true);
				if (Hash == TEXT("unreadable_or_over_budget")) return SandboxError(TEXT("ValidationFailed"), TEXT("Asset hash budget exceeded"));
				Baselines->SetStringField(Path, Hash);
			}
			Lease = MakeShared<FJsonObject>();
			Lease->SetStringField(TEXT("name"), Name);
			Lease->SetStringField(TEXT("owner"), Owner);
			Lease->SetStringField(TEXT("project"), Normalize(FPaths::GetProjectFilePath()));
			Lease->SetStringField(TEXT("lease_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Lease->SetStringField(TEXT("state"), TEXT("begin_intent"));
			Lease->SetObjectField(TEXT("baselines"), Baselines);
			Lease->SetObjectField(TEXT("sealed"), MakeShared<FJsonObject>());
			if (IFileManager::Get().FileExists(*JournalPath(Name))) { Lease.Reset(); return SandboxError(TEXT("ScopeViolation"), TEXT("Journal name already exists; reconcile instead of replacing")); }
			if (!Journal()) { Lease.Reset(); return SandboxError(TEXT("NeedsReconciliation"), TEXT("Cannot persist begin intent")); }
			const auto CreateResult = M->CreateNewSandbox(FNewSandboxArgs(Name, TEXT("UnrealBridge owned content work order ") + Owner));
			if (!CreateResult.HasValue()) { Lease.Reset(); return SandboxError(TEXT("NeedsReconciliation"), TEXT("Sandbox creation failed; journal retained")); }
			ISandboxInstance* Created = NotNullGet(CreateResult.GetValue());
			Lease->SetStringField(TEXT("root"), Normalize(Created->GetRootDirectory()));
			Lease->SetStringField(TEXT("state"), TEXT("isolated"));
		}
		++Generation;
		if (!Journal()) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Sandbox active; journal update failed"), TEXT("changed"));
		return GetSandboxStatus();
	}
	auto* Instance = M->GetActiveSandboxInstance();
	if (!Owned(Instance) || Get(Lease, TEXT("owner")) != Owner || Get(Lease, TEXT("lease_id")) != Get(Request, TEXT("lease_id")))
		return SandboxError(TEXT("StaleHandle"), TEXT("Exact sandbox owner/lease required"));
	if (Action == TEXT("leave_preserving"))
	{
		Lease->SetStringField(TEXT("state"), TEXT("leaving_preserving"));
		if (!Journal()) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Cannot record leave intent"));
		if (!M->LeaveSandbox()) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Sandbox could not be left"));
		Lease.Reset(); ++Generation;
		return GetSandboxStatus();
	}
	auto Manifest = Changes(Instance);
	if (!Manifest->GetBoolField(TEXT("complete"))) return SandboxError(TEXT("ScopeViolation"), TEXT("Incomplete change enumeration"));
	auto Baselines = Lease->GetObjectField(TEXT("baselines"));
	auto Sealed = Lease->GetObjectField(TEXT("sealed"));
	const auto& Rows = Manifest->GetArrayField(TEXT("changes"));
	if (Action == TEXT("reconcile_persist"))
	{
		for (const auto& Value : Rows)
			if (!Baselines->HasField(Get(Value->AsObject(), TEXT("path"))))
				return SandboxError(TEXT("ScopeViolation"), TEXT("Unknown change requires review before reconciliation"));
		// Read actual files after a lost reply/interrupted persist; never replay a write.
		TArray<TSharedPtr<FJsonValue>> Reconciled;
		bool AllApplied = !Sealed->Values.IsEmpty();
		for (const auto& Pair : Sealed->Values)
		{
			const FString Path(Pair.Key), Expected = Pair.Value->AsString();
			if (!Baselines->HasField(Path) || Expected.Len() != 40)
				return SandboxError(TEXT("NeedsReconciliation"), TEXT("Invalid sealed target"));
			const FString Actual = FileHash(Path, true);
			if (Actual != Expected && Actual != Get(Baselines, *Path))
				return SandboxError(TEXT("TargetRevisionMismatch"), TEXT("External file conflicts with both baseline and saved receipt"));
			const bool Applied = Actual == Expected;
			auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("path"), Path);
			Row->SetBoolField(TEXT("confirmed"), Applied); Row->SetStringField(TEXT("main_sha1"), Actual);
			Reconciled.Add(MakeShared<FJsonValueObject>(Row)); AllApplied &= Applied;
			if (Applied) Baselines->SetStringField(Path, Actual);
		}
		Lease->SetArrayField(TEXT("persist_results"), Reconciled);
		Lease->SetStringField(TEXT("state"), AllApplied ? TEXT("persisted_requires_main_readback") : TEXT("partially_persisted_needs_review"));
		if (!Journal()) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Reconciliation journal failed"));
		return GetSandboxStatus();
	}
	for (const auto& Value : Rows)
	{
		auto Row = Value->AsObject(); const FString Path = Get(Row, TEXT("path"));
		if (!Baselines->HasField(Path) || Row->GetIntegerField(TEXT("action")) == static_cast<int32>(ESandboxFileChange::Removed))
			return SandboxError(TEXT("ScopeViolation"), TEXT("Unknown or deleted asset blocks persistence"));
		if (Get(Baselines, *Path) != Get(Row, TEXT("main_sha1"))) return SandboxError(TEXT("TargetRevisionMismatch"), TEXT("Main-project file changed externally"));
	}
	if (Action == TEXT("record_saved"))
	{
		const TSharedPtr<FJsonObject>* Receipts;
		if (!Request->TryGetObjectField(TEXT("saved_hashes"), Receipts) || (*Receipts)->Values.Num() != Rows.Num())
			return SandboxError(TEXT("ValidationFailed"), TEXT("Provide exact observed saved hashes for reconciliation"));
		for (const auto& Value : Rows)
		{
			auto Row = Value->AsObject(); const FString Path = Get(Row, TEXT("path"));
			const FString Hash = Get(Row, TEXT("view_sha1"));
			if (Hash.Len() != 40 || Get(*Receipts, *Path) != Hash) return SandboxError(TEXT("TargetRevisionMismatch"), TEXT("Saved receipt changed"));
			Sealed->SetStringField(Path, Hash);
		}
		Lease->SetStringField(TEXT("state"), TEXT("saved_to_sandbox"));
		if (!Journal()) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Cannot record saved receipts"));
		return GetSandboxStatus();
	}
	if (Action != TEXT("preview_persist") && Action != TEXT("persist_approved")) return SandboxError(TEXT("ValidationFailed"), TEXT("Unsupported action"));
	for (const auto& Value : Rows)
	{
		auto Row = Value->AsObject(); const FString Path = Get(Row, TEXT("path"));
		if (Get(Sealed, *Path) != Get(Row, TEXT("view_sha1"))) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Unsealed/manual changes require reconciliation"));
	}
	Manifest->SetStringField(TEXT("lease_id"), Get(Lease, TEXT("lease_id")));
	Manifest->SetStringField(TEXT("owner"), Owner);
	Manifest->SetNumberField(TEXT("generation"), Generation);
	const FString ReviewHash = HashText(Json(Manifest));
	Manifest->SetStringField(TEXT("review_hash"), ReviewHash);
	Manifest->SetBoolField(TEXT("ok"), true);
	if (Action == TEXT("preview_persist")) return Json(Manifest);
	if (Get(Request, TEXT("review_hash")) != ReviewHash) return SandboxError(TEXT("TargetRevisionMismatch"), TEXT("Approved review hash changed"));
	Lease->SetStringField(TEXT("state"), TEXT("persist_intent"));
	Lease->SetStringField(TEXT("review_hash"), ReviewHash);
	if (!Journal()) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Cannot persist write intent"));
	TArray<TSharedPtr<FJsonValue>> Outcomes;
	const TArray<TSharedPtr<FJsonValue>>* PreviousOutcomes = nullptr;
	if (Lease->TryGetArrayField(TEXT("persist_results"), PreviousOutcomes)) Outcomes = *PreviousOutcomes;
	bool Success = true;
	for (const auto& Value : Rows)
	{
		auto Row = Value->AsObject(); const FString Path = Get(Row, TEXT("path"));
		// Reconcile again immediately before each official Engine persist. This is not an atomic filesystem CAS.
		if (FileHash(Path, true) != Get(Baselines, *Path) || FileHash(Path, false) != Get(Sealed, *Path)) { Success = false; break; }
		TArray<FString> Selection = {Path}; FPersistArgs Args; Args.Files = Selection;
		const auto Result = Instance->PersistSandbox(Args);
		const FString Actual = FileHash(Path, true);
		const bool Applied = Result.PersistStatus == EPersistStatus::Success && Actual == Get(Sealed, *Path);
		auto Outcome = MakeShared<FJsonObject>(); Outcome->SetStringField(TEXT("path"), Path);
		Outcome->SetBoolField(TEXT("confirmed"), Applied); Outcome->SetStringField(TEXT("main_sha1"), Actual);
		Outcomes.RemoveAll([&](const auto& Prior) { return Get(Prior->AsObject(), TEXT("path")) == Path; });
		Outcomes.Add(MakeShared<FJsonValueObject>(Outcome));
		Lease->SetArrayField(TEXT("persist_results"), Outcomes);
		Lease->SetStringField(TEXT("state"), TEXT("partially_persisted"));
		if (Applied) Baselines->SetStringField(Path, Actual);
		if (!Journal() || !Applied) { Success = false; break; }
	}
	Lease->SetStringField(TEXT("state"), Success ? TEXT("persisted_requires_main_readback") : TEXT("partially_persisted_needs_reconciliation"));
	++Generation;
	if (!Journal()) return SandboxError(TEXT("NeedsReconciliation"), TEXT("Persist results journal failed"), TEXT("unknown"));
	auto Out = Read(GetSandboxStatus()); Out->SetBoolField(TEXT("ok"), Success);
	Out->SetStringField(TEXT("status"), Get(Lease, TEXT("state")));
	return Json(Out);
#endif
}
