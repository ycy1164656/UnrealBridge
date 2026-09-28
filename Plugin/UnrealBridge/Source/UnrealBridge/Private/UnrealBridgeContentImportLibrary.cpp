#include "UnrealBridgeContentImportLibrary.h"
#include "UnrealBridgeSourcePaths.h"
#include "UnrealBridgeSha256.h"
#include "UnrealBridgeSandboxLibrary.h"
#include "UnrealBridgeChangeSetLibrary.h"
#include "UnrealBridgeAssetLibrary.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorReimportHandler.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/TextureFactory.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialEditingLibrary.h"
#include "FbxImporter.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "JsonObjectConverter.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMeshSocket.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "EditorFramework/AssetImportData.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/CustomVersion.h"
#include "UObject/ObjectVersion.h"
#include "Misc/EngineVersion.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectHash.h"
#include "UObject/StrongObjectPtr.h"
#include "FileHelpers.h"

namespace
{
	TMap<FString, FString> CI_Prepared;
	TMap<FString, FString> CI_Receipts;
	FString CI_Json(const TSharedRef<FJsonObject>& Value)
	{
		FString Text;
		FJsonSerializer::Serialize(Value, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}
	TSharedPtr<FJsonObject> CI_Read(const FString& Text)
	{
		TSharedPtr<FJsonObject> Value;
		if (Text.Len() <= 128 * 1024) { FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value); }
		return Value;
	}
	FString CI_Error(const FString& Code, const FString& Error, const FString& SideEffects = TEXT("none"))
	{
		const auto Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("ok"), false); Out->SetStringField(TEXT("error_code"), Code);
		Out->SetStringField(TEXT("error"), Error); Out->SetStringField(TEXT("side_effect_state"), SideEffects);
		Out->SetBoolField(TEXT("save_permitted"), false);
		return CI_Json(Out);
	}
	FString CI_String(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		FString Value; if (Object) { Object->TryGetStringField(Key, Value); } return Value;
	}
	FString CI_SourceHash(const FString& Path)
	{
		const int64 Size = IFileManager::Get().FileSize(*Path);
		if (Size <= 0 || Size > 64 * 1024 * 1024) { return FString(); }
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) { return FString(); }
		uint8 Hash[32]; UnrealBridgeSha256::Compute(Bytes.GetData(), Bytes.Num(), Hash);
		return BytesToHex(Hash, 32).ToLower();
	}
	class FCI_LimitedWriter : public FMemoryWriter
	{
	public:
		explicit FCI_LimitedWriter(TArray<uint8>& Bytes) : FMemoryWriter(Bytes) {}
		virtual void Serialize(void* Data, int64 Size) override
		{
			if (IsError() || Size < 0 || Tell() + Size > 16 * 1024 * 1024) { SetError(); return; }
			FMemoryWriter::Serialize(Data, Size);
		}
	};
	FString CI_Revision(UObject* Asset)
	{
		if (!Asset) { return TEXT("absent"); }
		TArray<UObject*> Objects; GetObjectsWithOuter(Asset->GetOutermost(), Objects, EGetObjectsFlags::IncludeNestedObjects);
		if (Objects.Num() > 2048) { return FString(); }
		Objects.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
		TArray<uint8> Bytes; FCI_LimitedWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Ar(Writer, false);
		Ar.SetUEVer(GPackageFileUEVersion); Ar.SetEngineVer(FEngineVersion::Current()); Ar.SetCustomVersions(FCurrentCustomVersions::GetAll());
		for (UObject* Object : Objects)
		{
			if (Object->HasAnyFlags(RF_Transient) || !IsValid(Object)) { continue; }
			FString Name = Object->GetPathName(); Ar << Name; Object->Serialize(Ar);
			// UE 5.8 package metadata is no longer a serialized UObject. Include
			// each object's sorted metadata explicitly so saved user annotations
			// invalidate a previously approved reimport just like properties do.
			TMap<FName, FString>* Metadata = FMetaData::GetMapForObject(Object);
			TArray<FString> Keys;
			if (Metadata) { for (const auto& Pair : *Metadata) { Keys.Add(Pair.Key.ToString()); } }
			Keys.Sort(); int32 Count = Keys.Num(); Ar << Count;
			for (FString& Key : Keys) { FString Value = Metadata->FindChecked(FName(*Key)); Ar << Key; Ar << Value; }
			if (Writer.IsError()) { return FString(); }
		}
		uint8 Hash[32]; UnrealBridgeSha256::Compute(Bytes.GetData(), Bytes.Num(), Hash);
		return BytesToHex(Hash, 32).ToLower();
	}
	FString CI_ObjectHash(UObject* Object)
	{
		if (!Object) { return TEXT("absent"); }
		TArray<uint8> Bytes; FCI_LimitedWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Ar(Writer, false);
		Ar.SetUEVer(GPackageFileUEVersion); Ar.SetEngineVer(FEngineVersion::Current()); Ar.SetCustomVersions(FCurrentCustomVersions::GetAll());
		Object->Serialize(Ar); if (Writer.IsError()) { return TEXT("unobservable"); }
		uint8 Hash[32]; UnrealBridgeSha256::Compute(Bytes.GetData(), Bytes.Num(), Hash); return BytesToHex(Hash, 32).ToLower();
	}
	TArray<TSharedPtr<FJsonValue>> CI_Strings(const TSet<FString>& Values);
	FString CI_ProtectedState(UObject* Asset)
	{
		const auto State = MakeShared<FJsonObject>();
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
		{
			TSet<FString> Materials, Sockets;
			for (int32 Index = 0; Index < Mesh->GetStaticMaterials().Num(); ++Index)
			{
				const auto& Slot = Mesh->GetStaticMaterials()[Index];
				Materials.Add(FString::FromInt(Index) + TEXT("|") + Slot.MaterialSlotName.ToString() + TEXT("|") + (Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : FString()));
			}
			for (UStaticMeshSocket* Socket : Mesh->Sockets) { if (Socket) { Sockets.Add(CI_ObjectHash(Socket)); } }
			State->SetArrayField(TEXT("materials"), CI_Strings(Materials)); State->SetArrayField(TEXT("sockets"), CI_Strings(Sockets));
			State->SetStringField(TEXT("collision"), CI_ObjectHash(Mesh->GetBodySetup()));
			State->SetNumberField(TEXT("lods"), Mesh->GetNumSourceModels()); State->SetBoolField(TEXT("nanite"), Mesh->GetNaniteSettings().bEnabled);
		}
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset))
		{
			TSet<FString> Materials;
			for (int32 Index = 0; Index < Mesh->GetMaterials().Num(); ++Index)
			{
				const auto& Slot = Mesh->GetMaterials()[Index];
				Materials.Add(FString::FromInt(Index) + TEXT("|") + Slot.MaterialSlotName.ToString() + TEXT("|") + (Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : FString()));
			}
			State->SetArrayField(TEXT("materials"), CI_Strings(Materials));
			State->SetStringField(TEXT("skeleton"), Mesh->GetSkeleton() ? Mesh->GetSkeleton()->GetPathName() : FString());
			State->SetStringField(TEXT("physics"), CI_ObjectHash(Mesh->GetPhysicsAsset()));
			State->SetNumberField(TEXT("lods"), Mesh->GetLODNum());
		}
		return CI_Json(State);
	}
	TSet<FString> CI_Dirty()
	{
		TArray<UPackage*> Packages; FEditorFileUtils::GetDirtyContentPackages(Packages);
		TSet<FString> Names; for (UPackage* Package : Packages) { Names.Add(Package->GetName()); } return Names;
	}
	TArray<TSharedPtr<FJsonValue>> CI_Strings(const TSet<FString>& Values)
	{
		TArray<FString> Sorted = Values.Array(); Sorted.Sort();
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& Value : Sorted) { Result.Add(MakeShared<FJsonValueString>(Value)); } return Result;
	}
	FString CI_View()
	{
		const auto State = CI_Read(UUnrealBridgeSandboxLibrary::GetSandboxStatus());
		if (!State || !State->GetBoolField(TEXT("active")) || !State->GetBoolField(TEXT("owned"))) { return FString(); }
		const auto Lease = State->GetObjectField(TEXT("lease_record"));
		return CI_String(State, TEXT("editor_session_id")) + TEXT("|") + CI_String(State, TEXT("root")) + TEXT("|") + CI_String(Lease, TEXT("lease_id"));
	}
	bool CI_TargetOwned(const FString& Target)
	{
		const auto State = CI_Read(UUnrealBridgeSandboxLibrary::GetSandboxStatus());
		if (!State || !State->GetBoolField(TEXT("owned"))) { return false; }
		FString File;
		if (!FPackageName::TryConvertLongPackageNameToFilename(Target, File, FPackageName::GetAssetPackageExtension())) { return false; }
		File = FPaths::ConvertRelativePathToFull(File); FPaths::NormalizeFilename(File);
		return State->GetObjectField(TEXT("lease_record"))->GetObjectField(TEXT("baselines"))->HasField(File.ToLower());
	}
	FString CI_InspectFbx(const FString& Source, USkeleton* Skeleton, const FString& Profile, TSharedRef<FJsonObject> Out)
	{
		UnFbx::FFbxImporter* Importer = UnFbx::FFbxImporter::GetInstance();
		Importer->ReleaseScene();
		if (!Importer->ImportFromFile(Source, TEXT("fbx"))) { Importer->ReleaseScene(); return TEXT("Native FBX parser rejected source"); }
		FString Error; int32 MeshCount = 0, BoneCount = 0;
		TArray<TSharedPtr<FJsonValue>> Mapping;
		TFunction<void(FbxNode*)> Visit = [&](FbxNode* Node)
		{
			if (!Node) { return; }
			if (Node->GetMesh()) { ++MeshCount; }
			if (Node->GetSkeleton())
			{
				++BoneCount;
				const FString BoneName = UTF8_TO_TCHAR(Node->GetName());
				if (Skeleton)
				{
					const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
					const int32 BoneIndex = Ref.FindBoneIndex(FName(*BoneName));
					if (BoneIndex == INDEX_NONE) { Error = TEXT("Source bone missing from approved Skeleton: ") + BoneName; }
					else
					{
						FbxNode* Parent = Node->GetParent();
						while (Parent && !Parent->GetSkeleton()) { Parent = Parent->GetParent(); }
						const int32 ParentIndex = Ref.GetParentIndex(BoneIndex);
						const FString ExpectedParent = ParentIndex == INDEX_NONE ? FString() : Ref.GetBoneName(ParentIndex).ToString();
						const FString SourceParent = Parent ? UTF8_TO_TCHAR(Parent->GetName()) : FString();
						if (SourceParent != ExpectedParent) { Error = TEXT("Source bone hierarchy differs: ") + BoneName; }
						const auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("bone"), BoneName);
						Row->SetNumberField(TEXT("target_index"), BoneIndex); Row->SetStringField(TEXT("parent"), SourceParent);
						Mapping.Add(MakeShared<FJsonValueObject>(Row));
					}
				}
			}
			for (int32 Index = 0; Index < Node->GetChildCount(); ++Index) { Visit(Node->GetChild(Index)); }
		};
		Visit(Importer->Scene->GetRootNode());
		const int32 Takes = Importer->Scene->GetSrcObjectCount<FbxAnimStack>();
		Out->SetNumberField(TEXT("source_mesh_count"), MeshCount); Out->SetNumberField(TEXT("source_bone_count"), BoneCount);
		Out->SetNumberField(TEXT("source_animation_takes"), Takes);
		Out->SetArrayField(TEXT("bone_mapping"), Mapping);
		Out->SetArrayField(TEXT("sidecars"), {});
		Out->SetStringField(TEXT("sidecar_policy"), TEXT("No sidecars are imported; materials and textures require separate exact contracts"));
		Out->SetNumberField(TEXT("source_centimeters_per_unit"), Importer->Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor());
		if (Skeleton && BoneCount == 0) { Error = TEXT("No source skeleton; animation/skeletal preflight cannot prove compatibility"); }
		if (Profile == TEXT("animation") && Takes != 1) { Error = TEXT("Animation profile requires exactly one take/output"); }
		if (Profile != TEXT("animation") && MeshCount == 0) { Error = TEXT("No source mesh"); }
		Importer->ReleaseScene();
		return Error;
	}
	FString CI_ExpectedClass(const FString& Profile)
	{
		if (Profile == TEXT("pbr_material")) { return TEXT("MaterialInstanceConstant"); }
		if (Profile == TEXT("texture")) { return TEXT("Texture2D"); }
		if (Profile == TEXT("static_mesh")) { return TEXT("StaticMesh"); }
		if (Profile == TEXT("skeletal_mesh")) { return TEXT("SkeletalMesh"); }
		if (Profile == TEXT("animation")) { return TEXT("AnimSequence"); }
		return FString();
	}
}

FString UUnrealBridgeContentImportLibrary::PrepareImport(const FString& RequestJson)
{
	const auto Request = CI_Read(RequestJson);
	if (!IsInGameThread() || !Request || CI_String(Request, TEXT("schema")) != TEXT("unrealbridge.external_import.v1"))
	{ return CI_Error(TEXT("invalid_request"), TEXT("Expected bounded external_import.v1 JSON on GameThread")); }
	const TSet<FString> Allowed = { TEXT("schema"), TEXT("request_id"), TEXT("profile"), TEXT("source_file"), TEXT("source_sha256"), TEXT("target"), TEXT("mode"), TEXT("role"), TEXT("skeleton"), TEXT("scale"), TEXT("nanite"), TEXT("collision"), TEXT("sample_rate"), TEXT("frame_start"), TEXT("frame_end"), TEXT("root_motion"), TEXT("parent_material"), TEXT("textures"), TEXT("repair_of") };
	for (const auto& Pair : Request->Values) { const FString Key(Pair.Key); if (!Allowed.Contains(Key)) { return CI_Error(TEXT("unknown_field"), Key); } }
	const FString Profile = CI_String(Request, TEXT("profile")); const FString Target = CI_String(Request, TEXT("target"));
	const FString Mode = CI_String(Request, TEXT("mode"));
	const bool bRepairBinding = Profile == TEXT("pbr_material") && Mode == TEXT("repair_binding");
	if (CI_ExpectedClass(Profile).IsEmpty() || (Mode != TEXT("new") && Mode != TEXT("reimport") && !bRepairBinding) || CI_String(Request, TEXT("request_id")).IsEmpty())
	{ return CI_Error(TEXT("unsupported_profile"), TEXT("Explicit request_id, mode and texture/static_mesh/skeletal_mesh/animation profile required")); }
	for (const TCHAR* Key : {TEXT("nanite"), TEXT("collision"), TEXT("root_motion")})
	{ if (Request->HasField(Key) && !Request->HasTypedField<EJson::Boolean>(Key)) { return CI_Error(TEXT("invalid_option_type"), Key); } }
	for (const TCHAR* Key : {TEXT("scale"), TEXT("sample_rate"), TEXT("frame_start"), TEXT("frame_end")})
	{ if (Request->HasField(Key) && !Request->HasTypedField<EJson::Number>(Key)) { return CI_Error(TEXT("invalid_option_type"), Key); } }
	double Scale = 1; Request->TryGetNumberField(TEXT("scale"), Scale);
	if (!FMath::IsFinite(Scale) || Scale <= 0 || Scale > 1000) { return CI_Error(TEXT("invalid_scale"), TEXT("Scale must be in (0,1000]")); }
	double Rate = 30; Request->TryGetNumberField(TEXT("sample_rate"), Rate);
	if (Rate < 1 || Rate > 240 || Rate != FMath::FloorToDouble(Rate)) { return CI_Error(TEXT("invalid_sample_rate"), TEXT("Integer sample_rate in 1..240 required")); }
	if (Request->HasField(TEXT("frame_start")) != Request->HasField(TEXT("frame_end"))) { return CI_Error(TEXT("invalid_range"), TEXT("Both frame_start and frame_end required")); }
	if (Request->HasField(TEXT("frame_start")) && (Request->GetNumberField(TEXT("frame_start")) < 0 || Request->GetNumberField(TEXT("frame_end")) < Request->GetNumberField(TEXT("frame_start"))))
	{ return CI_Error(TEXT("invalid_range"), TEXT("Frame range must be nonnegative and ordered")); }
	if (!FPackageName::IsValidLongPackageName(Target) || Target.Contains(TEXT(".")) || !CI_TargetOwned(Target))
	{ return CI_Error(TEXT("scope_violation"), TEXT("Target must belong to the exact active owned sandbox package whitelist")); }
	FString Source, Error;
	const bool bMaterial = Profile == TEXT("pbr_material");
	if (!bMaterial && !UnrealBridgeAuthorizeSourceFile(CI_String(Request, TEXT("source_file")), Source, Error)) { return CI_Error(TEXT("source_path_rejected"), Error); }
	const FString SourceHash = bMaterial ? UnrealBridgeSha256::HexOfString(CI_Json(Request.ToSharedRef())) : CI_SourceHash(Source);
	if (SourceHash.IsEmpty() || (!bMaterial && SourceHash != CI_String(Request, TEXT("source_sha256"))))
	{ return CI_Error(TEXT("source_revision_mismatch"), TEXT("Source missing/changed or outside 64 MiB profile budget")); }
	UObject* Existing = LoadObject<UObject>(nullptr, *(Target + TEXT(".") + FPackageName::GetShortName(Target)));
	if ((Mode == TEXT("new") && Existing) || ((Mode == TEXT("reimport") || bRepairBinding) && !Existing) || (Existing && Existing->GetOutermost()->IsDirty() && !bRepairBinding))
	{ return CI_Error(TEXT("target_conflict"), TEXT("New imports require absent targets; reimport requires a clean existing asset")); }
	if (bRepairBinding)
	{
		if (!Request->HasTypedField<EJson::Array>(TEXT("textures")))
		{ return CI_Error(TEXT("untrusted_repair"), TEXT("Repair requires the original texture binding array")); }
		const FString* Previous = CI_Prepared.Find(CI_String(Request, TEXT("repair_of")));
		const auto Prior = Previous ? CI_Read(*Previous) : nullptr;
		const auto Old = Prior ? Prior->GetObjectField(TEXT("request")) : nullptr;
		if (!Old || CI_String(Old, TEXT("target")) != Target || CI_String(Old, TEXT("parent_material")) != CI_String(Request, TEXT("parent_material"))
			|| !CI_Receipts.Contains(CI_String(Old, TEXT("request_id"))) || !FJsonValue::CompareEqual(*Old->TryGetField(TEXT("textures")), *Request->TryGetField(TEXT("textures")))
			|| Existing->GetOutermost()->GetMetaData().GetValue(Existing, TEXT("UnrealBridge.Import.SaveBlocked")) != FString(TEXT("binding_pending")))
		{ return CI_Error(TEXT("untrusted_repair"), TEXT("Repair requires this session's interrupted exact material contract and blocked original target")); }
	}
	if (Existing && Existing->GetClass()->GetName() != CI_ExpectedClass(Profile)) { return CI_Error(TEXT("wrong_target_type"), Existing->GetClass()->GetPathName()); }
	const auto Contract = MakeShared<FJsonObject>();
	Contract->SetObjectField(TEXT("request"), Request); Contract->SetStringField(TEXT("source_resolved"), Source);
	Contract->SetStringField(TEXT("source_sha256"), SourceHash);
	Contract->SetStringField(TEXT("view"), CI_View()); Contract->SetStringField(TEXT("expected_class"), CI_ExpectedClass(Profile));
	const FString Revision = CI_Revision(Existing);
	if (Revision.IsEmpty()) { return CI_Error(TEXT("snapshot_budget"), TEXT("Target exceeds bounded snapshot budget")); }
	Contract->SetStringField(TEXT("target_revision"), Revision);
	Contract->SetStringField(TEXT("protected_state"), CI_ProtectedState(Existing));
	Contract->SetStringField(TEXT("additive_policy"), TEXT("none; existing additive settings are protected on reimport"));
	Contract->SetStringField(TEXT("write_policy"), Mode == TEXT("new") ? TEXT("create exact output only") : TEXT("source payload only; preserve identity/materials/collision/LOD/sockets/physics"));
	Contract->SetStringField(TEXT("backend"), bMaterial ? TEXT("UMaterialInstanceConstantFactoryNew") : Profile == TEXT("texture") ? TEXT("UTextureFactory") : TEXT("UFbxFactory"));
	Contract->SetStringField(TEXT("execution"), TEXT("synchronous_native_import_non_interruptible; durable Job deadline is not cancellation"));
	Contract->SetArrayField(TEXT("predicted_outputs"), { MakeShared<FJsonValueString>(Target) });
	Contract->SetArrayField(TEXT("dirty_baseline"), CI_Strings(CI_Dirty()));
	if (bMaterial)
	{
		if (Mode != TEXT("new") && Mode != TEXT("reimport") && !bRepairBinding) { return CI_Error(TEXT("unsupported_profile"), TEXT("Material binding requires new, reimport or scoped repair")); }
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *CI_String(Request, TEXT("parent_material")));
		if (Mode == TEXT("reimport"))
		{
			const UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Existing);
			if (!Instance || Instance->Parent != Parent)
			{ return CI_Error(TEXT("protected_parent_changed"), TEXT("Material reimport preserves the existing parent and unlisted parameters")); }
		}
		const TArray<TSharedPtr<FJsonValue>>* Textures = nullptr;
		if (!Parent || Parent->GetOutermost()->IsDirty() || !Request->TryGetArrayField(TEXT("textures"), Textures) || Textures->Num() < 1 || Textures->Num() > 8)
		{ return CI_Error(TEXT("invalid_material_binding"), TEXT("Clean parent and 1..8 explicit role/parameter/texture bindings required")); }
		TArray<FName> Names; UMaterialEditingLibrary::GetTextureParameterNames(Parent, Names);
		const auto Revisions = MakeShared<FJsonObject>(); Revisions->SetStringField(Parent->GetPathName(), CI_Revision(Parent));
		TSet<FString> Used;
		for (const auto& Value : *Textures)
		{
			const auto Binding = Value->AsObject();
			const FString Role = CI_String(Binding, TEXT("role")), Parameter = CI_String(Binding, TEXT("parameter"));
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *CI_String(Binding, TEXT("texture")));
			if (!Binding || Binding->Values.Num() != 3 || !Texture || Texture->GetOutermost()->IsDirty() || !Names.Contains(FName(*Parameter)) || Used.Contains(Parameter))
			{ return CI_Error(TEXT("invalid_material_binding"), TEXT("Unknown/duplicate parameter or missing/Dirty Texture2D")); }
			const bool bSrgb = Role == TEXT("base_color") || Role == TEXT("emissive");
			if (!TSet<FString>{TEXT("base_color"),TEXT("normal"),TEXT("masks"),TEXT("emissive"),TEXT("opacity")}.Contains(Role)
				|| Texture->SRGB != bSrgb || (Role == TEXT("normal") && Texture->CompressionSettings != TC_Normalmap)
				|| ((Role == TEXT("masks") || Role == TEXT("opacity")) && Texture->CompressionSettings != TC_Masks))
			{ return CI_Error(TEXT("texture_role_mismatch"), Parameter); }
			Used.Add(Parameter); const FString RevisionHash = CI_Revision(Texture);
			if (RevisionHash.IsEmpty()) { return CI_Error(TEXT("snapshot_budget"), Texture->GetPathName()); }
			Revisions->SetStringField(Texture->GetPathName(), RevisionHash);
		}
		if (CI_Revision(Parent).IsEmpty()) { return CI_Error(TEXT("snapshot_budget"), Parent->GetPathName()); }
		Contract->SetObjectField(TEXT("binding_revisions"), Revisions);
	}
	else if (Profile == TEXT("texture"))
	{
		const FString Role = CI_String(Request, TEXT("role"));
		if (!FPaths::GetExtension(Source).Equals(TEXT("png"), ESearchCase::IgnoreCase) || !TSet<FString>{TEXT("ui"),TEXT("base_color"),TEXT("normal"),TEXT("masks"),TEXT("emissive"),TEXT("opacity")}.Contains(Role))
		{ return CI_Error(TEXT("invalid_texture_profile"), TEXT("PNG source and explicit ui/base_color/normal/masks/emissive/opacity role required")); }
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Source));
		uint8 Signature[8]{}; const uint8 PngSignature[8] = {137,80,78,71,13,10,26,10};
		if (Reader) { Reader->Serialize(Signature, 8); }
		if (!Reader || Reader->IsError() || FMemory::Memcmp(Signature, PngSignature, 8) != 0)
		{ return CI_Error(TEXT("invalid_png"), TEXT("Extension and file signature do not match")); }
	}
	else
	{
		if (!FPaths::GetExtension(Source).Equals(TEXT("fbx"), ESearchCase::IgnoreCase)) { return CI_Error(TEXT("unsupported_format"), TEXT("This profile requires FBX")); }
		USkeleton* Skeleton = nullptr;
		if (Profile != TEXT("static_mesh"))
		{
			Skeleton = LoadObject<USkeleton>(nullptr, *CI_String(Request, TEXT("skeleton")));
			if (!Skeleton || Skeleton->GetOutermost()->IsDirty()) { return CI_Error(TEXT("skeleton_conflict"), TEXT("Explicit clean existing Skeleton required")); }
			const FString SkeletonRevision = CI_Revision(Skeleton);
			if (SkeletonRevision.IsEmpty()) { return CI_Error(TEXT("snapshot_budget"), TEXT("Skeleton exceeds bounded snapshot")); }
			Contract->SetStringField(TEXT("skeleton_revision"), SkeletonRevision);
		}
		Error = CI_InspectFbx(Source, Skeleton, Profile, Contract);
		if (!Error.IsEmpty()) { return CI_Error(TEXT("source_incompatible"), Error); }
	}
	const FString Hash = UnrealBridgeSha256::HexOfString(CI_Json(Contract));
	Contract->SetStringField(TEXT("contract_hash"), Hash);
	CI_Prepared.Add(Hash, CI_Json(Contract));
	const auto Out = MakeShared<FJsonObject>(); Out->SetBoolField(TEXT("ok"), true); Out->SetObjectField(TEXT("contract"), Contract);
	Out->SetStringField(TEXT("side_effect_state"), TEXT("none")); Out->SetStringField(TEXT("status"), TEXT("prepared_not_imported"));
	return CI_Json(Out);
}

FString UUnrealBridgeContentImportLibrary::ExecuteImport(const FString& ContractJson)
{
	const auto Contract = CI_Read(ContractJson);
	const FString Hash = CI_String(Contract, TEXT("contract_hash"));
	const FString* Prepared = CI_Prepared.Find(Hash);
	if (!Contract || !Prepared || !FJsonValue::CompareEqual(FJsonValueObject(CI_Read(*Prepared)), FJsonValueObject(Contract))) { return CI_Error(TEXT("untrusted_contract"), TEXT("Use an unchanged contract prepared in this Editor session")); }
	const auto Request = Contract->GetObjectField(TEXT("request"));
	const FString Target = CI_String(Request, TEXT("target")), Profile = CI_String(Request, TEXT("profile"));
	const FString Identity = CI_String(Request, TEXT("request_id"));
	const bool bRepairBinding = Profile == TEXT("pbr_material") && CI_String(Request, TEXT("mode")) == TEXT("repair_binding");
	if (const FString* Previous = CI_Receipts.Find(Identity))
	{
		const auto Receipt = CI_Read(*Previous);
		if (!Receipt || CI_String(Receipt, TEXT("contract_hash")) != Hash) { return CI_Error(TEXT("idempotency_conflict"), TEXT("Request identity has another contract or pending intent")); }
		const auto Current = CI_Read(ReadbackImport(ContractJson));
		if (!Current || !Current->GetBoolField(TEXT("ok")) || CI_String(Current, TEXT("revision")) != CI_String(Receipt, TEXT("revision")))
		{ return CI_Error(TEXT("needs_reconciliation"), TEXT("Previously imported target changed; do not replay")); }
		Receipt->SetBoolField(TEXT("idempotent_noop"), true); return CI_Json(Receipt.ToSharedRef());
	}
	if (CI_View() != CI_String(Contract, TEXT("view")) || !CI_TargetOwned(Target)) { return CI_Error(TEXT("view_changed"), TEXT("Sandbox/session/lease changed")); }
	FString Source, Error;
	const bool bMaterial = Profile == TEXT("pbr_material");
	if (bMaterial)
	{
		for (const auto& Pair : Contract->GetObjectField(TEXT("binding_revisions"))->Values)
		{
			UObject* Dependency = LoadObject<UObject>(nullptr, *FString(Pair.Key));
			if (!Dependency || Dependency->GetOutermost()->IsDirty() || CI_Revision(Dependency) != Pair.Value->AsString())
			{ return CI_Error(TEXT("binding_changed"), TEXT("Parent/texture changed after plan")); }
		}
	}
	else if (!UnrealBridgeAuthorizeSourceFile(CI_String(Request, TEXT("source_file")), Source, Error) || Source != CI_String(Contract, TEXT("source_resolved")) || CI_SourceHash(Source) != CI_String(Request, TEXT("source_sha256")))
	{ return CI_Error(TEXT("source_changed"), Error.IsEmpty() ? TEXT("Source path or bytes changed") : Error); }
	UObject* Existing = LoadObject<UObject>(nullptr, *(Target + TEXT(".") + FPackageName::GetShortName(Target)));
	if (CI_Revision(Existing) != CI_String(Contract, TEXT("target_revision")) || (Existing && Existing->GetOutermost()->IsDirty() && !bRepairBinding))
	{ return CI_Error(TEXT("target_revision_mismatch"), TEXT("Target was edited after the plan; prepare a new contract")); }
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *CI_String(Request, TEXT("skeleton")));
	if (Skeleton && (CI_Revision(Skeleton) != CI_String(Contract, TEXT("skeleton_revision")) || Skeleton->GetOutermost()->IsDirty()))
	{ return CI_Error(TEXT("skeleton_changed"), TEXT("Approved Skeleton changed")); }
	if ((Profile == TEXT("skeletal_mesh") || Profile == TEXT("animation")) && !Skeleton)
	{ return CI_Error(TEXT("skeleton_changed"), TEXT("Approved Skeleton is no longer loadable")); }
	const FString ChangeSet = bRepairBinding ? UUnrealBridgeChangeSetLibrary::BeginChangeSet(TEXT("Resume owned material binding"), { Target }) : UUnrealBridgeChangeSetLibrary::BeginGuardedChangeSet(TEXT("Typed external import"), { Target });
	if (ChangeSet.IsEmpty()) { return CI_Error(TEXT("changeset_conflict"), TEXT("Cannot acquire clean exact guarded target")); }
	CI_Receipts.Add(Identity, TEXT("intent_pending_reconciliation"));
	const TSet<FString> DirtyBefore = CI_Dirty();
	TArray<UObject*> Imported;
	if (bMaterial)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *CI_String(Request, TEXT("parent_material")));
		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>(); Factory->InitialParent = Parent;
		UMaterialInstanceConstant* Instance = (bRepairBinding || CI_String(Request, TEXT("mode")) == TEXT("reimport")) ? Cast<UMaterialInstanceConstant>(Existing) : Cast<UMaterialInstanceConstant>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(FPackageName::GetShortName(Target), FPackageName::GetLongPackagePath(Target), UMaterialInstanceConstant::StaticClass(), Factory));
		if (!Instance) { return CI_Error(TEXT("material_creation_failed"), TEXT("Native factory failed"), TEXT("needs_reconciliation")); }
		Instance->GetOutermost()->GetMetaData().SetValue(Instance, TEXT("UnrealBridge.Import.SaveBlocked"), TEXT("binding_pending"));
		Instance->Modify();
		Imported.Add(Instance);
		for (const auto& Value : Request->GetArrayField(TEXT("textures")))
		{
			const auto Binding = Value->AsObject();
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *CI_String(Binding, TEXT("texture")));
			const FName Parameter(*CI_String(Binding, TEXT("parameter")));
			// UE 5.8 returns false even after a successful setter. The observed
			// parameter is authoritative; never infer success from the bool alone.
			UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, Parameter, Texture);
			if (UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(Instance, Parameter) != Texture)
			{ return CI_Error(TEXT("material_binding_failed"), Parameter.ToString(), TEXT("needs_reconciliation")); }
		}
		for (const auto& Pair : Contract->GetObjectField(TEXT("binding_revisions"))->Values)
		{ if (CI_Revision(LoadObject<UObject>(nullptr, *FString(Pair.Key))) != Pair.Value->AsString()) { return CI_Error(TEXT("shared_dependency_changed"), TEXT("Parent/texture modified unexpectedly"), TEXT("needs_reconciliation")); } }
	}
	else if (CI_String(Request, TEXT("mode")) == TEXT("reimport"))
	{
		if (!FReimportManager::Instance()->Reimport(Existing, false, false, Source, nullptr, INDEX_NONE, true, true, false))
		{ return CI_Error(TEXT("reimport_failed"), TEXT("Native reimport did not complete; do not replay"), TEXT("needs_reconciliation")); }
		Imported.Add(Existing);
	}
	else
	{
		TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
		Task->Filename = Source; Task->DestinationPath = FPackageName::GetLongPackagePath(Target); Task->DestinationName = FPackageName::GetShortName(Target);
		Task->bAutomated = true; Task->bSave = false; Task->bAsync = false; Task->bReplaceExisting = false; Task->bReplaceExistingSettings = false;
		if (Profile == TEXT("texture")) { Task->Factory = NewObject<UTextureFactory>(); }
		else
		{
			UFbxImportUI* Options = NewObject<UFbxImportUI>();
			Options->bAutomatedImportShouldDetectType = false;
			Options->MeshTypeToImport = Profile == TEXT("animation") ? FBXIT_Animation : Profile == TEXT("skeletal_mesh") ? FBXIT_SkeletalMesh : FBXIT_StaticMesh;
			Options->OriginalImportType = Options->MeshTypeToImport;
			Options->bImportAsSkeletal = Profile == TEXT("skeletal_mesh"); Options->bImportMesh = Profile != TEXT("animation");
			Options->bImportAnimations = Profile == TEXT("animation"); Options->Skeleton = Skeleton;
			Options->bCreatePhysicsAsset = false; Options->bImportMaterials = false; Options->bImportTextures = false; Options->bOverrideFullName = true;
			Options->OverrideAnimationName = Task->DestinationName;
			double Scale = 1; Request->TryGetNumberField(TEXT("scale"), Scale);
			if (!FMath::IsFinite(Scale) || Scale <= 0 || Scale > 1000) { return CI_Error(TEXT("invalid_scale"), TEXT("Scale must be in (0,1000]")); }
			Options->StaticMeshImportData->ImportUniformScale = Scale;
			Options->StaticMeshImportData->bCombineMeshes = true;
			Options->StaticMeshImportData->bAutoGenerateCollision = Request->HasField(TEXT("collision")) && Request->GetBoolField(TEXT("collision"));
			Options->StaticMeshImportData->bBuildNanite = Request->HasField(TEXT("nanite")) && Request->GetBoolField(TEXT("nanite"));
			Options->SkeletalMeshImportData->ImportUniformScale = Scale;
			Options->SkeletalMeshImportData->bUpdateSkeletonReferencePose = false; Options->SkeletalMeshImportData->bUseT0AsRefPose = false;
			Options->AnimSequenceImportData->bUseDefaultSampleRate = false;
			Options->AnimSequenceImportData->CustomSampleRate = Request->HasField(TEXT("sample_rate")) ? Request->GetIntegerField(TEXT("sample_rate")) : 30;
			Options->AnimSequenceImportData->bImportBoneTracks = true;
			if (Request->HasField(TEXT("frame_start")) && Request->HasField(TEXT("frame_end")))
			{
				Options->AnimSequenceImportData->AnimationLength = FBXALIT_SetRange;
				Options->AnimSequenceImportData->FrameImportRange = FInt32Interval(Request->GetIntegerField(TEXT("frame_start")), Request->GetIntegerField(TEXT("frame_end")));
			}
			Task->Factory = NewObject<UFbxFactory>(); Task->Options = Options;
		}
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({Task.Get()});
		if (!Task->IsAsyncImportComplete()) { return CI_Error(TEXT("native_import_pending"), TEXT("Native backend still pending; reconcile, never resubmit"), TEXT("needs_reconciliation")); }
		Imported = Task->GetObjects();
	}
	TSet<FString> Actual;
	for (UObject* Asset : Imported) { if (Asset) { Actual.Add(Asset->GetOutermost()->GetName()); } }
	for (UObject* Asset : Imported)
	{
		if (Asset && Asset->GetOutermost()->GetFName() == FName(*Target))
		{ Asset->GetOutermost()->GetMetaData().SetValue(Asset, TEXT("UnrealBridge.Import.SaveBlocked"), TEXT("validation_pending")); }
	}
	TSet<FString> Unexpected = CI_Dirty().Difference(DirtyBefore);
	for (auto It = Unexpected.CreateIterator(); It; ++It) { if (It->Equals(Target, ESearchCase::IgnoreCase)) { It.RemoveCurrent(); } }
	const bool bExactOutput = Actual.Num() == 1 && Actual.Array()[0].Equals(Target, ESearchCase::IgnoreCase);
	if (!bExactOutput || Unexpected.Num() > 0 || (Skeleton && CI_Revision(Skeleton) != CI_String(Contract, TEXT("skeleton_revision"))))
	{ return CI_Error(TEXT("unexpected_outputs_or_dependency_changes"), FString::Join(Unexpected.Array(), TEXT(",")), TEXT("needs_reconciliation")); }
	UObject* Asset = Imported[0];
	if (!Asset || Asset->GetClass()->GetName() != CI_String(Contract, TEXT("expected_class")))
	{ return CI_Error(TEXT("wrong_output_type"), TEXT("Actual imported type does not match contract"), TEXT("needs_reconciliation")); }
	if (CI_String(Request, TEXT("mode")) == TEXT("reimport") && CI_ProtectedState(Asset) != CI_String(Contract, TEXT("protected_state")))
	{ return CI_Error(TEXT("protected_fields_changed"), TEXT("Reimport changed material/collision/socket/LOD/physics fields; saving is blocked"), TEXT("needs_reconciliation")); }
	if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		const FString Role = CI_String(Request, TEXT("role"));
		Texture->SRGB = Role == TEXT("ui") || Role == TEXT("base_color") || Role == TEXT("emissive");
		Texture->CompressionSettings = Role == TEXT("normal") ? TC_Normalmap : (Role == TEXT("masks") || Role == TEXT("opacity")) ? TC_Masks : Role == TEXT("ui") ? TC_EditorIcon : TC_Default;
		Texture->LODGroup = Role == TEXT("ui") ? TEXTUREGROUP_UI : Role == TEXT("normal") ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_World;
		if (Role == TEXT("ui")) { Texture->MipGenSettings = TMGS_NoMipmaps; Texture->NeverStream = true; }
		Texture->PostEditChange();
	}
	if (UAnimSequence* Animation = Cast<UAnimSequence>(Asset))
	{
		Animation->bEnableRootMotion = Request->HasField(TEXT("root_motion")) && Request->GetBoolField(TEXT("root_motion"));
		Animation->PostEditChange();
	}
	Asset->GetOutermost()->GetMetaData().SetValue(Asset, TEXT("UnrealBridge.Import.SaveBlocked"), TEXT(""));
	Asset->GetOutermost()->GetMetaData().SetValue(Asset, TEXT("UnrealBridge.Import.ContractHash"), *Hash);
	Asset->GetOutermost()->GetMetaData().SetValue(Asset, TEXT("UnrealBridge.Import.SourceHash"), *CI_String(Contract, TEXT("source_sha256")));
	const auto Out = CI_Read(ReadbackImport(ContractJson));
	Out->SetStringField(TEXT("contract_hash"), Hash); Out->SetArrayField(TEXT("actual_outputs"), CI_Strings(Actual));
	Out->SetBoolField(TEXT("saved"), false); Out->SetBoolField(TEXT("persisted"), false);
	Out->SetBoolField(TEXT("save_permitted"), Out->GetBoolField(TEXT("ok")));
	Out->SetStringField(TEXT("change_set_id"), ChangeSet);
	Out->SetStringField(TEXT("status"), TEXT("imported_readback_not_saved"));
	CI_Receipts.Add(Identity, CI_Json(Out.ToSharedRef()));
	return CI_Json(Out.ToSharedRef());
}

FString UUnrealBridgeContentImportLibrary::ReadbackImport(const FString& ContractJson)
{
	const auto Contract = CI_Read(ContractJson);
	if (!Contract || !Contract->HasTypedField<EJson::Object>(TEXT("request"))) { return CI_Error(TEXT("invalid_contract"), TEXT("No import contract")); }
	const auto Request = Contract->GetObjectField(TEXT("request")); const FString Target = CI_String(Request, TEXT("target"));
	UObject* Asset = LoadObject<UObject>(nullptr, *(Target + TEXT(".") + FPackageName::GetShortName(Target)));
	if (!Asset) { return CI_Error(TEXT("output_unobservable"), TEXT("Expected asset is not observable; do not reimport")); }
	const auto Out = MakeShared<FJsonObject>(); Out->SetBoolField(TEXT("ok"), Asset->GetClass()->GetName() == CI_String(Contract, TEXT("expected_class")));
	Out->SetStringField(TEXT("object_path"), Asset->GetPathName()); Out->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
	Out->SetStringField(TEXT("revision"), CI_Revision(Asset)); Out->SetBoolField(TEXT("dirty"), Asset->GetOutermost()->IsDirty());
	Out->SetStringField(TEXT("recorded_contract_hash"), Asset->GetOutermost()->GetMetaData().GetValue(Asset, TEXT("UnrealBridge.Import.ContractHash")));
	Out->SetStringField(TEXT("recorded_source_sha256"), Asset->GetOutermost()->GetMetaData().GetValue(Asset, TEXT("UnrealBridge.Import.SourceHash")));
	Out->SetBoolField(TEXT("contract_matches_output"), CI_String(Out, TEXT("recorded_contract_hash")) == CI_String(Contract, TEXT("contract_hash")) && CI_String(Out, TEXT("recorded_source_sha256")) == CI_String(Contract, TEXT("source_sha256")));
	Out->SetBoolField(TEXT("ok"), Out->GetBoolField(TEXT("ok")) && Out->GetBoolField(TEXT("contract_matches_output")));
	const FString SaveBlock = Asset->GetOutermost()->GetMetaData().GetValue(Asset, TEXT("UnrealBridge.Import.SaveBlocked"));
	Out->SetStringField(TEXT("save_blocked"), SaveBlock);
	if (!SaveBlock.IsEmpty()) { Out->SetBoolField(TEXT("ok"), false); }
	// Observe the current native file view. The host can match this digest to
	// the original persisted Sandbox receipt without trusting caller flags.
	FString Filename;
	if (FPackageName::TryConvertLongPackageNameToFilename(Asset->GetOutermost()->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		Filename = FPaths::ConvertRelativePathToFull(Filename); FPaths::NormalizeFilename(Filename);
		Out->SetStringField(TEXT("package_filename"), Filename);
		Out->SetStringField(TEXT("package_sha256"), CI_SourceHash(Filename));
		Out->SetBoolField(TEXT("saved_in_current_view"), !Asset->GetOutermost()->IsDirty() && !CI_String(Out, TEXT("package_sha256")).IsEmpty() && SaveBlock.IsEmpty());
	}
	const auto CurrentView = CI_Read(UUnrealBridgeSandboxLibrary::GetSandboxStatus());
	if (CurrentView)
	{
		Out->SetStringField(TEXT("project_identity"), CI_String(CurrentView, TEXT("project_identity")));
		Out->SetStringField(TEXT("editor_session_id"), CI_String(CurrentView, TEXT("editor_session_id")));
		Out->SetBoolField(TEXT("sandbox_active"), CurrentView->GetBoolField(TEXT("active")));
		Out->SetStringField(TEXT("current_view"), CI_View());
	}
	if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset))
	{
		Out->SetStringField(TEXT("parent_material"), Instance->Parent ? Instance->Parent->GetPathName() : FString());
		const auto Bindings = MakeShared<FJsonObject>();
		for (const auto& Value : Request->GetArrayField(TEXT("textures")))
		{
			const auto Binding = Value->AsObject(); const FString Parameter = CI_String(Binding, TEXT("parameter"));
			const UTexture* Texture = UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(Instance, FName(*Parameter));
			Bindings->SetStringField(Parameter, Texture ? Texture->GetPathName() : FString());
			if (!Texture || FSoftObjectPath(Texture) != FSoftObjectPath(CI_String(Binding, TEXT("texture")))) { Out->SetBoolField(TEXT("ok"), false); }
		}
		Out->SetObjectField(TEXT("texture_bindings"), Bindings);
	}
	if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		Out->SetNumberField(TEXT("width"), Texture->Source.GetSizeX()); Out->SetNumberField(TEXT("height"), Texture->Source.GetSizeY());
		Out->SetBoolField(TEXT("srgb"), Texture->SRGB); Out->SetNumberField(TEXT("compression"), Texture->CompressionSettings);
		Out->SetNumberField(TEXT("texture_group"), Texture->LODGroup); Out->SetNumberField(TEXT("source_format"), Texture->Source.GetFormat());
		Out->SetNumberField(TEXT("mips"), Texture->GetNumMips()); Out->SetBoolField(TEXT("never_stream"), Texture->NeverStream);
		Out->SetNumberField(TEXT("mip_policy"), Texture->MipGenSettings);
		if (Texture->Source.GetFormat() == TSF_BGRA8)
		{
			TArray64<uint8> Pixels; bool Alpha = false;
			if (Texture->Source.GetSizeX() * static_cast<int64>(Texture->Source.GetSizeY()) <= 16 * 1024 * 1024 && Texture->Source.GetMipData(Pixels, 0))
			{
				for (int64 Index = 3; Index < Pixels.Num(); Index += 4) { if (Pixels[Index] < 255) { Alpha = true; break; } }
				Out->SetNumberField(TEXT("channels"), 4); Out->SetBoolField(TEXT("has_nonopaque_alpha"), Alpha);
			}
		}
		if (Texture->AssetImportData) { Out->SetStringField(TEXT("import_data"), Texture->AssetImportData->GetSourceData().ToJson()); }
	}
	if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
	{
		const auto Info = UUnrealBridgeAssetLibrary::GetStaticMeshInfo(Target);
		Out->SetNumberField(TEXT("lods"), Info.NumLODs); Out->SetStringField(TEXT("bounds_extent"), Info.BoundsExtent.ToString());
		Out->SetStringField(TEXT("bounds_origin"), Info.BoundsOrigin.ToString()); Out->SetNumberField(TEXT("material_slots"), Mesh->GetStaticMaterials().Num());
		Out->SetBoolField(TEXT("nanite"), Mesh->GetNaniteSettings().bEnabled); Out->SetBoolField(TEXT("has_body_setup"), Mesh->GetBodySetup() != nullptr);
		Out->SetObjectField(TEXT("mesh_info"), FJsonObjectConverter::UStructToJsonObject(Info));
		if (Mesh->GetBodySetup()) { Out->SetNumberField(TEXT("collision_primitives"), Mesh->GetBodySetup()->AggGeom.GetElementCount()); }
		if (Mesh->GetAssetImportData()) { Out->SetStringField(TEXT("import_data"), Mesh->GetAssetImportData()->GetSourceData().ToJson()); }
	}
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset))
	{
		Out->SetStringField(TEXT("skeleton"), Mesh->GetSkeleton() ? Mesh->GetSkeleton()->GetPathName() : FString());
		Out->SetNumberField(TEXT("bones"), Mesh->GetRefSkeleton().GetNum()); Out->SetNumberField(TEXT("lods"), Mesh->GetLODNum());
		Out->SetStringField(TEXT("bounds_extent"), Mesh->GetBounds().BoxExtent.ToString()); Out->SetNumberField(TEXT("material_slots"), Mesh->GetMaterials().Num());
		Out->SetBoolField(TEXT("physics_asset"), Mesh->GetPhysicsAsset() != nullptr);
		Out->SetNumberField(TEXT("morph_targets"), Mesh->GetMorphTargets().Num());
		Out->SetObjectField(TEXT("mesh_info"), FJsonObjectConverter::UStructToJsonObject(UUnrealBridgeAssetLibrary::GetSkeletalMeshInfo(Target)));
		if (Mesh->GetAssetImportData()) { Out->SetStringField(TEXT("import_data"), Mesh->GetAssetImportData()->GetSourceData().ToJson()); }
	}
	if (UAnimSequence* Animation = Cast<UAnimSequence>(Asset))
	{
		Out->SetStringField(TEXT("skeleton"), Animation->GetSkeleton() ? Animation->GetSkeleton()->GetPathName() : FString());
		Out->SetNumberField(TEXT("duration_seconds"), Animation->GetPlayLength()); Out->SetNumberField(TEXT("sampled_keys"), Animation->GetNumberOfSampledKeys());
		Out->SetBoolField(TEXT("root_motion"), Animation->bEnableRootMotion);
		Out->SetNumberField(TEXT("additive_type"), Animation->AdditiveAnimType);
		if (const IAnimationDataModel* Model = Animation->GetDataModel())
		{
			TArray<FName> Names; Model->GetBoneTrackNames(Names);
			TSet<FString> Tracks; for (FName Name : Names) { Tracks.Add(Name.ToString()); }
			Out->SetArrayField(TEXT("tracks"), CI_Strings(Tracks));
			Out->SetNumberField(TEXT("sample_rate"), Model->GetFrameRate().AsDecimal());
			if (!Names.IsEmpty())
			{
				Out->SetStringField(TEXT("first_track_start"), Model->EvaluateBoneTrackTransform(Names[0], FFrameTime(0), EAnimInterpolationType::Linear).ToString());
				Out->SetStringField(TEXT("first_track_end"), Model->EvaluateBoneTrackTransform(Names[0], FFrameTime(Animation->GetNumberOfSampledKeys() - 1), EAnimInterpolationType::Linear).ToString());
			}
		}
		if (Animation->AssetImportData) { Out->SetStringField(TEXT("import_data"), Animation->AssetImportData->GetSourceData().ToJson()); }
	}
	Out->SetStringField(TEXT("side_effect_state"), TEXT("none"));
	return CI_Json(Out);
}
