#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealBridgeAssetLibrary.h"
#include "UnrealBridgeJobManager.h"
#include "UnrealBridgeRegistryLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealBridgeJobIdempotencyTest,
	"UnrealBridge.Core.Job.Idempotency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealBridgeJobIdempotencyTest::RunTest(const FString& Parameters)
{
	FBridgeJobManager Manager;
	const FBridgeJobSubmitResult First = Manager.Submit(
		TEXT("print('same')"), TEXT("request-a"), 30.0, TEXT("stable-key"));
	const FBridgeJobSubmitResult Duplicate = Manager.Submit(
		TEXT("print('same')"), TEXT("request-b"), 30.0, TEXT("stable-key"));
	const FBridgeJobSubmitResult Conflict = Manager.Submit(
		TEXT("print('different')"), TEXT("request-c"), 30.0, TEXT("stable-key"));

	TestTrue(TEXT("First submission creates a Job"), First.Job.IsValid());
	TestTrue(TEXT("Identical idempotency key is deduplicated"), Duplicate.bDeduplicated);
	TestTrue(TEXT("Deduplicated submission resolves to the same Job"),
		First.Job.IsValid() && Duplicate.Job == First.Job);
	TestFalse(TEXT("Conflicting idempotency key is rejected"), Conflict.Job.IsValid());
	TestTrue(TEXT("Conflicting idempotency key reports an error"), !Conflict.Error.IsEmpty());
	Manager.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealBridgeJobLifecycleTest,
	"UnrealBridge.Core.Job.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealBridgeJobLifecycleTest::RunTest(const FString& Parameters)
{
	FBridgeJobManager Manager;
	const FBridgeJobSubmitResult Cancelled = Manager.Submit(
		TEXT("print('cancel')"), TEXT("cancel-request"), 30.0);
	FBridgeJobSnapshot Snapshot;
	FString Error;
	TestTrue(TEXT("Queued Job can be cancelled"),
		Cancelled.Job.IsValid() && Manager.Cancel(Cancelled.Job->JobId, Snapshot, Error));
	TestEqual(TEXT("Cancelled Job is terminal"), Snapshot.State, EBridgeJobState::Cancelled);
	TestFalse(TEXT("Cancelled Job is never claimed"), Manager.ClaimNextJob().IsValid());

	const FBridgeJobSubmitResult Submitted = Manager.Submit(
		TEXT("print('complete')"), TEXT("complete-request"), 30.0);
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> Running = Manager.ClaimNextJob();
	TestTrue(TEXT("Queued Job is claimed"), Running.IsValid());
	FBridgeJobResult Completion;
	Completion.bSuccess = true;
	Completion.Output = TEXT("done");
	Completion.SideEffectState = TEXT("none");
	Manager.Complete(Running, MoveTemp(Completion));
	TestTrue(TEXT("Completed Job remains queryable"),
		Submitted.Job.IsValid() && Manager.GetSnapshot(Submitted.Job->JobId, Snapshot));
	TestEqual(TEXT("Completed Job succeeded"), Snapshot.State, EBridgeJobState::Succeeded);
	TestEqual(TEXT("Completed Job output persisted"), Snapshot.Result.Output, FString(TEXT("done")));
	const FBridgeJobMetrics Metrics = Manager.GetMetrics();
	TestEqual(TEXT("Metrics count one success"), Metrics.TotalSucceeded, static_cast<int64>(1));
	TestEqual(TEXT("Metrics count one cancellation"), Metrics.TotalCancelled, static_cast<int64>(1));
	Manager.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealBridgeRegistryContractTest,
	"UnrealBridge.Core.Registry.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealBridgeRegistryContractTest::RunTest(const FString& Parameters)
{
	const FString RegistryJson = UUnrealBridgeRegistryLibrary::GetToolRegistryJson();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RegistryJson);
	TestTrue(TEXT("Registry is valid JSON"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid());
	TestTrue(TEXT("Registry exposes Niagara graph inspection"),
		RegistryJson.Contains(TEXT("get_niagara_system_structure")));
	TestTrue(TEXT("Registry exposes Sequencer property keys"),
		RegistryJson.Contains(TEXT("add_property_key")));
	TestTrue(TEXT("Registry exposes PCG graph validation"),
		RegistryJson.Contains(TEXT("validate_pcg_graph")));
	TestTrue(TEXT("Registry hash is populated"),
		!UUnrealBridgeRegistryLibrary::GetToolRegistryHash().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealBridgePollingJobLifecycleTest,
	"UnrealBridge.Core.Job.PollingLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealBridgePollingJobLifecycleTest::RunTest(const FString& Parameters)
{
	FBridgeJobManager Manager;
	const FBridgeJobSubmitResult Submitted = Manager.Submit(
		TEXT("print('start')"), TEXT("poll-request"), 30.0, TEXT("poll-key"),
		TEXT("print('{\"complete\":true}')"), 0.0, 30.0);
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> StartStep = Manager.ClaimNextJob();
	TestTrue(TEXT("Polling Job start step is claimed"), StartStep.IsValid());
	TestFalse(TEXT("First step is not a poll"), StartStep.IsValid() && StartStep->IsPollStep());
	FBridgeJobResult StartResult;
	StartResult.bSuccess = true;
	StartResult.Output = TEXT("started");
	Manager.CompletePollingStep(StartStep, MoveTemp(StartResult), false);

	FBridgeJobSnapshot Snapshot;
	TestTrue(TEXT("Polling Job remains queryable"),
		Submitted.Job.IsValid() && Manager.GetSnapshot(Submitted.Job->JobId, Snapshot));
	TestEqual(TEXT("Polling Job remains Running between steps"), Snapshot.State, EBridgeJobState::Running);
	TestTrue(TEXT("Snapshot marks polling mode"), Snapshot.bPolling);

	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> PollStep = Manager.ClaimNextJob();
	TestTrue(TEXT("Due poll step is claimed"), PollStep.IsValid());
	TestTrue(TEXT("Second step is a poll"), PollStep.IsValid() && PollStep->IsPollStep());
	FBridgeJobResult PollResult;
	PollResult.bSuccess = true;
	PollResult.Output = TEXT("complete");
	Manager.CompletePollingStep(PollStep, MoveTemp(PollResult), true);
	TestTrue(TEXT("Completed polling Job remains queryable"),
		Manager.GetSnapshot(Submitted.Job->JobId, Snapshot));
	TestEqual(TEXT("Polling Job succeeds after terminal poll"), Snapshot.State, EBridgeJobState::Succeeded);
	TestEqual(TEXT("Polling Job records both steps"), Snapshot.StepCount, 2);
	Manager.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealBridgeDependencyBoundsTest,
	"UnrealBridge.Asset.Dependency.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealBridgeDependencyBoundsTest::RunTest(const FString& Parameters)
{
	const FBridgeAssetDependencyTree Tree = UUnrealBridgeAssetLibrary::GetAssetDependencyTree(
		TEXT("/Game/__UnrealBridgeMissingAsset__"), false, 1, 1);
	TestEqual(TEXT("Dependency query preserves the requested root"),
		Tree.RootPackage, FString(TEXT("/Game/__UnrealBridgeMissingAsset__")));
	TestTrue(TEXT("Dependency query respects MaxNodes"), Tree.Nodes.Num() <= 1);
	TestTrue(TEXT("Missing package does not produce dependency edges"), Tree.Edges.IsEmpty());
	return true;
}

#endif
