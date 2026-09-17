#include "UnrealBridgeUE58Library.h"

#if WITH_DEV_AUTOMATION_TESTS && UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "ToolsetRegistry/Toolset.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"

namespace BridgeCatalogTest
{
	class FProbe final : public UE::ToolsetRegistry::FToolset
	{
	public:
		FString Name = TEXT("UnrealBridgeCatalogProbe_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		FString GetToolsetName() const override { return Name; }
		FString GetToolsetVersion() const override { return TEXT("1"); }
		FString GetToolsetDescription() const override { return TEXT("Transient Automation catalog probe"); }
	protected:
		TFuture<TValueOrError<FString, FString>> ExecuteToolInternal(const FString&, const FString&) override
		{ return MakeFulfilledPromise<TValueOrError<FString, FString>>(MakeError(FString(TEXT("probe_has_no_execution")))).GetFuture(); }
		FString GetJsonSchemaInternal() const override
		{ return FString::Printf(TEXT("{\"name\":\"%s\",\"version\":\"1\",\"tools\":[]}"), *Name); }
	};

	TSharedPtr<FJsonObject> Snapshot(bool bInclude)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UUnrealBridgeUE58Library::GetOfficialToolsetCatalogSnapshotJson(bInclude)), Out);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeCatalogRevisionTest, "UnrealBridge.Catalog.RevisionAndLegacyContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBridgeCatalogRevisionTest::RunTest(const FString&)
{
	const auto Subsystem = UToolsetRegistrySubsystem::Get(TEXT("UnrealBridge catalog test"));
	if (!TestTrue(TEXT("Registry available"), Subsystem.HasValue())) return false;
	auto Before = BridgeCatalogTest::Snapshot(false);
	if (!TestTrue(TEXT("Snapshot available"), Before.IsValid() && Before->GetBoolField(TEXT("success")))) return false;
	TestFalse(TEXT("Metadata omits large catalog"), Before->HasField(TEXT("catalog")));
	auto Probe = MakeShared<BridgeCatalogTest::FProbe>();
	if (!TestTrue(TEXT("Register owned probe"), Subsystem.GetValue()->ToolsetRegistry.RegisterToolset(Probe))) return false;
	bool bRegistered = true;
	ON_SCOPE_EXIT { if (bRegistered) Subsystem.GetValue()->ToolsetRegistry.UnregisterToolset(Probe); };
	auto During = BridgeCatalogTest::Snapshot(true);
	TestNotEqual(TEXT("Register increments revision"), Before->GetStringField(TEXT("registry_revision")), During->GetStringField(TEXT("registry_revision")));
	TestEqual(TEXT("Session stays stable"), Before->GetStringField(TEXT("editor_session_id")), During->GetStringField(TEXT("editor_session_id")));
	bool bFound = false;
	for (const auto& Item : During->GetArrayField(TEXT("catalog")))
		if (Item->AsObject()->GetStringField(TEXT("name")) == Probe->Name) bFound = true;
	TestTrue(TEXT("Catalog contains owned probe"), bFound);
	TestTrue(TEXT("Unregister owned probe"), Subsystem.GetValue()->ToolsetRegistry.UnregisterToolset(Probe));
	bRegistered = false;
	auto After = BridgeCatalogTest::Snapshot(true);
	TestNotEqual(TEXT("Unregister increments revision"), During->GetStringField(TEXT("registry_revision")), After->GetStringField(TEXT("registry_revision")));
	for (const auto& Item : After->GetArrayField(TEXT("catalog")))
		TestNotEqual(TEXT("Probe absent after unregister"), Item->AsObject()->GetStringField(TEXT("name")), Probe->Name);
	TArray<TSharedPtr<FJsonValue>> Legacy;
	TestTrue(TEXT("Legacy catalog is still an array"), FJsonSerializer::Deserialize(
		TJsonReaderFactory<>::Create(UUnrealBridgeUE58Library::GetOfficialToolsetCatalogJson()), Legacy));
	TestEqual(TEXT("Legacy and snapshot counts agree"), Legacy.Num(), After->GetArrayField(TEXT("catalog")).Num());
	return true;
}
#endif
