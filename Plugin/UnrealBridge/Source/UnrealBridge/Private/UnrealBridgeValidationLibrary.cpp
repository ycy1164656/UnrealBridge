#include "UnrealBridgeValidationLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "FileHelpers.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Package.h"

#if UNREALBRIDGE_WITH_DATA_VALIDATION
#include "EditorValidatorSubsystem.h"
#endif

namespace BridgeValidation
{
	static FString Json(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
		return Out;
	}
	static TSharedRef<FJsonObject> Base()
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("schema"), TEXT("unrealbridge.validation.v1"));
		Out->SetStringField(TEXT("save_behavior"), TEXT("NeverSave"));
		return Out;
	}
	static FString Error(const TCHAR* Code, const FString& Message)
	{
		auto Out = Base();
		Out->SetBoolField(TEXT("ok"), false);
		Out->SetStringField(TEXT("error_code"), Code);
		Out->SetStringField(TEXT("error"), Message);
		return Json(Out);
	}
	static TArray<FString> Dirty()
	{
		TArray<UPackage*> Packages;
		FEditorFileUtils::GetDirtyContentPackages(Packages);
		FEditorFileUtils::GetDirtyWorldPackages(Packages);
		TArray<FString> Names;
		for (UPackage* Package : Packages) if (Package) Names.AddUnique(Package->GetName());
		Names.Sort();
		return Names;
	}
	static TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : Values) Out.Add(MakeShared<FJsonValueString>(Value));
		return Out;
	}
	static void AddDirty(const TSharedRef<FJsonObject>& Out, const TArray<FString>& Before)
	{
		const TArray<FString> After = Dirty();
		Out->SetArrayField(TEXT("dirty_before"), Strings(Before));
		Out->SetArrayField(TEXT("dirty_after"), Strings(After));
		Out->SetBoolField(TEXT("unexpected_dirty"), Before != After);
	}
	static FString Bounded(const FText& Text)
	{
		return Text.ToString().Left(4096);
	}
}

FString UUnrealBridgeValidationLibrary::ValidateAssetsJson(const FString& RequestJson)
{
#if !UNREALBRIDGE_WITH_DATA_VALIDATION
	return BridgeValidation::Error(TEXT("unsupported"), TEXT("DataValidation module unavailable for this Engine"));
#else
	TSharedPtr<FJsonObject> Request;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RequestJson), Request) || !Request.IsValid())
		return BridgeValidation::Error(TEXT("invalid_request"), TEXT("JSON object required"));
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	int32 MaxAssets = 0;
	if (!Request->TryGetArrayField(TEXT("asset_paths"), Paths) || !Request->TryGetNumberField(TEXT("max_assets"), MaxAssets)
		|| MaxAssets < 1 || MaxAssets > 256 || Paths->IsEmpty())
		return BridgeValidation::Error(TEXT("invalid_scope"), TEXT("Exact asset_paths and max_assets 1..256 required"));
	if (!GEditor) return BridgeValidation::Error(TEXT("editor_unavailable"), TEXT("Editor required"));
	UEditorValidatorSubsystem* Validator = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	if (!Validator) return BridgeValidation::Error(TEXT("unsupported"), TEXT("EditorValidatorSubsystem unavailable"));
	const TArray<FString> DirtyBefore = BridgeValidation::Dirty();
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Assets;
	TArray<FString> Missing;
	TSet<FString> Seen;
	bool bLimitReached = Paths->Num() > MaxAssets;
	for (const TSharedPtr<FJsonValue>& Item : *Paths)
	{
		FString Path;
		if (!Item.IsValid() || !Item->TryGetString(Path) || !Path.StartsWith(TEXT("/")) || !Path.Contains(TEXT(".")))
			return BridgeValidation::Error(TEXT("invalid_scope"), TEXT("Every asset path must be an exact object path"));
		if (Seen.Contains(Path)) continue;
		Seen.Add(Path);
		if (Assets.Num() >= MaxAssets) { bLimitReached = true; break; }
		FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(Path));
		if (Asset.IsValid()) Assets.Add(Asset); else Missing.Add(Path);
	}
	FValidateAssetsSettings Settings;
	Settings.ValidationUsecase = EDataValidationUsecase::Script;
	Settings.bSilent = true;
	Settings.bShowIfNoFailures = false;
	Settings.bCollectPerAssetDetails = true;
	Settings.bCaptureAssetLoadLogs = false;
	Settings.bCaptureLogsDuringValidation = false;
	Settings.bUnloadAssetsLoadedForValidation = true;
	Settings.MaxAssetsToValidate = MaxAssets;
	Settings.ShowMessageLogSeverity.Reset();
	FValidateAssetsResults Results;
	Validator->ValidateAssetsWithSettings(Assets, Settings, Results);
	auto Out = BridgeValidation::Base();
	Out->SetBoolField(TEXT("ok"), true);
	Out->SetStringField(TEXT("operation"), TEXT("assets"));
	Out->SetNumberField(TEXT("requested"), Paths->Num());
	Out->SetNumberField(TEXT("checked"), Results.NumChecked);
	Out->SetNumberField(TEXT("valid"), Results.NumValid);
	Out->SetNumberField(TEXT("invalid"), Results.NumInvalid);
	Out->SetNumberField(TEXT("warning"), Results.NumWarnings);
	Out->SetNumberField(TEXT("skipped"), Results.NumSkipped);
	Out->SetNumberField(TEXT("unable"), Results.NumUnableToValidate + Missing.Num());
	const bool bActualLimit = bLimitReached || (Results.bAssetLimitReached && Results.NumChecked < Assets.Num());
	Out->SetBoolField(TEXT("limit_reached"), bActualLimit);
	Out->SetArrayField(TEXT("missing"), BridgeValidation::Strings(Missing));
	TArray<TSharedPtr<FJsonValue>> PerAsset;
	for (const auto& Pair : Results.AssetsDetails)
	{
		const FValidateAssetsDetails& Details = Pair.Value;
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("object_path"), Pair.Key);
		Entry->SetStringField(TEXT("package"), Details.PackageName.ToString());
		Entry->SetStringField(TEXT("result"), Details.Result == EDataValidationResult::Valid ? TEXT("Valid")
			: Details.Result == EDataValidationResult::Invalid ? TEXT("Invalid") : TEXT("NotValidated"));
		TArray<FString> Errors, Warnings, Messages;
		for (const FText& Text : Details.ValidationErrors) Errors.Add(BridgeValidation::Bounded(Text));
		for (const FText& Text : Details.ValidationWarnings) Warnings.Add(BridgeValidation::Bounded(Text));
		for (const TSharedRef<FTokenizedMessage>& Message : Details.ValidationMessages)
			Messages.Add(BridgeValidation::Bounded(Message->ToText()));
		Entry->SetArrayField(TEXT("errors"), BridgeValidation::Strings(Errors));
		Entry->SetArrayField(TEXT("warnings"), BridgeValidation::Strings(Warnings));
		Entry->SetArrayField(TEXT("messages"), BridgeValidation::Strings(Messages));
		PerAsset.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Out->SetArrayField(TEXT("per_asset"), PerAsset);
	BridgeValidation::AddDirty(Out, DirtyBefore);
	const bool bComplete = !bActualLimit && !Results.NumSkipped
		&& !Results.NumUnableToValidate && Missing.IsEmpty() && !Out->GetBoolField(TEXT("unexpected_dirty"));
	Out->SetStringField(TEXT("overall_result"), Results.NumInvalid ? TEXT("invalid")
		: bComplete && Results.NumWarnings == 0 ? TEXT("valid") : TEXT("incomplete_or_warning"));
	return BridgeValidation::Json(Out);
#endif
}

FString UUnrealBridgeValidationLibrary::MapCheckJson()
{
	if (!GEditor || GEditor->PlayWorld)
		return BridgeValidation::Error(TEXT("pie_active_or_editor_unavailable"), TEXT("Map Check requires editor world without PIE"));
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return BridgeValidation::Error(TEXT("world_unavailable"), TEXT("Current editor world unavailable"));
	const TArray<FString> DirtyBefore = BridgeValidation::Dirty();
	const FString MapBefore = World->GetOutermost()->GetName();
	const int32 SelectionBefore = GEditor->GetSelectedActors() ? GEditor->GetSelectedActors()->Num() : 0;
	const bool bRan = GEditor->Exec(World, TEXT("MAP CHECK DONTDISPLAYDIALOG"), *GLog);
	FMessageLogModule& Module = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
	const TSharedRef<IMessageLogListing> Listing = Module.GetLogListing(TEXT("MapCheck"));
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TSharedRef<FTokenizedMessage>& Message : Listing->GetFilteredMessages())
	{
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("message"), BridgeValidation::Bounded(Message->ToText()));
		Entry->SetStringField(TEXT("severity"), Message->GetSeverity() == EMessageSeverity::Error ? TEXT("error")
			: Message->GetSeverity() == EMessageSeverity::Warning ? TEXT("warning") : TEXT("info"));
		Messages.Add(MakeShared<FJsonValueObject>(Entry));
		if (Messages.Num() >= 512) break;
	}
	auto Out = BridgeValidation::Base();
	Out->SetBoolField(TEXT("ok"), bRan);
	Out->SetStringField(TEXT("operation"), TEXT("map_check"));
	Out->SetStringField(TEXT("map"), MapBefore);
	Out->SetArrayField(TEXT("messages"), Messages);
	Out->SetBoolField(TEXT("messages_limited"), Messages.Num() >= 512);
	Out->SetBoolField(TEXT("map_changed"), World->GetOutermost()->GetName() != MapBefore);
	Out->SetBoolField(TEXT("selection_changed"), SelectionBefore != (GEditor->GetSelectedActors() ? GEditor->GetSelectedActors()->Num() : 0));
	BridgeValidation::AddDirty(Out, DirtyBefore);
	if (Out->GetBoolField(TEXT("unexpected_dirty")) || Out->GetBoolField(TEXT("map_changed")) || Out->GetBoolField(TEXT("selection_changed")))
		Out->SetBoolField(TEXT("ok"), false);
	return BridgeValidation::Json(Out);
}
