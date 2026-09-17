#include "UnrealBridgeWorldLibrary.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealBridgeWorld, Log, All);

namespace BridgeWorldImpl
{
	struct FActorEntry
	{
		TWeakObjectPtr<AActor> Actor;
		FString WorldHandle;
		double LastUsed = 0.0;
	};
	struct FScopeEntry
	{
		FString Token;
		FString WorldHandle;
	};
	static FString SessionId;
	static FString PieSessionId;
	static FDelegateHandle BeginPieHandle, EndPieHandle;
	static void EnsurePieTracking()
	{
		if (BeginPieHandle.IsValid()) return;
		BeginPieHandle = FEditorDelegates::BeginPIE.AddLambda([](bool)
		{
			PieSessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		});
		EndPieHandle = FEditorDelegates::EndPIE.AddLambda([](bool) { PieSessionId.Reset(); });
		// A PIE session that predates this module cannot be adopted by a recipe.
		if (GEditor && GEditor->PlayWorld) PieSessionId = TEXT("unowned_existing_pie");
	}
	static TMap<FString, TWeakObjectPtr<UWorld>> Worlds;
	static TMap<FString, FActorEntry> Actors;
	static TArray<FScopeEntry> Scopes;
	static bool bExecuting = false;
	static bool bPoisoned = false;
	static FDelegateHandle CleanupHandle;
	static FDelegateHandle ReinstanceHandle;
	static constexpr int32 MaxActorReferences = 4096;
	static constexpr double ActorReferenceTtl = 15.0 * 60.0;

	FString Json(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
		return Out;
	}

	TSharedRef<FJsonObject> Result(bool bOk)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("ok"), bOk);
		Out->SetStringField(TEXT("schema"), TEXT("unrealbridge.world.v1"));
		Out->SetStringField(TEXT("editor_session_id"), SessionId);
		Out->SetStringField(TEXT("pie_session_id"), PieSessionId);
		return Out;
	}

	FString Error(const FString& Code, const FString& Message)
	{
		TSharedRef<FJsonObject> Out = Result(false);
		Out->SetStringField(TEXT("error_code"), Code);
		Out->SetStringField(TEXT("error"), Message);
		return Json(Out);
	}

	FString NewHandle(const TCHAR* Kind)
	{
		return FString::Printf(TEXT("ubr:%s:%s:%s"), Kind, *SessionId, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	bool IsLiveWorld(UWorld* World)
	{
		if (!IsValid(World) || World->bIsTearingDown || !GEngine)
		{
			return false;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() == World)
			{
				return true;
			}
		}
		return false;
	}

	UWorld* FindWorld(const FString& Handle)
	{
		const TWeakObjectPtr<UWorld>* Found = Worlds.Find(Handle);
		UWorld* World = Found ? Found->Get() : nullptr;
		return IsLiveWorld(World) ? World : nullptr;
	}

	void Prune()
	{
		for (auto It = Worlds.CreateIterator(); It; ++It)
		{
			if (!IsLiveWorld(It.Value().Get())) It.RemoveCurrent();
		}
		const double Now = FPlatformTime::Seconds();
		for (auto It = Actors.CreateIterator(); It; ++It)
		{
			AActor* Actor = It.Value().Actor.Get();
			if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->HasAnyFlags(RF_NewerVersionExists)
				|| !FindWorld(It.Value().WorldHandle) || Now - It.Value().LastUsed > ActorReferenceTtl)
			{
				It.RemoveCurrent();
			}
		}
	}

	FString HandleForWorld(UWorld* World)
	{
		for (const auto& Pair : Worlds)
		{
			if (Pair.Value.Get() == World) return Pair.Key;
		}
		if (Worlds.Num() >= 128) return FString();
		const FString Handle = NewHandle(TEXT("world"));
		Worlds.Add(Handle, World);
		return Handle;
	}

	FString WorldTypeName(EWorldType::Type Type)
	{
		switch (Type)
		{
		case EWorldType::Editor: return TEXT("Editor");
		case EWorldType::PIE: return TEXT("PIE");
		case EWorldType::Game: return TEXT("Game");
		case EWorldType::EditorPreview: return TEXT("EditorPreview");
		case EWorldType::GamePreview: return TEXT("GamePreview");
		default: return TEXT("Other");
		}
	}

	FString NetModeName(ENetMode Mode)
	{
		switch (Mode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}

	AActor* FromReference(const FString& Handle, UWorld* RequiredWorld, FString& OutError)
	{
		FActorEntry* Entry = Actors.Find(Handle);
		AActor* Actor = Entry ? Entry->Actor.Get() : nullptr;
		UWorld* World = Entry ? FindWorld(Entry->WorldHandle) : nullptr;
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->HasAnyFlags(RF_NewerVersionExists)
			|| !World || Actor->GetWorld() != World || FPlatformTime::Seconds() - Entry->LastUsed > ActorReferenceTtl)
		{
			OutError = TEXT("Actor reference is stale, expired, or belongs to another editor session.");
			return nullptr;
		}
		if (RequiredWorld && World != RequiredWorld)
		{
			OutError = TEXT("Actor reference does not belong to the selected world.");
			return nullptr;
		}
		Entry->LastUsed = FPlatformTime::Seconds();
		return Actor;
	}

	AActor* FindActor(const FString& Selector, UWorld* RequiredWorld, FString& OutError)
	{
		if (Selector.IsEmpty() || Selector.Len() > 4096)
		{
			OutError = TEXT("Actor selector must contain between 1 and 4096 characters.");
			return nullptr;
		}
		if (BridgeWorldContext::HasScope())
		{
			UWorld* Scoped = BridgeWorldContext::GetScopedWorld();
			if (!Scoped || (RequiredWorld && RequiredWorld != Scoped))
			{
				OutError = TEXT("World scope is invalid or conflicts with the requested world.");
				return nullptr;
			}
			RequiredWorld = Scoped;
		}
		if (Selector.StartsWith(TEXT("ubr:"))) return FromReference(Selector, RequiredWorld, OutError);
		if (!GEngine || (RequiredWorld && !IsLiveWorld(RequiredWorld)))
		{
			OutError = TEXT("World is no longer active.");
			return nullptr;
		}
		const bool bPath = Selector.StartsWith(TEXT("/")) && Selector.Contains(TEXT(":"));
		// Object paths and actor labels may be longer than an FName permits.
		// Compare those as strings; never intern an oversized remote selector.
		const bool bCanBeName = !bPath && Selector.Len() < NAME_SIZE;
		const FName Name = bCanBeName ? FName(*Selector, FNAME_Find) : NAME_None;
		AActor* Match = nullptr;
		FString OtherPath;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!IsLiveWorld(World) || (RequiredWorld && World != RequiredWorld)) continue;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->HasAnyFlags(RF_NewerVersionExists)) continue;
				if (Actor->GetPathName() == Selector) return Actor;
				if (!bPath && ((bCanBeName && Actor->GetFName() == Name) || Actor->GetActorLabel() == Selector))
				{
					if (!Match) Match = Actor;
					else if (Match != Actor && OtherPath.IsEmpty()) OtherPath = Actor->GetPathName();
				}
			}
		}
		if (!OtherPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Actor selector is ambiguous (including '%s' and '%s'); use an exact path or actor handle."), *Match->GetPathName(), *OtherPath);
			return nullptr;
		}
		if (!Match) OutError = TEXT("Actor was not found in the requested world.");
		return Match;
	}
}

void BridgeWorldContext::Startup()
{
	using namespace BridgeWorldImpl;
	check(IsInGameThread());
	SessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	EnsurePieTracking();
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World, bool, bool)
	{
		for (auto It = Worlds.CreateIterator(); It; ++It)
		{
			if (It.Value().Get() == World) It.RemoveCurrent();
		}
		Prune();
	});
	ReinstanceHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddLambda([](const TMap<UObject*, UObject*>& Replaced)
	{
		for (auto It = Actors.CreateIterator(); It; ++It)
		{
			if (Replaced.Contains(It.Value().Actor.Get())) It.RemoveCurrent();
		}
		for (auto It = Worlds.CreateIterator(); It; ++It)
		{
			if (Replaced.Contains(It.Value().Get())) It.RemoveCurrent();
		}
		Prune();
	});
}

void BridgeWorldContext::Shutdown()
{
	using namespace BridgeWorldImpl;
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	FCoreUObjectDelegates::OnObjectsReinstanced.Remove(ReinstanceHandle);
	FEditorDelegates::BeginPIE.Remove(BeginPieHandle);
	FEditorDelegates::EndPIE.Remove(EndPieHandle);
	BeginPieHandle.Reset();
	EndPieHandle.Reset();
	PieSessionId.Reset();
	CleanupHandle.Reset();
	ReinstanceHandle.Reset();
	Worlds.Reset();
	Actors.Reset();
	EndExecution();
	SessionId.Reset();
}

void BridgeWorldContext::BeginExecution()
{
	check(IsInGameThread());
	BridgeWorldImpl::Scopes.Reset();
	BridgeWorldImpl::bPoisoned = false;
	BridgeWorldImpl::bExecuting = true;
	BridgeWorldImpl::Prune();
}

void BridgeWorldContext::EndExecution()
{
	BridgeWorldImpl::Scopes.Reset();
	BridgeWorldImpl::bPoisoned = false;
	BridgeWorldImpl::bExecuting = false;
}

bool BridgeWorldContext::HasScope()
{
	return BridgeWorldImpl::bExecuting && (BridgeWorldImpl::bPoisoned || !BridgeWorldImpl::Scopes.IsEmpty());
}

UWorld* BridgeWorldContext::GetScopedWorld()
{
	using namespace BridgeWorldImpl;
	if (!bExecuting || bPoisoned || Scopes.IsEmpty()) return nullptr;
	UWorld* World = FindWorld(Scopes.Last().WorldHandle);
	if (!World) bPoisoned = true;
	return World;
}

AActor* BridgeWorldContext::ResolveActor(const FString& Selector, UWorld* RequiredWorld)
{
	if (!IsInGameThread()) return nullptr;
	FString Error;
	AActor* Actor = BridgeWorldImpl::FindActor(Selector, RequiredWorld, Error);
	if (!Actor && !Error.IsEmpty()) UE_LOG(LogUnrealBridgeWorld, Warning, TEXT("%s"), *Error);
	return Actor;
}

FString UUnrealBridgeWorldLibrary::GetWorldContexts(int32 MaxWorlds)
{
	using namespace BridgeWorldImpl;
	if (!IsInGameThread()) return BridgeWorldImpl::Error(TEXT("wrong_thread"), TEXT("World queries require the GameThread."));
	EnsurePieTracking();
	Prune();
	TArray<TSharedPtr<FJsonValue>> Items;
	TSet<UWorld*> Seen;
	int32 Total = 0;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!IsLiveWorld(World) || Seen.Contains(World)) continue;
			Seen.Add(World);
			++Total;
			if (Items.Num() >= FMath::Clamp(MaxWorlds, 1, 128)) continue;
			const FString Handle = HandleForWorld(World);
			if (Handle.IsEmpty()) continue;
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("world_handle"), Handle);
			Item->SetStringField(TEXT("world_path"), World->GetPathName());
			Item->SetStringField(TEXT("world_type"), WorldTypeName(World->WorldType));
			Item->SetStringField(TEXT("net_mode"), NetModeName(World->GetNetMode()));
			Item->SetNumberField(TEXT("pie_instance"), Context.PIEInstance);
			Item->SetBoolField(TEXT("has_begun_play"), World->HasBegunPlay());
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}
	}
	TSharedRef<FJsonObject> Out = Result(true);
	Out->SetArrayField(TEXT("worlds"), Items);
	Out->SetNumberField(TEXT("total_worlds"), Total);
	Out->SetBoolField(TEXT("truncated"), Items.Num() < Total);
	return Json(Out);
}

FString UUnrealBridgeWorldLibrary::BeginWorldScope(const FString& WorldHandle)
{
	using namespace BridgeWorldImpl;
	if (!IsInGameThread() || !bExecuting) return BridgeWorldImpl::Error(TEXT("no_execution"), TEXT("World scopes require a bridge job execution slice."));
	UWorld* World = FindWorld(WorldHandle);
	if (bPoisoned || !World || Scopes.Num() >= 16)
	{
		bPoisoned = true;
		return BridgeWorldImpl::Error(TEXT("invalid_world_scope"), TEXT("World is stale, scope is invalid, or nesting exceeds 16."));
	}
	FScopeEntry Entry{NewHandle(TEXT("scope")), WorldHandle};
	Scopes.Add(Entry);
	TSharedRef<FJsonObject> Out = Result(true);
	Out->SetStringField(TEXT("scope_token"), Entry.Token);
	Out->SetStringField(TEXT("world_handle"), WorldHandle);
	Out->SetStringField(TEXT("world_path"), World->GetPathName());
	return Json(Out);
}

FString UUnrealBridgeWorldLibrary::EndWorldScope(const FString& ScopeToken)
{
	using namespace BridgeWorldImpl;
	if (!IsInGameThread() || !bExecuting) return BridgeWorldImpl::Error(TEXT("no_execution"), TEXT("There is no active bridge execution."));
	if (Scopes.IsEmpty() || Scopes.Last().Token != ScopeToken)
	{
		bPoisoned = true;
		return BridgeWorldImpl::Error(TEXT("scope_order_mismatch"), TEXT("End scopes in reverse nesting order within the same job slice."));
	}
	Scopes.Pop(EAllowShrinking::No);
	if (bPoisoned)
	{
		return BridgeWorldImpl::Error(TEXT("invalid_world_scope"), TEXT("This execution slice encountered an invalid world selection or scope order."));
	}
	return Json(Result(true));
}

FString UUnrealBridgeWorldLibrary::ResolveActorReference(const FString& WorldHandle, const FString& ActorNameOrPath)
{
	using namespace BridgeWorldImpl;
	if (!IsInGameThread()) return BridgeWorldImpl::Error(TEXT("wrong_thread"), TEXT("Actor resolution requires the GameThread."));
	Prune();
	UWorld* World = FindWorld(WorldHandle);
	if (!World) return BridgeWorldImpl::Error(TEXT("stale_world"), TEXT("World handle is no longer valid in this editor session."));
	FString Message;
	AActor* Actor = FindActor(ActorNameOrPath, World, Message);
	if (!Actor) return BridgeWorldImpl::Error(TEXT("actor_resolution_failed"), Message);
	FString Handle;
	for (auto& Pair : Actors)
	{
		if (Pair.Value.Actor.Get() == Actor && Pair.Value.WorldHandle == WorldHandle)
		{
			Pair.Value.LastUsed = FPlatformTime::Seconds();
			Handle = Pair.Key;
			break;
		}
	}
	if (Handle.IsEmpty())
	{
		if (Actors.Num() >= MaxActorReferences) return BridgeWorldImpl::Error(TEXT("reference_capacity"), TEXT("Actor reference capacity reached; unused handles expire after 15 minutes."));
		Handle = NewHandle(TEXT("actor"));
		Actors.Add(Handle, FActorEntry{Actor, WorldHandle, FPlatformTime::Seconds()});
	}
	TSharedRef<FJsonObject> Out = Result(true);
	Out->SetStringField(TEXT("actor_handle"), Handle);
	Out->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Out->SetStringField(TEXT("world_handle"), WorldHandle);
	Out->SetNumberField(TEXT("idle_ttl_seconds"), ActorReferenceTtl);
	return Json(Out);
}

FString UUnrealBridgeWorldLibrary::ValidateActorReference(const FString& ActorHandle)
{
	using namespace BridgeWorldImpl;
	if (!IsInGameThread()) return BridgeWorldImpl::Error(TEXT("wrong_thread"), TEXT("Actor validation requires the GameThread."));
	if (!ActorHandle.StartsWith(TEXT("ubr:actor:"))) return BridgeWorldImpl::Error(TEXT("invalid_actor_handle"), TEXT("An opaque actor handle is required."));
	FString Message;
	AActor* Actor = FindActor(ActorHandle, nullptr, Message);
	if (!Actor) return BridgeWorldImpl::Error(TEXT("invalid_actor_reference"), Message);
	TSharedRef<FJsonObject> Out = Result(true);
	Out->SetStringField(TEXT("actor_handle"), ActorHandle);
	Out->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Out->SetStringField(TEXT("world_handle"), Actors.FindChecked(ActorHandle).WorldHandle);
	return Json(Out);
}
