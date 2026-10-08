#include "UnrealBridgeDeterministicPIELibrary.h"

#include "UnrealBridgeNetworkSessionLibrary.h"
#include "UnrealBridgeWorldLibrary.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Commands/UICommandList.h"
#include "Kismet2/DebuggerCommands.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BridgeDeterministic
{
	struct FWorldCounter
	{
		TWeakObjectPtr<UWorld> World;
		FString Handle;
		int32 PIEInstance = -1;
		uint64 Ticks = 0;
		uint64 BeforeStep = 0;
		double LastGameTime = 0.0;
		bool bWasPaused = false;
	};
	struct FController
	{
		FString Id, Owner, RunId, PIESession, EditorSession;
		double FixedDelta = 0.0, PreviousDelta = 0.0;
		bool bPreviousFixed = false, bPending = false;
		FDelegateHandle TickHandle, EndPIEHandle;
		TArray<FWorldCounter> Worlds;
	};
	static TUniquePtr<FController> Active;
	static FString Json(const TSharedRef<FJsonObject>& Out)
	{
		FString Text;
		FJsonSerializer::Serialize(Out, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}
	static TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Result;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
		return Result;
	}
	static FString Error(const TCHAR* Code, const TCHAR* Message)
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("ok"), false);
		Out->SetStringField(TEXT("error_code"), Code);
		Out->SetStringField(TEXT("error"), Message);
		return Json(Out);
	}
	static void Restore()
	{
		if (!Active) return;
		FWorldDelegates::OnWorldTickEnd.Remove(Active->TickHandle);
		FEditorDelegates::EndPIE.Remove(Active->EndPIEHandle);
		for (const FWorldCounter& Entry : Active->Worlds)
			if (UWorld* World = Entry.World.Get()) World->bDebugPauseExecution = Entry.bWasPaused;
		FApp::SetFixedDeltaTime(Active->PreviousDelta);
		FApp::SetUseFixedTimeStep(Active->bPreviousFixed);
		Active.Reset();
	}
	static bool SameSession()
	{
		if (!Active) return false;
		const TSharedPtr<FJsonObject> Catalog = Parse(UUnrealBridgeWorldLibrary::GetWorldContexts(32));
		return Catalog && Catalog->GetStringField(TEXT("editor_session_id")) == Active->EditorSession
			&& Catalog->GetStringField(TEXT("pie_session_id")) == Active->PIESession;
	}
	static FString State()
	{
		if (!Active) return BridgeDeterministic::Error(TEXT("unknown_controller"), TEXT("No active controller"));
		auto Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("ok"), true);
		Out->SetStringField(TEXT("controller_id"), Active->Id);
		Out->SetStringField(TEXT("owner_id"), Active->Owner);
		Out->SetStringField(TEXT("pie_session_id"), Active->PIESession);
		Out->SetNumberField(TEXT("fixed_delta_seconds"), Active->FixedDelta);
		const bool bSame = SameSession();
		Out->SetBoolField(TEXT("session_current"), bSame);
		bool bExact = bSame && Active->bPending;
		bool bOverstep = false, bAllPaused = true;
		TArray<TSharedPtr<FJsonValue>> Worlds;
		for (const FWorldCounter& Entry : Active->Worlds)
		{
			UWorld* World = Entry.World.Get();
			const int64 Delta = static_cast<int64>(Entry.Ticks - Entry.BeforeStep);
			bExact &= World && Delta == 1;
			bOverstep |= Delta > 1;
			bAllPaused &= World && World->bDebugPauseExecution;
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("world_handle"), Entry.Handle);
			Row->SetNumberField(TEXT("pie_instance"), Entry.PIEInstance);
			Row->SetNumberField(TEXT("ticks"), Entry.Ticks);
			Row->SetNumberField(TEXT("ticks_before"), Entry.BeforeStep);
			Row->SetNumberField(TEXT("delta"), Delta);
			Row->SetBoolField(TEXT("paused"), World && World->bDebugPauseExecution);
			Worlds.Add(MakeShared<FJsonValueObject>(Row));
		}
		Out->SetArrayField(TEXT("worlds"), Worlds);
		Out->SetBoolField(TEXT("paused_after"), bAllPaused);
		const FString Status = !bSame ? TEXT("stale") : bOverstep ? TEXT("known_limited")
			: Active->bPending && bExact && bAllPaused ? TEXT("step_complete")
			: Active->bPending ? TEXT("step_pending") : TEXT("ready");
		Out->SetStringField(TEXT("status"), Status);
		if (Status == TEXT("step_complete")) Active->bPending = false;
		if (Status == TEXT("known_limited"))
		{
			if (GEditor) GEditor->SetPIEWorldsPaused(true);
			Active->bPending = false;
		}
		return Json(Out);
	}
	static bool CheckOwner(const FString& ControllerId, const FString& OwnerId)
	{
		return Active && Active->Id == ControllerId && Active->Owner == OwnerId;
	}
}

FString UUnrealBridgeDeterministicPIELibrary::Begin(const FString& RunId, const FString& PIESessionId,
	const FString& OwnerId, double FixedDeltaSeconds)
{
	using namespace BridgeDeterministic;
	if (!IsInGameThread() || !GEditor || Active || OwnerId.IsEmpty() || OwnerId.Len() > 128
		|| !FMath::IsFinite(FixedDeltaSeconds) || FixedDeltaSeconds < 1.0 / 240.0 || FixedDeltaSeconds > 0.1)
		return BridgeDeterministic::Error(TEXT("invalid_request"), TEXT("GameThread, free controller, owner and fixed delta 1/240..0.1 required"));
	const auto Native = Parse(UUnrealBridgeNetworkSessionLibrary::GetNetworkSessionState(RunId));
	const auto Catalog = Parse(UUnrealBridgeWorldLibrary::GetWorldContexts(32));
	if (!Native || !Catalog || !Native->GetBoolField(TEXT("ok")) || Native->GetStringField(TEXT("status")) != TEXT("ready")
		|| Native->GetStringField(TEXT("owned_pie_session_id")) != PIESessionId
		|| Catalog->GetStringField(TEXT("pie_session_id")) != PIESessionId)
		return BridgeDeterministic::Error(TEXT("not_owned"), TEXT("Exact ready UnrealBridge network PIE ownership required"));
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Catalog->TryGetArrayField(TEXT("worlds"), Rows) || Catalog->GetBoolField(TEXT("truncated")))
		return BridgeDeterministic::Error(TEXT("world_catalog"), TEXT("Complete world catalog required"));
	auto Controller = MakeUnique<FController>();
	Controller->Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Controller->Owner = OwnerId;
	Controller->RunId = RunId;
	Controller->PIESession = PIESessionId;
	Controller->EditorSession = Catalog->GetStringField(TEXT("editor_session_id"));
	Controller->FixedDelta = FixedDeltaSeconds;
	Controller->PreviousDelta = FApp::GetFixedDeltaTime();
	Controller->bPreviousFixed = FApp::UseFixedTimeStep();
	for (const TSharedPtr<FJsonValue>& Value : *Rows)
	{
		const TSharedPtr<FJsonObject> Row = Value->AsObject();
		if (!Row || Row->GetStringField(TEXT("world_type")) != TEXT("PIE")) continue;
		UWorld* World = BridgeWorldContext::ResolveWorldHandle(Row->GetStringField(TEXT("world_handle")));
		if (!World) return BridgeDeterministic::Error(TEXT("stale_world"), TEXT("A PIE World handle expired"));
		FWorldCounter Entry;
		Entry.World = World;
		Entry.Handle = Row->GetStringField(TEXT("world_handle"));
		Entry.PIEInstance = Row->GetIntegerField(TEXT("pie_instance"));
		Entry.bWasPaused = World->bDebugPauseExecution;
		Entry.LastGameTime = World->GetTimeSeconds();
		Controller->Worlds.Add(Entry);
	}
	if (Controller->Worlds.IsEmpty() || Controller->Worlds.Num() > 4)
		return BridgeDeterministic::Error(TEXT("world_scope"), TEXT("One to four owned PIE Worlds required"));
	Active = MoveTemp(Controller);
	Active->TickHandle = FWorldDelegates::OnWorldTickEnd.AddLambda([](UWorld* World, ELevelTick, float)
	{
		if (!Active) return;
		for (FWorldCounter& Entry : Active->Worlds)
			if (Entry.World.Get() == World)
			{
				const double GameTime = World->GetTimeSeconds();
				// OnWorldTickEnd also fires while PIE is paused.  Count only
				// frames whose game clock actually advanced.
				if (GameTime > Entry.LastGameTime + UE_DOUBLE_SMALL_NUMBER) ++Entry.Ticks;
				Entry.LastGameTime = GameTime;
				break;
			}
		if (Active->bPending)
		{
			bool bAllAdvanced = true;
			for (const FWorldCounter& Entry : Active->Worlds)
				bAllAdvanced &= Entry.Ticks > Entry.BeforeStep;
			if (bAllAdvanced && GEditor) GEditor->SetPIEWorldsPaused(true);
		}
	});
	Active->EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([](bool) { Restore(); });
	GEditor->SetPIEWorldsPaused(true);
	FApp::SetFixedDeltaTime(FixedDeltaSeconds);
	FApp::SetUseFixedTimeStep(true);
	return State();
}

FString UUnrealBridgeDeterministicPIELibrary::RequestOneFrame(const FString& ControllerId, const FString& OwnerId)
{
	using namespace BridgeDeterministic;
	if (!CheckOwner(ControllerId, OwnerId)) return BridgeDeterministic::Error(TEXT("wrong_owner"), TEXT("Exact controller and owner required"));
	if (!SameSession()) return BridgeDeterministic::Error(TEXT("stale_controller"), TEXT("PIE or Editor session replaced"));
	if (Active->bPending) return BridgeDeterministic::Error(TEXT("step_pending"), TEXT("Reconcile previous request before another step"));
	for (FWorldCounter& Entry : Active->Worlds)
	{
		UWorld* World = Entry.World.Get();
		if (!World || !World->bDebugPauseExecution) return BridgeDeterministic::Error(TEXT("not_paused"), TEXT("All selected Worlds must be paused"));
		Entry.BeforeStep = Entry.Ticks;
	}
	const TSharedPtr<FUICommandList> Actions = FPlayWorldCommands::GlobalPlayWorldActions;
	const TSharedPtr<FUICommandInfo> Command = FPlayWorldCommands::Get().SingleFrameAdvance;
	if (!Actions || !Command || !Actions->CanExecuteAction(Command.ToSharedRef()))
		return BridgeDeterministic::Error(TEXT("actuator_unavailable"), TEXT("UE public SingleFrameAdvance command unavailable"));
	Active->bPending = true;
	if (!Actions->ExecuteAction(Command.ToSharedRef()))
	{
		Active->bPending = false;
		return BridgeDeterministic::Error(TEXT("actuator_failed"), TEXT("SingleFrameAdvance was not accepted"));
	}
	return State();
}

FString UUnrealBridgeDeterministicPIELibrary::Status(const FString& ControllerId, const FString& OwnerId)
{
	using namespace BridgeDeterministic;
	return CheckOwner(ControllerId, OwnerId) ? State()
		: BridgeDeterministic::Error(TEXT("wrong_owner"), TEXT("Exact controller and owner required"));
}

FString UUnrealBridgeDeterministicPIELibrary::End(const FString& ControllerId, const FString& OwnerId)
{
	using namespace BridgeDeterministic;
	if (!CheckOwner(ControllerId, OwnerId)) return BridgeDeterministic::Error(TEXT("wrong_owner"), TEXT("Exact controller and owner required"));
	if (Active->bPending) return BridgeDeterministic::Error(TEXT("step_pending"), TEXT("Reconcile pending frame first"));
	const bool bSame = SameSession();
	Restore();
	auto Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), bSame);
	Out->SetBoolField(TEXT("restored"), bSame);
	Out->SetStringField(TEXT("status"), bSame ? TEXT("ended") : TEXT("stale"));
	return Json(Out);
}
