#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealBridgeWorldLibrary.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

namespace BridgeWorldTests
{
	TSharedPtr<FJsonObject> Read(const FString& Value)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Value), Out);
		return Out;
	}
	bool Ok(const FString& Value)
	{
		const auto Out = Read(Value);
		return Out.IsValid() && Out->GetBoolField(TEXT("ok"));
	}
	FString WorldHandle(UWorld* World)
	{
		const auto Out = Read(UUnrealBridgeWorldLibrary::GetWorldContexts(128));
		if (!Out) return FString();
		for (const auto& Item : Out->GetArrayField(TEXT("worlds")))
		{
			const auto Object = Item->AsObject();
			if (Object->GetStringField(TEXT("world_path")) == World->GetPathName()) return Object->GetStringField(TEXT("world_handle"));
		}
		return FString();
	}
	UWorld* MakeWorld()
	{
		const FName Name(*FString::Printf(TEXT("UBIdentityTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false, Name, GetTransientPackage());
		if (World) GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
		return World;
	}
	void DisposeWorld(UWorld*& World)
	{
		if (!World) return;
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		World = nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealBridgeWorldIdentityTest, "UnrealBridge.World.IdentityAndScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealBridgeWorldIdentityTest::RunTest(const FString& Parameters)
{
	using namespace BridgeWorldTests;
	AddInfo(TEXT("UB_WORLD_IDENTITY_V2: long selectors, scope poisoning, reinstance and lifecycle checks"));
	if (!TestNotNull(TEXT("Engine exists"), GEngine)) return false;
	UWorld* First = MakeWorld();
	UWorld* Second = MakeWorld();
	ON_SCOPE_EXIT
	{
		BridgeWorldContext::EndExecution();
		DisposeWorld(First);
		DisposeWorld(Second);
	};
	if (!TestNotNull(TEXT("First test world"), First) || !TestNotNull(TEXT("Second test world"), Second)) return false;
	const FString FirstHandle = WorldHandle(First);
	const FString SecondHandle = WorldHandle(Second);
	TestFalse(TEXT("World handle is populated"), FirstHandle.IsEmpty());
	TestNotEqual(TEXT("Distinct worlds have distinct handles"), FirstHandle, SecondHandle);
	TestFalse(TEXT("Oversized name safely fails without constructing FName"),
		Ok(UUnrealBridgeWorldLibrary::ResolveActorReference(FirstHandle, FString::ChrN(2048, TEXT('x')))));
	TestFalse(TEXT("Oversized path safely fails without constructing FName"),
		Ok(UUnrealBridgeWorldLibrary::ResolveActorReference(FirstHandle, TEXT("/Transient:") + FString::ChrN(2048, TEXT('x')))));
	FActorSpawnParameters Spawn;
	Spawn.Name = TEXT("UBIdentityProbe");
	Spawn.ObjectFlags = RF_Transient;
	AActor* FirstActor = First->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	AActor* SecondActor = Second->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("First actor"), FirstActor) || !TestNotNull(TEXT("Second actor"), SecondActor)) return false;
	const auto FirstRef = Read(UUnrealBridgeWorldLibrary::ResolveActorReference(FirstHandle, TEXT("UBIdentityProbe")));
	const auto SecondRef = Read(UUnrealBridgeWorldLibrary::ResolveActorReference(SecondHandle, TEXT("UBIdentityProbe")));
	if (!FirstRef || !SecondRef || !FirstRef->GetBoolField(TEXT("ok")) || !SecondRef->GetBoolField(TEXT("ok")))
	{
		AddError(TEXT("Could not resolve the two owned test actors"));
		return false;
	}
	const FString FirstActorHandle = FirstRef->GetStringField(TEXT("actor_handle"));
	const FString SecondActorHandle = SecondRef->GetStringField(TEXT("actor_handle"));
	BridgeWorldContext::BeginExecution();
	const auto Entered = Read(UUnrealBridgeWorldLibrary::BeginWorldScope(FirstHandle));
	if (!Entered || !Entered->GetBoolField(TEXT("ok"))) return false;
	TestEqual(TEXT("Explicit scope selects first world"), BridgeWorldContext::GetScopedWorld(), First);
	TestTrue(TEXT("Same-world actor handle is valid"), Ok(UUnrealBridgeWorldLibrary::ValidateActorReference(FirstActorHandle)));
	TestFalse(TEXT("Cross-world actor handle is rejected"), Ok(UUnrealBridgeWorldLibrary::ValidateActorReference(SecondActorHandle)));
	TestTrue(TEXT("Valid scope closes"), Ok(UUnrealBridgeWorldLibrary::EndWorldScope(Entered->GetStringField(TEXT("scope_token")))));
	TestFalse(TEXT("Closed scope is absent"), BridgeWorldContext::HasScope());
	UUnrealBridgeWorldLibrary::BeginWorldScope(FirstHandle);
	BridgeWorldContext::EndExecution();
	TestFalse(TEXT("Job boundary discards an unclosed scope"), BridgeWorldContext::HasScope());
	BridgeWorldContext::BeginExecution();
	TestFalse(TEXT("Old session handle is rejected"), Ok(UUnrealBridgeWorldLibrary::BeginWorldScope(TEXT("ubr:world:old-session:missing"))));
	TestTrue(TEXT("Invalid selection prevents implicit fallback"), BridgeWorldContext::HasScope());
	TestNull(TEXT("Invalid selection has no world"), BridgeWorldContext::GetScopedWorld());
	BridgeWorldContext::EndExecution();
	BridgeWorldContext::BeginExecution();
	const auto PoisonedScope = Read(UUnrealBridgeWorldLibrary::BeginWorldScope(FirstHandle));
	if (!PoisonedScope || !PoisonedScope->GetBoolField(TEXT("ok"))) return false;
	TestFalse(TEXT("Wrong scope token rejected"), Ok(UUnrealBridgeWorldLibrary::EndWorldScope(TEXT("wrong-token"))));
	TestNull(TEXT("Bad nesting blocks selection"), BridgeWorldContext::GetScopedWorld());
	TestFalse(TEXT("Closing a poisoned scope does not report success"),
		Ok(UUnrealBridgeWorldLibrary::EndWorldScope(PoisonedScope->GetStringField(TEXT("scope_token")))));
	BridgeWorldContext::EndExecution();

	// Exercise the actual engine reinstance notification using only owned,
	// transient actors. Never compile or edit an existing Blueprint asset.
	Spawn.Name = TEXT("UBReplacementProbe");
	AActor* Replacement = First->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Replacement actor"), Replacement)) return false;
	TMap<UObject*, UObject*> Replacements;
	Replacements.Add(FirstActor, Replacement);
	FCoreUObjectDelegates::OnObjectsReinstanced.Broadcast(Replacements);
	TestFalse(TEXT("Reinstanced actor identity is invalidated"), Ok(UUnrealBridgeWorldLibrary::ValidateActorReference(FirstActorHandle)));
	const auto ReplacementRef = Read(UUnrealBridgeWorldLibrary::ResolveActorReference(FirstHandle, Replacement->GetPathName()));
	if (!ReplacementRef || !ReplacementRef->GetBoolField(TEXT("ok"))) return false;
	const FString ReplacementHandle = ReplacementRef->GetStringField(TEXT("actor_handle"));
	First->DestroyActor(Replacement, false, false);
	TestFalse(TEXT("Destroyed actor reference is rejected"), Ok(UUnrealBridgeWorldLibrary::ValidateActorReference(ReplacementHandle)));
	DisposeWorld(First);
	BridgeWorldContext::BeginExecution();
	TestFalse(TEXT("Cleaned-up world reference is rejected"), Ok(UUnrealBridgeWorldLibrary::BeginWorldScope(FirstHandle)));
	return true;
}

#endif
