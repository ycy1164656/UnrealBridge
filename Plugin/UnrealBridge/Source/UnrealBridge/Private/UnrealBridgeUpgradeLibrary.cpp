#include "UnrealBridgeUpgradeLibrary.h"

#include "UnrealBridgeEditorLibrary.h"
#include "UnrealBridgeWorldLibrary.h"
#include "Dom/JsonObject.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

namespace BridgeUpgradeImpl
{
	constexpr int32 MaxTargets = 16;
	constexpr int64 MaxSnapshotBytes = 8 * 1024 * 1024;
	const TCHAR* Schema = TEXT("unrealbridge.upgrade.v1");

	FString Json(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
		return Out;
	}
	TSharedPtr<FJsonObject> Read(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
		return Out;
	}
	FString UpgradeError(const TCHAR* Code, const FString& Message)
	{
		const auto Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("ok"), false);
		Out->SetStringField(TEXT("schema"), Schema);
		Out->SetStringField(TEXT("status"), TEXT("rejected"));
		Out->SetStringField(TEXT("error_code"), Code);
		Out->SetStringField(TEXT("error"), Message);
		Out->SetBoolField(TEXT("retryable"), false);
		Out->SetStringField(TEXT("side_effect_state"), TEXT("none"));
		return Json(Out);
	}
	bool StringField(const TSharedPtr<FJsonObject>& Object, FStringView Field, FString& Out)
	{
		return Object->HasTypedField<EJson::String>(Field) && Object->TryGetStringField(Field, Out);
	}
	bool PackageName(const FString& Value)
	{
		if (Value.Len() < 3 || Value.Len() > 512 || !FPackageName::IsValidLongPackageName(Value)
			|| Value.Contains(TEXT(".")) || Value.Contains(TEXT(":")) || Value.Contains(TEXT("\\"))) return false;
		for (const TCHAR* Root : {TEXT("/Engine/"), TEXT("/Script/"), TEXT("/Temp/"), TEXT("/Transient/"), TEXT("/Memory/")})
		{
			if (Value.StartsWith(Root, ESearchCase::IgnoreCase)) return false;
		}
		return true;
	}
	bool Identifier(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 128) return false;
		for (TCHAR C : Value)
		{
			if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')
				|| C == '_' || C == '.' || C == ':' || C == '-')) return false;
		}
		return true;
	}
	bool Targets(const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FString>& Out)
	{
		if (Values.Num() > MaxTargets) return false;
		TSet<FString> Seen;
		for (const auto& Item : Values)
		{
			FString Target;
			if (!Item.IsValid() || Item->Type != EJson::String || !Item->TryGetString(Target) || !PackageName(Target) || Seen.Contains(Target.ToLower())) return false;
			Seen.Add(Target.ToLower());
			Out.Add(Target);
		}
		return true;
	}
	FString ProjectIdentity(FString Path)
	{
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Path.Len() > 2048 || FPaths::IsRelative(Path) || !Path.EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase)) return FString();
		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("/"));
		for (const FString& Part : Segments) if (Part == TEXT(".") || Part == TEXT("..")) return FString();
		for (TCHAR C : Path) if (C < 32) return FString();
#if PLATFORM_WINDOWS
		Path.ToLowerInline();
#endif
		return Path;
	}
	class FLimitedWriter final : public FMemoryWriter
	{
	public:
		explicit FLimitedWriter(TArray<uint8>& Buffer) : FMemoryWriter(Buffer, true) {}
		virtual void Serialize(void* Data, int64 Num) override
		{
			if (IsError() || Num < 0 || Num > MaxSnapshotBytes || Tell() > MaxSnapshotBytes - Num)
			{
				SetError();
				return;
			}
			FMemoryWriter::Serialize(Data, Num);
		}
	};
	bool SupportedAsset(UObject* Asset)
	{
		static const TSet<FName> Classes = {
			TEXT("AnimMontage"), TEXT("AnimSequence"), TEXT("BehaviorTree"), TEXT("BlackboardData"),
			TEXT("SoundCue"), TEXT("SoundClass"), TEXT("SoundMix"), TEXT("SoundSubmixBase"),
			TEXT("SoundControlBus"), TEXT("SoundControlBusMix")};
		for (const UClass* Type = Asset ? Asset->GetClass() : nullptr; Type; Type = Type->GetSuperClass())
		{
			if (Classes.Contains(Type->GetFName())) return true;
		}
		return false;
	}
	TSharedPtr<FJsonObject> SnapshotTarget(const FString& Path, FString& Failure)
	{
		const bool bOnDisk = FPackageName::DoesPackageExist(Path);
		UPackage* Package = FindPackage(nullptr, *Path);
		const auto Out = MakeShared<FJsonObject>();
		if (!Package && !bOnDisk)
		{
			Out->SetStringField(TEXT("revision"), TEXT("absent"));
			Out->SetBoolField(TEXT("dirty"), false);
			Out->SetBoolField(TEXT("exists"), false);
			return Out;
		}
		if (!Package) Package = LoadPackage(nullptr, *Path, LOAD_None);
		UObject* Asset = Package ? FindObject<UObject>(Package, *FPackageName::GetShortName(Path)) : nullptr;
		if (!SupportedAsset(Asset))
		{
			Failure = TEXT("Target is not a supported animation/AI/audio authoring asset: ") + Path;
			return nullptr;
		}
		TArray<UObject*> Objects;
		GetObjectsWithOuter(Package, Objects, EGetObjectsFlags::IncludeNestedObjects);
		if (Objects.Num() > 2048)
		{
			Failure = TEXT("Target contains more than 2048 objects: ") + Path;
			return nullptr;
		}
		Objects.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
		TArray<uint8> Bytes;
		FLimitedWriter Writer(Bytes);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		for (UObject* Object : Objects)
		{
			if (!IsValid(Object) || Object->HasAnyFlags(RF_Transient)) continue;
			FString Name = Object->GetPathName();
			FString ClassName = Object->GetClass()->GetPathName();
			Archive << Name;
			Archive << ClassName;
			Object->Serialize(Archive);
			if (Writer.IsError())
			{
				Failure = TEXT("Target revision exceeds the 8 MiB snapshot budget: ") + Path;
				return nullptr;
			}
		}
		uint8 Digest[FSHA1::DigestSize];
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Digest);
		Out->SetStringField(TEXT("revision"), BytesToHex(Digest, FSHA1::DigestSize).ToLower());
		Out->SetStringField(TEXT("class_name"), Asset->GetClass()->GetPathName());
		Out->SetBoolField(TEXT("exists"), true);
		Out->SetBoolField(TEXT("dirty"), Package->IsDirty());
		Out->SetNumberField(TEXT("snapshot_bytes"), Bytes.Num());
		return Out;
	}
}

FString UUnrealBridgeUpgradeLibrary::GetAuthoringSnapshot(const FString& TargetPackagesJson)
{
	using namespace BridgeUpgradeImpl;
	if (!IsInGameThread()) return UpgradeError(TEXT("ValidationFailed"), TEXT("GameThread required"));
	if (FTCHARToUTF8(*TargetPackagesJson).Length() > 64 * 1024) return UpgradeError(TEXT("ValidationFailed"), TEXT("Target list exceeds request budget"));
	TArray<TSharedPtr<FJsonValue>> Values;
	TArray<FString> TargetsList;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TargetPackagesJson), Values) || !Targets(Values, TargetsList))
		return UpgradeError(TEXT("ScopeViolation"), TEXT("Expected at most 16 unique canonical package names"));
	const auto Context = Read(UUnrealBridgeWorldLibrary::GetWorldContexts(128));
	if (!Context || !Context->GetBoolField(TEXT("ok"))) return UpgradeError(TEXT("NeedsReconciliation"), TEXT("World context unavailable"));
	const auto Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), true);
	Out->SetStringField(TEXT("schema"), Schema);
	Out->SetStringField(TEXT("project_identity"), ProjectIdentity(FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())));
	Out->SetStringField(TEXT("editor_session_id"), Context->GetStringField(TEXT("editor_session_id")));
	Out->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	TArray<TSharedPtr<FJsonValue>> Handles;
	for (const auto& Item : Context->GetArrayField(TEXT("worlds")))
		Handles.Add(MakeShared<FJsonValueString>(Item->AsObject()->GetStringField(TEXT("world_handle"))));
	Out->SetArrayField(TEXT("world_handles"), Handles);
	const auto Observed = MakeShared<FJsonObject>();
	for (const FString& Target : TargetsList)
	{
		FString Failure;
		const auto Item = SnapshotTarget(Target, Failure);
		if (!Item) return UpgradeError(TEXT("UnsupportedCapability"), Failure);
		Observed->SetObjectField(Target, Item);
	}
	Out->SetObjectField(TEXT("targets"), Observed);
	Out->SetStringField(TEXT("save_policy"), TEXT("never"));
	return Json(Out);
}

FString UUnrealBridgeUpgradeLibrary::ValidateUpgradeRequest(const FString& RequestJson)
{
	using namespace BridgeUpgradeImpl;
	if (!IsInGameThread() || FTCHARToUTF8(*RequestJson).Length() > 64 * 1024)
		return UpgradeError(TEXT("ValidationFailed"), TEXT("GameThread and request budget required"));
	const auto Request = Read(RequestJson);
	if (!Request) return UpgradeError(TEXT("ValidationFailed"), TEXT("Request must be a JSON object"));
	static const TSet<FString> Fields = {TEXT("schema"), TEXT("request_id"), TEXT("operation_id"), TEXT("project_identity"),
		TEXT("editor_session_id"), TEXT("engine_version"), TEXT("target_packages"), TEXT("expected_revisions"),
		TEXT("dry_run"), TEXT("save_policy"), TEXT("world_handle"), TEXT("timeout_seconds")};
	for (const auto& Field : Request->Values)
	{
		const FString FieldName(*Field.Key);
		if (!Fields.Contains(FieldName)) return UpgradeError(TEXT("ValidationFailed"), TEXT("Unknown request field: ") + FieldName);
	}
	FString Version, Operation, RequestId, Session, Project, Engine, World, Save = TEXT("never");
	if (!StringField(Request, TEXT("schema"), Version) || Version != Schema
		|| !StringField(Request, TEXT("operation_id"), Operation) || Operation != TEXT("upgrade.validate"))
		return UpgradeError(TEXT("UnsupportedCapability"), TEXT("Schema or operation has no implemented executor"));
	if (!StringField(Request, TEXT("request_id"), RequestId) || !Identifier(RequestId)
		|| !StringField(Request, TEXT("editor_session_id"), Session) || !Identifier(Session))
		return UpgradeError(TEXT("ValidationFailed"), TEXT("Invalid request or editor identity"));
	if (!StringField(Request, TEXT("project_identity"), Project) || ProjectIdentity(Project).IsEmpty())
		return UpgradeError(TEXT("ScopeViolation"), TEXT("Explicit canonical .uproject identity required"));
	if (!StringField(Request, TEXT("engine_version"), Engine) || Engine.IsEmpty() || Engine.Len() > 128)
		return UpgradeError(TEXT("ValidationFailed"), TEXT("Explicit engine version required"));
	bool bDryRun = true;
	if (Request->HasField(TEXT("dry_run")) && (!Request->HasTypedField<EJson::Boolean>(TEXT("dry_run"))
		|| !Request->TryGetBoolField(TEXT("dry_run"), bDryRun)))
		return UpgradeError(TEXT("ValidationFailed"), TEXT("dry_run must be Boolean"));
	if (Request->HasField(TEXT("save_policy")) && (!StringField(Request, TEXT("save_policy"), Save) || Save != TEXT("never")))
		return UpgradeError(TEXT("ScopeViolation"), TEXT("Implicit save is not supported"));
	double Timeout = 30;
	if (Request->HasField(TEXT("timeout_seconds")) && (!Request->HasTypedField<EJson::Number>(TEXT("timeout_seconds")) || !Request->TryGetNumberField(TEXT("timeout_seconds"), Timeout) || !FMath::IsFinite(Timeout) || Timeout < 1 || Timeout > 120))
		return UpgradeError(TEXT("ValidationFailed"), TEXT("Timeout must be finite and within 1..120 seconds"));
	if (Request->HasField(TEXT("world_handle")) && (!StringField(Request, TEXT("world_handle"), World) || World.Len() > 256))
		return UpgradeError(TEXT("StaleHandle"), TEXT("Invalid world handle"));
	const TArray<TSharedPtr<FJsonValue>>* TargetValues = nullptr;
	TArray<FString> TargetNames;
	if (!Request->TryGetArrayField(TEXT("target_packages"), TargetValues) || !Targets(*TargetValues, TargetNames))
		return UpgradeError(TEXT("ScopeViolation"), TEXT("Invalid explicit target set"));
	const TSharedPtr<FJsonObject>* Revisions = nullptr;
	if (!Request->TryGetObjectField(TEXT("expected_revisions"), Revisions) || (*Revisions)->Values.Num() != TargetNames.Num())
		return UpgradeError(TEXT("TargetRevisionMismatch"), TEXT("Every target requires one expected revision"));
	for (const FString& Target : TargetNames)
	{
		FString Revision;
		if (!StringField(*Revisions, Target, Revision)) return UpgradeError(TEXT("TargetRevisionMismatch"), TEXT("Missing expected target revision"));
		if (Revision != TEXT("absent"))
		{
			if (Revision.Len() != 40) return UpgradeError(TEXT("TargetRevisionMismatch"), TEXT("Invalid expected revision format"));
			for (TCHAR C : Revision) if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f')))
				return UpgradeError(TEXT("TargetRevisionMismatch"), TEXT("Invalid expected revision format"));
		}
	}
	// Verify project/session before resolving or loading any asset target.
	const auto Base = Read(GetAuthoringSnapshot(TEXT("[]")));
	if (!Base || !Base->GetBoolField(TEXT("ok"))) return UpgradeError(TEXT("NeedsReconciliation"), TEXT("Native context unavailable"));
	if (ProjectIdentity(Project) != Base->GetStringField(TEXT("project_identity"))) return UpgradeError(TEXT("ScopeViolation"), TEXT("Different project"));
	if (Session != Base->GetStringField(TEXT("editor_session_id"))) return UpgradeError(TEXT("StaleHandle"), TEXT("Editor session changed"));
	if (Engine != Base->GetStringField(TEXT("engine_version"))) return UpgradeError(TEXT("EngineVersionMismatch"), TEXT("Engine version changed"));
	if (!World.IsEmpty())
	{
		bool bFound = false;
		for (const auto& Handle : Base->GetArrayField(TEXT("world_handles"))) bFound |= Handle->AsString() == World;
		if (!bFound) return UpgradeError(TEXT("StaleHandle"), TEXT("World has expired"));
	}
	FString TargetsJson;
	FJsonSerializer::Serialize(*TargetValues, TJsonWriterFactory<>::Create(&TargetsJson));
	const auto Snapshot = Read(GetAuthoringSnapshot(TargetsJson));
	if (!Snapshot || !Snapshot->GetBoolField(TEXT("ok"))) return Snapshot ? Json(Snapshot.ToSharedRef()) : UpgradeError(TEXT("NeedsReconciliation"), TEXT("Snapshot unavailable"));
	for (const FString& Target : TargetNames)
	{
		const auto Item = Snapshot->GetObjectField(TEXT("targets"))->GetObjectField(Target);
		if (Item->GetStringField(TEXT("revision")) != (*Revisions)->GetStringField(Target))
			return UpgradeError(TEXT("TargetRevisionMismatch"), TEXT("Target changed: ") + Target);
		if (!bDryRun && Item->GetBoolField(TEXT("dirty"))) return UpgradeError(TEXT("DirtyConflict"), TEXT("Target has preexisting unsaved edits: ") + Target);
	}
	Snapshot->SetStringField(TEXT("status"), TEXT("validated"));
	Snapshot->SetStringField(TEXT("request_id"), RequestId);
	Snapshot->SetStringField(TEXT("operation_id"), Operation);
	Snapshot->SetStringField(TEXT("error_code"), TEXT(""));
	Snapshot->SetBoolField(TEXT("retryable"), false);
	Snapshot->SetStringField(TEXT("side_effect_state"), TEXT("none"));
	const FTCHARToUTF8 InputBytes(*RequestJson);
	uint8 InputHash[FSHA1::DigestSize];
	FSHA1::HashBuffer(InputBytes.Get(), InputBytes.Length(), InputHash);
	Snapshot->SetStringField(TEXT("input_digest"), TEXT("sha1:") + BytesToHex(InputHash, FSHA1::DigestSize).ToLower());
	Snapshot->SetStringField(TEXT("cleanup_state"), TEXT("not_required"));
	Snapshot->SetArrayField(TEXT("target_packages"), *TargetValues);
	Snapshot->SetArrayField(TEXT("applied_operations"), {});
	Snapshot->SetArrayField(TEXT("dirty_delta"), {});
	Snapshot->SetArrayField(TEXT("artifacts"), {});
	const auto Validation = MakeShared<FJsonObject>();
	Validation->SetBoolField(TEXT("request_contract"), true);
	Validation->SetBoolField(TEXT("authoring_validated"), false);
	Snapshot->SetObjectField(TEXT("validation"), Validation);
	return Json(Snapshot.ToSharedRef());
}
