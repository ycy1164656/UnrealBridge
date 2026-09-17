#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealBridgeUpgradeLibrary.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace BridgeUpgradeTests
{
	TSharedPtr<FJsonObject> Read(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
		return Out;
	}
	FString Write(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Out));
		return Out;
	}
	TSharedPtr<FJsonObject> Request()
	{
		const auto Context = Read(UUnrealBridgeUpgradeLibrary::GetAuthoringSnapshot(TEXT("[]")));
		if (!Context || !Context->GetBoolField(TEXT("ok"))) return nullptr;
		const auto Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("schema"), TEXT("unrealbridge.upgrade.v1"));
		Out->SetStringField(TEXT("request_id"), TEXT("native-contract-1"));
		Out->SetStringField(TEXT("operation_id"), TEXT("upgrade.validate"));
		for (const TCHAR* Field : {TEXT("project_identity"), TEXT("editor_session_id"), TEXT("engine_version")})
			Out->SetStringField(Field, Context->GetStringField(Field));
		Out->SetArrayField(TEXT("target_packages"), {});
		Out->SetObjectField(TEXT("expected_revisions"), MakeShared<FJsonObject>());
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealBridgeUpgradeNativeValidationTest,
	"UnrealBridge.Upgrade.NativeValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealBridgeUpgradeNativeValidationTest::RunTest(const FString& Parameters)
{
	using namespace BridgeUpgradeTests;
	const auto Initial = Request();
	if (!TestTrue(TEXT("Native context available"), Initial.IsValid())) return false;
	const auto Good = Read(UUnrealBridgeUpgradeLibrary::ValidateUpgradeRequest(Write(Initial.ToSharedRef())));
	if (!TestTrue(TEXT("Current empty-target validation succeeds"), Good && Good->GetBoolField(TEXT("ok")))) return false;
	TestEqual(TEXT("Validation makes no mutations"), Good->GetStringField(TEXT("side_effect_state")), FString(TEXT("none")));
	const TArray<TPair<FString, FString>> Cases = {
		{TEXT("editor_session_id"), TEXT("StaleHandle")},
		{TEXT("engine_version"), TEXT("EngineVersionMismatch")},
		{TEXT("world_handle"), TEXT("StaleHandle")},
		{TEXT("save_policy"), TEXT("ScopeViolation")},
		{TEXT("operation_id"), TEXT("UnsupportedCapability")},
		{TEXT("unrecognized_field"), TEXT("ValidationFailed")}
	};
	for (const auto& Item : Cases)
	{
		const auto Value = Request();
		if (!TestTrue(TEXT("Context remains available"), Value.IsValid())) return false;
		Value->SetStringField(Item.Key, TEXT("changed"));
		const auto Result = Read(UUnrealBridgeUpgradeLibrary::ValidateUpgradeRequest(Write(Value.ToSharedRef())));
		if (!TestTrue(TEXT("Native result is JSON"), Result.IsValid())) return false;
		TestFalse(*Item.Key, Result->GetBoolField(TEXT("ok")));
		TestEqual(*(Item.Key + TEXT(" error code")), Result->GetStringField(TEXT("error_code")), Item.Value);
	}
	for (const TCHAR* Targets : {TEXT("[\"/Engine/Asset\"]"), TEXT("[\"/Game/A.A\"]"), TEXT("[\"/Game/../A\"]"), TEXT("[\"/Game/A\",\"/game/a\"]")})
	{
		const auto Result = Read(UUnrealBridgeUpgradeLibrary::GetAuthoringSnapshot(Targets));
		if (!TestTrue(TEXT("Snapshot result is JSON"), Result.IsValid())) return false;
		TestFalse(TEXT("Unsafe targets rejected before loading"), Result->GetBoolField(TEXT("ok")));
		TestEqual(TEXT("Scope error"), Result->GetStringField(TEXT("error_code")), FString(TEXT("ScopeViolation")));
	}
	const auto BadBoolean = Request();
	if (!TestTrue(TEXT("Boolean test context available"), BadBoolean.IsValid())) return false;
	BadBoolean->SetStringField(TEXT("dry_run"), TEXT("false"));
	const auto BooleanResult = Read(UUnrealBridgeUpgradeLibrary::ValidateUpgradeRequest(Write(BadBoolean.ToSharedRef())));
	if (!TestTrue(TEXT("Boolean result is JSON"), BooleanResult.IsValid())) return false;
	TestEqual(TEXT("String Boolean is invalid"), BooleanResult->GetStringField(TEXT("error_code")), FString(TEXT("ValidationFailed")));
	return true;
}

#endif
