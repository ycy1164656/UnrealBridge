#include "UnrealBridgeNetworkingLibrary.h"
#include "UnrealBridgeWorldLibrary.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeNetworkingLibrary"

namespace BridgeNetworkingImpl
{
	AActor* FindActor(const FString& NameOrLabel)
	{
		return BridgeWorldContext::ResolveActor(NameOrLabel);
	}

	FString DormancyToString(ENetDormancy Dormancy)
	{
		switch (Dormancy)
		{
		case DORM_Never: return TEXT("Never");
		case DORM_Awake: return TEXT("Awake");
		case DORM_DormantAll: return TEXT("DormantAll");
		case DORM_DormantPartial: return TEXT("DormantPartial");
		case DORM_Initial: return TEXT("Initial");
		default: return TEXT("Unknown");
		}
	}

	ENetDormancy ParseDormancy(const FString& Dormancy)
	{
		const FString Normalized = Dormancy.ToLower();
		if (Normalized == TEXT("never") || Normalized == TEXT("dorm_never")) return DORM_Never;
		if (Normalized == TEXT("dormantall") || Normalized == TEXT("dorm_all") || Normalized == TEXT("dorm_dormantall")) return DORM_DormantAll;
		if (Normalized == TEXT("dormantpartial") || Normalized == TEXT("partial") || Normalized == TEXT("dorm_dormantpartial")) return DORM_DormantPartial;
		if (Normalized == TEXT("initial") || Normalized == TEXT("dorm_initial")) return DORM_Initial;
		return DORM_Awake;
	}

	FBridgeActorNetworkingInfo MakeInfo(AActor* Actor)
	{
		FBridgeActorNetworkingInfo Info;
		if (!Actor)
		{
			return Info;
		}
		Info.ActorLabel = Actor->GetActorLabel();
		Info.ActorPath = Actor->GetPathName();
		Info.bReplicates = Actor->GetIsReplicated();
		Info.bReplicateMovement = Actor->IsReplicatingMovement();
		Info.bAlwaysRelevant = Actor->bAlwaysRelevant;
		Info.NetDormancy = DormancyToString(Actor->NetDormancy);
		Info.NetUpdateFrequency = Actor->GetNetUpdateFrequency();
		Info.MinNetUpdateFrequency = Actor->GetMinNetUpdateFrequency();
		Info.NetPriority = Actor->NetPriority;
		Info.NetCullDistanceSquared = Actor->GetNetCullDistanceSquared();
		return Info;
	}

	FString RoleToString(ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_None: return TEXT("None");
		case ROLE_SimulatedProxy: return TEXT("SimulatedProxy");
		case ROLE_AutonomousProxy: return TEXT("AutonomousProxy");
		case ROLE_Authority: return TEXT("Authority");
		default: return TEXT("Unknown");
		}
	}

	FString NetModeToString(ENetMode Mode)
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

	FString WorldTypeToString(EWorldType::Type Type)
	{
		switch (Type)
		{
		case EWorldType::Editor: return TEXT("Editor");
		case EWorldType::PIE: return TEXT("PIE");
		case EWorldType::Game: return TEXT("Game");
		case EWorldType::GamePreview: return TEXT("GamePreview");
		case EWorldType::EditorPreview: return TEXT("EditorPreview");
		default: return TEXT("Other");
		}
	}

	FBridgeNetworkActorRuntimeInfo MakeRuntimeInfo(AActor* Actor)
	{
		FBridgeNetworkActorRuntimeInfo Info;
		if (!Actor)
		{
			return Info;
		}
		Info.ActorName = Actor->GetName();
		Info.ActorPath = Actor->GetPathName();
		Info.ClassPath = Actor->GetClass()->GetPathName();
		Info.OwnerPath = IsValid(Actor->GetOwner()) ? Actor->GetOwner()->GetPathName() : FString();
		Info.LocalRole = RoleToString(Actor->GetLocalRole());
		Info.RemoteRole = RoleToString(Actor->GetRemoteRole());
		Info.Dormancy = DormancyToString(Actor->NetDormancy);
		if (const UNetConnection* Connection = Actor->GetNetConnection())
		{
			Info.NetConnectionPath = Connection->GetPathName();
		}
		Info.bReplicates = Actor->GetIsReplicated();
		Info.bTearOff = Actor->GetTearOff();
		Info.NetUpdateFrequency = Actor->GetNetUpdateFrequency();
		Info.MinNetUpdateFrequency = Actor->GetMinNetUpdateFrequency();
		Info.NetPriority = Actor->NetPriority;
		return Info;
	}

	UClass* ResolveClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}
		if (UClass* Class = LoadObject<UClass>(nullptr, *ClassPath))
		{
			return Class;
		}
		return FindObject<UClass>(nullptr, *ClassPath);
	}

	FBridgeNetworkClassAudit MakeClassAudit(UClass* Class, int32 MaxProperties, int32 MaxRPCs)
	{
		FBridgeNetworkClassAudit Audit;
		if (!Class)
		{
			return Audit;
		}
		Audit.ClassPath = Class->GetPathName();
		MaxProperties = FMath::Clamp(MaxProperties, 0, 16384);
		MaxRPCs = FMath::Clamp(MaxRPCs, 0, 8192);

		for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Net))
			{
				continue;
			}
			++Audit.TotalReplicatedPropertyCount;
			if (Audit.ReplicatedProperties.Num() >= MaxProperties)
			{
				continue;
			}
			FBridgeReplicatedPropertyInfo Info;
			Info.Name = Property->GetName();
			Info.OwnerClass = Property->GetOwnerClass() ? Property->GetOwnerClass()->GetPathName() : FString();
			Info.CppType = Property->GetCPPType();
			Info.RepNotifyFunction = Property->RepNotifyFunc.ToString();
			Audit.ReplicatedProperties.Add(MoveTemp(Info));
		}

		for (TFieldIterator<UFunction> It(Class, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			UFunction* Function = *It;
			if (!Function || !Function->HasAnyFunctionFlags(FUNC_Net))
			{
				continue;
			}
			++Audit.TotalRPCCount;
			if (Audit.RPCs.Num() >= MaxRPCs)
			{
				continue;
			}
			FBridgeRPCFunctionInfo Info;
			Info.Name = Function->GetName();
			Info.OwnerClass = Function->GetOwnerClass() ? Function->GetOwnerClass()->GetPathName() : FString();
			if (Function->HasAnyFunctionFlags(FUNC_NetServer)) Info.Direction = TEXT("Server");
			else if (Function->HasAnyFunctionFlags(FUNC_NetClient)) Info.Direction = TEXT("Client");
			else if (Function->HasAnyFunctionFlags(FUNC_NetMulticast)) Info.Direction = TEXT("NetMulticast");
			else Info.Direction = TEXT("Net");
			Info.bReliable = Function->HasAnyFunctionFlags(FUNC_NetReliable);
			Info.bWithValidation = Function->HasAnyFunctionFlags(FUNC_NetValidate);
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
			{
				FProperty* Parameter = *ParamIt;
				if (Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm))
				{
					Info.Parameters.Add(FString::Printf(TEXT("%s %s"), *Parameter->GetCPPType(), *Parameter->GetName()));
				}
			}
			Audit.RPCs.Add(MoveTemp(Info));
		}
		return Audit;
	}
}

FBridgeActorNetworkingInfo UUnrealBridgeNetworkingLibrary::GetActorNetworkingInfo(const FString& ActorNameOrLabel)
{
	return BridgeNetworkingImpl::MakeInfo(BridgeNetworkingImpl::FindActor(ActorNameOrLabel));
}

bool UUnrealBridgeNetworkingLibrary::SetActorReplicates(const FString& ActorNameOrLabel, bool bReplicates)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorReplicates", "Bridge: Set Actor Replicates"));
	Actor->Modify();
	Actor->SetReplicates(bReplicates);
	Actor->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeNetworkingLibrary::SetActorReplicateMovement(const FString& ActorNameOrLabel, bool bReplicateMovement)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorReplicateMovement", "Bridge: Set Actor Replicate Movement"));
	Actor->Modify();
	Actor->SetReplicateMovement(bReplicateMovement);
	Actor->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeNetworkingLibrary::SetActorAlwaysRelevant(const FString& ActorNameOrLabel, bool bAlwaysRelevant)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorAlwaysRelevant", "Bridge: Set Actor Always Relevant"));
	Actor->Modify();
	Actor->bAlwaysRelevant = bAlwaysRelevant;
	Actor->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeNetworkingLibrary::SetActorNetDormancy(const FString& ActorNameOrLabel, const FString& Dormancy)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorDormancy", "Bridge: Set Actor Net Dormancy"));
	Actor->Modify();
	Actor->NetDormancy = BridgeNetworkingImpl::ParseDormancy(Dormancy);
	Actor->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeNetworkingLibrary::SetActorNetUpdateSettings(
	const FString& ActorNameOrLabel,
	float NetUpdateFrequency,
	float MinNetUpdateFrequency,
	float NetPriority,
	float NetCullDistanceSquared)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorNetUpdate", "Bridge: Set Actor Net Update Settings"));
	Actor->Modify();
	if (NetUpdateFrequency >= 0.0f)
	{
		Actor->SetNetUpdateFrequency(NetUpdateFrequency);
	}
	if (MinNetUpdateFrequency >= 0.0f)
	{
		Actor->SetMinNetUpdateFrequency(MinNetUpdateFrequency);
	}
	if (NetPriority >= 0.0f)
	{
		Actor->NetPriority = NetPriority;
	}
	if (NetCullDistanceSquared >= 0.0f)
	{
		Actor->SetNetCullDistanceSquared(NetCullDistanceSquared);
	}
	Actor->MarkPackageDirty();
	return true;
}

FBridgeNetworkClassAudit UUnrealBridgeNetworkingLibrary::AuditNetworkClass(
	const FString& ClassPath,
	int32 MaxProperties,
	int32 MaxRPCs)
{
	return BridgeNetworkingImpl::MakeClassAudit(
		BridgeNetworkingImpl::ResolveClass(ClassPath), MaxProperties, MaxRPCs);
}

FBridgeNetworkActorAudit UUnrealBridgeNetworkingLibrary::AuditNetworkActor(
	const FString& ActorNameOrPath,
	int32 MaxProperties,
	int32 MaxRPCs)
{
	FBridgeNetworkActorAudit Audit;
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrPath);
	if (!Actor)
	{
		return Audit;
	}
	if (UWorld* World = Actor->GetWorld())
	{
		Audit.World = World->GetPathName();
		Audit.WorldType = BridgeNetworkingImpl::WorldTypeToString(World->WorldType);
		Audit.NetMode = BridgeNetworkingImpl::NetModeToString(World->GetNetMode());
	}
	Audit.Settings = BridgeNetworkingImpl::MakeInfo(Actor);
	Audit.Runtime = BridgeNetworkingImpl::MakeRuntimeInfo(Actor);
	Audit.ClassAudit = BridgeNetworkingImpl::MakeClassAudit(Actor->GetClass(), MaxProperties, MaxRPCs);
	return Audit;
}

TArray<FBridgeNetworkWorldSnapshot> UUnrealBridgeNetworkingLibrary::GetNetworkWorldSnapshots(
	bool bReplicatedActorsOnly,
	int32 MaxActorsPerWorld)
{
	TArray<FBridgeNetworkWorldSnapshot> Result;
	MaxActorsPerWorld = FMath::Clamp(MaxActorsPerWorld, 1, 32768);
	if (!GEngine)
	{
		return Result;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game && World->WorldType != EWorldType::GamePreview))
		{
			continue;
		}
		FBridgeNetworkWorldSnapshot Snapshot;
		Snapshot.World = World->GetPathName();
		Snapshot.WorldType = BridgeNetworkingImpl::WorldTypeToString(World->WorldType);
		Snapshot.NetMode = BridgeNetworkingImpl::NetModeToString(World->GetNetMode());
		Snapshot.PIEInstance = Context.PIEInstance;
		if (const UNetDriver* NetDriver = World->GetNetDriver())
		{
			Snapshot.NetDriverPath = NetDriver->GetPathName();
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}
			++Snapshot.TotalActorCount;
			if (Actor->GetIsReplicated())
			{
				++Snapshot.TotalReplicatedActorCount;
			}
			if ((bReplicatedActorsOnly && !Actor->GetIsReplicated()) || Snapshot.Actors.Num() >= MaxActorsPerWorld)
			{
				continue;
			}
			Snapshot.Actors.Add(BridgeNetworkingImpl::MakeRuntimeInfo(Actor));
		}
		Result.Add(MoveTemp(Snapshot));
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
